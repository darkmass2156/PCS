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
| 2026-10-27 | 0.4.6 + lite 1.0 | 6 | 1.4 | 2 | 4b + CS mod + SHT30, dividers 330/660 | rev11 | `fw-v0.4.6` |
| 2026-10-27 | 0.4.5 | 6 | 1.4 | 2 | 4b + CS mod + SHT30, dividers 330/660 | rev11 | `fw-v0.4.5` |
| 2026-10-26 | — | — | 1.4 | 2 | 4b + CS mod + SHT30 | rev11 | — |
| 2026-10-26 | — | — | 1.3 | 2 | 4b + CS mod + SHT30 | rev11 | — |
| 2026-10-17 | 0.4.4 | 6 | 1.2 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.4.4` |
| 2026-10-17 | 0.4.3 | 6 | 1.2 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.4.3` |
| 2026-10-17 | 0.4.2 | 5 | 1.2 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.4.2` |
| 2026-10-17 | 0.4.1 | 4 | 1.2 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.4.1` |
| 2026-10-17 | 0.4.0 | 3 | 1.2 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.4.0` |
| 2026-10-17 | 0.3.1 | 2 | 1.1 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.3.1` |
| 2026-10-17 | 0.3.0 | 1 | 1.1 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.3.0` |
| 2026-10-17 | 0.2.0 | — | 1.1 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.2.0` |
| 2026-10-17 | 0.1.0 | — | 1.0 | 2 | 4b + CS mod + SHT30 | rev11 | `fw-v0.1.0` |
| 2026-10-17 | — | — | — | 2 | 4b + CS mod + SHT30 | rev11 | `logic-r2` |
| 2026-10-17 | — | — | — | 1 | 4b + CS mod | rev10 | `logic-r1` |

---

## FW 0.4.6 + lite170 1.0 — 2026-10-27
Host syntax check (controller, lite170, 11_ui_dev); host render of the lite boot and all three 320x170 screens with
full controller data, a fault and the lite data (`docs/display/preview170_*.png`). Not yet compiled for AVR.

### Added
- `firmware/lite170`: lite case display for the 1.9" (320x170, 4 MHz) with the two SHT31s wired in permanently.
  Boot Pingu → crash → ROG, then Mode 1/2/3 screens re-laid out for 320x170 (`screens170.*`, class `Screens170`).
  Live case T / RH / dew point (highest valid of A/B; 3 bad reads invalidate, first good read validates at boot),
  everything else "--"; NO DEW POINT banner if neither sensor is valid. Buttons and LED as the controller, mode
  and splash bytes in EEPROM 0..3 shared with it. Serial line per read.
- `tools/ui_preview`: framebuffer size per build (`-DFB_W/-DFB_H`), `preview170.cpp`.

### Changed (FW, no visible change on the 1.69")
- `splash.h` works on any panel height: band count from H (15 on 170 rows, last band partial), underline and
  version tucked under the logo when there is no room for the usual gap.

## FW 0.4.5 + HW note — 2026-10-27
Host syntax check. Bench: 1.5 m C4 cable with the new dividers works up to 6 MHz.

### Changed
- HW: display dividers 1k/2k → 330/660 Ω (change note `docs/changes/display_dividers.md`). With 1k/2k the panel
  stayed black on the finished cable, also at 1 MHz.
- `DISP_SPI_HZ` 2 → 4 MHz; `06_display`, `11_ui_dev`, `12_disp170_sht31` follow.

## BENCH 1.4 — 2026-10-26
Host syntax check (both sensor modes) and a host run of the log: 600 samples with a reset and a B dropout,
CSV dump and log page checked (`docs/display/preview_disp170_log.png`). Not yet compiled for AVR.

### Added (`12_disp170_sht31`)
- Night log in EEPROM: every `LOG_INTERVAL_S` (60 s) the average T / RH of A and B, 4 bytes per sample
  (T 0.2 °C over −10…40.6 °C, RH 0.5 %), ring of 1016 samples = 16.9 h. EEPROM 16..4095 only, the firmware's
  bytes 0..3 stay untouched. A reset or power cut while logging resumes with a gap marker, so opening the serial
  monitor doesn't lose or end the log. Serial `d` dumps CSV (sample, minutes, A_T, A_RH, B_T, B_RH), `l` / `x` /
  `e` start / stop / erase.
- Log page (BTN1 cycles live → log → panel check): state, duration, fill, and T and RH charts over the whole log,
  auto-scaled, gaps marked. "LOG n" badge on the live page while logging.

### Changed
- Buttons: BTN2 short = other address (SENSORS 1) / reset counters (SENSORS 2), BTN1 long = new log,
  BTN2 long = stop log. Invert / BGR are serial only (`i` / `b`). `SENSORS 2` is the default again.

