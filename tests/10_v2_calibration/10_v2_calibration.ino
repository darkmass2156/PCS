// 10_v2_calibration — maps the V2 equation  θ = θ0 ± (90° / t_full,dir) · (t_on − backlash)
// and the hydraulic characteristic f(θ) = outdoor flow fraction at V2's outlet.
// Paste the whole Serial output back to Claude; CAL/TIME/BL/LIN lines are machine-readable.
//
// A. Travel time (dry is fine). Start at the bypass end (BTN2 short run first).
//    BTN1 short  start an OPEN run; press BTN1 short again the moment the valve stops -> one t_open sample
//    BTN2 short  start a CLOSE run; press BTN2 short again when it stops        -> one t_close sample
//    Do 3-5 of each. The other button during a run aborts it.
// B. Backlash.  BTN1 long  moves to ~40 % and arms step mode for the reverse direction:
//    BTN2 short = one 100 ms step, BTN1 short = "it moved now". Next BTN1 long measures the other direction.
// C. Linearity (serial only): 'L' opens in 10 equal steps from bypass; after each step type the indicator
//    angle, e.g. a27 + Enter. Uses the measured t_open.
// D. Flow characteristic (ON THE FILLED LOOP, PC running so T1 is warm, outdoor colder):
//    BTN2 long  park V2 -> P2 on 80 % + V1 open -> open in N steps, settle each, log T1 T2 T3 and
//               f = (T1-T2)/(T1-T3) -> close in N steps (hysteresis) -> park, P2 off, V1 off 10 s later.
//    Any button aborts. Auto-abort: T2 < max(T2_FLOOR_C, DP + 3), P2 stall, 12 V/fuse loss, bad NTC.
// Serial: n<N> steps (default 18), s<sec> settle (default 60), ? summary + config lines.

#include <Wire.h>
#include "bench.h"

// ---- copy these from firmware/controller/config.h ----
const float NTC_BETA = 3435.0f;
const float NTC_OFFSET_T1 = 0.0f, NTC_OFFSET_T2 = 0.0f, NTC_OFFSET_T3 = 0.0f;
const uint8_t P2_PWM_RUN = 80;
// ------------------------------------------------------
const float T2_FLOOR_C = 15.0f;          // used when no SHT31 answers
const uint32_t RUN_CAP_MS = 60000;
const uint8_t VO = 22, VC = 24, PEN = 26, SEN = 28;

// ---------------- valve drive (same interlock as the firmware) ----------------
int8_t dir, lastDir;
uint32_t runStart, runEnd, stopAt;

void drive(int8_t d) {
  digitalWrite(VO, LOW);
  digitalWrite(VC, LOW);
  if (d > 0) digitalWrite(VO, HIGH);
  else if (d < 0) digitalWrite(VC, HIGH);
}

uint32_t stopRun() {
  uint32_t ran = dir ? millis() - runStart : 0;
  drive(0);
  if (dir) { lastDir = dir; stopAt = millis(); }
  dir = 0;
  return ran;
}

bool startRun(int8_t d, uint32_t ms) {
  if (analogRead(A2) < 500) { Serial.println(F("S3 low: no 12 V on the valve fuse")); benchBlink(255, 0, 0, 3); return false; }
  stopRun();
  if (lastDir && d != lastDir) while (millis() - stopAt < 500) {}   // reversal dead time
  dir = d;
  runStart = millis();
  runEnd = runStart + ms;
  drive(d);
  return true;
}

// ---------------- sensors ----------------
float readC(uint8_t pin, float offset) {
  analogRead(pin);
  uint32_t s = 0;
  for (uint8_t i = 0; i < 16; i++) s += analogRead(pin);
  float adc = s / 16.0f;
  if (adc < 1 || adc > 1022) return NAN;
  float r = 10000.0f * adc / (1023.0f - adc);
  return 1.0f / (1.0f / 298.15f + log(r / 10000.0f) / NTC_BETA) - 273.15f + offset;
}

