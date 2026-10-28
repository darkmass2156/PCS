// lite170 — "lite" case display: the 1.9" ST7789 (170x320, landscape 320x170) on the C4 cable and the two
// in-case SHT31s (A 0x44, B 0x45 on D20/D21). No pumps, valves, fans or NTCs: it only shows.
// Boot: Pingu -> crash -> ROG (splash.h, same as the controller), then the three dashboard screens re-laid out
// for 320x170 (screens170.*). Case values (T, RH, dew point) are live from the SHT31s (highest valid of A/B,
// per value, as in the firmware); everything that needs the rest of the hardware shows "--".
// Libraries: "Adafruit ST7735 and ST7789 Library", "Adafruit GFX Library".
//
// Buttons (as the controller): BTN1 = Mode 1 standard, BTN2 short = Mode 2 sport, BTN2 long = Mode 3 engineer.
// The mode is kept in EEPROM 0/1 exactly like the firmware, the splash bytes 2/3 too.
// LED: mode colour breathing (white / blue / red), three green pulses on a mode change, yellow flash on an
// alarm (SHT invalid, DP spread > 2 °C), fast red blink when no SHT31 is valid.
// Serial 115200: one line per read (2 s): A t rh dp | B ... | DP max; ? = help.
// Shared files (canonical in firmware/controller/): fonts.h, scr_gfx.*, scr_data.h, splash.h, boot_sprite.h,
// rog_logo.h; bench.h from tests/common (outputs LOW, buttons). screens170.* are canonical here.

#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "bench.h"
#include "splash.h"
#include "screens170.h"

#define LITE_VERSION "lite 1.1"
#define TFT_RST 48          // RES wire to D48 (bench / Vlad's cable), -1 if RES only has the RC at the display
#define DISP_SPI_HZ 4000000UL   // 1.5 m C4 cable with 330/660 dividers: OK up to 6 MHz
#define DISP_INVERT 1
#define DISP_BGR 0
#define DISP_ROTATION 1     // 3 = landscape upside down
#define BOOT_SPLASH 1
#define DISP_REINIT_MS 300000UL
#define SHT_RH_OFFSET_A  0.75f  // RH calibration (% RH, as config.h): A 0x44 read low
#define SHT_RH_OFFSET_B -0.75f  // B 0x45 read high
const uint16_t W = 320, H = 170;

Adafruit_ST7789 tft(49, 53, TFT_RST);
BootSplash splash;
Screens170 screens;
UiData ui;
uint8_t mode = 1;

// ---------------- SHT31 A / B ----------------
struct Sht {
  const char *name;
  uint8_t addr;
  float rhOfs;            // calibration offset, % RH
  bool valid;             // after 3 good reads; lost after 3 bad ones (as the firmware)
  uint8_t bad, good;
  float t, rh, dp;
};
Sht sht[2] = {{"A", 0x44, SHT_RH_OFFSET_A, false, 0, 2}, {"B", 0x45, SHT_RH_OFFSET_B, false, 0, 2}};   // good = 2: the first good read validates at boot

uint8_t crc8(const uint8_t *d, uint8_t n) {
  uint8_t crc = 0xFF;
  for (uint8_t i = 0; i < n; i++) {
    crc ^= d[i];
    for (uint8_t b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : (crc << 1);
  }
  return crc;
}

float dewPoint(float t, float rh) {
  const float a = 17.62f, b = 243.12f;
  float g = log(rh / 100.0f) + a * t / (b + t);
  return b * g / (a - g);
}

bool command(uint8_t addr, uint16_t cmd) {
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)cmd);
  return Wire.endTransmission() == 0;
}

bool triggered[2];

void fetch(Sht &s, bool trig) {
  uint8_t b[6];
  bool ok = trig && Wire.requestFrom(s.addr, (uint8_t)6) == 6;
  if (ok) {
    for (uint8_t i = 0; i < 6; i++) b[i] = Wire.read();
    ok = crc8(b, 2) == b[2] && crc8(b + 3, 2) == b[5];
  }
  float t = 0, rh = 0;
  if (ok) {
    t = -45.0f + 175.0f * (uint16_t)((b[0] << 8) | b[1]) / 65535.0f;
    rh = constrain(100.0f * (uint16_t)((b[3] << 8) | b[4]) / 65535.0f + s.rhOfs, 0.0f, 100.0f);
    ok = t > -40 && t < 85 && rh > 0.5f && rh <= 100;   // plausible
  }
  if (ok) {
    s.t = t;
    s.rh = rh;
    s.dp = dewPoint(t, rh);
    s.bad = 0;
    if (s.good < 3 && ++s.good == 3) s.valid = true;
  } else {
    s.good = 0;
    if (s.bad < 3 && ++s.bad == 3) s.valid = false;
  }
}

