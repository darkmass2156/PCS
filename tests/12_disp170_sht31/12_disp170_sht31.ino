// 12_disp170_sht31 — the 1.9" ST7789 (170x320, used landscape 320x170) and the in-case SHT31s.
// SENSORS 1: one sensor (SINGLE_ADDR; BTN2 short / serial a switches 0x44 <-> 0x45), with raw values and the
//            status register on screen. Use it to test each module on its own.
// SENSORS 2: A (0x44) and B (0x45) side by side, DP spread and T A-B.
// Live T / RH / dew point on the screen and on Serial, a night log in EEPROM, and a panel check page.
//
// Night log (EEPROM, survives power loss and resets):
//   Every LOG_INTERVAL_S the average T and RH of A and B since the last sample (4 bytes: T 0.2 °C steps over
//   -10..40.6 °C, RH 0.5 % steps). Ring of 1016 samples = 16.9 h at 60 s; when full the oldest are overwritten.
//   EEPROM 16..4095 only (the controller firmware keeps its mode/splash bytes at 0..3).
//   BTN1 long starts a NEW log (erases the old one), BTN2 long stops it. A reset or power cut while running
//   resumes logging after boot with a gap marker, so opening the serial monitor (DTR reset) is safe.
//   Serial d dumps CSV (oldest first): sample, minutes, A_T, A_RH, B_T, B_RH. Copy it into a spreadsheet.
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
//   BTN1 short  page: live readings -> log charts -> panel check (frame, RGB bars)
//   BTN2 short  SENSORS 1: switch address 0x44 <-> 0x45 (counters reset) | SENSORS 2: reset the error counters
//   BTN1 long   start a new log (erases the previous one)
//   BTN2 long   stop the log
// Serial: p page, i invert, b BGR, r reset counters, m SPI mode 0/3 + re-init, c colour flash,
//         s I2C scan, h sensor reset + heater off, a switch address (SENSORS 1),
//         l start log, x stop log, d dump log as CSV, e erase log, ? help.
// One line per read (2 s): A t rh dp st=<status> [ok/trig nack/read nack/crc/timeout] | B ... | dT dDP
// Errors name the Wire code: trig e2 = address NACK (wiring, ADDR pin, power), e3 = data NACK, e5 = timeout;
// "read nack" = no data 20 ms after the trigger. st bit 13 = heater on, bit 4 = sensor reset since last clear.
// LED: green flash per read with both valid, yellow if one missing, red if none.
// Pass: both read every 2 s with no NACK/CRC for 10+ min, same air -> T within ~0.3 °C, DP within ~1 °C,
// breathing on one sensor moves only that column.

#include <SPI.h>
#include <Wire.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "bench.h"
#include "scr_gfx.h"

#define SENSORS 2           // 1 = one sensor, 2 = A and B
#define LOG_INTERVAL_S 60   // night log: seconds per sample (1016 samples: 60 s = 16.9 h, 90 s = 25 h)
#define SINGLE_ADDR 0x44    // SENSORS 1: address at boot (0x44 ADDR to GND, 0x45 ADDR to VIN)
#define TFT_RST 48          // pin wired to the module's RES (via divider), or -1
#define ROTATION 1          // 3 = landscape upside down
const uint16_t W = 320, H = 170;

Adafruit_ST7789 tft(49, 53, TFT_RST);
bool inv = true, bgr = false;   // real ST7789 defaults (config.h: DISP_INVERT 1, DISP_BGR 0)
uint8_t page = 0;               // 0 live, 1 log, 2 panel check
uint8_t spiMode = SPI_MODE0;    // modules without a CS pin need SPI_MODE3

