// 05_fan_pwm_tach — FAN_PWM D2 (OC3B, 25 kHz), tach FAN1 D18 (INT3), FAN2 D19 (INT2), 2 pulses/rev.
// Needs 12 V on the fan header. Answers: do the iPPCs stop at 0 %? lowest reliable duty (-> FAN_PWM_MIN)?
//
// Buttons:
//   BTN1 short  duty +10 %          BTN2 short  duty -10 %
//   BTN1 long   auto sweep 0..100 % in 10 % steps, 8 s each (press any button to stop)
//   BTN2 long   duty 0 %
// Serial: f<pct> fixed duty, s sweep.
// LED: brightness follows the duty.
// Columns: duty | raw rpm1 rpm2 (every falling edge) | filtered rpm1 rpm2: a falling edge counts only if the pin is
// still LOW in the ISR, the line was HIGH for >= TACH_HIGH_MIN_US before it, and >= TACH_LOCKOUT_US have passed since
// the last counted one. Raw >> filtered = noise / glitches on the tach lines.
// iPPC-3000: ~3000 rpm max = 100 Hz falling edges (10 ms apart), so the 3 ms lockout never drops a real pulse.

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "bench.h"
#include "scr_gfx.h"

// Display (as the controller, rev 6b): 1.9" ST7789 320x170, CS 49, DC 53, RES 47, 4 MHz. Shows duty and, per fan,
// filtered rpm (big), raw rpm and the live tach line level. Libraries: Adafruit ST7735 and ST7789, Adafruit GFX.
Adafruit_ST7789 tft(49, 53, 47);
const int16_t W = 320;

const uint16_t TACH_LOCKOUT_US = 3000, TACH_HIGH_MIN_US = 1000;
volatile uint16_t p1, p2, f1, f2;
volatile uint32_t last1, last2, rise1, rise2;
// D18 = PD3 (INT3), D19 = PD2 (INT2); interrupts on CHANGE
void isr1() {
  uint32_t t = micros();
  if (PIND & _BV(PD3)) { rise1 = t; return; }
  p1++;
  if (t - rise1 >= TACH_HIGH_MIN_US && t - last1 >= TACH_LOCKOUT_US) { f1++; last1 = t; }
}
void isr2() {
  uint32_t t = micros();
  if (PIND & _BV(PD2)) { rise2 = t; return; }
  p2++;
  if (t - rise2 >= TACH_HIGH_MIN_US && t - last2 >= TACH_LOCKOUT_US) { f2++; last2 = t; }
}

uint8_t duty;
bool sweep;
uint32_t stepAt;

// ---------------- display ----------------
void initPanel() {
  uint8_t colmod = 0x55, m = 0xA0;   // rotation 1, RGB
  tft.init(170, 320);
  tft.setSPISpeed(4000000);
  tft.sendCommand(ST77XX_SWRESET); delay(150);
  tft.sendCommand(ST77XX_SLPOUT);  delay(120);
  tft.sendCommand(ST77XX_COLMOD, &colmod, 1); delay(10);
  tft.sendCommand(ST77XX_NORON);   delay(10);
  tft.sendCommand(ST77XX_DISPON);  delay(10);
  tft.setRotation(1);
  tft.sendCommand(ST77XX_MADCTL, &m, 1);
  tft.invertDisplay(true);
  gfx = &tft;
  tft.fillScreen(C_BLK);
  ftxt(F_LABEL, 4, 3, C_LG, C_BLK, "05  FAN PWM / TACH", 0);
  tft.drawFastHLine(0, 16, W, C_DG);
  tft.drawFastVLine(160, 20, 112, C_DG);
  tft.drawFastHLine(0, 135, W, C_DG);
  ftxt(F_VALUE, 8, 21, C_WHT, C_BLK, "FAN 1", 0);
  ftxt(F_VALUE, 168, 21, C_WHT, C_BLK, "FAN 2", 0);
  ftxt(F_LABEL, 100, 24, C_LG, C_BLK, "D18", 0);
  ftxt(F_LABEL, 260, 24, C_LG, C_BLK, "D19", 0);
  ftxt(F_LABEL, 8, 146, C_LG, C_BLK, "DUTY", 0);
}

uint32_t cache[10];
void field(uint8_t id, const Font &f, int16_t x, int16_t y, uint16_t fg, const char *s, int16_t w, char align = 'l') {
  uint32_t h = 2166136261UL ^ fg;
  for (const char *c = s; *c; c++) { h ^= (uint8_t)*c; h *= 16777619UL; }
  h |= 1;
  if (cache[id] == h) return;
  cache[id] = h;
  ftxt(f, x, y, fg, C_BLK, s, w, align);
}

