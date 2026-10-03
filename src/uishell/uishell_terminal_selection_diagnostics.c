// Fixture feeds and the input sink stand in only for the Cleat process boundary.
// These traces use the same feed, mouse and Copy consumers as Terminal View.
typedef struct UIShell_SelectionFixture UIShell_SelectionFixture;
struct UIShell_SelectionFixture
{
  UIShell_TerminalViewState tv;
  U32 letters[18];
  cleat_render_cell cells[18];
  cleat_render_update_op op;
  cleat_render_update update;
  cleat_input_event sent[64];
  U64 sent_count;
  B32 accepted;
  U64 input_count;
};

internal B32
uishell_selection_test_input(void *user, cleat_input_event const *input, cleat_input_result *result)
{
  UIShell_SelectionFixture *f = user;
  if(f->sent_count < ArrayCount(f->sent)) { f->sent[f->sent_count++] = *input; }
  result->count = f->input_count;
  return f->accepted;
}

internal void
uishell_selection_fixture_init(UIShell_SelectionFixture *f)
{
  MemoryZeroStruct(f);
  f->accepted = 1; f->input_count = 1;
  f->tv.input_sink = uishell_selection_test_input; f->tv.input_sink_user = f;
  f->tv.cols = 6; f->tv.rows = 3;
  for(U64 i = 0; i < ArrayCount(f->letters); i++)
  {
    f->letters[i] = 'a'+i;
    f->cells[i].graphemes = &f->letters[i]; f->cells[i].grapheme_count = 1;
  }
  f->op = (cleat_render_update_op){.kind = CLEAT_RENDER_OP_FULL_VISIBLE_REPLACE,
    .row_count = 3, .col_count = 6, .cells = f->cells, .cell_count = 18};
  f->update = (cleat_render_update){.cols = 6, .rows = 3, .viewport_kind = CLEAT_VIEWPORT_LIVE_NORMAL,
    .ops = &f->op, .op_count = 1, .scrollbar = {.viewport_rows = 3}};
  uishell_terminal_apply_update(&f->tv, &f->update);
}

internal void
uishell_selection_fixture_release(UIShell_SelectionFixture *f)
{
  if(f->tv.cell_cache.arena) { arena_release(f->tv.cell_cache.arena); }
}

internal UIShell_TerminalMouseResult
uishell_selection_test_mouse(UIShell_SelectionFixture *f, UI_EventKind kind, WM_Modifiers mods, S64 row, S64 col)
{
  UI_Event evt = {.kind = kind, .key = kind == UI_EventKind_MouseMove ? WM_Key_Null : WM_Key_LeftMouseButton,
    .modifiers = mods, .pos = {(F32)col*10+5, (F32)row*10+5}};
  return uishell_terminal_mouse_event(&f->tv, &evt, r2f32p(0, 0, 60, 30), 10, 10);
}

