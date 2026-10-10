////////////////////////////////
//~ Weights

// Operations store weights with the config formatter's precision, so a saved
// and reloaded arrangement lays out exactly as the one that was edited.
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

// %f for weights operations wrote; more digits for a loaded weight %f would
// change, so moving a hand-edited panel keeps its value.
internal String8
rd_arrangement_weight_string(Arena *arena, F32 weight)
{
  String8 result = push_str8f(arena, "%f", weight);
  for(S32 digits = 7; digits <= 24 && (F32)f64_from_str8(result) != weight; digits += 1)
  { result = push_str8f(arena, "%.*f", digits, weight); }
  return result;
}

////////////////////////////////
//~ Config shape

typedef enum RD_ArrangementChild
{
  RD_ArrangementChild_Other,
  RD_ArrangementChild_Panel,
  RD_ArrangementChild_Tab,
} RD_ArrangementChild;

// A panel node's children: child panels (numeric), Views (identifiers) and
// panel options.
internal RD_ArrangementChild
rd_arrangement_child_from_cfg(CFG_Node *child)
{
  Temp scratch = scratch_begin(0, 0);
  MD_TokenizeResult tokenize = md_tokenize_from_text(scratch.arena, child->string);
  RD_ArrangementChild result = RD_ArrangementChild_Other;
  if(tokenize.tokens.count == 1 && tokenize.tokens.v[0].flags & MD_TokenFlag_Numeric)
  { result = RD_ArrangementChild_Panel; }
  else if(cfg_panel_child_is_option(child->string) || str8_match(child->string, str8_lit("selected"), 0))
  { result = RD_ArrangementChild_Other; }
  else if(tokenize.tokens.count == 1 && tokenize.tokens.v[0].flags & MD_TokenFlag_Identifier)
  { result = RD_ArrangementChild_Tab; }
  scratch_end(scratch);
  return result;
}

internal CFG_Node *
rd_arrangement_next_child_cfg(CFG_Node *node, RD_ArrangementChild kind)
{
  for(; node != &cfg_nil_node && rd_arrangement_child_from_cfg(node) != kind; node = node->next) {}
  return node;
}

internal RD_ArrangementKeys
rd_arrangement_keys_from_owner(Arena *arena, CFG_Node *owner, String8 root_name)
{
  RD_ArrangementKeys result = {owner, push_str8_copy(arena, root_name)};
  result.axis_key = str8_match(result.root_name, RD_DOCK_SIDEBAR_ROOT, 0) ?
    str8_lit("control_views_split_x") : str8_lit("split_x");
  return result;
}

internal RD_ArrangementKeys
rd_arrangement_keys(Arena *arena, CFG_Node *root)
{
  return rd_arrangement_keys_from_owner(arena, root->parent, root->string);
}

////////////////////////////////
//~ ID Sets

// Open addressing over nonzero IDs, so hand-edited trees of any size load
// and save in linear time.
typedef struct RD_ArrangementIDSet RD_ArrangementIDSet;
struct RD_ArrangementIDSet
{
  U64 *slots;
  U64 mask;
};

internal RD_ArrangementIDSet
rd_arrangement_id_set_alloc(Arena *arena, U64 count)
{
  RD_ArrangementIDSet set = {0};
  U64 cap = 16;
  for(; cap < count*2; cap *= 2) {}
  set.slots = push_array(arena, U64, cap);
  set.mask = cap-1;
  return set;
}

internal U64 *
rd_arrangement_id_set_slot(RD_ArrangementIDSet *set, U64 id)
{
  U64 i = (id*0x9E3779B97F4A7C15ull) & set->mask;
  for(; set->slots[i] != 0 && set->slots[i] != id; i = (i+1) & set->mask) {}
  return &set->slots[i];
}

