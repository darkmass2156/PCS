// 05_fan_pwm_tach — FAN_PWM D2 (OC3B, 25 kHz), tach FAN1 D18 (INT3), FAN2 D19 (INT2), 2 pulses/rev.
// Needs 12 V on the fan header. Answers: do the iPPCs stop at 0 %? lowest reliable duty (-> FAN_PWM_MIN)?
//
// Buttons:
//   BTN1 short  duty +10 %          BTN2 short  duty -10 %
//   BTN1 long   auto sweep 0..100 % in 10 % steps, 8 s each (press any button to stop)
//   BTN2 long   duty 0 %
// Serial: f<pct> fixed duty, s sweep.
// LED: brightness follows the duty.

#include "bench.h"

volatile uint16_t p1, p2;
void isr1() { p1++; }
void isr2() { p2++; }

uint8_t duty;
bool sweep;
uint32_t stepAt;

void setDuty(uint8_t pct) {
  duty = pct > 100 ? 100 : pct;
  if (duty == 0) { TCCR3A &= ~_BV(COM3B1); digitalWrite(2, LOW); }
  else { OCR3B = (uint16_t)(640UL * duty / 100) - 1; TCCR3A |= _BV(COM3B1); }
  benchLed(0, 0, duty * 2);
  Serial.print(F("duty ")); Serial.print(duty); Serial.println('%');
}

void setup() {
  benchOutputsLow();
  TCCR3A = _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);
  ICR3 = 639;
  pinMode(18, INPUT);
  pinMode(19, INPUT);
  attachInterrupt(digitalPinToInterrupt(18), isr1, FALLING);
  attachInterrupt(digitalPinToInterrupt(19), isr2, FALLING);
  Serial.begin(115200);
  benchBanner(F("05_fan_pwm_tach"));
  Serial.println(F("BTN1/BTN2 short: +/-10 % | BTN1 long: sweep | BTN2 long: 0 %   columns: duty rpm1 rpm2"));
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
  uint16_t a = p1, b = p2;
  p1 = p2 = 0;
  interrupts();
  Serial.print(duty); Serial.print(F("% "));
  Serial.print(a * 30000UL / dt); Serial.print(' ');
  Serial.println(b * 30000UL / dt);
}
