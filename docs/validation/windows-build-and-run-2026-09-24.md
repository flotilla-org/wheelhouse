# Wheelhouse on Windows: build and run assessment (2026-09-24)

Research for the Windows end-to-end map. Host: Beaufort, Windows 11 26200,
VS 2022 Build Tools, LLVM clang. Base revision `ed5b97b`. This separates what
was **observed** on Beaufort from **source inference**.

## Summary

Wheelhouse builds and runs natively on Windows today (D3D11). Five of six UI
diagnostics pass; `terminal_glyph` fails on colour emoji. The Wheelhouse-specific
IPC additions (HTTP-over-UDS ingress, Jackstay client, daily driver) are
POSIX-only. Windows CI (`.github/workflows/build.yml`, windows-2022) exists but
only builds with Cleat's no-VT variant and runs nothing.

## Observed

Worktrees: this branch, plus Cleat, Andamento and Jackstay detached at the
CI-pinned revisions. Builds were driven from a `vcvars64` cmd wrapper; the agent
environment's `NoDefaultCurrentDirectoryInExePath=1` had to be cleared or
`call build.bat` fails with "not recognized".

All of these exited 0 with no C warnings:

- MSVC debug, Cleat no-VT
- clang debug, Cleat no-VT
- MSVC debug with ghostty-vt (prebuilt `.tools/ghostty-install`, same Ghostty ref)
- clang release with ghostty-vt
- MSVC debug against Andamento HEAD `07aedfc` as well as pinned `9eace9d`

Build defects:

1. `build.bat` does not copy `ghostty-vt.dll` next to the executable. A ghostty
   build then hangs at load (no window, ~5 MB working set). Copying the DLL fixes it.
2. Committed metagen output is stale: regenerating changes
   `src/uishell/generated/uishell.meta.{c,h}` (missing `jackstay` vocab entry,
   44 vs 45).

Andamento is a build-time dependency: `build.bat` builds `andamento-ffi` and
`src/ingress` into `andamento_ffi.dll` and `wheelhouse_ingress.dll`. From a
worktree the default `..\andamento` does not resolve; set
`WHEELHOUSE_ANDAMENTO_DIR` (and `WHEELHOUSE_CLEAT_DIR`).

Launch: window "project - Wheelhouse (0.1.0 EXPERIMENTAL)" on D3D11 with
sidebar, Text and Output panels and menus rendering. Screenshot inspected.

Tests: `run_tests.bat` is inherited RAD tooling (raddbg/torture) and fails with
"no valid build target". The real checks are the `--*_diagnostics` flags, run
with temporary user/project directories:

| Diagnostic | Result |
| --- | --- |
| sidebar | pass |
| scroll_region | pass |
| preview | pass |
| tooltip | pass |
| panel | pass |
| terminal_glyph | **fail** |

`terminal_glyph` errors were discarded because the diagnostic runs after the
log scope closes; commit `15a8a5d` on this branch prints them to stderr. Both
failures share one cause: nothing renders via the source-colour emoji path
(`source_like=0`, `emoji=0`). D3D11 readback itself works.

The runs created `%APPDATA%\uishell\logs` even with `--user` (isolation bug).

## Source inference: non-portable subsystems and their layers

RAD's split is intact: `base/*` neutral APIs with `win32/base`, `mac/base`,
`linux/base`; `window_manager` and `render/{d3d11,metal,opengl}` likewise. The
win32 and mac window managers export the same `wm_*` API including file drop.
D3D11 supports pixel readback.

- **Jackstay client** (`src/jackstay/wheelhouse_jackstay.c`, only with
  `WHEELHOUSE_JACKSTAY`): pthreads, `AF_UNIX`, `poll`, `signal`. Threads move
  mechanically to base; sockets need a new **`base_ipc`** layer (local stream
  endpoint: connect/accept/read/write with timeouts; Unix sockets on POSIX,
  named pipes on win32). `build.bat` has no Jackstay wiring.
- **Ingress** (`src/ingress/lib.rs`): axum/tokio over a Unix socket;
  `cfg(not(unix))` returns unsupported. Needs a Windows listener (tokio named
  pipe carrying HTTP/1); Flotilla `pm connect` and `tools/andamento-publish.py`
  must match.
- **Daily driver** (`tools/daily-driver.py`, `scripts/run-daily-driver.sh`):
  `fcntl.flock`, `os.killpg`, `AF_UNIX`, `/tmp`.
- **Scattered ifdefs** that belong behind layers:
  - `uishell_terminal_glyph.c:242` hard-codes the Apple Color Emoji path →
    font provider system-emoji lookup.
  - `OS_MAC` blocks deciding whether failed readback is fatal → render
    capability query.
  - `uishell_views.c:3542` copy/paste chord → keymap.
  - `uishell_sidebar.c:155/191` Windows shell command → base default-shell API.
- **Terminal panes** use only Cleat's FFI: in-process sessions via Cleat's pty
  layer (ConPTY), daemon sessions via Cleat's named-pipe transport. The Kitty
  image path is layer-neutral (Cleat image → `r_tex2d_alloc` RGBA8 →
  `dr_img`). No live pane or on-screen Kitty image was exercised; inbox ConPTY
  drops Kitty APC (see the Katzensteg research), so this needs bundled ConPTY.
- Colour emoji likely needs DirectWrite colour-glyph image support
  (PNG/CBDT via `IDWriteFactory4`) or an embedded fallback.

## Proposed packets

1. (S) Build hygiene: copy `ghostty-vt.dll`, replace `run_tests.bat` with a
   diagnostics runner, regenerate metagen with a drift check, keep the stderr
   patch, add ghostty-vt build and passing diagnostics to Windows CI.
2. (S–M) Terminal pane smoke (in-process and daemon) with bundled ConPTY and a
   Kitty image check.
3. (M) Colour emoji in the DirectWrite provider plus system emoji lookup.
4. (S) Move the uishell ifdefs behind keymap, render capability and default shell.
5. (M) `base_ipc` layer; port the Jackstay C client and threads onto base.
6. (M) Windows ingress over the chosen transport and matching Flotilla client.
7. (M) Daily driver port.
8. (L) Jackstay graphics on Windows, after Jackstay's own work.
