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
