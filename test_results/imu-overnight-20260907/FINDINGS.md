# ESP32-S3 / STRDC BNO085 controlled trials

Started September 6, 2026, evening CDT. The user authorized continued unattended trials and computer-use uploads on COM7. This directory records what was actually observed; prepared trials are not treated as completed tests.

**Result:** Sixteen main observation runs completed: one STRDC accelerometer example, eight CEVA/Adafruit trials, six STRDC driver trials, and one passive UART-RVC trial. Accelerometer and geomagnetic output work; calibrated gyro output does not. Enabling gyro also stops otherwise working accel output. A separate UART test receives the IMU startup banner but no sensor stream. This makes a fault affecting the IMU's gyro operation or its supply the leading concern, without proving physical damage or identifying its cause. Historical logs show both working gyro output and intermittent failures before the new S3 work.

## Setup

Separate ESP32-S3 board, USB-to-UART COM7, console 115200 baud. Intended connections: 3V3 to VDC, GND to GND, GPIO8 SDA, GPIO9 SCL, GPIO4 RST, GPIO5 P0, GPIO6 INT. Physical wiring cannot be inspected through the software tools. No motion, power measurement, or physical USB power-cycle can be performed while the user is absent.

Arduino IDE 2.3.10 Tools menu verified: ESP32S3 Dev Module, COM7, CDC On Boot Disabled, DFU Disabled, erase-all Disabled, PSRAM Disabled, 4MB flash, UART0/Hardware CDC upload, Hardware CDC and JTAG USB mode. No motor firmware or robot controls are involved.

## Initial vendor gyro result

Before the new trials, the Arduino Serial Monitor visibly showed:

```text
21:32:50.600 -> BNO Initialized Successfully
21:32:50.600 -> BNO Begin Feature Set
21:32:50.810 -> [DEBUG] Timed out waiting for Set Feature Command Response
21:32:50.810 -> BNO Failed to set feature report
21:32:51.061 -> [DEBUG] Timed out waiting for Set Feature Command Response
21:32:51.061 -> BNO Failed to set feature report
```

This is a manually transcribed visible UI excerpt, not a complete serial capture. It confirms startup succeeded before the feature-response retry loop. The firmware source and its limitations are documented in `../../ESP32S3_STRDC_Gyro_Test/VALIDATION.md`.

## Trial sequence

1. STRDC's actual accelerometer example with the same ESP32 adapter and wiring. Preserve vendor behavior for a comparison with its gyro example.
2. Separately labeled diagnostic using CEVA SH-2: hardware-reset each run, request a report once, explicitly read back its configuration, count fresh decoded samples, and finish after a fixed observation period. Compare accelerometer, gyro, raw gyro, geomagnetic rotation vector, combined accel/gyro, low gyro rate, and I2C clock rate. Each result must be recorded before interpreting it.

No factory reset, calibration erase, IMU firmware update, or persistent configuration changes are part of these trials. Hardware-reset recovery is distinct from a full power-cycle, and that limitation must be retained in conclusions.

## Completed: STRDC accelerometer example

The exact adapted example in `../../ESP32S3_STRDC_Accel_Test` was uploaded using Arduino IDE on COM7. The IDE reported 341,792 bytes written and successful flash hash verification (`vendor-accel-upload-ui.txt`). The example passed initialization, feature setup, and its original ME calibration setup, then continued printing changing accelerometer values through a 35-second observation after the initial boot capture.

`vendor-accel-stream.txt` contains 3,617 sample-print lines, 73 distinct XYZ triples, and 3,368 changes between adjacent sample lines. The last 100 sample lines still contain 29 distinct triples. No Failed, Timed out, or Error lines occurred. These are print counts, not exact received report counts: the vendor requests 400 Hz but prints its most recently stored reading every 10 ms. The changes demonstrate continuing data updates, without proving the requested 400 Hz rate.

## Separate host serial reset problem

Opening Arduino Serial Monitor repeatedly put this ESP32-S3 into ROM download mode (`boot:0x3`, `waiting for download`). Closing the monitor and opening COM7 with DTR and RTS explicitly false, then pulsing RTS for 100 ms while keeping DTR false, recovered normal application boot (`boot:0xb`, `SPI_FAST_FLASH_BOOT`). Reopening the IDE monitor after a successful accelerometer capture reproduced download mode. This explains the quiet application after some uploads and is separate from the gyro feature timeout after a successful IMU initialization.

Uploads continue through Arduino IDE computer use. `capture_trials.ps1` uses .NET SerialPort for the hardware serial connection, checks the firmware banner before sending commands, captures full trial transcripts, and closes COM7 when finished. The IDE Serial Monitor stays closed. `vendor-accel-reset-recovery.txt` preserves the successful reset and accelerometer startup.

## Completed: eight CEVA/Adafruit diagnostic trials

