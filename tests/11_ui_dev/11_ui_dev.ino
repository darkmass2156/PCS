// 11_ui_dev — design the controller screen on the real panel.
// Draws the firmware layout (same field table and drawing as firmware/controller/display.cpp) with
// fake data, lets you change the layout live over Serial, and prints a layout.h block to paste back
// into firmware/controller/layout.h. Libraries: "Adafruit ST7735 and ST7789 Library", "Adafruit GFX Library".
//
// Buttons:
//   BTN1 short  next scenario (RUN, SLEEP, NO 12V, SAFE + fault, alarm, worst-case widths)
//   BTN2 short  next page: layout / layout + grid / colours + font sizes
//   BTN1 long   animate on/off (values change every second -> exercises partial redraw like the firmware)
//   BTN2 long   non-blocking re-init (SWRESET, no clear) -> the screen must not blank
// Serial (each change redraws):
//   r<px> row height   t<px> top   x<px> value x   l<px> label x   d<px> label dy
//   z<n> value text size (1-3)     w<n> value width (chars)
//   o<field>,<row>  move a field to a row (fields: 0 mode 1 heartbeat 2 T1 3 T2 4 Tset 5 Tout 6 dTpot
//                   7 Room 8 DP 9 P2 10 V1 11 V2 12 message)
//   p  print layout.h block   m  print draw timings   ?  help

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "bench.h"

#define DISPLAY_VARIANT 1   // 1 = MSP1691 240x280, 2 = MSP1901 170x320
#define DISP_INVERT 1
#define DISP_BGR 0
#include "layout.h"         // copy of firmware/controller/layout.h

const uint16_t W = LAYOUT_W, H = LAYOUT_H;
Adafruit_ST7789 tft(49, 53, -1);

const uint16_t BG = ST77XX_BLACK, C_LABEL = 0x8410, C_VALUE = ST77XX_WHITE;
const uint16_t C_FAULT = ST77XX_RED, C_ALARM = ST77XX_YELLOW;

enum { FD_MODE, FD_HEART, FD_T1, FD_T2, FD_TSET, FD_TOUT, FD_POT, FD_ROOM, FD_DP, FD_P2, FD_V1, FD_V2, FD_MSG, FD_COUNT };
const char *const LABELS[FD_COUNT] = {NULL, NULL, "T1", "T2", "Tset", "Tout", "dTpot", "Room", "DP", "P2", "V1", "V2", NULL};
const char *const FNAMES[FD_COUNT] = {"mode", "heartbeat", "T1", "T2", "Tset", "Tout", "dTpot", "Room", "DP", "P2", "V1", "V2", "message"};

// Live layout (starts from layout.h)
int16_t top = LAYOUT_TOP, rowH = LAYOUT_ROW_H, labelX = LAYOUT_LABEL_X, labelDy = LAYOUT_LABEL_DY;
int16_t valueX = LAYOUT_VALUE_X, valueSize = LAYOUT_VALUE_SIZE, valueW = LAYOUT_VALUE_W;
uint8_t rows[FD_COUNT] = LAYOUT_ROWS;

// ---------------- fake data ----------------
struct Scenario {
  const char *name;
  const char *v[FD_COUNT];   // heartbeat is generated
  uint16_t modeColor, msgColor;
};
const Scenario SCEN[] = {
  {"M2 RUN", {"M2 RUN", "", "31.2", "24.6", "24.5", "3.4", "27.8", "22.1", "9.8 x3", "80% 3120", "OPEN", "41.3\xF8", ""},
   ST77XX_GREEN, C_VALUE},
  {"M3 SLEEP", {"M3 SLEEP", "", "27.0", "27.1", "13.8", "-6.2", "33.2", "21.4", "9.8 x3", "OFF", "SHUT", "0.0\xF8", ""},
   ST77XX_CYAN, C_VALUE},
  {"NO 12V", {"NO 12V", "", "21.3", "21.0", "--", "4.0", "17.3", "21.0", "8.9 x3", "OFF", "SHUT", "0.0\xF8?", ""},
   C_ALARM, C_VALUE},
  {"SAFE+fault", {"M2 SAFE", "", "30.4", "26.0", "24.5", "2.1", "28.3", "22.0", "9.9 x3", "OFF", "SHUT", "12.5\xF8? <", "P2 STALL"},
   C_FAULT, C_FAULT},
  {"alarm", {"M3 RUN", "", "33.0", "14.2", "13.9", "-1.5", "34.5", "21.8", "9.9 x2", "80% 3080", "OPEN", "88.0\xF8 >", "DP SPREAD"},
   ST77XX_CYAN, C_ALARM},
  {"worst case", {"M3 START", "", "-25.5", "-25.5", "-25.5", "-25.5", "55.5 1234W", "-25.5", "-25.5 x3", "100% 4800", "OPEN", "90.0\xF8? >", "M3 DEGRADED"},
   ST77XX_CYAN, C_ALARM},
};
const uint8_t N_SCEN = sizeof(SCEN) / sizeof(SCEN[0]);
uint8_t scen, page;
bool animate;
char val[FD_COUNT][24];
uint16_t color[FD_COUNT];
uint16_t dirty;
uint8_t nextField;
uint32_t fieldUs[FD_COUNT], maxPassUs;

