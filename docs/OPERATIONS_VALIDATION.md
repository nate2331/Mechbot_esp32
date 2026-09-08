# Operations software validation

Subsequent live installation: [September 5 Pi deployment and stationary verification](PI_OPERATIONS_DEPLOYMENT_2026-09-05.md).
The results below describe the original software release checkpoint.

Completed during the September 4 overnight development session on this PC.
The release adds live wheel and IMU observations, encoder diagnostics, bounded
capture, offline replay, real bench-report summaries/comparison, measured-geometry
passive odometry, an offline CLI, and a responsive Operations console. The
existing PWM tuning page remains available at `/tuning`.

## Results

| Check | Result |
| --- | --- |
| Complete Python discovery suite, Python 3.12.14 with pySerial 3.5 | 222 tests run: 221 passed, one Windows symlink privilege fixture skipped; 22.703 seconds |
| Node 24.19.0 UI, replay and chart tests | 22 passed |
| Syntax checks on every Operations JavaScript file | Passed |
| Local coding runner, durable queue and guarded source-delivery utility | 76 tests run: 74 passed, two Windows privilege fixtures skipped |
| Real Edge browser, Maker simulator | Desktop 1440 x 1100 and mobile 390 x 844 passed; zero JavaScript errors or page overflow |
| Real Edge browser, legacy S3 simulator | Rear ticks/s, unsupported front encoders and disabled geometry verified; all four mobile views without overflow; zero JavaScript errors or action requests |
| Browser workflows | Measured geometry, capture/stop, replay/scrub, return to live, actual bench report comparison, all four mobile pages, retained tuning page |
| Hardware-independent encoder sampling validation, earlier in this session | All six baseline/staged C++ compilations and executions passed with warnings as errors |
| Maker firmware build, earlier in this session | ESP32 core 3.3.11: 359,815 bytes program, 27,688 bytes globals; three existing volatile increment warnings |

The firmware and its host-test inputs were unchanged during the Operations work.
The earlier build remains applicable; no upload occurred. Python test messages
about uploading reference mocked `/dev/serial/by-id/test-maker` fixtures.

The integration checks inject recorder failures while exercising fake serial
telemetry and both stop commands. They cover blocked disk preparation/writes,
capture activation concurrent with READY, unavailable storage at bridge startup,
bounded shutdown, and calibration restoration when recording starts before the
controller identifies itself. Recording errors remain explicit and do not interrupt the
legacy telemetry parser or motor stop sequence. Offline replay has no transport
reference and never dispatches historical TX commands.

Numerical tests cover both controller profiles, device-clock wrap/reset,
malformed/nonfinite values, stale data and gaps, wheel directions, analytic
forward/strafe/rotation, and invalidation of passive pose. No real wheel geometry
was guessed. Values entered by browser QA apply only to its clearly marked
simulation data directory outside the source checkout.

## Local model contribution

The repaired `gpt-oss-local-tools:latest` model generated 53,537 tokens across
36 coding attempts and 170 native Ollama responses on this PC. The transcripts
record 598,211 prompt tokens, including repeated context. These figures exclude
the earlier model diagnosis and encoder patch. Jobs ran sequentially on the GPU
with bounded file access and no shell or hardware tool.

The local model produced drafts; independent review corrected API, concurrency
and numerical mistakes. A finished model response did not count as a verified
task. The reusable runner and durable queue are in the parent task workspace.
Old queue prompts are obsolete after review and must not be resumed against
these files.

## Delivery and remaining work

The isolated branch is `overnight/operations-console`. Its baseline is
`9006700ca0fd26872a23334dcbb844509ef396c9`, which preserves the original project
snapshot and initial encoder patch. Delivery checks every original file against
that captured snapshot, backs up replaced files, and verifies SHA-256 hashes
after copying. It does not commit or discard the original checkout's existing
work. The parent workspace contains `robot-delivery-manifest.json`, the archive,
test logs and `browser-qa/` screenshots; the manifest records the actual delivery
outcome and source commit.

No Pi deployment, firmware upload, physical movement, encoder fault clearance,
closed-loop speed acceptance or autonomous navigation acceptance is claimed.
The next supervised work is the encoder recheck, reverse/start/hold measurements,
IMU reliability testing and loaded geometry measurements described in
[ROBOT_ROADMAP.md](ROBOT_ROADMAP.md).
