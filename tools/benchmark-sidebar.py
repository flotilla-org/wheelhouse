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


def validate_memory(output):
    config = re.search(r'SIDEBAR_CONFIG frames=(\d+) warmup=(\d+) sample_interval=(\d+) combinations=(\d+) worker_cpus=(\d+)', output)
    if not config:
        raise ValueError('missing sampling configuration')
    frames, warmup, interval, combinations, cpus = map(int, config.groups())
    if cpus != 1:
        raise ValueError('worker CPU sizing interposer did not apply')
    if not (0 < warmup < frames and interval > 0 and frames % interval == 0 and combinations > 0):
        raise ValueError('invalid sampling configuration')
    expected = list(range(interval, frames + 1, interval))
    post = [frame for frame in expected if frame >= warmup]
    if len(post) < 10 or post[-1] - post[0] < 3000:
        raise ValueError('insufficient long post-warmup window')
    rss = re.findall(r'SIDEBAR_RSS issues=\d+ frame=(\d+) VmRSS:\s*(\d+) kB', output)
    storage = re.findall(r'SIDEBAR_STORAGE frame=(\d+) draw=(\d+) font=(\d+) ui=(\d+) shell=(\d+)', output)
    measurements = []
    for name, samples in [('RSS', rss), ('storage', storage)]:
        if len(samples) != len(expected) * combinations:
            raise ValueError(f'incomplete {name} samples')
        for start in range(0, len(samples), len(expected)):
            group = [tuple(map(int, sample)) for sample in samples[start:start + len(expected)]]
            if [sample[0] for sample in group] != expected:
                raise ValueError(f'invalid {name} sample sequence')
            window = [sample for sample in group if sample[0] >= warmup]
            if name == 'storage':
                # Fixed-catalog app-owned arenas must remain bounded even when
                # one-time library/page residency changes process-wide RSS.
                for column in range(1, 5):
                    if max(sample[column] for sample in window) > window[0][column] + 1024 * 1024:
                        raise ValueError('app-owned storage grew by more than one MiB')
            else:
                # Exclude at most ONE interval's positive residency step. This
                # is neither a median nor a larger endpoint allowance: all
                # other net growth is charged over the remaining long window.
                # A sustained leak still contributes in every other interval.
                deltas = [b[1] - a[1] for a, b in zip(window, window[1:])]
                jump = max(range(len(deltas)), key=deltas.__getitem__)
                step = max(0, deltas[jump])
                residual = window[-1][1] - window[0][1] - step
                span = window[-1][0] - window[0][0] - (interval if step else 0)
                # Excess full-window growth must also continue through the
                # latter half. Early bounded residency recovery followed by a
                # long plateau is not sustained growth. Use the SAME excluded
                # interval in both windows, never discard a second step.
                tail_start = len(window) // 2
                tail_residual = window[-1][1] - window[tail_start][1]
                tail_span = window[-1][0] - window[tail_start][0]
                if step and jump >= tail_start:
                    tail_residual -= step
                    tail_span -= interval
                measurements.append((step, residual / span, tail_residual / tail_span))
                if residual * 2 > span and tail_residual * 2 > tail_span:
                    raise ValueError('sustained RSS slope exceeds 0.5 KiB/frame')
    return measurements


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT / 'build/wheelhouse')
    parser.add_argument('--time', default='/usr/bin/time', help='GNU time executable')
    parser.add_argument('--timeout', type=float, default=600, help='seconds allowed per 5,000-frame native run')
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
                try:
                    measurements = validate_memory(output)
                    for combination, (step, slope, tail_slope) in enumerate(measurements):
                        print(f'SIDEBAR_GATE run={identity} combination={combination} excluded_step_kib={step} residual_kib_per_frame={slope:.6f} tail_kib_per_frame={tail_slope:.6f}', flush=True)
                except ValueError as error:
                    raise SystemExit(f'{identity}: {error}; stop before increasing catalog size')


if __name__ == '__main__':
    main()
