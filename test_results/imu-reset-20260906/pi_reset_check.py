"""One stopped RST25 retry after Pi reconnection; never issues motion commands."""
import json
import time
import urllib.request
import serial


def api(path, payload=None):
    body = None if payload is None else json.dumps(payload).encode()
    request = urllib.request.Request(
        "http://127.0.0.1:8765" + path, data=body,
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=6) as response:
        return json.load(response)


report = {"started": time.time(), "transmitted": [], "lines": []}
maintenance_requested = False
connection = None


def send(command):
    connection.write((command + "\n").encode("ascii"))
    connection.flush()
    report["transmitted"].append(command)


def collect(seconds):
    until = time.monotonic() + seconds
    while time.monotonic() < until:
        raw = connection.read_until(b"\n", 2048)
        if raw:
            report["lines"].append(raw.decode("utf-8", "replace").strip())


try:
    before = api("/api/status")
    report["before"] = before
    if (not before.get("serial_connected") or before.get("board", {}).get("id") != "maker"
            or before.get("maintenance") or before.get("calibration_active")
            or before.get("gamepad_connected") or before.get("deadman")):
        raise RuntimeError("Requires stopped Maker with no gamepad, tuning or maintenance")
    for wheel in ("FL", "FR", "RL", "RR"):
        diagnostic = before.get("wheel_diagnostics", {}).get(wheel, {})
        if diagnostic.get("pwm") != 0 or time.time() - diagnostic.get("updated", 0) > 3:
            raise RuntimeError("Requires fresh zero-output diagnostics")
    port = before["serial_port"]
    if port != "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0":
        raise RuntimeError("Serial adapter differs from inspected Maker connection")
    api("/api/stop", {})
    report["settings_before"] = api("/api/settings")
    maintenance_requested = True
    api("/api/maintenance", {"enabled": True})
    released = api("/api/status")
    if not released.get("maintenance") or released.get("serial_connected"):
        raise RuntimeError("Bridge did not release serial ownership")
    connection = serial.Serial(port=None, baudrate=115200, timeout=0.1,
                               write_timeout=0.5, exclusive=True)
    connection.dtr = False
    connection.rts = False
    connection.port = port
    connection.open()
    send("X")
    send("?")
    collect(2)
    if not any("IMU: SDA21 SCL22 RST25;" in line for line in report["lines"]):
        raise RuntimeError("Running firmware did not confirm GPIO25 reset support")
    send("IMU RETRY")
    collect(10)
    send("X")
    send("DIAG")
    send("CFG GET")
    collect(3)
except Exception as exc:
    report["error"] = str(exc)
finally:
    if connection is not None:
        connection.close()
    if maintenance_requested:
        try:
            api("/api/maintenance", {"enabled": False})
            until = time.monotonic() + 12
            while time.monotonic() < until:
                time.sleep(0.5)
                report["after"] = api("/api/status")
                if (report["after"].get("serial_connected")
                        and report["after"].get("board", {}).get("id") == "maker"):
                    break
            api("/api/stop", {})
            report["settings_after"] = api("/api/settings")
            report["after"] = api("/api/status")
        except Exception as exc:
            report["restore_error"] = str(exc)
    report["finished"] = time.time()
    print(json.dumps(report, indent=2), flush=True)
