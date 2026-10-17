#include "ui.h"
#include "config.h"
#include "control.h"

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

// ---------------- LED (PROPOSAL scheme, provisional) ----------------
struct Rgb { uint8_t r, g, b; };

static const Rgb C_OFF = {0, 0, 0};
static const Rgb C_AMBER_DIM = {40, 12, 0};
static const Rgb C_GREEN_DIM = {0, 30, 0};
static const Rgb C_GREEN = {0, 200, 0};
static const Rgb C_BLUE_DIM = {0, 0, 40};
static const Rgb C_BLUE = {0, 0, 255};
static const Rgb C_WHITE = {120, 120, 120};
static const Rgb C_YELLOW = {200, 120, 0};
static const Rgb C_RED = {255, 0, 0};

static void ledWrite(const Rgb &c) {
  analogWrite(PIN_LED_R, c.r);
  analogWrite(PIN_LED_G, c.g);
  analogWrite(PIN_LED_B, c.b);
}

static Rgb ledFor(uint32_t now) {
  if (st.state == ST_NO12V) return C_OFF;
  if (st.state == ST_SAFE) return (now / 125) % 2 ? C_RED : C_OFF;          // 4 Hz
  Rgb c;
  bool run = st.state == ST_RUN;
  if (st.state == ST_HOMING || st.state == ST_STARTING || st.state == ST_STOPPING) {
    c = (now / 500) % 2 ? C_WHITE : C_OFF;                                  // 1 Hz
  } else if (st.mode == MODE_OFF) {
    c = C_AMBER_DIM;
  } else if (st.mode == MODE_LOADCAP) {
    c = run ? C_GREEN : C_GREEN_DIM;
  } else {
    c = run ? C_BLUE : C_BLUE_DIM;
  }
  if (st.alarms && now % 1000 < 100) c = C_YELLOW;                          // alarm flash
  return c;
}

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
