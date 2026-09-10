"""Pure encoder feedback for operator-triggered PWM tuning; no hardware IO."""
import math
import statistics

WHEELS = ("FL", "FR", "RL", "RR")
KEYS = tuple("pwm-" + w.lower() for w in WHEELS)


def measure(samples, cpr, signs):
    result = {"valid": False, "rpm": {}, "reason": "Run for five seconds to collect steady powered encoder samples."}
    # Samples are captured only between the first drive command and STOP.
    window = [s for s in samples if s["elapsed"] >= 1.0]
    if len(window) < 4:
        return result
    try:
        intervals = []
        for a, b in zip(window, window[1:]):
            dt = ((b["ms"] - a["ms"]) & 0xffffffff) / 1000
            if not 0 < dt <= .6:
                raise ValueError("Encoder timestamps reset, repeated, or have a gap over 600 ms.")
            intervals.append((dt, [signs[i] * (b["counts"][i]-a["counts"][i]) for i in range(4)]))
        duration = sum(dt for dt, _ in intervals)
        if duration < .6:
            return result
        rpm = {}
        for i, name in enumerate(WHEELS):
            calibration = float(cpr[name])
            if not math.isfinite(calibration) or calibration <= 0:
                raise ValueError("Missing wheel encoder calibration.")
            speeds = [counts[i] / dt for dt, counts in intervals]
            if any(counts[i] < 5 for _, counts in intervals):
                raise ValueError(f"{name} did not sustain motion in the commanded direction.")
            mean = sum(counts[i] for _, counts in intervals) / duration
            if statistics.pstdev(speeds) / mean > .30:
                raise ValueError(f"{name} speed varied too much for a steady-speed trim.")
            rpm[name] = round(mean * 60 / calibration, 3)
        result.update(valid=True, rpm=rpm, window_seconds=round(duration, 3),
                      samples=len(window), reason="Steady powered samples; first second and all stopping counts excluded.")
    except (ValueError, KeyError, TypeError, ZeroDivisionError) as exc:
        result["reason"] = str(exc)
    return result


def recommend(current, measurement, step=5, heading_on=False, quality="normal"):
    result = dict(kind="hold", source="encoders", summary="Keep PWM unchanged.",
                  basis=measurement.get("reason", "No steady encoder samples; repeat the run."),
                  step=step, deltas=dict.fromkeys(KEYS, 0), suggested_settings=dict(current))
    if heading_on:
        result["basis"] = "Turn heading correction off for PWM matching; correction changes individual wheel commands."
    elif quality != "normal":
        result["basis"] = "Motion was reported uneven or absent; repeat with sustained motion before applying encoder trims."
    elif measurement.get("valid"):
        speeds = measurement["rpm"]
        target = min(speeds.values())
        for key, wheel in zip(KEYS, WHEELS):
            if speeds[wheel] > target * 1.05:
                reduction = min(step, max(1, round(current[key] * (1-target/speeds[wheel]))))
                result["suggested_settings"][key] = max(0, current[key]-reduction)
                result["deltas"][key] = result["suggested_settings"][key]-current[key]
        changed = any(result["deltas"].values())
        result.update(kind="pwm-vector" if changed else "hold", target_rpm=target,
                      summary="Reduce faster wheels toward the slowest measured wheel, then repeat." if changed else "Wheel speeds are within 5%; keep PWM and repeat to confirm.",
                      basis=measurement["reason"] + " Calibrated RPM sets this bounded trim; observations do not calculate it. Encoder errors may bias results. This is not chassis heading control or continuous speed PID.")
    return result


def heading_result(samples):
    from mechbot_telemetry import parse_event
    angles = []
    previous_ms = None
    for sample in samples:
        event = parse_event(sample.get("imu"), sample["time"])
        if (not event or not event.get("valid") or
                event.get("heading_convention") != "ccw-positive" or
                not 0 <= sample["time"]-(sample.get("imu_updated") or 0) <= .5):
            return dict(valid=False, summary="Heading unavailable or stale during the run; no straightness verdict.")
        if previous_ms is not None and ((event["device_ms"]-previous_ms) & 0xffffffff) > 500:
            return dict(valid=False, summary="Heading timestamp reset or gap; no straightness verdict.")
        previous_ms = event["device_ms"]
        angles.append(event["yaw_rad"])
    if len(angles) < 3 or samples[-1]["time"]-samples[0]["time"] < .5:
        return dict(valid=False, summary="Too few heading samples for a straightness verdict.")
    total = 0.0
    peak = 0.0
    for a,b in zip(angles,angles[1:]):
        total += (b-a+math.pi) % (2*math.pi)-math.pi
        peak = max(peak,abs(total))
    degrees = math.degrees(total)
    return dict(valid=True, change_deg=round(degrees,2), peak_deg=round(math.degrees(peak),2),
                summary=f"Measured heading: {abs(degrees):.1f} degrees {'left / CCW' if degrees >= 0 else 'right / CW'}; maximum deviation {math.degrees(peak):.1f} degrees. Heading does not measure sideways drift.")
