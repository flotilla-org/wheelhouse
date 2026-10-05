#!/usr/bin/env python3
"""Run native sidebar timings with an 8 GiB process cap and RSS regression gate.

On Linux, limit application worker/stripe sizing to one CPU, independently of
host size, and use one llvmpipe worker. This measures UI builds, not GPU draws.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, default=ROOT / 'build/wheelhouse')
parser.add_argument('--time', default='/usr/bin/time', help='GNU time executable')
parser.add_argument('--sizes', type=int, nargs='+', default=[100, 300, 1000])
parser.add_argument('--output', type=Path, default=ROOT / 'build/sidebar-benchmark')
args = parser.parse_args()
if not shutil.which('prlimit'):
    parser.error('Linux prlimit is required; every native run must be capped')
if args.sizes != sorted(args.sizes) or not args.sizes or args.sizes[0] > 100 or any(n < 0 or n > 1000 for n in args.sizes):
    parser.error('start at 100 issues or fewer, then increase; maximum 1000')
if 1000 in args.sizes and 300 not in args.sizes:
    parser.error('run 300 issues before 1000')
args.output.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory(prefix='sidebar-benchmark-') as temporary:
    task = Path(temporary)
    # Process-boundary adapter: make worker/stripe sizing reproducible without
    # changing production CPU discovery or requiring privileged host setup.
    source = task / 'cpus.c'
    source.write_text('int get_nprocs(void) { return 1; }\n')
    library = task / 'cpus.so'
    subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', str(source), '-o', str(library)], check=True)
    env = dict(os.environ, LD_PRELOAD=str(library) + (':' + os.environ['LD_PRELOAD'] if os.environ.get('LD_PRELOAD') else ''),
               MALLOC_ARENA_MAX='2', LP_NUM_THREADS='1')
    for size in args.sizes:
        for uncached in (True, False):
            mode = 'linear' if uncached else 'lookup'
            identity = f'{size}-{mode}'
            command = [args.time, '-v', 'prlimit', '--as=8589934592', '--', str(args.binary.resolve()),
                       '--async_thread_count:1', f'--user:{task / (identity + "-user")}',
                       f'--project:{task / (identity + "-project")}', '--sidebar_benchmark',
                       f'--sidebar_benchmark_issues:{size}']
            if uncached:
                command += ['--sidebar_benchmark_uncached']
            result = subprocess.run(command, cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, timeout=180)
            (args.output / (identity + '.log')).write_text(result.stdout)
            print(result.stdout, end='', flush=True)
            if result.returncode:
                raise SystemExit(f'{identity}: native benchmark failed ({result.returncode}); stop before increasing catalog size')
            # At fixed size, post-warmup RSS should plateau. Two MiB accommodates
            # page residency/background workers while catching per-frame growth.
            samples = re.findall(r'SIDEBAR_RSS.*frame=(\d+) VmRSS:\s*(\d+) kB', result.stdout)
            groups = [samples[i:i+6] for i in range(0, len(samples), 6)]
            if len(groups) != 4 or any(len(g) != 6 or int(g[-1][1])-int(g[0][1]) > 2048 for g in groups):
                raise SystemExit(f'{identity}: RSS did not plateau; stop before increasing catalog size')
