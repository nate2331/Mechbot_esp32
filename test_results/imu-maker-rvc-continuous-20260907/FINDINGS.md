# Continuous RVC baseline / 2026-09-07

Uploaded Maker_IMU_RVC_Continuous_Test via Arduino IDE to Maker ESP32 COM8.
Successful flash hash verification observed. GPIO21 RX, UART TX absent.
Original 35-second diagnostic and robot firmware source preserved.

User confirmed existing LVC wiring and IMU 5V connected. Initial frame 16ms,
READY 56ms after ESP boot, clean counters through >150 seconds in IDE snapshots.

Recorded log: esp-reset-trials-20260907-135806.txt.
Ten explicit RTS reset pulses (100ms each, DTR false), IMU power untouched.
READY times: 54,54,57,55,60,56,53,53,54,54ms. All ten confirmed new firmware banner
and acquisition=1. No nonzero checksum, index-discontinuity, repeat, pause, or UART
event counters in recorded output. Final snapshot: boot18010ms,1804frames,
100/101frames per second, max observed interframe gap11ms, age0ms.
Serial open produced one additional controller restart/READY62ms before the
explicit trials; do not count it as a cold start or a commanded trial.

Conclusion: this powered IMU stream can be joined automatically after ESP-only
resets. Manual IMU power application after arming is unnecessary for these trials.
This does not establish reliable common-supply cold starts, reset-line recovery,
motor-noise tolerance, long-duration movement performance, or angle accuracy.
Those tests remain pending. No physical power switching or motor operation done.

Earlier empty logs 135644 and135712: COM8 access denied from other Arduino monitors;
no hardware reset occurred. Other monitors closed through UI before successful run.

## User startup sequence follow-up

Saved user-startups-01.txt from attachment c3b44c47-5d47-4df6-9bd6-e18b9894072e.
179 RUN lines contain five uptime segments: preceding monitor-open run (READY61ms,
75s observed) then four new startup sequences, READY367,367,367,368ms respectively.
Those four segments end at ESP uptime17,32,12,43 seconds with1672,3175,1171,4277frames.
First valid frame327-328ms, maximum observed frame gap11ms; all reported checksum,
index-discontinuity, repeat, pause and UART event counts zero. No STALE event.
Later stream rates100/101frames per1000ms; initial69frames reflects startup delay.
User describes these as a couple of startups in response to full-power-cycle
instructions. Electrical power removal is not directly instrumented by this log.
They support automatic startup without manual arming; full ten cold starts and
15-minute uninterrupted movement run are still not demonstrated by this excerpt.

## Long run confirmed by user

Saved user-long-run-01.txt from attachment faac5ef5-4e27-4651-80a6-ede1f7a69543.
2154 complete RUN lines, uptime118010 through2271010ms, monotonically increasing.
First partial line describes117s uptime. Final uptime37min51.010s; continuously
READY for2270642ms (37min50.642s), acquisition count1, first frame328ms,
first READY368ms, consistent with the final startup in user-startups-01.txt.
Final227501 checksum-valid frames,100/101per1000ms reporting window,
max interframe gap13ms, final frame age2ms. All reported checksum errors,
index discontinuities, repeats, pauses and UART event counters zero.
15-minute milestone present. Final orientation tuple changes10643, accel tuple
changes171562; user confirms values continue changing. Last samples include
yaw2.51,-1.66,-1.61 degrees. No stationary-reference drift/angle accuracy inferred.

The continuous bench-stream endurance test exceeds the proposed15-minute target.
This does not validate the unbuilt perfboard/reset stage, motor-powered operation,
angle accuracy, or robot firmware integration. Preserve this working baseline.
