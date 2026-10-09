// Every drop site box under `box`, except panels' catch-all sites, which
// cover their whole panel by design.
internal void
uishell_panel_drop_sites(Arena *arena, UI_Box *box, CFG_PanelTree *tree, UI_Box ***sites, U64 *count)
{
  for(UI_Box *child = box->first; !ui_box_is_nil(child); child = child->next)
  {
    B32 catchall = 0;
    for(CFG_PanelNode *p = tree->root; p != &cfg_nil_panel_node; p = cfg_panel_node_rec__depth_first_pre(tree->root, p).next)
    { catchall |= ui_key_match(child->key, rd_panel_catchall_drop_site_key(p->cfg)); }
    if(child->flags & UI_BoxFlag_DropSite && !catchall)
    {
      UI_Box **grown = push_array(arena, UI_Box *, *count+1);
      MemoryCopy(grown, *sites, sizeof(UI_Box *)*(*count));
      grown[(*count)++] = child;
      *sites = grown;
    }
    uishell_panel_drop_sites(arena, child, tree, sites, count);
  }
}

// Docking sites that share a line nest instead of overlapping (#257). As in
// the sidebar, a column holds a row in its middle, so the row's end and the
// window's boundary beside the column lie on one line, both centred on it.
internal U32
uishell_panel_drop_site_overlap_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  U32 failures = 0;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *owner = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  CFG_Node *root = cfg_node_new(rd_state->cfg, owner, str8_lit("panels"));
  CFG_Node *tabs[5];
  for(U32 i = 0; i < ArrayCount(tabs); i++) { tabs[i] = rd_cfg_new_view_tab(root, str8_lit("terminal_fixture"), str8_zero(), i == 0); }
  // root X [Y [t1, X [t2, t4], t3], t0]
  struct { U32 view, beside; Dir2 dir; } splits[] = {{1, 0, Dir2_Left}, {2, 1, Dir2_Down}, {3, 2, Dir2_Down}, {4, 2, Dir2_Right}};
  for(U32 i = 0; i < ArrayCount(splits); i++)
    UIShell_RegsScope(.window = ws->cfg_id, .panel = tabs[splits[i].view]->parent->id, .dst_panel = tabs[splits[i].beside]->parent->id,
                      .view = tabs[splits[i].view]->id, .dir2 = splits[i].dir)
  { uishell_dispatch_panel_command(str8_lit("split_panel")); }
  UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(arena, window, owner);
  CFG_PanelTree tree = mount.panel_tree;
  B32 nested = tree.root->child_count == 2 && tree.root->first->child_count == 3 && tree.root->first->first->next->child_count == 2;
  fprintf(stderr, "%s drop-site overlap layout: a row in the middle of a column\n", nested ? "PASS" : "FAIL");
  failures += !nested;
  Rng2F32 area = r2f32p(0, 0, 900, 600);
  for(CFG_PanelNode *leaf = tree.root; nested && leaf != &cfg_nil_panel_node; leaf = cfg_panel_node_rec__depth_first_pre(tree.root, leaf).next)
  {
    if(leaf->first != &cfg_nil_panel_node) { continue; }
    UI_State *test_ui = ui_state_alloc(), *saved_ui = ui_state;
    ui_select_state(test_ui);
    UI_Box **sites = 0; U64 count = 0;
    UIShell_RegsScope(.window = ws->cfg_id, .panel = tabs[0]->parent->id, .view = tabs[0]->id)
    {
      rd_drag_begin(UIShell_ContextRegSlot_View);
      UI_IconInfo icons = ws->ui->icon_info; UI_AnimationInfo animation = {0}; UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      ui_state->mouse = center_2f32(cfg_target_rect_from_panel_node(area, tree.root, leaf));
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      { rd_panel_area_ui(scratch, area, area, ws, &mount, 1, 0, 0, 0, 0); }
      ui_end_build();
      uishell_panel_drop_sites(arena, test_ui->root, &tree, &sites, &count);
      rd_drag_kill();
    }
    U32 overlaps = 0;
    for(U64 i = 0; i < count; i++) for(U64 j = i+1; j < count; j++)
    {
      Rng2F32 both = intersect_2f32(sites[i]->rect, sites[j]->rect);
      if(both.x1 - both.x0 > 0.5f && both.y1 - both.y0 > 0.5f) { overlaps += 1; }
    }
    B32 ok = count >= 8 && overlaps == 0;
    fprintf(stderr, "%s drop sites don't overlap over panel %p (%llu sites, %u overlaps)\n", ok ? "PASS" : "FAIL", leaf->cfg, (unsigned long long)count, overlaps);
    failures += !ok;
    ui_select_state(saved_ui);
    ui_state_release(test_ui);
  }
  cfg_node_release(rd_state->cfg, owner);
  scratch_end(scratch);
  return failures;
}

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
    // Its middle is the View's: no centre pill, and tabs drop on the strip.
    char *names[] = {"up", "down", "left", "right"};
    for(U32 i = 0; i < ArrayCount(names); i++)
    {
      UI_Key key = ui_key_from_stringf(ui_key_zero(), "drop_split_%s_%p", names[i], panels);
      B32 exists = !ui_box_is_nil(ui_box_from_key(key));
      fprintf(stderr, "%s single-panel drop target: %s\n", exists ? "PASS" : "FAIL", names[i]);
      failures += !exists;
    }
    B32 no_centre = ui_box_is_nil(ui_box_from_key(ui_key_from_stringf(ui_key_zero(), "drop_split_center_%p", panels)));
    UI_Box *strip = ui_box_from_key(rd_panel_catchall_drop_site_key(panels));
    B32 on_strip = !ui_box_is_nil(strip) && !contains_2f32(strip->rect, ui_state->mouse) && dim_2f32(strip->rect).y < 100;
    fprintf(stderr, "%s single panel: no centre pill; tabs drop on its strip\n", no_centre && on_strip ? "PASS" : "FAIL");
    failures += !(no_centre && on_strip);
    // A dragged View keeps its size and draws to a texture (#253)...
    UI_Box *surface_box = ui_box_from_key(rd_view_surface_key(view->id));
    B32 real_size = !ui_box_is_nil(surface_box) && surface_box->flags & UI_BoxFlag_RenderToSurface && dim_2f32(surface_box->rect).x > 500;
    fprintf(stderr, "%s a dragged View draws to a texture at its real size\n", real_size ? "PASS" : "FAIL");
    failures += !real_size;
    // ...which the floater shows scaled, without laying the View out again.
    rd_window_surface_node_from_key(ws, rd_view_surface_key(view->id).u64[0], v2s32(800, 400));
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12) { rd_drag_view_floater_ui(ws, view); }
    ui_end_build();
    UI_Box *preview = ui_box_from_key(ui_key_from_string(ui_key_zero(), str8_lit("###view_preview_container")));
    for(UI_Box *b = test_ui->root; ui_box_is_nil(preview) && !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, test_ui->root).next)
    { if(b->custom_draw == rd_workspace_preview_box_draw) { preview = b; } }
    B32 floater = !ui_box_is_nil(preview) && preview->custom_draw == rd_workspace_preview_box_draw && ui_box_is_nil(preview->first) &&
      ui_box_is_nil(ui_box_from_key(rd_view_surface_key(view->id))) && abs_f32(dim_2f32(preview->rect).y - 180.f) < 1.f;
    fprintf(stderr, "%s the drag floater shows the View's texture, scaled, without laying it out\n", floater ? "PASS" : "FAIL");
    failures += !floater;
    rd_drag_kill();
  }
  // A View that opts in to a live preview (text, binary) is laid out again in
  // the floater rather than drawn to a texture.
  CFG_Node *text_view = rd_cfg_new_view_tab(panels, str8_lit("text"), str8_zero(), 0);
  UIShell_RegsScope(.window = ws->cfg_id, .panel = panels->id, .view = text_view->id)
  {
    rd_drag_begin(UIShell_ContextRegSlot_View);
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12) { rd_drag_view_floater_ui(ws, text_view); }
    // Accents take the theme's focus border, not its faint selection wash.
    String8 focus_border[] = {str8_lit("focus"), str8_lit("border")};
    Vec4F32 accent = rd_accent_color(), wash = ui_color_from_name(str8_lit("selection"));
    Vec4F32 focus = ui_color_from_tags_key_extras(ui_top_tags_key(), (String8Array){focus_border, ArrayCount(focus_border)});
    B32 themed = accent.x == focus.x && accent.y == focus.y && accent.z == focus.z && accent.w == 1.f &&
      (accent.x != wash.x || accent.y != wash.y || accent.z != wash.z);
    ui_end_build();
    fprintf(stderr, "%s accents take the theme's focus border, not its selection wash\n", themed ? "PASS" : "FAIL");
    failures += !themed;
    // Hover moves the visible brightness by the same step on light and dark
    // backgrounds; a fixed opacity of black is invisible on a light one.
    Vec4F32 white = v4f32(1, 1, 1, 1), black = v4f32(0, 0, 0, 1);
    Vec4F32 dark = linear_from_srgba(v4f32(0x1f/255.f, 0x1f/255.f, 0x1f/255.f, 1));
    Vec4F32 light = linear_from_srgba(v4f32(0xf4/255.f, 0xf4/255.f, 0xf4/255.f, 1));
    F32 dark_a = rd_hover_alpha(white, dark, 0.06f), light_a = rd_hover_alpha(black, light, 0.06f);
    F32 dark_step = srgba_from_linear(mix_4f32(dark, white, dark_a)).x - srgba_from_linear(dark).x;
    F32 light_step = srgba_from_linear(light).x - srgba_from_linear(mix_4f32(light, black, light_a)).x;
    B32 hover = abs_f32(dark_step - 0.06f) < 0.005f && abs_f32(light_step - 0.06f) < 0.005f && light_a > 4*dark_a;
    fprintf(stderr, "%s hover changes light and dark backgrounds by the same visible step (alpha %.3f dark, %.3f light)\n", hover ? "PASS" : "FAIL", dark_a, light_a);
    failures += !hover;
    UI_Box *preview = &ui_nil_box;
    for(UI_Box *b = test_ui->root; ui_box_is_nil(preview) && !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, test_ui->root).next)
    {
      UI_Box *seed = b->parent;
      while(!ui_box_is_nil(seed) && ui_key_match(seed->key, ui_key_zero())) { seed = seed->parent; }
      if(!ui_box_is_nil(seed) && ui_key_match(b->key, ui_key_from_string(seed->key, str8_lit("###view_preview_container")))) { preview = b; }
    }
    B32 live = rd_view_drag_preview_is_live(text_view) && !rd_view_drag_preview_is_live(view) &&
      !ui_box_is_nil(preview) && preview->custom_draw == 0 && !ui_box_is_nil(preview->first);
    fprintf(stderr, "%s a text View's drag floater shows it live\n", live ? "PASS" : "FAIL");
    failures += !live;
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
  // Commands must enforce the same validity as drag queries. Exercise real
  // create, duplicate, close, move and drag-split dispatchers, not helper mocks.
  CFG_Node *commands_owner = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  CFG_Node *commands_panel = cfg_node_new(rd_state->cfg, commands_owner, str8_lit("panels"));
  UIShell_RegsScope(.window = window->id, .panel = commands_panel->id, .string = str8_lit("text"), .expr = str8_zero())
  { uishell_dispatch_tab_command(str8_lit("build_tab")); }
  CFG_Node *created = cfg_node_child_from_string(commands_panel, str8_lit("text"));
  failures += created == &cfg_nil_node;
  UIShell_RegsScope(.tab = created->id)
  { uishell_dispatch_tab_command(str8_lit("duplicate_tab")); }
  CFG_NodePtrList copies = cfg_node_child_list_from_string(scratch.arena, commands_panel, str8_lit("text"));
  failures += copies.count != 2;
  CFG_ID created_id = created->id;
  UIShell_RegsScope(.tab = created_id)
  { uishell_dispatch_tab_command(str8_lit("close_tab")); }
  failures += cfg_node_from_id(created_id) != &cfg_nil_node;
  CFG_Node *control = cfg_node_child_from_string_or_alloc(rd_state->cfg, window, RD_DOCK_SIDEBAR_ROOT);
  CFG_Node *selector = cfg_node_new(rd_state->cfg, control, str8_lit("workspace_selector"));
  CFG_ID selector_id = selector->id;
  log_scope_begin();
  UIShell_RegsScope(.window = window->id, .panel = commands_panel->id, .string = str8_lit("unknown_saved_view"))
  { uishell_dispatch_tab_command(str8_lit("build_tab")); }
  failures += cfg_node_child_from_string(commands_panel, str8_lit("unknown_saved_view")) != &cfg_nil_node;
  CFG_Node *unknown = cfg_node_new(rd_state->cfg, commands_panel, str8_lit("unknown_saved_view"));
  CFG_ID unknown_id = unknown->id;
  // An unknown saved View cannot duplicate, but closing must remove it so a
  // typo or unavailable extension never traps content in the saved layout.
  UIShell_RegsScope(.tab = unknown_id)
  { uishell_dispatch_tab_command(str8_lit("duplicate_tab")); }
  failures += cfg_node_from_id(unknown_id) != unknown;
  failures += cfg_node_child_list_from_string(scratch.arena, commands_panel, str8_lit("unknown_saved_view")).count != 1;
  UIShell_RegsScope(.tab = unknown_id)
  { uishell_dispatch_tab_command(str8_lit("close_tab")); }
  failures += cfg_node_from_id(unknown_id) != &cfg_nil_node;
  UIShell_RegsScope(.tab = selector_id)
  {
    uishell_dispatch_tab_command(str8_lit("duplicate_tab"));
    uishell_dispatch_tab_command(str8_lit("close_tab"));
  }
  UIShell_RegsScope(.view = selector_id, .dst_panel = commands_panel->id, .prev_tab = 0)
  { uishell_dispatch_tab_command(str8_lit("move_view")); }
  UIShell_RegsScope(.view = selector_id, .dst_panel = commands_panel->id, .dir2 = Dir2_Left)
  { uishell_dispatch_panel_command(str8_lit("split_panel")); }
  LogScopeResult rejected = log_scope_end(scratch.arena);
  // Every refusal names its action and the returned validity rule.
  String8 errors = rejected.strings[LogMsgKind_UserError];
  char *actions[] = {"Cannot create", "Cannot duplicate", "Cannot close", "Cannot move", "Cannot split with", "not registered"};
  for(U64 i = 0; i < ArrayCount(actions); i++)
  { failures += str8_find_needle(errors, 0, str8_cstring(actions[i]), 0) == errors.size; }
  failures += cfg_node_from_id(selector_id) != selector || selector->parent != control;
  failures += cfg_node_child_list_from_string(scratch.arena, control, str8_lit("workspace_selector")).count != 1;
  CFG_PanelTree command_tree = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, commands_owner).panel_tree;
  failures += command_tree.root->child_count != 0;
  // Reading mounts must not repair or release nodes retained by a caller.
  CFG_Node *invalid_copy = cfg_node_new(rd_state->cfg, commands_panel, str8_lit("workspace_selector"));
  CFG_ID invalid_id = invalid_copy->id;
  U64 before_read = cfg_change_gen();
  uishell_workspace_mount_from_owner_cfg(scratch.arena, window, commands_owner);
  failures += cfg_change_gen() != before_read || cfg_node_from_id(invalid_id) != invalid_copy || invalid_copy->parent != commands_panel;
  // Explicit repair prefers the valid selector and settles its generation.
  rd_dock_restore_layouts();
  failures += cfg_node_from_id(invalid_id) != &cfg_nil_node || cfg_node_from_id(selector_id) != selector;
  U64 after_repair = cfg_change_gen();
  rd_dock_restore_layouts();
  failures += cfg_change_gen() != after_repair;
  cfg_node_release(rd_state->cfg, selector);
  cfg_node_release(rd_state->cfg, commands_owner);
  ui_select_state(saved_ui);
  ui_state_release(test_ui);
  cfg_node_release(rd_state->cfg, owner);
  scratch_end(scratch);
  failures += uishell_panel_drop_site_overlap_diagnostics(ws);
  failures += !uishell_sidebar_docking_diagnostics(ws);
  failures += !uishell_border_diagnostics(ws);
  fprintf(stderr, "Panel diagnostics: %u failures\n", failures);
  return failures == 0;
}
