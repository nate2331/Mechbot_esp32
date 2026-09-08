# ESP32-S3 passive UART-RVC test

This custom diagnostic uses the existing wires. It does not use Adafruit SH-2,
the STRDC SDK, motors, firmware updates or persistent configuration writes.

## Verified wiring and mode

Keep 3V3->VDC, GND->GND, GPIO8->SDA, GPIO9->SCL, GPIO4->RST,
GPIO5->P0 and GPIO6->INT. P1 remains at its onboard low default and BT at its
onboard high default. No new wire is required.

The [STRDC schematic](https://docs.strdc.com/schematics/BNO085_BOB-R1_V1_Schematic.pdf)
connects SDA/MISO/TX through Q1 to BNO pin20, provides crystal Y1 and holds CLKSEL0
low. It shows PS0/PS1 pulldowns and BOOT pullup. The UART RX/TX wording in
STRDC's web pin table conflicts with its schematic; the schematic and chip pin
table agree that the SDA wire is the IMU's UART TX output.

The [CEVA BNO08X datasheet](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf),
sections1.2.1 and1.2.5, specifies RVC with PS1=0/PS0=1 at reset, reception at
1152008N1, and **19-byte** checksum frames. Section5.1 describes the ASCII
startup banner before binary reports. This firmware follows those definitions.

## Run

Use ESP32S3 Dev Module, USB Mode Hardware CDC and JTAG, USB CDC On Boot Disabled,
and COM7 at115200. On boot the sketch runs one35-second trial automatically:

1. Hold RST low, set P0 high, and attach Serial1 RX to GPIO8 with TX=-1.
2. Release RST. Count raw bytes and checksum-valid frames, index repetitions,
   discontinuities and changes in angle/acceleration values.
3. Print a final summary and the first128 bytes as hex and sanitized ASCII.
4. Hold RST low, detach UART, set P0 low, release RST, probe I2C0x4A/0x4B, and idle.

GPIO9 is an input during RVC. The sketch never transmits UART data to the IMU.
The installed ESP32 core3.3.11 assigns default UART1 pins only when both requested
pins are negative; RX8/TX=-1 therefore does not assign a default TX pin.

`x` stops and restores I2C early; `r` repeats while idle; `s` prints the current
or frozen sample snapshot. The reset restores interface selection only and does
not remove power. `RESTORE ACK ... rc=0` corroborates return to I2C; a commanded
GPIO level alone is not confirmation of remote pin state.

For the current controlled capture script, use protocol `stream` with a37-second
or longer capture window. It already waits2seconds before that window and resets
the S3 with DTR low and an RTS pulse. Leave Arduino Serial Monitor closed to
avoid the observed USB-UART DTR/RTS bootloader issue.

## Interpretation and provenance

Increasing valid frame counts plus changing indices demonstrate new reports.
Identical physical values can be normal on a stationary board; index repetitions
are counted separately. Discontinuities are not automatically lost-packet counts.
RVC provides orientation and acceleration, not an explicit gyro-rate output.
If only startup ASCII arrives and raw byte count then stops, packet parsing alone
cannot explain the absent later bytes.

`RvcParser.h` is copied unchanged from the previously verified local
`Maker_IMU_UART_RVC_Check/RvcParser.h`. Its fixed19-byte parser checks all payload
bytes, handles signed values, and preserves overlapping headers after noise.
The new sketch changes host pins, adds a finite observation window, additional
counters and I2C restoration. No earlier sketch or shared library is modified.
