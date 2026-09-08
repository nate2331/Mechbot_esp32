# Mechbot / Blackbot Two

Mecanum robot firmware, Raspberry Pi gamepad/serial bridge, and browser
Operations and tuning consoles. The development controller is the **NULLLAB
Maker ESP32 Pro with four encoders**. The ESP32-S3 robot implementation remains
a separate legacy target; S3 IMU experiments have their own sketches.

**Weekend checkpoint, September 4–7, 2026:** start with the
[weekend development record](docs/WEEKEND_PROGRESS_2026-09-04_TO_07.md) for the
complete chronology, measured results, source map, and unfinished hardware tests.
Documentation consolidated September 8 from saved logs and prior work; this
publication does not flash the robot or establish its current live state.

## Where we ended the weekend

| Area | Latest recorded result | Evidence |
| --- | --- | --- |
| Pi Operations | Installed September 5: live diagnostics, recording/replay, bench evidence, passive odometry, and mobile UI; tuning remains available | [Deployment](docs/PI_OPERATIONS_DEPLOYMENT_2026-09-05.md), [runbook](docs/OPERATIONS_RUNBOOK.md) |
| Encoders | Coherent A/B sampling uploaded; manual checks and short powered trials passed, but invalid transitions recurred in pair/matching tests | [Sampling change](ENCODER_SAMPLING_FIX.md), [pair trials](test_results/paired-motors-20260905/README.md) |
| PWM | Equal LEDC register settings did not produce equal wheel speed at 20 kHz. Later sweeps support a broad useful 200–300 Hz region; 248 Hz is a working trial setting, not a proven unique optimum | [Readback](test_results/pwm-readback-20260905/README.md), [frequency research and raw data](test_results/pwm-frequency-20260905-06/README.md) |
| IMU bench baseline | Maker + 5 V STRDC IMU + 3.3 V SN74LVC245AN buffer + UART-RVC: 227,501 valid frames over nearly 38 minutes, zero reported checksum/UART/index/repeat/pause errors | [Continuous test and logs](test_results/imu-maker-rvc-continuous-20260907/FINDINGS.md) |
| Next movement experiment | Separate RVC movement sketch compiled and passed seven host scenarios; physical upload and movement trial remain pending | [Movement test](Maker_IMU_RVC_Movement_Test/README.md), [build status](Maker_IMU_RVC_Movement_Test/BUILD_STATUS.md) |

The successful IMU stream is a **standalone bench diagnostic**. The integrated
`Maker_Mechbot` source still uses the experimental SPI adapter; its September 6
upload did not establish reliable sensor reports. See the [SPI hardware result](test_results/imu-spi-v2-20260906/PC_UPLOAD_RESULT.md).
Use the [continuous RVC instructions](Maker_IMU_RVC_Continuous_Test/README.md)
and [buffer wiring](Maker_IMU_RVC_5V_Test/WIRING.md) to reproduce the later working
bench setup. RVC supplies orientation and acceleration, without separate gyro
rate fields. Perfboard assembly, proposed transistor reset recovery, motor-noise tolerance, angle
accuracy, and integration into the robot firmware remain unverified.

Motor control is still open-loop PWM. No accepted shared trim or continuous
wheel-speed PID was delivered. Both Pi senders use full-scale cardinal gamepad
commands with a left-bumper deadman; proportional input remains deferred.
Maker integrated-source defaults are PWM 177 per wheel, heading correction off,
and field mode off; saved NVS can override defaults. The separate movement
experiment uses 248 Hz and logical duty 150. Do not interchange these baselines
or apply the older S3 PWM trims to Maker.

## Start here

1. [Weekend record and remaining work](docs/WEEKEND_PROGRESS_2026-09-04_TO_07.md)
2. [Project roadmap](PROJECT_ROADMAP.md) and [robot acceptance gates](docs/ROBOT_ROADMAP.md)
3. [Hardware history](HARDWARE_BASELINE.md) and [Maker integrated firmware](Maker_Mechbot/README.md)
4. [Operations runbook](docs/OPERATIONS_RUNBOOK.md) and [passive encoder observations](docs/ENCODER_OBSERVATION.md)
5. [PWM tuning](PWM_TUNING_GUIDE.md), [automatic bench tuner](MAKER_AUTO_TUNING.md), and [September 4 bench results](BENCH_RESULTS_2026-09-04.md)
6. [Gamepad integration](GAMEPAD_INTEGRATION.md), [navigation controls](NAVIGATION_CONTROLS.md), and [Pi deployment](DEPLOYMENT.md)

