# Overnight robot development

Started 2026-09-04, America/Chicago. User authorized sustained autonomous software work, full local access, and extensive use of the local model to conserve cloud tokens.

## Later September 5 hardware evidence

The dated software checkpoints below retain their original test counts and
deployment history. After the encoder firmware upload, [manual observations](../test_results/manual-encoder-20260905/README.md)
and [four raised-wheel powered pulses](../test_results/powered-encoder-20260905/README.md)
completed with zero new invalid transitions. Later [pair comparisons and static
matching attempts](../test_results/paired-motors-20260905/README.md) reproduced
invalid transitions and a load-dependent speed mismatch. A further [verified PWM
readback firmware upload and trial](../test_results/pwm-readback-20260905/README.md)
showed equal peripheral duty/frequency while the front wheels remained slower.
All original settings were restored; no shared trim was accepted or saved.
These checks supersede the pending-upload/motion statements at earlier checkpoints.
See the [project README](../README.md) for the subsequent September 6–7 IMU work.

## September 5 continuation

At this software checkpoint, the user deferred the sensor reset wire. Added a
[passive encoder observation window](ENCODER_OBSERVATION.md), with count/edge/error
deltas and downloadable evidence independent of IMU availability. Explicit IMU
OFFLINE telemetry now invalidates the previous heading and displays accurately.
No motor commands, reset wiring changes or firmware upload were added. The local
model produced 1,280 additional tokens for the comparison helper; review and
37 Node tests cover the completed implementation. The PC suite runs 230 Python
tests with one Windows privilege skip.

Encoder-upload checkpoint: the user authorized the encoder firmware upload.
The [upload record](MAKER_FIRMWARE_UPLOAD_2026-09-05.md) confirms a complete
pre-flash backup, verified firmware and unchanged NVS, fresh Maker READY, and
matching saved source on the Pi. The IMU was offline and motion retests had not
yet run at that checkpoint. The continuation history below predates that upload.

The user confirmed phone access to the simulator on the same Wi-Fi. A requested
USB RTS reset from the Pi restored Maker identification and valid IMU readings.
The Operations runtime was then installed and verified on the actual Pi, with
all 222 Python tests passing there and a 1,395-event stationary capture/replay
passing without drops. Live dashboard: `http://192.168.50.166:8765/`.
See [PI_OPERATIONS_DEPLOYMENT_2026-09-05.md](PI_OPERATIONS_DEPLOYMENT_2026-09-05.md)
for current deployment evidence; the original overnight checkpoint below remains
historical. No movement trial or firmware upload was included in that Operations
deployment checkpoint.

A subsequent user power cycle interrupted a Pi-side firmware compile and
exposed a missed startup identity query. The bridge now retries that read-only
query while idle, and the dashboard clears stale network errors on recovery.
The follow-up suite passed all 229 Python tests on the Pi and all 26 Node tests.
The IMU remained offline after the power cycle despite one stopped sensor retry
and one USB reset; this is unresolved. The earlier successful IMU capture remains
historical. Motor output stayed zero. At that checkpoint upload awaited confirmed
motor power off with Pi/ESP32 USB power retained; the later upload is recorded above.
The overnight automation was left paused.
The local model contributed another 1,007 generated tokens across five bounded
responses to the startup-retry task; its incomplete draft was finished and
reviewed before testing and installation.

## Original delivery workspace

These paths and queue details identify the completed overnight work. They are
historical handoff notes, not a new instruction to restart its automation or jobs.

- Working checkout: `C:\Users\nate2\Documents\Codex\2026-09-04\figure-out-why-we-couldn-t\overnight-work`
- Branch: `overnight/operations-console`
- Baseline snapshot: `9006700ca0fd26872a23334dcbb844509ef396c9` (includes all preexisting tracked/untracked source work and the tested encoder sampling patch).
- Original project: `C:\Users\nate2\OneDrive\Documents\Arduino\Mechbot_esp32`
- Local runner and queue live one directory above the working checkout. Model: `gpt-oss-local-tools:latest`, native Ollama API, 16K context. Run one model job at a time, with small tasks and explicit edited-file lists.
- Heartbeat automation: `advance-mecanum-robot-overnight`, hourly in this thread. Pause on verified delivery; do not restart completed local jobs.
- Implementation and integrated software checks are complete. Final delivery is tracked by the parent workspace's `robot-delivery-manifest.json`; its `completed` state and verified hashes are authoritative for that delivery. Subsequent hardware outcomes are linked above.

