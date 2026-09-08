# PC diagnostic upload and hardware observations

## V3 packet diagnostic

Flashed through Arduino IDE 2.3.10 using the computer-use skill on 2026-09-06,
approximately 17:16 America/Chicago. Board: ESP32 Dev Module, COM8, DIO,
80 MHz flash, 4 MB/default partitions, upload921600, whole-chip erase disabled.
IDE wrote361984 bytes at0x10000, verified the flash hash, then reset via RTS.

User confirmed VDC-to-GND measured a steady3.3 V with RST attached and that
DI and CS were reconnected. After this diagnostic disabled automatic resets,
the user reported: **LED is steady now**. This strongly supports a relationship
between the previous LED pulses and the firmware's recovery loop; it does not
establish the electrical mechanism behind brightness changes.

Hardware still does not provide sensor readings. At approximately109 seconds,
`IMU TRACE` reported9 packets and0 overflow. See `startup-trace.txt` for exact
accessibility-extracted content. The trace contains:

- Startup advertisement, successful unsolicited initialization response, and
  reset-complete notification.
- Product-ID request and responses.
- Three correctly formed21-byte Set Feature commands requesting game rotation,
  calibrated gyro, and linear acceleration every20000 microseconds.
- No later RX packet, feature response, or sensor report.

At approximately409 seconds the monitor still showed `WAIT Q0 G0 A0` and
`H ... IMU 1 0 1 1`, confirming no advancing quaternion timestamp and no
additional host initialization. No motor motion command was sent.

These results locate the observed stop after report-enable commands. TX trace
records prove the host submitted those bytes, not that the IMU accepted them.
The next diagnostic adds bounded feature-configuration and product-ID probes
without changing SPI clocking or wiring.

## V3B control probe

Flashed through the same IDE/board/settings at approximately17:27 local.
IDE wrote362688 bytes at0x10000 and reported hash verified, then reset via RTS.
At approximately74 seconds after boot, `IMU PROBE` sent FE08 and F900.
Both returned valid responses. The FC08 feature configuration contained a zero
report interval: quaternion reporting was disabled. Product IDs still worked.
Transport counters showed8 RX,6 TX,0 bad headers,0 wake timeouts,0 decoded
sensor events and no pending packets. See `v3b-probe-trace.txt`.

This rules out a complete communication loss after subscription attempts in
this run. It establishes that the requested quaternion interval was not applied.
It does not yet establish why. The next A/B changes only TX chunking: send the
known outgoing packet in one transfer call, then clock any longer RX tail under
the same CS assertion. Source and CLI binary for V3B were preserved locally.

## V3C single-burst TX result

The subsequent IDE upload wrote 364544 bytes at 0x10000, verified the hash,
and reset through RTS. Computer Use was then stopped by the user. All later
diagnostic output was supplied manually by the user.

The user's next valid `IMU PROBE` trace still shows no decoded sensor events.
At 11716 ms, FE08 was sent; the response at 11717 ms was FC08 with a zero
report interval. F900 at 12217 ms returned normal product records at 12218 and
12219 ms. Counters were RX8, TX6, BAD0, WAKE_TIMEOUT0, EVENTS0, RESET1,
REINIT1, REPORT_RETRY0. The three startup FD requests still carried the correct
20000-us interval. Thus the single-burst TX change has not resolved the fault.

The next isolated sketch, `Maker_IMU_Error_Check`, services the device between
feature requests and automatically obtains a bounded raw highest-priority
error-log response. It is being provided for manual flashing. No additional
flash or wiring change has been performed on the user's behalf.
