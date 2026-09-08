# V2 PC upload and hardware check — September 6, 2026

The user authorized flashing through the computer-use skill. The agent opened
the packaged V2 sketch in Arduino IDE 2.3.10 and uploaded it to COM8, selecting
ESP32 Dev Module, DIO, 80 MHz, 4 MB, default partitions and whole-chip erase
disabled. Upload speed was 921600. No motion command or saved-setting change
was sent.

Source: `firmware/Maker_Mechbot_SPI_V2_2026-09-06/Maker_Mechbot/`.
Sketch SHA-256: `2114d457b5f09d47ec10e19cc894666834b0d8e73e8d59dfc5e805764c301048`.

Arduino's upload output showed 367040 application bytes written at 0x10000,
followed by `Hash of data verified.` and `Hard resetting via RTS pin...`.
The IDE rebuilt the sketch; this is not a claim that its binary is byte-identical
to the separately packaged command-line build.

Serial Monitor was opened at 115200 baud with New Line. The response to `?`
included `FIRMWARE MAKER_SPI_V2_STALE_RECOVERY` at 16:41:58 local time. `X`
returned `OK STOP`. `DIAG` at 16:44:13 showed all four PWM outputs and all eight
motor pin duties at zero, and all four encoder invalid counters at zero.

## Sensor result: failed

The live monitor repeatedly showed `WAIT Q0 G0 A0`, with last quaternion time
zero. Automatic report requests and initialization attempts continued. Two
diagnostic snapshots below were transcribed from the computer-use screenshots;
they are not a continuous raw serial capture.

At 16:42:41.866–.911, around 103 seconds uptime:

```text
IMU DIAG SPI AVAILABLE 1 INT 1 Q 0 AGE -1 G 0 AGE -1 A 0 AGE -1 RESET 20 REINIT 20 REPORT_RETRY 58
IMU DIAG TRANSPORT RX 100 TX 314 NULL 392 BAD 0 WAKE_TIMEOUT 0 RX_FULL 0 RX_SMALL 0 EVENTS 0 DECODE_ERR 0 EVENT_DROP 0 RX_PENDING 0 EVENT_PENDING 0
```

At 16:44:32.165, around 213 seconds uptime:

```text
IMU DIAG SPI AVAILABLE 1 INT 1 Q 0 AGE -1 G 0 AGE -1 A 0 AGE -1 RESET 41 REINIT 41 REPORT_RETRY 121
IMU DIAG TRANSPORT RX 205 TX 650 NULL 812 BAD 0 WAKE_TIMEOUT 0 RX_FULL 0 RX_SMALL 0 EVENTS 0 DECODE_ERR 0 EVENT_DROP 0 RX_PENDING 0 EVENT_PENDING 0
```

Upload and running identity are verified. Sustained IMU operation failed: no
valid orientation, gyro or acceleration reports were observed. Some SPI packets
are received, but successful sensor-event decoding remains at zero. No bad
length headers, wake timeouts, decode errors or queue drops were counted. This
does not identify whether report subscription, transport details or hardware
cause the failure. Reset counts tracking initialization attempts are consistent
with the firmware's automatic recovery and do not alone prove spontaneous
sensor resets. Serial Monitor was left open; motor outputs were stopped.

The package manifest describes the pre-upload build checkpoint and remains
unchanged. This record supersedes its hardware-status fields.
