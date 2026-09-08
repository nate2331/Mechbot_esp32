# Mechbot project roadmap

## Latest checkpoint: September 7, documented September 8

The [weekend record](docs/WEEKEND_PROGRESS_2026-09-04_TO_07.md) and
[README](README.md) supersede the historical status sections below.

- Board-aware Maker tooling and the Operations console are integrated and were
  installed on the Pi September 5. Recording/replay, evidence comparison, and
  passive odometry are implemented; measured geometry and floor validation remain.
- The encoder sampling patch was uploaded and physically retested. Manual and
  short powered checks passed, but invalid transitions recurred in pair/matching
  trials; no shared PWM trim was accepted or saved.
- Frequency sweeps support a useful 200–300 Hz region. 248 Hz is a working test
  setting, not a unique proven optimum or a resolved electrical root cause.
- The standalone Maker 5 V/buffered UART-RVC receiver reached 37m51s and 227,501
  valid frames with clean communication counters. Integrated Maker robot source
  still uses the experimental SPI adapter. RVC robot integration remains open.
- The separate 248 Hz RVC movement sketch compiled and passed seven host scenarios;
  upload and physical movement testing remain pending. Perfboard/reset assembly,
  motor-noise tolerance, and IMU accuracy also remain unverified.

Next: preserve/retest the working RVC setup, verify the perfboard/reset stage,
run the supervised movement experiment, integrate reliable IMU telemetry, and
finish encoder/PWM characterization before floor driving and feedback control.
The ordered hardware gates are maintained in [docs/ROBOT_ROADMAP.md](docs/ROBOT_ROADMAP.md).

## Historical September 4 assessment

The assessment and ordered plan below record the September 4 checkpoint after
reviewing the Maker setup and follow-up tuning chats. Later dated additions and
the latest checkpoint above take precedence. Physical results are recorded in
[HARDWARE_BASELINE.md](HARDWARE_BASELINE.md).

### Status at September 4

The Maker ESP32 Pro is installed. All six directions were exercised with the
wheels raised, including sustained 177-PWM trials. Encoder direction and watchdog
checks passed. All four encoders were measured over ten manual wheel revolutions
and agree within 0.23%. These tests do not establish loaded/floor performance.

The latest [bench results](BENCH_RESULTS_2026-09-04.md) measured forward starting
PWM 119/119/155/131 twice. Both full runs stopped before reverse testing on a new
invalid transition during the first all-wheel matching sequence—RL in the first
run and FR in the retry. All runs restored original settings.
These unresolved observations determine the next tests:

- BNO085 sometimes supplies no reports and does not recover from stopped retry.
  Fresh reports returned after the September 4 retry failed and a power cycle was
  requested. A 43.8-second stationary check passes; load reliability is unresolved.
- RL has direction-dependent speed/coast behavior. The slower behavior persisted
  after swapping M1/M2 motor outputs; original wiring was subsequently restored.
  Suspected motor/gearbox drag is not a confirmed component diagnosis.
- Invalid encoder transitions increased during all-wheel sequences on two
  channels: RL in the first run/targeted diagnostic and FR in the full retry.
  Three isolated RL pulses had no new errors. Inspect shared signal integrity,
  grounding and encoder sampling under interrupt load before speed matching.

The existing Pi dashboard was built for the former S3/rear-encoder setup.
An automatic Maker bench tuner was completed and tested offline in a separate
September 4 chat folder but had not been integrated here. Navigation components
also exist in `C:/Users/nate2/OneDrive/Documents/Mecha`; its TASK-001 through
TASK-030 ledger describes completed offline work, not autonomous robot validation.
Do not rebuild those components from scratch or replace this firmware blindly.

## Ordered work and completion gates

1. **Consolidate the known baseline.** Preserve measured CPR, motion results,
   normal wiring, unresolved RL/IMU behavior and links to related work. Identify
   firmware and installed Pi service before deployment. Status: documentation
   complete for recovered history and a read-only September 4 Pi inspection;
   deployed details are in DEPLOYMENT.md.
2. **Complete Maker tooling integration.** Bring the existing automatic tuner and
   its tests into this repository. Make bridge/dashboard/updater select the board
   from its firmware identity; show all four Maker encoders and diagnostics;
   separate S3 and Maker starting settings; export session JSON/CSV. Require exact
   target matching and a fresh READY after upload. Status: implemented and installed;
   automated tests and simulated browser review recorded below. Matched update
   staged and verified on Pi, with installed files and live settings backed up.
   Service restarted after operator confirmed wheels powered and raised. Four
   zero-output diagnostics, settings preservation, served assets and exports
   verified. See DEPLOYMENT.md for exact stage/backup paths and results.
3. **Resolve the next supervised bench session.** First obtain fresh BNO085
   quaternion, gyro and acceleration reports (stationary recovery now recorded;
   recheck at trial start and under load). Inspect RL mechanical resistance
   and record whether a replacement is being used. Run the automatic tuner only
   with normal wiring and wheels raised. It measures repeated starts, minimum
   reliable starting PWM, steady speed and both directions, capped at 177 PWM.
   Gate: saved report with original settings restored, or a useful recorded
   failure identifying the next repair. Do not force a shared trim when the two
   directions require incompatible corrections.
   Current result: useful partial failure recorded in BENCH_RESULTS_2026-09-04.md.
   Forward startup is measured and reproduced; encoder validity during
   simultaneous wheel operation is now the immediate blocker. Reverse and
   shared-trim tests remain.
