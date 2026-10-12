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
rd_arrangement_new(Arena *arena, CFG_Node *owner, String8 root_name)
{
  RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, &cfg_nil_node);
  if(owner == &cfg_nil_node) { return arrangement; }
  RD_ArrangementKeys keys = rd_arrangement_keys_from_owner(arena, owner, root_name);
  arrangement->owner = owner->id;
  arrangement->root_name = keys.root_name;
  arrangement->axis_key = keys.axis_key;
  arrangement->root_axis = cfg_node_child_from_string(owner, keys.axis_key) != &cfg_nil_node ? Axis2_X : Axis2_Y;
  return arrangement;
}

internal RD_Arrangement *
rd_arrangement_from_owner(Arena *arena, CFG_Node *owner, String8 root_name)
{
  CFG_Node *root = cfg_node_child_from_string(owner, root_name);
  if(root != &cfg_nil_node || owner == &cfg_nil_node) { return rd_arrangement_from_cfg(arena, root); }
  return rd_arrangement_new(arena, owner, root_name);
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

internal void rd_view_namespace_views__walk(Arena *arena, CFG_NodePtrList *list, CFG_Node *container);

// A View node (`view`), or a panel node with the Views left in it, goes, with
// the Presentation State their host keeps for them, unless another View
// there has the address (one that took its place, as a rebind's new
// instance does).
internal void
rd_arrangement_release_cfg(CFG_State *state, CFG_Node *node, B32 view)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_NodePtrList views = {0};
  if(view) { cfg_node_ptr_list_push(scratch.arena, &views, node); }
  else { rd_view_namespace_views__walk(scratch.arena, &views, node); }
  for(CFG_NodePtrNode *v = views.first; v != 0; v = v->next)
  {
    CFG_Node *host = &cfg_nil_node;
    RD_ViewOwnerKind owner = rd_view_owner_kind_from_tab(v->v, &host);
    RD_ViewInstance instance = RD_ViewInstance_Current;
    String8 key = rd_view_key_from_cfg(v->v, &instance);
    CFG_Node *root = cfg_node_child_from_string(host, RD_VIEW_PRESENTATION_ROOT);
    if(owner == RD_ViewOwnerKind_Null || key.size == 0 || root == &cfg_nil_node) { continue; }
    B32 taken = 0;
    CFG_NodePtrList others = rd_view_namespace_views(scratch.arena, host, owner);
    for(CFG_NodePtrNode *n = others.first; n != 0 && !taken; n = n->next)
    {
      RD_ViewInstance other = RD_ViewInstance_Current;
      B32 going = 0;
      for(CFG_NodePtrNode *g = views.first; g != 0 && !going; g = g->next) { going = g->v == n->v; }
      taken = !going && str8_match(rd_view_key_from_cfg(n->v, &other), key, 0) && other == instance;
    }
    for(CFG_Node *c = root->first; c != &cfg_nil_node && !taken; c = c->next)
    {
      if(str8_match(c->string, rd_view_owner_kind_name(owner), 0) &&
         str8_match(cfg_node_child_from_string(c, str8_lit("key"))->first->string, key, 0) &&
         (cfg_node_child_from_string(c, str8_lit("previous")) != &cfg_nil_node) == (instance == RD_ViewInstance_Previous))
      {
        cfg_node_release(state, c);
        break;
      }
    }
  }
  scratch_end(scratch);
  cfg_node_release(state, node);
}

