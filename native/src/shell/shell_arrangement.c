////////////////////////////////
//~ Weights

// Operations store weights with the config formatter's precision, so a
// committed and rebuilt arrangement lays out exactly as the one that was
// edited.
internal F32
rd_arrangement_quantize(F32 weight)
{
  Temp scratch = scratch_begin(0, 0);
  F32 result = (F32)f64_from_str8(push_str8f(scratch.arena, "%f", weight));
  scratch_end(scratch);
  return result;
}

// A hand-edited panel may hold its whole parent despite siblings. Give the
// survivors equal weights when the normal denominator is not positive.
internal F32
rd_arrangement_remaining_weight(F32 weight, F32 removed, U64 count)
{
  Assert(count > 0); // Closing a panel always leaves a sibling.
  return rd_arrangement_quantize(removed < 1.f ? weight/(1.f-removed) : 1.f/Max(count, 1));
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

internal void
rd_arrangement_unlink(RD_Arrangement *arrangement, RD_ArrangementPanel *child)
{
  RD_ArrangementPanel *parent = child->parent;
  if(parent == &rd_nil_arrangement_panel) { arrangement->root = &rd_nil_arrangement_panel; }
  else
  {
    DLLRemove_NPZ(&rd_nil_arrangement_panel, parent->first, parent->last, child, next, prev);
    parent->child_count -= 1;
  }
  child->parent = child->next = child->prev = &rd_nil_arrangement_panel;
}

// `replacement` takes `panel`'s place, which leaves the tree.
internal void
rd_arrangement_replace(RD_Arrangement *arrangement, RD_ArrangementPanel *panel, RD_ArrangementPanel *replacement)
{
  RD_ArrangementPanel *parent = panel->parent, *prev = panel->prev;
  rd_arrangement_unlink(arrangement, panel);
  if(replacement->parent != &rd_nil_arrangement_panel || arrangement->root == replacement)
  { rd_arrangement_unlink(arrangement, replacement); }
  if(parent == &rd_nil_arrangement_panel) { arrangement->root = replacement; }
  else { rd_arrangement_insert(parent, prev, replacement); }
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

internal String8
rd_arrangement_problem(Arena *arena, RD_Arrangement *arrangement)
{
  RD_ArrangementPanel *root = arrangement->root;
  if(root != &rd_nil_arrangement_panel && (root->parent != &rd_nil_arrangement_panel || root->weight != 1.f))
  { return str8_lit("the root panel has a parent or a weight other than 1"); }
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p))
  {
    if(p->id == 0 || p->id >= arrangement->next_id) { return push_str8f(arena, "panel %I64u has an ID that is not allocated", p->id); }
    for(RD_ArrangementPanel *q = root; q != p; q = rd_arrangement_next(root, q))
    {
      if(q->id == p->id) { return push_str8f(arena, "two panels have ID %I64u", p->id); }
    }
    U64 children = 0;
    F32 total = 0;
    for(RD_ArrangementPanel *c = p->first; c != &rd_nil_arrangement_panel; c = c->next)
    {
      if(c->parent != p) { return push_str8f(arena, "panel %I64u's child %I64u has another parent", p->id, c->id); }
      if(!(c->weight >= 0)) { return push_str8f(arena, "panel %I64u has a negative weight", c->id); }
      children += 1;
      total += c->weight;
    }
    if(children != p->child_count) { return push_str8f(arena, "panel %I64u miscounts its children", p->id); }
    if(children == 1) { return push_str8f(arena, "panel %I64u splits into one panel", p->id); }
    if(children != 0 && abs_f32(total-1.f) > 0.00001f*children)
    { return push_str8f(arena, "panel %I64u's children weigh %f, not 1", p->id, total); }
    if(children != 0 && p->first_tab != 0) { return push_str8f(arena, "split panel %I64u has tabs", p->id); }
    U64 tabs = 0;
    B32 selected = rd_view_address_is_nil(p->selected);
    for(RD_ArrangementTab *tab = p->first_tab; tab != 0; tab = tab->next)
    {
      tabs += 1;
      selected |= rd_view_address_match(tab->view, p->selected);
      if(rd_view_address_is_nil(tab->view)) { return push_str8f(arena, "panel %I64u has a tab with no View", p->id); }
      for(RD_ArrangementPanel *q = root; q != &rd_nil_arrangement_panel; q = rd_arrangement_next(root, q))
      {
        for(RD_ArrangementTab *other = q->first_tab; other != 0; other = other->next)
        {
          if(other != tab && rd_view_address_match(other->view, tab->view)) { return push_str8f(arena, "panel %I64u's View has two tabs", p->id); }
        }
      }
    }
    if(tabs != p->tab_count) { return push_str8f(arena, "panel %I64u miscounts its tabs", p->id); }
    if(!selected) { return push_str8f(arena, "panel %I64u selects a View it does not have", p->id); }
  }
  return str8_zero();
}

