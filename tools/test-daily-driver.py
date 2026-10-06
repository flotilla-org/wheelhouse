#!/usr/bin/env python3
"""Exercise launcher lifecycle with controlled UI, watcher, and connector processes."""
import importlib.util
import itertools
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest
from unittest import mock

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
print('daemon=' + os.environ.get('FLOTILLA_DAEMON', ''), flush=True)
with socketserver.UnixStreamServer(path, Handler) as server:
    server.serve_forever()
'''
FAKE_FLOTILLA = '''#!/usr/bin/env python3
import os, sys, time
print('args=' + repr(sys.argv[1:]), flush=True)
print('socket=' + os.environ['WHEELHOUSE_SOCKET'], flush=True)
print('daemon=' + os.environ.get('FLOTILLA_DAEMON', ''), flush=True)
if os.environ.get('FAIL_PRODUCER') or (os.environ.get('FAIL_PRODUCER_UNTIL') and
                                      not os.path.exists(os.environ['FAIL_PRODUCER_UNTIL'])):
    sys.exit(7)
print('connector ready', flush=True)
while True: time.sleep(1)
'''
FAKE_WATCHER = '''#!/usr/bin/env python3
import http.client, os, socket, sys, time
assert sys.argv[1:3] == ['--transport', 'wheelhouse'], sys.argv
path = sys.argv[sys.argv.index('--socket') + 1]
roots = [sys.argv[i+1] for i, value in enumerate(sys.argv) if value == '--roots']
assert roots
print('args=' + repr(sys.argv[1:]), flush=True)
print('pid=' + str(os.getpid()), flush=True)
connection = http.client.HTTPConnection('localhost')
connection.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
connection.sock.connect(path)
connection.request('POST', '/v1/metadata/patch', '{}')
assert connection.getresponse().status == 204
connection.close()
while all(os.path.isdir(root) for root in roots): time.sleep(.1)
sys.exit(8)
'''
MISMATCH_FLOTILLA = '''#!/usr/bin/env python3
import sys
print('wire generation mismatch: client fingerprint old speaks proto 21 (build old+dirty); '
      'daemon fingerprint new speaks proto 21 (build new)', file=sys.stderr)