float dpFrom(uint8_t addr) {
  const uint8_t cmd[2] = {0x24, 0x00};
  Wire.beginTransmission(addr);
  Wire.write(cmd, 2);
  if (Wire.endTransmission() != 0) return NAN;
  delay(20);
  if (Wire.requestFrom(addr, (uint8_t)6) != 6) return NAN;
  uint8_t b[6];
  for (uint8_t i = 0; i < 6; i++) b[i] = Wire.read();
  float t = -45.0f + 175.0f * (uint16_t)((b[0] << 8) | b[1]) / 65535.0f;
  float rh = 100.0f * (uint16_t)((b[3] << 8) | b[4]) / 65535.0f;
  rh = constrain(rh + (addr == 0x44 ? 1.15f : -1.15f), 0.0f, 100.0f);   // RH calibration, as config.h SHT_RH_OFFSET_A/B
  if (rh < 1) return NAN;
  float g = log(rh / 100.0f) + 17.62f * t / (243.12f + t);
  return 243.12f * g / (17.62f - g);
}

float dewPoint() {   // max of the SHT31s that answer, NAN if none
  float a = dpFrom(0x44), b = dpFrom(0x45);
  if (isnan(a)) return b;
  if (isnan(b)) return a;
  return a > b ? a : b;
}

volatile uint16_t pumpPulses;
ISR(PCINT0_vect) {
  static uint8_t prev = _BV(PB6);
  uint8_t now = PINB & _BV(PB6);
  if (prev && !now) pumpPulses++;
  prev = now;
}

// ---------------- A: timing ----------------
uint32_t tOpen[8], tClose[8];
uint8_t nOpen, nClose;
bool timing;

uint32_t mean(const uint32_t *v, uint8_t n) {
  uint32_t s = 0;
  for (uint8_t i = 0; i < n; i++) s += v[i];
  return n ? s / n : 0;
}

uint32_t tOpenEst() { return nOpen ? mean(tOpen, nOpen) : 30000; }
uint32_t tCloseEst() { return nClose ? mean(tClose, nClose) : 30000; }

void timingButton(int8_t d) {
  if (dir == 0) {
    if (startRun(d, RUN_CAP_MS)) {
      timing = true;
      benchLed(0, d > 0 ? 120 : 0, d < 0 ? 120 : 0);
      Serial.println(d > 0 ? F("OPEN run: press BTN1 when it stops") : F("CLOSE run: press BTN2 when it stops"));
    }
  } else if (dir == d && timing) {
    uint32_t t = stopRun();
    timing = false;
    benchLed(0, 0, 0);
    uint32_t *v = d > 0 ? tOpen : tClose;
    uint8_t &n = d > 0 ? nOpen : nClose;
    if (n < 8) v[n++] = t;
    Serial.print(F("TIME,")); Serial.print(d > 0 ? F("open,") : F("close,")); Serial.print(t);
    Serial.print(F(",mean,")); Serial.print(mean(v, n)); Serial.print(F(",n,")); Serial.println(n);
  } else {
    stopRun();
    timing = false;
    benchLed(0, 0, 0);
    Serial.println(F("run aborted"));
  }
}

// ---------------- B: backlash ----------------
enum BlState : uint8_t { BL_OFF, BL_PREP, BL_STEP };
BlState bl;
int8_t blDir = -1;          // direction being measured (the reversal)
uint32_t blAccum;
uint16_t blResult[2];        // [0] close-after-open, [1] open-after-close

void backlashStart() {
  // Move 40 % in the opposite direction first so the gear train is loaded that way.
  int8_t pre = -blDir;
  uint32_t ms = (pre > 0 ? tOpenEst() : tCloseEst()) * 2 / 5;
  if (!startRun(pre, ms)) return;
  bl = BL_PREP;
  blAccum = 0;
  Serial.print(F("backlash: pre-move ")); Serial.print(pre > 0 ? F("open ") : F("close "));
  Serial.print(ms); Serial.println(F(" ms ..."));
}

