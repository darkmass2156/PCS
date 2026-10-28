#pragma once
// Pin map and tunables. Pin map mirrors CLAUDE.md (authoritative).

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
#define PIN_LED_G     10
#define PIN_LED_B     11
#define PIN_PUMP_TACH 12   // PCINT6
#define PIN_TACH1     18   // INT3
#define PIN_TACH2     19   // INT2
#define PIN_VO        22
#define PIN_VC        24
#define PIN_PEN       26
#define PIN_SEN       28
#define PIN_BTN1      30
#define PIN_BTN2      32
#define PIN_SDA2      34   // SHT30 soft I2C
#define PIN_SCL2      36
#define PIN_DISP_CS   49
#define PIN_DISP_DC   53

// ---------------- Display ----------------
#define DISPLAY_VARIANT 1          // 1 = MSP1691 (280x240 landscape); 2 = MSP1901 has no layouts since display rev 3
#define DISP_SPI_HZ     4000000UL
// Display target. The firmware drives an ST7789. The Wokwi simulator only has an ILI9341, which reads
// MADCTL differently (mirrored), wants BGR and no inversion. 1 = simulator, 0 = the real ST7789 panel.
// The banner prints which one a build is for. Set back to 0 before flashing the real controller.
#define DISP_TARGET_ILI9341_SIM 1
#if DISP_TARGET_ILI9341_SIM
#define DISP_INVERT     0
#define DISP_BGR        1
#define DISP_MIRROR_X   1
#else
#define DISP_INVERT     1          // ST7789 IPS: flip if colours look negative
#define DISP_BGR        0          // set if red and blue are swapped
#define DISP_MIRROR_X   0          // set if text is mirrored
#endif
#define DISP_ROTATION   1          // landscape 280x240; 3 = landscape upside down
#define BOOT_SPLASH     1          // penguin "noot noot" splash at boot (non-blocking, ~3 s)
#define DISP_REINIT_MS  300000UL

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
#define V2_T_OPEN_MS           30000UL  // TBD: 07_valve
#define V2_T_CLOSE_MS          30000UL  // TBD: 07_valve
#define V2_BACKLASH_MS         0UL      // TBD: 07_valve
#define V2_HOME_OVERDRIVE      1.3f
#define V2_MIN_PULSE_MS        150UL
#define V2_REVERSE_DEADTIME_MS 500UL
#define V2_CTRL_PERIOD_MS      10000UL
#define V2_KP_OPEN_MS_PER_C    300.0f   // open slow
#define V2_KP_CLOSE_MS_PER_C   600.0f   // close fast
#define V2_MAX_OPEN_PULSE_MS   1000UL
#define V2_MAX_CLOSE_PULSE_MS  3000UL
#define V2_FLOW_DEG            1.0f     // above this V2 passes outdoor flow
#define V2_START_LIMIT_MIN     5
#define V2_START_RATE_DEG_MIN  5.0f

// ---------------- V1 / P2 / fans ----------------
#define V1_OFF_DELAY_MS  10000UL
#define P2_PWM_RUN       80
#define P2_SPINUP_MS     3000UL
#define PUMP_STALL_MS    5000UL
#define FAN_PWM_MIN      20       // TBD: 05_fan_pwm_tach
#define FAN_STEP_PCT     5
#define FAN_STAGE_DEG    81.0f    // 90 % of travel
#define FAN_STALL_MS     5000UL

// ---------------- Fuse sense / 12V ----------------
#define FUSE_PRESENT_ADC 500
#define FUSE_ABSENT_ADC  250
#define FUSE_DEBOUNCE_MS 1000UL
#define V12_STABLE_MS    1000UL

// ---------------- NTC ----------------
#define NTC_R_PULLUP   10000.0f
#define NTC_R25        10000.0f
#define NTC_BETA       3435.0f   // ASSUMED until 01_ntc
#define NTC_OFFSET_T1  0.0f
#define NTC_OFFSET_T2  0.0f
#define NTC_OFFSET_T3  0.0f
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
// Only honoured when NTC_TRIMPOT_SIM is 1 — for a breadboard without Board 1 / pump:
#define SIM_FORCE_12V     0        // treat S1..S3 as present
#define SIM_FAKE_P2_TACH  0        // report 3000 rpm whenever PEN is on (no stall fault)

// ---------------- SHT3x ----------------
#define SHT_PERIOD_MS   2000UL
#define SHT_FAIL_LIMIT  3
#define SHT_GOOD_LIMIT  3
// RH calibration offsets, added to the reading (% RH), corrected RH clamped to 0..100.
// A/B from a side-by-side run (2026-10-28): A read low, B high.
#define SHT_RH_OFFSET_A  1.15f   // SHT31 A 0x44
#define SHT_RH_OFFSET_B -1.15f   // SHT31 B 0x45
#define SHT_RH_OFFSET_R  0.0f    // SHT30 room

// ---------------- Misc ----------------
#define LONG_PRESS_MS   2000UL
#define BOOT_GRACE_MS   10000UL  // faults that need sensor data aren't evaluated before this
#define WDT_ENABLE      1
#define FREEZE_GUARD_C  -20.0f   // PROPOSAL, unused
