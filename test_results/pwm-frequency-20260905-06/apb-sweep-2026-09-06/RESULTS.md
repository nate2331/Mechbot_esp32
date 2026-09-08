# Fixed-APB sweep compared with the history sweep

Archive note (September 8): the fine sweep proposed below was subsequently completed; see the [fine-sweep results](../fine-sweep-2026-09-06/RESULTS.md). The full sequence and final conclusions are in the [archive overview](../README.md).

**September 6, 2026 · FL motor / M2 output**

## Finding

The large frequency-dependent speed pattern persists with APB fixed across all conditions. **Automatic REF_TICK/APB clock switching is not required for the observed trough at 5 kHz.** Startup delay alone was already inadequate after the first sweep; the new late-window measurements reproduce the sustained difference.

This establishes the effect with a fixed requested clock, fixed nine-bit resolution, and fixed commanded duty fraction. It does not establish whether the physical cause lies in PWM delivery, the driver, motor current dynamics, the supply, or measurement behavior.

## Profile comparison

Values are arithmetic means of RPM magnitudes over approximately seconds 5–8, with equal weighting of three forward and three reverse trials.

| Frequency | History: AUTO, 8 bits | APB fixed, 9 bits |
|---|---:|---:|
| 248 Hz | 124.03 | 125.87 |
| 1,000 Hz | 103.24 | 105.43 |
| 5,000 Hz | 54.68 | 55.60 |
| 20,000 Hz | 99.04 | 101.26 |

The duty fractions are exactly equal: **175/256 = 350/512 = 68.359375%**.

Within the fixed-APB sweep:

- 5 kHz averages **45.09% below 20 kHz**.
- 248 Hz averages **24.30% above 20 kHz**.
- 248 Hz averages **19.38% above 1 kHz**.
- The ranking **248 > 1,000 > 20,000 > 5,000 Hz** holds in every matched block/direction comparison.
- The 20 kHz minus 5 kHz difference is **44.82–46.66 RPM** across the six matched comparisons.

The APB means are about 1.5–2.2% higher than the earlier means. The sessions were sequential, not a randomized clock-only crossover, and resolution changed too. Thermal, supply, or other drift was not measured. Therefore the small increase must not be attributed specifically to APB or nine-bit resolution.

## Fixed-APB results by direction

Each mean has three trials. Ranges are the observed minimum and maximum trial tail means, not confidence intervals.

| Frequency | Forward mean (range), RPM | Reverse magnitude mean (range), RPM |
|---|---:|---:|
| 248 Hz | 127.36 (126.91–127.98) | 124.37 (124.21–124.52) |
| 1,000 Hz | 106.20 (105.10–106.94) | 104.67 (104.03–105.14) |
| 5,000 Hz | 56.37 (55.95–56.63) | 54.83 (54.61–55.01) |
| 20,000 Hz | 102.42 (102.20–102.61) | 100.10 (99.43–100.57) |

The largest absolute change between a trial's 5–6 second and 7–8 second windows is **0.2672 RPM**. The gap between frequencies therefore persists while late-window speeds are changing very little.

## Data checks

The supplied log contains sequence 4, trials 33–56: all **24 planned trials**, three complete blocks, both directions at each of four frequencies. All report complete. Each has 81 samples, giving **1,944 timestamped samples**.

The parser checked row structure, unique trials, increasing timestamps, cumulative counters, balanced conditions, frequency readback agreement, and complete trial endpoints. Every printed RPM summary recalculates from the raw signed counts and recorded 2468.8 counts/turn calibration within its printed rounding precision.

- Powered command-relative duration: 8.000793–8.000828 seconds.
- Preceding off interval: 3.001047–3.001166 seconds.
- Invalid transitions reported: **zero**.
- Four trials contain one valid opposite-direction count each, inferred from valid_edges minus absolute net ticks equaling two. Trials 44, 47, 51, and 52 first show these at approximately 1.50, 0.10, 0.10, and 5.60 seconds, respectively.

Those few opposite steps are not invalid quadrature transitions. They could represent tiny motion reversals or signal behavior; the log does not identify the cause. They do not account for differences of roughly 45 RPM. Zero invalid transitions still does not exclude missed complete quadrature cycles.

The serial log confirms the requested APB setting, nine-bit resolution and duty 350/512. Frequency readback is integer peripheral data, not an independently measured GPIO waveform. Supply voltage, motor current, temperature and mechanical load are not recorded.

## Next experiment

To determine whether the useful region is centered on 248 or is a broader range, keep the current wiring, wheel, supply and duty unchanged. Remain in the APB profile and send:

    fine

This tests **200, 240, 248, 250, 256, 300, 500 and 1,000 Hz**, in both directions with three blocks: **48 trials, approximately nine minutes**.

It addresses the numerical choice of frequency. Explaining the physical mechanism still requires comparing measured PWM input, driver output/current, and supply behavior at selected frequencies; software logs alone cannot provide those electrical measurements.

## Reproducibility

Files in this directory:

- source-log.txt — byte-for-byte original attachment.
- trials.csv — summaries and independently recomputed diagnostic windows.
- samples.csv — all raw timestamped samples.
- analysis.json — checks, group statistics and matched comparisons.
- profile-comparison.csv — history/APB pooled mean comparison.

Parser: ../analyze_pwm_log.py, invoked with --sequence 4. The original history analysis is preserved in ../history-sweep-2026-09-06/.

New attachment SHA-256: 88c79ac853266f2676321d58eecb449d53015905a53eb18ea339fe05c641b560.

This analysis did not change firmware or operate the robot.