void backlashUpdate() {
  if (bl == BL_PREP && dir == 0) {
    bl = BL_STEP;
    Serial.print(F("step mode: BTN2 short = 100 ms ")); Serial.print(blDir > 0 ? F("OPEN") : F("CLOSE"));
    Serial.println(F(" step, BTN1 short = it moved"));
  }
}

void backlashMoved() {
  uint8_t i = blDir < 0 ? 0 : 1;
  blResult[i] = blAccum > 100 ? blAccum - 50 : blAccum / 2;   // middle of the last step
  Serial.print(F("BL,")); Serial.print(blDir < 0 ? F("close_after_open,") : F("open_after_close,"));
  Serial.print(blResult[i]); Serial.print(F(",range,")); Serial.print(blAccum > 100 ? blAccum - 100 : 0);
  Serial.print('-'); Serial.println(blAccum);
  bl = BL_OFF;
  blDir = -blDir;
  benchBlink(0, 255, 0, 2);
}

// ---------------- C: linearity ----------------
bool lin;
uint8_t linStep;

void linNext() {
  if (linStep >= 10) { lin = false; Serial.println(F("linearity done")); return; }
  linStep++;
  startRun(1, tOpenEst() / 10);
  Serial.print(F("step ")); Serial.print(linStep); Serial.println(F("/10 - type the indicator angle: a<deg>"));
}

// ---------------- D: flow sweep ----------------
enum SwState : uint8_t { SW_OFF, SW_PARK, SW_PRIME, SW_MOVE, SW_SETTLE, SW_STOP };
SwState sw;
uint8_t swN = 18, swStep;
int8_t swDir;
uint16_t settleS = 60;
uint32_t swAt, pumpOnAt, pumpZeroAt;
float sum1, sum2, sum3;
uint16_t nAvg;
float thetaModel;
float dpLast = NAN;
uint32_t dpAt;
uint16_t pumpRpm;

void sweepAbort(const __FlashStringHelper *why) {
  Serial.print(F("STOP: ")); Serial.println(why);
  sw = SW_STOP;
  swAt = millis();
  digitalWrite(SEN, LOW);
  digitalWrite(PEN, LOW);
  TCCR3A &= ~_BV(COM3C1);
  digitalWrite(3, LOW);
  startRun(-1, tCloseEst() * 13 / 10);
  benchLed(80, 0, 0);
}

void sweepStart() {
  if (analogRead(A0) < 500 || analogRead(A2) < 500) { Serial.println(F("S1/S3 low: no 12 V")); return; }
  Serial.println(F("sweep: parking V2"));
  Serial.println(F("CAL,step,dir,theta_model,T1,T2,T3,f,DP,P2rpm"));
  startRun(-1, tCloseEst() * 13 / 10);
  sw = SW_PARK;
  swStep = 0;
  thetaModel = 0;
}

