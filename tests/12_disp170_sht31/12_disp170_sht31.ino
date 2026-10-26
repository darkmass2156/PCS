// 12_disp170_sht31 — the 1.9" ST7789 (170x320, used landscape 320x170) and the two in-case SHT31s together.
// Live T / RH / dew point of A (0x44) and B (0x45) on the screen and on Serial, plus a panel check page.
// Libraries: "Adafruit ST7735 and ST7789 Library", "Adafruit GFX Library".
//
// Wiring (same pins as the controller, so it also runs on the finished box with the 1.9" on C4):
//   display  module SDA -> D51 (MOSI), SCL -> D52 (SCK)  (SPI, NOT the I2C pins D20/D21 despite the labels)
//            CS -> D49, DC -> D53, RES -> D48, all five through 1k/2k dividers (3.3 V logic).
//            VCC 3V3, GND, BLK open or 3V3. A floating RES keeps the panel in reset: black with backlight on.
//            Set TFT_RST -1 only if RES has its own pull-up / RC (as on the controller's C4 cable).
//   SHT31    SDA D20, SCL D21 (4.7k-10k pull-ups to 5V; most breakouts have them), VIN 5V, GND.
//            A: ADDR to GND = 0x44, B: ADDR to VIN = 0x45.
//
// Buttons:
//   BTN1 short  page: live readings <-> panel check (frame, RGB bars)
//   BTN2 short  invert on/off (colours negative -> toggle)
//   BTN1 long   BGR on/off (red and blue swapped -> toggle)
//   BTN2 long   reset the error counters
// Serial: p page, i invert, b BGR, r reset counters, m SPI mode 0/3 + re-init, c colour flash,
//         s I2C scan, h sensor reset + heater off, ? help.
// One line per read (2 s): A t rh dp st=<status> [ok/trig nack/read nack/crc/timeout] | B ... | dT dDP
// Errors name the Wire code: trig e2 = address NACK (wiring, ADDR pin, power), e3 = data NACK, e5 = timeout;
// "read nack" = no data 20 ms after the trigger. st bit 13 = heater on, bit 4 = sensor reset since last clear.
// LED: green flash per read with both valid, yellow if one missing, red if none.
// Pass: both read every 2 s with no NACK/CRC for 10+ min, same air -> T within ~0.3 °C, DP within ~1 °C,
// breathing on one sensor moves only that column.

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "bench.h"
#include "scr_gfx.h"

#define TFT_RST 48          // pin wired to the module's RES (via divider), or -1
#define ROTATION 1          // 3 = landscape upside down
const uint16_t W = 320, H = 170;

Adafruit_ST7789 tft(49, 53, TFT_RST);
bool inv = true, bgr = false;   // real ST7789 defaults (config.h: DISP_INVERT 1, DISP_BGR 0)
uint8_t page = 0;               // 0 live, 1 panel check
uint8_t spiMode = SPI_MODE0;    // modules without a CS pin need SPI_MODE3

struct Sht {
  const char *name;
  uint8_t addr;
  bool ok;
  uint8_t err;                 // last result: 0 ok, 1..5 Wire code of the trigger, 10 read NACK, 11 CRC, 12 timeout
  uint16_t good, nackT, nackR, crc, tmo;
  uint16_t status;             // status register (0xF32D), 0xFFFF = not readable
  float t, rh, dp;
};
Sht sht[2] = {{"A  0x44", 0x44}, {"B  0x45", 0x45}};
enum { E_READ = 10, E_CRC = 11, E_TMO = 12 };

// ---------------- SHT31 ----------------
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

// Sends a 16-bit command. Returns the Wire code (0 ok, 2 address NACK, 3 data NACK, 5 timeout).
uint8_t command(uint8_t addr, uint16_t cmd) {
  Wire.clearWireTimeoutFlag();
  Wire.beginTransmission(addr);
  Wire.write((uint8_t)(cmd >> 8));
  Wire.write((uint8_t)cmd);
  uint8_t e = Wire.endTransmission();
  return Wire.getWireTimeoutFlag() ? 5 : e;
}

void trigger(Sht &s) {   // single shot, high repeatability, no clock stretching
  s.err = command(s.addr, 0x2400);
  if (s.err) {
    if (s.err == 5) s.tmo++; else s.nackT++;
  }
}

// Status register; 0xFFFF if it can't be read.
uint16_t readStatus(uint8_t addr) {
  uint8_t b[3];
  if (command(addr, 0xF32D) || Wire.requestFrom(addr, (uint8_t)3) != 3) return 0xFFFF;
  for (uint8_t i = 0; i < 3; i++) b[i] = Wire.read();
  return crc8(b, 2) == b[2] ? (b[0] << 8) | b[1] : 0xFFFF;
}

