# Maker Mechbot integrated firmware

Standalone Maker ESP32 Pro port of the current main firmware. The S3 sketch,
Maker_M0_Test, Maker_IMU_Test, and existing Raspberry Pi files are unchanged.
September 3 raised-wheel integration tests covered all six directions at 177 PWM,
encoder polarity and command-watchdog stopping. Four encoder scales were measured.
See ../HARDWARE_BASELINE.md for the results, RL speed asymmetry and recurring IMU
faults. Floor operation, continuous speed control and sustained IMU reliability
in this integrated firmware remain unverified.

## September 7, 2026 checkpoint

This integrated source still uses the SPI V2 transport and its 20 kHz PWM / 177
default ceilings. Its September 6 upload was verified on COM8, but the IMU
produced no decoded sensor events during the live check. Later diagnostic
sketches replaced that bench upload; this source is not a claim about the
firmware currently installed on the Maker.

The successful IMU baseline is the separate
[continuous UART-RVC bench sketch](../Maker_IMU_RVC_Continuous_Test/README.md):
Maker ESP32, IMU VDC at 5 V, a 3.3 V-powered SN74LVC245AN buffer, and receive-only
UART on GPIO21. The saved run reached 37 min 51 s and 227,501 valid frames with
zero reported checksum, UART, index, repeat or pause errors. Ten ESP-only resets
also reacquired the powered stream, and four user startup sequences reached
READY. See the [bench findings](../test_results/imu-maker-rvc-continuous-20260907/FINDINGS.md).
RVC integration into this robot firmware remains pending. The recovery changed
the controller/setup, buffering, supply and startup procedure across trials;
the evidence does not isolate 5 V as the cause of recovery.

The standalone [RVC motor movement test](../Maker_IMU_RVC_Movement_Test/README.md)
uses the later 248 Hz / logical duty 150 setting. Its target compilation and
seven host scenarios passed, but it has not been uploaded or run on hardware.
Bench endurance does not establish motor-noise tolerance or navigation accuracy.
The SPI wiring below applies to this integrated source; follow each standalone
sketch's own wiring for its tests.

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

The user identified the breakout as **STRDC BNO085** on September 6 and requested
switching away from I2C. Current source uses **SPI with GPIO16 wake control**.
The user manually uploaded the first SPI build and reported brief readings
followed by STALE. Current V2 source fixes full-duplex reception, retention of
bundled reports, stale-stream recovery and Serial buffering. V2 was then uploaded
through Arduino IDE on COM8 and its identity verified, but the live check failed:
`WAIT Q0 G0 A0`, increasing SPI packets and zero decoded sensor events. Motor
outputs remained zero. See the [PC upload result](../test_results/imu-spi-v2-20260906/PC_UPLOAD_RESULT.md).
Follow [SPI wiring and startup](SPI_WIRING.md) and
the [V2 PC upload guide](../test_results/imu-spi-v2-20260906/FLASH_FROM_PC.md).
V2 target compilation and four host test programs passed; see
[the V2 build record](../test_results/imu-spi-v2-20260906/build-manifest.json).
The earlier real SH-2 session-lifecycle check is recorded in the
[V1 build record](../test_results/imu-spi-20260906/build-manifest.json).

| STRDC pin | Connection for this build |
| --- | --- |
| VDC | Regulated **3.3 V** supply for direct ESP32 logic connections |
| GND | Common ground with Maker |
| SDA / MISO | GPIO21 |
| SCL / SCK | GPIO22 |
| DI / MOSI | GPIO32 signal |
| CS | GPIO33 signal |
| RST | **GPIO25 signal** on the servo header; reserve this pin for reset |
| INT | GPIO26 signal |
| P0 / WAKE | GPIO16 on the SPI expansion header |
| P1 | 3.3 V / VDC |

The default SPI pins overlap encoders; use the remapped pins above. The local
MakerBnoSpi adapter implements the separate wake handshake and avoids waiting
or resetting on idle reads. The I2C upload and troubleshooting notes below are
historical evidence, not the new SPI wiring procedure.

