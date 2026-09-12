#!/usr/bin/env python3
"""Exercise real HTTP/UDS ingress and the real Andamento C decoder."""
import concurrent.futures
import ctypes as C
import http.client
import importlib.util
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
spec = importlib.util.spec_from_file_location('publisher', ROOT / 'tools/andamento-publish.py')
publisher = importlib.util.module_from_spec(spec)
spec.loader.exec_module(publisher)
WAKE = C.CFUNCTYPE(None)
APPLY = C.CFUNCTYPE(C.c_uint32, C.c_void_p, C.c_void_p, C.c_size_t)
class Text(C.Structure):
    _fields_ = [('data', C.c_void_p), ('len', C.c_size_t)]
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

    def request(self, body, content_type='application/json', chunked=False, poll=True):
        def send():
            connection = publisher.UnixHTTPConnection(self.path)
            try:
                payload = iter([body[:5], body[5:]]) if chunked else body
                connection.request('POST', '/v1/metadata/patch', payload, {'Content-Type': content_type}, encode_chunked=chunked)
                response = connection.getresponse()
                response.read()
                return response.status
            finally:
                connection.close()
        future = self.pool.submit(send)
        deadline = time.monotonic() + 8
        while not future.done() and time.monotonic() < deadline:
            if poll:
                lib.wheelhouse_ingress_poll(self.server, self.apply, None)
            time.sleep(.005)
        return future.result(timeout=1)

    def count(self):
        snapshot = core.andamento_snapshot_acquire(self.core, None)
        self.assertTrue(snapshot)
        count = core.andamento_snapshot_node_count(snapshot)
        core.andamento_snapshot_release(snapshot)
        return count

    def test_patch_duplicate_expiry_and_chunked_http(self):
        before = self.count()
        body = json.dumps(publisher.patch('project', 'p', {'display.label': 'Live project', 'flotilla.project': 'p'})).encode()
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
        body = json.dumps(publisher.patch('project', 'p', {'display.label': 'late'})).encode()
        self.assertEqual(self.request(body, poll=False), 503)
        lib.wheelhouse_ingress_poll(self.server, self.apply, None)
        self.assertEqual(self.received, [])

    @unittest.skipUnless(os.environ.get('WHEELHOUSE_TEST_BINARY'), 'native executable not selected')
    def test_native_process_applies_producer_patch(self):
        path = os.path.join(self.dir.name, 'native.sock')
        binary = str(Path(os.environ['WHEELHOUSE_TEST_BINARY']).resolve())
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
                publisher.publish(path, publisher.patch('project', 'native', {
                    'display.label': 'Native HTTP project', 'flotilla.project': 'native'}))
                connection = publisher.UnixHTTPConnection(path)
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
                publisher.publish(path, publisher.patch('project', 'native', {
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
        self.assertEqual(self.request(json.dumps(publisher.patch('project', 'p', {'display.label': 'restart'})).encode()), 204)

if __name__ == '__main__':
    unittest.main()
