# Controller — changes for firmware (hardware rev 6b, logic rev 2)

- **Date:** 2026-11-01
- **Applies to:** controller firmware on the rev 6b boards (PW power + LG logic, 22×27 holes).
- **Supersedes:** the pin map and display notes for hardware rev 4b.
- **Unchanged:** the control logic (modes, setpoints, feasibility and safety layer) stays as specified in logic rev 2 / `CLAUDE.md` → Control.

---

## 1. Hardware summary (what the code runs on)

| Item | Rev 4b | Rev 6b |
|---|---|---|
| Boards | 2 × 60×80, two 34-pin ribbons | 2 × 60×80: **PW** (fuses, FETs, field terminals) + **LG** (all logic) |
| PW ↔ LG | 34-pin ribbon | **16 soldered wires**. PW strip col 22 rows A–P ↔ LG row A. GND on wires 8–9. |
| LG ↔ Mega | 34-pin ribbon | 34-pin ribbon (kept). **Red stripe = pin 34.** Split into Dupont housings H1–H8. |
| Display RES | local 10k + 100 nF | **wired to Mega D47** through the C5 cable (pin 3) |
| Display dividers | 1k / 2k | **330 Ω / 680 Ω** (SCK, MOSI, DC, CS, RES) |
| Display BLK | → VCC | **not connected** (backlight always on) |
| Room sensor | not on board | **SHT30** on its own header, soft I2C |
| Mega power | USB | USB from the **PC** (mandatory — see §8) |

No change to the switching topology:
- **High-side P-FETs (IRF4905):** valve open, valve close, pump.
- **Low-side N-FET (IRLB8721):** solenoid.

---

## 2. Mega pin map (final, rev 6b)

| Mega pin | Signal | Direction | Notes |
|---|---|---|---|
| A0 | S1 | in | Pump fuse sense |
| A1 | S2 | in | Fan fuse sense |
| A2 | S3 | in | Solenoid + valve fuse sense |
| A3 | T3 | in | NTC, outdoor XE 240 |
| A4 | T2 | in | NTC, V2 outlet |
| A5 | T1 | in | NTC, loop return (via C5) |
| D2 | FAN_PWM | out | Both outdoor fans, 25 kHz |
| D3 | PUMP_PWM | out | P2 PWM, Timer3, 25 kHz |
| D9 | LED_R | out | PWM |
| D10 | LED_G | out | PWM |
| D11 | LED_B | out | PWM |
| D12 | PUMP_TACH | in | |
| D18 | TACH1 | in | Fan 1, interrupt |
| D19 | TACH2 | in | Fan 2, interrupt |
| D20 / D21 | SDA / SCL | i/o | `Wire`: SHT31 ×2 |
| D22 | VO | out | V2 open |
| D24 | VC | out | V2 close |
| D26 | PEN | out | P2 power enable |
| D28 | SEN | out | V1 solenoid |
| D30 | BTN1 | in | Active LOW |
| D32 | BTN2 | in | Active LOW |
| D34 / D36 | SDA2 / SCL2 | i/o | **Soft I2C: SHT30 room sensor** |
| **D47** | **TFT RES** | out | **New, required** |
| D49 | TFT CS | out | |
| D51 | TFT MOSI | out | Hardware SPI |
| D52 | TFT SCK | out | Hardware SPI |
| D53 | TFT DC | out | |

D50 (MISO) is unused and not wired. Keep D53 as OUTPUT; it is also the SPI SS pin.

---

## 3. Display (ST7789, 1.9" 170×320)

```cpp
#define TFT_CS   49
#define TFT_DC   53
#define TFT_RST  47          // MUST be 47 — RES has a 680 Ω to GND on LG; -1 leaves the panel in reset (backlight only)
#define DISP_SPI_HZ 4000000UL // Mega steps are 8/4/2/1 MHz; 7.5 MHz rounded down to 4 MHz anyway — state it explicitly
#define DISP_REINIT_MS 300000UL // full re-init + redraw every 5 min (self-heal on the 1.5 m run)

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);
// tft.init(170, 320); tft.setSPISpeed(DISP_SPI_HZ);
```

**Library init speed:** `tft.init()` always runs at the library default (8 MHz on AVR), and `setSPISpeed()` only takes effect after it. For init margin over the cable, set `SPI_DEFAULT_FREQ` to `4000000` in `Adafruit_ST77xx.h`, or keep the periodic re-init.

**Verified behaviour:**
- **Bench and box, Mega on PC USB:** clean image.
- **Mega on Mac USB:** garbage pixels, because the Mac and the PC sit on different grounds (§8). This is not a code issue.

