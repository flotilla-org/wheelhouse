// A test-only oracle (#516): the native shell's arrangement and docking
// rules as they were when the core took them over, kept to prove the core
// (crates/wheelhouse-core/src/arrangement.rs, dock.rs) gives the same
// results. Built by crates/wheelhouse-arrangement-oracle with `cc`, whose
// Hegel tests drive it beside the core. It goes once the operator has
// checked the roll (the follow-up to #516).
//
// Between the markers are verbatim copies of shell_arrangement.c, of
// shell_docking.h's types (its registry replaced by one the tests fill) and
// of shell_docking.c. Below them is the shim the tests call.
#define BUILD_CONSOLE_INTERFACE 1
#define WM_STUB 1
#include "base/base_inc.h"
#include "mdesk/mdesk.h"
#include "window_manager/window_manager_inc.h"
#include "config/config_inc.h"
#include "shell/shell_workspace_id.h"
#include "shell/shell_arrangement.h"
#include "base/base_inc.c"
#include "mdesk/mdesk.c"
#include "window_manager/window_manager_inc.c"
#include "config/config_inc.c"

//~ BEGIN verbatim: shell_arrangement.c
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

internal B32
rd_arrangement_closes_when_emptied(RD_ArrangementPanel *panel)
{
  return panel->doc_id.size == 0;
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
  if(may_close && (split ? panel->tab_count == 0 : first_shown == 0) && rd_arrangement_closes_when_emptied(panel))
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
  // A boundary pairs a panel that has space with the next that has (#486).
  if(panel == &rd_nil_arrangement_panel || panel->weight <= 0) { return; }
  while(next != &rd_nil_arrangement_panel && next->weight <= 0) { next = next->next; }
  if(next == &rd_nil_arrangement_panel) { return; }
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
  RD_ArrangementPanel *next = panel->next;
  while(next != &rd_nil_arrangement_panel && next->weight <= 0) { next = next->next; }
  if(panel->weight <= 0 || next == &rd_nil_arrangement_panel) { return; }
  rd_arrangement_resize(arrangement, id, (next->weight-panel->weight)/2, 0);
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
//~ END verbatim: shell_arrangement.c

//~ BEGIN verbatim: shell_docking.h types
// Declared View/host validity, independent of gestures and rendering.

// Restore deliberately ignores physical width: resizing is not structural repair.
#define RD_DOCK_UNMEASURED_WIDTH 3.402823466e38f

typedef enum RD_DockHostKind
{
  RD_DockHostKind_Sidebar,
  RD_DockHostKind_WorkspaceRegion,
  RD_DockHostKind_COUNT,
} RD_DockHostKind;

// Presentation belongs to the host; validity remains an independent query.
typedef enum RD_DockPresentation
{
  RD_DockPresentation_Tabs,
  RD_DockPresentation_SectionHeader,
} RD_DockPresentation;

internal RD_DockPresentation rd_dock_presentation(RD_DockHostKind host, U64 tab_count);

typedef U32 RD_ViewTraits;
enum
{
  RD_ViewTrait_Content = 1<<0,
  RD_ViewTrait_Section = 1<<1,
  RD_ViewTrait_SelectsWorkspaces = 1<<2,
  RD_ViewTrait_Singleton = 1<<3,
  RD_ViewTrait_NeedsWorkspaceSubject = 1<<4,
  // Content supplied by the root Controlled Split, not a child workspace.
  RD_ViewTrait_ControlSplitScope = 1<<6,
  // Laying it out again at another size is harmless, so its drag floater
  // shows it live rather than as its texture (#253).
  RD_ViewTrait_LiveDragPreview = 1<<7,
  // Draws a value: a Lens type of its name opens it, a watch cell or a hover
  // evaluation can embed it, and headless tests draw it as nothing.
  RD_ViewTrait_Visualizer = 1<<8,
  // A dragged expression dropped on it becomes its expression.
  RD_ViewTrait_ExpressionDrop = 1<<9,
  // Draws its evaluated expression, from its context's `legacy_eval`: the
  // evaluator escape hatch until the evaluator is retired (#313).
  RD_ViewTrait_LegacyEval = 1<<10,
  // Declares a producer version (terminal: render generation, text: content
  // hash), so shape-only workspace preservation is safe for it.
  RD_ViewTrait_VersionedSurface = 1<<11,
  // Offered by name where a View is made from an expression (the view lister).
  RD_ViewTrait_Listed = 1<<12,
  // A control view: it shows the Dashboard's arrangement, not a Slot.
  RD_ViewTrait_Control = 1<<13,
  // One of the shell's own pages (watch lists, the pending loader, getting
  // started): presses keep focus on its query bar, and floating, it has no
  // pull-out button.
  RD_ViewTrait_QueryFocus = 1<<14,
};

typedef struct RD_ViewRegistration RD_ViewRegistration;
struct RD_ViewRegistration
{
  String8 name;
  RD_ViewTraits traits;
  F32 minimum_width;
  RD_DockHostKind default_host;
};

typedef struct RD_DockHost RD_DockHost;
struct RD_DockHost
{
  RD_DockHostKind kind;
  CFG_ID controlled_split;
  CFG_ID workspace_region;
  B32 has_workspace_subject;
  F32 available_width;
  // Layout owner identity; independent of visual host kind or screen position.
  CFG_ID level;
};

typedef enum RD_DockRule
{
  RD_DockRule_Valid,
  RD_DockRule_RegisteredView,
  RD_DockRule_HostAcceptance,
  RD_DockRule_WorkspaceSubject,
  RD_DockRule_MinimumWidth,
  RD_DockRule_Singleton,
  RD_DockRule_OneControlSurface,
  RD_DockRule_SelectorOutsideSelectedRegion,
  RD_DockRule_SelectorCannotClose,
  RD_DockRule_ControlSplitLevel,
} RD_DockRule;

// Counts describe the proposed result, not the source layout. A move retains
// its existing instance; a creation adds one; a close removes one.
typedef struct RD_DockProposal RD_DockProposal;
struct RD_DockProposal
{
  RD_DockHost host;
  CFG_ID selected_workspace_region;
  U32 instances_after;
  U32 control_surfaces_after;
  B32 closing;
  // The View's existing owner, rather than the proposed destination's scope.
  CFG_ID view_level;
};

// The tests' registry, filled by oracle_set_registrations, in place of
// the Renderers' (uishell_renderers.h).
global RD_ViewRegistration rd_view_registrations[16];
global U64 rd_view_registration_count;

// A View as docking checks it: what it is, and where it is now, as the shell
// finds it by its View Address (rd_dock_view_from_address).
typedef struct RD_DockView RD_DockView;
struct RD_DockView
{
  // Whether there is such a View.
  B32 exists;
  // 0 for saved content that isn't a registered View.
  RD_ViewRegistration *registration;
  // The host it is in, and the level (layout owner) it belongs to.
  RD_DockHost source;
  CFG_ID level;
};

// How many Views of `registration` the Controlled Split `controlled_split`
// has: the shell counts the tabs of its window's arrangements.
typedef U32 RD_DockInstanceCount(CFG_ID controlled_split, RD_ViewRegistration *registration);
global RD_DockInstanceCount *rd_dock_instance_count = 0;

internal RD_ViewRegistration *rd_dock_view_from_name(String8 name);
internal RD_DockRule rd_dock_check(RD_ViewRegistration *view, RD_DockProposal proposal);
// The kind of host a panel owner's panels are in.
internal RD_DockHostKind rd_dock_host_kind_from_panel_owner(RD_PanelOwner owner);
// The level a View in `host` belongs to: its host's, or for a View of the
// root Controlled Split's own scope, that split's.
internal CFG_ID rd_dock_view_level(RD_ViewRegistration *registration, RD_DockHost host);
// Placing `view` in a panel of `destination`, the host that panel is in at
// its available width (the shell's rd_dock_host_from_panel); a host of no
// kind (RD_DockHostKind_COUNT) is no destination.
internal RD_DockRule rd_dock_placement(RD_DockView view, RD_DockHost destination);
// The production drag-target query, shared by all View drop sites.
internal B32 rd_dock_drag_target(RD_DockView view, RD_DockHost destination);
internal RD_DockRule rd_dock_creation(String8 name, RD_DockHost destination);
internal RD_DockRule rd_dock_closure(RD_DockView view);
internal String8 rd_dock_rule_message(RD_DockRule rule);
internal B32 rd_dock_can_create(String8 name, RD_DockHost destination);
internal B32 rd_dock_can_close(RD_DockView view);

//- Repair: restore, over a window's arrangements

// An arrangement a restore checks and may edit, with the host its panels
// are in.
typedef struct RD_DockDocument RD_DockDocument;
struct RD_DockDocument
{
  RD_DockDocument *next;
  RD_Arrangement *arrangement;
  RD_DockHost host;
  B32 dirty;
};

typedef struct RD_DockDocuments RD_DockDocuments;
struct RD_DockDocuments
{
  Arena *arena;
  RD_DockDocument *first;
  RD_DockDocument *last;
  // Where refused Views go by default host: the sidebar's dock, and the
  // window's first workspace (else its own panels).
  RD_DockDocument *sidebar;
  RD_DockDocument *workspace;
};

// What a View is, by its address: the shell reads its config node's kind.
typedef RD_ViewRegistration *RD_DockRegistration(RD_ViewAddress view);

internal RD_DockDocument *rd_dock_documents_add(RD_DockDocuments *documents, RD_Arrangement *arrangement, RD_DockHost host);
// The leaf a View restore moves goes to: down the first children of the
// root, which is made when there is none.
internal RD_ArrangementPanel *rd_dock_restore_leaf(RD_Arrangement *arrangement);
// Repairs a window's arrangements, every one in `documents`: one copy of
// each singleton, and each View its host refuses moved to its default host,
// keeping its address. Marks the documents it edits dirty, and returns the
// sidebar sections it moved, which the sidebar places by their hints once
// committed (uishell_sidebar_store.c).
internal RD_ViewAddressList rd_dock_restore(RD_DockDocuments *documents, RD_DockRegistration *registration);

// The body width `destination` (or the panel a split there makes) would have
// once a drop of `view` completes, measured on a copy; `shown` is the rule
// the drop commands settle its source with. With no View (nil) it measures
// the split alone.
internal F32 rd_dock_moving_width(RD_Arrangement *arrangement, RD_PanelID destination, RD_ViewAddress view,
                                 RD_ArrangementTabRule *shown, Rng2F32 area, Dir2 dir, F32 inset);
//~ END verbatim: shell_docking.h types

//~ BEGIN verbatim: shell_docking.c
internal RD_DockPresentation
rd_dock_presentation(RD_DockHostKind host, U64 tab_count)
{
  // A sidebar section's header holds all its panel's Views (sidebar-headers.md).
  if(host == RD_DockHostKind_Sidebar) { return RD_DockPresentation_SectionHeader; }
  return RD_DockPresentation_Tabs;
}

// One checker for drag feedback, command execution, and layout restore.
internal RD_ViewRegistration *
rd_dock_view_from_name(String8 name)
{
  for(U64 i = 0; i < ArrayCount(rd_view_registrations); i++)
  {
    if(str8_match(name, rd_view_registrations[i].name, 0)) { return &rd_view_registrations[i]; }
  }
  return 0;
}

typedef struct RD_DockAcceptance RD_DockAcceptance;
struct RD_DockAcceptance { RD_ViewTraits any_of, forbidden; };
read_only global RD_DockAcceptance rd_dock_acceptance[RD_DockHostKind_COUNT] =
{
  {RD_ViewTrait_Section, 0},
  {RD_ViewTrait_Content, 0},
};

internal RD_DockRule
rd_dock_check(RD_ViewRegistration *view, RD_DockProposal p)
{
  // Unknown saved content can be removed, but has no placement traits.
  if(view == 0) { return p.closing ? RD_DockRule_Valid : RD_DockRule_RegisteredView; }
  RD_ViewTraits traits = view->traits;
  if(p.closing && (traits & RD_ViewTrait_SelectsWorkspaces)) { return RD_DockRule_SelectorCannotClose; }
  if(p.host.controlled_split && p.control_surfaces_after != 1) { return RD_DockRule_OneControlSurface; }
  if(p.closing) { return RD_DockRule_Valid; }
  if((traits & RD_ViewTrait_SelectsWorkspaces) && p.selected_workspace_region &&
     p.selected_workspace_region == p.host.workspace_region) { return RD_DockRule_SelectorOutsideSelectedRegion; }
  if((U32)p.host.kind >= RD_DockHostKind_COUNT) { return RD_DockRule_HostAcceptance; }
  if(p.view_level != p.host.level) { return RD_DockRule_ControlSplitLevel; }
  RD_DockAcceptance acceptance = rd_dock_acceptance[p.host.kind];
  if((acceptance.any_of && !(traits & acceptance.any_of)) || (traits & acceptance.forbidden)) { return RD_DockRule_HostAcceptance; }
  if((traits & RD_ViewTrait_NeedsWorkspaceSubject) && !p.host.has_workspace_subject) { return RD_DockRule_WorkspaceSubject; }
  if(p.host.available_width < view->minimum_width) { return RD_DockRule_MinimumWidth; }
  if((traits & RD_ViewTrait_Singleton) && p.instances_after != 1) { return RD_DockRule_Singleton; }
  return RD_DockRule_Valid;
}


internal RD_DockHostKind
rd_dock_host_kind_from_panel_owner(RD_PanelOwner owner)
{
  if(owner.kind == RD_PanelOwnerKind_Sidebar) { return RD_DockHostKind_Sidebar; }
  return RD_DockHostKind_WorkspaceRegion;
}

// These declarations use the root split's core state, not the subject
// of whichever child workspace happens to contain an old placement.
internal CFG_ID
rd_dock_view_level(RD_ViewRegistration *registration, RD_DockHost host)
{
  if(registration && (registration->traits & RD_ViewTrait_ControlSplitScope)) { return host.controlled_split; }
  return host.level;
}

internal RD_DockRule
rd_dock_placement(RD_DockView view, RD_DockHost destination)
{
  if(!view.exists || destination.kind >= RD_DockHostKind_COUNT) { return RD_DockRule_RegisteredView; }
  RD_DockHost source = view.source;
  RD_DockProposal p = {0};
  p.host = destination;
  p.selected_workspace_region = source.controlled_split;
  RD_ViewRegistration *registration = view.registration;
  p.view_level = view.level;
  p.instances_after = 1;
  p.control_surfaces_after = 1; // TODO(#164): derive counts from nested Controlled Splits.
  // The current root Control Surface is implicit.
  if(registration && (registration->traits & RD_ViewTrait_Singleton))
  {
    p.instances_after = rd_dock_instance_count ? rd_dock_instance_count(destination.controlled_split, registration) : 1;
    if(source.controlled_split != p.host.controlled_split) { p.instances_after++; }
  }
  // Moving a selector to another Controlled Split would remove the source's
  // only Control Surface and add a second to the destination.
  if(registration && (registration->traits & RD_ViewTrait_SelectsWorkspaces) &&
     source.controlled_split != p.host.controlled_split) { p.control_surfaces_after = 2; }
  return rd_dock_check(registration, p);
}

internal B32
rd_dock_drag_target(RD_DockView view, RD_DockHost destination)
{
  return rd_dock_placement(view, destination) == RD_DockRule_Valid;
}

internal RD_DockRule
rd_dock_creation(String8 name, RD_DockHost destination)
{
  RD_ViewRegistration *view = rd_dock_view_from_name(name);
  if(!view) { return RD_DockRule_RegisteredView; }
  RD_DockProposal p = {0};
  p.host = destination;
  p.view_level = (view->traits & RD_ViewTrait_ControlSplitScope) ? destination.controlled_split : p.host.level;
  p.selected_workspace_region = p.host.controlled_split;
  p.instances_after = (view->traits & RD_ViewTrait_Singleton) && rd_dock_instance_count ? rd_dock_instance_count(destination.controlled_split, view)+1 : 1;
  // TODO(#164): derive this with placement/closure counts from nested splits.
  // A creation must not add another selector to the implicit root surface.
  p.control_surfaces_after = view && (view->traits & RD_ViewTrait_SelectsWorkspaces) ? 2 : 1;
  return destination.kind < RD_DockHostKind_COUNT ? rd_dock_check(view, p) : RD_DockRule_HostAcceptance;
}

internal RD_DockRule
rd_dock_closure(RD_DockView view)
{
  if(!view.exists) { return RD_DockRule_RegisteredView; }
  RD_DockProposal p = {0};
  p.host = view.source;
  p.closing = 1;
  // TODO(#164): derive this with placement/creation counts from nested splits.
  p.control_surfaces_after = 1;
  return rd_dock_check(view.registration, p);
}

internal B32
rd_dock_can_create(String8 name, RD_DockHost destination)
{ return rd_dock_creation(name, destination) == RD_DockRule_Valid; }

internal B32
rd_dock_can_close(RD_DockView view)
{ return rd_dock_closure(view) == RD_DockRule_Valid; }

internal String8
rd_dock_rule_message(RD_DockRule rule)
{
  switch(rule)
  {
    case RD_DockRule_Valid: return str8_lit("valid placement");
    case RD_DockRule_RegisteredView: return str8_lit("the View type is not registered");
    case RD_DockRule_HostAcceptance: return str8_lit("the host does not accept this View");
    case RD_DockRule_WorkspaceSubject: return str8_lit("a Workspace Subject is required");
    case RD_DockRule_MinimumWidth: return str8_lit("the target is narrower than the View's minimum width");
    case RD_DockRule_Singleton: return str8_lit("only one instance of this View is allowed");
    case RD_DockRule_OneControlSurface: return str8_lit("a Controlled Split must retain exactly one Control Surface");
    case RD_DockRule_SelectorOutsideSelectedRegion: return str8_lit("a selector cannot occupy its selected Workspace Region");
    case RD_DockRule_SelectorCannotClose: return str8_lit("the workspace selector cannot be closed");
    case RD_DockRule_ControlSplitLevel: return str8_lit("the View belongs to a different Controlled Split level");
  }
  return str8_lit("invalid docking rule");
}

////////////////////////////////
//~ Repair

internal RD_DockDocument *
rd_dock_documents_add(RD_DockDocuments *documents, RD_Arrangement *arrangement, RD_DockHost host)
{
  RD_DockDocument *document = push_array(documents->arena, RD_DockDocument, 1);
  document->arrangement = arrangement;
  document->host = host;
  SLLQueuePush(documents->first, documents->last, document);
  return document;
}

// A split root cannot hold tabs.
internal RD_ArrangementPanel *
rd_dock_restore_leaf(RD_Arrangement *arrangement)
{
  if(arrangement->root == &rd_nil_arrangement_panel) { rd_arrangement_clear(arrangement); }
  RD_ArrangementPanel *leaf = arrangement->root;
  for(; leaf->first != &rd_nil_arrangement_panel; leaf = leaf->first) {}
  return leaf;
}

// A View as restore found it: where it is, and whether it was its panel's
// Selected View there.
typedef struct RD_DockRestoreView RD_DockRestoreView;
struct RD_DockRestoreView
{
  RD_DockRestoreView *next;
  RD_ViewAddress view;
  RD_ViewRegistration *registration;
  RD_DockDocument *document;
  RD_PanelID panel;
  B32 selected;
  B32 gone;
};

// Whether the checker accepts `view` where it is, assessed with one
// instance so that duplicate cardinality does not mask validity.
internal B32
rd_dock_restore_valid(RD_DockRestoreView *view)
{
  RD_DockProposal p = {0};
  p.host = view->document->host;
  p.selected_workspace_region = p.host.controlled_split;
  p.instances_after = p.control_surfaces_after = 1;
  p.view_level = rd_dock_view_level(view->registration, p.host);
  return rd_dock_check(view->registration, p) == RD_DockRule_Valid;
}

internal RD_ViewAddressList
rd_dock_restore(RD_DockDocuments *documents, RD_DockRegistration *registration)
{
  Arena *arena = documents->arena;
  RD_ViewAddressList pending = {0};
  RD_DockRestoreView *first = 0, *last = 0;
  for(RD_DockDocument *d = documents->first; d != 0; d = d->next)
  {
    RD_Arrangement *a = d->arrangement;
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    {
      for(RD_ArrangementTab *tab = p->first_tab; tab != 0; tab = tab->next)
      {
        RD_DockRestoreView *v = push_array(arena, RD_DockRestoreView, 1);
        v->view = tab->view;
        v->registration = registration(tab->view);
        v->document = d;
        v->panel = p->id;
        v->selected = rd_view_address_match(p->selected, tab->view);
        SLLQueuePush(first, last, v);
      }
    }
  }

  // One copy of each singleton: the first in tree order, unless a later one
  // is where it is valid and the first is not.
  RD_DockRestoreView *keepers[ArrayCount(rd_view_registrations)] = {0};
  for(RD_DockRestoreView *v = first; v != 0; v = v->next)
  {
    if(!v->registration || !(v->registration->traits & RD_ViewTrait_Singleton)) { continue; }
    RD_DockRestoreView **keeper = &keepers[v->registration-rd_view_registrations];
    if(!*keeper || (!rd_dock_restore_valid(*keeper) && rd_dock_restore_valid(v))) { *keeper = v; }
  }
  for(RD_DockRestoreView *v = first; v != 0; v = v->next)
  {
    if(!v->registration || !(v->registration->traits & RD_ViewTrait_Singleton) || keepers[v->registration-rd_view_registrations] == v) { continue; }
    rd_arrangement_remove_tab(v->document->arrangement, v->view);
    v->document->dirty = 1;
    v->gone = 1;
  }

  // A View the checker refuses where it is moves to the end of its default
  // host's first leaf, keeping its address. A sidebar section moved so is
  // placed by its hints once the sidebar commits (`pending`), and the panel
  // it leaves goes with it then.
  for(RD_DockRestoreView *v = first; v != 0; v = v->next)
  {
    // Content that isn't a registered View has no default host: it stays.
    if(v->gone || !v->registration || rd_dock_restore_valid(v)) { continue; }
    RD_DockDocument *destination = v->registration->default_host == RD_DockHostKind_Sidebar ? documents->sidebar : documents->workspace;
    if(destination == 0) { continue; }
    if(str8_match(v->registration->name, str8_lit("sidebar_section"), 0))
    {
      rd_view_address_list_push(arena, &pending, v->view);
      RD_ArrangementPanel *source = rd_arrangement_panel_from_id(v->document->arrangement, v->panel);
      if(source != &rd_nil_arrangement_panel && !source->restore_emptied) { source->restore_emptied = 1; v->document->dirty = 1; }
    }
    RD_ArrangementPanel *leaf = rd_dock_restore_leaf(destination->arrangement);
    // A default can still lack context (for example a required subject).
    // Moving a View already there would only reorder it.
    if(v->document == destination && v->panel == leaf->id) { continue; }
    if(v->document != destination)
    {
      rd_arrangement_detach_tab(v->document->arrangement, v->view);
      v->document->dirty = 1;
    }
    // The destination keeps its Selected View unless this one was selected.
    rd_arrangement_insert_tab(destination->arrangement, v->view, leaf->id, leaf->last_tab ? leaf->last_tab->view : rd_nil_view_address, v->selected);
    destination->dirty = 1;
    v->document = destination;
    v->panel = leaf->id;
  }
  return pending;
}

// The proposal runs the commands' drop on a copy, which closes an emptied
// source as they do. A split destination that the closure collapses is
// measured before it, as the space its surviving panel takes over (the whole
// area for the root). Tabs occupy vertical chrome only; the panel inset
// consumes both X edges.
internal F32
rd_dock_moving_width(RD_Arrangement *arrangement, RD_PanelID destination, RD_ViewAddress view,
                     RD_ArrangementTabRule *shown, Rng2F32 area, Dir2 dir, F32 inset)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *proposal = rd_arrangement_copy(scratch.arena, arrangement);
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(proposal, rd_arrangement_drop(proposal, view, destination, dir, rd_nil_view_address, shown).panel);
  if(panel == &rd_nil_arrangement_panel)
  {
    proposal = rd_arrangement_copy(scratch.arena, arrangement);
    panel = rd_arrangement_panel_from_id(proposal, dir == Dir2_Invalid ? destination : rd_arrangement_split(proposal, destination, dir));
  }
  Rng2F32 rect = rd_arrangement_rect(proposal, area, panel);
  F32 result = panel == &rd_nil_arrangement_panel ? 0 : Max(0.f, round_f32(rect.x1-inset)-round_f32(rect.x0+inset));
  scratch_end(scratch);
  return result;
}
//~ END verbatim: shell_docking.c

#include "shell/tests/arrangement_oracle_shim.c"
