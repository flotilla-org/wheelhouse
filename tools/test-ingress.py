#!/usr/bin/env python3
"""Exercise real HTTP/UDS ingress and the real core C decoder."""
import concurrent.futures
import ctypes as C
import faulthandler
import http.client
import json
import importlib.util
import uuid
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import workspace_seed

# A crash inside a ctypes call (#336) prints every thread's Python stack.
faulthandler.enable()
ROOT = Path(__file__).resolve().parents[1]
LIBDIR = Path(sys.argv.pop(1))
WINDOWS = sys.platform == 'win32'
SUFFIX = '.dll' if WINDOWS else '.dylib' if sys.platform == 'darwin' else '.so'
PREFIX = '' if WINDOWS else 'lib'
lib = C.CDLL(str(LIBDIR / (PREFIX + 'wheelhouse_ingress' + SUFFIX)))
core = C.CDLL(str(LIBDIR / (PREFIX + 'wheelhouse_ffi' + SUFFIX)))
class UnixHTTPConnection(http.client.HTTPConnection):
    def __init__(self, path):
        super().__init__('localhost', timeout=7)
        self.path = path

    def connect(self):
        if WINDOWS:
            self.sock = PipeSocket(self.path, self.timeout)
            return
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect(self.path)


if WINDOWS:
    spec = importlib.util.spec_from_file_location('ingress_pipe', ROOT / 'tools/ingress-pipe.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    PipeSocket = module.PipeSocket


def wait_ready(path, timeout=10, process=None):
    """Health is the readiness contract, including after listener restart."""
    deadline = time.monotonic() + timeout
    while True:
        if process is not None and process.poll() is not None:
            raise RuntimeError(f'native ingress process exited with {process.returncode}')
        connection = UnixHTTPConnection(path)
        try:
            connection.request('GET', '/v1/health')
            response = connection.getresponse()
            response.read()
            if response.status == 204:
                return
        except OSError:
            if time.monotonic() >= deadline:
                raise
        finally:
            connection.close()
        if time.monotonic() >= deadline:
            raise TimeoutError('ingress health handshake timed out')
        time.sleep(.01)


def alive(pid):
    """Whether `pid` names a process that has not been reaped."""
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def patch(kind, identity, facts):
    return {'type': 'metadata-patch', 'target': {'kind': 'entity', 'value': {'kind': kind, 'id': identity}},
            'source_id': 'ingress.test', 'set': {
                key: {'value': {'type': 'text', 'value': value}}
                for key, value in {'entity.kind': kind, 'entity.id': identity, **facts}.items()}, 'unset': []}


def publish(path, value):
    connection = UnixHTTPConnection(path)
    try:
        connection.request('POST', '/v1/metadata/patch', json.dumps(value), {'Content-Type': 'application/json'})
        response = connection.getresponse()
        response.read()
        if response.status != 204:
            raise AssertionError(f'patch failed: HTTP {response.status}')
    finally:
        connection.close()

WAKE = C.CFUNCTYPE(None)
APPLY = C.CFUNCTYPE(C.c_uint32, C.c_void_p, C.c_void_p, C.c_size_t)
class Text(C.Structure):
    _fields_ = [('data', C.c_void_p), ('len', C.c_size_t)]
class Workdir(C.Structure):
    _fields_ = [('workspace_id', C.c_uint64), ('view_id', C.c_uint64)] + [
        (key, Text) for key in ['entity_kind', 'entity_id', 'cwd', 'live_cwd']]
EMIT = C.CFUNCTYPE(None, C.c_void_p, C.POINTER(Workdir))
OBSERVE = C.CFUNCTYPE(C.c_uint32, C.c_void_p, EMIT, C.c_void_p)
RETRACT = C.CFUNCTYPE(C.c_uint32, C.c_void_p)
lib.wheelhouse_ingress_poll_observed.argtypes = [C.c_void_p, APPLY, RETRACT, OBSERVE, C.c_void_p]
lib.wheelhouse_ingress_start.argtypes = [C.c_char_p, C.c_size_t, WAKE, C.c_void_p, C.c_size_t]
lib.wheelhouse_ingress_start.restype = C.c_void_p
lib.wheelhouse_ingress_poll.argtypes = [C.c_void_p, APPLY, RETRACT, C.c_void_p]
lib.wheelhouse_ingress_stop.argtypes = [C.c_void_p]
core.wheelhouse_create.argtypes = [C.c_char_p, C.c_size_t, C.c_void_p]
core.wheelhouse_create.restype = C.c_void_p
core.wheelhouse_destroy.argtypes = [C.c_void_p]
core.wheelhouse_provider_retract.argtypes = [C.c_void_p, Text, C.c_void_p]
core.wheelhouse_provider_retract.restype = C.c_uint32
core.wheelhouse_apply_patch_json.argtypes = [C.c_void_p, C.c_uint64, Text, Text, C.c_void_p]
core.wheelhouse_apply_patch_json.restype = C.c_uint32
core.wheelhouse_tick.argtypes = [C.c_void_p, C.c_uint64, C.c_void_p]
core.wheelhouse_snapshot_acquire.argtypes = [C.c_void_p, C.c_void_p]
core.wheelhouse_snapshot_acquire.restype = C.c_void_p
core.wheelhouse_snapshot_node_count.argtypes = [C.c_void_p]
core.wheelhouse_snapshot_node_count.restype = C.c_size_t
core.wheelhouse_snapshot_release.argtypes = [C.c_void_p]

# Patches the local endpoint receives are the host's own provider's.
PROVIDER_NAME = b'wheelhouse'
PROVIDER = Text(C.cast(C.c_char_p(PROVIDER_NAME), C.c_void_p), len(PROVIDER_NAME))
core.wheelhouse_abi_version.restype = C.c_uint32
assert core.wheelhouse_abi_version() == 18

lib.wheelhouse_ingress_start_recorded.argtypes = [C.c_char_p, C.c_size_t, WAKE,
    C.c_char_p, C.c_size_t, C.c_uint64, C.c_size_t, C.c_void_p, C.c_size_t]
lib.wheelhouse_ingress_start_recorded.restype = C.c_void_p

class IngressTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory(prefix='wh-', dir=None if WINDOWS else '/tmp')
        self.path = r'\\.\pipe\wheelhouse-test-' + uuid.uuid4().hex if WINDOWS else os.path.join(self.dir.name, 'facts.sock')
        self.wake = WAKE(lambda: None)
        config = (ROOT / 'data/sidebar/fixture.kdl').read_bytes()
        self.core = core.wheelhouse_create(config, len(config), None)
        self.assertTrue(self.core)
        self.received = []
        def apply(_, data, size):
            self.received.append(C.string_at(data, size))
            return core.wheelhouse_apply_patch_json(self.core, 0, PROVIDER, Text(data, size), None)
        self.apply = APPLY(apply)
        self.retracts = 0
        def retract(_):
            self.retracts += 1
            return core.wheelhouse_provider_retract(self.core, PROVIDER, None)
        self.retract = RETRACT(retract)
        self.workdirs = []
        self.observed = 0
        self.available = True
        def observe(_, emit, context):
            self.observed += 1
            for workspace_id, view_id, *strings in self.workdirs:
                buffers = [C.create_string_buffer(value.encode()) if value else None for value in strings]
                texts = [Text(C.cast(buf, C.c_void_p), len(buf.value)) if buf else Text() for buf in buffers]
                emit(context, C.byref(Workdir(workspace_id, view_id, *texts)))
            return int(self.available)
        self.observe = OBSERVE(observe)
        self.server = self.start()
        self.pool = concurrent.futures.ThreadPoolExecutor(max_workers=2)

    def start(self):
        error = C.create_string_buffer(512)
        server = lib.wheelhouse_ingress_start(self.path.encode(), len(self.path.encode()), self.wake, error, len(error))
        self.assertTrue(server, error.value)
        wait_ready(self.path)
        return server

    def tearDown(self):
        lib.wheelhouse_ingress_stop(self.server)
        self.pool.shutdown()
        core.wheelhouse_destroy(self.core)
        self.dir.cleanup()

    def request(self, body=b'', content_type='application/json', chunked=False, poll=True, read=False, path='/v1/metadata/patch', refused=False):
        def send():
            connection = UnixHTTPConnection(self.path)
            try:
                payload = iter([body[:5], body[5:]]) if chunked else body
                try:
                    connection.request('GET' if read else 'POST', '/v1/observed/workdirs' if read else path, payload, {'Content-Type': content_type}, encode_chunked=chunked)
                except (BrokenPipeError, ConnectionResetError):
                    # A server refusing a body may answer and close before
                    # reading it all. Its answer can still be on the socket,
                    # or the reset can have discarded it.
                    if not refused:
                        raise
                    try:
                        response = http.client.HTTPResponse(connection.sock)
                        response.begin()
                        return response.status
                    except (OSError, http.client.HTTPException):
                        return 'closed'
                response = connection.getresponse()
                data = response.read()
                return (response.status, data, response.getheader("Content-Type")) if read else response.status
            finally:
                connection.close()
        future = self.pool.submit(send)
        deadline = time.monotonic() + 8
        while not future.done() and time.monotonic() < deadline:
            if poll:
                lib.wheelhouse_ingress_poll_observed(self.server, self.apply, self.retract, self.observe, None)
            time.sleep(.005)
        return future.result(timeout=1)

    def enable_recording(self, path, size=4*1024*1024, files=4):
        lib.wheelhouse_ingress_stop(self.server)
        self.server = None
        error = C.create_string_buffer(512)
        encoded = str(path).encode()
        self.server = lib.wheelhouse_ingress_start_recorded(self.path.encode(), len(self.path.encode()),
            self.wake, encoded, len(encoded), size, files, error, len(error))
        self.assertTrue(self.server, error.value)
        wait_ready(self.path)

    def capture(self):
        lib.wheelhouse_ingress_stop(self.server)
        self.server = None
        return [json.loads(line) for line in (Path(self.dir.name) / 'ingress.jsonl').read_text().splitlines()]

    def test_recording_off_creates_no_files(self):
        # Issue #223: ordinary ingress never creates recording files.
        self.assertEqual(self.request(json.dumps(patch('project', 'p', {})).encode()), 204)
        self.assertFalse(list(Path(self.dir.name).glob('ingress.jsonl*')))

    def test_recorded_fixture_replays_to_identical_sidebar(self):
        # Issue #223: pm-connect and git-watcher style sources retain byte-exact
        # messages and replay every intermediate state through the shipped ABI.
        spec = importlib.util.spec_from_file_location('replay_ingress', ROOT / 'tools/replay-ingress.py')
        replay = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(replay)
        core.wheelhouse_destroy(self.core)
        config = (ROOT / 'data/sidebar/daily-driver.kdl').read_bytes()
        self.core = core.wheelhouse_create(config, len(config), None)
        self.assertTrue(self.core)
        self.enable_recording(Path(self.dir.name) / 'ingress.jsonl')
        fixtures = []
        for index, line in enumerate((ROOT / 'data/sidebar/fixture.jsonl').read_text().splitlines()):
            item = json.loads(line)
            item['type'] = 'metadata-patch'
            item['source_id'] = 'andamento-git-watcher' if any(k.startswith('git.') for k in item['set']) else 'pm-connect'
            fixtures.append(item)
            body = json.dumps(item, ensure_ascii=False, indent=1).encode()
            self.assertEqual(self.request(body), 204)
            self.assertEqual(self.received[-1], body)
        if not WINDOWS:
            # Real producer subprocess boundary: it uses git and curl against our
            # actual Unix endpoint while the same real ABI host drains requests.
            watcher = LIBDIR / 'wheelhouse-git-watcher'
            self.assertTrue(watcher.is_file(), 'build wheelhouse-git-watcher alongside the ABI')
            future = self.pool.submit(subprocess.run, [str(watcher.resolve()), '--transport', 'wheelhouse',
                '--socket', self.path, '--roots', str(ROOT), '--once'], capture_output=True, timeout=20)
            deadline = time.monotonic() + 20
            while not future.done() and time.monotonic() < deadline:
                lib.wheelhouse_ingress_poll_observed(self.server, self.apply, self.retract, self.observe, None)
                time.sleep(.005)
            completed = future.result(timeout=1)
            self.assertEqual(completed.returncode, 0, completed.stderr)
            fixtures = [json.loads(body) for body in self.received]
            self.assertTrue(any(p['source_id'] == 'andamento-git-watcher' for p in fixtures))
        records = self.capture()
        patches = [r for r in records if r['path'] == '/v1/metadata/patch']
        self.assertEqual([r['body'] for r in patches], fixtures)
        self.assertEqual([r['producer'] for r in patches], [p['source_id'] for p in fixtures])
        self.assertEqual([r['body_utf8'].encode() for r in patches], self.received)
        library = LIBDIR / (PREFIX + 'wheelhouse_ffi' + SUFFIX)
        direct = list(replay.replay(fixtures, library))
        captured = list(replay.replay(records, library))
        self.assertEqual([r['nodes'] for r in captured], [r['nodes'] for r in direct])
        abi, native = replay.native_api(library)
        self.assertEqual(captured[-1]['nodes'], replay.snapshot_nodes(abi, native, self.core))
        self.assertTrue(any(n['kind'] == 'role' for n in captured[-1]['nodes']))
        self.assertTrue(any(n['kind'] == 'convoy' for n in captured[-1]['nodes']))
        redacted = list(replay.replay(list(replay.redact(records)), library))
        identity = lambda frames: [sorted((n['kind'], n['id'], n['state']) for n in f['nodes']) for f in frames]
        self.assertEqual(identity(redacted), identity(captured))
        # A listener restart begins with fresh state and resets monotonic time.
        update = patch('project', 'fresh', {'flotilla.project': 'fresh'})
        restarted = dict(recording_id='next-run', sequence=0, timestamp_ms=0,
                         received_ms=0, path='/v1/metadata/patch', status=204,
                         body=update, body_utf8=json.dumps(update))
        combined = list(replay.replay(records + [restarted], library))
        alone = list(replay.replay([restarted], library))
        self.assertEqual(combined[-1]['nodes'], alone[-1]['nodes'])

    def test_recording_rotation_bounds_at_http_boundary(self):
        # Issue #223: generate varying entry sizes and retention counts through
        # the real endpoint; every retained line is complete and total size bounded.
        for files in (1, 2, 4):
            path = Path(self.dir.name) / f'rotation-{files}.jsonl'
            limit = 4096
            self.enable_recording(path, size=limit, files=files)
            last = None
            for index in range(20):
                last = patch('project', 'p', {'display.label': 'x' * (index % 5 * 64) + '/東京'})
                self.assertEqual(self.request(json.dumps(last).encode()), 204)
            lib.wheelhouse_ingress_stop(self.server)
            self.server = None
            paths = list(Path(self.dir.name).glob(f'rotation-{files}.jsonl*'))
            self.assertEqual(len(paths), files)
            self.assertLessEqual(sum(p.stat().st_size for p in paths), limit * files)
            entries = []
            for recording in paths:
                self.assertLessEqual(recording.stat().st_size, limit)
                if not WINDOWS:
                    self.assertEqual(recording.stat().st_mode & 0o777, 0o600)
                data = recording.read_bytes()
                self.assertTrue(data.endswith(b'\n'))
                entries.extend(json.loads(line) for line in data.splitlines())
            self.assertEqual(max(entries, key=lambda r: r['sequence'])['body'], last)

    def test_body_limit_is_identical_with_recording_on_and_off(self):
        # Review #226: the same 1 MiB decoded-body limit applies before capture
        # and before the ordinary handler, at below/exact/above boundaries.
        limit = 1024 * 1024
        message = json.dumps(patch('project', 'boundary', {})).encode()
        for enabled in (False, True):
            if enabled:
                self.enable_recording(Path(self.dir.name) / 'ingress.jsonl')
            statuses = [self.request(message + b' ' * (size - len(message)))
                        for size in (limit - 1, limit, limit + 1)]
            self.assertEqual(statuses, [204, 204, 413])
            self.assertEqual(self.request(message), 204)
        entries = [r for r in self.capture() if r['path'] == '/v1/metadata/patch']
        self.assertEqual([r['status'] for r in sorted(entries, key=lambda r: r['sequence'])], [204, 204, 413, 204])
        rejected = next(r for r in entries if r['status'] == 413)
        self.assertIsNone(rejected['body'])
        self.assertIsNone(rejected['body_utf8'])

    def test_recording_preserves_error_statuses_and_write_failure(self):
        # Issue #223: recording preserves rejection/unavailability statuses;
        # even an impossible output path leaves healthy requests usable.
        path = Path(self.dir.name) / 'ingress.jsonl'
        self.enable_recording(path)
        bodies = [b'broken', b'{}', json.dumps(patch('project', 'p', {})).encode()]
        expected = [400, 400, 415]
        for body, status in zip(bodies, expected):
            self.assertEqual(self.request(body, content_type='text/plain' if status == 415 else 'application/json'), status)
        invalid = {'type': 'metadata-patch'}
        self.assertEqual(self.request(json.dumps(invalid).encode()), 422)
        self.assertEqual(self.request(b'x' * (1024*1024 + 1)), 413)
        self.assertEqual(self.request(json.dumps(patch('project', 'p', {})).encode(), poll=False), 503)
        records = [r for r in self.capture() if r['path'] == '/v1/metadata/patch']
        self.assertEqual([r['status'] for r in sorted(records, key=lambda r: r['sequence'])], expected + [422, 413, 503])
        self.enable_recording(Path(self.dir.name)) # a directory cannot be opened as a log
        self.assertEqual(self.request(json.dumps(patch('project', 'p', {})).encode()), 204)

    def test_observed_directories_change_and_omit_unknown_views(self):
        self.workdirs = [
            (42, 81, 'worktree', 'one', '/saved/"quoted"/東京', None),
            (42, 82, 'worktree', 'one', '/saved/two', '/live/two'),
            (43, 83, None, None, None, None),
        ]
        status, body, content_type = self.request(read=True)
        self.assertEqual(status, 200)
        self.assertEqual(content_type, 'application/json')
        self.assertEqual(json.loads(body), {'workdirs': [
            dict(workspace_id=42, view_id=81, entity_kind='worktree', entity_id='one', cwd='/saved/"quoted"/東京', live_cwd=None),
            dict(workspace_id=42, view_id=82, entity_kind='worktree', entity_id='one', cwd='/saved/two', live_cwd='/live/two'),
        ]})
        self.workdirs[1] = (42, 82, 'worktree', 'one', '/saved/two', '/changed')
        self.assertEqual(json.loads(self.request(read=True)[1])['workdirs'][1]['live_cwd'], '/changed')
        self.workdirs = [(43, 83, None, None, None, '/live/only')]
        row = json.loads(self.request(read=True)[1])['workdirs'][0]
        self.assertIsNone(row['cwd'])
        self.assertIsNone(row['entity_id'])
        self.workdirs = []
        self.assertEqual(json.loads(self.request(read=True)[1]), {'workdirs': []})
        self.assertEqual(self.received, [])

    def test_invalid_utf8_does_not_invent_a_directory(self):
        raw = C.create_string_buffer(b'/bad/\xff')
        def invalid(_, emit, context):
            emit(context, C.byref(Workdir(1, 2, Text(), Text(),
                 Text(C.cast(raw, C.c_void_p), len(raw.value)), Text())))
            return 1
        self.observe = OBSERVE(invalid)
        self.assertEqual(self.request(read=True)[0], 503)

    def test_concurrent_http_read_burst(self):
        def read():
            connection = UnixHTTPConnection(self.path)
            try:
                connection.request('GET', '/v1/observed/workdirs')
                response = connection.getresponse()
                return response.status, json.loads(response.read())
            finally:
                connection.close()
        requests = [self.pool.submit(read) for _ in range(8)]
        deadline = time.monotonic() + 5
        while not all(future.done() for future in requests) and time.monotonic() < deadline:
            lib.wheelhouse_ingress_poll_observed(self.server, self.apply, self.retract, self.observe, None)
            time.sleep(.005)
        for future in requests:
            self.assertEqual(future.result(timeout=1), (200, {'workdirs': []}))

    def test_read_timeout_cancellation_and_unavailability(self):
        self.assertEqual(self.request(read=True, poll=False)[0], 503)
        lib.wheelhouse_ingress_poll_observed(self.server, self.apply, self.retract, self.observe, None)
        self.assertEqual(self.observed, 0)
        self.available = False
        self.assertEqual(self.request(read=True)[0], 503)

    def count(self):
        snapshot = core.wheelhouse_snapshot_acquire(self.core, None)
        self.assertTrue(snapshot)
        count = core.wheelhouse_snapshot_node_count(snapshot)
        core.wheelhouse_snapshot_release(snapshot)
        return count

    def test_patch_duplicate_withdrawal_and_chunked_http(self):
        before = self.count()
        body = json.dumps(patch('project', 'p', {'display.label': 'Live project', 'flotilla.project': 'p'})).encode()
        self.assertEqual(self.request(body), 204)
        after = self.count()
        self.assertGreater(after, before)
        self.assertEqual(self.request(body, chunked=True), 204)
        self.assertEqual(self.count(), after)
        self.assertEqual(self.received, [body, body])
        # An entity's facts don't expire; its producer withdraws them.
        self.assertEqual(core.wheelhouse_tick(self.core, 600_000, None), 1)
        self.assertEqual(self.count(), after)
        withdrawal = dict(json.loads(body), set={}, unset=['display.label', 'flotilla.project', 'entity.kind', 'entity.id'])
        self.assertEqual(self.request(json.dumps(withdrawal).encode()), 204)
        self.assertEqual(self.count(), before)

    def test_an_array_is_applied_in_one_drain(self):
        # flotilla pm connect posts each publication's patches as one array (#378).
        before = self.count()
        items = [patch('project', name, {'display.label': name, 'flotilla.project': name}) for name in ('p', 'q')]
        self.assertEqual(self.request(json.dumps(items).encode()), 204)
        self.assertEqual([json.loads(body)['target']['value']['id'] for body in self.received], ['p', 'q'])
        self.assertGreater(self.count(), before)
        self.assertEqual(self.request(json.dumps([items[0], {'type': 'other'}]).encode()), 400)

    def test_a_snapshot_replaces_the_providers_facts(self):
        before = self.count()
        project = lambda name: patch('project', name, {'display.label': name, 'flotilla.project': name})
        self.assertEqual(self.request(json.dumps(project('old')).encode()), 204)
        with_old = self.count()
        self.assertGreater(with_old, before)
        self.assertEqual(self.request(json.dumps([project('new')]).encode(), path='/v1/metadata/snapshot'), 204)
        self.assertEqual(self.retracts, 1)
        self.assertEqual(self.count(), with_old)
        self.assertEqual(self.request(b'[]', path='/v1/metadata/snapshot'), 204)
        self.assertEqual(self.count(), before)
        # A snapshot is an array, and an endpoint without a retract refuses it.
        self.assertEqual(self.request(json.dumps(project('new')).encode(), path='/v1/metadata/snapshot'), 400)
        self.retract = RETRACT()  # NULL
        self.assertEqual(self.request(b'[]', path='/v1/metadata/snapshot'), 404)

    def test_large_snapshot_with_recording_on_and_off(self):
        # A full Flotilla catalog can exceed the patch endpoint's 1 MiB limit.
        message = patch('project', 'large-catalog', {
            'display.label': 'Large catalog', 'flotilla.project': 'large-catalog',
            'summary.text': 'x' * (1024 * 1024),
        })
        body = json.dumps([message]).encode()
        oversized = body + b' ' * (16 * 1024 * 1024 + 1 - len(body))
        for enabled in (False, True):
            if enabled:
                self.enable_recording(Path(self.dir.name) / 'ingress.jsonl', size=8*1024*1024)
            before = self.retracts
            self.assertEqual(self.request(body, chunked=enabled, path='/v1/metadata/snapshot'), 204)
            self.assertEqual(self.retracts, before + 1)
            self.assertEqual(json.loads(self.received[-1]), message)
            self.assertIn(self.request(oversized, chunked=enabled, path='/v1/metadata/snapshot', refused=True), (413, 'closed'))
            self.assertEqual(self.retracts, before + 1)
            self.assertEqual(self.request(b'[]', path='/v1/metadata/snapshot'), 204)
        entries = [r for r in self.capture() if r['path'] == '/v1/metadata/snapshot']
        self.assertEqual([r['status'] for r in entries], [204, 413, 204])
        self.assertEqual(entries[0]['body'], [message])
        self.assertEqual(entries[0]['body_utf8'].encode(), body)
        self.assertIsNone(entries[1]['body'])

    def test_http_and_schema_errors(self):
        self.assertEqual(self.request(b'not json'), 400)
        self.assertEqual(self.request(b'{}'), 400)
        self.assertEqual(self.request(b'{}', 'text/plain'), 415)
        self.assertEqual(self.request(b'{"type":"metadata-patch"}'), 422)
        self.assertEqual(self.request(b'x' * (1024 * 1024 + 1)), 413)

    def test_ack_waits_for_application_and_cancelled_request_is_skipped(self):
        body = json.dumps(patch('project', 'p', {'display.label': 'late'})).encode()
        self.assertEqual(self.request(body, poll=False), 503)
        lib.wheelhouse_ingress_poll_observed(self.server, self.apply, self.retract, self.observe, None)
        self.assertEqual(self.received, [])

    @unittest.skipIf(WINDOWS, 'native sidebar/producer fixture uses Unix terminals; pipe HTTP contract runs on Windows')
    @unittest.skipUnless(os.environ.get('WHEELHOUSE_TEST_BINARY'), 'native executable not selected')
    def test_native_process_applies_producer_patch(self):
        path = os.path.join(self.dir.name, 'native.sock')
        binary = str(Path(os.environ['WHEELHOUSE_TEST_BINARY']).resolve())
        # Windows are a Dashboard's Presentation State, kept beside an explicit
        # --user file; the workspace's tabs are its record's, in the Dashboard.
        Path(self.dir.name, 'dashboard').mkdir()
        Path(self.dir.name, 'dashboard', 'id').write_text('ingress')
        workspace_seed.seed(Path(self.dir.name, 'dashboard'), Path(self.dir.name, 'presentation', 'ingress.wheelhouse'),
                            [workspace_seed.terminal('read answer', '/tmp'), workspace_seed.terminal('read answer', self.dir.name),
                             workspace_seed.terminal('read answer')], label='Observed')
        with tempfile.TemporaryFile() as log:
            process = subprocess.Popen([binary, '--user:' + self.dir.name + '/user',
                                        '--dashboard:' + self.dir.name + '/dashboard',
                                        '--project:' + self.dir.name + '/project',
                                        '--andamento_socket:' + path,
                                        '--andamento_config:' + str(ROOT / 'data/sidebar/fixture.kdl'),
                                        '--ingress_record', '--ingress_record_bytes:1048576',
                                        '--ingress_record_files:2'],
                                       stdout=log, stderr=log)
            try:
                try:
                    wait_ready(path, timeout=20, process=process)
                except (OSError, TimeoutError, RuntimeError):
                    log.seek(0)
                    self.fail('native listener failed: ' + log.read().decode(errors='replace'))
                connection = UnixHTTPConnection(path)
                try:
                    connection.request('GET', '/v1/observed/workdirs')
                    response = connection.getresponse()
                    self.assertEqual(response.status, 200)
                    rows = json.loads(response.read())['workdirs']
                    self.assertEqual({row['cwd'] for row in rows}, {'/tmp', self.dir.name})
                    self.assertEqual(len(rows), 2)
                    self.assertEqual(len({row['view_id'] for row in rows}), 2)
                    self.assertEqual(len({row['workspace_id'] for row in rows}), 1)
                    self.assertTrue(all(row['live_cwd'] is None and row['entity_id'] is None for row in rows))
                finally:
                    connection.close()
                if os.environ.get('WHEELHOUSE_TEST_WATCHER'):
                    subprocess.run([os.environ['WHEELHOUSE_TEST_WATCHER'], '--transport', 'wheelhouse',
                                    '--socket', path, '--roots', str(ROOT), '--once'],
                                   check=True, timeout=30, stdout=log, stderr=log)
                publish(path, patch('project', 'native', {
                    'display.label': 'Native HTTP project', 'flotilla.project': 'native'}))
                connection = UnixHTTPConnection(path)
                try:
                    connection.request('POST', '/v1/metadata/patch', b'{"type":"metadata-patch"}',
                                       {'Content-Type': 'application/json'})
                    response = connection.getresponse()
                    response.read()
                    self.assertEqual(response.status, 422)
                finally:
                    connection.close()
                # Let the application become idle, then prove background wakeup.
                time.sleep(1)
                publish(path, patch('project', 'native', {
                    'display.label': 'Updated while idle', 'flotilla.project': 'native'}))
                # Issue #223: the real native flag writes beside the isolated UI log.
                capture_path = Path(self.dir.name) / 'logs/ingress.jsonl'
                deadline = time.monotonic() + 2
                captured = []
                while time.monotonic() < deadline:
                    try:
                        captured = [json.loads(line) for line in capture_path.read_text().splitlines()]
                    except (FileNotFoundError, json.JSONDecodeError):
                        pass
                    if any(r.get('body', {}).get('set', {}).get('display.label', {}).get('value', {}).get('value') ==
                           'Updated while idle' for r in captured if isinstance(r.get('body'), dict)):
                        break
                    time.sleep(.01)
                self.assertTrue(captured)
                self.assertTrue(any(r.get('body', {}).get('set', {}).get('display.label', {}).get('value', {}).get('value') ==
                                    'Updated while idle' for r in captured if isinstance(r.get('body'), dict)))
                self.assertTrue(any(r['status'] == 422 for r in captured))
                self.assertTrue(any(r['producer'] == 'ingress.test' and r['status'] == 204 for r in captured))
                self.assertTrue((capture_path.parent / 'ui_thread.uishell_log').is_file())
                recordings = list(capture_path.parent.glob('ingress.jsonl*'))
                self.assertLessEqual(len(recordings), 2)
                self.assertLessEqual(sum(p.stat().st_size for p in recordings), 2*1048576)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    def launch_nested(self, name, *args, automation=None):
        """Wheelhouse launched from a terminal of another: WHEELHOUSE_SOCKET
        names the outer one's endpoint. Its one terminal writes the
        WHEELHOUSE_SOCKET it sees. Returns the address Wheelhouse wrote to
        --automation_socket_file and what the terminal saw, once it quits."""
        binary = str(Path(os.environ['WHEELHOUSE_TEST_BINARY']).resolve())
        root = Path(self.dir.name, name)
        dashboard, seen, address_file = root / 'dashboard', root / 'seen', root / 'address'
        dashboard.mkdir(parents=True)
        (dashboard / 'id').write_text(name)
        workspace_seed.seed(dashboard, root / 'presentation' / (name + '.wheelhouse'), [workspace_seed.terminal(
            'printf %%s "${WHEELHOUSE_SOCKET-unset}" > %s.tmp && mv %s.tmp %s; read answer' % (seen, seen, seen))])
        if automation:
            (root / 'user').write_text('// uishell 0.1.0 user file\n\nautomation: "%s"\n' % automation)
        log = tempfile.TemporaryFile()
        self.addCleanup(log.close)
        process = subprocess.Popen([binary, '--user:%s/user' % root, '--dashboard:%s' % dashboard, '--project:%s/project' % root,
                                    '--automation_socket_file:%s' % address_file, *args],
                                   stdout=log, stderr=log, env={**os.environ, 'WHEELHOUSE_SOCKET': '/outer/wheelhouse'})

        def failure(message):
            log.seek(0)
            return message + ': ' + log.read().decode(errors='replace')
        try:
            deadline = time.monotonic() + 20
            while not (address_file.exists() and seen.exists()):
                self.assertIsNone(process.poll(), failure('Wheelhouse exited'))
                self.assertLess(time.monotonic(), deadline, failure('the address file or the terminal\'s report never came'))
                time.sleep(.05)
            address = address_file.read_text()
            if address:
                wait_ready(address, timeout=5, process=process)
        finally:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if address:
            self.assertFalse(os.path.exists(address), 'the endpoint outlived Wheelhouse')
        return address, seen.read_text()

    @unittest.skipIf(WINDOWS, 'native sidebar/producer fixture uses Unix terminals; pipe HTTP contract runs on Windows')
    @unittest.skipUnless(os.environ.get('WHEELHOUSE_TEST_BINARY'), 'native executable not selected')
    @unittest.skipIf(os.environ.get('WHEELHOUSE_CLEAT_FEATURES') == 'none', 'terminals run no program without a VT engine')
    def test_a_nested_instance_tells_its_terminals_its_own_endpoint(self):
        # By default each instance listens at its own address under the
        # runtime directory, and its terminals see that, not the outer one.
        address, seen = self.launch_nested('default')
        runtime = os.environ.get('XDG_RUNTIME_DIR')
        parent = Path(runtime, 'wheelhouse') if runtime and os.path.isabs(runtime) else \
            Path(tempfile.gettempdir(), 'wheelhouse-%d' % os.geteuid())
        self.assertEqual(Path(address).parent, parent)
        uuid.UUID(Path(address).name)
        self.assertEqual(os.stat(parent).st_mode & 0o777, 0o700)
        self.assertEqual(seen, address)
        # Another instance gets another address.
        self.assertNotEqual(self.launch_nested('second')[0], address)
        # A launcher may name it.
        explicit = os.path.join(self.dir.name, 'explicit.sock')
        self.assertEqual(self.launch_nested('explicit', '--automation_socket:' + explicit), (explicit, explicit))
        # With automation off there is no default listener, and terminals are
        # told of none.
        self.assertEqual(self.launch_nested('off', automation='off'), ('', 'unset'))
        # An address the launcher names is still served, for metadata.
        explicit = os.path.join(self.dir.name, 'off.sock')
        self.assertEqual(self.launch_nested('off-explicit', '--automation_socket:' + explicit, automation='off'), (explicit, 'unset'))

    # A stand-in for `flotilla pm connect` that ignores SIGTERM, as does the
    # child it starts; it appends "<its pid> <child pid>" to $WH_TEST_CONNECTOR.
    STUBBORN_CONNECTOR = """#!/bin/sh
trap '' TERM
sleep 300 &
echo "$$ $!" >> "$WH_TEST_CONNECTOR"
wait
"""

    def start_with_connector(self):
        """Wheelhouse with one Flotilla subscription run by the stand-in;
        returns it and the connector's and its child's pids."""
        binary = str(Path(os.environ['WHEELHOUSE_TEST_BINARY']).resolve())
        flotilla = Path(self.dir.name, 'flotilla')
        flotilla.write_text(self.STUBBORN_CONNECTOR)
        flotilla.chmod(0o755)
        marker = Path(self.dir.name, 'connector.pids')
        Path(self.dir.name, 'dashboard').mkdir()
        Path(self.dir.name, 'dashboard', 'id').write_text('lifetime')
        path = os.path.join(self.dir.name, 'native.sock')
        log = tempfile.TemporaryFile()
        self.addCleanup(log.close)
        process = subprocess.Popen([binary, '--user:' + self.dir.name + '/user',
                                    '--dashboard:' + self.dir.name + '/dashboard',
                                    '--project:' + self.dir.name + '/project',
                                    '--andamento_socket:' + path,
                                    '--andamento_config:' + str(ROOT / 'data/sidebar/fixture.kdl'),
                                    '--flotilla_bin:' + str(flotilla), '--flotilla_subscription:ssh://lifetime'],
                                   stdout=log, stderr=log, env={**os.environ, 'WH_TEST_CONNECTOR': str(marker)})

        def failure(message):
            log.seek(0)
            return message + ': ' + log.read().decode(errors='replace')

        def cleanup():
            if process.poll() is None:
                process.kill()
                process.wait()
        self.addCleanup(cleanup)
        try:
            wait_ready(path, timeout=20, process=process)
        except (OSError, TimeoutError, RuntimeError):
            self.fail(failure('native listener failed'))
        deadline = time.monotonic() + 10
        while not (marker.exists() and marker.read_text().endswith('\n')):
            self.assertLess(time.monotonic(), deadline, failure('the connector did not start'))
            time.sleep(.05)
        pids = [int(pid) for pid in marker.read_text().splitlines()[-1].split()]
        self.assertTrue(all(alive(pid) for pid in pids))
        # Stopping what it started goes no further than its own processes.
        self.addCleanup(lambda: [os.kill(pid, 9) for pid in pids if alive(pid)])
        return process, pids, failure

    def assert_gone(self, pids, within, failure):
        deadline = time.monotonic() + within
        while any(alive(pid) for pid in pids):
            left = [pid for pid in pids if alive(pid)]
            self.assertLess(time.monotonic(), deadline, failure(f'connector processes {left} outlived Wheelhouse'))
            time.sleep(.05)

    @unittest.skipIf(WINDOWS, 'Windows connectors are held by a job object (connector.rs)')
    @unittest.skipUnless(os.environ.get('WHEELHOUSE_TEST_BINARY'), 'native executable not selected')
    def test_quitting_stops_the_connector_and_what_it_started(self):
        # SIGTERM quits as Exit does: Wheelhouse stops its connectors (TERM,
        # then KILL after 2s) and exits normally.
        process, pids, failure = self.start_with_connector()
        process.send_signal(15)
        try:
            self.assertEqual(process.wait(timeout=15), 0, failure('Wheelhouse did not quit normally'))
        except subprocess.TimeoutExpired:
            self.fail(failure('Wheelhouse did not quit'))
        self.assert_gone(pids, 1, failure)

    @unittest.skipIf(WINDOWS, 'Windows connectors are held by a job object (connector.rs)')
    @unittest.skipUnless(os.environ.get('WHEELHOUSE_TEST_BINARY'), 'native executable not selected')
    def test_connector_ends_when_wheelhouse_is_killed(self):
        # No teardown at all: the connector's lifeline closes and its guardian
        # ends its process group (TERM, then KILL after 2s).
        process, pids, failure = self.start_with_connector()
        process.kill()
        process.wait()
        self.assert_gone(pids, 8, failure)

    @unittest.skipUnless(WINDOWS, 'Windows protected pipe DACL')
    def test_pipe_dacl_is_protected_and_grants_only_system_and_current_user(self):
        # ADR 0011: inspect the actual created pipe, not just the SDDL builder.
        from ctypes import wintypes as W
        security, kernel = module.security, module.kernel
        security.GetSecurityInfo.argtypes = [W.HANDLE, C.c_int, W.DWORD,
            C.c_void_p, C.c_void_p, C.c_void_p, C.c_void_p, C.POINTER(C.c_void_p)]
        security.GetSecurityInfo.restype = W.DWORD
        security.GetSecurityDescriptorControl.argtypes = [C.c_void_p, C.POINTER(W.WORD), C.POINTER(W.DWORD)]
        security.GetSecurityDescriptorControl.restype = W.BOOL
        class AclHeader(C.Structure):
            _fields_ = [('revision', W.BYTE), ('reserved', W.BYTE),
                        ('size', W.WORD), ('count', W.WORD), ('reserved2', W.WORD)]
        class AllowedAce(C.Structure):
            _fields_ = [('kind', W.BYTE), ('flags', W.BYTE),
                        ('size', W.WORD), ('mask', W.DWORD), ('sid_start', W.DWORD)]
        security.GetAce.argtypes = [C.c_void_p, W.DWORD, C.POINTER(C.c_void_p)]
        security.GetAce.restype = W.BOOL
        security.ConvertSidToStringSidW.argtypes = [C.c_void_p, C.POINTER(C.c_void_p)]
        security.ConvertSidToStringSidW.restype = W.BOOL
        kernel.LocalFree.argtypes = [C.c_void_p]
        kernel.LocalFree.restype = C.c_void_p
        pipe = PipeSocket(self.path, 2)
        descriptor, dacl, user_text = C.c_void_p(), C.c_void_p(), C.c_void_p()
        try:
            result = security.GetSecurityInfo(pipe.handle, 6, 4, None, None, C.byref(dacl), None, C.byref(descriptor))
            self.assertEqual(result, 0)
            control, revision = W.WORD(), W.DWORD()
            module.checked(security.GetSecurityDescriptorControl(descriptor, C.byref(control), C.byref(revision)))
            self.assertTrue(control.value & 0x1000)  # SE_DACL_PROTECTED
            token_buffer, sid = module.owner(kernel.GetCurrentProcess())
            module.checked(security.ConvertSidToStringSidW(sid, C.byref(user_text)))
            self.assertTrue(dacl.value)
            self.assertEqual(C.cast(dacl, C.POINTER(AclHeader)).contents.count, 2)
            trustees = set()
            for index in range(2):
                pointer = C.c_void_p()
                module.checked(security.GetAce(dacl, index, C.byref(pointer)))
                ace = C.cast(pointer, C.POINTER(AllowedAce)).contents
                self.assertEqual(ace.kind, 0)  # ACCESS_ALLOWED_ACE_TYPE
                self.assertEqual(ace.flags, 0)  # Explicit grants, no inheritance
                self.assertIn(ace.mask, {0x10000000, 0x1f01ff})  # GA or FILE_ALL_ACCESS
                sid_text = C.c_void_p()
                # Compare binary ACE identities rendered by ConvertSidToStringSid,
                # not SDDL aliases (the local Administrator can render as LA).
                module.checked(security.ConvertSidToStringSidW(
                    C.c_void_p(pointer.value + AllowedAce.sid_start.offset), C.byref(sid_text)))
                try:
                    trustees.add(C.wstring_at(sid_text))
                finally:
                    kernel.LocalFree(sid_text)
            self.assertEqual(trustees, {'S-1-5-18', C.wstring_at(user_text)})
        finally:
            for allocation in (descriptor, user_text):
                if allocation.value:
                    kernel.LocalFree(allocation)
            pipe.close()

    @unittest.skipUnless(WINDOWS, 'Windows pipe identity and timeout boundary')
    def test_pipe_rejects_unexpected_owner_and_times_out_reads(self):
        # ADR 0011: reject a real server whose owner differs from the expected SID.
        # Use the null SID as the injected policy; no real process can run as it.
        module.security.ConvertStringSidToSidW.argtypes = [C.c_wchar_p, C.POINTER(C.c_void_p)]
        module.security.ConvertStringSidToSidW.restype = C.c_int
        module.kernel.LocalFree.argtypes = [C.c_void_p]
        module.kernel.LocalFree.restype = C.c_void_p
        sid = C.c_void_p()
        module.checked(module.security.ConvertStringSidToSidW('S-1-0-0', C.byref(sid)))
        pipe = PipeSocket(self.path, .1)
        try:
            with self.assertRaises(PermissionError):
                module.verify_server(pipe.handle, sid)
            # An idle connected server has no HTTP response to read. A bounded
            # read must cancel and settle rather than hang the test process.
            with pipe.makefile('rb') as reader:
                started = time.monotonic()
                with self.assertRaises(TimeoutError):
                    reader.read(1)
                self.assertLess(time.monotonic() - started, 2)
        finally:
            pipe.close()
            module.kernel.LocalFree(sid)

    def test_null_and_empty_socket_paths_are_rejected(self):
        for path, size in [(None, 0), (None, 1), (b"", 0)]:
            error = C.create_string_buffer(512)
            self.assertFalse(lib.wheelhouse_ingress_start(path, size, self.wake, error, len(error)))
            self.assertIn(b"null or empty", error.value)

    def test_socket_ownership_restart_and_no_stealing(self):
        # ADR 0011: duplicate startup must fail; shutdown permits a fresh owner.
        if not WINDOWS:
            self.assertEqual(os.stat(self.path).st_mode & 0o777, 0o600)
        error = C.create_string_buffer(512)
        second = lib.wheelhouse_ingress_start(self.path.encode(), len(self.path), self.wake, error, len(error))
        self.assertFalse(second)
        lib.wheelhouse_ingress_stop(self.server)
        self.server = None
        if not WINDOWS:
            self.assertFalse(os.path.exists(self.path))
        self.server = self.start()
        self.assertEqual(self.request(json.dumps(patch('project', 'p', {'display.label': 'restart'})).encode()), 204)

def load_tests(loader, tests, pattern):
    # Keep pure redaction/order invariants in the existing cross-platform native
    # ingress entry point, alongside the real-ABI capture/replay scenarios.
    spec = importlib.util.spec_from_file_location('test_replay_ingress', ROOT / 'tools/test-replay-ingress.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    tests.addTests(loader.loadTestsFromModule(module))
    return tests

if __name__ == '__main__':
    unittest.main()