internal B32
uishell_terminal_selection_lifetime_diagnostics(void)
{
  Temp scratch = scratch_begin(0, 0);
  B32 ok = 1;
#define SelectionCheck(expr) do { if(!(expr)) { fprintf(stderr, "selection trace failed at %i: %s\n", __LINE__, #expr); ok = 0; } } while(0)
  UIShell_SelectionFixture f;
  uishell_selection_fixture_init(&f);
  // Regression: selected ab must be deliberately cleared before Copy can return Xb.
  uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift, 0, 0);
  uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 0, 1);
  SelectionCheck(str8_match(uishell_terminal_copy_selection(scratch.arena, &f.tv), str8_lit("ab"), 0));
  f.letters[0] = 'X'; uishell_terminal_apply_update(&f.tv, &f.update);
  SelectionCheck(!f.tv.has_selection && !uishell_terminal_copy_selection(scratch.arena, &f.tv).size);
  uishell_selection_fixture_release(&f);

  // Exhaustive generator: stream/rectangle, four drag directions, each of 18
  // cells overwritten independently. Changes outside highlight must preserve Copy.
  for(U32 rectangle = 0; rectangle < 2; rectangle++)
  for(U32 direction = 0; direction < 4; direction++)
  for(U32 changed = 0; changed < 18; changed++)
  {
    uishell_selection_fixture_init(&f);
    S64 first_row = direction & 1 ? 2 : 0, last_row = 2-first_row;
    S64 first_col = direction & 2 ? 3 : 1, last_col = 4-first_col;
    WM_Modifiers mods = WM_Modifier_Shift | (rectangle ? WM_Modifier_Alt : 0);
    uishell_selection_test_mouse(&f, UI_EventKind_Press, mods, first_row, first_col);
    uishell_selection_test_mouse(&f, UI_EventKind_MouseMove, 0, last_row, last_col);
    UIShell_TerminalMouseResult released = uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, last_row, last_col);
    SelectionCheck(released.selection_completed && f.tv.selection_rectangular == rectangle && f.sent_count == 0);
    String8 original = uishell_terminal_copy_selection(scratch.arena, &f.tv);
    UIShell_TerminalCellFeed feed = uishell_terminal_cell_feed_from_cache(&f.tv.cell_cache);
    Rng1S64 columns = uishell_terminal_selection_columns(&feed, f.tv.sel_mark, f.tv.sel_cursor, rectangle, changed/6);
    B32 inside = (S64)(changed%6) >= columns.min && (S64)(changed%6) < columns.max;
    f.letters[changed] = 'X'; uishell_terminal_apply_update(&f.tv, &f.update);
    SelectionCheck(f.tv.has_selection == !inside);
    String8 copied = uishell_terminal_copy_selection(scratch.arena, &f.tv);
    SelectionCheck(inside ? !copied.size : str8_match(copied, original, 0));
    uishell_selection_fixture_release(&f);
  }

  // Mapping identities invalidate even when geometry and text happen to match.
  // Generate dimensions, viewport kind/offset/top, screen and session replacement.
  for(U32 transition = 0; transition < 7; transition++)
  {
    uishell_selection_fixture_init(&f);
    uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift, 0, 1);
    uishell_selection_test_mouse(&f, UI_EventKind_MouseMove, 0, 0, 3);
    switch(transition)
    {
      case 0: f.update.cols++; break;
      case 1: f.update.rows++; break;
      case 2: f.update.viewport_kind = CLEAT_VIEWPORT_NORMAL_SCROLLBACK; break;
      case 3: f.update.scrollback_offset_rows++; break;
      case 4: f.update.scrollbar.viewport_top_row++; break;
      case 5: f.update.terminal_modes.active_alternate_screen = 1; break;
      case 6: f.tv.selection_session = (cleat_session *)&f; break; // opaque identity, never dereferenced
    }
    uishell_terminal_apply_update(&f.tv, &f.update);
    SelectionCheck(!f.tv.has_selection && !f.tv.selecting);
    // A canceled local drag cannot reselect unrelated content or leak its release.
    uishell_selection_test_mouse(&f, UI_EventKind_MouseMove, 0, 1, 4);
    uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 1, 4);
    SelectionCheck(!f.tv.has_selection && f.sent_count == 0 && !f.tv.left_owner);
    uishell_selection_fixture_release(&f);
  }

  // Cursor-only, style-only and unchanged full refresh preserve exact Copy bytes.
  uishell_selection_fixture_init(&f);
  uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift, 0, 1);
  uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 0, 3);
  String8 original = uishell_terminal_copy_selection(scratch.arena, &f.tv);
  for(U32 repaint = 0; repaint < 4; repaint++)
  {
    f.update.render_generation++;
    f.update.cursor.col++;
    f.update.dirty = CLEAT_DIRTY_FULL;
    f.cells[1].style.flags ^= CLEAT_CELL_FLAG_BOLD;
    f.update.op_count = repaint & 1 ? 1 : 0;
    uishell_terminal_apply_update(&f.tv, &f.update);
    SelectionCheck(f.tv.has_selection && str8_match(uishell_terminal_copy_selection(scratch.arena, &f.tv), original, 0));
  }
  uishell_selection_fixture_release(&f);

  // Wide boundaries and grapheme changes on the leading cell invalidate a
  // selected tail, even if the leading cell lies outside a rectangular highlight.
  for(U32 wide_change = 0; wide_change < 3; wide_change++)
  {
    uishell_selection_fixture_init(&f);
    f.cells[1].style.width = CLEAT_CELL_WIDTH_WIDE;
    f.cells[2].style.width = CLEAT_CELL_WIDTH_SPACER_TAIL; f.cells[2].grapheme_count = 0;
    uishell_terminal_apply_update(&f.tv, &f.update);
    uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift|WM_Modifier_Alt, 0, 2);
    uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 1, 3);
    if(wide_change == 0) { f.letters[1] = 0x754c; }
    if(wide_change == 1) { f.cells[2].style.width = CLEAT_CELL_WIDTH_NARROW; }
    if(wide_change == 2) { f.cells[1].style.width = CLEAT_CELL_WIDTH_NARROW; }
    uishell_terminal_apply_update(&f.tv, &f.update);
    SelectionCheck(!f.tv.has_selection && !uishell_terminal_copy_selection(scratch.arena, &f.tv).size);
    uishell_selection_fixture_release(&f);
  }

  // Row-copy touching either source or destination cancels, even for identical
  // row contents; copies wholly outside the selection retain it.
  for(U32 source = 0; source < 3; source++)
  for(U32 dest = 0; dest < 3; dest++)
  {
    uishell_selection_fixture_init(&f);
    for(U32 i = 0; i < 18; i++) { f.letters[i] = 'a'+i%6; }
    uishell_terminal_apply_update(&f.tv, &f.update);
    uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift, 1, 1);
    uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 1, 3);
    f.op = (cleat_render_update_op){.kind = CLEAT_RENDER_OP_SCROLL_COPY, .src_row = source, .dst_row = dest, .row_count = 1};
    uishell_terminal_apply_update(&f.tv, &f.update);
    if(f.tv.has_selection != !(source != dest && (source == 1 || dest == 1))) { fprintf(stderr, "row copy src=%u dst=%u selected=%i\n", source, dest, f.tv.has_selection); }
    SelectionCheck(f.tv.has_selection == !(source != dest && (source == 1 || dest == 1)));
    uishell_selection_fixture_release(&f);
  }

  // The provider's accepted/count result governs input dismissal. Copy, focus,
  // empty Paste, rejected input and successful no-ops preserve it.
  for(U32 kind = 0; kind < 7; kind++)
  for(U32 acceptance = 0; acceptance < 3; acceptance++)
  {
    uishell_selection_fixture_init(&f);
    uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift, 0, 1);
    uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 0, 3);
    f.accepted = acceptance != 0; f.input_count = acceptance == 2 ? 1 : 0;
    cleat_input_event input = {0};
    switch(kind)
    {
      case 0: input = (cleat_input_event){.kind = CLEAT_INPUT_TEXT, .text = (U8 *)"x", .text_len = 1}; break;
      case 1: input = (cleat_input_event){.kind = CLEAT_INPUT_PASTE, .text = (U8 *)"x", .text_len = 1}; break;
      case 2: input = (cleat_input_event){.kind = CLEAT_INPUT_KEY, .key_action = CLEAT_KEY_ACTION_PRESS, .key_kind = CLEAT_KEY_NAMED, .key_code = CLEAT_KEY_ESCAPE}; break;
      case 3: input = (cleat_input_event){.kind = CLEAT_INPUT_PASTE}; break;
      case 4: input = (cleat_input_event){.kind = CLEAT_INPUT_FOCUS}; break;
      case 5: input = (cleat_input_event){.kind = CLEAT_INPUT_KEY, .key_action = CLEAT_KEY_ACTION_RELEASE, .key_kind = CLEAT_KEY_NAMED, .key_code = CLEAT_KEY_ESCAPE}; break;
      case 6: input = (cleat_input_event){.kind = CLEAT_INPUT_MOUSE, .mouse_kind = CLEAT_MOUSE_WHEEL}; break;
    }
    uishell_terminal_send_input(&f.tv, &input);
    B32 dismiss = acceptance == 2 && (kind < 3 || kind == 5 || kind == 6);
    SelectionCheck(f.tv.has_selection == !dismiss && f.sent_count == 1);
    if(kind == 2) { SelectionCheck(f.sent[0].key_code == CLEAT_KEY_ESCAPE); }
    uishell_selection_fixture_release(&f);
  }

  // Application press owns drag/release despite modifier/tracking changes.
  // Focus loss and superseding press must close the application gesture once.
  for(U32 ending = 0; ending < 3; ending++)
  {
    uishell_selection_fixture_init(&f); f.tv.mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NORMAL;
    uishell_selection_test_mouse(&f, UI_EventKind_Press, 0, 0, 1);
    f.tv.mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NONE;
    uishell_selection_test_mouse(&f, UI_EventKind_MouseMove, WM_Modifier_Shift|WM_Modifier_Alt, 1, 3);
    if(ending == 0) { uishell_selection_test_mouse(&f, UI_EventKind_Release, WM_Modifier_Shift, 5, 9); }
    if(ending == 1) { uishell_terminal_cancel_gesture(&f.tv); }
    if(ending == 2)
    {
      uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift, 1, 2);
      uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 1, 4);
    }
    SelectionCheck(f.sent_count == 3 && !f.tv.mouse_buttons_held && !f.tv.left_owner);
    SelectionCheck(f.sent[0].mouse_kind == CLEAT_MOUSE_PRESS && f.sent[1].mouse_kind == CLEAT_MOUSE_MOVE && f.sent[2].mouse_kind == CLEAT_MOUSE_RELEASE);
    SelectionCheck(f.sent[0].cell_col == 1 && f.sent[1].cell_col == 3 && !(f.sent[1].modifiers & CLEAT_MOD_SHIFT));
    uishell_selection_fixture_release(&f);
  }

  // A local press owns all subsequent events, including release outside and
  // focus loss. A stationary click never inherits the frame's later pointer.
  uishell_selection_fixture_init(&f); f.tv.mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NORMAL;
  uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift|WM_Modifier_Alt, 0, 1);
  uishell_selection_test_mouse(&f, UI_EventKind_MouseMove, 0, 1, 3);
  uishell_terminal_cancel_gesture(&f.tv);
  uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 4, 9);
  SelectionCheck(!f.tv.has_selection && !f.tv.selecting && f.sent_count == 0);
  uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift, 0, 2);
  uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 0, 2);
  SelectionCheck(!f.tv.has_selection && !f.tv.left_owner && f.sent_count == 0);
  // Retained content can select/Copy with no provider, and cannot send process input.
  f.tv.input_sink = 0;
  uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift, 0, 1);
  uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 0, 3);
  cleat_input_event paste = {.kind = CLEAT_INPUT_PASTE, .text = (U8 *)"x", .text_len = 1};
  SelectionCheck(!uishell_terminal_send_input(&f.tv, &paste) && f.tv.has_selection);
  SelectionCheck(str8_match(uishell_terminal_copy_selection(scratch.arena, &f.tv), str8_lit("bcd"), 0));
  uishell_selection_fixture_release(&f);
  // Modifier-only native presses are not process keys. Copy is local and does
  // not dismiss; a rejected application press must not acquire a child button.
  uishell_selection_fixture_init(&f);
  uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift, 0, 1);
  uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 0, 3);
  WM_Key modifiers[] = {WM_Key_Shift, WM_Key_Ctrl, WM_Key_Alt};
  for(U32 i = 0; i < ArrayCount(modifiers); i++)
  {
    UI_Event event = {.kind = UI_EventKind_Press, .key = modifiers[i]};
    U32 kind = 0, code = 0;
    SelectionCheck(!uishell_terminal_cleat_key_from_ui_event(&event, &kind, &code));
    SelectionCheck(f.tv.has_selection);
  }
  f.tv.mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NORMAL;
  f.accepted = 0;
  uishell_selection_test_mouse(&f, UI_EventKind_Press, 0, 0, 1);
  uishell_selection_test_mouse(&f, UI_EventKind_Release, WM_Modifier_Shift, 0, 3);
  SelectionCheck(f.sent_count == 1 && f.tv.has_selection && !f.tv.mouse_buttons_held);
  uishell_selection_fixture_release(&f);

  // Hyperlink activation owns the entire press independently of later modifiers
  // and never leaks press, drag or release to the child. Shift chooses selection.
  uishell_selection_fixture_init(&f);
  f.tv.cell_cache.hyperlinks[1] = str8_lit("https://example.test");
