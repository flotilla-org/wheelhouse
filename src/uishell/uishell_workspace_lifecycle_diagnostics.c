// Workspace close affordances (#229): detach keeps a subject workspace's layout
// for its row to reopen, close destroys a subjectless one, the last close
// leaves a fresh workspace, and new-workspace sits on the Other workspaces
// header. Runs after the sidebar host diagnostics on the same fixture window.

#define LifecycleCheck(cond, msg) do { if(!(cond)) { ok = 0; fprintf(stderr, "FAIL workspace lifecycle: %s\n", msg); } } while(0)

internal B32
uishell_workspace_lifecycle_find(UIShell_SidebarState *state, String8 entity_id, CFG_ID workspace_id, AndamentoNode *out)
{
  for(U64 i = 0; state->snapshot && i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
    if(node.is_section) { continue; }
    if((entity_id.size && str8_match(uishell_sidebar_string(node.entity_id), entity_id, 0)) ||
       (workspace_id && node.state == ANDAMENTO_LIVE && node.workspace_id == workspace_id))
    { *out = node; return 1; }
  }
  return 0;
}

internal B32
uishell_workspace_lifecycle_diagnostics(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  B32 ok = 1;
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  UIShell_SidebarState *state = ws != &rd_nil_window_state ? uishell_sidebar_init(ws) : 0;
  LifecycleCheck(state && state->core, "sidebar fixture core");
  if(!ok) { scratch_end(scratch); return 0; }
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  uishell_sidebar_observe(state, &split);
  uishell_sidebar_refresh(state);

  //- Open the fixture subject workspace and give it a user layout change.
  AndamentoNode node = {0};
  LifecycleCheck(uishell_workspace_lifecycle_find(state, str8_lit("multi"), 0, &node), "fixture subject row");
  if(node.state != ANDAMENTO_LIVE)
  {
    LifecycleCheck(uishell_sidebar_dispatch(state, node.activate, 0), "open subject");
    uishell_sidebar_effects(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    uishell_workspace_lifecycle_find(state, str8_lit("multi"), 0, &node);
  }
  CFG_Node *subject = cfg_node_from_id(node.workspace_id);
  LifecycleCheck(node.state == ANDAMENTO_LIVE && subject != &cfg_nil_node, "subject workspace live");
  CFG_Node *marker = cfg_node_new(rd_state->cfg, subject, str8_lit("lifecycle_marker"));
  CFG_ID subject_id = subject->id, marker_id = marker->id;
  U64 open_count = split.inventory.count;
  LifecycleCheck(uishell_sidebar_close_kind(node, uishell_sidebar_node_status(state, node)) == UIShell_SidebarCloseKind_Detach,
                 "a live subject workspace offers detach");

  //- Detach from the palette (no explicit workspace) acts on the Visible Workspace.
  ws->root_controlled_split_selected_workspace_id = subject_id;
  UIShell_RegsScope(.window = window->id) { uishell_dispatch_window_command(str8_lit("detach_workspace")); }
  split = uishell_root_controlled_split_from_window(scratch.arena, window);
  LifecycleCheck(cfg_node_from_id(subject_id) == subject && str8_match(subject->string, str8_lit("detached_workspace"), 0),
                 "detach keeps the workspace node, marked detached");
  LifecycleCheck(split.inventory.count == open_count-1, "detached workspace leaves the inventory");
  for(UIShell_MaterializedWorkspace *w = split.inventory.first; w; w = w->next)
  { LifecycleCheck(w->id != subject_id, "detached workspace is not materialized"); }
  uishell_sidebar_observe(state, &split);
  uishell_sidebar_refresh(state);
  uishell_workspace_lifecycle_find(state, str8_lit("multi"), 0, &node);
  LifecycleCheck(node.state == ANDAMENTO_LATENT && node.openable, "detached subject row goes latent and stays openable");

  //- Reopening the row reattaches the same layout.
  LifecycleCheck(uishell_sidebar_dispatch(state, node.activate, 0), "reopen subject");
  uishell_sidebar_effects(state, &split);
  split = uishell_root_controlled_split_from_window(scratch.arena, window);
  LifecycleCheck(str8_match(subject->string, str8_lit("workspace"), 0) &&
                 cfg_node_from_id(marker_id) == marker && marker->parent == subject,
                 "reopen reattaches the detached node and its layout");
  LifecycleCheck(split.inventory.count == open_count && ws->root_controlled_split_selected_workspace_id == subject_id,
                 "reopened workspace is materialized and selected");
  cfg_node_release(rd_state->cfg, marker);

  //- A subjectless workspace offers ×, and closing it destroys it.
  CFG_Node *loose = uishell_new_workspace(window);
  CFG_ID loose_id = loose->id;
  split = uishell_root_controlled_split_from_window(scratch.arena, window);
  uishell_sidebar_observe(state, &split);
  uishell_sidebar_refresh(state);
  AndamentoNode loose_node = {0};
  LifecycleCheck(uishell_workspace_lifecycle_find(state, str8_zero(), loose_id, &loose_node) &&
                 uishell_sidebar_close_kind(loose_node, uishell_sidebar_node_status(state, loose_node)) == UIShell_SidebarCloseKind_Destroy,
                 "a subjectless workspace offers close");
  LifecycleCheck(!uishell_workspace_cfg_has_subject(loose), "new workspaces have no subject");
  // The subject is a chip in the tree and a full row in Attention; match every
  // placement and use whichever renders as a row.
  String8 row_keys[8] = {push_str8_copy(scratch.arena, uishell_sidebar_string(loose_node.key))};
  U64 row_key_count = 1;
  for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot) && row_key_count < ArrayCount(row_keys); i++)
  {
    AndamentoNode n = {0}; andamento_snapshot_node(state->snapshot, i, &n);
    if(!n.is_section && str8_match(uishell_sidebar_string(n.entity_id), str8_lit("multi"), 0))
    { row_keys[row_key_count++] = push_str8_copy(scratch.arena, uishell_sidebar_string(n.key)); }
  }

  //- The Other workspaces header hosts new-workspace and reports it was drawn.
  //  Hovering a row turns its status mark into detach (subject) or × (none).
  {
    UI_State *saved_ui = ui_state, *test = ui_state_alloc();
    ui_select_state(test);
    RD_ChromeNiche saved_niche = ws->chrome_niche[RD_ChromeElementKind_NewWorkspace];
    ws->chrome_niche[RD_ChromeElementKind_NewWorkspace] = RD_ChromeNiche_SectionHeader;
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    B32 header_button = 0;
    // Frames: 0-1 settle with no hover, 2-3 hover the subject row, 4-5 hover
    // the subjectless row. Each hover reads the previous frame's row rect.
    Vec2F32 row_centers[2] = {0}; // [0] subject, [1] subjectless
    // Boxes not rebuilt are recycled, so record what each hover phase drew.
    B32 close_seen[3] = {0}, detach_drawn[3] = {0}, x_drawn[3] = {0};
    for(U32 frame = 0; frame < 6; frame++)
    {
      UI_IconInfo icons = ws->ui->icon_info;
      UI_AnimationInfo animation = {0}; UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      U32 hover = frame/2;
      ui_state->mouse = hover == 0 ? v2f32(-100, -100) : row_centers[hover-1];
      UIShell_RegsScope(.window = window->id)
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
      { uishell_sidebar_ui(r2f32p(0, 0, 320, 900), &split); }
      ui_end_build();
      for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
      {
        if(ui_box_is_nil(box->parent)) { continue; }
        if(ui_key_match(box->key, ui_key_from_string(box->parent->key, str8_lit("###new_workspace"))))
        {
          for(UI_Box *a = box->parent; !ui_box_is_nil(a); a = a->parent)
          {
            if(ui_key_match(a->key, ui_key_from_string(a->parent->key, str8_lit("###section_header_andamento.unplaced-workspaces"))))
            { header_button = 1; }
          }
        }
        for(U64 r = 0; r < row_key_count; r++)
        {
          U64 slot = r == 0 ? 1 : 0;
          if(ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###sidebar_row_%S", row_keys[r])) &&
             row_centers[slot].x == 0)
          { row_centers[slot] = center_2f32(box->rect); }
          if(frame % 2 == 1 &&
             ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###close_%S", row_keys[r])))
          {
            close_seen[hover] = 1;
            detach_drawn[hover] = box->custom_draw == rd_workspace_detach_icon_draw && ui_box_display_string(box).size == 0;
            x_drawn[hover] = box->custom_draw == 0 && str8_match(ui_box_display_string(box), str8_lit("×"), 0);
          }
        }
      }
    }
    LifecycleCheck(row_centers[0].x > 0 && row_centers[1].x > 0, "subject and subjectless rows are rendered");
    LifecycleCheck(!close_seen[0], "no close affordance without hover");
    LifecycleCheck(close_seen[1] && detach_drawn[1], "hovering a subject row shows detach");
    LifecycleCheck(close_seen[2] && x_drawn[2], "hovering a subjectless row shows ×");
    LifecycleCheck(ws->chrome_section_header_frame == rd_state->frame_index, "header records the frame it hosted chrome");
    LifecycleCheck(header_button, "new-workspace is built in the Other workspaces header");
    ws->chrome_niche[RD_ChromeElementKind_NewWorkspace] = saved_niche;
    ui_select_state(saved_ui); ui_state_release(test);
  }

  UIShell_RegsScope(.window = window->id, .cfg = loose_id) { uishell_dispatch_window_command(str8_lit("detach_workspace")); }
  LifecycleCheck(cfg_node_from_id(loose_id) == &cfg_nil_node, "detaching a subjectless workspace destroys it");

  //- Default names never repeat an open workspace's name, even after a close.
  {
    CFG_Node *first = uishell_new_workspace(window);
    uishell_new_workspace(window);
    cfg_node_release(rd_state->cfg, first);
    uishell_new_workspace(window);
    CFG_NodePtrList named = cfg_node_child_list_from_string(scratch.arena, window, str8_lit("workspace"));
    B32 distinct = 1;
    for(CFG_NodePtrNode *a = named.first; a; a = a->next)
      for(CFG_NodePtrNode *b = a->next; b; b = b->next)
    { distinct = distinct && !str8_match(rd_label_from_cfg(a->v), rd_label_from_cfg(b->v), 0); }
    LifecycleCheck(distinct, "new workspace names are unique among open workspaces");
  }

  //- Closing every workspace leaves exactly one fresh subjectless workspace.
  split = uishell_root_controlled_split_from_window(scratch.arena, window);
  U64 closing_count = split.inventory.count;
  CFG_ID *closing = push_array(scratch.arena, CFG_ID, closing_count);
  U64 idx = 0;
  for(UIShell_MaterializedWorkspace *w = split.inventory.first; w; w = w->next) { closing[idx++] = w->id; }
  for(U64 i = 0; i < closing_count; i++)
  { UIShell_RegsScope(.window = window->id, .cfg = closing[i]) { uishell_dispatch_window_command(str8_lit("close_workspace")); } }
  split = uishell_root_controlled_split_from_window(scratch.arena, window);
  B32 fresh = split.inventory.count == 1 && split.inventory.selected == split.inventory.first;
  for(U64 i = 0; fresh && i < closing_count; i++) { fresh = split.inventory.first->id != closing[i]; }
  LifecycleCheck(fresh && split.inventory.first->mount.workspace_cfg != &cfg_nil_node &&
                 !uishell_workspace_cfg_has_subject(split.inventory.first->mount.workspace_cfg),
                 "closing the last workspace leaves one fresh subjectless workspace");

  //- The window-backed legacy workspace can be the last one too: closing it
  //  removes the window's panels and leaves a fresh workspace child.
  cfg_node_release(rd_state->cfg, split.inventory.first->mount.workspace_cfg);
  uishell_default_workspace_panels(window, window);
  split = uishell_root_controlled_split_from_window(scratch.arena, window);
  LifecycleCheck(split.inventory.count == 1 && split.inventory.first->id == window->id, "legacy window workspace is the only one");
  UIShell_RegsScope(.window = window->id, .cfg = window->id) { uishell_dispatch_window_command(str8_lit("close_workspace")); }
  split = uishell_root_controlled_split_from_window(scratch.arena, window);
  LifecycleCheck(split.inventory.count == 1 && split.inventory.first->mount.workspace_cfg != &cfg_nil_node &&
                 cfg_node_child_from_string(window, str8_lit("panels")) == &cfg_nil_node,
                 "closing the last, window-backed workspace leaves one fresh workspace");

  fprintf(stderr, "Workspace lifecycle diagnostics: %s (detach/reopen, palette target, hover affordance, destroy, header new-workspace, unique names, last close, legacy last close)\n",
          ok ? "passed" : "FAILED");
  scratch_end(scratch);
  return ok;
}

#undef LifecycleCheck
