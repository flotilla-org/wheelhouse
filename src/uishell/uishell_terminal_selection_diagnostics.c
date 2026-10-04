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
  S64 viewport_top, viewport_max;
  cleat_viewport_command viewport[64];
  U64 viewport_count;
};

internal B32
uishell_selection_test_input(void *user, cleat_input_event const *input, cleat_input_result *result)
{
  UIShell_SelectionFixture *f = user;
  if(f->sent_count < ArrayCount(f->sent)) { f->sent[f->sent_count++] = *input; }
  result->count = f->input_count;
  return f->accepted;
}

internal B32
uishell_selection_test_viewport(void *user, cleat_viewport_command const *command, cleat_viewport_command_result *result)
{
  UIShell_SelectionFixture *f = user;
  if(f->viewport_count < ArrayCount(f->viewport)) { f->viewport[f->viewport_count++] = *command; }
  S64 next = Clamp(0, f->viewport_top+command->delta_rows, f->viewport_max);
  result->outcome = next == f->viewport_top ? CLEAT_VIEWPORT_OUTCOME_NO_OP : CLEAT_VIEWPORT_OUTCOME_MOVED;
  f->viewport_top = next;
  return 1;
}

internal void
uishell_selection_fixture_init(UIShell_SelectionFixture *f)
{
  MemoryZeroStruct(f);
  f->accepted = 1; f->input_count = 1;
  f->tv.input_sink = uishell_selection_test_input; f->tv.input_sink_user = f;
  f->tv.viewport_sink = uishell_selection_test_viewport; f->tv.viewport_sink_user = f;
  f->tv.cell_width_px = f->tv.cell_height_px = 10;
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

// Exhaustive stateful traces generate tracking, modifier and signed-delta
// combinations. The provider boundary fake models only outer viewport bounds.
internal B32
uishell_terminal_override_diagnostics(void)
{
  B32 ok = 1;
#define OverrideCheck(expr) do { if(!(expr)) { fprintf(stderr, "override trace failed at %i: %s\n", __LINE__, #expr); ok = 0; } } while(0)
  Rng2F32 canvas = r2f32p(0, 0, 60, 30);
  for(U32 capture = 0; capture < 2; capture++)
  for(U32 history = 0; history < 2; history++)
  for(U32 edge = 0; edge < 3; edge++)
  for(S32 sign = -1; sign <= 1; sign += 2)
  for(U32 precise = 0; precise < 2; precise++)
  {
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    f.tv.mouse_tracking_mode = capture ? CLEAT_MOUSE_TRACKING_NORMAL : CLEAT_MOUSE_TRACKING_NONE;
    f.viewport_max = history ? 12 : 0;
    f.viewport_top = edge == 0 ? 0 : edge == 1 ? f.viewport_max/2 : f.viewport_max;
    S64 initial = f.viewport_top;
    f.tv.has_selection = 1;
    UI_Event wheel = {.kind = UI_EventKind_Scroll, .modifiers = WM_Modifier_Shift,
      .pos = {25, 15}, .delta_2f32 = {0, precise ? sign*2.5f : sign*30.f}, .scroll_is_precise = precise};
    // Sub-row deltas accumulate exactly; local events produce viewport requests
    // only, even with capture, no history and either bound.
    U32 events = precise ? 4 : 1;
    for(U32 n = 0; n < events; n++)
    {
      uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
      OverrideCheck(f.sent_count == 0 && f.viewport_count == (n+1 == events));
    }
    OverrideCheck(f.viewport[0].delta_rows == sign && f.viewport_top == Clamp(0, initial+sign, f.viewport_max));
    OverrideCheck(f.tv.has_selection == (initial == f.viewport_top));
    // A plain event immediately after a local one retains its own coordinates,
    // modifier and accumulates fractional rows, without a second viewport request.
    wheel.modifiers = WM_Modifier_Ctrl; wheel.pos = v2f32(45, 25);
    uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    if(precise)
    {
      OverrideCheck(!f.sent_count);
      for(U32 n = 0; n < 3; n++) { uishell_terminal_wheel_event(&f.tv, &wheel, canvas); }
    }
    OverrideCheck(f.sent_count == 1 && f.viewport_count == 1);
    OverrideCheck(f.sent[0].mouse_kind == CLEAT_MOUSE_WHEEL && f.sent[0].modifiers == CLEAT_MOD_CTRL);
    OverrideCheck(f.sent[0].cell_col == 4 && f.sent[0].cell_row == 2 && f.sent[0].x_px == 45 && f.sent[0].y_px == 25);
    OverrideCheck(f.sent[0].wheel_delta_y == -sign);
    uishell_selection_fixture_release(&f);
  }
  {
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    f.viewport_max = 10; f.viewport_top = 5;
    UI_Event wheel = {.kind = UI_EventKind_Scroll, .modifiers = WM_Modifier_Shift,
      .delta_2f32 = {0, 7.5f}, .scroll_is_precise = 1};
    // Reversal cancels accumulated sub-rows. Geometry changes use current cell
    // height without reinterpreting previous fractions or inverting direction.
    uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    wheel.delta_2f32.y = -2.5f; uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    OverrideCheck(!f.viewport_count && f.tv.local_scroll_rows == 0.5);
    f.tv.rows = 6; f.tv.cell_height_px = 20;
    wheel.delta_2f32.y = 10; uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    OverrideCheck(f.viewport_count == 1 && f.viewport_top == 6 && !f.tv.local_scroll_rows);
    wheel.delta_2f32.y = 0; uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    wheel.delta_2f32.y = NAN; uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    OverrideCheck(f.viewport_count == 1 && !f.sent_count);
    uishell_selection_fixture_release(&f);
  }
  {
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    // Precise application input accumulates separately per modifier stream;
    // adding Shift cannot transfer any local sub-row movement to the process.
    UI_Event wheel = {.kind = UI_EventKind_Scroll, .scroll_is_precise = 1,
      .pos = {100, -20}, .delta_2f32 = {5, 5}};
    uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    wheel.modifiers = WM_Modifier_Ctrl; uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    wheel.modifiers = WM_Modifier_Shift; uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    OverrideCheck(!f.sent_count && !f.viewport_count);
    wheel.modifiers = 0; uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    OverrideCheck(f.sent_count == 1 && !f.viewport_count && f.sent[0].modifiers == 0);
    OverrideCheck(f.sent[0].wheel_delta_x == -1 && f.sent[0].wheel_delta_y == -1 && f.sent[0].cell_col == 5 && f.sent[0].cell_row == 0);
    wheel.modifiers = WM_Modifier_Ctrl; uishell_terminal_wheel_event(&f.tv, &wheel, canvas);
    OverrideCheck(f.sent_count == 2 && f.sent[1].modifiers == CLEAT_MOD_CTRL && f.tv.local_scroll_rows == 0.5);
    uishell_selection_fixture_release(&f);
  }
  for(U32 precise = 0; precise < 2; precise++)
  for(S32 sign = -1; sign <= 1; sign += 2)
  {
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    f.viewport_top = 5; f.viewport_max = 10;
    // AppKit Shift mouse wheels can arrive on X. The forced local override
    // uses that signed axis when Y is zero, including precise trackpad fractions.
    UI_Event wheel = {.kind = UI_EventKind_Scroll, .modifiers = WM_Modifier_Shift,
      .delta_2f32 = {precise ? sign*5.f : sign*30.f, 0}, .scroll_is_precise = precise};
    for(U32 n = 0; n < (precise ? 2 : 1); n++) { uishell_terminal_wheel_event(&f.tv, &wheel, canvas); }
    OverrideCheck(!f.sent_count && f.viewport_count == 1 && f.viewport[0].delta_rows == sign && f.viewport_top == 5+sign);
    uishell_selection_fixture_release(&f);
  }
  for(U32 down = 0; down < 2; down++)
  for(U32 semantic = 0; semantic < 2; semantic++)
  {
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    f.viewport_max = 100; f.viewport_top = 50;
    UI_Event page = {.kind = semantic ? UI_EventKind_Navigate : UI_EventKind_Press,
      .key = down ? WM_Key_PageDown : WM_Key_PageUp, .modifiers = semantic ? 0 : WM_Modifier_Shift,
      .flags = semantic ? UI_EventFlag_KeepMark : 0, .delta_unit = UI_EventDeltaUnit_Page, .delta_2s32 = {0, down ? 1 : -1}};
    // Drive the native shell ownership consumer and its actual semantic
    // command consumer together: one physical press yields one viewport request,
    // never a second UI Navigate/key event. Semantic commands act once too.
    Temp page_scratch = scratch_begin(0, 0);
    RD_WindowState *window = push_array(page_scratch.arena, RD_WindowState, 1);
    WM_Event wm = {.kind = WM_EventKind_Press, .key = page.key, .modifiers = WM_Modifier_Shift};
    String8 command = {0};
    if(semantic) { OverrideCheck(uishell_terminal_key_event(&f.tv, &page, CLEAT_KEY_ACTION_PRESS)); }
    else
    {
      OverrideCheck(uishell_terminal_page_binding_event(window, &wm, 42, &command));
      OverrideCheck(uishell_terminal_page_command(&f.tv, command, f.tv.rows));
    }
    OverrideCheck(f.viewport_count == 1 && !f.sent_count);
    f.tv.rows = 6; wm.is_repeat = 1; command = str8_zero();
    if(semantic) { OverrideCheck(uishell_terminal_key_event(&f.tv, &page, CLEAT_KEY_ACTION_PRESS)); }
    else
    {
      OverrideCheck(uishell_terminal_page_binding_event(window, &wm, 42, &command));
      OverrideCheck(uishell_terminal_page_command(&f.tv, command, f.tv.rows));
      wm.kind = WM_EventKind_Release; wm.modifiers = 0;
      OverrideCheck(uishell_terminal_page_binding_event(window, &wm, 42, &command));
    }
    OverrideCheck(f.viewport_count == 2 && !f.sent_count && f.viewport[0].delta_rows == (down ? 3 : -3) && f.viewport[1].delta_rows == (down ? 6 : -6));
    scratch_end(page_scratch);
    // Plain page press/release stay on the application path even if Shift is
    // added before release; there is no extra local page movement.
    page.kind = UI_EventKind_Press; page.flags = 0; page.modifiers = 0;
    OverrideCheck(uishell_terminal_key_event(&f.tv, &page, CLEAT_KEY_ACTION_PRESS));
    OverrideCheck(f.sent_count == 1 && f.sent[0].kind == CLEAT_INPUT_KEY && f.sent[0].key_code == (down ? CLEAT_KEY_PAGE_DOWN : CLEAT_KEY_PAGE_UP));
    page.kind = UI_EventKind_Release; page.modifiers = WM_Modifier_Shift;
    OverrideCheck(uishell_terminal_key_event(&f.tv, &page, CLEAT_KEY_ACTION_RELEASE) && f.viewport_count == 2);
    OverrideCheck(f.sent_count == 2 && f.sent[1].key_action == CLEAT_KEY_ACTION_RELEASE);
    OverrideCheck(uishell_terminal_page_command(&f.tv, down ? str8_lit("terminal_scroll_page_down") : str8_lit("terminal_scroll_page_up"), 9));
    OverrideCheck(f.sent_count == 2 && f.viewport_count == 3 && f.viewport[2].delta_rows == (down ? 9 : -9));
    uishell_selection_fixture_release(&f);
  }
  for(U32 capture = 0; capture < 2; capture++)
  for(U32 shift = 0; shift < 2; shift++)
  for(U32 empty = 0; empty < 2; empty++)
  for(U32 ending = 0; ending < 2; ending++)
  {
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    f.tv.native_view = 1;
    f.tv.mouse_tracking_mode = capture ? CLEAT_MOUSE_TRACKING_NORMAL : CLEAT_MOUSE_TRACKING_NONE;
    UI_Event middle = {.kind = UI_EventKind_Press, .key = WM_Key_MiddleMouseButton,
      .modifiers = shift ? WM_Modifier_Shift : 0, .pos = {15, 5}};
    B32 local = shift || !capture;
    // A complete middle gesture has one owner; local presses request exactly
    // one structured selection Paste, while empty selection is a consumed noop.
    UIShell_TerminalMouseResult result = uishell_terminal_mouse_event(&f.tv, &middle, canvas, 10, 10);
    OverrideCheck(result.paste_selection == local && f.tv.gesture_registered);
    if(result.paste_selection) { uishell_terminal_paste_selection(&f.tv, empty ? str8_zero() : str8_lit("selection\ntext")); }
    OverrideCheck(f.sent_count == (local ? !empty : 1));
    if(local && !empty) { OverrideCheck(f.sent[0].kind == CLEAT_INPUT_PASTE && f.sent[0].text_len == 14 && !memcmp(f.sent[0].text, "selection\ntext", 14)); }
    f.tv.mouse_tracking_mode = capture ? CLEAT_MOUSE_TRACKING_NONE : CLEAT_MOUSE_TRACKING_NORMAL;
    middle.modifiers = shift ? 0 : WM_Modifier_Shift;
    middle.kind = UI_EventKind_Release; middle.pos = v2f32(100, 100);
    if(ending) { uishell_terminal_cancel_buttons(&f.tv); }
    result = uishell_terminal_mouse_event(&f.tv, &middle, canvas, 10, 10);
    OverrideCheck(!result.paste_selection && !f.tv.middle_press_consumed && !f.tv.gesture_registered && !f.tv.mouse_buttons_held);
    OverrideCheck(f.sent_count == (local ? !empty : 2));
    if(!local) { OverrideCheck(f.sent[1].mouse_kind == CLEAT_MOUSE_RELEASE && !(f.sent[1].modifiers & CLEAT_MOD_SHIFT)); }
    uishell_selection_fixture_release(&f);
  }
  {
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    f.tv.mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NORMAL;
    // Independent local middle ownership must not cancel #154's active left
    // rectangle owner. Both complete gestures remain absent from child input.
    uishell_selection_test_mouse(&f, UI_EventKind_Press, WM_Modifier_Shift|WM_Modifier_Alt, 0, 0);
    UI_Event middle = {.kind = UI_EventKind_Press, .key = WM_Key_MiddleMouseButton, .modifiers = WM_Modifier_Shift, .pos = {15, 15}};
    OverrideCheck(uishell_terminal_mouse_event(&f.tv, &middle, canvas, 10, 10).paste_selection);
    middle.kind = UI_EventKind_Release; middle.modifiers = 0;
    uishell_terminal_mouse_event(&f.tv, &middle, canvas, 10, 10);
    OverrideCheck(f.tv.left_owner == UIShell_TerminalLeftOwner_Selection && f.tv.selecting && f.tv.selection_rectangular);
    OverrideCheck(uishell_selection_test_mouse(&f, UI_EventKind_Release, 0, 2, 3).selection_completed);
    OverrideCheck(!f.sent_count && !f.tv.left_owner && !f.tv.middle_press_consumed);
    uishell_selection_fixture_release(&f);
  }
  {
    Temp scratch = scratch_begin(0, 0);
    RD_WindowState *first = push_array(scratch.arena, RD_WindowState, 1);
    RD_WindowState *second = push_array(scratch.arena, RD_WindowState, 1);
    // Accepted configurable page commands latch in the invoking window. A
    // modifier change, focus change or another window cannot leak its release.
    uishell_terminal_page_binding_accept(first, WM_Key_Up, str8_lit("terminal_scroll_page_up"), 42);
    WM_Event event = {.kind = WM_EventKind_Press, .key = WM_Key_Up};
    String8 repeat = {0};
    OverrideCheck(uishell_terminal_page_binding_event(first, &event, 42, &repeat));
    OverrideCheck(str8_match(repeat, str8_lit("terminal_scroll_page_up"), 0));
    repeat = str8_zero();
    OverrideCheck(uishell_terminal_page_binding_event(first, &event, 43, &repeat) && !repeat.size);
    event.kind = WM_EventKind_Release; event.modifiers = WM_Modifier_Shift;
    OverrideCheck(!uishell_terminal_page_binding_event(second, &event, 42, &repeat));
    OverrideCheck(uishell_terminal_page_binding_event(first, &event, 0, &repeat) && !first->terminal_page_keys[WM_Key_Up]);
    OverrideCheck(!uishell_terminal_page_binding_event(first, &event, 42, &repeat));
    // Physical Shift-page uses the same window seam, including focus-loss
    // release ownership, while plain page remains an application gesture.
    event = (WM_Event){.kind = WM_EventKind_Press, .key = WM_Key_PageUp};
    OverrideCheck(!uishell_terminal_shift_page_command(&event).size);
    event.modifiers = WM_Modifier_Shift;
    String8 physical = uishell_terminal_shift_page_command(&event);
    OverrideCheck(str8_match(physical, str8_lit("terminal_scroll_page_up"), 0));
    uishell_terminal_page_binding_accept(first, event.key, physical, 42);
    event.kind = WM_EventKind_Release; event.modifiers = 0;
    OverrideCheck(uishell_terminal_page_binding_event(first, &event, 0, &repeat));
    // Adding Shift after an application page press cannot reclassify repeats
    // or swallow the application's matching release.
    first->terminal_page_keys[WM_Key_PageDown] = UIShell_TerminalPageOwner_Application;
    event = (WM_Event){.kind = WM_EventKind_Press, .key = WM_Key_PageDown, .modifiers = WM_Modifier_Shift};
    OverrideCheck(!uishell_terminal_page_binding_event(first, &event, 42, &repeat));
    OverrideCheck(first->terminal_page_keys[WM_Key_PageDown] == UIShell_TerminalPageOwner_Application);
    event.kind = WM_EventKind_Release;
    OverrideCheck(!uishell_terminal_page_binding_event(first, &event, 42, &repeat) && !first->terminal_page_keys[WM_Key_PageDown]);
    // Losing focus clears application latches and view references, while a
    // cancelled local latch consumes its late release without scrolling.
    first->terminal_page_keys[WM_Key_PageDown] = UIShell_TerminalPageOwner_Application;
    uishell_terminal_page_binding_accept(first, WM_Key_PageUp, str8_lit("terminal_scroll_page_up"), 42);
    event = (WM_Event){.kind = WM_EventKind_WindowLoseFocus}; repeat = str8_zero();
    OverrideCheck(!uishell_terminal_page_binding_event(first, &event, 0, &repeat));
    OverrideCheck(!first->terminal_page_keys[WM_Key_PageDown] && !first->terminal_page_views[WM_Key_PageUp]);
    event = (WM_Event){.kind = WM_EventKind_Release, .key = WM_Key_PageUp};
    OverrideCheck(uishell_terminal_page_binding_event(first, &event, 42, &repeat) && !repeat.size);
    event = (WM_Event){.kind = WM_EventKind_Press, .key = WM_Key_PageDown, .modifiers = WM_Modifier_Shift};
    OverrideCheck(uishell_terminal_page_binding_event(first, &event, 42, &repeat));
    OverrideCheck(str8_match(repeat, str8_lit("terminal_scroll_page_down"), 0));
    event.kind = WM_EventKind_WindowLoseFocus; repeat = str8_zero();
    uishell_terminal_page_binding_event(first, &event, 0, &repeat);
    event.kind = WM_EventKind_Press; event.is_repeat = 1;
    OverrideCheck(uishell_terminal_page_binding_event(first, &event, 42, &repeat) && !repeat.size);
    event.is_repeat = 0;
    OverrideCheck(uishell_terminal_page_binding_event(first, &event, 42, &repeat) && repeat.size);
    event.kind = WM_EventKind_Release;
    OverrideCheck(uishell_terminal_page_binding_event(first, &event, 42, &repeat));
    uishell_terminal_page_binding_accept(first, WM_Key_Down, str8_lit("copy"), 42);
    OverrideCheck(!first->terminal_page_keys[WM_Key_Down]);
    scratch_end(scratch);
  }
#undef OverrideCheck
  fprintf(stderr, "terminal override event traces %s\n", ok ? "passed" : "failed");
  return ok;
}

// Real UI claiming trace: the same frame contains different wheel modifiers,
// coordinates, and precision. The terminal canvas produces no aggregated scroll.
internal B32
uishell_terminal_override_ui_diagnostics(RD_WindowState *ws)
{
  UI_State *saved = ui_state, *test = ui_state_alloc();
  ui_select_state(test);
  UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
  f.viewport_top = 5; f.viewport_max = 10;
  UI_EventNode nodes[6] = {0}; UI_EventList events = {0};
  for(U32 n = 0; n < ArrayCount(nodes); n++)
  {
    nodes[n].v = (UI_Event){.kind = UI_EventKind_Scroll,
      .modifiers = n == 1 ? WM_Modifier_Ctrl : WM_Modifier_Shift,
      .pos = {15+(F32)n*10, 15}, .delta_2f32 = {0, n == 1 ? -30.f : 5.f},
      .scroll_is_precise = n != 1};
    if(n >= 4) { nodes[n].v = (UI_Event){.kind = n == 4 ? UI_EventKind_Press : UI_EventKind_Release,
      .key = WM_Key_MiddleMouseButton, .modifiers = n == 4 ? WM_Modifier_Shift : 0,
      .pos = n == 4 ? v2f32(25, 15) : v2f32(100, 100)}; }
    if(n) { nodes[n-1].next = &nodes[n]; nodes[n].prev = &nodes[n-1]; }
  }
  f.tv.mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NORMAL;
  events.first = nodes; events.last = &nodes[5]; events.count = 6;
  UI_AnimationInfo animation = {0};
  ui_begin_build(ws->os, &events, &ws->ui->icon_info, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = v2f32(500, 500); // sampled pointer cannot replace event coordinates
  UI_ScrollRegion region = {0}; region.viewport = r2f32p(0, 0, 60, 30);
  UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
  UI_Box *parent = ui_build_box_from_key(0, ui_key_make(1986)); parent->rect = region.viewport;
  UI_ScrollRegionSignal region_signal = ui_scroll_region_build(parent, ui_key_make(1987), &region, axes,
    UI_BoxFlag_MouseClickable|UI_BoxFlag_Scroll|UI_BoxFlag_CollectScrollEvents);
  UI_Box *box = region_signal.content_box; box->rect = region.viewport;
  UI_Signal signal = ui_signal_from_box(box);
  B32 ok = signal.mouse_events.count == 6 && events.count == 0 &&
    !signal.scroll.x && !signal.scroll.y && !signal.scroll_px.x && !signal.scroll_px.y && !region_signal.position.y.idx;
  U32 count = 0;
  for(UI_EventNode *n = signal.mouse_events.first; n; n = n->next)
  {
    ok &= n->v.modifiers == nodes[count].v.modifiers && n->v.pos.x == nodes[count].v.pos.x;
    if(n->v.kind == UI_EventKind_Scroll) { uishell_terminal_wheel_event(&f.tv, &n->v, box->rect); }
    else
    {
      UIShell_TerminalMouseResult result = uishell_terminal_mouse_event(&f.tv, &n->v, box->rect, 10, 10);
      if(result.paste_selection) { uishell_terminal_paste_selection(&f.tv, str8_lit("selection")); }
    }
    count++;
  }
  ok &= count == 6 && f.sent_count == 2 && f.viewport_count == 1 && f.viewport_top == 6 && f.tv.local_scroll_rows == 0.5;
  ok &= f.sent[1].kind == CLEAT_INPUT_PASTE && !f.tv.middle_press_consumed && !f.tv.mouse_buttons_held;
  ok &= f.sent[0].modifiers == CLEAT_MOD_CTRL && f.sent[0].cell_col == 2 && f.sent[0].wheel_delta_y == 1;
  ui_end_build(); ui_select_state(saved); ui_state_release(test);
  uishell_selection_fixture_release(&f);
  fprintf(stderr, "terminal override UI traces %s\n", ok ? "passed" : "failed");
  return ok;
}

#if OS_MAC
internal B32
uishell_terminal_snapshot_contains(cleat_session *session, String8 text)
{
  cleat_snapshot snapshot = {0}; B32 found = 0;
  if(cleat_session_snapshot(session, &snapshot))
  {
    for(U64 start = 0; start+text.size <= snapshot.cell_count && !found; start++)
    {
      found = 1;
      for(U64 n = 0; n < text.size; n++)
      {
        cleat_cell const *cell = &snapshot.cells[start+n];
        if(cell->grapheme_count != 1 || cell->graphemes[0] != text.str[n]) { found = 0; break; }
      }
    }
    cleat_session_release_snapshot(session, &snapshot);
  }
  return found;
}

// Native CI evidence: AppKit scroll conversion and Cleat's real PTY paste
// encoder. Physical mouse/trackpad and nested attach acceptance remain separate.
internal B32
uishell_terminal_override_appkit_diagnostics(void)
{
  B32 ok = 1;
  for(U32 horizontal = 0; horizontal < 2; horizontal++)
  for(U32 precise = 0; precise < 2; precise++)
  for(U32 shift = 0; shift < 2; shift++)
  for(S32 sign = -1; sign <= 1; sign += 2)
  {
    CGEventRef cg = CGEventCreateScrollWheelEvent(0, precise ? kCGScrollEventUnitPixel : kCGScrollEventUnitLine, 2, horizontal ? 0 : sign*5, horizontal ? sign*5 : 0);
    CGEventSetFlags(cg, shift ? kCGEventFlagMaskShift : 0);
    NSEvent *event = [NSEvent eventWithCGEvent:cg];
    WM_Event wm = {.kind = WM_EventKind_Scroll}; mac_wm_scroll_fields(&wm, event);
    // Assert the actual AppKit event's units and signed axis, rather than
    // assuming CGEvent's requested ticks equal AppKit's scrollingDelta pixels.
    F32 native_delta = wm.delta.y != 0 ? wm.delta.y : wm.delta.x;
    B32 case_ok = !!(wm.modifiers & WM_Modifier_Shift) == shift && wm.scroll_is_precise == precise;
    case_ok &= wm.delta.y == -(F32)[event scrollingDeltaY] && wm.delta.x == -(F32)[event scrollingDeltaX];
    case_ok &= native_delta*sign < 0;
    UIShell_SelectionFixture f; uishell_selection_fixture_init(&f);
    f.viewport_top = 20; f.viewport_max = 40;
    UI_Event ui = {.kind = UI_EventKind_Scroll, .modifiers = wm.modifiers,
      .pos = {25, 15}, .delta_2f32 = wm.delta, .scroll_is_precise = wm.scroll_is_precise};
    U32 events = 0;
    while(events < 1024 && !(shift ? f.viewport_count : f.sent_count))
    { uishell_terminal_wheel_event(&f.tv, &ui, r2f32p(0, 0, 60, 30)); events++; }
    F32 rows_per_event = uishell_terminal_wheel_rows(native_delta, wm.scroll_is_precise, 10);
    S32 expected_rows = (S32)(rows_per_event*events);
    case_ok &= f.sent_count == !shift && expected_rows != 0;
    if(!shift)
    {
      F32 delivered = wm.delta.y != 0 ? f.sent[0].wheel_delta_y : f.sent[0].wheel_delta_x;
      case_ok &= delivered == -expected_rows && delivered*sign > 0;
    }
    else { case_ok &= f.viewport_count == 1 && f.viewport[0].delta_rows == expected_rows && f.viewport[0].delta_rows*sign < 0; }
    if(!case_ok)
    {
      fprintf(stderr, "AppKit wheel failed: axis=%u precise=%u shift=%u sign=%i delta=(%g,%g) native_precise=%i mods=%u events=%u rows=%i input=%llu viewport=%llu\n",
        horizontal, precise, shift, sign, wm.delta.x, wm.delta.y, wm.scroll_is_precise, wm.modifiers, events, expected_rows,
        (unsigned long long)f.sent_count, (unsigned long long)f.viewport_count);
    }
    ok &= case_ok;
    uishell_selection_fixture_release(&f); CFRelease(cg);
  }
  Temp scratch = scratch_begin(0, 0);
  // Disposable named selection pasteboard is distinct from the standard one.
  String8 saved_selection = wm_get_selection_text(scratch.arena);
  String8 saved_clipboard = wm_get_clipboard_text(scratch.arena);
  wm_set_selection_text(str8_lit("paste")); wm_set_clipboard_text(str8_lit("standard"));
  B32 selection_ok = str8_match(wm_get_selection_text(scratch.arena), str8_lit("paste"), 0);
  B32 clipboard_ok = str8_match(wm_get_clipboard_text(scratch.arena), str8_lit("standard"), 0);
  if(!selection_ok || !clipboard_ok) { fprintf(stderr, "native clipboard distinction failed: selection=%i standard=%i\n", selection_ok, clipboard_ok); }
  ok &= selection_ok && clipboard_ok;
  char runtime_dir[] = "/tmp/wheelhouse-native-paste.XXXXXX";
  char *runtime_root = mkdtemp(runtime_dir);
  cleat_provider_desc provider_desc = {.abi_version = CLEAT_PROVIDER_ABI_VERSION, .backend = CLEAT_PROVIDER_BACKEND_IN_PROCESS,
    .runtime_root = (U8 *)runtime_root, .runtime_root_len = runtime_root ? strlen(runtime_root) : 0};
  cleat_provider *provider = runtime_root ? cleat_provider_open(&provider_desc) : 0;
  // The subprocess enables capture/bracketed paste, then prints the exact input
  // bytes. The 17-byte read cannot complete if Paste is sent as ordinary text.
  String8 command = str8_lit("/bin/sh -c \"stty raw -echo; printf '\\033[?2004h\\033[?1000hREADY'; dd bs=1 count=17 2>/dev/null | od -An -tx1 | tr -d ' \\n'\"");
  cleat_session_desc desc = {.cols = 80, .rows = 8, .cell_width_px = 10, .cell_height_px = 10,
    .vt_engine = CLEAT_PROVIDER_VT_GHOSTTY, .command = command.str, .command_len = command.size};
  cleat_session *session = provider ? cleat_session_create(provider, &desc) : 0;
  B32 ready = 0, wrapped = 0;
  if(session)
  {
    for(U32 n = 0; n < 300 && !ready; n++)
    { ready = uishell_terminal_snapshot_contains(session, str8_lit("READY")); if(!ready) { sleep_ms(10); } }
    UIShell_TerminalViewState tv = {.session = session, .mouse_tracking_mode = CLEAT_MOUSE_TRACKING_NORMAL};
    UI_Event middle = {.kind = UI_EventKind_Press, .key = WM_Key_MiddleMouseButton, .modifiers = WM_Modifier_Shift};
    UIShell_TerminalMouseResult result = uishell_terminal_mouse_event(&tv, &middle, r2f32p(0, 0, 800, 80), 10, 10);
    if(ready && result.paste_selection) { uishell_terminal_paste_selection(&tv, wm_get_selection_text(scratch.arena)); }
    middle.kind = UI_EventKind_Release; middle.modifiers = 0;
    uishell_terminal_mouse_event(&tv, &middle, r2f32p(0, 0, 800, 80), 10, 10);
    for(U32 n = 0; n < 300 && !wrapped; n++)
    { wrapped = uishell_terminal_snapshot_contains(session, str8_lit("1b5b3230307e70617374651b5b3230317e")); if(!wrapped) { sleep_ms(10); } }
    cleat_session_destroy(session);
  }
  if(provider) { cleat_provider_close(provider); }
  if(runtime_root)
  {
    NSError *error = nil;
    ok &= [[NSFileManager defaultManager] removeItemAtPath:[NSString stringWithUTF8String:runtime_root] error:&error];
    if(error) { fprintf(stderr, "native paste cleanup failed: %s\n", [[error description] UTF8String]); }
  }
  ok &= ready && wrapped;
  wm_set_selection_text(saved_selection); wm_set_clipboard_text(saved_clipboard);
  scratch_end(scratch);
  fprintf(stderr, "terminal AppKit/paste diagnostics %s (ready=%i wrapped=%i)\n", ok ? "passed" : "failed", ready, wrapped);
  return ok;
}
#endif
