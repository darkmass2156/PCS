# 570X outdoor free-cooling controller — Arduino Mega 2560

Firmware for the outdoor free-cooling branch of a watercooled PC (Corsair 570X).
The Mega runs pump 2, an NC solenoid and a motorized 3-way mixing valve. Its main job:
coolant entering the PC must never drop below in-case dew point + margin.

## How we work
- Vlad uses the Claude Code desktop app and uploads with **Arduino IDE 2**.
  Don't run terminal commands, install toolchains or open COM ports unless he asks.
- The Mega is a CH340 clone. If the IDE doesn't see the board, the CH340 USB driver is the first suspect.
- Arduino IDE rule: each sketch sits in a folder with the same name as its `.ino`.
  Extra `.h/.cpp` files go flat in that same folder.
  - Bench tests: `tests/NN_name/NN_name.ino`
  - Firmware: `firmware/controller/controller.ino`
- Every bench test prints to Serial at 115200. Vlad pastes the output back.
- If a library is needed, name it exactly as it appears in Library Manager so Vlad can install it.
- Keep replies short. Vlad knows electronics; skip the basics. Temperatures in °C.
- `docs/` holds:
  - `full_loop_schematic_rev10.png` — hydraulics and sensor placement
  - `build_component_list_rev4b.xlsx` — the "Rev 4b BOM" sheet is the controller parts list
  This file's pin map is authoritative.
- Anything marked OPEN or PROPOSAL: ask Vlad before implementing.

## Hardware
- **Controller**: MEGA2560 R3 (CH340), powered over USB from the PC.
  The 12V for loads comes from the PC PSU on a separate harness, so USB can be live while
  12V is off (S3/S5).
- **Boards**: two 60x80 perfboards (rev 4b). Board 1 = power, Board 2 = logic, linked by 34-pin ribbons.
- **Pump 1**: EK Kinetic D5 in the case, controlled by the PC (Apex). Not on the Mega.
- **Pump 2**: Bykski D5 + res in the controller box. The Mega switches its 12V (PEN) and drives PWM and tach.
- **3-way valve**: CR-02 12V, T-port, DN15, 3-wire with autostop.
- **Solenoid**: 2W-025-08, 1/4", 12V, normally closed.
- **Fans**: 2× NF-F12 iPPC-3000 on the outdoor XE 240. Their 12V is unswitched; only PWM controls them.
- **NTCs**: 3× Bykski B-PD5-TMR 10k probes on the Mega:
  - outdoor rad
  - valve outlet
  - GPU block (added in rev10)
  The cube NTC goes to the Apex, not the Mega.
- **Humidity**: 2× SHT31 in the case.
- **Display**: one ST7789 IPS screen in the case (see Display). Both sizes were ordered and one gets used.
  The 0.91" I2C OLEDs are **not** used.
- **Top panel**: 570X buttons BTN1 and BTN2, plus an RGB status LED under the power button.
  Button 3 and the power button go to the Apex, not the Mega.
- **Switched outputs**: all are **Mega HIGH = on**. Hardware pull-downs keep everything off during reset.
  - VO / VC: BC547 → IRF4905 high-side. These drive the valve open / close.
  - PEN: BC547 → IRF4905 high-side, switching pump 2's 12V (PWM alone can't stop a D5).
  - SEN: IRLB8721 low-side, driving the solenoid (HIGH = energised = open).

## Pin map (rev 4b + display CS mod — authoritative)
| Pin | Signal | Notes |
|---|---|---|
| A0 | S1 | pump fuse sense, 47k/20k divider (12V ≈ 3.58 V) |
| A1 | S2 | fan fuse sense |
| A2 | S3 | solenoid + valve fuse sense |
| A3 | NTC_R | outdoor rad (XE 240) |
| A4 | NTC_V | 3-way valve outlet — control sensor |
| A5 | NTC_GPU | GPU block — independent backstop |
| D2 | FAN_PWM | OC3B, 25 kHz |
| D3 | PUMP_PWM | OC3C, 25 kHz |
| D9 / D10 / D11 | LED R / G / B | common-cathode RGB, analogWrite (Timer2 / Timer1) |
| D12 | PUMP_TACH | not an INT pin → PCINT6 (PCINT0 group) |
| D18 | TACH1 | INT3 |
| D19 | TACH2 | INT2 |
| D20 / D21 | SDA / SCL | 4.7k pull-ups to 5V |
| D22 | VO | valve open |
| D24 | VC | valve close |
| D26 | PEN | pump 2 enable |
| D28 | SEN | solenoid enable |
| D30 / D32 | BTN1 / BTN2 | 10k pull-up, 1k + 100 nF RC, active LOW |
| D49 | DISP_CS | ST7789 CS via 1k/2k divider, Mega ribbon pin 32 |
| D51 / D52 / D53 | MOSI / SCK / DC | ST7789 via 1k/2k dividers; D50 unused |
| 3.3V pin | display 3V3 | powers the ST7789, backlight included |

