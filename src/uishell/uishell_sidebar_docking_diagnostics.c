// Exercise the shared panel host, not the former stacked sidebar adapter.
internal void
uishell_sidebar_docking_drain(UIShell_CmdNode *before)
{
  for(UIShell_CmdNode *n = before ? before->next : rd_state->cmds[0].first; n; n = n->next) UIShell_RegsScope()
  {
    MemoryCopyStruct(uishell_regs(), n->cmd.regs);
    if(!uishell_dispatch_tab_command(n->cmd.name)) { uishell_dispatch_panel_command(n->cmd.name); }
  }
}

internal B32
uishell_sidebar_docking_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *saved_host = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
  cfg_node_unhook(rd_state->cfg, window, saved_host);
  CFG_Node *saved_positions = cfg_node_child_from_string(window, UISHELL_REGION_INVENTORY);
  if(saved_positions != &cfg_nil_node) { cfg_node_unhook(rd_state->cfg, window, saved_positions); }
  U32 failures = 0;
#define DockFailure(expr) do { if(expr) { failures++; fprintf(stderr, "FAIL sidebar docking line %u: %s\n", __LINE__, #expr); } } while(0)
  CFG_Node *saved_axis = cfg_node_child_from_string(window, str8_lit("control_views_split_x"));
  CFG_Node *saved_sizing = cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized"));
  CFG_Node *saved_display = cfg_node_child_from_string(window, str8_lit("sidebar_display"));
  cfg_node_unhook(rd_state->cfg, window, saved_axis);
  cfg_node_unhook(rd_state->cfg, window, saved_sizing);
  cfg_node_unhook(rd_state->cfg, window, saved_display);
  UIShell_SidebarState *saved_sidebar = ws->sidebar;
  B32 saved_fixture = uishell_sidebar_fixture, saved_subject = uishell_sidebar_subject_fixture;
  uishell_sidebar_fixture = 1; uishell_sidebar_subject_fixture = 0;
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  UIShell_SidebarState unavailable = {.initialized = 1};
  ws->sidebar = &unavailable;
  CFG_Node *empty_saved = cfg_node_new(rd_state->cfg, window, str8_lit("sidebar_display"));
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, empty_saved, str8_lit("show-issues")), str8_lit("false"));
  U64 before_unavailable = cfg_change_gen();
  uishell_sidebar_restore_display(&unavailable, window);
  DockFailure(uishell_sidebar_dock_layout(&split) != &cfg_nil_node);
  DockFailure(cfg_change_gen() != before_unavailable);
  cfg_node_release(rd_state->cfg, empty_saved);
  ws->sidebar = 0;
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  CFG_Node *host = uishell_sidebar_dock_layout(&split);
  DockFailure(host == &cfg_nil_node);
  CFG_Node *first_panel = host->first, *second_panel = first_panel->next;
  CFG_Node *view = cfg_node_child_from_string(first_panel, str8_lit("sidebar_section"));
  CFG_Node *second_view = cfg_node_child_from_string(second_panel, str8_lit("sidebar_section"));
  CFG_ID view_id = view->id;
  String8 key = push_str8_copy(scratch.arena, cfg_node_child_from_string(view, str8_lit("section"))->first->string);
  String8 second_key = push_str8_copy(scratch.arena, cfg_node_child_from_string(second_view, str8_lit("section"))->first->string);
  UI_Key roots[2] = {ui_key_from_stringf(ui_key_zero(), "andamento_section_%S", key),
                     ui_key_from_stringf(ui_key_zero(), "andamento_section_%S", second_key)};
  UI_Key bodies[2] = {ui_key_from_stringf(roots[0], "section_body_%S", key),
                      ui_key_from_stringf(roots[1], "section_body_%S", second_key)};
  UI_State *saved_ui = ui_state, *test_ui = ui_state_alloc();
  ui_select_state(test_ui);
  CFG_ID saved_sidebar_focus = ws->active_panel_id;
  // Reconciliation also places the synthetic unplaced-workspaces region after
  // fixture observation. Leave room for all four headers and scroll bodies.
  F32 expected[2] = {0};
  U64 settled_cfg_gen = 0;
  for(U32 frame = 0; frame < 36; frame++)
  {
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0}; animation.scroll_animation_rate = .5f;
    UI_EventList events = {0}; UI_EventNode node = {0};
    if(frame >= 4 && frame < 20)
    {
      U32 which = (frame/4)%2;
      UI_Box *body = ui_box_from_key(bodies[which]);
      F32 delta = frame < 12 ? .25f : -.125f;
      node.v = (UI_Event){.kind = UI_EventKind_Scroll, .pos = center_2f32(body->rect),
                         .delta_2f32 = {0, delta}, .scroll_is_precise = 1};
      events.first = events.last = &node; events.count = 1; expected[which] += delta;
    }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = node.v.pos;
    UIShell_RegsScope(.window = window->id) UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_control_surface_ui(r2f32p(17, 29, 337, 229), &split); }
    ui_end_build();
    for(U32 i = 0; i < 2; i++)
    {
      UI_Box *body = ui_box_from_key(bodies[i]);
      DockFailure(ui_box_is_nil(body));
      if(!ui_box_is_nil(body))
      {
        DockFailure(body->rect.x0 < 17 || body->rect.x1 > 337 || body->rect.y0 < 29 || body->rect.y1 > 229);
        if(frame >= 4 && abs_f32(body->view_off_target.y-expected[i]) > .00001f)
        { fprintf(stderr, "FAIL dock scroll frame %u section %u: %g != %g\n", frame, i, body->view_off_target.y, expected[i]); failures++; }
      }
    }
    DockFailure(events.count != 0);
    if(frame == 25) { settled_cfg_gen = cfg_change_gen(); }
    if(frame > 25) { DockFailure(cfg_change_gen() != settled_cfg_gen); }
    UI_Key tabbar = ui_key_from_stringf(ui_key_zero(), "tab_bar_%p", first_panel);
    DockFailure(!ui_box_is_nil(ui_box_from_key(tabbar)));
  }
  // Drive a real boundary drag, serialize its manual allocation, then
  // double-click the boundary to return to content-based default sizing.
  F32 default_pct = (F32)f64_from_str8(first_panel->string);
  UI_Key boundary_key = ui_key_from_stringf(ui_state->root->key, "###%p_%p", first_panel, second_panel);
  Vec2F32 drag_start = {0};
  for(U32 frame = 0; frame < 12; frame++)
  {
    UI_Box *boundary = ui_box_from_key(boundary_key);
    DockFailure(ui_box_is_nil(boundary));
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    UI_EventList events = {0}; UI_EventNode event = {0};
    if(frame == 1 || frame == 2 || frame == 3 || frame == 7 || frame == 8 || frame == 9 || frame == 10)
    {
      if(frame == 1) { drag_start = center_2f32(boundary->rect); }
      event.v = (UI_Event){.key = WM_Key_LeftMouseButton,
        .kind = frame == 2 ? UI_EventKind_MouseMove : (frame == 1 || frame == 7 || frame == 9 ? UI_EventKind_Press : UI_EventKind_Release),
        .pos = frame < 4 ? add_2f32(drag_start, v2f32(0, frame == 1 ? 0 : 8)) : center_2f32(boundary->rect),
        .timestamp_us = frame < 4 ? 2000000+frame*20000 : 3000000+(frame-7)*50000};
      events.first = events.last = &event; events.count = 1;
    }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = events.count ? event.v.pos : v2f32(-100, -100);
    UIShell_RegsScope(.window = window->id) UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_control_surface_ui(r2f32p(17, 29, 337, 229), &split); }
    ui_end_build();
    if(frame == 4)
    {
      DockFailure(cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")) == &cfg_nil_node);
      DockFailure(abs_f32((F32)f64_from_str8(first_panel->string)-default_pct) < .0001f);
      CFG_State *loaded_cfg = cfg_state_alloc();
      String8 text = cfg_string_from_tree(scratch.arena, rd_state->cfg_schema_table, str8_zero(), window);
      CFG_NodePtrList loaded = cfg_node_ptr_list_from_string(scratch.arena, loaded_cfg, rd_state->cfg_schema_table, str8_zero(), text);
      DockFailure(loaded.count != 1);
      CFG_Node *restored_window = loaded.first->v;
      CFG_Node *restored_root = cfg_node_child_from_string(restored_window, RD_DOCK_SIDEBAR_ROOT);
      DockFailure(cfg_node_child_from_string(restored_window, str8_lit("sidebar_layout_sized")) == &cfg_nil_node);
      DockFailure(!str8_match(restored_root->first->string, first_panel->string, 0));
      UIShell_ControlledSplit restored_split = {.owner_cfg = restored_window};
      UIShell_WorkspaceMount restored_mount = uishell_workspace_mount_from_owner_cfg(scratch.arena, restored_window, restored_root);
      uishell_sidebar_size_panels(&restored_split, &restored_mount, r2f32p(17, 29, 337, 229));
      DockFailure(!str8_match(restored_root->first->string, first_panel->string, 0));
      cfg_state_release(loaded_cfg);
    }
  }
  DockFailure(cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")) != &cfg_nil_node);
  DockFailure(abs_f32((F32)f64_from_str8(first_panel->string)-default_pct) > .0001f);
  // Merge through the production command, then split the merged panel in the
  // opposite direction. This exercises host-root retention on split/collapse.
  UIShell_CmdNode *before = rd_state->cmds[0].last;
  UIShell_RegsScope(.window = window->id, .panel = second_panel->id, .view = second_view->id,
                   .dst_panel = first_panel->id, .prev_tab = view->id)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  uishell_sidebar_docking_drain(before);
  DockFailure(second_view->parent != view->parent);
  UIShell_WorkspaceMount merged = uishell_workspace_mount_from_cfg(scratch.arena, view);
  DockFailure(rd_dock_presentation(RD_DockHostKind_Sidebar,
    cfg_panel_node_from_tree_cfg(merged.panel_tree.root, view->parent)->tabs.count) != RD_DockPresentation_CompactTabs);
  before = rd_state->cmds[0].last;
  UIShell_RegsScope(.window = window->id, .panel = second_view->parent->id, .view = second_view->id,
                   .dst_panel = view->parent->id, .dir2 = Dir2_Right)
  { uishell_dispatch_panel_command(str8_lit("split_panel")); }
  uishell_sidebar_docking_drain(before);
  DockFailure(view->parent == second_view->parent);
  DockFailure(rd_dock_host_from_cfg(second_view, 320).kind != RD_DockHostKind_Sidebar);
  DockFailure(cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT) == &cfg_nil_node);
  // Child workspace targets must stay absent, and commands enforce the same
  // level boundary even when called without a drag. Old previews' saved
  // cross-level placements must recover into the root Control Region.
  CFG_Node *workspace = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  CFG_Node *panels = cfg_node_new(rd_state->cfg, workspace, str8_lit("panels"));
  UIShell_WorkspaceMount child_mount = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, workspace);
  UIShell_RegsScope(.window = window->id, .panel = view->parent->id, .view = view->id)
  {
    rd_drag_begin(UIShell_ContextRegSlot_View);
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = v2f32(320, 240);
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11)
    { rd_panel_area_ui(scratch, r2f32p(0, 0, 640, 480), r2f32p(0, 0, 640, 480), ws, &child_mount, 1, 0, 0, 0, 0); }
    ui_end_build();
    char *names[] = {"center", "up", "down", "left", "right"};
    for(U32 i = 0; i < ArrayCount(names); i++)
    {
      UI_Key site = ui_key_from_stringf(ui_key_zero(), "drop_split_%s_%p", names[i], panels);
      DockFailure(!ui_box_is_nil(ui_box_from_key(site)));
    }
    UI_Key catchall = ui_key_from_stringf(ui_key_zero(), "catchall_drop_site_%p", panels);
    DockFailure(!ui_box_is_nil(ui_box_from_key(catchall)));
    rd_drag_kill();
  }
  CFG_Node *original_panel = view->parent;
  U64 before_rejected = cfg_change_gen();
  log_scope_begin();
  before = rd_state->cmds[0].last;
  UIShell_RegsScope(.window = window->id, .panel = view->parent->id, .view = view->id,
                   .dst_panel = panels->id, .prev_tab = 0)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  uishell_sidebar_docking_drain(before);
  UIShell_RegsScope(.window = window->id, .panel = view->parent->id, .view = view->id,
                   .dst_panel = panels->id, .dir2 = Dir2_Right)
  { uishell_dispatch_panel_command(str8_lit("split_panel")); }
  LogScopeResult rejected = log_scope_end(scratch.arena);
  DockFailure(str8_find_needle(rejected.strings[LogMsgKind_UserError], 0,
    str8_lit("different Controlled Split level"), 0) == rejected.strings[LogMsgKind_UserError].size);
  DockFailure(view->parent != original_panel || cfg_change_gen() != before_rejected);
  cfg_node_unhook(rd_state->cfg, original_panel, view);
  cfg_node_insert_child(rd_state->cfg, panels, panels->last, view);
  rd_dock_restore_window(rd_state->cfg, window);
  DockFailure(view->parent == panels || view->id != view_id);
  DockFailure(rd_dock_host_from_cfg(view, 320).kind != RD_DockHostKind_Sidebar);
  CFG_Node *recovered_panel = view->parent;
  U64 recovered_gen = cfg_change_gen();
  rd_dock_restore_window(rd_state->cfg, window);
  DockFailure(view->parent != recovered_panel || cfg_change_gen() != recovered_gen);
  // Persistence crosses a new core instance, through the same code used at
  // application startup. Ephemeral declarations are excluded from storage.
  UIShell_SidebarState display = {0};
  String8 daily = str8_cstring((char *)uishell_sidebar_daily_config);
  char *error = 0;
  display.core = andamento_create(daily.str, daily.size, &error);
  DockFailure(!uishell_sidebar_result(&display, display.core != 0, error));
  uishell_sidebar_refresh(&display);
  AndamentoNode section = {0}; andamento_snapshot_node(display.snapshot, 0, &section);
  AndamentoControl control = {0};
  andamento_snapshot_control(display.snapshot, section.first_control, &control);
  B32 default_value = control.checked;
  error = 0;
  DockFailure(!andamento_dispatch(display.core, display.snapshot, control.action, &error));
  if(error) { andamento_string_free(error); }
  uishell_sidebar_refresh(&display);
  uishell_sidebar_save_display(&display, window);
  CFG_State *persisted_cfg = cfg_state_alloc();
  String8 saved_text = cfg_string_from_tree(scratch.arena, rd_state->cfg_schema_table, str8_zero(), window);
  CFG_NodePtrList loaded_display = cfg_node_ptr_list_from_string(scratch.arena, persisted_cfg, rd_state->cfg_schema_table, str8_zero(), saved_text);
  DockFailure(loaded_display.count != 1);
  uishell_sidebar_release(&display); MemoryZeroStruct(&display);
  error = 0; display.core = andamento_create(daily.str, daily.size, &error);
  DockFailure(!uishell_sidebar_result(&display, display.core != 0, error));
  uishell_sidebar_refresh(&display);
  uishell_sidebar_restore_display(&display, loaded_display.first->v);
  cfg_state_release(persisted_cfg);
  andamento_snapshot_node(display.snapshot, 0, &section);
  andamento_snapshot_control(display.snapshot, section.first_control, &control);
  DockFailure(!!control.checked == default_value);
  // Exercise real header buttons at both acceptance widths, including their
  // pressed appearance and dispatch through the native UI event path.
  ws->sidebar = &display; display.initialized = 1;
  UI_Key header_key = ui_key_from_stringf(roots[0], "###section_header_%S", key);
  F32 widths[] = {260, 600};
  for(U64 w = 0; w < ArrayCount(widths); w++)
  {
    andamento_snapshot_control(display.snapshot, section.first_control, &control);
    B32 before_click = !!control.checked;
    for(U32 frame = 0; frame < 5; frame++)
    {
      UI_IconInfo icons = ws->ui->icon_info;
      UI_AnimationInfo animation = {0};
      UI_EventList events = {0}; UI_EventNode event = {0};
      UI_Key button_key = ui_key_from_stringf(header_key, "###control_%S_0", key);
      if(frame == 2 || frame == 3)
      {
        UI_Box *button = ui_box_from_key(button_key);
        event.v = (UI_Event){.kind = frame == 2 ? UI_EventKind_Press : UI_EventKind_Release,
                            .key = WM_Key_LeftMouseButton, .pos = center_2f32(button->rect)};
        events.first = events.last = &event; events.count = 1;
      }
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      // (0, 0) is inside this header. Idle frames must use an explicit
      // point outside the panel rather than relying on native window geometry.
      ui_state->mouse = events.count ? event.v.pos : v2f32(-100, -100);
      UIShell_RegsScope(.window = window->id, .panel = view->parent->id, .view = view->id)
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
      { uishell_sidebar_render(r2f32p(0, 0, widths[w], 180), &split,
                                (UIShell_SidebarRenderParams){UIShell_SidebarRenderMode_SectionPanel, key}); }
      ui_end_build();
      UI_Box *close = ui_box_from_key(ui_key_from_string(header_key, str8_lit("###section_close")));
      DockFailure(ui_box_is_nil(close));
      if(frame == 1) { DockFailure(ui_box_display_string(close).size != 0); }
      if(frame == 2) { DockFailure(ui_box_display_string(close).size == 0); }
      F32 previous_right = 0;
      for(U64 c = 0; c < section.control_count; c++)
      {
        UI_Box *button = ui_box_from_key(ui_key_from_stringf(header_key, "###control_%S_%I64u", key, c));
        DockFailure(ui_box_is_nil(button));
        DockFailure(button->rect.x0 < previous_right || button->rect.x1 > widths[w] || dim_2f32(button->rect).x <= 0);
        previous_right = button->rect.x1;
      }
    }
    andamento_snapshot_control(display.snapshot, section.first_control, &control);
    DockFailure(!!control.checked == before_click);
    UI_Box *button = ui_box_from_key(ui_key_from_stringf(header_key, "###control_%S_0", key));
    DockFailure(!!(button->flags & UI_BoxFlag_DrawBackground) != !!control.checked);
  }
  ws->sidebar = state;
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("sidebar_display")));
  String8 persisted = str8_lit("display-variable \"show-role-attempts\" type=\"bool\" default=false label=\"Role history\" icon=\"R\" persist=true");
  U64 declaration_at = str8_find_needle(daily, 0, persisted, 0);
  DockFailure(declaration_at == daily.size);
  String8 ephemeral = push_str8f(scratch.arena, "%Sdisplay-variable \"show-role-attempts\" type=\"bool\" default=false label=\"Role history\" icon=\"R\" persist=false%S",
                                 str8_prefix(daily, declaration_at), str8_skip(daily, declaration_at+persisted.size));
  error = 0;
  DockFailure(!andamento_configure(display.core, uishell_sidebar_text(ephemeral), &error));
  if(error) { andamento_string_free(error); }
  uishell_sidebar_refresh(&display);
  uishell_sidebar_save_display(&display, window);
  CFG_Node *storage = cfg_node_child_from_string(window, str8_lit("sidebar_display"));
  DockFailure(cfg_node_child_from_string(storage, str8_lit("show-issues")) == &cfg_nil_node);
  DockFailure(cfg_node_child_from_string(storage, str8_lit("show-role-attempts")) != &cfg_nil_node);
  uishell_sidebar_release(&display);
  cfg_node_release(rd_state->cfg, storage);
  if(saved_display != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, saved_display); }
  ws->active_panel_id = saved_sidebar_focus;
  cfg_node_release(rd_state->cfg, workspace);
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT));
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("control_views_split_x")));
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")));
  if(saved_axis != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, saved_axis); }
  if(saved_sizing != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, saved_sizing); }
  if(saved_host != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, saved_host); }
  CFG_Node *test_positions = cfg_node_child_from_string(window, UISHELL_REGION_INVENTORY);
  if(test_positions != &cfg_nil_node) { cfg_node_release(rd_state->cfg, test_positions); }
  if(saved_positions != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, saved_positions); }
  uishell_sidebar_release(state);
  ws->sidebar = saved_sidebar; uishell_sidebar_fixture = saved_fixture; uishell_sidebar_subject_fixture = saved_subject;
  ui_select_state(saved_ui); ui_state_release(test_ui);
  scratch_end(scratch);
  fprintf(stderr, "Sidebar docking diagnostics: %u failures\n", failures);
  return failures == 0;
