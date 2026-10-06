// Capture before split/close mutates or releases the root's configuration.
internal RD_DockLayoutKeys
rd_dock_layout_keys(Arena *arena, CFG_Node *root)
{
  RD_DockLayoutKeys result = {root->parent, push_str8_copy(arena, root->string)};
  result.axis_key = str8_match(result.root_name, RD_DOCK_SIDEBAR_ROOT, 0) ?
    str8_lit("control_views_split_x") : str8_lit("split_x");
  return result;
}

internal RD_DockPresentation
rd_dock_presentation(RD_DockHostKind host, U64 tab_count)
{
  if(host == RD_DockHostKind_Sidebar)
  { return tab_count == 1 ? RD_DockPresentation_SectionHeader : RD_DockPresentation_CompactTabs; }
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
  {0, RD_ViewTrait_NeedsHost},
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

internal RD_DockHost
rd_dock_host_from_cfg(CFG_Node *cfg, F32 width)
{
  RD_DockHost host = {RD_DockHostKind_WorkspaceRegion, 0, 0, 0, width};
  for(CFG_Node *c = cfg; c != &cfg_nil_node; c = c->parent)
  {
    // A layout's owner identifies its level. Legacy window.panels is a child
    // workspace, even though it predates an explicit workspace config node.
    if(!host.level)
    {
      if(str8_match(c->string, RD_DOCK_SIDEBAR_ROOT, 0) ||
         str8_match(c->string, str8_lit("floating_panels"), 0))
      { host.level = c->parent->id; }
      else if(str8_match(c->string, str8_lit("panels"), 0))
      {
        host.level = str8_match(c->parent->string, str8_lit("workspace"), 0) ? c->parent->id : c->id;
      }
    }
    if(str8_match(c->string, RD_DOCK_SIDEBAR_ROOT, 0)) { host.kind = RD_DockHostKind_Sidebar; }
    if(str8_match(c->string, str8_lit("floating_panels"), 0)) { host.kind = RD_DockHostKind_FloatingPanel; }
    if(str8_match(c->string, str8_lit("workspace"), 0))
    {
      host.has_workspace_subject = cfg_node_child_from_string(c, str8_lit("subject")) != &cfg_nil_node;
    }
    if(str8_match(c->string, str8_lit("window"), 0))
    {
      host.controlled_split = c->id;
      // The current root Controlled Split owns every Workspace Mount in this
      // window. Its region identity stays fixed when Visible Workspace changes.
      if(host.kind == RD_DockHostKind_WorkspaceRegion) { host.workspace_region = c->id; }
      break;
    }
  }
  return host;
}

internal B32
rd_dock_is_container(CFG_Node *cfg)
{
  if(str8_match(cfg->string, str8_lit("workspace"), 0) ||
     str8_match(cfg->string, str8_lit("panels"), 0) ||
     str8_match(cfg->string, RD_DOCK_SIDEBAR_ROOT, 0) ||
     str8_match(cfg->string, str8_lit("floating_panels"), 0)) { return 1; }
  // Match a single MD numeric token without allocating a token array. Numeric
  // tokens start with a digit, .digit or -digit, then contain alnum, _ or . .
  String8 name = cfg->string;
  if(name.size == 0) { return 0; }
  B32 start = char_is_digit(name.str[0], 10) ||
    (name.size > 1 && (name.str[0] == '.' || name.str[0] == '-') && char_is_digit(name.str[1], 10));
  if(!start) { return 0; }
  for(U64 i = 1; i < name.size; i++)
  { if(!char_is_alpha(name.str[i]) && !char_is_digit(name.str[i], 10) && name.str[i] != '_' && name.str[i] != '.') { return 0; } }
  return 1;
}

internal CFG_Node *
rd_dock_window(CFG_Node *cfg)
{
  for(CFG_Node *c = cfg; c != &cfg_nil_node; c = c->parent)
  { if(str8_match(c->string, str8_lit("window"), 0)) { return c; } }
  return &cfg_nil_node;
}

internal U32
rd_dock_instances(CFG_Node *container, RD_ViewRegistration *registration)
{
  U32 count = 0;
  for(CFG_Node *c = container->first; c != &cfg_nil_node; c = c->next)
  {
    RD_ViewRegistration *view = rd_dock_view_from_name(c->string);
    if(view == registration) { count++; }
    else if(!view && rd_dock_is_container(c)) { count += rd_dock_instances(c, registration); }
  }
  return count;
}

// These declarations use the root split's Andamento state, not the subject
// of whichever child workspace happens to contain an old saved placement.
internal CFG_ID
rd_dock_view_level(RD_ViewRegistration *registration, CFG_Node *view)
{
  if(registration && (registration->traits & RD_ViewTrait_ControlSplitScope))
  { return rd_dock_window(view)->id; }
  return rd_dock_host_from_cfg(view->parent, RD_DOCK_UNMEASURED_WIDTH).level;
}

internal RD_DockRule
rd_dock_placement(CFG_Node *view, CFG_Node *destination, F32 width)
{
  if(view == &cfg_nil_node || destination == &cfg_nil_node) { return RD_DockRule_RegisteredView; }
  RD_DockHost source = rd_dock_host_from_cfg(view->parent, RD_DOCK_UNMEASURED_WIDTH);
  RD_DockProposal p = {0};
  p.host = rd_dock_host_from_cfg(destination, width);
  p.selected_workspace_region = source.controlled_split;
  RD_ViewRegistration *registration = rd_dock_view_from_name(view->string);
  p.view_level = rd_dock_view_level(registration, view);
  p.instances_after = 1;
  p.control_surfaces_after = 1; // TODO(#164): derive counts from nested Controlled Splits.
  // The current root Control Surface is implicit.
  if(registration && (registration->traits & RD_ViewTrait_Singleton))
  {
    p.instances_after = rd_dock_instances(rd_dock_window(destination), registration);
    if(source.controlled_split != p.host.controlled_split) { p.instances_after++; }
  }
  // Moving a selector to another Controlled Split would remove the source's
  // only Control Surface and add a second to the destination.
  if(registration && (registration->traits & RD_ViewTrait_SelectsWorkspaces) &&
     source.controlled_split != p.host.controlled_split) { p.control_surfaces_after = 2; }
  return rd_dock_check(registration, p);
}

internal B32
rd_dock_drag_target(CFG_Node *view, CFG_Node *destination, F32 width)
{
  return rd_dock_placement(view, destination, width) == RD_DockRule_Valid;
}

internal RD_DockRule
rd_dock_creation(String8 name, CFG_Node *destination)
{
  RD_ViewRegistration *view = rd_dock_view_from_name(name);
  if(!view) { return RD_DockRule_RegisteredView; }
  RD_DockProposal p = {0};
  p.host = rd_dock_host_from_cfg(destination, RD_DOCK_UNMEASURED_WIDTH);
  p.view_level = (view->traits & RD_ViewTrait_ControlSplitScope) ? rd_dock_window(destination)->id : p.host.level;
  p.selected_workspace_region = p.host.controlled_split;
  p.instances_after = (view->traits & RD_ViewTrait_Singleton) ? rd_dock_instances(rd_dock_window(destination), view)+1 : 1;
  // TODO(#164): derive this with placement/closure counts from nested splits.
  // A creation must not add another selector to the implicit root surface.
  p.control_surfaces_after = view && (view->traits & RD_ViewTrait_SelectsWorkspaces) ? 2 : 1;
  return destination != &cfg_nil_node ? rd_dock_check(view, p) : RD_DockRule_HostAcceptance;
}

internal RD_DockRule
rd_dock_closure(CFG_Node *view)
{
  if(view == &cfg_nil_node) { return RD_DockRule_RegisteredView; }
  RD_DockProposal p = {0};
  p.host = rd_dock_host_from_cfg(view->parent, RD_DOCK_UNMEASURED_WIDTH);
  p.closing = 1;
  // TODO(#164): derive this with placement/creation counts from nested splits.
  p.control_surfaces_after = 1;
  return rd_dock_check(rd_dock_view_from_name(view->string), p);
}

internal B32
rd_dock_can_create(String8 name, CFG_Node *destination)
{ return rd_dock_creation(name, destination) == RD_DockRule_Valid; }

internal B32
rd_dock_can_close(CFG_Node *view)
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

// A default can still lack context (for example a required subject). Moving
// a View already there would only reorder it.
internal void
rd_dock_restore_move(CFG_State *state, CFG_Node *view, CFG_Node *fallback)
{
  if(view->parent == fallback) { return; }
  cfg_node_unhook(state, view->parent, view);
  cfg_node_insert_child(state, fallback, fallback->last, view);
}

// Restore walks only layout containers, never View settings (whose keys can
// also be View names). Preserve the View node, settings and identity on fallback.
internal void
rd_dock_restore_container(CFG_State *state, CFG_Node *window, CFG_Node *container)
{
  for(CFG_Node *c = container->first, *next; c != &cfg_nil_node; c = next)
  {
    next = c->next;
    RD_ViewRegistration *view = rd_dock_view_from_name(c->string);
    if(view)
    {
      if(rd_dock_placement(c, container, RD_DOCK_UNMEASURED_WIDTH) != RD_DockRule_Valid)
      {
        // Region-specific defaults arrive with the native snapshot. Preserve
        // the need to resolve those hints after this generic safety fallback.
        if(str8_match(c->string, str8_lit("sidebar_section"), 0))
        {
          cfg_node_child_from_string_or_alloc(state, c, str8_lit("section_hint_pending"));
          cfg_node_child_from_string_or_alloc(state, container, str8_lit("section_hint_cleanup"));
        }
        CFG_Node *fallback = &cfg_nil_node;
        if(view->default_host == RD_DockHostKind_Sidebar)
        { fallback = cfg_node_child_from_string_or_alloc(state, window, RD_DOCK_SIDEBAR_ROOT); }
        else
        {
          CFG_Node *owner = window;
          for(CFG_Node *n = container; n != &cfg_nil_node && n != window; n = n->parent)
          { if(str8_match(n->string, str8_lit("workspace"), 0)) { owner = n; break; } }
          if(owner == window)
          {
            CFG_Node *workspace = cfg_node_child_from_string(window, str8_lit("workspace"));
            if(workspace != &cfg_nil_node) { owner = workspace; }
          }
          fallback = cfg_node_child_from_string_or_alloc(state, owner, str8_lit("panels"));
        }
        // A split root cannot hold tabs. Choose a leaf in either host kind.
        for(;;)
        {
          CFG_Node *child = &cfg_nil_node;
          for(CFG_Node *n = fallback->first; n != &cfg_nil_node; n = n->next)
          { if(!rd_dock_view_from_name(n->string) && rd_dock_is_container(n)) { child = n; break; } }
          if(child == &cfg_nil_node) { break; }
          fallback = child;
        }
        rd_dock_restore_move(state, c, fallback);
      }
    }
    else if(rd_dock_is_container(c))
    { rd_dock_restore_container(state, window, c); }
  }
}

// Prefer a valid saved placement; tree order breaks ties. Assess placement
// with one proposed instance so duplicate cardinality does not mask validity.
internal B32
rd_dock_saved_placement_valid(CFG_Node *view)
{
  RD_DockProposal p = {0};
  p.host = rd_dock_host_from_cfg(view->parent, RD_DOCK_UNMEASURED_WIDTH);
  p.selected_workspace_region = p.host.controlled_split;
  p.instances_after = p.control_surfaces_after = 1;
  RD_ViewRegistration *registration = rd_dock_view_from_name(view->string);
  p.view_level = rd_dock_view_level(registration, view);
  return rd_dock_check(registration, p) == RD_DockRule_Valid;
}

internal void
rd_dock_choose_singletons(CFG_Node *container, CFG_Node **keepers)
{
  for(CFG_Node *c = container->first; c != &cfg_nil_node; c = c->next)
  {
    RD_ViewRegistration *view = rd_dock_view_from_name(c->string);
    if(view && (view->traits & RD_ViewTrait_Singleton))
    {
      U64 index = (U64)(view-rd_view_registrations);
      if(!keepers[index] || (!rd_dock_saved_placement_valid(keepers[index]) && rd_dock_saved_placement_valid(c)))
      { keepers[index] = c; }
    }
    else if(!view && rd_dock_is_container(c)) { rd_dock_choose_singletons(c, keepers); }
  }
}

internal void
rd_dock_prune_singletons(CFG_State *state, CFG_Node *container, CFG_Node **keepers)
{
  for(CFG_Node *c = container->first, *next; c != &cfg_nil_node; c = next)
  {
    next = c->next;
    RD_ViewRegistration *view = rd_dock_view_from_name(c->string);
    if(view && (view->traits & RD_ViewTrait_Singleton))
    {
      if(keepers[view-rd_view_registrations] != c) { cfg_node_release(state, c); }
    }
    else if(!view && rd_dock_is_container(c)) { rd_dock_prune_singletons(state, c, keepers); }
  }
}

internal void
rd_dock_restore_window(CFG_State *state, CFG_Node *window)
{
  CFG_Node *keepers[ArrayCount(rd_view_registrations)] = {0};
  rd_dock_choose_singletons(window, keepers);
  rd_dock_prune_singletons(state, window, keepers);
  rd_dock_restore_container(state, window, window);
}

// Split commands store proportions with the config formatter's precision.
// Match the %f writes in split_panel (new_cfg and redistributed child pct).
// Reuse it so rounding at pixel boundaries matches the committed tree.
internal F32
rd_dock_allocated_fraction(F32 fraction)
{
  Temp scratch = scratch_begin(0, 0);
  F32 result = (F32)f64_from_str8(push_str8f(scratch.arena, "%f", fraction));
  scratch_end(scratch);
  return result;
}

// A hand-edited source may consume the whole parent despite siblings. Give
// the survivors equal allocations when the normal denominator is nonpositive;
// commands and geometry must use the same finite recovery policy.
internal F32
rd_dock_remaining_fraction(F32 fraction, F32 removed, U64 count)
{
  Assert(count > 0); // Source closure always leaves at least one sibling.
  return rd_dock_allocated_fraction(removed < 1.f ? fraction/(1.f-removed) : 1.f/Max(count, 1));
}

// Compute the new leaf's settled body width using the same allocation as
// split_panel: insert a sibling into a matching parent, otherwise bisect.
// Tabs occupy vertical chrome only; the panel inset consumes both X edges.
internal F32
rd_dock_resulting_width(CFG_PanelNode *root, CFG_PanelNode *panel,
                        Rng2F32 area, Dir2 dir, F32 inset)
{
  if(panel == &cfg_nil_panel_node) { return 0; }
  Rng2F32 rect = cfg_target_rect_from_panel_node(area, root, panel);
  if(dir != Dir2_Invalid)
  {
    Axis2 axis = axis2_from_dir2(dir);
    Side side = side_from_dir2(dir);
    CFG_PanelNode *parent = panel->parent;
    if(parent != &cfg_nil_panel_node && parent->split_axis == axis)
    {
      // The root is passed to its children unrounded by the layout walker.
      rect = parent == root ? area : cfg_target_rect_from_panel_node(area, root, parent);
      F32 start = rect.p0.v[axis], size = dim_2f32(rect).v[axis];
      F32 scale = (F32)parent->child_count/(parent->child_count+1);
      for(CFG_PanelNode *child = parent->first; child != &cfg_nil_panel_node; child = child->next)
      {
        if(child == panel && side == Side_Min) { break; }
        start += size*rd_dock_allocated_fraction(child->pct_of_parent*scale);
        if(child == panel) { break; }
      }
      rect.p0.v[axis] = round_f32(start);
      rect.p1.v[axis] = round_f32(start + size*rd_dock_allocated_fraction(1.f/(parent->child_count+1)));
    }
    else
    {
      if(panel == root) { rect = area; }
      F32 middle = round_f32((rect.p0.v[axis]+rect.p1.v[axis])*0.5f);
      if(side == Side_Min) { rect.p1.v[axis] = middle; }
      else { rect.p0.v[axis] = middle; }
    }
  }
  rect.x0 = round_f32(rect.x0); rect.x1 = round_f32(rect.x1);
  return Max(0.f, round_f32(rect.x1-inset)-round_f32(rect.x0+inset));
}

// Copy only layout nodes: proposals never mutate configuration or the frame's
// shared tree. Tab/config identities stay borrowed for lookup.
// Copy saved split chains without growing the C call stack. Tabs/config remain
// borrowed and immutable; only layout links need independent storage.
internal CFG_PanelNode *
rd_dock_copy_tree(Arena *arena, CFG_PanelNode *node)
{
  if(node == &cfg_nil_panel_node) { return node; }
  CFG_PanelNode *root = &cfg_nil_panel_node, *parent = &cfg_nil_panel_node;
  for(CFG_PanelNode *source = node;;)
  {
    CFG_PanelNode *copy = push_array(arena, CFG_PanelNode, 1);
    *copy = *source;
    copy->parent = parent;
    copy->next = copy->prev = copy->first = copy->last = &cfg_nil_panel_node;
    if(parent == &cfg_nil_panel_node) { root = copy; }
    else { DLLPushBack_NPZ(&cfg_nil_panel_node, parent->first, parent->last, copy, next, prev); }
    if(source->first != &cfg_nil_panel_node)
    { source = source->first; parent = copy; }
    else
    {
      while(source != node && source->next == &cfg_nil_panel_node)
      { source = source->parent; parent = parent->parent; }
      if(source == node) { break; }
      source = source->next;
    }
  }
  return root;
}

// Match command order: insert first, then close the emptied source. Closing
// rescales remaining siblings, or collapses a two-child parent and flattens
// matching axes. Every written fraction uses the config formatter precision.
internal F32
rd_dock_moving_width(CFG_PanelNode *root, CFG_PanelNode *panel,
                     CFG_PanelNode *origin, Rng2F32 area, Dir2 dir, F32 inset)
{
  // split_panel deliberately retains an emptied source when it is also the
  // split target; insertion-only geometry is correct for that self split.
  if(origin == &cfg_nil_panel_node || origin == panel || origin->parent == &cfg_nil_panel_node)
  { return rd_dock_resulting_width(root, panel, area, dir, inset); }
  Temp scratch = scratch_begin(0, 0);
  CFG_PanelNode *copy = rd_dock_copy_tree(scratch.arena, root);
  origin = cfg_panel_node_from_tree_cfg(copy, origin->cfg);
  panel = cfg_panel_node_from_tree_cfg(copy, panel->cfg);
  CFG_PanelNode *target = panel;
  if(dir != Dir2_Invalid)
  {
    Axis2 axis = axis2_from_dir2(dir);
    Side side = side_from_dir2(dir);
    CFG_PanelNode *parent = panel->parent;
    target = push_array(scratch.arena, CFG_PanelNode, 1);
    *target = cfg_nil_panel_node;
    if(parent != &cfg_nil_panel_node && parent->split_axis == axis)
    {
      target->pct_of_parent = rd_dock_allocated_fraction(1.f/(parent->child_count+1));
      F32 scale = (F32)parent->child_count/(parent->child_count+1);
      for(CFG_PanelNode *c = parent->first; c != &cfg_nil_panel_node; c = c->next)
      { c->pct_of_parent = rd_dock_allocated_fraction(c->pct_of_parent*scale); }
      CFG_PanelNode *previous = side == Side_Max ? panel : panel->prev;
      DLLInsert_NPZ(&cfg_nil_panel_node, parent->first, parent->last, previous, target, next, prev);
      target->parent = parent; parent->child_count++;
    }
    else
    {
      CFG_PanelNode *split = push_array(scratch.arena, CFG_PanelNode, 1);
      *split = cfg_nil_panel_node;
      split->split_axis = axis; split->pct_of_parent = panel->pct_of_parent;
      split->parent = parent; split->child_count = 2;
      if(parent == &cfg_nil_panel_node) { copy = split; }
      else
      {
        CFG_PanelNode *previous = panel->prev;
        DLLRemove_NPZ(&cfg_nil_panel_node, parent->first, parent->last, panel, next, prev);
        DLLInsert_NPZ(&cfg_nil_panel_node, parent->first, parent->last, previous, split, next, prev);
      }
      panel->parent = target->parent = split;
      panel->pct_of_parent = target->pct_of_parent = 0.5f;
      CFG_PanelNode *first = side == Side_Min ? target : panel;
      CFG_PanelNode *last = side == Side_Min ? panel : target;
      DLLPushBack_NPZ(&cfg_nil_panel_node, split->first, split->last, first, next, prev);
      DLLPushBack_NPZ(&cfg_nil_panel_node, split->first, split->last, last, next, prev);
    }
  }
  CFG_PanelNode *parent = origin->parent;
  if(parent->child_count == 2)
  {
    CFG_PanelNode *keep = origin == parent->first ? parent->last : parent->first;
    CFG_PanelNode *grandparent = parent->parent;
    keep->pct_of_parent = rd_dock_allocated_fraction(parent->pct_of_parent);
    keep->parent = grandparent;
    if(grandparent == &cfg_nil_panel_node)
    { copy = keep; keep->next = keep->prev = &cfg_nil_panel_node; }
    else
    {
      CFG_PanelNode *previous = parent->prev;
      DLLRemove_NPZ(&cfg_nil_panel_node, grandparent->first, grandparent->last, parent, next, prev);
      if(grandparent->split_axis == keep->split_axis && keep->child_count)
      {
        grandparent->child_count += keep->child_count-1;
        for(CFG_PanelNode *c = keep->first, *next; c != &cfg_nil_panel_node; c = next)
        {
          next = c->next; c->parent = grandparent;
          // keep inherits this parent allocation above; scale by the original
          // parent percentage, exactly as close_panel scales flattened children.
          c->pct_of_parent = rd_dock_allocated_fraction(c->pct_of_parent*parent->pct_of_parent);
          DLLInsert_NPZ(&cfg_nil_panel_node, grandparent->first, grandparent->last, previous, c, next, prev);
          previous = c;
        }
      }
      else
      { DLLInsert_NPZ(&cfg_nil_panel_node, grandparent->first, grandparent->last, previous, keep, next, prev); }
    }
  }
  else
  {
    DLLRemove_NPZ(&cfg_nil_panel_node, parent->first, parent->last, origin, next, prev);
    parent->child_count--;
    for(CFG_PanelNode *c = parent->first; c != &cfg_nil_panel_node; c = c->next)
    { c->pct_of_parent = rd_dock_remaining_fraction(c->pct_of_parent, origin->pct_of_parent, parent->child_count); }
  }
  F32 result = rd_dock_resulting_width(copy, target, area, Dir2_Invalid, inset);
  scratch_end(scratch);
  return result;
}
