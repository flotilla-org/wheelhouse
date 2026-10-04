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
  if(view == 0) { return RD_DockRule_RegisteredView; }
  RD_ViewTraits traits = view->traits;
  if(p.closing && (traits & RD_ViewTrait_SelectsWorkspaces)) { return RD_DockRule_SelectorCannotClose; }
  if(p.host.controlled_split && p.control_surfaces_after != 1) { return RD_DockRule_OneControlSurface; }
  if(p.closing) { return RD_DockRule_Valid; }
  if((traits & RD_ViewTrait_SelectsWorkspaces) && p.selected_workspace_region &&
     p.selected_workspace_region == p.host.workspace_region) { return RD_DockRule_SelectorOutsideSelectedRegion; }
  if((U32)p.host.kind >= RD_DockHostKind_COUNT) { return RD_DockRule_HostAcceptance; }
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
    if(str8_match(c->string, str8_lit("control_views"), 0)) { host.kind = RD_DockHostKind_Sidebar; }
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
     str8_match(cfg->string, str8_lit("control_views"), 0) ||
     str8_match(cfg->string, str8_lit("floating_panels"), 0)) { return 1; }
  // Match the panel parser's numeric split children, including signed values.
  Temp scratch = scratch_begin(0, 0);
  MD_TokenizeResult tokens = md_tokenize_from_text(scratch.arena, cfg->string);
  B32 numeric = tokens.tokens.count == 1 && (tokens.tokens.v[0].flags & MD_TokenFlag_Numeric);
  scratch_end(scratch);
  return numeric;
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

internal RD_DockRule
rd_dock_placement(CFG_Node *view, CFG_Node *destination, F32 width)
{
  if(view == &cfg_nil_node || destination == &cfg_nil_node) { return RD_DockRule_RegisteredView; }
  RD_DockHost source = rd_dock_host_from_cfg(view->parent, RD_DOCK_UNMEASURED_WIDTH);
  RD_DockProposal p = {0};
  p.host = rd_dock_host_from_cfg(destination, width);
  p.selected_workspace_region = source.controlled_split;
  RD_ViewRegistration *registration = rd_dock_view_from_name(view->string);
  p.instances_after = 1;
  p.control_surfaces_after = 1; // the root Control Surface is currently implicit
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

internal B32
rd_dock_can_create(String8 name, CFG_Node *destination)
{
  RD_ViewRegistration *view = rd_dock_view_from_name(name);
  RD_DockProposal p = {0};
  p.host = rd_dock_host_from_cfg(destination, RD_DOCK_UNMEASURED_WIDTH);
  p.selected_workspace_region = p.host.controlled_split;
  p.instances_after = rd_dock_instances(rd_dock_window(destination), view)+1;
  // A creation must not add another selector to the implicit root surface.
  p.control_surfaces_after = view && (view->traits & RD_ViewTrait_SelectsWorkspaces) ? 2 : 1;
  return destination != &cfg_nil_node && rd_dock_check(view, p) == RD_DockRule_Valid;
}

internal B32
rd_dock_can_close(CFG_Node *view)
{
  RD_DockProposal p = {0};
  p.host = rd_dock_host_from_cfg(view->parent, RD_DOCK_UNMEASURED_WIDTH);
  p.closing = 1;
  p.control_surfaces_after = 1;
  return rd_dock_check(rd_dock_view_from_name(view->string), p) == RD_DockRule_Valid;
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
        CFG_Node *fallback = &cfg_nil_node;
        if(view->default_host == RD_DockHostKind_Sidebar)
        { fallback = cfg_node_child_from_string_or_alloc(state, window, str8_lit("control_views")); }
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
          // A split root cannot hold tabs. Choose its first leaf.
          for(;;)
          {
            CFG_Node *child = &cfg_nil_node;
            for(CFG_Node *n = fallback->first; n != &cfg_nil_node; n = n->next)
            { if(!rd_dock_view_from_name(n->string) && rd_dock_is_container(n)) { child = n; break; } }
            if(child == &cfg_nil_node) { break; }
            fallback = child;
          }
        }
        cfg_node_unhook(state, container, c);
        cfg_node_insert_child(state, fallback, fallback->last, c);
      }
    }
    else if(rd_dock_is_container(c))
    { rd_dock_restore_container(state, window, c); }
  }
}

// An invalid duplicate singleton cannot be repaired by placing both copies at
// the default. Retain the first saved instance deterministically; the implicit
// root Control Surface supplies the selector when no explicit instance exists.
internal void
rd_dock_restore_singletons(CFG_State *state, CFG_Node *container, B32 *seen)
{
  for(CFG_Node *c = container->first, *next; c != &cfg_nil_node; c = next)
  {
    next = c->next;
    RD_ViewRegistration *view = rd_dock_view_from_name(c->string);
    if(view && (view->traits & RD_ViewTrait_Singleton))
    {
      U64 index = (U64)(view-rd_view_registrations);
      if(seen[index]) { cfg_node_release(state, c); }
      else { seen[index] = 1; }
    }
    else if(!view && rd_dock_is_container(c)) { rd_dock_restore_singletons(state, c, seen); }
  }
}

internal void
rd_dock_restore_window(CFG_State *state, CFG_Node *window)
{
  B32 seen[ArrayCount(rd_view_registrations)] = {0};
  rd_dock_restore_singletons(state, window, seen);
  rd_dock_restore_container(state, window, window);
}
