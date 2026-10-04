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
    { uishell_control_surface_ui(r2f32p(17, 29, 337, 149), &split); }
    ui_end_build();
    for(U32 i = 0; i < 2; i++)
    {
      UI_Box *body = ui_box_from_key(bodies[i]);
      DockFailure(ui_box_is_nil(body));
      if(!ui_box_is_nil(body))
      {
        DockFailure(body->rect.x0 < 17 || body->rect.x1 > 337 || body->rect.y0 < 29 || body->rect.y1 > 149);
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
    { uishell_control_surface_ui(r2f32p(17, 29, 337, 149), &split); }
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
      uishell_sidebar_size_panels(&restored_split, &restored_mount, r2f32p(17, 29, 337, 149));
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
  // Move a section to a Workspace Region and back through the same commands.
  CFG_Node *workspace = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  CFG_Node *panels = cfg_node_new(rd_state->cfg, workspace, str8_lit("panels"));
  before = rd_state->cmds[0].last;
  UIShell_RegsScope(.window = window->id, .panel = view->parent->id, .view = view->id,
                   .dst_panel = panels->id, .prev_tab = 0)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  uishell_sidebar_docking_drain(before);
  DockFailure(view->parent != panels || view->id != view_id);
  DockFailure(rd_dock_host_from_cfg(view, 320).kind != RD_DockHostKind_WorkspaceRegion);
  DockFailure(rd_dock_presentation(RD_DockHostKind_WorkspaceRegion, 1) != RD_DockPresentation_Tabs);
  before = rd_state->cmds[0].last;
  UIShell_RegsScope(.window = window->id, .panel = panels->id, .view = view->id,
                   .dst_panel = second_view->parent->id, .prev_tab = second_view->id)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  uishell_sidebar_docking_drain(before);
  DockFailure(view->parent != second_view->parent || view->id != view_id);
  CFG_Node *parent = view->parent;
  rd_dock_restore_window(rd_state->cfg, window);
  DockFailure(view->parent != parent);
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
  String8 persisted = str8_lit("display-variable \"show-role-attempts\" type=\"bool\" default=false label=\"Role attempts\" icon=\"R\" persist=true");
  U64 declaration_at = str8_find_needle(daily, 0, persisted, 0);
  DockFailure(declaration_at == daily.size);
  String8 ephemeral = push_str8f(scratch.arena, "%Sdisplay-variable \"show-role-attempts\" type=\"bool\" default=false label=\"Role attempts\" icon=\"R\" persist=false%S",
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
  uishell_sidebar_release(state);
  ws->sidebar = saved_sidebar; uishell_sidebar_fixture = saved_fixture; uishell_sidebar_subject_fixture = saved_subject;
  ui_select_state(saved_ui); ui_state_release(test_ui);
  scratch_end(scratch);
  fprintf(stderr, "Sidebar docking diagnostics: %u failures\n", failures);
  return failures == 0;
#undef DockFailure
}
