//- The Dashboard's sidebar arrangement in Andamento (ADR 0012; andamento.h,
// "the Dashboard's sidebar arrangement"; Andamento's
// docs/sidebar-design/sidebar-arrangement.md). Which sections are docked
// where, and which are closed, is Dashboard state: Andamento keeps it as one
// document in the dashboard record, and places, restores and flags sections
// against what the template and the local sections declare. Wheelhouse's
// live copy is the window's sidebar arrangement (the dock; shell_core.c,
// "Live arrangements"), which docking edits. A tab's key is a
// sidebar_section View's `section`; any other View is one of this host's
// own, keyed by its `slot` ("u:<n>"), which Andamento stores but never
// places.
//
//   load    A window saved since has no sidebar in the presentation file,
//           only `sidebar_presentation`: the dock follows
//           andamento_sidebar_arrangement_get, with this device's settings
//           merged back in. A new Dashboard's take Andamento's placement
//           (uishell_sidebar_store_load).
//   commit  Once no gesture is in flight, a sidebar whose arrangements were
//           set since is committed whole at the generation last seen
//           (andamento_sidebar_arrangement_commit). Closing a section is
//           committing without it. A stale commit is made again at the new
//           generation, as a workspace's is (uishell_workspace_store.c): only
//           Andamento's own reconciliation moves it. A section it placed
//           meanwhile, which the edit can't have, the recommit closes, so it
//           is restored: neither the edit nor what was declared is lost.
//   follow  When Andamento changed the document itself (it placed a section
//           newly declared, removed a duplicate, or another frontend
//           committed), the live copy follows it. The container region of
//           local sections, which Andamento places while there are none
//           (before Wheelhouse first publishes them), is not shown once they
//           exist: they are, in its place.
//   restore, reset   The Sections menu's Restore and Reset To Default Panel
//           Layout are andamento_sidebar_restore_section and
//           andamento_sidebar_reset, then a follow.
//
// What stays here needs pixels or this host's config: the docking checker
// (shell_docking.h: a section never docks inside a child Workspace level); a
// section a safety restore moved (`section_hint_pending`, rd_dock_restore),
// which a commit leaves out, with the panel it left (`restore_emptied`), and
// then restores by its hints; and the labels KDL titles give section Views
// (uishell_sidebar_store_labels).
//
// Andamento's notes: a key that no longer resolves (template drift, ADR 0013)
// keeps its tab, flagged, and its View shows a placeholder the person can
// remove (sidebar_section's View UI); a closed one stays closed. The same
// UNRESOLVED notes name saved Dashboard keys the template no longer has
// ("display:", "collapse:", "order:" or "variable:" and the key), which no
// tab carries. PLACED, RESTORED and DUPLICATE need nothing shown: the
// follow shows their result.
//
// Device-local (Presentation State, in the presentation file):
//
//   sidebar_generation: 12
//   sidebar_presentation: {
//     section: {key: "attention" weight: 0.25}   the size content gave a section
//     view: {terminal: {slot: "u:1" ...}}        a View of this host's own, whole
//   }
//
// Each View's Presentation State is the window's `view_presentation`, by View
// Address (shell_core.c, "View Presentation State"), and each panel's its
// `panel_presentation`, by panel address ("Panel Presentation State"), both
// saved with the window. A `panel` entry, which said where a Floating Panel
// floated before they were removed, goes with the rest on the next follow.
//
// A docked section the sidebar sizes from its content (no
// `sidebar_layout_sized`) is committed with weight 1: its size is this
// device's, kept by section key. Sizes set by dragging are the document's.

internal String8 uishell_sidebar_section_key(CFG_Node *view);
internal UIShell_SectionTitle *uishell_sidebar_section_title(UIShell_SidebarState *state, String8 key);
internal void uishell_sidebar_section_titles(UIShell_SidebarState *state);
internal B32 uishell_sidebar_section_container(UIShell_SidebarState *state, String8 key);

typedef struct UIShell_SidebarDoc UIShell_SidebarDoc;
struct UIShell_SidebarDoc
{
  // The dock's panels, in preorder, each naming its parent by index.
  AndamentoDocPanel *panels;
  U64 panel_count;
  // Each tab panel's keys, adjacent in tree order.
  AndamentoText *tabs;
  String8 *keys;
  U64 tab_count;
  // Sections a safety restore moved (section_hint_pending): left out, and
  // restored by their hints once committed.
  String8 *pending;
  U64 pending_count;
};

//- Keys

// A View's section key: a section View's `section`, else its `slot`. Saved
// content that isn't a registered View has none, and goes.
internal String8
uishell_sidebar_store_key(CFG_Node *view)
{
  if(str8_match(view->string, str8_lit("sidebar_section"), 0)) { return uishell_sidebar_section_key(view); }
  if(!rd_dock_view_from_name(view->string)) { return str8_zero(); }
  return uishell_store_setting(view, str8_lit("slot"));
}

// Whether `p`, a panel of the dock, is a section the sidebar sizes from its
// content: one tab, directly in the dock's root split.
internal B32
uishell_sidebar_store_content_sized(RD_Arrangement *dock, RD_ArrangementPanel *p)
{
  return p != dock->root && p->parent == dock->root && p->first == &rd_nil_arrangement_panel && p->tab_count == 1;
}

internal B32
uishell_sidebar_store_manual(CFG_Node *window)
{
  return cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")) != &cfg_nil_node;
}

// The window's dock, copied.
internal RD_Arrangement *
uishell_sidebar_store_dock(Arena *arena, CFG_Node *window)
{
  return rd_arrangement_from_panel_owner(arena, rd_panel_owner_from_host(RD_PanelOwnerKind_Sidebar, window));
}

// What the sidebar has to commit has changed when the dock was set, or its
// sections stopped (or started) sizing from content: a token of each.
internal U64
uishell_sidebar_store_version(CFG_Node *window)
{
  U64 version = rd_arrangement_version(rd_panel_owner_from_host(RD_PanelOwnerKind_Sidebar, window));
  return version*2 + uishell_sidebar_store_manual(window);
}

