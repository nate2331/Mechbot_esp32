#!/usr/bin/env python3
"""Command line interface for mechbot operations.

This module provides a small offline CLI that interacts with
``OperationsService`` from :mod:`mechbot_operations`.  It is intentionally
light‑weight and has no side effects on import.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from pathlib import Path

# Import the backend service.  The import is safe because it does not
# perform any I/O or start background threads.
from mechbot_operations import OperationsService

__all__ = ["main"]

# Default directories relative to this file.
_DEFAULT_RECORDING_DIR = Path(__file__).resolve().parent / "recordings"
_DEFAULT_REPORT_DIR = Path(__file__).resolve().parent / "test_results"


def _ensure_utf8(stream: "io.TextIOBase") -> None:
    """Reconfigure a stream to UTF‑8 if the method is available.

    The function mutates the stream in place.  It is safe to call on
    ``sys.stdout`` and ``sys.stderr``.
    """
    if hasattr(stream, "reconfigure"):
        try:
            stream.reconfigure(encoding="utf-8")
        except Exception:
            # Some environments (e.g. StringIO) may not support reconfigure.
            pass


def _print_json(value: object) -> None:
    """Serialize *value* to JSON and write it to stdout.

    The function guarantees that the entire JSON string is written in one
    operation, preventing partial output in the event of an exception.
    """
    json_str = json.dumps(value, allow_nan=False, ensure_ascii=False)
    sys.stdout.write(json_str + "\n")


def _error(message: str) -> int:
    """Write *message* to stderr and return the error exit code 2."""
    sys.stderr.write(message + "\n")
    return 2


def main(argv: list[str] | None = None) -> int:
    """Entry point for the CLI.

    Parameters
    ----------
    argv:
        Optional list of arguments.  If ``None`` (the default), the
        arguments are taken from :data:`sys.argv`.

    Returns
    -------
    int
        ``0`` on success, ``2`` on backend or serialization failure.
    """
    _ensure_utf8(sys.stdout)
    _ensure_utf8(sys.stderr)

    parser = argparse.ArgumentParser(prog="mechbot_ops")
    subparsers = parser.add_subparsers(dest="command", required=True)

    # reports
    reports_parser = subparsers.add_parser("reports", help="List evidence reports")
    reports_parser.add_argument(
        "--directory",
        dest="directory",
        metavar="DIR",
        help="Directory containing report files (overrides default)",
    )

    # compare
    compare_parser = subparsers.add_parser("compare", help="Compare two evidence sets")
    compare_parser.add_argument("LEFT", help="Left evidence identifier")
    compare_parser.add_argument("RIGHT", help="Right evidence identifier")
    compare_parser.add_argument(
        "--directory",
        dest="directory",
        metavar="DIR",
        help="Directory containing report files (overrides default)",
    )

    # recordings
    recordings_parser = subparsers.add_parser("recordings", help="List available recordings")
    recordings_parser.add_argument(
        "--directory",
        dest="directory",
        metavar="DIR",
        help="Directory containing recordings (overrides default)",
    )

    # replay
    replay_parser = subparsers.add_parser("replay", help="Replay a recording")
    replay_parser.add_argument("ID", help="Recording identifier to replay")
    replay_parser.add_argument(
        "--directory",
        dest="directory",
        metavar="DIR",
        help="Directory containing recordings (overrides default)",
    )
    replay_parser.add_argument(
        "--until",
        dest="until",
        type=float,
        metavar="SECONDS",
        help="Maximum duration to replay (seconds)",
    )
    replay_parser.add_argument(
        "--json",
        action="store_true",
        help="Compatibility flag; ignored – all output is JSON.",
    )

    args = parser.parse_args(argv)

    try:
        if args.command == "reports":
            report_dir = Path(args.directory) if args.directory else _DEFAULT_REPORT_DIR
            service = OperationsService(recording_dir=_DEFAULT_RECORDING_DIR, report_dir=report_dir)
            result = service.list_evidence()
            _print_json(result)
            return 0

        if args.command == "compare":
            report_dir = Path(args.directory) if args.directory else _DEFAULT_REPORT_DIR
            service = OperationsService(recording_dir=_DEFAULT_RECORDING_DIR, report_dir=report_dir)
            result = service.compare_evidence(args.LEFT, args.RIGHT)
            _print_json(result)
            return 0

        if args.command == "recordings":
            recording_dir = Path(args.directory) if args.directory else _DEFAULT_RECORDING_DIR
            service = OperationsService(recording_dir=recording_dir, report_dir=_DEFAULT_REPORT_DIR)
            result = {"recordings": service.capture.list_recordings()}
            _print_json(result)
            return 0

        if args.command == "replay":
            recording_dir = Path(args.directory) if args.directory else _DEFAULT_RECORDING_DIR
            if args.until is not None:
                if args.until < 0 or not math.isfinite(args.until):
                    return _error("--until must be a nonnegative finite number")
            service = OperationsService(recording_dir=recording_dir, report_dir=_DEFAULT_REPORT_DIR)
            result = service.replay(args.ID, until_s=args.until)
            _print_json(result)
            return 0

    except Exception as exc:  # pragma: no cover – backend or serialization errors
        return _error(str(exc))

    # Should never reach here
    return _error("Unknown command")


if __name__ == "__main__":  # pragma: no cover
    raise SystemExit(main())
