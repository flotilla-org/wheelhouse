////////////////////////////////
//~ Weights

// Operations store weights with the config formatter's precision, so a
// committed and rebuilt arrangement lays out exactly as the one that was
// edited. The format is the core's (arrangement::quantize).
internal F32
rd_arrangement_quantize(F32 weight)
{
  return wheelhouse_arrangement_quantize(weight);
}

internal String8
rd_arrangement_weight_string(Arena *arena, F32 weight)
{
  String8 result = push_str8f(arena, "%f", weight);
  for(S32 digits = 7; digits <= 24 && (F32)f64_from_str8(result) != weight; digits += 1)
  { result = push_str8f(arena, "%.*f", digits, weight); }
  return result;
}

////////////////////////////////
//~ Tree

internal RD_ArrangementPanel *
rd_arrangement_panel_alloc(RD_Arrangement *arrangement)
{
  RD_ArrangementPanel *panel = push_array(arrangement->arena, RD_ArrangementPanel, 1);
  MemoryCopyStruct(panel, &rd_nil_arrangement_panel);
  panel->id = arrangement->next_id++;
  return panel;
}

// Pre-order, without growing the C call stack on long split chains.
internal RD_ArrangementPanel *
rd_arrangement_next(RD_ArrangementPanel *root, RD_ArrangementPanel *panel)
{
  if(panel->first != &rd_nil_arrangement_panel) { return panel->first; }
  for(RD_ArrangementPanel *p = panel; p != &rd_nil_arrangement_panel && p != root; p = p->parent)
  {
    if(p->next != &rd_nil_arrangement_panel) { return p->next; }
  }
  return &rd_nil_arrangement_panel;
}

internal void
rd_arrangement_insert(RD_ArrangementPanel *parent, RD_ArrangementPanel *prev, RD_ArrangementPanel *child)
{
  DLLInsert_NPZ(&rd_nil_arrangement_panel, parent->first, parent->last, prev, child, next, prev);
  child->parent = parent;
  parent->child_count += 1;
}

internal RD_ArrangementTab *
rd_arrangement_tab_from_view(RD_ArrangementPanel *panel, RD_ViewAddress view)
{
  for(RD_ArrangementTab *tab = panel->first_tab; tab != 0; tab = tab->next)
  {
    if(rd_view_address_match(tab->view, view)) { return tab; }
  }
  return 0;
}

internal void
rd_arrangement_push_tab(RD_Arrangement *arrangement, RD_ArrangementPanel *panel, RD_ArrangementTab *prev, RD_ViewAddress view)
{
  RD_ArrangementTab *tab = push_array(arrangement->arena, RD_ArrangementTab, 1);
  tab->view = view;
  DLLInsert_NPZ((RD_ArrangementTab *)0, panel->first_tab, panel->last_tab, prev, tab, next, prev);
  panel->tab_count += 1;
}

////////////////////////////////
//~ Documents

internal RD_Arrangement *
rd_arrangement_alloc(Arena *arena, RD_PanelOwner owner)
{
  RD_Arrangement *arrangement = push_array(arena, RD_Arrangement, 1);
  arrangement->arena = arena;
  arrangement->owner = owner;
  arrangement->root = &rd_nil_arrangement_panel;
  arrangement->root_axis = Axis2_Y;
  arrangement->next_id = 1;
  return arrangement;
}

internal RD_Arrangement *
rd_arrangement_copy(Arena *arena, RD_Arrangement *src)
{
  RD_Arrangement *dst = push_array(arena, RD_Arrangement, 1);
  MemoryCopyStruct(dst, src);
  dst->arena = arena;
  dst->root = &rd_nil_arrangement_panel;
  dst->first_removed = 0;
  for(RD_ArrangementTab *tab = src->first_removed; tab != 0; tab = tab->next)
  {
    RD_ArrangementTab *removed = push_array(arena, RD_ArrangementTab, 1);
    removed->view = tab->view;
    SLLStackPush(dst->first_removed, removed);
  }
  RD_ArrangementPanel *parent = &rd_nil_arrangement_panel;
  for(RD_ArrangementPanel *s = src->root; s != &rd_nil_arrangement_panel;)
  {
    RD_ArrangementPanel *d = push_array(arena, RD_ArrangementPanel, 1);
    MemoryCopyStruct(d, s);
    d->first = d->last = d->next = d->prev = d->parent = &rd_nil_arrangement_panel;
    d->child_count = d->tab_count = 0;
    d->first_tab = d->last_tab = 0;
    d->doc_id = push_str8_copy(arena, s->doc_id);
    for(RD_ArrangementTab *tab = s->first_tab; tab != 0; tab = tab->next)
    { rd_arrangement_push_tab(dst, d, d->last_tab, tab->view); }
    if(parent == &rd_nil_arrangement_panel) { dst->root = d; }
    else { rd_arrangement_insert(parent, parent->last, d); }
    if(s->first != &rd_nil_arrangement_panel)
    {
      s = s->first;
      parent = d;
      continue;
    }
    for(; s != src->root && s->next == &rd_nil_arrangement_panel; s = s->parent) { parent = parent->parent; }
    s = s == src->root ? &rd_nil_arrangement_panel : s->next;
  }
  return dst;
}