## BENCH 1.3 fix 3 — 2026-10-26
### Added (`12_disp170_sht31`)
- `SENSORS 1` (now the default): one sensor on its own screen, address `SINGLE_ADDR`, BTN2 long / serial `a`
  switches 0x44 ⇄ 0x45 (counters reset, sensor re-initialised). Shows T, RH, DP, status register (heater
  flagged), counters and the raw T/RH ticks; serial prints the raw ticks too. `SENSORS 2` keeps the A/B page.

## BENCH 1.3 fix 2 — 2026-10-26
First run on the bench: display OK, but ~60 % failed SHT reads and sensor A at 67 °C / 17.6 % (valid CRC).
### Fixed / added (`12_disp170_sht31`)
- On-screen status said "CRC FAIL" for a read NACK. Errors are now split: trigger NACK (with the Wire code:
  e2 address, e3 data, e5 timeout), read NACK, CRC, timeout; serial `[ok/trig/read/crc/timeout]`.
- Sensors are soft-reset, heater off and status cleared at boot (and on serial `h`); the status register is
  read after each good read and "HEATER ON" shown if bit 13 is set.
- Reads are sequential (A, then B) instead of both triggered together.
- I2C scan at boot and on serial `s`.

## BENCH 1.3 fix — 2026-10-26
### Fixed
- `12_disp170_sht31` black screen with backlight on: `TFT_RST` defaulted to -1, and on a breadboard the module's
  RES floats (no pull-up on most 1.9" modules), so the panel never leaves reset. Default RES pin is now D48
  (via divider). Added a colour flash at boot (R, G, B, white), serial `m` (SPI mode 0/3 + re-init, for
  modules without CS) and `c` (colour flash). Header warns that the module's SDA/SCL are SPI (D51/D52).

## BENCH 1.3 — 2026-10-26
Host syntax check and a host render (`docs/display/preview_disp170_sht31.png`). Not yet compiled for AVR.

### Added
- `tests/12_disp170_sht31`: the 1.9" ST7789 (`init(170, 320)`, landscape 320x170, 2 MHz) and SHT31 A/B on
  hardware I2C (50 kHz, wire timeout). Live T / RH / DP per sensor with ok/NACK/CRC counters, DP spread and
  T A−B, flagged red over 2 °C; non-blocking 2 s read cycle; one Serial line per read. Panel check page
  (frame on all edges, RGB bars). Buttons: BTN1 page, BTN2 invert, BTN1 long BGR, BTN2 long reset counters.
  Same pins as the controller; `TFT_RST` for modules with a RES pin. Uses the dashboard AA fonts.

## FW 0.4.4 — 2026-10-17
Host syntax check of firmware and `11_ui_dev`. Not yet compiled for AVR. Animation unchanged.

### Fixed
- `11_ui_dev`: replaying the boot sequence (BTN2 long / `s` / `p0..p2`) left the dashboard black, because the
  screens still considered every widget drawn and only repainted values that changed. New `Screens::redraw()`,
  called when the splash ends.
- `BootSplash::begin()` resets the hardware scroll and invert, so a restart in the middle of the crash doesn't
  leave the picture shifted or negative (firmware only starts it once, so no visible change there).

## BENCH 1.2 fix — 2026-10-17 (no FW change)
### Fixed
- `11_ui_dev` still always opened with the hobo in Wokwi: the startup penguin came from an EEPROM rotation, and
  Wokwi starts every run with a blank EEPROM. Now picked like the firmware (random, seeded from ADC noise +
  micros, never the last one shown). The seed is printed at boot.

## FW 0.4.3, DISP rev 6 — 2026-10-17
Host-rendered with `tools/ui_preview`; host syntax check of `11_ui_dev`. Not yet compiled for AVR. Boot animation unchanged.

### Fixed
- Mode 2 big numbers clipped on the right: the oblique digits lean 2–5 px past their advance width and the
  glyph cell was cut at the advance (the next glyph painted over the overhang, the last one lost it).
  `make_fonts.py` now widens each cell to hold all its ink; regenerated `fonts.h` (all fonts, 1 px on a few
  DejaVu glyphs like T/Y/r).
- `11_ui_dev` played all three penguins and the ROG sequence three times before the UI (0.4.2 Wokwi startup
  loop). Back to one boot sequence per startup; the rotation across resets stays.

## FW 0.4.2, DISP rev 5 — 2026-10-17
Host-rendered with `tools/ui_preview` (now with a 2 MHz SPI time model); host syntax check of firmware and
`11_ui_dev`. Not yet compiled for AVR.

### Changed
- Boot: Pingu → ROG is now a fast crash (~0.8 s on the real bus, boot ~5.3 s). The 0.4.1 glitch was designed
  for 0.9 s but its full-width redraws took ~3.3 s at 2 MHz, plus 0.5 s to settle. New: crash mid-noot, hardware
  scroll jolts (free), invert flashes, wrong-colour tears, then the screen breaks up in 20 random bands that each
  land on their final ROG content (no separate settle pass).
- Mode 2: big numbers in FreeSans Bold Oblique (replaces the segment digits), labels/values/banner in the
  Mode 1 DejaVu fonts. Chips read EXT / V2 / FAN.
- Mode 3: all text in DejaVu Sans Mono 11 px (replaces 5x7); P&ID, table (12 px rows) and chart (0–35 °C over
  47 rows) re-fitted.
- `fonts.h`: + F_SPORT, F_MONO (~11.8 KB flash total).

### Fixed
- `11_ui_dev` always opened with the hobo: the penguin index started at 0 on every reset (and opening the serial
  monitor resets the Mega). Now rotates across resets via EEPROM addr 3; the Wokwi build plays all three at
  startup; serial `p0`/`p1`/`p2`.

## FW 0.4.1, DISP rev 4 — 2026-10-17
Host-rendered with `tools/ui_preview` (Mode 1 all states, boot frames of all three penguins); host syntax check.
Not yet compiled for AVR.

### Changed
- Mode 1 uses anti-aliased proportional fonts instead of the scaled 5x7 font and segment digits: FreeSans Bold
  33 px for the three big temperatures ("12.3°"), DejaVu Sans Bold 15 px for values, DejaVu Sans 10 px for
  labels. Layout re-fitted; tile values sit right of their measured label.
- Boot: the penguin glitching out and the ROG logo glitching in now overlap. Torn logo bands cut in while the
  penguin still tears, take over with shrinking displacement, then the logo resolves clean in white and a red
  scan crosses it (replaces "black, then grey logo, then white wipe").

### Added
- `fonts.h` + `ftxt()` in `scr_gfx`: 2-bit AA bitmap fonts streamed through one window per field.
  Generator `tools/fonts/make_fonts.py` (DejaVu Sans, GNU FreeSans).

### Fixed
- Red scan: the final white-restore slice could request columns past the logo edge (short address window).
  `rog()` now clamps to the logo width and clips shifted bands to the screen.

## FW 0.4.0, DISP rev 3, BENCH 1.2 — 2026-10-17
New dashboard: one screen per mode, ROG palette, plus the Pingu → glitch → ROG boot sequence. Rendered on the
host with `tools/ui_preview` (all modes: normal, changed values, fault, no 12V; boot frames of all three
penguins), host syntax check of firmware and `11_ui_dev`. Not yet compiled for AVR.

### Added
- `screens.*` (Mode 1 standard, Mode 2 sport/graphite, Mode 3 engineer with P&ID, raw table and a 272 s sweeping
  T2/T_set/DP chart), `scr_gfx.*` (palette, padded text, slanted shapes, segment digits redrawn per segment),
  `scr_data.h` (`UiData`, filled once per second by `display.cpp`).
- Boot: glitch transition (sprite bands torn sideways, scanline noise, two invert flashes, black bands) and the
  ROG logo (dark grey → white wipe with red edge → underline + version). `rog_logo.h` from Vlad's 280x240 artwork
  via `tools/sprites/make_rog.py` (run-length, round-trip checked).
- `tools/ui_preview/`: host framebuffer stand-in for the display library; renders the real display code to
  `docs/display/preview_boot.png` and `preview_modes.png`.

### Changed
- Big values: outside rad, loop 1 return, mixing valve. Smaller: case/room temp, humidity, dew point; cooling W,
  external loop, mixing valve, outdoor fan, distance to dew (T2 − DP max).
- Case values show the highest of the two SHT31s, per value. `K_POT` = 25.0 W/°C (placeholder).
- Clock removed; a 1 Hz dot stays as the frozen-screen check.
- Faults: red banner in Modes 1/2 (rotating through active faults), bottom line in Mode 3.
- `layout.h` removed (each mode has its own layout now). Variant 2 (1.9") has no layouts: compile error.
- BENCH 1.2: `11_ui_dev` runs the real screens and boot sequence with fake data (BTN1 mode, BTN2 scenario,
  BTN1 long animate, BTN2 long boot sequence).

## FW 0.3.1, DISP rev 2 — 2026-10-17
Fixes from the first Wokwi run (ILI9341 part driven by the ST7789 code). Splash re-verified on a strict
framebuffer model (every window filled exactly, nothing off screen); host syntax check for both targets.

### Fixed
- Hobo splash never appeared: the "last shown" index lived in `.noinit` RAM without a validity check, so a
  cold start with RAM at 0 always excluded penguin 0. Now EEPROM addr 3, and the seed mixes an EEPROM boot
  counter (addr 2) because micros()/ADC noise are almost the same every boot.
- Streaks and mirrored fragments in the beak area: the patch was drawn as ~50 separate fillRects, each with
  its own window commands; the corruption pattern (1-2 px streaks, patch pixels mirrored across the screen)
  is window/orientation commands going wrong in transit. Sprites and patches now stream through one address
  window per chunk (~13 windows per splash), opaque.

### Changed
- Vlad's MADCTL mirror support adopted, behind `DISP_TARGET_ILI9341_SIM` (1 = simulator: mirror, BGR, no
  invert; 0 = real ST7789: no mirror, RGB, invert). Banner warns on a simulator build. `11_ui_dev` follows.

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
