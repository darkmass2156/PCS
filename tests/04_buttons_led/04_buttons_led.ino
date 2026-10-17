// 04_buttons_led — BTN1 (D30), BTN2 (D32) active LOW; RGB LED R D9, G D10, B D11 (common cathode).
// Each press is printed as SHORT or LONG (>= 2 s, fires while held).
// BTN1 cycles the LED colour, BTN2 short = brightness step, BTN2 long = LED off.
// Also checks for contact bounce: prints the raw edge count per press (RC + 30 ms debounce should hide it).

const uint8_t BTN[2] = {30, 32};
const char *const BNAME[2] = {"BTN1", "BTN2"};
bool down[2], raw[2], longFired[2];
uint32_t rawAt[2], downAt[2];
uint16_t edges[2];

const uint8_t COLORS[][3] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}, {255, 120, 0}};
uint8_t colorIdx, level = 3;  // brightness 1..4

void led() {
  uint8_t r = COLORS[colorIdx][0] * level / 4, g = COLORS[colorIdx][1] * level / 4, b = COLORS[colorIdx][2] * level / 4;
  analogWrite(9, r); analogWrite(10, g); analogWrite(11, b);
}

void setup() {
  const uint8_t outs[] = {22, 24, 26, 28, 2, 3, 9, 10, 11};
  for (uint8_t p : outs) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }
  pinMode(30, INPUT);
  pinMode(32, INPUT);
  Serial.begin(115200);
  Serial.println(F("# 04_buttons_led  press each button short and long"));
  Serial.println(F("LED order: red, green, blue, white, amber -> check R/G/B wiring"));
  led();
}

void loop() {
  uint32_t now = millis();
  for (uint8_t i = 0; i < 2; i++) {
    bool r = digitalRead(BTN[i]) == LOW;
    if (r != raw[i]) { raw[i] = r; rawAt[i] = now; edges[i]++; }
    if (now - rawAt[i] >= 30 && r != down[i]) {
      down[i] = r;
      if (r) { downAt[i] = now; longFired[i] = false; edges[i] = 1; }
      else if (!longFired[i]) {
        Serial.print(BNAME[i]); Serial.print(F(" SHORT ")); Serial.print(now - downAt[i]);
        Serial.print(F(" ms, edges ")); Serial.println(edges[i]);
        if (i == 0) colorIdx = (colorIdx + 1) % 5; else level = level % 4 + 1;
        led();
      }
    }
    if (down[i] && !longFired[i] && now - downAt[i] >= 2000) {
      longFired[i] = true;
      Serial.print(BNAME[i]); Serial.println(F(" LONG"));
      if (i == 1) { analogWrite(9, 0); analogWrite(10, 0); analogWrite(11, 0); }
    }
  }
}
