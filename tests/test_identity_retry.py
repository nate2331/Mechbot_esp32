"""Lost startup-help recovery with fake transport; no motor or USB actions."""
import types
import unittest
from unittest.mock import Mock, patch

from mechbot_bridge import Bridge
from mechbot_profiles import MAKER_HELP_IDENTITY, PROFILES


class IdentityRetryTests(unittest.TestCase):
    def setUp(self):
        passive = types.SimpleNamespace(transmit=lambda *_: None, feed=lambda *_: None,
                                        boundary=lambda *_: None)
        self.bridge = Bridge(operations=passive)
        self.bridge.serial = Mock(in_waiting=0)
        self.bridge.write = Mock()
        self.bridge.connect_gamepad = Mock()

    def test_lost_initial_reply_retries_at_two_seconds_then_stops_on_identity(self):
        self.bridge.poll_identity(10)
        self.bridge.poll_identity(10.1)
        self.bridge.poll_identity(11.999)
        self.assertEqual(self.bridge.write.call_count, 1)
        self.bridge.poll_identity(12)
        self.assertEqual(self.bridge.write.call_count, 2)
        self.assertTrue(all(call.args == ('?',) for call in self.bridge.write.call_args_list))
        self.bridge.parse_line(MAKER_HELP_IDENTITY)
        self.bridge.poll_identity(100)
        self.assertEqual(self.bridge.board_profile()['id'], 'maker')
        self.assertEqual(self.bridge.write.call_count, 2)

    def test_busy_or_disconnected_bridge_never_queries(self):
        changes = [lambda: setattr(self.bridge, 'serial', None),
                   lambda: setattr(self.bridge, 'maintenance', True),
                   lambda: self.bridge.calibration.update(active=True),
                   lambda: self.bridge.firmware_job.update(state='starting'),
                   lambda: self.bridge.firmware_job.update(state='running')]
        for change in changes:
            with self.subTest(change=change):
                self.setUp()
                change()
                self.bridge.poll_identity(100)
                self.bridge.write.assert_not_called()

    def test_explicit_unknown_firmware_never_uses_help_fallback(self):
        self.bridge.parse_line('READY UNRECOGNIZED_FIRMWARE')
        self.bridge.poll_identity(100)
        self.bridge.write.assert_not_called()
        self.assertEqual(self.bridge.board_profile()['id'], 'unknown')

    def test_recognized_ready_never_queries(self):
        for profile in PROFILES.values():
            with self.subTest(profile=profile['id']):
                self.bridge.parse_line('READY ' + profile['firmware'])
                self.bridge.poll_identity(100)
                self.bridge.write.assert_not_called()

    def test_connect_initial_query_gets_full_interval_before_retry(self):
        self.bridge.serial = None
        self.bridge.find_port = Mock(return_value='/fake/usb')
        with patch('mechbot_bridge.serial.Serial', return_value=Mock(in_waiting=0)), \
                patch('mechbot_bridge.time.sleep'), patch('mechbot_bridge.time.monotonic', return_value=10):
            self.bridge.connect_serial()
        self.bridge.write.assert_called_once_with('?')
        self.bridge.poll_identity(11.999)
        self.bridge.write.assert_called_once_with('?')
        self.bridge.poll_identity(12)
        self.assertEqual(self.bridge.write.call_count, 2)

    def test_disconnect_resets_retry_deadline_and_identity(self):
        self.bridge.poll_identity(100)
        self.bridge.disconnect_serial()
        self.assertEqual(self.bridge._next_identity_query, 0)
        self.assertEqual(self.bridge.board_profile()['id'], 'unknown')

    def test_transport_loop_calls_recovery_poll(self):
        self.bridge.poll_identity = Mock()
        with patch('mechbot_bridge.time.monotonic', return_value=5), \
                patch('mechbot_bridge.time.sleep', side_effect=lambda *_: setattr(self.bridge, 'running', False)):
            self.bridge.loop()
        self.bridge.poll_identity.assert_called_once_with(5)


if __name__ == '__main__':
    unittest.main()
