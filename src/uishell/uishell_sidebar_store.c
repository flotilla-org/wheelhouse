//- The Dashboard's sidebar arrangement in Andamento (ADR 0012; andamento.h,
// "the Dashboard's sidebar arrangement"; Andamento's
// docs/sidebar-design/sidebar-arrangement.md). Which sections are docked
// where, which float, and which are closed is Dashboard state: Andamento
// keeps it as one document in the dashboard record, and places, restores and
// flags sections against what the template and the local sections declare.
// Wheelhouse's live copy is the window's `control_views` (the dock) and its
// `floating_panels` (each Floating Panel an arrangement of its own), which
// docking edits as before (shell_arrangement.h). A tab's key is a
// sidebar_section View's `section`; any other View is one of this host's own,
// keyed by its `slot` ("u:<n>"), which Andamento stores but never places.
//
//   load    A window saved since has no sidebar in the presentation file,
//           only `sidebar_presentation`: the dock and Floating Panels are
//           built from andamento_sidebar_arrangement_acquire, with this
//           device's settings merged back in. A window with neither (a new
//           Dashboard) takes Andamento's placement (uishell_sidebar_store_load).
//   import  A window saved with `control_views` and no `sidebar_generation`
//           (before this step) is committed whole on the first sync:
//           Andamento's first commit adopts it, closing the declared sections
//           it lacks. The next save drops it, and its `section_positions`.
//   commit  Once no gesture is in flight, a document that changed is
//           committed whole at the generation last seen
//           (andamento_set_sidebar_arrangement). Closing a section is
//           committing without it. A stale commit is made again at the new
//           generation, as a workspace's is (uishell_workspace_store.c): only
//           Andamento's own reconciliation moves it. A section it placed
//           meanwhile, which the edit can't have, the recommit closes, so it
//           is restored: neither the edit nor what was declared is lost.
//   pull    When Andamento changed the document itself (it placed a section
//           newly declared, removed a duplicate, or another frontend
//           committed), the live copy is rebuilt from it. The container
//           region of local sections, which Andamento places while there are
//           none (before Wheelhouse first publishes them), is not shown once
//           they exist: they are, in its place.
//   restore, reset   The Sections menu's Restore and Reset To Default Panel
//           Layout are andamento_sidebar_restore_section and
//           andamento_sidebar_reset, then a pull.
//
// What stays here needs pixels or this host's config: the docking checker
// (shell_docking.h: a section never docks inside a child Workspace level); a
// section a safety restore moved (`section_hint_pending`,
// rd_dock_restore_window), which a commit leaves out and then restores by its
// hints; and the labels KDL titles give section Views
// (uishell_sidebar_store_labels).
//
// Andamento's notes: a key that no longer resolves (template drift, ADR 0013)
// keeps its tab, flagged, and its View shows a placeholder the person can
// remove (sidebar_section's View UI); a closed one stays closed. PLACED,
// RESTORED and DUPLICATE need nothing shown: the pull shows their result.
//
// Device-local (Presentation State, in the presentation file):
//
//   sidebar_generation: 12
//   sidebar_presentation: {
//     panel: {id: "3" selected tabs_on_bottom}   a panel's Presentation State, by document ID
//     panel: {id: "f1.1" share: 0.5}             a Floating Panel's share of its host
//     section: {key: "attention" section_collapsed weight: 0.25}
//                                                a section View's own settings, and the
//                                                size content gave it
//     view: {terminal: {slot: "u:1" ...}}        a View of this host's own, whole
//   }
//
// A docked section the sidebar sizes from its content (no
// `sidebar_layout_sized`) is committed with weight 1: its size is this
// device's, kept by section key. Sizes set by dragging are the document's. A
// Floating Panel's panel IDs are its own arrangement's, so the document names
// them "f<n>.<id>", n counting Floating Panels from 1.

internal String8 uishell_sidebar_section_key(CFG_Node *view);
internal UIShell_SectionTitle *uishell_sidebar_section_title(UIShell_SidebarState *state, String8 key);
internal void uishell_sidebar_section_titles(UIShell_SidebarState *state);
internal B32 uishell_sidebar_section_container(UIShell_SidebarState *state, String8 key);
internal UIShell_SidebarDocks uishell_sidebar_docks(Arena *arena, CFG_Node *owner);

typedef struct UIShell_SidebarDoc UIShell_SidebarDoc;
struct UIShell_SidebarDoc
{
  AndamentoPanel *panels;
  U64 panel_count, floating_first;
  AndamentoTab *tabs;
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

// The window's sidebar arrangements: the dock first, then each Floating
// Panel in its host's order. *count_out of them.
internal RD_Arrangement **
uishell_sidebar_store_arrangements(Arena *arena, CFG_Node *window, U64 *count_out)
{
  CFG_Node *host = cfg_node_child_from_string(window, str8_lit("floating_panels"));
  U64 count = 1;
  for(CFG_Node *c = host->first; c != &cfg_nil_node; c = c->next) { count += rd_dock_floating_panel_from_cfg(c) == c; }
  RD_Arrangement **result = push_array(arena, RD_Arrangement *, count);
  result[0] = rd_arrangement_from_owner(arena, window, RD_DOCK_SIDEBAR_ROOT);
  U64 n = 1;
  for(CFG_Node *c = host->first; c != &cfg_nil_node; c = c->next)
  { if(rd_dock_floating_panel_from_cfg(c) == c) { result[n++] = rd_arrangement_from_cfg(arena, c); } }
  *count_out = count;
  return result;
}

// A panel's ID in the document: the dock's as they are, Floating Panel n's
// as "f<n>.<id>".
internal String8
uishell_sidebar_store_panel_id(Arena *arena, U64 arrangement, RD_PanelID id)
{
  return arrangement ? push_str8f(arena, "f%I64u.%I64u", arrangement, id) : push_str8f(arena, "%I64u", id);
}

// The arrangement's own ID in a document ID ("3", or "f1.3"); 0 for one
// Wheelhouse didn't make.
internal RD_PanelID
uishell_sidebar_store_local_id(String8 id)
{
  if(id.size && id.str[0] == 'f')
  {
    U64 dot = str8_find_needle(id, 0, str8_lit("."), 0);
    id = dot < id.size ? str8_skip(id, dot+1) : str8_zero();
  }
  return id.size && id.size < 18 && str8_is_integer(id, 10) ? u64_from_str8(id, 10) : 0;
}

// Gives each View of this host's own (anything but a section) a "u:<n>" key
// no other tab of the sidebar has. Returns whether any changed.
internal B32
uishell_sidebar_store_assign_keys(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  U64 count = 0;
  RD_Arrangement **arrangements = uishell_sidebar_store_arrangements(scratch.arena, window, &count);
  String8List keys = {0};
  CFG_NodePtrList unkeyed = {0};
  U64 next = 1;
  for(U64 i = 0; i < count; i++)
  {
    RD_Arrangement *a = arrangements[i];
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    {
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        CFG_Node *view = cfg_node_from_id(t->view);
        String8 key = uishell_sidebar_store_key(view);
        B32 seen = 0;
        for(String8Node *k = keys.first; k && !seen; k = k->next) { seen = str8_match(k->string, key, 0); }
        if(!str8_match(view->string, str8_lit("sidebar_section"), 0) && rd_dock_view_from_name(view->string) &&
           (!uishell_store_user_key_valid(key) || seen))
        { cfg_node_ptr_list_push(scratch.arena, &unkeyed, view); continue; }
        str8_list_push(scratch.arena, &keys, key);
        next = Max(next, uishell_store_user_number(key)+1);
      }
    }
  }
  for(CFG_NodePtrNode *n = unkeyed.first; n; n = n->next)
  { uishell_store_set_setting(n->v, str8_lit("slot"), push_str8f(scratch.arena, "u:%I64u", next++)); }
  scratch_end(scratch);
  return unkeyed.count != 0;
}

