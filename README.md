# STM32 Sensor Manager

## Overview

A production-grade firmware solution for STM32F407VGTx microcontroller that manages three sensors:

- **BME280** (I2C) - Temperature, pressure, humidity sensor
- **GPS** (UART) - NMEA parser with $GPRMC sentence support
- **Fuel Sensor** (ADC) - 4-20mA loop with 250 ohm shunt resistor

### Features

- State machine architecture (INIT, IDLE, READ, TRANSMIT, ERROR)
- Power management with STOP mode (30-second timeout)
- I2C timeout handling (100ms)
- GPS checksum validation
- JSON formatted data output via UART
- LED status indicators (Power, Activity, Error, GPS Lock)

## Hardware

### Target MCU

| Component | Value |
|-----------|-------|
| MCU | STM32F407VGTx |
| Board | STM32F407G-DISC1 |
| Core | ARM Cortex-M4F @ 168MHz |
| Flash | 1024 KB |
| RAM | 128 KB |
| CCMRAM | 64 KB |

### Pinout

```
┌───────────────────────────────────────────────────────────────────┐
│                    STM32F407G-DISC1                               │
├───────────────────────────────────────────────────────────────────┤
│                                                                   │
│  ┌──────────────────┐  ┌──────────────────┐                      │
│  │   I2C1 (BME280)  │  │   USART2 (GPS)   │                      │
│  │  PB6 (SCL)       │  │  PA2 (TX)        │                      │
│  │  PB7 (SDA)       │  │  PA3 (RX)        │                      │
│  └──────────────────┘  └──────────────────┘                      │
│                                                                   │
│  ┌──────────────────┐  ┌──────────────────┐                      │
│  │  ADC1 (Fuel)     │  │  LEDs (Status)   │                      │
│  │  PA0 (Channel 0) │  │  PD12 (Green)    │ ← Power              │
│  │                  │  │  PD13 (Orange)   │ ← Activity           │
│  │  4-20mA loop     │  │  PD14 (Red)      │ ← Error              │
│  │  250Ω shunt      │  │  PD15 (Blue)     │ ← GPS Lock           │
│  └──────────────────┘  └──────────────────┘                      │
│                                                                   │
│  ┌──────────────────┐                                            │
│  │  USART1 (Data)   │  Outputs JSON sensor data                  │
│  │  PA9 (TX)        │  @ 115200 baud                             │
│  │  PA10 (RX)       │                                            │
│  └──────────────────┘                                            │
│                                                                   │
└───────────────────────────────────────────────────────────────────┘
```

### I2C Pull-up Calculation

For I2C at 100kHz with estimated bus capacitance of ~100pF:

```
R_pullup = (t_r) / (0.8473 × C_bus)
         = (1000ns) / (0.8473 × 100pF)
         = ~11.8 kΩ

Using 4.7 kΩ for conservative margin with short traces
```

## Architecture

### State Machine Diagram

```
                    ┌─────────────────────┐
                    │        INIT         │
                    │ Initialize sensors  │
                    └──────────┬──────────┘
                               │
                               ▼
                    ┌─────────────────────┐    ┌──────────────┐
         ┌──────────│       IDLE          │◄───│  TRANSMIT    │
         │          │  Wait 5 seconds     │    │  Send data   │
         │          └──────────┬──────────┘    └──────────────┘
         │                     │
         │                     ▼
         │          ┌─────────────────────┐
         │          │       READ          │
         │          │  Read all sensors   │
         │          └──────────┬──────────┘
         │                     │
         │                     │
         │           [Error]   │
         │                     │
         └─────────────────────┼───────►┌──────────────┐
                                       │    ERROR     │
                                       │  Log & Retry │
                                       └──────────────┘
```

### Power Management

