// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal CFG_Node *
cfg_window_from_cfg(CFG_Node *cfg)
{
  CFG_Node *result = &cfg_nil_node;
  for(CFG_Node *c = cfg; c != &cfg_nil_node; c = c->parent)
  {
    if(c->parent->parent == cfg_node_root() && str8_match(c->string, str8_lit("window"), 0))
    {
      result = c;
      break;
    }
  }
  return result;
}

// A panel's children are its sub-panels, its Views and these options. `id`
// is the panel's stable ID (shell_arrangement.h); `section_hint_cleanup`
// marks a panel restore took a sidebar section out of, for the sidebar's
// reconciliation to remove if nothing is left in it.
internal B32
cfg_panel_child_is_option(String8 name)
{
  return str8_match(name, str8_lit("tabs_on_bottom"), 0) || str8_match(name, str8_lit("section_collapsed"), 0) ||
    str8_match(name, str8_lit("id"), 0) || str8_match(name, str8_lit("section_hint_cleanup"), 0) ||
    // The ID a provider's document gave the panel (uishell_workspace_store.c).
    str8_match(name, str8_lit("doc_id"), 0);
}

internal CFG_PanelNodeRec
cfg_panel_node_rec__depth_first(CFG_PanelNode *root, CFG_PanelNode *panel, U64 sib_off, U64 child_off)
{
  CFG_PanelNodeRec rec = {&cfg_nil_panel_node};
  if(*MemberFromOffset(CFG_PanelNode **, panel, child_off) != &cfg_nil_panel_node)
  {
    rec.next = *MemberFromOffset(CFG_PanelNode **, panel, child_off);
    rec.push_count += 1;
  }
  else for(CFG_PanelNode *p = panel; p != &cfg_nil_panel_node && p != root; p = p->parent, rec.pop_count += 1)
  {
    if(*MemberFromOffset(CFG_PanelNode **, p, sib_off) != &cfg_nil_panel_node)
    {
      rec.next = *MemberFromOffset(CFG_PanelNode **, p, sib_off);
      break;
    }
  }
  return rec;
}

internal CFG_PanelNode *
cfg_panel_node_from_tree_cfg(CFG_PanelNode *root, CFG_Node *cfg)
{
  CFG_PanelNode *result = &cfg_nil_panel_node;
  for(CFG_PanelNode *p = root;
      p != &cfg_nil_panel_node;
      p = cfg_panel_node_rec__depth_first_pre(root, p).next)
  {
    if(p->cfg == cfg)
    {
      result = p;
      break;
    }
  }
  return result;
}

internal Rng2F32
cfg_target_rect_from_panel_node_child(Rng2F32 parent_rect, CFG_PanelNode *parent, CFG_PanelNode *panel)
{
  Rng2F32 rect = parent_rect;
  if(parent != &cfg_nil_panel_node)
  {
    Vec2F32 parent_rect_size = dim_2f32(parent_rect);
    Axis2 axis = parent->split_axis;
    rect.p1.v[axis] = rect.p0.v[axis];
    for(CFG_PanelNode *child = parent->first; child != &cfg_nil_panel_node; child = child->next)
    {
      rect.p1.v[axis] += parent_rect_size.v[axis] * child->pct_of_parent;
      if(child == panel)
      {
        break;
      }
      rect.p0.v[axis] = rect.p1.v[axis];
    }
    //rect.p0.v[axis] += parent_rect_size.v[axis] * panel->off_pct_of_parent.v[axis];
    //rect.p0.v[axis2_flip(axis)] += parent_rect_size.v[axis2_flip(axis)] * panel->off_pct_of_parent.v[axis2_flip(axis)];
  }
  rect.x0 = round_f32(rect.x0);
  rect.x1 = round_f32(rect.x1);
  rect.y0 = round_f32(rect.y0);
  rect.y1 = round_f32(rect.y1);
  return rect;
}

internal Rng2F32
cfg_target_rect_from_panel_node(Rng2F32 root_rect, CFG_PanelNode *root, CFG_PanelNode *panel)
{
  Temp scratch = scratch_begin(0, 0);
  
  // rjf: count ancestors
  U64 ancestor_count = 0;
  for(CFG_PanelNode *p = panel->parent; p != &cfg_nil_panel_node; p = p->parent)
  {
    ancestor_count += 1;
  }
  
  // rjf: gather ancestors
  CFG_PanelNode **ancestors = push_array(scratch.arena, CFG_PanelNode *, ancestor_count);
  {
    U64 ancestor_idx = 0;
    for(CFG_PanelNode *p = panel->parent; p != &cfg_nil_panel_node; p = p->parent)
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
    CFG_PanelNode *ancestor = ancestors[ancestor_idx];
    CFG_PanelNode *parent = ancestor->parent;
    if(parent != &cfg_nil_panel_node)
    {
      parent_rect = cfg_target_rect_from_panel_node_child(parent_rect, parent, ancestor);
    }
  }
  
  // rjf: calculate final rect
  Rng2F32 rect = cfg_target_rect_from_panel_node_child(parent_rect, panel->parent, panel);
  
  scratch_end(scratch);
  return rect;
}
