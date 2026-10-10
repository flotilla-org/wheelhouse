# Andamento saving its own state: options

Research for [#292](https://github.com/flotilla-org/wheelhouse/issues/292), under map [#290](https://github.com/flotilla-org/wheelhouse/issues/290). Written 2026-10-10.

**Sources.** All Andamento references are to `flotilla-org/andamento` at `f8aaf40` ("Add sections, groups and references people make (#140)"), which is `main` as of this date and also CI's pinned rev. Wheelhouse references are to `origin/main` at `ba36986`. Zellij references are to the local checkout `~/dev/zellij` at `3e585cf` (branch `feat/kitty-image-plumbing`). Paths below are relative to each repo's root.

Vocabulary follows `CONTEXT.md`, including the proposed terms **Dashboard**, **Presentation State** and **Overlay Sync** from the plugin-views research branch.

## Summary

- **Today Andamento saves nothing.** The core does no file IO (`docs/sidebar-design/core-interface.md`, "Config parsing does not perform filesystem access"). Every frontend either loses its state on exit (Zellij, TUI, HTML) or replays it into a fresh core on launch (Wheelhouse).
- **Wheelhouse replays four kinds of state into Andamento through the RAD config tree**: sibling orders, persistent display variables, local sections, groups and homes (as facts), and workspace subjects. It does not save placement collapse, and it cannot save retained or ended subject records, because the ABI does not expose them.
- **Recommendation: a pull-style state export/import in the core ABI (option A), plus an optional thin native store crate on top (option B).** The core then owns the format and its versions, while each host decides where the bytes go. Most of the serializer already exists as the Zellij controller's `ControllerBootstrapSnapshot`.
- **One core per process is feasible without threads.** Every Wheelhouse window is already driven from one UI thread, which already fans each fact out to every window's core. What blocks it is that the per-client parts of `Sidebar` (topology, selection, pending effects, collapse, orders) are singular. The fix is to split a shared core from per-window **view** handles. The Zellij adapter's `ControllerState.clients` map is a precedent.

## 1. What state would need saving

The core's state is `Sidebar` (`crates/andamento-core/src/sidebar.rs:134`), which wraps `ControllerState` (`crates/andamento-core/src/state.rs:455-478`). Its rail UI part is `ControllerRailUiState` (`state.rs:423-429`).

| State | Where it lives in the core | Logical or presentation? | Saved today by Wheelhouse? |
|---|---|---|---|
| Placement collapse | `rail_ui.collapsed_placements: BTreeSet<PlacementKey>` | Open question (see note 1) | **No.** No config key exists. Collapse is lost on restart. |
| Sibling (drag) order | `rail_ui.sibling_orders: BTreeMap<PlacementLoopKey, Vec<EntityRef>>` | Logical (Dashboard "order") | Yes: window `sidebar_order`, replayed through `andamento_set_sibling_order` (`src/uishell/uishell_sidebar.c:599-650`, `:1385`) |
| Display variables (`persist=true`) | `rail_ui.variables` | Logical (Dashboard "display") | Yes: window `sidebar_display` (`uishell_sidebar.c:536-557`). Restoring it needs a retry loop that dispatches toggle actions until the snapshot agrees, with 3 attempts spaced 1 s apart (`uishell_sidebar.c:690-770`). The C ABI has no direct setter. |
| Scroll offset | `rail_ui.scroll_offset` | Presentation | Not applicable: Wheelhouse scrolls natively |
| Retained subjects (last known record of an open workspace's subject and ancestors) | `retained_subjects: BTreeMap<EntityRef, CatalogEntity>` | Logical (per workspace) | **No.** Wheelhouse saves only the subject reference (`sidebar_entity_kind`/`_id` on the workspace). `retain_workspace_paths` (`state.rs:2417-2453`) retains a subject only once its facts are present, so a subject that ended while Wheelhouse was closed has no record after restart. |
| Ended subjects | `ended_subjects: BTreeSet<EntityRef>` | Logical | **No.** It is derived again only if facts arrive saying the subject is terminal (`state.rs:2459-2490`). |
| Local sections, groups, refs (ghosts) and workspace homes | Not core state. The host publishes them as `.section`, `.group`, `.ref` and `.workspace` facts (`core-interface.md`, "Sections, groups and references") | Logical (Dashboard) | Yes: window `sidebar_local`, workspace `local_entity` GUID and `lives_with` (`uishell_sidebar.c:836-855`, `:994-1013`). They are republished every launch (`uishell_sidebar.c:1240-1290`). |
| Managed content applied targets | `Sidebar.managed` bindings keyed by workspace id (`crates/andamento-core/src/managed.rs`) | Host resource state | Yes, by design: the host stores `managed_target` on the primary terminal tab (`src/uishell/uishell_managed_content.c:53,90`) and passes it as the "actual persisted descriptor" to `plan` (`docs/sidebar-design/managed-primary-content.md`). The core rebuilds its bindings from that, so **the core does not need to save it**. |
| Placement variable overrides | `node_variable_overrides` | Logical | No. This is not projected into C. |
| Producer facts (metadata store) | `metadata: MetadataStore` | Neither: producers own it | No, and it should not be needed. Producers republish. An optional last-known cache could make cold start and offline use faster. |
| Pending effects, latent materializations, errors | `pending`, `pending_latent_materializations`, `errors` | Transient | No, and it should not be saved |
| Legacy Zellij: pins, sort mode, rail config, pane statuses | `pinned_tabs`, `sort_mode`, `rail_config`, `pane_statuses` | Zellij only | Not applicable |

Section collapse, section positions, pinned-card form and View geometry are Wheelhouse **Presentation State**. They already live on panel and window config nodes (`uishell_sidebar.c:1863-1877`, `docs/section-placement.md`) and stay out of scope here.

**Note 1, collapse.** In Zellij, collapse is session-wide: every rail shares one collapse set, synchronized deliberately through a broadcast (README, "Controller/Rail Prototype"). In Wheelhouse, each window's core has its own. Whether collapse is Dashboard state or per-window presentation is a decision for the #290 boundary ticket. It matters for the per-process design in section 4.

**Note 2, key stability.** Saved collapse and order keys are only useful if they mean the same thing after a restart. `PlacementKey` and `PlacementLoopKey` are built from loop names and `EntityRef`s (`crates/andamento-core/src/lib.rs:464-500`), so they are stable wherever entity IDs are stable. Fallback `.unplaced` and synthetic `.workspace` keys use host workspace IDs (`core-interface.md`, "Workspace coverage"). In Wheelhouse these are `CFG_ID`s, which are never saved (map #290 Notes). Anything keyed under them does not survive a restart. Host entities (`.workspace` with the GUID from `local_entity`) avoid this. Persistence therefore depends on the identity ticket.

## 2. How each frontend handles persistence today

**Zellij (controller, rail and config plugins).**
- The controller holds all state in memory. When a controller instance starts, it asks a peer for a `ControllerBootstrapSnapshot` through `andamento-controller-bootstrap-request`/`-state` pipe messages (`crates/andamento-controller/src/main.rs:522-553`). This is a hand-off between live instances. Nothing is written to disk.
- The snapshot type is serde-serializable (`crates/andamento-core/src/lib.rs:250-261`): sort mode, rail config, pins, pane statuses, node variable overrides, all live metadata patches, and `RailUiState`. `RailUiState` (`lib.rs:179-191`) carries collapse, scroll, variables and sibling orders. `bootstrap_snapshot` filters the variables to those declared `persist=true` (`state.rs:1002-1047`). This is the only place the core already serializes its own state.
- Zellij offers plugins two writable folders (`zellij-server/src/plugins/plugin_loader.rs:441-455`, `wasm_bridge.rs:127-136`):
  - `/data` is per plugin id and client, under the session cache. It is deleted when the plugin unloads (`wasm_bridge.rs:576-590`), so it does not survive a restart.
  - `/cache` is `ZELLIJ_CACHE_DIR/<plugin-url>/plugin_cache`. It is shared by every instance of that plugin URL and survives across sessions.

  Andamento uses neither. The controller does request `FullHdAccess` and remaps `/host` to `/` to read its template (`main.rs:206-238`), so it could also write anywhere.
- Zellij session resurrection restores the layout and the plugin configuration it contains. Plugin memory is not restored. After a resurrect, Andamento starts empty and waits for producers.

**TUI (`crates/andamento-tui`).** A tmux host. It reads the config KDL, spawns a facts command and runs the event loop (`src/main.rs:145-170`). It saves nothing.

**HTML (`crates/andamento-html`).** A CLI that renders a snapshot to static HTML from a config, a JSONL patch file and an optional JSONL file of core requests to replay (`src/main.rs`, `core-interface.md` "Proof and validation"). It has no live state to save. The replay input does show that a request log is already a supported way to rebuild state (`crates/andamento-core/src/replay.rs`, `src/bin/andamento-replay.rs`).

**Wheelhouse.** All state goes into the RAD project file through the config tree (see the table in section 1). Each `andamento_create` is followed by restoring orders, refreshing and restoring display variables (`uishell_sidebar.c:1340-1393`). Local entities are then republished whenever the window layout changes.

## 3. Options

All three options keep the ABI's current rules: a single owner per handle, borrowed inputs, owned outputs freed by the caller, and no callbacks. Effects are queued and drained (`andamento_effects_take`), not delivered through callbacks.

### A. Host storage hook (core stays off the filesystem)

The core owns *what* is saved and its format and version. The host owns *where* the bytes go and *when* they are written. The hook is pull-style rather than a callback, so it matches the rest of the ABI and works the same in WASM.

```c
/* Opaque, versioned bytes owned by Andamento (CBOR or JSON inside). */
typedef struct AndamentoBytes { uint8_t *data; size_t len; } AndamentoBytes;

/* Increments whenever savable state changes (collapse, order, display,
 * retained/ended records). Cheap; poll it once per frame or after a batch. */
uint64_t andamento_state_generation(const Andamento *h);

/* Serialize savable state. The caller frees it with andamento_bytes_free. */
uint32_t andamento_state_export(Andamento *h, AndamentoBytes *out, char **error);

/* Merge saved state into a freshly created core before the first observe.
 * Unknown versions or kinds are rejected without mutating anything.
 * Keys for entities not yet seen are kept and applied when they appear,
 * as andamento_set_sibling_order already does. */
uint32_t andamento_state_import(Andamento *h, AndamentoText bytes, char **error);
void     andamento_bytes_free(AndamentoBytes b);
```

- The payload would be `RailUiState` minus scroll, plus `node_variable_overrides` and the retained and ended subject records. That is essentially `ControllerBootstrapSnapshot` without metadata patches and the Zellij-only fields. Retained records should be trimmed to what a path and row need (identity, `display.label`, parents, terminal phase), not full `CatalogEntity` source maps.
- **Wheelhouse:** writes the bytes to the Dashboard file, or as a single config string during migration. It replaces the `sidebar_order` and `sidebar_display` replay code, including the display retry loop.
- **Zellij:** the controller writes to `/cache/<session-or-dashboard>.state` when the generation changes, debounced on its existing timer. It imports on `load`, and peer hand-off keeps working.
- **TUI:** writes to an XDG state file.
- **HTML:** optionally accepts the bytes as another input.
- **Pros:** the core stays free of IO and deterministic, so tests remain pure. It works under WASM and the Zellij sandbox. Format versions and migrations live in one place. Undo, or "implicit version control" (#290 Notes), is a host concern over opaque snapshots.
- **Cons:** every frontend writes about 20 lines of save and load code. Sharing between frontends needs an agreed location, which option B supplies. Whole-state export does not merge concurrent writers; a log does (option C1).

### B. Shared store crate (`andamento-store`)

A native Rust crate that frontends link. It resolves a Dashboard path (XDG state dir, keyed by Dashboard id), writes atomically (temp file and rename), takes an advisory lock, and optionally watches for external changes.

```rust
pub struct Store { path: PathBuf, lock: FileLock }
impl Store {
    pub fn open(dashboard: &DashboardId) -> io::Result<Store>;
    pub fn load(&self) -> io::Result<Option<Vec<u8>>>;   // bytes for state_import
    pub fn save(&self, bytes: &[u8]) -> io::Result<()>; // atomic replace
    pub fn changed_since(&self, stamp: Stamp) -> bool;   // another writer
}
```

Through FFI, `andamento_store_open(dashboard_id, &error)` and friends live in `andamento-ffi` behind a Cargo feature (like `json`). The isolation check (`scripts/check-independent-core.py`) keeps `andamento-core` free of IO.

- **Pros:** TUI, Wheelhouse and native Zellij share one location and format without effort. It is a natural home for Dashboard-level files, such as subscriptions and local sections.
- **Cons:** it brings filesystem policy and locking into Andamento's native library. It cannot run in the Zellij WASM sandbox except through `/cache` or a `FullHdAccess` path, so the plugin would still use A directly. A browser frontend cannot use it. Several processes on one Dashboard (two Wheelhouse instances, a Zellij session and a TUI) need either last-writer-wins or a merge, which B alone does not provide.

**B is a convenience layer over A, not an alternative to it.** Without A, B would have to reach into core internals.

### C. Other options

1. **Change journal instead of whole-state export.** The core queues typed state-change records, such as `SetOrder`, `Toggle` and `SetVariable`, like effects: `andamento_state_changes_take(h)` returns an owned batch. The host appends them to a log and replays them with `andamento_state_apply_change`. This fits Andamento's existing request replay (`replay.rs`) and the #290 notes on undo and version control. It also merges better across devices and frontends. The costs are log compaction and a stable schema for each change kind. It can be added later on top of A (snapshot plus log tail), so A does not rule it out.
2. **Logical state as facts from a local provider.** Sections, groups and refs already work this way: the host publishes them and the core stays stateless. Extending this so that a small local "dashboard provider" also publishes order, display and collapse as facts would keep the core free of persistence. That provider could be flotilla or a sidecar. The cost is that facts have TTL and source arbitration semantics that do not suit UI preferences, and it adds a process round trip to every collapse click. It suits Dashboard *structure* (sections, groups, subscriptions), which may move to flotilla anyway. It does not suit fine-grained UI state.
3. **Status quo: host replays through existing setters.** No new ABI. Each host keeps its own replay code. Retained and ended records stay unsaved, collapse needs new setters, and the display retry loop remains. This is the baseline to beat.

### Comparison

| | A: export/import | B: store crate | C1: journal | C2: facts provider | Status quo |
|---|---|---|---|---|---|
| Core stays IO-free | Yes | Yes (crate is separate) | Yes | Yes | Yes |
| Works in Zellij WASM sandbox | Yes (`/cache`) | Only through A | Yes | Yes | Not applicable |
| Saves retained and ended records | Yes | Yes (through A) | Yes | Awkward | No |
| One format for every frontend | Yes | Yes | Yes | Yes | No |
| Shared location for every frontend | No | Yes (native only) | No | Yes | No |
| Concurrent writers | Last writer wins | Lock, last writer wins | Mergeable | Provider arbitrates | Not applicable |
| Room for undo and version control | Snapshots | Snapshots | Native | Weak | No |
| ABI size | 4 functions | Optional feature | About 3 functions plus a schema per change kind | None | Grows a setter per kind |

**Recommendation.** Do A now and keep the payload's kinds explicit, so C1 can be added later. Add B when a second native frontend (TUI or native Zellij) needs to open the same Dashboard. Use C2 only for Dashboard structure, if flotilla takes ownership of it.

## 4. Moving Wheelhouse from one core per window to one per process

**What is per window today.** `uishell_sidebar_init` creates one `Andamento` per `RD_WindowState` (`uishell_sidebar.c:1340-1357`). The comment at `uishell_sidebar.c:1317` says so: "Each core controls one window. HTTP discovery unions all windows, while focus and derived open state stay local to the window owning this split." Every live patch and every 250 ms tick is applied to **every** window's core in a loop (`uishell_sidebar.c:5686-5740`). With N windows, the work of storing and evaluating metadata is therefore done N times.

**Threading is not the obstacle.** The ABI requires only that calls on one handle are serialized (`core-interface.md`, "Update and ownership rules"). Wheelhouse already makes every Andamento call from its single UI thread, walking `rd_state->first_window_state`, including fact ingress. A single core called from that same thread meets the contract with no locking. The `Sidebar` is `!Sync`: it holds `OnceCell` and `RefCell` (`sidebar.rs:134-146`). That only matters if a window moved to its own thread, which RAD does not do.

**What actually blocks it** is that `Sidebar` holds one client's worth of state:
- `observe(workspaces, panes)` replaces the whole topology and carries one `selected` flag (`sidebar.rs:282`, `Workspace` at `:18-23`). Two windows would overwrite each other.
- `pending` effects and their completions are not tagged with a window. A focus effect must go back to the window that asked for it.
- There is one `rail_ui`: collapse, orders and variables. That is correct if they are Dashboard state and wrong if they are per window (note 1).
- There is one cached snapshot per revision (`sidebar.rs:137`), and action references are checked against one client id (`andamento-ffi/src/lib.rs:19,57-62`).
- The `.unplaced` and default-group coverage is computed against "observed workspaces", which means one window's.
- Local sections and groups are published per window from `sidebar_local`. In one shared catalog, their ids would collide unless they become Dashboard-level, which is what #290 proposes anyway.

**Shape: one core and many views.**

```c
typedef struct AndamentoCore AndamentoCore;   /* facts, catalog, templates, retained/ended,
                                                 managed content, Dashboard-level state */
typedef struct AndamentoView AndamentoView;   /* topology, selection, pending effects,
                                                 snapshot cache, per-view presentation */

AndamentoCore *andamento_core_create(AndamentoText config, char **error);
AndamentoView *andamento_view_create(AndamentoCore *core, char **error);
void           andamento_view_destroy(AndamentoView *v);

/* Unchanged signatures, now on the view: */
andamento_observe(v, ...); andamento_snapshot_acquire(v, ...);
andamento_dispatch(v, snapshot, action, ...); andamento_effects_take(v, ...);
andamento_complete(v, ...);
/* On the core, applied once: */
andamento_apply_patch_json(core, ...); andamento_tick(core, ...);
andamento_state_export(core, ...);    /* section 3 A */
```

- The rule for all calls is unchanged: the core and every view are used from one thread. Views are owned by the core and must be destroyed first, or they keep it alive through a reference count.
- A view's snapshot is evaluated against the shared catalog plus that view's topology and presentation. Evaluation can be shared across views when their inputs match. `RevisionEvaluation` (`sidebar.rs:138`) is already a per-revision cache that could be keyed by (core revision, view revision).
- **There is a precedent.** `ControllerState` already has `clients: BTreeMap<u16, ControllerClientState>` (`state.rs:432-439`), which the Zellij adapter uses for per-client rails and inspection over a shared metadata store. The view split brings the same idea to `Sidebar`.
- **A cheaper step first.** Keep one `Andamento` handle but make it observe the union of all windows' workspaces, with workspace IDs namespaced by window, and keep presentation shared. Effects then carry a workspace ID that Wheelhouse maps back to its window. This removes the N-times fan-out at once. It is only correct if collapse, order and display are Dashboard-wide (note 1). Selection would need a per-window "selected" set (`selected` per window, not a single flag), which is a small change to the `observe` ABI.

**Estimated work.**
- Andamento:
  - split `Sidebar` into core and view state;
  - add view handles to the FFI;
  - move `revision`/`client` checks to the view;
  - port tests; `crates/andamento-ffi/tests/smoke.c` and `tests/typed_detail.rs` cover the ABI.
- Wheelhouse:
  - move `core` from `UIShell_SidebarState` to process scope with a view per window;
  - drop the per-window fan-out in `uishell_sidebar_apply_live`/`poll_live`;
  - move `sidebar_local` and `sidebar_order`/`sidebar_display` to the Dashboard file through `andamento_state_export`.
- **Ordering:** do this after the identity ticket. Without stable workspace IDs across windows and restarts, a shared core's saved keys are no better than today's.

## Open questions for the map

- Is placement collapse Dashboard state or per-window Presentation State? It decides whether the cheaper one-handle step is enough.
- Should retained subject records be saved in full, so an ended subject still shows its last state after a restart, or trimmed to label and path?
- Does Dashboard structure (sections, groups, subscriptions) live in Andamento's saved state (A) or in a provider (C2, possibly flotilla)? That decides how much of `sidebar_local` moves into Andamento's payload.
