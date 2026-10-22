# Research: where binnacle stands

Status: research notes, 22 October 2026, for #434 (child of the map #421).
Nothing here is decided.

Sources: the binnacle checkout at `~/dev/binnacle` (branch `main`, head
`27a0350`, 21 October 2026), the Flotilla checkout at `~/dev/flotilla`
(head `13de6663`), and issues on flotilla-org/binnacle, flotilla-org/flotilla
and this repo, read through `gh api`. Binnacle paths below are relative to its
root.

## Summary

- **binnacle is past planning.** Its README still says "Status: planning" and
  calls `binnacle-ffi` "empty for now" (`README.md`); both are stale. The repo
  started on 10 October 2026 and has 323 commits. It has a working core,
  in-memory source, ratatui adapter, wasm bridge, web widget with a React
  wrapper, and a C API with a hand-written header. v1 (binnacle#27) is closed.
- **The Grid holds no values and draws nothing.** It turns data into geometry
  and view state: Strips of visible items (Position, Key, offset, size),
  header placements, cell state bits, selection rects and damage. The host
  draws every cell. Over C, Wheelhouse gets layout and Keys back and must
  render cells itself.
- **Data comes in as counts, Keys and position-based Change Notices.** Sort
  and filter are View Specs that the Grid requests and the host's Table Source
  applies. This matches Flotilla's ADR 0067 view subscriptions (decision 15),
  which were written after binnacle reviewed the ADR.
- **There are no column definitions in the core.** Columns are an axis of
  Keys. Labels, types and renderers belong to the host. In Wheelhouse #125's
  terms, the "column-definition output mode" is Flotilla's ADR 0067 table
  description (Frictionless Table Schema), and the "generic table view" is a
  Wheelhouse-side Table Source plus a cell renderer over binnacle.
- **A SolidJS binding is small.** The web widget is framework-neutral; the
  React wrapper is 83 lines plus 46 of option handling. A Solid wrapper is the
  same size.
- **Gaps for native Wheelhouse:** the C API has no in-memory Table Source, no
  ABI version constant, and drops some Effects (activate, edit, column resize
  and auto-size) as `BINNACLE_EFFECT_OTHER`. No server-backed Table Source
  exists yet in any target.

## Crates and packages

`Cargo.toml` makes every directory under `crates/` a workspace member:

| Crate | What it is | Size (lines of `.rs`/`.h`, largest files) |
|---|---|---|
| `binnacle-core` | the Grid and every type on its boundary; no toolkit deps | `grid.rs` 1021, `view.rs` 829, `snapshot.rs` 719 |
| `binnacle-memory` | In-Memory Table Source (`InMemoryTableSource`, `MemorySource<R>`), `KeyInterner`, `pump` | `memory_source.rs` 1318, `table.rs` 836 |
| `binnacle-ratatui` | reference ratatui adapter and demo binary | `filter.rs` 1979, demo 1805, `lib.rs` 733 |
| `binnacle-wasm` | wasm-bindgen bridge for the web widget (`publish = false`) | `testing.rs` 1291, `effect.rs` 816 |
| `binnacle-ffi` | "Internal C API over the binnacle Grid, for wheelhouse" (`publish = false`, `staticlib` + `cdylib`) | `src/lib.rs` 2571, `include/binnacle.h` 969 |
| `binnacle-testkit` | scenario harness and Snapshot text renderer (not published) | `scenario.rs` 550, `render.rs` 541 |

`web/` is the npm package `@flotilla-org/binnacle` (private, not on npm;
`web/package.json`), with `web/src` at about 10,800 lines of TypeScript.

The vocabulary is fixed in `CONTEXT.md`: Grid, Table Source, Effect, Action,
Strip, Snapshot, Change Notice, Key, Window, View Spec, Sort Spec, Filter Spec,
Condition, Find Query, Totals and so on. The rest of this note uses those
terms.

## The core: what goes in, what comes out

From `CONTEXT.md`:

- **Grid:** "geometry, interaction and view state for a table, holding no cell
  values and nothing proportional to the number of rows or columns."
- **Table Source:** "the host-side owner of a table's data and every operation
  on it (sort, filter, roll-up, pivot, reordering), which supplies the Grid
  with counts and keys."
- Rows and columns are symmetric **Axes**. Each has a count, a **Window** of
  Keys the Grid asks for (visible range plus overscan) and a size measure.
  Sorting, filtering, pinning, hiding and moves work on either axis
  (`CHANGELOG.md`, Unreleased).
- **Keys** are opaque `u64`s the source assigns. A `KeyInterner`
  (`crates/binnacle-memory/src/interner.rs`) maps the host's own identities to
  Keys.
- Scroll is a **Scroll Anchor** (Position plus offset into it), so very large
  tables stay precise. The Grid owns it, which fits ADR 0010 here (content owns
  scroll position; see binnacle's own `docs/research/02-wheelhouse-provider-shape.md`).

### Input model

- **Data feed:** the source gives a count per axis, then fills each Window the
  Grid requests with Keys (optionally with content sizes). Values never reach
  the Grid. The host looks them up by `(row Key, col Key)` when drawing.
- **Updates:** a **Change Notice** per axis: `Reset` (new count, promises
  nothing), `Reordered`, `Inserted { at, n }`, `Removed { at, n }`,
  `ContentChanged { at, n }`, `Refiltered` (same Keys, a subset or superset,
  maybe reordered, so focus, picks and Size Overrides follow their Keys)
  (`binnacle.h`, `BINNACLE_NOTICE_*`; `CONTEXT.md`). These are positional
  deltas. A source has to know where a changed row sits in the current order.
- **Sorting and filtering:** a header click or filter edit makes the Grid emit
  `ViewRequested { spec, edit }`. The spec is the whole requested view (each
  axis's Sort Spec, Filter Spec and hidden Keys). The edit is a hint naming
  the cause. The source answers with the View Spec it actually applied, plus
  Change Notices, and may apply only part ("answers the source leaves part of
  out stay requested, not applied"). Late, superseded and failed answers are
  handled (binnacle#129). Filters are a quick filter (a Text Query across
  columns) plus Conditions per column: `Text` or `OneOf`, each optionally
  `not`, ANDed. Find is separate and only marks cells.
- **Capabilities:** the source declares per item what it supports
  (`BINNACLE_SUPPORT_SORT`, `TEXT`, `TEXT_REGEX`, `ONE_OF`), whether an axis
  has a quick filter, hides, finds, and is movable. Adapters offer only those
  controls.
- **Totals:** each axis's unfiltered count, for "1,204 of 5,001".

### Output

A **Snapshot** per frame, stamped with a `generation` and `damage` bits
(`LAYOUT`, `SELECTION`, `POSITIONS`): per axis the drawn Strip (leading pinned,
scrolling, trailing pinned), header rects with flags (sorted, descending, sort
place, pending, filtered, hovered and so on), scrollbar thumb estimates, a
row-major cell-state byte per visible cell (focused, selected, current match),
selection rects, mode, drop indicator and pin zones (`binnacle.h`,
`BinnacleSnapshot`). Effects are queued and polled.

## The C ABI (what native Wheelhouse would link)

`crates/binnacle-ffi/include/binnacle.h`, 969 lines, hand-written. Its own
header comment:

> binnacle's internal C API, for wheelhouse. It mirrors the Rust Grid only as
> far as wheelhouse needs and makes no stability promise: the header and
> library change together. [...] Calls on one Grid are serialised by the
> host. Input arrays are borrowed for the call only [...] Tags and booleans
> are uint32_t, not C enums or bool. [...] Nothing a call fills needs
> releasing: output arrays point into buffers the Grid owns. Nothing calls back
> into the host: Effects are polled. A panic aborts the process.

The crate builds `staticlib` and `cdylib` (`crates/binnacle-ffi/Cargo.toml`)
and depends only on `binnacle-core`.

**It hands back layout, not pixels, and expects a renderer.** The frame loop
is:

1. `binnacle_grid_begin_frame(grid, width, height, &requests)` returns the
   Windows each axis needs.
2. The host fills them with `binnacle_grid_set_window` or
   `binnacle_grid_set_window_sized` (Keys, optionally content sizes).
3. `binnacle_grid_snapshot(grid, &snap)` lays out the frame.
4. The host draws: it crosses the rows Strip with the columns Strip, looks up
   each cell's value by Keys, and paints it with `cell_states`,
   `selection_rects`, header flags and `focus_rect`. Items with `has_key == 0`
   draw as loading.
5. It drains `binnacle_grid_poll_effect` and answers: `VIEW_REQUESTED` with
   `binnacle_grid_view_applied` then `binnacle_grid_notify`, `LOCATE` with
   `binnacle_grid_located`, `MOVE_REQUESTED` with
   `binnacle_grid_move_answered`, find steps with `binnacle_grid_find_stepped`.

Input goes in as raw pointer events (`binnacle_grid_pointer`, viewport
coordinates in the host's unit, with click count and modifiers) and semantic
Actions (`binnacle_grid_move_focus`, `select_all`, `type_text`, `cancel`,
`scroll`, `scroll_to`, `scroll_by`, `find_next`, ...). The host's keymap maps
keys to Actions. Units are the host's (pixels for Wheelhouse, cells for
ratatui).

This is the pull-based, poll-the-Effects, single-thread shape that binnacle's
`docs/research/02-wheelhouse-provider-shape.md` derived from Cleat and
Andamento. Two differences from those ABIs:

- **No ABI version.** The header has no version constant or runtime check
  (grep for `ABI`/`VERSION` finds none). Andamento and Cleat both have one.
  Wheelhouse would pin a binnacle revision, as it pins `ANDAMENTO_REV`.
- **Snapshot buffers are owned by the Grid** and "stay valid until the next
  binnacle_grid_snapshot or binnacle_grid_free" (`binnacle.h`), not acquired
  and released. Wheelhouse would copy what it
  needs into its own frame cache, as it does with Cleat updates.

**Effects missing from C.** `binnacle-core/src/effect.rs` has `Activated`,
`EditRequested`, `EditCancelled`, `SizeChanged` and `AutoSizeRequested`, but
`binnacle-ffi/src/lib.rs:1943` maps everything outside its 11 kinds to
`BINNACLE_EFFECT_OTHER`. The C API also has no `Activate` or `BeginEdit`
Action call. For a read-only vessel overview, the one that matters is
`Activated`: double-clicking a cell emits it in the core
(`gesture.rs:250`), and the C host never sees it. Wheelhouse can work around
this with its own Enter or double-click handling on the focused cell, but it
is a small header addition. Size Overrides from a user's column resize can be
read with `binnacle_grid_size_overrides`.

**No source over C.** `binnacle-memory` (sorting, filtering, Remap, distinct
values, `KeyInterner`) is not exposed through `binnacle-ffi`. A C host must
implement its own Table Source: sort, filter and Key interning in C, or push
them to a server. The alternative is a small Rust crate on the Wheelhouse
side that links `binnacle-memory` and exposes a narrower API. Wheelhouse
already ships Rust (`src/ingress/`), and the Andamento merge brings a Cargo
workspace.

**Tests:** `crates/binnacle-ffi/tests/mirror.rs` (2056 lines, Hegel
properties) checks that a handle and a Rust Grid given the same inputs request
the same Windows, give the same Snapshot and drain the same Effects.
`tests/smoke.c` drives one Grid from C "the way wheelhouse does". binnacle#28
(open) proposes one action call in place of the eight wrappers.

## The ratatui adapter

`crates/binnacle-ratatui` is the reference adapter. `render` draws a Snapshot
into a ratatui `Buffer` in a `Theme`. The host supplies text through the
`Content` trait (`lib.rs:74`): `cell(row, col)`, `column_header(col)`,
`row_header`, `marked` (find), `unmatched`, and `renderer(row, col)` choosing a
Cell Renderer per cell (text, sparkline, bar). Around it: filter bar, column
menu with a checklist of distinct values, find bar, chooser for hiding many
items, pinning, scrollbars, border styles, shimmer for loading, saved layouts
(`save_layout`/`restore_layout`) and keymap presets. The demo runs with
`cargo run -p binnacle-ratatui --bin binnacle-demo`. Open: binnacle#97 (initial
sizes per column and row), binnacle#31 (keymap nits).

## The web widget and the React wrapper

`web/README.md`. The Grid runs as wasm on the main thread and paints onto a
canvas inside a native scroller (up to 2^53 - 1 rows). The data stays in
JavaScript behind the TypeScript Table Source (`web/src/source.ts`), whose
methods may return values or Promises and take an `AbortSignal`:

- per axis: `count()`, `window(start, end, signal)`, and optionally `support`,
  `distinct`, `locate`, `unmatched`, `move`, `movable`, `label`;
- per table: `cells(rows, cols, signal)`, `view(spec, signal)` returning the
  applied view, optional `find`, `emptyMessage`, and `attach(sink)`, through
  which the source pushes Change Notices, `viewChanged` and support changes.

`fromRows(rows, columns)` (`web/src/rows.ts`) is the ready-made in-memory
source: binnacle-memory compiled into the same wasm module, with the row
objects kept in JS. Around the canvas the package offers `createFilterBar`,
`createColumnMenu`, `createFindBar`, `createChooser`, themes, border styles,
cell renderers (`textRenderer`, `sparklineRenderer`, `barRenderer`), editing
through `onCommit`, saved layouts and an ARIA layer (`web/src/aria.ts`).

The neutral entry is `GridWidget` (`web/src/widget.ts`, 1537 lines).
`@flotilla-org/binnacle/react` (`web/src/react/index.ts`, 83 lines, plus
`options.ts`, 46) is a thin wrapper. React renders the container once. The
widget runs its own frame loop. Function options are always called as in the
latest render. `theme` and `border` restyle the same widget. Any other option
change recreates it. React is an optional peer dependency.

Building needs Rust with `wasm32-unknown-unknown`, `wasm-pack` and pnpm. The
package is consumed from a checkout (`workspace:*` or `link:`), not from npm.

### What a SolidJS binding would take

Port `web/src/react/index.ts`: about the same 80 lines.

- Mount a `<div ref>`. In `onMount`, call `GridWidget.create(el, options)`,
  which is async because it loads wasm. `onCleanup` calls `destroy()`.
- `createEffect` on `theme` and `border` to restyle the live widget.
- Route function options through a getter on the latest props, as
  `throughLatest` does in `web/src/react/options.ts`.
- Recreate the widget when a non-live option changes, mainly `source`.

Solid fits better than React here. Components run once and nothing
re-renders, which is how the widget already behaves. Reuse
`recreates`/`live` from `options.ts`. The DOM pieces (filter bar, column menu,
find bar) are imperative and attach to a `GridWidget`, so they need no
framework code.

## How this relates to #125

#125 proposed a Flotilla "non-visual/JSON mode emitting column definitions
(key, title, type, alignment, width hint, sort) plus rows and live updates",
and a generic Wheelhouse table view. Since then:

- **Flotilla's ADR 0067** (`docs/adr/0067-tables-describe-themselves-on-the-wire.md`,
  accepted 17 October 2026) is that output mode, done server-side and
  generally rather than as a TUI mode. Tables describe themselves as
  Frictionless Table Schema v2: fields, `primaryKey`, `foreignKeys`,
  `categories`, roles, units (UCUM), default `title`, and per-field
  sortable and `where`-operator capabilities. Then come `rows`, `synced`,
  `upsert`, `delete`, `progress`, `resync` and `count` messages.
- **Meaning crosses the wire, layout does not** (decision 1). That drops two
  of #125's fields: alignment and width hint stay in the viewer, though units
  may drive alignment. Sort is a request parameter and a declared capability,
  not part of the column definition.
- **binnacle has no column-definition type.** The Grid sees only Keys. A
  generic Wheelhouse table view would be a Table Source that turns an ADR 0067
  description into columns: a Key per field through `KeyInterner`, labels from
  `title`, support bits from the sortable and where-operator properties. Rows
  get Keys by interning the primary key (decision 9; composite keys as
  canonical CBOR, decision 16.4). The cell renderer is chosen by field type
  and units.
- #125's "shared scroll, selection and filter behaviour" is what the Grid
  provides.

So #125 can be restated as: Wheelhouse's native Table Source over Flotilla's
ADR 0067 tables and view subscriptions, plus a native cell painter. This
belongs with #435 (table formats and how a View declares what it consumes).

## What Flotilla's native support publishes

The only Flotilla file that mentions binnacle is ADR 0067. Its status is
"Accepted, not yet implemented", but parts have since landed:

| Piece | State |
|---|---|
| HTTP router and loopback listener (flotilla#3248) | closed 20 October 2026 |
| Descriptor carries meaning: enum names, reference targets, roles, describe-only derive (flotilla#3590, PR #3592) | merged 20 October 2026 |
| Self-describing tables over HTTP GET: `GET /tables`, `/tables/{table}` (Frictionless, Markdown, machine diagram), `/tables/{table}/rows` with cursor, sort, columns, where, `watch` (flotilla#3589, PR #3591) | merged 20 October 2026 |
| WebSocket surface protocol (flotilla#3249) | open |
| View subscriptions and snapshot sequences (decisions 15 and 16, flotilla#3447) | open, "designed against binnacle#47" |
| Describe layer follow-ups (flotilla#3598, #3600) | open |

Decision 15 is the part shaped for binnacle. A client subscribes to a view
(table, `where` clauses, sort, window of positions) over the WebSocket. The
server keeps an ordered index with subtree counts and sends the count, the
window's rows, changes as positions (inserted at, removed at, moved, content
changed), count changes, and answers to "what is the position of key K?".
Those map one-to-one onto binnacle's `count`, `window`, Change Notices and
`Locate`. Sort and `where` map onto View Specs. Text and OneOf Conditions,
the quick filter, OR and IN are not yet in the `where` grammar: "OR, IN and
text search are additions to the grammar" (decision 12). A server-backed
source would answer those parts as not applied until the grammar grows.

Small tables can be fetched whole with GET and sorted by binnacle's in-memory
source (decision 15, last bullet). That needs `binnacle-memory` on the native
side, which the C API does not provide (above).

binnacle#47 closed with the TypeScript Table Source interface and `fromRows`.
No server-backed source over ADR 0067 exists in binnacle yet, in TypeScript or
Rust.

## Maturity

- **Activity:** 323 commits from 10 to 21 October 2026. `CHANGELOG.md` has a
  long Unreleased section that is mostly breaking changes, including C API
  struct changes in the last week (picks per axis, `selection_runs`, pinned
  Strips, drop targets). The header says it "makes no stability promise".
  Expect churn.
- **Tests:** 623 Rust `#[test]`/`#[hegel::test]` functions across the crates,
  with property tests on the core (`tests/properties.rs` 2721 lines,
  `tests/machine.rs` a state machine), the source and the C mirror. Insta
  snapshots in ratatui. About 400 web unit and Playwright end-to-end tests
  (`web/test`, `web/e2e`, with screenshot baselines and a WebKit spec). I did
  not run them: building writes into the binnacle checkout.
- **Docs:** `CONTEXT.md` (glossary), `web/README.md` (thorough), `CHANGELOG.md`,
  Rust doc comments, the C header's comments, and two research notes
  (`docs/research/01-prior-art-headless-tables.md`,
  `02-wheelhouse-provider-shape.md`). The top-level `README.md` is out of date.
- **Issues:** 18 open, 120 closed. Open ones are mostly edge-case bugs and test
  hygiene: binnacle#174, #163, #159 (core range and huge-size precision),
  #139, #160, #101 (web find count, labels, VoiceOver), #167, #77, #98 (flaky
  or loose e2e), #28 (C action call), #97, #31 (ratatui). The one large open
  feature is hierarchical axes: grouping, subtotals and pivots (binnacle#92,
  spec binnacle#189 `ready-for-agent`).

## Open points for the map

1. Where native sorting and filtering live for small tables: a Wheelhouse Rust
   crate over `binnacle-memory`, an in-memory source added to `binnacle-ffi`,
   or always the server.
2. Add `Activated` (and an `Activate` Action) to the C API before the vessel
   overview needs it. Consider an ABI version constant to match Andamento and
   Cleat.
3. Who writes the ADR 0067 server-backed Table Source, and in which language.
   One Rust implementation could serve native over C and the web over wasm,
   if the WebSocket client lives on the Rust side. Otherwise there is one per
   frontend.
4. The vessel overview's data. Convoy and vessel status would need to be an
   ADR 0067 table (a named query described through the derive, decision 7) for
   the generic path to apply.