void sweepUpdate(uint32_t now) {
  // Pump rpm and guards, once a second.
  static uint32_t lastGuard;
  if (sw != SW_OFF && sw != SW_STOP && now - lastGuard >= 1000) {
    uint32_t dt = now - lastGuard;
    lastGuard = now;
    noInterrupts();
    uint16_t p = pumpPulses;
    pumpPulses = 0;
    interrupts();
    pumpRpm = p * 30000UL / dt;
    if (now - dpAt > 5000) { dpAt = now; dpLast = dewPoint(); }
    float t1 = readC(A5, NTC_OFFSET_T1), t2 = readC(A4, NTC_OFFSET_T2), t3 = readC(A3, NTC_OFFSET_T3);
    float floor = isnan(dpLast) ? T2_FLOOR_C : (dpLast + 3 > T2_FLOOR_C ? dpLast + 3 : T2_FLOOR_C);
    if (isnan(t1) || isnan(t2) || isnan(t3)) { sweepAbort(F("NTC short/open")); return; }
    if (sw >= SW_PRIME && t2 < floor) { sweepAbort(F("T2 below floor (DP + 3 / T2_FLOOR_C)")); return; }
    if (analogRead(A0) < 250 || analogRead(A2) < 250) { sweepAbort(F("12 V / fuse lost")); return; }
    if (sw >= SW_PRIME && now - pumpOnAt > 3000) {
      if (pumpRpm == 0) { if (!pumpZeroAt) pumpZeroAt = now; if (now - pumpZeroAt > 5000) { sweepAbort(F("P2 stall")); return; } }
      else pumpZeroAt = 0;
    }
  }

  switch (sw) {
    case SW_PARK:
      if (dir == 0) {
        digitalWrite(PEN, HIGH);
        OCR3C = (uint16_t)(640UL * P2_PWM_RUN / 100) - 1;
        TCCR3A |= _BV(COM3C1);
        pumpOnAt = now;
        pumpZeroAt = 0;
        digitalWrite(SEN, HIGH);
        sw = SW_SETTLE;   // settle at θ = 0 first: baseline row
        swDir = 1;
        swAt = now;
        sum1 = sum2 = sum3 = 0;
        nAvg = 0;
        Serial.println(F("P2 on, V1 open, baseline settle"));
      }
      break;
    case SW_MOVE:
      if (dir == 0) { sw = SW_SETTLE; swAt = now; sum1 = sum2 = sum3 = 0; nAvg = 0; }
      break;
    case SW_SETTLE: {
      uint32_t el = now - swAt;
      static uint32_t lastAvg;
      if (el + 10000 >= settleS * 1000UL && now - lastAvg >= 500) {   // average over the last 10 s
        lastAvg = now;
        sum1 += readC(A5, NTC_OFFSET_T1);
        sum2 += readC(A4, NTC_OFFSET_T2);
        sum3 += readC(A3, NTC_OFFSET_T3);
        nAvg++;
      }
      benchLed(0, benchBreath(now, 2000) / 2, benchBreath(now, 2000) / 2);
      if (el < settleS * 1000UL) break;
      float t1 = sum1 / nAvg, t2 = sum2 / nAvg, t3 = sum3 / nAvg;
      Serial.print(F("CAL,")); Serial.print(swStep); Serial.print(swDir > 0 ? F(",open,") : F(",close,"));
      Serial.print(thetaModel, 1); Serial.print(',');
      Serial.print(t1, 2); Serial.print(','); Serial.print(t2, 2); Serial.print(','); Serial.print(t3, 2); Serial.print(',');
      if (t1 - t3 > 3.0f) Serial.print((t1 - t2) / (t1 - t3), 3); else Serial.print(F("n/a"));
      Serial.print(','); Serial.print(dpLast, 1); Serial.print(','); Serial.println(pumpRpm);
      // next step
      if (swDir > 0 && swStep >= swN) swDir = -1;
      if (swDir < 0 && swStep == 0) { sweepAbort(F("sweep complete")); break; }
      if (swDir > 0) { swStep++; thetaModel += 90.0f / swN; startRun(1, tOpenEst() / swN); }
      else { swStep--; thetaModel -= 90.0f / swN; startRun(-1, tCloseEst() / swN); }
      sw = SW_MOVE;
      break;
    }
    case SW_STOP:
      if (dir == 0 && now - swAt >= 10000) {   // V1 off 10 s after P2, as in the firmware stop order
        digitalWrite(SEN, LOW);
        sw = SW_OFF;
        benchLed(0, 0, 0);
        Serial.println(F("sweep stopped: V2 parked, P2 off, V1 off"));
      }
      break;
    default:
      break;
  }
}

