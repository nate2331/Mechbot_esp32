# Integrated RVC software checkpoint

September 8, 2026. No upload, service activation or physical motion trial.

- `manifest.json`: exact source/binary hashes and target build sizes.
- `host-tests.txt`: actual-sketch RVC normal/UART/RX failure scenarios and SPI regressions.
- `rvc-integrated-python-tests.txt`: 241 tests, 240 passed, one platform skip.
- `rvc-integrated-node-tests.txt`: 39 passed.
- `browser-results.json`: mocked desktop/mobile Edge checks. Screenshots under ignored `browser-qa-output/rvc/`; integrated phone screenshot visually inspected.

See [integration instructions](../../docs/INTEGRATED_RVC.md) for capability limits,
session acceptance and the outstanding physical measurements. Test output mentioning
upload is from mocked updater tests using `/fake/usb`, not a device operation.
