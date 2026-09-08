# Blackbot Two operations runbook

The Operations console and its capture, observer, CLI and browser integration
passed the software checks recorded in [OPERATIONS_VALIDATION.md](OPERATIONS_VALIDATION.md).
The September 5 [Pi installation record](PI_OPERATIONS_DEPLOYMENT_2026-09-05.md)
confirms real telemetry and passive capture/replay at `http://192.168.50.166:8765/`.
Physical motion validation remains a separate step. Consult
[OVERNIGHT_PROGRESS.md](OVERNIGHT_PROGRESS.md) for source delivery status.
Earlier Pi installation results in
[DEPLOYMENT.md](../DEPLOYMENT.md) do not establish that these additions are installed.

## Pages and operating modes

The Overview's [Encoder check](ENCODER_OBSERVATION.md) compares counts and
diagnostic deltas without requiring the IMU. Start/finish/download are passive
observation controls. They do not enable motors or reset encoder counters.

After a restart, the bridge retries a missing controller identification every
two seconds while idle. The dashboard clears its network failure notice when
polling recovers. An identified board can still have an offline IMU: inspect
the stream status before navigation. The September 5 power-cycle follow-up
left the IMU offline despite a stopped reinitialization and USB reset.

- `/` is the new Operations console: live wheel/IMU observations, recordings,
  bench evidence, measured geometry and passive pose.
- `/tuning` retains the existing supervised PWM tuning workflow. Operations does
  not replace its settings, session restoration or board checks.
- **Live** means observations from the bridge, not proof that every stream is
  fresh. Check each wheel/IMU age and readiness reason.
- **Simulated** identifies the local demonstration. Its motor/encoder activity
  is generated software data and is not a robot result.
- **Offline replay** reads a saved capture into a separate observer. Historical
  TX commands are displayed as evidence and are never retransmitted. Geometry
  editing and new capture are disabled in the replay view. The button labelled
  **STOP MOTORS remains a live stop request**, including while viewing a replay.

The Maker profile has four encoders and measured counts per revolution. The
legacy S3 has rear encoders only; missing wheel measurements or calibration must
remain unavailable. Maker and S3 PWM baselines are different.

## Local preview

Run from the repository root with Python 3.10 or newer:

```sh
python tests/dashboard_demo_server.py --port 8876 --data-dir ./local-demo
```

Open `http://127.0.0.1:8876/`; use `/tuning` for the retained page. The server binds
loopback, uses fake serial IO and does not connect to the Pi. `--board s3` selects
the legacy demonstration; Maker is the default. `--data-dir` keeps demo captures
under `local-demo/recordings`; omitting it creates a temporary directory. The
demo reads existing bench reports from `test_results/`. Stop the server with
Ctrl+C. Demo recordings must keep their simulated provenance when exported.

For a phone on the same Wi-Fi, add `--lan-address PC_PRIVATE_IPV4` (for example,
`--lan-address 192.168.50.242`). Open `http://PC_PRIVATE_IPV4:8876/` on the phone;
`127.0.0.1` on the phone points to the phone itself. This option adds the named
private interface while preserving the PC's loopback preview. The PC must stay
awake, and Windows Firewall must allow TCP 8876 from the local subnet on the
Private network profile. Do not forward this unauthenticated simulator through
the router. Without this explicit option, it continues to bind loopback only.

## Capture and evidence

Start a recording with a useful label, surface/load description and measured
battery voltage if available. Capture includes raw received lines, transmitted
commands and connection boundaries, with monotonic offsets and initial board,
geometry and provenance metadata. Missing conditions remain unknown.
Finish the capture before changing geometry or resetting the live pose origin.
Replay integrates from a recording-relative origin; the live pose at capture
start is retained in metadata, rather than silently treated as the replay origin.
Capture also snapshots saved calibration with its firmware and encoder scales,
so replay can restore it after a matching controller reconnects. Calibration
from a different board remains inapplicable.

