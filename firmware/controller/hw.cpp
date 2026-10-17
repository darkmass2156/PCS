#include "hw.h"
#include "config.h"

uint16_t rpmFan1, rpmFan2, rpmPump;
bool fusePresent[3];
uint16_t fuseAdc[3];

static uint8_t fanPct, pumpPct;
static volatile uint16_t pulsesF1, pulsesF2, pulsesP;
static uint32_t lastRpmAt;

void hwAllOutputsLow() {
  const uint8_t pins[] = {PIN_VO, PIN_VC, PIN_PEN, PIN_SEN, PIN_FAN_PWM, PIN_PUMP_PWM,
                          PIN_LED_R, PIN_LED_G, PIN_LED_B};
  for (uint8_t p : pins) {
    digitalWrite(p, LOW);
    pinMode(p, OUTPUT);
  }
}

// Timer3 fast PWM mode 14, TOP = ICR3 = 639, no prescaler -> 25 kHz on OC3B (D2) and OC3C (D3).
static void pwmInit() {
  TCCR3A = 0;
  TCCR3B = 0;
  TCNT3 = 0;
  ICR3 = 639;
  OCR3B = 0;
  OCR3C = 0;
  TCCR3A = _BV(WGM31);
  TCCR3B = _BV(WGM33) | _BV(WGM32) | _BV(CS30);
}

// 0 % disconnects the compare output (OCR = 0 still emits a one-tick spike).
static void pwmWrite(uint8_t comBit, volatile uint16_t &ocr, uint8_t pin, uint8_t pct) {
  if (pct > 100) pct = 100;
  if (pct == 0) {
    TCCR3A &= ~_BV(comBit);
    digitalWrite(pin, LOW);
    return;
  }
  ocr = (uint16_t)((640UL * pct) / 100) - 1;
  TCCR3A |= _BV(comBit);
}

void hwSetFanPct(uint8_t pct) {
  fanPct = pct > 100 ? 100 : pct;
  pwmWrite(COM3B1, OCR3B, PIN_FAN_PWM, fanPct);
}

void hwSetPumpPct(uint8_t pct) {
  pumpPct = pct > 100 ? 100 : pct;
  pwmWrite(COM3C1, OCR3C, PIN_PUMP_PWM, pumpPct);
}

void hwSetPen(bool on) { digitalWrite(PIN_PEN, on ? HIGH : LOW); }
void hwSetSen(bool on) { digitalWrite(PIN_SEN, on ? HIGH : LOW); }
bool hwPen() { return digitalRead(PIN_PEN) == HIGH; }
bool hwSen() { return digitalRead(PIN_SEN) == HIGH; }
uint8_t hwFanPct() { return fanPct; }
uint8_t hwPumpPct() { return pumpPct; }

static void isrTach1() { pulsesF1++; }
static void isrTach2() { pulsesF2++; }

// D12 = PB6 = PCINT6. Count falling edges only.
ISR(PCINT0_vect) {
  static uint8_t prev = _BV(PB6);
  uint8_t now = PINB & _BV(PB6);
  if (prev && !now) pulsesP++;
  prev = now;
}

void hwInit() {
  pwmInit();
  hwSetFanPct(0);
  hwSetPumpPct(0);

  pinMode(PIN_TACH1, INPUT);   // external 10k pull-ups
  pinMode(PIN_TACH2, INPUT);
  pinMode(PIN_PUMP_TACH, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_TACH1), isrTach1, FALLING);
  attachInterrupt(digitalPinToInterrupt(PIN_TACH2), isrTach2, FALLING);
  PCMSK0 = _BV(PCINT6);
  PCIFR = _BV(PCIF0);
  PCICR |= _BV(PCIE0);

  pinMode(PIN_BTN1, INPUT);    // external 10k pull-ups + RC
  pinMode(PIN_BTN2, INPUT);
  lastRpmAt = millis();
}

void hwUpdateTach(uint32_t now) {
  uint32_t dt = now - lastRpmAt;
  if (dt < 1000) return;
  noInterrupts();
  uint16_t f1 = pulsesF1, f2 = pulsesF2, p = pulsesP;
  pulsesF1 = pulsesF2 = pulsesP = 0;
  interrupts();
  lastRpmAt = now;
  // 2 pulses per revolution
  rpmFan1 = (uint16_t)((f1 * 30000UL) / dt);
  rpmFan2 = (uint16_t)((f2 * 30000UL) / dt);
  rpmPump = (uint16_t)((p * 30000UL) / dt);
#if NTC_TRIMPOT_SIM && SIM_FAKE_P2_TACH
  if (hwPen()) rpmPump = 3000;
#endif
}

static const uint8_t fusePins[3] = {PIN_S1, PIN_S2, PIN_S3};
static uint32_t fuseChangeAt[3];
static bool fuseRaw[3];
static bool v12Raw, v12Stable;
static uint32_t v12ChangeAt;

void hwUpdateFuses(uint32_t now) {
  for (uint8_t i = 0; i < 3; i++) {
    uint16_t a = analogRead(fusePins[i]);
#if NTC_TRIMPOT_SIM && SIM_FORCE_12V
    a = 1023;
#endif
    fuseAdc[i] = a;
    bool raw = fuseRaw[i];
    if (a >= FUSE_PRESENT_ADC) raw = true;
    else if (a <= FUSE_ABSENT_ADC) raw = false;   // between thresholds: hold
    if (raw != fuseRaw[i]) {
      fuseRaw[i] = raw;
      fuseChangeAt[i] = now;
    }
    if (now - fuseChangeAt[i] >= FUSE_DEBOUNCE_MS) fusePresent[i] = fuseRaw[i];
  }
  bool anyRaw = fuseRaw[0] || fuseRaw[1] || fuseRaw[2];
  if (anyRaw != v12Raw) {
    v12Raw = anyRaw;
    v12ChangeAt = now;
  }
  // Loss is reported at once; return only after it has been stable.
  if (!anyRaw) v12Stable = false;
  else if (now - v12ChangeAt >= V12_STABLE_MS) v12Stable = true;
}

bool hw12vPresent() { return v12Stable; }
