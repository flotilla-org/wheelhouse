# Jackstay views

A Jackstay view consumes CPU video, or D3D11 frames on Windows, and optionally
produces cooperative input.
It fits in an ordinary Wheelhouse panel/workspace. Jackstay owns transport,
frame leases, input ordering, execution results and cleanup. Wheelhouse owns
presentation, input focus and the association between a view and its endpoints.

## Opening and restoring

Use **Open Jackstay Source** in the command menu. The mode button cycles
through four forms; enter the addresses and connect:

- **Combined source endpoint**: ABI 0.8 shared bootstrap with optional input.
- **Separate media/input endpoints**: existing CPU republications, including
  Porthole's.
- **D3D11 source endpoint** (Windows): a Jackstay D3D11 publication, such as
  Jackstay's `d3d11_source` example. See [D3D11 frames](#d3d11-frames-windows).
- **Porthole capture session** (Windows): a Porthole native capture session,
  given by the `native.endpoint`, `session_id` and `native.attach_token` that
  `porthole capture-session surface <id> --native --json` returns. Wheelhouse
  presents the token, and Porthole's reply names the publication that follows
  on the same connection: D3D11, or CPU when the capture device cannot share
  fences.

An absent or refused input channel leaves an observation view. D3D11 and
Porthole sources carry no input.

An address is a [Local Endpoint](../adr/0011-windows-local-ipc-uses-named-pipes-with-logical-endpoints.md)
name, such as `my-source`, on every platform. Prefix it with `session:` for a
session-scoped endpoint; `user:` names the default user scope explicitly.
Jackstay renders the name itself (a named pipe on Windows, a socket under the
runtime directory elsewhere), connects, and verifies
that the server runs as the current user before any setup byte. On macOS/Linux an
absolute socket path is also accepted, for publications that still bind a path.
Windows has no path form; there is no mapping from a path to a pipe name.

The connection form saves `source_endpoint` (combined), `media_endpoint` and
`input_endpoint` (separate), `d3d11_endpoint`, or `porthole_endpoint` and
`porthole_session` in the view configuration. Socket paths keep the
original `source_socket`, `media_socket` and `input_socket` keys, so existing
layouts restore unchanged. Restoring a saved layout shows the form again; it
does not connect or acquire control automatically. Direct endpoint opening
remains useful if discovery is added later.

```text
panels: selected jackstay:
{
  source_endpoint: "my-source"
  selected
}
```

The Porthole attach token is never saved: it lives in the running session only.
For scripted runs a layout may carry `attach_token` and a one-shot `connect: 1`.
Wheelhouse reads both when the view first builds, removes them from the
configuration, and connects for observation only (control still needs a click):

```text
panels: selected jackstay:
{
  porthole_endpoint: "porthole.capture.<id>"
  porthole_session: "<session id>"
  attach_token: "ptas_..."
  connect: 1
  selected
}
```

Clicking the image focuses the panel and also reaches the source when control
is already ready. A click made during connection is never replayed. Control
assignment lasts until disconnect/close; losing focus resets held input while
retaining assignment. Ctrl+Shift+Escape returns keyboard ownership to the shell.
Input focus does not tint or outline the video canvas.
Broader Cmd shortcut policy is deferred until the RAD input changes are adopted.
There is no clipboard synchronisation.

Media reconnects after failure; input is only reacquired through an explicit
click/retry. A busy or unavailable controller is not retried in the background.
The last frame remains visible with a disconnected status. The status row has a
fixed height. Disconnect returns to the connection form after workers retire.

## Lifetime and rendering

