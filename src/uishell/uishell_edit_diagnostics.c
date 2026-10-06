// Shared command traces use the production key map, command packs, local terminal
// selection and the same text-edit consumer as line edits and settings cells.
// Fakes stand in only for the OS clipboard and Cleat process/transport boundary.
typedef struct UIShell_EditClipboard UIShell_EditClipboard;
struct UIShell_EditClipboard
{
  U8 bytes[4096];
  U64 size, reads, writes;
  B32 change_after_read;
};

internal void
uishell_edit_clip_write(void *user, String8 text)
{
  UIShell_EditClipboard *clip = user;
  clip->writes++;
  clip->size = Min(sizeof(clip->bytes), text.size);
  MemoryCopy(clip->bytes, text.str, clip->size);
}

internal String8
uishell_edit_clip_read(void *user, Arena *arena)
{
  UIShell_EditClipboard *clip = user;
  clip->reads++;
  String8 result = push_str8_copy(arena, str8(clip->bytes, clip->size));
  if(clip->change_after_read) { clip->bytes[0] = '!'; }
  return result;
}

typedef struct UIShell_EditTransport UIShell_EditTransport;
struct UIShell_EditTransport
{
  UIShell_SelectionFixture fixture;
  U32 connection, role;
};

internal void
uishell_edit_transport_state(void *user, U32 *connection, U32 *role)
{
  // fixture is the first member; its existing input sink shares this boundary.
  UIShell_EditTransport *transport = user;
  *connection = transport->connection;
  *role = transport->role;
}

internal void
uishell_edit_bindings(Arena *arena, CFG_State *cfg, CFG_Node *user, B32 rebound)
{
  CFG_Node *old = cfg_node_child_from_string(user, str8_lit("keybindings"));
  if(old != &cfg_nil_node) { cfg_node_release(cfg, old); }
  CFG_Node *bindings = cfg_node_new(cfg, user, str8_lit("keybindings"));
  for(U64 i = 0; i < ArrayCount(uishell_shell_ui_event_default_binding_table); i++)
  {
    UIShell_DefaultBinding b = uishell_shell_ui_event_default_binding_table[i];
    if(rebound && str8_match(b.string, str8_lit("copy"), 0)) { continue; }
    CFG_Node *binding = cfg_node_new(cfg, bindings, str8_zero());
    cfg_node_new(cfg, binding, b.string);
    cfg_node_new(cfg, binding, wm_key_cfg_name_table[b.binding.key]);
    if(b.binding.modifiers & WM_Modifier_Ctrl) { cfg_node_new(cfg, binding, str8_lit("ctrl")); }
    if(b.binding.modifiers & WM_Modifier_Super) { cfg_node_new(cfg, binding, str8_lit("super")); }
    if(b.binding.modifiers & WM_Modifier_Shift) { cfg_node_new(cfg, binding, str8_lit("shift")); }
    if(b.binding.modifiers & WM_Modifier_Alt) { cfg_node_new(cfg, binding, str8_lit("alt")); }
  }
  if(rebound)
  {
    CFG_Node *binding = cfg_node_new(cfg, bindings, str8_zero());
    cfg_node_new(cfg, binding, str8_lit("copy"));
    cfg_node_new(cfg, binding, str8_lit("k"));
    cfg_node_new(cfg, binding, str8_lit("alt"));
  }
  rd_state->key_map = cfg_key_map_from_cfg(arena);
}

// Drive the same registry loop as rd_frame, including run_command expansion.
internal U64
uishell_edit_drain_commands(void)
{
  U64 count = 0;
  for(UIShell_Cmd *cmd = 0; uishell_next_cmd(&cmd);) UIShell_RegsScope()
  {
    MemoryCopyStruct(uishell_regs(), cmd->regs);
    for(UIShell_CmdPack *pack = rd_state->first_cmd_pack; pack; pack = pack->next)
    {
      if(pack->dispatch && pack->dispatch(cmd->name)) { break; }
    }
    if(uishell_is_edit_command(cmd->name)) { count++; }
  }
  MemoryZeroStruct(&rd_state->cmds[0]);
  return count;
}

internal void
uishell_edit_set_window(RD_WindowState *ws, CFG_ID view)
{
  uishell_base_regs()->window = ws->cfg_id;
  uishell_base_regs()->view = uishell_base_regs()->tab = view;
  uishell_base_regs()->edit_owner_key = ui_key_zero();
  uishell_base_regs()->edit_owner_captured = 0;
  ui_select_state(ws->ui);
  ws->ui->events = &ws->ui_events;
}

// Metadata fixture exercises combinations no application command currently uses
// (in particular embedded file paths), through the production query dispatcher.
global UIShell_QueryFlags uishell_query_fixture_flags;
global UIShell_AppRegSlot uishell_query_fixture_slot;
internal UIShell_AppCmdInfo
uishell_query_fixture_info(String8 name)
{
  UIShell_AppCmdInfo info = {0};
  if(str8_match(name, str8_lit("query_fixture"), 0))
  {
    info.string = name;
    info.query_flags = UIShell_QueryFlag_Required|uishell_query_fixture_flags;
    info.query_slot = uishell_query_fixture_slot;
  }
  return info;
}

