import json
import math
import unittest

import mechbot_telemetry as telemetry


U32_MAX = 2**32 - 1
I64_MIN, I64_MAX = -(2**63), 2**63 - 1
WHEELS = ('FL', 'FR', 'RL', 'RR')


class ParseEventTest(unittest.TestCase):
    def parse(self, line, host_time=12.5):
        event = telemetry.parse_event(line, host_time)
        self.assertIsInstance(event, dict)
        self.assertEqual(event['raw'], line)
        self.assertEqual(event['host_time'], float(host_time))
        json.dumps(event, allow_nan=False)
        return event

    def test_encoder_boundaries_and_raw_preservation(self):
        event = self.parse(f' T {U32_MAX} {I64_MIN} {I64_MAX} -1 0\r\n', 0)
        self.assertEqual(event['type'], 'encoders')
        self.assertEqual(event['device_ms'], U32_MAX)
        self.assertEqual(event['counts'], [I64_MIN, I64_MAX, -1, 0])

    def test_diagnostics_all_wheels_and_limits(self):
        for wheel in WHEELS:
            for pwm in (-255, 0, 255):
                event = self.parse(f'D {wheel} PWM {pwm} A 0 B {U32_MAX} INVALID 42')
                self.assertEqual((event['type'], event['wheel'], event['pwm']),
                                 ('diagnostic', wheel, pwm))
                self.assertEqual((event['a_edges'], event['b_edges'],
                                  event['invalid_transitions']), (0, U32_MAX, 42))

    def test_imu_yaw_normalization_and_sensor_vectors(self):
        for scale in (2, 1e300, 1e-300):
            with self.subTest(scale=scale):
                event = self.parse(f'I 123 0 0 {scale} {scale} 1 -2 3 4 5 -6 3')
                self.assertEqual((event['type'], event['valid'], event['status']), ('imu', True, 3))
                self.assertEqual(len(event['quaternion']), 4)
                self.assertAlmostEqual(event['yaw_rad'], math.pi / 2)
                self.assertEqual(event['gyro'], [1, -2, 3])
                self.assertEqual(event['acceleration'], [4, 5, -6])

    def test_imu_wait_stale_and_text_events(self):
        for reason in ('WAIT', 'STALE', 'OFFLINE'):
            event = self.parse(f'I 456 {reason}')
            self.assertEqual((event['type'], event['device_ms'], event['valid'],
                              event['reason']), ('imu', 456, False, reason.lower()))
        self.assertEqual(self.parse('READY ESP32 Maker v2')['firmware'], 'ESP32 Maker v2')
        for level in ('WARN', 'FAULT', 'ERR'):
            event = self.parse(f'{level} encoder signal lost')
            self.assertEqual((event['type'], event['level'], event['message']),
                             ('event', level.lower(), 'encoder signal lost'))

    def test_malformed_records_and_numeric_ranges(self):
        invalid = [None, b'T 0 0 0 0 0', 123, '', 'UNKNOWN data', 'READY', 'ERR',
                   'T 1 2 3 4', 'T 1 2 3 4 5 6', 'T -1 0 0 0 0',
                   f'T {U32_MAX + 1} 0 0 0 0', f'T 0 {I64_MIN - 1} 0 0 0',
                   f'T 0 {I64_MAX + 1} 0 0 0', 'D XX PWM 0 A 0 B 0 INVALID 0',
                   'D FL pwm 0 A 0 B 0 INVALID 0', 'D FL PWM 0 A 0 B 0 INVALID',
                   'D FL PWM 0 A -1 B 0 INVALID 0', f'D FL PWM 0 A 0 B 0 INVALID {U32_MAX + 1}',
                   'I 0 WAIT extra', 'I -1 WAIT', 'I 0 stale', 'I 0 0 0 0 0 0 0 0 0 0 0 0']
        for integer in ('1.0', '1e2', '0x10', '1_0', '0_1', '--1'):
            invalid.extend((f'T {integer} 0 0 0 0', f'T 0 {integer} 0 0 0',
                            f'D FL PWM 0 A {integer} B 0 INVALID 0',
                            f'I 0 0 0 0 1 0 0 0 0 0 0 {integer}'))
        for number in ('nan', 'inf', '-inf', '256', '-256'):
            invalid.append(f'D FL PWM {number} A 0 B 0 INVALID 0')
        for status in ('-1', '4'):
            invalid.append(f'I 0 0 0 0 1 0 0 0 0 0 0 {status}')
        for index in range(2, 12):
            tokens = 'I 0 0 0 0 1 0 0 0 0 0 0 0'.split()
            tokens[index] = 'nan'
            invalid.append(' '.join(tokens))
        for line in invalid:
            with self.subTest(line=line):
                self.assertIsNone(telemetry.parse_event(line, 1))

    def test_host_time_and_line_length_validation(self):
        for timestamp in (-1, math.inf, -math.inf, math.nan, True, False, None, 10**400):
            with self.subTest(timestamp=timestamp):
                self.assertIsNone(telemetry.parse_event('T 0 0 0 0 0', timestamp))
        self.parse('WARN ' + 'x' * 1019)
        self.assertIsNone(telemetry.parse_event('WARN ' + 'x' * 1020, 1))


