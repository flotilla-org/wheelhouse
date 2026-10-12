#!/usr/bin/env python3
"""Check real Cleat output consumption and eventual idle in an isolated overview."""
import argparse
import csv
import hashlib
import io
import json
from pathlib import Path
import shlex
import subprocess
import sys
import time


def read_frames(path):
    if not path.exists():
        return []
    data = path.read_text()
    # The application can be in the middle of appending its next record.
    data = data[:data.rfind('\n') + 1]
    return [{key: float(value) for key, value in row.items()}
            for row in csv.DictReader(io.StringIO(data))]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=Path('build/wheelhouse'))
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--count', type=int, choices=[1, 16, 48], default=48)
    parser.add_argument('--timeout', type=float, default=40)
    parser.add_argument('--max-frame-ms', type=float, default=50)
    parser.add_argument('--no-render-budget', action='store_true')
    parser.add_argument('--screenshots', action='store_true', help='capture phase readbacks; adds GPU synchronization')
    parser.add_argument('--images', action='store_true')
    parser.add_argument('--transitions', action='store_true')
    args = parser.parse_args()
    if sys.platform != 'darwin':
        parser.error('the native overview test driver currently supports macOS only')
    if args.timeout <= (20 if args.transitions else 12) or args.max_frame_ms <= 0:
        parser.error('allow over 12 seconds (20 with transitions) and a positive frame budget')
    binary = args.binary.resolve(strict=True)
    source = Path(__file__).with_name('overview-source.py').resolve(strict=True)
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    # Windows are a Dashboard's Presentation State, kept beside an explicit --user file.
    (output / 'dashboard').mkdir()
    (output / 'dashboard/id').write_text('benchmark')
    (output / 'presentation').mkdir()
    (output / 'presentation/benchmark.wheelhouse').write_text('window:\n{\n size: 1200 800\n}\n')
    command = shlex.join([sys.executable, str(source), '--duration', '14' if args.transitions else '8',
                          '--quiet-seconds', str(args.timeout)])
    if args.images:
        command += ' --images'
    launch = [str(binary), f'--user:{output}/user', f'--project:{output}/project', f'--dashboard:{output}/dashboard',
              f'--overview_benchmark:{output}', f'--overview_benchmark_count:{args.count}',
              f'--overview_benchmark_command:{command}']
    if not args.screenshots:
        launch.append('--overview_benchmark_no_screenshots')
    if args.transitions:
        launch.append('--overview_benchmark_live_transitions')
    if args.no_render_budget:
        launch.append('--no_preview_render_budget')
    samples = []
    frames = []
    idle_since = None
    previous_count = 0
    failures = []
    with (output / 'output.log').open('w') as log:
        process = subprocess.Popen(launch, stdout=log, stderr=subprocess.STDOUT)
        start = time.monotonic()
        try:
            while time.monotonic() - start < args.timeout:
                time.sleep(.25)
                if process.poll() is not None:
                    raise RuntimeError(f'Wheelhouse exited unexpectedly: {process.returncode}')
                frames = read_frames(output / 'frames.csv')
                now = time.monotonic()
                complete = (frames and frames[-1]['live_final_count'] == args.count
                            and frames[-1]['deferred'] == 0 and frames[-1]['surface_deferred'] == 0
                            and frames[-1]['snapshot_deferred'] == 0)
                if complete and len(frames) == previous_count:
                    idle_since = idle_since or now
                else:
                    idle_since = None
                previous_count = len(frames)
                samples.append({'seconds': round(now - start, 3), 'frames': len(frames),
                                'complete': bool(complete)})
                if idle_since is not None and now - idle_since >= 2:
                    break
            else:
                failures.append('all final updates did not reach a quiescent frame loop before timeout')
        finally:
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
    if not frames:
        raise RuntimeError('no frame metrics recorded')
    if sum(row['provider_starts'] for row in frames) != args.count:
        failures.append('not all terminal providers started exactly once')
    if not args.no_render_budget and max(row['background_starts'] for row in frames) > 1:
        failures.append('background provider starts exceeded their per-frame limit')
    if not args.no_render_budget and max(row['background_updates'] for row in frames) > 4:
        failures.append('background snapshot fetches exceeded their per-frame limit')
    if args.images:
        if frames[-1]['live_image_seen'] != args.count:
            failures.append('not all terminals consumed an image placement')
        if frames[-1]['live_image_terminals'] != 0:
            failures.append('final image deletion did not clear placements')
        # The current ABI exposes placement-derived resources, so the image
        # cache retains unplaced assets below its quota. One reused ID per
        # terminal must remain bounded even across replacement/deletion cycles.
        if max(row['live_image_resources'] for row in frames) > args.count:
            failures.append('same-ID replacements accumulated image resources')
    if args.screenshots:
        expected_phases = range(7) if args.transitions else (0, 6)
        captures = {}
        for phase in expected_phases:
            path = output / f'phase-{phase}.ppm'
            if not path.exists():
                failures.append(f'missing rendered checkpoint {phase}')
                continue
            magic, dimensions, maximum, pixels = path.read_bytes().split(b'\n', 3)
            width, height = map(int, dimensions.split())
            if magic != b'P6' or maximum != b'255' or len(pixels) != width * height * 3:
                raise RuntimeError(f'invalid checkpoint: {path}')
            colours = ((240, 40, 80), (30, 220, 180))
            counts = dict.fromkeys(colours, 0)
            for pixel in zip(pixels[::3], pixels[1::3], pixels[2::3]):
                if pixel in counts:
                    counts[pixel] += 1
            captures[phase] = counts
        if args.images:
            for colour in ((240, 40, 80), (30, 220, 180)):
                if not any(counts[colour] > 0 for phase, counts in captures.items() if phase != 6):
                    failures.append(f'image colour {colour} absent from rendered checkpoints')
            if 6 in captures and any(captures[6].values()):
                failures.append('deleted image pixels remain in the settled overview')
    if not args.no_render_budget and max(row['background_surface_redraws'] for row in frames) > 4:
        failures.append('actual background surface redraws exceeded their per-frame limit')
    # First paint includes native-window/font initialization. Report it separately;
    # every subsequent frame, including the remaining provider starts, is gated.
    worst = max((row['frame_us']-row['event_wait_us']) / 1000 for row in frames[1:])
    if worst > args.max_frame_ms:
        failures.append(f'post-first-paint frame {worst:.2f} ms exceeds {args.max_frame_ms:.2f} ms')
    phases = {}
    if args.transitions:
        for phase, name in enumerate(('initialize', 'select', 'expand', 'return', 'resize_small', 'resize_large')):
            durations = [(row['frame_us']-row['event_wait_us'])/1000 for row in frames[1:] if row['phase'] == phase]
            if not durations:
                failures.append(f'missing transition phase {name}')
            else:
                phases[name] = {'max_ms': max(durations), 'frames': len(durations)}
    report = {'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
              'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'phases': phases, 'count': args.count, 'frames': len(frames),
              'first_paint_ms': frames[0]['frame_us'] / 1000,
              'active_frame_max_ms': worst, 'event_wait_total_ms': sum(r['event_wait_us'] for r in frames)/1000, 'final_terminals': frames[-1]['live_final_count'],
              'images': args.images, 'screenshots': args.screenshots, 'peak_image_terminals': max(r['live_image_terminals'] for r in frames),
              'render_budget': not args.no_render_budget, 'failures': failures, 'samples': samples}
    (output / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k: v for k, v in report.items() if k != 'samples'}, indent=2))
    return bool(failures)


if __name__ == '__main__':
    raise SystemExit(main())
