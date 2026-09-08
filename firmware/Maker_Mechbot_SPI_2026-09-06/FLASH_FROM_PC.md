# Maker ESP32 Pro — SPI firmware

This is the compiled and software-tested SPI build for the NULLLAB Maker ESP32
Pro and STRDC BNO085 wiring already provided. Live SPI sensor operation still
needs verification after upload.

## Arduino IDE on this PC

1. Keep external motor power off. Connect the Maker's USB cable to this PC.
2. Extract the ZIP, then open `Maker_Mechbot/Maker_Mechbot.ino` in Arduino IDE.
   Keep all four `.h` files beside the sketch.
3. Select **ESP32 Dev Module** and the Maker's current COM port. The tested setup
   uses ESP32 core **3.3.11**, Adafruit BNO08x **1.2.7**, Adafruit BusIO **1.17.4**
   and Adafruit Unified Sensor **1.1.15**, already installed on this PC.
4. Leave **Erase All Flash Before Sketch Upload: Disabled**. Click **Upload**.
5. Open Serial Monitor at **115200 baud**, select a newline ending, and send `?`.
   The response must include:

```text
IMU: SPI SCK22 MISO21 MOSI32 CS33 INT26 RST25 WAKE16; relative yaw, NOT compass north
```

Then send `X` and `DIAG`. All wheel PWM values and pin duties should be zero.
Repeated numeric `I` lines indicate fresh orientation, gyro and acceleration
reports. `OFFLINE`, `WAIT` or `STALE` indicates the IMU is not providing all three
fresh streams. The report-request message by itself does not establish success.

## Already compiled binaries

For a serial flash tool that accepts binaries, choose chip **ESP32**, flash mode
**DIO**, frequency **80 MHz**, size **4 MB**, and these four files and offsets:

| File inside `bin/` | Address |
| --- | --- |
| `Maker_Mechbot.ino.bootloader.bin` | `0x1000` |
| `Maker_Mechbot.ino.partitions.bin` | `0x8000` |
| `boot_app0.bin` | `0xe000` |
| `Maker_Mechbot.ino.bin` | `0x10000` |

Use normal sector erasure for those writes; disable whole-chip erase. These
ranges preserve the saved-settings partition at `0x9000–0xdfff`. The application
file is **not** a complete image to write at address zero.

`manifest.json` records the exact file hashes, pin map and software validation.
See [SPI_WIRING.md](SPI_WIRING.md) for the physical connections.
