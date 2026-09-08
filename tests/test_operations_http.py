"""Offline HTTP adapter tests: no sockets, serial ports, or motor commands."""

import builtins
import importlib.util
import io
import json
from pathlib import Path
import types
import unittest
from unittest.mock import Mock, patch


HTTP_PATH = Path(__file__).resolve().parents[1] / "mechbot_http.py"


def load_adapter():
    original_import = builtins.__import__

    def offline_import(name, *args, **kwargs):
        if name.split(".")[0] in {"serial", "mechbot_bridge", "pi_mecanum_gamepad", "pi_mecanum_teleop"}:
            raise AssertionError(f"HTTP operations adapter must not import hardware module {name}")
        return original_import(name, *args, **kwargs)

    spec = importlib.util.spec_from_file_location("_operations_http_under_test", HTTP_PATH)
    module = importlib.util.module_from_spec(spec)
    with patch("builtins.__import__", side_effect=offline_import):
        spec.loader.exec_module(module)
    return module


class NoTransportBridge:
    """Only observation exists; accessing any hardware capability fails the test."""
    def __init__(self, operations, status):
        self.operations = operations
        self.snapshot = Mock(return_value=status)

    def __getattr__(self, name):
        raise AssertionError(f"operations routing must not access bridge transport/emergency capability: {name}")


class FakeHandler:
    def __init__(self, bridge):
        self.bridge = bridge
        self.replies = []

    def reply(self, status, payload):
        self.replies.append((status, payload))


