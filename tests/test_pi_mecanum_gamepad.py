import importlib.util
import io
import math
from pathlib import Path
import sys
import types
import unittest


SERIAL_STUB = types.ModuleType("serial")
SERIAL_STUB.SerialException = type("SerialException", (Exception,), {})
SERIAL_STUB.Serial = object
sys.modules.setdefault("serial", SERIAL_STUB)

MODULE_PATH = Path(__file__).resolve().parents[1] / "pi_mecanum_gamepad.py"
SPEC = importlib.util.spec_from_file_location("pi_mecanum_gamepad", MODULE_PATH)
GAMEPAD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GAMEPAD)


def raw_axis(value):
    return round(max(-1.0, min(1.0, value)) * 32767)


class FakeSerial:
    def __init__(self):
        self.writes = []
        self.flushed = False

    def write(self, value):
        self.writes.append(value)

    def flush(self):
        self.flushed = True


class GamepadMotionTest(unittest.TestCase):
    def blank_axes(self):
        return [0] * 9

    def axes_for_motion(self, forward=0.0, left=0.0, rotation=0.0):
        axes = self.blank_axes()
        axes[GAMEPAD.AXIS_FORWARD] = raw_axis(-forward)
        axes[GAMEPAD.AXIS_STRAFE] = raw_axis(-left)
        axes[GAMEPAD.AXIS_ROTATION] = raw_axis(-rotation)
        return axes

    def assert_motion_close(self, actual, expected, places=4):
        for actual_value, expected_value in zip(actual, expected):
            self.assertAlmostEqual(actual_value, expected_value, places=places)

    def test_cardinal_translation(self):
        self.assert_motion_close(
            GAMEPAD.gamepad_motion(self.axes_for_motion(forward=1.0)),
            (1.0, 0.0, 0.0),
        )
        self.assert_motion_close(
            GAMEPAD.gamepad_motion(self.axes_for_motion(left=1.0)),
            (0.0, 1.0, 0.0),
        )

    def test_diagonal_translation_preserves_both_axes(self):
        magnitude = 1.0
        angle = math.radians(15.0)
        expected = (magnitude * math.cos(angle), magnitude * math.sin(angle), 0.0)
        actual = GAMEPAD.gamepad_motion(
            self.axes_for_motion(forward=expected[0], left=expected[1])
        )
        self.assert_motion_close(actual, expected)
        self.assertAlmostEqual(math.atan2(actual[1], actual[0]), angle, places=4)

    def test_partial_magnitude_is_not_promoted_to_full_scale(self):
        magnitude = 0.60
        angle = math.radians(30.0)
        expected = (magnitude * math.cos(angle), magnitude * math.sin(angle), 0.0)
        actual = GAMEPAD.gamepad_motion(
            self.axes_for_motion(forward=expected[0], left=expected[1])
        )
        self.assert_motion_close(actual, expected)
        self.assertAlmostEqual(math.hypot(actual[0], actual[1]), magnitude, places=4)

    def test_right_stick_rotation_mixes_with_translation(self):
        expected = (0.80, 0.30, 0.50)
        actual = GAMEPAD.gamepad_motion(
            self.axes_for_motion(
                forward=expected[0], left=expected[1], rotation=expected[2]
            )
        )
        self.assert_motion_close(actual, expected)

    def test_radial_translation_and_rotation_deadzone(self):
        axes = self.axes_for_motion(forward=0.20, left=0.20, rotation=0.34)
        self.assertEqual(GAMEPAD.gamepad_motion(axes), (0.0, 0.0, 0.0))

    def test_deadman_release_commands_zero(self):
        axes = self.axes_for_motion(forward=1.0, left=0.25, rotation=0.50)
        self.assertEqual(GAMEPAD.commanded_motion(axes, False), (0.0, 0.0, 0.0))
        self.assertNotEqual(GAMEPAD.commanded_motion(axes, True), (0.0, 0.0, 0.0))

        serial_port = FakeSerial()
        GAMEPAD.handle_deadman_transition(serial_port, True, False)
        self.assertEqual(
            serial_port.writes,
            [b"V 0.000 0.000 0.000\n", b"X\n"],
        )

    def test_disconnect_event_and_stop_sequence(self):
        with self.assertRaisesRegex(RuntimeError, "Gamepad disconnected"):
            GAMEPAD.read_joystick_event(io.BytesIO(b""))

        serial_port = FakeSerial()
        GAMEPAD.send_stop(serial_port)
        self.assertEqual(
            serial_port.writes,
            [b"V 0.000 0.000 0.000\n", b"X\n"],
        )
        self.assertTrue(serial_port.flushed)


class TelemetryParsingTest(unittest.TestCase):
    def test_existing_encoder_and_imu_parsing(self):
        telemetry = {}
        GAMEPAD.parse_telemetry_line("T 100 1 2 3 4", telemetry)
        GAMEPAD.parse_telemetry_line(
            "I 101 0 0 0 1 0.1 0.2 0.3 1.0 2.0 3.0 3", telemetry
        )
        self.assertEqual(telemetry["encoder"]["counts"], (1, 2, 3, 4))
        self.assertEqual(telemetry["imu"]["state"], "OK")
        self.assertEqual(telemetry["imu"]["status"], 3)


if __name__ == "__main__":
    unittest.main()
