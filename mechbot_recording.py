"""Bounded passive capture and pure replay, with no robot transport."""
import json
import math
import os
from pathlib import Path
import queue
import re
import stat
import threading
import time
import uuid

FOOTER_RESERVE = 1024
MAX_METADATA_BYTES = 8192
MAX_LINE_CHARS = 1024
MAX_LINE_BYTES = 4096


def _encode_record(record):
    """Return compact, finite JSON plus newline as UTF-8 bytes."""
    try:
        return (json.dumps(record, ensure_ascii=False, allow_nan=False,
                           separators=(',', ':')) + '\n').encode('utf-8')
    except (TypeError, ValueError, OverflowError, UnicodeError, RecursionError) as exc:
        raise ValueError('record encoding failed') from exc


def _number(value):
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError('timestamp must be a finite number')
    try:
        value = float(value)
    except (ValueError, OverflowError) as exc:
        raise ValueError('timestamp is outside the finite range') from exc
    if not math.isfinite(value):
        raise ValueError('timestamp must be finite')
    return value


def _line_valid(direction, line):
    return (isinstance(direction, str) and direction in {'rx', 'tx', 'boundary'}
            and isinstance(line, str) and len(line) <= MAX_LINE_CHARS
            and not any(character in line for character in ('\r', '\n', '\0'))
            and len(line.encode('utf-8')) <= MAX_LINE_BYTES)


def _linked(info):
    return (stat.S_ISLNK(info.st_mode)
            or bool(getattr(info, 'st_file_attributes', 0)
                    & getattr(stat, 'FILE_ATTRIBUTE_REPARSE_POINT', 0x400)))


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('duplicate JSON object key')
        result[key] = value
    return result


class _CapturePreparation:
    """Opaque, single-use allocation; filesystem work has already completed."""
    __slots__ = ('owner', 'recording_id', 'options', 'used')

    def __init__(self, owner, recording_id):
        self.owner, self.recording_id = owner, recording_id
        self.options = (owner.max_events, owner.max_bytes, owner.queue_capacity)
        self.used = False


