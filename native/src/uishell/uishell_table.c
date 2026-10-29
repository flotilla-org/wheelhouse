// The table View (#506): a binnacle Grid over a table the core owns. The
// core answers the Grid's source Effects and fills its Windows
// (wheelhouse_table_frame); this View forwards input to the Grid through
// binnacle.h and paints its Snapshot in one box, with no box per row or
// cell. Its tables are canned until the described source over Flotilla's
// tables arrives (#505).

#include "binnacle.h"

typedef struct UIShell_TableState UIShell_TableState;
struct UIShell_TableState
{
  WheelhouseTable *table;
  // The canned table's name, as the View's expression has it.
  U8 name[32];
  U64 name_size;
  // The library's binnacle ABI isn't this header's: the View says so and
  // opens nothing.
  B32 abi_mismatch;
  // The pointer's last position sent to the Grid, viewport-relative.
  Vec2F32 last_pointer;
  B32 pointer_inside;
};

// Cells painted, counted by the table diagnostics.
global U64 uishell_table_cells_drawn;
// --table_screenshot: what writes the window once a table has painted a
// few frames (uishell_table_diagnostics.c sets it), and the frames painted.
global B32 (*uishell_table_screenshot_hook)(RD_WindowState *ws);
global U64 uishell_table_screenshot_frames;

internal void
uishell_table_release(void *user_data)
{
  UIShell_TableState *state = (UIShell_TableState *)user_data;
  if(state->table) { wheelhouse_table_free(state->table); state->table = 0; }
}

internal String8
uishell_table_text(WheelhouseText text)
{
  return str8((U8 *)text.data, text.len);
}

// Where strip item index of axis draws along that axis: its pinned Strip's
// span or the scrolling Strip's.
internal Rng1F32
uishell_table_strip_clip(BinnacleAxisSnapshot *axis, size_t index)
{
  if(index < axis->pinned_len) { return r1f32(axis->leading_start, axis->leading_end); }
  if(index >= axis->strip_len - axis->trailing_len) { return r1f32(axis->trailing_start, axis->trailing_end); }
  return r1f32(axis->scrolling_start, axis->scrolling_end);
}

typedef struct UIShell_TableDraw UIShell_TableDraw;
struct UIShell_TableDraw
{
  WheelhouseTable *table;
  BinnacleSnapshot snap;
};

internal Rng2F32
uishell_table_rect(Rng2F32 origin, BinnacleRect r)
{
  return r2f32p(origin.x0+r.x, origin.y0+r.y, origin.x0+r.x+r.width, origin.y0+r.y+r.height);
}

internal void
uishell_table_draw_text(UI_Box *box, Rng2F32 cell, Rng2F32 clip, String8 text, B32 right, Vec4F32 color)
{
  if(text.size == 0) { return; }
  Rng2F32 visible = intersect_2f32(cell, clip);
  if(visible.x1 <= visible.x0 || visible.y1 <= visible.y0) { return; }
  F32 pad = box->font_size*0.4f;
  FNT_Metrics metrics = fnt_metrics_from_tag_size(box->font, box->font_size);
  F32 baseline = cell.y0 + (dim_2f32(cell).y - (metrics.ascent+metrics.descent))/2 + metrics.ascent;
  F32 x = cell.x0+pad;
  if(right)
  {
    F32 width = fnt_dim_from_tag_size_string(box->font, box->font_size, 0, 0, text).x;
    x = Max(cell.x0+pad, cell.x1-pad-width);
  }
  DR_ClipScope(intersect_2f32(dr_top_clip(), r2f32p(visible.x0+pad*0.5f, visible.y0, visible.x1-pad*0.5f, visible.y1)))
  {
    dr_text(box->font, box->font_size, 0, 0, box->text_raster_flags, v2f32(x, baseline), color, text);
  }
}

