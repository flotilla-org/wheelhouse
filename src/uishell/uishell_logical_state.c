// The logical state as text (uishell_logical_state.h). It reads the config
// tree, each window's current Andamento snapshot and dashboard record, and
// what its workspace store last read of each Workspace Overlay, and changes
// none of them, so it can run between any two commands.
//
//   subscriptions         the Dashboard's, in order: kind, daemon, stale
//   window <n>
//     workspaces          selector order; subject or local; kept; mood; what
//                         its Workspace Overlay flags; the provider slots it
//                         hides; arrangement, tabs at their View Addresses
//                         with their slots' flags
//     sidebar             persisted display values; rows as Andamento presents
//                         them; the sibling orders it keeps; local sections
//                         and pins (a pin to a View that went away flagged)
//     sidebar arrangement where sections are docked, and which are closed
//   presentation
//     window <n>          visible workspace, focused panel, input host
//
// Things are named by meaning: workspaces by label, rows by entity and label,
// sections by title. Wheelhouse's own entities (.workspace, .section, .group,
// .ref) have generated ids, so their labels stand in for them. An entity
// from a subscription names it by position (provider=<n>, or "removed");
// "local" ones say nothing.

typedef struct UIShell_LogicalText UIShell_LogicalText;
struct UIShell_LogicalText
{
  Arena *arena;
  String8List lines;
};

internal void
uishell_logical_linef(UIShell_LogicalText *t, U64 depth, char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  String8 line = push_str8fv(t->arena, fmt, args);
  va_end(args);
  String8 indent = {push_array(t->arena, U8, depth*2), depth*2};
  MemorySet(indent.str, ' ', indent.size);
  str8_list_pushf(t->arena, &t->lines, "%S%S\n", indent, line);
}

// Quotes `s`, escaping quotes, backslashes and control bytes, so every value
// stays on its line.
internal String8
uishell_logical_quote(Arena *arena, String8 s)
{
  String8List parts = {0};
  str8_list_push(arena, &parts, str8_lit("\""));
  U64 start = 0;
  for(U64 i = 0; i < s.size; i++)
  {
    U8 c = s.str[i];
    if(c != '"' && c != '\\' && c >= 0x20) { continue; }
    str8_list_push(arena, &parts, str8_substr(s, r1u64(start, i)));
    if(c == '"') { str8_list_push(arena, &parts, str8_lit("\\\"")); }
    else if(c == '\\') { str8_list_push(arena, &parts, str8_lit("\\\\")); }
    else if(c == '\n') { str8_list_push(arena, &parts, str8_lit("\\n")); }
    else { str8_list_pushf(arena, &parts, "\\x%02x", (U32)c); }
    start = i+1;
  }
  str8_list_push(arena, &parts, str8_skip(s, start));
  str8_list_push(arena, &parts, str8_lit("\""));
  return str8_list_join(arena, &parts, 0);
}

// " provider=<n>" (and " stale") for a subscription's entity; empty for a
// local one.
internal String8
uishell_logical_provider(Arena *arena, String8 provider, B32 stale)
{
  String8 label = uishell_subscription_label(arena, provider);
  if(str8_match(label, str8_lit("local"), 0)) { return str8_zero(); }
  return push_str8f(arena, " provider=%S%s", label, stale ? " stale" : "");
}

internal String8
uishell_logical_entity(Arena *arena, String8 kind, String8 id)
{
  if(kind.size && kind.str[0] == '.') { return kind; }
  return push_str8f(arena, "%S/%S", kind, id);
}

//- Workspaces

// " view=<owner kind>:<key>" (and " previous"): the View Address `view` is
// at (shell_arrangement.h), without its owner's ID, which the tree it is in
// says. One its address doesn't find again says so (" unaddressed").
internal String8
uishell_logical_address(Arena *arena, CFG_Node *view)
{
  RD_ViewAddress address = rd_view_address_from_cfg(view);
  RD_ViewInstance instance = RD_ViewInstance_Current;
  String8 key = rd_view_key_from_cfg(view, &instance);
  // A section of Wheelhouse's own (".section:<id>") by its kind alone: its
  // label stands in for the generated ID.
  if(key.size && key.str[0] == '.') { key = str8_prefix(key, str8_find_needle(key, 0, str8_lit(":"), 0)); }
  return push_str8f(arena, " view=%S:%S%s%s", rd_view_owner_kind_name(address.owner), uishell_logical_quote(arena, key),
    instance == RD_ViewInstance_Previous ? " previous" : "", rd_cfg_from_view(address) != view ? " unaddressed" : "");
}

