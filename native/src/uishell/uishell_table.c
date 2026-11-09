// The table View (#506): a binnacle Grid over a table the core owns. The
// core answers the Grid's source Effects and fills its Windows
// (wheelhouse_table_frame); this View forwards input to the Grid through
// binnacle.h and paints its Snapshot in one box, with no box per row or
// cell. Its expression names a canned table (#506) or is a described
// table's link, read live from its server (#529).

#include "binnacle.h"

// The cell text painter, Rich Text's (uishell_rich_text.c): cells laid out
// with parley, right-aligned ones in tabular figures (#624).
typedef struct UIShell_RtCellText UIShell_RtCellText;
internal UIShell_RtCellText *uishell_rt_cell_text_frame(UIShell_RtCellText *ct, F32 font_size);
internal void uishell_rt_cell_text_release(UIShell_RtCellText *ct);
internal void uishell_rt_table_cell_text(void *user, UI_Box *box, Rng2F32 cell, Rng2F32 clip, String8 text, B32 right, Vec4F32 color);
internal F32 uishell_rt_table_cell_width(UIShell_RtCellText *ct, String8 text, B32 right);

typedef struct UIShell_TableState UIShell_TableState;
struct UIShell_TableState
{
  WheelhouseTable *table;
  // The text line and digit width the table was last sized for: a zoom or
  // layout scale changes them, and the table follows.
  F32 line;
  F32 digit;
  // Its cell text's layouts, kept while the View lives.
  UIShell_RtCellText *cell_text;
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
  // Find, from the View's search row: its toggles, and which way the last
  // step went (1 next, -1 previous) for the wrap note.
  B32 find_case;
  B32 find_regex;
  B32 find_by_cols;
  S32 find_dir;
  // As of the last Snapshot: whether the source finds (BINNACLE_FIND* bits),
  // and whether a step's match is current.
  U32 find_support;
  B32 find_current;
  // The filter bar: the rows' quick filter's text and toggles, and whether
  // its field has the keys.
  U8 filter_buffer[1024];
  U64 filter_size;
  TxtPt filter_cursor;
  TxtPt filter_mark;
  B32 filter_case;
  B32 filter_regex;
  B32 filter_focused;
  // As of the last Snapshot: the rows' BINNACLE_QUICK_FILTER* bits, whether
  // the source left out the filter asked for, and their count and unfiltered
  // total for the bar.
  U32 filter_support;
  B32 filter_rejected;
  U64 rows_count;
  B32 rows_has_total;
  U64 rows_total;
};

// A column's starting fit stays within these, in digits, as a cells table's
// starting widths do (wheelhouse-tables' CELLS_MIN_DIGITS and
// CELLS_MAX_DIGITS); an autosize the user asks for is not held to them.
#define UISHELL_TABLE_FIT_MIN_DIGITS 4.f
#define UISHELL_TABLE_FIT_MAX_DIGITS 40.f

// Cells painted, counted by the table diagnostics.
global U64 uishell_table_cells_drawn;
// --table_screenshot: what writes the window once a table has painted a
// few frames (uishell_table_diagnostics.c sets it), and the frames painted.
global B32 (*uishell_table_screenshot_hook)(RD_WindowState *ws);
global U64 uishell_table_screenshot_frames;

internal void
uishell_table_close(UIShell_TableState *state)
{
  if(state->table) { wheelhouse_table_free(state->table); state->table = 0; }
}

