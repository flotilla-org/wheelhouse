// Managing local sections and groups (uishell_local_groups.c): titles borrow
// a single group's name, renames, new groups, deletes moving workspaces to
// the default group, New workspace here, and the header menus. Runs after
// the workspace lifecycle diagnostics on the same fixture window.

#define GroupsCheck(cond, msg) do { if(!(cond)) { ok = 0; fprintf(stderr, "FAIL local groups: %s\n", msg); } } while(0)

// The snapshot label of the section or group published for `kind`/`id`.
internal String8
uishell_local_groups_label(Arena *arena, UIShell_SidebarState *state, String8 kind, String8 id)
{
  for(U64 i = 0; state->snapshot && i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode n = {0}; andamento_snapshot_node(state->snapshot, i, &n);
    if(str8_match(uishell_sidebar_string(n.entity_kind), kind, 0) && str8_match(uishell_sidebar_string(n.entity_id), id, 0))
    { return push_str8_copy(arena, uishell_sidebar_string(n.label)); }
  }
  return str8_zero();
}

internal void
uishell_local_groups_publish(UIShell_SidebarState *state, CFG_Node *window, Arena *arena)
{
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(arena, window);
  uishell_sidebar_publish_local(state, &split);
  uishell_sidebar_observe(state, &split);
  uishell_sidebar_refresh(state);
}

// The section Views the tests render, as docked Views render them: the
// first above, a second (when set) below.
global CFG_Node *uishell_local_groups_view = &cfg_nil_node;
global CFG_Node *uishell_local_groups_view2 = &cfg_nil_node;

internal void
uishell_local_groups_render(CFG_Node *window, UIShell_ControlledSplit *split)
{
  CFG_Node *views[] = {uishell_local_groups_view, uishell_local_groups_view2};
  for(U64 i = 0; i < ArrayCount(views); i++)
  {
    if(views[i] == &cfg_nil_node) { continue; }
    String8 section = cfg_node_child_from_string(views[i], str8_lit("section"))->first->string;
    Rng2F32 rect = r2f32p(0, 300.f*i, 320, 300.f*(i+1));
    UIShell_RegsScope(.window = window->id, .view = views[i]->id, .panel = views[i]->parent->id)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    {
      UI_Box *parent;
      UI_Rect(rect) { parent = ui_build_box_from_stringf(UI_BoxFlag_Clip, "###local_groups_view_%I64u", i); }
      UI_Parent(parent) { uishell_sidebar_render(rect, split, (UIShell_SidebarRenderParams){UIShell_SidebarRenderMode_SectionPanel, section}); }
    }
  }
}

// The tests' clock: each frame takes 50ms, and a pause separates gestures,
// so two clicks are a double click only when meant to be.
global U64 uishell_local_groups_clock_us = 1000000;

internal void
uishell_local_groups_pause(void)
{
  uishell_local_groups_clock_us += 5000000;
}

// One frame of those Views, with `event` (if any) at the box keyed `suffix`,
// found in the last frame's layout. Returns whether it was found.
internal B32
uishell_local_groups_frame(RD_WindowState *ws, CFG_Node *window, Arena *arena, String8 suffix, UI_EventKind kind, WM_Key key)
{
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(arena, window);
  uishell_local_groups_clock_us += 50000;
  Vec2F32 at = suffix.size ? uishell_workspace_lifecycle_center(ui_state, suffix) : v2f32(-100, -100);
  UI_IconInfo icons = ws->ui->icon_info;
  UI_AnimationInfo animation = {0}; UI_EventList events = {0}; UI_EventNode event = {0};
  if(kind != UI_EventKind_Null)
  {
    event.v = (UI_Event){.kind = kind, .key = key, .pos = at, .timestamp_us = uishell_local_groups_clock_us,
      .slot = key == WM_Key_Return ? UI_EventActionSlot_Accept : key == WM_Key_Esc ? UI_EventActionSlot_Cancel : UI_EventActionSlot_Null};
    events.first = events.last = &event; events.count = 1;
  }
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = at;
  uishell_local_groups_render(window, &split);
  ui_end_build();
  return !suffix.size || at.x != 0 || at.y != 0;
}

