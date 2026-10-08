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

// A section's title: the name you gave it; else its groups' names, joined.
// One group's name is the title then, and renaming the section renames it.
internal String8
uishell_sidebar_local_title(Arena *arena, CFG_Node *section)
{
  String8 label = uishell_sidebar_local_field(section, str8_lit("label"));
  if(label.size) { return label; }
  String8List names = {0};
  for(CFG_Node *g = section->first; g != &cfg_nil_node; g = g->next)
  { if(str8_match(g->string, str8_lit("group"), 0)) { str8_list_push(arena, &names, uishell_sidebar_local_field(g, str8_lit("label"))); } }
  StringJoin join = {.sep = str8_lit(", ")};
  return names.node_count ? str8_list_join(arena, &names, &join) : str8_lit("Section");
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
// one group renames that group; an empty name makes a section show its
// groups' names again.
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

// Adds a group at the end of `section`.
internal CFG_Node *
uishell_sidebar_local_add_group(CFG_Node *section, String8 label)
{
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

// Moves `group` into `section` after `after` (nil: at the end), with its
// workspaces and ghosts. A section it leaves without groups goes.
internal void
uishell_sidebar_local_move_group(CFG_Node *window, CFG_Node *group, CFG_Node *section, CFG_Node *after)
{
  CFG_Node *from = group->parent;
  if(group == after) { return; }
  cfg_node_unhook(rd_state->cfg, from, group);
  cfg_node_insert_child(rd_state->cfg, section, after != &cfg_nil_node ? after : section->last, group);
  if(from != section && uishell_sidebar_local_group_count(from) == 0)
  {
    uishell_sidebar_local_close_views(window, from);
    cfg_node_release(rd_state->cfg, from);
  }
}

// While a local section showing one group is dragged by its title (its
// View's docking drag), the group it shows: the section is that group, so
// other sections' groups claim the drag as they would the group's header,
// and a claim moves the group there instead of docking (drag_finish).
internal CFG_Node *
uishell_sidebar_section_drag_group(CFG_Node *window)
{
  if(!rd_drag_is_active() || rd_state->drag_drop_regs_slot != UIShell_ContextRegSlot_View ||
     rd_state->drag_drop_regs->window != window->id) { return &cfg_nil_node; }
  CFG_Node *group = uishell_sidebar_local_view_group(window, cfg_node_from_id(rd_state->drag_drop_regs->view));
  return group != &cfg_nil_node && uishell_sidebar_local_group_count(group->parent) == 1 ? group : &cfg_nil_node;
}

// Opens a new workspace living in `group`.
internal void
uishell_sidebar_local_new_workspace(CFG_Node *window, CFG_Node *group)
{
  CFG_Node *workspace = uishell_new_workspace(window);
  if(!uishell_sidebar_local_is_default(group))
  { uishell_sidebar_local_set_field(workspace, str8_lit("lives_in"), uishell_sidebar_local_field(group, str8_lit("id"))); }
}

//- Renaming in place. Double-clicking a title, or Rename… in its menu,
// swaps it for a field holding the name, all selected.

internal B32
uishell_sidebar_local_renaming(UIShell_SidebarState *state, CFG_Node *node)
{
  return node != &cfg_nil_node && state->rename_node == node->id;
}

internal void
uishell_sidebar_local_begin_rename(UIShell_SidebarState *state, CFG_Node *node, String8 current)
{
  state->rename_node = node->id;
  state->rename_size = Min(current.size, sizeof(state->rename_text));
  MemoryCopy(state->rename_text, current.str, state->rename_size);
  state->rename_cursor = txt_pt(1, state->rename_size+1);
  state->rename_mark = txt_pt(1, 1);
  state->rename_focus = 1;
  ui_ctx_menu_close();
  rd_request_frame();
}

// The field, built where the title would be (keyed `key` under the current
// parent); its signal. It owns the keyboard while it's shown: Enter applies,
// Esc cancels, and a press outside it applies, so it needs no focus tree.
internal UI_Signal
uishell_sidebar_local_rename_field(UIShell_SidebarState *state, String8 key)
{
  UI_Key field_key = ui_key_from_string(ui_active_seed_key(), key);
  UI_Box *previous = ui_box_from_key(field_key);
  B32 apply = 0, cancel = 0;
  if(!state->rename_focus)
  {
    cancel = ui_slot_press(UI_EventActionSlot_Cancel);
    apply = !cancel && ui_slot_press(UI_EventActionSlot_Accept);
    for(UI_Event *evt = 0; !apply && !cancel && ui_next_event(&evt);)
    {
      B32 mouse = evt->key == WM_Key_LeftMouseButton || evt->key == WM_Key_RightMouseButton || evt->key == WM_Key_MiddleMouseButton;
      apply = evt->kind == UI_EventKind_Press && mouse && !contains_2f32(previous->rect, evt->pos);
    }
  }
  state->rename_focus = 0;
  if(!apply && !cancel)
  {
    ui_consume_text_edit_events(field_key, state->rename_text, sizeof(state->rename_text), &state->rename_size,
                                &state->rename_cursor, &state->rename_mark, 0);
  }
  UI_Box *box;
  UI_CornerRadius(3.f)
  {
    box = ui_build_box_from_string(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder|UI_BoxFlag_MouseClickable|
                                   UI_BoxFlag_Clip|UI_BoxFlag_AllowOverflowX, key);
  }
  UI_Parent(box) UI_TextPadding(2.f)
  {
    String8 text = str8(state->rename_text, state->rename_size);
    ui_set_next_pref_width(ui_px(fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, ui_top_tab_size(), text).x+ui_top_font_size()*2, 1.f));
    UI_Box *text_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|UI_BoxFlag_DisableTextTrunc, "###rename_text");
    UI_LineEditDrawData *draw = push_array(ui_build_arena(), UI_LineEditDrawData, 1);
    draw->edited_string = push_str8_copy(ui_build_arena(), text);
    draw->cursor = state->rename_cursor;
    draw->mark = state->rename_mark;
    draw->trail = 1;
    ui_box_equip_display_string(text_box, text);
    ui_box_equip_custom_draw(text_box, ui_line_edit_draw, draw);
  }
  UI_Signal sig = ui_signal_from_box(box);
  if(cancel) { state->rename_node = 0; rd_request_frame(); }
  if(apply)
  {
    CFG_Node *node = cfg_node_from_id(state->rename_node);
    // An unchanged title is no name of the section's own.
    Temp scratch = scratch_begin(0, 0);
    String8 typed = str8_skip_chop_whitespace(str8(state->rename_text, state->rename_size));
    B32 unchanged = str8_match(node->string, str8_lit("section"), 0) &&
      !uishell_sidebar_local_field(node, str8_lit("label")).size &&
      str8_match(typed, uishell_sidebar_local_title(scratch.arena, node), 0);
    if(node != &cfg_nil_node && !unchanged) { uishell_sidebar_local_rename(node, typed); }
    scratch_end(scratch);
    state->rename_node = 0;
    rd_request_frame();
  }
  return sig;
}

