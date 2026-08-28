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

## Next Priority: Field-Oriented Control

**Goal:** Transform gamepad movement commands using the IMU heading so pushing forward always moves the robot away from the operator, regardless of the robot's orientation.

### Planned work

- [ ] Define the operator-facing field reference and startup orientation.
- [ ] Convert gamepad forward and sideways commands into field-relative movement commands.
- [ ] Rotate those commands into the robot frame using the current IMU heading.
- [ ] Verify correct behavior at several robot headings, including heading wraparound.
- [ ] Provide a simple way to reset or re-zero the field reference when needed.
- [ ] Test field-oriented control together with heading hold and document the interaction.

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
|  | Start with IMU-based heading hold on the ESP32. |  |  |
|  | Add field-oriented control so gamepad directions are independent of the robot's orientation. |  |  |

## Test Log

| Date | Test | Result | Next step |
| --- | --- | --- | --- |
|  |  |  |  |

## Backlog

- [ ] Document the robot hardware and wiring.
- [ ] Define the navigation architecture and interfaces.
- [ ] Add repeatable calibration and startup checks.
- [ ] Add diagnostics for sensors and motor commands.
