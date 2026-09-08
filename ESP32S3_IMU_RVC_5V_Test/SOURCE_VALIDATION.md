# Source validation — 2026-09-07

Static checks passed for the sketch SHA256
`9A903C2C58F2ABD76097F82E51B5655CF4934D992FB4E354E2E2AB67451B7CF3`:

- No Wire include/API, GPIO writes, or sensor-UART transmission calls.
- Every explicit `pinMode` call selects GPIO8 as INPUT.
- UART1 requests RX8/TX=-1 and checks actual attached RX/TX pins before capture.
- The arming state waits for sampled idle HIGH for at least 2 ms before UART init.
- One 120-second budget covers both idle-HIGH and first-byte waits.
- Capture ends 35 seconds after the first byte; deadlines are checked before reads.
- Busy `r` leaves the current trial intact; `x` ends it; results remain frozen
  until another explicit `r`. No automatic retries or sensor power operations.

The unchanged `RvcParser.h` matches the previous UART-RVC diagnostic byte for
byte, SHA256
`0C7777A9E1C7F60766E5340DF53C5A327CC0C68BD102EF74D7FE098B61ACBFD8`.

Installed Arduino ESP32 core 3.3.11 source was inspected: UART1 default pins are
selected only when both requested pins are negative. The sketch requests a
positive RX pin and additionally verifies the actual HAL pin mapping. The
available error callback API reports FIFO overflow, receive-buffer-full,
framing, parity and break events. These are event counts, not exact lost-byte
counts; zero events do not establish that capture is lossless.

These are source checks and review, not executed host simulation or hardware
validation. The parent task owns the full Arduino compile record. No flashing,
serial-port access, sensor writes, or electrical tests were performed by the
implementation agent. Stable sampled HIGH is a start condition, not independent
proof of sensor power. The first byte may be boot ASCII; RVC itself does not
provide a direct gyroscope-rate report.