## Connectors (for bench wiring)
- **Board 1 screw terminals** (row AA):
  - X1: +12V in, spare, GND in, rad NTC
  - X2: rad NTC return, valve NTC, valve NTC return, spare
  - X3: valve open, valve close, valve COM, solenoid +
  - X4: solenoid −, pump −, pump +, spare GND
- **Board 1 4-pin headers**: FAN1, FAN2, and PUMP (pump 2; pin 2 = switched 12V).
- **GX12 cables** (box ↔ case):
  - GX12 at both ends: C1, C2 and C5.
  - GX12 at the 570X bracket only, hardwired into the box: C3 and C4.
  - C1 2P: 12V feed
  - C2 4P: SHT31 ×2 (5V, GND, SDA, SCL)
  - C3 6P: top panel (BTN1, BTN2, LED R, G, B, GND)
  - C4 6P: display (GND, 3V3, SCK, MOSI, DC, CS)
  - C5 3P: GPU NTC (NTC, GND, spare)
  - C6: USB to the Mega (no GX12)
- **Board 2 display CS mod** (on top of rev 4b):
  - R22 1k at L7–P7, R23 2k at Q7–U7.
  - Bridges P7–Q7 and U7–V7.
  - Wires: S2 (Mega ribbon pin 32) > L7, and P7 > A20.
  - The DISP header becomes A15–A20, with A20 = CS.
  - Mega end: D49 on a 1-pin Dupont next to the SPI 2×2 housing.

## Interfaces
- **NTCs** (three, all the same probe model): 10k 1% pull-up to 5V, NTC to GND, 100 nF.
  Readings are ratiometric (AREF = AVCC).
  - Resistance: R = 10000 · adc / (1023 − adc).
  - Use one Beta (TBD from datasheet) plus a per-sensor offset from ice-water calibration.
  - Failure modes: a short reads HOT (the dangerous direction); an open circuit reads cold.
- **Fuse sense**: compare against thresholds (see Tunables). This is a binary present/absent check.
- **Tach**: open-collector, 10k pull-up to 5V, 2 pulses/rev.
- **PWM**: Timer3, fast PWM mode 14, no prescaler, ICR3 = 639 → 25 kHz.
  - Use no libraries that take Timer3.
  - Leave Timer1 and Timer2 alone (they drive the LED).
- **SHT31 ×2**: A = 0x44 (top-rear, near CPU) and B = 0x45 (mid-case, near GPU/RAM). Check the CRC on every read.

## Display
- **Two variants, one compile-time switch** (`DISPLAY_VARIANT` in `config.h`). Code must work on both.
  - `1` = MSP1691, 1.69" 240x280
  - `2` = MSP1901, 1.9" 170x320