sys.exit(1)
'''


@unittest.skipIf(sys.platform == 'win32', 'native watcher builds are Unix-only')
class WatcherBuildTests(unittest.TestCase):
    def test_build_ownership_and_bypasses(self):
        # Issue #138: the normal host build owns the watcher; an existing host
        # binary needs a standalone build unless overridden or builds/git are off.
        # Exhaust all 16 launcher switch combinations with release absent, empty, or set.
        # The launcher uses debug artifacts even when release is exported in the parent.
        spec = importlib.util.spec_from_file_location('daily_driver', ROOT / 'tools/daily-driver.py')
        launcher = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(launcher)
        for supplied_host, supplied_watcher, no_build, no_git, release_value in itertools.product(
                (False, True), (False, True), (False, True), (False, True), (None, '', '1')):
            with self.subTest(supplied_host=supplied_host, supplied_watcher=supplied_watcher,
                              no_build=no_build, no_git=no_git, release=release_value), tempfile.TemporaryDirectory() as directory:
                root = Path(directory)
                (root / 'data/sidebar').mkdir(parents=True)
                (root / 'data/sidebar/daily-driver.kdl').write_text('')
                host = 'test-native-host'
                target = root / 'native outputs'
                default_watcher = target / host / 'debug/andamento-git-watcher'
                binary = root / 'build/wheelhouse'
                watcher = root / 'supplied watcher' if supplied_watcher else default_watcher
                if supplied_host:
                    binary = root / 'supplied wheelhouse'
                flotilla = root / 'flotilla'
                for path in (binary, watcher, flotilla):
                    path.parent.mkdir(parents=True, exist_ok=True)
                    path.touch()
                    path.chmod(0o755)
                environment = {'FLOTILLA_BIN': str(flotilla), 'WHEELHOUSE_DAILY_DIR': str(root / 'state'),
                               'WHEELHOUSE_ANDAMENTO_DIR': str(root / 'andamento'),
                               'WHEELHOUSE_ANDAMENTO_TARGET_DIR': str(target)}
                if release_value is not None:
                    environment['release'] = release_value
                if supplied_host:
                    environment['WHEELHOUSE_BIN'] = str(binary)
                if supplied_watcher:
                    environment['ANDAMENTO_GIT_WATCHER_BIN'] = str(watcher)
                options = ['daily-driver.py', '--no-git'] if no_git else ['daily-driver.py', '--repo', str(ROOT)]
                if no_build:
                    options.append('--no-build')
                # Doubles stand at compiler/git subprocess and UI/producer launch boundaries.
                with mock.patch.object(launcher, 'ROOT', root), \
                        mock.patch.dict(os.environ, environment, clear=True), \
                        mock.patch.object(sys, 'argv', options), \
                        mock.patch.object(launcher.subprocess, 'run') as commands, \
                        mock.patch.object(launcher.subprocess, 'check_output', return_value=f'host: {host}\n') as rustc, \
                        mock.patch.object(launcher, 'run', return_value=0) as launch:
                    self.assertEqual(launcher.main(), 0)
                expected = []
                if not no_build and not supplied_host:
                    expected.append(['bash', 'build.sh', 'wheelhouse'])
                    host_build = next(call for call in commands.call_args_list
                                      if call.args[0][:2] == ['bash', 'build.sh'])
                    self.assertNotIn('release', host_build.kwargs['env'])
                if supplied_host and not (supplied_watcher or no_build or no_git):
                    expected.extend([
                        [sys.executable, str(root / 'tools/prepare-andamento-build.py'), str(root / 'andamento')],
                        ['cargo', 'build', '--manifest-path', str(root / 'build/andamento/Cargo.toml'),
                         '-p', 'andamento-git-watcher', '--bin', 'andamento-git-watcher', '--locked',
                         '--target', host, '--target-dir', str(target)],
                    ])
                # Only build commands are this contract; unrelated main() subprocesses may change.
                build_commands = [call.args[0] for call in commands.call_args_list
                                  if call.args[0][0] == 'cargo'
                                  or call.args[0][:2] == ['bash', 'build.sh']
                                  or call.args[0][:2] == [sys.executable, str(root / 'tools/prepare-andamento-build.py')]]
                self.assertEqual(build_commands, expected)
                self.assertEqual(rustc.call_count, int(not (supplied_watcher or no_git)))
                launch.assert_called_once()
                args, actual_binary, _, _ = launch.call_args.args
                self.assertEqual(actual_binary, binary)
                if not no_git:
                    self.assertEqual(args.watcher, watcher)
                else:
                    self.assertEqual(args.repo, [])


@unittest.skipIf(sys.platform == 'win32', 'Unix fixtures; Windows has native pipe/job lifecycle contracts')
class DailyDriverTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='wh-driver-test-', dir='/tmp')
        self.directory = Path(self.tmp.name)
        self.state = self.directory / 'saved settings'
        self.app = self.directory / 'fake wheelhouse'
        self.flotilla = self.directory / 'fake flotilla'
        self.watcher = self.directory / 'fake watcher'
        for path, content in [(self.app, FAKE_APP), (self.flotilla, FAKE_FLOTILLA), (self.watcher, FAKE_WATCHER)]:
            path.write_text(content)
            path.chmod(0o755)
        self.env = {**os.environ, 'WHEELHOUSE_BIN': str(self.app), 'ANDAMENTO_GIT_WATCHER_BIN': str(self.watcher), 'FLOTILLA_BIN': str(self.flotilla),
                    'HOME': str(self.directory), 'FLOTILLA_ROOT': str(self.directory / 'dev'),
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
        watcher = self.log('git')
        self.assertIn("'--transport', 'wheelhouse', '--socket'", watcher)
        self.assertIn("'--roots', " + repr(str(ROOT)), watcher)
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

    def test_missing_watcher_fails_before_starting_app(self):
        process = self.start(ANDAMENTO_GIT_WATCHER_BIN=str(self.directory / 'missing'))
        self.assertNotEqual(process.wait(timeout=10), 0)
        self.assertIn('Andamento git watcher not found', process.stdout.read())
        self.assertFalse((self.state / 'logs/wheelhouse.log').exists())

    def test_explicit_remote_endpoint_is_inherited_by_ui_and_connector(self):
        endpoint = 'ssh://udder/opt/flotilla tools/flotilla'
        process = self.start(['--daemon', endpoint])
        self.ready(process, connector=True)
        self.assertIn('daemon=' + endpoint, self.log('wheelhouse'))
        self.assertIn('daemon=' + endpoint, self.log('flotilla'))

    def test_no_git_does_not_require_watcher(self):
        self.command = self.command[:1]
        process = self.start(['--no-git'], ANDAMENTO_GIT_WATCHER_BIN=str(self.directory / 'missing'))
        deadline = time.monotonic() + 10
        while 'connector ready' not in self.log('flotilla') and time.monotonic() < deadline:
            if process.poll() is not None:
                self.fail(process.stdout.read())
            time.sleep(.05)
        self.assertIn('connector ready', self.log('flotilla'))
        self.assertFalse((self.state / 'logs/git.log').exists())

    def test_startup_failure_does_not_start_producers(self):
        process = self.start(FAIL_START='1')
        self.assertEqual(process.wait(timeout=10), 1)
        self.assertIn('before startup', process.stdout.read())
        self.assertFalse((self.state / 'logs/flotilla.log').exists())

    def install_fleet(self, generation, content=FAKE_FLOTILLA):
        fleet = self.directory / '.local/opt/flotilla-fleet'
        binary = fleet / generation / 'bin/flotilla'
        binary.parent.mkdir(parents=True)
        binary.write_text(content)
        binary.chmod(0o755)
        current = fleet / 'current'
        current.unlink(missing_ok=True)
        current.symlink_to(generation)
        return binary

    def test_fleet_default_follows_current(self):
        del self.env['FLOTILLA_BIN']
        self.install_fleet('old', MISMATCH_FLOTILLA)
        binary = self.install_fleet('new')
        process = self.start()
        self.ready(process, connector=True)
        self.assertIn(str(binary), self.log('flotilla'))

    def test_dev_fallback_without_fleet(self):
        del self.env['FLOTILLA_BIN']
        binary = Path(self.env['FLOTILLA_ROOT']) / 'target/debug/flotilla'
        binary.parent.mkdir(parents=True)
        shutil.copy2(self.flotilla, binary)
        process = self.start()
        self.ready(process, connector=True)
        self.assertIn(str(binary), self.log('flotilla'))

    def test_explicit_override_wins_over_fleet(self):
        self.install_fleet('old', MISMATCH_FLOTILLA)
        process = self.start()
        self.ready(process, connector=True)
        self.assertIn(str(self.flotilla), self.log('flotilla'))

    def test_mismatch_hint_and_recovery_after_fleet_roll(self):
        del self.env['FLOTILLA_BIN']
        self.install_fleet('old', MISMATCH_FLOTILLA)
        process = self.start()
        deadline = time.monotonic() + 10
        while 'restarting in' not in self.log('flotilla') and time.monotonic() < deadline:
            if process.poll() is not None:
                self.fail(process.stdout.read())
            time.sleep(.05)
        self.assertIn('restarting in', self.log('flotilla'))
        binary = self.install_fleet('new')
        self.ready(process, connector=True)
        self.assertIn(str(binary), self.log('flotilla'))
        process.terminate()
        self.assertEqual(process.wait(timeout=10), 130)
        output = process.stdout.read()
        self.assertIn('Flotilla build mismatch: client old+dirty, daemon new;', output)
        self.assertIn('set FLOTILLA_BIN', output)
        self.assertNotIn('wire generation mismatch:', output)

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

    def test_git_producer_failure_stops_app(self):
        repo = self.directory / 'disappearing-repo'
        repo.mkdir()
        subprocess.run(['git', 'init', '-q', str(repo)], check=True)
        process = self.start(['--git-only', '--repo', str(repo)])
        self.ready(process)
        pid = int(self.log('wheelhouse').split('pid=', 1)[1].splitlines()[0])
        self.assertIn(repr(str(repo.resolve())), self.log('git'))
        shutil.rmtree(repo)
        self.assertEqual(process.wait(timeout=10), 1)
        self.assertIn('git producer exited with status', process.stdout.read())
        with self.assertRaises(ProcessLookupError):
            os.kill(pid, 0)


if __name__ == '__main__':
    unittest.main()
