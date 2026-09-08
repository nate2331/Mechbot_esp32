# Pair comparison and attempted speed matching — September 5, 2026

The user instructed us to trust the calibrated encoder counts, correct the
speed mismatch, and test the front, rear, left and right pairs. Wheels were
confirmed raised. The supply is a 20 V drill battery through a regulator
set to 9 V, per the user; its current rating and loaded voltage are unknown.

All pair pulses used PWM 177 for enabled wheels and zero for other wheels,
with 1.2 seconds powered per pulse and verified stops between pulses.
Both directions of all four pairs were attempted once. Six passed all
checks; front reverse and rear forward were stopped by the strict
new-invalid-transition check. Their tail RPM is reconstructed from raw
powered encoder telemetry and explicitly marked below. Counts-per-turn
calibration was not changed or challenged.

| Pair/direction | FL RPM | FR RPM | RL RPM | RR RPM | Diagnostic result |
| --- | ---: | ---: | ---: | ---: | --- |
| Front forward | 88.6 | 89.2 | 0.0 | 0.0 | Passed |
| Front reverse | 88.3 | 90.5 | 0.0 | 0.0 | Transition-check stop; RPM reconstructed |
| Rear forward | 0.0 | 0.0 | 74.3 | 88.1 | Transition-check stop; RPM reconstructed |
| Rear reverse | 0.0 | 0.0 | 71.6 | 86.7 | Passed |
| Left forward | 87.4 | 0.0 | 71.9 | 0.0 | Passed |
| Left reverse | 87.9 | 0.0 | 72.0 | 0.0 | Passed |
| Right forward | 0.0 | 88.3 | 0.0 | 87.4 | Passed |
| Right reverse | 0.0 | 90.7 | 0.0 | 88.3 | Passed |

At the same PWM the front pair runs about 89 RPM, compared with only
15–21 RPM in the earlier all-wheel 1.2-second trial. Side-pair tests
also put the front wheels near 88–91 RPM. Rear-left remains about
72–74 RPM and rear-right about 87–88 RPM in pair tests. This supports
a real rear-left speed difference plus a separate all-wheel load effect.
Regulator current limiting, voltage sag, or shared power wiring are
hypotheses; voltage/current were not measured and no cause is proven.

Before the pair comparison, four bounded static-matching attempts
used 2.4-second pulses. All aborted on new invalid-transition counts
and restored settings. One clean [177,177,160,150] pulse measured
[27.95,21.09,58.58,71.54] RPM. A clean [88,177,80,75] pulse measured
FR 93.61 RPM with the other wheels stationary. These observations
make a fixed ratio derived from the earlier all-wheel speeds unreliable.
Do not install the search candidates as verified calibration.

No correction was applied or saved. All nine original settings were
restored after every run, four outputs were confirmed zero, and the
bridge resumed with rearm required. No firmware or encoder calibration
changed. IMU checks were excluded for these heading/field-disabled
wheel tests; telemetry, polarity, watchdog, output and error checks remained.

The search scripts are experimental evidence, not a production tuner.
The included search module is its final version, used only for
`maker-speed-match-08ov6zou`; earlier attempts used different baseline
settings. Each report and serial trace records what actually ran.

Next: measure regulator output and controller motor-supply voltage while
comparing two versus four running motors, and identify regulator current
rating. Establish repeatable all-wheel speed before installing static
trim or validating continuous encoder-feedback control.
