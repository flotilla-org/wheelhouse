//- The Automation Endpoint's read routes (#554;
// docs/protocol/automation-endpoint.md). At each frame's end the ingress
// asks for the Semantic Tree while a client wants it: every window's core
// gives its workspaces (wheelhouse_semantic_workspaces), and this adds what
// only the UI knows: which workspace each window shows, the View keys go to
// in each, and each terminal's session, size and, when the setting allows,
// screen. Rust builds the values.

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
    CFG_Node *window = cfg_node_from_id(ws->cfg_id);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    if(split.inventory.selected) { wheelhouse_ingress_automation_selected(out, &split.inventory.selected->workspace_id.v); }
    CFG_Node *focused = rd_cfg_from_view(uishell_workspace_mount_from_window(scratch.arena, window).panel_tree.focused->selected_tab);
    if(focused != &cfg_nil_node)
    {
      WheelhouseIngressView view = uishell_automation_view(scratch.arena, focused);
      wheelhouse_ingress_automation_focused(out, &view);
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


//- Commands (ADR 0014; #643). The Automation Endpoint lists the command
// tables as every registered pack gives them, and runs one as the palette
// would, with registers built from its target, from the frame's end, so it
// runs in the next frame. Its ticket rides in the registers of every
// command it pushes (UIShell_Regs.automation): one that would open a prompt
// refuses the invocation, and unless the command is about focus,
// focus_panel and bring_to_front are skipped while the window's focused
// panel stays. The first refusal, or user error it logs, is its result.

typedef struct UIShell_AutomationRun UIShell_AutomationRun;
struct UIShell_AutomationRun
{
  UIShell_AutomationRun *next;
  U64 ticket;
  B32 focus;
  // The target's window's focused panel before it ran.
  RD_PanelAddress focused;
  U32 status;
  String8 reason;
};

// Runs and the reasons given back live here; it is cleared only when no run
// is left, at the next invocation or intent, after Rust has copied every
// reason.
global Arena *uishell_automation_arena = 0;
global UIShell_AutomationRun *uishell_automation_runs = 0;

internal UIShell_AutomationRun *
uishell_automation_run_from_ticket(U64 ticket)
{
  for(UIShell_AutomationRun *run = uishell_automation_runs; run != 0; run = run->next)
  {
    if(run->ticket == ticket) { return run; }
  }
  return 0;
}

// The first refusal stands.
internal void
uishell_automation_refuse(UIShell_AutomationRun *run, U32 status, String8 reason)
{
  if(run->status == 0)
  {
    run->status = status;
    run->reason = push_str8_copy(uishell_automation_arena, reason);
  }
}

// The flags Rust reads (crates/wheelhouse-ingress/src/automation/commands.rs).
StaticAssert(UIShell_CmdFlag_ListInUI == (1<<0) && UIShell_CmdFlag_ListInTab == (1<<2) && UIShell_CmdFlag_Automation == (1<<6),
             uishell_automation_cmd_flags);

internal U32
uishell_automation_arg_from_slot(UIShell_AppRegSlot slot)
{
  switch(slot)
  {
    case UIShell_AppRegSlot_FilePath: return WHEELHOUSE_COMMAND_ARG_FILE_PATH;
    case UIShell_AppRegSlot_String:   return WHEELHOUSE_COMMAND_ARG_STRING;
    case UIShell_AppRegSlot_Cursor:   return WHEELHOUSE_COMMAND_ARG_CURSOR;
    case UIShell_AppRegSlot_Cfg:      return WHEELHOUSE_COMMAND_ARG_CFG;
    case UIShell_AppRegSlot_Vaddr:    return WHEELHOUSE_COMMAND_ARG_ADDRESS;
    case UIShell_AppRegSlot_CmdName:  return WHEELHOUSE_COMMAND_ARG_COMMAND;
    default: return WHEELHOUSE_COMMAND_ARG_NONE;
  }
}

// Every pack's every entry, in the order commands are looked up.
internal void
uishell_automation_commands(void *unused, WheelhouseIngressCommandTable *out)
{
  for(UIShell_CmdPack *pack = rd_state->first_cmd_pack; pack != 0; pack = pack->next)
  {
    U64 count = pack->cmd_count ? pack->cmd_count() : 0;
    for(U64 idx = 0; idx < count && pack->cmd_info_from_index; idx++)
    {
      UIShell_AppCmdInfo info = pack->cmd_info_from_index(idx);
      U32 arg = uishell_automation_arg_from_slot(info.query_slot);
      WheelhouseIngressCommand command =
      {
        .name = uishell_ingress_text(info.string),
        .display_name = uishell_ingress_text(info.display_name),
        .description = uishell_ingress_text(info.description),
        .tags = uishell_ingress_text(info.search_tags),
        .context = uishell_ingress_text(info.ctx_filter),
        .flags = info.flags,
        .arg = arg,
        .required = arg != WHEELHOUSE_COMMAND_ARG_NONE && (info.query_flags & UIShell_QueryFlag_Required) != 0,
      };
      wheelhouse_ingress_automation_command(out, &command);
    }
  }
}

// The live panel of `owner`'s whose ID the Semantic Tree gives as `id`: its
// number, or the word a provider's document named it by.
internal RD_ArrangementPanel *
uishell_automation_panel(Arena *arena, RD_PanelOwner owner, String8 id)
{
  RD_Arrangement *a = rd_arrangement_live(owner);
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    String8 named = p->doc_id.size ? p->doc_id : push_str8f(arena, "%I64u", p->id);
    if(str8_match(named, id, 0)) { return p; }
  }
  return &rd_nil_arrangement_panel;
}

