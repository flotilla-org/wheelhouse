// The overview (#450, uishell_overview.c) on the sidebar fixture window: a
// workspace at rest draws as itself; a switch is a flight drawn through the
// scene that settles on the new workspace, or a cut without motion; the
// toggle opens the overview; clicking a heading fits its group, and Escape,
// the back button or a click outside the group backs out, a level at a
// time; clicking a tile picks its workspace. Only the workspaces the region
// shows build: the current one, and through the scene those the camera
// sees. A local group is a row of its own under its name, and scrolling
// over or dragging a row too long for the region slides it. A switch lays
// the overview out as rows or a grid. Runs after the workspace lifecycle
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
// keyed `target` on the second and third (press, release), or, given a
// `scroll`, scrolling over it on the second. Returns whether the box was
// there.
internal B32
uishell_overview_diagnostic_input(RD_WindowState *ws, CFG_Node *window, CFG_Node *current, Rng2F32 region, Arena *arena, String8 target, Vec2F32 scroll)
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
    B32 scrolls = (scroll.x != 0 || scroll.y != 0);
    if(frame > 0 && found && !(scrolls && frame == 2))
    {
      event.v = (scrolls ? (UI_Event){.kind = UI_EventKind_Scroll, .pos = at, .delta_2f32 = scroll, .scroll_is_precise = 1}
                         : (UI_Event){.key = WM_Key_LeftMouseButton, .kind = frame == 1 ? UI_EventKind_Press : UI_EventKind_Release, .pos = at});
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

// Whether every tile label the last build drew reads its workspace's
// title, as the overview has it now.
internal B32
uishell_overview_diagnostic_labels_read(RD_WindowState *ws, CFG_Node *current, Rng2F32 region)
{
  B32 ok = 1;
  UIShell_Overview o = uishell_overview_diagnostic_step(ws, 0, current, region);
  for(U64 i = 0; o.frame != 0 && i < o.frame->tile_count; i++)
  {
    const WheelhouseOverviewTile *tile = &o.frame->tiles[i];
    CFG_ID cfg_id = uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_core(tile->workspace_id));
    UI_Box *label = ui_box_from_key(ui_key_from_stringf(ui_key_zero(), "###overview_tile_label_%I64u", cfg_id));
    if(!ui_box_is_nil(label) && label->last_touched_build_index + 1 == ui_state->build_index &&
       !str8_match(label->string, str8((U8 *)tile->title.data, tile->title.len), 0))
    { ok = 0; }
  }
  uishell_overview_end(&o);
  return ok;
}

// Builds the overview in a test UI state, a frame every 16ms of its
// clock: finds the box keyed `target` (or, with none, takes `point`),
// presses it, moves the mouse `by`,
// holds it there `hold_ms` and releases, then builds `after` frames more.
// Returns whether the box was there; `labels_read` whether every label
// drawn while dragging read its workspace's title.
internal B32
uishell_overview_diagnostic_drag(RD_WindowState *ws, CFG_Node *window, CFG_Node *current, Rng2F32 region, Arena *arena, String8 target, Vec2F32 by,
                                 F64 hold_ms, U32 after, B32 *labels_read, Vec2F32 point)
{
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  ui_select_state(test);
  Vec2F32 at = {0};
  B32 found = 0;
  *labels_read = 1;
  for(U32 frame = 0; frame < 4 + after; frame++)
  {
    UIShell_Overview o = uishell_overview_diagnostic_step(ws, frame == 0 ? 0 : frame == 3 ? 16 + hold_ms : 16, current, region);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(arena, window);
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0}; UI_EventList events = {0}; UI_EventNode event = {0};
    Vec2F32 mouse = frame >= 2 ? add_2f32(at, by) : at;
    if(found && (frame == 1 || frame == 3))
    {
      event.v = (UI_Event){.key = WM_Key_LeftMouseButton, .kind = frame == 1 ? UI_EventKind_Press : UI_EventKind_Release, .pos = mouse};
      events.first = events.last = &event; events.count = 1;
    }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = mouse;
    UIShell_RegsScope(.window = window->id)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_overview_ui(ws, &o, &split, current); }
    ui_end_build();
    uishell_overview_end(&o);
    if(frame == 0)
    {
      at = target.size != 0 ? uishell_workspace_lifecycle_center(test, target) : point;
      found = at.x != 0 || at.y != 0;
    }
    if(frame == 2 && !uishell_overview_diagnostic_labels_read(ws, current, region)) { *labels_read = 0; }
  }
  ui_select_state(saved_ui); ui_state_release(test);
  return found;
}

