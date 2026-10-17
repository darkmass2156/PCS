// 01_ntc — T1 (A5), T2 (A4), T3 (A3). Prints adc, R, °C at 1 Hz.
// Commands (Serial Monitor, newline):
//   c          ice-water point: average 60 s, print per-sensor offsets for config.h (assumes 0.0 °C)
//   r<temp>    second point at a reference temp, e.g. r40.2: average 60 s, print Beta per sensor
//              (needs a 'c' run first in the same session)
//   b<beta>    use this Beta for the printout, e.g. b3950

const uint8_t PINS[3] = {A5, A4, A3};
const char *const NAMES[3] = {"T1", "T2", "T3"};
float beta = 3435.0f;
float rIce[3];
bool haveIce;

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
  for (uint16_t i = 0; i < seconds; i++) {
    for (uint8_t k = 0; k < 3; k++) sum[k] += adcToR(readAdc(PINS[k]));
    Serial.print('.');
    delay(1000);
  }
  Serial.println();
  for (uint8_t k = 0; k < 3; k++) r[k] = sum[k] / seconds;
}

void setup() {
  const uint8_t outs[] = {22, 24, 26, 28, 2, 3, 9, 10, 11};
  for (uint8_t p : outs) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }
  Serial.begin(115200);
  Serial.println(F("# 01_ntc  adc / ohm / degC per sensor; commands: c, r<temp>, b<beta>"));
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c == 'b') { beta = Serial.parseFloat(); Serial.print(F("beta = ")); Serial.println(beta); }
    if (c == 'c') {
      Serial.println(F("ice water: hold all probes in stirred ice water, averaging 60 s"));
      average(rIce, 60);
      haveIce = true;
      for (uint8_t k = 0; k < 3; k++) {
        Serial.print(F("#define NTC_OFFSET_")); Serial.print(NAMES[k]); Serial.print(' ');
        Serial.print(-rToC(rIce[k]), 2);
        Serial.print(F("f   // R0 = ")); Serial.println(rIce[k], 0);
      }
    }
    if (c == 'r') {
      float tRef = Serial.parseFloat();
      if (!haveIce) { Serial.println(F("run 'c' first")); return; }
      Serial.print(F("reference ")); Serial.print(tRef, 2); Serial.println(F(" C, averaging 60 s"));
      float r2[3];
      average(r2, 60);
      for (uint8_t k = 0; k < 3; k++) {
        float b = log(rIce[k] / r2[k]) / (1.0f / 273.15f - 1.0f / (tRef + 273.15f));
        Serial.print(NAMES[k]); Serial.print(F(" beta ")); Serial.println(b, 0);
      }
      Serial.println(F("use the mean for NTC_BETA, then redo 'c' with that beta for the offsets"));
    }
  }

  static uint32_t last;
  if (millis() - last < 1000) return;
  last = millis();
  for (uint8_t k = 0; k < 3; k++) {
    float adc = readAdc(PINS[k]);
    Serial.print(NAMES[k]); Serial.print(' ');
    Serial.print(adc, 1); Serial.print(' ');
    if (adc < 1 || adc > 1022) { Serial.print(adc < 1 ? F("SHORT(reads hot) ") : F("OPEN(reads cold) ")); continue; }
    float r = adcToR(adc);
    Serial.print(r, 0); Serial.print(' ');
    Serial.print(rToC(r), 2); Serial.print(F("   "));
  }
  Serial.println();
}
