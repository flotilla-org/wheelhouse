//- Managing local sections and groups (drag-model.md, "Closing and deleting"
// and "Titles"). These read the core's Dashboard (uishell_sidebar_locals)
// and change its local entities at once (wheelhouse_local_set/remove).

typedef enum UIShell_MakeKind
{
  UIShell_Make_Workspace = 1,       // in a local group (target), or the default group
  UIShell_Make_ProjectWorkspace,    // living with a project
  UIShell_Make_Group,               // in a section (target)
}
UIShell_MakeKind;

typedef struct UIShell_MakeAction UIShell_MakeAction;
struct UIShell_MakeAction
{
  UIShell_MakeKind kind;
  String8 label;
  String8 target;                   // a local group's or section's id
  String8 project;
};

internal void uishell_sidebar_order_reset_button(UIShell_SidebarState *state, String8 loop);
internal CFG_Node *uishell_new_workspace(CFG_Node *window);

// A section's only group, or 0 when it has none or several.
internal UIShell_LocalGroup *
uishell_sidebar_local_only_group(UIShell_LocalSection *section)
{
  return section && section->group_count == 1 ? &section->groups[0] : 0;
}

// Whether the section holds the default group, which can't be deleted.
internal B32
uishell_sidebar_local_section_is_default(UIShell_LocalSection *section)
{
  for(U64 g = 0; section && g < section->group_count; g++) { if(section->groups[g].is_default) { return 1; } }
  return 0;
}

// Renames a group; an empty name changes nothing. A section showing its
// groups' names shows the new one.
internal void
uishell_sidebar_local_rename_group(CFG_Node *window, String8 id, String8 label)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_Locals *before = uishell_sidebar_locals(state);
  UIShell_LocalGroup *group = uishell_sidebar_local_group(window, id);
  label = str8_skip_chop_whitespace(label);
  if(!group || !label.size) { return; }
  uishell_sidebar_local_put_group(state, group->id, label, group->position, group->section->id, group->is_default);
  uishell_sidebar_local_settle(state, before);
}

// Renames a section. One without a name of its own showing one group
// renames that group; an empty name makes a section show its groups' names
// again.
internal void
uishell_sidebar_local_rename_section(CFG_Node *window, String8 id, String8 label)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_LocalSection *section = uishell_sidebar_local_section(window, id);
  label = str8_skip_chop_whitespace(label);
  if(!section) { return; }
  UIShell_LocalGroup *only = uishell_sidebar_local_only_group(section);
  if(!section->named && only) { uishell_sidebar_local_rename_group(window, only->id, label); return; }
  Temp scratch = scratch_begin(0, 0);
  // Its groups' names, when it is given none: no name of its own.
  if(!label.size) { label = uishell_sidebar_local_group_names(scratch.arena, section); }
  uishell_sidebar_local_put_section(state, section->id, label, section->position);
  scratch_end(scratch);
}

// Adds a group named `label` at the end of the section with id `section`.
internal UIShell_LocalGroup *
uishell_sidebar_local_add_group(CFG_Node *window, String8 section_id, String8 label)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_Locals *before = uishell_sidebar_locals(state);
  UIShell_LocalSection *section = uishell_sidebar_local_section(window, section_id);
  if(!section) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  String8 id = string_from_guid(scratch.arena, make_guid());
  U64 position = section->group_count ? section->groups[section->group_count-1].position+1 : 0;
  uishell_sidebar_local_put_group(state, id, label, position, section->id, 0);
  uishell_sidebar_local_settle(state, before);
  UIShell_LocalGroup *group = uishell_sidebar_local_group(window, id);
  scratch_end(scratch);
  return group;
}

// Visits the window's workspaces, open or kept, living in group `id`, as
// the core has their homes.
#define UIShell_LocalHomedEach(window, id, w) \
  for(CFG_Node *w = (window)->first; w != &cfg_nil_node; w = w->next) \
    if((str8_match(w->string, str8_lit("workspace"), 0) || str8_match(w->string, str8_lit("detached_workspace"), 0)) && \
       str8_match(uishell_sidebar_local_lives_in(w), (id), 0))

