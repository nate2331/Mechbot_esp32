# Build status — 2026-09-07

Arduino CLI compilation **passed** using the installed Espressif Arduino core 3.3.11.

- Board: ESP32S3 Dev Module.
- FQBN: `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=cdc,FlashMode=dio`.
- Program storage: 312322 bytes (23% of selected partition).
- Global variables: 22640 bytes (6% of available dynamic memory).
- Build output: `C:\Users\nate2\AppData\Local\Temp\codex-imu-rvc-5v-build`.

The parser is copied unchanged from the earlier UART-RVC diagnostic. Source review checked that GPIO8 is only an input; no I2C, IMU reset/strap writes, or UART transmissions are present. The installed core's explicit RX8/TX-1 behavior was checked, and the sketch additionally checks the actual UART pin assignments at runtime.

The startup wait requires a sampled high input for 2ms before attaching the UART. One 120-second budget covers both that wait and waiting for the first byte. The first byte starts the 35-second observation. Completion stops UART and returns GPIO8 to input. There are no automatic trials or retries.

**Not uploaded or tested on hardware.** COM7 was absent during preparation. Timing, error reporting, wiring, and actual IMU behavior still require the physical test. A successful build and source review do not establish a successful IMU test.
