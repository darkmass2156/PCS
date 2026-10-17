// 04_buttons_led — BTN1 (D30), BTN2 (D32) active LOW; RGB LED R D9, G D10, B D11 (common cathode).
// Every press prints SHORT (with duration and raw edge count = bounce) or LONG (>= 2 s, fires while held).
//
// Buttons:
//   BTN1 short  next test colour: red, green, blue, white, amber (check R/G/B wiring and balance)
//   BTN2 short  brightness step 25/50/75/100 %
//   BTN1 long   firmware LED scheme demo: Mode 1 white, Mode 2 blue, Mode 3 red breathing,
//               3 fast green pulses on each change, then fault (fast red) and alarm (yellow flash)
//   BTN2 long   LED off
//   BTN1 + BTN2 held together: prints BOTH (the firmware never needs it; checks for crosstalk)

#include "bench.h"

const uint8_t COLORS[][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {255, 120, 0}};
const char *const CNAMES[] = {"red", "green", "blue", "white", "amber"};
uint8_t colorIdx, level = 4;
uint16_t edges[2];
bool demo, bothShown;
uint32_t demoAt;

void led() {
  benchLed(COLORS[colorIdx][0] * level / 4, COLORS[colorIdx][1] * level / 4, COLORS[colorIdx][2] * level / 4);
}

// 6 s per step: M1, M2, M3, fault, alarm.
void demoLed(uint32_t now) {
  uint32_t t = now - demoAt;
  uint8_t step = (t / 6000) % 5;
  uint32_t inStep = t % 6000;
  if (step <= 2 && inStep < 750) {
    benchLed(0, inStep % 250 < 120 ? 255 : 0, 0);
    return;
  }
  uint8_t k = benchBreath(now, 4000);
  if (step == 0) benchLed(k, k, k);
  else if (step == 1) benchLed(0, 0, k);
  else if (step == 2) benchLed(k, 0, 0);
  else if (step == 3) benchLed((now / 125) % 2 ? 255 : 0, 0, 0);
  else if (now % 2000 < 100) benchLed(200, 120, 0);
  else benchLed(0, 0, k);
}

void report(BenchButton &b, const __FlashStringHelper *name, uint8_t i, uint32_t now) {
  if (b.pressed) edges[i] = 0;
  if (b.shortPress) {
    Serial.print(name); Serial.print(F(" SHORT ")); Serial.print(now - b.downAt);
    Serial.print(F(" ms, raw edges ")); Serial.println(edges[i]);
  }
  if (b.longPress) { Serial.print(name); Serial.println(F(" LONG")); }
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("04_buttons_led"));
  Serial.println(F("BTN1 short: colour | BTN2 short: brightness | BTN1 long: scheme demo | BTN2 long: off"));
  benchButtonsBegin();
  led();
}

void loop() {
  uint32_t now = millis();
  static bool r1, r2;
  bool n1 = digitalRead(30) == LOW, n2 = digitalRead(32) == LOW;
  if (n1 != r1) { r1 = n1; edges[0]++; }
  if (n2 != r2) { r2 = n2; edges[1]++; }
  benchButtonsUpdate();
  report(btn1, F("BTN1"), 0, now);
  report(btn2, F("BTN2"), 1, now);

  if (btn1.down && btn2.down) {
    if (!bothShown) { Serial.println(F("BOTH")); bothShown = true; }
  } else {
    bothShown = false;
  }

  if (btn1.shortPress) {
    demo = false;
    colorIdx = (colorIdx + 1) % 5;
    Serial.print(F("colour ")); Serial.println(CNAMES[colorIdx]);
    led();
  }
  if (btn2.shortPress) {
    demo = false;
    level = level % 4 + 1;
    Serial.print(F("level ")); Serial.print(level * 25); Serial.println('%');
    led();
  }
  if (btn1.longPress) {
    demo = true;
    demoAt = now;
    Serial.println(F("demo: M1 white, M2 blue, M3 red (green x3 before each), fault, alarm; 6 s each"));
  }
  if (btn2.longPress) { demo = false; benchLed(0, 0, 0); Serial.println(F("LED off")); }
  if (demo) demoLed(now);
}
