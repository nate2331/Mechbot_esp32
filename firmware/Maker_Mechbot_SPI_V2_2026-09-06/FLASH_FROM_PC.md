# Maker SPI V2 — manual PC upload

This revision addresses the user's brief-startup-then-STALE logs. It keeps the
existing motor/encoder assignments and SPI wiring. It uses Adafruit/CEVA's SH-2
decoder with a local SPI transport and a queue of decoded reports. STRDC's
driver was reviewed as the reference for wake and full-duplex communication;
this package does not run the STRDC SDK directly.

## Upload in Arduino IDE

1. Keep external motor power off and connect Maker USB to this PC.
2. Extract this V2 ZIP into a new folder. Open
   `Maker_Mechbot/Maker_Mechbot.ino`; keep the four `.h` files with it.
3. Select **ESP32 Dev Module** and the Maker's COM port. Keep **Erase All Flash
   Before Sketch Upload: Disabled**, then click **Upload**.
4. Open Serial Monitor at **115200 baud** with newline ending. Send `?` and
   confirm the new identity:

```text
FIRMWARE MAKER_SPI_V2_STALE_RECOVERY
```

The tested PC setup is ESP32 core 3.3.11, Adafruit BNO08x 1.2.7, BusIO 1.17.4
and Unified Sensor 1.1.15. These are already installed on this PC.

## Check the result

Send these commands individually, keeping motor power off:

```text
X
IMU DIAG
DIAG
```

`IMU DIAG` gives orientation (Q), gyro (G) and acceleration (A) validity and ages
in milliseconds, INT level, recovery counts and transport counters. Age `-1`
means no valid sample. `DIAG` should show all four PWM outputs and all eight
pin duties at zero.

Collect at least 30 seconds of serial output. Success requires numeric `I`
records with advancing `H` orientation timestamps, fresh Q/G/A ages, no recurring
resets/reinitializations and no repeated `STALE`, `WAIT` or `OFFLINE`. A single
numeric record or the report-request message is insufficient.

V2 keeps incoming SPI data during command writes, reads each packet with one
CS assertion, retains each decoded report from bundled packets, and returns
terminal wake timeouts as errors. It retries missing or stale streams while
stopped and escalates to reinitialization if they remain unhealthy. It also
buffers Serial output so ordinary telemetry bursts do not delay SPI polling.

Software verification is recorded in `manifest.json`. Live sensor behavior is
not established by a successful build or simulation.

## Binary flashing

For a binary flasher: chip **ESP32**, mode **DIO**, frequency **80 MHz**, size
**4 MB**. Use the four files inside `bin/` at these addresses:

| File | Address |
| --- | --- |
| `Maker_Mechbot.ino.bootloader.bin` | `0x1000` |
| `Maker_Mechbot.ino.partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `Maker_Mechbot.ino.bin` | `0x10000` |

Leave whole-chip erase disabled to preserve saved settings. The application
binary belongs at `0x10000`, not address zero.

Pin map: SCK22, MISO21, MOSI32, CS33, INT26, RST25, WAKE/P0 16, P1 and VDC3.3V,
common ground. Bus communication remains **1 MHz, SPI mode 3**.

References: [STRDC driver](https://github.com/STRDC/strdc-sdk/blob/2582838df20642f1d2568244daa56e1be1206bf0/modules/STRDC_BNO08X/src/BNO08X.cpp#L574),
[Gyro example](https://github.com/STRDC/strdc-sdk/blob/main/modules/STRDC_BNO08X/examples/Arduino/BNO08x_Gyro/BNO08x_Gyro.ino),
[Multi example (two IMUs)](https://github.com/STRDC/strdc-sdk/blob/main/modules/STRDC_BNO08X/examples/Arduino/BNO08x_Multi/BNO08x_Multi.ino),
[SPI wiring](https://docs.strdc.com/products/imus/bno085-bob/#spi-1).