// The dock's panel ID in a document ID; 0 for one Wheelhouse didn't make.
internal RD_PanelID
uishell_sidebar_store_local_id(String8 id)
{
  return id.size && id.size < 18 && str8_is_integer(id, 10) ? u64_from_str8(id, 10) : 0;
}

//- The sidebar's arrangements, edited together

internal UIShell_SidebarDocks
uishell_sidebar_docks(Arena *arena, CFG_Node *owner)
{
  UIShell_SidebarDocks docks = {owner, arena};
  RD_Arrangement *a = docks.dock = uishell_sidebar_store_dock(arena, owner);
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
    {
      UIShell_SidebarDockView *v = push_array(arena, UIShell_SidebarDockView, 1);
      v->view = t->view;
      v->node = rd_cfg_from_view(t->view);
      v->arrangement = a;
      v->panel = p->id;
      SLLQueuePush(docks.first, docks.last, v);
    }
  }
  return docks;
}

// Setting releases `view`; its panel stays until the next frame's layout
// closes it if it has no tabs left (uishell_sidebar_close_empty_panels).
internal void
uishell_sidebar_docks_remove(UIShell_SidebarDocks *docks, UIShell_SidebarDockView *view)
{
  if(rd_arrangement_remove_tab(view->arrangement, view->view)) { docks->dirty = 1; }
  view->node = &cfg_nil_node;
}

internal void
uishell_sidebar_docks_set(UIShell_SidebarDocks *docks)
{
  if(docks->dirty) { rd_arrangement_set(docks->dock); docks->dirty = 0; }
}

// Gives each View of this host's own (anything but a section) a "u:<n>" key
// no other tab of the sidebar has. Returns whether any changed.
internal B32
uishell_sidebar_store_assign_keys(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarDocks docks = uishell_sidebar_docks(scratch.arena, window);
  String8List keys = {0};
  B32 changed = 0;
  U64 next = rd_view_user_number_max(window, RD_ViewOwnerKind_Window)+1;
  for(UIShell_SidebarDockView *v = docks.first; v; v = v->next)
  {
    String8 key = uishell_sidebar_store_key(v->node);
    if(!str8_match(v->node->string, str8_lit("sidebar_section"), 0)) { next = Max(next, uishell_store_user_number(key)+1); }
  }
  for(UIShell_SidebarDockView *v = docks.first; v; v = v->next)
  {
    String8 key = uishell_sidebar_store_key(v->node);
    B32 seen = 0;
    for(String8Node *k = keys.first; k && !seen; k = k->next) { seen = str8_match(k->string, key, 0); }
    if(!str8_match(v->node->string, str8_lit("sidebar_section"), 0) && rd_dock_view_from_name(v->node->string) &&
       (!uishell_store_user_key_valid(key) || seen))
    {
      uishell_store_set_setting(v->node, str8_lit("slot"), push_str8f(scratch.arena, "u:%I64u", next++));
      RD_ViewAddress now = rd_view_address_from_cfg(v->node);
      rd_view_readdress(v->view, now);
      rd_arrangement_readdress(v->arrangement, v->view, now);
      docks.dirty = changed = 1;
      v->view = now;
      key = uishell_sidebar_store_key(v->node);
    }
    str8_list_push(scratch.arena, &keys, key);
  }
  uishell_sidebar_docks_set(&docks);
  scratch_end(scratch);
  return changed;
}

// A sidebar panel shows one of its tabs: a panel with tabs whose Selected
// View went (released with no settle) selects its first. A section's panel
// has no tab bar, so nothing else would bring it back. Returns whether any
// changed.
internal B32
uishell_sidebar_store_select_shown(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *a = uishell_sidebar_store_dock(scratch.arena, window);
  B32 changed = 0;
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    if(p->first_tab == 0 || (rd_arrangement_tab_from_view(p, p->selected) != 0 && rd_cfg_from_view(p->selected) != &cfg_nil_node)) { continue; }
    // Its first tab whose View is there; setting drops those that aren't.
    RD_ArrangementTab *shown = p->first_tab;
    while(shown && rd_cfg_from_view(shown->view) == &cfg_nil_node) { shown = shown->next; }
    changed |= rd_arrangement_select(a, p->id, shown ? shown->view : rd_nil_view_address);
  }
  if(changed) { rd_arrangement_set(a); }
  scratch_end(scratch);
  return changed;
}

//- Documents