internal B32
rd_arrangement_match(RD_Arrangement *a, RD_Arrangement *b)
{
  if(!rd_panel_owner_match(a->owner, b->owner) || (a->root != &rd_nil_arrangement_panel && a->root_axis != b->root_axis)) { return 0; }
  RD_ArrangementPanel *p = a->root, *q = b->root;
  for(; p != &rd_nil_arrangement_panel && q != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p), q = rd_arrangement_next(b->root, q))
  {
    if(p->id != q->id || p->weight != q->weight || p->child_count != q->child_count || p->tab_count != q->tab_count ||
       (p->parent == &rd_nil_arrangement_panel) != (q->parent == &rd_nil_arrangement_panel) ||
       (p->parent != &rd_nil_arrangement_panel && p->parent->id != q->parent->id) ||
       !rd_view_address_match(p->selected, q->selected) || p->options_from != q->options_from ||
       !str8_match(p->doc_id, q->doc_id, 0) || p->restore_emptied != q->restore_emptied) { return 0; }
    for(RD_ArrangementTab *x = p->first_tab, *y = q->first_tab; x && y; x = x->next, y = y->next)
    { if(!rd_view_address_match(x->view, y->view)) { return 0; } }
  }
  return p == &rd_nil_arrangement_panel && q == &rd_nil_arrangement_panel;
}

////////////////////////////////
//~ Panel Addresses

internal B32
rd_panel_owner_match(RD_PanelOwner a, RD_PanelOwner b)
{
  return a.kind == b.kind && MemoryMatch(a.id.v, b.id.v, sizeof(a.id.v));
}

internal B32
rd_panel_address_match(RD_PanelAddress a, RD_PanelAddress b)
{
  return a.id == b.id && rd_panel_owner_match(a.owner, b.owner);
}

internal RD_PanelAddress
rd_panel_address(RD_PanelOwner owner, RD_PanelID id)
{
  RD_PanelAddress address = {owner, id};
  return address;
}

internal String8
rd_panel_owner_kind_name(RD_PanelOwnerKind kind)
{
  switch(kind)
  {
    default:{}break;
    case RD_PanelOwnerKind_Workspace:{return str8_lit("workspace");}break;
    case RD_PanelOwnerKind_Sidebar:{return str8_lit("sidebar");}break;
  }
  return str8_zero();
}

////////////////////////////////
//~ View Addresses

internal RD_ViewAddress
rd_view_address_make(RD_ViewOwnerKind owner, UIShell_WorkspaceId id, String8 key, RD_ViewInstance instance)
{
  RD_ViewAddress result = {0};
  if(owner == RD_ViewOwnerKind_Null || key.size == 0) { return result; }
  result.owner = owner;
  result.instance = instance;
  result.id = id;
  result.key = rd_view_key_hash(key);
  return result;
}

internal B32
rd_view_address_match(RD_ViewAddress a, RD_ViewAddress b)
{
  return MemoryMatchStruct(&a, &b);
}

internal B32
rd_view_address_is_nil(RD_ViewAddress view)
{
  return view.owner == RD_ViewOwnerKind_Null;
}

internal U128
rd_view_key_hash(String8 key)
{
  return u128_hash_from_str8(key);
}

internal String8
rd_view_owner_kind_name(RD_ViewOwnerKind kind)
{
  switch(kind)
  {
    default:{}break;
    case RD_ViewOwnerKind_Workspace:{return str8_lit("workspace");}break;
    case RD_ViewOwnerKind_Dashboard:{return str8_lit("dashboard");}break;
    case RD_ViewOwnerKind_Window:{return str8_lit("window");}break;
    case RD_ViewOwnerKind_Transient:{return str8_lit("transient");}break;
  }
  return str8_zero();
}

internal void
rd_view_address_list_push(Arena *arena, RD_ViewAddressList *list, RD_ViewAddress view)
{
  RD_ViewAddressNode *node = push_array(arena, RD_ViewAddressNode, 1);
  node->v = view;
  SLLQueuePush(list->first, list->last, node);
  list->count += 1;
}

////////////////////////////////
//~ The Renderer's Panel Tree

