import time
import unittest
import tempfile
import sys
import types
from pathlib import Path

try:
    import serial  # noqa: F401
except ModuleNotFoundError:
    sys.modules["serial"] = types.SimpleNamespace(SerialException=OSError, Serial=None)

from mechbot_bridge import Bridge


class FakeSerial:
    def __init__(self, bridge=None):
        self.commands = []
        self.bridge = bridge
        self.settings = dict(zip(("pwm-fl", "pwm-fr", "pwm-rl", "pwm-rr"), (230, 230, 200, 200)))
        self.settings.update({"heading-kp": 0.7, "heading-max": 0.3,
                              "heading-deadband-deg": 1.5, "heading-sign": 1,
                              "heading-enabled": 1})

    def write(self, data):
        command = data.decode("ascii").strip()
        self.commands.append(command)
        if self.bridge and command == "CFG GET":
            for key, value in self.settings.items():
                self.bridge.parse_line(f"CFG {key} {value}")
        elif self.bridge and command == "IMU ACCEPT":
            self.bridge.parse_line("OK IMU ACCEPT SESSION_ONLY")
            self.bridge.parse_line("IR2 1200 0 READY 0 0 0 0 0 1000 1 0 0 0 0")
        elif command.startswith("CFG SET"):
            _, _, key, value = command.split()
            self.settings[key] = float(value)

    def close(self):
        pass

    def flush(self):
        pass


