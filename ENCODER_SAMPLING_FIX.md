# Encoder sampling update — September 4, 2026

The Maker firmware now samples each encoder's A/B inputs from one GPIO input register snapshot. Previously the ISR and startup initialization used two separate `digitalRead` calls, leaving time for a transition between the reads. `Maker_Mechbot/EncoderSampling.h` extracts both bits from one saved 32-bit value and is marked for IRAM use.

This change removes software sampling skew as one possible contributor to the invalid transitions observed during the all-wheel bench trials. It does not establish that this was the only cause or that electrical noise, missed interrupts, or wiring issues are resolved.

All current pairs occupy one input bank: FL 35/36 and FR 34/39 use the upper bank; RL 5/23 and RR 18/19 use the lower bank. The helper requires a same-bank pair. Host tests assert that invariant. The pin assignments, quadrature table, polarity, diagnostic counters, motor control, watchdog, IMU behavior, and serial protocol remain as before.

## Offline verification

- Actual firmware host test: passed with GCC 16.2.0, C++17, `-Wall -Wextra -Werror`.
- Motor safety host test: passed with the same flags.
- Navigation math host test: passed with the same flags.
- The three unmodified baseline host tests also passed.
- ESP32 Dev Module compilation: passed using Arduino ESP32 core 3.3.11, `esp32:esp32:esp32`, warnings enabled. Program storage: 359,815 bytes; global data: 27,688 bytes. The compiler reported three volatile-increment deprecation warnings on the existing encoder counter increments.

Expanded host coverage includes startup with nonzero states, all 16 state transitions on every wheel, wheel polarity, repeated states, invalid jumps and subsequent resynchronization, forward/reverse accumulation, edge totals, exactly one read of the selected GPIO bank per sample, and zero `digitalRead` calls. Existing motor/watchdog and IMU assertions are preserved.

The local GPT-OSS 20B model wrote the helper, its firmware integration and the GPIO test stubs through a bounded local file-tool workflow. A supervising agent reviewed those edits and added the independent transition tests.

## September 5 upload and motion checks

The patch was [uploaded and independently flash-verified on September 5](docs/MAKER_FIRMWARE_UPLOAD_2026-09-05.md), with the pre-upload flash backed up and NVS verified unchanged. Subsequent [manual forward/reverse observations](test_results/manual-encoder-20260905/README.md) and [four short raised-wheel powered pulses](test_results/powered-encoder-20260905/README.md) reported expected polarity and zero new invalid transitions.

Later [pair comparisons and attempted speed matching](test_results/paired-motors-20260905/README.md) did reproduce invalid transitions. The sampling change therefore has physical test evidence, but the encoder reliability issue is not closed. Front-wheel slowdown under all-wheel operation also persisted despite [equal PWM peripheral readback](test_results/pwm-readback-20260905/README.md).

Continue investigating signal integrity, wiring, interrupt behavior and the load-dependent speed difference under recorded conditions. No shared PWM trim was accepted or saved, and these short tests do not establish sustained or loaded reliability or readiness for wheel-speed control.

See `BENCH_RESULTS_2026-09-04.md` for the original observations. Detailed local-model and build evidence is saved in the Codex task folder `figure-out-why-we-couldn-t`, including `LOCAL_MODEL_FINDINGS.md` and `robot-host-validation/results.json`.
