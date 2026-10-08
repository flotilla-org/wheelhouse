//- Managing local sections and groups (drag-model.md, "Closing and deleting"
// and "Titles"). The window's `sidebar_local` node stays the source of truth:
// these change it, and publishing (uishell_sidebar_publish_local) follows.

internal void uishell_sidebar_order_reset_button(UIShell_SidebarState *state, String8 loop);
internal CFG_Node *uishell_new_workspace(CFG_Node *window);

// The local section with this id, or nil.
internal CFG_Node *
uishell_sidebar_local_section(CFG_Node *window, String8 id)
{
  if(!id.size) { return &cfg_nil_node; }
  CFG_Node *root = cfg_node_child_from_string(window, str8_lit("sidebar_local"));
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    if(str8_match(section->string, str8_lit("section"), 0) && str8_match(uishell_sidebar_local_field(section, str8_lit("id")), id, 0))
    { return section; }
  }
  return &cfg_nil_node;
}

internal B32
uishell_sidebar_local_is_default(CFG_Node *group)
{
  return cfg_node_child_from_string(group, str8_lit("default")) != &cfg_nil_node;
}

internal U64
uishell_sidebar_local_group_count(CFG_Node *section)
{
  U64 count = 0;
  for(CFG_Node *g = section->first; g != &cfg_nil_node; g = g->next) { count += str8_match(g->string, str8_lit("group"), 0); }
  return count;
}

// A section's only group, or nil when it has none or several.
internal CFG_Node *
uishell_sidebar_local_only_group(CFG_Node *section)
{
  if(uishell_sidebar_local_group_count(section) != 1) { return &cfg_nil_node; }
  return cfg_node_child_from_string(section, str8_lit("group"));
}

// Whether the section holds the default group, which can't be deleted.
internal B32
uishell_sidebar_local_section_is_default(CFG_Node *section)
{
  for(CFG_Node *g = section->first; g != &cfg_nil_node; g = g->next)
  { if(str8_match(g->string, str8_lit("group"), 0) && uishell_sidebar_local_is_default(g)) { return 1; } }
  return 0;
}

// A section's title: the name you gave it; else its only group's name; else
// the name it showed when a second group joined (its placeholder).
internal String8
uishell_sidebar_local_title(CFG_Node *section)
{
  String8 label = uishell_sidebar_local_field(section, str8_lit("label"));
  if(label.size) { return label; }
  CFG_Node *only = uishell_sidebar_local_only_group(section);
  if(only != &cfg_nil_node) { return uishell_sidebar_local_field(only, str8_lit("label")); }
  String8 placeholder = uishell_sidebar_local_field(section, str8_lit("placeholder"));
  return placeholder.size ? placeholder : str8_lit("Section");
}

// Sections saved before titles were borrowed named themselves after their
// only group; they borrow it now, so renaming either renames both.
internal void
uishell_sidebar_local_borrow_titles(CFG_Node *root)
{
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    CFG_Node *only = uishell_sidebar_local_only_group(section);
    CFG_Node *label = cfg_node_child_from_string(section, str8_lit("label"));
    if(only != &cfg_nil_node && label != &cfg_nil_node &&
       str8_match(label->first->string, uishell_sidebar_local_field(only, str8_lit("label")), 0))
    { cfg_node_release(rd_state->cfg, label); }
  }
}

// Renames a section or a group. A section without a name of its own showing
// one group renames that group; an empty name makes a section borrow again.
internal void
uishell_sidebar_local_rename(CFG_Node *node, String8 label)
{
  label = str8_skip_chop_whitespace(label);
  if(str8_match(node->string, str8_lit("section"), 0))
  {
    CFG_Node *only = uishell_sidebar_local_only_group(node);
    B32 borrowed = !uishell_sidebar_local_field(node, str8_lit("label")).size;
    if(borrowed && only != &cfg_nil_node) { node = only; }
    else if(!label.size)
    {
      cfg_node_release(rd_state->cfg, cfg_node_child_from_string(node, str8_lit("label")));
      return;
    }
  }
  if(label.size) { uishell_sidebar_local_set_field(node, str8_lit("label"), label); }
}

// Adds a group at the end of `section`. When it's the second, a section
// without a name of its own keeps the one it was borrowing.
internal CFG_Node *
uishell_sidebar_local_add_group(CFG_Node *section, String8 label)
{
  CFG_Node *only = uishell_sidebar_local_only_group(section);
  if(only != &cfg_nil_node && !uishell_sidebar_local_field(section, str8_lit("label")).size)
  { uishell_sidebar_local_set_field(section, str8_lit("placeholder"), uishell_sidebar_local_field(only, str8_lit("label"))); }
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *group = cfg_node_new(rd_state->cfg, section, str8_lit("group"));
  uishell_sidebar_local_set_field(group, str8_lit("id"), string_from_guid(scratch.arena, make_guid()));
  uishell_sidebar_local_set_field(group, str8_lit("label"), label);
  scratch_end(scratch);
  return group;
}

