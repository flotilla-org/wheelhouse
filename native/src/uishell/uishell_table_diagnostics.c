// Table View checks (#506): the core's canned tables driven through the
// real binnacle Grid as the View drives it, then the View itself built and
// painted. Prints TABLE_BENCH lines for each table's frame cost.

#define TableCheck(condition, label) do { if(!(condition)) { fprintf(stderr, "FAIL table: %s\n", label); failures++; } } while(0)

internal BinnacleSnapshot
uishell_table_diag_frame(WheelhouseTable *table, F32 width, F32 height, U64 now_us)
{
  BinnacleSnapshot snap = {0};
  wheelhouse_table_frame(table, width, height, now_us);
  BinnacleGrid *grid = wheelhouse_table_grid(table);
  for(BinnacleEffect effect; binnacle_grid_poll_effect(grid, &effect);) {}
  binnacle_grid_snapshot(grid, &snap);
  return snap;
}

internal void
uishell_table_diag_click(BinnacleGrid *grid, F32 x, F32 y)
{
  binnacle_grid_pointer(grid, (BinnaclePointerEvent){BINNACLE_POINTER_DOWN, x, y, BINNACLE_BUTTON_PRIMARY, 0, 1});
  binnacle_grid_pointer(grid, (BinnaclePointerEvent){BINNACLE_POINTER_UP, x, y, BINNACLE_BUTTON_PRIMARY, 0, 0});
}

// Whether the drawn item at `position` on `axis` is selected whole.
internal B32
uishell_table_diag_selected(BinnacleSnapshot *snap, U32 axis, U64 position)
{
  BinnacleAxisSnapshot *a = &snap->axes[axis];
  for(size_t i = 0; i < a->strip_len; i += 1)
  {
    if(a->strip[i].position == position) { return !!(a->headers[i].flags & BINNACLE_HEADER_SELECTED); }
  }
  return 0;
}

// A key as the window hands it to the View: a navigation command's event,
// or a press or typed text with modifiers.
internal UI_Event
uishell_table_diag_nav(S32 x, S32 y, UI_EventDeltaUnit unit)
{
  return (UI_Event){.kind = UI_EventKind_Navigate, .delta_unit = unit, .delta_2s32 = v2s32(x, y)};
}

internal String8
uishell_table_diag_cell(Arena *arena, WheelhouseTable *table, U64 row, U64 col)
{
  return push_str8_copy(arena, uishell_table_text(wheelhouse_table_cell(table, row, col, 0)));
}

internal B32
uishell_table_diag_filled(BinnacleSnapshot *snap)
{
  B32 filled = snap->axes[0].strip_len > 0 && snap->axes[1].strip_len > 0;
  for EachIndex(a, 2) for(size_t i = 0; i < snap->axes[a].strip_len; i += 1) { filled &= snap->axes[a].strip[i].has_key; }
  return filled;
}

// Frame cost of a table: 2000 frames of fill, Snapshot and a one-row scroll.
internal F64
uishell_table_diag_bench(String8 name, F32 line, F32 digit)
{
  WheelhouseTable *table = wheelhouse_table_open((WheelhouseText){name.str, name.size}, line, digit, 0, 0);
  BinnacleGrid *grid = wheelhouse_table_grid(table);
  uishell_table_diag_frame(table, 1200, 800, 0);
  U64 frames = 2000, begin = now_time_us();
  for(U64 i = 0; i < frames; i += 1)
  {
    BinnacleAction a = {sizeof(a), BINNACLE_ACTION_SCROLL};
    a.args.scroll = (BinnacleScroll){0, (i/50)%2 ? -line : line};
    binnacle_grid_action(grid, &a);
    uishell_table_diag_frame(table, 1200, 800, 0);
  }
  F64 us = (F64)(now_time_us()-begin)/frames;
  printf("TABLE_BENCH table=%.*s frame_us=%.1f\n", str8_varg(name), us);
  wheelhouse_table_free(table);
  return us;
}

// The font size uishell_table_diag_view_frame builds the View at, as a
// zoom sets it.
global F32 uishell_table_diag_font_size = 12;

