"""Service integration tests; capture transport is an in-memory test double."""
import copy
import json
import math
from pathlib import Path
import tempfile
import types
import unittest
from unittest import mock

import mechbot_operations as ops
from mechbot_profiles import MAKER_HELP_IDENTITY, PROFILES, profile_for_firmware


GEOMETRY = {'wheel_diameter_m': 1 / math.pi, 'wheelbase_m': 0.4, 'track_width_m': 0.2}
MAKER = PROFILES['maker']['firmware']
S3 = PROFILES['s3']['firmware']
RECORDING_ID = 'a' * 32


class MemoryCapture:
    def __init__(self, directory):
        self.directory = Path(directory)
        self.submissions = []
        self.metadata = None
        self.bundle = None
    def submit(self, direction, line):
        self.submissions.append((direction, line))
        return True
    def prepare_start(self):
        return object()
    def start(self, metadata, preparation=None):
        self.metadata = copy.deepcopy(metadata)
        return self.snapshot()
    def snapshot(self):
        return {'id': RECORDING_ID if self.metadata is not None else None,
                'state': 'active' if self.metadata is not None else 'idle',
                'accepted': len(self.submissions), 'written': 0, 'dropped': 0,
                'bytes': 0, 'error': None}
    def read_recording(self, recording_id):
        if recording_id != RECORDING_ID or self.bundle is None:
            raise FileNotFoundError(recording_id)
        return copy.deepcopy(self.bundle)


