# Jackstay views

A Jackstay view consumes CPU video and optionally produces cooperative input.
It fits in an ordinary Wheelhouse panel/workspace. Jackstay owns transport,
frame leases, input ordering, execution results and cleanup. Wheelhouse owns
presentation, input focus and the association between a view and its endpoints.

## Opening and restoring

Use **Open Jackstay Source** in the command menu. Choose a combined source
endpoint, or separate media/input endpoints, enter their addresses, and
connect. Combined endpoints use ABI 0.8 shared bootstrap with optional input.
Separate endpoints support existing CPU republications, including Porthole's.
An absent or refused input channel leaves an observation view.

An address is a [Local Endpoint](../adr/0011-windows-local-ipc-uses-named-pipes-with-logical-endpoints.md)
name, such as `my-source`, on every platform. Prefix it with `session:` for a
session-scoped endpoint. Jackstay renders the name itself (a named pipe on
Windows, a socket under the runtime directory elsewhere), connects, and verifies
that the server runs as the current user before any setup byte. On macOS/Linux an
absolute socket path is also accepted, for publications that still bind a path.
Windows has no path form; there is no mapping from a path to a pipe name.

The connection form saves `source_endpoint` (combined) or `media_endpoint` and
`input_endpoint` (separate) in the view configuration. Socket paths keep the
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

## Build and checks

Builds on every platform require a sibling Jackstay checkout, or
`WHEELHOUSE_JACKSTAY_DIR`. `WHEELHOUSE_JACKSTAY_TARGET_DIR` optionally selects its
Cargo output directory. `build.sh` and `build.bat` call locked Cargo for the C
library; `build.bat` links `jackstay.dll.lib` and copies `jackstay.dll` beside
`wheelhouse.exe`. Runtime admission requires an exact ABI match. CI pins Jackstay
in `.github/workflows/build.yml`.

```sh
WHEELHOUSE_JACKSTAY_DIR=/path/to/jackstay bash build.sh wheelhouse
WHEELHOUSE_JACKSTAY_DIR=/path/to/jackstay python3 tools/test-jackstay.py
```

On Windows, run `python tools\test-jackstay.py` from a Visual Studio developer
prompt after `build wheelhouse`; it compiles with `cl`.

The session acceptance suite compiles Wheelhouse's actual module, on RAD's base
layer, and Jackstay's independent reference source into separate processes. They
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
attachment. Local Porthole native captures need an additional attachment adapter
or upstream CPU exposure. Neither belongs in the panel drawing code.

Native GPU import, audio, relative pointer, full IME and clipboard transport are
outside this first slice. Profile CPU upload/copy costs before adding GPU imports.

A later Kitty extension can reuse the stream owner while terminal placements
supply geometry, cropping and stacking. It still needs explicit rules for source
identity/resolution, remote republication, scrollback, replay and input ownership.
Opening an image placement must not implicitly grant input control.