The separate `../../ESP32S3_IMU_Trials` sketch was uploaded through Arduino IDE with successful flash hash verification (`ceva-upload-ui.txt`). Each command reset and initialized the IMU, counted decoded sensor callbacks from the first Set Feature send for 35 seconds, then read back configuration and identification before disabling reports. All eight runs initialized, answered the final Product ID request, and recorded zero unexpected resets, decode errors, and SHTP events. These counters do not measure analog signal integrity or every possible bus fault.

| Trial/log | Requested mode | Fresh reports in 35 s | End-of-trial interval | Result |
|---|---|---:|---|---|
| `ceva-01-a.txt` | Accel 50 Hz, I2C 100 kHz | 2,235 | 16,000 us | Live; zero gaps/duplicates |
| `ceva-02-g.txt` | Calibrated gyro 50 Hz, 100 kHz | 0 | 0 | Read back disabled; control channel alive |
| `ceva-03-r.txt` | Raw gyro 50 Hz alone | 0 | 0 | Inconclusive: missing underlying gyro report prerequisite |
| `ceva-04-m.txt` | Geomagnetic rotation vector 20 Hz | 697 | 50,000 us | Live; zero gaps/duplicates |
| `ceva-05-l.txt` | Calibrated gyro 10 Hz | 0 | 0 | Lower requested rate did not help |
| `ceva-06-h.txt` | Calibrated gyro 50 Hz, 400 kHz | 0 | 0 | Faster I2C did not help |
| `ceva-07-b.txt` | Accel + calibrated gyro 50 Hz | Accel 5; gyro 0 | Accel 16,000 us; gyro 0 | Accel stopped at 133 ms; control channel alive |
| `ceva-08-a.txt` | Accel 50 Hz after reset | 2,235 | 16,000 us | Live again; zero gaps/duplicates |

