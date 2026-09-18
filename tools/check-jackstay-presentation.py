#!/usr/bin/env python3
"""macOS capture check for a connected jackstay-frame-source panel or SDL viewer.

Run with Pillow installed. The central crop must lie wholly inside the video.
The source alternates low-contrast grey levels, never black/white flashes.
"""

import argparse
from pathlib import Path
import subprocess
import tempfile
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("window", help="macOS CGWindowID")
parser.add_argument("--samples", type=int, default=20)
parser.add_argument(
    "--crop",
    type=float,
    nargs=4,
    default=(0.35, 0.35, 0.8, 0.75),
    metavar=("LEFT", "TOP", "RIGHT", "BOTTOM"),
    help="fractional window crop inside video",
)
parser.add_argument(
    "--artifacts", type=Path, default=Path("local/jackstay-presentation")
)
args = parser.parse_args()
args.artifacts.mkdir(parents=True, exist_ok=True)
levels = set()
failures = 0
with tempfile.TemporaryDirectory() as temporary:
    for index in range(args.samples):
        path = Path(temporary) / "frame.png"
        subprocess.run(
            ["screencapture", "-x", "-l", args.window, str(path)], check=True
        )
        with Image.open(path) as original:
            image = original.convert("RGB")
        width, height = image.size
        left, top, right, bottom = args.crop
        crop = image.crop(
            (
                int(width * left),
                int(height * top),
                int(width * right),
                int(height * bottom),
            )
        )
        extrema = crop.getextrema()
        levels.add(crop.getpixel((0, 0))[0])
        if max(high - low for low, high in extrema) > 5:
            failures += 1
            image.save(args.artifacts / f"mixed-{index}.png")
print(f"{failures}/{args.samples} mixed frames; observed levels={sorted(levels)}")
if len(levels) < 2 or min(levels) <= 40 or max(levels) >= 180:
    raise SystemExit("The crop does not show an advancing low-contrast source.")
raise SystemExit(bool(failures))