void loadScenario() {
  const Scenario &s = SCEN[scen];
  for (uint8_t f = 0; f < FD_COUNT; f++) {
    strncpy(val[f], s.v[f], sizeof(val[f]) - 1);
    color[f] = f == FD_MODE ? s.modeColor : f == FD_MSG ? s.msgColor : f == FD_HEART ? C_LABEL : C_VALUE;
  }
  dirty = (1 << FD_COUNT) - 1;
}

// ---------------- drawing (mirrors display.cpp) ----------------
int16_t fieldX(uint8_t f) {
  if (f == FD_MODE || f == FD_MSG) return labelX;
  if (f == FD_HEART) return W - 4 - 6 * 8;
  return valueX;
}
uint8_t fieldSize(uint8_t f) { return f == FD_HEART ? 1 : valueSize; }
uint8_t fieldWidth(uint8_t f) {
  if (f == FD_HEART) return 8;
  if (f == FD_MODE) return 9;
  if (f == FD_MSG) return (W - 2 * labelX) / (6 * valueSize);
  return valueW;
}

void drawField(uint8_t f) {
  uint32_t t0 = micros();
  int16_t y = top + rows[f] * rowH;
  if (LABELS[f]) {
    tft.setTextSize(1);
    tft.setTextColor(C_LABEL, BG);
    tft.setCursor(labelX, y + labelDy);
    tft.print(LABELS[f]);
  }
  char b[32];
  uint8_t w = fieldWidth(f);
  if (w > 30) w = 30;
  snprintf(b, sizeof(b), "%-*s", w, val[f]);
  b[w] = 0;
  tft.setTextSize(fieldSize(f));
  tft.setTextColor(color[f], BG);
  tft.setCursor(fieldX(f), y);
  tft.print(b);
  fieldUs[f] = micros() - t0;
  if (fieldUs[f] > maxPassUs) maxPassUs = fieldUs[f];
  // overflow warning: text past the right edge or the bottom
  int16_t right = fieldX(f) + 6 * fieldSize(f) * w;
  if (right > W || y + 8 * fieldSize(f) > H) {
    Serial.print(F("WARN ")); Serial.print(FNAMES[f]); Serial.print(F(" overflows: right ")); Serial.print(right);
    Serial.print(F(" bottom ")); Serial.println(y + 8 * fieldSize(f));
  }
}

void drawGrid() {
  for (uint8_t r = 0; top + r * rowH < H; r++) tft.drawFastHLine(0, top + r * rowH, W, 0x2104);
  tft.drawFastVLine(valueX, 0, H, 0x0210);
  tft.drawFastVLine(labelX, 0, H, 0x2104);
  tft.drawRect(0, 0, W, H, 0x4208);
}

void drawColours() {
  tft.fillScreen(BG);
  const uint16_t cs[] = {ST77XX_WHITE, C_LABEL, ST77XX_RED, ST77XX_GREEN, ST77XX_BLUE, ST77XX_CYAN, ST77XX_YELLOW, ST77XX_ORANGE, ST77XX_MAGENTA};
  const char *const cn[] = {"white", "label", "red", "green", "blue", "cyan", "yellow", "orange", "magenta"};
  for (uint8_t i = 0; i < 9; i++) {
    tft.fillRect(4, 4 + i * 18, 30, 14, cs[i]);
    tft.setTextSize(1);
    tft.setTextColor(cs[i], BG);
    tft.setCursor(40, 8 + i * 18);
    tft.print(cn[i]);
  }
  int16_t y = 4 + 9 * 18 + 6;
  for (uint8_t s = 1; s <= 3; s++) {
    tft.setTextSize(s);
    tft.setTextColor(C_VALUE, BG);
    tft.setCursor(4, y);
    tft.print(F("-25.5\xF8 8"));
    y += 8 * s + 4;
  }
}