// A tab: its View kind, its Slot's key, then what defines its content. A
// terminal is its command and launch directory (and daemon hosting), a
// Jackstay View its source endpoints, a placeholder the content it can't
// show, anything else its expression.
internal String8
uishell_logical_tab(Arena *arena, CFG_Node *tab, B32 selected)
{
  local_persist char *settings[] =
  {
    "cwd", "daemon", "daemon_name",
    "source_endpoint", "source_socket", "input_endpoint", "input_socket",
    "media_endpoint", "media_socket", "d3d11_endpoint", "porthole_endpoint", "content",
  };
  String8List parts = {0};
  str8_list_pushf(arena, &parts, "tab %S", tab->string);
  String8 label = rd_view_label_from_cfg(tab);
  if(label.size) { str8_list_pushf(arena, &parts, " %S", uishell_logical_quote(arena, label)); }
  // Its View Address, whose key is a workspace View's Slot's key in
  // Andamento (uishell_workspace_store.c); before it has one, the
  // provider's slot it was made for. A Slot's previous instance has the
  // Slot's key, at the previous instance.
  str8_list_push(arena, &parts, uishell_logical_address(arena, tab));
  String8 slot = cfg_node_child_from_string(tab, str8_lit("slot"))->first->string;
  if(!slot.size && !cfg_node_child_from_string(tab, str8_lit("previous_of"))->first->string.size)
  { slot = cfg_node_child_from_string(tab, str8_lit("resource_id"))->first->string; }
  String8 expr = rd_expr_from_cfg(tab);
  if(expr.size)
  {
    B32 terminal = str8_match(tab->string, str8_lit("terminal"), 0);
    str8_list_pushf(arena, &parts, " %s=%S", terminal ? "command" : "expression", uishell_logical_quote(arena, expr));
  }
  for(U64 i = 0; i < ArrayCount(settings); i++)
  {
    CFG_Node *setting = cfg_node_child_from_string(tab, str8_cstring(settings[i]));
    if(setting == &cfg_nil_node) { continue; }
    String8 value = setting->first->string;
    if(value.size) { str8_list_pushf(arena, &parts, " %s=%S", settings[i], uishell_logical_quote(arena, value)); }
    else { str8_list_pushf(arena, &parts, " %s", settings[i]); }
  }
  // What its Workspace Overlay says of it (ADR 0013): its content is the
  // user's (detached), the provider's changed under an edit (changed), the
  // provider removed it (removed), or its rebind policy is the user's.
  CFG_Node *owner = &cfg_nil_node;
  UIShell_SidebarState *state = slot.size ? uishell_store_state_from_view(tab, &owner) : 0;
  U32 flags = state ? uishell_store_slot_flags(state, owner, slot) : 0;
  if(flags & ANDAMENTO_SLOT_FLAG_DETACHED) { str8_list_push(arena, &parts, str8_lit(" detached")); }
  if(flags & ANDAMENTO_SLOT_FLAG_CHANGED) { str8_list_push(arena, &parts, str8_lit(" changed")); }
  if(flags & ANDAMENTO_SLOT_FLAG_REMOVED) { str8_list_push(arena, &parts, str8_lit(" removed")); }
  if(flags & ANDAMENTO_SLOT_FLAG_REBIND) { str8_list_push(arena, &parts, str8_lit(" rebind")); }
  if(selected) { str8_list_push(arena, &parts, str8_lit(" selected")); }
  return str8_list_join(arena, &parts, 0);
}

