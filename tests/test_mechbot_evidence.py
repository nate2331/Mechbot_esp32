"""Pure evidence checks using preserved reports and explicitly synthetic fixtures."""
import copy
import json
import math
from pathlib import Path
import unittest
from unittest import mock

import mechbot_evidence as evidence


WHEELS = ('FL', 'FR', 'RL', 'RR')
DIRECTIONS = ('forward', 'reverse')
RESULTS = Path(__file__).resolve().parents[1] / 'test_results'
AUTO = 'maker-bench-20260905-koa59no2'
RETRY = 'maker-bench-retry-20260905-djty58cn'
DIAGNOSTIC = 'maker-encoder-diagnostic-20260905-s1s7fr_s'


def fixture(name):
    return json.loads((RESULTS / name / 'results.json').read_text(encoding='utf-8'))


def complete_report():
    """Invented values for structure validation; never a robot measurement."""
    report = {'schema': 1, 'status': 'bench_trim_verified', 'simulated': False,
              'wheel_order': list(WHEELS), 'counts_per_revolution': [2400] * 4,
              'maximum_pwm': 177, 'tolerance_fraction': 0.05, 'startup': {},
              'directional_matching': {}, 'shared_verification': [],
              'original_live_settings_restored': True}
    for direction, label in ((1, 'forward'), (-1, 'reverse')):
        report['startup'][label] = {
            wheel: {'status': 'measured', 'minimum_reliable_start_pwm': 40 + i,
                    'confirmation_starts': 3,
                    'trials': [{'pwm': 40 + i, 'started_and_sustained': True, 'rpm': 1.0}
                               for _ in range(3)]} for i, wheel in enumerate(WHEELS)}
        measurement = {'pwm': [150] * 4, 'direction': direction, 'rpm': [50.0] * 4,
                       'repeatable': True, 'passed': True, 'spread_fraction': 0.0}
        report['directional_matching'][label] = {
            'converged': True, 'pwm': [150] * 4, 'history': [measurement]}
        for fraction in (0.6, 0.8, 1.0):
            report['shared_verification'].append(dict(
                measurement, command_fraction=fraction, pwm=[round(150 * fraction)] * 4))
    report['recommended_shared_pwm'] = [150] * 4
    report['shared_candidate_pwm'] = [150] * 4
    return report