internal RD_PanelTree
rd_panel_tree_from_arrangement(Arena *arena, RD_PanelOwner owner, RD_Arrangement *arrangement)
{
  RD_PanelTree tree = {&rd_nil_panel_node, &rd_nil_panel_node};
  RD_ArrangementPanel *root = arrangement->root;
  RD_PanelNode *parent = &rd_nil_panel_node;
  Axis2 axis = arrangement->root_axis;
  for(RD_ArrangementPanel *s = root; s != &rd_nil_arrangement_panel;)
  {
    RD_PanelNode *d = push_array(arena, RD_PanelNode, 1);
    MemoryCopyStruct(d, &rd_nil_panel_node);
    d->address = rd_panel_address(owner, s->id);
    d->split_axis = axis;
    d->pct_of_parent = s->weight;
    d->tab_side = Side_Min;
    if(rd_panel_presentation_hook)
    {
      RD_PanelPresentation presentation = rd_panel_presentation_hook(d->address);
      if(presentation.focused) { tree.focused = d; }
      if(presentation.tabs_on_bottom) { d->tab_side = Side_Max; }
    }
    for(RD_ArrangementTab *tab = s->first_tab; tab != 0; tab = tab->next)
    {
      rd_view_address_list_push(arena, &d->tabs, tab->view);
      if(rd_view_address_match(tab->view, s->selected)) { d->selected_tab = tab->view; }
    }
    d->parent = parent;
    if(parent == &rd_nil_panel_node) { tree.root = d; }
    else
    {
      DLLPushBack_NPZ(&rd_nil_panel_node, parent->first, parent->last, d, next, prev);
      parent->child_count += 1;
    }
    if(s->first != &rd_nil_arrangement_panel)
    {
      s = s->first;
      parent = d;
      axis = axis2_flip(axis);
      continue;
    }
    for(; s != root && s->next == &rd_nil_arrangement_panel; s = s->parent)
    {
      parent = parent->parent;
      axis = axis2_flip(axis);
    }
    s = s == root ? &rd_nil_arrangement_panel : s->next;
  }
  return tree;
}

internal RD_PanelNodeRec
rd_panel_node_rec__depth_first(RD_PanelNode *root, RD_PanelNode *panel, U64 sib_off, U64 child_off)
{
  RD_PanelNodeRec rec = {&rd_nil_panel_node};
  if(*MemberFromOffset(RD_PanelNode **, panel, child_off) != &rd_nil_panel_node)
  {
    rec.next = *MemberFromOffset(RD_PanelNode **, panel, child_off);
    rec.push_count += 1;
  }
  else for(RD_PanelNode *p = panel; p != &rd_nil_panel_node && p != root; p = p->parent, rec.pop_count += 1)
  {
    if(*MemberFromOffset(RD_PanelNode **, p, sib_off) != &rd_nil_panel_node)
    {
      rec.next = *MemberFromOffset(RD_PanelNode **, p, sib_off);
      break;
    }
  }
  return rec;
}

internal RD_PanelNode *
rd_panel_node_from_id(RD_PanelNode *root, RD_PanelID id)
{
  for(RD_PanelNode *p = root; p != &rd_nil_panel_node && id != 0; p = rd_panel_node_rec__depth_first_pre(root, p).next)
  {
    if(p->address.id == id) { return p; }
  }
  return &rd_nil_panel_node;
}

internal Rng2F32
rd_target_rect_from_panel_node_child(Rng2F32 parent_rect, RD_PanelNode *parent, RD_PanelNode *panel)
{
  Rng2F32 rect = parent_rect;
  if(parent != &rd_nil_panel_node)
  {
    Vec2F32 parent_rect_size = dim_2f32(parent_rect);
    Axis2 axis = parent->split_axis;
    rect.p1.v[axis] = rect.p0.v[axis];
    for(RD_PanelNode *child = parent->first; child != &rd_nil_panel_node; child = child->next)
    {
      rect.p1.v[axis] += parent_rect_size.v[axis] * child->pct_of_parent;
      if(child == panel)
      {
        break;
      }
      rect.p0.v[axis] = rect.p1.v[axis];
    }
  }
  rect.x0 = round_f32(rect.x0);
  rect.x1 = round_f32(rect.x1);
  rect.y0 = round_f32(rect.y0);
  rect.y1 = round_f32(rect.y1);
  return rect;
}

internal Rng2F32
rd_target_rect_from_panel_node(Rng2F32 root_rect, RD_PanelNode *root, RD_PanelNode *panel)
{
  Temp scratch = scratch_begin(0, 0);

  // rjf: count ancestors
  U64 ancestor_count = 0;
  for(RD_PanelNode *p = panel->parent; p != &rd_nil_panel_node; p = p->parent)
  {
    ancestor_count += 1;
  }

  // rjf: gather ancestors
  RD_PanelNode **ancestors = push_array(scratch.arena, RD_PanelNode *, ancestor_count);
  {
    U64 ancestor_idx = 0;
    for(RD_PanelNode *p = panel->parent; p != &rd_nil_panel_node; p = p->parent)
    {
      ancestors[ancestor_idx] = p;
      ancestor_idx += 1;
    }
  }

  // rjf: go from highest ancestor => panel and calculate rect
  Rng2F32 parent_rect = root_rect;
  for(S64 ancestor_idx = (S64)ancestor_count-1;
      0 <= ancestor_idx && ancestor_idx < ancestor_count;
      ancestor_idx -= 1)
  {
    RD_PanelNode *ancestor = ancestors[ancestor_idx];
    RD_PanelNode *parent = ancestor->parent;
    if(parent != &rd_nil_panel_node)
    {
      parent_rect = rd_target_rect_from_panel_node_child(parent_rect, parent, ancestor);
    }
  }

  // rjf: calculate final rect
  Rng2F32 rect = rd_target_rect_from_panel_node_child(parent_rect, panel->parent, panel);

  scratch_end(scratch);
  return rect;
}