// Whether `id` was new to the set.
internal B32
rd_arrangement_id_set_insert(RD_ArrangementIDSet *set, U64 id)
{
  U64 *slot = rd_arrangement_id_set_slot(set, id);
  B32 inserted = *slot == 0;
  *slot = id;
  return inserted;
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

// Pre-order, without growing the C call stack on long saved split chains.
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
rd_arrangement_tab_from_view(RD_ArrangementPanel *panel, CFG_ID view)
{
  for(RD_ArrangementTab *tab = panel->first_tab; tab != 0; tab = tab->next)
  {
    if(tab->view == view) { return tab; }
  }
  return 0;
}

internal void
rd_arrangement_push_tab(RD_Arrangement *arrangement, RD_ArrangementPanel *panel, RD_ArrangementTab *prev, CFG_ID view)
{
  RD_ArrangementTab *tab = push_array(arrangement->arena, RD_ArrangementTab, 1);
  tab->view = view;
  DLLInsert_NPZ((RD_ArrangementTab *)0, panel->first_tab, panel->last_tab, prev, tab, next, prev);
  panel->tab_count += 1;
}

////////////////////////////////
//~ Loading and Saving

internal RD_Arrangement *
rd_arrangement_from_cfg(Arena *arena, CFG_Node *panels_root)
{
  RD_Arrangement *arrangement = push_array(arena, RD_Arrangement, 1);
  arrangement->arena = arena;
  arrangement->root = &rd_nil_arrangement_panel;
  arrangement->next_id = 1;
  if(panels_root == &cfg_nil_node) { return arrangement; }
  // The arrangement names nodes by ID, so they must be the selected config's.
  Assert(cfg_node_from_id(panels_root->id) == panels_root);
  RD_ArrangementKeys keys = rd_arrangement_keys(arena, panels_root);
  arrangement->owner = keys.owner->id;
  arrangement->root_name = keys.root_name;
  arrangement->axis_key = keys.axis_key;
  arrangement->saved_root = panels_root->id;
  arrangement->root_axis = cfg_node_child_from_string(keys.owner, keys.axis_key) != &cfg_nil_node ? Axis2_X : Axis2_Y;

  // Panels in tree order, keeping saved IDs.
  U64 count = 0;
  RD_ArrangementPanel *parent = &rd_nil_arrangement_panel;
  for(CFG_Node *src = panels_root; src != &cfg_nil_node;)
  {
    RD_ArrangementPanel *panel = push_array(arena, RD_ArrangementPanel, 1);
    MemoryCopyStruct(panel, &rd_nil_arrangement_panel);
    panel->cfg = src->id;
    panel->weight = src == panels_root ? 1.f : (F32)f64_from_str8(src->string);
    if(parent == &rd_nil_arrangement_panel) { arrangement->root = panel; }
    else { rd_arrangement_insert(parent, parent->last, panel); }
    count += 1;
    CFG_Node *first_child = &cfg_nil_node;
    for(CFG_Node *child = src->first; child != &cfg_nil_node; child = child->next)
    {
      RD_ArrangementChild kind = rd_arrangement_child_from_cfg(child);
      if(kind == RD_ArrangementChild_Panel && first_child == &cfg_nil_node) { first_child = child; }
      else if(kind == RD_ArrangementChild_Tab)
      {
        rd_arrangement_push_tab(arrangement, panel, panel->last_tab, child->id);
        if(cfg_node_child_from_string(child, str8_lit("selected")) != &cfg_nil_node) { panel->selected = child->id; }
      }
      else if(str8_match(child->string, str8_lit("id"), 0) && str8_is_integer(child->first->string, 10))
      { panel->id = u64_from_str8(child->first->string, 10); }
    }
    if(first_child != &cfg_nil_node)
    {
      parent = panel;
      src = first_child;
      continue;
    }
    for(;;)
    {
      if(src == panels_root) { src = &cfg_nil_node; break; }
      CFG_Node *sibling = rd_arrangement_next_child_cfg(src->next, RD_ArrangementChild_Panel);
      if(sibling != &cfg_nil_node) { src = sibling; break; }
      src = src->parent;
      parent = parent->parent;
    }
  }

  // Panels without a saved ID, or with one an earlier panel has, take the
  // next free ones in tree order.
  Temp scratch = scratch_begin(&arena, 1);
  RD_ArrangementIDSet ids = rd_arrangement_id_set_alloc(scratch.arena, count);
  RD_PanelID max_id = 0;
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    if(p->id != 0 && !rd_arrangement_id_set_insert(&ids, p->id)) { p->id = 0; }
    max_id = Max(max_id, p->id);
  }
  scratch_end(scratch);
  arrangement->next_id = max_id+1;
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    if(p->id == 0) { p->id = arrangement->next_id++; }
  }
  return arrangement;
}

