# RVC heading convention correction

September 8 hand checks showed raw yaw decreasing for a reported counterclockwise
turn (-88.113 degrees), and increasing for clockwise (+90.510 degrees). This is
opposite the robot command convention. After finishing the first angle series,
V2 was deployed with motor power off. Images and unchanged NVS were independently
verified; a fresh V2 READY was captured. Release: `/home/nate/rvc-release-uiqtn4zg`;
upload and verified V1 rollback images: `/home/nate/maker-rvc-v2-upload-4me4ncrf`.
See `test_results/rvc-sign-deployment-20260908` for evidence.

V2 negates and wraps raw RVC yaw once at the heading input used by both heading hold
and field transformation. It preserves the raw orientation and acceleration fields
and emits `IR2`, adding a final `ccw_heading_rad` field to the prior IR1 layout.
Pi consumers use that explicit heading for `yaw_rad`; `ypr_deg` retains raw angles.
The parser validates that the added heading agrees with wrapped negative raw yaw.

Historical IR1 records retain their raw yaw for replay, but do not establish control
readiness. New Pi heading acceptance, field selection and re-zero require fresh IR2
data. Robot-frame selection and revocation remain available for older RVC firmware.
The updater recognizes V1/V2 sources and expects V2 after compiling the corrected
source. Session acceptance remains separate from stream qualification.

Do not reverse `heading-sign` to compensate for raw yaw direction: that setting
changes correction output only and would leave field transforms incorrect. This
change does not establish motor correction polarity or full navigation acceptance.
After deployment, repeat a measured turn before enabling heading control.
