# IMU Navigation Controls

The navigation controls are implemented in
`Mechbot_IMU_ESP32/Mechbot_IMU_ESP32.ino`. The non-IMU sketch is unchanged.

## Behavior and serial commands

Heading hold is always enabled. When a translation starts, the firmware captures
the current BNO085 yaw. With no deliberate turn input, it adds a bounded
proportional `ccw` correction to hold that yaw. A `ccw` input of at least `0.01`
always takes priority and continuously moves the target to the current yaw, so
releasing the turn stick holds the new direction. Heading hold does not rotate an
idle robot. If the quaternion is missing, invalid, more than 500 ms old, or still
has SH-2 accuracy status `0` (unreliable), the correction is bypassed and the
original robot-oriented command remains available.

Field-oriented control is independent and defaults off:

| Command | Result |
| --- | --- |
| `F 1` | Enable field-oriented translation and use the current yaw as field zero. |
| `F 0` | Disable field-oriented translation. |
| `Z` | Re-zero the field reference to the current yaw. |
| `V <forward> <left> <ccw>` | Drive using the selected translation frame. |
| `X` | Stop immediately. |

Before `F 1`, point the robot in the direction that should mean "away from the
operator." Field `forward` and `left` inputs are rotated into the robot frame
using the yaw relative to that captured direction. Deliberate `ccw` input remains
robot rotation and is not field-rotated. `F 0`, `F 1`, and `Z` stop the motors so
the next `V` command starts cleanly in the new frame.

Field-oriented translation requires a fresh yaw because silently reverting its
frame would produce surprising movement. Enabling it without a valid yaw is
rejected. If yaw becomes unavailable while it is enabled, a translation command
stops the motors. An IMU reset disables field mode and requires `F 1` again.

## Telemetry

The firmware adds one navigation line at the existing 200 ms telemetry interval:

```text
N <ms> <yaw_rad> <target_rad> <error_rad> <correction> <hold> <field> <ready>
```

`hold` is `1` because heading hold is the normal mode. `field` reports the field
toggle, and `ready` reports whether the heading quaternion is fresh and valid.
The target is `0` until translation captures one.

## Starting values that require hardware tuning

| Setting | Initial value | Purpose |
| --- | ---: | --- |
| Heading gain | `0.70` command/radian | Converts wrapped yaw error into turn command. |
| Maximum correction | `0.30` | Prevents heading hold from commanding an excessive turn. |
| Heading deadband | `1.5` degrees | Avoids hunting around the target. |
| Manual-turn threshold | `0.01` | Treats every motor-effective rotation command as deliberate. |
| IMU stale timeout | `500` ms | Rejects old quaternion data. |
| Minimum IMU status | `1` | Rejects an SH-2 heading still marked unreliable. |

These are conservative starting points, not validated tuning. The BNO085 axis
orientation, mecanum wheel direction, magnetic environment, surface traction,
center of mass, and motor matching all affect the correct signs and gains.

## Hardware test requirements

1. Put the chassis on blocks. Confirm positive `ccw` produces positive reported
   yaw and that a positive heading error commands the correction direction needed
   to reduce the error. Reverse the correction sign if the installed IMU axes do
   not match this assumption.
2. On the floor at low speed, command straight forward and sideways motion. Verify
   that drift correction converges without oscillating; tune gain, limit, then
   deadband in that order.
3. Hold translation while commanding a deliberate turn. Confirm the turn is not
   opposed and that the new heading is held after turn input returns to zero.
4. Point the robot away from the operator, send `F 1`, and test forward/left input
   at relative headings near 0, +90, -90, and 180 degrees.
5. Exercise yaw wraparound near +pi/-pi and verify there is no full-turn command.
6. While field-oriented mode is active, disconnect or reset the IMU and confirm a
   translation command stops rather than changing to robot-oriented movement.
7. Confirm `X` and the 300 ms command watchdog still stop every motor in all modes.

The host-side test in `tests/navigation_math_test.cpp` covers quaternion-to-yaw,
angle wraparound, bounded correction, the field-to-robot transform, continuous
15-degree translation, and simultaneous translation/rotation mixing. See
`GAMEPAD_INTEGRATION.md` for the active Raspberry Pi sender, continuous-axis
mapping, safety behavior, and Python tests. An actual ESP32/BNO085 build and the
above physical tests are still required before relying on either feature at full
speed.
