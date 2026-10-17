#pragma once
#include <Arduino.h>

enum Mode : uint8_t { MODE_OFF = 1, MODE_LOADCAP = 2, MODE_MAX = 3 };

enum BranchState : uint8_t {
  ST_NO12V,      // PC off: everything LOW, V2 not homed
  ST_HOMING,     // 12V returned: park V2 before anything else
  ST_IDLE,       // Mode 1, or sleeping in Mode 2/3
  ST_STARTING,
  ST_RUN,
  ST_STOPPING,
  ST_SAFE,       // latched fault
};

// Latched faults -> safe state
enum : uint16_t {
  F_NTC_T1   = 1 << 0,
  F_NTC_T2   = 1 << 1,
  F_NTC_T3   = 1 << 2,
  F_DP_NONE  = 1 << 3,
  F_T2_LOW   = 1 << 4,   // T2 < DP + 1 with P2 running
  F_T1_LOW   = 1 << 5,   // T1 < DP + 2 with P2 running
  F_FUSE_P2  = 1 << 6,   // S1
  F_FUSE_V   = 1 << 7,   // S3
  F_P2_STALL = 1 << 8,
};

// Live alarms (no safe state)
enum : uint16_t {
  A_FUSE_FAN   = 1 << 0,  // S2
  A_FAN1_STALL = 1 << 1,
  A_FAN2_STALL = 1 << 2,
  A_SHT_A      = 1 << 3,
  A_SHT_B      = 1 << 4,
  A_SHT_R      = 1 << 5,
  A_DP_SPREAD  = 1 << 6,
  A_M3_DEGRADED= 1 << 7,  // Mode 3 with < 2 DP sources, running the Mode 2 setpoint
  A_T1_LOW_IDLE= 1 << 8,  // T1 < DP + 2 with P2 off
  A_FEAS_FORCED= 1 << 9,  // forced to bypass by feasibility, T_out re-settling
};

struct Status {
  Mode mode;
  BranchState state;
  uint16_t faults;      // latched
  uint16_t faultsLive;
  uint16_t alarms;
  float dp;             // NAN if no valid source
  uint8_t dpValid;
  float tset;           // NAN in Mode 1
  float tOut;
  bool tOutOk;
};

extern Status st;

void controlInit(uint32_t now);
void controlUpdate(uint32_t now);
void controlButton(Mode m);      // a press selecting mode m (also acknowledges faults)
const char *stateName(BranchState s);
