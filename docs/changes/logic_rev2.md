# Control logic rev 2 — change note

- **Date:** 2026-10-17
- **Applies to:** controller firmware on hardware rev 4b + display CS mod
- **Supersedes:** logic rev 1 (Mode 1 auto / Mode 2 pre-cool / long press = override)
- **Authoritative spec:** `CLAUDE.md` → Control. This note records what changed and why.

## Summary
The mode logic was rewritten as three setpoint-based modes on top of an unchanged safety layer:
- **Mode 1** = branch off.
- **Mode 2** = room-relative load cap with auto-sleep.
- **Mode 3** = chase dew point + 4 °C.

The GPU NTC moves to the loop return (T1), and an SHT30 is added for room temperature.

## Why rev 1 was replaced
1. **No load signal.** The GPU NTC sat on the GPU block inlet, so it read valve-outlet water and couldn't see load. That left the Mode 2 idle → load trigger without a workable input.
2. **Mode 1 never switched off in winter.** At dew point + 4 the PC always produces heat. The valve therefore never parked at bypass, and the disengage condition never fired.
3. **No feasibility guard.** With outdoors warmer than the loop, opening V2 raises T2, so the controller opens further. That is a positive-feedback runaway that heats the loop.
4. **Fixed setpoints didn't fit a warm room.** A 20 °C target sits below the idle loop temperature in a 21–22 °C room, so auto-sleep could never trigger.

## Naming (new, used everywhere)
| Name | Part | Mega |
|---|---|---|
| P1 | EK Kinetic D5, in case, fixed 90 % on the Apex | — |
| P2 | Bykski D5 + res, controller box | PEN D26, PWM D3, tach D12 |
| V1 | NC solenoid 2W-025-08 | SEN D28 |
| V2 | CR-02 3-way T-port (LEFT outdoor, RIGHT bypass, TOP out) | VO D22, VC D24 |
| T1 | NTC, internal loop return, between the 360 outlet and the tee | A5 |
| T2 | NTC, V2 outlet | A4 |
| T3 | NTC, outdoor XE 240 | A3 |
| T_room | SHT30, outside the box on the room side | soft I2C D34/D36 |
| DP_A / DP_B | SHT31 ×2 in case | I2C 0x44 / 0x45 |

## Hardware changes
| Item | Rev 1 | Rev 2 |
|---|---|---|
| A5 NTC | GPU block inlet (backstop) | **T1**, loop return between the 360 outlet and the tee. Same C5 cable, rerouted. |
| Room sensor | none | **SHT30** on its own software I2C bus. SDA2 D34, SCL2 D36, 5V and GND from the Mega header, no board change. Its own bus is needed because 0x44 and 0x45 are both taken. |
| Dew point sources | 2 (SHT31 A/B) | 3 (adds the SHT30) |

No purchases: you already have the SHT30 modules.

## Logic changes
| Item | Rev 1 | Rev 2 |
|---|---|---|
| BTN1 short | Mode 1 auto | **Mode 1, branch off.** P2 off, V1 closed, V2 at bypass. No actuation; this is the maintenance state and the EEPROM default. |
| BTN2 short | Mode 2 pre-cool | **Mode 2, load cap.** T_set = max(T_room + δ, DP + 4). Sleeps at idle. |
| BTN2 long (≥ 2 s) | override → safe state | **Mode 3, max.** T_set = DP + 4. Same sleep/wake rules. |
| Off / override | separate Mode 0 | **merged into Mode 1** |
| Load detection | GPU ΔT / button / PC data (open) | **not needed.** Sleep = V2 parked at bypass ≥ 10 min. Wake = T1 > T_set + 1 °C. |
| Mode 2 setpoint | idle DP + 10 / load DP + 4 | room-relative. Fallback max(20, DP + 4) if the SHT30 is invalid. |
| Feasibility | Mode 1 only, loosely | **required in Modes 2/3.** V2 may open only if T_out < T1 − 5 °C. Forced to bypass if T3 > T1 − 1 °C for 2 min while flowing. |
| P2 speed | TBD | fixed 80 % (`P2_PWM_RUN`) |
| Shutdown order | V2 → V1 → P2 | **V2 to bypass (overdrive) → P2 off → V1 off 10 s later** |
| Backstop sensor | T_GPU < T_set − 1 | **T1 < DP + 2 → safe state** |
| Dew point | max of 2 SHT31 | **max of all valid** (A, B, room). Alarm if any two differ by > 2 °C. Mode 3 needs ≥ 2 valid. |
| V2 position | run-time estimate | **θ = θ₀ ± (90° / t_full,dir) × t_on.** Separate open/close travel times, backlash compensation, re-zero at bypass. |
| Display | proposal | T1, T2, T_out, cooling potential, T_room, DP, T_set, P2 state/PWM/rpm, V1 state, V2 angle, mode/state, heartbeat |
| Cooling potential | — | ΔT_pot = T1 − T_out. Watts = K_POT × ΔT_pot once K_POT is calibrated. |

## Removed
- The rev 1 Mode 1 enable/disable thresholds (ΔT_on, ΔT_off, the "pump off ≥ N min" condition).
- The Mode 2 idle/load margins and the open decision on the load trigger.
- The GPU NTC backstop.
- Mode 0 as a separate mode.

## Unchanged
These all carry over from rev 1:
- the dew point + margin cap
- sensor validity checks
- 12V presence handling
- fault latching
- watchdog
- floating valve control
- fan staging after V2
- boot order
- EEPROM storing only on change

## Bench checks required by rev 2
1. **δ:** run Mode 1 at idle for 30 min, then set δ = (T1 − T_room) + 1.5 °C.
2. **Series flow:** in Mode 2 under load, compare the cube NTC (Apex) with T2.
   - cube ≈ T2 → P2 and P1 run in series
   - cube between T2 and T1 → the check valve is passing flow; raise P2 to ≥ P1
3. **V2 travel:** measure t_full open, t_full close and the backlash.
4. **SHT30:** software I2C read with CRC; compare with the SHT31s in the same air.
5. **K_POT:** later, on the real loop.

## Doc follow-ups
- Schematic rev10 still shows the NTC at the GPU block and no SHT30. Update it to rev11.
