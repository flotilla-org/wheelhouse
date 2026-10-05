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
import signal
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--binary', type=Path, default=ROOT / 'build/wheelhouse')
parser.add_argument('--time', default='/usr/bin/time', help='GNU time executable')
parser.add_argument('--timeout', type=float, default=180, help='seconds allowed per native run')
parser.add_argument('--sizes', type=int, nargs='+', default=[100, 300, 1000])
parser.add_argument('--output', type=Path, default=ROOT / 'build/sidebar-benchmark')
args = parser.parse_args()
if args.timeout <= 0:
    parser.error('timeout must be positive')
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
            with subprocess.Popen(command, cwd=ROOT, env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.STDOUT, text=True, start_new_session=True) as process:
                timed_out = False
                try:
                    output, _ = process.communicate(timeout=args.timeout)
                except subprocess.TimeoutExpired:
                    # GNU time/prlimit wrap the native process: terminate the
                    # whole group so a timeout cannot leave a benchmark running.
                    timed_out = True
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass # The group exited between timeout and termination.
                    output, _ = process.communicate()
                log = args.output / (identity + '.log')
                log.write_text(output)
                print(output, end='', flush=True)
                if timed_out:
                    raise SystemExit(f'{identity}: timed out after {args.timeout:g}s; partial log: {log}')
                if process.returncode:
                    raise SystemExit(f'{identity}: native benchmark failed ({process.returncode}); stop before increasing catalog size')
            # At fixed size, post-warmup RSS should plateau. Two MiB accommodates
            # page residency/background workers while catching per-frame growth.
            config = re.search(r'SIDEBAR_CONFIG frames=(\d+) warmup=(\d+) sample_interval=(\d+) combinations=(\d+)', output)
            if not config:
                raise SystemExit(f'{identity}: missing sampling configuration')
            frames, warmup, interval, combinations = map(int, config.groups())
            if not (0 < warmup < frames and 0 < interval <= frames and combinations > 0):
                raise SystemExit(f'{identity}: invalid sampling configuration')
            expected_frames = list(range(interval, frames+1, interval))
            if sum(frame >= warmup for frame in expected_frames) < 2:
                raise SystemExit(f'{identity}: insufficient post-warmup RSS samples')
            samples = re.findall(r'SIDEBAR_RSS.*frame=(\d+) VmRSS:\s*(\d+) kB', output)
            count = len(expected_frames)
            groups = [samples[i:i+count] for i in range(0, len(samples), count)] if count else []
            if (len(groups) != combinations or
                any([int(frame) for frame, _ in g] != expected_frames for g in groups) or
                any(int(g[-1][1])-int(next(rss for frame, rss in g if int(frame) >= warmup)) > 2048 for g in groups)):
                raise SystemExit(f'{identity}: RSS did not plateau; stop before increasing catalog size')