void fullRedraw() {
  tft.fillScreen(BG);
  if (page == 2) { drawColours(); return; }
  if (page == 1) drawGrid();
  dirty = (1 << FD_COUNT) - 1;
}

// ---------------- init / re-init ----------------
void initSeqBlocking() {
  uint8_t colmod = 0x55;
  tft.sendCommand(ST77XX_SWRESET); delay(150);
  tft.sendCommand(ST77XX_SLPOUT);  delay(120);
  tft.sendCommand(ST77XX_COLMOD, &colmod, 1); delay(10);
  tft.sendCommand(ST77XX_NORON);   delay(10);
  tft.sendCommand(ST77XX_DISPON);  delay(10);
  tft.setRotation(0);
  if (DISP_BGR) { uint8_t m = 0xC8; tft.sendCommand(ST77XX_MADCTL, &m, 1); }
  tft.invertDisplay(DISP_INVERT);
}

int8_t reinitStep = -1;
uint32_t reinitWait;
void reinitUpdate(uint32_t now) {
  if (reinitStep < 0 || (int32_t)(now - reinitWait) < 0) return;
  uint8_t colmod = 0x55;
  switch (reinitStep++) {
    case 0: tft.sendCommand(ST77XX_SWRESET); reinitWait = now + 150; break;
    case 1: tft.sendCommand(ST77XX_SLPOUT); reinitWait = now + 120; break;
    case 2: tft.sendCommand(ST77XX_COLMOD, &colmod, 1); reinitWait = now + 10; break;
    case 3: tft.sendCommand(ST77XX_NORON); reinitWait = now + 10; break;
    case 4: tft.sendCommand(ST77XX_DISPON); reinitWait = now + 10; break;
    default:
      tft.setRotation(0);
      if (DISP_BGR) { uint8_t m = 0xC8; tft.sendCommand(ST77XX_MADCTL, &m, 1); }
      tft.invertDisplay(DISP_INVERT);
      reinitStep = -1;
      dirty = (1 << FD_COUNT) - 1;
      Serial.println(F("re-init done (no clear) - did the screen blank?"));
  }
}

// ---------------- serial ----------------
void printLayout() {
  Serial.println(F("--- replace this variant's block in firmware/controller/layout.h ---"));
  Serial.print(F("#if DISPLAY_VARIANT == ")); Serial.println(DISPLAY_VARIANT);
  Serial.print(F("#define LAYOUT_W          ")); Serial.println(W);
  Serial.print(F("#define LAYOUT_H          ")); Serial.println(H);
  Serial.print(F("#define LAYOUT_TOP        ")); Serial.println(top);
  Serial.print(F("#define LAYOUT_ROW_H      ")); Serial.println(rowH);
  Serial.print(F("#define LAYOUT_LABEL_X    ")); Serial.println(labelX);
  Serial.print(F("#define LAYOUT_LABEL_DY   ")); Serial.println(labelDy);
  Serial.print(F("#define LAYOUT_VALUE_X    ")); Serial.println(valueX);
  Serial.print(F("#define LAYOUT_VALUE_SIZE ")); Serial.println(valueSize);
  Serial.print(F("#define LAYOUT_VALUE_W    ")); Serial.println(valueW);
  Serial.print(F("#define LAYOUT_ROWS       {"));
  for (uint8_t f = 0; f < FD_COUNT; f++) { Serial.print(rows[f]); if (f + 1 < FD_COUNT) Serial.print(F(", ")); }
  Serial.println('}');
  Serial.println(F("---"));
}

void printTimings() {
  Serial.print(F("draw us per field:"));
  for (uint8_t f = 0; f < FD_COUNT; f++) { Serial.print(' '); Serial.print(FNAMES[f]); Serial.print('='); Serial.print(fieldUs[f]); }
  Serial.print(F("  | worst single pass ")); Serial.print(maxPassUs); Serial.println(F(" us"));
}

