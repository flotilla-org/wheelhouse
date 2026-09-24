# Kitty images in a Windows Cleat pane, 24 September 2026

For [#60](https://github.com/flotilla-org/wheelhouse/issues/60).

## Revisions and host

- Host: Beaufort, Windows 11 Pro 10.0.26200, over RDP. Wheelhouse uses its default
  D3D11 renderer: `d3d11.dll` and `dxgi.dll` are loaded and `opengl32.dll` is not.
- Wheelhouse: this branch, based on `c4a5a50`, built with `build.bat wheelhouse`
  (MSVC, debug).
- Cleat: `adeb033` (flotilla-org/cleat#235), in a clean detached worktree with Ghostty
  `c3dbb925`. The bundled ConPTY is `Microsoft.Windows.Console.ConPTY` 1.24.260710001,
  fetched by Cleat's `tools/prepare-conpty.ps1`. build.bat copied `conpty.dll`
  (SHA-256 `39fba271…`) and `OpenConsole.exe` (`b7fd936c…`) beside `wheelhouse.exe`.
  Both hashes match the package's x64 files.
- kitty-image-tests: `f1ae26d` (rjwittams/kitty-image-tests#32, which registers the
  detect/query visual observations so the runs can be packed).

## Method

- I launched Wheelhouse with temporary `--user`/`--project` files whose only window
  held one terminal tab. The tab's expression started the kitty-image-tests producer,
  which waits for the runner's named pipe. The runner then attached with
  `--attach-only --surface-id`, with stages `detect,query,explicit-png,explicit-rgba`.
  It took the screenshots through Porthole, of the Wheelhouse window only.
- Porthole was a separate `windows_foreground_server` instance with an in-memory policy
  store. A temporary agent identity held only a launch grant for its own launches and
  per-window observe, drive and manage grants. I revoked it and stopped the server
  afterwards.
- The daemon-backed panes (`daemon: 1`, `daemon_name: "kt60"`) used a Cleat daemon
  named `kt60` under a private runtime root (`CLEAT_RUNTIME_DIR`), run from the Cleat
  worktree's `target\debug`, where Cleat's build staged the bundled ConPTY. I stopped
  it afterwards. No other Porthole or Cleat daemon was touched.

## Results

Observed on Beaufort unless marked otherwise.

| Pane | ConPTY evidence | Kitty query | PNG | RGBA |
|---|---|---|---|---|
| In-process | Child `OpenConsole.exe` from `build\`; `conpty.dll` from `build\` loaded | pass (`_Gi=31;OK`) | Rendered | Rendered |
| In-process, `CLEAT_CONPTY=inbox` | Child `conhost.exe --headless`; no `conpty.dll` | fail; graphics stages skipped | — | — |
| Daemon-backed | `cleat inspect`: `conpty: bundled 1.24.260710001`, `graphics_passthrough: true`, path beside `cleat.exe` | pass | Rendered after the observation screenshot (see below) | Rendered |

I inspected these screenshots myself:

- In-process, bundled: `explicit-png__explicit-png-visual.png` shows the Zellij logo
  filling the boxed area. `explicit-rgba__rgba-visual.png` shows the RGB gradient under
  the alpha checker in the left box and the plain gradient control on the right.
  `query__query-visual.png` has every query case PASS and the a=q box empty.
  `detect__detect-visual.png` shows "kitty basic query yes", DA1 `62;22` and XTVERSION
  `libghostty`.
- In-process, inbox: `detect__detect-visual.png` shows "kitty basic query no". DA1 is
  `61;6;7;21;22;23;24;28;32;42`, which conhost answered itself. The final pane shows
  "SKIP: kitty graphics query did not succeed."
- Daemon-backed: `explicit-rgba__rgba-visual.png` matches the in-process result. Both
  `explicit-png` screenshots show the box without the logo. A PNG-only probe run then
  showed the logo in the box 6 s later, in a Porthole screenshot of the same window.
  The observation screenshot came before the image did. Porthole's Windows adapter
  has no stable-frame wait, so the runner captures as soon as the producer reports.
  I did not measure the delay or find where it comes from. Also observed: the
  daemon-backed pane answers the CSI 16t cell-size query with `6;1;1t`, where the
  in-process pane answers `6;18;9t`.

The image pixels appear in Wheelhouse's window, drawn by its D3D11 renderer. They
did not come from an outer terminal: the pane runs directly in Wheelhouse.

`tools/check-windows-conpty.ps1` passed with `-Expect bundled` and with
`-Expect inbox`. With `OpenConsole.exe` removed from `build\` it failed. A
Cleat-staged file missing from `build\` is copied again on rebuild. The five UI
diagnostics in `run_tests` still pass.

## Evidence bundles

The bundles are packed with `harness.bundle pack` and verified. They are catalogued
in kitty-image-tests' `site/run-bundles.json` (rjwittams/kitty-image-tests#32), and
the archives are kept on Beaufort under that repository's ignored `evidence-bundles/`.

| Run id | Bundle SHA-256 |
|---|---|
| `2026-09-24-windows-wheelhouse-cleat-inprocess-bundled-conpty-beaufort` | `4de85e5922f2185e0840390e9019034bce706febc209bb17f7a3772c0e85367d` |
| `2026-09-24-windows-wheelhouse-cleat-inprocess-inbox-conpty-beaufort` | `b4438bcdef3b1b0b8ffe3a8a702d48bc645285a429b088d11858c2a9795a6fa2` |
| `2026-09-24-windows-wheelhouse-cleat-daemon-bundled-conpty-beaufort` | `32df00a964b4fa13da750196757d96a9bd6f1cb80eec0976105687cf4b959d27` |
| `2026-09-24-windows-wheelhouse-cleat-daemon-bundled-conpty-beaufort-r2` | `56de5fec3663468d65d80d896ebe7d385632fd8f7fe78dddd92c390a9aee1125` |

The PNG-only probe (`2026-09-24-windows-wheelhouse-cleat-daemon-png-probe`) was packed
but not catalogued. Its later screenshot is outside the bundle.

## Not covered

- The inbox contrast was run for an in-process pane only.
- The daemon ran from the Cleat worktree. build.bat does not put `cleat.exe` beside
  `wheelhouse.exe`, so a daemon that Wheelhouse spawns itself uses whichever
  `cleat.exe` is on `PATH`, with the ConPTY files beside that copy.
- Other image stages, resize, scrollback and multiple panes were not run. Porthole's
  Windows adapter cannot place windows or report their content rect.
- The kitty-image-tests runner has no Wheelhouse terminal id. The runs pass
  `--terminal windows-terminal` as a placeholder, with `--terminal-name` naming the
  Wheelhouse pane.
- Nothing here shows the pane surviving RDP disconnect or a screen lock.
