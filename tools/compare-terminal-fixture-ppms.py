#!/usr/bin/env python3
"""Compare two raw PPM terminal fixture artifacts.

The default mode reports metrics and exits successfully when both files are
valid P6 images with the same dimensions. Optional thresholds can make the
comparison fail for use in tighter CI gates later.
"""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path


def read_token(data: bytes, offset: int) -> tuple[bytes, int]:
    while offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    if offset < len(data) and data[offset] == ord("#"):
        while offset < len(data) and data[offset] not in b"\r\n":
            offset += 1
        return read_token(data, offset)
    start = offset
    while offset < len(data) and data[offset] not in b" \t\r\n":
        offset += 1
    return data[start:offset], offset


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    magic, offset = read_token(data, 0)
    if magic != b"P6":
        raise ValueError(f"{path}: expected P6 magic, got {magic!r}")
    width_token, offset = read_token(data, offset)
    height_token, offset = read_token(data, offset)
    max_token, offset = read_token(data, offset)
    if max_token != b"255":
        raise ValueError(f"{path}: expected max value 255, got {max_token!r}")
    if offset >= len(data) or data[offset] not in b" \t\r\n":
        raise ValueError(f"{path}: missing whitespace after PPM header")
    while offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    width = int(width_token)
    height = int(height_token)
    expected_size = width * height * 3
    pixels = data[offset:]
    if len(pixels) != expected_size:
        raise ValueError(f"{path}: expected {expected_size} pixel bytes, got {len(pixels)}")
    return width, height, pixels


def compare(a_pixels: bytes, b_pixels: bytes) -> dict[str, float | int]:
    if len(a_pixels) != len(b_pixels):
        raise ValueError("pixel buffers have different byte counts")
    byte_count = len(a_pixels)
    pixel_count = byte_count // 3
    changed_pixels = 0
    total_abs = 0
    total_sq = 0
    max_abs = 0
    channel_abs = [0, 0, 0]
    for idx in range(0, byte_count, 3):
        pixel_changed = False
        for channel in range(3):
            delta = abs(a_pixels[idx + channel] - b_pixels[idx + channel])
            if delta != 0:
                pixel_changed = True
            total_abs += delta
            total_sq += delta * delta
            channel_abs[channel] += delta
            if delta > max_abs:
                max_abs = delta
        if pixel_changed:
            changed_pixels += 1
    sample_count = byte_count
    return {
        "pixels": pixel_count,
        "changed_pixels": changed_pixels,
        "changed_ratio": changed_pixels / pixel_count if pixel_count else 0.0,
        "mean_abs": total_abs / sample_count if sample_count else 0.0,
        "rmse": math.sqrt(total_sq / sample_count) if sample_count else 0.0,
        "max_abs": max_abs,
        "mean_abs_r": channel_abs[0] / pixel_count if pixel_count else 0.0,
        "mean_abs_g": channel_abs[1] / pixel_count if pixel_count else 0.0,
        "mean_abs_b": channel_abs[2] / pixel_count if pixel_count else 0.0,
    }


def write_ppm(path: Path, width: int, height: int, pixels: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(f"P6\n{width} {height}\n255\n".encode("ascii") + pixels)


def diff_pixels(a_pixels: bytes, b_pixels: bytes, scale: float) -> bytes:
    if len(a_pixels) != len(b_pixels):
        raise ValueError("pixel buffers have different byte counts")
    out = bytearray(len(a_pixels))
    for idx, (a, b) in enumerate(zip(a_pixels, b_pixels)):
        out[idx] = min(255, int(abs(a - b) * scale + 0.5))
    return bytes(out)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("a", type=Path)
    parser.add_argument("b", type=Path)
    parser.add_argument("--json", action="store_true", help="Emit metrics as JSON.")
    parser.add_argument("--diff-out", type=Path, default=None, help="Write a P6 PPM visual diff artifact.")
    parser.add_argument("--diff-scale", type=float, default=4.0, help="Multiplier applied to absolute channel deltas in --diff-out.")
    parser.add_argument("--max-rmse", type=float, default=None)
    parser.add_argument("--max-changed-ratio", type=float, default=None)
    args = parser.parse_args()

    a_width, a_height, a_pixels = read_ppm(args.a)
    b_width, b_height, b_pixels = read_ppm(args.b)
    if (a_width, a_height) != (b_width, b_height):
        raise ValueError(f"dimension mismatch: {args.a} is {a_width}x{a_height}, {args.b} is {b_width}x{b_height}")

    metrics = compare(a_pixels, b_pixels)
    metrics = {"width": a_width, "height": a_height, **metrics}
    if args.diff_out is not None:
        write_ppm(args.diff_out, a_width, a_height, diff_pixels(a_pixels, b_pixels, args.diff_scale))
        metrics["diff_out"] = str(args.diff_out)
        metrics["diff_scale"] = args.diff_scale

    if args.json:
        print(json.dumps(metrics, sort_keys=True))
    else:
        diff_suffix = f", diff_out={args.diff_out}" if args.diff_out is not None else ""
        print(
            "ppm comparison: "
            f"{args.a} vs {args.b}: "
            f"{a_width}x{a_height}, "
            f"changed={metrics['changed_pixels']}/{metrics['pixels']} ({metrics['changed_ratio']:.6f}), "
            f"mean_abs={metrics['mean_abs']:.3f}, "
            f"rmse={metrics['rmse']:.3f}, "
            f"max_abs={metrics['max_abs']}"
            f"{diff_suffix}"
        )

    failed = False
    if args.max_rmse is not None and metrics["rmse"] > args.max_rmse:
        print(f"rmse {metrics['rmse']:.6f} exceeds limit {args.max_rmse:.6f}")
        failed = True
    if args.max_changed_ratio is not None and metrics["changed_ratio"] > args.max_changed_ratio:
        print(f"changed ratio {metrics['changed_ratio']:.6f} exceeds limit {args.max_changed_ratio:.6f}")
        failed = True
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