//- Menus. A local section's title and a group's header open these; a
// section showing one group carries that group's actions too.

// Delete, asking first when workspaces would move to the default group.
internal void
uishell_sidebar_local_delete_button(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *node)
{
  B32 section = str8_match(node->string, str8_lit("section"), 0);
  U64 moving = uishell_sidebar_local_homed_count(window, node);
  if(state->confirm_delete == node->id && moving)
  {
    String8 confirm = push_str8f(ui_build_arena(), "Move %I64u workspace%s to Workspaces, delete", moving, moving == 1 ? "" : "s");
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

// A menu's width: its longest item, including a delete awaiting confirmation.
internal F32
uishell_sidebar_local_menu_width(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *deletable)
{
  String8 labels[] = {str8_lit("New workspace here"), str8_lit("Delete section"), str8_lit("Reset order"), str8_zero()};
  if(deletable != &cfg_nil_node && state->confirm_delete == deletable->id)
  {
    U64 moving = uishell_sidebar_local_homed_count(window, deletable);
    labels[3] = push_str8f(ui_build_arena(), "Move %I64u workspace%s to Workspaces, delete", moving, moving == 1 ? "" : "s");
  }
  return uishell_sidebar_menu_width(labels, ArrayCount(labels));
}

// A group's own actions: rename, new workspace, reset order, delete.
// `loop` is its items' loop key, for Reset order when it has been reordered.
internal void
uishell_sidebar_local_group_items(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *group, String8 loop, B32 rename)
{
  if(rename && ui_clicked(ui_button(str8_lit("Rename…"))))
  { uishell_sidebar_local_begin_rename(state, group, uishell_sidebar_local_field(group, str8_lit("label"))); }
  if(ui_clicked(ui_button(str8_lit("New workspace here"))))
  { uishell_sidebar_local_new_workspace(window, group); ui_ctx_menu_close(); }
  if(loop.size && uishell_sidebar_order_saved(window, loop)) { uishell_sidebar_order_reset_button(state, loop); }
  if(!uishell_sidebar_local_is_default(group) && uishell_sidebar_local_group_count(group->parent) > 1)
  { uishell_sidebar_local_delete_button(state, window, group); }
}

// A local section's menu, at `menu`; `view` is the View showing it, for Hide.
internal void
uishell_sidebar_local_section_menu(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *section, CFG_Node *view,
                                   String8 loop, UI_Key menu)
{
  UIShell_SidebarMenu(menu, uishell_sidebar_local_menu_width(state, window, section))
  {
    if(section == &cfg_nil_node) { ui_ctx_menu_close(); }
    else
    {
      if(ui_clicked(ui_button(str8_lit("Rename…"))))
      {
        Temp scratch = scratch_begin(0, 0);
        uishell_sidebar_local_begin_rename(state, section, uishell_sidebar_local_title(scratch.arena, section));
        scratch_end(scratch);
      }
      if(ui_clicked(ui_button(str8_lit("New group"))))
      {
        CFG_Node *group = uishell_sidebar_local_add_group(section, str8_lit("New group"));
        uishell_sidebar_local_begin_rename(state, group, uishell_sidebar_local_field(group, str8_lit("label")));
      }
      CFG_Node *only = uishell_sidebar_local_only_group(section);
      if(only != &cfg_nil_node) { uishell_sidebar_local_group_items(state, window, only, loop, 0); }
      if(view != &cfg_nil_node && rd_dock_can_close(view) && ui_clicked(ui_button(str8_lit("Hide"))))
      { UIShell_RegsScope(.tab = view->id, .view = view->id) { uishell_cmd("close_tab"); } ui_ctx_menu_close(); }
      if(!uishell_sidebar_local_section_is_default(section)) { uishell_sidebar_local_delete_button(state, window, section); }
    }
  }
}

// A group header's menu, at `menu`.
internal void
uishell_sidebar_local_group_menu(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *group, String8 loop, UI_Key menu)
{
  UIShell_SidebarMenu(menu, uishell_sidebar_local_menu_width(state, window, group))
  {
    if(group == &cfg_nil_node) { ui_ctx_menu_close(); }
    else { uishell_sidebar_local_group_items(state, window, group, loop, 1); }
  }
}
