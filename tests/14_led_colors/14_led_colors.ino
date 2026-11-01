// 14_led_colors — map the status LED colours and find per-channel gains (white balance).
// R D9 (220 Ω), G D10 (150 Ω), B D11 (150 Ω), common cathode. Every change prints the PWM values sent.
// BTN1 long = next page. Pages:
//   1 COLORS  the firmware colours (both schemes) with the gains applied
//             BTN1 short next, BTN2 short previous, BTN2 long breathing on/off
//   2 MIX     raw R/G/B, no gains. BTN1 short next channel (it flashes alone for 0.4 s),
//             BTN2 short +16 (wraps to 0 after 255), BTN2 long selected channel to 0
//   3 GAIN    white balance: shows white x gains. BTN1 short next channel (flashes alone),
//             BTN2 short gain -5 % (wraps 0 -> 100), BTN2 long print the #define lines for config.h
//   4 SWEEP   hue wheel with the gains. BTN1 short pause and print, BTN2 short speed 3 / 6 / 12 s
// Serial: c<r> <g> <b> raw colour (goes to MIX), g<r> <g> <b> gains in %, n next page, p print, ? help.
// Goal: find gains where white looks white and the colours are distinct; send me the printed #define lines.

#include "bench.h"

struct Named { const char *name; uint8_t r, g, b; };
const Named COLORS[] = {
  {"white (Mode 1)", 255, 255, 255}, {"blue (Mode 2)", 0, 0, 255},   {"red (Mode 3 / fault)", 255, 0, 0},
  {"green (mode change)", 0, 255, 0}, {"yellow (alarm, s1)", 200, 120, 0}, {"dim white (s2 Mode 1)", 50, 50, 50},
  {"cyan (s2 V2 moving)", 0, 200, 200}, {"purple (s2 boot / no DP)", 150, 0, 200},
  {"orange", 255, 60, 0}, {"pink", 255, 40, 120}, {"lime", 120, 255, 0}, {"sky", 0, 120, 255},
};
const uint8_t NCOL = sizeof(COLORS) / sizeof(COLORS[0]);
const char *const PAGE[] = {"COLORS", "MIX", "GAIN", "SWEEP"};
const char CH[3] = {'R', 'G', 'B'};

uint8_t page, colIdx, chan, speedIdx = 1;
uint8_t mix[3] = {255, 255, 255};
uint8_t gain[3] = {100, 100, 100};   // %
bool breathing, sweepPaused;
uint32_t flashUntil, sweepT0, sweepHeld;
const uint16_t SPEEDS[3] = {3000, 6000, 12000};
uint8_t lastOut[3] = {1, 1, 1};

uint8_t g(uint8_t v, uint8_t k) { return (uint16_t)v * gain[k] / 100; }

void hue(uint32_t t, uint16_t period, uint8_t c[3]) {
  uint16_t h = (uint32_t)(t % period) * 1536UL / period;   // 6 sectors x 256
  uint8_t s = h / 256, f = h % 256;
  uint8_t up = f, dn = 255 - f;
  const uint8_t tbl[6][3] = {{255, up, 0}, {dn, 255, 0}, {0, 255, up}, {0, dn, 255}, {up, 0, 255}, {255, 0, dn}};
  for (uint8_t k = 0; k < 3; k++) c[k] = tbl[s][k];
}

void printDefines() {
  Serial.println(F("// gains for config.h (applied to every LED colour):"));
  for (uint8_t k = 0; k < 3; k++) {
    Serial.print(F("#define LED_GAIN_")); Serial.print(CH[k]); Serial.print(' '); Serial.println(gain[k]);
  }
}

void announce() {
  Serial.print(F("[")); Serial.print(PAGE[page]); Serial.print(F("] "));
  if (page == 0) {
    Serial.print(COLORS[colIdx].name); Serial.print(F("  in ")); Serial.print(COLORS[colIdx].r); Serial.print(' ');
    Serial.print(COLORS[colIdx].g); Serial.print(' '); Serial.print(COLORS[colIdx].b);
    if (breathing) Serial.print(F("  breathing"));
  } else if (page == 1) {
    Serial.print(F("raw ")); Serial.print(mix[0]); Serial.print(' '); Serial.print(mix[1]); Serial.print(' ');
    Serial.print(mix[2]); Serial.print(F("  editing ")); Serial.print(CH[chan]);
  } else if (page == 2) {
    Serial.print(F("white, gains % ")); Serial.print(gain[0]); Serial.print(' '); Serial.print(gain[1]);
    Serial.print(' '); Serial.print(gain[2]); Serial.print(F("  editing ")); Serial.print(CH[chan]);
  } else {
    Serial.print(sweepPaused ? F("paused") : F("running")); Serial.print(F(", period ")); Serial.print(SPEEDS[speedIdx]);
    Serial.print(F(" ms"));
  }
  Serial.println();
}

