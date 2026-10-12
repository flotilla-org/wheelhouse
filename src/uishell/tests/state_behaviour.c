// Compiled by tools/test-state-behaviour.py from the production amalgamation
// with its entry point replaced, linked against real Andamento and Cleat, with
// the headless WM/renderer/font backends. It builds state through production
// commands and sidebar actions, writes the logical state
// (uishell_logical_state.c), restarts, and writes it again; the script
// compares both with goldens. With --state_phase it runs instead as two
// processes, the second a fresh launch against the files the first left
// (entry_point says how), so nothing in memory survives the restart.
//
// A restart is the production one in-process: autosave writes the user file
// and the Dashboard's presentation (rd_autosave), the window state and its
// Andamento core are released as a frame does once their config is gone,
// writing its records into the Dashboard directory, the Dashboard is opened
// as --dashboard opens it (uishell_dashboard_open) when the restart names
// one, the initial load reopens the files (UISHELL_APP_INITIAL_LOAD, which
// repairs layouts), and a fresh core runs the sidebar's first-frame path. Config is edited directly only where no command
// or sidebar action exists; each such place says so.
//
// The fixture's producer is Andamento's patch ABI: the sidebar fixture's facts
// (as --sidebar_subject_fixture), plus what the scenario's producer changes
// later. A restarted producer reports its current state again.
//
// Subscriptions run live: each gets its endpoint, named after
// --andamento_socket, and its connector, a stand-in for `flotilla pm
// connect` (--flotilla_bin, written by the script) that publishes over it.

#define StateCheck(x) do { if(!(x)) { fprintf(stderr, "FAIL state behaviour line %d: %s\n", __LINE__, #x); state_failures++; } } while(0)
global U32 state_failures;
// What the producer reports beyond the fixture: vessel "multi" ended, and
// vessel "chip-v" gone while its workspace is open.
global B32 state_multi_ended, state_chip_gone;
// Role "p/reviewer", once published: a Suggested Layout of four slots, the
// primary terminal, `partner` (a provider facet keeping its previous
// instance), `watch` (one asking first) and `log` (a local recipe), which it
// arranges side by side. Each facet's target is "<slot>#<n>", running
// "attach <slot> <n>"; 0 publishes nothing.
global U32 state_reviewer_partner, state_reviewer_watch;
// The reviewer's Suggested Layout version: 2 adds slot `notes` beside
// `log`; 3 gives `log` other content and its panel twice the room; 4 drops
// `watch`.
global U32 state_reviewer_version = 1;

internal String8
state_json_list(Arena *arena, String8 key, char **values, U64 count)
{
  String8List parts = {0};
  for(U64 i = 0; i < count; i++) { str8_list_pushf(arena, &parts, "%s\"%s\"", i ? "," : "", values[i]); }
  return push_str8f(arena, "\"%S\":{\"value\":{\"type\":\"string-list\",\"value\":[%S]}}", key, str8_list_join(arena, &parts, 0));
}

// A provider facet slot of the reviewer's layout, at target `n`.
internal String8
state_reviewer_facet(Arena *arena, char *slot, char *rebind, U32 n)
{
  String8 prefix = push_str8f(arena, "layout.slot.%s.", slot);
  String8 facts[] =
  {
    uishell_sidebar_json_fact_ref(arena, push_str8f(arena, "%Sentity", prefix), str8_lit("vessel"), push_str8f(arena, "%s-v", slot)),
    uishell_sidebar_json_fact_text(arena, push_str8f(arena, "%Sfacet", prefix), str8_lit("terminal")),
    uishell_sidebar_json_fact_text(arena, push_str8f(arena, "%Spresentation", prefix), str8_lit("terminal")),
    uishell_sidebar_json_fact_text(arena, push_str8f(arena, "%Srebind", prefix), str8_cstring(rebind)),
    uishell_sidebar_json_fact_text(arena, push_str8f(arena, "%Sstate", prefix), str8_lit("ready")),
    uishell_sidebar_json_fact_text(arena, push_str8f(arena, "%Starget", prefix), push_str8f(arena, "%s#%u", slot, n)),
    uishell_sidebar_json_fact_text(arena, push_str8f(arena, "%Skind", prefix), str8_lit("command")),
    uishell_sidebar_json_fact_text(arena, push_str8f(arena, "%Scommand", prefix), push_str8f(arena, "attach %s %u", slot, n)),
  };
  StringJoin join = {.sep = str8_lit(",")};
  String8List list = {0};
  for(U64 i = 0; i < ArrayCount(facts); i++) { str8_list_push(arena, &list, facts[i]); }
  return str8_list_join(arena, &list, &join);
}

internal void
state_producer(UIShell_SidebarState *state)
{
  if(state_reviewer_partner)
  {
    Temp scratch = scratch_begin(0, 0);
    Arena *arena = scratch.arena;
    B32 watch = state_reviewer_version < 4;
    char *slots[5] = {0}, *children[] = {"agents", "side"}, *agents[] = {"primary", "partner", "watch"}, *side[] = {"log", "notes"};
    U64 slot_count = 0, side_count = state_reviewer_version >= 2 ? 2 : 1;
    slots[slot_count++] = "primary";
    slots[slot_count++] = "partner";
    if(watch) { slots[slot_count++] = "watch"; }
    slots[slot_count++] = "log";
    if(state_reviewer_version >= 2) { slots[slot_count++] = "notes"; }
    String8 facts[] =
    {
      uishell_sidebar_json_fact_text(arena, str8_lit("flotilla.project"), str8_lit("p")),
      uishell_sidebar_json_fact_text(arena, str8_lit("display.label"), str8_lit("reviewer")),
      uishell_sidebar_json_fact_text(arena, str8_lit("flotilla.role"), str8_lit("p/reviewer")),
      uishell_sidebar_json_fact_text(arena, str8_lit("flotilla.role.name"), str8_lit("reviewer")),
      uishell_sidebar_json_fact_text(arena, str8_lit("status.state"), str8_lit("running")),
      str8_lit("\"status.attention\":{\"value\":{\"type\":\"bool\",\"value\":true}}"),
      uishell_sidebar_json_fact_text(arena, str8_lit("workspace.primary.state"), str8_lit("ready")),
      uishell_sidebar_json_fact_text(arena, str8_lit("workspace.primary.target"), str8_lit("vessel:reviewer-v")),
      uishell_sidebar_json_fact_text(arena, str8_lit("action.primary.recipe"), str8_lit("exec /bin/sh")),
      uishell_sidebar_json_fact_text(arena, str8_lit("layout.version"), push_str8f(arena, "%u", state_reviewer_version)),
      state_json_list(arena, str8_lit("layout.slots"), slots, slot_count),
      state_reviewer_facet(arena, "partner", "keep-previous", state_reviewer_partner),
      watch ? state_reviewer_facet(arena, "watch", "ask", state_reviewer_watch) : str8_zero(),
      uishell_sidebar_json_fact_text(arena, str8_lit("layout.slot.log.kind"), str8_lit("command")),
      uishell_sidebar_json_fact_text(arena, str8_lit("layout.slot.log.command"),
                                     state_reviewer_version >= 3 ? str8_lit("tail -f test.log") : str8_lit("tail -f build.log")),
      // Facts for a slot the layout doesn't list are an error.
      state_reviewer_version >= 2 ? uishell_sidebar_json_fact_text(arena, str8_lit("layout.slot.notes.kind"), str8_lit("command")) : str8_zero(),
      state_reviewer_version >= 2 ? uishell_sidebar_json_fact_text(arena, str8_lit("layout.slot.notes.command"), str8_lit("less NOTES.md")) : str8_zero(),
      push_str8f(arena, "\"layout.panel.side.weight\":{\"value\":{\"type\":\"integer\",\"value\":%u}}", state_reviewer_version >= 3 ? 2 : 1),
      uishell_sidebar_json_fact_text(arena, str8_lit("layout.root"), str8_lit("main")),
      uishell_sidebar_json_fact_text(arena, str8_lit("layout.panel.main.axis"), str8_lit("row")),
      state_json_list(arena, str8_lit("layout.panel.main.children"), children, ArrayCount(children)),
      state_json_list(arena, str8_lit("layout.panel.agents.slots"), agents, watch ? 3 : 2),
      uishell_sidebar_json_fact_text(arena, str8_lit("layout.panel.agents.selected"), str8_lit("primary")),
      state_json_list(arena, str8_lit("layout.panel.side.slots"), side, side_count),
    };
    StringJoin join = {.sep = str8_lit(",")};
    String8List list = {0};
    for(U64 i = 0; i < ArrayCount(facts); i++) { if(facts[i].size) { str8_list_push(arena, &list, facts[i]); } }
    String8 patch = push_str8f(arena, "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"role\",\"id\":\"p/reviewer\"}},"
                                      "\"source_id\":\"fixture\",\"set\":{%S},\"unset\":[%s]}", str8_list_join(arena, &list, &join),
                               // A slot dropped from the layout goes with its facts.
                               watch ? "" : "\"layout.slot.watch.entity\",\"layout.slot.watch.facet\",\"layout.slot.watch.presentation\","
                                            "\"layout.slot.watch.rebind\",\"layout.slot.watch.state\",\"layout.slot.watch.target\","
                                            "\"layout.slot.watch.kind\",\"layout.slot.watch.command\"");
    char *error = 0;
    StateCheck(uishell_sidebar_result(state, andamento_apply_patch_json(state->core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(patch), &error), error));
    scratch_end(scratch);
  }
  if(state_multi_ended)
  {
    AndamentoFact facts[2] = {0};
    facts[0].key = uishell_sidebar_text(str8_lit("flotilla.convoy.phase"));
    facts[0].kind = ANDAMENTO_FACT_TEXT;
    facts[0].text = uishell_sidebar_text(str8_lit("landed"));
    facts[1].key = uishell_sidebar_text(str8_lit("status.state"));
    facts[1].kind = ANDAMENTO_FACT_TEXT;
    facts[1].text = uishell_sidebar_text(str8_lit("ended"));
    char *error = 0;
    StateCheck(uishell_sidebar_result(state, andamento_apply_entity(state->core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(str8_lit("vessel")),
      uishell_sidebar_text(str8_lit("multi")), uishell_sidebar_text(str8_lit("fixture")), facts, ArrayCount(facts), &error), error));
  }
  if(state_chip_gone)
  {
    String8 retract = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"vessel\",\"id\":\"chip-v\"}},\"source_id\":\"fixture\",\"set\":{},"
      "\"unset\":[\"flotilla.project\",\"flotilla.convoy\",\"display.label\",\"status.state\",\"presentation.icon\",\"action.primary.recipe\"]}");
    char *error = 0;
    StateCheck(uishell_sidebar_result(state, andamento_apply_patch_json(state->core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(retract), &error), error));
  }
}

// Runs queued commands, and those they queue, through the registered command
// packs as rd_frame's top-level command loop does, then repairs layouts as it
// does after the loop.
internal void
state_pump(void)
{
  UIShell_Cmd *cmd = 0;
  for(;uishell_next_cmd(&cmd);) UIShell_RegsScope()
  {
    MemoryCopyStruct(uishell_regs(), cmd->regs);
    for(UIShell_CmdPack *pack = rd_state->first_cmd_pack; pack != 0; pack = pack->next)
    { if(pack->dispatch != 0 && pack->dispatch(cmd->name)) { break; } }
  }
  arena_clear(rd_state->cmds_arenas[0]);
  MemoryZeroStruct(&rd_state->cmds[0]);
  rd_dock_restore_layouts();
}

// What a frame does for the sidebar between commands: the workspace path
// observes the window's workspaces, and the sidebar's layout reconciles
// its docked sections against the snapshot. Then, as a frame does before
// Views build, the states of Views that left a live place are released.
internal void
state_frame(RD_WindowState *ws)
{
  state_pump();
  Temp scratch = scratch_begin(0, 0);
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, cfg_node_from_id(ws->cfg_id));
  uishell_sidebar_observe(ws->sidebar, &split);
  uishell_sidebar_refresh(ws->sidebar);
  uishell_sidebar_dock_layout(&split);
  scratch_end(scratch);
  rd_dock_restore_layouts();
  rd_view_states_release_unowned();
}

// Set: a window's first frame draws its sidebar before anything else
// observes the window, as a frame does when the title bar has no room for
// the workspace path (whose build otherwise observes first).
global B32 state_draw_sidebar;

// The sidebar's draw, as the window frame makes it: its panel tree, with each
// docked section's View rendered in it. A section View's render observes the
// window, and so syncs the arrangement stores, while the tree is drawn.
internal void
state_sidebar_draw(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, cfg_node_from_id(ws->cfg_id));
  UI_IconInfo icons = {0}; UI_AnimationInfo animation = {0}; UI_EventList events = {0};
  fnt_frame();
  dr_begin_frame(rd_font_from_slot(RD_FontSlot_Icons));
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  UIShell_RegsScope(.window = ws->cfg_id) UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11)
  { uishell_control_surface_ui(r2f32p(0, 0, 320, 600), &split); }
  ui_end_build();
  scratch_end(scratch);
}

// A whole window frame, as rd_frame runs one. The theme it makes lives in
// its scratch; the harness's own builds keep theirs.
internal void
state_window_frame(RD_WindowState *ws)
{
  state_pump();
  UI_Theme *theme = ws->theme;
  fnt_frame();
  dr_begin_frame(rd_font_from_slot(RD_FontSlot_Icons));
  UIShell_RegsScope(.window = ws->cfg_id) { rd_window_frame(); }
  ws->theme = theme;
  rd_dock_restore_layouts();
}

// A window's first frame: a new window state and Andamento core (fixture
// facts, and the window's records imported), what the producer reports now,
// then the sidebar's first build: observe and reconcile docked sections.
internal RD_WindowState *
state_open_window(UI_Theme *theme)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_NodePtrList windows = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("window"));
  StateCheck(windows.count == 1);
  CFG_Node *window = windows.first->v;
  RD_WindowState *ws = rd_window_state_from_cfg(window);
  ws->theme = theme;
  ui_select_state(ws->ui);
  UI_IconInfo icons = {0}; UI_AnimationInfo animation = {0}; UI_EventList events = {0};
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_end_build();
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  StateCheck(state->core != 0 && state->snapshot != 0);
  state_producer(state);
  if(state_draw_sidebar)
  {
    state_sidebar_draw(ws);
    state_window_frame(ws);
  }
  else
  {
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
  }
  scratch_end(scratch);
  state_frame(ws);
  return ws;
}

// Quitting, as the exit command does: Presentation State saved and the
// user and project files written; then the frame that sees the quit
// releases the window state and its Andamento core, writing its records.
internal void
state_quit(RD_WindowState *ws)
{
  uishell_cmd("exit");
  state_pump();
  StateCheck(rd_state->quit);
  rd_window_state_release(ws);
}

// The production restart: autosave writes the user file and presentation,
// the window state and its Andamento core are released (writing its
// records), the process opens `dashboard` if given (a launch with
// --dashboard:<dashboard>), else the same Dashboard, and the initial load
// reopens the files for a new window state and core.
internal RD_WindowState *
state_restart(RD_WindowState *ws, UI_Theme *theme, String8 dashboard)
{
  Temp scratch = scratch_begin(0, 0);
  // Nothing may have changed since the last autosave.
  rd_autosave();
  state_pump();
  rd_window_state_release(ws);
  if(dashboard.size) { uishell_dashboard_open(dashboard, str8_chop_last_slash(rd_state->user_path)); }
  String8 user_path = push_str8_copy(scratch.arena, rd_state->user_path);
  String8 project_path = push_str8_copy(scratch.arena, rd_state->project_path);
  UISHELL_APP_INITIAL_LOAD(user_path, project_path);
  state_pump();
  ws = state_open_window(theme);
  scratch_end(scratch);
  return ws;
}

