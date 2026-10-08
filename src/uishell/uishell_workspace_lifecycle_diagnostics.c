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

internal U64
uishell_workspace_lifecycle_queued(String8 name, CFG_ID cfg)
{
  U64 count = 0;
  for(U64 list = 0; list < ArrayCount(rd_state->cmds); list++)
  {
    for(UIShell_CmdNode *n = rd_state->cmds[list].first; n; n = n->next)
    { count += str8_match(n->cmd.name, name, 0) && n->cmd.regs->cfg == cfg; }
  }
  return count;
}

// The center of the box keyed `suffix` under its nearest keyed ancestor, as
// last laid out in `ui`; zero when absent.
internal Vec2F32
uishell_workspace_lifecycle_center(UI_State *ui, String8 suffix)
{
  for(UI_Box *b = ui->root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, ui->root).next)
  {
    UI_Box *keyed = b->parent;
    while(!ui_box_is_nil(keyed) && ui_key_match(keyed->key, ui_key_zero())) { keyed = keyed->parent; }
    if(!ui_box_is_nil(keyed) && ui_key_match(b->key, ui_key_from_string(keyed->key, suffix))) { return center_2f32(b->rect); }
  }
  return v2f32(0, 0);
}

// Drags the box keyed `from` onto the box keyed `to` through the rendered
// sidebar: press, move past the threshold, release; the shared drag finish
// runs each frame, as at the end of a window's build. Returns whether the
// drag started and the target lit up.
internal B32
uishell_workspace_lifecycle_drag(RD_WindowState *ws, CFG_Node *window, Arena *arena, String8 from, String8 to, String8 lit)
{
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  ui_select_state(test);
  Vec2F32 start = {0}, target = {0};
  B32 lit_seen = 0;
  for(U32 frame = 0; frame < 7; frame++)
  {
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(arena, window);
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0}; UI_EventList events = {0}; UI_EventNode event = {0};
    Vec2F32 at = frame < 2 ? start : frame == 2 ? add_2f32(start, v2f32(0, 12)) : target;
    if(frame == 1 || frame == 5)
    {
      event.v = (UI_Event){.key = WM_Key_LeftMouseButton, .kind = frame == 1 ? UI_EventKind_Press : UI_EventKind_Release, .pos = at};
      events.first = events.last = &event; events.count = 1;
    }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = at;
    UIShell_RegsScope(.window = window->id)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_sidebar_ui(r2f32p(0, 0, 320, 900), &split); }
    uishell_sidebar_drag_finish(ws);
    ui_end_build();
    if(frame == 0) { start = uishell_workspace_lifecycle_center(test, from); }
    if(frame >= 2) { target = uishell_workspace_lifecycle_center(test, to); }
    if(frame == 4) { lit_seen = rd_drag_is_active() && !ui_box_is_nil(ui_box_from_key(ui_key_from_string(ui_key_zero(), lit))); }
  }
  rd_drag_kill(); ui_kill_action();
  ui_select_state(saved_ui); ui_state_release(test);
  return lit_seen;
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

  //- A local workspace is its own host entity: it has details (a hover card),
  //  lives with a project when annotated, and returns to Workspaces without.
  String8 local_id = push_str8_copy(scratch.arena, uishell_sidebar_local_entity(loose));
  {
    LifecycleCheck(str8_match(uishell_sidebar_string(loose_node.entity_kind), str8_lit(".workspace"), 0) &&
                   str8_match(uishell_sidebar_string(loose_node.entity_id), local_id, 0),
                   "a local workspace's row is its host entity");
    AndamentoEntity local = {uishell_sidebar_text(str8_lit(".workspace")), uishell_sidebar_text(local_id)};
    LifecycleCheck(uishell_sidebar_card_find(state, local, 0) != ANDAMENTO_NONE, "a local workspace has details for its hover card");
    CFG_Node *home = cfg_node_child_from_string_or_alloc(rd_state->cfg, loose, str8_lit("lives_with"));
    cfg_node_new(rd_state->cfg, home, str8_lit("p"));
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    AndamentoNode homed = {0}, parent = {0};
    B32 found = uishell_workspace_lifecycle_find(state, str8_zero(), loose_id, &homed);
    if(found && homed.parent != ANDAMENTO_NONE) { andamento_snapshot_node(state->snapshot, homed.parent, &parent); }
    LifecycleCheck(found && str8_match(uishell_sidebar_string(parent.entity_kind), str8_lit("project"), 0) &&
                   str8_match(uishell_sidebar_string(parent.entity_id), str8_lit("p"), 0) &&
                   str8_match(uishell_sidebar_string(homed.entity_id), local_id, 0),
                   "a local workspace that lives with a project is placed in its group, live");
    cfg_node_release(rd_state->cfg, home);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    found = uishell_workspace_lifecycle_find(state, str8_zero(), loose_id, &homed);
    AndamentoNode section = {0};
    if(found && homed.parent != ANDAMENTO_NONE) { andamento_snapshot_node(state->snapshot, homed.parent, &section); }
    LifecycleCheck(found && str8_match(uishell_sidebar_string(section.entity_kind), str8_lit(".group"), 0) &&
                   str8_match(uishell_sidebar_string(section.entity_id), uishell_sidebar_default_local_id, 0) &&
                   str8_match(uishell_sidebar_string(homed.entity_id), local_id, 0), "without its project it returns to the default Workspaces group");
    loose_node = homed;

    //- Dragging it onto the project's group moves it there (Move, not ghost),
    //  as a row; dragging that row back to the Workspaces group moves it back.
    AndamentoNode project = {0};
    LifecycleCheck(uishell_workspace_lifecycle_find(state, str8_lit("p"), 0, &project), "fixture project row");
    String8 project_box = push_str8f(scratch.arena, "###project_%S", uishell_sidebar_string(project.key));
    String8 row = push_str8f(scratch.arena, "###entry_%S", uishell_sidebar_string(homed.key));
    B32 lit = uishell_workspace_lifecycle_drag(ws, window, scratch.arena, row, project_box, str8_lit("sidebar_home_project"));
    LifecycleCheck(lit, "a local workspace dragged over a project's group lights the group");
    LifecycleCheck(str8_match(uishell_sidebar_local_home(loose), str8_lit("p"), 0), "dropping it there makes it live with the project");
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    found = uishell_workspace_lifecycle_find(state, str8_zero(), loose_id, &homed);
    LifecycleCheck(found && !str8_match(uishell_sidebar_string(homed.layout), str8_lit("inline"), 0), "it lives in the project's group as a row, not a chip");
    String8 homed_row = push_str8f(scratch.arena, "###entry_%S", uishell_sidebar_string(homed.key));
    // Onto another workspace's row in the Workspaces group.
    String8 workspaces_row = str8_zero();
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode n = {0}; andamento_snapshot_node(state->snapshot, i, &n);
      String8 k = uishell_sidebar_string(n.key);
      if(str8_match(uishell_sidebar_string(n.entity_kind), str8_lit(".workspace"), 0) && n.workspace_id != loose_id &&
         str8_find_needle(k, 0, str8_lit(".section10:workspaces"), 0) < k.size)
      { workspaces_row = push_str8f(scratch.arena, "###sidebar_row_%S", k); break; }
    }
    lit = uishell_workspace_lifecycle_drag(ws, window, scratch.arena, homed_row, workspaces_row, str8_lit("group_drop_line_workspaces"));
    LifecycleCheck(found && lit && !uishell_sidebar_local_home(loose).size &&
                   str8_match(uishell_sidebar_local_field(loose, str8_lit("lives_in")), uishell_sidebar_default_local_id, 0),
                   "its row dragged to the Workspaces group moves it back, with an insertion line there");
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    uishell_workspace_lifecycle_find(state, str8_zero(), loose_id, &loose_node);

    //- Dropped on another local group with Option/Alt held, a workspace stays
    //  at home and that group gets a ghost of it; without, it moves there.
    {
      CFG_Node *builds = uishell_sidebar_local_new_group(window, str8_lit("Builds"));
      String8 builds_id = push_str8_copy(scratch.arena, uishell_sidebar_local_field(builds, str8_lit("id")));
      CFG_Node *seed = cfg_node_new(rd_state->cfg, builds, str8_lit("card"));
      uishell_sidebar_local_set_field(seed, str8_lit("ghost"), str8_lit("lifecycle-seed"));
      uishell_sidebar_local_set_field(seed, str8_lit("kind"), str8_lit("project"));
      uishell_sidebar_local_set_field(seed, str8_lit("entity"), str8_lit("p"));
      cfg_node_new(rd_state->cfg, seed, str8_lit("compact"));
      uishell_sidebar_observe(state, &split);
      uishell_sidebar_refresh(state);
      AndamentoNode seed_node = {0}, row_node = {0};
      uishell_workspace_lifecycle_find(state, str8_lit("lifecycle-seed"), 0, &seed_node);
      uishell_workspace_lifecycle_find(state, str8_zero(), loose_id, &row_node);
      String8 seed_row = push_str8f(scratch.arena, "###entry_%S", uishell_sidebar_string(seed_node.key));
      String8 loose_row = push_str8f(scratch.arena, "###entry_%S", uishell_sidebar_string(row_node.key));
      String8 line = push_str8f(scratch.arena, "group_drop_line_%S", builds_id);
      uishell_sidebar_test_modifiers = WM_Modifier_Alt;
      lit = uishell_workspace_lifecycle_drag(ws, window, scratch.arena, loose_row, seed_row, line);
      uishell_sidebar_test_modifiers = 0;
      B32 ghosted = 0;
      for(CFG_Node *c = builds->first; c != &cfg_nil_node; c = c->next)
      { ghosted |= str8_match(uishell_sidebar_local_field(c, str8_lit("entity")), local_id, 0) && str8_match(uishell_sidebar_local_field(c, str8_lit("kind")), str8_lit(".workspace"), 0); }
      LifecycleCheck(lit && ghosted && !str8_match(uishell_sidebar_local_field(loose, str8_lit("lives_in")), builds_id, 0),
                     "Option-dropping a workspace on another group adds a ghost of it there and leaves it at home");
      uishell_sidebar_observe(state, &split);
      uishell_sidebar_refresh(state);
      uishell_workspace_lifecycle_find(state, str8_zero(), loose_id, &row_node);
      loose_row = push_str8f(scratch.arena, "###entry_%S", uishell_sidebar_string(row_node.key));
      uishell_workspace_lifecycle_find(state, str8_lit("lifecycle-seed"), 0, &seed_node);
      seed_row = push_str8f(scratch.arena, "###entry_%S", uishell_sidebar_string(seed_node.key));
      lit = uishell_workspace_lifecycle_drag(ws, window, scratch.arena, loose_row, seed_row, line);
      LifecycleCheck(lit && str8_match(uishell_sidebar_local_field(loose, str8_lit("lives_in")), builds_id, 0),
                     "dropping a workspace on another group moves it there");
      cfg_node_release(rd_state->cfg, cfg_node_child_from_string(loose, str8_lit("lives_in")));
      cfg_node_release(rd_state->cfg, builds->parent);
      split = uishell_root_controlled_split_from_window(scratch.arena, window);
      uishell_sidebar_observe(state, &split);
      uishell_sidebar_refresh(state);
      uishell_workspace_lifecycle_find(state, str8_zero(), loose_id, &loose_node);
    }
  }
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

  //- The Workspaces group offers New workspace in a footer that opens at its
  //  last row, and reports it hosted chrome. Hovering a row shows detach (subject) or × (none) in the
  //  right margin beside it, leaving the row's own status mark in place.
  {
    UI_State *saved_ui = ui_state, *test = ui_state_alloc();
    ui_select_state(test);
    RD_ChromeNiche saved_niche = ws->chrome_niche[RD_ChromeElementKind_NewWorkspace];
    ws->chrome_niche[RD_ChromeElementKind_NewWorkspace] = RD_ChromeNiche_SectionHeader;
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    B32 entry_row = 0, titled = 0, footer_open[3] = {0};
    Rng2F32 subjectless_row = {0}, subjectless_close = {0};
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
        // Workspaces offers New workspace in its footer (sidebar-headers.md).
        if(str8_match(box->string, str8_lit("###make_section_.section:workspaces"), 0))
        {
          entry_row = 1;
          if(dim_2f32(box->rect).y >= 1 && frame % 2 == 1) { footer_open[hover] = 1; }
          for(UI_Box *a = box->parent; !ui_box_is_nil(a) && !ui_box_is_nil(a->parent); a = a->parent)
          {
            if(ui_key_match(a->key, ui_key_from_string(a->parent->key, str8_lit("###section_header_.section:workspaces"))))
            { entry_row = 0; }
          }
        }
        if(ui_key_match(box->key, ui_key_from_string(box->parent->key, str8_lit("###section_.section:workspaces"))))
        { titled = str8_match(ui_box_display_string(box), str8_lit("WORKSPACES"), 0); }
        for(U64 r = 0; r < row_key_count; r++)
        {
          U64 slot = r == 0 ? 1 : 0;
          if(ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###sidebar_row_%S", row_keys[r])) &&
             row_centers[slot].x == 0)
          { row_centers[slot] = center_2f32(box->rect); }
          if(r == 0 && ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###sidebar_row_%S", row_keys[r])))
          { subjectless_row = box->rect; }
          if(frame % 2 == 1 &&
             ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###close_%S", row_keys[r])))
          {
            close_seen[hover] = 1;
            detach_drawn[hover] = box->custom_draw == rd_workspace_detach_icon_draw && ui_box_display_string(box).size == 0;
            x_drawn[hover] = box->custom_draw == 0 && str8_match(ui_box_display_string(box), str8_lit("×"), 0);
            if(r == 0) { subjectless_close = box->rect; }
          }
        }
      }
    }
    // Clicking the affordance queues its command for that row's workspace:
    // hover to reveal it, then press and release on the button itself.
    for(U32 target = 0; target < 2; target++)
    {
      // The subject is a tree chip and an Attention row; the row hosts the button.
      Vec2F32 hover_at = target == 0 ? row_centers[0] : row_centers[1];
      Vec2F32 button = {0};
      String8 command = target == 0 ? str8_lit("detach_workspace") : str8_lit("close_workspace");
      CFG_ID expected = target == 0 ? subject_id : loose_id;
      U64 queued_before = uishell_workspace_lifecycle_queued(command, expected);
      // Frames 0-1 hover the row (the affordance follows the previous frame's
      // row rect), 2 presses and 3 releases on the button, 4 settles.
      for(U32 frame = 0; frame < 5; frame++)
      {
        UI_IconInfo icons = ws->ui->icon_info;
        UI_AnimationInfo animation = {0}; UI_EventList events = {0}; UI_EventNode event = {0};
        Vec2F32 at = frame < 2 || button.x == 0 ? hover_at : button;
        if(frame == 2 || frame == 3)
        {
          event.v = (UI_Event){.key = WM_Key_LeftMouseButton, .kind = frame == 2 ? UI_EventKind_Press : UI_EventKind_Release,
                               .pos = at, .timestamp_us = 5000000+frame*50000};
          events.first = events.last = &event; events.count = 1;
        }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        ui_state->mouse = at;
        UIShell_RegsScope(.window = window->id)
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
        { uishell_sidebar_ui(r2f32p(0, 0, 320, 900), &split); }
        ui_end_build();
        for(UI_Box *box = test->root; frame == 1 && !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
        {
          for(U64 r = target == 0 ? 1 : 0; r < (target == 0 ? row_key_count : 1); r++)
          {
            if(!ui_box_is_nil(box->parent) && ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###close_%S", row_keys[r])))
            { button = center_2f32(box->rect); }
          }
        }
      }
      B32 queued = uishell_workspace_lifecycle_queued(command, expected) > queued_before;
      LifecycleCheck(button.x > 0, target == 0 ? "detach button has a hit rect" : "close button has a hit rect");
      LifecycleCheck(queued, target == 0 ? "clicking detach queues detach_workspace" : "clicking × queues close_workspace");
    }
    LifecycleCheck(row_centers[0].x > 0 && row_centers[1].x > 0, "subject and subjectless rows are rendered");
    LifecycleCheck(!close_seen[0], "no close affordance without hover");
    LifecycleCheck(close_seen[1] && detach_drawn[1], "hovering a subject row shows detach");
    LifecycleCheck(close_seen[2] && x_drawn[2], "hovering a subjectless row shows ×");
    LifecycleCheck(subjectless_close.x0 >= subjectless_row.x1 && subjectless_row.x1 > 0, "close sits in the margin beside the row");
    LifecycleCheck(ws->chrome_section_header_frame == rd_state->frame_index+1, "Workspaces records the frame it hosted chrome");
    LifecycleCheck(entry_row, "New workspace is the Workspaces group's footer, not a header button");
    // It animates open: hover the group's last row (just above the footer,
    // where it is now) a few frames.
    for(U32 frame = 0; frame < 8; frame++)
    {
      Vec2F32 last_row = row_centers[1];
      for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
      {
        if(str8_match(box->string, str8_lit("###make_section_.section:workspaces"), 0))
        { last_row = v2f32(center_2f32(box->rect).x, box->rect.y0-6.f); }
      }
      UI_IconInfo icons = ws->ui->icon_info;
      UI_AnimationInfo animation = {0}; UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      ui_state->mouse = last_row;
      UIShell_RegsScope(.window = window->id)
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
      { uishell_sidebar_ui(r2f32p(0, 0, 320, 900), &split); }
      ui_end_build();
    }
    for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
    { if(str8_match(box->string, str8_lit("###make_section_.section:workspaces"), 0) && dim_2f32(box->rect).y >= 1) { footer_open[2] = 1; } }
    LifecycleCheck(!footer_open[0] && footer_open[2], "the footer opens when the pointer reaches the Workspaces group's last row");
    LifecycleCheck(titled, "the local workspace section is titled Workspaces");

    // Hovering the margin control shows its tooltip beside it, at its own
    // size. Holding it past the threshold opens the row's menu, and the
    // release that follows doesn't close the workspace.
    // Frames: 0-1 hover the row, 2 hovers the button, 3 presses, 4 holds,
    // 5 releases, 6 settles.
    {
      Vec2F32 button = {0};
      U64 queued_before = uishell_workspace_lifecycle_queued(str8_lit("close_workspace"), loose_id);
      B32 menu_open = 0, tip_beside = 0, tip_sized = 0;
      for(U32 frame = 0; frame < 7; frame++)
      {
        UI_IconInfo icons = ws->ui->icon_info;
        UI_AnimationInfo animation = {0}; UI_EventList events = {0}; UI_EventNode event = {0};
        Vec2F32 at = frame < 2 || button.x == 0 ? row_centers[1] : button;
        if(frame == 3 || frame == 5)
        {
          event.v = (UI_Event){.key = WM_Key_LeftMouseButton, .kind = frame == 3 ? UI_EventKind_Press : UI_EventKind_Release,
                               .pos = at, .timestamp_us = 6000000+frame*50000};
          events.first = events.last = &event; events.count = 1;
        }
        if(frame == 4) { sleep_ms(UIShell_HoldUS/1000+50); }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        ui_state->mouse = at;
        UIShell_RegsScope(.window = window->id)
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
        { uishell_sidebar_ui(r2f32p(0, 0, 320, 900), &split); }
        ui_end_build();
        if(frame == 2)
        {
          UI_Box *label = &ui_nil_box;
          for(UI_Box *b = ui_state->tooltip_root; !ui_box_is_nil(b) && ui_box_is_nil(label); b = ui_box_rec_df_pre(b, ui_state->tooltip_root).next)
          { if(str8_match(ui_box_display_string(b), str8_lit("Close workspace"), 0)) { label = b; } }
          F32 text = fnt_dim_from_tag_size_string(label->font, label->font_size, 0, 0, str8_lit("Close workspace")).x;
          tip_sized = !ui_box_is_nil(label) && dim_2f32(label->rect).x >= text;
          // Horizontal only: a window shorter than the sidebar clamps the
          // tooltip vertically (headless CI), while the inherited floating
          // position pushed it far to the right.
          tip_beside = !ui_box_is_nil(label) && label->rect.x0 < button.x+50.f;
        }
        if(frame == 6) { menu_open = ui_any_ctx_menu_is_open(); }
        for(UI_Box *box = test->root; frame == 1 && !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
        {
          if(!ui_box_is_nil(box->parent) && ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###close_%S", row_keys[0])))
          { button = center_2f32(box->rect); }
        }
      }
      LifecycleCheck(tip_sized, "margin control tooltip fits its label");
      LifecycleCheck(tip_beside, "margin control tooltip sits beside the control");
      LifecycleCheck(menu_open, "holding the margin control opens the row menu");
      LifecycleCheck(uishell_workspace_lifecycle_queued(str8_lit("close_workspace"), loose_id) == queued_before,
        "releasing after a hold doesn't close");
      ui_ctx_menu_close();
    }
    // A press released off the control ends the hold: once the pointer is
    // away, the control is no longer drawn. Frames: 0-1 hover the row, 2
    // presses on the control, 3 releases away, 4-5 stay away.
    {
      Vec2F32 button = {0};
      B32 lingering = 0;
      for(U32 frame = 0; frame < 6; frame++)
      {
        UI_IconInfo icons = ws->ui->icon_info;
        UI_AnimationInfo animation = {0}; UI_EventList events = {0}; UI_EventNode event = {0};
        Vec2F32 at = frame < 2 || button.x == 0 ? row_centers[1] : frame == 2 ? button : v2f32(-100, -100);
        if(frame == 2 || frame == 3)
        {
          event.v = (UI_Event){.key = WM_Key_LeftMouseButton, .kind = frame == 2 ? UI_EventKind_Press : UI_EventKind_Release,
                               .pos = at, .timestamp_us = 7000000+frame*50000};
          events.first = events.last = &event; events.count = 1;
        }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        ui_state->mouse = at;
        UIShell_RegsScope(.window = window->id)
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
        { uishell_sidebar_ui(r2f32p(0, 0, 320, 900), &split); }
        ui_end_build();
        for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
        {
          if(ui_box_is_nil(box->parent) || !ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###close_%S", row_keys[0]))) { continue; }
          if(frame == 1) { button = center_2f32(box->rect); }
          if(frame == 5) { lingering = 1; }
        }
      }
      LifecycleCheck(button.x > 0, "release-away control has a hit rect");
      LifecycleCheck(!lingering, "a press released off the control doesn't keep it shown");
    }

    // Collapsed, the Workspaces group draws no entry row and doesn't claim
    // the niche, so new-workspace moves to the sidebar action row.
    {
      UIShell_SidebarSection *workspaces = state->sections;
      for(; workspaces && !str8_match(workspaces->key, str8_lit(".section:workspaces"), 0); workspaces = workspaces->next) {}
      LifecycleCheck(workspaces != 0, "Workspaces section state exists");
      if(workspaces)
      {
        workspaces->collapsed = 1;
        ws->chrome_section_header_frame = 0;
        B32 entry_drawn = 0;
        UI_IconInfo icons = ws->ui->icon_info;
        UI_AnimationInfo animation = {0}; UI_EventList events = {0};
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        ui_state->mouse = v2f32(-100, -100);
        UIShell_RegsScope(.window = window->id)
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
        { uishell_sidebar_ui(r2f32p(0, 0, 320, 900), &split); }
        ui_end_build();
        for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
        {
          if(str8_match(box->string, str8_lit("###make_section_.section:workspaces"), 0)) { entry_drawn = 1; }
        }
        LifecycleCheck(!entry_drawn, "collapsed Workspaces draws no New workspace footer");
        LifecycleCheck(ws->chrome_section_header_frame == 0, "collapsed Workspaces leaves the niche to the action row");
        workspaces->collapsed = 0;
      }
    }
    ws->chrome_niche[RD_ChromeElementKind_NewWorkspace] = saved_niche;
    ui_select_state(saved_ui); ui_state_release(test);
  }

  UIShell_RegsScope(.window = window->id, .cfg = loose_id) { uishell_dispatch_window_command(str8_lit("detach_workspace")); }
  LifecycleCheck(cfg_node_from_id(loose_id) == &cfg_nil_node, "detaching a subjectless workspace destroys it");
  {
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    AndamentoNode gone = {0};
    AndamentoEntity local = {uishell_sidebar_text(str8_lit(".workspace")), uishell_sidebar_text(local_id)};
    LifecycleCheck(!uishell_workspace_lifecycle_find(state, local_id, 0, &gone) && uishell_sidebar_card_find(state, local, 0) == ANDAMENTO_NONE,
                   "a destroyed local workspace's entity is retracted");
  }

  //- A detached workspace keeps its name reserved: it reopens under it.
  {
    CFG_Node *label = cfg_node_child_from_string(subject, str8_lit("label"));
    LifecycleCheck(label != &cfg_nil_node, "subject workspace has a label");
    String8 saved = push_str8_copy(scratch.arena, rd_label_from_cfg(subject));
    cfg_node_equip_string(rd_state->cfg, label->first, str8_lit("Workspace 1"));
    UIShell_RegsScope(.window = window->id, .cfg = subject_id) { uishell_dispatch_window_command(str8_lit("detach_workspace")); }
    CFG_Node *fresh = uishell_new_workspace(window);
    LifecycleCheck(str8_match(subject->string, str8_lit("detached_workspace"), 0) &&
                   !str8_match(rd_label_from_cfg(fresh), str8_lit("Workspace 1"), 0),
                   "a new default name skips a detached workspace's name");
    cfg_node_release(rd_state->cfg, fresh);
    cfg_node_equip_string(rd_state->cfg, label->first, saved);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    uishell_workspace_lifecycle_find(state, str8_lit("multi"), 0, &node);
    LifecycleCheck(uishell_sidebar_dispatch(state, node.activate, 0), "reopen subject after name check");
    uishell_sidebar_effects(state, &split);
    LifecycleCheck(str8_match(subject->string, str8_lit("workspace"), 0), "subject reopened after name check");
  }

  //- Detaching a workspace whose subject ended destroys it, even from the palette:
  //  its row will never open it again.
  {
    // Native status is field 2 (label/kind/status). The fixture's vessel line
    // omits kind, so give it the production field order for this check.
    String8 config = str8_cstring((char *)uishell_sidebar_fixture_config);
    String8 status_field = str8_lit("field \"status\" key=\"status.state\"");
    U64 at = str8_find_needle(config, 0, status_field, 0);
    LifecycleCheck(at < config.size, "fixture vessel template has a status field");
    String8 ordered = push_str8f(scratch.arena, "%S field \"kind\" source=\"literal\" value=\"vessel\"\n  %S",
                                 str8_prefix(config, at), str8_skip(config, at));
    LifecycleCheck(andamento_configure(state->core, uishell_sidebar_text(ordered), 0), "reconfigure with native field order");
    AndamentoFact end_fact = {0};
    end_fact.key = uishell_sidebar_text(str8_lit("flotilla.convoy.phase"));
    end_fact.kind = ANDAMENTO_FACT_TEXT;
    end_fact.text = uishell_sidebar_text(str8_lit("landed"));
    LifecycleCheck(andamento_apply_entity(state->core, 0, uishell_sidebar_text(str8_lit("vessel")), uishell_sidebar_text(str8_lit("multi")),
                                          uishell_sidebar_text(str8_lit("fixture")), &end_fact, 1, 0), "end the fixture subject");
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    LifecycleCheck(uishell_sidebar_workspace_subject_ended(ws, subject_id), "sidebar reports the subject ended");
    ws->root_controlled_split_selected_workspace_id = subject_id;
    UIShell_RegsScope(.window = window->id) { uishell_dispatch_window_command(str8_lit("detach_workspace")); }
    LifecycleCheck(cfg_node_from_id(subject_id) == &cfg_nil_node, "detaching an ended subject's workspace destroys it");
  }

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

  //- A workspace whose group is gone lives in the default group, whatever id
  //  the default group has: as one of its items, not a leftover Andamento
  //  covers there (its key shows which loop placed it).
  {
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    CFG_Node *workspace = split.inventory.first->mount.owner_cfg;
    CFG_Node *home = uishell_sidebar_local_group(window, uishell_sidebar_default_local_id);
    uishell_sidebar_local_set_field(home, str8_lit("id"), str8_lit("home"));
    uishell_sidebar_local_set_field(workspace, str8_lit("lives_in"), str8_lit("gone"));
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    AndamentoNode row = {0};
    B32 found = uishell_workspace_lifecycle_find(state, uishell_sidebar_local_entity(workspace), 0, &row);
    AndamentoNode parent = {0};
    B32 placed = found && row.parent != ANDAMENTO_NONE && andamento_snapshot_node(state->snapshot, row.parent, &parent) &&
      str8_match(uishell_sidebar_string(parent.entity_id), str8_lit("home"), 0) &&
      str8_find_needle(uishell_sidebar_string(row.key), 0, str8_lit(".unplaced"), 0) == uishell_sidebar_string(row.key).size;
    LifecycleCheck(placed, "a workspace whose group is gone lives in the default group, whatever its id");
    uishell_sidebar_local_set_field(home, str8_lit("id"), uishell_sidebar_default_local_id);
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(workspace, str8_lit("lives_in")));
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
  }

  fprintf(stderr, "Workspace lifecycle diagnostics: %s (detach/reopen, palette target, margin close and hold menu, destroy, Workspaces entry row, reserved names, ended detach, unique names, last close, legacy last close, default group by mark)\n",
          ok ? "passed" : "FAILED");
  scratch_end(scratch);
  return ok;
}

#undef LifecycleCheck
