# STRDC BNO085 library comparison — 2026-09-06

The vendor code provides useful startup and transport clues, but no confirmed
fix for this robot yet. The current failure messages do not establish a wiring
fault or a damaged device. This review did not change or flash firmware.

## Sources inspected

- [STRDC SDK](https://github.com/STRDC/strdc-sdk/tree/2582838df20642f1d2568244daa56e1be1206bf0),
  commit `2582838df20642f1d2568244daa56e1be1206bf0`: BNO08X module,
  Rotation Vector example, I2C middleware, and Teensyduino HAL.
- Local Adafruit BNO08x 1.2.7, BusIO, and ESP32 Arduino core 3.3.11 sources.
- Maker_Mechbot initialization and the saved stopped serial transcripts.
- Actual IDE-upload artifact still hashes to
  `3dfb175963d729db389a48d8637966c26401df5b491b1aec1abcc4a02f9d0358`.
  Its dependency file resolves the inspected Adafruit source; its binary contains
  the library's `I2C address not found` diagnostic string.

## Findings

1. **The failure stage is currently ambiguous.** Our `initializeImu()` prints
   `WARN IMU not detected` for any failed Adafruit `begin_I2C()`: initial address
   detection, SH-2 startup, or product-ID exchange. Only then does it scan the
   bus. An empty scan after initialization does not prove the first address
   check failed. The captured 3.3 V follow-up has eight empty scans but no
   `I2C address not found` line. That suggests, without proving, that an earlier
   address check passed and startup failed later. STRDC explicitly distinguishes
   address, advertisement, hub startup, reset-complete, and product-ID failures
   in [its initializer](https://github.com/STRDC/strdc-sdk/blob/2582838df20642f1d2568244daa56e1be1206bf0/modules/STRDC_BNO08X/src/BNO08X.cpp#L197).

2. **Startup handling differs.** STRDC probes the address before its reset, with
   address retries bounded by a 500 ms watchdog. Its reset is high 1 ms, low
   100 us, then high with 100 ms settling. Our wrapper resets and waits 500 ms
   before Adafruit's address check; Adafruit then resets again and sends a
   software reset. STRDC checks individual startup responses; Adafruit's
   `sh2_open()` waits 200 ms for reset completion but returns SH2_OK even if that
   wait expires, leaving the later product-ID exchange to establish progress.
   Our 20 ms Wire transaction timeout and STRDC's 500 ms retry watchdog cover
   different operations; they are not equivalent timeout settings.

3. **A successful report request does not guarantee reports.** STRDC waits for
   and validates the Set Feature response. Adafruit's Set Sensor Config
   operation completes after sending. Our `IMU READY` text can therefore precede
   `WAIT Q0 G0 A0`; fresh sample validation remains necessary. See
   [vendor feature confirmation](https://github.com/STRDC/strdc-sdk/blob/2582838df20642f1d2568244daa56e1be1206bf0/modules/STRDC_BNO08X/src/BNO08X.cpp#L2180).

4. **Extra INT or wake wires are not established fixes for I2C.** STRDC's I2C
   receive path has interrupt waiting disabled and its example supports polling.
   It holds P0 low during I2C reset and raises it afterward, using wake pulses
   when software tracks sleep. Our firmware does not request sleep, and the
   [board documentation](https://docs.strdc.com/products/imus/bno085-bob/)
   permits P0 held low. Current user reports: power LED on, supply rewired after
   3.3 V instructions, RST disconnected. Actual pad voltages and a subsequent
   full power cycle remain unverified.

5. **The vendor SDK is not a direct ESP32 replacement.** Its supplied HAL is
   Teensyduino-specific; the Rotation Vector example defaults to SPI and
   interrupts. Selecting I2C uses 400 kHz, versus our 100 kHz. Those are useful
   test differences, not proof that 400 kHz or SPI selection fixes this board.
   Separately, [Adafruit documents BNO08x I2C timing incompatibilities with ESP32](https://learn.adafruit.com/adafruit-9-dof-orientation-imu-fusion-breakout-bno085/arduino),
   making a transport issue a credible hypothesis.

## Next discriminating test

Use a motor-disabled diagnostic to record bus setup success, SDA/SCL digital
levels, and address results before and after each startup stage. Preserve raw
Wire errors and durations: the installed core reports timeout as code 5 and
ACK as code 0; code 2 includes address-not-found/transaction failure. Compare
bounded retries at 100 and 400 kHz, verify product ID, then confirm one report
before enabling all three streams. Do not infer a voltage from a digital read.

If the transport remains unreliable, an SPI trial can test a different host
interface. It requires a separately checked pin map and physical rewiring;
the vendor's Teensy pin numbers must not be copied onto the motor controller.