//- Documents

// The window's sidebar as a document: the dock's panels, then each Floating
// Panel's, in preorder. With `declared_only` (importing), a section the
// snapshot doesn't declare is left out, as Andamento would refuse it.
internal UIShell_SidebarDoc
uishell_sidebar_store_doc(Arena *arena, UIShell_SidebarState *state, CFG_Node *window, B32 declared_only)
{
  UIShell_SidebarDoc doc = {0};
  U64 count = 0;
  RD_Arrangement **arrangements = uishell_sidebar_store_arrangements(arena, window, &count);
  U64 panels = 0, tabs = 0;
  for(U64 i = 0; i < count; i++)
  {
    RD_Arrangement *a = arrangements[i];
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    { panels += 1; tabs += p->tab_count; }
  }
  doc.panels = push_array(arena, AndamentoPanel, panels);
  doc.tabs = push_array(arena, AndamentoTab, tabs);
  doc.keys = push_array(arena, String8, tabs);
  doc.pending = push_array(arena, String8, tabs);
  RD_ArrangementPanel **order = push_array(arena, RD_ArrangementPanel *, panels);
  B32 manual = uishell_sidebar_store_manual(window);
  for(U64 i = 0; i < count; i++)
  {
    RD_Arrangement *a = arrangements[i];
    U64 first = doc.panel_count;
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    {
      // A panel a safety restore emptied (`section_hint_cleanup`) goes with
      // the section it lost, unless its split would be left with none.
      if(p->first == &rd_nil_arrangement_panel && p != a->root && p->parent->child_count > 1 &&
         cfg_node_child_from_string(cfg_node_from_id(p->cfg), str8_lit("section_hint_cleanup")) != &cfg_nil_node)
      {
        B32 keeps = 0;
        for(RD_ArrangementTab *t = p->first_tab; t && !keeps; t = t->next)
        { keeps = cfg_node_child_from_string(cfg_node_from_id(t->view), str8_lit("section_hint_pending")) == &cfg_nil_node; }
        if(!keeps)
        {
          for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
          { doc.pending[doc.pending_count++] = uishell_sidebar_store_key(cfg_node_from_id(t->view)); }
          continue;
        }
      }
      U64 index = doc.panel_count++;
      order[index] = p;
      AndamentoPanel *out = &doc.panels[index];
      out->parent = ANDAMENTO_NONE;
      for(U64 j = index; j > first && out->parent == ANDAMENTO_NONE; j--) { if(order[j-1] == p->parent) { out->parent = j-1; } }
      out->id = uishell_sidebar_text(uishell_sidebar_store_panel_id(arena, i, p->id));
      // A root's weight means nothing to Andamento, and the size the sidebar
      // gives a docked section from its content is this device's.
      F32 weight = p->weight;
      if(p == a->root || (i == 0 && !manual && uishell_sidebar_store_content_sized(a, p))) { weight = 1.f; }
      out->weight = (weight > 0 && weight < 1e30f) ? f64_from_str8(rd_arrangement_weight_string(arena, weight)) : 0.01;
      out->selected = ANDAMENTO_NONE;
      if(p->first != &rd_nil_arrangement_panel)
      {
        out->kind = ANDAMENTO_PANEL_SPLIT;
        out->axis = rd_arrangement_split_axis(a, p) == Axis2_X ? ANDAMENTO_AXIS_ROW : ANDAMENTO_AXIS_COLUMN;
        continue;
      }
      out->kind = ANDAMENTO_PANEL_TABS;
      out->first_tab = doc.tab_count;
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        CFG_Node *view = cfg_node_from_id(t->view);
        String8 key = uishell_sidebar_store_key(view);
        // A View with no key is corrupt saved state, shown as unavailable
        // and never committed.
        if(!key.size) { continue; }
        B32 section = str8_match(view->string, str8_lit("sidebar_section"), 0);
        if(declared_only && section && state->snapshot && !uishell_sidebar_section_title(state, key)) { continue; }
        if(cfg_node_child_from_string(view, str8_lit("section_hint_pending")) != &cfg_nil_node)
        { doc.pending[doc.pending_count++] = key; continue; }
        if(t->view == p->selected) { out->selected = doc.tab_count-out->first_tab; }
        doc.keys[doc.tab_count] = key;
        doc.tabs[doc.tab_count].slot = uishell_sidebar_text(key);
        doc.tab_count += 1;
      }
      out->tab_count = doc.tab_count-out->first_tab;
    }
    if(i == 0) { doc.floating_first = doc.panel_count; }
  }
  return doc;
}

