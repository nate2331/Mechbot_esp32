# Continuous Maker IMU bench test

Separate diagnostic; original Maker_IMU_RVC_5V_Test remains unchanged.
Target: NULLLAB Maker ESP32 Pro, ESP32 Dev Module, core 3.3.11.
Uploaded through IDE on COM8 using its existing QIO/80MHz/4MB settings.
UART1: RX GPIO21, TX disconnected, 115200 8N1, 8192-byte receive buffer.

## Existing wiring

Keep the successful LVC circuit. LVC pin20 and pin1 to Maker 3.3V;
pin10, pin19 and pins3-9 to common ground; IMU SDA/TX to pin2;
pin18 to Maker GPIO21; pins11-17 disconnected. Keep bypass capacitor in place.
IMU VDC to Maker 5V, P0 to IMU VDC; common ground. P1/BT retain onboard defaults.
No host connections to IMU RST, SCL, INT or CS. No transistor required yet.
Use USB bench power with external motor DC/battery disconnected.

Unlike the earlier sketch, leave IMU 5V connected before startup. No r command.
The receiver starts automatically, ignores non-frame bytes and seeks valid frames.
Five checksum-valid frames with sequential indices and gaps <=100ms establish READY.
More than 500ms without a valid frame causes STALE; five sequential frames reacquire.
READY indicates a live stream, not sensor calibration or an error-free test.

## Procedure

1. Upload, open serial monitor at 115200, and check UART_CHECK ok=1 rx=21 tx=-1.
2. Verify READY and roughly 100 new frames per 1000ms reporting window.
3. Reset only Maker ESP32 using its reset/EN button, leaving IMU 5V connected.
   Repeat ten times, saving each boot and first READY result. Do not press BOOT.
4. Remove and reconnect the common USB supply ten times, leaving all signal and
   power jumpers in place. Allow five seconds off and ten seconds on each time.
   Reopen monitor if required. Save each startup separately; firmware counters
   reset with ESP power/reset. Missed console startup is not a measured failure.
5. Run at least 15 minutes with occasional gentle rotations, saving serial output.
   The milestone is informational; listening continues and does not declare PASS.

No simulated reset, motor motion, or automatic power cycling is built in.
Physical power cycles and movement require the operator.

## Counters

- boot_ms/first_frame_boot_ms/first_ready_boot_ms are ESP uptime, not the IMU's
  actual power-on timing. Frames buffered before reading can share timestamps.
- new/window_ms describes the recent rate. s prints a snapshot without resetting
  the regular reporting window; ? prints help. No counter-clear command.
- UART_EVENTS lists FIFO overflow, buffer full, frame, parity, break in that order.
  Each pair is total/post-first-READY. Callback event counts are not lost bytes.
- post_first_ready counters exclude startup noise but include every later dropout.
- pauses count >500ms gaps after the first frame, once per outage. max_gap_ms
  updates on arrival of the next valid frame; age_ms shows an ongoing outage.
- Index discontinuities include resets and repeats, not exact lost-frame counts.
- Angles are degrees. Acceleration is mg including gravity. No separate gyro rate.

Aim for READY on every start/reset, no post-start checksum/UART errors or index
discontinuities, and no pauses in the uninterrupted run. Investigate failures;
do not automatically classify them as IMU hardware damage.

## Verified 2026-09-07

Compile passed (275904 bytes program, 22300 bytes RAM for the QIO CLI build).
Arduino IDE upload completed with hash verification. Runtime stream observed.
Initial join: first frame at ESP uptime 16ms, READY at 56ms; over 150 seconds
observed with zero checksum, UART errors, discontinuities or pauses.
Ten explicit RTS-driven ESP-only resets passed, READY at 54,54,57,55,60,56,53,53,54,54ms.
IMU power was not switched; user confirmed 5V connected and existing wiring.
Every recorded counter line was clean, including 18 seconds after the final reset.
Opening the host serial port also caused an additional ESP restart (READY 62ms);
this is separate from the ten explicit trials. A monitor-open reset is not a full
IMU power cycle. Later startup and endurance observations are recorded below.
Reset transistor remains uninstalled.

Log: ../test_results/imu-maker-rvc-continuous-20260907/esp-reset-trials-20260907-135806.txt
The two earlier empty log files record failed host port opens before extra IDE
monitors were closed; no reset trials ran in those attempts.

Later user evidence: four new startup sequences reached READY at 367-368 ms with
clean counters. The user described power startups; electrical power removal was
not independently instrumented. A subsequent long-run log ends at 37 min 51 s
uptime with 227,501 valid frames, a maximum observed gap of 13 ms and zero
checksum/UART/index/repeat/pause counts. The 15-minute bench endurance target
is exceeded. See the [findings](../test_results/imu-maker-rvc-continuous-20260907/FINDINGS.md)
and [long-run log](../test_results/imu-maker-rvc-continuous-20260907/user-long-run-01.txt).
The full ten common-supply cold starts, deliberate interruption/reacquisition,
physical perfboard, transistor reset, motor-noise, angle accuracy and robot
integration tests remain pending. The separate
[motor movement sketch](../Maker_IMU_RVC_Movement_Test/README.md) has passed its
target build and host tests but has not been uploaded or tested on hardware.
