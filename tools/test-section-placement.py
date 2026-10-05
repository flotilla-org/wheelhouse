#!/usr/bin/env python3
"""Run headless region lifecycle integration checks against the native shell."""
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile

root = Path(__file__).resolve().parents[1]
config = root / "data/sidebar/daily-driver.kdl"
before = hashlib.sha256(config.read_bytes()).digest()
binary = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / "build/wheelhouse"
# Isolate user/project paths even though the headless diagnostics use in-memory
# config trees and the real serializer. No display or terminal daemon is needed.
with tempfile.TemporaryDirectory(prefix="section-placement-") as directory:
    subprocess.run([str(binary), "--section_placement_diagnostics",
                    f"--user:{directory}/user", f"--project:{directory}/project"],
                   check=True, timeout=60)
# User layout operations must never write the producer's shared KDL source.
assert hashlib.sha256(config.read_bytes()).digest() == before, "section placement changed KDL"
