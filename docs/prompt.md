# AI usage and prompt strategy

The assessment allows AI assistance on condition that the prompt strategy is documented.
This file describes how AI was used, what it produced and what was verified.

**Tool:** Claude Code (Anthropic's CLI coding agent).
**Model:** Claude Sonnet 4.5 in phase 1, Claude Opus 5.5 in phase 2.

## Phase 1: January/February 2026 (original attempt, not submitted)

I received the assessment on 29 January 2026 and chose Path A. I worked inside an STM32CubeIDE
project for the STM32F407G-DISC1 and asked Claude Code to:

1. analyse the CubeIDE project (`"analisa proyek stm32cube ide ini"`);
2. implement Path A from the pasted assessment text. Claude asked three clarifying questions
   (output method, measurement interval, LED usage); I answered UART JSON + internal log, 5 s,
   all four LEDs;
3. write the drivers (BME280, GPS, fuel), the state machine, power management and the docs.

The result covered only the Level 1 scope, and several parts were left as comments or TODOs:
UART transmit, RTC wake-up configuration, clock restore after STOP. I did not submit it. At that
time I was still finishing my D4 studies in Malang and could not commit to relocating to
Jakarta. That code is still in the git history (commit `60f76f3`).

## Phase 2: October 2026 (this submission)

After graduating I gave Claude Code one instruction: complete this assessment properly in the
same repository, keep the earlier history, and prepare a late submission with an apology.
Claude Code then planned and carried out the work in the steps below (summarised from the
session), using the Notion task page and the February code as input:

| Step | Working instruction (summarised) | Output |
|---|---|---|
| 1 | Read the Notion assessment page and the February code; list what is missing per level | Gap list: Level 1 bugs, no bootloader, no RTOS, no MPU |
| 2 | Review the February code against the spec and the datasheets: real bugs only, no style nits | The issues listed below |
| 3 | Design the flash layout and the boot protocol for a *single-bank* F407 before writing code | Two 384 KB slots, append-only CRC journal, header written last |
| 4 | Keep all decision logic hardware-independent so it can be unit-tested on a PC | `boot_decide()`, NMEA, fuel math, BME280 compensation, state machine, frame codec |
| 5 | Generate module by module, build after each step with `-Wall -Wextra`, fix every warning | 3 images build clean |
| 6 | Write host tests incl. failure injection (torn flash writes, bit flips, wrong slot, bad checksums) and run them under ASan/UBSan | 214 checks pass; UBSan found undefined left shifts in the datasheet formula, which were fixed |
| 7 | Write the documentation the spec requires; every number must come from a datasheet or the build output | README, ARCHITECTURE.md, docs/HARDWARE.md |

### February issues found in step 2 (and fixed)

* BME280 temperature compensation returned a wrong scale and stored `t_fine` in `dig_T1`,
  which corrupted the calibration constant for every following reading.
* Fuel sender: the 1–5 V shunt voltage went directly into the 3.3 V ADC, which saturates above
  about 13 mA. It was also on PA0, which is the user button on the DISC1.
* STOP mode: the RTC wake-up timer was never configured, and `SystemClock_Config()` after
  wake-up was commented out, so the UART baud rates were wrong after the first STOP.
* GPS: only `$GPRMC` was accepted (multi-GNSS modules send `$GNRMC`), it was parsed in the RX
  ISR with double math, and a UART overrun stopped reception permanently.
* TRANSMIT never sent anything (the `HAL_UART_Transmit` call was still a TODO). The ERROR state
  retried forever without re-initialising the bus. There was no watchdog.

### Key design decisions (to be discussed in the interview)

* Two slots in one bank instead of a dual-bank part, because the target is the F407 from the
  task. Images are linked per slot instead of being position-independent.
* The 10-second rollback is tied to *health* (all task heartbeats), not just uptime. The
  watchdog is started by the bootloader, so a hang before the scheduler starts is also caught.
* Any reset during the trial, power loss included, rolls back. This is conservative on purpose.
* The fault record goes to the RTC backup registers, not flash, from inside the fault handler.
* The F4 has no D-cache: coherency means "DMA buffers in SRAM, never CCM", plus no-op
  `cache_clean/invalidate` hooks at every DMA hand-over for a later M7 port.

## What was verified and what was not

* ✅ `cmake --build` for the bootloader and both slots (GNU Arm 13.3), no warnings.
* ✅ `make -C tests`: 214 host checks, including images produced by `prepare-firmware.py`
  verified by the C bootloader code (the two header formats are cross-checked).
* ❌ **Not yet run on a physical board.** Rollback timing, STOP current and update throughput
  are calculated, not measured (see docs/HARDWARE.md).

I am going through every module before the interview so I can explain the design and
live-debug it on a DISC1 board.