// A sidebar View names the section it shows by title; other Views are tabs.
// A section whose key no longer resolves is flagged (template drift), and
// the leftover section is hidden while it stands aside.
internal String8
uishell_logical_view(Arena *arena, CFG_Node *view, B32 selected)
{
  if(!str8_match(view->string, str8_lit("sidebar_section"), 0)) { return uishell_logical_tab(arena, view, selected); }
  RD_WindowState *ws = rd_window_state_from_cfg__existing(rd_window_from_cfg(view));
  UIShell_SidebarState *state = ws != &rd_nil_window_state ? ws->sidebar : 0;
  B32 gone = state && uishell_sidebar_section_gone(state, uishell_sidebar_section_key(view));
  return push_str8f(arena, "section %S%S%s%s%s%s", uishell_logical_quote(arena, rd_label_from_cfg(view)),
    uishell_logical_address(arena, view), selected ? " selected" : "", uishell_sidebar_section_collapsed(view) ? " collapsed" : "",
    uishell_sidebar_view_hidden(view) ? " hidden" : "", gone ? " flagged" : "");
}

// An arrangement's panels: splits with their axis, each child's weight to
// two places, and leaf panels' tabs in order.
internal void
uishell_logical_panels(UIShell_LogicalText *t, U64 depth, RD_ArrangementPanel *panel, Axis2 axis, B32 root)
{
  String8 head = root ? str8_lit("panel") : push_str8f(t->arena, "panel weight=%.2f", panel->weight);
  if(panel->first != &rd_nil_arrangement_panel)
  {
    uishell_logical_linef(t, depth, "%S split=%s", head, axis == Axis2_X ? "x" : "y");
    for(RD_ArrangementPanel *child = panel->first; child != &rd_nil_arrangement_panel; child = child->next)
    { uishell_logical_panels(t, depth+1, child, axis2_flip(axis), 0); }
    return;
  }
  uishell_logical_linef(t, depth, "%S", head);
  for(RD_ArrangementTab *tab = panel->first_tab; tab; tab = tab->next)
  { uishell_logical_linef(t, depth+1, "%S", uishell_logical_view(t->arena, rd_cfg_from_view(tab->view), rd_view_address_match(tab->view, panel->selected))); }
}

internal void
uishell_logical_arrangement(UIShell_LogicalText *t, U64 depth, CFG_Node *window, CFG_Node *owner)
{
  RD_Arrangement *arrangement = uishell_workspace_mount_from_owner_cfg(t->arena, window, owner).arrangement;
  if(arrangement->root == &rd_nil_arrangement_panel) { uishell_logical_linef(t, depth, "no panels"); return; }
  uishell_logical_panels(t, depth, arrangement->root, arrangement->root_axis, 1);
}

// One workspace: its label, its subject (or local, with where it lives),
// whether it was closed but kept, and its arrangement.
internal void
uishell_logical_workspace(UIShell_LogicalText *t, U64 depth, CFG_Node *window, CFG_Node *owner, String8 label, B32 kept)
{
  Arena *arena = t->arena;
  String8 home = str8_lit(" local");
  if(uishell_workspace_cfg_has_subject(owner))
  {
    home = push_str8f(arena, " subject=%S/%S%S",
      cfg_node_child_from_string(owner, str8_lit("sidebar_entity_kind"))->first->string,
      cfg_node_child_from_string(owner, str8_lit("sidebar_entity_id"))->first->string,
      uishell_logical_provider(arena, uishell_workspace_cfg_subject_provider(owner), 0));
  }
  else if(uishell_sidebar_local_home(owner).size)
  { home = push_str8f(arena, " local with=project/%S", uishell_sidebar_local_home(owner)); }
  else
  {
    UIShell_LocalGroup *group = uishell_sidebar_local_group(window, uishell_sidebar_local_lives_in(owner));
    if(group && !group->is_default)
    { home = push_str8f(arena, " local group=%S", uishell_logical_quote(arena, group->label)); }
  }
  // Its mood, and what its Workspace Overlay says of its arrangement: the
  // provider changed one the user owns, soft overrides (sizes, tabs) apply,
  // or one names a panel that went.
  String8List extra = {0};
  String8 mood = cfg_node_child_from_string(owner, str8_lit("theme"))->first->string;
  if(mood.size) { str8_list_pushf(arena, &extra, " mood=%S", uishell_logical_quote(arena, mood)); }
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  UIShell_SidebarState *state = ws != &rd_nil_window_state ? ws->sidebar : 0;
  U32 flags = state ? uishell_store_arrangement_flags(state, owner) : 0;
  if(flags & ANDAMENTO_ARRANGEMENT_FLAG_PROVIDER_CHANGED) { str8_list_push(arena, &extra, str8_lit(" provider-changed")); }
  if(flags & ANDAMENTO_ARRANGEMENT_FLAG_SOFT) { str8_list_push(arena, &extra, str8_lit(" soft")); }
  if(flags & ANDAMENTO_ARRANGEMENT_FLAG_UNRESOLVED) { str8_list_push(arena, &extra, str8_lit(" unresolved")); }
  uishell_logical_linef(t, depth, "workspace %S%S%s%S", uishell_logical_quote(arena, label), home, kept ? " kept" : "",
    str8_list_join(arena, &extra, 0));
  // Provider slots the user closed, which tombstones hide.
  UIShell_StoreEntry *entry = state ? uishell_store_entry_find(state, uishell_workspace_id_from_cfg(owner)) : 0;
  for(U64 i = 0; entry && i < entry->tombstone_count; i++)
  { uishell_logical_linef(t, depth+1, "hidden slot=%S", uishell_logical_quote(arena, entry->tombstones[i])); }
  uishell_logical_arrangement(t, depth+1, window, owner);
}

