# STRDC BNO085 investigation — 2026-09-06

## Subsequent supply measurement

User reported 3.0 V at the IMU breakout's 3V3 output-to-GND after being asked
to measure with the existing wiring. No change to 5-V supply was reported.
The exact diagnostic phase during this measurement was not specified.
CEVA BNO08X datasheet section6.2 specifies sensor VDD2.4–3.6V, so3.0V is
within its operating range and is not evidence of steady undervoltage.
A multimeter reading does not exclude brief supply dips during gyro startup.
This resolves the previously pending regulator-output measurement below;
the next discriminating checks are a known-good unit comparison, supply
waveform capture during startup, or STRDC interpretation of the chip error.

Tests run on the actual Maker ESP32 Pro through Arduino IDE / COM8, with all
motor direction pins held LOW. No motors commanded. Current wiring has P1 GND,
P0 GPIO16, RST GPIO25, SDA21, SCL22, DI32, CS33, INT26, VDC3.3V/common ground.

## Final V4 comparison

| Report | Requested rate | Decoded samples | First-to-last span | Transport errors |
|---|---:|---:|---:|---:|
| Calibrated gyro | 50 Hz | 2 | 125 ms | 0 |
| Calibrated gyro | 200 Hz | 5 | 16 ms | 0 |
| Uncalibrated gyro | 50 Hz | 2 | 109 ms | 0 |
| Geomagnetic rotation vector (no gyro) | 10 Hz | 338 | 33686 ms | 0 |
| Accelerometer | 10 Hz requested, 15.625 Hz returned | 542 | 33802 ms | 0 |

Each gyro phase continued polling for more than30 seconds before requesting
errors. No later gyro samples arrived. Both50Hz cases also remained stalled
after an executable ON command; the200Hz case did not receive that command
because its five samples bypassed the diagnostic's fewer-than-five condition.
Calibrated and uncalibrated gyro retained the requested feature
intervals, so these failures are not merely rejected report rates.
All three returned the same chip-level error record described below. Threshold4
also repeated the record, so this behavior is not unique to threshold255.
The 16 stored records per failed phase hit the diagnostic storage cap; they are
not16 unique failures. Error responses did not reach their terminal marker.

The geomagnetic quaternion packets contain changing data and status3. That
status is sensor-reported; physical heading accuracy has not been tested.
Geomagnetic orientation uses accelerometer and magnetometer without the gyro
(CEVA BNO08X datasheet2.2.1). This is a working data path, not a validated
replacement for gyro-assisted robot navigation around motors.

Inference: repeatable failure of gyro-dependent paths across earlier SPI/RVC
and current I2C, alongside working accelerometer/magnetometer/geomagnetic
streams, points to the gyro subsystem, its internal firmware, or its supply.
It does not prove a particular component is damaged. Rewriting host parsers or
switching buses again is not supported as the next fix by these results.

Next physical check: measure voltage from the IMU breakout's pad labelled3V3
(regulator output) to its GND while powered. The previous VDC measurement
does not establish this output voltage. If that rail is healthy, a known-good
sensor comparison or STRDC interpretation of the saved internal error code
is the next diagnostic step. No message has been sent to STRDC.

Board left running `MAKER_IMU_BUS_DIAGNOSTIC_V4`: motor pins LOW, I2C stopped
after the tests, P0LOW, RST released, stored summaries every10 seconds.
Full V4 capture: `serial-capture-v4-complete.txt`; extracted final block:
`hardware-v4-summary.txt`; exact source: `flashed-v4.ino`; SHA256:
`SOURCE_V4_SHA256.txt`. V4 built and uploaded in Arduino IDE and its banner and
results were observed on COM8. Host absence/stream simulations passed with
`-std=c++17 -Wall -Wextra -Werror`.

## Confirmed V3 results

| Isolated report | Decoded samples | First-to-last span | Report interval returned | I2C errors |
|---|---:|---:|---:|---:|
| Accelerometer | 526 | 32806 ms | 64000 us | 0 |
| Calibrated gyro, requested 100000 us | 0 | — | 0 (disabled) | 0 |
| Magnetometer | 330 | 32886 ms | 100000 us | 0 |
| Raw gyro alone | 0 | — | 0 | 0 |
| Accelerometer repeated | 526 | 32807 ms | 64000 us | 0 |

