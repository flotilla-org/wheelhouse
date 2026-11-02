//- A workspace's lifecycle and home (#631). The core keeps each
// workspace's Life (whether it is kept, and its Workspace Home) and decides
// what opening, closing and re-homing one does (crates/wheelhouse-core/src/
// lifecycle.rs). Native makes the Workspace ID, sends the intent through one
// entry point, and carries out the effects on the config tree: a
// `workspace` node opened with that ID and its default panels, one closed
// and kept as `detached_workspace` (a Kept Workspace) or released, and the
// one shown after. Selection and the Visible Workspace stay native.

internal UIShell_SidebarState *uishell_sidebar_init(RD_WindowState *ws);
// Set when a SHOW_WORKSPACE effect is carried out.
global B32 uishell_workspace_shown;
internal UIShell_SidebarState *uishell_sidebar_window_state(CFG_Node *window);
internal void uishell_sidebar_observe(UIShell_SidebarState *state, UIShell_ControlledSplit *split);
internal void uishell_sidebar_effects(UIShell_SidebarState *state, UIShell_ControlledSplit *split);

// The one entry point for a lifecycle intent: observe the window's
// workspaces into its core first (so their order and selection are
// current), apply the intent, and carry out its effects before returning,
// so the workspace opens or closes in the same frame, as the commands did.
// Returns whether the core accepted it; a refusal changes nothing.
internal B32
uishell_workspace_lifecycle_apply(CFG_Node *window, WheelhouseIntent *intent)
{
  RD_WindowState *ws = window != &cfg_nil_node ? rd_window_state_from_cfg__existing(window) : &rd_nil_window_state;
  if(ws == &rd_nil_window_state) { return 0; }
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  if(!state->core) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  uishell_sidebar_observe(state, &split);
  char *error = 0;
  B32 accepted = wheelhouse_apply_intent(state->core, intent, 0, &error) == WHEELHOUSE_INTENT_ACCEPTED;
  // A refusal (a workspace not open, say) is no error to show.
  wheelhouse_string_free(error);
  if(accepted)
  {
    uishell_sidebar_effects(state, &split);
    uishell_sidebar_refresh(state);
    rd_request_frame();
  }
  scratch_end(scratch);
  return accepted;
}

// The window's workspace with this Workspace ID: a `workspace` or
// `detached_workspace` child, or the window itself when its own panels are
// its workspace; nil for none.
internal CFG_Node *
uishell_workspace_node_from_id(CFG_Node *window, UIShell_WorkspaceId id)
{
  CFG_Node *node = cfg_node_from_id(uishell_workspace_cfg_id_from_id(id));
  B32 found = node == window ||
    (node->parent == window && (str8_match(node->string, str8_lit("workspace"), 0) ||
                                str8_match(node->string, str8_lit("detached_workspace"), 0)));
  return found ? node : &cfg_nil_node;
}

// Opens a new subjectless workspace, selected, named `name` (empty: the
// lowest "Workspace N" no open or kept workspace uses), living in local
// group `group` or with project `project` (empty: neither). Returns its
// node, or nil if the core refused.
internal CFG_Node *
uishell_new_workspace_in(CFG_Node *window, String8 name, String8 group, String8 project)
{
  UIShell_WorkspaceId id = uishell_workspace_id_make();
  WheelhouseIntent intent = {.kind = WHEELHOUSE_INTENT_NEW_WORKSPACE, .workspace_id = uishell_sidebar_workspace(id),
    .name = uishell_sidebar_text(name), .group = uishell_sidebar_text(group), .project = uishell_sidebar_text(project)};
  if(!uishell_workspace_lifecycle_apply(window, &intent)) { return &cfg_nil_node; }
  return uishell_workspace_node_from_id(window, id);
}

// A subjectless workspace, selected, with default panels, in the default
// group. Window sidebar regions are left alone; they are not part of the
// new workspace.
internal CFG_Node *
uishell_new_workspace(CFG_Node *window)
{
  return uishell_new_workspace_in(window, str8_zero(), str8_zero(), str8_zero());
}

