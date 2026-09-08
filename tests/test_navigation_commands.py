import unittest
from test_mechbot_bridge import Bridge, FakeSerial
from mechbot_bridge import BUTTON_DEADMAN


class NavigationCommandsTest(unittest.TestCase):
    def setUp(self):
        self.bridge = Bridge()
        self.bridge.serial = FakeSerial()
        self.bridge.telemetry.update(serial_connected=True,
            firmware='ESP32_MAKER_MECANUM_RVC_V1')
        self.bridge._restore_pending = False

    def test_stopped_commands_are_exact_and_not_claimed_acknowledged(self):
        for action, command in [('robot', 'F 0'), ('field', 'F 1'), ('zero', 'Z'),
                                ('revoke', 'IMU REVOKE'), ('accept', 'IMU ACCEPT')]:
            self.bridge.serial.commands.clear()
            result = self.bridge.navigation_command(action, 'HEADING_MEASURED')
            self.assertEqual(self.bridge.serial.commands, ['V 0 0 0', 'X', command])
            self.assertFalse(result['acknowledged'])
            self.assertTrue(self.bridge.rearm_required)

    def test_missing_measurement_and_injected_commands_do_not_write(self):
        for action, confirmation in [('accept', None), ('F 1\nV 1 0 0', None), ([], None)]:
            with self.assertRaises(ValueError):
                self.bridge.navigation_command(action, confirmation)
        self.assertEqual(self.bridge.serial.commands, [])

    def test_held_deadman_or_exclusive_work_blocks_mode_changes(self):
        for target, key, value in [(self.bridge.buttons, BUTTON_DEADMAN, 1),
                (self.bridge.telemetry, 'deadman', True),
                (self.bridge.calibration, 'active', True),
                (self.bridge.tuning_session, 'active', True),
                (self.bridge.firmware_job, 'state', 'running')]:
            previous = target[key] if not isinstance(target, dict) else target.get(key)
            target[key] = value
            with self.assertRaises(RuntimeError):
                self.bridge.navigation_command('field')
            target[key] = previous
        self.assertEqual(self.bridge.serial.commands, [])

    def test_diagnostic_or_spi_identity_cannot_authorize_navigation(self):
        for identity in [None, 'MAKER_IMU_RVC_CONTINUOUS_V1', 'ESP32_MAKER_MECANUM_IMU_V1']:
            self.bridge.telemetry['firmware'] = identity
            with self.assertRaises(RuntimeError):
                self.bridge.navigation_command('robot')
        self.assertEqual(self.bridge.serial.commands, [])

    def test_failed_stop_prevents_reference_command(self):
        def fail(data):
            raise OSError('disconnected during stop')
        self.bridge.serial.write = fail
        with self.assertRaises(OSError):
            self.bridge.navigation_command('field')
        self.assertTrue(self.bridge.rearm_required)
        self.assertEqual(self.bridge.serial.commands, [])


if __name__ == '__main__':
    unittest.main()
