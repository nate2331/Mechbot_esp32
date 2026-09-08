"""Observation, evidence and offline replay. This module has no robot transport."""

import copy
from datetime import datetime, timezone
import json
import math
import os
from pathlib import Path
import re
import tempfile
import threading
import time

from mechbot_evidence import summarize_report, compare_reports
from mechbot_observer import TelemetryObserver
from mechbot_odometry import validate_geometry
from mechbot_profiles import profile_for_firmware
from mechbot_recording import CaptureStore

ROOT = Path(__file__).resolve().parent
REPORT_LIMIT = 2 * 1024 * 1024
WHEELS = {'FL', 'FR', 'RL', 'RR'}


def _same_board(left, right):
    """Identity provenance can change while the calibrated board stays the same."""
    return all(left.get(key) == right.get(key) for key in
               ('id', 'firmware', 'encoder_wheels', 'counts_per_revolution'))


def _finite(value, name, maximum=None):
    try:
        if isinstance(value, bool) or not isinstance(value, (int, float)):
            raise ValueError(f'{name} must be a number')
        number = float(value)
        if not math.isfinite(number) or number < 0 or (maximum is not None and number > maximum):
            raise ValueError(f'{name} is out of range')
        return number
    except OverflowError as exc:
        raise ValueError(f'{name} is too large') from exc


def _read_json(path, limit):
    if path.is_symlink() or path.resolve() != path.absolute():
        raise ValueError('linked evidence files are not supported')
    with path.open('rb') as source:
        raw = source.read(limit + 1)
    if len(raw) > limit:
        raise ValueError('file exceeds the read limit')
    return json.loads(raw)


class _UnavailableCapture:
    """An explicit, inert failure state when the recording directory is unusable."""

    def __init__(self, error):
        self.error = ('recording unavailable: ' + str(error))[:255]

    def snapshot(self):
        return {'id': None, 'state': 'error', 'accepted': 0, 'written': 0,
                'dropped': 0, 'bytes': 0, 'error': self.error}

    def submit(self, direction, line):
        return False

    def stop(self, timeout=None):
        return self.snapshot()

    def _unavailable(self, *args, **kwargs):
        raise RuntimeError(self.error)

    prepare_start = start = list_recordings = read_recording = _unavailable


