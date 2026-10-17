// 08_solenoid — V1 2W-025-08 NC via SEN D28 (IRLB8721 low-side, HIGH = energised = open). Needs 12 V (S3).
// Commands: 1 open (auto-close after 5 min), 0 close.
// Check: audible click on both edges, flow only when open, S3 stays present (fuse holds the inrush),
// no reset/brown-out of the Mega when it switches (watch for a new "# 08_solenoid" banner).

const uint8_t SEN = 28;
bool open_;
uint32_t openedAt;

void set(bool on) {
  digitalWrite(SEN, on ? HIGH : LOW);
  open_ = on;
  openedAt = millis();
  Serial.print(on ? F("V1 OPEN") : F("V1 SHUT"));
  Serial.print(F("  S3 adc ")); Serial.println(analogRead(A2));
}

void setup() {
  const uint8_t outs[] = {22, 24, 26, 28, 2, 3, 9, 10, 11};
  for (uint8_t p : outs) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }
  Serial.begin(115200);
  Serial.println(F("# 08_solenoid  commands: 1 open, 0 close"));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '1') {
      if (analogRead(A2) < 500) Serial.println(F("S3 low: no 12 V on the V1/V2 fuse"));
      else set(true);
    }
    if (c == '0') set(false);
  }
  if (open_ && millis() - openedAt > 300000UL) { Serial.print(F("timeout: ")); set(false); }
  static uint32_t last;
  if (open_ && millis() - last >= 5000) {
    last = millis();
    Serial.print(F("open ")); Serial.print((millis() - openedAt) / 1000);
    Serial.print(F(" s, S3 adc ")); Serial.println(analogRead(A2));
  }
}
