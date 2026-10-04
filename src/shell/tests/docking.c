// Headless tests use the real configuration parser and production drag query.
#define BUILD_CONSOLE_INTERFACE 1
#define WM_STUB 1
#include "base/base_inc.h"
#include "mdesk/mdesk.h"
#include "window_manager/window_manager_inc.h"
#include "config/config_inc.h"
#include "shell/shell_docking.h"
#include "base/base_inc.c"
#include "mdesk/mdesk.c"
#include "window_manager/window_manager_inc.c"
#include "config/config_inc.c"
#include "shell/shell_docking.c"

#define Check(x) do { if(!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while(0)

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
  CFG_Node *sidebar = cfg_node_child_from_string(window, str8_lit("control_views"));
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
  CFG_Node *split_sidebar = cfg_node_new(cfg, split, str8_lit("control_views"));
  CFG_Node *misplaced = cfg_node_new(cfg, split_sidebar, str8_lit("terminal"));
  rd_dock_restore_window(cfg, split);
  Check(misplaced->parent == leaf);
  Check(cfg_panel_tree_from_panels_cfg(arena, split_panels, Axis2_X).root->first->tabs.first->v == misplaced);
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
  Check(first_selector->parent == cfg_node_child_from_string(all_invalid, str8_lit("control_views")));
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
  // Repairing to the current default must preserve order and generation,
  // even if future context requirements cannot be satisfied there.
  CFG_Node *following_view = cfg_node_new(cfg, panels, str8_lit("text"));
  U64 before_default = cfg_change_gen();
  CFG_Node *before_prev = width_fixture->prev;
  rd_dock_restore_move(cfg, width_fixture, panels);
  Check(cfg_change_gen() == before_default && width_fixture->prev == before_prev);
  Check(width_fixture->next == following_view && panels->last == following_view);
  // A different default still moves the same View, and repeating it is stable.
  rd_dock_restore_move(cfg, width_fixture, floating_panel);
  Check(width_fixture->parent == floating_panel);
  before_default = cfg_change_gen();
  rd_dock_restore_move(cfg, width_fixture, floating_panel);
  Check(cfg_change_gen() == before_default && width_fixture->parent == floating_panel);
  // Unknown saved content must remain removable even though it cannot be
  // created, duplicated or offered a docking target without declared traits.
  CFG_Node *unknown_saved = cfg_node_new(cfg, panels, str8_lit("unknown_saved_view"));
  Check(rd_dock_can_close(unknown_saved));
  Check(!rd_dock_drag_target(unknown_saved, panels, 640));
  Check(!rd_dock_can_close(&cfg_nil_node));
  Check(rd_dock_creation(str8_lit("unknown_saved_view"), panels) == RD_DockRule_RegisteredView);
  Check(rd_dock_rule_message(RD_DockRule_RegisteredView).size != 0);
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
