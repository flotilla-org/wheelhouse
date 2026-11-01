//- The Automation Endpoint's read routes (#554;
// docs/protocol/automation-endpoint.md). At each frame's end the ingress
// asks for the Semantic Tree while a client wants it: every window's core
// gives its workspaces (wheelhouse_semantic_workspaces), and this adds what
// only the UI knows: which workspace each window shows, and each terminal's
// session, size and, when the setting allows, screen. Rust builds the
// values.

// The `automation` user setting: off, observe or full (the default when
// missing). Any other value is off, as at launch (uishell_automation_enabled).
internal U32
uishell_automation_mode(void)
{
  CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  String8 value = cfg_node_child_from_string(user, str8_lit("automation"))->first->string;
  if(value.size == 0 || str8_match(value, str8_lit("full"), StringMatchFlag_CaseInsensitive)) { return WHEELHOUSE_AUTOMATION_FULL; }
  if(str8_match(value, str8_lit("observe"), StringMatchFlag_CaseInsensitive)) { return WHEELHOUSE_AUTOMATION_OBSERVE; }
  return WHEELHOUSE_AUTOMATION_OFF;
}

// Whether `automation_terminal_text` lets clients read terminals' screens.
internal B32
uishell_automation_terminal_text(void)
{
  CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  String8 value = cfg_node_child_from_string(user, str8_lit("automation_terminal_text"))->first->string;
  return str8_match(value, str8_lit("screen"), StringMatchFlag_CaseInsensitive);
}

// A View by its View Address, which Rust makes its Automation ID; owner
// none for a View owned by none it can name.
internal WheelhouseIngressView
uishell_automation_view(Arena *arena, CFG_Node *view)
{
  RD_ViewAddress address = rd_view_address_from_cfg(view);
  RD_ViewInstance instance = RD_ViewInstance_Current;
  String8 key = push_str8_copy(arena, rd_view_key_from_cfg(view, &instance));
  WheelhouseIngressView result = {0};
  result.owner = (address.owner == RD_ViewOwnerKind_Workspace ? WHEELHOUSE_AUTOMATION_OWNER_WORKSPACE :
                  address.owner == RD_ViewOwnerKind_Window ? WHEELHOUSE_AUTOMATION_OWNER_WINDOW : WHEELHOUSE_AUTOMATION_OWNER_NONE);
  MemoryCopy(result.id, address.id.v, sizeof(result.id));
  result.key = uishell_ingress_text(key);
  result.previous = instance == RD_ViewInstance_Previous;
  return result;
}

// A terminal's visible rows, trailing blanks trimmed. Never scrollback: the
// cell cache holds only the screen.
internal WheelhouseIngressText *
uishell_automation_screen(Arena *arena, UIShell_TerminalCellCache *cache)
{
  WheelhouseIngressText *rows = push_array(arena, WheelhouseIngressText, cache->rows);
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
      // A blank cell: none written, or a space.
      if(cell->grapheme_count == 0 || (cell->grapheme_count == 1 && cell->graphemes[0] == ' '))
      { str8_list_push(arena, &cells, str8_lit(" ")); continue; }
      for(U64 g = 0; g < cell->grapheme_count; g++)
      {
        U8 *utf8 = push_array(arena, U8, 4);
        str8_list_push(arena, &cells, str8(utf8, utf8_encode(utf8, cell->graphemes[g])));
      }
      kept = cells.total_size;
    }
    // Trailing blanks go.
    rows[row] = uishell_ingress_text(str8_prefix(str8_list_join(arena, &cells, 0), kept));
  }
  return rows;
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

// The ingress's observer: every window's workspaces, the one it shows, and
// what the UI knows of its terminals; their screens only when a client asked
// and the setting allows.
internal U32
uishell_automation_observe(void *unused, WheelhouseIngressAutomationBuild *out, U32 wanted)
{
  Temp scratch = scratch_begin(0, 0);
  U32 result = 1;
  B32 screens = wanted && uishell_automation_terminal_text();
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
      // 0 discards what out was given: nothing is published this frame.
      result = 0;
      break;
    }
    wheelhouse_ingress_automation_workspaces(out, (WheelhouseIngressText){bytes.data, bytes.len});
    wheelhouse_bytes_free(bytes);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, cfg_node_from_id(ws->cfg_id));
    if(split.inventory.selected) { wheelhouse_ingress_automation_selected(out, &split.inventory.selected->workspace_id.v); }
    for(UIShell_MaterializedWorkspace *w = split.inventory.first; w; w = w->next)
    {
      RD_Arrangement *arrangement = rd_arrangement_live(w->mount.owner);
      for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
      {
        for(RD_ArrangementTab *tab = p->first_tab; tab; tab = tab->next)
        {
          CFG_Node *view = rd_cfg_from_view(tab->view);
          if(!str8_match(view->string, str8_lit("terminal"), 0)) { continue; }
          RD_ViewState *vs = rd_view_state_from_view(tab->view, 0);
          UIShell_TerminalViewState *tv = vs != 0 && vs != &rd_nil_view_state ? vs->user_data : 0;
          WheelhouseIngressTerminal terminal = {0};
          terminal.view = uishell_automation_view(scratch.arena, view);
          terminal.session = uishell_ingress_text(uishell_automation_session(scratch.arena, view, tv));
          if(tv) { terminal.cols = tv->cols; terminal.rows = tv->rows; }
          if(screens && tv && tv->cell_cache.cells)
          {
            terminal.screen = uishell_automation_screen(scratch.arena, &tv->cell_cache);
            terminal.screen_rows = tv->cell_cache.rows;
            terminal.cursor_row = tv->cell_cache.cursor.row;
            terminal.cursor_col = tv->cell_cache.cursor.col;
            terminal.cursor_visible = tv->cell_cache.cursor.visible;
          }
          wheelhouse_ingress_automation_terminal(out, &terminal);
        }
      }
    }
  }
  scratch_end(scratch);
  return result;
}

// At a frame's end: the setting, and the tree while a client wants it
// (wheelhouse_ingress_automation).
internal void
uishell_automation_frame_end(void)
{
  if(uishell_ingress == 0) { return; }
  WheelhouseIngressAutomationHost host = {.mode = uishell_automation_mode(), .observe = uishell_automation_observe};
  wheelhouse_ingress_automation(uishell_ingress, &host);
}
