# The host interface (ABI 10)

What a host such as Wheelhouse uses to drive Andamento through its C ABI.
`crates/wheelhouse-ffi/include/wheelhouse.h` is generated from the FFI crate
by cbindgen (`scripts/generate-header.sh`; CI fails if it is stale), so each
declaration's comment there is its contract. This page is the map. The model
behind it is in [Slots and arrangements](slots-and-arrangements.md),
[The sidebar arrangement](sidebar-arrangement.md) and the
[Workspace Overlay](workspace-overlay.md).

`wheelhouse_abi_version()` returns 9. Each call has one form: there is no
compatibility with earlier versions, and a host requires exactly the version
it was built against.

## Conventions

- **Text** is `WheelhouseText {data, len}`: UTF-8, length-delimited. Input is
  borrowed for the call; output is borrowed from whatever returned it, and
  empty output text has a non-NULL pointer.
- **Errors.** A call that mutates returns 1 (or a status) on success, and
  changes nothing on invalid input. `error_out`, when not NULL, is cleared on
  entry and receives a message to free with `wheelhouse_string_free`. A panic
  poisons the sidebar: destroy and recreate it.
- **Getters** that fill an `out` struct return 0 for NULL handles or output
  and invalid indices, leaving `out` untouched.
- **Documents** (arrangements, slots, plans, the overlay, the Dashboard) are
  returned as one struct in one allocation, with every pointer and string in
  it valid until its `_free` call. `_free(NULL)` is harmless. A struct the
  host passes in is only read during the call.
- **Compare-and-set** calls return `WHEELHOUSE_COMMITTED`, `WHEELHOUSE_STALE`
  (no error, nothing changed: read again) or fail with an error
  (`WHEELHOUSE_INVALID`). Generations come from one clock per sidebar: they
  are never reused (not even across an import) and never persisted, so a
  host never keeps one across sessions.
- `WHEELHOUSE_NONE` (`SIZE_MAX`) is an index that names nothing.

## Identity

- **Workspace IDs** are 16 bytes (`WheelhouseWorkspaceId`) the host supplies,
  UUIDv7 in practice. Andamento only compares and prints them (as a
  hyphenated UUID, which is also their JSON form).
- **Entities** are `WheelhouseEntity {provider, kind, id}`. The provider is
  the Dashboard subscription the facts came from; the same kind and ID under
  two providers are two entities. `"local"` is Andamento's own provider
  (local sections, groups and refs); a host's subscription is never called
  that. Every entity a host passes names its provider.

## Facts and topology

| Call | |
|---|---|
| `wheelhouse_apply_patch_json(h, now_ms, provider, json)` | One producer patch, from `provider` (required, not `"local"`), stamped over every entity it names. |
| `wheelhouse_apply_entity`, `wheelhouse_apply_workspace` | Scalar facts (`WheelhouseFact`) on an entity, or on a workspace (a tab target), from `provider`. |
| `wheelhouse_tick(h, now_ms)` | Advance time without facts. |
| `wheelhouse_provider_retract`, `wheelhouse_provider_set_stale` | Drop a subscription's facts; mark one stale or fresh. |
| `wheelhouse_observe(h, workspaces, n, panes, m)` | The whole topology: `WheelhouseWorkspace {id, position, name, selected}`, `WheelhousePane`. |
| `wheelhouse_observe_workdirs` | Directory associations, replaced whole. |
| `wheelhouse_workspace_register` / `_forget` / `_registered` | Workspaces the host made itself; a forgotten one's record goes. |

## The snapshot and effects

`wheelhouse_snapshot_acquire` (or `_acquire_details`, with every catalog
card) returns the sidebar as nodes in preorder; `wheelhouse_snapshot_is_current`
checks it cheaply. An `WheelhouseNode` carries what ABI 2 kept in side calls:
its entity with provider, `stale`, `loop_key`, `workspace_id` (16 bytes, LIVE
nodes), a section's `default_host`/`has_order`/`order`, and `copy_url`. An
`WheelhouseControl` carries its display variable's name and `persist`. Fields,
diagnostics and details are read by index as before;
`wheelhouse_snapshot_detail_find` and `_request` take an `WheelhouseEntity`.

