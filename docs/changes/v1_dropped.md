# V1 dropped (2026-11-03, FW 0.8.0)

Bench (Vlad): the 2W-025-08 NC solenoid draws 1.4 A at 12 V (~17 W) and its coil passed 50 °C within minutes.
V1 is held open for the whole RUN, so that heat sits inside the controller box, and F3 (1 A slow) was too small
for it anyway. Decision: drop V1 for now; a second 3-way may take its place later (OPEN).

## Firmware
- `V1_FITTED 0` (config.h): SEN (D28) is never driven HIGH; `hwV1Open()` reads as open, so "flowing" = P2 on and
  V2 above the edge. All V1 steps in the sequences become no-ops. Display EXT LOOP shows OPEN while outdoor flows.
- Test mode `v1`/`v0` answer "V1 not fitted". CSV column V1 stays (always 0).
- `V1_FITTED 1` restores the original behaviour.

## Safety consequence
- No fail-closed barrier on the outdoor branch. On a fault or the hard low limit the outdoor port shuts when V2
  reaches the edge (play 725 ms + up to ~3.8 s from all ports open), then V2 parks.
- On a Mega hang the watchdog resets it and the boot parks V2. With the PC off, P1 is off too.
- If V2 is left open when the PC shuts down, the branch stays open until the next boot park.

## Hardware notes if V1 comes back
- F3 must be 2.5–3.15 A slow (solenoid + CR-02).
- Flyback diode across the coil (cathode on V1 +, AA13).