// Visits the window's workspaces, open or detached, living in `group`.
#define UIShell_LocalHomedEach(window, group, w) \
  for(CFG_Node *w = (window)->first; w != &cfg_nil_node; w = w->next) \
    if((str8_match(w->string, str8_lit("workspace"), 0) || str8_match(w->string, str8_lit("detached_workspace"), 0)) && \
       str8_match(uishell_sidebar_local_field(w, str8_lit("lives_in")), uishell_sidebar_local_field((group), str8_lit("id")), 0))

// How many workspaces deleting `node` (a section or group) would move to
// the default group.
internal U64
uishell_sidebar_local_homed_count(CFG_Node *window, CFG_Node *node)
{
  U64 count = 0;
  if(str8_match(node->string, str8_lit("group"), 0)) { UIShell_LocalHomedEach(window, node, w) { count++; } }
  else for(CFG_Node *g = node->first; g != &cfg_nil_node; g = g->next)
  { if(str8_match(g->string, str8_lit("group"), 0)) { UIShell_LocalHomedEach(window, g, w) { count++; } } }
  return count;
}

// Closes every View showing a local section, selecting a neighbour where
// one was selected, as close_tab does.
internal void
uishell_sidebar_local_close_views(CFG_Node *window, CFG_Node *section)
{
  Temp scratch = scratch_begin(0, 0);
  String8 key = uishell_sidebar_local_key(scratch.arena, uishell_sidebar_local_field(section, str8_lit("id")));
  for(CFG_Node *view = uishell_sidebar_region_view(window, key); view != &cfg_nil_node; view = uishell_sidebar_region_view(window, key))
  {
    if(cfg_node_child_from_string(view, str8_lit("selected")) != &cfg_nil_node)
    {
      CFG_Node *next = view->next != &cfg_nil_node ? view->next : view->prev;
      if(next != &cfg_nil_node) { cfg_node_child_from_string_or_alloc(rd_state->cfg, next, str8_lit("selected")); }
    }
    cfg_node_release(rd_state->cfg, view);
  }
  scratch_end(scratch);
}

// Deletes a group: its workspaces move to the default group and its ghosts
// go with it. A section left without groups goes too. The default group
// can't be deleted.
internal void
uishell_sidebar_local_delete_group(CFG_Node *window, CFG_Node *group)
{
  if(group == &cfg_nil_node || uishell_sidebar_local_is_default(group)) { return; }
  UIShell_LocalHomedEach(window, group, w) { cfg_node_release(rd_state->cfg, cfg_node_child_from_string(w, str8_lit("lives_in"))); }
  CFG_Node *section = group->parent;
  cfg_node_release(rd_state->cfg, group);
  if(uishell_sidebar_local_group_count(section) == 0)
  {
    uishell_sidebar_local_close_views(window, section);
    cfg_node_release(rd_state->cfg, section);
  }
}

// Deletes a section and its groups (uishell_sidebar_local_delete_group),
// unless it holds the default group.
internal void
uishell_sidebar_local_delete_section(CFG_Node *window, CFG_Node *section)
{
  if(section == &cfg_nil_node || uishell_sidebar_local_section_is_default(section)) { return; }
  // Deleting the last group deletes the section too, fields and all.
  for(U64 left = uishell_sidebar_local_group_count(section); left > 0; left--)
  { uishell_sidebar_local_delete_group(window, cfg_node_child_from_string(section, str8_lit("group"))); }
}

// Opens a new workspace living in `group`.
internal void
uishell_sidebar_local_new_workspace(CFG_Node *window, CFG_Node *group)
{
  CFG_Node *workspace = uishell_new_workspace(window);
  if(!uishell_sidebar_local_is_default(group))
  { uishell_sidebar_local_set_field(workspace, str8_lit("lives_in"), uishell_sidebar_local_field(group, str8_lit("id"))); }
}

//- Menus. A local section's header and a group's header open these; a
// section showing one group carries that group's actions too.

internal void
uishell_sidebar_local_begin_rename(UIShell_SidebarState *state, CFG_Node *node, String8 current, UI_Key menu, UI_Key anchor)
{
  state->rename_node = node->id;
  state->rename_size = Min(current.size, sizeof(state->rename_text));
  MemoryCopy(state->rename_text, current.str, state->rename_size);
  state->rename_cursor = txt_pt(1, state->rename_size+1);
  state->rename_mark = txt_pt(1, 1);
  state->rename_focus = 1;
  ui_ctx_menu_open(menu, anchor, v2f32(0, ui_top_font_size()*1.8f));
}

// The rename popup at `menu`: a field holding the name, applied on Enter.
internal void
uishell_sidebar_local_rename_menu(UIShell_SidebarState *state, UI_Key menu)
{
  UI_CtxMenu(menu) UI_PrefWidth(ui_em(18.f, 1)) UI_PrefHeight(ui_em(1.8f, 1))
  {
    CFG_Node *node = cfg_node_from_id(state->rename_node);
    if(node == &cfg_nil_node) { ui_ctx_menu_close(); }
    UI_Key field = ui_key_from_string(ui_active_seed_key(), str8_lit("###local_rename"));
    if(state->rename_focus) { ui_set_auto_focus_active_key(field); state->rename_focus = 0; }
    UI_Signal sig = ui_line_edit(&state->rename_cursor, &state->rename_mark, state->rename_text, sizeof(state->rename_text),
                                 &state->rename_size, str8(state->rename_text, state->rename_size), str8_lit("###local_rename"));
    if(ui_committed(sig) && node != &cfg_nil_node)
    {
      uishell_sidebar_local_rename(node, str8(state->rename_text, state->rename_size));
      state->rename_node = 0;
      ui_ctx_menu_close();
    }
  }
}

