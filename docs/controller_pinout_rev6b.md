# Controller pinout — component to Arduino Mega (rev 6b)

**How to read the tables:**
- **Ribbon** is the LG → Mega 34-pin ribbon pin. The header holes are on LG rows Z and AA. The red stripe is on pin 34.
- **H · pos** is the Dupont housing and position at the Mega (H1–H8, see the end of this file).
- **LINK** is one of the 16 soldered PW → LG wires: PW column 22 rows A–P to LG row A.

---

## Case side → LG

| Component pin | Cable | LG hole | On LG | Ribbon | H · pos | Mega |
|---|---|---|---|---|---|---|
| SHT31 A+B VIN | C2-1 | D1 | 5V bus | 8 (AA6) | H4 · 2 | 5V |
| SHT31 A+B GND | C2-2 | D2 | GND bus | 9/10 | H1·1 / H2·1 | GND |
| SHT31 A+B SDA | C2-3 | D3 | → J10 (4.7k pull-up) | 19 (Z12) | H5 · 2 | D20 |
| SHT31 A+B SCL | C2-4 | D4 | → J11 (4.7k) | 20 (AA12) | H5 · 1 | D21 |
| SHT30 VIN | direct | D6 | 5V bus | 8 | H4 · 2 | 5V |
| SHT30 GND | direct | D7 | GND bus | 9/10 | — | GND |
| SHT30 SDA | direct | D8 | → J12 (4.7k) | 27 (Z16) | H3 · 2 | D34 |
| SHT30 SCL | direct | D9 | → J13 (4.7k) | 28 (AA16) | H3 · 1 | D36 |
| T1 NTC lead 1 | C5-1 | D11 | → K6 (10k pull-up + 100 nF) | 6 (AA5) | H8 · 6 | A5 |
| T1 NTC lead 2 | C5-2 | D12 | GND | — | — | GND |
| Display GND | C4-1 | O2 | GND | — | — | GND |
| Display VCC | C4-2 | O3 | direct | 7 (Z6) | H4 · 1 | 3.3V |
| Display SCL | C4-3 | O4 | ← N1, 330/680 divider ← M1 | 33 (Z19) | H2 · 2 | D52 |
| Display SDA | C4-4 | O5 | ← N3 ← M3 | 31 (Z18) | H1 · 3 | D51 |
| Display DC | C4-5 | O6 | ← N5 ← M5 | 32 (AA18) | H1 · 2 | D53 |
| Display CS | C4-6 | O7 | ← N7 ← M7 | 30 (AA17) | H1 · 4 | D49 |
| Display RES | C5-3 | D13 | ← N9 ← M9 | 29 (Z17) | H1 · 5 | D47 |
| Display BLK | — | — | not connected | — | — | — |
| Button 1 leg A | C3-1 | O9 | → J17 (10k pull-up) → 1k → O17 (100 nF) | 25 (Z15) | H3 · 4 | D30 |
| Button 2 leg A | C3-2 | O10 | → J19 (10k) → 1k → O19 (100 nF) | 26 (AA15) | H3 · 3 | D32 |
| LED red anode | C3-3 | O11 | → F14, 220 Ω, J14 | 13 (Z9) | H6 · 4 | D9 |
| LED green anode | C3-4 | O12 | → F15, 150 Ω, J15 | 14 (AA9) | H6 · 3 | D10 |
| LED blue anode | C3-5 | O13 | → F16, 150 Ω, J16 | 15 (Z10) | H6 · 2 | D11 |
| Buttons leg B + LED cathode | C3-6 | O14 | GND | — | — | GND |

---

## Box side → PW → LINK → LG

