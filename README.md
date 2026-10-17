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
| `tests/NN_name/` | Standalone bench test sketches, run in order 00 → 09 |

Libraries (Library Manager): **Adafruit ST7735 and ST7789 Library**, **Adafruit GFX Library**.
Board: Arduino Mega or Mega 2560 (CH340 clone — install the CH340 driver if the port doesn't show).
