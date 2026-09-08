# ESP32-S3 hardware abstraction port

The `hal_*` files in `src/vendor` were copied from STRDC's
`hal/teensyduino/src`, then adapted for Arduino-ESP32 3.3.11. All original
copyright/license blocks are retained. The STRDC middleware and BNO08X module
were not changed by this port.

- **I2C:** `i2c1` is real `Wire`, opened with `Wire.begin(8, 9, speed)`.
  `i2c2` remains real `Wire1`; set its pins before opening if ever used.
  The nonexistent third controller was removed. The vendor's 32-byte transfer
  limit, read loops, timeouts, and transaction behavior are unchanged. The
  sketch supplies the vendor's 400 kHz bus speed.
- **GPIO:** native ESP32 Arduino mode values replace Teensy mode numbers;
  toggle uses `digitalWrite(pin, !digitalRead(pin))`. The unsupported input
  disable mode macro was removed. The sketch assigns RST=4, P0/WAKE=5,
  INT=6, LED=47; the HAL does not substitute absent hardware pins.
- **Interrupts:** STRDC mode `GPIO_LOW` maps explicitly to ESP32 `ONLOW`.
  `attachInterrupt()` installs the callback; `gpio_intr_disable()` and
  `gpio_intr_enable()` mask/unmask that GPIO, including when called from
  the example's ISR. Re-enabling a still-LOW pin retains level-triggered
  behavior. There is no polling substitution. Teensy-only NVIC/peripheral
  IRQ fields and global handlers were removed. Per-pin priority is unsupported
  and logs an error if requested; the example never requests it. Global
  disable/enable uses ESP32 Arduino `noInterrupts()`/`interrupts()` and affects
  the calling core, unlike Cortex-M global interrupt semantics.
- **Timers:** the original microsecond timer/delay code is unchanged.
  GPIO, interrupt, and timer HAL `.c` translation units are named `.cpp`
  for ESP32 Arduino C++ compatibility.
- **SPI (unused by this test):** native ESP32 SPI mode/order constants and
  `transferBytes()` replace Teensy encodings and the separate-buffer overload.
  Only the existing real `SPI` handler is exposed; no fictitious buses were
  introduced. SPI pins would need their own hookup before a SPI test.
- **UART (unused by this test):** the bus type is `HardwareSerial`, format
  is 32-bit `SERIAL_8N1`, and only real `Serial1`/`Serial2` handlers are exposed.
  Basic UART uses ESP32 `begin(speed, format, rx, tx)`; RX clear drains bytes.
  Flow control/RS485 return failure from open because they were not ported.
  Appending caller-owned RX/TX buffers is unsupported and logs an error;
  ESP32 buffer sizes must instead be configured before opening a UART.
  These unused transport paths are compile/link support, not hardware-tested.

The port was checked against the installed ESP32 Arduino GPIO, HardwareSerial,
and SPI headers/implementations. Integration compilation and physical IMU
testing are separate steps; this note does not claim either passed.