Historical September 6 I2C upload follow-up: before the SPI changes, the user
requested an Arduino IDE upload through
computer-use, stated the IMU was disconnected, and confirmed RST on GPIO25.
Upload on COM8 succeeded with written-data hash verification and full-chip erase
disabled. Running help confirmed RST25; STOP was acknowledged, and DIAG showed
all four outputs and all eight PWM duties zero. Runtime IMU status was
`WAIT Q0 G0 A0`, with no valid reports. No motion command was sent; physical
reset recovery remains unverified. See
[`upload-result.json`](../test_results/imu-reset-20260906/upload-result.json).

Pi reconnection follow-up: the user confirmed the IMU was connected too. The
Pi bridge identified Maker with fresh zero-output diagnostics but no valid IMU
reports. An exclusive serial handoff confirmed RST25 and sent one `IMU RETRY`.
A fresh controller READY was also observed during the handoff; it was not a
sensor-only reset experiment. The IMU remained OFFLINE and repeated scans found
no I2C devices. The bridge was restored, all nine settings matched, outputs were
zero, and deadman rearm was required. Power/logic voltage and physical reset
operation still need checking. Evidence:
[`pi-reset-result.json`](../test_results/imu-reset-20260906/pi-reset-result.json)
and [`pi-reconnect-followup.json`](../test_results/imu-reset-20260906/pi-reconnect-followup.json).

Supply correction follow-up: the user confirmed VDC had been on 5 V and then
reported rewiring after the 3.3 V instructions. A further stopped `IMU RETRY`
still found no I2C devices and remained OFFLINE. The bridge was restored, all
nine settings matched, and all motor outputs were zero. Voltage at the IMU pads,
its power LED, and the physical RST signal have not been verified. See
[`pi-3v3-followup.json`](../test_results/imu-reset-20260906/pi-3v3-followup.json)
and [`pi-3v3-reset-result.json`](../test_results/imu-reset-20260906/pi-3v3-reset-result.json).

Check GPIO25 is unused before wiring/upload. Use its signal contact, not the
adjacent servo power contact. Disconnect battery and USB before soldering.
Soldering the complete pin headers is fine; extra pins do not need wires.
For SPI, connect P1 high and P0 to GPIO16; DI is now MOSI. Leave BT and unused
interface pins at their defaults. Do not tie P0 directly to VDC with this build.