// How many workspaces deleting `group`, or every group of `section`, would
// move to the default group.
internal U64
uishell_sidebar_local_homed_count(CFG_Node *window, UIShell_LocalSection *section, UIShell_LocalGroup *group)
{
  U64 count = 0;
  if(group) { UIShell_LocalHomedEach(window, group->id, w) { count++; } }
  for(U64 g = 0; section && g < section->group_count; g++) { UIShell_LocalHomedEach(window, section->groups[g].id, w) { count++; } }
  return count;
}

// Closes every View showing local section `id`, selecting a neighbour where
// one was selected, as close_tab does.
internal void
uishell_sidebar_local_close_views(CFG_Node *window, String8 id)
{
  Temp scratch = scratch_begin(0, 0);
  String8 key = uishell_sidebar_local_key(scratch.arena, id);
  UIShell_SidebarDocks docks = uishell_sidebar_docks(scratch.arena, window);
  for(UIShell_SidebarDockView *v = docks.first; v != 0; v = v->next)
  {
    if(!str8_match(v->node->string, str8_lit("sidebar_section"), 0) ||
       !str8_match(uishell_sidebar_section_key(v->node), key, 0)) { continue; }
    RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(v->arrangement, v->panel);
    RD_ArrangementTab *tab = rd_arrangement_tab_from_view(panel, v->view);
    RD_ArrangementTab *neighbour = tab == 0 ? 0 : tab->next ? tab->next : tab->prev;
    RD_ViewAddress next = neighbour ? neighbour->view : rd_nil_view_address;
    B32 selected = tab != 0 && rd_view_address_match(panel->selected, tab->view);
    uishell_sidebar_docks_remove(&docks, v);
    if(selected && neighbour) { rd_arrangement_select(v->arrangement, panel->id, next); }
  }
  uishell_sidebar_docks_set(&docks);
  scratch_end(scratch);
}

// Removes a group and its pins, and its section with its Views when that
// is left without groups. Workspaces living in it are left to the caller.
internal void
uishell_sidebar_local_drop_group(CFG_Node *window, UIShell_LocalGroup *group)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  for(U64 p = 0; p < group->pin_count; p++) { uishell_sidebar_local_remove(state, str8_lit(".ref"), group->pins[p].ghost); }
  uishell_sidebar_local_remove(state, str8_lit(".group"), group->id);
  UIShell_LocalSection *section = uishell_sidebar_local_section(window, group->section->id);
  if(section && section->group_count == 0)
  {
    uishell_sidebar_local_close_views(window, section->id);
    uishell_sidebar_local_remove(state, str8_lit(".section"), section->id);
  }
}

// Removes local section `id` with its groups and their pins, and nothing
// else: its Views and the workspaces living in it are left as they are (what
// diagnostics clean up with).
internal void
uishell_sidebar_local_forget_section(CFG_Node *window, String8 id)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_Locals *before = uishell_sidebar_locals(state);
  UIShell_LocalSection *section = uishell_sidebar_local_section(window, id);
  if(!section) { return; }
  for(U64 g = 0; g < section->group_count; g++)
  {
    for(U64 p = 0; p < section->groups[g].pin_count; p++) { uishell_sidebar_local_remove(state, str8_lit(".ref"), section->groups[g].pins[p].ghost); }
    uishell_sidebar_local_remove(state, str8_lit(".group"), section->groups[g].id);
  }
  uishell_sidebar_local_remove(state, str8_lit(".section"), section->id);
  uishell_sidebar_local_settle(state, before);
}

// Deletes a group: its workspaces move to the default group (the core
// moves them as it removes the group) and its ghosts go with it. A section
// left without groups goes too. The default group can't be deleted.
internal void
uishell_sidebar_local_delete_group(CFG_Node *window, String8 id)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_Locals *before = uishell_sidebar_locals(state);
  UIShell_LocalGroup *group = uishell_sidebar_local_group(window, id);
  if(!group || group->is_default) { return; }
  uishell_sidebar_local_drop_group(window, group);
  uishell_sidebar_local_settle(state, before);
}