// The window's sidebar as a document: the dock's panels, in preorder.
internal UIShell_SidebarDoc
uishell_sidebar_store_doc(Arena *arena, UIShell_SidebarState *state, CFG_Node *window)
{
  UIShell_SidebarDoc doc = {0};
  RD_Arrangement *a = uishell_sidebar_store_dock(arena, window);
  U64 panels = 0, tabs = 0;
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  { panels += 1; tabs += p->tab_count; }
  doc.panels = push_array(arena, AndamentoDocPanel, panels);
  doc.tabs = push_array(arena, AndamentoText, tabs);
  doc.keys = push_array(arena, String8, tabs);
  doc.pending = push_array(arena, String8, tabs);
  RD_ArrangementPanel **order = push_array(arena, RD_ArrangementPanel *, panels);
  B32 manual = uishell_sidebar_store_manual(window);
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    // A panel a safety restore emptied goes with the section it lost,
    // unless its split would be left with none.
    if(p->first == &rd_nil_arrangement_panel && p != a->root && p->parent->child_count > 1 && p->restore_emptied)
    {
      B32 keeps = 0;
      for(RD_ArrangementTab *t = p->first_tab; t && !keeps; t = t->next)
      { keeps = cfg_node_child_from_string(rd_cfg_from_view(t->view), str8_lit("section_hint_pending")) == &cfg_nil_node; }
      if(!keeps)
      {
        for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
        { doc.pending[doc.pending_count++] = uishell_sidebar_store_key(rd_cfg_from_view(t->view)); }
        continue;
      }
    }
    U64 index = doc.panel_count++;
    order[index] = p;
    AndamentoDocPanel *out = &doc.panels[index];
    out->parent = ANDAMENTO_NONE;
    for(U64 j = index; j > 0 && out->parent == ANDAMENTO_NONE; j--) { if(order[j-1] == p->parent) { out->parent = j-1; } }
    out->id = uishell_sidebar_text(push_str8f(arena, "%I64u", p->id));
    // A root's weight means nothing to Andamento, and the size the sidebar
    // gives a docked section from its content is this device's.
    F32 weight = p->weight;
    if(p == a->root || (!manual && uishell_sidebar_store_content_sized(a, p))) { weight = 1.f; }
    out->weight = (weight > 0 && weight < 1e30f) ? f64_from_str8(rd_arrangement_weight_string(arena, weight)) : 0.01;
    out->selected = ANDAMENTO_NONE;
    if(p->first != &rd_nil_arrangement_panel)
    {
      out->kind = ANDAMENTO_PANEL_SPLIT;
      out->axis = rd_arrangement_split_axis(a, p) == Axis2_X ? ANDAMENTO_AXIS_ROW : ANDAMENTO_AXIS_COLUMN;
      continue;
    }
    out->kind = ANDAMENTO_PANEL_TABS;
    out->tabs = doc.tabs+doc.tab_count;
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
    {
      CFG_Node *view = rd_cfg_from_view(t->view);
      String8 key = uishell_sidebar_store_key(view);
      // A View with no key is corrupt saved state, shown as unavailable
      // and never committed.
      if(!key.size) { continue; }
      if(cfg_node_child_from_string(view, str8_lit("section_hint_pending")) != &cfg_nil_node)
      { doc.pending[doc.pending_count++] = key; continue; }
      if(rd_view_address_match(t->view, p->selected)) { out->selected = out->tab_count; }
      doc.keys[doc.tab_count] = key;
      doc.tabs[doc.tab_count] = uishell_sidebar_text(key);
      doc.tab_count += 1;
      out->tab_count += 1;
    }
  }
  return doc;
}

// The document as Andamento takes it.
internal AndamentoSidebarDoc
uishell_sidebar_store_andamento_doc(UIShell_SidebarDoc *doc)
{
  AndamentoSidebarDoc result = {{doc->panels, doc->panel_count}};
  return result;
}

// Whether Andamento made `after` of `sent` as it was: the same panels, with
// the same tabs.
internal B32
uishell_sidebar_store_same(AndamentoSidebarDoc *after, AndamentoSidebarDoc *sent)
{
  AndamentoArrangementDoc *a = &after->dock, *b = &sent->dock;
  if(a->panel_count != b->panel_count) { return 0; }
  for(U64 i = 0; i < a->panel_count; i++)
  {
    AndamentoDocPanel const *p = &a->panels[i], *q = &b->panels[i];
    if(!str8_match(uishell_sidebar_string(p->id), uishell_sidebar_string(q->id), 0) || p->parent != q->parent || p->weight != q->weight ||
       p->kind != q->kind || (p->kind == ANDAMENTO_PANEL_SPLIT && p->axis != q->axis) || p->tab_count != q->tab_count || p->selected != q->selected) { return 0; }
    for(U64 t = 0; t < p->tab_count; t++) { if(!str8_match(uishell_sidebar_string(p->tabs[t]), uishell_sidebar_string(q->tabs[t]), 0)) { return 0; } }
  }
  return 1;
}

// What the sidebar last committed, or followed, kept: committing the same
// again would change nothing, though its arrangements were set since (and
// set back), so it isn't.
internal void
uishell_sidebar_store_saw(UIShell_SidebarState *state, AndamentoSidebarDoc *doc)
{
  if(!state->sidebar_seen_arena) { state->sidebar_seen_arena = arena_alloc(); }
  arena_clear(state->sidebar_seen_arena);
  Arena *arena = state->sidebar_seen_arena;
  AndamentoSidebarDoc result = {0};
  AndamentoArrangementDoc *from = &doc->dock;
  AndamentoDocPanel *panels = push_array(arena, AndamentoDocPanel, from->panel_count);
  for(U64 i = 0; i < from->panel_count; i++)
  {
    panels[i] = from->panels[i];
    panels[i].id = uishell_sidebar_text(push_str8_copy(arena, uishell_sidebar_string(from->panels[i].id)));
    AndamentoText *tabs = push_array(arena, AndamentoText, panels[i].tab_count);
    for(U64 t = 0; t < panels[i].tab_count; t++) { tabs[t] = uishell_sidebar_text(push_str8_copy(arena, uishell_sidebar_string(from->panels[i].tabs[t]))); }
    panels[i].tabs = tabs;
  }
  result.dock.panels = panels;
  result.dock.panel_count = from->panel_count;
  state->sidebar_seen = result;
  state->sidebar_seen_valid = 1;
}

//- Notes

internal B32
uishell_sidebar_store_listed(String8 *keys, U64 count, String8 key)
{
  for(U64 i = 0; i < count; i++) { if(str8_match(keys[i], key, 0)) { return 1; } }
  return 0;
}

// Whether `key` no longer resolves: the template dropped or renamed its
// region (ADR 0013), or its local section is gone.
internal B32
uishell_sidebar_section_gone(UIShell_SidebarState *state, String8 key)
{
  return uishell_sidebar_store_listed(state->sidebar_gone, state->sidebar_gone_count, key);
}

// What Andamento's notes say is closed, and what no longer resolves.
internal void
uishell_sidebar_store_notes(UIShell_SidebarState *state, AndamentoSidebarArrangement *arrangement)
{
  if(!state->sidebar_notes_arena) { state->sidebar_notes_arena = arena_alloc(); }
  arena_clear(state->sidebar_notes_arena);
  Arena *arena = state->sidebar_notes_arena;
  U64 count = arrangement ? arrangement->note_count : 0;
  state->sidebar_closed = push_array(arena, String8, count+1);
  state->sidebar_gone = push_array(arena, String8, count+1);
  state->sidebar_closed_count = state->sidebar_gone_count = 0;
  for(U64 i = 0; i < count; i++)
  {
    AndamentoNote *note = &arrangement->notes[i];
    String8 key = push_str8_copy(arena, uishell_sidebar_string(note->key));
    if(note->kind == ANDAMENTO_NOTE_CLOSED) { state->sidebar_closed[state->sidebar_closed_count++] = key; }
    if(note->kind == ANDAMENTO_NOTE_UNRESOLVED) { state->sidebar_gone[state->sidebar_gone_count++] = key; }
  }
}

