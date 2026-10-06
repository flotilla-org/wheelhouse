#!/usr/bin/env python3
"""Fixed-catalog storage bounds and sustained slope survive one residency step."""
import unittest
import importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location('benchmark_sidebar', Path(__file__).with_name('benchmark-sidebar.py'))
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)
validate_memory = benchmark.validate_memory


def fixture(slope=0, steps=(), storage_growth=0, storage_field='shell'):
    lines = ['SIDEBAR_CONFIG frames=5000 warmup=1000 sample_interval=200 combinations=4 worker_cpus=1']
    for combination in range(4):
        for frame in range(200, 5001, 200):
            process_frame = combination * 5000 + frame
            rss = 130000 + int(slope * process_frame) + sum(size for at, size in steps if process_frame >= at)
            lines.append(f'SIDEBAR_RSS issues=100 frame={frame} VmRSS: {rss} kB')
            arenas = ' '.join(f'{name}={100 + (storage_growth * frame if name == storage_field else 0)}' for name in ('draw', 'font', 'ui', 'shell'))
            lines.append(f'SIDEBAR_STORAGE frame={frame} {arenas}')
    return '\n'.join(lines)


class MemoryGate(unittest.TestCase):
    def test_generated_single_steps_and_slope_boundary(self):
        # Generate the whole sampling window, step magnitudes and slope boundary.
        # A single residency change passes; a sustained slope above the bound fails.
        for frame in range(1000, 20001, 200):
            for magnitude in (0, 6400, 65536):
                for slope in (0, .49, .5, .51, 1.4):
                    with self.subTest(frame=frame, magnitude=magnitude, slope=slope):
                        output = fixture(slope, [(frame, magnitude)])
                        if slope <= .5:
                            validate_memory(output)
                        else:
                            with self.assertRaisesRegex(ValueError, 'sustained RSS slope'):
                                validate_memory(output)

    def test_bounded_recovery_then_long_plateau(self):
        # Several early residency changes that settle for the latter half of the process run
        # are bounded recovery, not continuing growth. Arena bounds still apply.
        validate_memory(fixture(steps=[(1800, 12088), (2400, 32000), (3000, 12)]))

    def test_multiple_steps_are_not_hidden(self):
        # Only one interval may be excluded; recurring jumps remain charged.
        with self.assertRaisesRegex(ValueError, 'sustained RSS slope'):
            validate_memory(fixture(steps=[(frame, 6400) for frame in range(1600, 20000, 2400)]))

    def test_decommit_and_recovery_are_net_memory(self):
        # RSS is a trend, not allocation churn: a net decrease passes while
        # growth that exceeds a release still fails; arena bounds are separate.
        validate_memory(fixture(1.4, [(3000, -20000)]))
        with self.assertRaisesRegex(ValueError, 'sustained RSS slope'):
            validate_memory(fixture(1.4, [(3000, -1000)]))

    def test_storage_is_independent_of_rss(self):
        # Flat RSS must not conceal a growing app-owned arena.
        for name in ('draw', 'font', 'ui', 'shell'):
            with self.subTest(arena=name), self.assertRaisesRegex(ValueError, 'app-owned storage'):
                validate_memory(fixture(storage_growth=263, storage_field=name))
            validate_memory(fixture(storage_growth=262, storage_field=name)) # just below one MiB/window

    def test_missing_and_duplicate_samples_fail_closed(self):
        # Empty, truncated, duplicated and short-window logs cannot claim success.
        output = fixture()
        for invalid in ('', output.replace('frame=200 ', 'frame=400 ', 1),
                        output + '\nSIDEBAR_RSS issues=100 frame=5000 VmRSS: 130000 kB',
                        output.replace('warmup=1000', 'warmup=4000'),
                        output.replace('worker_cpus=1', 'worker_cpus=2'),
                        output.replace(' shell=100', '')):
            with self.subTest(invalid=invalid[:80]), self.assertRaises(ValueError):
                validate_memory(invalid)


if __name__ == '__main__':
    unittest.main()
