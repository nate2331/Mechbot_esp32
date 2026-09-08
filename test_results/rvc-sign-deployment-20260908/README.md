# Corrected RVC heading deployment

September 8, after the hand-positioned angle series. Existing authorization to
flash with motor power off remains in effect; no motor-powered test was performed.

- Pi release `/home/nate/rvc-release-uiqtn4zg`: 249 Python tests passed on Pi, 54 non-Markdown release file hashes match local sources. Previous installed files backed up under `backup/`.
- Firmware upload `/home/nate/maker-rvc-v2-upload-4me4ncrf`: V1 rollback images copied and independently verified against the device before writing. Current partition/NVS region backed up; partition map matched. All four V2 images verified after writing, NVS unchanged.
- Fresh `READY ESP32_MAKER_MECANUM_RVC_V2` at 0.680 seconds after reset. Build: 329027 flash / 23400 global RAM bytes. Distinct V2 help also identifies the firmware after reconnect.
- Service active/running with zero automatic restarts. Updater dry run recognizes V2 as both source and expected target, with the RVC build flag and isolated output directory.
- Thirty stopped observations: fresh valid IR2, zero checksum/index/UART counters, four fresh zero outputs, deadman false, rearm required and heading acceptance false. Every explicit heading equals wrapped negative raw yaw within protocol precision.

This verifies deployment and conversion, not physical correction polarity under
power. Raw angles remain unchanged in `ypr_deg`; `yaw_rad` is now CCW-positive.
Historical IR1 remains readable but cannot establish control readiness in the new
Pi software. No IMU ACCEPT, heading-enable or nonzero motion command was issued.

Host checks cover measured-direction fixtures, field transformation, angle wrap,
qualification, stale/error rejection and motor watchdog. Windows Python: 249 tests
with one platform skip. JavaScript: 39 passes. Logs and release/image hashes are
beside this file. Next: one measured post-update turn, then repeatability/drift and
supervised motor-powered acceptance before closing the roadmap tasks.