void fetch(Sht &s) {
  uint8_t b[6];
  s.ok = false;
  if (s.err) return;   // trigger already failed (counted there)
  Wire.clearWireTimeoutFlag();
  if (Wire.requestFrom(s.addr, (uint8_t)6) != 6) {
    if (Wire.getWireTimeoutFlag()) { s.err = E_TMO; s.tmo++; }
    else { s.err = E_READ; s.nackR++; }
    return;
  }
  for (uint8_t i = 0; i < 6; i++) b[i] = Wire.read();
  if (crc8(b, 2) != b[2] || crc8(b + 3, 2) != b[5]) {
    s.err = E_CRC;
    s.crc++;
    return;
  }
  s.t = -45.0f + 175.0f * (uint16_t)((b[0] << 8) | b[1]) / 65535.0f;
  s.rh = 100.0f * (uint16_t)((b[3] << 8) | b[4]) / 65535.0f;
  s.dp = dewPoint(s.t, s.rh);
  s.ok = true;
  s.good++;
  s.status = readStatus(s.addr);
}

// Soft reset, heater off, clear status. A heater left on (or a sensor in a strange state) reads far too hot.
void sensorInit(Sht &s) {
  uint8_t e = command(s.addr, 0x30A2);   // soft reset
  delay(2);
  command(s.addr, 0x3066);               // heater off
  command(s.addr, 0x3041);               // clear status
  s.status = readStatus(s.addr);
  Serial.print(s.name); Serial.print(F(": reset ")); Serial.print(e ? F("NACK e") : F("ok")); if (e) Serial.print(e);
  Serial.print(F(", status 0x")); Serial.println(s.status, HEX);
}

void scan() {
  Serial.print(F("I2C scan:"));
  uint8_t n = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print(F(" 0x")); Serial.print(a, HEX);
      n++;
    }
  }
  Serial.println(n ? "" : " nothing");
}

const char *errText(const Sht &s) {
  switch (s.err) {
    case 0: return (s.status != 0xFFFF && (s.status & 0x2000)) ? "HEATER ON" : "OK";
    case 2: return "NO ACK";
    case 3: return "DATA NACK";
    case 5: case E_TMO: return "TIMEOUT";
    case E_READ: return "READ NACK";
    case E_CRC: return "CRC FAIL";
    default: return "BUS ERR";
  }
}

void printRead() {
  for (Sht &s : sht) {
    Serial.print(s.name[0]); Serial.print(' ');
    if (s.ok) {
      Serial.print(s.t, 2); Serial.print(F(" C ")); Serial.print(s.rh, 1); Serial.print(F(" % DP ")); Serial.print(s.dp, 2);
      Serial.print(F(" st=")); Serial.print(s.status, HEX);
    } else {
      Serial.print(errText(s));
      if (s.err < 10) { Serial.print(F(" (trig e")); Serial.print(s.err); Serial.print(')'); }
    }
    Serial.print(F(" [")); Serial.print(s.good); Serial.print('/'); Serial.print(s.nackT); Serial.print('/');
    Serial.print(s.nackR); Serial.print('/'); Serial.print(s.crc); Serial.print('/'); Serial.print(s.tmo);
    Serial.print(F("] | "));
  }
  if (sht[0].ok && sht[1].ok) {
    Serial.print(F("dT ")); Serial.print(sht[0].t - sht[1].t, 2);
    Serial.print(F(" dDP ")); Serial.print(fabs(sht[0].dp - sht[1].dp), 2);
    if (fabs(sht[0].dp - sht[1].dp) > 2.0f) Serial.print(F(" > 2 C ALARM"));
  }
  Serial.println();
  uint8_t n = sht[0].ok + sht[1].ok;
  if (n == 2) benchLed(0, 60, 0); else if (n) benchLed(120, 70, 0); else benchLed(150, 0, 0);
}

// ---------------- display ----------------
void applyOrientation() {   // as display.cpp: rotation, then MADCTL with BGR, then invert
  tft.setRotation(ROTATION);
  const uint8_t madctl[4] = {0xC0, 0xA0, 0x00, 0x60};
  uint8_t m = madctl[ROTATION & 3] | (bgr ? 0x08 : 0);
  tft.sendCommand(ST77XX_MADCTL, &m, 1);
  tft.invertDisplay(inv);
}

void initPanel() {
  uint8_t colmod = 0x55;
  tft.init(170, 320, spiMode);  // native portrait size; the library sets the 35-column offset (and pulses RES)
  tft.setSPISpeed(2000000);     // long cable: redo the init sequence at 2 MHz
  tft.sendCommand(ST77XX_SWRESET); delay(150);
  tft.sendCommand(ST77XX_SLPOUT);  delay(120);
  tft.sendCommand(ST77XX_COLMOD, &colmod, 1); delay(10);
  tft.sendCommand(ST77XX_NORON);   delay(10);
  tft.sendCommand(ST77XX_DISPON);  delay(10);
  applyOrientation();
  tft.cp437(true);
}

