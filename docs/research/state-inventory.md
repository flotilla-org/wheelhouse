# Research: what Wheelhouse stores today, sorted into layers

Status: research notes for #291 (map #290), 10 October 2026. Read at
`origin/main` `ba36986`. Andamento was read at the CI-pinned revision.
Nothing here is decided. The layer column is a proposal for #294 to check.

Layers, using the vocabulary in `CONTEXT.md` and the map's Notes:

- **Dashboard**: provider subscriptions, grouping, sections, order and display,
  which turn provider entries into a Workspace Inventory. It is logical and
  shareable.
- **Workspace (logical)**: a workspace's identity and subject, and its
  view specs (view kind and target), when they come from a provider's
  Suggested Layout.
- **Workspace Overlay**: the user's changes to a workspace relative to a
  Suggested Layout. It is logical and could be shared later.
- **Presentation State**: one frontend on one device: geometry, tab placement,
  theme and chrome.
- **Leftover**: inherited from the RAD debugger. It does nothing in
  Wheelhouse, or only fixtures and diagnostics use it.

## 1. Where state lives

| Store | Path / origin | Read | Written | Notes |
|---|---|---|---|---|
| RAD **user** file | `--user:<path>`, or the path in `<appdata>/uishell/last_user`, or `<appdata>/uishell/default.uishell_user` (`shell_core.c:10008-10038`, `uishell_main.c:15-19`) | `open_user` at startup (`uishell_dispatch.h:58-205`) | `write_user_data`: **autosave every 5 s** (`shell_core.c:11144-11152`), and on exit (`uishell_dispatch.h:490-500`). Written through a temp file, then renamed (`uishell_dispatch.h:7-35`) | Holds **all windows**, so every workspace, layout and sidebar record. It also holds the settings, keybindings and recent projects. |
| RAD **project** file | `--project:<path>`, or an implicit argument. Not opened when absent (`shell_core.c:10040-10051`) | `open_project` | Autosave and exit, as for the user file | In practice it holds only the `project` schema keys (name, theme, tab_width). `open_window` creates new windows in the window's own bucket, which is `user` by default (`shell_commands.c:1652-1658`). |
| `last_user` | `<appdata>/uishell/last_user` | startup | `record_user_as_last_opened` (`uishell_dispatch.h:322-333`) | The path of the last user file opened. |
| `transient` and `command_line` buckets | cfg roots created at init (`shell_core.c:9924-9925`) | in process | never saved | `transient/immediate/*` keeps per-window query and lister views (`window_query_%p`, `shell_commands.c:1935`). |
| Andamento KDL config | `--andamento_config:<file>`, needed with `--andamento_socket` (`uishell_main.c:203-213`). Otherwise an embedded config (`uishell_sidebar_local_config`, or the fixture and daily configs) (`uishell_sidebar.c:1355-1357`) | startup. Probed once, then one `andamento_create` per window | Wheelhouse never writes it | See section 4. |
| Andamento core memory | one `Andamento *core` per window, in `RD_WindowState.sidebar` (`uishell_sidebar.c:1345-1390`) | — | — | Lost on exit. Wheelhouse replays part of it from the user file every launch (section 5). |
| Ingress recording | `<logdir>/ingress.jsonl`, only with `--ingress_record` (`uishell_main.c:221-227`) | — | rotating, 4×4 MB | Diagnostics. |
| Fixture geometry dumps | `--sidebar_subject_geometry:<path>` (`uishell_sidebar.c:4343-4354`) | — | each frame, fixture only | Leftover (tests). |

## 2. RAD config tree: shape and layer of each key

Counting the string literals passed to `cfg_node_*`, `rd_setting_*` and
`rd_*_from_cfg` helpers in non-diagnostic shell, uishell and config code gives
**125 distinct keys**. Diagnostics and fixtures add the rest of the ~131. Some
of them are values (`true`, `0.5`) or schema type names rather than keys. They
are grouped by where they sit in the tree.

Settings are looked up from the most specific node upwards: view → panel →
window → `user` → `project`, then the schema `@default`
(`rd_setting_from_name`, `shell_core.c:417-500`). So a key's meaning depends
partly on which node it sits under.

### 2.1 `user` bucket: settings and app records