internal B32
state_find(UIShell_SidebarState *state, String8 kind, String8 id, AndamentoNode *out)
{
  for(U64 i = 0; state->snapshot && i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    andamento_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && str8_match(uishell_sidebar_string(node.entity.kind), kind, 0) &&
       (!id.size || str8_match(uishell_sidebar_string(node.entity.id), id, 0)))
    { *out = node; return 1; }
  }
  return 0;
}

// The row of a local workspace, by its label.
internal U64
state_find_local(UIShell_SidebarState *state, String8 label)
{
  for(U64 i = 0; state->snapshot && i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    andamento_snapshot_node(state->snapshot, i, &node);
    if(str8_match(uishell_sidebar_string(node.entity.kind), str8_lit(".workspace"), 0) &&
       str8_match(uishell_sidebar_string(node.label), label, 0)) { return i; }
  }
  return ANDAMENTO_NONE;
}

// The key of the section titled `title`.
internal String8
state_section_key(Arena *arena, UIShell_SidebarState *state, String8 title)
{
  for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    uishell_sidebar_node_at(state, i, &node);
    if(node.is_section && str8_match(uishell_logical_section_title(state, node), title, 0))
    { return push_str8_copy(arena, uishell_sidebar_string(node.key)); }
  }
  return str8_zero();
}

// A click on a sidebar control, as the sidebar's render takes one.
internal void
state_click(RD_WindowState *ws, size_t action)
{
  StateCheck(action != ANDAMENTO_NONE);
  Temp scratch = scratch_begin(0, 0);
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, cfg_node_from_id(ws->cfg_id));
  uishell_sidebar_perform(ws->sidebar, &split, action);
  scratch_end(scratch);
  state_frame(ws);
}

// Opens a subject's workspace from its row, as clicking it does.
internal CFG_Node *
state_open_subject(RD_WindowState *ws, String8 kind, String8 id)
{
  AndamentoNode node = {0};
  StateCheck(state_find(ws->sidebar, kind, id, &node) && node.state == ANDAMENTO_LATENT && node.openable);
  state_click(ws, node.activate);
  CFG_Node *workspace = cfg_node_from_id(ws->root_controlled_split_selected_workspace_id);
  StateCheck(str8_match(cfg_node_child_from_string(workspace, str8_lit("sidebar_entity_id"))->first->string, id, 0));
  return workspace;
}

// Rows of entity `kind`/`id` from `provider`, and (optional) whether one of
// them is stale.
internal U64
state_count_from(UIShell_SidebarState *state, String8 kind, String8 id, String8 provider, B32 *stale)
{
  U64 count = 0;
  for(U64 i = 0; state->snapshot && i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    andamento_snapshot_node(state->snapshot, i, &node);
    if(node.is_section || !str8_match(uishell_sidebar_string(node.entity.kind), kind, 0) ||
       !str8_match(uishell_sidebar_string(node.entity.id), id, 0) ||
       !str8_match(uishell_sidebar_string(node.entity.provider), provider, 0)) { continue; }
    count++;
    if(stale && node.stale) { *stale = 1; }
  }
  return count;
}

// Frames, with live ingress polled before each as a frame does, until `cond`
// holds or ten seconds pass.
#define StateWaitFor(ws, cond) do \
{ \
  U64 deadline_ = wheelhouse_ingress_now_ms() + 10000; \
  for(;;) \
  { \
    uishell_sidebar_poll_live(); \
    state_frame(ws); \
    if(cond) { break; } \
    if(wheelhouse_ingress_now_ms() > deadline_) { StateCheck(cond); break; } \
    sleep_ms(20); \
  } \
} while(0)

internal RD_PanelTree
state_panels(Arena *arena, CFG_Node *workspace)
{
  return uishell_workspace_mount_from_owner_cfg(arena, rd_window_from_cfg(workspace), workspace).panel_tree;
}

internal String8
state_write(String8 dir, char *name)
{
  Temp scratch = scratch_begin(0, 0);
  U64 gen = cfg_change_gen();
  String8 text = uishell_logical_state_text(scratch.arena);
  // Writing the state down must not change it.
  StateCheck(cfg_change_gen() == gen);
  String8 path = push_str8f(scratch.arena, "%S/%s", dir, name);
  StateCheck(write_data_to_file_path(path, text));
  scratch_end(scratch);
  return text;
}

// Each workspace's Workspace ID, one "name id" line each: subject workspaces
// named by subject, local ones by label, the window's own layout as such.
// IDs are not in the logical state, so the restart check compares these.
internal String8List
state_workspace_ids(Arena *arena)
{
  String8List result = {0};
  CFG_NodePtrList windows = cfg_node_top_level_list_from_string(arena, str8_lit("window"));
  for(CFG_NodePtrNode *n = windows.first; n; n = n->next)
  {
    CFG_Node *window = n->v;
    // The window's own Window ID is kept the same way.
    String8 window_id = cfg_node_child_from_string(window, str8_lit("window_id"))->first->string;
    UIShell_WindowId parsed_window_id = {0};
    StateCheck(uishell_workspace_id_from_string(window_id, &parsed_window_id) && (parsed_window_id.v[6] >> 4) == 7);
    str8_list_pushf(arena, &result, "(window ID) %S", window_id);
    if(cfg_node_child_from_string(window, str8_lit("workspace_id")) != &cfg_nil_node)
    { str8_list_pushf(arena, &result, "(window) %S", cfg_node_child_from_string(window, str8_lit("workspace_id"))->first->string); }
    for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next)
    {
      if(!str8_match(c->string, str8_lit("workspace"), 0) && !str8_match(c->string, str8_lit("detached_workspace"), 0)) { continue; }
      String8 name = uishell_workspace_cfg_has_subject(c) ?
        push_str8f(arena, "%S/%S", cfg_node_child_from_string(c, str8_lit("sidebar_entity_kind"))->first->string,
                   cfg_node_child_from_string(c, str8_lit("sidebar_entity_id"))->first->string) :
        rd_label_from_cfg(c);
      String8 id = cfg_node_child_from_string(c, str8_lit("workspace_id"))->first->string;
      UIShell_WorkspaceId parsed = {0};
      // Every saved workspace has a UUIDv7, and no two share one.
      StateCheck(uishell_workspace_id_from_string(id, &parsed) && (parsed.v[6] >> 4) == 7 && (parsed.v[8] >> 6) == 2);
      for(String8Node *seen = result.first; seen; seen = seen->next)
      { StateCheck(str8_find_needle(seen->string, 0, id, 0) == seen->string.size); }
      str8_list_pushf(arena, &result, "%S %S", name, id);
    }
  }
  return result;
}

// Whether a saved file holds a workspace's panel tree: since #309 Andamento's
// workspace records hold arrangements, and the presentation file only this
// device's part of them.
internal B32
state_saves_panel_tree(String8 text)
{
  for(U64 start = 0; start < text.size;)
  {
    U64 end = str8_find_needle(text, start, str8_lit("\n"), 0);
    String8 line = str8_skip_chop_whitespace(str8_substr(text, r1u64(start, end)));
    if(str8_match(str8_prefix(line, 7), str8_lit("panels:"), 0)) { return 1; }
    start = end+1;
  }
  return 0;
}

// The window's workspaces as the logical state shows them.
internal String8
state_workspaces_text(Arena *arena)
{
  String8 text = uishell_logical_state_text(arena);
  U64 start = str8_find_needle(text, 0, str8_lit("\n  workspaces\n"), 0);
  U64 end = str8_find_needle(text, start, str8_lit("\n  sidebar\n"), 0);
  return str8_substr(text, r1u64(start, end));
}

internal CFG_Node *
state_workspace_labelled(CFG_Node *window, String8 label)
{
  for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next)
  {
    if(str8_match(c->string, str8_lit("workspace"), 0) && str8_match(rd_label_from_cfg(c), label, 0)) { return c; }
  }
  return &cfg_nil_node;
}

// Every workspace of the window is committed, and its panel tree is the
// document Andamento keeps: the same panels, weights, tabs by slot key and
// Selected Views, at the generation the window last saw. Andamento owns it
// once a commit changed its structure; one that changed only weights or
// selections leaves it following the provider (ADR 0013).
// Whether two documents are the same: the same panels, in order, with the
// same parents, tabs and Selected Views; weights within rounding, roots' not
// at all.
internal B32
state_same_doc(AndamentoArrangementDoc const *a, AndamentoArrangementDoc const *b)
{
  B32 same = a->panel_count == b->panel_count;
  for(U64 i = 0; same && i < a->panel_count; i++)
  {
    AndamentoDocPanel const *p = &a->panels[i], *q = &b->panels[i];
    B32 root = p->parent == ANDAMENTO_NONE;
    same = (str8_match(uishell_sidebar_string(p->id), uishell_sidebar_string(q->id), 0) && p->parent == q->parent && p->kind == q->kind &&
            (p->kind != ANDAMENTO_PANEL_SPLIT || p->axis == q->axis) && (root || abs_f32((F32)(p->weight-q->weight)) < 1e-6f) &&
            p->tab_count == q->tab_count && p->selected == q->selected);
    for(U64 t = 0; same && t < p->tab_count; t++) { same = str8_match(uishell_sidebar_string(p->tabs[t]), uishell_sidebar_string(q->tabs[t]), 0); }
  }
  return same;
}

internal void
state_check_committed(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *state = ws->sidebar;
  CFG_NodePtrList owners = uishell_store_owners(scratch.arena, cfg_node_from_id(ws->cfg_id));
  StateCheck(owners.count != 0);
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next)
  {
    CFG_Node *owner = n->v;
    UIShell_StoreDoc doc = uishell_store_doc(scratch.arena, owner);
    AndamentoWorkspaceArrangement *stored = andamento_arrangement_get(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner)), 0);
    AndamentoArrangementDoc ours = {doc.panels, doc.panel_count};
    B32 same = stored && state_same_doc(&stored->doc, &ours) &&
      str8_match(uishell_store_setting(owner, str8_lit("arrangement_generation")), push_str8f(scratch.arena, "%I64u", stored->generation), 0);
    for(U64 i = 0; same && stored && i < stored->note_count; i++) { same = stored->notes[i].kind != ANDAMENTO_NOTE_GONE; }
    if(!same) { fprintf(stderr, "Workspace %.*s is not the arrangement Andamento keeps\n", str8_varg(rd_label_from_cfg(owner))); state_failures++; }
    andamento_arrangement_free(stored);
  }
  scratch_end(scratch);
}

// The window's sidebar is committed, and is the document Andamento keeps:
// the same panels, tabs by section key, Selected Views and weights, at the
// generation the window last saw.
internal void
state_check_sidebar_committed(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *state = ws->sidebar;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  UIShell_SidebarDoc doc = uishell_sidebar_store_doc(scratch.arena, state, window, 0);
  AndamentoSidebarArrangement *stored = andamento_sidebar_arrangement_get(state->core, 0);
  AndamentoSidebarDoc ours = uishell_sidebar_store_andamento_doc(&doc);
  B32 same = stored && stored->owned && state_same_doc(&stored->doc.dock, &ours.dock) && state_same_doc(&stored->doc.floating, &ours.floating) &&
    str8_match(uishell_store_setting(window, str8_lit("sidebar_generation")), push_str8f(scratch.arena, "%I64u", stored->generation), 0);
  if(!same) { fprintf(stderr, "The sidebar is not the arrangement Andamento keeps\n"); state_failures++; }
  andamento_sidebar_arrangement_free(stored);
  scratch_end(scratch);
}

// Whether the window's docked sidebar has a leaf with no tabs.
internal B32
state_sidebar_has_empty_leaf(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *sidebar = rd_arrangement_from_owner(scratch.arena, window, RD_DOCK_SIDEBAR_ROOT);
  B32 empty = 0;
  for(RD_ArrangementPanel *p = sidebar->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(sidebar->root, p))
  { empty |= p != sidebar->root && p->first == &rd_nil_arrangement_panel && p->tab_count == 0; }
  scratch_end(scratch);
  return empty;
}

// The View of Slot `key` in `workspace`, or nil.
internal CFG_Node *
state_slot_view(CFG_Node *workspace, String8 key)
{
  CFG_Node *panels = cfg_node_child_from_string(workspace, str8_lit("panels"));
  for(CFG_Node *n = panels; n != &cfg_nil_node; n = cfg_node_rec__depth_first(panels, n).next)
  {
    if(str8_match(n->string, str8_lit("slot"), 0) && str8_match(n->first->string, key, 0)) { return n->parent; }
  }
  return &cfg_nil_node;
}

// The previous instance kept beside Slot `key`'s View, or nil.
internal CFG_Node *
state_previous_view(CFG_Node *workspace, String8 key)
{
  CFG_Node *panels = cfg_node_child_from_string(workspace, str8_lit("panels"));
  for(CFG_Node *n = panels; n != &cfg_nil_node; n = cfg_node_rec__depth_first(panels, n).next)
  {
    if(str8_match(n->string, str8_lit("previous_of"), 0) && str8_match(n->first->string, key, 0)) { return n->parent; }
  }
  return &cfg_nil_node;
}

// Slot `key`'s plan for what `view` applied: its state, and whether it
// reports a previous instance.
internal uint32_t
state_slot_plan(UIShell_SidebarState *state, CFG_Node *workspace, String8 key, CFG_Node *view, B32 *has_previous)
{
  AndamentoSlotContent *plan = andamento_slot_plan(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(workspace)),
    uishell_sidebar_text(key), uishell_sidebar_text(uishell_store_setting(view, str8_lit("applied"))), 0);
  uint32_t result = plan ? plan->state : ANDAMENTO_CONTENT_UNAVAILABLE;
  if(has_previous) { *has_previous = plan && plan->has_previous; }
  andamento_slot_plan_free(plan);
  return result;
}

// The first notice of `view` offering `action`, if any.
internal B32
state_offers(CFG_Node *view, UIShell_SlotAction action)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SlotNotice notices[8];
  U64 count = uishell_store_notices(scratch.arena, view, notices, ArrayCount(notices));
  B32 found = 0;
  for(U64 i = 0; i < count && !found; i++) { found = notices[i].actions[0] == action || notices[i].actions[1] == action; }
  scratch_end(scratch);
  return found;
}

// The sidebar's arrangement as the logical state shows it.
internal String8
state_sidebar_text(Arena *arena)
{
  String8 text = uishell_logical_state_text(arena);
  U64 start = str8_find_needle(text, 0, str8_lit("\n  sidebar arrangement\n"), 0);
  U64 end = str8_find_needle(text, start, str8_lit("\npresentation\n"), 0);
  return str8_substr(text, r1u64(start, end));
}

// A terminal View's first build (WH_VIEW_UI_FUNCTION_DEF(terminal)),
// headless: a terminal whose Target Resolution names a session re-attaches
// to it, which needs a Cleat daemon, so the attach is recorded instead; any
// other launches in an in-process Cleat session. Released runtimes are
// counted.
global U32 state_runtimes_released;
global U8 state_attached_buffer[64];
global String8 state_attached;

internal void
state_terminal_release(void *data)
{
  state_runtimes_released++;
  uishell_terminal_runtime_release(data);
}

