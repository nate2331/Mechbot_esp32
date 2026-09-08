# Duty-150 broad sweep: the 5 kHz slowdown persists

September 6, 2026. Sequence 7, trials 153–176. FL motor / M2, APB clock, nine-bit PWM, duty 300/512 = 150/256 = 58.59375%.

## Finding

The measured late-window speed at 5 kHz is 30.75 RPM, versus 80.60 RPM at 20 kHz. Thus 5 kHz is 61.85% slower than 20 kHz, and the 20 kHz speed is 2.621 times the 5 kHz speed. The gap is present in all six matched block/direction comparisons and is not a startup-only effect.

The rank is 248 Hz > 1 kHz > 20 kHz > 5 kHz in every block and both directions. This independently repeats the rank previously seen at duty 175 with the same APB profile. It establishes a repeatable nonmonotonic relationship between the commanded frequency and measured speed in this setup, without yet identifying its electrical cause. A slowdown at one sampled intermediate frequency does not locate the actual minimum or establish a narrow notch/resonance.

## Measurements

RPM values are speed magnitudes calculated over approximately seconds 5–8 of each eight-second trial, using the logged 2468.8 encoder counts per output revolution. Each directional mean has three trials; combined means weight all six trials equally.

| Frequency | Forward RPM | Reverse RPM | Combined RPM | Earlier APB duty-175 RPM |
|---:|---:|---:|---:|---:|
| 248 Hz | 121.92 | 119.90 | 120.91 | 125.87 |
| 1,000 Hz | 97.01 | 95.60 | 96.30 | 105.43 |
| 5,000 Hz | 30.96 | 30.54 | 30.75 | 55.60 |
| 20,000 Hz | 81.53 | 79.67 | 80.60 | 101.26 |

At duty 150, 248 Hz is 50.01% faster than 20 kHz and 25.55% faster than 1 kHz. These are speed comparisons at equal commanded duty, not torque, efficiency or available power measurements.

| Paired comparison | Mean difference | Minimum–maximum difference across six pairs |
|---|---:|---:|
| 20 kHz minus 5 kHz | 49.84875 RPM | 48.85771–50.85058 RPM |
| 248 Hz minus 20 kHz | 40.31106 RPM | 39.36323–41.02395 RPM |
| 248 Hz minus 1 kHz | 24.60708 RPM | 24.07646–25.29162 RPM |
| 1 kHz minus 20 kHz | 15.70398 RPM | 14.43615–16.28320 RPM |

All paired differences have the same sign. Trial repetitions are the analysis units; 100 ms samples are not independent experimental replicates. Both directions use one physical motor/output, not independent hardware samples. Directional trial standard deviations range from approximately 0.12 to 0.81 RPM, far below the 49.85 RPM 20 kHz/5 kHz difference.

## Duty comparison and session drift

The command reduction from 175 to 150 is 14.29% relative. Compared with the earlier fixed-APB broad sweep, late-window speeds fell by 3.94% at 248 Hz, 8.66% at 1 kHz, 44.69% at 5 kHz and 20.40% at 20 kHz. The 5 kHz deficit relative to 20 kHz expanded from 45.09% to 61.85%.

This is evidence of a stronger frequency effect in the lower-duty session, but it is not a randomized estimate of duty's causal effect. The two duties were run in different sessions without logged supply voltage, temperature or load. The immediate preceding duty-150 fine sweep also provides repeated anchors: this broad sweep is 0.75% lower at 248 Hz and 1.02% lower at 1 kHz. That demonstrates why small absolute changes between files should not be overinterpreted. The large within-sweep 5 kHz/20 kHz difference remains repeatable across shuffled blocks.

## What the experiment narrows down

1. **A clock-source transition is not required.** Both the earlier and current APB sweeps show the slowdown with APB explicitly requested at every frequency. The log confirms successful configuration and integer frequency readback, rather than an independently measured output waveform.
2. **Startup delay alone is insufficient.** The approximately 50 RPM gap persists during seconds 5–8. Within any individual trial, the maximum absolute difference between seconds 5–6 and 7–8 is only 0.243 RPM.
3. **A single fixed per-pulse turn-on delay is insufficient by itself.** At the same duty, a constant lost time per cycle predicts a larger lost duty fraction as frequency increases. That simple model cannot by itself account for recovery when increasing frequency from 5 to 20 kHz. This does not exclude more complex driver behavior, changes in current decay, or interacting effects.
4. **The exact number 248 is not needed to explain the observed low-frequency advantage.** The preceding fine sweeps showed a gradual response around 240–256 Hz and slightly higher speed at 200 Hz. The stronger diagnostic feature is the intermediate-frequency slowdown and subsequent recovery.

