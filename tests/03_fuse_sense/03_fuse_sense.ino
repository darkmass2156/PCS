// 03_fuse_sense — S1 (A0, P2 fuse), S2 (A1, fans), S3 (A2, V1+V2). 47k/20k dividers, 12 V ≈ 3.58 V ≈ 733 adc.
// Run three ways and paste each: 12V present, 12V absent (PC off / bench supply off), one fuse pulled.
// Thresholds: present >= 500, absent <= 250, in between = hold previous.

const uint8_t PINS[3] = {A0, A1, A2};
const char *const NAMES[3] = {"S1 P2", "S2 fan", "S3 valve"};
bool present[3];

void setup() {
  const uint8_t outs[] = {22, 24, 26, 28, 2, 3, 9, 10, 11};
  for (uint8_t p : outs) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }
  Serial.begin(115200);
  Serial.println(F("# 03_fuse_sense  adc / volts at pin / volts at fuse / state"));
}

void loop() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < 3; i++) {
    uint16_t a = analogRead(PINS[i]);
    if (a >= 500) present[i] = true;
    else if (a <= 250) present[i] = false;
    float v = a * 5.0f / 1023.0f;
    Serial.print(NAMES[i]); Serial.print(' ');
    Serial.print(a); Serial.print(' ');
    Serial.print(v, 2); Serial.print(F("V "));
    Serial.print(v * 67.0f / 20.0f, 1); Serial.print(F("V "));
    Serial.print(present[i] ? F("OK   ") : F("--   "));
    n += present[i];
  }
  if (n == 0) Serial.println(F("=> 12V ABSENT (PC off, no alarm)"));
  else if (n == 3) Serial.println(F("=> 12V present, all fuses OK"));
  else {
    Serial.print(F("=> FUSE:"));
    if (!present[0]) Serial.print(F(" S1->fault"));
    if (!present[1]) Serial.print(F(" S2->alarm"));
    if (!present[2]) Serial.print(F(" S3->fault"));
    Serial.println();
  }
  delay(1000);
}
