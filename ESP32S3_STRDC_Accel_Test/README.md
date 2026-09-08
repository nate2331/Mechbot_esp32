# STRDC accelerometer example on ESP32-S3

This is STRDC's supplied `BNO08x_Accelerometer.ino` from
https://github.com/STRDC/strdc-sdk at commit
`2582838df20642f1d2568244daa56e1be1206bf0`, with the same ESP32 HAL as
`ESP32S3_STRDC_Gyro_Test`. The BNO08X module and middleware remain unchanged.
The original sketch and MIT license are preserved in `data/upstream`.

Changes are limited to bundled include paths, selecting the supplied I2C
option, and assigning INT=6, RST=4, P0/WAKE=5 and the unused LED output=47.
The accelerometer example already assigns these pins to its driver object.
`EXAMPLE_CHANGES.diff` and `SOURCE_MANIFEST.json` record the exact adaptation.
See `HAL_PORT_NOTES.md` for the shared ESP32 platform changes.

## Wiring and settings

| ESP32-S3 | STRDC BNO085 |
|---|---|
| 3V3 | VDC |
| GND | GND |
| GPIO8 | SDA |
| GPIO9 | SCL |
| GPIO4 | RST |
| GPIO5 | P0 |
| GPIO6 | INT |

P1 and DI retain their onboard LOW defaults (I2C address `0x4A`); BT retains
its HIGH default. Leave GPIO47 unconnected. Change wires only with power off.
No motors are involved in this test.

Select ESP32S3 Dev Module, COM7, USB CDC On Boot Disabled, USB Mode Hardware
CDC and JTAG, Upload Mode UART0 / Hardware CDC, PSRAM Disabled, and 4 MB flash.
The local core is Arduino-ESP32 3.3.11. Serial Monitor uses 115200 baud.

## Expected behavior and comparison

The original 400 Hz accelerometer request, 400 kHz I2C bus, interrupt reads,
calibration call, retries, and 10 ms print interval are preserved. The banner
is `Stardust Orbital BNO08x Accelerometer Example`. Setup should print
`BNO Initialized Successfully`, complete feature configuration, and reach
`Begin loop()` before printing acceleration in m/s^2.

Use this as the same-port comparison against the supplied gyro example.
The example prints cached values, so repeated lines do not prove fresh
reports. A physical change in orientation should change the acceleration
components. A failed test still needs separation from ESP32 port or transport
issues before concluding that the sensor is damaged.
