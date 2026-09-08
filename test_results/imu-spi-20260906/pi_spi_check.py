#!/usr/bin/env python3
"""Stopped SPI-upload evidence capture. Never writes settings or commands motion."""
import argparse
import fcntl
import json
import math
from pathlib import Path
import time
import urllib.request

import serial

BASE = "http://127.0.0.1:8765"
PORT = "/dev/serial/by-id/usb-1a86_USB_Serial-if00-port0"
IDENTITY = "ESP32_MAKER_MECANUM_IMU_V1"
MAPPING = "Maker mapping: FL=M2 FR=M3 RL=M1 RR=M0; all encoders forward-positive"
SPI_HELP = "IMU: SPI SCK22 MISO21 MOSI32 CS33 INT26 RST25 WAKE16; relative yaw, NOT compass north"
PINS = {"FL": (17, 12), "FR": (14, 15), "RL": (4, 2), "RR": (27, 13)}
CFG_KEYS = {"pwm-fl", "pwm-fr", "pwm-rl", "pwm-rr", "heading-kp", "heading-max",
            "heading-deadband-deg", "heading-sign", "heading-enabled"}


def api(path, payload=None):
    body = None if payload is None else json.dumps(payload).encode()
    request = urllib.request.Request(BASE + path, data=body,
                                     headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=3) as response:
        return json.load(response)


def stopped_status(status, require_fresh=True):
    if (not status.get("serial_connected") or status.get("serial_port") != PORT
            or status.get("board", {}).get("id") != "maker"
            or status.get("firmware") not in (None, IDENTITY)
            or status.get("maintenance") or status.get("gamepad_connected")
            or status.get("deadman") or status.get("calibration_active")):
        raise RuntimeError("requires identified Maker, expected USB, and no gamepad/calibration/maintenance")
    if status.get("firmware") is None and status.get("help_identity") != MAPPING:
        raise RuntimeError("no exact Maker READY or mapping identity")
    for wheel in PINS:
        item = status.get("wheel_diagnostics", {}).get(wheel, {})
        if item.get("pwm") != 0:
            raise RuntimeError("nonzero or missing output diagnostic: " + wheel)
        age = time.time() - item.get("updated", 0)
        if require_fresh and not 0 <= age <= 3:
            raise RuntimeError("output diagnostic is not fresh: " + wheel)