// A document as one hash: its panels, tab keys and what it leaves pending.
internal U64
uishell_sidebar_store_hash(AndamentoPanel *panels, U64 panel_count, U64 floating_first, String8 *keys, U64 tab_count,
                           String8 *pending, U64 pending_count)
{
  U64 hash = 5381*33 + floating_first;
  for(U64 i = 0; i < panel_count; i++)
  {
    AndamentoPanel *p = &panels[i];
    U64 weight_bits = 0;
    MemoryCopy(&weight_bits, &p->weight, sizeof(weight_bits));
    hash = uishell_store_hash_text(hash, uishell_sidebar_string(p->id));
    hash = hash*33 + p->parent;
    hash = hash*33 + weight_bits;
    hash = hash*33 + p->kind*7 + (p->kind == ANDAMENTO_PANEL_SPLIT ? p->axis : 0);
    hash = hash*33 + (p->kind == ANDAMENTO_PANEL_TABS ? p->first_tab : 0);
    hash = hash*33 + p->tab_count;
    hash = hash*33 + p->selected;
  }
  for(U64 i = 0; i < tab_count; i++) { hash = uishell_store_hash_text(hash, keys[i]); }
  for(U64 i = 0; i < pending_count; i++) { hash = uishell_store_hash_text(hash*33 + 1, pending[i]); }
  return hash;
}

internal U64
uishell_sidebar_store_doc_hash(UIShell_SidebarDoc *doc)
{
  return uishell_sidebar_store_hash(doc->panels, doc->panel_count, doc->floating_first, doc->keys, doc->tab_count,
                                    doc->pending, doc->pending_count);
}

