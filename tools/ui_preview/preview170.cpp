// Host preview of firmware/lite170 (splash.h at 320x170 + screens170) -> docs/display/preview170_*.png.
// Build/run: ./run.sh (builds with -DFB_W=320 -DFB_H=170).
#include "Adafruit_ST7789.h"
#include "splash.h"
#include "screens170.h"
#include <string>

static Adafruit_ST7789 tft;
static std::string OUT;

static UiData base() {   // full controller data, to check every field fits
  UiData d = {};
  d.mode = 1; d.state = "RUN";
  d.rad = 12.3f; d.loop = 18.5f; d.valve = 15.3f;
  d.caseT = 25.1f; d.caseRH = 42.1f; d.caseDP = 11.2f;
  d.roomT = 22.3f; d.roomRH = 38.5f; d.roomDP = 8.1f;
  d.dp = 11.2f; d.dpA = 11.2f; d.dpB = 10.9f; d.dpR = 8.1f; d.dpValid = 3;
  d.tset = 15.2f; d.tout = 4.0f; d.toutOk = true;
  d.potOk = true; d.potK = 14.5f; d.potW = 25.0f * 14.5f;
  d.extOpen = true; d.mixing = true; d.v2deg = 41.3f; d.v2homed = true;
  d.fanPct = 45; d.rpmF1 = 1410; d.rpmF2 = 1395;
  d.p2On = true; d.p2Pct = 80; d.rpmP2 = 3120;
  d.dew = d.valve - d.dp; d.fuses = 7; d.alarms = 0x008;
  return d;
}

static UiData lite() {   // what lite170 fills: case values from the two SHT31s, the rest invalid
  UiData d = {};
  d.mode = 1; d.state = "LITE";
  d.rad = d.loop = d.valve = NAN;
  d.caseT = 24.6f; d.caseRH = 47.9f; d.caseDP = 12.9f;
  d.roomT = d.roomRH = d.roomDP = NAN;
  d.dpA = 12.9f; d.dpB = 12.4f; d.dpR = NAN; d.dp = 12.9f; d.dpValid = 2;
  d.tset = d.tout = d.potK = d.potW = d.v2deg = d.dew = NAN;
  return d;
}

static void settle(Screens170 &s, const UiData &d, uint32_t &now, int calls = 400) {
  for (int i = 0; i < calls; i++) s.update(now += 5, d);
}

int main(int argc, char **argv) {
  OUT = argc > 1 ? argv[1] : ".";
  srand(11);
  for (int k = 0; k < 3; k++) {   // boot sequence, SPI time modelled at 4 MHz
    BootSplash sp;
    tft.fillScreen(0);
    sp.begin(&tft, 320, 170, k, "lite 1.0", false, 0);
    struct { uint32_t t; const char *name; } shots[] = {{900, "noot"}, {1900, "crash_a"}, {2050, "crash_b"},
                                                         {2250, "crash_c"}, {2950, "rog"}};
    int si = 0;
    double clk = 0;
    for (uint32_t now = 0; now < 15000; now = (uint32_t)clk) {
      tft.busyUs = 0;
      sp.update(now);
      clk += 1 + tft.busyUs / 2000.0;   // 4 MHz: half the 2 MHz cost model
      if (si < 5 && now >= shots[si].t) {
        tft.savePPM((OUT + "/b170_" + std::to_string(k) + "_" + shots[si].name + ".ppm").c_str());
        si++;
      }
      if (!sp.active()) { printf("170 boot %d done at %u ms\n", k, now); break; }
    }
  }
  static Screens170 s;
  s.begin(&tft, "lite 1.0");
  uint32_t now = 100000;
  for (int i = 0; i < 260; i++) {
    UiData h = base();
    h.valve = 15.2f + 3.3f * expf(-i / 50.0f) + 0.25f * sinf(i / 6.0f);
    h.dp = 11.2f + 0.3f * sinf(i / 37.0f);
    s.sample(h);
  }
  for (int m = 1; m <= 3; m++) {
    UiData d = base(); d.mode = m;
    settle(s, d, now);
    tft.savePPM((OUT + "/s170_" + std::to_string(m) + ".ppm").c_str());
    UiData f = d; f.faults = 0x100 | 0x002; f.state = "SAFE"; f.p2On = false;
    settle(s, f, now);
    tft.savePPM((OUT + "/s170_" + std::to_string(m) + "_fault.ppm").c_str());
    UiData l = lite(); l.mode = m;
    settle(s, l, now);
    tft.savePPM((OUT + "/s170_" + std::to_string(m) + "_lite.ppm").c_str());
  }
  return 0;
}
