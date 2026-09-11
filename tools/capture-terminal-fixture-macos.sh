#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "capture-terminal-fixture-macos.sh only supports macOS." >&2
  exit 1
fi

out_dir="${1:-local/screenshots}"
mkdir -p "$out_dir"

timestamp="$(date +%Y%m%d-%H%M%S)"
out_path="$out_dir/terminal-fixture-$timestamp.png"

echo "Open Wheelhouse with a Terminal Fixture tab visible, then click the window to capture."
screencapture -W -o -x "$out_path"

if [[ ! -s "$out_path" ]]; then
  echo "No screenshot was written." >&2
  exit 1
fi

echo "$out_path"
