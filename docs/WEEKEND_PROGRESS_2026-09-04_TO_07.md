# Weekend development record: September 4–7, 2026

Consolidated September 8, 2026. Dates use America/Chicago where local dates are
known; some raw filenames use UTC and can fall on the following day. This is a
record of the work and saved evidence, not a new live inspection or hardware run.

The weekend produced a deployed Pi Operations console, better encoder and PWM
diagnostics, measured PWM frequency comparisons, and a recovered continuous IMU
bench stream. The robot still uses open-loop motor control. Encoder faults under
some powered conditions, reliable IMU integration, and loaded driving remain
unfinished. Source, tests, raw observations, and historical failures are retained
so the next session can build on the measurements.

## Results at a glance

| Workstream | What was completed | What the result does not establish |
| --- | --- | --- |
| Maker integration | Board-specific bridge/dashboard/updater, four encoder profiles, bench tuner, settings/export handling | A successful automatic calibration or continuous speed control |
| Pi Operations | Live health, recordings, offline replay, evidence comparison, passive odometry, responsive UI; installed and checked on Pi | Measured robot geometry or accepted floor odometry |
| Encoder investigation | Single-register A/B sampling, verified upload, manual/powered/pair observations | Resolution of all invalid transitions; some later powered trials still aborted |
| PWM investigation | Peripheral readback plus repeated coarse/fine frequency sweeps | A unique optimal frequency, physical waveform measurement, or a proven electrical root cause |
| IMU recovery | Buffered 5 V Maker UART-RVC stream; nearly 38 minutes and 227,501 valid frames with clean communication counters | Angular accuracy, motor-noise immunity, or integration into navigation firmware |
| Next test hardware/software | Perfboard/reset plan and compiled supervised RVC movement sketch | Completed assembly, verified reset circuit, or a physical movement run |

## Friday, September 4: establish the Maker baseline

Recovered the installed Maker ESP32 Pro history, wheel mapping, encoder polarity,
watchdog checks, and measured counts per wheel revolution. Raised-wheel motion
had been exercised in all six directions; ten-turn measurements agreed within
0.23%. These are bench observations. The [hardware record](../HARDWARE_BASELINE.md)
preserves the preceding September 3 setup rather than treating it as new weekend
work.

Integrated the existing automatic Maker bench tuner and board-aware Pi tooling:

- Shared Maker/S3 profiles with distinct READY identities, encoder capabilities,
  measured Maker scales, and separate PWM baselines.
- Four-wheel diagnostics and tuning exports, board-bound setting restoration,
  exclusive serial ownership, and deadman rearm after maintenance.
- Firmware target and port selection with exact board matching and fresh READY
  verification after upload.
- Persistent tuning controls, stopped-only IMU recovery, and clearer navigation
  and gamepad contracts. Current gamepad driving is cardinal/full-scale;
  proportional input and continuous speed PID remain later work.

The matched Pi update was installed with settings and files backed up. Its
checkpoint recorded 77 Python tests and three C++ host programs passing on Pi.
See [deployment](../DEPLOYMENT.md), [automatic tuning](../MAKER_AUTO_TUNING.md),
[gamepad integration](../GAMEPAD_INTEGRATION.md), and
[navigation controls](../NAVIGATION_CONTROLS.md).

Two automatic bench runs independently measured forward starting PWM
**119 / 119 / 155 / 131** for FL / FR / RL / RR. Both stopped in the first
all-wheel speed-matching sequence after a new invalid encoder transition: RL
first, FR on retry. Reverse startup and a shared trim were not completed;
original settings were restored. RL direction-dependent speed/coast behavior
persisted after a motor-output swap, but motor or gearbox damage was not proven.
The [bench report](../BENCH_RESULTS_2026-09-04.md) preserves the observations.

The encoder ISR then changed from two separate digital reads to one saved GPIO
bank snapshot for each A/B pair. This removes sampling skew within that pair;
it does not eliminate electrical noise or missed interrupts. Expanded host
coverage exercises all transitions, polarity, invalid jumps, resynchronization,
edge totals, startup states, and one register read per sample. The ESP32 target
build passed. See [sampling change](../ENCODER_SAMPLING_FIX.md).

## Friday night–Saturday, September 5: Operations and powered diagnostics

Built the Operations console at `/`, preserving the tuning workflow at `/tuning`:

- Structured telemetry with stale-data, device-clock, reboot, and board-profile
  handling; wheel RPM and diagnostic health.
- Bounded recording and saved sessions; deterministic passive replay that never
  transmits recorded commands to the robot.
- Bench evidence summaries and comparisons that retain aborted, simulated,
  incomplete, and incomparable states.
- Passive odometry requiring measured geometry, plus offline CLI tools.
- Responsive desktop/mobile layouts, simulated preview, and HTTP input/origin
  checks. Browser reviews covered Maker and separate S3 capabilities.

