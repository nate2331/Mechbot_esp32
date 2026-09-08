# ElectroCookie 17-row board — component-side layout

View from the component side, as photo1: row1 at top, row17 at bottom,
A-E on the left, F-J on the right. Do not mirror these coordinates when reading
this plan. The solder-side view is mirrored. Each A-E row is one electrical node;
each F-J row is a separate node. Adjacent numbered rows are NOT joined.
Verify A1-E1 continuity, A1-F1 isolation and A1-A2 isolation before assembly.

## Components and connections

Use a 20-pin DIP socket across columns E/F, rows3-12, notch toward row1.
Pin1=E3, pin10=E12, pin20=F3, pin11=F12.

Maker cable: A13=3.3V, A14=GND, A15=GPIO21 UART RX,
A16=GPIO25 reset control, A17=5V. A 1x5 header fits here.

IMU signal cable: J1=SDA/TX, J2=RST. A 1x2 header fits here.
IMU power cable: J13=VDC/5V, J14=GND. A second 1x2 header fits here.
Keep P0 strapped to the IMU's own VDC. P1/BT retain onboard defaults.
These three headers are custom pinouts, NOT standard JST/I2C cable pinouts.

Q1 transistor net landing points: H14=emitter, H15=base, H16=collector.
These are required electrical destinations, NOT an assertion of the transistor's
physical leg order. Confirm the actual 2N2222 part/package before installing.
Use insulated short leads if its leg order cannot align directly with these holes;
never cross bare transistor leads. No flat-face orientation is specified yet.

R1 1k: D16 to F15. R2 10k: F14 to I15.
C1 100nF ceramic: H3 to H4, short leads. These nodes are +3.3V and GND.
If retaining the provisional 0.1uF electrolytic from the bench, positive=H3,
striped negative=H4. Ceramic is preferred for the final board.

## Add these jumper wires

| From | To | Purpose |
|---|---|---|
| C13 | C3 | Maker 3.3V to DIR pin1 |
| D3 | G3 | DIR supply to VCC pin20; insulated wire routed around socket top |
| B14 | B12 | Maker ground to IC pin10 |
| A5 | A12, soldered at EVERY row A5,A6,A7,A8,A9,A10,A11,A12 | Ground pins3-9 and pin10 |
| C12 | J4 | Ground /OE pin19 |
| G14 | I4 | Ground transistor emitter and IMU power-header ground |
| B17 | G13 | 5V to IMU VDC header |
| B15 | G5 | Buffer output pin18 to Maker GPIO21 |
| G1 | D4 | IMU TX to buffer input pin2 |
| I2 | I16 | IMU RST to transistor collector |

Use insulated wire for all jumpers except the straight A5-A12 ground bus.
Route long wires around the socket and leave clearance around mounting holes.
Crossing insulated wires are not electrical junctions. No copper cuts required.
Unused LVC outputs pins11-17 (F6-F12 nodes) remain disconnected; do not ground them.
Do not connect 5V to LVC VCC or directly to Maker GPIO21/GPIO25.

## Before powering

With all power removed, check the row connections, jumper endpoints and lack of
unintended 5V-to-3.3V or supply-to-ground shorts. Check socket orientation and Q1
pinout before inserting/connecting parts. Initially leave GPIO25 and IMU RST
cables disconnected; repeat the proven continuous UART test after moving the LVC.
Then connect and test reset separately once the Q1 pinout is confirmed.
Current continuous test firmware does not control GPIO25 or command IMU reset.
The reset stage pulls IMU RST low when GPIO25 is high; it does not switch IMU 5V.

Verified IC reference: https://www.ti.com/lit/ds/symlink/sn74lvc245a.pdf
Existing IMU reset pullup reference: STRDC BNO085_BOB-R1_V1 schematic.
Layout is a design proposal, not an electrically tested assembly.
