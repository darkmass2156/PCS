#pragma once
// Pin map and tunables. Pin map mirrors CLAUDE.md (authoritative), hardware rev 6b.

// ---------------- Pins ----------------
#define PIN_S1        A0   // P2 fuse sense
#define PIN_S2        A1   // fan fuse sense
#define PIN_S3        A2   // V1 + V2 fuse sense
#define PIN_T3        A3   // outdoor XE 240
#define PIN_T2        A4   // V2 outlet (control)
#define PIN_T1        A5   // loop return
#define PIN_FAN_PWM   2    // OC3B
#define PIN_PUMP_PWM  3    // OC3C
#define PIN_LED_R     9
#define PIN_LED_G     11   // as built: green die on D11, blue on D10 (bench 2026-11-03, Mode 2 breathed green)
#define PIN_LED_B     10
#define PIN_PUMP_TACH 12   // PCINT6
#define PIN_TACH1     18   // INT3
#define PIN_TACH2     19   // INT2
#define PIN_VO        22
#define PIN_VC        24
#define PIN_PEN       26
#define PIN_SEN       28
#define PIN_BTN1      30
#define PIN_BTN2      32
#define PIN_SDA2      38   // SHT30 soft I2C, wired straight to the Mega header (was D34 via LG, never answered)
#define PIN_SCL2      40
#define PIN_DISP_RST  47   // rev 6b: display RES through C5 pin 3 (680 Ω to GND on LG: -1 would hold it in reset)
#define PIN_DISP_CS   49
#define PIN_DISP_DC   53

// ---------------- Display ----------------
#define DISPLAY_VARIANT 2          // 2 = 1.9" 170x320 (320x170 landscape, rev 6b, screens170.*); 1 = 1.69" 280x240 (screens.*)
#define DISP_SPI_HZ     4000000UL
// Display target. The firmware drives an ST7789. The Wokwi simulator only has an ILI9341, which reads
// MADCTL differently (mirrored), wants BGR and no inversion. 1 = simulator, 0 = the real ST7789 panel.
// The banner prints which one a build is for. Set back to 0 before flashing the real controller.
#define DISP_TARGET_ILI9341_SIM 0
#if DISP_TARGET_ILI9341_SIM
#define DISP_INVERT     0
#define DISP_BGR        1
#define DISP_MIRROR_X   1
#else
#define DISP_INVERT     1          // ST7789 IPS: flip if colours look negative
#define DISP_BGR        0          // set if red and blue are swapped
#define DISP_MIRROR_X   0          // set if text is mirrored
#endif
#define DISP_ROTATION   1          // landscape; 3 = landscape upside down
#define BOOT_SPLASH     1          // penguin "noot noot" splash at boot (non-blocking, ~3 s)
#define DISP_REINIT_MS  300000UL   // self-heal on the 1.5 m run: re-send the init sequence, then redraw everything

// ---------------- Setpoint and margins (°C) ----------------
#define MARGIN_DP_C        4.0f
#define DELTA_ROOM_C       3.0f    // δ, TBD: bench check 1
#define MODE2_FALLBACK_C   20.0f   // Mode 2 room term when the SHT30 is invalid
#define HARD_LOW_OFFSET_C  1.0f    // T2 < T_set - this -> park V2
#define T2_TRIP_DP_C       1.0f    // T2 < DP + this -> fault
#define T1_TRIP_DP_C       2.0f    // T1 < DP + this -> fault
#define DEADBAND_C         0.5f
#define WAKE_OFFSET_C      1.0f
#define SLEEP_PARK_MIN     10
#define FEAS_OPEN_C        5.0f    // V2 may open only if T_out < T1 - this
#define FEAS_FORCE_C       1.0f    // forced bypass if T3 > T1 - this ...
#define FEAS_FORCE_MS      120000UL // ... for this long while flowing
#define T_OUT_SETTLE_MIN   10      // ASSUMED: stagnation time before T_out tracks T3
#define DP_SPREAD_ALARM_C  2.0f
#define K_POT              25.0f   // W/°C, placeholder until the A/B calibration test