internal U32
uishell_automation_invoke(void *unused, const WheelhouseIngressInvocation *in, WheelhouseIngressText *reason)
{
  Temp scratch = scratch_begin(0, 0);
  if(uishell_automation_arena == 0) { uishell_automation_arena = arena_alloc(); }
  if(uishell_automation_runs == 0) { arena_clear(uishell_automation_arena); }
  String8 name = str8((U8 *)in->command.data, in->command.len);
  UIShell_AppCmdInfo info = uishell_app_cmd_info_from_string(name);
  String8 why = {0};
  U32 status = 0;
  
  //- The registers its target names.
  const WheelhouseIngressView *named = &in->target.view;
  UIShell_WorkspaceId id = {0};
  MemoryCopy(id.v, named->id, sizeof(id.v));
  String8 key = str8((U8 *)named->key.data, named->key.len);
  B32 window_owned = named->owner == WHEELHOUSE_AUTOMATION_OWNER_WINDOW;
  CFG_Node *host = window_owned ? uishell_window_from_id(id) : cfg_node_from_id(uishell_workspace_cfg_id_from_id(id));
  CFG_Node *window = &cfg_nil_node;
  CFG_ID workspace = 0;
  RD_PanelAddress panel = rd_nil_panel_address;
  RD_ViewAddress view = rd_nil_view_address;
  if(info.string.size == 0) { status = 404; why = push_str8f(scratch.arena, "no command %S", name); }
  else if(in->target.kind != WHEELHOUSE_TARGET_NONE && host == &cfg_nil_node)
  { status = 422; why = str8_lit("no workspace or window has the target's ID"); }
  else if(in->target.kind == WHEELHOUSE_TARGET_OWNER)
  {
    window = rd_window_from_cfg(host);
    if(!window_owned)
    {
      workspace = host->id;
      panel = rd_panel_address(rd_panel_owner_from_host(RD_PanelOwnerKind_Workspace, host), 0);
    }
  }
  else if(in->target.kind == WHEELHOUSE_TARGET_PANEL)
  {
    // A window's panels, as an Automation ID names them, are its sidebar's.
    RD_PanelOwner owner = {window_owned ? RD_PanelOwnerKind_Sidebar : RD_PanelOwnerKind_Workspace, id};
    RD_ArrangementPanel *p = uishell_automation_panel(scratch.arena, owner, key);
    if(p == &rd_nil_arrangement_panel) { status = 422; why = push_str8f(scratch.arena, "no panel %S there", key); }
    else
    {
      // With its Selected View, as the palette has the focused panel's.
      panel = rd_panel_address(owner, p->id);
      view = p->selected;
      window = rd_window_from_cfg(host);
      if(!window_owned) { workspace = host->id; }
    }
  }
  else if(in->target.kind == WHEELHOUSE_TARGET_VIEW)
  {
    view = rd_view_address_make(window_owned ? RD_ViewOwnerKind_Window : RD_ViewOwnerKind_Workspace, id, key,
                                named->previous ? RD_ViewInstance_Previous : RD_ViewInstance_Current);
    CFG_Node *view_cfg = rd_cfg_from_view(view);
    if(view_cfg == &cfg_nil_node) { status = 422; why = push_str8f(scratch.arena, "no view %S there", key); }
    else
    {
      panel = rd_panel_from_view(view);
      window = rd_window_from_cfg(view_cfg);
      if(!window_owned) { workspace = host->id; }
    }
  }
  if(status == 0 && in->target.kind != WHEELHOUSE_TARGET_NONE && window == &cfg_nil_node)
  { status = 422; why = str8_lit("no window shows the target"); }
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  if(status == 0 && uishell_is_edit_command(name) && !uishell_edit_command_enabled(name, ws))
  { status = 422; why = push_str8f(scratch.arena, "%S isn't available there now", name); }
  
  //- Queued with its argument, as the palette would once it had one.
  if(status == 0)
  {
    UIShell_AutomationRun *run = push_array(uishell_automation_arena, UIShell_AutomationRun, 1);
    run->ticket = in->ticket;
    run->focus = (info.flags & UIShell_CmdFlag_Focus) != 0;
    if(window != &cfg_nil_node) { run->focused = uishell_workspace_mount_from_window(scratch.arena, window).panel_tree.focused->address; }
    SLLStackPush(uishell_automation_runs, run);
    String8 text = str8((U8 *)in->text.data, in->text.len);
    UIShell_RegsScope(.window = window->id, .panel = panel, .tab = view, .view = view, .prev_tab = rd_nil_view_address,
                      .dst_panel = rd_nil_panel_address, .cfg = workspace, .automation = in->ticket)
    {
      switch(in->arg)
      {
        default: break;
        case WHEELHOUSE_COMMAND_ARG_FILE_PATH: { uishell_regs()->file_path = text; }break;
        case WHEELHOUSE_COMMAND_ARG_STRING:    { uishell_regs()->string = text; }break;
        case WHEELHOUSE_COMMAND_ARG_CURSOR:    { uishell_regs()->cursor = txt_pt((S64)in->line, (S64)in->column); }break;
        case WHEELHOUSE_COMMAND_ARG_CFG:       { uishell_regs_fill_slot_from_string(UIShell_ContextRegSlot_Cfg, info.query_expr, text); }break;
        case WHEELHOUSE_COMMAND_ARG_ADDRESS:   { uishell_regs()->vaddr = in->address; }break;
        case WHEELHOUSE_COMMAND_ARG_COMMAND:   { uishell_regs()->cmd_name = text; }break;
      }
      uishell_push_cmd_current(name);
    }
    rd_request_frame();
  }
  *reason = uishell_ingress_text(push_str8_copy(uishell_automation_arena ? uishell_automation_arena : scratch.arena, why));
  scratch_end(scratch);
  return status;
}

