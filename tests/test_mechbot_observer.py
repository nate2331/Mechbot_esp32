"""Offline observer contracts; synthetic dimensions are not robot calibration."""
import copy
from concurrent.futures import ThreadPoolExecutor
import json
import math
import unittest
from unittest import mock

from mechbot_observer import TelemetryObserver
from mechbot_profiles import MAKER_HELP_IDENTITY, PROFILES, profile_for_firmware


WHEELS = ('FL', 'FR', 'RL', 'RR')
GEOMETRY = {'wheel_diameter_m': 1 / math.pi, 'wheelbase_m': 0.4, 'track_width_m': 0.2}


def maker():
    profile = profile_for_firmware(PROFILES['maker']['firmware'])
    profile['counts_per_revolution'] = dict.fromkeys(WHEELS, 100)
    return profile


class ObserverTest(unittest.TestCase):
    def test_explicit_imu_offline_replaces_previously_valid_heading(self):
        observer = TelemetryObserver(maker())
        observer.feed('I 100 0 0 0 1 0 0 0 0 0 0 3', 1.0)
        self.assertTrue(observer.snapshot(1.0)['imu']['valid'])
        observer.feed('I 200 OFFLINE', 1.1)
        imu = observer.snapshot(1.1)['imu']
        self.assertFalse(imu['valid'])
        self.assertTrue(imu['fresh'])
        self.assertEqual(imu['reason'], 'offline')
        self.assertNotIn('yaw_rad', imu)

    def snapshot(self, observer, now):
        result = observer.snapshot(now)
        self.assertEqual(set(result), {'profile', 'epoch', 'rates', 'imu', 'diagnostics',
                                      'samples', 'events', 'pose', 'geometry'})
        json.dumps(result, allow_nan=False)
        return result

    def test_unknown_does_not_invent_encoder_scale_or_geometry(self):
        observer = TelemetryObserver()
        initial = self.snapshot(observer, 0)
        self.assertEqual(initial['profile']['id'], 'unknown')
        self.assertEqual(initial['epoch'], 0)
        self.assertIsNone(initial['geometry'])
        self.assertIsNone(initial['rates'])
        self.assertIsNone(initial['imu'])
        observer.feed('T 0 0 0 0 0', 0)
        observer.feed('T 100 10 10 10 10', 0.1)
        result = self.snapshot(observer, 0.1)
        self.assertFalse(result['rates']['valid'])
        self.assertEqual(result['rates']['reason'], 'profile_required')
        self.assertEqual(result['rates']['rpm'], {})
        self.assertFalse(result['pose']['configured'])
        with self.assertRaises(ValueError):
            observer.configure_geometry(GEOMETRY)

    def test_known_maker_rates_and_explicit_wheel_only_pose(self):
        observer = TelemetryObserver(maker(), GEOMETRY)
        event = observer.feed('T 0 0 0 0 0', 0)
        self.assertEqual(event['type'], 'encoders')
        observer.feed('T 500 50 50 50 50', 0.5)
        result = self.snapshot(observer, 0.5)
        self.assertEqual(result['rates']['ticks_per_s'], dict.fromkeys(WHEELS, 100))
        self.assertEqual(result['rates']['rpm'], dict.fromkeys(WHEELS, 60))
        self.assertAlmostEqual(result['pose']['x_m'], 0.5)
        self.assertTrue(result['pose']['valid'])
        sample = result['samples'][-1]
        self.assertTrue({'host_time', 'device_ms', 'counts', 'rates', 'pose'}.issubset(sample))
        self.assertEqual((sample['host_time'], sample['device_ms'], sample['counts']),
                         (0.5, 500, [50] * 4))
        observer.feed('I 500 0 0 1 1 0 0 0 0 0 0 3', 0.6)
        observer.feed('T 1000 100 100 100 100', 1)
        result = self.snapshot(observer, 1)
        self.assertAlmostEqual(result['pose']['x_m'], 1)
        self.assertAlmostEqual(result['pose']['y_m'], 0)
        self.assertAlmostEqual(result['pose']['yaw_rad'], 0)

    def test_s3_has_only_rear_rates_and_no_invented_rpm(self):
        observer = TelemetryObserver(profile_for_firmware(PROFILES['s3']['firmware']))
        observer.feed('T 0 1000 2000 0 0', 0)
        observer.feed('T 1000 1100 2200 300 -600', 1)
        result = self.snapshot(observer, 1)
        self.assertEqual(result['rates']['ticks_per_s'], {'RL': 300, 'RR': -600})
        self.assertEqual(result['rates']['rpm'], {'RL': None, 'RR': None})
        self.assertFalse(result['pose']['configured'])
        with self.assertRaises(ValueError):
            observer.configure_geometry(GEOMETRY)

    def test_ready_and_explicit_boundary_start_new_epochs(self):
        observer = TelemetryObserver(maker(), GEOMETRY)
        observer.feed('T 0 0 0 0 0', 0)
        observer.feed('T 500 50 50 50 50', 0.5)
        observer.feed('I 500 WAIT', 0.5)
        observer.feed('D FL PWM 0 A 1 B 1 INVALID 0', 0.5)
        observer.feed('WARN before restart', 0.5)
        observer.feed('READY ' + PROFILES['maker']['firmware'], 1)
        result = self.snapshot(observer, 1)
        self.assertEqual((result['epoch'], result['profile']['id']), (1, 'maker'))
        self.assertIsNone(result['rates'])
        self.assertIsNone(result['imu'])
        self.assertIsNone(result['geometry'])
        self.assertEqual((result['samples'], result['diagnostics']), ([], {}))
        self.assertEqual(result['pose']['x_m'], 0)
        self.assertTrue(any(event.get('message') == 'before restart' for event in result['events']))
        observer.boundary('serial disconnected', host_time=5)
        result = self.snapshot(observer, 5)
        self.assertEqual((result['epoch'], result['profile']['id']), (2, 'unknown'))
        self.assertEqual(result['events'][-1]['code'], 'boundary')
        self.assertEqual(result['events'][-1]['host_time'], 5)
        observer.feed('READY mystery_firmware', 6)
        self.assertEqual(self.snapshot(observer, 6)['profile']['id'], 'unknown')
        observer.boundary('default timestamp')
        self.assertEqual(self.snapshot(observer, 6)['events'][-1]['host_time'], 6)

    def test_exact_help_identity_only_identifies_unknown_profile(self):
        observer = TelemetryObserver()
        self.assertIsNone(observer.feed(MAKER_HELP_IDENTITY + ' altered', 1))
        self.assertEqual(self.snapshot(observer, 1)['profile']['id'], 'unknown')
        self.assertEqual(observer.feed(MAKER_HELP_IDENTITY, 2)['type'], 'ready')
        result = self.snapshot(observer, 2)
        self.assertEqual((result['profile']['id'], result['profile']['identity_source']),
                         ('maker', 'maker-help'))
        observer.feed('READY ' + PROFILES['s3']['firmware'], 3)
        before = self.snapshot(observer, 3)
        self.assertIsNone(observer.feed(MAKER_HELP_IDENTITY, 4))
        self.assertEqual(self.snapshot(observer, 3), before)

    def test_diagnostic_deltas_reset_without_negative_warnings(self):
        observer = TelemetryObserver(maker())
        for index, (a, b, invalid) in enumerate(((10, 10, 5), (20, 20, 8), (2, 2, 2), (3, 3, 4))):
            observer.feed(f'D RL PWM -177 A {a} B {b} INVALID {invalid}', index)
        result = self.snapshot(observer, 3)
        events = [event for event in result['events'] if event.get('code', '').startswith('encoder_')]
        self.assertEqual([event['code'] for event in events],
                         ['encoder_invalid', 'encoder_counter_reset', 'encoder_invalid'])
        self.assertEqual([event.get('delta') for event in events], [3, None, 2])
        self.assertTrue(all(event['wheel'] == 'RL' for event in events))
        self.assertEqual(result['diagnostics']['RL']['invalid_transitions'], 4)
        self.assertEqual(result['diagnostics']['RL']['pwm'], -177)
        observer.feed('D RL PWM 0 A 3 B 3 INVALID 4', 4)
        self.assertEqual(len(self.snapshot(observer, 4)['events']), len(events))

    def test_freshness_boundaries_preserve_history_and_raw_counters(self):
        observer = TelemetryObserver(maker())
        observer.feed('T 0 0 0 0 0', 0)
        observer.feed('T 100 10 10 10 10', 1)
        observer.feed('I 100 0 0 0 1 0 0 0 0 0 0 3', 1)
        observer.feed('D FR PWM 1 A 10 B 10 INVALID 2', 1)
        self.assertTrue(self.snapshot(observer, 2.5)['rates']['fresh'])
        self.assertTrue(self.snapshot(observer, 2.5)['imu']['valid'])
        stale = self.snapshot(observer, 2.500001)
        self.assertEqual((stale['rates']['valid'], stale['rates']['reason']), (False, 'stale'))
        self.assertEqual(stale['rates']['rpm'], dict.fromkeys(WHEELS))
        self.assertEqual(stale['rates']['ticks_per_s'], dict.fromkeys(WHEELS))
        self.assertFalse(stale['imu']['valid'])
        self.assertTrue(stale['samples'][-1]['rates']['valid'])
        self.assertEqual(stale['samples'][-1]['rates']['rpm']['FL'], 60)
        self.assertTrue(self.snapshot(observer, 4)['diagnostics']['FR']['fresh'])
        self.assertFalse(self.snapshot(observer, 4.000001)['diagnostics']['FR']['fresh'])
        self.assertEqual(self.snapshot(observer, 4.000001)['diagnostics']['FR']['a_edges'], 10)
        future = self.snapshot(observer, 0.5)
        self.assertFalse(future['rates']['fresh'])
        self.assertEqual(future['rates']['reason'], 'host_clock')
        self.assertTrue(self.snapshot(observer, 2.5)['rates']['valid'])

    def test_wait_stale_and_malformed_records_do_not_corrupt_state(self):
        observer = TelemetryObserver(maker())
        for index, reason in enumerate(('WAIT', 'STALE')):
            observer.feed(f'I {index} {reason}', index)
            self.assertFalse(self.snapshot(observer, index)['imu']['valid'])
            self.assertEqual(self.snapshot(observer, index)['imu']['reason'], reason.lower())
        for level in ('WARN', 'FAULT', 'ERR'):
            observer.feed(f'{level} recorded message', 1)
        before = self.snapshot(observer, 1)
        self.assertEqual([event['level'] for event in before['events']], ['warn', 'fault', 'err'])
        for line, timestamp in ((None, 1), ('T bad 0 0 0 0', 1), ('I 0 0 0 0 0 0 0 0 0 0 0 0', 1),
                                ('READY bad', True), (MAKER_HELP_IDENTITY, math.nan)):
            self.assertIsNone(observer.feed(line, timestamp))
        self.assertEqual(self.snapshot(observer, 1), before)

    def test_geometry_configuration_and_invalid_attempts_are_atomic(self):
        observer = TelemetryObserver(maker())
        observer.feed('T 0 0 0 0 0', 0)
        configured = observer.configure_geometry(GEOMETRY)
        self.assertEqual(configured['geometry'], GEOMETRY)
        self.assertTrue(configured['pose']['configured'])
        self.assertEqual(configured['samples'], [])
        observer.feed('T 500 50 50 50 50', 0.5)
        self.assertEqual(self.snapshot(observer, 0.5)['rates']['reason'], 'initial')
        before = self.snapshot(observer, 0.5)
        for geometry in ({}, {**GEOMETRY, 'wheelbase_m': math.nan}):
            with self.assertRaises(ValueError):
                observer.configure_geometry(geometry)
            self.assertEqual(self.snapshot(observer, 0.5), before)
        for now in (None, True, -1, math.nan, math.inf):
            with self.assertRaises(ValueError):
                observer.snapshot(now)

    def test_bounded_histories_keep_latest_samples_and_events(self):
        observer = TelemetryObserver(maker())
        for index in range(305):
            observer.feed(f'T {index * 100} {index} {index} {index} {index}', index / 10)
        for index in range(105):
            observer.feed(f'WARN warning {index}', 31 + index / 10)
        result = self.snapshot(observer, 42)
        self.assertEqual(len(result['samples']), 300)
        self.assertEqual(result['samples'][0]['device_ms'], 500)
        self.assertEqual(result['samples'][-1]['device_ms'], 30400)
        self.assertEqual(len(result['events']), 100)
        self.assertEqual(result['events'][0]['message'], 'warning 5')
        self.assertEqual(result['events'][-1]['message'], 'warning 104')

    def test_snapshots_and_returned_events_cannot_mutate_observer(self):
        profile = maker()
        observer = TelemetryObserver(profile, GEOMETRY)
        profile['counts_per_revolution']['FL'] = 1
        with mock.patch('builtins.open', side_effect=AssertionError('observer opened a file')):
            event = observer.feed('T 0 0 0 0 0', 0)
            event['counts'][0] = 999
            observer.feed('T 1000 100 100 100 100', 1)
            result = self.snapshot(observer, 1)
        self.assertEqual(result['rates']['rpm']['FL'], 60)
        original = copy.deepcopy(result)
        result['samples'][-1]['rates']['rpm']['FL'] = -99
        result['profile']['counts_per_revolution']['FL'] = 2
        result['geometry']['wheelbase_m'] = 2
        self.assertEqual(self.snapshot(observer, 1), original)

    def test_concurrent_feed_and_snapshot_remain_consistent(self):
        observer = TelemetryObserver(maker())
        def writer():
            for index in range(100):
                observer.feed(f'T {index * 10} {index} {index} {index} {index}', index / 100)
        def reader():
            for _ in range(100):
                result = self.snapshot(observer, 1)
                if result['samples']:
                    self.assertEqual(result['rates']['device_ms'], result['samples'][-1]['device_ms'])
        with ThreadPoolExecutor(max_workers=3) as pool:
            futures = [pool.submit(writer), pool.submit(reader), pool.submit(reader)]
            for future in futures:
                future.result(timeout=10)


if __name__ == '__main__':
    unittest.main()
