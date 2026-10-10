// The logical state as text (uishell_logical_state.h). It reads the config
// tree and each window's current Andamento snapshot and dashboard record and
// changes none of them, so it can run between any two commands.
//
//   subscriptions         the Dashboard's, in order: kind, daemon, stale
//   window <n>
//     workspaces          selector order; subject or local; kept; arrangement
//     sidebar             persisted display values; rows as Andamento presents
//                         them; the sibling orders it keeps; local sections
//                         and pins
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
  String8 label = rd_label_from_cfg(tab);
  if(label.size) { str8_list_pushf(arena, &parts, " %S", uishell_logical_quote(arena, label)); }
  // Its Slot's key in Andamento (uishell_workspace_store.c); before it has
  // one, the provider's slot it was made for.
  String8 slot = cfg_node_child_from_string(tab, str8_lit("slot"))->first->string;
  if(!slot.size) { slot = cfg_node_child_from_string(tab, str8_lit("resource_id"))->first->string; }
  if(slot.size) { str8_list_pushf(arena, &parts, " slot=%S", uishell_logical_quote(arena, slot)); }
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
  return push_str8f(arena, "section %S%s%s%s%s", uishell_logical_quote(arena, rd_label_from_cfg(view)),
    selected ? " selected" : "", uishell_sidebar_section_collapsed(view) ? " collapsed" : "",
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
  { uishell_logical_linef(t, depth+1, "%S", uishell_logical_view(t->arena, cfg_node_from_id(tab->view), tab->view == panel->selected)); }
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
    CFG_Node *group = uishell_sidebar_local_group(window, uishell_sidebar_local_field(owner, str8_lit("lives_in")));
    if(group != &cfg_nil_node && cfg_node_child_from_string(group, str8_lit("default")) == &cfg_nil_node)
    { home = push_str8f(arena, " local group=%S", uishell_logical_quote(arena, uishell_sidebar_local_field(group, str8_lit("label")))); }
  }
  uishell_logical_linef(t, depth, "workspace %S%S%s", uishell_logical_quote(arena, label), home, kept ? " kept" : "");
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
  CFG_Node *section = uishell_sidebar_local_section(window, uishell_sidebar_local_key_id(key));
  if(section != &cfg_nil_node) { return uishell_sidebar_local_title(arena, section); }
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
    AndamentoText provider = {0};
    uint32_t stale = 0;
    if(andamento_snapshot_node_provider(state->snapshot, i, &provider, &stale))
    { str8_list_push(arena, &flags, uishell_logical_provider(arena, uishell_sidebar_string(provider), stale)); }
    uishell_logical_linef(t, depth+depths[i], "row %S %S%S",
      uishell_logical_entity(arena, uishell_sidebar_string(node.entity_kind), uishell_sidebar_string(node.entity_id)),
      title, str8_list_join(arena, &flags, 0));
  }
}

// An entity named in a saved order, labelled as its row is.
internal String8
uishell_logical_order_entity(Arena *arena, UIShell_SidebarState *state, String8 kind, String8 id)
{
  String8 name = uishell_logical_entity(arena, kind, id);
  for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    if(str8_match(uishell_sidebar_string(node.entity_kind), kind, 0) && str8_match(uishell_sidebar_string(node.entity_id), id, 0))
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
  for(UIShell_KdlNode *order = uishell_sidebar_dashboard(state); order; order = order->next)
  {
    if(!str8_match(order->name, str8_lit("order"), 0)) { continue; }
    String8 under = str8_lit("(no rows)");
    U64 row = uishell_sidebar_order_row(state, order);
    if(row != ANDAMENTO_NONE)
    {
      AndamentoNode node = {0}, parent = {0};
      uishell_sidebar_snapshot_node(state->snapshot, row, &node);
      uishell_sidebar_node_at(state, node.parent, &parent);
      under = uishell_logical_quote(arena, parent.is_section ? uishell_logical_section_title(state, parent) : uishell_sidebar_string(parent.label));
    }
    uishell_logical_linef(t, depth, "order under %S", under);
    for(UIShell_KdlNode *e = order->first; e; e = e->next)
    {
      if(!str8_match(e->name, str8_lit("entity"), 0) || e->arg_count < 2) { continue; }
      uishell_logical_linef(t, depth+1, "%S", uishell_logical_order_entity(arena, state, e->args[0], e->args[1]));
    }
  }
}