Immediate PRE configuration readbacks were zero even on healthy controls because the enable request had only just been sent. The table uses POST readbacks after 35 seconds, with actual reports counted separately. A matching successful Get Feature response with interval zero means the sensor reported that feature disabled; it is not merely an untouched host default. The [SH-2 reference manual, sections 6.5.3–6.5.5](https://www.ceva-ip.com/wp-content/uploads/SH-2-Reference-Manual.pdf) describes Set/Get Feature and the response.

Raw-only output is not an independent failure: the [BNO08X datasheet, section 2.1.2](https://www.ceva-ip.com/wp-content/uploads/BNO080_085-Datasheet.pdf) requires separately enabling an underlying gyro report. The geomagnetic rotation vector is the useful fusion control here because it does not require gyro data.

Healthy accel controls returned four error records, including `04 00 04 01 15 00` and `04 00 04 16 0D 00`; the geomagnetic control returned two of the latter. Gyro-enabled tests captured eight repeated `04 00 04 01 15 00` records and did not receive an accepted end-of-errors marker within the diagnostic one-second limit. Repeated records with sequence zero are not proof of eight distinct faults. Module/code meanings have not been established, and module 21 also appears during healthy accel operation; these bytes alone do not prove physical damage.

Product ID responses consistently identified part 10004148 version 3.2.13 build 6; the complete four-entry identification is in every transcript.

## Completed: six STRDC driver readback trials

The separate `../../ESP32S3_STRDC_Readback_Test` diagnostic kept the STRDC module, middleware and the already tested ESP32 HAL unchanged. It made one Set Feature attempt, explicitly requested Get Feature even after timeout, observed fresh `newData` flags for 35 seconds, and requested configuration and a new Product ID at the end. The IDE upload hash passed (`strdc-upload-ui.txt`). These counts are lower bounds because bundled samples can collapse to one flag; setup-time reports are excluded.

| Trial/log | Mode | Fresh report indications | Set result | Final configuration/result |
|---|---|---:|---|---|
| `strdc-01-a.txt` | Accel 50 Hz | 2,239 | Success | 16,000 us; live |
| `strdc-02-g.txt` | Gyro 50 Hz | 0 | Response timeout | 0 us; no gyro |
| `strdc-03-m.txt` | Geomagnetic rotation vector 20 Hz | 700 | Success | 50,000 us; live |
| `strdc-04-b.txt` | Accel + gyro 50 Hz | 0 + 0 after setup | Accel success; gyro timeout | Accel 16,000 us; gyro 0; neither streaming |
| `strdc-05-h.txt` | Gyro 50 Hz, 400 kHz I2C | 0 | Response timeout | 0 us; no gyro |
| `strdc-06-a.txt` | Accel 50 Hz after reset | 2,239 | Success | 16,000 us; live again |

All six runs initialized and answered fresh final identification requests; no post-init reset was counted. Thus waiting for a missing unsolicited feature acknowledgment alone cannot explain the observed failure. The exact vendor accel example works, while two separately implemented host drivers see gyro failure and loss of accel streaming when gyro is added.

The vendor Get routine clears its response flag and sends a new request, so success is not merely a cached flag. It does, however, extract configuration numbers at fixed buffer offsets assuming the requested control response comes first in the packet; raw response bytes were not captured to exclude that packet-position limitation. The independent CEVA implementation matches the sensor ID and decodes the interval from the matching response and produced the same result. No vendor code was patched to hide or bypass the observed timeout.

## Completed: independent passive UART-RVC test

`../../ESP32S3_IMU_RVC_Test` used the existing GPIO8 SDA wire as the IMU's UART TX to S3 RX, with P0 high across reset selecting RVC and no host UART transmitter attached. The STRDC schematic and CEVA datasheet were checked for this mapping; the STRDC web pin table has conflicting RX/TX wording, documented in the sketch README. The IDE upload passed hash verification (`rvc-upload-ui.txt`).

`rvc-stream.txt` records exactly 78 received bytes, all at 164–168 ms after reset release:

```text
%Hillcrest Labs 10004148
%SW Ver 3.2.13
%(c) 2016 Hillcrest Laboratories, Inc.
```

No additional UART bytes and no checksum-valid 19-byte sensor frames arrived through 35,000 ms. The readable manufacturer/version banner supports successful UART reception and startup; an error in the binary packet parser alone cannot explain the absence of any later raw bytes. RVC supplies orientation/acceleration rather than an explicit gyro-rate report, so this result is a separate failure of expected sensor streaming, not a direct measurement of the gyro element.

The firmware then asserted reset, detached UART, restored P0 low, released reset, and observed I2C ACK at 0x4A (0x4B did not ACK). This independently corroborates interface restoration. No physical rewiring was performed.

## Historical evidence

The IMU previously emitted live gyro data. `../maker-encoder-diagnostic-20260905-s1s7fr_s/serial.jsonl` spans a healthy segment on September 4, 20:15:54–20:16:18 CDT, with 121 numeric IMU frames, 90 distinct quaternions, and 45 frames containing nonzero gyro values. Intermittent OFFLINE/WAIT behavior was already recorded in `../../DEPLOYMENT.md` and `../powered-encoder-20260905/serial.jsonl`, before the new S3 work.

The earlier `../imu-bus-diagnostic-20260906/FINDINGS.md` also recorded gyro reports stopping after a handful of samples while accel/geomagnetic controls worked. This establishes prior operation and pre-existing failures. It does not establish whether physical damage occurred, when, or why.

## Remaining physical distinction

Changing SDA/SCL mapping cannot explain the successful accel/geomagnetic communication in these runs; the tested software mapping is SDA8/SCL9. Both I2C implementations and the passive UART result make a single host-driver bug an insufficient explanation for all observations. The chip is not completely dead: startup, identification, accel and geomagnetic output still function.

Software tests here cannot distinguish an internal gyro problem, IMU firmware/calibration state, or a supply problem. The smallest useful next hardware comparison is a known-good BNO085 breakout on the same S3, wiring, power and diagnostic, running accel then calibrated gyro. If a spare is unavailable, measure the breakout's regulated supply during gyro startup with an oscilloscope; a steady multimeter reading does not exclude a brief dip. A complete power-cycle and physical wiring inspection also remain distinct from the resets performed here.

## State left for the next session

**Later September7 follow-up:** the user supplied a successful 35-second UART-RVC log using the Maker ESP32 Pro, intended5V IMU supply and a3.3V-powered SN74LVC245AN buffer:3493 valid sensor frames with no reported checksum/index/UART errors. See [the later trial](../imu-maker-rvc-5v-20260907/FINDINGS.md). The state described below is the end of the overnight S3 trials, not the later Maker bench setup. This recovery does not isolate supply voltage as the sole cause.

The reusable `ESP32S3_IMU_Trials` diagnostic was restored through Arduino IDE on COM7 and flash hash verification passed (`final-idle-upload-ui.txt`). A final serial capture confirmed normal SPI flash boot and the diagnostic help banner, ending with `idle until a trial command` (`final-idle-stream.txt`). No new trial command was sent. The capture helper exited successfully and closed COM7; Arduino Serial Monitors remain closed. The IMU had already acknowledged I2C restoration after the RVC trial.

The requested STRDC examples remain preserved in `../../ESP32S3_STRDC_Accel_Test` and `../../ESP32S3_STRDC_Gyro_Test`. The current flashed diagnostic is separately labeled custom. To repeat a finite accel/gyro comparison from this project directory, with the IDE monitor closed:

```powershell
& '.\test_results\imu-overnight-20260907\capture_trials.ps1' -Label next-ag -Protocol ceva -Commands 'ag'
```

Use a new `-Label` to preserve existing transcripts; the helper writes files under its own evidence directory. No further unattended work, open serial capture, or automatic trial loop remains running.
