#pragma once
// Shared helpers for the bench sketches. CANONICAL copy: tests/common/bench.h.
// Arduino IDE can't include across sketch folders, so every test folder holds an identical copy.
// After editing this file run:  for d in tests/[0-9]*/; do cp tests/common/bench.h "$d"; done
#include <Arduino.h>

#define BENCH_VERSION "1.4"

// Safety rule 1 for every sketch: all switched outputs LOW before anything else.
inline void benchOutputsLow() {
  const uint8_t outs[] = {22, 24, 26, 28, 2, 3, 9, 10, 11};   // VO VC PEN SEN FAN PUMP LED R G B
  for (uint8_t i = 0; i < sizeof(outs); i++) {
    digitalWrite(outs[i], LOW);
    pinMode(outs[i], OUTPUT);
  }
}

inline void benchBanner(const __FlashStringHelper *name) {
  Serial.print(F("# "));
  Serial.print(name);
  Serial.print(F("  (bench " BENCH_VERSION ")"));
  Serial.println();
}

// ---------------- Buttons ----------------
// BTN1 D30, BTN2 D32, active LOW. INPUT_PULLUP so a bare breadboard button to GND works;
// harmless with the real panel's 10k pull-up + RC. 30 ms debounce.
// After update(): pressed / released are edges, shortPress fires on release (< 2 s),
// longPress fires once while held at 2 s.
struct BenchButton {
  uint8_t pin;
  bool raw, down, longFired;
  uint32_t rawAt, downAt;
  bool pressed, released, shortPress, longPress;

  void begin(uint8_t p) {
    pin = p;
    pinMode(pin, INPUT_PULLUP);
    raw = down = longFired = false;
    rawAt = downAt = millis();
  }

  void update(uint32_t now) {
    pressed = released = shortPress = longPress = false;
    bool r = digitalRead(pin) == LOW;
    if (r != raw) {
      raw = r;
      rawAt = now;
    }
    if (now - rawAt >= 30 && r != down) {
      down = r;
      if (down) {
        pressed = true;
        downAt = now;
        longFired = false;
      } else {
        released = true;
        if (!longFired) shortPress = true;
      }
    }
    if (down && !longFired && now - downAt >= 2000) {
      longFired = true;
      longPress = true;
    }
  }

  uint32_t heldMs(uint32_t now) const { return down ? now - downAt : 0; }

  // The current press was used on its `pressed` edge: suppress its shortPress/longPress.
  void consume() { longFired = true; }
};

BenchButton btn1, btn2;

inline void benchButtonsBegin() {
  btn1.begin(30);
  btn2.begin(32);
}

inline void benchButtonsUpdate() {
  uint32_t now = millis();
  btn1.update(now);
  btn2.update(now);
}

// ---------------- LED ----------------
inline void benchLed(uint8_t r, uint8_t g, uint8_t b) {
  analogWrite(9, r);
  analogWrite(10, g);
  analogWrite(11, b);
}

// 0..255 breathing level (triangle squared), period in ms. Call from loop() only:
// if loop() hangs, the LED freezes — that is the point.
inline uint8_t benchBreath(uint32_t now, uint16_t periodMs) {
  uint32_t p = now % periodMs;
  uint32_t tri = p < periodMs / 2u ? p * 510UL / periodMs : (periodMs - p) * 510UL / periodMs;
  return (uint8_t)(tri * tri / 255);
}

// Short confirmation blink (blocking, only for bench sketches).
inline void benchBlink(uint8_t r, uint8_t g, uint8_t b, uint8_t times) {
  for (uint8_t i = 0; i < times; i++) {
    benchLed(r, g, b);
    delay(80);
    benchLed(0, 0, 0);
    delay(80);
  }
}