// At the end of the frame a run ran in. What a command defers past that
// frame (a file dialog's answer, say) isn't in its result.
internal U32
uishell_automation_settle(void *unused, U64 ticket, WheelhouseIngressText *reason)
{
  // A run lost track of is no success.
  U32 status = 500;
  *reason = uishell_ingress_text(str8_lit("the UI lost track of the invocation"));
  for(UIShell_AutomationRun **link = &uishell_automation_runs; *link != 0; link = &(*link)->next)
  {
    if((*link)->ticket == ticket)
    {
      status = (*link)->status;
      *reason = uishell_ingress_text((*link)->reason);
      *link = (*link)->next;
      break;
    }
  }
  return status;
}

internal B32
uishell_automation_admit(String8 name, UIShell_Regs *regs)
{
  UIShell_AutomationRun *run = uishell_automation_run_from_ticket(regs->automation);
  if(run == 0) { return 1; }
  if(str8_match(name, str8_lit("push_query"), 0))
  {
    uishell_automation_refuse(run, 403, str8_lit("it would open a prompt, which an invocation never does"));
    return 0;
  }
  if(!run->focus && str8_match(name, str8_lit("bring_to_front"), 0)) { return 0; }
  // Focus may leave a panel the invocation closed, as it must.
  if(!run->focus && str8_match(name, str8_lit("focus_panel"), 0)) { return run->focused.id != 0 && !rd_panel_exists(run->focused); }
  return 1;
}

