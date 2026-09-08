"""Mechbot evidence processing module.

Summaries and comparisons preserve recorded facts and missing conditions.
Recommendations require complete measured verification, never status alone.

The implementation is intentionally pure Python – no file I/O, no
hardware interaction, no clocks, and no mutation of the input data.
"""

from __future__ import annotations

from copy import deepcopy
import math
from typing import Any, Dict

# ---------------------------------------------------------------------------
# Public API
# ---------------------------------------------------------------------------

def summarize_report(report: Any, report_id: str = "") -> Dict[str, Any]:
    """Validate *report* and return a JSON‑safe evidence dictionary.

    Parameters
    ----------
    report:
        The input report tree.  Must be a JSON‑serialisable structure
        consisting only of ``dict`` (with string keys), ``list``, ``str``,
        ``bool``, ``None`` and numeric types.  Floats must be finite.
    report_id:
        Optional identifier for the report.  Must be a string.

    Returns
    -------
    dict
        A new dictionary containing the evidence fields described in the
        task specification.

    Raises
    ------
    ValueError
        If the input does not satisfy the validation rules.
    """
    if not isinstance(report_id, str):
        raise ValueError("report_id must be a string")

    try:
        _validate_json_tree(report, path="root")
    except RecursionError as exc:
        raise ValueError("report nesting is too deep or cyclic") from exc

    if not isinstance(report, dict):
        raise ValueError("report must be a dictionary")

    out: Dict[str, Any] = {
        "id": report_id,
        "kind": _determine_kind(report),
        "status": "unrecorded",
        "simulated": None,
        "error": None,
        "restored": None,
        "maximum_pwm": None,
        "maximum_trial_pwm": None,
        "startup": _default_startup(),
        "directional_matching": _default_directional_matching(),
        "trial_count": 0,
        "completed_trial_count": 0,
        "recommended_shared_pwm": None,
        "conditions": _default_conditions(report),
    }

    # Optional fields
    if "status" in report:
        _validate_string_or_none(report["status"], "status")
        out["status"] = report["status"]
    if "error" in report:
        _validate_string_or_none(report["error"], "error")
        out["error"] = report["error"]
    if "restore_error" in report:
        _validate_string_or_none(report["restore_error"], "restore_error")
    if "simulated" in report:
        _validate_bool_or_none(report["simulated"], "simulated")
        out["simulated"] = report["simulated"]
    if "original_live_settings_restored" in report:
        _validate_bool_or_none(report["original_live_settings_restored"], "original_live_settings_restored")
        out["restored"] = report["original_live_settings_restored"]
    if "maximum_pwm" in report:
        out["maximum_pwm"] = _validate_pwm(report["maximum_pwm"], "maximum_pwm")

    # Extract recorded facts before evaluating whether a recommendation is justified.
    _details(report, out)
    rec_pwm = _recommendation(report, out)
    out["recommended_shared_pwm"] = rec_pwm

    return out

# ---------------------------------------------------------------------------
# Validation helpers
# ---------------------------------------------------------------------------

_VALID_WHEELS = ("FL", "FR", "RL", "RR")
_DIRECTIONS = ("forward", "reverse")


def _validate_json_tree(node: Any, path: str) -> None:
    """Recursively validate that *node* is JSON‑safe and contains only
    allowed types.
    """
    if isinstance(node, dict):
        for k, v in node.items():
            if not isinstance(k, str):
                raise ValueError(f"{path}: dictionary keys must be strings (got {k!r})")
            _validate_json_tree(v, f"{path}.{k}")
    elif isinstance(node, list):
        for idx, item in enumerate(node):
            _validate_json_tree(item, f"{path}[{idx}]")
    elif isinstance(node, (str, bool)):
        return
    elif node is None:
        return
    elif isinstance(node, int):
        return
    elif isinstance(node, float):
        if not (node == node and node != float("inf") and node != float("-inf")):
            raise ValueError(f"{path}: non‑finite float value {node!r}")
        return
    else:
        raise ValueError(f"{path}: unsupported type {type(node).__name__}")


def _validate_string_or_none(value: Any, name: str) -> None:
    if value is not None and not isinstance(value, str):
        raise ValueError(f"{name} must be a string or None")


def _validate_bool_or_none(value: Any, name: str) -> None:
    if value is not None and not isinstance(value, bool):
        raise ValueError(f"{name} must be a bool or None")


def _validate_pwm(value: Any, name: str) -> int | None:
    if value is None:
        return None
    if isinstance(value, bool):
        raise ValueError(f"{name} cannot be a bool")
    if not isinstance(value, int):
        raise ValueError(f"{name} must be an integer or None")
    if not (0 <= value <= 255):
        raise ValueError(f"{name} must be between 0 and 255 inclusive")
    return value

