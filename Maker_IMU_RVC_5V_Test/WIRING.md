# NULLLAB Maker ESP32 Pro / STRDC BNO085 5V UART test

This is a custom receive-only UART-RVC diagnostic. It does not use the STRDC I2C example. RVC reports orientation and acceleration, not separate gyro rates.

Use the confirmed **SN74LVC245AN**, powered from the Maker's **3.3V** supply. The STRDC IMU's **VDC** receives regulated **5V**. All grounds join. The chip's 5V-tolerant input protects the ESP32 from the IMU board's external signal pull-up to VDC. Do not power the LVC chip from 5V.

## Wiring with all power disconnected

Use an isolated breadboard circuit; remove connections to other sensors. With the IC notch facing left and the marking readable as in the user's photo, pin 1 is bottom-left, pin 10 bottom-right, pin 11 top-right and pin 20 top-left.

| SN74LVC245AN pin | Connection |
| --- | --- |
| 20 VCC | Maker 3.3V |
| 10 GND | Common ground |
| 1 DIR | 3.3V (A to B direction) |
| 19 /OE | Ground (enabled) |
| 2 A1 | STRDC IMU SDA / MISO / TX |
| 18 B1 | Maker GPIO21 / SDA |
| 3 through 9 | Ground; these are unused A inputs |
| 11 through 17 | Disconnected; these are unused B outputs |

Place a 100nF (0.1uF) bypass capacitor at pins 20 and 10 with short connections. Ceramic is preferred. The user's available 0.1uF/50V electrolytic is a provisional bench substitute: positive to pin 20, striped negative to pin 10. This substitute is less effective at suppressing fast switching noise; a noisy or failed test would need confirmation with suitable bypassing.

| STRDC IMU pin | Connection |
| --- | --- |
| VDC | Regulated 5V, applied only after the receiver is armed |
| GND | Common ground |
| P0 | Its own VDC (select UART-RVC) |
| SDA / MISO / TX | LVC chip pin 2 |
| SCL, RST, INT, CS | No ESP32 connection |
| P1, BT | Leave onboard defaults: P1 low, BT high |

Remove the previous direct SDA-to-GPIO21 wire and all previous ESP32 wires to IMU RST, P0, SCL and INT. Remove the old 3.3V-to-VDC supply wire. The LVC output is the only IMU signal connection to the ESP32. GPIO21 here is UART input, not I2C SDA.

## Test sequence

1. Keep IMU 5V disconnected. Connect the Maker by USB and upload this new sketch. Power the buffer from Maker 3.3V.
2. Open serial at 115200 baud. Send `?` for the sketch's current instructions, then `r` to arm.
3. Apply IMU 5V after the ARMED message. Keep ground connected throughout. The receiver waits for a high idle level before enabling UART, then records 35 seconds starting from the first byte. There is a bounded 120 second wait for startup.
4. Keep the IMU still initially, then gently rotate it during capture. Save all console output, including the final result and startup prefix.
5. Remove IMU 5V before changing wiring. For another test, remove IMU 5V, send `r`, and reapply IMU 5V. There are no automatic retries.

`x` stops the observation; it does not switch off external IMU power. The sketch never resets the IMU, changes interface straps, sends commands to it, or switches GPIO21 back to I2C.

## References

- [TI SN74LVC245A datasheet](https://www.ti.com/lit/ds/symlink/sn74lvc245a.pdf): 3.3V operation, 5.5V-tolerant inputs, direction and enable pins, bypass recommendation.
- [STRDC board schematic](https://docs.strdc.com/schematics/BNO085_BOB-R1_V1_Schematic.pdf): VDC regulator, external SDA pull-up and level translation, interface straps.
- [CEVA BNO080/085 datasheet](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf): UART-RVC interface and report format.

Software build and hardware-test status are recorded separately; this wiring document is not evidence of a completed physical test.

## Maker-specific power and firmware

Use **USB power only** with the external DC/battery disconnected and the main DC switch OFF. The manufacturer's schematic shows USB feeding the exposed +5V rail. This is USB-derived power, not a separate 5V regulator operating from USB.

The I2C header power pin is +5V and may supply IMU VDC. The SPI header power pins are 3.3V and may supply LVC pin20/DIR. Follow printed labels: the OLED connector reverses SDA/SCL ordering relative to the regular I2C headers. Disconnect other I2C peripherals for this isolated trial.

LVC pin18 goes to **GPIO21, labeled SDA**, which the new sketch uses as UART RX. The S3's GPIO8 assignment does not apply. The Maker's onboard GPIO21 pull-up is 10k to 3.3V.

Build/upload **Maker_IMU_RVC_5V_Test**, board **ESP32 Dev Module**, Espressif core3.3.11. Use serial115200 through the Maker's USB-to-UART interface. All eight motor-driver inputs (27,13,4,2,17,12,14,15) are held LOW; no PWM or motor commands are used. Keep external motor power disconnected.

Do not use the older Maker_IMU_UART_RVC_Check sketch for this wiring: it drives the IMU mode/reset pins, which are disconnected in this 5V test.

[NULLLAB Maker ESP32 Pro schematic](https://github.com/nulllaborg/maker-esp32-pro/blob/main/maker-esp32-pro.pdf). These power and pin findings are from the schematic; physical wiring and voltages have not yet been checked.