#!/usr/bin/env python3
"""Exercise build.sh failure propagation with an isolated fake toolchain."""
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class BuildFailures(unittest.TestCase):
    def run_build(self, platform, fail_stage):
        with tempfile.TemporaryDirectory(prefix='wheelhouse-build-test-') as directory:
            root = Path(directory)
            shutil.copy2(ROOT / 'build.sh', root / 'build.sh')
            (root / 'build').mkdir()
            # A prior successful build must not make a failed rebuild look green.
            (root / 'build/wheelhouse').write_text('previous build')
            (root / 'deps').mkdir()
            bin_dir = root / 'bin'
            bin_dir.mkdir()
            tool = bin_dir / 'tool'
            tool.write_text(f'#!{sys.executable}\n' + '''
import os
from pathlib import Path
import sys
name = Path(sys.argv[0]).name
stage = name
if name == 'cc':
    stage = 'compile' if '-c' in sys.argv else 'link'
with open(os.environ['BUILD_TEST_LOG'], 'a') as log:
    log.write(stage + '\\n')
if stage == os.environ['BUILD_TEST_FAIL']:
    print('injected failure: ' + stage, file=sys.stderr)
    sys.exit(23)
if name == 'uname':
    print(os.environ['BUILD_TEST_PLATFORM'])
elif name == 'rustc':
    print('host: test-target')
elif name == 'git':
    print('test-revision')
elif name == 'cc':
    Path(sys.argv[sys.argv.index('-o') + 1]).write_text('new ' + stage)
''')
            tool.chmod(0o755)
            for name in ('cc', 'cargo', 'rustc', 'python3', 'uname', 'git',
                         'dsymutil', 'codesign', 'mdimport'):
                (bin_dir / name).symlink_to(tool)
            log = root / 'calls'
            env = {key: value for key, value in os.environ.items()
                   if not key.startswith('WHEELHOUSE_')}
            env.update(PATH=str(bin_dir) + os.pathsep + os.environ['PATH'],
                       CC=str(bin_dir / 'cc'), BUILD_TEST_LOG=str(log),
                       BUILD_TEST_FAIL=fail_stage, BUILD_TEST_PLATFORM=platform,
                       WHEELHOUSE_CLEAT_DIR=str(root / 'deps'),
                       WHEELHOUSE_JACKSTAY_DIR=str(root / 'deps'),
                       WHEELHOUSE_ANDAMENTO_DIR=str(root / 'deps'))
            result = subprocess.run(['bash', str(root / 'build.sh'), 'wheelhouse'],
                                    env=env, capture_output=True, text=True, timeout=10)
            calls = log.read_text().splitlines()
            artifact = (root / 'build/wheelhouse').read_text()
            return result, calls, artifact

    def test_failures_stop_the_build(self):
        for platform, stages in (
                ('Linux', ('cargo', 'compile', 'link')),
                ('Darwin', ('cargo', 'compile', 'link', 'dsymutil', 'codesign'))):
            for stage in stages:
                with self.subTest(platform=platform, stage=stage):
                    result, calls, artifact = self.run_build(platform, stage)
                    self.assertEqual(result.returncode, 23, result.stdout + result.stderr)
                    self.assertEqual(calls[-1], stage, calls)
                    if stage in ('cargo', 'compile', 'link'):
                        self.assertEqual(artifact, 'previous build')

    def test_success(self):
        for platform in ('Linux', 'Darwin'):
            with self.subTest(platform=platform):
                result, calls, artifact = self.run_build(platform, '')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(artifact, 'new link')
                self.assertEqual(calls[-1], 'codesign' if platform == 'Darwin' else 'link')


if __name__ == '__main__':
    unittest.main()