# ---------------------------------------------------------------------------
# Kind determination
# ---------------------------------------------------------------------------

def _determine_kind(report: Dict[str, Any]) -> str:
    if "schema" in report or "startup" in report:
        return "automatic"
    if "trials" in report:
        return "diagnostic"
    return "unknown"

# ---------------------------------------------------------------------------
# Default structures
# ---------------------------------------------------------------------------

def _default_startup() -> Dict[str, Any]:
    directions = {}
    for dir_name in ("forward", "reverse"):
        wheels = {}
        for wheel in _VALID_WHEELS:
            wheels[wheel] = {
                "status": "unrecorded",
                "minimum_start_pwm": None,
                "confirmation_starts": None,
            }
        directions[dir_name] = wheels
    return directions


def _default_directional_matching() -> Dict[str, Any]:
    return {
        "forward": {"status": "unrecorded", "pwm": None},
        "reverse": {"status": "unrecorded", "pwm": None},
    }

# ---------------------------------------------------------------------------
# Conditions defaults
# ---------------------------------------------------------------------------

def _default_conditions(report: Dict[str, Any]) -> Dict[str, Any]:
    cond: Dict[str, Any] = {
        "maximum_pwm": None,
        "wheel_order": None,
        "counts_per_revolution": None,
        "tolerance_fraction": None,
        "original_settings": None,
        "battery_voltage": None,
        "surface": None,
        "load": None,
    }
    # Copy known fields if present
    if "maximum_pwm" in report:
        cond["maximum_pwm"] = report["maximum_pwm"]
    if "wheel_order" in report:
        _validate_wheel_order(report["wheel_order"], "wheel_order")
        cond["wheel_order"] = list(report["wheel_order"])  # shallow copy
    if "counts_per_revolution" in report:
        _validate_cpr(report["counts_per_revolution"], "counts_per_revolution")
        cond["counts_per_revolution"] = list(report["counts_per_revolution"])  # shallow copy
    if "tolerance_fraction" in report:
        _validate_tolerance(report["tolerance_fraction"], "tolerance_fraction")
        cond["tolerance_fraction"] = report["tolerance_fraction"]
    # original_settings: prefer original_live_settings, else original_settings
    if "original_live_settings" in report:
        _validate_dict(report["original_live_settings"], "original_live_settings")
        cond["original_settings"] = deepcopy(report["original_live_settings"])
    elif "original_settings" in report:
        _validate_dict(report["original_settings"], "original_settings")
        cond["original_settings"] = deepcopy(report["original_settings"])
    for key in ("battery_voltage", "surface", "load"):
        if key in report:
            if key == "battery_voltage" and report[key] is not None:
                _number(report[key], key)
            cond[key] = deepcopy(report[key])
    return cond


def _validate_wheel_order(order: Any, name: str) -> None:
    if not isinstance(order, list) or len(order) != 4:
        raise ValueError(f"{name} must be a list of four wheels")
    seen = set()
    for w in order:
        if not isinstance(w, str) or w not in _VALID_WHEELS:
            raise ValueError(f"{name} contains invalid wheel {w!r}")
        if w in seen:
            raise ValueError(f"{name} contains duplicate wheel {w!r}")
        seen.add(w)


def _validate_cpr(cpr: Any, name: str) -> None:
    if not isinstance(cpr, list) or len(cpr) != 4:
        raise ValueError(f"{name} must be a list of four numbers")
    for idx, val in enumerate(cpr):
        _number(val, f"{name}[{idx}]")
        if val <= 0:
            raise ValueError(f"{name}[{idx}] must be a finite positive number")


def _validate_tolerance(val: Any, name: str) -> None:
    if not isinstance(val, (int, float)) or isinstance(val, bool):
        raise ValueError(f"{name} must be a number")
    if not (0 <= val <= 1 and val == val and val != float("inf") and val != float("-inf")):
        raise ValueError(f"{name} must be between 0 and 1 inclusive")


def _validate_dict(d: Any, name: str) -> None:
    if not isinstance(d, dict):
        raise ValueError(f"{name} must be a dictionary")

# ---------------------------------------------------------------------------
# Detail validation and conservative recommendation checks
# ---------------------------------------------------------------------------

def _number(value, name, maximum=None):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f'{name} must be a number')
    try:
        finite = math.isfinite(value)
    except OverflowError as exc:
        raise ValueError(f'{name} is too large') from exc
    if not finite or value < 0 or (maximum is not None and value > maximum):
        raise ValueError(f'{name} is out of range')
    return value


