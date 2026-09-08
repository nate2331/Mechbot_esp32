#!/usr/bin/env python3
"""Supervised Maker ESP32 Pro bench calibration. Importing never opens hardware.

Run --simulate to exercise the complete search without a robot. A real run needs
--run --wheels-up --bridge-stopped --normal-wiring. Results go to a new JSON file;
the original live settings are restored and CFG SAVE is never issued.
"""
from __future__ import annotations

import argparse
import json
import math
from dataclasses import asdict, dataclass
from datetime import datetime, timezone
from pathlib import Path
import signal
import statistics
import sys
import time

from mechbot_profiles import MAKER_COUNTS_PER_REV


WHEELS = ("FL", "FR", "RL", "RR")
# Actual ten-wheel-revolution measurements from 2026-09-03, divided by ten.
COUNTS_PER_REV = MAKER_COUNTS_PER_REV
PWM_KEYS = tuple("pwm-" + wheel.lower() for wheel in WHEELS)
MAX_PWM = 177  # Ceiling already used in the supervised Maker-board tests.
CONFIG_KEYS = (*PWM_KEYS, "heading-kp", "heading-max", "heading-deadband-deg",
               "heading-sign", "heading-enabled")
MIN_TICKS = 5
TOLERANCE = 0.05
TIME_BUDGET = 1200


class CalibrationError(RuntimeError):
    pass


@dataclass(frozen=True)
class Frame:
    """Receive time, device milliseconds, and forward-positive x4 counts."""
    received: float
    device_ms: int
    counts: tuple[int, int, int, int]


def device_seconds(new: int, old: int) -> float:
    delta = (new - old) & 0xFFFFFFFF
    if not 0 < delta <= 10000:
        raise CalibrationError("Encoder timestamp reset or did not advance")
    return delta / 1000


@dataclass
class Observation:
    pwm: list[int]
    direction: int
    rpm: list[float]
    sustained: list[bool]
    start_delay_bounds_ms: list[list[float] | None]
    sample_gap_ms: float


def analyse(frames: list[Frame], started: float, pwm, direction,
            cpr=COUNTS_PER_REV) -> Observation:
    """Separate steady powered speed from startup and coast-down counts."""
    if direction not in (-1, 1) or len(frames) < 5:
        raise CalibrationError("Too few encoder samples or invalid direction")
    if len(cpr) != 4 or any(not math.isfinite(c) or c <= 0 for c in cpr):
        raise CalibrationError("Four positive counts-per-revolution values required")
    gaps = [device_seconds(b.device_ms, a.device_ms)
            for a, b in zip(frames, frames[1:])]
    if max(gaps) > 0.6:
        raise CalibrationError("Encoder sample gap exceeded 600 ms")
    initial = frames[0].counts
    delays = [None] * 4
    for i in range(4):
        previous_t = 0.0
        for frame in frames[1:]:
            moved = direction * (frame.counts[i] - initial[i])
            if pwm[i] == 0 and abs(moved) >= MIN_TICKS:
                raise CalibrationError(f"Uncommanded {WHEELS[i]} moved; check motor/encoder mapping")
            if pwm[i] and moved <= -MIN_TICKS:
                raise CalibrationError(f"{WHEELS[i]} encoder direction is wrong")
            elapsed = max(0.0, frame.received - started)
            if moved >= MIN_TICKS and delays[i] is None:
                delays[i] = [round(previous_t * 1000, 1), round(elapsed * 1000, 1)]
            previous_t = elapsed
    # Last three samples measure sustained movement; the early ramp is excluded.
    tail = frames[-3:]
    if tail[0].received - started < 0.35:
        raise CalibrationError("Steady-speed window overlaps initial output ramp")
    dt = device_seconds(tail[-1].device_ms, tail[0].device_ms)
    rpm, sustained = [], []
    for i in range(4):
        increments = [direction * (b.counts[i] - a.counts[i])
                      for a, b in zip(tail, tail[1:])]
        if any(delta < -MIN_TICKS for delta in increments) and pwm[i]:
            raise CalibrationError(f"{WHEELS[i]} reversed during the measurement")
        rpm.append(max(0, sum(increments)) * 60 / dt / cpr[i])
        sustained.append(pwm[i] > 0 and all(n >= MIN_TICKS for n in increments))
    return Observation(list(pwm), direction, rpm, sustained, delays,
                       round(max(gaps) * 1000, 1))


