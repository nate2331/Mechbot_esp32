#!/usr/bin/env python3
"""Persistent MechBot serial/gamepad bridge and local tuning API."""

import argparse
import copy
import csv
import glob
import io
import json
import math
import os
import select
import struct
import subprocess
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import urlparse

import serial
from mechbot_profiles import MAKER_HELP_IDENTITY, profile_for_firmware, require_profile
from mechbot_operations import OperationsService
from mechbot_http import handle_get, handle_post, read_json_body, validate_origin

SEND_HZ = 20.0
AXIS_STRAFE, AXIS_FORWARD, AXIS_ROTATION = 0, 1, 3
BUTTON_DEADMAN = 4
JS_EVENT_BUTTON, JS_EVENT_AXIS, JS_EVENT_INIT = 0x01, 0x02, 0x80
JS_EVENT_FORMAT = "<IhBB"
JS_EVENT_SIZE = struct.calcsize(JS_EVENT_FORMAT)
DIRECTION_THRESHOLD = 0.35
ALLOWED_SETTINGS = {
    "pwm-fl", "pwm-fr", "pwm-rl", "pwm-rr", "heading-kp",
    "heading-max", "heading-deadband-deg", "heading-sign", "heading-enabled",
}
PWM_KEYS = ("pwm-fl", "pwm-fr", "pwm-rl", "pwm-rr")
MOTOR_NAMES = ("FL", "FR", "RL", "RR")
CALIBRATION_DIRECTIONS = {
    "forward": (1.0, 0.0, 0.0), "reverse": (-1.0, 0.0, 0.0),
    "left": (0.0, 1.0, 0.0), "right": (0.0, -1.0, 0.0),
    "ccw": (0.0, 0.0, 1.0), "cw": (0.0, 0.0, -1.0),
}
MIN_TEST_MAGNITUDE = 0.15
MAX_TEST_MAGNITUDE = 1.0
DEFAULT_TEST_MAGNITUDE = 0.45
DASHBOARD_DIR = Path(__file__).with_name("dashboard")
SESSION_FILE = Path(os.environ.get("MECHBOT_SESSION_FILE", Path(__file__).with_name("tuning-session.json")))
SESSION_TIMEOUT_SECONDS = 30 * 60


def normalized(value):
    return max(-1.0, min(1.0, value / 32767.0))


def gamepad_motion(axes):
    choices = [(abs(normalized(axes[AXIS_FORWARD])), "forward"),
               (abs(normalized(axes[AXIS_STRAFE])), "strafe"),
               (abs(normalized(axes[AXIS_ROTATION])), "rotation")]
    magnitude, direction = max(choices)
    if magnitude < DIRECTION_THRESHOLD:
        return 0.0, 0.0, 0.0
    if direction == "forward":
        return (-1.0 if axes[AXIS_FORWARD] > 0 else 1.0), 0.0, 0.0
    if direction == "strafe":
        return 0.0, (-1.0 if axes[AXIS_STRAFE] > 0 else 1.0), 0.0
    return 0.0, 0.0, (-1.0 if axes[AXIS_ROTATION] > 0 else 1.0)