// Reads Andamento's notes again: what resolves follows the snapshot, and the
// document's generation doesn't move when nothing in it did.
internal void
uishell_sidebar_store_read_notes(UIShell_SidebarState *state)
{
  AndamentoSidebarArrangement *stored = UIShell_StoreCall(state, andamento_sidebar_arrangement_get(state->core, 0));
  uishell_sidebar_store_notes(state, stored);
  andamento_sidebar_arrangement_free(stored);
}

//- Following Andamento's document

// The device-local entry of `kind` whose `name` setting is `value`, or nil.
internal CFG_Node *
uishell_sidebar_store_device(CFG_Node *device, String8 kind, String8 name, String8 value)
{
  for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next)
  {
    if(str8_match(c->string, kind, 0) && str8_match(uishell_store_setting(c, name), value, 0)) { return c; }
  }
  return &cfg_nil_node;
}

// A device-local `view` entry's View: its child that isn't its `weight`.
internal CFG_Node *
uishell_sidebar_store_entry_view(CFG_Node *entry)
{
  CFG_Node *v = entry->first;
  for(; v != &cfg_nil_node && str8_match(v->string, str8_lit("weight"), 0); v = v->next) {}
  return v;
}

// The View of this host's own keyed `key`, as this device saved it, or nil.
internal CFG_Node *
uishell_sidebar_store_device_view(CFG_Node *device, String8 key)
{
  for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next)
  {
    CFG_Node *view = uishell_sidebar_store_entry_view(c);
    if(str8_match(c->string, str8_lit("view"), 0) && str8_match(uishell_store_setting(view, str8_lit("slot")), key, 0)) { return view; }
  }
  return &cfg_nil_node;
}

// Whether Andamento noted `key` as `kind` (ANDAMENTO_NOTE_*).
internal B32
uishell_sidebar_store_noted(AndamentoSidebarArrangement *a, U32 kind, String8 key)
{
  for(U64 i = 0; a && i < a->note_count; i++)
  { if(a->notes[i].kind == kind && str8_match(uishell_sidebar_string(a->notes[i].key), key, 0)) { return 1; } }
  return 0;
}

typedef struct UIShell_SidebarFollow UIShell_SidebarFollow;
struct UIShell_SidebarFollow
{
  Arena *arena;
  // Every tab, the dock's first, and each tab's View (nil: none to show
  // here). A panel's first tab is first_tab[] of its document.
  AndamentoText *tabs;
  U64 tab_count;
  RD_ViewAddress *views;
  // The sizes this device gave the dock's sections from their content, by key.
  String8 *size_keys;
  F32 *sizes;
  U64 size_count;
  B32 manual;
  // A panel whose Selected View the document didn't name shows its first.
  B32 selected_first;
};

// Fills the cleared `a` with the tree under panel `root` of `doc`. `old_ids`
// are the IDs the arrangement had, which new panels stay clear of.
internal void
uishell_sidebar_store_fill(UIShell_SidebarFollow *f, RD_Arrangement *a, AndamentoArrangementDoc *doc, UIShell_DocShape *shape,
                           U64 *first_tab, U64 root, B32 dock, RD_PanelID *old_ids, U64 old_count)
{
  Arena *arena = f->arena;
  U64 count = doc->panel_count;
  // The panels under `root`: each after its parent.
  B32 *in_tree = push_array(arena, B32, count+1);
  U64 max_id = 0, panel_count = 0;
  for(U64 i = 0; i < old_count; i++) { max_id = Max(max_id, old_ids[i]); }
  for(U64 i = root; i < count; i++)
  {
    U64 up = doc->panels[i].parent;
    in_tree[i] = i == root || (up < i && in_tree[up]);
    if(!in_tree[i]) { continue; }
    panel_count += 1;
    max_id = Max(max_id, uishell_sidebar_store_local_id(uishell_sidebar_string(doc->panels[i].id)));
  }
  U64 fresh = max_id+1;
  // Allocations stay clear of the IDs set below until they are all set.
  a->next_id = max_id + (1ull << 40);
  if(doc->panels[root].kind == ANDAMENTO_PANEL_SPLIT) { a->root_axis = doc->panels[root].axis == ANDAMENTO_AXIS_ROW ? Axis2_X : Axis2_Y; }
  U64 ids_used = 0;
  RD_PanelID *ids = push_array(arena, RD_PanelID, panel_count+1);
  RD_PanelID *made = push_array(arena, RD_PanelID, count+1);
  for(U64 index = root; index < count; index++)
  {
    if(!in_tree[index]) { continue; }
    AndamentoDocPanel *src = &doc->panels[index];
    U64 up = index == root ? ANDAMENTO_NONE : shape->parent[index];
    if(index != root && shape->lifted[index]) { made[index] = made[up]; continue; }
    F32 weight = shape->weight[index];
    // A docked section's size that content decided stays this device's.
    if(dock && !f->manual && up == root && src->kind == ANDAMENTO_PANEL_TABS && src->tab_count == 1)
    {
      String8 key = uishell_sidebar_string(src->tabs[0]);
      for(U64 i = 0; i < f->size_count; i++) { if(str8_match(f->size_keys[i], key, 0)) { weight = f->sizes[i]; break; } }
    }
    RD_PanelID panel_id = up != ANDAMENTO_NONE ? rd_arrangement_add(a, made[up], weight) : a->root->id;
    RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(a, panel_id);
    // A panel keeps its ID, and so its Presentation State. A leaf Andamento
    // lifted out of the root keeps the root's ID, and its Presentation State.
    RD_PanelID wanted = uishell_sidebar_store_local_id(uishell_sidebar_string(src->id));
    for(U64 i = 0; i < ids_used && wanted; i++) { if(ids[i] == wanted) { wanted = 0; } }
    if(!wanted) { wanted = fresh++; }
    panel->id = wanted;
    made[index] = wanted;
    ids[ids_used++] = wanted;
    if(src->kind != ANDAMENTO_PANEL_TABS) { continue; }
    RD_ViewAddress prev = rd_nil_view_address, selected = rd_nil_view_address, first = rd_nil_view_address;
    for(U64 t = 0; t < src->tab_count && first_tab[index]+t < f->tab_count; t++)
    {
      RD_ViewAddress view = f->views[first_tab[index]+t];
      if(rd_view_address_is_nil(view)) { continue; }
      rd_arrangement_insert_tab(a, view, wanted, prev, 0);
      if(t == src->selected) { selected = view; }
      if(rd_view_address_is_nil(first)) { first = view; }
      prev = view;
    }
    // Andamento's document may select none of a panel's tabs; the sidebar
    // shows its first (uishell_sidebar_store_select_shown).
    f->selected_first |= rd_view_address_is_nil(selected) && !rd_view_address_is_nil(first);
    rd_arrangement_select(a, wanted, rd_view_address_is_nil(selected) ? first : selected);
  }
  a->next_id = fresh;
  for(U64 i = 0; i < ids_used; i++) { a->next_id = Max(a->next_id, ids[i]+1); }
}

