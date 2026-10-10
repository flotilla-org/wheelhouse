// Managed content: a terminal's runtime half of an update Andamento plans.
// Reconciliation policy and tokens are owned by Andamento; this adapter owns
// configuration and terminal runtime operations.
internal void
uishell_managed_set(CFG_Node *node, String8 key, String8 value)
{
  CFG_Node *field = cfg_node_child_from_string_or_alloc(rd_state->cfg, node, key);
  cfg_node_new_replace(rd_state->cfg, field, value);
}

// A terminal update's new in-process session, started before the update's
// token is checked so the current one is untouched until it commits. Nothing
// to prepare (0 out) when the View hasn't started its terminal yet: it starts
// from its config, which committing rewrites. Returns 0 when it couldn't
// start one.
typedef struct UIShell_ManagedPrepared UIShell_ManagedPrepared;
struct UIShell_ManagedPrepared
{
  cleat_provider *provider;
  cleat_session *session;
  cleat_session_colors colors;
};

internal B32
uishell_managed_prepare(UIShell_TerminalViewState *tv, String8 command, B32 has_cwd, String8 cwd, UIShell_ManagedPrepared *out)
{
  MemoryZeroStruct(out);
  out->colors = tv ? tv->session_colors : (cleat_session_colors){0};
  if(!tv || !tv->initialized) { return 1; }
  cleat_provider_desc desc = {.abi_version = CLEAT_PROVIDER_ABI_VERSION,
    .requested_features = CLEAT_PROVIDER_FEATURE_CELL_SNAPSHOTS|CLEAT_PROVIDER_FEATURE_STRUCTURED_MOUSE_INPUT|
                          CLEAT_PROVIDER_FEATURE_RENDER_UPDATES|CLEAT_PROVIDER_FEATURE_IMAGE_STATE,
    .backend = CLEAT_PROVIDER_BACKEND_IN_PROCESS};
  out->provider = cleat_provider_open(&desc);
  cleat_session_desc target = {.cols = Max(tv->cols, 1), .rows = Max(tv->rows, 1),
    .cell_width_px = tv->cell_width_px, .cell_height_px = tv->cell_height_px,
    .colors = &out->colors, .vt_engine = CLEAT_PROVIDER_VT_GHOSTTY, .command = command.str, .command_len = command.size,
    .cwd = has_cwd ? cwd.str : 0, .cwd_len = has_cwd ? cwd.size : 0};
  if(out->provider) { out->session = cleat_session_create(out->provider, &target); }
  if(!out->session)
  {
    if(out->provider) { cleat_provider_close(out->provider); }
    MemoryZeroStruct(out);
    return 0;
  }
  return 1;
}

internal void
uishell_managed_discard(UIShell_ManagedPrepared *prepared)
{
  if(prepared->session) { cleat_session_destroy(prepared->session); }
  if(prepared->provider) { cleat_provider_close(prepared->provider); }
  MemoryZeroStruct(prepared);
}

// Commits a prepared update: the View's config runs `command` in `cwd`, its
// old session (and the session ID that named it) goes, and the prepared one
// takes its place.
internal void
uishell_managed_install(CFG_Node *view, UIShell_ManagedPrepared *prepared, String8 command, B32 has_cwd, String8 cwd,
                        B32 has_target, String8 target)
{
  RD_ViewState *vs = rd_view_state_from_cfg(view);
  UIShell_TerminalViewState *tv = vs->user_data;
  // All mutation and token validation happen on the UI thread; no ingress patch
  // can intervene between validation, installation and acknowledgement.
  if(tv) { uishell_terminal_runtime_release(tv); }
  UIShell_RegsScope(.view = view->id) { rd_store_view_expr_string(command); }
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("session")));
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("daemon_name")));
  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("cwd")));
  if(has_cwd) { uishell_managed_set(view, str8_lit("cwd"), cwd); }
  if(has_target) { uishell_managed_set(view, str8_lit("managed_target"), target); }
  if(prepared->session)
  {
    tv->session_colors = prepared->colors;
    tv->initialized = 1;
    tv->provider = prepared->provider;
    tv->session = prepared->session;
    uishell_terminal_clipboard_register(tv, view->id, uishell_regs()->window);
    vs->release_user_data = uishell_terminal_runtime_release;
    cleat_provider_set_wake_callback(prepared->provider, uishell_terminal_provider_wake, tv);
  }
  MemoryZeroStruct(prepared);
  rd_request_frame();
}

// Whether `view`'s terminal is hosted by a Cleat daemon, whose session is the
// daemon's rather than the View's, so an update never replaces it in place.
internal B32
uishell_managed_daemon_backed(CFG_Node *view)
{
  RD_ViewState *vs = rd_view_state_from_cfg(view);
  UIShell_TerminalViewState *tv = vs->user_data;
  if(cfg_node_child_from_string(view, str8_lit("daemon")) != &cfg_nil_node) { return 1; }
  if(tv && tv->initialized) { return tv->daemon_backend; }
  return cfg_node_child_from_string(view, str8_lit("session"))->first->string.size != 0;
}

internal void uishell_store_plan_owner(UIShell_SidebarState *state, CFG_Node *owner);

// Plans a workspace's managed content: each of its Slots follows the slot
// protocol (uishell_workspace_store.c, "Slot plans").
internal void
uishell_sidebar_reconcile_workspace(UIShell_SidebarState *state, CFG_Node *workspace)
{
  uishell_store_plan_owner(state, workspace);
}