The default contract limits each recording to **5,000,000 bytes and 10,000
accepted events**, including file overhead. Raw lines are limited to 1,024
characters and 4,096 UTF-8 bytes; metadata is limited to 8 KB. A bounded queue
keeps disk work off the serial/control thread. Watch `written`, `dropped`, `bytes`
and `error`; a dropped event makes the finalized capture **incomplete**. These
are per-recording limits, not an automatic archive-retention policy.

Stopping drains the queue. A slow disk can leave the state `stopping` after the
bounded five-second wait; poll until it finishes. A completed or incomplete
capture is atomically renamed from `.part` to `.jsonl`. Disk failures report
`error`; interrupted `.part` files are retained for diagnosis but are not listed
or replayed as completed captures. This release does not recover partial files.

Report discovery reads `test_results/<report-id>/results.json`. Compare the same
wheel, direction, requested duty, command fraction and known setup. An aborted
run can contain useful forward-start measurements without establishing reverse
behavior or a valid shared trim. Pulse-average counts include startup; use the
tuner's powered steady window when comparing speed. Unknown setup, simulated
data, missing reverse data and incomparable measurements must stay labelled.

## Offline command line

The `mechbot_ops.py` interface is separate from the live bridge CLI.
These operations read saved data and print JSON; they do not open
serial, send motion, start a server or apply settings:

```sh
python mechbot_ops.py reports --directory ./test_results
python mechbot_ops.py compare LEFT_REPORT_ID RIGHT_REPORT_ID --directory ./test_results
python mechbot_ops.py recordings --directory ./local-demo/recordings
python mechbot_ops.py replay RECORDING_ID --directory ./local-demo/recordings --until 2.5 --json
```

Use an ID returned by listing, not a filename or arbitrary path. A recording ID
is 32 lowercase hexadecimal characters. Omitting `--until` replays the complete
recording; times must be finite and nonnegative. `--directory` selects reports
for `reports`/`compare` and captures for `recordings`/`replay`. Defaults are rooted
beside the script, not in the shell's current directory. Successful stdout is one
JSON value; errors go to stderr with exit status 2. `--json` is accepted for
replay, and every command already returns JSON.

The existing `mechbotctl.py` talks to the live bridge at `127.0.0.1:8765` and
includes stop/settings/save/reset actions. It is not the offline replay tool.

## Operations API contract

The routes are defined in [mechbot_http.py](../mechbot_http.py), with isolated
observation/replay behavior in [mechbot_operations.py](../mechbot_operations.py).
The integration suite exercises these routes using fake transport and temporary storage.

| Method and path | Input or result |
| --- | --- |
| `GET /api/operations` | Observed telemetry, capture state, configuration errors, bridge state and live/simulated mode |
| `GET /api/recordings` | Finalized capture summaries |
| `GET /api/recordings/ID` | Capture metadata, events, completion state and drops |
| `POST /api/recordings/start` | Optional `label`, `surface`, `load`, `battery_voltage`; starts passive capture |
| `POST /api/recordings/stop` | `{}`; requests bounded capture shutdown |
| `POST /api/replay` | `{"id":"ID","until_s":2.5}`; independent offline snapshot, no transmission |
| `GET /api/evidence` | Saved report summaries and per-report errors |
| `POST /api/evidence/compare` | `{"left":"LEFT_REPORT_ID","right":"RIGHT_REPORT_ID"}` |
| `POST /api/geometry` | All three measured numbers: `wheel_diameter_m`, `wheelbase_m`, `track_width_m` |
| `POST /api/odometry/reset` | `{}`; resets the passive estimate, not physical position or firmware encoders |

Geometry uses metres and full wheel-centre spacings. All three values must be
finite, positive and at most 3 m; those software bounds do not supply calibration.
Four measured encoder scales are also required. Saving geometry starts a new
estimate and associates the calibration with its matching board profile.

## Pi installation and dependencies