// Drags the box keyed `from` onto the box keyed `to` (or, with `dock`, onto
// a docking site below the first View's panel): press, move past the
// threshold, release, finishing the drag each frame as a window does.
internal B32
uishell_local_groups_drag(RD_WindowState *ws, CFG_Node *window, Arena *arena, String8 from, String8 to, B32 dock)
{
  Vec2F32 start = {0}, target = {0};
  B32 started = 0;
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
    if(frame == 5 && dock) { uishell_sidebar_drag_panel_drop(uishell_local_groups_view->parent->id, Dir2_Down, 0); }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = at;
    uishell_local_groups_render(window, &split);
    uishell_sidebar_drag_finish(ws);
    ui_end_build();
    if(frame == 0) { start = uishell_workspace_lifecycle_center(ui_state, from); }
    if(frame >= 2) { target = dock ? v2f32(-50, -50) : uishell_workspace_lifecycle_center(ui_state, to); }
    if(frame == 3) { started = rd_drag_is_active(); }
  }
  rd_drag_kill(); ui_kill_action();
  return started;
}

// The `###entry_` suffix of the row presenting local entity `id`.
internal String8
uishell_local_groups_row(Arena *arena, UIShell_SidebarState *state, String8 id)
{
  return push_str8f(arena, "###entry_%S", uishell_sidebar_string(uishell_sidebar_reorder_node(state, id).key));
}

// A ghost of project `p` in `group`, so the group has a row to drop on.
internal void
uishell_local_groups_seed(CFG_Node *group, String8 ghost)
{
  CFG_Node *seed = cfg_node_new(rd_state->cfg, group, str8_lit("card"));
  uishell_sidebar_local_set_field(seed, str8_lit("ghost"), ghost);
  uishell_sidebar_local_set_field(seed, str8_lit("kind"), str8_lit("project"));
  uishell_sidebar_local_set_field(seed, str8_lit("entity"), str8_lit("p"));
  cfg_node_new(rd_state->cfg, seed, str8_lit("compact"));
}

// Clicks the box keyed `suffix` with `button`, then lets a frame settle.
// With `again`, it follows the last click closely enough to double it.
internal B32
uishell_local_groups_click_(RD_WindowState *ws, CFG_Node *window, Arena *arena, String8 suffix, WM_Key button, B32 again)
{
  if(!again) { uishell_local_groups_pause(); }
  uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
  B32 found = uishell_local_groups_frame(ws, window, arena, suffix, UI_EventKind_Press, button);
  found = found && uishell_local_groups_frame(ws, window, arena, suffix, UI_EventKind_Release, button);
  uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
  return found;
}
#define uishell_local_groups_click(ws, window, arena, suffix, button) uishell_local_groups_click_((ws), (window), (arena), (suffix), (button), 0)
#define uishell_local_groups_double_click(ws, window, arena, suffix) \
  (uishell_local_groups_click_((ws), (window), (arena), (suffix), WM_Key_LeftMouseButton, 0), \
   uishell_local_groups_click_((ws), (window), (arena), (suffix), WM_Key_LeftMouseButton, 1))