void printRead() {
  for (Sht &s : sht) {
    Serial.print(s.name); Serial.print(' ');
    if (s.valid) {
      Serial.print(s.t, 2); Serial.print(F(" C ")); Serial.print(s.rh, 1); Serial.print(F(" % DP ")); Serial.print(s.dp, 2);
    } else Serial.print(F("invalid"));
    if (s.bad) { Serial.print(F(" (bad ")); Serial.print(s.bad); Serial.print(')'); }
    Serial.print(F(" | "));
  }
  Serial.print(F("DP max ")); Serial.println(ui.dp, 2);
}

// ---------------- dashboard data ----------------
float maxValid(float a, bool aOk, float b, bool bOk) {
  if (aOk && bOk) return a > b ? a : b;
  return aOk ? a : bOk ? b : NAN;
}

void buildUi() {
  bool a = sht[0].valid, b = sht[1].valid;
  ui.mode = mode;
  ui.state = "LITE";
  ui.no12v = ui.sim = false;
  ui.rad = ui.loop = ui.valve = NAN;
  ui.caseT = maxValid(sht[0].t, a, sht[1].t, b);
  ui.caseRH = maxValid(sht[0].rh, a, sht[1].rh, b);
  ui.caseDP = maxValid(sht[0].dp, a, sht[1].dp, b);
  ui.roomT = ui.roomRH = ui.roomDP = NAN;   // SHT30 not fitted in the lite build
  ui.dpA = a ? sht[0].dp : NAN;
  ui.dpB = b ? sht[1].dp : NAN;
  ui.dpR = NAN;
  ui.dp = ui.caseDP;
  ui.dpValid = a + b;
  ui.tset = ui.tout = NAN;
  ui.toutOk = ui.potOk = false;
  ui.potK = ui.potW = NAN;
  ui.extOpen = ui.homing = ui.mixing = ui.v2homed = ui.p2On = false;
  ui.v2deg = NAN;
  ui.fanPct = ui.p2Pct = 0;
  ui.rpmF1 = ui.rpmF2 = ui.rpmP2 = 0;
  ui.dew = NAN;                              // needs T2
  ui.faults = ui.dpValid ? 0 : (1 << 3);     // F_DP_NONE
  ui.alarms = (a ? 0 : 1 << 3) | (b ? 0 : 1 << 4) | (a && b && fabs(sht[0].dp - sht[1].dp) > 2.0f ? 1 << 6 : 0);
  ui.fuses = 0;
}

// ---------------- display ----------------
struct InitStep { uint8_t cmd; uint8_t arg; bool hasArg; uint8_t waitMs; };
const InitStep initSeq[] = {
  {ST77XX_SWRESET, 0, false, 150},
  {ST77XX_SLPOUT, 0, false, 120},
  {ST77XX_COLMOD, 0x55, true, 10},
  {ST77XX_NORON, 0, false, 10},
  {ST77XX_DISPON, 0, false, 10},
};
const uint8_t INIT_STEPS = sizeof(initSeq) / sizeof(initSeq[0]);
int8_t initStep = -1;   // -1 = not re-initialising
uint32_t initWaitUntil, lastReinitAt;

void applyOrientation() {
  tft.setRotation(DISP_ROTATION);
  const uint8_t madctl[4] = {0xC0, 0xA0, 0x00, 0x60};
  uint8_t m = madctl[DISP_ROTATION & 3] | (DISP_BGR ? 0x08 : 0);
  tft.sendCommand(ST77XX_MADCTL, &m, 1);
  tft.invertDisplay(DISP_INVERT);
}

void sendStep(uint8_t i) {
  uint8_t a = initSeq[i].arg;
  if (initSeq[i].hasArg) tft.sendCommand(initSeq[i].cmd, &a, 1);
  else tft.sendCommand(initSeq[i].cmd);
}

// Random penguin per boot, never the same twice in a row (EEPROM 2 = boot counter, 3 = last shown).
uint8_t pickSplash() {
  uint8_t boots = EEPROM.read(2) + 1;
  EEPROM.update(2, boots);
  uint32_t seed = micros() ^ ((uint32_t)boots * 2654435761UL);
  for (uint8_t i = 0; i < 16; i++) seed = (seed << 1) ^ (analogRead(A3 + (i % 3)) & 1);
  randomSeed(seed);
  uint8_t last = EEPROM.read(3);
  uint8_t n = random(SPLASH_COUNT);
  if (last < SPLASH_COUNT && n == last) n = (n + 1 + random(SPLASH_COUNT - 1)) % SPLASH_COUNT;
  EEPROM.update(3, n);
  return n;
}

// ---------------- LED (as ui.cpp) ----------------
uint32_t modeChangedAt;
bool greenActive;

uint8_t breath(uint32_t now) {
  uint32_t p = now % 4000;
  uint32_t tri = p < 2000 ? p * 510UL / 4000 : (4000 - p) * 510UL / 4000;
  return (uint8_t)(tri * tri / 255);
}

