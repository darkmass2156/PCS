// 03_fuse_sense — S1 (A0, P2 fuse), S2 (A1, fans), S3 (A2, V1+V2). 47k/20k dividers, 12 V ≈ 3.58 V ≈ 733 adc.
// Thresholds: present >= 500, absent <= 250, in between = hold previous.
//
// Buttons:
//   BTN1 short  next expected scenario: ALL OK -> 12V ABSENT -> S1 PULLED -> S2 PULLED -> S3 PULLED
//               each line then ends with PASS/FAIL against the expectation
//   BTN2 short  print a "--- marker ---" line (use it when you flip the supply or pull a fuse)
// LED: green = matches the expected scenario, red = doesn't.

#include "bench.h"

const uint8_t PINS[3] = {A0, A1, A2};
const char *const NAMES[3] = {"S1 P2", "S2 fan", "S3 valve"};
const char *const SCEN[5] = {"ALL OK", "12V ABSENT", "S1 PULLED", "S2 PULLED", "S3 PULLED"};
const uint8_t EXPECT[5] = {0b111, 0b000, 0b110, 0b101, 0b011};   // bit i = S(i+1) present
bool present[3];
uint8_t scenario;

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("03_fuse_sense"));
  Serial.println(F("BTN1 short: next expected scenario | BTN2 short: marker"));
  benchButtonsBegin();
}

void loop() {
  benchButtonsUpdate();
  if (btn1.shortPress) {
    scenario = (scenario + 1) % 5;
    Serial.print(F("--- expecting ")); Serial.print(SCEN[scenario]); Serial.println(F(" ---"));
  }
  if (btn2.shortPress) Serial.println(F("--- marker ---"));

  static uint32_t last;
  if (millis() - last < 1000) return;
  last = millis();
  uint8_t mask = 0;
  for (uint8_t i = 0; i < 3; i++) {
    uint16_t a = analogRead(PINS[i]);
    if (a >= 500) present[i] = true;
    else if (a <= 250) present[i] = false;
    float v = a * 5.0f / 1023.0f;
    Serial.print(NAMES[i]); Serial.print(' ');
    Serial.print(a); Serial.print(' ');
    Serial.print(v, 2); Serial.print(F("V "));
    Serial.print(v * 67.0f / 20.0f, 1); Serial.print(F("V "));
    Serial.print(present[i] ? F("OK   ") : F("--   "));
    if (present[i]) mask |= 1 << i;
  }
  if (mask == 0) Serial.print(F("=> 12V ABSENT"));
  else if (mask == 0b111) Serial.print(F("=> all OK"));
  else {
    Serial.print(F("=> FUSE:"));
    if (!present[0]) Serial.print(F(" S1->fault"));
    if (!present[1]) Serial.print(F(" S2->alarm"));
    if (!present[2]) Serial.print(F(" S3->fault"));
  }
  bool pass = mask == EXPECT[scenario];
  Serial.print(F("   [")); Serial.print(SCEN[scenario]); Serial.println(pass ? F("] PASS") : F("] FAIL"));
  if (pass) benchLed(0, 60, 0); else benchLed(150, 0, 0);
}