// The window's dock and Floating Panels follow Andamento's document. The View
// of every key it still tabs stays (runtime state is keyed by its address),
// and so do the dock's panels, by ID. New Views take their settings from the
// device-local `sidebar_presentation`, which goes. Reads Andamento's notes
// too. Returns whether Andamento had a document.
internal B32
uishell_sidebar_store_follow(UIShell_SidebarState *state, CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  AndamentoSidebarArrangement *stored = UIShell_StoreCall(state, andamento_sidebar_arrangement_get(state->core, 0));
  if(!stored)
  {
    scratch_end(scratch);
    return 0;
  }
  uishell_sidebar_store_notes(state, stored);
  AndamentoArrangementDoc *doc = &stored->doc.dock;
  UIShell_DocShape shape = uishell_store_doc_shape(arena, doc);
  CFG_Node *device = cfg_node_child_from_string(window, str8_lit("sidebar_presentation"));
  UIShell_SidebarFollow f = {arena};
  f.manual = uishell_sidebar_store_manual(window);
  // Every tab, in order.
  U64 *first_tab = push_array(arena, U64, doc->panel_count+1);
  for(U64 i = 0; i < doc->panel_count; i++) { first_tab[i] = f.tab_count; f.tab_count += doc->panels[i].tab_count; }
  f.tabs = push_array(arena, AndamentoText, f.tab_count+1);
  f.views = push_array(arena, RD_ViewAddress, f.tab_count+1);
  for(U64 i = 0; i < doc->panel_count; i++)
  {
    AndamentoDocPanel *p = &doc->panels[i];
    for(U64 t = 0; t < p->tab_count; t++) { f.tabs[first_tab[i]+t] = p->tabs[t]; }
  }

  // What the window has now: its dock, every View in it, and the sizes
  // content gave its docked sections, as the dock has them, else as this
  // device saved them.
  UIShell_SidebarDocks docks = uishell_sidebar_docks(arena, window);
  RD_Arrangement *dock = docks.dock;
  U64 old_count = 0;
  for(RD_ArrangementPanel *p = dock->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(dock->root, p)) { old_count++; }
  RD_PanelID *old_ids = push_array(arena, RD_PanelID, old_count+1);
  {
    U64 n = 0;
    for(RD_ArrangementPanel *p = dock->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(dock->root, p), n++) { old_ids[n] = p->id; }
  }
  U64 size_capacity = 0;
  for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next) { size_capacity++; }
  U64 dock_tabs = 0;
  for(RD_ArrangementPanel *p = dock->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(dock->root, p)) { dock_tabs += p->tab_count; }
  f.size_keys = push_array(arena, String8, size_capacity+dock_tabs+1);
  f.sizes = push_array(arena, F32, size_capacity+dock_tabs+1);
  if(dock->root != &rd_nil_arrangement_panel)
  {
    for(RD_ArrangementPanel *p = dock->root->first; p != &rd_nil_arrangement_panel; p = p->next)
    {
      if(p->first != &rd_nil_arrangement_panel || !(p->weight > 0 && p->weight < 1e30f)) { continue; }
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        f.size_keys[f.size_count] = uishell_sidebar_store_key(rd_cfg_from_view(t->view));
        f.sizes[f.size_count++] = p->weight;
      }
    }
  }
  for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next)
  {
    CFG_Node *weight = cfg_node_child_from_string(c, str8_lit("weight"));
    B32 section = str8_match(c->string, str8_lit("section"), 0), view = str8_match(c->string, str8_lit("view"), 0);
    if(!(section || view) || weight == &cfg_nil_node) { continue; }
    F32 value = (F32)f64_from_str8(weight->first->string);
    if(!(value > 0 && value < 1e30f)) { continue; }
    f.size_keys[f.size_count] = section ? uishell_store_setting(c, str8_lit("key")) : uishell_store_setting(uishell_sidebar_store_entry_view(c), str8_lit("slot"));
    f.sizes[f.size_count++] = value;
  }

  // Each tab's View: the one showing its key, else a new one.
  CFG_Node *home = rd_view_namespace(window, RD_ViewOwnerKind_Window);
  for(U64 t = 0; t < f.tab_count; t++)
  {
    String8 key = uishell_sidebar_string(f.tabs[t]);
    for(UIShell_SidebarDockView *v = docks.first; v; v = v->next)
    {
      if(v->node == &cfg_nil_node || !str8_match(uishell_sidebar_store_key(v->node), key, 0)) { continue; }
      f.views[t] = v->view;
      v->node = &cfg_nil_node;
      break;
    }
    if(!rd_view_address_is_nil(f.views[t])) { continue; }
    // The container of local sections, placed while there were none (as
    // before they are first published), shows them in its place instead.
    if(uishell_sidebar_store_noted(stored, ANDAMENTO_NOTE_UNRESOLVED, key) && uishell_sidebar_section_container(state, key)) { continue; }
    if(str8_match(str8_prefix(key, 2), str8_lit("u:"), 0))
    {
      // A View of this host's own: as this device saved it. Another
      // device's isn't shown: the dock takes only sections.
      CFG_Node *saved = uishell_sidebar_store_device_view(device, key);
      CFG_Node *view = &cfg_nil_node;
      if(saved != &cfg_nil_node)
      {
        view = cfg_node_deep_copy(rd_state->cfg, saved);
        cfg_node_insert_child(rd_state->cfg, home, home->last, view);
      }
      f.views[t] = rd_view_address_from_cfg(view);
      continue;
    }
    CFG_Node *view = cfg_node_new(rd_state->cfg, home, str8_lit("sidebar_section"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("section")), key);
    UIShell_SectionTitle *title = uishell_sidebar_section_title(state, key);
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("label")), title ? title->title : key);
    // This device's settings for it.
    CFG_Node *entry = uishell_sidebar_store_device(device, str8_lit("section"), str8_lit("key"), key);
    for(CFG_Node *s = entry->first; s != &cfg_nil_node; s = s->next)
    {
      if(str8_match(s->string, str8_lit("key"), 0) || str8_match(s->string, str8_lit("weight"), 0)) { continue; }
      cfg_node_insert_child(rd_state->cfg, view, view->last, cfg_node_deep_copy(rd_state->cfg, s));
    }
    f.views[t] = rd_view_address_from_cfg(view);
  }

  // The dock.
  if(doc->panel_count == 0) { rd_arrangement_discard(dock); }
  else
  {
    rd_arrangement_clear(dock);
    uishell_sidebar_store_fill(&f, dock, doc, &shape, first_tab, 0, 1, old_ids, old_count);
  }
  rd_arrangement_set(dock);

  // A safety restore's marks are done with.
  for(U64 t = 0; t < f.tab_count; t++)
  {
    CFG_Node *pending = cfg_node_child_from_string(rd_cfg_from_view(f.views[t]), str8_lit("section_hint_pending"));
    if(pending != &cfg_nil_node) { cfg_node_release(rd_state->cfg, pending); }
  }
  if(device != &cfg_nil_node) { cfg_node_release(rd_state->cfg, device); }
  uishell_store_set_setting(window, str8_lit("sidebar_generation"), push_str8f(arena, "%I64u", stored->generation));
  state->sidebar_generation = stored->generation;
  andamento_sidebar_arrangement_free(stored);
  state->sidebar_version = uishell_sidebar_store_version(window);
  // A selection the document lacked is the sidebar's change: the next sync
  // commits it.
  state->sidebar_seen_valid = 0;
  if(f.selected_first) { state->sidebar_version = 0; }
  else
  {
    UIShell_SidebarDoc seen = uishell_sidebar_store_doc(arena, state, window);
    AndamentoSidebarDoc seen_doc = uishell_sidebar_store_andamento_doc(&seen);
    uishell_sidebar_store_saw(state, &seen_doc);
  }
  scratch_end(scratch);
  return 1;
}