#if OS_MAC
  WM_Modifiers link_mod = WM_Modifier_Super;
#else
  WM_Modifiers link_mod = WM_Modifier_Ctrl;
#endif
  UIShell_TerminalMouseResult link = uishell_selection_test_mouse(&f, UI_EventKind_Press, link_mod, 0, 1);
  SelectionCheck(link.link.size && f.tv.left_owner == UIShell_TerminalLeftOwner_Link);
  uishell_selection_test_mouse(&f, UI_EventKind_MouseMove, WM_Modifier_Shift, 2, 5);
  uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 2, 5);
  SelectionCheck(f.sent_count == 0 && !f.tv.has_selection && !f.tv.left_owner);
  link = uishell_selection_test_mouse(&f, UI_EventKind_Press, link_mod|WM_Modifier_Shift, 0, 1);
  SelectionCheck(!link.link.size && f.tv.left_owner == UIShell_TerminalLeftOwner_Selection);
  // Teardown uses the actual runtime release hook and leaves no owner registered.
  f.tv.native_view = 1;
  uishell_selection_test_mouse(&f, UI_EventKind_Press, 0, 0, 2);
  uishell_terminal_runtime_release(&f.tv);
  SelectionCheck(!f.tv.left_owner && !f.tv.gesture_registered && !uishell_terminal_gestures);
  uishell_selection_fixture_release(&f);
