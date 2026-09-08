"""Offline calibration and serial-runner checks. No real serial port is opened."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import types
import unittest
from unittest import mock

import maker_auto_tune as tune


class CalibrationTests(unittest.TestCase):
    def test_finds_each_directional_minimum_with_repeated_starts(self):
        rig = tune.SimulatedRig()
        tuner = tune.Tuner(rig, progress=lambda _: None)
        for direction, expected in ((1, (36, 38, 45, 40)), (-1, (39, 40, 49, 42))):
            for wheel, minimum in enumerate(expected):
                result = tuner.minimum_start(wheel, direction)
                self.assertEqual(result["minimum_reliable_start_pwm"], minimum)
                self.assertEqual(result["highest_lower_tested_failure_pwm"], minimum - 1)
                self.assertEqual([t["pwm"] for t in result["trials"][-3:]], [minimum] * 3)
                self.assertTrue(all(t["started_and_sustained"] for t in result["trials"][-3:]))
        self.assertTrue(all(sum(p > 0 for p in values) == 1 for values, _, _ in rig.pulses))
        self.assertLessEqual(max(max(values) for values, _, _ in rig.pulses), tune.MAX_PWM)

    def test_failed_threshold_does_not_increase_past_ceiling_and_keeps_trials(self):
        tuner = tune.Tuner(tune.SimulatedRig(), maximum=32, progress=lambda _: None)
        with self.assertRaisesRegex(tune.CalibrationError, "never started"):
            tuner.minimum_start(0, 1)
        result = tuner.report["startup"]["forward"]["FL"]
        self.assertNotIn("minimum_reliable_start_pwm", result)
        self.assertEqual(result["trials"][-1]["pwm"], 32)

    def test_intermittent_start_is_not_called_reliable(self):
        class Intermittent(tune.SimulatedRig):
            attempts = 0
            def pulse(self, values, direction, seconds):
                observation = super().pulse(values, direction, seconds)
                if values[0] == 36:
                    self.attempts += 1
                    if self.attempts % 2 == 0:
                        observation.sustained[0] = False
                return observation
        tuner = tune.Tuner(Intermittent(), progress=lambda _: None)
        self.assertEqual(tuner.minimum_start(0, 1)["minimum_reliable_start_pwm"], 37)

    def test_full_feedback_search_verifies_shared_trim_in_both_directions_and_three_speeds(self):
        tuner = tune.Tuner(tune.SimulatedRig(), progress=lambda _: None)
        report = tuner.run()
        self.assertEqual(report["status"], "bench_trim_verified")
        self.assertEqual(len(report["shared_verification"]), 6)
        self.assertTrue(all(m["passed"] for m in report["shared_verification"]))
        self.assertNotEqual(report["recommended_shared_pwm"], [177] * 4)
        self.assertLessEqual(max(report["recommended_shared_pwm"]), 177)

    def test_direction_dependent_motor_is_not_given_false_shared_trim(self):
        report = tune.Tuner(tune.SimulatedRig(asymmetric=True), progress=lambda _: None).run()
        self.assertTrue(all(m["converged"] for m in report["directional_matching"].values()))
        self.assertEqual(report["status"], "measured_no_shared_trim")
        self.assertIsNone(report["recommended_shared_pwm"])
        self.assertTrue(any(not m["passed"] for m in report["shared_verification"]))

    def test_cli_requires_motion_flags_before_creating_report_or_importing_serial(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.json"
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                tune.main(["--run", "--output", str(path)])
            self.assertFalse(path.exists())

    def test_simulation_writes_report_without_overwriting(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.json"
            with contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(tune.main(["--simulate", "--output", str(path)]), 0)
            report = json.loads(path.read_text())
            self.assertTrue(report["simulated"])
            self.assertFalse(report["settings_saved_to_controller"])
            original = path.read_bytes()
            with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
                tune.main(["--simulate", "--output", str(path)])
            self.assertEqual(path.read_bytes(), original)


class MeasurementTests(unittest.TestCase):
    def frames(self, direction=1, moving=(True,) * 4):
        return [tune.Frame(i * 0.2, i * 200,
                           tuple(round(direction * tune.COUNTS_PER_REV[w] * i / 10) if moving[w] else 0 for w in range(4)))
                for i in range(7)]

    def test_uses_per_wheel_cpr_and_preserves_direction(self):
        for direction in (1, -1):
            result = tune.analyse(self.frames(direction), 0, [100] * 4, direction)
            for rpm in result.rpm:
                self.assertAlmostEqual(rpm, 30, delta=0.08)
            self.assertTrue(all(result.sustained))
            self.assertEqual(result.start_delay_bounds_ms, [[0, 200]] * 4)

    def test_encoder_twitch_is_not_a_reliable_start(self):
        frames = [tune.Frame(i / 5, i * 200, (8 if i else 0, 0, 0, 0)) for i in range(7)]
        result = tune.analyse(frames, 0, [50, 0, 0, 0], 1)
        self.assertIsNotNone(result.start_delay_bounds_ms[0])
        self.assertFalse(result.sustained[0])

    def test_late_start_and_slower_steady_speed_are_separate(self):
        frames = []
        for i in range(10):
            frames.append(tune.Frame(i / 5, i * 200,
                                    (i * 100, max(0, i - 3) * 100, i * 50, 0)))
        result = tune.analyse(frames, 0, [100, 100, 100, 0], 1)
        self.assertGreater(result.start_delay_bounds_ms[1][0], result.start_delay_bounds_ms[0][1])
        self.assertAlmostEqual(result.rpm[0], result.rpm[1], delta=0.02)
        self.assertLess(result.rpm[2], result.rpm[0] * 0.51)

    def test_wrong_sign_and_uncommanded_wheel_fail(self):
        with self.assertRaisesRegex(tune.CalibrationError, "direction"):
            tune.analyse(self.frames(-1), 0, [100] * 4, 1)
        with self.assertRaisesRegex(tune.CalibrationError, "Uncommanded"):
            tune.analyse(self.frames(), 0, [100, 0, 0, 0], 1)

    def test_clock_wrap_is_accepted_reset_and_stale_samples_are_not(self):
        self.assertAlmostEqual(tune.device_seconds(100, 0xFFFFFFFF - 99), 0.2)
        with self.assertRaises(tune.CalibrationError):
            tune.device_seconds(0, 2000)
        frames = self.frames()
        frames[-1] = tune.Frame(2.0, 2000, frames[-1].counts)
        with self.assertRaisesRegex(tune.CalibrationError, "sample gap"):
            tune.analyse(frames, 0, [100] * 4, 1)


class FakeClock:
    def __init__(self):
        self.now = 10.0
    def __call__(self):
        return self.now


class FakePort:
    """Emulate the existing firmware wire protocol, including its stop lease."""
    def __init__(self, clock):
        self.clock = clock
        self.queue, self.commands = [], []
        self.config = dict(zip(tune.PWM_KEYS, (171, 172, 173, 174)))
        self.config.update({"heading-kp": 0.7, "heading-max": 0.3,
                            "heading-deadband-deg": 1.5, "heading-sign": 1, "heading-enabled": 1})
        self.counts = [0.0] * 4
        self.direction = 0
        self.last_command = 0
        self.motion_started = 0
        self.next_telemetry = clock()
        self.invalid = [0] * 4
        self.health = (0, 0)
        self.drop_encoders = False
        self.drop_imu = False
        self.swap_encoders = False
        self.ignore_set = False
        self.break_on_motion = False
        self.wrong_applied = False
        self.closed = False

    def add(self, line):
        self.queue.append((line + "\n").encode())

    def write(self, value):
        command = value.decode().strip()
        self.commands.append(command)
        if command == "X":
            self.direction = 0
            self.add("OK STOP")
        elif command == "?":
            self.add("Maker mapping: FL=M2 FR=M3 RL=M1 RR=M0; all encoders forward-positive")
            self.add("Watchdog: 300 ms")
        elif command == "CFG GET":
            for key, item in self.config.items():
                self.add(f"CFG {key} {item}")
            self.add("OK CFG GET")
        elif command.startswith("CFG SET"):
            self.direction = 0
            _, _, key, item = command.split()
            if not self.ignore_set:
                self.config[key] = float(item)
            self.add("OK CFG SET " + key)
        elif command == "DIAG":
            for i, wheel in enumerate(tune.WHEELS):
                applied = self.config[tune.PWM_KEYS[i]] * self.direction
                if self.wrong_applied and self.direction and i == 0:
                    applied = 0
                self.add(f"D {wheel} PWM {applied} A 10 B 10 INVALID {self.invalid[i]}")
        elif command.startswith("V "):
            if self.direction == 0:
                self.motion_started = self.clock()
            self.direction = int(command.split()[1])
            self.last_command = self.clock()
        else:
            raise AssertionError("Unexpected wire command: " + command)
        return len(value)

    def readline(self, size):
        if self.queue:
            return self.queue.pop(0)
        if self.break_on_motion and self.direction:
            raise KeyboardInterrupt("simulated interruption")
        self.clock.now += 0.02
        if self.direction and self.clock() - self.last_command > 0.3:
            self.direction = 0
            self.add("WARN watchdog stopped")
        if self.direction and self.clock() - self.motion_started >= 0.2:
            for i, key in enumerate(tune.PWM_KEYS):
                if self.config[key] >= 40:
                    encoder = (2 if i == 0 else 0 if i == 2 else i) if self.swap_encoders else i
                    self.counts[encoder] += self.direction * tune.COUNTS_PER_REV[i] * 0.02
        if self.clock() >= self.next_telemetry:
            self.next_telemetry = self.clock() + 0.2
            stamp = round(self.clock() * 1000)
            if not (self.drop_encoders and self.direction):
                self.add("T " + str(stamp) + " " + " ".join(str(round(c)) for c in self.counts))
            if not (self.drop_imu and self.direction):
                self.add(f"I {stamp} 0 0 0 1 0 0 0 0 0 0 3")
            self.add(f"N {stamp} 0 0 0 0 {int(self.config['heading-enabled'])} 0 1")
            self.add(f"H {stamp} IMU 1 {stamp} {self.health[0]} {self.health[1]}")
        return self.queue.pop(0) if self.queue else b""

    def reset_input_buffer(self):
        self.queue.clear()

    def close(self):
        self.closed = True


class SerialRunnerTests(unittest.TestCase):
    def setUp(self):
        self.clock = FakeClock()
        self.port = FakePort(self.clock)
        self.original = dict(self.port.config)
        self.rig = tune.MakerSerialRig(self.port, self.clock)
        self.rig.prepare()

    def tearDown(self):
        self.port.break_on_motion = False
        self.port.ignore_set = False
        self.rig.restore()
        self.assertEqual(self.port.direction, 0)
        self.assertEqual(self.port.config, self.original)
        self.assertNotIn("CFG SAVE", self.port.commands)

    def test_real_runner_protocol_pulse_stops_and_restores(self):
        observation = self.rig.pulse([100] * 4, 1, 1.2)
        self.assertTrue(all(observation.sustained))
        for rpm in observation.rpm:
            self.assertAlmostEqual(rpm, 60, delta=0.2)
        self.assertEqual(self.port.direction, 0)
        self.assertFalse(self.port.config["heading-enabled"])

    def test_below_threshold_is_a_measurement_not_a_false_fault(self):
        observation = self.rig.pulse([16, 0, 0, 0], 1, 1.2)
        self.assertEqual(observation.sustained, [False] * 4)
        self.assertEqual(observation.start_delay_bounds_ms, [None] * 4)

    def test_stale_encoders_stop_an_active_pulse(self):
        self.port.drop_encoders = True
        with self.assertRaisesRegex(tune.CalibrationError, "Encoder telemetry is stale"):
            self.rig.pulse([100] * 4, 1, 1.2)
        self.assertEqual(self.port.direction, 0)
        self.assertEqual(self.port.commands[-1], "X")

    def test_imu_loss_stops_an_active_pulse(self):
        self.port.drop_imu = True
        with self.assertRaisesRegex(tune.CalibrationError, "telemetry is stale"):
            self.rig.pulse([100] * 4, 1, 1.2)
        self.assertEqual(self.port.direction, 0)

    def test_swapped_motor_or_encoder_fails_during_motion(self):
        self.port.swap_encoders = True
        with self.assertRaisesRegex(tune.CalibrationError, "Unexpected wheel motion"):
            self.rig.pulse([100, 0, 0, 0], 1, 1.2)
        self.assertEqual(self.port.direction, 0)

    def test_keyboard_interrupt_stops_immediately(self):
        self.port.break_on_motion = True
        with self.assertRaises(KeyboardInterrupt):
            self.rig.pulse([100] * 4, 1, 1.2)
        self.assertEqual(self.port.direction, 0)

    def test_settings_readback_failure_does_not_start_motion(self):
        self.port.ignore_set = True
        with self.assertRaisesRegex(tune.CalibrationError, "did not apply"):
            self.rig.pulse([100] * 4, 1, 1.2)
        self.assertFalse(any(c.startswith("V ") for c in self.port.commands))

    def test_ceiling_and_duration_limits_reject_before_motion(self):
        with self.assertRaisesRegex(tune.CalibrationError, "range"):
            self.rig.pulse([178] * 4, 1, 1.2)
        with self.assertRaisesRegex(tune.CalibrationError, "duration"):
            self.rig.pulse([100] * 4, 1, 10)
        self.assertFalse(any(c.startswith("V ") for c in self.port.commands))

    def test_imu_reset_rejected_without_another_pulse(self):
        self.port.health = (1, 0)
        with self.assertRaisesRegex(tune.CalibrationError, "reset"):
            self.rig.pulse([100] * 4, 1, 1.2)
        self.assertFalse(any(c.startswith("V ") for c in self.port.commands))

    def test_invalid_encoder_transitions_invalidate_a_measurement(self):
        read = self.port.readline
        def with_noise(size):
            if self.port.direction:
                self.port.invalid[0] = 1
            return read(size)
        self.port.readline = with_noise
        with self.assertRaisesRegex(tune.CalibrationError, "invalid transitions"):
            self.rig.pulse([100] * 4, 1, 1.2)
        self.assertEqual(self.port.direction, 0)

    def test_applied_pwm_mismatch_stops_during_the_pulse(self):
        self.port.wrong_applied = True
        with self.assertRaisesRegex(tune.CalibrationError, "Applied motor PWM"):
            self.rig.pulse([100] * 4, 1, 1.2)
        self.assertEqual(self.port.direction, 0)


class HardwareEntryPointTests(unittest.TestCase):
    def run_fake(self, interrupted=False):
        clock = FakeClock()
        port = FakePort(clock)
        original = dict(port.config)
        port.break_on_motion = interrupted
        original_rig = tune.MakerSerialRig
        class ClockedRig(original_rig):
            def __init__(self, serial_port):
                super().__init__(serial_port, clock)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "run.json"
            with mock.patch.dict("sys.modules", {"serial": types.SimpleNamespace(Serial=lambda *a, **k: port)}), \
                    mock.patch.object(tune, "MakerSerialRig", ClockedRig), \
                    contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                code = tune.main(["--run", "--wheels-up", "--bridge-stopped", "--normal-wiring", "--output", str(path)])
            report = json.loads(path.read_text())
        self.assertTrue(port.closed)
        self.assertEqual(port.direction, 0)
        self.assertEqual(port.config, original)
        self.assertTrue(report["original_live_settings_restored"])
        self.assertEqual(report["original_live_settings"], original)
        self.assertFalse(report["settings_saved_to_controller"])
        self.assertNotIn("CFG SAVE", port.commands)
        return code, report

    def test_full_hardware_entry_point_using_fake_serial_restores_on_success(self):
        code, report = self.run_fake()
        self.assertEqual(code, 0)
        self.assertEqual(report["status"], "bench_trim_verified")
        self.assertFalse(report["simulated"])

    def test_interruption_saves_partial_report_and_restores_originals(self):
        code, report = self.run_fake(interrupted=True)
        self.assertEqual(code, 1)
        self.assertEqual(report["status"], "aborted")
        self.assertIsNone(report["recommended_shared_pwm"])
        self.assertIn("FL", report["startup"]["forward"])


if __name__ == "__main__":
    unittest.main()
