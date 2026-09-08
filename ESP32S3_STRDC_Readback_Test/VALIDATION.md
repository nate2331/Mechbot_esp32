# Validation

- Compiled successfully with Arduino CLI 1.5.1 and Arduino-ESP32 3.3.11.
- FQBN: `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default`.
- Program storage: 339026 / 1310720 bytes (25%).
- Global memory: 74440 / 327680 bytes (22%); 253240 bytes remain.
- Build output: `C:/Users/nate2/AppData/Local/Temp/codex-strdc-readback-trial-20260907`.
- All bundled `src/vendor` files, including the ESP32 HAL, are byte-identical
  to `ESP32S3_STRDC_Gyro_Test`. The 14 upstream module/middleware source files
  remain identical to the pinned upstream checkout; hashes are recorded in
  `SOURCE_MANIFEST.json`.
- Static review confirms one call site each for initialization and feature
  set (one invocation per trial / selected report), no attached ISR, and no
  calibration or persistent-write call from the harness.

## Physical trials, 2026-09-07

Uploaded through Arduino IDE to the separate ESP32-S3 on COM7. The IDE upload
reported 339408 application bytes and a verified flash hash; that upload build
is distinct from the initial CLI compile size above. Upload evidence is
`../test_results/imu-overnight-20260907/strdc-upload-ui.txt`.

All six trials initialized successfully and completed their 35000 ms observation
windows. Each final Product ID probe received a new response: part 10004148,
build 6, version 3.2.13. No reset notification occurred during any observation.

| Log in `../test_results/imu-overnight-20260907/` | Trial | Set result | Observation counts | Final age | Reported initial/final interval |
|---|---|---|---|---|---|
| `strdc-01-a.txt` | Accel 50 Hz, I2C 100 kHz | 0 | 2239 accel | 1 ms | 16000 / 16000 us |
| `strdc-02-g.txt` | Gyro 50 Hz, I2C 100 kHz | 1, timeout at 252 ms | 0 gyro | No report | 0 / 0 us |
| `strdc-03-m.txt` | Geomagnetic RV 20 Hz, I2C 100 kHz | 0 | 700 geomagnetic RV | 17 ms | 50000 / 50000 us |
| `strdc-04-b.txt` | Accel + gyro 50 Hz, I2C 100 kHz | Accel 0; gyro 1, timeout at 253 ms | 0 accel; 0 gyro | No reports in observation | Accel 16000 / 16000 us; gyro 0 / 0 us |
| `strdc-05-h.txt` | Gyro 50 Hz, I2C 400 kHz | 1, timeout at 252 ms | 0 gyro | No report | 0 / 0 us |
| `strdc-06-a.txt` | Accel recovery 50 Hz, I2C 100 kHz | 0 | 2239 accel | 13 ms | 16000 / 16000 us |

Every explicit initial/final Get Feature call returned success, including those
after gyro Set timeouts. The successful accelerometer recovery followed the
same harness hardware-reset initialization; no calibration command was sent.

## Interpretation limits

Counts are lower bounds of fresh parsed report indications, not exact sample
counts: reports bundled inside one packet share one vendor `newData` flag.
Setup and configuration readback occur before observation counting, so zero
accelerometer reports in the combined trial means none arrived during that
subsequent 35-second window; it does not establish that none arrived earlier.

The Get success flags are not retained from an earlier call. Each trial clears
the driver object, and every Get clears the requested report's `respRcd` before
sending a new request. Only a matching parsed feature response sets it again.
Similarly, the final ID probe explicitly clears `isID` before sending a request.

One upstream numeric-readback limitation remains: `bno08x_feature_get` extracts
interval and related values from fixed offsets in the received SHTP buffer,
assuming the requested feature response is first in that control packet. The
vendor parser also permits multiple control responses per packet. Raw packet
bytes were not captured here, so that assumption cannot be checked separately
for every printed interval. The known incorrect flags extraction is unused.

These trials demonstrate that merely continuing past the missing unsolicited
Set Feature response does not restore gyro streaming. Explicit control requests
still receive responses while gyro reports remain absent. They reproduce the
sensor-dependent failure and successful accel/geomagnetic paths seen with the
separate CEVA diagnostic. They do not establish physical damage or its cause.
