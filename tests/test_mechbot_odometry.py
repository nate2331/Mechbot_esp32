"""Pure passive-odometry contract; all dimensions below are synthetic fixtures.

These values are mathematical test inputs, never calibration for the real robot.
No serial device, bridge, motor command, or hardware dependency is imported.
"""
import math
from pathlib import Path
import subprocess
import sys
import unittest

from mechbot_odometry import PassiveOdometry, inverse_wheel_motion, validate_geometry


WHEELS = ("FL", "FR", "RL", "RR")
# Unit circumference makes analytic motion cases independent of conversion code.
GEOMETRY = {"wheel_diameter_m": 1 / math.pi, "wheelbase_m": 0.4, "track_width_m": 0.2}
CPR = dict(zip(WHEELS, (100.0, 200.0, 400.0, 800.0)))
POSE_KEYS = ("x_m", "y_m", "yaw_rad", "distance_m")


def sample(wheel_speeds=(0.0, 0.0, 0.0, 0.0), dt=1.0):
    """A WheelRateEstimator-shaped sample for the unit-circumference fixture."""
    return {"valid": True, "reason": "ok", "dt_s": dt,
            "ticks_per_s": {wheel: speed * CPR[wheel]
                            for wheel, speed in zip(WHEELS, wheel_speeds)}}


class GeometryTests(unittest.TestCase):
    def test_requires_all_dimensions_and_returns_only_normalized_known_keys(self):
        source = {"wheel_diameter_m": 1, "wheelbase_m": 2, "track_width_m": 3,
                  "note": "synthetic geometry"}
        result = validate_geometry(source)
        self.assertEqual(result, {"wheel_diameter_m": 1.0, "wheelbase_m": 2.0,
                                  "track_width_m": 3.0})
        self.assertIsNot(result, source)
        self.assertTrue(all(isinstance(value, float) for value in result.values()))
        result["wheelbase_m"] = 0.5
        self.assertEqual(source["wheelbase_m"], 2)
        for key in GEOMETRY:
            with self.subTest(missing=key), self.assertRaises(ValueError):
                validate_geometry({k: v for k, v in GEOMETRY.items() if k != key})

    def test_invalid_geometry_is_rejected_without_guessing_or_string_coercion(self):
        for value in (None, False, True, [], "geometry", 1):
            with self.subTest(container=value), self.assertRaises(ValueError):
                validate_geometry(value)
        for key in GEOMETRY:
            for value in (None, False, True, "0.2", 0, -0.1, 3.00001,
                          math.nan, math.inf, -math.inf):
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    validate_geometry({**GEOMETRY, key: value})
        self.assertEqual(validate_geometry({key: 3 for key in GEOMETRY}),
                         {key: 3.0 for key in GEOMETRY})
        self.assertEqual(validate_geometry({key: 1e-9 for key in GEOMETRY}),
                         {key: 1e-9 for key in GEOMETRY})


class InverseWheelMotionTests(unittest.TestCase):
    def assertDelta(self, distances, forward, left, yaw):
        result = inverse_wheel_motion(distances, GEOMETRY)
        self.assertAlmostEqual(result["forward_m"], forward)
        self.assertAlmostEqual(result["left_m"], left)
        self.assertAlmostEqual(result["yaw_rad"], yaw)

    def test_cardinal_directions_match_the_existing_mecanum_sign_convention(self):
        for distances, forward, left, yaw in (
            ((0, 0, 0, 0), 0, 0, 0),
            ((1, 1, 1, 1), 1, 0, 0),
            ((-1, -1, -1, -1), -1, 0, 0),
            ((-1, 1, 1, -1), 0, 1, 0),
            ((1, -1, -1, 1), 0, -1, 0),
            ((-0.3, 0.3, -0.3, 0.3), 0, 0, 1),
            ((0.3, -0.3, 0.3, -0.3), 0, 0, -1),
            ((0.65, 1.35, 1.15, 0.85), 1, 0.25, 1 / 3),
        ):
            with self.subTest(distances=distances):
                self.assertDelta(distances, forward, left, yaw)

    def test_geometry_uses_full_wheel_center_spacing(self):
        wider = {**GEOMETRY, "wheelbase_m": 0.8, "track_width_m": 0.4}
        result = inverse_wheel_motion([-0.3, 0.3, -0.3, 0.3], wider)
        self.assertAlmostEqual(result["yaw_rad"], 0.5)

    def test_finite_large_average_does_not_overflow_intermediate_sum(self):
        result = inverse_wheel_motion([1e308] * 4, GEOMETRY)
        self.assertEqual(result, {"forward_m": 1e308, "left_m": 0.0, "yaw_rad": 0.0})

    def test_helpers_reject_bad_distance_shapes_values_and_geometry(self):
        for distances in (None, True, "1234", {}, [], [1, 2, 3], [1] * 5):
            with self.subTest(distances=distances), self.assertRaises(ValueError):
                inverse_wheel_motion(distances, GEOMETRY)
        for index in range(4):
            for value in (None, True, False, "1", math.nan, math.inf, -math.inf):
                distances = [0.0] * 4
                distances[index] = value
                with self.subTest(index=index, value=value), self.assertRaises(ValueError):
                    inverse_wheel_motion(distances, GEOMETRY)
        with self.assertRaises(ValueError):
            inverse_wheel_motion([0] * 4, {**GEOMETRY, "track_width_m": 0})


