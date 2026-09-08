# ESP32-S3 four-wire BNO085 test

Standalone test for the existing STRDC BNO085 on a separate ESP32-S3.
The user described an S3 with two USB-C connectors; the exact model is unconfirmed.
GPIO8/GPIO9 are the generic Arduino S3 I2C defaults and are exposed on the
Espressif DevKitC-1. Use actual GPIO labels, not header positions or D-number aliases.

## Wiring

With all power disconnected, remove the IMU's connections to the robot and
connect only these four wires to the new S3:

| STRDC IMU | ESP32-S3 |
|---|---|
| VDC | 3V3 |
| GND | GND |
| SDA | GPIO8 |
| SCL | GPIO9 |

Power the S3 from the computer's USB only. Use short wires. Leave the IMU's
3V3 output, RST, INT, P0, P1, DI, CS, BT, ESDA, and ESCL unconnected.
This relies on the unmodified STRDC board's onboard pull resistors: P0/P1/DI
LOW select I2C at 0x4A; RST/BT HIGH allow normal boot. Remove previously added
external mode straps. VDC must be 3.3 V to match ESP32 logic.
If your board does not expose GPIO8/GPIO9, choose exposed free GPIOs and change
SDA_PIN/SCL_PIN in the sketch to match before wiring.

## Arduino IDE

1. Open `ESP32S3_IMU_Test.ino` and select **ESP32S3 Dev Module**, or the exact
   board profile if known. The sketch needs no PSRAM; generic PSRAM Disabled is fine.
2. Install **Adafruit BNO08x** and its dependencies through Library Manager.
3. Select the new S3's port. USB setup depends on which connector is used:

| Connector | USB Mode | USB CDC On Boot | Upload Mode |
|---|---|---|---|
| USB-to-UART bridge, usually labeled UART | Hardware CDC and JTAG | Disabled | UART0 / Hardware CDC |
| Native ESP32 USB, usually labeled USB or OTG | Hardware CDC and JTAG | Enabled | UART0 / Hardware CDC |

Connector labels vary; do not identify the connector by left/right position.
Upload, then unplug USB and reconnect so both the S3 and IMU start from power-off.
Open Serial Monitor at **115200 baud**. Native USB may acquire a new port after
upload; reselect it if needed. If upload cannot enter download mode, hold BOOT,
press and release RESET, then release BOOT and retry. After a manual download-mode
upload, press RESET to run the sketch.

## Run the test

1. Observe **ACCEL_ONLY** for 30 seconds. ACCEL should stay LIVE and `samples`
   should keep increasing. At rest, acceleration magnitude should be about
   9.8 m/s^2; tilting changes its distribution across the three axes.
2. Send **g** in Serial Monitor. This adds the calibrated gyro while keeping
   the accelerometer active. Observe another 30 seconds and gently rotate the
   IMU. GYRO should stay LIVE; angular velocity in rad/s should change with motion
   and return near zero when stationary.
3. Save several consecutive summaries before and after `g`. If gyro readings
   stop after a few samples, include the later STALE lines even if the last
   printed XYZ values look plausible.

`samples` counts received events, `new` counts events since the previous
summary (roughly one second), and `age_ms` is time since the last received event.
OFF means gyro has not been requested; WAIT means no samples; STALE means no fresh
sample for over one second. `last_xyz` is the last received value, not a new
measurement on each summary line. The requested report rate is 50 Hz; observed
counts can differ because of sensor scheduling. An SH-2 callback counts every
decoded report, including multiple sensor reports bundled into a single packet.
`decode_errors` counts reports the decoder could not interpret.

There is no automatic reset, reinitialization, or report retry. A detected
post-initialization IMU reset stops the test and stays visible. The library issues
a software reset during initialization, but this is not a hardware power cycle.
For each clean comparison, unplug USB to power off BOTH boards. Pressing only
the S3 RESET button does not remove power from the IMU.

Address ACK and configuration success alone do not establish sensor health.
This comparison checks the earlier gyro-stall symptom using a separate host,
power source, and four-wire connection. Failure alone does not identify a specific
damaged component. This test does not measure heading or fused orientation.

## Sources

- [STRDC board pinout, supplies, and I2C defaults](https://docs.strdc.com/products/imus/bno085-bob/)
- [Espressif DevKitC-1 GPIO headers](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.0.html)
- [Espressif Arduino USB CDC settings](https://docs.espressif.com/projects/arduino-esp32/en/latest/tutorials/cdc_dfu_flash.html)

## Build validation

Compiled with Arduino CLI 1.5.1, ESP32 core 3.3.11, Adafruit BNO08x 1.2.7,
BusIO 1.17.4, and Adafruit Unified Sensor 1.1.15. Both generic S3 configurations
passed: native hardware USB CDC (332198 bytes flash, 26624 bytes globals) and
USB-to-UART serial (336850 bytes flash, 26440 bytes globals). These are build
checks, not evidence that the physical IMU works.
