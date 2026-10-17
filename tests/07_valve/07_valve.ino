// 07_valve — V2 CR-02: VO D22 (open), VC D24 (close), both high-side via IRF4905. Needs 12 V (S3 checked).
// Goals: direction (VO should move toward ALL-PORTS-OPEN, VC toward BYPASS-ONLY), t_full open,
// t_full close, backlash.
// Commands:
//   o / c      run open / close until the autostop; press 'm' the moment motion stops -> prints elapsed ms
//   O<ms>      open pulse, e.g. O1000      C<ms>  close pulse
//   s          stop (both LOW)
// Backlash: from mid-travel do O2000, then C pulses of 100 ms (C100 ...) until the stem visibly moves back;
// the summed close time before movement is the backlash.
// The interlock here mirrors the firmware: one drive() function, 500 ms dead time before reversing,
// and a 60 s safety timeout on every run.

const uint8_t VO = 22, VC = 24;
int8_t dir;            // 1 open, -1 close, 0 stopped
int8_t lastDir;
uint32_t startAt, endAt, stopAt;

void drive(int8_t d) {
  digitalWrite(VO, LOW);
  digitalWrite(VC, LOW);
  if (d > 0) digitalWrite(VO, HIGH);
  else if (d < 0) digitalWrite(VC, HIGH);
}

bool twelveVolt() { return analogRead(A2) >= 500; }

void run(int8_t d, uint32_t ms) {
  if (!twelveVolt()) { Serial.println(F("S3 low: no 12 V on the valve fuse, not moving")); return; }
  if (dir != 0) { drive(0); dir = 0; stopAt = millis(); }
  if (lastDir != 0 && d != lastDir) {
    while (millis() - stopAt < 500) {}
  }
  dir = d;
  lastDir = d;
  startAt = millis();
  endAt = startAt + ms;
  drive(d);
  Serial.print(d > 0 ? F("OPEN ") : F("CLOSE ")); Serial.print(ms); Serial.println(F(" ms"));
}

void halt(const __FlashStringHelper *why) {
  drive(0);
  if (dir != 0) {
    Serial.print(why); Serial.print(F(" after ")); Serial.print(millis() - startAt); Serial.println(F(" ms"));
  }
  dir = 0;
  stopAt = millis();
}

void setup() {
  const uint8_t outs[] = {22, 24, 26, 28, 2, 3, 9, 10, 11};
  for (uint8_t p : outs) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }
  Serial.begin(115200);
  Serial.println(F("# 07_valve  commands: o c O<ms> C<ms> m s"));
  Serial.print(F("S3 adc ")); Serial.println(analogRead(A2));
}

void loop() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'o') run(1, 60000);
    else if (c == 'c') run(-1, 60000);
    else if (c == 'O') run(1, Serial.parseInt());
    else if (c == 'C') run(-1, Serial.parseInt());
    else if (c == 'm') {
      Serial.print(F("MARK ")); Serial.print(dir > 0 ? F("open ") : dir < 0 ? F("close ") : F("(idle) "));
      Serial.print(millis() - startAt); Serial.println(F(" ms since start"));
    }
    else if (c == 's') halt(F("STOP"));
  }
  if (dir != 0 && (int32_t)(millis() - endAt) >= 0) halt(F("done"));
}
