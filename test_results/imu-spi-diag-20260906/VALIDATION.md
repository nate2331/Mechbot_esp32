# SPI packet diagnostic validation — 2026-09-06

User measured a steady 3.3 V between IMU VDC and GND with RST connected, then
confirmed DI and CS both reconnected. A multimeter reading does not rule out
brief rail transients. V2's repeated host reset cycles complicate interpreting
the reported LED pulses; this diagnostic removes those cycles.

## Offline checks

- Focused trace transport test passed (`maker_bno_spi_trace_test.cpp`, strict
  g++ C++17 with `-Wall -Wextra -Werror`): packet ordering/content, full declared
  length versus bounded prefix, null filtering, invalid headers, capacity,
  overflow, failed-init retention and capture reset.
- Actual diagnostic sketch host test passed (`maker_spi_diag_firmware_test.cpp`,
  same strict flags): no automatic recovery over simulated minutes while
  missing, stale or offline; no reset-triggered report reconfiguration; motion
  commands leave outputs zero; manual retry and read-only trace remain usable.
- Independent source review found no additional reset call sites beyond setup
  and manual `IMU RETRY`, unchanged pin map, and no motor/encoder pin conflicts.
- ESP32 core 3.3.11 compile passed: 361631 bytes program, 33840 bytes globals.
  Compile log is in this directory. Adafruit BNO08x 1.2.7, BusIO 1.17.4,
  Unified Sensor 1.1.15. FQBN `esp32:esp32:esp32`.

## Source and command-line build hashes

SHA-256:

- `Maker_Mechbot_SPI_Diag.ino`: `486d2bd57cec89d063b436398d4966c9ac0f3cbb0cc5e7ffcda45f1f42fcf4be`
- `MakerBnoSpi.h`: `d8a07c36e1edd36750602a1618f127e3503675435a0859e34f0a310396c88f15`
- CLI application binary: `235396adcdd70befcf6cf3c30e09c533a97862546eb8d257bdaac3bcb9da0e42`

Arduino IDE rebuilds before upload, so its application binary need not be
byte-identical to the command-line build. Hardware upload/results are recorded
in `PC_UPLOAD_RESULT.md` after observation.

## V3B bounded control probe

The extended transport test passed for sequence continuity, rollover, response
capture, invalid requests, timeout and backpressure. The diagnostic sketch host
test also passed: the manual probe services input for two500 ms windows and
does not reset, enable reports, or energize motors. Independent review found
no conflicting automatic SH-2 control writes after the raw probes.

Compile passed:362327 bytes program,33840 bytes globals; `compile-v3b.log`.
SHA-256 values for V3B:

- Sketch: `608c6986de7952f6169f1abef515decb39ff54d3862840f69fe212b29bebca6c`
- Adapter: `349eb1ff17d3849673b2f320546ec5ad3212b568c98a57d918ce840e233046b1`
- CLI binary: `211b6e6d85332ec964e4e037789fe1ad5abe8672180397802d9f4f38359ca890`

## V3C complete outgoing packet per SPI call

Strict transport tests passed for21-byte TX with null,20-byte,52-byte,276-byte
and invalid RX. Checks cover exact TX bytes and zero padding, call counts,
word alignment, continuous CS and complete queued RX. The diagnostic sketch
host test also passed. Independent source review found no buffer, alias,
sequence or pin mapping issue. Arduino IDE performs the ESP32 compile before
upload; its observed result is recorded in `PC_UPLOAD_RESULT.md`.

SHA-256 values:

- Sketch: `864491e6ab455a4c9a73a4a86781774198748742114fa8abcb18be340511028f`
- Adapter: `115c9d4664b4fe60557cc204fdb3a95e9aa5f095e469ce1e86a532426d39a22d`
