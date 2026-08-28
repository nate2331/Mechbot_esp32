# Gamepad Integration Contract

The ESP32 serial protocol already accepts three independent normalized values:

```text
V <forward> <left> <ccw>
```

`forward` and `left` are simultaneous continuous translation components; neither
is reduced to a cardinal direction. `ccw` is mixed at the same time, so the robot
can translate and intentionally rotate together. In field-oriented mode, only
the translation vector is rotated into the robot frame; `ccw` remains intentional
robot rotation. Heading hold yields for a motor-effective `ccw` command of at
least `0.01` and captures the new heading while the operator turns.

For a full-scale left stick 15 degrees left of forward, the sender must emit both
components:

```text
V 0.965926 0.258819 0
```

A simultaneous 0.30 counter-clockwise right-stick command must preserve them:

```text
V 0.965926 0.258819 0.30
```

The host tests cover both commands at the ESP32 mecanum mixer and verify that all
four wheel outputs retain the combined vector.

## Missing deployed sender source

The repository does not contain the deployed `pi_mecanum_gamepad.py`. That exact
Raspberry Pi source file is the remaining artifact needed to remove any
dominant-axis or WASD-style quantization before commands reach the ESP32. It must
be supplied from the robot's deployed Pi environment and added to the repository;
this change does not invent a replacement disconnected from the active controls.

When supplied, its mapping must be updated and tested so that:

- left-stick vertical maps continuously to `forward`;
- left-stick horizontal maps continuously to `left`;
- right-stick horizontal maps continuously to `ccw`;
- the left-stick deadband preserves the two-axis angle;
- the existing deadman, disconnect stop, and command cadence remain intact for
  the ESP32's 300 ms watchdog.