////////////////////////////////
//~ Operations

// The tabs of `panel` and those under it go when the arrangement is set,
// unless a tab takes their Views back first.
internal void
rd_arrangement_remove_tabs(RD_Arrangement *arrangement, RD_ArrangementPanel *panel)
{
  for(RD_ArrangementPanel *p = panel; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(panel, p))
  {
    for(RD_ArrangementTab *tab = p->first_tab, *next = 0; tab != 0; tab = next)
    {
      next = tab->next;
      tab->prev = 0;
      SLLStackPush(arrangement->first_removed, tab);
    }
    p->first_tab = p->last_tab = 0;
    p->tab_count = 0;
    p->selected = rd_nil_view_address;
  }
}

internal RD_PanelID
rd_arrangement_clear(RD_Arrangement *arrangement)
{
  rd_arrangement_remove_tabs(arrangement, arrangement->root);
  arrangement->root = rd_arrangement_panel_alloc(arrangement);
  arrangement->root->weight = 1.f;
  return arrangement->root->id;
}

internal RD_PanelID
rd_arrangement_split(RD_Arrangement *arrangement, RD_PanelID id, Dir2 dir)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
  if(panel == &rd_nil_arrangement_panel || dir == Dir2_Invalid) { return 0; }
  Axis2 axis = axis2_from_dir2(dir);
  Side side = side_from_dir2(dir);
  RD_ArrangementPanel *parent = panel->parent;
  RD_ArrangementPanel *created = rd_arrangement_panel_alloc(arrangement);
  if(parent != &rd_nil_arrangement_panel && rd_arrangement_split_axis(arrangement, parent) == axis)
  {
    F32 scale = (F32)parent->child_count/(parent->child_count+1);
    created->weight = rd_arrangement_quantize(1.f/(parent->child_count+1));
    for(RD_ArrangementPanel *child = parent->first; child != &rd_nil_arrangement_panel; child = child->next)
    { child->weight = rd_arrangement_quantize(child->weight*scale); }
    rd_arrangement_insert(parent, side == Side_Max ? panel : panel->prev, created);
  }
  else
  {
    // The new split takes the panel's place and weight; a level deeper, the
    // axes alternate to the split's.
    RD_ArrangementPanel *split = rd_arrangement_panel_alloc(arrangement);
    split->weight = panel->weight;
    rd_arrangement_replace(arrangement, panel, split);
    if(parent == &rd_nil_arrangement_panel) { arrangement->root_axis = axis; }
    panel->weight = created->weight = 0.5f;
    rd_arrangement_insert(split, split->last, side == Side_Min ? created : panel);
    rd_arrangement_insert(split, split->last, side == Side_Min ? panel : created);
  }
  return created->id;
}

