#!/usr/bin/env python3
"""A table given at startup (--table) opens in a restored workspace.

A restored workspace's panels come from the window's core, which the startup
command runs before; the tab must still open, and be saved in the workspace's
record. Run with WHEELHOUSE_TEST_BINARY set to the native binary.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import workspace_seed

TEXT = {'content': {'Local': {'Url': {'url': 'wheelhouse:text'}}}, 'presentation': 'text'}


class TableAtStartup(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.binary = str(Path(os.environ['WHEELHOUSE_TEST_BINARY']).resolve())

    def tearDown(self):
        self.dir.cleanup()

    def open_at_startup(self, label):
        root = Path(self.dir.name)
        dashboard = root / 'dashboard'
        dashboard.mkdir()
        (dashboard / 'id').write_text('startup')
        workspace_id = workspace_seed.seed(dashboard, root / 'presentation' / 'startup.wheelhouse', [TEXT], label=label)
        record = dashboard / ('workspace-%s.record' % workspace_id)
        with tempfile.TemporaryFile() as log:
            process = subprocess.Popen([self.binary, '--user:%s/user' % root, '--dashboard:%s' % dashboard,
                                        '--project:%s/project' % root, '--table:records'], stdout=log, stderr=log)
            try:
                urls = []
                deadline = time.monotonic() + 30
                while time.monotonic() < deadline and process.poll() is None:
                    try:
                        slots = json.loads(record.read_text())['slots']['user']
                        urls = [s['spec']['content']['Local']['Url']['url'] for s in slots
                                if 'Url' in s['spec']['content'].get('Local', {})]
                    except (FileNotFoundError, json.JSONDecodeError, KeyError):
                        pass
                    if 'wheelhouse:table?expression=records' in urls:
                        break
                    time.sleep(.1)
                errors = ''.join(p.read_text(errors='replace') for p in root.glob('logs/*.uishell_log'))
                self.assertNotIn('no panel was available', errors)
                log.seek(0)
                self.assertIn('wheelhouse:table?expression=records', urls,
                              'no table tab saved; log:\n' + log.read().decode(errors='replace'))
                self.assertIn('wheelhouse:text', urls)
            finally:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    def test_the_windows_own_workspace(self):
        self.open_at_startup(None)

    def test_a_workspace_of_the_window(self):
        self.open_at_startup('Restored')


if __name__ == '__main__':
    unittest.main()
