# Architecture

This document covers the three topics the assessment asks for (power-loss survival during
flash writes, the state machines, persistent logging) and the reasoning behind the main
design decisions.

## 1. Images, slots and the boot journal

The STM32F407 has **one** 1 MB flash bank, so "Bank 1 / Bank 2" are two equal 384 KB slots
(sectors 5–7 and 8–10). Each slot holds:

```
slot + 0x000   fw_image_header_t (128 B): magic "TTFW", header version, target slot,
               version (major.minor.patch), image size, CRC-32, build time, SHA-256,
               git revision, header CRC-32
slot + 0x080   0xFF up to 0x200   (keeps the vector table 512-byte aligned for VTOR)
slot + 0x200   vector table + code
```

The application is **linked twice** (`app_slot_a.elf` at `0x08020200`, `app_slot_b.elf` at
`0x08080200`) instead of being position-independent: no GOT, no relocation code in the
bootloader, and the header's `target_slot` field plus a vector-table range check stop an image
from being booted from the wrong slot.

Which slot is active is not stored in a single "config word". It lives in the **boot journal**
(sector 4): an append-only list of 32-byte records

```
magic | sequence | active_slot | pending_slot | trial_active | last_event |
rollback_count | boot_count | reserved | CRC-32
```

The newest record with a valid CRC wins. Records are never modified in place.

## 2. Surviving power loss during flash writes

NOR flash can only be erased in whole sectors (16–128 KB, up to 2 s) and programmed from 1 to 0.
A power cut can interrupt an erase or leave a half-programmed word. Each case:

| Interrupted operation | What is on flash afterwards | Why the device still boots correctly |
|---|---|---|
| Writing the new image (DATA frames) | inactive slot partly written, **header still erased** | The header is written **last**, after all data. No header means the bootloader treats the slot as invalid. The running slot is never touched. |
| Writing the header | header present but its CRC or the image CRC/SHA does not match | `fw_image_verify()` rejects it, and the journal still points at the old slot. |
| Appending a journal record | record with a bad CRC | `journal_read()` ignores it, so the previous record (old state) stays in effect. The next write skips the damaged slot and never rewrites it. |
| Erasing the full journal sector | empty journal | `boot_decide()` rebuilds the state from the slots: the highest valid version wins (`BOOT_EVT_FACTORY`). |
| Staging (journal: `pending = B`) | either old or new record | A single 32-byte append is atomic from the reader's point of view: either the CRC matches or it doesn't. |
| Trial boot of the new image | `trial_active = 1` written **before** the jump | Any reset before the app confirms (power loss included) counts as a failed trial, and the device rolls back. Conservative, but the result is always a known-good image. |
| Event log page rotation | the older page is erased, the newer one is intact | At most the oldest 16 KB of history is lost. |

Additional rules:

* Every write is verified by reading it back (`flash_f4_program()` compares each word).
* The ART data/instruction caches are reset after an erase, so stale data is never read back.
* A 128 KB sector erase stalls the CPU (single bank, the code runs from the same flash).
  The update task refreshes the IWDG between sector erases, because the monitor task cannot
  run during the stall.
* The bootloader never writes to the slots, only to the journal. The worst case it can
  create is a stale journal record, which is recoverable.
* The bootloader sectors are mapped read-only by the MPU in the application.

## 3. Update and boot state machine

```
                                       app: UPDATE_STAGED
   ┌──────────┐   START/DATA/END ok    (pending = inactive slot)     ┌────────────┐
   │  RUNNING │ ─────────────────────────────────────────────────▶   │  STAGED    │
   │ (slot X) │ ◀──── NAK, slot erased, nothing staged ────────┐     └─────┬──────┘
   └──────────┘        (bad header / CRC / SHA / timeout)      │           │ reset
        ▲                                                       │           ▼
        │                                              ┌────────┴───────────────────┐
        │   rollback: pending cleared,                 │ BOOTLOADER                 │
        │   rollback_count++                           │ verify pending slot        │
        │◀─────────────────────────────────────────────│  invalid → PENDING_INVALID │
        │                                              │  valid   → trial_active=1  │
        │                                              │           IWDG 8 s, jump   │
        │                                              └────────┬───────────────────┘
        │                                                       ▼
        │                                              ┌────────────────────────────┐
        │  watchdog / HardFault / assert / power loss  │ TRIAL (slot Y)             │
        └──────────────────────────────────────────────│ monitor: all heartbeats ok │
                       before confirmation             │ for 10 s continuously      │
                                                       └────────┬───────────────────┘
                                                                │ CONFIRMED
                                                                ▼ (active = Y)
                                                          RUNNING (slot Y)
```

