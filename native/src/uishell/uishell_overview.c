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
// those.
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
  F64 pan_by;
  B32 zooms;
  U64 zoom_group;
  B32 picks;
  WheelhouseWorkspaceId pick;
  CFG_ID pick_cfg;
};

// What a drag moves, chosen as it starts: mostly sideways over a row too
// long for the region slides the row; mostly up or down, over a row or
// between them, pans an overview taller than the region.
typedef enum UIShell_OverviewDragAxis
{
  UIShell_OverviewDragAxis_None,
  UIShell_OverviewDragAxis_Slide,
  UIShell_OverviewDragAxis_Pan,
}
UIShell_OverviewDragAxis;

// What's let go of mid-drag glides on at its speed then, slowing to a stop
// over about a third of a second, or at its end.
#define UIShell_OverviewGlideTauMs 325.0
// A drag held still this long before its release doesn't glide.
#define UIShell_OverviewGlideHoldMs 60.0

internal B32
uishell_overview_slides(WheelhouseOverviewFrame *f, U64 group)
{
  return group < f->group_count && f->view_mode == WHEELHOUSE_OVERVIEW_ROWS && f->groups[group].slide_min < 0;
}

// Whether the overview is taller than the region, so it pans.
internal B32
uishell_overview_pans(WheelhouseOverviewFrame *f)
{
  return (f->mode == WHEELHOUSE_OVERVIEW_OVERVIEW &&
          (f->bounds.y0 < f->view.y0 - 1e-6 || f->bounds.y1 > f->view.y1 + 1e-6));
}

// Scene units a pixel of the region, across or down.
internal F64
uishell_overview_units(UIShell_Overview *o, Axis2 axis)
{
  WheelhouseOverviewFrame *f = o->frame;
  Vec2F32 dim = dim_2f32(o->region);
  return axis == Axis2_X ? (f->view.x1 - f->view.x0)/Max(1.f, dim.x) : (f->view.y1 - f->view.y0)/Max(1.f, dim.y);
}

// Where a drag's axis is now: the row's slide, or the camera's height.
internal F64
uishell_overview_drag_place(WheelhouseOverviewFrame *f, UIShell_OverviewDragAxis axis, U64 group)
{
  return axis == UIShell_OverviewDragAxis_Slide ? f->groups[group].slide : (f->view.y0 + f->view.y1)/2;
}

internal void
uishell_overview_move_by(UIShell_OverviewMove *move, UIShell_OverviewDragAxis axis, U64 group, F64 by)
{
  if(by == 0) { return; }
  if(axis == UIShell_OverviewDragAxis_Slide && !move->slides) { move->slides = 1; move->slide_group = group; move->slide_by = by; }
  if(axis == UIShell_OverviewDragAxis_Pan && !move->pans)     { move->pans = 1; move->pan_by = by; }
}

// Dragging the overview with the mouse slides a row or pans, following the
// mouse from where it was when pressed, as far as the ends. A press only
// becomes a drag past half a line, so a click on a tile still picks it.
// `group` is the row under the press, or none between rows.
internal void
uishell_overview_drag(UIShell_Overview *o, UI_Signal sig, U64 group, UIShell_OverviewMove *move)
{
  WheelhouseOverviewFrame *f = o->frame;
  UIShell_SidebarState *state = o->state;
  if(ui_pressed(sig))
  {
    state->overview_dragged = 0;
    state->overview_drag_axis = UIShell_OverviewDragAxis_None;
    state->overview_drag_v = 0;
    state->overview_drag_ms = o->now_ms;
    state->overview_glide_v = 0;
  }
  UIShell_OverviewDragAxis axis = state->overview_drag_axis;
  if(ui_released(sig) && axis != UIShell_OverviewDragAxis_None)
  {
    B32 held = (o->now_ms - state->overview_drag_ms > UIShell_OverviewGlideHoldMs);
    state->overview_glide_axis = axis;
    state->overview_glide_group = group;
    state->overview_glide_v = (held || !o->motion) ? 0 : state->overview_drag_v;
    state->overview_glide_ms = o->now_ms;
  }
  if(!ui_dragging(sig)) { return; }
  Vec2F32 d = ui_drag_delta();
  if(!state->overview_dragged)
  {
    if(Max(abs_f32(d.x), abs_f32(d.y)) < ui_top_font_size()*0.5f) { return; }
    state->overview_dragged = 1;
    axis = (abs_f32(d.x) >= abs_f32(d.y) ? (uishell_overview_slides(f, group) ? UIShell_OverviewDragAxis_Slide : UIShell_OverviewDragAxis_None)
                                         : (uishell_overview_pans(f) ? UIShell_OverviewDragAxis_Pan : UIShell_OverviewDragAxis_None));
    state->overview_drag_axis = axis;
    if(axis != UIShell_OverviewDragAxis_None)
    {
      state->overview_drag_from = state->overview_drag_at = uishell_overview_drag_place(f, axis, group);
    }
  }
  if(axis == UIShell_OverviewDragAxis_None) { return; }
  // Dragging down shows what is above.
  F64 to = state->overview_drag_from + (axis == UIShell_OverviewDragAxis_Slide ? d.x*uishell_overview_units(o, Axis2_X)
                                                                               : -d.y*uishell_overview_units(o, Axis2_Y));
  // Its speed, smoothed over the last few moves.
  F64 dt = o->now_ms - state->overview_drag_ms;
  if(to != state->overview_drag_at && dt > 0)
  {
    F64 v = (to - state->overview_drag_at)/dt;
    state->overview_drag_v = (dt > UIShell_OverviewGlideHoldMs) ? v : mix_1f64(state->overview_drag_v, v, 0.6);
    state->overview_drag_at = to;
    state->overview_drag_ms = o->now_ms;
  }
  uishell_overview_move_by(move, axis, group, to - uishell_overview_drag_place(f, axis, group));
}

