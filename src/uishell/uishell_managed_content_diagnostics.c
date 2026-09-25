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
  // Daemon-backed replacement is not supported in this slice; a new resolution
  // must leave such a view's content and saved command untouched.
  {
    CFG_Node *daemon = cfg_node_new(rd_state->cfg, view, str8_lit("daemon"));
    cfg_node_new(rd_state->cfg, daemon, str8_lit("1"));
    facts[1].text = uishell_sidebar_text(str8_lit("three"));
    facts[2].text = uishell_sidebar_text(str8_lit("printf D; read answer"));
    error = 0;
    andamento_apply_entity(state.core, 4, uishell_sidebar_text(str8_lit("project-role")),
      uishell_sidebar_text(str8_lit("p/governor")), uishell_sidebar_text(str8_lit("fixture")), facts, 3, &error);
    andamento_string_free(error);
    uishell_sidebar_reconcile_workspace(&state, workspace);
    ManagedCheck(tv->session == unchanged && str8_match(rd_expr_from_cfg(view), str8_lit("printf B; read answer"), 0) &&
                 str8_match(cfg_node_child_from_string(view, str8_lit("managed_target"))->first->string, str8_lit("two"), 0),
                 "daemon-backed primary view is never replaced");
    cfg_node_release(rd_state->cfg, daemon);
  }
  andamento_destroy(state.core);

  // Opening an entity from its current resolution through the production effect
  // path records the managed target, so the first plan does not restart it.
  {
    String8 config = str8_lit(
      "grouping \"roles\" { filter key=\"entity.kind\"; presence kind=\"role\" class=\"tab\"; level key=\"entity.id\"; }\n"
      "region \"tree\" source=\"tree\" root-template=\"roles\" form=\"compact\" placement=\"tree\"\n"
      "template \"roles\" slot=\"compact\" node-kind=\"entity\" { field \"label\" source=\"literal\" value=\"Roles\"; }\n"
      "placement \"tree\" { for \"role\" kind=\"role\" { apply-template \"role/entry\"; }; }\n"
      "template \"role/entry\" { field \"label\" key=\"entity.id\"; }\n");
    UIShell_SidebarState opened = {0};
    error = 0;
    opened.core = andamento_create(config.str, config.size, &error);
    andamento_string_free(error);
    ManagedCheck(opened.core != 0, "role sidebar core initializes");
    values[2] = "printf C; read answer";
    facts[0].text = uishell_sidebar_text(str8_lit("ready"));
    for(U32 i = 1; i < 3; i++) { facts[i].text = uishell_sidebar_text(str8_cstring(values[i])); }
    error = 0;
    andamento_apply_entity(opened.core, 4, uishell_sidebar_text(str8_lit("role")),
      uishell_sidebar_text(str8_lit("p/governor")), uishell_sidebar_text(str8_lit("fixture")), facts, 3, &error);
    andamento_string_free(error);
    error = 0;
    AndamentoSnapshot *snapshot = andamento_snapshot_acquire(opened.core, &error);
    andamento_string_free(error);
    size_t activate = ANDAMENTO_NONE;
    for(U64 i = 0; snapshot && i < andamento_snapshot_node_count(snapshot); i++)
    {
      AndamentoNode node = {0};
      if(andamento_snapshot_node(snapshot, i, &node) && str8_match(uishell_sidebar_string(node.entity_kind), str8_lit("role"), 0))
      { activate = node.activate; }
    }
    ManagedCheck(activate != ANDAMENTO_NONE && andamento_dispatch(opened.core, snapshot, activate, 0), "role entry dispatches its opening");
    andamento_snapshot_release(snapshot);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_effects(&opened, &split);
    CFG_Node *role_workspace = cfg_node_from_id(ws->root_controlled_split_selected_workspace_id);
    CFG_Node *role_view = &cfg_nil_node;
    CFG_Node *role_panels = cfg_node_child_from_string(role_workspace, str8_lit("panels"));
    CFG_PanelTree tree = cfg_panel_tree_from_panels_cfg(scratch.arena, role_panels, Axis2_X);
    if(tree.root != &cfg_nil_panel_node && tree.root->tabs.first) { role_view = tree.root->tabs.first->v; }
    ManagedCheck(str8_match(cfg_node_child_from_string(role_workspace, str8_lit("sidebar_entity_kind"))->first->string, str8_lit("role"), 0) &&
                 str8_match(cfg_node_child_from_string(role_view, str8_lit("managed_target"))->first->string, str8_lit("two"), 0),
                 "opened workspace records its current managed target");
    error = 0;
    AndamentoContentPlan *plan = andamento_content_plan(opened.core, role_workspace->id,
      uishell_sidebar_text(str8_lit("role")), uishell_sidebar_text(str8_lit("p/governor")),
      uishell_sidebar_text(cfg_node_child_from_string(role_view, str8_lit("managed_target"))->first->string),
      uishell_sidebar_text(rd_expr_from_cfg(role_view)), 0, (AndamentoText){0}, &error);
    andamento_string_free(error);
    AndamentoContent content = {0};
    ManagedCheck(plan && andamento_content_get(plan, &content) && content.state == ANDAMENTO_CONTENT_CURRENT,
                 "first plan for freshly opened content is current");
    andamento_content_release(plan);
    andamento_destroy(opened.core);
    cfg_node_release(rd_state->cfg, role_workspace);
  }
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
