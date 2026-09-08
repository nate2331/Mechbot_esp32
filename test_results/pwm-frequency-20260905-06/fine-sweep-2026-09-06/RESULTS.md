# Fine-frequency sweep: 248 Hz is within a broad high-speed region

Archive note (September 8): the duty-150 fine sweep proposed below was subsequently completed; see the [lower-duty results](../fine-duty150-2026-09-06/RESULTS.md). The full sequence and final conclusions are in the [archive overview](../README.md).

**September 6, 2026 · FL motor / M2 · fixed APB · nine bits · duty 350/512**

## Finding

The fine sweep does not show a distinct peak at 248 Hz. At this motor, output, and duty, 248 and 250 Hz produce nearly identical late-window speed. The highest measured mean is at 200 Hz, the lowest frequency sampled, but its advantage over 248 Hz is under 1%.

This supports selecting from a useful low-frequency region rather than treating 248 as a uniquely optimal physical constant. It does not establish the best frequency for loaded torque, current, heat, noise, or lower-duty startup.

## Late-window speeds

Means use approximately seconds 5–8 of each eight-second trial. Reverse speeds are magnitudes. Each directional mean has three trials; the combined mean weights the six trials equally.

| Frequency | Forward RPM | Reverse RPM | Combined RPM | Difference from 248 Hz |
|---|---:|---:|---:|---:|
| 200 Hz | 129.00 | 126.29 | 127.65 | +0.89% |
| 240 Hz | 128.32 | 125.12 | 126.72 | +0.16% |
| 248 Hz | 128.01 | 125.02 | 126.52 | reference |
| 250 Hz | 127.81 | 124.89 | 126.35 | −0.13% |
| 256 Hz | 127.53 | 124.99 | 126.26 | −0.20% |
| 300 Hz | 126.62 | 123.49 | 125.05 | −1.16% |
| 500 Hz | 119.93 | 118.29 | 119.11 | −5.86% |
| 1,000 Hz | 107.30 | 105.28 | 106.29 | −15.99% |

The combined means from 240 through 256 Hz span only about **0.36%** of the 248 Hz mean. Across 200–300 Hz, the span is about **2.03% of the highest mean**. These are descriptive ranges at the tested points, not guarantees about every intervening frequency or other operating conditions.

### 248 versus 250 Hz

The pooled difference is only **0.1647 RPM**, approximately 0.13%. In matched block/direction comparisons, 250 minus 248 Hz ranges from −0.5833 to +0.4051 RPM. Thus the ordering is not consistent: 250 wins two of the six comparisons, and 248 wins four.

The result provides no compelling operating-speed reason to favor exactly 248 over 250 Hz. This is not a formal equivalence test; three repeats per direction and one motor/output do not establish universal interchangeability.

### 200 versus 248 Hz

200 Hz exceeds 248 Hz in all six matched comparisons, by 0.7939–1.6607 RPM; the average difference is 1.1315 RPM. That is a small, repeatable speed advantage within this sweep.

Because 200 Hz is the lower endpoint, the experiment has not found the location of an optimum. Faster measured wheel speed alone is also insufficient to justify changing the robot's operating default.

### Larger frequency changes

500 Hz is approximately 5.86% below 248 Hz, and 1 kHz is approximately 15.99% below it. The combined means decrease gradually over this sampled low-frequency interval. This must not be extrapolated to the entire frequency range: the earlier fixed-APB sweep demonstrated a large 5 kHz trough followed by recovery at 20 kHz.

Absolute means across successive files should not be pooled as if every condition was randomized in one experiment. Temperature, supply behavior and other session drift were not measured.

## Validation

The supplied attachment contains sequence 5, trials 57–104: all **48 planned trials**. Each of three complete randomized blocks contains eight frequencies in both directions.

- All 48 trials report complete.
- There are **3,888 samples**, 81 per trial.
- All requested frequencies agree with integer peripheral readback.
- All reported invalid transitions are zero.
- Valid edge totals equal the magnitude of signed net counts in all trials; no opposite-direction steps are detected by that comparison.
- Powered durations are 8.000808–8.000829 seconds.
- Preceding off intervals are 3.001037–3.001163 seconds.
- Every printed RPM summary independently recalculates from raw counts and the logged 2468.8 counts/turn calibration to printed rounding precision.
- The maximum absolute change from a trial's 5–6 second window to its 7–8 second window is **0.1942 RPM**.

The three forward 240 Hz tail means coincide exactly at the printed precision because they have identical net tail counts and duration. This does not imply zero measurement uncertainty.

The first encoder activity is loop-observed, not a direct torque or oscilloscope measurement. Zero invalid transitions does not exclude missed complete quadrature cycles. Supply voltage, current, temperature and load are not logged.

## Decision and next test

Retain the existing explicit 248 Hz setting as a working baseline. This sweep gives no substantial speed benefit for changing it to 250 Hz, and less than a 1% benefit for moving to 200 Hz under the present conditions. It also gives no basis for claiming that 248 is uniquely required.

The next useful software-only experiment is the same fixed-APB fine sweep at the previously relevant lower duty of 150/256. From IDLE, send each command on its own line:

    duty 150
    fine

The APB profile remains selected. The sketch scales that command to **300/512**. With three blocks this takes approximately nine minutes. It tests whether the useful region survives a lower command and may reveal frequency-dependent startup or running boundaries. It does not itself identify a sleep, current, or supply mechanism.

If a trial stops for no_encoder_edges, retain the partial log and inspect the motor/encoder before restarting. Missing trials are not successful results. After testing, duty 175 restores the previous command while IDLE.

Scope/current measurements and loaded trials remain necessary to identify the electrical cause and choose an operating frequency with meaningful performance margin.

## Reproducibility

The directory includes source-log.txt, trials.csv, samples.csv and analysis.json. The parser is ../analyze_pwm_log.py, invoked with --sequence 5.

Attachment SHA-256: b10d9e633f664a430d0f0ab1d876d925c64bcb39aaca1e5345eb677fb56a7da5.

The source file was analyzed without operating the robot or changing its firmware.
