#!/usr/bin/env python3
"""Exercise real HTTP/UDS ingress and the real Andamento C decoder."""
import concurrent.futures
import ctypes as C
import http.client
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
LIBDIR = Path(sys.argv.pop(1))
SUFFIX = '.dylib' if sys.platform == 'darwin' else '.so'
lib = C.CDLL(str(LIBDIR / ('libwheelhouse_ingress' + SUFFIX)))
core = C.CDLL(str(LIBDIR / ('libandamento_ffi' + SUFFIX)))
class UnixHTTPConnection(http.client.HTTPConnection):
    def __init__(self, path):
        super().__init__('localhost', timeout=7)
        self.path = path

    def connect(self):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect(self.path)


def patch(kind, identity, facts):
    return {'type': 'metadata-patch', 'target': {'kind': 'entity', 'value': {'kind': kind, 'id': identity}},
            'source_id': 'ingress.test', 'set': {
                key: {'value': {'type': 'text', 'value': value}, 'ttl_ms': 6000}
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
lib.wheelhouse_ingress_poll_observed.argtypes = [C.c_void_p, APPLY, OBSERVE, C.c_void_p]
lib.wheelhouse_ingress_start.argtypes = [C.c_char_p, C.c_size_t, WAKE, C.c_void_p, C.c_size_t]
lib.wheelhouse_ingress_start.restype = C.c_void_p
lib.wheelhouse_ingress_poll.argtypes = [C.c_void_p, APPLY, C.c_void_p]
lib.wheelhouse_ingress_stop.argtypes = [C.c_void_p]
core.andamento_create.argtypes = [C.c_char_p, C.c_size_t, C.c_void_p]
core.andamento_create.restype = C.c_void_p
core.andamento_destroy.argtypes = [C.c_void_p]
core.andamento_apply_patch_json.argtypes = [C.c_void_p, C.c_uint64, Text, C.c_void_p]
core.andamento_apply_patch_json.restype = C.c_uint32
core.andamento_tick.argtypes = [C.c_void_p, C.c_uint64, C.c_void_p]
core.andamento_snapshot_acquire.argtypes = [C.c_void_p, C.c_void_p]
core.andamento_snapshot_acquire.restype = C.c_void_p
core.andamento_snapshot_node_count.argtypes = [C.c_void_p]
core.andamento_snapshot_node_count.restype = C.c_size_t
core.andamento_snapshot_release.argtypes = [C.c_void_p]

class IngressTests(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory(prefix='wh-', dir='/tmp')
        self.path = os.path.join(self.dir.name, 'facts.sock')
        self.wake = WAKE(lambda: None)
        config = (ROOT / 'data/sidebar/fixture.kdl').read_bytes()
        self.core = core.andamento_create(config, len(config), None)
        self.assertTrue(self.core)
        self.received = []
        def apply(_, data, size):
            self.received.append(C.string_at(data, size))
            return core.andamento_apply_patch_json(self.core, 0, Text(data, size), None)
        self.apply = APPLY(apply)
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
        return server

    def tearDown(self):
        lib.wheelhouse_ingress_stop(self.server)
        self.pool.shutdown()
        core.andamento_destroy(self.core)
        self.dir.cleanup()

    def request(self, body=b'', content_type='application/json', chunked=False, poll=True, read=False):
        def send():
            connection = UnixHTTPConnection(self.path)
            try:
                payload = iter([body[:5], body[5:]]) if chunked else body
                connection.request('GET' if read else 'POST', '/v1/observed/workdirs' if read else '/v1/metadata/patch', payload, {'Content-Type': content_type}, encode_chunked=chunked)
                response = connection.getresponse()
                data = response.read()
                return (response.status, data, response.getheader("Content-Type")) if read else response.status
            finally:
                connection.close()
        future = self.pool.submit(send)
        deadline = time.monotonic() + 8
        while not future.done() and time.monotonic() < deadline:
            if poll:
                lib.wheelhouse_ingress_poll_observed(self.server, self.apply, self.observe, None)
            time.sleep(.005)
        return future.result(timeout=1)

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

    def test_read_timeout_cancellation_and_unavailability(self):
        self.assertEqual(self.request(read=True, poll=False)[0], 503)
        lib.wheelhouse_ingress_poll_observed(self.server, self.apply, self.observe, None)
        self.assertEqual(self.observed, 0)
        self.available = False
        self.assertEqual(self.request(read=True)[0], 503)

    def count(self):
        snapshot = core.andamento_snapshot_acquire(self.core, None)
        self.assertTrue(snapshot)
        count = core.andamento_snapshot_node_count(snapshot)
        core.andamento_snapshot_release(snapshot)
        return count

    def test_patch_duplicate_expiry_and_chunked_http(self):
        before = self.count()
        body = json.dumps(patch('project', 'p', {'display.label': 'Live project', 'flotilla.project': 'p'})).encode()
        self.assertEqual(self.request(body), 204)
        after = self.count()
        self.assertGreater(after, before)
        self.assertEqual(self.request(body, chunked=True), 204)
        self.assertEqual(self.count(), after)
        self.assertEqual(self.received, [body, body])
        self.assertEqual(core.andamento_tick(self.core, 6001, None), 1)
        self.assertEqual(self.count(), before)

    def test_http_and_schema_errors(self):
        self.assertEqual(self.request(b'not json'), 400)
        self.assertEqual(self.request(b'{}'), 400)
        self.assertEqual(self.request(b'{}', 'text/plain'), 415)
        self.assertEqual(self.request(b'{"type":"metadata-patch"}'), 422)
        self.assertEqual(self.request(b'x' * (1024 * 1024 + 1)), 413)

    def test_ack_waits_for_application_and_cancelled_request_is_skipped(self):
        body = json.dumps(patch('project', 'p', {'display.label': 'late'})).encode()
        self.assertEqual(self.request(body, poll=False), 503)
        lib.wheelhouse_ingress_poll_observed(self.server, self.apply, self.observe, None)
        self.assertEqual(self.received, [])

    @unittest.skipUnless(os.environ.get('WHEELHOUSE_TEST_BINARY'), 'native executable not selected')
    def test_native_process_applies_producer_patch(self):
        path = os.path.join(self.dir.name, 'native.sock')
        binary = str(Path(os.environ['WHEELHOUSE_TEST_BINARY']).resolve())
        Path(self.dir.name, 'user').write_text(
            'window: {\nsize: 900 600\nworkspace: {\nlabel: "Observed"\npanels: {\n'
            'terminal: {\nexpression: "read answer"\ncwd: "/tmp"\nselected\n}\n'
            'terminal: {\nexpression: "read answer"\ncwd: "' + self.dir.name + '"\n}\n'
            'terminal: {\nexpression: "read answer"\n}\n}\n}\n}\n')
        with tempfile.TemporaryFile() as log:
            process = subprocess.Popen([binary, '--user:' + self.dir.name + '/user',
                                        '--project:' + self.dir.name + '/project',
                                        '--andamento_socket:' + path,
                                        '--andamento_config:' + str(ROOT / 'data/sidebar/fixture.kdl')],
                                       stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 20
                while not os.path.exists(path) and time.monotonic() < deadline and process.poll() is None:
                    time.sleep(.05)
                if not os.path.exists(path):
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
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    def test_null_and_empty_socket_paths_are_rejected(self):
        for path, size in [(None, 0), (None, 1), (b"", 0)]:
            error = C.create_string_buffer(512)
            self.assertFalse(lib.wheelhouse_ingress_start(path, size, self.wake, error, len(error)))
            self.assertIn(b"null or empty", error.value)

    def test_socket_ownership_restart_and_no_stealing(self):
        self.assertEqual(os.stat(self.path).st_mode & 0o777, 0o600)
        error = C.create_string_buffer(512)
        second = lib.wheelhouse_ingress_start(self.path.encode(), len(self.path), self.wake, error, len(error))
        self.assertFalse(second)
        lib.wheelhouse_ingress_stop(self.server)
        self.server = None
        self.assertFalse(os.path.exists(self.path))
        self.server = self.start()
        self.assertEqual(self.request(json.dumps(patch('project', 'p', {'display.label': 'restart'})).encode()), 204)

if __name__ == '__main__':
    unittest.main()
