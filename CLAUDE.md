# PCS — 570X outdoor free-cooling controller (Arduino Mega 2560)

Firmware for the outdoor free-cooling branch of a watercooled PC (Corsair 570X).
The Mega runs P2, the NC solenoid V1 and the motorized 3-way valve V2. Its main job:
coolant entering the PC must never drop below in-case dew point + margin.

## Revisions (current)
| Item | Rev | Where |
|---|---|---|
| Hardware | 4b + display CS mod + SHT30 on D34/D36 | this file, `docs/build_component_list_rev4b.xlsx` |
| Schematic | rev11 | `docs/schematics/full_loop_schematic_rev11.png` |
| Control logic | rev 2 | this file → Control; change note `docs/changes/logic_rev2.md` |
| Display software | rev 2 (landscape, boot splash, sim/real target) | this file → Display; `DISPLAY_REV` in `version.h` |
| Firmware | see `firmware/controller/version.h` | `CHANGELOG.md` |

### How revisions are tracked
- `CHANGELOG.md` has one entry per firmware version and per doc revision (newest first).
- A logic or hardware change gets a change note in `docs/changes/` (what changed and why),
  then this file is updated to match. This file stays the authoritative spec; change notes are history.
- Firmware version is in `firmware/controller/version.h` (semver) together with `LOGIC_REV`, `HW_REV` and
  `DISPLAY_REV` (bump DISPLAY_REV on any change to what the screen shows: layout, splash, drawing).
  It's printed at boot and in the CSV header. Bump it with every firmware change.
- Git tags: `logic-rN` on the commit that introduces logic rev N, `fw-vX.Y.Z` on firmware releases.
- Superseded schematics stay in `docs/schematics/` with their rev number.

## How we work
- Vlad uses the Claude Code desktop app and uploads with **Arduino IDE 2**.
  Don't run terminal commands, install toolchains or open COM ports on his machine unless he asks.
- The Mega is a CH340 clone. If the IDE doesn't see the board, the CH340 USB driver is the first suspect.
- Arduino IDE rule: each sketch sits in a folder with the same name as its `.ino`.
  Extra `.h/.cpp` files go flat in that same folder.
  - Bench tests: `tests/NN_name/NN_name.ino` (each one standalone)
  - Firmware: `firmware/controller/controller.ino`
- Every bench test prints to Serial at 115200. Vlad pastes the output back.
- Every bench test is driven by **BTN1/BTN2** (breadboard buttons to GND work: tests use INPUT_PULLUP);
  the button map is in the sketch header and printed at boot. Serial commands stay as a backup.
- Shared bench code is `tests/common/bench.h` (canonical, `BENCH_VERSION`); each test folder holds an
  identical copy. Same for `softi2c.*`, `layout.h`, `splash.h` and `boot_sprite.h` (canonical in `firmware/controller/`).
- If a library is needed, name it exactly as it appears in Library Manager so Vlad can install it.
  Current list: **Adafruit ST7735 and ST7789 Library**, **Adafruit GFX Library** (+ its dependency Adafruit BusIO).
  Everything else (SHT3x, soft I2C) is in-tree.
- Keep replies short. Vlad knows electronics; skip the basics. Temperatures in °C.
- Anything marked OPEN or PROPOSAL: ask Vlad before implementing.
  Anything marked ASSUMED is implemented but needs Vlad's confirmation.

## Naming
| Name | Part | Mega |
|---|---|---|
| P1 | EK Kinetic D5, in case, fixed 90 % on the Apex | — |
| P2 | Bykski D5 + res, controller box | PEN D26, PWM D3, tach D12 |
| V1 | NC solenoid 2W-025-08 | SEN D28 |
| V2 | CR-02 3-way T-port (LEFT outdoor, RIGHT bypass, TOP out) | VO D22, VC D24 |
| T1 | NTC, internal loop return, between the 360 outlet and the tee | A5 |
| T2 | NTC, V2 outlet (control sensor) | A4 |
| T3 | NTC, outdoor XE 240 | A3 |
| T_room | SHT30, outside the box on the room side | soft I2C D34/D36 |
| DP_A / DP_B | SHT31 ×2 in case | I2C 0x44 / 0x45 |
| DP_R | dew point from the SHT30 | — |
| T_out | outdoor temperature estimate (see Feasibility) | — |

