// Native managed-content diagnostic using the shared Andamento reconciliation API.
// Uses isolated configuration, real terminal view rendering and in-process Cleat.
#if OS_MAC || OS_LINUX
#include <unistd.h>

internal B32
uishell_managed_diagnostic_marker(cleat_session *session, U32 marker)
{
  for(U32 attempt = 0; session && attempt < 200; attempt++)
  {
    cleat_snapshot snapshot = {0};
    B32 found = 0;
    if(cleat_session_snapshot(session, &snapshot))
    {
      if(snapshot.cell_count && snapshot.cells[0].grapheme_count)
      { found = snapshot.cells[0].graphemes[0] == marker; }
      cleat_session_release_snapshot(session, &snapshot);
    }
    if(found) { return 1; }
    usleep(10000);
  }
  return 0;
}

internal void
uishell_managed_diagnostic_frame(RD_WindowState *ws, CFG_Node *view)
{
  UI_IconInfo icons = ws->ui->icon_info;
  UI_AnimationInfo animation = {0};
  UI_EventList events = {0};
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  UIShell_RegsScope(.window = ws->cfg_id, .panel = view->parent->id, .view = view->id)
  UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
  {
    E_Eval eval = {0};
    RD_VIEW_UI_FUNCTION_NAME(terminal)(eval, r2f32p(0, 0, 640, 480));
  }
  ui_end_build();
}

internal B32
uishell_managed_content_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  UI_State *saved = ui_state, *test = ui_state_alloc();
  ui_select_state(test);
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *workspace = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  CFG_Node *panels = cfg_node_new(rd_state->cfg, workspace, str8_lit("panels"));
  CFG_Node *view = rd_cfg_new_view_tab(panels, str8_lit("terminal"), str8_lit("printf A; read answer"), 1);
  CFG_Node *slot = cfg_node_new(rd_state->cfg, view, str8_lit("resource_id"));
  cfg_node_new(rd_state->cfg, slot, str8_lit("primary"));
  CFG_Node *personal = rd_cfg_new_view_tab(panels, str8_lit("terminal_fixture"), str8_zero(), 0);
  CFG_ID workspace_id = workspace->id, view_id = view->id, personal_id = personal->id;
  U32 failures = 0;
#define ManagedCheck(c, label) do { B32 ok = (c); fprintf(stderr, "%s managed terminal: %s\n", ok ? "PASS" : "FAIL", label); failures += !ok; } while(0)
  uishell_managed_diagnostic_frame(ws, view);
  UIShell_TerminalViewState *tv = rd_view_state_from_cfg(view)->user_data;
  ManagedCheck(uishell_managed_diagnostic_marker(tv->session, 'A'), "original live terminal emits A");
  UIShell_RegsScope(.window = window->id, .panel = panels->id, .dst_panel = panels->id,
                   .view = view->id, .dir2 = Dir2_Right)
  { uishell_dispatch_panel_command(str8_lit("split_panel")); }
  CFG_Node *moved_panel = view->parent;
  uishell_managed_diagnostic_frame(ws, view);
  ManagedCheck(view->parent != personal->parent, "production split command moves managed view beside user view");

  // Exercise real producer facts -> shared plan -> native commit -> completion.
  UIShell_SidebarState state = {0};
  char *error = 0;
  state.core = andamento_create(0, 0, &error);
  ManagedCheck(state.core != 0, "shared core initializes");
  andamento_string_free(error);
  uishell_managed_set(workspace, str8_lit("sidebar_entity_kind"), str8_lit("project-role"));
  uishell_managed_set(workspace, str8_lit("sidebar_entity_id"), str8_lit("p/governor"));
  uishell_managed_set(view, str8_lit("managed_target"), str8_lit("one"));
  CFG_Node *session = cfg_node_new(rd_state->cfg, view, str8_lit("session"));
  cfg_node_new(rd_state->cfg, session, str8_lit("obsolete-session"));
  AndamentoFact facts[3] = {0};
  char *keys[] = {"workspace.primary.state", "workspace.primary.target", "action.primary.recipe"};
  char *values[] = {"ready", "two", "printf B; read answer"};
  for(U32 i = 0; i < 3; i++)
  {
    facts[i].key = uishell_sidebar_text(str8_cstring(keys[i]));
    facts[i].kind = ANDAMENTO_FACT_TEXT;
    facts[i].text = uishell_sidebar_text(str8_cstring(values[i]));
  }
  error = 0;
  B32 applied = andamento_apply_entity(state.core, 1, uishell_sidebar_text(str8_lit("project-role")),
    uishell_sidebar_text(str8_lit("p/governor")), uishell_sidebar_text(str8_lit("fixture")), facts, 3, &error);
  ManagedCheck(applied, "desired replacement facts accepted");
  andamento_string_free(error);
  uishell_sidebar_reconcile_workspace(&state, workspace);
  uishell_managed_diagnostic_frame(ws, view);
  ManagedCheck(uishell_managed_diagnostic_marker(tv->session, 'B'), "same terminal view now receives B via shared reconciliation");
  ManagedCheck(workspace->id == workspace_id && view->id == view_id && view->parent == moved_panel,
               "workspace, view identity and moved panel survive replacement");
  ManagedCheck(cfg_node_from_id(personal_id) == personal && personal->parent != moved_panel,
               "user-added view remains in its panel");
  ManagedCheck(cfg_node_child_from_string(view, str8_lit("session")) == &cfg_nil_node &&
               str8_match(rd_expr_from_cfg(view), str8_lit("printf B; read answer"), 0),
               "saved config resolves new command without obsolete session ID");
  ManagedCheck(str8_match(slot->first->string, str8_lit("primary"), 0), "managed slot identity retained");
  cleat_session *unchanged = tv->session;
  uishell_sidebar_reconcile_workspace(&state, workspace);
  ManagedCheck(tv->session == unchanged, "repeated observation does not restart terminal");
  facts[0].text = uishell_sidebar_text(str8_lit("held"));
  error = 0;
  andamento_apply_entity(state.core, 2, uishell_sidebar_text(str8_lit("project-role")),
    uishell_sidebar_text(str8_lit("p/governor")), uishell_sidebar_text(str8_lit("fixture")), facts, 1, &error);
  andamento_string_free(error);
  uishell_sidebar_reconcile_workspace(&state, workspace);
  ManagedCheck(tv->session == unchanged, "explicit held state retains existing content");
  facts[0].text = uishell_sidebar_text(str8_lit("ready"));
  error = 0;
  andamento_apply_entity(state.core, 3, uishell_sidebar_text(str8_lit("project-role")),
    uishell_sidebar_text(str8_lit("p/governor")), uishell_sidebar_text(str8_lit("fixture")), facts, 3, &error);
  andamento_string_free(error);
  uishell_sidebar_reconcile_workspace(&state, workspace);
  ManagedCheck(tv->session == unchanged, "unchanged resolution after held interval does not restart");
  andamento_destroy(state.core);
  uishell_terminal_runtime_release(tv);
  cfg_node_release(rd_state->cfg, workspace);
  ui_select_state(saved);
  ui_state_release(test);
  scratch_end(scratch);
  fprintf(stderr, "Managed content diagnostics: %u failures\n", failures);
#undef ManagedCheck
  return failures == 0;
}
#endif
