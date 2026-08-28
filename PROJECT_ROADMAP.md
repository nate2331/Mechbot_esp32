# Mechbot ESP32 Project Roadmap

This document is the shared working plan for the Mechbot ESP32 project. Update it as priorities, decisions, and results change.

## Current Priority: IMU Heading Hold

**Goal:** Keep the robot driving in its intended direction by using the IMU to detect heading drift and apply a steering correction.

### Planned work

- [ ] Confirm the IMU is initialized, calibrated, and producing stable heading data.
- [ ] Define the desired heading when manual drive begins or when heading-hold is enabled.
- [ ] Measure heading error between the desired and current heading.
- [ ] Apply a bounded proportional steering correction to the motor-control command.
- [ ] Add safeguards for sensor dropouts, heading wraparound, and deliberate turns by the operator.
- [ ] Tune and test the control response at low speed before higher-speed trials.
- [ ] Record test results and parameter changes below.

## Near-Term Milestones

1. **Sensor confidence** — Verify stable IMU readings and calibration procedure.
2. **Heading-hold prototype** — Implement and test basic correction while driving straight.
3. **Control tuning** — Adjust correction strength, deadband, and limits for stable behavior.
4. **Navigation integration** — Connect heading hold to higher-level movement and navigation behavior.
5. **Field validation** — Test on the intended surface and document remaining issues.

## Decisions and Notes

| Date | Decision or observation | Owner | Follow-up |
| --- | --- | --- | --- |
|  | Start with IMU-based heading hold on the ESP32. |  |  |

## Test Log

| Date | Test | Result | Next step |
| --- | --- | --- | --- |
|  |  |  |  |

## Backlog

- [ ] Document the robot hardware and wiring.
- [ ] Define the navigation architecture and interfaces.
- [ ] Add repeatable calibration and startup checks.
- [ ] Add diagnostics for sensors and motor commands.