## Hardware
- **Controller**: MEGA2560 R3 (CH340), powered over USB from the PC.
  The 12V for loads comes from the PC PSU on a separate harness, so USB can be live while
  12V is off (S3/S5).
- **Boards**: two 60x80 perfboards (rev 4b). Board 1 = power, Board 2 = logic, linked by 34-pin ribbons.
- **P1**: in the case, controlled by the PC (Apex). Not on the Mega.
- **P2**: the Mega switches its 12V (PEN) and drives PWM and tach.
- **V2**: CR-02 12V, T-port, DN15, 3-wire with autostop.
- **V1**: 2W-025-08, 1/4", 12V, normally closed.
- **Fans**: 2× NF-F12 iPPC-3000 on the outdoor XE 240. Their 12V is unswitched; only PWM controls them.
- **NTCs**: 3× Bykski B-PD5-TMR 10k probes on the Mega (T1, T2, T3). The cube NTC goes to the Apex.
- **Humidity**: 2× SHT31 in the case (hardware I2C) + 1× SHT30 on the room side (software I2C, own bus
  because 0x44/0x45 are taken). SHT30 powered from the Mega header 5V/GND; no board change.
- **Display**: one ST7789 IPS screen in the case (see Display), mounted landscape. The 1.69" MSP1691 is the
  one in use (280x240 landscape); the 1.9" stays supported as variant 2.
  The 0.91" I2C OLEDs are **not** used.
- **Top panel**: 570X buttons BTN1 and BTN2, plus an RGB status LED under the power button.
  Button 3 and the power button go to the Apex, not the Mega.
- **Switched outputs**: all are **Mega HIGH = on**. Hardware pull-downs keep everything off during reset.
  - VO / VC: BC547 → IRF4905 high-side. These drive V2 open / close.
  - PEN: BC547 → IRF4905 high-side, switching P2's 12V (PWM alone can't stop a D5).
  - SEN: IRLB8721 low-side, driving V1 (HIGH = energised = open).

## Pin map (rev 4b + display CS mod + SHT30 — authoritative)
| Pin | Signal | Notes |
|---|---|---|
| A0 | S1 | P2 fuse sense, 47k/20k divider (12V ≈ 3.58 V) |
| A1 | S2 | fan fuse sense |
| A2 | S3 | V1 + V2 fuse sense |
| A3 | T3 | outdoor XE 240 |
| A4 | T2 | V2 outlet — control sensor |
| A5 | T1 | loop return (360 outlet → tee), C5 cable rerouted |
| D2 | FAN_PWM | OC3B, 25 kHz |
| D3 | PUMP_PWM | OC3C, 25 kHz |
| D9 / D10 / D11 | LED R / G / B | common-cathode RGB, analogWrite (Timer2 / Timer1) |
| D12 | PUMP_TACH | not an INT pin → PCINT6 (PCINT0 group) |
| D18 | TACH1 | INT3 |
| D19 | TACH2 | INT2 |
| D20 / D21 | SDA / SCL | 4.7k pull-ups to 5V |
| D22 | VO | V2 open |
| D24 | VC | V2 close |
| D26 | PEN | P2 enable |
| D28 | SEN | V1 enable |
| D30 / D32 | BTN1 / BTN2 | 10k pull-up, 1k + 100 nF RC, active LOW |
| D34 / D36 | SDA2 / SCL2 | SHT30, software I2C (module pull-ups + internal pull-ups) |
| D49 | DISP_CS | ST7789 CS via 1k/2k divider, Mega ribbon pin 32 |
| D51 / D52 / D53 | MOSI / SCK / DC | ST7789 via 1k/2k dividers; D50 unused |
| 3.3V pin | display 3V3 | powers the ST7789, backlight included |

## Connectors (for bench wiring)
- **Board 1 screw terminals** (row AA):
  - X1: +12V in, spare, GND in, T3 NTC
  - X2: T3 NTC return, T2 NTC, T2 NTC return, spare
  - X3: V2 open, V2 close, V2 COM, V1 +
  - X4: V1 −, P2 −, P2 +, spare GND
