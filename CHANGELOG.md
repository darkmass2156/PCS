# Changelog

Newest first. Each entry says which revision it belongs to:
**FW** = firmware (`firmware/controller/version.h`), **LOGIC** = control logic rev,
**HW** = hardware rev, **SCH** = schematic rev, **DOC** = documentation only.
Change notes with the reasoning live in `docs/changes/`. `CLAUDE.md` is the authoritative spec.

## Revision index
| Date | FW | Logic | HW | Schematic | Git tag |
|---|---|---|---|---|---|
| 2026-10-17 | — | 2 | 4b + CS mod + SHT30 | rev11 | `logic-r2` |
| 2026-10-17 | — | 1 | 4b + CS mod | rev10 | `logic-r1` |

---

## 2026-10-17 — LOGIC rev 2, SCH rev11, HW SHT30 add-on
Change note: [`docs/changes/logic_rev2.md`](docs/changes/logic_rev2.md)

### Changed
- Modes rewritten as three setpoint modes: Mode 1 = branch off (EEPROM default),
  Mode 2 = load cap T_set = max(T_room + δ, DP + 4), Mode 3 = DP + 4 (BTN2 long).
- Sleep = V2 parked ≥ 10 min; wake = T1 > T_set + 1 °C.
- Feasibility guard: V2 opens only if T_out < T1 − 5 °C; forced bypass if T3 > T1 − 1 °C for 2 min.
- Backstop is now T1 < DP + 2 °C (was T_GPU < T_set − 1).
- DP = max of all valid sources (SHT31 A, B + SHT30 room); alarm on > 2 °C spread.
- Stop order: V2 park → P2 off → V1 off 10 s later.
- V2 position model: separate open/close travel times + backlash, re-zero at bypass.
- P2 fixed at 80 %.
- Naming P1/P2, V1/V2, T1/T2/T3 used everywhere.
- A5 NTC moved from the GPU block to the loop return (T1); SHT30 added on soft I2C D34/D36.

### Removed
- Rev 1 Mode 1 enable/disable thresholds, Mode 2 idle/load margins and the open load-trigger item,
  GPU NTC backstop, separate override mode.

### Spec interpretations (ASSUMED, need confirmation)
- T_out = T3 after the rad has been stagnant `T_OUT_SETTLE_MIN`; held while flowing; a forced bypass
  invalidates it until re-settled.
- Wake also requires feasibility (otherwise it would sleep again 10 min later).
- Mode 3 with < 2 valid DP sources runs the Mode 2 setpoint + alarm.
- Faults still drop V1 and P2 immediately (the 10 s V1 delay is for normal stops only).
- T1 backstop and T2 trip armed only while P2 runs; T1 < DP + 2 with P2 off is an alarm.
- Fault latching: faults latch, any button press clears those whose cause is gone.

### Docs
- `CLAUDE.md` rewritten for logic rev 2; revision tracking section added.
- Schematic rev11 added (`docs/schematics/`); rev10 kept.

## 2026-10-17 — LOGIC rev 1 baseline (tag `logic-r1`)
- Imported the rev 1 spec (`CLAUDE.md`), BOM rev 4b and schematic rev10 as received.
