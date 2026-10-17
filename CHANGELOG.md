# Changelog

Newest first. Each entry says which revision it belongs to:
**FW** = firmware (`firmware/controller/version.h`), **LOGIC** = control logic rev,
**HW** = hardware rev, **SCH** = schematic rev, **DISP** = display software rev (`DISPLAY_REV`),
**BENCH** = bench sketches (`tests/common/bench.h`),
**DOC** = documentation only.
Change notes with the reasoning live in `docs/changes/`. `CLAUDE.md` is the authoritative spec.

## Revision index
| Date | FW | Disp | Bench | Logic | HW | Schematic | Git tag |
|---|---|---|---|---|---|---|---|
| 2026-10-17 | 0.3.0 | 1 | 1.1 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.3.0` |
| 2026-10-17 | 0.2.0 | — | 1.1 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.2.0` |
| 2026-10-17 | 0.1.0 | — | 1.0 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.1.0` |
| 2026-10-17 | — | — | — | 2 | 4b + CS mod + SHT30 | rev11 | `logic-r2` |
| 2026-10-17 | — | — | — | 1 | 4b + CS mod | rev10 | `logic-r1` |

---

## FW 0.3.0, DISP rev 1 — 2026-10-17
First display-software revision. Host syntax check of both variants; the splash was rendered on a simulated
280x240 framebuffer (all three penguins, animation and clear verified). Not yet compiled for AVR.

### Added
- Boot splash: three original Pingu-style penguins (hobo, sit, bow), one at random per boot, never the same twice
  across warm resets. Each noots twice (beak → trumpet / O-lips) in ~3 s, non-blocking, then clears to black.
- `tools/sprites/make_pingu.py` generates `boot_sprite.h` (RLE, 16-colour palettes, beak patches) and
  `docs/display/pingu_preview.png`.
- `DISPLAY_REV` in `version.h`, printed in the banner.

### Changed
- Display is landscape: 280x240 on the 1.69" (in use), 320x170 on the 1.9". `DISP_ROTATION` = 1.
- `layout.h` is now per field {row, labelX, valueX, width}: header row, two columns of five, message row.
- `11_ui_dev`: new layout format (`c` command places a field), truncation warnings, splash replay page.
- `06_display`: landscape, positions fit both variants.
- dTpot with K_POT shows whole degrees ("28 450W") to fit the column.

## FW 0.2.0 + BENCH 1.1 — 2026-10-17
Still not compiled for AVR in the build session (host syntax check of both firmware variants,
normal and SIM, plus all 12 sketches; LED behaviour simulated).

### Changed (FW)
- Status LED scheme decided: Mode 1 white / Mode 2 blue / Mode 3 red slow breathing, 3 fast green pulses
  on a mode change. Driven from `loop()` only so a hang freezes it (watchdog resets 2 s later).
  Overlays kept as ASSUMED: 12V absent off, latched fault fast red, alarm yellow flash.
- Display geometry moved to `layout.h` (one block per variant) so `11_ui_dev` can generate it.

### Added (FW)
- Trimpot test build: `NTC_TRIMPOT_SIM` (3296W 10k as rheostat → −30…+50 °C linear), with optional
  `SIM_FORCE_12V` and `SIM_FAKE_P2_TACH` for a breadboard. SIM shown in banner and on the display.

### Added / changed (BENCH)
- `tests/common/bench.h`: shared outputs-LOW, debounced BTN1/BTN2 (short / long / held / edges), LED helpers.
- All tests 00–09 operable from BTN1/BTN2 (maps in each header). 00 now demos the LED heartbeat and
  simulates a hang (BTN2 long). 03 checks fuse scenarios PASS/FAIL. 07 jogs V2 hold-to-run.
- New `10_v2_calibration`: travel times, backlash, indicator linearity, on-loop flow characteristic sweep
  with DP/T2-floor, stall and 12 V aborts. Prints config lines.
- New `11_ui_dev`: layout tool with scenarios, grid, colour/font page, live edits, draw timings,
  prints a `layout.h` block.

## FW 0.1.0 — 2026-10-17 (logic rev 2, HW 4b + CS + SHT30)
First firmware. **Not yet compiled for AVR** (no toolchain in the build session) — first IDE compile may
surface small errors. Host-side syntax check and a control-logic simulation passed.

### Added
- `firmware/controller/`: safe boot (outputs LOW first), Timer3 25 kHz PWM (0 % = pin held LOW),
  tach via INT2/INT3/PCINT6, fuse sense with hysteresis + debounce, 12V presence.
- NTC T1/T2/T3 (Beta + offset, EMA filter, 2 s out-of-range debounce).
- SHT31 A/B on hardware I2C (50 kHz, Wire timeout) + SHT30 on in-tree soft I2C, CRC, 3-fail/3-good validity.
- Logic rev 2 state machine: NO12V → HOME → IDLE ⇄ START → RUN → STOP, SAFE on latched faults.
- V2 driver: single interlocked VO/VC writer, reversal dead time, θ model with open/close times and
  backlash, every move to bypass = full park with overdrive and re-zero.
- Fan staging after V2, start rate limit, T_out estimate, feasibility guard, sleep/wake.
- Buttons (BTN1 = Mode 1, BTN2 short/long = Mode 2/3, any press acks cleared faults), EEPROM mode.
- Status LED scheme (PROPOSAL, provisional).
- ST7789 display: both variants, 2 MHz re-sent init, one field per loop pass, 5-min non-blocking re-init,
  rotating fault/alarm line.
- CSV log at 1 Hz, `?` status dump, watchdog 2 s, reset-cause capture.
- `tests/00…09` bench sketches; `02` includes the SHT30, `01` can measure Beta with a second point.

### Not implemented
- Freeze guard (PROPOSAL, awaiting decision).

## 2026-10-17 — LOGIC rev 2, SCH rev11, HW SHT30 add-on
Change note: [`docs/changes/logic_rev2.md`](docs/changes/logic_rev2.md)

### Changed
- Modes rewritten as three setpoint modes: Mode 1 = branch off (EEPROM default),
  Mode 2 = load cap T_set = max(T_room + δ, DP + 4), Mode 3 = DP + 4 (BTN2 long).
- Sleep = V2 parked ≥ 10 min; wake = T1 > T_set + 1 °C.
- Feasibility guard: V2 opens only if T_out < T1 − 5 °C; forced bypass if T3 > T1 − 1 °C for 2 min.
- Backstop is now T1 < DP + 2 °C (was T_GPU < T_set − 1).
- DP = max of all valid sources (SHT31 A, B + SHT30 room); alarm on > 2 °C spread.
- Stop order: V2 park → P2 off → V1 off 10 s later.
- V2 position model: separate open/close travel times + backlash, re-zero at bypass.
- P2 fixed at 80 %.
- Naming P1/P2, V1/V2, T1/T2/T3 used everywhere.
- A5 NTC moved from the GPU block to the loop return (T1); SHT30 added on soft I2C D34/D36.

### Removed
- Rev 1 Mode 1 enable/disable thresholds, Mode 2 idle/load margins and the open load-trigger item,
  GPU NTC backstop, separate override mode.

### Spec interpretations (ASSUMED, need confirmation)
- T_out = T3 after the rad has been stagnant `T_OUT_SETTLE_MIN`; held while flowing; a forced bypass
  invalidates it until re-settled.
- Wake also requires feasibility (otherwise it would sleep again 10 min later).
- Mode 3 with < 2 valid DP sources runs the Mode 2 setpoint + alarm.
- Faults still drop V1 and P2 immediately (the 10 s V1 delay is for normal stops only).
- T1 backstop and T2 trip armed only while P2 runs; T1 < DP + 2 with P2 off is an alarm.
- Fault latching: faults latch, any button press clears those whose cause is gone.

### Docs
- `CLAUDE.md` rewritten for logic rev 2; revision tracking section added.
- Schematic rev11 added (`docs/schematics/`); rev10 kept.

## 2026-10-17 — LOGIC rev 1 baseline (tag `logic-r1`)
- Imported the rev 1 spec (`CLAUDE.md`), BOM rev 4b and schematic rev10 as received.
