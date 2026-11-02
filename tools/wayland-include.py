#!/usr/bin/env python3
"""Generate the Wayland window manager's protocol code with wayland-scanner,
from the system's protocol XML (the wayland and wayland-protocols packages),
and print the include directory that holds it: sources include
"wayland_protocols/<name>-protocol.h" and "...c" from there."""
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
include = root / "build/generated"
out = include / "wayland_protocols"


def pkgdatadir(package, fallback):
    try:
        path = subprocess.check_output(["pkg-config", "--variable=pkgdatadir", package],
                                       text=True, stderr=subprocess.DEVNULL).strip()
    except (OSError, subprocess.CalledProcessError):
        path = ""
    return Path(path or fallback)


def first(*paths):
    return next((p for p in paths if p.is_file()), paths[0])


if shutil.which("wayland-scanner") is None:
    sys.exit("wayland-scanner not found; install wayland (Arch) or libwayland-dev (Debian, Ubuntu)")
core = pkgdatadir("wayland-scanner", "/usr/share/wayland")
protocols = pkgdatadir("wayland-protocols", "/usr/share/wayland-protocols")
# tablet-v2 only provides the interface cursor-shape's get_tablet_tool_v2
# names. It moved to stable in wayland-protocols 1.36; Ubuntu 24.04 has 1.34.
xmls = [
    core / "wayland.xml",
    protocols / "stable/xdg-shell/xdg-shell.xml",
    protocols / "unstable/xdg-decoration/xdg-decoration-unstable-v1.xml",
    protocols / "staging/cursor-shape/cursor-shape-v1.xml",
    first(protocols / "stable/tablet/tablet-v2.xml", protocols / "unstable/tablet/tablet-unstable-v2.xml"),
]
missing = [str(xml) for xml in xmls if not xml.is_file()]
if missing:
    sys.exit("Wayland protocol XML not found (install wayland-protocols 1.32 or later): " + ", ".join(missing))

out.mkdir(parents=True, exist_ok=True)
with tempfile.TemporaryDirectory() as scratch:
    for xml in xmls:
        # The unstable tablet file gets the stable name, so the include is the same either way.
        name = "tablet-v2" if xml.stem.startswith("tablet") else xml.stem
        for mode, suffix in [("client-header", ".h"), ("private-code", ".c")]:
            args = ["wayland-scanner"] + (["--include-core-only"] if mode == "client-header" else [])
            generated = Path(scratch) / (name + "-protocol" + suffix)
            subprocess.run(args + [mode, str(xml), str(generated)], check=True)
            # Only a changed file is rewritten, so its timestamp says when the XML changed.
            target = out / generated.name
            if not target.is_file() or target.read_bytes() != generated.read_bytes():
                shutil.copyfile(generated, target)
print(include)
