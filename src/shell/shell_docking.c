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

// Split/leaf discriminator: recurse through layout containers, never through
// a View's settings, even when their keys resemble layout node names.
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

// A Floating Panel is a panel node directly under a `floating_panels` host.
internal CFG_Node *
rd_dock_floating_panel_from_cfg(CFG_Node *cfg)
{
  for(CFG_Node *c = cfg; c != &cfg_nil_node && !str8_match(c->string, str8_lit("window"), 0); c = c->parent)
  {
    if(str8_match(c->parent->string, str8_lit("floating_panels"), 0)) { return rd_dock_is_container(c) ? c : &cfg_nil_node; }
  }
  return &cfg_nil_node;
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
  Temp scratch = scratch_begin(0, 0);
  RD_DockDocuments documents = {scratch.arena};
  RD_DockSavedViewList views = rd_dock_saved_views(&documents, container);
  U32 count = 0;
  for(RD_DockSavedView *v = views.first; v != 0; v = v->next) { count += rd_dock_view_from_name(v->view->string) == registration; }
  scratch_end(scratch);
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

////////////////////////////////
//~ Repair

internal RD_DockDocument *
rd_dock_documents_add(RD_DockDocuments *documents, RD_Arrangement *arrangement)
{
  RD_DockDocument *document = push_array(documents->arena, RD_DockDocument, 1);
  document->arrangement = arrangement;
  SLLQueuePush(documents->first, documents->last, document);
  return document;
}

internal RD_DockDocument *
rd_dock_document_from_owner(RD_DockDocuments *documents, CFG_Node *owner, String8 root_name)
{
  CFG_Node *root = cfg_node_child_from_string(owner, root_name);
  for(RD_DockDocument *d = documents->first; d != 0; d = d->next)
  {
    RD_Arrangement *a = d->arrangement;
    if(root != &cfg_nil_node ? a->saved_root == root->id :
       (a->owner == owner->id && a->saved_root == 0 && str8_match(a->root_name, root_name, 0))) { return d; }
  }
  return rd_dock_documents_add(documents, rd_arrangement_from_owner(documents->arena, owner, root_name));
}

internal void
rd_dock_documents_add_floating(RD_DockDocuments *documents, CFG_Node *host)
{
  for(CFG_Node *c = host->first; c != &cfg_nil_node; c = c->next)
  {
    if(rd_dock_floating_panel_from_cfg(c) == c) { rd_dock_documents_add(documents, rd_arrangement_from_cfg(documents->arena, c)); }
  }
}

internal RD_ArrangementPanel *
rd_dock_panel_from_cfg(RD_DockDocuments *documents, CFG_ID cfg, RD_DockDocument **document_out)
{
  for(RD_DockDocument *d = documents->first; d != 0; d = d->next)
  {
    RD_ArrangementPanel *panel = rd_arrangement_panel_from_cfg(d->arrangement, cfg);
    if(panel != &rd_nil_arrangement_panel) { *document_out = d; return panel; }
  }
  *document_out = 0;
  return &rd_nil_arrangement_panel;
}

// Restore walks only layout containers, never View settings (whose keys can
// also be View names).
internal void
rd_dock_saved_views__walk(RD_DockDocuments *documents, RD_DockSavedViewList *list, CFG_Node *container,
                          RD_DockDocument *document, RD_ArrangementPanel *panel, CFG_ID holder)
{
  RD_DockDocument *container_document = 0;
  RD_ArrangementPanel *container_panel = rd_dock_panel_from_cfg(documents, container->id, &container_document);
  B32 in_panel = container_panel != &rd_nil_arrangement_panel;
  if(in_panel) { document = container_document; panel = container_panel; holder = 0; }
  for(CFG_Node *c = container->first; c != &cfg_nil_node; c = c->next)
  {
    if(rd_dock_view_from_name(c->string))
    {
      RD_DockSavedView *saved = push_array(documents->arena, RD_DockSavedView, 1);
      saved->view = c;
      saved->document = document;
      saved->panel = document ? panel : &rd_nil_arrangement_panel;
      saved->holder = holder;
      SLLQueuePush(list->first, list->last, saved);
      list->count += 1;
    }
    // A container saved in a panel that is not a child panel is one of its
    // tabs: anything inside it is a stray.
    else if(rd_dock_is_container(c))
    { rd_dock_saved_views__walk(documents, list, c, document, panel, in_panel ? c->id : holder); }
  }
}

internal RD_DockSavedViewList
rd_dock_saved_views(RD_DockDocuments *documents, CFG_Node *container)
{
  RD_DockSavedViewList list = {0};
  if(container != &cfg_nil_node) { rd_dock_saved_views__walk(documents, &list, container, 0, &rd_nil_arrangement_panel, 0); }
  return list;
}

internal B32
rd_dock_saved_view_is_tab(RD_DockSavedView *view)
{
  return view->document != 0 && view->holder == 0;
}

internal void
rd_dock_documents_save(CFG_State *state, RD_DockDocuments *documents)
{
  for(U32 pass = 0; pass < 2; pass += 1)
  {
    for(RD_DockDocument *d = documents->first; d != 0; d = d->next)
    {
      if(d->dirty && (pass == 1 || d->took))
      {
        rd_arrangement_save(state, d->arrangement);
        d->dirty = d->took = 0;
      }
    }
  }
}

// A window's own layout, its workspaces' and the sidebar, and every Floating
// Panel under them.
internal void
rd_dock_documents_add_window(RD_DockDocuments *documents, CFG_Node *window)
{
  rd_dock_document_from_owner(documents, window, RD_DOCK_SIDEBAR_ROOT);
  rd_dock_document_from_owner(documents, window, str8_lit("panels"));
  for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next)
  {
    if(str8_match(c->string, str8_lit("floating_panels"), 0)) { rd_dock_documents_add_floating(documents, c); }
    if(!str8_match(c->string, str8_lit("workspace"), 0)) { continue; }
    rd_dock_document_from_owner(documents, c, str8_lit("panels"));
    for(CFG_Node *host = c->first; host != &cfg_nil_node; host = host->next)
    {
      if(str8_match(host->string, str8_lit("floating_panels"), 0)) { rd_dock_documents_add_floating(documents, host); }
    }
  }
}

