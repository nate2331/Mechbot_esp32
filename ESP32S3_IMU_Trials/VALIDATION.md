# Validation

Prepared 2026-09-06 local / 2026-09-07 UTC. Hardware testing is performed separately
by the root task through the computer-use skill. This preparation did not open
a serial port or upload firmware.

- Arduino CLI 1.5.1, installed ESP32 core 3.3.11.
- FQBN: `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default`.
- Final compile: **PASS**, 339,554 bytes program storage, 28,376 bytes globals.
- Build output: `C:/Users/nate2/AppData/Local/Temp/codex-imu-trials-build-20260907`.
- Final sketch SHA256: `0BB93A2E81A755AA9F4FBC54B63D46779FD47F1EB48DBA7C72220E6D001E26CE`.
- Hash comparison confirms only `sh2.c` and `Adafruit_BNO08x.cpp` differ from
  the copied upstream source files. The two changes are recorded in
  `UPSTREAM_DIFF.patch`; original hashes are in `UPSTREAM_SHA256.csv`.

Reviewed initialization and transport: explicit RST handling avoids the optional
Adafruit GPIO reset; SH-2 host sessions close before re-opening; P0 stays low;
all reports use a sensor callback; reset events use a separate callback; Wire
transactions have a 50 ms timeout; SH-2 operations have a 1 s fallback timeout;
a failed I2C write terminates SHTP sending instead of its upstream infinite retry.
No unbounded operation retry is implemented by the sketch.

The firmware requests only transient report configuration and diagnostic readback.
There are no sketch calls to calibration persistence, calibration clear, FRS
writes, DFU or motor controls. Runtime conclusions require the serial results.

## Physical COM7 trials completed

Arduino IDE upload through computer use passed flash hash verification, writing
339,952 bytes. Evidence and full transcripts are in
`../test_results/imu-overnight-20260907/` (`ceva-upload-ui.txt`, `ceva-01-a.txt`
through `ceva-08-a.txt`). Each trial observed 35 seconds after fresh initialization.

- Accelerometer controls before/after: 2,235 fresh reports each, no sequence gaps
  or duplicates; final configuration interval 16,000 us.
- Geomagnetic rotation vector: 697 fresh reports; final interval 50,000 us.
- Calibrated gyro at 50 Hz, 10 Hz, and 50 Hz with 400 kHz I2C: zero reports;
  configuration interval still zero at the end.
- Combined accel/gyro: five accel reports ending at 133 ms, zero gyro reports;
  final accel configuration 16,000 us and gyro zero.
- Raw-only: zero reports, but this is inconclusive because raw gyro requires a
  separately enabled underlying gyro report (see README).
- All eight runs initialized successfully and answered post-trial product-ID
  requests. No unexpected reset, decoder error, or SHTP event was counted.

Error records also occur during healthy controls. Gyro-enabled trials returned
repeated module-21 records without a completion terminator before the one-second
diagnostic timeout. These are captured records, not proven distinct faults;
their private module/code meanings are not decoded. Full interpretation is in
the shared `FINDINGS.md` beside the logs.