// Andamento's document, hashed as the window's would be.
internal U64
uishell_sidebar_store_stored_hash(Arena *arena, AndamentoArrangement *stored)
{
  AndamentoArrangementInfo info = {0};
  if(!andamento_arrangement_info(stored, &info)) { return 0; }
  AndamentoPanel *panels = push_array(arena, AndamentoPanel, info.panel_count);
  String8 *keys = push_array(arena, String8, info.tab_count);
  for(U64 i = 0; i < info.panel_count; i++) { andamento_arrangement_panel(stored, i, &panels[i]); }
  for(U64 i = 0; i < info.tab_count; i++)
  {
    AndamentoTab tab = {0};
    andamento_arrangement_tab(stored, i, &tab);
    keys[i] = uishell_sidebar_string(tab.slot);
  }
  return uishell_sidebar_store_hash(panels, info.panel_count, Min(andamento_arrangement_floating_first(stored), info.panel_count),
                                    keys, info.tab_count, 0, 0);
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
uishell_sidebar_store_notes(UIShell_SidebarState *state, AndamentoArrangement *arrangement)
{
  if(!state->sidebar_notes_arena) { state->sidebar_notes_arena = arena_alloc(); }
  arena_clear(state->sidebar_notes_arena);
  Arena *arena = state->sidebar_notes_arena;
  U64 count = arrangement ? andamento_arrangement_note_count(arrangement) : 0;
  state->sidebar_closed = push_array(arena, String8, count+1);
  state->sidebar_gone = push_array(arena, String8, count+1);
  state->sidebar_closed_count = state->sidebar_gone_count = 0;
  for(U64 i = 0; i < count; i++)
  {
    AndamentoSectionNote note = {0};
    if(!andamento_arrangement_note(arrangement, i, &note)) { continue; }
    String8 key = push_str8_copy(arena, uishell_sidebar_string(note.key));
    if(note.kind == ANDAMENTO_SECTION_CLOSED) { state->sidebar_closed[state->sidebar_closed_count++] = key; }
    if(note.kind == ANDAMENTO_SECTION_UNRESOLVED) { state->sidebar_gone[state->sidebar_gone_count++] = key; }
  }
}

// Reads Andamento's notes again: what resolves follows the snapshot, and the
// document's generation doesn't move when nothing in it did.
internal void
uishell_sidebar_store_read_notes(UIShell_SidebarState *state)
{
  AndamentoArrangement *stored = UIShell_StoreCall(state, andamento_sidebar_arrangement_acquire(state->core, 0));
  uishell_sidebar_store_notes(state, stored);
  andamento_arrangement_release(stored);
}

//- Building the live copy from Andamento's document

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

typedef struct UIShell_SidebarBuilt UIShell_SidebarBuilt;
struct UIShell_SidebarBuilt
{
  UIShell_SidebarBuilt *next;
  String8 id;
  RD_Arrangement *arrangement;
  RD_PanelID panel;
};

typedef struct UIShell_SidebarBuild UIShell_SidebarBuild;
struct UIShell_SidebarBuild
{
  Arena *arena;
  AndamentoTab *tabs;
  U64 tab_count;
  // Each tab's View (nil: none to show here).
  CFG_Node **views;
  // The sizes this device gave the dock's sections from their content, by key.
  String8 *size_keys;
  F32 *sizes;
  U64 size_count;
  B32 manual;
  // The panels made, by document ID.
  UIShell_SidebarBuilt *first_built;
};

// Fills the cleared `a` with the tree under `root`, reusing the panel nodes
// of `old_ids` by ID.
internal void
uishell_sidebar_store_fill(UIShell_SidebarBuild *b, RD_Arrangement *a, UIShell_StorePanel *root, B32 dock,
                           RD_PanelID *old_ids, CFG_ID *old_cfgs, U64 old_count)
{
  Arena *arena = b->arena;
  struct Pending { struct Pending *next; UIShell_StorePanel *panel; RD_PanelID parent; } *queue = 0, *queue_last = 0;
  U64 max_id = 0, panel_count = 0;
  for(U64 i = 0; i < old_count; i++) { max_id = Max(max_id, old_ids[i]); }
  {
    UIShell_StorePanel *stack[256];
    U64 depth = 0;
    stack[depth++] = root;
    while(depth)
    {
      UIShell_StorePanel *p = stack[--depth];
      panel_count += 1;
      max_id = Max(max_id, uishell_sidebar_store_local_id(p->id));
      for(UIShell_StorePanel *child = p->first; child && depth < ArrayCount(stack); child = child->next) { stack[depth++] = child; }
    }
  }
  struct Pending *start = push_array(arena, struct Pending, 1);
  start->panel = root;
  SLLQueuePush(queue, queue_last, start);
  U64 fresh = max_id+1;
  // Allocations stay clear of the IDs set below until they are all set.
  a->next_id = max_id + (1ull << 40);
  if(root->kind == ANDAMENTO_PANEL_SPLIT) { a->root_axis = root->axis == ANDAMENTO_AXIS_ROW ? Axis2_X : Axis2_Y; }
  U64 ids_used = 0;
  RD_PanelID *ids = push_array(arena, RD_PanelID, panel_count+1);
  RD_PanelID root_id = 0;
  for(struct Pending *item = queue; item; item = item->next)
  {
    UIShell_StorePanel *src = item->panel;
    F32 weight = (F32)src->weight;
    // A docked section's size that content decided stays this device's.
    if(dock && !b->manual && item->parent && item->parent == root_id && src->kind == ANDAMENTO_PANEL_TABS &&
       src->tab_count == 1 && src->first_tab < b->tab_count)
    {
      String8 key = uishell_sidebar_string(b->tabs[src->first_tab].slot);
      for(U64 i = 0; i < b->size_count; i++) { if(str8_match(b->size_keys[i], key, 0)) { weight = b->sizes[i]; break; } }
    }
    RD_PanelID panel_id = item->parent ? rd_arrangement_add(a, item->parent, weight) : a->root->id;
    RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(a, panel_id);
    RD_PanelID wanted = uishell_sidebar_store_local_id(src->id);
    for(U64 i = 0; i < ids_used && wanted; i++) { if(ids[i] == wanted) { wanted = 0; } }
    if(!wanted) { wanted = fresh++; }
    panel->id = wanted;
    if(!item->parent) { root_id = wanted; }
    ids[ids_used++] = wanted;
    // A panel keeps its node by ID, and the root keeps the root's. A leaf
    // Andamento lifted out of the root (it keeps the root's ID) takes the
    // root's Presentation State with it.
    if(!item->parent && old_count) { panel->cfg = old_cfgs[0]; }
    for(U64 i = 0; i < old_count && item->parent; i++)
    {
      if(old_ids[i] != wanted) { continue; }
      if(i == 0) { panel->options_from = old_cfgs[i]; }
      else { panel->cfg = old_cfgs[i]; }
    }
    UIShell_SidebarBuilt *built = push_array(arena, UIShell_SidebarBuilt, 1);
    built->id = src->id;
    built->arrangement = a;
    built->panel = wanted;
    SLLStackPush(b->first_built, built);
    for(UIShell_StorePanel *child = src->first; child; child = child->next)
    {
      struct Pending *pending = push_array(arena, struct Pending, 1);
      pending->panel = child;
      pending->parent = wanted;
      SLLQueuePush(queue, queue_last, pending);
    }
    if(src->kind != ANDAMENTO_PANEL_TABS) { continue; }
    CFG_ID prev = 0, selected = 0;
    for(U64 t = src->first_tab; t < src->first_tab+src->tab_count && t < b->tab_count; t++)
    {
      CFG_Node *view = b->views[t];
      if(view == &cfg_nil_node) { continue; }
      rd_arrangement_insert_tab(a, view->id, wanted, prev, 0);
      if(t-src->first_tab == src->selected) { selected = view->id; }
      prev = view->id;
    }
    rd_arrangement_select(a, wanted, selected);
  }
  a->next_id = fresh;
  for(U64 i = 0; i < ids_used; i++) { a->next_id = Max(a->next_id, ids[i]+1); }
}

// Replaces the window's dock and Floating Panels with Andamento's document.
// The View node of every key it still tabs is kept (runtime state is keyed by
// it), and so are the dock's panel nodes, by ID. New Views take their
// settings from the device-local `sidebar_presentation`, which goes. Reads
// Andamento's notes too. Returns whether Andamento had a document.
internal B32
uishell_sidebar_store_build(UIShell_SidebarState *state, CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  AndamentoArrangement *stored = UIShell_StoreCall(state, andamento_sidebar_arrangement_acquire(state->core, 0));
  AndamentoArrangementInfo info = {0};
  if(!stored || !andamento_arrangement_info(stored, &info))
  {
    andamento_arrangement_release(stored);
    scratch_end(scratch);
    return 0;
  }
  uishell_sidebar_store_notes(state, stored);
  U64 floating_first = Min(andamento_arrangement_floating_first(stored), info.panel_count);
  CFG_Node *device = cfg_node_child_from_string(window, str8_lit("sidebar_presentation"));
  UIShell_SidebarBuild b = {arena};
  b.manual = uishell_sidebar_store_manual(window);
  b.tab_count = info.tab_count;
  b.tabs = push_array(arena, AndamentoTab, info.tab_count+1);
  b.views = push_array(arena, CFG_Node *, info.tab_count+1);
  for(U64 i = 0; i < info.tab_count; i++) { andamento_arrangement_tab(stored, i, &b.tabs[i]); b.views[i] = &cfg_nil_node; }
  UIShell_StorePanel **all = info.panel_count ? uishell_store_panel_array(arena, stored, info.panel_count) : 0;
  U64 *tab_panel = push_array(arena, U64, info.tab_count+1);
  U64 floating_count = 0;
  for(U64 i = 0; i < info.panel_count; i++)
  {
    AndamentoPanel p = {0};
    andamento_arrangement_panel(stored, i, &p);
    floating_count += i >= floating_first && p.parent == ANDAMENTO_NONE;
    for(U64 t = p.first_tab; p.kind == ANDAMENTO_PANEL_TABS && t < p.first_tab+p.tab_count && t < info.tab_count; t++) { tab_panel[t] = i; }
  }

  // What the window has now: its dock and Floating Panels, every View in
  // them (strays too), and the sizes content gave its docked sections, as
  // config has them, else as this device saved them.
  UIShell_SidebarDocks docks = uishell_sidebar_docks(arena, window);
  RD_Arrangement *dock = docks.sidebar->arrangement;
  U64 old_count = 0;
  for(RD_ArrangementPanel *p = dock->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(dock->root, p)) { old_count++; }
  RD_PanelID *old_ids = push_array(arena, RD_PanelID, old_count+1);
  CFG_ID *old_cfgs = push_array(arena, CFG_ID, old_count+1);
  {
    U64 n = 0;
    for(RD_ArrangementPanel *p = dock->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(dock->root, p), n++)
    { old_ids[n] = p->id; old_cfgs[n] = p->cfg; }
  }
  U64 size_capacity = 0;
  for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next) { size_capacity++; }
  U64 dock_tabs = 0;
  for(RD_ArrangementPanel *p = dock->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(dock->root, p)) { dock_tabs += p->tab_count; }
  b.size_keys = push_array(arena, String8, size_capacity+dock_tabs+1);
  b.sizes = push_array(arena, F32, size_capacity+dock_tabs+1);
  if(dock->root != &rd_nil_arrangement_panel)
  {
    for(RD_ArrangementPanel *p = dock->root->first; p != &rd_nil_arrangement_panel; p = p->next)
    {
      if(p->first != &rd_nil_arrangement_panel || !(p->weight > 0 && p->weight < 1e30f)) { continue; }
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        b.size_keys[b.size_count] = uishell_sidebar_store_key(cfg_node_from_id(t->view));
        b.sizes[b.size_count++] = p->weight;
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
    b.size_keys[b.size_count] = section ? uishell_store_setting(c, str8_lit("key")) : uishell_store_setting(uishell_sidebar_store_entry_view(c), str8_lit("slot"));
    b.sizes[b.size_count++] = value;
  }
  // The shares the Floating Panels have now, by document ID.
  U64 current_count = 0;
  RD_Arrangement **current = uishell_sidebar_store_arrangements(arena, window, &current_count);
  String8 *current_ids = push_array(arena, String8, current_count);
  String8 *current_shares = push_array(arena, String8, current_count);
  for(U64 k = 1; k < current_count; k++)
  {
    current_ids[k] = uishell_sidebar_store_panel_id(arena, k, current[k]->root->id);
    current_shares[k] = push_str8_copy(arena, cfg_node_from_id(current[k]->root->cfg)->string);
  }

  // Each tab's View: the one showing its key, else a new one.
  B32 *taken = push_array(arena, B32, docks.views.count+1);
  RD_DockSavedView **sources = push_array(arena, RD_DockSavedView *, info.tab_count+1);
  for(U64 t = 0; t < info.tab_count; t++)
  {
    String8 key = uishell_sidebar_string(b.tabs[t].slot);
    U64 index = 0;
    for(RD_DockSavedView *v = docks.views.first; v; v = v->next, index++)
    {
      if(taken[index] || v->view == &cfg_nil_node || !str8_match(uishell_sidebar_store_key(v->view), key, 0)) { continue; }
      taken[index] = 1;
      sources[t] = v;
      b.views[t] = v->view;
      break;
    }
    if(b.views[t] != &cfg_nil_node) { continue; }
    // The container of local sections, placed while there were none (as
    // before they are first published), shows them in its place instead.
    if(b.tabs[t].gone && uishell_sidebar_section_container(state, key)) { continue; }
    if(str8_match(str8_prefix(key, 2), str8_lit("u:"), 0))
    {
      // A View of this host's own: as this device saved it. Another
      // device's shows a placeholder in a Floating Panel; the dock takes
      // only sections, so there it isn't shown.
      CFG_Node *saved = uishell_sidebar_store_device_view(device, key);
      if(saved != &cfg_nil_node) { b.views[t] = cfg_node_deep_copy(rd_state->cfg, saved); }
      else if(tab_panel[t] >= floating_first)
      {
        b.views[t] = rd_cfg_new_view_tab(&cfg_nil_node, str8_lit("placeholder"), str8_zero(), 0);
        uishell_store_set_setting(b.views[t], str8_lit("slot"), key);
        uishell_store_set_setting(b.views[t], str8_lit("content"), str8_lit("a View another device keeps"));
      }
      continue;
    }
    CFG_Node *view = cfg_node_new(rd_state->cfg, &cfg_nil_node, str8_lit("sidebar_section"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("section")), key);
    UIShell_SectionTitle *title = uishell_sidebar_section_title(state, key);
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("label")), title ? title->title : key);
    // This device's settings for it (its collapse).
    CFG_Node *entry = uishell_sidebar_store_device(device, str8_lit("section"), str8_lit("key"), key);
    for(CFG_Node *s = entry->first; s != &cfg_nil_node; s = s->next)
    {
      if(str8_match(s->string, str8_lit("key"), 0) || str8_match(s->string, str8_lit("weight"), 0)) { continue; }
      cfg_node_insert_child(rd_state->cfg, view, view->last, cfg_node_deep_copy(rd_state->cfg, s));
    }
    b.views[t] = view;
  }
  // A View moving between the dock and a Floating Panel, or out of a
  // Floating Panel that goes, is detached first: no save releases it.
  for(U64 t = 0; t < info.tab_count; t++)
  {
    RD_DockSavedView *v = sources[t];
    if(!v || !rd_dock_saved_view_is_tab(v)) { continue; }
    if(v->document != docks.sidebar || tab_panel[t] >= floating_first) { rd_arrangement_detach_tab(v->document->arrangement, v->view->id); }
  }

  // Floating Panels: each new, in the document's order, with its share of
  // the host as this device has it.
  RD_Arrangement **made = push_array(arena, RD_Arrangement *, floating_count+1);
  CFG_Node *host = floating_count ? cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("floating_panels")) : &cfg_nil_node;
  {
    U64 n = 0;
    for(U64 i = floating_first; i < info.panel_count; i++)
    {
      AndamentoPanel p = {0};
      andamento_arrangement_panel(stored, i, &p);
      if(p.parent != ANDAMENTO_NONE) { continue; }
      String8 id = uishell_sidebar_string(p.id);
      String8 share = uishell_store_setting(uishell_sidebar_store_device(device, str8_lit("panel"), str8_lit("id"), id), str8_lit("share"));
      for(U64 k = 1; k < current_count && !share.size; k++) { if(str8_match(current_ids[k], id, 0)) { share = current_shares[k]; } }
      F32 value = (F32)f64_from_str8(share);
      made[n] = rd_arrangement_new(arena, host, value > 0 && value < 1e30f ? share : str8_lit("1"));
      rd_arrangement_clear(made[n]);
      uishell_sidebar_store_fill(&b, made[n], all[i], 0, 0, 0, 0);
      n++;
    }
  }
  // The dock.
  if(floating_first == 0) { rd_arrangement_discard(dock); }
  else
  {
    rd_arrangement_clear(dock);
    uishell_sidebar_store_fill(&b, dock, all[0], 1, old_ids, old_cfgs, old_count);
  }
  // The new Floating Panels take their Views first, then the dock, then
  // the old Floating Panels go with what nothing took.
  for(U64 i = 0; i < floating_count; i++) { rd_arrangement_save(rd_state->cfg, made[i]); }
  rd_arrangement_save(rd_state->cfg, dock);
  for(RD_DockDocument *d = docks.documents.first; d; d = d->next)
  {
    if(d == docks.sidebar) { continue; }
    rd_arrangement_discard(d->arrangement);
    rd_arrangement_save(rd_state->cfg, d->arrangement);
  }
  // Strays nothing took (saved outside any panel) go too.
  {
    U64 index = 0;
    for(RD_DockSavedView *v = docks.views.first; v; v = v->next, index++)
    {
      if(taken[index] || rd_dock_saved_view_is_tab(v) || v->view == &cfg_nil_node || cfg_node_from_id(v->view->id) != v->view) { continue; }
      cfg_node_release(rd_state->cfg, v->view);
    }
  }
  host = cfg_node_child_from_string(window, str8_lit("floating_panels"));
  if(host != &cfg_nil_node && host->first == &cfg_nil_node) { cfg_node_release(rd_state->cfg, host); }

  // This device's Presentation State for each panel; a safety restore's
  // marks are done with.
  for(UIShell_SidebarBuilt *built = b.first_built; built; built = built->next)
  {
    CFG_Node *node = cfg_node_from_id(rd_arrangement_panel_from_id(built->arrangement, built->panel)->cfg);
    if(node == &cfg_nil_node) { continue; }
    CFG_Node *cleanup = cfg_node_child_from_string(node, str8_lit("section_hint_cleanup"));
    if(cleanup != &cfg_nil_node) { cfg_node_release(rd_state->cfg, cleanup); }
    CFG_Node *entry = uishell_sidebar_store_device(device, str8_lit("panel"), str8_lit("id"), built->id);
    for(CFG_Node *s = entry->first; s != &cfg_nil_node; s = s->next)
    {
      if(str8_match(s->string, str8_lit("id"), 0) || str8_match(s->string, str8_lit("share"), 0) ||
         cfg_node_child_from_string(node, s->string) != &cfg_nil_node) { continue; }
      cfg_node_insert_child(rd_state->cfg, node, node->last, cfg_node_deep_copy(rd_state->cfg, s));
    }
  }
  for(U64 t = 0; t < info.tab_count; t++)
  {
    CFG_Node *pending = cfg_node_child_from_string(b.views[t], str8_lit("section_hint_pending"));
    if(pending != &cfg_nil_node) { cfg_node_release(rd_state->cfg, pending); }
  }
  if(device != &cfg_nil_node) { cfg_node_release(rd_state->cfg, device); }
  uishell_store_set_setting(window, str8_lit("sidebar_generation"), push_str8f(arena, "%I64u", info.generation));
  state->sidebar_generation = info.generation;
  andamento_arrangement_release(stored);
  UIShell_SidebarDoc rebuilt = uishell_sidebar_store_doc(arena, state, window, 0);
  state->sidebar_hash = uishell_sidebar_store_doc_hash(&rebuilt);
  scratch_end(scratch);
  return 1;
}