4. **Establish a floor baseline.** Apply a bench-verified candidate live, then
   repeat forward/reverse/left/right on one surface at controlled battery/load.
   Heading correction stays off. Gate: repeated results in all directions before
   saving. No existing raised-wheel result marks this complete.
5. **Introduce wheel-speed control and proportional input.** Use measured motor
   response as feed-forward, with per-wheel feedback, bounded output, integral
   limits, zero-speed behavior and sensor-fault stops. Preserve the independent
   watchdog and reversal pause. Test offline first and leave control disabled
   until supervised verification. Gate: repeatable low-speed response under load
   before enabling continuous two-axis gamepad translation/mixed turns.
6. **Validate heading hold and field-relative driving.** Require reliable IMU
   operation; verify correction sign, deliberate turns, wraparound, sensor loss
   and headings 0/+90/-90/180 degrees. Gate: logged low-speed floor tests without
   oscillation or unexpected frame changes.
7. **Integrate existing navigation work selectively.** Review the Mecha modules
   and protocol extensions against this project's bridge and control ownership.
   Measure wheel diameter, wheelbase and track width. Start with passive odometry,
   replay and simulation; integrate command arbitration before motion primitives
   or waypoint execution. Gate: explicit interface review and physical validation;
   offline completion in the other repository does not satisfy that gate.

## Recorded software verification

- Initial assessment: 28 existing Python tests pass; dashboard JavaScript syntax
  check passes. No serial hardware opened.
- Recovered automatic tuner: included with its 25 offline tests, using the shared
  measured encoder scales. The combined suite passes 77 Python tests, including
  board selection, session recovery, exact upload verification, four-wheel
  comparison/export, invalid IMU data and in-test encoder loss.
- Dashboard JavaScript syntax passes. Simulated Maker browser workflow confirms
  177 starting settings, four-wheel response cards, completed bounded tests and
  original-setting restore. S3 retains rear-only displays and its own baseline.
  Both browser checks reported zero console errors.
  Firmware and native C++ control code were not changed in this milestone.
- Live read-only check: older bridge active, gamepad disconnected, IMU still
  WAIT Q0 G0 A0. No service restart, upload or physical motion test was performed.
- Subsequent deployment preflight: exact Maker READY and live 177-all settings
  confirmed; IMU now OFFLINE despite I2C detection at 0x4A. All 77 Python tests and
  three C++ host programs pass in an isolated Pi stage. Updater dry run selects
  Maker correctly. Existing installation and settings backed up; service unchanged.
- Deployment completed afterward: service active with zero restarts, all four
  motor outputs zero, live settings unchanged, dashboard hashes and exports
  verified. Maker firmware builds successfully on Pi (27% flash, 8% globals);
  no upload. Stopped IMU retry failed and a full Maker/IMU power cycle was requested.
- Fresh IMU reports subsequently returned. Thirty stationary samples over 43.8 s
  all passed freshness and zero-output checks; reset/reinit counters stayed 3/1.
  This is short stationary recovery, not a completed automatic motor-tuning trial.

## Historical S3 tuning results

These entries describe the previous hardware/deployment. Their PWM settings and
rear-only encoder assumptions must not be transferred to Maker.


| Date | Test | Result | Next step |
| --- | --- | --- | --- |
| 2026-08-28 | Host C++ navigation-math test with warnings as errors | Passed wraparound, bounded correction, quaternion yaw, field transform, 15-degree continuous translation, and simultaneous turn cases. | Compile for the ESP32 target and run the documented block tests. |
| 2026-08-29 | Python gamepad sender unit tests | Passed 8 cases covering cardinal/diagonal/partial translation, mixed turn, deadzone, deadman, disconnect/stop, and telemetry parsing. | Validate the deployed controller mapping and motor response on hardware. |
| 2026-08-29 | Manual rear-only PWM strafe test | `FL 230, FR 230, RL 200, RR 200` produced a substantially straighter left/right strafe than ratio-based rear encoder matching. Rear cumulative counts still differed (`44782` vs `61877`), showing raw RL/RR ticks are not yet comparable distance units. | Use 230/230/200/200 as the manual chassis baseline; do not automatically ratio-match rear PWM until encoder scale/CPR is characterized. Validate forward, reverse, and rotation before saving permanently. |
| 2026-08-30 | Guided PWM workflow simulation and browser QA | Passed 28 host tests plus complete set/test/observe/recommend/apply/repeat and prior-best rollback browser cycles at desktop and phone breakpoints. Confirmed stale-encoder rejection, matched-setup rate comparison, mecanum trim geometry, safe heading-state restore before save, zero modal overflow, and zero browser console errors. | Run the documented wheels-up response check and floor A/B sequence on the actual robot. |
| 2026-08-30 | Pi tuning-dashboard deployment | Passed all 21 bridge tests on the Pi before installing the bridge as both `/home/nate/pi_mecanum_gamepad.py` and `/home/nate/mechbot_bridge.py`, the four dashboard assets, and the tuning guide. Restarted `mecanum-gamepad.service`; verified served asset hashes, advancing rear-encoder timestamps, updated tuning API, zero service restarts, and the existing tailnet-only proxy. Preserved live PWM `230/230/205/205` and all heading settings (heading correction off); no motor pulses, firmware flashing, or NVS save. Previous installation, session, service definition, and settings are backed up at `/home/nate/mechbot-tuning-backup.ToQNNh`. | Refresh the tuning page and perform supervised wheels-up and floor trials; physical tuning remains unverified. |
