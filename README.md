# PCS
Home of the Penguin Cooling System for subambient custom water loop.

Arduino Mega 2560 controller for the outdoor free-cooling branch of a watercooled Corsair 570X.

| Path | What |
|---|---|
| `CLAUDE.md` | Authoritative spec: hardware, pin map, control logic, safety, tunables |
| `CHANGELOG.md` | Revision index and change history |
| `docs/changes/` | Change notes (why a logic/hardware revision changed) |
| `docs/schematics/` | Loop schematics, all revisions |
| `docs/build_component_list_rev4b.xlsx` | BOM ("Rev 4b BOM" sheet = controller parts) |
| `firmware/controller/` | Controller firmware (Arduino IDE 2, open `controller.ino`) |
| `firmware/lite170/` | Lite case display: 1.9" 320x170 + SHT31 A/B, boot sequence and the three mode screens |
| `tests/NN_name/` | Standalone bench sketches (BTN1/BTN2 driven): tests 00 → 09, `10_v2_calibration`, `11_ui_dev`, `12_disp170_sht31` |
| `tests/common/bench.h` | Shared bench helpers (copied into each sketch folder) |
| `tools/sprites/` | Generators for the boot sprites (`make_pingu.py` → `boot_sprite.h`, `make_rog.py` → `rog_logo.h`) |
| `tools/fonts/make_fonts.py` | Generates the anti-aliased fonts (`fonts.h`) |
| `tools/ui_preview/` | Host render of the real display code → `docs/display/preview_*.png` (`./run.sh`) |

Libraries (Library Manager): **Adafruit ST7735 and ST7789 Library**, **Adafruit GFX Library**.
Board: Arduino Mega or Mega 2560 (CH340 clone — install the CH340 driver if the port doesn't show).