// ---------------- V2 ----------------
// Bench 2026-11-02 (07_valve, CR-02): VO opens the outdoor port, VC goes to bypass (confirmed).
// Stepped from bypass after the play: 1300 ms dead zone (outdoor still shut), +1800 ms = 50 % mix,
// +1950 ms = all ports open. One straight end-to-end run takes 6400 ms.
#define V2_T_OPEN_MS           5050UL   // bypass -> all ports open, motion after the play (stepped)
#define V2_T_CLOSE_MS          5050UL   // all ports open -> bypass after the play (bench: same as open)
#define V2_BACKLASH_MS         725UL    // play after a reversal before the ball moves
#define V2_DEAD_MS             1300UL   // motion from bypass before the outdoor port starts to open
#define V2_HALF_MS             3100UL   // motion from bypass to 50 % outdoor (both inlets half)
#define V2_PARK_MS             10000UL  // park: VC this long (end-to-end 6400 ms + margin, autostop ends it)
#define V2_MIN_PULSE_MS        150UL
#define V2_REVERSE_DEADTIME_MS 500UL
#define V2_CTRL_PERIOD_MS      10000UL
#define V2_KP_OPEN_MS_PER_C    100.0f   // open slow (500 ms ~ 1.35 mm of port)
#define V2_KP_CLOSE_MS_PER_C   200.0f   // close fast
#define V2_MAX_OPEN_PULSE_MS   500UL
#define V2_MAX_CLOSE_PULSE_MS  1500UL
#define V2_FLOW_DEG            (90.0f * V2_DEAD_MS / V2_T_OPEN_MS)   // the edge (23.2°): above it outdoor flows
#define V2_START_LIMIT_MIN     5
#define V2_START_RATE_DEG_MIN  5.0f     // counted from the edge

// ---------------- V1 / P2 / fans ----------------
#define V1_FITTED        0        // 0: no V1 (dropped 2026-11-03, coil 1.4 A / >50 °C); SEN stays LOW, V2 alone shuts the
                                  //    outdoor branch. 1: NC solenoid on SEN as designed.
#define V1_OFF_DELAY_MS  10000UL
#define P2_PWM_RUN       80
#define P2_SPINUP_MS     3000UL
#define PUMP_STALL_MS    5000UL
#define FAN_PWM_MIN      20       // TBD: 05_fan_pwm_tach
#define FAN_STEP_PCT     5
#define FAN_STAGE_DEG    81.0f    // 90 % of travel
#define TACH_LOCKOUT_US  3000UL   // tach edge filter: min time between counted edges (3000 rpm = 10 ms)
#define FAN_STALL_MS     5000UL

// ---------------- Fuse sense / 12V ----------------
#define FUSE_PRESENT_ADC 500
#define FUSE_ABSENT_ADC  250
#define FUSE_DEBOUNCE_MS 1000UL
#define V12_STABLE_MS    1000UL

// ---------------- Status LED ----------------
// 1 = decided 2026-10-17: mode colour breathing (Mode 1 white, 2 blue, 3 red), 3 green pulses on a mode change,
//     12V absent off, fault fast red, alarm yellow flash.
// 2 = PROPOSAL from the rev 6b change note (not decided): Mode 1 dim white / 2 green / 3 blue solid, sleep = slow
//     pulse in the mode colour, V2 moving = cyan flash, alarm = amber pulse, fault = fast red, boot / waiting for
//     valid sensors = purple. Selectable so both can be judged on the real LED.
#define LED_SCHEME       1
#define LED_BRIGHTNESS_PCT 80     // all LED colours scaled to this (Vlad 2026-11-03)
#define TEST_TIMEOUT_MS  1800000UL // serial test mode ends 30 min after the last command

