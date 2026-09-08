"""Focused passive-service integration checks with fake robot transport only."""
import copy
import io
import sys
import tempfile
import threading
import types
import unittest
from pathlib import Path
from unittest.mock import Mock, patch

try:
    import serial
except ModuleNotFoundError:
    sys.modules['serial'] = types.SimpleNamespace(SerialException=OSError, Serial=None)

from mechbot_bridge import Bridge
from mechbot_http import handle_get, handle_post
import mechbot_operations as operations
from mechbot_profiles import PROFILES


class FakeSerial:
    def __init__(self):
        self.commands = []
        self.closed = False

    def write(self, payload):
        self.commands.append(payload.decode('ascii').strip())

    def flush(self):
        pass

    def close(self):
        self.closed = True


class PassiveHookIsolationTests(unittest.TestCase):
    def setUp(self):
        self.service = types.SimpleNamespace(transmit=Mock(), feed=Mock(), boundary=Mock())
        self.bridge = Bridge(operations=self.service)
        self.transport = FakeSerial()
        self.bridge.serial = self.transport
        self.bridge.telemetry.update(serial_connected=True, firmware=PROFILES['maker']['firmware'])

    def test_recording_hook_failure_cannot_skip_the_second_motor_stop_command(self):
        self.service.transmit.side_effect = RuntimeError('passive capture failed')
        self.bridge.emergency_stop()
        self.assertEqual(self.transport.commands, ['V 0 0 0', 'X'])
        self.assertTrue(self.bridge.rearm_required)

    def test_observer_failure_does_not_discard_legacy_encoder_telemetry(self):
        self.service.feed.side_effect = ValueError('passive observer rejected its state')
        self.bridge.parse_line('T 100 1 2 3 4')
        self.assertEqual(self.bridge.telemetry['encoders'], [1, 2, 3, 4])
        self.assertEqual(self.bridge.telemetry['last_line'], 'T 100 1 2 3 4')

    def test_boundary_failure_cannot_prevent_disconnect_cleanup(self):
        self.service.boundary.side_effect = RuntimeError('passive boundary failed')
        self.bridge.disconnect_serial()
        self.assertTrue(self.transport.closed)
        self.assertIsNone(self.bridge.serial)
        self.assertFalse(self.bridge.telemetry['serial_connected'])
        self.assertNotIn('firmware', self.bridge.telemetry)

    def test_passive_error_is_bounded_latched_and_snapshot_is_independent(self):
        self.service.transmit.side_effect = RuntimeError('x' * 1000)
        self.bridge.write('V 0 0 0')
        saved = self.bridge.snapshot()['operations_error']
        self.assertEqual(saved['method'], 'transmit')
        self.assertEqual(len(saved['error']), 255)
        saved['error'] = 'caller changed its copy'
        self.service.transmit.side_effect = None
        self.bridge.write('X')
        self.assertEqual(self.bridge.snapshot()['operations_error']['error'], 'x' * 255)

    def test_real_transport_failure_still_propagates(self):
        self.transport.write = Mock(side_effect=OSError('real serial failure'))
        with self.assertRaisesRegex(OSError, 'real serial failure'):
            self.bridge.write('V 0 0 0')
        self.service.transmit.assert_not_called()


class RecordingIntegrationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.directory = Path(temporary.name)

    def test_unusable_recording_directory_keeps_controls_observation_and_evidence_available(self):
        with patch.object(operations, 'CaptureStore', side_effect=PermissionError('denied ' + 'x' * 500)) as factory:
            service = operations.OperationsService(self.directory / 'recordings', self.directory / 'reports')
            bridge = Bridge(operations=service)
            bridge.serial = FakeSerial()
            for count in range(20):
                bridge.parse_line(f'T {100 + count} {count} 2 3 4')
                bridge.write('V 0 0 0')
            bridge.emergency_stop()
            self.assertEqual(bridge.telemetry['encoders'], [19, 2, 3, 4])
            self.assertEqual(bridge.serial.commands[-2:], ['V 0 0 0', 'X'])
            self.assertEqual(len(bridge.serial.commands), 22)
            self.assertIsNone(bridge.snapshot()['operations_error'])
            self.assertEqual(service.list_evidence(), {'reports': [], 'errors': []})
            captured = service.snapshot()['capture']
            self.assertEqual(captured['state'], 'error')
            self.assertIsNone(captured['id'])
            for key in ('accepted', 'written', 'dropped', 'bytes'):
                self.assertEqual(captured[key], 0)
            self.assertLessEqual(len(captured['error']), 255)
            self.assertTrue(captured['error'].startswith('recording unavailable: denied'))
            self.assertFalse(service.capture.submit('rx', 'T 999 1 2 3 4'))
            self.assertEqual(service.capture.stop(), captured)
            handler = types.SimpleNamespace(bridge=bridge, reply=Mock())
            for route in ('/api/recordings', '/api/recordings/example'):
                self.assertTrue(handle_get(handler, route))
                self.assertEqual(handler.reply.call_args.args, (400, {'error': captured['error']}))
            for route, body in (('/api/recordings/start', {}), ('/api/replay', {'id': 'example'})):
                self.assertTrue(handle_post(handler, route, body))
                self.assertEqual(handler.reply.call_args.args, (400, {'error': captured['error']}))
            self.assertTrue(handle_get(handler, '/api/operations'))
            self.assertEqual(handler.reply.call_args.args[0], 200)
            self.assertEqual(handler.reply.call_args.args[1]['capture'], captured)
            self.assertTrue(handle_get(handler, '/api/evidence'))
            self.assertEqual(handler.reply.call_args.args, (200, {'reports': [], 'errors': []}))
            with self.assertRaisesRegex(RuntimeError, 'recording unavailable:'):
                service.capture.start({})
            factory.assert_called_once()  # Passive traffic and requests never retry filesystem setup.

    def test_startup_fallback_catches_path_rejection_but_does_not_hide_programming_errors(self):
        with patch.object(operations, 'CaptureStore', side_effect=ValueError('linked recording directory')):
            service = operations.OperationsService(self.directory / 'recordings', self.directory / 'reports')
        self.assertIn('linked recording directory', service.capture.snapshot()['error'])
        with patch.object(operations, 'CaptureStore', side_effect=RuntimeError('unexpected constructor bug')):
            with self.assertRaisesRegex(RuntimeError, 'unexpected constructor bug'):
                operations.OperationsService(self.directory / 'recordings', self.directory / 'reports')

    def test_prepared_activation_has_no_filesystem_calls_on_the_caller_thread(self):
        store = operations.CaptureStore(self.directory / 'recordings')
        preparation = store.prepare_start()
        caller = threading.get_ident()
        safe_path = store._safe_path
        def checked_path(*args, **kwargs):
            self.assertNotEqual(threading.get_ident(), caller, 'prepared start touched disk on activation thread')
            return safe_path(*args, **kwargs)
        try:
            with patch.object(store, '_safe_path', side_effect=checked_path), patch.object(store, 'prepare_start', side_effect=AssertionError('prepared twice')):
                store.start({'simulated': True}, preparation=preparation)
                self.assertEqual(store.stop(timeout=2)['state'], 'completed')
        finally:
            store.stop(timeout=2)

    def test_preparation_validates_owner_options_id_and_single_use(self):
        store = operations.CaptureStore(self.directory / 'first')
        other = operations.CaptureStore(self.directory / 'second')
        preparation = store.prepare_start()
        with self.assertRaises(ValueError):
            other.start({}, preparation=preparation)
        store.queue_capacity += 1
        with self.assertRaises(ValueError):
            store.start({}, preparation=preparation)
        store.queue_capacity -= 1
        recording_id = preparation.recording_id
        preparation.recording_id = '../outside'
        with self.assertRaises(ValueError):
            store.start({}, preparation=preparation)
        preparation.recording_id = recording_id
        try:
            store.start({}, preparation=preparation)
            self.assertEqual(store.stop(timeout=2)['state'], 'completed')
            with self.assertRaises(ValueError):
                store.start({}, preparation=preparation)
        finally:
            store.stop(timeout=2)
            other.stop(timeout=2)

    def test_slow_capture_preparation_does_not_block_live_observation_or_commands(self):
        service = operations.OperationsService(self.directory / 'recordings', self.directory / 'reports')
        bridge = Bridge(operations=service)
        bridge.serial = FakeSerial()
        entered, release, observed = threading.Event(), threading.Event(), threading.Event()
        prepare = service.capture.prepare_start
        def slow_prepare():
            entered.set()
            if not release.wait(2):
                raise RuntimeError('test preparation not released')
            return prepare()
        service.capture.prepare_start = slow_prepare
        errors = []
        def capture():
            try:
                service.start_capture({'simulated': True}, {})
            except Exception as error:
                errors.append(error)
        def observe():
            try:
                bridge.parse_line('T 100 1 2 3 4')
                bridge.write('V 0 0 0')
            except Exception as error:
                errors.append(error)
            finally:
                observed.set()
        starter, feeder = threading.Thread(target=capture), threading.Thread(target=observe)
        try:
            starter.start()
            self.assertTrue(entered.wait(1))
            feeder.start()
            self.assertTrue(observed.wait(0.5), 'live feed waited for recording preparation IO')
            self.assertEqual(bridge.telemetry['encoders'], [1, 2, 3, 4])
            self.assertEqual(bridge.serial.commands, ['V 0 0 0'])
        finally:
            release.set()
            starter.join(2)
            if feeder.ident is not None:
                feeder.join(2)
            service.capture.stop(timeout=2)
        self.assertEqual(errors, [])

    def test_blocked_recording_disk_write_does_not_block_bridge_command_sender(self):
        service = operations.OperationsService(self.directory / 'recordings', self.directory / 'reports')
        entered, release, sent = threading.Event(), threading.Event(), threading.Event()
        open_part = service.capture._open_part

        class SlowFile:
            def __init__(self, file):
                self.file = file
            def __enter__(self):
                return self
            def __exit__(self, *args):
                return self.file.__exit__(*args)
            def write(self, value):
                entered.set()
                if not release.wait(2):
                    raise OSError('test disk remained blocked')
                return self.file.write(value)
            def flush(self):
                return self.file.flush()
            def fileno(self):
                return self.file.fileno()

        service.capture._open_part = lambda path: SlowFile(open_part(path))
        bridge = Bridge(operations=service)
        bridge.serial = FakeSerial()
        errors = []
        def send():
            try:
                for _ in range(20):
                    bridge.write('V 0 0 0')
            except Exception as error:
                errors.append(error)
            finally:
                sent.set()
        worker = None
        try:
            service.start_capture({'simulated': True}, {'label': 'blocked disk test'})
            self.assertTrue(entered.wait(1), 'writer never reached fake disk')
            worker = threading.Thread(target=send)
            worker.start()
            self.assertTrue(sent.wait(0.5), 'command sender waited for recording disk IO')
            self.assertEqual(errors, [])
            self.assertEqual(len(bridge.serial.commands), 20)
        finally:
            release.set()
            if worker:
                worker.join(2)
            service.capture.stop(timeout=2)

    def test_recording_start_cannot_miss_a_board_transition_between_metadata_and_activation(self):
        entered, release = threading.Event(), threading.Event()

        class Capture:
            def __init__(self, directory):
                self.active = False
                self.metadata = None
                self.events = []
            def snapshot(self):
                return {'state': 'active' if self.active else 'idle'}
            def prepare_start(self):
                return object()
            def start(self, metadata, preparation=None):
                entered.set()
                if not release.wait(2):
                    raise RuntimeError('test capture start was not released')
                self.metadata = copy.deepcopy(metadata)
                self.active = True
                return self.snapshot()
            def submit(self, direction, line):
                if self.active:
                    self.events.append((direction, line))
                return self.active

        with patch.object(operations, 'CaptureStore', Capture):
            service = operations.OperationsService(self.directory / 'recordings', self.directory / 'reports')
        maker = PROFILES['maker']['firmware']
        changed = PROFILES['s3']['firmware']
        service.feed('READY ' + maker, 1)
        errors = []
        def start():
            try:
                service.start_capture({'simulated': True}, {})
            except Exception as error:
                errors.append(error)
        start_thread = threading.Thread(target=start)
        feed_thread = threading.Thread(target=lambda: service.feed('READY ' + changed, 2))
        try:
            start_thread.start()
            self.assertTrue(entered.wait(1))
            feed_thread.start()
            # An implementation may serialize the boundary after activation. The
            # test accepts either correct metadata or a recorded identity event.
            feed_thread.join(0.1)
        finally:
            release.set()
            start_thread.join(2)
            feed_thread.join(2)
        self.assertFalse(start_thread.is_alive() or feed_thread.is_alive())
        self.assertEqual(errors, [])
        metadata_firmware = service.capture.metadata['profile']['firmware']
        recorded_change = ('rx', 'READY ' + changed) in service.capture.events
        self.assertTrue(metadata_firmware == changed or recorded_change,
                        'capture retained old board metadata but omitted the READY that changed the board')


if __name__ == '__main__':
    unittest.main()
