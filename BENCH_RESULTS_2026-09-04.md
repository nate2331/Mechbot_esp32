# Maker bench results — September 4, 2026

Status: **forward startup measurements reproduced exactly; both speed-matching
attempts stopped on a new encoder-invalid transition during all-wheel operation.
No new trim was approved or saved.** Times in the raw files use UTC September 5;
the operator's local date was September 4.

The operator confirmed powered, raised wheels and requested automatic tuning.
Normal M2/E2=FL, M3/E3=FR, M1/E1=RL, M0/E0=RR wiring was the previously confirmed
setup. Battery voltage, motor temperature and loaded geometry were not measured.
The bridge's maintenance mode released serial and stopped its command senders;
the tuner used exclusive serial ownership. Maximum PWM remained 177.

## Forward startup measurements

Each threshold passed three final starts from rest and sustained motion during
the final two encoder intervals of a 1.2-second pulse. These are bench starting
thresholds, not holding duty or installed minimum-output compensation.

| Wheel | Minimum reliable forward-start PWM / 255 | Fraction of the 177 ceiling | Final confirmation starts |
| --- | ---: | ---: | ---: |
| FL | 119 | 67.2% | 3 |
| FR | 119 | 67.2% | 3 |
| RL | 155 | 87.6% | 3 |
| RR | 131 | 74.0% | 3 |

The search recorded 107 startup probes in total. Every threshold's immediately
lower tested duty failed the start-and-sustain criterion. Encoder timing gives
roughly 200 ms start brackets, so these results do not establish millisecond
differences between wheels. Individual brackets and powered tail speeds are in
the report; speeds measured at different PWM values are not a matched-speed test.

Rear-left needs 36 more PWM counts than either front wheel to start reliably in
this setup. Even 60% of the maximum permitted ceiling is only about 106 PWM,
below all four measured thresholds. Simple ceiling trims therefore cannot be
assumed to provide reliable starts across the requested command range. Reverse
thresholds and minimum holding duty are still unmeasured.

## Full automatic retry

The operator requested an immediate retry under the same powered, raised-wheel
setup. All four forward thresholds reproduced exactly: FL 119, FR 119, RL 155 and
RR 131. Final confirmation speeds were also closely grouped within each wheel:
FL 34.1–34.5, FR 39.4–40.1, RL 53.5–54.2 and RR 48.0–49.4 RPM. These speeds were
measured at different PWM thresholds and are not a matched-duty comparison.

The retry again reached forward speed matching and aborted on an increased
invalid-transition counter. This time FR increased from 0 to 1 while RL remained
at 2; FL and RR remained at 0. The error is therefore not confined to RL. Both
full attempts encountered a rare invalid state during the first all-wheel
matching sequence, on different encoder channels.

This points toward simultaneous encoder interrupt load, incoherent A/B sampling,
grounding, supply disturbance, or motor noise across the encoder wiring. RL's
high starting threshold and earlier drag/coast behavior remain wheel-specific
mechanical or motor observations, separate from this multi-channel encoder issue.

## Automatic run and stop

The automatic run lasted approximately 406 seconds including ownership changes.
After completing forward startup tests, it began the forward speed-matching
stage and aborted on `Encoder invalid transitions increased during the pulse`.
The RL counter increased from 0 to 1; FL, FR and RR remained at 0. No complete
three-repeat speed-matching measurement was recorded. Reverse startup, reverse
matching and shared-trim verification were not reached.

The IMU remained valid through the checked sequence, with reset/reinitialization
counters unchanged at 3/1. This is useful raised-wheel evidence but does not
resolve the historical intermittent IMU fault for every operating condition.

## Targeted encoder diagnostic

Using the same pulse implementation and abort rules, with raw TX/RX logging:

- Three isolated RL-forward pulses at 177 PWM, each 2.4 seconds: no new invalid
  transitions. Powered tail speeds were approximately 77.3, 79.3 and 79.3 RPM.
- The first all-wheel forward pulse at 177 PWM reproduced the fault: RL increased
  from 1 to 2, and the test aborted. Other wheels remained at 0.
- The new count was first observed in the stopped DIAG read after that pulse.
  The available samples cannot locate it precisely within the powered interval
  versus coast-down. Do not describe it as proven to occur only while powered.

The fault is repeatable in the all-wheel test sequence and was absent in these
three isolated RL trials. That does not identify a failed part. Electrical
signal disturbance and missed encoder state changes are still alternatives:
the current firmware counts `previousState XOR currentState == 3` as invalid and
samples A/B with separate GPIO reads in its shared encoder ISR critical section.
The earlier RL motor/gearbox drag observation remains a separate concern.

## Restored state and next work

Both runs stopped all four outputs, restored PWM 177/177/177/177 and the original
heading configuration, closed exclusive serial and released maintenance. Both
restoration reports and independent postflight CFG GET snapshots match all nine
original settings. Heading/field control stayed off, the gamepad was disconnected,
and the bridge required deadman rearm. No CFG SAVE or firmware upload occurred.

1. With motor power off, inspect all encoder connectors, shared grounding, cable
   routing and supply connections, paying particular attention to RL and FR.
   Inspect RL mechanical resistance separately. Record any physical change.
2. Investigate simultaneous encoder sampling and capture A/B signals if
   available. Consider an atomic GPIO-register snapshot in each ISR and measure
   whether all-wheel interrupt load causes skipped states. Add closer diagnostic
   observations to distinguish powered motion from coast-down. Do not assume
   that increasing PWM fixes a sensing fault.
3. Repeat the isolated-versus-all-wheel comparison after a justified change.
   Once counts remain clean, complete reverse startup and directional matching.
4. Before implementing low-speed compensation, measure holding duty and both
   directions. Keep the watchdog/reversal behavior and explicit output limits.
   A shared ceiling trim alone does not supply a startup boost.

## Evidence

- [Automatic report](test_results/maker-bench-20260905-koa59no2/results.json),
  [progress log](test_results/maker-bench-20260905-koa59no2/run.log), and pre/post
  settings and status in the same directory.
- [Targeted diagnostic report](test_results/maker-encoder-diagnostic-20260905-s1s7fr_s/results.json),
  [raw serial log](test_results/maker-encoder-diagnostic-20260905-s1s7fr_s/serial.jsonl),
  and pre/post snapshots in the same directory.
- [Full automatic retry](test_results/maker-bench-retry-20260905-djty58cn/results.json),
  with progress log and pre/post snapshots in the same directory.
- Original Pi copies: `/home/nate/maker-bench-20260905-koa59no2` and
  `/home/nate/maker-encoder-diagnostic-20260905-s1s7fr_s`.
- Retry Pi copy: `/home/nate/maker-bench-retry-20260905-djty58cn`.
- Automatic tuner SHA256:
  `eb385257c32f5dbb90d826cfae4d00f9e4a99d246665edd40d96fbf0db8a76cb`.
