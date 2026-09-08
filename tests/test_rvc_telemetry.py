"""Passive RVC protocol and control-boundary regression checks; no hardware IO."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock

from mechbot_bridge import Bridge
from mechbot_observer import TelemetryObserver
from mechbot_operations import OperationsService
from mechbot_profiles import MAKER_HELP_IDENTITY, MAKER_RVC_HELP_IDENTITY
from mechbot_firmware_update import build_plan, perform_update, verified_boot
from mechbot_telemetry import parse_event, RVC_BANNERS, RVC_HELP_READY

BANNER = next(iter(RVC_BANNERS))
RUN = ('RUN boot_ms=84010 state=READY bytes=159393 frames=8385 new=100 window_ms=1000 '
       'age_ms=4 first_frame_boot_ms=328 first_ready_boot_ms=368 acquisitions=1 pauses=0 '
       'max_gap_ms=11 ready_ms=83642 longest_ready_ms=83642')
COUNTS = ('COUNTS bad_checksum=0 discontinuities=0 repeats=0 '
          'post_first_ready_bad=0 post_first_ready_discontinuities=0')
UART = 'UART_EVENTS total/post_first_ready fifo,buffer,frame,parity,break= 0/0 0/0 0/0 0/0 0/0'
VALUE = 'VALUE ypr_deg=-0.01,-0.75,0.40 accel_mg=-8,-12,970 changes_ypr=417 changes_accel=6624'


class RvcTests(unittest.TestCase):
    def test_integrated_rvc_units_acceptance_and_malformed_reports(self):
        line='IR1 1234 2 READY 90.00 -0.75 0.40 -8 -12 974 1 2 3 4'
        parsed=parse_event(line,1)
        self.assertEqual(parsed['transport'],'uart-rvc')
        self.assertTrue(parsed['valid'] and parsed['control_ready'])
        self.assertAlmostEqual(parsed['yaw_rad'],1.5707963267948966)
        self.assertEqual(parsed['raw_acceleration_mg'],[-8,-12,974])
        for key in ('quaternion','gyro','acceleration','status'):
            self.assertIsNone(parsed[key])
        observer=TelemetryObserver()
        observer.feed(line,1)
        self.assertFalse(observer.snapshot(3)['imu']['control_ready'])
        for text in (line.replace(' READY ', ' STALE '),line.replace('1234 2','1234 501')):
            self.assertFalse(parse_event(text,1)['valid'])
        self.assertFalse(parse_event(line.replace('974 1','974 0'),1)['control_ready'])
        for text in (line+' extra',line.replace('90.00','nan'),line.replace('974 1','974 2'),
                     line.replace('-8','-32769'),line.replace('1234 2','-1 2')):
            self.assertIsNone(parse_event(text,1))

    def test_integrated_rvc_help_and_ready_select_only_rvc_build(self):
        bridge=Bridge(operations=Mock())
        bridge.serial=Mock()
        bridge.telemetry.update(serial_connected=True,serial_port='/fake/usb')
        bridge.parse_line(MAKER_RVC_HELP_IDENTITY)
        plan=build_plan(bridge.snapshot(),'maker')
        self.assertEqual(plan['expected'],'ESP32_MAKER_MECANUM_RVC_V1')
        self.assertEqual(plan['build_properties'],['compiler.cpp.extra_flags=-DMAKER_IMU_RVC=1'])
        self.assertFalse(verified_boot(bridge.snapshot(),plan,0))
        bridge.parse_line('READY ESP32_MAKER_MECANUM_RVC_V1')
        self.assertTrue(verified_boot(bridge.snapshot(),plan,0))
        bridge.parse_line('IR1 1234 2 READY 90 0 0 -8 -12 974 0 0 0 0')
        self.assertTrue(bridge.telemetry['imu_valid'])
        bridge.parse_line('IR1 malformed')
        self.assertFalse(bridge.telemetry['imu_valid'])
        bridge.parse_line('READY unexpected')
        bridge.parse_line(MAKER_RVC_HELP_IDENTITY)
        with self.assertRaisesRegex(RuntimeError,'recognized'):
            build_plan(bridge.snapshot())

    def test_rvc_updater_uploads_exact_variant_output_and_checks_fresh_identity(self):
        status={'serial_connected':True,'serial_port':'/fake/usb',
                'firmware':'ESP32_MAKER_MECANUM_RVC_V1','firmware_received':0}
        calls=[]
        def run(args,check):
            calls.append(args)
            if args[1]=='upload': status['firmware_received']=101
        plan=perform_update('maker',api_call=lambda *_:dict(status),run=run,sleep=lambda *_:None,clock=lambda:100)
        self.assertIn('compiler.cpp.extra_flags=-DMAKER_IMU_RVC=1',calls[0])
        self.assertEqual(calls[0][calls[0].index('--output-dir')+1],plan['output_dir'])
        self.assertEqual(calls[1][calls[1].index('--input-dir')+1],plan['output_dir'])

    def observer(self, run=RUN):
        observer = TelemetryObserver()
        observer.feed(BANNER, 1)
        for index, line in enumerate((run, COUNTS, UART, VALUE)):
            observer.feed(line, 2 + index * .01)
        return observer

    def test_recorded_values_preserve_units_and_missing_capabilities(self):
        snapshot = self.observer().snapshot(2.1)
        rvc = snapshot['rvc']
        self.assertTrue(rvc['valid'])
        self.assertEqual(rvc['value']['ypr_deg'], [-.01, -.75, .4])
        self.assertEqual(rvc['value']['accel_mg'], [-8, -12, 970])
        self.assertFalse(rvc['gyro_available'])
        self.assertIsNone(rvc['calibration_status'])
        self.assertFalse(rvc['robot_control_ready'])
        self.assertIsNone(snapshot['imu'])
        self.assertEqual(snapshot['profile']['id'], 'unknown')
        self.assertFalse(snapshot['profile']['encoder_wheels'])
        json.dumps(snapshot, allow_nan=False)

    def test_banner_is_not_a_motor_profile_and_help_is_not_firmware(self):
        bridge = Bridge(operations=Mock())
        bridge.serial = Mock()
        bridge.telemetry['serial_connected'] = True
        bridge.write = Mock()
        bridge.parse_line(MAKER_HELP_IDENTITY)
        for banner, token in RVC_BANNERS.items():
            bridge.parse_line(banner)
            bridge.parse_line(RVC_HELP_READY)
            self.assertEqual(bridge.snapshot()['firmware'], token)
            self.assertEqual(bridge.board_profile()['id'], 'unknown')
            bridge.poll_identity(100)
            with self.assertRaisesRegex(RuntimeError, 'recognized'):
                bridge.start_tuning('bench', 'WHEELS_UP')
            with self.assertRaisesRegex(RuntimeError, 'recognized'):
                bridge.start_firmware_update()
        bridge.write.assert_not_called()
        self.assertIsNone(parse_event(RVC_HELP_READY, 1))
        bridge.disconnect_serial()
        self.assertNotIn('firmware', bridge.telemetry)

    def test_invalid_fields_rejected(self):
        for line in (RUN.replace('frames=8385', 'frames=-1'),
                     RUN.replace('state=READY', 'state=UNKNOWN'),
                     RUN.replace('age_ms=4', 'age_ms=-2'), RUN + ' frames=9',
                     COUNTS.replace('repeats=0', 'repeats=4294967296'),
                     VALUE.replace('-0.01', 'nan'), VALUE.replace('-0.01', '1e300'),
                     VALUE.replace('-8,-12,970', '1,2'),
                     VALUE.replace('-8,-12,970', '1,2,32768'), UART.replace('0/0', '0/-1'),
                     UART + ' 0/0'):
            with self.subTest(line=line):
                self.assertIsNone(parse_event(line, 1))

    def test_stale_unqualified_and_missing_value(self):
        for state in ('STALE', 'SYNCING', 'UART_FAILED'):
            self.assertFalse(self.observer(RUN.replace('state=READY', 'state='+state)).snapshot(2.1)['rvc']['valid'])
        observer = self.observer()
        self.assertFalse(observer.snapshot(4)['rvc']['valid'])
        self.assertFalse(observer.snapshot(1)['rvc']['valid'])
        self.assertTrue(observer.snapshot(2.1)['rvc']['valid'])  # snapshots do not mutate state
        observer.feed(RUN.replace('84010', '85010').replace('8385', '8485'), 3)
        self.assertEqual(observer.snapshot(3)['rvc']['reason'], 'awaiting_value')
        observer.feed(VALUE, 3.8)
        self.assertFalse(observer.snapshot(3.8)['rvc']['valid'])
        for age in ('-1', '501'):
            self.assertFalse(self.observer(RUN.replace('age_ms=4', 'age_ms='+age)).snapshot(2.1)['rvc']['valid'])

    def test_device_restart_wrap_duplicate_and_stalled_frames(self):
        observer = self.observer()
        observer.feed(RUN, 3)
        observer.feed(VALUE, 3.01)
        self.assertEqual(observer.snapshot(3.1)['rvc']['reason'], 'no_progress')
        observer.feed(RUN.replace('84010', '85010'), 4)
        observer.feed(VALUE, 4.01)
        self.assertFalse(observer.snapshot(4.1)['rvc']['valid'])
        observer.feed(RUN.replace('84010', '10').replace('8385', '5'), 5)
        state = observer.snapshot(5)
        self.assertNotIn('counts', state['rvc'])
        self.assertFalse(state['rvc']['valid'])
        self.assertEqual(state['events'][-1]['code'], 'rvc_restart')
        observer = self.observer(RUN.replace('84010', '4294967290'))
        observer.feed(RUN.replace('84010', '994').replace('8385', '8485'), 3)
        observer.feed(VALUE, 3.01)
        self.assertTrue(observer.snapshot(3.1)['rvc']['valid'])
        self.assertNotEqual(observer.snapshot(3.1)['events'][-1].get('code'), 'rvc_restart')
        observer.boundary('disconnect', 4)
        self.assertIsNone(observer.snapshot(4)['rvc'])

    def test_errors_preserved_without_claiming_accuracy(self):
        observer = self.observer()
        observer.feed(COUNTS.replace('bad_checksum=0', 'bad_checksum=2'), 2.1)
        observer.feed(UART.replace('0/0', '3/2', 1), 2.11)
        state = observer.snapshot(2.2)['rvc']
        self.assertEqual(state['counts']['bad_checksum'], 2)
        self.assertEqual(state['uart']['totals'], [3, 0, 0, 0, 0])
        self.assertTrue(state['valid'])  # current readings; not an error-free test verdict
        self.assertFalse(state['robot_control_ready'])

    def test_stale_event_invalidates_before_next_periodic_report(self):
        observer = self.observer()
        observer.feed('EVENT STALE boot_ms=84520 age_ms=510', 2.2)
        self.assertFalse(observer.snapshot(2.2)['rvc']['valid'])
        observer.feed(RUN.replace('84010', '85010').replace('8385', '8485'), 3)
        observer.feed(VALUE, 3.01)
        self.assertTrue(observer.snapshot(3.1)['rvc']['valid'])

    def test_replay_uses_same_parser_and_never_transmits(self):
        with tempfile.TemporaryDirectory() as folder:
            service = OperationsService(Path(folder)/'recordings', Path(folder)/'reports', Path(folder)/'geometry.json')
            lines = [BANNER, RUN, COUNTS, UART, VALUE]
            for i, line in enumerate(lines):
                service.feed(line, 1+i*.01)
            self.assertTrue(service.snapshot(1.1)['observed']['rvc']['valid'])
            service.capture.read_recording = Mock(return_value={
                'id': 'a'*32, 'metadata': {}, 'state': 'complete', 'dropped': 0,
                'events': [dict(offset_s=1+i*.01, direction='rx', line=line) for i, line in enumerate(lines)] +
                          [dict(offset_s=1.1, direction='tx', line='GO')]})
            before = service.snapshot(1.1)
            service.transmit = Mock(side_effect=AssertionError('Replay transmitted'))
            replay = service.replay('a'*32)
            observer = TelemetryObserver()
            for i, line in enumerate(lines):
                observer.feed(line, 1+i*.01)
            self.assertEqual(replay['observed'], observer.snapshot(1.1))
            self.assertEqual(service.snapshot(1.1), before)
            service.transmit.assert_not_called()


if __name__ == '__main__':
    unittest.main()