struct Sht {
  char name[8];
  uint8_t addr;
  bool ok;
  uint8_t err;                 // last result: 0 ok, 1..5 Wire code of the trigger, 10 read NACK, 11 CRC, 12 timeout
  uint16_t good, nackT, nackR, crc, tmo;
  uint16_t status;             // status register (0xF32D), 0xFFFF = not readable
  float t, rh, dp;
  uint16_t rawT, rawRH;        // sensor ticks as received (CRC checked)
};
#if SENSORS == 1
Sht sht[1] = {{"0x44", SINGLE_ADDR}};
#else
Sht sht[2] = {{"A  0x44", 0x44}, {"B  0x45", 0x45}};
#endif
const uint8_t NS = SENSORS;
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
  s.rawT = (b[0] << 8) | b[1];
  s.rawRH = (b[3] << 8) | b[4];
  s.t = -45.0f + 175.0f * s.rawT / 65535.0f;   // datasheet conversion
  s.rh = 100.0f * s.rawRH / 65535.0f;
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
    Serial.print(s.name); Serial.print(' ');
    if (s.ok) {
      Serial.print(s.t, 2); Serial.print(F(" C ")); Serial.print(s.rh, 1); Serial.print(F(" % DP ")); Serial.print(s.dp, 2);
      Serial.print(F(" st=")); Serial.print(s.status, HEX);
      Serial.print(F(" raw ")); Serial.print(s.rawT, HEX); Serial.print('/'); Serial.print(s.rawRH, HEX);
    } else {
      Serial.print(errText(s));
      if (s.err < 10) { Serial.print(F(" (trig e")); Serial.print(s.err); Serial.print(')'); }
    }
    Serial.print(F(" [")); Serial.print(s.good); Serial.print('/'); Serial.print(s.nackT); Serial.print('/');
    Serial.print(s.nackR); Serial.print('/'); Serial.print(s.crc); Serial.print('/'); Serial.print(s.tmo);
    Serial.print(F("] | "));
  }
  if (NS == 2 && sht[0].ok && sht[NS - 1].ok) {
    Serial.print(F("dT ")); Serial.print(sht[0].t - sht[NS - 1].t, 2);
    Serial.print(F(" dDP ")); Serial.print(fabs(sht[0].dp - sht[NS - 1].dp), 2);
    if (fabs(sht[0].dp - sht[NS - 1].dp) > 2.0f) Serial.print(F(" > 2 C ALARM"));
  }
  Serial.println();
  uint8_t n = 0;
  for (Sht &s : sht) n += s.ok;
  if (n == NS) benchLed(0, 60, 0); else if (n) benchLed(120, 70, 0); else benchLed(150, 0, 0);
}

// ---------------- night log (EEPROM) ----------------
// Header at LOG_BASE: 'P' 'L' version running interval(2) head(2) count(2). Samples from LOG_DATA, 4 bytes:
// A_T A_RH B_T B_RH. T byte = (t + 10) * 5 (0..253), RH byte = rh * 2 (0..200), 0xFF = no valid read,
// A_T = 0xFE = gap (reset / power cut while logging).
#define LOG_BASE 16
#define LOG_DATA 32
#define LOG_CAP ((4096 - LOG_DATA) / 4)
#define LOG_INVALID 0xFF
#define LOG_GAP 0xFE

struct LogState {
  bool valid, running;
  uint16_t interval, head, count;
  uint32_t lastAt;
  float sumT[2], sumRH[2];
  uint8_t n[2];
} lg;

uint16_t eeRead16(int a) { return EEPROM.read(a) | (EEPROM.read(a + 1) << 8); }
void eeWrite16(int a, uint16_t v) {
  EEPROM.update(a, v & 0xFF);
  EEPROM.update(a + 1, v >> 8);
}

void logSaveHeader() {
  EEPROM.update(LOG_BASE, 'P');
  EEPROM.update(LOG_BASE + 1, 'L');
  EEPROM.update(LOG_BASE + 2, 1);
  EEPROM.update(LOG_BASE + 3, lg.running);
  eeWrite16(LOG_BASE + 4, lg.interval);
  eeWrite16(LOG_BASE + 6, lg.head);
  eeWrite16(LOG_BASE + 8, lg.count);
}

void logClearSums() {
  for (uint8_t i = 0; i < 2; i++) lg.sumT[i] = lg.sumRH[i] = lg.n[i] = 0;
}

void logAppend(const uint8_t *rec) {
  int a = LOG_DATA + 4 * lg.head;
  for (uint8_t i = 0; i < 4; i++) EEPROM.update(a + i, rec[i]);
  lg.head = (lg.head + 1) % LOG_CAP;
  if (lg.count < LOG_CAP) lg.count++;
  eeWrite16(LOG_BASE + 6, lg.head);
  eeWrite16(LOG_BASE + 8, lg.count);
}