////////////////////////////////
//~ Queries

internal RD_ArrangementPanel *
rd_arrangement_panel_from_id(RD_Arrangement *arrangement, RD_PanelID id)
{
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel && id != 0; p = rd_arrangement_next(arrangement->root, p))
  {
    if(p->id == id) { return p; }
  }
  return &rd_nil_arrangement_panel;
}

internal RD_ArrangementPanel *
rd_arrangement_panel_from_view(RD_Arrangement *arrangement, RD_ViewAddress view)
{
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel && !rd_view_address_is_nil(view); p = rd_arrangement_next(arrangement->root, p))
  {
    if(rd_arrangement_tab_from_view(p, view)) { return p; }
  }
  return &rd_nil_arrangement_panel;
}

// Axes alternate by depth.
internal Axis2
rd_arrangement_split_axis(RD_Arrangement *arrangement, RD_ArrangementPanel *panel)
{
  Axis2 axis = arrangement->root_axis;
  for(RD_ArrangementPanel *p = panel->parent; p != &rd_nil_arrangement_panel; p = p->parent) { axis = axis2_flip(axis); }
  return axis;
}

// As rd_target_rect_from_panel_node_child.
internal Rng2F32
rd_arrangement_child_rect(Rng2F32 parent_rect, Axis2 axis, RD_ArrangementPanel *parent, RD_ArrangementPanel *panel)
{
  Rng2F32 rect = parent_rect;
  if(parent != &rd_nil_arrangement_panel)
  {
    F32 size = dim_2f32(parent_rect).v[axis];
    rect.p1.v[axis] = rect.p0.v[axis];
    for(RD_ArrangementPanel *child = parent->first; child != &rd_nil_arrangement_panel; child = child->next)
    {
      rect.p1.v[axis] += size * child->weight;
      if(child == panel) { break; }
      rect.p0.v[axis] = rect.p1.v[axis];
    }
  }
  rect.x0 = round_f32(rect.x0);
  rect.x1 = round_f32(rect.x1);
  rect.y0 = round_f32(rect.y0);
  rect.y1 = round_f32(rect.y1);
  return rect;
}

// As rd_target_rect_from_panel_node: the root's children divide the area
// unrounded, and each level below divides its rounded parent.
internal Rng2F32
rd_arrangement_rect(RD_Arrangement *arrangement, Rng2F32 area, RD_ArrangementPanel *panel)
{
  if(panel == &rd_nil_arrangement_panel) { return r2f32p(0, 0, 0, 0); }
  Temp scratch = scratch_begin(0, 0);
  U64 depth = 0;
  for(RD_ArrangementPanel *p = panel->parent; p != &rd_nil_arrangement_panel; p = p->parent) { depth += 1; }
  RD_ArrangementPanel **path = push_array(scratch.arena, RD_ArrangementPanel *, depth+1);
  {
    U64 i = depth;
    for(RD_ArrangementPanel *p = panel; p != &rd_nil_arrangement_panel; p = p->parent) { path[i--] = p; }
  }
  Rng2F32 rect = area;
  Axis2 axis = arrangement->root_axis;
  if(depth == 0) { rect = rd_arrangement_child_rect(area, axis, &rd_nil_arrangement_panel, panel); }
  for(U64 i = 1; i <= depth; i += 1)
  {
    rect = rd_arrangement_child_rect(rect, axis, path[i-1], path[i]);
    axis = axis2_flip(axis);
  }
  scratch_end(scratch);
  return rect;
}

internal WheelhouseEditDoc rd_core_doc_from_arrangement(Arena *arena, RD_Arrangement *arrangement);

