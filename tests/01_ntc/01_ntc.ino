// 01_ntc — T1 (A5), T2 (A4), T3 (A3). Prints adc, ohm, °C at 1 Hz.
//
// Buttons:
//   BTN1 short  ice-water point: average 60 s, print per-sensor offsets for config.h (assumes 0.0 °C)
//   BTN2 short  next Beta preset (3435 / 3950 / 3380 / 3977) for the printout
//   BTN1 long   second point at REF_C below (warm water + reference thermometer): prints Beta per sensor
//   BTN2 long   pause / resume the 1 Hz printout
// Serial: c = ice point, r<temp> = second point at that temp (e.g. r40.2), b<beta> = set Beta.
// LED: blue while averaging, green blink = done.

#include "bench.h"

const float REF_C = 40.0f;   // second-point temperature for BTN1 long (serial 'r' takes any value)
const float BETAS[] = {3435, 3950, 3380, 3977};

const uint8_t PINS[3] = {A5, A4, A3};
const char *const NAMES[3] = {"T1", "T2", "T3"};
float beta = 3435.0f;
uint8_t betaIdx;
float rIce[3];
bool haveIce, paused;

float readAdc(uint8_t pin) {
  analogRead(pin);
  uint32_t s = 0;
  for (uint8_t i = 0; i < 16; i++) s += analogRead(pin);
  return s / 16.0f;
}

float adcToR(float adc) { return 10000.0f * adc / (1023.0f - adc); }

float rToC(float r) { return 1.0f / (1.0f / 298.15f + log(r / 10000.0f) / beta) - 273.15f; }

void average(float r[3], uint16_t seconds) {
  float sum[3] = {0, 0, 0};
  benchLed(0, 0, 80);
  for (uint16_t i = 0; i < seconds; i++) {
    for (uint8_t k = 0; k < 3; k++) sum[k] += adcToR(readAdc(PINS[k]));
    Serial.print('.');
    delay(1000);
  }
  Serial.println();
  for (uint8_t k = 0; k < 3; k++) r[k] = sum[k] / seconds;
  benchLed(0, 0, 0);
  benchBlink(0, 255, 0, 3);
}

void icePoint() {
  Serial.println(F("ice water: probes in stirred ice water, averaging 60 s"));
  average(rIce, 60);
  haveIce = true;
  for (uint8_t k = 0; k < 3; k++) {
    Serial.print(F("#define NTC_OFFSET_")); Serial.print(NAMES[k]); Serial.print(' ');
    Serial.print(-rToC(rIce[k]), 2);
    Serial.print(F("f   // R0 = ")); Serial.println(rIce[k], 0);
  }
}

void secondPoint(float tRef) {
  if (!haveIce) { Serial.println(F("do the ice point first")); benchBlink(255, 0, 0, 3); return; }
  Serial.print(F("reference ")); Serial.print(tRef, 2); Serial.println(F(" C, averaging 60 s"));
  float r2[3];
  average(r2, 60);
  for (uint8_t k = 0; k < 3; k++) {
    float b = log(rIce[k] / r2[k]) / (1.0f / 273.15f - 1.0f / (tRef + 273.15f));
    Serial.print(NAMES[k]); Serial.print(F(" beta ")); Serial.println(b, 0);
  }
  Serial.println(F("use the mean for NTC_BETA, then redo the ice point with that beta for the offsets"));
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("01_ntc"));
  Serial.println(F("BTN1 short: ice point | BTN2 short: next beta | BTN1 long: 2nd point | BTN2 long: pause"));
  benchButtonsBegin();
}

void loop() {
  benchButtonsUpdate();
  if (btn1.shortPress) icePoint();
  if (btn1.longPress) secondPoint(REF_C);
  if (btn2.shortPress) {
    betaIdx = (betaIdx + 1) % 4;
    beta = BETAS[betaIdx];
    Serial.print(F("beta = ")); Serial.println(beta, 0);
  }
  if (btn2.longPress) { paused = !paused; Serial.println(paused ? F("paused") : F("running")); }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'b') { beta = Serial.parseFloat(); Serial.print(F("beta = ")); Serial.println(beta); }
    if (c == 'c') icePoint();
    if (c == 'r') secondPoint(Serial.parseFloat());
  }

  static uint32_t last;
  if (paused || millis() - last < 1000) return;
  last = millis();
  for (uint8_t k = 0; k < 3; k++) {
    float adc = readAdc(PINS[k]);
    Serial.print(NAMES[k]); Serial.print(' ');
    Serial.print(adc, 1); Serial.print(' ');
    if (adc < 1 || adc > 1022) { Serial.print(adc < 1 ? F("SHORT(reads hot)   ") : F("OPEN(reads cold)   ")); continue; }
    float r = adcToR(adc);
    Serial.print(r, 0); Serial.print(' ');
    Serial.print(rToC(r), 2); Serial.print(F("   "));
  }
  Serial.println();
}