def speed_spread(rpm) -> float | None:
    if any(not math.isfinite(v) or v <= 0 for v in rpm):
        return None
    return (max(rpm) - min(rpm)) / min(rpm)


class Tuner:
    """Feedback search. Every change is followed by another measured pulse."""
    def __init__(self, rig, maximum=MAX_PWM, progress=print):
        if not 32 <= maximum <= MAX_PWM:
            raise ValueError(f"Maximum PWM must be between 32 and {MAX_PWM}")
        self.rig, self.maximum, self.progress = rig, maximum, progress
        self.report = {
            "schema": 1, "created_utc": datetime.now(timezone.utc).isoformat(),
            "simulated": isinstance(rig, SimulatedRig),
            "wheel_order": list(WHEELS), "counts_per_revolution": list(COUNTS_PER_REV),
            "maximum_pwm": maximum, "tolerance_fraction": TOLERANCE,
            "startup": {}, "directional_matching": {}, "shared_verification": [],
            "recommended_shared_pwm": None, "status": "in_progress",
            "notes": [
                "Minimum PWM means the lowest tested duty that repeatedly started from rest and sustained encoder motion in this bench test.",
                "A start must produce at least five forward-signed counts and keep moving in both final sample intervals; a single encoder twitch does not pass.",
                "Start-delay brackets include the firmware slew ramp, encoder detection threshold and serial sampling latency (nominal telemetry period 200 ms).",
                "Thresholds depend on load, battery, temperature and shaft position; verify loaded behavior separately.",
                "Startup thresholds are measured, not installed as minimum-output compensation. Existing firmware only stores one PWM ceiling per wheel for both directions.",
                "No wheel-speed PID is installed. This is an automated measure-adjust-retest calibration, not continuous control during driving.",
            ],
        }

    def minimum_start(self, wheel, direction):
        trials = []
        label = "forward" if direction == 1 else "reverse"
        result = {"status": "measuring", "trials": trials}
        self.report["startup"].setdefault(label, {})[WHEELS[wheel]] = result

        def reliable(value):
            successful = True
            for _ in range(3):
                pwms = [0] * 4
                pwms[wheel] = value
                observation = self.rig.pulse(pwms, direction, 1.2)
                trials.append({"pwm": value, "started_and_sustained": observation.sustained[wheel],
                               "rpm": round(observation.rpm[wheel], 3),
                               "start_delay_bounds_ms": observation.start_delay_bounds_ms[wheel]})
                if not observation.sustained[wheel]:
                    successful = False
                    break
            return successful

        lower, upper = 0, None
        for candidate in (*range(16, self.maximum, 8), self.maximum):
            if reliable(candidate):
                upper = candidate
                break
            lower = candidate
        if upper is None:
            raise CalibrationError(f"{WHEELS[wheel]} never started reliably at or below {self.maximum} PWM ({direction:+d})")
        while upper - lower > 1:
            candidate = (lower + upper) // 2
            if reliable(candidate):
                upper = candidate
            else:
                lower = candidate
        # A fresh confirmation catches an intermittent result after refinement.
        if not reliable(upper):
            raise CalibrationError(f"{WHEELS[wheel]} start threshold is not repeatable; inspect recorded trials")
        result.update(status="measured", minimum_reliable_start_pwm=upper,
                      duty_percent=round(100 * upper / 255, 2),
                      highest_lower_tested_failure_pwm=lower,
                      resolution_pwm_counts=1, confirmation_starts=3)
        return result

    def measure(self, pwms, direction, repeats=3):
        observations = [self.rig.pulse(pwms, direction, 2.4) for _ in range(repeats)]
        rpm = [statistics.median(o.rpm[i] for o in observations) for i in range(4)]
        repeatable = all(
            all(o.sustained[i] for o in observations) and
            max(o.rpm[i] for o in observations) - min(o.rpm[i] for o in observations)
            <= max(0.5, rpm[i] * 0.1) for i in range(4))
        spread = speed_spread(rpm)
        return {"pwm": list(pwms), "direction": direction,
                "rpm": [round(v, 4) for v in rpm], "repeatable": repeatable,
                "spread_fraction": spread,
                "passed": repeatable and spread is not None and spread <= TOLERANCE,
                "starts": [o.start_delay_bounds_ms for o in observations]}

    def match_direction(self, direction, thresholds):
        pwms = [self.maximum] * 4
        history = []
        label = "forward" if direction == 1 else "reverse"
        self.report["directional_matching"][label] = {"converged": False, "history": history}
        # Hold the slowest motor at its tested ceiling; reduce faster motors.
        # Corrections can then move either way within that original ceiling.
        target = None
        for _ in range(12):
            measurement = self.measure(pwms, direction)
            history.append(measurement)
            if measurement["passed"]:
                return {"converged": True, "pwm": pwms, "history": history}
            if not measurement["repeatable"]:
                return {"converged": False, "reason": "Motion was absent or speed was inconsistent", "history": history}
            if target is None:
                target = min(measurement["rpm"])
            updated = []
            for i, rpm in enumerate(measurement["rpm"]):
                wanted = round(pwms[i] * target / rpm)
                step = max(-8, min(8, wanted - pwms[i]))
                value = max(thresholds[i], min(self.maximum, pwms[i] + step))
                updated.append(value)
            if updated == pwms:
                break
            pwms = updated
        return {"converged": False, "reason": "No repeatable match within the bounded search", "history": history}

    def run(self):
        for direction, label in ((1, "forward"), (-1, "reverse")):
            self.report["startup"][label] = {}
            for wheel in range(4):
                self.progress(f"Finding {WHEELS[wheel]} {label} reliable starting PWM...")
                result = self.minimum_start(wheel, direction)
                self.report["startup"][label][WHEELS[wheel]] = result
                self.progress(f"  {WHEELS[wheel]} {label}: {result['minimum_reliable_start_pwm']}/255, confirmed three starts")
            thresholds = [self.report["startup"][label][wheel]["minimum_reliable_start_pwm"] for wheel in WHEELS]
            self.progress(f"Matching steady {label} wheel speeds...")
            self.report["directional_matching"][label] = self.match_direction(direction, thresholds)
        matches = self.report["directional_matching"]
        if not all(result["converged"] for result in matches.values()):
            self.report["status"] = "measured_no_shared_trim"
            return self.report
        candidate = [min(matches["forward"]["pwm"][i], matches["reverse"]["pwm"][i]) for i in range(4)]
        self.report["shared_candidate_pwm"] = candidate
        self.progress("Checking one shared trim at 60%, 80%, and 100% command, in both directions...")
        for fraction in (0.6, 0.8, 1.0):
            actual_pwm = [round(pwm * fraction) for pwm in candidate]
            for direction in (1, -1):
                test = self.measure(actual_pwm, direction)
                test["command_fraction"] = fraction
                self.report["shared_verification"].append(test)
        if all(test["passed"] for test in self.report["shared_verification"]):
            self.report["recommended_shared_pwm"] = candidate
            self.report["status"] = "bench_trim_verified"
        else:
            self.report["status"] = "measured_no_shared_trim"
        return self.report


