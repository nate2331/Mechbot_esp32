"""
Mechbot odometry utilities.

This module provides pure mathematical helpers for converting wheel
motions into robot pose changes.  The geometry must be measured
externally; this module does not command motors or perform any I/O.
"""

from __future__ import annotations

from math import isfinite, fsum
from typing import Dict, Mapping, Sequence

__all__ = ["validate_geometry", "inverse_wheel_motion"]

# ---------------------------------------------------------------------------
# Geometry validation
# ---------------------------------------------------------------------------

def validate_geometry(data: Mapping) -> Dict[str, float]:
    """Validate and normalise wheel geometry.

    Parameters
    ----------
    data:
        Mapping containing ``wheel_diameter_m``, ``wheelbase_m`` and
        ``track_width_m``.

    Returns
    -------
    dict
        New dictionary with exactly the three keys, all values converted to
        ``float``.

    Raises
    ------
    ValueError
        If any required key is missing, not a real number, ``bool`` or
        ``None``, or if the value is not finite, <= 0 or > 3.
    """
    if not isinstance(data, dict):
        raise ValueError("geometry must be a dictionary of measured dimensions")
    required = ["wheel_diameter_m", "wheelbase_m", "track_width_m"]
    result: Dict[str, float] = {}
    for key in required:
        if key not in data:
            raise ValueError(f"Missing required geometry key: {key}")
        value = data[key]
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f"Geometry value for {key} must not be a bool")
        try:
            fval = float(value)
        except Exception as exc:  # pragma: no cover - defensive
            raise ValueError(f"Geometry value for {key} is not a number") from exc
        if not isfinite(fval):
            raise ValueError(f"Geometry value for {key} must be finite")
        if fval <= 0 or fval > 3:
            raise ValueError(
                f"Geometry value for {key} must be >0 and <=3 metres (got {fval})"
            )
        result[key] = fval
    return result

# ---------------------------------------------------------------------------
# Inverse wheel motion
# ---------------------------------------------------------------------------

def inverse_wheel_motion(
    distances: Sequence[float], geometry: Mapping
) -> Dict[str, float]:
    """Convert wheel distances into robot motion components.

    Parameters
    ----------
    distances:
        Sequence of four finite numeric distances (metres) in the order
        ``[FL, FR, RL, RR]``.
    geometry:
        Mapping returned by :func:`validate_geometry`.

    Returns
    -------
    dict
        ``{"forward_m": ..., "left_m": ..., "yaw_rad": ...}``.

    Raises
    ------
    ValueError
        If ``distances`` is not a sequence of length four, contains non‑numeric
        or ``bool`` values, or any value is not finite.  Also raised if the
        derived components are not finite.
    """
    if not isinstance(distances, (list, tuple)):
        raise ValueError("distances must be a list or tuple of four numbers")
    if len(distances) != 4:
        raise ValueError("distances must contain exactly four elements")
    fl, fr, rl, rr = distances
    for name, val in zip(("FL", "FR", "RL", "RR"), (fl, fr, rl, rr)):
        if isinstance(val, bool) or not isinstance(val, (int, float)):
            raise ValueError(f"Distance {name} must not be a bool")
        try:
            fval = float(val)
        except Exception as exc:  # pragma: no cover - defensive
            raise ValueError(f"Distance {name} is not a number") from exc
        if not isfinite(fval):
            raise ValueError(f"Distance {name} must be finite")
        # replace in place for later use
        if name == "FL":
            fl = fval
        elif name == "FR":
            fr = fval
        elif name == "RL":
            rl = fval
        else:
            rr = fval
    geom = validate_geometry(geometry)
    wheelbase = geom["wheelbase_m"]
    track_width = geom["track_width_m"]
    # Compute motion components
    # Scale before summing so finite averages do not overflow intermediate sums.
    forward_m = fsum((fl / 4, fr / 4, rl / 4, rr / 4))
    left_m = fsum((-fl / 4, fr / 4, rl / 4, -rr / 4))
    denom = (wheelbase + track_width) / 2.0
    yaw_rad = fsum((-fl / 4, fr / 4, -rl / 4, rr / 4)) / denom
    for name, val in (
        ("forward_m", forward_m),
        ("left_m", left_m),
        ("yaw_rad", yaw_rad),
    ):
        if not isfinite(val):
            raise ValueError(f"Derived {name} is not finite")
    return {"forward_m": forward_m, "left_m": left_m, "yaw_rad": yaw_rad}

# ---------------------------------------------------------------------------
# Passive odometry implementation
# ---------------------------------------------------------------------------

from math import pi, cos, sin, hypot

__all__.append("PassiveOdometry")

# Helper to wrap angle to [-pi,pi]

def _wrap_angle(rad: float) -> float:
    """Wrap angle to the range [-π, π]."""
    return (rad + pi) % (2 * pi) - pi


