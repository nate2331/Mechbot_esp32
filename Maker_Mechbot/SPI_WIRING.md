# STRDC BNO085 on SPI

This is the wiring for the new Maker_Mechbot SPI firmware. It preserves the
orientation, calibrated gyro and linear-acceleration telemetry used by the Pi.
The user has reported completing this wiring and manually uploaded the first
SPI build. Its logs showed brief readings followed by STALE. Current V2 source
addresses the transport and recovery issues. V2 was uploaded on COM8 and its
identity verified, but its live check returned `WAIT Q0 G0 A0` with no decoded
sensor events. See the [PC upload result](../test_results/imu-spi-v2-20260906/PC_UPLOAD_RESULT.md).
The wiring below is shared by both SPI builds.

V2 compilation passed on ESP32 core 3.3.11 / Adafruit BNO08x 1.2.7, along with
four host test programs. See the
[V2 build record](../test_results/imu-spi-v2-20260906/build-manifest.json).

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
| RST | GPIO25 signal | Hardware reset |
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
mode 3. V2 keeps incoming data from command transfers, reads a packet under one
CS assertion, and queues each decoded report. Idle reads never reset hardware;
terminal wake timeouts return errors. Known-open SH-2 sessions are released on
failed startup and before retry. Missing or stale reports trigger bounded
recovery while stopped. Use `IMU DIAG` for report ages and transport counters.

After the correct firmware is uploaded, open serial at 115200 and send `?`.
Expect this transport/pin identification:

```text
IMU: SPI SCK22 MISO21 MOSI32 CS33 INT26 RST25 WAKE16; relative yaw, NOT compass north
```

V2 also prints `FIRMWARE MAKER_SPI_V2_STALE_RECOVERY`. See the
[V2 upload and diagnosis instructions](../test_results/imu-spi-v2-20260906/FLASH_FROM_PC.md).

Send `X`, `CFG GET` and `DIAG`. All four PWM outputs and all eight pin duties
must be zero. Verify repeated numeric `I` records with fresh orientation,
gyro and acceleration values; the report-request message alone is not a pass.
The READY identity and Pi serial protocol remain `ESP32_MAKER_MECANUM_IMU_V1`.

Sources: [STRDC pinout and SPI instructions](https://docs.strdc.com/products/imus/bno085-bob/),
[Maker pin assignments](https://github.com/nulllaborg/maker-esp32-pro), and
[CEVA SPI wake/reset sequence, pages 18–19](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf).
