// A window's overview (docs/design/overview-scene.md): every workspace the
// sidebar shows live is a tile of one scene, and the workspace region is a
// camera on it. The core holds the scene and flies the camera
// (wheelhouse_overview_*); this feeds it the window's current workspace and
// region each frame, draws through the camera whenever the workspace isn't
// open and still, and passes on the user's moves.
//
// ws->workspace_zoom_open stays the overview toggle the rest of the shell
// flips (the chrome button, the tool strip, diagnostics): a change to it
// since the last frame opens or closes the overview; otherwise it follows
// the core's mode, which a switch, a pick or going back also change.

typedef struct UIShell_Overview UIShell_Overview;
struct UIShell_Overview
{
  UIShell_SidebarState *state;
  WheelhouseOverviewFrame *frame;
  Rng2F32 region;
  F64 now_ms;
  // Whether the overview moves smoothly (uishell_overview_motion) or cuts.
  B32 motion;
  // Draw the region through the scene, not the current workspace as itself.
  B32 through_scene;
  // A drag or glide past an end, in pixels as drawn: the region's content
  // is shifted by `over`, across only for row `over_group` when a row
  // slides (max_U64 when everything pans). The frame has it drawn in
  // already; this says how far, for the diagnostics.
  Vec2F32 over;
  U64 over_group;
};

// The overview's clock: frame time, at the animation speed setting, so a
// slowed-down flight can be inspected and a replayed one is deterministic.
internal F64
uishell_overview_clock_ms(UIShell_SidebarState *state)
{
  if(state->overview_clock_frame != rd_state->frame_index)
  {
    state->overview_clock_frame = rd_state->frame_index;
    F32 speed = rd_setting_f32_from_name(str8_lit("animation_speed"));
    state->overview_clock_ms += 1000.0*rd_state->frame_dt*(speed > 0 ? speed : 1.f);
  }
  return state->overview_clock_ms;
}

// The overview's view mode, as the user's setting has it: rows unless it
// says grid. Read from the user's settings, where the switch writes it, so
// it needs no window to look from.
internal U32
uishell_overview_view_mode_setting(void)
{
  CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  String8 value = cfg_node_child_from_string(user, str8_lit("overview_view_mode"))->first->string;
  return str8_match(value, str8_lit("grid"), StringMatchFlag_CaseInsensitive) ? WHEELHOUSE_OVERVIEW_GRID : WHEELHOUSE_OVERVIEW_ROWS;
}

// The sidebar state's overview_view_mode_seen starts zeroed and so says
// rows, which is where the core's overview starts: a rows setting makes
// no call on the first frame, and a grid one does.
StaticAssert(WHEELHOUSE_OVERVIEW_ROWS == 0, uishell_overview_rows_is_zero);

// Write the user's setting; false when there is no user bucket to keep it
// in (nothing may be written under the read-only nil node).
internal B32
uishell_overview_set_view_mode_setting(U32 view_mode)
{
  CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  if(user == &cfg_nil_node) { return 0; }
  CFG_Node *node = cfg_node_child_from_string_or_alloc(rd_state->cfg, user, str8_lit("overview_view_mode"));
  cfg_node_new_replacef(rd_state->cfg, node, "%s", view_mode == WHEELHOUSE_OVERVIEW_GRID ? "grid" : "rows");
  return 1;
}

// The grid's packing, as the user's setting has it: shelves unless it says
// treemap.
internal U32
uishell_overview_grid_packing_setting(void)
{
  CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  String8 value = cfg_node_child_from_string(user, str8_lit("overview_grid_packing"))->first->string;
  return str8_match(value, str8_lit("treemap"), StringMatchFlag_CaseInsensitive) ? WHEELHOUSE_OVERVIEW_TREEMAP : WHEELHOUSE_OVERVIEW_SHELVES;
}

// How wide, in points, a grid's tile needs to be to read: the user's
// setting, 160 without one.
internal F64
uishell_overview_readable_width_setting(void)
{
  CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  String8 value = cfg_node_child_from_string(user, str8_lit("overview_readable_tile_width"))->first->string;
  F64 width = value.size != 0 ? f64_from_str8(value) : 160.0;
  return width > 0 ? width : 0;
}