uint32_t cache[16];   // per field: hash of what is on screen (0 = not drawn)

uint32_t hs(const char *s, uint32_t h) {
  while (*s) {
    h ^= (uint8_t)*s++;
    h *= 16777619UL;
  }
  return h | 1;
}

// Draws a text field only if its content or colour changed (background filled, so no flicker).
void field(uint8_t id, const Font &f, int16_t x, int16_t y, uint16_t fg, const char *s, int16_t w, char align = 'l') {
  uint32_t k = hs(s, 2166136261UL ^ fg);
  if (cache[id] == k) return;
  cache[id] = k;
  ftxt(f, x, y, fg, C_BLK, s, w, align);
}

char *num(char *out, float v, bool ok, uint8_t dec, const char *unit) {
  if (!ok) strcpy(out, dec == 2 ? "--.--" : "--.-");
  else dtostrf(v, 0, dec, out);
  strcat(out, unit);
  return out;
}

void drawLiveStatic() {
  tft.fillScreen(C_BLK);
  memset(cache, 0, sizeof(cache));
  ftxt(F_LABEL, 4, 3, C_LG, C_BLK, "SHT31 A / B   -   1.9\"  320 x 170", 0);
  tft.drawFastHLine(0, 16, W, C_DG);
  tft.drawFastVLine(160, 20, 106, C_DG);
  tft.drawFastHLine(0, 129, W, C_DG);
  for (uint8_t i = 0; i < 2; i++) {
    int16_t x0 = i * 160;
    ftxt(F_VALUE, x0 + 8, 21, C_WHT, C_BLK, sht[i].name, 0);
    ftxt(F_LABEL, x0 + 8, 74, C_LG, C_BLK, "RH", 0);
    ftxt(F_LABEL, x0 + 8, 92, C_LG, C_BLK, "Dew point", 0);
  }
  ftxt(F_LABEL, 8, 136, C_LG, C_BLK, "DP spread", 0);
  ftxt(F_LABEL, 168, 136, C_LG, C_BLK, "T  A - B", 0);
}

void drawLive() {
  char b[40], u[12];
  for (uint8_t i = 0; i < 2; i++) {
    Sht &s = sht[i];
    int16_t x0 = i * 160;
    field(i * 5 + 0, F_BIG, x0 + 8, 44, s.ok ? C_WHT : C_DG, num(b, s.t, s.ok, 2, "\xF8"), 144);
    field(i * 5 + 1, F_VALUE, x0 + 70, 71, C_WHT, num(b, s.rh, s.ok, 1, " %"), 82, 'r');
    field(i * 5 + 2, F_VALUE, x0 + 70, 89, C_WHT, num(b, s.dp, s.ok, 2, "\xF8" "C"), 82, 'r');
    uint16_t bad = s.nackT + s.nackR + s.crc + s.tmo;
    snprintf(b, sizeof(b), "ok %u  nack %u/%u  crc %u", s.good, s.nackT, s.nackR + s.tmo, s.crc);   // trig/read
    field(i * 5 + 3, F_LABEL, x0 + 8, 111, bad ? C_HOT : C_LG, b, 146);
    const char *e = errText(s);
    field(i * 5 + 4, F_LABEL, x0 + 82, 24, e[0] == 'O' ? C_LG : C_HOT, e, 70, 'r');
  }
  bool both = sht[0].ok && sht[1].ok;
  float spread = fabs(sht[0].dp - sht[1].dp);
  field(10, F_VALUE, 70, 133, both && spread > 2.0f ? C_HOT : C_WHT, num(b, spread, both, 2, "\xF8" "C"), 82, 'r');
  field(11, F_VALUE, 230, 133, C_WHT, num(u, sht[0].t - sht[1].t, both, 2, "\xF8" "C"), 82, 'r');
  field(12, F_LABEL, 8, 155, both && spread > 2.0f ? C_HOT : C_DG,
        both ? (spread > 2.0f ? "spread > 2 \xF8" "C: firmware alarm" : "spread within 2 \xF8" "C") : "need both sensors", 200);
}

void drawCheck() {
  tft.fillScreen(C_BLK);
  memset(cache, 0, sizeof(cache));
  tft.drawRect(0, 0, W, H, ST77XX_WHITE);   // must touch all four edges (offset check)
  const uint16_t c[3] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE};
  for (uint8_t i = 0; i < 3; i++) tft.fillRect(10, 28 + i * 30, W - 20, 24, c[i]);
  ftxt(F_VALUE, 10, 6, C_WHT, C_BLK, "320 x 170   bars: RED GREEN BLUE", 0);
  char b[40];
  snprintf(b, sizeof(b), "invert %u   BGR %u   (BTN2 / BTN1 long)", inv, bgr);
  ftxt(F_LABEL, 10, 124, C_LG, C_BLK, b, 0);
  ftxt(F_LABEL, 10, 142, C_LG, C_BLK, "background black, frame on all 4 edges", 0);
}