internal UI_BOX_CUSTOM_DRAW(uishell_table_draw)
{
  UIShell_TableDraw *d = (UIShell_TableDraw *)user_data;
  BinnacleSnapshot *snap = &d->snap;
  BinnacleAxisSnapshot *rows = &snap->axes[BINNACLE_ROWS], *cols = &snap->axes[BINNACLE_COLS];
  Rng2F32 o = box->rect;
  Vec4F32 text_color = ui_color_from_name(str8_lit("text"));
  Vec4F32 weak = text_color; weak.w *= 0.55f;
  Vec4F32 line = ui_color_from_name(str8_lit("border"));
  Vec4F32 selection = ui_color_from_name(str8_lit("selection"));
  Vec4F32 band = mix_4f32(ui_color_from_name(str8_lit("background")), text_color, 0.05f);
  Vec4F32 accent = rd_accent_color();
  F32 col_band = cols->header, row_band = rows->header;
  Rng2F32 body = r2f32p(o.x0+col_band, o.y0+row_band, o.x1, o.y1);

  // Body cells: each row item crossing each column item, clipped to the
  // Strips they draw in.
  for(size_t r = 0; r < rows->strip_len; r += 1)
  {
    BinnacleStripItem *ri = &rows->strip[r];
    Rng1F32 ry = uishell_table_strip_clip(rows, r);
    for(size_t c = 0; c < cols->strip_len; c += 1)
    {
      BinnacleStripItem *ci = &cols->strip[c];
      Rng1F32 cx = uishell_table_strip_clip(cols, c);
      Rng2F32 clip = intersect_2f32(body, r2f32p(o.x0+cx.min, o.y0+ry.min, o.x0+cx.max, o.y0+ry.max));
      Rng2F32 cell = r2f32p(o.x0+ci->offset, o.y0+ri->offset, o.x0+ci->offset+ci->size, o.y0+ri->offset+ri->size);
      if(clip.x1 <= clip.x0 || clip.y1 <= clip.y0) { continue; }
      if(!ri->has_key || !ci->has_key)
      {
        // Not filled yet: a faint loading wash.
        Vec4F32 loading = weak; loading.w *= 0.15f;
        dr_rect(intersect_2f32(cell, clip), loading, 0, 0, 1.f);
        continue;
      }
      U32 align = 0;
      String8 text = uishell_table_text(wheelhouse_table_cell(d->table, ri->key, ci->key, &align));
      uishell_table_draw_text(box, cell, clip, text, align == WHEELHOUSE_TABLE_ALIGN_RIGHT, text_color);
      uishell_table_cells_drawn += 1;
    }
  }

  // Selection and picks, under the lines.
  DR_ClipScope(intersect_2f32(dr_top_clip(), body))
  {
    Vec4F32 wash = selection; wash.w *= 0.45f;
    for(size_t i = 0; i < snap->selection_rects_len; i += 1)
    {
      dr_rect(uishell_table_rect(o, snap->selection_rects[i]), wash, 0, 0, 1.f);
    }
  }

  // Column rules, between the column header band and the body.
  for(size_t c = 0; c < cols->strip_len; c += 1)
  {
    BinnacleStripItem *ci = &cols->strip[c];
    Rng1F32 cx = uishell_table_strip_clip(cols, c);
    F32 x = o.x0+ci->offset+ci->size;
    if(x > o.x0+cx.min && x <= o.x0+cx.max)
    {
      dr_rect(r2f32p(x, o.y0, x+1.f, o.y1), line, 0, 0, 0.f);
    }
  }

  // Header bands.
  dr_rect(r2f32p(o.x0, o.y0, o.x1, o.y0+row_band), band, 0, 0, 0.f);
  dr_rect(r2f32p(o.x0, o.y0+row_band, o.x0+col_band, o.y1), band, 0, 0, 0.f);
  for EachIndex(axis_idx, 2)
  {
    BinnacleAxisSnapshot *axis = &snap->axes[axis_idx];
    Rng2F32 band_rect = axis_idx == BINNACLE_COLS ? r2f32p(o.x0+col_band, o.y0, o.x1, o.y0+row_band)
                                                  : r2f32p(o.x0, o.y0+row_band, o.x0+col_band, o.y1);
    for(size_t i = 0; i < axis->headers_len; i += 1)
    {
      BinnacleHeader *h = &axis->headers[i];
      Rng1F32 span = uishell_table_strip_clip(axis, i);
      Rng2F32 clip = band_rect;
      if(axis_idx == BINNACLE_COLS) { clip.x0 = Max(clip.x0, o.x0+span.min); clip.x1 = Min(clip.x1, o.x0+span.max); }
      else { clip.y0 = Max(clip.y0, o.y0+span.min); clip.y1 = Min(clip.y1, o.y0+span.max); }
      Rng2F32 rect = uishell_table_rect(o, h->rect);
      if(h->flags & (BINNACLE_HEADER_SELECTED|BINNACLE_HEADER_HOVERED))
      {
        Vec4F32 lit = h->flags & BINNACLE_HEADER_SELECTED ? selection : text_color;
        lit.w *= h->flags & BINNACLE_HEADER_SELECTED ? 0.45f : 0.06f;
        DR_ClipScope(intersect_2f32(dr_top_clip(), clip)) { dr_rect(rect, lit, 0, 0, 1.f); }
      }
      if(!h->has_key) { continue; }
      Temp scratch = scratch_begin(0, 0);
      String8 label = uishell_table_text(wheelhouse_table_header(d->table, (U32)axis_idx, h->key, h->position));
      if(h->flags & BINNACLE_HEADER_SORTED)
      {
        String8 arrow = h->flags & BINNACLE_HEADER_DESCENDING ? str8_lit(" ▼") : str8_lit(" ▲");
        label = h->sort_place > 1 ? push_str8f(scratch.arena, "%S%S%u", label, arrow, h->sort_place)
                                  : push_str8f(scratch.arena, "%S%S", label, arrow);
      }
      B32 pending = !!(h->flags & (BINNACLE_HEADER_PENDING|BINNACLE_HEADER_MOVE_PENDING|BINNACLE_HEADER_FILTER_PENDING));
      uishell_table_draw_text(box, rect, clip, label, axis_idx == BINNACLE_ROWS, pending ? weak : text_color);
      scratch_end(scratch);
    }
  }
  dr_rect(r2f32p(o.x0, o.y0+row_band-1.f, o.x1, o.y0+row_band), line, 0, 0, 0.f);
  dr_rect(r2f32p(o.x0+col_band-1.f, o.y0, o.x0+col_band, o.y1), line, 0, 0, 0.f);

  // Focus, then a drag's drop line.
  if(snap->has_focus_rect)
  {
    DR_ClipScope(intersect_2f32(dr_top_clip(), snap->outline_focused ? o : body))
    {
      dr_rect(uishell_table_rect(o, snap->focus_rect), accent, 0, 1.5f, 1.f);
    }
  }
  if(snap->has_drop)
  {
    Rng2F32 drop = snap->drop_axis == BINNACLE_COLS ? r2f32p(o.x0+snap->drop_offset-1.f, o.y0, o.x0+snap->drop_offset+1.f, o.y1)
                                                     : r2f32p(o.x0, o.y0+snap->drop_offset-1.f, o.x1, o.y0+snap->drop_offset+1.f);
    dr_rect(drop, accent, 0, 0, 0.f);
  }

  // Scroll thumbs: a thin overlay bar along each axis that overflows.
  F32 thick = Max(3.f, box->font_size*0.3f);
  Vec4F32 thumb = text_color; thumb.w *= 0.3f;
  if(rows->thumb.size < 1.0)
  {
    F32 travel = dim_2f32(body).y;
    F32 y0 = body.y0 + (F32)(rows->thumb.fraction*(1.0-rows->thumb.size))*travel;
    dr_rect(r2f32p(o.x1-thick-2.f, y0, o.x1-2.f, y0+Max(thick*2, (F32)rows->thumb.size*travel)), thumb, thick/2, 0, 1.f);
  }
  if(cols->thumb.size < 1.0)
  {
    F32 travel = dim_2f32(body).x;
    F32 x0 = body.x0 + (F32)(cols->thumb.fraction*(1.0-cols->thumb.size))*travel;
    dr_rect(r2f32p(x0, o.y1-thick-2.f, x0+Max(thick*2, (F32)cols->thumb.size*travel), o.y1-2.f), thumb, thick/2, 0, 1.f);
  }
}

