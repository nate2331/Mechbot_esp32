"""Telemetry parser for MechBot.

Provides a single function ``parse_event(line, host_time)`` that parses a
single line of serial output and returns a JSON‑safe dictionary or ``None``
for malformed input.

No external dependencies are used.
"""

from __future__ import annotations

import math
import re
from typing import Any, Dict, List, Optional

RVC_BANNERS = {
    f"MAKER_IMU_RVC_{kind} V1 | UART1 RX21 TX=none | 115200 8N1":
        f"MAKER_IMU_RVC_{kind}_V1" for kind in ('CONTINUOUS', 'MOVEMENT')
}
RVC_HELP_READY = ('READY requires 5 sequential checksum-valid frames, <=100ms apart; '
                  'STALE after >500ms without a valid frame.')


def _rvc_fields(tokens, names):
    if len(tokens) != len(names):
        return None
    fields = {}
    for token, name in zip(tokens, names):
        key, separator, value = token.partition('=')
        if not separator or key != name:
            return None
        fields[key] = value
    return fields


def _parse_rvc(tokens):
    kind = tokens[0]
    if kind == 'RUN':
        names = ('boot_ms state bytes frames new window_ms age_ms first_frame_boot_ms '
                 'first_ready_boot_ms acquisitions pauses max_gap_ms ready_ms longest_ready_ms').split()
        fields = _rvc_fields(tokens[1:], names)
        if fields is None or fields['state'] not in ('READY', 'STALE', 'SYNCING', 'UART_FAILED'):
            return None
        for key in names:
            if key == 'state':
                continue
            fields[key] = -1 if key == 'age_ms' and fields[key] == '-1' else _parse_uint32(fields[key])
            if fields[key] is None:
                return None
        return dict(type='rvc', record='run', **fields)
    if kind == 'COUNTS':
        names = 'bad_checksum discontinuities repeats post_first_ready_bad post_first_ready_discontinuities'.split()
        fields = _rvc_fields(tokens[1:], names)
        if fields is None:
            return None
        fields = {key: _parse_uint32(value) for key, value in fields.items()}
        return None if None in fields.values() else dict(type='rvc', record='counts', **fields)
    if kind == 'VALUE':
        fields = _rvc_fields(tokens[1:], ['ypr_deg', 'accel_mg', 'changes_ypr', 'changes_accel'])
        if fields is None:
            return None
        for key in ('ypr_deg', 'accel_mg'):
            parser = _parse_float if key == 'ypr_deg' else _parse_int64
            values = [parser(value) for value in fields[key].split(',')]
            limit = 327.68 if key == 'ypr_deg' else 32768
            if len(values) != 3 or any(value is None or not -limit <= value < limit for value in values):
                return None
            fields[key] = values
        for key in ('changes_ypr', 'changes_accel'):
            fields[key] = _parse_uint32(fields[key])
            if fields[key] is None:
                return None
        return dict(type='rvc', record='value', **fields)
    if kind == 'UART_EVENTS' and len(tokens) == 8 and tokens[1:3] == [
            'total/post_first_ready', 'fifo,buffer,frame,parity,break=']:
        pairs = [[_parse_uint32(value) for value in token.split('/')] for token in tokens[3:]]
        if any(len(pair) != 2 or None in pair for pair in pairs):
            return None
        return dict(type='rvc', record='uart', totals=[pair[0] for pair in pairs],
                    post_first_ready=[pair[1] for pair in pairs])
    return None

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _is_finite_nonneg(x: float) -> bool:
    return math.isfinite(x) and x >= 0.0


def _parse_uint32(tok: str) -> Optional[int]:
    if not re.fullmatch(r'[+-]?[0-9]+', tok):
        return None
    try:
        v = int(tok)
    except ValueError:
        return None
    return v if 0 <= v <= 0xFFFFFFFF else None


def _parse_int64(tok: str) -> Optional[int]:
    if not re.fullmatch(r'[+-]?[0-9]+', tok):
        return None
    try:
        v = int(tok)
    except ValueError:
        return None
    return v if -0x8000000000000000 <= v <= 0x7FFFFFFFFFFFFFFF else None


def _parse_float(tok: str) -> Optional[float]:
    try:
        v = float(tok)
    except ValueError:
        return None
    return v if math.isfinite(v) else None

