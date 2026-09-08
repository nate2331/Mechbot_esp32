"""Board capabilities shared by the bridge and updater; no hardware IO."""

from copy import deepcopy
from mechbot_telemetry import RVC_BANNERS

WHEELS = ("FL", "FR", "RL", "RR")
PWM_KEYS = tuple("pwm-" + wheel.lower() for wheel in WHEELS)
# September 3, 2026: ten complete forward wheel turns, normal encoder wiring.
MAKER_COUNTS_PER_REV = (2468.8, 2467.9, 2473.5, 2469.8)
MAKER_HELP_IDENTITY = "Maker mapping: FL=M2 FR=M3 RL=M1 RR=M0; all encoders forward-positive"
MAKER_RVC_HELP_IDENTITY = "Maker RVC mapping: FL=M2 FR=M3 RL=M1 RR=M0; all encoders forward-positive"

PROFILES = {
    "maker": {
        "id": "maker", "name": "Maker ESP32 Pro",
        "firmware": "ESP32_MAKER_MECANUM_IMU_V1",
        "fqbn": "esp32:esp32:esp32", "sketch": "Maker_Mechbot",
        "encoder_wheels": list(WHEELS), "diagnostics": True,
        "baseline": dict.fromkeys(PWM_KEYS, 177),
        "baseline_label": "Maker starting settings · 177 on every wheel",
        "baseline_note": "Used in raised-wheel tests; not a matched-speed or floor-validated trim.",
        "counts_per_revolution": dict(zip(WHEELS, MAKER_COUNTS_PER_REV)),
        "calibration_source": "2026-09-03 ten-turn measurements; see HARDWARE_BASELINE.md",
        "closed_loop": False,
    },
    "s3": {
        "id": "s3", "name": "Legacy ESP32-S3",
        "firmware": "ESP32_MECANUM_USB_IMU_NAV_V4",
        "fqbn": "esp32:esp32:esp32s3", "sketch": "Mechbot_IMU_ESP32",
        "encoder_wheels": ["RL", "RR"], "diagnostics": False,
        "baseline": dict(zip(PWM_KEYS, (230, 230, 200, 200))),
        "baseline_label": "Legacy chassis baseline · 230 / 230 / 200 / 200",
        "baseline_note": "Observed on the older rear-encoder chassis; do not transfer to Maker.",
        "counts_per_revolution": {}, "calibration_source": None,
        "closed_loop": False,
    },
}


def profile_for_firmware(firmware, help_identity=None):
    """Exact READY or the deployed Maker's distinctive help identifies its board."""
    if firmware == 'ESP32_MAKER_MECANUM_RVC_V1' or (firmware is None and help_identity == MAKER_RVC_HELP_IDENTITY):
        return dict(deepcopy(PROFILES['maker']), firmware='ESP32_MAKER_MECANUM_RVC_V1',
                    identity_source='ready' if firmware else 'maker-rvc-help',
                    imu_transport='uart-rvc', build_properties=['compiler.cpp.extra_flags=-DMAKER_IMU_RVC=1'])
    for profile in PROFILES.values():
        if firmware == profile["firmware"]:
            return dict(deepcopy(profile), identity_source="ready")
    if firmware is None and help_identity == MAKER_HELP_IDENTITY:
        return dict(deepcopy(PROFILES["maker"]), identity_source="maker-help")
    diagnostic = firmware in RVC_BANNERS.values()
    return {"id": "unknown", "name": "Maker UART-RVC diagnostic" if diagnostic else "Unidentified controller",
            "firmware": firmware, "encoder_wheels": [], "diagnostics": False,
            "baseline": {}, "baseline_label": "Identify the connected controller",
            "baseline_note": ("Standalone sensor diagnostic; robot control and firmware updates are unavailable."
                              if diagnostic else "Board-specific tuning and firmware updates are unavailable."),
            "counts_per_revolution": {}, "calibration_source": None,
            "closed_loop": False}


def require_profile(firmware, help_identity=None):
    profile = profile_for_firmware(firmware, help_identity)
    if profile["id"] == "unknown":
        raise RuntimeError("a recognized firmware READY identity is required")
    return profile
