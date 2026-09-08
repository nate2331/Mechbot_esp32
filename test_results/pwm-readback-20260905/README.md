# Powered PWM peripheral readback — September 5, 2026

The user reported visibly slower front wheels and requested investigation of
the control path. Saved settings and previous D diagnostics alone could not
verify the PWM peripheral output. The firmware now adds P records to DIAG,
reading LEDC duty registers and frequency through the installed ESP32 core.
Drive behavior, PWM settings, encoder scaling and watchdog were unchanged.

Actual firmware host tests passed, including a deliberately mismatched duty
register to prove that readback does not merely repeat the software target.
ESP32 core 3.3.11 compilation passed: 360423 flash bytes, 27688 global bytes.
Firmware source commit 0c06b6c was uploaded, independently flash-verified, and
fresh READY observed. NVS was independently verified byte-for-byte unchanged.
The backup covers 0x00000 through 0x6ffff: all overwritten ranges and NVS.
Unwritten flash outside that range was left untouched, not included in this
backup. Backup on Pi: /home/nate/maker-pwm-readback-upload-rsy2gw6x/flash-before.bin;
PC copy: pwm-readback-flash-before.bin in the parent workspace.

Two initial trials sent no motion: offline-IMU retries paused telemetry about
800 ms while stopped. The test-only rest routine was adjusted to wait up to
six seconds for fresh stationary evidence following acknowledged STOP.
It resets stationary evidence on any stale interval; powered freshness,
wrong-direction, output, watchdog and invalid-transition checks stay intact.
No changes were made to the production automatic tuner.

The final 1.2-second all-forward trial completed with no new invalid transitions.
During powered DIAG, all four active inputs read duty 177 and 20000 Hz; all
opposite inputs read duty zero (the core returns zero frequency at zero duty).
This is a peripheral-register readback, not an oscilloscope or voltmeter result.

| Wheel | Active pin | Readback duty | Readback Hz | Tail RPM |
| --- | ---: | ---: | ---: | ---: |
| FL | 17 | 177 | 20000 | 12.58 |
| FR | 14 | 177 | 20000 | 8.57 |
| RL | 4 | 177 | 20000 | 60.58 |
| RR | 13 | 177 | 20000 | 71.06 |

The slowdown reproduced despite equal peripheral duty and frequency. No
incorrect PWM scaling or mixer output was demonstrated. Physical waveforms,
motor-driver output and regulated voltage at the controller remain unmeasured;
the root cause is not established. Do not claim equal motor terminal voltages
or apply fixed trims based on these load-dependent speeds.

All nine settings were restored, four motor outputs confirmed zero, the bridge
resumed and the controller requires rearm. The IMU is currently OFFLINE after
the firmware reboot; heading and field modes remain disabled. Firmware source
and host-test changes were copied to the original Arduino project after a
guarded comparison and backup. Post-commit documentation/tests correct the
zero-duty frequency semantics; these do not alter the uploaded binary.
