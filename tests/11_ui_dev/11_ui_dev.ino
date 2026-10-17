// 11_ui_dev — the real dashboard screens and boot sequence on the panel, fed with fake data.
// Display rev 3: 280x240 landscape (MSP1691). Libraries: "Adafruit ST7735 and ST7789 Library",
// "Adafruit GFX Library". Copies here (canonical in firmware/controller/): scr_gfx.*, scr_data.h,
// screens.*, splash.h, boot_sprite.h, rog_logo.h. Host preview of the same code: tools/ui_preview/run.sh.
//
// Buttons:
//   BTN1 short  next mode screen (1 standard -> 2 sport -> 3 engineer)
//   BTN2 short  next scenario (normal, cold + changed values, fault, no 12V)
//   BTN1 long   animate on/off (temperatures drift every second -> partial redraws)
//   BTN2 long   replay the boot sequence (next penguin)
// One penguin per startup, then the UI. The startup penguin is picked like the firmware does it: random
// (seeded from ADC noise + micros), never the one shown last (EEPROM addr 3). Works on a fresh Wokwi run too,
// where EEPROM is blank. BTN2 long then cycles hobo -> sit -> bow.
// Serial: 1 2 3 = mode, n = next scenario, s = boot sequence, p0 p1 p2 = boot with that penguin, ? = help.

#include <SPI.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "bench.h"

#define DISP_ROTATION 1     // 3 = landscape upside down
#define DISP_TARGET_ILI9341_SIM 1   // same switch as config.h: 1 = Wokwi ILI9341, 0 = real ST7789
#if DISP_TARGET_ILI9341_SIM
#define DISP_INVERT 0
#define DISP_BGR 1
#define DISP_MIRROR_X 1
#else
#define DISP_INVERT 1
#define DISP_BGR 0
#define DISP_MIRROR_X 0
#endif

#include "splash.h"
#include "screens.h"

const uint16_t W = 280, H = 240;
Adafruit_ST7789 tft(49, 53, -1);
BootSplash splash;
Screens screens;
UiData d;
uint8_t mode = 1, scen, penguin;
bool animate;
const char *const SCEN_NAMES[] = {"normal", "cold + changed", "fault", "no 12V"};

void scenario() {
  memset(&d, 0, sizeof(d));
  d.state = "RUN";
  d.rad = 12.3f; d.loop = 18.5f; d.valve = 15.3f;
  d.caseT = 25.1f; d.caseRH = 42.1f; d.caseDP = 11.2f;
  d.roomT = 22.3f; d.roomRH = 38.5f; d.roomDP = 8.1f;
  d.dp = 11.2f; d.dpA = 11.2f; d.dpB = 10.9f; d.dpR = 8.1f; d.dpValid = 3;
  d.tset = 15.2f; d.tout = 4.0f; d.toutOk = true;
  d.potOk = true; d.potK = 14.5f; d.potW = 25.0f * 14.5f;
  d.extOpen = true; d.mixing = true; d.v2deg = 41.3f; d.v2homed = true;
  d.fanPct = 45; d.rpmF1 = 1410; d.rpmF2 = 1395;
  d.p2On = true; d.p2Pct = 80; d.rpmP2 = 3120;
  d.fuses = 7;
  if (scen == 1) {
    d.rad = -14.7f; d.loop = 21.0f; d.valve = 12.4f; d.potK = 35.7f; d.potW = 892;
    d.fanPct = 0; d.mixing = false; d.extOpen = false; d.state = "SLEEP"; d.p2On = false;
  }
  if (scen == 2) { d.faults = 0x102; d.state = "SAFE"; d.p2On = false; }
  if (scen == 3) { d.no12v = true; d.state = "NO12V"; d.p2On = false; d.extOpen = false; }
  d.dew = d.valve - d.dp;
  d.mode = mode;
}

void applyOrientation() {   // mirrors display.cpp
  tft.setRotation(DISP_ROTATION);
  const uint8_t madctl[4] = {0xC0, 0xA0, 0x00, 0x60};
  uint8_t r = DISP_ROTATION & 3;
  uint8_t m = madctl[r];
  if (DISP_MIRROR_X) m ^= (r & 1) ? 0x80 : 0x40;   // MY in landscape, MX in portrait
  if (DISP_BGR) m |= 0x08;
  tft.sendCommand(ST77XX_MADCTL, &m, 1);
  tft.invertDisplay(DISP_INVERT);
}

