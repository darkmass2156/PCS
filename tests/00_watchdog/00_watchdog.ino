// 00_watchdog — clean boot, WDT reset after a hang, and the firmware's LED heartbeat.
// The LED breathes from loop() exactly like the firmware: if loop() hangs, the LED freezes,
// then the 2 s watchdog resets the board (LED off during reset) and it comes back.
//
// Buttons:
//   BTN1 short  next mode colour (white = Mode 1, blue = Mode 2, red = Mode 3), with 3 fast green pulses
//   BTN2 short  print boot/reset stats
//   BTN2 long   simulate a hang (infinite loop, no WDT pet)  -> expect freeze, reset within ~2 s, "PASS"
// Serial: 'h' = hang, '?' = stats.
// FAIL signs: no reboot after the hang, or endless reboots (LED L flickering, boot count racing up):
// the bootloader can't handle the watchdog — tell Claude before flashing the firmware.

#include <avr/wdt.h>
#include "bench.h"

uint8_t resetFlags __attribute__((section(".noinit")));
uint8_t bootCount __attribute__((section(".noinit")));
uint8_t wdtCount __attribute__((section(".noinit")));
uint8_t magic __attribute__((section(".noinit")));
uint8_t savedMode __attribute__((section(".noinit")));

void captureResetFlags() __attribute__((naked, used, section(".init3")));
void captureResetFlags() {
  resetFlags = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

uint8_t mode = 1;
uint32_t modeChangedAt;
bool green;

void stats() {
  Serial.print(F("boot ")); Serial.print(bootCount);
  Serial.print(F(", watchdog resets ")); Serial.print(wdtCount);
  Serial.print(F(", last MCUSR 0x")); Serial.print(resetFlags, HEX);
  Serial.println(F(" (PORF=1 EXTRF=2 BORF=4 WDRF=8)"));
}

void hang() {
  Serial.println(F("hanging now: LED should freeze, reset within ~2 s"));
  Serial.flush();
  while (true) {}
}

void setup() {
  benchOutputsLow();
  if (magic != 0x5A || (resetFlags & _BV(PORF))) {
    magic = 0x5A;
    bootCount = wdtCount = 0;
    savedMode = 1;
  }
  bootCount++;
  if (resetFlags & _BV(WDRF)) wdtCount++;
  mode = savedMode >= 1 && savedMode <= 3 ? savedMode : 1;

  Serial.begin(115200);
  benchBanner(F("00_watchdog"));
  Serial.println(F("BTN1 short: next mode colour | BTN2 short: stats | BTN2 long or 'h': hang"));
  stats();
  if (resetFlags & _BV(WDRF)) Serial.println(F("WDT reset detected -> PASS (LED should breathe again)"));

  benchButtonsBegin();
  wdt_enable(WDTO_2S);
}

void loop() {
  uint32_t now = millis();
  wdt_reset();
  benchButtonsUpdate();

  if (btn1.shortPress) {
    mode = mode % 3 + 1;
    savedMode = mode;
    modeChangedAt = now;
    green = true;
    Serial.print(F("mode ")); Serial.println(mode);
  }
  if (btn2.shortPress) stats();
  if (btn2.longPress) hang();
  while (Serial.available()) {
    char c = Serial.read();
    if (c == 'h') hang();
    if (c == '?') stats();
  }

  // Same scheme as the firmware.
  if (green && now - modeChangedAt < 750) {
    bool on = (now - modeChangedAt) % 250 < 120;
    benchLed(0, on ? 255 : 0, 0);
  } else {
    green = false;
    uint8_t k = benchBreath(now, 4000);
    if (mode == 1) benchLed(k, k, k);
    else if (mode == 2) benchLed(0, 0, k);
    else benchLed(k, 0, 0);
  }
}
