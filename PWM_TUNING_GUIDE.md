# Blackbot Two PWM tuning guide

The installed Maker ESP32 Pro has four encoders with measured counts per wheel
revolution. The updated console detects Maker versus legacy S3 firmware and
selects encoder displays and starting settings accordingly. It tunes open-loop
PWM ceilings; it does not create closed-loop wheel-speed control.

For automatic Maker starting-threshold, startup-delay and steady-speed matching,
use [MAKER_AUTO_TUNING.md](MAKER_AUTO_TUNING.md). Two [September 4 supervised
runs](BENCH_RESULTS_2026-09-04.md) reproduced the forward starting thresholds,
then aborted on invalid encoder transitions before completing speed matching.
The production tuner requires fresh IMU reports. Later [September 5 wheel-only
trials](test_results/paired-motors-20260905/README.md) used separate supervised
adapters while heading and field control were disabled; they did not modify
that requirement in the production tuner. No verified shared trim was saved.

## What the page can infer

- Each installed encoder response can be compared with its own earlier response.
- A floor observation of nose rotation and off-axis path drift can identify a
  useful four-wheel trim through the mecanum mixing geometry.
- Maker's measured scales support RPM comparison in the automatic bench tool.
  The browser pulse workflow includes startup and is not a steady-speed test.
- Legacy S3 rear ticks do not have that calibration. Never transfer Maker CPR or
  PWM trims to the old chassis.

The PWM value is a wheel's full-command ceiling. A controlled test multiplies
that ceiling by the selected output. For example, a rear PWM ceiling of 200 at
45% output produces an approximate duty of 90. The page previews this duty for
all four wheels before the test.

## 1. Wheels-up response check

1. Secure the chassis so all four wheels are clear of the floor.
2. Open **Encoder response check** and confirm the wheels-up condition.
3. Start at 15% output for 0.75 seconds. Increase output one level at a time
   until the installed channels report nonzero, repeatable response without jerking.
4. Repeat an identical direction/output/duration before judging a change. The
   page suppresses percentage comparisons if output or duration changed.
5. Use this step to find a reliable test threshold and diagnose dead, reversed,
   or intermittent encoder response. Use the automatic bench tool for measured
   speed matching; do not infer it from one short pulse total.

## 2. Guided floor balance

1. Use one flat surface, a consistent battery state, and a marked start line.
2. Use board-specific starting settings: Maker `177 / 177 / 177 / 177` is the
   previously tested raised-wheel output, not a balanced floor trim. Legacy S3
   alone uses `230 / 230 / 200 / 200`. Prefer a subsequently verified Maker bench
   candidate once available.
3. Select **Forward**, 45% output, and 1.0 second for the first test.
4. After the automatic stop, record two separate observations:
   - whether the robot's nose rotated left/counterclockwise or right/clockwise;
   - whether the path drifted off-axis.
5. Start with a 3-count suggested trim. Use 5 only for a pronounced error and
   1–2 for final refinement.
6. Review the proposed PWM values. Applying them is never automatic.
7. Repeat the exact same direction, output, and duration. Compare each installed
   encoder with its own prior rate and score the physical path again.
8. Keep the better result. Use **Restore #N PWM** after a worse trial, **Use #N
   PWM** in history to restore any tested set, or **Undo last PWM change** before
   retesting. Require at least two consistent runs before treating a setting as
   a winner.
9. Tune in this order: forward, reverse, strafe left, then strafe right. A trim
   that helps only one direction is not yet a general winner.

Heading correction stays disabled during motor tests so it cannot hide an
open-loop motor imbalance. **STOP NOW** aborts motion but retains the active
session and live PWM values, allowing inspection or a safe resume.

## 3. Keep, restore, or save

- **Finish · keep live only** ends exclusive tuning control without writing NVS.
- **Restore originals & finish** returns every setting captured at session start.
- **Save PWM & finish** restores the original heading-enabled state first, then
  writes the winning live configuration to ESP32 nonvolatile storage.

Do not save after a single good-looking run. Save only after repeatable results
in both longitudinal directions and both strafe directions.

## Remaining limitation

Maker's encoders are normalized by recorded per-wheel measurements. The automatic
bench tool can measure/adjust/retest PWM, but continuous wheel-speed regulation
still requires firmware integration and physical validation. September 5 pair
and all-wheel trials found a load-dependent front-wheel slowdown and a slower
RL in pair tests. [Peripheral readback](test_results/pwm-readback-20260905/README.md)
confirmed equal 177 duty and 20 kHz settings during the all-wheel slowdown;
physical PWM waveforms, motor-terminal voltage and loaded regulated voltage
were not measured. Establish repeatable behavior before adopting fixed trims.
RL's historical forward/reverse asymmetry may also prevent one shared PWM
ceiling from balancing both directions.

Use **Diagnostics & maintenance → Export tuning session** to download JSON or
CSV. JSON includes board identity, original/live settings, observations and
adjustments; CSV contains a row per wheel per test. Rates are powered-pulse
response estimates including startup, not calibrated steady-speed RPM. Keep
exports with surface, battery and hardware-change notes.