The Pi installation serves Operations at `/` and tuning at `/tuning` on port
8765. The historical deployment copied `mechbot_bridge.py` over
`/home/nate/pi_mecanum_gamepad.py`; inspect `mecanum-gamepad.service` before
changing its entry point. The repository's standalone `pi_mecanum_gamepad.py`
is a different sender without the dashboard. Run only one serial owner.

Preview locally with `python tests/dashboard_demo_server.py --port 8876`, then
open `http://127.0.0.1:8876/`. Preview data is explicitly simulated.

## Repository map

| Path | Role |
| --- | --- |
| `Maker_Mechbot/` | Integrated Maker firmware, encoder sampler, experimental SPI IMU adapter, independent motor-output watchdog |
| `Maker_M0_Test/`, `mechbot_4encoder_test/` | Supervised individual-wheel and encoder diagnostics; historical duplicate retained |
| `Maker_IMU_Test/`, `Maker_Mechbot_SPI_Diag/`, `Maker_IMU_Error_Check/`, `Maker_IMU_Bus_Diagnostic/` | Isolated and instrumented IMU investigations |
| `Maker_IMU_UART_RVC_Check/`, `Maker_IMU_RVC_5V_Test/`, `Maker_IMU_RVC_Continuous_Test/` | RVC investigation, buffered 35-second test, and successful continuous bench receiver |
| `Maker_IMU_RVC_Movement_Test/` | Prepared supervised motor/IMU experiment; hardware trial pending |
| `ESP32S3_IMU_*/`, `ESP32S3_STRDC_*/` | Separate S3 diagnostics and documented vendor-library adaptations |
| `Flipper_IMU_RVC_Test/` | Paused, uncompiled Flipper app draft |
| `Mechbot_IMU_ESP32/`, `Mechbot_esp32.ino` | Legacy S3 robot firmware; IMU and original non-IMU variants |
| `mechbot_bridge.py`, `mechbot_*.py`, `dashboard/` | Pi service, Operations modules, maintenance CLI, and browser consoles |
| `maker_auto_tune.py`, `tests/` | Bench calibration tooling, host tests, simulator, and supervised physical trial scripts |
| `test_results/` | Dated raw observations, analysis, build records, and PWM research archive |
| `firmware/` | Historical upload notes, manifests, and source snapshots; see its [archive policy](firmware/README.md) |

Related navigation work in the separate local `Mecha` project reached TASK-001
through TASK-030 at offline scope. It has not been integrated here or validated
as autonomous robot behavior.

## Software checks

Run from the repository root:

```sh
python3 -m unittest discover -s tests -p 'test_*.py' -v
node --check dashboard/app.js
node --test tests/test_ops_charts.cjs tests/ops_ui_test.cjs tests/test_ops_connection.cjs tests/test_encoder_window.cjs
c++ -std=c++17 -Wall -Wextra -Werror tests/navigation_math_test.cpp -o /tmp/navigation-test
c++ -std=c++17 -Wall -Wextra -Werror tests/maker_motor_safety_test.cpp -o /tmp/motor-test
c++ -std=c++17 -Wall -Wextra -Werror -Itests/maker_host_stubs tests/maker_firmware_host_test.cpp -o /tmp/firmware-test
/tmp/navigation-test
/tmp/motor-test
/tmp/firmware-test
```

Python tests use simulated serial IO; C++ tests use fake IO and scheduling.
The September 8 publication check ran 230 Python tests (229 passed, one Windows
privilege skip) and all 37 Node tests passed. Earlier target builds and physical
observations retain their original dates in the linked records.
`tests/maker_pi_speed_trial.py` is a physical motor trial outside test discovery.

The browser check `tests/browser_operations_qa.cjs` requires Playwright, Edge
(or `BROWSER_CHANNEL`), and the local demo on port 8876. It refuses a non-local or
non-simulated server. `PLAYWRIGHT_MODULE`, `QA_BASE_URL`, and `QA_OUTPUT_DIR` select
the installed module, preview URL, and screenshot directory. The separate
[movement host check](Maker_IMU_RVC_Movement_Test/BUILD_STATUS.md) requires Zig.