- **Library**: Adafruit_ST7789 + Adafruit_GFX. Use `init(240, 280)` or `init(170, 320)`; the library handles the panel offsets.
- **Field report (1.69", one buyer)**: Adafruit with `init(240, 280)` worked. If something looks wrong, these are the fallbacks:
  - red and blue swapped → set the MADCTL BGR bit
  - colors look negative → flip `invertDisplay()`
  - image shifted by 20 rows → apply the 20-row offset manually (normally `init(240, 280)` handles it)
  That buyer was very likely on a 3.3V board with short leads and RST wired. Our remaining risks are different: no reset pin, 5V resistor-divider level shifting, and a ~1.5 m cable.
- **Wiring**:
  - CS is on D49 through a 1k/2k divider, so pass CS = 49. The CS edge resyncs the panel on every transaction.
  - RES goes through a 10k + 100 nF power-on RC at the display (no reset pin), so pass RST = -1. The software reset is reliable now that CS is wired.
  - Use the default SPI mode.
- **SPI clock**: ≤ 2 MHz, because the run is ~1.5 m with resistor-divider level shifting.
- **D53** stays OUTPUT: it's DC, and SPI master mode needs it anyway.
- **D50** is MISO. The SPI hardware forces it to an input, so it can't be used as an output.
- **Backlight** is tied to VCC, so it's always on with no dimming.
- **Redraw**: update at 1 Hz and redraw only the values that changed. No full-screen clears (they flicker).
- **Self-heal**:
  - Show a heartbeat (e.g. a ticking seconds counter) so a frozen screen is obvious.
  - Re-init the display every few minutes.
  - The RGB LED stays the authoritative status.
- **PROPOSAL — layout**:
  - mode and state
  - T_V vs T_set
  - DP
  - T_R and T_GPU
  - valve %, fan %, pump 2 rpm
  - active fault in red
  - "12V absent" when the PC is off

## Hydraulics (matches rev10 schematic)
```
Pump 1 (PC-controlled, always on) → cube → GPU (NTC_GPU) → RAM → CPU → 120 rad → 360 rad → tee
  tee → check valve → pump 1 res "in 1"
  tee → QDC → pump 2 → tee
              ├→ solenoid (NC) → outdoor XE 240 (NTC_R) → 3-way LEFT
              └→ Barrow balancing valve                → 3-way RIGHT
           3-way TOP (NTC_V) → QDC → pump 1 res "in 2"
```
- When pump 2 is off, the check valve opens and flow returns via "in 1".
- The valve runs between two ends:
  - all-ports-open: maximum outdoor flow
  - bypass-only: outdoor port blocked. This is the home end and the safe end.
- The valve has no position feedback; position is estimated from run time.
  Assumed: VO moves toward all-ports-open and VC toward bypass-only. **Verify on the bench.**
- Coolant freeze point is −25 °C.
- The blue line and the valve body sit indoors at outdoor-return temperature, which is a condensation risk inside the box.

## Control
- DP = max(dew point A, dew point B), using Magnus with a = 17.62 and b = 243.12.
- T_set = DP + margin.

### Modes
- **Mode 1** (BTN1 short press) — automatic.
  - Enable when pump 2 has been off for ≥ N min (rad water ≈ ambient) and T_R < T_GPU − ΔT_on.
  - Margin = x (default 4 °C).
  - Disable after the valve has been parked at bypass for ≥ N min, or when T_R > T_GPU − ΔT_off.
- **Mode 2** (BTN2 short press) — pre-cool.
  - Pump 2 runs continuously.
  - Idle margin = 10 °C (≈ 50 % surface RH); load margin = x.
  - **OPEN: what switches idle → load** (GPU NTC rise, button, or PC data over USB serial).
    Don't implement until Vlad decides.
- **Long press** (≥ 2 s) — override: force the branch to safe state until the next short press.
  No override may ever defeat the cap or the fault trips.
- Store the mode in EEPROM, writing only when it changes.

### Valve (floating control)
- Non-blocking step pulses with a minimum pulse length, a ±0.5 °C deadband and a dead time before reversing.
- Close fast, open slow.
- Drive VO and VC through ONE function that guarantees they're never HIGH at the same time.
- Clamp the position estimate to 0–100 %. Every move to bypass overdrives to travel × 1.3, which doubles as a re-home.
- Rate-limit opening for the first minutes after a start, because the stagnant rad sends a cold slug.

### Fans
- Fans are staged after the valve.
  - Hold them at minimum until the valve is ≥ ~90 % open and T_V is still above T_set.
  - Ramp them down before closing the valve.
- Hold them at minimum while T_R < DP, to limit condensation on the indoor blue line.
- Check on the bench whether the iPPCs stop at 0 % PWM. If they don't, "off" means minimum speed.

### Pump 2
- Run PWM is TBD from the bench.
- Assumed: higher duty = faster (verify).

## Safety (non-negotiable)
1. The first lines of `setup()` set all outputs LOW, before Serial or anything else.
2. Safe state, in order:
   1. SEN LOW, PEN LOW, PUMP_PWM 0.
   2. Drive VC to bypass (if 12V is present).
   3. Fans to minimum.
3. Hard low limit:
   - T_V < T_set − 1 °C → drive VC immediately.
   - T_V < DP + 1 °C → safe state.
4. Backstop: T_GPU < T_set − 1 °C → safe state.
5. Sensor validity:
   - Any NTC outside −40…+80 °C → fault → safe state.
   - SHT31 CRC fail or NACK 3× in a row → that sensor is invalid. If one sensor is still valid, use it and raise an alarm; if neither is, safe state.
6. 12V presence:
   - All of S1–S3 low → 12V absent (PC off). Hold outputs LOW, no alarm. When 12V returns, home the valve before doing anything else.
   - One line low → that fuse:
     - S1 → safe state + alarm.
     - S2 → alarm, keep running.
     - S3 → safe state + alarm (the solenoid is already closed by hardware).
7. Stalls:
   - Pump 2: tach = 0 for > 5 s after the spin-up grace period with PEN on → safe state + alarm.
   - Fans: stall → alarm only.
8. PROPOSAL — freeze guard: pump 2 off and T_R < −20 °C → periodic low-speed circulation, fans at minimum, valve under the normal cap.
9. Watchdog on. CH340 clones can ship with a bootloader that reset-loops with WDT enabled, so test `00_watchdog` first.
10. No `delay()` in the main loop; run a `millis()` state machine.
11. The display is never in the safety path. A display hang or error must not block control.

### Sequences
- **Start**:
  1. Home the valve.
  2. PEN on and pump PWM set.
  3. SEN on.
  4. Rate-limited valve opening, fans at minimum.
- **Stop**:
  1. VC to bypass (with overdrive).
  2. SEN off.
  3. PEN off.
- **Fault**: SEN off and PEN off immediately, with VC driving to bypass in parallel.

## Status and logging
- Status LED: propose a scheme (mode / fault / 12V absent) before implementing.
- Serial CSV at 1 Hz, with these columns:
  - millis, mode, state
  - T_R, T_V, T_GPU, DP_A, DP_B, T_set
  - valve %, fan %, pump %
  - rpm ×3 (fan 1, fan 2, pump)
  - fuse flags

## Bench test order
Each test is its own sketch in `tests/`. Pump 2 goes last. The harness only gets built once every test passes.
1. `00_watchdog` — clean boot and a WDT reset check
2. `01_ntc` — all three probes, plus ice-water calibration
3. `02_sht31_dewpoint`
4. `03_fuse_sense` — 12V present, 12V absent, and a pulled fuse
5. `04_buttons_led` — short and long press
6. `05_fan_pwm_tach`
7. `06_display` — run it on both variants via `DISPLAY_VARIANT`
8. `07_valve` — direction, travel time, homing
9. `08_solenoid`
10. `09_pump2`

## Tunables (`config.h`)
- `DISPLAY_VARIANT` = 1 (MSP1691) or 2 (MSP1901)
- Setpoint and margins:
  - `MARGIN_M1_C` = 4.0
  - `MARGIN_M2_IDLE_C` = 10.0
  - `HARD_LOW_OFFSET_C` = 1.0
  - `DEADBAND_C` = 0.5
- Valve:
  - `VALVE_TRAVEL_MS` = TBD (measure)
  - `VALVE_HOME_OVERDRIVE` = 1.3
  - `VALVE_MIN_PULSE_MS` = 150
  - `VALVE_REVERSE_DEADTIME_MS` = 500
- Mode 1 thresholds:
  - `DT_ON_C` = TBD
  - `DT_OFF_C` = TBD
  - `PARK_TIMEOUT_MIN` = TBD
- Fuse sense:
  - `FUSE_PRESENT_ADC` = 500
  - `FUSE_ABSENT_ADC` = 250
- NTCs:
  - `NTC_R25` = 10000
  - `NTC_BETA` = TBD
  - `NTC_OFFSET_R`, `NTC_OFFSET_V`, `NTC_OFFSET_GPU` = 0.0 (from calibration)
  - valid range −40…+80 °C
- `FREEZE_GUARD_C` = −20
- PWM:
  - `PUMP2_PWM_RUN` = TBD
  - `FAN_PWM_MIN` = TBD
- `LONG_PRESS_MS` = 2000
- `PUMP_STALL_MS` = 5000

## Known doc issues
- The component list scopes the Bykski D5 as in-case and the used EK D5 as the external extension.
  The rev10 schematic says the opposite (pump 2 = Bykski in the box).
  - The schematic is newer.
  - Confirm pump 2 has PWM + tach before `09_pump2`.

## Open items
- Mode 2 idle → load trigger
- NTC Beta
- Valve direction and travel time
- Pump 2 run PWM
- Fan minimum PWM / 0 % behaviour
- Freeze guard behaviour
- Final display size
- Display layout and LED scheme
