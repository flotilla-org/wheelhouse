//- The Automation Endpoint's read routes (#554;
// docs/protocol/automation-endpoint.md). At each frame's end the ingress
// asks for the Semantic Tree while a client waits or has read recently:
// every window's core gives its workspaces (wheelhouse_semantic_workspaces),
// and this adds what only the UI knows, by Automation ID: which workspace
// each window shows, and each terminal's session, size and, when asked,
// screen.

// The `automation` user setting: off, observe or full (the default).
internal U32
uishell_automation_mode(void)
{
  CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  String8 value = cfg_node_child_from_string(user, str8_lit("automation"))->first->string;
  if(str8_match(value, str8_lit("off"), StringMatchFlag_CaseInsensitive)) { return WHEELHOUSE_AUTOMATION_OFF; }
  if(str8_match(value, str8_lit("observe"), StringMatchFlag_CaseInsensitive)) { return WHEELHOUSE_AUTOMATION_OBSERVE; }
  return WHEELHOUSE_AUTOMATION_FULL;
}

// Whether `automation_terminal_text` lets clients read terminals' screens.
internal B32
uishell_automation_terminal_text(void)
{
  CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  String8 value = cfg_node_child_from_string(user, str8_lit("automation_terminal_text"))->first->string;
  return str8_match(value, str8_lit("screen"), StringMatchFlag_CaseInsensitive);
}

// A View's Automation ID, `<owner>/view/<key>[@previous]`, mirroring its
// View Address; empty for a View owned by none it can name.
internal String8
uishell_automation_view_id(Arena *arena, CFG_Node *view)
{
  RD_ViewAddress address = rd_view_address_from_cfg(view);
  char *owner = address.owner == RD_ViewOwnerKind_Workspace ? "workspace" : address.owner == RD_ViewOwnerKind_Window ? "window" : 0;
  RD_ViewInstance instance = RD_ViewInstance_Current;
  String8 key = rd_view_key_from_cfg(view, &instance);
  if(owner == 0 || key.size == 0) { return str8_zero(); }
  return push_str8f(arena, "%s/%S/view/%S%s", owner, uishell_string_from_workspace_id(arena, address.id), key,
                    instance == RD_ViewInstance_Previous ? "@previous" : "");
}

// `text` as a JSON string, quotes included.
internal String8
uishell_automation_json(Arena *arena, String8 text)
{
  String8List parts = {0};
  str8_list_push(arena, &parts, str8_lit("\""));
  U64 start = 0;
  for(U64 i = 0; i < text.size; i++)
  {
    U8 c = text.str[i];
    if(c != '"' && c != '\\' && c >= 0x20) { continue; }
    str8_list_push(arena, &parts, str8_substr(text, r1u64(start, i)));
    str8_list_pushf(arena, &parts, c == '"' ? "\\\"" : c == '\\' ? "\\\\" : "\\u%04x", c);
    start = i+1;
  }
  str8_list_push(arena, &parts, str8_substr(text, r1u64(start, text.size)));
  str8_list_push(arena, &parts, str8_lit("\""));
  return str8_list_join(arena, &parts, 0);
}

// A terminal's visible rows as JSON strings, trailing blanks trimmed, and
// its cursor. Never scrollback: the cell cache holds only the screen.
internal String8
uishell_automation_screen(Arena *arena, UIShell_TerminalCellCache *cache)
{
  String8List rows = {0};
  for(U16 row = 0; row < cache->rows; row++)
  {
    String8List cells = {0};
    U64 kept = 0;
    for(U16 col = 0; col < cache->cols; col++)
    {
      U64 index = (U64)row*cache->cols + col;
      if(index >= cache->cell_count) { break; }
      cleat_cell *cell = &cache->cells[index];
      if(cell->width == CLEAT_CELL_WIDTH_SPACER_TAIL || cell->width == CLEAT_CELL_WIDTH_SPACER_HEAD) { continue; }
      if(cell->grapheme_count == 0) { str8_list_push(arena, &cells, str8_lit(" ")); continue; }
      for(U64 g = 0; g < cell->grapheme_count; g++)
      {
        U8 *utf8 = push_array(arena, U8, 4);
        str8_list_push(arena, &cells, str8(utf8, utf8_encode(utf8, cell->graphemes[g])));
      }
      kept = cells.node_count;
    }
    // Trailing blanks go.
    String8List trimmed = {0};
    U64 n = 0;
    for(String8Node *c = cells.first; c && n < kept; c = c->next, n++) { str8_list_push(arena, &trimmed, c->string); }
    str8_list_push(arena, &rows, uishell_automation_json(arena, str8_list_join(arena, &trimmed, 0)));
  }
  String8 text = str8_list_join(arena, &rows, &(StringJoin){.pre = str8_lit("["), .sep = str8_lit(","), .post = str8_lit("]")});
  if(rows.node_count == 0) { text = str8_lit("[]"); }
  return push_str8f(arena, ", \"text\": %S, \"cursor\": {\"row\": %u, \"col\": %u, \"visible\": %s}", text,
                    (U32)cache->cursor.row, (U32)cache->cursor.col, cache->cursor.visible ? "true" : "false");
}