class WheelRateEstimatorTest(unittest.TestCase):
    def estimator(self, **kwargs):
        return telemetry.WheelRateEstimator(**kwargs)

    def check_result(self, result, valid, reason, wheels=WHEELS):
        self.assertEqual((result['valid'], result['reason']), (valid, reason))
        self.assertTrue({'dt_s', 'device_ms', 'host_time', 'counts',
                         'ticks_per_s', 'rpm'}.issubset(result))
        self.assertEqual(set(result['ticks_per_s']), set(wheels))
        self.assertEqual(set(result['rpm']), set(wheels))
        if not valid:
            self.assertTrue(all(value is None for value in result['ticks_per_s'].values()))
            self.assertTrue(all(value is None for value in result['rpm'].values()))
        json.dumps(result, allow_nan=False)

    def test_normal_rates_signed_rpm_and_copied_cpr(self):
        scales = {'FL': 100, 'FR': 200, 'RL': 400, 'RR': 800}
        estimator = self.estimator(counts_per_revolution=scales)
        scales['FL'] = 1
        self.check_result(estimator.update(100, [10, 20, 30, 40], 1), False, 'initial')
        result = estimator.update(600, [60, -30, 130, -160], 1.5)
        self.check_result(result, True, 'ok')
        self.assertEqual(result['dt_s'], 0.5)
        self.assertEqual(result['ticks_per_s'], dict(zip(WHEELS, [100, -100, 200, -400])))
        self.assertEqual(result['rpm'], dict(zip(WHEELS, [60, -30, 30, -30])))
        self.assertEqual((result['device_ms'], result['host_time'], result['counts']),
                         (600, 1.5, [60, -30, 130, -160]))

    def test_missing_cpr_and_s3_rear_wheel_selection(self):
        estimator = self.estimator(counts_per_revolution={'RR': 600}, wheels=('RL', 'RR'))
        estimator.update(0, [1000, 2000, 0, 0], 0)
        result = estimator.update(1000, [1100, 2200, 300, -600], 1)
        self.check_result(result, True, 'ok', ('RL', 'RR'))
        self.assertEqual(result['ticks_per_s'], {'RL': 300, 'RR': -600})
        self.assertEqual(result['rpm'], {'RL': None, 'RR': -60})
        unscaled = self.estimator()
        unscaled.update(0, [0] * 4, 0)
        self.assertEqual(unscaled.update(100, [10] * 4, 1)['rpm'], dict.fromkeys(WHEELS))

    def test_rollover_reset_gap_and_following_sample(self):
        cases = [(U32_MAX - 49, 50, 'ok'), (10000, 10, 'clock_reset'), (100, 1700, 'gap')]
        for first, second, reason in cases:
            with self.subTest(reason=reason):
                estimator = self.estimator()
                estimator.update(first, [0] * 4, 1)
                result = estimator.update(second, [10] * 4, 2)
                self.check_result(result, reason == 'ok', reason)
                follow = estimator.update(second + 100, [20] * 4, 3)
                self.check_result(follow, True, 'ok')
                self.assertEqual(follow['ticks_per_s']['FL'], 100)

    def test_valid_integer_limits(self):
        estimator = self.estimator()
        counts = [I64_MIN, I64_MAX, -1, 0]
        self.check_result(estimator.update(U32_MAX, counts, 0), False, 'initial')
        result = estimator.update(0, counts, 0.001)
        self.check_result(result, True, 'ok')
        self.assertEqual(result['dt_s'], 0.001)
        self.assertEqual(result['ticks_per_s'], dict.fromkeys(WHEELS, 0))

    def test_duplicate_and_host_clock_replace_baseline(self):
        for second_time, second_host, reason in ((100, 2, 'duplicate'),
                                                  (200, 1, 'host_clock'), (200, 0, 'host_clock')):
            estimator = self.estimator()
            estimator.update(100, [0] * 4, 1)
            self.check_result(estimator.update(second_time, [5] * 4, second_host), False, reason)
            result = estimator.update(second_time + 100, [15] * 4, 3)
            self.check_result(result, True, 'ok')
            self.assertEqual(result['ticks_per_s']['FL'], 100)

    def test_gap_and_jump_thresholds_then_reset(self):
        estimator = self.estimator(max_gap_ms=100)
        estimator.update(0, [0] * 4, 0)
        self.check_result(estimator.update(100, [100000] * 4, 1), True, 'ok')
        self.check_result(estimator.update(200, [200001] * 4, 2), False, 'counter_jump')
        self.assertEqual(estimator.update(300, [200011] * 4, 3)['ticks_per_s']['FL'], 100)
        self.check_result(estimator.update(401, [200021] * 4, 4), False, 'gap')
        estimator.reset()
        self.check_result(estimator.update(402, [200022] * 4, 5), False, 'initial')

    def test_constructor_rejects_invalid_scales_and_wheels(self):
        for cpr in (0, -1, math.nan, math.inf, -math.inf):
            with self.subTest(cpr=cpr), self.assertRaises(ValueError):
                self.estimator(counts_per_revolution={'FL': cpr})
        with self.assertRaises(ValueError):
            self.estimator(wheels=('RL', 'LEFT'))

    def test_invalid_update_does_not_change_baseline(self):
        invalid = [(-1, [0] * 4, 2), (U32_MAX + 1, [0] * 4, 2), (1.5, [0] * 4, 2),
                   (True, [0] * 4, 2), (200, [1, 2, 3], 2), (200, [1, 2, 3, 4, 5], 2),
                   (200, [I64_MIN - 1, 0, 0, 0], 2), (200, [I64_MAX + 1, 0, 0, 0], 2),
                   (200, [1.5, 0, 0, 0], 2), (200, ['1', 0, 0, 0], 2), (200, None, 2),
                   (200, [True, 0, 0, 0], 2), (200, [0] * 4, 10**400),
                   (200, [0] * 4, -1), (200, [0] * 4, math.inf),
                   (200, [0] * 4, math.nan), (200, [0] * 4, None)]
        for arguments in invalid:
            with self.subTest(arguments=arguments):
                estimator = self.estimator()
                estimator.update(100, [10] * 4, 1)
                with self.assertRaises(ValueError):
                    estimator.update(*arguments)
                result = estimator.update(300, [30] * 4, 3)
                self.check_result(result, True, 'ok')
                self.assertEqual(result['ticks_per_s']['FL'], 100)


if __name__ == '__main__':
    unittest.main()