internal UIShell_TerminalViewState *
state_terminal(CFG_Node *view)
{
  Temp scratch = scratch_begin(0, 0);
  WH_Renderer *renderer = wh_renderer_from_name(view->string);
  RD_ViewState *vs = rd_view_state_from_cfg(view);
  UIShell_TerminalViewState *tv = rd_view_user_data(vs, renderer->state_size);
  if(!tv->initialized)
  {
    WH_ViewContext ctx = wh_view_context_from_cfg(scratch.arena, view, renderer, tv, r2f32p(0, 0, 640, 384));
    tv->initialized = 1;
    vs->release_user_data = state_terminal_release;
    if(ctx.resolution.session.size != 0)
    {
      tv->daemon_backend = 1;
      state_attached = str8(state_attached_buffer, Min(ctx.resolution.session.size, sizeof(state_attached_buffer)));
      MemoryCopy(state_attached_buffer, ctx.resolution.session.str, state_attached.size);
    }
    else
    {
      cleat_provider_desc pd = {.abi_version = CLEAT_PROVIDER_ABI_VERSION, .backend = CLEAT_PROVIDER_BACKEND_IN_PROCESS};
      tv->provider = cleat_provider_open(&pd);
      String8 command = str8_lit("/bin/sh -c 'sleep 60'");
      cleat_session_desc sd = {.cols = 80, .rows = 24, .cell_width_px = 8, .cell_height_px = 16,
        .vt_engine = CLEAT_PROVIDER_VT_PASSTHROUGH, .command = command.str, .command_len = command.size};
      tv->session = tv->provider ? cleat_session_create(tv->provider, &sd) : 0;
    }
  }
  scratch_end(scratch);
  return tv;
}

// Whether a launched terminal's session is running in-process.
internal B32
state_terminal_running(UIShell_TerminalViewState *tv)
{
  cleat_str hosting = {0};
  return tv->session != 0 && cleat_session_hosting(tv->session, &hosting) &&
    str8_match(str8((U8 *)hosting.ptr, hosting.len), str8_lit("in_process"), 0);
}

internal B32
state_has_view_state(CFG_ID view)
{
  for(U64 slot = 0; slot < rd_state->view_state_slots_count; slot++)
  {
    for(RD_ViewState *vs = rd_state->view_state_slots[slot].first; vs; vs = vs->hash_next)
    { if(vs->cfg_id == view) { return 1; } }
  }
  return 0;
}

// Opens vessel `v`'s workspace from its row, kept or not.
internal CFG_Node *
state_open_v(RD_WindowState *ws)
{
  AndamentoNode node = {0};
  StateCheck(state_find(ws->sidebar, str8_lit("vessel"), str8_lit("v"), &node) && node.openable);
  state_click(ws, node.activate);
  CFG_Node *workspace = cfg_node_from_id(ws->root_controlled_split_selected_workspace_id);
  StateCheck(str8_match(workspace->string, str8_lit("workspace"), 0) &&
             str8_match(cfg_node_child_from_string(workspace, str8_lit("sidebar_entity_id"))->first->string, str8_lit("v"), 0));
  return workspace;
}