The inspected installation uses `mecanum-gamepad.service`, user `nate`, working
directory `/home/nate`, and this command:

```text
/usr/bin/python3 /home/nate/pi_mecanum_gamepad.py
```

That installed file is a copy of **`mechbot_bridge.py`**. This repository's
standalone `pi_mecanum_gamepad.py` is a different sender and must not replace it.
The documented update method stages a matched file set, backs up service/files
and live settings, copies the bridge to both installed bridge entry points, and
restarts the existing service. See [DEPLOYMENT.md](../DEPLOYMENT.md) for the
recorded stage/backup paths and supervised installation procedure. There is no
new service installer in this release.

The live bridge requires Python 3.10 or newer and the `serial` module supplied by
pySerial. Install the pinned bridge dependency with
`python -m pip install -r requirements-bridge.txt`. The PC validation environment
uses Python 3.12. Confirm imports and run the tests under the service's interpreter
before installation.
The demo provides fake serial if pySerial is absent. Operations math, recording,
HTTP and offline CLI are designed to use the standard library; the browser uses
local static assets. Node and a C++17 compiler are host-check tools, not browser
runtime dependencies.

Ship the bridge, profiles, updater, live/offline CLIs, **all** dashboard assets
and the Operations support modules as one set: `mechbot_telemetry.py`,
`mechbot_observer.py`, `mechbot_recording.py`, `mechbot_evidence.py`,
`mechbot_odometry.py`, `mechbot_operations.py` and `mechbot_http.py`. Preserve
`tuning-session.json`, captures, reports and measured calibration. Keep one
serial owner. Do not start the standalone gamepad sender or automatic tuner
alongside the bridge.

Bridge storage defaults are beside its script. Override them with
`MECHBOT_RECORDING_DIR`, `MECHBOT_REPORT_DIR` and `MECHBOT_GEOMETRY_FILE` for
captures, report folders and `measured-geometry.json`, respectively. Ensure the
service user can write capture/calibration directories. Install every support
module before restarting; an import failure is a deployment failure.

Firmware compilation separately requires Espressif ESP32 core 3.x and Adafruit
BNO08x with its dependencies. The existing updater expects
`~/.local/bin/arduino-cli` and source under `~/mechbot-src/`; Maker's target is
`esp32:esp32:esp32`. A bridge/Operations update does not upload firmware.

## Verification and limits

Run the [README checks](../README.md#software-checks), the new Operations test
modules and syntax checks for every Operations JavaScript file. Then exercise
live/simulated labels, capture-stop-replay, scrubbing, gaps, evidence comparison,
geometry rejection and the retained tuning page in the local demo. Record actual
results in the progress ledger; do not infer them from this checklist.

The sampling patch was [uploaded and flash-verified on September 5](MAKER_FIRMWARE_UPLOAD_2026-09-05.md).
The subsequent [manual observations](../test_results/manual-encoder-20260905/README.md)
and [four short powered pulses](../test_results/powered-encoder-20260905/README.md)
were clean, but later [pair/matching trials](../test_results/paired-motors-20260905/README.md)
reproduced invalid transitions. [PWM peripheral readback](../test_results/pwm-readback-20260905/README.md)
also confirmed equal duty/frequency during the unexplained all-wheel front
slowdown. No accepted trim was saved. Signal reliability, repeatable speed under
load, reverse startup/holding measurements, integrated IMU reliability and
missing geometry remain the gates in
[ROBOT_ROADMAP.md](ROBOT_ROADMAP.md).

Captures contain raw robot commands, settings and operator-entered notes; review
them before sharing. The bridge defaults to `0.0.0.0:8765` and has no application
authentication. Use loopback or a trusted local network; this is not a secure
public service. Browser action requests must originate from the dashboard's
own host; local CLI requests remain supported. This origin check does not add
authentication. The demo defaults to loopback only; the explicit `--lan-address`
option also exposes the simulator to the selected private network.
