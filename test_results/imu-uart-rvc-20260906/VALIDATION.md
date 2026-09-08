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

## User UART-RVC hardware result: banner only

The user supplied running V1 output with RX21 BYTES78, FRAMES0, NEW0, BAD0,
INDEX_GAPS0, and RX22 BYTES0. RX21's first24 bytes were:
25 48 69 6C 6C 63 72 65 73 74 20 4C 61 62 73 20 31 30 30 30 34 31 34 38
This decodes to: %Hillcrest Labs 10004148

The counters were unchanged at72021ms. A following ESP sketch startup banner
was followed by the same78/0 counters at1021,2021,3021,4021,5021ms. The cause
of the ESP restart was not supplied and should not be inferred as a brownout.

CEVA BNO08X datasheet section5.1 describes the ASCII startup banner in RVC
mode and states sensor packets follow it. This result therefore establishes
successful startup-text reception on GPIO21 and strongly confirms RVC mode,
but no following sensor bytes reached this sketch. Raw BYTES is incremented
before parser.feed, so a packet-parser defect cannot by itself freeze BYTES78.

Prior SPI testing also failed to sustain reports. This raises suspicion of
sensor-side startup, power, board, or remaining electrical connections rather
than establishing any particular hardware defect. No new firmware was flashed
by the assistant and no hardware failure is considered proven by this result.

Next requested hardware evidence: measure the IMU board's 3V3 output pin relative to its GND with this setup powered. The earlier stable3.3V measurement was at VDC; the regulated rail has not been reported. No firmware or wiring change is required for this measurement. Independent review found no parser/receive-loop explanation for the raw count staying78 while a normal stream arrives.
