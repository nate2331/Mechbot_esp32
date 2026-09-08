# Buffered UART-RVC observation support — September 8, 2026

Notion workstreams: task 02 (sensor confidence) and task 07 (diagnostics).
The running continuous diagnostic already supplies healthy raw data over the Pi
serial connection. This change makes those observations usable in Operations
and recorded replay. It does not change or flash the ESP32 firmware.

## Behavior

- Exact continuous/movement V1 diagnostic banners produce a descriptive sensor
  identity. Their `READY requires ...` help sentence is not a firmware identity.
- These identities retain an unknown **robot** profile, no encoder calibration,
  no tuning/update target, and no gamepad motor enable. A diagnostic banner is
  not evidence of an integrated robot protocol.
- `observed.rvc` in `/api/operations` contains parsed RUN, VALUE, COUNTS and
  UART_EVENTS reports. Existing `observed.imu` remains reserved for the integrated
  quaternion/gyro/linear-acceleration protocol.
- Yaw/pitch/roll remain degrees and acceleration remains the diagnostic's raw mg.
  Gyro availability is false, calibration status is null, and robot-control
  readiness is false. No quaternion, gyro or linear acceleration is invented.
- Each report has independent host freshness (1.5 seconds). A usable reading
  requires qualified READY, a device-reported frame age at most 500 ms, and a
  VALUE received within 0.5 seconds after that RUN. New RUN records invalidate
  previous VALUE records. This is freshness of a periodic report, not a guarantee
  of the sensor's instantaneous state between reports.
- Duplicate clocks/nonadvancing frame totals invalidate readings. Device restarts
  clear earlier counter/value records; uint32 clock wrap is supported. An explicit
  STALE event invalidates immediately; disconnect clears the observation.
- Communication errors remain visible independently of reading validity. A fresh
  reading is not an error-free endurance or heading-accuracy verdict.
- The Operations IMU card shows RVC yaw, frame rate and checksum/index/UART counts.
  Missing/stale counters display unavailable. The same parser handles replay;
  recorded TX commands are never sent back to hardware.

## Validation

- Windows: 238 Python tests, 237 passed and one Windows privilege skip.
- Pi isolated stage: all 238 Python tests passed; all 46 staged file hashes
  matched the local manifest. The initial stage omitted three existing evidence
  fixtures; adding those fixtures resolved seven file-not-found test errors.
- Node: all 38 tests passed, including diagnostic display, stale/offline clearing
  and disabled geometry controls.
- Edge headless browser: desktop 1440 px and phone 390 px, no horizontal overflow,
  no runtime errors, stale/offline display checked. All API responses were mocked;
  this was not a robot test.
- Eight new Python cases cover real-format RVC records, malformed values, lost
  reports, explicit STALE, reset/wrap/duplicate frames, error counters, identity
  rejection for tuning/flashing, and actual Operations replay isolation.

Reproduce browser checks with a local static server:

```powershell
python -m http.server 8877 --bind 127.0.0.1 --directory dashboard
# In a second terminal; set PLAYWRIGHT_MODULE if not on the normal module path.
node tests/browser_rvc_qa.cjs
```

Screenshots and browser results: `browser-qa-output/rvc/` (local build output).
The earlier live observation evidence is in
`test_results/imu-pi-passive-20260908/`; overlapping HTTP snapshots are not a
complete UART log.

## Deployment and remaining roadmap gates

Prepared Pi stage: `/home/nate/mechbot-rvc-stage.Yck0llDu`. Its manifest records
the matched Python modules, tests and dashboard assets. The staged
`pi_mecanum_gamepad.py` is the repository's standalone sender, for tests only;
deployment must copy `mechbot_bridge.py` to the existing service entry point.
Follow the matched backup/install procedure in DEPLOYMENT.md. Staging does not
activate this change or restart the bridge.

Before integrated firmware work, define how RVC orientation enters heading
control without inventing calibration confidence, gyro rates or gravity-removed
acceleration. Preserve disabled heading/field modes until known-angle direction,
startup repeatability and motor-noise tests pass. The old SPI firmware must not
be uploaded onto the current buffered UART wiring.
