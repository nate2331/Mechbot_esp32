# Gamepad Integration

## Active control path

`pi_mecanum_gamepad.py` is the Xbox gamepad sender used by the Raspberry Pi.
It opens `/dev/input/js0`, uses the left bumper as a deadman, sends commands at
20 Hz, and parses encoder and IMU telemetry.

The ESP32 serial protocol remains:

```text
V <forward> <left> <ccw>
```

## Current full-power cardinal mode

The robot's current mixed motors do not respond consistently to fractional
open-loop PWM. The gamepad therefore selects the dominant stick axis and sends
one full-scale cardinal command, matching the previously proven WASD/QE behavior:

- Left-stick Y: full-speed forward or reverse
- Left-stick X: full-speed strafe left or right
- Right-stick X: full-speed rotate counter-clockwise or clockwise

Only the dominant direction is sent. Diagonal/360-degree proportional motion and
simultaneous translation/rotation are intentionally disabled until per-wheel
closed-loop speed control or minimum-PWM compensation is implemented.

## IMU heading hold

Heading hold remains implemented in
`Mechbot_IMU_ESP32/Mechbot_IMU_ESP32.ino`. During translation, the firmware
uses BNO085 yaw to correct unintended rotation. Deliberate rotation commands take
priority and establish a new held heading when released.

## Safety

- Releasing the left-bumper deadman immediately sends zero velocity and `X`.
- A disconnected joystick requests a clean stop.
- Commands are sent at 20 Hz inside the ESP32's 300 ms watchdog.
- Center noise below the 0.35 threshold produces no motion.

## Host verification

Run:

```bash
python3 -m unittest discover -s tests -p "test_*.py" -v
```