void redraw() {
  if (page == 0) {
    drawLiveStatic();
    drawLive();
  } else drawCheck();
}

void help() {
  Serial.println(F("BTN1 short: page | BTN2 short: invert | BTN1 long: BGR | BTN2 long: reset counters"));
  Serial.println(F("serial: p page, i invert, b BGR, r reset counters, m SPI mode 0/3, c colour flash,"));
  Serial.println(F("        s I2C scan, h sensor reset + heater off, ? help"));
  Serial.println(F("read line: [ok/trig nack/read nack/crc/timeout]"));
  Serial.print(F("pins: CS 49, DC 53, MOSI 51, SCK 52, RES ")); Serial.print(TFT_RST);
  Serial.print(F(" | SPI mode ")); Serial.println(spiMode == SPI_MODE0 ? 0 : 3);
}

// Full-screen colours one after another, ~0.4 s each: if this stays black, the panel isn't receiving at all.
void colourFlash() {
  const uint16_t c[4] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_WHITE};
  for (uint8_t i = 0; i < 4; i++) {
    tft.fillScreen(c[i]);
    delay(400);   // test only, outside the read cycle
  }
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("12_disp170_sht31"));
  help();
  benchButtonsBegin();
  Wire.begin();
  Wire.setClock(50000);
  Wire.setWireTimeout(5000, true);
  scan();
  for (Sht &s : sht) sensorInit(s);
  gfx = &tft;
  uint32_t t0 = millis();
  initPanel();
  colourFlash();   // first sign of life: R, G, B, white
  redraw();
  Serial.print(F("panel init + first draw ms ")); Serial.println(millis() - t0);
}

void loop() {
  uint32_t now = millis();
  benchButtonsUpdate();
  char c = 0;
  if (btn1.shortPress) c = 'p';
  if (btn2.shortPress) c = 'i';
  if (btn1.longPress) c = 'b';
  if (btn2.longPress) c = 'r';
  if (!c && Serial.available()) c = Serial.read();
  if (c == 'p') page ^= 1;
  if (c == 'i') { inv = !inv; tft.invertDisplay(inv); }
  if (c == 'b') { bgr = !bgr; applyOrientation(); }
  if (c == 'r') {
    for (Sht &s : sht) s.good = s.nackT = s.nackR = s.crc = s.tmo = 0;
    Serial.println(F("--- counters reset ---"));
  }
  if (c == '?') help();
  if (c == 's') scan();
  if (c == 'h') for (Sht &s : sht) sensorInit(s);
  if (c == 'm') {
    spiMode = spiMode == SPI_MODE0 ? SPI_MODE3 : SPI_MODE0;
    Serial.print(F("SPI mode ")); Serial.println(spiMode == SPI_MODE0 ? 0 : 3);
    initPanel();
    colourFlash();
    redraw();
  }
  if (c == 'c') {
    colourFlash();
    redraw();
  }
  if (c == 'i' || c == 'b') { Serial.print(F("invert ")); Serial.print(inv); Serial.print(F(" BGR ")); Serial.println(bgr); }
  if (c == 'p' || c == 'i' || c == 'b') redraw();

  // read cycle without delay(), one sensor at a time: A trigger, 20 ms, A fetch + B trigger, 20 ms, B fetch.
  // Every 2 s. Sequential so a sensor that drags the bus can't take the other one's read with it.
  static uint32_t cycleAt, stepAt, ledAt;
  static uint8_t stage;   // 0 idle, 1 A measuring, 2 B measuring
  if (stage == 0 && now - cycleAt >= 2000) {
    cycleAt = stepAt = now;
    trigger(sht[0]);
    stage = 1;
  } else if (stage && millis() - stepAt >= 20) {
    fetch(sht[stage - 1]);
    if (stage == 1) {
      trigger(sht[1]);
      stepAt = millis();
      stage = 2;
    } else {
      stage = 0;
      printRead();
      ledAt = now;
    }
  }
  if (ledAt && now - ledAt >= 40) {
    benchLed(0, 0, 0);
    ledAt = 0;
  }

  if (page == 0) {
    drawLive();   // only changed fields are sent
    bool lit = (now / 1000) & 1;   // 1 Hz dot: the screen is alive
    static int8_t dot = -1;
    if (dot != lit) {
      dot = lit;
      tft.fillCircle(312, 8, 2, lit ? C_WHT : C_DG);
    }
  }
}
