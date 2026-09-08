# Integrated Maker RVC firmware

Current firmware is V2 with explicit counterclockwise-positive heading and IR2
telemetry. See [heading convention correction](RVC_HEADING_CONVENTION.md).
The V1 contract below describes the first deployment and historical captures.

September 8, 2026: installed and flashed with motor power confirmed off. See
[verified deployment](../test_results/rvc-deployment-20260908/README.md) for
image/NVS verification and stopped telemetry checks. The Pi's earlier
`/home/nate/mechbot-rvc-stage.Yck0llDu` contains the passive diagnostic update,
not this newer matched integration. Current release: `/home/nate/rvc-release-sneyh6cn`.

## Select the correct build

`Maker_Mechbot` defaults to the existing SPI variant. For the established buffered
5 V RVC circuit, compile with `--build-property compiler.cpp.extra_flags=-DMAKER_IMU_RVC=1`
and FQBN `esp32:esp32:esp32:FlashMode=dio`. Keep its build/output directory separate
from SPI. The matched Pi updater selects this option only for the exact integrated
RVC READY identity or distinct Maker RVC mapping help line. Standalone diagnostic
firmware remains unknown to the automatic updater; it cannot authorize a first flash.

The receive-only adapter uses UART1 at 115200 baud on GPIO21, TX disabled. It does
not drive sensor reset, mode or power pins. This describes the existing buffered
circuit, not authorization to connect a 5 V logic signal directly to an ESP32 pin.
Integrated motor defaults remain 20 kHz / duty 177, unlike the separate movement
experiment's 248 Hz / duty 150. Confirm the intended experiment before uploading.

## Qualification and controls

Five consecutive checksum-valid frames with no index gap and no interval over
100 ms qualify the stream. Staleness, checksum/index/UART errors, a polling stall
or excessive queue backlog revoke heading acceptance. Draining is bounded to
1024 bytes per loop so serial recovery cannot monopolize the motor command loop.

Startup forces heading correction off in RAM. Qualification only establishes
communication health. RVC provides no calibration status or separate gyro rate.
After measuring mounting, yaw sign, known-angle return and drift while stopped,
`IMU ACCEPT` enables heading eligibility for this session only. It stops first.
`IMU REVOKE` stops and removes that eligibility. Restart/retry/fault requires a
new acceptance; nothing is saved automatically.

`F 1` captures the current field reference; `Z` replaces it. Both require accepted,
fresh heading. A lost field reference stops and latches motion. Fresh data and
`IMU ACCEPT` alone do not clear that latch: explicitly choose `F 0` for robot frame
or fresh accepted `F 1` for field frame. Heading-hold settings still require an
explicit `CFG SET heading-enabled 1`. Physical correction sign is not yet verified.

## Telemetry contract

Identity: `READY ESP32_MAKER_MECANUM_RVC_V1`.

```text
IR1 ms age_ms state yaw_deg pitch_deg roll_deg ax_mg ay_mg az_mg accepted bad gaps uart_errors
```

State is OFFLINE, SYNCING, READY or STALE; age is -1 until a sample exists.
Acceleration is raw mg, not gravity-compensated linear acceleration. Legacy `I`
remains unchanged for SPI. Matched Pi parsing exposes transport `uart-rvc`, radians
for yaw consumers, raw mg separately, and null unavailable quaternion/gyro/status.
Dashboard session acceptance is not a whole-robot readiness or accuracy assertion.

## Verification and remaining hardware work

Both target builds pass: RVC 328939 flash / 23400 global RAM bytes; SPI 366987 /
31920. Integrated actual-sketch host tests pass normal, UART failure and wrong-RX
scenarios; existing SPI firmware/navigation host suites pass. Python: 241 tests,
240 passed and one platform skip. JavaScript: 39 passed. Mocked Edge browser checks
pass at 1440 and 390 pixels, including stale/offline clearing and no runtime errors.
Evidence: `test_results/imu-rvc-integrated-20260908`.

Backup, deployment, fresh identity and zero-output verification are complete.
Next record repeatable sensor startup and known-reference measurements. Only after
those pass, perform supervised low-power heading/field trials and log correction
sign, drift, oscillation and fault-stop behavior. No such physical result is claimed.
