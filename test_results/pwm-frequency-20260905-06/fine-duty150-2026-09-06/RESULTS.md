# Fine sweep at duty 150: a gradual frequency response around 248 Hz

Archive note (September 8): the duty-150 broad sweep proposed below was subsequently completed; see the [broad-sweep results](../apb-duty150-sweep-2026-09-06/RESULTS.md). The full sequence and final conclusions are in the [archive overview](../README.md).

September 6, 2026. Sequence 6, trials 105–152. FL motor / M2, fixed APB clock, nine-bit PWM, duty 300/512 = 150/256 = 58.59375%.

## Result

All 48 trials completed. Combined late-window mean speed decreases at every sampled frequency from 200 to 1,000 Hz. There is no distinct maximum at 248 Hz or large speed discontinuity between 240 and 248 Hz. This finding applies to the sampled settings and this motor/output under the test conditions; it does not establish behavior at every intermediate frequency.

The speed advantage of 200 over 248 Hz is 2.07794 RPM, or 1.706%. The advantage of 248 over 250 Hz is 0.27404 RPM, or 0.225% of the 248 Hz reference. The sampled 240–256 Hz means span 0.82901 RPM, or 0.680% of the 248 Hz mean.

## Comparison with the preceding duty-175 fine sweep

Each cell is the average magnitude of speed over approximately seconds 5–8, with three trials in each direction, equally weighted. These are wheel/output RPM using the logged 2468.8 encoder counts per turn.

| Frequency | Duty 175/256 RPM | Duty 150/256 RPM |
|---:|---:|---:|
| 200 Hz | 127.65 | 123.90 |
| 240 Hz | 126.72 | 122.17 |
| 248 Hz | 126.52 | 121.83 |
| 250 Hz | 126.35 | 121.55 |
| 256 Hz | 126.26 | 121.34 |
| 300 Hz | 125.05 | 119.60 |
| 500 Hz | 119.11 | 113.08 |
| 1,000 Hz | 106.29 | 97.29 |

The relative advantage of 200 over 248 Hz increased from about 0.89% to 1.71%. At duty 150, 248 Hz is 25.22% faster than 1,000 Hz. These are descriptive observations. Duty was changed between successive sessions, not randomized within the same session, and supply voltage, motor temperature and load were not recorded. Consequently the exact between-session changes cannot all be assigned to duty alone.

### Repeatability within the duty-150 sweep

Comparisons below pair trials by block and direction; each comparison has six pairs. Trial counts, rather than the 100 ms samples, are the replication units. Both directions share one motor, so these are not six independent motors.

| Comparison | Mean advantage | Range across matched pairs | Pair ordering |
|---|---:|---:|---|
| 200 over 248 Hz | 2.07794 RPM | 1.77414–2.34142 RPM | 6 of 6 |
| 240 over 248 Hz | 0.34298 RPM | 0.02430–0.85082 RPM | 6 of 6 |
| 248 over 250 Hz | 0.27404 RPM | −0.03256–0.57518 RPM | 5 of 6 |
| 248 over 1,000 Hz | 24.53549 RPM | 24.04385–25.11340 RPM | 6 of 6 |

The small 248/250 difference is descriptive, not a formal equivalence result. Frequency means decline in both directions separately as well as in the combined table.

## What this says about the sleep hypothesis

The manufacturer-authored SS6625E datasheet gives a typical 1.7 ms interval with both inputs LOW before sleep, and a typical 60 microsecond startup time applying to initial power-up or exit from sleep. Neither is supplied with minimum/maximum timing limits. Source: [SS6625E datasheet, pages 4 and 8](https://datasheet4u.com/pdf/1596900/SS6625E.pdf). A retained local copy was reread for the original analysis; a fresh online opening of the mirror failed. The downloaded PDF is not redistributed in this archive; see the [driver source notes](../PWM_DRIVER_SOURCES.md) for provenance.

With the test sketch's drive/coast control and duty 150/256, the nominal both-LOW interval is:

    t_off = (1 - 150/256) / frequency

| Frequency | Nominal both-LOW interval |
|---:|---:|
| 200 Hz | 2.07031 ms |
| 240 Hz | 1.72526 ms |
| 248 Hz | 1.66961 ms |
| 250 Hz | 1.65625 ms |
| 256 Hz | 1.61743 ms |

The boundary calculated from the typical 1.7 ms value is approximately 243.57 Hz. Thus 240 and 248 Hz straddle that nominal boundary, while 200 Hz has a longer off interval still. Nevertheless 240 Hz is slightly faster than 248 Hz in every matched pair, and 200 Hz is faster again. The prediction of a large speed collapse below that typical boundary is not supported by this experiment.

This does not show that the driver never sleeps. A modest wake penalty could coexist with other frequency effects, and the actual sleep threshold is not measured. RPM alone cannot identify that internal state. In particular, the 60 microsecond wake time must not be subtracted from every PWM pulse without evidence that the device actually enters sleep between pulses.

## Audit

- 48 of 48 planned trials complete, in three balanced blocks containing all eight frequencies and both directions.
- 3,888 samples, 81 per trial.
- Zero reported invalid encoder transitions and zero opposite-direction steps inferred from valid-edge totals versus signed counts.
- Every requested frequency agrees with the integer peripheral readback; this is not an oscilloscope measurement of the input or bridge output waveform.
- Every printed full-trial, first-five-second and late-window RPM recalculates from the raw timestamps/counts to printed rounding precision.
- Powered durations: 8.000797–8.000826 seconds.
- Preceding off intervals: 3.001052–3.001165 seconds.
- First encoder edge observed by the main loop: 4.819–14.817 ms. This is not a direct torque-onset measurement.
- Maximum absolute change between a trial's 5–6 second and 7–8 second speed windows: 0.31572 RPM, small compared with the 248/1,000 Hz gap.
- Log ends with IDLE, all outputs LOW.

Zero invalid transitions cannot exclude missed complete quadrature cycles. No current, bridge voltage, supply voltage, temperature or independently measured load is included in this log. Successful eight-second trials do not establish thermal endurance or loaded startup reliability.

## Next step

Keep 248 Hz as the existing working reference; these results do not require a change in the operating default. 200 Hz has a repeatable speed advantage in this test, but choosing the operating frequency also requires current, heating and loaded performance evidence. An endpoint speed maximum does not locate an optimum.

For the next software-only comparison, leave APB and duty 150 selected and issue `sweep`. With three blocks, this runs 24 trials in approximately 4.5 minutes and tests 248, 1,000, 5,000 and 20,000 Hz in both directions. It will show whether the previously measured 5 kHz trough and 20 kHz recovery persist at this lower duty. The command is already supported; no upload is needed. This analysis did not operate the hardware.

The decisive mechanism experiment is synchronized capture of driver input PWM, differential motor-terminal voltage, motor current and driver supply voltage at 248 Hz, 5 kHz and 20 kHz under documented supply/load conditions. Input/output comparison tests pulse delivery and delays; current and supply traces test current decay, interruption and supply sag. Another speed sweep can characterize the response, but cannot by itself separate these mechanisms. Proper differential measurement is required across the motor terminals; an earth-referenced scope ground must not be connected to an H-bridge output.

## Reproduction

Retained files: source-log.txt, trials.csv, samples.csv and analysis.json. Analysis uses ../analyze_pwm_log.py with --sequence 6. The duty-175 comparison uses ../fine-sweep-2026-09-06/analysis.json.

Input SHA-256: 9a8c78a912896d5c833bd5e9b46bffd2fc77d025c66bdcf03319a445110fb554.
