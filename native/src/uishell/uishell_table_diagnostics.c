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
