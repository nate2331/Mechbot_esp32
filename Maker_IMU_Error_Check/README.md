# Automatic IMU error check

This is a standalone diagnostic for the current STRDC BNO085 / Maker ESP32 Pro
SPI wiring. It does not implement robot motion. All eight motor direction pins
are held low. It resets the IMU once at startup and does not automatically retry.

## Flash from this PC

1. Open `Maker_IMU_Error_Check.ino` in Arduino IDE. Keep `MakerBnoSpi.h` in the
   same sketch folder.
2. Select **ESP32 Dev Module** and the Maker's USB port (previously COM8).
3. Upload, then open Serial Monitor at **115200 baud**.
4. Copy one complete block from `CHECK RESULT BEGIN` to `CHECK RESULT END`.
   No serial command is needed. The results repeat every ten seconds.

Dependencies on this PC: Espressif ESP32 core 3.3.11, Adafruit BNO08x 1.2.7,
Adafruit BusIO 1.17.4 and Adafruit Unified Sensor 1.1.15.

## Existing wiring

| IMU pin | Maker ESP32 connection |
| --- | --- |
| VDC | 3.3 V |
| GND | GND |
| SCL / SCK | GPIO22 |
| SDA / MISO | GPIO21 |
| DI / MOSI | GPIO32 signal contact |
| CS | GPIO33 signal contact |
| INT | GPIO26 signal contact |
| RST | GPIO25 signal contact |
| P0 / WAKE | GPIO16 |
| P1 | 3.3 V |

SPI is 1 MHz, mode 3. These are the existing connections; no rewiring is needed.

## What it checks

The previous V3C trace showed valid product-ID replies but a zero quaternion
report interval after report-enable writes. This sketch requests the same
quaternion, gyro and linear-acceleration reports. It services incoming replies
between requests (250 ms, 250 ms, then 500 ms), instead of the prior 10 ms gaps.
It then reads quaternion configuration and product IDs, and makes one raw
Report Errors request. It never sends another control command after those
queries. Result repetition does not reread or clear the device's error queue.

The error request uses severity threshold **0**, documented as highest priority.
It does **not** establish that every severity was queried. `ERROR LOG COMPLETE 1`
means a matching end-of-response marker was received; it does not prove the
capture is gap-free. Up to 32 error records are retained, with overflow reported.
Bundled replies are parsed beyond the 32-byte packet-trace prefix; unknown,
truncated and continuation records are skipped conservatively. A missing reply
or an empty log is not proof that the IMU is fault-free.

`REPORT WRITES ... 1` means the bytes were submitted to SPI, not acknowledged
by the sensor. `STARTUP OK` means initialization succeeded, not that sensor
reports are flowing. `SENSOR EVENTS` is the decoded-event count. If the fixed
startup trace fills before the configuration reply, its interval is reported as
not captured rather than assumed zero.

Query waiting periods are bounded. Initial product-ID startup still uses the
installed Adafruit/SH-2 library's synchronous initialization. If only the
`starting automatic check` line appears and no result block follows, report that
line; do not interpret it as a completed diagnostic.

References: [STRDC board documentation](https://docs.strdc.com/products/imus/bno085-bob/),
[STRDC implementation](https://github.com/STRDC/strdc-sdk/blob/main/modules/STRDC_BNO08X/src/BNO08X.cpp),
[SH-2 reference manual, section 6.4.1](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf).