// What's gliding, slowing: its move this frame, until it stops or meets
// its end.
internal void
uishell_overview_glide(UIShell_Overview *o, UIShell_OverviewMove *move)
{
  WheelhouseOverviewFrame *f = o->frame;
  UIShell_SidebarState *state = o->state;
  UIShell_OverviewDragAxis axis = state->overview_glide_axis;
  U64 group = state->overview_glide_group;
  if(state->overview_glide_v == 0) { return; }
  F64 dt = o->now_ms - state->overview_glide_ms;
  state->overview_glide_ms = o->now_ms;
  if(axis == UIShell_OverviewDragAxis_Slide ? !uishell_overview_slides(f, group) || f->mode != WHEELHOUSE_OVERVIEW_OVERVIEW
                                            : !uishell_overview_pans(f))
  {
    state->overview_glide_v = 0;
    return;
  }
  // Exact for a speed falling exponentially: v(t) = v·e^(-t/τ).
  F64 decay = exp(-dt/UIShell_OverviewGlideTauMs);
  F64 by = state->overview_glide_v*UIShell_OverviewGlideTauMs*(1 - decay);
  state->overview_glide_v *= decay;
  B32 at_end = 0;
  if(axis == UIShell_OverviewDragAxis_Slide)
  {
    const WheelhouseOverviewGroup *g = &f->groups[group];
    F64 to = Clamp(g->slide_min, g->slide + by, 0);
    at_end = (by < 0 ? to <= g->slide_min : by > 0 && to >= 0);
    by = to - g->slide;
  }
  else
  {
    at_end = (by < 0 ? f->view.y0 + by <= f->bounds.y0 : by > 0 && f->view.y1 + by >= f->bounds.y1);
  }
  // Stopped: slower than a pixel in a tenth of a second, or at its end.
  F64 unit = uishell_overview_units(o, axis == UIShell_OverviewDragAxis_Slide ? Axis2_X : Axis2_Y);
  if(abs_f64(state->overview_glide_v)*100.0 < unit || at_end) { state->overview_glide_v = 0; }
  uishell_overview_move_by(move, axis, group, by);
  if(state->overview_glide_v != 0) { rd_request_frame(); }
}

internal String8
uishell_overview_text(Arena *arena, WheelhouseText text)
{
  return push_str8_copy(arena, str8((U8 *)text.data, text.len));
}

// The region drawn through the scene: cluster washes, group headings and
// the tiles, each showing its workspace's surface. Picking a tile and
// fitting a group are flights; scrolling pans the overview or slides a row,
// as dragging a row does.
// Input waits for the overview: a switch's flight is only drawn.
internal void
uishell_overview_ui(RD_WindowState *ws, UIShell_Overview *o, UIShell_ControlledSplit *split, CFG_Node *current)
{
  WheelhouseOverviewFrame *f = o->frame;
  if(f == 0 || !o->through_scene) { return; }
  Temp scratch = scratch_begin(0, 0);
  B32 interactive = (f->mode != WHEELHOUSE_OVERVIEW_WORKSPACE);
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
      UI_CornerRadius(floor_f32(ui_top_font_size()*0.4f))
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
          if(interactive && cfg_id != 0 && ui_clicked(sig) && !o->state->overview_dragged)
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
  }
  if(interactive) { uishell_overview_glide(o, &move); }

  //- scrolling: down and up pan the overview when it is taller than the
  // region, else slide the row under the mouse, as sideways always does
  if(interactive)
  {
    UI_Signal sig = ui_signal_from_box(scene_box);
    uishell_overview_drag(o, sig, f->group_count, &move);
    Vec2F32 px = add_2f32(sig.scroll_px, v2f32(sig.scroll.x*30.f, sig.scroll.y*30.f));
    if(px.x != 0 || px.y != 0)
    {
      F64 units = uishell_overview_units(o, Axis2_Y);
      B32 pans = uishell_overview_pans(f);
      Vec2F32 mouse = sub_2f32(ui_mouse(), o->region.p0);
      F32 slide_px = px.x;
      o->state->overview_glide_v = 0;
      if(pans) { move.pans = 1; move.pan_by = px.y*units; }
      else if(slide_px == 0) { slide_px = px.y; }
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
  scratch_end(scratch);

  //- the moves, now the region is drawn
  if(move.slides && move.slide_by != 0)
  {
    moved = wheelhouse_overview_slide(o->state->core, move.slide_group, move.slide_by, o->now_ms, 0) || moved;
  }
  if(move.pans && move.pan_by != 0)
  {
    moved = wheelhouse_overview_pan(o->state->core, move.pan_by, 0) || moved;
  }
  if(move.zooms)
  {
    moved = wheelhouse_overview_zoom_group(o->state->core, move.zoom_group, o->now_ms, 0) || moved;
  }
  if(move.picks)
  {
    o->state->overview_glide_v = 0;
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
