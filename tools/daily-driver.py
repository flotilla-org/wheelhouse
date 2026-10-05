#!/usr/bin/env python3
"""Launch Wheelhouse with live git facts and Flotilla's catalog connector."""
import argparse
import contextlib
import errno
import http.client
import importlib.util
import os
from pathlib import Path
import re
import signal
import socket
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[1]
WINDOWS = sys.platform == 'win32'
EXE = '.exe' if WINDOWS else ''
CONNECTOR_STABLE_SECONDS = 30
MAX_CONNECTOR_BACKOFF_SECONDS = 30


def load_tool(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


if WINDOWS:
    import msvcrt
    PipeSocket = load_tool('ingress_pipe', 'ingress-pipe.py').PipeSocket
    WindowsProcesses = load_tool('daily_driver_windows', 'daily-driver-windows.py').WindowsProcesses
else:
    import fcntl


def lock_profile(lock):
    if WINDOWS:
        if lock.seek(0, os.SEEK_END) == 0:
            lock.write(b'\0')
            lock.flush()
        lock.seek(0)
        msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
    else:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)


def default_state():
    if WINDOWS:
        local = Path(os.environ.get('LOCALAPPDATA', Path.home() / 'AppData/Local'))
        return local / 'Wheelhouse/daily-driver'
    return Path(os.environ.get('XDG_CONFIG_HOME', Path.home() / '.config')) / 'wheelhouse/daily-driver'


def flotilla_binary():
    if 'FLOTILLA_BIN' in os.environ:
        return Path(os.environ['FLOTILLA_BIN']).resolve()
    fleet = Path.home() / ('.local/opt/flotilla-fleet/current/bin/flotilla' + EXE)
    if fleet.is_file():
        return fleet.resolve()
    root = Path(os.environ.get('FLOTILLA_ROOT', ROOT.parent / 'flotilla'))
    return (root / ('target/debug/flotilla' + EXE)).resolve()


def mismatch_hint(output):
    mismatch = re.search(r'wire generation mismatch:.*?client fingerprint.*?\(build ([^)]+)\)'
                         r'.*?daemon fingerprint.*?\(build ([^)]+)\)', output, re.DOTALL)
    if mismatch:
        client, daemon = mismatch.groups()
        return (f'Flotilla build mismatch: client {client}, daemon {daemon}; '
                'set FLOTILLA_BIN to a binary matching the daemon '
                '(fleet install: ~/.local/opt/flotilla-fleet/current/bin/flotilla).')


class LocalHTTPConnection(http.client.HTTPConnection):
    def __init__(self, path):
        super().__init__('localhost', timeout=.5)
        self.path = path

    def connect(self):
        if WINDOWS:
            self.sock = PipeSocket(self.path, self.timeout)
            return
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.settimeout(self.timeout)
        self.sock.connect(self.path)


def wait_ready(app, path, timeout=30):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if app.poll() is not None:
            raise RuntimeError(f'Wheelhouse exited with status {app.returncode} before startup')
        connection = LocalHTTPConnection(path)
        try:
            connection.request('GET', '/v1/health')
            if connection.getresponse().status == 204:
                return
        except (OSError, http.client.HTTPException):
            pass
        finally:
            connection.close()
        time.sleep(.1)
    raise RuntimeError('Wheelhouse did not become ready within 30 seconds')


def stop(process):
    if WINDOWS:
        # The helper owns a kill-on-close job containing the actual process and
        # its descendants. Stopping the helper releases that entire tree.
        if process.poll() is None:
            process.terminate()
        process.wait(timeout=5)
        return
    # Children have their own process groups, including producer subprocesses.
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except ProcessLookupError:
        pass
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        os.killpg(process.pid, signal.SIGKILL)
        process.wait()


