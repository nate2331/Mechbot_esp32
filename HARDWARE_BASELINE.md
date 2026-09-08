# Hardware baseline and validation record

Updated 2026-09-07. Record observations here; do not turn an implemented feature
into a passed hardware test without a saved result.

## September 7 checkpoint

The integrated `Maker_Mechbot` source uses SPI with P0/WAKE on GPIO16 and RST on
GPIO25. The user flashed the first SPI build and observed brief reports followed
by STALE. V2 was subsequently uploaded through Arduino IDE on COM8; its running
identity and stopped motor outputs were verified, but no decoded IMU events
arrived. See the [SPI V2 hardware result](test_results/imu-spi-v2-20260906/PC_UPLOAD_RESULT.md)
and [SPI wiring map](Maker_Mechbot/SPI_WIRING.md). Later diagnostic uploads mean
this source does not establish the currently installed Maker firmware.

The latest successful sensor baseline is the standalone
[continuous UART-RVC bench test](Maker_IMU_RVC_Continuous_Test/README.md), with
Maker ESP32, IMU VDC at 5 V, a 3.3 V-powered SN74LVC245AN buffer and GPIO21 RX.
Ten ESP-only resets reacquired the powered stream; four user startup sequences
reached READY. The saved long run reached 37 min 51 s uptime and 227,501 valid
frames, maximum observed gap 13 ms, with zero reported checksum, UART, index,
repeat or pause errors. The 15-minute bench endurance target is achieved.
See the [continuous findings](test_results/imu-maker-rvc-continuous-20260907/FINDINGS.md)
and [earlier successful movement of the sensor by hand](test_results/imu-maker-rvc-5v-20260907/FINDINGS.md).
The changed supply, buffering, controller/setup and startup procedure do not
isolate the cause of recovery.

The [standalone RVC motor movement sketch](Maker_IMU_RVC_Movement_Test/README.md)
uses 248 Hz / logical duty 150 and passed target compilation plus seven host
scenarios. It has not been uploaded or tested on hardware. The continuous bench
result does not validate motor-powered operation, the proposed perfboard/reset
stage, angle accuracy or integration into the robot firmware. The full ten
common-supply cold starts and deliberate interruption/reacquisition remain
pending; the recorded ESP-only resets did not switch IMU power.

## Historical September 6 I2C troubleshooting