void ledUpdate(uint32_t now) {
  if (greenActive) {
    uint32_t t = now - modeChangedAt;
    if (t < 750) {
      benchLed(0, t % 250 < 120 ? 255 : 0, 0);
      return;
    }
    greenActive = false;
  }
  if (ui.faults) {   // no SHT31 valid
    benchLed((now / 125) % 2 ? 255 : 0, 0, 0);
    return;
  }
  if (ui.alarms && now % 2000 < 100) {
    benchLed(200, 120, 0);
    return;
  }
  uint8_t k = breath(now);
  if (mode == 1) benchLed(k, k, k);
  else if (mode == 2) benchLed(0, 0, k);
  else benchLed(k, 0, 0);
}

void setMode(uint8_t m, uint32_t now) {
  if (m == mode) return;
  mode = m;
  EEPROM.update(0, 0xA5);   // same layout as control.cpp: magic, mode
  EEPROM.update(1, m);
  modeChangedAt = now;
  greenActive = true;
  Serial.print(F("mode ")); Serial.println(m);
}

void help() {
  Serial.println(F("BTN1: Mode 1 | BTN2 short: Mode 2 | BTN2 long: Mode 3 | serial 1 2 3, ? help"));
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  Serial.println(F("# PCS " LITE_VERSION " - 1.9in 320x170 + SHT31 A/B"));
  help();
  benchButtonsBegin();
  if (EEPROM.read(0) == 0xA5 && EEPROM.read(1) >= 1 && EEPROM.read(1) <= 3) mode = EEPROM.read(1);
  Wire.begin();
  Wire.setClock(50000);
  Wire.setWireTimeout(5000, true);
  for (Sht &s : sht) {   // known state: soft reset, heater off, clear status
    command(s.addr, 0x30A2);
    delay(2);
    command(s.addr, 0x3066);
    command(s.addr, 0x3041);
  }
  tft.init(170, 320);              // runs at the library default clock ...
  tft.setSPISpeed(DISP_SPI_HZ);    // ... so redo the init sequence at 4 MHz for the long cable
  for (uint8_t i = 0; i < INIT_STEPS; i++) {
    sendStep(i);
    delay(initSeq[i].waitMs);
  }
  applyOrientation();
  tft.fillScreen(ST77XX_BLACK);
  tft.cp437(true);
  screens.begin(&tft, LITE_VERSION);
  buildUi();
#if BOOT_SPLASH
  splash.begin(&tft, W, H, pickSplash(), LITE_VERSION, DISP_INVERT, millis());
#endif
  lastReinitAt = millis();
}

void loop() {
  uint32_t now = millis();
  benchButtonsUpdate();
  char c = 0;
  if (btn1.shortPress || btn1.longPress) c = '1';
  if (btn2.shortPress) c = '2';
  if (btn2.longPress) c = '3';
  if (!c && Serial.available()) c = Serial.read();
  if (c >= '1' && c <= '3') setMode(c - '0', now);
  if (c == '?') help();
  ledUpdate(now);

  // SHT31 read cycle, one sensor at a time: A trigger, 20 ms, A fetch + B trigger, 20 ms, B fetch. Every 2 s.
  static uint32_t cycleAt, stepAt;
  static uint8_t stage;
  if (stage == 0 && now - cycleAt >= 2000) {
    cycleAt = stepAt = now;
    triggered[0] = command(sht[0].addr, 0x2400);
    stage = 1;
  } else if (stage && millis() - stepAt >= 20) {
    fetch(sht[stage - 1], triggered[stage - 1]);
    if (stage == 1) {
      triggered[1] = command(sht[1].addr, 0x2400);
      stepAt = millis();
      stage = 2;
    } else {
      stage = 0;
      buildUi();
      printRead();
    }
  }

  // display: boot sequence, then the periodic non-blocking re-init, then the screens
  if (splash.active()) {
    if (splash.update(now)) {
      screens.redraw();
      lastReinitAt = now;
    }
    return;
  }
  if (initStep >= 0) {
    if ((int32_t)(now - initWaitUntil) < 0) return;
    if (initStep < INIT_STEPS) {
      sendStep(initStep);
      initWaitUntil = now + initSeq[initStep].waitMs;
      initStep++;
    } else {
      applyOrientation();
      initStep = -1;
      lastReinitAt = now;
    }
    return;
  }
  if (now - lastReinitAt >= DISP_REINIT_MS) {
    initStep = 0;
    initWaitUntil = now;
    return;
  }
  static uint32_t lastSec;
  if (now - lastSec >= 1000) {
    lastSec = now;
    screens.sample(ui);
  }
  ui.mode = mode;   // mode switches show at once
  screens.update(now, ui);
}