class PassiveOdometryTests(unittest.TestCase):
    def setUp(self):
        self.odom = PassiveOdometry(GEOMETRY, CPR)

    def assertPose(self, state, x=0.0, y=0.0, yaw=0.0, distance=0.0):
        for key, expected in zip(POSE_KEYS, (x, y, yaw, distance)):
            self.assertTrue(math.isfinite(state[key]), key)
            self.assertAlmostEqual(state[key], expected, places=9, msg=key)

    def assertNoIntegration(self, before, after, reason):
        self.assertFalse(after["valid"])
        self.assertEqual(after["reason"], reason)
        for key in POSE_KEYS:
            self.assertEqual(after[key], before[key], key)

    def test_default_has_no_invented_geometry_or_pose_validity(self):
        odom = PassiveOdometry()
        state = odom.snapshot()
        self.assertFalse(state["configured"])
        self.assertFalse(state["valid"])
        self.assertPose(state)
        state = odom.update(sample((1, 1, 1, 1)))
        self.assertFalse(state["configured"])
        self.assertNoIntegration(odom.snapshot(), state, "geometry_required")
        self.assertPose(state)

    def test_constructor_requires_both_calibration_inputs_or_neither(self):
        for cpr in (CPR, {}, True, False):
            with self.subTest(cpr=cpr), self.assertRaises(ValueError):
                PassiveOdometry(counts_per_revolution=cpr)
        with self.assertRaises(ValueError):
            PassiveOdometry(geometry=GEOMETRY)

    def test_zero_rates_are_a_valid_stationary_measurement(self):
        state = self.odom.update(sample())
        self.assertTrue(state["configured"])
        self.assertTrue(state["valid"])
        self.assertEqual(state["reason"], "ok")
        self.assertPose(state)

    def test_per_wheel_cpr_and_dt_produce_analytic_forward_and_left_motion(self):
        # Unequal CPR values represent identical physical wheel speeds.
        state = self.odom.update(sample((1, 1, 1, 1), dt=0.25))
        self.assertPose(state, x=0.25, distance=0.25)
        state = self.odom.update(sample((-1, 1, 1, -1), dt=0.5))
        self.assertPose(state, x=0.25, y=0.5, distance=0.75)
        self.assertTrue(state["valid"])

    def test_wheel_diameter_controls_scale(self):
        self.odom.configure({**GEOMETRY, "wheel_diameter_m": 2 / math.pi}, CPR)
        self.assertPose(self.odom.update(sample((1, 1, 1, 1), dt=0.5)),
                        x=1, distance=1)

    def test_rotation_does_not_add_translation_and_heading_rotates_next_step(self):
        turn = 0.3 * math.pi / 2
        state = self.odom.update(sample((-turn, turn, -turn, turn)))
        self.assertPose(state, yaw=math.pi / 2)
        state = self.odom.update(sample((1, 1, 1, 1)))
        self.assertPose(state, y=1, yaw=math.pi / 2, distance=1)

    def test_mixed_translation_and_turn_use_midpoint_heading(self):
        turn = 0.3 * math.pi / 2
        state = self.odom.update(sample((1 - turn, 1 + turn, 1 - turn, 1 + turn)))
        self.assertPose(state, x=math.sqrt(0.5), y=math.sqrt(0.5),
                        yaw=math.pi / 2, distance=1)
        state = self.odom.update(sample((-1, 1, 1, -1), dt=0.5))
        self.assertPose(state, x=math.sqrt(0.5) - 0.5, y=math.sqrt(0.5),
                        yaw=math.pi / 2, distance=1.5)

    def test_distance_is_traveled_translation_not_displacement(self):
        self.odom.update(sample((1, 1, 1, 1)))
        state = self.odom.update(sample((-1, -1, -1, -1)))
        self.assertPose(state, distance=2)
        state = self.odom.update(sample((0, 2, 2, 0)))
        self.assertPose(state, x=1, y=1, distance=2 + math.sqrt(2))

    def test_yaw_wraps_in_both_directions_and_remains_bounded(self):
        turn = 0.3 * 3 * math.pi / 2
        state = self.odom.update(sample((-turn, turn, -turn, turn)))
        self.assertPose(state, yaw=-math.pi / 2)
        self.odom.reset()
        state = self.odom.update(sample((turn, -turn, turn, -turn)))
        self.assertPose(state, yaw=math.pi / 2)
        for _ in range(20):
            state = self.odom.update(sample((-turn, turn, -turn, turn)))
            self.assertGreaterEqual(state["yaw_rad"], -math.pi)
            self.assertLessEqual(state["yaw_rad"], math.pi)

    def test_unavailable_samples_preserve_pose_and_report_the_estimator_reason(self):
        before = self.odom.update(sample((1, 1, 1, 1)))
        for reason in ("initial", "duplicate", "gap", "clock_reset", "counter_jump", "host_clock"):
            with self.subTest(reason=reason):
                after = self.odom.update({"valid": False, "reason": reason})
                self.assertNoIntegration(before, after, reason)
        for reason in (None, "", False):
            after = self.odom.update({"valid": False, "reason": reason})
            self.assertNoIntegration(before, after, "sample_unavailable")
        self.assertNoIntegration(before, self.odom.update({"valid": False, "reason": 42}), "42")
        # Only the next accepted interval is integrated; no gap is backfilled.
        self.assertPose(self.odom.update(sample((1, 1, 1, 1), dt=0.25)),
                        x=1.25, distance=1.25)

    def test_malformed_samples_are_invalid_without_exceptions_or_integration(self):
        before = self.odom.update(sample((1, 1, 1, 1)))
        invalid = [None, False, [], "sample", {}, {"valid": True},
                   {**sample(), "valid": 1}, {**sample(), "valid": "true"}]
        for dt in (None, False, True, "0.5", 0, -0.1, 1.500001, math.nan, math.inf):
            invalid.append({**sample(), "dt_s": dt})
        for rates in (None, [], [0] * 4, "rates", {"RL": 0, "RR": 0}):
            invalid.append({**sample(), "ticks_per_s": rates})
        for wheel in WHEELS:
            for value in (None, False, True, "1", math.nan, math.inf, -math.inf):
                rates = dict.fromkeys(WHEELS, 0)
                rates[wheel] = value
                invalid.append({**sample(), "ticks_per_s": rates})
            invalid.append({**sample(), "ticks_per_s":
                            {key: 0 for key in WHEELS if key != wheel}})
        for bad in invalid:
            with self.subTest(sample=bad):
                self.assertNoIntegration(before, self.odom.update(bad), "invalid_sample")
        # The accepted upper boundary remains valid after all rejected samples.
        self.assertPose(self.odom.update(sample((1, 1, 1, 1), dt=1.5)),
                        x=2.5, distance=2.5)

    def test_numerical_overflow_does_not_poison_the_pose(self):
        odom = PassiveOdometry({key: 3.0 for key in GEOMETRY}, dict.fromkeys(WHEELS, 1e-308))
        before = odom.snapshot()
        rates = {"valid": True, "dt_s": 1.5, "ticks_per_s": dict.fromkeys(WHEELS, 1e308)}
        self.assertNoIntegration(before, odom.update(rates), "invalid_sample")

    def test_huge_integers_fail_validation_without_partial_pose_changes(self):
        huge = 1 << 20000
        before = self.odom.update(sample((1, 1, 1, 1)))
        invalid = [{**sample(), "dt_s": huge},
                   {**sample(), "ticks_per_s": {**sample()["ticks_per_s"], "FL": huge}}]
        for index, rates in enumerate(invalid):
            with self.subTest(case=index):
                self.assertNoIntegration(before, self.odom.update(rates), "invalid_sample")
        with self.assertRaises(ValueError):
            validate_geometry({**GEOMETRY, "wheelbase_m": huge})
        with self.assertRaises(ValueError):
            inverse_wheel_motion([huge, 0, 0, 0], GEOMETRY)
        before = self.odom.snapshot()
        with self.assertRaises(ValueError):
            self.odom.configure(GEOMETRY, {**CPR, "FL": huge})
        self.assertEqual(self.odom.snapshot(), before)

    def test_extremely_large_finite_angle_finishes_in_bounded_time(self):
        # A subprocess timeout catches an angle-subtraction loop without leaving
        # a runaway background thread in this test process.
        code = """
import math, sys
sys.path.insert(0, sys.argv[1])
from mechbot_odometry import PassiveOdometry
geometry = {'wheel_diameter_m': 1/math.pi, 'wheelbase_m': .4, 'track_width_m': .2}
odom = PassiveOdometry(geometry, dict.fromkeys(('FL','FR','RL','RR'), 1))
state = odom.update({'valid': True, 'dt_s': 1, 'ticks_per_s':
                     {'FL': -1e100, 'FR': 1e100, 'RL': -1e100, 'RR': 1e100}})
assert state['valid'], state
assert -math.pi <= state['yaw_rad'] <= math.pi, state
assert state['x_m'] == state['y_m'] == state['distance_m'] == 0, state
"""
        result = subprocess.run([sys.executable, "-c", code, str(Path(__file__).resolve().parents[1])],
                                capture_output=True, text=True, timeout=3)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_invalid_configuration_is_atomic_and_constructors_reject_it(self):
        self.odom.update(sample((1, 1, 1, 1)))
        invalid_cpr = [None, True, False, [], [100] * 4, "cpr", {}, {"RL": 100, "RR": 100}]
        for wheel in WHEELS:
            for value in (None, True, False, "100", 0, -1, math.nan, math.inf):
                invalid_cpr.append({**CPR, wheel: value})
        for cpr in invalid_cpr:
            before = self.odom.snapshot()
            with self.subTest(cpr=cpr):
                with self.assertRaises(ValueError):
                    self.odom.configure({**GEOMETRY, "wheel_diameter_m": 2 / math.pi}, cpr)
                self.assertEqual(self.odom.snapshot(), before)
                with self.assertRaises(ValueError):
                    PassiveOdometry(GEOMETRY, cpr)
        for geometry in (True, False, None, {}, {**GEOMETRY, "wheelbase_m": True}):
            before = self.odom.snapshot()
            with self.subTest(geometry=geometry):
                with self.assertRaises(ValueError):
                    self.odom.configure(geometry, CPR)
                self.assertEqual(self.odom.snapshot(), before)
                if geometry is not None:
                    with self.assertRaises(ValueError):
                        PassiveOdometry(geometry, CPR)
        self.assertPose(self.odom.update(sample((1, 1, 1, 1))), x=2, distance=2)

    def test_configuration_copies_inputs_and_reconfiguration_resets_estimate(self):
        geometry, cpr = dict(GEOMETRY), dict(CPR)
        state = self.odom.configure(geometry, cpr)
        self.assertTrue(state["configured"])
        self.assertFalse(state["valid"])
        self.assertPose(state)
        geometry["wheel_diameter_m"] = 2 / math.pi
        cpr["FL"] = 1
        self.assertPose(self.odom.update(sample((1, 1, 1, 1))), x=1, distance=1)
        state = self.odom.configure({**GEOMETRY, "wheel_diameter_m": 2 / math.pi}, CPR)
        self.assertTrue(state["configured"])
        self.assertFalse(state["valid"])
        self.assertPose(state)
        self.assertPose(self.odom.update(sample((1, 1, 1, 1))), x=2, distance=2)

    def test_reset_keeps_calibration_but_requires_a_new_valid_sample(self):
        self.odom.update(sample((1, 1, 1, 1)))
        state = self.odom.reset()
        self.assertTrue(state["configured"])
        self.assertFalse(state["valid"])
        self.assertEqual(state["reason"], "reset")
        self.assertPose(state)
        self.assertPose(self.odom.update(sample((1, 1, 1, 1), dt=0.5)),
                        x=0.5, distance=0.5)

    def test_returned_snapshots_do_not_alias_internal_state(self):
        updated = self.odom.update(sample((1, 1, 1, 1)))
        copied = self.odom.snapshot()
        updated["x_m"] = 999
        copied["distance_m"] = 999
        self.assertPose(self.odom.snapshot(), x=1, distance=1)


if __name__ == "__main__":
    unittest.main()