def _vector(value, name, maximum=None, boolean=False):
    if not isinstance(value, list) or len(value) != 4:
        raise ValueError(f'{name} must be a list of four values')
    for item in value:
        if boolean:
            if not isinstance(item, bool):
                raise ValueError(f'{name} must contain boolean values')
        else:
            _number(item, name, maximum)
    return list(value)


def _rows(value, name):
    if not isinstance(value, list) or any(not isinstance(row, dict) for row in value):
        raise ValueError(f'{name} must be a list of objects')
    return value


def _direction(value, name):
    if type(value) is not int or value not in (-1, 1):
        raise ValueError(f'{name} must be -1 or 1')


def _measurement(row, name, pwm_values):
    if 'pwm' in row:
        pwm_values.extend(_vector(row['pwm'], name + '.pwm', 255))
    if 'rpm' in row:
        _vector(row['rpm'], name + '.rpm')
    if 'direction' in row:
        _direction(row['direction'], name + '.direction')
    for key in ('passed', 'repeatable'):
        if key in row and not isinstance(row[key], bool):
            raise ValueError(f'{name}.{key} must be boolean')
    for key, maximum in (('spread_fraction', None), ('command_fraction', 1)):
        if key in row and row[key] is not None:
            _number(row[key], name + '.' + key, maximum)


def _details(report, out):
    """Extract facts; trial counts refer to top-level diagnostic attempts only."""
    pwm_values = []
    startup = report.get('startup', {})
    _validate_dict(startup, 'startup')
    for direction, wheel_rows in startup.items():
        if direction not in _DIRECTIONS:
            raise ValueError('startup has an unknown direction')
        _validate_dict(wheel_rows, 'startup.' + direction)
        for wheel, row in wheel_rows.items():
            if wheel not in _VALID_WHEELS:
                raise ValueError('startup has an unknown wheel')
            name = f'startup.{direction}.{wheel}'
            _validate_dict(row, name)
            status = row.get('status', 'unrecorded')
            if not isinstance(status, str):
                raise ValueError(name + '.status must be a string')
            target = out['startup'][direction][wheel]
            target['status'] = status
            minimum = row.get('minimum_reliable_start_pwm')
            confirmations = row.get('confirmation_starts')
            if 'minimum_reliable_start_pwm' in row:
                if minimum is None:
                    raise ValueError(name + '.minimum_reliable_start_pwm must be an integer')
                _validate_pwm(minimum, name + '.minimum_reliable_start_pwm')
            if 'confirmation_starts' in row:
                if type(confirmations) is not int or confirmations < 0:
                    raise ValueError(name + '.confirmation_starts must be a nonnegative integer')
            if status == 'measured' and minimum is not None and confirmations is not None:
                target.update(minimum_start_pwm=minimum, confirmation_starts=confirmations)
            for trial in _rows(row.get('trials', []), name + '.trials'):
                if 'pwm' in trial:
                    pwm_values.append(_number(trial['pwm'], name + '.trials.pwm', 255))
                if 'rpm' in trial:
                    _number(trial['rpm'], name + '.trials.rpm')
                if 'started_and_sustained' in trial and not isinstance(trial['started_and_sustained'], bool):
                    raise ValueError(name + '.started_and_sustained must be boolean')

    matches = report.get('directional_matching', {})
    _validate_dict(matches, 'directional_matching')
    for direction, row in matches.items():
        if direction not in _DIRECTIONS:
            raise ValueError('directional_matching has an unknown direction')
        name = 'directional_matching.' + direction
        _validate_dict(row, name)
        target = out['directional_matching'][direction]
        if 'converged' in row:
            if not isinstance(row['converged'], bool):
                raise ValueError(name + '.converged must be boolean')
            target['status'] = 'converged' if row['converged'] else 'not_converged'
        if row.get('pwm') is not None:
            target['pwm'] = _vector(row['pwm'], name + '.pwm', 255)
            pwm_values.extend(target['pwm'])
        for measurement in _rows(row.get('history', []), name + '.history'):
            _measurement(measurement, name + '.history', pwm_values)
    for row in _rows(report.get('shared_verification', []), 'shared_verification'):
        _measurement(row, 'shared_verification', pwm_values)

    trials = _rows(report.get('trials', []), 'trials')
    out['trial_count'] = len(trials)
    for trial in trials:
        if 'pwm' in trial:
            pwm_values.extend(_vector(trial['pwm'], 'trials.pwm', 255))
        if 'direction' in trial:
            _direction(trial['direction'], 'trials.direction')
        if 'observation' not in trial:
            continue
        observation = trial['observation']
        _validate_dict(observation, 'trials.observation')
        for key in ('pwm', 'rpm', 'sustained'):
            if key in observation:
                _vector(observation[key], 'observation.' + key,
                        255 if key == 'pwm' else None, boolean=key == 'sustained')
        if all(key in observation for key in ('pwm', 'rpm', 'sustained')):
            out['completed_trial_count'] += 1
    out['maximum_trial_pwm'] = max(pwm_values) if pwm_values else None


