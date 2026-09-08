# S3 upload observation

Uploaded through Arduino IDE 2.3.10 using the Windows computer-use skill, at the
user's explicit request, to COM7. Selected ESP32S3 Dev Module, USB CDC On Boot
Disabled, Hardware CDC and JTAG, UART0 / Hardware CDC, PSRAM Disabled, and 4 MB
flash. Erase All Flash Before Sketch Upload remained Disabled.

Arduino IDE displayed the completed application write (337232 bytes,
195807 compressed), `Verifying written data...`, `Hash of data verified.`,
and `Hard resetting via RTS pin...`.

The COM7 Serial Monitor at 115200 initially displayed the ESP32-S3 ROM banner,
`boot:0x3 (DOWNLOAD(USB/UART0))`, and `waiting for download`. The user was asked
to release BOOT and press RESET/EN once. Their subsequent capture showed normal
flash boot, `ESP32S3_IMU_TEST V1`, ACK and initialization at 0x4A, and live
accelerometer reports. This supersedes the initial execution-pending observation.

## Observed comparison on COM7

The user's initial capture showed ACCEL samples 60 at ms1903 through 701 at
ms11911, about 64 received reports per second, with zero decode errors. GYRO
was OFF, as expected before its command. Live UI observation then confirmed
continued ACCEL_ONLY reception through ms73950, samples4667, before the gyro
test. Values remained approximately 0.27, -0.078, 9.5 m/s^2.

Using the Serial Monitor UI, sent `g` once. The sketch printed
`GYRO requested at ms=74863` and `GYRO configuration sent; wait for fresh samples.`
At ms74951, ACCEL was still LIVE with samples4731, while GYRO was WAIT with zero
samples. Later observations showed all sensor reporting stalled:

```text
ms=148023 TEST=ACCEL_PLUS_GYRO decode_errors=0
ACCEL STALE samples=4735 new=0 age_ms=73015 last_xyz=0.270,-0.078,9.504 m/s^2
GYRO WAIT samples=0 new=0 age_ms=-1
ms=152027 TEST=ACCEL_PLUS_GYRO decode_errors=0
ACCEL STALE samples=4735 new=0 age_ms=77019 last_xyz=0.270,-0.078,9.504 m/s^2
GYRO WAIT samples=0 new=0 age_ms=-1
```

The accelerometer's last received sample was at ms75008, about 145 ms after
the gyro request. No gyro report was decoded, and no RESET_STOPPED state was
observed. The final captured summary is over 77 seconds after the gyro request.
The sketch and Serial Monitor continued running while sensor reporting stopped.

Interpretation: a working accelerometer stream followed by loss of all sensor
reports when the gyro is enabled also occurs on the separate ESP32-S3. This
narrows the investigation toward the IMU/gyro startup or its supply, but does not
identify a damaged component or exclude transport/driver behavior. Zero decode
errors is not a measurement of I2C transport errors. This test did not query the
internal chip error found in the earlier Maker diagnostic, measure supply
waveforms, or verify physical motion response or orientation accuracy.

Evidence: [observed UI excerpt](../test_results/esp32s3-imu-test/gyro-stall-ui-excerpt.txt).
This is a saved accessibility excerpt, not a complete raw serial capture.
COM7 was left open with the failed state visible. No additional reset or retry
was issued after the gyro command.