#undef SelectionCheck
  scratch_end(scratch);
  if(ok) { fprintf(stderr, "terminal selection lifetime/event traces passed\n"); }
  return ok;
}

// Exercise UI claiming before the Terminal View consumer. The frame's sampled
// mouse deliberately differs from every event; ordered messages must win.
internal B32
uishell_terminal_selection_ui_diagnostics(RD_WindowState *ws)
{
  UI_State *saved = ui_state, *test = ui_state_alloc();
  ui_select_state(test);
  B32 ok = 1;
  for(U32 app = 0; app < 2; app++)
  {
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    f.tv.mouse_tracking_mode = app ? CLEAT_MOUSE_TRACKING_NORMAL : CLEAT_MOUSE_TRACKING_NONE;
    UI_EventNode nodes[6] = {0};
    UI_EventKind kinds[] = {UI_EventKind_Press, UI_EventKind_MouseMove, UI_EventKind_Release,
                            UI_EventKind_Press, UI_EventKind_MouseMove, UI_EventKind_Release};
    Vec2F32 positions[] = {{15, 5}, {35, 15}, {35, 15}, {25, 5}, {45, 15}, {95, 45}};
    UI_EventList events = {0};
    for(U32 i = 0; i < 6; i++)
    {
      nodes[i].v = (UI_Event){.kind = kinds[i], .key = kinds[i] == UI_EventKind_MouseMove ? WM_Key_Null : WM_Key_LeftMouseButton,
        .pos = positions[i], .modifiers = i >= 3 ? WM_Modifier_Shift|WM_Modifier_Alt : 0, .timestamp_us = 2000000+i};
      if(i) { nodes[i-1].next = &nodes[i]; nodes[i].prev = &nodes[i-1]; }
    }
    events.first = &nodes[0]; events.last = &nodes[5]; events.count = 6;
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = v2f32(55, 25);
    // An earlier sibling crossed by the drag cannot steal its motion before
    // the terminal processes the queued press. It owns none of these presses.
    UI_Box *sibling = ui_build_box_from_key(UI_BoxFlag_MouseClickable|(app ? UI_BoxFlag_CollectMouseMotion : 0), ui_key_make(986));
    sibling->rect = r2f32p(30, 0, 150, 50);
    UI_Signal sibling_signal = ui_signal_from_box(sibling);
    ok &= sibling_signal.mouse_events.count == 0;
    UI_Box *box = ui_build_box_from_key(UI_BoxFlag_MouseClickable|UI_BoxFlag_CollectMouseMotion, ui_key_make(987));
    box->rect = r2f32p(0, 0, 60, 30);
    UI_Signal signal = ui_signal_from_box(box);
    U32 count = 0;
    for(UI_EventNode *node = signal.mouse_events.first; node; node = node->next)
    {
      if(count >= ArrayCount(kinds)) { ok = 0; break; }
      ok &= node->v.kind == kinds[count] && MemoryMatchStruct(&node->v.pos, &positions[count]);
      uishell_terminal_mouse_event(&f.tv, &node->v, box->rect, 10, 10); count++;
    }
    ok &= count == 6 && events.count == 0 && f.sent_count == (app ? 3 : 0);
    ok &= f.tv.selection_rectangular && txt_pt_match(f.tv.sel_mark, txt_pt(0, 2)) && txt_pt_match(f.tv.sel_cursor, txt_pt(2, 5));
    ok &= f.tv.has_selection && !f.tv.left_owner && !f.tv.mouse_buttons_held;
    // Ordinary clickable widgets keep hover decoration without consuming the
    // move; an opted-in terminal underneath can still receive that message.
    UI_EventNode hover = {.v = {.kind = UI_EventKind_MouseMove, .pos = {45, 15}}};
    events.first = events.last = &hover; events.count = 1;
    sibling->flags &= ~UI_BoxFlag_CollectMouseMotion;
    sibling_signal = ui_signal_from_box(sibling);
    ok &= sibling_signal.mouse_events.count == 0 && events.count == 1 && (sibling_signal.f & UI_SignalFlag_MouseOver);
    signal = ui_signal_from_box(box);
    ok &= signal.mouse_events.count == 1 && events.count == 0;
    // A non-terminal control's active drag retains its signal and cannot lose
    // motion to a terminal beside/under it, even inside the terminal bounds.
    hover.v = (UI_Event){.kind = UI_EventKind_MouseMove, .pos = {45, 15}};
    events.first = events.last = &hover; events.count = 1;
    test->active_box_key[UI_MouseButtonKind_Left] = sibling->key;
    sibling_signal = ui_signal_from_box(sibling);
    signal = ui_signal_from_box(box);
    ok &= (sibling_signal.f & UI_SignalFlag_LeftDragging) && !signal.mouse_events.count && events.count == 1;
    test->active_box_key[UI_MouseButtonKind_Left] = ui_key_zero();
    ui_end_build();
    // Hidden View retirement and teardown run their actual native hooks, pairing
    // an application press once and removing it from the active-gesture list.
    // Canceling terminal capture releases its UI keys as well as provider
    // ownership, without clearing another control's active button.
    f.tv.input_canvas_key = box->key;
    test->active_box_key[UI_MouseButtonKind_Left] = box->key;
    test->active_box_key[UI_MouseButtonKind_Middle] = sibling->key;
    uishell_terminal_clear_ui_capture(&f.tv, test);
    ok &= ui_key_match(test->active_box_key[UI_MouseButtonKind_Left], ui_key_zero()) &&
          ui_key_match(test->active_box_key[UI_MouseButtonKind_Middle], sibling->key);
    test->active_box_key[UI_MouseButtonKind_Middle] = ui_key_zero();
    U64 previous = f.sent_count;
    f.tv.native_view = 1; f.tv.input_window = ws->os;
    f.tv.input_frame = rd_state->frame_index + 7;
    f.tv.mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NORMAL;
    uishell_selection_test_mouse(&f, UI_EventKind_Press, 0, 0, 1);
    uishell_terminal_retire_gestures(1);
    ok &= f.sent_count == previous+2 && !f.tv.left_owner && !f.tv.gesture_registered;
    uishell_selection_test_mouse(&f, UI_EventKind_Press, 0, 0, 1);
    uishell_terminal_runtime_release(&f.tv);
    ok &= f.sent_count == previous+4 && !f.tv.gesture_registered && !uishell_terminal_gestures;
    uishell_selection_fixture_release(&f);
  }
  // Application middle/right presses also retire on a hidden view. Their late
  // physical releases are orphans and must not create a second child release.
  for(U32 button = 0; button < 2; button++)
  {
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    f.tv.native_view = 1; f.tv.input_window = ws->os; f.tv.input_frame = rd_state->frame_index + 7;
    f.tv.mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NORMAL;
    UI_Event event = {.kind = UI_EventKind_Press, .key = button ? WM_Key_RightMouseButton : WM_Key_MiddleMouseButton, .pos = {15, 5}};
    uishell_terminal_mouse_event(&f.tv, &event, r2f32p(0, 0, 60, 30), 10, 10);
    uishell_terminal_retire_gestures(1);
    ok &= f.sent_count == 2 && f.sent[1].mouse_kind == CLEAT_MOUSE_RELEASE && !f.tv.mouse_buttons_held && !f.tv.gesture_registered;
    event.kind = UI_EventKind_Release;
    uishell_terminal_mouse_event(&f.tv, &event, r2f32p(0, 0, 60, 30), 10, 10);
    ok &= f.sent_count == 2;
    uishell_selection_fixture_release(&f);
  }
  // A closed window is absent from shell state. Retirement must close its
  // provider gesture without dereferencing the retired native window handle.
  UIShell_SelectionFixture closed; uishell_selection_fixture_init(&closed);
  closed.tv.native_view = 1; closed.tv.input_canvas_key = ui_key_make(987);
  closed.tv.mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NORMAL;
  uishell_selection_test_mouse(&closed, UI_EventKind_Press, 0, 0, 1);
  uishell_terminal_retire_gestures(1);
  ok &= closed.sent_count == 2 && !closed.tv.left_owner && !closed.tv.gesture_registered;
  uishell_selection_fixture_release(&closed);
  ui_select_state(saved); ui_state_release(test);
  fprintf(stderr, "terminal selection ordered UI events %s\n", ok ? "passed" : "failed");
  return ok;
}
