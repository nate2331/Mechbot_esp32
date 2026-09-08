"""Bounded passive telemetry coordination; no transport or disk access."""
from collections import deque
import copy
import math
import threading

from mechbot_telemetry import parse_event, WheelRateEstimator
from mechbot_odometry import PassiveOdometry, validate_geometry
from mechbot_profiles import profile_for_firmware, MAKER_HELP_IDENTITY, MAKER_RVC_HELP_IDENTITY, MAKER_RVC_V2_HELP_IDENTITY

WHEELS = ('FL', 'FR', 'RL', 'RR')


def _valid_time(value):
    try:
        return (not isinstance(value, bool) and isinstance(value, (int, float))
                and math.isfinite(value) and value >= 0)
    except OverflowError:
        return False


class TelemetryObserver:
    def __init__(self, profile=None, geometry=None):
        self._lock = threading.RLock()
        self._epoch = 0
        self._last_time = 0.0
        self._samples = deque(maxlen=300)
        self._events = deque(maxlen=100)
        self._set_profile(profile or profile_for_firmware(None))
        if geometry is not None:
            self.configure_geometry(geometry)

    def _set_profile(self, profile):
        self._profile = copy.deepcopy(profile)
        self._estimator = WheelRateEstimator(
            self._profile['counts_per_revolution'], self._profile['encoder_wheels'])
        self._odometry = PassiveOdometry()
        self._geometry = None
        self._rates = None
        self._imu = None
        self._rvc = {}
        self._diagnostics = {}
        self._samples.clear()

    def _event(self, code, message, now, level='info', **details):
        self._events.append(dict(type='event', code=code, message=message, host_time=now,
                                 epoch=self._epoch, level=level, **details))

    def boundary(self, reason, host_time=None):
        if not isinstance(reason, str) or not reason:
            raise ValueError('boundary requires a reason')
        if host_time is not None and not _valid_time(host_time):
            raise ValueError('host_time must be a finite nonnegative number')
        with self._lock:
            now = self._last_time if host_time is None else float(host_time)
            self._last_time = now
            self._epoch += 1
            self._set_profile(profile_for_firmware(None))
            self._event('boundary', reason, now)

    def configure_geometry(self, geometry):
        measured = validate_geometry(geometry)
        with self._lock:
            cpr = self._profile['counts_per_revolution']
            if set(self._profile['encoder_wheels']) != set(WHEELS) or set(cpr) != set(WHEELS):
                raise ValueError('four calibrated wheel encoders are required')
            candidate = PassiveOdometry(measured, cpr)
            self._odometry = candidate
            self._geometry = measured
            self._estimator.reset()
            self._rates = None
            self._samples.clear()
            return self.snapshot(self._last_time)

    def feed(self, line, host_time):
        event = parse_event(line, host_time)
        if not _valid_time(host_time):
            return None
        with self._lock:
            if line in (MAKER_HELP_IDENTITY, MAKER_RVC_HELP_IDENTITY, MAKER_RVC_V2_HELP_IDENTITY) and self._profile['id'] == 'unknown':
                profile = profile_for_firmware(None, line)
                event = dict(type='ready', firmware=profile['firmware'], host_time=float(host_time), raw=line)
            elif event and event['type'] == 'ready':
                profile = profile_for_firmware(event['firmware'])
            else:
                profile = None
            if event is None:
                return None
            now = float(host_time)
            self._last_time = now
            kind = event['type']
            if kind == 'ready':
                self._epoch += 1
                self._set_profile(profile)
                self._event('boundary', 'Controller identified: ' + profile['name'], now)
            elif kind == 'encoders':
                rates = self._estimator.update(event['device_ms'], event['counts'], now)
                if self._profile['id'] == 'unknown':
                    rates.update(valid=False, reason='profile_required')
                pose = self._odometry.update(rates)
                self._rates = rates
                self._samples.append(dict(host_time=now, device_ms=event['device_ms'],
                                          counts=list(event['counts']), rates=copy.deepcopy(rates),
                                          pose=pose))
            elif kind == 'diagnostic':
                wheel = event['wheel']
                previous = self._diagnostics.get(wheel)
                if previous:
                    counters = ('a_edges', 'b_edges', 'invalid_transitions')
                    if any(event[key] < previous[key] for key in counters):
                        self._event('encoder_counter_reset', wheel + ' diagnostic counters reset',
                                    now, wheel=wheel)
                    elif event['invalid_transitions'] > previous['invalid_transitions']:
                        delta = event['invalid_transitions'] - previous['invalid_transitions']
                        self._event('encoder_invalid', f'{wheel}: {delta} additional invalid transitions',
                                    now, 'warn', wheel=wheel, delta=delta)
                self._diagnostics[wheel] = copy.deepcopy(event)
            elif kind == 'imu':
                self._imu = copy.deepcopy(event)
            elif kind == 'rvc':
                record = event['record']
                if record == 'run':
                    previous = self._rvc.get('run')
                    if previous:
                        elapsed = (event['boot_ms'] - previous['boot_ms']) & 0xffffffff
                        if elapsed >= 2**31 or event['frames'] < previous['frames']:
                            self._rvc.clear()
                            self._event('rvc_restart', 'RVC device clock/counters restarted', now)
                        elif elapsed == 0 or event['frames'] == previous['frames']:
                            event = dict(event, state='NO_PROGRESS')
                    # VALUE has no timestamp: never pair a previous value with a new RUN.
                    self._rvc.pop('value', None)
                    self._rvc.pop('stale', None)
                self._rvc[record] = copy.deepcopy(event)
            elif kind == 'event':
                self._events.append(dict(copy.deepcopy(event), epoch=self._epoch))
            return copy.deepcopy(event)

    def snapshot(self, now):
        if not _valid_time(now):
            raise ValueError('now must be a finite nonnegative number')
        with self._lock:
            result = copy.deepcopy(dict(
                profile=self._profile, epoch=self._epoch, rates=self._rates, imu=self._imu,
                rvc=copy.deepcopy(self._rvc) if self._rvc else None,
                diagnostics=self._diagnostics, samples=list(self._samples), events=list(self._events),
                pose=self._odometry.snapshot(), geometry=self._geometry))
        rvc = result['rvc']
        if rvc:
            for record in rvc.values():
                difference = now - record['host_time']
                record.update(age_s=max(0.0, difference), fresh=0 <= difference <= 1.5)
            run, value = rvc.get('run'), rvc.get('value')
            reason = 'awaiting_run'
            if run:
                reason = ('host_clock' if now < run['host_time'] else 'stale') if not run['fresh'] else run['state'].lower()
                if reason == 'ready' and (run['frames'] < 5 or run['acquisitions'] == 0 or not 0 <= run['age_ms'] <= 500):
                    reason = 'stale'
                if reason == 'ready' and (not value or not value['fresh'] or
                        not 0 <= value['host_time'] - run['host_time'] <= 0.5):
                    reason = 'awaiting_value'
            if 'stale' in rvc:
                reason = 'stale'
            rvc.update(valid=reason == 'ready', reason=reason,
                       transport='uart-rvc', calibration_status=None,
                       gyro_available=False, acceleration_kind='raw_mg',
                       robot_control_ready=False)
        for name in ('rates', 'imu'):
            record = result[name]
            if record is None:
                continue
            difference = now - record['host_time']
            record.update(age_s=max(0.0, difference), fresh=0 <= difference <= 1.5)
            if not record['fresh']:
                record.update(valid=False, reason='host_clock' if difference < 0 else 'stale')
                if 'control_ready' in record:
                    record['control_ready'] = False
                if name == 'rates':
                    record['rpm'] = dict.fromkeys(record['rpm'])
                    record['ticks_per_s'] = dict.fromkeys(record['ticks_per_s'])
        for record in result['diagnostics'].values():
            difference = now - record['host_time']
            record.update(age_s=max(0.0, difference), fresh=0 <= difference <= 3)
        if result['rates'] and not result['rates']['valid'] and result['pose']['configured']:
            result['pose'].update(valid=False, reason=result['rates']['reason'])
        return result