def _positive_pwm(value, maximum):
    return (isinstance(value, list) and len(value) == 4
            and all(type(item) is int and 0 < item <= maximum for item in value))


def _verified_measurement(row, direction, tolerance):
    if (row.get('direction') != direction or row.get('passed') is not True
            or row.get('repeatable') is not True):
        return False
    rpm = row.get('rpm')
    if not isinstance(rpm, list) or len(rpm) != 4 or not all(value > 0 for value in rpm):
        return False
    return (max(rpm) - min(rpm)) / min(rpm) <= tolerance


def _recommendation(report, out):
    """Only release a recorded recommendation backed by complete real evidence."""
    if (out['kind'] != 'automatic' or out['status'] != 'bench_trim_verified'
            or out['simulated'] is not False or out['error'] or report.get('restore_error')
            or out['restored'] is False or type(report.get('schema')) is not int
            or report['schema'] != 1 or report.get('wheel_order') != list(_VALID_WHEELS)):
        return None
    maximum, tolerance = out['maximum_pwm'], report.get('tolerance_fraction')
    if maximum is None or tolerance is None:
        return None
    for direction in _DIRECTIONS:
        for wheel in _VALID_WHEELS:
            row = out['startup'][direction][wheel]
            minimum, confirmations = row['minimum_start_pwm'], row['confirmation_starts']
            if (row['status'] != 'measured' or minimum is None or not 1 <= minimum <= maximum
                    or confirmations is None or confirmations < 3):
                return None
            trials = report['startup'][direction][wheel].get('trials', [])
            if len(trials) < 3 or any(trial.get('pwm') != minimum
                    or trial.get('started_and_sustained') is not True
                    or trial.get('rpm', 0) <= 0 for trial in trials[-3:]):
                return None
    vectors = []
    for direction, label in ((1, 'forward'), (-1, 'reverse')):
        match = report.get('directional_matching', {}).get(label, {})
        pwm, history = match.get('pwm'), match.get('history', [])
        if (match.get('converged') is not True or not _positive_pwm(pwm, maximum)
                or not history or not _verified_measurement(history[-1], direction, tolerance)
                or history[-1].get('pwm') != pwm):
            return None
        vectors.append(pwm)
    recommendation = report.get('recommended_shared_pwm')
    expected = [min(a, b) for a, b in zip(*vectors)]
    if not _positive_pwm(recommendation, maximum) or recommendation != expected:
        return None
    if 'shared_candidate_pwm' in report:
        candidate = report['shared_candidate_pwm']
        if not _positive_pwm(candidate, maximum) or candidate != expected:
            return None
    verification = report.get('shared_verification', [])
    if len(verification) != 6:
        return None
    pairs = set()
    for row in verification:
        direction, fraction = row.get('direction'), row.get('command_fraction')
        if type(direction) is not int or direction not in (-1, 1) or fraction not in (0.6, 0.8, 1.0):
            return None
        pair = (direction, fraction)
        spread = row.get('spread_fraction')
        if (pair in pairs or not _verified_measurement(row, direction, tolerance)
                or spread is None or not 0 <= spread <= tolerance
                or row.get('pwm') != [round(value * fraction) for value in recommendation]):
            return None
        pairs.add(pair)
    return list(recommendation)


def compare_reports(left, right, left_id='', right_id=''):
    """Compare recorded conditions and measured thresholds without causal claims."""
    a, b = summarize_report(left, left_id), summarize_report(right, right_id)
    conditions = {}
    for key, value in a['conditions'].items():
        other = b['conditions'][key]
        conditions[key] = {'left': deepcopy(value), 'right': deepcopy(other),
            'status': 'unrecorded' if value is None or other is None
                      else 'same' if value == other else 'different'}
    deltas = {}
    for direction in _DIRECTIONS:
        deltas[direction] = {}
        for wheel in _VALID_WHEELS:
            first, second = a['startup'][direction][wheel], b['startup'][direction][wheel]
            x, y = first['minimum_start_pwm'], second['minimum_start_pwm']
            deltas[direction][wheel] = (y - x if first['status'] == second['status'] == 'measured'
                                         and x is not None and y is not None else None)
    return {'left': a, 'right': b, 'conditions': conditions, 'threshold_deltas': deltas}
