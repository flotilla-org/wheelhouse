// The rule is the core's (crates/wheelhouse-core/src/dock.rs): these pass
// the shell's registrations, hosts and counts to it.

internal WheelhouseViewRegistration
rd_dock_core_registration(RD_ViewRegistration *view)
{
  WheelhouseViewRegistration result = {{view->name.str, view->name.size}, view->traits, view->minimum_width, view->default_host};
  return result;
}

internal WheelhouseDockHost
rd_dock_core_host(RD_DockHost host)
{
  WheelhouseDockHost result = {(U32)host.kind < RD_DockHostKind_COUNT ? (U32)host.kind : WHEELHOUSE_DOCK_HOST_NONE,
                               host.has_workspace_subject != 0, host.controlled_split, host.workspace_region, host.level, host.available_width};
  return result;
}

internal RD_DockRule
rd_dock_core_rule(U32 verdict, char *error)
{
  if(error)
  {
    fprintf(stderr, "Docking rule not applied: %s\n", error);
    wheelhouse_string_free(error);
  }
  return (RD_DockRule)verdict;
}

internal RD_DockPresentation
rd_dock_presentation(RD_DockHostKind host, U64 tab_count)
{
  // A sidebar section's header holds all its panel's Views (sidebar-headers.md).
  U32 kind = (U32)host < RD_DockHostKind_COUNT ? (U32)host : WHEELHOUSE_DOCK_HOST_NONE;
  return wheelhouse_dock_presentation(kind, tab_count) == WHEELHOUSE_DOCK_PRESENTATION_SECTION_HEADER ?
    RD_DockPresentation_SectionHeader : RD_DockPresentation_Tabs;
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

internal RD_DockRule
rd_dock_check(RD_ViewRegistration *view, RD_DockProposal p)
{
  WheelhouseViewRegistration registration = view ? rd_dock_core_registration(view) : (WheelhouseViewRegistration){0};
  WheelhouseDockProposal proposal = {rd_dock_core_host(p.host), p.selected_workspace_region, p.view_level,
                                     p.instances_after, p.control_surfaces_after, p.closing != 0};
  char *error = 0;
  U32 verdict = wheelhouse_dock_check(view ? &registration : 0, &proposal, &error);
  return rd_dock_core_rule(verdict, error);
}

internal RD_DockHostKind
rd_dock_host_kind_from_panel_owner(RD_PanelOwner owner)
{
  U32 kind = wheelhouse_dock_host_kind_from_owner(owner.kind == RD_PanelOwnerKind_Sidebar ? WHEELHOUSE_OWNER_SIDEBAR : WHEELHOUSE_OWNER_WORKSPACE);
  return kind == WHEELHOUSE_DOCK_HOST_SIDEBAR ? RD_DockHostKind_Sidebar : RD_DockHostKind_WorkspaceRegion;
}

// These declarations use the root split's core state, not the subject
// of whichever child workspace happens to contain an old placement.
internal CFG_ID
rd_dock_view_level(RD_ViewRegistration *registration, RD_DockHost host)
{
  WheelhouseViewRegistration core = registration ? rd_dock_core_registration(registration) : (WheelhouseViewRegistration){0};
  WheelhouseDockHost core_host = rd_dock_core_host(host);
  return wheelhouse_dock_view_level(registration ? &core : 0, &core_host);
}

// rd_dock_instance_count, for the registration a placement or creation
// checks.
internal U32
rd_dock_core_instances(void *context, U64 controlled_split)
{
  return rd_dock_instance_count(controlled_split, (RD_ViewRegistration *)context);
}

internal WheelhouseDockView
rd_dock_core_view(RD_DockView view, WheelhouseViewRegistration *registration)
{
  if(view.registration) { *registration = rd_dock_core_registration(view.registration); }
  WheelhouseDockView result = {view.exists != 0, view.registration ? registration : 0, rd_dock_core_host(view.source), view.level};
  return result;
}

internal RD_DockRule
rd_dock_placement(RD_DockView view, RD_DockHost destination)
{
  WheelhouseViewRegistration registration = {0};
  WheelhouseDockView core_view = rd_dock_core_view(view, &registration);
  WheelhouseDockHost host = rd_dock_core_host(destination);
  char *error = 0;
  U32 verdict = wheelhouse_dock_placement(&core_view, &host, rd_dock_instance_count ? rd_dock_core_instances : 0, view.registration, &error);
  return rd_dock_core_rule(verdict, error);
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
  WheelhouseViewRegistration registration = view ? rd_dock_core_registration(view) : (WheelhouseViewRegistration){0};
  WheelhouseDockHost host = rd_dock_core_host(destination);
  char *error = 0;
  U32 verdict = wheelhouse_dock_creation(view ? &registration : 0, &host, rd_dock_instance_count ? rd_dock_core_instances : 0, view, &error);
  return rd_dock_core_rule(verdict, error);
}

internal RD_DockRule
rd_dock_closure(RD_DockView view)
{
  WheelhouseViewRegistration registration = {0};
  WheelhouseDockView core_view = rd_dock_core_view(view, &registration);
  char *error = 0;
  U32 verdict = wheelhouse_dock_closure(&core_view, &error);
  return rd_dock_core_rule(verdict, error);
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
  WheelhouseText message = wheelhouse_dock_rule_message(rule);
  return message.len ? str8((U8 *)message.data, message.len) : str8_lit("invalid docking rule");
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

// Restore is the core's (dock::restore): each document goes as the core's
// edit document with its tabs' registrations, and comes back edited.
internal RD_ViewAddressList
rd_dock_restore(RD_DockDocuments *documents, RD_DockRegistration *registration)
{
  Arena *arena = documents->arena;
  RD_ViewAddressList pending = {0};
  Temp scratch = scratch_begin(&arena, 1);
  U64 count = 0;
  for(RD_DockDocument *d = documents->first; d != 0; d = d->next) { count += 1; }
  WheelhouseDockDocument *core_documents = push_array(scratch.arena, WheelhouseDockDocument, count);
  RD_DockDocument **list = push_array(scratch.arena, RD_DockDocument *, count);
  U64 sidebar = WHEELHOUSE_NONE, workspace = WHEELHOUSE_NONE, i = 0;
  for(RD_DockDocument *d = documents->first; d != 0; d = d->next, i += 1)
  {
    list[i] = d;
    if(d == documents->sidebar) { sidebar = i; }
    if(d == documents->workspace) { workspace = i; }
    WheelhouseDockDocument *c = &core_documents[i];
    c->doc = rd_core_doc_from_arrangement(scratch.arena, d->arrangement);
    c->host = rd_dock_core_host(d->host);
    U64 tabs = 0;
    for(U64 p = 0; p < c->doc.panel_count; p++) { tabs += c->doc.panels[p].tab_count; }
    size_t *kinds = push_array(scratch.arena, size_t, tabs);
    U64 t = 0;
    for(U64 p = 0; p < c->doc.panel_count; p++)
    {
      for(U64 k = 0; k < c->doc.panels[p].tab_count; k++, t++)
      {
        RD_ViewRegistration *r = registration(rd_view_from_core_tab(&c->doc.panels[p].tabs[k]));
        kinds[t] = r ? (size_t)(r - rd_view_registrations) : WHEELHOUSE_NONE;
      }
    }
    c->tab_registrations = kinds;
  }
  WheelhouseViewRegistration registrations[ArrayCount(rd_view_registrations)];
  for(U64 r = 0; r < ArrayCount(rd_view_registrations); r++) { registrations[r] = rd_dock_core_registration(&rd_view_registrations[r]); }
  char *error = 0;
  WheelhouseDockRestore *restored = wheelhouse_dock_restore(core_documents, count, sidebar, workspace, registrations, ArrayCount(registrations), &error);
  if(restored)
  {
    for(U64 d = 0; d < count; d++)
    {
      if(!restored->dirty[d]) { continue; }
      list[d]->dirty = 1;
      rd_arrangement_take(list[d]->arrangement, &restored->docs[d]);
    }
    for(U64 m = 0; m < restored->moved_count; m++) { rd_view_address_list_push(arena, &pending, rd_view_from_core_tab(&restored->moved[m])); }
    wheelhouse_dock_restore_free(restored);
  }
  else
  {
    fprintf(stderr, "Dock restore not applied: %s\n", error ? error : "unknown error");
    wheelhouse_string_free(error);
  }
  scratch_end(scratch);
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
