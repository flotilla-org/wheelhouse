# Wheelhouse

Wheelhouse is a native application for composing workspaces from tabbed panels and embedded views, derived from the RAD Debugger UI stack. The product target is `wheelhouse`; the original debugger repository remains the reference/oracle at `/Users/robert/dev/raddebugger`.

## Build

On macOS/Linux:

```sh
bash build.sh wheelhouse
```

On macOS, build an app bundle with:

```sh
bash build.sh bundle
```

On Windows:

```bat
build wheelhouse
```

The executable is `build/wheelhouse` (`build/wheelhouse.exe` on Windows); the macOS bundle is `build/Wheelhouse.app`. Build and diagnostic overrides use the `WHEELHOUSE_` environment-variable prefix.

Internal source names and existing configuration storage still use `uishell`.

The native-build CI workflow checks exact Cleat, Andamento and Jackstay revisions, recorded in `.github/workflows/build.yml`. It builds on macOS with Ghostty and on Linux/Windows with Cleat’s no-VT variant, and runs workspace bridge diagnostics on macOS and Linux. Windows runtime behavior and Ghostty on Linux/Windows are not covered by these jobs. Local builds continue to use the configured sibling checkouts. Andamento is private, so CI requires a `CI_APP_ID` repository variable and `CI_APP_PRIVATE_KEY` secret for a GitHub App installed on Andamento with Contents read-only permission. The workflow requests a short-lived token scoped to Andamento. Fork PRs do not receive the App secret.

## Jackstay views

On macOS/Linux, **Open Jackstay Source** opens a video panel with optional keyboard
and pointer input. Builds require `../jackstay`, or `WHEELHOUSE_JACKSTAY_DIR`, and
compile its C library with Cargo. See [Jackstay views](docs/design/jackstay-view.md)
for endpoint configuration, focus behaviour and acceptance checks.

## Sidebar fixture

The control region has **Workspaces** and **Andamento** modes. The choice is saved per window. Workspaces keeps the preview selector; Andamento renders an embedded example project and terminal using the shared core. The same terminal appears under Projects and Attention, with independent collapse state.

Click **Example workspace** for a primary terminal on the left and Shell/Tools tabs on the right, split 60/40. **Example terminal** keeps the single-terminal layout. Each entry creates or focuses its own workspace. Switch to Workspaces to close it; the example then becomes available again. Existing fixture workspaces are rebound after restart without creating duplicates. The facts and status are examples, not live git or Flotilla information.

Builds require the sibling `../andamento` checkout with C ABI 2. Wheelhouse builds the FFI and core through a separate Cargo consumer workspace with a committed lockfile in `tools/andamento-build/`, so no Zellij checkout is needed. Override its location with `WHEELHOUSE_ANDAMENTO_DIR`, or its Cargo output directory with `WHEELHOUSE_ANDAMENTO_TARGET_DIR`. The build explicitly selects the native Rust target rather than Andamento's WASM default. The multi-terminal resource list lives in `data/sidebar/resources.json`, with the primary first. This is host-side fixture data; dynamic terminal resource discovery and updates are not implemented yet. Fixture inputs live in `data/sidebar/`; the build embeds them into the executable.

On macOS (or Linux with a display), run the native workspace bridge checks with:

```sh
bash tools/run-sidebar-diagnostics.sh
```

These use temporary configuration files and exercise materialisation, focus, closure, failed focus, retry, and restoration. These fixture diagnostics do not exercise pane observations. Live HTTP/UDS ingress has separate producer tests below.

## Scroll region fixture

Run `./build/wheelhouse --scroll_region_fixture` to open a two-axis grid. Its
buttons switch between classic and overlay bars and between large and fitting
content. Drag either thumb, use Shift-wheel for horizontal scrolling, and resize
the view to exercise automatic gutters. Use temporary `--user:<path>` and
`--project:<path>` files to keep fixture tabs out of your usual workspace.

`bash tools/run-scroll-region-diagnostics.sh` builds and checks viewport geometry,
corner clearance, dragging outside the region, and wheel routing with synthetic
UI events. These checks require a graphical session (Xvfb works on Linux).

## Scope

The shell keeps the platform, windowing, renderer, font, UI, config, panel, tab, text, file-stream, and content-cache layers needed for an empty RAD-style window and file-backed text/binary views.

Debugger app targets and local RAD utility/tool build targets have been removed from this tree. Do not reintroduce them as regression gates; use the original RAD Debugger checkout for debugger behavior comparisons.

### Daily driver (macOS/Linux)

```sh
scripts/run-daily-driver.sh
```