`wheelhouse_dispatch(h, snapshot, action)` queues effects;
`wheelhouse_effects_take` drains them. An `WheelhouseEffect` carries
`workspace_id` (FOCUS), `entity` with provider (MATERIALIZE, INSPECT), the
recipe or URL, and `has_primary_target`/`primary_target` (MATERIALIZE).
`wheelhouse_complete(h, request_id, outcome, workspace_id, message)` completes
every FOCUS and MATERIALIZE request. `wheelhouse_copy_subject_url` takes an
entity.

## Records

Andamento owns the Dashboard's and each workspace's logical state, and hands
it to the host as **records** to store as they are: their bytes (JSON, today)
are private to Andamento and may change from one build to the next. The host
decides where to keep them; Andamento never touches a filesystem.

| Call | |
|---|---|
| `wheelhouse_record_names` | `dashboard`, then `workspace/<id>` per registered workspace, one per line. |
| `wheelhouse_record_generation(h, name)` | A counter: nonzero, changes whenever the record does. Cheap to poll; store the record when it differs from the one last stored. |
| `wheelhouse_record_get` / `_put` | The bytes to store, and restoring them before the first observe. A record that doesn't read is rejected without change; putting `workspace/<id>` registers the workspace. |

There are no record versions or migrations: a Dashboard directory from before
ABI 4 is not imported.

## The Dashboard

| Call | |
|---|---|
| `wheelhouse_set_display_variable` | Set or reset a declared display variable. |
| `wheelhouse_local_set` / `_remove` | A local section, group or ref with its facts (`WheelhouseLocalFact`; an `ENTITY` fact names its provider). |
| `wheelhouse_set_sibling_order(h, loop_key, entities)` | The host's order for one sibling run, keyed by a node's `loop_key`. |
| `wheelhouse_dashboard_get` / `_free` | `WheelhouseDashboard`: the dashboard record's `generation`, the local entities with their facts (in the form `local_set` takes), and each saved sibling order with its `loop_key` (the same text nodes carry) and entities. Read again when the generation moves. |

## Workspaces

| Call | |
|---|---|
| `wheelhouse_slots_get` / `_free` | `WheelhouseSlots`: each `WheelhouseSlot` with its View Spec, rebind policy, `in_baseline`, `detached`, `WHEELHOUSE_SLOT_FLAG_*` flags and saved portable resolution (`has_saved`, `saved`). |
| `wheelhouse_slot_set` / `_remove` / `_reattach` | The user's own slots (`u:<id>`) and edits of baseline slots. |
| `wheelhouse_slot_plan(h, ws, key, applied)` / `_plan_free` | `WheelhouseSlotContent`: state, token, resolution identity, target, recipe, rebind, previous instance and the saved resolution. |
| `wheelhouse_slot_valid` / `_complete` / `_retry` / `_release_previous` | The plan protocol. |
| `wheelhouse_slot_resolution_set` / `_clear` | Compare-and-set of a slot's saved portable resolution, at the slot's generation (0 for none). |
| `wheelhouse_workspace_set_name` / `_set_mood` | The user's name and mood over the provider's. |
| `wheelhouse_overlay_get` / `_free` | `WheelhouseOverlay`: baseline version, the user's name and mood, the subject's name (its display label now), `owned`, and TOMBSTONED and DEPARTED notes. A workspace is named by the user's name if it has one, else the subject's. |
| `wheelhouse_workspace_content_revision` | Poll to learn when to read slots, plans and arrangements again. |
| `wheelhouse_layouts_get` / `_free` | `WheelhouseLayoutList`: a workspace's [Workspace Layouts](workspace-layouts.md) in the template's order, each `WheelhouseLayout` with `WHEELHOUSE_LAYOUT_DEFAULT` and `_CURRENT` flags. Empty when its subject's kind has no template. |
| `wheelhouse_layout_choose(h, ws, has_name, name)` | Show a layout, or (`has_name` 0) follow the default. The shown layout's edits are kept for when it is shown again; the arrangement's generation moves. Resetting a layout is `wheelhouse_arrangement_resolve` FOLLOW. |
| `wheelhouse_strip_get(h, subject)` / `_free` | `WheelhouseStrip`: the subject's [tool strip](tool-strip.md), its related workspaces then its linked items, each an `WheelhouseStripEntry` with its `WHEELHOUSE_STRIP_*` section and entity. Read each entry's detail for its label, state and actions. It follows the catalog: read it again when the snapshot's revision moves. |

