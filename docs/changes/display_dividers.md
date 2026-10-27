# Display level shifting on the 1.5 m C4 cable — 2026-10-27

## What changed
- All ST7789 signal dividers (SCK, MOSI, DC, CS; Board 2, incl. the CS mod R22/R23) went from 1k / 2k to
  **330 Ω / 660 Ω** (series 330 Ω from the Mega pin, 660 Ω to GND at the junction; still 5 V → 3.33 V).
- `DISP_SPI_HZ` 2 MHz → **4 MHz**.

## Why
- With the finished 1.5 m cable the panel stayed black (backlight on), also at 1 MHz: the 1k/2k divider is a
  ~670 Ω source into the cable capacitance, too soft/slow for clean edges over that length.
- The 3× stiffer 330/660 divider fixed it. Bench result: works up to 6 MHz; 4 MHz chosen for margin.
- Cost: ~5 mA per line while HIGH (5 V / 990 Ω), fine for the Mega pins.

## Notes
- Faster SPI makes every redraw ~2× quicker. The boot crash animation is paced by drawing cost, so it runs a bit
  faster than the 0.8 s measured at 2 MHz.
- Bench: Vlad's breadboard has RES wired straight to D48 (12_disp170_sht31 `TFT_RST 48`). The controller keeps
  RST = -1 with the 10k + 100 nF RC at the display (C4 has no RES wire).
