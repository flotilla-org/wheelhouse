# Research: what state a second frontend would otherwise reimplement

Status: research notes, 22 October 2026. Nothing here is decided.

Question: [#424](https://github.com/flotilla-org/wheelhouse/issues/424), for the
map [#421](https://github.com/flotilla-org/wheelhouse/issues/421). Which state
and rules in Wheelhouse's C would a web frontend, running the shared core as
wasm, otherwise have to write again? For each: where it lives, how big it is,
what it depends on, and how much of it Andamento already owns.

**Sources.** Wheelhouse is cited at `origin/main` 99f1355. Andamento is cited
at `/Users/robert/dev/andamento-worktrees/daily-driver-main`, 7fe869c (the
merge of andamento#180, which is what 99f1355 builds against), and written
`and:` below. Line counts are approximate and cover only the code named, not
whole files, unless a file is given whole.

## Summary

- **Andamento already owns the logical data.** ADR 0012 still says "accepted,
  not yet implemented", but most of it has landed: Slots and View Specs,
  both arrangement documents with generation compare-and-set, the Workspace
  Overlay's edit set, the plan/token protocol, names, moods, local sections,
  groups and pins, sibling order, and the saved records. Its C ABI is 79
  functions (`and:crates/andamento-ffi/include/andamento.h`, ABI 7).
- **What Wheelhouse still holds is rules around that data,** and they are
  written against RAD's config tree (`CFG_Node`) and `rd_*` arrangements, so
  none of it lifts out as is. The largest pieces:
  1. the arrangement document and its edit operations (`shell_arrangement.c`,
     ~900 lines, pure) and docking validity (`shell_docking.c`, ~300 lines,
     pure);
  2. the workspace store: commit, follow, slot plans, overlay notices
     (`uishell_workspace_store.c`, ~2,400 of 2,647 lines);
  3. the arrangement edit commands (`shell_commands.c:692-1336`, ~640 lines);
  4. the sidebar's own policy: local-entity housekeeping, workspace homing,
     snapshot role interpretation, chip semantics, drop rules and the drop
     table (~1,500 lines across `uishell_sidebar.c`, `uishell_local_groups.c`,
     `uishell_detached_cards.c`);
  5. workspace lifecycle rules (close kind, detach, next selection, naming,
     materialize handling, ~400 lines).
- **Some logical state is still outside Andamento:** a local workspace's home
  (`lives_in`, `lives_with`) is in Wheelhouse's config tree
  (`uishell_sidebar.c:815-830`), subscriptions are in
  `subscriptions.wheelhouse` (`uishell_subscriptions.c`), and the
  per-workspace theme is a config field.
- **Clearly per-frontend:** drawing, docking pixels and drop geometry, preview
  budgets and surfaces, hover-card drawing, keybindings, theme rendering,
  Jackstay, the terminal runtime, file I/O and platform folders. Together this
  is the large majority of `shell_core.c` (~11k of 13.5k lines) and nearly
  all of `uishell_views.c`.
- **Not built yet:** Workspace Layouts and the tool strip
  (`docs/design/subject-workspaces.md`), the home workspace (TODOs at
  `shell_commands.c:1571`, `shell_core.c:4855`) and the Automation Endpoint's
  `/v1/commands` (ADR 0014; `src/ingress/lib.rs:253-256` serves only health,
  metadata and workdirs). The design already puts layouts in Andamento as
  templates (`subject-workspaces.md`, "Who defines what"), so they can be
  built there from the start.

## What Andamento owns today

| Area | Andamento | Where |
|---|---|---|
| Facts, providers, staleness | yes | `and:crates/andamento-ffi/src/lib.rs` (`apply_patch_json` 263, `provider_retract` 280, `provider_set_stale` 292) |
| Sidebar evaluation, templates, display variables, detail cards | yes | `and:crates/andamento-core/src/sidebar.rs`, `detail.rs`, `template_config.rs` |
| Local sections, groups, pins (`.section`, `.group`, `.ref`) | stores them | `and:…/andamento-ffi/src/dashboard.rs` (`local_set` 99, `local_remove` 125) |
| Sibling order | yes | `dashboard.rs` (`set_sibling_order` 68) |
| Workspaces: register, forget, subject | yes; IDs supplied by the host | `lib.rs` (`workspace_register` 535, `workspace_forget` 544) |
| Slots, View Specs, rebind policy, portable Target Resolutions | yes | `and:…/andamento-ffi/src/slots.rs` (`slot_set` 263 … `slot_resolution_clear` 562) |
| Plan/token protocol for slot updates | yes | `slots.rs` (`slot_plan` 359, `slot_complete` 427, `slot_retry` 440) |
| Workspace and sidebar arrangement documents | stores, validates, reconciles | `and:…/andamento-ffi/src/documents.rs` (`arrangement_commit` 307, `sidebar_arrangement_commit` 416) |
| Workspace Overlay | yes: overrides, tombstones, `u:` keys, name, mood, arrangement ownership | `slots.rs` (`overlay_get` 688, `workspace_set_name` 598); `and:docs/sidebar-design/workspace-overlay.md` |
| Persistence | records only; never touches files | `dashboard.rs` (`record_get` 23, `record_put` 30); bytes are JSON (`and:crates/andamento-core/src/records.rs:125`) |
| Selected workspace | no; Wheelhouse tells it | `uishell_sidebar.c:1694` sends `split->inventory.selected` through `andamento_observe` |
| Previews | no | `and:docs/sidebar-design/host-and-renderer-independence.md:126-129` |
| Commands | no registry; a small semantic action enum and host effects | `and:crates/andamento-core/src/sidebar.rs:43`, `:135` |
| Browser wasm | no | only `wasm32-wasip1` for Zellij plugins (`and:.cargo/config.toml:2`); no wasm-bindgen. The core does no I/O (`and:docs/sidebar-design/core-interface.md:15-17`) |

Two documents disagree with the code. ADR 0012 says records are KDL in a
versioned envelope that keeps unknown nodes; Andamento writes unversioned JSON
(`records.rs:125`), and `and:docs/sidebar-design/host-interface.md:197` lists
versions and unknown-node preservation as dropped. ADR 0013 also says "not yet
implemented", yet its edit set is in `slots.rs`.

## Inventory

Paths are under `src/` unless given otherwise.

### 1. Workspace state and lifecycle

| What | Where | Lines | Depends on | Andamento |
|---|---|---|---|---|
| Workspace IDs (UUIDv7), Window IDs | `shell/shell_workspace_id.c` (`uishell_workspace_id_make` 19, string round trip 50-80, index 82-115, Window IDs 168-212) | 214 | clock and GUID (OS) | host supplies IDs by ADR 0012, so each host generates them |
| New workspace and its name ("Workspace N", skipping names detached ones use) | `shell/shell_commands.c:1397-1427` (`uishell_new_workspace`) | ~30 | config tree | no |
| Select, close, detach; next-selected rule (next, else previous); last close makes a fresh one | `shell_commands.c:1490-1584` | ~95 | config tree | no |
| Which close a row offers (remove pin, destroy, detach) | `uishell/uishell_sidebar.c:2622-2711` (`uishell_sidebar_close_kind`, `_workspace_subject_ended` 2650) | ~90 (rules), drawing 2713-2757 | snapshot nodes | partly: ended status comes from it |
| Register and forget; topology and workdirs | `uishell_sidebar.c:1651-1745` (`_register`, `_observe`) | ~95 | config tree; terminal `cwd` from Views | calls `workspace_register/forget`, `observe`, `observe_workdirs` |
| Materialize effect: find, reattach a detached workspace, or create one marked `arrangement_fresh`; no recipe means an unfilled primary slot | `uishell_sidebar.c:1834-2012` (`_populate`, `_effects`) | ~180 | config tree; `wm_open_in_browser`, `wm_set_clipboard_text` for the URL effects | emits the effects; executing them is the host's |
| Workspace mounts, Controlled Split, inventory and selection | `shell/shell_core.c:4566-4755`, `uishell_root_controlled_split_from_window` 4669 | ~190 | window state | no (selection is Presentation State) |
| Can-close, subject lookup | `shell_core.c:4858-4890` | ~30 | config tree | no |
| Logical state as text (golden-test dump) | `uishell/uishell_logical_state.c` | 532 | snapshot reads, config tree | reads it; a good spec of what counts as logical |

### 2. View state

| What | Where | Lines | Depends on | Andamento |
|---|---|---|---|---|
| View addresses, slot key assignment (`u:<n>`), rekey and readdress | `shell_core.c:3636-4018` (`rd_view_key_assign` 3765, `rd_view_address_from_cfg` 3812, `rd_cfg_from_view` 3834) | ~380 | config tree | the `u:` namespace is ADR 0013's; keys are assigned here |
| Which View settings are logical and which are presentation | `shell_core.c:4059` (`rd_view_setting_is_logical`), View Presentation State 4018-4226 | ~210 | config tree | no |
| Panel Presentation State, orphan sweep | `shell_core.c:4226-4410` | ~185 | config tree | no (Presentation State by ADR 0012) |
| View state lifetime, Renderer contexts | `shell_core.c:1563-1708` | ~145 | per-View arenas | no |
| Renderer registry and View traits | `uishell/uishell_renderers.h:67-140`, `shell_core.c:40-270` | ~300 | the C Renderers | no; each frontend has its own Renderers, but the traits (`RD_ViewTraits`) feed docking validity |
| View kinds and their View Specs | `uishell/uishell_workspace_store.c:302-725` (spec from View 317-400, conversion, match, hash, `uishell_store_view_from_spec`) | ~420 | config tree | stores the specs; mapping a kind to a spec is here |
| `wheelhouse:` page URLs | `uishell_workspace_store.c:226-302` | ~75 | none | no; another frontend shows a placeholder for them |

`uishell/uishell_views.c` (6,214 lines) is Renderers. Its only rule is the
terminal's placeholder words (`uishell_views.c:5996`).

### 3. Arrangement and layout rules

| What | Where | Lines | Depends on | Andamento |
|---|---|---|---|---|
| Typed arrangement document: panels with stable IDs, tabs by View Address, selected tab, weights, alternating axes; validation (`rd_arrangement_problem` 507); edit operations (split, add, remove, close with heir, tab insert/move/detach, `settle` 770-797, drop 798, select, reorder, resize 850, equalize) | `shell/shell_arrangement.c`, `.h` | 896 + 379 | none: no UI, draw or OS calls | stores and reconciles the document; the edit operations are only here |
| Live arrangements: `rd_arrangement_set` with versioning, take/release Views | `shell_core.c:3373-3636` | ~260 | View state | no |
| Docking validity: host kinds, traits, `rd_dock_check` 29, placement 67, creation 98, closure 114, messages 134, `rd_dock_restore` 203 (repair of saved docs) | `shell/shell_docking.c`, `.h` | 298 + 201 | traits from the Renderer registry; `rd_dock_moving_width` 283 takes pixel widths | no |
| Arrangement commands: tab open/close/move/duplicate, split, new panel, close panel with heir, rotate | `shell_commands.c:692-1336` (`uishell_drop_view` 718, `uishell_dispatch_tab_command` 757, `uishell_dispatch_panel_command` 1020) | ~640 | config tree, live arrangement | no |
| Gestures: drag state machine, drop target, boundary resize (edits a copy, commits at the end) | `shell_core.c:270-392`, `5228-5387`, `5660-5728` | ~500 | pixels | no; ADR 0012 keeps these in each frontend |

ADR 0012 keeps the live copy and docking previews in the frontend because they
need pixels. The document type and its edit operations do not: a web
frontend would otherwise write its own `shell_arrangement.c`, and drift
between the two would surface as documents one frontend writes and the other
repairs.

### 4. Workspace store, Slot plans and the Workspace Overlay

All in `uishell/uishell_workspace_store.c` (2,647 lines; header 72). Only the
notice banner (2166-2200) draws.

| What | Lines | Andamento |
|---|---|---|
| Design comment: load, commit, follow, plan; device-local layout | 1-90 | — |
| Key rules: user keys vs provider keys, `uishell_store_assign_keys`, building the document from the live arrangement (`uishell_store_doc` 817) | 728-946 | validates against the Slot set |
| Portable Target Resolutions: a Cleat session is saved only if it is this machine's | 947-1115 | stores them (`slot_resolution_set/clear`) |
| Follow: rebuild the live arrangement from Andamento's document and slots (`uishell_store_follow` 1170) | 1115-1340 | supplies the document |
| Slot plans: per-state handling (CURRENT, UPDATING with replace / keep-previous / ask, HELD, UNAVAILABLE, FAILED); a Cleat session is never replaced in place (`uishell_store_plan_owner` 1562, `uishell_store_apply` 1429, `uishell_store_complete` 1531) | 1341-1680 | owns the plan and token; applying it is here |
| Ask, decline, release previous, retry, name and mood sync | 1683-1800 | `slot_release_previous`, `slot_retry`, `workspace_set_name/mood` |
| Overlay: read flags and tombstones (`uishell_store_read_overlay` 1859), resolve arrangement (Keep or Follow, 1934), reattach 1954, keep 1977, remove 2003 | 1775-2040 | owns the edit set; Wheelhouse reads it and offers resolutions |
| Override detection: a changed `applied_spec` on a following View detaches it | 2367-2374 | — |
| Tombstone vs remove on close | 2385-2403 | — |
| A fresh workspace takes the Suggested Layout (`primary_only` aside) | 2316-2338 | — |
| Notices and actions: text and badges | 2041-2165 | — |
| Commit: skip while unfollowed or mid-gesture, set changed slots, remove closed seen slots, commit at expected generation, retry once on stale, follow if Andamento placed slots (`uishell_store_sync_owner`) | 2286-2447 | `slot_set`, `slot_remove`, `arrangement_commit` |
| Device-local text: strip Views and runtime keys before saving (`uishell_workspace_store_window_text`) | 2529-2647 | — |

Runtime side of slot updates: `uishell/uishell_managed_content.c` (110 lines)
starts a new Cleat session before the token check. That is terminal runtime,
per frontend.

### 5. The Slot Host

| What | Where | Lines | Kind |
|---|---|---|---|
| Held commands, capped at 64, replayed in order when the Slot shows (`wh_slot_host_hold_cmds` 1726, `_replay_cmds` 1745) | `shell_core.c:1708-1755` | ~50 | rule |
| Generic placeholder words per wait state (`wh_slot_placeholder_from_ctx` 1757) | `shell_core.c:1757-1784` | ~30 | rule (text) |
| Waiting vs unverified: waiting needs a wait, no instance and no content of its own; unverified is a live instance whose content is unavailable (`uishell_store_view_slot`) | `uishell_workspace_store.c:402-458` | ~55 | rule |
| Placeholder drawing and the unverified line (`wh_slot_host_placeholder_ui` 1784, `wh_slot_host_ui` 1810) | `shell_core.c:1784-1835` | ~50 | drawing |
| Types: `WH_ViewContext`, `WH_SlotStatus`, `WH_SlotWait`, `WH_SlotPlaceholder` | `shell/shell_core.h:30-160` | ~130 | interface |

Small. The waiting/unverified rule and the wait states are what a web Slot
Host needs to agree on; the words could come with them.

### 6. Sidebar policy

Andamento evaluates the sidebar and stores its local entities. Wheelhouse
adds these rules on top:

| What | Where | Lines | Depends on |
|---|---|---|---|
| Snapshot role interpretation: a `layout="section"` node is keyed `.section:<id>` and its parent becomes a hidden container; a section's only group has no header; `.unplaced` hides when empty | `uishell_sidebar.c:848-985` (`_roles` 862, `_has_children` 902, `_leftover_hidden` 913) | ~140 | snapshot |
| Local entities: build the tree from `dashboard_get`, renumber after every edit, keep a section's title equal to its groups' names unless renamed (`_local_settle` 1373), make the default Workspaces section and group (`_local_ensure_default` 1403), pin position base 1000000 (1311) | `uishell_sidebar.c:989-1420` | ~430 | `local_set/remove` |
| Section and group editing: rename, add, drop (removes its pins, and the section if it empties), delete (its workspaces lose `lives_in`; the default can't be deleted), place, move | `uishell/uishell_local_groups.c:1-330` | ~330 (+310 UI) | config tree, `local_*` wrappers |
| Workspace homing: a local workspace lives with a project (`lives_with`), else in a group (`lives_in`), else the default group; published as patches | `uishell_sidebar.c:815-830`, `1543-1653` (`_publish_workspaces` 1564) | ~130 | **state in Wheelhouse's config tree**, `apply_patch_json`, `apply_workspace` |
| Reveal: deepest occurrence, first section on a tie | `uishell_sidebar.c:2016-2097` | ~80 | snapshot |
| Row click meaning: toggle, activate, or "no opening recipe" | `uishell_sidebar.c:2818-2995` (`_entry_signal`) | ~180 | snapshot, mixed with drawing |
| Chips: field protocol (`_chip_fields_start` 3040), status (3077), attention (3096), colour (3107), icon fallback (3123); folding | `uishell_sidebar.c:3040-3160`; `uishell/uishell_sidebar_chips.h` | ~120 + 54 | snapshot fields |
| Drop refusals ("only workspaces go in Workspaces", "groups only go into your own sections", section rules) and gap selection | `uishell_sidebar.c:3545-3726` (`_section_drag_refusal` 3596) | ~180 | mixed with pixels |
| Drop table (drag-model.md): pinned area, back inline, new pinned area, move to another group (Alt: ghost), move group, reorder; full sibling order (`_group_order` 724, `_reorder_commit` 750, `_drag_finish` 782-960) | `uishell/uishell_detached_cards.c:710-960` | ~250 | config tree for `lives_in`, `set_sibling_order`, `local_*` |
| Pins: create (`_pin_add` 346), migrate legacy (225), new areas (447-505) | `uishell_detached_cards.c` | ~200 | `local_*` |
| Sidebar dock store: load, commit, follow, restore, reset; recommit after stale; hide the container once local sections exist; content-sized sections commit weight 1 | `uishell/uishell_sidebar_store.c` (`_store_follow` 544-695, `_store_sync` 778-862) | ~900 of 989 | `sidebar_arrangement_*`, `sidebar_restore_section`, `sidebar_reset` |
| Subscriptions: list, add, remove, go live, poll | `uishell/uishell_subscriptions.c` | 313 | **own file** (`subscriptions.wheelhouse`); spawning `flotilla pm connect` is platform | `provider_set_stale`, `provider_retract` |

Card placement (transient, float, inline, pinned), panel sizing and the
render (`uishell_sidebar.c:3821-4870`, `5109-5356`) are frontend-specific.

### 7. Hover cards and previews

| What | Where | Lines | Kind |
|---|---|---|---|
| Timing: open 300 ms, leave 400 ms, transition 100 ms, corridor padding 12 pt, near gap 16 pt | `uishell/uishell_hover_cards.c:5-18` | ~15 | product policy |
| Which card opens: none while a menu, a row drag or a focused card; switch at once if one is open, else wait; only a node's home row is its source (`_card_hover_at` 215-245, `_card_source_at` 248) | `uishell_hover_cards.c:149-289` (corridor 149-176, leave `_card_tick` 267) | ~140 | policy, needs pointer geometry |
| Card identity: actions kept as entity plus intent and resolved against the next snapshot; navigation path stack | `uishell_hover_cards.c:62-120`, `178`, `372` | ~80 | rule |
| Drawing and input routing | `uishell_hover_cards.c:291-1245` | ~950 | frontend |
| Preview demand, refresh interval by width (≤128 pt: 200 ms; ≤256 pt: 100 ms; larger: none), surface sizing, 4 deferred surfaces per frame | `shell_core.c:2826-2975` (`rd_workspace_preview_refresh_interval` 2887) | ~150 | renderer |
| Terminal preview admission: 1 start and 4 updates per frame; per-workspace refresh phase | `uishell_views.c:373-440`, `4600-4640` | ~110 | renderer |

Detail content is Andamento's (`snapshot_detail_*`). The timings could be
shared as constants; the rest is per frontend. Preview policy depends on how
each frontend draws and presents, so a web frontend needs its own.

### 8. Commands

| What | Where | Lines |
|---|---|---|
| Shell command packs (app, query, font, window, panel, tab, ui_event) and default bindings | `shell/shell_commands.h` | 343 |
| App commands (config, file, viewer; subscriptions) | `uishell/uishell_commands.h`, `uishell/uishell_dispatch.h` | 247 + 592 |
| Dispatch | `shell/shell_commands.c` | 2,023 |
| Vocabulary, binding-name remap | `uishell/uishell.mdesk` | 119 |

Commands that change logical state do so by editing RAD config and the live
arrangement, which the stores commit later; none goes through Andamento
directly. Those are `new_workspace`, `select_workspace`, `close_workspace`,
`detach_workspace`, the panel and tab commands, `reset_to_*_panels` and
`add/remove_subscription`. A web frontend would otherwise write each again.
Everything else is platform (windows, fullscreen, files, clipboard, URLs) or UI
(palette, fonts, `ui_event`). ADR 0014's named-command endpoint is not built.

### 9. Settings

| What | Where | Lines | Depends on |
|---|---|---|---|
| Config tree library | `config/config_core.c` | 918 | generic; no product settings |
| Setting schemas (user, project, theme colour, window, workspace, tab) | `uishell/uishell_meta.h:28-110` | ~80 | RAD schema strings |
| Reading settings | `shell_core.c:549-617`, view settings 2377-2431, 4165-4219 | ~150 | config tree |
| Theme presets and resolution (ADR 0009) | `shell/shell.mdesk:121-950`; `shell_core.c:10905-11030` | ~950 | renderer |
| Keybindings | `config/config_bindings.c`, `UISHELL_BIND` tables | — | `WM_Key`, platform menus |
| Eval hooks for the palette and settings UI | `uishell/uishell_eval.c`, `shell/shell_eval.c` | 654 + 597 | RAD eval; no Andamento |

ADR 0012 puts fonts, keybindings and chrome in device-local Presentation
State, so most settings are per frontend by design. The shared parts are
small: a workspace's `label` (already Andamento's name) and its `theme`, which
is a config field today.

### 10. Persistence

| What | File | Format | Code |
|---|---|---|---|
| Andamento records (`dashboard`, `workspace/<id>`) | `<Dashboard>/dashboard.record`, `workspace-<id>.record` | JSON from Andamento | `uishell/uishell_sidebar_records.c` (write via temp and rename 97, save ~1 s after a generation change 138, load 225), 280 lines |
| Dashboard directory and identity | `<shared>/dashboards/<name>/id`, `<device>/last_dashboard` | text | `uishell/uishell_dashboard.c` (platform folders 73, open 119), 219 lines |
| Subscriptions | `<Dashboard>/subscriptions.wheelhouse` | RAD config | `uishell_subscriptions.c:167-188` |
| Presentation State, device settings | `<device>/presentation/<Dashboard ID>.wheelhouse`; user file | RAD config | `uishell_dispatch.h:17-57`, `366-392`; `shell_core.c:4778-4843` |

The browser has no filesystem, so every host writes records its own way.
Andamento already never touches files, so the records themselves need
nothing new. The deciding rules (save on generation change, after a delay,
flush on release) are ~50 lines and could be documented rather than shared.

## Clearly frontend-specific

- Drawing: `shell_core.c` view surface effects and cover flow (2615-3373),
  panel frame segments (5043-5284), drop-target visuals (5387-5660),
  `rd_panel_area_ui` (5728-7000), chrome placement (7052-7278),
  `rd_window_frame` (7278-10610), eval visualisation, colours and fonts.
- The frame loop, activation routing, window geometry (`shell_core.c:7387-7499`,
  `11646-13497`).
- Renderers: `uishell_views.c`, `uishell_terminal_glyph.c`, `uishell_jackstay.c`.
- Input: hover-card event routing, drag state machines, keybindings.
- Platform: workspace ID clock and GUID, platform folders, file writes, URL and
  clipboard effects, spawning `flotilla pm connect`, the terminal runtime.
- Previews and their budgets.

## Candidates for the shared core, by what each saves a second frontend

1. **The arrangement document and its operations** (`shell_arrangement.c`,
   `shell_docking.c`, the command bodies in `shell_commands.c:692-1336`):
   ~1,800 lines of pure rules, today in C over `rd_*` types. ADR 0012 would
   need a narrower line: the live copy and previews stay in the frontend, the
   type, validity and edits move.
2. **The workspace store's protocol** (commit, follow, plan, overlay
   resolutions, notices): ~2,400 lines. Much is glue between Andamento's
   calls and RAD config; written against Andamento's own types it would
   shrink.
3. **Sidebar policy:** local-entity housekeeping, workspace homing (moving
   `lives_in`/`lives_with` into Andamento's state), role interpretation, chip
   semantics, drop refusals and the drop table: ~1,500 lines.
4. **Workspace lifecycle:** close kind, detach, next selection, naming,
   materialize rules: ~400 lines. The effects stay host-executed.
5. **Subscriptions** as Dashboard state rather than a Wheelhouse file.
6. **Slot Host rules** (wait states, waiting vs unverified, held commands):
   ~150 lines, small but easy to get subtly different.
7. **Workspace Layouts** (not built): build them in the core from the start.

Not worth sharing: preview policy, hover-card behaviour beyond its constants,
keybindings, theme rendering, file I/O.