internal U32
uishell_table_mods(WM_Modifiers m)
{
  return ((m & WM_Modifier_Shift) ? BINNACLE_MOD_SHIFT : 0) | ((m & WM_Modifier_Ctrl) ? BINNACLE_MOD_CTRL : 0) |
         ((m & WM_Modifier_Alt) ? BINNACLE_MOD_ALT : 0) | ((m & WM_Modifier_Super) ? BINNACLE_MOD_META : 0);
}

internal void
uishell_table_act(BinnacleGrid *grid, BinnacleAction action)
{
  action.size = sizeof(action);
  binnacle_grid_action(grid, &action);
}

// One keyboard event for the Grid: 1 if the Grid took it.
internal B32
uishell_table_key(BinnacleGrid *grid, UI_Event *evt)
{
  BinnacleAction a = {0};
  if(evt->flags & UI_EventFlag_Copy) { a.kind = BINNACLE_ACTION_COPY; uishell_table_act(grid, a); return 1; }
  if(evt->kind == UI_EventKind_Text && evt->string.size != 0)
  {
    a.kind = BINNACLE_ACTION_TYPE_TEXT;
    a.args.type_text = (BinnacleText){evt->string.str, evt->string.size};
    uishell_table_act(grid, a);
    return 1;
  }
  if(evt->kind == UI_EventKind_Navigate && (evt->delta_2s32.x != 0 || evt->delta_2s32.y != 0))
  {
    B32 extend = !!(evt->flags & UI_EventFlag_KeepMark);
    U32 by = BINNACLE_FOCUS_ITEM;
    switch(evt->delta_unit)
    {
      default:{}break;
      case UI_EventDeltaUnit_Page:{ by = BINNACLE_FOCUS_PAGE; }break;
      case UI_EventDeltaUnit_Line:
      case UI_EventDeltaUnit_Whole:{ by = BINNACLE_FOCUS_EDGE; }break;
    }
    for EachIndex(axis_idx, 2)
    {
      S32 delta = axis_idx == BINNACLE_ROWS ? evt->delta_2s32.y : evt->delta_2s32.x;
      if(delta == 0) { continue; }
      a.kind = BINNACLE_ACTION_MOVE_FOCUS;
      a.args.move_focus = (BinnacleMoveFocus){(U32)axis_idx, by, delta > 0, extend};
      uishell_table_act(grid, a);
    }
    return 1;
  }
  if(evt->kind != UI_EventKind_Press) { return 0; }
  if(evt->slot == UI_EventActionSlot_Accept || evt->key == WM_Key_Return) { a.kind = BINNACLE_ACTION_ACTIVATE; }
  else if(evt->slot == UI_EventActionSlot_Cancel || evt->key == WM_Key_Esc) { a.kind = BINNACLE_ACTION_CANCEL; }
  else if(evt->key == WM_Key_F2) { a.kind = BINNACLE_ACTION_BEGIN_EDIT; }
  else if(evt->key == WM_Key_A && (evt->modifiers & WM_Modifier_Accel)) { a.kind = BINNACLE_ACTION_SELECT_ALL; }
  else { return 0; }
  uishell_table_act(grid, a);
  return 1;
}