// One frame of the table View at 900x600 in `style`, with `event` at
// `mouse`. Returns the grid box (the scroll region's content, so its key is
// the region's); `part` gets the vertical bar's part named by `which`.
typedef enum { UIShell_TableDiagBar_Thumb, UIShell_TableDiagBar_Before, UIShell_TableDiagBar_After, UIShell_TableDiagBar_Down } UIShell_TableDiagBar;
internal UI_Box *
uishell_table_diag_view_frame(RD_WindowState *ws, CFG_Node *view, UI_ScrollBarStyle style, UI_Event event, Vec2F32 mouse,
                              UIShell_TableDiagBar which, UI_Box **part)
{
  UI_EventNode node = {.v = event};
  UI_EventList events = {0};
  if(event.kind != UI_EventKind_Null) { events.first = events.last = &node; events.count = 1; }
  UI_AnimationInfo animation = {0};
  animation.scroll_animation_rate = animation.hot_animation_rate = 1.f;
  ui_set_active_scroll_bar_style(style);
  ui_begin_build(ws->os, &events, &ws->ui->icon_info, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = mouse;
  // Overlay bars show only once scrolled, or always where reserved.
  ui_state->scroll_bars_reserved = 1;
  UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(uishell_table_diag_font_size)
  {
    wh_view_ui_direct(wh_renderer_from_name(str8_lit("table")), rd_view_address_from_cfg(view), r2f32p(0, 0, 900, 600));
  }
  ui_end_build();
  UI_Box *grid_box = &ui_nil_box;
  for(UI_Box *box = ui_state->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, ui_state->root).next)
  {
    if(box->custom_draw == uishell_table_draw) { grid_box = box; }
  }
  *part = &ui_nil_box;
  if(!ui_box_is_nil(grid_box))
  {
    UI_Key bar_key = ui_key_from_stringf(grid_box->key, "scroll_region_bar_%i", Axis2_Y);
    UI_Key track_key = ui_key_from_stringf(bar_key, "##_scroll_area_%i", Axis2_Y);
    UI_Key key = which == UIShell_TableDiagBar_Thumb ? ui_key_from_stringf(track_key, "##_scroller_%i", Axis2_Y) :
      which == UIShell_TableDiagBar_Before ? ui_key_from_string(track_key, str8_lit("##scroll_area_before")) :
      which == UIShell_TableDiagBar_After ? ui_key_from_string(track_key, str8_lit("##scroll_area_after")) :
      ui_key_from_stringf(bar_key, "%S##_max_scroll_%i", ui_icon_string_from_kind(UI_IconKind_DownArrow), Axis2_Y);
    UI_Box *box = ui_box_from_key(key);
    if(!ui_box_is_nil(box) && box->last_touched_build_index+1 >= ui_state->build_index) { *part = box; }
  }
  return grid_box;
}

// Clicks the vertical bar's part `which` and returns the grid box of the
// frame after, whose draw data is that frame's (nil: the part isn't there).
internal UI_Box *
uishell_table_diag_click_bar(RD_WindowState *ws, CFG_Node *view, UI_ScrollBarStyle style, UIShell_TableDiagBar which, UI_Box **part)
{
  UI_Event none = {0};
  Vec2F32 away = v2f32(450, 300);
  uishell_table_diag_view_frame(ws, view, style, none, away, which, part);
  if(ui_box_is_nil(*part)) { return &ui_nil_box; }
  Vec2F32 at = center_2f32((*part)->rect);
  UI_Event press = {.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = at};
  UI_Event release = {.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = at};
  uishell_table_diag_view_frame(ws, view, style, press, at, which, part);
  uishell_table_diag_view_frame(ws, view, style, release, at, which, part);
  return uishell_table_diag_view_frame(ws, view, style, none, away, which, part);
}

// Whether the rows moved a page from `from` towards `dir` (-1 up, +1 down):
// the scrolling Strip's length in rows, give or take one, measured in `d`.
internal B32
uishell_table_diag_paged(UIShell_TableDraw *d, U64 from, S64 dir)
{
  BinnacleAxisSnapshot *rows = &d->snap.axes[BINNACLE_ROWS];
  if(rows->strip_len < 2) { return 0; }
  F32 row_size = rows->strip[1].size > 0 ? rows->strip[1].size : 1;
  S64 page_rows = (S64)((rows->scrolling_end - rows->scrolling_start)/row_size);
  S64 paged = dir*((S64)rows->anchor.position - (S64)from);
  return paged > 0 && paged + 1 >= page_rows && paged <= page_rows + 1;
}