class MakerSerialRig:
    """Sole-owner serial runner retaining the ESP32's 300 ms output watchdog."""
    def __init__(self, serial_port, clock=time.monotonic):
        self.serial, self.clock = serial_port, clock
        self.frame = None
        self.imu_at = self.health_at = self.nav_at = 0.0
        self.health = self.initial_health = None
        self.nav = None
        self.config = {}
        self.diagnostics = {}
        self.armed = False
        self.original = None
        self.changed = False
        self.began = clock()
        self.restored = False

    def send(self, line):
        encoded = (line + "\n").encode("ascii")
        if self.serial.write(encoded) != len(encoded):
            raise CalibrationError("Incomplete serial write")
        # Do not use unbounded flush(): write_timeout bounds a blocked USB write.

    def read(self):
        raw = self.serial.readline(512)
        if not raw:
            return ""
        if not raw.endswith(b"\n") or len(raw) >= 512:
            raise CalibrationError("Incomplete or oversized serial response")
        line = raw.decode("ascii", errors="strict").strip()
        parts = line.split()
        if not parts:
            return line
        now = self.clock()
        if line.startswith(("ERR ", "FAULT", "FATAL")):
            raise CalibrationError(line)
        if self.armed and (line.startswith("READY ") or "watchdog" in line.lower()):
            raise CalibrationError("Controller restarted or motor command watchdog expired")
        try:
            if parts[0] == "T":
                if len(parts) != 6:
                    raise ValueError("encoder frame shape")
                frame = Frame(now, int(parts[1]), tuple(map(int, parts[2:])))
                if not 0 <= frame.device_ms <= 0xFFFFFFFF:
                    raise ValueError("device clock range")
                if self.frame:
                    device_seconds(frame.device_ms, self.frame.device_ms)
                self.frame = frame
            elif parts[0] == "I":
                valid = len(parts) == 13 and all(math.isfinite(float(v)) for v in parts[2:])
                if not valid:
                    self.imu_at = 0
                    if self.armed:
                        raise CalibrationError("IMU is not providing valid telemetry")
                else:
                    self.imu_at = now
            elif parts[0] == "H":
                if len(parts) != 7 or parts[2] != "IMU" or parts[3] != "1":
                    raise ValueError("IMU health frame")
                self.health = tuple(map(int, parts[-2:]))
                self.health_at = now
                if self.armed and self.health != self.initial_health:
                    raise CalibrationError("IMU reset or reinitialization during calibration")
            elif parts[0] == "N":
                if len(parts) != 9:
                    raise ValueError("navigation frame shape")
                self.nav = (int(parts[6]), int(parts[7]))  # Heading and field enable.
                self.nav_at = now
                if self.armed and self.nav != (0, 0):
                    raise CalibrationError("Heading or field control became active")
            elif parts[0] == "D":
                if len(parts) != 10 or parts[1] not in WHEELS or parts[2] != "PWM" or parts[8] != "INVALID":
                    raise ValueError("diagnostic frame shape")
                applied, invalid = float(parts[3]), int(parts[9])
                if not math.isfinite(applied) or abs(applied) > 255 or invalid < 0:
                    raise ValueError("diagnostic value")
                self.diagnostics[parts[1]] = (applied, invalid)
            elif parts[0] == "CFG" and len(parts) == 3 and parts[1] in CONFIG_KEYS:
                value = float(parts[2])
                if not math.isfinite(value):
                    raise ValueError("configuration is not finite")
                self.config[parts[1]] = value
        except (ValueError, OverflowError) as exc:
            raise CalibrationError(f"Malformed telemetry: {line}") from exc
        return line

    def command(self, line, expected, timeout=2.0):
        self.send(line)
        deadline = self.clock() + timeout
        seen = []
        while self.clock() < deadline:
            response = self.read()
            seen.append(response)
            if response == expected:
                return seen
        raise CalibrationError(f"No acknowledgement for {line}")

    def get_config(self):
        self.config.clear()
        self.command("CFG GET", "OK CFG GET")
        if set(self.config) != set(CONFIG_KEYS):
            raise CalibrationError("Incomplete configuration snapshot")
        if any(not 0 <= self.config[key] <= 255 for key in PWM_KEYS):
            raise CalibrationError("PWM configuration outside range")
        return dict(self.config)

    def get_diagnostics(self):
        self.diagnostics.clear()
        self.send("DIAG")
        deadline = self.clock() + 2
        while len(self.diagnostics) < 4 and self.clock() < deadline:
            self.read()
        if len(self.diagnostics) != 4:
            raise CalibrationError("Missing per-wheel diagnostics")
        return dict(self.diagnostics)

    def healthy(self):
        now = self.clock()
        if not self.frame or now - self.frame.received > 0.6:
            raise CalibrationError("Encoder telemetry is stale")
        if any(now - stamp > 0.6 for stamp in (self.imu_at, self.health_at, self.nav_at)):
            raise CalibrationError("IMU or control-state telemetry is stale")
        if self.nav != (0, 0) or self.health != self.initial_health:
            raise CalibrationError("Unexpected navigation state or IMU reset")
        if now - self.began > TIME_BUDGET:
            raise CalibrationError("Calibration reached its 20-minute time limit")

    def rest(self):
        self.command("X", "OK STOP")
        began = self.clock()
        still_since = None
        previous = self.frame
        while self.clock() - began < 6:
            self.read()
            self.healthy()
            if self.frame is previous:
                continue
            if previous and self.frame.counts == previous.counts:
                if still_since is None:
                    still_since = previous.received
            else:
                still_since = None
            previous = self.frame
            if still_since is not None and self.clock() - began >= 1 and self.frame.received - still_since >= 0.6:
                if any(abs(v[0]) > 0.1 for v in self.get_diagnostics().values()):
                    raise CalibrationError("Motor output is nonzero while stopped")
                return
        raise CalibrationError("Wheels did not settle to rest within six seconds")

    def prepare(self):
        # Serial open can reset the board. Stop throughout a bounded startup wait.
        until = self.clock() + 2.0
        self.send("X")
        while self.clock() < until:
            self.read()
        self.command("X", "OK STOP")
        help_lines = self.command("?", "Watchdog: 300 ms")
        if not any(line.startswith("Maker mapping: FL=M2 FR=M3 RL=M1 RR=M0;") for line in help_lines):
            raise CalibrationError("Connected firmware is not the expected Maker mapping")
        self.original = self.get_config()
        deadline = self.clock() + 2
        while self.clock() < deadline:
            self.read()
            if self.nav and self.health and self.frame and self.imu_at:
                break
        if self.nav is None or self.nav[1] != 0:
            raise CalibrationError("Turn field mode off before calibration; its reference cannot be restored exactly")
        self.changed = True  # Includes uncertainty if the acknowledgement is lost.
        self.command("CFG SET heading-enabled 0", "OK CFG SET heading-enabled")
        self.initial_health = self.health
        # Consume a fresh N frame after the setting change, not the cached state.
        self.nav_at = 0
        deadline = self.clock() + 2
        while not self.nav_at and self.clock() < deadline:
            self.read()
        self.healthy()
        self.armed = True
        self.rest()

    def set_pwm(self, values):
        if len(values) != 4 or any(type(v) is not int or not 0 <= v <= MAX_PWM for v in values):
            raise CalibrationError("PWM outside the tested 0..177 range")
        for key, value in zip(PWM_KEYS, values):
            self.command(f"CFG SET {key} {value}", f"OK CFG SET {key}")
        actual = self.get_config()
        if any(actual[key] != value for key, value in zip(PWM_KEYS, values)) or actual["heading-enabled"] != 0:
            raise CalibrationError("Controller did not apply the requested settings")

    def pulse(self, values, direction, seconds):
        if direction not in (-1, 1) or not 1 <= seconds <= 3:
            raise CalibrationError("Invalid pulse direction or duration")
        self.healthy()
        self.rest()
        self.set_pwm(values)
        before = self.get_diagnostics()
        self.healthy()
        if any(abs(v[0]) > 0.1 for v in before.values()):
            raise CalibrationError("Nonzero output before pulse")
        frames = [self.frame]
        started = self.clock()
        next_send = started
        requested_output_check = verified_output = False
        try:
            while self.clock() - started < seconds:
                self.healthy()
                now = self.clock()
                if now >= next_send:
                    # A missed lease must fail, never resume a delayed sequence.
                    if now - next_send > 0.15:
                        raise CalibrationError("Host command scheduling was delayed")
                    self.send(f"V {direction} 0 0")
                    next_send = now + 0.05
                if not requested_output_check and now - started >= 0.35:
                    self.diagnostics.clear()
                    self.send("DIAG")
                    requested_output_check = True
                previous = self.frame
                self.read()
                if requested_output_check and len(self.diagnostics) == 4 and not verified_output:
                    if any(abs(self.diagnostics[w][0] - direction * values[i]) > 1
                           for i, w in enumerate(WHEELS)):
                        raise CalibrationError("Applied motor PWM did not reach the requested test duty")
                    if any(self.diagnostics[w][1] != before[w][1] for w in WHEELS):
                        raise CalibrationError("Encoder invalid transitions increased during the pulse")
                    verified_output = True
                if self.frame is not previous:
                    frames.append(self.frame)
                    # Check wiring and polarity while powered, not just afterward.
                    for i, value in enumerate(values):
                        delta = direction * (self.frame.counts[i] - frames[0].counts[i])
                        if (value == 0 and abs(delta) >= MIN_TICKS) or (value and delta <= -MIN_TICKS):
                            raise CalibrationError("Unexpected wheel motion; check wiring and polarity")
        finally:
            self.send("X")
        if not verified_output:
            raise CalibrationError("Missing applied-PWM verification during the pulse")
        # Frames after X/coast are deliberately excluded from the observation.
        observation = analyse(frames, started, values, direction)
        self.rest()
        after = self.get_diagnostics()
        if any(after[w][1] != before[w][1] for w in WHEELS):
            raise CalibrationError("Encoder invalid transitions increased during the pulse")
        return observation

    def restore(self):
        """Always stop; restore RAM settings only. Never persist calibration."""
        self.armed = False
        self.send("X")
        if not self.changed or self.original is None:
            return
        # Discard pending responses so restoration acknowledgements are current.
        self.serial.reset_input_buffer()
        self.frame = None
        self.command("X", "OK STOP")
        for key in (*PWM_KEYS, "heading-enabled"):
            self.command(f"CFG SET {key} {self.original[key]:g}", f"OK CFG SET {key}")
        actual = self.get_config()
        if any(actual[key] != self.original[key] for key in (*PWM_KEYS, "heading-enabled")):
            raise CalibrationError("Original live settings could not be verified")
        self.command("X", "OK STOP")
        self.restored = True