Local-model drafts were independently reviewed and corrected. The overnight
record logs 53,537 generated tokens across 36 attempts and 170 responses, with
additional bounded follow-up work. This was software development and simulation;
it did not establish autonomous physical behavior. See the
[overnight ledger](OVERNIGHT_PROGRESS.md), [validation](OPERATIONS_VALIDATION.md),
and [runbook](OPERATIONS_RUNBOOK.md).

The Operations release was installed on the actual Pi: 30 deployed files were
verified, nine settings preserved, and all 222 Python tests passed there. A
stationary real capture/replay contained **1,395 events with zero drops**, zero
motor outputs, and unchanged encoder counts. Following a power cycle, an idle
read-only identity retry and dashboard error-recovery fix passed 229 Python tests
on Pi and 26 Node tests. The IMU was again offline at that checkpoint. See the
[Pi deployment record](PI_OPERATIONS_DEPLOYMENT_2026-09-05.md).

Added a passive encoder Start/Finish/Download workflow with count, revolution,
edge, and invalid-transition deltas. Gaps, stale data, reconnects, resets, and
profile changes invalidate an observation rather than creating false evidence.
Explicit IMU OFFLINE telemetry now clears an old heading. This follow-up reached
230 Python tests with one Windows skip and 37 passing Node tests. See
[encoder observation](ENCODER_OBSERVATION.md).

The coherent encoder firmware was uploaded September 5 with independent image
verification, a full 4 MB pre-flash backup, unchanged 20,480-byte NVS, and matching
saved source on Pi. See [upload evidence](MAKER_FIRMWARE_UPLOAD_2026-09-05.md).
Subsequent observations progressed beyond that upload:

| Check | Recorded outcome | Evidence |
| --- | --- | --- |
| Manual wheel turns | Correct polarity and no new invalid transitions. RL follow-up recorded +2443 counts (0.9877 rev) and −2481 (−1.0030 rev), with other wheels unchanged | [Manual results](../test_results/manual-encoder-20260905/README.md) |
| Short powered pulses | Four checks passed without new invalids, but all-wheel front speed was about 15–21 RPM against rear 66–86 RPM | [Powered results](../test_results/powered-encoder-20260905/README.md) |
| Paired motors and matching | Six of eight pair trials passed; two aborted on invalid transitions. Four static matching attempts aborted. No accepted or saved trim | [Pair results](../test_results/paired-motors-20260905/README.md) |
| PWM peripheral readback | Added DIAG P records; all four active inputs read duty 177 at 20,000 Hz while wheel speed remained unequal | [PWM readback](../test_results/pwm-readback-20260905/README.md) |

The readback trial measured FL / FR / RL / RR tail RPM of
**12.58 / 8.57 / 60.58 / 71.06** despite equal active LEDC duty/frequency. Opposite
inputs were zero. Host testing deliberately mismatched software targets and
register values to verify the diagnostic reads the peripheral. Firmware upload
and unchanged NVS were verified. Register readback is not an oscilloscope capture
or motor-terminal voltage measurement; no incorrect mixer scaling was shown.
Nine original settings were restored and motor outputs returned to zero.

## Saturday–Sunday, September 5–6: investigate why lower PWM helped

Recovered the dedicated PWM research task into the
[frequency research archive](../test_results/pwm-frequency-20260905-06/README.md).
It includes standalone trial firmware, source analysis, parsers, raw serial logs,
derived tables, and the original experiment reports.

The investigation progressed from driver/ESP32 source review to five completed
logged sweeps: **169 trials and 13,689 samples**, comprising 168 planned trials
and one preliminary trial. Fixed-APB comparisons reproduced the frequency effect
at both tested duties: 248 Hz performed better than 1 kHz, then 20 kHz, then
5 kHz in those comparisons. Fine sweeps showed a **broad useful 200–300 Hz
region**, with no unique peak at exactly 248 Hz. The archived README explains
the individual runs, resolution/clock controls, and comparison limits.

The exact 248 originated in an upstream `uint8_t` frequency declaration assigned
75000: conversion yields `75000 % 256 = 248`. That explains the software value,
not a physical optimum. The archive pins the original source. At logical duty
150, 248 Hz was 50.01% faster than 20 kHz and 5 kHz was 61.85% slower. Fine tests
across a predicted typical driver sleep boundary showed no large speed collapse;
driver sleep alone was not demonstrated as the explanation. These controlled
sweeps exercised the FL motor on M2, not simultaneous all-wheel loaded driving.

