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
