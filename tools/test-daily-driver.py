#!/usr/bin/env python3
"""Exercise launcher lifecycle with a fake UI and the real git producer."""
import os
from pathlib import Path
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
FAKE_APP = '''#!/usr/bin/env python3
import http.server, os, signal, socketserver, sys
signal.signal(signal.SIGUSR1, lambda *args: sys.exit(0))
path = next(a.split(':', 1)[1] for a in sys.argv if a.startswith('--andamento_socket:'))
if os.environ.get('FAIL_START'):
    sys.exit(9)
class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(204); self.end_headers()
    def do_POST(self):
        self.rfile.read(int(self.headers['Content-Length']))
        print('patch received', flush=True)
        self.send_response(204); self.end_headers()
    def log_message(self, *args): pass
print('pid=' + str(os.getpid()), flush=True)
with socketserver.UnixStreamServer(path, Handler) as server:
    server.serve_forever()
'''
FAKE_FLOTILLA = '''#!/usr/bin/env python3
import os, sys, time
print('args=' + repr(sys.argv[1:]), flush=True)
print('socket=' + os.environ['WHEELHOUSE_SOCKET'], flush=True)
if os.environ.get('FAIL_PRODUCER') or (os.environ.get('FAIL_PRODUCER_UNTIL') and
                                    not os.path.exists(os.environ['FAIL_PRODUCER_UNTIL'])):
    sys.exit(7)
print('connector ready', flush=True)
while True: time.sleep(1)
'''


class DailyDriverTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='wh-driver-test-', dir='/tmp')
        self.directory = Path(self.tmp.name)
        self.state = self.directory / 'saved settings'
        self.app = self.directory / 'fake wheelhouse'
        self.flotilla = self.directory / 'fake flotilla'
        for path, content in [(self.app, FAKE_APP), (self.flotilla, FAKE_FLOTILLA)]:
            path.write_text(content)
            path.chmod(0o755)
        self.env = {**os.environ, 'WHEELHOUSE_BIN': str(self.app), 'FLOTILLA_BIN': str(self.flotilla),
                    'WHEELHOUSE_DAILY_DIR': str(self.state), 'WHEELHOUSE_ANDAMENTO_CONFIG': str(ROOT / 'data/sidebar/fixture.kdl')}
        self.command = [str(ROOT / 'scripts/run-daily-driver.sh'), '--repo', str(ROOT)]
        self.processes = []

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            process.stdout.close()
        self.tmp.cleanup()

    def start(self, extra=(), **env):
        process = subprocess.Popen(self.command + list(extra), env={**self.env, **env},
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        self.processes.append(process)
        return process

    def log(self, name):
        path = self.state / 'logs' / (name + '.log')
        return path.read_text() if path.exists() else ''

    def ready(self, process, connector=False, previous_pid=None):
        deadline = time.monotonic() + 10
        while time.monotonic() < deadline:
            app_log = self.log('wheelhouse')
            if ('patch received' in app_log and
                    (not connector or 'socket=' in self.log('flotilla')) and
                    (previous_pid is None or f'pid={previous_pid}\n' not in app_log)):
                return
            if process.poll() is not None:
                self.fail(process.stdout.read())
            time.sleep(.05)
        self.fail('git producer did not reach the UI')

    def test_full_launch_lock_signal_cleanup_and_restart(self):
        process = self.start()
        self.ready(process, connector=True)
        connector = self.log('flotilla')
        self.assertIn("'pm', 'connect', '--wheelhouse-socket'", connector)
        self.assertIn(str(self.flotilla), connector)
        path = connector.split('socket=', 1)[1].splitlines()[0]
        self.assertTrue(Path(path).is_socket())
        self.assertEqual(Path(path).parent.stat().st_mode & 0o777, 0o700)
        second = self.start()
        self.assertNotEqual(second.wait(timeout=10), 0)
        self.assertIn('already using', second.stdout.read())
        process.send_signal(signal.SIGTERM)
        self.assertEqual(process.wait(timeout=10), 130)
        self.assertFalse(Path(path).parent.exists())
        pid = int(self.log('wheelhouse').split('pid=', 1)[1].splitlines()[0])
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)
        (self.state / 'user').write_text('saved layout')
        restarted = self.start(['--git-only'])
        self.ready(restarted, previous_pid=pid)
        os.kill(int(self.log('wheelhouse').split('pid=', 1)[1].splitlines()[0]), signal.SIGUSR1)
        self.assertEqual(restarted.wait(timeout=10), 0)
        self.assertEqual((self.state / 'user').read_text(), 'saved layout')

    def test_startup_failure_does_not_start_producers(self):
        process = self.start(FAIL_START='1')
        self.assertEqual(process.wait(timeout=10), 1)
        self.assertIn('before startup', process.stdout.read())
        self.assertFalse((self.state / 'logs/flotilla.log').exists())

    def test_flotilla_failure_restarts_without_closing_app(self):
        marker = self.directory / 'fleet-upgrade-complete'
        process = self.start(FAIL_PRODUCER_UNTIL=str(marker))
        deadline = time.monotonic() + 10
        while 'status 7' not in self.log('flotilla') and time.monotonic() < deadline:
            if process.poll() is not None:
                self.assertIsNone(process.poll(), process.stdout.read())
            time.sleep(.05)
        self.assertIn('status 7', self.log('flotilla'))
        self.assertIsNone(process.poll(), 'Wheelhouse should survive a connector failure')
        marker.touch()
        while 'connector ready' not in self.log('flotilla') and time.monotonic() < deadline:
            time.sleep(.05)
        self.assertIn('connector ready', self.log('flotilla'))
        self.assertIsNone(process.poll())
        pid = int(self.log('wheelhouse').split('pid=', 1)[1].splitlines()[0])
        os.kill(pid, signal.SIGUSR1)
        self.assertEqual(process.wait(timeout=10), 0)


if __name__ == '__main__':
    unittest.main()
