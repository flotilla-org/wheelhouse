// Compiled by tools/test-docking-integration.py from the production shell
// amalgamation with its entry point replaced. Links real Andamento/Cleat native
// libraries and native ingress; headless WM/renderer/font backends avoid a display.
// Tests use the real parser, panel UI, commands, checker and settings evaluator.
// OS window sizing and the external action ABI are the fault boundaries;
// config, command dispatch, snapshot refresh and validity checking are real.
global Rng2F32 integration_rect;
global U64 integration_now = 10000;
internal U64 integration_now_ms(void) { return integration_now; }
global U32 integration_failures_left, integration_dispatches;
// Queue timer threads at the OS boundary so early wake and lifetime sequences
// are deterministic. The production worker, completion token and polling run.
typedef struct { void *(*callback)(void *); void *data; } IntegrationWake;
global IntegrationWake integration_wakes[64];
global U32 integration_wake_first, integration_wake_count, integration_wake_posts;
internal int
integration_thread_create(pthread_t *thread, const pthread_attr_t *attr, void *(*callback)(void *), void *data)
{
  if(integration_wake_count == ArrayCount(integration_wakes)) { return 1; }
  *thread = (pthread_t)0;
  integration_wakes[integration_wake_count++] = (IntegrationWake){callback, data};
  return 0;
}
internal int integration_thread_detach(pthread_t thread) { return 0; }
internal void integration_sleep_ms(U32 delay) { }
internal void integration_post_wake(void) { integration_wake_posts++; }
internal void
integration_fire_wake(void)
{
  Assert(integration_wake_first < integration_wake_count);
  IntegrationWake wake = integration_wakes[integration_wake_first++];
  wake.callback(wake.data);
}
internal void
integration_finish_wakes(void)
{
  while(integration_wake_first < integration_wake_count) { integration_fire_wake(); }
}
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
  F32 width = rd_dock_target_width(scratch.arena, destination, dir, &cfg_nil_node, 0);
  scratch_end(scratch);
  return width;
}
internal B32
integration_drag_site(RD_WindowState *ws, CFG_Node *source, CFG_Node *destination, Dir2 dir, F32 *rendered_width)
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
  F32 layout_font_size = 0;
  UIShell_RegsScope(.window = ws->cfg_id, .panel = 0, .view = 0, .tab = 0)
  { layout_font_size = rd_font_size(); }
  UIShell_RegsScope(.window = ws->cfg_id, .panel = source->parent->id, .view = source->id)
  {
    rd_drag_begin(UIShell_ContextRegSlot_View);
    UI_IconInfo icons = {0}; UI_AnimationInfo animation = {0}; UI_EventList events = {0};
    fnt_frame();
    dr_begin_frame(rd_font_from_slot(RD_FontSlot_Icons));
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = v2f32(250, 250);
    UI_FontSize(layout_font_size) UI_Font(rd_font_from_slot(RD_FontSlot_Main))
    {
      Rng2F32 area = pad_2f32(integration_rect, -rd_window_edge_inset_px(ws));
      ui_state->mouse = center_2f32(cfg_target_rect_from_panel_node(area, mount.panel_tree.root, target));
      rd_panel_area_ui(scratch, area, integration_rect, ws, &mount, 1, 0, 0, 0, 0);
    }
    ui_end_build();
    rd_drag_kill();
  }
  B32 exists = !ui_box_is_nil(ui_box_from_key(site));
  if(rendered_width)
  {
    UI_Box *body = ui_box_from_key(ui_key_from_stringf(ui_key_zero(), "panel_box_%p", destination));
    *rendered_width = ui_box_is_nil(body) ? 0 : dim_2f32(body->rect).x;
  }
  ui_select_state(previous_ui);
  ui_state_release(gesture_ui);
  scratch_end(scratch);
  return exists;
}
#define IntegrationCheck(x) do { if(!(x)) { fprintf(stderr, "FAIL integration line %d: %s\n", __LINE__, #x); failures++; } } while(0)
// Reset the saved tree through the real config API between generated cases.
internal CFG_Node *
integration_reset_panels(CFG_Node *window, Axis2 axis)
{
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("panels")));
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("split_x")));
  if(axis == Axis2_X) { cfg_node_new(rd_state->cfg, window, str8_lit("split_x")); }
  return cfg_node_new(rd_state->cfg, window, str8_lit("panels"));
}