| Component pin | PW hole | On PW | LINK wire (PW → LG) | On LG | Ribbon | H · pos | Mega |
|---|---|---|---|---|---|---|---|
| T3 NTC lead 1 (XE 240) | AA7 | wire | 1 (A22 → A3) | → K4 (10k + 100 nF) | 4 (AA4) | H8 · 4 | A3 |
| T3 NTC lead 2 | AA8 | GND | — | — | — | — | GND |
| T2 NTC lead 1 (V2 outlet) | AA9 | wire | 2 (B22 → A4) | → K5 (10k + 100 nF) | 5 (Z5) | H8 · 5 | A4 |
| T2 NTC lead 2 | AA10 | GND | — | — | — | — | GND |
| Fan 1 pin 3 tach | U16 | wire | 3 (C22 → A5) | → J7 (10k pull-up) | 17 (Z11) | H5 · 4 | D18 |
| Fan 2 pin 3 tach | U21 | wire | 4 (D22 → A6) | → J8 (10k) | 18 (AA11) | H5 · 3 | D19 |
| P2 pin 3 tach | X16 | wire | 5 (E22 → A7) | → J9 (10k) | 16 (AA10) | H6 · 1 | D12 |
| Fan 1 + 2 pin 4 PWM | U17 + U22 | wire | 6 (F22 → A8) | direct | 11 (Z8) | H7 · 2 | D2 |
| P2 pin 4 PWM | X17 | wire | 7 (G22 → A9) | direct | 12 (AA8) | H7 · 1 | D3 |
| GND reference | — | GND | 8 + 9 (H22/I22 → A19/A20) | GND bus | 9/10 | H1·1 / H2·1 | GND |
| Fuse sense, solenoid + valve | — | F3 → 47k/20k (K13) | 10 (J22 → A17) | → K3 (100 nF) | 3 (Z4) | H8 · 3 | A2 |
| Fuse sense, fans | — | F2 → 47k/20k (G13) | 11 (K22 → A16) | → K2 | 2 (AA3) | H8 · 2 | A1 |
| Fuse sense, pump | — | F1 → 47k/20k (C13) | 12 (L22 → A15) | → K1 | 1 (Z3) | H8 · 1 | A0 |
| V1 solenoid − | AA14 | ← M4 drain; gate via 100 Ω (S14) | 13 (M22 → A13) | direct | 24 (AA14) | H3 · 5 | D28 |
| V1 solenoid + | AA13 | 12V via F3 | — | — | — | — | — |
| P2 +12V (pin 2, or SATA on AA12) | X15 | ← M3 drain ← T3 driver (S11) | 14 (N22 → A12) | direct | 23 (Z14) | H3 · 6 | D26 |
| V2 close | AA17 | ← M2 drain ← T2 driver (S7) | 15 (O22 → A11) | direct | 22 (AA13) | H3 · 7 | D24 |
| V2 open | AA16 | ← M1 drain ← T1 driver (S3) | 16 (P22 → A10) | direct | 21 (Z13) | H3 · 8 | D22 |
| V2 COM | AA15 | GND | — | — | — | — | GND |
| Fans + P2 pin 1 GND | U14, U19, X14 | GND | — | — | — | — | GND |
| Fans pin 2 +12V | U15, U20 | 12V via F2 | — | — | — | — | — |
| PSU +12V | AA3 (C1-1) | fuse bus → F1/F2/F3 | — | — | — | — | — |
| PSU GND | AA4 (C1-2) | GND bus | — | — | — | — | GND |

Every GND in both tables meets at the Mega through ribbon pins 9 and 10. With the Mega on PC USB, the whole system shares one ground.

---

## Mega housings (position 1 first)

| Housing | Size | Mega header | Positions |
|---|---|---|---|
| H1 | 1×5 | Double header, odd side | GND · D53 · D51 · D49 · D47 |
| H2 | 1×2 | Double header, even side | GND · D52 |
| H3 | 1×8 | Double header, even side | D36 · D34 · D32 · D30 · D28 · D26 · D24 · D22 |
| H4 | 1×2 | Power header | 3.3V · 5V |
| H5 | 1×4 | D14–D21 header | D21 · D20 · D19 · D18 |
| H6 | 1×4 | PWM header | D12 · D11 · D10 · D9 |
| H7 | 1×2 | D7–D0 header | D3 · D2 |
| H8 | 1×6 | Analog header | A0 · A1 · A2 · A3 · A4 · A5 |