| Key(s) | Where defined | Writer / when | Layer | Notes |
|---|---|---|---|---|
| `animations`, `scrolling_animations`, `tooltip_animations`, `menu_animations`, `animation_speed` | `user` schema (`uishell_meta.h:33-38`) | Settings UI, by the user | Presentation | |
| `main_font`, `code_font`, `terminal_fallback_fonts` | same | user | Presentation | These hold device-local font paths, so they cannot be shared as they are. |
| `theme`, `theme_colors` (children `theme_color{tags,value}`) | `user` and `project` schemas | user | Presentation | Theme is per frontend and device. A per-workspace `theme` is listed in 2.3. |
| `opaque_backgrounds`, `background_blur`, `drop_shadows`, `rounded_corner_amount`, `inactive_panel_dim`, `panel_gap`, `panel_border_px`, `tab_gap`, `tab_width`, `overlay_scrollbars`, `cursor_scope_lines`, `cursor_scope_end_annotations`, `cursor_trail` | `user` schema | user | Presentation | Shell chrome and density. |
| `mac_window_decorations`, `mac_native_menu_bar`, `compact_menu_bar`, `show_project_selector`, `show_status_bar`, `tabs_in_title_bar`, `focus_menu_bar_with_alt`, `hover_cards_outside_sidebar`, `use_native_file_system_dialog` | `user` schema | user | Presentation | Chrome placement (ADR-0006). |
| `deny_application_clipboard_writes` | `user` schema | user | Presentation (a device policy) | A security preference. It is per device and should not travel with a Dashboard. |
| `autocompletion_lister`, `view_call_argument_helper`, `transient_tabs` | `user` schema | user | Leftover | Expression-editing and source-snapping features from RAD. |
| tweak names (`crt_*` ×6, `coverflow_*` ×10) | the dynamic `code_defaults` schema that `user` inherits (`shell_core.c:600-635`, `9865-9870`) | the dev tweak UI | Presentation (dev) | "Write default to source" rewrites the C literal instead (`shell_core.c:637-790`). |
| `keybindings` (children carry `ctrl`/`shift`/`alt`/`super` and command names) | `config_bindings.c` | `reset_to_default_bindings` the first time a user file loads with none (`uishell_dispatch.h:178-185`). Rebinding UI. | Presentation | Per device. |
| `recent_project{path,name}` (max 32) | `uishell_meta.h:250` | `record_project_in_user` on every project open (`uishell_dispatch.h:276-320`) | Presentation | An app MRU. |
| `current_path` | — | `set_current_path` on project open (`uishell_dispatch.h:398-404`) | Presentation | The file dialog's directory. |

### 2.2 `project` bucket

| Key | Writer / when | Layer | Notes |
|---|---|---|---|
| `name` | user | Leftover / Dashboard name? | The title-bar project selector, hidden by default. A Dashboard would need a name; this is the nearest thing to one. |
| `tab_width`, `theme`, `theme_colors` | user | Presentation | They override the user values. |
| `display_pointer_addresses_before_contents` | user | Leftover | A debugger eval visualiser setting. |
| `project` *on any node* | — | Leftover | `rd_cfg_is_project_filtered` hides a node whose `project` names a different project file (`shell_core.c:281-286`). |

### 2.3 `window` (the root Controlled Split; it lives in the `user` bucket)

| Key | Writer / when | Layer | CFG_ID-keyed runtime alongside | Notes |
|---|---|---|---|---|
| `size`, `pos`, `monitor`, `fullscreen`, `maximized` | the window manager, on move or resize | Presentation | `RD_WindowState` (hash keyed by window `CFG_ID`, `shell_core.h:620-700`) | `open_window` deliberately does not copy these (`shell_commands.c:1662-1672`). |
| `label` | user | Presentation (a legacy window-backed workspace name) | | |
| `font_size`, `row_height`, `tab_height`, `smooth_ui_text`, `hint_ui_text`, `smooth_code_text`, `hint_code_text`, `use_project_theme` | user | Presentation | | Per-window overrides (`uishell_meta.h:73-86`). |
| `control_split_pct`, `control_split_collapsed` | dragging or collapsing the sidebar (`shell_core.c:3179-3265`) | Presentation | | |
| `control_views` + `control_views_split_x` | docking into the sidebar | Presentation (sidebar layout) | | The sidebar's dock tree (`RD_DOCK_SIDEBAR_ROOT`, `shell_docking.h:8`). Its views are `sidebar_section`, `workspace_selector` and the legacy `pinned_cards`. |
| `floating_panels` | dragging a View out to float | Presentation | | |
| `section_positions/<key>[closed]` (`UISHELL_REGION_INVENTORY`) | `uishell_sidebar_dock_layout`, once per snapshot (`uishell_sidebar.c:4439, 4630-4680`) | Presentation | | The Andamento regions this window has placed, and the ones the user closed. It is keyed by the Andamento region key, which is stable. |
| `sidebar_layout_sized` | the first manual sidebar resize (`uishell_sidebar.c:4861-4863`) | Presentation | | |
| `sidebar_display/<control-name> true\|false` | when the user toggles an Andamento display control declared `persist` (`uishell_sidebar.c:535-558`) | **Dashboard** (display) | `UIShell_DisplayRestore` list, keyed by name | It is **replayed into Andamento on every launch** (`uishell_sidebar_restore_display`, `:772-798`). |
| `sidebar_order/<loop-key> kind id kind id…` | when the user drags to reorder rows (`uishell_sidebar_set_order`, `:600-628`) | **Dashboard** (order) | | **Replayed** through `andamento_set_sibling_order` on launch (`:633-650`). It is keyed by Andamento loop key and entity, both stable. |
| `sidebar_local/section{id,label,group{id,label,default,card{ghost,kind,entity,label,compact}}}` | local sections, groups and pins: creating, renaming, deleting or dragging them (`uishell_local_groups.c`, `uishell_detached_cards.c:533-542`). Created by default (`uishell_sidebar.c:996-1012`) | **Dashboard** (grouping and sections). Pinned `card`s are Dashboard too, as references to provider entities. | `local_published[]` (strings), `rename_node` and `confirm_delete` (CFG_IDs) | **Republished to Andamento** as `.section`/`.group`/`.ref` entities whenever their hash changes (`uishell_sidebar_publish_local`, `:1160-1265`). IDs are GUID strings, which are stable. |
| `workspace` (many) / `detached_workspace` | see 2.4 | | | |
| `panels` + `split_x` directly on the window | legacy | Presentation | | A "window-backed workspace" left over from before `workspace` nodes existed (`shell_core.c:3035-3050`). |

