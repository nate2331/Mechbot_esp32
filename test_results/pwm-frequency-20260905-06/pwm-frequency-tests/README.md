# Host simulation tests

Run `./run-tests.ps1` from PowerShell with `g++` on PATH, or pass
`-Compiler 'C:/path/to/g++.exe'`. This compiles the actual neighboring
`Maker_PWM_Frequency_Test.ino`, replacing Arduino, GPIO, LEDC, time, interrupts,
and serial with deterministic host stubs. Each scenario runs in a fresh process.
The PWM stubs model the current ESP-IDF API: stopping a channel disables its
output while retaining its programmed duty register and pin mapping.

The scenarios exercise output initialization, real parser and state-machine
paths, timed completion, watchdog and operator cancellation, queued trials,
API failures, quadrature handling, balanced reproducible ordering, duty scaling,
CSV completeness, microsecond wraparound, restart with stale duty retained,
and stopped transitions between queued trials. They do not operate hardware.

This is not an ESP32 compiler check or an electrical simulation. Clock divider
selection, real peripheral timing, GPIO ownership after a failed stop, ISR
concurrency, real serial blocking, and actual pin waveforms require platform or
hardware verification. The stub models GPIO mode selection as restoring GPIO
ownership, so the failure-path tests verify the sketch's fallback calls and
fault latch, not an absolute physical guarantee after an API failure.
