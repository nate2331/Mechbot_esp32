"""Fail-closed telemetry checks for a supervised V2 motor-noise trial.

This module does not command motors. A caller must stop on TrialFault and must
not refresh a motion lease unless check() succeeds. It does not replace the
ESP32 watchdog or operator confirmation of the physical setup.
"""
import math

from mechbot_telemetry import parse_event


class TrialFault(RuntimeError):
    pass


class RvcTrialGuard:
    def __init__(self, *, heading_enabled=False, heading_accepted=False,
                 require_clean_encoders=True):
        self.heading_enabled = bool(heading_enabled)
        self.heading_accepted = bool(heading_accepted)
        self.require_clean_encoders = bool(require_clean_encoders)
        if self.heading_enabled and not self.heading_accepted:
            raise ValueError('Heading trial requires accepted heading')
        self.last_device_ms = None
        self.last_advance = None
        self.invalid = None
        self.fault = None

    def check(self, status, now, *, stopped=False):
        if self.fault:
            raise TrialFault(self.fault)
        try:
            self._check(status, now, stopped)
        except (KeyError, TypeError, ValueError, TrialFault) as error:
            self.fault = str(error) or 'Malformed telemetry'
            raise TrialFault(self.fault) from error

    def _check(self, s, now, stopped):
        def require(condition, message):
            if not condition:
                raise TrialFault(message)

        def fresh(stamp, limit):
            return isinstance(stamp, (int, float)) and math.isfinite(stamp) and 0 <= now - stamp <= limit

        require(math.isfinite(now), 'Invalid host clock')
        require(s['serial_connected'] and not s['maintenance'], 'Serial unavailable')
        require(not s['deadman'] and not s['gamepad_connected'] and
                not s['calibration_active'], 'Competing control source')
        require(s['board']['firmware'] == 'ESP32_MAKER_MECANUM_RVC_V2', 'Wrong firmware')
        require(fresh(s['updated'], 0.5) and fresh(s['imu_updated'], 0.5), 'Stale stream')
        require(s['imu'].startswith('IR2 '), 'Wrong IMU protocol')
        event = parse_event(s['imu'], now)
        require(event is not None and event['valid'], 'Invalid IMU')
        require(event['heading_accepted'] == self.heading_accepted,
                'Unexpected heading acceptance')
        require(event['bad_checksum'] == event['discontinuities'] ==
                event['uart_errors'] == 0, 'IMU communication fault')
        device_ms = event['device_ms']
        if self.last_device_ms is None:
            self.last_advance = now
        elif device_ms != self.last_device_ms:
            delta = (device_ms - self.last_device_ms) & 0xffffffff
            require(0 < delta <= 1000, 'Device reset or telemetry gap')
            self.last_advance = now
        require(now - self.last_advance <= 0.5, 'Frozen device time')
        self.last_device_ms = device_ms
        wheels = s['wheel_diagnostics']
        require(set(wheels) == {'FL', 'FR', 'RL', 'RR'}, 'Missing wheel diagnostics')
        invalid = {}
        for name, wheel in wheels.items():
            require(fresh(wheel['updated'], 1.5), 'Stale wheel diagnostics')
            require(math.isfinite(wheel['pwm']) and abs(wheel['pwm']) <= 177,
                    'Unexpected PWM')
            require(not stopped or wheel['pwm'] == 0, 'Output not stopped')
            count = wheel['invalid_transitions']
            require(type(count) is int and count >= 0, 'Invalid encoder counter')
            invalid[name] = count
        if self.invalid is not None and self.require_clean_encoders:
            require(invalid == self.invalid, 'Encoder error or counter reset')
        self.invalid = invalid
        nav = s['navigation'].split()
        expected = [str(int(self.heading_enabled)), '0', str(int(self.heading_accepted))]
        require(len(nav) == 9 and nav[0] == 'N' and nav[-3:] == expected,
                'Unexpected heading or field state')
        nav_age = (device_ms - int(nav[1])) & 0xffffffff
        require(nav_age <= 500 or nav_age >= 0xffffffff-20,
                'Stale navigation telemetry')
