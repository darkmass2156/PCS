// 09_pump2 — P2: PEN D26 (12 V high-side), PUMP_PWM D3 (OC3C, 25 kHz), tach D12 (PCINT6), 2 pulses/rev.
// ONLY with the loop filled: a D5 must never run dry. V2 at bypass gives P2 a path via the Barrow valve.
// First confirm P2 is the 4-pin PWM + tach Bykski.
//
// Buttons:
//   BTN1 short  PEN on at 50 %, then +10 % per press     BTN2 short  -10 % (at 10 %: stays on)
//   BTN1 long   ramp 20..100 % in 10 % steps, 10 s each (checks higher duty = faster)
//   BTN2 long   PEN off
// Serial: p<pct> on at duty, 0 off, r ramp, n on with no PWM signal (pin LOW).
// Prints rpm each second; STALL like the firmware (rpm 0 > 5 s after 3 s spin-up).
// LED: blue brightness = duty, red = stall.

#include "bench.h"

const uint8_t PEN = 26;
volatile uint16_t pulses;
ISR(PCINT0_vect) {
  static uint8_t prev = _BV(PB6);
  uint8_t now = PINB & _BV(PB6);
  if (prev && !now) pulses++;
  prev = now;
}

bool on, ramp;
uint8_t duty;
uint32_t onAt, zeroSince, rampAt;

void setDuty(uint8_t pct) {
  if (pct == 0) { TCCR3A &= ~_BV(COM3C1); digitalWrite(3, LOW); return; }
  OCR3C = (uint16_t)(640UL * pct / 100) - 1;
  TCCR3A |= _BV(COM3C1);
}

void start(uint8_t pct) {
  duty = pct > 100 ? 100 : pct;
  setDuty(duty);
  if (!on) { digitalWrite(PEN, HIGH); on = true; onAt = millis(); zeroSince = 0; }
  benchLed(0, 0, duty * 2);
  Serial.print(F("PEN on, duty ")); Serial.println(duty);
}

void stop() {
  ramp = false;
  digitalWrite(PEN, LOW);
  setDuty(0);
  on = false;
  duty = 0;
  benchLed(0, 0, 0);
  Serial.println(F("PEN off"));
}

void setup() {
  benchOutputsLow();
  TCCR3A = _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);
  ICR3 = 639;
  setDuty(0);
  pinMode(12, INPUT);
  PCMSK0 = _BV(PCINT6);
  PCIFR = _BV(PCIF0);
  PCICR |= _BV(PCIE0);
  Serial.begin(115200);
  benchBanner(F("09_pump2"));
  Serial.println(F("BTN1 short: on/+10 | BTN2 short: -10 | BTN1 long: ramp | BTN2 long: off  (loop must be filled)"));
  Serial.print(F("S1 adc ")); Serial.println(analogRead(A0));
  benchButtonsBegin();
}

void loop() {
  benchButtonsUpdate();
  if (btn1.shortPress) { ramp = false; start(on ? duty + 10 : 50); }
  if (btn2.shortPress && on) { ramp = false; start(duty > 10 ? duty - 10 : 10); }
  if (btn1.longPress) { ramp = true; rampAt = millis(); start(20); }
  if (btn2.longPress) stop();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'p') { ramp = false; start(constrain(Serial.parseInt(), 1, 100)); }
    if (c == '0') stop();
    if (c == 'r') { ramp = true; rampAt = millis(); start(20); }
    if (c == 'n') { ramp = false; start(0); Serial.println(F("(no PWM signal)")); }
  }
  if (ramp && millis() - rampAt >= 10000) {
    rampAt = millis();
    if (duty >= 100) { ramp = false; Serial.println(F("ramp done")); }
    else start(duty + 10);
  }

  static uint32_t last;
  if (millis() - last < 1000) return;
  uint32_t dt = millis() - last;
  last = millis();
  noInterrupts();
  uint16_t p = pulses;
  pulses = 0;
  interrupts();
  uint32_t rpm = p * 30000UL / dt;
  if (on && millis() - onAt > 3000 && rpm == 0) {
    if (!zeroSince) zeroSince = millis();
    if (millis() - zeroSince > 5000) { Serial.print(F("STALL ")); benchLed(150, 0, 0); }
  } else {
    zeroSince = 0;
  }
  Serial.print(on ? F("on ") : F("off ")); Serial.print(duty); Serial.print(F("% "));
  Serial.print(rpm); Serial.println(F(" rpm"));
}
