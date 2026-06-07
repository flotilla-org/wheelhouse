#!/usr/bin/env python3
"""Create a small HTML report for terminal fixture readback artifacts."""

from __future__ import annotations

import argparse
import html
import math
import shutil
import struct
import zlib
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
    while offset < len(data) and data[offset] in b" \t\r\n":
        offset += 1
    width = int(width_token)
    height = int(height_token)
    pixels = data[offset:]
    expected_size = width * height * 3
    if len(pixels) != expected_size:
        raise ValueError(f"{path}: expected {expected_size} pixel bytes, got {len(pixels)}")
    return width, height, pixels


def png_chunk(kind: bytes, data: bytes) -> bytes:
    crc = zlib.crc32(kind)
    crc = zlib.crc32(data, crc)
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", crc & 0xFFFFFFFF)


def write_png(path: Path, width: int, height: int, rgb_pixels: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    row_size = width * 3
    filtered = bytearray()
    for row in range(height):
        filtered.append(0)
        start = row * row_size
        filtered.extend(rgb_pixels[start:start + row_size])
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + png_chunk(b"IHDR", ihdr)
        + png_chunk(b"IDAT", zlib.compress(bytes(filtered), 9))
        + png_chunk(b"IEND", b"")
    )
    path.write_bytes(png)


def diff_pixels(a_pixels: bytes, b_pixels: bytes, scale: float) -> bytes:
    if len(a_pixels) != len(b_pixels):
        raise ValueError("pixel buffers have different byte counts")
    out = bytearray(len(a_pixels))
    for idx, (a, b) in enumerate(zip(a_pixels, b_pixels)):
        out[idx] = min(255, int(abs(a - b) * scale + 0.5))
    return bytes(out)


def compare(a_pixels: bytes, b_pixels: bytes) -> dict[str, float | int]:
    if len(a_pixels) != len(b_pixels):
        raise ValueError("pixel buffers have different byte counts")
    changed_pixels = 0
    total_abs = 0
    total_sq = 0
    max_abs = 0
    pixel_count = len(a_pixels) // 3
    for idx in range(0, len(a_pixels), 3):
        pixel_changed = False
        for channel in range(3):
            delta = abs(a_pixels[idx + channel] - b_pixels[idx + channel])
            if delta:
                pixel_changed = True
            total_abs += delta
            total_sq += delta * delta
            max_abs = max(max_abs, delta)
        if pixel_changed:
            changed_pixels += 1
    sample_count = len(a_pixels)
    return {
        "pixels": pixel_count,
        "changed_pixels": changed_pixels,
        "changed_ratio": changed_pixels / pixel_count if pixel_count else 0.0,
        "mean_abs": total_abs / sample_count if sample_count else 0.0,
        "rmse": math.sqrt(total_sq / sample_count) if sample_count else 0.0,
        "max_abs": max_abs,
    }


def metrics_text(label: str, metrics: dict[str, float | int]) -> str:
    return (
        f"{label}\n"
        f"  changed_pixels: {metrics['changed_pixels']}/{metrics['pixels']} ({metrics['changed_ratio']:.6f})\n"
        f"  mean_abs: {metrics['mean_abs']:.3f}\n"
        f"  rmse: {metrics['rmse']:.3f}\n"
        f"  max_abs: {metrics['max_abs']}\n"
    )


def artifact_row(label: str, ppm_path: Path | None, width: int | None = None, height: int | None = None) -> str:
    if ppm_path is None:
        return (
            "<tr>"
            f"<th>{html.escape(label)}</th>"
            "<td class=\"missing\">not provided</td>"
            "<td></td>"
            "<td></td>"
            "</tr>"
        )
    size = ppm_path.stat().st_size
    dimensions = f"{width}x{height}" if width is not None and height is not None else ""
    return (
        "<tr>"
        f"<th>{html.escape(label)}</th>"
        f"<td><code>{html.escape(str(ppm_path))}</code></td>"
        f"<td>{html.escape(dimensions)}</td>"
        f"<td>{size}</td>"
        "</tr>"
    )


