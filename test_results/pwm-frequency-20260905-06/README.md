# PWM investigation and frequency sweeps — September 5–6, 2026

The weekend tests established a repeatable frequency-dependent speed response and a useful low-frequency region. **248 Hz works as a reference setting, but it is not a uniquely optimal motor frequency.** The strongest unresolved feature is the large slowdown at 5 kHz followed by recovery at 20 kHz.

This archive was assembled on September 8 from the local **Research PWM 248 Hz Fix** task. It preserves the completed September 6 measurements alongside the September 5 driver research and September 6 source audit. The reports describe historical, user-operated tests. Archiving and recalculating them did not operate or flash the robot.

## Measured results

All five datasets use the physical FL motor on board output M2 and encoder calibration **2468.8 counts per output revolution**. Trials last approximately eight seconds, with at least three seconds off between trials. Each sweep has three shuffled complete blocks, testing both directions at every frequency. The table uses equally weighted magnitudes of encoder-derived speed over approximately seconds 5–8; each frequency mean includes three forward and three reverse trials.

| Frequency | History/AUTO, duty 175/256 | Fixed APB, duty 350/512 | Fixed APB, duty 300/512 |
|---:|---:|---:|---:|
| 248 Hz | 124.03 RPM | 125.87 RPM | 120.91 RPM |
| 1,000 Hz | 103.24 RPM | 105.43 RPM | 96.30 RPM |
| 5,000 Hz | 54.68 RPM | 55.60 RPM | 30.75 RPM |
| 20,000 Hz | 99.04 RPM | 101.26 RPM | 80.60 RPM |

The history profile uses eight-bit PWM and automatic clock selection; the APB profile fixes the clock and uses nine bits. **175/256 and 350/512 are exactly the same duty fraction.** At the lower command, 300/512 equals 150/256. These sequential sessions should not be pooled as a randomized clock-only or duty-only experiment.

The ordering **248 > 1,000 > 20,000 > 5,000 Hz** holds in every matched block/direction comparison in all three broad sweeps. In the fixed-APB duty-150 sweep, 5 kHz is **61.85% slower** than 20 kHz, while 248 Hz is **50.01% faster** than 20 kHz. These are speed comparisons at equal commanded duty, not measurements of torque, efficiency or available power.

The two fine sweeps show why the exact number 248 should not be treated as special:

| Frequency | Fixed APB, duty 350/512 | Fixed APB, duty 300/512 |
|---:|---:|---:|
| 200 Hz | 127.65 RPM | 123.90 RPM |
| 240 Hz | 126.72 RPM | 122.17 RPM |
| 248 Hz | 126.52 RPM | 121.83 RPM |
| 250 Hz | 126.35 RPM | 121.55 RPM |
| 256 Hz | 126.26 RPM | 121.34 RPM |
| 300 Hz | 125.05 RPM | 119.60 RPM |
| 500 Hz | 119.11 RPM | 113.08 RPM |
| 1,000 Hz | 106.29 RPM | 97.29 RPM |

At duty 175, 248 and 250 Hz differ by only **0.13%**; at duty 150, they differ by **0.225%**. The tested 240–256 Hz means span **0.36%** and **0.68%**, respectively. The lowest sampled frequency, 200 Hz, is faster than 248 Hz by **0.89%** and **1.71%**. An endpoint speed maximum does not locate an optimum. The investigation retained 248 Hz as its working reference pending measurements under load and of current, temperature and electrical waveforms.

## What the evidence resolved

- The 5 kHz deficit persists late in each run, so startup delay alone cannot explain it.
- It persists with the APB clock and nine-bit resolution fixed across frequencies, so automatic clock-source switching is not required for the observed response.
- Fine sweeps reject a claim of a distinct measured speed peak at exactly 248 Hz. They do not constitute a formal equivalence test of neighboring frequencies.
- At duty 150, 240 and 248 Hz straddle the sleep boundary calculated from the SS6625E's **typical** 1.7 ms delay. Speed changes gradually, and 240 and 200 Hz are faster. The predicted large speed collapse below that nominal boundary was not observed. RPM alone cannot establish whether the driver enters sleep.
- A single constant lost turn-on time per PWM cycle cannot, by itself, explain recovery from 5 to 20 kHz. Current decay, pulse delivery, driver behavior and supply interaction remain possible contributors.