// Open workspaces in selector order, with kept ones where they were.
internal void
uishell_logical_workspaces(UIShell_LogicalText *t, U64 depth, CFG_Node *window)
{
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(t->arena, window);
  uishell_logical_linef(t, depth, "workspaces");
  // The window's own legacy layout, when present, is the selector's first.
  for(UIShell_MaterializedWorkspace *w = split.inventory.first; w; w = w->next)
  { if(w->id == window->id) { uishell_logical_workspace(t, depth+1, window, window, w->display_name, 0); } }
  for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next)
  {
    B32 kept = str8_match(c->string, str8_lit("detached_workspace"), 0);
    if(!kept && !str8_match(c->string, str8_lit("workspace"), 0)) { continue; }
    String8 label = rd_label_from_cfg(c);
    for(UIShell_MaterializedWorkspace *w = split.inventory.first; w; w = w->next)
    { if(w->id == c->id) { label = w->display_name; } }
    uishell_logical_workspace(t, depth+1, window, c, label, kept);
  }
}

//- Sidebar

// A section's title, as docking names it: its first field, else its label.
internal String8
uishell_logical_section_title(UIShell_SidebarState *state, AndamentoNode node)
{
  if(node.field_count)
  {
    AndamentoField field = {0};
    andamento_snapshot_field(state->snapshot, node.first_field, &field);
    return uishell_sidebar_string(field.text);
  }
  return uishell_sidebar_string(node.label);
}

// The title of the section keyed `key` (".section:<id>" for local ones).
internal String8
uishell_logical_section_key_title(Arena *arena, UIShell_SidebarState *state, CFG_Node *window, String8 key)
{
  for(U64 i = 0; state && state->snapshot && i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    uishell_sidebar_node_at(state, i, &node);
    if(node.is_section && str8_match(uishell_sidebar_string(node.key), key, 0)) { return uishell_logical_section_title(state, node); }
  }
  UIShell_LocalSection *section = uishell_sidebar_local_section(window, uishell_sidebar_local_key_id(key));
  if(section) { return uishell_sidebar_local_title(arena, section); }
  return str8_lit("(unknown section)");
}

internal void
uishell_logical_display(UIShell_LogicalText *t, U64 depth, UIShell_SidebarState *state)
{
  String8List seen = {0};
  UIShell_DisplayControlIterator it = {state->snapshot};
  AndamentoControl control = {0};
  AndamentoText name = {0};
  while(uishell_sidebar_next_persistent_control(&it, &control, &name))
  {
    String8 variable = uishell_sidebar_string(name);
    if(uishell_sidebar_local_seen(&seen, variable)) { continue; }
    str8_list_push(t->arena, &seen, variable);
    uishell_logical_linef(t, depth, "display %S=%s", variable, control.checked ? "on" : "off");
  }
}

