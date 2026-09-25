// One managed primary terminal slot. Reconciliation policy and tokens are owned
// by Andamento; this adapter owns configuration and terminal runtime operations.
internal void
uishell_managed_set(CFG_Node *node, String8 key, String8 value)
{
  CFG_Node *field = cfg_node_child_from_string_or_alloc(rd_state->cfg, node, key);
  cfg_node_new_replace(rd_state->cfg, field, value);
}

internal B32
uishell_managed_commit(UIShell_SidebarState *state, CFG_Node *workspace, CFG_Node *view, AndamentoContent *update)
{
  RD_ViewState *vs = rd_view_state_from_cfg(view);
  UIShell_TerminalViewState *tv = vs->user_data;
  // This content kind is an in-process command. Do not silently reinterpret
  // user-configured daemon attachment semantics as a different backend.
  if((tv && tv->daemon_backend) || cfg_node_child_from_string(view, str8_lit("daemon")) != &cfg_nil_node)
  { return 0; }
  cleat_session_colors colors = tv ? tv->session_colors : (cleat_session_colors){0};
  cleat_provider *provider = 0;
  cleat_session *session = 0;
  if(tv && tv->initialized)
  {
    cleat_provider_desc desc = {.abi_version = CLEAT_PROVIDER_ABI_VERSION,
      .requested_features = CLEAT_PROVIDER_FEATURE_CELL_SNAPSHOTS|CLEAT_PROVIDER_FEATURE_STRUCTURED_MOUSE_INPUT|
                            CLEAT_PROVIDER_FEATURE_RENDER_UPDATES|CLEAT_PROVIDER_FEATURE_IMAGE_STATE,
      .backend = CLEAT_PROVIDER_BACKEND_IN_PROCESS};
    provider = cleat_provider_open(&desc);
    cleat_session_desc target = {.cols = Max(tv->cols, 1), .rows = Max(tv->rows, 1),
      .cell_width_px = tv->cell_width_px, .cell_height_px = tv->cell_height_px,
      .colors = &colors, .vt_engine = CLEAT_PROVIDER_VT_GHOSTTY, .command = update->command.data, .command_len = update->command.len,
      .cwd = update->has_cwd ? update->cwd.data : 0, .cwd_len = update->has_cwd ? update->cwd.len : 0};
    if(provider) { session = cleat_session_create(provider, &target); }
    if(!session) { if(provider) { cleat_provider_close(provider); } return 0; }
  }
  char *error = 0;
  B32 valid = andamento_content_valid(state->core, workspace->id, update->token, &error);
  andamento_string_free(error);
  if(!valid || cfg_node_from_id(view->id) != view)
  {
    if(session) { cleat_session_destroy(session); }
    if(provider) { cleat_provider_close(provider); }
    return 0;
  }
  // All mutation and token validation happen on the UI thread; no ingress patch
  // can intervene between validation, installation and acknowledgement.
  if(tv) { uishell_terminal_runtime_release(tv); }
  UIShell_RegsScope(.view = view->id) { rd_store_view_expr_string(uishell_sidebar_string(update->command)); }
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("session")));
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("daemon_name")));
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("cwd")));
  if(update->has_cwd) { uishell_managed_set(view, str8_lit("cwd"), uishell_sidebar_string(update->cwd)); }
  uishell_managed_set(view, str8_lit("managed_target"), uishell_sidebar_string(update->target));
  if(session)
  {
    tv->session_colors = colors;
    tv->initialized = 1;
    tv->provider = provider;
    tv->session = session;
    vs->release_user_data = uishell_terminal_runtime_release;
    cleat_provider_set_wake_callback(provider, uishell_terminal_provider_wake, tv);
  }
  rd_request_frame();
  return 1;
}

internal void
uishell_sidebar_reconcile_workspace(UIShell_SidebarState *state, CFG_Node *workspace)
{
  String8 kind = cfg_node_child_from_string(workspace, str8_lit("sidebar_entity_kind"))->first->string;
  String8 id = cfg_node_child_from_string(workspace, str8_lit("sidebar_entity_id"))->first->string;
  if(!kind.size || !id.size) { return; }
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *panels = cfg_node_child_from_string(workspace, str8_lit("panels"));
  CFG_PanelTree tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
  CFG_Node *primary = &cfg_nil_node;
  U64 matches = 0;
  for(CFG_PanelNode *p = tree.root; p != &cfg_nil_panel_node; p = cfg_panel_node_rec__depth_first_pre(tree.root, p).next)
  {
    for(CFG_NodePtrNode *tab = p->tabs.first; tab; tab = tab->next)
    {
      if(str8_match(cfg_node_child_from_string(tab->v, str8_lit("resource_id"))->first->string, str8_lit("primary"), 0))
      { primary = tab->v; matches++; }
    }
  }
  // Missing/ambiguous/user-replaced slots are not a license to overwrite a view.
  if(matches == 1 && str8_match(primary->string, str8_lit("terminal"), 0))
  {
    String8 target = cfg_node_child_from_string(primary, str8_lit("managed_target"))->first->string;
    CFG_Node *cwd = cfg_node_child_from_string(primary, str8_lit("cwd"));
    char *error = 0;
    AndamentoContentPlan *plan = andamento_content_plan(state->core, workspace->id,
      uishell_sidebar_text(kind), uishell_sidebar_text(id), uishell_sidebar_text(target),
      uishell_sidebar_text(rd_expr_from_cfg(primary)), cwd != &cfg_nil_node,
      uishell_sidebar_text(cwd->first->string), &error);
    if(uishell_sidebar_result(state, plan != 0, error))
    {
      AndamentoContent content = {0};
      if(andamento_content_get(plan, &content) && content.state == ANDAMENTO_CONTENT_UPDATING)
      {
        B32 success = uishell_managed_commit(state, workspace, primary, &content);
        error = 0;
        andamento_content_complete(state->core, workspace->id, content.token, success, &error);
        andamento_string_free(error);
        if(!success)
        {
          state->managed_error_workspace = workspace->id;
          snprintf((char *)state->error, sizeof(state->error), "Managed terminal update failed; select its workspace to retry");
        }
        else if(state->managed_error_workspace == workspace->id)
        { state->managed_error_workspace = 0; state->error[0] = 0; }
      }
      andamento_content_release(plan);
    }
  }
  scratch_end(scratch);
}
