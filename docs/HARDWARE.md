# Hardware analysis

## I2C pull-up resistors (BME280 on I2C1, 100 kHz)

Limits from the I2C specification (NXP UM10204, Standard-mode):

* rise time t_r ≤ 1000 ns, V_OL ≤ 0.4 V at I_OL = 3 mA
* estimated bus capacitance C_b ≈ 100 pF (MCU pin ~10 pF + BME280 ~10 pF + 20 cm of wire
  and breakout board ~50–80 pF)

```
R_p(min) = (V_DD − V_OL(max)) / I_OL         = (3.3 − 0.4) / 3 mA        ≈ 0.97 kΩ
R_p(max) = t_r / (0.8473 · C_b)              = 1000 ns / (0.8473 · 100 pF) ≈ 11.8 kΩ
```

**4.7 kΩ** is in range: t_r = 0.8473 · 4.7 kΩ · 100 pF ≈ **0.40 µs** (40 % of the limit),
and the sink current is 3.3 V / 4.7 kΩ ≈ 0.7 mA. Two cautions:
many BME280 breakouts already carry 10 kΩ pull-ups (4.7k ∥ 10k = 3.2 kΩ, still fine), and
on the DISC1, PB6 is also routed to the on-board CS43L22 audio codec. Its address (0x94/0x4A)
does not clash with the BME280 at 0x76.

## 4–20 mA fuel sender input

```
 loop +24 V ──► sensor ──► I_loop ──┬── 250 Ω 0.1 % (0.25 W) ──┬── GND
                                    │                           │
                                    └─ 10 kΩ ─┬─ 1 kΩ ─┬─ PA1   │
                                              │        │        │
                                            10 kΩ   BAT54S   100 nF
                                              │   (to VDDA/GND) │
                                             GND               GND
```

| Loop current | Shunt voltage | Pin voltage (÷2) | Meaning |
|---|---|---|---|
| 0 mA | 0 V | 0 V | open loop / broken wire → fault |
| 4 mA | 1.0 V | 0.50 V | 0 % |
| 12 mA | 3.0 V | 1.50 V | 50 % |
| 20 mA | 5.0 V | 2.50 V | 100 % |
| 24 mA | 6.0 V | 3.00 V | fault high, still measurable at VDDA = 3.0 V |

* The original design connected the 1–5 V shunt voltage directly to the ADC. Above
  VDDA (3.0–3.3 V) that saturates the reading and stresses the pin. The divider fixes this.
  With a 0.5 ratio, NAMUR fault currents up to 24 mA can still be measured even when the board
  runs at 3.0 V.
* Divider source impedance 10k ∥ 10k = 5 kΩ. The ADC is sampled with 480 cycles at 21 MHz
  (22.8 µs), well above the ~7 τ needed for 12-bit settling with this impedance.
* Loading: the divider draws 5 V / 20 kΩ = 0.25 mA in parallel with the shunt, a 1.25 % gain
  error. It is constant, so it can be calibrated out (`FUEL_DIVIDER_RATIO`).
* Resolution: 16 mA span = 2.0 V ≈ 2480 counts at VDDA 3.3 V, so ~6.5 µA/count, or 0.04 % of
  the span per count.
* The shunt dissipates 20 mA² · 250 Ω = 0.1 W, so a 0.25 W part is used.

## Power budget

Values are datasheet typicals (STM32F407 DS8626 Tables 21–27, BME280 DS, typical u-blox NEO-6M
module) at 25 °C, 3.3 V. These are estimates and still need to be measured on JP1 (IDD) of
the DISC1.

| State | MCU | BME280 | GPS module | Notes |
|---|---|---|---|---|
| Run, 168 MHz, peripherals on | ≈ 87 mA | 0.1 µA (sleep) | ≈ 37–45 mA | CPU mostly blocked in RTOS idle (WFI not used) |
| Sample burst (~60 ms) | ≈ 87 mA | ≈ 0.7 mA for 9 ms | – | clock restore + I2C + ADC + 400 B at 115200 baud ≈ 35 ms |
| STOP, LP regulator, flash power-down | ≈ 0.3 mA | 0.1 µA | still powered | RTC on LSI + IWDG ≈ 1–2 µA |

Average MCU current in the "parked vehicle, no GPS" case, 5 s wake period:

```
I_avg ≈ 0.3 mA + 87 mA · (60 ms / 5000 ms) ≈ 0.3 + 1.04 ≈ 1.35 mA   (vs ≈ 87 mA always-on)
```

That is a ~98 % reduction of the MCU share. The GPS module then dominates (≈ 40 mA), so the next
improvement is a load switch or the module's backup mode (`UBX-RXM-PMREQ`), which can be
added to the same STOP policy. The ST-LINK and the on-board peripherals of the DISC1 are not
included.

## Memory map

### Flash (1 MB)

| Region | Range | Size | Used |
|---|---|---|---|
| Bootloader | `0x0800_0000–0x0800_7FFF` | 32 KB | 7.2 KB (22 %) |
| Event log A/B | `0x0800_8000–0x0800_FFFF` | 2 × 16 KB | – |
| Boot journal | `0x0801_0000–0x0801_FFFF` | 64 KB | 32 B per record |
| Slot A | `0x0802_0000–0x0807_FFFF` | 384 KB | 35 KB (9 %) |
| Slot B | `0x0808_0000–0x080D_FFFF` | 384 KB | 35 KB (9 %) |
| Reserved | `0x080E_0000–0x080F_FFFF` | 128 KB | – |

Small sectors 2–4 hold frequently erased data (fast erase, and the large sectors are not worn by
logging). The slots are aligned to the 128 KB sectors, so an update erases exactly the target
slot.

### RAM

| Region | Size | Content |
|---|---|---|
| SRAM1+2 `0x2000_0000` | 128 KB | `.data/.bss` 6.7 KB (DMA buffers: GPS 256 B, host RX 512 B), newlib heap 1 KB, MSP stack 2 KB (ISRs + start-up) |
| CCM `0x1000_0000` | 64 KB | FreeRTOS heap 32 KB: task stacks 5 KB + TCBs, stream buffer 2 KB, mutexes |

## Clocks

HSE 8 MHz → PLL (M=8, N=336, P=2, Q=7) → SYSCLK 168 MHz, AHB 168 MHz, APB1 42 MHz
(timers 84 MHz), APB2 84 MHz, flash latency 5 WS. RTC on LSI (~32 kHz, no LSE fitted on DISC1).
The LSI can be ±30 % off, which only affects the STOP wake period, and that stays below
the IWDG timeout across the whole tolerance range: the IWDG also runs on the LSI, so both
scale together.