`src/jackstay/wheelhouse_jackstay.*` is independent of shell configuration and
GPU objects. It is built on RAD's base layer: its separate media and input
workers are `base_threads` threads synchronised with a base mutex, on every
platform. CPU setup can be cancelled; bootstrap and input admission retain
Jackstay's bounded blocking setup contract. No connection setup runs on the GUI
thread. Jackstay connects Local Endpoints itself (`ft_local_connect` and the
`_local` setup calls); only the POSIX path form opens a socket in Wheelhouse. The
media worker notices a vanished producer through
`ft_acquisition_cpu_connection_alive`, which never consumes setup bytes, rather
than peeking at a borrowed descriptor. Jackstay suppresses SIGPIPE on its own
sockets ([jackstay#18](https://github.com/flotilla-org/jackstay/pull/18)), so
the client changes no signal disposition or thread mask.

The media worker acquires one lease, copies packed RGBA into a bounded latest
frame mailbox, and releases the lease. BGRA and padded rows are converted there.
Frames above 64 MiB or with invalid dimensions/strides are rejected. The GUI
copies the newest available frame, with at most one upload per shell frame even
when both the workspace preview and main panel build the view.

Textures are immutable after upload. The initial dynamic-texture implementation
used Metal `replaceRegion` while previous GPU submissions could still sample
that texture, producing mixed rows/blocks. Allocating a static texture for each
new presentation avoids that race. A future reusable texture pool must retire
slots on actual GPU completion; a fixed number of buffers alone is not proof of
safe reuse. This first implementation prioritises coherence over upload cost.

Views have runtime owners outside the transient `RD_ViewState` arena. Hidden
views retain their connection; their input focus is cleared. Deleted views stop
workers and are reclaimed after they finish. App shutdown also drains these
owners before exiting. Destroying a local input handle never means remote
cleanup was confirmed; that outcome is recorded separately.

Input has bounded event/text queues and execution results. Queue overflow,
uncertain execution and rejected releases end control rather than dropping
transitions or replaying actions. Target geometry updates preserve keyboard
bindings and invalidate old pointer work. Displayed image coordinates are mapped
through the aspect-fit rectangle into the advertised logical input extent.
Resizing a panel does not request a source-window resize.

## D3D11 frames (Windows)

This follows Jackstay's decision ([jackstay#22](https://github.com/flotilla-org/jackstay/issues/22),
point 5) and its reference consumer, `tools/capture-viewer-d3d11`.

**One device, one adapter.** RAD's D3D11 renderer creates one device for the
whole UI, and every window, font atlas and texture lives on it. The adapter is
chosen once, at startup: `--render_adapter:<LUID>` (as Jackstay prints it,
`00000000:00009fe5`, or just `9fe5`), `--render_adapter:warp`, or the system
default. `r_adapter_info()` reports the chosen adapter's LUID, description and
whether it can share fences. The renderer never moves to another adapter while
running: that would mean recreating every GPU resource of every pane (RAD keeps
no CPU copies of static textures), and two sources on different adapters would
make it thrash. A second device only for the view is no better: its frames
could not be drawn by the UI's device without a cross-adapter copy, which the
decision rules out. Running the whole UI on a non-default adapter works: DXGI
presents a flip-model swap chain from any adapter (verified on WARP).

**Choosing the path.** After `ft_acquisition_d3d11_describe` names the
producer's adapter, the session's worker picks:

- **Import**, when the renderer's device is on that adapter and has a shared
  release fence (`ID3D11Device5`/`ID3D11DeviceContext4`). The worker attaches
  with the renderer's device, holding up to three frames, and registers the
  view's release fence. Each acquired frame moves to the GUI thread (a
  one-frame mailbox; an untaken frame is released at once, since nothing
  sampled it).
- **Read back**, otherwise: another adapter, no shared fences, a producer that
  refused the renderer's adapter (`FT_STATUS_ADAPTER_MISMATCH`), or an import
  the renderer could not open. The worker creates its own device on the
  producer's adapter, attaches with it, GPU-waits each frame's readiness value,
  copies it into a staging texture and maps it, then releases the frame at
  once: the map proves the copy finished. The pixels take the ordinary CPU
  path. This is a CPU copy, not a cross-adapter GPU copy.

The status row shows the frame size and the path with the producer's adapter,
for example `600x200 D3D11 import on 00000000:00009fe5 AMD Radeon(TM) Graphics`,
or `D3D11 read back on ...: the renderer is on another adapter (renderer ...)`.
A Porthole session whose device cannot share fences is a CPU publication and
takes the existing CPU setup on the same connection.

**Import, wait, release.** On the GUI thread the view imports the pool texture
with `OpenSharedResource1` (`r_tex2d_open_shared`, cached by incarnation, pool
and slot) and the readiness fence with `OpenSharedFence` (cached by incarnation
and fence id). It discards a frame whose readiness fence is abandoned
(`ft_d3d11_fence_alive`: the producer died and the copy may never have run),
keeping the last good frame. Otherwise it signals its release fence for the
frame it replaces, queues `ID3D11DeviceContext4::Wait` for the new frame's
readiness value on the renderer's immediate context, and draws it with the
rest of the UI. The next replacement (or disconnect, or closing the view)
signals the release fence after all work queued so far, which covers every
draw of the old frame, and defers that frame's release to the signalled value.
The CPU never waits for the producer or for the renderer's GPU work. The view
owns the release fence for its whole life, so values only increase across
reconnects; Jackstay keeps its own reference for releases still pending.

**Generations and epochs.** A resize arrives as `RECONFIGURATION`; the worker
relinquishes and installs the replacement pool, and the first frame of the new
pool drops imports of pools no held frame uses. A device-loss epoch closes
setup: the worker reconnects, and the new incarnation's frames are imported
afresh (caches are keyed by incarnation, since pool ids can repeat). A release
timeline outlives its incarnation while the GUI still holds one of its frames.

**Layering.** The render layer adds neutral hooks (`r_adapter_info`,
`r_native_device`, `r_tex2d_open_shared`, `r_timeline_*`, `r_queue_wait`,
`r_queue_signal`) that D3D11 implements and the other backends stub, so
`uishell_jackstay.c` has no platform branches. The Jackstay D3D11 calls and the
read-back device live in `src/jackstay/wheelhouse_jackstay_d3d11.c`, which the
session module includes on Windows only. Jackstay's D3D11 frame calls need a
library built with `backend-windows`; `build.bat` builds it so.

