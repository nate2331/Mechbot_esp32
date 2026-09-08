# Build and simulated test result — 2026-09-07

**Final target compilation passed. This sketch has not been uploaded or run on
the robot by this task.**

- Controller: NULLLAB Maker ESP32 Pro / classic ESP32.
- FQBN: `esp32:esp32:esp32:FlashMode=dio`.
- Installed Espressif Arduino core: **3.3.11**.
- Flash: **288951 / 1310720 bytes (22%)**.
- Static RAM: **22648 / 327680 bytes (6%)**.
- Motor setup: 248 Hz APB, 9-bit LEDC, logical duty150 scaled to hardware300/512.
- Target build folder: `C:/Users/nate2/AppData/Local/Temp/codex-maker-rvc-movement-build`.

Compile from the repository root:

```powershell
& 'C:/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe' compile --config-file 'C:/Users/nate2/.arduinoIDE/arduino-cli.yaml' --fqbn 'esp32:esp32:esp32:FlashMode=dio' --build-path 'C:/Users/nate2/AppData/Local/Temp/codex-maker-rvc-movement-build' 'Maker_IMU_RVC_Movement_Test'
```

The actual `.ino` was also compiled against isolated fake Arduino/serial/RTOS
interfaces using Zig C++17 and exercised on the PC. All seven process scenarios
passed: normal operation plus motor attachment, task creation, mutex creation,
clock selection, UART initialization, and incorrect UART RX pin failures.

Normal-operation assertions cover:

- Idle boot and fresh-IMU requirement for GO; three-second countdown.
- Independent expected physical driver pins, wheel signs and doubled PWM duties.
- Eight successive one-second motion phases and 300 ms coasts, including repeat.
- Immediate X without a newline or waiting for the next simulated output tick.
- Stale IMU stopping after more than 500 ms and no automatic restart on recovery.
- Output-task-only 300 ms lease expiry and one-second phase deadline.
- Checksum, index and UART fault telemetry during movement.
- Millisecond timer wraparound and latched PWM write failure.

Reproduce the host checks from the repository root (requires Zig):

```powershell
& './tests/maker_rvc_movement_run.ps1' -ZigPath 'C:/Users/nate2/AppData/Local/Temp/mechbot-host-tests-56e058a7e7b14976ad7f1afacd01c006/ziglang/zig.exe'
```

The script accepts another Zig path and writes its executable/log into a new
temporary folder. Retained evidence is in
`../test_results/imu-rvc-movement-20260907/`.

The copied `MotorSafety.h` and `RvcParser.h` match the original files by SHA256.
Independent source review checked direction mapping, mutex use, fault latching
and PWM configuration. Simulation does not model real RTOS scheduling, electrical
noise, physical coasting or traction. Actual robot testing remains pending.
