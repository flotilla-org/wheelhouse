// Headless tests use the real configuration parser and production drag query.
#define BUILD_CONSOLE_INTERFACE 1
#define WM_STUB 1
#include "base/base_inc.h"
#include "mdesk/mdesk.h"
#include "window_manager/window_manager_inc.h"
#include "config/config_inc.h"
#include "shell/shell_arrangement.h"
#include "shell/shell_docking.h"
#include "base/base_inc.c"
#include "mdesk/mdesk.c"
#include "window_manager/window_manager_inc.c"
#include "config/config_inc.c"
#include "shell/shell_arrangement.c"
#include "shell/shell_docking.c"

#define Check(x) do { if(!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while(0)

// Parses one top-level config tree, as the user file loader does.
internal CFG_Node *
restore_fixture(Arena *arena, CFG_State *cfg, char *text)
{
  CFG_SchemaNode *slot = 0;
  CFG_SchemaTable schemas = {&slot, 1};
  CFG_NodePtrList parsed = cfg_node_ptr_list_from_string(arena, cfg, &schemas, str8_zero(), str8_cstring(text));
  for(CFG_NodePtrNode *n = parsed.first; n; n = n->next) { cfg_node_insert_child(cfg, cfg_node_root(), cfg_node_root()->last, n->v); }
  return parsed.first->v;
}

// The child of `node` named `name`, or with `label` as its label.
internal CFG_Node *
restore_child(CFG_Node *node, char *name, char *label)
{
  for(CFG_Node *c = node->first; c != &cfg_nil_node; c = c->next)
  {
    if(str8_match(c->string, str8_cstring(name), 0) &&
       (label == 0 || str8_match(cfg_node_child_from_string(c, str8_lit("label"))->first->string, str8_cstring(label), 0))) { return c; }
  }
  return &cfg_nil_node;
}

// Each arrangement restore saved loads again with no broken invariant.
internal B32
restore_saved_valid(Arena *arena, CFG_Node *root)
{
  return rd_arrangement_problem(arena, rd_arrangement_from_cfg(arena, root)).size == 0;
}

