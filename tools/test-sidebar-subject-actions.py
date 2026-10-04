#!/usr/bin/env python3
"""Exercise native subject click/copy against X11 and a real clipboard reader.

Run under an X11 display (or xvfb-run), with xdotool and xclip installed.
Browser launch is the only fake: a subprocess boundary records the URL.
"""
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]


def main():
    for command in ('xdotool', 'xclip'):
        if not shutil.which(command):
            raise SystemExit(f'{command} is required for the interactive subject test')
    with tempfile.TemporaryDirectory(prefix='wheelhouse-subject-actions-') as temp:
        directory = Path(temp)
        browser = directory / 'xdg-open'
        browser.write_text('#!/bin/sh\nprintf "%s\\n" "$1" >> "$WHEELHOUSE_TEST_URLS"\n')
        browser.chmod(0o755)
        urls = directory / 'urls'
        env = {**os.environ, 'PATH': str(directory) + os.pathsep + os.environ['PATH'],
               'WHEELHOUSE_TEST_URLS': str(urls)}
        project = directory / 'subject-actions-project'
        geometry = directory / 'geometry.json'
        user = directory / 'user'
        user.write_text('window:\n{\n  size: 1280 720\n  control_split_pct: 0.32\n}\n')
        with (directory / 'app.log').open('w') as log:
            app = subprocess.Popen([str(ROOT / 'build/wheelhouse'), '--sidebar_subject_fixture',
                                    '--user:' + str(user), '--project:' + str(project),
                                    '--sidebar_subject_geometry:' + str(geometry)],
                                   env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 10
                window = None
                while time.monotonic() < deadline and app.poll() is None:
                    found = subprocess.run(['xdotool', 'search', '--onlyvisible', '--name', '^subject-actions-project - Wheelhouse'],
                                           capture_output=True, text=True)
                    if found.returncode == 0:
                        window = found.stdout.splitlines()[-1]
                        break
                    time.sleep(.05)
                assert window, (directory / 'app.log').read_text()
                subprocess.run(['xdotool', 'windowmove', window, '0', '0', 'windowsize', window, '1280', '720',
                                'windowfocus', window], check=True)
                time.sleep(.5)

                def click(x, y, button=1):
                    subprocess.run(['xdotool', 'mousemove', str(x), str(y), 'click', str(button)], check=True)
                    time.sleep(.15)

                def clipboard(target=None):
                    command = ['xclip', '-selection', 'clipboard', '-o']
                    if target:
                        command += ['-target', target]
                    return subprocess.check_output(command, text=True, timeout=3)

                def hit(identity, action='subject', chip=True):
                    deadline = time.monotonic() + 5
                    while time.monotonic() < deadline:
                        try:
                            records = json.loads(geometry.read_text())
                            record = next(r for r in records if r['id'] == identity and
                                          r['action'] == action and r['chip'] == chip)
                            x0, y0, x1, y1 = record['rect']
                            return round((x0 + x1) / 2), round((y0 + y1) / 2)
                        except (OSError, ValueError, StopIteration):
                            time.sleep(.05)
                    raise AssertionError(f'No {action} geometry for {identity}, chip={chip}')

                # Use real laid-out rectangles so widths, tiers and chip order
                # cannot silently turn this test back into child-row clicks.
                click(*hit('pr-281'))
                assert urls.read_text().splitlines() == ['https://forge.example/org/wheelhouse/review/281']
                for chip in (True, False):
                    click(*hit('pr-281', chip=chip), button=3)
                    click(*hit('pr-281', action='copy', chip=chip))
                    assert clipboard() == 'https://forge.example/org/wheelhouse/review/281'
                    assert clipboard('UTF8_STRING') == 'https://forge.example/org/wheelhouse/review/281'
                click(*hit('no-forge'))
                assert len(urls.read_text().splitlines()) == 1
                click(*hit('no-forge'), button=3)
                click(*hit('no-forge', action='copy'))
                assert clipboard() == '!2508'
                click(*hit('issue-137'))
                assert urls.read_text().splitlines()[-1] == 'https://forge.example/org/wheelhouse/ticket/137'
                click(*hit('issue-137'), button=3)
                click(*hit('issue-137', action='copy'))
                assert clipboard() == 'https://forge.example/org/wheelhouse/ticket/137'
                print('Native subject URL and clipboard scenarios passed')
            finally:
                app.terminate()
                try:
                    app.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    app.kill()
                    app.wait()


if __name__ == '__main__':
    main()