class CalibrationSafetyTest(unittest.TestCase):
    def setUp(self):
        self.bridge = Bridge()
        self.tempdir = tempfile.TemporaryDirectory()
        self.bridge.session_file = Path(self.tempdir.name) / "session.json"
        self.bridge.serial = FakeSerial(self.bridge)
        self.bridge.telemetry.update(serial_connected=True, encoders=[10, 20, 30, 40],
                                     encoder_updated=time.time(), firmware="ESP32_MECANUM_USB_IMU_NAV_V4")

    def tearDown(self):
        self.tempdir.cleanup()

    def test_confirmation_is_required(self):
        with self.assertRaises(ValueError):
            self.bridge.start_calibration("yes")

    def test_pwm_configuration_is_reported_from_firmware(self):
        self.bridge.parse_line('PWM_CONFIG HZ 248 BITS 9 SCALE 2 CLOCK APB')
        config = self.bridge.snapshot()['pwm_config']
        self.assertEqual((config['hz'], config['bits'], config['scale']), (248,9,2))
        self.bridge.parse_line('PWM_CONFIG HZ bad BITS 9 SCALE 2 CLOCK APB')
        self.assertEqual(self.bridge.snapshot()['pwm_config'], config)

    def test_heading_off_baseline_does_not_require_imu(self):
        self.bridge.telemetry.update(firmware='ESP32_MAKER_MECANUM_RVC_V2', imu_updated=None)
        self.bridge.start_tuning('imu', 'AREA_CLEAR')
        self.bridge.start_calibration_pulse('forward', 500)
        deadline = time.time()+2
        while self.bridge.calibration_status()['running'] and time.time()<deadline:
            time.sleep(.02)
        result=self.bridge.calibration_status()
        self.assertIsNone(result['error'])
        self.assertEqual(result['phase'], 'complete')
        self.assertIn('V 0.450 0.000 0.000', self.bridge.serial.commands)

    def test_heading_settings_require_confirmation_and_stop_before_enable(self):
        self.bridge.telemetry.update(firmware='ESP32_MAKER_MECANUM_RVC_V2',
            imu='IR2 1000 0 READY 0 0 0 0 0 1000 0 0 0 0 0', imu_updated=time.time())
        self.bridge.start_tuning('imu', 'AREA_CLEAR')
        self.bridge.serial.commands.clear()
        with self.assertRaises(ValueError):
            self.bridge.set_trial_heading(True,.7,.3,1.5)
        self.assertEqual(self.bridge.serial.commands, [])
        self.bridge.set_trial_heading(True,.7,.3,1.5,'HEADING_MEASURED')
        self.assertEqual(self.bridge.serial.commands[:4], ['V 0 0 0','X','F 0','IMU REVOKE'])
        self.assertEqual(self.bridge.serial.commands[-1], 'IMU ACCEPT')
        self.bridge.end_tuning()
        self.assertEqual(self.bridge.serial.commands[-1], 'IMU REVOKE')

    def test_heading_rejected_ack_leaves_mode_off(self):
        self.bridge.telemetry.update(firmware='ESP32_MAKER_MECANUM_RVC_V2',
            imu='IR2 1000 0 READY 0 0 0 0 0 1000 0 0 0 0 0', imu_updated=time.time())
        self.bridge.start_tuning('imu', 'AREA_CLEAR')
        self.bridge.command = lambda *args, **kwargs: ['ERR IMU ACCEPT requires qualified fresh RVC']
        with self.assertRaisesRegex(RuntimeError, 'did not accept'):
            self.bridge.set_trial_heading(True,.7,.3,1.5,'HEADING_MEASURED')
        self.assertEqual(self.bridge.tuning_session['live_settings']['heading-enabled'],0)
        self.assertFalse(self.bridge._heading_configuring)
        self.assertEqual(self.bridge.serial.commands[-2:],['CFG SET heading-enabled 0','IMU REVOKE'])

    def test_bench_tuning_requires_wheels_up_confirmation(self):
        with self.assertRaises(ValueError):
            self.bridge.start_tuning("bench", "AREA_CLEAR")
        state = self.bridge.start_tuning("bench", "WHEELS_UP")
        self.assertTrue(state["active"])

    def test_rear_encoder_profile_is_explicit(self):
        status = self.bridge.start_calibration("WHEELS_UP", "rear")
        self.assertEqual(status["encoder_profile"], "rear")
        self.bridge.end_calibration()
        with self.assertRaises(ValueError):
            self.bridge.start_calibration("WHEELS_UP", "front")

    def test_pulse_is_bounded_and_finishes_with_stop(self):
        self.bridge.start_calibration("WHEELS_UP")
        with self.assertRaises(ValueError):
            self.bridge.start_calibration_pulse("forward", 400)
        with self.assertRaises(ValueError):
            self.bridge.start_calibration_pulse("forward", 5001)
        self.assertEqual(self.bridge.tuning_status()['test_limits']['duration_max_ms'], 5000)
        with self.assertRaises(ValueError):
            self.bridge.start_calibration_pulse("diagonal", 500)

        self.bridge.start_calibration_pulse("forward", 500)
        deadline = time.time() + 2
        while self.bridge.calibration_status()["running"] and time.time() < deadline:
            time.sleep(0.02)

        status = self.bridge.calibration_status()
        self.assertFalse(status["running"])
        self.assertEqual(status["deltas"], [0, 0, 0, 0])
        self.assertIn("V 0 0 0", self.bridge.serial.commands)
        self.assertEqual(self.bridge.serial.commands[-1], "X")

    def test_pulse_magnitude_is_bounded_and_scales_motion(self):
        self.bridge.start_calibration("WHEELS_UP")
        with self.assertRaises(ValueError):
            self.bridge.start_calibration_pulse("forward", 500, 0.14)
        with self.assertRaises(ValueError):
            self.bridge.start_calibration_pulse("forward", 500, 1.01)
        self.bridge.start_calibration_pulse("left", 500, 0.35)
        deadline = time.time() + 2
        while self.bridge.calibration_status()["running"] and time.time() < deadline:
            time.sleep(0.02)
        self.assertIn("V 0.000 0.350 0.000", self.bridge.serial.commands)

    def test_pulse_rejects_stale_encoder_telemetry(self):
        self.bridge.start_calibration("WHEELS_UP")
        self.bridge.telemetry["encoder_updated"] = time.time() - 2
        with self.assertRaisesRegex(RuntimeError, "stale"):
            self.bridge.start_calibration_pulse("forward", 500, 0.35)

    def test_emergency_stop_aborts_active_pulse(self):
        self.bridge.start_calibration("WHEELS_UP")
        self.bridge.start_calibration_pulse("forward", 5000)
        time.sleep(0.08)
        self.bridge.emergency_stop()
        deadline = time.time() + 1
        while self.bridge.calibration_status()["running"] and time.time() < deadline:
            time.sleep(0.02)
        self.assertFalse(self.bridge.calibration_status()["running"])
        self.assertEqual(self.bridge.calibration_status()["phase"], "aborted")
        self.assertLess(self.bridge.calibration_status()['elapsed_ms'], 1000)
        self.assertEqual(self.bridge.serial.commands[-1], "X")

    def test_assisted_session_persists_test_and_observation(self):
        state = self.bridge.start_tuning("floor", "AREA_CLEAR")
        self.assertTrue(state["active"])
        self.bridge.start_calibration_pulse("left", 500)
        deadline = time.time() + 2
        while self.bridge.calibration_status()["running"] and time.time() < deadline:
            time.sleep(0.02)
        state = self.bridge.tuning_status()
        self.assertEqual(state["tests"][0]["command"], "left")
        state = self.bridge.record_observation(1, "arc-left", 2, "tile floor")
        self.assertEqual(state["tests"][0]["score"], 2)
        self.assertIsNone(state["tests"][0]["prior_best_id"])
        self.assertTrue(self.bridge.session_file.exists())

    def test_revert_restores_original_settings(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        self.bridge.apply_baseline()
        self.bridge.end_tuning(False)
        self.assertFalse(self.bridge.tuning_status()["active"])
        self.assertEqual(self.bridge.tuning_status()["phase"], "restored")

    def test_raw_ticks_never_generate_pwm_recommendation(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        self.bridge.start_calibration_pulse("forward", 500)
        deadline = time.time() + 2
        while self.bridge.calibration_status()["running"] and time.time() < deadline:
            time.sleep(0.02)
        test = self.bridge.tuning_status()["tests"][0]
        self.assertNotIn("recommended", test)
        self.assertNotIn("pwm", test)

    def test_active_session_is_recovered_from_disk(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        recovered = Bridge()
        recovered.session_file = self.bridge.session_file
        recovered.tuning_session = recovered._load_session()
        recovered._restore_pending = bool(recovered.tuning_session["active"])
        self.assertTrue(recovered.tuning_session["active"])
        self.assertTrue(recovered._restore_pending)

    def test_live_and_saved_settings_are_recorded(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        self.bridge.note_live_setting("pwm-fl", 225)
        self.bridge.note_settings_saved()
        state = self.bridge.tuning_status()
        self.assertEqual(state["live_settings"]["pwm-fl"], 225)
        self.assertEqual(state["saved_settings"]["pwm-fl"], 225)

    def test_manual_pwm_changes_are_validated_and_audited(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        with self.assertRaises(ValueError):
            self.bridge.apply_tuning_settings({"heading-kp": 2})
        with self.assertRaises(ValueError):
            self.bridge.apply_tuning_settings({"pwm-rl": 256})
        state = self.bridge.apply_tuning_settings(
            {"pwm-fl": 230, "pwm-fr": 228, "pwm-rl": 201, "pwm-rr": 199})
        self.assertEqual(state["live_settings"]["pwm-fr"], 228)
        self.assertEqual(state["adjustments"][-1]["source"], "manual")
        self.assertIn("CFG SET pwm-rr 199", self.bridge.serial.commands)

    def _run_scored_test(self, direction, heading, path="none", step=3):
        self.bridge.start_calibration_pulse(direction, 500, 0.45)
        deadline = time.time() + 2
        while self.bridge.calibration_status()["running"] and time.time() < deadline:
            time.sleep(0.02)
        test = self.bridge.tuning_status()["tests"][-1]
        return self.bridge.record_observation(
            test["id"], heading, 3, heading=heading, path=path,
            quality="normal", adjustment_step=step)["tests"][-1]

    def test_forward_yaw_feedback_generates_balanced_side_trim(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        test = self._run_scored_test("forward", "yaw-left")
        recommendation = test["recommendation"]
        self.assertEqual(recommendation["kind"], "pwm-vector")
        self.assertEqual(recommendation["suggested_settings"], {
            "pwm-fl": 233, "pwm-fr": 227, "pwm-rl": 203, "pwm-rr": 197})
        self.assertIn("never compared", recommendation["basis"])

    def test_forward_path_feedback_generates_diagonal_trim(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        test = self._run_scored_test("forward", "straight", "drift-left", 2)
        self.assertEqual(test["recommendation"]["suggested_settings"], {
            "pwm-fl": 232, "pwm-fr": 228, "pwm-rl": 198, "pwm-rr": 202})

    def test_strafe_yaw_feedback_adjusts_front_and_rear_pairs(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        test = self._run_scored_test("left", "yaw-left")
        self.assertEqual(test["recommendation"]["suggested_settings"], {
            "pwm-fl": 227, "pwm-fr": 227, "pwm-rl": 203, "pwm-rr": 203})

    def test_bench_feedback_never_generates_chassis_trim(self):
        self.bridge.start_tuning("bench", "WHEELS_UP")
        test = self._run_scored_test("forward", "yaw-left")
        self.assertEqual(test["recommendation"]["kind"], "hold")
        self.assertIn("wheels-up", test["recommendation"]["basis"])

    def test_stalled_motion_does_not_generate_directional_trim(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        recommendation = self.bridge._build_recommendation(
            {"command": "forward", "settings": {}},
            "yaw-left", "drift-left", "no-motion", 3)
        self.assertEqual(recommendation["kind"], "hold")
        self.assertIn("Raise test output", recommendation["summary"])

    def test_rate_comparison_requires_identical_output_and_duration(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        self._run_scored_test("forward", "straight")
        self.bridge.telemetry["encoder_updated"] = time.time()
        self.bridge.start_calibration_pulse("forward", 500, 0.60)
        deadline = time.time() + 2
        while self.bridge.calibration_status()["running"] and time.time() < deadline:
            time.sleep(0.02)
        comparison = self.bridge.tuning_status()["tests"][-1]["comparison"]
        self.assertFalse(comparison["comparable"])
        self.assertIn("output", comparison["reason"])

    def test_recommendation_requires_approval_before_live_apply(self):
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        test = self._run_scored_test("forward", "yaw-right")
        before = dict(self.bridge.tuning_status()["live_settings"])
        self.assertNotEqual(before.get("pwm-fl"),
                            test["recommendation"]["suggested_settings"]["pwm-fl"])
        state = self.bridge.apply_recommendation(test["id"])
        self.assertTrue(state["tests"][-1]["recommendation_applied"])
        self.assertEqual(state["live_settings"]["pwm-fl"], 227)

    def test_save_restores_heading_state_before_cfg_save(self):
        self.bridge.lines = [
            "CFG pwm-fl 230", "CFG pwm-fr 230", "CFG pwm-rl 200", "CFG pwm-rr 200",
            "CFG heading-kp 0.7", "CFG heading-max 0.3",
            "CFG heading-deadband-deg 1.5", "CFG heading-sign 1",
            "CFG heading-enabled 1",
        ]
        self.bridge.start_tuning("floor", "AREA_CLEAR")
        self.bridge.apply_tuning_settings({"pwm-rl": 202})
        state = self.bridge.end_tuning(True, True)
        self.assertEqual(state["phase"], "ended-saved")
        self.assertEqual(state["saved_settings"]["heading-enabled"], 1)
        heading_restore = max(index for index, command in enumerate(self.bridge.serial.commands)
                              if command == "CFG SET heading-enabled 1.0")
        save = max(index for index, command in enumerate(self.bridge.serial.commands)
                   if command == "CFG SAVE")
        self.assertLess(heading_restore, save)


if __name__ == "__main__":
    unittest.main()