// Deletes a section and its groups (uishell_sidebar_local_delete_group),
// unless it holds the default group.
internal void
uishell_sidebar_local_delete_section(CFG_Node *window, String8 id)
{
  UIShell_LocalSection *section = uishell_sidebar_local_section(window, id);
  if(!section || uishell_sidebar_local_section_is_default(section)) { return; }
  // Deleting the last group deletes the section too.
  for(U64 g = 0; g < section->group_count; g++) { uishell_sidebar_local_delete_group(window, section->groups[g].id); }
}

// Moves the groups `ids` names into section `section_id`, in order, at
// `index` among its other groups (max_U64: at the end), with their
// workspaces and ghosts. The default group stays in the Workspaces section,
// which hosts New workspace; the section itself moves by docking. A section
// they leave without groups goes, with its Views.
internal void
uishell_sidebar_local_place_groups(CFG_Node *window, String8 *ids, U64 count, String8 section_id, U64 index)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_Locals *before = uishell_sidebar_locals(state);
  UIShell_LocalSection *section = uishell_sidebar_local_section(window, section_id);
  if(!section) { return; }
  Temp scratch = scratch_begin(0, 0);
  String8 *moving = push_array(scratch.arena, String8, count);
  U64 moving_count = 0;
  for(U64 i = 0; i < count; i++)
  {
    UIShell_LocalGroup *group = uishell_sidebar_local_group(window, ids[i]);
    if(group && !group->is_default) { moving[moving_count++] = group->id; }
  }
  // The section's order: its other groups, the moving ones at `index`.
  String8 *order = push_array(scratch.arena, String8, section->group_count+moving_count);
  U64 order_count = 0, at = 0;
  for(U64 g = 0; g < section->group_count; g++)
  {
    B32 carried = 0;
    for(U64 m = 0; m < moving_count; m++) { carried |= str8_match(moving[m], section->groups[g].id, 0); }
    if(!carried) { order[order_count++] = section->groups[g].id; }
  }
  at = Min(index, order_count);
  for(U64 k = order_count; k > at; k--) { order[k+moving_count-1] = order[k-1]; }
  for(U64 m = 0; m < moving_count; m++) { order[at+m] = moving[m]; }
  order_count += moving_count;
  uishell_sidebar_local_order_groups(state, section->id, order, order_count);
  // Sections left without groups go.
  UIShell_Locals *after = uishell_sidebar_locals(state);
  for(U64 s = 0; s < before->section_count; s++)
  {
    String8 id = before->sections[s].id;
    B32 left = 0, empty = 0;
    for(U64 g = 0; g < before->sections[s].group_count; g++)
    for(U64 m = 0; m < moving_count; m++) { left |= str8_match(before->sections[s].groups[g].id, moving[m], 0); }
    for(U64 k = 0; k < after->section_count; k++)
    { if(str8_match(after->sections[k].id, id, 0)) { empty = after->sections[k].group_count == 0; } }
    if(left && empty && !str8_match(id, section->id, 0))
    {
      uishell_sidebar_local_close_views(window, id);
      uishell_sidebar_local_remove(state, str8_lit(".section"), id);
    }
  }
  uishell_sidebar_local_settle(state, before);
  scratch_end(scratch);
}

// The index of group `id` among its section's groups, max_U64 for none.
internal U64
uishell_sidebar_local_group_index(CFG_Node *window, String8 id)
{
  UIShell_LocalGroup *group = uishell_sidebar_local_group(window, id);
  return group ? (U64)(group - group->section->groups) : max_U64;
}

// Moves `group` into `section`, after group `after` (empty: at the end).
internal void
uishell_sidebar_local_move_group(CFG_Node *window, String8 group, String8 section, String8 after)
{
  U64 index = after.size ? uishell_sidebar_local_group_index(window, after) : max_U64;
  if(index != max_U64)
  {
    // Counted among the section's groups without the one moving.
    UIShell_LocalGroup *moving = uishell_sidebar_local_group(window, group);
    U64 own = moving && str8_match(moving->section->id, section, 0) ? uishell_sidebar_local_group_index(window, group) : max_U64;
    index = own < index ? index : index+1;
    if(str8_match(group, after, 0)) { return; }
  }
  uishell_sidebar_local_place_groups(window, &group, 1, section, index);
}