## Build and checks

Builds on every platform require a sibling Jackstay checkout, or
`WHEELHOUSE_JACKSTAY_DIR`. `WHEELHOUSE_JACKSTAY_TARGET_DIR` optionally selects its
Cargo output directory. `build.sh` and `build.bat` call locked Cargo for the C
library (`build.bat` with `backend-windows`); `build.bat` links
`jackstay.dll.lib` and copies `jackstay.dll` beside `wheelhouse.exe`.
Runtime admission requires an exact ABI match. CI pins Jackstay
in `.github/workflows/build.yml`.

```sh
WHEELHOUSE_JACKSTAY_DIR=/path/to/jackstay bash build.sh wheelhouse
WHEELHOUSE_JACKSTAY_DIR=/path/to/jackstay python3 tools/test-jackstay.py
```

On Windows, run `python tools\test-jackstay.py` from a Visual Studio developer
prompt after `build wheelhouse`; it compiles with `cl`.

The session acceptance suite compiles Wheelhouse's actual module, on RAD's base
layer, and Jackstay's independent reference sources into separate processes.
On Windows it also runs Jackstay's `d3d11_source` (resizing every 300 ms) three
ways: imported on a device on the producer's adapter, with the renderer
claiming another adapter, and with no renderer device. The last two must read
frames back. Each run sees frames of two sizes, survives the source being
killed and restarted, and finally waits for its own release fence. They
meet by Local Endpoint name on every platform; on macOS/Linux the input and
recovery cases also run against a socket path. It checks frame delivery, key
down/repeat/up, UTF-8 commits, pointer holds, focus cleanup, clean optional input
refusal, observation, missing endpoints and source-death recovery without
reacquiring control. Source output verifies executed events and final held state.

For a live source, run the reference with `--endpoint NAME --log-input` and add
`--repeat --resize-every-ms 4000` to exercise reconnection and source resizes;
[its README](https://github.com/flotilla-org/jackstay/blob/main/tools/capture-viewer-sdl/README.md)
has the Windows build line. The first Windows run is recorded in
[validation](../validation/jackstay-cpu-source-windows-2026-09-25.md).

`tools/jackstay-frame-source.c` is a low-contrast coherence source. Compile it
against the same Jackstay headers/library, run it on an unused socket, and open
that socket in Wheelhouse or the SDL reference viewer. Every frame is uniformly
96 or 104 grey. A mixed frame is a failure, not part of the pattern.

With Pillow installed, the macOS screenshot check is:

```sh
python3 tools/check-jackstay-presentation.py WINDOW_ID
```

Its crop must lie wholly inside the video; use `--crop` if the default central
crop includes letterboxing/chrome. It rejects a static/non-source crop, saves
mixed captures, and checks spatial coherence. It does not measure vsync, latency
or frame pacing.

Local macOS acceptance (2026-09-18): the session suite passed; the fixed
Wheelhouse renderer and SDL reference viewer each produced 0 mixed frames in 20
captures of an advancing coherence source. Katzensteg's SDL2 input probe received
mouse down/up, physical key down/up and text through the native Wheelhouse view.
The diagnostic is spatial only; display pacing/vsync remains unmeasured.

### Native input regression checks

On macOS, run `python3 tools/test-jackstay-native.py` after building Wheelhouse.
Set `WHEELHOUSE_JACKSTAY_DIR` for a non-sibling dependency and `WHEELHOUSE_BIN`
to test another build. The driver needs Accessibility permission and raises its
own temporary window; avoid interacting with that window during the run.

The test uses a separate instrumented source with gated admission. It verifies
that an early click is not replayed, a ready click delivers both transitions,
physical keys and text reach the source, resizing while a button is held resets
input, and Ctrl+Shift+Escape releases keyboard ownership until another click.
Shutdown must clear held input, retire both processes and exit successfully.
These checks are opt-in and are not part of headless CI.

The first native shutdown run reproduced a crash after the window/UI state had
been destroyed: asynchronous Jackstay cleanup kept the frame loop alive, which
re-entered `rd_frame` with retired UI state. The frame entry point now drains
Jackstay owners without rebuilding the shell after quit. The same native test
passes with that fix.

## Later stages

Porthole publication browsing and registry discovery resolve endpoints before
attachment; they do not belong in the panel drawing code. Until then a Porthole
capture session is opened by its endpoint, id and token.

Audio, relative pointer, full IME and clipboard transport are outside this
first slice. D3D11 import is Windows-only; Metal and OpenGL imports would
implement the same render hooks. The D3D11 evidence is in
[validation](../validation/jackstay-d3d11-windows-2026-09-25.md).

A later Kitty extension can reuse the stream owner while terminal placements
supply geometry, cropping and stacking. It still needs explicit rules for source
identity/resolution, remote republication, scrollback, replay and input ownership.
Opening an image placement must not implicitly grant input control.