// Boot: pick up an existing log; if it was running, mark the gap and keep going.
void logBoot(uint32_t now) {
  lg.valid = EEPROM.read(LOG_BASE) == 'P' && EEPROM.read(LOG_BASE + 1) == 'L' && EEPROM.read(LOG_BASE + 2) == 1;
  lg.running = false;
  if (!lg.valid) {
    Serial.println(F("log: none (BTN1 long / l starts one)"));
    return;
  }
  lg.running = EEPROM.read(LOG_BASE + 3);
  lg.interval = eeRead16(LOG_BASE + 4);
  lg.head = eeRead16(LOG_BASE + 6) % LOG_CAP;
  lg.count = eeRead16(LOG_BASE + 8);
  if (lg.count > LOG_CAP || !lg.interval) {
    lg.valid = lg.running = false;
    Serial.println(F("log: header damaged, ignored"));
    return;
  }
  lg.lastAt = now;
  logClearSums();
  if (lg.running) {
    const uint8_t gap[4] = {LOG_GAP, LOG_GAP, LOG_GAP, LOG_GAP};
    logAppend(gap);
  }
  Serial.print(F("log: ")); Serial.print(lg.count); Serial.print(F(" samples, ")); Serial.print(lg.interval);
  Serial.println(lg.running ? F(" s, was running: resumed after a gap marker") : F(" s, stopped (d dumps it)"));
}

void logStart(uint32_t now) {
  lg.valid = lg.running = true;
  lg.interval = LOG_INTERVAL_S;
  lg.head = lg.count = 0;
  lg.lastAt = now;
  logClearSums();
  logSaveHeader();
  Serial.print(F("--- log started, every ")); Serial.print(lg.interval); Serial.print(F(" s, room for "));
  Serial.print(LOG_CAP * (uint32_t)lg.interval / 3600.0f, 1); Serial.println(F(" h ---"));
}

void logStop() {
  if (!lg.valid) return;
  lg.running = false;
  EEPROM.update(LOG_BASE + 3, 0);
  Serial.print(F("--- log stopped, ")); Serial.print(lg.count); Serial.println(F(" samples (d dumps them) ---"));
}

void logErase() {
  EEPROM.update(LOG_BASE, 0);
  lg.valid = lg.running = false;
  lg.count = 0;
  Serial.println(F("--- log erased ---"));
}

// After every read cycle: add the valid readings; write a sample when the interval is up. True if written.
bool logTick(uint32_t now) {
  if (!lg.running) return false;
  for (uint8_t i = 0; i < NS; i++)
    if (sht[i].ok) {
      lg.sumT[i] += sht[i].t;
      lg.sumRH[i] += sht[i].rh;
      lg.n[i]++;
    }
  if (now - lg.lastAt < lg.interval * 1000UL) return false;
  lg.lastAt += lg.interval * 1000UL;
  uint8_t rec[4] = {LOG_INVALID, LOG_INVALID, LOG_INVALID, LOG_INVALID};
  for (uint8_t i = 0; i < NS; i++)
    if (lg.n[i]) {
      float t = lg.sumT[i] / lg.n[i], rh = lg.sumRH[i] / lg.n[i];
      rec[2 * i] = constrain(lround((t + 10) * 5), 0, 253);
      rec[2 * i + 1] = constrain(lround(rh * 2), 0, 200);
    }
  logClearSums();
  logAppend(rec);
  return true;
}

// Sample i of the log, oldest first.
void logGet(uint16_t i, uint8_t *rec) {
  int a = LOG_DATA + 4 * ((lg.head + LOG_CAP - lg.count + i) % LOG_CAP);
  for (uint8_t k = 0; k < 4; k++) rec[k] = EEPROM.read(a + k);
}

float logT(uint8_t b) { return b / 5.0f - 10; }
float logRH(uint8_t b) { return b / 2.0f; }

