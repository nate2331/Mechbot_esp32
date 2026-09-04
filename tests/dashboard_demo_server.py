"""Local simulated ESP32 bridge for manual dashboard browser checks."""

import sys
import os
import tempfile
import time
import types
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

try:
    import serial  # noqa: F401
except ModuleNotFoundError:
    sys.modules["serial"] = types.SimpleNamespace(SerialException=OSError, Serial=None)

from mechbot_bridge import Bridge, Handler, PROVEN_BASELINE


class DemoSerial:
    def __init__(self, bridge):
        self.bridge = bridge
        self.commands = []

    def write(self, data):
        command = data.decode("ascii").strip()
        self.commands.append(command)
        if command.startswith("CFG SET "):
            _, _, key, value = command.split()
            self.bridge.demo_settings[key] = float(value)
        elif command.startswith("V ") and command != "V 0 0 0":
            _, forward, left, ccw = command.split()
            direction_sign = 1 if max(float(forward), float(left), float(ccw)) >= 0 else -1
            self.bridge.telemetry["encoders"][2] += 18 * direction_sign
            self.bridge.telemetry["encoders"][3] += 22 * direction_sign
            self.bridge.telemetry["encoder_updated"] = time.time()

    def flush(self):
        pass


class DemoBridge(Bridge):
    def __init__(self):
        super().__init__()
        self.demo_settings = dict(PROVEN_BASELINE, **{
            "heading-kp": 0.7, "heading-max": 0.3,
            "heading-deadband-deg": 1.5, "heading-sign": 1,
            "heading-enabled": 1,
        })
        self.serial = DemoSerial(self)
        self.telemetry.update({
            "serial_connected": True,
            "serial_port": "DEMO",
            "firmware": "ESP32_MECANUM_USB_IMU_NAV_V4 (simulated)",
            "updated": time.time(),
            "encoders": [0, 0, 18420, 22610],
            "encoder_updated": time.time(),
            "health": "H 12000 IMU 1 11800 0 1",
            "imu": "I 12000 simulated",
        })
        self.tuning_session = self._empty_session()
        self.session_file = Path(tempfile.gettempdir()) / "mechbot-dashboard-demo.json"

    def _read_settings(self):
        return dict(self.demo_settings)

    def snapshot(self):
        self.telemetry["updated"] = time.time()
        self.telemetry["encoder_updated"] = time.time()
        return super().snapshot()

    def command(self, command, wait=0):
        self.write(command)
        if command == "CFG GET":
            return [f"CFG {key} {value}" for key, value in self.demo_settings.items()]
        return [f"OK {command}"]


if __name__ == "__main__":
    bridge = DemoBridge()
    Handler.bridge = bridge
    print(f"Demo dashboard: http://127.0.0.1:8876 (PID {os.getpid()})", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 8876), Handler).serve_forever()