internal void
uishell_logical_rows(UIShell_LogicalText *t, U64 depth, UIShell_SidebarState *state)
{
  Arena *arena = t->arena;
  U64 count = andamento_snapshot_node_count(state->snapshot);
  U64 *depths = push_array(arena, U64, count);
  B32 *hidden = push_array(arena, B32, count);
  for(U64 i = 0; i < count; i++)
  {
    AndamentoNode node = {0};
    UIShell_SidebarRole role = uishell_sidebar_node_at(state, i, &node);
    depths[i] = node.parent == ANDAMENTO_NONE ? 0 : depths[node.parent]+1;
    // The sidebar leaves out the empty leftover section, and what's under a
    // hidden node.
    hidden[i] = (node.parent != ANDAMENTO_NONE && hidden[node.parent]) || uishell_sidebar_leftover_hidden(state, i, node);
    if(hidden[i]) { continue; }
    String8 title = uishell_logical_quote(arena, node.is_section ? uishell_logical_section_title(state, node) : uishell_sidebar_string(node.label));
    if(role == UIShell_SidebarRole_Container || (node.is_section && role != UIShell_SidebarRole_LocalSection))
    { uishell_logical_linef(t, depth+depths[i], "region %S", title); continue; }
    if(role == UIShell_SidebarRole_LocalSection)
    { uishell_logical_linef(t, depth+depths[i], "section %S", title); continue; }
    String8List flags = {0};
    String8 layout = uishell_sidebar_string(node.layout);
    if(layout.size) { str8_list_pushf(arena, &flags, " layout=%S", layout); }
    // Whether the row opens a workspace, is opening one, or shows an open one.
    if(node.state == ANDAMENTO_LATENT && node.openable) { str8_list_push(arena, &flags, str8_lit(" openable")); }
    if(node.state == ANDAMENTO_OPENING) { str8_list_push(arena, &flags, str8_lit(" opening")); }
    if(node.state == ANDAMENTO_LIVE) { str8_list_push(arena, &flags, str8_lit(" live")); }
    String8 status = uishell_sidebar_node_status(state, node);
    if(status.size) { str8_list_pushf(arena, &flags, " status=%S", uishell_logical_quote(arena, status)); }
    if(node.collapsed && uishell_sidebar_has_children(state, i)) { str8_list_push(arena, &flags, str8_lit(" collapsed")); }
    if(role == UIShell_SidebarRole_PassThrough) { str8_list_push(arena, &flags, str8_lit(" untitled")); }
    if(!node.is_section)
    { str8_list_push(arena, &flags, uishell_logical_provider(arena, uishell_sidebar_string(node.entity.provider), node.stale)); }
    uishell_logical_linef(t, depth+depths[i], "row %S %S%S",
      uishell_logical_entity(arena, uishell_sidebar_string(node.entity.kind), uishell_sidebar_string(node.entity.id)),
      title, str8_list_join(arena, &flags, 0));
  }
}

// An entity named in a saved order, labelled as its row is.
internal String8
uishell_logical_order_entity(Arena *arena, UIShell_SidebarState *state, AndamentoEntity entity)
{
  String8 name = uishell_logical_entity(arena, uishell_sidebar_string(entity.kind), uishell_sidebar_string(entity.id));
  for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    andamento_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && uishell_sidebar_card_entity_match(node.entity, entity))
    { return push_str8f(arena, "%S %S", name, uishell_logical_quote(arena, uishell_sidebar_string(node.label))); }
  }
  return push_str8f(arena, "%S (absent)", name);
}

// Each sibling order Andamento keeps, named by the row (or region) its run
// is under.
internal void
uishell_logical_orders(UIShell_LogicalText *t, U64 depth, UIShell_SidebarState *state)
{
  Arena *arena = t->arena;
  AndamentoDashboard *dashboard = uishell_sidebar_dashboard(state);
  for(U64 o = 0; dashboard && o < dashboard->order_count; o++)
  {
    AndamentoSiblingOrder *order = (AndamentoSiblingOrder *)&dashboard->orders[o];
    String8 loop = uishell_sidebar_string(order->loop_key), under = str8_lit("(no rows)");
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}, parent = {0};
      andamento_snapshot_node(state->snapshot, i, &node);
      if(node.is_section || node.parent == ANDAMENTO_NONE || !str8_match(uishell_sidebar_string(node.loop_key), loop, 0)) { continue; }
      uishell_sidebar_node_at(state, node.parent, &parent);
      under = uishell_logical_quote(arena, parent.is_section ? uishell_logical_section_title(state, parent) : uishell_sidebar_string(parent.label));
      break;
    }
    uishell_logical_linef(t, depth, "order under %S", under);
    for(U64 e = 0; e < order->entity_count; e++)
    { uishell_logical_linef(t, depth+1, "%S", uishell_logical_order_entity(arena, state, order->entities[e])); }
  }
}