#undef DockFailure
}

// Headless lifecycle scenarios use the real native snapshot and config codec.
internal B32
uishell_section_placement_diagnostics(String8 source_path)
{
  Temp scratch = scratch_begin(0, 0);
  RD_State *saved_rd = rd_state;
  CFG_Ctx *saved_ctx = cfg_ctx;
  RD_State state = {0}; rd_state = &state;
  state.arena = scratch.arena;
  state.cfg = cfg_state_alloc(); cfg_ctx_select(cfg_state_ctx(state.cfg));
  CFG_SchemaNode *slot = 0; CFG_SchemaTable schemas = {&slot, 1}; state.cfg_schema_table = &schemas;
  CFG_Node *user = cfg_node_new(state.cfg, cfg_node_root(), str8_lit("user"));
  CFG_Node *window = cfg_node_new(state.cfg, user, str8_lit("window"));
  UIShell_SidebarState sidebar = {.initialized = 1};
  RD_WindowState ws = {.cfg_id = window->id, .sidebar = &sidebar};
  state.window_state_last_accessed_id = window->id; state.window_state_last_accessed = &ws;
  UIShell_ControlledSplit split = {.owner_cfg = window};
  U32 failures = 0;
#define PlacementCheck(expr) do { if(!(expr)) { failures++; fprintf(stderr, "FAIL section placement line %u: %s\n", __LINE__, #expr); } } while(0)
  // The source-integrity integration runner supplies the shipped KDL. Build,
  // reorder and reset its real declarations before the generated scenarios.
  if(source_path.size)
  {
    String8 source = data_from_file_path(scratch.arena, source_path);
    PlacementCheck(source.size != 0);
    Andamento *source_core = andamento_create(source.str, source.size, 0);
    PlacementCheck(source_core != 0);
    if(source_core)
    {
      sidebar.snapshot = andamento_snapshot_acquire(source_core, 0);
      CFG_Node *source_host = uishell_sidebar_dock_layout(&split);
      PlacementCheck(source_host != &cfg_nil_node && source_host->first != &cfg_nil_node);
      if(source_host->first != &cfg_nil_node)
      {
        CFG_Node *moved = source_host->last;
        cfg_node_insert_child(state.cfg, source_host, &cfg_nil_node, moved);
        uishell_sidebar_dock_layout(&split);
        PlacementCheck(source_host->first == moved);
        String8 text = cfg_string_from_tree(scratch.arena, &schemas, str8_zero(), window);
        PlacementCheck(text.size != 0);
      }
      uishell_sidebar_reset_regions(window);
      uishell_sidebar_replace_snapshot(&sidebar, 0);
      andamento_destroy(source_core);
    }
  }
  String8 config = str8_lit(
    "region \"b\" root-template=\"flotilla/region/tree\" default-host=\"sidebar\" order=20\n"
    "region \"a\" root-template=\"flotilla/region/tree\" default-host=\"sidebar\" order=10\n");
  Andamento *core = andamento_create(config.str, config.size, 0);
  PlacementCheck(core != 0);
  sidebar.snapshot = andamento_snapshot_acquire(core, 0);
  CFG_Node *host = uishell_sidebar_dock_layout(&split);
  CFG_Node *a = uishell_sidebar_region_view(window, str8_lit("a"));
  CFG_Node *b = uishell_sidebar_region_view(window, str8_lit("b"));
  // Defaults are stable by hint, regardless of declaration order.
  PlacementCheck(a != &cfg_nil_node && b != &cfg_nil_node);
  PlacementCheck(host->first == a->parent && host->last == b->parent);
  CFG_ID a_id = a->id, b_id = b->id;
  // A user reorder changes only layout, and reconciliation is idempotent.
  cfg_node_insert_child(state.cfg, host, &cfg_nil_node, b->parent);
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(host->first == b->parent && a->id == a_id && b->id == b_id);
  U64 generation = cfg_change_gen(), cache_builds = sidebar.placement_cache_builds;
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(cfg_change_gen() == generation && sidebar.placement_cache_builds == cache_builds);
  // Reconciliation must retain an unrelated intentionally empty saved panel.
  CFG_Node *reserved_panel = cfg_node_new(state.cfg, host, str8_lit("0.25"));
  CFG_ID reserved_id = reserved_panel->id;
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(cfg_node_from_id(reserved_id) == reserved_panel);
  cfg_node_release(state.cfg, reserved_panel);
  // Restart through the actual serializer/parser retains that arrangement.
  String8 serialized = cfg_string_from_tree(scratch.arena, &schemas, str8_zero(), window);
  CFG_NodePtrList loaded = cfg_node_ptr_list_from_string(scratch.arena, state.cfg, &schemas, str8_zero(), serialized);
  PlacementCheck(loaded.count == 1);
  CFG_Node *restored = loaded.first->v; cfg_node_insert_child(state.cfg, user, user->last, restored);
  ws.cfg_id = restored->id; state.window_state_last_accessed_id = restored->id;
  split.owner_cfg = restored;
  CFG_Node *restored_host = uishell_sidebar_dock_layout(&split);
  PlacementCheck(str8_match(cfg_node_child_from_string(restored_host->first->first, str8_lit("section"))->first->string, str8_lit("b"), 0));
  // Adding a hinted region inserts it without changing the saved pair's order.
  String8 added = push_str8f(scratch.arena, "%Sregion \"c\" root-template=\"flotilla/region/tree\" order=15\n", config);
  PlacementCheck(andamento_configure(core, (AndamentoText){added.str, added.size}, 0));
  uishell_sidebar_replace_snapshot(&sidebar, andamento_snapshot_acquire(core, 0));
  uishell_sidebar_dock_layout(&split);
  CFG_Node *c = uishell_sidebar_region_view(restored, str8_lit("c"));
  a = uishell_sidebar_region_view(restored, str8_lit("a")); b = uishell_sidebar_region_view(restored, str8_lit("b"));
  PlacementCheck(c != &cfg_nil_node && c->parent->next == b->parent && b->parent->next == a->parent);
  // Closing a known section remains distinguishable from an unseen id.
  cfg_node_release(state.cfg, c);
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("c")) == &cfg_nil_node);
  CFG_Node *inventory = cfg_node_child_from_string(restored, UISHELL_REGION_INVENTORY);
  PlacementCheck(cfg_node_child_from_string(cfg_node_child_from_string(inventory, str8_lit("c")), str8_lit("closed")) != &cfg_nil_node);
  // Closed intent survives the same restart codec as saved positions.
  serialized = cfg_string_from_tree(scratch.arena, &schemas, str8_zero(), restored);
  loaded = cfg_node_ptr_list_from_string(scratch.arena, state.cfg, &schemas, str8_zero(), serialized);
  PlacementCheck(loaded.count == 1);
  restored = loaded.first->v; cfg_node_insert_child(state.cfg, user, user->last, restored);
  ws.cfg_id = restored->id; state.window_state_last_accessed_id = restored->id; split.owner_cfg = restored;
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("c")) == &cfg_nil_node);
  inventory = cfg_node_child_from_string(restored, UISHELL_REGION_INVENTORY);
  a = uishell_sidebar_region_view(restored, str8_lit("a"));
  // Removal drops the View and inventory, retaining the surviving View identity.
  CFG_ID kept = a->id;
  String8 removed = str8_lit("region \"a\" root-template=\"flotilla/region/tree\" order=10\n");
  PlacementCheck(andamento_configure(core, (AndamentoText){removed.str, removed.size}, 0));
  uishell_sidebar_replace_snapshot(&sidebar, andamento_snapshot_acquire(core, 0));
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("b")) == &cfg_nil_node);
  PlacementCheck(cfg_node_child_from_string(inventory, str8_lit("b")) == &cfg_nil_node);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("a"))->id == kept);
  // Reset clears closes and user positions, restoring all hints.
  PlacementCheck(andamento_configure(core, (AndamentoText){added.str, added.size}, 0));
  uishell_sidebar_replace_snapshot(&sidebar, andamento_snapshot_acquire(core, 0));
  uishell_sidebar_reset_regions(restored); restored_host = uishell_sidebar_dock_layout(&split);
  a = uishell_sidebar_region_view(restored, str8_lit("a")); b = uishell_sidebar_region_view(restored, str8_lit("b"));
  c = uishell_sidebar_region_view(restored, str8_lit("c"));
  PlacementCheck(restored_host->first == a->parent && a->parent->next == c->parent && c->parent->next == b->parent);
  // A saved user split remains intact when a new neighbour is hinted before
  // a View within it. The whole subtree is the insertion anchor.
  CFG_Node *nested = cfg_node_new(state.cfg, restored_host, str8_lit("0.5"));
  cfg_node_new(state.cfg, nested, str8_lit("split_x"));
  CFG_Node *nested_b = b->parent; CFG_ID nested_b_id = b->id;
  cfg_node_insert_child(state.cfg, nested, nested->last, nested_b);
  String8 nested_added = push_str8f(scratch.arena, "%Sregion \"d\" root-template=\"flotilla/region/tree\" order=17\n", added);
  PlacementCheck(andamento_configure(core, (AndamentoText){nested_added.str, nested_added.size}, 0));
  uishell_sidebar_replace_snapshot(&sidebar, andamento_snapshot_acquire(core, 0));
  uishell_sidebar_dock_layout(&split);
  CFG_Node *d = uishell_sidebar_region_view(restored, str8_lit("d"));
  PlacementCheck(d != &cfg_nil_node && d->parent->next == nested);
  PlacementCheck(b->id == nested_b_id && b->parent == nested_b && nested_b->parent == nested);
  PlacementCheck(str8_match(nested->string, str8_lit("0.5"), 0));
  // A saved section inside a child Workspace level is rejected by the shared
  // checker and moves to its hint, preserving identity and removing its empty panel.
  CFG_Node *invalid_workspace = cfg_node_new(state.cfg, restored_host, str8_lit("workspace"));
  CFG_ID invalid_workspace_id = invalid_workspace->id;
  CFG_Node *invalid_panels = cfg_node_new(state.cfg, invalid_workspace, str8_lit("panels"));
  CFG_Node *invalid_panel = cfg_node_new(state.cfg, invalid_panels, str8_lit("1"));
  CFG_ID invalid_panel_id = invalid_panel->id, invalid_view_id = a->id;
  CFG_Node *old_a_panel = a->parent;
  cfg_node_insert_child(state.cfg, invalid_panel, invalid_panel->last, a);
  cfg_node_release(state.cfg, old_a_panel);
  PlacementCheck(!rd_dock_saved_placement_valid(a));
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(a->id == invalid_view_id && rd_dock_saved_placement_valid(a));
  PlacementCheck(cfg_node_from_id(invalid_panel_id) == &cfg_nil_node);
  PlacementCheck(cfg_node_from_id(invalid_workspace_id) == &cfg_nil_node);
  // Legacy adoption closes currently absent ids only. A declaration added on
  // a subsequent update is new and appears without requiring reset or #183.
  cfg_node_release(state.cfg, cfg_node_child_from_string(restored, UISHELL_REGION_INVENTORY));
  cfg_node_release(state.cfg, d);
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("d")) == &cfg_nil_node);
  String8 post_upgrade = push_str8f(scratch.arena, "%Sregion \"e\" root-template=\"flotilla/region/tree\" order=25\n", nested_added);
  PlacementCheck(andamento_configure(core, (AndamentoText){post_upgrade.str, post_upgrade.size}, 0));
  uishell_sidebar_replace_snapshot(&sidebar, andamento_snapshot_acquire(core, 0));
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("e")) != &cfg_nil_node);
  // The empty authoritative declaration is a valid reconciler input, unlike a
  // missing native snapshot. Removal must clear positions and closed intent.
  uishell_sidebar_reconcile_regions(restored, 0, 0);
  PlacementCheck(cfg_node_child_from_string(restored, UISHELL_REGION_INVENTORY)->first == &cfg_nil_node);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("a")) == &cfg_nil_node);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("e")) == &cfg_nil_node);
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("d")) != &cfg_nil_node);
  // Generated host/order cases cover absent hints, equal hints, negative order,
  // opaque unknown hosts and the supported floating host. Every placement
  // passes the same validity checker used by drag and restore.
  for(U64 variant = 0; variant < 4; variant++)
  {
    char *attributes[] = {"", "default-host=\"unknown\" order=-10", "default-host=\"floating\" order=0", "order=0"};
    String8 generated = push_str8f(scratch.arena,
      "region \"x\" root-template=\"flotilla/region/tree\" %s\n"
      "region \"y\" root-template=\"flotilla/region/tree\" %s\n", attributes[variant], attributes[variant]);
    PlacementCheck(andamento_configure(core, (AndamentoText){generated.str, generated.size}, 0));
    uishell_sidebar_replace_snapshot(&sidebar, andamento_snapshot_acquire(core, 0));
    uishell_sidebar_reset_regions(restored); uishell_sidebar_dock_layout(&split);
    CFG_Node *x = uishell_sidebar_region_view(restored, str8_lit("x"));
    CFG_Node *y = uishell_sidebar_region_view(restored, str8_lit("y"));
    PlacementCheck(x != &cfg_nil_node && y != &cfg_nil_node);
    PlacementCheck(x->parent->next == y->parent);
    PlacementCheck(rd_dock_saved_placement_valid(x) && rd_dock_saved_placement_valid(y));
    PlacementCheck(rd_dock_host_from_cfg(x, RD_DOCK_UNMEASURED_WIDTH).kind ==
      (variant == 2 ? RD_DockHostKind_FloatingPanel : RD_DockHostKind_Sidebar));
    if(variant == 2)
    {
      // Startup first applies generic safety restore. A floating region hint
      // must still win when native reconciliation sees that recovered View.
      CFG_Node *floating = cfg_node_child_from_string(restored, str8_lit("floating_panels"));
      CFG_Node *bad_workspace = cfg_node_new(state.cfg, floating, str8_lit("workspace"));
      CFG_Node *bad_panels = cfg_node_new(state.cfg, bad_workspace, str8_lit("panels"));
      CFG_Node *bad_panel = cfg_node_new(state.cfg, bad_panels, str8_lit("1"));
      CFG_ID bad_id = bad_panel->id, x_id = x->id;
      CFG_Node *previous_panel = x->parent;
      cfg_node_insert_child(state.cfg, bad_panel, bad_panel->last, x);
      cfg_node_release(state.cfg, previous_panel);
      rd_dock_restore_window(state.cfg, restored);
      PlacementCheck(cfg_node_child_from_string(x, str8_lit("section_hint_pending")) != &cfg_nil_node);
      uishell_sidebar_dock_layout(&split);
      PlacementCheck(x->id == x_id && rd_dock_saved_placement_valid(x));
      PlacementCheck(rd_dock_host_from_cfg(x, RD_DOCK_UNMEASURED_WIDTH).kind == RD_DockHostKind_FloatingPanel);
      PlacementCheck(cfg_node_from_id(bad_id) == &cfg_nil_node);
      PlacementCheck(cfg_node_child_from_string(x, str8_lit("section_hint_pending")) == &cfg_nil_node);
      String8 changed_host = str8_lit("region \"x\" root-template=\"flotilla/region/tree\" default-host=\"sidebar\"\nregion \"y\" root-template=\"flotilla/region/tree\"\n");
      PlacementCheck(andamento_configure(core, (AndamentoText){changed_host.str, changed_host.size}, 0));
      uishell_sidebar_replace_snapshot(&sidebar, andamento_snapshot_acquire(core, 0));
      uishell_sidebar_dock_layout(&split);
      PlacementCheck(x->id == x_id && rd_dock_host_from_cfg(x, RD_DOCK_UNMEASURED_WIDTH).kind == RD_DockHostKind_FloatingPanel);
    }
  }
  // Rejected placement checks must leave no hosts/panels or generation churn.
  CFG_Node *reject_owner = cfg_node_new(state.cfg, user, str8_lit("window"));
  CFG_Node *unknown_view = cfg_node_new(state.cfg, reject_owner, str8_lit("unknown_view"));
  UIShell_SectionPlacement rejected = {str8_lit("reject"), str8_lit("Reject"), str8_lit("floating"), 0};
  U64 reject_generation = cfg_change_gen();
  for(U32 attempt = 0; attempt < 2; attempt++)
  { PlacementCheck(uishell_sidebar_place_region(reject_owner, &rejected, 1, 0, unknown_view) == unknown_view); }
  PlacementCheck(cfg_change_gen() == reject_generation && reject_owner->first == unknown_view && unknown_view->next == &cfg_nil_node);
  cfg_node_release(state.cfg, reject_owner);
  // Unusual numeric and reserved-looking ids survive the actual config codec.
  UIShell_SectionPlacement unusual[] = {{str8_lit("123"), str8_lit("Number"), str8_zero(), 0},
                                      {str8_lit("closed"), str8_lit("Closed"), str8_zero(), 1}};
  CFG_Node *id_owner = cfg_node_new(state.cfg, user, str8_lit("window"));
  uishell_sidebar_reconcile_regions(id_owner, unusual, ArrayCount(unusual));
  uishell_sidebar_reset_regions(id_owner);
  uishell_sidebar_reconcile_regions(id_owner, unusual, ArrayCount(unusual));
  for(U32 restart = 0; restart < 2; restart++)
  {
    String8 roundtrip = cfg_string_from_tree(scratch.arena, &schemas, str8_zero(), id_owner);
    CFG_NodePtrList parsed = cfg_node_ptr_list_from_string(scratch.arena, state.cfg, &schemas, str8_zero(), roundtrip);
    PlacementCheck(parsed.first != 0);
    CFG_Node *next_owner = parsed.first->v;
    cfg_node_insert_child(state.cfg, user, user->last, next_owner);
    cfg_node_release(state.cfg, id_owner); id_owner = next_owner;
    for(U64 i = 0; i < ArrayCount(unusual); i++)
    { PlacementCheck(uishell_sidebar_region_view(id_owner, unusual[i].key) != &cfg_nil_node); }
    uishell_sidebar_reconcile_regions(id_owner, unusual, ArrayCount(unusual));
  }
  cfg_node_release(state.cfg, id_owner);
  // Updating a default host does not move a valid saved position; KDL-owned
  // titles do refresh. Removing a region also removes its pending hint marker.
  CFG_Node *saved_x = uishell_sidebar_region_view(restored, str8_lit("x"));
  CFG_ID saved_x_id = saved_x->id;
  CFG_Node *removed_y = uishell_sidebar_region_view(restored, str8_lit("y"));
  CFG_ID removed_y_id = removed_y->id;
  cfg_node_new(state.cfg, removed_y, str8_lit("section_hint_pending"));
  String8 retitled = str8_lit(
    "region \"x\" root-template=\"renamed\" form=\"compact\" default-host=\"floating\"\n"
    "template \"renamed\" slot=\"compact\" node-kind=\"entity\" { field \"label\" source=\"literal\" value=\"Renamed\"; }\n");
  PlacementCheck(andamento_configure(core, (AndamentoText){retitled.str, retitled.size}, 0));
  uishell_sidebar_replace_snapshot(&sidebar, andamento_snapshot_acquire(core, 0));
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(saved_x->id == saved_x_id && rd_dock_host_from_cfg(saved_x, RD_DOCK_UNMEASURED_WIDTH).kind == RD_DockHostKind_Sidebar);
  PlacementCheck(str8_match(cfg_node_child_from_string(saved_x, str8_lit("label"))->first->string, str8_lit("Renamed"), 0));
  PlacementCheck(cfg_node_from_id(removed_y_id) == &cfg_nil_node);
  // A provider outage retains the complete saved arrangement. An authoritative
  // changed declaration removes stale positions; a later declaration is new.
  CFG_Node *x = uishell_sidebar_region_view(restored, str8_lit("x"));
  AndamentoSnapshot *saved_snapshot = sidebar.snapshot; sidebar.snapshot = 0;
  generation = cfg_change_gen(); uishell_sidebar_dock_layout(&split);
  PlacementCheck(cfg_change_gen() == generation && uishell_sidebar_region_view(restored, str8_lit("x")) == x);
  sidebar.snapshot = saved_snapshot;
  String8 empty = str8_lit("region \"x\" root-template=\"flotilla/region/tree\"\n");
  PlacementCheck(andamento_configure(core, (AndamentoText){empty.str, empty.size}, 0));
  uishell_sidebar_replace_snapshot(&sidebar, andamento_snapshot_acquire(core, 0));
  uishell_sidebar_dock_layout(&split);
  PlacementCheck(uishell_sidebar_region_view(restored, str8_lit("y")) == &cfg_nil_node);
  andamento_snapshot_release(sidebar.snapshot); andamento_destroy(core);
  if(sidebar.placement_arena) { arena_release(sidebar.placement_arena); }
  cfg_state_release(state.cfg); cfg_ctx_select(saved_ctx); rd_state = saved_rd;
  fprintf(stderr, "Section placement diagnostics: %s (hints, reorder, restart, add, close, remove, reset)\n", failures ? "FAILED" : "passed");
#undef PlacementCheck
  scratch_end(scratch); return failures == 0;
}