internal B32
uishell_sidebar_store_pull(UIShell_SidebarState *state, CFG_Node *window)
{
  return uishell_sidebar_store_build(state, window);
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
    U64 count = 0;
    RD_Arrangement **arrangements = uishell_sidebar_store_arrangements(scratch.arena, window, &count);
    for(U64 k = 0; k < count; k++)
    {
      RD_Arrangement *a = arrangements[k];
      RD_PanelID *emptied = push_array(scratch.arena, RD_PanelID, 1);
      U64 emptied_count = 0;
      B32 changed = 0;
      for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
      {
        B32 lost = 0;
        for(RD_ArrangementTab *t = p->first_tab, *t_next = 0; t; t = t_next)
        {
          t_next = t->next;
          if(!str8_match(uishell_sidebar_store_key(cfg_node_from_id(t->view)), key, 0)) { continue; }
          rd_arrangement_remove_tab(a, t->view);
          lost = changed = 1;
        }
        if(lost && p->tab_count == 0 && p != a->root && !emptied_count) { emptied[emptied_count++] = p->id; }
      }
      for(U64 e = 0; e < emptied_count; e++) { rd_arrangement_remove(a, emptied[e]); }
      if(changed) { rd_arrangement_save(rd_state->cfg, a); }
    }
    scratch_end(scratch);
  }
}