internal RD_Arrangement *
rd_arrangement_from_owner(Arena *arena, CFG_Node *owner, String8 root_name)
{
  CFG_Node *root = cfg_node_child_from_string(owner, root_name);
  if(root != &cfg_nil_node || owner == &cfg_nil_node) { return rd_arrangement_from_cfg(arena, root); }
  RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, &cfg_nil_node);
  RD_ArrangementKeys keys = rd_arrangement_keys_from_owner(arena, owner, root_name);
  arrangement->owner = owner->id;
  arrangement->root_name = keys.root_name;
  arrangement->axis_key = keys.axis_key;
  arrangement->root_axis = cfg_node_child_from_string(owner, keys.axis_key) != &cfg_nil_node ? Axis2_X : Axis2_Y;
  return arrangement;
}

internal RD_Arrangement *
rd_arrangement_copy(Arena *arena, RD_Arrangement *src)
{
  RD_Arrangement *dst = push_array(arena, RD_Arrangement, 1);
  MemoryCopyStruct(dst, src);
  dst->arena = arena;
  dst->root = &rd_nil_arrangement_panel;
  dst->root_name = push_str8_copy(arena, src->root_name);
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

// Puts `child` after `prev` among `parent`'s children of its kind (first
// when `prev` is nil), unless it is there already.
internal void
rd_arrangement_place_cfg(CFG_State *state, CFG_Node *parent, CFG_Node *prev, CFG_Node *child, RD_ArrangementChild kind)
{
  CFG_Node *at = rd_arrangement_next_child_cfg(prev == &cfg_nil_node ? parent->first : prev->next, kind);
  if(at != child) { cfg_node_insert_child(state, parent, prev, child); }
}

internal void
rd_arrangement_save(CFG_State *state, RD_Arrangement *arrangement)
{
  CFG_Node *owner = cfg_node_from_id(arrangement->owner);
  RD_ArrangementPanel *root = arrangement->root;
  if(owner == &cfg_nil_node || root == &rd_nil_arrangement_panel) { return; }
  Temp scratch = scratch_begin(0, 0);

  // The panel nodes saved last time, before any move.
  CFG_NodePtrList saved = {0};
  {
    CFG_Node *saved_root = cfg_node_from_id(arrangement->saved_root);
    if(saved_root != &cfg_nil_node) { cfg_node_ptr_list_push(scratch.arena, &saved, saved_root); }
    for(CFG_NodePtrNode *n = saved.first; n != 0; n = n->next)
    {
      for(CFG_Node *c = rd_arrangement_next_child_cfg(n->v->first, RD_ArrangementChild_Panel); c != &cfg_nil_node;
          c = rd_arrangement_next_child_cfg(c->next, RD_ArrangementChild_Panel))
      { cfg_node_ptr_list_push(scratch.arena, &saved, c); }
    }
  }

  // A node for each panel, named for its place, with its ID.
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p))
  {
    CFG_Node *node = cfg_node_from_id(p->cfg);
    if(node == &cfg_nil_node)
    {
      node = cfg_node_alloc(state);
      p->cfg = node->id;
    }
    if(p == root)
    {
      if(!str8_match(node->string, arrangement->root_name, 0)) { cfg_node_equip_string(state, node, arrangement->root_name); }
    }
    else if(rd_arrangement_child_from_cfg(node) != RD_ArrangementChild_Panel || (F32)f64_from_str8(node->string) != p->weight)
    { cfg_node_equip_string(state, node, rd_arrangement_weight_string(scratch.arena, p->weight)); }
    String8 id = push_str8f(scratch.arena, "%I64u", p->id);
    CFG_Node *id_node = cfg_node_child_from_string(node, str8_lit("id"));
    if(id_node == &cfg_nil_node) { cfg_node_new(state, cfg_node_new(state, node, str8_lit("id")), id); }
    else if(!str8_match(id_node->first->string, id, 0) || id_node->first != id_node->last)
    { cfg_node_new_replace(state, id_node, id); }
  }

  // Each node under its parent, in order; Views carry their own settings.
  // Parents are placed before their children, so no node lands inside itself.
  CFG_Node *root_node = cfg_node_from_id(root->cfg);
  if(root_node->parent != owner) { cfg_node_insert_child(state, owner, owner->last, root_node); }
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p))
  {
    CFG_Node *node = cfg_node_from_id(p->cfg);
    CFG_Node *prev = &cfg_nil_node;
    for(RD_ArrangementPanel *child = p->first; child != &rd_nil_arrangement_panel; child = child->next)
    {
      CFG_Node *child_node = cfg_node_from_id(child->cfg);
      rd_arrangement_place_cfg(state, node, prev, child_node, RD_ArrangementChild_Panel);
      prev = child_node;
    }
    prev = &cfg_nil_node;
    for(RD_ArrangementTab *tab = p->first_tab; tab != 0; tab = tab->next)
    {
      CFG_Node *view = cfg_node_from_id(tab->view);
      if(view == &cfg_nil_node) { continue; }
      rd_arrangement_place_cfg(state, node, prev, view, RD_ArrangementChild_Tab);
      prev = view;
    }
  }

  // A panel that took another's tabs takes its Presentation State too.
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p))
  {
    CFG_Node *from = cfg_node_from_id(p->options_from), *node = cfg_node_from_id(p->cfg);
    for(CFG_Node *c = from->first, *next = &cfg_nil_node; c != &cfg_nil_node && from != node; c = next)
    {
      next = c->next;
      if(rd_arrangement_child_from_cfg(c) == RD_ArrangementChild_Other && !str8_match(c->string, str8_lit("id"), 0))
      { cfg_node_insert_child(state, node, node->last, c); }
    }
    p->options_from = 0;
  }

  // Only the Selected View is marked `selected`, whatever marks Views brought
  // with them.
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p))
  {
    for(RD_ArrangementTab *tab = p->first_tab; tab != 0; tab = tab->next)
    {
      CFG_Node *view = cfg_node_from_id(tab->view);
      CFG_Node *mark = cfg_node_child_from_string(view, str8_lit("selected"));
      if(tab->view != p->selected)
      {
        for(; mark != &cfg_nil_node; mark = cfg_node_child_from_string(view, str8_lit("selected"))) { cfg_node_release(state, mark); }
      }
      else if(mark == &cfg_nil_node && view != &cfg_nil_node)
      {
        mark = cfg_node_alloc(state);
        cfg_node_equip_string(state, mark, str8_lit("selected"));
        cfg_node_insert_child(state, view, &cfg_nil_node, mark);
      }
    }
  }

  // The root's axis.
  CFG_Node *axis = cfg_node_child_from_string(owner, arrangement->axis_key);
  if(arrangement->root_axis == Axis2_X && axis == &cfg_nil_node) { cfg_node_new(state, owner, arrangement->axis_key); }
  if(arrangement->root_axis != Axis2_X && axis != &cfg_nil_node) { cfg_node_release(state, axis); }

  // Panel nodes no longer in the arrangement go, with any Views left in them.
  U64 count = 0;
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p)) { count += 1; }
  RD_ArrangementIDSet used = rd_arrangement_id_set_alloc(scratch.arena, count);
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p)) { rd_arrangement_id_set_insert(&used, p->cfg); }
  for(CFG_NodePtrNode *n = saved.first; n != 0; n = n->next)
  {
    CFG_ID id = n->v->id;
    if(id != 0 && *rd_arrangement_id_set_slot(&used, id) != id) { cfg_node_release(state, n->v); }
  }

  // Removed tabs' Views go, unless a tab took them back.
  for(RD_ArrangementTab *tab = arrangement->first_removed; tab != 0; tab = tab->next)
  {
    CFG_Node *view = cfg_node_from_id(tab->view);
    if(view != &cfg_nil_node && rd_arrangement_panel_from_view(arrangement, tab->view) == &rd_nil_arrangement_panel)
    { cfg_node_release(state, view); }
  }
  arrangement->first_removed = 0;
  arrangement->saved_root = root->cfg;
  scratch_end(scratch);
}

