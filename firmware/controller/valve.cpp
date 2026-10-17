#include "valve.h"
#include "config.h"

static float deg;
static bool homed;
static bool powerOk;
static ValveDir moving = V_NONE;   // what the outputs are doing now
static ValveDir lastDir = V_NONE;  // last direction that actually ran (for backlash)
static bool parking;
static uint32_t moveEnd, lastIntegrate, stoppedAt;
static uint32_t backlashLeft;

static ValveDir pendDir = V_NONE;
static uint32_t pendMs;
static bool pendPark;

// The only writer of VO/VC: both LOW first, then at most one HIGH.
static void drive(ValveDir d) {
  digitalWrite(PIN_VO, LOW);
  digitalWrite(PIN_VC, LOW);
  if (d == V_OPEN) digitalWrite(PIN_VO, HIGH);
  else if (d == V_CLOSE) digitalWrite(PIN_VC, HIGH);
}

static void integrate(uint32_t now) {
  if (moving == V_NONE) return;
  uint32_t dt = now - lastIntegrate;
  lastIntegrate = now;
  if (backlashLeft) {
    uint32_t b = dt < backlashLeft ? dt : backlashLeft;
    backlashLeft -= b;
    dt -= b;
  }
  float full = moving == V_OPEN ? (float)V2_T_OPEN_MS : (float)V2_T_CLOSE_MS;
  deg += (moving == V_OPEN ? 90.0f : -90.0f) * dt / full;
  if (deg < 0) deg = 0;
  if (deg > 90) deg = 90;
}

// completed = the move ran its full time (only then does a park re-zero).
static void halt(uint32_t now, bool completed) {
  integrate(now);
  drive(V_NONE);
  if (moving != V_NONE) {
    lastDir = moving;
    stoppedAt = now;
  }
  moving = V_NONE;
  if (parking && completed) {
    deg = 0;
    homed = true;
  }
  parking = false;
}

static void begin(ValveDir d, uint32_t ms, bool park, uint32_t now) {
  backlashLeft = (lastDir != V_NONE && d != lastDir) ? V2_BACKLASH_MS : 0;
  moving = d;
  parking = park;
  lastIntegrate = now;
  moveEnd = now + ms;
  drive(d);
}

void valveInit() {
  drive(V_NONE);
  deg = 0;
  homed = false;
  stoppedAt = millis();
}

void valveSetPowerOk(bool ok) {
  if (powerOk && !ok) {
    halt(millis(), false);
    homed = false;          // position unknown until the next park
    pendDir = V_NONE;
    pendPark = false;
  }
  powerOk = ok;
}

void valveUpdate(uint32_t now) {
  if (moving != V_NONE) {
    if ((int32_t)(now - moveEnd) >= 0) halt(now, true);
    else integrate(now);
    return;
  }
  if (pendDir == V_NONE || !powerOk) return;
  // Dead time before reversing.
  if (lastDir != V_NONE && pendDir != lastDir && now - stoppedAt < V2_REVERSE_DEADTIME_MS) return;
  ValveDir d = pendDir;
  pendDir = V_NONE;
  begin(d, pendMs, pendPark, now);
}

static void request(ValveDir d, uint32_t ms, bool park) {
  pendDir = d;
  pendMs = ms;
  pendPark = park;
  valveUpdate(millis());
}

void valvePark() {
  if (!powerOk) return;
  if (parking || (pendPark && pendDir == V_CLOSE)) return;
  if (moving != V_NONE) halt(millis(), false);
  homed = false;
  request(V_CLOSE, (uint32_t)(V2_T_CLOSE_MS * V2_HOME_OVERDRIVE) + V2_BACKLASH_MS, true);
}

bool valveStep(ValveDir dir, uint32_t ms) {
  if (!powerOk || !homed || valveBusy() || dir == V_NONE) return false;
  if (ms < V2_MIN_PULSE_MS) ms = V2_MIN_PULSE_MS;
  if (dir == V_CLOSE && deg - 90.0f * ms / V2_T_CLOSE_MS <= 0.0f) {
    valvePark();             // every move to bypass overdrives and re-zeroes
    return true;
  }
  if (dir == V_OPEN && deg >= 90.0f) return false;
  request(dir, ms, false);
  return true;
}

void valveStop() {
  bool wasParking = parking || pendPark;
  halt(millis(), false);
  if (wasParking) homed = false;
  pendDir = V_NONE;
  pendPark = false;
}

bool valveBusy() { return moving != V_NONE || pendDir != V_NONE; }
bool valveHomed() { return homed; }
bool valveParked() { return homed && deg <= 0.0f && !valveBusy(); }
float valveDeg() { return deg; }
ValveDir valveMoving() { return moving; }