// Moves the groups a drag carries (all of section `from_section`'s, or
// group `group`) into `section`, in order, after group `after` (empty:
// before its first group) (#282). A section left without groups goes with
// its Views.
internal void
uishell_sidebar_local_drop_groups(CFG_Node *window, String8 from_section, String8 group, String8 section, String8 after)
{
  Temp scratch = scratch_begin(0, 0);
  String8List moving = {0};
  UIShell_LocalSection *from = uishell_sidebar_local_section(window, from_section);
  for(U64 g = 0; from && g < from->group_count; g++) { str8_list_push(scratch.arena, &moving, from->groups[g].id); }
  if(!from && group.size) { str8_list_push(scratch.arena, &moving, group); }
  String8Array ids = str8_array_from_list(scratch.arena, &moving);
  U64 index = after.size ? uishell_sidebar_local_group_index(window, after)+1 : 0;
  uishell_sidebar_local_place_groups(window, ids.v, ids.count, section, index);
  scratch_end(scratch);
}

// While one View of a local section is dragged (by its title, or as one of
// several tabs), the section it shows: its groups can join another section's
// list of groups, between groups, instead of docking (#282). Not a whole
// section of several tabs dragged by its grip, which moves its Views, nor
// one holding the default group, which stays put.
internal UIShell_LocalSection *
uishell_sidebar_section_drag_section(CFG_Node *window)
{
  if(!rd_drag_is_active() || rd_state->drag_drop_regs_slot != UIShell_ContextRegSlot_View ||
     rd_state->drag_drop_regs->window != window->id || rd_state->drag_drop_commit == uishell_sidebar_section_drop_commit) { return 0; }
  UIShell_LocalGroup *group = uishell_sidebar_local_view_group(window, rd_cfg_from_view(rd_state->drag_drop_regs->view));
  if(!group || uishell_sidebar_local_section_is_default(group->section)) { return 0; }
  return group->section;
}

// The same, when the section shows one group: that group.
internal UIShell_LocalGroup *
uishell_sidebar_section_drag_group(CFG_Node *window)
{
  return uishell_sidebar_local_only_group(uishell_sidebar_section_drag_section(window));
}

// Opens a new workspace living in `group` (the core gives one in the
// default group no group of its own).
internal CFG_Node *
uishell_sidebar_local_new_workspace(CFG_Node *window, UIShell_LocalGroup *group)
{
  return uishell_new_workspace_in(window, str8_zero(), group->id, str8_zero());
}

//- Renaming in place. Double-clicking a title, or Rename… in its menu,
// swaps it for a field holding the name, all selected.

// `handle` is a section's or group's (uishell_sidebar_section_handle,
// uishell_sidebar_group_handle).
internal B32
uishell_sidebar_local_renaming(UIShell_SidebarState *state, U64 handle)
{
  return handle != 0 && state->rename_node == handle;
}

internal void
uishell_sidebar_local_begin_rename(UIShell_SidebarState *state, U64 handle, String8 current)
{
  state->rename_node = handle;
  state->rename_size = Min(current.size, sizeof(state->rename_text));
  MemoryCopy(state->rename_text, current.str, state->rename_size);
  state->rename_cursor = txt_pt(1, state->rename_size+1);
  state->rename_mark = txt_pt(1, 1);
  state->rename_focus = 1;
  ui_ctx_menu_close();
  rd_request_frame();
}

// A name field (keyed `key` under the current parent) over the shared edit
// buffer (rename_text). It owns the keyboard while it's shown: Enter applies,
// Esc cancels, and a press outside it applies, so it needs no focus tree.
// *outcome is 1 on apply, 2 on cancel, else 0.
internal UI_Signal
uishell_sidebar_name_field(UIShell_SidebarState *state, String8 key, U32 *outcome)
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
  ui_take_text_field_focus();
  if(!apply && !cancel)
  {
    ui_consume_text_edit_events(field_key, state->rename_text, sizeof(state->rename_text), &state->rename_size,
                                &state->rename_cursor, &state->rename_mark, 0);
  }
  // It shows focused (its cursor) whichever panel has focus.
  UI_Box *box;
  ui_push_focus_active(UI_FocusKind_Root);
  ui_push_focus_active(UI_FocusKind_On);
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
  ui_pop_focus_active();
  ui_pop_focus_active();
  *outcome = apply ? 1 : cancel ? 2 : 0;
  if(apply || cancel) { rd_request_frame(); }
  return ui_signal_from_box(box);
}

