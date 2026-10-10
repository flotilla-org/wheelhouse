#!/usr/bin/env python3
"""Native Windows pipe, profile-lock and owned-process lifecycle contracts."""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
WINDOWS = sys.platform == 'win32'

# These controlled children are Python programs behind trusted batch wrappers,
# keeping the tests independent of a compiler or a particular app/daemon build.
APP = r'''
import ctypes as C
from ctypes import wintypes as W
import os, sys, threading, time
if os.environ.get('FAIL_START'):
    sys.exit(9)
pipe = next(arg.split(':', 1)[1] for arg in sys.argv if arg.startswith('--andamento_socket:'))
kernel = C.WinDLL('kernel32', use_last_error=True)
for fn, args, result in [
    (kernel.CreateNamedPipeW, [W.LPCWSTR, W.DWORD, W.DWORD, W.DWORD, W.DWORD, W.DWORD, W.DWORD, C.c_void_p], W.HANDLE),
    (kernel.ConnectNamedPipe, [W.HANDLE, C.c_void_p], W.BOOL),
    (kernel.ReadFile, [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p], W.BOOL),
    (kernel.WriteFile, [W.HANDLE, C.c_void_p, W.DWORD, C.POINTER(W.DWORD), C.c_void_p], W.BOOL),
    (kernel.FlushFileBuffers, [W.HANDLE], W.BOOL),
    (kernel.DisconnectNamedPipe, [W.HANDLE], W.BOOL),
    (kernel.CloseHandle, [W.HANDLE], W.BOOL),
]:
    fn.argtypes, fn.restype = args, result
def close_when_requested():
    while not os.path.exists(os.environ['CLOSE_APP']): time.sleep(.05)
    os._exit(0)
threading.Thread(target=close_when_requested, daemon=True).start()
print('args=' + repr(sys.argv[1:]), flush=True)
print('pid=' + str(os.getpid()), flush=True)
print('daemon=' + os.environ.get('FLOTILLA_DAEMON', ''), flush=True)
print('socket=' + pipe, flush=True)
# Test-owned health-only server: no metadata or secrets are accepted here.
handle = kernel.CreateNamedPipeW(pipe, 3 | 0x80000, 8, 1, 4096, 4096, 0, None)
assert handle != W.HANDLE(-1).value, C.get_last_error()
while True:
    assert kernel.ConnectNamedPipe(handle, None) or C.get_last_error() == 535
    request = b''
    count = W.DWORD()
    while b'\r\n\r\n' not in request:
        buffer = C.create_string_buffer(4096)
        if not kernel.ReadFile(handle, buffer, len(buffer), C.byref(count), None): break
        if not count.value: break
        request += buffer.raw[:count.value]
    if request.startswith(b'GET /v1/health HTTP/'):
        response = b'HTTP/1.1 204 No Content\r\nContent-Length: 0\r\nConnection: close\r\n\r\n'
        kernel.WriteFile(handle, response, len(response), C.byref(count), None)
        kernel.FlushFileBuffers(handle)
    kernel.DisconnectNamedPipe(handle)
'''

CONNECTOR = '''
import os, subprocess, sys, time
child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(300)'],
                         creationflags=subprocess.CREATE_NO_WINDOW)
print('pid=' + str(os.getpid()), flush=True)
print('grandchild=' + str(child.pid), flush=True)
print('args=' + repr(sys.argv[1:]), flush=True)
print('daemon=' + os.environ.get('FLOTILLA_DAEMON', ''), flush=True)
print('socket=' + os.environ['WHEELHOUSE_SOCKET'], flush=True)
marker = os.environ.get('FAIL_UNTIL')
if marker and not os.path.exists(marker): sys.exit(7)
print('connector ready', flush=True)
while True: time.sleep(1)
'''


