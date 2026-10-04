#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash build.sh wheelhouse
# Headless production consumer; provider and desktop boundaries are fake sinks.
./build/wheelhouse --terminal_clipboard_diagnostics
