# UART-RVC diagnostic validation

This is an isolated passive diagnostic. It has not been flashed by the assistant
or tested on the user's IMU. The user's preceding SPI test remained at two
decoded reports across six consecutive ten-second summaries.

## Host checks

Both focused tests passed using g++ -std=c++17 -Wall -Wextra -Werror:

- maker_rvc_parser_test.cpp: official CEVA packet and signed values; all split
  positions; index wrap/duplicates/skips; noise, bad checksums and truncation;
  overlapping headers; embedded AA bytes and reserved-byte checksum coverage.
- maker_rvc_sketch_test.cpp: both UART receivers have explicit RX pins and no
  TX; exactly one reset with mode HIGH before release; pin mode is configured
  before writes to P0/RST; all eight motor directions stay LOW without PWM;
  live counters and WAIT/LIVE/STALE output; no reinitialization over a simulated
  minute without data. The fake serial class has no write method, so the test
  build cannot silently introduce UART transmissions.

## Source review

Independent review checked the actual ESP32 core3.3.11 implementation. UART1
and UART2 start without assigned pins. With RX explicitly21/22 and TX=-1,
HardwareSerial does not assign a default TX pin. The final sketch sets OUTPUT
before writing P0 HIGH and RST LOW, because ESP32 ignores a digitalWrite before
GPIO registration. This was corrected before the final compilation.

The parser uses a fixed19-byte buffer, and each receiver drains at most512bytes
per loop iteration. The diagnostics print live counters once per second rather
than depending on blocking reads. There is no SPI or Adafruit/SH-2 dependency.

STRDC's schematic maps SDA/MISO/TX through Q1 to BNO pin20 (UARTTX), and
SCL/SCK/RX to pin19 (UARTRX). Its UART web table and drawing conflict with that
mapping. Both existing lines are listened to independently, without transmitting,
so the diagnostic can identify the actual output safely. It predicts frames on
GPIO21 but does not rely on that prediction to capture data.

## What remains unverified

Host tests do not establish actual UART bytes, wiring, mode selection, physical
signal integrity, or sustained sensor output. The user must flash the sketch,
power off, move P1from3.3VtoGND, retain the existing P0/GPIO16 and RST/GPIO25
connections, and power back on to obtain hardware evidence. This test is fixed
UART-RVC output and does not implement the full robot sensor protocol.

Final ESP32 build passed: esp32:esp32:esp32, core 3.3.11. Program 273680 bytes (20%); globals 22468 bytes (6%). The final generated sketch confirms OUTPUT is set before the RST/P0 writes. No hardware result yet.
