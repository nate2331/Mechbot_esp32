"""Board identity, update selection, recovery and export regression tests."""
import csv
import io
import json
import tempfile
import time
import unittest
from pathlib import Path
from unittest.mock import patch

from test_mechbot_bridge import FakeSerial
from mechbot_bridge import Bridge
from mechbot_profiles import MAKER_HELP_IDENTITY, PROFILES, PWM_KEYS, profile_for_firmware
from mechbot_firmware_update import build_plan, perform_update, verified_boot


def status(board="maker"):
    return {"serial_connected": True, "serial_port": "/dev/serial/by-id/test-maker",
            "firmware": PROFILES[board]["firmware"], "firmware_received": 100}


class BoardIntegrationTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.bridge = Bridge()
        self.bridge.session_file = Path(self.temp.name) / "session.json"
        self.bridge.tuning_session = self.bridge._empty_session()
        self.bridge._restore_pending = False
        self.bridge.serial = FakeSerial(self.bridge)
        self.bridge.serial.settings.update(dict.fromkeys(PWM_KEYS, 177))
        self.bridge.serial.settings["heading-enabled"] = 0
        self.bridge.telemetry.update(status(), encoders=[0]*4, encoder_updated=time.time())

    def tearDown(self):
        self.temp.cleanup()

    def test_maker_baseline_never_uses_legacy_trims(self):
        self.bridge.start_tuning("bench", "WHEELS_UP")
        result = self.bridge.apply_baseline()
        self.assertEqual(result["proven_baseline"], dict.fromkeys(PWM_KEYS, 177))
        self.assertEqual(result["calibration"]["encoder_profile"], "four")
        self.assertEqual(result["board"]["counts_per_revolution"]["RL"], 2473.5)
        self.assertNotIn("CFG SET pwm-fl 230", self.bridge.serial.commands)

    def test_unknown_firmware_rejects_tuning_and_update(self):
        self.bridge.telemetry["firmware"] = "ESP32_MAKER_MECANUM_IMU_V1 unexpected"
        for action in (lambda: self.bridge.start_tuning("bench", "WHEELS_UP"),
                       self.bridge.start_firmware_update):
            with self.assertRaisesRegex(RuntimeError, "recognized"):
                action()
        self.assertEqual(self.bridge.serial.commands, [])

    def test_deployed_maker_help_identifies_board_without_inventing_ready(self):
        self.bridge.telemetry.pop("firmware")
        self.bridge.parse_line(MAKER_HELP_IDENTITY)
        self.assertEqual(self.bridge.board_profile()["id"], "maker")
        self.assertEqual(self.bridge.board_profile()["identity_source"], "maker-help")
        self.assertNotIn("firmware", self.bridge.telemetry)
        plan = build_plan(self.bridge.snapshot())
        self.assertEqual(plan["board_id"], "maker")
        self.assertFalse(verified_boot(self.bridge.snapshot(), plan, 0))

    def test_explicit_unknown_ready_is_not_overridden_by_old_help(self):
        self.bridge.parse_line(MAKER_HELP_IDENTITY)
        self.bridge.parse_line("READY DIFFERENT_FIRMWARE")
        self.assertEqual(self.bridge.board_profile()["id"], "unknown")

    def test_cannot_restore_settings_to_other_board(self):
        self.bridge.start_tuning("bench", "WHEELS_UP")
        self.bridge.telemetry["firmware"] = PROFILES["s3"]["firmware"]
        self.bridge.serial.commands.clear()
        with self.assertRaisesRegex(RuntimeError, "another"):
            self.bridge._restore_session_settings()
        self.assertEqual(self.bridge.serial.commands, [])

    def test_pre_profile_session_is_not_blindly_restored(self):
        self.bridge.tuning_session.update(active=True, original_settings={"pwm-fl": 230})
        with self.assertRaisesRegex(RuntimeError, "unidentified"):
            self.bridge._restore_session_settings()

    def test_disconnect_clears_identity_and_old_diagnostics(self):
        self.bridge.parse_line("D FL PWM 177.0 A 10 B 20 INVALID 1")
        self.bridge.disconnect_serial()
        self.assertEqual(self.bridge.board_profile()["id"], "unknown")
        self.assertNotIn("wheel_diagnostics", self.bridge.telemetry)
        self.assertTrue(self.bridge.rearm_required)

    def test_valid_diagnostics_and_bad_values(self):
        self.bridge.parse_line("D FL PWM -177.0 A 10 B 20 INVALID 1")
        diagnostic = self.bridge.snapshot()["wheel_diagnostics"]["FL"]
        self.assertEqual(diagnostic["pwm"], -177)
        self.assertEqual(diagnostic["invalid_transitions"], 1)
        self.bridge.parse_line("D FL PWM nan A 10 B 20 INVALID 2")
        self.assertEqual(self.bridge.snapshot()["wheel_diagnostics"]["FL"], diagnostic)

    def test_imu_wait_and_nan_are_not_fresh(self):
        for line in ("I 100 0 0 0 1 0 0 0 0 0 0 3",):
            self.bridge.parse_line(line)
            self.assertTrue(self.bridge.telemetry["imu_valid"])
        for line in ("I 101 WAIT Q0 G0 A0", "I 102 nan 0 0 1 0 0 0 0 0 0 3"):
            self.bridge.parse_line(line)
            self.assertFalse(self.bridge.telemetry["imu_valid"])
            self.assertIsNone(self.bridge.telemetry["imu_updated"])

    def test_emergency_stop_requires_new_deadman_release(self):
        self.bridge.rearm_required = False
        self.bridge.emergency_stop()
        self.assertTrue(self.bridge.rearm_required)
        self.assertEqual(self.bridge.serial.commands[-1], "X")

    def test_update_reserves_job_before_worker_starts(self):
        with patch("mechbot_bridge.threading.Thread") as thread:
            self.bridge.start_firmware_update()
            with self.assertRaisesRegex(RuntimeError, "already running"):
                self.bridge.start_firmware_update()
            thread.assert_called_once()
            self.assertEqual(thread.call_args.kwargs["args"], ("maker",))

    def test_update_and_maintenance_reject_active_tuning(self):
        self.bridge.start_tuning("bench", "WHEELS_UP")
        with self.assertRaisesRegex(RuntimeError, "finish tuning"):
            self.bridge.start_firmware_update()
        with self.assertRaisesRegex(RuntimeError, "finish tuning"):
            self.bridge.set_maintenance(True)

    def test_update_starting_during_settings_read_blocks_new_session(self):
        def settings():
            self.bridge.firmware_job["state"] = "starting"
            return dict(self.bridge.serial.settings)
        with patch.object(self.bridge, "_read_settings", side_effect=settings):
            with self.assertRaisesRegex(RuntimeError, "firmware maintenance"):
                self.bridge.start_tuning("bench", "WHEELS_UP")
        self.assertFalse(self.bridge.tuning_session["active"])

    def test_legacy_calibration_api_also_respects_update_reservation(self):
        self.bridge.firmware_job["state"] = "starting"
        with self.assertRaisesRegex(RuntimeError, "update is running"):
            self.bridge.start_calibration("WHEELS_UP")

    def test_maker_rejects_rear_only_profile(self):
        with self.assertRaisesRegex(ValueError, "does not match"):
            self.bridge.start_calibration("WHEELS_UP", "rear")

    def test_encoder_loss_during_pulse_aborts_and_stops(self):
        self.bridge.start_calibration("WHEELS_UP")
        self.bridge.start_calibration_pulse("forward", 3000)
        self.bridge.telemetry["encoder_updated"] = time.time() - 2
        deadline = time.monotonic() + 2
        while self.bridge.calibration["running"] and time.monotonic() < deadline:
            time.sleep(.02)
        self.assertEqual(self.bridge.calibration["phase"], "error")
        self.assertIn("stale", self.bridge.calibration["error"])
        self.assertEqual(self.bridge.serial.commands[-1], "X")

    def test_maker_comparison_contains_each_of_four_wheels(self):
        self.bridge.start_tuning("bench", "WHEELS_UP")
        for number in (1, 2):
            self.bridge.telemetry["encoder_updated"] = time.time()
            self.bridge.start_calibration_pulse("forward", 500)
            deadline = time.monotonic() + 2
            while self.bridge.calibration["running"] and time.monotonic() < deadline:
                time.sleep(.02)
            self.bridge.record_observation(number, "straight", 3)
        comparison = self.bridge.tuning_session["tests"][-1]["comparison"]
        self.assertEqual(set(comparison["wheels"]), {"FL", "FR", "RL", "RR"})
        self.assertEqual(set(comparison["rear"]), {"RL", "RR"})

    def test_settings_query_does_not_reuse_old_ring_buffer(self):
        self.bridge.parse_line("CFG pwm-fl 230")
        self.bridge.serial.bridge = None  # No fresh response.
        self.assertEqual(self.bridge._read_settings(), {})
        with self.assertRaisesRegex(RuntimeError, "complete live settings"):
            self.bridge.start_tuning("bench", "WHEELS_UP")

    def test_snapshot_and_export_preserve_board_and_all_wheels(self):
        self.bridge.start_tuning("bench", "WHEELS_UP")
        self.bridge.tuning_session["tests"] = [{"id": 1, "command": "reverse",
            "timestamp": 1000, "duration_ms": 1000, "magnitude": .5,
            "settings": dict.fromkeys(PWM_KEYS, 177), "deltas": [-10, -20, -30, -40],
            "encoder_rates": dict(zip(("FL", "FR", "RL", "RR"), (-10, -20, -30, -40))),
            "notes": '=HYPERLINK("example")\nsecond line'}]
        parsed = json.loads(self.bridge.export_tuning("json"))
        self.assertEqual(parsed["session"]["board_id"], "maker")
        rows = list(csv.DictReader(io.StringIO(self.bridge.export_tuning("csv"))))
        self.assertEqual([r["wheel"] for r in rows], ["FL", "FR", "RL", "RR"])
        self.assertEqual(rows[0]["pulse_average_ticks_s"], "-10")
        self.assertTrue(rows[0]["notes"].startswith("'="))
        snapshot = self.bridge.tuning_status()
        snapshot["tests"][0]["notes"] = "mutated"
        self.assertNotEqual(self.bridge.tuning_session["tests"][0]["notes"], "mutated")


