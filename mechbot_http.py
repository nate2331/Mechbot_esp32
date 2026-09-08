"""Small HTTP adapter for the passive operations tools."""

import json
import math
import re
from urllib.parse import urlsplit


def validate_origin(handler):
    """The served dashboard may act on its own host; browser drive-by requests may not."""
    origin = handler.headers.get('Origin')
    if origin is None:
        if handler.headers.get('Sec-Fetch-Site') == 'cross-site':
            raise ValueError('cross-site action requests are not accepted')
        return  # Local scripts and the offline CLI need not send browser headers.
    parsed = urlsplit(origin)
    host = handler.headers.get('Host', '')
    if (parsed.scheme not in ('http', 'https') or not host or parsed.username is not None
            or parsed.password is not None or parsed.path or parsed.query or parsed.fragment
            or parsed.netloc.casefold() != host.casefold()):
        raise ValueError('action requests must come from this dashboard origin')


def read_json_body(handler, max_bytes=16384):
    length = handler.headers.get('Content-Length')
    if not isinstance(length, str) or not re.fullmatch(r'[0-9]+', length):
        raise ValueError('Content-Length must be a nonnegative decimal integer')
    length = int(length)
    if length > max_bytes:
        raise ValueError(f'request body exceeds {max_bytes} bytes')
    raw = handler.rfile.read(length)
    if len(raw) != length:
        raise ValueError('incomplete request body')
    try:
        data = json.loads(raw.decode('utf-8'))
        if not isinstance(data, dict):
            raise ValueError('request body must be a JSON object')
        # json.loads accepts NaN, Infinity and overflowing exponents by default.
        json.dumps(data, allow_nan=False)
    except (UnicodeError, ValueError, OverflowError, RecursionError) as exc:
        raise ValueError('request must contain a finite JSON object') from exc
    return data


def _reply(handler, operation):
    try:
        handler.reply(200, operation())
    except (ValueError, OSError, RuntimeError, TypeError, KeyError) as exc:
        handler.reply(400, {'error': str(exc)})
    return True


def handle_get(handler, path):
    if not path.startswith(('/api/operations', '/api/recordings', '/api/evidence')):
        return False
    service = handler.bridge.operations
    if path == '/api/operations':
        def status():
            bridge = handler.bridge.snapshot()
            return dict(service.snapshot(), bridge=bridge,
                        mode='simulated' if bridge.get('simulated') is True else 'live')
        return _reply(handler, status)
    if path == '/api/recordings':
        return _reply(handler, lambda: {'recordings': service.capture.list_recordings()})
    if path.startswith('/api/recordings/'):
        return _reply(handler, lambda: service.capture.read_recording(path[len('/api/recordings/'):]))
    if path == '/api/evidence':
        return _reply(handler, service.list_evidence)
    return False


def handle_post(handler, path, data):
    paths = {'/api/recordings/start', '/api/recordings/stop', '/api/replay',
             '/api/evidence/compare', '/api/geometry', '/api/odometry/reset'}
    if path not in paths:
        return False
    service = handler.bridge.operations
    if not isinstance(data, dict):
        handler.reply(400, {'error': 'request body must be an object'})
        return True
    if path == '/api/recordings/start':
        return _reply(handler, lambda: service.start_capture(handler.bridge.snapshot(), data))
    if path == '/api/recordings/stop':
        return _reply(handler, service.capture.stop)
    if path == '/api/replay':
        return _reply(handler, lambda: service.replay(data.get('id'), data.get('until_s')))
    if path == '/api/evidence/compare':
        return _reply(handler, lambda: service.compare_evidence(data.get('left'), data.get('right')))
    if path == '/api/geometry':
        return _reply(handler, lambda: service.configure_geometry(data))
    return _reply(handler, service.reset_pose)
