# Passive Pi IMU observation — September 8, 2026

User confirmed the buffered 5 V IMU setup and connection through the Pi.
Read-only HTTP and SSH inspection reached `mechpi` at `192.168.50.166`.
The active `mecanum-gamepad.service` runs `/home/nate/pi_mecanum_gamepad.py`;
its serial device is `/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0`.

`status-samples.json` contains 16 HTTP status snapshots from
21:18:36.870 to 21:19:07.352 UTC. Recent-line buffers overlap: they are not
independent samples or a complete raw UART recording.

- Serial connected throughout; gamepad disconnected and deadman false.
- Retained RUN records cover ESP uptime 84.010–120.010 seconds, with
  8,385–11,992 valid frames. Reported rate is about 100 frames/second.
- READY throughout retained RUN records, one acquisition, zero pauses,
  maximum reported interframe gap 11 ms.
- All retained checksum, discontinuity, repeat and UART error counters are zero.
- Retained yaw values span -0.01 to 0.00 degrees. Physical stillness and a
  reference angle were not independently established; this is not an accuracy
  or calibration acceptance result.

The output matches the continuous RVC diagnostic format. Exact flashed binary
identity is not proven: the bridge mistakes its `READY requires ...` help text
for a firmware token and correctly leaves the robot board profile unknown.
This diagnostic does not provide the integrated robot protocol, so dashboard
robot/encoder/IMU readiness cannot be inferred from the healthy raw RVC stream.

No new serial owner, reset, service restart, upload, settings change or motor
command was issued by this inspection. Next: establish known-angle/startup
evidence and prepare RVC integration while preserving this working baseline.
