# TGR-17 · pF Meter & Coffee-Bean Moisture Meter

![Platform](https://img.shields.io/badge/platform-ESP32--S3-blue)
![Framework](https://img.shields.io/badge/framework-Arduino%203.x%20(pioarduino)-teal)
![Build](https://img.shields.io/badge/build-PlatformIO-orange)

ESP32-S3 firmware that measures picofarad-level capacitance with an RC timing circuit and an LM393 comparator, then uses the same front-end with a parallel-plate sensor to measure coffee-bean moisture.
Built by team **TGR-17** for **TESA Top Gun Rally 2026 #20 — Track 1: Precision Sensing**.

| Day | Task | Firmware | Output |
|---|---|---|---|
| 1 (18 Nov) | pF capacitance meter | `esp32s3-n16r8` (default) | `Capacitance = xx.xx pF` |
| 2 (20 Nov) | Coffee-bean moisture meter | `moisture` | `Moisture Level = xx.xx %` |

---

## How it works

### Capacitance (both days)

1. A GPIO charges the unknown capacitor `Cx` through `R1 = 1 MΩ`.
2. The LM393 flips its output when the capacitor voltage `Vc` crosses `Vref` (≈ Vcc/2).
3. The ESP32 times the charge with the CPU cycle counter (interrupts off), averages 256 charges (300 on day 2), then solves the RC equation:

```
Vc(t) = Vcc · (1 − e^(−t/RC))      →      C = t / (R1 · ln(Vcc / (Vcc − Vref)))
```

4. A linear calibration against LCR-measured reference capacitors removes stray capacitance and gain error:

```
C_x = gain · C_raw + offset          gain = 0.96213, offset = −9.3134 pF  (R² = 0.99998)
```

### Moisture (day 2)

Water has a much higher permittivity than dry bean matter, so wetter beans raise the capacitance of a box filled with beans.

1. **Sensor:** two 5 × 10 cm PCB plates in the box's center slots, 1 cm apart.
   - Copper faces outward, so the FR4 substrate insulates the beans from the copper.
   - The GND plate faces the operator and acts as an electrostatic shield against hands and bodies.
   - The `Vc` plate faces the circuit.
2. **Normalize to relative permittivity:** divide by the empty-box capacitance `C0`. This cancels small changes in plate gap and geometry.

```
εr = (C − C_STRAY) / (C0 − C_STRAY)          C_STRAY = 0
```

3. **Map εr to moisture (%MC, wet basis):** piecewise-linear interpolation through three reference bags (6.9 / 11.5 / 14.4 %). The end segments extend beyond the table. Sensitivity is about 1.6–1.8 pF per 1 %MC.

| Bag | εr |
|---|---|
| 6.9 % | 1.5060 |
| 11.5 % | 2.1599 |
| 14.4 % | 2.5219 |

The curve flattens at high moisture (7.0 → 8.0 %MC per unit εr between segments), so a piecewise fit beats a single line.

## Hardware

| Part | Connection |
|---|---|
| ESP32-S3-DevKitC-1 (N16R8) | Controller |
| R1 1 MΩ | GPIO4 (CHARGE) → `Vc` node |
| Cx / sensor plates | `Vc` node → GND |
| LM393 comparator 1 | IN+ = `Vc`, IN− = `Vref` (10k trimpot, ≈ 1.65 V — do not turn after calibration), OUT → GPIO1 (SENSE) |
| Pull-up 2 kΩ | LM393 OUT → 3.3 V (open-collector output) |
| Push button + 0.1 µF | GPIO2 → GND, 10 kΩ pull-up, 0.1 µF across switch (debounce) |
| On-board RGB LED | GPIO48 — flashes white for 200 ms on every press |

## Getting started

Requires [PlatformIO](https://platformio.org/) (VS Code extension or CLI).

The MQTT helper needs a credentials header, even though no competition build uses it:

```bash
cp include/secrets.example.h include/secrets.h   # git-ignored, never commit it
```

### Build environments

| Env | Source | Purpose |
|---|---|---|
| `esp32s3-n16r8` (default) | `src/main.cpp` | **Day 1 competition firmware** — pF meter |
| `calib` | `src/calib.cpp` | Fits `gain` / `offset` from the 5 / 10 / 15 / 20 pF reference capacitors |
| `moisture` | `src/moisture.cpp` | **Day 2 competition firmware** — moisture meter (`COLLECT 0`), or guided calibration (`COLLECT 1`) |
| `c0` | `src/c0.cpp` | Measures the empty box and prints a ready-to-paste `#define C0_PF` line |

```bash
pio run -e <env> -t upload     # build + flash
pio device monitor             # 9600 baud, UART port
```

The `moisture` and `c0` envs also log the serial output to `logs/`.

## Calibration

### 1. Capacitance meter (only after a hardware change)

Redo this after changing wiring, R1, the comparator, the trimpot, or the sample count.

1. Flash `calib` and follow the prompts with the reference capacitors.
2. Copy the printed `gain` / `offset` into **all three** places:
   - `setup()` in [src/main.cpp](src/main.cpp)
   - `CAL_GAIN` / `CAL_OFFSET` in [src/moisture.cpp](src/moisture.cpp)
   - `CAL_GAIN` / `CAL_OFFSET` in [src/c0.cpp](src/c0.cpp)
3. A new `offset` invalidates `CAL_ER`, so redo step 3 as well.

### 2. Empty box `C0` (every session, and on contest day)

`C0` drifts by about 0.1–0.5 pF between sessions, mostly from wire placement and people nearby. An error of 0.1 pF costs about 0.1 %MC.

1. Set the box up exactly as it will be used: same slots, wire taped in place, hands away.
2. Flash `c0` and press the button 5–6 times on the empty box. The SD should stay below about 0.03 pF.
3. Paste the printed `#define C0_PF …` line into [src/moisture.cpp](src/moisture.cpp).
4. Flash `moisture` and press on the empty box. It should read **≈ 3.34 %**, because εr = 1 maps to 3.34 % on the extended first segment.

If only the `moisture` firmware is on the board, `C0` can be back-calculated from the empty-box reading `M`. This works only while `C_STRAY = 0` and `CAL_ER` is unchanged:

```
C0_new = C0_in_firmware × (0.52515 + 0.142152 × M)
```

### 3. Moisture table `CAL_ER` (only with fresh reference bags)

1. Set `#define COLLECT 1` in `moisture.cpp`, then flash `moisture`.
2. The serial monitor guides each step:
   - empty box ×6
   - each bag ×6
   - empty box ×6 again, to check `C0` drift
3. For every bag reading:
   - empty the box
   - pour the beans through a funnel
   - level the top
   - do not shake or tap
   - press the button
4. When it finishes, it prints `RAW,…` lines, a per-stage mean and SD, the start-to-end `C0` drift, a line-vs-piecewise recommendation, and ready-to-paste `C0_PF` / `CAL_ER[]` lines.
5. Paste them in and set `COLLECT 0` before submitting.

> Opened bags exchange moisture with room air at about 0.05–0.07 %MC per minute. Measure within the first minute after opening, or the bag label no longer holds.
> The current table comes from the earliest run (19 Nov 08:23–08:34). Raw data is in [data/](data/).

## Output

Press and release the button. Each measurement prints:

```
"TGR-17"
Moisture Level = 11.52 %

```

The day 1 firmware prints `Capacitance = 12.34 pF` in the same format. Response time is about 0.8 s (day 1) and 1 s (day 2).

If no valid reading arrives within 2.5 s, the output is `… = ERROR`.

## Before submitting (day 2)

- [ ] Re-measure `C0` in the contest room with the `c0` env, and update `C0_PF`
- [ ] `COLLECT 0` in `moisture.cpp`
- [ ] Flash `pio run -e moisture -t upload` (not `c0`, not the default env)
- [ ] Empty box reads ≈ 3.34 %
- [ ] Wire taped down, trimpot untouched
- [ ] No code or hardware changes after submission

## Configuration

| Setting | Location | Default |
|---|---|---|
| Samples averaged per reading | `CAP_SAMPLES` in `main.cpp` / `calib.cpp` · `moisture.cpp` / `c0.cpp` (keep these two equal) | 256 · 300 |
| R1, Vcc, Vref, timeout, discharge time | `CapConfig` in [include/cap_meter.h](include/cap_meter.h) | 1 MΩ, 3.3 V, 1.65 V, 5 ms, 3 ms |
| Meter gain / offset | `main.cpp`, `moisture.cpp`, `c0.cpp` | from the `calib` run |
| Empty-box capacitance | `C0_PF` in `moisture.cpp` | from the `c0` run |
| Moisture table | `CAL_ER` / `CAL_MC` in `moisture.cpp` | 3 points |
| Fit | `FIT_MODE` in `moisture.cpp` | `1` (piecewise); `0` = least-squares line |
| Calibration mode | `COLLECT` in `moisture.cpp` | `0` |
| Debug timing output | `SHOW_DEBUG` in `main.cpp` | `0` |

## Project structure

```
├── include/
│   ├── cap_meter.h          # Capacitance measurement API
│   ├── AnalogInput.h        # On-chip ADC helper (unused)
│   ├── MqttManager.h        # MQTT + JSON helper (unused)
│   └── secrets.example.h    # Template for git-ignored secrets.h
├── src/
│   ├── main.cpp             # Day 1 firmware: pF meter
│   ├── calib.cpp            # Meter calibration (env: calib)
│   ├── moisture.cpp         # Day 2 firmware: moisture meter + COLLECT mode
│   ├── c0.cpp               # Empty-box C0 measurement (env: c0)
│   ├── cap_meter.cpp
│   ├── AnalogInput.cpp
│   └── MqttManager.cpp
├── data/                    # Moisture calibration runs (CSV)
├── docs/                    # Status and task notes (Thai)
├── _disabled/               # Parked modules (ADS1115, NTC, psychrometer) — not compiled
└── platformio.ini           # Envs: esp32s3-n16r8 (default), calib, moisture, c0
```

## Roadmap

- [x] pF capacitance meter with calibration mode
- [x] Parallel-plate sensor for coffee-bean moisture
- [x] Moisture calibration (`Moisture Level = xx.xx %` output)
- [ ] Temperature compensation

## Team

**TGR-17** — IOTE KMITL