## Arrangements

Both arrangements cross as the same document: an array of
`WheelhouseDocPanel`, each naming its parent by index.

```c
typedef struct {
    size_t parent;               /* an earlier SPLIT panel, or WHEELHOUSE_NONE for a root */
    WheelhouseText id;            /* stable, nonempty, unique in the document */
    double weight;               /* relative to siblings; finite and positive */
    uint32_t kind, axis;         /* WHEELHOUSE_PANEL_SPLIT/TABS, WHEELHOUSE_AXIS_ROW/COLUMN */
    const WheelhouseText *tabs;   /* a TABS panel's keys, in order */
    size_t tab_count;
    size_t selected;             /* an index into tabs, or WHEELHOUSE_NONE */
} WheelhouseDocPanel;
typedef struct { const WheelhouseDocPanel *panels; size_t panel_count; } WheelhouseArrangementDoc;
```

A split's children are the panels naming it as parent, in array order. A
host writes a document by walking its tree in preorder, and reads one in a
single pass, appending each panel to its parent.

- **A workspace's** (`wheelhouse_arrangement_get`, `_commit(h, ws, doc,
  expected)`, `_free`): at most one root; tabs are slot keys. Commit and get
  return `WheelhouseWorkspaceArrangement` with `status`, `generation`,
  `owned`, `flags` (PROVIDER_CHANGED, SOFT, UNRESOLVED), the Workspace Layout
  it arranges (`has_layout`, `layout`) and notes (PLACED, GONE, UNRESOLVED,
  and UNPLACED: slots the layout doesn't show, in slot order).
  `wheelhouse_arrangement_resolve` KEEPs or FOLLOWs after a provider or
  template change, and FOLLOW resets a layout at any time.
- **The sidebar's** (`wheelhouse_sidebar_arrangement_get`, `_commit(h, doc,
  expected)`, `wheelhouse_sidebar_restore_section`, `wheelhouse_sidebar_reset`,
  `_free`, and `wheelhouse_sidebar_arrangement_generation` to poll): an
  `WheelhouseSidebarDoc {dock}`, the dock with at most one root; tabs are
  section keys. Every call
  returns `WheelhouseSidebarArrangement` with notes CLOSED, UNRESOLVED,
  PLACED, RESTORED and DUPLICATE. UNRESOLVED also lists saved dashboard keys
  the template no longer resolves, as `display:`, `collapse:`, `order:` or
  `variable:` and the key.

Committing an arrangement never changes the snapshot's revision.

## From ABI 9

ABI 10 adds the [tool strip](tool-strip.md): `wheelhouse_strip_get` and
`_free`, which list a subject's related workspaces and linked items as
entities. `WheelhouseLayout` gains `glyph`, and `WheelhouseNote` gains `panel`:
for an UNPLACED slot, the tab panel showing it puts it in. Both structs
change layout, so a host built against ABI 9 must be rebuilt.

## From ABI 8

ABI 9 adds a content kind, `WHEELHOUSE_SLOT_CLEAT_SESSION`: a direct Cleat
packet view of a session (wheelhouse#104). A View Spec gains its fields at the
end: `cleat_transport` (`local` or `ssh`), `cleat_host`,
`cleat_runtime_root`, `cleat_daemon` and `cleat_session`. A primary slot
resolves to one when its provider publishes a direct endpoint
([Suggested Layouts](suggested-layouts.md#the-primary-slot)); MATERIALIZE then
carries no recipe and no primary target, so the host opens the workspace with
its primary waiting and the slot's first plan fills it. `WheelhouseViewSpec`
changes layout, so a host built against ABI 8 must be rebuilt.

## From ABI 7

ABI 8 adds [Workspace Layouts](workspace-layouts.md):
`wheelhouse_layouts_get`, `_free` and `wheelhouse_layout_choose`, the UNPLACED
note, and `has_layout` and `layout` in `WheelhouseWorkspaceArrangement`. A
View Spec gains a role (`has_role`, `role`), which a host may pass when it
sets a slot. Both structs change layout, so a host built against ABI 7 must
be rebuilt.

## From ABI 6

ABI 7 removes floating panels (wheelhouse#380): `WheelhouseSidebarDoc` loses
`floating`, so it and `WheelhouseSidebarArrangement`, which embeds it, change
layout, and a host built against ABI 6 must be rebuilt. Every section docks; a
region's `default-host` no longer places one. A stored record's floating
panels are dropped on import, and the sections only they held are docked;
host views (`u:<id>`) only they held are dropped.

## From ABI 5

ABI 6 removes `has_ttl` and `ttl_ms` from `WheelhouseDetailField`: an entity's
facts have no lease (andamento#173), so they were always 0. `stale` now
follows the provider alone. The struct's layout changed, so a host built
against ABI 5 must be rebuilt.

## From ABI 4

ABI 5 adds `has_subject_name` and `subject_name` to `WheelhouseOverlay`,
after `mood`: the change is additive, but the struct's layout changed, so a
host built against ABI 4 must be rebuilt.

## From ABI 3

| ABI 3 | ABI 4 |
|---|---|
| `wheelhouse_observe3`, `_observe_workdirs3`, `_complete3`, `*3` structs | `wheelhouse_observe`, `_observe_workdirs`, `_complete`, plain structs with 16-byte IDs |
| `wheelhouse_apply_patch_json_from`, `_apply_entity_from`, `_apply_workspace_from`, `wheelhouse_set_default_provider` | `wheelhouse_apply_patch_json`, `_apply_entity`, `_apply_workspace`, each with a required provider |
| `WheelhouseEntity3`, `wheelhouse_set_sibling_order3`, `_local_set3`, `_copy_subject_url3`, `_snapshot_detail_find3`/`_request3` | `WheelhouseEntity` (with provider) and the plain calls |
| `wheelhouse_snapshot_node_workspace`, `_node_provider`, `_node_loop_key`, `_region_hints`, `_copy_url_action`, `_control_variable` | `WheelhouseNode` and `WheelhouseControl` fields |
| `wheelhouse_snapshot_detail_workspace`, `_detail_provider`, `wheelhouse_effects_workspace`, `_effects_provider`, `_effects_primary_target` | `WheelhouseDetail` and `WheelhouseEffect` fields |
| `wheelhouse_content_get`/`_plan`/`_plan_entity`/`_valid`/`_complete`/`_retry`/`_release` (and their `3` forms) | the `primary` slot's `wheelhouse_slot_*` calls; `WheelhouseEffect.primary_target` |
| `wheelhouse_slots_acquire`/`_count`/`_get`/`_flags`/`_release` | `wheelhouse_slots_get`/`_free` |
| `wheelhouse_slot_plan_get`/`_release`, `_slot_plan_resolution` | `wheelhouse_slot_plan` returns `WheelhouseSlotContent`; `wheelhouse_slot_plan_free` |
| `wheelhouse_slot_resolution_acquire`/`_get`/`_release` | `WheelhouseSlot.saved`, `WheelhouseSlotContent.saved` |
| `wheelhouse_overlay_acquire`/`_info`/`_note`/`_release` | `wheelhouse_overlay_get`/`_free` |
| `wheelhouse_set_sidebar_arrangement`, `wheelhouse_sidebar_arrangement_acquire`, `wheelhouse_arrangement_info`/`_panel`/`_tab`/`_floating_first`/`_note_count`/`_note`/`_release` | `wheelhouse_sidebar_arrangement_get`/`_commit`/`_free`; restore and reset return the arrangement |
| nested `WheelhouseDocPanel.children` | `WheelhouseDocPanel.parent` |
| reading the dashboard record's KDL | `wheelhouse_dashboard_get` |
| `wheelhouse_overlay_export` (Overlay Sync proposal), `wheelhouse_dashboard_overlay_acquire`/`_info`/`_note`/`_release`, `.view` pins on refs | gone; template drift is in the sidebar arrangement's UNRESOLVED notes |
| record versions, unknown-node preservation, u64 Workspace IDs, `v2;` loop keys | gone |
