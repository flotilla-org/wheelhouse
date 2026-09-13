#!/usr/bin/env bash
# Native Wheelhouse + live git facts + Flotilla's catalog connector.
set -euo pipefail
exec python3 "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)/tools/daily-driver.py" "$@"