internal RD_PanelID
rd_arrangement_add(RD_Arrangement *arrangement, RD_PanelID parent_id, F32 weight)
{
  RD_ArrangementPanel *parent = rd_arrangement_panel_from_id(arrangement, parent_id);
  if(parent == &rd_nil_arrangement_panel)
  {
    if(parent_id != 0 || arrangement->root != &rd_nil_arrangement_panel) { return 0; }
    parent = arrangement->root = rd_arrangement_panel_alloc(arrangement);
    parent->weight = 1.f;
  }
  if(parent->first == &rd_nil_arrangement_panel && parent->first_tab != 0)
  {
    RD_ArrangementPanel *lifted = rd_arrangement_panel_alloc(arrangement);
    lifted->weight = 1.f;
    lifted->first_tab = parent->first_tab;
    lifted->last_tab = parent->last_tab;
    lifted->tab_count = parent->tab_count;
    lifted->selected = parent->selected;
    lifted->options_from = parent->id;
    parent->first_tab = parent->last_tab = 0;
    parent->tab_count = 0;
    parent->selected = rd_nil_view_address;
    rd_arrangement_insert(parent, &rd_nil_arrangement_panel, lifted);
  }
  RD_ArrangementPanel *created = rd_arrangement_panel_alloc(arrangement);
  if(weight > 0) { created->weight = rd_arrangement_quantize(weight); }
  else
  {
    // Malformed or zero weights still keep a share, so none becomes
    // unreachable.
    F32 total = 0;
    for(RD_ArrangementPanel *child = parent->first; child != &rd_nil_arrangement_panel; child = child->next)
    { total += Max(.01f, child->weight); }
    F32 fraction = 1.f/(parent->child_count+1);
    for(RD_ArrangementPanel *child = parent->first; child != &rd_nil_arrangement_panel; child = child->next)
    { child->weight = rd_arrangement_quantize((1-fraction)*Max(.01f, child->weight)/total); }
    created->weight = rd_arrangement_quantize(fraction);
  }
  rd_arrangement_insert(parent, parent->last, created);
  return created->id;
}

internal B32
rd_arrangement_remove(RD_Arrangement *arrangement, RD_PanelID id)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
  if(panel == &rd_nil_arrangement_panel || panel == arrangement->root) { return 0; }
  rd_arrangement_remove_tabs(arrangement, panel);
  for(;;)
  {
    RD_ArrangementPanel *parent = panel->parent;
    rd_arrangement_unlink(arrangement, panel);
    if(parent == arrangement->root || parent->first != &rd_nil_arrangement_panel || parent->first_tab != 0) { break; }
    panel = parent;
  }
  return 1;
}

internal RD_PanelID
rd_arrangement_close(RD_Arrangement *arrangement, RD_PanelID id)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
  RD_ArrangementPanel *parent = panel->parent;
  if(panel == &rd_nil_arrangement_panel || parent == &rd_nil_arrangement_panel) { return 0; }
  rd_arrangement_remove_tabs(arrangement, panel);
  RD_ArrangementPanel *heir = &rd_nil_arrangement_panel;
  if(parent->child_count == 2)
  {
    RD_ArrangementPanel *keep = panel == parent->first ? parent->last : parent->first;
    RD_ArrangementPanel *grandparent = parent->parent;
    for(heir = keep; heir->first != &rd_nil_arrangement_panel; heir = heir->first) {}
    rd_arrangement_unlink(arrangement, panel);
    if(grandparent == &rd_nil_arrangement_panel)
    {
      // The survivor becomes the root, keeping the axis it had.
      arrangement->root_axis = rd_arrangement_split_axis(arrangement, keep);
      rd_arrangement_replace(arrangement, parent, keep);
      keep->weight = 1.f;
    }
    else if(keep->first == &rd_nil_arrangement_panel)
    {
      keep->weight = rd_arrangement_quantize(parent->weight);
      rd_arrangement_replace(arrangement, parent, keep);
    }
    else
    {
      // A surviving split runs along its grandparent's axis, so its panels
      // join the grandparent in its place, sharing its weight.
      RD_ArrangementPanel *prev = parent->prev;
      F32 scale = parent->weight;
      rd_arrangement_unlink(arrangement, parent);
      for(RD_ArrangementPanel *child = keep->first, *next = &rd_nil_arrangement_panel; child != &rd_nil_arrangement_panel; child = next)
      {
        next = child->next;
        rd_arrangement_unlink(arrangement, child);
        child->weight = rd_arrangement_quantize(child->weight*scale);
        rd_arrangement_insert(grandparent, prev, child);
        prev = child;
      }
    }
  }
  else
  {
    heir = panel->prev != &rd_nil_arrangement_panel ? panel->prev : panel->next;
    for(; heir->first != &rd_nil_arrangement_panel; heir = heir->first) {}
    F32 removed = panel->weight;
    rd_arrangement_unlink(arrangement, panel);
    for(RD_ArrangementPanel *child = parent->first; child != &rd_nil_arrangement_panel; child = child->next)
    { child->weight = rd_arrangement_remaining_weight(child->weight, removed, parent->child_count); }
  }
  return heir->id;
}

