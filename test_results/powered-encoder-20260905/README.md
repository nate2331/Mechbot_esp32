# Raised-wheel powered encoder check, September 5

User authorized the test and confirmed the wheels were raised. Four bounded
1.2-second pulses used the previously tested 177 PWM ceiling, with heading and
field control disabled. The sequence was RL forward, RL reverse, all forward,
all reverse. Each pulse was separated by a verified zero-output/stationary
interval. No further motor tests were run after this sequence.

All four pulses completed with expected encoder polarity, sustained movement
on the enabled wheels, applied-PWM verification and zero new invalid
transitions. The maximum reported encoder sample gap was 200 ms.

| Pulse | FL RPM | FR RPM | RL RPM | RR RPM |
| --- | ---: | ---: | ---: | ---: |
| RL forward | 0.0 | 0.0 | 71.1 | 0.0 |
| RL reverse | 0.0 | 0.0 | 70.5 | 0.0 |
| All forward | 18.9 | 15.5 | 70.2 | 85.4 |
| All reverse | 19.7 | 20.6 | 66.1 | 86.1 |

RPM values are magnitudes calculated from the final three powered encoder
samples. These short pulse-tail measurements are not a complete steady-speed
characterization. Front readings of about 15–21 RPM versus 66–86 RPM at the
rear during combined motion need further investigation. This run does not
distinguish actual motor-speed differences from counting loss under load.

The earlier invalid-transition fault did not recur in these four short pulses.
This does not establish sustained reliability, repair every encoder fault,
or validate floor motion, matched speed, or navigation. The IMU still supplied
no valid reports and was deliberately excluded from this wheel-only trial.

The runner reused the existing tested pulse, stop, rest and settings-restoration
implementation. Its narrow wheel-only adapter retained encoder/control-state
freshness checks, direction/mapping checks, firmware-watchdog handling, output
verification and error aborts. Raw IMU lines remain in the trace; only their
IMU-dependent calibration validation was excluded. Two adapter unit tests
passed before execution. The base automatic tuner was not modified.

All nine original live settings were restored and independently matched the
postflight query. No CFG SAVE or firmware upload occurred. The serial bridge
reconnected with rearm required, no active deadman and all applied outputs zero.

Pi evidence directory: `/home/nate/maker-powered-encoder-tp9_r18m`.
The upload provenance and runner SHA-256 are included in `trial.json`.
Battery voltage and load details beyond the user-confirmed raised-wheel setup
were not recorded.
