# Robot roadmap: evidence before autonomous motion

The immediate software scope is the Operations console, passive capture/replay,
bench comparisons and measured-geometry odometry. Current implementation gates
are tracked in [OVERNIGHT_PROGRESS.md](OVERNIGHT_PROGRESS.md); operating interfaces
are in [OPERATIONS_RUNBOOK.md](OPERATIONS_RUNBOOK.md). This roadmap records work
still needed, not permission or evidence that physical tests occurred.

## Established baseline and open questions

The active robot is Maker ESP32 Pro with four forward-positive quadrature
encoders. Measured x4 counts per revolution are FL **2468.8**, FR **2467.9**,
RL **2473.5**, RR **2469.8**. These come from ten manual forward turns; they do
not supply loaded wheel diameter, wheelbase or track width.
[Source: hardware baseline](../HARDWARE_BASELINE.md).

Two supervised runs reproduced forward starting PWM **119 / 119 / 155 / 131**
for FL/FR/RL/RR under the recorded raised-wheel setup. Both aborted during the
first all-wheel speed-matching stage, on a new invalid transition: RL first, FR
in the retry. Reverse startup, reverse matching and holding duty remain unknown;
no new calibrated trim was saved. Three isolated RL pulses were clean, while an
all-wheel sequence reproduced the issue. Its precise powered/coast timing was
not established. RL's direction-dependent response is an observation, not a
confirmed failed gearbox or motor.
[Source: bench results](../BENCH_RESULTS_2026-09-04.md).

The new single-register encoder sampling removes one software skew opportunity.
Its host tests and ESP32 build passed, and the [September 5 upload was verified](MAKER_FIRMWARE_UPLOAD_2026-09-05.md).
Subsequent [manual observations](../test_results/manual-encoder-20260905/README.md)
and [four short powered pulses](../test_results/powered-encoder-20260905/README.md)
had zero new invalid transitions. Later [pair and static-matching trials](../test_results/paired-motors-20260905/README.md)
reproduced invalid transitions, so signal reliability remains unresolved.
[Source: sampling update](../ENCODER_SAMPLING_FIX.md).

The September 5 pair trials measured front wheels near 88–91 RPM with only two
motors enabled, while all-wheel trials reproduced much slower front speeds.
RL also remained slower than RR in pair tests. The [PWM readback trial](../test_results/pwm-readback-20260905/README.md)
confirmed equal 177 duty and 20 kHz peripheral settings during an all-wheel pulse
without correcting the speed difference. Regulator current rating, loaded supply
voltage and physical PWM waveforms remain unmeasured. No candidate trim was
accepted or saved. Reverse motion was observed, but reverse startup thresholds,
holding duty and a shared verified trim remain incomplete.

The BNO085 has intermittently stopped reporting, including failed stopped retry
despite an I2C address response. Fresh reports later returned; a 43.8-second
stationary check and unchanged counters during the checked bench run do not
establish continuous reliability under load.
[Source: deployment observations](../DEPLOYMENT.md).

## Ordered milestones

The [passive encoder observation tool](ENCODER_OBSERVATION.md) was added while
the user deferred the IMU reset wire on September 5. It supports signal-quality
checks without requiring an IMU. Subsequent reset, SPI and UART investigations
are indexed in the [project README](../README.md); the earlier I2C observations
above are historical. Heading-dependent navigation requires fresh, validated
IMU data from the integrated robot firmware.

| Stage | Work | Evidence required before advancing |
| --- | --- | --- |
| 1. Measure encoder signal quality | Supervised comparison of individual and combined wheels after identifying the exact flashed build; record wiring, power/setup, RX/TX and per-wheel diagnostic deltas. Examine shared signal integrity if errors remain. | Repeatable direction/mapping and clean invalid-counter deltas under the defined test conditions; reboot/reset boundaries distinguished from counter increases. A software pass alone does not clear this gate. |
| 2. Characterize repeatable start, reverse and hold | Measure all four wheels in both directions. Separate starts from rest, minimum sustaining duty, output ramp, powered steady speed and coast. Retain failures and original settings. | Repeated starts and matched-condition steady measurements within a predeclared tolerance; known reverse/holding behavior; stop/restoration checks; no unexplained encoder faults. The current forward thresholds are only one part of this evidence. |
| 3. Validate passive floor pose | Supply measured loaded wheel diameter and full wheel-centre wheelbase/track. Record supervised low-speed forward, strafe and rotation while the estimator only observes. Compare against independently measured displacement/angle. | Explicit calibration provenance, bounded error under documented surface/load, and correct invalidation on gaps/reboots. Missing geometry produces unavailable pose, never invented dimensions. Slipping wheels and stale IMU must not be hidden by a plausible trace. |
| 4. Develop an offline wheel-speed controller | Use measured directional response to design a bounded controller, then exercise it against the existing offline simulator and replayed disturbances. Keep controller output disconnected from hardware. | Deterministic tests for deadband/startup, saturation/anti-windup, reversal, invalid feedback, scheduler gaps, stopping and manual priority. Define speed/output limits from measurements and hardware ratings rather than guesses. |
| 5. Supervised low-power closed-loop trials | Integrate only after the previous gates. Begin with raised-wheel checks and explicit mode selection, then limited clear-area floor trials. Preserve the independent output task and command lease. | Correct feedback sign, controlled startup/reversal, bounded speed/error, immediate stop and deadman takeover, stale-feedback shutdown and no automatic resume after a fault. Measured stopping/coast behavior is distinct from electrical zero PWM. |
| 6. Navigation | Integrate the validated pose, heading, command arbiter and motion primitives in small steps before waypoint execution. Treat obstacle sensing/coverage as a separate measured capability. | Repeatable short primitives and route completion; cancellation/manual takeover; loss-of-feedback behavior; measured localization and obstacle-coverage limits. Offline route success is not autonomous hardware acceptance. |

Stages 1–3 and 5–6 require supervised physical evidence. During unattended
development, advance software, fixtures, replay analysis, documentation and
offline controller/simulation tests. Do not turn a readiness display into an
automatic motor-enable decision.

## Reuse and integration boundaries

Related work in `C:/Users/nate2/OneDrive/Documents/Mecha` has an offline
TASK-001–TASK-030 ledger. Useful components include `MecanumOdometry.h`,
`PlanarPoseEstimator.h`, `CommandArbiter.h`, `NavigationState.h`, translation and
rotation primitives, and `simulation/MecanumSimulator.h` / `WaypointFollower.h`.
Its telemetry tooling understands additional `W1`/`P1` messages; the current
Maker protocol does not imply those streams exist. Port and test interfaces
selectively instead of replacing this firmware with that separate checkout.

Current Maker control remains open-loop PWM; default heading correction and
field mode are off, and saved settings can override defaults. The independent
output task enforces a 300 ms command lease. The Pi gamepad currently sends
full-scale dominant-axis cardinal commands with a left-bumper deadman; continuous
proportional wheel-speed control is not installed.
[Sources: Maker behavior](../Maker_Mechbot/README.md),
[gamepad integration](../GAMEPAD_INTEGRATION.md).

For each milestone retain the source/build identity, calibration, setup,
supervision, raw evidence, failures and measured acceptance result. Readiness
should say **unknown**, **blocked**, **software checked** or **physically
validated** as appropriate. One green telemetry sample cannot substitute for
these separate records.
