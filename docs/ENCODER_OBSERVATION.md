# Encoder observations without the IMU

The user deferred adding the BNO085 reset wire. `IMU_RESET_PIN` remains `-1`;
no additional firmware upload or automatic wheel motion is part of this change.

The Operations Overview now includes **Encoder check**. It observes the existing
Maker telemetry and works when the IMU is offline. Choose **Start observation**,
observe a supervised check, then **Finish observation** and **Download result**.
For manual wheel turns, switch motor power off and support the robot securely.
These three controls never command movement, change settings or clear hardware
counters. The main STOP button retains its existing behavior.

The result contains signed count changes, net revolutions using the existing
measured CPR, A/B edge deltas and new invalid transitions for each wheel. It
includes the start/end snapshots and a live/simulated label. Missing calibration
leaves revolutions unavailable. An unmoved wheel does not establish an encoder
test pass. Low or zero invalid counts also cannot establish absence of missed
whole transitions or good electrical signal quality.

Reconnections, profile/epoch changes, diagnostic counter decreases, stale data,
replay activation, changed calibration, unsafe numeric precision and observation
gaps invalidate an active window. A new observation is required after a fault;
later larger counters cannot silently clear it. Windows are limited to one hour.
Device-clock rollover is supported; diagnostic counter rollover requires a new
window. Downloaded failed observations retain their failure reason.

Counts and DIAG records arrive separately, so these are approximate observation
windows, not synchronous motor measurements. Finish after fresh stopped samples.
Use Recordings when the complete serial timeline is needed. Finished results
remain downloadable if the connection is subsequently lost; browser refresh
clears the local observation. Downloads are saved by the browser, not on the Pi.

The telemetry parser now recognizes `I <ms> OFFLINE` and immediately invalidates
the previous IMU heading. The dashboard explicitly displays **Sensor offline**.
This corrects the status report; it does not repair the sensor.

Validation: 37 Node tests passed, including 11 new comparison/workflow cases.
The complete PC Python suite ran 230 tests (229 passed, one Windows privilege
fixture skipped), including replacement of a previously valid IMU reading by an
OFFLINE message. The local model drafted the pure comparison helper using 1,280
generated tokens; review corrected signed-count handling, calibration checks,
numeric limits and missing-data errors before acceptance.