// The container region of local sections, which Andamento places while
// there are none (before Wheelhouse first publishes them), no longer
// resolves once they exist: they are in its place. Its View goes, with its
// panel when that has nothing else, and the next commit forgets it.
internal void
uishell_sidebar_store_drop_containers(UIShell_SidebarState *state, CFG_Node *window)
{
  for(U64 i = 0; i < state->sidebar_gone_count; i++)
  {
    String8 key = state->sidebar_gone[i];
    if(!uishell_sidebar_section_container(state, key)) { continue; }
    Temp scratch = scratch_begin(0, 0);
    RD_Arrangement *a = uishell_sidebar_store_dock(scratch.arena, window);
    RD_PanelID *emptied = push_array(scratch.arena, RD_PanelID, 1);
    U64 emptied_count = 0;
    B32 changed = 0;
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    {
      B32 lost = 0;
      for(RD_ArrangementTab *t = p->first_tab, *t_next = 0; t; t = t_next)
      {
        t_next = t->next;
        if(!str8_match(uishell_sidebar_store_key(rd_cfg_from_view(t->view)), key, 0)) { continue; }
        rd_arrangement_remove_tab(a, t->view);
        lost = changed = 1;
      }
      if(lost && p->tab_count == 0 && p != a->root && !emptied_count) { emptied[emptied_count++] = p->id; }
    }
    for(U64 e = 0; e < emptied_count; e++) { rd_arrangement_remove(a, emptied[e]); }
    if(changed) { rd_arrangement_set(a); }
    scratch_end(scratch);
  }
}

//- Sync

// Follows Andamento when there is no live copy of this host's own yet: in a
// window saved since (its `sidebar_presentation`), or a new one.
internal void
uishell_sidebar_store_load(UIShell_SidebarState *state, CFG_Node *window)
{
  if(!state->core || !state->window || window->id != state->window) { return; }
  B32 device = cfg_node_child_from_string(window, str8_lit("sidebar_presentation")) != &cfg_nil_node;
  B32 marked = cfg_node_child_from_string(window, str8_lit("sidebar_generation")) != &cfg_nil_node;
  if(device || !marked) { uishell_sidebar_store_follow(state, window); }
}

