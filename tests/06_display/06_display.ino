// 06_display — ST7789 at 2 MHz, CS D49, DC D53, RST -1 (power-on RC), via the C4 cable.
// Libraries: "Adafruit ST7735 and ST7789 Library", "Adafruit GFX Library".
// Set DISPLAY_VARIANT to the panel fitted, upload, check:
//   - a 1 px white frame touches all four edges (offset OK; if shifted ~20 rows, report it)
//   - bars top to bottom read RED, GREEN, BLUE (else 'b' toggles BGR)
//   - background black, text white (else 'i' toggles invert)
//   - the counter ticks every second for 10+ minutes, including after 'x' (re-init without clear)
// Buttons: BTN1 short = invert, BTN2 short = BGR, BTN1 long = re-init (no clear), BTN2 long = full redraw.
// Serial: i invert, b BGR, x re-init (2 MHz sequence, no clear), f full redraw.
// For layout work use tests/11_ui_dev; this sketch only proves the panel, cable and colours.

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "bench.h"

#define DISPLAY_VARIANT 1   // 1 = MSP1691 (landscape 280x240), 2 = MSP1901 (landscape 320x170)
#define ROTATION 1          // 3 = landscape upside down

#if DISPLAY_VARIANT == 1
const uint16_t IW = 240, IH = 280, W = 280, H = 240;   // init with the native portrait size
#else
const uint16_t IW = 170, IH = 320, W = 320, H = 170;
#endif

Adafruit_ST7789 tft(49, 53, -1);
bool inv = true, bgr = false;

void initSeq() {
  uint8_t colmod = 0x55;
  tft.sendCommand(ST77XX_SWRESET); delay(150);
  tft.sendCommand(ST77XX_SLPOUT);  delay(120);
  tft.sendCommand(ST77XX_COLMOD, &colmod, 1); delay(10);
  tft.sendCommand(ST77XX_NORON);   delay(10);
  tft.sendCommand(ST77XX_DISPON);  delay(10);
  tft.setRotation(ROTATION);
  const uint8_t madctl[4] = {0xC0, 0xA0, 0x00, 0x60};
  if (bgr) { uint8_t m = madctl[ROTATION & 3] | 0x08; tft.sendCommand(ST77XX_MADCTL, &m, 1); }
  tft.invertDisplay(inv);
}

void drawPattern() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawRect(0, 0, W, H, ST77XX_WHITE);
  uint16_t bh = 30;
  tft.fillRect(10, 30, W - 20, bh, ST77XX_RED);
  tft.fillRect(10, 30 + bh + 5, W - 20, bh, ST77XX_GREEN);
  tft.fillRect(10, 30 + 2 * (bh + 5), W - 20, bh, ST77XX_BLUE);
  tft.setTextSize(2);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setCursor(10, 10);
  tft.print(W); tft.print('x'); tft.print(H);
  tft.setCursor(10, 136);
  tft.print(F("R G B ^"));
  tft.setTextSize(1);
  tft.setCursor(10, 156);
  tft.print(F("inv ")); tft.print(inv); tft.print(F(" bgr ")); tft.print(bgr);
}

void setup() {
  benchOutputsLow();
  Serial.begin(115200);
  benchBanner(F("06_display"));
  Serial.print(F("variant ")); Serial.println(DISPLAY_VARIANT);
  benchButtonsBegin();
  uint32_t t0 = millis();
  tft.init(IW, IH);
  tft.setSPISpeed(2000000);
  initSeq();
  drawPattern();
  Serial.print(F("init + pattern ms ")); Serial.println(millis() - t0);
}

void loop() {
  benchButtonsUpdate();
  char b = 0;
  if (btn1.shortPress) b = 'i';
  if (btn2.shortPress) b = 'b';
  if (btn1.longPress) b = 'x';
  if (btn2.longPress) b = 'f';
  while (b || Serial.available()) {
    char c = b ? b : Serial.read();
    b = 0;
    if (c == 'i') { inv = !inv; tft.invertDisplay(inv); drawPattern(); }
    if (c == 'b') { bgr = !bgr; initSeq(); drawPattern(); }
    if (c == 'x') { uint32_t t = millis(); initSeq(); Serial.print(F("re-init ms ")); Serial.println(millis() - t); }
    if (c == 'f') drawPattern();
    if (c == 'i' || c == 'b') { Serial.print(F("inv ")); Serial.print(inv); Serial.print(F(" bgr ")); Serial.println(bgr); }
  }
  static uint32_t last;
  if (millis() - last < 1000) return;
  last = millis();
  uint32_t t = micros();
  tft.setTextSize(3);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  tft.setCursor(W - 90, 6);
  tft.print(last / 1000);
  t = micros() - t;
  Serial.print(F("tick ")); Serial.print(last / 1000); Serial.print(F(" draw us ")); Serial.println(t);
}