void initPanel() {
  uint8_t colmod = 0x55;
  tft.init(240, 280);
  tft.setSPISpeed(2000000);
  tft.sendCommand(ST77XX_SWRESET); delay(150);
  tft.sendCommand(ST77XX_SLPOUT);  delay(120);
  tft.sendCommand(ST77XX_COLMOD, &colmod, 1); delay(10);
  tft.sendCommand(ST77XX_NORON);   delay(10);
  tft.sendCommand(ST77XX_DISPON);  delay(10);
  applyOrientation();
  tft.fillScreen(ST77XX_BLACK);
  tft.cp437(true);
}

void boot() {
  tft.fillScreen(ST77XX_BLACK);
  splash.begin(&tft, W, H, penguin, "ui-dev", DISP_INVERT, millis());
  static const char *const NAMES[] = {"hobo", "sit", "bow"};
  Serial.print(F("boot sequence, penguin ")); Serial.println(NAMES[penguin % 3]);
  EEPROM.update(3, penguin);
  penguin = (penguin + 1) % SPLASH_COUNT;
}

void help() {
  Serial.println(F("BTN1 short: mode | BTN2 short: scenario | BTN1 long: animate | BTN2 long: boot sequence"));
  Serial.println(F("serial: 1 2 3 mode, n scenario, s boot, p0/p1/p2 boot with penguin, ? help"));
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("11_ui_dev"));
  help();
  benchButtonsBegin();
  uint32_t seed = micros();
  for (uint8_t i = 0; i < 32; i++) seed = (seed << 1) ^ (seed >> 31) ^ (analogRead(A3 + i % 3) & 3);  // ADC noise
  randomSeed(seed);
  uint8_t last = EEPROM.read(3);   // 0xFF on a blank EEPROM
  penguin = random(SPLASH_COUNT);
  if (last < SPLASH_COUNT && penguin == last) penguin = (penguin + 1 + random(SPLASH_COUNT - 1)) % SPLASH_COUNT;
  Serial.print(F("seed 0x")); Serial.println(seed, HEX);
  initPanel();
  screens.begin(&tft, W, H, "ui-dev");
  scenario();
  boot();
}

void loop() {
  uint32_t now = millis();
  benchButtonsUpdate();
  char c = 0;
  if (btn1.shortPress) c = 'm';
  if (btn2.shortPress) c = 'n';
  if (btn2.longPress) c = 's';
  if (btn1.longPress) { animate = !animate; Serial.println(animate ? F("animate on") : F("animate off")); }
  if (!c && Serial.available()) c = Serial.read();
  if (c == 'm') mode = mode % 3 + 1;
  if (c >= '1' && c <= '3') mode = c - '0';
  if (c == 'n') { scen = (scen + 1) % 4; Serial.print(F("scenario: ")); Serial.println(SCEN_NAMES[scen]); scenario(); }
  if (c == 's') boot();
  if (c == 'p') {   // p0 / p1 / p2
    uint32_t t = millis();
    while (!Serial.available() && millis() - t < 500) {}
    char k = Serial.read();
    if (k >= '0' && k < '0' + SPLASH_COUNT) { penguin = k - '0'; boot(); }
  }
  if (c == '?') help();
  if (c == 'm' || (c >= '1' && c <= '3')) { d.mode = mode; Serial.print(F("mode ")); Serial.println(mode); }

  if (splash.active()) {
    splash.update(now);
    return;
  }

  static uint32_t lastSec;
  if (now - lastSec >= 1000) {
    lastSec = now;
    if (animate) {
      float s = now / 1000.0f;
      d.rad = 12.3f + 4 * sin(s / 9);
      d.loop = 18.5f + 2 * sin(s / 13);
      d.valve = 15.2f + 0.6f * sin(s / 4);
      d.dew = d.valve - d.dp;
      d.potW = 25.0f * (d.loop - d.tout);
      d.rpmP2 = 3080 + (uint16_t)(now / 1000) % 80;
    }
    screens.sample(d);
  }
  screens.update(now, d);
}
