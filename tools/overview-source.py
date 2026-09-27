#!/usr/bin/env python3
"""Finite terminal output followed by silence for live preview acceptance."""
import argparse
import base64
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--images', action='store_true', help='replace and delete a Kitty image during output')
parser.add_argument('--duration', type=float, default=8)
parser.add_argument('--rate', type=float, default=10)
parser.add_argument('--quiet-seconds', type=float, default=30)
args = parser.parse_args()
if args.duration <= 0 or args.rate <= 0 or args.quiet_seconds < 0:
    parser.error('duration and rate must be positive; quiet-seconds must be nonnegative')

def image_update(tick):
    # A single small direct-RGBA command stays below the Kitty chunk limit.
    # Keep the same ID to exercise replacement, with periodic explicit deletion.
    sys.stdout.write('\x1b_Gq=2,a=d,d=A\x1b\\' if tick % 3 == 2 else '')
    if tick % 3 != 2:
        pixels = bytes((240, 40, 80, 255) if tick % 3 == 0 else (30, 220, 180, 255)) * (16 * 16)
        payload = base64.b64encode(pixels).decode('ascii')
        sys.stdout.write('\x1b7\x1b[2;2H')
        sys.stdout.write(f'\x1b_Gq=2,a=T,C=1,i=700,p=1,c=12,r=5,t=d,f=32,s=16,v=16;{payload}\x1b\\')
        sys.stdout.write('\x1b8')


sys.stdout.write('\x1b[2J\x1b[H\x1b[?25l')
start = time.monotonic()
for tick in range(int(args.duration * args.rate)):
    colour = 2 + tick % 5
    sys.stdout.write(f'\x1b[3{colour}m{tick:05d} │ build → test ✓ 日本語 🚢  agent output\x1b[0m\r\n')
    if args.images:
        image_update(tick)
    sys.stdout.flush()
    time.sleep(max(0, start + (tick + 1) / args.rate - time.monotonic()))
if args.images:
    sys.stdout.write('\x1b_Gq=2,a=d,d=A\x1b\\')
sys.stdout.write('\x1b[0mFINAL UPDATE — producer is now silent\r\n')
sys.stdout.flush()
# Remain alive without producing output; distinguishes idle from PTY EOF.
time.sleep(args.quiet_seconds)