internal B32
uishell_table_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  U32 failures = 0;
  F32 line = 20.f, digit = 8.f;
  TableCheck(binnacle_abi_version() == BINNACLE_ABI_VERSION, "the library's binnacle ABI is the header's");
  TableCheck(wheelhouse_table_open((WheelhouseText){(U8 *)"nope", 4}, line, digit, 0, 0) == 0, "an unknown table opens nothing");

  // Every canned table fills its first frame, with headers named.
  for(size_t i = 0; i < wheelhouse_table_count(); i += 1)
  {
    WheelhouseTable *table = wheelhouse_table_open(wheelhouse_table_name(i), line, digit, 0, 0);
    BinnacleSnapshot snap = uishell_table_diag_frame(table, 800, 600, 0);
    TableCheck(table != 0 && uishell_table_diag_filled(&snap), "a canned table fills its first frame");
    BinnacleAxisSnapshot *cols = &snap.axes[BINNACLE_COLS];
    B32 named = cols->headers_len > 0;
    for(size_t c = 0; c < cols->headers_len; c += 1)
    {
      named &= wheelhouse_table_header(table, BINNACLE_COLS, cols->headers[c].key, cols->headers[c].position).len > 0;
    }
    TableCheck(named, "every column header has a name");
    wheelhouse_table_free(table);
  }

  // Records: a header click sorts the column; a cell click focuses it, and
  // the arrow keys' action moves focus.
  {
    WheelhouseTable *table = wheelhouse_table_open((WheelhouseText){(U8 *)"records", 7}, line, digit, 0, 0);
    BinnacleGrid *grid = wheelhouse_table_grid(table);
    BinnacleSnapshot snap = uishell_table_diag_frame(table, 1200, 800, 0);
    BinnacleAxisSnapshot *cols = &snap.axes[BINNACLE_COLS];
    BinnacleRect crew = {0};
    U64 crew_index = 0;
    for(size_t c = 0; c < cols->headers_len; c += 1)
    {
      if(cols->headers[c].key == 3) { crew = cols->headers[c].rect; crew_index = c; }
    }
    TableCheck(crew.width > 0, "the crew column's header is shown");
    U32 id_align = 9, name_align = 9;
    wheelhouse_table_cell(table, 1, 0, &id_align);
    wheelhouse_table_cell(table, 1, 1, &name_align);
    TableCheck(id_align == WHEELHOUSE_TABLE_ALIGN_RIGHT && name_align == WHEELHOUSE_TABLE_ALIGN_LEFT, "numbers align right and text left");
    uishell_table_diag_click(grid, crew.x+crew.width/2, crew.y+crew.height/2);
    snap = uishell_table_diag_frame(table, 1200, 800, 0);
    TableCheck(snap.axes[BINNACLE_COLS].headers[crew_index].flags & BINNACLE_HEADER_SORTED, "a header click sorts its column");
    BinnacleAxisSnapshot *rows = &snap.axes[BINNACLE_ROWS];
    B32 ascending = rows->strip_len > 1;
    S64 last = -1;
    for(size_t r = 0; r < rows->strip_len; r += 1)
    {
      String8 text = uishell_table_diag_cell(scratch.arena, table, rows->strip[r].key, 3);
      S64 value = (S64)u64_from_str8(text, 10);
      ascending &= value >= last;
      last = value;
    }
    TableCheck(ascending, "the sorted column's values ascend down the rows");
    BinnacleStripItem *third = &rows->strip[2];
    BinnacleStripItem *name_col = &snap.axes[BINNACLE_COLS].strip[1];
    uishell_table_diag_click(grid, name_col->offset+name_col->size/2, third->offset+third->size/2);
    snap = uishell_table_diag_frame(table, 1200, 800, 0);
    U64 focused = snap.axes[BINNACLE_ROWS].focus.position;
    TableCheck(snap.has_focus && focused == third->position, "a cell click focuses its row");
    BinnacleAction down = {sizeof(down), BINNACLE_ACTION_MOVE_FOCUS};
    down.args.move_focus = (BinnacleMoveFocus){BINNACLE_ROWS, BINNACLE_FOCUS_ITEM, 1, 0};
    binnacle_grid_action(grid, &down);
    snap = uishell_table_diag_frame(table, 1200, 800, 0);
    TableCheck(snap.axes[BINNACLE_ROWS].focus.position == focused+1, "moving focus down moves it one row");

    // The View's keys, as binnacle's web host binds them.
    U64 row = snap.axes[BINNACLE_ROWS].focus.position, row_count = snap.axes[BINNACLE_ROWS].count;
    UI_Event evt = uishell_table_diag_nav(0, 1, UI_EventDeltaUnit_Word);
    B32 taken = uishell_table_key(grid, &evt);
    snap = uishell_table_diag_frame(table, 1200, 800, 0);
    TableCheck(taken && snap.axes[BINNACLE_ROWS].focus.position == row_count-1, "Cmd+Down goes to the last row");
    evt = uishell_table_diag_nav(1, 0, UI_EventDeltaUnit_Word);
    uishell_table_key(grid, &evt);
    snap = uishell_table_diag_frame(table, 1200, 800, 0);
    U64 last_col = snap.axes[BINNACLE_COLS].focus.position;
    TableCheck(last_col == snap.axes[BINNACLE_COLS].count-1, "Cmd+Right goes to the last column");
    evt = (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_PageUp, .modifiers = WM_Modifier_Alt};
    taken = uishell_table_key(grid, &evt);
    snap = uishell_table_diag_frame(table, 1200, 800, 0);
    TableCheck(taken && snap.axes[BINNACLE_COLS].focus.position < last_col && snap.axes[BINNACLE_ROWS].focus.position == row_count-1,
               "Alt+Page Up pages back along the columns");
    evt = uishell_table_diag_nav(0, -1, UI_EventDeltaUnit_Word);
    uishell_table_key(grid, &evt);
    snap = uishell_table_diag_frame(table, 1200, 800, 0);
    row = snap.axes[BINNACLE_ROWS].focus.position;
    U64 col = snap.axes[BINNACLE_COLS].focus.position;
    TableCheck(row == 0, "Cmd+Up goes to the first row");
    evt = (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Space, .modifiers = WM_Modifier_Shift};
    taken = uishell_table_key(grid, &evt);
    UI_Event space = {.kind = UI_EventKind_Text, .string = str8_lit(" "), .modifiers = WM_Modifier_Shift};
    B32 space_taken = uishell_table_key(grid, &space);
    B32 typed = 0;
    for(BinnacleEffect effect; binnacle_grid_poll_effect(grid, &effect);) { typed |= effect.kind == BINNACLE_EFFECT_TEXT_TYPED; }
    snap = uishell_table_diag_frame(table, 1200, 800, 0);
    TableCheck(taken && uishell_table_diag_selected(&snap, BINNACLE_ROWS, row) && !uishell_table_diag_selected(&snap, BINNACLE_COLS, col),
               "Shift+Space selects the focused row");
    TableCheck(space_taken && !typed,
               "the space Shift+Space types is not text for the cell");
    evt = (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Space, .modifiers = WM_Modifier_Ctrl};
    taken = uishell_table_key(grid, &evt);
    snap = uishell_table_diag_frame(table, 1200, 800, 0);
    TableCheck(taken && uishell_table_diag_selected(&snap, BINNACLE_COLS, col) && !uishell_table_diag_selected(&snap, BINNACLE_ROWS, row),
               "Ctrl+Space selects the focused column");
    evt = (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_F3};
    TableCheck(uishell_table_key(grid, &evt), "F3 steps find");
    evt = (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_F3, .modifiers = WM_Modifier_Shift};
    TableCheck(uishell_table_key(grid, &evt), "Shift+F3 steps find back");
    wheelhouse_table_free(table);
  }

  // Huge: scrolling to the end shows the last of 10^15 rows, filled.
  {
    WheelhouseTable *table = wheelhouse_table_open((WheelhouseText){(U8 *)"huge", 4}, line, digit, 0, 0);
    BinnacleGrid *grid = wheelhouse_table_grid(table);
    uishell_table_diag_frame(table, 1200, 800, 0);
    BinnacleAction end = {sizeof(end), BINNACLE_ACTION_SCROLL_TO};
    end.args.scroll_to = (BinnacleScrollTo){BINNACLE_ROWS, 1.0};
    binnacle_grid_action(grid, &end);
    BinnacleSnapshot snap = uishell_table_diag_frame(table, 1200, 800, 0);
    BinnacleAxisSnapshot *rows = &snap.axes[BINNACLE_ROWS];
    TableCheck(rows->strip_len > 0 && rows->strip[rows->strip_len-1].position == rows->count-1 &&
               uishell_table_diag_filled(&snap), "scrolling to the end shows the last row, filled");
    wheelhouse_table_free(table);
  }

  // Live: values change and new tickers are listed as its clock runs, and
  // the frame says when it next changes.
  {
    WheelhouseTable *table = wheelhouse_table_open((WheelhouseText){(U8 *)"live", 4}, line, digit, 0, 0);
    BinnacleSnapshot snap = uishell_table_diag_frame(table, 1200, 800, 1000000);
    U64 count = snap.axes[BINNACLE_ROWS].count;
    String8 before = uishell_table_diag_cell(scratch.arena, table, 0, 1);
    B32 changed = 0;
    // The clock runs a tick at a time: after a long gap the table starts
    // its clock again rather than catching up.
    for(U64 t = 1; t <= 12; t += 1)
    {
      snap = uishell_table_diag_frame(table, 1200, 800, 1000000+t*250000);
      changed |= !str8_match(before, uishell_table_diag_cell(scratch.arena, table, 0, 1), 0);
    }
    TableCheck(changed, "a live ticker's price changes as the clock runs");
    TableCheck(snap.axes[BINNACLE_ROWS].count > count, "the live table lists new tickers");
    TableCheck(wheelhouse_table_frame(table, 1200, 800, 4000000) <= 250000, "a live table says when it next changes");
    wheelhouse_table_free(table);
  }

  // The View: built from a tab's expression and painted, it draws only the
  // cells in view, however big the table.
  {
    CFG_Node *window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *host = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
    RD_PanelAddress panel = rd_fixture_leaf(rd_fixture_owner(host));
    String8 names[] = {str8_lit("records"), str8_lit("huge"), str8_lit("live")};
    UI_State *saved_ui = ui_state, *test_ui = ui_state_alloc();
    ui_select_state(test_ui);
    // The footer builds nothing without a sidebar state, which would leave
    // the Down below passing for want of a footer; an empty one serves: no
    // error, no notice, no sections, so the footer draws its bare row.
    UIShell_SidebarState *saved_sidebar = ws->sidebar, sidebar = {0};
    if(!ws->sidebar) { ws->sidebar = &sidebar; }
    for EachElement(n, names)
    {
      CFG_Node *view = rd_fixture_view(panel, str8_lit("table"), names[n], 1);
      U64 drawn = 0, visible = 0;
      // Records: a press and release on the crew header, sent as UI events,
      // reach the Grid and sort the column.
      Vec2F32 header_pos = {0}, cell_pos = {0};
      B32 sorted = 0;
      U64 requested_after = 0, deadline_after = 0, built_at = 0;
      U64 saved_requested = rd_state->num_frames_requested, saved_deadline = rd_state->frame_deadline_us;
      U64 clicked_row = 0, focused_row = 0;
      UIShell_RegsScope(.window = ws->cfg_id, .panel = panel, .tab = rd_view_address_from_cfg(view), .view = rd_view_address_from_cfg(view))
      for(U32 frame = 0; frame < 10; frame += 1)
      {
        UI_EventList events = {0};
        UI_EventNode click = {0};
        // Frames 3-4 click the crew header, 6-7 a cell, and 8 presses Down.
        Vec2F32 pos = frame <= 4 ? header_pos : cell_pos;
        B32 clicking = n == 0 && (frame == 3 || frame == 4 || frame == 6 || frame == 7) && pos.x > 0;
        if(clicking)
        {
          click.v = (UI_Event){.kind = frame == 3 || frame == 6 ? UI_EventKind_Press : UI_EventKind_Release,
                               .key = WM_Key_LeftMouseButton, .pos = pos};
          events.first = events.last = &click;
          events.count = 1;
        }
        // Down as its key binding sends it: the move_down command's event.
        if(n == 0 && frame == 8)
        {
          uishell_dispatch_ui_event_command(str8_lit("move_down"));
          events = ws->ui_events;
          MemoryZeroStruct(&ws->ui_events);
        }
        // Frame 9 has no input: what the View asks for then is its own.
        if(frame == 9) { rd_state->num_frames_requested = 0; rd_state->frame_deadline_us = 0; built_at = now_time_us(); }
        UI_AnimationInfo animation = {0};
        ui_begin_build(ws->os, &events, &ws->ui->icon_info, ws->theme, &animation, 1.f/60, 1.f/60);
        if(clicking) { ui_state->mouse = pos; }
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        {
          // The sidebar footer builds before the workspace's panels, as in
          // the window: it must not take the View's keys.
          UIShell_ControlledSplit split = {.owner_cfg = window};
          uishell_sidebar_footer_ui(r2f32p(0, 600, 900, 640), &split);
          wh_view_ui_direct(wh_renderer_from_name(str8_lit("table")), rd_view_address_from_cfg(view), r2f32p(0, 0, 900, 600));
        }
        ui_end_build();
        if(frame == 9) { requested_after = rd_state->num_frames_requested; deadline_after = rd_state->frame_deadline_us; }
        UI_Box *grid_box = &ui_nil_box;
        for(UI_Box *box = ui_state->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, ui_state->root).next)
        {
          if(box->custom_draw == uishell_table_draw) { grid_box = box; }
        }
        if(frame == 2 && !ui_box_is_nil(grid_box))
        {
          UIShell_TableDraw *d = (UIShell_TableDraw *)grid_box->custom_draw_user_data;
          BinnacleAxisSnapshot *cols = &d->snap.axes[BINNACLE_COLS];
          for(size_t c = 0; c < cols->headers_len; c += 1)
          {
            BinnacleRect r = cols->headers[c].rect;
            if(cols->headers[c].key == 3) { header_pos = v2f32(grid_box->rect.x0+r.x+r.width/2, grid_box->rect.y0+r.y+r.height/2); }
          }
        }
        if(frame == 5 && !ui_box_is_nil(grid_box))
        {
          UIShell_TableDraw *d = (UIShell_TableDraw *)grid_box->custom_draw_user_data;
          BinnacleAxisSnapshot *cols = &d->snap.axes[BINNACLE_COLS];
          for(size_t c = 0; c < cols->headers_len; c += 1)
          {
            if(cols->headers[c].key == 3) { sorted = !!(cols->headers[c].flags & BINNACLE_HEADER_SORTED); }
          }
          BinnacleStripItem *row = &d->snap.axes[BINNACLE_ROWS].strip[3], *col = &cols->strip[1];
          cell_pos = v2f32(grid_box->rect.x0+col->offset+col->size/2, grid_box->rect.y0+row->offset+row->size/2);
          clicked_row = row->position;
        }
        if(frame == 9 && !ui_box_is_nil(grid_box))
        {
          UIShell_TableDraw *d = (UIShell_TableDraw *)grid_box->custom_draw_user_data;
          focused_row = d->snap.has_focus ? d->snap.axes[BINNACLE_ROWS].focus.position : 0;
        }
        if(frame == 2 && !ui_box_is_nil(grid_box))
        {
          UIShell_TableDraw *d = (UIShell_TableDraw *)grid_box->custom_draw_user_data;
          visible = (U64)d->snap.axes[0].strip_len*d->snap.axes[1].strip_len;
          uishell_table_cells_drawn = 0;
          DR_BucketScope(dr_bucket_make()) { grid_box->custom_draw(grid_box, grid_box->custom_draw_user_data); }
          drawn = uishell_table_cells_drawn;
        }
      }
      rd_state->num_frames_requested = saved_requested;
      rd_state->frame_deadline_us = saved_deadline;
      if(str8_match(names[n], str8_lit("live"), 0))
      {
        TableCheck(requested_after == 0 && deadline_after > built_at && deadline_after <= now_time_us() + 250000,
                   "a live table asks for one frame at its next tick, not every frame");
      }
      else
      {
        TableCheck(requested_after == 0 && deadline_after == 0, "a still table asks for no frames");
      }
      if(n == 0)
      {
        TableCheck(sorted, "a click on a header, through the View, sorts its column");
        TableCheck(clicked_row > 0 && focused_row == clicked_row+1, "a cell click then Down, through the View, focuses the next row");
      }
      TableCheck(drawn > 0, "the View paints cells");
      TableCheck(drawn <= visible && visible < 2000, "the View paints only the cells in view");
      printf("TABLE_PAINT table=%.*s drawn=%llu visible=%llu\n", str8_varg(names[n]), (unsigned long long)drawn, (unsigned long long)visible);
    }

    // The bars (#577): a huge table in either style shows a vertical bar
    // whose thumb, dragged to the end of its track, shows the last row; the
    // classic arrow scrolls one row and the track a page.
    UI_ScrollBarStyle saved_style = ui_active_scroll_bar_style();
    for(U32 style = 0; style < 2; style += 1)
    {
      CFG_Node *view = rd_fixture_view(panel, str8_lit("table"), str8_lit("huge"), 1);
      UIShell_RegsScope(.window = ws->cfg_id, .panel = panel, .tab = rd_view_address_from_cfg(view), .view = rd_view_address_from_cfg(view))
      {
        UI_Event none = {0};
        Vec2F32 away = v2f32(450, 300);
        UI_Box *thumb = &ui_nil_box, *grid_box = &ui_nil_box;
        for(U32 frame = 0; frame < 4; frame += 1) { grid_box = uishell_table_diag_view_frame(ws, view, style, none, away, UIShell_TableDiagBar_Thumb, &thumb); }
        TableCheck(!ui_box_is_nil(thumb), "a huge table shows a vertical scroll bar");
        if(ui_box_is_nil(thumb) || ui_box_is_nil(grid_box)) { continue; }
        {
          UIShell_TableDraw *first = (UIShell_TableDraw *)grid_box->custom_draw_user_data;
          F32 scrolling_start = grid_box->rect.y0 + first->snap.axes[BINNACLE_ROWS].scrolling_start;
          TableCheck(thumb->parent->rect.y0 >= scrolling_start - 0.5f, "the vertical bar keeps clear of the column headers");
        }
        // Classic, from the top: the track below the thumb scrolls down a
        // page. (Near the end of a huge table the track below the thumb is
        // too thin to click, so this comes before the drag.)
        if(style == UI_ScrollBarStyle_Classic)
        {
          U64 from = ((UIShell_TableDraw *)grid_box->custom_draw_user_data)->snap.axes[BINNACLE_ROWS].anchor.position;
          UI_Box *after = &ui_nil_box;
          grid_box = uishell_table_diag_click_bar(ws, view, style, UIShell_TableDiagBar_After, &after);
          TableCheck(!ui_box_is_nil(grid_box) && uishell_table_diag_paged((UIShell_TableDraw *)grid_box->custom_draw_user_data, from, +1),
                     "the track below the thumb scrolls down a page");
          for(U32 frame = 0; frame < 2; frame += 1) { grid_box = uishell_table_diag_view_frame(ws, view, style, none, away, UIShell_TableDiagBar_Thumb, &thumb); }
          if(ui_box_is_nil(thumb) || ui_box_is_nil(grid_box)) { continue; }
        }
        // Drag the thumb past the end of its track.
        Vec2F32 grab = center_2f32(thumb->rect);
        Rng2F32 track = thumb->parent->rect;
        for(U32 frame = 0; frame < 2; frame += 1) { uishell_table_diag_view_frame(ws, view, style, none, grab, UIShell_TableDiagBar_Thumb, &thumb); }
        UI_Event press = {.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = grab};
        uishell_table_diag_view_frame(ws, view, style, press, grab, UIShell_TableDiagBar_Thumb, &thumb);
        Vec2F32 end = v2f32(grab.x, track.y1 + 40);
        for(U32 frame = 0; frame < 2; frame += 1) { uishell_table_diag_view_frame(ws, view, style, none, end, UIShell_TableDiagBar_Thumb, &thumb); }
        UI_Event release = {.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = end};
        uishell_table_diag_view_frame(ws, view, style, release, end, UIShell_TableDiagBar_Thumb, &thumb);
        for(U32 frame = 0; frame < 3; frame += 1) { grid_box = uishell_table_diag_view_frame(ws, view, style, none, away, UIShell_TableDiagBar_Thumb, &thumb); }
        UIShell_TableDraw *d = (UIShell_TableDraw *)grid_box->custom_draw_user_data;
        BinnacleAxisSnapshot *rows = &d->snap.axes[BINNACLE_ROWS];
        B32 last_shown = rows->strip_len > 0 && rows->strip[rows->strip_len-1].position == rows->count-1;
        TableCheck(last_shown && d->snap.axes[BINNACLE_ROWS].thumb.fraction > 0.99, "dragging the thumb to the end shows the last row");
        // Under a classic bar the grid runs to the edge, and its End Gap
        // brings the last row clear of the bar.
        if(last_shown && !ui_box_is_nil(thumb))
        {
          BinnacleStripItem *last = &rows->strip[rows->strip_len-1];
          UI_Box *vbar = ui_box_from_key(ui_key_from_stringf(grid_box->key, "scroll_region_bar_%i", Axis2_Y));
          UI_Box *hbar = ui_box_from_key(ui_key_from_stringf(grid_box->key, "scroll_region_bar_%i", Axis2_X));
          if(style == UI_ScrollBarStyle_Classic)
          {
            TableCheck(!ui_box_is_nil(vbar) && vbar->rect.x0 < grid_box->rect.x1 && !ui_box_is_nil(hbar) && hbar->rect.y0 < grid_box->rect.y1,
                       "classic bars lie over the grid's edges");
            TableCheck(!ui_box_is_nil(hbar) && grid_box->rect.y0 + last->offset + last->size <= hbar->rect.y0 + 0.5f,
                       "scrolled to the end, the last row ends clear of the horizontal bar");
          }
        }
        if(style != UI_ScrollBarStyle_Classic) { continue; }
        // Back towards the top with the track above the thumb, a page at a
        // time. Each check reads the frame it follows: the draw data is
        // rebuilt every frame.
        U64 from = rows->anchor.position;
        UI_Box *before = &ui_nil_box;
        grid_box = uishell_table_diag_click_bar(ws, view, style, UIShell_TableDiagBar_Before, &before);
        TableCheck(!ui_box_is_nil(grid_box) && uishell_table_diag_paged((UIShell_TableDraw *)grid_box->custom_draw_user_data, from, -1),
                   "the track above the thumb scrolls up a page");
        if(ui_box_is_nil(grid_box)) { continue; }
        // The arrow below the track scrolls one row down.
        from = ((UIShell_TableDraw *)grid_box->custom_draw_user_data)->snap.axes[BINNACLE_ROWS].anchor.position;
        UI_Box *arrow = &ui_nil_box;
        grid_box = uishell_table_diag_click_bar(ws, view, style, UIShell_TableDiagBar_Down, &arrow);
        TableCheck(!ui_box_is_nil(grid_box) && ((UIShell_TableDraw *)grid_box->custom_draw_user_data)->snap.axes[BINNACLE_ROWS].anchor.position == from+1,
                   "the arrow below the track scrolls one row down");
      }
    }

    // A zoom (the font size grown by half) resizes the open table's rows,
    // columns and header bands with the text, and the first row shown
    // stays first.
    {
      CFG_Node *view = rd_fixture_view(panel, str8_lit("table"), str8_lit("records"), 1);
      UIShell_RegsScope(.window = ws->cfg_id, .panel = panel, .tab = rd_view_address_from_cfg(view), .view = rd_view_address_from_cfg(view))
      {
        UI_Event none = {0};
        Vec2F32 away = v2f32(450, 300);
        UI_Box *thumb = &ui_nil_box, *grid_box = &ui_nil_box;
        for(U32 frame = 0; frame < 3; frame += 1) { grid_box = uishell_table_diag_view_frame(ws, view, UI_ScrollBarStyle_Overlay, none, away, UIShell_TableDiagBar_Thumb, &thumb); }
        UIShell_TableDraw *d = (UIShell_TableDraw *)grid_box->custom_draw_user_data;
        BinnacleAction scroll = {sizeof(scroll), BINNACLE_ACTION_SCROLL_TO};
        scroll.args.scroll_to = (BinnacleScrollTo){BINNACLE_ROWS, 0.5};
        if(d) { binnacle_grid_action(wheelhouse_table_grid(d->table), &scroll); }
        for(U32 frame = 0; frame < 2; frame += 1) { grid_box = uishell_table_diag_view_frame(ws, view, UI_ScrollBarStyle_Overlay, none, away, UIShell_TableDiagBar_Thumb, &thumb); }
        d = (UIShell_TableDraw *)grid_box->custom_draw_user_data;
        // The Snapshot's arrays are the Grid's, so the sizes are kept as
        // values: [first row, row header band, first column, column header band].
        F32 before[4] = {0}, after[4] = {0};
        U64 first_before = 0, first_after = 0;
        B32 shown = d && d->snap.axes[BINNACLE_ROWS].strip_len > 1 && d->snap.axes[BINNACLE_COLS].strip_len > 0;
        if(shown)
        {
          BinnacleAxisSnapshot *rows = &d->snap.axes[BINNACLE_ROWS], *cols = &d->snap.axes[BINNACLE_COLS];
          F32 sizes[4] = {rows->strip[1].size, rows->header, cols->strip[0].size, cols->header};
          MemoryCopyArray(before, sizes);
          first_before = rows->anchor.position;
        }
        uishell_table_diag_font_size = 18;
        for(U32 frame = 0; frame < 2; frame += 1) { grid_box = uishell_table_diag_view_frame(ws, view, UI_ScrollBarStyle_Overlay, none, away, UIShell_TableDiagBar_Thumb, &thumb); }
        uishell_table_diag_font_size = 12;
        d = (UIShell_TableDraw *)grid_box->custom_draw_user_data;
        shown = shown && d && d->snap.axes[BINNACLE_ROWS].strip_len > 1 && d->snap.axes[BINNACLE_COLS].strip_len > 0;
        if(shown)
        {
          BinnacleAxisSnapshot *rows = &d->snap.axes[BINNACLE_ROWS], *cols = &d->snap.axes[BINNACLE_COLS];
          F32 sizes[4] = {rows->strip[1].size, rows->header, cols->strip[0].size, cols->header};
          MemoryCopyArray(after, sizes);
          first_after = rows->anchor.position;
        }
        printf("TABLE_ZOOM row=%.1f->%.1f col=%.1f->%.1f first=%llu->%llu\n", before[0], after[0], before[2], after[2],
               (unsigned long long)first_before, (unsigned long long)first_after);
        TableCheck(shown && first_before > 0 && first_after == first_before, "a zoom keeps the first row shown");
        TableCheck(shown && abs_f32(after[0] - before[0]*1.5f) < 1.f && abs_f32(after[1] - before[1]*1.5f) < 1.f,
                   "a zoom resizes the rows and the header row with the text");
        TableCheck(shown && after[2] > before[2]*1.3f && after[3] > before[3]*1.3f,
                   "a zoom resizes the columns and the row numbers with the text");
      }
    }
    ui_set_active_scroll_bar_style(saved_style);
    ws->sidebar = saved_sidebar;
    ui_select_state(saved_ui);
  }

  // A timed frame request bounds the event wait: a blocking wait returns by
  // its deadline, and the bound applies to one wait.
  {
    wm_get_events(scratch.arena, 0);
    U64 begin = now_time_us(), deadline = begin + 40000;
    U64 waits = 0, empty = 0;
    while(now_time_us() < deadline && now_time_us() < begin + 1000000)
    {
      wm_set_wait_deadline(deadline);
      WM_EventList got = wm_get_events(scratch.arena, 1);
      waits += 1; empty += got.count == 0;
    }
    U64 end = now_time_us();
    printf("TABLE_WAIT waits=%llu empty=%llu us=%llu\n", (unsigned long long)waits, (unsigned long long)empty, (unsigned long long)(end-begin));
    TableCheck(end >= deadline && end < deadline + 50000, "a blocking wait returns at its deadline");
    wm_set_wait_deadline(end + 1000);
    wm_take_wait_timeout_us();
    TableCheck(wm_take_wait_timeout_us() == max_U64, "a wait deadline bounds one wait");
  }

  // Frame cost: the 10^15-row table costs about what 1,000 rows do.
  F64 records_us = uishell_table_diag_bench(str8_lit("records"), line, digit);
  F64 huge_us = uishell_table_diag_bench(str8_lit("huge"), line, digit);
  uishell_table_diag_bench(str8_lit("live"), line, digit);
  TableCheck(huge_us <= records_us*3 + 50, "a huge table's frame costs about a small one's");

  if(failures == 0) { printf("table diagnostics passed\n"); }
  scratch_end(scratch);
  return failures == 0;
}

