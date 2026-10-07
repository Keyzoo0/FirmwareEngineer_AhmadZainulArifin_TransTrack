# TransTRACK Firmware Engineer Assessment: STM32 Sensor Manager (Path A)

Firmware for the **STM32F407G-DISC1** that reads a BME280, a GPS receiver and a 4–20 mA fuel
sender, sends JSON telemetry, sleeps in STOP mode when idle, and updates itself in the field
using a **dual-slot bootloader with CRC-32 + SHA-256 verification and automatic rollback**.

| Level | Requirement | Where |
|---|---|---|
| 1 | BME280 (I2C) + GPS `$GPRMC` (UART) + 4–20 mA fuel (250 Ω shunt) | `app/src/bme280.c`, `nmea.c`, `gps.c`, `fuel_calc.c`, `fuel_sensor.c` |
| 1 | State machine INIT / IDLE / READ / TRANSMIT / ERROR | `app/src/sensor_sm.c`, `telemetry_task.c` |
| 1 | STOP mode after 30 s without activity / GPS | `app/src/power_manager.c` |
| 1 | I2C timeouts + bus recovery, GPS checksum validation | `app/src/bme280.c`, `bsp.c`, `nmea.c` |
| 2 | Bootloader verifies the new image (CRC-32 **and** SHA-256) before jumping | `bootloader/src/main.c`, `common/src/fw_image.c` |
| 2 | Automatic rollback to Bank 1 if the new image fails within 10 s | `common/src/boot_journal.c`, `app/src/sys_monitor.c` |
| 3 | FreeRTOS with Telemetry, Update and System Monitor tasks | `app/src/*_task.c`, `sys_monitor.c` |
| 3 | MPU: no execution from RAM; D-cache coherency for DMA | `app/src/mpu_config.c`, `app/inc/mpu_config.h` |
| – | `prepare-firmware.py` adds the version/size/CRC/SHA header | `scripts/prepare-firmware.py` |

Design details: **[ARCHITECTURE.md](ARCHITECTURE.md)**. Hardware calculations (I2C pull-ups,
fuel input, power budget, memory map): **[docs/HARDWARE.md](docs/HARDWARE.md)**.
How AI was used: **[docs/prompt.md](docs/prompt.md)**.

## Architecture

```
                         STM32F407VG  (1 MB flash, 128 KB SRAM + 64 KB CCM)
 ┌──────────────────────────────────────────────────────────────────────────────────┐
 │  Bootloader (0x0800_0000, 32 KB, no HAL)                                         │
 │    verify slot A + B (header CRC, CRC-32, SHA-256, vectors) → boot journal       │
 │    → decide (trial / rollback / fallback) → start IWDG 8 s → jump                │
 ├──────────────────────────────────────────────────────────────────────────────────┤
 │  Application (slot A 0x0802_0200 or slot B 0x0808_0200), FreeRTOS                │
 │                                                                                  │
 │   ┌───────────────┐   ┌────────────────┐   ┌─────────────────────────────┐       │
 │   │ Monitor  (p4) │   │ Update   (p3)  │   │ Telemetry (p2)              │       │
 │   │ heartbeats    │   │ framed proto   │   │ INIT→IDLE→READ→TRANSMIT     │       │
 │   │ → IWDG refresh│   │ → inactive slot│   │        ↘ ERROR (recover)    │       │
 │   │ boot confirm  │   │ → verify→stage │   │ STOP mode policy            │       │
 │   └──────┬────────┘   └──────┬─────────┘   └───┬─────────┬─────────┬─────┘       │
 │          │ journal           │ USART1 RX DMA   │ I2C1    │ USART2  │ ADC1        │
 │          ▼                   ▼                 ▼         ▼ DMA+IDLE▼ +VREFINT    │
 │   flash: journal (s4)   host link ◄── JSON ── BME280    GPS       4-20 mA        │
 │          event log (s2/3)                                                        │
 └──────────────────────────────────────────────────────────────────────────────────┘
```

Flash map (single-bank F407, so "Bank 1 / Bank 2" are two equal slots):

| Sectors | Address | Size | Content |
|---|---|---|---|
| 0–1 | `0x0800_0000` | 32 KB | bootloader |
| 2–3 | `0x0800_8000` | 2 × 16 KB | persistent event log (ping-pong) |
| 4 | `0x0801_0000` | 64 KB | boot-control journal (append-only) |
| 5–7 | `0x0802_0000` | 384 KB | **slot A / Bank 1**: 512 B header + image |
| 8–10 | `0x0808_0000` | 384 KB | **slot B / Bank 2**: 512 B header + image |
| 11 | `0x080E_0000` | 128 KB | reserved |

## Pinout (STM32F407G-DISC1)

