#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
bash build.sh wheelhouse
task_dir=$(mktemp -d "${TMPDIR:-/tmp}/wheelhouse-scroll.XXXXXX")
trap 'rm -rf "$task_dir"' EXIT
./build/wheelhouse --user:"$task_dir/user" --project:"$task_dir/project" --scroll_region_diagnostics
