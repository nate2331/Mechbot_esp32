#!/usr/bin/env python3
"""Bluetooth gamepad teleop for the ESP32 mecanum USB controller."""

import argparse
import glob
import math
import os
import select
import struct
import sys
import time

import serial


SEND_HZ = 20.0
TELEMETRY_HZ = 1.0
DIRECTION_THRESHOLD = 0.35
MAX_SERIAL_BUFFER = 8192

# Xbox-compatible mapping reported by jstest.
AXIS_STRAFE = 0       # Left stick X
AXIS_FORWARD = 1      # Left stick Y (inverted below)
AXIS_ROTATION = 3     # Right stick X
BUTTON_DEADMAN = 4    # Left bumper (BtnTL)

JS_EVENT_BUTTON = 0x01
JS_EVENT_AXIS = 0x02
JS_EVENT_INIT = 0x80
JS_EVENT_FORMAT = "<IhBB"
JS_EVENT_SIZE = struct.calcsize(JS_EVENT_FORMAT)


def find_serial_port(requested):
    if requested:
        return requested
    ports = sorted(glob.glob("/dev/ttyACM*") + glob.glob("/dev/ttyUSB*"))
    if not ports:
        raise RuntimeError("No ESP32 serial port found under /dev/ttyACM* or /dev/ttyUSB*")
    return ports[0]


def normalized_axis(raw_value):
    return max(-1.0, min(1.0, raw_value / 32767.0))


def gamepad_motion(axes):
    """Return continuous left-stick translation and right-stick rotation.

    Apply the existing threshold radially to the left stick so small center
    noise is removed without collapsing or distorting the translation angle.
    Values outside the deadzone retain their proportional axis magnitudes.
    """
    forward = -normalized_axis(axes[AXIS_FORWARD])
    left = -normalized_axis(axes[AXIS_STRAFE])
    if math.hypot(forward, left) < DIRECTION_THRESHOLD:
        forward = left = 0.0

    rotation = -normalized_axis(axes[AXIS_ROTATION])
    if abs(rotation) < DIRECTION_THRESHOLD:
        rotation = 0.0

    return forward, left, rotation


def commanded_motion(axes, deadman_pressed):
    if not deadman_pressed:
        return 0.0, 0.0, 0.0
    return gamepad_motion(axes)


def send_velocity(ser, forward, strafe, rotation):
    ser.write(f"V {forward:.3f} {strafe:.3f} {rotation:.3f}\n".encode("ascii"))


def send_stop(ser):
    send_velocity(ser, 0.0, 0.0, 0.0)
    ser.write(b"X\n")
    ser.flush()


def handle_deadman_transition(ser, was_pressed, is_pressed):
    if was_pressed and not is_pressed:
        send_stop(ser)


def read_joystick_event(joystick):
    event = joystick.read(JS_EVENT_SIZE)
    if len(event) != JS_EVENT_SIZE:
        raise RuntimeError("Gamepad disconnected")
    return struct.unpack(JS_EVENT_FORMAT, event)


def quaternion_to_euler(qx, qy, qz, qw):
    """Return ROS-convention roll, pitch, and yaw in degrees."""
    sin_roll = 2.0 * (qw * qx + qy * qz)
    cos_roll = 1.0 - 2.0 * (qx * qx + qy * qy)
    roll = math.atan2(sin_roll, cos_roll)

    sin_pitch = 2.0 * (qw * qy - qz * qx)
    sin_pitch = max(-1.0, min(1.0, sin_pitch))
    pitch = math.asin(sin_pitch)

    sin_yaw = 2.0 * (qw * qz + qx * qy)
    cos_yaw = 1.0 - 2.0 * (qy * qy + qz * qz)
    yaw = math.atan2(sin_yaw, cos_yaw)

    return tuple(math.degrees(angle) for angle in (roll, pitch, yaw))


def parse_telemetry_line(line, telemetry):
    """Parse one ESP32 line; ignore boot/help text and malformed telemetry."""
    fields = line.split()
    if not fields:
        return

    if fields[0] == "T" and len(fields) == 6:
        try:
            telemetry["encoder"] = {
                "ms": int(fields[1]),
                "counts": tuple(int(value) for value in fields[2:6]),
                "received": time.monotonic(),
            }
        except ValueError:
            pass
        return

    if fields[0] != "I" or len(fields) < 3:
        return

    try:
        imu_ms = int(fields[1])
    except ValueError:
        return

    if fields[2] in {"OFFLINE", "WAIT", "STALE"}:
        telemetry["imu"] = {
            "ms": imu_ms,
            "state": " ".join(fields[2:]),
            "received": time.monotonic(),
        }
        return

    if len(fields) != 13:
        return

    try:
        values = [float(value) for value in fields[2:12]]
        status = int(fields[12])
    except ValueError:
        return

    qx, qy, qz, qw, gx, gy, gz, ax, ay, az = values
    roll, pitch, yaw = quaternion_to_euler(qx, qy, qz, qw)
    telemetry["imu"] = {
        "ms": imu_ms,
        "state": "OK",
        "quaternion": (qx, qy, qz, qw),
        "gyro": (gx, gy, gz),
        "acceleration": (ax, ay, az),
        "euler": (roll, pitch, yaw),
        "status": status,
        "received": time.monotonic(),
    }


