#!/usr/bin/env python3
"""Resolve the embedded FFI as a consumer, without loading Zellij workspace members."""
import json
import shutil
import sys
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1]).resolve() / 'crates' / 'andamento-ffi'
if not (source / 'Cargo.toml').is_file():
    raise SystemExit(f'Andamento FFI manifest not found: {source / "Cargo.toml"}')
output = root / 'build' / 'andamento'
output.mkdir(parents=True, exist_ok=True)
(output / 'Cargo.toml').write_text(
    '[package]\nname = "wheelhouse-native-deps"\nversion = "0.1.0"\nedition = "2021"\n'
    '[lib]\nname = "wheelhouse_ingress"\ncrate-type = ["cdylib", "rlib"]\npath = ' + json.dumps(str(root / 'src/ingress/lib.rs')) + '\n'
    '[dependencies]\nandamento-ffi = { path = ' + json.dumps(str(source)) + ' }\n'
    '[target.\'cfg(unix)\'.dependencies]\naxum = { version = "0.8", default-features = false, features = ["http1", "tokio"] }\ntokio = { version = "1", features = ["rt", "net", "sync", "time", "macros"] }\nserde_json = "1"\nlibc = "0.2"\n[workspace]\n', encoding='utf-8')
shutil.copyfile(root / 'tools' / 'andamento-build' / 'Cargo.lock', output / 'Cargo.lock')