// Sections and groups people made, and the pins in each group, as
// Andamento's Dashboard has them.
internal void
uishell_logical_local(UIShell_LogicalText *t, U64 depth, CFG_Node *window)
{
  Arena *arena = t->arena;
  UIShell_Locals *locals = uishell_sidebar_window_locals(window);
  for(U64 s = 0; s < locals->section_count; s++)
  {
    UIShell_LocalSection *section = &locals->sections[s];
    uishell_logical_linef(t, depth, "section %S", uishell_logical_quote(arena, uishell_sidebar_local_title(arena, section)));
    for(U64 g = 0; g < section->group_count; g++)
    {
      UIShell_LocalGroup *group = &section->groups[g];
      uishell_logical_linef(t, depth+1, "group %S%s", uishell_logical_quote(arena, group->label), group->is_default ? " default" : "");
      for(U64 p = 0; p < group->pin_count; p++)
      {
        UIShell_LocalPin *pin = &group->pins[p];
        AndamentoEntity target = uishell_sidebar_pin_entity(pin);
        uishell_logical_linef(t, depth+2, "pin %S %S %s%S",
          uishell_logical_entity(arena, uishell_sidebar_string(target.kind), uishell_sidebar_string(target.id)),
          uishell_logical_quote(arena, pin->label), pin->compact ? "row" : "card",
          uishell_logical_provider(arena, uishell_sidebar_string(target.provider), 0));
      }
    }
  }
}

// Where sections are docked, in the sidebar or floating, and which the
// person closed.
internal void
uishell_logical_sidebar_arrangement(UIShell_LogicalText *t, U64 depth, CFG_Node *window, UIShell_SidebarState *state)
{
  uishell_logical_linef(t, depth, "sidebar arrangement");
  RD_Arrangement *sidebar = rd_arrangement_live(rd_panel_owner_from_host(RD_PanelOwnerKind_Sidebar, window, 0));
  if(sidebar->root != &rd_nil_arrangement_panel)
  {
    uishell_logical_linef(t, depth+1, "docked");
    uishell_logical_panels(t, depth+2, sidebar->root, sidebar->root_axis, 1);
  }
  U64 floating = rd_floating_count(window);
  if(floating != 0)
  {
    uishell_logical_linef(t, depth+1, "floating");
    // Each Floating Panel is its own arrangement.
    for(U64 n = 1; n <= floating; n++)
    {
      RD_Arrangement *arrangement = rd_arrangement_live(rd_panel_owner_from_host(RD_PanelOwnerKind_Floating, window, n));
      uishell_logical_panels(t, depth+2, arrangement->root, arrangement->root_axis, 1);
    }
  }
  // Andamento's notes: the sections closed on purpose.
  for(U64 i = 0; state && i < state->sidebar_closed_count; i++)
  {
    String8 key = state->sidebar_closed[i];
    uishell_logical_linef(t, depth+1, "closed %S%s",
      uishell_logical_quote(t->arena, uishell_logical_section_key_title(t->arena, state, window, key)),
      uishell_sidebar_section_gone(state, key) ? " flagged" : "");
  }
}

internal void
uishell_logical_sidebar(UIShell_LogicalText *t, U64 depth, CFG_Node *window)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  UIShell_SidebarState *state = ws != &rd_nil_window_state ? ws->sidebar : 0;
  uishell_logical_linef(t, depth, "sidebar");
  if(!state || !state->snapshot) { uishell_logical_linef(t, depth+1, "unavailable"); }
  else
  {
    uishell_logical_display(t, depth+1, state);
    uishell_logical_linef(t, depth+1, "rows");
    uishell_logical_rows(t, depth+2, state);
    uishell_logical_linef(t, depth+1, "orders");
    uishell_logical_orders(t, depth+2, state);
  }
  uishell_logical_linef(t, depth+1, "local");
  uishell_logical_local(t, depth+2, window);
  uishell_logical_sidebar_arrangement(t, depth, window, state);
}