def receive_serial_telemetry(ser, rx_buffer, telemetry):
    """Drain available serial bytes without blocking gamepad control."""
    waiting = ser.in_waiting
    if waiting <= 0:
        return

    rx_buffer.extend(ser.read(waiting))
    if len(rx_buffer) > MAX_SERIAL_BUFFER:
        del rx_buffer[:-MAX_SERIAL_BUFFER]

    while b"\n" in rx_buffer:
        raw_line, _, remainder = rx_buffer.partition(b"\n")
        rx_buffer[:] = remainder
        line = raw_line.decode("ascii", errors="replace").strip()
        parse_telemetry_line(line, telemetry)


def print_telemetry_summary(telemetry):
    encoder = telemetry.get("encoder")
    imu = telemetry.get("imu")

    if encoder:
        fl, fr, rl, rr = encoder["counts"]
        encoder_text = f"ENC FL={fl} FR={fr} RL={rl} RR={rr}"
    else:
        encoder_text = "ENC waiting"

    if not imu:
        imu_text = "IMU waiting"
    elif imu["state"] != "OK":
        imu_text = f"IMU {imu['state']}"
    else:
        roll, pitch, yaw = imu["euler"]
        _, _, gz = imu["gyro"]
        imu_text = (
            f"IMU yaw={yaw:7.1f} pitch={pitch:6.1f} roll={roll:6.1f} "
            f"gz={gz:+.3f} status={imu['status']}"
        )

    print(f"{encoder_text} | {imu_text}", flush=True)


def main():
    parser = argparse.ArgumentParser(description="Bluetooth gamepad mecanum teleop")
    parser.add_argument("--port", help="ESP32 serial port, e.g. /dev/ttyACM0")
    parser.add_argument("--joystick", default="/dev/input/js0", help="Linux joystick device")
    parser.add_argument(
        "--telemetry-hz",
        type=float,
        default=TELEMETRY_HZ,
        help="console telemetry summaries per second; use 0 to disable",
    )
    args = parser.parse_args()

    if args.telemetry_hz < 0:
        raise RuntimeError("--telemetry-hz must be zero or greater")

    if not os.path.exists(args.joystick):
        raise RuntimeError(f"Joystick not found: {args.joystick}")

    serial_port = find_serial_port(args.port)
    axes = [0] * 9
    buttons = [0] * 11

    print(f"Joystick: {args.joystick}")
    print(f"ESP32:    {serial_port}")
    print("Hold LEFT BUMPER to drive. Left stick moves; right stick X rotates.")
    print("Left-stick direction and magnitude are continuous through 360 degrees.")
    print("Release LEFT BUMPER for an immediate stop. Ctrl-C exits.")

    with open(args.joystick, "rb", buffering=0) as joystick, serial.Serial(
        serial_port, 115200, timeout=0, write_timeout=0.1
    ) as ser:
        # Opening USB serial may reset the ESP32; allow its firmware to boot.
        time.sleep(2.0)
        ser.reset_input_buffer()
        serial_rx = bytearray()
        telemetry = {}
        next_send = time.monotonic()
        next_telemetry_print = next_send
        deadman_was_pressed = False

        try:
            while True:
                if not os.path.exists(args.joystick):
                    raise RuntimeError("Gamepad disconnected")
                readable, _, _ = select.select([joystick], [], [], 0.01)
                if readable:
                    _, value, event_type, number = read_joystick_event(joystick)
                    event_type &= ~JS_EVENT_INIT
                    if event_type == JS_EVENT_AXIS and number < len(axes):
                        axes[number] = value
                    elif event_type == JS_EVENT_BUTTON and number < len(buttons):
                        buttons[number] = value

                deadman_pressed = bool(buttons[BUTTON_DEADMAN])
                handle_deadman_transition(
                    ser, deadman_was_pressed, deadman_pressed
                )
                deadman_was_pressed = deadman_pressed

                receive_serial_telemetry(ser, serial_rx, telemetry)

                now = time.monotonic()
                if now >= next_send:
                    forward, strafe, rotation = commanded_motion(
                        axes, deadman_pressed
                    )
                    send_velocity(ser, forward, strafe, rotation)
                    next_send = now + (1.0 / SEND_HZ)

                if args.telemetry_hz > 0 and now >= next_telemetry_print:
                    print_telemetry_summary(telemetry)
                    next_telemetry_print = now + (1.0 / args.telemetry_hz)

        except KeyboardInterrupt:
            pass
        finally:
            # The ESP32 watchdog is the backstop; also request a clean stop here.
            try:
                send_stop(ser)
            except (OSError, serial.SerialException):
                pass
            print("Stopped.")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, serial.SerialException) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