// Takes `view`'s tab out of its panel, leaving the tab for the caller.
internal RD_ArrangementTab *
rd_arrangement_unlink_tab(RD_Arrangement *arrangement, RD_ViewAddress view)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_view(arrangement, view);
  RD_ArrangementTab *tab = rd_arrangement_tab_from_view(panel, view);
  if(tab)
  {
    DLLRemove_NPZ((RD_ArrangementTab *)0, panel->first_tab, panel->last_tab, tab, next, prev);
    panel->tab_count -= 1;
    if(rd_view_address_match(panel->selected, view)) { panel->selected = rd_nil_view_address; }
    tab->next = tab->prev = 0;
  }
  return tab;
}

internal B32
rd_arrangement_insert_tab(RD_Arrangement *arrangement, RD_ViewAddress view, RD_PanelID destination, RD_ViewAddress prev_view, B32 select)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, destination);
  if(panel == &rd_nil_arrangement_panel || rd_view_address_is_nil(view) || rd_view_address_match(view, prev_view)) { return 0; }
  rd_arrangement_unlink_tab(arrangement, view);
  rd_arrangement_push_tab(arrangement, panel, rd_view_address_is_nil(prev_view) ? 0 : rd_arrangement_tab_from_view(panel, prev_view), view);
  if(select) { panel->selected = view; }
  return 1;
}

internal B32
rd_arrangement_move_tab(RD_Arrangement *arrangement, RD_ViewAddress view, RD_PanelID destination, RD_ViewAddress prev_view)
{
  return rd_arrangement_insert_tab(arrangement, view, destination, prev_view, 1);
}

internal B32
rd_arrangement_detach_tab(RD_Arrangement *arrangement, RD_ViewAddress view)
{
  return rd_arrangement_unlink_tab(arrangement, view) != 0;
}

internal B32
rd_arrangement_settle__heir(RD_Arrangement *arrangement, RD_ArrangementPanel *panel, Dir2 dir, RD_ArrangementTabRule *shown, B32 may_close, RD_PanelID *heir)
{
  if(panel == &rd_nil_arrangement_panel) { return 0; }
  RD_ArrangementTab *first_shown = 0;
  for(RD_ArrangementTab *tab = panel->first_tab; tab != 0 && first_shown == 0; tab = tab->next)
  {
    if(shown == 0 || shown(tab->view)) { first_shown = tab; }
  }
  B32 split = dir != Dir2_Invalid;
  if(may_close && (split ? panel->tab_count == 0 : first_shown == 0))
  {
    // Closing the root does nothing.
    *heir = rd_arrangement_close(arrangement, panel->id);
    return *heir != 0;
  }
  if(first_shown == 0 || (split && !rd_view_address_is_nil(panel->selected)) || rd_view_address_match(panel->selected, first_shown->view)) { return 0; }
  panel->selected = first_shown->view;
  return 1;
}

internal B32
rd_arrangement_settle(RD_Arrangement *arrangement, RD_PanelID panel, Dir2 dir, RD_ArrangementTabRule *shown, B32 may_close)
{
  RD_PanelID heir = 0;
  return rd_arrangement_settle__heir(arrangement, rd_arrangement_panel_from_id(arrangement, panel), dir, shown, may_close, &heir);
}

internal RD_ArrangementDrop
rd_arrangement_drop(RD_Arrangement *arrangement, RD_ViewAddress view, RD_PanelID destination, Dir2 dir, RD_ViewAddress prev_view, RD_ArrangementTabRule *shown)
{
  RD_ArrangementDrop result = {0};
  RD_ArrangementPanel *source = rd_arrangement_panel_from_view(arrangement, view);
  RD_PanelID target = dir == Dir2_Invalid ? destination : rd_arrangement_split(arrangement, destination, dir);
  if(rd_view_address_is_nil(view)) { result.panel = target; return result; }
  if(!rd_arrangement_move_tab(arrangement, view, target, prev_view)) { return result; }
  result.panel = target;
  result.source = source->id;
  if(dir != Dir2_Invalid || source->id != destination)
  { rd_arrangement_settle__heir(arrangement, source, dir, shown, source->id != destination, &result.heir); }
  return result;
}