void showFan(uint8_t k, uint32_t raw, uint32_t filt, bool lineHigh) {
  char b[24];
  int16_t x0 = k * 160;
  snprintf(b, sizeof(b), "%lu", (unsigned long)filt);
  field(k * 4 + 0, F_BIG, x0 + 8, 46, filt ? C_WHT : C_DG, b, 144);
  field(k * 4 + 1, F_LABEL, x0 + 8, 90, C_LG, "rpm (filtered)", 144);
  snprintf(b, sizeof(b), "raw %lu", (unsigned long)raw);
  field(k * 4 + 2, F_LABEL, x0 + 8, 106, raw > filt + filt / 2 + 60 ? C_HOT : C_LG, b, 144);   // red: lots of glitches
  field(k * 4 + 3, F_LABEL, x0 + 8, 120, lineHigh ? C_LG : C_HOT, lineHigh ? "line HIGH" : "line LOW", 144);
}

void showDuty() {
  char b[16];
  snprintf(b, sizeof(b), "%u %%", duty);
  field(8, F_BIG, 60, 138, C_WHT, b, 130);
  field(9, F_LABEL, 200, 146, C_LG, sweep ? "sweep running" : "BTN1/2: +/-10 %", 116, 'r');
}

void setDuty(uint8_t pct) {
  duty = pct > 100 ? 100 : pct;
  if (duty == 0) { TCCR3A &= ~_BV(COM3B1); digitalWrite(2, LOW); }
  else { OCR3B = (uint16_t)(640UL * duty / 100) - 1; TCCR3A |= _BV(COM3B1); }
  benchLed(0, 0, duty * 2);
  Serial.print(F("duty ")); Serial.print(duty); Serial.println('%');
  showDuty();
}

void setup() {
  benchOutputsLow();
  TCCR3A = _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);
  ICR3 = 639;
  pinMode(18, INPUT_PULLUP);   // straight PW -> Mega since 2026-11-04: external 10k + 100 nF at the Mega, internal on too
  pinMode(19, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(18), isr1, CHANGE);
  attachInterrupt(digitalPinToInterrupt(19), isr2, CHANGE);
  Serial.begin(115200);
  benchBanner(F("05_fan_pwm_tach"));
  initPanel();
  Serial.println(F("BTN1/BTN2 short: +/-10 % | BTN1 long: sweep | BTN2 long: 0 %"));
  Serial.println(F("columns: duty | raw rpm1 rpm2 | filtered rpm1 rpm2"));
  benchButtonsBegin();
  setDuty(0);
}

void loop() {
  benchButtonsUpdate();
  if (sweep && (btn1.shortPress || btn2.shortPress)) { sweep = false; Serial.println(F("sweep stopped")); }
  else {
    if (btn1.shortPress) setDuty(duty + 10 > 100 ? 100 : duty + 10);
    if (btn2.shortPress) setDuty(duty < 10 ? 0 : duty - 10);
  }
  if (btn1.longPress) { sweep = true; stepAt = millis(); setDuty(0); }
  if (btn2.longPress) { sweep = false; setDuty(0); }
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'f') { sweep = false; setDuty(constrain(Serial.parseInt(), 0, 100)); }
    if (c == 's') { sweep = true; stepAt = millis(); setDuty(0); }
  }
  if (sweep && millis() - stepAt >= 8000) {
    stepAt = millis();
    if (duty >= 100) { sweep = false; setDuty(0); Serial.println(F("sweep done")); }
    else setDuty(duty + 10);
  }

  static uint32_t last;
  if (millis() - last < 1000) return;
  uint32_t dt = millis() - last;
  last = millis();
  noInterrupts();
  uint16_t a = p1, b = p2, fa = f1, fb = f2;
  p1 = p2 = f1 = f2 = 0;
  interrupts();
  Serial.print(duty); Serial.print(F("% | raw "));
  Serial.print(a * 30000UL / dt); Serial.print(' ');
  Serial.print(b * 30000UL / dt); Serial.print(F(" | filtered "));
  Serial.print(fa * 30000UL / dt); Serial.print(' ');
  Serial.println(fb * 30000UL / dt);
  showFan(0, a * 30000UL / dt, fa * 30000UL / dt, PIND & _BV(PD3));
  showFan(1, b * 30000UL / dt, fb * 30000UL / dt, PIND & _BV(PD2));
  showDuty();
}
