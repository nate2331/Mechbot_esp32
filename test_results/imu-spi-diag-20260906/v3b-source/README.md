# Temporary SPI packet diagnostic

Identity: `MAKER_SPI_V3B_CONTROL_PROBE`. This isolated sketch is for IMU diagnosis;
all velocity commands stop the motors and are rejected. It initializes the IMU
once at boot. Missing/stale reports and reset notifications do not trigger
automatic retries or report reconfiguration. `IMU RETRY` explicitly starts a
new initialization and packet capture.

The V2 SPI transport is unchanged apart from an in-memory capture of the first
48 non-null RX or TX packets, with timestamp, declared length and up to 32 raw
bytes. `IMU TRACE` prints those records without communicating with the IMU.
Null RX headers are counted in `IMU DIAG` but omitted from the capture. Invalid
headers record only four bytes. Overflow retains the first 48 records. Failed
initialization retains its trace until another initialization is requested.

Pins: SCK22, MISO21, MOSI32, CS33, INT26, RST25, P0/WAKE16. P1 to 3.3 V;
VDC to 3.3 V and shared GND. Signal pins on the Maker servo headers must be used.
SPI is 1 MHz, mode 3. Wiring changes require power off.

Open `Maker_Mechbot_SPI_Diag.ino` in Arduino IDE, select ESP32 Dev Module and
the connected board's port. Serial Monitor: 115200 baud, New Line.
Useful commands: `?`, `X`, `DIAG`, `IMU DIAG`, `IMU TRACE`, `IMU RETRY`.

`IMU PROBE` sends raw six-byte Get Feature (FE08) and Product ID (F900)
requests, servicing incoming packets for500 ms after each. Each send uses one
bounded HAL write attempt; no reset or report-enable command is sent. The
result says whether each request was sent, then prints counters and the trace.
Responses, not the send return value, establish whether the IMU answered.
These diagnostic queries advance the wire sequence separately from SH-2's
private counter. Use `IMU RETRY` before any subsequent SH-2 control operation.
The sketch has no automatic control operations after startup; ordinary sensor
polling and repeated raw probes remain available.

The packet capture is bounded. The inherited Adafruit/SH-2 product-ID startup
operation can still wait indefinitely after a partial startup; if that happens,
the serial command loop is not reached. A successful compile is not evidence
that sensor reports work. Hardware results are recorded separately under
`test_results/imu-spi-diag-20260906/`.
