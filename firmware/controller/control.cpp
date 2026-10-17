#include "control.h"
#include "config.h"
#include "hw.h"
#include "sensors.h"
#include "valve.h"
#include "since.h"
#include <EEPROM.h>
#include <math.h>

Status st;

static const uint8_t EE_MAGIC_ADDR = 0, EE_MODE_ADDR = 1, EE_MAGIC = 0xA5;

static uint32_t bootAt, stateAt, v12At, p2OnAt, runStartAt, lastCtrlAt;
static Since parked, forced, p2Zero, fanZero[2];
static uint32_t noFlowAt, seqAt;
static bool noFlow, bootSettle = true, v12Was;
static uint8_t seqStep, fanPct = FAN_PWM_MIN;

// ---------------- helpers ----------------
static void enter(BranchState s, uint32_t now) {
  st.state = s;
  stateAt = now;
  seqStep = 0;
}

static void p2AndV1Off() {
  hwSetSen(false);
  hwSetPen(false);
  hwSetPumpPct(0);
}

static void setFans(uint8_t pct) {
  fanPct = pct;
  hwSetFanPct(pct);
}

static bool flowing() {
  // Without a home the angle is unknown: count it as flowing whenever P2 and V1 are on.
  return hwPen() && hwSen() && (!valveHomed() || valveDeg() > V2_FLOW_DEG);
}

static bool feasibleOpen() {
  return st.tOutOk && ntcT1.valid && st.tOut < ntcT1.c - FEAS_OPEN_C;
}

static void parkIfNeeded() {
  if (!valveParked()) valvePark();
}

// ---------------- derived values ----------------
static void updateDerived(uint32_t now) {
  float spread;
  st.dp = dewPointMax(st.dpValid, spread);

  uint16_t a = st.alarms & (A_FUSE_FAN | A_FAN1_STALL | A_FAN2_STALL | A_FEAS_FORCED);
  bool grace = now - bootAt < BOOT_GRACE_MS;
  if (!grace) {
    if (!sht[SHT_A].valid) a |= A_SHT_A;
    if (!sht[SHT_B].valid) a |= A_SHT_B;
    if (!sht[SHT_R].valid) a |= A_SHT_R;
  }
  if (spread > DP_SPREAD_ALARM_C) a |= A_DP_SPREAD;

  // Setpoint
  st.tset = NAN;
  if (st.mode != MODE_OFF && st.dpValid > 0) {
    float dpTerm = st.dp + MARGIN_DP_C;
    bool m2 = st.mode == MODE_LOADCAP || st.dpValid < 2;
    if (st.mode == MODE_MAX && st.dpValid < 2) a |= A_M3_DEGRADED;
    if (m2) {
      float room = sht[SHT_R].valid ? sht[SHT_R].t + DELTA_ROOM_C : MODE2_FALLBACK_C;
      st.tset = room > dpTerm ? room : dpTerm;
    } else {
      st.tset = dpTerm;
    }
  }

  // T_out: tracks T3 once the rad has been stagnant long enough (ASSUMED definition).
  if (flowing()) {
    noFlow = false;
    bootSettle = false;
  } else {
    if (!noFlow) {
      noFlow = true;
      noFlowAt = now;
    }
    if (ntcT3.valid && (bootSettle || now - noFlowAt >= T_OUT_SETTLE_MIN * 60000UL)) {
      st.tOut = ntcT3.c;
      st.tOutOk = true;
      a &= ~A_FEAS_FORCED;
    }
  }

  if (!hwPen() && st.dpValid && ntcT1.valid && ntcT1.c < st.dp + T1_TRIP_DP_C) a |= A_T1_LOW_IDLE;
  st.alarms = a;
}