| Function | Pin | Peripheral | Notes |
|---|---|---|---|
| BME280 SCL / SDA | PB6 / PB7 | I2C1, 100 kHz | 4.7 kΩ pull-ups to 3.3 V, address 0x76 |
| GPS TX → MCU | PA3 | USART2 RX, 9600 8N1 | circular DMA1 Stream5 + IDLE line |
| MCU → GPS | PA2 | USART2 TX | |
| Host / telemetry / update | PA9 / PA10 | USART1, 115200 8N1 | DMA2 Stream2 RX |
| Fuel sender | PA1 | ADC1_IN1 | 250 Ω shunt → 10k/10k divider → clamp, see HARDWARE.md |
| User button (wake) | PA0 | EXTI0 | wakes from STOP |
| LED green / orange / red / blue | PD12 / PD13 / PD14 / PD15 | GPIO | alive / activity / error / GPS fix |

The original version used PA0 for the fuel ADC. PA0 is the user button on this board, so the
fuel input moved to PA1.

## Build

Requirements: `arm-none-eabi-gcc` (GNU Arm toolchain 12+ or the one bundled with STM32CubeIDE),
CMake ≥ 3.20, Python 3. Dependencies are pinned git submodules (CMSIS 5.6.0, CMSIS-Device F4
2.6.11, STM32F4 HAL 1.8.5, FreeRTOS-Kernel V11.1.0).

```bash
git clone --recursive https://github.com/Keyzoo0/FirmwareEngineer_AhmadZainulArifin_TransTrack.git
cd FirmwareEngineer_AhmadZainulArifin_TransTrack
cmake -S . -B build                      # add -DTOOLCHAIN_PREFIX=/path/to/bin/ if not on PATH
cmake --build build -j                   # bootloader.elf, app_slot_a.elf, app_slot_b.elf + .bin
make -C tests                            # host unit tests (gcc, ASan + UBSan)
```

Packaging and flashing:

```bash
# Factory image: bootloader + slot A v1.1.0 in one file, flash at 0x08000000
python3 scripts/prepare-firmware.py build/app_slot_a.bin --slot a --version 1.1.0 \
        --bootloader build/bootloader.bin -o factory.bin
st-flash write factory.bin 0x08000000

# Field update: build for the *other* slot, wrap it, send it over USART1
cmake -S . -B build -DFW_VERSION_MINOR=2 && cmake --build build
python3 scripts/prepare-firmware.py build/app_slot_b.bin --slot b --version 1.2.0 -o update_b.img
python3 scripts/send-firmware.py /dev/ttyUSB0 update_b.img
python3 scripts/send-firmware.py /dev/ttyUSB0 --info      # running slot, trial state, rollbacks
```

CI (`.github/workflows/build.yml`) runs the unit tests, builds all three images and publishes
them as artifacts.

## Telemetry format (USART1, one JSON object per line, every 5 s)

```json
{"seq":12,"up":61,"fw":"1.1.0","slot":"A","state":"TRANSMIT","err":2,
 "env":{"t":27.31,"p":1008.42,"h":61.4},
 "gps":{"fix":false,"cksum_err":0},
 "fuel":{"pct":48.7,"ma":11.79,"vdda":3297,"st":"ok"}}
```

`err` bits: `0x01` BME280, `0x02` no GPS fix, `0x04` fuel loop fault. A failed sensor is
reported as `null` / a status string, and telemetry continues with the remaining sensors.

## LEDs

| LED | Meaning |
|---|---|
| green blinking (1 Hz) | system monitor running, watchdog being refreshed |
| orange | sensor read in progress, or firmware update session active |
| red | telemetry state machine in ERROR (recovery in progress) |
| blue | valid GPS fix |
| red + orange in bootloader | last trial image was rolled back |
| red blinking in bootloader | no valid image in either slot |

## Testing status

- Host unit tests: 214 checks pass (CRC/SHA vectors, image verification incl. Python-generated
  images, journal torn writes, all rollback paths, NMEA, 4–20 mA conversion, BME280 compensation
  against the datasheet example, state machine, update framing).
- Firmware builds without warnings for all three images (`-Wall -Wextra`).
- **Not yet run on hardware.** The rollback, STOP-mode current and update paths need to be
  checked on a board with an ammeter on JP1 and a USB-UART on PA9/PA10.

## Repository layout

```
bootloader/   bootloader source + linker script
app/          application (drivers, tasks, FreeRTOSConfig.h, HAL config, linker scripts)
common/       code shared by bootloader, app and host tests (CRC, SHA-256, image header,
              boot journal, flash driver)
scripts/      prepare-firmware.py, send-firmware.py
tests/        host unit tests
cmake/        toolchain file + shared linker sections
third_party/  submodules (CMSIS, HAL, FreeRTOS)
docs/         HARDWARE.md, prompt.md
```

## License

MIT
