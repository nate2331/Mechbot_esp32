"""CLI boundary checks; mocked services ensure these tests never touch hardware."""

import builtins
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import types
import unittest
from unittest.mock import Mock, patch


CLI_PATH = Path(__file__).resolve().parents[1] / "mechbot_ops.py"


class OfflineCliTests(unittest.TestCase):
    def setUp(self):
        self.service = types.SimpleNamespace(
            list_evidence=Mock(return_value={
                "reports": [{"id": "bench-safe", "label": "Recorded evidence — no motion"}],
                "errors": [{"id": "incomplete-run", "error": "results.json is missing"}],
            }),
            compare_evidence=Mock(return_value={"left": "left-run", "right": "right-run", "changes": []}),
            capture=types.SimpleNamespace(list_recordings=Mock(return_value=[{
                "id": "SIMULATED-fixture", "mode": "simulated", "duration_s": 8.0,
            }])),
            replay=Mock(return_value={
                "mode": "replay", "id": "SIMULATED-fixture", "duration_s": 8.0,
                "position_s": 2.5, "metadata": {"simulated": True}, "state": "stopped", "dropped": 0,
                "observed": {"pose": {"x_m": 0.25, "y_m": -0.5, "yaw_rad": 0.1}},
                "tx": [{"t": 2.0, "line": "historical only"}],
            }),
        )
        self.constructor_calls = []
        self.constructor_error = None

        def factory(recording_dir, report_dir, geometry_file=None):
            self.constructor_calls.append({"recording_dir": str(recording_dir), "report_dir": str(report_dir),
                                           "geometry_file": geometry_file})
            if self.constructor_error is not None:
                raise self.constructor_error
            return self.service

        backend = types.ModuleType("mechbot_operations")
        backend.OperationsService = factory
        original_import = builtins.__import__

        def offline_import(name, *args, **kwargs):
            if name.split(".")[0] in {"serial", "mechbot_bridge", "pi_mecanum_gamepad", "pi_mecanum_teleop"}:
                raise AssertionError(f"offline CLI must not import hardware module {name}")
            return original_import(name, *args, **kwargs)

        self.import_stdout, self.import_stderr = io.StringIO(), io.StringIO()
        backend_patch = patch.dict(sys.modules, {"mechbot_operations": backend})
        backend_patch.start()
        self.addCleanup(backend_patch.stop)
        import_patch = patch("builtins.__import__", side_effect=offline_import)
        import_patch.start()
        self.addCleanup(import_patch.stop)
        spec = importlib.util.spec_from_file_location("_mechbot_cli_under_test", CLI_PATH)
        self.cli = importlib.util.module_from_spec(spec)
        with contextlib.redirect_stdout(self.import_stdout), contextlib.redirect_stderr(self.import_stderr):
            spec.loader.exec_module(self.cli)

    def invoke(self, arguments):
        stdout, stderr = io.StringIO(), io.StringIO()
        with contextlib.redirect_stdout(stdout), contextlib.redirect_stderr(stderr):
            try:
                code = self.cli.main(arguments)
            except SystemExit as error:
                code = error.code
        return code, stdout.getvalue(), stderr.getvalue()

    def success(self, arguments):
        code, stdout, stderr = self.invoke(arguments)
        self.assertEqual(code, 0, stderr)
        self.assertEqual(stderr, "")
        self.assertTrue(stdout.endswith("\n"))
        # json.loads rejects a banner, trailing status message, or multiple JSON values.
        return json.loads(stdout)

    def test_import_has_no_service_or_output_side_effects_and_no_hardware_imports(self):
        self.assertEqual(self.constructor_calls, [])
        self.assertEqual(self.import_stdout.getvalue(), "")
        self.assertEqual(self.import_stderr.getvalue(), "")

    def test_reports_preserve_partial_evidence_errors_as_machine_readable_json(self):
        result = self.success(["reports", "--directory", "evidence with spaces"])
        self.assertEqual(result, self.service.list_evidence.return_value)
        self.service.list_evidence.assert_called_once_with()
        self.assertEqual(self.constructor_calls[0]["report_dir"], "evidence with spaces")
        self.assertEqual(Path(self.constructor_calls[0]["recording_dir"]), CLI_PATH.parent / "recordings")

    def test_default_directories_are_anchored_to_module(self):
        self.success(["reports"])
        self.assertEqual(Path(self.constructor_calls[0]["report_dir"]), CLI_PATH.parent / "test_results")
        self.assertEqual(Path(self.constructor_calls[0]["recording_dir"]), CLI_PATH.parent / "recordings")

    def test_compare_passes_identifiers_and_directory_without_reinterpreting_them(self):
        result = self.success(["compare", "left-run", "right-run", "--directory", "archived reports"])
        self.service.compare_evidence.assert_called_once_with("left-run", "right-run")
        self.assertEqual(result, self.service.compare_evidence.return_value)
        self.assertEqual(self.constructor_calls[0]["report_dir"], "archived reports")

    def test_recordings_retains_simulation_metadata_and_uses_capture_directory(self):
        result = self.success(["recordings", "--directory", "saved captures"])
        self.assertEqual(result, {"recordings": self.service.capture.list_recordings.return_value})
        self.service.capture.list_recordings.assert_called_once_with()
        self.assertEqual(self.constructor_calls[0]["recording_dir"], "saved captures")
        self.assertEqual(Path(self.constructor_calls[0]["report_dir"]), CLI_PATH.parent / "test_results")

    def test_replay_preserves_observer_snapshot_and_historical_tx_without_sending(self):
        result = self.success(["replay", "SIMULATED-fixture", "--directory", "recorded inputs", "--until", "2.5", "--json"])
        self.service.replay.assert_called_once_with("SIMULATED-fixture", until_s=2.5)
        self.assertEqual(result, self.service.replay.return_value)
        self.assertEqual(result["mode"], "replay")
        self.assertTrue(result["metadata"]["simulated"])
        self.assertEqual(result["tx"], [{"t": 2.0, "line": "historical only"}])
        self.assertEqual(self.constructor_calls[0]["recording_dir"], "recorded inputs")

    def test_replay_without_until_requests_full_recording(self):
        self.success(["replay", "SIMULATED-fixture"])
        self.service.replay.assert_called_once_with("SIMULATED-fixture", until_s=None)

    def test_zero_replay_position_is_valid(self):
        self.success(["replay", "SIMULATED-fixture", "--until", "0"])
        self.service.replay.assert_called_once_with("SIMULATED-fixture", until_s=0.0)

    def test_invalid_replay_times_are_rejected_before_backend_is_constructed(self):
        for value in ("-0.1", "nan", "inf", "-inf", "not-a-number"):
            with self.subTest(value=value):
                code, stdout, stderr = self.invoke(["replay", "SIMULATED-fixture", "--until=" + value])
                self.assertEqual(code, 2)
                self.assertEqual(stdout, "")
                self.assertTrue(stderr)
        self.assertEqual(self.constructor_calls, [])
        self.service.replay.assert_not_called()

    def test_missing_or_unknown_commands_return_usage_error_without_backend(self):
        for arguments in ([], ["unknown"], ["compare", "left-only"], ["replay"]):
            with self.subTest(arguments=arguments):
                code, stdout, stderr = self.invoke(arguments)
                self.assertEqual(code, 2)
                self.assertEqual(stdout, "")
                self.assertTrue(stderr)
        self.assertEqual(self.constructor_calls, [])

    def test_backend_failures_go_only_to_stderr_and_exit_two(self):
        for error in (ValueError("invalid recording ID"), FileNotFoundError("recording does not exist"), RuntimeError("capture is unavailable")):
            with self.subTest(error=type(error).__name__):
                self.service.replay.side_effect = error
                code, stdout, stderr = self.invoke(["replay", "bad-recording"])
                self.assertEqual(code, 2)
                self.assertEqual(stdout, "")
                self.assertIn(str(error), stderr)
                self.assertNotIn("Traceback", stderr)

    def test_nonfinite_backend_data_cannot_leak_invalid_or_partial_json(self):
        for value in (float("nan"), float("inf"), float("-inf")):
            with self.subTest(value=value):
                self.service.list_evidence.return_value = {"reports": [{"score": value}], "errors": []}
                code, stdout, stderr = self.invoke(["reports"])
                self.assertEqual(code, 2)
                self.assertEqual(stdout, "")
                self.assertTrue(stderr)

    def test_backend_construction_error_follows_error_protocol(self):
        self.constructor_error = OSError("recording directory unavailable")
        code, stdout, stderr = self.invoke(["recordings"])
        self.assertEqual(code, 2)
        self.assertEqual(stdout, "")
        self.assertIn("recording directory unavailable", stderr)

    def test_unserializable_backend_data_cannot_emit_partial_output(self):
        self.service.list_evidence.return_value = {"reports": [object()], "errors": []}
        code, stdout, stderr = self.invoke(["reports"])
        self.assertEqual(code, 2)
        self.assertEqual(stdout, "")
        self.assertTrue(stderr)

    def test_script_entrypoint_obeys_json_and_exit_status_contract(self):
        # Replace only the backend module. Run the real __main__ block in a fresh
        # process to catch forgotten SystemExit and accidental stdout diagnostics.
        bootstrap = """
import runpy, sys, types
cli, fail = sys.argv[1:]
def reports():
    if fail == 'yes':
        raise ValueError('recorded fixture failure')
    return {'reports': [{'id': 'SIMULATED-fixture'}], 'errors': []}
backend = types.ModuleType('mechbot_operations')
backend.OperationsService = lambda *a, **k: types.SimpleNamespace(list_evidence=reports)
sys.modules['mechbot_operations'] = backend
sys.argv = [cli, 'reports']
runpy.run_path(cli, run_name='__main__')
"""
        for fail in ("no", "yes"):
            with self.subTest(fail=fail):
                result = subprocess.run([sys.executable, "-X", "utf8", "-B", "-c", bootstrap, str(CLI_PATH), fail],
                                        capture_output=True, text=True, encoding="utf-8", timeout=10, shell=False)
                if fail == "no":
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(json.loads(result.stdout)["reports"][0]["id"], "SIMULATED-fixture")
                    self.assertEqual(result.stderr, "")
                else:
                    self.assertEqual(result.returncode, 2)
                    self.assertEqual(result.stdout, "")
                    self.assertIn("recorded fixture failure", result.stderr)


if __name__ == "__main__":
    unittest.main()