// A place in the region (absolute) that is bare background, inside the
// fitted group's bounds or outside them as `inside` says, or zero: no tile,
// label, heading or control there.
internal Vec2F32
uishell_overview_diagnostic_bare(UIShell_Overview *o, B32 inside)
{
  WheelhouseOverviewFrame *f = o->frame;
  Vec2F32 dim = dim_2f32(o->region);
  Rng2F32 group = uishell_overview_drawn_rect(o, f->groups[f->group].bounds, f->group);
  for(F32 y = 4; y < dim.y; y += 6)
  {
    for(F32 x = 4; x < dim.x*0.6f; x += 6)
    {
      Vec2F32 at = v2f32(x, y);
      B32 bare = (contains_2f32(group, at) == !!inside);
      for(U64 i = 0; bare && i < f->tile_count; i++)
      {
        Rng2F32 tile = uishell_overview_drawn_rect(o, f->tiles[i].rect, f->tiles[i].group);
        Rng2F32 label = uishell_overview_drawn_rect(o, f->tiles[i].label, f->tiles[i].group);
        if(contains_2f32(pad_2f32(tile, 4), at) || contains_2f32(pad_2f32(label, 4), at)) { bare = 0; }
      }
      for(U64 i = 0; bare && i < f->group_count; i++)
      {
        if(contains_2f32(pad_2f32(uishell_overview_drawn_rect(o, f->groups[i].heading_rect, max_U64), 4), at)) { bare = 0; }
      }
      if(bare) { return add_2f32(at, o->region.p0); }
    }
  }
  return v2f32(0, 0);
}

internal B32
uishell_overview_diagnostic_click(RD_WindowState *ws, CFG_Node *window, CFG_Node *current, Rng2F32 region, Arena *arena, String8 target)
{
  return uishell_overview_diagnostic_input(ws, window, current, region, arena, target, v2f32(0, 0));
}

