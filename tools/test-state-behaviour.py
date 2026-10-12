#!/usr/bin/env python3
"""Check restart behaviour against golden logical-state snapshots.

Compiles src/uishell/tests/state_behaviour.c into the production amalgamation
(entry point replaced, headless WM/renderer/fonts, real Andamento and Cleat),
runs its scenario, and compares the logical state before and after an
in-process restart with the goldens in src/uishell/tests/state_behaviour/.

The after-restart golden is the restarted state with each line that differs
from before the restart preceded by a comment naming the known loss and the
migration step that fixes it (docs/roadmap.md, "State model migration"). A
difference no known loss explains fails the test, as does any golden diff.
Pass --update-goldens to rewrite them after an intended change.
"""
import argparse
import difflib
import os
import re
from pathlib import Path
import shlex
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'build'
GOLDENS = ROOT / 'src/uishell/tests/state_behaviour'
BUILD.mkdir(exist_ok=True)
parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('andamento_lib', type=Path)
parser.add_argument('cleat_lib', type=Path)
parser.add_argument('--andamento-include', type=Path,
                    default=Path(os.environ.get('WHEELHOUSE_ANDAMENTO_DIR', ROOT.parent/'andamento'))/'crates/andamento-ffi/include')
parser.add_argument('--cleat-include', type=Path,
                    default=Path(os.environ.get('WHEELHOUSE_CLEAT_DIR', ROOT.parent/'cleat'))/'crates/cleat/include')
parser.add_argument('--update-goldens', action='store_true', help='rewrite the goldens from this run')
args = parser.parse_args()

# Reuse the production amalgamation's includes, replacing only its entry
# point, as tools/test-docking-integration.py does.
main = (ROOT / 'src/uishell/uishell_main.c').read_text()
prefix = main[:main.index('internal void\nentry_point(CmdLine *cmd_line)')]
arena_table_definition = '#define ARENA_TABLE_DEBUG BUILD_DEBUG'
assert arena_table_definition in prefix, 'production arena table definition changed; review the capped harness boundary'
# The debug arena table's 256 GiB reservation exceeds the native 8 GiB cap.
prefix = prefix.replace(arena_table_definition, '#define ARENA_TABLE_DEBUG 0')
prefix = re.sub(r'^#include "uishell/[^"]*diagnostics.c"\n', '', prefix, flags=re.M)
source = BUILD / 'state_behaviour.c'
source.write_text(prefix + '\n#include "uishell/tests/state_behaviour.c"\n')
binary = BUILD / 'state_behaviour'
andamento, cleat = args.andamento_lib.resolve(), args.cleat_lib.resolve()
command = shlex.split(os.environ.get('CC', 'clang')) + [
    '-g', '-O0', '-D_GNU_SOURCE', '-DBUILD_DEBUG=1', '-DNO_ASYNC=1',
    '-DWM_STUB=1', '-DR_BACKEND=0', '-DFP_BACKEND=0', '-ffunction-sections', '-fdata-sections',
    '-Wno-initializer-overrides', '-Wno-unused-value',
    '-Wno-incompatible-pointer-types-discards-qualifiers',
    '-I'+str(ROOT/'src'), '-I'+str(args.andamento_include.resolve()),
    '-I'+str(args.cleat_include.resolve()), str(source),
    '-L'+str(andamento), '-landamento_ffi', '-lwheelhouse_ingress', '-Wl,-rpath,'+str(andamento),
    '-L'+str(cleat), '-lcleat', '-Wl,-rpath,'+str(cleat), '-lpthread', '-lm', '-ldl',
    '-Wl,--gc-sections', '-o', str(binary)]
subprocess.run(command, check=True)

# A fresh user file each run: the scenario starts from a first launch.
run_dir = BUILD / 'state_behaviour_run'
shutil.rmtree(run_dir, ignore_errors=True)
run_dir.mkdir()
subprocess.run([str(binary), '--async_thread_count:1', '--state_dir:'+str(run_dir),
                '--user:'+str(run_dir/'user.wheelhouse')], cwd=ROOT, check=True, timeout=60)