**Redraw policy:** partial updates only, at about 1–2 Hz. Each redraw blocks the loop while SPI runs, so keep valve timing and tach counting off the redraw path.

---

## 4. Sensors

- **SHT31 ×2 (`Wire`, D20/D21):**
  - Sensor A is at 0x44 (ADDR → GND). Sensor B is at 0x45 (ADDR → VIN).
  - Run the bus at 50 kHz.
  - Use `Wire.setWireTimeout(25000, true)`.
  - Verify the CRC on every read; reject the reading on a CRC fail.
  - Add bus recovery (9 SCL pulses) after a timeout.
- **AL / ALERT pins:** left unconnected on all SHT3x. 5V on AL shorts the output and heats the sensor; that caused the bad readings in the bench test.
- **SHT30 room sensor:**
  - Soft I2C on D34 (SDA2) and D36 (SCL2), at 0x44 on its own bus.
  - 4.7k pull-ups to 5V on LG.
  - Use the same CRC check.
- **NTC T1 / T2 / T3:**
  - 10k 1 % pull-up to 5V, NTC to GND, 100 nF filter.
  - The ADC reads the pull-up/NTC node.
  - Treat a reading near 0 or near 1023 as an open or shorted sensor, and mark that value invalid.
- **Fuse sense S1–S3:**
  - 47k/20k divider from the fused branch, 100 nF filter.
  - V_ADC = V_branch × 20/67, so 12.0V reads as 3.58V, about 733 counts at 5V Vref.
  - A blown fuse reads about 0V.

---

## 5. Outputs (polarity)

| Pin | HIGH means |
|---|---|
| D22 VO | V2 drives open (BC547 → IRF4905 high-side on) |
| D24 VC | V2 drives close |
| D26 PEN | P2 powered |
| D28 SEN | V1 solenoid energised, valve open |

- **Boot:** all four outputs start LOW, which is also their hardware default (base and gate pull-downs).
- **Interlock:** never set VO and VC HIGH at the same time.

---

## 6. Buttons and status LED

**Buttons:** D30 and D32 are active LOW, with a 10k pull-up, 1k series resistor and 100 nF on LG. Use `INPUT` (not `INPUT_PULLUP`) and a software debounce of about 30 ms.

**LED:** common-cathode RGB, HIGH = on.
- R on D9 through 220 Ω.
- G on D10 through 150 Ω.
- B on D11 through 150 Ω.

**Open item:** the colour/state table isn't defined yet. Red pulsing was seen on the bench, most likely from invalid or missing sensors driving the safe state. Proposed table:

| State | Colour |
|---|---|
| Mode 1, branch off | dim white, solid |
| Mode 2 | green, solid |
| Mode 3 | blue, solid |
| Sleep (V2 parked at bypass) | slow pulse in the mode's colour |
| V2 moving | cyan flash |
| Alarm (DP spread > 2 °C, a sensor invalid) | amber pulse |
| Fault latched / safe state | fast red blink |
| Boot / waiting for valid sensors | purple |

Firmware should print the active alarm or fault reason on Serial at boot and on every change, so the LED colour can be matched to a cause.

**Test sketch for LED wiring:** cycle D9, D10, D11 in turn and print which pin is active.

---

## 7. Hardware notes (no code impact)

- **Valve flyback diodes:** SB560 fitted instead of 1N5819 on the V2 outputs. Fine electrically, with the band (cathode) on the switched side.
- **LG wiring:** all LG wires are 24 AWG.
- **PW wiring:** power runs are 20/22 AWG; the bare 12V and GND buses are 20 AWG or a solid solder fill.

---

## 8. Power and grounding (affects testing)

- In the build, the Mega is powered from the **PC USB (C6)**. That puts the Mega, PW (12V from the PC PSU) and the in-case parts on one ground.
- **Programming or debugging from the Mac** creates a ground loop through the signal grounds and corrupts the display SPI. To avoid it:
  - run the Mac on battery, or
  - use a USB isolator (ADuM3160), or
  - only plug the Mac in to upload.

---

## 9. Bench checks still open

1. LED colour test (§6); then define the status table in `CLAUDE.md`.
2. Display at 4 MHz, PC-powered, soaked for a few hours under real load with the pump, fans and valve switching. Drop to 2 MHz on any glitch.
3. Serial log of the sensor validity state, to confirm why the LED pulses red.
4. Carried over from logic rev 2: δ calibration, series-flow check, V2 travel times and backlash, SHT30 vs SHT31 comparison, K_POT.