// The live copy's own links and counts are checked here; the document's
// rules are the core's (arrangement::problem).
internal String8
rd_arrangement_problem(Arena *arena, RD_Arrangement *arrangement)
{
  String8 result = str8_zero();
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    U64 children = 0, tabs = 0;
    for(RD_ArrangementPanel *c = p->first; c != &rd_nil_arrangement_panel; c = c->next)
    {
      if(c->parent != p) { return push_str8f(arena, "panel %I64u's child %I64u has another parent", p->id, c->id); }
      children += 1;
    }
    if(children != p->child_count) { return push_str8f(arena, "panel %I64u miscounts its children", p->id); }
    for(RD_ArrangementTab *tab = p->first_tab; tab != 0; tab = tab->next)
    {
      if(rd_view_address_is_nil(tab->view)) { return push_str8f(arena, "panel %I64u has a tab with no View", p->id); }
      tabs += 1;
    }
    if(tabs != p->tab_count) { return push_str8f(arena, "panel %I64u miscounts its tabs", p->id); }
  }
  Temp scratch = scratch_begin(&arena, 1);
  WheelhouseEditDoc doc = rd_core_doc_from_arrangement(scratch.arena, arrangement);
  char *problem = 0, *error = 0;
  wheelhouse_arrangement_problem(&doc, &problem, &error);
  scratch_end(scratch);
  if(problem) { result = push_str8_copy(arena, str8_cstring(problem)); }
  if(error) { result = push_str8f(arena, "malformed arrangement: %s", error); }
  wheelhouse_string_free(problem);
  wheelhouse_string_free(error);
  return result;
}

////////////////////////////////
//~ Operations: the core's rules (#516)
//
// The rules are the core's (crates/wheelhouse-core/src/arrangement.rs).
// Each operation writes the arrangement as the core's edit document, applies
// one edit there, and takes the edited document back into the same
// arrangement: a panel that stays keeps its node, so a pointer to it stays
// good across an edit, as it did when the rules were C, and a panel the edit
// takes away is left unlinked and empty. A tab is its View Address, copied
// whole.

StaticAssert(sizeof(RD_ViewAddress) == WHEELHOUSE_TAB_BYTES, rd_view_address_is_a_core_tab);

internal WheelhouseTab
rd_core_tab(RD_ViewAddress view)
{
  WheelhouseTab tab;
  MemoryCopy(tab.bytes, &view, sizeof(view));
  return tab;
}

internal RD_ViewAddress
rd_view_from_core_tab(const WheelhouseTab *tab)
{
  RD_ViewAddress view;
  MemoryCopy(&view, tab->bytes, sizeof(view));
  return view;
}

// The arrangement as the core's edit document, in `arena`.
internal WheelhouseEditDoc
rd_core_doc_from_arrangement(Arena *arena, RD_Arrangement *arrangement)
{
  WheelhouseEditDoc doc = {0};
  RD_ArrangementPanel *root = arrangement->root;
  doc.root_axis = arrangement->root_axis == Axis2_X ? WHEELHOUSE_AXIS_ROW : WHEELHOUSE_AXIS_COLUMN;
  doc.next_id = arrangement->next_id;
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p)) { doc.panel_count += 1; }
  WheelhouseEditPanel *panels = push_array(arena, WheelhouseEditPanel, doc.panel_count);
  // The indices of the panel's ancestors, as the walk goes down and up.
  U64 *ancestors = push_array(arena, U64, doc.panel_count+1);
  U64 depth = 0, n = 0;
  for(RD_ArrangementPanel *s = root; s != &rd_nil_arrangement_panel;)
  {
    WheelhouseEditPanel *d = &panels[n];
    d->parent = depth == 0 ? WHEELHOUSE_NONE : ancestors[depth-1];
    d->id = s->id;
    d->weight = s->weight;
    d->restore_emptied = s->restore_emptied != 0;
    d->options_from = s->options_from;
    d->named.data = s->doc_id.str;
    d->named.len = s->doc_id.size;
    WheelhouseTab *tabs = push_array(arena, WheelhouseTab, s->tab_count);
    U64 t = 0;
    for(RD_ArrangementTab *tab = s->first_tab; tab != 0; tab = tab->next) { tabs[t++] = rd_core_tab(tab->view); }
    d->tabs = tabs;
    d->tab_count = t;
    d->has_selected = !rd_view_address_is_nil(s->selected);
    d->selected = rd_core_tab(s->selected);
    if(s->first != &rd_nil_arrangement_panel)
    {
      ancestors[depth++] = n++;
      s = s->first;
      continue;
    }
    n += 1;
    for(; s != root && s->next == &rd_nil_arrangement_panel; s = s->parent) { depth -= 1; }
    s = s == root ? &rd_nil_arrangement_panel : s->next;
  }
  doc.panels = panels;
  // Oldest first; the list is newest first.
  for(RD_ArrangementTab *tab = arrangement->first_removed; tab != 0; tab = tab->next) { doc.removed_count += 1; }
  WheelhouseTab *removed = push_array(arena, WheelhouseTab, doc.removed_count);
  U64 r = doc.removed_count;
  for(RD_ArrangementTab *tab = arrangement->first_removed; tab != 0; tab = tab->next) { removed[--r] = rd_core_tab(tab->view); }
  doc.removed = removed;
  return doc;
}