// The field, built where the title would be; its signal.
internal UI_Signal
uishell_sidebar_local_rename_field(UIShell_SidebarState *state, String8 key)
{
  U32 outcome = 0;
  UI_Signal sig = uishell_sidebar_name_field(state, key, &outcome);
  if(outcome == 2) { state->rename_node = 0; }
  if(outcome == 1)
  {
    // The section or group being renamed, by its handle.
    CFG_Node *window = cfg_node_from_id(state->window ? state->window : uishell_regs()->window);
    UIShell_Locals *locals = uishell_sidebar_locals(state);
    Temp scratch = scratch_begin(0, 0);
    String8 typed = str8_skip_chop_whitespace(str8(state->rename_text, state->rename_size));
    for(U64 s = 0; s < locals->section_count; s++)
    {
      UIShell_LocalSection *section = &locals->sections[s];
      // An unchanged title is no name of the section's own.
      if(uishell_sidebar_section_handle(section) == state->rename_node &&
         !(!section->named && str8_match(typed, uishell_sidebar_local_title(scratch.arena, section), 0)))
      { uishell_sidebar_local_rename_section(window, section->id, typed); break; }
      for(U64 g = 0; g < section->group_count; g++)
      {
        if(uishell_sidebar_group_handle(&section->groups[g]) == state->rename_node)
        { uishell_sidebar_local_rename_group(window, section->groups[g].id, typed); }
      }
    }
    scratch_end(scratch);
    state->rename_node = 0;
  }
  return sig;
}

//- Making workspaces and groups (sidebar-headers.md): a footer that opens
// at a group's last row and pushes what's below down. It offers New
// workspace, and New group at a section's last group; choosing one makes
// the row a name field.

// Makes what `action` offers, named `name` (empty: the default name).
internal void
uishell_sidebar_make(CFG_Node *window, UIShell_MakeAction action, String8 name)
{
  if(action.kind == UIShell_Make_Group)
  {
    uishell_sidebar_local_add_group(window, action.target, name.size ? name : str8_lit("New group"));
    return;
  }
  if(action.kind == UIShell_Make_Workspace)
  {
    UIShell_LocalGroup *group = uishell_sidebar_local_group(window, action.target);
    uishell_new_workspace_in(window, name, group ? group->id : str8_zero(), str8_zero());
  }
  if(action.kind == UIShell_Make_ProjectWorkspace) { uishell_new_workspace_in(window, name, str8_zero(), action.project); }
}

