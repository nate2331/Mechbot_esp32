# Maker Mechbot integrated firmware

Standalone Maker ESP32 Pro port of the current main firmware. The S3 sketch,
Maker_M0_Test, Maker_IMU_Test, and existing Raspberry Pi files are unchanged.
Hardware integration under motor load is NOT yet verified.

## Hardware

| Wheel | Motor / encoder | Driver inputs | Encoder A / B | Motor sign | Encoder sign |
| --- | --- | --- | --- | --- | --- |
| FL | M2 / E2 | 17 / 12 | 35 / 36 | +1 | +1 |
| FR | M3 / E3 | 14 / 15 | 34 / 39 | +1 | -1 |
| RL | M1 / E1 | 4 / 2 | 5 / 23 | +1 | +1 |
| RR | M0 / E0 | 27 / 13 | 18 / 19 | -1 | -1 |

These polarities follow the user's successful individual-wheel tests. Set the
M2/M3 switches to Motor. GPIO34/35/36/39 have no internal pull-ups; external
pull-ups to 3.3 V are needed if the connected encoder outputs require them.
All encoder and I2C signal levels must be ESP32-compatible (not 5 V logic).

BNO085: SDA21, SCL22, common ground, user-confirmed 5 V-compatible breakout
power input. No reset or interrupt wire; do not confuse breakout VIN tolerance
with chip or GPIO voltage tolerance. The tested address is 0x4A.

## Defaults and behavior

- All four PWM ceilings default to **177/255**, without old motor matching trims.
- Heading correction and field-oriented control default OFF.
- Encoders have forward-positive x4 counts, but **no wheel-speed PID or calibrated
  distance/CPR**. Do not automatically equalize raw wheel counts yet.
- Game rotation vector supplies relative yaw; yaw may drift and is not north.
- Quaternion, calibrated gyro and linear acceleration are requested at 50 Hz.
  I telemetry is sent only when all three streams are fresh and valid.
- A dedicated high-priority output task checks the 300 ms command lease every
  approximately 5 ms, independently of blocking main-loop I2C operations.
- Startup has no commanded motion. X and invalid input zero outputs immediately.
  Normal increases are ramped at 255 duty units per 300 ms. Sign reversals ramp
  to zero, coast at least 150 ms, then ramp in the opposite direction. This is
  not proof that the rotor has fully stopped; wait for visible stop during tests.
- Motors coast at zero output, not active braking. No current limiting,
  electrical protection guarantee, or encoder-based stall cutoff is implemented.
- IMU reset/reinitialization stops motion and invalidates field/heading references.
  Without RST, a hung sensor may require a power cycle. Missing reports are
  retried while stopped; `IMU RETRY` explicitly reinitializes while stopped.
- Settings use NVS namespace `maker_v1`, isolated from the old `mechbot` namespace.
  CFG RESET restores RAM defaults; CFG SAVE is required to persist them.

## Upload

1. Raise all wheels securely and keep motor power OFF during upload.
2. Stop/disconnect the Pi bridge/gamepad serial sender first. Its V stream could
   otherwise start the robot after boot; do not run two serial owners together.
3. Open Maker_Mechbot.ino in Arduino IDE. Select **ESP32 Dev Module**, not S3.
   Requires Espressif ESP32 core 3.x and Adafruit BNO08x with its dependencies.
4. Upload manually to the Maker board. Open Serial Monitor at **115200 baud**,
   with **Newline** or **Both NL & CR** enabled.
5. Expect `READY ESP32_MAKER_MECANUM_IMU_V1`. Power the IMU as wired and inspect
   telemetry. Enable motor power only when ready for the supervised tests below.

Do NOT use the existing mechbot_firmware_update.py: it still targets the old
ESP32-S3 sketch/FQBN. This work does not flash hardware or deploy Pi services.

## First integrated test (all wheels raised)

Send each command as a separate complete line:

```text
CFG GET
F 0
CFG SET heading-enabled 0
DIAG
```

Confirm all PWM settings are 177 and I telemetry has numeric data, not OFFLINE,
WAIT, or STALE. Rotate the chassis gently by hand first to confirm yaw response.

Send **one** of the following motion commands, then wait for every wheel to stop
before the next. Each single command expires after about 300 ms; do not expect
the 750 ms bench-sketch pulses or use its `a/b/c/d/f/r/t` commands here.

| Command | Intended movement | Encoder count-change signs FL / FR / RL / RR |
| --- | --- | --- |
| `V 1 0 0` | Forward | + + + + |
| `V -1 0 0` | Reverse | - - - - |
| `V 0 1 0` | Strafe left | - + + - |
| `V 0 -1 0` | Strafe right | + - - + |
| `V 0 0 1` | Counterclockwise | - + - + |
| `V 0 0 -1` | Clockwise | + - + - |

`X` stops immediately. A single V command followed by silence must produce a
watchdog-stop message. Paste the output for review, especially T, I, H and DIAG
before/after. Check for encoder channel errors, stale IMU reports, and new resets
with motors powered. These first tests are not floor/loaded-operation approval.

The Pi normally sends V at 20 Hz, which keeps the lease alive. Close Serial
Monitor before reconnecting the bridge. Don't increase PWM or enable heading
correction to compensate for wiring, power or sensor problems.

## Serial compatibility

Existing commands V, X, F, Z, ?, CFG GET/SET/SAVE/RESET and T/I/N/H telemetry
formats are preserved; T remains FL, FR, RL, RR order. New commands:

- `DIAG`: per-wheel applied logical PWM, cumulative A/B edges and invalid
  transition counts. This is an additive D line and can be ignored by old hosts.
- `IMU RETRY`: stop and reinitialize BNO085.

The N hold field now reflects the actual heading-enabled setting. The READY
identifier changed intentionally to distinguish this board from the S3 firmware.
The old dashboard still has rear-only calibration labels/logic; four-wheel PID,
dashboard changes, and the Pi firmware updater are separate follow-up work.

## Heading/field verification before enabling

Mount the sensor rigidly in the intended orientation. With motors stopped,
rotate the chassis CCW from above and verify reported yaw increases. Heading-sign
can reverse correction direction, but is NOT a substitute for proper IMU axis
alignment, particularly for field-oriented translation. Verify correction is
negative feedback at low command levels before enabling on the floor.

`CFG SET heading-enabled 1` enables relative heading hold. `F 1` captures a field
reference; `Z` changes it, and `F 0` disables field mode. A reset invalidates that
reference. All require separate physical validation on this new board.

## Verification

ESP32 target compilation and host tests are performed without opening robot
serial ports. Native tests use fake IO, not a physical board:

```text
c++ -std=c++17 -Wall -Wextra -Werror tests/maker_motor_safety_test.cpp -o motor-test
c++ -std=c++17 -Wall -Wextra -Werror -Itests/maker_host_stubs tests/maker_firmware_host_test.cpp -o firmware-test
```

Run both binaries from the repository root. Tests cover output timeout/ramping,
reversal pauses, immediate stop, timer rollover, mecanum math, actual firmware
pin mapping/polarity/defaults, malformed/nonfinite/overflow serial input, stale
IMU streams, sensor reset and simulated blocked-I2C timeout. They do not model
electrical transients, physical coasting, actual RTOS scheduling or I2C noise.
