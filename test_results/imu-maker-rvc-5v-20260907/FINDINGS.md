# Maker / buffered UART-RVC / 5V IMU: bench results

The user's complete 35-second console log shows a continuous, checksum-valid UART-RVC sensor stream. This is a successful bench observation in the new setup, contrasting with the earlier S3 trial that received only the startup banner. It does not establish the sole cause of the earlier failures or directly verify all gyro axes.

A second user-run trial with movement shows large, coherent changes in orientation and an uninterrupted valid stream after a noisy startup. It provides substantially stronger evidence of functioning gyro-based heading response; it is not a precision angular calibration or direct raw-gyro test.

## Evidence and setup

- `user-trial-01-full-35s.txt`: complete user-pasted console output, including RESULT, FINAL, prefix bytes and END. The earlier `user-trial-01-partial-18s.txt` is the first part of this same trial, not a second test.
- Runtime banner: `MAKER_IMU_RVC_5V_TEST V1 CUSTOM`; UART1 RX21, TX detached, 115200 8N1, 8192-byte RX buffer.
- Planned/user-directed wiring: NULLLAB Maker ESP32 Pro, STRDC IMU VDC from Maker USB-derived 5V, IMU P0 tied to its VDC, SN74LVC245AN powered at 3.3V between IMU SDA/TX and Maker GPIO21. The user had only electrolytic capacitors and was instructed to use a provisional 0.1uF/50V bypass.
- Firmware source: `../../Maker_IMU_RVC_5V_Test/`; compilation passed before this test. The user supplied the running output; this task did not perform or independently capture this upload. No measured rail voltages, complete wiring inspection, raw full UART byte stream, or flashed-binary hash accompany the pasted result.

## Complete trial 1

| Observation | Result |
| --- | --- |
| Observation duration | 35000ms from first byte |
| Total received bytes | 66445 |
| Checksum-valid 19-byte frames | 3493 |
| Fresh indices | 3493 |
| Repeated indices / discontinuities | 0 / 0 |
| Checksum failures | 0 |
| UART FIFO overflow / buffer-full / framing / parity / break events | All 0 |
| First sensor frame | 148ms after first byte |
| Last sensor frame | 34997ms after first byte |
| Final frame age | 3ms |
| New frames per later one-second interval | 100 or 101 |
| Orientation tuple changes | 243 |
| Acceleration tuple changes | 2797 |
| Final yaw, pitch, roll | 0.10, -0.16, -1.41 degrees |
| Final acceleration | 28, 0, 974mg |

The final byte count is internally consistent: 3493*19 + 78 = 66445. The prefix contains the same 78-byte startup identity seen previously, followed immediately by binary RVC frames:

```text
%Hillcrest Labs 10004148
%SW Ver 3.2.13
%(c) 2016 Hillcrest Laboratories, Inc.
```

The first-to-last sensor-frame timestamps give approximately 100.2 frames/second. The count includes no indicated sequence gaps; these counters do not exclude every conceivable electrical fault or index-wrap-sized loss. All logged intervals are LIVE through the end.

## Trial 2: movement, with startup corruption

`user-trial-02-movement-35s.txt` is the full user-supplied second trial; the user explicitly described it as "with movement". It completes the configured 35 seconds from the first received byte.

| Observation | Result |
| --- | --- |
| Received bytes / valid frames | 100517 / 3104 |
| Fresh indices / repeated indices / discontinuities | 3104 / 0 / 0 |
| First valid frame / last valid frame | 4028ms / 34997ms from first byte |
| Valid-stream span and rate | About30.97 seconds at100.2Hz |
| Final frame age | 3ms; LIVE |
| Checksum failures | 1, already present at3seconds, unchanged afterward |
| UART break events | 1497 by4seconds, unchanged afterward |
| Other reported UART error types | 0 throughout |
| Logged yaw range | -105.02 to+0.22 degrees |
| Logged pitch range | -3.84 to+26.45 degrees |
| Logged roll range | -19.41 to+21.46 degrees |
| Orientation / acceleration tuple changes | 2778 / 2859 |

Yaw progresses from near zero through -18.63, -48.79, -77.77, -98.72 and -105.02degrees, then returns toward zero. Later roll reaches +21.46 and -19.41degrees, and pitch reaches +26.45degrees. Acceleration components change along with tilt. These are one-second logged snapshots, not exact extrema of every received sample; the physical turn angles were not independently measured. The final yaw offset does not by itself establish drift because exact physical return to the starting pose was not documented.

The initial four seconds are **not clean**: the128-byte prefix contains invalid data rather than the readable startup banner, and the first valid frame arrives only at4028ms. The byte excess is100517 -3104*19 =41541, matching the cumulative pre-frame byte count shown at4seconds. Both the checksum-error and break-event counters stop increasing before the valid stream starts. Every later logged interval delivers100 or101 new valid frames with no index discontinuities through completion.

The receiver began only37ms after `r` in this trial, versus4758ms in trial1; its sampled2ms idle-high gate did not prevent premature data capture here. A transient, floating signal while power was being connected, connection intermittency, or other startup signal conditions are plausible explanations; the log alone does not locate the fault. The existing35-second timer intentionally starts on any byte, so this run contains about31seconds of valid sensor streaming, not35seconds of clean data. The firmware and wiring have been preserved rather than changing the successful baseline during analysis.

## What changed in the evidence

The [earlier S3 RVC trial](../imu-overnight-20260907/FINDINGS.md) received those 78 startup bytes, then no subsequent bytes or valid sensor frames for 35 seconds. The new setup produces continuous packets. The IMU is therefore demonstrably capable of sensor streaming in this setup; a blanket conclusion that it is dead is unsupported.

The existing I2C gyro-report failures remain historical observations. These RVC tests bypass SH-2 feature requests and contain orientation and acceleration fields rather than explicit raw/calibrated gyro-rate reports. Trial1 showed only small angular changes. Trial2 now demonstrates large yaw/pitch/roll response while the user moved the board, substantially weakening a permanently failed gyro interpretation. Direct raw-gyro reporting, calibrated angular accuracy, and long-duration stability remain untested in this setup.

Supply voltage, controller, wiring/buffering, and startup/power-cycle procedure all differ from the earlier S3 test. The recovery cannot yet be attributed to 5V alone, or specifically to regulator headroom. Treat the current setup as a working baseline.

## Next discriminating observation

The movement check has now been performed. Keep this wiring and firmware as the baseline. A further clean cold-start repetition can check whether the trial2 startup corruption repeats; the timing and exact power/connection sequence should be documented. Report startup errors separately from errors during valid sensor streaming.

A controlled supply comparison could keep this same Maker, buffer, wiring and startup sequence while changing only IMU VDC and its attached P0 from5V to3.3V and back, with power disconnected while moving the supply connection. That comparison has not been performed. It would compare supply/interface-voltage conditions without confusing them with a different controller or host protocol.

## Primary reference

[CEVA BNO08X datasheet](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf), sections 1.2.5.2 and 5.1, specifies the 100Hz, 19-byte RVC stream, yaw/pitch/roll/acceleration fields, index and checksum, and startup text preceding sensor packets.