//- Sync

// Builds the live copy from Andamento when there is none of this host's own
// yet: in a window saved since (its `sidebar_presentation`), or a new one. A
// window saved before this step keeps its sidebar for the first sync to
// import.
internal void
uishell_sidebar_store_load(UIShell_SidebarState *state, CFG_Node *window)
{
  if(!state->core || !state->window || window->id != state->window) { return; }
  B32 device = cfg_node_child_from_string(window, str8_lit("sidebar_presentation")) != &cfg_nil_node;
  B32 marked = cfg_node_child_from_string(window, str8_lit("sidebar_generation")) != &cfg_nil_node;
  B32 legacy = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT) != &cfg_nil_node ||
    cfg_node_child_from_string(window, str8_lit("floating_panels")) != &cfg_nil_node;
  if(device || (!marked && !legacy)) { uishell_sidebar_store_pull(state, window); }
}

// Commits `doc` at `expected`. A stale commit is made again at the
// generation that moved it, and what Andamento placed meanwhile, which `doc`
// can't have, is added to *placed_out (from `arena`): committing without it
// closed it, so the caller restores it. Returns Andamento's result.
internal uint32_t
uishell_sidebar_store_commit(Arena *arena, UIShell_SidebarState *state, UIShell_SidebarDoc *doc, U64 expected, U64 *generation_out,
                             String8List *placed_out)
{
  uint32_t result = ANDAMENTO_ARRANGEMENT_INVALID;
  char *error = 0;
  for(U32 attempt = 0; attempt < 2; attempt++)
  {
    andamento_string_free(error);
    error = 0;
    U64 generation = 0;
    result = UIShell_StoreCall(state, andamento_set_sidebar_arrangement(state->core, doc->panels, doc->panel_count, doc->floating_first,
                                                                        doc->tabs, doc->tab_count, expected, &generation, &error));
    state->sidebar_commits += 1;
    *generation_out = generation;
    if(result != ANDAMENTO_ARRANGEMENT_STALE) { break; }
    // Stale: Andamento reconciled what was declared since. The edit is
    // committed again at the new generation, keeping what it placed.
    AndamentoArrangement *now = UIShell_StoreCall(state, andamento_sidebar_arrangement_acquire(state->core, 0));
    AndamentoArrangementInfo info = {0};
    for(U64 t = 0; now && andamento_arrangement_info(now, &info) && t < info.tab_count; t++)
    {
      AndamentoTab tab = {0};
      if(!andamento_arrangement_tab(now, t, &tab) || !tab.placed) { continue; }
      String8 key = uishell_sidebar_string(tab.slot);
      if(!uishell_sidebar_store_listed(doc->keys, doc->tab_count, key)) { str8_list_push(arena, placed_out, push_str8_copy(arena, key)); }
    }
    andamento_arrangement_release(now);
    expected = generation;
  }
  if(result != ANDAMENTO_ARRANGEMENT_COMMITTED)
  { uishell_sidebar_set_error(state, error ? str8_cstring(error) : str8_lit("The sidebar's arrangement was not committed")); }
  andamento_string_free(error);
  return result;
}

