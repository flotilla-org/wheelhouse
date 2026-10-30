// The overview (#450, uishell_overview.c) on the sidebar fixture window: a
// workspace at rest draws as itself; a switch is a flight drawn through the
// scene that settles on the new workspace, or a cut without motion; the
// toggle opens the overview; clicking a heading fits its group and Escape
// backs out a level at a time; clicking a tile picks its workspace. Only
// the workspaces the region shows build: the current one, and through the
// scene those the camera sees. Runs after the workspace lifecycle
// diagnostics, on the overview's own clock.

#define OverviewCheck(cond, msg) do { if(!(cond)) { ok = 0; fprintf(stderr, "FAIL overview: %s\n", msg); } } while(0)

// The overview after `ms` more on its clock, with `current` the window's
// workspace. Free it with uishell_overview_end.
internal UIShell_Overview
uishell_overview_diagnostic_step_motion(RD_WindowState *ws, F64 ms, CFG_Node *current, Rng2F32 region, B32 motion)
{
  ws->sidebar->overview_clock_frame = rd_state->frame_index;
  ws->sidebar->overview_clock_ms += ms;
  return uishell_overview_begin(ws, current, region, motion);
}

internal UIShell_Overview
uishell_overview_diagnostic_step(RD_WindowState *ws, F64 ms, CFG_Node *current, Rng2F32 region)
{
  return uishell_overview_diagnostic_step_motion(ws, ms, current, region, 1);
}

// Builds the overview in a test UI state for three frames, clicking the box
// keyed `target` on the second and third (press, release). Returns whether
// the box was there to click.
internal B32
uishell_overview_diagnostic_click(RD_WindowState *ws, CFG_Node *window, CFG_Node *current, Rng2F32 region, Arena *arena, String8 target)
{
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  ui_select_state(test);
  Vec2F32 at = {0};
  B32 found = 0;
  for(U32 frame = 0; frame < 3; frame++)
  {
    UIShell_Overview o = uishell_overview_diagnostic_step(ws, 0, current, region);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(arena, window);
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0}; UI_EventList events = {0}; UI_EventNode event = {0};
    if(frame > 0 && found)
    {
      event.v = (UI_Event){.key = WM_Key_LeftMouseButton, .kind = frame == 1 ? UI_EventKind_Press : UI_EventKind_Release, .pos = at};
      events.first = events.last = &event; events.count = 1;
    }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = at;
    UIShell_RegsScope(.window = window->id)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_overview_ui(ws, &o, &split, current); }
    ui_end_build();
    uishell_overview_end(&o);
    if(frame == 0)
    {
      at = uishell_workspace_lifecycle_center(test, target);
      found = at.x != 0 || at.y != 0;
    }
  }
  ui_select_state(saved_ui); ui_state_release(test);
  return found;
}