// ---------------- NTC ----------------
#define NTC_R_PULLUP   10000.0f
#define NTC_R25        10000.0f
#define NTC_BETA       3435.0f   // ASSUMED until 01_ntc
#define NTC_OFFSET_T1  0.0f
#define NTC_OFFSET_T2  0.0f
#define NTC_OFFSET_T3  0.0f
#define NTC_ADC_SHORT  8        // ADC below this = probe shorted (~80 Ω), invalid at once
#define NTC_ADC_OPEN   1015     // ADC above this = probe open / unplugged, invalid at once
#define NTC_FLOAT_DELTA 40      // open-pin check: with the internal pull-up the pin reads > NTC_ADC_OPEN and rose more
                                //   than this = floating (a connected node moves ~20 counts, ~65 without its 100 nF)
#define NTC_MIN_C      -40.0f
#define NTC_MAX_C      80.0f
#define NTC_FAULT_MS   2000UL

// ---------------- Bench test build (trimpots instead of NTCs) ----------------
// NTC_TRIMPOT_SIM 1: each NTC input takes a 3296W 10k trimpot wired as a rheostat (wiper + one end)
// in place of the probe. R 0..10k maps linearly to SIM_T_MIN_C..SIM_T_MAX_C (25 turns ≈ 3.2 °C/turn).
// < SIM_SHORT_OHM reads as a short and > 10.5k (unplugged) as open: both raise the NTC fault.
// Banner, CSV header and display show "SIM". Never ship with this set.
#define NTC_TRIMPOT_SIM   0
#define SIM_T_MIN_C       -30.0f   // at 0 ohm
#define SIM_T_MAX_C       50.0f    // at 10k
#define SIM_SHORT_OHM     50.0f
// Bench without the 12V supply / pump (independent of NTC_TRIMPOT_SIM). Banner and display show SIM. Never ship.
// SIM_FORCE_12V: S1..S3 read as present, so the state machine runs (homing, start, RUN, stop) and every
// output can be watched on a meter / LED; nothing moves without 12V.
#define SIM_FORCE_12V     0        // treat S1..S3 as present
#define SIM_FAKE_P2_TACH  0        // report 3000 rpm whenever PEN is on (no P2 stall fault)
#define SIM_FAKE_FAN_TACH 0        // report fan rpm from the PWM % (no fan stall alarms)
#define SIM_ACTIVE (NTC_TRIMPOT_SIM || SIM_FORCE_12V || SIM_FAKE_P2_TACH || SIM_FAKE_FAN_TACH)

// ---------------- SHT3x ----------------
#define SHT_PERIOD_MS   2000UL
#define SHT_FAIL_LIMIT  3
#define SHT_GOOD_LIMIT  3
#define SHT_WIRE_TIMEOUT_US 25000UL   // rev 6b: Wire timeout; after one, the bus is recovered (9 SCL pulses)
// RH calibration offsets, added to the reading (% RH), corrected RH clamped to 0..100.
// A/B from a side-by-side run (2026-10-28): A read low, B high.
// Calibration 2026-11-03: all three side by side, raw readings pulled to the median of the three
// (A 21.45 °C 33.5 %, B 22.51 °C 34.8 %, R 22.64 °C 35.0 % -> median 22.51 °C 34.8 %).
#define SHT_RH_OFFSET_A  1.3f    // SHT31 A 0x44, % RH
#define SHT_RH_OFFSET_B  0.0f    // SHT31 B 0x45 (the median)
#define SHT_RH_OFFSET_R -0.2f    // SHT30 room
#define SHT_T_OFFSET_A   1.06f   // °C
#define SHT_T_OFFSET_B   0.0f
#define SHT_T_OFFSET_R  -0.13f

// ---------------- Misc ----------------
#define LONG_PRESS_MS   2000UL
#define BOOT_GRACE_MS   10000UL  // faults that need sensor data aren't evaluated before this
#define WDT_ENABLE      1
#define FREEZE_GUARD_C  -20.0f   // PROPOSAL, unused