This is stronger evidence than a single before/after observation. It still does
not identify the motor/driver electrical mechanism or prove a universal optimum.
Driver decay behavior, motor inductance/current buildup, supply behavior, and
friction were investigated as explanations; terminal waveforms and current were
not measured. Earlier reports that propose sweeps are historical plans; the
later logs record which sweeps were actually completed. The next movement sketch
uses 248 Hz as a tested working setting, not as a scientifically unique number.

## Sunday–Monday, September 6–7: isolate the IMU failure

The STRDC BNO085 investigation separated host transport problems, startup
behavior, sensor report configuration, and possible hardware/power effects.
Several apparently promising software changes failed on hardware; those results
are preserved.

| Experiment | What happened | Evidence |
| --- | --- | --- |
| STRDC/Adafruit review and reset work | Compared initialization, feature confirmation, reset timing, and ESP32 transport behavior; documented the vendor HAL port requirement | [Library review](../test_results/imu-reset-20260906/strdc-library-review.md) |
| Integrated SPI V1/V2 | V1 briefly reported then went stale. V2 fixed duplex reception, bundled report handling, wake bounds, and stale recovery; uploaded but produced zero decoded Q/G/A events | [V2 user log analysis](../test_results/imu-spi-v2-20260906/USER_LOG_ANALYSIS.md), [upload result](../test_results/imu-spi-v2-20260906/PC_UPLOAD_RESULT.md) |
| SPI packet diagnostics | Product identity remained responsive while quaternion configuration readback was disabled; changing transmit framing did not recover the stream | [Diagnostic upload](../test_results/imu-spi-diag-20260906/PC_UPLOAD_RESULT.md), [validation](../test_results/imu-spi-diag-20260906/VALIDATION.md) |
| Isolated SPI error-check | Accepted 20 ms quaternion configuration, decoded two events, then stalled through six ten-second summaries | [Error-check result](../test_results/imu-error-check-20260906/VALIDATION.md) |
| Initial Maker UART-RVC | Received the 78-byte Hillcrest startup banner and no sensor frames; raw byte count also stopped, so parsing alone did not explain it | [UART result](../test_results/imu-uart-rvc-20260906/VALIDATION.md) |
| Maker I2C bus diagnostics | Corrected early test interpretation; final trials sustained accelerometer/geomagnetic data while gyro stopped after a few samples | [Bus findings](../test_results/imu-bus-diagnostic-20260906/FINDINGS.md) |
| Separate ESP32-S3 and vendor examples | Reproduced accel stopping after gyro enable; adapted STRDC accel example worked, gyro example timed out during feature response | [S3 validation](../ESP32S3_IMU_Test/VALIDATION.md), [STRDC accel](../ESP32S3_STRDC_Accel_Test/VALIDATION.md), [STRDC gyro](../ESP32S3_STRDC_Gyro_Test/VALIDATION.md) |
| Overnight comparison | Sixteen controlled observations across vendor accel, CEVA/Adafruit, vendor readback, and passive RVC; accel/geomagnetic worked, calibrated gyro did not | [Overnight findings and logs](../test_results/imu-overnight-20260907/FINDINGS.md) |

The final Maker bus trial logged gyro bursts of only two to five samples, while
geomagnetic and accelerometer reports lasted roughly 34 seconds with no counted
transport errors. Earlier V2 bus interpretation was superseded; use the corrected
V3/V4 evidence. A user-measured regulator output of 3.0 V is recorded in the bus
findings, but neither supply transients nor a failed component were established.
An independent USB DTR/RTS/IDE-monitor issue could leave S3 in ROM download mode;
that host boot issue was diagnosed separately from sensor streaming.

The September 4 logs also contain genuine changing quaternion and nonzero gyro
data. Later failures cannot establish that the IMU never worked, and internal
error bytes were not sufficient to diagnose a specific damaged part.

## Monday, September 7: recover a continuous RVC bench stream

The successful configuration uses **Maker ESP32 Pro, STRDC VDC at 5 V, and an
SN74LVC245AN powered at 3.3 V** to buffer IMU TX into GPIO21 UART RX. P0 is
strapped to the IMU's own VDC; grounds are shared. It is receive-only UART-RVC at
115200 baud. Follow the exact [wiring record](../Maker_IMU_RVC_5V_Test/WIRING.md)
and [continuous test instructions](../Maker_IMU_RVC_Continuous_Test/README.md).

The first complete 35-second trial captured **3,493 checksum-valid frames** at
about 100.2 Hz, with no checksum/index/UART errors. A second manual-orientation
trial captured 3,104 frames with coherent large angle changes after a noisy
startup. See [5 V RVC findings](../test_results/imu-maker-rvc-5v-20260907/FINDINGS.md).

The continuous receiver removed the manual arming/35-second limit and established
READY from five sequential checksum-valid frames. Recorded follow-up:

- Ten explicit ESP-only resets, IMU power retained: READY in **53–60 ms**, clean
  recorded counters. A separate monitor-open reset was not counted as a trial.
