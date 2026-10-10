// Compiled by tools/test-state-behaviour.py from the production amalgamation
// with its entry point replaced, linked against real Andamento and Cleat, with
// the headless WM/renderer/font backends. It builds state through production
// commands and sidebar actions, writes the logical state
// (uishell_logical_state.c), restarts, and writes it again; the script
// compares both with goldens.
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

internal void
state_producer(UIShell_SidebarState *state)
{
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
    StateCheck(uishell_sidebar_result(state, andamento_apply_entity(state->core, 0, uishell_sidebar_text(str8_lit("vessel")),
      uishell_sidebar_text(str8_lit("multi")), uishell_sidebar_text(str8_lit("fixture")), facts, ArrayCount(facts), &error), error));
  }
  if(state_chip_gone)
  {
    String8 retract = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"vessel\",\"id\":\"chip-v\"}},\"source_id\":\"fixture\",\"set\":{},"
      "\"unset\":[\"flotilla.project\",\"flotilla.convoy\",\"display.label\",\"status.state\",\"presentation.icon\",\"action.primary.recipe\"]}");
    char *error = 0;
    StateCheck(uishell_sidebar_result(state, andamento_apply_patch_json(state->core, 0, uishell_sidebar_text(retract), &error), error));
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
// its docked sections against the snapshot.
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
    uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && str8_match(uishell_sidebar_string(node.entity_kind), kind, 0) &&
       (!id.size || str8_match(uishell_sidebar_string(node.entity_id), id, 0)))
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
    uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    if(str8_match(uishell_sidebar_string(node.entity_kind), str8_lit(".workspace"), 0) &&
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
    AndamentoText from = {0};
    uint32_t node_stale = 0;
    uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    if(node.is_section || !str8_match(uishell_sidebar_string(node.entity_kind), kind, 0) ||
       !str8_match(uishell_sidebar_string(node.entity_id), id, 0) ||
       !andamento_snapshot_node_provider(state->snapshot, i, &from, &node_stale) ||
       !str8_match(uishell_sidebar_string(from), provider, 0)) { continue; }
    count++;
    if(stale && node_stale) { *stale = 1; }
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

internal CFG_PanelTree
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
// Selected Views, at the generation the window last saw.
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
    AndamentoArrangement *stored = andamento_arrangement_acquire(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner)), 0);
    AndamentoArrangementInfo info = {0};
    B32 same = stored && andamento_arrangement_info(stored, &info) && info.owned &&
      info.panel_count == doc.panel_count && info.tab_count == doc.tab_count &&
      str8_match(uishell_store_setting(owner, str8_lit("arrangement_generation")), push_str8f(scratch.arena, "%I64u", info.generation), 0);
    for(U64 i = 0; same && i < info.panel_count; i++)
    {
      AndamentoPanel p = {0}, q = doc.panels[i];
      andamento_arrangement_panel(stored, i, &p);
      same = (str8_match(uishell_sidebar_string(p.id), uishell_sidebar_string(q.id), 0) && p.parent == q.parent && p.kind == q.kind &&
              (p.kind != ANDAMENTO_PANEL_SPLIT || p.axis == q.axis) && (i == 0 || abs_f32((F32)(p.weight-q.weight)) < 1e-6f) &&
              p.tab_count == q.tab_count && p.first_tab == q.first_tab && p.selected == q.selected);
    }
    for(U64 i = 0; same && i < info.tab_count; i++)
    {
      AndamentoTab t = {0};
      andamento_arrangement_tab(stored, i, &t);
      same = !t.gone && str8_match(uishell_sidebar_string(t.slot), doc.keys[i], 0);
    }
    if(!same) { fprintf(stderr, "Workspace %.*s is not the arrangement Andamento keeps\n", str8_varg(rd_label_from_cfg(owner))); state_failures++; }
    andamento_arrangement_release(stored);
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
  AndamentoArrangement *stored = andamento_sidebar_arrangement_acquire(state->core, 0);
  AndamentoArrangementInfo info = {0};
  B32 same = stored && andamento_arrangement_info(stored, &info) && info.owned &&
    info.panel_count == doc.panel_count && info.tab_count == doc.tab_count &&
    andamento_arrangement_floating_first(stored) == doc.floating_first &&
    str8_match(uishell_store_setting(window, str8_lit("sidebar_generation")), push_str8f(scratch.arena, "%I64u", info.generation), 0);
  for(U64 i = 0; same && i < info.panel_count; i++)
  {
    AndamentoPanel p = {0}, q = doc.panels[i];
    andamento_arrangement_panel(stored, i, &p);
    same = (str8_match(uishell_sidebar_string(p.id), uishell_sidebar_string(q.id), 0) && p.parent == q.parent && p.kind == q.kind &&
            (p.kind != ANDAMENTO_PANEL_SPLIT || p.axis == q.axis) && abs_f32((F32)(p.weight-q.weight)) < 1e-6f &&
            p.tab_count == q.tab_count && (p.kind != ANDAMENTO_PANEL_TABS || p.first_tab == q.first_tab) && p.selected == q.selected);
  }
  for(U64 i = 0; same && i < info.tab_count; i++)
  {
    AndamentoTab t = {0};
    andamento_arrangement_tab(stored, i, &t);
    same = str8_match(uishell_sidebar_string(t.slot), doc.keys[i], 0);
  }
  if(!same) { fprintf(stderr, "The sidebar is not the arrangement Andamento keeps\n"); state_failures++; }
  andamento_arrangement_release(stored);
  scratch_end(scratch);
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

