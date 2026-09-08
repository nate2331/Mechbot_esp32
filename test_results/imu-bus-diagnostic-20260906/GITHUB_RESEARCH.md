# GitHub research for the current BNO085 fault

Observed fault: RVC startup banner is readable on GPIO21; the byte counter stops
at78, with no binary packets. SPI previously accepted a20000-us configuration
but delivered only two decoded sensor events, then stopped.

Sources checked on2026-09-06:

1. https://github.com/STRDC/strdc-sdk/blob/main/modules/STRDC_BNO08X/examples/Arduino/BNO08x_UART_RVC/BNO08x_UART_RVC.ino
   Manufacturer example: UART115200, AA AA header,17followingbytes, summed
   checksum, signed orientation/acceleration decode. No report-enable exchange.
   Its bundled HAL targets Teensy; the protocol is still applicable to ESP32.
2. https://github.com/adafruit/Adafruit_BNO08x_RVC/blob/master/examples/uart_rvc/uart_rvc.ino
   Independent Arduino example: hardware Serial1at115200, read-only RVC flow.
3. https://github.com/rdagger/micropython-bno08x-rvc
   MicroPython port of Adafruit's RVC implementation. Supports the same passive
   stream rather than a separate startup-enable command.
4. https://github.com/adafruit/Adafruit_CircuitPython_BNO08x_RVC/issues/1
   Reported problem is stale buffered readings caused by reading more slowly
   than the sensor emits data. That assumes incoming packets and does not match
   the current raw receive count remaining78 indefinitely.
5. https://github.com/adafruit/Adafruit_BNO08x_RVC/issues/3
   Issue listing identifies Software Serial problems. Current tests use hardware
   UART1/2 with2048-byte buffers; no SoftwareSerial implementation is involved.
6. https://github.com/myles-parfeniuk/esp32_BNO08x
   ESP-IDF SPI implementation using CEVA SH-2. Relevant alternative if SPI is
   selected again, but not a ready Arduino UART-RVC fix. Requires different mode
   strapping from the user's current P1=GND.
7. https://github.com/sparkfun/SparkFun_BNO080_Arduino_Library/issues/72
   I2C hanging discussion was found by GitHub issue search. Different transport
   and board; not evidence for the current RVC banner-only symptom.

GitHub issue searches for BNO085 RVC banner and the STRDC repository did not
find an exact matching fault report or a documented fix for it. Search results
are not exhaustive. Part10004148 is also present in other BNO085 example logs;
its presence in the startup banner is not itself an error.

CEVA authoritative behavior:
https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf
Section5.1 explicitly describes the Hillcrest startup banner in UART-RVC mode
and says packets follow. Section1.2.5 specifies automatic100Hz output.

Next controlled experiment: full raw banner capture; UART1/driven10ms reset;
UART2/100ms reset released via the board pullup; I2C on the same SDA21/SCL22
with P0 selected LOW in firmware and existing P1 held GND. Query feature state,
product ID and internal errors without using blocking third-party IMU startup.

I2C transport follow-up: CEVA SHTP section2.3.1 explicitly says that a
header-only read is continued by another read with bit15 set and sequence
incremented. V1 omitted control parsing on that bit. V2 incorrectly required
identical sequence numbers; it was rejected as an invalid diagnostic pass.
V3 validates same channel, sequence+1, complete length, continuation bit.
Source: https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf
Adafruit_BNO08x.cpp i2chal_read follows header peek/full payload reads too.

Completed automatic tests: see FINDINGS.md and hardware-v4-summary.txt.
I2C100kHz reliably delivers acceleration, magnetometer, and geomagnetic
quaternion. Gyro50Hz/200Hz and uncalibrated gyro50Hz stop after2/5/2 samples.
No exact public GitHub fix or numeric chip-error mapping found. Next check is
physical regulator-output voltage, then known-good hardware/vendor diagnosis.
