# Maker encoder firmware upload, September 5

Following the user's instruction to proceed, the tested encoder sampling patch
was uploaded through the Pi's CH340 USB adapter to the Maker ESP32 Pro. Flash
verification succeeded and a fresh `READY ESP32_MAKER_MECANUM_IMU_V1` was
captured 1.686 seconds after the post-upload reset. The live bridge reconnected
and identified Maker, with deadman rearm required and all four outputs zero.

## Uploaded build

- Source revision: `a4b04b521000316440ae03050afd00b8771f4092`.
- Board: `esp32:esp32:esp32`, Espressif core 3.3.11.
- Fresh PC build: 359,815 bytes program, 27,688 bytes globals.
- Application image: 359,968 bytes; SHA-256
  `5eca6567974baf1a3796c447d1c8c411af0020dab128a0bc94523d74dc28a470`.
- The sketch and all three local headers matched the reviewed source before
  packaging. This is the single-register encoder-pair sampling change already
  covered by the prior host tests; no new motion behavior was added here.

## Recovery evidence and verification

The first backup attempt at 460800 baud failed with a serial data-stream error
before writing firmware. A USB reset recovered the controller; retrying at
115200 baud completed the full 4 MB backup in 413.1 seconds.

Successful upload and backup directory:
`/home/nate/maker-upload-r3xc0deu`. `flash-before.bin` is the complete pre-upload
image. A second copy is retained in the PC task workspace as
`maker-flash-before-20260905.bin`. The existing partition table matched the
candidate before any write. The uploader wrote the standard bootloader,
partition table, boot-app and application images without whole-chip erase.

Independent esptool verification matched all four uploaded files. It also
matched the entire 20,480-byte NVS region against the pre-upload backup.
All nine live settings remained unchanged: four PWM values of 177, heading
KP 0.7, maximum correction 0.3, deadband 1.5 degrees, sign +1 and heading off.

The Pi's saved source under `/home/nate/mechbot-src/Maker_Mechbot` was also
updated to match the uploaded source, after verifying every old file against
the inspected baseline and backing it up. This prevents a later build from
silently reverting to the old encoder sampler. The source-install report
records its backup directory and all hashes.

PC workspace evidence: `maker-upload-build.log`, `maker-upload-manifest.json`,
`maker-upload-result.json`, `maker-upload-verify.log`, `maker-source-install.json`
and `maker-upload-followup.json`. Detailed command logs and images remain in
the Pi upload directory.

## Remaining physical validation

The IMU still reports OFFLINE after upload. Flash verification does not resolve
that intermittent sensor issue. No wheel-motion trial was commanded; zero
stationary encoder counts cannot prove the invalid-transition fix under motion.
Next steps remain a supervised encoder retest, reverse/holding-duty
characterization, measured wheel geometry and loaded trials.