internal void
rd_arrangement_save(CFG_State *state, RD_Arrangement *arrangement)
{
  CFG_Node *owner = cfg_node_from_id(arrangement->owner);
  RD_ArrangementPanel *root = arrangement->root;
  if(owner == &cfg_nil_node) { return; }
  if(root == &rd_nil_arrangement_panel)
  {
    // Discarded, or never had panels: the old root node and removed Views go.
    CFG_Node *saved_root = cfg_node_from_id(arrangement->saved_root);
    if(saved_root != &cfg_nil_node) { rd_arrangement_release_cfg(state, saved_root, 0); }
    for(RD_ArrangementTab *tab = arrangement->first_removed; tab != 0; tab = tab->next)
    {
      CFG_Node *view = cfg_node_from_id(tab->view);
      if(view != &cfg_nil_node) { rd_arrangement_release_cfg(state, view, 1); }
    }
    arrangement->first_removed = 0;
    arrangement->saved_root = 0;
    return;
  }
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

  // Each node under its parent, in order; Views carry their own logical
  // settings (their Presentation State is their host's, by View Address).
  // Parents are placed before their children, so no node lands inside itself.
  // The Views that come from another panel, or from none.
  CFG_NodePtrList moved = {0};
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
      if(view->parent != node)
      {
        cfg_node_ptr_list_push(scratch.arena, &moved, view);
        if(rd_arrangement_view_move_hook) { rd_arrangement_view_move_hook(view, 0); }
      }
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
    if(id != 0 && *rd_arrangement_id_set_slot(&used, id) != id) { rd_arrangement_release_cfg(state, n->v, 0); }
  }

  // Removed tabs' Views go, unless a tab took them back.
  for(RD_ArrangementTab *tab = arrangement->first_removed; tab != 0; tab = tab->next)
  {
    CFG_Node *view = cfg_node_from_id(tab->view);
    if(view != &cfg_nil_node && rd_arrangement_panel_from_view(arrangement, tab->view) == &rd_nil_arrangement_panel)
    { rd_arrangement_release_cfg(state, view, 1); }
  }
  arrangement->first_removed = 0;
  arrangement->saved_root = root->cfg;

  // Every View has a key in its owner's namespace. One from elsewhere, maybe
  // another owner, keeps its key only while no View there has it.
  for(RD_ArrangementPanel *p = root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(root, p))
  {
    for(RD_ArrangementTab *tab = p->first_tab; tab != 0; tab = tab->next)
    {
      CFG_Node *view = cfg_node_from_id(tab->view);
      B32 from_elsewhere = 0;
      for(CFG_NodePtrNode *n = moved.first; n != 0 && !from_elsewhere; n = n->next) { from_elsewhere = n->v == view; }
      if(view != &cfg_nil_node) { rd_view_key_assign(state, view, from_elsewhere); }
    }
  }
  if(rd_arrangement_view_move_hook)
  {
    for(CFG_NodePtrNode *n = moved.first; n != 0; n = n->next) { rd_arrangement_view_move_hook(n->v, 1); }
  }
  scratch_end(scratch);
}

////////////////////////////////
//~ Panel Addresses

