// 08_solenoid — V1 2W-025-08 NC via SEN D28 (IRLB8721 low-side, HIGH = energised = open). Needs 12 V (S3).
// Check: click on both edges, flow only when open, S3 stays present (fuse holds the inrush),
// no Mega reset when it switches (a new "# 08_solenoid" banner would mean a brown-out).
//
// Buttons:
//   BTN1 short  open (auto-close after 5 min)     BTN2 short  close
//   BTN1 long   cycle test: 10× (2 s open, 2 s shut), prints S3 adc at each edge
// Serial: 1 open, 0 close.
// LED: green = open.

#include "bench.h"

const uint8_t SEN = 28;
bool isOpen;
uint32_t openedAt;
uint8_t cyclesLeft;
uint32_t cycleAt;

void set(bool on) {
  if (on && analogRead(A2) < 500) {
    Serial.println(F("S3 low: no 12 V on the V1/V2 fuse"));
    benchBlink(255, 0, 0, 3);
    return;
  }
  digitalWrite(SEN, on ? HIGH : LOW);
  isOpen = on;
  openedAt = millis();
  benchLed(0, on ? 120 : 0, 0);
  Serial.print(on ? F("V1 OPEN") : F("V1 SHUT"));
  Serial.print(F("  S3 adc ")); Serial.println(analogRead(A2));
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("08_solenoid"));
  Serial.println(F("BTN1 short: open | BTN2 short: close | BTN1 long: 10 cycles"));
  benchButtonsBegin();
}

void loop() {
  benchButtonsUpdate();
  if (btn1.shortPress) { cyclesLeft = 0; set(true); }
  if (btn2.shortPress) { cyclesLeft = 0; set(false); }
  if (btn1.longPress) { cyclesLeft = 20; cycleAt = millis(); Serial.println(F("cycle test")); }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '1') set(true);
    if (c == '0') set(false);
  }
  if (cyclesLeft && millis() - cycleAt >= 2000) {
    cycleAt = millis();
    cyclesLeft--;
    set(!isOpen && cyclesLeft > 0);
    if (!cyclesLeft) Serial.println(F("cycle test done"));
  }
  if (isOpen && millis() - openedAt > 300000UL) { Serial.print(F("timeout: ")); set(false); }
  static uint32_t last;
  if (isOpen && millis() - last >= 5000) {
    last = millis();
    Serial.print(F("open ")); Serial.print((millis() - openedAt) / 1000);
    Serial.print(F(" s, S3 adc ")); Serial.println(analogRead(A2));
  }
}