class OperationsTest(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)
        patcher = mock.patch.object(ops, 'CaptureStore', MemoryCapture)
        patcher.start()
        self.addCleanup(patcher.stop)
        clock = mock.patch.object(ops, 'time', types.SimpleNamespace(monotonic=lambda: 10.0))
        clock.start()
        self.addCleanup(clock.stop)
        self.service = self.make_service()

    def make_service(self, report_dir=None):
        return ops.OperationsService(self.directory / 'recordings',
            report_dir or self.directory / 'reports', self.directory / 'geometry.json')

    def identify(self, firmware=MAKER):
        self.service.feed('READY ' + firmware, 1)

    def bundle(self, events, geometry=GEOMETRY):
        self.service.capture.bundle = {
            'id': RECORDING_ID, 'metadata': {'profile': profile_for_firmware(MAKER),
                'geometry': copy.deepcopy(geometry), 'simulated': None},
            'events': [{'type': 'event', 'offset_s': offset, 'direction': direction, 'line': line}
                       for offset, direction, line in events],
            'state': 'completed', 'dropped': 0, 'error': None}

    def test_capture_metadata_preserves_unknown_and_explicit_conditions(self):
        self.identify()
        self.service.configure_geometry(GEOMETRY)
        data = {'label': '  bench A ', 'surface': '', 'load': ' raised wheels ', 'battery_voltage': 12.4}
        self.service.start_capture({'serial_connected': True}, data)
        metadata = self.service.capture.metadata
        self.assertEqual(metadata['label'], 'bench A')
        self.assertEqual(metadata['conditions'], {'surface': None, 'load': 'raised wheels', 'battery_voltage': 12.4})
        self.assertIsNone(metadata['simulated'])
        self.assertTrue(metadata['serial_connected'])
        self.assertEqual(metadata['profile']['id'], 'maker')
        self.assertEqual(metadata['geometry'], GEOMETRY)
        self.assertIn('started_utc', metadata)
        self.assertEqual(data['label'], '  bench A ')
        for simulated in (False, True):
            self.service.start_capture({'simulated': simulated}, {})
            self.assertIs(self.service.capture.metadata['simulated'], simulated)
        for settings in (None, [], {'label': 'x' * 81}, {'surface': 1}, {'load': 'x' * 121},
                         {'battery_voltage': True}, {'battery_voltage': math.nan},
                         {'battery_voltage': -1}, {'battery_voltage': 60.01}):
            with self.subTest(settings=settings), self.assertRaises(ValueError):
                self.service.start_capture({}, settings)

    def test_persisted_geometry_restores_only_matching_calibrated_board(self):
        self.identify()
        self.service.configure_geometry(GEOMETRY)
        saved = json.loads((self.directory / 'geometry.json').read_text())
        self.assertEqual(saved['firmware'], MAKER)
        self.assertEqual(saved['cpr'], PROFILES['maker']['counts_per_revolution'])
        restored = self.make_service()
        self.assertIsNone(restored.snapshot(1)['observed']['geometry'])
        restored.feed('READY ' + S3, 1)
        self.assertIsNone(restored.snapshot(1)['observed']['geometry'])
        restored.boundary('identify again', 2)
        restored.feed(MAKER_HELP_IDENTITY, 3)
        self.assertEqual(restored.snapshot(3)['observed']['geometry'], GEOMETRY)
        restored.boundary('lost serial', 4)
        self.assertIsNone(restored.snapshot(4)['observed']['geometry'])
        restored.feed('READY ' + MAKER, 5)
        self.assertTrue(restored.snapshot(5)['observed']['pose']['configured'])

    def test_bad_persisted_configuration_reports_error_without_poisoning_feed(self):
        for firmware, cpr in ((S3, {}), (MAKER, {}), ('unknown', {})):
            (self.directory / 'geometry.json').write_text(json.dumps({
                'firmware': firmware, 'cpr': cpr, 'geometry': GEOMETRY}))
            service = self.make_service()
            self.assertTrue(service.config_error)
            service.feed('READY ' + firmware, 1)
            self.assertIsNone(service.snapshot(1)['observed']['geometry'])
        (self.directory / 'geometry.json').write_text(' ' * 8193)
        self.assertIn('limit', self.make_service().config_error)

    def test_geometry_write_failure_keeps_prior_configuration_and_no_temp_files(self):
        self.identify()
        self.service.configure_geometry(GEOMETRY)
        before = self.service.snapshot(10)
        saved = (self.directory / 'geometry.json').read_bytes()
        with mock.patch.object(ops.os, 'replace', side_effect=OSError('disk failure')):
            with self.assertRaisesRegex(OSError, 'disk failure'):
                self.service.configure_geometry({**GEOMETRY, 'wheelbase_m': 0.8})
        self.assertEqual(self.service.snapshot(10), before)
        self.assertEqual((self.directory / 'geometry.json').read_bytes(), saved)
        self.assertEqual(list(self.directory.glob('.geometry-*')), [])

    def test_identity_provenance_change_during_write_is_still_same_board(self):
        self.identify()
        replace = ops.os.replace
        def change_source(source, destination):
            replace(source, destination)
            self.service.boundary('reidentify', 2)
            self.service.feed(MAKER_HELP_IDENTITY, 3)
        with mock.patch.object(ops.os, 'replace', side_effect=change_source):
            configured = self.service.configure_geometry(GEOMETRY)
        self.assertEqual(configured['profile']['identity_source'], 'maker-help')
        self.assertEqual(configured['geometry'], GEOMETRY)

    def test_reset_pose_keeps_geometry_but_requires_new_rate_baseline(self):
        self.identify()
        with self.assertRaises(ValueError):
            self.service.reset_pose()
        self.service.configure_geometry(GEOMETRY)
        self.service.feed('T 0 0 0 0 0', 2)
        self.service.feed('T 1000 100 100 100 100', 3)
        self.assertGreater(self.service.snapshot(3)['observed']['pose']['distance_m'], 0)
        reset = self.service.reset_pose()
        self.assertEqual(reset['geometry'], GEOMETRY)
        self.assertEqual(reset['pose']['distance_m'], 0)
        self.service.feed('T 1100 110 110 110 110', 4)
        self.assertEqual(self.service.snapshot(4)['observed']['rates']['reason'], 'initial')

    def test_replay_is_isolated_and_restores_geometry_after_help_identity(self):
        self.identify()
        self.service.configure_geometry(GEOMETRY)
        before = self.service.snapshot(10)
        self.bundle([(0, 'boundary', 'recorded reconnect'), (0.1, 'rx', MAKER_HELP_IDENTITY),
                     (0.2, 'rx', 'T 0 0 0 0 0'), (0.3, 'tx', 'V 1 0 0'),
                     (1.2, 'rx', 'T 1000 100 100 100 100')])
        with mock.patch.object(self.service.capture, 'submit', side_effect=AssertionError('replay wrote live data')):
            replay = self.service.replay(RECORDING_ID)
        self.assertEqual(self.service.snapshot(10), before)
        self.assertEqual((replay['mode'], replay['duration_s'], replay['position_s']), ('replay', 1.2, 1.2))
        self.assertEqual(replay['observed']['profile']['identity_source'], 'maker-help')
        self.assertEqual(replay['observed']['geometry'], GEOMETRY)
        self.assertGreater(replay['observed']['pose']['distance_m'], 0)
        self.assertEqual(replay['tx'][0]['line'], 'V 1 0 0')
        self.assertIsNone(replay['metadata']['simulated'])
        partial = self.service.replay(RECORDING_ID, until_s=0.25)
        self.assertEqual(partial['observed']['rates']['reason'], 'initial')
        self.assertEqual(partial['tx'], [])
        self.assertEqual(self.service.replay(RECORDING_ID, 999)['position_s'], 1.2)

    def test_replay_limits_status_labels_and_corrupt_calibration(self):
        self.bundle([(index / 10, 'tx', f'CFG GET {index}') for index in range(25)], geometry=None)
        self.service.capture.bundle.update(state='incomplete', dropped=2)
        result = self.service.replay(RECORDING_ID)
        self.assertEqual((result['state'], result['dropped']), ('incomplete', 2))
        self.assertEqual(len(result['tx']), 20)
        self.assertEqual(result['tx'][0]['line'], 'CFG GET 5')
        for position in (-1, True, math.nan, math.inf):
            with self.assertRaises(ValueError):
                self.service.replay(RECORDING_ID, position)
        self.bundle([])
        self.service.capture.bundle['metadata']['profile']['counts_per_revolution']['FL'] = 1
        with self.assertRaisesRegex(ValueError, 'calibration'):
            self.service.replay(RECORDING_ID)
        with self.assertRaises(FileNotFoundError):
            self.service.replay('missing')

    def test_catalogue_summarizes_real_reports_and_exposes_file_failures(self):
        reports = self.directory / 'reports'
        names = ('maker-bench-20260905-koa59no2', 'maker-bench-retry-20260905-djty58cn',
                 'maker-encoder-diagnostic-20260905-s1s7fr_s')
        for name in names:
            source = Path(ops.__file__).parent / 'test_results' / name / 'results.json'
            destination = reports / source.parent.name
            destination.mkdir(parents=True)
            (destination / 'results.json').write_bytes(source.read_bytes())
        for name, raw in (('bad-json', b'{'), ('oversize', b' ' * (ops.REPORT_LIMIT + 1))):
            (reports / name).mkdir()
            (reports / name / 'results.json').write_bytes(raw)
        result = self.service.list_evidence()
        self.assertEqual(len(result['reports']), 3)
        self.assertTrue(all(report['status'] == 'aborted' for report in result['reports']))
        self.assertEqual({error['id'] for error in result['errors']}, {'bad-json', 'oversize'})
        diagnostic = next(report for report in result['reports'] if report['kind'] == 'diagnostic')
        self.assertIsNone(diagnostic['simulated'])
        automatic = sorted(report['id'] for report in result['reports'] if report['kind'] == 'automatic')
        compared = self.service.compare_evidence(*automatic)
        self.assertEqual(compared['conditions']['battery_voltage']['status'], 'unrecorded')

    def test_report_path_bounds_and_empty_catalogue(self):
        self.assertEqual(self.service.list_evidence(), {'reports': [], 'errors': []})
        for report_id in ('', '..', '../outside', 'x/y', 'x\\y', 'a' * 121, None):
            with self.subTest(report_id=report_id), self.assertRaises(ValueError):
                self.service.compare_evidence(report_id, 'valid')
        with self.assertRaises(FileNotFoundError):
            self.service.compare_evidence('missing', 'also-missing')

    def test_active_capture_keeps_geometry_and_pose_origin_consistent(self):
        self.identify()
        self.service.configure_geometry(GEOMETRY)
        self.service.start_capture({'simulated': True}, {'label': 'synthetic fixed calibration'})
        before = self.service.snapshot(10)['observed']
        with self.assertRaises(RuntimeError):
            self.service.configure_geometry({**GEOMETRY, 'wheelbase_m': .8})
        with self.assertRaises(RuntimeError):
            self.service.reset_pose()
        self.assertEqual(self.service.snapshot(10)['observed'], before)
        self.assertEqual(self.service.capture.metadata['pose_origin'], 'recording-relative')

    def test_capture_before_identification_preserves_calibration_for_matching_reconnect(self):
        self.identify()
        self.service.configure_geometry(GEOMETRY)
        self.service.boundary('disconnected', 2)
        self.service.start_capture({'simulated': True}, {})
        metadata = copy.deepcopy(self.service.capture.metadata)
        self.assertEqual(metadata['profile']['id'], 'unknown')
        self.assertIsNone(metadata['geometry'])
        events = [(0.1, 'rx', 'READY ' + S3), (0.2, 'boundary', 'changed controller'),
                  (0.3, 'rx', MAKER_HELP_IDENTITY), (0.4, 'rx', 'T 0 0 0 0 0'),
                  (1.4, 'rx', 'T 1000 100 100 100 100')]
        for offset, direction, line in events:
            if direction == 'boundary':
                self.service.boundary(line, 10 + offset)
            else:
                self.service.feed(line, 10 + offset)
        live = self.service.snapshot(11.4)['observed']
        self.assertGreater(live['pose']['distance_m'], 0)
        self.bundle(events, geometry=None)
        self.service.capture.bundle['metadata'] = metadata
        wrong_board = self.service.replay(RECORDING_ID, 0.1)['observed']
        self.assertEqual(wrong_board['profile']['id'], 's3')
        self.assertIsNone(wrong_board['geometry'])
        replay = self.service.replay(RECORDING_ID)['observed']
        self.assertEqual(replay['geometry'], GEOMETRY)
        self.assertAlmostEqual(replay['pose']['distance_m'], live['pose']['distance_m'])
        self.assertEqual(self.service.snapshot(11.4)['observed'], live)

    def test_recorded_persisted_calibration_requires_known_matching_scale_and_dimensions(self):
        calibration = {'firmware': MAKER, 'cpr': PROFILES['maker']['counts_per_revolution'],
                       'geometry': GEOMETRY}
        invalid = [[], {**calibration, 'firmware': 'unknown'},
                   {**calibration, 'firmware': S3, 'cpr': {}},
                   {**calibration, 'cpr': {}},
                   {**calibration, 'geometry': {**GEOMETRY, 'wheelbase_m': math.nan}}]
        for value in invalid:
            with self.subTest(calibration=value):
                self.bundle([], geometry=None)
                self.service.capture.bundle['metadata']['geometry_calibration'] = value
                with self.assertRaises(ValueError):
                    self.service.replay(RECORDING_ID)


if __name__ == '__main__':
    unittest.main()