class OperationsRouterTests(unittest.TestCase):
    def setUp(self):
        self.http = load_adapter()
        self.recording = {"id": "SIMULATED-01", "metadata": {"simulated": True}, "state": "stopped",
                          "dropped": 0, "events": [{"offset_s": 0.25, "direction": "tx", "line": "historical only"}]}
        self.operations = types.SimpleNamespace(
            snapshot=Mock(return_value={"observed": {"pose": {"valid": False}}, "capture": {"active": False}}),
            list_evidence=Mock(return_value={"reports": [{"id": "bench-01"}], "errors": [{"id": "partial", "error": "missing results"}]}),
            compare_evidence=Mock(return_value={"left": "bench-01", "right": "bench-02", "changes": []}),
            start_capture=Mock(return_value={"id": "capture-new", "active": True}),
            replay=Mock(return_value={"mode": "replay", "id": "SIMULATED-01", "position_s": 0.5,
                                      "observed": {"pose": {"valid": False}}, "tx": self.recording["events"]}),
            configure_geometry=Mock(return_value={"configured": True}),
            reset_pose=Mock(return_value={"pose": {"x_m": 0.0, "y_m": 0.0, "yaw_rad": 0.0}}),
            capture=types.SimpleNamespace(
                list_recordings=Mock(return_value=[{"id": "SIMULATED-01", "simulated": True}]),
                read_recording=Mock(return_value=self.recording),
                stop=Mock(return_value={"id": "capture-new", "active": False}),
            ),
        )
        self.status = {"simulated": False, "serial_connected": False, "firmware": None}
        self.bridge = NoTransportBridge(self.operations, self.status)
        self.handler = FakeHandler(self.bridge)

    def reply(self, expected=200):
        self.assertEqual(len(self.handler.replies), 1)
        status, payload = self.handler.replies[0]
        self.assertEqual(status, expected)
        json.dumps(payload, allow_nan=False)  # Responses must remain ordinary finite JSON.
        return payload

    def test_operations_get_preserves_service_and_bridge_snapshots_and_labels_mode(self):
        for simulated, mode in ((False, "live"), (True, "simulated")):
            with self.subTest(simulated=simulated):
                self.handler.replies.clear()
                self.status["simulated"] = simulated
                self.assertIs(self.http.handle_get(self.handler, "/api/operations"), True)
                payload = self.reply()
                self.assertEqual(payload["mode"], mode)
                self.assertEqual(payload["bridge"], self.status)
                self.assertEqual(payload["observed"], self.operations.snapshot.return_value["observed"])
                self.assertEqual(payload["capture"], {"active": False})

    def test_recording_list_and_raw_bundle_retain_simulation_and_historical_events(self):
        self.assertIs(self.http.handle_get(self.handler, "/api/recordings"), True)
        self.assertEqual(self.reply(), {"recordings": self.operations.capture.list_recordings.return_value})
        self.handler.replies.clear()
        self.assertIs(self.http.handle_get(self.handler, "/api/recordings/SIMULATED-01"), True)
        self.operations.capture.read_recording.assert_called_once_with("SIMULATED-01")
        self.assertEqual(self.reply(), self.recording)

    def test_evidence_get_preserves_partial_read_errors(self):
        self.assertIs(self.http.handle_get(self.handler, "/api/evidence"), True)
        self.assertEqual(self.reply(), self.operations.list_evidence.return_value)

    def test_capture_start_receives_observed_bridge_status_and_metadata_only(self):
        metadata = {"label": "bench observation", "surface": "mat", "battery_voltage": 11.7}
        self.assertIs(self.http.handle_post(self.handler, "/api/recordings/start", metadata), True)
        self.operations.start_capture.assert_called_once_with(self.status, metadata)
        self.assertEqual(self.reply(), self.operations.start_capture.return_value)

    def test_capture_stop_is_a_capture_action_without_transport_access(self):
        self.assertIs(self.http.handle_post(self.handler, "/api/recordings/stop", {}), True)
        self.operations.capture.stop.assert_called_once_with()
        self.assertEqual(self.reply(), self.operations.capture.stop.return_value)

    def test_replay_passes_position_and_returns_historical_tx_without_retransmission(self):
        self.assertIs(self.http.handle_post(self.handler, "/api/replay", {"id": "SIMULATED-01", "until_s": 0.5}), True)
        self.operations.replay.assert_called_once()
        args, kwargs = self.operations.replay.call_args
        self.assertEqual(args[0] if args else kwargs["recording_id"], "SIMULATED-01")
        self.assertEqual(args[1] if len(args) > 1 else kwargs["until_s"], 0.5)
        self.assertEqual(self.reply(), self.operations.replay.return_value)
        self.operations.start_capture.assert_not_called()
        self.operations.capture.stop.assert_not_called()
        self.operations.reset_pose.assert_not_called()

    def test_full_replay_uses_none_for_omitted_position(self):
        self.assertIs(self.http.handle_post(self.handler, "/api/replay", {"id": "SIMULATED-01"}), True)
        args, kwargs = self.operations.replay.call_args
        self.assertIsNone(args[1] if len(args) > 1 else kwargs.get("until_s"))
        self.reply()

    def test_compare_geometry_and_pose_reset_delegate_only_their_operation(self):
        geometry = {"wheel_radius_m": 0.05, "half_length_m": 0.15, "half_width_m": 0.1}
        cases = [
            ("/api/evidence/compare", {"left": "bench-01", "right": "bench-02"}, self.operations.compare_evidence, ("bench-01", "bench-02")),
            ("/api/geometry", geometry, self.operations.configure_geometry, (geometry,)),
            ("/api/odometry/reset", {}, self.operations.reset_pose, ()),
        ]
        for path, data, method, arguments in cases:
            with self.subTest(path=path):
                self.handler.replies.clear()
                self.assertIs(self.http.handle_post(self.handler, path, data), True)
                method.assert_called_once_with(*arguments)
                self.assertEqual(self.reply(), method.return_value)

    def test_unknown_routes_remain_available_to_the_existing_bridge_handler(self):
        self.assertIs(self.http.handle_get(self.handler, "/api/status"), False)
        self.assertIs(self.http.handle_post(self.handler, "/api/drive", {"vx": 0}), False)
        self.assertEqual(self.handler.replies, [])
        self.bridge.snapshot.assert_not_called()
        self.operations.snapshot.assert_not_called()

    def test_get_backend_failures_become_json_400(self):
        for error in (ValueError("invalid report"), OSError("cannot read report"), RuntimeError("busy"), TypeError("bad metadata")):
            with self.subTest(error=type(error).__name__):
                self.handler.replies.clear()
                self.operations.list_evidence.side_effect = error
                self.assertIs(self.http.handle_get(self.handler, "/api/evidence"), True)
                self.assertIn(str(error), self.reply(400)["error"])

    def test_post_backend_failures_become_json_400_without_motion_recovery(self):
        for error in (ValueError("invalid capture"), OSError("recording unavailable"), RuntimeError("busy"), TypeError("bad recording")):
            with self.subTest(error=type(error).__name__):
                self.handler.replies.clear()
                self.operations.replay.side_effect = error
                self.assertIs(self.http.handle_post(self.handler, "/api/replay", {"id": "bad-id"}), True)
                self.assertIn(str(error), self.reply(400)["error"])