////////////////////////////////
//~ The Renderer's Panel Tree

internal CFG_PanelTree
rd_panel_tree_from_arrangement(Arena *arena, RD_Arrangement *arrangement)
{
  CFG_PanelTree tree = {&cfg_nil_panel_node, &cfg_nil_panel_node};
  RD_ArrangementPanel *root = arrangement->root;
  CFG_PanelNode *parent = &cfg_nil_panel_node;
  Axis2 axis = arrangement->root_axis;
  for(RD_ArrangementPanel *s = root; s != &rd_nil_arrangement_panel;)
  {
    CFG_PanelNode *d = push_array(arena, CFG_PanelNode, 1);
    MemoryCopyStruct(d, &cfg_nil_panel_node);
    d->cfg = cfg_node_from_id(s->cfg);
    d->split_axis = axis;
    d->pct_of_parent = s->weight;
    d->tab_side = Side_Min;
    for(CFG_Node *c = d->cfg->first; c != &cfg_nil_node; c = c->next)
    {
      if(str8_match(c->string, str8_lit("selected"), 0)) { tree.focused = d; }
      else if(str8_match(c->string, str8_lit("tabs_on_bottom"), 0)) { d->tab_side = Side_Max; }
    }
    for(RD_ArrangementTab *tab = s->first_tab; tab != 0; tab = tab->next)
    {
      CFG_Node *view = cfg_node_from_id(tab->view);
      cfg_node_ptr_list_push(arena, &d->tabs, view);
      if(tab->view == s->selected) { d->selected_tab = view; }
    }
    d->parent = parent;
    if(parent == &cfg_nil_panel_node) { tree.root = d; }
    else
    {
      DLLPushBack_NPZ(&cfg_nil_panel_node, parent->first, parent->last, d, next, prev);
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

internal CFG_PanelTree
rd_panel_tree_from_cfg(Arena *arena, CFG_Node *panels_root)
{
  return rd_panel_tree_from_arrangement(arena, rd_arrangement_from_cfg(arena, panels_root));
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
rd_arrangement_panel_from_cfg(RD_Arrangement *arrangement, CFG_ID cfg)
{
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel && cfg != 0; p = rd_arrangement_next(arrangement->root, p))
  {
    if(p->cfg == cfg) { return p; }
  }
  return &rd_nil_arrangement_panel;
}

internal RD_ArrangementPanel *
rd_arrangement_panel_from_view(RD_Arrangement *arrangement, CFG_ID view)
{
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel && view != 0; p = rd_arrangement_next(arrangement->root, p))
  {
    if(rd_arrangement_tab_from_view(p, view)) { return p; }
  }
  return &rd_nil_arrangement_panel;
}

// Axes alternate by depth: the config can store nothing else.
internal Axis2
rd_arrangement_split_axis(RD_Arrangement *arrangement, RD_ArrangementPanel *panel)
{
  Axis2 axis = arrangement->root_axis;
  for(RD_ArrangementPanel *p = panel->parent; p != &rd_nil_arrangement_panel; p = p->parent) { axis = axis2_flip(axis); }
  return axis;
}

// As cfg_target_rect_from_panel_node_child.
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

// As cfg_target_rect_from_panel_node: the root's children divide the area
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
    B32 selected = p->selected == 0;
    for(RD_ArrangementTab *tab = p->first_tab; tab != 0; tab = tab->next)
    {
      tabs += 1;
      selected |= tab->view == p->selected;
      for(RD_ArrangementPanel *q = root; q != &rd_nil_arrangement_panel; q = rd_arrangement_next(root, q))
      {
        for(RD_ArrangementTab *other = q->first_tab; other != 0; other = other->next)
        {
          if(other != tab && other->view == tab->view) { return push_str8f(arena, "View %I64u has two tabs", tab->view); }
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
    lifted->options_from = parent->cfg;
    parent->first_tab = parent->last_tab = 0;
    parent->tab_count = 0;
    parent->selected = 0;
    rd_arrangement_insert(parent, &rd_nil_arrangement_panel, lifted);
  }
  RD_ArrangementPanel *created = rd_arrangement_panel_alloc(arrangement);
  if(weight > 0) { created->weight = rd_arrangement_quantize(weight); }
  else
  {
    // Malformed or zero saved weights still keep a share, so none becomes
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
  // Saving releases its Views, and the panel nodes no longer used.
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
    p->selected = 0;
  }
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

internal B32
rd_arrangement_move_tab(RD_Arrangement *arrangement, CFG_ID view, RD_PanelID destination, CFG_ID prev_view)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, destination);
  if(panel == &rd_nil_arrangement_panel || view == 0 || view == prev_view) { return 0; }
  RD_ArrangementPanel *source = rd_arrangement_panel_from_view(arrangement, view);
  RD_ArrangementTab *tab = rd_arrangement_tab_from_view(source, view);
  if(tab)
  {
    DLLRemove_NPZ((RD_ArrangementTab *)0, source->first_tab, source->last_tab, tab, next, prev);
    source->tab_count -= 1;
    if(source->selected == view) { source->selected = 0; }
  }
  rd_arrangement_push_tab(arrangement, panel, prev_view ? rd_arrangement_tab_from_view(panel, prev_view) : 0, view);
  panel->selected = view;
  return 1;
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
  if(first_shown == 0 || (split && panel->selected != 0) || panel->selected == first_shown->view) { return 0; }
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
rd_arrangement_drop(RD_Arrangement *arrangement, CFG_ID view, RD_PanelID destination, Dir2 dir, CFG_ID prev_view, RD_ArrangementTabRule *shown)
{
  RD_ArrangementDrop result = {0};
  RD_ArrangementPanel *source = rd_arrangement_panel_from_view(arrangement, view);
  RD_PanelID target = dir == Dir2_Invalid ? destination : rd_arrangement_split(arrangement, destination, dir);
  if(view == 0) { result.panel = target; return result; }
  if(!rd_arrangement_move_tab(arrangement, view, target, prev_view)) { return result; }
  result.panel = target;
  result.source = source->id;
  if(dir != Dir2_Invalid || source->id != destination)
  { rd_arrangement_settle__heir(arrangement, source, dir, shown, source->id != destination, &result.heir); }
  return result;
}

internal B32
rd_arrangement_remove_tab(RD_Arrangement *arrangement, CFG_ID view)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_view(arrangement, view);
  RD_ArrangementTab *tab = rd_arrangement_tab_from_view(panel, view);
  if(tab == 0) { return 0; }
  DLLRemove_NPZ((RD_ArrangementTab *)0, panel->first_tab, panel->last_tab, tab, next, prev);
  panel->tab_count -= 1;
  if(panel->selected == view) { panel->selected = 0; }
  tab->prev = 0;
  SLLStackPush(arrangement->first_removed, tab);
  return 1;
}

internal B32
rd_arrangement_select(RD_Arrangement *arrangement, RD_PanelID id, CFG_ID view)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
  B32 result = panel != &rd_nil_arrangement_panel && (view == 0 || rd_arrangement_tab_from_view(panel, view) != 0);
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