internal void
entry_point(CmdLine *cmdline)
{
  String8 dir = cmd_line_string(cmdline, str8_lit("state_dir"));
  if(!dir.size) { fprintf(stderr, "state behaviour needs --state_dir:DIR --user:FILE --dashboard:DIR\n"); abort_self(2); }
  // As --sidebar_subject_fixture: the daily-driver sidebar with fixture facts.
  uishell_sidebar_fixture = uishell_sidebar_subject_fixture = 1;
  wm_init(); fp_init(); r_init(cmdline); fnt_init(); rd_init(cmdline);
  // Every registered Renderer except visualizers renders, so docked
  // sections render when the sidebar is drawn (state_sidebar_draw).
  rd_state->headless_views = 1;
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
  CFG_PanelTree tree = state_panels(scratch.arena, work);
  CFG_Node *editor_panel = tree.root->first->cfg, *output_panel = tree.root->last->cfg;
  CFG_Node *output = output_panel->first;
  uishell_cmd("build_tab", .window = window->id, .panel = editor_panel->id, .string = str8_lit("terminal"), .expr = str8_lit("make test"));
  state_frame(ws);
  // A new tab is the panel's last; its node sits among the panel's tabs.
  tree = state_panels(scratch.arena, work);
  CFG_Node *terminal = cfg_panel_node_from_tree_cfg(tree.root, editor_panel)->tabs.last->v;
  StateCheck(str8_match(terminal->string, str8_lit("terminal"), 0));
  // A launch directory comes from a provider's recipe or hand editing; no
  // command sets one.
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, terminal, str8_lit("cwd")), str8_lit("/src/wheelhouse"));
  uishell_cmd("split_panel", .window = window->id, .panel = editor_panel->id, .view = terminal->id,
              .dst_panel = editor_panel->id, .dir2 = Dir2_Down);
  state_frame(ws);
  CFG_Node *terminal_panel = terminal->parent;
  StateCheck(terminal_panel != editor_panel);
  // Moving the last tab out of a panel closes it.
  uishell_cmd("move_view", .window = window->id, .panel = output_panel->id, .view = output->id,
              .dst_panel = terminal_panel->id, .prev_tab = terminal->id);
  state_frame(ws);
  StateCheck(output->parent == terminal_panel);
  // A Jackstay View in the other one.
  CFG_Node *notes_panel = state_panels(scratch.arena, notes).root->first->cfg;
  uishell_cmd("build_tab", .window = window->id, .panel = notes_panel->id, .string = str8_lit("jackstay"));
  state_frame(ws);
  tree = state_panels(scratch.arena, notes);
  CFG_Node *jackstay = cfg_panel_node_from_tree_cfg(tree.root, notes_panel)->tabs.last->v;
  StateCheck(str8_match(jackstay->string, str8_lit("jackstay"), 0));
  // Its address field saves the endpoint on Connect; no command does.
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, jackstay, str8_lit("source_endpoint")), str8_lit("porthole-demo"));

  //- Subject workspaces opened from their rows: one whose layout the person
  // changes and whose subject then ends, one closed but kept, and one whose
  // subject the producer stops reporting while it is open.
  CFG_Node *multi = state_open_subject(ws, str8_lit("vessel"), str8_lit("multi"));
  tree = state_panels(scratch.arena, multi);
  CFG_Node *tools = tree.root->last->tabs.last->v;
  uishell_cmd("focus_tab", .window = window->id, .panel = tools->parent->id, .tab = tools->id);
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
    CFG_Node *section = uishell_sidebar_local_section(window, uishell_sidebar_local_key_id(uishell_sidebar_section_key(view)));
    // Its name field opened; typing a name and Enter applies it.
    uishell_sidebar_local_rename(section, str8_lit("Builds"));
    state->rename_node = 0;
    CFG_Node *group = uishell_sidebar_local_only_group(section);
    uishell_sidebar_make(window, (UIShell_MakeAction){UIShell_Make_Workspace, str8_lit("New workspace"), group->id}, str8_lit("Build logs"));
    state_frame(ws);
    // Pin on the project's hover card.
    AndamentoEntity path[] = {{uishell_sidebar_text(str8_lit("project")), uishell_sidebar_text(str8_lit("p"))}};
    UIShell_HoverCard card = {.path = path, .depth = 1, .retained_label = str8_lit("Example project")};
    CFG_Node *pin = uishell_sidebar_card_pin(ws, &card, 0);
    StateCheck(pin != &cfg_nil_node && pin->parent == group);
    state_frame(ws);
  }

  //- Docked sections: Git closed, Attention collapsed.
  {
    String8 git = state_section_key(scratch.arena, state, str8_lit("Git"));
    CFG_Node *view = uishell_sidebar_region_view(window, git);
    StateCheck(view != &cfg_nil_node);
    uishell_cmd("close_tab", .window = window->id, .panel = view->parent->id, .tab = view->id);
    state_frame(ws);
    StateCheck(uishell_sidebar_region_view(window, git) == &cfg_nil_node);
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
    CFG_PanelNode *leaf = &cfg_nil_panel_node;
    F32 sum = 0, header_h = 0;
    B32 collapsed = 0;
    UI_IconInfo icons = {0}; UI_AnimationInfo animation = {0}; UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    UI_FontSize(11)
    {
      uishell_sidebar_size_panels(&split, &mount, r2f32p(0, 0, 300, 600));
      for(CFG_PanelNode *p = mount.panel_tree.root->first; p != &cfg_nil_panel_node; p = p->next)
      {
        sum += p->pct_of_parent;
        if(p->selected_tab == attention) { leaf = p; }
      }
      collapsed = uishell_sidebar_panel_collapsed(leaf, uishell_sidebar_row_height(), &header_h);
    }
    ui_end_build();
    StateCheck(leaf != &cfg_nil_panel_node && collapsed);
    StateCheck(abs_f32(leaf->pct_of_parent*600.f - header_h) < 1.f && abs_f32(sum-1.f) < .001f);
  }

  //- Presentation: the split local workspace visible, its terminal focused.
  uishell_cmd("select_workspace", .window = window->id, .cfg = work->id);
  uishell_cmd("focus_panel", .window = window->id, .panel = terminal_panel->id);
  state_frame(ws);
  state_write(dir, "before_restart.txt");
  String8List ids_before = state_workspace_ids(scratch.arena);
  StateCheck(ids_before.node_count == 7);

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
    CFG_PanelNode *first = mount.panel_tree.root->first;
    CFG_Node *first_cfg = first->cfg;
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
    CFG_PanelNode *first = mount.panel_tree.root->first;
    CFG_Node *first_cfg = first->cfg;
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
    CFG_PanelTree panels = state_panels(scratch.arena, workspace);
    CFG_Node *make = &cfg_nil_node;
    for(CFG_PanelNode *p = panels.root; p != &cfg_nil_panel_node; p = cfg_panel_node_rec__depth_first_pre(panels.root, p).next)
    { for(CFG_NodePtrNode *t = p->tabs.first; t; t = t->next) { if(str8_match(t->v->string, str8_lit("terminal"), 0)) { make = t->v; } } }
    StateCheck(make != &cfg_nil_node);
    uishell_cmd("focus_tab", .window = window->id, .panel = make->parent->id, .tab = make->id);
    state_frame(ws);
    state_frame(ws);
    String8 text = state_workspaces_text(scratch.arena);
    StateCheck(str8_find_needle(text, 0, str8_lit("tab placeholder slot=\"u:web\" content=\"https://example.com/board\""), 0) < text.size);
    state_check_committed(ws);
    AndamentoSlots *slots = andamento_slots_acquire(state->core, id, 0);
    AndamentoSlot slot = {0};
    StateCheck(uishell_store_slot_find(slots, str8_lit("u:web"), &slot) && slot.spec.content == ANDAMENTO_SLOT_URL &&
               str8_match(uishell_sidebar_string(slot.spec.url), str8_lit("https://example.com/board"), 0));
    andamento_slots_release(slots);
  }

  //- A commit made against a generation that has moved since (here another
  // host committed in between) is made again at the new generation: the
  // edit is kept, whole.
  {
    state = ws->sidebar;
    window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *workspace = state_workspace_labelled(window, str8_lit("Workspace 1"));
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(workspace));
    AndamentoArrangement *stored = andamento_arrangement_acquire(state->core, id, 0);
    AndamentoArrangementInfo info = {0};
    StateCheck(stored && andamento_arrangement_info(stored, &info) && info.panel_count == 3);
    AndamentoPanel panels[3] = {0};
    AndamentoTab *tabs = push_array(scratch.arena, AndamentoTab, info.tab_count);
    for(U64 i = 0; i < info.panel_count && i < 3; i++) { andamento_arrangement_panel(stored, i, &panels[i]); }
    for(U64 i = 0; i < info.tab_count; i++) { andamento_arrangement_tab(stored, i, &tabs[i]); }
    panels[1].weight = panels[2].weight = 0.5;
    StateCheck(andamento_set_arrangement(state->core, id, panels, 3, tabs, info.tab_count, info.generation, 0, 0) == ANDAMENTO_ARRANGEMENT_COMMITTED);
    andamento_arrangement_release(stored);
    CFG_PanelTree tree = state_panels(scratch.arena, workspace);
    CFG_Node *first_cfg = tree.root->first->cfg;
    String8 weight = push_str8_copy(scratch.arena, first_cfg->string);
    CFG_Node *other = tree.root->first->tabs.first->v == tree.root->first->selected_tab ? tree.root->first->tabs.last->v : tree.root->first->tabs.first->v;
    U64 commits = state->store_commits;
    uishell_cmd("focus_tab", .window = window->id, .panel = first_cfg->id, .tab = other->id);
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
    AndamentoSlots *slots = andamento_slots_acquire(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(governor)), 0);
    AndamentoSlot slot = {0};
    StateCheck(uishell_store_slot_find(slots, str8_lit("primary"), &slot) && slot.in_baseline && !slot.detached &&
               slot.spec.content == ANDAMENTO_SLOT_FACET);
    andamento_slots_release(slots);
    state_check_committed(ws);
    // Back to the workspace that was visible.
    window = cfg_node_from_id(ws->cfg_id);
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
    String8 path = push_str8f(scratch.arena, "%S/dashboard.kdl", state->records_dir);
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
    StateCheck(!str8_match(after, before, 0) && str8_find_needle(after, 0, str8_lit("display \"show-role-attempts\" true"), 0) < after.size);
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
    StateCheck(str8_find_needle(presentation, 0, str8_lit("Notes"), 0) < presentation.size);
    StateCheck(file_path_exists(push_str8f(scratch.arena, "%S/dashboard.kdl", dashboard_a)));
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
    StateCheck(file_path_exists(push_str8f(scratch.arena, "%S/workspace-%S.kdl", dashboard_b, only_b_id)));
    StateCheck(!file_path_exists(push_str8f(scratch.arena, "%S/workspace-%S.kdl", dashboard_a, only_b_id)));
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
    String8 saved = data_from_file_path(scratch.arena, push_str8f(scratch.arena, "%S/subscriptions.kdl", dashboard->dir));
    StateCheck(str8_find_needle(saved, 0, push_str8f(scratch.arena, "subscription \"%S\" {\n    kind \"flotilla\"\n    daemon \"ssh://alpha\"\n}", alpha_id), 0) < saved.size);
    StateCheck(str8_find_needle(saved, 0, beta_id, 0) < saved.size);
    String8 project = str8_lit("project"), fleet = str8_lit("fleet"), vessel = str8_lit("vessel"), fleet_v = str8_lit("fleet-v");
    StateWaitFor(ws, state_count_from(ws->sidebar, project, fleet, alpha_id, 0) == 1 && state_count_from(ws->sidebar, project, fleet, beta_id, 0) == 1 &&
                     state_count_from(ws->sidebar, vessel, fleet_v, alpha_id, 0) >= 1 && state_count_from(ws->sidebar, vessel, fleet_v, beta_id, 0) >= 1);
    StateCheck(file_path_exists(push_str8f(scratch.arena, "%S/flotilla-%S.log", dir, alpha_id)));
    // Opening alpha's vessel opens a workspace on it, not on beta's.
    size_t activate = ANDAMENTO_NONE;
    for(U64 i = 0; i < andamento_snapshot_node_count(ws->sidebar->snapshot); i++)
    {
      AndamentoNode node = {0}; AndamentoText from = {0};
      uishell_sidebar_snapshot_node(ws->sidebar->snapshot, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity_id), fleet_v, 0) && andamento_snapshot_node_provider(ws->sidebar->snapshot, i, &from, 0) &&
         str8_match(uishell_sidebar_string(from), alpha_id, 0) && node.openable) { activate = node.activate; break; }
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

    //- Removing a subscription stops its connector and retracts its facts;
    // the workspace open on its vessel stays, retained.
    uishell_cmd("remove_subscription", .window = window->id, .string = alpha_id);
    state_pump();
    StateWaitFor(ws, state_count_from(ws->sidebar, vessel, fleet_v, beta_id, 0) >= 1);
    StateCheck(uishell_subscriptions.count == 1 && uishell_subscription_from_text(alpha_id) == 0);
    StateCheck(state_count_from(ws->sidebar, vessel, fleet_v, alpha_id, 0) >= 1);
    StateCheck(cfg_node_from_id(ws->root_controlled_split_selected_workspace_id) == opened);
    saved = data_from_file_path(scratch.arena, push_str8f(scratch.arena, "%S/subscriptions.kdl", dashboard->dir));
    StateCheck(str8_find_needle(saved, 0, alpha_id, 0) == saved.size && str8_find_needle(saved, 0, beta_id, 0) < saved.size);
    state_write(dir, "subscriptions_removed.txt");

    //- A restart opens the Dashboard's subscriptions again and reconnects them.
    ws = state_restart(ws, &theme, push_str8_copy(scratch.arena, dashboard->dir));
    StateCheck(uishell_subscriptions.count == 1 && str8_match(uishell_subscriptions.first->id, beta_id, 0));
    StateWaitFor(ws, state_count_from(ws->sidebar, project, fleet, beta_id, 0) == 1 &&
                     state_count_from(ws->sidebar, vessel, fleet_v, beta_id, 0) >= 1);
    state_write(dir, "subscriptions_restarted.txt");
    uishell_subscriptions_close();
  }

  scratch_end(scratch);
  fprintf(stderr, "State behaviour: %u failures\n", state_failures);
  abort_self(state_failures ? 1 : 0);
}
