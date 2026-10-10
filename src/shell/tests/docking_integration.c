// Compiled by tools/test-docking-integration.py from the production shell
// amalgamation with its entry point replaced. Links real Andamento/Cleat native
// libraries and native ingress; headless WM/renderer/font backends avoid a display.
// Tests use the real parser, panel UI, commands, checker and settings evaluator.
// OS window sizing is the fault boundary; config, command dispatch and
// validity checking are real.
global Rng2F32 integration_rect;
internal Rng2F32 integration_client_rect(WM_Window window) { return integration_rect; }
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
  String8 site_name = str8_zero();
  if(dir == Dir2_Up) site_name = str8_cstring(names[0]);
  if(dir == Dir2_Down) site_name = str8_cstring(names[1]);
  if(dir == Dir2_Left) site_name = str8_cstring(names[2]);
  if(dir == Dir2_Right) site_name = str8_cstring(names[3]);
  CFG_PanelNode *boundary = side_from_dir2(dir) == Side_Max ? target->next : target;
  // A drop into the panel lands on its tab strip.
  UI_Key site = insertion ? ui_key_from_stringf(ui_key_zero(), "drop_boundary_%p_%p", target->parent->cfg, boundary->cfg) :
    dir == Dir2_Invalid ? rd_panel_catchall_drop_site_key(destination) :
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
// Successive events are this far apart; the stub's double-click time is 0.
global U64 integration_event_us, integration_event_gap_us = 1000000;
// One frame of the window's panel area, with the mouse at `mouse` and an
// optional left-button `kind` event there. Returns the laid out width of
// `leaf`'s body.
internal F32
integration_panel_frame(RD_WindowState *ws, CFG_Node *window, CFG_Node *leaf, Vec2F32 mouse, UI_EventKind kind)
{
  Temp scratch = scratch_begin(0, 0);
  rd_state->frame_index += 1;
  UIShell_WorkspaceMount mount = uishell_workspace_mount_from_cfg(scratch.arena, window);
  F32 layout_font_size = 0;
  UIShell_RegsScope(.window = ws->cfg_id, .panel = 0, .view = 0, .tab = 0)
  { layout_font_size = rd_font_size(); }
  UI_EventList events = {0};
  if(kind != UI_EventKind_Null)
  {
    integration_event_us += integration_event_gap_us;
    UI_Event event = {.kind = kind, .key = WM_Key_LeftMouseButton, .pos = mouse, .timestamp_us = integration_event_us};
    ui_event_list_push(scratch.arena, &events, &event);
  }
  UI_IconInfo icons = {0}; UI_AnimationInfo animation = {0};
  fnt_frame();
  dr_begin_frame(rd_font_from_slot(RD_FontSlot_Icons));
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = mouse;
  UI_FontSize(layout_font_size) UI_Font(rd_font_from_slot(RD_FontSlot_Main))
  { rd_panel_area_ui(scratch, pad_2f32(integration_rect, -rd_window_edge_inset_px(ws)), integration_rect, ws, &mount, 1, 0, 0, 0, 0); }
  ui_end_build();
  UI_Box *body = ui_box_from_key(ui_key_from_stringf(ui_key_zero(), "panel_box_%p", leaf));
  scratch_end(scratch);
  return ui_box_is_nil(body) ? 0 : dim_2f32(body->rect).x;
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

// A drop closes its emptied source in the same edit; nothing is left queued
// to close it later.
internal B32
integration_close_queued(UIShell_CmdNode *before)
{
  for(UIShell_CmdNode *n = before ? before->next : rd_state->cmds[0].first; n; n = n->next)
  {
    if(str8_match(n->cmd.name, str8_lit("close_panel"), 0)) { return 1; }
  }
  return 0;
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
  // Proposals copy the arrangement; src/shell/tests/arrangement.c checks that
  // a hand-edited deep split chain copies without exhausting the C stack.
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
  // Cancelling and restarting a drag reuses only the immutable layout tree;
  // the active View's distinct source allocation is read anew on every query.
  panels = integration_reset_panels(window, Axis2_X);
  CFG_Node *drag_origins[] = {
    cfg_node_new(rd_state->cfg, panels, str8_lit("0.2")),
    cfg_node_new(rd_state->cfg, panels, str8_lit("0.3")),
  };
  CFG_Node *drag_destination = cfg_node_new(rd_state->cfg, panels, str8_lit("0.5"));
  cfg_node_new(rd_state->cfg, drag_destination, str8_lit("terminal"));
  integration_rect.x1 = 1200;
  CFG_Node *drag_views[] = {
    cfg_node_new(rd_state->cfg, drag_origins[0], str8_lit("scroll_region_fixture")),
    cfg_node_new(rd_state->cfg, drag_origins[1], str8_lit("scroll_region_fixture")),
  };
  UIShell_WorkspaceMount drag_mount = uishell_workspace_mount_from_cfg(scratch.arena, drag_destination);
  RD_DockGeometry drag_geometry = rd_dock_geometry_from_mount(&drag_mount);
  F32 drag_widths[2] = {0};
  for(U32 i = 0; i < ArrayCount(drag_origins); i++)
  {
    CFG_Node *drag_view = drag_views[i];
    UIShell_RegsScope(.window = window->id, .panel = drag_origins[i]->id, .view = drag_view->id)
    {
      rd_drag_begin(UIShell_ContextRegSlot_View);
      drag_widths[i] = rd_dock_width_from_geometry(&drag_geometry, drag_destination, Dir2_Invalid);
      IntegrationCheck(drag_widths[i] == rd_dock_target_width(scratch.arena, drag_destination, Dir2_Invalid, drag_view, 0));
      rd_drag_kill();
    }
  }
  IntegrationCheck(drag_widths[0] != drag_widths[1]);
  // A last-View self split deliberately keeps its now-empty original Panel.
  // Generate both parent axes and all directions; predicted body width must
  // match command execution and the real renderer, without queued closure.
  for(U32 axis = 0; axis < 2; axis++)
  for(U32 direction = 1; direction < ArrayCount(directions); direction++)
  {
    panels = integration_reset_panels(window, axis);
    CFG_Node *origin = cfg_node_new(rd_state->cfg, panels, str8_lit("0.5"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, panels, str8_lit("0.5")), str8_lit("terminal"));
    source = cfg_node_new(rd_state->cfg, origin, str8_lit("scroll_region_fixture"));
    CFG_ID origin_id = origin->id;
    integration_rect.x1 = 1200;
    Dir2 dir = directions[direction];
    F32 predicted = rd_dock_target_width(scratch.arena, origin, dir, source, 0);
    U64 before_gen = cfg_change_gen();
    IntegrationCheck(predicted >= 128 && cfg_change_gen() == before_gen);
    UIShell_CmdNode *before_cmd = integration_move(window, source, origin, dir);
    IntegrationCheck(!integration_close_queued(before_cmd));
    IntegrationCheck(source->parent != origin && cfg_node_from_id(origin_id) == origin);
    F32 rendered = 0;
    integration_drag_site(ws, source, source->parent, Dir2_Invalid, &rendered);
    IntegrationCheck(rendered == predicted);
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
      IntegrationCheck(!integration_close_queued(before_cmd));
      F32 rendered = 0;
      integration_drag_site(ws, source, source->parent, Dir2_Invalid, &rendered);
      IntegrationCheck(rendered == boundary && integration_width(source->parent, Dir2_Invalid) == rendered);
    }
  }
  // Additional removal shapes exercise sibling rescaling and nested parent
  // collapse/flattening. Prediction must equal the committed rendered body,
  // and feedback must never modify the saved tree.
  // Reproducible pseudo-random positive allocations across flat/nested trees,
  // both axes and every direction compare the copied proposal to real commands.
  U32 allocation_seed = 0x196206;
  for(U32 variation = 0; variation < 8; variation++)
  for(U32 shape = 0; shape < 5; shape++)
  for(U32 axis = 0; axis < 2; axis++)
  for(U32 direction = 0; direction < ArrayCount(directions); direction++)
  {
    panels = integration_reset_panels(window, axis);
    CFG_Node *origin = cfg_node_new(rd_state->cfg, panels, str8_lit("0.2"));
    CFG_Node *destination = cfg_node_new(rd_state->cfg, panels, str8_lit("0.5"));
    CFG_Node *extra = cfg_node_new(rd_state->cfg, panels, str8_lit("0.3"));
    cfg_node_new(rd_state->cfg, extra, str8_lit("terminal"));
    if(variation)
    {
      CFG_Node *siblings[] = {origin, destination, extra};
      U32 weights[3], total = 0;
      for(U32 i = 0; i < ArrayCount(weights); i++)
      {
        allocation_seed = allocation_seed*1664525u+1013904223u;
        weights[i] = 200+(allocation_seed%400); total += weights[i];
      }
      for(U32 i = 0; i < ArrayCount(weights); i++)
      { cfg_node_equip_stringf(rd_state->cfg, siblings[i], "%f", (F64)weights[i]/total); }
    }
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
      // Keep three sibling Panels beside the filtered-tab source, exercising
      // multi-sibling rescaling as well as the move/split close-rule difference.
      for(CFG_Node *child = panels->first; child != &cfg_nil_node; child = child->next)
      { cfg_node_equip_stringf(rd_state->cfg, child, "%f", f64_from_str8(child->string)*.8); }
      cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, panels, str8_lit("0.2")), str8_lit("terminal"));
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
    integration_rect = r2f32p(0, 0, 2400, 1600);
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
    IntegrationCheck(!integration_close_queued(before_cmd));
    if(shape == 2)
    { IntegrationCheck((cfg_node_from_id(origin_id) == &cfg_nil_node) == (dir == Dir2_Invalid)); }
    F32 rendered = 0;
    integration_drag_site(ws, source, source->parent, Dir2_Invalid, &rendered);
    IntegrationCheck(rendered == predicted);
  }
  // Splitting the last View out of the focused Panel closes it in the same
  // edit and queues focus for its heir after the new panel's, as the queued
  // close did.
  {
    panels = integration_reset_panels(window, Axis2_X);
    CFG_Node *origin = cfg_node_new(rd_state->cfg, panels, str8_lit("0.3"));
    CFG_Node *destination = cfg_node_new(rd_state->cfg, panels, str8_lit("0.4"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, panels, str8_lit("0.3")), str8_lit("terminal"));
    cfg_node_new(rd_state->cfg, origin, str8_lit("selected"));
    source = cfg_node_new(rd_state->cfg, origin, str8_lit("scroll_region_fixture"));
    cfg_node_new(rd_state->cfg, destination, str8_lit("terminal"));
    CFG_ID origin_id = origin->id;
    integration_rect = r2f32p(0, 0, 2400, 1600);
    UIShell_CmdNode *before_cmd = integration_move(window, source, destination, Dir2_Right);
    IntegrationCheck(cfg_node_from_id(origin_id) == &cfg_nil_node && !integration_close_queued(before_cmd));
    CFG_ID last_focus = 0, first_focus = 0;
    for(UIShell_CmdNode *n = before_cmd ? before_cmd->next : rd_state->cmds[0].first; n; n = n->next)
    {
      if(str8_match(n->cmd.name, str8_lit("focus_panel"), 0))
      { if(first_focus == 0) { first_focus = n->cmd.regs->panel; } last_focus = n->cmd.regs->panel; }
    }
    IntegrationCheck(first_focus == source->parent->id && last_focus == destination->id);
  }
  integration_rect.y1 = 480;
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
    .arrangement = rd_arrangement_from_cfg(scratch.arena, floating)};
  floating_mount.panel_tree = rd_panel_tree_from_arrangement(scratch.arena, floating_mount.arrangement);
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

  // A boundary drag lays out its resized copy of the arrangement every frame
  // and leaves config alone until the mouse is released, then saves once.
  // A drag that ends without a release saves nothing.
  {
    integration_rect = r2f32p(0, 0, 640, 480);
    panels = integration_reset_panels(window, Axis2_X);
    CFG_Node *left = cfg_node_new(rd_state->cfg, panels, str8_lit("0.3"));
    CFG_Node *right = cfg_node_new(rd_state->cfg, panels, str8_lit("0.7"));
    cfg_node_new(rd_state->cfg, left, str8_lit("terminal"));
    cfg_node_new(rd_state->cfg, right, str8_lit("terminal"));
    Rng2F32 area = pad_2f32(integration_rect, -rd_window_edge_inset_px(ws));
    F32 total = dim_2f32(area).x;
    Vec2F32 grab = v2f32(area.x0+0.3f*total, center_2f32(area).y);
    for(S32 cancel = 1; cancel >= 0; cancel--)
    {
      F32 before = integration_panel_frame(ws, window, left, grab, UI_EventKind_Null);
      U64 before_gen = cfg_change_gen();
      integration_panel_frame(ws, window, left, grab, UI_EventKind_Press);
      IntegrationCheck(rd_state->boundary_resize.arena != 0);
      F32 dragged = 0;
      for(U32 step = 1; step <= 4; step++)
      {
        integration_panel_frame(ws, window, left, v2f32(grab.x+10.f*step, grab.y), UI_EventKind_Null);
        dragged = integration_panel_frame(ws, window, left, v2f32(grab.x+10.f*step, grab.y), UI_EventKind_Null);
        IntegrationCheck(dragged > before+10.f*step-2.f && dragged < before+10.f*step+2.f);
        IntegrationCheck(cfg_change_gen() == before_gen);
      }
      if(cancel)
      {
        // The action was killed: no release ever comes.
        ui_kill_action();
        integration_panel_frame(ws, window, left, grab, UI_EventKind_Null);
        F32 after = integration_panel_frame(ws, window, left, grab, UI_EventKind_Null);
        IntegrationCheck(rd_state->boundary_resize.arena == 0 && after == before);
        IntegrationCheck(cfg_change_gen() == before_gen && str8_match(left->string, str8_lit("0.3"), 0));
        continue;
      }
      integration_panel_frame(ws, window, left, v2f32(grab.x+40.f, grab.y), UI_EventKind_Release);
      U64 saved_gen = cfg_change_gen();
      IntegrationCheck(saved_gen != before_gen && rd_state->boundary_resize.arena == 0);
      F32 after = integration_panel_frame(ws, window, left, v2f32(grab.x+40.f, grab.y), UI_EventKind_Null);
      IntegrationCheck(after == dragged && cfg_change_gen() == saved_gen);
      // Within the pixel the panel area's rect rounds to.
      F32 expected = 0.3f+40.f/total;
      F32 saved_left = (F32)f64_from_str8(left->string), saved_right = (F32)f64_from_str8(right->string);
      IntegrationCheck(abs_f32(saved_left-expected) < 1.f/total && abs_f32(saved_left+saved_right-1.f) < .00001f);
      // A double click gives both sides equal shares.
      Vec2F32 boundary = v2f32(area.x0+saved_left*total, grab.y);
      integration_panel_frame(ws, window, left, boundary, UI_EventKind_Null);
      integration_event_gap_us = 0;
      for(U32 click = 0; click < 2; click++)
      {
        integration_panel_frame(ws, window, left, boundary, UI_EventKind_Press);
        integration_panel_frame(ws, window, left, boundary, UI_EventKind_Release);
      }
      integration_event_gap_us = 1000000;
      IntegrationCheck(str8_match(left->string, str8_lit("0.500000"), 0) && str8_match(right->string, str8_lit("0.500000"), 0));
    }
  }

  scratch_end(scratch);
  fprintf(stderr, "Docking integration: %u failures\n", failures);
  abort_self(failures ? 1 : 0);
}