`boot_decide()` (`common/src/boot_journal.c`) is a pure function, so every arrow above is
covered by a host unit test (`tests/test_main.c`). The bootloader also handles two cases
that are not part of the update flow:

* **Active slot corrupt** (bit rot, interrupted factory flashing): boot the other slot if it is
  valid (`BOOT_EVT_FALLBACK`).
* **Nothing valid**: stay in the bootloader, blink red, and wait for SWD (the IWDG is not started).

What counts as "the new firmware crashes within 10 seconds":

* HardFault, MemManage, BusFault, UsageFault, stack overflow, malloc failure or `configASSERT`:
  the handler stores PC/LR/CFSR in the RTC backup registers and resets.
* A hung or starved task: the monitor stops refreshing the IWDG, which resets the MCU within 8 s.
* A hang before the scheduler starts: the IWDG was started by the bootloader, so this is
  covered too.

The image is confirmed only after **10 s of continuous health**. An image that crashes at
9.9 s is never confirmed.

### Update protocol (USART1)

Frames are `A5 5A | type | seq | len16 | payload | CRC-32`. The protocol is stop-and-wait, and
every frame is ACKed or NAKed. A corrupted frame is NAKed with `UP_E_CRC` and retransmitted.
A duplicate DATA frame (the ACK was lost) is re-ACKed without writing again. A session with
no traffic for 30 s is abandoned. Telemetry output pauses during a session. Telemetry is plain
JSON text, so the host decoder skips everything that is not a frame.

## 4. Telemetry state machine (Level 1)

```
      INIT_OK                TIMER (5 s / RTC wake)        READ_OK
 INIT ───────▶ IDLE ───────────────────────────▶ READ ──────────────▶ TRANSMIT
  │             ▲  ▲                               │                     │
  │ INIT_FAIL   │  └──── READ_FAIL (< 5 in a row) ─┘        TX_DONE      │
  ▼             │                                  (errors := 0) ────────┘
 ERROR ◀── 5th consecutive READ/TX failure
  │  RECOVERED (I2C bus recovery + re-init OK) ──▶ IDLE
  └─ 5 failed recoveries ──▶ log EVT_CONTROLLED_RESET, NVIC_SystemReset()
```

* **Degraded mode**: READ fails only if *no* data source worked. A disconnected BME280 does
  not stop GPS and fuel reporting. It appears as `"env":null` with error bit `0x01`, and the
  driver re-probes it every cycle (hot-plug).
* **I2C**: every transfer has a 100 ms timeout. On error the driver does a bus recovery
  (9 SCL pulses + STOP, then a peripheral reset for the F4 BUSY-flag erratum) and retries up
  to 3 times.
* **GPS**: circular DMA + IDLE-line interrupt, so no bytes are lost while the CPU is busy. The
  NMEA checksum is mandatory (sentences without `*hh` are rejected), and any talker ID is
  accepted (`$GPRMC`, `$GNRMC`, ...). Empty fields mean "no fix" instead of 0°/0°. Overlong
  lines are dropped.
* **Fuel**: NAMUR NE43 limits. Below 3.6 mA = open loop (broken wire, *not* "0 % fuel"), above
  21 mA = short. VDDA is measured with VREFINT on every read. Faults use the instantaneous
  value; the level uses an 8-sample moving average (fuel slosh).

### Power policy (STOP mode)

The telemetry task enters STOP when **all** of these are true: no activity (button, host
traffic, GPS fix) for 30 s, no GPS fix, no update session, and the running image is
confirmed. In STOP:

* low-power regulator, flash in power-down, SysTick and the TIM6 HAL tick stopped;
* the RTC wake-up timer (LSI) wakes the MCU every **5 s**, which is shorter than the
  **8 s IWDG**. The F407 IWDG cannot be frozen in STOP, so this ordering is required. The MCU
  takes a sample, transmits it, listens for 200 ms, and returns to STOP;
* on wake-up the clock tree is restored (the MCU wakes on HSI 16 MHz), the RTOS tick is
  advanced by the time spent in STOP (`xTaskCatchUpTicks`), heartbeats are resynchronised and
  GPS DMA is re-armed.