internal void
entry_point(CmdLine *cmdline)
{
  String8 dir = cmd_line_string(cmdline, str8_lit("state_dir"));
  if(!dir.size) { fprintf(stderr, "state behaviour needs --state_dir:DIR --user:FILE --dashboard:DIR\n"); abort_self(2); }
  // As --sidebar_subject_fixture: the daily-driver sidebar with fixture facts.
  uishell_sidebar_fixture = uishell_sidebar_subject_fixture = 1;
  wm_init(); fp_init(); r_init(cmdline); fnt_init(); rd_init(cmdline);
  // Every registered Renderer except visualizers renders, and so do docked
  // sections when the sidebar is drawn (state_sidebar_draw).
  rd_state->headless_views = 1;
  rd_state->headless_sections = 1;
  e_select_cache(rd_state->eval_cache);
  E_BaseCtx base_ctx = {.address_arch = Arch_CURRENT, .space_gen = rd_eval_space_gen,
                       .space_read = rd_eval_space_read, .space_write = rd_eval_space_write}; e_select_base_ctx(&base_ctx);
  // Settings evaluate through the production raw lens, as in the frame setup.
  E_String2ExprMap macros = e_string2expr_map_make(rd_state->arena, 16);
  E_Expr *raw = e_push_expr(rd_state->arena, E_ExprKind_LeafOffset, r1u64(0, 0));
  raw->type_key = e_type_key_cons(.kind = E_TypeKind_LensSpec, .name = str8_lit("raw"));
  e_string2expr_map_insert(rd_state->arena, &macros, str8_lit("raw"), raw);
  E_IRCtx ir_ctx = {.macro_map = &macros}; e_select_ir_ctx(&ir_ctx);
  E_InterpretCtx interpret_ctx = {0}; e_select_interpret_ctx(&interpret_ctx);
  Temp scratch = scratch_begin(0, 0);
  UI_Theme theme = {0};

  // --state_phase:save runs the scenario to its restart, saves as the
  // process does when it quits, and exits; --state_phase:restart is the
  // next launch, a fresh process against the same files, which writes the
  // state it opens with (fresh_restart.txt) and checks each workspace kept
  // the ID the save wrote (workspace_ids.txt).
  String8 phase = cmd_line_string(cmdline, str8_lit("state_phase"));
  String8 ids_path = push_str8f(scratch.arena, "%S/workspace_ids.txt", dir);
  StringJoin lines = {.sep = str8_lit("\n")};
  if(str8_match(phase, str8_lit("restart"), 0))
  {
    // The producer, another process, reports what it did when the first
    // one quit.
    state_multi_ended = state_chip_gone = 1;
    state_pump();
    RD_WindowState *ws = state_open_window(&theme);
    state_write(dir, "fresh_restart.txt");
    String8List ids_after = state_workspace_ids(scratch.arena);
    String8 ids = str8_list_join(scratch.arena, &ids_after, &lines);
    String8 saved = data_from_file_path(scratch.arena, ids_path);
    StateCheck(saved.size != 0);
    if(!str8_match(ids, saved, 0))
    { fprintf(stderr, "Workspace IDs changed across a fresh-process restart:\n%.*s\n->\n%.*s\n", str8_varg(saved), str8_varg(ids)); state_failures++; }
    state_quit(ws);
    scratch_end(scratch);
    fprintf(stderr, "State behaviour (fresh-process restart): %u failures\n", state_failures);
    abort_self(state_failures ? 1 : 0);
  }

  //- First launch: rd_init queued the initial load of the (new) --user file.
  state_pump();
  RD_WindowState *ws = state_open_window(&theme);
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  UIShell_SidebarState *state = ws->sidebar;

  //- Local workspaces, one renamed, one with a terminal split beside its
  // editor and the output tab moved in with it.
  uishell_cmd("new_workspace", .window = window->id);
  uishell_cmd("new_workspace", .window = window->id);
  state_frame(ws);
  CFG_NodePtrList locals = cfg_node_child_list_from_string(scratch.arena, window, str8_lit("workspace"));
  StateCheck(locals.count == 2);
  CFG_Node *work = locals.first->v, *notes = locals.last->v;
  // No command renames a workspace; Workspace Settings edits its label in place.
  cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string(notes, str8_lit("label")), str8_lit("Notes"));
  RD_PanelTree tree = state_panels(scratch.arena, work);
  CFG_Node *editor_panel = rd_cfg_from_panel(tree.root->first->address), *output_panel = rd_cfg_from_panel(tree.root->last->address);
  CFG_Node *output = output_panel->first;
  uishell_cmd("build_tab", .window = window->id, .panel = rd_panel_from_cfg(editor_panel), .string = str8_lit("terminal"), .expr = str8_lit("make test"));
  state_frame(ws);
  // A new tab is the panel's last; its node sits among the panel's tabs.
  tree = state_panels(scratch.arena, work);
  CFG_Node *terminal = rd_panel_node_from_id(tree.root, rd_panel_from_cfg(editor_panel).id)->tabs.last->v;
  StateCheck(str8_match(terminal->string, str8_lit("terminal"), 0));
  // A launch directory comes from a provider's recipe or hand editing; no
  // command sets one.
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, terminal, str8_lit("cwd")), str8_lit("/src/wheelhouse"));
  uishell_cmd("split_panel", .window = window->id, .panel = rd_panel_from_cfg(editor_panel), .view = terminal->id,
              .dst_panel = rd_panel_from_cfg(editor_panel), .dir2 = Dir2_Down);
  state_frame(ws);
  CFG_Node *terminal_panel = terminal->parent;
  StateCheck(terminal_panel != editor_panel);
  // Moving the last tab out of a panel closes it.
  uishell_cmd("move_view", .window = window->id, .panel = rd_panel_from_cfg(output_panel), .view = output->id,
              .dst_panel = rd_panel_from_cfg(terminal_panel), .prev_tab = terminal->id);
  state_frame(ws);
  StateCheck(output->parent == terminal_panel);
  // A Jackstay View in the other one.
  CFG_Node *notes_panel = rd_cfg_from_panel(state_panels(scratch.arena, notes).root->first->address);
  uishell_cmd("build_tab", .window = window->id, .panel = rd_panel_from_cfg(notes_panel), .string = str8_lit("jackstay"));
  state_frame(ws);
  tree = state_panels(scratch.arena, notes);
  CFG_Node *jackstay = rd_panel_node_from_id(tree.root, rd_panel_from_cfg(notes_panel).id)->tabs.last->v;
  StateCheck(str8_match(jackstay->string, str8_lit("jackstay"), 0));
  // Its address field saves the endpoint on Connect; no command does.
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, jackstay, str8_lit("source_endpoint")), str8_lit("porthole-demo"));

  //- Subject workspaces opened from their rows: one whose layout the person
  // changes and whose subject then ends, one closed but kept, and one whose
  // subject the producer stops reporting while it is open.
  CFG_Node *multi = state_open_subject(ws, str8_lit("vessel"), str8_lit("multi"));
  tree = state_panels(scratch.arena, multi);
  CFG_Node *tools = tree.root->last->tabs.last->v;
  uishell_cmd("focus_tab", .window = window->id, .panel = rd_panel_from_cfg(tools), .tab = tools->id);
  state_frame(ws);
  CFG_Node *kept = state_open_subject(ws, str8_lit("vessel"), str8_lit("v"));
  uishell_cmd("detach_workspace", .window = window->id, .cfg = kept->id);
  state_frame(ws);
  StateCheck(str8_match(kept->string, str8_lit("detached_workspace"), 0));
  state_open_subject(ws, str8_lit("vessel"), str8_lit("chip-v"));
  state_multi_ended = state_chip_gone = 1;
  state_producer(state);
  state_frame(ws);

  //- Sidebar: a display variable off, a project collapsed, and the
  // Workspaces group's rows reordered.
  {
    UIShell_DisplayControlIterator it = {state->snapshot};
    AndamentoControl control = {0}; AndamentoText name = {0};
    size_t toggle = ANDAMENTO_NONE;
    while(uishell_sidebar_next_persistent_control(&it, &control, &name))
    { if(str8_match(uishell_sidebar_string(name), str8_lit("show-issues"), 0)) { toggle = control.action; break; } }
    state_click(ws, toggle);
    AndamentoNode project = {0};
    StateCheck(state_find(state, str8_lit("project"), str8_lit("p"), &project) && !project.collapsed);
    state_click(ws, project.toggle);
    // A row drop commits the run's whole order once the render ends.
    U64 row = state_find_local(state, str8_lit("Notes"));
    StateCheck(row != ANDAMENTO_NONE);
    String8 loop = push_str8_copy(scratch.arena, uishell_sidebar_loop_key(state->snapshot, row));
    AndamentoEntity *siblings = 0;
    U64 count = uishell_sidebar_siblings(scratch.arena, state->snapshot, loop, &siblings);
    StateCheck(count >= 2);
    AndamentoEntity *order = push_array(scratch.arena, AndamentoEntity, count);
    for(U64 i = 0; i < count; i++) { order[i] = siblings[count-1-i]; }
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_publish(state, &split);
    uishell_sidebar_set_order(state, loop, order, count);
    state_frame(ws);
  }

  //- A section of one's own, with a workspace living in its group and a
  // pinned project.
  {
    CFG_Node *view = uishell_sidebar_new_section(state, window);
    StateCheck(view != &cfg_nil_node);
    String8 section = push_str8_copy(scratch.arena, uishell_sidebar_local_key_id(uishell_sidebar_section_key(view)));
    // Its name field opened; typing a name and Enter applies it.
    uishell_sidebar_local_rename_section(window, section, str8_lit("Builds"));
    state->rename_node = 0;
    UIShell_LocalGroup *group = uishell_sidebar_local_only_group(uishell_sidebar_local_section(window, section));
    StateCheck(group != 0);
    String8 group_id = push_str8_copy(scratch.arena, group->id);
    uishell_sidebar_make(window, (UIShell_MakeAction){UIShell_Make_Workspace, str8_lit("New workspace"), group_id}, str8_lit("Build logs"));
    state_frame(ws);
    // Pin on the project's hover card.
    AndamentoEntity path[] = {uishell_sidebar_entity(uishell_sidebar_host_provider, str8_lit("project"), str8_lit("p"))};
    UIShell_HoverCard card = {.path = path, .depth = 1, .retained_label = str8_lit("Example project")};
    UIShell_LocalPin *pin = uishell_sidebar_card_pin(ws, &card, 0);
    StateCheck(pin && str8_match(pin->group->id, group_id, 0));
    state_frame(ws);
  }

  //- Docked sections: Git closed, Attention collapsed.
  {
    String8 git = state_section_key(scratch.arena, state, str8_lit("Git"));
    CFG_Node *view = uishell_sidebar_region_view(window, git);
    StateCheck(view != &cfg_nil_node);
    CFG_ID git_panel = view->parent->id;
    uishell_cmd("close_tab", .window = window->id, .panel = rd_panel_from_cfg(view), .tab = view->id);
    state_frame(ws);
    StateCheck(uishell_sidebar_region_view(window, git) == &cfg_nil_node);
    // Its panel went with it, and Andamento keeps the sidebar without it
    // (#358).
    StateCheck(cfg_node_from_id(git_panel) == &cfg_nil_node && !state_sidebar_has_empty_leaf(window));
    state_check_sidebar_committed(ws);
    // An empty leaf saved before then (a file from an older build) goes on
    // the next frame, and the commit after drops it too. No command makes
    // one; the config is edited directly.
    {
      CFG_Node *host = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
      cfg_node_new(rd_state->cfg, host, str8_lit("0.1"));
      StateCheck(state_sidebar_has_empty_leaf(window));
      state_frame(ws);
      state_frame(ws);
      StateCheck(!state_sidebar_has_empty_leaf(window));
      state_check_sidebar_committed(ws);
    }
    CFG_Node *attention = uishell_sidebar_region_view(window, state_section_key(scratch.arena, state, str8_lit("Attention")));
    StateCheck(attention != &cfg_nil_node);
    // The section header's disclosure.
    uishell_sidebar_section_set_collapsed(attention, 1);
    state_frame(ws);
    // The sidebar's panel tree, as its render builds and sizes it, sees the
    // collapse: Attention's leaf keeps only its header, and the leaves still
    // fill the sidebar.
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    CFG_Node *root = uishell_sidebar_dock_layout(&split);
    UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, root);
    RD_PanelNode *leaf = &rd_nil_panel_node;
    F32 sum = 0, header_h = 0;
    B32 collapsed = 0;
    UI_IconInfo icons = {0}; UI_AnimationInfo animation = {0}; UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    UI_FontSize(11)
    {
      uishell_sidebar_size_panels(&split, &mount, r2f32p(0, 0, 300, 600));
      for(RD_PanelNode *p = mount.panel_tree.root->first; p != &rd_nil_panel_node; p = p->next)
      {
        sum += p->pct_of_parent;
        if(p->selected_tab == attention) { leaf = p; }
      }
      collapsed = uishell_sidebar_panel_collapsed(leaf, uishell_sidebar_row_height(), &header_h);
    }
    ui_end_build();
    StateCheck(leaf != &rd_nil_panel_node && collapsed);
    StateCheck(abs_f32(leaf->pct_of_parent*600.f - header_h) < 1.f && abs_f32(sum-1.f) < .001f);
  }

  //- Presentation: the split local workspace visible, its terminal focused.
  uishell_cmd("select_workspace", .window = window->id, .cfg = work->id);
  uishell_cmd("focus_panel", .window = window->id, .panel = rd_panel_from_cfg(terminal_panel));
  state_frame(ws);
  state_write(dir, "before_restart.txt");
  String8List ids_before = state_workspace_ids(scratch.arena);
  StateCheck(ids_before.node_count == 8);
  if(str8_match(phase, str8_lit("save"), 0))
  {
    StateCheck(write_data_to_file_path(ids_path, str8_list_join(scratch.arena, &ids_before, &lines)));
    state_quit(ws);
    scratch_end(scratch);
    fprintf(stderr, "State behaviour (save for a fresh-process restart): %u failures\n", state_failures);
    abort_self(state_failures ? 1 : 0);
  }

  //- Restart.
  ws = state_restart(ws, &theme, str8_zero());
  state_write(dir, "after_restart.txt");

  //- The restart read every arrangement from Andamento's workspace records:
  // the presentation file it read holds no panel tree, only this device's
  // part of each (focus, labels, Target Resolutions), and each panel tree is
  // the document Andamento keeps.
  {
    String8 presentation = data_from_file_path(scratch.arena, uishell_dashboard.presentation_path);
    StateCheck(!state_saves_panel_tree(presentation));
    StateCheck(str8_find_needle(presentation, 0, str8_lit("arrangement_presentation"), 0) < presentation.size);
    state_check_committed(ws);
  }

  //- The sidebar's arrangement came back from the dashboard record: the
  // presentation file holds no dock and no closed sections, only this
  // device's part (collapse, sizes, focus), and the dock is the document
  // Andamento keeps.
  {
    String8 presentation = data_from_file_path(scratch.arena, uishell_dashboard.presentation_path);
    StateCheck(str8_find_needle(presentation, 0, str8_lit("control_views"), 0) == presentation.size);
    StateCheck(str8_find_needle(presentation, 0, str8_lit("section_positions"), 0) == presentation.size);
    StateCheck(str8_find_needle(presentation, 0, str8_lit("sidebar_presentation"), 0) < presentation.size);
    StateCheck(str8_find_needle(presentation, 0, str8_lit("section_collapsed"), 0) < presentation.size);
    state_check_sidebar_committed(ws);
  }

  //- Every workspace, kept ones included, has the ID it had.
  String8List ids_after = state_workspace_ids(scratch.arena);
  StateCheck(ids_after.node_count == ids_before.node_count);
  for(String8Node *a = ids_before.first, *b = ids_after.first; a && b; a = a->next, b = b->next)
  {
    if(!str8_match(a->string, b->string, 0))
    { fprintf(stderr, "Workspace ID changed across restart: %.*s -> %.*s\n", str8_varg(a->string), str8_varg(b->string)); state_failures++; }
  }

  //- A divider drag reaches Andamento once, when it ends: no call while it
  // is in flight, one commit after.
  {
    state = ws->sidebar;
    window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *workspace = state_workspace_labelled(window, str8_lit("Workspace 1"));
    UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, workspace);
    RD_PanelNode *first = mount.panel_tree.root->first;
    CFG_Node *first_cfg = rd_cfg_from_panel(first->address);
    String8 weight_before = push_str8_copy(scratch.arena, first_cfg->string);
    U64 calls = state->store_calls, commits = state->store_commits;
    rd_boundary_resize_begin(ui_key_from_string(ui_key_zero(), str8_lit("state_behaviour_drag")), &mount, first);
    for(U32 step = 1; step <= 4; step++)
    {
      rd_state->frame_index += 1;
      rd_boundary_resize_move(0.05f*step, 0.05f, 1);
      state_frame(ws);
      StateCheck(state->store_calls == calls && state->store_commits == commits);
    }
    rd_state->frame_index += 1;
    rd_boundary_resize_commit();
    state_frame(ws);
    state_frame(ws);
    StateCheck(state->store_commits == commits+1);
    StateCheck(!str8_match(first_cfg->string, weight_before, 0));
    state_check_committed(ws);
  }

  //- A sidebar divider drag reaches Andamento once, when it ends, sizes
  // and all: releasing it opts into sizes set by hand, as the boundary does.
  {
    state = ws->sidebar;
    window = cfg_node_from_id(ws->cfg_id);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    CFG_Node *root = uishell_sidebar_dock_layout(&split);
    UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, root);
    RD_PanelNode *first = mount.panel_tree.root->first;
    CFG_Node *first_cfg = rd_cfg_from_panel(first->address);
    String8 weight_before = push_str8_copy(scratch.arena, first_cfg->string);
    U64 calls = state->store_calls, commits = state->sidebar_commits;
    rd_boundary_resize_begin(ui_key_from_string(ui_key_zero(), str8_lit("state_behaviour_sidebar_drag")), &mount, first);
    for(U32 step = 1; step <= 4; step++)
    {
      rd_state->frame_index += 1;
      rd_boundary_resize_move(0.05f*step, 0.05f, 1);
      state_frame(ws);
      StateCheck(state->store_calls == calls && state->sidebar_commits == commits);
    }
    rd_state->frame_index += 1;
    uishell_sidebar_manual_sizing(window, 1);
    rd_boundary_resize_commit();
    state_frame(ws);
    state_frame(ws);
    StateCheck(state->sidebar_commits == commits+1);
    StateCheck(!str8_match(first_cfg->string, weight_before, 0));
    state_check_sidebar_committed(ws);
  }

  //- A region the template drops (template drift, ADR 0013) keeps its
  // place, flagged, rather than disappearing; once the template has it
  // again, it is where it was.
  {
    state = ws->sidebar;
    window = cfg_node_from_id(ws->cfg_id);
    String8 daily = str8_cstring((char *)uishell_sidebar_daily_config);
    U64 at = str8_find_needle(daily, 0, str8_lit("region \"sessions\""), 0);
    U64 end = str8_find_needle(daily, at, str8_lit("\n"), 0);
    StateCheck(at < daily.size);
    String8 drifted = push_str8f(scratch.arena, "%S%S", str8_prefix(daily, at), str8_skip(daily, end+1));
    CFG_Node *sessions = uishell_sidebar_region_view(window, str8_lit("sessions"));
    StateCheck(sessions != &cfg_nil_node);
    StateCheck(andamento_configure(state->core, uishell_sidebar_text(drifted), 0));
    uishell_sidebar_refresh(state);
    state_frame(ws);
    StateCheck(uishell_sidebar_region_view(window, str8_lit("sessions")) == sessions && uishell_sidebar_section_gone(state, str8_lit("sessions")));
    String8 text = state_sidebar_text(scratch.arena);
    StateCheck(str8_find_needle(text, 0, str8_lit("section \"Sessions\" selected flagged"), 0) < text.size);
    StateCheck(andamento_configure(state->core, uishell_sidebar_text(daily), 0));
    uishell_sidebar_refresh(state);
    state_frame(ws);
    StateCheck(uishell_sidebar_region_view(window, str8_lit("sessions")) == sessions && !uishell_sidebar_section_gone(state, str8_lit("sessions")));
    state_check_sidebar_committed(ws);
  }

  //- A slot whose content Wheelhouse can't show (a web page another
  // frontend added, which Andamento places at the next commit) gets a
  // placeholder naming it, and keeps its View Spec.
  {
    state = ws->sidebar;
    window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *workspace = state_workspace_labelled(window, str8_lit("Workspace 1"));
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(workspace));
    AndamentoViewSpec web = {0};
    web.content = ANDAMENTO_SLOT_URL;
    web.url = uishell_sidebar_text(str8_lit("https://example.com/board"));
    web.has_presentation = 1;
    web.presentation = uishell_sidebar_text(str8_lit("web"));
    StateCheck(andamento_slot_set(state->core, id, uishell_sidebar_text(str8_lit("u:web")), &web, ANDAMENTO_REBIND_REPLACE, 0));
    // Any edit commits: the terminal tab selected.
    RD_PanelTree panels = state_panels(scratch.arena, workspace);
    CFG_Node *make = &cfg_nil_node;
    for(RD_PanelNode *p = panels.root; p != &rd_nil_panel_node; p = rd_panel_node_rec__depth_first_pre(panels.root, p).next)
    { for(CFG_NodePtrNode *t = p->tabs.first; t; t = t->next) { if(str8_match(t->v->string, str8_lit("terminal"), 0)) { make = t->v; } } }
    StateCheck(make != &cfg_nil_node);
    uishell_cmd("focus_tab", .window = window->id, .panel = rd_panel_from_cfg(make), .tab = make->id);
    state_frame(ws);
    state_frame(ws);
    String8 text = state_workspaces_text(scratch.arena);
    StateCheck(str8_find_needle(text, 0, str8_lit("tab placeholder slot=\"u:web\" content=\"https://example.com/board\""), 0) < text.size);
    state_check_committed(ws);
    AndamentoSlots *slots = andamento_slots_get(state->core, id, 0);
    AndamentoSlot slot = {0};
    StateCheck(uishell_store_slot_find(slots, str8_lit("u:web"), &slot) && slot.spec.content == ANDAMENTO_SLOT_URL &&
               str8_match(uishell_sidebar_string(slot.spec.url), str8_lit("https://example.com/board"), 0));
    andamento_slots_free(slots);
  }

  //- A commit made against a generation that has moved since (here another
  // host committed in between) is made again at the new generation: the
  // edit is kept, whole.
  {
    state = ws->sidebar;
    window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *workspace = state_workspace_labelled(window, str8_lit("Workspace 1"));
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(workspace));
    AndamentoWorkspaceArrangement *stored = andamento_arrangement_get(state->core, id, 0);
    U64 count = stored ? stored->doc.panel_count : 0, children = 0;
    AndamentoDocPanel *panels = push_array(scratch.arena, AndamentoDocPanel, count+1);
    for(U64 i = 0; i < count; i++)
    {
      panels[i] = stored->doc.panels[i];
      if(panels[i].parent == 0) { panels[i].weight = 0.5; children++; }
    }
    StateCheck(children == 2);
    AndamentoWorkspaceArrangement *committed = andamento_arrangement_commit(state->core, id, &(AndamentoArrangementDoc){panels, count},
                                                                            stored ? stored->generation : 0, 0);
    StateCheck(committed && committed->status == ANDAMENTO_COMMITTED);
    andamento_arrangement_free(committed);
    andamento_arrangement_free(stored);
    RD_PanelTree tree = state_panels(scratch.arena, workspace);
    CFG_Node *first_cfg = rd_cfg_from_panel(tree.root->first->address);
    String8 weight = push_str8_copy(scratch.arena, first_cfg->string);
    CFG_Node *other = tree.root->first->tabs.first->v == tree.root->first->selected_tab ? tree.root->first->tabs.last->v : tree.root->first->tabs.first->v;
    U64 commits = state->store_commits;
    uishell_cmd("focus_tab", .window = window->id, .panel = rd_panel_from_cfg(first_cfg), .tab = other->id);
    state_frame(ws);
    StateCheck(state->store_commits == commits+2);
    StateCheck(str8_match(first_cfg->string, weight, 0) && cfg_node_child_from_string(other, str8_lit("selected")) != &cfg_nil_node);
    state_check_committed(ws);
  }

  //- A subject publishing workspace.primary.* has a Suggested Layout of its
  // own: its terminal is the provider's `primary` slot, which follows the
  // provider, so Wheelhouse never sets its spec and keeps what it runs on
  // this device.
  {
    CFG_Node *governor = state_open_subject(ws, str8_lit("role"), str8_lit("p/governor"));
    state = ws->sidebar;
    state_frame(ws);
    CFG_Node *primary = state_panels(scratch.arena, governor).root->tabs.first->v;
    StateCheck(str8_match(uishell_store_setting(primary, str8_lit("slot")), str8_lit("primary"), 0));
    StateCheck(cfg_node_child_from_string(primary, str8_lit("follows_provider")) != &cfg_nil_node);
    AndamentoSlots *slots = andamento_slots_get(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(governor)), 0);
    AndamentoSlot slot = {0};
    StateCheck(uishell_store_slot_find(slots, str8_lit("primary"), &slot) && slot.in_baseline && !slot.detached &&
               slot.spec.content == ANDAMENTO_SLOT_FACET);
    andamento_slots_free(slots);
    state_check_committed(ws);
    // Back to the workspace that was visible.
    window = cfg_node_from_id(ws->cfg_id);
    uishell_cmd("select_workspace", .window = window->id, .cfg = state_workspace_labelled(window, str8_lit("Workspace 1"))->id);
    state_frame(ws);
  }

  //- A subject whose Suggested Layout arranges its workspace: it opens as
  // the provider arranges it, and every slot, not just `primary`, goes
  // through Andamento's plan: planned, applied, completed.
  CFG_Node *reviewer = &cfg_nil_node;
  {
    state_reviewer_partner = state_reviewer_watch = 1;
    state = ws->sidebar;
    state_producer(state);
    state_frame(ws);
    reviewer = state_open_subject(ws, str8_lit("role"), str8_lit("p/reviewer"));
    state_frame(ws);
    state_frame(ws);
    String8 text = state_workspaces_text(scratch.arena);
    String8 expected = str8_lit("workspace \"reviewer\" subject=role/p/reviewer\n"
                                "      panel split=x\n"
                                "        panel weight=1.00\n"
                                "          tab terminal \"Terminal\" slot=\"primary\" command=\"exec /bin/sh\" selected\n"
                                "          tab terminal slot=\"partner\" command=\"attach partner 1\"\n"
                                "          tab terminal slot=\"watch\" command=\"attach watch 1\"\n"
                                "        panel weight=1.00\n"
                                "          tab terminal slot=\"log\" command=\"tail -f build.log\" selected");
    StateCheck(str8_find_needle(text, 0, expected, 0) < text.size);
    if(str8_find_needle(text, 0, expected, 0) == text.size) { fprintf(stderr, "workspaces:\n%.*s\n", str8_varg(text)); }
    CFG_Node *partner = state_slot_view(reviewer, str8_lit("partner"));
    StateCheck(cfg_node_child_from_string(partner, str8_lit("follows_provider")) != &cfg_nil_node);
    StateCheck(state_slot_plan(state, reviewer, str8_lit("partner"), partner, 0) == ANDAMENTO_CONTENT_CURRENT);
    StateCheck(state_slot_plan(state, reviewer, str8_lit("log"), state_slot_view(reviewer, str8_lit("log")), 0) == ANDAMENTO_CONTENT_CURRENT);
    state_check_committed(ws);
  }

  //- A keep-previous slot rebinds to its provider's new target, and the
  // instance it replaced stays reachable, in the tab beside it, until the
  // user releases it.
  {
    state_reviewer_partner = 2;
    state_producer(state);
    state_frame(ws);
    state_frame(ws);
    CFG_Node *partner = state_slot_view(reviewer, str8_lit("partner"));
    CFG_Node *previous = state_previous_view(reviewer, str8_lit("partner"));
    StateCheck(str8_match(rd_expr_from_cfg(partner), str8_lit("attach partner 2"), 0));
    StateCheck(previous != &cfg_nil_node && previous->parent == partner->parent &&
               str8_match(rd_expr_from_cfg(previous), str8_lit("attach partner 1"), 0));
    B32 has_previous = 0;
    StateCheck(state_slot_plan(state, reviewer, str8_lit("partner"), partner, &has_previous) == ANDAMENTO_CONTENT_CURRENT && has_previous);
    StateCheck(state_offers(previous, UIShell_SlotAction_ReleasePrevious) && state_offers(partner, UIShell_SlotAction_ReleasePrevious));
    // The document has the Slot once: the previous instance isn't one.
    state_check_committed(ws);
    uishell_store_act(previous, UIShell_SlotAction_ReleasePrevious);
    state_frame(ws);
    StateCheck(state_previous_view(reviewer, str8_lit("partner")) == &cfg_nil_node);
    StateCheck(state_slot_plan(state, reviewer, str8_lit("partner"), partner, &has_previous) == ANDAMENTO_CONTENT_CURRENT && !has_previous);
  }

  //- An ask slot waits for the user: the View says what it would show, and
  // updates only when told to; declining keeps what it shows.
  {
    state_reviewer_watch = 2;
    state_producer(state);
    state_frame(ws);
    CFG_Node *watch = state_slot_view(reviewer, str8_lit("watch"));
    StateCheck(str8_match(rd_expr_from_cfg(watch), str8_lit("attach watch 1"), 0));
    StateCheck(str8_match(uishell_store_setting(watch, str8_lit("asks")), str8_lit("attach watch 2"), 0));
    StateCheck(state_offers(watch, UIShell_SlotAction_Update) && state_offers(watch, UIShell_SlotAction_Decline));
    StateCheck(state_slot_plan(state, reviewer, str8_lit("watch"), watch, 0) == ANDAMENTO_CONTENT_UPDATING);
    uishell_store_act(watch, UIShell_SlotAction_Update);
    state_frame(ws);
    watch = state_slot_view(reviewer, str8_lit("watch"));
    StateCheck(str8_match(rd_expr_from_cfg(watch), str8_lit("attach watch 2"), 0) && !state_offers(watch, UIShell_SlotAction_Update));
    StateCheck(state_slot_plan(state, reviewer, str8_lit("watch"), watch, 0) == ANDAMENTO_CONTENT_CURRENT);
    state_reviewer_watch = 3;
    state_producer(state);
    state_frame(ws);
    uishell_store_act(watch, UIShell_SlotAction_Decline);
    state_frame(ws);
    StateCheck(str8_match(rd_expr_from_cfg(watch), str8_lit("attach watch 2"), 0) && !state_offers(watch, UIShell_SlotAction_Update));
    StateCheck(state_slot_plan(state, reviewer, str8_lit("watch"), watch, 0) == ANDAMENTO_CONTENT_FAILED);
  }

  //- A View Spec another host changes reaches the View already open,
  // without a restart.
  {
    window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *workspace = state_workspace_labelled(window, str8_lit("Workspace 1"));
    CFG_Node *make = state_slot_view(workspace, str8_lit("u:3"));
    StateCheck(str8_match(rd_expr_from_cfg(make), str8_lit("make test"), 0));
    AndamentoViewSpec check = {0};
    check.content = ANDAMENTO_SLOT_COMMAND;
    check.command = uishell_sidebar_text(str8_lit("make check"));
    check.has_cwd = 1;
    check.cwd = uishell_sidebar_text(str8_lit("/src/wheelhouse"));
    check.has_presentation = 1;
    check.presentation = uishell_sidebar_text(str8_lit("terminal"));
    StateCheck(andamento_slot_set(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(workspace)),
                                  uishell_sidebar_text(str8_lit("u:3")), &check, ANDAMENTO_REBIND_REPLACE, 0));
    state_frame(ws);
    StateCheck(state_slot_view(workspace, str8_lit("u:3")) == make && str8_match(rd_expr_from_cfg(make), str8_lit("make check"), 0));
    state_check_committed(ws);
  }

  //- A View Spec's argv reaches the program as it is: the terminal runs the
  // View's command line, as its first build launches it, and the program
  // gets each argument verbatim, shell metacharacters and all. Saving the
  // workspace keeps the slot's argv.
  {
    window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *workspace = state_workspace_labelled(window, str8_lit("Workspace 1"));
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(workspace));
    // The program: its arguments after the first, each ended by a NUL, into
    // the file its first names.
    String8 program = push_str8f(scratch.arena, "%S/print-argv", dir);
    String8 out = push_str8f(scratch.arena, "%S/argv.out", dir);
    write_data_to_file_path(program, str8_lit("#!/bin/sh\nout=$1; shift\nfor a; do printf '%s\\0' \"$a\"; done > \"$out.tmp\" && mv \"$out.tmp\" \"$out\"\n"));
    chmod((char *)push_str8_copy(scratch.arena, program).str, 0755);
    String8 args[] = {program, out, str8_lit("a b"), str8_lit("$HOME"), str8_lit("'q'"), str8_lit(";"), str8_lit("*"),
                      str8_lit("\"x\" \\ y"), str8_lit("`id` $(id) & | < > ( ) # ~ !"), str8_lit("")};
    AndamentoText argv[ArrayCount(args)];
    for(U64 i = 0; i < ArrayCount(args); i++) { argv[i] = uishell_sidebar_text(args[i]); }
    AndamentoViewSpec spec = {0};
    spec.content = ANDAMENTO_SLOT_COMMAND;
    spec.argv = argv;
    spec.argc = ArrayCount(argv);
    spec.has_cwd = 1;
    spec.cwd = uishell_sidebar_text(dir);
    spec.has_presentation = 1;
    spec.presentation = uishell_sidebar_text(str8_lit("terminal"));
    StateCheck(andamento_slot_set(state->core, id, uishell_sidebar_text(str8_lit("u:3")), &spec, ANDAMENTO_REBIND_REPLACE, 0));
    state_frame(ws);
    CFG_Node *view = state_slot_view(workspace, str8_lit("u:3"));
    String8 command = rd_expr_from_cfg(view), cwd = uishell_store_setting(view, str8_lit("cwd"));
    StateCheck(str8_match(cwd, dir, 0));
    cleat_provider_desc pd = {.abi_version = CLEAT_PROVIDER_ABI_VERSION, .backend = CLEAT_PROVIDER_BACKEND_IN_PROCESS};
    cleat_provider *provider = cleat_provider_open(&pd);
    cleat_session_desc sd = {.cols = 80, .rows = 24, .cell_width_px = 8, .cell_height_px = 16, .vt_engine = CLEAT_PROVIDER_VT_PASSTHROUGH,
                             .command = command.str, .command_len = command.size, .cwd = cwd.str, .cwd_len = cwd.size};
    cleat_session *session = provider ? cleat_session_create(provider, &sd) : 0;
    StateCheck(session != 0);
    String8 got = str8_zero();
    for(U32 attempt = 0; session && attempt < 1000 && !got.size; attempt++)
    {
      got = data_from_file_path(scratch.arena, out);
      if(!got.size) { usleep(10000); }
    }
    String8List expected = {0};
    for(U64 i = 2; i < ArrayCount(args); i++) { str8_list_pushf(scratch.arena, &expected, "%S%c", args[i], 0); }
    String8 want = str8_list_join(scratch.arena, &expected, 0);
    StateCheck(str8_match(got, want, 0));
    if(!str8_match(got, want, 0)) { fprintf(stderr, "command: %.*s\nreceived (NUL as |): ", str8_varg(command));
                                    for(U64 i = 0; i < got.size; i++) { fputc(got.str[i] ? got.str[i] : '|', stderr); } fputc('\n', stderr); }
    if(session) { cleat_session_destroy(session); }
    if(provider) { cleat_provider_close(provider); }
    AndamentoSlots *slots = andamento_slots_get(state->core, id, 0);
    AndamentoSlot slot = {0};
    StateCheck(uishell_store_slot_find(slots, str8_lit("u:3"), &slot) && slot.spec.argc == ArrayCount(args) && !slot.spec.command.len);
    andamento_slots_free(slots);
    // Kept by the user, it is still argv.
    uishell_store_keep(view);
    slots = andamento_slots_get(state->core, id, 0);
    StateCheck(uishell_store_slot_find(slots, str8_lit("u:3"), &slot) && slot.spec.argc == ArrayCount(args));
    andamento_slots_free(slots);
    state_check_committed(ws);
    // Back to the command it ran.
    AndamentoViewSpec check = {0};
    check.content = ANDAMENTO_SLOT_COMMAND;
    check.command = uishell_sidebar_text(str8_lit("make check"));
    check.has_cwd = 1;
    check.cwd = uishell_sidebar_text(str8_lit("/src/wheelhouse"));
    check.has_presentation = 1;
    check.presentation = uishell_sidebar_text(str8_lit("terminal"));
    StateCheck(andamento_slot_set(state->core, id, uishell_sidebar_text(str8_lit("u:3")), &check, ANDAMENTO_REBIND_REPLACE, 0));
    state_frame(ws);
    StateCheck(str8_match(rd_expr_from_cfg(state_slot_view(workspace, str8_lit("u:3"))), str8_lit("make check"), 0));
  }

  //- A pure reorder (a tab moved within its panel, which leaves the config
  // generation where it was) is committed.
  {
    window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *workspace = state_workspace_labelled(window, str8_lit("Workspace 1"));
    CFG_Node *make = state_slot_view(workspace, str8_lit("u:3"));
    CFG_Node *panel = make->parent;
    CFG_Node *last = &cfg_nil_node;
    for(CFG_Node *c = panel->first; c != &cfg_nil_node; c = c->next) { if(cfg_node_child_from_string(c, str8_lit("slot")) != &cfg_nil_node) { last = c; } }
    StateCheck(last != make);
    String8 moved = push_str8_copy(scratch.arena, uishell_store_setting(last, str8_lit("slot")));
    U64 gen = cfg_change_gen(), commits = state->store_commits;
    cfg_node_insert_child(rd_state->cfg, panel, make->prev, last);
    StateCheck(cfg_change_gen() == gen && last->next == make);
    state_frame(ws);
    StateCheck(state->store_commits == commits+1);
    state_check_committed(ws);
    UIShell_StoreDoc doc = uishell_store_doc(scratch.arena, workspace);
    B32 before = 0;
    for(U64 i = 0; i+1 < doc.tab_count; i++) { before = before || (str8_match(doc.keys[i], moved, 0) && str8_match(doc.keys[i+1], str8_lit("u:3"), 0)); }
    StateCheck(before);
  }

  //- A Cleat daemon session is a portable Target Resolution: saved with its
  // Slot in Andamento rather than in this device's file, tried first after a
  // restart, and cleared when the provider names a new target.
  {
    CFG_Node *partner = state_slot_view(reviewer, str8_lit("partner"));
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(reviewer));
    // The terminal saves these when it starts its session in a daemon.
    uishell_store_set_setting(partner, str8_lit("session"), str8_lit("S1"));
    uishell_store_set_setting(partner, str8_lit("daemon_name"), str8_lit("D1"));
    state_frame(ws);
    AndamentoSlots *slots = andamento_slots_get(state->core, id, 0);
    AndamentoSlot slot = {0};
    B32 saved = uishell_store_slot_find(slots, str8_lit("partner"), &slot) && slot.has_saved;
    AndamentoResolution r = slot.saved;
    StateCheck(saved && str8_match(uishell_sidebar_string(r.kind), str8_lit("cleat-session"), 0) &&
               str8_match(uishell_store_resolution_field(&r, str8_lit("session")), str8_lit("S1"), 0) &&
               str8_match(uishell_store_resolution_field(&r, str8_lit("daemon")), str8_lit("D1"), 0) &&
               str8_match(uishell_store_resolution_field(&r, str8_lit("host")), uishell_store_host(), 0));
    andamento_slots_free(slots);
    rd_autosave();
    state_pump();
    String8 presentation = data_from_file_path(scratch.arena, uishell_dashboard.presentation_path);
    StateCheck(str8_find_needle(presentation, 0, str8_lit("\"S1\""), 0) == presentation.size);
    ws = state_restart(ws, &theme, str8_zero());
    state = ws->sidebar;
    window = cfg_node_from_id(ws->cfg_id);
    reviewer = state_workspace_labelled(window, str8_lit("reviewer"));
    partner = state_slot_view(reviewer, str8_lit("partner"));
    StateCheck(str8_match(uishell_store_setting(partner, str8_lit("session")), str8_lit("S1"), 0) &&
               str8_match(uishell_store_setting(partner, str8_lit("daemon_name")), str8_lit("D1"), 0));
    StateCheck(state_slot_plan(state, reviewer, str8_lit("partner"), partner, 0) == ANDAMENTO_CONTENT_CURRENT);
    // The provider names a new target: the saved session is for the old
    // one. The daemon's session stays beside the new View until released.
    state_reviewer_partner = 3;
    state_producer(state);
    state_frame(ws);
    state_frame(ws);
    slots = andamento_slots_get(state->core, id, 0);
    StateCheck(uishell_store_slot_find(slots, str8_lit("partner"), &slot) && !slot.has_saved);
    andamento_slots_free(slots);
    partner = state_slot_view(reviewer, str8_lit("partner"));
    CFG_Node *previous = state_previous_view(reviewer, str8_lit("partner"));
    StateCheck(str8_match(rd_expr_from_cfg(partner), str8_lit("attach partner 3"), 0) && !uishell_store_setting(partner, str8_lit("session")).size);
    StateCheck(str8_match(uishell_store_setting(previous, str8_lit("session")), str8_lit("S1"), 0));
    uishell_store_act(previous, UIShell_SlotAction_ReleasePrevious);
    state_frame(ws);
    StateCheck(state_previous_view(reviewer, str8_lit("partner")) == &cfg_nil_node);
    // Back to the workspace that was visible.
    uishell_cmd("select_workspace", .window = window->id, .cfg = state_workspace_labelled(window, str8_lit("Workspace 1"))->id);
    state_frame(ws);
  }

  //- The Workspace Overlay (ADR 0013). A divider resize is a soft edit: the
  // arrangement keeps following its provider, whose new slot arrives where
  // it says, and the size stays.
  {
    state = ws->sidebar;
    window = cfg_node_from_id(ws->cfg_id);
    reviewer = state_workspace_labelled(window, str8_lit("reviewer"));
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(reviewer));
    UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, reviewer);
    RD_PanelNode *first = mount.panel_tree.root->first;
    rd_boundary_resize_begin(ui_key_from_string(ui_key_zero(), str8_lit("state_behaviour_soft_drag")), &mount, first);
    rd_state->frame_index += 1;
    rd_boundary_resize_move(0.1f, 0.05f, 1);
    state_frame(ws);
    rd_state->frame_index += 1;
    rd_boundary_resize_commit();
    state_frame(ws);
    AndamentoWorkspaceArrangement *stored = andamento_arrangement_get(state->core, id, 0);
    StateCheck(stored && !stored->owned && stored->flags == ANDAMENTO_ARRANGEMENT_FLAG_SOFT);
    andamento_arrangement_free(stored);
    String8 weight = push_str8_copy(scratch.arena, rd_cfg_from_panel(state_panels(scratch.arena, reviewer).root->first->address)->string);
    StateCheck(!str8_match(weight, str8_lit("1"), 0));
    state_reviewer_version = 2;
    state_producer(state);
    state_frame(ws);
    state_frame(ws);
    CFG_Node *notes = state_slot_view(reviewer, str8_lit("notes"));
    StateCheck(notes != &cfg_nil_node && notes->parent == state_slot_view(reviewer, str8_lit("log"))->parent &&
               str8_match(rd_expr_from_cfg(notes), str8_lit("less NOTES.md"), 0));
    StateCheck(str8_match(rd_cfg_from_panel(state_panels(scratch.arena, reviewer).root->first->address)->string, weight, 0));
    String8 text = state_workspaces_text(scratch.arena);
    StateCheck(str8_find_needle(text, 0, str8_lit("workspace \"reviewer\" subject=role/p/reviewer soft\n"), 0) < text.size);
    state_check_committed(ws);
  }

  //- Closing a provider's slot tombstones it: hidden while its provider
  // keeps it, and still the provider's arrangement.
  {
    CFG_Node *log = state_slot_view(reviewer, str8_lit("log"));
    StateCheck(state_offers(log, UIShell_SlotAction_None) == 0);
    uishell_cmd("close_tab", .window = window->id, .panel = rd_panel_from_cfg(log), .tab = log->id);
    state_frame(ws);
    state_frame(ws);
    StateCheck(state_slot_view(reviewer, str8_lit("log")) == &cfg_nil_node);
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(reviewer));
    AndamentoOverlay *overlay = andamento_overlay_get(state->core, id, 0);
    B32 tombstoned = 0;
    for(U64 i = 0; overlay && i < overlay->note_count; i++)
    {
      AndamentoOverlayNote *note = &overlay->notes[i];
      tombstoned = tombstoned || (note->kind == ANDAMENTO_OVERLAY_TOMBSTONED && str8_match(uishell_sidebar_string(note->key), str8_lit("log"), 0));
    }
    StateCheck(overlay && tombstoned && !overlay->owned);
    andamento_overlay_free(overlay);
    String8 text = state_workspaces_text(scratch.arena);
    StateCheck(str8_find_needle(text, 0, str8_lit("      hidden slot=\"log\"\n"), 0) < text.size);
    state_check_committed(ws);
  }

  //- A slot that comes back in the frame the user closes a tab isn't what
  // the user closed: this device never showed it. Reattached elsewhere (as
  // another frontend would), the provider's hint places it, which Andamento
  // doesn't note as placed.
  {
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(reviewer));
    CFG_Node *watch = state_slot_view(reviewer, str8_lit("watch"));
    StateCheck(watch != &cfg_nil_node && state_slot_view(reviewer, str8_lit("log")) == &cfg_nil_node);
    StateCheck(andamento_slot_reattach(state->core, id, uishell_sidebar_text(str8_lit("log")), 0));
    if(watch != &cfg_nil_node) { uishell_cmd("close_tab", .window = window->id, .panel = rd_panel_from_cfg(watch), .tab = watch->id); }
    state_frame(ws);
    state_frame(ws);
    String8 text = state_workspaces_text(scratch.arena);
    B32 kept = state_slot_view(reviewer, str8_lit("log")) != &cfg_nil_node &&
               str8_find_needle(text, 0, str8_lit("      hidden slot=\"log\"\n"), 0) == text.size;
    StateCheck(kept);
    if(!kept) { fprintf(stderr, "the reattached log was hidden with the closed tab:\n%.*s\n", str8_varg(text)); }
    StateCheck(state_slot_view(reviewer, str8_lit("watch")) == &cfg_nil_node &&
               str8_find_needle(text, 0, str8_lit("      hidden slot=\"watch\"\n"), 0) < text.size);
    // Back as it was: watch shown, log hidden.
    StateCheck(andamento_slot_reattach(state->core, id, uishell_sidebar_text(str8_lit("watch")), 0));
    state_frame(ws);
    state_frame(ws);
    CFG_Node *log = state_slot_view(reviewer, str8_lit("log"));
    if(log != &cfg_nil_node) { uishell_cmd("close_tab", .window = window->id, .panel = rd_panel_from_cfg(log), .tab = log->id); }
    state_frame(ws);
    state_frame(ws);
    StateCheck(state_slot_view(reviewer, str8_lit("watch")) != &cfg_nil_node && state_slot_view(reviewer, str8_lit("log")) == &cfg_nil_node);
    state_check_committed(ws);
  }

  //- A split is structural: the overlay owns the arrangement from then on,
  // and a provider's later change to it is flagged, not applied, until the
  // user keeps theirs or follows the provider's. The provider reusing the
  // tombstoned key for other content shows it again, flagged.
  {
    CFG_Node *notes = state_slot_view(reviewer, str8_lit("notes"));
    uishell_cmd("split_panel", .window = window->id, .panel = rd_panel_from_cfg(notes), .view = notes->id,
                .dst_panel = rd_panel_from_cfg(notes), .dir2 = Dir2_Down);
    state_frame(ws);
    state_frame(ws);
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(reviewer));
    AndamentoWorkspaceArrangement *stored = andamento_arrangement_get(state->core, id, 0);
    StateCheck(stored && stored->owned && stored->flags == 0);
    andamento_arrangement_free(stored);
    String8 before = push_str8_copy(scratch.arena, state_workspaces_text(scratch.arena));
    state_reviewer_version = 3;
    state_producer(state);
    state_frame(ws);
    state_frame(ws);
    stored = andamento_arrangement_get(state->core, id, 0);
    StateCheck(stored && stored->owned && (stored->flags & ANDAMENTO_ARRANGEMENT_FLAG_PROVIDER_CHANGED));
    andamento_arrangement_free(stored);
    CFG_Node *log = state_slot_view(reviewer, str8_lit("log"));
    StateCheck(str8_match(rd_expr_from_cfg(log), str8_lit("tail -f test.log"), 0));
    StateCheck(state_offers(log, UIShell_SlotAction_Reattach) && state_offers(log, UIShell_SlotAction_Remove));
    String8 text = state_workspaces_text(scratch.arena);
    StateCheck(str8_find_needle(text, 0, str8_lit("workspace \"reviewer\" subject=role/p/reviewer provider-changed\n"), 0) < text.size &&
               str8_find_needle(text, 0, str8_lit("tab terminal slot=\"log\" command=\"tail -f test.log\" changed"), 0) < text.size);
    if(str8_find_needle(text, 0, str8_lit(" provider-changed\n"), 0) == text.size) { fprintf(stderr, "before:\n%.*s\nafter:\n%.*s\n", str8_varg(before), str8_varg(text)); }
    // The workspace's notice is on its first panel's Views.
    CFG_Node *primary = state_slot_view(reviewer, str8_lit("primary"));
    StateCheck(state_offers(primary, UIShell_SlotAction_KeepLayout) && state_offers(primary, UIShell_SlotAction_FollowLayout));
    uishell_store_act(primary, UIShell_SlotAction_KeepLayout);
    state_frame(ws);
    stored = andamento_arrangement_get(state->core, id, 0);
    StateCheck(stored && stored->owned && stored->flags == 0);
    andamento_arrangement_free(stored);
    StateCheck(!state_offers(primary, UIShell_SlotAction_KeepLayout));
    // Taking the reused slot as the provider's settles it.
    uishell_store_act(log, UIShell_SlotAction_Reattach);
    state_frame(ws);
    StateCheck(!state_offers(state_slot_view(reviewer, str8_lit("log")), UIShell_SlotAction_Reattach));
    state_check_committed(ws);
  }

  //- A slot its provider removes, which the user never edited, follows its
  // rebind policy: this one (ask) stays until the user closes it, which
  // releases it.
  {
    state_reviewer_version = 4;
    state_producer(state);
    state_frame(ws);
    state_frame(ws);
    CFG_Node *watch = state_slot_view(reviewer, str8_lit("watch"));
    StateCheck(watch != &cfg_nil_node && state_offers(watch, UIShell_SlotAction_Remove));
    uishell_store_act(watch, UIShell_SlotAction_Remove);
    state_frame(ws);
    state_frame(ws);
    StateCheck(state_slot_view(reviewer, str8_lit("watch")) == &cfg_nil_node);
    UIShell_StoreEntry *entry = uishell_store_entry_find(state, uishell_workspace_id_from_cfg(reviewer));
    StateCheck(entry && entry->departed_count == 0);
    state_check_committed(ws);
  }

  //- Editing a provider's slot detaches it from its provider; reattaching
  // drops the edit, and its provider's content comes back.
  {
    CFG_Node *partner = state_slot_view(reviewer, str8_lit("partner"));
    UIShell_RegsScope(.view = partner->id) { rd_store_view_expr_string(str8_lit("htop")); }
    state_frame(ws);
    state_frame(ws);
    partner = state_slot_view(reviewer, str8_lit("partner"));
    StateCheck(str8_match(rd_expr_from_cfg(partner), str8_lit("htop"), 0) && state_offers(partner, UIShell_SlotAction_Reattach));
    String8 text = state_workspaces_text(scratch.arena);
    StateCheck(str8_find_needle(text, 0, str8_lit("slot=\"partner\" command=\"htop\" daemon_name=\"D1\" detached"), 0) < text.size);
    uishell_store_act(partner, UIShell_SlotAction_Reattach);
    state_frame(ws);
    state_frame(ws);
    partner = state_slot_view(reviewer, str8_lit("partner"));
    StateCheck(str8_match(rd_expr_from_cfg(partner), str8_lit("attach partner 3"), 0) && !state_offers(partner, UIShell_SlotAction_Reattach));
    // Its edit was the user's own content, so no previous instance is kept.
    if(state_previous_view(reviewer, str8_lit("partner")) != &cfg_nil_node)
    { uishell_store_act(state_previous_view(reviewer, str8_lit("partner")), UIShell_SlotAction_ReleasePrevious); state_frame(ws); }
    state_check_committed(ws);
  }

  //- Renaming a workspace and giving it a mood are overlay edits: they are
  // in its record, not this device's file, and survive a restart.
  {
    cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string(reviewer, str8_lit("label")), str8_lit("Review"));
    cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string_or_alloc(rd_state->cfg, reviewer, str8_lit("theme")), str8_lit("Calm"));
    state_frame(ws);
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(reviewer));
    AndamentoOverlay *overlay = andamento_overlay_get(state->core, id, 0);
    StateCheck(overlay && overlay->has_name && str8_match(uishell_sidebar_string(overlay->name), str8_lit("Review"), 0) &&
               overlay->has_mood && str8_match(uishell_sidebar_string(overlay->mood), str8_lit("Calm"), 0));
    andamento_overlay_free(overlay);
    rd_autosave();
    state_pump();
    String8 presentation = data_from_file_path(scratch.arena, uishell_dashboard.presentation_path);
    StateCheck(str8_find_needle(presentation, 0, str8_lit("Review"), 0) == presentation.size &&
               str8_find_needle(presentation, 0, str8_lit("Calm"), 0) == presentation.size);
    String8 before = push_str8_copy(scratch.arena, state_workspaces_text(scratch.arena));
    ws = state_restart(ws, &theme, str8_zero());
    state = ws->sidebar;
    window = cfg_node_from_id(ws->cfg_id);
    reviewer = state_workspace_labelled(window, str8_lit("Review"));
    StateCheck(reviewer != &cfg_nil_node && str8_match(uishell_store_setting(reviewer, str8_lit("theme")), str8_lit("Calm"), 0));
    String8 after = state_workspaces_text(scratch.arena);
    StateCheck(str8_match(before, after, 0));
    if(!str8_match(before, after, 0)) { fprintf(stderr, "before:\n%.*s\nafter:\n%.*s\n", str8_varg(before), str8_varg(after)); }
    uishell_cmd("select_workspace", .window = window->id, .cfg = state_workspace_labelled(window, str8_lit("Workspace 1"))->id);
    state_frame(ws);
  }

  //- A restart restores them all from the workspace records, and the
  // sidebar, the drag's sizes included, from the dashboard record.
  {
    String8 before = state_workspaces_text(scratch.arena);
    String8 sidebar_before = state_sidebar_text(scratch.arena);
    ws = state_restart(ws, &theme, str8_zero());
    String8 after = state_workspaces_text(scratch.arena);
    StateCheck(str8_match(before, after, 0));
    if(!str8_match(before, after, 0)) { fprintf(stderr, "before:\n%.*s\nafter:\n%.*s\n", str8_varg(before), str8_varg(after)); }
    state_check_committed(ws);
    String8 sidebar_after = state_sidebar_text(scratch.arena);
    StateCheck(str8_match(sidebar_before, sidebar_after, 0));
    if(!str8_match(sidebar_before, sidebar_after, 0)) { fprintf(stderr, "before:\n%.*s\nafter:\n%.*s\n", str8_varg(sidebar_before), str8_varg(sidebar_after)); }
    state_check_sidebar_committed(ws);
  }

  //- Idle: once the restart's own changes are saved, frames that change
  // nothing write nothing, and changing only the visible workspace writes.
  rd_autosave();
  state_pump();
  state_frame(ws);
  state_frame(ws);
  StateCheck(!rd_autosave());
  window = cfg_node_from_id(ws->cfg_id);
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  StateCheck(split.inventory.last != split.inventory.selected);
  uishell_cmd("select_workspace", .window = window->id, .cfg = split.inventory.last->id);
  state_frame(ws);
  StateCheck(rd_autosave());
  state_pump();

  //- A change is written a second after it is first seen, not at once.
  {
    state = ws->sidebar;
    String8 path = push_str8f(scratch.arena, "%S/dashboard.record", state->records_dir);
    UIShell_DisplayControlIterator it = {state->snapshot};
    AndamentoControl control = {0}; AndamentoText name = {0};
    size_t toggle = ANDAMENTO_NONE;
    while(uishell_sidebar_next_persistent_control(&it, &control, &name))
    { if(str8_match(uishell_sidebar_string(name), str8_lit("show-role-attempts"), 0)) { toggle = control.action; break; } }
    String8 before = data_from_file_path(scratch.arena, path);
    state_click(ws, toggle);
    U64 now = 1000000;
    uishell_sidebar_records_save(state, now, 0);
    uishell_sidebar_records_save(state, now+999, 0);
    StateCheck(str8_match(data_from_file_path(scratch.arena, path), before, 0));
    uishell_sidebar_records_save(state, now+1000, 0);
    String8 after = data_from_file_path(scratch.arena, path);
    // Its bytes are Andamento's; only that they changed is Wheelhouse's to see.
    StateCheck(after.size && !str8_match(after, before, 0));
  }

  //- The Dashboard directory named on the command line holds the records;
  // this device's area holds its windows, and the user file only settings.
  UIShell_Dashboard *dashboard = &uishell_dashboard;
  String8 dashboard_a = push_str8_copy(scratch.arena, dashboard->dir);
  {
    uishell_cmd("write_user_data");
    state_pump();
    StateCheck(str8_match(dashboard_a, push_str8f(scratch.arena, "%S/dashboard-a", dir), 0));
    StateCheck(str8_match(str8_skip_chop_whitespace(data_from_file_path(scratch.arena, push_str8f(scratch.arena, "%S/id", dashboard_a))),
                          dashboard->id, 0));
    StateCheck(str8_match(dashboard->presentation_path, push_str8f(scratch.arena, "%S/presentation/%S.wheelhouse", dir, dashboard->id), 0));
    String8 presentation = data_from_file_path(scratch.arena, dashboard->presentation_path);
    StateCheck(str8_find_needle(presentation, 0, str8_lit("window:"), 0) < presentation.size);
    // A workspace's name is its record's (an overlay edit), not this device's.
    CFG_Node *notes = state_workspace_labelled(cfg_node_from_id(ws->cfg_id), str8_lit("Notes"));
    String8 record = data_from_file_path(scratch.arena, push_str8f(scratch.arena, "%S/workspace-%S.record", dashboard_a, uishell_workspace_id_text_from_cfg(notes)));
    StateCheck(str8_find_needle(presentation, 0, str8_lit("Notes"), 0) == presentation.size &&
               str8_find_needle(record, 0, str8_lit("Notes"), 0) < record.size);
    StateCheck(file_path_exists(push_str8f(scratch.arena, "%S/dashboard.record", dashboard_a)));
    String8 user = data_from_file_path(scratch.arena, rd_state->user_path);
    StateCheck(str8_find_needle(user, 0, str8_lit("keybindings"), 0) < user.size &&
               str8_find_needle(user, 0, str8_lit("\nwindow:"), 0) == user.size);
  }

  //- A Dashboard opened for the first time starts afresh, and two opened
  // alternately keep their own workspaces, sidebars and records.
  {
    String8List ids_a = state_workspace_ids(scratch.arena);
    String8 dashboard_b = push_str8f(scratch.arena, "%S/dashboard-b", dir);
    ws = state_restart(ws, &theme, dashboard_b);
    StateCheck(str8_match(dashboard->dir, dashboard_b, 0));
    StateCheck(state_find_local(ws->sidebar, str8_lit("Notes")) == ANDAMENTO_NONE);
    window = cfg_node_from_id(ws->cfg_id);
    uishell_cmd("new_workspace", .window = window->id);
    state_frame(ws);
    CFG_Node *only_b = cfg_node_child_list_from_string(scratch.arena, window, str8_lit("workspace")).last->v;
    cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string_or_alloc(rd_state->cfg, only_b, str8_lit("label")), str8_lit("Only in B"));
    state_frame(ws);
    String8 only_b_id = push_str8_copy(scratch.arena, uishell_workspace_id_text_from_cfg(only_b));
    String8List ids_b = state_workspace_ids(scratch.arena);
    for(String8Node *b = ids_b.first; b; b = b->next)
    {
      String8 id = str8_postfix(b->string, 36);
      for(String8Node *a = ids_a.first; a; a = a->next) { StateCheck(str8_find_needle(a->string, 0, id, 0) == a->string.size); }
    }
    for(U32 round = 0; round < 2; round++)
    {
      ws = state_restart(ws, &theme, dashboard_a);
      StringJoin join = {.sep = str8_lit("\n")};
      String8List ids = state_workspace_ids(scratch.arena);
      StateCheck(str8_match(str8_list_join(scratch.arena, &ids_a, &join), str8_list_join(scratch.arena, &ids, &join), 0));
      StateCheck(state_find_local(ws->sidebar, str8_lit("Notes")) != ANDAMENTO_NONE);
      StateCheck(state_find_local(ws->sidebar, str8_lit("Only in B")) == ANDAMENTO_NONE);
      ws = state_restart(ws, &theme, dashboard_b);
      ids = state_workspace_ids(scratch.arena);
      StateCheck(str8_match(str8_list_join(scratch.arena, &ids_b, &join), str8_list_join(scratch.arena, &ids, &join), 0));
      StateCheck(state_find_local(ws->sidebar, str8_lit("Only in B")) != ANDAMENTO_NONE);
      StateCheck(state_find_local(ws->sidebar, str8_lit("Notes")) == ANDAMENTO_NONE);
    }
    StateCheck(file_path_exists(push_str8f(scratch.arena, "%S/workspace-%S.record", dashboard_b, only_b_id)));
    StateCheck(!file_path_exists(push_str8f(scratch.arena, "%S/workspace-%S.record", dashboard_a, only_b_id)));
    // Without --dashboard, a launch opens the one opened last.
    rd_autosave();
    state_pump();
    uishell_dashboard_open(str8_zero(), dir);
    StateCheck(str8_match(dashboard->dir, dashboard_b, 0));
  }

  //- A user file saved before Dashboards keeps its windows, unread: no
  // Dashboard imports them, and an older build still finds them.
  {
    rd_autosave();
    state_pump();
    rd_window_state_release(ws);
    String8 user_path = push_str8_copy(scratch.arena, rd_state->user_path);
    StateCheck(append_data_to_file_path(user_path, str8_lit("window:\n{\n size: 900 600\n workspace:\n {\n  label: \"Before Dashboards\"\n }\n}\n")));
    UISHELL_APP_INITIAL_LOAD(user_path, rd_state->project_path);
    state_pump();
    ws = state_open_window(&theme);
    StateCheck(state_find_local(ws->sidebar, str8_lit("Before Dashboards")) == ANDAMENTO_NONE);
    StateCheck(state_find_local(ws->sidebar, str8_lit("Only in B")) != ANDAMENTO_NONE);
    uishell_cmd("write_user_data");
    state_pump();
    String8 user = data_from_file_path(scratch.arena, user_path);
    StateCheck(str8_find_needle(user, 0, str8_lit("Before Dashboards"), 0) < user.size);
    String8 presentation = data_from_file_path(scratch.arena, dashboard->presentation_path);
    StateCheck(str8_find_needle(presentation, 0, str8_lit("Before Dashboards"), 0) == presentation.size);
  }

  //- A Dashboard whose presentation file still holds its workspaces' panel
  // trees and its sidebar, as state model step 4 saved them, imports each
  // into Andamento on its first load, slots and arrangement, and its next
  // save drops them.
  {
    // Dashboard C: this window saved as step 4 saved it, with no records.
    rd_autosave();
    state_pump();
    String8 workspaces_b = state_workspaces_text(scratch.arena);
    String8 sidebar_b = state_sidebar_text(scratch.arena);
    window = cfg_node_from_id(ws->cfg_id);
    CFG_State *legacy = cfg_state_alloc();
    CFG_Node *copy = cfg_node_deep_copy(legacy, window);
    {
      CFG_NodePtrList drop = {0};
      for(CFG_Node *c = copy; c != &cfg_nil_node; c = cfg_node_rec__depth_first(copy, c).next)
      {
        if(str8_match(c->string, str8_lit("arrangement_generation"), 0) || str8_match(c->string, str8_lit("slot"), 0) ||
           str8_match(c->string, str8_lit("sidebar_generation"), 0))
        { cfg_node_ptr_list_push(scratch.arena, &drop, c); }
      }
      for(CFG_NodePtrNode *n = drop.first; n; n = n->next) { cfg_node_release(legacy, n->v); }
    }
    String8 dashboard_c = push_str8f(scratch.arena, "%S/dashboard-c", dir);
    String8 c_id = uishell_string_from_workspace_id(scratch.arena, uishell_workspace_id_make());
    StateCheck(uishell_dashboard_make_directories(dashboard_c));
    StateCheck(write_data_to_file_path(push_str8f(scratch.arena, "%S/id", dashboard_c), push_str8f(scratch.arena, "%S\n", c_id)));
    String8 c_presentation = push_str8f(scratch.arena, "%S/presentation/%S.wheelhouse", dir, c_id);
    String8 legacy_text = push_str8f(scratch.arena, "%s%s presentation file\n\n%S", RD_APP_CONFIG_MAGIC, BUILD_VERSION_STRING_LITERAL,
                                     cfg_string_from_tree(scratch.arena, rd_state->cfg_schema_table, str8_chop_last_slash(c_presentation), copy));
    cfg_state_release(legacy);
    StateCheck(state_saves_panel_tree(legacy_text));
    StateCheck(str8_find_needle(legacy_text, 0, str8_lit("control_views"), 0) < legacy_text.size);
    StateCheck(write_data_to_file_path(c_presentation, legacy_text));
    ws = state_restart(ws, &theme, dashboard_c);
    StateCheck(str8_match(uishell_dashboard.dir, dashboard_c, 0));
    String8 workspaces_c = state_workspaces_text(scratch.arena);
    StateCheck(str8_match(workspaces_c, workspaces_b, 0));
    if(!str8_match(workspaces_c, workspaces_b, 0)) { fprintf(stderr, "B:\n%.*s\nC:\n%.*s\n", str8_varg(workspaces_b), str8_varg(workspaces_c)); }
    state_check_committed(ws);
    String8 sidebar_c = state_sidebar_text(scratch.arena);
    StateCheck(str8_match(sidebar_c, sidebar_b, 0));
    if(!str8_match(sidebar_c, sidebar_b, 0)) { fprintf(stderr, "B:\n%.*s\nC:\n%.*s\n", str8_varg(sidebar_b), str8_varg(sidebar_c)); }
    state_check_sidebar_committed(ws);
    rd_autosave();
    state_pump();
    String8 saved_c = data_from_file_path(scratch.arena, c_presentation);
    StateCheck(!state_saves_panel_tree(saved_c));
    StateCheck(str8_find_needle(saved_c, 0, str8_lit("control_views"), 0) == saved_c.size);
    // Read back from C's records alone.
    ws = state_restart(ws, &theme, dashboard_c);
    StateCheck(str8_match(state_workspaces_text(scratch.arena), workspaces_b, 0));
    StateCheck(str8_match(state_sidebar_text(scratch.arena), sidebar_b, 0));
    state_check_committed(ws);
    state_check_sidebar_committed(ws);
    ws = state_restart(ws, &theme, push_str8f(scratch.arena, "%S/dashboard-b", dir));
  }

  //- A new Dashboard's first frame draws its sidebar before anything else
  // observes it. Andamento placed the container of local sections, as it
  // does before any are published; with them published, it no longer
  // resolves. The first observation is a section View's render, in the
  // middle of drawing the sidebar's panel tree, and its sync drops the
  // container's View and the panel it empties. That waits for the tree to be
  // drawn: dropped mid-draw, the tree walks the released panel (whose links
  // are cleared) and crashes.
  {
    String8 dashboard_b = push_str8_copy(scratch.arena, dashboard->dir);
    state_draw_sidebar = 1;
    ws = state_restart(ws, &theme, push_str8f(scratch.arena, "%S/dashboard-d", dir));
    state_draw_sidebar = 0;
    window = cfg_node_from_id(ws->cfg_id);
    String8 container = {0};
    for(U64 i = 0; i < andamento_snapshot_node_count(ws->sidebar->snapshot); i++)
    {
      AndamentoNode node = {0};
      if(uishell_sidebar_node_at(ws->sidebar, i, &node) == UIShell_SidebarRole_Container)
      { container = push_str8_copy(scratch.arena, uishell_sidebar_string(node.key)); }
    }
    StateCheck(container.size != 0 && uishell_sidebar_region_view(window, container) == &cfg_nil_node);
    StateCheck(uishell_sidebar_region_view(window, uishell_sidebar_local_key(scratch.arena, str8_lit("workspaces"))) != &cfg_nil_node);
    state_window_frame(ws);
    state_check_sidebar_committed(ws);
    ws = state_restart(ws, &theme, dashboard_b);
  }

  //- A sidebar panel shows one of its sections; a section's panel has no tab
  // bar to choose another. The Selected View of a panel holding a second
  // section released with no settle (as a test's cleanup or a hand edit
  // does) leaves the other unselected: the next frame selects it, and
  // Andamento's record keeps it selected. A record selecting none of a
  // panel's tabs, which Andamento allows, restores with its first shown,
  // and the section draws its body.
  {
    window = cfg_node_from_id(ws->cfg_id);
    String8 key = state_section_key(scratch.arena, ws->sidebar, str8_lit("Projects"));
    CFG_Node *view = uishell_sidebar_region_view(window, key);
    StateCheck(view != &cfg_nil_node);
    String8 sidebar_before = state_sidebar_text(scratch.arena);
    CFG_Node *added = uishell_sidebar_local_new_view(rd_panel_from_cfg(view->parent), str8_lit("transient"));
    StateCheck(added != &cfg_nil_node && cfg_node_child_from_string(view, str8_lit("selected")) == &cfg_nil_node);
    cfg_node_release(rd_state->cfg, added);
    state_frame(ws);
    StateCheck(cfg_node_child_from_string(view, str8_lit("selected")) != &cfg_nil_node);
    StateCheck(str8_match(state_sidebar_text(scratch.arena), sidebar_before, 0));
    state_check_sidebar_committed(ws);

    // Andamento's record, with that panel selecting none of its tabs. The
    // window, unchanged, has nothing to commit over it before the restart.
    UIShell_SidebarDoc doc = uishell_sidebar_store_doc(scratch.arena, ws->sidebar, window, 0);
    StateCheck(uishell_sidebar_store_doc_hash(&doc) == ws->sidebar->sidebar_hash);
    U64 unselected = 0;
    for(U64 i = 0; i < doc.panel_count; i++)
    {
      AndamentoDocPanel *p = &doc.panels[i];
      if(p->kind != ANDAMENTO_PANEL_TABS || p->tab_count == 0 || !str8_match(uishell_sidebar_string(p->tabs[0]), key, 0)) { continue; }
      p->selected = ANDAMENTO_NONE;
      unselected += 1;
    }
    StateCheck(unselected == 1);
    String8List placed = {0};
    AndamentoSidebarArrangement *after = uishell_sidebar_store_commit(scratch.arena, ws->sidebar, &doc, ws->sidebar->sidebar_generation, &placed);
    StateCheck(after && after->status == ANDAMENTO_COMMITTED);
    U64 committed = after ? after->generation : 0;
    andamento_sidebar_arrangement_free(after);
    ws->sidebar->sidebar_generation = committed;
    uishell_store_set_setting(window, str8_lit("sidebar_generation"), push_str8f(scratch.arena, "%I64u", committed));
    ws = state_restart(ws, &theme, str8_zero());
    window = cfg_node_from_id(ws->cfg_id);
    view = uishell_sidebar_region_view(window, key);
    StateCheck(view != &cfg_nil_node && cfg_node_child_from_string(view, str8_lit("selected")) != &cfg_nil_node);
    String8 sidebar_after = state_sidebar_text(scratch.arena);
    StateCheck(str8_match(sidebar_after, sidebar_before, 0));
    if(!str8_match(sidebar_after, sidebar_before, 0)) { fprintf(stderr, "before:\n%.*s\nafter:\n%.*s\n", str8_varg(sidebar_before), str8_varg(sidebar_after)); }
    state_check_sidebar_committed(ws);
    state_sidebar_draw(ws);
    UI_Key root = ui_key_from_stringf(ui_key_zero(), "andamento_section_%S", key);
    StateCheck(!ui_box_is_nil(ui_box_from_key(ui_key_from_stringf(root, "section_body_%S", key))));
    state_frame(ws);
    state_check_sidebar_committed(ws);
  }

  //- A local workspace saved before Workspace IDs, whose sidebar entity was
  // a GUID of its own: its ID takes the GUID's place wherever the window
  // names it (as a pin's or a saved order's entity, in the keys saved before
  // records, which a new core imports after IDs are given).
  {
    window = cfg_node_from_id(ws->cfg_id);
    String8 guid = str8_lit("0F1E2D3C-4B5A-6978-8796-A5B4C3D2E1F0");
    CFG_Node *legacy = cfg_node_new(rd_state->cfg, window, str8_lit("detached_workspace"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, legacy, str8_lit("local_entity")), guid);
    CFG_Node *reference = cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, window, str8_lit("sidebar_order")), guid);
    String8 id = push_str8_copy(scratch.arena, uishell_workspace_id_text_from_cfg(legacy));
    StateCheck(id.size == 36 && str8_match(reference->string, id, 0));
    StateCheck(cfg_node_child_from_string(legacy, str8_lit("local_entity")) == &cfg_nil_node);
    StateCheck(uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_cfg(legacy)) == legacy->id);
  }

  //- A window saved before Window IDs, which named its records directory by
  // an ID of its own: that ID becomes its Window ID, so the directory stays
  // its own.
  {
    window = cfg_node_from_id(ws->cfg_id);
    StateCheck(uishell_window_from_id(uishell_window_id_from_cfg(window)) == window);
    String8 records = str8_lit("01890a5d-ac96-774b-bcce-b302099a8057");
    CFG_Node *legacy = cfg_node_new(rd_state->cfg, &cfg_nil_node, str8_lit("window"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, legacy, str8_lit("andamento_records")), records);
    StateCheck(str8_match(uishell_window_id_text_from_cfg(legacy), records, 0));
    StateCheck(cfg_node_child_from_string(legacy, str8_lit("andamento_records")) == &cfg_nil_node);
    cfg_node_release(rd_state->cfg, legacy);
  }

  //- Subscriptions. A Dashboard without any has only local facts. Adding
  // one saves it in the Dashboard and starts its connector on its own
  // endpoint; two publishing the same project and vessel make two of each.
  {
    StateCheck(uishell_subscriptions.first == 0);
    String8 local = cmd_line_string(cmdline, str8_lit("andamento_socket"));
    StateCheck(local.size != 0);
    uishell_subscriptions_go_live(local, cmd_line_string(cmdline, str8_lit("flotilla_bin")), dir);
    window = cfg_node_from_id(ws->cfg_id);
    uishell_cmd("add_flotilla_subscription", .window = window->id, .string = str8_lit("ssh://alpha"));
    uishell_cmd("add_flotilla_subscription", .window = window->id, .string = str8_lit("ssh://beta"));
    // The same daemon again adds nothing.
    uishell_cmd("add_flotilla_subscription", .window = window->id, .string = str8_lit("ssh://beta"));
    state_pump();
    StateCheck(uishell_subscriptions.count == 2);
    UIShell_Subscription *alpha = uishell_subscription_from_text(str8_lit("ssh://alpha"));
    UIShell_Subscription *beta = uishell_subscription_from_text(str8_lit("ssh://beta"));
    StateCheck(alpha && beta && alpha != beta);
    String8 alpha_id = push_str8_copy(scratch.arena, alpha->id), beta_id = push_str8_copy(scratch.arena, beta->id);
    UIShell_WorkspaceId parsed = {0};
    StateCheck(uishell_workspace_id_from_string(alpha_id, &parsed) && (parsed.v[6] >> 4) == 7);
    StateCheck(str8_match(alpha->endpoint, push_str8f(scratch.arena, "%S-%S", local, alpha_id), 0));
    String8 saved = data_from_file_path(scratch.arena, push_str8f(scratch.arena, "%S/subscriptions.wheelhouse", dashboard->dir));
    StateCheck(str8_find_needle(saved, 0, push_str8f(scratch.arena, "subscription:\n{\n  id: \"%S\"\n  kind: \"flotilla\"\n  daemon: \"ssh://alpha\"\n}", alpha_id), 0) < saved.size);
    StateCheck(str8_find_needle(saved, 0, beta_id, 0) < saved.size);
    String8 project = str8_lit("project"), fleet = str8_lit("fleet"), vessel = str8_lit("vessel"), fleet_v = str8_lit("fleet-v");
    StateWaitFor(ws, state_count_from(ws->sidebar, project, fleet, alpha_id, 0) == 1 && state_count_from(ws->sidebar, project, fleet, beta_id, 0) == 1 &&
                     state_count_from(ws->sidebar, vessel, fleet_v, alpha_id, 0) >= 1 && state_count_from(ws->sidebar, vessel, fleet_v, beta_id, 0) >= 1);
    StateCheck(file_path_exists(push_str8f(scratch.arena, "%S/flotilla-%S.log", dir, alpha_id)));
    // Opening alpha's vessel opens a workspace on it, not on beta's.
    size_t activate = ANDAMENTO_NONE;
    for(U64 i = 0; i < andamento_snapshot_node_count(ws->sidebar->snapshot); i++)
    {
      AndamentoNode node = {0};
      andamento_snapshot_node(ws->sidebar->snapshot, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity.id), fleet_v, 0) && str8_match(uishell_sidebar_string(node.entity.provider), alpha_id, 0) &&
         node.openable) { activate = node.activate; break; }
    }
    state_click(ws, activate);
    CFG_Node *opened = cfg_node_from_id(ws->root_controlled_split_selected_workspace_id);
    StateCheck(str8_match(uishell_workspace_cfg_subject_provider(opened), alpha_id, 0));
    StateWaitFor(ws, state_count_from(ws->sidebar, vessel, fleet_v, beta_id, 0) >= 1);
    state_write(dir, "subscriptions_two.txt");

    //- A connector going down makes its provider stale: its rows stay,
    // marked, past their facts' lifetime, until it publishes again.
    String8 stop_beta = push_str8f(scratch.arena, "%S/stop-beta", dir);
    StateCheck(write_data_to_file_path(stop_beta, str8_lit("stop\n")));
    StateWaitFor(ws, beta->stale);
    U64 until = wheelhouse_ingress_now_ms() + 3000;
    StateWaitFor(ws, wheelhouse_ingress_now_ms() > until);
    B32 stale = 0;
    StateCheck(state_count_from(ws->sidebar, project, fleet, beta_id, &stale) == 1 && stale);
    StateCheck(!alpha->stale && state_count_from(ws->sidebar, project, fleet, alpha_id, 0) == 1);
    state_write(dir, "subscriptions_stale.txt");
    StateCheck(delete_file_at_path(stop_beta));
    StateWaitFor(ws, !beta->stale);
    stale = 0;
    StateCheck(state_count_from(ws->sidebar, project, fleet, beta_id, &stale) == 1 && !stale);
    state_write(dir, "subscriptions_fresh.txt");

    //- A subscription's entity has a hover card, as a local one does: its
    // row's card opens on the entity under the subscription's provider (the
    // same kind and ID from the host's provider is another entity) with its
    // details. Pinned, the pin keeps the provider.
    AndamentoEntity fleet_beta = uishell_sidebar_entity(push_str8_copy(scratch.arena, beta_id), project, fleet);
    String8 fleet_pin = str8_zero();
    {
      AndamentoNode row = {0};
      for(U64 i = 0; i < andamento_snapshot_node_count(ws->sidebar->snapshot); i++)
      {
        AndamentoNode node = {0};
        andamento_snapshot_node(ws->sidebar->snapshot, i, &node);
        if(!node.is_section && uishell_sidebar_card_entity_match(node.entity, fleet_beta)) { row = node; break; }
      }
      StateCheck(uishell_sidebar_card_entity_match(row.entity, fleet_beta));
      // Hovering the row past the open delay, as the sidebar's render reports it.
      UIShell_HoverCard *card = &ws->sidebar->cards[0];
      UI_Box source = {.key = ui_key_make(0x5eb5), .rect = r2f32p(0, 0, 100, 20)};
      UI_Signal hover = {.box = &source, .f = UI_SignalFlag_Hovering};
      uishell_sidebar_card_source_at(ws->sidebar, row, hover, str8_zero(), 0, 1000000);
      uishell_sidebar_card_source_at(ws->sidebar, row, hover, str8_zero(), 0, 1000000+UIShell_HoverCardOpenDelayUS);
      StateCheck(card->open && card->depth == 1 && uishell_sidebar_card_entity_match(card->path[0], fleet_beta));
      AndamentoNode found = {0};
      U64 index = card->open ? uishell_sidebar_card_find(ws->sidebar, card->path[0], &found) : ANDAMENTO_NONE;
      AndamentoDetail detail = {0};
      StateCheck(index != ANDAMENTO_NONE && andamento_snapshot_detail(ws->sidebar->snapshot, index, &detail));
      StateCheck(uishell_sidebar_card_entity_match(detail.entity, fleet_beta) && detail.field_count > 0 &&
                 str8_match(uishell_sidebar_string(detail.label), str8_lit("Fleet via beta"), 0));
      StateCheck(uishell_sidebar_card_find(ws->sidebar, uishell_sidebar_entity(uishell_sidebar_host_provider, project, fleet), 0) == ANDAMENTO_NONE);
      // Pin on its hover card.
      UIShell_LocalPin *pinned = uishell_sidebar_card_pin(ws, card, 0);
      StateCheck(pinned && uishell_sidebar_card_entity_match(uishell_sidebar_pin_entity(pinned), fleet_beta));
      fleet_pin = pinned ? push_str8_copy(scratch.arena, pinned->ghost) : str8_zero();
      uishell_sidebar_card_close(card);
      state_frame(ws);
      // Its ghost row offers the subscription entity's card.
      AndamentoNode ghost = {0};
      StateCheck(state_find(ws->sidebar, str8_lit(".ref"), fleet_pin, &ghost));
    }

    //- Removing a subscription stops its connector and retracts its facts;
    // the workspace open on its vessel stays, retained.
    uishell_cmd("remove_subscription", .window = window->id, .string = alpha_id);
    state_pump();
    StateWaitFor(ws, state_count_from(ws->sidebar, vessel, fleet_v, beta_id, 0) >= 1);
    StateCheck(uishell_subscriptions.count == 1 && uishell_subscription_from_text(alpha_id) == 0);
    StateCheck(state_count_from(ws->sidebar, vessel, fleet_v, alpha_id, 0) >= 1);
    StateCheck(cfg_node_from_id(ws->root_controlled_split_selected_workspace_id) == opened);
    saved = data_from_file_path(scratch.arena, push_str8f(scratch.arena, "%S/subscriptions.wheelhouse", dashboard->dir));
    StateCheck(str8_find_needle(saved, 0, alpha_id, 0) == saved.size && str8_find_needle(saved, 0, beta_id, 0) < saved.size);
    state_write(dir, "subscriptions_removed.txt");

    //- A restart opens the Dashboard's subscriptions again and reconnects them.
    ws = state_restart(ws, &theme, push_str8_copy(scratch.arena, dashboard->dir));
    StateCheck(uishell_subscriptions.count == 1 && str8_match(uishell_subscriptions.first->id, beta_id, 0));
    StateWaitFor(ws, state_count_from(ws->sidebar, project, fleet, beta_id, 0) == 1 &&
                     state_count_from(ws->sidebar, vessel, fleet_v, beta_id, 0) >= 1);
    state_write(dir, "subscriptions_restarted.txt");
    // The pin of beta's project survives the restart with its provider, and
    // its card, opened from the saved pin, finds its details.
    {
      UIShell_LocalPin *pin = uishell_sidebar_pin_find(cfg_node_from_id(ws->cfg_id), fleet_beta);
      StateCheck(pin != 0);
      UIShell_HoverCard *pinned = pin ? uishell_sidebar_saved_card(ws, pin) : 0;
      AndamentoNode found = {0};
      StateCheck(pinned && uishell_sidebar_card_entity_match(pinned->path[0], fleet_beta) &&
                 uishell_sidebar_card_find(ws->sidebar, pinned->path[0], &found) != ANDAMENTO_NONE &&
                 str8_match(uishell_sidebar_string(found.label), str8_lit("Fleet via beta"), 0));
      if(pinned) { uishell_sidebar_card_close(pinned); }
      state_frame(ws);
    }
    uishell_subscriptions_close();
  }

  //- A View's state lives as long as its Slot does in a materialized
  // workspace, whether or not it is drawn: a terminal in a tab that isn't
  // selected, one filtered out by project, one in a Floating Panel, all in
  // a workspace that isn't visible, keep their runtimes however many frames
  // pass. Keeping the workspace releases them; reopening it starts them
  // again, the one with a session re-attaching to it.
  {
    window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *workspace = state_open_v(ws);
    RD_PanelTree tree = state_panels(scratch.arena, workspace);
    RD_PanelNode *leaf = tree.root;
    while(leaf->first != &rd_nil_panel_node) { leaf = leaf->first; }
    CFG_Node *panel = rd_cfg_from_panel(leaf->address);
    char *commands[] = {"make watch", "tail -f build.log", "htop"};
    CFG_Node *terminals[4] = {0};
    for(U64 i = 0; i < ArrayCount(commands); i++)
    {
      uishell_cmd("build_tab", .window = window->id, .panel = rd_panel_from_cfg(panel), .string = str8_lit("terminal"), .expr = str8_cstring(commands[i]));
      state_frame(ws);
      tree = state_panels(scratch.arena, workspace);
      terminals[i] = rd_panel_node_from_id(tree.root, rd_panel_from_cfg(panel).id)->tabs.last->v;
      StateCheck(str8_match(terminals[i]->string, str8_lit("terminal"), 0));
    }
    // A terminal saves its daemon session's ID when it starts one, and a
    // tab's project comes from the file it was saved in; no command sets
    // either.
    CFG_Node *attached = terminals[0], *filtered = terminals[1];
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, attached, str8_lit("session")), str8_lit("s-kept"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, filtered, str8_lit("project")), str8_lit("/elsewhere"));
    StateCheck(rd_cfg_is_project_filtered(filtered));
    tree = state_panels(scratch.arena, workspace);
    StateCheck(rd_panel_node_from_id(tree.root, rd_panel_from_cfg(panel).id)->selected_tab == terminals[2]);
    // No command puts a terminal in a Floating Panel; a detached card is
    // the only thing that makes one.
    CFG_Node *floating_panel = cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, workspace, str8_lit("floating_panels")), str8_lit("1"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, floating_panel, str8_lit("text")), str8_lit("selected"));
    terminals[3] = cfg_node_new(rd_state->cfg, floating_panel, str8_lit("terminal"));
    state_frame(ws);

    // Each builds once while it is the selected tab; then another
    // workspace becomes visible.
    state_attached = str8_zero();
    UIShell_TerminalViewState *tvs[4] = {0};
    for(U64 i = 0; i < ArrayCount(terminals); i++) { tvs[i] = state_terminal(terminals[i]); }
    StateCheck(str8_match(state_attached, str8_lit("s-kept"), 0) && tvs[0]->session == 0);
    for(U64 i = 1; i < ArrayCount(tvs); i++) { StateCheck(state_terminal_running(tvs[i])); }
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    CFG_ID other = split.inventory.first->id != workspace->id ? split.inventory.first->id : split.inventory.last->id;
    StateCheck(other != workspace->id);
    uishell_cmd("select_workspace", .window = window->id, .cfg = other);
    U32 released = state_runtimes_released;
    for(U32 frame = 0; frame < 30; frame++)
    {
      rd_state->frame_index += 1;
      state_frame(ws);
    }
    StateCheck(ws->root_controlled_split_selected_workspace_id == other);
    StateCheck(state_runtimes_released == released);
    for(U64 i = 0; i < ArrayCount(terminals); i++) { StateCheck(rd_view_state_from_cfg(terminals[i])->user_data == tvs[i]); }
    for(U64 i = 1; i < ArrayCount(tvs); i++) { StateCheck(state_terminal_running(tvs[i])); }

    // Closing it and keeping it releases all four runtimes.
    uishell_cmd("detach_workspace", .window = window->id, .cfg = workspace->id);
    state_frame(ws);
    StateCheck(str8_match(workspace->string, str8_lit("detached_workspace"), 0));
    StateCheck(state_runtimes_released == released+4);
    for(U64 i = 0; i < ArrayCount(terminals); i++) { StateCheck(!state_has_view_state(terminals[i]->id)); }
    for(U32 frame = 0; frame < 3; frame++)
    {
      rd_state->frame_index += 1;
      state_frame(ws);
    }
    StateCheck(state_runtimes_released == released+4);

    // Reopening it starts them again: the same Views, the one with a
    // session re-attached to it, the others launched afresh.
    StateCheck(state_open_v(ws) == workspace);
    state_attached = str8_zero();
    for(U64 i = 0; i < ArrayCount(terminals); i++)
    {
      StateCheck(cfg_node_from_id(terminals[i]->id) == terminals[i] && !state_has_view_state(terminals[i]->id));
      tvs[i] = state_terminal(terminals[i]);
    }
    StateCheck(str8_match(state_attached, str8_lit("s-kept"), 0) && tvs[0]->session == 0);
    for(U64 i = 1; i < ArrayCount(tvs); i++) { StateCheck(state_terminal_running(tvs[i])); }

    // Closing a tab releases its runtime; closing the workspace, the rest.
    uishell_cmd("close_tab", .window = window->id, .panel = rd_panel_from_cfg(panel), .tab = filtered->id);
    state_frame(ws);
    StateCheck(state_runtimes_released == released+5 && !state_has_view_state(filtered->id));
    StateCheck(state_terminal_running(tvs[2]) && state_terminal_running(tvs[3]));
    uishell_cmd("close_workspace", .window = window->id, .cfg = workspace->id);
    state_frame(ws);
    StateCheck(state_runtimes_released == released+8);
  }

  scratch_end(scratch);
  fprintf(stderr, "State behaviour: %u failures\n", state_failures);
  abort_self(state_failures ? 1 : 0);
}
