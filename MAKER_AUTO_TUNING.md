# Maker automatic PWM bench tuning

This supervised Pi script measures each wheel's reliable starting PWM and start
delay, then uses encoder feedback to search for steady-speed PWM trims. It uses
the existing Maker firmware and does not install continuous wheel-speed PID.

The September 3 ten-turn measurements were FL 24,688, FR 24,679, RL 24,735 and
RR 24,698 counts. The script divides each by ten and uses each wheel's measured
counts per revolution. The encoder scales differ by less than 0.23%.

## What it measures

1. One wheel at a time, in forward and reverse: search upward in bounded steps,
   then refine to a one-count PWM interval. Every passing level requires three
   starts from rest and continued movement. Confirm the final threshold with
   three additional starts. A single encoder twitch is not a pass.
2. Record a bracket for time from command to detected movement, and steady speed
   separately. Firmware reports counts every 200 ms, so the script cannot prove
   that one wheel starts a few milliseconds later. Timing includes the firmware
   output ramp and serial latency. It is not a motor-only response-time test.
3. Automatically adjust PWM, stop, and repeat measured tests to match wheel
   speeds within 5%. Keep every output at or below the supervised 177/255 ceiling.
   Match forward and reverse separately, then check a shared candidate with three
   repeats at 60%, 80% and 100% command in both directions.
4. Save a JSON report. Recommend a shared trim only when all six verification
   conditions pass. Direction-dependent drag or low-speed startup failure can
   produce `measured_no_shared_trim`, which is a useful result rather than a
   reason to keep increasing power.

The threshold is the lowest tested PWM that reliably starts AND sustains motion
within a 1.2-second pulse under the current bench conditions. This is different
from the lowest PWM that can keep an already-spinning motor moving; the script
does not measure that holding threshold. It also does not install startup boost
or minimum-output compensation. Those would require a separate firmware change.

## Offline checks

From the repository root (the tool was recovered from the September 4 voice-chat
workspace and now shares encoder calibration with `mechbot_profiles.py`):

```powershell
python -m unittest discover -s tests -p 'test_maker_auto_tune.py' -v
python maker_auto_tune.py --simulate --output simulation.json
python maker_auto_tune.py --simulate --asymmetric-demo --output asymmetric.json
```

Simulation values are synthetic; they are not measurements of the robot. The
asymmetric example is expected to return exit code 2, showing that a single trim
cannot correct a wheel with different forward/reverse behavior across speeds.

## Supervised hardware run

Copy `maker_auto_tune.py` and `mechbot_profiles.py` together to the Pi. The script needs Python 3.10+ and `pyserial`, which
the existing bridge already uses. Stop the bridge/gamepad serial sender and close
other serial monitors. Restore the normal wiring after the earlier swap test:
M2/E2=FL, M3/E3=FR, M1/E1=RL, M0/E0=RR. Secure all four wheels clear of the floor;
keep the motor-power switch accessible. Field mode must already be off.

The following command **moves motors**. Run it only while attending the robot:

```sh
python3 maker_auto_tune.py --run --wheels-up --bridge-stopped --normal-wiring \
  --port /dev/ttyUSB0 --output maker-bench-results.json
```

`--max-pwm` can lower the ceiling, but cannot raise it above 177. The run has a
20-minute upper time limit and normally finishes sooner. Ctrl+C stops it. Each
pulse lasts at most three seconds; startup probes last 1.2 seconds. Stops between
probes must have unchanged encoder counts for at least 600 ms and at least a
one-second rest. The firmware's independent 300 ms command watchdog stays active.

It checks the Maker mapping, settings readback, actual applied PWM after ramp-up, encoder direction and freshness,
uncommanded wheel motion, IMU health, encoder invalid transitions, and zero output
at rest. A missing encoder cannot be distinguished from a stalled motor just by
its absence of counts, so the search stops at the configured ceiling instead of
assuming that more duty is the answer. These are software checks, not current
limiting; the existing firmware has no encoder-based hardware stall protection.

The script requires exclusive serial ownership. The `--bridge-stopped` flag is
your confirmation that other senders are stopped; it does not stop services for
you. For the installed bridge, an attended wrapper may instead enable
`POST /api/maintenance` with `{"enabled": true}`, verify maintenance is active
and serial/gamepad connections are closed, then run the tuner exclusively. In
this case the service stays alive but its command senders are stopped. Release
maintenance in a `finally` block only after the tuner has stopped, restored its
settings and closed serial; verify the reconnected board, fresh zero-output DIAG
frames and original settings. Do not manually release maintenance during a run.
A disconnected USB cable or forcibly killed process may prevent restoration;
the command watchdog remains the stop mechanism in that case.

## Reading and using a report

- `startup.forward.FL.minimum_reliable_start_pwm` (and the other seven entries):
  measured start duty on the 0–255 scale, with individual trial results.
- `start_delay_bounds_ms`: the encoder-detected time interval for a start.
- `directional_matching`: PWM changes, measured RPM, and repeatability for each
  direction. The ordering of all four-element lists is FL, FR, RL, RR.
- `shared_verification`: results at three speeds in each direction, including
  simultaneous-start delay brackets for all wheels.
- `recommended_shared_pwm`: populated only if all shared checks passed.
- `original_live_settings_restored`: whether the original PWM and heading setting
  were read back successfully after a hardware run.

The original PWM and heading setting are restored after success, failure or
interruption when serial communication remains available. Nothing is saved to
controller flash; the JSON report is saved automatically, including partial
results on failure. Existing report files are never overwritten.

Two [September 4 real bench runs](BENCH_RESULTS_2026-09-04.md) both measured
forward starting PWM 119/119/155/131, then aborted during the first all-wheel
forward speed-matching sequence. RL gained an invalid transition in the first
run; FR gained one in the full retry. A short diagnostic also reproduced an RL
increase in an all-wheel test after three clean isolated RL runs. Reverse
measurements and shared-trim verification remain incomplete. Every run restored
original settings; the partial results are not a recommended trim.

Bench trims and start thresholds depend on battery voltage, motor temperature,
shaft position and mechanical load. Review the report, apply a verified shared
trim through the existing controls, and check forward/reverse/strafe on the floor
before saving it to the controller. With direction-dependent motor drag, one PWM
ceiling per wheel may not fit; separate direction/feed-forward settings or future
closed-loop speed control would be a further firmware change.

Exit codes: 0 = verified bench trim; 2 = measurements completed without a verified
shared trim; 1 = aborted or restoration not confirmed. No hardware testing or
deployment is implied by passing the offline tests.
