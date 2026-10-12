// Compiled by tools/test-docking-integration.py from the production shell
// amalgamation with its entry point replaced. Links real Andamento/Cleat native
// libraries and native ingress; headless WM/renderer/font backends avoid a display.
// Tests use the real panel UI, live arrangements, commands, checker and
// settings evaluator. OS window sizing is the fault boundary; arrangements,
// command dispatch and validity checking are real.
global Rng2F32 integration_rect;
internal Rng2F32 integration_client_rect(WM_Window window) { return integration_rect; }

//- Fixtures: a host's own panels (a window's, or a workspace's), edited
// through its live arrangement as commands edit it.

// What changes when the config or any arrangement does: both only count up.
internal U64
integration_gen(void)
{
  return cfg_change_gen() + rd_state->arrangement_version;
}

// `host`'s panels made one empty root along `axis`; their Views go.
internal RD_PanelAddress
integration_reset_panels(CFG_Node *host, Axis2 axis)
{
  Temp scratch = scratch_begin(0, 0);
  RD_PanelOwner owner = rd_fixture_owner(host);
  RD_Arrangement *arrangement = rd_arrangement_from_panel_owner(scratch.arena, owner);
  RD_PanelID root = rd_arrangement_clear(arrangement);
  arrangement->root_axis = axis;
  rd_arrangement_set(arrangement);
  scratch_end(scratch);
  return rd_panel_address(owner, root);
}

// A new panel of `weight` (as written, unscaled) as `parent`'s last child.
internal RD_PanelAddress
integration_panel(RD_PanelAddress parent, F32 weight)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *arrangement = rd_arrangement_from_panel_owner(scratch.arena, parent.owner);
  RD_PanelID id = rd_arrangement_add(arrangement, parent.id, 1.f);
  rd_arrangement_panel_from_id(arrangement, id)->weight = weight;
  rd_arrangement_set(arrangement);
  scratch_end(scratch);
  return rd_panel_address(parent.owner, id);
}

internal F32
integration_weight(RD_PanelAddress panel)
{
  return rd_arrangement_panel_from_id(rd_arrangement_live(panel.owner), panel.id)->weight;
}

internal void
integration_set_weight(RD_PanelAddress panel, F32 weight)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *arrangement = rd_arrangement_from_panel_owner(scratch.arena, panel.owner);
  rd_arrangement_panel_from_id(arrangement, panel.id)->weight = weight;
  rd_arrangement_set(arrangement);
  scratch_end(scratch);
}

// A new View of `kind` as `panel`'s last tab.
internal CFG_Node *
integration_view(RD_PanelAddress panel, char *kind)
{
  return rd_fixture_view(panel, str8_cstring(kind), str8_zero(), 0);
}

// The `n`th Floating Panel `host` (a window or a workspace) has, made of one
// empty panel.
internal RD_PanelAddress
integration_floating(CFG_Node *host, U64 n)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *arrangement = rd_arrangement_alloc(scratch.arena, rd_panel_owner_from_host(RD_PanelOwnerKind_Floating, host, n));
  RD_PanelID root = rd_arrangement_clear(arrangement);
  rd_arrangement_set(arrangement);
  scratch_end(scratch);
  return rd_panel_address(arrangement->owner, root);
}

internal RD_ViewAddress
integration_tab(RD_PanelAddress panel, U64 index)
{
  RD_ArrangementTab *t = rd_arrangement_panel_from_id(rd_arrangement_live(panel.owner), panel.id)->first_tab;
  for(U64 i = 0; t && i < index; i++) { t = t->next; }
  return t ? t->view : rd_nil_view_address;
}

internal B32
integration_in(CFG_Node *view, RD_PanelAddress panel)
{
  return rd_panel_address_match(rd_panel_from_cfg(view), panel);
}