internal B32
uishell_local_groups_diagnostics(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  B32 ok = 1;
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  UIShell_SidebarState *state = ws != &rd_nil_window_state ? uishell_sidebar_init(ws) : 0;
  GroupsCheck(state && state->core, "sidebar fixture core");
  if(!ok) { scratch_end(scratch); return 0; }

  //- One group: the section borrows its name, and renaming either renames both.
  CFG_Node *builds = uishell_sidebar_local_new_group(window, str8_lit("Builds"));
  CFG_Node *section = builds->parent;
  String8 section_id = push_str8_copy(arena, uishell_sidebar_local_field(section, str8_lit("id")));
  String8 builds_id = push_str8_copy(arena, uishell_sidebar_local_field(builds, str8_lit("id")));
  uishell_local_groups_publish(state, window, arena);
  GroupsCheck(str8_match(uishell_local_groups_label(arena, state, str8_lit(".section"), section_id), str8_lit("Builds"), 0),
              "a section with one group and no name of its own shows the group's name");
  uishell_sidebar_local_rename(section, str8_lit("Nightly"));
  GroupsCheck(str8_match(uishell_sidebar_local_field(builds, str8_lit("label")), str8_lit("Nightly"), 0) &&
              !uishell_sidebar_local_field(section, str8_lit("label")).size,
              "renaming a section that borrows its only group's name renames the group");

  //- A second group: the section shows both groups' names, and groups show their own.
  CFG_Node *tests = uishell_sidebar_local_add_group(section, str8_lit("Tests"));
  String8 tests_id = push_str8_copy(arena, uishell_sidebar_local_field(tests, str8_lit("id")));
  uishell_local_groups_publish(state, window, arena);
  GroupsCheck(str8_match(uishell_local_groups_label(arena, state, str8_lit(".section"), section_id), str8_lit("Nightly, Tests"), 0) &&
              str8_match(uishell_local_groups_label(arena, state, str8_lit(".group"), tests_id), str8_lit("Tests"), 0),
              "with several groups and no name of its own, a section shows their names, joined");
  uishell_sidebar_local_rename(section, str8_lit("CI"));
  GroupsCheck(str8_match(uishell_sidebar_local_title(arena, section), str8_lit("CI"), 0) &&
              str8_match(uishell_sidebar_local_field(builds, str8_lit("label")), str8_lit("Nightly"), 0),
              "with several groups, renaming the section names the section only");

  //- New workspace here lives in the group; deleting the group moves it home.
  CFG_Node *made = &cfg_nil_node;
  {
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(arena, window);
    U64 before = split.inventory.count;
    uishell_sidebar_local_new_workspace(window, tests);
    split = uishell_root_controlled_split_from_window(arena, window);
    for(UIShell_MaterializedWorkspace *w = split.inventory.first; w; w = w->next)
    { if(str8_match(uishell_sidebar_local_field(w->mount.owner_cfg, str8_lit("lives_in")), tests_id, 0)) { made = w->mount.owner_cfg; } }
    GroupsCheck(split.inventory.count == before+1 && made != &cfg_nil_node, "New workspace here opens a workspace living in the group");
  }
  GroupsCheck(uishell_sidebar_local_homed_count(window, tests) == 1 && uishell_sidebar_local_homed_count(window, section) == 1,
              "deleting the group or its section would move one workspace");
  uishell_sidebar_local_delete_group(window, tests);
  GroupsCheck(uishell_sidebar_local_group(window, tests_id) == &cfg_nil_node &&
              cfg_node_child_from_string(made, str8_lit("lives_in")) == &cfg_nil_node,
              "deleting a group moves its workspaces to the default group");
  GroupsCheck(str8_match(uishell_sidebar_local_title(arena, section), str8_lit("CI"), 0), "a name you gave the section sticks with one group again");
  uishell_sidebar_local_rename(section, str8_zero());
  GroupsCheck(str8_match(uishell_sidebar_local_title(arena, section), str8_lit("Nightly"), 0), "clearing the section's name makes it show its group's again");

  //- The default section and group can't be deleted.
  CFG_Node *workspaces = uishell_sidebar_local_group(window, uishell_sidebar_default_local_id);
  uishell_sidebar_local_delete_group(window, workspaces);
  uishell_sidebar_local_delete_section(window, workspaces->parent);
  GroupsCheck(uishell_sidebar_local_group(window, uishell_sidebar_default_local_id) == workspaces, "the default section and group stay");

  //- The section menu: right-click its title. New group adds a group and
  //  opens its rename; Enter applies the name typed.
  {
    UI_State *saved_ui = ui_state, *test = ui_state_alloc();
    ui_select_state(test);
    CFG_Node *panel = cfg_node_new(rd_state->cfg, cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT), str8_lit("0.2"));
    uishell_local_groups_view = uishell_sidebar_local_new_view(panel, section);
    uishell_local_groups_publish(state, window, arena);
    String8 key = uishell_sidebar_local_key(arena, section_id);
    String8 title = push_str8f(arena, "###section_%S", key);
    //- Double-clicking the title renames in place, leaving the section open;
    //  Esc cancels, Enter applies.
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    uishell_local_groups_double_click(ws, window, arena, title);
    // The field, holding the name, in place of the title.
    B32 field = !ui_box_is_nil(uishell_sidebar_reorder_box(ui_state->root, str8_lit("Nightly"))) &&
      ui_box_is_nil(uishell_sidebar_reorder_box(ui_state->root, title));
    GroupsCheck(field && uishell_sidebar_local_renaming(state, section) &&
                cfg_node_child_from_string(uishell_local_groups_view, str8_lit("section_collapsed")) == &cfg_nil_node,
                "double-clicking a section's title swaps it for a name field, leaving the section open");
    char cancelled[] = "Discarded";
    MemoryCopy(state->rename_text, cancelled, sizeof(cancelled)-1); state->rename_size = sizeof(cancelled)-1;
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Press, WM_Key_Esc);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    GroupsCheck(state->rename_node == 0 && str8_match(uishell_sidebar_local_field(builds, str8_lit("label")), str8_lit("Nightly"), 0),
                "Esc in the name field cancels the rename");
    uishell_local_groups_double_click(ws, window, arena, title);
    char applied[] = "Builds";
    MemoryCopy(state->rename_text, applied, sizeof(applied)-1); state->rename_size = sizeof(applied)-1;
    state->rename_cursor = state->rename_mark = txt_pt(1, state->rename_size+1);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Press, WM_Key_Return);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    GroupsCheck(state->rename_node == 0 && str8_match(uishell_sidebar_local_field(builds, str8_lit("label")), str8_lit("Builds"), 0),
                "Enter applies the name: here, the section's only group's");
    //- The menu: New group adds a group and renames it in its own header.
    B32 found = uishell_local_groups_click(ws, window, arena, title, WM_Key_RightMouseButton);
    GroupsCheck(found && ui_any_ctx_menu_is_open(), "right-clicking a section's title opens its menu");
    UI_Box *title_box = uishell_sidebar_reorder_box(ui_state->root, title);
    Vec2F32 opened_at = add_2f32(title_box->rect.p0, ui_state->ctx_menu_anchor_off);
    GroupsCheck(length_2f32(sub_2f32(opened_at, center_2f32(title_box->rect))) < 1.f, "the menu opens where the pointer was");
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    F32 menu_width = dim_2f32(ui_state->ctx_menu_root->rect).x;
    F32 item_width = 0;
    for(UI_Box *b = ui_state->ctx_menu_root->first; !ui_box_is_nil(b); b = b->next)
    { if(b->flags & UI_BoxFlag_Clickable) { item_width = Max(item_width, dim_2f32(b->rect).x); } }
    GroupsCheck(item_width > 0 && abs_f32(menu_width-item_width) < 1.f, "the menu is as wide as its items, with no margin to the right");
    U64 groups_before = uishell_sidebar_local_group_count(section);
    found = uishell_local_groups_click(ws, window, arena, str8_lit("New group"), WM_Key_LeftMouseButton);
    CFG_Node *added = cfg_node_from_id(state->rename_node);
    GroupsCheck(found && uishell_sidebar_local_group_count(section) == groups_before+1 && added->parent == section &&
                !ui_any_ctx_menu_is_open(), "New group adds a group and starts renaming it");
    uishell_local_groups_publish(state, window, arena);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    AndamentoNode added_node = uishell_sidebar_reorder_node(state, uishell_sidebar_local_field(added, str8_lit("id")));
    String8 added_entry = push_str8f(arena, "###entry_%S", uishell_sidebar_string(added_node.key));
    GroupsCheck(!ui_box_is_nil(uishell_sidebar_reorder_box(ui_state->root, str8_lit("New group"))) &&
                ui_box_is_nil(uishell_sidebar_reorder_box(ui_state->root, added_entry)),
                "the new group's header holds the name field");
    char typed[] = "Releases";
    MemoryCopy(state->rename_text, typed, sizeof(typed)-1); state->rename_size = sizeof(typed)-1;
    state->rename_cursor = state->rename_mark = txt_pt(1, state->rename_size+1);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Press, WM_Key_Return);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    GroupsCheck(str8_match(uishell_sidebar_local_field(added, str8_lit("label")), str8_lit("Releases"), 0),
                "Enter in the header's field names the new group");
    // Two groups now: each has a header, whose menu deletes it.
    uishell_local_groups_publish(state, window, arena);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    AndamentoNode group_node = uishell_sidebar_reorder_node(state, uishell_sidebar_local_field(added, str8_lit("id")));
    String8 group_row = push_str8f(arena, "###entry_%S", uishell_sidebar_string(group_node.key));
    found = uishell_local_groups_click(ws, window, arena, group_row, WM_Key_RightMouseButton);
    GroupsCheck(found && ui_any_ctx_menu_is_open(), "right-clicking a group's header opens its menu");
    String8 added_id = push_str8_copy(arena, uishell_sidebar_local_field(added, str8_lit("id")));
    found = uishell_local_groups_click(ws, window, arena, str8_lit("Delete group"), WM_Key_LeftMouseButton);
    GroupsCheck(found && uishell_sidebar_local_group(window, added_id) == &cfg_nil_node, "Delete group with no workspaces deletes it at once");
    ui_ctx_menu_close();
    ui_select_state(saved_ui); ui_state_release(test);

    //- Deleting the section closes its View and deletes its groups.
    uishell_sidebar_local_delete_section(window, section);
    GroupsCheck(uishell_sidebar_local_section(window, section_id) == &cfg_nil_node &&
                uishell_sidebar_local_group(window, builds_id) == &cfg_nil_node &&
                uishell_sidebar_region_view(window, key) == &cfg_nil_node,
                "deleting a section deletes its groups and closes its View");
    cfg_node_release(rd_state->cfg, panel);
    uishell_local_groups_view = &cfg_nil_node;
  }
  //- Dragging a group's header moves the group: into another section after
  //  the group dropped on, or to a docking site as a section of its own.
  {
    UI_State *saved_ui = ui_state, *test = ui_state_alloc();
    ui_select_state(test);
    CFG_Node *sidebar_root = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
    CFG_Node *top = cfg_node_new(rd_state->cfg, sidebar_root, str8_lit("0.2"));
    CFG_Node *bottom = cfg_node_new(rd_state->cfg, sidebar_root, str8_lit("0.2"));
    CFG_Node *alpha = uishell_sidebar_local_new_group(window, str8_lit("Alpha"));
    CFG_Node *beta = uishell_sidebar_local_add_group(alpha->parent, str8_lit("Beta"));
    CFG_Node *gamma = uishell_sidebar_local_new_group(window, str8_lit("Gamma"));
    uishell_local_groups_seed(alpha, str8_lit("groups-alpha"));
    uishell_local_groups_seed(beta, str8_lit("groups-beta"));
    uishell_local_groups_seed(gamma, str8_lit("groups-gamma"));
    CFG_Node *first = alpha->parent, *second = gamma->parent;
    String8 beta_id = push_str8_copy(arena, uishell_sidebar_local_field(beta, str8_lit("id")));
    uishell_local_groups_view = uishell_sidebar_local_new_view(top, first);
    uishell_local_groups_view2 = uishell_sidebar_local_new_view(bottom, second);
    uishell_local_groups_publish(state, window, arena);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    B32 started = uishell_local_groups_drag(ws, window, arena, uishell_local_groups_row(arena, state, beta_id),
                                            uishell_local_groups_row(arena, state, str8_lit("groups-gamma")), 0);
    GroupsCheck(started && beta->parent == second && beta->prev == gamma && uishell_sidebar_local_group_count(first) == 1 &&
                cfg_node_child_from_string(beta, str8_lit("card")) != &cfg_nil_node,
                "a group's header dropped on a group in another section moves it there, after that group, with its items");
    GroupsCheck(str8_match(uishell_sidebar_local_title(arena, second), str8_lit("Gamma, Beta"), 0),
                "the section it joined shows both groups' names");
    // Back to the first section, then out to a docking site.
    uishell_local_groups_publish(state, window, arena);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    uishell_local_groups_drag(ws, window, arena, uishell_local_groups_row(arena, state, beta_id),
                              uishell_local_groups_row(arena, state, str8_lit("groups-alpha")), 0);
    GroupsCheck(beta->parent == first, "and dropped back, it returns");
    U64 sections_before = 0;
    for(CFG_Node *n = first->parent->first; n != &cfg_nil_node; n = n->next) { sections_before += str8_match(n->string, str8_lit("section"), 0); }
    uishell_local_groups_publish(state, window, arena);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    started = uishell_local_groups_drag(ws, window, arena, uishell_local_groups_row(arena, state, beta_id), str8_zero(), 1);
    CFG_Node *own = beta->parent;
    U64 sections_after = 0;
    for(CFG_Node *n = first->parent->first; n != &cfg_nil_node; n = n->next) { sections_after += str8_match(n->string, str8_lit("section"), 0); }
    GroupsCheck(started && own != first && sections_after == sections_before+1 && uishell_sidebar_local_group_count(own) == 1 &&
                str8_match(uishell_sidebar_local_title(arena, own), str8_lit("Beta"), 0) &&
                uishell_sidebar_local_view(window, beta) != &cfg_nil_node,
                "dropped on a docking site, a group becomes a section of its own, shown there, borrowing its name");
    GroupsCheck(str8_match(uishell_sidebar_local_title(arena, first), str8_lit("Alpha"), 0), "the section it left borrows its remaining group's name");
    CFG_Node *own_view = uishell_sidebar_local_view(window, beta);
    if(own_view != &cfg_nil_node) { cfg_node_release(rd_state->cfg, own_view); }

    //- A section showing one group is that group: dragging its title onto a
    //  group in another section moves the group there, and the emptied
    //  section and its View go.
    String8 second_id = push_str8_copy(arena, uishell_sidebar_local_field(second, str8_lit("id")));
    uishell_local_groups_publish(state, window, arena);
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    String8 alpha_row = uishell_local_groups_row(arena, state, str8_lit("groups-alpha"));
    uishell_local_groups_frame(ws, window, arena, str8_zero(), UI_EventKind_Null, 0);
    UIShell_RegsScope(.window = window->id, .view = uishell_local_groups_view2->id, .panel = bottom->id, .tab = uishell_local_groups_view2->id)
    { rd_drag_begin(UIShell_ContextRegSlot_View); }
    B32 dragging = rd_drag_is_active();
    for(U64 frame = 0; frame < 3; frame++)
    {
      if(frame == 2) { rd_state->drag_drop_state = RD_DragDropState_Dropping; }
      UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(arena, window);
      Vec2F32 at = uishell_workspace_lifecycle_center(ui_state, alpha_row);
      UI_IconInfo icons = ws->ui->icon_info; UI_AnimationInfo animation = {0}; UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      ui_state->mouse = at;
      uishell_local_groups_render(window, &split);
      uishell_sidebar_drag_finish(ws);
      ui_end_build();
    }
    rd_drag_kill();
    GroupsCheck(dragging && gamma->parent == first && gamma->prev == alpha &&
                uishell_sidebar_local_section(window, second_id) == &cfg_nil_node &&
                cfg_node_child_from_string(bottom, str8_lit("sidebar_section")) == &cfg_nil_node,
                "a one-group section's title dropped on another section's group moves its group there, and the section and its View go");
    uishell_local_groups_view2 = &cfg_nil_node;
    // Each section that remains, once.
    String8 made_ids[] = {push_str8_copy(arena, uishell_sidebar_local_field(own, str8_lit("id"))),
      push_str8_copy(arena, uishell_sidebar_local_field(first, str8_lit("id"))), second_id};
    for(U64 i = 0; i < ArrayCount(made_ids); i++)
    {
      CFG_Node *left = uishell_sidebar_local_section(window, made_ids[i]);
      if(left != &cfg_nil_node) { cfg_node_release(rd_state->cfg, left); }
    }
    cfg_node_release(rd_state->cfg, top);
    cfg_node_release(rd_state->cfg, bottom);
    uishell_sidebar_manual_sizing(window, 0);
    uishell_local_groups_view = uishell_local_groups_view2 = &cfg_nil_node;
    ui_select_state(saved_ui); ui_state_release(test);
  }

  if(made != &cfg_nil_node) { UIShell_RegsScope(.window = window->id, .cfg = made->id) { uishell_dispatch_window_command(str8_lit("close_workspace")); } }
  uishell_local_groups_publish(state, window, arena);

  fprintf(stderr, "Local groups diagnostics: %s (borrowed titles, rename, second group, new workspace here, delete group, delete section, default kept, rename in place, Esc, section menu, new group renamed in its header, group menu, group drag between sections, group to a docking site, one-group section title drag)\n",
          ok ? "passed" : "FAILED");
  scratch_end(scratch);
  return ok;
}

#undef GroupsCheck
