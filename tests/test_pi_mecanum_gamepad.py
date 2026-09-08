import importlib.util
import io
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

    def test_cardinal_translation_is_full_scale(self):
        self.assertEqual(
            GAMEPAD.gamepad_motion(self.axes_for_motion(forward=0.60)),
            (1.0, 0.0, 0.0),
        )
        self.assertEqual(
            GAMEPAD.gamepad_motion(self.axes_for_motion(left=-0.70)),
            (0.0, -1.0, 0.0),
        )

    def test_dominant_translation_axis_wins(self):
        self.assertEqual(
            GAMEPAD.gamepad_motion(
                self.axes_for_motion(forward=0.80, left=0.55)
            ),
            (1.0, 0.0, 0.0),
        )

    def test_dominant_rotation_axis_wins(self):
        self.assertEqual(
            GAMEPAD.gamepad_motion(
                self.axes_for_motion(forward=0.50, rotation=-0.90)
            ),
            (0.0, 0.0, -1.0),
        )

    def test_deadzone_stops_motion(self):
        self.assertEqual(
            GAMEPAD.gamepad_motion(
                self.axes_for_motion(forward=0.20, left=0.20, rotation=0.34)
            ),
            (0.0, 0.0, 0.0),
        )

    def test_deadman_release_commands_zero_and_stop(self):
        axes = self.axes_for_motion(forward=1.0)
        self.assertEqual(
            GAMEPAD.commanded_motion(axes, False), (0.0, 0.0, 0.0)
        )
        self.assertEqual(
            GAMEPAD.commanded_motion(axes, True), (1.0, 0.0, 0.0)
        )

        serial_port = FakeSerial()
        GAMEPAD.handle_deadman_transition(serial_port, True, False)
        self.assertEqual(
            serial_port.writes,
            [b"V 0.000 0.000 0.000\n", b"X\n"],
        )

    def test_disconnect_and_stop_sequence(self):
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