```
                     ┌────────────────────┐
                     │   RUN Mode        │
                     │  Sensors active   │
                     └─────────┬─────────┘
                               │
                               │ No GPS signal
                               │ > 30 seconds
                               ▼
                     ┌────────────────────┐
                     │   STOP Mode        │
                     │  Minimal power     │
                     │  (~100 μA)         │
                     └─────────┬─────────┘
                               │
                               │ Wakeup on:
                               │ • RTC (5s)
                               │ • GPS data
                               │ • Button
                               ▼
                     ┌────────────────────┐
                     │   RUN Mode         │
                     │  Resume sensors    │
                     └────────────────────┘
```

## Building

### Prerequisites

- STM32CubeIDE 2.0.0+ or ARM GCC toolchain
- arm-none-eabi-gcc
- arm-none-eabi-objcopy

### Build with STM32CubeIDE

1. Open project in STM32CubeIDE
2. Build → Build All (Ctrl+B)
3. Output: `Debug/sensor_manager.elf`

### Build with Make (alternative)

```bash
# The project uses managed make by STM32CubeIDE
# Build output will be in Debug/ directory
```

### Prepare Firmware Binary

```bash
# Convert ELF to BIN with header and CRC
python scripts/prepare-firmware.py Debug/sensor_manager.elf sensor_manager_v1.0.0.bin

# Verify existing firmware
python scripts/prepare-firmware.py sensor_manager.bin --verify
```

## UART Data Format

### Sensor Data Output (USART1 @ 115200 baud)

```json
{
  "timestamp": 1234567890,
  "bme280": {
    "temp": 25.4,
    "press": 101325,
    "hum": 60.2
  },
  "gps": {
    "valid": true,
    "time": "123456.00",
    "date": "010226",
    "lat": -6.2088,
    "lon": 106.8456,
    "speed": 0.0
  },
  "fuel": {
    "level": 75.5,
    "voltage": 2.5,
    "current": 12.0
  },
  "status": "OK"
}
```

### GPS Input (USART2 @ 9600 baud)

```
$GPRMC,123456.00,A,0607.1234,S,10645.7890,E,0.0,0.0,010226,,,A*6F
```

## Configuration

Edit `Inc/config.h` to modify:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `SENSOR_READ_INTERVAL_MS` | 5000 | Sensor reading interval |
| `STOP_MODE_TIMEOUT_MS` | 30000 | Inactivity timeout for STOP mode |
| `BME280_I2C_TIMEOUT` | 100 | I2C timeout in ms |
| `GPS_UART_BAUDRATE` | 9600 | GPS baud rate |
| `DATA_UART_BAUDRATE` | 115200 | Data output baud rate |

## LED Indicators

| LED | GPIO | Color | State | Meaning |
|-----|------|-------|-------|---------|
| LED4 | PD12 | Green | ON | System running |
| LED3 | PD13 | Orange | Blink | Reading sensors |
| LED5 | PD14 | Red | ON | Error occurred |
| LED6 | PD15 | Blue | ON | GPS 3D fix acquired |

## Error Handling

The system handles:

- **I2C Timeout**: BME280 read failures trigger retry
- **GPS Checksum**: Invalid sentences are discarded with counter
- **ADC Validation**: Out-of-range current (not 4-20mA) flagged
- **State Machine**: Max 5 consecutive errors before entering ERROR state

## Power Loss Survival Strategy

See [ARCHITECTURE.md](docs/ARCHITECTURE.md) for details on:

- Flash write endurance handling
- Critical data backup to RTC registers
- STOP mode context save/restore
- Watchdog configuration

## Documentation

- [ARCHITECTURE.md](docs/ARCHITECTURE.md) - Detailed architecture documentation
- [docs/prompt.md](docs/prompt.md) - AI prompt strategy used

## Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0.0 | 2026-02-02 | Initial release |

## License

This project is provided as-is for technical assessment purposes.

## Author

Generated for STM32F407VGTx using STM32CubeIDE
# FirmwareEngineer_AhmadZainulArifin_TransTrack
