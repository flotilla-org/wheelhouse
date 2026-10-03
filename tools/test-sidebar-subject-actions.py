#!/usr/bin/env python3
"""Exercise native subject click/copy against X11 and a real clipboard reader.

Run under an X11 display (or xvfb-run), with xdotool and xclip installed.
Browser launch is the only fake: a subprocess boundary records the URL.
"""
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
        with (directory / 'app.log').open('w') as log:
            app = subprocess.Popen([str(ROOT / 'build/wheelhouse'), '--sidebar_subject_fixture',
                                    '--user:' + str(directory / 'user'), '--project:' + str(project)],
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

                # A click opens the canonical URL through the forge's custom template.
                click(85, 148)
                assert urls.read_text().splitlines() == ['https://forge.example/org/wheelhouse/review/281']
                # Both tree and detached Attention copies yield the external canonical URL.
                for row, menu_copy in [(148, 198), (510, 560)]:
                    click(85, row, 3)
                    click(95, menu_copy)
                    assert clipboard() == 'https://forge.example/org/wheelhouse/review/281'
                    assert clipboard('UTF8_STRING') == 'https://forge.example/org/wheelhouse/review/281'
                # A missing forge copies the subject's own short reference and cannot open a browser.
                click(85, 196)
                assert len(urls.read_text().splitlines()) == 1
                click(85, 196, 3)
                click(95, 226)
                assert clipboard() == '!2508'
                # Issues use their own forge template rather than a hard-coded /issues path.
                click(85, 220)
                assert urls.read_text().splitlines()[-1] == 'https://forge.example/org/wheelhouse/ticket/137'
                click(85, 220, 3)
                click(95, 270)
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
