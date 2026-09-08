# STRDC BNO085 on SPI

This is the wiring for the new Maker_Mechbot SPI firmware. It preserves the
orientation, calibrated gyro and linear-acceleration telemetry used by the Pi.
The earlier known upload used I2C; this change needs both the new firmware and
the connections below. The user has reported completing this wiring. Hardware
operation with this SPI build has not yet been verified.

Build verification passed on ESP32 core 3.3.11 / Adafruit BNO08x 1.2.7, including
four host test programs and a check against the actual SH-2 C sources. See the
[package record](manifest.json).

Disconnect Maker USB and external power before wiring or soldering. Keep motor
power off for the first upload and sensor checks.

| STRDC BNO085 label | Maker ESP32 Pro connection | Function |
| --- | --- | --- |
| VDC | Regulated 3.3 V | Power and logic reference |
| GND | GND | Common ground |
| SCL | GPIO22 | SPI clock / SCK |
| SDA | GPIO21 | SPI MISO, sensor to Maker |
| DI | GPIO32 signal | SPI MOSI, Maker to sensor |
| CS | GPIO33 signal | Chip select |
| INT | GPIO26 signal | Sensor data/ready indication |
| RST | GPIO25 signal | Hardware reset; reconnect this wire |
| P0 | GPIO16 | SPI mode selection at reset, then wake control |
| P1 | 3.3 V / VDC | Select SPI together with P0 high during reset |

Use the **signal** contacts on servo headers 25, 26, 32 and 33, not the adjacent
servo power contacts. GPIO16 is exposed on the Maker's SPI expansion header.
Leave BT at its existing normal-boot pull-up. Leave the IMU's 3V3 output and
ESDA/ESCL environmental-bus pads unconnected.

The original SCL/SDA/GND cable connections may stay on GPIO22/21/GND, with VDC
fed from 3.3 V. Both IMU JST sockets expose those same four nets. Those signal
wires will now carry SPI; disconnect any other I2C devices sharing GPIO21/22.

Do not connect the IMU to the Maker's default SPI signals as a group: GPIO5,
18, 19 and 23 already serve wheel encoders. Firmware explicitly remaps SPI to
the pins in this table. GPIO16 is the only pin used from that expansion header.
Do not bridge P0 to VDC: this build drives P0 from GPIO16. If a P0-high solder
jumper was previously closed, open it before connecting GPIO16.

The local MakerBnoSpi adapter holds P0 high during reset until the first INT,
then asserts it low when requesting a host write. It waits for INT before
selecting the device and raises P0 when the write finishes. SPI runs at 1 MHz,
mode 3. An idle sensor does not cause a blocking read or automatic reset.
The adapter also releases known-open SH-2 sessions on failed startup and before
retry, preventing the library's single session slot from blocking recovery.

After the correct firmware is uploaded, open serial at 115200 and send `?`.
Expect this transport/pin identification:

```text
IMU: SPI SCK22 MISO21 MOSI32 CS33 INT26 RST25 WAKE16; relative yaw, NOT compass north
```

Send `X`, `CFG GET` and `DIAG`. All four PWM outputs and all eight pin duties
must be zero. Verify repeated numeric `I` records with fresh orientation,
gyro and acceleration values; the report-request message alone is not a pass.
The READY identity and Pi serial protocol remain `ESP32_MAKER_MECANUM_IMU_V1`.

Sources: [STRDC pinout and SPI instructions](https://docs.strdc.com/products/imus/bno085-bob/),
[Maker pin assignments](https://github.com/nulllaborg/maker-esp32-pro), and
[CEVA SPI wake/reset sequence, pages 18–19](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf).