// Closes an open workspace: keeps it when `keep` and the core says it may
// (its subject hasn't ended), else destroys it, and shows the next. Until a
// permanent home workspace exists, the last close leaves a fresh subjectless
// workspace rather than an empty Workspace Region.
internal B32
uishell_workspace_close(CFG_Node *window, UIShell_WorkspaceId id, B32 keep)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  UIShell_SidebarState *state = ws != &rd_nil_window_state ? uishell_sidebar_init(ws) : 0;
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(id);
  // A workspace the core never registered (a subject's opened while its
  // records weren't kept) closes as one without a subject.
  if(state && state->core && !wheelhouse_workspace_registered(state->core, workspace, 0))
  { wheelhouse_workspace_register(state->core, workspace, 0); }
  WheelhouseIntent intent = {.kind = WHEELHOUSE_INTENT_CLOSE_WORKSPACE, .workspace_id = workspace, .keep = keep != 0};
  uishell_workspace_shown = 0;
  B32 closed = uishell_workspace_lifecycle_apply(window, &intent);
  // No workspace shown after: it was the last open.
  if(closed && !uishell_workspace_shown) { uishell_new_workspace(window); }
  return closed;
}

// Re-homes a local workspace, as a row's drop does: onto local group `group`
// when `to_group` (clearing its project), else with project `project`
// (empty: none), leaving its group.
internal B32
uishell_workspace_set_home(CFG_Node *window, CFG_Node *workspace, B32 to_group, String8 group, String8 project)
{
  WheelhouseIntent intent = {.kind = WHEELHOUSE_INTENT_SET_WORKSPACE_HOME,
    .workspace_id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(workspace)),
    .has_group = to_group != 0, .group = uishell_sidebar_text(group), .project = uishell_sidebar_text(project)};
  return uishell_workspace_lifecycle_apply(window, &intent);
}

//- The effects

// OPEN_WORKSPACE: a `workspace` node with the core's ID and name, selected,
// with default panels.
internal CFG_Node *
uishell_workspace_open(CFG_Node *window, UIShell_WorkspaceId id, String8 name)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *workspace = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, workspace, str8_lit("workspace_id")), uishell_string_from_workspace_id(scratch.arena, id));
  uishell_workspace_index_insert(id, workspace->id);
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, workspace, str8_lit("label")), name);
  RD_WindowState *ws = rd_window_state_from_cfg(window);
  if(ws != &rd_nil_window_state)
  {
    ws->root_controlled_split_initialized = 1;
    ws->root_controlled_split_selected_workspace_id = workspace->id;
    ws->active_panel = rd_nil_panel_address;
  }
  UISHELL_APP_DEFAULT_WORKSPACE_PANELS(window, workspace);
  scratch_end(scratch);
  return workspace;
}

// CLOSE_WORKSPACE: a kept workspace's node stays, as `detached_workspace`,
// for its subject's next materialization; another's is released. The
// window-backed legacy workspace is the window's layout payload: closing it
// removes that payload, while window-level metadata such as `label` stays
// on the window node.
internal void
uishell_workspace_close_node(CFG_Node *window, UIShell_WorkspaceId id, B32 kept)
{
  CFG_Node *workspace = uishell_workspace_node_from_id(window, id);
  if(workspace == &cfg_nil_node) { return; }
  if(workspace == window)
  {
    Temp scratch = scratch_begin(0, 0);
    RD_Arrangement *own = rd_arrangement_from_panel_owner(scratch.arena, rd_panel_owner_from_host(RD_PanelOwnerKind_Workspace, window));
    rd_arrangement_discard(own);
    rd_arrangement_set(own);
    scratch_end(scratch);
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("views")));
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("workspace_id")));
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("arrangement_generation")));
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("arrangement_presentation")));
  }
  // TODO(#159): a kept node whose subject never returns is kept forever;
  // prune it with the retained-workspace expiry policy.
  else if(kept) { cfg_node_equip_string(rd_state->cfg, workspace, str8_lit("detached_workspace")); }
  else { cfg_node_release(rd_state->cfg, workspace); }
}

