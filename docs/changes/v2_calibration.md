# V2 calibration (2026-11-02, FW 0.6.0)

Bench, `07_valve`, CR-02 on PW rev 6b (Vlad).

| Measured | Value |
|---|---|
| Direction | VO (D22) toward all ports open, VC (D24) toward bypass (`C10000` = full bypass) |
| End to end, one straight run | 6400 ms |
| Play after a reversal | 725 ms before the ball moves |
| From bypass, after the play | 1300 ms: outdoor port just not open (dead zone) |
| | +1800 ms (3100): 50 % outdoor, both inlets half |
| | +1950 ms (5050): all ports open |
| Port opening rate | ~1.35 mm per 500 ms |

Stepped segments add up to 725 + 5050 = 5775 ms, less than the straight 6400 ms run (play / start-stop).
The model uses the stepped numbers since regulation moves in steps: t_full = 5050 ms after the play.

## What changed
- θ over 0…90° = 0…5050 ms of motion, backlash 725 ms on every reversal.
- The edge θ = 23.2° (`V2_FLOW_DEG`, from `V2_DEAD_MS`). Below it no outdoor flow.
- Open steps from below the edge add the dead-zone crossing time, so the first step after a park actually opens.
- Regulation closes down to the edge only (it used to turn any close reaching 0° into a park). Parks stay for start,
  stop/sleep, the hard low limit, forced bypass and SAFE. Sleep counts time at or below the edge.
- Park = VC for `V2_PARK_MS` 10 s (the valve autostops), replacing t_close × 1.3.
- Start rate limit is counted from the edge.
- Gains and max pulses scaled to the real travel (they were for a 30 s placeholder). Tune on the filled loop.

## Still open
- Close travel assumed equal to open; flow characteristic f(θ) and the gains need `10_v2_calibration` on the loop.