internal void
uishell_automation_ran(U64 ticket, String8 errors)
{
  UIShell_AutomationRun *run = uishell_automation_run_from_ticket(ticket);
  if(run != 0 && errors.size != 0) { uishell_automation_refuse(run, 422, str8_skip_chop_whitespace(errors)); }
}

//- Intents (ADR 0014's amendment; #643), applied at a frame's end; the
// answer waits for the next frame's end, by when the stores have committed
// what changed. Until the Core's stores take intents (#441), an arrangement
// intent is applied as native's commands apply one, to the owner's live
// arrangement (rd_arrangement_apply_intent), with a move or drop checked by
// the dock rules first; its request is the endpoint's ticket. A sidebar
// intent goes to the window's core, and its host effects are carried out
// as for a click.

typedef struct UIShell_AutomationIntentName UIShell_AutomationIntentName;
struct UIShell_AutomationIntentName
{
  String8 name;
  U32 kind;
};

read_only global UIShell_AutomationIntentName uishell_automation_arrangement_intents[] =
{
  {str8_lit_comp("select-tab"), WHEELHOUSE_INTENT_SELECT_TAB},
  {str8_lit_comp("move-tab"), WHEELHOUSE_INTENT_MOVE_TAB},
  {str8_lit_comp("close-tab"), WHEELHOUSE_INTENT_CLOSE_TAB},
  {str8_lit_comp("drop-tab"), WHEELHOUSE_INTENT_DROP_TAB},
  {str8_lit_comp("split-panel"), WHEELHOUSE_INTENT_SPLIT_PANEL},
  {str8_lit_comp("close-panel"), WHEELHOUSE_INTENT_CLOSE_PANEL},
  {str8_lit_comp("reorder-panel"), WHEELHOUSE_INTENT_REORDER_PANEL},
  {str8_lit_comp("resize-panel"), WHEELHOUSE_INTENT_RESIZE_PANEL},
  {str8_lit_comp("equalize-panel"), WHEELHOUSE_INTENT_EQUALIZE_PANEL},
};

read_only global UIShell_AutomationIntentName uishell_automation_sidebar_intents[] =
{
  {str8_lit_comp("copy-subject-url"), WHEELHOUSE_INTENT_COPY_SUBJECT_URL},
  {str8_lit_comp("activate-placement"), WHEELHOUSE_INTENT_ACTIVATE_PLACEMENT},
  {str8_lit_comp("activate"), WHEELHOUSE_INTENT_ACTIVATE},
  {str8_lit_comp("toggle-placement"), WHEELHOUSE_INTENT_TOGGLE_PLACEMENT},
  {str8_lit_comp("set-variable"), WHEELHOUSE_INTENT_SET_VARIABLE},
  {str8_lit_comp("toggle-display-variable"), WHEELHOUSE_INTENT_TOGGLE_DISPLAY_VARIABLE},
};