internal B32
uishell_overview_motion(void)
{
  return (rd_setting_b32_from_name(str8_lit("animations")) &&
          rd_setting_b32_from_name(str8_lit("workspace_animations")) &&
          !wm_reduce_motion());
}

internal void
uishell_overview_refresh(RD_WindowState *ws, UIShell_Overview *o)
{
  if(o->frame != 0) { wheelhouse_overview_frame_free(o->frame); }
  o->frame = wheelhouse_overview_frame(o->state->core, o->now_ms, 0);
  WheelhouseOverviewFrame *f = o->frame;
  // An overview with nothing in it closes.
  if(f != 0 && f->mode != WHEELHOUSE_OVERVIEW_WORKSPACE && f->tile_count == 0)
  {
    wheelhouse_overview_close(o->state->core, o->now_ms, 0);
    wheelhouse_overview_frame_free(f);
    f = o->frame = wheelhouse_overview_frame(o->state->core, o->now_ms, 0);
  }
  // At rest on its workspace the region draws it as itself, whether or not
  // the sidebar shows it.
  o->through_scene = (f != 0 && f->has_view && !f->settled &&
                      (f->mode != WHEELHOUSE_OVERVIEW_WORKSPACE || f->flying));
  if(f != 0)
  {
    ws->workspace_zoom_open = (f->mode != WHEELHOUSE_OVERVIEW_WORKSPACE);
    if(f->flying) { rd_request_frame(); }
  }
  o->state->overview_toggle_seen = !!ws->workspace_zoom_open;
  // The stretch in pixels: a row moves with its slide; the content moves
  // against the camera.
  o->over = v2f32(0, 0);
  o->over_group = max_U64;
  if(f != 0 && f->has_view && (f->stretch_x != 0 || f->stretch_y != 0))
  {
    Vec2F32 dim = dim_2f32(o->region);
    F64 px_x = dim.x/Max(1e-9, f->view.x1 - f->view.x0), px_y = dim.y/Max(1e-9, f->view.y1 - f->view.y0);
    B32 row = (f->stretch_group != WHEELHOUSE_NONE);
    o->over_group = row ? (U64)f->stretch_group : max_U64;
    o->over = v2f32((F32)((row ? 1 : -1)*f->stretch_x*px_x), (F32)(-f->stretch_y*px_y));
  }
}

// `motion` is uishell_overview_motion(), read in the window's frame.
internal UIShell_Overview
uishell_overview_begin(RD_WindowState *ws, CFG_Node *current, Rng2F32 region, B32 motion)
{
  UIShell_Overview o = {0};
  o.region = region;
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  if(state->core == 0) { return o; }
  o.state = state;
  o.now_ms = uishell_overview_clock_ms(state);
  o.motion = motion;
  WheelhouseWorkspaceId id = {0};
  if(current != &cfg_nil_node) { id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(current)); }
  Vec2F32 dim = dim_2f32(region);
  wheelhouse_overview_update(state->core, id, dim.y > 0 ? dim.x/dim.y : 0, o.now_ms, motion, 0);
  U32 view_mode = uishell_overview_view_mode_setting();
  if(view_mode != state->overview_view_mode_seen)
  {
    state->overview_view_mode_seen = view_mode;
    wheelhouse_overview_set_view_mode(state->core, view_mode, o.now_ms, 0);
  }
  // Both widths in points: the setting is, and moving to a display of
  // another scale changes neither.
  F32 scale = wm_layout_scale_from_window(ws->os);
  F64 region_pt = dim.x/(scale > 0 ? scale : 1.f);
  wheelhouse_overview_set_grid(state->core, uishell_overview_grid_packing_setting(), uishell_overview_readable_width_setting(), region_pt, o.now_ms, 0);
  if(!!ws->workspace_zoom_open != state->overview_toggle_seen)
  {
    if(ws->workspace_zoom_open) { wheelhouse_overview_open(state->core, o.now_ms, 0); }
    else                        { wheelhouse_overview_close(state->core, o.now_ms, 0); }
  }
  uishell_overview_refresh(ws, &o);
  return o;
}

internal void
uishell_overview_end(UIShell_Overview *o)
{
  if(o->frame != 0) { wheelhouse_overview_frame_free(o->frame); }
  o->frame = 0;
}

