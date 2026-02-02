# Sensor Manager Architecture Documentation

## Table of Contents

1. [System Overview](#system-overview)
2. [State Machine Design](#state-machine-design)
3. [Power Management Strategy](#power-management-strategy)
4. [I2C Pull-up Calculations](#i2c-pull-up-calculations)
5. [Memory Map](#memory-map)
6. [Flash Write Endurance Strategy](#flash-write-endurance-strategy)
7. [Error Handling](#error-handling)
8. [Communication Protocols](#communication-protocols)

---

## System Overview

The Sensor Manager firmware implements a modular architecture with the following components:

```
┌─────────────────────────────────────────────────────────────────┐
│                        main.c                                  │
│                    (Application Entry)                         │
└──────────────┬──────────────────────────────┬─────────────────┘
               │                              │
               ▼                              ▼
    ┌─────────────────────┐        ┌─────────────────────┐
    │  state_machine.c    │        │  power_manager.c    │
    │                     │        │                     │
    │  ┌───────────────┐  │        │  ┌───────────────┐  │
    │  │ INIT          │  │        │  │ RTC Config    │  │
    │  │ IDLE          │  │        │  │ STOP Mode     │  │
    │  │ READ          │  │        │  │ Wakeup Sources│  │
    │  │ TRANSMIT      │  │        │  └───────────────┘  │
    │  │ ERROR         │  │        │                     │
    │  └───────────────┘  │        └─────────────────────┘
    └─────────────────────┘
               │
        ┌──────┼──────┬──────┐
        ▼      ▼      ▼      ▼
   ┌────────┐ ┌────┐ ┌─────┐ ┌────┐
   │bme280.c│ │gps.c│ │fuel │ │UART│
   │(I2C)   │ │(UART)│ │(ADC)│ │TX  │
   └────────┘ └────┘ └─────┘ └────┘
```

---

## State Machine Design

### State Definitions

| State | Purpose | Entry Action | Exit Action |
|-------|---------|--------------|-------------|
| **INIT** | Initialize all sensors and peripherals | Call init for all sensors | None |
| **IDLE** | Wait for measurement interval | Start timer | Check timeout |
| **READ** | Read all sensors with timeout | Read BME280, GPS, Fuel | Validate data |
| **TRANSMIT** | Send data via UART | Format JSON, transmit | Clear buffer |
| **ERROR** | Handle errors | Log error, update LED | Retry after delay |

### State Transitions

```
              power_on / reset
                    │
                    ▼
┌─────────────────────────────────────────┐
│                 INIT                    │
│  - Initialize I2C                       │
│  - Initialize UART                      │
│  - Initialize ADC                       │
│  - Initialize sensors                   │
└────────────────┬────────────────────────┘
                 │ success
                 ▼
┌─────────────────────────────────────────┐
│                 IDLE                    │
│  - Wait for 5 second interval           │
└────────────────┬────────────────────────┘
                 │ timeout (5s)
                 ▼
┌─────────────────────────────────────────┐
│                 READ                    │
│  - Read BME280 (I2C, 100ms timeout)     │
│  - Read Fuel Sensor (ADC)               │
│  - Check GPS data freshness             │
└────────────────┬────────────────────────┘
                 │
         ┌───────┴───────┐
         │ data ready    │ error count > 5
         ▼               ▼
┌─────────────────┐  ┌─────────────────┐
│   TRANSMIT      │  │     ERROR       │
│  - Format JSON  │  │  - Log error    │
│  - Send via UART│  │  - LED Red ON   │
└────────┬────────┘  │  - Wait 1s      │
         │           │  - Retry        │
         │           └────────┬────────┘
         │                    │
         └────────────────────┘
                    │
                    ▼
               (back to IDLE)
```

### Error Recovery

```
┌──────────────────────────────────────────┐
│  Error Detection                         │
│  ┌────────────────────────────────────┐  │
│  │ 1. I2C timeout (BME280)            │  │
│  │ 2. GPS checksum failure            │  │
│  │ 3. ADC out of range (Fuel)         │  │
│  │ 4. Sensor not responding           │  │
│  └────────────────────────────────────┘  │
│                   │                      │
│                   ▼                      │
│  ┌────────────────────────────────────┐  │
│  │ Increment error counter            │  │
│  │ Set error_code                     │  │
│  │ Update last_error_time             │  │
│  └────────────────────────────────────┘  │
│                   │                      │
│         ┌─────────┴─────────┐            │
│         │                   │            │
│    error_count < 5    error_count >= 5   │
│         │                   │            │
│         ▼                   ▼            │
│  ┌───────────┐      ┌───────────┐       │
│  │ Continue  │      │  ERROR    │       │
│  │ (partial  │      │  STATE    │       │
│  │  data OK) │      └───────────┘       │
│  └───────────┘                           │
└──────────────────────────────────────────┘
```

---

## Power Management Strategy

### STOP Mode Entry Conditions

```
                    ┌──────────────────┐
                    │   Check every    │
                    │   main loop      │
                    └─────────┬────────┘
                              │
                ┌─────────────┼─────────────┐
                │                             │
         GPS lost for >30s           No activity for >30s
        (last_gps_time)              (last_activity_time)
                │                             │
                └─────────────┬───────────────┘
                              │
                              ▼
                    ┌──────────────────┐
                    │  ENTER STOP MODE │
                    └─────────┬────────┘
                              │
        ┌─────────────────────┼─────────────────────┐
        │                     │                     │
        ▼                     ▼                     ▼
┌───────────────┐    ┌───────────────┐    ┌───────────────┐
│  Save Context │    │  Disable IRQ  │    │  Halt CPU     │
│  - State      │    │  (selective)  │    │  Keep SRAM    │
│  - Timestamp  │    │               │    │  Wake on      │
└───────────────┘    └───────────────┘    │  RTC/UART/BTN │
                                        └───────────────┘
```

### STOP Mode Wakeup Sources

| Source | Trigger | Latency | Action |
|--------|---------|---------|--------|
| **RTC Alarm** | Every 5 seconds | ~100 μs | Periodic sensor check |
| **UART RX** | GPS data received | ~50 μs | Process incoming GPS |
| **EXTI (Button)** | User button press | ~40 μs | Manual wakeup |

### STOP Mode Power Consumption

```
Run Mode:  ~50 mA @ 3.3V = 165 mW
Stop Mode: ~100 μA @ 3.3V = 0.33 mW
Savings:   99.8% power reduction
```

### Context Save/Restore

```c
/* Before STOP */
void save_context(void) {
    // Save state machine state
    RTC->BKP0R = state_machine.current_state;
    RTC->BKP1R = state_machine.error_count;
    // More critical data...
}

/* After wakeup */
void restore_context(void) {
    // Restore state
    state_machine.current_state = RTC->BKP0R;
    state_machine.error_count = RTC->BKP1R;
    // Reinitialize clocks
    SystemClock_Config();
}
```

---

## I2C Pull-up Calculations

### Bus Parameters

| Parameter | Value |
|-----------|-------|
| I2C Frequency | 100 kHz (Standard mode) |
| Bus Capacitance (C<sub>b</sub>) | ~100 pF (estimated) |
| Rise Time (t<sub>r</sub>) | ≤ 1000 ns (for 100 kHz) |

### Pull-up Resistor Calculation

```
Formula: R_pullup = (t_r) / (0.8473 × C_bus)

For 100 kHz with 100 pF:
R_pullup = (1000 ns) / (0.8473 × 100 pF)
         = 1000 × 10⁻⁹ / (0.8473 × 100 × 10⁻¹²)
         = 11.8 kΩ
```

### Selection

| Calculated | Standard Value | Margin |
|------------|----------------|--------|
| 11.8 kΩ | 4.7 kΩ | Conservative ×2.5 |

**Rationale**: Using 4.7 kΩ provides:
- Faster rise times (~400 ns)
- Lower impedance, better noise immunity
- Still within I2C sink current spec (3 mA)

### Current Draw

```
I_pullup = VDD / R_pullup
         = 3.3V / 4700Ω
         = 0.7 mA per line
Total (SDA+SCL) = ~1.4 mA
```

---

## Memory Map

### STM32F407VGTx Memory Layout

```
┌─────────────────────────────────────────────────────────────┐
│                    FLASH (1024 KB)                          │
├─────────────────────────────────────────────────────────────┤
│ 0x0800_0000  │  Vector Table & ISR                          │
│ 0x0800_0100  │  .text (Code)                               │
│              │  - Startup code                             │
│              │  - HAL drivers                              │
│              │  - Application code                         │
├─────────────────────────────────────────────────────────────┤
│ 0x0800_0000  │  .rodata (Constants)                        │
│              │  - Calibration data                         │
│              │  - String literals                          │
├─────────────────────────────────────────────────────────────┤
│ 0x0807_F000  │  .data (initialized variables, copied to RAM)│
├─────────────────────────────────────────────────────────────┤
│ 0x0808_0000  │  Reserved for OTA (future)                  │
│ 0x080F_FFFF  │  End of FLASH                               │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                     RAM (128 KB)                            │
├─────────────────────────────────────────────────────────────┤
│ 0x2000_0000  │  .data (initialized data)                   │
│ 0x2000_1000  │  .bss (zero-initialized data)               │
│              │  - Global state machine                     │
│              │  - Sensor data structures                   │
│              │  - GPS buffer (256 bytes)                   │
│              │  - Moving average buffer                    │
├─────────────────────────────────────────────────────────────┤
│ 0x2001_D000  │  Heap (malloc, if used)                     │
│              │  Heap size: 512 bytes                       │
├─────────────────────────────────────────────────────────────┤
│ 0x2001_F000  │  Stack (main thread)                        │
│              │  Stack size: 1024 bytes                     │
│ 0x2001_FFFF  │  End of RAM                                 │
└─────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────┐
│                   CCMRAM (64 KB)                            │
├─────────────────────────────────────────────────────────────┤
│ 0x1000_0000  │  Available (not used in current design)     │
│ 0x1000_FFFF  │  End of CCMRAM                              │
│              │  Could be used for:                         │
│              │  - DMA buffers                              │
│              │  - Critical data (no DMA access)            │
└─────────────────────────────────────────────────────────────┘
```

### Memory Usage Summary

| Region | Size | Usage | Free |
|--------|------|-------|------|
| FLASH | 1024 KB | ~30 KB | ~994 KB |
| RAM | 128 KB | ~5 KB | ~123 KB |
| CCMRAM | 64 KB | 0 KB | 64 KB |

---

## Flash Write Endurance Strategy

### Problem

STM32F407 FLASH endurance: **10,000 write/erase cycles**

### Strategy

```
┌─────────────────────────────────────────────────────────────┐
│  Minimize FLASH writes by:                                 │
│                                                             │
│  1. Store configuration in RTC backup registers (SRAM)     │
│     → Survives VDD removal, no FLASH wear                  │
│                                                             │
│  2. Wear-leveling for persistent logs                      │
│     → Round-robin across 32 sectors                        │
│     → Each sector: 128 KB, 10k cycles                      │
│     → Total: 320k log writes                               │
│                                                             │
│  3. Journaling for critical data                           │
│     → Write new location, update pointer                   │
│     → Validated by CRC on read                             │
│                                                             │
│  4. Lazy write-back                                        │
│     → Cache writes in RAM                                  │
│     → Only flush on:                                      │
│       - Power loss detected                                │
│       - Cache full                                         │
│       - Periodic (every 1 hour)                            │
└─────────────────────────────────────────────────────────────┘
```

### Backup Register Usage

| Register | Data | Size | Persistence |
|----------|------|------|-------------|
| RTC_BKP0R | State | 32-bit | V<sub>BAT</sub> |
| RTC_BKP1R | Error count | 32-bit | V<sub>BAT</sub> |
| RTC_BKP2R | Boot counter | 32-bit | V<sub>BAT</sub> |
| RTC_BKP3R | Config flags | 32-bit | V<sub>BAT</sub> |

---

## Error Handling

### Error Categories

| Category | Detection | Action |
|----------|-----------|--------|
| **Sensor** | I2C NACK, ADC out of range | Retry 3×, log |
| **Communication** | Checksum fail, timeout | Discard, increment counter |
| **System** | Stack overflow, hard fault | Reset, log to backup |
| **Power** | Brownout detected | Enter safe mode |

### Error Logging

```c
typedef struct {
    uint32_t timestamp;
    error_code_t code;
    uint8_t sensor_id;
    uint16_t additional_info;
} error_log_entry_t;

// Circular buffer in RAM, periodically flushed to FLASH
error_log_entry_t error_log[32];
uint8_t error_log_head = 0;
```

---

## Communication Protocols

### I2C (BME280)

```
Start | Addr+W | Reg | Start | Addr+R | Data[0..N] | Stop
```

- Speed: 100 kHz
- Timeout: 100 ms
- Retry: 3 attempts
- Error handling: NACK detection, bus reset

### UART (GPS)

```
Format: NMEA-0183
Example: $GPRMC,123456.00,A,0607.1234,S,10645.7890,E,0.0,0.0,010226,,,A*6F
```

- Baud: 9600
- Data: 8N1
- Checksum: XOR (validated)
- Processing: Interrupt-driven, ring buffer

### UART (Data Output)

```
Format: JSON
Baud: 115200
Interval: Every 5 seconds (on successful read)
```

---

## Performance Metrics

| Metric | Target | Typical |
|--------|--------|---------|
| State machine cycle | < 1 ms | ~500 μs |
| BME280 read time | < 10 ms | ~2 ms |
| GPS parse time | < 1 ms | ~200 μs |
| ADC conversion | < 1 ms | ~200 μs |
| JSON formatting | < 5 ms | ~1 ms |
| Total READ state | < 20 ms | ~5 ms |

---

## Future Enhancements

1. **OTA Updates**: Dual-bank bootloader with rollback
2. **DMA Usage**: Offload UART/I2C transactions
3. **FreeRTOS**: Multitasking for better responsiveness
4. **Sensor Fusion**: Kalman filter for GPS + IMU
5. **Edge Processing**: On-device anomaly detection
