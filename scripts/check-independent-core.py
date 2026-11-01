#!/usr/bin/env python3
"""Check the Cargo workspace stays free of Zellij and Flotilla crates.

The core, its frontends and the ingress depend on neither: Flotilla reaches
Wheelhouse over the facts stream, not as a library. Rigging's shared leaf
crates (flotilla ADR 0068), such as flotilla-cbor, depend on no project crate
and are allowed. Also links and runs real C clients against the exported ABI
(the sidebar, text layout with an arena allocator, and Rich Text's deltas,
markdown and conversations),
with and without the FFI's default features.
"""
import json
import os
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
host = next(line.split(": ", 1)[1] for line in subprocess.check_output(
    ["rustc", "-vV"], text=True).splitlines() if line.startswith("host: "))
target = Path(os.environ.get("CARGO_TARGET_DIR", root / "target"))

metadata = json.loads(subprocess.check_output(
    ["cargo", "metadata", "--format-version=1", "--locked"], cwd=root, text=True))
rigging = "git+https://github.com/flotilla-org/rigging"
forbidden = [package["name"] for package in metadata["packages"]
             if "zellij" in package["name"]
             or package["name"].startswith("flotilla") and not (package["source"] or "").startswith((rigging + "?", rigging + "#"))]
assert not forbidden, forbidden

subprocess.run(["cargo", "check", "-p", "wheelhouse-ffi", "--no-default-features",
                "--locked", "--target", host], cwd=root, check=True)
subprocess.run(["cargo", "build", "-p", "wheelhouse-ffi", "--locked", "--target", host],
               cwd=root, check=True)
library = target / host / "debug"
smoke = target / "ffi-smoke"
subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(root / "crates/wheelhouse-ffi/tests/smoke.c"),
                "-I", str(root / "crates/wheelhouse-ffi/include"),
                "-L", str(library), "-lwheelhouse_ffi", "-o", str(smoke)], check=True)
subprocess.run([str(smoke),
                str(root / "crates/wheelhouse-core/tests/fixtures/sidebar.kdl"),
                str(root / "crates/wheelhouse-core/tests/fixtures/sidebar.jsonl"),
                ], env=dict(os.environ, LD_LIBRARY_PATH=str(library),
                            DYLD_LIBRARY_PATH=str(library)), check=True)
text = target / "ffi-text"
subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(root / "crates/wheelhouse-ffi/tests/text.c"),
                "-I", str(root / "crates/wheelhouse-ffi/include"),
                "-L", str(library), "-lwheelhouse_ffi", "-o", str(text)], check=True)
subprocess.run([str(text), str(root / "data")], env=dict(os.environ, LD_LIBRARY_PATH=str(library),
               DYLD_LIBRARY_PATH=str(library)), check=True)
rich_text = target / "ffi-rich-text"
subprocess.run(["cc", "-std=c11", "-Wall", "-Wextra", "-Werror", str(root / "crates/wheelhouse-ffi/tests/rich_text.c"),
                "-I", str(root / "crates/wheelhouse-ffi/include"),
                "-L", str(library), "-lwheelhouse_ffi", "-o", str(rich_text)], check=True)
fixtures = root / "crates/wheelhouse-rich-text/src/fixtures"
subprocess.run([str(rich_text)] + [str(fixtures / name) for name in ("blocks.md", "gfm.md", "extensions.md", "conversation.jsonl")],
               env=dict(os.environ, LD_LIBRARY_PATH=str(library), DYLD_LIBRARY_PATH=str(library)), check=True)
print("Workspace and C ABI verified without a Zellij or Flotilla dependency.")