This builds Wheelhouse, launches its live Andamento sidebar, watches the current
checkout with the Python git producer, and runs `flotilla pm connect` against the
local daemon. It uses Wheelhouse's native `data/sidebar/daily-driver.kdl` template.
Projects contain checkouts, convoys/vessels, and issues; sessions have their own
section. Attention is a second placement of the same entities. New live windows
start in Andamento mode; an existing saved sidebar choice takes precedence.

Use the disclosure arrow to expand a branch. Clicking an entry runs its supplied
recipe in a native terminal workspace, or focuses its existing workspace, including
when it was opened from Attention or an alias such as a one-vessel convoy.
The current workspace has a highlighted row. Tooltips explain each entry's action;
entries without a recipe show information inside the sidebar when clicked.
Sections scroll independently, and display controls stay at the bottom.
The native template gives checkouts workspace presence, so the git producer's
shell recipe can open too. It does not use the legacy grouping tree in Andamento's
Zellij template, which native snapshots deliberately omit.

Build Flotilla with the HTTP/UDS sink first (Flotilla PR #1860 or newer). The
launcher expects `../flotilla/target/debug/flotilla`, as the Zellij daily driver
does, and does not rebuild Flotilla or start a Zellij session.

```sh
# Git facts only; no Flotilla binary or daemon needed.
scripts/run-daily-driver.sh --git-only
# Watch several checkouts and use an existing Wheelhouse build.
scripts/run-daily-driver.sh --no-build --repo ~/dev/wheelhouse --repo ~/dev/flotilla
# Use a particular Flotilla build.
FLOTILLA_BIN=/path/to/flotilla scripts/run-daily-driver.sh
```

Settings and layouts persist in `${XDG_CONFIG_HOME:-~/.config}/wheelhouse/daily-driver`.
Set `WHEELHOUSE_DAILY_DIR` to use a different profile. Only one launcher may use a
profile at a time. Each launch gets a fresh private socket under `/tmp`, printed
as `WHEELHOUSE_SOCKET` for additional producers and inherited by the app and its
terminals. Closing Wheelhouse or pressing Ctrl-C stops the launcher-owned
producers and removes that runtime directory. The Flotilla daemon follows its
normal lifecycle. A producer exiting unexpectedly stops the daily driver and
reports its log path. Logs for active components in the profile's `logs/`
directory are replaced on the next launch; saved layouts are retained.

`WHEELHOUSE_BIN` selects an existing binary and skips the build. `FLOTILLA_ROOT`
overrides the sibling Flotilla checkout. `WHEELHOUSE_ANDAMENTO_DIR` (or
`ANDAMENTO_ROOT`) selects Andamento; `WHEELHOUSE_ANDAMENTO_CONFIG` overrides the
KDL template. Normal `WHEELHOUSE_CLEAT_*` build overrides also apply.

`python3 tools/test-native-sidebar.py /path/to/libandamento_ffi.dylib` (or `.so`)
checks the shipped hierarchy, opening capability, and shared Open/Focus identity
through the real C ABI.

`python3 tools/test-daily-driver.py` checks producer delivery, profile locking,
startup failure, producer failure, restart, and process cleanup with a fake UI.

### Live Andamento facts (Unix)

Launch a separate Wheelhouse instance with a socket in a private directory and
an Andamento KDL template. The listener broadcasts facts to its windows;
selection and collapse remain local to each window. Select **Andamento** in
the workspace selector. This mode starts without the example facts.

```sh
runtime_dir=$(mktemp -d /tmp/wheelhouse.XXXXXX)
./build/wheelhouse --user:"$runtime_dir/user" --project:"$runtime_dir/project" \
  --andamento_socket:"$runtime_dir/facts.sock" \
  --andamento_config:data/sidebar/daily-driver.kdl
```

From another terminal, publish real git facts without Flotilla:

```sh
python3 tools/andamento-publish.py --socket /path/to/facts.sock --repo /path/to/checkout
```

Or use a Flotilla build with the HTTP sink:
`flotilla pm connect --wheelhouse-socket /path/to/facts.sock`.
The transport contract is in [docs/protocol/pm-connect.md](docs/protocol/pm-connect.md).
Clicking an entry runs its supplied recipe in an ordinary Terminal View;
Flotilla's recipes use `flotilla attach` or `flotilla view`. Resource enumeration
and dynamic overflow tabs remain follow-up work. A new window receives facts
on the producer's next periodic reassertion.

The build now also produces `wheelhouse_ingress`, a Wheelhouse-owned Rust HTTP
adapter with a small C boundary. Andamento's core remains transport-independent.
Ingress is opt-in; Windows builds retain the fixture sidebar but reject the
Unix listener option. Clean application shutdown removes its socket. After a
crash, remove the stale socket before restarting with the same path.
