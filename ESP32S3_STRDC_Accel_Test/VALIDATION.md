# Validation

- Compiled successfully with Arduino CLI 1.5.1 and Arduino-ESP32 3.3.11.
- FQBN: `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default`.
- Program storage: 341382 / 1310720 bytes (26%).
- Global memory: 75056 / 327680 bytes (22%); 252624 bytes remain.
- Build output: `C:/Users/nate2/AppData/Local/Temp/codex-strdc-accel-trial-20260907`.
- All 14 STRDC BNO08X module and middleware files are byte-identical to the
  pinned upstream checkout. The full source/HAL hashes are in
  `SOURCE_MANIFEST.json`.
- Normalizing line endings, the sketch matches the upstream accelerometer
  example after only the documented include, bus-selection, and pin changes.

## Physical trial: passed accelerometer comparison

Uploaded through Arduino IDE to the separate ESP32-S3 on COM7 during the
2026-09-07 overnight trials. The supplied example successfully initialized the
BNO085, completed feature setup and MECal configuration, and reached
`Begin loop()`. The recorded 35-second streaming observation continued showing
changing acceleration components through its end.

Evidence:

- `../test_results/imu-overnight-20260907/vendor-accel-reset-recovery.txt`
  records normal ESP32 flash boot, the supplied accelerometer banner, successful
  setup, and initial output.
- `../test_results/imu-overnight-20260907/vendor-accel-stream.txt` contains
  3617 printed acceleration lines, 73 distinct printed x/y/z combinations,
  and 3368 changes between adjacent data lines. The final line is
  `x: 0.2310, y: -0.1518, z: 9.5005` m/s^2, with Medium accuracy.

These are **printed-line counts, not sensor-report counts**. The vendor example
prints cached values every 10 ms. Continued changes show that its cached data
was being updated during this observation; this capture cannot establish the
exact sample frequency or verify that every requested 400 Hz report arrived.
It establishes that the vendor accelerometer example can run on this port and
wiring; it does not establish gyro health.

Opening the serial monitor was also observed to leave the ESP32-S3 in its ROM
download mode. The test operator recovered normal flash boot by explicitly
opening serial with DTR=false and RTS=false, then pulsing RTS true for 100 ms
and returning it false. The reset-recovery evidence above then shows
`boot:0xb (SPI_FAST_FLASH_BOOT)` and successful application execution. This
USB/UART control-line startup issue is separate from an IMU stream stalling
after the application has initialized.