internal void
entry_point(CmdLine *cmdline)
{
  U32 failures = 0;
  // The registry generator spans every View type and every declared host,
  // including the selector's own and a different Workspace Region.
  for(U64 i = 0; i < ArrayCount(rd_view_registrations); i++)
  {
    RD_ViewRegistration *view = &rd_view_registrations[i];
    Check(rd_dock_view_from_name(view->name) == view);
    for(U32 kind = 0; kind < RD_DockHostKind_COUNT; kind++)
    {
      RD_DockProposal p = {{kind, 7, kind == RD_DockHostKind_WorkspaceRegion ? 7 : 0, 1, 640}, 7, 1, 1, 0};
      // Sidebar accepts sections; content belongs in Workspace Regions;
      // a selector cannot inhabit the Workspace Region it selects.
      B32 expected = kind != RD_DockHostKind_Sidebar || (view->traits & RD_ViewTrait_Section);
      if(kind == RD_DockHostKind_WorkspaceRegion && (view->traits & RD_ViewTrait_SelectsWorkspaces)) { expected = 0; }
      Check((rd_dock_check(view, p) == RD_DockRule_Valid) == !!expected);
      // Selection bindings, rather than the View name, determine exclusion.
      p.host.workspace_region = kind == RD_DockHostKind_WorkspaceRegion ? 8 : 0;
      if(kind == RD_DockHostKind_WorkspaceRegion) { Check(rd_dock_check(view, p) == RD_DockRule_Valid); }
      p.host.kind = view->default_host;
      p.host.workspace_region = 0;
      Check(rd_dock_check(view, p) == RD_DockRule_Valid);
    }
  }
  RD_ViewRegistration *selector = rd_dock_view_from_name(str8_lit("workspace_selector"));
  RD_ViewRegistration custom = {str8_lit_comp("subject_section"), RD_ViewTrait_Section|RD_ViewTrait_NeedsWorkspaceSubject|RD_ViewTrait_NeedsHost|RD_ViewTrait_Singleton, 100, RD_DockHostKind_Sidebar};
  RD_DockProposal p = {{RD_DockHostKind_Sidebar, 7, 0, 1, 100}, 7, 1, 1, 0};
  // Width is inclusive at the boundary. Missing subjects, duplicate singleton
  // instances, and zero/two Control Surfaces are invalid proposed layouts.
  Check(rd_dock_check(&custom, p) == RD_DockRule_Valid);
  p.host.available_width = 99; Check(rd_dock_check(&custom, p) == RD_DockRule_MinimumWidth);
  p.host.available_width = 101; Check(rd_dock_check(&custom, p) == RD_DockRule_Valid);
  p.host.available_width = 100;
  p.host.has_workspace_subject = 0; Check(rd_dock_check(&custom, p) == RD_DockRule_WorkspaceSubject);
  p.host.has_workspace_subject = 1;
  p.host.kind = RD_DockHostKind_FloatingPanel; Check(rd_dock_check(&custom, p) == RD_DockRule_HostAcceptance);
  p.host.kind = RD_DockHostKind_Sidebar;
  p.instances_after = 0; Check(rd_dock_check(&custom, p) == RD_DockRule_Singleton);
  p.instances_after = 2; Check(rd_dock_check(&custom, p) == RD_DockRule_Singleton);
  p.instances_after = 1;
  p.control_surfaces_after = 0; Check(rd_dock_check(selector, p) == RD_DockRule_OneControlSurface);
  p.control_surfaces_after = 2; Check(rd_dock_check(selector, p) == RD_DockRule_OneControlSurface);
  p.control_surfaces_after = 1;
  // The selecting View may move, but cannot close.
  Check(rd_dock_check(selector, p) == RD_DockRule_Valid);
  p.closing = 1; Check(rd_dock_check(selector, p) == RD_DockRule_SelectorCannotClose);
  p.closing = 0;
  Check(rd_dock_check(0, p) == RD_DockRule_RegisteredView);
  p.host.kind = RD_DockHostKind_COUNT; Check(rd_dock_check(selector, p) == RD_DockRule_HostAcceptance);
  p.host.kind = (RD_DockHostKind)-1; Check(rd_dock_check(selector, p) == RD_DockRule_HostAcceptance);
  RD_ViewRegistration *fleet_section = rd_dock_view_from_name(str8_lit("sidebar_section"));
  p.host.kind = RD_DockHostKind_WorkspaceRegion;
  p.host.workspace_region = 0;
  p.host.level = p.view_level = 7;
  Check(rd_dock_check(fleet_section, p) == RD_DockRule_Valid);
  p.host.level = 8;
  Check(rd_dock_check(fleet_section, p) == RD_DockRule_ControlSplitLevel);

  Check(rd_dock_presentation(RD_DockHostKind_Sidebar, 1) == RD_DockPresentation_SectionHeader);
  Check(rd_dock_presentation(RD_DockHostKind_Sidebar, 2) == RD_DockPresentation_SectionHeader);
  Check(rd_dock_presentation(RD_DockHostKind_WorkspaceRegion, 1) == RD_DockPresentation_Tabs);
  Check(rd_dock_presentation(RD_DockHostKind_FloatingPanel, 1) == RD_DockPresentation_Tabs);
  Arena *arena = arena_alloc();
  CFG_State *cfg = cfg_state_alloc();
  cfg_ctx_select(cfg_state_ctx(cfg));
  CFG_SchemaNode *slot = 0;
  CFG_SchemaTable schemas = {&slot, 1};
  CFG_NodePtrList parsed = cfg_node_ptr_list_from_string(arena, cfg, &schemas, str8_zero(), str8_lit(
    "window:{workspace:{panels:{workspace_selector:{label:`Saved selector`} terminal:{label:`Keep terminal`}}} control_views:{text:{label:`Saved text`}}}"));
  for(CFG_NodePtrNode *n = parsed.first; n; n = n->next) { cfg_node_insert_child(cfg, cfg_node_root(), cfg_node_root()->last, n->v); }
  CFG_Node *window = cfg_node_child_from_string(cfg_node_root(), str8_lit("window"));
  CFG_Node *workspace = cfg_node_child_from_string(window, str8_lit("workspace"));
  CFG_Node *panels = cfg_node_child_from_string(workspace, str8_lit("panels"));
  CFG_Node *sidebar = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
  CFG_Node *selecting_view = cfg_node_child_from_string(panels, str8_lit("workspace_selector"));
  CFG_Node *text = cfg_node_child_from_string(sidebar, str8_lit("text"));
  CFG_Node *terminal = cfg_node_child_from_string(panels, str8_lit("terminal"));
  CFG_ID selector_id = selecting_view->id, text_id = text->id;
  // These are the very queries made by split, boundary and catch-all drop sites.
  // Rejection includes the selected workspace and the legacy window mount.
  Check(!rd_dock_drag_target(selecting_view, panels, 640));
  Check(!rd_dock_drag_target(selecting_view, window, 640));
  Check(rd_dock_drag_target(selecting_view, sidebar, 640));
  Check(rd_dock_drag_target(terminal, panels, 640));
  Check(!rd_dock_drag_target(text, sidebar, 640));
  Check(!rd_dock_can_close(selecting_view));
  Check(rd_dock_can_close(terminal));
  // Creating content is allowed; creating or duplicating the selector would
  // introduce a second Control Surface into its Controlled Split.
  Check(rd_dock_can_create(str8_lit("terminal"), panels));
  // File loading and property inspectors use shell-dispatched Views as well
  // as visualizer hooks. They must remain creatable and movable.
  Check(rd_dock_can_create(str8_lit("pending"), panels));
  Check(rd_dock_can_create(str8_lit("watch"), panels));
  Check(rd_dock_can_create(str8_lit("getting_started"), panels));
  Check(!rd_dock_can_create(str8_lit("workspace_selector"), sidebar));
  // Fleet sections belong to the root Controlled Split, regardless of a
  // destination's tab styling or the workspace currently selected there.
  CFG_Node *section_panel = cfg_node_new(cfg, sidebar, str8_lit("1"));
  CFG_Node *section = cfg_node_new(cfg, section_panel, str8_lit("sidebar_section"));
  cfg_node_new(cfg, cfg_node_new(cfg, section, str8_lit("section")), str8_lit("attention"));
  CFG_ID section_id = section->id;
  Check(rd_dock_drag_target(section, section_panel, 640));
  Check(!rd_dock_drag_target(section, panels, 640));
  Check(!rd_dock_can_create(str8_lit("sidebar_section"), panels));
  CFG_Node *legacy_panels = cfg_node_new(cfg, window, str8_lit("panels"));
  Check(!rd_dock_drag_target(section, legacy_panels, 640));
  CFG_Node *root_floating = cfg_node_new(cfg, window, str8_lit("floating_panels"));
  Check(rd_dock_drag_target(section, root_floating, 640));
  CFG_Node *child_floating = cfg_node_new(cfg, workspace, str8_lit("floating_panels"));
  Check(!rd_dock_drag_target(section, child_floating, 640));
  CFG_Node *other_workspace = cfg_node_new(cfg, window, str8_lit("workspace"));
  CFG_Node *other_panels = cfg_node_new(cfg, other_workspace, str8_lit("panels"));
  Check(!rd_dock_drag_target(terminal, other_panels, 640));
  CFG_Node *other_window = cfg_node_new(cfg, cfg_node_root(), str8_lit("window"));
  CFG_Node *other_sidebar = cfg_node_new(cfg, other_window, RD_DOCK_SIDEBAR_ROOT);
  Check(!rd_dock_drag_target(section, other_sidebar, 640));
  // Repair old cross-level placements into a leaf, preserving identity and settings.
  cfg_node_unhook(cfg, section_panel, section);
  cfg_node_insert_child(cfg, panels, panels->last, section);
  rd_dock_restore_window(cfg, window);
  Check(section->parent == section_panel && section->id == section_id);
  Check(str8_match(cfg_node_child_from_string(section, str8_lit("section"))->first->string, str8_lit("attention"), 0));
  cfg_node_unhook(cfg, selecting_view->parent, selecting_view);
  cfg_node_insert_child(cfg, sidebar, sidebar->last, selecting_view);
  cfg_node_release(cfg, section_panel);
  cfg_node_release(cfg, legacy_panels);
  cfg_node_release(cfg, root_floating);
  cfg_node_release(cfg, child_floating);
  cfg_node_release(cfg, other_workspace);
  cfg_node_release(cfg, other_window);
  // A hand-edited invalid placement returns to its declared default without
  // losing identity or settings. Valid Views stay put. Repair is idempotent.
  rd_dock_restore_window(cfg, window);
  Check(selecting_view->parent == sidebar && selecting_view->id == selector_id);
  Check(text->parent == panels && text->id == text_id);
  Check(terminal->parent == panels);
  Check(str8_match(cfg_node_child_from_string(selecting_view, str8_lit("label"))->first->string, str8_lit("Saved selector"), 0));
  Check(str8_match(cfg_node_child_from_string(text, str8_lit("label"))->first->string, str8_lit("Saved text"), 0));
  rd_dock_restore_window(cfg, window);
  Check(selecting_view->parent == sidebar && text->parent == panels && terminal->parent == panels);
  // Duplicate singleton declarations cannot create a second Control Surface.
  // Restore prefers the valid placement even when an invalid copy comes first.
  CFG_Node *duplicate = cfg_node_new(cfg, panels, str8_lit("workspace_selector"));
  CFG_ID duplicate_id = duplicate->id;
  Check(!rd_dock_drag_target(selecting_view, sidebar, 640));
  rd_dock_restore_window(cfg, window);
  Check(cfg_node_from_id(duplicate_id) == &cfg_nil_node);
  Check(selecting_view->parent == sidebar && selecting_view->id == selector_id);
  Check(rd_dock_instances(window, selector) == 1);
  Check(rd_dock_drag_target(selecting_view, sidebar, 640));
  // Invalid content under the sidebar falls back to a leaf of a split root.
  CFG_Node *split = cfg_node_new(cfg, cfg_node_root(), str8_lit("window"));
  CFG_Node *split_panels = cfg_node_new(cfg, split, str8_lit("panels"));
  CFG_Node *leaf = cfg_node_new(cfg, split_panels, str8_lit("0.5"));
  cfg_node_new(cfg, split_panels, str8_lit("0.5"));
  CFG_Node *split_sidebar = cfg_node_new(cfg, split, RD_DOCK_SIDEBAR_ROOT);
  CFG_Node *misplaced = cfg_node_new(cfg, split_sidebar, str8_lit("terminal"));
  rd_dock_restore_window(cfg, split);
  Check(misplaced->parent == leaf);
  Check(rd_panel_tree_from_cfg(arena, split_panels).root->first->tabs.first->v == misplaced);
  // The selector cannot cross Controlled Splits, which would leave one split
  // without its Control Surface and give the other two.
  Check(!rd_dock_drag_target(selecting_view, split_sidebar, 640));
  // When all copies are invalid, tree order is the deterministic tie-breaker.
  CFG_Node *all_invalid = cfg_node_new(cfg, cfg_node_root(), str8_lit("window"));
  CFG_Node *invalid_panels = cfg_node_new(cfg, all_invalid, str8_lit("panels"));
  CFG_Node *first_selector = cfg_node_new(cfg, invalid_panels, str8_lit("workspace_selector"));
  CFG_Node *second_selector = cfg_node_new(cfg, invalid_panels, str8_lit("workspace_selector"));
  CFG_ID second_id = second_selector->id;
  rd_dock_restore_window(cfg, all_invalid);
  Check(first_selector->parent == cfg_node_child_from_string(all_invalid, RD_DOCK_SIDEBAR_ROOT));
  Check(cfg_node_from_id(second_id) == &cfg_nil_node);
  // Floating Panels are real config hosts, including beneath a Workspace with
  // a subject. Restore retains their valid Views and preserves the subject.
  CFG_Node *subject = cfg_node_new(cfg, workspace, str8_lit("subject"));
  cfg_node_new(cfg, subject, str8_lit("convoy:example"));
  CFG_Node *floating = cfg_node_new(cfg, workspace, str8_lit("floating_panels"));
  CFG_Node *floating_panel = cfg_node_new(cfg, floating, str8_lit("1"));
  CFG_Node *floating_text = cfg_node_new(cfg, floating_panel, str8_lit("text"));
  RD_DockHost floating_host = rd_dock_host_from_cfg(floating_panel, 100);
  Check(floating_host.kind == RD_DockHostKind_FloatingPanel && floating_host.has_workspace_subject);
  Check(rd_dock_drag_target(floating_text, floating_panel, 100));
  // A subject-dependent section can use its Workspace context while floating
  // if it does not require a host. Missing context is rejected by the checker.
  RD_ViewRegistration subject_view = {str8_lit_comp("subject"), RD_ViewTrait_Section|RD_ViewTrait_NeedsWorkspaceSubject, 0, RD_DockHostKind_WorkspaceRegion};
  RD_DockProposal subject_proposal = {floating_host, 0, 1, 1, 0};
  subject_proposal.view_level = floating_host.level;
  Check(rd_dock_check(&subject_view, subject_proposal) == RD_DockRule_Valid);
  subject_proposal.host.has_workspace_subject = 0;
  Check(rd_dock_check(&subject_view, subject_proposal) == RD_DockRule_WorkspaceSubject);
  rd_dock_restore_window(cfg, window);
  Check(floating_text->parent == floating_panel && subject->parent == workspace);
  // The cheap container classifier must agree with the production panel
  // tokenizer for numeric spellings and invalid/identifier boundaries.
  char *spellings[] = {"", "0", "1", "0.5", ".5", "-1", "-.5", "+1", "1e3", "1_0", "_", "selected", "1/2", "1 2"};
  for(U64 i = 0; i < ArrayCount(spellings); i++)
  {
    CFG_Node *test_container = cfg_node_new(cfg, cfg_node_root(), str8_lit("test"));
    CFG_Node *node = cfg_node_new(cfg, test_container, str8_cstring(spellings[i]));
    MD_TokenizeResult tokens = md_tokenize_from_text(arena, node->string);
    B32 expected_numeric = tokens.tokens.count == 1 && (tokens.tokens.v[0].flags & MD_TokenFlag_Numeric);
    Check(rd_dock_is_container(node) == !!expected_numeric);
    cfg_node_release(cfg, test_container);
  }
  // The registered scroll-region diagnostic fixture requires 128px. These
  // boundary widths exercise the production drag query and registry lookup,
  // so bypassing width checks in the drag adapter cannot survive.
  CFG_Node *width_fixture = cfg_node_new(cfg, panels, str8_lit("scroll_region_fixture"));
  F32 query_widths[] = {0, 127, 128, 129, 640};
  B32 expected_widths[] = {0, 0, 1, 1, 1};
  for(U64 i = 0; i < ArrayCount(query_widths); i++)
  { Check(rd_dock_drag_target(width_fixture, panels, query_widths[i]) == expected_widths[i]); }
  // Repairing a layout with nothing to repair preserves order and generation.
  CFG_Node *following_view = cfg_node_new(cfg, panels, str8_lit("text"));
  rd_dock_restore_window(cfg, window);
  U64 before_default = cfg_change_gen();
  CFG_Node *before_prev = width_fixture->prev;
  rd_dock_restore_window(cfg, window);
  Check(cfg_change_gen() == before_default && width_fixture->prev == before_prev);
  Check(width_fixture->next == following_view && panels->last == following_view);
  // Unknown saved content must remain removable even though it cannot be
  // created, duplicated or offered a docking target without declared traits.
  CFG_Node *unknown_saved = cfg_node_new(cfg, panels, str8_lit("unknown_saved_view"));
  Check(rd_dock_can_close(unknown_saved));
  Check(!rd_dock_drag_target(unknown_saved, panels, 640));
  Check(!rd_dock_can_close(&cfg_nil_node));
  Check(rd_dock_creation(str8_lit("unknown_saved_view"), panels) == RD_DockRule_RegisteredView);
  Check(rd_dock_rule_message(RD_DockRule_RegisteredView).size != 0);
  // A View restore moves between two layouts leaves one arrangement for the
  // other with its node, settings and identity: the arrangement it leaves
  // keeps its panel and other Views, and releases nothing.
  {
    CFG_Node *moving = restore_fixture(arena, cfg,
      "window:{workspace:{panels:{0.5:{terminal:{label:`left`}} 0.5:{text}}} "
      "control_views:{0.5:{sidebar_section:{selected section:a} terminal:{label:`misplaced`}} 0.5:{sidebar_section:{section:b}}}}");
    CFG_Node *moving_panels = restore_child(restore_child(moving, "workspace", 0), "panels", 0);
    CFG_Node *moving_sidebar = restore_child(moving, "control_views", 0);
    CFG_Node *left = moving_panels->first, *source = moving_sidebar->first;
    CFG_Node *misplaced = restore_child(source, "terminal", "misplaced");
    CFG_ID misplaced_id = misplaced->id;
    rd_dock_restore_window(cfg, moving);
    Check(cfg_node_from_id(misplaced_id) == misplaced && misplaced->parent == left);
    Check(rd_panel_tree_from_cfg(arena, moving_panels).root->first->tabs.last->v == misplaced);
    Check(source->parent == moving_sidebar && restore_child(source, "sidebar_section", 0) != &cfg_nil_node);
    Check(restore_child(source, "terminal", 0) == &cfg_nil_node);
    Check(restore_saved_valid(arena, moving_panels) && restore_saved_valid(arena, moving_sidebar));
    U64 repaired = cfg_change_gen();
    rd_dock_restore_window(cfg, moving);
    Check(cfg_change_gen() == repaired);
    cfg_node_release(cfg, moving);
  }
  // Views saved in no layout (directly under a workspace or a Floating
  // Panels host, or in a second root) are strays: one the checker refuses
  // where it is moves to its default host, as any refused View does, and
  // one it accepts stays.
  {
    CFG_Node *strays = restore_fixture(arena, cfg,
      "window:{workspace:{text:{label:`loose`} workspace_selector:{label:`selector`} panels:{terminal} "
      "panels:{sidebar_section:{section:second}} floating_panels:{sidebar_section:{section:hosted}}} "
      "control_views:{sidebar_section:{section:docked}}}");
    CFG_Node *strays_workspace = restore_child(strays, "workspace", 0);
    CFG_Node *strays_sidebar = restore_child(strays, "control_views", 0);
    CFG_Node *leaf = strays_sidebar;
    CFG_Node *loose = restore_child(strays_workspace, "text", 0);
    CFG_Node *stray_selector = restore_child(strays_workspace, "workspace_selector", 0);
    CFG_Node *second = restore_child(strays_workspace->last->prev, "sidebar_section", 0);
    CFG_Node *hosted = restore_child(restore_child(strays_workspace, "floating_panels", 0), "sidebar_section", 0);
    CFG_ID selector_stray_id = stray_selector->id, second_id = second->id, hosted_id = hosted->id;
    Check(second != &cfg_nil_node && hosted != &cfg_nil_node);
    rd_dock_restore_window(cfg, strays);
    Check(loose->parent == strays_workspace);
    Check(stray_selector->parent == leaf && stray_selector->id == selector_stray_id);
    Check(second->parent == leaf && second->id == second_id && hosted->parent == leaf && hosted->id == hosted_id);
    Check(cfg_node_child_from_string(second, str8_lit("section_hint_pending")) != &cfg_nil_node);
    Check(cfg_node_child_from_string(hosted, str8_lit("section_hint_pending")) != &cfg_nil_node);
    CFG_PanelTree strays_tree = rd_panel_tree_from_cfg(arena, strays_sidebar);
    Check(strays_tree.root->tabs.count == 4 && strays_tree.root->tabs.last->v == hosted);
    Check(restore_child(restore_child(strays_workspace, "panels", 0), "terminal", 0) != &cfg_nil_node);
    Check(restore_saved_valid(arena, strays_sidebar));
    cfg_node_release(cfg, strays);
  }
  // Duplicate singletons keep the first copy in config order, where a split
  // saved with tabs of its own lists its child panels and tabs interleaved,
  // not the arrangement's tabs-first order.
  {
    CFG_Node *duplicates = restore_fixture(arena, cfg,
      "window:{control_views:{0.5:{workspace_selector:{label:`first`}} workspace_selector:{label:`second`} 0.5:{sidebar_section:{section:a}}}}");
    CFG_Node *duplicates_sidebar = restore_child(duplicates, "control_views", 0);
    CFG_Node *first = restore_child(duplicates_sidebar->first, "workspace_selector", 0);
    CFG_ID second_selector_id = restore_child(duplicates_sidebar, "workspace_selector", 0)->id;
    rd_dock_restore_window(cfg, duplicates);
    Check(cfg_node_from_id(second_selector_id) == &cfg_nil_node);
    Check(first->parent == duplicates_sidebar->first && rd_dock_instances(duplicates, selector) == 1);
    cfg_node_release(cfg, duplicates);
  }
  // Where a View lands, its panel keeps its Selected View unless the View
  // was itself selected where it was saved.
  for(U32 marked = 0; marked < 2; marked++)
  {
    CFG_Node *selection = restore_fixture(arena, cfg, marked ?
      "window:{workspace:{panels:{terminal:{selected} text}} control_views:{1:{sidebar_section:{section:a} jackstay:{selected}}}}" :
      "window:{workspace:{panels:{terminal:{selected} text}} control_views:{1:{sidebar_section:{selected section:a} jackstay}}}");
    CFG_Node *destination = restore_child(restore_child(selection, "workspace", 0), "panels", 0);
    CFG_Node *source = restore_child(selection, "control_views", 0)->first;
    CFG_Node *jackstay = restore_child(source, "jackstay", 0);
    CFG_Node *selected_terminal = restore_child(destination, "terminal", 0);
    rd_dock_restore_window(cfg, selection);
    CFG_PanelTree tree = rd_panel_tree_from_cfg(arena, destination);
    Check(jackstay->parent == destination && tree.root->tabs.last->v == jackstay);
    Check(tree.root->selected_tab == (marked ? jackstay : selected_terminal));
    Check((cfg_node_child_from_string(selected_terminal, str8_lit("selected")) != &cfg_nil_node) == !marked);
    // The panel it left keeps its own Selected View, or has none if it left.
    CFG_Node *section = restore_child(source, "sidebar_section", 0);
    Check((cfg_node_child_from_string(section, str8_lit("selected")) != &cfg_nil_node) == !marked);
    Check(restore_saved_valid(arena, destination));
    cfg_node_release(cfg, selection);
  }
  // Empty saved layouts remain empty; there is already an implicit root
  // Control Surface and restore must not manufacture content Views.
  CFG_Node *empty = cfg_node_new(cfg, cfg_node_root(), str8_lit("window"));
  rd_dock_restore_window(cfg, empty);
  Check(empty->first == &cfg_nil_node);
  Check(!rd_dock_drag_target(&cfg_nil_node, panels, 640));
  Check(!rd_dock_drag_target(terminal, &cfg_nil_node, 640));
  cfg_state_release(cfg);
  arena_release(arena);
  fprintf(stderr, "Docking validity: %u failures\n", failures);
  exit(failures != 0);
}
