# Pi deployment and verification

## Later deployment and firmware checkpoints

The September 4 installation history below was followed by the [September 5
Operations deployment](docs/PI_OPERATIONS_DEPLOYMENT_2026-09-05.md), including
real capture/replay and startup-identity recovery. The current matched runtime
file list is in the [Operations runbook](docs/OPERATIONS_RUNBOOK.md#pi-installation-and-dependencies).
The installed service entry point remains a copy of `mechbot_bridge.py`.

The encoder sampler was later [uploaded and independently verified](docs/MAKER_FIRMWARE_UPLOAD_2026-09-05.md),
followed by [powered wheel checks](test_results/powered-encoder-20260905/README.md),
[pair comparisons](test_results/paired-motors-20260905/README.md), and a further
[PWM-readback firmware upload and trial](test_results/pwm-readback-20260905/README.md).
Those uploads preserved NVS, and the motor trials restored the original live
settings. No verified speed-matching trim was saved. The no-upload statements
below describe the September 4 checkpoint. Later September 6–7 IMU experiments
and their individual upload results are indexed in the [project README](README.md).

## September 4 release installed

The matched update was installed from `/home/nate/mechbot-release-stage.aueeSC`
after the operator confirmed the wheels were powered and raised. The service was
placed in maintenance, 22 files were installed, and the existing service was
restarted. Checks confirmed active/running, zero automatic restarts, all four
fresh DIAG outputs zero, zero encoder counts/errors, and deadman rearm required.
All live CFG values below were preserved. Served dashboard asset hashes and both
tuning export endpoints passed verification.

The preflight bridge retained the exact Maker READY identity. After reconnect,
the board did not emit a new READY; the new bridge correctly identifies Maker
from its exact mapping help line and leaves the unreported firmware token empty.
The installed updater dry run selects Maker and the connected serial port.

Backup: `/home/nate/mechbot-release-backup.8sOvE7`. It contains the existing bridge
entry points, dashboard, available support scripts, firmware source tree and
tuning session, plus `service-before.txt`, `status-before.json` and
`settings-before.json`. `install-files/` and `install-targets.json` preserve the
immediate pre-install file set; `installed-sha256.json`, `status-after.json` and
`settings-after.json` record verification. These are live settings, not a readout
of saved NVS. Later documentation-only changes are recorded separately.

Fresh preflight confirms READY `ESP32_MAKER_MECANUM_IMU_V1`, serial connected,
gamepad disconnected, deadman false, and no calibration or maintenance active.
CFG: PWM 177 on all wheels; heading-kp 0.7, heading-max 0.3,
heading-deadband-deg 1.5, heading-sign 1, heading-enabled 0. Navigation reports
field mode off. Encoder counts are zero with advancing telemetry.

The IMU now reports `OFFLINE`, with initialization repeatedly failing even though
the I2C scan reports `0x4A`. This is a newer observation than the WAIT state below.
A subsequent stopped `IMU RETRY` also failed. Its serial transcript is
`imu-recovery-lines.txt` in the backup folder; `status-after-imu-retry.json`
records OFFLINE, IMU validity false, all four outputs zero and maintenance released.
Only STOP, IMU RETRY and DIAG commands were sent during exclusive serial recovery.
No motor pulse, firmware upload or NVS save was performed. A full Maker/IMU power
cycle was requested. Fresh reports subsequently returned; the operator's physical
power-cycle completion was not separately confirmed at the time of this record.

Stationary recovery check: 30 HTTP samples over 43.8 seconds all had valid IMU
reports younger than two seconds and four fresh zero-output diagnostics. H reset
and reinitialization counters remained at 3 and 1 throughout the sample window;
the bridge again retained the exact Maker READY identity. Report:
`imu-stability-after-power-cycle.json` in the backup folder. This establishes
short stationary recovery, not reliability under motor load. The file name refers
to the requested recovery sequence, not confirmation of the physical action.
At that checkpoint, automatic motor tuning had not run; normal wiring, current
motor-power state and continued raised-wheel supervision were trial prerequisites.

Later the same local date, the operator requested the supervised run. Forward
startup measurements completed, but an RL invalid-encoder count stopped forward
matching. A targeted comparison reproduced the count increase in an all-wheel
sequence after three clean isolated RL runs. Both runs restored the original
settings and reconnected the bridge with all outputs zero. See
[BENCH_RESULTS_2026-09-04.md](BENCH_RESULTS_2026-09-04.md) for results and raw logs;
the earlier stationary-only checkpoint above is historical.

An immediate full retry reproduced all four forward thresholds exactly and
again aborted at the first all-wheel matching sequence. The new invalid count was
on FR rather than RL, so the calibration blocker is not confined to one encoder
channel. The retry restored every original setting, left all outputs zero,
released maintenance and retained valid IMU health with counters 3/1.

All 41 staged file hashes match `mechbot-release-20260904-manifest.json`.
All 77 Python tests and all three C++ host test programs pass on the Pi, including
the actual firmware with fake IO and the blocked-I2C motor-watchdog test. The
updater dry run selects Maker, `esp32:esp32:esp32`, and the connected USB path.
The original `~/mechbot-src` contained only S3 sketches. The staged Maker source
is now installed at `/home/nate/mechbot-src/Maker_Mechbot`.
Staging includes the standalone gamepad sender for its tests only; it must not
replace the deployed service entry point.

The staged Maker sketch compiles with the Pi's ESP32 core 3.3.11 toolchain:
359,751 bytes flash (27%), 27,688 bytes globals (8%). It was not uploaded.
Build artifact: `maker-build/Maker_Mechbot.ino.bin` under the stage directory;
SHA256 `e3e34ac97baef68f9df4da54f662c27d927d297a5a3f28d7a3a31508f7308a55`.
This hash identifies the staged build, not the firmware already on the controller.

## Original installation, inspected before update

`mecanum-gamepad.service` is active, runs as nate with WorkingDirectory=/home/nate,
and launches `/usr/bin/python3 /home/nate/pi_mecanum_gamepad.py`.
The installed gamepad entry point and `/home/nate/mechbot_bridge.py` have the same
SHA256: `f5af203883e105ab08be1516d342cc79414f5bdbdaea4d0cfd2d08059f92fd82`.
They are the previous bridge implementation. The gamepad was disconnected and
deadman false. The serial path was
`/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`.

Telemetry was current but IMU reported `WAIT Q0 G0 A0`, H reset count 2/reinit
count 1, and field/heading correction were off. This was read-only status access;
it did not open a second serial connection, restart the service or move motors.
The old bridge did not retain a firmware READY token, so this observation alone
does not establish the installed firmware build/version.

## Update as one matched set

Stage these files together before replacing the current installation. This list
includes the later Operations additions; the September 4 installation itself
used the smaller historical release recorded above:

- `mechbot_bridge.py`, copied to both existing bridge paths above to preserve the
  service entry point. Do not copy this repository's standalone gamepad sender
  over that deployed path.
- `mechbot_profiles.py` alongside both bridge/updater entry points.
- `mechbot_firmware_update.py`, `mechbotctl.py`, `mechbot_ops.py` and every
  asset in `dashboard/`, including the Operations console and encoder modules.
- Operations support: `mechbot_telemetry.py`, `mechbot_observer.py`,
  `mechbot_recording.py`, `mechbot_evidence.py`, `mechbot_odometry.py`,
  `mechbot_operations.py` and `mechbot_http.py`.
- Optional supervised tool: `maker_auto_tune.py`, with the shared profiles module.
- Documentation: README, roadmap, baseline record and both tuning guides.

Back up the installed files, systemd definition and any tuning-session.json
before replacement. Preserve actual live CFG values; defaults are not a settings
backup. Preserve recordings, saved evidence reports and measured geometry, and
install the full matched module/asset set before restarting. End an active tuning
session before deployment. Older saved sessions lack
board identity: the new bridge keeps their history but refuses to restore their
settings automatically onto an unidentified or different board.

For a bridge-only update, keep the gamepad disconnected and wheels raised and
secured under supervision; motor power off is preferred. Firmware uploads require
motor power off. Install the staged files and restart the existing service. The bridge sends only stopped
commands until identification and a new deadman release. Read status, settings,
served asset hashes and service logs. Verify four encoder displays, Maker 177
starting settings, DIAG frames, and correct WAIT/stale IMU display. Do not apply
defaults or save NVS simply to test deployment. Roll back the backed-up file set
if imports, serial identity, or status fail.

The new bridge identifies exact READY tokens. If the deployed Maker firmware
does not reboot on serial open, it queries `?` and recognizes the exact Maker
mapping line; the UI says its firmware version was not reported. Unknown explicit
READY identities are never overridden by a help response.

## Firmware maintenance

First inspect the plan, without compiling, maintenance changes or upload:

```sh
python3 /home/nate/mechbot_firmware_update.py --board maker --dry-run
```

The updater expects Arduino CLI at `~/.local/bin/arduino-cli` and source under
`~/mechbot-src/`. Maker selects `Maker_Mechbot` and `esp32:esp32:esp32`; S3 selects
`Mechbot_IMU_ESP32` and `esp32:esp32:esp32s3`. Board is identified before compile
and rechecked afterward. The port comes from the connected bridge, not a fixed
USB name. Concurrent updater invocations are rejected. Upload verification
requires a fresh exact READY from the expected target/port; an H line or cached
READY is insufficient. This verifies the firmware identity, not a unique source
commit; record the source revision and build artifact separately.

An actual upload remains a supervised maintenance operation: motor power off,
wheels secured and controller disconnected. The first flash or unidentified
firmware uses manual Arduino target selection. Do not invoke an older installed
S3-only updater on Maker.

## Host checks before installation

Run the README test commands. The simulated UI can be served independently:

```sh
python3 tests/dashboard_demo_server.py --board maker --port 8876
python3 tests/dashboard_demo_server.py --board s3 --port 8877
```

These servers have fake serial IO and reject firmware uploads. They never connect
to the Pi. The automatic tuner remains a separate exclusive serial owner; its
`--bridge-stopped` flag does not stop the service for the operator.
