#include "ui.h"
#include "config.h"
#include "control.h"
#include "valve.h"

// ---------------- Buttons ----------------
// Active LOW with hardware RC; 30 ms software debounce on top.
struct Button {
  uint8_t pin;
  bool down;          // debounced
  bool raw;
  uint32_t rawAt, downAt;
  bool longFired;
};

static Button btn1 = {PIN_BTN1, false, false, 0, 0, false};
static Button btn2 = {PIN_BTN2, false, false, 0, 0, false};

enum Press : uint8_t { P_NONE, P_SHORT, P_LONG };

static Press poll(Button &b, uint32_t now) {
  bool raw = digitalRead(b.pin) == LOW;
  if (raw != b.raw) {
    b.raw = raw;
    b.rawAt = now;
  }
  if (now - b.rawAt < 30 || raw == b.down) {
    if (b.down && !b.longFired && now - b.downAt >= LONG_PRESS_MS) {
      b.longFired = true;
      return P_LONG;                 // fires while held
    }
    return P_NONE;
  }
  b.down = raw;
  if (raw) {
    b.downAt = now;
    b.longFired = false;
    return P_NONE;
  }
  return b.longFired ? P_NONE : P_SHORT;
}

// ---------------- LED ----------------
// Mode colour breathes slowly: Mode 1 white, Mode 2 blue, Mode 3 red. A mode change shows three fast
// green pulses first. Computed from millis() in loop() only, so a hung loop freezes the LED
// (and the watchdog resets 2 s later).
// ASSUMED overlays: 12V absent = off, latched fault = fast red blink, alarm = short yellow flash.
struct Rgb { uint8_t r, g, b; };

static const Rgb C_WHITE = {255, 255, 255};
static const Rgb C_BLUE = {0, 0, 255};
static const Rgb C_RED = {255, 0, 0};
static const Rgb C_GREEN = {0, 255, 0};
static const Rgb C_YELLOW = {200, 120, 0};
static const Rgb C_OFF = {0, 0, 0};

static const uint16_t BREATH_MS = 4000;
static const uint16_t GREEN_ON_MS = 120, GREEN_PERIOD_MS = 250, GREEN_COUNT = 3;

#if LED_SCHEME != 2
static bool modeSeen;
static Mode shownMode;
static uint32_t modeChangedAt;
static bool greenActive;
#endif

static void ledWrite(const Rgb &c) {
  analogWrite(PIN_LED_R, (uint16_t)c.r * LED_BRIGHTNESS_PCT / 100);
  analogWrite(PIN_LED_G, (uint16_t)c.g * LED_BRIGHTNESS_PCT / 100);
  analogWrite(PIN_LED_B, (uint16_t)c.b * LED_BRIGHTNESS_PCT / 100);
}

// Triangle wave squared (rough gamma): 0..255 over BREATH_MS.
static uint8_t breath(uint32_t now) {
  uint32_t p = now % BREATH_MS;
  uint32_t tri = p < BREATH_MS / 2 ? p * 510UL / BREATH_MS : (BREATH_MS - p) * 510UL / BREATH_MS;
  return (uint8_t)(tri * tri / 255);
}

static Rgb scale(const Rgb &c, uint8_t k) {
  return {(uint8_t)(c.r * k / 255), (uint8_t)(c.g * k / 255), (uint8_t)(c.b * k / 255)};
}

#if LED_SCHEME == 2
// PROPOSAL (rev 6b change note, not decided): solid mode colours, sleep pulses, V2 moving flashes cyan,
// alarm amber pulse, fault fast red, boot / waiting for valid sensors purple. 12V absent: the mode colour.
static const Rgb C_DIMWHITE = {50, 50, 50};
static const Rgb C_CYAN = {0, 200, 200};
static const Rgb C_PURPLE = {150, 0, 200};

static Rgb ledFor(uint32_t now) {
  if (now < BOOT_GRACE_MS || st.dpValid == 0) return C_PURPLE;            // boot / waiting for sensors
  if (st.state == ST_SAFE) return (now / 125) % 2 ? C_RED : C_OFF;          // fault latched: 4 Hz
  if (valveBusy() && now % 500 < 120) return C_CYAN;                         // V2 moving
  if (st.alarms) return scale(C_YELLOW, breath(now));                       // amber pulse
  const Rgb &c = st.mode == MODE_OFF ? C_DIMWHITE : st.mode == MODE_LOADCAP ? C_GREEN : C_BLUE;
  if (st.mode != MODE_OFF && st.state == ST_IDLE) return scale(c, breath(now));   // sleep
  return c;
}
#else
static Rgb ledFor(uint32_t now) {
  if (!modeSeen) {
    modeSeen = true;
    shownMode = st.mode;           // no green pulses for the mode restored from EEPROM
  } else if (st.mode != shownMode) {
    shownMode = st.mode;
    modeChangedAt = now;
    greenActive = true;
  }

  if (greenActive) {               // shown even with 12V absent, as press feedback
    uint32_t t = now - modeChangedAt;
    if (t < (uint32_t)GREEN_PERIOD_MS * GREEN_COUNT) return t % GREEN_PERIOD_MS < GREEN_ON_MS ? C_GREEN : C_OFF;
    greenActive = false;
  }
  if (st.state == ST_NO12V) return C_OFF;
  if (st.state == ST_SAFE) return (now / 125) % 2 ? C_RED : C_OFF;          // 4 Hz, unlike Mode 3's slow red
  const Rgb &c = st.mode == MODE_OFF ? C_WHITE : st.mode == MODE_LOADCAP ? C_BLUE : C_RED;
  return scale(c, breath(now));
}
#endif

void uiInit() {
  uint32_t now = millis();
  btn1.rawAt = btn2.rawAt = now;
  ledWrite(C_OFF);
}

void uiUpdate(uint32_t now) {
  Press p1 = poll(btn1, now);
  Press p2 = poll(btn2, now);
  if (p1 != P_NONE) controlButton(MODE_OFF);
  if (p2 == P_SHORT) controlButton(MODE_LOADCAP);
  if (p2 == P_LONG) controlButton(MODE_MAX);
  ledWrite(ledFor(now));
}
