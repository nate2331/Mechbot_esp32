# BNO085 bus and sensor diagnostic

Current sketch identity: `MAKER_IMU_BUS_DIAGNOSTIC_V4`.

This is a motor-disabled diagnostic for Nate's Maker ESP32 Pro and STRDC BNO085.
It is not robot navigation firmware. The fixed motor direction pins remain LOW.

Existing wiring: VDC to Maker 3.3V, common GND, P1 to GND, P0 to GPIO16,
RST to GPIO25, SDA to GPIO21, SCL to GPIO22, DI to GPIO32, CS to GPIO33,
INT to GPIO26. Firmware selects I2C by holding P0 LOW before releasing reset.
DI is held LOW to select address 0x4A. Do not change this wiring for this sketch.

V4 tests calibrated gyro at 20000 and 5000 us, uncalibrated gyro at 20000 us,
geomagnetic rotation vector at 100000 us, and accelerometer at 100000 us.
Each phase resets the IMU and uses I2C at 100 kHz. It observes output for over
30 seconds before querying errors at thresholds 0 and 4. Threshold 4 was
observed in V3; it is not assumed to cover every possible severity.

V3 tested accelerometer and magnetometer successfully for about 33 seconds
each. Its standalone raw gyro probe was inconclusive because raw reports can
depend on another enabled sensor. V3 source and raw logs are preserved in the
results directory. V4 avoids the threshold-255 query that produced repeated
chip-error records during the V3 gyro phase.

After all phases, the firmware stops I2C and prints stored results every ten
seconds. It does not repeatedly reset the IMU. P0 remains LOW and RST is released
to the breakout's pullup. Summary repetition does not imply new measurements.

`SAMPLES` counts decoded vector reports; `SPAN_MS` is time between first and
last decoded samples. Values are raw fixed-point integers, not all in the same
units. `INPUT` counts transport packets, which is weaker evidence than decoded
samples. First/last packet bytes and control/error responses are retained for
review. For quaternion reports the raw XYZ columns contain the first three
quaternion components; the full first/last packet bytes also contain W.

Transport checks follow CEVA SHTP section 2.3.1: the header-only read is followed
by a complete remaining-payload read, with continuation bit and sequence+1.
Requests are bounded, use STOP, and have a 25-ms I2C timeout. No persistent FRS,
calibration-save, firmware-update, or factory-reset commands are issued to the IMU.

Build/upload using Arduino IDE, ESP32 Dev Module, COM8, ESP32 core 3.3.11.
Serial Monitor is 115200 baud. Pin assignments are specific to this robot.

Research and hardware observations are in
`../test_results/imu-bus-diagnostic-20260906/`.