void help() {
  Serial.println(F("BTN1 short: scenario | BTN2 short: page | BTN1 long: animate | BTN2 long: re-init"));
  Serial.println(F("serial: r t x l d z w <n>, o<field>,<row>, p print layout, m timings"));
}

void handleSerial() {
  if (!Serial.available()) return;
  char c = Serial.read();
  bool redraw = true;
  switch (c) {
    case 'r': rowH = Serial.parseInt(); break;
    case 't': top = Serial.parseInt(); break;
    case 'x': valueX = Serial.parseInt(); break;
    case 'l': labelX = Serial.parseInt(); break;
    case 'd': labelDy = Serial.parseInt(); break;
    case 'z': valueSize = constrain(Serial.parseInt(), 1, 3); break;
    case 'w': valueW = constrain(Serial.parseInt(), 1, 20); break;
    case 'o': {
      int f = Serial.parseInt(), r = Serial.parseInt();
      if (f >= 0 && f < FD_COUNT) rows[f] = r;
      break;
    }
    case 'p': printLayout(); redraw = false; break;
    case 'm': printTimings(); redraw = false; break;
    case '?': help(); redraw = false; break;
    default: redraw = false;
  }
  if (redraw) {
    Serial.print(F("rowH ")); Serial.print(rowH); Serial.print(F(" top ")); Serial.print(top);
    Serial.print(F(" valueX ")); Serial.print(valueX); Serial.print(F(" size ")); Serial.print(valueSize);
    Serial.print(F(" width ")); Serial.println(valueW);
    maxPassUs = 0;
    fullRedraw();
  }
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("11_ui_dev"));
  Serial.print(F("variant ")); Serial.print(DISPLAY_VARIANT); Serial.print(' '); Serial.print(W); Serial.print('x'); Serial.println(H);
  help();
  benchButtonsBegin();
  tft.init(W, H);
  tft.setSPISpeed(2000000);
  initSeqBlocking();
  tft.cp437(true);
  loadScenario();
  fullRedraw();
}

void loop() {
  uint32_t now = millis();
  benchButtonsUpdate();
  handleSerial();

  if (btn1.shortPress) {
    scen = (scen + 1) % N_SCEN;
    Serial.print(F("scenario ")); Serial.println(SCEN[scen].name);
    loadScenario();
    if (page == 2) { page = 0; fullRedraw(); }
  }
  if (btn2.shortPress) {
    page = (page + 1) % 3;
    Serial.println(page == 0 ? F("page: layout") : page == 1 ? F("page: layout + grid") : F("page: colours + fonts"));
    fullRedraw();
  }
  if (btn1.longPress) { animate = !animate; Serial.println(animate ? F("animate on") : F("animate off")); }
  if (btn2.longPress && reinitStep < 0) { reinitStep = 0; reinitWait = now; Serial.println(F("re-init...")); }

  reinitUpdate(now);
  if (reinitStep >= 0 || page == 2) return;

  static uint32_t lastSec;
  if (now - lastSec >= 1000) {
    lastSec = now;
    uint32_t s = now / 1000;
    snprintf(val[FD_HEART], sizeof(val[0]), "%02u:%02u:%02u", (unsigned)(s / 3600 % 100), (unsigned)(s / 60 % 60), (unsigned)(s % 60));
    dirty |= 1 << FD_HEART;
    if (animate) {
      float t1 = 30 + 3 * sin(s / 7.0f), t2 = 24.5f + 0.6f * sin(s / 3.0f);
      dtostrf(t1, 0, 1, val[FD_T1]);
      dtostrf(t2, 0, 1, val[FD_T2]);
      snprintf(val[FD_P2], sizeof(val[0]), "80%% %u", (unsigned)(3050 + s % 90));
      dirty |= (1 << FD_T1) | (1 << FD_T2) | (1 << FD_P2);
    }
  }

  // one field per pass, like the firmware
  for (uint8_t k = 0; k < FD_COUNT; k++) {
    uint8_t f = (nextField + k) % FD_COUNT;
    if (dirty & (1 << f)) {
      dirty &= ~(1 << f);
      drawField(f);
      nextField = f + 1;
      break;
    }
  }
}