// Escape: back one level, from a group to the overview, from the overview
// to the current workspace.
internal void
uishell_overview_back(RD_WindowState *ws)
{
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  if(state->core == 0) { ws->workspace_zoom_open = 0; return; }
  UIShell_Overview o = {state, 0, r2f32p(0, 0, 0, 0), uishell_overview_clock_ms(state), 0};
  wheelhouse_overview_back(state->core, o.now_ms, 0);
  uishell_overview_refresh(ws, &o);
  uishell_overview_end(&o);
  rd_request_frame();
}

// A scene rectangle in the region, relative to its top left.
internal Rng2F32
uishell_overview_rect(UIShell_Overview *o, WheelhouseOverviewRect r)
{
  WheelhouseOverviewRect v = o->frame->view;
  Vec2F32 dim = dim_2f32(o->region);
  F64 sx = dim.x/(v.x1 - v.x0);
  F64 sy = dim.y/(v.y1 - v.y0);
  return r2f32p((F32)((r.x0 - v.x0)*sx), (F32)((r.y0 - v.y0)*sy), (F32)((r.x1 - v.x0)*sx), (F32)((r.y1 - v.y0)*sy));
}

// Whether a tile, with its label, is in the region: the scene draws only
// those. The label is under the tile, so it matters only at the top edge:
// a tile scrolled off the top still shows while its label does.
internal B32
uishell_overview_tile_in_view(UIShell_Overview *o, const WheelhouseOverviewTile *tile)
{
  Vec2F32 dim = dim_2f32(o->region);
  Rng2F32 rect = uishell_overview_rect(o, tile->rect);
  Rng2F32 label = uishell_overview_rect(o, tile->label);
  return !(rect.x1 < 0 || rect.x0 > dim.x || label.y1 < 0 || rect.y0 > dim.y);
}

// Whether the region shows a workspace this frame, so it needs building:
// the current one always (it has the keyboard, also mid-flight), and
// through the scene those whose tiles the camera sees. The others wait,
// however many the window has.
internal B32
uishell_overview_shows(UIShell_Overview *o, CFG_Node *current, CFG_ID cfg_id)
{
  if(cfg_id == current->id) { return 1; }
  WheelhouseOverviewFrame *f = o->frame;
  // Defensive: at rest the camera fits the current tile, so no other tile
  // is in view anyway; this keeps it so without the geometry's say.
  if(f == 0 || !o->through_scene) { return 0; }
  for(U64 i = 0; i < f->tile_count; i++)
  {
    if(uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_core(f->tiles[i].workspace_id)) == cfg_id)
    {
      return uishell_overview_tile_in_view(o, &f->tiles[i]);
    }
  }
  return 0;
}

// What the user did to the overview this frame. It changes the overview
// only once the region is drawn: the frame's text is borrowed from the
// overview, and a change frees it under the tiles still to draw.
typedef struct UIShell_OverviewMove UIShell_OverviewMove;
struct UIShell_OverviewMove
{
  B32 slides;
  U64 slide_group;
  F64 slide_by;
  B32 pans;
  F64 pan_dx;
  F64 pan_dy;
  B32 zooms;
  U64 zoom_group;
  B32 picks;
  WheelhouseWorkspaceId pick;
  CFG_ID pick_cfg;
  B32 sets_view_mode;
  U32 view_mode;
  B32 backs;
  // A press, its drag and its release, for the core to follow.
  B32 grabs;
  U64 grab_group;
  B32 drags;
  F64 drag_dx;
  F64 drag_dy;
  B32 releases;
  // A grid zoomed by hand, by `scale` about `scale_at` (shares of the
  // region), and the Fit all toggle.
  B32 scales;
  F64 scale;
  Vec2F32 scale_at;
  B32 fits_all;
};

// How far a pinch or a Cmd+scroll zooms: a pinch by its own growth, a
// scroll by about an eighth a wheel notch, in as it scrolls up.
internal F64
uishell_overview_scale_from_event(UI_Event *evt)
{
  if(evt->kind == UI_EventKind_Magnify) { return exp(evt->delta_2f32.x); }
  F32 d = evt->delta_2f32.y;
  if(!evt->scroll_is_precise)
  {
    // As boxes take a wheel: notches of 30, at least one.
    F32 notches = (F32)(S32)(d/30.f);
    if(notches == 0 && d != 0) { notches = d > 0 ? 1.f : -1.f; }
    d = notches*30.f;
  }
  return exp(-d*0.004);
}