// The group headed `heading` in the overview's frame, or -1.
internal S64
uishell_overview_diagnostic_group(UIShell_Overview *o, String8 heading)
{
  for(U64 i = 0; o->frame && i < o->frame->group_count; i++)
  {
    WheelhouseText text = o->frame->groups[i].heading;
    if(str8_match(str8((U8 *)text.data, text.len), heading, 0)) { return (S64)i; }
  }
  return -1;
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
  Vec2F32 bare_inside = uishell_overview_diagnostic_bare(&o, 1), bare_outside = uishell_overview_diagnostic_bare(&o, 0);
  uishell_overview_end(&o);
  //- Out of a fitted group: its back button, or a click outside it, shows
  // the whole overview; a click on bare background inside it doesn't.
  // A click is a drag that doesn't move; these don't check its labels.
  B32 unused_labels = 0;
  OverviewCheck(bare_inside.x != 0 && bare_outside.x != 0, "bare background inside and outside a fitted group");
  uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, str8_zero(), v2f32(0, 0), 0, 0, &unused_labels, bare_inside);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_GROUP, "a click inside a fitted group keeps it fitted");
  uishell_overview_end(&o);
  uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, str8_zero(), v2f32(0, 0), 0, 0, &unused_labels, bare_outside);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_OVERVIEW && ws->workspace_zoom_open, "a click outside a fitted group shows the overview");
  uishell_overview_end(&o);
  OverviewCheck(!uishell_overview_diagnostic_click(ws, window, b, region, scratch.arena, str8_lit("###overview_back")),
                "no back button without a fitted group");
  OverviewCheck(uishell_overview_diagnostic_click(ws, window, b, region, scratch.arena, str8_lit("###overview_heading_0")), "a group heading, again");
  o = uishell_overview_diagnostic_step(ws, 1000, b, region); uishell_overview_end(&o);
  OverviewCheck(uishell_overview_diagnostic_click(ws, window, b, region, scratch.arena, str8_lit("###overview_back")), "a fitted group's back button");
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_OVERVIEW && ws->workspace_zoom_open, "a fitted group's back button shows the overview");
  uishell_overview_end(&o);
  OverviewCheck(uishell_overview_diagnostic_click(ws, window, b, region, scratch.arena, str8_lit("###overview_heading_0")), "a group heading, to Escape from");
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
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

  //- A local group is a row of its own, headed by its name, beside the
  // default group's in the same section.
  String8 home_section = push_str8_copy(scratch.arena, uishell_local_groups_section_of(window, uishell_sidebar_default_local_id));
  UIShell_LocalGroup *made_group = uishell_sidebar_local_add_group(window, home_section, str8_lit("Overview row"));
  String8 made_group_id = push_str8_copy(scratch.arena, made_group->id);
  uishell_sidebar_local_new_workspace(window, uishell_sidebar_local_group(window, made_group_id));
  //- ...and enough workspaces in the default group to overflow its row.
  for(U64 i = 0; i < 6; i++) { uishell_new_workspace(window); }
  uishell_local_groups_publish(state, window, scratch.arena);
  split = uishell_root_controlled_split_from_window(scratch.arena, window);
  uishell_sidebar_observe(state, &split);
  uishell_sidebar_refresh(state);
  ws->workspace_zoom_open = 1;
  o = uishell_overview_diagnostic_step(ws, 16, b, region); uishell_overview_end(&o);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  S64 made_row = uishell_overview_diagnostic_group(&o, str8_lit("Overview row"));
  OverviewCheck(made_row >= 0 && o.frame->group_count >= 2, "a local group is a row of its own, headed by its name");
  S64 long_row = -1;
  for(U64 i = 0; i < o.frame->group_count; i++) { if(o.frame->groups[i].slide_min < 0) { long_row = (S64)i; } }
  OverviewCheck(long_row >= 0, "a row too long for the region can slide");
  CFG_ID long_tile = 0;
  for(U64 i = 0; long_row >= 0 && i < o.frame->tile_count; i++)
  {
    if(o.frame->tiles[i].group == (U64)long_row && long_tile == 0)
    { long_tile = uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_core(o.frame->tiles[i].workspace_id)); }
  }
  uishell_overview_end(&o);
  if(long_tile != 0)
  {
    OverviewCheck(uishell_overview_diagnostic_input(ws, window, b, region, scratch.arena,
                                                    push_str8f(scratch.arena, "###overview_tile_%I64u", long_tile), v2f32(0, 200)),
                  "a tile in the long row");
    o = uishell_overview_diagnostic_step(ws, 0, b, region);
    OverviewCheck(o.frame->groups[long_row].slide < 0, "scrolling over a long row slides it");
    //- Dragging a tile of the row slides it back, and releasing over the
    // tile doesn't pick it.
    F64 before = o.frame->groups[long_row].slide;
    CFG_ID shown_tile = 0;
    Vec2F32 dim = dim_2f32(region);
    for(U64 i = 0; i < o.frame->tile_count; i++)
    {
      Vec2F32 at = center_2f32(uishell_overview_rect(&o, o.frame->tiles[i].rect));
      if(o.frame->tiles[i].group == (U64)long_row && !o.frame->tiles[i].current && at.x > 0 && at.x < dim.x && shown_tile == 0)
      { shown_tile = uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_core(o.frame->tiles[i].workspace_id)); }
    }
    uishell_overview_end(&o);
    U64 picks = uishell_workspace_lifecycle_queued(str8_lit("select_workspace"), shown_tile);
    String8 shown_key = push_str8f(scratch.arena, "###overview_tile_%I64u", shown_tile);
    B32 labels_read = 0;
    OverviewCheck(uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, shown_key, v2f32(40, 0), 200, 0, &labels_read, v2f32(0, 0)),
                  "a tile of the long row in view");
    OverviewCheck(labels_read, "the labels drawn while a row slides read their workspaces' titles");
    o = uishell_overview_diagnostic_step(ws, 0, b, region);
    F64 released = o.frame->groups[long_row].slide;
    OverviewCheck(released > before, "dragging a long row slides it with the mouse");
    OverviewCheck(uishell_workspace_lifecycle_queued(str8_lit("select_workspace"), shown_tile) == picks &&
                  o.frame->mode == WHEELHOUSE_OVERVIEW_OVERVIEW, "a drag doesn't pick the tile it started on");
    uishell_overview_end(&o);
    //- A row let go of while moving glides on; one held still first stays
    // where it was let go.
    OverviewCheck(uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, shown_key, v2f32(40, 0), 0, 0, &labels_read, v2f32(0, 0)),
                  "a tile of the long row to drag on");
    o = uishell_overview_diagnostic_step(ws, 0, b, region);
    released = o.frame->groups[long_row].slide;
    uishell_overview_end(&o);
    uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, str8_lit("###overview_none"), v2f32(0, 0), 0, 8, &labels_read, v2f32(0, 0));
    o = uishell_overview_diagnostic_step(ws, 0, b, region);
    OverviewCheck(o.frame->groups[long_row].slide > released, "a row let go of while moving glides on");
    uishell_overview_end(&o);
    OverviewCheck(uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, shown_key, v2f32(-30, 0), 200, 0, &labels_read, v2f32(0, 0)),
                  "a tile of the long row to drag and hold");
    o = uishell_overview_diagnostic_step(ws, 0, b, region);
    released = o.frame->groups[long_row].slide;
    uishell_overview_end(&o);
    uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, str8_lit("###overview_none"), v2f32(0, 0), 0, 8, &labels_read, v2f32(0, 0));
    o = uishell_overview_diagnostic_step(ws, 0, b, region);
    OverviewCheck(o.frame->groups[long_row].slide == released, "a row held still before it's let go doesn't glide");
    uishell_overview_end(&o);
    //- Dragged past its end, a row stretches, less than the mouse moved, and
    // springs back when let go of.
    OverviewCheck(uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, shown_key, v2f32(2000, 0), 200, 0, &labels_read, v2f32(0, 0)),
                  "a tile of the long row to drag past its end");
    o = uishell_overview_diagnostic_step(ws, 0, b, region);
    OverviewCheck(o.frame->groups[long_row].slide == 0, "a row dragged past its end stops there");
    OverviewCheck(o.over.x > 1 && o.over.x < 1000 && o.over_group == (U64)long_row && o.over.y == 0,
                  "a row dragged past its end stretches, less than the mouse moved");
    for(U64 i = 0; i < o.frame->tile_count; i++)
    {
      if(o.frame->tiles[i].group != (U64)long_row) { continue; }
      Rng2F32 plain = uishell_overview_rect(&o, o.frame->tiles[i].rect);
      Rng2F32 drawn = uishell_overview_drawn_rect(&o, o.frame->tiles[i].rect, o.frame->tiles[i].group);
      OverviewCheck(drawn.x0 - plain.x0 == o.over.x && drawn.y0 == plain.y0, "a stretched row's tiles are drawn stretched");
      break;
    }
    uishell_overview_end(&o);
    uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, str8_lit("###overview_none"), v2f32(0, 0), 0, 60, &labels_read, v2f32(0, 0));
    o = uishell_overview_diagnostic_step(ws, 0, b, region);
    OverviewCheck(o.over.x == 0 && o.frame->groups[long_row].slide == 0, "a stretched row springs back to its end");
    uishell_overview_end(&o);
    //- A row gliding into its end carries past it, then springs back.
    OverviewCheck(uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, shown_key, v2f32(-60, 0), 200, 0, &labels_read, v2f32(0, 0)),
                  "a tile of the long row to drag from its end");
    OverviewCheck(uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, shown_key, v2f32(30, 0), 0, 0, &labels_read, v2f32(0, 0)),
                  "a tile of the long row to flick at its end");
    F32 carried = 0;
    for(U32 i = 0; i < 12; i++)
    {
      uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, str8_lit("###overview_none"), v2f32(0, 0), 0, 0, &labels_read, v2f32(0, 0));
      o = uishell_overview_diagnostic_step(ws, 0, b, region);
      carried = Max(carried, o.over.x);
      uishell_overview_end(&o);
    }
    o = uishell_overview_diagnostic_step(ws, 0, b, region);
    OverviewCheck(carried > 1, "a row gliding into its end carries past it");
    OverviewCheck(o.over.x == 0 && o.frame->groups[long_row].slide == 0, "a row carried past its end springs back to it");
    uishell_overview_end(&o);
    //- In a region too short for the overview, dragging up or down pans it,
    // over a tile or between rows, and what's let go of while moving glides
    // on. A drag goes the way there's room: down shows what is above.
    Rng2F32 short_region = r2f32p(region.x0, region.y0, region.x1, region.y0 + 50);
    o = uishell_overview_diagnostic_step(ws, 0, b, short_region); uishell_overview_end(&o);
    o = uishell_overview_diagnostic_step(ws, 1000, b, short_region);
    OverviewCheck(uishell_overview_pans(o.frame), "an overview taller than its region pans");
    // A third of the room there is on the roomier side, in pixels.
    F64 px_units = uishell_overview_units(&o, Axis2_Y);
    F64 room_above = (o.frame->view.y0 - o.frame->bounds.y0)/px_units, room_below = (o.frame->bounds.y1 - o.frame->view.y1)/px_units;
    F32 drag_y = (F32)(room_above > room_below ? room_above/3 : -room_below/3);
    OverviewCheck(abs_f32(drag_y) >= 6, "room to pan the short region");
    F64 cy = (o.frame->view.y0 + o.frame->view.y1)/2;
    CFG_ID pan_tile = 0;
    Vec2F32 between = {0};
    Vec2F32 short_dim = dim_2f32(short_region);
    for(U64 i = 0; i < o.frame->tile_count; i++)
    {
      Rng2F32 rect = uishell_overview_rect(&o, o.frame->tiles[i].rect);
      if(!o.frame->tiles[i].current && rect.x0 > 0 && rect.x1 < short_dim.x && rect.y0 > 0 && rect.y1 < short_dim.y && pan_tile == 0)
      { pan_tile = uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_core(o.frame->tiles[i].workspace_id)); }
    }
    for(U64 i = 0; i < o.frame->group_count; i++)
    {
      // The right end of a short row's heading line: no tile, heading,
      // wash or long row to take the press.
      Rng2F32 rect = uishell_overview_rect(&o, o.frame->groups[i].heading_rect);
      Vec2F32 at = v2f32(short_dim.x - 4, (rect.y0 + rect.y1)/2);
      if(o.frame->groups[i].slide_min >= 0 && at.y > 0 && at.y < short_dim.y && between.x == 0) { between = add_2f32(at, short_region.p0); }
    }
    uishell_overview_end(&o);
    U64 pan_picks = uishell_workspace_lifecycle_queued(str8_lit("select_workspace"), pan_tile);
    OverviewCheck(uishell_overview_diagnostic_drag(ws, window, b, short_region, scratch.arena,
                                                   push_str8f(scratch.arena, "###overview_tile_%I64u", pan_tile), v2f32(0, drag_y), 200, 0,
                                                   &labels_read, v2f32(0, 0)),
                  "a tile in the short region");
    o = uishell_overview_diagnostic_step(ws, 0, b, short_region);
    F64 dragged_cy = (o.frame->view.y0 + o.frame->view.y1)/2;
    OverviewCheck(drag_y > 0 ? dragged_cy < cy : dragged_cy > cy, "dragging a tile up or down pans the overview");
    OverviewCheck(uishell_workspace_lifecycle_queued(str8_lit("select_workspace"), pan_tile) == pan_picks &&
                  o.frame->mode == WHEELHOUSE_OVERVIEW_OVERVIEW, "a pan doesn't pick the tile it started on");
    uishell_overview_end(&o);
    OverviewCheck(between.x != 0, "a place between rows in the short region");
    uishell_overview_diagnostic_drag(ws, window, b, short_region, scratch.arena, str8_zero(), v2f32(0, drag_y), 0, 0, &labels_read, between);
    o = uishell_overview_diagnostic_step(ws, 0, b, short_region);
    F64 released_cy = (o.frame->view.y0 + o.frame->view.y1)/2;
    OverviewCheck(drag_y > 0 ? released_cy < dragged_cy : released_cy > dragged_cy, "dragging between rows pans the overview");
    uishell_overview_end(&o);
    uishell_overview_diagnostic_drag(ws, window, b, short_region, scratch.arena, str8_lit("###overview_none"), v2f32(0, 0), 0, 8, &labels_read, v2f32(0, 0));
    o = uishell_overview_diagnostic_step(ws, 0, b, short_region);
    F64 glided_cy = (o.frame->view.y0 + o.frame->view.y1)/2;
    OverviewCheck(drag_y > 0 ? glided_cy < released_cy : glided_cy > released_cy, "a pan let go of while moving glides on");
    uishell_overview_end(&o);
    //- Dragged past its top or bottom, the overview stretches and springs
    // back.
    uishell_overview_diagnostic_drag(ws, window, b, short_region, scratch.arena, str8_zero(), v2f32(0, drag_y*20), 200, 0, &labels_read, between);
    o = uishell_overview_diagnostic_step(ws, 0, b, short_region);
    OverviewCheck(drag_y > 0 ? o.over.y > 1 : o.over.y < -1, "an overview dragged past its end stretches");
    uishell_overview_end(&o);
    uishell_overview_diagnostic_drag(ws, window, b, short_region, scratch.arena, str8_lit("###overview_none"), v2f32(0, 0), 0, 60, &labels_read, v2f32(0, 0));
    o = uishell_overview_diagnostic_step(ws, 0, b, short_region);
    OverviewCheck(o.over.y == 0, "a stretched overview springs back");
    uishell_overview_end(&o);
    o = uishell_overview_diagnostic_step(ws, 0, b, region); uishell_overview_end(&o);
    o = uishell_overview_diagnostic_step(ws, 1000, b, region); uishell_overview_end(&o);
    //- A click on a tile of a long row still picks it, even if the mouse
    // moves a little, short of a drag: the row under it doesn't take the
    // press.
    OverviewCheck(uishell_overview_diagnostic_drag(ws, window, b, region, scratch.arena, shown_key, v2f32(3, 0), 0, 0, &labels_read, v2f32(0, 0)),
                  "a tile of the long row to click");
    OverviewCheck(uishell_workspace_lifecycle_queued(str8_lit("select_workspace"), shown_tile) == picks + 1,
                  "a click on a tile of a long row picks it, though the mouse moved a little");
  }
  //- The view mode switch: Grid lays the overview out in blocks and says so
  // in the user's setting, and Rows goes back; the setting drives the mode
  // too. In a grid, a heading still fits its group.
  ws->workspace_zoom_open = 1;
  o = uishell_overview_diagnostic_step(ws, 16, b, region); uishell_overview_end(&o);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_OVERVIEW && o.frame->view_mode == WHEELHOUSE_OVERVIEW_ROWS, "the overview opens in rows");
  uishell_overview_end(&o);
  OverviewCheck(uishell_overview_diagnostic_click(ws, window, b, region, scratch.arena, str8_lit("###overview_view_mode_1")), "the overview's Grid switch");
  o = uishell_overview_diagnostic_step(ws, 0, b, region);
  OverviewCheck(o.frame->view_mode == WHEELHOUSE_OVERVIEW_GRID && o.frame->mode == WHEELHOUSE_OVERVIEW_OVERVIEW,
                "clicking Grid lays the overview out as a grid, and only that");
  OverviewCheck(uishell_overview_view_mode_setting() == WHEELHOUSE_OVERVIEW_GRID, "clicking Grid sets the user's setting");
  B32 grid_slides = 0;
  for(U64 i = 0; i < o.frame->group_count; i++) { if(uishell_overview_slides(o.frame, i)) { grid_slides = 1; } }
  OverviewCheck(!grid_slides, "a grid has no rows to slide");
  uishell_overview_end(&o);
  OverviewCheck(uishell_overview_diagnostic_click(ws, window, b, region, scratch.arena, str8_lit("###overview_heading_0")), "a group heading in the grid");
  o = uishell_overview_diagnostic_step(ws, 1000, b, region);
  OverviewCheck(o.frame->mode == WHEELHOUSE_OVERVIEW_GROUP && o.frame->group == 0, "in a grid, clicking a heading fits its group");
  uishell_overview_end(&o);
  uishell_overview_back(ws);
  o = uishell_overview_diagnostic_step(ws, 1000, b, region); uishell_overview_end(&o);
  OverviewCheck(uishell_overview_diagnostic_click(ws, window, b, region, scratch.arena, str8_lit("###overview_view_mode_0")), "the overview's Rows switch");
  o = uishell_overview_diagnostic_step(ws, 0, b, region);
  OverviewCheck(o.frame->view_mode == WHEELHOUSE_OVERVIEW_ROWS && uishell_overview_view_mode_setting() == WHEELHOUSE_OVERVIEW_ROWS,
                "clicking Rows goes back to rows, and sets the setting");
  uishell_overview_end(&o);
  uishell_overview_set_view_mode_setting(WHEELHOUSE_OVERVIEW_GRID);
  o = uishell_overview_diagnostic_step(ws, 0, b, region);
  OverviewCheck(o.frame->view_mode == WHEELHOUSE_OVERVIEW_GRID, "the overview follows the user's setting");
  uishell_overview_end(&o);
  uishell_overview_set_view_mode_setting(WHEELHOUSE_OVERVIEW_ROWS);
  o = uishell_overview_diagnostic_step(ws, 0, b, region); uishell_overview_end(&o);
  uishell_sidebar_local_delete_group(window, made_group_id);
  ws->workspace_zoom_open = 0;
  o = uishell_overview_diagnostic_step(ws, 1000, b, region); uishell_overview_end(&o);
  ws->workspace_zoom_open = saved_toggle;
  fprintf(stderr, "Overview diagnostics: %s\n", ok ? "passed" : "FAILED");
  scratch_end(scratch);
  return ok;
}