class Bridge:
    def __init__(self, port=None, joystick="/dev/input/js0", operations=None):
        self.requested_port = port
        self.joystick_path = joystick
        self.lock = threading.RLock()
        self.serial = None
        self.joystick = None
        self.axes = [0] * 9
        self.buttons = [0] * 16
        self.lines = []
        self._line_sequence = 0
        self._responses = []
        self.telemetry = {"serial_connected": False, "gamepad_connected": False,
                          "deadman": False, "last_line": None, "updated": None}
        self.running = True
        self.maintenance = False
        self.firmware_job = {"state": "idle", "started": None, "finished": None,
                             "returncode": None, "lines": []}
        self.firmware_process = None
        self.calibration = {"active": False, "running": False, "phase": "idle",
                            "direction": None, "started": None, "duration_ms": 0,
                            "magnitude": DEFAULT_TEST_MAGNITUDE, "elapsed_ms": 0,
                            "before": None, "after": None, "deltas": None,
                            "live_deltas": None, "error": None, "aborted": False}
        self.calibration_abort = threading.Event()
        self.session_file = SESSION_FILE
        self.tuning_session = self._load_session()
        self._restore_pending = bool(self.tuning_session.get("active"))
        self.rearm_required = True
        self._next_diagnostics = 0.0
        self._next_identity_query = 0.0
        self.operations_error = None
        self.operations = operations if operations is not None else OperationsService(
            recording_dir=os.environ.get('MECHBOT_RECORDING_DIR', Path(__file__).with_name('recordings')),
            report_dir=os.environ.get('MECHBOT_REPORT_DIR', Path(__file__).with_name('test_results')),
            geometry_file=os.environ.get('MECHBOT_GEOMETRY_FILE', Path(__file__).with_name('measured-geometry.json')))

    def board_profile(self):
        return profile_for_firmware(self.telemetry.get("firmware"), self.telemetry.get("help_identity"))

    def _require_board(self):
        if not self.serial or not self.telemetry.get("serial_connected"):
            raise RuntimeError("ESP32 is disconnected")
        return require_profile(self.telemetry.get("firmware"), self.telemetry.get("help_identity"))

    def _require_session_board(self):
        board = self._require_board()
        if self.tuning_session.get("board_id") != board["id"]:
            raise RuntimeError("session belongs to another or unidentified board; original settings were not applied")
        return board

    def _empty_session(self):
        return {"active": False, "workflow": None, "started": None, "updated": None,
                "original_settings": {}, "live_settings": {}, "saved_settings": {},
                "tests": [], "adjustments": [], "best_test_id": None,
                "best_test_ids": {}, "phase": "idle", "error": None}

    def _load_session(self):
        try:
            data = json.loads(self.session_file.read_text(encoding="utf-8"))
            return {**self._empty_session(), **data}
        except (OSError, ValueError, TypeError):
            return self._empty_session()

    def _persist_session(self):
        self.tuning_session["updated"] = time.time()
        temp = self.session_file.with_suffix(".tmp")
        temp.write_text(json.dumps(self.tuning_session, indent=2), encoding="utf-8")
        temp.replace(self.session_file)

    def tuning_status(self):
        with self.lock:
            board = self.board_profile()
            return copy.deepcopy(dict(self.tuning_session, proven_baseline=board["baseline"],
                        board=board,
                        calibration=dict(self.calibration), pwm_keys=PWM_KEYS,
                        test_limits={"magnitude_min": MIN_TEST_MAGNITUDE,
                                     "magnitude_max": MAX_TEST_MAGNITUDE,
                                     "duration_min_ms": 500,
                                     "duration_max_ms": 3000}))

    def export_tuning(self, format):
        """Read-only snapshot; rates are pulse averages, not steady wheel speeds."""
        with self.lock:
            data = copy.deepcopy(self.tuning_session)
        if format == "json":
            return json.dumps({"schema": 1, "session": data}, indent=2, allow_nan=False)
        if format != "csv":
            raise ValueError("export format must be json or csv")
        output = io.StringIO(newline="")
        fields = ["board_id", "firmware", "test_id", "timestamp", "direction",
                  "duration_ms", "magnitude", "wheel", "pwm", "requested_duty",
                  "delta_ticks", "pulse_average_ticks_s", "score", "heading", "path", "notes"]
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        for test in data.get("tests", []):
            for index, wheel in enumerate(MOTOR_NAMES):
                key = PWM_KEYS[index]
                row = dict(board_id=data.get("board_id"), firmware=data.get("firmware"),
                           test_id=test["id"], timestamp=test.get("timestamp"),
                           direction=test.get("command"), duration_ms=test.get("duration_ms"),
                           magnitude=test.get("magnitude"), wheel=wheel,
                           pwm=test.get("settings", {}).get(key),
                           requested_duty=test.get("actual_duty", {}).get(key),
                           delta_ticks=(test.get("deltas") or [None]*4)[index],
                           pulse_average_ticks_s=test.get("encoder_rates", {}).get(wheel),
                           score=test.get("score"), heading=test.get("heading"),
                           path=test.get("path"), notes=test.get("notes", ""))
                # Keep user notes literal when opened in spreadsheet software.
                for name, value in row.items():
                    if isinstance(value, str) and value.lstrip().startswith(("=", "+", "-", "@")):
                        row[name] = "'" + value
                writer.writerow(row)
        return output.getvalue()

    def _read_settings(self):
        lines = self.command("CFG GET")
        values = {}
        for line in lines:
            parts = line.split(maxsplit=2)
            if len(parts) == 3 and parts[0] == "CFG" and parts[1] in ALLOWED_SETTINGS:
                try:
                    value = float(parts[2])
                    if math.isfinite(value): values[parts[1]] = value
                except ValueError: pass
        return values

    def start_tuning(self, workflow, confirmation):
        if workflow not in ("bench", "floor", "imu"):
            raise ValueError("invalid tuning workflow")
        required_confirmation = "WHEELS_UP" if workflow == "bench" else "AREA_CLEAR"
        if confirmation != required_confirmation:
            raise ValueError("safety confirmation required")
        with self.lock:
            board = self._require_board()
            if self.maintenance or self.firmware_job["state"] in ("starting", "running"):
                raise RuntimeError("firmware maintenance is active")
            if self._restore_pending:
                raise RuntimeError("wait for session recovery")
        original = self._read_settings()
        with self.lock:
            if self.maintenance or self.firmware_job["state"] in ("starting", "running"):
                raise RuntimeError("firmware maintenance is active")
            if self._require_board()["id"] != board["id"]:
                raise RuntimeError("controller changed during settings read")
            if self.tuning_session.get("active"):
                self._require_session_board()
                return self.tuning_status()
            if set(original) != ALLOWED_SETTINGS:
                raise RuntimeError("complete live settings are required before tuning")
            if not self.serial:
                raise RuntimeError("ESP32 is disconnected")
            self.stop(); self.disconnect_gamepad()
            self.write("CFG SET heading-enabled 0")
            now = time.time()
            self.tuning_session = {**self._empty_session(), "active": True,
                "board_id": board["id"], "firmware": self.telemetry.get("firmware"),
                "identity_source": board["identity_source"],
                "board": board,
                "workflow": workflow, "started": now, "updated": now,
                "original_settings": original,
                "live_settings": dict(original, **{"heading-enabled": 0}),
                "saved_settings": {}, "phase": "ready"}
            self.calibration.update(active=True, running=False, phase="ready",
                                    encoder_profile="four" if board["id"] == "maker" else "rear", error=None, aborted=False,
                                    magnitude=DEFAULT_TEST_MAGNITUDE, elapsed_ms=0,
                                    live_deltas=None)
            self._persist_session()
            return self.tuning_status()

    @staticmethod
    def _wheel_commands(direction):
        forward, left, ccw = CALIBRATION_DIRECTIONS[direction]
        wheel = [forward - left - ccw, forward + left + ccw,
                 forward + left - ccw, forward - left + ccw]
        largest = max(1.0, *(abs(value) for value in wheel))
        return [value / largest for value in wheel]

    @staticmethod
    def _validate_pwm_updates(settings):
        if not isinstance(settings, dict) or not settings:
            raise ValueError("at least one PWM setting is required")
        unknown = set(settings) - set(PWM_KEYS)
        if unknown:
            raise ValueError("only motor PWM settings can be changed here")
        clean = {}
        for key, value in settings.items():
            if (isinstance(value, bool) or not isinstance(value, (int, float)) or
                    not math.isfinite(value)):
                raise ValueError(f"{key} must be a finite number")
            rounded = int(round(value))
            if not 0 <= rounded <= 255:
                raise ValueError(f"{key} must be from 0 to 255")
            clean[key] = rounded
        return clean

    def _build_recommendation(self, test, heading, path, quality, step):
        current = {key: int(round(test.get("settings", {}).get(
            key, self.tuning_session.get("live_settings", {}).get(
                key, self.board_profile()["baseline"][key])))) for key in PWM_KEYS}
        if self.tuning_session.get("workflow") == "bench":
            return {"kind": "hold",
                    "summary": "Use each wheel's response to check repeatability, then adjust PWM or test output manually and repeat the identical setup.",
                    "basis": "A wheels-up run cannot reveal chassis heading or path error. Each installed encoder is compared only with its own earlier runs.",
                    "step": step, "deltas": {key: 0 for key in PWM_KEYS},
                    "suggested_settings": current}
        if quality != "normal" or test["command"] in ("ccw", "cw"):
            if quality == "no-motion":
                summary = "No directional trim is safe yet. Raise test output one level and retry."
            elif quality == "jerky":
                summary = "Establish smooth, repeatable motion before trimming direction. Check traction and the stall threshold."
            else:
                summary = "Rotation checks do not produce a translation trim. Balance forward, reverse, and strafe first."
            return {"kind": "hold", "summary": summary,
                    "basis": "A directional PWM trim needs a stable translation observation. Wheel ticks alone cannot establish chassis heading or path error.",
                    "step": step, "deltas": {key: 0 for key in PWM_KEYS},
                    "suggested_settings": current}
        wheel = self._wheel_commands(test["command"])
        adjustments = [0.0] * 4
        reasons = []

        if heading in ("yaw-left", "yaw-right"):
            error_sign = 1.0 if heading == "yaw-left" else -1.0
            yaw_coefficients = (-1.0, 1.0, -1.0, 1.0)
            for index in range(4):
                adjustments[index] += -error_sign * yaw_coefficients[index] * wheel[index]
            reasons.append("counterclockwise heading drift" if error_sign > 0
                           else "clockwise heading drift")

        command = test["command"]
        if command in ("forward", "reverse") and path in ("drift-left", "drift-right"):
            error_sign = 1.0 if path == "drift-left" else -1.0
            lateral_coefficients = (-1.0, 1.0, 1.0, -1.0)
            for index in range(4):
                adjustments[index] += -error_sign * lateral_coefficients[index] * wheel[index]
            reasons.append("left path drift" if error_sign > 0 else "right path drift")
        elif command in ("left", "right") and path in ("drift-forward", "drift-back"):
            error_sign = 1.0 if path == "drift-forward" else -1.0
            forward_coefficients = (1.0, 1.0, 1.0, 1.0)
            for index in range(4):
                adjustments[index] += -error_sign * forward_coefficients[index] * wheel[index]
            reasons.append("forward path drift" if error_sign > 0 else "backward path drift")

        strongest = max((abs(value) for value in adjustments), default=0.0)
        if strongest == 0:
            if quality == "no-motion":
                summary = "No directional trim is safe yet. Raise test output one level and retry."
            elif quality == "jerky":
                summary = "Repeat once at the same settings. Jerky motion can be traction or a stall threshold."
            elif heading == "straight" and path == "none":
                summary = "Keep these PWM values and repeat the same test to confirm the result."
            else:
                summary = "No automatic PWM change is justified by this observation. Adjust manually or repeat."
            return {"kind": "hold", "summary": summary, "basis":
                    "Encoder response is shown per motor for repeatability; short pulse totals are not used for automatic speed matching.",
                    "step": step, "deltas": {key: 0 for key in PWM_KEYS},
                    "suggested_settings": current}

        deltas = {}
        suggested = {}
        for index, key in enumerate(PWM_KEYS):
            scaled = step * adjustments[index] / strongest
            delta = 0 if adjustments[index] == 0 else int(math.copysign(
                max(1, int(round(abs(scaled)))), adjustments[index]))
            suggested[key] = max(0, min(255, current[key] + delta))
            deltas[key] = suggested[key] - current[key]
        readable_reasons = " and ".join(reasons)
        return {"kind": "pwm-vector",
                "summary": f"Correct for {readable_reasons} with a balanced {step}-count PWM trim, then repeat the identical test.",
                "basis": "The trim comes from the observed chassis motion and mecanum wheel geometry. Pulse response includes startup; these rates are never compared as steady-speed measurements.",
                "step": step, "deltas": deltas, "suggested_settings": suggested}

    def record_observation(self, test_id, observation, score, notes="", heading=None,
                           path="none", quality="normal", adjustment_step=3):
        with self.lock:
            if not self.tuning_session.get("active"):
                raise RuntimeError("no active tuning session")
            self._require_session_board()
            if (not isinstance(score, int) or isinstance(score, bool) or
                    not 1 <= score <= 5):
                raise ValueError("score must be an integer from 1 to 5")
            test = next((x for x in self.tuning_session["tests"] if x["id"] == test_id), None)
            if not test: raise ValueError("test not found")
            heading = heading or {"arc-left": "yaw-left", "arc-right": "yaw-right",
                                  "straight": "straight"}.get(observation, "unsure")
            if heading not in ("straight", "yaw-left", "yaw-right", "unsure"):
                raise ValueError("invalid heading observation")
            if path not in ("none", "drift-left", "drift-right", "drift-forward",
                            "drift-back", "unsure"):
                raise ValueError("invalid path observation")
            if quality not in ("normal", "jerky", "no-motion"):
                raise ValueError("invalid motion quality")
            if (not isinstance(adjustment_step, int) or isinstance(adjustment_step, bool) or
                    not 1 <= adjustment_step <= 10):
                raise ValueError("adjustment_step must be an integer from 1 to 10")
            prior = [x for x in self.tuning_session["tests"]
                     if x["id"] != test_id and x.get("command") == test.get("command")
                     and x.get("score") and x.get("duration_ms") == test.get("duration_ms")
                     and abs(float(x.get("magnitude", 1.0)) -
                             float(test.get("magnitude", 1.0))) < 0.001]
            test["prior_best_id"] = max(prior, key=lambda x: x["score"])["id"] if prior else None
            test.update(observation=str(observation)[:80], heading=heading, path=path,
                        quality=quality, score=score, notes=str(notes)[:500])
            test["recommendation"] = self._build_recommendation(
                test, heading, path, quality, adjustment_step)
            best = [x for x in self.tuning_session["tests"] if x.get("score")]
            self.tuning_session["best_test_id"] = max(best, key=lambda x: x["score"])["id"] if best else None
            by_command = [x for x in best if x.get("command") == test.get("command")]
            if by_command:
                self.tuning_session.setdefault("best_test_ids", {})[test["command"]] = max(
                    by_command, key=lambda x: x["score"])["id"]
            self.tuning_session["phase"] = ("adjustment-ready" if
                test["recommendation"]["kind"] == "pwm-vector" else "ready")
            self._persist_session()
            return self.tuning_status()

    def apply_tuning_settings(self, settings, source="manual", test_id=None):
        with self.lock:
            if not self.tuning_session.get("active"):
                raise RuntimeError("no active tuning session")
            board = self._require_session_board()
            if self.calibration.get("running"):
                raise RuntimeError("wait for the active test to stop before changing PWM")
            if not self.serial:
                raise RuntimeError("ESP32 is disconnected")
            clean = self._validate_pwm_updates(settings)
            before = {key: int(round(self.tuning_session.get("live_settings", {}).get(
                key, board["baseline"][key]))) for key in PWM_KEYS}
            self.stop()
            for key, value in clean.items():
                self.write(f"CFG SET {key} {value}")
            self.tuning_session.setdefault("live_settings", {}).update(clean)
            after = dict(before)
            after.update(clean)
            self.tuning_session.setdefault("adjustments", []).append({
                "id": len(self.tuning_session.get("adjustments", [])) + 1,
                "timestamp": time.time(), "source": str(source)[:40],
                "test_id": test_id, "before": before, "after": after})
            self.tuning_session["phase"] = "ready"
            self._persist_session()
            return self.tuning_status()

    def apply_recommendation(self, test_id):
        with self.lock:
            test = next((item for item in self.tuning_session.get("tests", [])
                         if item["id"] == test_id), None)
            if not test or not test.get("recommendation"):
                raise ValueError("test recommendation not found")
            recommendation = test["recommendation"]
            if recommendation.get("kind") != "pwm-vector":
                raise ValueError("this result does not contain a PWM change")
            self.apply_tuning_settings(
                recommendation["suggested_settings"], "recommendation", test_id)
            test["recommendation_applied"] = True
            self._persist_session()
            return self.tuning_status()

    def apply_baseline(self):
        return self.apply_tuning_settings(self._require_board()["baseline"], "board-baseline")

    def note_live_setting(self, key, value):
        with self.lock:
            if self.tuning_session.get("active"):
                self.tuning_session.setdefault("live_settings", {})[key] = value
                self._persist_session()

    def note_settings_saved(self):
        with self.lock:
            if self.tuning_session.get("active"):
                self.tuning_session["saved_settings"] = dict(
                    self.tuning_session.get("live_settings", {}))
                self._persist_session()

    def _restore_session_settings(self):
        self._require_session_board()
        for key, value in self.tuning_session.get("original_settings", {}).items():
            if key in ALLOWED_SETTINGS: self.write(f"CFG SET {key} {value}")
        self.stop()

    def end_tuning(self, keep_live=False, save=False):
        with self.lock:
            self._require_session_board()
            if save and not keep_live:
                raise ValueError("saving requires keep_live")
            self.calibration_abort.set(); self.stop()
            if not keep_live:
                self._restore_session_settings()
                self.tuning_session["live_settings"] = dict(self.tuning_session["original_settings"])
            else:
                original_heading = self.tuning_session.get("original_settings", {}).get("heading-enabled")
                if save and original_heading is None:
                    raise RuntimeError("cannot safely save because the original heading state is unknown")
                if original_heading is not None:
                    self.write(f"CFG SET heading-enabled {original_heading}")
                    self.tuning_session["live_settings"]["heading-enabled"] = original_heading
                if save:
                    self.write("CFG SAVE")
                    self.tuning_session["saved_settings"] = dict(
                        self.tuning_session.get("live_settings", {}))
            self.tuning_session["active"] = False
            self.tuning_session["phase"] = ("ended-saved" if save else
                ("ended-live" if keep_live else "restored"))
            self._persist_session()
            self.calibration["active"] = False
            return self.tuning_status()

    def find_port(self):
        if self.requested_port and os.path.exists(self.requested_port):
            return self.requested_port
        stable = sorted(glob.glob("/dev/serial/by-id/*"))
        ports = stable or sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
        return ports[0] if ports else None

    def _observe(self, method, *args):
        """Passive failures must not change transport, stop, or legacy telemetry behavior."""
        try:
            return getattr(self.operations, method)(*args)
        except Exception as exc:
            try:
                message = str(exc)
            except Exception:
                message = type(exc).__name__
            self.operations_error = {'method': method, 'error': message[:255]}
            return None

    def write(self, command):
        with self.lock:
            if not self.serial:
                raise RuntimeError("ESP32 is disconnected")
            self.serial.write((command.rstrip() + "\n").encode("ascii"))
            self.serial.flush()
            self._observe('transmit', command.rstrip())

    def stop(self):
        try:
            self.write("V 0 0 0")
            self.write("X")
        except (RuntimeError, OSError, serial.SerialException):
            pass

    def emergency_stop(self):
        self.calibration_abort.set()
        self.rearm_required = True
        self.stop()

    def command(self, command, wait=0.35):
        with self.lock:
            marker = self._line_sequence
            self.write(command)
        time.sleep(wait)
        with self.lock:
            return [line for sequence, line in self._responses if sequence > marker]

    def snapshot(self):
        with self.lock:
            return copy.deepcopy(dict(self.telemetry, simulated=self.telemetry.get('simulated', False), maintenance=self.maintenance,
                        operations_error=self.operations_error,
                        board=self.board_profile(),
                        rearm_required=self.rearm_required,
                        calibration_active=self.calibration["active"],
                        recent_lines=self.lines[-30:]))

    def calibration_status(self):
        with self.lock:
            return dict(self.calibration)

    def start_calibration(self, confirmation, encoder_profile=None):
        with self.lock:
            if confirmation != "WHEELS_UP":
                raise ValueError("wheels-up confirmation required")
            board = self._require_board()
            expected_profile = "four" if board["id"] == "maker" else "rear"
            encoder_profile = encoder_profile or expected_profile
            if encoder_profile not in ("rear", "four"):
                raise ValueError("encoder_profile must be rear or four")
            if encoder_profile != expected_profile:
                raise ValueError("encoder profile does not match the connected board")
            if not self.serial or not self.telemetry.get("serial_connected"):
                raise RuntimeError("ESP32 is disconnected")
            if self.maintenance:
                raise RuntimeError("maintenance mode is active")
            if self.firmware_job["state"] in ("starting", "running"):
                raise RuntimeError("firmware update is running")
            self.stop()
            self.disconnect_gamepad()
            self.write("CFG SET heading-enabled 0")
            self.calibration = {"active": True, "running": False, "phase": "ready",
                                "direction": None, "started": time.time(),
                                "duration_ms": 0, "magnitude": DEFAULT_TEST_MAGNITUDE,
                                "elapsed_ms": 0, "before": None, "after": None,
                                "deltas": None, "live_deltas": None, "error": None,
                                "aborted": False,
                                "encoder_profile": encoder_profile}
            return dict(self.calibration)

    def end_calibration(self):
        with self.lock:
            self.calibration_abort.set()
            self.stop()
            self.calibration["active"] = False
            self.calibration["phase"] = "stopping" if self.calibration["running"] else "ended"
            return dict(self.calibration)

    def start_calibration_pulse(self, direction, duration_ms,
                                magnitude=DEFAULT_TEST_MAGNITUDE):
        if direction not in CALIBRATION_DIRECTIONS:
            raise ValueError("invalid calibration direction")
        if (not isinstance(duration_ms, int) or isinstance(duration_ms, bool) or
                not 500 <= duration_ms <= 3000):
            raise ValueError("duration_ms must be an integer from 500 to 3000")
        if (isinstance(magnitude, bool) or not isinstance(magnitude, (int, float)) or
                not math.isfinite(magnitude) or
                not MIN_TEST_MAGNITUDE <= magnitude <= MAX_TEST_MAGNITUDE):
            raise ValueError("magnitude must be from 0.15 to 1.0")
        magnitude = float(magnitude)
        with self.lock:
            if not self.calibration["active"]:
                raise RuntimeError("calibration mode is not active")
            if self.calibration["running"]:
                raise RuntimeError("a calibration pulse is already running")
            if not self.serial:
                raise RuntimeError("ESP32 is disconnected")
            self._require_board()
            if self.tuning_session.get("active"):
                self._require_session_board()
            tests = self.tuning_session.get("tests", [])
            if (self.tuning_session.get("active") and tests and
                    tests[-1].get("score") is None):
                raise RuntimeError("record the previous test observation before running again")
            encoder_updated = self.telemetry.get("encoder_updated")
            if encoder_updated is None or time.time() - encoder_updated > 1.0:
                raise RuntimeError("encoder telemetry is unavailable or stale")
            before = list(self.telemetry.get("encoders", []))
            if len(before) != 4:
                raise RuntimeError("encoder telemetry is unavailable")
            self.calibration.update(running=True, phase="running", direction=direction,
                                    started=time.time(), duration_ms=duration_ms,
                                    magnitude=magnitude, elapsed_ms=0, before=before,
                                    after=None, deltas=None, live_deltas=[0, 0, 0, 0],
                                    error=None, aborted=False)
            self.calibration_abort.clear()
        threading.Thread(target=self._run_calibration_pulse,
                         args=(direction, duration_ms, magnitude), daemon=True).start()
        return self.calibration_status()

    def _run_calibration_pulse(self, direction, duration_ms, magnitude):
        motion = tuple(value * magnitude for value in CALIBRATION_DIRECTIONS[direction])
        started = time.monotonic()
        deadline = started + duration_ms / 1000.0
        error = None
        after = list(self.calibration["before"] or [0, 0, 0, 0])
        board_id = self.board_profile()["id"]
        try:
            while time.monotonic() < deadline and not self.calibration_abort.is_set():
                if self.board_profile()["id"] != board_id:
                    raise RuntimeError("controller changed during test")
                if time.time() - (self.telemetry.get("encoder_updated") or 0) > 1:
                    raise RuntimeError("encoder telemetry became stale during test")
                self.write("V %.3f %.3f %.3f" % motion)
                with self.lock:
                    current = list(self.telemetry.get("encoders", [0, 0, 0, 0]))
                    before = self.calibration.get("before") or [0, 0, 0, 0]
                    self.calibration["elapsed_ms"] = min(
                        duration_ms, int((time.monotonic() - started) * 1000))
                    self.calibration["live_deltas"] = [
                        current[index] - before[index] for index in range(4)]
                time.sleep(1.0 / SEND_HZ)
        except Exception as exc:
            error = str(exc)
        finally:
            # Capture powered response before STOP; exclude the coast-down wait.
            with self.lock:
                after = list(self.telemetry.get("encoders", after))
            self.stop()
        aborted = self.calibration_abort.is_set()
        time.sleep(0.35)
        with self.lock:
            before = self.calibration["before"] or [0, 0, 0, 0]
            deltas = [after[i] - before[i] for i in range(4)]
            phase = "aborted" if aborted else ("error" if error else "complete")
            self.calibration.update(running=False, phase=phase, elapsed_ms=duration_ms,
                                    after=after, deltas=deltas, live_deltas=deltas,
                                    error=error, aborted=aborted)
            if self.tuning_session.get("active") and not error and not aborted:
                live = dict(self.tuning_session.get("live_settings", {}))
                settings = {key: int(round(live.get(key, self.board_profile()["baseline"][key])))
                            for key in PWM_KEYS}
                actual_duty = {key: int(round(settings[key] * magnitude))
                               for key in PWM_KEYS}
                rates = {MOTOR_NAMES[index]: round(deltas[index] * 1000.0 / duration_ms, 2)
                         for index in range(4)}
                prior = next((item for item in reversed(self.tuning_session["tests"])
                              if item.get("command") == direction), None)
                comparison = None
                if prior:
                    comparable = (abs(float(prior.get("magnitude", 1.0)) - magnitude) < 0.001
                                  and int(prior.get("duration_ms", 0)) == duration_ms)
                    comparison = {"test_id": prior["id"], "comparable": comparable,
                                  "rear": {}, "wheels": {}}
                    if not comparable:
                        comparison["reason"] = "test output or duration changed"
                    else:
                        for index, key in enumerate(PWM_KEYS):
                            name = MOTOR_NAMES[index]
                            if name not in self.board_profile()["encoder_wheels"]:
                                continue
                            previous_rate = abs(float(prior.get("encoder_rates", {}).get(name, 0)))
                            current_rate = abs(rates[name])
                            rate_change = None if previous_rate == 0 else round(
                                (current_rate - previous_rate) * 100.0 / previous_rate, 1)
                            comparison["wheels"][name] = {
                                "previous_rate": previous_rate, "rate_change_percent": rate_change,
                                "pwm_change": settings[key] - int(round(
                                    prior.get("settings", {}).get(key, settings[key])))}
                        comparison["rear"] = {name: value for name, value in comparison["wheels"].items()
                                              if name in ("RL", "RR")}
                self.tuning_session["tests"].append({
                    "id": len(self.tuning_session["tests"]) + 1,
                    "timestamp": time.time(), "command": direction,
                    "duration_ms": duration_ms, "magnitude": magnitude,
                    "deltas": deltas, "encoder_rates": rates,
                    "actual_duty": actual_duty, "comparison": comparison,
                    "settings": settings, "observation": None, "heading": None,
                    "path": None, "quality": None, "score": None, "notes": "",
                    "recommendation": None, "recommendation_applied": False})
                self.tuning_session["phase"] = "observation-required"
                self._persist_session()

    def set_maintenance(self, enabled):
        with self.lock:
            if enabled and (self.calibration["active"] or self.tuning_session.get("active")):
                raise RuntimeError("finish tuning before firmware maintenance")
            self.rearm_required = True
            self.stop()
            self.maintenance = bool(enabled)
            if enabled:
                self.disconnect_gamepad()
                self.disconnect_serial()
                self.lines.clear()
                for key in ("firmware", "imu", "navigation", "health", "encoders",
                            "encoder_updated", "last_line"):
                    self.telemetry.pop(key, None)

    def firmware_status(self):
        with self.lock:
            return dict(self.firmware_job, lines=self.firmware_job["lines"][-100:])

    def start_firmware_update(self):
        with self.lock:
            board = self._require_board()
            if self.firmware_job["state"] in ("starting", "running"):
                raise RuntimeError("firmware update already running")
            if self.calibration["active"] or self.tuning_session.get("active") or self.maintenance:
                raise RuntimeError("finish tuning and maintenance before updating firmware")
            if self.telemetry.get("gamepad_connected") or self.telemetry.get("deadman"):
                raise RuntimeError("disconnect the controller before updating firmware")
            self.stop()
            self.firmware_job = {"state": "starting", "started": time.time(),
                                 "finished": None, "returncode": None, "lines": [],
                                 "board_id": board["id"]}
        threading.Thread(target=self._run_firmware_update, args=(board["id"],), daemon=True).start()

    def _run_firmware_update(self, board_id):
        updater = str(Path(__file__).with_name("mechbot_firmware_update.py"))
        try:
            process = subprocess.Popen([sys.executable, "-u", updater, "--board", board_id], stdout=subprocess.PIPE,
                                       stderr=subprocess.STDOUT, text=True, bufsize=1)
            with self.lock:
                self.firmware_process = process
                self.firmware_job["state"] = "running"
            for line in process.stdout:
                with self.lock:
                    self.firmware_job["lines"].append(line.rstrip())
                    del self.firmware_job["lines"][:-300]
            returncode = process.wait()
            with self.lock:
                self.firmware_job.update(state="complete" if returncode == 0 else "failed",
                                         finished=time.time(), returncode=returncode)
        except Exception as exc:
            with self.lock:
                self.firmware_job["lines"].append(f"ERROR: {exc}")
                self.firmware_job.update(state="failed", finished=time.time(), returncode=-1)

    def parse_line(self, line):
        now = time.time()
        with self.lock:
            self._observe('feed', line)
            self.lines.append(line)
            del self.lines[:-300]
            self._line_sequence += 1
            self._responses.append((self._line_sequence, line))
            del self._responses[:-300]
            self.telemetry["last_line"] = line
            self.telemetry["updated"] = now
            parts = line.split()
            if parts[:1] in (["READY"], ["WARN"], ["FAULT"], ["ERR"]):
                print(line, flush=True)
            try:
                if parts[:1] == ["T"] and len(parts) == 6:
                    self.telemetry["encoders"] = [int(x) for x in parts[2:6]]
                    self.telemetry["encoder_updated"] = now
                elif parts[:1] == ["I"]:
                    self.telemetry["imu"] = line
                    valid = len(parts) == 13 and all(math.isfinite(float(value)) for value in parts[2:])
                    self.telemetry["imu_valid"] = valid
                    self.telemetry["imu_updated"] = now if valid else None
                elif parts[:1] == ["N"]:
                    self.telemetry["navigation"] = line
                elif parts[:1] == ["H"]:
                    self.telemetry["health"] = line
                elif parts[:1] == ["READY"]:
                    if self.calibration.get("running"):
                        self.calibration_abort.set()
                    self.telemetry["firmware"] = " ".join(parts[1:])
                    self.telemetry["firmware_received"] = now
                    self.rearm_required = True
                elif line == MAKER_HELP_IDENTITY:
                    self.telemetry["help_identity"] = line
                elif parts[:1] == ["D"] and len(parts) == 10:
                    if (parts[1] in MOTOR_NAMES and parts[2::2] == ["PWM", "A", "B", "INVALID"]):
                        pwm = float(parts[3])
                        a, b, invalid = (int(parts[i]) for i in (5, 7, 9))
                        if math.isfinite(pwm) and abs(pwm) <= 255 and min(a, b, invalid) >= 0:
                            self.telemetry.setdefault("wheel_diagnostics", {})[parts[1]] = {
                                "pwm": pwm, "a_edges": a, "b_edges": b,
                                "invalid_transitions": invalid, "updated": now}
            except ValueError:
                if parts[:1] == ["I"]:
                    self.telemetry["imu_valid"] = False
                    self.telemetry["imu_updated"] = None

    def poll_identity(self, now):
        """Recover a missed startup help reply without resetting the controller."""
        with self.lock:
            if (not self.serial or self.maintenance or self.calibration["active"]
                    or self.firmware_job["state"] in ("starting", "running")
                    or self.telemetry.get("firmware") is not None
                    or self.board_profile()["id"] != "unknown"
                    or now < self._next_identity_query):
                return
            self._next_identity_query = now + 2.0
            self.write("?")

    def connect_serial(self):
        port = self.find_port()
        if not port:
            return
        self.serial = serial.Serial(port, 115200, timeout=0, write_timeout=0.2, exclusive=True)
        self.rearm_required = True
        # Preserve boot output: READY is the firmware-update verification token.
        time.sleep(2.0)
        self.telemetry["serial_connected"] = True
        self.telemetry["serial_port"] = port
        self.write("?")  # Recognize deployed Maker even when serial open does not reset it.
        self._next_identity_query = time.monotonic() + 2.0
        print(f"ESP32 connected: {port}", flush=True)

    def disconnect_serial(self):
        self._next_identity_query = 0.0
        self._observe('boundary', 'disconnect')
        if self.serial:
            try: self.serial.close()
            except Exception: pass
        self.serial = None
        self.telemetry["serial_connected"] = False
        self.rearm_required = True
        for key in ("firmware", "firmware_received", "help_identity", "imu", "imu_valid", "imu_updated",
                    "navigation", "health", "encoders", "encoder_updated", "wheel_diagnostics"):
            self.telemetry.pop(key, None)
        if self.tuning_session.get("active"):
            self._restore_pending = True

    def connect_gamepad(self):
        if os.path.exists(self.joystick_path):
            self.rearm_required = True
            self.joystick = open(self.joystick_path, "rb", buffering=0)
            self.axes = [0] * 9
            self.buttons = [0] * 16
            self.telemetry["gamepad_connected"] = True
            print(f"Gamepad connected: {self.joystick_path}", flush=True)

    def disconnect_gamepad(self):
        self.rearm_required = True
        self.stop()
        if self.joystick:
            try: self.joystick.close()
            except Exception: pass
        self.joystick = None
        self.buttons = [0] * 16
        self.telemetry["gamepad_connected"] = False
        self.telemetry["deadman"] = False

    def loop(self):
        next_send = time.monotonic()
        rx = bytearray()
        while self.running:
            try:
                if self.maintenance:
                    time.sleep(0.1)
                    continue
                if not self.serial:
                    self.connect_serial()
                if (self.tuning_session.get("active") and self.tuning_session.get("updated") and
                        time.time() - self.tuning_session["updated"] > SESSION_TIMEOUT_SECONDS):
                    try: self.end_tuning(False)
                    except Exception: pass
                if (not self.calibration["active"] and not self.joystick and
                        self.firmware_job["state"] not in ("starting", "running")):
                    self.connect_gamepad()
                if self.calibration["active"] and self.joystick:
                    self.disconnect_gamepad()
                if self.serial and self.serial.in_waiting:
                    rx.extend(self.serial.read(self.serial.in_waiting))
                    while b"\n" in rx:
                        raw, _, rx = rx.partition(b"\n")
                        self.parse_line(raw.decode("ascii", "replace").strip())
                if self.serial and self._restore_pending and self.board_profile()["id"] != "unknown":
                    try:
                        self._restore_session_settings()
                        self.tuning_session.update(active=False, phase="recovered-after-restart")
                    except RuntimeError as exc:
                        self.stop()
                        self.tuning_session.update(active=False, phase="recovery-blocked", error=str(exc))
                    self._persist_session()
                    self._restore_pending = False
                    self.calibration["active"] = False
                if self.joystick:
                    if not os.path.exists(self.joystick_path):
                        self.disconnect_gamepad()
                    else:
                        readable, _, _ = select.select([self.joystick], [], [], 0)
                        if readable:
                            data = self.joystick.read(JS_EVENT_SIZE)
                            if len(data) != JS_EVENT_SIZE:
                                self.disconnect_gamepad()
                            else:
                                _, value, kind, number = struct.unpack(JS_EVENT_FORMAT, data)
                                kind &= ~JS_EVENT_INIT
                                if kind == JS_EVENT_AXIS and number < len(self.axes): self.axes[number] = value
                                elif kind == JS_EVENT_BUTTON and number < len(self.buttons):
                                    self.buttons[number] = value
                                    if number == BUTTON_DEADMAN and value == 0:
                                        self.rearm_required = False
                now = time.monotonic()
                self.poll_identity(now)
                if self.serial and not self.calibration["active"] and now >= next_send:
                    deadman = bool(self.joystick and self.buttons[BUTTON_DEADMAN] and
                                   not self.rearm_required and not self._restore_pending and
                                   self.board_profile()["id"] != "unknown" and
                                   self.firmware_job["state"] not in ("starting", "running"))
                    self.telemetry["deadman"] = deadman
                    motion = gamepad_motion(self.axes) if deadman else (0.0, 0.0, 0.0)
                    self.write("V %.3f %.3f %.3f" % motion)
                    next_send = now + 1.0 / SEND_HZ
                if self.serial and self.board_profile()["diagnostics"] and now >= self._next_diagnostics:
                    self.write("DIAG")
                    self._next_diagnostics = now + 1.0
            except (OSError, RuntimeError, serial.SerialException) as exc:
                print(f"Device disconnected: {exc}", flush=True)
                self.disconnect_gamepad(); self.disconnect_serial()
                rx = bytearray()
            time.sleep(0.01)


