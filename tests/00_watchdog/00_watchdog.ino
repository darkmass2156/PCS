// 00_watchdog — clean boot + WDT reset check. Run this before anything that enables the WDT.
// Expected: "boot 1" ... pets for 10 s ... stops petting ... board resets within ~2 s ...
// "boot 2, WDT reset detected -> PASS". If instead the board hangs or reset-loops
// (LED L flashing fast, no new output), the bootloader can't handle WDT: tell Claude.

#include <avr/wdt.h>

uint8_t resetFlags __attribute__((section(".noinit")));
uint8_t bootCount __attribute__((section(".noinit")));
uint8_t magic __attribute__((section(".noinit")));

void captureResetFlags() __attribute__((naked, used, section(".init3")));
void captureResetFlags() {
  resetFlags = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

void setup() {
  const uint8_t outs[] = {22, 24, 26, 28, 2, 3, 9, 10, 11};
  for (uint8_t p : outs) { digitalWrite(p, LOW); pinMode(p, OUTPUT); }

  if (magic != 0x5A || (resetFlags & _BV(PORF))) { magic = 0x5A; bootCount = 0; }
  bootCount++;

  Serial.begin(115200);
  Serial.println(F("# 00_watchdog"));
  Serial.print(F("boot ")); Serial.print(bootCount);
  Serial.print(F(", MCUSR 0x")); Serial.print(resetFlags, HEX);
  Serial.print(F(" (PORF=1 EXTRF=2 BORF=4 WDRF=8)"));
  Serial.println();

  if (resetFlags & _BV(WDRF)) {
    Serial.println(F("WDT reset detected -> PASS. WDT stays off now."));
    magic = 0;
    while (true) {}
  }

  Serial.println(F("WDT on (2 s), petting for 10 s..."));
  wdt_enable(WDTO_2S);
  uint32_t t0 = millis();
  while (millis() - t0 < 10000) {
    wdt_reset();
    if ((millis() - t0) % 1000 == 0) { Serial.print('.'); delay(1); }
  }
  Serial.println(F("\nstopped petting, expect a reset within ~2 s"));
  uint32_t t1 = millis();
  while (true) {
    Serial.print(F("alive ")); Serial.println(millis() - t1);
    delay(250);
  }
}

void loop() {}