void help() {
  Serial.println(F("BTN1 long: next page (COLORS / MIX / GAIN / SWEEP)"));
  Serial.println(F("COLORS: BTN1 next, BTN2 prev, BTN2 long breathe | MIX: BTN1 channel, BTN2 +16, BTN2 long ch=0"));
  Serial.println(F("GAIN: BTN1 channel, BTN2 -5 %, BTN2 long print defines | SWEEP: BTN1 pause, BTN2 speed"));
  Serial.println(F("serial: c<r> <g> <b>  g<r> <g> <b>  n  p  ?"));
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("14_led_colors"));
  help();
  benchButtonsBegin();
  sweepT0 = millis();
  announce();
}

void loop() {
  benchButtonsUpdate();
  uint32_t now = millis();

  if (btn1.longPress) { page = (page + 1) % 4; chan = 0; announce(); }
  if (btn1.shortPress) {
    if (page == 0) colIdx = (colIdx + 1) % NCOL;
    else if (page == 3) {
      sweepPaused = !sweepPaused;
      if (sweepPaused) sweepHeld = now - sweepT0; else sweepT0 = now - sweepHeld;
    } else { chan = (chan + 1) % 3; flashUntil = now + 400; }
    announce();
  }
  if (btn2.shortPress) {
    if (page == 0) colIdx = (colIdx + NCOL - 1) % NCOL;
    else if (page == 1) mix[chan] = mix[chan] == 255 ? 0 : mix[chan] >= 240 ? 255 : mix[chan] + 16;
    else if (page == 2) gain[chan] = gain[chan] == 0 ? 100 : gain[chan] - 5;
    else speedIdx = (speedIdx + 1) % 3;
    announce();
  }
  if (btn2.longPress) {
    if (page == 0) breathing = !breathing;
    else if (page == 1) mix[chan] = 0;
    else if (page == 2) printDefines();
    announce();
  }

  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'c' || c == 'g') {
      int a = Serial.parseInt(), b = Serial.parseInt(), d = Serial.parseInt();
      if (c == 'c') { mix[0] = constrain(a, 0, 255); mix[1] = constrain(b, 0, 255); mix[2] = constrain(d, 0, 255); page = 1; }
      else { gain[0] = constrain(a, 0, 100); gain[1] = constrain(b, 0, 100); gain[2] = constrain(d, 0, 100); }
      announce();
    }
    if (c == 'n') { page = (page + 1) % 4; announce(); }
    if (c == 'p') { announce(); printDefines(); }
    if (c == '?') help();
  }

  uint8_t out[3];
  if (page == 0) {
    const Named &n = COLORS[colIdx];
    uint8_t k = breathing ? benchBreath(now, 4000) : 255;
    out[0] = g(n.r, 0) * k / 255; out[1] = g(n.g, 1) * k / 255; out[2] = g(n.b, 2) * k / 255;
  } else if (page == 1) {
    for (uint8_t i = 0; i < 3; i++) out[i] = mix[i];
  } else if (page == 2) {
    for (uint8_t i = 0; i < 3; i++) out[i] = g(255, i);
  } else {
    uint8_t c[3];
    hue(sweepPaused ? sweepHeld : now - sweepT0, SPEEDS[speedIdx], c);
    for (uint8_t i = 0; i < 3; i++) out[i] = g(c[i], i);
  }
  if ((page == 1 || page == 2) && (int32_t)(flashUntil - now) > 0)      // selected channel alone
    for (uint8_t i = 0; i < 3; i++) if (i != chan) out[i] = 0;
  benchLed(out[0], out[1], out[2]);

  bool changed = out[0] != lastOut[0] || out[1] != lastOut[1] || out[2] != lastOut[2];
  if (changed && page != 3 && !(page == 0 && breathing) && (int32_t)(flashUntil - now) <= 0) {
    Serial.print(F("  pwm out ")); Serial.print(out[0]); Serial.print(' '); Serial.print(out[1]); Serial.print(' ');
    Serial.println(out[2]);
  }
  if (sweepPaused && page == 3 && changed) {
    Serial.print(F("  pwm out ")); Serial.print(out[0]); Serial.print(' '); Serial.print(out[1]); Serial.print(' ');
    Serial.println(out[2]);
  }
  for (uint8_t i = 0; i < 3; i++) lastOut[i] = out[i];
}
