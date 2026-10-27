#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
# Linux harness: a C compiler is required for the CPU adapter.
command -v "${CC:-cc}" >/dev/null || { echo 'sidebar diagnostics require a C compiler (CC or cc)' >&2; exit 1; }
bash native/build.sh wheelhouse release
task_dir=$(mktemp -d "${TMPDIR:-/tmp}/wheelhouse-sidebar.XXXXXX")
trap 'rm -rf "$task_dir"' EXIT
# Process-boundary CPU adapter, shared with benchmark-sidebar.py: keep worker
# and cache stripe sizing independent of an uncapped container's host CPUs.
printf 'int get_nprocs(void) { return 1; }\n' > "$task_dir/cpus.c"
"${CC:-cc}" -shared -fPIC "$task_dir/cpus.c" -o "$task_dir/cpus.so"
LD_PRELOAD="$task_dir/cpus.so${LD_PRELOAD:+:$LD_PRELOAD}" LP_NUM_THREADS=1 MALLOC_ARENA_MAX=2 \
  ./build/wheelhouse --async_thread_count:1 \
  --user:"$task_dir/user" --project:"$task_dir/project" --sidebar_diagnostics