# ---------------------------------------------------------------------------
# Main parser
# ---------------------------------------------------------------------------

def parse_event(line: Any, host_time: Any) -> Optional[Dict[str, Any]]:
    """Parse a single telemetry line.

    Parameters
    ----------
    line: str
        Raw line from the serial port.
    host_time: float
        Timestamp from the host system.

    Returns
    -------
    dict or None
        JSON‑safe dictionary or ``None`` if the line is malformed.
    """
    if not isinstance(line, str) or not isinstance(host_time, (int, float)):
        return None
    if isinstance(host_time, bool):
        return None
    try:
        host_time_f = float(host_time)
    except (ValueError, OverflowError):
        return None
    if len(line) > 1024:
        return None
    if not _is_finite_nonneg(host_time_f):
        return None

    tokens = line.strip().split()
    if not tokens:
        return None

    t0 = tokens[0]
    out: Dict[str, Any] = {
        "raw": line,
        "host_time": float(host_time),
    }

    if line.strip() in RVC_BANNERS:
        return dict(out, type='ready', firmware=RVC_BANNERS[line.strip()])
    if line.strip() == RVC_HELP_READY:
        return None
    if t0 == 'EVENT' and len(tokens) == 4 and tokens[1] == 'STALE':
        fields = _rvc_fields(tokens[2:], ['boot_ms', 'age_ms'])
        if fields:
            fields = {key: _parse_uint32(value) for key, value in fields.items()}
            if None not in fields.values():
                return dict(out, type='rvc', record='stale', **fields)
        return None
    if t0 in ('RUN', 'COUNTS', 'VALUE', 'UART_EVENTS'):
        rvc = _parse_rvc(tokens)
        return dict(out, **rvc) if rvc else None

    # Encoder record
    if t0 == "T" and len(tokens) == 6:
        dev_ms = _parse_uint32(tokens[1])
        if dev_ms is None:
            return None
        counts = [_parse_int64(tok) for tok in tokens[2:]]
        if None in counts:
            return None
        out.update(
            {
                "type": "encoders",
                "device_ms": dev_ms,
                "counts": counts,
            }
        )
        return out

    # Diagnostic record
    if t0 == "D" and len(tokens) == 10 and tokens[2] == "PWM" and tokens[4] == "A" and tokens[6] == "B" and tokens[8] == "INVALID":
        wheel = tokens[1]
        if wheel not in {"FL", "FR", "RL", "RR"}:
            return None
        pwm = _parse_float(tokens[3])
        if pwm is None or not (-255 <= pwm <= 255):
            return None
        a = _parse_uint32(tokens[5])
        b = _parse_uint32(tokens[7])
        inv = _parse_uint32(tokens[9])
        if None in (a, b, inv):
            return None
        out.update(
            {
                "type": "diagnostic",
                "wheel": wheel,
                "pwm": pwm,
                "a_edges": a,
                "b_edges": b,
                "invalid_transitions": inv,
            }
        )
        return out

    # IMU record
    if t0 in ('IR1', 'IR2'):
        if len(tokens) != (15 if t0 == 'IR2' else 14) or tokens[3] not in ('READY', 'STALE', 'SYNCING', 'OFFLINE'):
            return None
        dev_ms = _parse_uint32(tokens[1])
        age_ms = -1 if tokens[2] == '-1' else _parse_uint32(tokens[2])
        angles = [_parse_float(token) for token in tokens[4:7]]
        acceleration = [_parse_int64(token) for token in tokens[7:10]]
        accepted = _parse_uint32(tokens[10])
        errors = [_parse_uint32(token) for token in tokens[11:14]]
        if (dev_ms is None or age_ms is None or accepted not in (0, 1) or None in errors or
                any(value is None or not -327.68 <= value < 327.68 for value in angles) or
                any(value is None or not -32768 <= value <= 32767 for value in acceleration)):
            return None
        heading = _parse_float(tokens[14]) if t0 == 'IR2' else math.radians(angles[0])
        if t0 == 'IR2':
            expected = (-math.radians(angles[0]) + math.pi) % (2*math.pi) - math.pi
            if heading is None or not -math.pi-1e-6 <= heading <= math.pi+1e-6 or abs((heading-expected+math.pi) % (2*math.pi)-math.pi) > 1e-5:
                return None
        valid = tokens[3] == 'READY' and 0 <= age_ms <= 500
        return dict(out, type='imu', transport='uart-rvc', device_ms=dev_ms,
                    frame_age_ms=age_ms, valid=valid,
                    reason='ready' if valid else tokens[3].lower() if tokens[3] != 'READY' else 'stale',
                    yaw_rad=heading if valid else None, ypr_deg=angles,
                    heading_convention='ccw-positive' if t0 == 'IR2' else 'raw-unverified',
                    raw_acceleration_mg=acceleration, quaternion=None, gyro=None,
                    acceleration=None, status=None, heading_accepted=bool(accepted),
                    control_ready=valid and bool(accepted) and t0 == 'IR2',
                    bad_checksum=errors[0], discontinuities=errors[1], uart_errors=errors[2])
    if t0 == "I":
        if len(tokens) == 3 and tokens[2] in {"WAIT", "STALE", "OFFLINE"}:
            dev_ms = _parse_uint32(tokens[1])
            if dev_ms is None:
                return None
            out.update(
                {
                    "type": "imu",
                    "device_ms": dev_ms,
                    "valid": False,
                    "reason": tokens[2].lower(),
                }
            )
            return out
        if len(tokens) == 13:
            dev_ms = _parse_uint32(tokens[1])
            if dev_ms is None:
                return None
            nums = [_parse_float(tok) for tok in tokens[2:12]]
            if None in nums:
                return None
            status = _parse_int64(tokens[12])
            if status is None or not (0 <= status <= 3):
                return None
            qx, qy, qz, qw = nums[0:4]
            scale = max(abs(v) for v in nums[:4])
            if scale == 0:
                return None
            scaled = [v / scale for v in nums[:4]]
            norm = math.sqrt(sum(v * v for v in scaled))
            qx, qy, qz, qw = [v / norm for v in scaled]
            yaw = math.atan2(2 * (qw * qz + qx * qy), 1 - 2 * (qy * qy + qz * qz))
            out.update(
                {
                    "type": "imu",
                    "device_ms": dev_ms,
                    "valid": True,
                    "quaternion": [qx, qy, qz, qw],
                    "gyro": nums[4:7],
                    "acceleration": nums[7:10],
                    "status": status,
                    "yaw_rad": yaw,
                }
            )
            return out
        return None

    # READY record
    if t0 == "READY" and len(tokens) >= 2:
        out.update({"type": "ready", "firmware": " ".join(tokens[1:])})
        return out

    # Event records
    if t0 in {"WARN", "FAULT", "ERR"} and len(tokens) >= 2:
        out.update({"type": "event", "level": t0.lower(), "message": " ".join(tokens[1:])})
        return out

    return None

