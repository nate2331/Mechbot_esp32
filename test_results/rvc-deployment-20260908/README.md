# Buffered RVC deployment — September 8, 2026

Nathan authorized flashing when motor power is off and explicitly confirmed motor
power off with Pi/ESP32 powered. This supersedes earlier staging-only statements.
No nonzero motion command or heading acceptance was issued.

- Matched Pi release: `/home/nate/rvc-release-sneyh6cn`; backup under `backup/`. 58 packaged source/runtime/test files hash verified; all 246 Python tests passed on the Pi. Service active/running with zero automatic restarts.
- ESP32 upload: `/home/nate/maker-rvc-upload-dnnwe2yi`. Full 4 MB backup completed in 411.2 seconds. Local second copy: ignored `build/rvc-flash-before-20260908.bin`.
- Backup SHA-256: `b4434f5dae29f17c38ffd3887fbff26770211826d4ca01abaa674e4d3f7afb3e`.
- Existing partition table matched before writing. All four images independently verified; 20480-byte NVS region unchanged. No whole-chip erase.
- Fresh `READY ESP32_MAKER_MECANUM_RVC_V1` captured 0.681 seconds after reset. Source label `MAKER_RVC_V1_FIELD_FAULT_LATCH`.
- Settings: four PWM values 177; heading KP 0.7, maximum 0.3, deadband 1.5 degrees, sign +1, heading enabled 0. Integrated PWM frequency remains 20 kHz.
- Updater dry run selects RVC build property and separate `.maker-rvc-build` directory. Installed Maker source matches the release.

Thirty samples with two-second pauses all had fresh valid RVC, zero checksum/index/
UART counters, four fresh zero outputs, deadman false, rearm required and heading
acceptance false. Yaw range was 0.03 degrees; stillness and reference angles were
not independently measured, so this is communication evidence, not accuracy acceptance.

The live endpoint sent F 1 while heading remained unaccepted. Firmware rejected it
with `ERR field mode requires a fresh IMU heading; motors stopped`. Navigation
remained off. No IMU ACCEPT, heading enable or nonzero V was sent.

Artifacts: `upload-result.json`, `verify-flash.log`, `release-manifest.json`,
`pi-tests.txt`, `live-observation.json`, `live-summary.json`, `field-rejection.json`.
Local checks also passed 39 JavaScript tests and desktop/mobile Edge interactions.

Remaining: measured mounting/sign/angles, startup repeatability, motor-noise
reliability, heading/field driving results and intended-surface trials.
