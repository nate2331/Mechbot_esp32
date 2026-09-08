# IMU navigation controls

Updated 2026-09-04. Applies to Maker_Mechbot and the legacy Mechbot_IMU_ESP32
sketch. See HARDWARE_BASELINE.md for actual physical test coverage.

## Implemented behavior

Heading correction is a runtime setting: `CFG SET heading-enabled 0|1`.
Fresh Maker defaults are OFF; fresh legacy S3 defaults are ON. Saved NVS settings
can override these defaults. Field-oriented control defaults OFF on both boards.

At translation start, firmware captures current yaw. With heading correction
enabled and no deliberate turn, it adds a bounded proportional correction to
hold that yaw. Deliberate turns take priority and update the target. Idle input
does not cause heading correction to rotate the robot.

Relative heading hold accepts fresh valid quaternion data at accuracy status 0.
Field-oriented translation requires status at least 1. Quaternion age must be at
most 500 ms. When heading is unavailable in robot-relative mode, correction is
bypassed. Field-relative translation instead stops if its required yaw is lost.
IMU reset clears the field reference; Maker also stops before report recovery.

September 8 Maker source update: loss of field heading/reference or an IMU reset
while field mode is enabled latches a motor stop. Fresh reports, ordinary `V`,
`X`, `Z` or configuration changes do not acknowledge that fault. Send `F 0` to
explicitly return to robot-relative mode, or `F 1` with a fresh qualified heading
to recapture the field reference. This prevents queued gamepad commands from
silently changing coordinate frames after recovery. `N` ready is false while
the latch is set; `?` reports the latch. The update is not flashed and still uses
the experimental SPI transport, which is incompatible with the current UART wiring.

| Command | Behavior |
| --- | --- |
| V forward left ccw | Normalized motion components; firmware supports vector mixing |
| X | Immediate zero output (coast, not active braking) |
| CFG SET heading-enabled 0 or 1 | Disable/enable correction; setting changes stop output |
| F 1 | Stop and capture the present yaw as the field reference |
| F 0 | Stop and disable field-relative motion |
| Z | Stop and re-zero the field reference |
| IMU RETRY | Maker only: stopped sensor reinitialization |

The Pi gamepad input currently chooses one full-scale cardinal direction. Thus
firmware vector support does not mean the active gamepad provides proportional
translation, diagonals, or simultaneous turn. See GAMEPAD_INTEGRATION.md.

## Telemetry and tuning values

`N <ms> <yaw_rad> <target_rad> <error_rad> <correction> <hold> <field> <ready>`

Maker's hold field reflects the setting. Legacy S3's current N implementation
prints hold=1 even when correction is disabled; use CFG GET for that setting.
I is numeric only when the required sensor streams are available; WAIT or STALE
is not fresh IMU data. H reports availability and reset/reinitialization counters.

Starting gain is 0.70 command/radian, maximum correction 0.30, error deadband 1.5
degrees and manual-turn threshold 0.01. These values require physical tuning.
Game rotation vector provides relative yaw that can drift; it is not north.

## Remaining hardware checks

The six Maker directions and watchdog were tested with wheels raised. Heading
hold and field-oriented floor driving have not been accepted. First resolve the
recurring BNO085 missing-report fault and establish repeatable wheel response.
Then verify CCW chassis rotation gives increasing yaw, correction reduces error,
manual turns take precedence, and wraparound does not cause a large command.
Test field translation at 0, +90, -90 and 180 degrees relative to the captured
reference. Verify stale heading stops field translation and the 300 ms command
timeout stops output in every mode. Record settings and results in
HARDWARE_BASELINE.md; host math tests are not physical validation.

## Related navigation work

The separate Mecha repository contains tested offline odometry, pose estimation,
heading-controller, motion-primitive, arbitration, simulator and waypoint work.
Its protocol and firmware have diverged from this tree. Review and port selected
components after measurement and interface checks; do not assume those features
are deployed on this robot.
# Pi RVC operator controls — September 8, 2026

The Operations console provides Robot frame, Field frame, Re-zero field, Revoke
heading and Accept measured heading. Controls require a live integrated RVC
connection; replay, standalone diagnostic and offline views cannot use them.
Acceptance requires the operator to confirm measured mounting, yaw direction and
accuracy. It must not be used merely because changing sensor values are visible.

The single bridge owner accepts `POST /api/navigation` with `action` set to
`robot`, `field`, `zero`, `revoke` or `accept`. The latter additionally requires
`confirmation: "HEADING_MEASURED"`. These map to F 0, F 1, Z, IMU REVOKE and IMU
ACCEPT. Unknown commands, held deadman, disconnected/non-RVC firmware, tuning,
pending restoration and maintenance are rejected before writing. Each permitted
action writes zero velocity, X, then the selected command under one lock and
requires deadman rearming. A failed stop prevents the reference command.

The HTTP result reports dispatch, not acknowledgement. Inspect the captured OK/ERR
and live heading status for acceptance; firmware still enforces stream freshness.
Field-reference loss requires explicit recovery as described below. Heading hold
is configured separately; these buttons do not enable it or save NVS settings.