// Each frame the sidebar observes its window: follows a document Andamento
// changed, then commits a sidebar that changed, once no gesture is in
// flight. `force` commits even then (the window is closing, or a menu
// command needs Andamento's document current). Moving a config node doesn't
// change the config gen (a reorder only moves panels), so the document is
// compared every frame: it is a handful of panels.
internal void
uishell_sidebar_store_sync(UIShell_SidebarState *state, CFG_Node *window, B32 force)
{
  if(!state->core || !state->window || window->id != state->window) { return; }
  if(!force && uishell_store_gesture_in_flight()) { return; }
  Temp scratch = scratch_begin(0, 0);
  B32 snapshot = state->snapshot_count != state->sidebar_snapshot_count;
  uishell_sidebar_store_load(state, window);
  B32 marked = cfg_node_child_from_string(window, str8_lit("sidebar_generation")) != &cfg_nil_node;
  B32 legacy = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT) != &cfg_nil_node ||
    cfg_node_child_from_string(window, str8_lit("floating_panels")) != &cfg_nil_node;
  // Andamento moved the document, and the live copy hasn't changed since:
  // it follows.
  char *error = 0;
  U64 generation = UIShell_StoreCall(state, andamento_sidebar_arrangement_generation(state->core, &error));
  andamento_string_free(error);
  B32 pulled = 0;
  if(marked && generation != state->sidebar_generation)
  {
    UIShell_SidebarDoc doc = uishell_sidebar_store_doc(scratch.arena, state, window, 0);
    if(uishell_sidebar_store_doc_hash(&doc) == state->sidebar_hash) { pulled = uishell_sidebar_store_pull(state, window); }
  }
  // What resolves follows the snapshot even when the document doesn't move.
  B32 notes = pulled || snapshot || !state->sidebar_synced;
  if(!pulled && notes) { uishell_sidebar_store_read_notes(state); }
  if(notes) { uishell_sidebar_store_drop_containers(state, window); }
  if(marked || legacy)
  {
    uishell_sidebar_store_assign_keys(window);
    UIShell_SidebarDoc doc = uishell_sidebar_store_doc(scratch.arena, state, window, !marked);
    U64 hash = uishell_sidebar_store_doc_hash(&doc);
    if(!marked || hash != state->sidebar_hash)
    {
      // At the generation last seen; importing, at Andamento's own.
      U64 expected = UIShell_StoreCall(state, andamento_sidebar_arrangement_generation(state->core, 0));
      if(marked)
      {
        expected = state->sidebar_generation;
        String8 saved = uishell_store_setting(window, str8_lit("sidebar_generation"));
        if(!expected && str8_is_integer(saved, 10)) { expected = u64_from_str8(saved, 10); }
      }
      U64 committed = 0;
      String8List restore = {0};
      state->sidebar_hash = hash;
      if(uishell_sidebar_store_commit(scratch.arena, state, &doc, expected, &committed, &restore) == ANDAMENTO_ARRANGEMENT_COMMITTED)
      {
        state->sidebar_generation = committed;
        uishell_store_set_setting(window, str8_lit("sidebar_generation"), push_str8f(scratch.arena, "%I64u", committed));
        // Andamento keeps the closed sections now.
        CFG_Node *inventory = cfg_node_child_from_string(window, str8_lit("section_positions"));
        if(inventory != &cfg_nil_node) { cfg_node_release(rd_state->cfg, inventory); }
        // A section a safety restore moved goes where its hints say, as does
        // one Andamento placed while this commit was stale.
        for(U64 i = 0; i < doc.pending_count; i++) { str8_list_push(scratch.arena, &restore, doc.pending[i]); }
        for(String8Node *n = restore.first; n; n = n->next)
        {
          U64 restored = 0;
          char *restore_error = 0;
          if(UIShell_StoreCall(state, andamento_sidebar_restore_section(state->core, uishell_sidebar_text(n->string), committed,
                                                                        &restored, &restore_error)) == ANDAMENTO_ARRANGEMENT_COMMITTED)
          { committed = restored; }
          andamento_string_free(restore_error);
        }
        // Whatever Andamento made of it (a section placed, a duplicate
        // removed; anything, importing) joins the live copy.
        AndamentoArrangement *now = UIShell_StoreCall(state, andamento_sidebar_arrangement_acquire(state->core, 0));
        B32 same = marked && now && !restore.node_count && uishell_sidebar_store_stored_hash(scratch.arena, now) ==
          uishell_sidebar_store_hash(doc.panels, doc.panel_count, doc.floating_first, doc.keys, doc.tab_count, 0, 0);
        if(same) { uishell_sidebar_store_notes(state, now); }
        andamento_arrangement_release(now);
        if(!same) { uishell_sidebar_store_build(state, window); }
      }
    }
    else if(!pulled && generation != state->sidebar_generation) { uishell_sidebar_store_pull(state, window); }
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
  uint32_t result = UIShell_StoreCall(state, andamento_sidebar_restore_section(state->core, uishell_sidebar_text(key), generation, 0, &error));
  andamento_string_free(error);
  if(result != ANDAMENTO_ARRANGEMENT_COMMITTED) { return 0; }
  uishell_sidebar_store_build(state, window);
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
  uint32_t result = UIShell_StoreCall(state, andamento_sidebar_reset(state->core, generation, 0, &error));
  uishell_sidebar_result(state, result == ANDAMENTO_ARRANGEMENT_COMMITTED, error);
  uishell_sidebar_store_build(state, window);
}