class EvidenceTest(unittest.TestCase):
    def summarize(self, report, report_id=''):
        result = evidence.summarize_report(report, report_id)
        json.dumps(result, allow_nan=False)
        self.assertEqual(set(result), {'id', 'kind', 'status', 'simulated', 'error',
            'restored', 'maximum_pwm', 'maximum_trial_pwm', 'startup',
            'directional_matching', 'trial_count', 'completed_trial_count',
            'recommended_shared_pwm', 'conditions'})
        return result

    def assert_no_recommendation(self, report):
        try:
            result = self.summarize(report)
        except ValueError:  # A malformed present structure may be rejected outright.
            return
        self.assertIsNone(result['recommended_shared_pwm'])

    def test_real_aborted_automatic_reports_keep_partial_measurements(self):
        for name in (AUTO, RETRY):
            with self.subTest(name=name):
                result = self.summarize(fixture(name), name)
                self.assertEqual((result['id'], result['kind'], result['status']),
                                 (name, 'automatic', 'aborted'))
                self.assertIs(result['simulated'], False)
                self.assertIs(result['restored'], True)
                self.assertEqual(result['maximum_pwm'], 177)
                self.assertEqual(result['error'], 'Encoder invalid transitions increased during the pulse')
                for wheel, minimum in zip(WHEELS, (119, 119, 155, 131)):
                    self.assertEqual(result['startup']['forward'][wheel], {
                        'status': 'measured', 'minimum_start_pwm': minimum, 'confirmation_starts': 3})
                    self.assertEqual(result['startup']['reverse'][wheel], {
                        'status': 'unrecorded', 'minimum_start_pwm': None, 'confirmation_starts': None})
                self.assertEqual(result['directional_matching']['forward']['status'], 'not_converged')
                self.assertEqual(result['directional_matching']['reverse']['status'], 'unrecorded')
                self.assertIsNone(result['recommended_shared_pwm'])

    def test_real_diagnostic_trials_are_not_startup_thresholds(self):
        result = self.summarize(fixture(DIAGNOSTIC), DIAGNOSTIC)
        self.assertEqual((result['kind'], result['status']), ('diagnostic', 'aborted'))
        self.assertIsNone(result['simulated'])
        self.assertIsNone(result['maximum_pwm'])
        self.assertEqual(result['maximum_trial_pwm'], 177)
        self.assertEqual((result['trial_count'], result['completed_trial_count']), (4, 3))
        self.assertTrue(result['restored'])
        self.assertIsNone(result['recommended_shared_pwm'])
        self.assertEqual(result['conditions']['original_settings'], fixture(DIAGNOSTIC)['original_settings'])
        for direction in DIRECTIONS:
            for wheel in WHEELS:
                self.assertIsNone(result['startup'][direction][wheel]['minimum_start_pwm'])

    def test_sparse_report_preserves_unknowns(self):
        result = self.summarize({})
        self.assertEqual((result['kind'], result['status']), ('unknown', 'unrecorded'))
        self.assertIsNone(result['simulated'])
        self.assertIsNone(result['restored'])
        self.assertTrue(all(value is None for value in result['conditions'].values()))
        self.assertEqual((result['trial_count'], result['completed_trial_count']), (0, 0))
        report = {'startup': {'forward': {'FL': {'status': 'measuring', 'trials': [{'pwm': 177}]}}}}
        self.assertIsNone(self.summarize(report)['startup']['forward']['FL']['minimum_start_pwm'])

    def test_trial_maximum_uses_recorded_requests_and_not_incomplete_threshold_claim(self):
        report = {'startup': {'forward': {'FL': {'status': 'measured',
            'minimum_reliable_start_pwm': 200, 'trials': [{'pwm': 24}, {'pwm': 64.5}]}}}}
        result = self.summarize(report)
        self.assertEqual(result['maximum_trial_pwm'], 64.5)
        self.assertIsNone(result['startup']['forward']['FL']['minimum_start_pwm'])
        self.assertIsNone(result['startup']['forward']['FL']['confirmation_starts'])

    def test_complete_structure_allows_copied_recommendation(self):
        report = complete_report()
        result = self.summarize(report)
        self.assertEqual(result['recommended_shared_pwm'], [150] * 4)
        result['recommended_shared_pwm'][0] = 1
        self.assertEqual(report['recommended_shared_pwm'], [150] * 4)

    def test_incomplete_simulated_and_aborted_never_recommend(self):
        for field, value in (('status', 'aborted'), ('status', 'in_progress'),
                             ('status', 'measured_no_shared_trim'), ('simulated', True),
                             ('simulated', None), ('error', 'stopped'), ('restore_error', 'failed'),
                             ('original_live_settings_restored', False)):
            with self.subTest(field=field, value=value):
                report = complete_report()
                report[field] = value
                self.assert_no_recommendation(report)
        for field in ('simulated', 'startup', 'directional_matching', 'shared_verification'):
            report = complete_report()
            del report[field]
            self.assert_no_recommendation(report)

    def test_recommendation_requires_measured_confirmed_startup(self):
        for field, value in (('status', 'measuring'), ('confirmation_starts', 2),
                             ('minimum_reliable_start_pwm', None), ('trials', [])):
            report = complete_report()
            report['startup']['reverse']['RR'][field] = value
            self.assert_no_recommendation(report)
        report = complete_report()
        report['startup']['forward']['FL']['trials'][-1]['started_and_sustained'] = False
        self.assert_no_recommendation(report)

    def test_verification_flags_and_actual_measurements_must_agree(self):
        for field, value in (('passed', False), ('passed', 'true'), ('repeatable', False),
                             ('rpm', [1, 1, 1, 2]), ('rpm', [0, 0, 0, 0]),
                             ('spread_fraction', 0.1), ('pwm', [1, 2, 3, 4])):
            with self.subTest(field=field, value=value):
                report = complete_report()
                report['shared_verification'][0][field] = value
                self.assert_no_recommendation(report)
        report = complete_report()
        report['shared_verification'][-1] = copy.deepcopy(report['shared_verification'][0])
        self.assert_no_recommendation(report)
        report = complete_report()
        report['recommended_shared_pwm'][0] = 151
        self.assert_no_recommendation(report)
        report = complete_report()
        report['directional_matching']['reverse']['history'] = []
        self.assert_no_recommendation(report)
        report = complete_report()
        report['directional_matching']['reverse']['history'][-1]['pwm'] = [100] * 4
        self.assert_no_recommendation(report)

    def test_untrusted_json_and_malformed_fields_rejected(self):
        invalid = [None, [], {'simulated': 'false'}, {'simulated': 1}, {'maximum_pwm': True},
                   {'maximum_pwm': 256}, {'maximum_pwm': math.nan}, {'startup': []},
                   {'startup': {'forward': {'FL': []}}}, {'trials': 'wrong'},
                   {'trials': [{'pwm': [0, 0, 0]}]}, {'counts_per_revolution': [1, 2, 3, 0]},
                   {'directional_matching': {'forward': {'converged': 'false'}}},
                   {'notes': [{'anything': math.inf}]}, {'original_live_settings': []},
                   {1: 'non-string key'}, {'object': object()},
                   {'startup': {'forward': []}}, {'startup': {'forward': {'FL': {'trials': {}}}}},
                   {'startup': {'forward': {'FL': {'trials': [{'pwm': True}]}}}},
                   {'startup': {'forward': {'FL': {'status': 'measuring', 'confirmation_starts': True}}}},
                   {'directional_matching': {'reverse': {'history': [None]}}},
                   {'directional_matching': {'reverse': {'history': [{'pwm': [256] * 4}]}}},
                   {'shared_verification': {}}, {'shared_verification': [{'direction': True}]},
                   {'shared_verification': [{'passed': 'true'}]},
                   {'trials': [{'observation': {'rpm': [False] * 4}}]},
                   {'trials': [{'observation': {'rpm': [0] * 3}}]},
                   {'wheel_order': [['FL'], 'FR', 'RL', 'RR']},
                   {'counts_per_revolution': [10**400] * 4}, {'battery_voltage': True}]
        for report in invalid:
            with self.subTest(report=report), self.assertRaises(ValueError):
                evidence.summarize_report(report)

    def test_comparison_retains_aborts_unknown_conditions_and_partial_deltas(self):
        result = evidence.compare_reports(fixture(AUTO), fixture(RETRY), AUTO, RETRY)
        self.assertEqual(set(result), {'left', 'right', 'conditions', 'threshold_deltas'})
        self.assertEqual((result['left']['status'], result['right']['status']), ('aborted', 'aborted'))
        self.assertEqual(result['conditions']['maximum_pwm'], {'left': 177, 'right': 177, 'status': 'same'})
        for field in ('battery_voltage', 'surface', 'load'):
            self.assertEqual(result['conditions'][field], {'left': None, 'right': None, 'status': 'unrecorded'})
        self.assertEqual(result['threshold_deltas']['forward'], dict.fromkeys(WHEELS, 0))
        self.assertEqual(result['threshold_deltas']['reverse'], dict.fromkeys(WHEELS))
        json.dumps(result, allow_nan=False)

    def test_comparison_records_changed_conditions_without_claiming_missing_ones(self):
        left, right = fixture(AUTO), fixture(RETRY)
        left['battery_voltage'], right['battery_voltage'] = 12.0, 11.5
        right['surface'] = 'wheels raised'
        right['startup']['forward']['RL']['minimum_reliable_start_pwm'] = 160
        result = evidence.compare_reports(left, right)
        self.assertEqual(result['conditions']['battery_voltage']['status'], 'different')
        self.assertEqual(result['conditions']['surface']['status'], 'unrecorded')
        self.assertEqual(result['threshold_deltas']['forward']['RL'], 5)

    def test_functions_do_not_mutate_input_or_access_files(self):
        report = fixture(AUTO)
        report['original_live_settings']['extra_metadata'] = {'nested': ['recorded']}
        original = copy.deepcopy(report)
        with mock.patch('builtins.open', side_effect=AssertionError('pure function opened a file')):
            result = self.summarize(report)
            evidence.compare_reports(report, report)
        result['conditions']['counts_per_revolution'][0] = 1
        result['conditions']['original_settings']['extra_metadata']['nested'][0] = 'changed'
        result['startup']['forward']['FL']['status'] = 'changed'
        self.assertEqual(report, original)


if __name__ == '__main__':
    unittest.main()
