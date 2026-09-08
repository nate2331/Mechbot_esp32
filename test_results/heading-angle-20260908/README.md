# Hand-positioned heading checks — September 8, 2026

Motor power off per Nathan. Integrated RVC source revision 65bb431. Nathan was
asked to align the bot to a starting mark, rotate 90 degrees counterclockwise
viewed from above, return, then rotate 90 degrees clockwise. Each sample window
started only after his position confirmation. Ten fresh HTTP observations were
saved per position with 500 ms pauses. No motion command or heading acceptance
was issued. Reference tool precision and mounting axes have not been documented.

| Position | Mean raw yaw (degrees) | Change from original baseline |
| --- | ---: | ---: |
| Baseline | -53.710 | 0 |
| Reported 90-degree CCW | -141.823 | -88.113 |
| First return | -53.813 | -0.103 |
| Reported 90-degree CW | 36.800 | 90.510 |
| Final return after CW | -53.900 | -0.190 |
| Reported 180-degree position | 125.150 | 178.860 |

Both directions indicate raw yaw is clockwise-positive in the current setup,
opposite the controller's counterclockwise-positive heading convention. Correction
must be applied consistently to heading and field transforms before acceptance;
changing only the heading correction gain sign would not fix field rotation.

Magnitude discrepancies are 1.887 degrees for CCW and 0.510 degrees for CW against
the original reference. They combine hand positioning/reference error and sensor
error; they are not isolated IMU accuracy measurements. Relative to the immediately
preceding returned position, CW change is 90.613 degrees. Final return was steady
at -53.90 across ten samples. The reported 180-degree position was steady at 125.15,
a wrapped change of 178.86 degrees (1.14 degrees from a half-turn). This verifies
the endpoint difference, not continuous tracking across the wrap boundary, since
the hand-turn trajectory was not captured. Repeats, stationary drift and startup
repeatability remain open.