For clarity, the ideal command timing at duty 150 is:

| Frequency | Period | HIGH interval | LOW interval |
|---:|---:|---:|---:|
| 248 Hz | 4,032.258 us | 2,362.651 us | 1,669.607 us |
| 1 kHz | 1,000 us | 585.938 us | 414.063 us |
| 5 kHz | 200 us | 117.188 us | 82.813 us |
| 20 kHz | 50 us | 29.297 us | 20.703 us |

These are calculations from nominal frequency and duty, not measured driver input or motor-terminal waveforms. The simple fixed-delay model is D_effective = max(0, D - f * t_delay), under the explicit assumption that each cycle loses the same time and the other operating conditions are fixed. Its failure to explain the complete response is a limitation of that model, not proof of a particular alternative mechanism.

## Audit

- All 24 planned trials complete, in three balanced shuffled blocks.
- 1,944 samples, 81 per trial.
- All selected trials report FL, APB, nine bits, 300/512 duty, and matching requested/integer readback frequencies.
- Every printed full, first-five-second and late-window RPM independently recalculates from raw timestamps/counts to printed rounding precision.
- Zero invalid encoder transitions.
- Two trials contain one inferred opposite-direction valid step each: trial 154 first observed at 5.500817 seconds, and trial 172 at 3.400817 seconds. These are valid transition counts, not invalid transitions. They do not establish whether the cause is physical motion or electrical disturbance.
- One encoder count over three seconds represents approximately 0.00810 RPM. Even treating a reversed step as two counts of error is orders of magnitude too small to explain the 49.85 RPM difference. This observation does not exclude unobserved missed complete quadrature cycles.
- Powered durations: 8.000804–8.000823 seconds.
- Preceding off intervals: 3.001031–3.001166 seconds.
- First encoder edge observed by the main loop: 0.817–15.817 ms; this is not direct measurement of rotor acceleration or bridge switching.
- Maximum absolute change from a trial's 5–6 second to 7–8 second speed window: 0.24297 RPM.
- Log ends in IDLE with all outputs LOW.

## Next decisive experiment

The software sweeps have now reproduced the core response sufficiently to prioritize mechanism measurements. Keep FL/M2, duty 150 and the documented supply/load conditions, and compare 248 Hz, 5 kHz and 20 kHz while recording:

- Driver input PWM, to verify delivered frequency, duty and pulse integrity.
- Differential motor-terminal voltage, to check whether commanded pulses reach the winding and whether there are delays or interruptions.
- Motor current, to identify current decay and any interruption pattern rather than inferring it from speed.
- Supply voltage at the driver, to identify frequency-dependent droop or ripple.

An independent tachometer or optical rotation measurement at 5 and 20 kHz would first confirm that the encoder speed ratio is physical. Output voltage and current traces can then distinguish normal pulse delivery with different winding behavior from missing/delayed output or supply disturbance. More than one mechanism may contribute. A correctly rated differential method is needed across H-bridge motor terminals; an earth-referenced oscilloscope ground must not be connected to a bridge output.

Retain 248 Hz as the existing working baseline while investigating. The current evidence supports useful speed at low frequencies, but it does not establish the optimal setting for loaded torque, current, efficiency, heating, all four motors, or endurance. No hardware was operated or firmware changed by this analysis.

## Reproduction

Files retained here: source-log.txt, trials.csv, samples.csv and analysis.json. Parser: ../analyze_pwm_log.py, invoked with --sequence 7. Comparisons use ../apb-sweep-2026-09-06/analysis.json and ../fine-duty150-2026-09-06/analysis.json.

Input SHA-256: 0961947f0471bb27ca410e80a8c4711476d48b74e3cf4f8d5b88d877fd456a3a.