class CaptureStore:
    def __init__(self, directory, *, max_events=10000, max_bytes=5_000_000,
                 queue_capacity=256, clock=time.monotonic):
        for name, value, minimum in (('max_events', max_events, 1),
                                     ('queue_capacity', queue_capacity, 1),
                                     ('max_bytes', max_bytes, 2048)):
            if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
                raise ValueError(f'{name} must be an integer >= {minimum}')
        if not callable(clock):
            raise ValueError('clock must be callable')
        try:
            self.directory = Path(directory).absolute()
        except (TypeError, ValueError) as exc:
            raise ValueError('directory must be a path') from exc
        self._check_directory(missing_ok=True)
        self.directory.mkdir(parents=True, exist_ok=True)
        self._check_directory()
        self.max_events, self.max_bytes = max_events, max_bytes
        self.queue_capacity, self.clock = queue_capacity, clock
        self._lock = threading.RLock()
        self._queue = self._thread = None
        self._stop_event = threading.Event()
        self._start_time = self._last_offset = 0.0
        self._reserved_bytes = 0
        self._status = {'id': None, 'state': 'idle', 'accepted': 0, 'written': 0,
                        'dropped': 0, 'bytes': 0, 'error': None}

    def _check_directory(self, missing_ok=False):
        # Walk from root toward the leaf; never resolve away a linked component.
        for path in [*reversed(self.directory.parents), self.directory]:
            try:
                info = path.lstat()
            except FileNotFoundError:
                if missing_ok:
                    continue
                raise
            if _linked(info) or not stat.S_ISDIR(info.st_mode):
                raise ValueError('capture directory cannot contain links, junctions or non-directories')

    def _safe_path(self, recording_id, suffix='.jsonl'):
        if not isinstance(recording_id, str) or not re.fullmatch(r'[0-9a-f]{32}', recording_id):
            raise ValueError('recording_id must be 32 lowercase hexadecimal characters')
        if suffix not in ('.jsonl', '.part'):
            raise ValueError('invalid recording suffix')
        self._check_directory()
        path = self.directory / (recording_id + suffix)
        try:
            info = path.lstat()
        except FileNotFoundError:
            return path
        if _linked(info) or not stat.S_ISREG(info.st_mode):
            raise ValueError('recording must be a regular file, without links or junctions')
        return path

    def _open_part(self, path):
        """Exclusive binary creation; overridable only for disk-fault tests."""
        path = Path(path)
        if path != self._safe_path(path.stem, '.part'):
            raise ValueError('path does not match the capture location')
        return path.open('xb')

    def _require_idle(self):
        if (self._status['state'] in {'active', 'stopping'}
                or (self._thread is not None and self._thread.is_alive())):
            raise RuntimeError('a previous recording is active or its writer is still stopping')

    def prepare_start(self):
        """Check paths and allocate an ID in the HTTP worker, outside observation locks."""
        with self._lock:
            self._require_idle()
        for _ in range(8):
            recording_id = uuid.uuid4().hex
            part = self._safe_path(recording_id, '.part')
            final = self._safe_path(recording_id)
            if not part.exists() and not final.exists():
                return _CapturePreparation(self, recording_id)
        raise RuntimeError('could not allocate an unused recording ID')

    def start(self, metadata, preparation=None):
        """Activate using a prepared allocation without filesystem calls on this thread."""
        with self._lock:
            self._require_idle()
        if not isinstance(metadata, dict):
            raise ValueError('metadata must be a dictionary')
        encoded_meta = _encode_record(metadata)
        if len(encoded_meta) > MAX_METADATA_BYTES:
            raise ValueError('metadata exceeds 8192 bytes')
        copied_meta = json.loads(encoded_meta)
        preparation = self.prepare_start() if preparation is None else preparation
        if (not isinstance(preparation, _CapturePreparation) or preparation.owner is not self
                or preparation.used
                or preparation.options != (self.max_events, self.max_bytes, self.queue_capacity)
                or not isinstance(preparation.recording_id, str)
                or not re.fullmatch(r'[0-9a-f]{32}', preparation.recording_id)):
            raise ValueError('capture preparation is invalid, consumed, or belongs to different options')
        recording_id = preparation.recording_id
        try:
            started = _number(self.clock())
        except Exception as exc:
            raise ValueError('clock must return a finite number') from exc
        header = _encode_record({'type': 'header', 'schema': 1, 'id': recording_id,
                                 'metadata': copied_meta})
        if len(header) + FOOTER_RESERVE > self.max_bytes:
            raise ValueError('header and footer exceed the recording byte budget')
        with self._lock:
            self._require_idle()
            if preparation.used:
                raise ValueError('capture preparation was already consumed')
            preparation.used = True
            self._status = {'id': recording_id, 'state': 'active', 'accepted': 0,
                            'written': 0, 'dropped': 0, 'bytes': 0, 'error': None}
            self._start_time, self._last_offset = started, 0.0
            self._reserved_bytes = len(header) + FOOTER_RESERVE
            self._queue = queue.Queue(maxsize=self.queue_capacity)
            self._stop_event = threading.Event()
            self._thread = threading.Thread(target=self._writer,
                                            args=(recording_id, header), daemon=True)
            try:
                self._thread.start()
            except Exception as exc:
                self._status.update(state='error', error=str(exc)[:255])
                raise RuntimeError('could not start recording worker') from exc
            return dict(self._status)

    def submit(self, direction, line):
        """Validate and enqueue bounded data; never access disk or wait for space."""
        with self._lock:
            if self._status['state'] != 'active':
                return False
            try:
                if not _line_valid(direction, line):
                    raise ValueError('invalid capture line')
                offset = _number(_number(self.clock()) - self._start_time)
                if offset < 0 or offset < self._last_offset:
                    raise ValueError('clock moved backward')
                if self._status['accepted'] >= self.max_events:
                    raise ValueError('event limit reached')
                encoded = _encode_record({'type': 'event', 'offset_s': offset,
                                          'direction': direction, 'line': line})
                if self._reserved_bytes + len(encoded) > self.max_bytes:
                    raise ValueError('byte limit reached')
                self._queue.put_nowait(encoded)
            except Exception:
                self._status['dropped'] += 1
                return False
            self._status['accepted'] += 1
            self._reserved_bytes += len(encoded)
            self._last_offset = offset
            return True

    def _writer(self, recording_id, header):
        def write_record(stream, encoded, event=False):
            if stream.write(encoded) != len(encoded):
                raise OSError('short capture write')
            with self._lock:
                self._status['bytes'] += len(encoded)
                self._status['written'] += int(event)

        try:
            part = self._safe_path(recording_id, '.part')
            final = self._safe_path(recording_id)
            if final.exists():
                raise FileExistsError('final recording already exists')
            with self._open_part(part) as stream:
                write_record(stream, header)
                while True:
                    try:
                        encoded = self._queue.get(timeout=0.05)
                    except queue.Empty:
                        # A producer may have queued its last event after get()
                        # timed out but before stop(). Check both under the same
                        # lock used by submit/stop before finalizing the file.
                        with self._lock:
                            if self._stop_event.is_set() and self._queue.empty():
                                break
                        continue
                    write_record(stream, encoded, event=True)
                with self._lock:
                    totals = dict(self._status)
                final_state = 'incomplete' if totals['dropped'] else 'completed'
                footer = _encode_record({'type': 'footer', 'state': final_state,
                                         'accepted': totals['accepted'], 'written': totals['written'],
                                         'dropped': totals['dropped'], 'error': None})
                if len(footer) > FOOTER_RESERVE or totals['bytes'] + len(footer) > self.max_bytes:
                    raise ValueError('footer exceeds recording byte budget')
                write_record(stream, footer)
                stream.flush()
                os.fsync(stream.fileno())
            part = self._safe_path(recording_id, '.part')
            final = self._safe_path(recording_id)
            if final.exists():
                raise FileExistsError('final recording already exists')
            os.replace(part, final)
            with self._lock:
                self._status['state'] = final_state
        except Exception as exc:
            with self._lock:
                self._status.update(state='error', error=str(exc)[:255])

    def stop(self, timeout=5.0):
        timeout = _number(timeout)
        if not 0 <= timeout <= 5:
            raise ValueError('timeout must be between zero and five seconds')
        with self._lock:
            if self._status['state'] == 'active':
                self._status['state'] = 'stopping'
            if self._status['state'] == 'stopping':
                self._stop_event.set()
            thread = self._thread
        if thread is not None and thread.ident is not None and thread is not threading.current_thread():
            thread.join(timeout)
        return self.snapshot()

    def snapshot(self):
        with self._lock:
            return dict(self._status)

    def read_recording(self, recording_id):
        """Read validated finalized evidence. No live observer or transport exists here."""
        path = self._safe_path(recording_id)
        with path.open('rb') as source:
            raw = source.read(self.max_bytes + 1)
        if len(raw) > self.max_bytes:
            raise ValueError('recording exceeds the byte limit')
        if not raw.endswith(b'\n'):
            raise ValueError('recording is incomplete or lacks a final newline')
        try:
            # Split only the JSONL delimiter, not valid Unicode line separators.
            lines = raw.split(b'\n')[:-1]
            if not 2 <= len(lines) <= self.max_events + 2 or any(not line for line in lines):
                raise ValueError('invalid recording length')
            records = [json.loads(line.decode('utf-8'), object_pairs_hook=_unique_object) for line in lines]
            if any(not isinstance(record, dict) for record in records):
                raise ValueError('records must be JSON objects')
            header, footer = records[0], records[-1]
            if (header.get('type') != 'header' or type(header.get('schema')) is not int
                    or header['schema'] != 1 or header.get('id') != recording_id
                    or not isinstance(header.get('metadata'), dict)):
                raise ValueError('invalid recording header')
            if len(_encode_record(header['metadata'])) > MAX_METADATA_BYTES:
                raise ValueError('metadata exceeds its limit')
            events, previous = records[1:-1], 0.0
            for event in events:
                offset = _number(event['offset_s'])
                if (event.get('type') != 'event' or offset < previous or offset < 0
                        or not _line_valid(event['direction'], event['line'])):
                    raise ValueError('invalid recording event')
                previous = offset
            if footer.get('type') != 'footer' or footer.get('state') not in ('completed', 'incomplete'):
                raise ValueError('invalid recording footer')
            for field in ('accepted', 'written', 'dropped'):
                if type(footer.get(field)) is not int or footer[field] < 0:
                    raise ValueError('invalid footer counter')
            if footer['accepted'] != len(events) or footer['written'] != len(events):
                raise ValueError('footer counts do not match events')
            error = footer.get('error')
            if error is not None and (not isinstance(error, str) or len(error) > 255):
                raise ValueError('invalid footer error')
            if ((footer['state'] == 'completed' and (footer['dropped'] != 0 or error is not None))
                    or (footer['state'] == 'incomplete' and footer['dropped'] == 0)):
                raise ValueError('completion state contradicts footer')
            # Reject nonfinite values even in unrecognized optional fields.
            _encode_record(records)
        except (ValueError, TypeError, KeyError, OverflowError, UnicodeError, RecursionError) as exc:
            raise ValueError(f'invalid recording: {exc}') from exc
        return {'id': recording_id, 'metadata': header['metadata'], 'events': events,
                'state': footer['state'], 'dropped': footer['dropped'], 'error': error}

    def list_recordings(self):
        self._check_directory()
        summaries = []
        with os.scandir(self.directory) as entries:
            for entry in entries:
                if not re.fullmatch(r'[0-9a-f]{32}\.jsonl', entry.name):
                    continue
                try:
                    info = entry.stat(follow_symlinks=False)
                    if _linked(info) or not stat.S_ISREG(info.st_mode):
                        continue
                    recording_id = entry.name[:-6]
                    try:
                        bundle = self.read_recording(recording_id)
                        count = len(bundle['events'])
                        summary = {key: bundle[key] for key in ('id', 'state', 'metadata', 'dropped', 'error')}
                        summary.update(accepted=count, written=count, bytes=info.st_size)
                    except FileNotFoundError:
                        continue
                    except (ValueError, OSError) as exc:
                        summary = {'id': recording_id, 'state': 'error', 'metadata': {},
                                   'accepted': 0, 'written': 0, 'dropped': 0,
                                   'bytes': info.st_size, 'error': str(exc)[:255]}
                    summaries.append(summary)
                except FileNotFoundError:
                    continue
        return sorted(summaries, key=lambda item: item['id'])