internal B32
uishell_edit_command_diagnostics(B32 native)
{
#if OS_MAC
  B32 saved_reentrant = mac_wm_state ? mac_wm_state->suppress_reentrant_frame : 0;
  if(native) { mac_wm_state->suppress_reentrant_frame = 1; }
#endif
  RD_State *saved_rd = rd_state;
  CFG_Ctx *saved_cfg = cfg_ctx;
  UI_State *saved_ui = ui_state;
  WM_ClipboardIO saved_clip = wm_clipboard_io;
  Arena *arena = arena_alloc();
  RD_State state = {0};
  rd_state = &state;
  state.arena = arena;
  state.top_regs = &state.base_regs;
  state.frame_arenas[0] = state.frame_arenas[1] = arena;
  state.cmds_arenas[0] = state.cmds_arenas[1] = arena;
  state.cfg = cfg_state_alloc();
  cfg_ctx_select(cfg_state_ctx(state.cfg));
  CFG_Node *user = cfg_node_new(state.cfg, cfg_node_root(), str8_lit("user"));
  RD_WindowStateSlot slot = {0};
  RD_WindowState windows[2] = {0};
  CFG_Node *views[2][2] = {0};
  UIShell_EditTransport terminals[2][2] = {0};
  for(U64 w = 0; w < 2; w++)
  {
    CFG_Node *window = cfg_node_new(state.cfg, user, str8_lit("window"));
    windows[w].cfg_id = window->id;
    windows[w].os = native ? wm_window_open(r2f32p(0, 0, 320, 200), 0, str8_lit("Edit dispatch diagnostic")) : (WM_Window){w+1};
    windows[w].ui = ui_state_alloc();
    UI_InitStacks(windows[w].ui);
    windows[w].ui->build_index = 1;
    windows[w].order_next = w ? &rd_nil_window_state : &windows[1];
    windows[w].hash_next = w ? 0 : &windows[1];
    for(U64 p = 0; p < 2; p++)
    {
      CFG_Node *panel = cfg_node_new(state.cfg, window, str8_lit("panel"));
      views[w][p] = cfg_node_new(state.cfg, panel, str8_lit("terminal"));
      UIShell_EditTransport *t = &terminals[w][p];
      uishell_selection_fixture_init(&t->fixture);
      t->connection = CLEAT_SESSION_STREAMING; t->role = CLEAT_ROLE_CONTROLLER;
      t->fixture.tv.input_state_sink = uishell_edit_transport_state;
    }
  }
  state.first_window_state = &windows[0];
  slot.first = &windows[0]; slot.last = &windows[1];
  state.window_state_slots_count = 1; state.window_state_slots = &slot;
  if(saved_rd) { state.first_cmd_pack = saved_rd->first_cmd_pack; state.last_cmd_pack = saved_rd->last_cmd_pack; }
  else { UISHELL_APP_REGISTER_CMD_PACKS(); uishell_register_shell_cmd_packs(); }
  UIShell_EditClipboard clip = {0};
  wm_clipboard_io = (WM_ClipboardIO){&clip, uishell_edit_clip_write, uishell_edit_clip_read};
  B32 ok = 1;
#define EditCheck(expr) do { if(!(expr)) { fprintf(stderr, "FAIL Edit trace %d: %s\n", __LINE__, #expr); ok = 0; } } while(0)

  // Scenario generator: embedded search and cursor/address queries span empty,
  // ASCII and multibyte edited inputs, both focus states, two views and windows.
  // Repeating an open query preserves text/identity/owner and selects all bytes.
  RD_ViewStateSlot query_slots[1] = {0};
  state.view_state_slots_count = ArrayCount(query_slots);
  state.view_state_slots = query_slots;
  String8 query_commands[] = {str8_lit("search"), str8_lit("goto_line"), str8_lit("goto_address")};
  String8 query_inputs[] = {str8_zero(), str8_lit("edited"), str8_lit("a \xce\xbb\xf0\x9f\x98\x80")};
  for(U64 w = 0; w < 2; w++)
  for(U64 p = 0; p < 2; p++)
  for(U64 c = 0; c < ArrayCount(query_commands); c++)
  for(U64 t = 0; t < ArrayCount(query_inputs); t++)
  for(U64 contents = 0; contents < 2; contents++)
  {
    RD_WindowState *ws = &windows[w];
    uishell_edit_set_window(ws, views[w][p]->id);
    RD_ViewState *vs = rd_view_state_from_cfg(views[w][p]);
    vs->query_is_open = 0;
    uishell_cmd("run_command", .cmd_name = query_commands[c]);
    uishell_edit_drain_commands();
    EditCheck(vs->query_is_open && !vs->contents_are_focused);
    EditCheck(str8_match(rd_view_query_cmd(), query_commands[c], 0));
    uishell_cmd("update_query", .string = query_inputs[t]);
    uishell_edit_drain_commands();
    RD_ViewState other_states[2][2] = {0};
    String8 other_text[2][2] = {0}, other_command[2][2] = {0};
    for(U64 ow = 0; ow < 2; ow++) for(U64 op = 0; op < 2; op++)
    {
      other_states[ow][op] = *rd_view_state_from_cfg(views[ow][op]);
      UIShell_RegsScope(.view = views[ow][op]->id)
      {
        other_text[ow][op] = push_str8_copy(arena, rd_view_query_input());
        other_command[ow][op] = push_str8_copy(arena, rd_view_query_cmd());
      }
    }
    vs->contents_are_focused = contents;
    uishell_cmd("run_command", .cmd_name = query_commands[c]);
    uishell_edit_drain_commands();
    EditCheck(vs->query_is_open && !vs->contents_are_focused);
    EditCheck(vs->query_cursor.line == 1 && vs->query_cursor.column == query_inputs[t].size+1);
    EditCheck(vs->query_mark.line == 1 && vs->query_mark.column == 1);
    EditCheck(str8_match(rd_view_query_input(), query_inputs[t], 0));
    EditCheck(str8_match(rd_view_query_cmd(), query_commands[c], 0));
    EditCheck(uishell_regs()->view == views[w][p]->id && uishell_regs()->window == ws->cfg_id);
    for(U64 ow = 0; ow < 2; ow++) for(U64 op = 0; op < 2; op++)
    {
      if(ow == w && op == p) { continue; }
      RD_ViewState *other = rd_view_state_from_cfg(views[ow][op]);
      EditCheck(other->query_is_open == other_states[ow][op].query_is_open &&
                other->contents_are_focused == other_states[ow][op].contents_are_focused);
      EditCheck(other->query_cursor.column == other_states[ow][op].query_cursor.column &&
                other->query_mark.column == other_states[ow][op].query_mark.column);
      UIShell_RegsScope(.view = views[ow][op]->id)
      {
        EditCheck(str8_match(rd_view_query_input(), other_text[ow][op], 0));
        EditCheck(str8_match(rd_view_query_cmd(), other_command[ow][op], 0));
      }
    }
    // A different query still initializes by its own flags, even while open.
    uishell_cmd("run_command", .cmd_name = str8_lit("goto_line"));
    if(c == 1) { uishell_cmd("run_command", .cmd_name = str8_lit("goto_address")); }
    uishell_edit_drain_commands();
    EditCheck(vs->query_is_open && rd_view_query_input().size == 0);
    // A different KeepOldInput query preserves input but does not select it
    // while already open; SelectOldInput applies to closed initialization only.
    uishell_cmd("update_query", .string = query_inputs[t]); uishell_edit_drain_commands();
    uishell_cmd("run_command", .cmd_name = str8_lit("search_backwards")); uishell_edit_drain_commands();
    EditCheck(vs->query_is_open && str8_match(rd_view_query_input(), query_inputs[t], 0));
    EditCheck(str8_match(rd_view_query_cmd(), str8_lit("search_backwards"), 0));
    EditCheck(vs->query_mark.column == vs->query_cursor.column && vs->query_cursor.column == query_inputs[t].size+1);
  }

  // Flags are exhaustively generated at the real metadata/dispatch seam.
  // Closed queries retain initialization rules; open repeats never reset paths.
  UIShell_CmdPack fixture_pack = {.next = state.first_cmd_pack, .cmd_info_from_string = uishell_query_fixture_info};
  state.first_cmd_pack = &fixture_pack;
  for(U64 file = 0; file < 2; file++)
  for(U64 keep = 0; keep < 2; keep++)
  for(U64 select = 0; select < 2; select++)
  {
    uishell_query_fixture_flags = keep*UIShell_QueryFlag_KeepOldInput|select*UIShell_QueryFlag_SelectOldInput;
    uishell_query_fixture_slot = file ? UIShell_AppRegSlot_FilePath : UIShell_AppRegSlot_String;
    RD_WindowState *ws = &windows[0];
    uishell_edit_set_window(ws, views[0][0]->id);
    RD_ViewState *vs = rd_view_state_from_cfg(views[0][0]);
    vs->query_is_open = 0;
    uishell_cmd("update_query", .string = str8_lit("old")); uishell_edit_drain_commands();
    uishell_cmd("push_query", .cmd_name = str8_lit("query_fixture")); uishell_edit_drain_commands();
    String8 initialized = rd_view_query_input();
    EditCheck(vs->query_is_open && !vs->contents_are_focused);
    EditCheck(file ? initialized.size > 0 && initialized.str[initialized.size-1] == '/' :
              str8_match(initialized, keep ? str8_lit("old") : str8_zero(), 0));
    EditCheck(vs->query_cursor.column == initialized.size+1 && vs->query_mark.column == (select ? 1 : initialized.size+1));
    uishell_cmd("update_query", .string = str8_lit("/edited/\xce\xbb")); uishell_edit_drain_commands();
    uishell_cmd("push_query", .cmd_name = str8_lit("query_fixture")); uishell_edit_drain_commands();
    EditCheck(vs->query_is_open && str8_match(rd_view_query_input(), str8_lit("/edited/\xce\xbb"), 0));
    EditCheck(vs->query_cursor.column == sizeof("/edited/\xce\xbb") && vs->query_mark.column == 1);
    // Listers retain their command, input, focus and selection on push_query.
    cfg_node_new(state.cfg, views[0][0], str8_lit("lister"));
    vs->contents_are_focused = 1; vs->query_mark = vs->query_cursor;
    uishell_cmd("push_query", .cmd_name = str8_lit("goto_line")); uishell_edit_drain_commands();
    EditCheck(str8_match(rd_view_query_cmd(), str8_lit("query_fixture"), 0) && vs->contents_are_focused);
    EditCheck(vs->query_mark.column == vs->query_cursor.column);
    cfg_node_release(state.cfg, cfg_node_child_from_string(views[0][0], str8_lit("lister")));
  }
  state.first_cmd_pack = fixture_pack.next;

  // Synthetic events drive the shell's real binding resolver, command expansion,
  // #152 latch and text consumer. Only activation text belongs to the shortcut.
  for(U64 w = 0; w < 2; w++)
  {
    uishell_edit_bindings(arena, state.cfg, user, 0);
    CFG_Node *bindings = cfg_node_child_from_string(user, str8_lit("keybindings"));
    CFG_Node *binding = cfg_node_new(state.cfg, bindings, str8_zero());
    cfg_node_new(state.cfg, binding, str8_lit("search"));
    cfg_node_new(state.cfg, binding, str8_lit("k"));
    cfg_node_new(state.cfg, binding, str8_lit("alt"));
    state.key_map = cfg_key_map_from_cfg(arena);
    RD_WindowState *ws = &windows[w];
    uishell_edit_set_window(ws, views[w][0]->id);
    RD_ViewState *vs = rd_view_state_from_cfg(views[w][0]);
    vs->query_is_open = 0;
    WM_Event press = {.kind = WM_EventKind_Press, .window = ws->os, .key = WM_Key_K, .modifiers = WM_Modifier_Alt};
    EditCheck(!uishell_route_edit_activation(arena, ws, &press, 0));
    EditCheck(uishell_route_command_activation(arena, ws, &press, 0, 1));
    uishell_edit_drain_commands();
    EditCheck(vs->query_is_open && str8_match(rd_view_query_cmd(), str8_lit("search"), 0));
    uishell_cmd("update_query", .string = str8_lit("edited")); uishell_edit_drain_commands();
    vs->contents_are_focused = 1;
    press.is_repeat = 1;
    EditCheck(!uishell_route_edit_activation(arena, ws, &press, 0));
    EditCheck(uishell_route_command_activation(arena, ws, &press, 0, 1));
    uishell_edit_drain_commands();
    EditCheck(vs->query_is_open && !vs->contents_are_focused && vs->query_mark.column == 1);
    WM_Event texts[] = {
      {.kind = WM_EventKind_Text, .window = ws->os, .source_key = WM_Key_K, .modifiers = WM_Modifier_Alt, .character = 'k'},
      {.kind = WM_EventKind_Text, .window = ws->os, .source_key = WM_Key_J, .character = 'j'},
      {.kind = WM_EventKind_Text, .window = windows[1-w].os, .source_key = WM_Key_K, .character = 'k'},
      {.kind = WM_EventKind_Text, .window = ws->os, .source_key = WM_Key_Null, .character = 0x03bb},
    };
    WM_EventList queued = {0};
    for(U64 t = 0; t < ArrayCount(texts); t++)
    {
      WM_Event *node = wm_event_list_push_new(arena, &queued, WM_EventKind_Text);
      node->window = texts[t].window; node->source_key = texts[t].source_key;
      node->modifiers = texts[t].modifiers; node->character = texts[t].character;
    }
    U64 activation_text_count = 0;
    for(WM_Event *node = queued.first, *next = 0; node; node = next)
    {
      next = node->next;
      if(uishell_route_edit_activation(arena, ws, node, 0))
      { activation_text_count++; wm_eat_event(&queued, node); }
    }
    EditCheck(activation_text_count == 1 && queued.count == 3);
    EditCheck(queued.first->source_key == WM_Key_J && queued.last->source_key == WM_Key_Null);
    EditCheck(wm_window_match(queued.first->next->window, windows[1-w].os));
    // A second pass cannot consume the activation again or eat surviving text.
    for(WM_Event *node = queued.first; node; node = node->next)
    { EditCheck(!uishell_route_edit_activation(arena, ws, node, 0)); }
    for(WM_Event *node = queued.first; node; node = node->next)
    {
      if(wm_window_match(node->window, ws->os))
      {
        String32 cp = str32(&node->character, 1);
        uishell_cmd("insert_text", .string = str8_from_32(arena, cp));
      }
    }
    uishell_edit_drain_commands();
    U8 buffer[64] = "edited"; U64 size = 6;
    ui_consume_text_edit_events(ui_key_make(345+w), buffer, sizeof(buffer), &size, &vs->query_cursor, &vs->query_mark, 0);
    EditCheck(str8_match(str8(buffer, size), str8_lit("j\xce\xbb"), 0));
    EditCheck(ws->ui_events.count == 0);
    WM_Event release = {.kind = WM_EventKind_Release, .window = ws->os, .key = WM_Key_K};
    EditCheck(uishell_route_edit_activation(arena, ws, &release, 0));
    EditCheck(!uishell_route_edit_activation(arena, ws, &release, 0));
    EditCheck(!uishell_route_edit_activation(arena, ws, &texts[0], 0));
    // Losing focus clears stale chords, and the other view/window never changes.
    uishell_route_command_activation(arena, ws, &press, 0, 1); uishell_edit_drain_commands();
    WM_Event lost = {.kind = WM_EventKind_WindowLoseFocus, .window = ws->os};
    uishell_route_edit_activation(arena, ws, &lost, 0);
    EditCheck(!ws->edit_chord_held[WM_Key_K]);
  }

  // Exhaustive scenario generator spans two windows, two panels, default/rebound
  // bindings, menu activation and every modifier combination on the matching release.
  // Each local activation produces one command and no terminal process input.
  for(U64 rebound = 0; rebound < 2; rebound++)
  for(U64 w = 0; w < 2; w++)
  for(U64 p = 0; p < 2; p++)
  {
    uishell_edit_bindings(arena, state.cfg, user, rebound);
    RD_WindowState *ws = &windows[w];
    UIShell_EditTransport *t = &terminals[w][p];
    UI_Key key = ui_key_make(100+w*2+p);
    uishell_edit_set_window(ws, views[w][p]->id);
    uishell_terminal_register_edit_owner(key, &t->fixture.tv, views[w][p]->id);
    U64 before_inputs = t->fixture.sent_count;
    uishell_cmd("select_all");
    EditCheck(uishell_edit_drain_commands() == 1);
    EditCheck(t->fixture.tv.sel_mark.line == 0 && t->fixture.tv.sel_mark.column == 0);
    EditCheck(t->fixture.tv.sel_cursor.line == 2 && t->fixture.tv.sel_cursor.column == 5);
    EditCheck(t->fixture.sent_count == before_inputs);
    CFG_Binding copy = cfg_native_menu_binding_for_owner(state.key_map, str8_lit("copy"), 1);
    EditCheck(copy.key == (rebound ? WM_Key_K : WM_Key_C));
    EditCheck(copy.modifiers == (rebound ? WM_Modifier_Alt : OS_MAC ? WM_Modifier_Super : WM_Modifier_Ctrl|WM_Modifier_Shift));
    for(U64 mods = 0; mods < 16; mods++)
    {
      U64 writes = clip.writes;
      WM_Event press = {.kind = WM_EventKind_Press, .window = ws->os, .key = copy.key, .modifiers = copy.modifiers};
      EditCheck(uishell_route_edit_activation(arena, ws, &press, 1));
      // Repeat and correlated text belong to the physical host chord through release.
      press.is_repeat = 1;
      EditCheck(uishell_route_edit_activation(arena, ws, &press, 1));
      WM_Event text = {.kind = WM_EventKind_Text, .window = ws->os, .source_key = copy.key, .character = 'k'};
      EditCheck(uishell_route_edit_activation(arena, ws, &text, 1));
      text.source_key = WM_Key_J;
      EditCheck(!uishell_route_edit_activation(arena, ws, &text, 1));
      text.source_key = WM_Key_Null; // composition/synthetic text remains independent
      EditCheck(!uishell_route_edit_activation(arena, ws, &text, 1));
      WM_Event release = {.kind = WM_EventKind_Release, .window = ws->os, .key = copy.key, .modifiers = mods};
      EditCheck(!uishell_route_edit_activation(arena, &windows[1-w], &release, 1));
      EditCheck(uishell_route_edit_activation(arena, ws, &release, 1));
      EditCheck(!uishell_route_edit_activation(arena, ws, &release, 1));
      EditCheck(uishell_edit_drain_commands() == 1 && clip.writes == writes+1);
      EditCheck(str8_match(str8(clip.bytes, clip.size), str8_lit("abcdef\nghijkl\nmnopqr"), 0));
      EditCheck(t->fixture.sent_count == before_inputs);
    }
    WM_Event menu = {.kind = WM_EventKind_MenuCommand, .window = ws->os, .string = str8_lit("copy")};
    U64 writes = clip.writes;
    EditCheck(uishell_route_edit_activation(arena, ws, &menu, 1));
    EditCheck(uishell_edit_drain_commands() == 1 && clip.writes == writes+1);
    uishell_cmd("clear_selection");
    EditCheck(uishell_edit_drain_commands() == 1 && !t->fixture.tv.has_selection && t->fixture.sent_count == before_inputs);
    // No selection, terminal Cut and unsupported Undo/Redo remain ineffective even directly invoked.
    char *disabled[] = {"copy", "cut", "undo", "redo", "clear_selection"};
    for(U64 i = 0; i < ArrayCount(disabled); i++)
    {
      String8 command = str8_cstring(disabled[i]);
      EditCheck(!uishell_edit_command_enabled(command, ws));
      uishell_push_stored_cmd(command, uishell_regs());
      uishell_edit_drain_commands();
      EditCheck(clip.writes == writes+1 && t->fixture.sent_count == before_inputs);
    }
    // Paste reads once; later clipboard changes cannot change the structured Cleat payload.
    uishell_edit_clip_write(&clip, str8_lit("paste\n\x1b[200~ \xce\xbb"));
    clip.change_after_read = 1;
    U64 reads = clip.reads;
    uishell_cmd("paste");
    EditCheck(uishell_edit_drain_commands() == 1 && clip.reads == reads+1);
    EditCheck(t->fixture.sent_count == before_inputs+1);
    cleat_input_event input = t->fixture.sent[t->fixture.sent_count-1];
    EditCheck(input.kind == CLEAT_INPUT_PASTE && str8_match(str8((U8 *)input.text, input.text_len), str8_lit("paste\n\x1b[200~ \xce\xbb"), 0));
    clip.change_after_read = 0;
    // Empty clipboard still has one coherent snapshot and no invented key event.
    clip.size = 0; reads = clip.reads;
    U64 empty_inputs = t->fixture.sent_count;
    uishell_cmd("paste"); uishell_edit_drain_commands();
    EditCheck(clip.reads == reads+1 && t->fixture.sent_count == empty_inputs);
    state.popup_active = 1;
    EditCheck(!uishell_edit_command_enabled(str8_lit("copy"), ws));
    state.popup_active = 0;
    U64 owner_view = ws->ui->edit_owner_view;
    ws->ui->edit_owner_view = 0;
    EditCheck(!uishell_edit_command_enabled(str8_lit("paste"), ws));
    ws->ui->edit_owner_view = owner_view;
    // All reported transport/role combinations enforce disabled Paste before reading/sending.
    for(U32 connection = CLEAT_SESSION_CONNECTING; connection <= CLEAT_SESSION_CLOSED; connection++)
    for(U32 role = CLEAT_ROLE_UNKNOWN; role <= CLEAT_ROLE_CONTROLLER; role++)
    {
      t->connection = connection; t->role = role;
      B32 available = connection == CLEAT_SESSION_STREAMING && role == CLEAT_ROLE_CONTROLLER;
      EditCheck(uishell_edit_command_enabled(str8_lit("paste"), ws) == available);
      if(!available)
      {
        reads = clip.reads; U64 count = t->fixture.sent_count;
        uishell_cmd("paste"); uishell_edit_drain_commands();
        EditCheck(clip.reads == reads && t->fixture.sent_count == count);
      }
    }
    t->connection = CLEAT_SESSION_CLOSED;
    uishell_cmd("select_all"); uishell_edit_drain_commands();
    EditCheck(uishell_edit_command_enabled(str8_lit("copy"), ws));
    uishell_cmd("copy"); uishell_edit_drain_commands();
    EditCheck(str8_match(str8(clip.bytes, clip.size), str8_lit("abcdef\nghijkl\nmnopqr"), 0));
    t->connection = CLEAT_SESSION_STREAMING; t->role = CLEAT_ROLE_CONTROLLER;
    // Plain Ctrl-C/V/A are never host edits; real key input reaches Cleat.
    WM_Key plain_keys[] = {WM_Key_C, WM_Key_V, WM_Key_A};
    U32 plain_codes[] = {'c', 'v', 'a'};
    for(U64 i = 0; i < ArrayCount(plain_keys); i++)
    {
      WM_Event ctrl = {.kind = WM_EventKind_Press, .key = plain_keys[i], .modifiers = WM_Modifier_Ctrl};
      EditCheck(!uishell_route_edit_activation(arena, ws, &ctrl, 1));
      UI_Event physical = {.kind = UI_EventKind_Press, .key = ctrl.key, .modifiers = ctrl.modifiers};
      U64 count = t->fixture.sent_count;
      EditCheck(uishell_terminal_send_key_event(&t->fixture.tv, &physical));
      physical.kind = UI_EventKind_Release;
      EditCheck(!uishell_terminal_send_key_event(&t->fixture.tv, &physical));
      EditCheck(t->fixture.sent_count == count+1 && t->fixture.sent[count].kind == CLEAT_INPUT_KEY &&
        t->fixture.sent[count].key_action == CLEAT_KEY_ACTION_PRESS && t->fixture.sent[count].key_code == plain_codes[i]);
    }
  }

  // Kitty event-types can encode named-key releases. A rebound host F5 must
  // never reach that real terminal key consumer, while an independent F6 does.
  {
    uishell_edit_bindings(arena, state.cfg, user, 0);
    CFG_Node *bindings = cfg_node_child_from_string(user, str8_lit("keybindings"));
    CFG_Node *binding = cfg_node_new(state.cfg, bindings, str8_zero());
    cfg_node_new(state.cfg, binding, str8_lit("copy"));
    cfg_node_new(state.cfg, binding, str8_lit("f5"));
    cfg_node_new(state.cfg, binding, str8_lit("alt"));
    state.key_map = cfg_key_map_from_cfg(arena);
    RD_WindowState *ws = &windows[0];
    UIShell_EditTransport *t = &terminals[0][0];
    uishell_edit_set_window(ws, views[0][0]->id);
    uishell_terminal_register_edit_owner(ui_key_make(100), &t->fixture.tv, views[0][0]->id);
    uishell_cmd("select_all"); uishell_edit_drain_commands();
    U64 inputs = t->fixture.sent_count, writes = clip.writes;
    WM_Event events[] = {
      {.kind = WM_EventKind_Press, .key = WM_Key_F5, .modifiers = WM_Modifier_Alt},
      {.kind = WM_EventKind_Release, .key = WM_Key_F5, .modifiers = 0},
      {.kind = WM_EventKind_Press, .key = WM_Key_F6},
      {.kind = WM_EventKind_Release, .key = WM_Key_F6},
    };
    for(U64 i = 0; i < ArrayCount(events); i++)
    {
      B32 taken = uishell_route_edit_activation(arena, ws, &events[i], 1);
      EditCheck(taken == (i < 2));
      if(i == 1)
      {
        EditCheck(uishell_edit_drain_commands() == 1 && clip.writes == writes+1 && t->fixture.sent_count == inputs);
      }
      if(!taken)
      {
        UI_Event physical = {.kind = events[i].kind == WM_EventKind_Press ? UI_EventKind_Press : UI_EventKind_Release,
          .key = events[i].key, .modifiers = events[i].modifiers};
        EditCheck(uishell_terminal_send_key_event(&t->fixture.tv, &physical));
      }
    }
    EditCheck(uishell_edit_drain_commands() == 0);
    EditCheck(t->fixture.sent_count == inputs+2 && t->fixture.sent[inputs+1].key_action == CLEAT_KEY_ACTION_RELEASE);
    WM_Event focus_loss = {.kind = WM_EventKind_WindowLoseFocus};
    ws->edit_chord_held[WM_Key_F5] = 1;
    uishell_route_edit_activation(arena, ws, &focus_loss, 1);
    EditCheck(!ws->edit_chord_held[WM_Key_F5]);
  }

  // Text consumers take precedence over terminal output, including a palette/query
  // and settings input. They keep whole-input Select All and editable Cut behavior.
  for(U64 w = 0; w < 2; w++)
  for(U64 surface = 0; surface < 3; surface++)
  {
    RD_WindowState *ws = &windows[w];
    uishell_edit_set_window(ws, views[w][0]->id);
    UI_Key key = ui_key_make(200+w*3+surface);
    U8 buffer[64] = "alpha \xce\xbb"; U64 size = sizeof("alpha \xce\xbb")-1;
    TxtPt cursor = txt_pt(1, size+1), mark = txt_pt(1, 1);
    state.popup_active = surface == 1; ws->query_is_active = surface == 1;
    ui_register_text_edit_owner(key, cursor, mark);
    U64 inputs = terminals[w][0].fixture.sent_count;
    uishell_cmd("copy");
    EditCheck(uishell_edit_drain_commands() == 1);
    // An unrelated input cannot steal a targeted event.
    ui_state->edit_consumer_key = ui_key_make(key.u64[0]+1);
    UI_Event *event = 0;
    EditCheck(!ui_next_event(&event));
    ui_state->edit_consumer_key = ui_key_zero();
    ui_consume_text_edit_events(key, buffer, sizeof(buffer), &size, &cursor, &mark, 0);
    EditCheck(str8_match(str8(clip.bytes, clip.size), str8_lit("alpha \xce\xbb"), 0));
    uishell_cmd("cut"); uishell_edit_drain_commands();
    ui_consume_text_edit_events(key, buffer, sizeof(buffer), &size, &cursor, &mark, 0);
    EditCheck(size == 0 && cursor.column == 1 && mark.column == 1);
    EditCheck(!uishell_edit_command_enabled(str8_lit("copy"), ws) && !uishell_edit_command_enabled(str8_lit("undo"), ws));
    uishell_edit_clip_write(&clip, str8_lit("replace"));
    U64 reads = clip.reads;
    clip.change_after_read = 1;
    uishell_cmd("paste"); uishell_edit_drain_commands();
    ui_consume_text_edit_events(key, buffer, sizeof(buffer), &size, &cursor, &mark, 0);
    EditCheck(clip.reads == reads+1 && str8_match(str8(buffer, size), str8_lit("replace"), 0));
    clip.change_after_read = 0;
    uishell_cmd("select_all"); uishell_edit_drain_commands();
    ui_consume_text_edit_events(key, buffer, sizeof(buffer), &size, &cursor, &mark, 0);
    EditCheck(cursor.column == size+1 && mark.column == 1 && terminals[w][0].fixture.sent_count == inputs);
    // A replacement anchored at the selection start clamps both ends to capacity.
    uishell_edit_clip_write(&clip, str8_lit("0123456789"));
    uishell_cmd("paste"); uishell_edit_drain_commands();
    ui_consume_text_edit_events(key, buffer, 4, &size, &cursor, &mark, 0);
    EditCheck(size == 4 && str8_match(str8(buffer, size), str8_lit("0123"), 0) && cursor.column == 5 && mark.column == 5);
    clip.size = 0; reads = clip.reads;
    uishell_cmd("paste"); uishell_edit_drain_commands();
    ui_consume_text_edit_events(key, buffer, sizeof(buffer), &size, &cursor, &mark, 0);
    EditCheck(clip.reads == reads+1 && size == 4);
    cursor = txt_pt(1, 5); mark = txt_pt(1, 1);
    ui_register_text_edit_owner(key, cursor, mark);
    // Cursor/mark values cannot escape the valid range after a bounded edit.
    UI_TxtOp invalid_position = {.cursor = txt_pt(1, -99), .mark = txt_pt(1, 999)};
    ui_apply_text_edit_op(arena, invalid_position, buffer, sizeof(buffer), &size, &cursor, &mark);
    EditCheck(cursor.column == 1 && mark.column == size+1);
    ui_register_text_edit_owner(key, cursor, mark);
    // A queued command retains its original owner, including after popup/focus changes.
    U64 writes = clip.writes;
    uishell_cmd("copy");
    ui_register_text_edit_owner(ui_key_make(999), cursor, mark);
    uishell_edit_drain_commands();
    EditCheck(clip.writes == writes && ws->ui_events.count == 0);
    ws->ui->build_index += 2;
    EditCheck(!uishell_edit_command_enabled(str8_lit("paste"), ws));
    state.popup_active = 0; ws->query_is_active = 0;
  }

#if OS_WINDOWS
  // Zero-scan IME/synthetic WM_CHAR must not acquire a physical chord owner.
  EditCheck(w32_wm_source_key_from_char_lparam(0) == WM_Key_Null);
  EditCheck(w32_wm_source_key_from_char_lparam(bit24|bit29|1) == WM_Key_Null);
#endif

  // All menu representations read the same command metadata and disabled-state model.
  RD_AppMenuSpecList menus = rd_app_menu_specs();
  RD_AppMenuSpec *edit_spec = 0;
  for(U64 i = 0; i < menus.count; i++)
  { if(str8_match(menus.v[i].label, str8_lit("Edit"), 0)) { edit_spec = &menus.v[i]; break; } }
  EditCheck(menus.count >= 5 && edit_spec && edit_spec->item_count == 9);
#if OS_MAC
  if(native && edit_spec)
  {
    // AppKit translation is followed all the way into the real shell packs and
    // text/terminal consumers, including a changed key window during tracking.
    NSMenu *saved_menu = [[NSApp mainMenu] retain];
    B32 saved_native = wm_application_menu_bar_is_native();
    wm_set_preferred_native_menu_bar(1);
    for(U64 rebound = 0; rebound < 2; rebound++)
    for(U64 editor = 0; editor < 2; editor++)
    {
      uishell_edit_bindings(arena, state.cfg, user, rebound);
      RD_WindowState *ws = &windows[0];
      UIShell_EditTransport *t = &terminals[0][0];
      UI_Key key = ui_key_make(700+editor);
      U8 buffer[64] = "native field"; U64 size = sizeof("native field")-1;
      TxtPt cursor = txt_pt(1, size+1), mark = txt_pt(1, 1);
      uishell_edit_set_window(ws, views[0][0]->id);
      if(editor) { ui_register_text_edit_owner(key, cursor, mark); }
      else
      {
        uishell_terminal_register_edit_owner(key, &t->fixture.tv, views[0][0]->id);
        uishell_cmd("select_all"); uishell_edit_drain_commands();
      }
      MAC_WM_Window *target = mac_wm_window_from_handle(ws->os);
      [target->ns_window makeKeyAndOrderFront:0];
      mac_wm_set_focused_window(target);
      wm_get_events(arena, 0);
      WM_MenuItem items[9] = {0};
      for(U64 i = 0; i < edit_spec->item_count; i++)
      { items[i] = uishell_menu_item_from_spec(&edit_spec->items[i], ws); }
      WM_Menu menu = {str8_lit("Edit"), ArrayCount(items), items};
      wm_set_main_menu((WM_MenuArray){1, &menu});
      NSMenu *edit_menu = [[[NSApp mainMenu] itemAtIndex:1] submenu];
      NSMenuItem *copy = [edit_menu itemAtIndex:4];
      EditCheck([copy isEnabled] && ![[edit_menu itemAtIndex:0] isEnabled]);
      // A disabled native item is ineffective even when its target is called directly.
      U64 writes = clip.writes;
      [mac_wm_state->menu_target menuItemSelected:[edit_menu itemAtIndex:0]];
      WM_EventList disabled_events = wm_get_events(arena, 0);
      for(WM_Event *e = disabled_events.first; e; e = e->next)
      { EditCheck(e->kind != WM_EventKind_MenuCommand); }
      NSEvent *press = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
        modifierFlags:rebound ? NSEventModifierFlagOption : NSEventModifierFlagCommand
        timestamp:[[NSProcessInfo processInfo] systemUptime] windowNumber:[target->ns_window windowNumber]
        context:0 characters:rebound ? @"k" : @"c" charactersIgnoringModifiers:rebound ? @"k" : @"c"
        isARepeat:NO keyCode:rebound ? 40 : 8];
      [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidBeginTrackingNotification object:[NSApp mainMenu]];
      [mac_wm_state->menu_target menuWillOpen:edit_menu];
      // Tracking snapshots the invoking window, so another focused window cannot steal Copy.
      mac_wm_set_focused_window(mac_wm_window_from_handle(windows[1].os));
      [NSApp postEvent:press atStart:NO];
      NSEvent *repeat = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
        modifierFlags:[press modifierFlags] timestamp:[[NSProcessInfo processInfo] systemUptime]
        windowNumber:[target->ns_window windowNumber] context:0 characters:[press characters]
        charactersIgnoringModifiers:[press charactersIgnoringModifiers] isARepeat:YES keyCode:[press keyCode]];
      [NSApp postEvent:repeat atStart:NO];
      WM_EventList tracked = wm_get_events(arena, 0);
      U64 activations = 0;
      for(WM_Event *e = tracked.first; e; e = e->next)
      {
        EditCheck(e->kind != WM_EventKind_Press && e->kind != WM_EventKind_Text);
        if(e->kind == WM_EventKind_MenuCommand)
        {
          EditCheck(wm_window_match(e->window, ws->os));
          EditCheck(uishell_route_edit_activation(arena, ws, e, !editor));
          activations++;
        }
      }
      EditCheck(activations == 1 && uishell_edit_drain_commands() == 1);
      if(editor) { ui_consume_text_edit_events(key, buffer, sizeof(buffer), &size, &cursor, &mark, 0); }
      EditCheck(clip.writes == writes+1);
      EditCheck(str8_match(str8(clip.bytes, clip.size), editor ? str8_lit("native field") : str8_lit("abcdef\nghijkl\nmnopqr"), 0));
      [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidEndTrackingNotification object:[NSApp mainMenu]];
      NSEvent *release = [NSEvent keyEventWithType:NSEventTypeKeyUp location:NSZeroPoint modifierFlags:0
        timestamp:[[NSProcessInfo processInfo] systemUptime] windowNumber:[target->ns_window windowNumber]
        context:0 characters:rebound ? @"k" : @"c" charactersIgnoringModifiers:rebound ? @"k" : @"c"
        isARepeat:NO keyCode:rebound ? 40 : 8];
      [NSApp postEvent:release atStart:NO];
      WM_EventList released = wm_get_events(arena, 0);
      for(WM_Event *e = released.first; e; e = e->next)
      { EditCheck(e->kind != WM_EventKind_Release && e->kind != WM_EventKind_Text); }
      // Clicking the item reaches the same semantic consumer and writes once.
      mac_wm_set_focused_window(target);
      writes = clip.writes;
      [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidBeginTrackingNotification object:[NSApp mainMenu]];
      [edit_menu performActionForItemAtIndex:4];
      [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidEndTrackingNotification object:[NSApp mainMenu]];
      tracked = wm_get_events(arena, 0);
      activations = 0;
      for(WM_Event *e = tracked.first; e; e = e->next)
      {
        if(e->kind == WM_EventKind_MenuCommand)
        { EditCheck(uishell_route_edit_activation(arena, ws, e, !editor)); activations++; }
      }
      EditCheck(activations == 1 && uishell_edit_drain_commands() == 1);
      if(editor) { ui_consume_text_edit_events(key, buffer, sizeof(buffer), &size, &cursor, &mark, 0); }
      EditCheck(clip.writes == writes+1);
    }
    // A missed tracking key-up cannot swallow a fresh press after focus loss.
    {
      MAC_WM_Window *target = mac_wm_window_from_handle(windows[0].os);
      mac_wm_state->menu_key_held[WM_Key_K] = 1;
      [target->delegate windowDidResignKey:0];
      NSEvent *fresh = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint modifierFlags:0
        timestamp:[[NSProcessInfo processInfo] systemUptime] windowNumber:[target->ns_window windowNumber]
        context:0 characters:@"k" charactersIgnoringModifiers:@"k" isARepeat:NO keyCode:40];
      [NSApp postEvent:fresh atStart:NO];
      WM_EventList events = wm_get_events(arena, 0);
      U64 presses = 0;
      for(WM_Event *event = events.first; event; event = event->next)
      { if(event->kind == WM_EventKind_Press && event->key == WM_Key_K) { presses++; } }
      EditCheck(presses == 1 && !mac_wm_state->menu_key_held[WM_Key_K]);
    }
    // Real Cocoa clipboard bytes go through the structured paste consumer too.
    RD_WindowState *ws = &windows[0];
    uishell_edit_set_window(ws, views[0][0]->id);
    UIShell_EditTransport *t = &terminals[0][0];
    uishell_terminal_register_edit_owner(ui_key_make(799), &t->fixture.tv, views[0][0]->id);
    wm_clipboard_io = (WM_ClipboardIO){0};
    wm_set_clipboard_text(str8_lit("Cocoa clipboard\n\xce\xbb"));
    U64 count = t->fixture.sent_count;
    uishell_cmd("paste"); uishell_edit_drain_commands();
    EditCheck(t->fixture.sent_count == count+1);
    cleat_input_event input = t->fixture.sent[t->fixture.sent_count-1];
    EditCheck(input.kind == CLEAT_INPUT_PASTE && str8_match(str8((U8 *)input.text, input.text_len), str8_lit("Cocoa clipboard\n\xce\xbb"), 0));
    wm_clipboard_io = (WM_ClipboardIO){&clip, uishell_edit_clip_write, uishell_edit_clip_read};
    wm_set_preferred_native_menu_bar(saved_native);
    [NSApp setMainMenu:saved_menu];
    [saved_menu release];
  }
