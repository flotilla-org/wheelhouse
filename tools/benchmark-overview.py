#!/usr/bin/env python3
"""Run isolated, frame-indexed overview replays and compare measured budgets.

macOS first: native resize is implemented in the diagnostic backend. No live
sessions or global input injection. Each run uses fresh configuration files.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import platform
import subprocess
import sys
import time

PHASES = ('warmup', 'open', 'select', 'expand', 'return', 'resize_small', 'resize_large', 'drain')


def percentile(values, quantile):
    return sorted(values)[max(0, math.ceil(len(values) * quantile) - 1)]


def summarize(directory, count, screenshots, render_budget):
    with (directory / 'frames.csv').open() as stream:
        rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader(stream)]
    if [r['frame'] for r in rows] != list(range(240)):
        raise ValueError('missing or duplicated replay frames')
    if any(r['phase'] != int(r['frame']) // 30 for r in rows):
        raise ValueError('phase schedule changed')
    if rows[59]['zoom_t'] < .99 or rows[119]['zoom_t'] > .01:
        raise ValueError('overview did not open and close')
    if (rows[179]['width'], rows[179]['height']) != (900, 600):
        raise ValueError('small resize did not complete')
    if (rows[209]['width'], rows[209]['height']) != (1200, 800):
        raise ValueError('large resize did not complete')
    if max(r['terminal_visits'] for r in rows) != count:
        raise ValueError('not all fixture workspaces were rendered')
    # Single-workspace control is static. Once settled, it must reuse its bucket.
    if count == 1 and any(r['rebuilds'] for r in rows[215:]):
        raise ValueError('static terminal is rebuilding after settling')
    if any(r['updates'] or r['rebuilds'] for r in rows[225:]):
        raise ValueError('final quiet phase did not settle')
    if count > 1 and not sum(r['updates'] for r in rows[210:]):
        raise ValueError('dynamic replay stopped updating')
    if render_budget:
        if any(r['surface_admissions'] > 4 for r in rows):
            raise ValueError('preview render admissions exceeded the per-frame budget')
        if any(r['background_surface_redraws'] > 4 for r in rows):
            raise ValueError('actual background surface redraws exceeded the per-frame budget')
        if any(r['surface_deferred'] for r in rows[230:]):
            raise ValueError('preview rendering did not drain after final output')
    result = {'phases': {}, 'checkpoints': {}}
    for phase, name in enumerate(PHASES):
        samples = [r for r in rows if r['phase'] == phase]
        result['phases'][name] = {
            'frame_p50_ms': percentile([r['frame_us'] / 1000 for r in samples], .5),
            'frame_p95_ms': percentile([r['frame_us'] / 1000 for r in samples], .95),
            'frame_p99_ms': percentile([r['frame_us'] / 1000 for r in samples], .99),
            'frame_max_ms': max(r['frame_us'] / 1000 for r in samples),
            'action_frame_ms': samples[0]['frame_us'] / 1000,
            'build_p95_ms': percentile([r['build_us'] / 1000 for r in samples], .95),
            'surface_lookup_total_ms': sum(r.get('surface_us', 0) for r in samples) / 1000,
            'glyph_build_total_ms': sum(r.get('glyph_us', 0) for r in samples) / 1000,
            'window_total_ms': sum(r.get('window_us', 0) for r in samples) / 1000,
            'rebuilds': int(sum(r['rebuilds'] for r in samples)),
            'cells_built': int(sum(r['cells_built'] for r in samples)),
            'updates': int(sum(r['updates'] for r in samples)),
            'deferred': int(sum(r.get('deferred', 0) for r in samples)),
            'surface_admissions': int(sum(r.get('surface_admissions', 0) for r in samples)),
            'surface_deferred': int(sum(r.get('surface_deferred', 0) for r in samples)),
            'rect_instances': int(sum(r.get('rect_instances', 0) for r in samples)),
            'surface_allocations': int(sum(r['surface_allocations'] for r in samples)),
            'surface_pixels_peak': int(max(r['surface_pixels'] for r in samples)),
        }
        if screenshots:
            path = directory / f'phase-{phase}.ppm'
            data = path.read_bytes()
            magic, dims, limit, pixels = data.split(b'\n', 3)
            width, height = map(int, dims.split())
            if magic != b'P6' or limit != b'255' or len(pixels) != width * height * 3:
                raise ValueError(f'invalid screenshot {path}')
            if len(set(pixels)) < 8:
                raise ValueError(f'blank screenshot {path}')
            result['checkpoints'][name] = {'sha256': hashlib.sha256(data).hexdigest(),
                                           'width': width, 'height': height}
    if screenshots:
        for frame in (31, 33, 37):
            data = (directory / f'frame-{frame}.ppm').read_bytes()
            result['checkpoints'][f'frame-{frame}'] = {'sha256': hashlib.sha256(data).hexdigest()}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/wheelhouse'))
    parser.add_argument('--output', type=Path, required=True, help='new output directory, never overwritten')
    parser.add_argument('--counts', type=int, nargs='+', default=[1, 16, 48], choices=[1, 16, 48])
    parser.add_argument('--repeats', type=int, default=2)
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--no-screenshots', action='store_true')
    parser.add_argument('--preview-refresh-budget', action='store_true')
    parser.add_argument('--preview-surface-budget', action='store_true')
    parser.add_argument('--preview-render-budget', action='store_true')
    parser.add_argument('--interrupt', action='store_true')
    parser.add_argument('--busy', action='store_true', help='scroll every fixture terminal')
    parser.add_argument('--max-frame-ms', type=float, help='fail any non-warmup frame exceeding this end-to-end budget')
    parser.add_argument('--max-build-p95-ms', type=float, help='fail any non-warmup phase exceeding this budget')
    parser.add_argument('--baseline', type=Path, help='previous summary.json; compare matching counts/repetitions')
    parser.add_argument('--max-regression-percent', type=float, default=15)
    parser.add_argument('--match-checkpoints', nargs='*', choices=PHASES, default=[],
                        help='require these checkpoint hashes to match the baseline')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('native replay resize currently supports macOS only')
    if args.repeats < 1 or args.timeout <= 0:
        parser.error('repeats and timeout must be positive')
    if args.match_checkpoints and (not args.baseline or args.no_screenshots):
        parser.error('--match-checkpoints requires a baseline and screenshots')
    binary = args.binary.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=False)
    baseline = json.loads(args.baseline.read_text()) if args.baseline else None
    if baseline and baseline.get('screenshots') != (not args.no_screenshots):
        parser.error('baseline must use the same screenshot setting')
    report = {'schema': 1, 'binary': str(binary), 'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'platform': platform.platform(), 'virtual_hz': 60, 'frames': 240,
              'screenshots': not args.no_screenshots, 'preview_refresh_budget': args.preview_refresh_budget, 'preview_surface_budget': args.preview_surface_budget,
              'preview_render_budget': args.preview_render_budget, 'interrupt': args.interrupt, 'busy': args.busy, 'runs': {}, 'failures': []}
    for count in args.counts:
        for repeat in range(args.repeats):
            key = f'{count}-{repeat}'
            run = args.output / key
            run.mkdir()
            # Windows are a Dashboard's Presentation State, kept beside an explicit --user file.
            (run / 'dashboard').mkdir()
            (run / 'dashboard/id').write_text('benchmark')
            (run / 'presentation').mkdir()
            (run / 'presentation/benchmark.wheelhouse').write_text('window:\n{\n size: 1200 800\n}\n')
            (run / 'project').write_text('')
            command = [str(binary), f'--user:{run.resolve()}/user', f'--project:{run.resolve()}/project',
                       f'--dashboard:{run.resolve()}/dashboard',
                       f'--overview_benchmark:{run.resolve()}', f'--overview_benchmark_count:{count}']
            if not args.preview_refresh_budget:
                command.append('--no_preview_refresh_budget')
            if args.busy:
                command.append('--overview_benchmark_busy')
            if args.interrupt:
                command.append('--overview_benchmark_interrupt')
            if not args.preview_render_budget:
                command.append('--no_preview_render_budget')
            if not args.preview_surface_budget:
                command.append('--no_preview_surface_budget')
            if args.no_screenshots:
                command.append('--overview_benchmark_no_screenshots')
            start = time.monotonic()
            with (run / 'output.log').open('w') as log:
                subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout, check=True)
            result = summarize(run, count, not args.no_screenshots, args.preview_render_budget)
            result['wall_seconds'] = time.monotonic() - start
            if repeat:
                first = report['runs'][f'{count}-0']
                if result['checkpoints'] != first['checkpoints']:
                    report['failures'].append(f'{key}: checkpoint pixels differ from first repetition')
                for name in PHASES:
                    # Timing can vary; deterministic replay must not change its workload.
                    for field in ('updates', 'rebuilds', 'cells_built', 'surface_allocations', 'surface_pixels_peak', 'surface_admissions', 'surface_deferred', 'rect_instances'):
                        if result['phases'][name][field] != first['phases'][name][field]:
                            report['failures'].append(f'{key}/{name}: repeat differs in {field}')

            report['runs'][key] = result
            for phase, values in result['phases'].items():
                if phase == 'warmup':
                    continue
                if args.max_frame_ms is not None and values['frame_max_ms'] > args.max_frame_ms:
                    report['failures'].append(f"{key}/{phase}: frame max {values['frame_max_ms']:.2f} ms exceeds budget")
                measured = values['build_p95_ms']
                if args.max_build_p95_ms is not None and measured > args.max_build_p95_ms:
                    report['failures'].append(f'{key}/{phase}: build p95 {measured:.2f} ms exceeds budget')
                if baseline:
                    old = baseline['runs'][key]['phases'][phase]['build_p95_ms']
                    if measured > old * (1 + args.max_regression_percent / 100):
                        report['failures'].append(f'{key}/{phase}: build p95 {old:.2f} -> {measured:.2f} ms')
            for phase in args.match_checkpoints:
                if result['checkpoints'][phase] != baseline['runs'][key]['checkpoints'][phase]:
                    report['failures'].append(f'{key}/{phase}: checkpoint differs from baseline')
            (args.output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
            worst = max(p['build_p95_ms'] for name, p in result['phases'].items() if name != 'warmup')
            print(f'{key}: passed replay checks; worst phase build p95={worst:.2f} ms', flush=True)
    if report['failures']:
        print('\n'.join(report['failures']), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