Both accelerometer runs had plausible roughly 9.5 m/s² vertical acceleration,
small horizontal components, incrementing report sequence, and changing values.
Magnetometer values also changed. These are sustained streams, not counts of
repeated summary lines. No duplicate adjacent input transport sequence was seen.
This verifies communication and two sensor data paths; it does not validate
orientation accuracy or operation during robot motion.

The gyro phase returned internal error records with R0..R5 bytes
`04 00 04 01 15 00`: severity4, error sequence0, source4, error1, module21,
code0. CEVA SH-2 section6.4.1 identifies source4 as the chip-level executable.
The published manual does not decode error1/module21/code0; no exact public
GitHub mapping was found. Do not label that number a definitive broken-gyro code.
The threshold255 query repeatedly returned this record without completion;
V4 uses threshold4 after the streaming observation instead.

The standalone raw-gyro phase is inconclusive: raw reporting can depend on an
already enabled sensor. V4 checks calibrated gyro at50Hz/200Hz, uncalibrated
gyro at50Hz, and geomagnetic quaternion at10Hz.

## Earlier interface tests

UART-RVC on hardware UART1 and UART2, with 10-ms and100-ms reset pulses,
both stopped at78 received bytes with no valid RVC frame. Earlier SPI diagnostics
accepted a20-ms quaternion configuration but stopped after two decoded events.
V1 I2C at10kHz failed transport checks;100kHz works for the streams above.

The Hillcrest ASCII startup banner is normal in UART-RVC (CEVA datasheet5.1).
The Adafruit/STRDC RVC examples do not require a hidden start command. A silent
second diagnostic RX pin is expected when IMU TX is connected only toGPIO21.

## Diagnostic corrections and evidence limits

V1 skipped control decoding on the continuation bit, making its FEATURE0
summary unreliable. V2 incorrectly required an unchanged sequence between the
header peek and full read, rejecting valid packets; discard its sensor findings.
V3 follows SHTP2.3.1: continuation bit plus sequence increment on the second read.
Sources and raw captures are preserved. Host tests include this continuation
behavior, simulated absence, finite phase timing, and all motor pins LOW. Host
simulation is not proof of physical sensor operation; the saved COM8 logs are.

The user measured stable VDC-to-GND voltage. The breakout regulator OUTPUT
labelled3V3-to-GND has not yet been measured. Communication success does not
rule out a sensor supply or internal hardware fault.

## Primary sources

- [STRDC UART-RVC example](https://github.com/STRDC/strdc-sdk/blob/main/modules/STRDC_BNO08X/examples/Arduino/BNO08x_UART_RVC/BNO08x_UART_RVC.ino)
- [Adafruit Arduino UART-RVC](https://github.com/adafruit/Adafruit_BNO08x_RVC/blob/master/examples/uart_rvc/uart_rvc.ino)
- [ESP32 SPI implementation](https://github.com/myles-parfeniuk/esp32_BNO08x)
- [CEVA BNO08X datasheet](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf)
- [CEVA SH-2 reference](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf)
- [CEVA SHTP reference](https://www.ceva-ip.com/wp-content/uploads/Sensor-Hub-Transport-Protocol.pdf)
- [STRDC schematic](https://docs.strdc.com/schematics/BNO085_BOB-R1_V1_Schematic.pdf)

No report or message was sent to STRDC, GitHub maintainers, or anyone else.

## New user capture after wiring-cleanup discussion

Saved as user-rerun-rst-low.txt. Completed 50Hz gyro phase still reports
RX291 INPUT_PKTS2 BAD0 IO_ERR0, followed by the familiar chip error record.
The later MCU startup banner in this capture has PINS RST0, unlike the RST1
seen in preceding phases. The diagnostic releases GPIO25 to INPUT and relies
on the breakout pullup; this is the ESP32 pin reading, not a direct independent
measurement of the IMU reset input. The IMU still returns product/configuration
responses, with transport sequences continuing, so a disconnected/misconnected
reset lead is a possibility. Confirm actual RST-to-GPIO25 connection before
using that last restart as a fresh IMU reset test. CS/DI removal was advised,
but the user has not explicitly confirmed which leads were actually removed.
