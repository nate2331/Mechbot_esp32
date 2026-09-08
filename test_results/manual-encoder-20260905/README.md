# Forward and reverse encoder observations, September 5

The user supplied these two live Maker exports as the forward and back test.
The preceding procedure requested one manual wheel revolution in each direction.
Exact hand positioning and power isolation are not established by the exports.

| Wheel | Forward counts | Forward turns | Reverse counts | Reverse turns | New invalid transitions |
| --- | ---: | ---: | ---: | ---: | ---: |
| FL | +2487 | 1.007 | -2480 | -1.005 | 0 |
| FR | +2435 | 0.987 | -2449 | -0.992 | 0 |
| RL | +2465 | 0.997 | -2350 | -0.950 | 0 |
| RR | +2490 | 1.008 | -2456 | -0.994 | 0 |

Both windows are valid and have the same Maker epoch. Forward counts are positive
on every wheel; reverse counts are negative. All A/B channels registered edges,
and no new invalid transitions were recorded. Applied PWM is zero in the start
and end diagnostic snapshots. The exports do not prove zero PWM throughout.

RL reverse is 2,350 counts versus calibrated 2,473.5 per revolution: about 5.0%
short if exactly one revolution was intended. At initial review, a repeat with
a visible wheel mark was requested before changing calibration or concluding
counts were lost; the later saved observations are recorded below. The other seven
turn magnitudes are within 1.34% of one calibrated revolution.

These observations support direction polarity and error-free observed transitions
for this test. They do not close the earlier powered simultaneous-wheel fault,
establish absence of missed whole transitions, or validate loaded motion.

## Rear-left follow-up observations

The later saved RL-only observations provide the requested repeat evidence:

| Export | RL count change | Calibrated revolutions | New invalid transitions |
| --- | ---: | ---: | ---: |
| [Forward repeat](rear-left-observation-4.json) | +2,443 | +0.988 | 0 |
| [Reverse repeat](rear-left-reverse-observation-5.json) | −2,481 | −1.003 | 0 |

Both are valid live Maker windows. The other three wheels have zero count and
edge deltas, while both RL channels registered edges. Start/end diagnostic
snapshots show zero applied PWM. The reverse repeat is about 0.30% above the
recorded counts per revolution, so the original 5.0% short observation did not
recur in that repeat. Exact hand positioning and throughout-window power state
are still not established by these exports; calibration was not changed.

Later [powered checks](../powered-encoder-20260905/README.md) and
[pair/matching trials](../paired-motors-20260905/README.md) have separate results.
The latter reproduced invalid transitions, so these manual observations do not
establish sustained powered encoder reliability.

## Original exports

- forward: `C:\Users\nate2\Downloads\encoder-observation-live (2).json`; SHA-256 `2efb974dae73d674756838c259a5eccfb02ed594d3a4447f9502616b9b7fba52`.
- reverse: `C:\Users\nate2\Downloads\encoder-observation-live (3).json`; SHA-256 `8aac7b5de88360c4fdd48c1ed5a9c72c6391905cd9c51dc73530c8ffb58d887a`.
