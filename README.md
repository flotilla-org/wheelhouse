# Wheelhouse

Wheelhouse is a native application for composing workspaces from tabbed panels and embedded views, derived from the RAD Debugger UI stack. The product target is `wheelhouse`; the original debugger repository remains the reference/oracle at `/Users/robert/dev/raddebugger`.

The [roadmap](docs/roadmap.md) records the current feature order, cross-repo
dependencies, and open reliability work. [Map issue #17](https://github.com/flotilla-org/wheelhouse/issues/17)
holds the broader destination.

## Build

On macOS/Linux:

```sh
bash build.sh wheelhouse
```

The default is an unoptimized debug build. For normal use and performance
measurements, build with optimizations enabled in both C and the Rust dependencies:

```sh
bash build.sh wheelhouse release
```

On macOS, build an app bundle with:

```sh
bash build.sh bundle release
```

On Windows:

```bat
build wheelhouse
run_tests
```

`build.bat` builds the sibling `..\cleat`, `..\andamento` and `..\jackstay` checkouts
(override with `WHEELHOUSE_CLEAT_DIR` / `WHEELHOUSE_ANDAMENTO_DIR` /
`WHEELHOUSE_JACKSTAY_DIR`) and copies their DLLs, including
`ghostty-vt.dll`, next to the executable. Cleat's default `ghostty-vt` feature needs
`tools\prepare-ghostty-vt.ps1` run in the Cleat checkout first. `run_tests` runs the
sidebar, scroll-region, preview, tooltip, panel and terminal-glyph diagnostics, each with
throwaway `--user`/`--project` files. Colour emoji come from Segoe UI Emoji; the
terminal-glyph diagnostic fails, naming the missing family, on a host without it.
After `build wheelhouse meta`, `python tools/check-generated.py` fails if committed
metagen output is stale.

Terminal panes on Windows need Cleat's bundled ConPTY: the inbox ConPTY drops Kitty
graphics. `build.bat` runs the Cleat checkout's `tools\prepare-conpty.ps1`, which fetches
and verifies the package pinned in Cleat's `tools\conpty.toml` on first use. It then copies
`conpty.dll`, `OpenConsole.exe` and `conpty-LICENSE.txt` next to `wheelhouse.exe`. Cleat
uses them only from the executable's directory, and only when both binaries are there.
`tools\check-windows-conpty.ps1` opens an in-process pane and checks that it runs under
that `OpenConsole.exe`. Pass `-Expect inbox` to run it with `CLEAT_CONPTY=inbox`, which
forces the inbox fallback. Daemon-backed panes use the files beside `cleat.exe`, and
`cleat inspect` reports the ConPTY each session uses.

The executable is `build/wheelhouse` (`build/wheelhouse.exe` on Windows); the macOS bundle is `build/Wheelhouse.app`. Build and diagnostic overrides use the `WHEELHOUSE_` environment-variable prefix.

Internal source names and existing configuration storage still use `uishell`.

The native-build CI workflow checks exact Cleat, Andamento and Jackstay revisions, recorded in `.github/workflows/build.yml`. It builds on macOS and Windows with Ghostty and on Linux with Cleat’s no-VT variant, runs the UI diagnostics and the Jackstay session acceptance on all three, and on Windows checks that committed metagen output is current and that an in-process pane runs under the bundled ConPTY. Ghostty on Linux is not covered by these jobs. Local builds continue to use the configured sibling checkouts. For terminal hosting changes, use a sibling Cleat checkout at the revision pinned by `CLEAT_REV` or a descendant. CI checks out the public Andamento repository without an App credential, including for fork PRs.

## Terminal hyperlinks

Hover explicit OSC 8 links to inspect their destinations. Cmd-click on macOS or
Ctrl-click on Linux/Windows opens HTTP(S) links; Shift-drag retains local text
selection. See [terminal hyperlinks](docs/terminal-hyperlinks.md) for the URI
policy, Cleat requirements, and interactive validation steps.

## Jackstay views

**Open Jackstay Source** opens a video panel with optional keyboard and pointer
input, on Windows, macOS and Linux. Sources are named by Local Endpoint (ADR 0011):
a named pipe on Windows, a Unix socket elsewhere. Builds require `../jackstay`, or
`WHEELHOUSE_JACKSTAY_DIR`, and compile its C library with Cargo; `build.bat` copies
`jackstay.dll` next to the executable. See [Jackstay views](docs/design/jackstay-view.md)
for endpoint configuration, focus behaviour and acceptance checks.

## Workspace previews

Overview bounds background terminal starts, snapshot fetches and surface redraws,
retaining completed previews while newer work waits. Small text previews refresh
less frequently and use textures sized to their displayed demand. Selected and
expanding workspaces remain immediate; preview scaling preserves terminal dimensions.

These policies are enabled by default. For diagnostics and comparisons, disable
them individually with `--no_preview_render_budget`, `--no_preview_surface_budget`
or `--no_preview_refresh_budget`. See the [preview performance guide](docs/validation/overview-performance.md)
for measurements, reproduction commands and limits.

## Sidebar

The Andamento tree is the workspace sidebar. Without a producer, it lists local
workspaces under Workspaces, which opens with a New workspace row; live catalog
entries appear when connected to `pm connect`. Hovering a row shows detach or
close in the margin beside it; hold it for the row's menu. Workspace previews
are available on hover, with the full overview and Reveal in the toolbar.
**Window → Workspace Settings** edits the current workspace's properties,
including its name.
Old saved `sidebar_mode` values no longer select a different sidebar.

Hover cards appear after 300 ms, then swap immediately as you scan rows or
chips. Move into a card to reveal actions and related navigation; click it to
keep it open and give it keyboard focus. Escape dismisses a click-focused card,
while a peek leaves Escape with the focused View. Related items navigate inside
the card with Back; Cmd-click or Ctrl-click opens a second
card. Near is the default placement. User Settings includes **Hover Cards
Outside Sidebar** for the alternate placement. Both states retain live workspace previews. See the
[acceptance notes](docs/hover-card-acceptance.md).

Run `./build/wheelhouse --sidebar_fixture` with temporary user/project files to
show the embedded example catalog. **Example workspace** opens a primary terminal
on the left and Shell/Tools tabs on the right, split 60/40. **Example terminal**
uses a single terminal. Each entry creates or focuses its own workspace. Close it
with the toolbar control to make the example available again. The facts and
status in this fixture are examples, not live git or Flotilla information.

Builds require the sibling `../andamento` checkout with C ABI 3. Wheelhouse builds the FFI and core through a separate Cargo consumer workspace with a committed lockfile in `tools/andamento-build/`, so no Zellij checkout is needed. Override its location with `WHEELHOUSE_ANDAMENTO_DIR`, or its Cargo output directory with `WHEELHOUSE_ANDAMENTO_TARGET_DIR`. The build explicitly selects the native Rust target rather than Andamento's WASM default. The multi-terminal resource list lives in `data/sidebar/resources.json`, with the primary first. This is host-side fixture data; dynamic terminal resource discovery and updates are not implemented yet. Fixture inputs live in `data/sidebar/`; the build embeds them into the executable.

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

### Dashboards

One Wheelhouse process opens one Dashboard: its workspaces (with their views
and how they are arranged), sidebar sections (and where they are docked, and
which are closed), groups, pins and order. `--dashboard:<name|path>` names it; without it, the
Dashboard opened last on this device opens, else a new one named `default`. A name
is a directory under `dashboards/` in the platform's config folder
(`${XDG_CONFIG_HOME:-~/.config}/wheelhouse`, `~/Library/Application Support/Wheelhouse`,
`%APPDATA%\Wheelhouse`). Its windows, focus, labels and terminal sessions
within its workspaces, and the sidebar's sizes and collapsed sections, are this
device's own, kept under `presentation/` in
`${XDG_STATE_HOME:-~/.local/state}/wheelhouse`, the same macOS folder, or
`%LOCALAPPDATA%\Wheelhouse`. The user file keeps this device's
fonts, keybindings and theme. An explicit `--user` keeps all of these beside it.
Windows saved in a user file before Dashboards are not imported; they stay in it,
unread, for an older build. See `src/uishell/uishell_dashboard.c` for the layout.

### Daily driver (macOS/Linux)

```sh
scripts/run-daily-driver.sh
```

This builds Wheelhouse and the native Andamento git watcher, launches its live Andamento
sidebar, discovers git worktrees from configured roots and terminal directories, and
subscribes its Dashboard to the local Flotilla daemon (see "Dashboard subscriptions"
below), whose `flotilla pm connect` Wheelhouse runs. It uses Wheelhouse's native
`data/sidebar/daily-driver.kdl` template. Projects contain checkouts, convoys/vessels,
and issues; sessions and Git have their own sections. Attention is a second placement of
the same entities. The project tree is the only sidebar, including when an older saved
`sidebar_mode` value exists.

Use the disclosure arrow to expand a branch. Clicking an entry runs its supplied
recipe in a native terminal workspace, or focuses its existing workspace, including
when it was opened from Attention or an alias such as a one-vessel convoy. The
current workspace has a highlighted row. Tooltips explain each entry's action;
entries without a recipe show information inside the sidebar when clicked. Sections
are docked Views. Single-section sidebar panels show a header with a count and
compact display toggles; merged panels show compact tabs. Drag the header's hover
handle to reorder or merge sections within their owning Controlled Split level.
Fleet sections stay available while child workspaces switch; they cannot be docked
into those workspaces. Sections retain independent scrolling, and their panel boundaries
resize their allocation. Reveal expands a collapsed section to show its workspace.
Closed sections stay closed on restart; restoring them is tracked in #183.
Sections added by a later template are not automatically inserted into an existing
saved arrangement; placement hints and migration remain #162's work.
Display-variable toggles follow the template's persistence
declarations. The native template gives worktrees workspace presence, so the git
producer's shell recipe opens a terminal in its root. It does not use the legacy
grouping tree in Andamento's Zellij template, which native snapshots deliberately
omit.

Use a Flotilla binary with the HTTP/UDS `pm connect` sink. The launcher prefers
`~/.local/opt/flotilla-fleet/current/bin/flotilla` when installed, passing that path
unresolved so each connector restart follows `current` to the fleet generation. Without
a fleet install it uses `${FLOTILLA_ROOT:-../flotilla}/target/debug/flotilla`.
`FLOTILLA_BIN` overrides both. The launcher passes it to Wheelhouse
(`--flotilla_bin`) and asks for a subscription to `--daemon`, else `FLOTILLA_DAEMON`,
else Flotilla's own daemon (`--flotilla_subscription[:<daemon>]`), which Wheelhouse
adds to the Dashboard unless it has one. It does not rebuild Flotilla or start a
Zellij session. If a connector reports a wire build mismatch, Wheelhouse notes both
builds and a `FLOTILLA_BIN` hint in its log and the connector's,
`logs/flotilla-<subscription ID>.log`.

```sh
# Git facts only; adds no Flotilla subscription (ones the Dashboard has still connect).
scripts/run-daily-driver.sh --git-only
# Watch several checkouts using existing Wheelhouse and watcher builds.
scripts/run-daily-driver.sh --no-build --repo ~/dev/wheelhouse --repo ~/dev/flotilla
# Use a particular Flotilla build.
FLOTILLA_BIN=/path/to/flotilla scripts/run-daily-driver.sh
```

Settings persist in `${XDG_CONFIG_HOME:-~/.config}/wheelhouse/daily-driver`, and
the launcher opens the Dashboard `dashboards/daily` there; `--dashboard NAME|DIR` (or
`WHEELHOUSE_DASHBOARD`) opens another. Set `WHEELHOUSE_DAILY_DIR` to use a different profile. Only one launcher may use a
profile at a time. Each launch gets a fresh private socket under `/tmp`, printed as
`WHEELHOUSE_SOCKET` for additional producers and inherited by the app and its terminals.
Closing Wheelhouse or pressing Ctrl-C stops the launcher-owned producers and removes
that runtime directory. The Flotilla daemon follows its normal lifecycle. A producer
exiting unexpectedly stops the daily driver and reports its log path. Logs for active
components in the profile's `logs/` directory are replaced on the next launch; saved
layouts are retained.

`WHEELHOUSE_BIN` selects an existing binary and skips the build. `FLOTILLA_ROOT`
overrides the sibling Flotilla checkout. `WHEELHOUSE_ANDAMENTO_DIR` (or
`ANDAMENTO_ROOT`) selects Andamento; `WHEELHOUSE_ANDAMENTO_CONFIG` overrides the KDL
template. `ANDAMENTO_GIT_WATCHER_BIN` selects an existing watcher and skips its build;
otherwise the launcher builds the native target of the sibling `andamento-git-watcher`
crate. `--no-build` reuses both binaries, and `--no-git` omits the watcher entirely. The
launcher defaults to debug builds; for release `--no-build` use `WHEELHOUSE_BIN` and
`ANDAMENTO_GIT_WATCHER_BIN` to select the release binaries. Normal `WHEELHOUSE_CLEAT_*`
build overrides also apply.

`python3 tools/test-native-sidebar.py /path/to/libandamento_ffi.dylib` (or `.so`) checks
the shipped hierarchy, opening capability, and shared Open/Focus identity through the
real C ABI.

`python3 tools/test-daily-driver.py` checks producer delivery, profile locking, startup
failure, producer failure, restart, the subscription and Flotilla it asks Wheelhouse
for, and process cleanup with controlled UI and watcher processes. Real HTTP/UDS
delivery is covered by `tools/test-andamento-ingress.py`; connector restarts and
cleanup by `src/ingress/connector.rs`'s tests, and subscriptions end to end by the
behaviour harness (`tools/test-state-behaviour.py`).

### Daily driver (Windows)

Use CPython 3 (other Python interpreters are unsupported) and an existing
Flotilla Windows client with the SSH endpoint from
flotilla#2639. The remote CLI must provide `daemon-bridge`, and the client must
match the running daemon's protocol fingerprint. The launcher does not rebuild
Flotilla or upgrade the remote fleet. A remote candidate executable can be selected
by its absolute path in the endpoint.

```powershell
# From a Developer PowerShell with the native build dependencies configured:
scripts/run-daily-driver.ps1 --daemon ssh://udder
# Reuse an existing Wheelhouse build and select compatible Flotilla binaries:
$env:WHEELHOUSE_BIN = 'C:\dev\wheelhouse\build\wheelhouse.exe'
$env:FLOTILLA_BIN = 'C:\dev\flotilla-ssh\target\debug\flotilla.exe'
scripts/run-daily-driver.ps1 --no-build --daemon ssh://udder/home/robert/candidates/flotilla
```

`--daemon` overrides `FLOTILLA_DAEMON`; the selected endpoint is the daemon the
Dashboard subscribes to, and is inherited by the UI and its terminals. Without either, the Windows launcher fails
before starting Wheelhouse. The default binaries have an `.exe` suffix. Without
`WHEELHOUSE_BIN` or `--no-build`, the launcher runs `build.bat wheelhouse`.
The PowerShell entrypoint uses `py -3` to select an interpreter, then runs that
Python executable directly. It falls back to `python` when `py` is absent; set
`WHEELHOUSE_PYTHON_BIN` to select a particular Python executable. Running
`python tools/daily-driver.py` directly supports the same options.

Settings and layouts persist in `%LOCALAPPDATA%\Wheelhouse\daily-driver`;
`WHEELHOUSE_DAILY_DIR` selects a separate profile. A byte-range lock prevents
concurrent launchers from using the same profile. Custom profile directories
retain their existing Windows ACLs. Each launch creates a fresh
`\\.\pipe\wheelhouse-daily-<unique-name>` endpoint. Readiness uses HTTP health
over the pipe with same-user server verification, matching ADR 0011. The app
and its terminals inherit `WHEELHOUSE_SOCKET` for additional producers.

Wheelhouse runs the connector in a Job Object of its own and restarts it with
backoff while it stays open. Logs live in the profile's `logs` directory. Closing
Wheelhouse or pressing Ctrl-C stops the launcher-owned process trees, and with
Wheelhouse the connector's, including its SSH subprocesses. Windows Job Objects
also clean up children if the Python launcher is abruptly terminated. Helper
processes do not create visible console windows. Saved profiles are retained
across launches; remote daemons remain running.

Windows currently publishes the Flotilla catalog only. Local git discovery is
disabled by default, and `--repo` / `--git-only` are refused until
[andamento#123](https://github.com/flotilla-org/andamento/issues/123) provides the
watcher's named-pipe sink. Clickable Windows recipes and remote terminal
attachment remain [flotilla#2470](https://github.com/flotilla-org/flotilla/issues/2470).
Permanent connector-error classification remains flotilla#2589; the launcher
retains the existing retry policy.

`python tools/test-daily-driver-windows.py` checks native pipe readiness, profile
locking, endpoint inheritance and subscription, settings retention, UI closure,
Ctrl-C, abrupt termination and descendant cleanup. These contracts use controlled
processes and a health-only named-pipe server, independently of a fleet or native
Wheelhouse build. Real metadata ingress is covered by `tools/test-andamento-ingress.py`.

### Live Andamento facts (Unix)

Launch a separate Wheelhouse instance with a socket in a private directory and an
Andamento KDL template. The listener broadcasts facts to its windows; selection and
collapse remain local to each window. The tree starts without example facts and shows
local workspaces until a producer publishes entries.

```sh
runtime_dir=$(mktemp -d /tmp/wheelhouse.XXXXXX)
./build/wheelhouse --user:"$runtime_dir/user" --project:"$runtime_dir/project" \
  --andamento_socket:"$runtime_dir/facts.sock" \
  --andamento_config:data/sidebar/daily-driver.kdl
```

From another terminal, publish real git facts without Flotilla:

```sh
andamento-git-watcher --transport wheelhouse --socket /path/to/facts.sock --roots /path/to/checkout
```

Or use a Flotilla build with the HTTP sink: `flotilla pm connect --wheelhouse-socket
/path/to/facts.sock`. The transport contract is in
[docs/protocol/pm-connect.md](docs/protocol/pm-connect.md). Clicking an entry runs its
supplied recipe in an ordinary Terminal View; Flotilla's recipes use `flotilla attach`
or `flotilla view`. Resource enumeration and dynamic overflow tabs remain follow-up
work. A new window receives facts on the producer's next periodic reassertion.

#### Dashboard subscriptions

A Dashboard lists the providers it subscribes to in `subscriptions.kdl` in its
directory: each has a subscription ID (a UUIDv7, made when it is added), a kind
(`flotilla`) and its daemon endpoint. Every fact a subscription publishes belongs to
it, so two subscriptions reporting the same project show two projects. With
`--andamento_socket`, each subscription gets an ingress endpoint of its own, named
after that one (`<socket>-<subscription ID>`, a socket beside it or a named pipe),
and Wheelhouse runs `flotilla pm connect --wheelhouse-socket <its endpoint>` for it
(`--flotilla_bin`, else `FLOTILLA_BIN`, else `flotilla`), restarting it after 1s,
doubling to 30s, when it exits. While a connector is down its rows stay, marked
stale, until it publishes again. The `--andamento_socket` endpoint itself, which
the git watcher and one-off scripts use, is the `local` provider.

The command palette's **Add Flotilla Subscription** takes a daemon endpoint
(`default` for Flotilla's own) and starts its connector; **Remove Subscription** takes
a subscription ID or daemon endpoint, stops its connector and retracts its facts.
Workspaces open on them stay, retained. `--flotilla_subscription[:<daemon>]` adds one
at launch unless the Dashboard has it.

To capture lifecycle bugs, add `--ingress_record` to Wheelhouse or run the daily
driver with `scripts/run-daily-driver.sh --ingress-record`. Recording is off by
default. It writes `logs/ingress.jsonl` beside `ui_thread.uishell_log` in the app
data folder (beside an explicit `--user` file, or in daily-driver settings).
Rotation retains four files of up to 4 MiB each, 16 MiB total. Override with
`--ingress_record_bytes:<bytes>` / `--ingress_record_files:<count>`; launcher
options are `--ingress-record-bytes <bytes>` / `--ingress-record-files <count>`.
Write failures, a full 64-entry writer queue, or a serialized entry larger than
one file permanently disable recording for that listener run, with one warning
while requests continue. Restart the listener to begin a fresh capture.
Reduced retention limits delete existing captures outside the new size/count
bounds on startup; copy any captures you want to keep before reducing limits.
Unix capture files use mode 0600, including retained archives.

`python3 tools/replay-ingress.py <logs/ingress.jsonl...> --library <Andamento shared library>`
replays patches through the shipped sidebar template and reports per-patch
role/convoy fields. Before sharing, run
`python3 tools/replay-ingress.py <logs/ingress.jsonl...> --redact > ingress-redacted.jsonl`
to hash paths, hosts and labels while retaining identities and ingress order.
See the [recording contract](docs/protocol/pm-connect.md#opt-in-ingress-recording-and-replay)
for replay limits and redaction details.

Producers discover terminal directories with `GET /v1/observed/workdirs` on that same
Unix socket. A successful response is `200 application/json` with `Cache-Control:
no-store`:

```json
{"workdirs":[{"workspace_id":42,"view_id":81,"entity_kind":"worktree","entity_id":"example","cwd":"/saved/root","live_cwd":null}]}
```

Each UI drain takes a fresh snapshot for queued reads (reusing it until an intervening
patch), including unselected views. `cwd` is the saved launch directory; `live_cwd` is
reserved for a provider-reported current directory. Both are nullable, and views with
neither are omitted. Prefer a nonempty `live_cwd`, falling back to `cwd`. The current
Cleat provider API does not report live directories, so Wheelhouse currently returns
null for `live_cwd`. Numeric host IDs remain stable while the workspace/view exists in
this process; do not cache them across restarts. Directory matching and focus stay local
to each window; discovery polls union all windows so producers can discover every
repository. The nullable `entity_kind` and `entity_id` carry the persisted producer
identity when the workspace has one. Empty inventory is `{"workdirs":[]}`; multiple
views may have the same directory. There is no cursor or subscription. Poll periodically
(the watcher defaults to five seconds); a full queue, missing observer, invalid UTF-8 in
a host record, or UI response timeout returns 503, which is not an empty inventory.
Invalid paths are never rewritten with replacement characters. The existing `POST
/v1/metadata/patch` behavior is unchanged.

The Rust watcher from [Andamento
#107](https://github.com/flotilla-org/andamento/issues/107) replaces
`tools/andamento-publish.py`. Migration: replace the old Python invocation with
`andamento-git-watcher --transport wheelhouse --socket PATH --roots DIR` (`--roots`
replaces `--repo`). Its repo/worktree facts populate the Git section using `git.repo`
relationships, `git.root`, branch and dirty state. The native `layout="fields"`
presentation measures template fields and removes optional, then low-priority fields as
width shrinks; required branch/dirty fields remain, with normal text elision at very
small widths. Producer medium/short labels come from the shared abbreviation tiers. Live
daily-driver validation with the paired watcher remains pending; automated diagnostics
are isolated.

The build now also produces `wheelhouse_ingress`, a Wheelhouse-owned Rust HTTP adapter
with a small C boundary. Andamento's core remains transport-independent. Ingress is
opt-in; Windows builds retain the fixture sidebar but reject the Unix listener option.
Clean application shutdown removes its socket. After a crash, remove the stale socket
before restarting with the same path.

### Moving a live terminal session

The hosting pill in each terminal view shows `in_process` or `daemon:<name@generation>`.
Click **Hand to daemon** or **Adopt**, or run **Hand Terminal to Daemon** / **Adopt Terminal**
from the command palette. The view, selection, session handle, and wake callback stay in place.
Handing off uses the view's `daemon_name` setting, or `default`; start that daemon first.
Failures appear as non-modal notices and leave the prior hosting usable. Windows reports
that transfer is unsupported.

A handed-off session survives Wheelhouse closing. An adopted session leaves its recording
with the daemon, which marks it hosted elsewhere until Wheelhouse releases it or exits;
then `cleat attach` can recreate it from that recording.

### Terminal selection

Shift-drag selects linear terminal text, even when the child application captures
mouse input. Shift-Option-drag on macOS selects a rectangle; on Linux and Windows,
use Shift-Alt-drag. The mode is fixed when the left button goes down and lasts
through release, even if you release the modifiers first. Copy with Cmd-C on macOS
or Ctrl-Shift-C elsewhere; releasing the drag also updates the middle-click
selection buffer.

Rectangles include both endpoint cells and preserve blank cells and trailing
spaces, with one slice per physical screen row, including wrapped lines. A wide
character is copied once when both its cells are inside the rectangle; a clipped
half is copied as a space. Ordinary linear selection still trims trailing spaces.

### Native terminal overrides

Shift-wheel scrolls the Terminal View's **outer displayed document** through
Cleat's viewport API, even when the child captures mouse input. Trackpad pixels
accumulate into rows using the current cell height. A Shift wheel reported on
AppKit's horizontal axis still navigates this outer document vertically. With no outer history or at
a bound, this is a consumed local no-op. In a nested Flotilla attachment, outer
repaint history is not the inner Cleat session's complete history. Plain wheel
keeps Cleat's ordinary application/history routing.

Shift-PageUp/Down also navigates that outer viewport and consumes the matching
key release. Keyboards without page keys can use **Scroll Terminal View Up** /
**Scroll Terminal View Down** (`terminal_scroll_page_up` /
`terminal_scroll_page_down`) in the command palette or configurable bindings;
a Super/Cmd binding with Up/Down remains available while Terminal View is focused.
Use non-text keys for these bindings; ordinary typing remains application input.
The existing page-selection semantic commands also request local navigation.
Plain PageUp/Down continues to reach the child.

Shift-middle-click pastes the selection buffer locally under mouse capture.
Plain middle-click pastes it when the application does not capture the mouse,
and otherwise goes to the application. Press ownership lasts through release,
even outside the canvas or after modifiers/capture change; focus loss cancels
it. Empty selection is a consumed local no-op. Paste remains structured input,
so Cleat applies bracketed-paste mode. Selection and standard clipboard remain
separate: macOS uses its named selection pasteboard; Linux and Windows currently
use Wheelhouse's process-local selection buffer. Linux does not read external
X11 PRIMARY selections, and Windows has no system PRIMARY capability; neither
silently substitutes the standard clipboard. External X11 PRIMARY integration is
tracked in [#150](https://github.com/flotilla-org/wheelhouse/issues/150). This is a forced local Shift
override, with no claim of XTSHIFTESCAPE negotiation.

The headless `--terminal_selection_diagnostics` runner includes capture/ownership
and signed/precise scroll traces. `--scroll_region_diagnostics` checks real UI
event claiming; on macOS it also exercises AppKit wheel events and a real Cleat
PTY's bracketed-paste bytes. Physical native acceptance is recorded separately
using [the acceptance checklist](docs/shift-override-acceptance.md).

`tools/run-sidebar-diagnostics.sh` is a Linux-only release-build runner requiring
`prlimit` and a C compiler; use the platform CI diagnostics on macOS and Windows.
