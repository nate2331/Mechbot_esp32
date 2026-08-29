# Gamepad Integration

## Active control path

`pi_mecanum_gamepad.py` is the Xbox gamepad sender used for automatic control. It
opens `/dev/input/js0`, uses the left bumper as a deadman, sends velocity commands
at 20 Hz, and parses the ESP32 encoder and IMU telemetry. `pi_mecanum_teleop.py`
is the separate interactive keyboard utility; its WASD/QE mapping is intentional
and was not changed.

The ESP32 serial protocol accepts three independent normalized components:

```text
V <forward> <left> <ccw>
```

The active gamepad sender maps left-stick Y to `forward`, left-stick X to `left`,
and right-stick X to `ccw`, with the signs established by its Xbox-compatible
`jstest` mapping.

## Continuous translation and rotation

The previous `full_scale_motion()` selected the single largest axis and emitted a
full-scale cardinal command. The active sender now preserves both normalized
left-stick components and its proportional magnitude. The existing `0.35`
translation deadzone is applied to the vector magnitude, not separately to each
axis, so filtering does not collapse or distort the stick angle.

For a full-scale stick 15 degrees left of forward, the logical command is:

```text
V 0.965926 0.258819 0
```

A simultaneous 0.30 counter-clockwise right-stick command retains the same
translation:

```text
V 0.965926 0.258819 0.30
```

Right-stick X has the same `0.35` center threshold but is evaluated independently
from translation. In the IMU firmware, every motor-effective nonzero rotation
command yields heading hold; releasing the right stick lets heading hold capture
and maintain the new orientation.

## Safety and telemetry

- Releasing the left-bumper deadman immediately sends zero velocity and `X`.
- A missing joystick path or short joystick event is treated as a disconnect;
  the sender's `finally` block requests the same clean stop.
- Commands continue at 20 Hz while connected, comfortably inside the ESP32's
  300 ms command watchdog.
- Center noise produces zero motion, and movement is never synthesized while the
  deadman is released.
- Existing nonblocking `T` encoder and `I` IMU telemetry parsing is unchanged.

The repository does not contain the Pi's service definition. Its existing
auto-start command should continue to execute `pi_mecanum_gamepad.py`; no service
or deployment path change is required for this update.

## Host verification

Run the gamepad tests with:

```bash
python3 -m unittest discover -s tests -p "test_*.py" -v
```

The tests cover cardinal translation, the 15-degree vector, partial magnitude,
simultaneous right-stick rotation, radial/rotation deadzones, deadman release,
disconnect/stop behavior, and existing telemetry parsing.
