#!/usr/bin/env python3
"""Fail when committed generated sources differ from what the last build wrote.

Run after a build that regenerates them. Metagen output is only rewritten by
`build wheelhouse meta` (Windows); metagen walks src/ in directory order, so
Windows is the reference platform for it. Every build rewrites the embedded
sidebar fixture.
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATHSPEC = [':(glob)src/**/generated/**']


def main():
    changed = subprocess.run(['git', 'diff', '--stat', '--exit-code', '--', *PATHSPEC], cwd=ROOT)
    untracked = subprocess.run(['git', 'ls-files', '--others', '--exclude-standard', '--', *PATHSPEC],
                               cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip()
    if untracked:
        print('Untracked generated files:\n' + untracked)
    if changed.returncode != 0 or untracked:
        print('Generated sources are out of date; commit the regenerated files '
              '(`build wheelhouse meta` on Windows).', file=sys.stderr)
        return 1
    print('Generated sources match the committed copies.')
    return 0


if __name__ == '__main__':
    sys.exit(main())