class Capture:
    def __init__(self, report, connection, deadline):
        self.report, self.connection, self.deadline = report, connection, deadline
        self.buffer = bytearray()
        self.cfg, self.diagnostics, self.pin_duties = {}, {}, {}
        self.mapping_seen = self.spi_seen = self.cfg_complete = False

    def send(self, command):
        if command not in ("X", "?", "CFG GET", "DIAG"):
            raise RuntimeError("command outside stopped capture allowlist")
        self.connection.write((command + "\n").encode("ascii"))
        self.connection.flush()
        self.report["transmitted"].append({"time": time.time(), "command": command})

    def query(self):
        for command in ("X", "?", "CFG GET", "DIAG"):
            self.send(command)

    def accept(self, line):
        row = {"time": time.time(), "elapsed_s": round(time.monotonic() - self.report["start_monotonic"], 4),
               "line": line}
        self.report["lines"].append(row)
        parts = line.split()
        if parts[:1] == ["READY"] and line != "READY " + IDENTITY:
            raise RuntimeError("unexpected READY identity: " + line)
        if line.startswith("Maker mapping:") and line != MAPPING:
            raise RuntimeError("unexpected Maker mapping: " + line)
        self.mapping_seen |= line == MAPPING
        self.spi_seen |= line == SPI_HELP
        if len(parts) == 3 and parts[0] == "CFG" and parts[1] in CFG_KEYS:
            if not math.isfinite(float(parts[2])):
                raise RuntimeError("nonfinite setting readback")
            self.cfg[parts[1]] = parts[2]
        self.cfg_complete |= line == "OK CFG GET"
        if parts[:1] == ["D"]:
            if (len(parts) != 10 or parts[1] not in PINS
                    or parts[2::2] != ["PWM", "A", "B", "INVALID"]):
                raise RuntimeError("malformed motor diagnostic: " + line)
            if float(parts[3]) != 0:
                raise RuntimeError("nonzero applied motor output: " + line)
            self.diagnostics[parts[1]] = row
        if parts[:1] == ["P"]:
            if (len(parts) != 14 or parts[1] not in PINS
                    or parts[2::2] != ["PIN", "DUTY", "HZ", "PIN", "DUTY", "HZ"]
                    or (int(parts[3]), int(parts[9])) != PINS[parts[1]]):
                raise RuntimeError("malformed or unexpected pin readback: " + line)
            if int(parts[5]) != 0 or int(parts[11]) != 0:
                raise RuntimeError("nonzero peripheral duty: " + line)
            self.pin_duties[parts[1]] = row

    def collect(self, seconds):
        end = min(time.monotonic() + seconds, self.deadline)
        while time.monotonic() < end:
            self.buffer.extend(self.connection.read(max(1, min(self.connection.in_waiting, 8192))))
            while b"\n" in self.buffer:
                raw, _, tail = self.buffer.partition(b"\n")
                self.buffer = bytearray(tail)
                line = raw.decode("ascii", "replace").strip()
                if line:
                    self.accept(line)
            if len(self.buffer) > 8192:
                raise RuntimeError("unterminated oversized serial record")
        if time.monotonic() >= self.deadline:
            raise RuntimeError("capture deadline exceeded")

    def ready(self, phase):
        return (self.mapping_seen and (phase == "before" or self.spi_seen)
                and self.cfg_complete and set(self.cfg) == CFG_KEYS
                and set(self.diagnostics) == set(PINS) and set(self.pin_duties) == set(PINS))


def summarize_imu(rows):
    valid, invalid, health = [], [], []
    reasons = {}
    for row in rows:
        parts = row["line"].split()
        if parts[:1] == ["I"]:
            try:
                numeric = (len(parts) == 13 and 0 <= int(parts[1]) <= 0xFFFFFFFF
                           and all(math.isfinite(float(x)) for x in parts[2:12])
                           and 0 <= int(parts[12]) <= 3
                           and sum(float(x) ** 2 for x in parts[2:6]) > 0)
            except (ValueError, OverflowError):
                numeric = False
            if numeric:
                valid.append(row)
            else:
                invalid.append(row)
                reason = parts[2] if len(parts) > 2 else "malformed"
                reasons[reason] = reasons.get(reason, 0) + 1
        if len(parts) == 7 and parts[0] == "H" and parts[2] == "IMU":
            try:
                health.append({"time": row["time"], "device_ms": int(parts[1]),
                               "available": int(parts[3]), "quaternion_ms": int(parts[4]),
                               "resets": int(parts[5]), "reinitializations": int(parts[6])})
            except ValueError:
                pass
    gaps = [b["time"] - a["time"] for a, b in zip(valid, valid[1:])]
    advancing = all(0 < ((int(b["line"].split()[1]) - int(a["line"].split()[1])) & 0xFFFFFFFF) < 0x80000000
                    for a, b in zip(valid, valid[1:]))
    changes = sum((a["resets"], a["reinitializations"]) != (b["resets"], b["reinitializations"])
                  for a, b in zip(health, health[1:]))
    span = valid[-1]["time"] - valid[0]["time"] if len(valid) > 1 else 0
    verified = (len(valid) >= 20 and not invalid and span >= 28 and advancing
                and bool(gaps) and max(gaps) <= 1.5 and len(health) >= 20
                and all(item["available"] == 1 for item in health) and changes == 0)
    return {"valid_count": len(valid), "invalid_count": len(invalid), "invalid_reasons": reasons,
            "numeric_span_s": span, "max_numeric_gap_s": max(gaps, default=None),
            "device_time_advancing": advancing, "health_count": len(health),
            "health_counter_changes": changes, "health_first": health[0] if health else None,
            "health_last": health[-1] if health else None, "hardware_spi_verified": verified}