def alive(pid):
    kernel = C.WinDLL('kernel32', use_last_error=True)
    kernel.OpenProcess.argtypes, kernel.OpenProcess.restype = [W.DWORD, W.BOOL, W.DWORD], W.HANDLE
    kernel.WaitForSingleObject.argtypes, kernel.WaitForSingleObject.restype = [W.HANDLE, W.DWORD], W.DWORD
    kernel.CloseHandle.argtypes = [W.HANDLE]
    handle = kernel.OpenProcess(0x100000, False, pid)  # SYNCHRONIZE
    if not handle:
        if C.get_last_error() == 87:  # PID no longer exists
            return False
        raise C.WinError(C.get_last_error())
    try:
        result = kernel.WaitForSingleObject(handle, 0)
        assert result in (0, 258), result
        return result == 258
    finally:
        kernel.CloseHandle(handle)


@unittest.skipUnless(WINDOWS, 'native Windows contracts')
class WindowsDailyDriverTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='wh-driver-win-')
        self.directory = Path(self.tmp.name) / 'paths with spaces'
        self.directory.mkdir()
        self.state = self.directory / 'saved settings'
        self.close_app = self.directory / 'close-app'
        self.app = self.wrapper('fake wheelhouse', APP)
        self.connector = self.wrapper('fake flotilla', CONNECTOR)
        self.env = {**os.environ, 'PYTHONUTF8': '1', 'WHEELHOUSE_BIN': str(self.app),
                    'FLOTILLA_BIN': str(self.connector), 'FLOTILLA_DAEMON': 'ssh://inherited',
                    'WHEELHOUSE_DAILY_DIR': str(self.state), 'CLOSE_APP': str(self.close_app),
                    'WHEELHOUSE_ANDAMENTO_CONFIG': str(ROOT / 'data/sidebar/fixture.kdl'),
                    'WHEELHOUSE_PYTHON_BIN': sys.executable,
                    'ANDAMENTO_GIT_WATCHER_BIN': str(self.directory / 'absent watcher')}
        self.command = [sys.executable, str(ROOT / 'tools/daily-driver.py'), '--no-build']
        self.processes = []

    def wrapper(self, name, content):
        script = self.directory / (name + '.py')
        script.write_text(content, encoding='utf-8')
        wrapper = self.directory / (name + '.cmd')
        wrapper.write_text(f'@echo off\n@"{sys.executable}" "{script}" %*\n', encoding='utf-8')
        return wrapper

    def tearDown(self):
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
            process.stdout.close()
        self.tmp.cleanup()

    def start(self, extra=(), process_options=None, **environment):
        process = subprocess.Popen(self.command + list(extra), env={**self.env, **environment},
                                   stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                                   **(process_options or {}))
        self.processes.append(process)
        return process

    def log(self, name):
        path = self.state / 'logs' / (name + '.log')
        return path.read_text(encoding='utf-8') if path.exists() else ''

    def ready(self, process, previous_pids=()):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            connector = self.log('flotilla')
            app = self.log('wheelhouse')
            current_pids = {int(line.split('=', 1)[1]) for line in (app + connector).splitlines()
                            if line.startswith(('pid=', 'grandchild='))}
            if ('connector ready' in connector and
                    not set(previous_pids).intersection(current_pids)):
                return
            if process.poll() is not None:
                self.fail(process.stdout.read())
            time.sleep(.05)
        self.fail('connector did not reach readiness: ' + self.log('flotilla'))

    def pids(self):
        return [int(line.split('=', 1)[1]) for log in [self.log('wheelhouse'), self.log('flotilla')]
                for line in log.splitlines() if line.startswith(('pid=', 'grandchild='))]

    def assert_stopped(self, pids):
        deadline = time.monotonic() + 5
        while any(alive(pid) for pid in pids) and time.monotonic() < deadline:
            time.sleep(.05)
        self.assertFalse([pid for pid in pids if alive(pid)], 'owned process survived shutdown')

    def test_pipe_health_lock_app_close_settings_restart_and_endpoint(self):
        endpoint = 'ssh://udder/opt/flotilla tools/flotilla'
        process = self.start(['--daemon', endpoint])
        self.ready(process)
        self.assertIn('daemon=' + endpoint, self.log('wheelhouse'))
        self.assertIn('daemon=' + endpoint, self.log('flotilla'))
        # #316: the daily driver opens a Dashboard of its profile.
        # resolve() expands the runner's 8.3 temp name (RUNNER~1), as the driver's path does.
        self.assertIn(repr('--dashboard:' + str(self.state.resolve() / 'dashboards' / 'daily')), self.log('wheelhouse'))
        first_pipe = self.log('flotilla').split('socket=', 1)[1].splitlines()[0]
        self.assertTrue(first_pipe.startswith(r'\\.\pipe\wheelhouse-daily-'))
        self.assertIn("'pm', 'connect', '--wheelhouse-socket'", self.log('flotilla'))
        self.assertIn(repr(str(self.connector.resolve())), self.log('flotilla'))
        self.assertFalse((self.state / 'logs/git.log').exists(), 'Windows defaults to no git watcher')
        pids = self.pids()
        self.assertTrue(all(alive(pid) for pid in pids))
        second = self.start()
        self.assertEqual(second.wait(timeout=10), 2)
        self.assertIn('already using', second.stdout.read())
        self.assertEqual(pids, self.pids(), 'second launcher touched existing logs')
        self.close_app.touch()
        self.assertEqual(process.wait(timeout=10), 0)
        self.assert_stopped(pids)
        (self.state / 'user').write_text('saved layout')
        self.close_app.unlink()
        restarted = self.start()
        self.ready(restarted, previous_pids=pids)
        next_pipe = self.log('flotilla').split('socket=', 1)[1].splitlines()[0]
        self.assertNotEqual(first_pipe, next_pipe)
        self.assertEqual((self.state / 'user').read_text(), 'saved layout')
        pids = self.pids()
        self.close_app.touch()
        self.assertEqual(restarted.wait(timeout=10), 0)
        self.assert_stopped(pids)

    def test_abrupt_launcher_exit_kills_owned_descendants(self):
        process = self.start()
        self.ready(process)
        pids = self.pids()
        process.terminate()  # TerminateProcess: no Python finally/signal handler.
        process.wait(timeout=10)
        self.assert_stopped(pids)

    def test_console_interrupt_cleans_up_owned_descendants(self):
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0  # Hidden test console, independent of the runner.
        process = self.start(process_options={'creationflags': subprocess.CREATE_NEW_CONSOLE,
                                              'startupinfo': startup})
        self.ready(process)
        pids = self.pids()
        controller = r'''
import ctypes as C
from ctypes import wintypes as W
import os, sys, time
kernel = C.WinDLL('kernel32', use_last_error=True)
kernel.AttachConsole.argtypes = [W.DWORD]
kernel.GenerateConsoleCtrlEvent.argtypes = [W.DWORD, W.DWORD]
HANDLER = C.WINFUNCTYPE(W.BOOL, W.DWORD)
kernel.SetConsoleCtrlHandler.argtypes = [HANDLER, W.BOOL]
ignore = HANDLER(lambda event: True)
def checked(name, result):
    if not result:
        with open(sys.argv[2], 'w') as output: output.write(name + ': ' + str(C.get_last_error()))
        os._exit(1)
kernel.FreeConsole()
checked('AttachConsole', kernel.AttachConsole(int(sys.argv[1])))
try:
    checked('SetConsoleCtrlHandler', kernel.SetConsoleCtrlHandler(ignore, True))
    checked('GenerateConsoleCtrlEvent', kernel.GenerateConsoleCtrlEvent(0, 0))
    time.sleep(.5)
finally:
    kernel.FreeConsole()
os._exit(0)
'''
        diagnostic = self.directory / 'console-controller-error'
        result = subprocess.run([sys.executable, '-c', controller, str(process.pid), str(diagnostic)],
                                creationflags=subprocess.CREATE_NO_WINDOW, timeout=10)
        self.assertEqual(result.returncode, 0, diagnostic.read_text() if diagnostic.exists() else 'controller failed')
        self.assertEqual(process.wait(timeout=10), 130)
        self.assert_stopped(pids)

    def test_connector_failure_restarts_without_app_or_orphaned_grandchild(self):
        marker = self.directory / 'daemon-ready'
        process = self.start(FAIL_UNTIL=str(marker))
        deadline = time.monotonic() + 15
        while 'restarting in' not in self.log('flotilla') and time.monotonic() < deadline:
            if process.poll() is not None:
                self.fail(process.stdout.read())
            time.sleep(.05)
        self.assertIn('restarting in', self.log('flotilla'))
        app_pid = int(self.log('wheelhouse').split('pid=', 1)[1].splitlines()[0])
        first_pids = [pid for pid in self.pids() if pid != app_pid]
        self.assert_stopped(first_pids)
        marker.touch()
        self.ready(process)
        self.assertTrue(alive(app_pid))
        self.assertEqual(app_pid, int(self.log('wheelhouse').split('pid=', 1)[1].splitlines()[0]))
        pids = self.pids()
        self.close_app.touch()
        self.assertEqual(process.wait(timeout=10), 0)
        self.assert_stopped(pids)

    def test_startup_failure_starts_no_connector(self):
        process = self.start(FAIL_START='1')
        self.assertEqual(process.wait(timeout=10), 1)
        self.assertIn('before startup', process.stdout.read())
        self.assertFalse((self.state / 'logs/flotilla.log').exists())

    def test_missing_endpoint_fails_before_launch(self):
        self.env.pop('FLOTILLA_DAEMON')
        process = self.start()
        self.assertEqual(process.wait(timeout=10), 2)
        self.assertIn('Windows requires --daemon', process.stdout.read())
        self.assertFalse(self.state.exists())

    def test_helper_eof_starts_no_unowned_command(self):
        marker = self.directory / 'unowned-command-started'
        command = [sys.executable, str(ROOT / 'tools/daily-driver-windows.py'), sys.executable,
                   '-c', 'from pathlib import Path; import sys; Path(sys.argv[1]).touch()', str(marker)]
        result = subprocess.run(command, input=b'', capture_output=True, timeout=10,
                                creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b'did not authorize process startup', result.stderr)
        self.assertFalse(marker.exists())

    def test_unsupported_git_discovery_is_explicit(self):
        for options in [['--git-only'], ['--repo', str(ROOT)]]:
            with self.subTest(options=options):
                process = self.start(options)
                self.assertEqual(process.wait(timeout=10), 2)
                self.assertIn('andamento#123', process.stdout.read())
                self.assertFalse(self.state.exists())

    def test_powershell_entrypoint_preserves_arguments(self):
        self.command = ['powershell', '-NoProfile', '-File', str(ROOT / 'scripts/run-daily-driver.ps1'), '--no-build']
        endpoint = 'ssh://udder/opt/flotilla tools/flotilla'
        for explicit_python in [True, False]:
            with self.subTest(explicit_python=explicit_python):
                self.state = self.directory / ('explicit Python' if explicit_python else 'automatic Python')
                self.env['WHEELHOUSE_DAILY_DIR'] = str(self.state)
                if not explicit_python:
                    self.env.pop('WHEELHOUSE_PYTHON_BIN')
                    self.close_app.unlink()
                process = self.start(['--daemon', endpoint])
                try:
                    self.ready(process)
                    self.assertIn('daemon=' + endpoint, self.log('flotilla'))
                    pids = self.pids()
                finally:
                    # Let the Python launcher unwind even when the wrapper test fails.
                    self.close_app.touch()
                self.assertEqual(process.wait(timeout=10), 0)
                self.assert_stopped(pids)


if __name__ == '__main__':
    unittest.main()