// Delete, asking first when workspaces would move to the default group.
internal void
uishell_sidebar_local_delete_button(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *node)
{
  B32 section = str8_match(node->string, str8_lit("section"), 0);
  U64 moving = uishell_sidebar_local_homed_count(window, node);
  if(state->confirm_delete == node->id && moving)
  {
    String8 confirm = push_str8f(ui_build_arena(), "Move %I64u workspace%s to Workspaces and delete", moving, moving == 1 ? "" : "s");
    if(ui_clicked(ui_button(confirm)))
    {
      if(section) { uishell_sidebar_local_delete_section(window, node); }
      else { uishell_sidebar_local_delete_group(window, node); }
      state->confirm_delete = 0;
      ui_ctx_menu_close();
    }
    if(ui_clicked(ui_button(str8_lit("Cancel")))) { state->confirm_delete = 0; ui_ctx_menu_close(); }
    return;
  }
  if(ui_clicked(ui_button(section ? str8_lit("Delete section") : str8_lit("Delete group"))))
  {
    if(moving) { state->confirm_delete = node->id; }
    else
    {
      if(section) { uishell_sidebar_local_delete_section(window, node); }
      else { uishell_sidebar_local_delete_group(window, node); }
      ui_ctx_menu_close();
    }
  }
}

// A group's own actions: rename, new workspace, reset order, delete.
// `loop` is its items' loop key, for Reset order when it has been reordered.
internal void
uishell_sidebar_local_group_items(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *group, String8 loop,
                                  UI_Key rename_menu, UI_Key anchor, B32 rename)
{
  if(rename && ui_clicked(ui_button(str8_lit("Rename…"))))
  { uishell_sidebar_local_begin_rename(state, group, uishell_sidebar_local_field(group, str8_lit("label")), rename_menu, anchor); }
  if(ui_clicked(ui_button(str8_lit("New workspace here"))))
  { uishell_sidebar_local_new_workspace(window, group); ui_ctx_menu_close(); }
  if(loop.size && uishell_sidebar_order_saved(window, loop)) { uishell_sidebar_order_reset_button(state, loop); }
  if(!uishell_sidebar_local_is_default(group) && uishell_sidebar_local_group_count(group->parent) > 1)
  { uishell_sidebar_local_delete_button(state, window, group); }
}

// A local section's menu, at `menu`; `view` is the View showing it, for Hide.
internal void
uishell_sidebar_local_section_menu(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *section, CFG_Node *view,
                                   String8 loop, UI_Key menu, UI_Key rename_menu, UI_Key anchor)
{
  UI_CtxMenu(menu) UI_PrefWidth(ui_em(18.f, 1)) UI_PrefHeight(ui_em(1.8f, 1))
  {
    if(section == &cfg_nil_node) { ui_ctx_menu_close(); }
    else
    {
      if(ui_clicked(ui_button(str8_lit("Rename…"))))
      { uishell_sidebar_local_begin_rename(state, section, uishell_sidebar_local_title(section), rename_menu, anchor); }
      if(ui_clicked(ui_button(str8_lit("New group"))))
      {
        CFG_Node *group = uishell_sidebar_local_add_group(section, str8_lit("New group"));
        uishell_sidebar_local_begin_rename(state, group, uishell_sidebar_local_field(group, str8_lit("label")), rename_menu, anchor);
      }
      CFG_Node *only = uishell_sidebar_local_only_group(section);
      if(only != &cfg_nil_node) { uishell_sidebar_local_group_items(state, window, only, loop, rename_menu, anchor, 0); }
      if(view != &cfg_nil_node && rd_dock_can_close(view) && ui_clicked(ui_button(str8_lit("Hide"))))
      { UIShell_RegsScope(.tab = view->id, .view = view->id) { uishell_cmd("close_tab"); } ui_ctx_menu_close(); }
      if(!uishell_sidebar_local_section_is_default(section)) { uishell_sidebar_local_delete_button(state, window, section); }
    }
  }
  uishell_sidebar_local_rename_menu(state, rename_menu);
}

// A group header's menu, at `menu`.
internal void
uishell_sidebar_local_group_menu(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *group, String8 loop,
                                 UI_Key menu, UI_Key rename_menu, UI_Key anchor)
{
  UI_CtxMenu(menu) UI_PrefWidth(ui_em(18.f, 1)) UI_PrefHeight(ui_em(1.8f, 1))
  {
    if(group == &cfg_nil_node) { ui_ctx_menu_close(); }
    else { uishell_sidebar_local_group_items(state, window, group, loop, rename_menu, anchor, 1); }
  }
  uishell_sidebar_local_rename_menu(state, rename_menu);
}
