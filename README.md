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

The native-build CI workflow checks exact Cleat and Andamento revisions, recorded in `.github/workflows/build.yml`. It builds on macOS with Ghostty and on Linux/Windows with Cleat’s no-VT variant, and runs workspace bridge diagnostics on macOS and Linux. Windows runtime behavior and Ghostty on Linux/Windows are not covered by these jobs. Local builds continue to use the configured sibling checkouts. Andamento is private, so CI requires a `CI_APP_ID` repository variable and `CI_APP_PRIVATE_KEY` secret for a GitHub App installed on Andamento with Contents read-only permission. The workflow requests a short-lived token scoped to Andamento. Fork PRs do not receive the App secret.

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

### Live Andamento facts (Unix)

Launch a separate Wheelhouse instance with a socket in a private directory and
an Andamento KDL template. The listener broadcasts facts to its windows;
selection and collapse remain local to each window. Select **Andamento** in
the workspace selector. This mode starts without the example facts.

```sh
runtime_dir=$(mktemp -d /tmp/wheelhouse.XXXXXX)
./build/wheelhouse --user:"$runtime_dir/user" --project:"$runtime_dir/project" \
  --andamento_socket:"$runtime_dir/facts.sock" \
  --andamento_config:../andamento/templates/flotilla-default.kdl
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
