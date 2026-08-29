#!/usr/bin/env python3
"""USB keyboard teleop for esp32_mecanum_usb_controller.ino.

Controls:
  W/S  forward/reverse
  A/D  strafe left/right
  Q/E  rotate left/right
  X or Space  stop
  Ctrl-C       stop and exit

The active command expires if no repeated keypress arrives for 0.30 seconds.
Holding a key uses the terminal's normal key-repeat behavior. Commands are sent
at 20 Hz; the ESP32 independently stops after a 300 ms communication timeout.
"""

from __future__ import annotations

import argparse
import glob
import os
import select
import sys
import termios
import time
import tty

import serial


BAUD_RATE = 115200
SEND_INTERVAL_S = 0.05
KEY_TIMEOUT_S = 0.30

VELOCITIES = {
    "w": (1.0, 0.0, 0.0),
    "s": (-1.0, 0.0, 0.0),
    "a": (0.0, 1.0, 0.0),
    "d": (0.0, -1.0, 0.0),
    "q": (0.0, 0.0, 1.0),
    "e": (0.0, 0.0, -1.0),
}


def find_serial_port(requested: str | None) -> str:
    if requested:
        if not os.path.exists(requested):
            raise FileNotFoundError(f"Serial port does not exist: {requested}")
        return requested

    candidates = sorted(glob.glob("/dev/ttyACM*")) + sorted(
        glob.glob("/dev/ttyUSB*")
    )
    if not candidates:
        raise FileNotFoundError(
            "No ESP32 serial port found under /dev/ttyACM* or /dev/ttyUSB*."
        )
    if len(candidates) > 1:
        print(f"Multiple serial ports found; using {candidates[0]}")
        print("Use --port /dev/tty... to select another one.")
    return candidates[0]


def send_velocity(link: serial.Serial, velocity: tuple[float, float, float]) -> None:
    forward, left, ccw = velocity
    link.write(f"V {forward:.3f} {left:.3f} {ccw:.3f}\n".encode("ascii"))


def send_stop(link: serial.Serial) -> None:
    link.write(b"X\n")
    link.flush()


def drain_telemetry(link: serial.Serial) -> str | None:
    latest = None
    while link.in_waiting:
        raw = link.readline()
        if not raw.endswith(b"\n"):
            break
        line = raw.decode("utf-8", errors="replace").strip()
        if line:
            latest = line
    return latest


def run(port: str) -> None:
    print(f"Opening {port} at {BAUD_RATE} baud...")
    with serial.Serial(port, BAUD_RATE, timeout=0) as link:
        # Opening USB serial commonly resets the ESP32-S3.
        time.sleep(1.5)
        link.reset_input_buffer()
        send_stop(link)

        old_terminal = termios.tcgetattr(sys.stdin.fileno())
        tty.setcbreak(sys.stdin.fileno())
        try:
            print("WASD move | Q/E rotate | X or Space stop | Ctrl-C exit")
            print("Hold a key to move; releasing it stops within 0.30 seconds.")

            velocity = (0.0, 0.0, 0.0)
            last_motion_key = 0.0
            last_send = 0.0
            last_status = ""

            while True:
                now = time.monotonic()
                readable, _, _ = select.select([sys.stdin], [], [], 0.01)
                if readable:
                    key = sys.stdin.read(1).lower()
                    if key == "\x03":
                        raise KeyboardInterrupt
                    if key in VELOCITIES:
                        velocity = VELOCITIES[key]
                        last_motion_key = now
                    elif key in ("x", " "):
                        velocity = (0.0, 0.0, 0.0)
                        last_motion_key = 0.0
                        send_stop(link)

                if last_motion_key and now - last_motion_key > KEY_TIMEOUT_S:
                    velocity = (0.0, 0.0, 0.0)
                    last_motion_key = 0.0
                    send_stop(link)

                if now - last_send >= SEND_INTERVAL_S:
                    send_velocity(link, velocity)
                    last_send = now

                status = drain_telemetry(link)
                if status and status != last_status:
                    print(f"\r{status:<78}", end="", flush=True)
                    last_status = status

        except KeyboardInterrupt:
            print("\nStopping...")
        finally:
            send_stop(link)
            time.sleep(0.05)
            send_stop(link)
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old_terminal)
            print("Motors stopped. Teleop closed.")


def main() -> int:
    parser = argparse.ArgumentParser(description="ESP32 mecanum USB teleop")
    parser.add_argument(
        "--port",
        help="Serial device, such as /dev/ttyACM0; autodetected when omitted",
    )
    args = parser.parse_args()

    try:
        run(find_serial_port(args.port))
    except (FileNotFoundError, PermissionError, serial.SerialException) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
