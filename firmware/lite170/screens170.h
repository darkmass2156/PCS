#pragma once
// Dashboard screens for the 1.9" ST7789 in landscape (320x170), one per mode, same design as screens.* (280x240):
//   Mode 1 standard  - black/white, big temperatures, case/room rows, four tiles, distance to dew
//   Mode 2 sport     - graphite, slanted numbers and segment bars, dew shift lights in the header
//   Mode 3 engineer  - grey, P&ID, raw table (incl. case/room), sweeping T2/Tset/DP chart
// Non-blocking: update() does one small step (a clear strip, a static chunk or one changed widget).
// Canonical in firmware/lite170/ (the only user so far; the controller's variant 2 can take it over).
#include "scr_data.h"
#include "scr_gfx.h"

#define CHART170_W 310

class Screens170 {
 public:
  void begin(Adafruit_ST7789 *t, const char *version);
  void update(uint32_t now, const UiData &d);   // switches screen when d.mode changes
  void sample(const UiData &d);                 // call at 1 Hz: chart history (kept in every mode)
  void redraw() { mode = 0; }                   // screen was overwritten (e.g. splash): rebuild it fully

 private:
  enum Phase : uint8_t { CLEAR, STATIC, CHART, LIVE };
  static const uint16_t W = 320, H = 170;
  uint8_t mode = 0;
  Phase phase = CLEAR;
  uint16_t step;
  uint8_t next;
  uint32_t now;
  uint32_t cache[24];
  uint8_t barPrev[5];
  uint8_t stripKind;      // what the bottom strip shows now: 0 nothing, 1 normal, 2 no 12V, 3 fault
  const char *ver;
  // chart: ring buffer aligned with the columns, (v + 10) * 5 -> 0..250 (0.2 °C, -10..40 °C)
  uint8_t hist[3][CHART170_W];
  uint32_t samples;
  uint32_t drawnSamples;

  uint16_t bg() const;
  bool staticStep(uint16_t i);
  uint8_t widgetCount() const;
  uint32_t widget(uint8_t i, const UiData &d, bool draw);
  void chartColumn(uint16_t c);
  uint32_t heartbeat(int16_t x, int16_t y, uint16_t on, uint16_t off, bool draw);
  uint32_t strip1(const UiData &d, bool draw);
  uint32_t strip2(const UiData &d, bool draw);
  uint32_t slantBar(uint8_t slot, int16_t x, int16_t y, int16_t w, int16_t h, uint8_t nseg, uint8_t lit,
                    uint8_t redFrom, uint8_t redTo, bool tipRed, bool draw);
  const char *faultText(const UiData &d, uint8_t &count) const;
};
