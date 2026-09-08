# Preparation validation

- Final compile **PASS** with Arduino CLI1.5.1 and ESP32 core3.3.11.
- FQBN `esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default`.
- Program328,370 bytes; globals23,776 bytes.
- Output `C:/Users/nate2/AppData/Local/Temp/codex-imu-rvc-build-20260907`.
- Sketch SHA256 `F584514DC663D47290376437BF9F0FB900AD3E31743914F4E14CFF0371620F48`.
- Parser SHA256 `0C7777A9E1C7F60766E5340DF53C5A327CC0C68BD102EF74D7FE098B61ACBFD8`,
  identical to the earlier local Maker UART-RVC parser.
- Re-ran the existing parser test executable successfully: official example,
  signed extremes, split input, index wrap/discontinuities, noise/checksum
  rejection, truncated/overlapping headers, embeddedAA bytes and reserved-byte
  checksum coverage. This executable was not rebuilt as part of this preparation.

Source review checked that UART1 has explicit RX8/TX=-1, GPIO9 remains input in
RVC mode, the counters and receiver start before RST release, and the trial ends
after35seconds with a P0-low reset and plain I2C ACK checks. Only Serial0 sends
diagnostic text; no `Serial1.write` or equivalent is present.

Visually inspected the complete STRDC R1 schematic, including U1 pin20 routing
through Q1, Y1 crystal/CLKSEL0 ground, and PS0/PS1/BOOT strap resistors. Reviewed
CEVA BNO08X v1.17 interface selection, pin mapping, RVC frame format and startup
banner documentation. See README for source links and the web-table discrepancy.

No serial port, UI, or hardware upload was used by the preparing subagent.
The root task subsequently performed the hardware test described below.

## Hardware result: startup banner, no sensor frames

The root task uploaded the IDE-built application through Arduino IDE to COM7,
with [upload hash verification](../test_results/imu-overnight-20260907/rvc-upload-ui.txt)
reported successful (328,768 application bytes).
The source SHA256 remains the preparation value above. Controlled serial capture
used DTR disabled and an RTS reset pulse, with Arduino Serial Monitor closed.

Full evidence: [RVC capture](../test_results/imu-overnight-20260907/rvc-stream.txt).

- Exactly 78 bytes reached the sketch, arriving 164-168 ms after RST release.
- The bytes decode as the Hillcrest startup banner, product10004148,
  software3.2.13.
- At35,000 ms: bytes78, valid frames0, checksum failures0, and last-byte age34,832 ms.
- No subsequent UART bytes reached the receive loop during the remaining trial.
- P0 was returned low and RST pulsed. The subsequent I2C scan returned
  `0x4A rc=0` and `0x4B rc=2`, corroborating return to the expected I2C address.

The startup text matches the RVC startup behavior described in CEVA datasheet
section5.1. Together with the deliberate P0-high reset and readable115200-baud
reception, it strongly confirms the intended mode and the SDA-to-RX8 route.
The address ACK after restoration is an address-level check, not a full SH-2
initialization or sensor-report test.

Raw byte counting occurs before frame parsing. A frame-parser mistake alone
cannot account for the byte count stopping at78. This result bypasses the I2C
report-request path and shows that the RVC application also failed to deliver
sensor frames to the sketch. It does not distinguish physical sensor damage,
sensor firmware/startup failure, supply behavior, or a later electrical/UART
receive failure. No waveform or rail-transient measurement was made. The
absence of frames therefore does not establish physical damage or its cause.

Generated IDE build artifacts are retained locally and excluded by this sketch's
`.gitignore`; no pre-existing build files were deleted. The current exported
`build/...ino.bin` is328,512 bytes and should not be presented as a byte-identical
copy of the328,768-byte application in the upload receipt. Upload verification
here is grounded in that receipt and the captured runtime, not the exported file.