internal B32
rd_panel_owner_match(RD_PanelOwner a, RD_PanelOwner b)
{
  return a.kind == b.kind && a.floating == b.floating && MemoryMatch(a.id.v, b.id.v, sizeof(a.id.v));
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

////////////////////////////////
//~ View Addresses

StaticAssert(sizeof(RD_ViewAddress) == 2*sizeof(U32) + sizeof(UIShell_WorkspaceId) + sizeof(U128), rd_view_address_unpadded);

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

// A sidebar section View, keyed by the section it shows. One that names no
// section is keyed as any other View of its window.
internal B32
rd_view_is_section(CFG_Node *view)
{
  return str8_match(view->string, str8_lit("sidebar_section"), 0) &&
    cfg_node_child_from_string(view, str8_lit("section"))->first->string.size != 0;
}

internal String8
rd_view_key_from_cfg(CFG_Node *view, RD_ViewInstance *instance_out)
{
  RD_ViewInstance instance = RD_ViewInstance_Current;
  String8 key = str8_zero();
  if(rd_view_is_section(view)) { key = cfg_node_child_from_string(view, str8_lit("section"))->first->string; }
  else
  {
    key = cfg_node_child_from_string(view, str8_lit("slot"))->first->string;
    if(!key.size)
    {
      key = cfg_node_child_from_string(view, str8_lit("previous_of"))->first->string;
      if(key.size) { instance = RD_ViewInstance_Previous; }
    }
    // A workspace View made for a provider's slot, until the workspace store
    // keys it (which knows whether Andamento has that slot): that slot's key.
    if(!key.size) { key = cfg_node_child_from_string(view, str8_lit("resource_id"))->first->string; }
  }
  if(instance_out) { *instance_out = instance; }
  return key;
}

internal RD_ViewOwnerKind
rd_view_owner_kind_from_tab(CFG_Node *view, CFG_Node **host_out)
{
  B32 section = rd_view_is_section(view);
  RD_ViewOwnerKind kind = RD_ViewOwnerKind_Null;
  CFG_Node *host = &cfg_nil_node;
  for(CFG_Node *c = view->parent; c != &cfg_nil_node && kind == RD_ViewOwnerKind_Null; c = c->parent)
  {
    if(str8_match(c->string, str8_lit("panels"), 0)) { host = c->parent; kind = RD_ViewOwnerKind_Workspace; }
    else if(str8_match(c->string, str8_lit("control_views"), 0) ||
            (str8_match(c->string, str8_lit("floating_panels"), 0) && str8_match(c->parent->string, str8_lit("window"), 0)))
    { host = c->parent; kind = section ? RD_ViewOwnerKind_Dashboard : RD_ViewOwnerKind_Window; }
    else if(str8_match(c->string, str8_lit("floating_panels"), 0)) { host = c->parent; kind = RD_ViewOwnerKind_Workspace; }
  }
  if(host_out) { *host_out = host; }
  return kind;
}

internal RD_ViewAddress
rd_view_address_from_tab(RD_PanelOwner owner, CFG_Node *view)
{
  RD_ViewInstance instance = RD_ViewInstance_Current;
  String8 key = rd_view_key_from_cfg(view, &instance);
  RD_ViewOwnerKind kind = owner.kind == RD_PanelOwnerKind_Null ? RD_ViewOwnerKind_Null : rd_view_owner_kind_from_tab(view, 0);
  return rd_view_address_make(kind, owner.id, key, instance);
}

// The Views of `host`'s namespace for `owner` (rd_view_owner_kind_from_tab):
// the tabs of its arrangements, in tree order.
internal void
rd_view_namespace_views__walk(Arena *arena, CFG_NodePtrList *list, CFG_Node *container)
{
  // Without recursion: a hand-edited split chain can be very deep.
  CFG_Node *panel = container, *c = container->first;
  for(;;)
  {
    if(c == &cfg_nil_node)
    {
      if(panel == container) { break; }
      c = panel->next;
      panel = panel->parent;
      continue;
    }
    RD_ArrangementChild kind = rd_arrangement_child_from_cfg(c);
    if(kind == RD_ArrangementChild_Panel) { panel = c; c = c->first; continue; }
    if(kind == RD_ArrangementChild_Tab) { cfg_node_ptr_list_push(arena, list, c); }
    c = c->next;
  }
}

internal CFG_NodePtrList
rd_view_namespace_views(Arena *arena, CFG_Node *host, RD_ViewOwnerKind owner)
{
  CFG_NodePtrList list = {0};
  B32 window = str8_match(host->string, str8_lit("window"), 0);
  CFG_Node *roots[2] = {&cfg_nil_node, &cfg_nil_node};
  if(owner == RD_ViewOwnerKind_Workspace)
  {
    roots[0] = cfg_node_child_from_string(host, str8_lit("panels"));
    if(!window) { roots[1] = cfg_node_child_from_string(host, str8_lit("floating_panels")); }
  }
  else if(owner == RD_ViewOwnerKind_Window || owner == RD_ViewOwnerKind_Dashboard)
  {
    roots[0] = cfg_node_child_from_string(host, str8_lit("control_views"));
    roots[1] = cfg_node_child_from_string(host, str8_lit("floating_panels"));
  }
  for(U64 i = 0; i < ArrayCount(roots); i++)
  {
    if(roots[i] == &cfg_nil_node) { continue; }
    // A Floating Panel host's panels are each a root.
    if(str8_match(roots[i]->string, str8_lit("floating_panels"), 0))
    {
      for(CFG_Node *c = roots[i]->first; c != &cfg_nil_node; c = c->next)
      { if(rd_arrangement_child_from_cfg(c) == RD_ArrangementChild_Panel) { rd_view_namespace_views__walk(arena, &list, c); } }
    }
    else { rd_view_namespace_views__walk(arena, &list, roots[i]); }
  }
  // Sections are the Dashboard's; anything else in a window's sidebar is the
  // window's.
  for(CFG_NodePtrNode *n = list.first, *prev = 0; n != 0; n = n->next)
  {
    B32 section = rd_view_is_section(n->v);
    B32 keep = owner == RD_ViewOwnerKind_Workspace || (owner == RD_ViewOwnerKind_Dashboard) == section;
    if(keep) { prev = n; continue; }
    if(prev) { prev->next = n->next; } else { list.first = n->next; }
    if(list.last == n) { list.last = prev; }
    list.count -= 1;
  }
  return list;
}

// The n of a "u:<n>" key, else 0.
internal U64
rd_view_user_number(String8 key)
{
  String8 digits = str8_skip(key, 2);
  B32 user = str8_match(str8_prefix(key, 2), str8_lit("u:"), 0) && digits.size != 0 && digits.size < 18 && str8_is_integer(digits, 10);
  return user ? u64_from_str8(digits, 10) : 0;
}

internal U64
rd_view_user_number_max(CFG_Node *host, RD_ViewOwnerKind owner)
{
  Temp scratch = scratch_begin(0, 0);
  U64 max = 0;
  CFG_NodePtrList views = rd_view_namespace_views(scratch.arena, host, owner);
  for(CFG_NodePtrNode *n = views.first; n != 0; n = n->next) { max = Max(max, rd_view_user_number(rd_view_key_from_cfg(n->v, 0))); }
  // A key whose Presentation State is still kept isn't reused, so a new
  // View never takes over another's settings.
  String8 name = rd_view_owner_kind_name(owner);
  for(CFG_Node *c = cfg_node_child_from_string(host, RD_VIEW_PRESENTATION_ROOT)->first; c != &cfg_nil_node; c = c->next)
  {
    if(str8_match(c->string, name, 0)) { max = Max(max, rd_view_user_number(cfg_node_child_from_string(c, str8_lit("key"))->first->string)); }
  }
  scratch_end(scratch);
  return max;
}

internal B32
rd_view_key_assign(CFG_State *state, CFG_Node *view, B32 unique)
{
  CFG_Node *host = &cfg_nil_node;
  RD_ViewOwnerKind owner = rd_view_owner_kind_from_tab(view, &host);
  if(owner != RD_ViewOwnerKind_Workspace && owner != RD_ViewOwnerKind_Window) { return 0; }
  RD_ViewInstance instance = RD_ViewInstance_Current;
  String8 key = rd_view_key_from_cfg(view, &instance);
  B32 assign = key.size == 0;
  if(!assign && unique)
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_NodePtrList views = rd_view_namespace_views(scratch.arena, host, owner);
    for(CFG_NodePtrNode *n = views.first; n != 0 && !assign; n = n->next)
    {
      RD_ViewInstance other = RD_ViewInstance_Current;
      assign = n->v != view && str8_match(rd_view_key_from_cfg(n->v, &other), key, 0) && other == instance;
    }
    scratch_end(scratch);
  }
  if(!assign) { return 0; }
  // A previous instance whose Slot isn't here is a View of its own.
  CFG_Node *previous_of = cfg_node_child_from_string(view, str8_lit("previous_of"));
  if(previous_of != &cfg_nil_node) { cfg_node_release(state, previous_of); }
  Temp scratch = scratch_begin(0, 0);
  String8 next = push_str8f(scratch.arena, "u:%I64u", rd_view_user_number_max(host, owner)+1);
  CFG_Node *slot = cfg_node_child_from_string(view, str8_lit("slot"));
  if(slot == &cfg_nil_node) { slot = cfg_node_new(state, view, str8_lit("slot")); }
  cfg_node_new_replace(state, slot, next);
  scratch_end(scratch);
  return 1;
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
    CFG_Node *cfg = cfg_node_from_id(s->cfg);
    for(CFG_Node *c = cfg->first; c != &cfg_nil_node; c = c->next)
    {
      if(str8_match(c->string, str8_lit("selected"), 0)) { tree.focused = d; }
      else if(str8_match(c->string, str8_lit("tabs_on_bottom"), 0)) { d->tab_side = Side_Max; }
    }
    for(RD_ArrangementTab *tab = s->first_tab; tab != 0; tab = tab->next)
    {
      RD_ViewAddress view = rd_view_address_from_tab(owner, cfg_node_from_id(tab->view));
      rd_view_address_list_push(arena, &d->tabs, view);
      if(tab->view == s->selected) { d->selected_tab = view; }
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
rd_arrangement_clear(RD_Arrangement *arrangement)
{
  // Saving releases the panel nodes no longer used, and these tabs' Views.
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    for(RD_ArrangementTab *tab = p->first_tab, *next = 0; tab != 0; tab = next)
    {
      next = tab->next;
      tab->prev = 0;
      SLLStackPush(arrangement->first_removed, tab);
    }
  }
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

// Takes `view`'s tab out of its panel, leaving the tab for the caller.
internal RD_ArrangementTab *
rd_arrangement_unlink_tab(RD_Arrangement *arrangement, CFG_ID view)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_view(arrangement, view);
  RD_ArrangementTab *tab = rd_arrangement_tab_from_view(panel, view);
  if(tab)
  {
    DLLRemove_NPZ((RD_ArrangementTab *)0, panel->first_tab, panel->last_tab, tab, next, prev);
    panel->tab_count -= 1;
    if(panel->selected == view) { panel->selected = 0; }
    tab->next = tab->prev = 0;
  }
  return tab;
}

internal B32
rd_arrangement_insert_tab(RD_Arrangement *arrangement, CFG_ID view, RD_PanelID destination, CFG_ID prev_view, B32 select)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, destination);
  if(panel == &rd_nil_arrangement_panel || view == 0 || view == prev_view) { return 0; }
  rd_arrangement_unlink_tab(arrangement, view);
  rd_arrangement_push_tab(arrangement, panel, prev_view ? rd_arrangement_tab_from_view(panel, prev_view) : 0, view);
  if(select) { panel->selected = view; }
  return 1;
}