// The footer keyed `key`, open while `engaged` or naming; returns the height
// it takes this frame (it animates open and closed). `inset` lines its
// text up with the rows above it.
internal F32
uishell_sidebar_make_footer(UIShell_SidebarState *state, CFG_Node *window, String8 key, UIShell_MakeAction *actions, U64 count,
                            B32 engaged, F32 row_height, F32 inset)
{
  B32 naming = state->make_key && state->make_key == u64_hash_from_str8(key);
  if(naming) { state->make_build = ui_state->build_index; }
  // While a press is held (dragging the scroll bar, say), it stays as it
  // was: opening moves the rows under the pointer, which would close it again.
  UI_Box *previous = ui_box_from_key(ui_key_from_stringf(ui_active_seed_key(), "###make_%S", key));
  if(!ui_key_match(ui_active_key(UI_MouseButtonKind_Left), ui_key_zero()))
  { engaged = !ui_box_is_nil(previous) && dim_2f32(previous->rect).y >= 1.f; }
  F32 t = ui_anim(ui_key_from_stringf(ui_key_zero(), "make_footer_%S", key), engaged || naming ? 1.f : 0.f,
                  .rate = rd_state->menu_animation_rate, .epsilon = 0.001f);
  F32 height = floor_f32(row_height*t);
  UI_Box *row = &ui_nil_box;
  UI_PrefHeight(ui_px(height, 1)) UI_PrefWidth(ui_pct(1, 0)) UI_ChildLayoutAxis(Axis2_Y)
  {
    row = ui_build_box_from_stringf(UI_BoxFlag_Clip, "###make_%S", key);
    ui_box_equip_display_string(row, push_str8f(ui_build_arena(), "###make_%S", key));
  }
  if(height < 1.f) { return 0; }
  // Its controls sit 2px in from the row's top, as a row's own box does, so
  // the clip leaves a field's border whole.
  UI_Box *line;
  UI_Parent(row)
  {
    ui_spacer(ui_px(2.f, 1));
    UI_PrefHeight(ui_px(row_height-4.f, 1)) UI_PrefWidth(ui_pct(1, 0)) UI_ChildLayoutAxis(Axis2_X)
    { line = ui_build_box_from_key(0, ui_key_zero()); }
  }
  UI_Parent(line) UI_PrefHeight(ui_px(row_height-4.f, 1))
  {
    ui_spacer(ui_px(inset, 1));
    if(naming)
    {
      U32 outcome = 0;
      UI_PrefWidth(ui_pct(1, 0)) UI_PrefHeight(ui_px(row_height-4.f, 1))
      { uishell_sidebar_name_field(state, push_str8f(ui_build_arena(), "###make_name_%S", key), &outcome); }
      ui_spacer(ui_px(8.f, 1));
      String8 typed = str8_skip_chop_whitespace(str8(state->rename_text, state->rename_size));
      if(outcome == 1 && state->make_index < count) { uishell_sidebar_make(window, actions[state->make_index], typed); }
      if(outcome) { state->make_key = 0; }
    }
    else UI_TagF("weak")
    {
      for(U64 i = 0; i < count; i++)
      {
        String8 label = push_str8f(ui_build_arena(), "+ %S###make_%S_%I64u", actions[i].label, key, i);
        UI_Signal sig;
        UI_PrefWidth(ui_text_dim(10.f, 1)) UI_CornerRadius(3.f) UI_PrefHeight(ui_px(row_height-4.f, 1))
        { sig = uishell_sidebar_button(label); }
        if(ui_clicked(sig))
        {
          state->make_key = u64_hash_from_str8(key);
          state->make_build = ui_state->build_index;
          state->make_index = (U32)i;
          state->rename_size = 0; state->rename_cursor = state->rename_mark = txt_pt(1, 1);
          // One name field at a time: this ends a rename in progress
          // unapplied, as Esc would.
          state->rename_focus = 1; state->rename_node = 0;
          rd_request_frame();
        }
      }
    }
  }
  return height;
}

//- Menus. A local section's title and a group's header open these; a
// section showing one group carries that group's actions too.

// Delete, asking first when workspaces would move to the default group:
// `section`, or `group`.
internal void
uishell_sidebar_local_delete_button(UIShell_SidebarState *state, CFG_Node *window, UIShell_LocalSection *section, UIShell_LocalGroup *group)
{
  U64 handle = section ? uishell_sidebar_section_handle(section) : uishell_sidebar_group_handle(group);
  String8 id = section ? section->id : group->id;
  U64 moving = uishell_sidebar_local_homed_count(window, section, section ? 0 : group);
  if(state->confirm_delete == handle && moving)
  {
    String8 confirm = push_str8f(ui_build_arena(), "Move %I64u workspace%s to Workspaces, delete", moving, moving == 1 ? "" : "s");
    if(ui_clicked(ui_button(confirm)))
    {
      if(section) { uishell_sidebar_local_delete_section(window, id); }
      else { uishell_sidebar_local_delete_group(window, id); }
      state->confirm_delete = 0;
      ui_ctx_menu_close();
    }
    if(ui_clicked(ui_button(str8_lit("Cancel")))) { state->confirm_delete = 0; ui_ctx_menu_close(); }
    return;
  }
  if(ui_clicked(ui_button(section ? str8_lit("Delete section") : str8_lit("Delete group"))))
  {
    if(moving) { state->confirm_delete = handle; }
    else
    {
      if(section) { uishell_sidebar_local_delete_section(window, id); }
      else { uishell_sidebar_local_delete_group(window, id); }
      ui_ctx_menu_close();
    }
  }
}