// Whether the overview is taller than the region, so it pans down, or (on
// Axis2_X) a grid wider than it, so it pans across; rows slide instead.
internal B32
uishell_overview_pans(WheelhouseOverviewFrame *f, Axis2 axis)
{
  if(f->mode != WHEELHOUSE_OVERVIEW_OVERVIEW) { return 0; }
  if(axis == Axis2_X && f->view_mode != WHEELHOUSE_OVERVIEW_GRID) { return 0; }
  return (axis == Axis2_X ? (f->bounds.x0 < f->view.x0 - 1e-6 || f->bounds.x1 > f->view.x1 + 1e-6)
                          : (f->bounds.y0 < f->view.y0 - 1e-6 || f->bounds.y1 > f->view.y1 + 1e-6));
}

// Scene units a pixel of the region, across or down.
internal F64
uishell_overview_units(UIShell_Overview *o, Axis2 axis)
{
  WheelhouseOverviewFrame *f = o->frame;
  Vec2F32 dim = dim_2f32(o->region);
  return axis == Axis2_X ? (f->view.x1 - f->view.x0)/Max(1.f, dim.x) : (f->view.y1 - f->view.y0)/Max(1.f, dim.y);
}

// Pressing the overview catches whatever glides or springs back there, and
// a press that moves past half a line drags: the core slides a row or pans,
// following the mouse and stretching past the ends, and glides on once let
// go of. A press short of a drag is still a click. `group` is the row under
// the press, or none (f->group_count) between rows.
internal void
uishell_overview_drag(UIShell_Overview *o, UI_Signal sig, U64 group, UIShell_OverviewMove *move)
{
  UIShell_SidebarState *state = o->state;
  if(ui_pressed(sig))
  {
    state->overview_dragged = 0;
    state->overview_caught = 0;
    move->grabs = 1;
    move->grab_group = group < o->frame->group_count ? group : WHEELHOUSE_NONE;
  }
  if(ui_dragging(sig))
  {
    Vec2F32 d = ui_drag_delta();
    if(Max(abs_f32(d.x), abs_f32(d.y)) >= ui_top_font_size()*0.5f) { state->overview_dragged = 1; }
    if(state->overview_dragged)
    {
      Vec2F32 dim = dim_2f32(o->region);
      move->drags = 1;
      move->drag_dx = d.x/Max(1.f, dim.x);
      move->drag_dy = d.y/Max(1.f, dim.y);
    }
  }
  if(ui_released(sig)) { move->releases = 1; }
}

internal String8
uishell_overview_text(Arena *arena, WheelhouseText text)
{
  return push_str8_copy(arena, str8((U8 *)text.data, text.len));
}

