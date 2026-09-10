import copy
import unittest

from mechbot_rvc_trial_guard import RvcTrialGuard, TrialFault


def sample(now=10, device_ms=1000):
    return dict(serial_connected=True, maintenance=False, deadman=False,
                gamepad_connected=False, calibration_active=False,
                board={'firmware': 'ESP32_MAKER_MECANUM_RVC_V2'},
                updated=now, imu_updated=now,
                imu=f'IR2 {device_ms} 0 READY 0 0 0 0 0 1000 0 0 0 0 0',
                navigation=f'N {device_ms} 0 0 0 0 0 0 0',
                wheel_diagnostics={w: dict(updated=now, pwm=0, invalid_transitions=0)
                                   for w in ('FL', 'FR', 'RL', 'RR')})


class TrialGuardTests(unittest.TestCase):
    def test_missing_imu_timestamp_reports_stale_stream(self):
        s = sample()
        s['imu_updated'] = None
        with self.assertRaisesRegex(TrialFault, 'Stale stream'):
            RvcTrialGuard().check(s, 10)
    def test_heading_trial_requires_enabled_fresh_accepted_state(self):
        s = sample()
        s['imu'] = 'IR2 1000 0 READY 0 0 0 0 0 1000 1 0 0 0 0'
        s['navigation'] = 'N 1000 0 0 0 0 1 0 1'
        RvcTrialGuard(heading_enabled=True, heading_accepted=True).check(s, 10)
        for key, value in [('navigation', 'N 1000 0 0 0 0 0 0 1'),
                           ('navigation', 'N 1 0 0 0 0 1 0 1'),
                           ('imu', sample()['imu'])]:
            bad = dict(s)
            bad[key] = value
            with self.assertRaises(TrialFault):
                RvcTrialGuard(heading_enabled=True, heading_accepted=True).check(bad, 10)

    def test_explicit_encoder_waiver_retains_other_checks(self):
        guard = RvcTrialGuard(require_clean_encoders=False)
        guard.check(sample(), 10)
        s = sample(10.2, 1200)
        s['wheel_diagnostics']['FR']['invalid_transitions'] = 1
        guard.check(s, 10.2)
        s['serial_connected'] = False
        with self.assertRaises(TrialFault):
            guard.check(s, 10.2)

    def test_progressing_clean_stream(self):
        guard = RvcTrialGuard()
        for i in range(10):
            guard.check(sample(10+i*.2, 1000+i*200), 10+i*.2, stopped=True)

    def test_frozen_device_despite_fresh_host_timestamps(self):
        guard = RvcTrialGuard()
        guard.check(sample(), 10)
        with self.assertRaises(TrialFault):
            guard.check(sample(10.6), 10.6)

    def test_reset_and_encoder_changes_latch(self):
        for mutation in ('reset', 'encoder'):
            guard = RvcTrialGuard()
            guard.check(sample(), 10)
            bad = sample(10.2, 1 if mutation == 'reset' else 1200)
            if mutation == 'encoder':
                bad['wheel_diagnostics']['RL']['invalid_transitions'] = 1
            with self.assertRaises(TrialFault):
                guard.check(bad, 10.2)
            with self.assertRaises(TrialFault):
                guard.check(sample(10.4, 1400), 10.4)

    def test_fault_inputs(self):
        base = sample()
        bad_samples = []
        for key, value in [('serial_connected', False), ('deadman', True),
                           ('imu_updated', 8), ('navigation', 'N 1000 0 0 0 0 0 1 1'),
                           ('imu', 'IR2 1000 0 READY 0 0 0 0 0 1000 0 1 0 0 0')]:
            bad = copy.deepcopy(base)
            bad[key] = value
            bad_samples.append(bad)
        bad = copy.deepcopy(base)
        bad['wheel_diagnostics']['FR']['pwm'] = 1
        bad_samples.append(bad)
        for bad in bad_samples:
            with self.subTest(bad=bad), self.assertRaises(TrialFault):
                RvcTrialGuard().check(bad, 10, stopped=True)


if __name__ == '__main__':
    unittest.main()