// Commits `doc` at `expected`. A stale commit is made again at the
// generation that moved it, and what Andamento placed meanwhile, which `doc`
// can't have, is added to *placed_out (from `arena`): committing without it
// closed it, so the caller restores it. Returns the arrangement after
// (COMMITTED, or STALE twice over), or 0 with the sidebar's error set.
internal AndamentoSidebarArrangement *
uishell_sidebar_store_commit(Arena *arena, UIShell_SidebarState *state, UIShell_SidebarDoc *doc, U64 expected, String8List *placed_out)
{
  AndamentoSidebarDoc out = uishell_sidebar_store_andamento_doc(doc);
  AndamentoSidebarArrangement *after = 0;
  char *error = 0;
  for(U32 attempt = 0; attempt < 2; attempt++)
  {
    andamento_string_free(error);
    error = 0;
    andamento_sidebar_arrangement_free(after);
    after = UIShell_StoreCall(state, andamento_sidebar_arrangement_commit(state->core, &out, expected, &error));
    state->sidebar_commits += 1;
    if(!after || after->status != ANDAMENTO_STALE) { break; }
    // Stale: Andamento reconciled what was declared since, and this is its
    // document now. The edit is committed again at its generation, keeping
    // what it placed.
    for(U64 i = 0; i < after->note_count; i++)
    {
      String8 key = uishell_sidebar_string(after->notes[i].key);
      if(after->notes[i].kind != ANDAMENTO_NOTE_PLACED || uishell_sidebar_store_listed(doc->keys, doc->tab_count, key)) { continue; }
      str8_list_push(arena, placed_out, push_str8_copy(arena, key));
    }
    expected = after->generation;
  }
  if(!after || after->status != ANDAMENTO_COMMITTED)
  { uishell_sidebar_set_error(state, error ? str8_cstring(error) : str8_lit("The sidebar's arrangement was not committed")); }
  andamento_string_free(error);
  return after;
}

// Each frame the sidebar observes its window: follows a document Andamento
// changed, then commits a sidebar whose arrangements were set since, once
// no gesture is in flight. `force` commits even then (the window is
// closing, or a menu command needs Andamento's document current).
internal void
uishell_sidebar_store_sync(UIShell_SidebarState *state, CFG_Node *window, B32 force)
{
  if(!state->core || !state->window || window->id != state->window) { return; }
  if(!force && uishell_store_gesture_in_flight()) { return; }
  Temp scratch = scratch_begin(0, 0);
  B32 snapshot = state->snapshot_count != state->sidebar_snapshot_count;
  uishell_sidebar_store_load(state, window);
  B32 marked = cfg_node_child_from_string(window, str8_lit("sidebar_generation")) != &cfg_nil_node;
  // Arrangements set since, but set back, haven't changed.
  if(marked && state->sidebar_seen_valid && uishell_sidebar_store_version(window) != state->sidebar_version)
  {
    UIShell_SidebarDoc doc = uishell_sidebar_store_doc(scratch.arena, state, window);
    AndamentoSidebarDoc unsent = uishell_sidebar_store_andamento_doc(&doc);
    if(doc.pending_count == 0 && uishell_sidebar_store_same(&state->sidebar_seen, &unsent)) { state->sidebar_version = uishell_sidebar_store_version(window); }
  }
  // Andamento moved the document, and the live copy hasn't changed since:
  // it follows.
  char *error = 0;
  U64 generation = UIShell_StoreCall(state, andamento_sidebar_arrangement_generation(state->core, &error));
  andamento_string_free(error);
  B32 followed = 0;
  if(marked && generation != state->sidebar_generation && uishell_sidebar_store_version(window) == state->sidebar_version)
  { followed = uishell_sidebar_store_follow(state, window); }
  // What resolves follows the snapshot even when the document doesn't move.
  B32 notes = followed || snapshot || !state->sidebar_synced;
  if(!followed && notes) { uishell_sidebar_store_read_notes(state); }
  if(notes) { uishell_sidebar_store_drop_containers(state, window); }
  if(marked)
  {
    uishell_sidebar_store_assign_keys(window);
    uishell_sidebar_store_select_shown(window);
    U64 version = uishell_sidebar_store_version(window);
    if(version != state->sidebar_version)
    {
      UIShell_SidebarDoc doc = uishell_sidebar_store_doc(scratch.arena, state, window);
      // At the generation last seen; not followed this session (generations
      // aren't kept across sessions), at Andamento's own.
      U64 expected = state->sidebar_generation ? state->sidebar_generation :
        UIShell_StoreCall(state, andamento_sidebar_arrangement_generation(state->core, 0));
      String8List restore = {0};
      state->sidebar_version = version;
      AndamentoSidebarArrangement *after = uishell_sidebar_store_commit(scratch.arena, state, &doc, expected, &restore);
      if(after && after->status == ANDAMENTO_COMMITTED)
      {
        U64 committed = after->generation;
        state->sidebar_generation = committed;
        uishell_store_set_setting(window, str8_lit("sidebar_generation"), push_str8f(scratch.arena, "%I64u", committed));
        // A section a safety restore moved goes where its hints say, as does
        // one Andamento placed while this commit was stale.
        for(U64 i = 0; i < doc.pending_count; i++) { str8_list_push(scratch.arena, &restore, doc.pending[i]); }
        for(String8Node *n = restore.first; n; n = n->next)
        {
          char *restore_error = 0;
          AndamentoSidebarArrangement *restored = UIShell_StoreCall(state, andamento_sidebar_restore_section(state->core,
            uishell_sidebar_text(n->string), committed, &restore_error));
          andamento_string_free(restore_error);
          if(restored && restored->status == ANDAMENTO_COMMITTED)
          {
            committed = restored->generation;
            andamento_sidebar_arrangement_free(after);
            after = restored;
          }
          else { andamento_sidebar_arrangement_free(restored); }
        }
        // Whatever Andamento made of it (a section placed, a duplicate
        // removed) joins the live copy.
        AndamentoSidebarDoc sent = uishell_sidebar_store_andamento_doc(&doc);
        if(!restore.node_count && uishell_sidebar_store_same(&after->doc, &sent)) { uishell_sidebar_store_notes(state, after); uishell_sidebar_store_saw(state, &sent); }
        else { uishell_sidebar_store_follow(state, window); }
      }
      andamento_sidebar_arrangement_free(after);
    }
    else if(!followed && generation != state->sidebar_generation) { uishell_sidebar_store_follow(state, window); }
  }
  scratch_end(scratch);
  state->sidebar_synced = 1;
  state->sidebar_snapshot_count = state->snapshot_count;
}

//- Menu commands

// Brings Andamento's document up to date with the live copy, and returns
// its generation.
internal U64
uishell_sidebar_store_current(UIShell_SidebarState *state, CFG_Node *window)
{
  state->sidebar_synced = 0;
  uishell_sidebar_store_sync(state, window, 1);
  return UIShell_StoreCall(state, andamento_sidebar_arrangement_generation(state->core, 0));
}