def _sample_number(value) -> float:
    """Accept finite real JSON numbers without treating booleans as numbers."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError("sample value must be a number")
    try:
        result = float(value)
    except (OverflowError, ValueError) as exc:
        raise ValueError("sample value is outside the finite range") from exc
    if not isfinite(result):
        raise ValueError("sample value must be finite")
    return result

# Validate CPR dictionary

def _validate_cpr(cpr: Mapping) -> Dict[str, float]:
    if not isinstance(cpr, dict):
        raise ValueError("CPR must be a dictionary of wheel counts per revolution")
    required = ["FL", "FR", "RL", "RR"]
    result: Dict[str, float] = {}
    for key in required:
        if key not in cpr:
            raise ValueError(f"Missing CPR key: {key}")
        val = cpr[key]
        if isinstance(val, bool) or not isinstance(val, (int, float)):
            raise ValueError(f"CPR value for {key} must not be a bool")
        try:
            fval = float(val)
        except Exception as exc:  # pragma: no cover
            raise ValueError(f"CPR value for {key} is not a number") from exc
        if not isfinite(fval) or fval <= 0:
            raise ValueError(f"CPR value for {key} must be positive finite")
        result[key] = fval
    return result

class PassiveOdometry:
    """Passive odometry estimator.

    Parameters
    ----------
    geometry: Mapping | None
        Optional wheel geometry dictionary.  If provided, ``cpr`` must also
        be supplied.
    counts_per_revolution: Mapping | None
        Optional CPR dictionary.
    """

    def __init__(self, geometry: Mapping | None = None, counts_per_revolution: Mapping | None = None):
        self._geometry: Dict[str, float] | None = None
        self._cpr: Dict[str, float] | None = None
        self._pose = {
            "x_m": 0.0,
            "y_m": 0.0,
            "yaw_rad": 0.0,
            "distance_m": 0.0,
        }
        self._valid = False
        self._reason = "reset"
        if geometry is None and counts_per_revolution is not None:
            raise ValueError("geometry required when CPR is provided")
        if geometry is not None:
            if counts_per_revolution is None:
                raise ValueError("CPR required when geometry is provided")
            self.configure(geometry, counts_per_revolution)

    # ------------------------------------------------------------------
    def configure(self, geometry: Mapping, cpr: Mapping) -> Dict:
        """Configure geometry and CPR.

        Validation is performed before any state change.  On success the
        pose is reset and a snapshot is returned.
        """
        geom = validate_geometry(geometry)
        cpr_val = _validate_cpr(cpr)
        # All good, commit
        self._geometry = geom
        self._cpr = cpr_val
        self.reset()
        return self.snapshot()

    # ------------------------------------------------------------------
    def reset(self) -> Dict:
        """Reset pose to origin, keep configuration."""
        self._pose = {
            "x_m": 0.0,
            "y_m": 0.0,
            "yaw_rad": 0.0,
            "distance_m": 0.0,
        }
        self._valid = False
        self._reason = "reset"
        return self.snapshot()

    # ------------------------------------------------------------------
    def snapshot(self) -> Dict:
        """Return a JSON‑safe snapshot of the current state."""
        return {
            "configured": self._geometry is not None,
            "valid": self._valid,
            "reason": self._reason,
            **self._pose,
        }

    # ------------------------------------------------------------------
    def _unavailable(self, reason: str) -> Dict:
        self._valid = False
        self._reason = reason
        return self.snapshot()

    def update(self, rates: Mapping) -> Dict:
        """Consume a WheelRateEstimator sample.

        The sample must contain ``valid``, ``reason``, ``dt_s`` and
        ``ticks_per_s`` mapping.  See module documentation for details.
        """
        if self._geometry is None:
            return self._unavailable("geometry_required")
        if not isinstance(rates, dict) or type(rates.get("valid")) is not bool:
            return self._unavailable("invalid_sample")
        # Unavailable estimator output may intentionally omit rates and dt.
        # Preserve its explanation before validating fields for integration.
        if not rates["valid"]:
            try:
                reason = str(rates.get("reason") or "sample_unavailable")
            except (ValueError, TypeError, OverflowError):
                reason = "invalid_sample"
            return self._unavailable(reason)
        try:
            dt_s = _sample_number(rates["dt_s"])
            if not 0 < dt_s <= 1.5 or not isinstance(rates["ticks_per_s"], dict):
                raise ValueError("invalid sample interval or rate mapping")
            ticks = [_sample_number(rates["ticks_per_s"][key])
                     for key in ("FL", "FR", "RL", "RR")]
            circumference = pi * self._geometry["wheel_diameter_m"]
            distances = [(rate / self._cpr[key]) * circumference * dt_s
                         for key, rate in zip(("FL", "FR", "RL", "RR"), ticks)]
            motion = inverse_wheel_motion(distances, self._geometry)
        except (ValueError, TypeError, KeyError, OverflowError, ZeroDivisionError):
            return self._unavailable("invalid_sample")
        forward = motion["forward_m"]
        left = motion["left_m"]
        delta_yaw = motion["yaw_rad"]
        # Midpoint heading
        mid_yaw = _wrap_angle(self._pose["yaw_rad"] + delta_yaw / 2.0)
        world_dx = forward * cos(mid_yaw) - left * sin(mid_yaw)
        world_dy = forward * sin(mid_yaw) + left * cos(mid_yaw)
        new_x = self._pose["x_m"] + world_dx
        new_y = self._pose["y_m"] + world_dy
        new_yaw = _wrap_angle(self._pose["yaw_rad"] + delta_yaw)
        new_dist = self._pose["distance_m"] + hypot(forward, left)
        # Validate new values
        if not all(isfinite(v) for v in (new_x, new_y, new_yaw, new_dist)):
            return self._unavailable("invalid_sample")
        # Commit
        self._pose["x_m"] = new_x
        self._pose["y_m"] = new_y
        self._pose["yaw_rad"] = new_yaw
        self._pose["distance_m"] = new_dist
        self._valid = True
        self._reason = "ok"
        return self.snapshot()