//- Presentation

// A panel's path from its tree's root: child positions from one, joined by
// dots ("root" for the root itself).
internal String8
uishell_logical_panel_path(Arena *arena, RD_PanelNode *root, RD_PanelNode *panel)
{
  String8 path = str8_zero();
  for(RD_PanelNode *p = panel; p != root && p != &rd_nil_panel_node; p = p->parent)
  {
    U64 index = 1;
    for(RD_PanelNode *s = p->prev; s != &rd_nil_panel_node; s = s->prev) { index++; }
    path = path.size ? push_str8f(arena, "%I64u.%S", index, path) : push_str8f(arena, "%I64u", index);
  }
  return path.size ? path : str8_lit("root");
}

internal void
uishell_logical_presentation(UIShell_LogicalText *t, U64 depth, CFG_Node *window, U64 number)
{
  Arena *arena = t->arena;
  uishell_logical_linef(t, depth, "window %I64u", number);
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  if(ws == &rd_nil_window_state) { uishell_logical_linef(t, depth+1, "not open"); return; }
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(arena, window);
  UIShell_MaterializedWorkspace *visible = split.inventory.selected;
  if(!visible) { uishell_logical_linef(t, depth+1, "visible workspace none"); return; }
  uishell_logical_linef(t, depth+1, "visible workspace %S", uishell_logical_quote(arena, visible->display_name));
  RD_PanelTree tree = visible->mount.panel_tree;
  if(tree.focused == &rd_nil_panel_node) { uishell_logical_linef(t, depth+1, "focused panel none"); }
  else
  {
    uishell_logical_linef(t, depth+1, "focused panel %S: %S", uishell_logical_panel_path(arena, tree.root, tree.focused),
      !rd_view_address_is_nil(tree.focused->selected_tab) ? uishell_logical_tab(arena, rd_cfg_from_view(tree.focused->selected_tab), 0) : str8_lit("no tab"));
  }
  // Which host takes input: the sidebar's sections, the workspace, or none
  // until something is focused.
  char *input = "none";
  if(rd_panel_exists(ws->active_panel))
  {
    RD_PanelOwnerKind kind = ws->active_panel.owner.kind;
    input = kind == RD_PanelOwnerKind_Sidebar || kind == RD_PanelOwnerKind_Floating ? "sidebar" : "workspace";
  }
  uishell_logical_linef(t, depth+1, "input %s", input);
}

internal String8
uishell_logical_state_text(Arena *arena)
{
  Temp scratch = scratch_begin(&arena, 1);
  UIShell_LogicalText t = {scratch.arena};
  CFG_NodePtrList windows = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("window"));
  uishell_logical_linef(&t, 0, "subscriptions%s", uishell_subscriptions.first ? "" : " none");
  U64 number = 1;
  for(UIShell_Subscription *s = uishell_subscriptions.first; s; s = s->next, number++)
  {
    uishell_logical_linef(&t, 1, "subscription %I64u %S%S%s", number, s->kind,
      s->daemon.size ? push_str8f(t.arena, " daemon=%S", uishell_logical_quote(t.arena, s->daemon)) : str8_zero(),
      s->stale ? " stale" : "");
  }
  number = 1;
  for(CFG_NodePtrNode *n = windows.first; n; n = n->next, number++)
  {
    uishell_logical_linef(&t, 0, "window %I64u", number);
    uishell_logical_workspaces(&t, 1, n->v);
    uishell_logical_sidebar(&t, 1, n->v);
  }
  uishell_logical_linef(&t, 0, "presentation");
  number = 1;
  for(CFG_NodePtrNode *n = windows.first; n; n = n->next, number++)
  { uishell_logical_presentation(&t, 1, n->v, number); }
  String8 result = str8_list_join(arena, &t.lines, 0);
  scratch_end(scratch);
  return result;
}