// A menu's width: its longest item, including a delete of `section` or
// `group` awaiting confirmation.
internal F32
uishell_sidebar_local_menu_width(UIShell_SidebarState *state, CFG_Node *window, UIShell_LocalSection *section, UIShell_LocalGroup *group)
{
  String8 labels[] = {str8_lit("New workspace here"), str8_lit("Delete section"), str8_lit("Reset order"), str8_zero()};
  U64 handle = section ? uishell_sidebar_section_handle(section) : uishell_sidebar_group_handle(group);
  if(handle && state->confirm_delete == handle)
  {
    U64 moving = uishell_sidebar_local_homed_count(window, section, section ? 0 : group);
    labels[3] = push_str8f(ui_build_arena(), "Move %I64u workspace%s to Workspaces, delete", moving, moving == 1 ? "" : "s");
  }
  return uishell_sidebar_menu_width(labels, ArrayCount(labels));
}

// A group's own actions: rename, new workspace, reset order, delete.
// `loop` is its items' loop key, for Reset order when it has been reordered.
internal void
uishell_sidebar_local_group_items(UIShell_SidebarState *state, CFG_Node *window, UIShell_LocalGroup *group, String8 loop, B32 rename)
{
  if(rename && ui_clicked(ui_button(str8_lit("Rename…"))))
  { uishell_sidebar_local_begin_rename(state, uishell_sidebar_group_handle(group), group->label); }
  if(ui_clicked(ui_button(str8_lit("New workspace here"))))
  { uishell_sidebar_local_new_workspace(window, group); ui_ctx_menu_close(); }
  if(loop.size && uishell_sidebar_order_saved(state, loop)) { uishell_sidebar_order_reset_button(state, loop); }
  if(!group->is_default && group->section->group_count > 1)
  { uishell_sidebar_local_delete_button(state, window, 0, group); }
}

// A local section's menu, at `menu`; `view` is the View showing it, for Hide.
internal void
uishell_sidebar_local_section_menu(UIShell_SidebarState *state, CFG_Node *window, UIShell_LocalSection *section, CFG_Node *view,
                                   String8 loop, UI_Key menu)
{
  UIShell_SidebarMenu(menu, uishell_sidebar_local_menu_width(state, window, section, 0))
  {
    if(!section) { ui_ctx_menu_close(); }
    else
    {
      if(ui_clicked(ui_button(str8_lit("Rename…"))))
      {
        Temp scratch = scratch_begin(0, 0);
        uishell_sidebar_local_begin_rename(state, uishell_sidebar_section_handle(section), uishell_sidebar_local_title(scratch.arena, section));
        scratch_end(scratch);
      }
      if(ui_clicked(ui_button(str8_lit("New group"))))
      {
        UIShell_LocalGroup *group = uishell_sidebar_local_add_group(window, section->id, str8_lit("New group"));
        if(group) { uishell_sidebar_local_begin_rename(state, uishell_sidebar_group_handle(group), group->label); }
      }
      UIShell_LocalGroup *only = uishell_sidebar_local_only_group(section);
      if(only) { uishell_sidebar_local_group_items(state, window, only, loop, 0); }
      if(view != &cfg_nil_node && rd_dock_can_close(rd_dock_view_from_address(rd_view_address_from_cfg(view))) && ui_clicked(ui_button(str8_lit("Hide"))))
      { UIShell_RegsScope(.tab = rd_view_address_from_cfg(view), .view = rd_view_address_from_cfg(view)) { uishell_cmd("close_tab"); } ui_ctx_menu_close(); }
      if(!uishell_sidebar_local_section_is_default(section)) { uishell_sidebar_local_delete_button(state, window, section, 0); }
    }
  }
}

// A group header's menu, at `menu`.
internal void
uishell_sidebar_local_group_menu(UIShell_SidebarState *state, CFG_Node *window, UIShell_LocalGroup *group, String8 loop, UI_Key menu)
{
  UIShell_SidebarMenu(menu, uishell_sidebar_local_menu_width(state, window, 0, group))
  {
    if(!group) { ui_ctx_menu_close(); }
    else { uishell_sidebar_local_group_items(state, window, group, loop, 1); }
  }
}
