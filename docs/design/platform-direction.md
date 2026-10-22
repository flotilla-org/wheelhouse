# Platform direction: one repo, three frontends

Status: direction, recorded 2026-10-21 from the operator's notes. The open
questions it listed are now decision tickets under
[Map: the way to building the web frontend is clear](https://github.com/flotilla-org/wheelhouse/issues/421),
charted on 2026-10-22. That map holds the decisions from here on; this
document keeps the direction and the decisions made before it.

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

## Open questions and order

These are on [the map](https://github.com/flotilla-org/wheelhouse/issues/421):
the repo merge, the core's boundary and name, the browser's saved state, the
relay (local first, then remote and cloud), the screen-state format and the
web terminal renderer, the web UI framework, binnacle and the first shared
View formats, and how each frontend resolves a View. The TUI is outside that
map and gets its own later.

Project and convoy workspaces (#401, #391) continue meanwhile in the native
app; their custom views are the first users of the shared view components.
