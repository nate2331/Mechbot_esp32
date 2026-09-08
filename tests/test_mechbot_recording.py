"""Passive capture/replay contract, using temporary files and controlled disk IO."""
import json
import math
import os
from pathlib import Path
import queue
import stat
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest import mock

from mechbot_recording import CaptureStore


class Clock:
    def __init__(self):
        self.now = 10.0

    def __call__(self):
        return self.now


class GatedStream:
    """Block disk writes without blocking the caller's capture submission."""
    def __init__(self, stream, entered, release):
        self.stream, self.entered, self.release = stream, entered, release

    def write(self, value):
        self.entered.set()
        if not self.release.wait(3):
            raise OSError("test disk gate timed out")
        return self.stream.write(value)

    def __getattr__(self, name):
        return getattr(self.stream, name)

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return self.stream.__exit__(*args)


class CaptureTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.clock = Clock()
        self.stores = []

    def tearDown(self):
        for store in self.stores:
            store.stop(timeout=2)
        self.temp.cleanup()

    def store(self, **kwargs):
        store = CaptureStore(self.root / "captures", clock=self.clock, **kwargs)
        self.stores.append(store)
        return store

    def finish(self, store):
        state = store.stop(timeout=2)
        self.assertNotEqual(state["state"], "stopping")
        return state

    def wait_state(self, store, expected):
        deadline = time.monotonic() + 2
        while time.monotonic() < deadline:
            state = store.snapshot()
            if state["state"] == expected:
                return state
            time.sleep(0.005)
        self.fail(f"Capture did not reach {expected}: {store.snapshot()}")

    def test_idle_empty_capture_and_idempotent_stop(self):
        store = self.store()
        self.assertEqual(store.snapshot()["state"], "idle")
        self.assertFalse(store.submit("rx", "T 1 0 0 0 0"))
        self.assertEqual(store.stop()["state"], "idle")
        begun = store.start({"firmware": "TEST_ONLY", "simulated": True})
        self.assertRegex(begun["id"], r"^[0-9a-f]{32}$")
        final = self.finish(store)
        self.assertEqual(final["state"], "completed")
        self.assertEqual((final["accepted"], final["written"], final["dropped"]), (0, 0, 0))
        self.assertEqual(store.stop(), final)
        self.assertEqual(store.read_recording(final["id"])["events"], [])

    def test_round_trip_preserves_order_directions_unicode_metadata_and_byte_total(self):
        store = self.store()
        metadata = {"firmware": "TEST_ONLY", "simulated": True, "config": {"pwm-fl": 0}}
        recording_id = store.start(metadata)["id"]
        metadata["config"]["pwm-fl"] = 255
        expected = [("rx", "T 1 0 0 0 0"), ("tx", "X"),
                    ("boundary", "disconnect"), ("boundary", "READY TEST_ONLY"),
                    ("rx", "note caf\u00e9 \U0001f916")]
        for offset, (direction, line) in enumerate(expected):
            self.clock.now = 10 + offset / 10
            self.assertTrue(store.submit(direction, line))
        state = self.finish(store)
        self.assertEqual(state["state"], "completed")
        self.assertEqual((state["accepted"], state["written"], state["dropped"]), (5, 5, 0))
        path = self.root / "captures" / (recording_id + ".jsonl")
        self.assertFalse(path.with_suffix(".part").exists())
        self.assertEqual(state["bytes"], path.stat().st_size)
        records = [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(records[0]["type"], "header")
        self.assertEqual(records[0]["schema"], 1)
        self.assertEqual(records[0]["id"], recording_id)
        self.assertEqual(records[-1]["type"], "footer")
        self.assertEqual(records[-1]["state"], "completed")
        replay = store.read_recording(recording_id)
        self.assertEqual(replay["metadata"]["config"]["pwm-fl"], 0)
        self.assertEqual([(event["direction"], event["line"]) for event in replay["events"]], expected)
        for index, event in enumerate(replay["events"]):
            self.assertAlmostEqual(event["offset_s"], index / 10)
        # Replay and inspection return independent data and create no new events.
        replay["events"][0]["line"] = "modified only in caller"
        replay["metadata"]["config"]["pwm-fl"] = 99
        again = store.read_recording(recording_id)
        self.assertEqual(again["events"][0]["line"], expected[0][1])
        self.assertEqual(again["metadata"]["config"]["pwm-fl"], 0)
        self.assertEqual(store.snapshot(), state)

    def test_event_limit_records_drops_and_completes_as_incomplete(self):
        store = self.store(max_events=2)
        recording_id = store.start({})["id"]
        self.assertTrue(store.submit("rx", "one"))
        self.assertTrue(store.submit("rx", "two"))
        self.assertFalse(store.submit("rx", "three"))
        final = self.finish(store)
        self.assertEqual((final["accepted"], final["written"], final["dropped"]), (2, 2, 1))
        self.assertEqual(final["state"], "incomplete")
        self.assertEqual(store.read_recording(recording_id)["state"], "incomplete")

    def test_byte_limit_includes_header_events_and_footer(self):
        store = self.store(max_bytes=2048)
        recording_id = store.start({})["id"]
        self.assertFalse(store.submit("rx", "x" * 1024))
        self.assertTrue(store.submit("tx", "X"))
        final = self.finish(store)
        path = self.root / "captures" / (recording_id + ".jsonl")
        self.assertLessEqual(path.stat().st_size, 2048)
        self.assertEqual(final["bytes"], path.stat().st_size)
        self.assertEqual(final["written"], 1)
        self.assertEqual(final["dropped"], 1)

    def test_raw_bounds_and_bad_inputs_drop_without_raising(self):
        store = self.store(max_events=20)
        store.start({})
        self.assertTrue(store.submit("rx", "x" * 1024))
        self.assertTrue(store.submit("rx", "\U0001f916" * 1024))
        rejected = [("rx", "x" * 1025), ("rx", "\U0001f916" * 1025),
                    ("rx", "\ud800"), ("rx", None), ("rx", b"bytes"),
                    ("rx", "a\nb"), ("rx", "a\rb"), ("rx", "a\x00b"),
                    ("RX", "line"), ("command", "line"), (None, "line"), ([], "line")]
        for direction, line in rejected:
            with self.subTest(direction=direction, line=repr(line)):
                self.assertFalse(store.submit(direction, line))
        final = self.finish(store)
        self.assertEqual(final["dropped"], len(rejected))
        self.assertEqual(final["written"], 2)

    def test_nonfinite_and_backward_clock_drops_preserve_nondecreasing_offsets(self):
        store = self.store()
        recording_id = store.start({})["id"]
        self.assertTrue(store.submit("rx", "first"))
        self.assertTrue(store.submit("rx", "same time"))
        self.clock.now = 10.5
        self.assertTrue(store.submit("rx", "later"))
        for value in (10.4, math.inf, math.nan, True, 10 ** 1000):
            self.clock.now = value
            self.assertFalse(store.submit("rx", "invalid time"))
        self.clock.now = 11
        self.assertTrue(store.submit("rx", "latest"))
        final = self.finish(store)
        self.assertEqual(final["dropped"], 5)
        self.assertEqual([event["offset_s"] for event in store.read_recording(recording_id)["events"]],
                         [0, 0, 0.5, 1])

    def test_gated_disk_queue_overflow_and_bounded_stop_do_not_block_submit(self):
        store = self.store(queue_capacity=2)
        entered, release = threading.Event(), threading.Event()
        original_open = store._open_part
        try:
            with mock.patch.object(store, "_open_part", side_effect=lambda path:
                                   GatedStream(original_open(path), entered, release)):
                recording_id = store.start({})["id"]
                self.assertTrue(entered.wait(1), "writer did not reach gated disk")
                done, results = threading.Event(), []
                def submit_events():
                    results.extend(store.submit("rx", str(index)) for index in range(3))
                    done.set()
                producer = threading.Thread(target=submit_events, daemon=True)
                producer.start()
                self.assertTrue(done.wait(0.5), "submission waited for blocked disk")
                self.assertEqual(results, [True, True, False])
                self.assertEqual(store.list_recordings(), [])
                self.assertFalse((self.root / "captures" / (recording_id + ".jsonl")).exists())
                state = store.stop(timeout=0.01)
                self.assertEqual(state["state"], "stopping")
                self.assertFalse(store.submit("tx", "X"))
                self.assertEqual(store.snapshot()["dropped"], 1)
                with self.assertRaises(RuntimeError):
                    store.start({})
                release.set()
                final = self.finish(store)
                self.assertEqual((final["written"], final["dropped"]), (2, 1))
                self.assertEqual(final["state"], "incomplete")
        finally:
            release.set()

    def test_disk_open_failure_is_observable_and_does_not_escape_submit(self):
        store = self.store()
        with mock.patch.object(store, "_open_part", side_effect=OSError("test disk unavailable")):
            store.start({})
            state = self.wait_state(store, "error")
        self.assertIn("test disk unavailable", state["error"])
        self.assertFalse(store.submit("rx", "still safe"))
        self.assertEqual(store.list_recordings(), [])
        self.assertEqual(store.stop()["state"], "error")

    def test_midstream_disk_failure_leaves_only_an_unfinalized_part(self):
        store = self.store()
        original_open = store._open_part
        class BrokenStream(GatedStream):
            writes = 0
            def write(self, value):
                self.writes += 1
                if self.writes > 1:
                    raise OSError("test disk failed after header")
                return self.stream.write(value)
        with mock.patch.object(store, "_open_part", side_effect=lambda path:
                               BrokenStream(original_open(path), None, None)):
            recording_id = store.start({})["id"]
            self.assertTrue(store.submit("rx", "first event"))
            state = self.wait_state(store, "error")
        self.assertIn("test disk failed after header", state["error"])
        self.assertTrue((self.root / "captures" / (recording_id + ".part")).exists())
        self.assertFalse((self.root / "captures" / (recording_id + ".jsonl")).exists())
        self.assertEqual(store.list_recordings(), [])
        with self.assertRaises(FileNotFoundError):
            store.read_recording(recording_id)

    def test_metadata_validation_and_active_start_do_not_replace_the_current_capture(self):
        store = self.store()
        for metadata in (None, True, [], {"n": math.nan}, {"n": math.inf}, {"n": object()},
                         {"large": "x" * 8192}):
            with self.subTest(metadata=repr(metadata)), self.assertRaises(ValueError):
                store.start(metadata)
            self.assertEqual(store.snapshot()["state"], "idle")
        recording_id = store.start({"firmware": "TEST_ONLY"})["id"]
        with self.assertRaises(RuntimeError):
            store.start({"firmware": "replacement"})
        self.assertEqual(store.snapshot()["id"], recording_id)
        self.assertTrue(store.submit("rx", "retained"))
        self.finish(store)
        self.assertEqual(store.read_recording(recording_id)["metadata"]["firmware"], "TEST_ONLY")

    def test_new_sessions_get_distinct_ids_and_fresh_counters(self):
        store = self.store(max_events=1)
        first = store.start({"session": 1})["id"]
        store.submit("rx", "first")
        store.submit("rx", "dropped")
        self.finish(store)
        second = store.start({"session": 2})["id"]
        self.assertNotEqual(first, second)
        self.assertEqual(store.snapshot()["dropped"], 0)
        self.finish(store)
        rows = store.list_recordings()
        self.assertEqual({row["id"] for row in rows}, {first, second})
        self.assertEqual({row["id"]: row["state"] for row in rows},
                         {first: "incomplete", second: "completed"})

    def test_invalid_limits_and_unbounded_stop_timeouts_are_rejected(self):
        for key, values in (("max_events", (0, -1, True, 1.5)),
                            ("queue_capacity", (0, -1, False, 2.5)),
                            ("max_bytes", (0, -1, True, 1024, 2048.5))):
            for value in values:
                with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                    self.store(**{key: value})
        store = self.store()
        for timeout in (-1, 5.01, True, math.inf, math.nan):
            with self.subTest(timeout=timeout), self.assertRaises(ValueError):
                store.stop(timeout=timeout)

    def test_ids_cannot_escape_capture_directory_and_parts_are_not_replayed(self):
        store = self.store()
        for recording_id in (None, True, "", "../outside", "a" * 31, "g" * 32,
                             "A" * 32, "/" + "a" * 32, "a" * 32 + ".jsonl", "a" * 31 + "\x00"):
            with self.subTest(recording_id=recording_id), self.assertRaises(ValueError):
                store.read_recording(recording_id)
        directory = self.root / "captures"
        directory.mkdir(exist_ok=True)
        recording_id = "b" * 32
        (directory / (recording_id + ".part")).write_text("incomplete", encoding="utf-8")
        self.assertEqual(store.list_recordings(), [])
        with self.assertRaises(FileNotFoundError):
            store.read_recording(recording_id)

    def test_symlink_leaf_and_parent_are_rejected_without_following_them(self):
        store = self.store()
        directory = self.root / "captures"
        directory.mkdir(exist_ok=True)
        outside = self.root / "outside.jsonl"
        outside.write_text("private outside capture directory", encoding="utf-8")
        recording_id = "c" * 32
        try:
            (directory / (recording_id + ".jsonl")).symlink_to(outside)
        except (OSError, NotImplementedError) as exc:
            self.skipTest(f"Platform cannot create symlink fixture: {exc}")
        with self.assertRaises(ValueError):
            store.read_recording(recording_id)
        self.assertEqual(store.list_recordings(), [])
        parent_link = self.root / "linked"
        parent_link.symlink_to(directory, target_is_directory=True)
        with self.assertRaises(ValueError):
            linked = CaptureStore(parent_link / "child", clock=self.clock)
            try:
                linked.start({})
            finally:
                linked.stop(timeout=2)
        self.assertFalse((directory / "child").exists())

    def test_strict_replay_rejects_corrupt_structure_offsets_and_footer_totals(self):
        store = self.store(max_events=20)
        recording_id = store.start({"simulated": True})["id"]
        store.submit("rx", "one")
        self.clock.now = 11
        store.submit("boundary", "disconnect")
        self.finish(store)
        path = self.root / "captures" / (recording_id + ".jsonl")
        original = path.read_bytes()
        base = [json.loads(line) for line in original.splitlines()]
        malformed = [b"", original[:-2], b"not JSON\n", original + b"{}\n"]
        for index, key, value in ((0, "schema", 2), (0, "id", "d" * 32),
                                  (1, "offset_s", -1), (1, "offset_s", True),
                                  (1, "offset_s", 2),
                                  (1, "offset_s", math.nan), (1, "direction", "execute"),
                                  (1, "line", "x" * 1025), (2, "offset_s", -0.5),
                                  (3, "written", 99), (3, "accepted", 99),
                                  (3, "dropped", True), (3, "state", "active")):
            records = json.loads(json.dumps(base))
            records[index][key] = value
            malformed.append(("\n".join(json.dumps(row) for row in records) + "\n").encode())
        try:
            for data in malformed:
                with self.subTest(data=data[:100]):
                    path.write_bytes(data)
                    with self.assertRaises(ValueError):
                        store.read_recording(recording_id)
        finally:
            path.write_bytes(original)
        self.assertEqual(len(store.read_recording(recording_id)["events"]), 2)

    def test_replay_never_invokes_capture_or_submission_methods(self):
        store = self.store()
        recording_id = store.start({"firmware": "TEST_ONLY"})["id"]
        store.submit("tx", "V 1 0 0")
        store.submit("tx", "X")
        self.finish(store)
        with mock.patch.object(store, "submit", side_effect=AssertionError("replay submitted")), \
             mock.patch.object(store, "start", side_effect=AssertionError("replay started capture")), \
             mock.patch.object(store, "_open_part", side_effect=AssertionError("replay wrote capture")):
            self.assertEqual(len(store.read_recording(recording_id)["events"]), 2)
            self.assertEqual(len(store.list_recordings()), 1)

    def test_relative_and_string_directory_inputs_are_supported(self):
        relative = os.path.relpath(self.root / "string-path", Path.cwd())
        store = CaptureStore(relative, clock=self.clock)
        self.stores.append(store)
        self.assertTrue(store.directory.is_absolute())
        recording_id = store.start({})["id"]
        self.finish(store)
        self.assertEqual(store.read_recording(recording_id)["state"], "completed")

    def test_final_accepted_event_is_drained_when_stop_races_with_queue_timeout(self):
        store = self.store()
        entered, release = threading.Event(), threading.Event()
        original_get = queue.Queue.get
        first = True
        def timed_out_before_last_submission(instance, *args, **kwargs):
            nonlocal first
            if first:
                first = False
                entered.set()
                if not release.wait(2):
                    raise OSError("queue timeout race fixture stalled")
                raise queue.Empty
            return original_get(instance, *args, **kwargs)
        try:
            with mock.patch.object(queue.Queue, "get", timed_out_before_last_submission):
                recording_id = store.start({})["id"]
                self.assertTrue(entered.wait(1))
                self.assertTrue(store.submit("rx", "last accepted event"))
                self.assertEqual(store.stop(timeout=0)["state"], "stopping")
                release.set()
                state = self.finish(store)
                self.assertEqual((state["accepted"], state["written"]), (1, 1))
                self.assertEqual(store.read_recording(recording_id)["events"][0]["line"],
                                 "last accepted event")
        finally:
            release.set()

    def test_worker_setup_failure_and_short_write_publish_error(self):
        store = self.store()
        original_safe = store._safe_path
        def safe_path(recording_id, suffix=".jsonl"):
            if threading.current_thread() is store._thread:
                raise OSError("worker path failed")
            return original_safe(recording_id, suffix)
        with mock.patch.object(store, "_safe_path", side_effect=safe_path):
            store.start({})
            self.assertIn("worker path failed", self.wait_state(store, "error")["error"])
        self.finish(store)
        original_open = store._open_part
        class ShortStream(GatedStream):
            def write(self, value):
                return self.stream.write(value[:1])
        with mock.patch.object(store, "_open_part", side_effect=lambda path:
                               ShortStream(original_open(path), None, None)):
            recording_id = store.start({})["id"]
            self.assertIn("short capture write", self.wait_state(store, "error")["error"])
        self.assertFalse(store.submit("rx", "discarded after error"))
        self.assertFalse((store.directory / (recording_id + ".jsonl")).exists())

    def test_terminal_status_cannot_replace_a_writer_that_has_not_exited(self):
        store = self.store()
        alive = SimpleNamespace(is_alive=lambda: True)
        with mock.patch.object(store, "_thread", alive):
            with self.assertRaises(RuntimeError):
                store.start({})
        self.assertEqual(store.snapshot()["state"], "idle")

    def test_reparse_directory_and_dangling_leaf_are_rejected_without_link_privileges(self):
        store = self.store()
        original_lstat = Path.lstat
        leaf = store.directory / (("e" * 32) + ".jsonl")
        for target, mode, attributes in ((store.directory, stat.S_IFDIR, 0x400),
                                          (leaf, stat.S_IFLNK, 0)):
            def lstat(path, *args, **kwargs):
                if path == target:
                    return SimpleNamespace(st_mode=mode, st_file_attributes=attributes)
                return original_lstat(path, *args, **kwargs)
            with self.subTest(target=target), mock.patch.object(Path, "lstat", lstat):
                with self.assertRaises(ValueError):
                    store.read_recording("e" * 32)

    def test_unicode_line_separators_round_trip_and_duplicate_json_keys_fail(self):
        store = self.store()
        recording_id = store.start({})["id"]
        value = "a\u2028b\u0085c"
        self.assertTrue(store.submit("rx", value))
        self.finish(store)
        self.assertEqual(store.read_recording(recording_id)["events"][0]["line"], value)
        path = store.directory / (recording_id + ".jsonl")
        raw = path.read_bytes()
        path.write_bytes(raw.replace(b'"schema":1', b'"schema":1,"schema":1'))
        with self.assertRaises(ValueError):
            store.read_recording(recording_id)
        rows = store.list_recordings()
        self.assertEqual(rows[0]["state"], "error")


if __name__ == "__main__":
    unittest.main()
