#!/usr/bin/env python3
"""macOS native Jackstay input acceptance; requires Accessibility permission.

Build Wheelhouse first. This raises a temporary test window and sends native
input only to that window. The source is a separate instrumented process.
"""
import os
from pathlib import Path
import queue
import subprocess
import sys
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parent.parent
JACKSTAY = Path(os.environ.get('WHEELHOUSE_JACKSTAY_DIR', ROOT.parent / 'jackstay')).resolve()
LIB = Path(os.environ.get('WHEELHOUSE_JACKSTAY_TARGET_DIR', JACKSTAY / 'target')) / 'debug'
BINARY = Path(os.environ.get('WHEELHOUSE_BIN', ROOT / 'build/wheelhouse')).resolve()
if sys.platform != 'darwin':
    raise SystemExit('This native driver requires macOS; session checks are portable.')

with tempfile.TemporaryDirectory(prefix='wh-js-native-', dir='/tmp') as temporary:
    work = Path(temporary)
    subprocess.run(['clang', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(JACKSTAY / 'crates/jackstay/include'),
                    str(ROOT / 'tools/jackstay-native-source.c'), '-L'+str(LIB),
                    '-ljackstay', '-Wl,-rpath,'+str(LIB), '-o', str(work/'source')], check=True)
    subprocess.run(['swiftc', str(ROOT / 'tools/jackstay-native-driver.swift'),
                    '-o', str(work/'driver')], check=True)
    path = work/'source.sock'
    (work/'user').write_text('window:\n{\n size: 1100 760\n panels: selected jackstay:\n {\n '
                            f'source_socket: "{path}"\n selected\n' + ' }\n}\n')
    lines = queue.Queue()
    source = subprocess.Popen([str(work/'source'), str(path)], stdin=subprocess.PIPE,
                              stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
    threading.Thread(target=lambda: [lines.put(line.strip()) for line in source.stdout], daemon=True).start()
    app = None
    history = []

    def until(predicate, timeout=8):
        end = time.monotonic()+timeout
        while time.monotonic() < end:
            try:
                line = lines.get(timeout=.1)
            except queue.Empty:
                continue
            history.append(line)
            if predicate(line):
                return line
        raise AssertionError('Source condition timed out: '+repr(history[-15:]))

    def drive(*args):
        subprocess.run([str(work/'driver'), str(app.pid), *map(str,args)], check=True,
                       stdout=subprocess.DEVNULL)

    def snapshot():
        source.stdin.write('s');source.stdin.flush()
        return list(map(int, until(lambda line: line.startswith('snapshot ')).split()[1:]))

    try:
        until(lambda line: line == 'ready')
        with (work/'app.log').open('w') as log:
            app = subprocess.Popen([str(BINARY), '--user:'+str(work/'user'),
                                    '--project:'+str(work/'project')], stdout=log, stderr=log)
            deadline = time.monotonic()+20
            while True:
                probe = subprocess.run([str(work/'driver'),str(app.pid),'activate'], capture_output=True, text=True)
                if probe.returncode == 0:
                    break
                assert app.poll() is None, 'Wheelhouse exited before its window opened'
                assert time.monotonic() < deadline, probe.stderr
                time.sleep(.2)
            time.sleep(.3)
            drive('click',400,140)
            until(lambda line: line == 'accepted')
            drive('click',600,400)  # Admission is still gated: this must not replay.
            source.stdin.write('g');source.stdin.flush()
            until(lambda line: line == 'connected')
            time.sleep(.4)
            assert snapshot()[0] == 0, 'connection-time click replayed'
            print('PASS: connection-time click is not replayed', flush=True)

            drive('click',600,400)
            until(lambda line: line.startswith('state ') and int(line.split()[2]) == 1)
            assert snapshot()[:3] == [1,1,0], 'ready click did not deliver down and up'
            drive('key',0)
            until(lambda line: line.startswith('state ') and int(line.split()[6]) == 1 and int(line.split()[4]) == 0)
            assert snapshot()[6] == 1, 'native text was not delivered'
            print('PASS: ready click and physical key/text delivery', flush=True)

            drive('down',600,400)
            until(lambda line: line.startswith('state ') and int(line.split()[3]) != 0)
            before = snapshot()
            drive('resize',950,650)
            until(lambda line: line.startswith('state ') and int(line.split()[5]) > before[4])
            after = snapshot()
            assert after[2:4] == [0,0], 'resize left input held'
            drive('up',600,400);time.sleep(.2)
            assert snapshot()[0] == before[0], 'resize replayed old press'
            print('PASS: resizing while held resets input without replay', flush=True)

            drive('click',600,350)
            time.sleep(.15)
            keys = snapshot()[5]
            drive('escape');drive('key',0);time.sleep(.15)
            assert snapshot()[5] == keys, 'escape chord did not return keyboard ownership'
            drive('click',600,350);drive('key',0)
            until(lambda line: line.startswith('state ') and int(line.split()[6]) > keys)
            print('PASS: escape releases focus and clicking restores input', flush=True)
            cleanup = snapshot()[4]
            drive('close')
            until(lambda line: line.startswith('state ') and int(line.split()[5]) > cleanup
                  and int(line.split()[3]) == 0 and int(line.split()[4]) == 0)
            app.wait(timeout=12);source.wait(timeout=12)
            assert app.returncode == 0 and source.returncode == 0
            print('PASS: clean native shutdown', flush=True)
    except Exception:
        if app and app.poll() is None:
            probe = subprocess.run([str(work/'driver'), str(app.pid), 'window-id'], capture_output=True, text=True)
            if probe.returncode == 0:
                subprocess.run(['screencapture','-x','-l',probe.stdout.strip(),'/tmp/wh-native-failure.png'])
        print('\n'.join(history[-20:]), file=sys.stderr)
        if (work/'app.log').exists():
            print((work/'app.log').read_text()[-4000:], file=sys.stderr)
        raise
    finally:
        for process in (app, source):
            if process and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=12)
                except subprocess.TimeoutExpired:
                    process.kill();process.wait()
