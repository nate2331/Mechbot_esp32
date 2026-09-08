# Passive UART-RVC check

This standalone test checks whether the STRDC BNO085 can send a continuous
stream without SPI or report-enable commands. It holds the Maker's eight motor
direction pins low and does not implement robot commands. It uses the ESP32
core only; no IMU library installation is needed.

## Upload and make one wiring change

1. Open `Maker_IMU_UART_RVC_Check.ino` in Arduino IDE, with `RvcParser.h` in the
   same folder. Select **ESP32 Dev Module**, select the Maker USB port, and upload.
2. Turn off power to the Maker and IMU, including USB power.
3. Move the **IMU P1 wire from 3.3 V to GND**. This is the only wiring change
   from the last SPI diagnostic.
4. Keep **IMU P0 on GPIO16**, **RST on GPIO25**, **SDA on GPIO21**, and
   **SCL on GPIO22**. VDC stays on **3.3 V**, and GND stays on common GND.
5. Power on and open Serial Monitor at **115200 baud**. Paste three consecutive
   pairs of `RVC RX21 ...` / `RVC RX22 ...` status lines. If readings appear,
   gently turn the IMU and check whether the printed angles change.

The firmware holds P0 HIGH and resets the IMU once after configuring both
receive ports. P1 LOW and P0 HIGH select UART-RVC at that reset. The old DI,
CS and INT wires can remain: GPIO32, GPIO33 and GPIO26 are inputs in this
sketch. Neither serial port has a transmit pin. There is no automatic reset loop.

## Reading the result

The sketch listens independently on the two existing data wires, GPIO21 and
GPIO22, to handle an inconsistency in STRDC's UART pin documentation. The
schematic and chip pin definitions predict data on **SDA / GPIO21**. The
published UART drawing and table disagree. Listening to both pins avoids
driving a possible IMU output and identifies which carries valid packets.

Expect one receiver's `FRAMES` to keep increasing, with roughly **100 `NEW`
frames per second**, and `LIVE`. The other receiver can stay `WAIT`.

- `FRAMES`: cumulative packets that passed the checksum.
- `NEW` and `HZ`: packets received since the preceding summary, and measured rate.
- `AGE_MS`: time since the last valid packet. `STALE` means over 500 ms old.
- `BAD`: failed checksum candidates; a startup banner or noise can require resync.
- `INDEX_GAPS`: adjacent valid packet indexes were not consecutive modulo 256.
  This can indicate losses, duplicates, or a device restart; it is not an exact
  count of dropped packets.
- `BYTES`: received bytes, including startup text and invalid packet candidates.
- `FIRST_BYTES`: initial received bytes, shown if no valid packet has decoded.

Angles are degrees; acceleration is in mg. UART-RVC supplies fixed orientation
and acceleration output, rather than the complete SH-2 quaternion/gyro/control
interface. It is a diagnostic, not a replacement for the robot firmware. Good
streaming here would justify further UART integration; failure here would still
require checking mode selection, wiring and the device before blaming hardware.

## Sources and build

Protocol and mode selection: [CEVA BNO08X datasheet, sections 1.2.5 and 5.1](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf).
Board connections: [STRDC schematic](https://docs.strdc.com/schematics/BNO085_BOB-R1_V1_Schematic.pdf)
and [STRDC UART-RVC documentation](https://docs.strdc.com/products/imus/bno085-bob/#uart-rvc).

Built for `esp32:esp32:esp32` with ESP32 core 3.3.11. In this core, specifying
an RX pin and TX = -1 on a fresh serial port leaves TX unattached; it does not
claim default pins. The test uses UART1/RX21 and UART2/RX22, both at 115200 8N1.