WH_VIEW_UI_FUNCTION_DEF(table)
{
  Temp scratch = scratch_begin(0, 0);
  Rng2F32 rect = ctx->rect;
  UIShell_TableState *state = wh_view_state(ctx, UIShell_TableState);
  CFG_Node *view = rd_cfg_from_view(ctx->settings.view);
  String8 name = str8_skip_chop_whitespace(rd_expr_from_cfg(view));
  F32 line = ui_top_font_size()*1.6f;
  F32 digit = fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, str8_lit("0")).x;

  // The table follows the View's expression: open, reopen or close.
  if(state->table && !str8_match(name, str8(state->name, state->name_size), 0))
  {
    uishell_table_release(state);
  }
  if(!state->table && name.size != 0 && name.size <= sizeof(state->name))
  {
    state->abi_mismatch = binnacle_abi_version() != BINNACLE_ABI_VERSION;
    if(!state->abi_mismatch)
    {
      state->table = wheelhouse_table_open((WheelhouseText){name.str, name.size}, line, digit);
      MemoryCopy(state->name, name.str, name.size);
      state->name_size = name.size;
      rd_view_state_from_view(ctx->settings.view, 0)->release_user_data = uishell_table_release;
    }
  }

  Vec2F32 dim = dim_2f32(rect);
  UI_Box *root;
  UI_FixedWidth(dim.x) UI_FixedHeight(dim.y) UI_ChildLayoutAxis(Axis2_Y)
  {
    root = ui_build_box_from_string(0, str8_lit("table_view"));
  }
  if(!state->table)
  {
    // No table yet: offer the canned ones.
    UI_Parent(root) UI_PrefHeight(ui_em(2.f, 1)) UI_PrefWidth(ui_em(16, 1))
    {
      if(state->abi_mismatch)
      {
        ui_labelf("binnacle ABI %u is required; this library has ABI %u", BINNACLE_ABI_VERSION, binnacle_abi_version());
      }
      else if(name.size != 0)
      {
        ui_labelf("No table named \"%S\"", name);
      }
      ui_labelf("Open a table:");
      for(size_t i = 0; i < wheelhouse_table_count(); i += 1)
      {
        String8 option = uishell_table_text(wheelhouse_table_name(i));
        if(ui_clicked(ui_buttonf("%S###table_%S", option, option)))
        {
          CFG_Node *expr = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("expression"));
          cfg_node_new_replace(rd_state->cfg, expr, option);
        }
      }
    }
    scratch_end(scratch);
    return;
  }

  BinnacleGrid *grid = wheelhouse_table_grid(state->table);
  UI_Box *box;
  UI_Parent(root) UI_PrefWidth(ui_px(dim.x, 1)) UI_PrefHeight(ui_px(dim.y, 1))
  {
    box = ui_build_box_from_string(UI_BoxFlag_Clickable|UI_BoxFlag_Scroll|UI_BoxFlag_ScrollPrecise|UI_BoxFlag_Clip, str8_lit("table_grid"));
  }

  // Input from this frame, against last frame's layout.
  UI_Signal sig = ui_signal_from_box(box);
  Vec2F32 mouse = sub_2f32(ui_mouse(), box->rect.p0);
  U32 mods = uishell_table_mods(wm_get_modifiers());
  B32 inside = ui_hovering(sig) || ui_dragging(sig);
  if(ui_pressed(sig))
  {
    uishell_cmd("focus_panel");
    BinnaclePointerEvent e = {BINNACLE_POINTER_DOWN, mouse.x, mouse.y, BINNACLE_BUTTON_PRIMARY, mods, ui_double_clicked(sig) ? 2u : 1u};
    binnacle_grid_pointer(grid, e);
  }
  else if(inside && (!state->pointer_inside || mouse.x != state->last_pointer.x || mouse.y != state->last_pointer.y))
  {
    BinnaclePointerEvent e = {BINNACLE_POINTER_MOVE, mouse.x, mouse.y, BINNACLE_BUTTON_PRIMARY, mods, 0};
    binnacle_grid_pointer(grid, e);
  }
  if(ui_released(sig))
  {
    BinnaclePointerEvent e = {BINNACLE_POINTER_UP, mouse.x, mouse.y, BINNACLE_BUTTON_PRIMARY, mods, 0};
    binnacle_grid_pointer(grid, e);
  }
  state->last_pointer = mouse;
  state->pointer_inside = inside;
  if(sig.scroll_px.x != 0 || sig.scroll_px.y != 0 || sig.scroll.x != 0 || sig.scroll.y != 0)
  {
    BinnacleAction a = {0};
    a.kind = BINNACLE_ACTION_SCROLL;
    a.args.scroll = (BinnacleScroll){sig.scroll_px.x + sig.scroll.x*line*3, sig.scroll_px.y + sig.scroll.y*line*3};
    uishell_table_act(grid, a);
  }
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active())
  {
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      if(uishell_table_key(grid, evt)) { ui_eat_event(evt); }
    }
  }

  // The frame: the core answers and fills, then the Grid lays out.
  U64 wake = wheelhouse_table_frame(state->table, dim.x, dim.y, now_time_us());
  UIShell_TableDraw *draw = push_array(ui_build_arena(), UIShell_TableDraw, 1);
  draw->table = state->table;
  // Effects for the host: nothing acts on them yet (#506 has no actions),
  // so drain them to keep the queue bounded, before the Snapshot whose
  // arrays the draw reads.
  for(BinnacleEffect effect; binnacle_grid_poll_effect(grid, &effect);) {}
  binnacle_grid_snapshot(grid, &draw->snap);
  ui_box_equip_custom_draw(box, uishell_table_draw, draw);
  if(wake != WHEELHOUSE_TABLE_NEVER || draw->snap.capturing || draw->snap.view_pending) { rd_request_frame(); }
  if(uishell_table_screenshot_hook != 0)
  {
    uishell_table_screenshot_frames += 1;
    if(uishell_table_screenshot_frames == 10) { rd_state->frame_diagnostic = uishell_table_screenshot_hook; }
    rd_request_frame();
  }
  scratch_end(scratch);
}
