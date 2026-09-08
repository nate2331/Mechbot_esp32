# ESP32-S3 finite IMU trials

This is a **custom diagnostic**, separate from `ESP32S3_STRDC_Gyro_Test` and the
unaltered STRDC examples. It uses the installed Adafruit BNO08x 1.2.7 / CEVA SH-2
implementation, bundled locally under `src/adafruit_bno08x`. Shared Arduino
libraries and earlier sketches are unchanged.

## Wiring and upload

| ESP32-S3 GPIO / power | STRDC breakout |
| --- | --- |
| 3V3 | VDC |
| GND | GND |
| 8 | SDA |
| 9 | SCL |
| 4 | RST |
| 5 | P0 |
| 6 | INT |

Leave P1, DI and BT in their breakout defaults. P0 is driven low throughout
these I2C trials. INT is observed as an input; the library polls I2C.
Use `ESP32S3 Dev Module`, USB CDC On Boot **Disabled**, USB Mode **Hardware CDC
and JTAG**, COM7 and Serial Monitor 115200 for the current USB-UART connection.
The firmware waits for a serial command after boot.

On this particular COM7 setup, opening Arduino Serial Monitor was observed to
put the S3 into ROM download mode. The tested capture helper is
`../test_results/imu-overnight-20260907/capture_trials.ps1`; it explicitly controls
DTR/RTS. Keep the IDE monitor closed when using it.

## Commands

| Command | Reports requested | I2C clock |
| --- | --- | --- |
| `a` | Accelerometer 50 Hz | 100 kHz |
| `g` | Calibrated gyro 50 Hz, alone | 100 kHz |
| `r` | Raw gyro 50 Hz, alone | 100 kHz |
| `m` | Geomagnetic rotation vector 20 Hz | 100 kHz |
| `b` | Accelerometer 50 Hz + calibrated gyro 50 Hz | 100 kHz |
| `l` | Calibrated gyro 10 Hz, alone | 100 kHz |
| `h` | Calibrated gyro 50 Hz, alone | 400 kHz |
| `s` | Print live or last completed sample-count snapshot | no new trial |
| `x` | End the running trial and collect readbacks | no new trial |
| `?` | Help | no new trial |

The raw-only `r` command is **not an independent gyro-health test**. The
[BNO08X datasheet, section 2.1.2](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf)
requires an underlying gyro report to be enabled separately. No raw reports in
this raw-only trial can be expected even on healthy hardware.

Each trial closes the old host session, drives P0 low, asserts RST low for 20 ms,
restarts I2C, releases RST and waits 400 ms. Adafruit's normal I2C open also sends
its software reset. The log distinguishes the commanded GPIO pulse from actual
SH-2 reset notifications and a successful product-ID transaction. GPIO readback
does not prove the remote RST wire is connected. This is **not a power cycle**.

The observation window starts immediately before the first Set Feature request,
lasts 35 seconds, and includes explicit Get Feature configuration readbacks.
Every decoded sensor report is counted by a callback; no cached-only report is
counted. Once-per-second lines show counts, new reports, last-report age,
sequence gaps and repeated sequence numbers. `FINAL` includes first/last sample
arrival times and last values. The driver-provided sensor timestamp is not used
for freshness; host `millis()` at each callback is used.

At the end, sample counters freeze. The sketch reads configuration, product ID
and up to eight error records, sends report-disable configurations and closes
the host session. It does not retry a trial automatically. Unexpected reset
notifications end measurement immediately. An idle `s` shows the frozen sample
snapshot without talking to the IMU; the POST lines remain in monitor history.

`SET rc=0` means a successful command send. Actual configuration requires a
successful matching `CFG` readback. Fresh sample counts are a separate check.
An immediate PRE readback may still show zero while enablement is pending;
the healthy accelerometer did this before streaming and its POST readback was
nonzero. Interpret the end-of-trial POST configuration with the report counts.
Actual reporting frequency may differ from the requested rate. Repeated numeric
values on a stationary board are normal; increasing callback counts demonstrate
new reports. A successful trial must continue receiving fresh reports at its end.

## Diagnostic-only library changes

The bundled source and license notices came from the installed Adafruit BNO08x
1.2.7. `UPSTREAM_SHA256.csv` identifies those original bytes. Only two library
source changes are made:

1. In `sh2.c`, `opProcess` substitutes a 1,000,000 us timeout when an operation's
   timeout is zero. Upstream treats zero as unlimited; product/config/error
   readbacks could otherwise hang forever after a sensor fault.
2. In `Adafruit_BNO08x.cpp`, a failed synchronous I2C write returns `SH2_ERR_IO`
   instead of zero. Upstream SHTP retries a zero return indefinitely inside the
   send operation, before the operation timeout can take effect.

The sketch subclasses the library's virtual initializer to register its own
reset and sensor callbacks. The explicit RST pulse replaces the library's
optional GPIO-reset call; Adafruit's I2C transport and SH-2 packet parsing remain
in use. No call writes FRS, persists calibration, clears calibration, changes
firmware, enters DFU, or controls motors.

Reference implementations: [Adafruit BNO08x](https://github.com/adafruit/Adafruit_BNO08x),
[CEVA SH-2 manual](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf).
