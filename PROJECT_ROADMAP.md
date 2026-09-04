# Mechbot ESP32 Project Roadmap

This document is the shared working plan for the Mechbot ESP32 project. Update it as priorities, decisions, and results change.

## Current Priority: Tuning Platform

**Goal:** Provide a persistent Pi bridge and a safe browser console for live
telemetry, runtime tuning, emergency stop, and firmware maintenance.

### Delivered

- [x] Persistent single-owner serial/gamepad bridge with reconnect behavior.
- [x] Local HTTP status, settings, stop, and maintenance API.
- [x] Runtime motor and heading settings with ESP32 NVS persistence.
- [x] Command-line control and guarded single-job Pi firmware updater.
- [x] Responsive Pi-hosted tuning console with safety state, telemetry,
  encoder counts, serial events, tuning controls, and emergency stop.
- [x] In-session four-motor PWM workbench with test-output and duration controls,
  bounded pulses, automatic stop, approval-gated apply/save, abort, and rollback.
- [x] Rear-encoder-aware A/B feedback: compare RL only with prior RL runs and RR
  only with prior RR runs when direction, output, and duration match.
- [x] Chassis-observation recommendations that convert heading/path error into a
  small mecanum-aware four-wheel PWM trim without ratio-matching raw rear ticks.
- [x] Rear-encoder bench mode for response and stall-threshold checks. The
  four-encoder speed-calibration workflow remains locked pending the upgrade.

### Next work

- [x] Add firmware-update progress and maintenance controls to the console.
- [x] Add short rolling telemetry history and persistent tuning-session recovery.
- [ ] Add tuning-session JSON/CSV export for offline comparison.
- [x] Add authenticated remote access through Tailscale at
  `https://mechpi.tail861f16.ts.net/` (tailnet-only; public Funnel disabled).
- [ ] Perform blocked-drive and low-speed control tests when hardware is ready.
- [ ] Add IMU-assisted on-floor drift calibration after BNO085 reset recovery is
  reliable. Use bounded low-speed runs with heading hold temporarily disabled,
  measure net yaw drift, trim front-pair imbalance, and retest. Treat this as
  chassis-level correction, not direct front-wheel speed measurement.

## Paused Hardware Work: IMU Heading Hold

**Goal:** Keep the robot driving in its intended direction by using the IMU to detect heading drift and apply a steering correction.

### Planned work

- [ ] Confirm on hardware that the IMU is calibrated and producing stable heading data.
- [x] Define the desired heading when manual translation begins.
- [x] Measure wrapped heading error between the desired and current heading.
- [x] Apply a bounded proportional steering correction to the motor-control command.
- [x] Add safeguards for sensor dropouts, heading wraparound, and deliberate turns by the operator.
- [ ] Tune and test the control response at low speed before higher-speed trials.
- [ ] Record test results and parameter changes below.

**Paused 2026-08-29:** The BNO085 reports correctly after full power removal,
then can reset and block inside SH-2 report recovery. Its RST pin is not wired.
Planned recovery connection is BNO085 RST to unused ESP32-S3 GPIO 42; the local
firmware source is prepared for that connection but has not been flashed.

## Next Priority: Field-Oriented Control

**Goal:** Transform gamepad movement commands using the IMU heading so pushing forward always moves the robot away from the operator, regardless of the robot's orientation.

### Planned work

- [x] Define the operator-facing field reference and startup orientation.
- [x] Convert gamepad forward and sideways commands into field-relative movement commands.
- [x] Rotate those commands into the robot frame using the current IMU heading.
- [ ] Verify correct behavior on hardware at several robot headings, including heading wraparound.
- [x] Provide a simple way to reset or re-zero the field reference when needed.
- [ ] Test field-oriented control together with heading hold on hardware.

## Near-Term Milestones

1. **Sensor confidence** — Verify stable IMU readings and calibration procedure.
2. **Heading-hold prototype** — Implement and test basic correction while driving straight.
3. **Field-oriented prototype** — Implement field-relative gamepad control.
4. **Control tuning** — Adjust correction strength, deadband, and limits for stable behavior.
5. **Navigation integration** — Connect heading hold and field-oriented control to higher-level movement and navigation behavior.
6. **Field validation** — Test on the intended surface and document remaining issues.

## Decisions and Notes

| Date | Decision or observation | Owner | Follow-up |
| --- | --- | --- | --- |
| 2026-08-28 | Heading hold is enabled by default and has no routine disable command. | Nate | Tune conservative starting values on hardware. |
| 2026-08-28 | Field-oriented control remains independently toggleable and captures field zero when enabled. | Nate | Validate IMU mounting sign and operator reference. |
| 2026-08-28 | Field-oriented translation stops if its required heading is stale or invalid. | Codex | Exercise IMU reset/dropout during block testing. |
| 2026-08-29 | `pi_mecanum_gamepad.py` is the active Xbox sender; preserve two-axis translation instead of dominant-axis quantization. | Nate | Deploy through the existing Pi auto-start configuration and validate on blocks. |

## Test Log

| Date | Test | Result | Next step |
| --- | --- | --- | --- |
| 2026-08-28 | Host C++ navigation-math test with warnings as errors | Passed wraparound, bounded correction, quaternion yaw, field transform, 15-degree continuous translation, and simultaneous turn cases. | Compile for the ESP32 target and run the documented block tests. |
| 2026-08-29 | Python gamepad sender unit tests | Passed 8 cases covering cardinal/diagonal/partial translation, mixed turn, deadzone, deadman, disconnect/stop, and telemetry parsing. | Validate the deployed controller mapping and motor response on hardware. |
| 2026-08-29 | Manual rear-only PWM strafe test | `FL 230, FR 230, RL 200, RR 200` produced a substantially straighter left/right strafe than ratio-based rear encoder matching. Rear cumulative counts still differed (`44782` vs `61877`), showing raw RL/RR ticks are not yet comparable distance units. | Use 230/230/200/200 as the manual chassis baseline; do not automatically ratio-match rear PWM until encoder scale/CPR is characterized. Validate forward, reverse, and rotation before saving permanently. |
| 2026-08-30 | Guided PWM workflow simulation and browser QA | Passed 28 host tests plus complete set/test/observe/recommend/apply/repeat and prior-best rollback browser cycles at desktop and phone breakpoints. Confirmed stale-encoder rejection, matched-setup rate comparison, mecanum trim geometry, safe heading-state restore before save, zero modal overflow, and zero browser console errors. | Run the documented wheels-up response check and floor A/B sequence on the actual robot. |
| 2026-08-30 | Pi tuning-dashboard deployment | Passed all 21 bridge tests on the Pi before installing the bridge as both `/home/nate/pi_mecanum_gamepad.py` and `/home/nate/mechbot_bridge.py`, the four dashboard assets, and the tuning guide. Restarted `mecanum-gamepad.service`; verified served asset hashes, advancing rear-encoder timestamps, updated tuning API, zero service restarts, and the existing tailnet-only proxy. Preserved live PWM `230/230/205/205` and all heading settings (heading correction off); no motor pulses, firmware flashing, or NVS save. Previous installation, session, service definition, and settings are backed up at `/home/nate/mechbot-tuning-backup.ToQNNh`. | Refresh the tuning page and perform supervised wheels-up and floor trials; physical tuning remains unverified. |

## Backlog

- [ ] Document the robot hardware and wiring.
- [x] Update the active `pi_mecanum_gamepad.py` to preserve continuous two-axis translation and its deadman/watchdog behavior.
- [ ] Define the navigation architecture and interfaces.
- [ ] Add repeatable calibration and startup checks.
- [ ] Add diagnostics for sensors and motor commands.