internal F32
integration_width(RD_PanelAddress destination, Dir2 dir)
{
  Temp scratch = scratch_begin(0, 0);
  F32 width = rd_dock_target_width(scratch.arena, destination, dir, rd_nil_view_address, 0);
  scratch_end(scratch);
  return width;
}
internal B32
integration_drag_site(RD_WindowState *ws, CFG_Node *source, RD_PanelAddress destination_panel, Dir2 dir, F32 *rendered_width)
{
  Temp scratch = scratch_begin(0, 0);
  UI_State *previous_ui = ui_state, *gesture_ui = ui_state_alloc();
  ui_select_state(gesture_ui);
  UIShell_WorkspaceMount mount = uishell_workspace_mount_from_panel_owner(scratch.arena, destination_panel.owner);
  RD_PanelNode *target = rd_panel_node_from_id(mount.panel_tree.root, destination_panel.id);
  B32 insertion = dir != Dir2_Invalid && target->parent != &rd_nil_panel_node &&
    target->parent->split_axis == axis2_from_dir2(dir);
  char *names[] = {"up", "down", "left", "right"};
  String8 site_name = str8_zero();
  if(dir == Dir2_Up) site_name = str8_cstring(names[0]);
  if(dir == Dir2_Down) site_name = str8_cstring(names[1]);
  if(dir == Dir2_Left) site_name = str8_cstring(names[2]);
  if(dir == Dir2_Right) site_name = str8_cstring(names[3]);
  RD_PanelNode *boundary = side_from_dir2(dir) == Side_Max ? target->next : target;
  // A drop into the panel lands on its tab strip.
  UI_Key site = insertion ? ui_key_from_stringf(rd_panel_ui_key(target->parent->address), "drop_boundary_%I64u", boundary->address.id) :
    dir == Dir2_Invalid ? rd_panel_catchall_drop_site_key(destination_panel) :
    ui_key_from_stringf(rd_panel_ui_key(destination_panel), "drop_split_%S", site_name);
  F32 layout_font_size = 0;
  UIShell_RegsScope(.window = ws->cfg_id, .panel = rd_nil_panel_address, .view = rd_nil_view_address, .tab = rd_nil_view_address)
  { layout_font_size = rd_font_size(); }
  UIShell_RegsScope(.window = ws->cfg_id, .panel = rd_panel_from_cfg(source), .view = rd_view_address_from_cfg(source))
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
      ui_state->mouse = center_2f32(rd_target_rect_from_panel_node(area, mount.panel_tree.root, target));
      rd_panel_area_ui(scratch, area, integration_rect, ws, &mount, 1, 0, 0, 0, 0);
    }
    ui_end_build();
    rd_drag_kill();
  }
  B32 exists = !ui_box_is_nil(ui_box_from_key(site));
  if(rendered_width)
  {
    UI_Box *body = ui_box_from_key(ui_key_from_stringf(rd_panel_ui_key(destination_panel), "panel_box"));
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
integration_panel_frame(RD_WindowState *ws, CFG_Node *window, RD_PanelAddress leaf, Vec2F32 mouse, UI_EventKind kind)
{
  Temp scratch = scratch_begin(0, 0);
  rd_state->frame_index += 1;
  UIShell_WorkspaceMount mount = uishell_workspace_mount_from_panel_owner(scratch.arena, rd_fixture_owner(window));
  F32 layout_font_size = 0;
  UIShell_RegsScope(.window = ws->cfg_id, .panel = rd_nil_panel_address, .view = rd_nil_view_address, .tab = rd_nil_view_address)
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
  UI_Box *body = ui_box_from_key(ui_key_from_stringf(rd_panel_ui_key(leaf), "panel_box"));
  scratch_end(scratch);
  return ui_box_is_nil(body) ? 0 : dim_2f32(body->rect).x;
}
#define IntegrationCheck(x) do { if(!(x)) { fprintf(stderr, "FAIL integration line %d: %s\n", __LINE__, #x); failures++; } } while(0)

internal UIShell_CmdNode *
integration_move(CFG_Node *window, CFG_Node *source, RD_PanelAddress destination, Dir2 dir)
{
  UIShell_CmdNode *before = rd_state->cmds[0].last;
  UIShell_RegsScope(.window = window->id, .panel = rd_panel_from_cfg(source), .view = rd_view_address_from_cfg(source),
                   .dst_panel = destination, .dir2 = dir)
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
  // Proposals copy the arrangement; src/shell/tests/arrangement.c checks that
  // a deep split chain copies without exhausting the C stack.
  CFG_Node *user = cfg_node_new(rd_state->cfg, cfg_node_root(), str8_lit("user"));
  CFG_Node *window = cfg_node_new(rd_state->cfg, user, str8_lit("window"));
  // Nonzero spacing makes renderer/measurement drift observable.
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, user, str8_lit("panel_gap")), str8_lit("1"));
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, user, str8_lit("font_size")), str8_lit("16"));
  // Window chrome uses its own size even with a different View override.
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, window, str8_lit("font_size")), str8_lit("24"));
  cfg_node_new(rd_state->cfg, window, str8_lit("control_split_collapsed"));
  RD_PanelAddress panels = integration_reset_panels(window, Axis2_Y);
  CFG_Node *source = integration_view(panels, "scroll_region_fixture");
  // Keep a second tab so moving the fixture cannot close the source panel.
  integration_view(panels, "terminal");
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
    RD_PanelAddress origin = shape ? integration_panel(panels, 0.3f) : panels;
    RD_PanelAddress destination = shape ? integration_panel(panels, 0.7f) : panels;
    source = integration_view(origin, "scroll_region_fixture");
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, source, str8_lit("font_size")), str8_lit("72"));
    integration_view(origin, "terminal");
    integration_view(destination, "terminal");
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
      U64 resized_gen = integration_gen();
      UIShell_RegsScope(.window = window->id, .panel = origin, .view = rd_view_address_from_cfg(source),
                       .dst_panel = destination, .dir2 = dir)
      {
        rd_drag_begin(UIShell_ContextRegSlot_View);
        if(dir == Dir2_Invalid) { uishell_dispatch_tab_command(str8_lit("move_view")); }
        else { uishell_dispatch_panel_command(str8_lit("split_panel")); }
        rd_drag_kill();
      }
      IntegrationCheck(integration_in(source, origin) && integration_gen() == resized_gen);
      integration_rect.x1 = shown_width;
    }
    if(shape && axis == Axis2_X && boundary == 128)
    {
      // Inserting another sibling after feedback changes both the target's
      // allocation and the insertion denominator. Commit uses the new tree.
      integration_set_weight(origin, 0.27f);
      integration_set_weight(destination, 0.63f);
      RD_PanelAddress extra = integration_panel(panels, 0.1f);
      integration_view(extra, "terminal");
      U64 changed_layout_gen = integration_gen();
      IntegrationCheck(integration_width(destination, dir) < 128);
      UIShell_RegsScope(.window = window->id, .panel = origin, .view = rd_view_address_from_cfg(source),
                       .dst_panel = destination, .dir2 = dir)
      {
        if(dir == Dir2_Invalid) { uishell_dispatch_tab_command(str8_lit("move_view")); }
        else { uishell_dispatch_panel_command(str8_lit("split_panel")); }
      }
      IntegrationCheck(integration_in(source, origin) && integration_gen() == changed_layout_gen);
      rd_fixture_remove_panel(extra);
      integration_set_weight(origin, 0.3f);
      integration_set_weight(destination, 0.7f);
    }
    // Scripted commands must enforce the same measured rule as drag feedback.
    RD_PanelAddress before_parent = rd_panel_from_cfg(source);
    U64 before_gen = integration_gen();
    UIShell_RegsScope(.window = window->id, .panel = origin, .view = rd_view_address_from_cfg(source),
                     .dst_panel = destination, .dir2 = dir)
    {
      if(dir == Dir2_Invalid) { uishell_dispatch_tab_command(str8_lit("move_view")); }
      else { uishell_dispatch_panel_command(str8_lit("split_panel")); }
    }
    IntegrationCheck(!integration_in(source, before_parent) == (boundary >= 128));
    if(boundary < 128) { IntegrationCheck(integration_gen() == before_gen); }
    if(boundary >= 128)
    {
      F32 actual = integration_width(rd_panel_from_cfg(source), Dir2_Invalid), rendered = 0;
      // Committed measurement must equal the body rectangle the real UI lays
      // out, so a future rendering-only inset change cannot silently drift.
      integration_drag_site(ws, source, rd_panel_from_cfg(source), Dir2_Invalid, &rendered);
      IntegrationCheck(actual == boundary && rendered == actual);
    }
  }
  // Cancelling and restarting a drag reuses only the immutable layout tree;
  // the active View's distinct source allocation is read anew on every query.
  panels = integration_reset_panels(window, Axis2_X);
  RD_PanelAddress drag_origins[] = {integration_panel(panels, 0.2f), integration_panel(panels, 0.3f)};
  RD_PanelAddress drag_destination = integration_panel(panels, 0.5f);
  integration_view(drag_destination, "terminal");
  integration_rect.x1 = 1200;
  CFG_Node *drag_views[] = {integration_view(drag_origins[0], "scroll_region_fixture"), integration_view(drag_origins[1], "scroll_region_fixture")};
  UIShell_WorkspaceMount drag_mount = uishell_workspace_mount_from_panel_owner(scratch.arena, drag_destination.owner);
  RD_DockGeometry drag_geometry = rd_dock_geometry_from_mount(&drag_mount);
  F32 drag_widths[2] = {0};
  for(U32 i = 0; i < ArrayCount(drag_origins); i++)
  {
    CFG_Node *drag_view = drag_views[i];
    UIShell_RegsScope(.window = window->id, .panel = drag_origins[i], .view = rd_view_address_from_cfg(drag_view))
    {
      rd_drag_begin(UIShell_ContextRegSlot_View);
      drag_widths[i] = rd_dock_width_from_geometry(&drag_geometry, drag_destination, Dir2_Invalid);
      IntegrationCheck(drag_widths[i] == rd_dock_target_width(scratch.arena, drag_destination, Dir2_Invalid, rd_view_address_from_cfg(drag_view), 0));
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
    RD_PanelAddress origin = integration_panel(panels, 0.5f);
    integration_view(integration_panel(panels, 0.5f), "terminal");
    source = integration_view(origin, "scroll_region_fixture");
    integration_rect.x1 = 1200;
    Dir2 dir = directions[direction];
    U64 before_gen = integration_gen();
    F32 predicted = rd_dock_target_width(scratch.arena, origin, dir, rd_view_address_from_cfg(source), 0);
    IntegrationCheck(predicted >= 128 && integration_gen() == before_gen);
    UIShell_CmdNode *before_cmd = integration_move(window, source, origin, dir);
    IntegrationCheck(!integration_close_queued(before_cmd));
    IntegrationCheck(!integration_in(source, origin) && rd_panel_exists(origin));
    F32 rendered = 0;
    integration_drag_site(ws, source, rd_panel_from_cfg(source), Dir2_Invalid, &rendered);
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
    RD_PanelAddress origin = integration_panel(panels, 0.3f);
    RD_PanelAddress destination = integration_panel(panels, 0.7f);
    source = integration_view(origin, "scroll_region_fixture");
    integration_view(destination, "terminal");
    Dir2 dir = directions[direction];
    // With two siblings: a center move fills the root; matching-axis
    // insertion allocates 1/3 then removes the source's serialized 1/5,
    // leaving 5/12. Cross-axis bisection fills the root's width (Y), or half (X).
    F32 fraction = dir == Dir2_Invalid ? 1.f :
      axis2_from_dir2(dir) == Axis2_X ? (axis == Axis2_X ? 0.416666f : 0.5f) : 1.f;
    F32 inset = 0;
    UIShell_RegsScope(.window = window->id, .panel = rd_nil_panel_address, .view = rd_nil_view_address, .tab = rd_nil_view_address)
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
    U64 before_gen = integration_gen();
    UIShell_CmdNode *before_cmd = integration_move(window, source, destination, dir);
    IntegrationCheck(!integration_in(source, origin) == (boundary >= 128));
    if(boundary < 128) { IntegrationCheck(integration_gen() == before_gen); }
    else
    {
      IntegrationCheck(!integration_close_queued(before_cmd));
      F32 rendered = 0;
      integration_drag_site(ws, source, rd_panel_from_cfg(source), Dir2_Invalid, &rendered);
      IntegrationCheck(rendered == boundary && integration_width(rd_panel_from_cfg(source), Dir2_Invalid) == rendered);
    }
  }
  // Additional removal shapes exercise sibling rescaling and nested parent
  // collapse/flattening. Prediction must equal the committed rendered body,
  // and feedback must never modify the arrangement.
  // Reproducible pseudo-random positive allocations across flat/nested trees,
  // both axes and every direction compare the copied proposal to real commands.
  U32 allocation_seed = 0x196206;
  for(U32 variation = 0; variation < 8; variation++)
  for(U32 shape = 0; shape < 5; shape++)
  for(U32 axis = 0; axis < 2; axis++)
  for(U32 direction = 0; direction < ArrayCount(directions); direction++)
  {
    panels = integration_reset_panels(window, axis);
    RD_PanelAddress origin = integration_panel(panels, 0.2f);
    RD_PanelAddress destination = integration_panel(panels, 0.5f);
    RD_PanelAddress extra = integration_panel(panels, 0.3f);
    integration_view(extra, "terminal");
    if(variation)
    {
      RD_PanelAddress siblings[] = {origin, destination, extra};
      U32 weights[3], total = 0;
      for(U32 i = 0; i < ArrayCount(weights); i++)
      {
        allocation_seed = allocation_seed*1664525u+1013904223u;
        weights[i] = 200+(allocation_seed%400); total += weights[i];
      }
      for(U32 i = 0; i < ArrayCount(weights); i++)
      { integration_set_weight(siblings[i], (F32)f64_from_str8(push_str8f(scratch.arena, "%f", (F64)weights[i]/total))); }
    }
    if(shape == 1)
    {
      RD_PanelAddress container = origin;
      origin = integration_panel(container, 0.4f);
      RD_PanelAddress keep = integration_panel(container, 0.6f);
      for(U32 child = 0; child < 2; child++) { integration_view(integration_panel(keep, 0.5f), "terminal"); }
    }
    source = integration_view(origin, "scroll_region_fixture");
    integration_view(destination, "terminal");
    if(shape == 2)
    {
      // Keep three sibling Panels beside the filtered-tab source, exercising
      // multi-sibling rescaling as well as the move/split close-rule difference.
      RD_PanelAddress siblings[] = {origin, destination, extra};
      for(U32 i = 0; i < ArrayCount(siblings); i++)
      { integration_set_weight(siblings[i], (F32)f64_from_str8(push_str8f(scratch.arena, "%f", integration_weight(siblings[i])*.8))); }
      integration_view(integration_panel(panels, 0.2f), "terminal");
      // move_view ignores project-filtered sibling tabs for source closure;
      // split_panel retains a source with any tab. Both outcomes must match UI.
      CFG_Node *hidden = integration_view(origin, "terminal");
      cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, hidden, str8_lit("project")), str8_lit("/unselected-project"));
      IntegrationCheck(rd_cfg_is_project_filtered(hidden));
    }
    if(shape >= 3)
    {
      // Hand-edited full/overfull source allocations recover finitely after
      // closure, rather than divide by zero or produce negative allocations.
      integration_set_weight(origin, shape == 3 ? 1.f : 1.1f);
      integration_set_weight(destination, 0);
      integration_set_weight(extra, 0);
    }
    integration_rect = r2f32p(0, 0, 2400, 1600);
    Dir2 dir = directions[direction];
    U64 before_gen = integration_gen();
    F32 predicted = rd_dock_target_width(scratch.arena, destination, dir, rd_view_address_from_cfg(source), 0);
    IntegrationCheck(predicted >= 128);
    if(shape < 3) { IntegrationCheck(integration_drag_site(ws, source, destination, dir, 0)); }
    // Malformed zero-sized destinations have no reachable UI hit region;
    // validate the production feedback checker with the measured proposal.
    else { IntegrationCheck(rd_dock_drag_target(rd_dock_view_from_address(rd_view_address_from_cfg(source)), rd_dock_host_from_panel(destination, predicted))); }
    IntegrationCheck(integration_gen() == before_gen);
    UIShell_CmdNode *before_cmd = integration_move(window, source, destination, dir);
    IntegrationCheck(!integration_close_queued(before_cmd));
    if(shape == 2)
    { IntegrationCheck(!rd_panel_exists(origin) == (dir == Dir2_Invalid)); }
    F32 rendered = 0;
    integration_drag_site(ws, source, rd_panel_from_cfg(source), Dir2_Invalid, &rendered);
    IntegrationCheck(rendered == predicted);
  }
  // Splitting the last View out of the focused Panel closes it in the same
  // edit, and focus follows the View into the new panel (#329).
  {
    panels = integration_reset_panels(window, Axis2_X);
    RD_PanelAddress origin = integration_panel(panels, 0.3f);
    RD_PanelAddress destination = integration_panel(panels, 0.4f);
    integration_view(integration_panel(panels, 0.3f), "terminal");
    source = integration_view(origin, "scroll_region_fixture");
    integration_view(destination, "terminal");
    rd_panel_focus_mark(origin);
    integration_rect = r2f32p(0, 0, 2400, 1600);
    UIShell_CmdNode *before_cmd = integration_move(window, source, destination, Dir2_Right);
    IntegrationCheck(!rd_panel_exists(origin) && !integration_close_queued(before_cmd));
    RD_PanelAddress last_focus = rd_nil_panel_address;
    for(UIShell_CmdNode *n = before_cmd ? before_cmd->next : rd_state->cmds[0].first; n; n = n->next)
    {
      if(str8_match(n->cmd.name, str8_lit("focus_panel"), 0)) { last_focus = n->cmd.regs->panel; }
    }
    IntegrationCheck(rd_panel_address_match(last_focus, rd_panel_from_cfg(source)));
  }
  // Closing a panel's last tab closes the panel (#358): the panel beside it
  // takes its space and, when it had focus, focus. The last panel stays.
  {
    panels = integration_reset_panels(window, Axis2_X);
    RD_PanelAddress left = integration_panel(panels, 0.3f);
    RD_PanelAddress middle = integration_panel(panels, 0.3f);
    RD_PanelAddress right = integration_panel(panels, 0.4f);
    CFG_Node *other = integration_view(left, "terminal");
    CFG_Node *closing = integration_view(middle, "terminal");
    CFG_Node *kept = integration_view(right, "terminal");
    rd_panel_focus_mark(middle);
    UIShell_CmdNode *before_cmd = rd_state->cmds[0].last;
    UIShell_RegsScope(.window = window->id, .panel = middle, .tab = rd_view_address_from_cfg(closing)) { uishell_dispatch_tab_command(str8_lit("close_tab")); }
    IntegrationCheck(!rd_panel_exists(middle));
    RD_Arrangement *closed = rd_arrangement_live(panels.owner);
    IntegrationCheck(closed->root->child_count == 2);
    F32 weights = 0;
    for(RD_ArrangementPanel *p = closed->root->first; p != &rd_nil_arrangement_panel; p = p->next) { weights += p->weight; }
    IntegrationCheck(abs_f32(weights-1.f) < .001f);
    RD_PanelAddress last_focus = rd_nil_panel_address;
    for(UIShell_CmdNode *n = before_cmd ? before_cmd->next : rd_state->cmds[0].first; n; n = n->next)
    { if(str8_match(n->cmd.name, str8_lit("focus_panel"), 0)) { last_focus = n->cmd.regs->panel; } }
    IntegrationCheck(rd_panel_address_match(last_focus, left));
    // The two left collapse into one when either closes; the last stays.
    UIShell_RegsScope(.window = window->id, .panel = left, .tab = rd_view_address_from_cfg(other)) { uishell_dispatch_tab_command(str8_lit("close_tab")); }
    closed = rd_arrangement_live(panels.owner);
    IntegrationCheck(closed->root->first == &rd_nil_arrangement_panel && rd_arrangement_panel_from_view(closed, rd_view_address_from_cfg(kept)) == closed->root);
    UIShell_RegsScope(.window = window->id, .panel = rd_panel_from_cfg(kept), .tab = rd_view_address_from_cfg(kept)) { uishell_dispatch_tab_command(str8_lit("close_tab")); }
    closed = rd_arrangement_live(panels.owner);
    IntegrationCheck(closed->root != &rd_nil_arrangement_panel && closed->root->tab_count == 0);
  }
  // A View re-addressed onto an address that still has a Presentation State
  // record, which no live View can own, keeps its own settings, and the
  // address keeps one record.
  {
    panels = integration_reset_panels(window, Axis2_X);
    CFG_Node *view = integration_view(panels, "terminal");
    RD_ViewAddress from = rd_view_address_from_cfg(view);
    rd_view_setting_set(from, str8_lit("font_size"), str8_lit("10"));
    CFG_Node *root = cfg_node_child_from_string(window, RD_VIEW_PRESENTATION_ROOT);
    CFG_Node *stale = cfg_node_new(rd_state->cfg, root, rd_view_owner_kind_name(from.owner));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, stale, str8_lit("key")), str8_lit("u:99"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, stale, str8_lit("font_size")), str8_lit("20"));
    cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("slot")), str8_lit("u:99"));
    RD_ViewAddress to = rd_view_address_from_cfg(view);
    IntegrationCheck(!rd_view_address_match(from, to));
    rd_view_readdress(from, to);
    IntegrationCheck(str8_match(rd_view_setting_from_view(to, str8_lit("font_size")), str8_lit("10"), 0));
    IntegrationCheck(rd_view_presentation_from_view(from) == &cfg_nil_node);
    U64 records = 0;
    for(CFG_Node *c = root->first; c != &cfg_nil_node; c = c->next)
    { records += str8_match(cfg_node_child_from_string(c, str8_lit("key"))->first->string, str8_lit("u:99"), 0); }
    IntegrationCheck(records == 1);
    cfg_node_release(rd_state->cfg, root);
  }
  integration_rect.y1 = 480;
  // A shown target is remeasured when the drop is committed after a resize.
  panels = integration_reset_panels(window, Axis2_Y);
  RD_PanelAddress origin = integration_panel(panels, 0.5f);
  RD_PanelAddress destination = integration_panel(panels, 0.5f);
  source = integration_view(origin, "scroll_region_fixture");
  CFG_Node *ordinary = integration_view(origin, "terminal");
  integration_view(destination, "terminal");
  integration_rect.x1 = 640;
  IntegrationCheck(integration_drag_site(ws, source, destination, Dir2_Invalid, 0));
  integration_rect.x1 = 100;
  U64 before_resize = integration_gen();
  log_scope_begin();
  UIShell_RegsScope(.window = window->id, .panel = origin, .view = rd_view_address_from_cfg(source), .dst_panel = destination)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  LogScopeResult resize_log = log_scope_end(scratch.arena);
  String8 explanation = rd_dock_rule_message(RD_DockRule_MinimumWidth);
  // Scripted width refusals retain the checker’s user-facing explanation.
  IntegrationCheck(str8_find_needle(resize_log.strings[LogMsgKind_UserError], 0, explanation, 0) !=
                   resize_log.strings[LogMsgKind_UserError].size);
  IntegrationCheck(integration_in(source, origin) && integration_gen() == before_resize);
  // A destination with no live window reports unavailable geometry explicitly.
  CFG_Node *unopened = cfg_node_new(rd_state->cfg, user, str8_lit("window"));
  RD_PanelAddress unopened_root = integration_reset_panels(unopened, Axis2_Y);
  RD_PanelAddress unopened_origin = integration_panel(unopened_root, 0.5f);
  RD_PanelAddress unopened_panel = integration_panel(unopened_root, 0.5f);
  UIShell_WorkspaceMount absent_mount = uishell_workspace_mount_from_panel_owner(scratch.arena, unopened_panel.owner);
  log_scope_begin();
  rd_drag_begin(UIShell_ContextRegSlot_View);
  RD_DockGeometry absent_geometry = rd_dock_geometry_from_mount(&absent_mount);
  rd_drag_kill();
  LogScopeResult drag_log = log_scope_end(scratch.arena);
  // Active-drag geometry without window state is silent, and zero measured.
  IntegrationCheck(dim_2f32(absent_geometry.area).x == 0 && drag_log.strings[LogMsgKind_UserError].size == 0);
  CFG_Node *unopened_source = integration_view(unopened_origin, "scroll_region_fixture");
  log_scope_begin();
  IntegrationCheck(!rd_dock_move_allowed(scratch.arena, "move", rd_view_address_from_cfg(unopened_source), unopened_panel, Dir2_Invalid));
  LogScopeResult missing_log = log_scope_end(scratch.arena);
  String8 missing_reason = str8_lit("destination has no live window state");
  IntegrationCheck(str8_find_needle(missing_log.strings[LogMsgKind_UserError], 0, missing_reason, 0) !=
                   missing_log.strings[LogMsgKind_UserError].size);
  // Zero-minimum commands into an unopened host succeed without an error.
  CFG_Node *unconstrained = integration_view(unopened_origin, "terminal");
  log_scope_begin();
  UIShell_RegsScope(.window = window->id, .panel = unopened_origin, .view = rd_view_address_from_cfg(unconstrained), .dst_panel = unopened_panel)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  LogScopeResult accepted_log = log_scope_end(scratch.arena);
  IntegrationCheck(integration_in(unconstrained, unopened_panel) && accepted_log.strings[LogMsgKind_UserError].size == 0);
  cfg_node_release(rd_state->cfg, unopened);
  rd_arrangements_release_unowned();
  // Unrendered floating hosts retain the declared checker: zero-minimum
  // ordinary Views fit; the 128px fixture refuses measured-zero placement.
  RD_PanelAddress floating = integration_floating(window, rd_floating_count(window)+1);
  integration_view(floating, "terminal");
  // Current mount builders select rendered panels/sidebar roots. Exercise a
  // future floating mount explicitly to cover the defensive no-render branch.
  UIShell_WorkspaceMount floating_mount = {.window_cfg = window, .owner_cfg = window,
    .arrangement = rd_arrangement_from_panel_owner(scratch.arena, floating.owner), .owner = floating.owner};
  floating_mount.panel_tree = rd_panel_tree_from_arrangement(scratch.arena, floating_mount.owner, floating_mount.arrangement);
  RD_DockGeometry floating_geometry = rd_dock_geometry_from_mount(&floating_mount);
  F32 floating_width = rd_dock_width_from_geometry(&floating_geometry, floating_mount.panel_tree.root->address, Dir2_Invalid);
  IntegrationCheck(floating_width == 0);
  // The window's own Floating Panels are its sidebar's level; these Views'
  // level is its own panels', which the host is taken at here.
  RD_DockHost floating_host = rd_dock_host_from_panel(floating, floating_width);
  floating_host.level = rd_dock_view_from_address(rd_view_address_from_cfg(source)).level;
  IntegrationCheck(floating_host.kind == RD_DockHostKind_FloatingPanel);
  IntegrationCheck(rd_dock_placement(rd_dock_view_from_address(rd_view_address_from_cfg(source)), floating_host) == RD_DockRule_MinimumWidth);
  IntegrationCheck(rd_dock_placement(rd_dock_view_from_address(rd_view_address_from_cfg(ordinary)), floating_host) == RD_DockRule_Valid);
  {
    RD_Arrangement *gone = rd_arrangement_from_panel_owner(scratch.arena, floating.owner);
    rd_arrangement_discard(gone);
    rd_arrangement_set(gone);
  }
  // Restore retains its structural policy even when geometry is too narrow.
  rd_dock_restore_window(window);
  IntegrationCheck(integration_in(source, origin));

  // Each Floating Panel is its own arrangement, and the tab and drop
  // commands edit it through the module like any other: one Selected View,
  // and no split.
  {
    CFG_Node *other = cfg_node_new(rd_state->cfg, user, str8_lit("window"));
    CFG_Node *workspace = cfg_node_new(rd_state->cfg, other, str8_lit("workspace"));
    RD_PanelAddress workspace_root = integration_reset_panels(workspace, Axis2_Y);
    RD_PanelAddress left = integration_panel(workspace_root, 0.5f);
    RD_PanelAddress right = integration_panel(workspace_root, 0.5f);
    CFG_Node *lone = integration_view(left, "terminal");
    integration_view(right, "terminal");
    RD_PanelAddress first = integration_floating(workspace, 1);
    RD_PanelAddress second = integration_floating(workspace, 2);
    CFG_Node *a = integration_view(first, "text");
    CFG_Node *b = integration_view(first, "terminal");
    CFG_Node *c = integration_view(second, "terminal");
    rd_fixture_select(a, 1);
    rd_fixture_select(c, 1);
    // A workspace panel emptied into its workspace's Floating Panel closes,
    // in the same command; the View lands selected, the only one selected.
    UIShell_CmdNode *before_cmd = integration_move(other, lone, second, Dir2_Invalid);
    IntegrationCheck(integration_in(lone, second) && rd_view_address_match(integration_tab(second, 0), rd_view_address_from_cfg(lone)) && !integration_close_queued(before_cmd));
    IntegrationCheck(!rd_panel_exists(left) && rd_arrangement_live(workspace_root.owner)->root->id == right.id);
    IntegrationCheck(rd_view_is_selected(rd_view_address_from_cfg(lone)) && !rd_view_is_selected(rd_view_address_from_cfg(c)));
    IntegrationCheck(rd_arrangement_live(second.owner)->root->first == &rd_nil_arrangement_panel);
    // Selecting a tab selects it.
    UIShell_RegsScope(.window = other->id, .tab = rd_view_address_from_cfg(b)) { uishell_dispatch_tab_command(str8_lit("focus_tab")); }
    IntegrationCheck(rd_view_is_selected(rd_view_address_from_cfg(b)) && !rd_view_is_selected(rd_view_address_from_cfg(a)));
    // Reordering a tab moves it within its Floating Panel.
    UIShell_RegsScope(.window = other->id, .tab = rd_view_address_from_cfg(a)) { uishell_dispatch_tab_command(str8_lit("move_tab_right")); }
    UIShell_Cmd *reorder = &rd_state->cmds[0].last->cmd;
    IntegrationCheck(str8_match(reorder->name, str8_lit("move_view"), 0) && rd_panel_address_match(reorder->regs->dst_panel, first) && rd_view_address_match(reorder->regs->prev_tab, rd_view_address_from_cfg(b)));
    UIShell_RegsScope(.window = other->id, .view = rd_view_address_from_cfg(a), .dst_panel = first, .prev_tab = rd_view_address_from_cfg(b))
    { uishell_dispatch_tab_command(str8_lit("move_view")); }
    IntegrationCheck(integration_in(a, first) && rd_view_address_match(integration_tab(first, 1), rd_view_address_from_cfg(a)));
    // Another Floating Panel's source is reselected; emptied, it stays.
    integration_move(other, b, second, Dir2_Invalid);
    IntegrationCheck(integration_in(b, second) && rd_view_address_match(integration_tab(second, 0), rd_view_address_from_cfg(b)) && rd_view_is_selected(rd_view_address_from_cfg(a)));
    integration_move(other, a, second, Dir2_Invalid);
    IntegrationCheck(integration_in(a, second) && rd_panel_exists(first) && rd_floating_count(workspace) == 2 && rd_view_address_is_nil(integration_tab(first, 0)));
    // Duplicating places the copy after its source; closing releases it.
    UIShell_RegsScope(.window = other->id, .tab = rd_view_address_from_cfg(c)) { uishell_dispatch_tab_command(str8_lit("duplicate_tab")); }
    RD_ViewAddress copy = rd_nil_view_address;
    for(U64 i = 0; !rd_view_address_is_nil(integration_tab(second, i)); i++)
    { if(rd_view_address_match(integration_tab(second, i), rd_view_address_from_cfg(c))) { copy = integration_tab(second, i+1); } }
    IntegrationCheck(rd_panel_address_match(rd_panel_from_view(copy), second) && str8_match(rd_cfg_from_view(copy)->string, str8_lit("terminal"), 0));
    IntegrationCheck(rd_view_is_selected(copy));
    UIShell_RegsScope(.window = other->id, .tab = copy) { uishell_dispatch_tab_command(str8_lit("close_tab")); }
    IntegrationCheck(rd_cfg_from_view(copy) == &cfg_nil_node);
    // A new tab goes after the last tab.
    UIShell_RegsScope(.window = other->id, .panel = second, .string = str8_lit("text"), .expr = str8_zero())
    { uishell_dispatch_tab_command(str8_lit("build_tab")); }
    RD_ArrangementPanel *second_panel = rd_arrangement_panel_from_id(rd_arrangement_live(second.owner), second.id);
    IntegrationCheck(str8_match(rd_cfg_from_view(second_panel->last_tab->view)->string, str8_lit("text"), 0) &&
                     rd_view_address_match(second_panel->last_tab->prev->view, rd_view_address_from_cfg(c)));
    // A Floating Panel holds tabs, never splits.
    U64 split_gen = integration_gen();
    integration_move(other, b, second, Dir2_Right);
    UIShell_RegsScope(.window = other->id, .panel = second) { uishell_dispatch_panel_command(str8_lit("new_panel_right")); }
    IntegrationCheck(integration_gen() == split_gen && integration_in(b, second) && rd_arrangement_live(second.owner)->root->id == second.id);
    // A move out into the workspace reselects the Floating Panel it left.
    UIShell_RegsScope(.window = other->id, .tab = rd_view_address_from_cfg(c)) { uishell_dispatch_tab_command(str8_lit("focus_tab")); }
    integration_move(other, c, right, Dir2_Invalid);
    IntegrationCheck(integration_in(c, right) && rd_view_address_match(integration_tab(second, 0), rd_view_address_from_cfg(a)) && rd_view_is_selected(rd_view_address_from_cfg(a)));
    cfg_node_release(rd_state->cfg, other);
    rd_arrangements_release_unowned();
  }

  // A boundary drag lays out its resized copy of the arrangement every frame
  // and leaves the arrangement alone until the mouse is released, then sets
  // it once. A drag that ends without a release sets nothing.
  {
    integration_rect = r2f32p(0, 0, 640, 480);
    panels = integration_reset_panels(window, Axis2_X);
    RD_PanelAddress left = integration_panel(panels, 0.3f);
    RD_PanelAddress right = integration_panel(panels, 0.7f);
    integration_view(left, "terminal");
    integration_view(right, "terminal");
    Rng2F32 area = pad_2f32(integration_rect, -rd_window_edge_inset_px(ws));
    F32 total = dim_2f32(area).x;
    Vec2F32 grab = v2f32(area.x0+0.3f*total, center_2f32(area).y);
    for(S32 cancel = 1; cancel >= 0; cancel--)
    {
      F32 before = integration_panel_frame(ws, window, left, grab, UI_EventKind_Null);
      U64 before_gen = integration_gen();
      integration_panel_frame(ws, window, left, grab, UI_EventKind_Press);
      IntegrationCheck(rd_state->boundary_resize.arena != 0);
      F32 dragged = 0;
      for(U32 step = 1; step <= 4; step++)
      {
        integration_panel_frame(ws, window, left, v2f32(grab.x+10.f*step, grab.y), UI_EventKind_Null);
        dragged = integration_panel_frame(ws, window, left, v2f32(grab.x+10.f*step, grab.y), UI_EventKind_Null);
        IntegrationCheck(dragged > before+10.f*step-2.f && dragged < before+10.f*step+2.f);
        IntegrationCheck(integration_gen() == before_gen);
      }
      if(cancel)
      {
        // The action was killed: no release ever comes.
        ui_kill_action();
        integration_panel_frame(ws, window, left, grab, UI_EventKind_Null);
        F32 after = integration_panel_frame(ws, window, left, grab, UI_EventKind_Null);
        IntegrationCheck(rd_state->boundary_resize.arena == 0 && after == before);
        IntegrationCheck(integration_gen() == before_gen && integration_weight(left) == 0.3f);
        continue;
      }
      integration_panel_frame(ws, window, left, v2f32(grab.x+40.f, grab.y), UI_EventKind_Release);
      U64 saved_gen = integration_gen();
      IntegrationCheck(saved_gen != before_gen && rd_state->boundary_resize.arena == 0);
      F32 after = integration_panel_frame(ws, window, left, v2f32(grab.x+40.f, grab.y), UI_EventKind_Null);
      IntegrationCheck(after == dragged && integration_gen() == saved_gen);
      // Within the pixel the panel area's rect rounds to.
      F32 expected = 0.3f+40.f/total;
      F32 saved_left = integration_weight(left), saved_right = integration_weight(right);
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
      IntegrationCheck(integration_weight(left) == 0.5f && integration_weight(right) == 0.5f);
    }
  }

  //- The Renderer registry: docking's entries are its own, and View Spec
  // content routes to the Renderer that shows it.
  for(U64 i = 0; i < WH_RendererIndex_COUNT; i++)
  {
    WH_Renderer *renderer = wh_renderer_from_index(i);
    IntegrationCheck(renderer->registration == &rd_view_registrations[i] && str8_match(renderer->name, renderer->registration->name, 0));
    IntegrationCheck(wh_renderer_from_name(renderer->name) == renderer && renderer->ui != 0);
    IntegrationCheck(!!(renderer->content & WH_Content_Dashboard) == !!(renderer->registration->traits & RD_ViewTrait_Control));
  }
  IntegrationCheck(wh_renderer_from_name(str8_lit("no_such_view")) == &wh_nil_renderer);
  IntegrationCheck(str8_match(wh_renderer_from_content(WH_Content_Command)->name, str8_lit("terminal"), 0));
  IntegrationCheck(str8_match(wh_renderer_from_content(WH_Content_Facet)->name, str8_lit("terminal"), 0));
  IntegrationCheck(str8_match(wh_renderer_from_content(WH_Content_File)->name, str8_lit("text"), 0));
  IntegrationCheck(str8_match(wh_renderer_from_content(WH_Content_Jackstay)->name, str8_lit("jackstay"), 0));
  IntegrationCheck(str8_match(wh_renderer_from_content(WH_Content_Unshown)->name, str8_lit("placeholder"), 0));
  IntegrationCheck(wh_renderer_from_content(WH_Content_Url) == &wh_nil_renderer);
  // Headless, no Visualizer draws (as before the registry, when none was
  // registered outside a frame).
  IntegrationCheck(wh_visualizer_from_name(str8_lit("text")) == &wh_nil_renderer);

  //- A View in a workspace's arrangement is a Slot, with its spec and
  // resolution; a Floating Panel's View of the window's is not.
  {
    CFG_Node *workspace = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
    String8 id_text = str8_lit("0192f0c4-1c2d-7a3b-8c4d-5e6f70819203");
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, workspace, str8_lit("workspace_id")), id_text);
    CFG_Node *slotted = rd_fixture_view(rd_fixture_leaf(rd_fixture_owner(workspace)), str8_lit("terminal"), str8_lit("make test"), 1);
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, slotted, str8_lit("session")), str8_lit("s-1"));
    WH_Renderer *terminal = wh_renderer_from_name(str8_lit("terminal"));
    RD_ViewState *vs = rd_view_state_from_view(rd_view_address_from_cfg(slotted), 0);
    WH_ViewContext ctx = wh_view_context_from_view(scratch.arena, rd_view_address_from_cfg(slotted), terminal, rd_view_user_data(vs, terminal->state_size), r2f32p(0, 0, 64, 48));
    UIShell_WorkspaceId id = {0};
    IntegrationCheck(uishell_workspace_id_from_string(id_text, &id) && uishell_workspace_id_match(ctx.slot.workspace, id));
    IntegrationCheck(str8_match(ctx.slot.key, str8_lit("u:1"), 0) && ctx.status == WH_SlotStatus_Slot);
    IntegrationCheck(ctx.spec && ctx.spec->content == ANDAMENTO_SLOT_COMMAND && str8_match(ctx.spec->command, str8_lit("make test"), 0));
    IntegrationCheck(str8_match(ctx.resolution.session, str8_lit("s-1"), 0) && ctx.rect.x1 == 64);
    IntegrationCheck(wh_view_state(&ctx, UIShell_TerminalViewState) == vs->user_data);
    RD_PanelAddress floating_panel = integration_floating(window, rd_floating_count(window)+1);
    CFG_Node *floating_view = integration_view(floating_panel, "text");
    WH_ViewContext floating_ctx = wh_view_context_from_view(scratch.arena, rd_view_address_from_cfg(floating_view), wh_renderer_from_name(str8_lit("text")), 0, r2f32p(0, 0, 1, 1));
    IntegrationCheck(floating_ctx.status == 0 && floating_ctx.slot.key.size == 0 && floating_ctx.spec == 0);
    cfg_node_release(rd_state->cfg, workspace);
    rd_arrangements_release_unowned();
    RD_Arrangement *gone = rd_arrangement_from_panel_owner(scratch.arena, floating_panel.owner);
    rd_arrangement_discard(gone);
    rd_arrangement_set(gone);
  }

  //- Panels' Presentation State follows their arrangement as it is set
  // (rd_panel_presentation_arranged), and before a save what has no View or
  // panel goes, but only an owner whose arrangement is live is judged
  // (rd_presentation_sweep).
  {
    CFG_Node *workspace = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, workspace, str8_lit("workspace_id")), str8_lit("0192f0c4-1c2d-7a3b-8c4d-5e6f70819204"));
    RD_PanelOwner owner = rd_fixture_owner(workspace);
    RD_PanelAddress root = rd_fixture_leaf(owner);
    CFG_Node *view = rd_fixture_view(root, str8_lit("terminal"), str8_zero(), 1);
    String8 flag = str8_lit("tabs_on_bottom");
    rd_panel_flag_set(root, flag, 1);
    // A panel lifted out of the root with its tabs takes the root's record.
    RD_Arrangement *a = rd_arrangement_from_panel_owner(scratch.arena, owner);
    RD_PanelAddress added = rd_panel_address(owner, rd_arrangement_add(a, root.id, 0));
    RD_PanelAddress lifted = rd_panel_address(owner, a->root->first->id);
    rd_arrangement_set(a);
    IntegrationCheck(lifted.id != root.id && rd_panel_flag(lifted, flag));
    IntegrationCheck(!rd_panel_flag(root, flag) && !rd_panel_flag(added, flag));
    // A panel the arrangement is set without loses its record.
    rd_panel_flag_set(added, flag, 1);
    a = rd_arrangement_from_panel_owner(scratch.arena, owner);
    IntegrationCheck(rd_arrangement_remove(a, added.id));
    rd_arrangement_set(a);
    IntegrationCheck(rd_panel_presentation_from_panel(added) == &cfg_nil_node && rd_panel_flag(lifted, flag));
    // The Floating Panels after one that goes move up, and their records
    // with them.
    RD_PanelAddress first = integration_floating(workspace, 1);
    RD_PanelAddress second = integration_floating(workspace, 2);
    rd_panel_flag_set(first, flag, 1);
    rd_panel_flag_set(second, str8_lit("focused"), 1);
    RD_Arrangement *gone = rd_arrangement_from_panel_owner(scratch.arena, first.owner);
    rd_arrangement_discard(gone);
    rd_arrangement_set(gone);
    RD_PanelAddress moved = rd_panel_address(rd_panel_owner_from_host(RD_PanelOwnerKind_Floating, workspace, 1), second.id);
    IntegrationCheck(rd_floating_count(workspace) == 1 && rd_panel_flag(moved, str8_lit("focused")) && !rd_panel_flag(moved, flag));
    // The sweep: a panel's or a View's record with nothing goes; a live
    // panel's, Floating Panel's and View's stay.
    RD_ViewAddress live_view = rd_view_address_from_cfg(view);
    rd_view_setting_set(live_view, str8_lit("font_size"), str8_lit("10"));
    RD_PanelAddress orphan_panel = rd_panel_address(owner, 99);
    rd_panel_flag_set(orphan_panel, flag, 1);
    CFG_Node *views = cfg_node_child_from_string(workspace, RD_VIEW_PRESENTATION_ROOT);
    CFG_Node *orphan_view = cfg_node_new(rd_state->cfg, views, rd_view_owner_kind_name(RD_ViewOwnerKind_Workspace));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, orphan_view, str8_lit("key")), str8_lit("u:99"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, orphan_view, str8_lit("font_size")), str8_lit("20"));
    rd_presentation_sweep(workspace);
    IntegrationCheck(rd_panel_presentation_from_panel(orphan_panel) == &cfg_nil_node);
    IntegrationCheck(rd_panel_flag(lifted, flag) && rd_panel_flag(moved, str8_lit("focused")));
    IntegrationCheck(str8_match(rd_view_setting_from_view(live_view, str8_lit("font_size")), str8_lit("10"), 0));
    U64 view_records = 0;
    for(CFG_Node *c = views->first; c != &cfg_nil_node; c = c->next) { view_records += 1; }
    IntegrationCheck(view_records == 1);
    // A kept workspace (no live arrangement) keeps its records until it
    // reopens.
    CFG_Node *kept = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, kept, str8_lit("workspace_id")), str8_lit("0192f0c4-1c2d-7a3b-8c4d-5e6f70819205"));
    RD_PanelAddress kept_panel = rd_panel_address(rd_fixture_owner(kept), 5);
    rd_panel_flag_set(kept_panel, flag, 1);
    CFG_Node *kept_views = cfg_node_child_from_string_or_alloc(rd_state->cfg, kept, RD_VIEW_PRESENTATION_ROOT);
    CFG_Node *kept_view = cfg_node_new(rd_state->cfg, kept_views, rd_view_owner_kind_name(RD_ViewOwnerKind_Workspace));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, kept_view, str8_lit("key")), str8_lit("u:5"));
    IntegrationCheck(rd_arrangement_version(kept_panel.owner) == 0 && rd_panel_flag(kept_panel, flag));
    rd_presentation_sweep(kept);
    IntegrationCheck(rd_panel_flag(kept_panel, flag) && kept_views->first == kept_view);
    cfg_node_release(rd_state->cfg, kept);
    gone = rd_arrangement_from_panel_owner(scratch.arena, moved.owner);
    rd_arrangement_discard(gone);
    rd_arrangement_set(gone);
    cfg_node_release(rd_state->cfg, workspace);
    rd_arrangements_release_unowned();
  }

  scratch_end(scratch);
  fprintf(stderr, "Docking integration: %u failures\n", failures);
  abort_self(failures ? 1 : 0);
}
