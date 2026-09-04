#!/usr/bin/env python3
"""Compile, flash, and verify MechBot firmware through the local bridge."""
import json, os, subprocess, sys, time, urllib.request

BASE = "http://127.0.0.1:8765"
CLI = os.path.expanduser("~/.local/bin/arduino-cli")
SKETCH = os.path.expanduser("~/mechbot-src/Mechbot_IMU_ESP32")
PORT = "/dev/serial/by-id/usb-1a86_USB_Single_Serial_58CF094084-if00"
FQBN = "esp32:esp32:esp32s3"
EXPECTED = "ESP32_MECANUM_USB_IMU_NAV_V4"

def api(path, payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(BASE + path, data=data, headers={"Content-Type":"application/json"})
    return json.load(urllib.request.urlopen(req, timeout=5))

def main():
    print("Compiling firmware with one job...")
    subprocess.run([CLI, "compile", "--jobs", "1", "--fqbn", FQBN, SKETCH], check=True)
    api("/api/maintenance", {"enabled": True})
    try:
        time.sleep(1)
        print("Uploading over USB...")
        subprocess.run([CLI, "upload", "-p", PORT, "--fqbn", FQBN, SKETCH], check=True)
    finally:
        api("/api/maintenance", {"enabled": False})
    deadline = time.time() + 15
    while time.time() < deadline:
        status = api("/api/status")
        if (EXPECTED in status.get("firmware", "") or
                any(line.startswith("H ") for line in status.get("recent_lines", []))):
            print("Verified", EXPECTED)
            return
        time.sleep(1)
    raise RuntimeError("flash completed but READY V4 was not verified")

if __name__ == "__main__":
    try: main()
    except Exception as exc:
        print("ERROR:", exc, file=sys.stderr); sys.exit(1)