internal B32
rd_arrangement_remove_tab(RD_Arrangement *arrangement, RD_ViewAddress view)
{
  RD_ArrangementTab *tab = rd_arrangement_unlink_tab(arrangement, view);
  if(tab == 0) { return 0; }
  SLLStackPush(arrangement->first_removed, tab);
  return 1;
}

internal void
rd_arrangement_discard(RD_Arrangement *arrangement)
{
  rd_arrangement_remove_tabs(arrangement, arrangement->root);
  arrangement->root = &rd_nil_arrangement_panel;
}

internal B32
rd_arrangement_select(RD_Arrangement *arrangement, RD_PanelID id, RD_ViewAddress view)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
  B32 result = panel != &rd_nil_arrangement_panel && (rd_view_address_is_nil(view) || rd_arrangement_tab_from_view(panel, view) != 0);
  if(result) { panel->selected = view; }
  return result;
}

internal B32
rd_arrangement_reorder(RD_Arrangement *arrangement, RD_PanelID id, RD_PanelID prev_id)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
  RD_ArrangementPanel *parent = panel->parent;
  RD_ArrangementPanel *prev = rd_arrangement_panel_from_id(arrangement, prev_id);
  if(parent == &rd_nil_arrangement_panel || prev == panel || (prev_id != 0 && prev->parent != parent)) { return 0; }
  DLLRemove_NPZ(&rd_nil_arrangement_panel, parent->first, parent->last, panel, next, prev);
  DLLInsert_NPZ(&rd_nil_arrangement_panel, parent->first, parent->last, prev, panel, next, prev);
  return 1;
}

internal void
rd_arrangement_resize(RD_Arrangement *arrangement, RD_PanelID id, F32 delta, F32 floor)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
  RD_ArrangementPanel *next = panel->next;
  if(panel == &rd_nil_arrangement_panel || next == &rd_nil_arrangement_panel) { return; }
  F32 min = panel->weight, max = next->weight, total = 0;
  for(RD_ArrangementPanel *child = panel->parent->first; child != &rd_nil_arrangement_panel; child = child->next)
  { total += Max(0.f, child->weight); }
  if(total > 0 && abs_f32(total-1.f) > .0001f)
  {
    min /= total;
    max /= total;
    for(RD_ArrangementPanel *child = panel->parent->first; child != &rd_nil_arrangement_panel; child = child->next)
    { child->weight = rd_arrangement_quantize(Max(0.f, child->weight)/total); }
  }
  if(min+max >= 2*floor) { delta = Clamp(floor-min, delta, max-floor); }
  panel->weight = rd_arrangement_quantize(min+delta);
  next->weight = rd_arrangement_quantize(max-delta);
}

internal void
rd_arrangement_equalize(RD_Arrangement *arrangement, RD_PanelID id)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
  rd_arrangement_resize(arrangement, id, 0, 0);
  rd_arrangement_resize(arrangement, id, (panel->next->weight-panel->weight)/2, 0);
}

internal B32
rd_arrangement_readdress(RD_Arrangement *arrangement, RD_ViewAddress from, RD_ViewAddress to)
{
  B32 found = 0;
  if(rd_view_address_is_nil(from) || rd_view_address_match(from, to)) { return 0; }
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    for(RD_ArrangementTab *tab = p->first_tab; tab != 0; tab = tab->next)
    {
      if(rd_view_address_match(tab->view, from)) { tab->view = to; found = 1; }
    }
    if(rd_view_address_match(p->selected, from)) { p->selected = to; }
  }
  for(RD_ArrangementTab *tab = arrangement->first_removed; tab != 0; tab = tab->next)
  {
    if(rd_view_address_match(tab->view, from)) { tab->view = to; }
  }
  return found;
}