void logDump() {
  if (!lg.valid) {
    Serial.println(F("# no log"));
    return;
  }
  Serial.print(F("# PCS night log: ")); Serial.print(lg.count); Serial.print(F(" samples, every "));
  Serial.print(lg.interval); Serial.print(F(" s, oldest first")); Serial.println(lg.running ? F(" (still running)") : F(""));
  Serial.println(F("# T in 0.2 C steps, RH in 0.5 % steps, empty = no valid read; times after a gap are relative"));
  Serial.println(F("sample,minutes,A_T,A_RH,B_T,B_RH"));
  uint32_t secs = 0;
  for (uint16_t i = 0; i < lg.count; i++) {
    uint8_t r[4];
    logGet(i, r);
    if (r[0] == LOG_GAP) {
      Serial.println(F("# gap: reset or power cut, time unknown"));
      continue;
    }
    secs += lg.interval;
    Serial.print(i); Serial.print(','); Serial.print(secs / 60.0f, 1);
    for (uint8_t k = 0; k < 4; k++) {
      Serial.print(',');
      if (r[k] != LOG_INVALID) Serial.print(k % 2 ? logRH(r[k]) : logT(r[k]), 1);
    }
    Serial.println();
  }
  Serial.println(F("# end"));
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

void drawLiveStatic2() {
  tft.fillScreen(C_BLK);
  memset(cache, 0, sizeof(cache));
  ftxt(F_LABEL, 4, 3, C_LG, C_BLK, "SHT31 A / B   -   1.9\"  320 x 170", 0);
  tft.drawFastHLine(0, 16, W, C_DG);
  tft.drawFastVLine(160, 20, 106, C_DG);
  tft.drawFastHLine(0, 129, W, C_DG);
  for (uint8_t i = 0; i < NS; i++) {
    int16_t x0 = i * 160;
    ftxt(F_VALUE, x0 + 8, 21, C_WHT, C_BLK, sht[i].name, 0);
    ftxt(F_LABEL, x0 + 8, 74, C_LG, C_BLK, "RH", 0);
    ftxt(F_LABEL, x0 + 8, 92, C_LG, C_BLK, "Dew point", 0);
  }
  ftxt(F_LABEL, 8, 136, C_LG, C_BLK, "DP spread", 0);
  ftxt(F_LABEL, 168, 136, C_LG, C_BLK, "T  A - B", 0);
}

void drawLogBadge() {   // live pages: "LOG n" next to the 1 Hz dot while logging
  char b[16];
  if (lg.running) snprintf(b, sizeof(b), "LOG %u", lg.count);
  else b[0] = 0;
  field(14, F_LABEL, 236, 3, C_HOT, b, 66, 'r');
}

void drawLive2() {
  char b[40], u[12];
  for (uint8_t i = 0; i < NS; i++) {
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
  bool both = sht[0].ok && sht[NS - 1].ok;
  float spread = fabs(sht[0].dp - sht[NS - 1].dp);
  field(10, F_VALUE, 70, 133, both && spread > 2.0f ? C_HOT : C_WHT, num(b, spread, both, 2, "\xF8" "C"), 82, 'r');
  field(11, F_VALUE, 230, 133, C_WHT, num(u, sht[0].t - sht[NS - 1].t, both, 2, "\xF8" "C"), 82, 'r');
  field(12, F_LABEL, 8, 155, both && spread > 2.0f ? C_HOT : C_DG,
        both ? (spread > 2.0f ? "spread > 2 \xF8" "C: firmware alarm" : "spread within 2 \xF8" "C") : "need both sensors", 200);
}

// SENSORS 1: one sensor, big temperature left, RH / DP / status register right, counters and raw ticks below.
void drawLiveStatic1() {
  char b[40];
  tft.fillScreen(C_BLK);
  memset(cache, 0, sizeof(cache));
  ftxt(F_LABEL, 4, 3, C_LG, C_BLK, "SHT31 single   -   1.9\"  320 x 170", 0);
  tft.drawFastHLine(0, 16, W, C_DG);
  tft.drawFastVLine(172, 20, 106, C_DG);
  tft.drawFastHLine(0, 129, W, C_DG);
  snprintf(b, sizeof(b), "SHT31  %s", sht[0].name);
  ftxt(F_VALUE, 8, 21, C_WHT, C_BLK, b, 160);
  ftxt(F_LABEL, 180, 22, C_LG, C_BLK, "RH", 0);
  ftxt(F_LABEL, 180, 58, C_LG, C_BLK, "Dew point", 0);
  ftxt(F_LABEL, 180, 94, C_LG, C_BLK, "Status register", 0);
}

void drawLive1() {
  char b[80];
  Sht &s = sht[0];
  field(0, F_BIG, 8, 48, s.ok ? C_WHT : C_DG, num(b, s.t, s.ok, 2, "\xF8"), 160);
  const char *e = errText(s);
  field(1, F_VALUE, 8, 86, e[0] == 'O' ? C_LG : C_HOT, e, 160);
  field(2, F_VALUE, 180, 36, C_WHT, num(b, s.rh, s.ok, 1, " %"), 132, 'r');
  field(3, F_VALUE, 180, 72, C_WHT, num(b, s.dp, s.ok, 2, "\xF8" "C"), 132, 'r');
  bool heat = s.status != 0xFFFF && (s.status & 0x2000);
  if (s.status == 0xFFFF) strcpy(b, "--");
  else snprintf(b, sizeof(b), "0x%04X%s", s.status, heat ? " HEAT" : "");
  field(4, F_VALUE, 180, 108, heat ? C_HOT : C_WHT, b, 132, 'r');
  uint16_t bad = s.nackT + s.nackR + s.crc + s.tmo;
  snprintf(b, sizeof(b), "ok %u   trig nack %u   read nack %u   crc %u   timeout %u", s.good, s.nackT, s.nackR, s.crc, s.tmo);
  field(5, F_LABEL, 8, 135, bad ? C_HOT : C_LG, b, 304);
  if (s.good) snprintf(b, sizeof(b), "raw  T 0x%04X   RH 0x%04X", s.rawT, s.rawRH);
  else strcpy(b, "raw  --");
  field(6, F_LABEL, 8, 153, C_DG, b, 170);
  field(7, F_LABEL, 180, 153, C_DG, "BTN2: other address", 132, 'r');
}

void drawLiveStatic() {
  if (NS == 1) drawLiveStatic1(); else drawLiveStatic2();
}

void drawLive() {
  if (NS == 1) drawLive1(); else drawLive2();
  drawLogBadge();
}

void switchAddr() {   // SENSORS 1: 0x44 <-> 0x45, counters and readings reset
  uint8_t a = sht[0].addr == 0x44 ? 0x45 : 0x44;
  memset(&sht[0], 0, sizeof(Sht));
  sht[0].addr = a;
  snprintf(sht[0].name, sizeof(sht[0].name), "0x%02X", a);
  Serial.print(F("--- sensor 0x")); Serial.print(a, HEX); Serial.println(F(" ---"));
  sensorInit(sht[0]);
  if (page == 0) drawLiveStatic();
}

// Log page: state line, then T and RH over the whole log (A white / red, B blue / orange), auto-scaled.
#define CH_X 34
#define CH_W 282
void logChart(int16_t y0, int16_t h, bool rh, const char *unit) {
  tft.fillRect(0, y0, W, h + 1, C_BLK);
  tft.drawRect(CH_X - 1, y0, CH_W + 2, h + 1, C_DG);
  float lo = 1e9, hi = -1e9;
  for (uint16_t i = 0; i < lg.count; i++) {
    uint8_t r[4];
    logGet(i, r);
    if (r[0] == LOG_GAP) continue;
    for (uint8_t k = rh; k < 4; k += 2)
      if (r[k] != LOG_INVALID) {
        float v = rh ? logRH(r[k]) : logT(r[k]);
        if (v < lo) lo = v;
        if (v > hi) hi = v;
      }
  }
  char b[12];
  if (lo > hi) {
    ftxt(F_LABEL, CH_X + 8, y0 + h / 2 - 5, C_DG, C_BLK, lg.valid ? "no data yet" : "no log", 0);
    ftxt(F_LABEL, 2, y0 + 2, C_LG, C_BLK, unit, 0);
    return;
  }
  float span = rh ? 5 : 2;   // minimum scale
  if (hi - lo < span) {
    float m = (hi + lo) / 2;
    lo = m - span / 2;
    hi = m + span / 2;
  }
  ftxt(F_LABEL, 2, y0 + 2, C_LG, C_BLK, unit, 0);
  ftxt(F_LABEL, 2, y0 + 14, C_WHT, C_BLK, dtostrf(hi, 0, rh ? 0 : 1, b), 30);
  ftxt(F_LABEL, 2, y0 + h - 11, C_WHT, C_BLK, dtostrf(lo, 0, rh ? 0 : 1, b), 30);
  static const uint16_t COL[2][2] = {{C_WHT, 0x867F}, {C_HOT, 0xFD20}};   // [rh][sensor]: A white / red, B blue / orange
  int16_t prev[2] = {-1, -1};
  uint16_t cols = lg.count < CH_W ? lg.count : CH_W;
  for (uint16_t x = 0; x < cols; x++) {
    uint8_t r[4];
    logGet(lg.count <= CH_W ? x : (uint32_t)x * lg.count / CH_W, r);
    if (r[0] == LOG_GAP) {
      tft.drawFastVLine(CH_X + x, y0 + 1, h - 1, C_MAR);   // gap marker
      prev[0] = prev[1] = -1;
      continue;
    }
    for (uint8_t s = 0; s < 2; s++) {
      uint8_t v = r[2 * s + rh];
      if (v == LOG_INVALID) {
        prev[s] = -1;
        continue;
      }
      float f = rh ? logRH(v) : logT(v);
      int16_t y = y0 + h - 1 - lround((f - lo) * (h - 3) / (hi - lo));
      int16_t a = prev[s] < 0 ? y : prev[s];
      tft.drawFastVLine(CH_X + x, a < y ? a : y, abs(y - a) + 1, COL[rh][s]);
      prev[s] = y;
    }
  }
}

void drawLogHeader() {
  char b[64];
  if (!lg.valid) strcpy(b, "no log   -   BTN1 long starts one");
  else {
    uint32_t mins = (uint32_t)lg.count * lg.interval / 60;
    snprintf(b, sizeof(b), "%s   %u:%02u h   %u / %u   every %u s", lg.running ? "LOGGING" : "STOPPED",
             (unsigned)(mins / 60), (unsigned)(mins % 60), lg.count, (unsigned)LOG_CAP, lg.interval);
  }
  field(0, F_LABEL, 4, 3, lg.running ? C_HOT : C_LG, b, 300);
}

void drawLogPage() {
  tft.fillScreen(C_BLK);
  memset(cache, 0, sizeof(cache));
  drawLogHeader();
  tft.drawFastHLine(0, 16, W, C_DG);
  logChart(20, 66, false, "T \xF8" "C");
  logChart(90, 66, true, "RH %");
  ftxt(F_LABEL, 4, 158, C_DG, C_BLK, "A white/red  B blue/orange   BTN1 long new   BTN2 long stop", 0);
}

void drawCheck() {
  tft.fillScreen(C_BLK);
  memset(cache, 0, sizeof(cache));
  tft.drawRect(0, 0, W, H, ST77XX_WHITE);   // must touch all four edges (offset check)
  const uint16_t c[3] = {ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE};
  for (uint8_t i = 0; i < 3; i++) tft.fillRect(10, 28 + i * 30, W - 20, 24, c[i]);
  ftxt(F_VALUE, 10, 6, C_WHT, C_BLK, "320 x 170   bars: RED GREEN BLUE", 0);
  char b[40];
  snprintf(b, sizeof(b), "invert %u   BGR %u   (serial i / b)", inv, bgr);
  ftxt(F_LABEL, 10, 124, C_LG, C_BLK, b, 0);
  ftxt(F_LABEL, 10, 142, C_LG, C_BLK, "background black, frame on all 4 edges", 0);
}

void redraw() {
  if (page == 0) {
    drawLiveStatic();
    drawLive();
  } else if (page == 1) drawLogPage();
  else drawCheck();
}

void help() {
  Serial.println(NS == 1 ? F("BTN1 short: page | BTN2 short: other address | BTN1 long: new log | BTN2 long: stop log")
                         : F("BTN1 short: page | BTN2 short: reset counters | BTN1 long: new log | BTN2 long: stop log"));
  Serial.println(F("serial: p page, i invert, b BGR, r reset counters, m SPI mode 0/3, c colour flash,"));
  Serial.println(F("        s I2C scan, h sensor reset + heater off, a switch address (SENSORS 1),"));
  Serial.println(F("        l start log, x stop log, d dump log (CSV), e erase log, ? help"));
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
  if (NS == 1) snprintf(sht[0].name, sizeof(sht[0].name), "0x%02X", sht[0].addr);
  for (Sht &s : sht) sensorInit(s);
  logBoot(millis());
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
  if (btn2.shortPress) c = NS == 1 ? 'a' : 'r';
  if (btn1.longPress) c = 'l';
  if (btn2.longPress) c = 'x';
  if (!c && Serial.available()) c = Serial.read();
  if (c == 'p') page = (page + 1) % 3;
  if (c == 'l') logStart(now);
  if (c == 'x') logStop();
  if (c == 'e') logErase();
  if (c == 'd') logDump();
  if ((c == 'l' || c == 'x' || c == 'e') && page == 1) drawLogPage();
  if (c == 'i') { inv = !inv; tft.invertDisplay(inv); }
  if (c == 'b') { bgr = !bgr; applyOrientation(); }
  if (c == 'r') {
    for (Sht &s : sht) s.good = s.nackT = s.nackR = s.crc = s.tmo = 0;
    Serial.println(F("--- counters reset ---"));
  }
  if (c == '?') help();
  if (c == 's') scan();
  if (c == 'a' && NS == 1) switchAddr();
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
    if (stage < NS) {
      trigger(sht[stage]);
      stepAt = millis();
      stage++;
    } else {
      stage = 0;
      printRead();
      ledAt = now;
      if (logTick(now) && page == 1) drawLogPage();   // new sample: redraw the charts (~0.3 s)
    }
  }
  if (ledAt && now - ledAt >= 40) {
    benchLed(0, 0, 0);
    ledAt = 0;
  }

  if (page == 1) drawLogHeader();
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
