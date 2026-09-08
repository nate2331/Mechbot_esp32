# FL / M2 frequency sweep — September 6, 2026

Archive note (September 8): the fixed-APB next test proposed below was subsequently completed; see the [APB results](../apb-sweep-2026-09-06/RESULTS.md). The full sequence and final conclusions are in the [archive overview](../README.md).

## Result

The randomized sweep reproduces a large, sustained frequency-associated speed difference on the FL motor/M2 output. The ordering is **248 Hz > 1,000 Hz > 20,000 Hz > 5,000 Hz** in every matched block/direction comparison.

The 5 kHz deficit persists in the final three seconds of the eight-second trials. Startup delay alone therefore does not explain this result. This is evidence of a repeatable response in the tested system; it does not identify the electrical mechanism or establish 248 Hz as an optimum.

## Measurements

All values below are **speed magnitudes in RPM**, averaged over approximately seconds 5–8. Each direction/frequency mean has **three trials**, one in each randomized complete block. The preliminary single 248 Hz run is excluded from these means.

| Requested frequency | Forward mean | Forward range | Reverse mean | Reverse range |
|---|---:|---:|---:|---:|
| 248 Hz | 125.37 | 124.96–125.69 | 122.69 | 122.38–123.15 |
| 1,000 Hz | 103.92 | 102.49–104.97 | 102.57 | 100.84–103.67 |
| 5,000 Hz | 55.29 | 54.67–55.70 | 54.08 | 53.69–54.42 |
| 20,000 Hz | 99.88 | 99.72–100.17 | 98.20 | 97.01–98.96 |

Averaging forward and reverse equally:

- 248 Hz: **124.03 RPM**, **25.23% above 20 kHz**.
- 1 kHz: **103.24 RPM**.
- 5 kHz: **54.68 RPM**, **44.79% below 20 kHz**.
- 20 kHz: **99.04 RPM**.

For each of the six matched block/direction comparisons, 20 kHz exceeds 5 kHz by **43.32–45.04 RPM**. Thus the difference is present throughout the sweep rather than arising from a single exceptional trial. These are descriptive comparisons of three repeated blocks on one output, not population confidence intervals or independent motors.

## Log integrity

The attachment contains one preliminary trial followed by all **24 planned sweep trials**. Every trial reports complete and ends in approximately eight seconds. Each has 81 timestamped samples: 2,025 samples total.

The parser checked unique trials, complete balanced conditions, increasing timestamps, cumulative encoder counters, sample endpoints, and requested/readback frequency agreement. All logged whole-run, first-five-second, and tail RPM values recalculate from the raw counts and the logged 2468.8 counts/turn calibration to the displayed rounding precision.

- Sweep commanded-on intervals: 8.000810–8.000827 seconds.
- Sweep preceding off intervals: 3.001022–3.001174 seconds.
- Reported invalid quadrature transitions: **zero across all 25 trials**.
- First observed encoder activity in the sweep: approximately 1.8–10.8 ms after the software command.

First-edge latency is a loop-observed event, not a scope measurement or a direct measurement of torque. Zero invalid transitions does not prove that no complete quadrature cycles were missed.

## Interpretation

The settings remain FL/M2, duty **175/256**, eight-bit **history/AUTO**, Arduino ESP32 3.3.11 / IDF 5.5.5. The log does not measure supply voltage, current, load, or temperature; the earlier 9 V setup cannot be independently confirmed from this file.

The late-window results make the previous startup-only explanation inadequate. The strong 5 kHz trough also means a simple statement that lower frequency always increases this motor's speed is inconsistent with these observations: reducing 20 kHz to 5 kHz substantially lowers measured speed, while reducing it further to 1 kHz or 248 Hz raises speed.

There are only four widely separated tested frequencies. No observation yet distinguishes 248 from 240, 250, or other nearby frequencies. The best measured speed among these four settings is not automatically the best operating choice for load, heat, noise, efficiency, or low-duty startup.

The current history profile also allows automatic PWM clock selection. The next comparison should hold the clock source fixed before assigning the entire effect to the carrier frequency.

## Next test

Leave wheel, wiring, duty, supply, and load unchanged. From IDLE, send each command on its own line:

    profile apb
    sweep

This runs the same frequency set with nine-bit APB PWM and duty **350/512**, exactly equal to **175/256**. It requires no firmware change. Keep this log separate from the history profile.

Compare its 5–8 second speeds with the table above. If the same pattern persists, automatic clock-source switching cannot account for it by itself. If it changes materially, follow the existing history/ref/apb bridge comparisons to separate clock and resolution effects. Either outcome still requires electrical waveforms to identify a driver or supply mechanism.

## Reproducibility files

- source-log.txt: byte-for-byte copy of the supplied attachment.
- trials.csv: 25 trial summaries plus recomputed diagnostic windows.
- samples.csv: all 2,025 raw timestamped samples.
- analysis.json: checks, grouped descriptive statistics and paired comparisons.
- analyze_log.py: dependency-free parser and recalculation script.

Source SHA-256: d7e386769aec26ccbb78d55b0e52862fd14d8346c5af44306c9a4438f9c47b77.

The supplied data were analyzed without operating the robot or changing its firmware.