// A terminal View's session ID: its Cleat endpoint's, the daemon session it
// saved, or its live session's when the session has one. In-process
// sessions have none yet (cleat#170).
internal String8
uishell_automation_session(Arena *arena, CFG_Node *view, UIShell_TerminalViewState *tv)
{
  String8 session = cfg_node_child_from_string(view, str8_lit("cleat_session"))->first->string;
  if(!session.size) { session = cfg_node_child_from_string(view, str8_lit("session"))->first->string; }
  cleat_str id = {0};
  cleat_session *live = tv ? uishell_terminal_session(tv) : 0;
  if(!session.size && live && cleat_session_id(live, &id) && id.len) { session = push_str8_copy(arena, str8((U8 *)id.ptr, id.len)); }
  return session;
}

// The ingress's observer: every window's workspaces, then what the UI adds.
internal U32
uishell_automation_observe(void *unused, U32 flags, WheelhouseIngressAutomationEmit emit, void *out)
{
  Temp scratch = scratch_begin(0, 0);
  U32 result = 1;
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  {
    UIShell_SidebarState *state = uishell_sidebar_init(ws);
    if(state->core == 0) { continue; }
    WheelhouseBytes bytes = {0};
    char *error = 0;
    if(!wheelhouse_semantic_workspaces(state->core, &bytes, &error))
    {
      fprintf(stderr, "Automation Endpoint: %s\n", error ? error : "no Semantic Tree");
      wheelhouse_string_free(error);
      result = 0;
      break;
    }
    emit(out, WHEELHOUSE_AUTOMATION_WORKSPACES, (WheelhouseIngressText){bytes.data, bytes.len}, (WheelhouseIngressText){0});
    wheelhouse_bytes_free(bytes);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, cfg_node_from_id(ws->cfg_id));
    if(split.inventory.selected)
    {
      String8 id = push_str8f(scratch.arena, "workspace/%S", uishell_string_from_workspace_id(scratch.arena, split.inventory.selected->workspace_id));
      String8 extra = str8_lit("{\"state\": [\"selected\"]}");
      emit(out, WHEELHOUSE_AUTOMATION_ANNOTATION, uishell_ingress_text(id), uishell_ingress_text(extra));
    }
    for(UIShell_MaterializedWorkspace *w = split.inventory.first; w; w = w->next)
    {
      RD_Arrangement *arrangement = rd_arrangement_live(w->mount.owner);
      for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
      {
        for(RD_ArrangementTab *tab = p->first_tab; tab; tab = tab->next)
        {
          CFG_Node *view = rd_cfg_from_view(tab->view);
          if(!str8_match(view->string, str8_lit("terminal"), 0)) { continue; }
          String8 id = uishell_automation_view_id(scratch.arena, view);
          if(!id.size) { continue; }
          RD_ViewState *vs = rd_view_state_from_view(tab->view, 0);
          UIShell_TerminalViewState *tv = vs != 0 && vs != &rd_nil_view_state ? vs->user_data : 0;
          String8List fields = {0};
          String8 session = uishell_automation_session(scratch.arena, view, tv);
          if(session.size) { str8_list_pushf(scratch.arena, &fields, "\"session\": %S", uishell_automation_json(scratch.arena, session)); }
          String8 cwd = cfg_node_child_from_string(view, str8_lit("cwd"))->first->string;
          if(cwd.size) { str8_list_pushf(scratch.arena, &fields, "\"cwd\": %S", uishell_automation_json(scratch.arena, cwd)); }
          if(tv && tv->cols && tv->rows) { str8_list_pushf(scratch.arena, &fields, "\"cols\": %u, \"rows\": %u", (U32)tv->cols, (U32)tv->rows); }
          String8 joined = str8_list_join(scratch.arena, &fields, &(StringJoin){.sep = str8_lit(", ")});
          String8 screen = (flags & WHEELHOUSE_AUTOMATION_TERMINAL_TEXT) && tv && tv->cell_cache.cells ?
            uishell_automation_screen(scratch.arena, &tv->cell_cache) : str8_zero();
          // The screen's fields follow the others with a comma of their own.
          if(!joined.size && screen.size) { screen = str8_skip(screen, 2); }
          String8 extra = push_str8f(scratch.arena, "{\"props\": {\"terminal\": {%S%S}}}", joined, screen);
          emit(out, WHEELHOUSE_AUTOMATION_ANNOTATION, uishell_ingress_text(id), uishell_ingress_text(extra));
        }
      }
    }
  }
  scratch_end(scratch);
  return result;
}

// At a frame's end: answer waiting reads (wheelhouse_ingress_automation).
internal void
uishell_automation_frame_end(void)
{
  if(uishell_ingress == 0) { return; }
  WheelhouseIngressAutomationHost host =
  {
    .mode = uishell_automation_mode(),
    .terminal_text = uishell_automation_terminal_text(),
    .observe = uishell_automation_observe,
  };
  wheelhouse_ingress_automation(uishell_ingress, &host);
}