There is no key for the **Visible Workspace**. It is held only in
`RD_WindowState.root_controlled_split_selected_workspace_id`, a `CFG_ID`
(`shell_core.h:673`), and after a restart it falls back to the first workspace
(`shell_core.c:3020-3035`).

### 2.4 `workspace` (a child of a window)

| Key | Writer / when | Layer | CFG_ID-keyed runtime alongside | Notes |
|---|---|---|---|---|
| node identity | `uishell_new_workspace` (`shell_commands.c:1615-1640`) or a MATERIALIZE effect (`uishell_sidebar.c:1470-1480`) | — | `CFG_ID`, assigned on load and **never saved**. It is passed to Andamento as `AndamentoWorkspace.id` (`andamento_observe`, `:1301-1325`), `andamento_complete(…, workspace->id)` (`:1535`), `andamento_content_*` (`uishell_managed_content.c:37-104`) and the `tab` patch target (`:1240-1244`) | One of the "three identities". |
| `sidebar_entity_kind`, `sidebar_entity_id` | the MATERIALIZE effect, once | **Workspace (logical)**: the Workspace Subject | | The durable link to the provider entity. On launch, `uishell_sidebar_restore` re-binds it (`:1540-1575`). |
| `local_entity` (GUID) | made lazily for a subjectless workspace (`uishell_sidebar.c:836-848`) | **Workspace (logical)** identity | | The second identity: published as `.workspace/<guid>`. |
| `label` | creation (effect name or "Workspace N"), then rename | Workspace (logical), or Overlay when renamed | | |
| `lives_in` (local group id), `lives_with` (project) | dragging a row to a group or project (`uishell_local_groups.c:282, 411`) | **Dashboard** (grouping) | `row_drag_workspace` (CFG_ID) | It is stored on the workspace but is a grouping fact. |
| `subject` | — | Workspace (logical) | | Read by docking to decide whether a View needing a subject may dock (`shell_docking.c:82`). |
| `theme` | user | Presentation | | Workspace mood (ADR-0009). Not shareable as it is. |
| `panels` tree (`selected`, numeric split weights, `split_x`, `tabs_on_bottom`, tabs) | provider populate (`uishell_sidebar_populate`, `:1396-1430`), then every user dock edit | Suggested Layout at creation, then **Workspace Overlay** (structure) + **Presentation** (weights, `selected`, `tabs_on_bottom`) | `RD_WindowState.active_panel_id` | The saved tree mixes the provider baseline and the user's edits; nothing tells them apart. |
| `detached_workspace` (renamed node) | closing a subject workspace with detach (`shell_commands.c:1752-1757`) | Workspace Overlay (retained) | | Kept for ever (TODO #159). It is reattached on the next MATERIALIZE of the same subject. |

### 2.5 Tabs and Views (children of a panel)

| Key | Writer | Layer | Runtime alongside | Notes |
|---|---|---|---|---|
| view kind (`terminal`, `text`, `jackstay`, `sidebar_section`, `workspace_selector`, `binary`, …; `shell_docking.h:104-124`) | dock and new-tab | **View spec** (Workspace logical / Overlay) | `RD_ViewState` keyed by view `CFG_ID` (`shell_core.h:306-330`): scroll, eval view, terminal and Jackstay user data | |
| `terminal`: `command`, `cwd`, `resource_id`, `managed_target`, `session`, `daemon_name` | populate (`uishell_sidebar.c:1416-1426`), managed content (`uishell_managed_content.c:49-53`), attach (`uishell_views.c:2826-2830, 4024-4025, 4211`) | `command`/`cwd`/`resource_id`: view spec. `managed_target`: provider-plan bookkeeping. `session`/`daemon_name`: **runtime binding** saved for reattach | `UIShell_TerminalViewState` (in RD_ViewState); clipboard registry keyed by `clipboard_view_id`/`window_id` (`uishell_views.c:36-50`) | `session` is a Cleat session id: device-local. |
| `terminal`: `hosting`, `hosting_action`, `show_hosting_overlay` | — | runtime value / Presentation | | Schema `@runtime_value`. |
| `jackstay`: `porthole_endpoint`, `porthole_session`, `attach_token`, `fmt` | jackstay view | view spec / runtime binding | `UIShell_JackstayView` list keyed by `CFG_ID` (`uishell_jackstay.c:13-20`) | |
| `sidebar_section`: `section` (region key), `section_collapsed`, `section_hint_pending`, `section_hint_cleanup` | sidebar placement and collapse (`uishell_sidebar.c:1863-1877, 4568-4569`; `shell_docking.c:259-262`) | Presentation | `UIShell_SidebarSection.collapsed` | Section collapse is persisted. **Row** collapse is not (section 5). |
| `label`, `font_size`, `selected` | user / KDL (a section's label is overwritten from KDL each placement, `:4669-4673`) | Presentation | | |
| `text` and `binary`: `expression`, `lang`, `show_line_numbers`, `line_wrapping`, `scroll_to_bottom_on_change`, `auto`, `num_columns`, `auto_columns` | user | view spec + Presentation | | Mostly Leftover from RAD (eval expressions). |
| `query{cmd,input}`, `lister`, `explicit_root`, `autocomplete`, `watch`, `immediate`, `hot` | command palette, in `transient` | Leftover (in process, never saved) | `query_view_id`, `query_last_view_id` | |

### 2.6 Debugger leftovers in shell code

`condition`, `hit_count`, `source_location`, `address_location`, `enabled`,
`hsva`, `conversion_task`, `display_pointer_addresses_before_contents`, `cmd`
on breakpoint-shaped nodes (`shell_core.c:290-380`, `shell_widgets.c:303-325`).
Nothing in Wheelhouse creates these nodes. They are only read.

## 3. CFG_ID-keyed runtime maps

`CFG_ID` is a per-process counter assigned when the tree is parsed, and it is
never written to disk. Every structure below is rebuilt each launch, and any
state it alone holds is lost.

| Map / field | Keyed by | Holds | Lost on restart? |
|---|---|---|---|
| `RD_WindowState` hash (`shell_core.h:620-700`, `958-967`) | window | OS window, UI state, theme cache, **Visible Workspace** (`root_controlled_split_selected_workspace_id`), `active_panel_id` (focus), query state, `UIShell_SidebarState *sidebar` (the Andamento core) | Visible Workspace and focus: yes. |
| `RD_ViewState` hash (`shell_core.h:306-330`, `970-974`) | view/tab | scroll position, eval view, per-view user data (terminal provider and session handles) | Scroll: yes. |
| `UIShell_TerminalViewState` clipboard registry | view and window ids | clipboard ownership | runtime only |
| `UIShell_JackstayView` list | view | stream session, textures | runtime only |
| `UIShell_SidebarState` (`uishell_sidebar.c:105-215`) | window (via RD_WindowState) | hover/detached cards (`saved`), drag/drop/rename/confirm CFG_IDs, `local_published`, topology hashes, `display_restores` | UI transients |
| `UIShell_Regs` (`shell_core.h:186-196`) | — | the command context: window/panel/tab/view/cfg CFG_IDs | per command |
| **Andamento core** | **workspace CFG_ID** as `AndamentoWorkspace.id` and the `tab` patch target value | open/focused state of rows, workdir observations, content plans, effect completions | yes. Wheelhouse re-observes each launch, and Andamento re-derives "open" by matching `sidebar_entity_*`/`.host.*` facts |

## 4. Andamento KDL config and the flotilla subscription

| Item | Where | Writer / when | Layer | Notes |
|---|---|---|---|---|
| KDL config: `version`, `region`, `placement`, `template`, `fragment`, `display-variable`, `visibility` nodes | `data/sidebar/daily-driver.kdl` by default, or `$WHEELHOUSE_ANDAMENTO_CONFIG`. It reaches Wheelhouse as `--andamento_config:` (`tools/daily-driver.py:159, 252`) | Written by hand and checked into the repo. Read once per window by `andamento_create`, and `andamento_configure` when the mode changes (`uishell_sidebar.c:5211, 5300`) | **Dashboard**: grouping projection, sections (regions), display variables and their defaults. Region and section titles too | It is a repo template rather than user data. Nothing records which config a user ran with. |
| `flotilla pm connect --wheelhouse-socket <sock>` | spawned and restarted by `tools/daily-driver.py:172-179`, not by Wheelhouse | the launcher, each run | **Dashboard** (provider subscription), implicit | It subscribes to the whole fleet; there is no per-provider choice. Producers resend every ~10 s with a ~30 s TTL. Nothing is persisted, so what is shown is whatever is live now. |
| Ingress socket | `--andamento_socket:` → `wheelhouse_ingress_start_recorded` (`uishell_main.c:203-228`) | runtime | — | It is the transport, not state. |

Andamento writes nothing to disk.

## 5. State only in Andamento's memory, and what Wheelhouse replays

| State | Where (Andamento) | Survives restart? | Replayed by Wheelhouse? | Layer it belongs to |
|---|---|---|---|---|
| Display-variable values (`persist` controls) | presentation model | no | **yes**: from `window/sidebar_display` (`uishell_sidebar_restore_display`, `uishell_sidebar.c:772-798`) | Dashboard |
| Sibling order per loop key | sidebar model | no | **yes**: from `window/sidebar_order` (`:633-650`) | Dashboard |
| Local `.section`/`.group`/`.ref`/`.workspace` entities | entity store | no | **yes**: republished from `window/sidebar_local` and the workspace nodes on every change (`:1160-1265`) | Dashboard (and Workspace identity for `.workspace`) |
| Open/focused/selected workspace rows | `andamento_observe` items keyed by workspace `CFG_ID` (`:1301-1325`) | no | yes: re-observed each frame when the hash changes | runtime |
| **Row collapse** (`collapsed_placements`, `andamento-core/src/presentation.rs:473`) | presentation model | no | **no**: lost on every restart | Presentation (but grouping-adjacent) |
| **Retained and ended subjects** (`retained_subjects`, `ended_subjects`, `andamento-core/src/state.rs:461-463`; `retained_paths` keyed by u64 workspace id, `sidebar.rs:143`) | state | no | no. `detached_workspace` nodes are the only durable trace on the Wheelhouse side | Workspace Overlay (retained workspaces) |
| Provider facts, pending requests, content plans, caches | state | no | rebuilt from live producers | runtime |

## 6. Summary by layer

- **Dashboard** is split across four places: the KDL template in the repo
  (regions, grouping, display defaults); the launcher's implicit
  `flotilla pm connect` (subscriptions); and, per window in the **user**
  file, `sidebar_display`, `sidebar_order` and `sidebar_local`, plus
  `lives_in`/`lives_with` on each workspace node. Because the per-window keys
  hang off `window`, two windows hold two Dashboards.
- **Workspace (logical)** is the `workspace` node's
  `sidebar_entity_kind`/`sidebar_entity_id` (subject) or `local_entity` (GUID),
  plus `label` and the view specs in its tabs (`terminal command/cwd/resource_id`).
  Each workspace has three identities: the subject ref, the local GUID and the
  unsaved `CFG_ID`, which is all that Andamento sees.
- **Workspace Overlay** is not stored separately. The provider's Suggested
  Layout is written into `panels` once (`uishell_sidebar_populate`), and later
  user edits overwrite it in place. `detached_workspace` is the only retained
  overlay.
- **Presentation State** is everything else in the user file: window geometry,
  sidebar width and collapse, `control_views`, `floating_panels`,
  `section_positions`, `section_collapsed`, split weights, `selected`, themes,
  fonts, keybindings and tweaks. The project file adds only a name, a theme and
  a tab width.
- **Not saved anywhere**: the Visible Workspace and focus (CFG_ID in
  `RD_WindowState`), scroll positions, row collapse, and retained or ended
  subjects.
- **Leftover**: the `project` bucket's debugger keys, `project`-filtered nodes,
  breakpoint-shaped keys, `transient/immediate` query views, text/binary
  expression views, and the fixture outputs.