- **Board 1 4-pin headers**: FAN1, FAN2, and PUMP (P2; pin 2 = switched 12V).
- **GX12 cables** (box ↔ case):
  - GX12 at both ends: C1, C2 and C5.
  - GX12 at the 570X bracket only, hardwired into the box: C3 and C4.
  - C1 2P: 12V feed
  - C2 4P: SHT31 ×2 (5V, GND, SDA, SCL)
  - C3 6P: top panel (BTN1, BTN2, LED R, G, B, GND)
  - C4 6P: display (GND, 3V3, SCK, MOSI, DC, CS)
  - C5 3P: T1 NTC (NTC, GND, spare) — rerouted from the GPU block to the loop return
  - C6: USB to the Mega (no GX12)
- **Board 2 display CS mod** (on top of rev 4b):
  - R22 1k at L7–P7, R23 2k at Q7–U7.
  - Bridges P7–Q7 and U7–V7.
  - Wires: S2 (Mega ribbon pin 32) > L7, and P7 > A20.
  - The DISP header becomes A15–A20, with A20 = CS.
  - Mega end: D49 on a 1-pin Dupont next to the SPI 2×2 housing.
- **SHT30**: direct to the Mega header (5V, GND, D34 SDA2, D36 SCL2).

## Interfaces
- **NTCs** (three, all the same probe model): 10k 1% pull-up to 5V, NTC to GND, 100 nF.
  Readings are ratiometric (AREF = AVCC).
  - Resistance: R = 10000 · adc / (1023 − adc).
  - One Beta (ASSUMED 3435 until `01_ntc` measures it) plus a per-sensor offset from ice-water calibration.
  - Failure modes: a short reads HOT (the dangerous direction); an open circuit reads cold.
- **Fuse sense**: compare against thresholds with hysteresis (see Tunables). Binary present/absent check.
- **Tach**: open-collector, 10k pull-up to 5V, 2 pulses/rev.
- **PWM**: Timer3, fast PWM mode 14, no prescaler, ICR3 = 639 → 25 kHz.
  - 0 % disconnects the compare output and holds the pin LOW (OCR = 0 still gives a 62 ns spike).
  - Use no libraries that take Timer3. Leave Timer1 and Timer2 alone (they drive the LED).
- **SHT31 ×2**: A = 0x44 (top-rear, near CPU) and B = 0x45 (mid-case, near GPU/RAM). Hardware I2C at 50 kHz,
  `Wire.setWireTimeout` on. Check the CRC on every read.
- **SHT30**: 0x44 on its own software I2C bus (D34/D36), same command set and CRC.
- All three use single-shot, no clock stretching (0x2400), triggered every 2 s and read 20 ms later.

## Display
- **Target switch** `DISP_TARGET_ILI9341_SIM` (config.h): 1 = Wokwi simulator, whose only panel is an ILI9341
  (reads MADCTL mirrored, wants BGR, no inversion) → `DISP_MIRROR_X 1`, `DISP_BGR 1`, `DISP_INVERT 0`.
  0 = the real ST7789 → mirror 0, BGR 0, invert 1. The banner says when a build is for the simulator.
  **Set 0 before flashing the real controller.**
- **Landscape** (display rev 1): `DISP_ROTATION 1` (3 if mounted upside down). `init()` takes the panel's native
  portrait size (`LAYOUT_INIT_W/H`), the rotation turns it into `LAYOUT_W x LAYOUT_H`.
- **Two variants, one compile-time switch** (`DISPLAY_VARIANT` in `config.h`). Code must work on both.
  - `1` = MSP1691, 1.69", **280x240** landscape (in use)
  - `2` = MSP1901, 1.9", 320x170 landscape