## Product direction

Build a cohesive Blackbot Two operations console around trustworthy observations: live wheel/IMU health, timestamped capture and replay, saved bench evidence and comparisons, and passive odometry once measured geometry is supplied. Retain the working tuning workflow. This prepares dependable proportional wheel control and later navigation without pretending the existing bench fault is resolved.

## Implementation queue and gates

| ID | Work | State | Completion gate |
|---|---|---|---|
| OPS-01 | Structured telemetry, clock/boot boundaries, wheel RPM and diagnostics | verified | Malformed data, device-time wrap/reboot, stale gaps and both board profiles |
| OPS-02 | Bounded capture, saved recordings and deterministic offline replay | verified | Disk-failure/stop isolation, bounded storage, concurrent activation, no TX playback |
| OPS-03 | Bench report summaries and comparable-run analysis | verified | Three real reports; aborted/simulated/incomparable evidence explicitly distinguished |
| OPS-04 | Measured geometry and passive odometry | verified | Analytic forward/strafe/rotation, gaps/reset; explicit measured dimensions required |
| OPS-05 | Bridge API and CLI integration | verified | Complete Python suite; legacy behavior preserved, endpoint validation and offline CLI checks |
| OPS-06 | Responsive Operations web interface | verified | 22 Node tests and real Edge desktop/mobile workflows; tuning page retained |
| OPS-07 | Demo, documentation, packaging and source reconciliation | final delivery | Local preview, runbook, validation report; inspect delivery manifest for verified original-project copy |

## Original software completion checkpoint

All local generation jobs had ended at software delivery. Do not resume old queue manifests: subsequent review corrected generated files, and those old prompts are obsolete. Future local jobs need new task IDs/prompts.

Validation is documented in [OPERATIONS_VALIDATION.md](OPERATIONS_VALIDATION.md).
The complete Python suite ran 222 tests: 221 passed and one Windows symlink
privilege fixture was skipped. All 22 Node tests and desktop/mobile Edge browser
checks passed, including a separate S3 capability check.
The local coding runner, queue and guarded delivery utility also passed 76
tests (74 passed, two Windows privilege skips). The local model generated
53,537 tokens across 36 attempts and 170 API responses; independent review
corrected drafts before acceptance.

The Maker preview runs at `http://127.0.0.1:8876/`, explicitly simulated, using
the isolated `../robot-venv` runtime (Python 3.12 and pySerial 3.5). Its data is
in `../demo-data`, outside the checkout. Start a fresh preview using the runbook
if the task's process has stopped. The actual robot/Pi service is not deployed
by this software delivery.

For continuation, inspect `../robot-delivery-manifest.json` first. If completed,
do not repeat generation or source reconciliation; the remaining robot gates
below require new physical evidence. The original project's existing edits
are preserved, with replaced-file backups in `../source-reconciliation-backup`.
## Known hardware gates

The later supervised checks reproduced encoder invalid transitions despite the sampling change, so sustained signal reliability remains unresolved. Reverse startup and holding-duty characterization are incomplete. RL speed differences, the all-wheel front slowdown and intermittent IMU behavior remain observations, not confirmed component diagnoses. Loaded diameter, wheelbase and track width are not measured. Physical movement and firmware upload were not part of unattended overnight validation; their later supervised records are linked above.

## Evidence and checkpoints

- Earlier encoder change: ESP32 build and all six baseline/staged host tests passed; source file hashes verified on apply.
- Detailed initial local-model repair: `../LOCAL_MODEL_FINDINGS.md`.
- Current overnight source manifest and baseline metadata: `../overnight-source-manifest.json`, `../overnight-baseline.json`.
- The working branch contains the preserved baseline and reviewed Operations release. The parent workspace delivery summary records the final commit, ZIP hash, test artifacts and backup location. No original-branch commit or push is performed.
