# STRDC gyro example on ESP32-S3

This is STRDC's supplied `BNO08x_Gyro.ino`, adapted to the separate ESP32-S3 test board. It uses STRDC's BNO08X module and middleware, with a local ESP32 Arduino hardware abstraction layer (HAL). It does not use the Adafruit BNO08x library or the earlier custom test's sensor callback.

Source: https://github.com/STRDC/strdc-sdk

Pinned upstream commit: `2582838df20642f1d2568244daa56e1be1206bf0`.

The original example is retained in `data/upstream/BNO08x_Gyro.ino`. Copyright and MIT license notices remain in the vendor files. `SOURCE_MANIFEST.json` records upstream and local hashes; `EXAMPLE_CHANGES.diff` shows the example changes. See `HAL_PORT_NOTES.md` for the ESP32 platform changes.

## Wiring

Unplug USB before changing wires. Use the separate ESP32-S3 board, with no motors attached.

| ESP32-S3 | STRDC BNO085 breakout |
|---|---|
| 3V3 | VDC |
| GND | GND |
| GPIO8 | SDA |
| GPIO9 | SCL |
| GPIO4 | RST |
| GPIO5 | P0 |
| GPIO6 | INT |

Leave P1 and DI at their onboard LOW defaults for I2C address `0x4A`. Leave BT at its onboard HIGH default. No other connections are needed. GPIO47 is reserved for the example's LED writes and should remain unconnected.

The reset wire preserves STRDC's hardware-reset initialization and retry behavior. INT preserves the original example's interrupt-based reads. P0 gives the vendor driver control over its wake pin. STRDC documents P0 as optional when sleep/wake is unused, but this wiring preserves the driver's actual pin operations.

SDA and SCL are different signals. With this build, GPIO8 must reach IMU SDA and GPIO9 must reach IMU SCL. To exchange the two ESP32 pins, change the pin arguments in `src/vendor/hal_i2c_master.cpp` as well as the wiring.

Manufacturer wiring: https://docs.strdc.com/products/imus/bno085-bob/

## What changed in the example

- Includes point to the bundled `src/vendor` files, so no shared Arduino library installation is needed.
- Select the example's supplied I2C option instead of SPI. Keep its interrupt option enabled.
- Set INT=6, RST=4, P0/WAKE=5, and the unused LED output=47.
- Copy the three shared pin assignments from STRDC's `BNO08x_Rotation_Vector.ino`. The upstream gyro example defines these pins but omits assigning them to `bno`; without this correction the zero-initialized object would use GPIO0.

The sensor report remains calibrated gyro at the supplied 400 Hz request. I2C remains 400 kHz, serial remains 115200 baud, and output remains the supplied 10 ms print interval. Initialization retries, calibration commands, report handling, and all gyro math remain as supplied.

## Build and test

Open `ESP32S3_STRDC_Gyro_Test.ino` in Arduino IDE. Select ESP32S3 Dev Module and COM7 for the current USB-to-UART connection. Use USB CDC On Boot: Disabled, USB Mode: Hardware CDC and JTAG, Upload Mode: UART0 / Hardware CDC, PSRAM: Disabled, and 4 MB flash, matching the earlier successful S3 upload. The verified local ESP32 Arduino core is 3.3.11.

After upload, use Serial Monitor at 115200 baud. The supplied banner is `Stardust Orbital BNO08x Gyro Example`. Successful initialization prints `BNO Initialized Successfully`; normal setup proceeds through feature set and calibration to `Begin loop()`.

Gently rotate the IMU about each axis and look for changing x/y/z values. These are gyro angular rates in radians per second, not tilt angles. The example prints its cached values every 10 ms even if no new sensor report arrives: repeated output alone does not demonstrate a working gyro. A stationary board can also legitimately produce near-zero rates.

This is an ESP32 port of the actual vendor example, not a test on STRDC's supported Teensy hardware. A failure here would still need to be separated from possible port or transport behavior before declaring the sensor damaged.
