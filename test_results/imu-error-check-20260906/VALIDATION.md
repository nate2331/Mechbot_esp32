# Automatic IMU error-check validation

The isolated sketch uses the existing SPI pin mapping and does not issue motion
commands. No hardware upload has been performed for this build.

Host checks passed with g++ -std=c++17 -Wall -Wextra -Werror and the existing
maker_host_stubs:

- maker_imu_error_sketch_test.cpp: startup success and startup failure; all eight
  motor direction pins stay low; no PWM attaches; automatic summary appears;
  missing data, reset notifications and incoming serial text cause no retries
  or further queries during a simulated minute.
- maker_bno_error_check_test.cpp: F2 command framing and sequences; full-packet
  bundled error records; matching and empty terminators; unmatched, truncated,
  continuation and unknown-record rejection; 32-record overflow; bounded wake
  timeout; failed-send and initialization cleanup.

Independent source review confirmed the error response offsets against SH-2,
no motor-output path, and no later SH-2 control writes after raw queries. Review
also identified that a terminal marker is not proof of gap-free error capture;
README documents this and the severity-0 limitation.

These checks verify firmware behavior and parsing in fake IO. They do not verify
sensor hardware, wire timing, acceptance of feature writes, or sensor reports.

ESP32 compilation and binary hashes are recorded below when complete.

ESP32 build passed: esp32:esp32:esp32, core 3.3.11. Program 311316 bytes (23%); globals 33132 bytes (10%). See compile.log and build outputs. No hardware result yet.

## First user hardware result

The user manually flashed MAKER_IMU_ERROR_CHECK_V1 and supplied the first result
block, preserved in user-first-result.txt (Markdown underscore escaping removed).
Quaternion configuration is now 20000 us, compared with 0 in the prior V3C
probe. Two events decoded; transport RX12/TX7 with no bad-header or wake timeout
count. A matching error-log terminal was received with zero captured records
at severity threshold 0. This is not an all-severity or gap-free health check.

This demonstrates report configuration acceptance and at least two decoded
reports in this run. It does not establish sustained streaming. The first
summary follows 3000 ms of service waits after startup. Further summaries read
the live cumulative event counter, so the next two SENSOR EVENTS lines will
show whether decoding continues. The quaternion interval in repeated summaries
comes from the stored startup trace, not a new configuration query.

Firmware timing and runtime workload both differ from the earlier robot sketch;
this result alone does not isolate the change responsible. No wiring change or
additional flash was requested; the next evidence is from the running sketch.

## Repeated user result: streaming stalled

The user then supplied six consecutive summary blocks. All six remained at
SENSOR EVENTS2, RX12, TX7, INT1, with the stored quaternion interval20000 and
error-log terminal/zero records unchanged. The sketch's summaries read fresh
cumulative RX and event counters every10seconds. Thus the repeated output
confirms no ongoing received or decoded stream over this interval; it is not a
cached-counter display issue. Two decoded reports do not establish that all
three requested sensor types were received.

The next diagnostic is the isolated passive Maker_IMU_UART_RVC_Check. No new
SPI flash was performed. Its required mode change is P1from3.3VtoGND, while
firmware holds the already-wired P0/GPIO16 high before resetting once. It uses
two receive-only UART ports on existing SDA21/SCL22 to resolve the conflicting
UART labels between STRDC's schematic and its web table/reference drawing.