class SimulatedRig:
    """Deterministic motor model for offline tests; not a hardware prediction."""
    def __init__(self, asymmetric=False):
        self.asymmetric = asymmetric
        self.pulses = []

    def pulse(self, pwms, direction, seconds):
        if any(not 0 <= p <= MAX_PWM for p in pwms):
            raise CalibrationError("Simulation PWM limit")
        self.pulses.append((list(pwms), direction, seconds))
        # Similar motors with unequal start friction; optional RL reverse drag.
        starts = (36, 38, 45, 40) if direction == 1 else (39, 40, 49, 42)
        gains = [0.40, 0.405, 0.38, 0.395]
        if self.asymmetric and direction == -1:
            gains[2] = 0.27
        rpm = [gains[i] * max(0, p - 12) if p >= starts[i] else 0 for i, p in enumerate(pwms)]
        return Observation(list(pwms), direction, rpm, [v > 0 for v in rpm],
                           [[0.0, 200.0] if v > 0 else None for v in rpm], 200.0)


def parser():
    result = argparse.ArgumentParser(description=__doc__)
    mode = result.add_mutually_exclusive_group(required=True)
    mode.add_argument("--simulate", action="store_true")
    mode.add_argument("--run", action="store_true", help="run supervised motor tests on the Pi")
    result.add_argument("--asymmetric-demo", action="store_true", help="simulate a slower RL reverse motor")
    result.add_argument("--port", default="/dev/ttyUSB0")
    result.add_argument("--max-pwm", type=int, default=MAX_PWM)
    result.add_argument("--wheels-up", action="store_true")
    result.add_argument("--bridge-stopped", action="store_true")
    result.add_argument("--normal-wiring", action="store_true", help="M2/E2=FL, M3/E3=FR, M1/E1=RL, M0/E0=RR")
    result.add_argument("--output", type=Path, help="new JSON report path; existing files are never overwritten")
    return result