internal UIShell_CmdNode *
integration_move(CFG_Node *window, CFG_Node *source, CFG_Node *destination, Dir2 dir)
{
  UIShell_CmdNode *before = rd_state->cmds[0].last;
  UIShell_RegsScope(.window = window->id, .panel = source->parent->id, .view = source->id,
                   .dst_panel = destination->id, .dir2 = dir)
  {
    if(dir == Dir2_Invalid) { uishell_dispatch_tab_command(str8_lit("move_view")); }
    else { uishell_dispatch_panel_command(str8_lit("split_panel")); }
  }
  return before;
}

// Source closure is queued by the production command; dispatch that command
// rather than modelling closure in the harness.
internal void
integration_close_queued(UIShell_CmdNode *before)
{
  for(UIShell_CmdNode *n = before ? before->next : rd_state->cmds[0].first; n; n = n->next)
  {
    if(str8_match(n->cmd.name, str8_lit("close_panel"), 0)) UIShell_RegsScope()
    { MemoryCopyStruct(uishell_regs(), n->cmd.regs); uishell_dispatch_panel_command(n->cmd.name); break; }
  }
}

internal U32
integration_display_policy(CFG_Node *window, RD_WindowState *ws)
{
  U32 failures = 0;
  // Real persistent declarations and snapshots cross a failing external ABI.
  // Each independent run starts with a fresh core, saved config and OS/fault
  // state. The operations within it intentionally form one recovery scenario.
  integration_finish_wakes();
  integration_now = 10000; integration_failures_left = integration_dispatches = 0;
  integration_wake_first = integration_wake_count = integration_wake_posts = 0;
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("sidebar_display")));
  UIShell_SidebarState display = {0}; char *error = 0;
  String8 daily = str8_cstring((char *)uishell_sidebar_daily_config);
  display.core = andamento_create(daily.str, daily.size, &error);
  IntegrationCheck(uishell_sidebar_result(&display, display.core != 0, error));
  // Polling a core whose initial snapshot is absent must acquire it again.
  display.initialized = 1; ws->sidebar = &display;
  IntegrationCheck(display.snapshot == 0);
  uishell_sidebar_poll_live();
  IntegrationCheck(display.snapshot != 0);
  UIShell_DisplayControlIterator it = {display.snapshot};
  AndamentoControl control = {0}; AndamentoText name = {0};
  IntegrationCheck(uishell_sidebar_next_persistent_control(&it, &control, &name));
  B32 initial = !!control.checked;
  CFG_Node *saved = cfg_node_new(rd_state->cfg, window, str8_lit("sidebar_display"));
  CFG_Node *value = cfg_node_new(rd_state->cfg, saved, uishell_sidebar_string(name));
  cfg_node_new(rd_state->cfg, value, initial ? str8_lit("false") : str8_lit("true"));
  // Initial restore must reacquire an absent snapshot and still restore intent.
  andamento_snapshot_release(display.snapshot); display.snapshot = 0;
  integration_failures_left = 1; integration_dispatches = 0;
  uishell_sidebar_restore_display(&display, window);
  UIShell_DisplayRestore *retry = display.display_restores;
  IntegrationCheck(retry && retry->pending && retry->attempts == 1 && display.error[0]);
  // A completed timer can wake before the deadline (e.g. interrupted sleep).
  // Consuming its completion must re-arm exactly once, without dispatching early.
  U32 scheduled = integration_wake_count;
  IntegrationCheck(display.display_wakeup && scheduled == 1);
  integration_fire_wake();
  uishell_sidebar_retry_display(&display, window, retry->retry_at-1);
  IntegrationCheck(integration_dispatches == 1 && integration_wake_count == scheduled+1);
  IntegrationCheck(display.display_wakeup_at == retry->retry_at && integration_wake_posts == 1);
  uishell_sidebar_retry_display(&display, window, retry->retry_at-1);
  IntegrationCheck(integration_wake_count == scheduled+1);
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
  rd_state->num_frames_requested = 0; // The event loop consumed requested frames.
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
  // A saved change and an external live change can race. Polling must retain
  // the saved preference rather than treating the external change as user intent.
  cfg_node_new_replace(rd_state->cfg, value, initial ? str8_lit("false") : str8_lit("true"));
  uishell_sidebar_restore_display(&display, window); retry = display.display_restores;
  cfg_node_new_replace(rd_state->cfg, value, initial ? str8_lit("true") : str8_lit("false"));
  it = (UIShell_DisplayControlIterator){display.snapshot};
  uishell_sidebar_next_persistent_control(&it, &control, &name);
  IntegrationCheck(andamento_dispatch(display.core, display.snapshot, control.action, 0));
  uishell_sidebar_refresh(&display);
  uishell_sidebar_retry_display(&display, window, retry->retry_at);
  IntegrationCheck(retry->pending && str8_match(value->first->string,
                   initial ? str8_lit("true") : str8_lit("false"), 0));
  // A declaration disappearing from a refreshed snapshot names its identity
  // on every bounded attempt, preserves saved intent and stops dispatching.
  UIShell_DisplayRestore missing = {.name = str8_lit("missing-display-declaration"), .desired = 1, .pending = 1};
  CFG_Node *missing_saved = cfg_node_new(rd_state->cfg, saved, missing.name);
  cfg_node_new(rd_state->cfg, missing_saved, str8_lit("true"));
  display.display_restores = &missing;
  U32 before_missing = integration_dispatches;
  for(U32 attempt = 1; attempt <= UIShell_DisplayRetryLimit; attempt++)
  {
    uishell_sidebar_retry_display(&display, window, missing.retry_at);
    IntegrationCheck(missing.attempts == attempt && missing.pending);
    IntegrationCheck(strstr((char *)display.error, "missing-display-declaration") != 0);
    IntegrationCheck(str8_match(missing_saved->first->string, str8_lit("true"), 0));
  }
  uishell_sidebar_retry_display(&display, window, missing.retry_at+10000);
  IntegrationCheck(missing.attempts == UIShell_DisplayRetryLimit && integration_dispatches == before_missing);
  // Long absent identities are truncated by the shared error setter, with
  // NUL termination on every retry even when the prior buffer is nonzero.
  U8 long_name[2048]; MemorySet(long_name, 'x', sizeof(long_name));
  missing = (UIShell_DisplayRestore){.name = str8(long_name, sizeof(long_name)), .desired = 1, .pending = 1};
  cfg_node_equip_string(rd_state->cfg, missing_saved, missing.name);
  for(U32 attempt = 1; attempt <= UIShell_DisplayRetryLimit; attempt++)
  {
    MemorySet(display.error, '!', sizeof(display.error));
    uishell_sidebar_retry_display(&display, window, missing.retry_at);
    IntegrationCheck(display.error[sizeof(display.error)-1] == 0);
    IntegrationCheck(strnlen((char *)display.error, sizeof(display.error)) == sizeof(display.error)-1);
    IntegrationCheck(strncmp((char *)display.error, "Sidebar display declaration unavailable: x", 41) == 0);
    IntegrationCheck(missing.pending && missing.attempts == attempt && integration_dispatches == before_missing);
    IntegrationCheck(str8_match(missing_saved->first->string, str8_lit("true"), 0));
  }
  cfg_node_release(rd_state->cfg, missing_saved);
  display.display_restores = 0;
  // Retiring state while a worker is still queued must leave only the token;
  // callbacks after arena/core release cannot touch the retired sidebar.
  cfg_node_new_replace(rd_state->cfg, value, initial ? str8_lit("true") : str8_lit("false"));
  uishell_sidebar_restore_display(&display, window);
  IntegrationCheck(display.display_wakeup != 0);
  ws->sidebar = 0;
  uishell_sidebar_release(&display);
  integration_finish_wakes();
  return failures;
}
internal void
entry_point(CmdLine *cmdline)
{
  U32 failures = 0;
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
  CFG_Node *user = cfg_node_new(rd_state->cfg, cfg_node_root(), str8_lit("user"));
  CFG_Node *window = cfg_node_new(rd_state->cfg, user, str8_lit("window"));
  // Nonzero spacing makes renderer/measurement drift observable.
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, user, str8_lit("panel_gap")), str8_lit("1"));
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, user, str8_lit("font_size")), str8_lit("16"));
  // Window chrome uses its own size even with a different View override.
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, window, str8_lit("font_size")), str8_lit("24"));
  cfg_node_new(rd_state->cfg, window, str8_lit("control_split_collapsed"));
  CFG_Node *panels = cfg_node_new(rd_state->cfg, window, str8_lit("panels"));
  CFG_Node *source = cfg_node_new(rd_state->cfg, panels, str8_lit("scroll_region_fixture"));
  // Keep a second tab so moving the fixture cannot close the source panel.
  cfg_node_new(rd_state->cfg, panels, str8_lit("terminal"));
  integration_rect = r2f32p(0, 0, 640, 480);
  RD_WindowState *ws = rd_window_state_from_cfg(window);
  UI_Theme theme = {0}; ws->theme = &theme;
  UIShell_RegsScope(.window = window->id)
  { IntegrationCheck(rd_panel_inset_px(rd_font_size()) > 0); }
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
    panels = integration_reset_panels(window, axis);
    CFG_Node *origin = shape ? cfg_node_new(rd_state->cfg, panels, str8_lit("0.3")) : panels;
    CFG_Node *destination = shape ? cfg_node_new(rd_state->cfg, panels, str8_lit("0.7")) : panels;
    source = cfg_node_new(rd_state->cfg, origin, str8_lit("scroll_region_fixture"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, source, str8_lit("font_size")), str8_lit("72"));
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
    B32 site = integration_drag_site(ws, source, destination, dir, 0);
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
    {
      F32 actual = integration_width(source->parent, Dir2_Invalid), rendered = 0;
      // Committed measurement must equal the body rectangle the real UI lays
      // out, so a future rendering-only inset change cannot silently drift.
      integration_drag_site(ws, source, source->parent, Dir2_Invalid, &rendered);
      IntegrationCheck(actual == boundary && rendered == actual);
    }
  }
  // Moving the last View measures insertion followed by source removal.
  // Generate both axes, every direction and inclusive minimum boundaries;
  // compare feedback and refusal with the final production-rendered body.
  for(U32 axis = 0; axis < 2; axis++)
  for(U32 direction = 0; direction < ArrayCount(directions); direction++)
  for(U32 boundary = 127; boundary <= 129; boundary++)
  {
    panels = integration_reset_panels(window, axis);
    CFG_Node *origin = cfg_node_new(rd_state->cfg, panels, str8_lit("0.3"));
    CFG_Node *destination = cfg_node_new(rd_state->cfg, panels, str8_lit("0.7"));
    source = cfg_node_new(rd_state->cfg, origin, str8_lit("scroll_region_fixture"));
    cfg_node_new(rd_state->cfg, destination, str8_lit("terminal"));
    Dir2 dir = directions[direction];
    // With two siblings: a center move fills the root; matching-axis
    // insertion allocates 1/3 then removes the source's serialized 1/5,
    // leaving 5/12. Cross-axis bisection fills the root's width (Y), or half (X).
    F32 fraction = dir == Dir2_Invalid ? 1.f :
      axis2_from_dir2(dir) == Axis2_X ? (axis == Axis2_X ? 0.416666f : 0.5f) : 1.f;
    F32 inset = 0;
    UIShell_RegsScope(.window = window->id, .panel = 0, .view = 0, .tab = 0)
    { inset = rd_panel_inset_px(rd_font_size()); }
    for(U32 pixels = 100; pixels < 1600; pixels++)
    {
      integration_rect.x1 = pixels;
      F32 edge = rd_window_edge_inset_px(ws);
      F32 start = edge;
      if(dir == Dir2_Right) { start += (pixels-2*edge)*(1-fraction); }
      F32 end = start+(pixels-2*edge)*fraction;
      if(Max(0.f, round_f32(round_f32(end)-inset)-round_f32(round_f32(start)+inset)) == boundary) { break; }
    }
    B32 last_site = integration_drag_site(ws, source, destination, dir, 0);
    IntegrationCheck(last_site == (boundary >= 128));
    U64 before_gen = cfg_change_gen();
    UIShell_CmdNode *before_cmd = integration_move(window, source, destination, dir);
    IntegrationCheck((source->parent != origin) == (boundary >= 128));
    if(boundary < 128) { IntegrationCheck(cfg_change_gen() == before_gen); }
    else
    {
      integration_close_queued(before_cmd);
      F32 rendered = 0;
      integration_drag_site(ws, source, source->parent, Dir2_Invalid, &rendered);
      IntegrationCheck(rendered == boundary && integration_width(source->parent, Dir2_Invalid) == rendered);
    }
  }
  // Additional removal shapes exercise sibling rescaling and nested parent
  // collapse/flattening. Prediction must equal the committed rendered body,
  // and feedback must never modify the saved tree.
  for(U32 shape = 0; shape < 5; shape++)
  for(U32 axis = 0; axis < 2; axis++)
  for(U32 direction = 0; direction < ArrayCount(directions); direction++)
  {
    panels = integration_reset_panels(window, axis);
    CFG_Node *origin = cfg_node_new(rd_state->cfg, panels, str8_lit("0.2"));
    CFG_Node *destination = cfg_node_new(rd_state->cfg, panels, str8_lit("0.5"));
    CFG_Node *extra = cfg_node_new(rd_state->cfg, panels, str8_lit("0.3"));
    cfg_node_new(rd_state->cfg, extra, str8_lit("terminal"));
    if(shape == 1)
    {
      CFG_Node *container = origin;
      origin = cfg_node_new(rd_state->cfg, container, str8_lit("0.4"));
      CFG_Node *keep = cfg_node_new(rd_state->cfg, container, str8_lit("0.6"));
      for(U32 child = 0; child < 2; child++)
      { cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, keep, str8_lit("0.5")), str8_lit("terminal")); }
    }
    source = cfg_node_new(rd_state->cfg, origin, str8_lit("scroll_region_fixture"));
    cfg_node_new(rd_state->cfg, destination, str8_lit("terminal"));
    CFG_ID origin_id = origin->id;
    if(shape == 2)
    {
      // move_view ignores project-filtered sibling tabs for source closure;
      // split_panel retains a source with any tab. Both outcomes must match UI.
      CFG_Node *hidden = cfg_node_new(rd_state->cfg, origin, str8_lit("terminal"));
      cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, hidden, str8_lit("project")), str8_lit("/unselected-project"));
      IntegrationCheck(rd_cfg_is_project_filtered(hidden));
    }
    if(shape >= 3)
    {
      // Hand-edited full/overfull source allocations recover finitely after
      // closure, rather than divide by zero or produce negative allocations.
      cfg_node_equip_string(rd_state->cfg, origin, shape == 3 ? str8_lit("1") : str8_lit("1.1"));
      cfg_node_equip_string(rd_state->cfg, destination, str8_lit("0"));
      cfg_node_equip_string(rd_state->cfg, extra, str8_lit("0"));
    }
    integration_rect.x1 = 1200;
    Dir2 dir = directions[direction];
    U64 before_gen = cfg_change_gen();
    F32 predicted = rd_dock_target_width(scratch.arena, destination, dir, source, 0);
    IntegrationCheck(predicted >= 128);
    if(shape < 3) { IntegrationCheck(integration_drag_site(ws, source, destination, dir, 0)); }
    // Malformed zero-sized destinations have no reachable UI hit region;
    // validate the production feedback checker with the measured proposal.
    else { IntegrationCheck(rd_dock_drag_target(source, destination, predicted)); }
    IntegrationCheck(cfg_change_gen() == before_gen);
    UIShell_CmdNode *before_cmd = integration_move(window, source, destination, dir);
    integration_close_queued(before_cmd);
    if(shape == 2)
    { IntegrationCheck((cfg_node_from_id(origin_id) == &cfg_nil_node) == (dir == Dir2_Invalid)); }
    F32 rendered = 0;
    integration_drag_site(ws, source, source->parent, Dir2_Invalid, &rendered);
    IntegrationCheck(rendered == predicted);
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
  IntegrationCheck(integration_drag_site(ws, source, destination, Dir2_Invalid, 0));
  integration_rect.x1 = 100;
  U64 before_resize = cfg_change_gen();
  log_scope_begin();
  UIShell_RegsScope(.window = window->id, .panel = origin->id, .view = source->id, .dst_panel = destination->id)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  LogScopeResult resize_log = log_scope_end(scratch.arena);
  String8 explanation = rd_dock_rule_message(RD_DockRule_MinimumWidth);
  // Scripted width refusals retain the checker’s user-facing explanation.
  IntegrationCheck(str8_find_needle(resize_log.strings[LogMsgKind_UserError], 0, explanation, 0) !=
                   resize_log.strings[LogMsgKind_UserError].size);
  IntegrationCheck(source->parent == origin && cfg_change_gen() == before_resize);
  // A destination with no live window reports unavailable geometry explicitly.
  CFG_Node *unopened = cfg_node_new(rd_state->cfg, user, str8_lit("window"));
  CFG_Node *unopened_panel = cfg_node_new(rd_state->cfg, unopened, str8_lit("panels"));
  CFG_Node *unopened_origin = cfg_node_new(rd_state->cfg, unopened_panel, str8_lit("0.5"));
  unopened_panel = cfg_node_new(rd_state->cfg, unopened_panel, str8_lit("0.5"));
  UIShell_WorkspaceMount absent_mount = uishell_workspace_mount_from_cfg(scratch.arena, unopened_panel);
  log_scope_begin();
  rd_drag_begin(UIShell_ContextRegSlot_View);
  RD_DockGeometry absent_geometry = rd_dock_geometry_from_mount(&absent_mount);
  rd_drag_kill();
  LogScopeResult drag_log = log_scope_end(scratch.arena);
  // Active-drag geometry without window state is silent, and zero measured.
  IntegrationCheck(dim_2f32(absent_geometry.area).x == 0 && drag_log.strings[LogMsgKind_UserError].size == 0);
  CFG_Node *unopened_source = cfg_node_new(rd_state->cfg, unopened_origin, str8_lit("scroll_region_fixture"));
  log_scope_begin();
  IntegrationCheck(!rd_dock_move_allowed(scratch.arena, "move", unopened_source, unopened_panel, Dir2_Invalid));
  LogScopeResult missing_log = log_scope_end(scratch.arena);
  String8 missing_reason = str8_lit("destination has no live window state");
  IntegrationCheck(str8_find_needle(missing_log.strings[LogMsgKind_UserError], 0, missing_reason, 0) !=
                   missing_log.strings[LogMsgKind_UserError].size);
  // Zero-minimum commands into an unopened host succeed without an error.
  CFG_Node *unconstrained = cfg_node_new(rd_state->cfg, unopened_origin, str8_lit("terminal"));
  log_scope_begin();
  UIShell_RegsScope(.window = window->id, .panel = unopened_origin->id, .view = unconstrained->id, .dst_panel = unopened_panel->id)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  LogScopeResult accepted_log = log_scope_end(scratch.arena);
  IntegrationCheck(unconstrained->parent == unopened_panel && accepted_log.strings[LogMsgKind_UserError].size == 0);
  cfg_node_release(rd_state->cfg, unopened);
  // Unrendered floating hosts retain the declared checker: zero-minimum
  // ordinary Views fit; the 128px fixture refuses measured-zero placement.
  CFG_Node *floating = cfg_node_new(rd_state->cfg, panels, str8_lit("floating_panels"));
  cfg_node_new(rd_state->cfg, floating, str8_lit("terminal"));
  // Current mount builders select rendered panels/sidebar roots. Exercise a
  // future floating mount explicitly to cover the defensive no-render branch.
  UIShell_WorkspaceMount floating_mount = {.window_cfg = window, .owner_cfg = window,
    .panel_tree = cfg_panel_tree_from_panels_cfg(scratch.arena, floating, Axis2_X)};
  RD_DockGeometry floating_geometry = rd_dock_geometry_from_mount(&floating_mount);
  F32 floating_width = rd_dock_width_from_geometry(&floating_geometry, floating, Dir2_Invalid);
  IntegrationCheck(floating_width == 0);
  IntegrationCheck(rd_dock_placement(source, floating, floating_width) == RD_DockRule_MinimumWidth);
  CFG_Node *ordinary = origin->last;
  IntegrationCheck(rd_dock_placement(ordinary, floating, floating_width) == RD_DockRule_Valid);
  cfg_node_release(rd_state->cfg, floating);
  // Restore retains its structural policy even when geometry is too narrow.
  rd_dock_restore_window(rd_state->cfg, window);
  IntegrationCheck(source->parent == origin);

  failures += integration_display_policy(window, ws);
  failures += integration_display_policy(window, ws);
  scratch_end(scratch);
  fprintf(stderr, "Docking integration: %u failures\n", failures);
  abort_self(failures ? 1 : 0);
}
