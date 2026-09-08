# Gamepad Integration

## Active control path

The Pi service launches `/home/nate/pi_mecanum_gamepad.py`. A September 4 read-only
inspection confirmed this installed file is a copy of `mechbot_bridge.py`, not
the standalone gamepad file of the same name in this repository. The bridge owns
serial, gamepad, telemetry, and the tuning HTTP service. Preserve that distinction
when deploying; see DEPLOYMENT.md. Never run both implementations together.

Both implementations open `/dev/input/js0`, use the left bumper as a deadman,
and send at 20 Hz. The updated bridge requires an identified board and a deadman
release after connect, controller reboot, or emergency stop before driving.

The ESP32 serial protocol remains:

```text
V <forward> <left> <ccw>
```

## Current full-power cardinal mode

Cardinal mode originated with the older mixed-motor chassis. The Maker robot now
has four measured encoders, but speed control is not installed and RL has shown
direction-dependent response. The gamepad still selects the dominant axis and sends
one full-scale cardinal command, matching the previously proven WASD/QE behavior:

- Left-stick Y: full-speed forward or reverse
- Left-stick X: full-speed strafe left or right
- Right-stick X: full-speed rotate counter-clockwise or clockwise

Only the dominant direction is sent. Diagonal/360-degree proportional motion and
simultaneous translation/rotation are intentionally disabled until per-wheel
closed-loop speed control or minimum-PWM compensation is implemented.

## IMU heading hold

Heading hold is implemented in both Maker_Mechbot and Mechbot_IMU_ESP32. Maker
defaults it off; the current setting is available through CFG GET. When enabled,
the firmware
uses BNO085 yaw to correct unintended rotation. Deliberate rotation commands take
priority and establish a new held heading when released.

## Safety

- The standalone sender sends zero velocity and X on deadman release. The bridge
  sends zero velocity on its next 20 Hz cycle; emergency stop sends X and requires
  a new deadman release before driving can resume.
- A disconnected joystick requests a clean stop.
- Commands are sent at 20 Hz inside the ESP32's 300 ms watchdog.
- Center noise below the 0.35 threshold produces no motion.

## Host verification

Run:

```bash
python3 -m unittest discover -s tests -p "test_*.py" -v
```
