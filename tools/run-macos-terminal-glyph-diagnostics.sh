#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "run-macos-terminal-glyph-diagnostics.sh only supports macOS." >&2
  exit 1
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"
fixture_ppm="${WHEELHOUSE_MACOS_METAL_FIXTURE_PPM:-local/screenshots/macos-metal-terminal-fixture.ppm}"

cd "$repo_root"
mkdir -p "$(dirname "$fixture_ppm")"
bash build.sh wheelhouse
./build/wheelhouse --terminal_glyph_diagnostics --terminal_glyph_fixture_ppm:"$fixture_ppm"

if [[ ! -s "$fixture_ppm" ]]; then
  echo "No terminal fixture PPM was written: $fixture_ppm" >&2
  exit 1
fi

if [[ "$(head -c 2 "$fixture_ppm")" != "P6" ]]; then
  echo "Terminal fixture output is not a raw PPM: $fixture_ppm" >&2
  exit 1
fi

echo "$fixture_ppm"