[STRDC's pinout](https://docs.strdc.com/products/imus/bno085-bob/) specifies that
VDC must match the host logic voltage, RST has a pull-up to VDC, and the separate
3V3 pin is a regulator **output**. Earlier notes recorded a 5 V-compatible power
input; that does not establish compatibility of 5 V-powered breakout signals
with direct ESP32 GPIO. Use 3.3 V on VDC for the wiring above. Verify the supply
connection rather than assuming the Maker's I2C/servo power contacts are 3.3 V.

## Defaults and behavior

- All four PWM ceilings default to **177/255**, without old motor matching trims.
- Heading correction and field-oriented control default OFF.
- Encoders have forward-positive x4 counts and measured per-wheel CPR in
  ../HARDWARE_BASELINE.md. The firmware still has **no wheel-speed PID or distance
  odometry**. The supervised bench tuner uses the measured scales; pulse totals
  that include startup/coasting are not steady speed measurements.
- Game rotation vector supplies relative yaw; yaw may drift and is not north.
- Quaternion, calibrated gyro and linear acceleration are requested at 50 Hz.
  I telemetry is sent only when all three streams are fresh and valid.
- A dedicated high-priority output task checks the 300 ms command lease every
  approximately 5 ms, independently of blocking main-loop IMU operations.
- Startup has no commanded motion. X and invalid input zero outputs immediately.
  Normal increases are ramped at 255 duty units per 300 ms. Sign reversals ramp
  to zero, coast at least 150 ms, then ramp in the opposite direction. This is
  not proof that the rotor has fully stopped; wait for visible stop during tests.
- Motors coast at zero output, not active braking. No current limiting,
  electrical protection guarantee, or encoder-based stall cutoff is implemented.
- IMU reset/reinitialization stops motion and invalidates field/heading references.
  Startup, stopped OFFLINE retries, and `IMU RETRY` reset the sensor with SPI
  mode selected. Failed initialization clears old report validity/timestamps.
  Missing or stale reports are selectively requested again while stopped after
  a two-second grace period. If they remain unhealthy for three seconds after
  the first request, firmware stops outputs and reinitializes the IMU.
  Requested motion defers automatic requests and resets; `IMU RETRY` explicitly
  stops and reinitializes. Use `IMU DIAG` while stopped for sample ages and
  transport counters. These changes still need sustained hardware verification.
- Settings use NVS namespace `maker_v1`, isolated from the old `mechbot` namespace.
  CFG RESET restores RAM defaults; CFG SAVE is required to persist them.

## Upload

1. Raise all wheels securely and keep motor power OFF during upload.
2. Stop/disconnect the Pi bridge/gamepad serial sender first. Its V stream could
   otherwise start the robot after boot; do not run two serial owners together.
3. Open Maker_Mechbot.ino in Arduino IDE. Select **ESP32 Dev Module**, not S3.
   Requires Espressif ESP32 core 3.x and Adafruit BNO08x with its dependencies.
   The SPI build uses core 3.3.11 and Adafruit BNO08x 1.2.7 with the local adapter.
   Keep **Erase All Flash Before Sketch Upload: Disabled** to preserve NVS.
4. Upload manually to the Maker board. Open Serial Monitor at **115200 baud**,
   with **Newline** or **Both NL & CR** enabled.
5. Expect `READY ESP32_MAKER_MECANUM_IMU_V1` and help containing
   `IMU: SPI SCK22 MISO21 MOSI32 CS33 INT26 RST25 WAKE16`.
   Send `IMU RETRY`, then `CFG GET` and `DIAG`; inspect for fresh numeric I
   records and all four outputs zero. Report-request success alone is insufficient.
   Enable motor power only when ready for the supervised tests below.

The updated mechbot_firmware_update.py selects Maker or S3 from the bridge's exact
READY identity and verifies a fresh matching READY after upload. Deploy it with
mechbot_profiles.py and the updated bridge; older installed copies still target
S3 and must not be used on Maker. See ../DEPLOYMENT.md. First uploads or unknown
firmware still use the explicit manual board selection above.

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
The updated local dashboard displays all four Maker encoders and DIAG data, uses
Maker-specific starting settings, and exports sessions. Install the matching Pi
files together. Four-wheel PID remains separate follow-up work.

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
c++ -std=c++17 -Wall -Wextra -Werror -Itests/maker_host_stubs tests/maker_bno_spi_test.cpp -o spi-test
```

Run these binaries from the repository root. Tests cover output timeout/ramping,
reversal pauses, immediate stop, timer rollover, mecanum math, actual firmware
pin mapping/polarity/defaults, malformed/nonfinite/overflow serial input, stale
IMU streams, sensor reset, simulated blocked-IMU timeout, and SPI wake/CS
sequencing and failure recovery. They do not model electrical transients,
physical coasting, actual RTOS scheduling or SPI signal integrity.
# PWM readback diagnostics

`DIAG` retains the existing `D` records (software applied PWM and encoder
diagnostics) and adds one `P` record per wheel. Each `P` record lists both motor
input GPIOs, their LEDC duty-register readbacks, and timer frequencies in Hz.
For full forward at PWM 177, FL/FR/RL should read 177 on the first pin and zero
on the second; RR should read zero then 177 because of its electrical polarity.
The powered pin should report 20000 Hz. Arduino core 3.3.11 returns zero Hz
when duty is zero, even though the channel is attached. Stopped duties and
reported frequencies should both be zero.

These register readbacks can expose a difference between the software command
and PWM peripheral state. They do not measure the physical pin waveform, motor
driver output, supply voltage, or motor speed. Equal software settings alone
do not establish equal electrical outputs or equal speeds.
