// The table View (#506): a binnacle Grid over a table the core owns. The
// core answers the Grid's source Effects and fills its Windows
// (wheelhouse_table_frame); this View forwards input to the Grid through
// binnacle.h and paints its Snapshot in one box, with no box per row or
// cell. Its expression names a canned table (#506) or is a described
// table's link, read live from its server (#529).

#include "binnacle.h"

typedef struct UIShell_TableState UIShell_TableState;
struct UIShell_TableState
{
  WheelhouseTable *table;
  // The table's name or link, as the View's expression has it.
  U8 name[1024];
  U64 name_size;
  // The library's binnacle ABI isn't this header's: the View says so and
  // opens nothing.
  B32 abi_mismatch;
  // The pointer's last position sent to the Grid, viewport-relative.
  Vec2F32 last_pointer;
  B32 pointer_inside;
  // The name a failed open was tried with, so it isn't retried each frame.
  U8 failed[1024];
  U64 failed_size;
  // As of the last Snapshot, per UI axis: binnacle's Thumb, the content's
  // length it stands for, and the scrolling Strip's length (a page). The
  // bars are laid out from these, before this frame's Grid lays out.
  UI_ScrollThumb thumb[Axis2_COUNT];
  F32 content_px[Axis2_COUNT];
  F32 page_px[Axis2_COUNT];
  // Where the scrolling Strip draws, which the bar keeps to: it scrolls only
  // what moves, clear of the headers and pinned items.
  Rng1F32 scrolling[Axis2_COUNT];
  // Bumped when either Thumb moves, which keeps an overlay bar up a while.
  U64 scrolled;
  // The End Gap given each axis: the reserved gutter across its far end.
  F32 end_gap[Axis2_COUNT];
  // The fraction a thumb drag last asked for, so a held thumb asks once.
  F64 dragged_to[Axis2_COUNT];
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

// Seconds east of UTC that local time is now, for a described table's
// dates.
internal S32
uishell_table_utc_offset(void)
{
  DateTime utc = now_time_universal();
  DateTime local = local_from_universal_time(&utc);
  // The days differ by at most one, across a month's end too.
  S32 days = (S32)local.day - (S32)utc.day;
  if(days > 1) { days = -1; }
  if(days < -1) { days = 1; }
  return ((days*24 + ((S32)local.hour - (S32)utc.hour))*60 + ((S32)local.min - (S32)utc.min))*60;
}

internal WheelhouseTable *
uishell_table_open(String8 name, F32 line, F32 digit)
{
  return wheelhouse_table_open((WheelhouseText){name.str, name.size}, line, digit, uishell_table_utc_offset(), wm_send_wakeup_event);
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
  // The border color can be near the band's own, so header rules are drawn
  // a step from the band towards the text.
  Vec4F32 header_line = mix_4f32(band, text_color, 0.2f);
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

  // Header bands, then the rules after each item across them and the body,
  // so header cells are divided as the body's are.
  dr_rect(r2f32p(o.x0, o.y0, o.x1, o.y0+row_band), band, 0, 0, 0.f);
  dr_rect(r2f32p(o.x0, o.y0+row_band, o.x0+col_band, o.y1), band, 0, 0, 0.f);
  for(size_t c = 0; c < cols->strip_len; c += 1)
  {
    BinnacleStripItem *ci = &cols->strip[c];
    Rng1F32 cx = uishell_table_strip_clip(cols, c);
    F32 x = o.x0+ci->offset+ci->size;
    if(x > o.x0+cx.min && x <= o.x0+cx.max)
    {
      dr_rect(r2f32p(x, o.y0, x+1.f, o.y0+row_band), header_line, 0, 0, 0.f);
      dr_rect(r2f32p(x, o.y0+row_band, x+1.f, o.y1), line, 0, 0, 0.f);
    }
  }
  for(size_t r = 0; r < rows->strip_len; r += 1)
  {
    BinnacleStripItem *ri = &rows->strip[r];
    Rng1F32 ry = uishell_table_strip_clip(rows, r);
    F32 y = o.y0+ri->offset+ri->size;
    if(y > o.y0+ry.min && y <= o.y0+ry.max)
    {
      dr_rect(r2f32p(o.x0, y-1.f, o.x0+col_band, y), header_line, 0, 0, 0.f);
      dr_rect(r2f32p(o.x0+col_band, y-1.f, o.x1, y), line, 0, 0, 0.f);
    }
  }
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
  dr_rect(r2f32p(o.x0, o.y0+row_band-1.f, o.x1, o.y0+row_band), header_line, 0, 0, 0.f);
  dr_rect(r2f32p(o.x0+col_band-1.f, o.y0, o.x0+col_band, o.y1), header_line, 0, 0, 0.f);

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

// One keyboard event for the Grid: 1 if the Grid took it. The keys follow
// binnacle's web host (web/src/keymap.ts) where Wheelhouse's own bindings
// leave them free.
internal B32
uishell_table_key(BinnacleGrid *grid, UI_Event *evt)
{
  BinnacleAction a = {0};
  if(evt->flags & UI_EventFlag_Copy) { a.kind = BINNACLE_ACTION_COPY; uishell_table_act(grid, a); return 1; }
  // Shift+Space and Ctrl+Space select, on their press: the space they also
  // type is not text for the cell. There is no cell editor yet (F2 only
  // asks for one); when there is, it takes the keys while open, as the web
  // host's textarea does, and none of these reach the Grid.
  if(evt->kind == UI_EventKind_Text && str8_match(evt->string, str8_lit(" "), 0) &&
     (evt->modifiers & (WM_Modifier_Shift|WM_Modifier_Ctrl)))
  {
    return 1;
  }
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
      // Home and End (a line's ends) go to the row's first or last column,
      // Ctrl+Home and Ctrl+End (Whole) to the table's first or last row, and
      // Cmd (Ctrl elsewhere) with an arrow (Word) to that edge.
      case UI_EventDeltaUnit_Word:
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
  else if(evt->key == WM_Key_Space && (evt->modifiers & WM_Modifier_Shift)) { a.kind = BINNACLE_ACTION_SELECT_ROW; }
  else if(evt->key == WM_Key_Space && (evt->modifiers & WM_Modifier_Ctrl)) { a.kind = BINNACLE_ACTION_SELECT_COLUMN; }
  else if(evt->key == WM_Key_F3) { a.kind = (evt->modifiers & WM_Modifier_Shift) ? BINNACLE_ACTION_FIND_PREVIOUS : BINNACLE_ACTION_FIND_NEXT; }
  // Alt with Page Up or Down pages along the columns.
  else if((evt->key == WM_Key_PageUp || evt->key == WM_Key_PageDown) && (evt->modifiers & WM_Modifier_Alt))
  {
    a.kind = BINNACLE_ACTION_MOVE_FOCUS;
    a.args.move_focus = (BinnacleMoveFocus){BINNACLE_COLS, BINNACLE_FOCUS_PAGE, evt->key == WM_Key_PageDown,
                                            !!(evt->modifiers & WM_Modifier_Shift)};
  }
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
  rd_view_state_from_view(ctx->settings.view, 0)->release_user_data = uishell_table_release;
  if(!state->table && name.size != 0 && name.size <= sizeof(state->name) &&
     !str8_match(name, str8(state->failed, state->failed_size), 0))
  {
    state->abi_mismatch = binnacle_abi_version() != BINNACLE_ABI_VERSION;
    if(!state->abi_mismatch)
    {
      state->table = uishell_table_open(name, line, digit);
      MemoryCopy(state->name, name.str, name.size);
      state->name_size = name.size;
    }
    if(!state->table)
    {
      MemoryCopy(state->failed, name.str, name.size);
      state->failed_size = name.size;
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
      ui_labelf("Open a table, or give a table's link as the View's expression:");
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
  // A described table's state (connecting, retrying, failed, partly shown)
  // takes a line above the Grid, as of the last frame.
  String8 status = push_str8_copy(scratch.arena, uishell_table_text(wheelhouse_table_status(state->table)));
  F32 status_h = status.size != 0 ? line : 0;
  if(status.size != 0) UI_Parent(root) UI_PrefWidth(ui_px(dim.x, 1)) UI_PrefHeight(ui_px(status_h, 1))
  {
    ui_label(status);
  }
  // The Grid in a scroll region, with either style's bars, as other Views
  // have. Its Thumbs are estimates in no units, so the bars take them as
  // they are, from the last Snapshot.
  UI_ScrollRegionParams params = ui_scroll_region_params(r2f32p(0, status_h, dim.x, Max(dim.y, status_h)),
                                                        UI_ScrollAxisPolicy_Auto, UI_ScrollAxisPolicy_Auto);
  params.content_dim_px = v2f32(state->content_px[Axis2_X], state->content_px[Axis2_Y]);
  // The Grid runs to the edge under reserved gutters, so its headers and
  // pinned items carry on beside a bar kept to what scrolls; an End Gap the
  // gutter's size keeps the last item clear of the bar.
  params.content_under_gutters = 1;
  UI_ScrollRegion region = ui_scroll_region_layout(params);
  UI_ScrollRegionThumbAxis axes[Axis2_COUNT] = {0};
  for EachEnumVal(Axis2, axis)
  {
    // The bar only compares activity for a change, so the count wraps: kept
    // to 16 bits, every bump stays a distinct F32.
    axes[axis] = (UI_ScrollRegionThumbAxis){state->thumb[axis], (F32)(state->scrolled & 0xffff),
                                            &state->thumb[axis].fraction, sizeof(state->thumb[axis].fraction),
                                            state->scrolling[axis]};
  }
  UI_ScrollRegionThumbSignal bars = ui_scroll_region_build_thumbs(root, ui_key_from_string(root->key, str8_lit("table_grid")), &region, axes,
                                                                  UI_BoxFlag_Clickable|UI_BoxFlag_Scroll|UI_BoxFlag_ScrollPrecise|UI_BoxFlag_Clip);
  UI_Box *box = bars.content_box;
  Vec2F32 grid_dim = dim_2f32(region.content);
  for EachEnumVal(Axis2, axis)
  {
    F32 gap = region.content.p1.v[axis] - region.viewport.p1.v[axis];
    if(gap != state->end_gap[axis])
    {
      state->end_gap[axis] = gap;
      binnacle_grid_set_end_gap(grid, axis == Axis2_Y ? BINNACLE_ROWS : BINNACLE_COLS, gap);
    }
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
  else if(!inside && state->pointer_inside)
  {
    // The pointer left: binnacle has no leave event, so a move outside the
    // viewport clears what it was hovering.
    BinnaclePointerEvent e = {BINNACLE_POINTER_MOVE, -1.f, -1.f, BINNACLE_BUTTON_PRIMARY, mods, 0};
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
  // The bars: a drag puts the thumb at its fraction, an arrow scrolls an
  // item and the track a page.
  for EachEnumVal(Axis2, axis)
  {
    UI_ScrollBarSignal bar = bars.bar[axis];
    U32 binnacle_axis = axis == Axis2_Y ? BINNACLE_ROWS : BINNACLE_COLS;
    BinnacleAction a = {0};
    if(bar.kind != UI_ScrollBarActionKind_Drag) { state->dragged_to[axis] = -1; }
    else if(bar.drag_moved)
    {
      F64 origin = 0;
      if(bar.drag_origin != 0) { MemoryCopy(&origin, bar.drag_origin, sizeof(origin)); }
      F64 fraction = Clamp(0.0, origin + bar.drag_pct, 1.0);
      if(fraction != state->dragged_to[axis])
      {
        state->dragged_to[axis] = fraction;
        a.kind = BINNACLE_ACTION_SCROLL_TO;
        a.args.scroll_to = (BinnacleScrollTo){binnacle_axis, fraction};
        uishell_table_act(grid, a);
      }
    }
    if(bar.step != 0 && bar.page)
    {
      F32 page = bar.step*state->page_px[axis];
      a.kind = BINNACLE_ACTION_SCROLL;
      a.args.scroll = axis == Axis2_Y ? (BinnacleScroll){0, page} : (BinnacleScroll){page, 0};
      uishell_table_act(grid, a);
    }
    else if(bar.step != 0)
    {
      a.kind = BINNACLE_ACTION_SCROLL_BY;
      a.args.scroll_by = (BinnacleScrollBy){binnacle_axis, bar.step};
      uishell_table_act(grid, a);
    }
  }
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active())
  {
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      if(uishell_table_key(grid, evt)) { ui_eat_event(evt); }
    }
  }

  // The frame: the core answers and fills, then the Grid lays out.
  U64 now = now_time_us();
  U64 wake = wheelhouse_table_frame(state->table, grid_dim.x, grid_dim.y, now);
  // What came this frame can change the status line laid out above.
  if(!str8_match(status, uishell_table_text(wheelhouse_table_status(state->table)), 0)) { rd_request_frame(); }
  UIShell_TableDraw *draw = push_array(ui_build_arena(), UIShell_TableDraw, 1);
  draw->table = state->table;
  // Effects for the host: nothing acts on them yet (#506 has no actions),
  // so drain them to keep the queue bounded, before the Snapshot whose
  // arrays the draw reads.
  for(BinnacleEffect effect; binnacle_grid_poll_effect(grid, &effect);) {}
  binnacle_grid_snapshot(grid, &draw->snap);
  // Next frame's bars: a Thumb stands for the scrolling Strip's share of the
  // content it scrolls through; the pinned Strips stay put.
  for EachEnumVal(Axis2, axis)
  {
    BinnacleAxisSnapshot *a = &draw->snap.axes[axis == Axis2_Y ? BINNACLE_ROWS : BINNACLE_COLS];
    UI_ScrollThumb thumb = {a->thumb.fraction, a->thumb.size};
    F32 body = Max(a->scrolling_end - a->scrolling_start, 0);
    if(thumb.fraction != state->thumb[axis].fraction) { state->scrolled += 1; }
    state->thumb[axis] = thumb;
    state->page_px[axis] = body;
    state->scrolling[axis] = r1f32(a->scrolling_start, a->scrolling_end);
    state->content_px[axis] = !(thumb.size > 0 && thumb.size < 1) ? 0 : a->extent - body + (F32)(body/thumb.size);
  }
  ui_box_equip_custom_draw(box, uishell_table_draw, draw);
  // A drag can autoscroll and a pending view fills next frame; a live
  // table only changes at its next tick.
  if(draw->snap.capturing || draw->snap.view_pending) { rd_request_frame(); }
  else if(wake != WHEELHOUSE_TABLE_NEVER) { rd_request_frame_at(now + wake); }
  if(uishell_table_screenshot_hook != 0)
  {
    uishell_table_screenshot_frames += 1;
    if(uishell_table_screenshot_frames == 10) { rd_state->frame_diagnostic = uishell_table_screenshot_hook; }
    rd_request_frame();
  }
  scratch_end(scratch);
}
