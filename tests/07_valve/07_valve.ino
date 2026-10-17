// 07_valve — V2 CR-02: VO D22 (open), VC D24 (close), high-side via IRF4905. Needs 12 V (S3 checked).
// Quick checks: direction (VO toward ALL-PORTS-OPEN, VC toward BYPASS-ONLY), autostop at both ends,
// interlock. For travel times, backlash and the θ equation use tests/10_v2_calibration.
//
// Buttons (hold to jog):
//   BTN1 held   VO on while held (open)     BTN2 held   VC on while held (close)
//   release     stops, prints how long it ran
//   both held   nothing moves (interlock), prints INTERLOCK
// Serial: o / c run to the end (60 s cap), O<ms> / C<ms> pulses, m = mark elapsed, s = stop.
// LED: green opening, blue closing, red = refused (no 12 V or interlock).
// Interlock mirrors the firmware: one drive() function, 500 ms dead time before reversing.

#include "bench.h"

const uint8_t VO = 22, VC = 24;
int8_t dir;            // 1 open, -1 close, 0 stopped
int8_t lastDir;
uint32_t startAt, endAt, stopAt;

void drive(int8_t d) {
  digitalWrite(VO, LOW);
  digitalWrite(VC, LOW);
  if (d > 0) digitalWrite(VO, HIGH);
  else if (d < 0) digitalWrite(VC, HIGH);
  benchLed(0, d > 0 ? 120 : 0, d < 0 ? 120 : 0);
}

bool twelveVolt() { return analogRead(A2) >= 500; }

void halt(const __FlashStringHelper *why) {
  drive(0);
  if (dir != 0) {
    Serial.print(why); Serial.print(dir > 0 ? F(" open ") : F(" close "));
    Serial.print(millis() - startAt); Serial.println(F(" ms"));
    stopAt = millis();
  }
  dir = 0;
}

void run(int8_t d, uint32_t ms) {
  if (!twelveVolt()) {
    Serial.println(F("S3 low: no 12 V on the valve fuse, not moving"));
    benchLed(150, 0, 0);
    return;
  }
  if (dir != 0) halt(F("stop"));
  if (lastDir != 0 && d != lastDir) {
    while (millis() - stopAt < 500) {}
  }
  dir = d;
  lastDir = d;
  startAt = millis();
  endAt = startAt + ms;
  drive(d);
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("07_valve"));
  Serial.println(F("hold BTN1 = open, hold BTN2 = close; serial o c O<ms> C<ms> m s"));
  Serial.print(F("S3 adc ")); Serial.println(analogRead(A2));
  benchButtonsBegin();
}

void loop() {
  benchButtonsUpdate();
  if (btn1.down && btn2.down) {
    if (dir != 0) halt(F("INTERLOCK"));
    benchLed(150, 0, 0);
  } else {
    if (btn1.pressed) run(1, 60000);
    if (btn2.pressed) run(-1, 60000);
    if ((btn1.released && dir > 0) || (btn2.released && dir < 0)) halt(F("released"));
  }

  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'o') run(1, 60000);
    else if (c == 'c') run(-1, 60000);
    else if (c == 'O') run(1, Serial.parseInt());
    else if (c == 'C') run(-1, Serial.parseInt());
    else if (c == 'm') {
      Serial.print(F("MARK ")); Serial.print(millis() - startAt); Serial.println(F(" ms since start"));
    }
    else if (c == 's') halt(F("STOP"));
  }
  if (dir != 0 && (int32_t)(millis() - endAt) >= 0) halt(F("timeout"));
}