// Takes an edited document back into `arrangement`, reusing each panel's node
// by ID and its tab nodes.
internal void
rd_arrangement_take(RD_Arrangement *arrangement, const WheelhouseEditDoc *doc)
{
  Temp scratch = scratch_begin(&arrangement->arena, 1);
  RD_ArrangementPanel *root = arrangement->root;
  // The panels it has, by ID, in an open-addressed table.
  U64 old_count = 0;
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p)) { old_count += 1; }
  RD_ArrangementPanel **old = push_array(scratch.arena, RD_ArrangementPanel *, old_count);
  B8 *kept = push_array(scratch.arena, B8, old_count);
  U64 slot_count = 16;
  while(slot_count < 2*old_count) { slot_count *= 2; }
  U64 *slots = push_array(scratch.arena, U64, slot_count); // index+1; 0 is empty
  // Tab nodes to reuse.
  RD_ArrangementTab *pool = 0;
  {
    U64 i = 0;
    for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p), i += 1)
    {
      old[i] = p;
      U64 h = (p->id*0x9e3779b97f4a7c15ull) & (slot_count-1);
      while(slots[h] != 0) { h = (h+1) & (slot_count-1); }
      slots[h] = i+1;
    }
    for(U64 j = 0; j < old_count; j++)
    {
      for(RD_ArrangementTab *tab = old[j]->first_tab, *next = 0; tab != 0; tab = next) { next = tab->next; SLLStackPush(pool, tab); }
    }
    for(RD_ArrangementTab *tab = arrangement->first_removed, *next = 0; tab != 0; tab = next) { next = tab->next; SLLStackPush(pool, tab); }
  }
#define RD_TakeTab(out) do { if(pool) { (out) = pool; SLLStackPop(pool); } else { (out) = push_array(arrangement->arena, RD_ArrangementTab, 1); } MemoryZeroStruct(out); } while(0)
  RD_ArrangementPanel **nodes = push_array(scratch.arena, RD_ArrangementPanel *, doc->panel_count);
  for(U64 i = 0; i < doc->panel_count; i++)
  {
    const WheelhouseEditPanel *v = &doc->panels[i];
    RD_ArrangementPanel *node = 0;
    for(U64 h = (v->id*0x9e3779b97f4a7c15ull) & (slot_count-1); slots[h] != 0; h = (h+1) & (slot_count-1))
    {
      U64 j = slots[h]-1;
      if(old[j]->id == v->id && !kept[j]) { node = old[j]; kept[j] = 1; break; }
    }
    if(node == 0)
    {
      node = push_array(arrangement->arena, RD_ArrangementPanel, 1);
      MemoryCopyStruct(node, &rd_nil_arrangement_panel);
    }
    node->first = node->last = node->next = node->prev = node->parent = &rd_nil_arrangement_panel;
    node->child_count = 0;
    node->first_tab = node->last_tab = 0;
    node->tab_count = 0;
    node->id = v->id;
    node->weight = v->weight;
    node->options_from = v->options_from;
    node->restore_emptied = v->restore_emptied;
    String8 named = str8((U8 *)v->named.data, v->named.len);
    if(!str8_match(node->doc_id, named, 0)) { node->doc_id = push_str8_copy(arrangement->arena, named); }
    for(U64 t = 0; t < v->tab_count; t++)
    {
      RD_ArrangementTab *tab;
      RD_TakeTab(tab);
      tab->view = rd_view_from_core_tab(&v->tabs[t]);
      DLLPushBack_NPZ((RD_ArrangementTab *)0, node->first_tab, node->last_tab, tab, next, prev);
      node->tab_count += 1;
    }
    node->selected = v->has_selected ? rd_view_from_core_tab(&v->selected) : rd_nil_view_address;
    if(v->parent != WHEELHOUSE_NONE)
    {
      RD_ArrangementPanel *parent = nodes[v->parent];
      DLLPushBack_NPZ(&rd_nil_arrangement_panel, parent->first, parent->last, node, next, prev);
      node->parent = parent;
      parent->child_count += 1;
    }
    nodes[i] = node;
  }
  // A panel the edit took away is out of the tree, with no tabs, as a C
  // close left it.
  for(U64 j = 0; j < old_count; j++)
  {
    if(kept[j]) { continue; }
    RD_ArrangementPanel *gone = old[j];
    gone->first = gone->last = gone->next = gone->prev = gone->parent = &rd_nil_arrangement_panel;
    gone->child_count = gone->tab_count = 0;
    gone->first_tab = gone->last_tab = 0;
    gone->selected = rd_nil_view_address;
  }
  arrangement->root = doc->panel_count ? nodes[0] : &rd_nil_arrangement_panel;
  arrangement->root_axis = doc->root_axis == WHEELHOUSE_AXIS_ROW ? Axis2_X : Axis2_Y;
  arrangement->next_id = doc->next_id;
  arrangement->first_removed = 0;
  for(U64 r = 0; r < doc->removed_count; r++)
  {
    RD_ArrangementTab *tab;
    RD_TakeTab(tab);
    tab->view = rd_view_from_core_tab(&doc->removed[r]);
    SLLStackPush(arrangement->first_removed, tab);
  }