internal B32
uishell_overview_diagnostics(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  B32 ok = 1;
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  UIShell_SidebarState *state = ws != &rd_nil_window_state ? uishell_sidebar_init(ws) : 0;
  OverviewCheck(state && state->core, "sidebar fixture core");
  if(!ok) { scratch_end(scratch); return 0; }
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  for(U64 i = split.inventory.count; i < 3; i++)
  {
    uishell_new_workspace(window);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
  }
  uishell_sidebar_observe(state, &split);
  uishell_sidebar_refresh(state);
  CFG_Node *a = split.inventory.first ? cfg_node_from_id(split.inventory.first->id) : &cfg_nil_node;
  OverviewCheck(a != &cfg_nil_node && split.inventory.count >= 3, "three open workspaces");
  if(!ok) { scratch_end(scratch); return 0; }
  Rng2F32 region = r2f32p(320, 0, 1600, 800);
  B32 saved_toggle = ws->workspace_zoom_open;
  ws->workspace_zoom_open = 0;
  UIShell_Overview o = {0};

  //- At rest on a workspace, it draws as itself, and only it builds. Of the
  // others, b's tile is nearest a's and c's farthest.
  CFG_Node *b = &cfg_nil_node, *c = &cfg_nil_node;
  o = uishell_overview_diagnostic_step(ws, 0, a, region); uishell_overview_end(&o);
  o = uishell_overview_diagnostic_step(ws, 1000, a, region);
  OverviewCheck(o.frame && o.frame->tile_count >= 3, "every open workspace has a tile");
  OverviewCheck(!o.through_scene && o.frame && o.frame->settled, "a workspace at rest draws as itself");
  if(o.frame)
  {
    const WheelhouseOverviewTile *home = 0;
    for(U64 i = 0; i < o.frame->tile_count; i++) { if(o.frame->tiles[i].current) { home = &o.frame->tiles[i]; } }
    F64 nearest = inf32(), farthest = -1;
    for(U64 i = 0; home != 0 && i < o.frame->tile_count; i++)
    {
      const WheelhouseOverviewTile *tile = &o.frame->tiles[i];
      if(tile == home) { continue; }
      F64 dx = (tile->rect.x0 + tile->rect.x1 - home->rect.x0 - home->rect.x1)/2;
      F64 dy = (tile->rect.y0 + tile->rect.y1 - home->rect.y0 - home->rect.y1)/2;
      F64 d = dx*dx + dy*dy;
      CFG_Node *node = cfg_node_from_id(uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_core(tile->workspace_id)));
      if(d < nearest)  { nearest = d; b = node; }
      if(d > farthest) { farthest = d; c = node; }
    }
  }
  OverviewCheck(b != &cfg_nil_node && c != &cfg_nil_node && b != c, "tiles near and far from the current one");
  if(!ok) { uishell_overview_end(&o); scratch_end(scratch); return 0; }
  OverviewCheck(uishell_overview_shows(&o, a, a->id) && !uishell_overview_shows(&o, a, b->id) && !uishell_overview_shows(&o, a, c->id),
                "at rest only the current workspace builds");
  uishell_overview_end(&o);

  //- A switch flies through the scene and settles on the new workspace.
  // Leaving, the camera sees a and b but not yet c, so c doesn't build.
  o = uishell_overview_diagnostic_step(ws, 1, b, region);
  OverviewCheck(o.through_scene && uishell_overview_shows(&o, b, a->id) && uishell_overview_shows(&o, b, b->id),
                "a switch builds the workspaces it flies between");
  OverviewCheck(!uishell_overview_shows(&o, b, c->id), "a switch doesn't build a workspace out of view");
  uishell_overview_end(&o);
  o = uishell_overview_diagnostic_step(ws, 16, b, region);
  OverviewCheck(o.through_scene && o.frame->flying, "a switch is a flight through the scene");
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_WORKSPACE && !ws->workspace_zoom_open, "a switch leaves the overview closed");
  uishell_overview_end(&o);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(!o.through_scene && o.frame->settled, "a switch settles on the new workspace");
  uishell_overview_end(&o);

  //- The toggle opens the overview with a flight.
  ws->workspace_zoom_open = 1;
  o = uishell_overview_diagnostic_step(ws, 16, b, region);
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_OVERVIEW && o.frame->flying && o.through_scene, "the toggle flies out to the overview");
  uishell_overview_end(&o);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_OVERVIEW && !o.frame->flying && o.through_scene && ws->workspace_zoom_open,
                "the overview stays open at rest");
  OverviewCheck(uishell_overview_shows(&o, b, a->id) && uishell_overview_shows(&o, b, c->id), "the overview builds every tile it shows");
  uishell_overview_end(&o);

  //- A heading fits its group; Escape backs out a level at a time.
  OverviewCheck(uishell_overview_diagnostic_click(ws, window, b, region, scratch.arena, str8_lit("###overview_heading_0")), "a group heading");
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_GROUP && o.frame->group == 0 && ws->workspace_zoom_open, "clicking a heading fits its group");
  uishell_overview_end(&o);
  uishell_overview_back(ws);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_OVERVIEW && ws->workspace_zoom_open, "Escape from a group shows the overview");
  uishell_overview_end(&o);
  uishell_overview_back(ws);
  OverviewCheck(!ws->workspace_zoom_open, "Escape from the overview closes it");
  o = uishell_overview_diagnostic_step(ws, 16, b, region);
  OverviewCheck(o.through_scene && o.frame->flying, "closing flies back into the workspace");
  uishell_overview_end(&o);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(!o.through_scene, "closing settles on the workspace");
  uishell_overview_end(&o);

  //- Clicking another workspace's tile picks it: the overview closes with
  // a flight and the window switches.
  ws->workspace_zoom_open = 1;
  o = uishell_overview_diagnostic_step(ws, 16, b, region); uishell_overview_end(&o);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region); uishell_overview_end(&o);
  U64 queued = uishell_workspace_lifecycle_queued(str8_lit("select_workspace"), a->id);
  OverviewCheck(uishell_overview_diagnostic_click(ws, window, b, region, scratch.arena, push_str8f(scratch.arena, "###overview_tile_%I64u", a->id)),
                "the other workspace's tile");
  OverviewCheck(uishell_workspace_lifecycle_queued(str8_lit("select_workspace"), a->id) == queued + 1, "a picked tile selects its workspace");
  OverviewCheck(!ws->workspace_zoom_open, "picking a tile closes the overview");
  o = uishell_overview_diagnostic_step(ws, 16, a, region);
  OverviewCheck(o.through_scene && o.frame->flying, "a pick flies into its tile");
  uishell_overview_end(&o);
  o = uishell_overview_diagnostic_step(ws, 1000, a, region);
  OverviewCheck(!o.through_scene && o.frame->settled, "a pick settles on its workspace");
  uishell_overview_end(&o);

  //- Without motion (the setting, or the system's reduce motion) a switch
  // cuts.
  o = uishell_overview_diagnostic_step_motion(ws, 16, b, region, 0);
  OverviewCheck(!o.through_scene && o.frame->settled, "without motion a switch cuts");
  uishell_overview_end(&o);

  o = uishell_overview_diagnostic_step(ws, 1000, b, region); uishell_overview_end(&o);
  ws->workspace_zoom_open = saved_toggle;
  fprintf(stderr, "Overview diagnostics: %s\n", ok ? "passed" : "FAILED");
  scratch_end(scratch);
  return ok;
}