USART RX cannot wake an F4 from STOP. `send-firmware.py` keeps sending INFO for 12 s, so
it reaches the device in its next wake window. Any received frame counts as activity and
keeps the device awake.

## 5. RTOS design (Level 3)

| Task | Priority | Stack | Blocks on | Heartbeat |
|---|---|---|---|---|
| Monitor | 4 (highest) | 1 KB | `vTaskDelayUntil` 500 ms | owns the IWDG |
| Update | 3 | 2 KB | stream buffer, 1 s timeout | every loop |
| Telemetry | 2 | 2 KB | `vTaskDelay` / sensor I/O | every loop |

* ISR → task data paths: USART1 RX goes through a **stream buffer**, GPS through the parser
  state, which is read inside a critical section. ISRs that call FreeRTOS APIs run at NVIC
  priority 6, below `configMAX_SYSCALL_INTERRUPT_PRIORITY` (5).
* Shared resources: the USART1 TX mutex, the event-log mutex and the boot-journal mutex.
  Each driver is owned by exactly one task.
* The kernel heap (32 KB, heap_4) and therefore all task stacks are in **CCM RAM**. CCM is
  zero-wait but **not reachable by DMA**, so all DMA buffers are static objects in SRAM. This is
  the F4 equivalent of the cache-coherency problem below.

## 6. MPU and cache coherency (Level 3)

| Region | Range | Attributes | Purpose |
|---|---|---|---|
| 0 | `0x0000_0000` 1 KB | no access | NULL-pointer dereference → MemManage |
| 1 | `0x0800_0000` 32 KB | read-only, executable | bootloader cannot be overwritten |
| 2 | SRAM 128 KB | RW, **execute-never** | no code execution from RAM |
| 3 | CCM 64 KB | RW, **execute-never** | stacks/heap: no stack-smash execution |
| 4 | peripherals 512 MB | RW, XN, device | strongly ordered register access |
| 5 | system memory/OTP 32 KB | RO, XN | VREFINT calibration is read-only |

Everything else uses the default map (`PRIVDEFENA`). Flash outside the bootloader stays
writable because the application programs the log, the journal and the inactive slot.

**D-cache coherency:** the Cortex-M4 in the F407 has **no data cache**; the ART accelerator
caches only flash. DMA ↔ CPU coherency is therefore guaranteed by the bus matrix, provided
the buffers are in DMA-reachable SRAM (not CCM). The code still calls
`cache_clean()` before DMA reads CPU data and `cache_invalidate()` after DMA wrote data (UART
RX/TX paths). Both compile to nothing on the F4 and to `SCB_Clean/InvalidateDCache_by_Addr`
on a Cortex-M7 (`__DCACHE_PRESENT`). Moving the code to an STM32F7/H7 then also requires
32-byte-aligned DMA buffers, or an MPU region that makes them non-cacheable.

## 7. Persistent logging (NVS)

The F407 has no EEPROM, so two 16 KB sectors (2 and 3) form a ping-pong event log:

* 32-byte slots, each holding `{seq, uptime_s, id, arg, data}` plus a CRC-32. A torn record is
  skipped on read.
* Append until the page is full, then erase the *other* page and continue there. The newest
  page is never erased, so the last 512–1024 events always survive.
* Endurance: 512 records per page × 10 000 erase cycles × 2 pages ≈ 10 M events. At one event
  per minute that is about 19 years. Events are state changes (boot, fault, ERROR, recovery,
  STOP entry, update steps), not periodic samples, so the real rate is far lower.
* Faults cannot safely write flash from the fault handler. They are latched in the RTC backup
  registers and moved into the log on the next boot (`fault_log_flush()`), together with the
  reset cause from `RCC_CSR` (watchdog, software, brown-out, pin).

The boot journal (sector 4, 64 KB) follows the same append-only + CRC principle. One record
is written per boot (boot counter), so the sector is erased only every ~2000 boots.

## 8. Known limitations / next steps

* Not yet validated on hardware: STOP current, rollback timing and the update throughput
  need to be measured.
* Images are integrity-checked (CRC-32 + SHA-256) but **not authenticated**. Anyone who can
  talk to USART1 can install firmware. Next step: sign the header (ECDSA P-256) and verify
  the signature in the bootloader. Also enable RDP level 1 so the key and code cannot be read
  out over SWD.
* No anti-rollback (downgrade) protection: a version counter in OTP would provide it.
* The GPS module stays powered in STOP. A load switch on its supply would cut 25–45 mA.