class FirmwareUpdateTest(unittest.TestCase):
    def test_target_selected_by_identity_not_usb_port_name(self):
        for board, fqbn in (("maker", "esp32:esp32:esp32"), ("s3", "esp32:esp32:esp32s3")):
            self.assertEqual(build_plan(status(board))["fqbn"], fqbn)
        with self.assertRaisesRegex(RuntimeError, "does not match"):
            build_plan(status("maker"), "s3")
        self.assertEqual(profile_for_firmware("unknown")["baseline"], {})

    def test_health_line_or_stale_ready_cannot_verify_upload(self):
        plan = build_plan(status())
        self.assertFalse(verified_boot({"recent_lines": ["H 1 IMU 1 1 0 0"]}, plan, 99))
        self.assertFalse(verified_boot(status(), plan, 101))
        self.assertFalse(verified_boot(status("s3"), plan, 99))
        self.assertTrue(verified_boot(status(), plan, 99))

    def test_board_swap_during_compile_cancels_before_maintenance(self):
        calls = []
        snapshots = iter([status("maker"), status("s3")])
        def api(path, payload=None):
            calls.append(path)
            return next(snapshots)
        with patch("subprocess.run") as run:
            with self.assertRaisesRegex(RuntimeError, "does not match"):
                perform_update(api_call=api, run=run)
            self.assertEqual(run.call_count, 1)
        self.assertNotIn("/api/maintenance", calls)

    def test_upload_failure_releases_maintenance(self):
        calls = []
        def api(path, payload=None):
            calls.append((path, payload))
            return status()
        with patch("subprocess.run", side_effect=[None, RuntimeError("upload failed")]) as run:
            with self.assertRaisesRegex(RuntimeError, "upload failed"):
                perform_update(api_call=api, run=run, sleep=lambda _: None)
        self.assertEqual(calls[-1], ("/api/maintenance", {"enabled": False}))

    def test_success_requires_post_upload_ready(self):
        readings = iter([status(), status(), dict(status(), firmware_received=101)])
        def api(path, payload=None):
            return next(readings) if path == "/api/status" else {}
        with patch("subprocess.run") as run:
            result = perform_update(api_call=api, run=run, sleep=lambda _: None, clock=lambda: 100)
        self.assertEqual(result["board_id"], "maker")
        self.assertEqual(run.call_count, 2)


if __name__ == "__main__":
    unittest.main()