// ---------------- summary ----------------
void summary() {
  Serial.println(F("--- summary (paste into config.h) ---"));
  Serial.print(F("#define V2_T_OPEN_MS   ")); Serial.print(tOpenEst()); Serial.print(F("UL  // n=")); Serial.println(nOpen);
  Serial.print(F("#define V2_T_CLOSE_MS  ")); Serial.print(tCloseEst()); Serial.print(F("UL  // n=")); Serial.println(nClose);
  Serial.print(F("#define V2_BACKLASH_MS ")); Serial.print(max(blResult[0], blResult[1]));
  Serial.print(F("UL  // close-after-open ")); Serial.print(blResult[0]);
  Serial.print(F(", open-after-close ")); Serial.println(blResult[1]);
}

void setup() {
  benchOutputsLow();
  TCCR3A = _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);
  ICR3 = 639;
  pinMode(12, INPUT);
  PCMSK0 = _BV(PCINT6);
  PCIFR = _BV(PCIF0);
  PCICR |= _BV(PCIE0);
  Wire.begin();
  Wire.setClock(50000);
  Wire.setWireTimeout(5000, true);
  Serial.begin(115200);
  benchBanner(F("10_v2_calibration"));
  Serial.println(F("A: BTN1/BTN2 short = timed open/close run | B: BTN1 long = backlash"));
  Serial.println(F("C: serial L = linearity | D: BTN2 long = flow sweep (filled loop) | ? = summary"));
  benchButtonsBegin();
}

void loop() {
  uint32_t now = millis();
  benchButtonsUpdate();

  if (sw != SW_OFF && sw != SW_STOP) {
    if (btn1.pressed || btn2.pressed) sweepAbort(F("button"));
  } else if (bl == BL_STEP) {
    if (btn2.shortPress) { blAccum += 100; startRun(blDir, 100); Serial.print(F("step total ")); Serial.println(blAccum); }
    if (btn1.shortPress) backlashMoved();
    if (btn1.longPress || btn2.longPress) { bl = BL_OFF; Serial.println(F("backlash cancelled")); }
  } else if (sw == SW_OFF) {
    if (timing && dir != 0) {
      // Mark on the press edge (not the release) so the timing doesn't include how long you held it.
      if (btn1.pressed) { btn1.consume(); timingButton(1); }
      if (btn2.pressed) { btn2.consume(); timingButton(-1); }
    } else {
      if (btn1.shortPress) timingButton(1);
      if (btn2.shortPress) timingButton(-1);
      if (btn1.longPress && dir == 0) backlashStart();
      if (btn2.longPress && dir == 0) sweepStart();
    }
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'n') { swN = constrain(Serial.parseInt(), 2, 45); Serial.print(F("steps ")); Serial.println(swN); }
    if (c == 's') { settleS = constrain(Serial.parseInt(), 15, 600); Serial.print(F("settle s ")); Serial.println(settleS); }
    if (c == '?') summary();
    if (c == 'L' && dir == 0 && sw == SW_OFF) {
      lin = true;
      linStep = 0;
      Serial.println(F("LIN: valve must be at bypass (0 deg). LIN,step,t_on_ms,theta_model,indicator"));
      linNext();
    }
    if (c == 'a' && lin) {
      float a = Serial.parseFloat();
      Serial.print(F("LIN,")); Serial.print(linStep); Serial.print(',');
      Serial.print(tOpenEst() / 10 * linStep); Serial.print(','); Serial.print(9.0f * linStep, 1);
      Serial.print(','); Serial.println(a, 1);
      linNext();
    }
  }

  if (dir != 0 && (int32_t)(now - runEnd) >= 0) {
    uint32_t t = stopRun();
    if (timing) { timing = false; Serial.print(F("run hit the 60 s cap (")); Serial.print(t); Serial.println(F(" ms), no sample")); }
    if (sw == SW_OFF && bl == BL_OFF) benchLed(0, 0, 0);
  }
  backlashUpdate();
  sweepUpdate(now);
}
