#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash build.sh wheelhouse
# Uses the native settings lister and canvas; requires a display (Xvfb on Linux).
./build/wheelhouse --hosting_diagnostics