#endif

  for(U64 w = 0; w < 2; w++)
  {
    for(U64 p = 0; p < 2; p++) { uishell_selection_fixture_release(&terminals[w][p].fixture); }
    if(native) { wm_window_close(windows[w].os); }
    ui_state_release(windows[w].ui);
  }
  for(RD_ViewState *vs = query_slots[0].first; vs; vs = vs->hash_next)
  { arena_release(vs->arena); ev_view_release(vs->ev_view); }
  cfg_state_release(state.cfg);
  arena_release(arena);
  rd_state = saved_rd; cfg_ctx_select(saved_cfg); ui_select_state(saved_ui); wm_clipboard_io = saved_clip;
#if OS_MAC
  if(native) { mac_wm_state->suppress_reentrant_frame = saved_reentrant; }
#endif
  fprintf(stderr, "semantic Edit command/focus/clipboard traces %s\n", ok ? "passed" : "failed");
#undef EditCheck
  return ok;
}

// Real custom menu buttons run under the existing shared UI diagnostics on
// every platform. The fixture replaces only the terminal process and clipboard.
internal B32
uishell_edit_menu_ui_diagnostics(RD_WindowState *ws)
{
  UI_State *saved_ui = ui_state, *saved_window_ui = ws->ui;
  UI_State *test = ui_state_alloc();
  UIShell_CmdList saved_commands[2] = {rd_state->cmds[0], rd_state->cmds[1]};
  Arena *saved_command_arenas[2] = {rd_state->cmds_arenas[0], rd_state->cmds_arenas[1]};
  U64 saved_generation = rd_state->cmds_gen;
  B32 saved_popup = rd_state->popup_active, saved_query = ws->query_is_active;
  WM_ClipboardIO saved_clipboard = wm_clipboard_io;
  Arena *arena = arena_alloc();
  rd_state->cmds[0] = (UIShell_CmdList){0}; rd_state->cmds[1] = (UIShell_CmdList){0};
  rd_state->cmds_arenas[0] = rd_state->cmds_arenas[1] = arena;
  rd_state->cmds_gen = 0; rd_state->popup_active = 0; ws->query_is_active = 0;
  ws->ui = test; ui_select_state(test);
  CFG_Node *view = cfg_node_new(rd_state->cfg, cfg_node_from_id(ws->cfg_id), str8_lit("terminal"));
  UIShell_SelectionFixture fixture; uishell_selection_fixture_init(&fixture);
  UIShell_EditClipboard clip = {0};
  wm_clipboard_io = (WM_ClipboardIO){&clip, uishell_edit_clip_write, uishell_edit_clip_read};
  B32 ok = 1;
#define MenuEditCheck(expr) do { if(!(expr)) { fprintf(stderr, "FAIL custom Edit menu %d: %s\n", __LINE__, #expr); ok = 0; } } while(0)
  UI_Key root_key = ui_key_make(88001), owner_key = ui_key_make(88002);
  RD_AppMenuSpecList specs = uishell_shell_edit_menu_specs();
  UIShell_RegsScope(.window = ws->cfg_id, .view = view->id, .edit_owner_key = ui_key_zero(), .edit_owner_captured = 0)
  {
    // Both full and compact menu contents consume the same action availability
    // and route through actual buttons, not a test-only activation callback.
    for(U64 compact = 0; compact < 2; compact++)
    for(U64 action = 0; action < 2; action++)
    {
      uishell_terminal_edit_dispatch(&fixture.tv, str8_lit("select_all"), str8_zero());
      UI_Key button_key = ui_key_from_string(root_key, action ? str8_lit("###cmd_cut") : str8_lit("###cmd_copy"));
      Vec2F32 mouse = v2f32(0, 0);
      for(U64 frame = 0; frame < 4; frame++)
      {
        UI_EventList events = {0};
        if(frame == 3)
        {
          UI_Box *button = ui_box_from_key(button_key);
          MenuEditCheck(!ui_box_is_nil(button));
          if(ui_box_is_nil(button)) { break; }
          mouse = center_2f32(button->rect);
          UI_Event press = {.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = mouse, .timestamp_us = 9000000};
          UI_Event release = {.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = mouse, .timestamp_us = 9000010};
          ui_event_list_push(arena, &events, &press); ui_event_list_push(arena, &events, &release);
        }
        UI_IconInfo icons = saved_window_ui->icon_info;
        UI_AnimationInfo animation = {0};
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        ui_state->mouse = mouse;
        ui_state->edit_menu_focus = 1;
        uishell_terminal_register_edit_owner(owner_key, &fixture.tv, view->id);
        ui_set_next_rect(r2f32p(0, 0, 400, 400));
        UI_Box *root = ui_build_box_from_key(0, root_key);
        UI_Parent(root) UI_ChildLayoutAxis(Axis2_Y)
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(16)
        UI_PrefWidth(ui_px(380, 1)) UI_PrefHeight(ui_px(28, 1))
        {
          if(compact) { rd_app_menu_spec_content(&specs.v[0]); }
          else { rd_app_menu_buttons(&specs.v[0]); }
        }
        ui_end_build();
      }
      UI_Box *button = ui_box_from_key(button_key);
      MenuEditCheck(!ui_box_is_nil(button) && !!(button->flags & UI_BoxFlag_Disabled) == !!action);
      U64 writes = clip.writes, inputs = fixture.sent_count;
      U64 dispatched = uishell_edit_drain_commands();
      MenuEditCheck(dispatched == (action ? 0 : 1));
      MenuEditCheck(clip.writes == writes+(action ? 0 : 1) && fixture.sent_count == inputs);
      if(!action) { MenuEditCheck(str8_match(str8(clip.bytes, clip.size), str8_lit("abcdef\nghijkl\nmnopqr"), 0)); }
    }
  }
  cfg_node_release(rd_state->cfg, view);
  uishell_selection_fixture_release(&fixture);
  wm_clipboard_io = saved_clipboard;
  ws->ui = saved_window_ui; ui_select_state(saved_ui);
  ui_state_release(test);
  rd_state->cmds[0] = saved_commands[0]; rd_state->cmds[1] = saved_commands[1];
  rd_state->cmds_arenas[0] = saved_command_arenas[0]; rd_state->cmds_arenas[1] = saved_command_arenas[1];
  rd_state->cmds_gen = saved_generation; rd_state->popup_active = saved_popup; ws->query_is_active = saved_query;
  arena_release(arena);
  fprintf(stderr, "full/compact custom Edit menu consumer traces %s\n", ok ? "passed" : "failed");
#undef MenuEditCheck
  return ok;
}

