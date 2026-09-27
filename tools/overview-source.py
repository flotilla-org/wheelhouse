#!/usr/bin/env python3
"""Finite terminal output followed by silence for live preview acceptance."""
import argparse
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--duration', type=float, default=8)
parser.add_argument('--rate', type=float, default=10)
parser.add_argument('--quiet-seconds', type=float, default=30)
args = parser.parse_args()
if args.duration <= 0 or args.rate <= 0 or args.quiet_seconds < 0:
    parser.error('duration and rate must be positive; quiet-seconds must be nonnegative')

sys.stdout.write('\x1b[2J\x1b[H\x1b[?25l')
start = time.monotonic()
for tick in range(int(args.duration * args.rate)):
    colour = 2 + tick % 5
    sys.stdout.write(f'\x1b[3{colour}m{tick:05d} │ build → test ✓ 日本語 🚢  agent output\x1b[0m\r\n')
    sys.stdout.flush()
    time.sleep(max(0, start + (tick + 1) / args.rate - time.monotonic()))
sys.stdout.write('\x1b[0mFINAL UPDATE — producer is now silent\r\n')
sys.stdout.flush()
# Remain alive without producing output; distinguishes idle from PTY EOF.
time.sleep(args.quiet_seconds)