def main(argv=None):
    args = parser().parse_args(argv)
    if not 32 <= args.max_pwm <= MAX_PWM:
        parser().error(f"--max-pwm must be between 32 and {MAX_PWM}")
    if args.run and not (args.wheels_up and args.bridge_stopped and args.normal_wiring):
        parser().error("A real run requires --wheels-up --bridge-stopped --normal-wiring")
    if args.run and args.asymmetric_demo:
        parser().error("--asymmetric-demo is for simulation only")
    path = args.output or Path("maker-tuning-" + datetime.now().strftime("%Y%m%d-%H%M%S-%f") + ".json")
    # Reserve the report before touching a serial port; don't lose results or
    # overwrite someone's earlier calibration after completing a motor test.
    try:
        output = path.open("x", encoding="utf-8")
    except OSError as exc:
        parser().error(f"Cannot create report: {exc}")
    rig, tuner, port = None, None, None
    report = {"status": "aborted", "simulated": args.simulate}
    exit_code = 0
    previous_handler = signal.getsignal(signal.SIGTERM)
    def interrupted(signum, frame):
        raise KeyboardInterrupt("Calibration interrupted")
    signal.signal(signal.SIGTERM, interrupted)
    try:
        if args.simulate:
            rig = SimulatedRig(args.asymmetric_demo)
        else:
            import serial  # pyserial is needed only on the actual Pi.
            port = serial.Serial(args.port, 115200, timeout=0.02,
                                 write_timeout=0.2, exclusive=True)
            rig = MakerSerialRig(port)
        tuner = Tuner(rig, args.max_pwm, progress=lambda text: print(text, flush=True))
        report = tuner.report
        if args.run:
            rig.prepare()
        tuner.run()
        exit_code = 0 if report["status"] == "bench_trim_verified" else 2
    except (Exception, KeyboardInterrupt) as exc:
        report["status"] = "aborted"
        report["error"] = str(exc) or type(exc).__name__
        exit_code = 1
    finally:
        if isinstance(rig, MakerSerialRig):
            report["original_live_settings"] = rig.original
            try:
                rig.restore()
                report["original_live_settings_restored"] = rig.restored
            except (Exception, KeyboardInterrupt) as exc:
                report["restore_error"] = str(exc) or type(exc).__name__
                report["original_live_settings_restored"] = False
                report["status"] = "aborted"
                exit_code = 1
            finally:
                # A second best-effort stop also covers restoration failure.
                try:
                    rig.send("X")
                except Exception:
                    pass
        if port is not None:
            port.close()
        if report["status"] == "aborted":
            report["recommended_shared_pwm"] = None
        report["settings_saved_to_controller"] = False
        json.dump(report, output, indent=2, allow_nan=False)
        output.write("\n")
        output.close()
        signal.signal(signal.SIGTERM, previous_handler)
    print(f"{report['status']}: {path.resolve()}")
    if report.get("error"):
        print(report["error"], file=sys.stderr)
    if report.get("restore_error"):
        print("RESTORE NOT CONFIRMED: " + report["restore_error"], file=sys.stderr)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
