// 05_fan_pwm_tach — FAN_PWM D2 (OC3B, 25 kHz), tach FAN1 D18 (INT3), FAN2 D19 (INT2), 2 pulses/rev.
// Needs 12 V on the fan header. Auto sweep 0..100 % in 10 % steps, 8 s each, printing rpm per second.
// Answers: do the iPPCs stop at 0 %? lowest duty that still spins reliably (-> FAN_PWM_MIN)?
// Commands: f<pct> holds a fixed duty (stops the sweep), s restarts the sweep.

volatile uint16_t p1, p2;
void isr1() { p1++; }
void isr2() { p2++; }

void setDuty(uint8_t pct) {
  if (pct == 0) { TCCR3A &= ~_BV(COM3B1); digitalWrite(2, LOW); return; }
  OCR3B = (uint16_t)(640UL * pct / 100) - 1;
  TCCR3A |= _BV(COM3B1);
}

uint8_t duty;
bool sweep = true;
uint32_t stepAt;

void setup() {
  const uint8_t outs[] = {22, 24, 26, 28, 2, 3, 9, 10, 11};
  for (uint8_t p : outs) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }
  TCCR3A = _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);
  ICR3 = 639;
  setDuty(0);
  pinMode(18, INPUT);
  pinMode(19, INPUT);
  attachInterrupt(digitalPinToInterrupt(18), isr1, FALLING);
  attachInterrupt(digitalPinToInterrupt(19), isr2, FALLING);
  Serial.begin(115200);
  Serial.println(F("# 05_fan_pwm_tach  duty% rpm1 rpm2"));
  stepAt = millis();
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'f') { duty = constrain(Serial.parseInt(), 0, 100); sweep = false; setDuty(duty); }
    if (c == 's') { duty = 0; sweep = true; stepAt = millis(); setDuty(0); }
  }
  if (sweep && millis() - stepAt >= 8000) {
    stepAt = millis();
    duty = duty >= 100 ? 0 : duty + 10;
    setDuty(duty);
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