def file_artifact_row(label: str, path: Path | None) -> str:
    if path is None:
        return (
            "<tr>"
            f"<th>{html.escape(label)}</th>"
            "<td class=\"missing\">not provided</td>"
            "<td></td>"
            "<td></td>"
            "</tr>"
        )
    size = path.stat().st_size
    return (
        "<tr>"
        f"<th>{html.escape(label)}</th>"
        f"<td><code>{html.escape(str(path))}</code></td>"
        "<td>reference image</td>"
        f"<td>{size}</td>"
        "</tr>"
    )


def figure_html(caption: str, filename: str) -> str:
    return f'<figure><figcaption>{html.escape(caption)}</figcaption><img src="{html.escape(filename)}"></figure>'


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mac", type=Path, default=Path("local/screenshots/macos-metal-terminal-fixture.ppm"))
    parser.add_argument("--linux", type=Path, default=Path("local/screenshots/linux-opengl-terminal-fixture.ppm"))
    parser.add_argument("--windows", type=Path, default=None)
    parser.add_argument("--reference-image", type=Path, default=None, help="Optional human-reference image, such as a Ghostty screenshot.")
    parser.add_argument("--reference-caption", default="Reference / Ghostty", help="Caption for --reference-image.")
    parser.add_argument("--out-dir", type=Path, default=Path("local/screenshots/terminal-fixture-report"))
    parser.add_argument("--diff-scale", type=float, default=4.0)
    args = parser.parse_args()

    mac_w, mac_h, mac_pixels = read_ppm(args.mac)
    linux_w, linux_h, linux_pixels = read_ppm(args.linux)
    if (mac_w, mac_h) != (linux_w, linux_h):
        raise ValueError(f"dimension mismatch: macOS={mac_w}x{mac_h} Linux={linux_w}x{linux_h}")
    windows_pixels = None
    win_w = None
    win_h = None
    if args.windows is not None:
        win_w, win_h, windows_pixels = read_ppm(args.windows)
        if (mac_w, mac_h) != (win_w, win_h):
            raise ValueError(f"dimension mismatch: macOS={mac_w}x{mac_h} Windows={win_w}x{win_h}")

    args.out_dir.mkdir(parents=True, exist_ok=True)
    mac_png = args.out_dir / "macos-metal-terminal-fixture.png"
    linux_png = args.out_dir / "linux-opengl-terminal-fixture.png"
    diff_png = args.out_dir / "metal-opengl-terminal-fixture-diff.png"
    write_png(mac_png, mac_w, mac_h, mac_pixels)
    write_png(linux_png, linux_w, linux_h, linux_pixels)
    write_png(diff_png, mac_w, mac_h, diff_pixels(mac_pixels, linux_pixels, args.diff_scale))
    figures = [
        figure_html("macOS / Metal", mac_png.name),
        figure_html("Linux / OpenGL", linux_png.name),
        figure_html("Metal vs OpenGL Absolute Difference", diff_png.name),
    ]

    metrics_blocks = [metrics_text("macOS / Metal vs Linux / OpenGL", compare(mac_pixels, linux_pixels))]
    if windows_pixels is not None:
        windows_png = args.out_dir / "windows-d3d11-terminal-fixture.png"
        mac_windows_diff_png = args.out_dir / "metal-d3d11-terminal-fixture-diff.png"
        linux_windows_diff_png = args.out_dir / "opengl-d3d11-terminal-fixture-diff.png"
        write_png(windows_png, mac_w, mac_h, windows_pixels)
        write_png(mac_windows_diff_png, mac_w, mac_h, diff_pixels(mac_pixels, windows_pixels, args.diff_scale))
        write_png(linux_windows_diff_png, mac_w, mac_h, diff_pixels(linux_pixels, windows_pixels, args.diff_scale))
        figures.extend([
            figure_html("Windows / D3D11", windows_png.name),
            figure_html("Metal vs D3D11 Absolute Difference", mac_windows_diff_png.name),
            figure_html("OpenGL vs D3D11 Absolute Difference", linux_windows_diff_png.name),
        ])
        metrics_blocks.extend([
            metrics_text("macOS / Metal vs Windows / D3D11", compare(mac_pixels, windows_pixels)),
            metrics_text("Linux / OpenGL vs Windows / D3D11", compare(linux_pixels, windows_pixels)),
        ])

    reference_path = None
    if args.reference_image is not None:
        if not args.reference_image.is_file():
            raise ValueError(f"reference image was not found: {args.reference_image}")
        reference_path = args.out_dir / ("reference-" + args.reference_image.name)
        if reference_path.resolve() != args.reference_image.resolve():
            shutil.copy2(args.reference_image, reference_path)
        figures.append(figure_html(args.reference_caption, reference_path.name))

    index = args.out_dir / "index.html"
    metrics_joined = "\n".join(metrics_blocks)
    figures_joined = "\n".join(figures)
    artifact_rows = "\n".join(
        [
            artifact_row("macOS / Metal", args.mac, mac_w, mac_h),
            artifact_row("Linux / OpenGL", args.linux, linux_w, linux_h),
            artifact_row("Windows / D3D11", args.windows, win_w, win_h),
            file_artifact_row(args.reference_caption, args.reference_image),
        ]
    )
    html_text = f"""<!doctype html>
<meta charset="utf-8">
<title>UIShell Terminal Fixture Report</title>
<style>
body {{ background: #171717; color: #e7e7e7; font-family: -apple-system, BlinkMacSystemFont, sans-serif; margin: 24px; }}
main {{ max-width: 1600px; margin: 0 auto; }}
.metrics {{ font-family: ui-monospace, SFMono-Regular, Menlo, monospace; background: #242424; padding: 12px; border: 1px solid #444; }}
.provenance {{ border-collapse: collapse; margin: 16px 0 24px; width: 100%; }}
.provenance th, .provenance td {{ border: 1px solid #444; padding: 8px; text-align: left; vertical-align: top; }}
.provenance th {{ width: 180px; background: #242424; }}
.missing {{ color: #aaa; }}
code {{ font-family: ui-monospace, SFMono-Regular, Menlo, monospace; }}
.grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(360px, 1fr)); gap: 16px; }}
figure {{ margin: 0; }}
figcaption {{ margin-bottom: 8px; font-weight: 600; }}
img {{ width: 100%; height: auto; image-rendering: auto; background: #000; border: 1px solid #444; }}
</style>
<main>
<h1>UIShell Terminal Fixture Report</h1>
<p>This report compares deterministic backend readback artifacts. It is a visual-review aid, not a Ghostty parity claim.</p>
<h2>Inputs</h2>
<table class="provenance">
<thead><tr><th>Backend</th><th>PPM artifact</th><th>Dimensions</th><th>Bytes</th></tr></thead>
<tbody>
{artifact_rows}
</tbody>
</table>
<h2>How To Regenerate</h2>
<pre class="metrics">tools/run-terminal-color-font-checks.sh

# Optional Windows artifact, run on a Windows host:
powershell -ExecutionPolicy Bypass -File tools\\run-windows-terminal-glyph-diagnostics.ps1

# Rebuild only this report from existing artifacts:
tools/make-terminal-fixture-report.py --mac {html.escape(str(args.mac))} --linux {html.escape(str(args.linux))}{' --windows ' + html.escape(str(args.windows)) if args.windows is not None else ''}{' --reference-image ' + html.escape(str(args.reference_image)) if args.reference_image is not None else ''} --out-dir {html.escape(str(args.out_dir))}</pre>
<h2>Metrics</h2>
<pre class="metrics">size: {mac_w}x{mac_h}
diff_scale: {args.diff_scale:.3f}

{html.escape(metrics_joined)}</pre>
<h2>Artifacts</h2>
<section class="grid">
{figures_joined}
</section>
</main>
"""
    index.write_text(html_text)
    print(index)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