// Where a View refused where it is saved goes: the sidebar, or the layout of
// the workspace it was saved under (else the window's first, else the
// window's own).
internal RD_DockDocument *
rd_dock_restore_destination(RD_DockDocuments *documents, CFG_Node *window, RD_DockSavedView *saved)
{
  RD_ViewRegistration *registration = rd_dock_view_from_name(saved->view->string);
  if(registration->default_host == RD_DockHostKind_Sidebar)
  { return rd_dock_document_from_owner(documents, window, RD_DOCK_SIDEBAR_ROOT); }
  CFG_Node *owner = window;
  for(CFG_Node *n = saved->view->parent; n != &cfg_nil_node && n != window; n = n->parent)
  { if(str8_match(n->string, str8_lit("workspace"), 0)) { owner = n; break; } }
  if(owner == window)
  {
    CFG_Node *workspace = cfg_node_child_from_string(window, str8_lit("workspace"));
    if(workspace != &cfg_nil_node) { owner = workspace; }
  }
  return rd_dock_document_from_owner(documents, owner, str8_lit("panels"));
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

internal void
rd_dock_restore_window(CFG_State *state, CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  RD_DockDocuments documents = {scratch.arena};
  rd_dock_documents_add_window(&documents, window);
  RD_DockSavedViewList views = rd_dock_saved_views(&documents, window);

  // One copy of each singleton: the first in config order, unless a later
  // one is saved where it is valid and the first is not.
  RD_DockSavedView *keepers[ArrayCount(rd_view_registrations)] = {0};
  for(RD_DockSavedView *v = views.first; v != 0; v = v->next)
  {
    RD_ViewRegistration *registration = rd_dock_view_from_name(v->view->string);
    if(!(registration->traits & RD_ViewTrait_Singleton)) { continue; }
    RD_DockSavedView **keeper = &keepers[registration-rd_view_registrations];
    if(!*keeper || (!rd_dock_saved_placement_valid((*keeper)->view) && rd_dock_saved_placement_valid(v->view))) { *keeper = v; }
  }
  for(RD_DockSavedView *v = views.first; v != 0; v = v->next)
  {
    RD_ViewRegistration *registration = rd_dock_view_from_name(v->view->string);
    if(!(registration->traits & RD_ViewTrait_Singleton) || keepers[registration-rd_view_registrations] == v) { continue; }
    RD_DockDocument *document = v->document ? v->document : rd_dock_restore_destination(&documents, window, v);
    if(rd_dock_saved_view_is_tab(v)) { rd_arrangement_remove_tab(document->arrangement, v->view->id); }
    else { rd_arrangement_release_orphan(document->arrangement, v->view->id); }
    document->dirty = 1;
    v->view = &cfg_nil_node;
  }
  // The checker counts saved instances, so the copies go before it runs.
  rd_dock_documents_save(state, &documents);

  // A View the checker refuses where it is saved moves to the end of its
  // default host's first leaf, keeping its node, settings and identity. A
  // stray saved where it is valid stays, as it always has.
  for(RD_DockSavedView *v = views.first; v != 0; v = v->next)
  {
    if(v->view == &cfg_nil_node || rd_dock_placement(v->view, v->view->parent, RD_DOCK_UNMEASURED_WIDTH) == RD_DockRule_Valid) { continue; }
    // Region-specific defaults arrive with the native snapshot. Preserve the
    // need to resolve those hints after this generic safety fallback, and
    // mark the panel the section leaves for reconciliation to clean up.
    if(str8_match(v->view->string, str8_lit("sidebar_section"), 0))
    {
      cfg_node_child_from_string_or_alloc(state, v->view, str8_lit("section_hint_pending"));
      CFG_Node *panel = cfg_node_from_id(v->panel->cfg);
      if(panel != &cfg_nil_node) { cfg_node_child_from_string_or_alloc(state, panel, str8_lit("section_hint_cleanup")); }
    }
    RD_DockDocument *destination = rd_dock_restore_destination(&documents, window, v);
    RD_ArrangementPanel *leaf = rd_dock_restore_leaf(destination->arrangement);
    // A default can still lack context (for example a required subject).
    // Moving a View already there would only reorder it.
    if(rd_dock_saved_view_is_tab(v) && v->document == destination && v->panel == leaf) { continue; }
    // The destination keeps its Selected View unless this one was selected.
    B32 selected = cfg_node_child_from_string(v->view, str8_lit("selected")) != &cfg_nil_node;
    if(rd_dock_saved_view_is_tab(v) && v->document != destination)
    {
      rd_arrangement_detach_tab(v->document->arrangement, v->view->id);
      v->document->dirty = 1;
    }
    rd_arrangement_insert_tab(destination->arrangement, v->view->id, leaf->id, leaf->last_tab ? leaf->last_tab->view : 0, selected);
    destination->dirty = 1;
    destination->took |= v->document != destination;
    v->document = destination;
    v->panel = leaf;
    v->holder = 0;
  }
  rd_dock_documents_save(state, &documents);
  scratch_end(scratch);
}

// The proposal runs the commands' drop on a copy, which closes an emptied
// source as they do. A split destination that the closure collapses is
// measured before it, as the space its surviving panel takes over (the whole
// area for the root). Tabs occupy vertical chrome only; the panel inset
// consumes both X edges.
internal F32
rd_dock_moving_width(RD_Arrangement *arrangement, RD_PanelID destination, CFG_ID view,
                     RD_ArrangementTabRule *shown, Rng2F32 area, Dir2 dir, F32 inset)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *proposal = rd_arrangement_copy(scratch.arena, arrangement);
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(proposal, rd_arrangement_drop(proposal, view, destination, dir, 0, shown).panel);
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