def run(args):
    started = time.monotonic()
    report = {"phase": args.phase, "started": time.time(), "start_monotonic": started,
              "transmitted": [], "lines": [], "ok": False}
    connection = capture = None
    maintenance_requested = False
    lock = (Path.home() / ".mechbot-firmware-update.lock").open("a")
    try:
        fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        report["before"] = api("/api/status")
        stopped_status(report["before"])
        if api("/api/tuning").get("active"):
            raise RuntimeError("active tuning session")
        if api("/api/firmware").get("state") in ("starting", "running"):
            raise RuntimeError("firmware updater is active")
        api("/api/stop", {})
        maintenance_requested = True
        api("/api/maintenance", {"enabled": True})
        released = api("/api/status")
        if not released.get("maintenance") or released.get("serial_connected"):
            raise RuntimeError("bridge did not release serial ownership")
        connection = serial.Serial(port=None, baudrate=115200, timeout=0.05,
                                   write_timeout=0.5, exclusive=True)
        connection.dtr = connection.rts = False
        connection.port = PORT
        connection.open()
        capture = Capture(report, connection, started + 66)
        query_deadline = min(time.monotonic() + 18, started + 30)
        while time.monotonic() < query_deadline:
            capture.query()
            capture.collect(3)
            if capture.ready(args.phase):
                break
        report["settings"] = dict(capture.cfg)
        report["spi_help_confirmed"] = capture.spi_seen
        if not capture.ready(args.phase):
            raise RuntimeError("incomplete identity, CFG GET, or zero D/P readbacks")
        if args.phase == "after":
            first_row = len(report["lines"])
            for _ in range(10):
                capture.send("X")
                capture.send("DIAG")
                capture.collect(3)
            report["imu_observation"] = summarize_imu(report["lines"][first_row:])
        capture.send("X")
        capture.send("CFG GET")
        capture.send("DIAG")
        capture.collect(2)
        if report["settings"] != capture.cfg:
            raise RuntimeError("settings changed during stopped capture")
        report["zero_diagnostics"] = capture.diagnostics
        report["zero_pin_duties"] = capture.pin_duties
        report["ok"] = True
    except Exception as exc:
        report["error"] = str(exc)
    finally:
        if capture is not None:
            report["settings"] = dict(capture.cfg)
            report["spi_help_confirmed"] = capture.spi_seen
        if connection is not None:
            try:
                if connection.is_open:
                    connection.write(b"X\n")
                    connection.flush()
                    report["transmitted"].append({"time": time.time(), "command": "X"})
                connection.close()
            except Exception as exc:
                report["serial_close_error"] = str(exc)
                report["ok"] = False
        if maintenance_requested:
            try:
                api("/api/maintenance", {"enabled": False})
                end = min(time.monotonic() + 10, started + 80)
                last_error = None
                while time.monotonic() < end:
                    report["after"] = api("/api/status")
                    try:
                        stopped_status(report["after"])
                        if not report["after"].get("rearm_required"):
                            raise RuntimeError("bridge did not retain deadman rearm requirement")
                        last_error = None
                        break
                    except RuntimeError as exc:
                        last_error = str(exc)
                    time.sleep(0.25)
                api("/api/stop", {})
                if last_error or "after" not in report:
                    raise RuntimeError(last_error or "bridge restore deadline exceeded")
            except Exception as exc:
                report["restore_error"] = str(exc)
                report["ok"] = False
        lock.close()
        report["finished"] = time.time()
        report["elapsed_s"] = round(time.monotonic() - started, 3)
        report.pop("start_monotonic", None)
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        output.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps({key: report.get(key) for key in
                          ("phase", "ok", "error", "restore_error", "settings", "spi_help_confirmed",
                           "imu_observation", "elapsed_s")} | {"output": str(output)}), flush=True)
    return 0 if report["ok"] else 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--phase", choices=("before", "after"), required=True)
    parser.add_argument("--output", required=True)
    raise SystemExit(run(parser.parse_args()))
