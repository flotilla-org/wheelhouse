#!/usr/bin/env python3
"""Print the directory holding binnacle.h for the binnacle-ffi this workspace
builds, wherever Cargo checked it out (a git dependency, or a patched path)."""
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
metadata = json.loads(subprocess.check_output(
    ["cargo", "metadata", "--format-version=1", "--locked"], cwd=root, text=True))
manifest = next(package["manifest_path"] for package in metadata["packages"]
                if package["name"] == "binnacle-ffi")
print(Path(manifest).parent / "include")