internal String8
uishell_automation_string(WheelhouseIngressText text)
{
  return str8((U8 *)text.data, text.len);
}

// The View an intent names, or nil when it names none that is there.
internal RD_ViewAddress
uishell_automation_intent_view(const WheelhouseIngressView *view)
{
  UIShell_WorkspaceId id = {0};
  MemoryCopy(id.v, view->id, sizeof(id.v));
  RD_ViewAddress address = rd_view_address_make(view->owner == WHEELHOUSE_AUTOMATION_OWNER_WINDOW ? RD_ViewOwnerKind_Window : RD_ViewOwnerKind_Workspace,
                                                id, uishell_automation_string(view->key), view->previous ? RD_ViewInstance_Previous : RD_ViewInstance_Current);
  return rd_cfg_from_view(address) != &cfg_nil_node ? address : rd_nil_view_address;
}

internal U32
uishell_automation_apply(void *unused, const WheelhouseIngressIntent *in, U64 *request, WheelhouseIngressText *reason)
{
  Temp scratch = scratch_begin(0, 0);
  if(uishell_automation_arena == 0) { uishell_automation_arena = arena_alloc(); }
  if(uishell_automation_runs == 0) { arena_clear(uishell_automation_arena); }
  String8 name = uishell_automation_string(in->name);
  UIShell_WorkspaceId id = {0};
  MemoryCopy(id.v, in->owner_id, sizeof(id.v));
  B32 window_owned = in->owner == WHEELHOUSE_AUTOMATION_OWNER_WINDOW;
  CFG_Node *host = window_owned ? uishell_window_from_id(id) : cfg_node_from_id(uishell_workspace_cfg_id_from_id(id));
  U32 status = 422;
  String8 why = {0};
  if(host == &cfg_nil_node) { why = str8_lit("no workspace or window has the owner's ID"); }
  else if(in->family == WHEELHOUSE_INGRESS_INTENT_ARRANGEMENT)
  {
    U32 kind = max_U32;
    for EachElement(i, uishell_automation_arrangement_intents)
    { if(str8_match(uishell_automation_arrangement_intents[i].name, name, 0)) { kind = uishell_automation_arrangement_intents[i].kind; break; } }
    RD_PanelOwner owner = {window_owned ? RD_PanelOwnerKind_Sidebar : RD_PanelOwnerKind_Workspace, id};
    String8 panels[] = {uishell_automation_string(in->panel), uishell_automation_string(in->after_panel)};
    RD_PanelID ids[ArrayCount(panels)] = {0};
    for EachElement(i, panels)
    {
      RD_ArrangementPanel *p = panels[i].size ? uishell_automation_panel(scratch.arena, owner, panels[i]) : &rd_nil_arrangement_panel;
      if(panels[i].size && p == &rd_nil_arrangement_panel && !why.size) { why = push_str8f(scratch.arena, "no panel %S there", panels[i]); }
      ids[i] = p->id;
    }
    RD_ViewAddress tab = in->has_tab ? uishell_automation_intent_view(&in->tab) : rd_nil_view_address;
    RD_ViewAddress after = in->has_after ? uishell_automation_intent_view(&in->after) : rd_nil_view_address;
    if(kind == max_U32) { why = push_str8f(scratch.arena, "no arrangement intent %S", name); }
    else if((in->has_tab && rd_view_address_is_nil(tab)) || (in->has_after && rd_view_address_is_nil(after)))
    { why = str8_lit("no such view there"); }
    if(!why.size)
    {
      WheelhouseArrangementIntent intent = rd_arrangement_intent(kind, owner);
      intent.panel = ids[0];
      intent.after_panel = ids[1];
      rd_arrangement_intent_tab(&intent, tab);
      rd_arrangement_intent_after(&intent, after);
      Dir2 dir = in->dir == WHEELHOUSE_INGRESS_DIR_LEFT ? Dir2_Left : in->dir == WHEELHOUSE_INGRESS_DIR_UP ? Dir2_Up :
                 in->dir == WHEELHOUSE_INGRESS_DIR_RIGHT ? Dir2_Right : in->dir == WHEELHOUSE_INGRESS_DIR_DOWN ? Dir2_Down : Dir2_Invalid;
      intent.dir = rd_arrangement_core_dir(dir);
      intent.delta = in->delta;
      intent.floor = in->floor;
      // The dock rules say why, as a user error.
      B32 allowed = 1;
      if(kind == WHEELHOUSE_INTENT_MOVE_TAB || kind == WHEELHOUSE_INTENT_DROP_TAB)
      {
        log_scope_begin();
        allowed = rd_dock_move_allowed(scratch.arena, "move", tab, rd_panel_address(owner, intent.panel), dir);
        why = str8_skip_chop_whitespace(log_scope_end(scratch.arena).strings[LogMsgKind_UserError]);
      }
      // TODO(#441): the Core's request, once its stores take intents.
      if(allowed && rd_arrangement_apply_intent(intent).applied) { status = 0; *request = in->ticket; rd_request_frame(); }
      else if(allowed) { why = push_str8f(scratch.arena, "%S doesn't apply to that arrangement", name); }
      else if(why.size == 0) { why = str8_lit("the dock rules don't allow that move"); }
    }
  }
  else if(in->family == WHEELHOUSE_INGRESS_INTENT_SIDEBAR)
  {
    WheelhouseIntent intent = {.kind = max_U32};
    for EachElement(i, uishell_automation_sidebar_intents)
    { if(str8_match(uishell_automation_sidebar_intents[i].name, name, 0)) { intent.kind = uishell_automation_sidebar_intents[i].kind; break; } }
    CFG_Node *window = window_owned ? host : rd_window_from_cfg(host);
    RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
    UIShell_SidebarState *state = ws != &rd_nil_window_state ? uishell_sidebar_init(ws) : 0;
    if(intent.kind == max_U32) { why = push_str8f(scratch.arena, "no sidebar intent %S", name); }
    else if(state == 0 || state->core == 0) { why = str8_lit("no window with a sidebar shows the owner"); }
    else
    {
      intent.key = (WheelhouseText){in->key.data, in->key.len};
      intent.entity = (WheelhouseEntity){{in->entity_provider.data, in->entity_provider.len}, {in->entity_kind.data, in->entity_kind.len},
                                         {in->entity_id.data, in->entity_id.len}};
      intent.name = (WheelhouseText){in->variable.data, in->variable.len};
      intent.has_value = in->has_value;
      intent.value = (WheelhouseText){in->value.data, in->value.len};
      uishell_sidebar_labels_invalidate(state);
      char *error = 0;
      if(wheelhouse_apply_intent(state->core, &intent, request, &error) == WHEELHOUSE_INTENT_ACCEPTED)
      {
        status = 0;
        UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
        uishell_sidebar_effects(state, &split);
        uishell_sidebar_refresh(state);
        rd_request_frame();
      }
      else { why = push_str8_copy(scratch.arena, str8_cstring(error ? error : "the sidebar refused it")); }
      wheelhouse_string_free(error);
    }
  }
  *reason = uishell_ingress_text(push_str8_copy(uishell_automation_arena, why));
  scratch_end(scratch);
  return status;
}

// At a frame's end: the setting, the tree while a client wants it, and
// what clients asked: those this frame did answered, invocations waiting
// queued for the next, and intents applied (wheelhouse_ingress_automation).
internal void
uishell_automation_frame_end(void)
{
  if(uishell_ingress == 0) { return; }
  WheelhouseIngressAutomationHost host =
  {
    .mode = uishell_automation_mode(),
    .observe = uishell_automation_observe,
    .commands = uishell_automation_commands,
    .invoke = uishell_automation_invoke,
    .settle = uishell_automation_settle,
    .apply = uishell_automation_apply,
  };
  wheelhouse_ingress_automation(uishell_ingress, &host);
}