before = (run_dir/'before_restart.txt').read_text().splitlines()
after = (run_dir/'after_restart.txt').read_text().splitlines()

# Known losses across a restart today, and the migration step that ends each.
STEP2 = 'fixed by step 2 (#315)'
STEP3 = 'fixed by step 3 (#307)'
PRESENTATION = re.compile(r'^\s*(visible workspace|focused panel|input) ')
SUBJECT_ROW = re.compile(r'^\s*row [^.\s][^/\s]*/\S+ .* live\b')
LOCAL_ROW = re.compile(r'^\s*row \.workspace (".*") live$')


def subject_workspace(lines, quoted_label):
    return any(re.match(r'^\s*workspace %s subject=' % re.escape(quoted_label), line) for line in lines)


def changed_pair(old, new):
    """Why `old` became `new` across the restart, or None."""
    if old.endswith(' collapsed') and old[:-len(' collapsed')] == new:
        return 'row collapse is not saved, %s; was collapsed' % STEP3
    if PRESENTATION.match(old) and PRESENTATION.match(new) and PRESENTATION.match(old).group(1) == PRESENTATION.match(new).group(1):
        return 'the visible workspace and focus are not saved, %s; was %s' % (STEP2, old.strip())
    return None


def removed(old):
    if SUBJECT_ROW.match(old):
        return 'the row of an open workspace whose subject ended or went is not kept, %s; was %s' % (STEP3, old.strip())
    return None


def added(new):
    match = LOCAL_ROW.match(new)
    if match and subject_workspace(after, match.group(1)):
        return 'this subject workspace is no longer bound to its subject (it ended or went), %s' % STEP3
    return None


def comment(line, text):
    return line[:len(line)-len(line.lstrip())] + '# ' + text


annotated, unexplained = [], []
for op, i1, i2, j1, j2 in difflib.SequenceMatcher(None, before, after, autojunk=False).get_opcodes():
    olds, news = before[i1:i2], after[j1:j2]
    if op == 'equal':
        annotated += news
        continue
    if op == 'replace' and len(olds) == len(news):
        # Lines changed in place: why, above each new form.
        gone, came = [], [changed_pair(o, n) for o, n in zip(olds, news)]
    else:
        # Lines that went are noted where they were; lines that came, above themselves.
        gone, came = [removed(o) for o in olds], [added(n) for n in news]
    if not all(gone + came):
        unexplained.append('\n'.join(['-' + o for o in olds] + ['+' + n for n in news]))
    annotated += [comment(o, why or 'unexplained change') for o, why in zip(olds, gone)]
    for n, why in zip(news, came):
        annotated += [comment(n, why or 'unexplained change'), n]

HEADER = ('# Generated by tools/test-state-behaviour.py --update-goldens from the logical state\n'
          '# (src/uishell/uishell_logical_state.c) %s.\n')
golden_text = {
    'before_restart.txt': HEADER % 'after the scenario in src/uishell/tests/state_behaviour.c' + '\n'.join(before) + '\n',
    'after_restart.txt': HEADER % 'after restarting; "#" lines name what the restart lost and the step that fixes it' +
                         '\n'.join(annotated) + '\n',
}
if unexplained:
    print('Restart changed state in ways no known loss explains:\n\n' + '\n\n'.join(unexplained), file=sys.stderr)
    sys.exit(1)
if args.update_goldens:
    GOLDENS.mkdir(parents=True, exist_ok=True)
    for name, text in golden_text.items():
        (GOLDENS/name).write_text(text)
    print('Updated goldens in %s' % GOLDENS.relative_to(ROOT))
    sys.exit(0)
failed = False
for name, text in golden_text.items():
    golden = GOLDENS/name
    expected = golden.read_text() if golden.exists() else ''
    if expected != text:
        failed = True
        sys.stderr.writelines(difflib.unified_diff(expected.splitlines(True), text.splitlines(True),
                                                   str(golden.relative_to(ROOT)), 'this run'))
if failed:
    print('Logical state differs from the goldens; if intended, rerun with --update-goldens.', file=sys.stderr)
    sys.exit(1)
print('State behaviour goldens match')