- **Library**: Adafruit_ST7789 + Adafruit_GFX. Use `init(240, 280)` or `init(170, 320)`; the library handles the panel offsets.
- **Field report (1.69", one buyer)**: Adafruit with `init(240, 280)` worked. Fallbacks (`config.h`):
  - red and blue swapped → `DISP_BGR 1`
  - colors look negative → flip `DISP_INVERT`
  - image shifted by 20 rows → apply the 20-row offset manually (normally `init(240, 280)` handles it)
  Remaining risks: no reset pin, 5V resistor-divider level shifting, and a ~1.5 m cable.
- **Wiring**: CS = 49 (through the divider), RST = -1 (10k + 100 nF power-on RC at the display), default SPI mode.
- **SPI clock**: ≤ 2 MHz. `init()` itself runs at the library default, so the firmware sets 2 MHz and then
  re-sends the init sequence (SWRESET → SLPOUT → COLMOD → NORON → DISPON) at 2 MHz.
- **D53** stays OUTPUT (DC, and SPI master needs it). **D50** is MISO, unusable as output.
- **Backlight** is tied to VCC, so it's always on with no dimming.
- **Redraw**: 1 Hz, only the fields that changed, one field per loop pass, text drawn with background colour.
  No full-screen clears (they flicker).
- **Self-heal**: uptime heartbeat; non-blocking re-init (SWRESET keeps GRAM, so no blank) every 5 min.
  The RGB LED stays the authoritative status.
- **Layout**: geometry lives in `firmware/controller/layout.h`, one block per variant: a header row
  (mode/state + heartbeat), two columns of five fields, and a message row. Each field is {row, labelX, valueX,
  width}. Tune it on the panel with `tests/11_ui_dev` and paste the block it prints ('p').
- **Boot splash** (`splash.h`, `BOOT_SPLASH 1`): one of three penguins at random (hobo with stick and bundle,
  sitting with the long trumpet, bowing with O-lips); never the same one twice across warm resets.
  He "noots" twice (beak → trumpet / O-lips), ~3 s, then the screen clears to black for the dashboard.
  Non-blocking: one small drawing step per loop pass, so control, LED heartbeat and watchdog keep running.
  Drawing streams pixels through one address window per chunk (6 sprite rows, or a whole beak patch)
  instead of one fillRect per colour run: ~13 windows per splash instead of hundreds.
  The choice is seeded from an EEPROM boot counter (addr 2) and the last penguin is kept in EEPROM (addr 3).
  Sprites are original pixel art generated by `tools/sprites/make_pingu.py` → `boot_sprite.h`
  (preview: `docs/display/pingu_preview.png`). This one-off clear is the only full-screen fill.
- **Fields (rev 2)**: mode/state + heartbeat, T1, T2, T_set, T_out, cooling potential (ΔT, W once K_POT is set),
  T_room, DP, P2 (PWM % + rpm, or OFF), V1 (OPEN/SHUT), V2 angle (`?` when not homed), active fault in red,
  "NO 12V" when the PC is off.

## Hydraulics (matches rev11 schematic)
```
P1 (PC-controlled, fixed 90 %) → cube → GPU → RAM → CPU → 120 rad → 360 rad → T1 → tee
  tee → check valve → P1 res "in 1"
  tee → QDC → P2 → tee
              ├→ V1 (NC) → outdoor XE 240 (T3) → V2 LEFT
              └→ Barrow balancing valve        → V2 RIGHT
           V2 TOP (T2) → QDC → P1 res "in 2"
```
- When P2 is off, the check valve opens and flow returns via "in 1".
- V2 runs between two ends:
  - θ = 90°, all-ports-open: maximum outdoor flow
  - θ = 0°, bypass-only: outdoor port blocked. This is the home end and the safe end.
- V2 has no position feedback; θ is estimated from run time (see Valve).
  ASSUMED: VO moves toward all-ports-open and VC toward bypass-only. **Verify on the bench.**
- Coolant freeze point is −25 °C.
- The blue line and the V2 body sit indoors at outdoor-return temperature, which is a condensation risk inside the box.

## Control (logic rev 2)

### Sensors → derived values
- DP = max of all **valid** dew points (DP_A, DP_B, DP_R), Magnus a = 17.62, b = 243.12.
  - Alarm if any two valid ones differ by > 2 °C.
  - 0 valid → fault → safe state.
- **T_out** (ASSUMED definition — the change note uses it without defining it):
  - The rad is "stagnant" when not flowing (flowing = P2 on, V1 open and V2 θ > 1°).
  - After `T_OUT_SETTLE_MIN` of stagnation, T_out tracks T3. While flowing, and for the settle time after, T_out holds.
  - At boot T_out = T3 (the rad was stagnant while the PC was off).
  - A forced bypass (see Feasibility) invalidates T_out until it has re-settled.
- Cooling potential ΔT_pot = T1 − T_out. Watts = K_POT × ΔT_pot once K_POT is calibrated (0 = hidden).

### Modes (stored in EEPROM addr 0/1, written only when changed; default Mode 1)
| Input | Mode | Behaviour |
|---|---|---|
| BTN1 short or long | **Mode 1, branch off** | P2 off, V1 closed, V2 at bypass. Maintenance state. |
| BTN2 short | **Mode 2, load cap** | T_set = max(T_room + δ, DP + 4). If the SHT30 is invalid: max(20, DP + 4). |
| BTN2 long (≥ 2 s) | **Mode 3, max** | T_set = DP + 4. Needs ≥ 2 valid DP sources; ASSUMED: with fewer it runs the Mode 2 setpoint and raises an alarm. |

- Every button press also clears latched faults whose cause is gone.
- **Sleep** (Modes 2/3): V2 parked at bypass ≥ 10 min → stop sequence.
- **Wake** (Modes 2/3): T1 > T_set + 1 °C **and** feasible (ASSUMED: waking with no way to open V2 would just
  sleep again 10 min later).

### Feasibility (Modes 2/3)
- V2 may open only if T_out < T1 − 5 °C.
- Forced to bypass if T3 > T1 − 1 °C for 2 min while flowing (then T_out is invalid until re-settled).

### Valve V2 (floating control)
- θ = θ₀ ± (90° / t_full,dir) × t_on, with separate open/close travel times.
- Backlash: after a direction reversal, the first `V2_BACKLASH_MS` of motion doesn't move θ.
- Clamp θ to 0–90°. Every move to bypass is a park: drive VC for t_full,close × 1.3, then θ = 0 (re-zero, homed).
- Non-blocking step pulses every `V2_CTRL_PERIOD_MS`, length ∝ error, with a minimum pulse,
  a ±0.5 °C deadband and a dead time before reversing.
- Close fast, open slow (separate gains and max pulse lengths).
- VO and VC are written by ONE function that guarantees they're never HIGH at the same time.
- Rate-limit opening for the first `V2_START_LIMIT_MIN` after a start (stagnant rad sends a cold slug).
- After 12V loss or an S3 fault, θ is unknown (not homed) until the next park.

### Fans
- Staged after V2: held at minimum until V2 ≥ 90 % (81°) and T2 is still above T_set + deadband; then step up.
- When T2 drops below T_set − deadband, step fans down first; V2 only closes once fans are at minimum.
- Held at minimum while T3 < DP (limits condensation on the indoor blue line).
- Outside RUN they sit at minimum. Check on the bench whether the iPPCs stop at 0 %.

### P2
- Fixed `P2_PWM_RUN` = 80 %. ASSUMED: higher duty = faster (verify in `09_pump2`).

### Sequences
- **Start**: park V2 (skipped if already parked) → PEN on + PWM → V1 open → RUN with rate-limited opening, fans at minimum.
- **Stop** (Mode 1, sleep): V2 to bypass (park, overdrive) → P2 off → V1 off `V1_OFF_DELAY_MS` (10 s) later.
- **Fault**: V1 off and P2 off immediately, V2 park in parallel (rev 1 order kept for faults — ASSUMED).

## Safety (non-negotiable)
1. The first lines of `setup()` set all outputs LOW, before Serial or anything else.
2. Safe state, in order:
   1. SEN LOW, PEN LOW, PUMP_PWM 0.
   2. Park V2 (if 12V and S3 are present).
   3. Fans to minimum.
3. Hard low limit (armed while P2 runs):
   - T2 < T_set − 1 °C → park V2 immediately (not a fault).
   - T2 < DP + 1 °C → fault → safe state.
4. Backstop: T1 < DP + 2 °C → fault → safe state (armed while P2 runs; with P2 off it is an alarm only — ASSUMED).
5. Sensor validity:
   - Any NTC outside −40…+80 °C for 2 s → fault → safe state.
   - SHT3x CRC fail, NACK or implausible value 3× in a row → that sensor is invalid (alarm); 3 good reads restore it.
6. 12V presence:
   - All of S1–S3 low → 12V absent (PC off). Hold outputs LOW, no alarm, V2 marked not homed.
     When 12V returns (stable 1 s), park V2 before doing anything else.
   - One line low (for 1 s) → that fuse:
     - S1 → fault (safe state).
     - S2 → alarm, keep running.
     - S3 → fault (V1 is already closed by hardware; V2 can't move).
7. Stalls:
   - P2: tach = 0 for > 5 s after the 3 s spin-up grace with PEN on → fault.
   - Fans: tach = 0 for > 5 s with PWM > 0 → alarm only.
8. Fault latching (ASSUMED — from the rev 1 question list): faults latch; a button press clears those whose
   cause is gone. Alarms are live (clear themselves). Latched faults don't survive a reset.
9. Watchdog on (2 s). CH340 clones can ship with a bootloader that reset-loops with WDT enabled, so test `00_watchdog` first.
10. No `delay()` in the main loop; run a `millis()` state machine.
11. The display is never in the safety path. A display hang or error must not block control.
12. PROPOSAL — freeze guard (P2 off and T3 < −20 °C → periodic low-speed circulation). **Not implemented.**
    Only possible while the PC is on.

## Status and logging
- Status LED (`ui.cpp`), decided by Vlad 2026-10-17:
  | State | LED |
  |---|---|
  | Mode 1 | white, slow breathing (4 s) |
  | Mode 2 | blue, slow breathing |
  | Mode 3 | red, slow breathing |
  | Mode change | 3 fast green pulses (750 ms), then the new mode's colour |
  | 12V absent | off (ASSUMED; green pulses still show on a mode press) |
  | Latched fault | fast red blink 4 Hz (ASSUMED) |
  | Alarm | 100 ms yellow flash every 2 s over the breathing (ASSUMED) |
  - Liveness: the LED is computed from `millis()` in `loop()` only (no timer/ISR). A hung loop freezes the
    LED at its current brightness; the 2 s watchdog then resets (LED off) and breathing resumes after boot.
- Serial CSV at 1 Hz. `#` lines are comments (version banner, header). Columns:
  `ms,mode,state,T1,T2,T3,Tout,Troom,DP_A,DP_B,DP_R,DP,Tset,V2deg,fan%,pump%,rpmF1,rpmF2,rpmP2,V1,fuses,faults,alarms`
  (fuses/faults/alarms as hex bitmasks, see `control.h`).
- Serial input: `?` prints version, tunables and a status line.
- Known: opening the COM port toggles DTR and resets the Mega (CH340 auto-reset) → safe boot, V2 re-park.

## Bench test order
Each test is its own sketch in `tests/`. P2 goes last. The harness only gets built once every test passes.
1. `00_watchdog` — clean boot and a WDT reset check
2. `01_ntc` — all three probes, ice-water offsets, optional second point for Beta
3. `02_sht31_dewpoint` — SHT31 A/B and the SHT30 on soft I2C, CRC, DP spread
4. `03_fuse_sense` — 12V present, 12V absent, and a pulled fuse
5. `04_buttons_led` — short and long press
6. `05_fan_pwm_tach`
7. `06_display` — run it on both variants via `DISPLAY_VARIANT`
8. `07_valve` — direction, t_full open/close, backlash, homing
9. `08_solenoid`
10. `09_pump2`
11. `10_v2_calibration` — t_open/t_close (button-marked), backlash, indicator linearity, and on the filled
    loop the flow characteristic f(θ) = (T1 − T2)/(T1 − T3). Output → V2 tunables and the θ equation.
12. `11_ui_dev` — screen layout tool (scenarios, grid, colours, live layout edits, draw timings) and splash replay.

### Trimpot test build
- `NTC_TRIMPOT_SIM 1` in `config.h`: T1/T2/T3 are 3296W 10k trimpots as rheostats (wiper + one end) in the
  NTC terminals. R 0…10k maps linearly to −30…+50 °C (≈ 3.2 °C/turn). < 50 Ω = short, unplugged = open
  (both raise the NTC fault). Calibration offsets are ignored. Banner and display heartbeat show SIM.
- For a breadboard without Board 1 / P2: `SIM_FORCE_12V 1` (S1–S3 present) and `SIM_FAKE_P2_TACH 1`
  (3000 rpm while PEN is on). Only honoured with `NTC_TRIMPOT_SIM 1`. Never ship a SIM build.
- DP still needs at least one real SHT3x (or the system sits in SAFE with "NO DP").

### Bench checks required by logic rev 2
1. **δ**: run Mode 1 at idle for 30 min, then set δ = (T1 − T_room) + 1.5 °C.
2. **Series flow**: in Mode 2 under load, compare the cube NTC (Apex) with T2.
   cube ≈ T2 → P2 and P1 in series; cube between T2 and T1 → the check valve passes flow; raise P2 to ≥ P1.
3. **V2 travel**: t_full open, t_full close, backlash.
4. **SHT30**: software I2C read with CRC; compare with the SHT31s in the same air.
5. **K_POT**: later, on the real loop.

## Tunables (`config.h`)
- Display: `DISPLAY_VARIANT` = 1, `DISP_SPI_HZ` = 2 MHz, `DISP_INVERT` = 1, `DISP_BGR` = 0, `DISP_ROTATION` = 1, `DISP_REINIT_MS` = 300000,
  `BOOT_SPLASH` = 1
- Setpoint and margins:
  - `MARGIN_DP_C` = 4.0, `DELTA_ROOM_C` (δ) = 3.0 (TBD, bench check 1), `MODE2_FALLBACK_C` = 20.0
  - `HARD_LOW_OFFSET_C` = 1.0, `T2_TRIP_DP_C` = 1.0, `T1_TRIP_DP_C` = 2.0, `DEADBAND_C` = 0.5
  - `WAKE_OFFSET_C` = 1.0, `SLEEP_PARK_MIN` = 10
  - `FEAS_OPEN_C` = 5.0, `FEAS_FORCE_C` = 1.0, `FEAS_FORCE_MS` = 120000, `T_OUT_SETTLE_MIN` = 10 (ASSUMED)
  - `DP_SPREAD_ALARM_C` = 2.0, `K_POT` = 0 (W/K, TBD)
- V2: `V2_T_OPEN_MS` / `V2_T_CLOSE_MS` = 30000 placeholder (TBD), `V2_BACKLASH_MS` = 0 (TBD), `V2_HOME_OVERDRIVE` = 1.3,
  `V2_MIN_PULSE_MS` = 150, `V2_REVERSE_DEADTIME_MS` = 500, `V2_CTRL_PERIOD_MS` = 10000,
  gains/max pulses, `V2_START_LIMIT_MIN` = 5, `V2_START_RATE_DEG_MIN` = 5
- V1: `V1_OFF_DELAY_MS` = 10000
- P2: `P2_PWM_RUN` = 80, `P2_SPINUP_MS` = 3000, `PUMP_STALL_MS` = 5000
- Fans: `FAN_PWM_MIN` = 20 (TBD), `FAN_STEP_PCT` = 5, `FAN_STAGE_DEG` = 81, `FAN_STALL_MS` = 5000
- Fuse sense: `FUSE_PRESENT_ADC` = 500, `FUSE_ABSENT_ADC` = 250
- NTCs: `NTC_R25` = 10000, `NTC_BETA` = 3435 (ASSUMED), `NTC_OFFSET_T1/T2/T3` = 0.0, valid −40…+80 °C
- `LONG_PRESS_MS` = 2000, `WDT_ENABLE` = 1
- Bench build: `NTC_TRIMPOT_SIM` = 0, `SIM_T_MIN_C` = −30, `SIM_T_MAX_C` = 50, `SIM_SHORT_OHM` = 50,
  `SIM_FORCE_12V` = 0, `SIM_FAKE_P2_TACH` = 0
- `FREEZE_GUARD_C` = −20 (PROPOSAL, unused)

## Open items
- OPEN: freeze guard behaviour (PROPOSAL, not implemented)
- TBD (bench): NTC Beta, V2 direction + travel + backlash, δ, fan minimum / 0 % behaviour, P2 PWM direction, K_POT
- Confirm ASSUMED items: T_out definition, wake needs feasibility, Mode 3 degraded behaviour, fault order kept
  for faults, T1 backstop alarm-only with P2 off, fault latching, LED overlays (12V absent / fault / alarm)
- Docs: BOM rev 4b sheet has no line for the SHT30 (already owned); RAM block is Bykski in the BOM and IceMan on the schematic.
