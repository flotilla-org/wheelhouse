// OS window sizing and the external action ABI are the fault boundaries;
// config, command dispatch, snapshot refresh and validity checking are real.
global Rng2F32 integration_rect;
global U64 integration_now = 10000;
internal U64 integration_now_ms(void) { return integration_now; }
global U32 integration_failures_left, integration_dispatches;
internal Rng2F32 integration_client_rect(WM_Window window) { return integration_rect; }
internal U32
integration_dispatch(Andamento *core, const AndamentoSnapshot *snapshot, size_t action, char **error)
{
  integration_dispatches++;
  if(!andamento_snapshot_is_current(core, snapshot, 0)) { fprintf(stderr, "obsolete restore action\n"); abort_self(1); }
  if(integration_failures_left) { integration_failures_left--; return 0; }
  return andamento_dispatch(core, snapshot, action, error);
}
internal F32
integration_width(CFG_Node *destination, Dir2 dir)
{
  Temp scratch = scratch_begin(0, 0);
  F32 width = rd_dock_target_width(scratch.arena, destination, dir);
  scratch_end(scratch);
  return width;
}
internal B32
integration_drag_site(RD_WindowState *ws, CFG_Node *source, CFG_Node *destination, Dir2 dir)
{
  Temp scratch = scratch_begin(0, 0);
  UI_State *previous_ui = ui_state, *gesture_ui = ui_state_alloc();
  ui_select_state(gesture_ui);
  UIShell_WorkspaceMount mount = uishell_workspace_mount_from_cfg(scratch.arena, destination);
  CFG_PanelNode *target = cfg_panel_node_from_tree_cfg(mount.panel_tree.root, destination);
  B32 insertion = dir != Dir2_Invalid && target->parent != &cfg_nil_panel_node &&
    target->parent->split_axis == axis2_from_dir2(dir);
  char *names[] = {"up", "down", "left", "right"};
  String8 site_name = str8_lit("center");
  if(dir == Dir2_Up) site_name = str8_cstring(names[0]);
  if(dir == Dir2_Down) site_name = str8_cstring(names[1]);
  if(dir == Dir2_Left) site_name = str8_cstring(names[2]);
  if(dir == Dir2_Right) site_name = str8_cstring(names[3]);
  CFG_PanelNode *boundary = side_from_dir2(dir) == Side_Max ? target->next : target;
  UI_Key site = insertion ? ui_key_from_stringf(ui_key_zero(), "drop_boundary_%p_%p", target->parent->cfg, boundary->cfg) :
    ui_key_from_stringf(ui_key_zero(), "drop_split_%S_%p", site_name, destination);
  UIShell_RegsScope(.window = ws->cfg_id, .panel = source->parent->id, .view = source->id)
  {
    rd_drag_begin(UIShell_ContextRegSlot_View);
    UI_IconInfo icons = {0}; UI_AnimationInfo animation = {0}; UI_EventList events = {0};
    fnt_frame();
    dr_begin_frame(rd_font_from_slot(RD_FontSlot_Icons));
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = v2f32(250, 250);
    UI_FontSize(rd_font_size()) UI_Font(rd_font_from_slot(RD_FontSlot_Main))
    {
      Rng2F32 area = pad_2f32(integration_rect, -96.f*0.035f);
      ui_state->mouse = center_2f32(cfg_target_rect_from_panel_node(area, mount.panel_tree.root, target));
      rd_panel_area_ui(scratch, area, integration_rect, ws, &mount, 1, 0, 0, 0, 0);
    }
    ui_end_build();
    rd_drag_kill();
  }
  B32 exists = !ui_box_is_nil(ui_box_from_key(site));
  ui_select_state(previous_ui);
  ui_state_release(gesture_ui);
  scratch_end(scratch);
  return exists;
}
#define IntegrationCheck(x) do { if(!(x)) { fprintf(stderr, "FAIL integration line %d: %s\n", __LINE__, #x); failures++; } } while(0)
internal void
entry_point(CmdLine *cmdline)
{
  U32 failures = 0;
  wm_init(); fp_init(); r_init(cmdline); fnt_init(); rd_init(cmdline);
  rd_state->view_ui_rule_map = rd_view_ui_rule_map_make(rd_state->arena, 512);
  e_select_cache(rd_state->eval_cache);
  E_BaseCtx base_ctx = {.address_arch = Arch_CURRENT}; e_select_base_ctx(&base_ctx);
  E_IRCtx ir_ctx = {0}; e_select_ir_ctx(&ir_ctx);
  E_InterpretCtx interpret_ctx = {0}; e_select_interpret_ctx(&interpret_ctx);
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *user = cfg_node_new(rd_state->cfg, cfg_node_root(), str8_lit("user"));
  CFG_Node *window = cfg_node_new(rd_state->cfg, user, str8_lit("window"));
  cfg_node_new(rd_state->cfg, window, str8_lit("control_split_collapsed"));
  CFG_Node *panels = cfg_node_new(rd_state->cfg, window, str8_lit("panels"));
  CFG_Node *source = cfg_node_new(rd_state->cfg, panels, str8_lit("scroll_region_fixture"));
  // Keep a second tab so moving the fixture cannot close the source panel.
  cfg_node_new(rd_state->cfg, panels, str8_lit("terminal"));
  integration_rect = r2f32p(0, 0, 640, 480);
  RD_WindowState *ws = rd_window_state_from_cfg(window);
  UI_Theme theme = {0}; ws->theme = &theme;
  ui_select_state(ws->ui);
  UI_IconInfo init_icons = {0}; UI_AnimationInfo init_animation = {0}; UI_EventList init_events = {0};
  ui_begin_build(ws->os, &init_events, &init_icons, ws->theme, &init_animation, 1.f/60, 1.f/60);
  ui_end_build();
  integration_rect = r2f32p(0, 0, 640, 480);
  // Generate move and all split directions, both parent axes, and widths
  // immediately below/equal/above the fixture's declared 128px minimum.
  Dir2 directions[] = {Dir2_Invalid, Dir2_Left, Dir2_Right, Dir2_Up, Dir2_Down};
  for(U32 shape = 0; shape < 2; shape++)
  for(U32 axis = 0; axis < 2; axis++)
  for(U32 direction = 0; direction < ArrayCount(directions); direction++)
  for(U32 boundary = 127; boundary <= 129; boundary++)
  {
    if(shape == 0 && direction == 0) { continue; }
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("panels")));
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("split_x")));
    if(axis == Axis2_X) { cfg_node_new(rd_state->cfg, window, str8_lit("split_x")); }
    panels = cfg_node_new(rd_state->cfg, window, str8_lit("panels"));
    CFG_Node *origin = shape ? cfg_node_new(rd_state->cfg, panels, str8_lit("0.3")) : panels;
    CFG_Node *destination = shape ? cfg_node_new(rd_state->cfg, panels, str8_lit("0.7")) : panels;
    source = cfg_node_new(rd_state->cfg, origin, str8_lit("scroll_region_fixture"));
    cfg_node_new(rd_state->cfg, origin, str8_lit("terminal"));
    cfg_node_new(rd_state->cfg, destination, str8_lit("terminal"));
    Dir2 dir = directions[direction];
    B32 measured = 0;
    for(U32 pixels = 100; pixels < 1600; pixels++)
    {
      integration_rect.x1 = pixels;
      if(integration_width(destination, dir) == boundary) { measured = 1; break; }
    }
    IntegrationCheck(measured);
    if(!measured) { fprintf(stderr, "axis %u dir %u boundary %u width %g\n", axis, direction, boundary, integration_width(destination, dir)); continue; }
    B32 site = integration_drag_site(ws, source, destination, dir);
    IntegrationCheck(site == (boundary >= 128));
    if(site != (boundary >= 128)) { fprintf(stderr, "site axis=%u dir=%u width=%u pixels=%g exists=%u\n", axis, direction, boundary, integration_rect.x1, site); }
    if(boundary == 128)
    {
      // A target shown during a drag must be refused after a smaller resize,
      // for moves, bisection and sibling insertion in every direction.
      F32 shown_width = integration_rect.x1;
      integration_rect.x1 *= 0.5f;
      U64 resized_gen = cfg_change_gen();
      UIShell_RegsScope(.window = window->id, .panel = origin->id, .view = source->id,
                       .dst_panel = destination->id, .dir2 = dir)
      {
        rd_drag_begin(UIShell_ContextRegSlot_View);
        if(dir == Dir2_Invalid) { uishell_dispatch_tab_command(str8_lit("move_view")); }
        else { uishell_dispatch_panel_command(str8_lit("split_panel")); }
        rd_drag_kill();
      }
      IntegrationCheck(source->parent == origin && cfg_change_gen() == resized_gen);
      integration_rect.x1 = shown_width;
    }
    if(shape && axis == Axis2_X && boundary == 128)
    {
      // Inserting another sibling after feedback changes both the target's
      // allocation and the insertion denominator. Commit uses the new tree.
      cfg_node_equip_string(rd_state->cfg, origin, str8_lit("0.27"));
      cfg_node_equip_string(rd_state->cfg, destination, str8_lit("0.63"));
      CFG_Node *extra = cfg_node_new(rd_state->cfg, panels, str8_lit("0.1"));
      cfg_node_new(rd_state->cfg, extra, str8_lit("terminal"));
      U64 changed_layout_gen = cfg_change_gen();
      IntegrationCheck(integration_width(destination, dir) < 128);
      UIShell_RegsScope(.window = window->id, .panel = origin->id, .view = source->id,
                       .dst_panel = destination->id, .dir2 = dir)
      {
        if(dir == Dir2_Invalid) { uishell_dispatch_tab_command(str8_lit("move_view")); }
        else { uishell_dispatch_panel_command(str8_lit("split_panel")); }
      }
      IntegrationCheck(source->parent == origin && cfg_change_gen() == changed_layout_gen);
      cfg_node_release(rd_state->cfg, extra);
      cfg_node_equip_string(rd_state->cfg, origin, str8_lit("0.3"));
      cfg_node_equip_string(rd_state->cfg, destination, str8_lit("0.7"));
    }
    // Scripted commands must enforce the same measured rule as drag feedback.
    CFG_Node *before_parent = source->parent;
    U64 before_gen = cfg_change_gen();
    UIShell_RegsScope(.window = window->id, .panel = origin->id, .view = source->id,
                     .dst_panel = destination->id, .dir2 = dir)
    {
      if(dir == Dir2_Invalid) { uishell_dispatch_tab_command(str8_lit("move_view")); }
      else { uishell_dispatch_panel_command(str8_lit("split_panel")); }
    }
    IntegrationCheck((source->parent != before_parent) == (boundary >= 128));
    if(boundary < 128) { IntegrationCheck(cfg_change_gen() == before_gen); }
    if(boundary >= 128)
    { F32 actual = integration_width(source->parent, Dir2_Invalid); IntegrationCheck(actual >= 128); if(actual < 128) fprintf(stderr, "actual axis=%u dir=%u boundary=%u actual=%g\n", axis, direction, boundary, actual); }
  }
  // A shown target is remeasured when the drop is committed after a resize.
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("panels")));
  panels = cfg_node_new(rd_state->cfg, window, str8_lit("panels"));
  CFG_Node *origin = cfg_node_new(rd_state->cfg, panels, str8_lit("0.5"));
  CFG_Node *destination = cfg_node_new(rd_state->cfg, panels, str8_lit("0.5"));
  source = cfg_node_new(rd_state->cfg, origin, str8_lit("scroll_region_fixture"));
  cfg_node_new(rd_state->cfg, origin, str8_lit("terminal"));
  cfg_node_new(rd_state->cfg, destination, str8_lit("terminal"));
  integration_rect.x1 = 640;
  IntegrationCheck(integration_drag_site(ws, source, destination, Dir2_Invalid));
  integration_rect.x1 = 100;
  U64 before_resize = cfg_change_gen();
  UIShell_RegsScope(.window = window->id, .panel = origin->id, .view = source->id, .dst_panel = destination->id)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  IntegrationCheck(source->parent == origin && cfg_change_gen() == before_resize);
  // Restore retains its structural policy even when geometry is too narrow.
  rd_dock_restore_window(rd_state->cfg, window);
  IntegrationCheck(source->parent == origin);

  // Real persistent declarations and snapshots cross a failing external ABI.
  UIShell_SidebarState display = {0}; char *error = 0;
  String8 daily = str8_cstring((char *)uishell_sidebar_daily_config);
  display.core = andamento_create(daily.str, daily.size, &error);
  IntegrationCheck(uishell_sidebar_result(&display, display.core != 0, error));
  uishell_sidebar_refresh(&display);
  UIShell_DisplayControlIterator it = {display.snapshot};
  AndamentoControl control = {0}; AndamentoText name = {0};
  IntegrationCheck(uishell_sidebar_next_persistent_control(&it, &control, &name));
  B32 initial = !!control.checked;
  CFG_Node *saved = cfg_node_new(rd_state->cfg, window, str8_lit("sidebar_display"));
  CFG_Node *value = cfg_node_new(rd_state->cfg, saved, uishell_sidebar_string(name));
  cfg_node_new(rd_state->cfg, value, initial ? str8_lit("false") : str8_lit("true"));
  integration_failures_left = 1; integration_dispatches = 0;
  uishell_sidebar_restore_display(&display, window);
  UIShell_DisplayRestore *retry = display.display_restores;
  IntegrationCheck(retry && retry->pending && retry->attempts == 1 && display.error[0]);
  uishell_sidebar_retry_display(&display, window, retry->retry_at-1);
  IntegrationCheck(integration_dispatches == 1);
  // Saving unrelated UI state preserves failed restore intent.
  uishell_sidebar_save_display(&display, window);
  IntegrationCheck(str8_match(value->first->string, initial ? str8_lit("false") : str8_lit("true"), 0));
  // Expire the old snapshot through a real tick before recovery.
  it = (UIShell_DisplayControlIterator){display.snapshot};
  AndamentoControl unrelated = {0}; AndamentoText unrelated_name = {0};
  uishell_sidebar_next_persistent_control(&it, &unrelated, &unrelated_name);
  IntegrationCheck(uishell_sidebar_next_persistent_control(&it, &unrelated, &unrelated_name));
  IntegrationCheck(andamento_dispatch(display.core, display.snapshot, unrelated.action, 0));
  // The production poll services local sidebars even without live ingress.
  display.initialized = 1; ws->sidebar = &display;
  integration_now = retry->retry_at;
  uishell_sidebar_poll_live();
  IntegrationCheck(integration_dispatches == 2 && !retry->pending);
  it = (UIShell_DisplayControlIterator){display.snapshot};
  uishell_sidebar_next_persistent_control(&it, &control, &name);
  IntegrationCheck(!!control.checked != initial);
  uishell_sidebar_save_display(&display, window);
  // Three failures exhaust recovery; thousands of frames never dispatch again.
  cfg_node_new_replace(rd_state->cfg, value, initial ? str8_lit("true") : str8_lit("false"));
  integration_failures_left = 100; integration_dispatches = 0;
  uishell_sidebar_restore_display(&display, window); retry = display.display_restores;
  uishell_sidebar_retry_display(&display, window, retry->retry_at);
  uishell_sidebar_retry_display(&display, window, retry->retry_at);
  U64 frames_after_exhaustion = rd_state->num_frames_requested;
  for(U32 frame = 0; frame < 1000; frame++)
  { uishell_sidebar_retry_display(&display, window, retry->retry_at+frame); }
  IntegrationCheck(integration_dispatches == 3 && retry->pending && retry->attempts == 3);
  IntegrationCheck(!display.display_wakeup_at && rd_state->num_frames_requested == frames_after_exhaustion);
  uishell_sidebar_save_display(&display, window);
  IntegrationCheck(str8_match(value->first->string, initial ? str8_lit("true") : str8_lit("false"), 0));
  // Reconciliation continues after exhaustion without dispatching: a new
  // saved value matching live state settles the old unresolved preference.
  cfg_node_new_replace(rd_state->cfg, value, initial ? str8_lit("false") : str8_lit("true"));
  uishell_sidebar_retry_display(&display, window, retry->retry_at);
  IntegrationCheck(!retry->pending && integration_dispatches == 3);
  cfg_node_new_replace(rd_state->cfg, value, initial ? str8_lit("true") : str8_lit("false"));
  // A newer saved setting during recovery cancels the obsolete target.
  integration_dispatches = 0;
  uishell_sidebar_restore_display(&display, window); retry = display.display_restores;
  cfg_node_new_replace(rd_state->cfg, value, initial ? str8_lit("false") : str8_lit("true"));
  uishell_sidebar_retry_display(&display, window, retry->retry_at);
  IntegrationCheck(!retry->pending && integration_dispatches == 1);
  // A successful user toggle also supersedes recovery without another toggle.
  cfg_node_new_replace(rd_state->cfg, value, initial ? str8_lit("true") : str8_lit("false"));
  uishell_sidebar_restore_display(&display, window); retry = display.display_restores;
  it = (UIShell_DisplayControlIterator){display.snapshot};
  uishell_sidebar_next_persistent_control(&it, &control, &name);
  IntegrationCheck(andamento_dispatch(display.core, display.snapshot, control.action, 0));
  uishell_sidebar_refresh(&display);
  uishell_sidebar_save_display(&display, window);
  IntegrationCheck(!retry->pending);
  ws->sidebar = 0;
  uishell_sidebar_release(&display);
  scratch_end(scratch);
  fprintf(stderr, "Docking integration: %u failures\n", failures);
  abort_self(failures ? 1 : 0);
}
