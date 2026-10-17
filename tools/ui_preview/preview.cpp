// Host preview of the real display code (splash.h + screens.cpp) on a 280x240 framebuffer.
// Build/run: ./run.sh  -> docs/display/preview_*.png. The font is a look-alike; layout and colours are exact.
#include "Adafruit_ST7789.h"
#include "splash.h"
#include "screens.h"
#include <string>

static Adafruit_ST7789 tft;
static std::string OUT;

static UiData base() {
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

static void settle(Screens &s, const UiData &d, uint32_t &now, int calls = 400) {
  for (int i = 0; i < calls; i++) s.update(now += 5, d);
}

int main(int argc, char **argv) {
  OUT = argc > 1 ? argv[1] : ".";
  srand(7);
  // ---- boot sequence for each penguin ----
  for (int k = 0; k < 3; k++) {
    BootSplash sp;
    tft.fillScreen(0);
    sp.begin(&tft, 280, 240, k, "0.4.0", false, 0);
    struct { uint32_t t; const char *name; } shots[] = {{1150, "noot"}, {2400, "glitch1"}, {2560, "glitch2"},
                                                         {2800, "glitch3"}, {3800, "rog_sweep"}, {4600, "rog"}};
    int si = 0;
    for (uint32_t now = 0; now < 9000; now += 10) {
      sp.update(now);
      if (si < 6 && now >= shots[si].t) {
        tft.savePPM((OUT + "/boot" + std::to_string(k) + "_" + shots[si].name + ".ppm").c_str());
        si++;
      }
      if (!sp.active()) { printf("boot %d done at %u ms, %ld windows\n", k, now, tft.windows); tft.windows = 0; break; }
    }
  }
  // ---- dashboards ----
  static Screens s;   // static like the firmware's
  s.begin(&tft, 280, 240, "v0.4.0");
  uint32_t now = 100000;
  UiData d = base();
  for (int i = 0; i < 230; i++) {   // chart history: valve cooling towards T_set
    UiData h = d;
    h.valve = 15.2f + 3.3f * expf(-i / 50.0f) + 0.25f * sinf(i / 6.0f);
    h.dp = 11.2f + 0.3f * sinf(i / 37.0f);
    s.sample(h);
  }
  for (int m = 1; m <= 3; m++) {
    d = base(); d.mode = m;
    settle(s, d, now);
    tft.savePPM((OUT + "/mode" + std::to_string(m) + ".ppm").c_str());
    // value change: everything should update in place
    UiData e = d; e.rad = -4.7f; e.loop = 21.0f; e.valve = 14.1f; e.dew = 1.6f; e.fanPct = 0; e.mixing = false;
    e.extOpen = false; e.potW = 1012; e.alarms = 0;
    settle(s, e, now);
    tft.savePPM((OUT + "/mode" + std::to_string(m) + "_changed.ppm").c_str());
    UiData f = d; f.faults = 0x100 | 0x002; f.state = "SAFE"; f.p2On = false;
    settle(s, f, now);
    tft.savePPM((OUT + "/mode" + std::to_string(m) + "_fault.ppm").c_str());
    UiData g = d; g.no12v = true; g.state = "NO12V";
    settle(s, g, now);
    tft.savePPM((OUT + "/mode" + std::to_string(m) + "_no12v.ppm").c_str());
  }
  return 0;
}