// ---------------- faults ----------------
static void updateFaults(uint32_t now) {
  if (now - bootAt < BOOT_GRACE_MS) return;
  uint16_t live = 0;
  if (!ntcT1.valid) live |= F_NTC_T1;
  if (!ntcT2.valid) live |= F_NTC_T2;
  if (!ntcT3.valid) live |= F_NTC_T3;
  if (st.dpValid == 0) live |= F_DP_NONE;

  bool pen = hwPen();
  if (pen && st.dpValid) {
    if (ntcT2.valid && ntcT2.c < st.dp + T2_TRIP_DP_C) live |= F_T2_LOW;
    if (ntcT1.valid && ntcT1.c < st.dp + T1_TRIP_DP_C) live |= F_T1_LOW;
  }

  // Fuses: only once 12V has settled (the three lines don't rise in the same millisecond).
  bool fusesSettled = now - v12At >= 2 * FUSE_DEBOUNCE_MS;
  uint16_t a = st.alarms & ~(A_FUSE_FAN | A_FAN1_STALL | A_FAN2_STALL);
  if (fusesSettled) {
    if (!fusePresent[0]) live |= F_FUSE_P2;
    if (!fusePresent[2]) live |= F_FUSE_V;
    if (!fusePresent[1]) a |= A_FUSE_FAN;
  }

  // P2 stall
  if (pen && now - p2OnAt >= P2_SPINUP_MS && rpmPump == 0) {
    p2Zero.hold(now);
    if (p2Zero.elapsed(now, PUMP_STALL_MS)) live |= F_P2_STALL;
  } else {
    p2Zero.clear();
  }

  // Fan stall (alarm only)
  const uint16_t rpm[2] = {rpmFan1, rpmFan2};
  const uint16_t bit[2] = {A_FAN1_STALL, A_FAN2_STALL};
  for (uint8_t i = 0; i < 2; i++) {
    if (fusesSettled && fusePresent[1] && hwFanPct() > 0 && rpm[i] == 0) {
      fanZero[i].hold(now);
      if (fanZero[i].elapsed(now, FAN_STALL_MS)) a |= bit[i];
    } else {
      fanZero[i].clear();
    }
  }

  st.alarms = a;
  st.faultsLive = live;
  st.faults |= live;
}

// ---------------- regulation (RUN) ----------------
static void openPulse(float err, uint32_t now) {
  float ms = err * V2_KP_OPEN_MS_PER_C;
  if (ms > V2_MAX_OPEN_PULSE_MS) ms = V2_MAX_OPEN_PULSE_MS;
  if (ms < V2_MIN_PULSE_MS) ms = V2_MIN_PULSE_MS;
  uint32_t since = now - runStartAt;
  if (since < V2_START_LIMIT_MIN * 60000UL) {
    float room = since / 60000.0f * V2_START_RATE_DEG_MIN - valveDeg();
    if (room <= 0) return;
    float roomMs = room / 90.0f * V2_T_OPEN_MS;
    if (roomMs < ms) ms = roomMs;
    if (ms < V2_MIN_PULSE_MS) return;
  }
  valveStep(V_OPEN, (uint32_t)ms);
}

static void closePulse(float err) {
  float ms = err * V2_KP_CLOSE_MS_PER_C;
  if (ms > V2_MAX_CLOSE_PULSE_MS) ms = V2_MAX_CLOSE_PULSE_MS;
  valveStep(V_CLOSE, (uint32_t)ms);
}

static void regulate(uint32_t now) {
  if (isnan(st.tset) || !ntcT1.valid || !ntcT2.valid || !ntcT3.valid) return;  // faults handle it
  float t1 = ntcT1.c, t2 = ntcT2.c, t3 = ntcT3.c;
  float err = t2 - st.tset;

  // Hard low limit: park now, fans to minimum.
  if (t2 < st.tset - HARD_LOW_OFFSET_C) {
    setFans(FAN_PWM_MIN);
    parkIfNeeded();
    return;
  }

  // Feasibility: rad warmer than the loop return while flowing -> force bypass.
  if (flowing() && t3 > t1 - FEAS_FORCE_C) {
    forced.hold(now);
    if (forced.elapsed(now, FEAS_FORCE_MS)) {
      forced.clear();
      st.tOutOk = false;
      st.alarms |= A_FEAS_FORCED;
      setFans(FAN_PWM_MIN);
      valvePark();
      return;
    }
  } else {
    forced.clear();
  }

  if (now - lastCtrlAt < V2_CTRL_PERIOD_MS) return;
  lastCtrlAt = now;
  if (valveBusy()) return;

  uint8_t f = fanPct;
  if (err > DEADBAND_C) {
    if (feasibleOpen()) {
      if (valveDeg() < 90.0f) openPulse(err, now);
      if (valveDeg() >= FAN_STAGE_DEG && f + FAN_STEP_PCT <= 100) f += FAN_STEP_PCT;
    }
  } else if (err < -DEADBAND_C) {
    if (f > FAN_PWM_MIN) f = f - FAN_STEP_PCT < FAN_PWM_MIN ? FAN_PWM_MIN : f - FAN_STEP_PCT;  // fans down first
    else closePulse(-err);
  }
  if (t3 < st.dp) f = FAN_PWM_MIN;   // limit condensation on the indoor blue line
  setFans(f);
}

