# V2 physical direction verification

September 8, motor power off. Baseline uses the bot's earlier 180-degree position,
which Nathan said was unchanged, then confirmed ready. Ten fresh samples:
raw yaw +125.111 degrees, corrected heading -125.111 degrees.

The requested turn was initially described as counterclockwise, but Nathan
subsequently clarified that he turned clockwise. The sample file is `cw90.json`
to reflect the actual reported direction. Ten fresh samples: raw yaw -141.520,
corrected heading +141.520. Wrapped corrected change is -93.369 degrees.

This agrees with a clockwise turn under the corrected CCW-positive convention.
The 3.369-degree magnitude difference includes placement/reference error; it is
not an isolated sensor accuracy measurement. Device time advanced from
424752–430954 ms at baseline to 476159–482559 ms at the turned position.
The raw-to-corrected relationship was consistent in both windows. Heading
acceptance remains off; no robot motion command was sent.

Return confirmed by Nathan: ten samples at corrected -126.699991 degrees,
offset -1.588961 degrees from the V2 baseline. This includes reference/placement
error and is not isolated sensor drift.

After asking Nathan to leave the bot untouched, 51 fresh observations spanned
59.399 seconds. Every corrected heading was -126.699991 degrees, with zero
reported endpoint change and range at the sensor's reporting resolution. All
communication counters stayed zero and all four outputs were fresh and zero.
This short stationary interval does not establish long-term drift or motor-noise
reliability. Position was operator-controlled, not independently instrumented.