class Handler(BaseHTTPRequestHandler):
    bridge = None
    def reply(self, code, payload):
        body = json.dumps(payload).encode()
        self.send_response(code); self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_OPTIONS(self):
        try: validate_origin(self)
        except ValueError as exc: self.reply(403, {"error": str(exc)}); return
        self.send_response(204); self.end_headers()
    def do_GET(self):
        path = urlparse(self.path).path
        if handle_get(self, path): return
        if path == "/api/status": self.reply(200, self.bridge.snapshot())
        elif path == "/api/settings": self.reply(200, {"lines": self.bridge.command("CFG GET")})
        elif path == "/api/firmware": self.reply(200, self.bridge.firmware_status())
        elif path == "/api/calibration": self.reply(200, self.bridge.calibration_status())
        elif path == "/api/tuning": self.reply(200, self.bridge.tuning_status())
        elif path in ("/api/tuning/export.json", "/api/tuning/export.csv"):
            format = path.rsplit(".", 1)[-1]
            body = self.bridge.export_tuning(format).encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "application/json" if format == "json" else "text/csv; charset=utf-8")
            self.send_header("Content-Disposition", f'attachment; filename="mechbot-tuning.{format}"')
            self.send_header("Content-Length", str(len(body)))
            self.end_headers(); self.wfile.write(body)
        elif path == "/": self.serve_file("ops.html", "text/html; charset=utf-8")
        elif path == "/tuning": self.serve_file("index.html", "text/html; charset=utf-8")
        elif path == "/ops.css": self.serve_file("ops.css", "text/css; charset=utf-8")
        elif path == "/ops.js": self.serve_file("ops.js", "text/javascript; charset=utf-8")
        elif path in ("/ops-live.js", "/ops-sessions.js", "/ops-evidence.js", "/ops-encoder-math.js", "/ops-encoder.js"):
            self.serve_file(path[1:], "text/javascript; charset=utf-8")
        elif path == "/app.css": self.serve_file("app.css", "text/css; charset=utf-8")
        elif path == "/calibration.css": self.serve_file("calibration.css", "text/css; charset=utf-8")
        elif path == "/app.js": self.serve_file("app.js", "text/javascript; charset=utf-8")
        else: self.reply(404, {"error": "not found"})
    def serve_file(self, name, content_type):
        try:
            body = (DASHBOARD_DIR / name).read_bytes()
        except OSError:
            self.reply(404, {"error": "dashboard not installed"}); return
        self.send_response(200); self.send_header("Content-Type", content_type)
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Content-Length", str(len(body))); self.end_headers(); self.wfile.write(body)
    def do_POST(self):
        try:
            validate_origin(self)
            data = read_json_body(self)
            if handle_post(self, urlparse(self.path).path, data): return
            if self.path.startswith("/api/settings"):
                board = self.bridge._require_board()
                if data.get("board_id", board["id"]) != board["id"]:
                    raise RuntimeError("controller changed; reload settings")
                if self.bridge.calibration["active"] or self.bridge.maintenance:
                    raise RuntimeError("finish tuning and maintenance before editing settings")
                if self.bridge.firmware_job["state"] in ("starting", "running"):
                    raise RuntimeError("firmware update is running")
            if self.path == "/api/stop": self.bridge.emergency_stop(); result = {"ok": True}
            elif self.path == "/api/maintenance":
                self.bridge.set_maintenance(bool(data.get("enabled")))
                result = {"ok": True, "maintenance": self.bridge.maintenance}
            elif self.path == "/api/settings":
                key, value = data.get("key"), data.get("value")
                if (key not in ALLOWED_SETTINGS or isinstance(value, bool) or
                        not isinstance(value, (int, float)) or not math.isfinite(value)):
                    raise ValueError("invalid setting")
                result = {"lines": self.bridge.command(f"CFG SET {key} {value}")}
                self.bridge.note_live_setting(key, value)
            elif self.path == "/api/settings/save":
                result = {"lines": self.bridge.command("CFG SAVE")}
                self.bridge.note_settings_saved()
            elif self.path == "/api/settings/reset": result = {"lines": self.bridge.command("CFG RESET")}
            elif self.path == "/api/firmware/start":
                self.bridge.start_firmware_update(); result = self.bridge.firmware_status()
            elif self.path == "/api/calibration/start":
                result = self.bridge.start_calibration(
                    data.get("confirmation"), data.get("encoder_profile"))
            elif self.path == "/api/calibration/pulse":
                result = self.bridge.start_calibration_pulse(
                    data.get("direction"), data.get("duration_ms"),
                    data.get("magnitude", DEFAULT_TEST_MAGNITUDE))
            elif self.path == "/api/calibration/end": result = self.bridge.end_calibration()
            elif self.path == "/api/tuning/start":
                result = self.bridge.start_tuning(data.get("workflow"), data.get("confirmation"))
            elif self.path == "/api/tuning/observe":
                result = self.bridge.record_observation(data.get("test_id"), data.get("observation"),
                    data.get("score"), data.get("notes", ""), data.get("heading"),
                    data.get("path", "none"), data.get("quality", "normal"),
                    data.get("adjustment_step", 3))
            elif self.path == "/api/tuning/settings":
                result = self.bridge.apply_tuning_settings(
                    data.get("settings"), data.get("source", "manual"))
            elif self.path == "/api/tuning/recommendation":
                result = self.bridge.apply_recommendation(data.get("test_id"))
            elif self.path == "/api/tuning/baseline": result = self.bridge.apply_baseline()
            elif self.path == "/api/tuning/end":
                result = self.bridge.end_tuning(
                    bool(data.get("keep_live")), bool(data.get("save")))
            else: self.reply(404, {"error": "not found"}); return
            self.reply(200, result)
        except (ValueError, RuntimeError) as exc: self.reply(400, {"error": str(exc)})
    def log_message(self, fmt, *args): pass


def main():
    parser = argparse.ArgumentParser(); parser.add_argument("--port"); parser.add_argument("--joystick", default="/dev/input/js0")
    parser.add_argument("--listen", default="0.0.0.0"); parser.add_argument("--http-port", type=int, default=8765)
    args = parser.parse_args(); bridge = Bridge(args.port, args.joystick); Handler.bridge = bridge
    threading.Thread(target=bridge.loop, daemon=True).start()
    print(f"MechBot bridge API: http://{args.listen}:{args.http_port}", flush=True)
    try: ThreadingHTTPServer((args.listen, args.http_port), Handler).serve_forever()
    finally:
        bridge.running = False
        bridge.stop()
        bridge.operations.capture.stop()

if __name__ == "__main__": main()
