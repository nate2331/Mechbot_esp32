# Paused source checkpoint — 2026-09-07

**Draft only. Not compiled, installed, run, or approved for connection.** Work
paused because the Flipper is not currently available. No firmware or hardware
was changed by this implementation task.

Files present:

- `application.fam`: native external app manifest, `imu_rvc_test`.
- `imu_rvc_test.cpp`: C++ draft of a one-trial, receive-only UART-RVC app.
- `RvcParser.h`: unchanged copy of the existing ESP32 diagnostic parser.
- `.gitignore`: excludes local build output.

The draft prepares PB7 / physical pin14 for USART1 AF7 with NoPull before asking
the user to connect the unpowered IMU. It disables UART TX, disables expansion
before acquiring UART, and verifies GPIO and power-control pull registers.
It then offers an explicit Start action for one nominal 35-second capture.
Storage uses unique `.bin` and `.txt` paths under `/ext/apps_data/imu_rvc_test`.

Power-service OTG requests remain off; Start uses one direct HAL boost attempt,
avoiding the power service's automatic retry behavior. Stop disables boost and
retains UART configuration. UART release and expansion restoration require
stable VBUS-absent indications and an explicit physical-disconnection confirmation.
The BQ25896's zero VBUS indication means VBUS_GD is clear, **not a precise
measurement proving the rail is fully discharged**. It has approximately one
second ADC updates and the HAL reader does not expose an I2C-read validity result.

Remaining work before use:

1. Confirm the actual official firmware version and compile against its matching
   SDK. Headers inspected during preparation were official release 1.4.3; the
   installed version and API export compatibility have not been established.
2. Complete independent source review, including acquisition-failure cleanup,
   RX register checks, supply sequencing, SD failure paths, and screen fit.
3. Decide how to guarantee the power-off deadline independently of SD writes.
   The draft checks elapsed time in the main loop; a blocking storage write can
   delay power-off beyond 35 seconds. A dedicated deadline-control thread is a
   possible solution. No strict real-time cutoff is currently claimed.
4. Verify log completeness and UART loss counters with a harmless synthetic
   stream, then check device UI and all stop/disconnect states with no IMU attached.
5. Finish user instructions and provenance. This app cannot prevent forced
   termination, reboot, battery removal, USB insertion, or incorrect wiring.

Primary API sources inspected:

- [Official 1.4.3 serial HAL](https://github.com/flipperdevices/flipperzero-firmware/blob/1.4.3/targets/f7/furi_hal/furi_hal_serial.h)
- [Official 1.4.3 GPIO HAL](https://github.com/flipperdevices/flipperzero-firmware/blob/1.4.3/targets/f7/furi_hal/furi_hal_gpio.h)
- [Official 1.4.3 power service](https://github.com/flipperdevices/flipperzero-firmware/blob/1.4.3/applications/services/power/power_service/power.c)
- [STRDC R1 schematic](https://docs.strdc.com/schematics/BNO085_BOB-R1_V1_Schematic.pdf)

The Flipper receiver's conditional 5 V input tolerance was separately reviewed
by another agent. This draft is not a substitute for the final hardware review.
