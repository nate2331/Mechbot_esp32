# Historical firmware packages

These September 6 folders preserve upload instructions, source snapshots, and
build/verification manifests from the SPI, error-check, and UART-RVC investigation.
They are historical checkpoints, not a recommendation to replace the later
working UART-RVC bench setup.

Generated `.bin`, `.elf`, `.map`, `.exe`, build directories, and local package
ZIPs are excluded from Git. Existing copies remain on the development PC.
Older instructions that name those files refer to the original local packages;
a fresh clone does not contain ready-to-flash images or pre-flash backups.
Rebuild the appropriate source with the documented board/core configuration.
An archived manifest identifies its original build; it does not authenticate a
newly rebuilt image.

For the latest recorded result and outstanding tests, use the
[weekend record](../docs/WEEKEND_PROGRESS_2026-09-04_TO_07.md) and
[continuous RVC bench test](../Maker_IMU_RVC_Continuous_Test/README.md).
