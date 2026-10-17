#pragma once
// Everything the dashboard shows, filled once per second by display.cpp (or with fake data by
// tests/11_ui_dev). The screens never read control/sensor globals directly.
#include <Arduino.h>

struct UiData {
  uint8_t mode;            // 1..3
  const char *state;       // "RUN", "SLEEP", ...
  bool no12v, sim;
  float rad, loop, valve;  // T3, T1, T2 (NAN = invalid)
  float caseT, caseRH, caseDP;   // highest of the valid case SHT31s, per value
  float roomT, roomRH, roomDP;   // SHT30
  float dp;                // max of all valid dew points (the cap uses this)
  float dpA, dpB, dpR;
  uint8_t dpValid;
  float tset, tout;
  bool toutOk;
  float potK, potW;        // T1 - T_out, K_POT * that
  bool potOk;
  bool extOpen;            // V1
  bool homing, mixing;     // V2: homing / outdoor port open
  float v2deg;
  bool v2homed;
  uint8_t fanPct;
  uint16_t rpmF1, rpmF2;
  bool p2On;
  uint8_t p2Pct;
  uint16_t rpmP2;
  float dew;               // valve - dp: margin above the dew point
  uint16_t faults, alarms;
  uint8_t fuses;
};
