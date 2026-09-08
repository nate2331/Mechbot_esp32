# Maker navigation source verification — September 8, 2026

No upload, serial reset or motor trial performed. This build still uses the
experimental SPI transport; do not upload it onto the current buffered UART setup.

## Change and tests

The field-reference fault now latches a stop. Recovered sensor data, ordinary
velocity commands, X, Z and CFG changes do not clear it. Explicit F 0 or a
successful fresh F 1 is required. This closes the prior IMU-reset path that could
silently reinterpret queued field commands as robot-relative commands.

Both actual-sketch host programs passed with C++17 and warnings as errors:

- `tests/maker_navigation_host_test.cpp`: heading capture, correction sign,
  deadband, limit, deliberate turns, idle, wraparound, robot-relative dropout
  bypass, field status gating, headings 0/+90/-90/180, re-zero, stale/reset latch,
  explicit mode recovery and N-ready clearing while latched.
- `tests/maker_firmware_host_test.cpp`: existing motor/encoder, independent output
  timeout, malformed commands, settings and SPI recovery regression coverage.
  Its recovery scenario now explicitly selects F 0 before robot-relative motion.

Target compile passed with installed ESP32 core 3.3.11 and
`esp32:esp32:esp32:FlashMode=dio`: 366,987 bytes flash and 31,920 bytes globals.
`manifest.json` records final source and binary SHA256 identities.
Help identifies this source as `MAKER_SPI_V3_FIELD_FAULT_LATCH`; the established
robot READY board token is unchanged.

To reproduce each host program from the repository root, use Zig's C++ driver
(or a compatible native compiler):

```powershell
zig c++ -std=c++17 -Wall -Wextra -Werror -Itests/maker_host_stubs tests/maker_navigation_host_test.cpp -o build/maker-navigation-test.exe
./build/maker-navigation-test.exe
zig c++ -std=c++17 -Wall -Wextra -Werror -Itests/maker_host_stubs tests/maker_firmware_host_test.cpp -o build/maker-firmware-test.exe
./build/maker-firmware-test.exe
```

Notion tasks 03 and 05 still require sensor confidence, RVC integration and
physical straight-drive/multi-heading results. Task 04 tuning and task 08 floor
acceptance remain unperformed. The interface review is in
`docs/NAVIGATION_INTERFACE_PLAN.md`; all task gates are in
`docs/NOTION_TASK_AUDIT_2026-09-08.md`.