internal B32
rd_arrangement_move_tab(RD_Arrangement *arrangement, CFG_ID view, RD_PanelID destination, CFG_ID prev_view)
{
  return rd_arrangement_insert_tab(arrangement, view, destination, prev_view, 1);
}

internal B32
rd_arrangement_detach_tab(RD_Arrangement *arrangement, CFG_ID view)
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
  RD_ArrangementTab *tab = rd_arrangement_unlink_tab(arrangement, view);
  if(tab == 0) { return 0; }
  SLLStackPush(arrangement->first_removed, tab);
  return 1;
}

internal B32
rd_arrangement_release_orphan(RD_Arrangement *arrangement, CFG_ID view)
{
  if(view == 0 || rd_arrangement_panel_from_view(arrangement, view) != &rd_nil_arrangement_panel) { return 0; }
  RD_ArrangementTab *tab = push_array(arrangement->arena, RD_ArrangementTab, 1);
  tab->view = view;
  SLLStackPush(arrangement->first_removed, tab);
  return 1;
}

internal void
rd_arrangement_discard(RD_Arrangement *arrangement)
{
  // Saving releases the root node, with every View no tab took back.
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    for(RD_ArrangementTab *tab = p->first_tab, *next = 0; tab != 0; tab = next)
    {
      next = tab->next;
      tab->prev = 0;
      SLLStackPush(arrangement->first_removed, tab);
    }
  }
  arrangement->root = &rd_nil_arrangement_panel;
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
