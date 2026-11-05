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
void hwSetSen(bool on) { digitalWrite(PIN_SEN, (on && V1_FITTED) ? HIGH : LOW); }
bool hwPen() { return digitalRead(PIN_PEN) == HIGH; }
bool hwSen() { return digitalRead(PIN_SEN) == HIGH; }
bool hwV1Open() { return V1_FITTED ? hwSen() : true; }   // without V1 nothing blocks the branch but V2
uint8_t hwFanPct() { return fanPct; }
uint8_t hwPumpPct() { return pumpPct; }

// Tach input: Timer4 samples the tach pins every ~250 us (TACH_SAMPLE_US); a level change counts only after
// TACH_DEBOUNCE samples in a row (~0.75 ms). Bench 2026-11-04: edge interrupts lost single pulses (a bounce on the slow
// RC edge was read as a rise) and counted glitches; sampling rejects both. 3000 rpm = 5 ms per half, D5 ~4800 rpm
// = 3.1 ms, so no real pulse is lost. Timer4 is free (Timer3 = PWM, Timer1/2 = LED, Timer0 = millis).
struct TachDeb { bool high; uint8_t n; };
static TachDeb debF2 = {true, 0}, debP = {true, 0};
#if FAN1_TACH_FITTED
static TachDeb debF1 = {true, 0};
#endif
static inline void tachSample(TachDeb &d, bool high, volatile uint16_t &pulses) {
  if (high == d.high) { d.n = 0; return; }
  if (++d.n < TACH_DEBOUNCE) return;
  d.high = high;
  d.n = 0;
  if (!high) pulses++;
}
ISR(TIMER4_COMPA_vect) {
  tachSample(debF2, PIND & _BV(PD2), pulsesF2);   // D19 = PD2
#if FAN1_TACH_FITTED
  tachSample(debF1, PIND & _BV(PD3), pulsesF1);   // D18 = PD3
#endif
  tachSample(debP, PINB & _BV(PB6), pulsesP);     // D12 = PB6
}

static void tachTimerInit() {   // Timer4 CTC, /64, OCR 62 -> 3968 Hz (252 us)
  TCCR4A = 0;
  TCCR4B = _BV(WGM42) | _BV(CS41) | _BV(CS40);
  OCR4A = 62;
  TIMSK4 = _BV(OCIE4A);
}

void hwInit() {
  pwmInit();
  hwSetFanPct(0);
  hwSetPumpPct(0);

  pinMode(PIN_TACH1, INPUT_PULLUP);   // fan tachs wired straight PW C22/D22 -> D18/D19 (2026-11-04), no LG 10k:
  pinMode(PIN_TACH2, INPUT_PULLUP);   // internal ~35k pull-up (an external 10k at the Mega end is still welcome)
  pinMode(PIN_PUMP_TACH, INPUT);
  tachTimerInit();

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
  rpmFan2 = (uint16_t)((f2 * 30000UL) / dt);
  // Fan 1's tach picks up the PWM below ~30 % duty (bench 2026-11-05: raw ~380k, no valid pulses at 20 %, clean from
  // 30 %). Below FAN1_TACH_MIN_PCT it reports fan 2's rpm (both share one PWM line, so they run alike) - Vlad's call.
  rpmFan1 = !FAN1_TACH_FITTED ? 0 : fanPct < FAN1_TACH_MIN_PCT ? rpmFan2 : (uint16_t)((f1 * 30000UL) / dt);
  rpmPump = (uint16_t)((p * 30000UL) / dt);
#if SIM_FAKE_P2_TACH
  if (hwPen()) rpmPump = 3000;
#endif
#if SIM_FAKE_FAN_TACH
  rpmFan2 = hwFanPct() * 30;   // iPPC-3000: ~30 rpm per %
  rpmFan1 = FAN1_TACH_FITTED ? rpmFan2 : 0;
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
#if SIM_FORCE_12V
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
