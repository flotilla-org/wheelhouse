# Wheelhouse observed workdirs / Git producer validation

Andamento PR #112 merged as `56bd2d057456321208496986a6e69e2b400ddc59`,
now pinned by operator commit `038ec57`. Its only difference from the locally
validated `5dd8c0bccefec18334f8c4fb882ecc35f346c292` is an explanatory comment
in remote URL redaction; the watcher and observed-workdirs code are unchanged.
Linux CI run `36861249247` passes against the merged pin. Local validation used Cleat CI pin
`87b9d853be9a2858ef73f82ea8442a9ae30683e7`, and Jackstay pin `91156bfac2bc2f6df168c928b445c98e002f4950`.

- HTTP/Unix-socket ingress: 10 tests passed, including a real Wheelhouse process,
  saved and live-directory response fields, two views plus a directory-less view,
  changed directories, empty inventory, JSON escaping, unavailable/cancelled reads,
  invalid UTF-8 rejection, concurrent reads, existing patch handling, and a real Rust watcher `--once` against the process.
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
- Two deterministic Rust drain tests: burst reads share snapshots, patches invalidate
  them, cancelled reads are skipped, failures are shared within a drain, and
  neither successful nor failed caches survive the drain. Passed.
- Native consumer Clippy with `-D warnings`: passed.
- Windows GNU target `cargo check --lib --bins` of the ingress consumer and the
  watcher's own crate: passed. This checks non-Unix Rust compilation; full Windows
  linking/UI checks still belong to CI.
- Generated source check and git diff whitespace check: passed.

Xvfb and runtime/keyboard packages were downloaded using private apt lists and
extracted under `/tmp/wh-xvfb`. The extracted X server's `/usr/bin` lookup was
changed to `/tmp/xkb` (same-length binary string), pointing at the extracted
xkbcomp; tests used `DISPLAY=:91 LIBGL_ALWAYS_SOFTWARE=1`. Ghostty was built for
real terminal tests. Scratch dependency checkouts are outside the vessel.

Interactive acceptance with a live daily driver, activating worktrees and
observing a branch switch within one TTL, remains **pending**. Xvfb diagnostics
and the automated watcher handshake do not claim that acceptance criterion.

The operator applied the Andamento pin; this crew did not change workflow files.
The same CI run exposed two diagnostic/generation portability issues: macOS
resolves `/tmp` to `/private/tmp`, and Windows reads UTF-8 arrows using its default
code page. The diagnostic now compares directory identity; the fixture generator
uses explicit UTF-8 and LF output, with a CP1252/CRLF regression test.
Live daily-driver acceptance remains tracked as pending under wheelhouse#130.

Follow-up CI run `36863619948` passed Linux and macOS, including the cwd
diagnostic. Windows passed generated-source verification, then revealed MSVC
C4566 warnings: its default execution code page cannot encode the Git arrows.
The Windows build now specifies `/utf-8` for both source and execution encoding;
the existing Git sidebar diagnostic checks the resulting arrow bytes.
