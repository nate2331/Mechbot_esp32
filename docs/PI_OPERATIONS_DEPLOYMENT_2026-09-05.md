# September 5 Operations deployment on mechpi

The Operations dashboard is installed on the real Raspberry Pi and reachable
on the local Wi-Fi at **http://192.168.50.166:8765/**. It displays **LIVE**.
The PC simulator remains separate at http://192.168.50.242:8876/ and displays
**SIMULATED**. Neither link is a public Internet endpoint.

## USB reset and controller recovery

At the user's request, the bridge stopped output and entered maintenance to
release its exclusive CH340 USB serial port. A one-time reset held DTR false
and pulsed RTS true for 120 ms, then false. Captured ROM output reported normal
flash boot and a fresh `READY ESP32_MAKER_MECANUM_IMU_V1`.

The first IMU initialization failed, but the firmware's automatic retry
reported `IMU READY BNO085 GAME_ROTATION_VECTOR GYRO LINEAR_ACCEL` about 3.6
seconds after reset. Bridge ownership was restored, Maker was identified from
its exact mapping response, and valid/fresh IMU readings persisted throughout
a 30.6-second follow-up. This recovery does not establish the cause of the
earlier intermittent failures. Encoder counts stayed unchanged and all four
applied PWM values stayed zero. No firmware upload or factory reset occurred.

## Installation

- Runtime source commit: `cf10c5ab11dd0457fb81c317105897fd0c094e32`.
- Staging: `/home/nate/mechbot-operations-stage-yvb87fsp`.
- Backup: `/home/nate/mechbot-operations-backup-2of7x_gg`.
- Service: `mecanum-gamepad.service`; existing user `nate`, working directory
  `/home/nate`, and `/usr/bin/python3 /home/nate/pi_mecanum_gamepad.py` entry point.
- That entry point and `/home/nate/mechbot_bridge.py` both contain the reviewed
  bridge. The separate standalone gamepad sender was not installed over it.
- Thirty runtime, asset, evidence and reference-document files were verified.
  Every existing target matched the reviewed baseline before replacement.
  Old files, service definition, live settings and saved tuning session were
  backed up before maintenance and installation. No existing recordings,
  calibration or tuning session were overwritten.
- The existing restricted `mechbot-admin restart-bridge` helper restarted the
  service after file installation. The service returned active/running with
  zero automatic restarts and deadman rearm required.

All nine live settings were unchanged: PWM 177 for FL/FR/RL/RR, heading KP 0.7,
maximum correction 0.3, deadband 1.5 degrees, sign +1, and heading correction off.
NVS was not written. Firmware source under `/home/nate/mechbot-src` was not
replaced by this runtime deployment.

## Verification

All **222 Python tests passed on the Pi**, using Python 3.10.12 and pySerial 3.5,
in 33.078 seconds. These tests use fake serial IO; their upload log messages are
mocked. The deployment then verified every installed file and all ten served
dashboard assets against their staged bytes.

At initial acceptance, the live Operations API reported Maker, fresh wheel rates, valid/fresh IMU,
no configuration or observation errors, and unset wheel geometry. All three
original bench reports load without catalogue errors. The browser rendered LIVE,
the actual USB port, four supported stationary wheel measurements, and IMU yaw.

A new real stationary capture, **Stationary post-deployment verification**,
saved **1,395 events with zero drops**. Recording ID:
`82610bec47e74b1b86a0014398a2753a`. It is retained on the Pi, explicitly marked
as nonsimulated, and available under Recordings. Its physical surface and load
remain unrecorded. Passive replay preserved the Maker profile and capture
duration without dispatching recorded TX. During the capture, wheel and IMU
readings stayed fresh, outputs stayed zero, encoder counts stayed unchanged,
and rearm remained required. Measured geometry was not invented or saved.

Detailed artifacts are in the PC task workspace: `pi-usb-reset-result.json`,
`pi-usb-reset-followup.json`, `pi-operations-deployment-result.json`,
`pi-operations-live-verification.json`, and `pi-operations-python-tests.log`.
The Pi stage also retains the deployment manifest and test log.

## Power-cycle follow-up and current status

The user switched power during an optional Pi-side candidate firmware compile.
The Pi restarted and the bridge service started automatically. That compile was
interrupted; it is not a successful build result and no firmware was uploaded.
The earlier PC candidate build remains the completed build evidence.

The restart exposed a bridge startup defect: its single identity query could
be missed while firmware initialization was busy, leaving the board unknown.
Commit `fd2fc5cff5cde2a7a42b3820cadd79cedc1eb905` adds a read-only `?` retry
every two seconds until identity is known. Maintenance, calibration and firmware
jobs suppress retries; explicit unknown READY identities remain unsupported.
This does not automatically reset the controller or issue motion commands.
Both installed bridge entry points were updated with verified hashes and a
backup at `/home/nate/mechbot-operations-backup-e8vee4i_`.

The expanded Python suite passed **all 229 tests on the Pi** in 33.196 seconds;
on Windows, 228 passed and one symlink privilege fixture was skipped. The
dashboard connection fix clears its own stale network error after polling
recovers while preserving newer operator notices and current observation faults.
All **26 Node tests passed**, including four connection recovery cases.

After the power cycle, Maker identification and stationary wheel diagnostics
recovered, but the IMU reported OFFLINE. One stopped `IMU RETRY` attempt and one
explicit USB RTS reset both failed to restore IMU readings. The reset produced
a fresh Maker READY; subsequent IMU initialization attempts still reported
not detected. The earlier successful IMU recovery and capture are historical
evidence, not the current sensor state. The sensor fault remains unresolved;
software observations do not distinguish power, wiring or sensor causes.
All observed encoder counts and applied PWM values remained zero. No firmware
upload, NVS write or physical movement test was performed.

Follow-up evidence in the PC task workspace: `pi-identity-deployment-result.json`,
`pi-identity-python-tests.log`, `pi-imu-retry-result.json`,
`pi-usb-reset-post-powercycle.json`, and `pi-post-powercycle-followup.json`.

## Next supervised work

Subsequent user authorization led to the [verified encoder firmware upload](MAKER_FIRMWARE_UPLOAD_2026-09-05.md).
The earlier no-upload statements above describe the Operations deployment and
power-cycle checkpoint. With the flashed build now identified, repeat the
encoder diagnostics, characterize both directions and holding duty,
and measure loaded wheel diameter plus full wheel-centre wheelbase and track.
Continue IMU reliability checks under documented conditions. The new dashboard
and recordings now provide the evidence tools for those steps.
