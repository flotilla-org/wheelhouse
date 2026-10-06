#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash build.sh wheelhouse
# Uses the native settings lister and canvas; requires a display.
# On headless Linux: xvfb-run -a bash tools/run-terminal-hosting-diagnostics.sh
./build/wheelhouse --hosting_diagnostics
