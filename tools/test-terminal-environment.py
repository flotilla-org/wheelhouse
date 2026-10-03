#!/usr/bin/env python3
"""Exercise the native startup environment policy without opening a window."""
import os
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='wh-terminal-env-') as directory:
    output = Path(directory) / ('test.exe' if os.name == 'nt' else 'test')
    if os.name == 'nt':
        command = ['cl', '/nologo', '/W4', '/I' + str(ROOT / 'src'),
                   '/Fe:' + str(output), '/Fo:' + str(Path(directory) / 'test.obj'),
                   str(ROOT / 'tools/test-terminal-environment.c')]
    else:
        command = [os.environ.get('CC', 'clang'), '-std=c11',
                   '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(ROOT / 'src'), str(ROOT / 'tools/test-terminal-environment.c'),
                   '-o', str(output)]
    subprocess.run(command, cwd=directory, check=True)
    subprocess.run([str(output)], check=True)