#undef RD_TakeTab
  scratch_end(scratch);
}

// What an edit did, as the operations report it.
typedef struct RD_ArrangementOutcome RD_ArrangementOutcome;
struct RD_ArrangementOutcome
{
  B32 applied;
  RD_PanelID panel;
  RD_PanelID source;
  RD_PanelID heir;
};

typedef struct RD_ArrangementRuleContext RD_ArrangementRuleContext;
struct RD_ArrangementRuleContext
{
  RD_ArrangementTabRule *shown;
};

internal U32
rd_arrangement_hidden(void *context, const WheelhouseTab *tab)
{
  RD_ArrangementRuleContext *rule = (RD_ArrangementRuleContext *)context;
  return !rule->shown(rd_view_from_core_tab(tab));
}

internal WheelhouseEdit
rd_arrangement_edit(U32 kind)
{
  WheelhouseEdit edit = {0};
  edit.kind = kind;
  edit.dir = WHEELHOUSE_DIR_NONE;
  return edit;
}

internal U32
rd_arrangement_core_dir(Dir2 dir)
{
  return dir == Dir2_Invalid ? WHEELHOUSE_DIR_NONE : (U32)dir;
}

internal void
rd_arrangement_edit_tab(U32 *has, WheelhouseTab *tab, RD_ViewAddress view)
{
  *has = !rd_view_address_is_nil(view);
  *tab = rd_core_tab(view);
}

// Applies `edit` to `arrangement` by the core's rules, `shown` (0: every
// tab) the tabs a settle counts.
internal RD_ArrangementOutcome
rd_arrangement_apply(RD_Arrangement *arrangement, WheelhouseEdit edit, RD_ArrangementTabRule *shown)
{
  RD_ArrangementOutcome result = {0};
  Temp scratch = scratch_begin(&arrangement->arena, 1);
  WheelhouseEditDoc doc = rd_core_doc_from_arrangement(scratch.arena, arrangement);
  RD_ArrangementRuleContext rule = {shown};
  char *error = 0;
  WheelhouseEditResult *edited = wheelhouse_arrangement_apply(&doc, &edit, shown ? rd_arrangement_hidden : 0, &rule, &error);
  if(edited)
  {
    rd_arrangement_take(arrangement, &edited->doc);
    result.applied = edited->outcome.applied != 0;
    result.panel = edited->outcome.panel;
    result.source = edited->outcome.source;
    result.heir = edited->outcome.heir;
    wheelhouse_edit_result_free(edited);
  }
  else
  {
    // Only a malformed document (a provider's panel name that isn't UTF-8,
    // say) gets here; the arrangement is as it was.
    fprintf(stderr, "Arrangement edit %u not applied: %s\n", edit.kind, error ? error : "unknown error");
    wheelhouse_string_free(error);
  }
  scratch_end(scratch);
  return result;
}

internal RD_PanelID
rd_arrangement_clear(RD_Arrangement *arrangement)
{
  return rd_arrangement_apply(arrangement, rd_arrangement_edit(WHEELHOUSE_EDIT_CLEAR), 0).panel;
}

internal RD_PanelID
rd_arrangement_split(RD_Arrangement *arrangement, RD_PanelID id, Dir2 dir)
{
  if(dir == Dir2_Invalid) { return 0; }
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_SPLIT);
  edit.panel = id;
  edit.dir = rd_arrangement_core_dir(dir);
  return rd_arrangement_apply(arrangement, edit, 0).panel;
}

internal RD_PanelID
rd_arrangement_add(RD_Arrangement *arrangement, RD_PanelID parent_id, F32 weight)
{
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_ADD);
  edit.panel = parent_id;
  edit.weight = weight;
  return rd_arrangement_apply(arrangement, edit, 0).panel;
}

internal B32
rd_arrangement_remove(RD_Arrangement *arrangement, RD_PanelID id)
{
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_REMOVE);
  edit.panel = id;
  return rd_arrangement_apply(arrangement, edit, 0).applied;
}

internal B32
rd_arrangement_closes_when_emptied(RD_ArrangementPanel *panel)
{
  return panel->doc_id.size == 0;
}

internal RD_PanelID
rd_arrangement_close(RD_Arrangement *arrangement, RD_PanelID id)
{
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_CLOSE);
  edit.panel = id;
  return rd_arrangement_apply(arrangement, edit, 0).heir;
}

internal B32
rd_arrangement_insert_tab(RD_Arrangement *arrangement, RD_ViewAddress view, RD_PanelID destination, RD_ViewAddress prev_view, B32 select)
{
  if(rd_view_address_is_nil(view)) { return 0; }
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_INSERT_TAB);
  rd_arrangement_edit_tab(&edit.has_tab, &edit.tab, view);
  rd_arrangement_edit_tab(&edit.has_after, &edit.after, prev_view);
  edit.panel = destination;
  edit.select = select != 0;
  return rd_arrangement_apply(arrangement, edit, 0).applied;
}