class OperationsService:
    def __init__(self, recording_dir=ROOT / 'recordings', report_dir=ROOT / 'test_results',
                 geometry_file=None):
        try:
            self.capture = CaptureStore(recording_dir)
        except (OSError, ValueError) as exc:
            self.capture = _UnavailableCapture(exc)
        self.observer = TelemetryObserver()
        self.report_dir = Path(report_dir).absolute()
        self.geometry_file = Path(geometry_file).absolute() if geometry_file else None
        self._state_lock = threading.RLock()
        self._configuration_lock = threading.RLock()
        self._replay_lock = threading.Lock()
        self._geometry_config = None
        self.config_error = None
        if self.geometry_file and self.geometry_file.exists():
            try:
                config = _read_json(self.geometry_file, 8192)
                config['geometry'] = validate_geometry(config['geometry'])
                expected = profile_for_firmware(config.get('firmware'))
                if (expected['id'] == 'unknown'
                        or set(expected['encoder_wheels']) != WHEELS
                        or set(expected['counts_per_revolution']) != WHEELS
                        or config.get('cpr') != expected['counts_per_revolution']):
                    raise ValueError('geometry calibration does not match the board profile')
                self._geometry_config = config
            except (OSError, ValueError, TypeError, KeyError) as exc:
                self.config_error = str(exc)

    def _restore_geometry(self, observed):
        config = self._geometry_config
        if (config and observed['geometry'] is None
                and observed['profile']['firmware'] == config['firmware']
                and observed['profile']['counts_per_revolution'] == config['cpr']):
            self.observer.configure_geometry(config['geometry'])

    def feed(self, line, host_time=None):
        now = time.monotonic() if host_time is None else host_time
        with self._state_lock:
            self.capture.submit('rx', line)
            event = self.observer.feed(line, now)
            if event and event.get('type') == 'ready':
                self._restore_geometry(self.observer.snapshot(now))
            return event

    def transmit(self, line):
        self.capture.submit('tx', line)

    def boundary(self, reason='disconnect', host_time=None):
        now = time.monotonic() if host_time is None else host_time
        with self._state_lock:
            self.capture.submit('boundary', reason)
            self.observer.boundary(reason, now)

    def snapshot(self, now=None):
        now = time.monotonic() if now is None else now
        with self._state_lock:
            return {'observed': self.observer.snapshot(now), 'capture': self.capture.snapshot(),
                    'config_error': self.config_error}

    def configure_geometry(self, geometry):
        with self._configuration_lock:
            return self._configure_geometry(geometry)

    def _configure_geometry(self, geometry):
        if self.capture.snapshot()['state'] in ('active', 'stopping'):
            raise RuntimeError('finish the recording before changing its measured geometry')
        measured = validate_geometry(geometry)
        before = self.observer.snapshot(time.monotonic())['profile']
        cpr = before['counts_per_revolution']
        if set(cpr) != WHEELS:
            raise ValueError('four calibrated wheel encoders are required')
        config = {'schema': 1, 'firmware': before['firmware'], 'cpr': copy.deepcopy(cpr),
                  'geometry': measured, 'measured_utc': datetime.now(timezone.utc).isoformat()}
        # HTTP worker performs disk IO before taking the observation lock.
        if self.geometry_file:
            self.geometry_file.parent.mkdir(parents=True, exist_ok=True)
            descriptor, temporary = tempfile.mkstemp(prefix='.geometry-', dir=self.geometry_file.parent)
            try:
                with os.fdopen(descriptor, 'w', encoding='utf-8') as target:
                    json.dump(config, target, allow_nan=False, indent=2)
                    target.flush()
                os.replace(temporary, self.geometry_file)
            finally:
                if os.path.exists(temporary):
                    os.unlink(temporary)
        with self._state_lock:
            self._geometry_config = config
            self.config_error = None
            if not _same_board(self.observer.snapshot(time.monotonic())['profile'], before):
                raise RuntimeError('controller changed; geometry retained for its matching profile')
            return self.observer.configure_geometry(measured)

    def reset_pose(self):
        with self._configuration_lock, self._state_lock:
            if self.capture.snapshot()['state'] in ('active', 'stopping'):
                raise RuntimeError('finish the recording before resetting its pose origin')
            geometry = self.observer.snapshot(time.monotonic())['geometry']
            if geometry is None:
                raise ValueError('measured geometry is required first')
            return self.observer.configure_geometry(geometry)

    def start_capture(self, status, data):
        with self._configuration_lock:
            return self._start_capture(status, data)

    def _start_capture(self, status, data):
        if not isinstance(data, dict):
            raise ValueError('capture settings must be an object')
        conditions = {}
        for key, maximum in (('label', 80), ('surface', 80), ('load', 120)):
            value = data.get(key, '')
            if not isinstance(value, str) or len(value) > maximum:
                raise ValueError(f'{key} must be text of at most {maximum} characters')
            conditions[key] = value.strip() or None
        voltage = data.get('battery_voltage')
        conditions['battery_voltage'] = None if voltage is None else _finite(voltage, 'battery_voltage', 60)
        preparation = self.capture.prepare_start()  # Filesystem checks stay off the sender's observation lock.
        with self._state_lock:
            observed = self.observer.snapshot(time.monotonic())
            metadata = {'label': conditions.pop('label'), 'conditions': conditions,
                        'profile': observed['profile'], 'geometry': observed['geometry'],
                        'geometry_calibration': (copy.deepcopy({key: self._geometry_config[key]
                            for key in ('firmware', 'cpr', 'geometry')})
                            if self._geometry_config else None),
                        'pose_origin': 'recording-relative', 'live_pose_at_start': observed['pose'],
                        'simulated': status.get('simulated') if isinstance(status.get('simulated'), bool) else None,
                        'started_utc': datetime.now(timezone.utc).isoformat(),
                        'serial_connected': status.get('serial_connected') is True}
            return self.capture.start(metadata, preparation=preparation)

    def _report_path(self, report_id):
        if not isinstance(report_id, str) or not re.fullmatch(r'[A-Za-z0-9_-]{1,120}', report_id):
            raise ValueError('invalid report id')
        root = self.report_dir
        path = root / report_id / 'results.json'
        if root.resolve() != root.absolute() or path.parent.resolve() != path.parent.absolute():
            raise ValueError('linked report directories are not supported')
        return path

    def _report(self, report_id):
        return _read_json(self._report_path(report_id), REPORT_LIMIT)

    def list_evidence(self):
        reports, errors = [], []
        if not self.report_dir.exists():
            return {'reports': [], 'errors': []}
        with os.scandir(self.report_dir) as entries:
            names = []
            for entry in entries:
                if len(names) >= 100:
                    errors.append({'id': '', 'error': 'Only the first 100 report directories are listed'})
                    break
                if entry.is_dir(follow_symlinks=False) and (Path(entry.path) / 'results.json').exists():
                    names.append(entry.name)
        for name in sorted(names, reverse=True):
            try:
                reports.append(summarize_report(self._report(name), name))
            except (OSError, ValueError, TypeError, KeyError) as exc:
                errors.append({'id': name, 'error': str(exc)})
        return {'reports': reports, 'errors': errors}

    def compare_evidence(self, left, right):
        return compare_reports(self._report(left), self._report(right), left, right)

    def replay(self, recording_id, until_s=None):
        # Separate observer per request. No reference to the live observer or transport.
        with self._replay_lock:
            bundle = self.capture.read_recording(recording_id)
            events = bundle['events']
            duration = events[-1]['offset_s'] if events else 0.0
            position = duration if until_s is None else min(_finite(until_s, 'until_s'), duration)
            metadata = bundle['metadata']
            profile_data = metadata.get('profile') or {}
            if not isinstance(profile_data, dict):
                raise ValueError('recording profile must be an object')
            profile = profile_for_firmware(profile_data.get('firmware'))
            observer = TelemetryObserver(profile=profile)
            geometry = metadata.get('geometry')
            geometry_profile = profile
            if geometry is not None:
                if (set(profile['encoder_wheels']) != WHEELS
                        or set(profile['counts_per_revolution']) != WHEELS
                        or profile_data.get('counts_per_revolution') != profile['counts_per_revolution']):
                    raise ValueError('recorded calibration does not match the known profile')
                geometry = validate_geometry(geometry)
            # A recording may begin disconnected, before persisted dimensions can
            # be applied. Keep their board identity independent of the initial one.
            calibration = metadata.get('geometry_calibration')
            if calibration is not None:
                if not isinstance(calibration, dict):
                    raise ValueError('recorded geometry calibration must be an object')
                geometry_profile = profile_for_firmware(calibration.get('firmware'))
                if (geometry_profile['id'] == 'unknown'
                        or set(geometry_profile['encoder_wheels']) != WHEELS
                        or set(geometry_profile['counts_per_revolution']) != WHEELS
                        or calibration.get('cpr') != geometry_profile['counts_per_revolution']):
                    raise ValueError('recorded calibration does not match the known profile')
                calibrated_geometry = validate_geometry(calibration.get('geometry'))
                if (geometry is not None and _same_board(profile, geometry_profile)
                        and geometry != calibrated_geometry):
                    raise ValueError('recorded geometry conflicts with its calibration')
                geometry = calibrated_geometry
            if geometry is not None and _same_board(profile, geometry_profile):
                observer.configure_geometry(geometry)
            tx = []
            for event in events:
                if event['offset_s'] > position:
                    break
                if event['direction'] == 'rx':
                    parsed = observer.feed(event['line'], event['offset_s'])
                    if parsed and parsed['type'] == 'ready' and geometry is not None:
                        current = observer.snapshot(event['offset_s'])['profile']
                        if _same_board(current, geometry_profile):
                            observer.configure_geometry(geometry)
                elif event['direction'] == 'boundary':
                    observer.boundary(event['line'], event['offset_s'])
                elif event['direction'] == 'tx':
                    tx.append(event)
                    del tx[:-20]
            return {'mode': 'replay', 'id': bundle['id'], 'duration_s': duration,
                    'position_s': position, 'observed': observer.snapshot(position),
                    'metadata': metadata, 'state': bundle['state'], 'dropped': bundle['dropped'], 'tx': tx}