# End of file

class WheelRateEstimator:
    """Estimate wheel RPM from encoder counts.

    Parameters
    ----------
    counts_per_revolution: dict[str, float] | None
        Mapping wheel name to counts per revolution.  Values must be
        finite and >0.  Missing wheels are allowed; those wheels will
        produce ``None`` for RPM.
    wheels: tuple[str, ...]
        Subset of ``("FL","FR","RL","RR")`` to include in output.
    max_gap_ms: int
        Maximum allowed time gap between samples before a reset.
    """

    def __init__(self, counts_per_revolution=None, wheels=('FL', 'FR', 'RL', 'RR'), max_gap_ms=1500):
        if counts_per_revolution is None:
            counts_per_revolution = {}
        if not isinstance(counts_per_revolution, dict):
            raise ValueError("counts_per_revolution must be a dict")
        self._cpr = {}
        for w, c in counts_per_revolution.items():
            if isinstance(c, bool) or not isinstance(c, (int, float)):
                raise ValueError(f"invalid CPR for wheel {w}")
            try:
                c = float(c)
            except OverflowError as exc:
                raise ValueError(f"invalid CPR for wheel {w}") from exc
            if not math.isfinite(c) or c <= 0:
                raise ValueError(f"invalid CPR for wheel {w}")
            self._cpr[w] = float(c)
        valid_wheels = {"FL", "FR", "RL", "RR"}
        if not isinstance(wheels, (list, tuple)):
            raise ValueError("wheels must be a sequence")
        for w in wheels:
            if w not in valid_wheels:
                raise ValueError(f"invalid wheel name {w}")
        self.wheels = tuple(wheels)
        try:
            if (isinstance(max_gap_ms, bool) or not isinstance(max_gap_ms, (int, float))
                    or not math.isfinite(max_gap_ms) or not 0 < max_gap_ms < 2**31):
                raise ValueError("max_gap_ms must be positive and below half the device clock range")
        except OverflowError as exc:
            raise ValueError("max_gap_ms is too large") from exc
        self.max_gap_ms = max_gap_ms
        self._reset()

    def _reset(self):
        self._baseline_ms = None
        self._baseline_counts = None
        self._prev_host_time = None

    def reset(self):
        self._reset()

    def update(self, device_ms, counts, host_time):
        # Validate inputs
        if isinstance(device_ms, bool) or not isinstance(device_ms, int) or not (0 <= device_ms <= 0xFFFFFFFF):
            raise ValueError("device_ms must be uint32")
        if isinstance(host_time, bool) or not isinstance(host_time, (int, float)):
            raise ValueError("host_time must be a number")
        try:
            host_time = float(host_time)
        except OverflowError as exc:
            raise ValueError("host_time is too large") from exc
        if not math.isfinite(host_time) or host_time < 0:
            raise ValueError("host_time must be finite non‑negative")
        if not isinstance(counts, (list, tuple)) or len(counts) != 4:
            raise ValueError("counts must be a 4‑tuple of int")
        for c in counts:
            if isinstance(c, bool) or not isinstance(c, int) or not -(2**63) <= c < 2**63:
                raise ValueError("counts must be integers")
        # Prepare output dict
        out = {
            "valid": False,
            "reason": None,
            "dt_s": None,
            "device_ms": device_ms,
            "host_time": host_time,
            "counts": list(counts),
            "ticks_per_s": {w: None for w in self.wheels},
            "rpm": {w: None for w in self.wheels},
        }
        # First sample
        if self._baseline_ms is None:
            self._baseline_ms = device_ms
            self._baseline_counts = list(counts)
            self._prev_host_time = host_time
            out["valid"] = False
            out["reason"] = "initial"
            return out
        # Compute elapsed ms with rollover
        elapsed = (device_ms - self._baseline_ms) & 0xFFFFFFFF
        if elapsed == 0:
            out["valid"] = False
            out["reason"] = "duplicate"
            # replace baseline
            self._baseline_ms = device_ms
            self._baseline_counts = list(counts)
            self._prev_host_time = host_time
            return out
        dt_s = elapsed / 1000.0
        out["dt_s"] = dt_s
        # Host time check
        if host_time <= self._prev_host_time:
            out["valid"] = False
            out["reason"] = "host_clock"
            self._baseline_ms = device_ms
            self._baseline_counts = list(counts)
            self._prev_host_time = host_time
            return out
        # Gap check
        if elapsed > self.max_gap_ms:
            if device_ms < self._baseline_ms:
                out["valid"] = False
                out["reason"] = "clock_reset"
            else:
                out["valid"] = False
                out["reason"] = "gap"
            self._baseline_ms = device_ms
            self._baseline_counts = list(counts)
            self._prev_host_time = host_time
            return out
        # Counter jump check
        candidate_rates = {}
        candidate_rpm = {}
        for w in self.wheels:
            i = ('FL', 'FR', 'RL', 'RR').index(w)
            delta = counts[i] - self._baseline_counts[i]
            rate = delta / dt_s
            if abs(rate) > 1_000_000:
                out["valid"] = False
                out["reason"] = "counter_jump"
                self._baseline_ms = device_ms
                self._baseline_counts = list(counts)
                self._prev_host_time = host_time
                return out
            candidate_rates[w] = rate
            if w in self._cpr:
                candidate_rpm[w] = rate * 60.0 / self._cpr[w]
                if not math.isfinite(candidate_rpm[w]):
                    out['reason'] = 'counter_jump'
                    self._baseline_ms = device_ms
                    self._baseline_counts = list(counts)
                    self._prev_host_time = host_time
                    return out
        out['ticks_per_s'].update(candidate_rates)
        out['rpm'].update(candidate_rpm)
        out["valid"] = True
        out["reason"] = "ok"
        # Replace baseline
        self._baseline_ms = device_ms
        self._baseline_counts = list(counts)
        self._prev_host_time = host_time
        return out

# End of file