// --table_screenshot: once the table View has painted a few frames, write
// the window to a PPM and exit (macOS).
global String8 uishell_table_screenshot_path;

internal B32
uishell_table_screenshot(RD_WindowState *ws)
{
  B32 ok = 0;
#if OS_MAC
  Temp scratch = scratch_begin(0, 0);
  R_Readback rb = uishell_overview_benchmark_readback(scratch.arena, ws);
  if(rb.format == R_Tex2DFormat_BGRA8 && rb.data.size == (U64)rb.size.x*rb.size.y*4)
  {
    U64 count = (U64)rb.size.x*rb.size.y;
    U8 *rgb = push_array_no_zero(scratch.arena, U8, count*3);
    for(U64 i = 0; i < count; i += 1) { rgb[3*i] = rb.data.str[4*i+2]; rgb[3*i+1] = rb.data.str[4*i+1]; rgb[3*i+2] = rb.data.str[4*i]; }
    String8List output = {0};
    str8_list_push(scratch.arena, &output, push_str8f(scratch.arena, "P6\n%i %i\n255\n", rb.size.x, rb.size.y));
    str8_list_push(scratch.arena, &output, str8(rgb, count*3));
    ok = write_data_list_to_file_path(uishell_table_screenshot_path, output);
  }
  scratch_end(scratch);
#endif
  return ok;
}
