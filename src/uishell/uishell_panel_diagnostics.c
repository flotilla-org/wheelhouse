// Exercises production drop-site generation with an isolated workspace and UI state.
internal B32
uishell_panel_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  UI_State *saved_ui = ui_state, *test_ui = ui_state_alloc();
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *owner = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  cfg_node_new(rd_state->cfg, owner, str8_lit("split_x"));
  CFG_Node *panels = cfg_node_new(rd_state->cfg, owner, str8_lit("panels"));
  cfg_node_new(rd_state->cfg, panels, str8_lit("selected"));
  CFG_Node *view = rd_cfg_new_view_tab(panels, str8_lit("terminal_fixture"), str8_zero(), 1);
  rd_cfg_new_view_tab(panels, str8_lit("terminal_fixture"), str8_zero(), 0);
  UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, owner);
  U32 failures = 0;
  ui_select_state(test_ui);
  UIShell_RegsScope(.window = ws->cfg_id, .panel = panels->id, .view = view->id)
  {
    rd_drag_begin(UIShell_ContextRegSlot_View);
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = v2f32(320, 240);
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    {
      rd_panel_area_ui(scratch, r2f32p(0, 0, 640, 480), r2f32p(0, 0, 640, 480), ws, &mount, 1, 0, 0, 0, 0);
    }
    ui_end_build();
    char *names[] = {"center", "up", "down", "left", "right"};
    for(U32 i = 0; i < ArrayCount(names); i++)
    {
      UI_Key key = ui_key_from_stringf(ui_key_zero(), "drop_split_%s_%p", names[i], panels);
      B32 exists = !ui_box_is_nil(ui_box_from_key(key));
      fprintf(stderr, "%s single-panel drop target: %s\n", exists ? "PASS" : "FAIL", names[i]);
      failures += !exists;
    }
    rd_drag_kill();
  }
  // Closing the empty source panel is also part of moving its last tab.
  // Use a workspace separate from the window's selected workspace so a
  // window-level lookup cannot accidentally supply the correct panel tree.
  CFG_Node *other = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  cfg_node_new(rd_state->cfg, other, str8_lit("split_x"));
  CFG_Node *other_panels = cfg_node_new(rd_state->cfg, other, str8_lit("panels"));
  CFG_Node *middle = 0, *moving_view = 0, *destination = 0;
  for(U32 i = 0; i < 3; i++)
  {
    CFG_Node *panel = cfg_node_new(rd_state->cfg, other_panels, str8_lit("0.333333"));
    CFG_Node *tab = rd_cfg_new_view_tab(panel, str8_lit("terminal_fixture"), str8_zero(), 1);
    if(i == 0) { destination = panel; }
    if(i == 1) { middle = panel; moving_view = tab; }
  }
  UIShell_CmdNode *before_move = rd_state->cmds[0].last;
  UIShell_RegsScope(.window = ws->cfg_id, .panel = middle->id, .view = moving_view->id,
                   .dst_panel = destination->id, .prev_tab = 0)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  B32 closed_source = 0;
  for(UIShell_CmdNode *n = before_move ? before_move->next : rd_state->cmds[0].first; n; n = n->next)
  {
    if(str8_match(n->cmd.name, str8_lit("close_panel"), 0)) UIShell_RegsScope()
    {
      MemoryCopyStruct(uishell_regs(), n->cmd.regs);
      uishell_dispatch_panel_command(n->cmd.name);
      closed_source = 1;
      break;
    }
  }
  failures += !closed_source;
  CFG_PanelTree remaining = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, other).panel_tree;
  F32 total = 0;
  for(CFG_PanelNode *p = remaining.root->first; p != &cfg_nil_panel_node; p = p->next)
  { total += p->pct_of_parent; }
  B32 filled = remaining.root->child_count == 2 && abs_f32(total-1.f) < 0.0001f;
  fprintf(stderr, "%s remaining panels fill workspace: %.6f\n", filled ? "PASS" : "FAIL", total);
  failures += !filled;
  cfg_node_release(rd_state->cfg, other);
  Dir2 directions[] = {Dir2_Left, Dir2_Right, Dir2_Up, Dir2_Down};
  for(U32 i = 0; i < ArrayCount(directions); i++)
  {
    CFG_Node *split_owner = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
    CFG_Node *root = cfg_node_new(rd_state->cfg, split_owner, str8_lit("panels"));
    CFG_Node *moving = rd_cfg_new_view_tab(root, str8_lit("terminal_fixture"), str8_zero(), 1);
    rd_cfg_new_view_tab(root, str8_lit("terminal_fixture"), str8_zero(), 0);
    UIShell_RegsScope(.window = ws->cfg_id, .panel = root->id, .dst_panel = root->id,
                     .view = moving->id, .dir2 = directions[i])
    { uishell_dispatch_panel_command(str8_lit("split_panel")); }
    CFG_PanelTree tree = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, split_owner).panel_tree;
    B32 valid = tree.root->child_count == 2 && tree.root->split_axis == axis2_from_dir2(directions[i]) &&
      tree.root->first->tabs.count == 1 && tree.root->last->tabs.count == 1;
    fprintf(stderr, "%s split/move direction %u preserves both views\n", valid ? "PASS" : "FAIL", i);
    failures += !valid;
    cfg_node_release(rd_state->cfg, split_owner);
  }
  ui_select_state(saved_ui);
  ui_state_release(test_ui);
  cfg_node_release(rd_state->cfg, owner);
  scratch_end(scratch);
  fprintf(stderr, "Panel diagnostics: %u failures\n", failures);
  return failures == 0;
}