// SHOW_WORKSPACE: select it, after a close.
internal void
uishell_workspace_show(CFG_Node *window, UIShell_WorkspaceId id)
{
  CFG_Node *workspace = uishell_workspace_node_from_id(window, id);
  RD_WindowState *ws = rd_window_state_from_cfg(window);
  if(workspace == &cfg_nil_node || ws == &rd_nil_window_state) { return; }
  uishell_workspace_shown = 1;
  ws->root_controlled_split_initialized = 1;
  ws->root_controlled_split_selected_workspace_id = workspace->id;
  ws->window_layout_reset = 1;
}

//- Reading a workspace's Life

typedef struct UIShell_WorkspaceLife UIShell_WorkspaceLife;
struct UIShell_WorkspaceLife
{
  // The core has a record of it, and knows whether it is kept.
  B32 registered;
  B32 kept_known;
  B32 kept;
  // Its home's two fields, and its one home (WHEELHOUSE_HOME_*).
  String8 group;
  String8 project;
  U32 home_kind;
  String8 home;
};

// A workspace's Life, as its window's core keeps it; its texts copied to
// `arena`.
internal UIShell_WorkspaceLife
uishell_workspace_life(Arena *arena, CFG_Node *workspace)
{
  UIShell_WorkspaceLife life = {0};
  CFG_Node *window = str8_match(workspace->string, str8_lit("window"), 0) ? workspace : rd_window_from_cfg(workspace);
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  if(!state || !state->core || workspace == &cfg_nil_node) { return life; }
  state->life_calls += 1;
  WheelhouseWorkspaceLife *read = wheelhouse_workspace_life(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(workspace)), 0);
  if(read)
  {
    life.registered = 1;
    life.kept_known = read->kept_known != 0;
    life.kept = read->kept != 0;
    life.group = push_str8_copy(arena, uishell_sidebar_string(read->group));
    life.project = push_str8_copy(arena, uishell_sidebar_string(read->project));
    life.home_kind = read->home_kind;
    life.home = push_str8_copy(arena, uishell_sidebar_string(read->home));
  }
  wheelhouse_workspace_life_free(read);
  return life;
}

// The project a local workspace lives with ("lives with project X"); empty
// when it lives in a group.
internal String8
uishell_sidebar_local_home(CFG_Node *workspace)
{
  return uishell_workspace_life(rd_frame_arena(), workspace).project;
}

// The local group a local workspace lives in; empty when it lives in the
// default group (or has never been given one).
internal String8
uishell_sidebar_local_lives_in(CFG_Node *workspace)
{
  return uishell_workspace_life(rd_frame_arena(), workspace).group;
}

//- Records from before the core kept Life

// Each of the window's workspace nodes, open or kept, and the core's Life
// of it. A record written before the core kept Life (`kept` unknown) is
// seeded once from its node: kept from the node kind, its home from the two
// fields main wrote (`lives_in`, `lives_with`). After that the core's record
// is the truth: native drops those fields, and equips a node whose kind
// disagrees with the record to match it. A node the core doesn't know is
// left alone.
internal void
uishell_workspace_lives_follow(UIShell_SidebarState *state, CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next)
  {
    B32 open = str8_match(c->string, str8_lit("workspace"), 0);
    if(!open && !str8_match(c->string, str8_lit("detached_workspace"), 0)) { continue; }
    WheelhouseWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(c));
    if(!wheelhouse_workspace_registered(state->core, id, 0)) { continue; }
    CFG_Node *lives_in = cfg_node_child_from_string(c, str8_lit("lives_in"));
    CFG_Node *lives_with = cfg_node_child_from_string(c, str8_lit("lives_with"));
    UIShell_WorkspaceLife life = uishell_workspace_life(scratch.arena, c);
    if(!life.kept_known)
    {
      char *error = 0;
      uishell_sidebar_result(state, wheelhouse_workspace_seed_life(state->core, id, !open, uishell_sidebar_text(lives_in->first->string),
                                                                   uishell_sidebar_text(lives_with->first->string), &error) || !error, error);
    }
    else if(life.kept == open)
    { cfg_node_equip_string(rd_state->cfg, c, life.kept ? str8_lit("detached_workspace") : str8_lit("workspace")); }
    cfg_node_release(rd_state->cfg, lives_in);
    cfg_node_release(rd_state->cfg, lives_with);
  }
  scratch_end(scratch);
}
