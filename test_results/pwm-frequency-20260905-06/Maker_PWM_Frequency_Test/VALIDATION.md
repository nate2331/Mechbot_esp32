# Validation — September 6, 2026

Archive note (September 8): the build-time statement below that this source had not yet been uploaded predates the supplied user-operated sweep logs. Completed measurements and their limits are documented in the [archive overview](../README.md). This record describes the original compile/host validation, not a new hardware action.

The final source compiled with Arduino CLI 1.5.1, Espressif Arduino ESP32 core 3.3.11 / ESP-IDF 5.5.5, board esp32:esp32:esp32, and --warnings all.

- Compiler exit status: 0; zero warnings or errors.
- Program storage: 289,747 bytes (22%).
- Global RAM: 26,464 bytes (8%).
- Final source SHA-256: 72BC359A83637C236B2D79FD0DF15C71FDEB5EE98657F6660EA2CB40B0449522.
- The source did not change during compilation.
- 26 host simulations passed against the actual sketch with deterministic Arduino/IDF stubs. They cover boot outputs, timed/no-edge/emergency stops, queue cancellation, driver-error injection, parser limits, balanced repeatable plans, duty scaling, CSV fields, encoder transitions, timer rollover, and restart state.

Host simulation checks software behavior. It does not simulate the motor, transistor switching, interrupt concurrency, or actual current/voltage waveforms. The source has not been uploaded or tested on the robot in this task.

September 8 archive check: the copied sketch matches the source SHA-256 above and all 26 host simulations passed again using the retained source and stubs. The portable runner now accepts `-Compiler` or finds `g++` on PATH. No new ESP32 platform build, upload or hardware operation was performed for the archive.
