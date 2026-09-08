# Validation

## Compile

Passed on 2026-09-06 local time using Arduino CLI 1.5.1 and ESP32 Arduino core 3.3.11.

FQBN: `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default`

```text
Sketch uses 341382 bytes (26%) of program storage space. Maximum is 1310720 bytes.
Global variables use 75056 bytes (22%) of dynamic memory, leaving 252624 bytes for local variables. Maximum is 327680 bytes.
```

Application binary: 341,536 bytes; SHA256 `f992ed810d9791ae044da4e9e5a0225e6b7da6d5b9f1b77c575c5dc381a152d8`.

Command:

```powershell
& 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --config-file 'C:\Users\nate2\.arduinoIDE\arduino-cli.yaml' --fqbn 'esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default' --output-dir 'C:\Users\nate2\AppData\Local\Temp\codex-strdc-gyro-build-20260907' 'ESP32S3_STRDC_Gyro_Test'
```

## Source verification

All 14 BNO08X module and middleware files were checked by SHA256 and match the pinned Windows checkout byte for byte. The original middleware `.c` files compile as supplied; no middleware extension changes were necessary. The example diff contains only bundled include paths, selecting I2C, GPIO definitions, and the missing shared pin assignments copied from the vendor rotation-vector example.

The ESP32 HAL preserves the vendor's 32-byte I2C reads, timing, and interrupt-driven read behavior. See `SOURCE_MANIFEST.json`, `EXAMPLE_CHANGES.diff`, and `HAL_PORT_NOTES.md` for the complete record.

An independent static review found no concrete pin or interrupt defect in the adaptation. This does not replace the pending physical test.

## Hardware status

At completion of the build, the sketch was open in Arduino IDE 2.3.10 with ESP32S3 Dev Module on COM7 selected. The assistant had not uploaded it; confirmation of the added RST=4, P0=5, and INT=6 wiring was pending.

The user subsequently supplied a runtime excerpt with this pair repeating:

```text
[DEBUG] Timed out waiting for Set Feature Command Response
BNO Failed to set feature report
```

The excerpt is consistent with the prepared STRDC gyro example running, but does not independently verify the flashed binary hash, physical control-pin wiring, or full startup transcript. The brief leading garbled characters are insufficient to diagnose any electrical problem.

### What this result establishes

- In the prepared example, reaching this loop means `bno08x_init()` returned success. The complete startup messages were not included in this excerpt.
- `bno08x_feature_set()` sends the calibrated-gyro Set Feature command (report `0x02`, requested 400 Hz), and this specific timeout occurs after the transport send returned success. An acknowledged I2C write does not itself prove the sensor applied the command.
- The driver then fails to parse a matching `0xFC` feature response within its 250 ms wait. It retries indefinitely, producing the repeated pair of lines.
- The failure precedes ME calibration and the example's `interrupt_init()`/`interrupt_set()` calls. Feature setup polls I2C directly; a missing INT connection alone does not explain this timeout.
- This is neither a gyro sample result nor proof of permanent physical damage. It is compatible with the earlier gyro-dependent failure, but also leaves the vendor driver's response expectations and I2C response delivery unresolved.

### Driver caveat and next comparison

The unchanged upstream driver waits for a Get Feature Response after sending Set Feature; it does not explicitly send Get Feature Request in this path. CEVA's SH-2 reference manual section 6.5.5 documents Get Feature Responses for explicit Get Feature Requests and unsolicited responses when a sensor's rate changes. Repeatedly sending an unchanged configuration therefore is not a reliable way to establish that fresh responses must keep arriving. This caveat does not explain whether the first enable attempt failed or whether any gyro samples arrived.

Source: https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf (sections 6.5.3–6.5.5).

The next useful comparison is STRDC's supplied accelerometer example using the same ESP32 HAL and wiring. Passing its feature-setup stage would help distinguish a gyro-dependent failure from a general problem with this vendor setup/transport path. A later explicit feature readback and fresh-sample check would be a separate diagnostic change, not the unchanged vendor example requested here.

The earlier custom accel/gyro diagnostic results are recorded separately in `../ESP32S3_IMU_Test/VALIDATION.md`; they are not results from this STRDC example.
