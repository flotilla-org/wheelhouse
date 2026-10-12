// Compiled by tools/test-state-behaviour.py from the production amalgamation
// with its entry point replaced, linked against real Andamento and Cleat, with
// the headless WM/renderer/font backends. It builds state through production
// commands and sidebar actions, writes the logical state
// (uishell_logical_state.c), restarts, and writes it again; the script
// compares both with goldens.
//
// A restart is the production one in-process: autosave writes the user file
// (rd_autosave), the window state and its Andamento core are released
// as a frame does once their config is gone, the initial load reopens the file
// (UISHELL_APP_INITIAL_LOAD, which repairs layouts), and a fresh core runs the
// sidebar's first-frame path. Config is edited directly only where no command
// or sidebar action exists; each such place says so.
//
// The fixture's producer is Andamento's patch ABI: the sidebar fixture's facts
// (as --sidebar_subject_fixture), plus what the scenario's producer changes
// later. A restarted producer reports its current state again.

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

// A window's first frame: a new window state and Andamento core (fixture
// facts, saved orders and display values), what the producer reports now,
// then the sidebar's first build: observe, rebind saved subject workspaces
// (uishell_sidebar_restore) and reconcile docked sections.
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
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  uishell_sidebar_observe(state, &split);
  uishell_sidebar_refresh(state);
  uishell_sidebar_restore(state, &split);
  scratch_end(scratch);
  state_frame(ws);
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

internal void
entry_point(CmdLine *cmdline)
{
  String8 dir = cmd_line_string(cmdline, str8_lit("state_dir"));
  if(!dir.size) { fprintf(stderr, "state behaviour needs --state_dir:DIR --user:FILE\n"); abort_self(2); }
  // As --sidebar_subject_fixture: the daily-driver sidebar with fixture facts.
  uishell_sidebar_fixture = uishell_sidebar_subject_fixture = 1;
  wm_init(); fp_init(); r_init(cmdline); fnt_init(); rd_init(cmdline);
  rd_state->view_ui_rule_map = rd_view_ui_rule_map_make(rd_state->arena, 512);
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
    uishell_sidebar_publish_local(state, &split);
    uishell_sidebar_set_order(state, window, loop, order, count);
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
  StateCheck(rd_autosave());
  state_pump();
  rd_window_state_release(ws);
  String8 user_path = push_str8_copy(scratch.arena, rd_state->user_path);
  String8 project_path = push_str8_copy(scratch.arena, rd_state->project_path);
  UISHELL_APP_INITIAL_LOAD(user_path, project_path);
  state_pump();
  ws = state_open_window(&theme);
  state_write(dir, "after_restart.txt");

  //- Every workspace, kept ones included, has the ID it had.
  String8List ids_after = state_workspace_ids(scratch.arena);
  StateCheck(ids_after.node_count == ids_before.node_count);
  for(String8Node *a = ids_before.first, *b = ids_after.first; a && b; a = a->next, b = b->next)
  {
    if(!str8_match(a->string, b->string, 0))
    { fprintf(stderr, "Workspace ID changed across restart: %.*s -> %.*s\n", str8_varg(a->string), str8_varg(b->string)); state_failures++; }
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

  //- A local workspace saved before Workspace IDs, whose sidebar entity was
  // a GUID of its own: its ID takes the GUID's place wherever the window
  // names it (as a pin's or a saved order's entity).
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

  scratch_end(scratch);
  fprintf(stderr, "State behaviour: %u failures\n", state_failures);
  abort_self(state_failures ? 1 : 0);
}
