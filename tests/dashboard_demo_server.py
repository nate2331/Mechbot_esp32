"""Local simulated ESP32 bridge for manual dashboard browser checks."""

import sys
import argparse
import os
import tempfile
import time
import types
import math
import threading
import ipaddress
from http.server import ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

try:
    import serial  # noqa: F401
except ModuleNotFoundError:
    sys.modules["serial"] = types.SimpleNamespace(SerialException=OSError, Serial=None)

from mechbot_bridge import Bridge, Handler
from mechbot_profiles import PROFILES
from mechbot_operations import OperationsService


class DemoSerial:
    def __init__(self, bridge):
        self.bridge = bridge
        self.commands = []

    def write(self, data):
        command = data.decode("ascii").strip()
        self.commands.append(command)
        del self.commands[:-300]
        if command.startswith("CFG SET "):
            _, _, key, value = command.split()
            self.bridge.demo_settings[key] = float(value)
        elif command.startswith("V ") and command != "V 0 0 0":
            _, forward, left, ccw = command.split()
            direction_sign = 1 if max(float(forward), float(left), float(ccw)) >= 0 else -1
            for index, wheel in enumerate(("FL", "FR", "RL", "RR")):
                if wheel in self.bridge.board_profile()["encoder_wheels"]:
                    self.bridge.telemetry["encoders"][index] += (18 + index) * direction_sign
            self.bridge.telemetry["encoder_updated"] = time.time()

    def flush(self):
        pass


class DemoBridge(Bridge):
    def __init__(self, board="maker", data_dir=None):
        self.demo_data_dir = Path(data_dir or tempfile.mkdtemp(prefix="mechbot-demo-"))
        super().__init__(operations=OperationsService(
            self.demo_data_dir / "recordings", Path(__file__).resolve().parents[1] / "test_results"))
        self.demo_started = time.monotonic()
        self.demo_last_tick = self.demo_started
        self.demo_counts = [0.0] * 4
        self.demo_edges = [0.0] * 4
        self.demo_settings = dict(PROFILES[board]["baseline"], **{
            "heading-kp": 0.7, "heading-max": 0.3,
            "heading-deadband-deg": 1.5, "heading-sign": 1,
            "heading-enabled": 0,
        })
        self.serial = DemoSerial(self)
        self.telemetry.update({
            "serial_connected": True,
            "simulated": True,
            "serial_port": "DEMO",
            "firmware": PROFILES[board]["firmware"],
            "updated": time.time(),
            "encoders": [0, 0, 18420, 22610],
            "encoder_updated": time.time(),
            "health": "H 12000 IMU 1 11800 0 1",
            "imu": "I 12000 0 0 0 1 0 0 0 0 0 0 3",
            "imu_valid": True, "imu_updated": time.time(),
        })
        self.tuning_session = self._empty_session()
        self._restore_pending = False
        self.session_file = Path(tempfile.gettempdir()) / f"mechbot-dashboard-demo-{os.getpid()}.json"
        self.parse_line("READY " + PROFILES[board]["firmware"])

    def _read_settings(self):
        return dict(self.demo_settings)

    def snapshot(self):
        with self.lock:
            self.simulate_tick()
            return super().snapshot()

    def simulate_tick(self):
        """Clearly synthetic wheel/IMU activity, without a serial connection."""
        now = time.monotonic()
        dt = min(0.25, now - self.demo_last_tick)
        if dt < 0.04:
            return
        self.demo_last_tick = now
        elapsed = now - self.demo_started
        phase = elapsed % 24
        base = 700 * math.sin(elapsed * 0.55) if phase < 20 else 0
        for i, wheel in enumerate(("FL", "FR", "RL", "RR")):
            rate = base * (1, .97, .82, 1.02)[i] + (12 * math.sin(elapsed * 3 + i) if base else 0)
            if wheel not in self.board_profile()["encoder_wheels"]:
                rate = 0
            self.demo_counts[i] += rate * dt
            self.demo_edges[i] += abs(rate) * dt
        milliseconds = int(elapsed * 1000) & 0xffffffff
        self.parse_line(f"T {milliseconds} " + " ".join(str(round(c)) for c in self.demo_counts))
        if 12 <= phase < 15:
            self.parse_line(f"I {milliseconds} WAIT")
        else:
            yaw = .3 * math.sin(elapsed * .2)
            self.parse_line(f"I {milliseconds} 0 0 {math.sin(yaw/2):.6f} {math.cos(yaw/2):.6f} 0 0 0.06 0 0 9.81 3")
        if self.board_profile()["diagnostics"]:
            for i, wheel in enumerate(("FL", "FR", "RL", "RR")):
                invalid = int(elapsed / 6) if wheel == "RL" else 0
                edges = round(self.demo_edges[i])
                pwm = round(base / 700 * 150)
                self.parse_line(f"D {wheel} PWM {pwm} A {edges} B {edges} INVALID {invalid}")

    def start_firmware_update(self):
        raise RuntimeError("Simulation only: firmware upload is disabled")

    def command(self, command, wait=0):
        self.write(command)
        if command == "CFG GET":
            return [f"CFG {key} {value}" for key, value in self.demo_settings.items()]
        return [f"OK {command}"]


def lan_address(value):
    """Opt in to one explicit RFC1918 interface; never bind every interface."""
    try:
        address = ipaddress.IPv4Address(value)
    except ipaddress.AddressValueError as exc:
        raise argparse.ArgumentTypeError("use the PC's private IPv4 address") from exc
    networks = ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16')
    if not any(address in ipaddress.IPv4Network(network) for network in networks):
        raise argparse.ArgumentTypeError("LAN address must be a private 10.x, 172.16-31.x or 192.168.x address")
    return str(address)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=PROFILES, default="maker")
    parser.add_argument("--port", type=int, default=8876)
    parser.add_argument("--data-dir", help="Demo recordings directory; defaults to a temporary folder")
    parser.add_argument("--lan-address", type=lan_address,
                        help="Also serve this simulator on the PC's specified private IPv4 address")
    args = parser.parse_args()
    bridge = DemoBridge(args.board, args.data_dir)
    Handler.bridge = bridge
    def simulate():
        while bridge.running:
            with bridge.lock:
                bridge.simulate_tick()
            time.sleep(0.05)
    servers = []
    try:
        addresses = ['127.0.0.1'] + ([args.lan_address] if args.lan_address else [])
        for address in addresses:
            servers.append(ThreadingHTTPServer((address, args.port), Handler))
        threading.Thread(target=simulate, daemon=True).start()
        for server in servers[1:]:
            threading.Thread(target=server.serve_forever, daemon=True).start()
        for address in addresses:
            print(f"SIMULATED {args.board} dashboard: http://{address}:{args.port}/ (PID {os.getpid()})", flush=True)
        servers[0].serve_forever()
    finally:
        bridge.running = False
        bridge.operations.capture.stop()
        for server in servers:
            server.server_close()