//- Labels

// KDL owns section titles: each section View's label follows its section's,
// checked once per snapshot or config change.
internal void
uishell_sidebar_store_labels(UIShell_SidebarState *state, CFG_Node *window)
{
  if(!state->snapshot || (state->labels_snapshot_count == state->snapshot_count && state->labels_cfg_gen == cfg_change_gen())) { return; }
  Temp scratch = scratch_begin(0, 0);
  U64 count = 0;
  RD_Arrangement **arrangements = uishell_sidebar_store_arrangements(scratch.arena, window, &count);
  for(U64 i = 0; i < count; i++)
  {
    RD_Arrangement *a = arrangements[i];
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    {
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        CFG_Node *view = cfg_node_from_id(t->view);
        if(!str8_match(view->string, str8_lit("sidebar_section"), 0)) { continue; }
        UIShell_SectionTitle *title = uishell_sidebar_section_title(state, uishell_sidebar_section_key(view));
        if(!title) { continue; }
        CFG_Node *label = cfg_node_child_from_string(view, str8_lit("label"));
        if(label != &cfg_nil_node && label->first == label->last && str8_match(label->first->string, title->title, 0)) { continue; }
        cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("label")), title->title);
      }
    }
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

// `window`'s copy as the presentation file holds it: once Andamento keeps
// the sidebar's arrangement, the dock, Floating Panels and closed sections
// are left out, and this device's part of them is `sidebar_presentation`.
internal void
uishell_sidebar_store_presentation(CFG_State *copy_state, CFG_Node *window, CFG_Node *copy)
{
  if(cfg_node_child_from_string(window, str8_lit("sidebar_generation")) == &cfg_nil_node ||
     cfg_node_child_from_string(window, str8_lit("sidebar_presentation")) != &cfg_nil_node) { return; }
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  String8 drop[] = {RD_DOCK_SIDEBAR_ROOT, str8_lit("control_views_split_x"), str8_lit("floating_panels"), str8_lit("section_positions")};
  for(U64 i = 0; i < ArrayCount(drop); i++)
  {
    for(CFG_Node *c = cfg_node_child_from_string(copy, drop[i]); c != &cfg_nil_node; c = cfg_node_child_from_string(copy, drop[i]))
    { cfg_node_release(copy_state, c); }
  }
  CFG_Node *device = cfg_node_new(copy_state, copy, str8_lit("sidebar_presentation"));
  B32 manual = uishell_sidebar_store_manual(window);
  U64 count = 0;
  RD_Arrangement **arrangements = uishell_sidebar_store_arrangements(arena, window, &count);
  for(U64 i = 0; i < count; i++)
  {
    RD_Arrangement *a = arrangements[i];
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    {
      CFG_Node *node = cfg_node_from_id(p->cfg), *entry = &cfg_nil_node;
      String8 id = uishell_sidebar_store_panel_id(arena, i, p->id);
      for(CFG_Node *c = node->first; c != &cfg_nil_node; c = c->next)
      {
        if(rd_arrangement_child_from_cfg(c) != RD_ArrangementChild_Other || str8_match(c->string, str8_lit("id"), 0) ||
           str8_match(c->string, str8_lit("section_hint_cleanup"), 0)) { continue; }
        uishell_sidebar_store_entry(copy_state, device, &entry, str8_lit("panel"), str8_lit("id"), id);
        cfg_node_insert_child(copy_state, entry, entry->last, cfg_node_deep_copy(copy_state, c));
      }
      // Where a Floating Panel floats among the others.
      if(i && p == a->root)
      {
        uishell_sidebar_store_entry(copy_state, device, &entry, str8_lit("panel"), str8_lit("id"), id);
        cfg_node_new(copy_state, cfg_node_new(copy_state, entry, str8_lit("share")), node->string);
      }
      B32 sized = i == 0 && !manual && uishell_sidebar_store_content_sized(a, p);
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        CFG_Node *view = cfg_node_from_id(t->view);
        String8 key = uishell_sidebar_store_key(view);
        if(!key.size) { continue; }
        if(!str8_match(view->string, str8_lit("sidebar_section"), 0))
        {
          CFG_Node *saved = cfg_node_new(copy_state, device, str8_lit("view"));
          if(sized) { cfg_node_new(copy_state, cfg_node_new(copy_state, saved, str8_lit("weight")), rd_arrangement_weight_string(arena, p->weight)); }
          CFG_Node *view_copy = cfg_node_deep_copy(copy_state, view);
          CFG_Node *selected = cfg_node_child_from_string(view_copy, str8_lit("selected"));
          if(selected != &cfg_nil_node) { cfg_node_release(copy_state, selected); }
          cfg_node_insert_child(copy_state, saved, saved->last, view_copy);
          continue;
        }
        CFG_Node *section = &cfg_nil_node;
        for(CFG_Node *c = view->first; c != &cfg_nil_node; c = c->next)
        {
          String8 skip[] = {str8_lit("section"), str8_lit("label"), str8_lit("selected"), str8_lit("section_hint_pending")};
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
  }
  scratch_end(scratch);
}