Before the SPI changes, the user identified the IMU as STRDC BNO085 and chose to
flash manually. That I2C source reserved GPIO25 for RST and reset before probing
I2C. See [the firmware history](Maker_Mechbot/README.md#hardware).
STRDC requires VDC to match host logic: use 3.3 V for direct ESP32 connections;
the earlier 5 V power observation is not wiring approval. A subsequent user-
requested IDE upload on COM8 passed written-data verification; running help
confirmed RST25 and DIAG showed zero outputs. User stated IMU disconnected;
runtime reported WAIT with no valid samples. Physical reset recovery remains
unverified. The user later confirmed VDC had been on 5 V and reported rewiring
after instructions to use 3.3 V. A stopped retry still found no I2C devices;
bridge restored, all nine settings unchanged, motor outputs zero. Actual pad
voltages and power LED were unverified at that checkpoint. Later bus-diagnostic
notes record a user measurement of 3.0 V at the breakout's regulated 3V3 output;
that steady reading does not measure startup transients. See the
[bus investigation](test_results/imu-bus-diagnostic-20260906/FINDINGS.md) and
[supply correction evidence](test_results/imu-reset-20260906/pi-3v3-followup.json).
The table below describes the September 4 installation, before these diagnostic
uploads.

## Target versus installation

| Item | Development target | Installed/verified evidence |
| --- | --- | --- |
| Controller | Maker ESP32 Pro, ESP32 Dev Module target | User confirms installed; September 3 Maker setup chat |
| Firmware | Maker_Mechbot; READY ESP32_MAKER_MECANUM_IMU_V1 | September 4 live status confirms exact READY identity; deployed binary/source hash still unknown |
| Encoders | FL, FR, RL, RR quadrature, forward-positive x4 counts | Six-direction raised-wheel tests passed; measured CPR below |
| IMU | BNO085, address 0x4A, SDA21/SCL22, no RST wire | WAIT/OFFLINE and failed retry followed by fresh reports; September 4 stationary check passes 30 samples over 43.8 s; load reliability remains unresolved |
| PWM | Fresh defaults 177/177/177/177 | September 4 live CFG GET confirms 177 all wheels; no NVS write during staging |
| Navigation | Heading correction off; field mode off | September 4 CFG GET and N telemetry confirm both off |
| Pi serial owner | mechbot_bridge.py via mecanum-gamepad.service | September 4 Maker bridge update installed and verified; all four outputs zero, settings preserved; details in DEPLOYMENT.md |
| Wheel scale | Measured per-wheel x4 counts/revolution below | Ten-turn forward manual measurement complete; loaded wheel diameter still needed |
| Geometry | Wheelbase, track width, roller orientation | Measure and record before odometry |

The [Maker README](Maker_Mechbot/README.md#hardware) is the wiring reference.
The S3 reset proposal (GPIO42) does not apply to Maker without a separate wiring
decision. The old 230/230/200/200 chassis baseline belongs to the legacy S3 setup.

## Acceptance sequence

For every trial record date, operator, board, source revision, settings, battery
voltage, surface/load, command/output/duration, observations and log filename.

1. **Stationary startup:** identify firmware and settings; outputs remain zero;
   encoder data arrives; rotate the chassis by hand and verify CCW increases yaw.
2. **Wheels raised:** follow the six directions in the Maker README. Verify all
   expected encoder signs, no unexplained channel errors, and visible stopping.
3. **Stop behavior:** verify deadman release, controller disconnection, serial
   command silence, X, and reversal pause. Record observed stop timing and coast.
4. **Sensor under load:** repeat short raised-wheel runs while logging I/H/D
   telemetry; record stale reports, reset/reinit counters and invalid transitions.
   Verify IMU failure leaves the command timeout effective. Do not physically
   jam motors as a substitute for a simulated blocked-I2C test.
5. **Wheel characterization:** with motor output disabled, rotate each wheel a
   measured number of complete revolutions; calculate x4 counts per wheel
   revolution. Repeat both ways. Then measure repeatable response at several PWM
   values in both directions using supervised bounded trials.
6. **Floor baseline:** repeat forward/reverse/left/right at identical settings
   on a clear surface, heading correction off. Record path and yaw drift.
7. **Navigation:** only after sensor/motion confidence, verify negative-feedback
   heading correction, deliberate turns, wraparound, and field-relative motion
   at 0, +90, -90 and 180 degrees. Recheck stale-heading stopping.

## Results

| Date | Test | Settings / artifact | Result | Follow-up |
| --- | --- | --- | --- | --- |
| 2026-09-04 | Initial repository review | 28 Python tests; no serial hardware opened | Passed software checks | Reconcile chat evidence below |
| 2026-09-04 | Read-only Pi service and HTTP status | Active service; controller disconnected; WAIT Q0 G0 A0 | IMU missing-report fault still present | Recover sensor before automatic bench tests; see DEPLOYMENT.md |
| 2026-09-04 | Deployment preflight and isolated staging | Exact Maker READY; live PWM 177 all wheels; IMU OFFLINE despite 0x4A detection; backup saved | 77 Python tests and three C++ host programs pass on Pi; service unchanged | Confirm current physical setup, install staged update, then attempt stopped IMU recovery |
| 2026-09-04 | Maker bridge/dashboard installation | Operator confirms wheels powered and raised; 22 files installed; preserved 177-all and heading settings | Active service, zero restarts, four fresh zero-output diagnostics, asset hashes and exports verified; Maker firmware also compiles without upload | Validate physical gamepad rearm behavior in a later supervised test |
| 2026-09-04 | Stopped IMU reinitialization | Exclusive serial with STOP / IMU RETRY / DIAG; transcript in deployment backup | Failed: OFFLINE despite 0x4A; bridge reconnected, outputs zero, maintenance released | Full Maker/IMU power cycle requested; automatic tuning remains blocked on fresh sensor reports |
| 2026-09-04 | Stationary IMU check after fresh reports returned | 30 samples over 43.8 s; saved in deployment backup; exact Maker READY retained | Valid fresh IMU in every sample, reset/reinit counters unchanged at 3/1, four outputs zero | Stationary recovery confirmed; verify sensor under load during the next supervised trial; physical power-cycle action not separately confirmed |
| 2026-09-04 | Automatic raised-wheel bench run | Forward startup PWM FL119 FR119 RL155 RR131; three confirmation starts each | Forward matching aborted on RL invalid count 0→1; reverse not reached; original settings restored | [Saved results](BENCH_RESULTS_2026-09-04.md); resolve encoder fault before finishing tuning |
| 2026-09-04 | Isolated RL versus all-wheel diagnostic | Three isolated RL-forward pulses at 177 clean; first all-wheel pulse sequence reproduced RL invalid count 1→2 | Stopped and restored; IMU counters unchanged at 3/1; all outputs zero | Inspect RL signal path and simultaneous sampling; exact powered/coast timing remains unresolved |
| 2026-09-04 | Full automatic retry | Forward thresholds reproduced exactly: FL119 FR119 RL155 RR131 | First matching sequence aborted on FR invalid count 0→1 while RL stayed 2; settings restored, IMU counters 3/1 | Fault is not RL-only; inspect shared noise, grounding and simultaneous encoder sampling |

### Recovered September 3 Maker results

Source: local chat **Set up Maker ESP32 Pro**, thread
`01a06788-60e7-7af1-b902-4972617f89e3`. These are historical chat results, not a
fresh hardware inspection in this change. Normal wiring was confirmed restored
after the M1/M2 motor-output swap.

- All six short-pulse directions: expected encoder signs, no invalid transitions,
  watchdog cutoff and no new IMU resets. User reported the wheels looked good.
- Sustained 177-PWM checks: all six directions completed; isolated FR/RR invalid
  transitions occurred in earlier attempts, none in the final four directions.
- Manual ten-turn forward test, motor power off: no new invalid transitions.

| Wheel | Counts over 10 revolutions | x4 counts per wheel revolution |
| --- | ---: | ---: |
| FL | 24,688 | 2,468.8 |
| FR | 24,679 | 2,467.9 |
| RL | 24,735 | 2,473.5 |
| RR | 24,698 | 2,469.8 |

The spread is below 0.23%; these scales support normalized speed comparison on
this Maker robot. They are now shared by the automatic bench tuner and board
profile. Re-measure after changing a motor, encoder, or gearing.

Longer raised-wheel trials at 177 PWM separated powered speed from coast-down:

| Wheel | Normal wiring forward RPM | Normal wiring reverse RPM | M1/M2 motor-swap forward RPM | Motor-swap reverse RPM |
| --- | ---: | ---: | ---: | ---: |
| FL | 47.6 | 54.9 | 52.2 | 49.8 |
| FR | 49.7 | 57.2 | 50.1 | 54.9 |
| RL | 47.0 | 39.6 | 36.3 | 39.1 |
| RR | 45.5 | 51.6 | 41.3 | 45.4 |

RPM is approximate (historical calculations used nominal 2470 CPR). The swap
interpretation assumes E1/E2 remained in normal positions. RL stopped coasting
sooner and remained slower in the swapped test. Results suggest motor/gearbox,
mechanical drag or motor-side wiring rather than specifically the M1 driver;
they do not establish a confirmed failed part. A replacement request was drafted.

Several attempts aborted on stale/no IMU reports. The final attempted PWM
matching session sent no motion because preflight and stopped reinitialization
failed. This remains the immediate test prerequisite. Prior permission that the
wheels were raised does not establish today's physical setup.

Source for the recovered automatic tuner: **New Realtime Voice Chat**, September
4, thread `01a06c20-1eb5-7992-9ec0-864c269ecff3`. It measured synthetic motors in
25 offline tests; no real automatic-tuning result or saved winning trim was found.

## Release gate

Basic raised-wheel direction and command-watchdog checks have recorded results.
Repeat only checks affected by changes or faults. Sustained IMU reliability under motor load,
gamepad rearm/disconnect, and reversal behavior still need a complete acceptance
record. Floor validation requires step 6. Wheel-scale measurement is complete;
speed control still needs response characterization from step 5. Navigation needs
step 7. Keep host-test results separate from physical results.
