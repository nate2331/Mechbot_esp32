# SPI V1 stalled reports — September 6, 2026

The user manually flashed the supplied PC package and provided serial output.
No SPI upload or physical sensor measurement was performed by the agent.

Initial pasted log: `H 56869 IMU 1 1792 2 1`, unchanged through 61669 ms.
The health fields are uptime, initialized flag, last orientation arrival time,
sensor reset notifications and initialization attempts. Thus 1792 is a timestamp,
not an event count. `STALE` means all three report-valid flags were set but at
least one report is older than 500 ms.

The attached retry log is preserved as `user-v1-retry-log.txt`. Its second
initialization at about 66 s produces three reset notifications, then numeric
records at 68100 and 68300 ms. Those records repeat the same data; orientation
arrival remains 68007 ms. At 68500 ms the combined record becomes stale and
stays stale through 73500 ms. A short numeric interval is not sustained success.
The log alone does not identify the electrical condition or exact cause.

Code review established these software problems:

- Recovery checked sticky validity flags without checking sample age, so a
  previously working stream could stay stale forever without recovery.
- The SPI write path discarded simultaneously received bytes. STRDC's driver
  handles that incoming traffic explicitly.
- The inherited SPI reader could hardware-reset on a second interrupt wait,
  including after an empty header. Splitting SPI reads is permitted by SHTP;
  the hidden timeout/reset behavior is the concern.
- Adafruit's sensor callback writes every decoded report into one destination,
  so a packet containing several reports exposes only the final report through
  its getSensorEvent API. The destination is initially null before that API is
  first called, which also matters if initialization services early samples.
- The installed SHTP retries HAL writes returning zero without a deadline;
  a terminal wake timeout must return an error, not temporary backpressure.
- The ESP32 Serial default has no software transmit queue; long telemetry and
  help output can postpone sensor interrupt handling.

Primary references inspected:

- [STRDC SDK duplex transmit handling](https://github.com/STRDC/strdc-sdk/blob/2582838df20642f1d2568244daa56e1be1206bf0/modules/STRDC_BNO08X/src/BNO08X.cpp#L574)
- [STRDC SPI pinout and wake guidance](https://docs.strdc.com/products/imus/bno085-bob/#spi-1)
- [CEVA SHTP specification](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf)
- [CEVA BNO08X datasheet, SPI sections 1.2.4 and 6.5](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf)
- Installed Adafruit BNO08x 1.2.7, SH-2/SHTP sources, BusIO 1.17.4 and ESP32
  core 3.3.11. These local sources were reviewed directly.

V2 addresses these software paths. Compilation and simulation cannot establish
that the user's wiring and sensor now produce sustained correct readings.
