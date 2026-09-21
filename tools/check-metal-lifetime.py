#!/usr/bin/env python3
"""Check shared-event references stay bounded in a rendering macOS process.

Run against an actively rendering Wheelhouse view. Uses lsmp's typed Mach-port
inventory, not aggregate port counts. It does not stop or mutate the process.
"""
import argparse
import subprocess
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('pid', type=int)
parser.add_argument('--interval', type=float, default=5)
parser.add_argument('--samples', type=int, default=5)
parser.add_argument('--max-growth', type=int, default=32)
args = parser.parse_args()
if args.samples < 2 or args.interval <= 0 or args.max_growth < 0:
    parser.error('need at least two samples, positive interval, nonnegative growth')
counts = []
for i in range(args.samples):
    result = subprocess.run(['lsmp','-p',str(args.pid),'-v'], capture_output=True, text=True, check=True)
    counts.append(sum('IOSurfaceSharedEventReference' in line for line in result.stdout.splitlines()))
    print(f'sample {i+1}: {counts[-1]} shared-event references', flush=True)
    if i+1 < args.samples:
        time.sleep(args.interval)
growth = max(counts)-min(counts)
print(f'Observed range: {min(counts)}..{max(counts)}; growth allowance: {args.max_growth}')
raise SystemExit(growth > args.max_growth)
