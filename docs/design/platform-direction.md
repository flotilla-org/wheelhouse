# Platform direction: one repo, three frontends

Status: direction, recorded 2026-10-21 from the operator's notes. Nothing here
is scheduled. After the current stability fixes it becomes a wayfinder map:
each open question below becomes a decision ticket under the map, and this
document then points to the map instead of holding the decisions itself.

## Destination

Wheelhouse becomes a family of frontends over a shared core:

- **native**, the macOS/Windows/Linux app that exists today;
- **web**, the next frontend and the higher priority;
- **TUI**, later, built on the parts the native app and Andamento already have.

They share what makes sense: the sidebar model and its templates, workspace
and View state, and the formats views consume. Each frontend keeps its own
Renderers and Handlers (CONTEXT.md), as [web-views.md](web-views.md) already
assumes: a TUI may show a URL as a link where the web shows the page.

There is no rush. The aim is to get the pieces into good shape so each step
is small when it comes.

## Where things are now

| Piece | What it is | Where |
|---|---|---|
| Wheelhouse | native frontend, C (RAD-derived UI, Metal/D3D11/OpenGL) | this repo |
| Andamento | placement core, sidebar templates, FFI; also `andamento-tui`, `andamento-html`, `andamento-terminal`, `andamento-rail` | flotilla-org/andamento, pinned by `ANDAMENTO_REV` |
| Cleat | terminal sessions and their daemon | flotilla-org/cleat |
| Jackstay | frame and input transport between producers and consumers | flotilla-org/jackstay |
| Luchs | web page producer for Jackstay (WebKit; CEF on a branch) | flotilla-org/luchs |
| binnacle | toolkit-agnostic table/grid core: ratatui, wasm, C ABI | ~/dev/binnacle (planning) |
| katzensteg-wm | Katzensteg's window manager, with a Cleat client built in | katzensteg repo |

## Decided

1. **Andamento joins the Wheelhouse repo.** The two change together in most
   work. Today that costs a pin bump and PR ordering across two repos for
   every sidebar change (#409 and andamento#178 are a recent example).
2. **The web comes before the TUI.**
3. **The TUI builds on what exists:** Andamento's core and its existing
   terminal sidebar, plus code, ideas or structure from katzensteg-wm, which
   already has a Cleat client. That makes the TUI cheaper than the web, but
   it is still second.
4. **Views are reusable components over common formats** most of the time:
   tables, graphs, markdown, and similar. Flotilla's domain concepts live in
   the data and in a thin mapping, not inside the components.
5. **Tables come from binnacle**, which Flotilla will support natively, so
   table views are not limited to the TUI.

## Open questions

Each heading is a likely decision ticket.

### Repo merge

- **History:** keep Andamento's history (subtree or filter-repo merge), or
  import a snapshot and link the old repo.
- **Layout:** a Cargo workspace beside `src/`, or `crates/` at the root, and
  how `build.sh`/`build.bat` find it instead of `WHEELHOUSE_ANDAMENTO_DIR`.
- **What retires:** `ANDAMENTO_REV`, the cross-repo pin steps in CI, and the
  merge-order rule between paired PRs.
- **Other consumers:** what else builds from Andamento's repo (the daily
  driver's `andamento-git-watcher`, at least), and what it pins to afterwards.
- **When:** after the stability fixes, before web work starts, and while few
  Andamento PRs are open, because open PRs are what make the move costly.

### Core boundary and naming

- What moves from Wheelhouse's C into the shared core. Rule of thumb:
  anything a second frontend would otherwise reimplement. Candidates are
  workspace and View state, arrangement and layout rules, and settings.
- Whether the core is then renamed (for example `wheelhouse-core`) and the
  Andamento name retired. Settle the boundary first; the name follows.
- How the core is exposed to each frontend: the C ABI for native, Rust
  directly for the TUI, wasm for the web.

### Web

- **Prerequisite: remote Cleat that actually works.** A browser can't reach a
  local socket, so the web depends on remote terminal sessions (#104) and on a
  server to reach them through.
- **Server infrastructure:** what runs where, how a browser authenticates, and
  how it reaches Cleat, Jackstay and the Flotilla daemon. We have none of this
  yet; the first step is to define it.
- **Terminal rendering:** our own renderer over Cleat's screen state (canvas
  2D, WebGL or WebGPU), rather than Ghostty's wasm build. Open: which API, and
  how much renderer to share with native.
- **UI framework:** SolidJS is the candidate, as an alternative to React.
  Most components will be our own anyway, so fine-grained reactivity and a
  small runtime matter more than an ecosystem.
- **Web pages:** much easier than native, since the browser embeds pages
  itself. Decide when an embedded page is enough and when Luchs still has a
  role (pages that need a separate profile, or headless capture).
- **Jackstay on the web** (not urgent): decode and draw frames ourselves, or
  stream into a video element (WebRTC, or WebCodecs with media source).
- **Starting points:** the sidebar prototypes in this directory
  (`sidebar-cards-prototype.html`, `card-features-prototype.html`,
  `sidebar-header-controls-prototype.html`, `drop-targets-prototype.html`,
  `sections-groups-examples.html`) and `andamento-html`.

### TUI

- How much of katzensteg-wm to take: code, structure, or only ideas.
- How `andamento-tui` and `andamento-terminal` relate to it, and whether the
  TUI is a new binary or grows from one of them.

### Views and formats

- **The component set:** table (binnacle), graph, markdown, log, plus the
  terminal, Jackstay and web views that exist.
- **Formats:** where an existing format fits, use it (CommonMark for
  markdown, for example); where none does, define ours once and use it in
  every frontend. Open: what tables and graphs take.
- **How a View declares what it consumes**, so content finds its component
  through a Handler the way [web-views.md](web-views.md) describes for pages.
- **Where Flotilla-specific views are still needed**, and keeping them thin.
- Related: #125 (a generic table view fed by a column-definition output mode).

## Order

1. Stability fixes in progress, including the vessel opening action that
   comes and goes (Flotilla withdraws it while re-checking attach capability).
2. Turn this document into a wayfinder map with its decision tickets.
3. Repo merge.
4. Core boundary decisions, then the web's prerequisites: remote Cleat and
   the server infrastructure.
5. Web frontend, then the TUI.

Project and convoy workspaces (#401, #391) continue meanwhile in the native
app; their custom views are the first users of the shared view components.
