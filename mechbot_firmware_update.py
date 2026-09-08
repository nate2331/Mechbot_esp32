#!/usr/bin/env python3
"""Compile and upload only the target identified by the live bridge."""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import urllib.request

from mechbot_profiles import PROFILES, require_profile

BASE = "http://127.0.0.1:8765"
CLI = os.path.expanduser("~/.local/bin/arduino-cli")
SOURCE_ROOT = Path(os.path.expanduser("~/mechbot-src"))


def api(path, payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    request = urllib.request.Request(BASE + path, data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(request, timeout=5) as response:
        return json.load(response)


def build_plan(status, requested_board=None):
    if not status.get("serial_connected") or not status.get("serial_port"):
        raise RuntimeError("connected serial controller required")
    board = require_profile(status.get("firmware"), status.get("help_identity"))
    if requested_board and requested_board != board["id"]:
        raise RuntimeError("requested board does not match connected firmware")
    if status.get("gamepad_connected") or status.get("deadman"):
        raise RuntimeError("disconnect the controller before updating firmware")
    if status.get("calibration_active") or status.get("maintenance"):
        raise RuntimeError("finish tuning and maintenance before updating firmware")
    plan = {"board_id": board["id"], "fqbn": board["fqbn"],
            "sketch": str(SOURCE_ROOT / board["sketch"]),
            "port": status["serial_port"], "expected": board.get("update_firmware", board["firmware"])}
    if board.get('build_properties'):
        plan['source_firmware'] = board['firmware']
        plan['build_properties'] = board['build_properties']
        plan['output_dir'] = str(SOURCE_ROOT / '.maker-rvc-build')
    return plan


def verified_boot(status, plan, after):
    return (status.get("serial_connected") is True and
            status.get("serial_port") == plan["port"] and
            status.get("firmware") == plan["expected"] and
            status.get("firmware_received", 0) > after)


def perform_update(board_id=None, api_call=api, run=subprocess.run,
                   sleep=time.sleep, clock=time.time):
    plan = build_plan(api_call("/api/status"), board_id)
    print(f"Compiling {plan['board_id']} ({plan['fqbn']}) with one job...", flush=True)
    compile_args = [CLI, "compile", "--jobs", "1", "--fqbn", plan["fqbn"]]
    for setting in plan.get('build_properties', []):
        compile_args += ['--build-property', setting]
    if plan.get('output_dir'):
        compile_args += ['--output-dir', plan['output_dir']]
    run(compile_args + [plan['sketch']], check=True)
    current = build_plan(api_call("/api/status"), plan["board_id"])
    if current != plan:
        raise RuntimeError("controller changed during compilation; upload cancelled")
    api_call("/api/maintenance", {"enabled": True})
    upload_started = clock()
    try:
        sleep(1)
        print(f"Uploading {plan['board_id']} on {plan['port']}...", flush=True)
        upload_args = [CLI, "upload", "-p", plan["port"], "--fqbn", plan["fqbn"]]
        if plan.get('output_dir'):
            upload_args += ['--input-dir', plan['output_dir']]
        run(upload_args + [plan['sketch']], check=True)
    finally:
        api_call("/api/maintenance", {"enabled": False})
    deadline = clock() + 20
    while clock() < deadline:
        if verified_boot(api_call("/api/status"), plan, upload_started):
            print("Verified fresh READY " + plan["expected"], flush=True)
            return plan
        sleep(1)
    raise RuntimeError("upload finished but a fresh matching firmware READY was not received")


@contextmanager
def update_lock():
    # Pi/Linux: serialize CLI and dashboard invocations.
    import fcntl
    with (Path.home() / ".mechbot-firmware-update.lock").open("a") as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as exc:
            raise RuntimeError("another firmware updater is running") from exc
        yield


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--board", choices=PROFILES)
    parser.add_argument("--dry-run", action="store_true", help="read the plan without compiling or flashing")
    args = parser.parse_args()
    if args.dry_run:
        print(json.dumps(build_plan(api("/api/status"), args.board), indent=2))
        return
    with update_lock():
        perform_update(args.board)


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:
        print("ERROR:", exc, file=sys.stderr)
        sys.exit(1)