// The region drawn through the scene: cluster washes, group headings and
// the tiles, each showing its workspace's surface, and the controls. Picking
// a tile, fitting a group and backing out of one are flights; scrolling
// pans the overview or slides a row, as dragging a row does. Input waits
// for the overview: a switch's flight is only drawn.
internal void
uishell_overview_ui(RD_WindowState *ws, UIShell_Overview *o, UIShell_ControlledSplit *split, CFG_Node *current)
{
  WheelhouseOverviewFrame *f = o->frame;
  if(f == 0 || !o->through_scene) { return; }
  Temp scratch = scratch_begin(0, 0);
  B32 interactive = (f->mode != WHEELHOUSE_OVERVIEW_WORKSPACE);
  // Fitting a group leaves the overview's own mode, so no grid zooms then.
  B32 zoomable = (interactive && f->zoomable);
  B32 moved = 0;
  UIShell_OverviewMove move = {0};
  Vec2F32 dim = dim_2f32(o->region);
  Rng2F32 visible = r2f32p(0, 0, dim.x, dim.y);
  F32 label_alpha = 1.f;
  const WheelhouseOverviewTile *tiles = f->tiles;
  const WheelhouseOverviewGroup *groups = f->groups;
  const WheelhouseOverviewRegion *regions = f->regions;
  // Titles fade as the camera closes on one tile, and are gone by the time
  // it fills the region.
  for(U64 i = 0; i < f->tile_count; i++)
  {
    if(tiles[i].current)
    {
      F32 fill = dim_2f32(uishell_overview_rect(o, tiles[i].rect)).y/Max(1.f, dim.y);
      label_alpha = 1.f - Clamp(0.f, (fill - 0.5f)/0.4f, 1.f);
    }
  }

  ui_set_next_rect(o->region);
  UI_Box *scene_box = ui_build_box_from_stringf(UI_BoxFlag_Clip|UI_BoxFlag_DrawBackground|
                                                (interactive ? UI_BoxFlag_Clickable|UI_BoxFlag_Scroll|UI_BoxFlag_ScrollPrecise : 0),
                                                "###workspace_overview");
  UI_Parent(scene_box)
  {
    //- the overview's controls, in the top right: the view mode switch,
    // Rows and Grid, and beside it back out of a fitted group, or, on a grid,
    // the Fit all toggle. They read input before anything under them and are
    // drawn last, over the scene.
    F32 switch_size = floor_f32(ui_top_font_size()*2.2f);
    F32 switch_margin = floor_f32(ui_top_font_size()*0.6f);
    F32 switch_gap = floor_f32(ui_top_font_size()*0.4f);
    B32 fitted = (f->mode == WHEELHOUSE_OVERVIEW_GROUP);
    U32 switch_modes[2] = {WHEELHOUSE_OVERVIEW_ROWS, WHEELHOUSE_OVERVIEW_GRID};
    UI_Signal switch_sigs[2] = {0};
    Rng2F32 switch_rects[2] = {0};
    UI_Signal side_sig = {0};
    Rng2F32 side_rect = {0};
    for(U64 i = 0; interactive && i < 2; i++)
    {
      F32 x1 = dim.x - switch_margin - (1 - i)*switch_size;
      switch_rects[i] = r2f32p(x1 - switch_size, switch_margin, x1, switch_margin + switch_size);
      ui_set_next_rect(switch_rects[i]);
      UI_Box *hit = ui_build_box_from_stringf(UI_BoxFlag_Clickable, "###overview_view_mode_%I64u", i);
      switch_sigs[i] = ui_signal_from_box(hit);
      uishell_tool_strip_tooltip(switch_sigs[i], i == 0 ? str8_lit("Rows") : str8_lit("Grid"));
      if(ui_clicked(switch_sigs[i]) && f->view_mode != switch_modes[i])
      {
        move.sets_view_mode = 1;
        move.view_mode = switch_modes[i];
      }
    }
    if((interactive && fitted) || zoomable)
    {
      F32 x1 = switch_rects[0].x0 - switch_gap;
      side_rect = r2f32p(x1 - switch_size, switch_margin, x1, switch_margin + switch_size);
      ui_set_next_rect(side_rect);
      UI_Box *hit = ui_build_box_from_stringf(UI_BoxFlag_Clickable, fitted ? "###overview_back" : "###overview_fit_all");
      side_sig = ui_signal_from_box(hit);
      uishell_tool_strip_tooltip(side_sig, fitted ? str8_lit("All groups (Esc)") : str8_lit("Fit all"));
      if(ui_clicked(side_sig) && fitted) { move.backs = 1; }
      if(ui_clicked(side_sig) && !fitted) { move.fits_all = 1; }
    }
    B32 side = (side_rect.x1 > side_rect.x0);

    //- rows too long for the region: dragging one, between its tiles or
    // on them, slides it. A row lies under its tiles and takes input after
    // them, so a press on a tile is the tile's.
    UI_Box **rows = push_array(scratch.arena, UI_Box *, f->group_count);
    for(U64 i = 0; interactive && f->view_mode == WHEELHOUSE_OVERVIEW_ROWS && i < f->group_count; i++)
    {
      if(groups[i].slide_min >= 0) { continue; }
      Rng2F32 rect = uishell_overview_rect(o, groups[i].bounds);
      if(rect.x1 < visible.x0 || rect.x0 > visible.x1 || rect.y1 < visible.y0 || rect.y0 > visible.y1) { continue; }
      ui_set_next_rect(rect);
      rows[i] = ui_build_box_from_stringf(UI_BoxFlag_Clickable, "###overview_row_%I64u", i);
    }

    //- clusters: a faint wash behind each, one hover step over the region
    Vec4F32 wash = ui_color_from_name(str8_lit("hover"));
    wash.w *= rd_hover_alpha(wash, ui_color_from_name(str8_lit("background")), RD_HoverBrightnessStep)*label_alpha;
    for(U64 i = 0; i < f->region_count; i++)
    {
      Rng2F32 rect = uishell_overview_rect(o, regions[i].rect);
      if(rect.x1 < visible.x0 || rect.x0 > visible.x1 || rect.y1 < visible.y0 || rect.y0 > visible.y1) { continue; }
      ui_set_next_rect(rect);
      ui_set_next_background_color(wash);
      UI_CornerRadius(f->view_mode == WHEELHOUSE_OVERVIEW_GRID ? 0 : floor_f32(ui_top_font_size()*0.4f))
      {
        ui_build_box_from_stringf(UI_BoxFlag_DrawBackground, "###overview_region_%I64u", i);
      }
    }

    //- group headings: clicking one fits the group
    for(U64 i = 0; i < f->group_count; i++)
    {
      Rng2F32 rect = uishell_overview_rect(o, groups[i].heading_rect);
      if(rect.x1 < visible.x0 || rect.x0 > visible.x1 || rect.y1 < visible.y0 || rect.y0 > visible.y1) { continue; }
      String8 heading = uishell_overview_text(scratch.arena, groups[i].heading);
      F32 text_w = fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, heading).x + ui_top_font_size();
      rect.x1 = Min(rect.x1, rect.x0 + text_w);
      ui_set_next_rect(rect);
      UI_Transparency(1.f - label_alpha) UI_TagF("weak")
      {
        UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|(interactive ? UI_BoxFlag_Clickable|UI_BoxFlag_DrawHotEffects : 0),
                                                "###overview_heading_%I64u", i);
        ui_box_equip_display_string(box, heading);
        if(interactive && ui_clicked(ui_signal_from_box(box)))
        {
          move.zooms = 1;
          move.zoom_group = i;
        }
      }
    }

    //- tiles, the current one last so it draws over the others it passes
    for(U64 pass = 0; pass < 2; pass++)
    {
      for(U64 i = 0; i < f->tile_count; i++)
      {
        const WheelhouseOverviewTile *tile = &tiles[i];
        if((pass == 1) != (tile->current != 0)) { continue; }
        if(!uishell_overview_tile_in_view(o, tile)) { continue; }
        Rng2F32 rect = uishell_overview_rect(o, tile->rect);
        Rng2F32 label_rect = uishell_overview_rect(o, tile->label);
        CFG_ID cfg_id = uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_core(tile->workspace_id));
        UI_TagF("tab") UI_TagF(tile->current ? "" : "inactive")
        {
          ui_set_next_rect(rect);
          UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder|
                                                  (interactive ? UI_BoxFlag_Clickable|UI_BoxFlag_DrawHotEffects : 0),
                                                  "###overview_tile_%I64u", cfg_id);
          if(cfg_id != 0)
          {
            rd_workspace_preview_demand_push(ws, cfg_id, dim_2f32(rect).x);
            UI_Key wrapper_key = ui_key_from_stringf(ui_key_zero(), "###workspace_surface_%I64u", cfg_id);
            RD_SurfaceCacheNode *node = rd_window_surface_node_lookup(ws, wrapper_key.u64[0]);
            B32 workspace_surface = node != 0 && node->rendered_hash != 0;
            if(!workspace_surface) { node = rd_window_surface_node_lookup(ws, rd_workspace_preview_surface_key(cfg_id)); }
            if(node != 0)
            {
              RD_WorkspacePreviewDraw *draw = push_array(ui_build_arena(), RD_WorkspacePreviewDraw, 1);
              draw->node = node;
              draw->workspace_surface = workspace_surface;
              draw->src_uv = r2f32p(0, 0, 1, 1);
              ui_box_equip_custom_draw(box, rd_workspace_preview_box_draw, draw);
            }
          }
          UI_Signal sig = interactive ? ui_signal_from_box(box) : (UI_Signal){0};
          if(interactive) { uishell_overview_drag(o, sig, tile->group, &move); }
          if(interactive && cfg_id != 0 && ui_clicked(sig) && !o->state->overview_dragged && !o->state->overview_caught)
          {
            move.picks = 1;
            move.pick = tile->workspace_id;
            move.pick_cfg = cfg_id;
          }
        }
        if(label_alpha > 0 && dim_2f32(label_rect).y >= ui_top_font_size())
        {
          ui_set_next_rect(label_rect);
          UI_TextAlignment(UI_TextAlign_Center) UI_Transparency(1.f - label_alpha) UI_TagF(tile->current ? "" : "weak")
          {
            UI_Box *label = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "###overview_tile_label_%I64u", cfg_id);
            ui_box_equip_display_string(label, uishell_overview_text(scratch.arena, tile->title));
          }
        }
      }
    }
    for(U64 i = 0; i < f->group_count; i++)
    {
      if(rows[i] != 0) { uishell_overview_drag(o, ui_signal_from_box(rows[i]), i, &move); }
    }

    //- the controls, drawn over the scene: a plate behind each set of flat
    // buttons, the current mode's marked with a faint wash
    if(interactive)
    {
      F32 pad = floor_f32(ui_top_font_size()*0.2f);
      for(U64 plate = 0; plate < (side ? 2 : 1); plate++)
      {
        Rng2F32 rect = plate == 0 ? r2f32p(switch_rects[0].x0, switch_rects[0].y0, switch_rects[1].x1, switch_rects[1].y1) : side_rect;
        ui_set_next_rect(pad_2f32(rect, pad));
        UI_CornerRadius(floor_f32(ui_top_font_size()*0.4f)) UI_TagF("floating")
        {
          ui_build_box_from_stringf(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawDropShadow, "###overview_controls_plate_%I64u", plate);
        }
      }
      for(U64 i = 0; i < (side ? 3 : 2); i++)
      {
        B32 back = (i == 2 && fitted);
        B32 fit_all = (i == 2 && !fitted);
        B32 marked = fit_all ? !!f->fits_all : !back && (f->view_mode == switch_modes[i]);
        UI_Signal sig = i == 2 ? side_sig : switch_sigs[i];
        Vec4F32 wash = ui_color_from_name(str8_lit("hover"));
        F32 steps = (marked ? 1.f : 0.f) + (ui_hovering(sig) ? 1.f : 0.f);
        wash.w *= rd_hover_alpha(wash, ui_color_from_name(str8_lit("background")), RD_HoverBrightnessStep)*steps;
        ui_set_next_rect(i == 2 ? side_rect : switch_rects[i]);
        ui_set_next_background_color(wash);
        UI_TextAlignment(UI_TextAlign_Center) UI_CornerRadius(ui_top_font_size()*0.25f) RD_Font(RD_FontSlot_Icons)
          UI_TagF(marked || back ? "" : "weak")
        {
          UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawText|UI_BoxFlag_DisableTextTrunc,
                                                  "###overview_control_icon_%I64u", i);
          RD_IconKind icon = back ? RD_IconKind_LeftArrow : fit_all ? RD_IconKind_Thumbnails : i == 0 ? RD_IconKind_List : RD_IconKind_Grid;
          ui_box_equip_display_string(box, rd_icon_kind_text_table[icon]);
        }
      }
    }
  }
  //- scrolling: down and up pan the overview when it is taller than the
  // region, else slide the row under the mouse; sideways pans it when it is
  // wider, else slides the row too
  if(interactive)
  {
    UI_Signal sig = ui_signal_from_box(scene_box);
    uishell_overview_drag(o, sig, f->group_count, &move);
    // A click outside a fitted group, where nothing else took it, backs out
    // to the whole overview.
    if(f->mode == WHEELHOUSE_OVERVIEW_GROUP && f->group < f->group_count && ui_clicked(sig) &&
       !o->state->overview_dragged && !o->state->overview_caught)
    {
      Rng2F32 group_rect = uishell_overview_rect(o, groups[f->group].bounds);
      if(!contains_2f32(group_rect, sub_2f32(ui_mouse(), o->region.p0))) { move.backs = 1; }
    }
    Vec2F32 px = add_2f32(sig.scroll_px, v2f32(sig.scroll.x*30.f, sig.scroll.y*30.f));
    if(px.x != 0 || px.y != 0)
    {
      F64 units = uishell_overview_units(o, Axis2_Y);
      B32 pans = uishell_overview_pans(f, Axis2_Y);
      B32 pans_across = uishell_overview_pans(f, Axis2_X);
      Vec2F32 mouse = sub_2f32(ui_mouse(), o->region.p0);
      F32 slide_px = pans_across ? 0 : px.x;
      if(pans) { move.pans = 1; move.pan_dy = px.y*units; }
      else if(slide_px == 0 && !pans_across) { slide_px = px.y; }
      if(pans_across) { move.pans = 1; move.pan_dx = px.x*uishell_overview_units(o, Axis2_X); }
      if(slide_px != 0)
      {
        for(U64 i = 0; i < f->group_count; i++)
        {
          Rng2F32 bounds = uishell_overview_rect(o, groups[i].bounds);
          if(bounds.y0 <= mouse.y && mouse.y < bounds.y1)
          {
            move.slides = 1;
            move.slide_group = i;
            move.slide_by = -slide_px*units;
            break;
          }
        }
      }
    }
  }

  //- zooming a grid by hand: a pinch, or Cmd+scroll (Ctrl+scroll off
  // macOS), about the pointer
  if(zoomable)
  {
    Vec2F32 dim_safe = v2f32(Max(1.f, dim.x), Max(1.f, dim.y));
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      B32 pinch = (evt->kind == UI_EventKind_Magnify);
      B32 accel_scroll = (evt->kind == UI_EventKind_Scroll && evt->modifiers == WM_Modifier_Accel);
      if(!(pinch || accel_scroll) || !contains_2f32(o->region, evt->pos)) { continue; }
      Vec2F32 at = sub_2f32(evt->pos, o->region.p0);
      move.scale = (move.scales ? move.scale : 1.0)*uishell_overview_scale_from_event(evt);
      move.scale_at = v2f32(at.x/dim_safe.x, at.y/dim_safe.y);
      move.scales = 1;
      ui_eat_event(evt);
    }
  }
  scratch_end(scratch);

  //- the moves, now the region is drawn: a press first, so it catches
  // what glides before anything else stops it
  if(move.grabs)
  {
    U32 caught = 0;
    wheelhouse_overview_grab(o->state->core, move.grab_group, o->now_ms, &caught, 0);
    o->state->overview_caught = !!caught;
    moved = 1;
  }
  if(move.drags)
  {
    wheelhouse_overview_drag(o->state->core, move.drag_dx, move.drag_dy, o->now_ms, 0);
    moved = 1;
  }
  if(move.releases)
  {
    wheelhouse_overview_release(o->state->core, o->now_ms, 0);
    moved = 1;
  }
  if(move.slides && move.slide_by != 0)
  {
    moved = wheelhouse_overview_slide(o->state->core, move.slide_group, move.slide_by, o->now_ms, 0) || moved;
  }
  if(move.pans && (move.pan_dx != 0 || move.pan_dy != 0))
  {
    moved = wheelhouse_overview_pan(o->state->core, move.pan_dx, move.pan_dy, 0) || moved;
  }
  if(move.backs)
  {
    moved = wheelhouse_overview_back(o->state->core, o->now_ms, 0) || moved;
  }
  if(move.scales && move.scale != 1.0)
  {
    moved = wheelhouse_overview_zoom(o->state->core, move.scale, move.scale_at.x, move.scale_at.y, 0) || moved;
  }
  if(move.fits_all)
  {
    moved = wheelhouse_overview_fit_all(o->state->core, o->now_ms, 0) || moved;
  }
  if(move.sets_view_mode)
  {
    // `seen` follows the setting, so the next frame doesn't apply it again;
    // a mode that couldn't be kept lasts until the setting changes.
    if(uishell_overview_set_view_mode_setting(move.view_mode)) { o->state->overview_view_mode_seen = move.view_mode; }
    moved = wheelhouse_overview_set_view_mode(o->state->core, move.view_mode, o->now_ms, 0) || moved;
  }
  if(move.zooms)
  {
    moved = wheelhouse_overview_zoom_group(o->state->core, move.zoom_group, o->now_ms, 0) || moved;
  }
  if(move.picks)
  {
    moved = wheelhouse_overview_pick(o->state->core, move.pick, o->now_ms, 0) || moved;
    if(move.pick_cfg != current->id)
    {
      uishell_cmd("select_workspace", .window = split->owner_cfg->id, .cfg = move.pick_cfg);
    }
  }

  // A move this frame: draw what it did next frame, and keep the toggle in
  // step with the mode now rather than then.
  if(moved)
  {
    uishell_overview_refresh(ws, o);
    rd_request_frame();
  }
}