The exact number came from the upstream declaration `static constexpr uint8_t kPwmFrequency = 75000;`: unsigned eight-bit conversion yields `75000 % 256 = 248`. This establishes its software origin, not physical optimality. Earlier bare-LEDC trials reproduced useful 248 Hz operation while retaining eight-bit PWM and both-LOW stopping, on both replacement and restored original boards. Those earlier all-wheel cohorts were separate from the controlled FL/M2 sweeps here. [Pinned upstream declaration](https://github.com/emakefun-arduino-library/em_esp32_encoder_motor/blob/29ef1c31f3c0b0bc51cc279b74e97a64a4e4ccf5/src/motor.h#L48).

The [driver source notes](PWM_DRIVER_SOURCES.md) document the unresolved SS6625E/RZ7889 identity discrepancy in the board README and schematic. The SS6625E timing model remains conditional on the fitted silicon. The [GitHub evidence audit](SS6625E_GitHub_Audit.md) found configuration precedents at 1, 5 and 20 kHz, but no independent controlled experiment establishing 248 Hz as optimal. Longer current pulses and drive/coast current decay are plausible mechanisms; no measured current or motor-terminal waveform in this archive identifies the cause.

## Reports and original data

| Dataset | Sequence | Planned trials | Samples retained | Report |
|---|---:|---:|---:|---|
| History/AUTO, duty 175 | 2 | 24 + one preliminary trial | 2,025 | [Results](history-sweep-2026-09-06/RESULTS.md) |
| Fixed APB, duty 175 | 4 | 24 | 1,944 | [Results](apb-sweep-2026-09-06/RESULTS.md) |
| Fixed APB fine sweep, duty 175 | 5 | 48 | 3,888 | [Results](fine-sweep-2026-09-06/RESULTS.md) |
| Fixed APB fine sweep, duty 150 | 6 | 48 | 3,888 | [Results](fine-duty150-2026-09-06/RESULTS.md) |
| Fixed APB broad sweep, duty 150 | 7 | 24 | 1,944 | [Results](apb-duty150-sweep-2026-09-06/RESULTS.md) |

Each folder contains `source-log.txt`, `trials.csv`, `samples.csv` and `analysis.json`. In total, **168 planned sweep trials plus one preliminary trial** completed, with **13,689 timestamped samples**. All reported invalid-transition totals are zero, and all requested frequencies agree with integer peripheral readback. Six APB trials contain a single inferred opposite-direction valid step; these are not invalid transitions and are far too small to explain the measured speed gaps.

Zero invalid transitions cannot exclude missed complete quadrature cycles. Frequency readback is peripheral data, not a scope trace. Supply voltage, current, temperature and mechanical load were not logged. The earlier setup was described as wheels raised on a regulated 9 V supply, but the serial files do not independently confirm the supply voltage. These tests do not establish all-wheel behavior, loaded traction, thermal endurance or integrated-controller performance.

## Reproduce the analysis

The dependency-free Python parsers independently check CSV structure, complete balanced conditions, timestamps, counters, requested/readback frequency agreement and all printed RPM calculations. Run these commands from this archive directory; outputs go to a separate review directory:

```powershell
python history-sweep-2026-09-06/analyze_log.py history-sweep-2026-09-06/source-log.txt recheck/history
python analyze_pwm_log.py apb-sweep-2026-09-06/source-log.txt recheck/apb --sequence 4
python analyze_pwm_log.py fine-sweep-2026-09-06/source-log.txt recheck/fine175 --sequence 5
python analyze_pwm_log.py fine-duty150-2026-09-06/source-log.txt recheck/fine150 --sequence 6
python analyze_pwm_log.py apb-duty150-sweep-2026-09-06/source-log.txt recheck/apb150 --sequence 7
```

The [standalone test sketch](Maker_PWM_Frequency_Test/README.md) retains the commands, wiring, timing and logging protocol used for the investigation. Its [historical validation record](Maker_PWM_Frequency_Test/VALIDATION.md) reports a successful ESP32 core 3.3.11 build and 26 host simulations. [Host-test source and instructions](pwm-frequency-tests/README.md) are retained without compiled output. Neither host simulations nor analysis scripts operate the robot.

For the September 8 archive check, all five parsers were rerun, all stored numeric analysis values matched, the copied raw logs and sketch matched their source SHA-256 hashes, and all 26 host simulations passed again. The final shared parser adds some fields absent from the earlier APB JSON; every previously stored field still matches. Original attachment paths in `analysis.json` were replaced with the portable `source-log.txt`; raw logs and numeric results were left unchanged. Report annotations identify historical next steps that were subsequently completed. Manufacturer PDFs, rendered pages, private task transcripts and compiled build trees are not included; original public source citations remain in the reports.

## Remaining experiment

The next mechanism test is synchronized driver-input PWM, differential motor-terminal voltage, motor current and supply voltage at 248 Hz, 5 kHz and 20 kHz, ideally with an independent optical speed check. Motor-terminal measurement must use an appropriate differential method because both H-bridge outputs switch. Follow with controlled loaded starts and temperature/current measurements before selecting an operating frequency for the complete robot.

These remain proposed measurements; none is claimed as completed by this archive. The recorded experimental setting does not establish which integrated firmware is currently flashed.
