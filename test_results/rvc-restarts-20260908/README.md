# Integrated RVC controller restart checks

September 8. Motor power remained off under the existing operator setup. Three
ESP32 resets were performed through CH340 RTS while the IMU remained powered.
This does not test removal of IMU power, shared-supply cold startup or motor noise.

The bridge entered maintenance and released serial ownership. Each reset was
observed for six seconds. Only X, DIAG and CFG GET were written by the test;
there was no flash, NVS save, heading acceptance or nonzero velocity command.

All three passed: fresh V2 READY in 0.662–0.663 seconds; first qualified IR2 report
in 0.861–0.862 seconds. All sensor reports retained zero communication counters
and no heading acceptance. Each round showed four zero outputs, heading-enabled
0 and navigation hold/field/ready all 0. Raw transcripts are in `results.json`;
the exact procedure is retained in `procedure.py`.

Serial ownership returned to the bridge successfully, with fresh zero-output
diagnostics, V2 identity, deadman false and rearm required. Pi evidence directory:
`/home/nate/rvc-restarts-x_6azrkq`.

Next: confirm whether the IMU is powered from Maker 5 V or a separate supply before
guiding a full-power startup test. Sensor confidence remains open for that test,
agreed accuracy criteria and motor-powered reliability.