// Restores the declared section `key` by its hints, with an equal share of
// the dock. Returns whether Andamento did.
internal B32
uishell_sidebar_store_restore(UIShell_SidebarState *state, CFG_Node *window, String8 key)
{
  if(!state->core || !state->window || window->id != state->window) { return 0; }
  U64 generation = uishell_sidebar_store_current(state, window);
  char *error = 0;
  AndamentoSidebarArrangement *after = UIShell_StoreCall(state, andamento_sidebar_restore_section(state->core, uishell_sidebar_text(key),
                                                                                                 generation, &error));
  andamento_string_free(error);
  B32 restored = after && after->status == ANDAMENTO_COMMITTED;
  andamento_sidebar_arrangement_free(after);
  if(!restored) { return 0; }
  uishell_sidebar_store_follow(state, window);
  return 1;
}

// Reset To Default Panel Layout: Andamento forgets the arrangement, its
// closed sections and its flagged keys, and places every declared section
// by its hints.
internal void
uishell_sidebar_reset_regions(CFG_Node *window)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  if(ws == &rd_nil_window_state || !ws->sidebar || !ws->sidebar->core || ws->sidebar->window != window->id) { return; }
  UIShell_SidebarState *state = ws->sidebar;
  U64 generation = uishell_sidebar_store_current(state, window);
  char *error = 0;
  AndamentoSidebarArrangement *after = UIShell_StoreCall(state, andamento_sidebar_reset(state->core, generation, &error));
  uishell_sidebar_result(state, after && after->status == ANDAMENTO_COMMITTED, error);
  andamento_sidebar_arrangement_free(after);
  uishell_sidebar_store_follow(state, window);
}

//- Labels

// KDL owns section titles: each section View's label follows its section's,
// checked once per snapshot or config change.
internal void
uishell_sidebar_store_labels(UIShell_SidebarState *state, CFG_Node *window)
{
  if(!state->snapshot || (state->labels_snapshot_count == state->snapshot_count && state->labels_cfg_gen == cfg_change_gen())) { return; }
  Temp scratch = scratch_begin(0, 0);
  CFG_NodePtrList views = rd_view_namespace_views(scratch.arena, window, RD_ViewOwnerKind_Dashboard);
  for(CFG_NodePtrNode *n = views.first; n; n = n->next)
  {
    CFG_Node *view = n->v;
    UIShell_SectionTitle *title = uishell_sidebar_section_title(state, uishell_sidebar_section_key(view));
    if(!title) { continue; }
    CFG_Node *label = cfg_node_child_from_string(view, str8_lit("label"));
    if(label != &cfg_nil_node && label->first == label->last && str8_match(label->first->string, title->title, 0)) { continue; }
    cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("label")), title->title);
  }
  scratch_end(scratch);
  state->labels_snapshot_count = state->snapshot_count;
  state->labels_cfg_gen = cfg_change_gen();
}

//- Device-local

// Starts `kind` entry of `device` named by `name`: `value`, unless *entry has.
internal CFG_Node *
uishell_sidebar_store_entry(CFG_State *copy_state, CFG_Node *device, CFG_Node **entry, String8 kind, String8 name, String8 value)
{
  if(*entry == &cfg_nil_node)
  {
    *entry = cfg_node_new(copy_state, device, kind);
    cfg_node_new(copy_state, cfg_node_new(copy_state, *entry, name), value);
  }
  return *entry;
}

// `window`'s copy as the presentation file holds it: its sidebar's Views are
// left out, and once Andamento keeps the sidebar's arrangement, this
// device's part of them is `sidebar_presentation`.
internal void
uishell_sidebar_store_presentation(CFG_State *copy_state, CFG_Node *window, CFG_Node *copy)
{
  cfg_node_release(copy_state, cfg_node_child_from_string(copy, RD_VIEW_NAMESPACE_WINDOW));
  if(cfg_node_child_from_string(window, str8_lit("sidebar_generation")) == &cfg_nil_node ||
     cfg_node_child_from_string(window, str8_lit("sidebar_presentation")) != &cfg_nil_node) { return; }
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  CFG_Node *device = cfg_node_new(copy_state, copy, str8_lit("sidebar_presentation"));
  B32 manual = uishell_sidebar_store_manual(window);
  RD_Arrangement *a = uishell_sidebar_store_dock(arena, window);
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    B32 sized = !manual && uishell_sidebar_store_content_sized(a, p);
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
    {
      CFG_Node *view = rd_cfg_from_view(t->view);
      String8 key = uishell_sidebar_store_key(view);
      if(!key.size) { continue; }
      if(!str8_match(view->string, str8_lit("sidebar_section"), 0))
      {
        CFG_Node *saved = cfg_node_new(copy_state, device, str8_lit("view"));
        if(sized) { cfg_node_new(copy_state, cfg_node_new(copy_state, saved, str8_lit("weight")), rd_arrangement_weight_string(arena, p->weight)); }
        cfg_node_insert_child(copy_state, saved, saved->last, cfg_node_deep_copy(copy_state, view));
        continue;
      }
      CFG_Node *section = &cfg_nil_node;
      for(CFG_Node *c = view->first; c != &cfg_nil_node; c = c->next)
      {
        String8 skip[] = {str8_lit("section"), str8_lit("label"), str8_lit("section_hint_pending")};
        B32 skipped = 0;
        for(U64 k = 0; k < ArrayCount(skip) && !skipped; k++) { skipped = str8_match(c->string, skip[k], 0); }
        if(skipped) { continue; }
        uishell_sidebar_store_entry(copy_state, device, &section, str8_lit("section"), str8_lit("key"), key);
        cfg_node_insert_child(copy_state, section, section->last, cfg_node_deep_copy(copy_state, c));
      }
      if(sized)
      {
        uishell_sidebar_store_entry(copy_state, device, &section, str8_lit("section"), str8_lit("key"), key);
        cfg_node_new(copy_state, cfg_node_new(copy_state, section, str8_lit("weight")), rd_arrangement_weight_string(arena, p->weight));
      }
    }
  }
  scratch_end(scratch);
}
