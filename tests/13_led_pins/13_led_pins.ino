// 13_led_pins — status LED wiring check (rev 6b): common-cathode RGB, HIGH = on.
// R on D9 (220 Ω), G on D10 (150 Ω), B on D11 (150 Ω), through C3 to the 570X top panel.
// Cycles one pin at a time (1 s each): D9 -> D10 -> D11 -> all three (white) -> off, and prints which pin
// is HIGH, so a swapped wire shows up as the wrong colour for the printed pin.
// Buttons:
//   BTN1 short  pause / resume the cycle
//   BTN2 short  next step (also while paused)
//   BTN1 long   PWM fade test: each channel 0 -> 255 -> 0 with analogWrite (Timer2 D9/D10, Timer1 D11)
//   BTN2 long   colour sample: the colours both LED schemes use, 2 s each, with their names
// Serial: p pause, n next, f fade, c colours, ? help.
// Pass: D9 = red, D10 = green, D11 = blue, all three = white-ish; fades are smooth with no flicker.

#include "bench.h"

const uint8_t PIN_R = 9, PIN_G = 10, PIN_B = 11;
const char *const STEP_NAMES[] = {"D9  HIGH -> should be RED", "D10 HIGH -> should be GREEN",
                                  "D11 HIGH -> should be BLUE", "D9 + D10 + D11 HIGH -> should be WHITE", "all LOW -> off"};
uint8_t stepNo;
bool paused;

void showStep() {
  digitalWrite(PIN_R, stepNo == 0 || stepNo == 3);
  digitalWrite(PIN_G, stepNo == 1 || stepNo == 3);
  digitalWrite(PIN_B, stepNo == 2 || stepNo == 3);
  Serial.println(STEP_NAMES[stepNo]);
}

void fade() {
  const uint8_t pins[3] = {PIN_R, PIN_G, PIN_B};
  const char *const names[3] = {"D9 red", "D10 green", "D11 blue"};
  benchLed(0, 0, 0);
  for (uint8_t c = 0; c < 3; c++) {
    Serial.print(F("fade ")); Serial.println(names[c]);
    for (int16_t v = 0; v <= 510; v += 2) {
      analogWrite(pins[c], v <= 255 ? v : 510 - v);
      delay(6);   // test only
    }
    analogWrite(pins[c], 0);
  }
}

void colours() {
  struct { const char *name; uint8_t r, g, b; } c[] = {
    {"scheme 1: Mode 1 white", 255, 255, 255}, {"scheme 1: Mode 2 blue", 0, 0, 255},
    {"scheme 1: Mode 3 red", 255, 0, 0}, {"scheme 1: mode change green", 0, 255, 0},
    {"scheme 1: alarm yellow", 200, 120, 0}, {"scheme 2: Mode 1 dim white", 50, 50, 50},
    {"scheme 2: V2 moving cyan", 0, 200, 200}, {"scheme 2: boot / no sensors purple", 150, 0, 200}};
  for (auto &k : c) {
    Serial.println(k.name);
    benchLed(k.r, k.g, k.b);
    delay(2000);   // test only
  }
  benchLed(0, 0, 0);
}

void help() {
  Serial.println(F("BTN1 short: pause/resume | BTN2 short: next | BTN1 long: PWM fade | BTN2 long: colour sample"));
  Serial.println(F("serial: p pause, n next, f fade, c colours, ? help"));
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("13_led_pins"));
  help();
  benchButtonsBegin();
  showStep();
}

void loop() {
  benchButtonsUpdate();
  char c = 0;
  if (btn1.shortPress) c = 'p';
  if (btn2.shortPress) c = 'n';
  if (btn1.longPress) c = 'f';
  if (btn2.longPress) c = 'c';
  if (!c && Serial.available()) c = Serial.read();
  static uint32_t at;
  if (c == 'p') {
    paused = !paused;
    Serial.println(paused ? F("--- paused ---") : F("--- running ---"));
  }
  if (c == '?') help();
  if (c == 'f' || c == 'c') {
    if (c == 'f') fade(); else colours();
    showStep();
    at = millis();
  }
  if (c == 'n' || (!paused && millis() - at >= 1000)) {
    stepNo = (stepNo + 1) % 5;
    showStep();
    at = millis();
  }
}