// ---------------- public ----------------
void controlInit(uint32_t now) {
  bootAt = now;
  st.mode = MODE_OFF;
  if (EEPROM.read(EE_MAGIC_ADDR) == EE_MAGIC) {
    uint8_t m = EEPROM.read(EE_MODE_ADDR);
    if (m >= MODE_OFF && m <= MODE_MAX) st.mode = (Mode)m;
  }
  st.state = ST_NO12V;
  st.dp = st.tset = st.tOut = NAN;
  stateAt = now;
}

void controlButton(Mode m) {
  st.faults &= st.faultsLive;   // acknowledge faults whose cause is gone
  if (m != st.mode) {
    st.mode = m;
    EEPROM.update(EE_MAGIC_ADDR, EE_MAGIC);
    EEPROM.update(EE_MODE_ADDR, m);
  }
}

void controlUpdate(uint32_t now) {
  bool v12 = hw12vPresent();
  if (v12 && !v12Was) v12At = now;
  v12Was = v12;
  valveSetPowerOk(v12 && fusePresent[2]);

  updateDerived(now);

  if (!v12) {
    st.alarms &= ~(A_FUSE_FAN | A_FAN1_STALL | A_FAN2_STALL);
    if (st.state != ST_NO12V) {
      p2AndV1Off();
      valveStop();
      setFans(0);
      enter(ST_NO12V, now);
    }
    return;
  }
  if (st.state == ST_NO12V) enter(ST_HOMING, now);

  updateFaults(now);

  if (st.faults && st.state != ST_SAFE) {
    p2AndV1Off();             // V1 and P2 off at once, V2 parks in parallel
    setFans(FAN_PWM_MIN);
    parkIfNeeded();
    enter(ST_SAFE, now);
  }

  switch (st.state) {
    case ST_HOMING:
      p2AndV1Off();
      setFans(FAN_PWM_MIN);
      if (valveParked()) enter(ST_IDLE, now);
      else if (!valveBusy()) valvePark();   // re-requested if S3 wasn't settled yet
      break;

    case ST_IDLE:
      p2AndV1Off();
      setFans(FAN_PWM_MIN);
      if (!valveHomed() && !valveBusy()) valvePark();
      if (st.mode != MODE_OFF && !isnan(st.tset) && ntcT1.valid &&
          ntcT1.c > st.tset + WAKE_OFFSET_C && feasibleOpen()) {
        enter(ST_STARTING, now);
      }
      break;

    case ST_STARTING:
      setFans(FAN_PWM_MIN);
      if (st.mode == MODE_OFF) {
        enter(ST_STOPPING, now);
      } else if (seqStep == 0) {
        if (valveParked()) {
          hwSetPen(true);
          hwSetPumpPct(P2_PWM_RUN);
          p2OnAt = now;
          seqStep = 1;
        } else if (!valveBusy()) {
          valvePark();
        }
      } else {
        hwSetSen(true);
        runStartAt = lastCtrlAt = now;
        parked.clear();
        forced.clear();
        enter(ST_RUN, now);
      }
      break;

    case ST_RUN:
      if (st.mode == MODE_OFF) {
        enter(ST_STOPPING, now);
        break;
      }
      regulate(now);
      if (valveParked()) {
        parked.hold(now);
        if (parked.elapsed(now, SLEEP_PARK_MIN * 60000UL)) enter(ST_STOPPING, now);
      } else {
        parked.clear();
      }
      break;

    case ST_STOPPING:
      setFans(FAN_PWM_MIN);
      if (seqStep == 0) {
        if (valveParked()) {
          hwSetPen(false);
          hwSetPumpPct(0);
          seqAt = now;
          seqStep = 1;
        } else if (!valveBusy() || valveMoving() == V_OPEN) {
          valvePark();
        }
      } else if (now - seqAt >= V1_OFF_DELAY_MS) {
        hwSetSen(false);
        enter(ST_IDLE, now);
      }
      break;

    case ST_SAFE:
      p2AndV1Off();
      setFans(FAN_PWM_MIN);
      if (!valveHomed() && !valveBusy()) valvePark();
      if (!st.faults) enter(ST_IDLE, now);
      break;

    case ST_NO12V:
      break;
  }
}

const char *stateName(BranchState s) {
  switch (s) {
    case ST_NO12V:    return "NO12V";
    case ST_HOMING:   return "HOME";
    case ST_IDLE:     return "IDLE";
    case ST_STARTING: return "START";
    case ST_RUN:      return "RUN";
    case ST_STOPPING: return "STOP";
    case ST_SAFE:     return "SAFE";
  }
  return "?";
}
