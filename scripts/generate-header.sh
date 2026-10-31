#!/bin/sh
# Regenerate crates/wheelhouse-ffi/include/wheelhouse.h from the crate's
# source with cbindgen (`cargo install cbindgen`). With --check, fail if
# the committed header is stale instead.
set -e
ffi="$(dirname "$0")/../crates/wheelhouse-ffi"
out="$ffi/include/wheelhouse.h"
generate() {
  # The crate, not lib.rs alone, so cbindgen can read wheelhouse-text's types.
  cbindgen --quiet --config "$ffi/cbindgen.toml" --crate wheelhouse-ffi --output "$1" "$ffi"
}
if [ "$1" = "--check" ]; then
  tmp=$(mktemp)
  trap 'rm -f "$tmp"' EXIT
  generate "$tmp"
  if ! diff -u "$out" "$tmp"; then
    echo "wheelhouse.h is stale: run scripts/generate-header.sh" >&2
    exit 1
  fi
else
  generate "$out"
fi