#if OS_LINUX
// Exercise the actual X11 producer, not a fabricated WM ordering. Physical
// presses precede their text, and plain/shifted typing reaches the real editor.
internal B32
uishell_edit_x11_text_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  wm_get_events(scratch.arena, 0);
  LNX_WM_Window *window = (LNX_WM_Window *)ws->os.u64[0];
  KeySym symbols[] = {XK_a, XK_b};
  unsigned int modifiers[] = {0, ShiftMask};
  for(U64 i = 0; i < ArrayCount(symbols); i++)
  for(U64 release = 0; release < 2; release++)
  {
    XEvent event = {0};
    event.xkey.type = release ? KeyRelease : KeyPress;
    event.xkey.display = lnx_wm_state->display;
    event.xkey.window = window->window;
    event.xkey.root = DefaultRootWindow(lnx_wm_state->display);
    event.xkey.same_screen = True;
    event.xkey.keycode = XKeysymToKeycode(lnx_wm_state->display, symbols[i]);
    event.xkey.state = modifiers[i];
    XSendEvent(lnx_wm_state->display, window->window, False, release ? KeyReleaseMask : KeyPressMask, &event);
  }
  XSync(lnx_wm_state->display, False);
  WM_EventList events = wm_get_events(scratch.arena, 0);
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  ui_select_state(test); UI_InitStacks(test);
  UI_EventList text_events = {0}; test->events = &text_events;
  WM_Key preceding = WM_Key_Null;
  U64 presses = 0, texts = 0, releases = 0;
  B32 ok = 1;
  for(WM_Event *event = events.first; event; event = event->next)
  {
    if(!wm_window_match(event->window, ws->os)) { continue; }
    if(event->kind == WM_EventKind_Press) { preceding = event->key; presses++; }
    if(event->kind == WM_EventKind_Release) { releases++; preceding = WM_Key_Null; }
    if(event->kind == WM_EventKind_Text)
    {
      ok &= preceding != WM_Key_Null && event->source_key == preceding;
      ok &= event->modifiers == (texts ? WM_Modifier_Shift : 0);
      U8 bytes[4]; U32 size = utf8_encode(bytes, event->character);
      UI_Event text = {.kind = UI_EventKind_Text, .string = push_str8_copy(scratch.arena, str8(bytes, size))};
      ui_event_list_push(scratch.arena, &text_events, &text);
      texts++;
    }
  }
  U8 buffer[16] = {0}; U64 size = 0;
  TxtPt cursor = txt_pt(1, 1), mark = cursor;
  ui_consume_text_edit_events(ui_key_make(99001), buffer, sizeof(buffer), &size, &cursor, &mark, 0);
  ok &= presses == 2 && texts == 2 && releases == 2 && str8_match(str8(buffer, size), str8_lit("aB"), 0);
  ui_select_state(saved_ui); ui_state_release(test);
  scratch_end(scratch);
  fprintf(stderr, "X11 physical/text ordering and editor typing %s\n", ok ? "passed" : "failed");
  return ok;
}
#endif