- Four additional user startup sequences: READY in **367–368 ms**, with clean
  counters. The logs do not independently instrument electrical power removal.
- Endurance log: final uptime **37 min 51 s**, **227,501 valid frames**, maximum
  observed gap **13 ms**, no reported checksum/UART/index/repeat/pause errors,
  and changing orientation/acceleration values. The 15-minute bench endurance
  target was exceeded.

See [continuous findings](../test_results/imu-maker-rvc-continuous-20260907/FINDINGS.md)
and its linked raw captures. This establishes a useful bench-stream baseline.
Power, controller, buffering/wiring, and startup procedure changed across the
investigation, so **5 V alone is not proven as the cause of recovery**. RVC does
not provide separate gyro-rate fields, and changing orientation does not prove
angular accuracy or stationary drift performance.

Additional work prepared the next experiments:

- [ElectroCookie perfboard layout](../Maker_IMU_RVC_Continuous_Test/PERFBOARD_LAYOUT.md)
  and [drawing](../Maker_IMU_RVC_Continuous_Test/perfboard-layout.png): socketed
  buffer, connectors, bypassing, and a proposed GPIO25 transistor reset stage.
  The physical assembly, transistor pinout, and reset recovery remain unverified.
- [RVC movement sketch](../Maker_IMU_RVC_Movement_Test/README.md): repeated bounded
  forward/backward/left/right phases at 248 Hz, logical duty 150 scaled to
  9-bit duty 300/512. Idle boot, GO countdown, immediate X, stale-IMU stop, and
  independent output watchdog/deadlines. ESP32 core 3.3.11 build passed at
  288,951 flash bytes / 22,648 static RAM bytes; seven host scenarios passed.
  **It has not been uploaded or run on the robot.** See [build evidence](../Maker_IMU_RVC_Movement_Test/BUILD_STATUS.md).
- [ESP32-S3 5 V RVC variant](../ESP32S3_IMU_RVC_5V_Test/BUILD_STATUS.md): compiled
  and source-reviewed; not uploaded or hardware-tested.
- [Flipper RVC app draft](../Flipper_IMU_RVC_Test/DEVELOPMENT_STATUS.md): source,
  manifest, parser, and logging concept retained. Paused, uncompiled, and untested;
  no Flipper device was available.

The subsequent September 7 comparison of the DIY buffer and a small HiLetgo
level-converter module kept the working DIY circuit as the baseline. No
side-by-side electrical comparison or alternative-module test was performed.
That discussion did not validate the proposed reset circuit or motor operation.

## Next work, in order

1. Preserve and reproduce the working buffered RVC setup. Finish and inspect the
   perfboard/reset assembly; repeat startup/endurance observations after wiring
   changes, including a documented full cold-start series and interrupted-stream
   recovery.
2. Run the prepared movement experiment under supervision and save IMU and motor
   evidence. This is the missing motor-noise and physical-output check.
3. Integrate the verified RVC path into Maker robot firmware and Pi telemetry,
   explicitly accounting for RVC's missing separate gyro-rate fields. Verify
   restart, stale data, stop, and watchdog behavior before navigation features.
4. Recheck encoder validity and repeat PWM comparisons under controlled supply,
   duty, direction, and load. Characterize starting/holding duty and reverse
   behavior; accept a shared trim only if the data supports one.
5. Establish repeatable floor driving, measure loaded wheel diameter/wheelbase/
   track width, and validate passive odometry. Then introduce wheel-speed
   feedback and proportional input, followed by heading/field control.
6. Review the separate Mecha navigation modules for selective reuse after these
   hardware gates. Their offline completion is not autonomous-robot validation.

See [project roadmap](../PROJECT_ROADMAP.md) and [robot gates](ROBOT_ROADMAP.md).

## Publication and verification

The GitHub checkpoint preserves the weekend source, tests, diagnostic sketches,
vendor-source provenance/licenses, documentation, and compact evidence files.
Generated binaries, build directories, and local package ZIPs remain on the PC;
see [firmware archive policy](../firmware/README.md). Raw logs retain their
historical settings and outcomes. Earlier instructions and manifests can name
local build artifacts that are intentionally absent from a fresh clone.

On September 8, the consolidated checkout ran **230 Python tests: 229 passed,
one Windows symlink-privilege fixture skipped**. All **37 Node tests passed**.
The PWM archive's five datasets were re-parsed and matched their stored numeric
results; all **26 PWM host simulations passed** again. Dashboard JavaScript
syntax, documentation links, and whitespace checks also passed.
These are fresh software checks; the dated target-build, browser, Pi, and physical
trial results above remain historical evidence. Publishing this record performs
no firmware upload, service deployment, or motor command.