internal B32
rd_arrangement_move_tab(RD_Arrangement *arrangement, RD_ViewAddress view, RD_PanelID destination, RD_ViewAddress prev_view)
{
  return rd_arrangement_insert_tab(arrangement, view, destination, prev_view, 1);
}

internal B32
rd_arrangement_detach_tab(RD_Arrangement *arrangement, RD_ViewAddress view)
{
  // Nothing to do, and setting an arrangement asks every other one.
  if(rd_arrangement_panel_from_view(arrangement, view) == &rd_nil_arrangement_panel) { return 0; }
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_DETACH_TAB);
  rd_arrangement_edit_tab(&edit.has_tab, &edit.tab, view);
  return rd_arrangement_apply(arrangement, edit, 0).applied;
}

internal B32
rd_arrangement_settle(RD_Arrangement *arrangement, RD_PanelID panel, Dir2 dir, RD_ArrangementTabRule *shown, B32 may_close)
{
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_SETTLE);
  edit.panel = panel;
  edit.dir = rd_arrangement_core_dir(dir);
  edit.may_close = may_close != 0;
  return rd_arrangement_apply(arrangement, edit, shown).applied;
}

internal RD_ArrangementDrop
rd_arrangement_drop(RD_Arrangement *arrangement, RD_ViewAddress view, RD_PanelID destination, Dir2 dir, RD_ViewAddress prev_view, RD_ArrangementTabRule *shown)
{
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_DROP);
  rd_arrangement_edit_tab(&edit.has_tab, &edit.tab, view);
  rd_arrangement_edit_tab(&edit.has_after, &edit.after, prev_view);
  edit.panel = destination;
  edit.dir = rd_arrangement_core_dir(dir);
  RD_ArrangementOutcome outcome = rd_arrangement_apply(arrangement, edit, shown);
  RD_ArrangementDrop result = {outcome.panel, outcome.source, outcome.heir};
  return result;
}

internal B32
rd_arrangement_remove_tab(RD_Arrangement *arrangement, RD_ViewAddress view)
{
  if(rd_view_address_is_nil(view)) { return 0; }
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_REMOVE_TAB);
  rd_arrangement_edit_tab(&edit.has_tab, &edit.tab, view);
  return rd_arrangement_apply(arrangement, edit, 0).applied;
}

internal void
rd_arrangement_discard(RD_Arrangement *arrangement)
{
  rd_arrangement_apply(arrangement, rd_arrangement_edit(WHEELHOUSE_EDIT_DISCARD), 0);
}

internal B32
rd_arrangement_select(RD_Arrangement *arrangement, RD_PanelID id, RD_ViewAddress view)
{
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_SELECT);
  rd_arrangement_edit_tab(&edit.has_tab, &edit.tab, view);
  edit.panel = id;
  return rd_arrangement_apply(arrangement, edit, 0).applied;
}

internal B32
rd_arrangement_reorder(RD_Arrangement *arrangement, RD_PanelID id, RD_PanelID prev_id)
{
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_REORDER);
  edit.panel = id;
  edit.after_panel = prev_id;
  return rd_arrangement_apply(arrangement, edit, 0).applied;
}

internal void
rd_arrangement_resize(RD_Arrangement *arrangement, RD_PanelID id, F32 delta, F32 floor)
{
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_RESIZE);
  edit.panel = id;
  edit.delta = delta;
  edit.floor = floor;
  rd_arrangement_apply(arrangement, edit, 0);
}

internal void
rd_arrangement_equalize(RD_Arrangement *arrangement, RD_PanelID id)
{
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_EQUALIZE);
  edit.panel = id;
  rd_arrangement_apply(arrangement, edit, 0);
}

internal B32
rd_arrangement_readdress(RD_Arrangement *arrangement, RD_ViewAddress from, RD_ViewAddress to)
{
  if(rd_view_address_is_nil(from) || rd_view_address_match(from, to)) { return 0; }
  // Nothing to do, and setting an arrangement asks every other one.
  B32 has = 0;
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel && !has; p = rd_arrangement_next(arrangement->root, p))
  { has = rd_view_address_match(p->selected, from) || rd_arrangement_tab_from_view(p, from) != 0; }
  for(RD_ArrangementTab *tab = arrangement->first_removed; tab != 0 && !has; tab = tab->next) { has = rd_view_address_match(tab->view, from); }
  if(!has) { return 0; }
  WheelhouseEdit edit = rd_arrangement_edit(WHEELHOUSE_EDIT_READDRESS);
  rd_arrangement_edit_tab(&edit.has_tab, &edit.tab, from);
  edit.to = rd_core_tab(to);
  return rd_arrangement_apply(arrangement, edit, 0).applied;
}
