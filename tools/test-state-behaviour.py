#!/usr/bin/env python3
"""Check restart behaviour against golden logical-state snapshots.

Compiles src/uishell/tests/state_behaviour.c into the production amalgamation
(entry point replaced, headless WM/renderer/fonts, real Andamento and Cleat),
runs its scenario, and compares the logical state before and after an
in-process restart with the goldens in src/uishell/tests/state_behaviour/.

The after-restart golden is the restarted state with each line that differs
from before the restart preceded by a comment naming the known loss and
where it is decided (docs/roadmap.md, "State model migration"). A difference
no known loss explains fails the test, as does any golden diff.
Its subscriptions part runs a stand-in for `flotilla pm connect` (written
here, given as --flotilla_bin) for each of two Flotilla subscriptions, and
keeps their rows, as the logical state shows them at each stage, in
subscriptions.txt.

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
import tempfile

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

# A stand-in for `flotilla pm connect`: it publishes project "fleet" with
# vessel "fleet-v", labelled with its daemon's host, to the endpoint it is
# given, every half second (facts last two), until the scenario asks it to
# stop by making stop-<host> in the run directory.
FAKE_CONNECTOR = r'''#!/usr/bin/env python3
import http.client, json, os, socket, sys, time
args = sys.argv[1:]
assert args[:2] == ['pm', 'connect'] and '--flotilla-bin' in args, args
endpoint = args[args.index('--wheelhouse-socket') + 1]
assert os.environ['WHEELHOUSE_SOCKET'] == endpoint
host = os.environ.get('FLOTILLA_DAEMON', 'default').split('://')[-1]
stop = os.path.join(os.environ['STATE_BEHAVIOUR_DIR'], 'stop-' + host)
def facts(**values):
    return {key: {'value': {'type': 'text', 'value': value}, 'ttl_ms': 2000} for key, value in values.items()}
patches = [
    {'type': 'metadata-patch', 'target': {'kind': 'entity', 'value': {'kind': 'project', 'id': 'fleet'}},
     'source_id': 'fake-connector', 'unset': [],
     'set': facts(**{'flotilla.project': 'fleet', 'display.label': 'Fleet via ' + host})},
    {'type': 'metadata-patch', 'target': {'kind': 'entity', 'value': {'kind': 'vessel', 'id': 'fleet-v'}},
     'source_id': 'fake-connector', 'unset': [],
     'set': {**facts(**{'flotilla.project': 'fleet', 'flotilla.vessel': 'fleet-v', 'display.label': 'Fleet vessel via ' + host,
                        'status.state': 'waiting', 'action.primary.recipe': "printf 'Fleet vessel\\n'; exec /bin/sh"}),
             'status.attention': {'value': {'type': 'bool', 'value': True}, 'ttl_ms': 2000}}},
]
while not os.path.exists(stop):
    for patch in patches:
        connection = http.client.HTTPConnection('localhost', timeout=10)
        try:
            connection.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            connection.sock.connect(endpoint)
            connection.request('POST', '/v1/metadata/patch', json.dumps(patch), {'Content-Type': 'application/json'})
            # 503 while the scenario isn't between frames; the next round resends.
            status = connection.getresponse().status
            assert status in (204, 503), status
        except (OSError, http.client.HTTPException) as error:
            print('publish failed:', error, flush=True)
        finally:
            connection.close()
    time.sleep(.5)
sys.exit(3)
'''

# A fresh user file and Dashboard each run: the scenario starts from a first
# launch, with the Dashboard directory given as --dashboard gives it.
run_dir = BUILD / 'state_behaviour_run'
shutil.rmtree(run_dir, ignore_errors=True)
run_dir.mkdir()
connector = run_dir / 'fake-flotilla'
connector.write_text(FAKE_CONNECTOR)
connector.chmod(0o755)
# Unix socket paths are short; a private directory of its own holds the
# local endpoint's name, which subscriptions' endpoints are named after.
with tempfile.TemporaryDirectory(prefix='wh-sb-', dir='/tmp') as sockets:
    subprocess.run([str(binary), '--async_thread_count:1', '--state_dir:'+str(run_dir),
                    '--user:'+str(run_dir/'user.wheelhouse'), '--dashboard:'+str(run_dir/'dashboard-a'),
                    '--andamento_socket:'+sockets+'/local.sock', '--flotilla_bin:'+str(connector)],
                   cwd=ROOT, check=True, timeout=90, env={**os.environ, 'STATE_BEHAVIOUR_DIR': str(run_dir)})
before = (run_dir/'before_restart.txt').read_text().splitlines()
after = (run_dir/'after_restart.txt').read_text().splitlines()

# Known losses across a restart today, and what ends each.
# A subject no producer reports is drawn from its workspace record, which
# keeps its label, whether it ended, and the facts placement reads; a status
# it had is not among them (andamento#151, "Decisions").
STATUS = re.compile(r'^(\s*row \S+ .*) status="[^"]*"$')


def changed_pair(old, new):
    """Why `old` became `new` across the restart, or None."""
    match = STATUS.match(old)
    if match and match.group(1) == new:
        return ('the status of a subject no producer reports is not recorded (its record keeps the facts placement '
                'reads, andamento#151); was %s' % old.strip())
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
        # Lines that went are noted where they were; lines that came, above
        # themselves. No known loss explains either.
        gone, came = [None]*len(olds), [None]*len(news)
    if not all(gone + came):
        unexplained.append('\n'.join(['-' + o for o in olds] + ['+' + n for n in news]))
    annotated += [comment(o, why or 'unexplained change') for o, why in zip(olds, gone)]
    for n, why in zip(news, came):
        annotated += [comment(n, why or 'unexplained change'), n]

HEADER = ('# Generated by tools/test-state-behaviour.py --update-goldens from the logical state\n'
          '# (src/uishell/uishell_logical_state.c) %s.\n')
# Each subscriptions stage: the Dashboard's subscriptions, and the rows and
# workspaces of a subscription's entities.
SUBSCRIPTION_STAGES = [
    ('subscriptions_two.txt', 'two subscriptions publishing the same project and vessel; alpha\'s vessel opened'),
    ('subscriptions_stale.txt', 'beta\'s connector down: its rows kept, stale'),
    ('subscriptions_fresh.txt', 'beta\'s connector back and publishing'),
    ('subscriptions_removed.txt', 'alpha removed: its facts retracted, its open workspace kept'),
    ('subscriptions_restarted.txt', 'after a restart: beta reconnected from the Dashboard'),
]
subscriptions = []
for name, title in SUBSCRIPTION_STAGES:
    subscriptions.append('## ' + title)
    subscriptions += [line for line in (run_dir/name).read_text().splitlines()
                      if line.lstrip().startswith('subscription') or ' provider=' in line]
golden_text = {
    'before_restart.txt': HEADER % 'after the scenario in src/uishell/tests/state_behaviour.c' + '\n'.join(before) + '\n',
    'after_restart.txt': HEADER % 'after restarting; "#" lines name what the restart lost and why' +
                         '\n'.join(annotated) + '\n',
    'subscriptions.txt': HEADER % 'at each stage of the subscriptions scenario: subscriptions, and lines naming a provider' +
                         '\n'.join(subscriptions) + '\n',
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