// Sections and groups people made, and the pins in each group (the
// window's working copy of what Andamento keeps).
internal void
uishell_logical_local(UIShell_LogicalText *t, U64 depth, CFG_Node *window)
{
  Arena *arena = t->arena;
  CFG_Node *root = uishell_sidebar_local_tree(window);
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    if(!str8_match(section->string, str8_lit("section"), 0)) { continue; }
    uishell_logical_linef(t, depth, "section %S", uishell_logical_quote(arena, uishell_sidebar_local_title(arena, section)));
    for(CFG_Node *group = section->first; group != &cfg_nil_node; group = group->next)
    {
      if(!str8_match(group->string, str8_lit("group"), 0)) { continue; }
      uishell_logical_linef(t, depth+1, "group %S%s", uishell_logical_quote(arena, uishell_sidebar_local_field(group, str8_lit("label"))),
        uishell_sidebar_local_is_default(group) ? " default" : "");
      for(CFG_Node *card = group->first; card != &cfg_nil_node; card = card->next)
      {
        if(!str8_match(card->string, str8_lit("card"), 0)) { continue; }
        uishell_logical_linef(t, depth+2, "pin %S %S %s",
          uishell_logical_entity(arena, uishell_sidebar_local_field(card, str8_lit("kind")), uishell_sidebar_local_field(card, str8_lit("entity"))),
          uishell_logical_quote(arena, uishell_sidebar_local_field(card, str8_lit("label"))),
          cfg_node_child_from_string(card, str8_lit("compact")) != &cfg_nil_node ? "row" : "card");
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
  CFG_Node *sidebar = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
  if(sidebar != &cfg_nil_node)
  {
    uishell_logical_linef(t, depth+1, "docked");
    RD_Arrangement *arrangement = uishell_arrangement_from_cfg(t->arena, sidebar);
    uishell_logical_panels(t, depth+2, arrangement->root, arrangement->root_axis, 1);
  }
  CFG_Node *floating = cfg_node_child_from_string(window, str8_lit("floating_panels"));
  if(floating != &cfg_nil_node)
  {
    uishell_logical_linef(t, depth+1, "floating");
    // Each Floating Panel is its own arrangement.
    for(CFG_Node *c = floating->first; c != &cfg_nil_node; c = c->next)
    {
      if(rd_dock_floating_panel_from_cfg(c) != c) { continue; }
      RD_Arrangement *arrangement = uishell_arrangement_from_cfg(t->arena, c);
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
uishell_logical_panel_path(Arena *arena, CFG_PanelNode *root, CFG_PanelNode *panel)
{
  String8 path = str8_zero();
  for(CFG_PanelNode *p = panel; p != root && p != &cfg_nil_panel_node; p = p->parent)
  {
    U64 index = 1;
    for(CFG_PanelNode *s = p->prev; s != &cfg_nil_panel_node; s = s->prev) { index++; }
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
  CFG_PanelTree tree = visible->mount.panel_tree;
  if(tree.focused == &cfg_nil_panel_node) { uishell_logical_linef(t, depth+1, "focused panel none"); }
  else
  {
    uishell_logical_linef(t, depth+1, "focused panel %S: %S", uishell_logical_panel_path(arena, tree.root, tree.focused),
      tree.focused->selected_tab != &cfg_nil_node ? uishell_logical_tab(arena, tree.focused->selected_tab, 0) : str8_lit("no tab"));
  }
  // Which host takes input: the sidebar's sections, the workspace, or none
  // until something is focused.
  char *input = "none";
  for(CFG_Node *c = cfg_node_from_id(ws->active_panel_id); c != &cfg_nil_node && c != window; c = c->parent)
  {
    input = "workspace";
    if(str8_match(c->string, RD_DOCK_SIDEBAR_ROOT, 0) || str8_match(c->string, str8_lit("floating_panels"), 0))
    { input = "sidebar"; break; }
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