def run(args, binary, template, state):
    with contextlib.ExitStack() as stack:
        if WINDOWS:
            path = r'\\.\pipe\wheelhouse-daily-' + uuid.uuid4().hex
            processes = stack.enter_context(WindowsProcesses())
        else:
            # Keep Unix socket paths short even on macOS, where TMPDIR is long.
            runtime = stack.enter_context(tempfile.TemporaryDirectory(prefix='wh-daily-', dir='/tmp'))
            path = runtime + '/facts.sock'
        env = {**os.environ, 'WHEELHOUSE_SOCKET': path}
        if args.daemon is not None:
            # UI recipe processes inherit the same endpoint as the connector.
            env['FLOTILLA_DAEMON'] = args.daemon
        # Pane identity belongs to Wheelhouse, not an enclosing Zellij session.
        env.pop('WHEELHOUSE_PANE_ID', None)
        logs = state / 'logs'
        logs.mkdir(exist_ok=True)

        def launch(name, command, append=False):
            with (logs / (name + '.log')).open('a' if append else 'w', encoding='utf-8') as output:
                options = dict(env=env, stdout=output, stderr=subprocess.STDOUT)
                if WINDOWS:
                    return processes.launch(command, **options)
                return subprocess.Popen(command, start_new_session=True, **options)

        print(f'Daily-driver settings: {state}\nLogs: {logs}\nWHEELHOUSE_SOCKET={path}', flush=True)
        app = launch('wheelhouse', [str(binary), '--user:' + str(state / 'user'),
                                   '--project:' + str(state / 'project'),
                                   '--andamento_socket:' + path, '--andamento_config:' + str(template)])
        stack.callback(stop, app)
        wait_ready(app, path)
        producers = []
        if args.repo:
            command = [str(args.watcher), '--transport', 'wheelhouse', '--socket', path]
            for repo in args.repo:
                command.extend(['--roots', str(repo)])
            process = launch('git', command)
            stack.callback(stop, process)
            producers.append(('git', process))
        connector_log_start = 0

        def launch_connector(append=False):
            nonlocal connector_log_start
            # Resolve current again on retries so a fleet roll can recover in place.
            flotilla = flotilla_binary()
            log = logs / 'flotilla.log'
            connector_log_start = log.stat().st_size if append and log.exists() else 0
            return launch('flotilla', [str(flotilla), 'pm', 'connect', '--wheelhouse-socket', path,
                                      '--flotilla-bin', str(flotilla)], append=append)

        connector = None
        connector_started_at = 0
        restart_at = 0
        backoff = 1
        if not args.git_only:
            connector = launch_connector()
            connector_started_at = time.monotonic()

        def stop_connector():
            if connector is not None:
                stop(connector)

        stack.callback(stop_connector)
        print('Wheelhouse is running. Close the app or press Ctrl-C to stop the daily driver.', flush=True)
        while app.poll() is None:
            for name, process in producers:
                if process.poll() is not None:
                    raise RuntimeError(f'{name} producer exited with status {process.returncode}; see {logs / (name + ".log")}')
            if connector is not None and connector.poll() is not None:
                status = connector.returncode
                if time.monotonic() - connector_started_at >= CONNECTOR_STABLE_SECONDS:
                    backoff = 1
                stop(connector)
                connector = None
                with (logs / 'flotilla.log').open('rb') as output:
                    output.seek(connector_log_start)
                    hint = mismatch_hint(output.read().decode(errors='replace'))
                if hint:
                    print(hint, flush=True)
                restart_at = time.monotonic() + backoff
                message = f'Flotilla connector exited with status {status}; restarting in {backoff}s'
                with (logs / 'flotilla.log').open('a', encoding='utf-8') as output:
                    print(message, file=output, flush=True)
                print(message, flush=True)
                backoff = min(backoff * 2, MAX_CONNECTOR_BACKOFF_SECONDS)
            if connector is None and not args.git_only and time.monotonic() >= restart_at:
                try:
                    connector = launch_connector(append=True)
                    connector_started_at = time.monotonic()
                except OSError as error:
                    message = f'Flotilla connector could not start: {error}; retrying in {backoff}s'
                    with (logs / 'flotilla.log').open('a', encoding='utf-8') as output:
                        print(message, file=output, flush=True)
                    print(message, flush=True)
                    restart_at = time.monotonic() + backoff
                    backoff = min(backoff * 2, MAX_CONNECTOR_BACKOFF_SECONDS)
            time.sleep(.2)
        return app.returncode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--no-build', action='store_true', help='use existing Wheelhouse and git watcher binaries')
    parser.add_argument('--no-git', action='store_true', help='omit local git discovery; use provider facts only')
    parser.add_argument('--git-only', action='store_true', help='omit Flotilla; publish only local git facts')
    parser.add_argument('--daemon', help='remote daemon endpoint; overrides FLOTILLA_DAEMON and is inherited by the UI')
    parser.add_argument('--repo', type=Path, action='append', help='Unix git checkout to watch; repeatable, defaults to current directory')
    args = parser.parse_args()
    if args.no_git and (args.git_only or args.repo):
        parser.error('--no-git cannot be combined with --git-only or --repo')
    if WINDOWS and (args.repo or args.git_only):
        parser.error('local git discovery on Windows awaits andamento#123; omit --repo/--git-only')
    if WINDOWS and not (args.daemon or os.environ.get('FLOTILLA_DAEMON')):
        parser.error('Windows requires --daemon ssh://host[/path/to/flotilla] or FLOTILLA_DAEMON; no local daemon is hosted')
    args.repo = [] if args.no_git or WINDOWS else [repo.resolve() for repo in (args.repo or [Path.cwd()])]
    binary = Path(os.environ.get('WHEELHOUSE_BIN', ROOT / ('build/wheelhouse' + EXE))).resolve()
    flotilla = flotilla_binary()
    andamento = Path(os.environ.get('WHEELHOUSE_ANDAMENTO_DIR', os.environ.get('ANDAMENTO_ROOT', ROOT.parent / 'andamento'))).resolve()
    template = Path(os.environ.get('WHEELHOUSE_ANDAMENTO_CONFIG', ROOT / 'data/sidebar/daily-driver.kdl')).resolve()
    state = Path(os.environ.get('WHEELHOUSE_DAILY_DIR', default_state())).resolve()
    if not template.is_file():
        parser.error(f'Andamento template not found: {template}')
    if not args.git_only and not os.access(flotilla, os.X_OK):
        parser.error(f'Flotilla binary not found: {flotilla}; build Flotilla with the HTTP sink or set FLOTILLA_BIN')
    for repo in args.repo:
        subprocess.run(['git', '-C', str(repo), 'rev-parse', '--show-toplevel'], check=True, stdout=subprocess.DEVNULL)
    state.mkdir(mode=0o700, parents=True, exist_ok=True)
    with (state / 'launcher.lock').open('a+b') as lock:
        try:
            lock_profile(lock)
        except OSError as error:
            if error.errno not in (errno.EACCES, errno.EAGAIN, errno.EDEADLK):
                raise
            parser.error(f'a daily driver is already using {state}')
        if not args.no_build and 'WHEELHOUSE_BIN' not in os.environ:
            build = [str(ROOT / 'build.bat'), 'wheelhouse'] if WINDOWS else ['bash', 'build.sh', 'wheelhouse']
            subprocess.run(build, cwd=ROOT,
                           env={**os.environ, 'WHEELHOUSE_ANDAMENTO_DIR': str(andamento)}, check=True)
        if args.repo:
            if 'ANDAMENTO_GIT_WATCHER_BIN' in os.environ:
                args.watcher = Path(os.environ['ANDAMENTO_GIT_WATCHER_BIN']).resolve()
            else:
                version = subprocess.check_output(['rustc', '-vV'], text=True)
                host = next(line.split(': ', 1)[1] for line in version.splitlines() if line.startswith('host: '))
                target = Path(os.environ.get('WHEELHOUSE_ANDAMENTO_TARGET_DIR',
                              os.environ.get('CARGO_TARGET_DIR', andamento / 'target'))).resolve()
                args.watcher = target / host / ('debug/andamento-git-watcher' + EXE)
                if not args.no_build:
                    subprocess.run([sys.executable, str(ROOT / 'tools/prepare-andamento-build.py'), str(andamento)], check=True)
                    subprocess.run(['cargo', 'build', '--manifest-path', str(ROOT / 'build/andamento/Cargo.toml'),
                                    '-p', 'andamento-git-watcher', '--bin', 'andamento-git-watcher', '--locked', '--target', host,
                                    '--target-dir', str(target)], check=True)
            if not os.access(args.watcher, os.X_OK):
                parser.error(f'Andamento git watcher not found: {args.watcher}; build andamento#107 or set ANDAMENTO_GIT_WATCHER_BIN')
        if not os.access(binary, os.X_OK):
            build_hint = 'build.bat wheelhouse' if WINDOWS else 'bash build.sh wheelhouse'
            parser.error(f'Wheelhouse binary not found: {binary}; run {build_hint}')
        return run(args, binary, template, state)


if __name__ == '__main__':
    def interrupted(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, interrupted)
    if WINDOWS:
        signal.signal(signal.SIGBREAK, interrupted)
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f'error: {error}', file=sys.stderr)
        sys.exit(1)