internal void
uishell_table_release(void *user_data)
{
  UIShell_TableState *state = (UIShell_TableState *)user_data;
  uishell_table_close(state);
  uishell_rt_cell_text_release(state->cell_text);
  state->cell_text = 0;
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

// Paints a cell's or header's text in `cell`, clipped to `clip`.
typedef void UIShell_TableTextFunction(void *user, UI_Box *box, Rng2F32 cell, Rng2F32 clip, String8 text, B32 right, Vec4F32 color);

typedef struct UIShell_TableDraw UIShell_TableDraw;
struct UIShell_TableDraw
{
  WheelhouseTable *table;
  BinnacleSnapshot snap;
  // The text's painter: uishell_rt_table_cell_text, with its layouts.
  UIShell_TableTextFunction *draw_text;
  void *text_user;
  // Find is open: mark the cells that match, in match.
  B32 finding;
  Vec4F32 match;
};

internal Rng2F32
uishell_table_rect(Rng2F32 origin, BinnacleRect r)
{
  return r2f32p(origin.x0+r.x, origin.y0+r.y, origin.x0+r.x+r.width, origin.y0+r.y+r.height);
}

// What an empty table says in its body, as on the web: "No rows" once it
// has no rows or no columns, but nothing while its status line says why
// (connecting, loading, retrying, failed). Rows the filter leaves out are
// said to be.
internal String8
uishell_table_empty_message(BinnacleSnapshot *snap, String8 status)
{
  BinnacleAxisSnapshot *rows = &snap->axes[BINNACLE_ROWS];
  B32 empty = rows->count == 0 || snap->axes[BINNACLE_COLS].count == 0;
  if(!empty || status.size != 0) { return str8_zero(); }
  return rows->count == 0 && rows->has_total && rows->total != 0 ? str8_lit("No rows match the filter") : str8_lit("No rows");
}

// A header's label as the View draws it: a sorted item's with its arrow,
// and its place among several sorts.
internal String8
uishell_table_header_label(Arena *arena, WheelhouseTable *table, U32 axis, BinnacleHeader *h)
{
  String8 label = uishell_table_text(wheelhouse_table_header(table, axis, h->key, h->position));
  if(h->flags & BINNACLE_HEADER_SORTED)
  {
    String8 arrow = h->flags & BINNACLE_HEADER_DESCENDING ? str8_lit(" ▼") : str8_lit(" ▲");
    label = h->sort_place > 1 ? push_str8f(arena, "%S%S%u", label, arrow, h->sort_place)
                              : push_str8f(arena, "%S%S", label, arrow);
  }
  return label;
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
  UIShell_TableTextFunction *draw_text = d->draw_text;
  Rng2F32 body = r2f32p(o.x0+col_band, o.y0+row_band, o.x1, o.y1);

  // Which shown cells match the Find Query, row by row over the Strips.
  Temp scratch = scratch_begin(0, 0);
  U8 *marks = 0;
  if(d->finding && rows->strip_len != 0 && cols->strip_len != 0)
  {
    U64 *row_keys = push_array(scratch.arena, U64, rows->strip_len);
    U64 *col_keys = push_array(scratch.arena, U64, cols->strip_len);
    for(size_t r = 0; r < rows->strip_len; r += 1) { row_keys[r] = rows->strip[r].key; }
    for(size_t c = 0; c < cols->strip_len; c += 1) { col_keys[c] = cols->strip[c].key; }
    marks = push_array(scratch.arena, U8, rows->strip_len*cols->strip_len);
    wheelhouse_table_find_marks(d->table, row_keys, rows->strip_len, col_keys, cols->strip_len, marks);
  }
  // Matches in the theme's match colour; the current one in the accent.
  Vec4F32 match = d->match, current = accent;
  current.w *= 0.4f;

  // Body cells: each row item crossing each column item, clipped to the
  // Strips they draw in; a match washed under its text, the current one
  // more strongly.
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
      if(marks != 0 && marks[r*cols->strip_len + c])
      {
        B32 is_current = snap->has_find_match && snap->find_match.row == ri->key && snap->find_match.col == ci->key;
        dr_rect(intersect_2f32(cell, clip), is_current ? current : match, 0, 0, 1.f);
      }
      U32 align = 0;
      String8 text = uishell_table_text(wheelhouse_table_cell(d->table, ri->key, ci->key, &align));
      draw_text(d->text_user, box, cell, clip, text, align == WHEELHOUSE_TABLE_ALIGN_RIGHT, text_color);
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

  // The table ends at its last row and column, as on the web: past them
  // the View is empty, with no band or rule.
  F32 x_end = o.x0+col_band, y_end = o.y0+row_band;
  for(size_t c = 0; c < cols->strip_len; c += 1) { x_end = Max(x_end, o.x0+cols->strip[c].offset+cols->strip[c].size); }
  for(size_t r = 0; r < rows->strip_len; r += 1) { y_end = Max(y_end, o.y0+rows->strip[r].offset+rows->strip[r].size); }
  x_end = Min(x_end, o.x1);
  y_end = Min(y_end, o.y1);

  // Header bands, then the rules after each item across them and the body,
  // so header cells are divided as the body's are.
  dr_rect(r2f32p(o.x0, o.y0, x_end, o.y0+row_band), band, 0, 0, 0.f);
  dr_rect(r2f32p(o.x0, o.y0+row_band, o.x0+col_band, y_end), band, 0, 0, 0.f);
  for(size_t c = 0; c < cols->strip_len; c += 1)
  {
    BinnacleStripItem *ci = &cols->strip[c];
    Rng1F32 cx = uishell_table_strip_clip(cols, c);
    F32 x = o.x0+ci->offset+ci->size;
    if(x > o.x0+cx.min && x <= o.x0+cx.max)
    {
      dr_rect(r2f32p(x, o.y0, x+1.f, o.y0+row_band), header_line, 0, 0, 0.f);
      dr_rect(r2f32p(x, o.y0+row_band, x+1.f, y_end), line, 0, 0, 0.f);
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
      dr_rect(r2f32p(o.x0+col_band, y-1.f, x_end, y), line, 0, 0, 0.f);
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
      String8 label = uishell_table_header_label(scratch.arena, d->table, (U32)axis_idx, h);
      B32 pending = !!(h->flags & (BINNACLE_HEADER_PENDING|BINNACLE_HEADER_MOVE_PENDING|BINNACLE_HEADER_FILTER_PENDING));
      draw_text(d->text_user, box, rect, clip, label, axis_idx == BINNACLE_ROWS, pending ? weak : text_color);
      scratch_end(scratch);
    }
  }
  // The bands' edges on whole pixels, where the scroll bars beside them
  // start (ui_scroll_region_build_thumbs), so neither bar covers a rule.
  F32 band_y = round_f32(o.y0+row_band), band_x = round_f32(o.x0+col_band);
  dr_rect(r2f32p(o.x0, band_y-1.f, x_end, band_y), header_line, 0, 0, 0.f);
  dr_rect(r2f32p(band_x-1.f, o.y0, band_x, y_end), header_line, 0, 0, 0.f);

  // Focus, then a drag's drop line.
  if(snap->has_focus_rect)
  {
    DR_ClipScope(intersect_2f32(dr_top_clip(), snap->outline_focused ? o : body))
    {
      dr_rect(uishell_table_rect(o, snap->focus_rect), accent, 0, 1.5f, 1.f);
    }
  }
  // A resize's guide: where the resized item's moving edge is.
  if(snap->has_resize_guide)
  {
    F32 at = snap->resize_guide.offset;
    Rng2F32 guide = snap->resize_guide.axis == BINNACLE_COLS ? r2f32p(o.x0+at-1.f, o.y0, o.x0+at+1.f, o.y1)
                                                          : r2f32p(o.x0, o.y0+at-1.f, o.x1, o.y0+at+1.f);
    dr_rect(guide, accent, 0, 0, 0.f);
  }
  if(snap->has_drop)
  {
    Rng2F32 drop = snap->drop_axis == BINNACLE_COLS ? r2f32p(o.x0+snap->drop_offset-1.f, o.y0, o.x0+snap->drop_offset+1.f, o.y1)
                                                     : r2f32p(o.x0, o.y0+snap->drop_offset-1.f, o.x1, o.y0+snap->drop_offset+1.f);
    dr_rect(drop, accent, 0, 0, 0.f);
  }
  scratch_end(scratch);
}

// The width that shows column strip item c's header and the values of the
// rows in the Strip whole, as the View draws them; *complete is 0 while the
// column or any of those rows is still to come.
internal F32
uishell_table_fit_width(UIShell_TableState *state, BinnacleSnapshot *snap, size_t c, B32 *complete)
{
  BinnacleAxisSnapshot *rows = &snap->axes[BINNACLE_ROWS], *cols = &snap->axes[BINNACLE_COLS];
  BinnacleStripItem *ci = &cols->strip[c];
  *complete = !!ci->has_key;
  if(!ci->has_key) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  String8 label = uishell_table_header_label(scratch.arena, state->table, BINNACLE_COLS, &cols->headers[c]);
  F32 width = uishell_rt_table_cell_width(state->cell_text, label, 0);
  for(size_t r = 0; r < rows->strip_len; r += 1)
  {
    BinnacleStripItem *ri = &rows->strip[r];
    if(!ri->has_key) { *complete = 0; continue; }
    U32 align = 0;
    String8 text = uishell_table_text(wheelhouse_table_cell(state->table, ri->key, ci->key, &align));
    width = Max(width, uishell_rt_table_cell_width(state->cell_text, text, align == WHEELHOUSE_TABLE_ALIGN_RIGHT));
  }
  scratch_end(scratch);
  return width;
}

// Answers this frame's autosize requests (a double-click on a border or a
// band's edge), and gives each column shown that has no size yet its
// starting fit once the rows shown are all in, within the fit's limits: 1
// if any size changed.
internal B32
uishell_table_fit(UIShell_TableState *state, BinnacleGrid *grid, BinnacleSnapshot *snap, BinnacleEffect *asks, U64 asks_count)
{
  BinnacleAxisSnapshot *rows = &snap->axes[BINNACLE_ROWS], *cols = &snap->axes[BINNACLE_COLS];
  B32 changed = 0;
  for(U64 i = 0; i < asks_count; i += 1)
  {
    BinnacleEffect *ask = &asks[i];
    if(ask->kind == BINNACLE_EFFECT_AUTO_SIZE_REQUESTED && ask->axis == BINNACLE_ROWS)
    {
      // Rows hold one line of text: fitting one is going back to the
      // default height.
      binnacle_grid_clear_size(grid, BINNACLE_ROWS, ask->key);
      changed = 1;
    }
    else if(ask->kind == BINNACLE_EFFECT_AUTO_SIZE_REQUESTED)
    {
      for(size_t c = 0; c < cols->strip_len; c += 1)
      {
        if(!cols->strip[c].has_key || cols->strip[c].key != ask->key) { continue; }
        B32 complete = 0;
        binnacle_grid_set_size(grid, BINNACLE_COLS, ask->key, uishell_table_fit_width(state, snap, c, &complete));
        changed = 1;
        break;
      }
    }
    else if(ask->kind == BINNACLE_EFFECT_BAND_AUTO_SIZE_REQUESTED && ask->axis == BINNACLE_ROWS)
    {
      // The column headers are a line of text, as they start.
      binnacle_grid_set_header(grid, BINNACLE_ROWS, state->line);
      changed = 1;
    }
    else if(ask->kind == BINNACLE_EFFECT_BAND_AUTO_SIZE_REQUESTED)
    {
      // The row numbers: the labels of the rows shown, whole.
      Temp scratch = scratch_begin(0, 0);
      F32 width = 0;
      for(size_t r = 0; r < rows->headers_len; r += 1)
      {
        if(!rows->headers[r].has_key) { continue; }
        String8 label = uishell_table_header_label(scratch.arena, state->table, BINNACLE_ROWS, &rows->headers[r]);
        width = Max(width, uishell_rt_table_cell_width(state->cell_text, label, 1));
      }
      scratch_end(scratch);
      if(width > 0) { binnacle_grid_set_header(grid, BINNACLE_COLS, width); changed = 1; }
    }
  }
  // Starting fits, for a table that brings no widths, wait for rows to fit
  // to, and leave alone a column that has a size: its fit, or a resize.
  if(rows->strip_len != 0 && wheelhouse_table_fits_columns(state->table))
  {
    BinnacleSizeOverrides sized = {0};
    binnacle_grid_size_overrides(grid, BINNACLE_COLS, &sized);
    for(size_t c = 0; c < cols->strip_len; c += 1)
    {
      BinnacleStripItem *ci = &cols->strip[c];
      if(!ci->has_key) { continue; }
      // Overrides come in Key order.
      size_t lo = 0, hi = sized.len;
      while(lo < hi)
      {
        size_t mid = lo + (hi - lo)/2;
        if(sized.keys[mid] < ci->key) { lo = mid + 1; } else { hi = mid; }
      }
      if(lo < sized.len && sized.keys[lo] == ci->key) { continue; }
      B32 complete = 0;
      F32 width = uishell_table_fit_width(state, snap, c, &complete);
      if(!complete) { continue; }
      width = Clamp(UISHELL_TABLE_FIT_MIN_DIGITS*state->digit, width, UISHELL_TABLE_FIT_MAX_DIGITS*state->digit);
      binnacle_grid_set_size(grid, BINNACLE_COLS, ci->key, ceil_f32(width));
      changed = 1;
    }
  }
  return changed;
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

// What the find bar says of the matches: "3 of 120" once a step has landed
// on a current match, else "120 matches"; "No matches"; "+" on a partial
// count, and how many lie in collapsed groups. Empty while find is closed.
internal String8
uishell_table_find_count(Arena *arena, WheelhouseTableFind find, B32 current)
{
  if(!find.open) { return str8_zero(); }
  if(find.total == 0 && !find.partial) { return str8_lit("No matches"); }
  String8 total = push_str8f(arena, "%S%s", str8_from_u64(arena, find.total, 10, 0, ','), find.partial ? "+" : "");
  String8 hidden = find.hidden == 0 ? str8_zero() :
                   push_str8f(arena, ", %S in collapsed groups", str8_from_u64(arena, find.hidden, 10, 0, ','));
  if(find.stepped && find.has_index && current)
  {
    return push_str8f(arena, "%S of %S%S", str8_from_u64(arena, find.index+1, 10, 0, ','), total, hidden);
  }
  return push_str8f(arena, "%S %s%S", total, find.total == 1 && !find.partial ? "match" : "matches", hidden);
}

// Whether the View's query row is a search, whose text is the table's
// Find Query.
internal B32
uishell_table_searching(RD_ViewState *vs)
{
  String8 cmd = rd_view_query_cmd();
  return vs->query_is_open && (str8_match(cmd, str8_lit("search"), 0) || str8_match(cmd, str8_lit("search_backwards"), 0));
}

// One find step, Next (1) or Previous (-1), noted for the wrap note.
internal void
uishell_table_find_step(UIShell_TableState *state, S32 dir)
{
  BinnacleAction a = {0};
  a.kind = dir > 0 ? BINNACLE_ACTION_FIND_NEXT : BINNACLE_ACTION_FIND_PREVIOUS;
  uishell_table_act(wheelhouse_table_grid(state->table), a);
  state->find_dir = dir;
  rd_request_frame();
}

// "Show only matching rows": the find text, matching as find does, becomes
// the rows' quick filter.
internal void
uishell_table_filter_from_find(UIShell_TableState *state, String8 text)
{
  state->filter_size = Min(text.size, sizeof(state->filter_buffer));
  // Cut on a character, not inside one.
  while(state->filter_size < text.size && state->filter_size > 0 && (text.str[state->filter_size] & 0xC0) == 0x80) { state->filter_size -= 1; }
  MemoryCopy(state->filter_buffer, text.str, state->filter_size);
  state->filter_cursor = state->filter_mark = txt_pt(1, state->filter_size+1);
  state->filter_case = state->find_case;
  state->filter_regex = state->find_regex;
  rd_request_frame();
}

// The table's part of its search row: the count, a note when a step wraps,
// matching case, regexes where the source takes them, the order steps go
// in, and Previous and Next.
internal void
uishell_table_query_extras(void *user_data)
{
  UIShell_TableState *state = (UIShell_TableState *)user_data;
  if(state->table == 0 || !uishell_table_searching(rd_view_state_from_view(uishell_regs()->view, 0))) { return; }
  Temp scratch = scratch_begin(0, 0);
  // The buttons are for the mouse: Enter and Escape stay the field's.
  UI_PrefWidth(ui_text_dim(10, 1)) RD_Font(RD_FontSlot_Main) UI_Focus(UI_FocusKind_Off)
  {
    if(!(state->find_support & BINNACLE_FIND))
    {
      UI_TagF("weak") ui_labelf("This table has no find");
    }
    else
    {
      WheelhouseTableFind find = wheelhouse_table_find(state->table);
      String8 count = uishell_table_find_count(scratch.arena, find, state->find_current);
      ui_spacer(ui_em(0.5f, 1.f));
      if(count.size != 0)
      {
        if(find.total == 0 && !find.partial) UI_TagF("bad") { ui_label(count); }
        else UI_TagF("weak") { ui_label(count); }
      }
      if(find.open && find.stepped && find.wrapped)
      {
        UI_TagF("weak") ui_label(state->find_dir < 0 ? str8_lit("Wrapped to the end") : str8_lit("Wrapped to the start"));
      }
      ui_spacer(ui_em(0.5f, 1.f));
      UI_TagF(state->find_case ? "pop" : "")
      {
        UI_Signal sig = ui_buttonf("Aa###find_case");
        if(ui_hovering(sig)) UI_Tooltip { ui_labelf("Match case"); }
        if(ui_clicked(sig)) { state->find_case = !state->find_case; rd_request_frame(); }
      }
      if(state->find_support & BINNACLE_FIND_REGEX) UI_TagF(state->find_regex ? "pop" : "")
      {
        UI_Signal sig = ui_buttonf(".*###find_regex");
        if(ui_hovering(sig)) UI_Tooltip { ui_labelf("Use regular expression"); }
        if(ui_clicked(sig)) { state->find_regex = !state->find_regex; rd_request_frame(); }
      }
      {
        UI_Signal sig = ui_buttonf("%s###find_by", state->find_by_cols ? "By columns" : "By rows");
        if(ui_hovering(sig)) UI_Tooltip { ui_labelf("The order find steps in"); }
        if(ui_clicked(sig)) { state->find_by_cols = !state->find_by_cols; rd_request_frame(); }
      }
      if(state->filter_support & BINNACLE_QUICK_FILTER)
      {
        UI_Signal sig = ui_buttonf("Filter###find_filter");
        if(ui_hovering(sig)) UI_Tooltip { ui_labelf("Show only matching rows"); }
        if(ui_clicked(sig)) { uishell_table_filter_from_find(state, rd_view_query_input()); }
      }
      RD_Font(RD_FontSlot_Icons)
      {
        UI_Signal previous = ui_buttonf("%S###find_previous", rd_icon_kind_text_table[RD_IconKind_UpArrow]);
        if(ui_hovering(previous)) UI_Tooltip RD_Font(RD_FontSlot_Main) { ui_labelf("Previous match (Shift+Enter)"); }
        if(ui_clicked(previous)) { uishell_table_find_step(state, -1); }
        UI_Signal next = ui_buttonf("%S###find_next", rd_icon_kind_text_table[RD_IconKind_DownArrow]);
        if(ui_hovering(next)) UI_Tooltip RD_Font(RD_FontSlot_Main) { ui_labelf("Next match (Enter)"); }
        if(ui_clicked(next)) { uishell_table_find_step(state, 1); }
      }
      ui_spacer(ui_em(0.5f, 1.f));
    }
  }
  scratch_end(scratch);
}

// What a table View's tab says in place of its expression: a described
// table's table name, not its link (#577); a canned table's own name.
internal String8
uishell_table_title(Arena *arena, String8 expr)
{
  U64 cap = expr.size;
  U8 *out = push_array_no_zero(arena, U8, cap);
  U64 size = wheelhouse_table_title((WheelhouseText){expr.str, expr.size}, out, cap);
  return str8(out, size);
}

// What the filter bar says of the rows: "1,204 of 5,001" while a filter
// leaves some out, else "5,001 rows".
internal String8
uishell_table_row_count(Arena *arena, U64 count, B32 has_total, U64 total)
{
  String8 shown = str8_from_u64(arena, count, 10, 0, ',');
  if(has_total && total != count)
  {
    return push_str8f(arena, "%S of %S", shown, str8_from_u64(arena, total, 10, 0, ','));
  }
  return push_str8f(arena, "%S %s", shown, count == 1 ? "row" : "rows");
}

// The filter bar (#535), where the rows take a quick filter: its field,
// matching case, regexes where the source takes them, a clear button while
// there is text, "Not applied" while the source leaves the filter out, and
// the count. Escape in the field clears it, or with nothing to clear hands
// the keys back to the Grid, as Enter does.
internal void
uishell_table_filter_bar(UIShell_TableState *state, F32 width, F32 height)
{
  Temp scratch = scratch_begin(0, 0);
  UI_Box *bar;
  UI_PrefWidth(ui_px(width, 1)) UI_PrefHeight(ui_px(height, 1)) UI_ChildLayoutAxis(Axis2_X)
  {
    bar = ui_build_box_from_string(UI_BoxFlag_DrawSideBottom, str8_lit("table_filter_bar"));
  }
  UI_Parent(bar) UI_PrefHeight(ui_pct(1, 1)) RD_Font(RD_FontSlot_Main)
  {
    ui_spacer(ui_em(0.5f, 1.f));
    UI_Focus(state->filter_focused ? UI_FocusKind_On : UI_FocusKind_Off)
    {
      if(ui_is_focus_active() && ui_slot_press(UI_EventActionSlot_Cancel))
      {
        if(state->filter_size != 0) { state->filter_size = 0; state->filter_cursor = state->filter_mark = txt_pt(1, 1); }
        else { state->filter_focused = 0; }
      }
      if(ui_is_focus_active() && ui_slot_press(UI_EventActionSlot_Accept)) { state->filter_focused = 0; }
      RD_CellParams params = {0};
      params.flags = RD_CellFlag_Border;
      params.cursor = &state->filter_cursor;
      params.mark = &state->filter_mark;
      params.edit_buffer = state->filter_buffer;
      params.edit_buffer_size = sizeof(state->filter_buffer);
      params.edit_string_size_out = &state->filter_size;
      params.pre_edit_value = str8(state->filter_buffer, state->filter_size);
      // Empty, it says what it is for.
      if(state->filter_size == 0) UI_TagF("weak")
      {
        DR_FStrParams fparams = {ui_top_font(), ui_top_text_raster_flags(), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
        dr_fstrs_push_new(scratch.arena, &params.value_fstrs, &fparams, str8_lit("Filter rows…"));
      }
      UI_Signal sig;
      UI_PrefWidth(ui_em(20, 1)) { sig = rd_cellf(&params, "###table_filter"); }
      if(ui_pressed(sig))
      {
        state->filter_focused = 1;
        uishell_cmd("focus_panel");
      }
    }
    // The buttons are for the mouse: Enter and Escape stay the field's.
    UI_PrefWidth(ui_text_dim(10, 1)) UI_Focus(UI_FocusKind_Off)
    {
      UI_TagF(state->filter_case ? "pop" : "")
      {
        UI_Signal sig = ui_buttonf("Aa###filter_case");
        if(ui_hovering(sig)) UI_Tooltip { ui_labelf("Match case"); }
        if(ui_clicked(sig)) { state->filter_case = !state->filter_case; rd_request_frame(); }
      }
      if(state->filter_support & BINNACLE_QUICK_FILTER_REGEX) UI_TagF(state->filter_regex ? "pop" : "")
      {
        UI_Signal sig = ui_buttonf(".*###filter_regex");
        if(ui_hovering(sig)) UI_Tooltip { ui_labelf("Use regular expression"); }
        if(ui_clicked(sig)) { state->filter_regex = !state->filter_regex; rd_request_frame(); }
      }
      if(state->filter_size != 0) RD_Font(RD_FontSlot_Icons)
      {
        UI_Signal sig = ui_buttonf("%S###filter_clear", rd_icon_kind_text_table[RD_IconKind_X]);
        if(ui_hovering(sig)) UI_Tooltip RD_Font(RD_FontSlot_Main) { ui_labelf("Clear filter"); }
        if(ui_clicked(sig))
        {
          state->filter_size = 0;
          state->filter_cursor = state->filter_mark = txt_pt(1, 1);
          state->filter_focused = 1;
          uishell_cmd("focus_panel");
          rd_request_frame();
        }
      }
      ui_spacer(ui_em(0.5f, 1.f));
      if(state->filter_rejected) UI_TagF("bad") { ui_labelf("Not applied"); }
      UI_TagF("weak") ui_label(uishell_table_row_count(scratch.arena, state->rows_count, state->rows_has_total, state->rows_total));
    }
  }
  scratch_end(scratch);
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
    uishell_table_close(state);
  }
  RD_ViewState *vs = rd_view_state_from_view(ctx->settings.view, 0);
  vs->release_user_data = uishell_table_release;
  vs->query_extras = uishell_table_query_extras;
  if(!state->table && name.size != 0 && name.size <= sizeof(state->name) &&
     !str8_match(name, str8(state->failed, state->failed_size), 0))
  {
    state->abi_mismatch = binnacle_abi_version() != BINNACLE_ABI_VERSION;
    if(!state->abi_mismatch)
    {
      state->table = uishell_table_open(name, line, digit);
      state->line = line;
      state->digit = digit;
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

  if(line != state->line || digit != state->digit)
  {
    wheelhouse_table_set_metrics(state->table, line, digit);
    state->line = line;
    state->digit = digit;
  }

  BinnacleGrid *grid = wheelhouse_table_grid(state->table);
  // Find follows the View's search row: its text, with the row's toggles, is
  // the Find Query while it is open, and closing it closes find. Enter (the
  // search command), F3 and the row's buttons step; Shift+Enter, Shift+F3
  // and Cmd+F3 step back.
  B32 finding = uishell_table_searching(vs) && (state->find_support & BINNACLE_FIND);
  String8 find_text = finding ? rd_view_query_input() : str8_zero();
  if(find_text.size != 0)
  {
    BinnacleFindQuery query = {0};
    query.query.text = (BinnacleText){find_text.str, find_text.size};
    query.query.flags = (state->find_case ? BINNACLE_TEXT_CASE_SENSITIVE : 0) |
                        (state->find_regex && (state->find_support & BINNACLE_FIND_REGEX) ? BINNACLE_TEXT_REGEX : 0);
    query.by = state->find_by_cols ? BINNACLE_COLS : BINNACLE_ROWS;
    binnacle_grid_set_find(grid, &query);
  }
  else
  {
    binnacle_grid_set_find(grid, 0);
  }
  for(UIShell_Cmd *cmd = 0; uishell_next_view_cmd(&cmd);)
  {
    if(str8_match(cmd->name, str8_lit("search"), 0) || str8_match(cmd->name, str8_lit("find_next"), 0))
    {
      uishell_table_find_step(state, 1);
    }
    else if(str8_match(cmd->name, str8_lit("search_backwards"), 0) || str8_match(cmd->name, str8_lit("find_prev"), 0))
    {
      uishell_table_find_step(state, -1);
    }
  }
  // The filter bar's text, with its toggles, is the rows' quick filter.
  // It has the keys until the Grid or the search row takes them.
  B32 row_focused = vs->query_is_open && !vs->contents_are_focused;
  if(row_focused) { state->filter_focused = 0; }
  F32 bar_h = 0;
  if(state->filter_support & BINNACLE_QUICK_FILTER)
  {
    bar_h = line;
    UI_Parent(root) { uishell_table_filter_bar(state, dim.x, bar_h); }
    BinnacleTextQuery quick = {0};
    quick.text = (BinnacleText){state->filter_buffer, state->filter_size};
    quick.flags = (state->filter_case ? BINNACLE_TEXT_CASE_SENSITIVE : 0) |
                  (state->filter_regex && (state->filter_support & BINNACLE_QUICK_FILTER_REGEX) ? BINNACLE_TEXT_REGEX : 0);
    quick.searched = BINNACLE_SEARCHED_SHOWN;
    binnacle_grid_set_quick_filter(grid, BINNACLE_ROWS, &quick);
  }
  // A described table's state (connecting, loading, retrying, failed, partly shown)
  // takes a line above the Grid, as of the last frame.
  String8 status = push_str8_copy(scratch.arena, uishell_table_text(wheelhouse_table_status(state->table)));
  F32 status_h = status.size != 0 ? line : 0;
  if(status.size != 0) UI_Parent(root) UI_PrefWidth(ui_px(dim.x, 1)) UI_PrefHeight(ui_px(status_h, 1))
  {
    ui_label(status);
  }
  F32 top = bar_h + status_h;
  // The Grid in a scroll region, with either style's bars, as other Views
  // have. Its Thumbs are estimates in no units, so the bars take them as
  // they are, from the last Snapshot.
  UI_ScrollRegionParams params = ui_scroll_region_params(r2f32p(0, top, dim.x, Max(dim.y, top)),
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
    state->filter_focused = 0;
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
  // Keys go to the Grid unless its query row has them (Enter there steps
  // find, and Escape closes it) or its filter bar does.
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active() && !row_focused && !state->filter_focused)
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
  state->cell_text = uishell_rt_cell_text_frame(state->cell_text, ui_top_font_size());
  draw->draw_text = uishell_rt_table_cell_text;
  draw->text_user = state->cell_text;
  draw->finding = find_text.size != 0;
  // Matches in the theme's match colour, its "match background"; a
  // theme without one marks them in its selection colour.
  UI_TagF("match") { draw->match = ui_color_from_name(str8_lit("background")); }
  if(draw->match.w == 0) { draw->match = ui_color_from_name(str8_lit("selection")); }
  // Effects for the host: autosize requests, answered from the cells the
  // Snapshot shows. Nothing acts on the rest yet (#506 has no actions), so
  // they are drained to keep the queue bounded.
  BinnacleEffect asks[16];
  U64 asks_count = 0;
  for(BinnacleEffect effect; binnacle_grid_poll_effect(grid, &effect);)
  {
    if((effect.kind == BINNACLE_EFFECT_AUTO_SIZE_REQUESTED || effect.kind == BINNACLE_EFFECT_BAND_AUTO_SIZE_REQUESTED) &&
       asks_count < ArrayCount(asks))
    {
      asks[asks_count] = effect;
      asks_count += 1;
    }
  }
  binnacle_grid_snapshot(grid, &draw->snap);
  // Sizes fitted now are drawn now.
  if(uishell_table_fit(state, grid, &draw->snap, asks, asks_count)) { binnacle_grid_snapshot(grid, &draw->snap); }
  switch(draw->snap.cursor)
  {
    case BINNACLE_CURSOR_RESIZE_COLS:{ box->hover_cursor = WM_Cursor_LeftRight; }break;
    case BINNACLE_CURSOR_RESIZE_ROWS:{ box->hover_cursor = WM_Cursor_UpDown; }break;
    case BINNACLE_CURSOR_MOVE:{ box->hover_cursor = WM_Cursor_HandPoint; }break;
    default:{ box->hover_cursor = WM_Cursor_Pointer; }break;
  }
  state->find_support = draw->snap.find_support;
  state->find_current = draw->snap.has_find_match;
  // The bar, laid out above, shows these as of the last frame: a change
  // shows next frame.
  {
    BinnacleAxisSnapshot *rows = &draw->snap.axes[BINNACLE_ROWS];
    B32 rejected = !wheelhouse_table_quick_filter_applied(state->table);
    if(rows->quick_filter != state->filter_support || rejected != state->filter_rejected || rows->count != state->rows_count ||
       !!rows->has_total != state->rows_has_total || rows->total != state->rows_total)
    {
      rd_request_frame();
    }
    state->filter_support = rows->quick_filter;
    state->filter_rejected = rejected;
    state->rows_count = rows->count;
    state->rows_has_total = !!rows->has_total;
    state->rows_total = rows->total;
  }
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
  String8 empty = uishell_table_empty_message(&draw->snap, uishell_table_text(wheelhouse_table_status(state->table)));
  if(empty.size != 0) UI_Parent(box) UI_TagF("weak") UI_TextAlignment(UI_TextAlign_Center)
  {
    F32 left = draw->snap.axes[BINNACLE_COLS].header, top = draw->snap.axes[BINNACLE_ROWS].header;
    ui_set_next_fixed_x(left);
    ui_set_next_fixed_y(top);
    ui_set_next_fixed_width(Max(grid_dim.x - left, 0));
    ui_set_next_fixed_height(Max(grid_dim.y - top, 0));
    UI_Box *message = ui_build_box_from_string(UI_BoxFlag_DrawText, str8_lit("table_empty"));
    ui_box_equip_display_string(message, empty);
  }
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
