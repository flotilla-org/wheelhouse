# Wheelhouse observed workdirs / Git producer validation

Validated on Linux against Andamento PR #112 commit
`c92ea96180ff2efd97e7670a28c001e09ef46ee6`, Cleat CI pin
`87b9d853be9a2858ef73f82ea8442a9ae30683e7`, and Jackstay pin `91156bfac2bc2f6df168c928b445c98e002f4950`.

- HTTP/Unix-socket ingress: 8 tests passed, including a real Wheelhouse process,
  saved and live-directory response fields, two views plus a directory-less view,
  changed directories, empty inventory, JSON escaping, unavailable/cancelled reads,
  existing patch handling, and a real Rust watcher `--once` against the process.
- Native sidebar C ABI: 20 tests passed, including Git fixture relationship and
  materialization checks.
- Daily-driver launcher: 10 tests passed with controlled child processes,
  including repeated roots, startup, locking, restart, failure/cleanup, missing
  watcher handling, and disabling Git discovery.
- Native `--sidebar_diagnostics`: passed, including two repos/four worktrees at
  220px and 800px and the measured field ladder.
- Native `--managed_content_diagnostics`: zero failures, including a real terminal
  running at git.root, existing-directory focus without duplicate workspace or
  user-command replacement, and removing the match after cwd changes.
- Native consumer Clippy with `-D warnings`: passed.
- Generated source check and git diff whitespace check: passed.

Xvfb and runtime/keyboard packages were downloaded using private apt lists and
extracted under `/tmp/wh-xvfb`. The extracted X server's `/usr/bin` lookup was
changed to `/tmp/xkb` (same-length binary string), pointing at the extracted
xkbcomp; tests used `DISPLAY=:91 LIBGL_ALWAYS_SOFTWARE=1`. Ghostty was built for
real terminal tests. Scratch dependency checkouts are outside the vessel.

Interactive acceptance with a live daily driver, activating worktrees and
observing a branch switch within one TTL, remains **pending**. Xvfb diagnostics
and the automated watcher handshake do not claim that acceptance criterion.

CI needs the hand-applied Andamento pin patch in the PR description. No workflow
file has been changed by this crew.