class BoundedStream(io.BytesIO):
    def __init__(self, value):
        super().__init__(value)
        self.read_sizes = []

    def read(self, size=-1):
        if size < 0:
            raise AssertionError("HTTP body reads must be bounded")
        self.read_sizes.append(size)
        return super().read(size)


class JsonBodyTests(unittest.TestCase):
    def setUp(self):
        self.http = load_adapter()

    def handler(self, raw, length=None):
        headers = {"Content-Length": str(len(raw)) if length is None else length}
        return types.SimpleNamespace(headers=headers, rfile=BoundedStream(raw))

    def test_normal_utf8_object_is_read_with_exact_declared_bound(self):
        value = {"label": "SIMULATED — café", "enabled": False, "until_s": 0, "nested": [1, None]}
        raw = json.dumps(value, ensure_ascii=False).encode("utf-8")
        handler = self.handler(raw)
        self.assertEqual(self.http.read_json_body(handler), value)
        self.assertTrue(handler.rfile.read_sizes)
        self.assertTrue(all(size <= len(raw) for size in handler.rfile.read_sizes))

    def test_exact_limit_object_is_allowed_and_larger_declaration_is_rejected_before_read(self):
        raw = b'{"x":"' + b"a" * (16384 - 8) + b'"}'
        self.assertEqual(len(raw), 16384)
        self.assertEqual(len(self.http.read_json_body(self.handler(raw))["x"]), 16384 - 8)
        handler = self.handler(b"{}", "16385")
        with self.assertRaises(ValueError):
            self.http.read_json_body(handler)
        self.assertEqual(handler.rfile.read_sizes, [])

    def test_custom_smaller_limit_is_enforced_before_reading(self):
        handler = self.handler(b'{"long":"value"}')
        with self.assertRaises(ValueError):
            self.http.read_json_body(handler, max_bytes=8)
        self.assertEqual(handler.rfile.read_sizes, [])

    def test_missing_or_invalid_content_length_is_rejected_without_read(self):
        for length in ("-1", "+2", "2.0", "1_0", "two", "", " 2 ", "\u0662"):
            with self.subTest(length=length):
                handler = self.handler(b"{}", length)
                with self.assertRaises(ValueError):
                    self.http.read_json_body(handler)
                self.assertEqual(handler.rfile.read_sizes, [])
        handler = self.handler(b"{}")
        handler.headers.clear()
        with self.assertRaises(ValueError):
            self.http.read_json_body(handler)
        self.assertEqual(handler.rfile.read_sizes, [])

    def test_truncated_declared_body_is_rejected(self):
        with self.assertRaises(ValueError):
            self.http.read_json_body(self.handler(b"{}", "3"))

    def test_body_reader_does_not_consume_bytes_after_declared_length(self):
        handler = self.handler(b"{}next-request", "2")
        self.assertEqual(self.http.read_json_body(handler), {})
        self.assertEqual(handler.rfile.tell(), 2)

    def test_invalid_json_utf8_and_nonobject_values_are_rejected(self):
        for raw in (b"", b"{", b"{} {}", b'{"x":1,}', b'"text"', b"[]", b"null", b"true", b"123", b'{"x":"\xff"}'):
            with self.subTest(raw=raw):
                with self.assertRaises(ValueError):
                    self.http.read_json_body(self.handler(raw))

    def test_nonfinite_numbers_are_rejected_at_any_depth_including_float_overflow(self):
        for raw in (b'{"x":NaN}', b'{"x":Infinity}', b'{"x":-Infinity}', b'{"x":1e999}',
                    b'{"nested":[{"x":-1e999}]}'):
            with self.subTest(raw=raw):
                with self.assertRaises(ValueError):
                    self.http.read_json_body(self.handler(raw))


if __name__ == "__main__":
    unittest.main()
