# STRDC configuration readback diagnostic

This is a separate diagnostic harness, not an unchanged STRDC example. The
bundled BNO08X module, middleware, and ESP32 HAL are copied without changes
from `ESP32S3_STRDC_Gyro_Test`. Upstream is https://github.com/STRDC/strdc-sdk
at `2582838df20642f1d2568244daa56e1be1206bf0`. The vendor MIT license remains
in the sources and `data/upstream/LICENSE`; hashes are in `SOURCE_MANIFEST.json`.
The existing exact gyro and accelerometer examples are not modified.

Same wiring: 3V3 to VDC, GND to GND, GPIO8 to SDA, GPIO9 to SCL, GPIO4 to RST,
GPIO5 to P0, GPIO6 to INT. P1/DI retain their LOW defaults and BT its HIGH
default. Use the separate ESP32-S3 and COM7 USB/UART bridge, Serial Monitor
115200, USB CDC On Boot Disabled. No motors are involved.

## Commands

The board idles after boot until one literal command character is received.
Send one command at a time; input queued during an active trial is discarded
when that trial finishes. No newline is required.

| Command | Requested reports | I2C |
|---|---|---|
| `a` | Accelerometer 50 Hz | 100 kHz |
| `g` | Calibrated gyro 50 Hz | 100 kHz |
| `r` | Raw gyro 50 Hz | 100 kHz |
| `m` | Geomagnetic rotation vector 20 Hz | 100 kHz |
| `b` | Accelerometer + calibrated gyro, both 50 Hz | 100 kHz |
| `h` | Calibrated gyro 50 Hz | 400 kHz |
| `?` | Print help; no IMU action | — |

Raw-only `r` is inconclusive for gyro health: BNO08X datasheet section 2.1.2
requires a separately enabled underlying gyro report. It is retained for
reproducibility, not treated as independent evidence of a failed gyro.

On this COM7 setup the IDE Serial Monitor was observed to force ESP32 ROM
download mode. Keep it closed during capture and use the explicit DTR/RTS helper
`../test_results/imu-overnight-20260907/capture_trials.ps1` with `-Protocol strdc`.

Each trial clears the host object, reopens I2C at the specified rate, and calls
vendor `bno08x_init` once. This normally performs the wired hardware reset;
an earlier initialization failure ends the trial without retries. Each target
report gets exactly one `bno08x_feature_set` call, then an explicit
`bno08x_feature_get` regardless of the Set result. The vendor calls retain
their own bounded waits. Get result and returned interval/batch/configuration
are printed; the known incorrect upstream flags extraction is not used.

The harness then polls the existing `bno08x_get_messages` for 35 seconds and
prints counts, changes, and age once per second. It attaches no interrupt
handler. A count increments only when `reports[id].newData` is true and clears
that flag immediately. Counts are **lower bounds**, because multiple reports
inside one packet collapse to one vendor flag. They distinguish continued
fresh parsed reports from no reports; they are not exact sample-rate measures.
Reports parsed during setup/readback are excluded from observation counts.

After the summary, one bounded configuration readback per target and one
Product ID probe check whether the control channel still responds. The latter
uses the same six-byte request as the vendor's private `bno08x_get_ID`, one
write through its I2C middleware, and its unchanged parser with a 250 ms wait.
It does not modify vendor code or infer success from a previously cached ID.

No calibration, DCD save, FRS write, tare, sensor-disable command, or persistent
write is issued. After the finite trial, the host idles; the IMU may remain
configured and producing reports until the next reset. No automatic retry or
reinitialization is performed. A hardware reset is not a power cycle, and this
ESP32 port/transport can still influence results.
