# Mechbot ESP32 Project Roadmap

This document is the shared working plan for the Mechbot ESP32 project. Update it as priorities, decisions, and results change.

## Current Priority: IMU Heading Hold

**Goal:** Keep the robot driving in its intended direction by using the IMU to detect heading drift and apply a steering correction.

### Planned work

- [ ] Confirm on hardware that the IMU is calibrated and producing stable heading data.
- [x] Define the desired heading when manual translation begins.
- [x] Measure wrapped heading error between the desired and current heading.
- [x] Apply a bounded proportional steering correction to the motor-control command.
- [x] Add safeguards for sensor dropouts, heading wraparound, and deliberate turns by the operator.
- [ ] Tune and test the control response at low speed before higher-speed trials.
- [ ] Record test results and parameter changes below.

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

## Test Log

| Date | Test | Result | Next step |
| --- | --- | --- | --- |
| 2026-08-28 | Host C++ navigation-math test with warnings as errors | Passed wraparound, bounded correction, quaternion yaw, and field transform cases. | Compile for the ESP32 target and run the documented block tests. |

## Backlog

- [ ] Document the robot hardware and wiring.
- [ ] Define the navigation architecture and interfaces.
- [ ] Add repeatable calibration and startup checks.
- [ ] Add diagnostics for sensors and motor commands.
