# Build status — 2026-09-07

Arduino CLI compilation **passed** with the installed Espressif Arduino core 3.3.11.

- Board: ESP32 Dev Module (classic ESP32-WROOM-32E on NULLLAB Maker ESP32 Pro).
- FQBN: `esp32:esp32:esp32:FlashMode=dio`.
- Program storage: 277928 bytes (21% of selected partition).
- Global variables: 22428 bytes (6% of available dynamic memory).
- Build output: `C:\Users\nate2\AppData\Local\Temp\codex-maker-imu-rvc-5v-build`.

Adapted from the compiled ESP32S3_IMU_RVC_5V_Test: target guard changed to classic ESP32; UART RX moved to GPIO21; all eight Maker motor inputs initialized LOW; host serial TX buffer set to 2048 bytes. UART parsing, the 2ms sampled idle-high gate, the total 120-second startup wait, and the 35-second observation remain the same. RvcParser.h is unchanged.

Source checks found no I2C calls, IMU reset/mode outputs, or sensor-UART transmissions. GPIO21 is input only. The only output writes set the known motor-driver input pins LOW. No PWM is attached. This is an isolated IMU bench diagnostic, not robot-control firmware.

At build completion, this task had not uploaded or tested the sketch on hardware. Neither the Maker nor the S3 was enumerated on the PC at that time. Source review and compilation alone do not establish measured supply voltages, correct physical wiring, or a working IMU.

**Subsequent user-run hardware result:** a complete 35-second log with this firmware banner reports 3493 valid RVC frames, zero checksum/index/UART errors, and LIVE output through the end. See [saved findings](../test_results/imu-maker-rvc-5v-20260907/FINDINGS.md). The user supplied the console text; this task did not independently capture the upload, wiring, supply voltages or flashed-binary hash.

A second user-run movement log reports3104 valid frames with large yaw/pitch/roll changes. Its first~4seconds contain invalid data,1497 UART break events and1 checksum failure; those counters then remain constant while valid data streams at~100Hz through the end. The35-second window starts at any byte, so this second trial contains about31seconds of clean valid streaming. See the same findings for details; the sketch was not changed after these observations.

Use the Maker version only when the Maker is the test controller. If the Maker supplies only 5V and the S3 remains the receiver, use the separate S3 sketch and its GPIO8 wiring.
