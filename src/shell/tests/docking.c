// Headless tests of the docking checker, the production drag query and
// restore over typed arrangements. The shell's hosts are described here as
// it makes them (rd_dock_host_from_owner): one window (Controlled Split 7),
// its child workspace 20, and their levels.
#define BUILD_CONSOLE_INTERFACE 1
#define WM_STUB 1
#include "base/base_inc.h"
#include "mdesk/mdesk.h"
#include "window_manager/window_manager_inc.h"
#include "config/config_inc.h"
#include "shell/shell_workspace_id.h"
#include "shell/shell_arrangement.h"
#include "shell/shell_docking.h"
#include "base/base_inc.c"
#include "mdesk/mdesk.c"
#include "window_manager/window_manager_inc.c"
#include "config/config_inc.c"
#include "shell/shell_arrangement.c"
#include "shell/shell_docking.c"

#define Check(x) do { if(!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while(0)
global U32 failures;

read_only global RD_PanelOwner test_owner = {RD_PanelOwnerKind_Workspace};

// The window's sidebar, its own panels (a level apart), and a child
// workspace's panels. `width` wide.
internal RD_DockHost
host(RD_DockHostKind kind, CFG_ID level, B32 subject, F32 width)
{
  RD_DockHost result = {kind, 7, kind == RD_DockHostKind_WorkspaceRegion ? 7 : 0, subject, width, level};
  return result;
}
#define SIDEBAR(w) host(RD_DockHostKind_Sidebar, 7, 0, (w))
#define OWN(w) host(RD_DockHostKind_WorkspaceRegion, ~(CFG_ID)7, 0, (w))
#define WORKSPACE(w) host(RD_DockHostKind_WorkspaceRegion, 20, 0, (w))

// Each View is its kind and a count; the registry names its kind.
typedef struct TestView TestView;
struct TestView { RD_ViewAddress address; RD_ViewRegistration *registration; };
global TestView test_views[256];
global U64 test_view_count;

internal RD_ViewAddress
new_view(char *kind)
{
  Temp scratch = scratch_begin(0, 0);
  TestView *v = &test_views[test_view_count++];
  v->address = rd_view_address_make(RD_ViewOwnerKind_Workspace, (UIShell_WorkspaceId){0},
                                    push_str8f(scratch.arena, "%s#%I64u", kind, test_view_count), RD_ViewInstance_Current);
  v->registration = rd_dock_view_from_name(str8_cstring(kind));
  scratch_end(scratch);
  return v->address;
}

internal RD_ViewRegistration *
test_registration(RD_ViewAddress view)
{
  for(U64 i = 0; i < test_view_count; i++) { if(rd_view_address_match(test_views[i].address, view)) { return test_views[i].registration; } }
  return 0;
}

// `name` as docking finds a View of it in `source`.
internal RD_DockView
dock_view(char *name, RD_DockHost source)
{
  RD_DockView result = {1, rd_dock_view_from_name(str8_cstring(name)), source};
  result.level = rd_dock_view_level(result.registration, source);
  return result;
}

// The singletons the window has, as the shell counts them.
global U32 test_instances;
internal U32
test_instance_count(CFG_ID controlled_split, RD_ViewRegistration *registration)
{
  return test_instances;
}

// An arrangement as `text` describes it, in the config syntax: a weight is
// a panel, anything else a tab, `selected` its panel's Selected View.
global CFG_State *cfg;

internal void
build_panel(RD_Arrangement *arrangement, RD_PanelID id, CFG_Node *node)
{
  for(CFG_Node *c = node->first; c != &cfg_nil_node; c = c->next)
  {
    RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
    if(c->string.size && (char_is_digit(c->string.str[0], 10) || c->string.str[0] == '.'))
    {
      RD_PanelID child = rd_arrangement_add(arrangement, id, 1.f);
      rd_arrangement_panel_from_id(arrangement, child)->weight = (F32)f64_from_str8(c->string);
      build_panel(arrangement, child, c);
    }
    else if(!str8_match(c->string, str8_lit("selected"), 0))
    {
      B32 selected = cfg_node_child_from_string(c, str8_lit("selected")) != &cfg_nil_node;
      rd_arrangement_insert_tab(arrangement, new_view((char *)push_str8_copy(arrangement->arena, c->string).str), id,
                                panel->last_tab ? panel->last_tab->view : rd_nil_view_address, selected);
    }
  }
}

internal RD_Arrangement *
build(Arena *arena, char *text)
{
  CFG_SchemaNode *slot = 0;
  CFG_SchemaTable schemas = {&slot, 1};
  Temp scratch = scratch_begin(&arena, 1);
  CFG_NodePtrList parsed = cfg_node_ptr_list_from_string(scratch.arena, cfg, &schemas, str8_zero(), str8_cstring(text));
  RD_Arrangement *arrangement = rd_arrangement_alloc(arena, test_owner);
  for(CFG_NodePtrNode *n = parsed.first; n; n = n->next)
  {
    if(str8_match(n->v->string, str8_lit("panels"), 0)) { build_panel(arrangement, rd_arrangement_clear(arrangement), n->v); }
    cfg_node_release(cfg, n->v);
  }
  scratch_end(scratch);
  return arrangement;
}

// The tab of `name` in `arrangement`, and the panel it is in.
internal RD_ViewAddress
find(RD_Arrangement *arrangement, char *name)
{
  RD_ViewRegistration *registration = rd_dock_view_from_name(str8_cstring(name));
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next) { if(test_registration(t->view) == registration) { return t->view; } }
  }
  return rd_nil_view_address;
}

internal U64
count(RD_Arrangement *arrangement, char *name)
{
  U64 result = 0;
  RD_ViewRegistration *registration = rd_dock_view_from_name(str8_cstring(name));
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next) { result += test_registration(t->view) == registration; }
  }
  return result;
}

internal RD_PanelID
panel_of(RD_Arrangement *arrangement, RD_ViewAddress view)
{
  return rd_arrangement_panel_from_view(arrangement, view)->id;
}

internal void
entry_point(CmdLine *cmdline)
{
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
  RD_ViewRegistration custom = {str8_lit_comp("subject_section"), RD_ViewTrait_Section|RD_ViewTrait_NeedsWorkspaceSubject|RD_ViewTrait_Singleton, 100, RD_DockHostKind_Sidebar};
  RD_DockProposal p = {{RD_DockHostKind_Sidebar, 7, 0, 1, 100}, 7, 1, 1, 0};
  // Width is inclusive at the boundary. Missing subjects, duplicate singleton
  // instances, and zero/two Control Surfaces are invalid proposed layouts.
  Check(rd_dock_check(&custom, p) == RD_DockRule_Valid);
  p.host.available_width = 99; Check(rd_dock_check(&custom, p) == RD_DockRule_MinimumWidth);
  p.host.available_width = 101; Check(rd_dock_check(&custom, p) == RD_DockRule_Valid);
  p.host.available_width = 100;
  p.host.has_workspace_subject = 0; Check(rd_dock_check(&custom, p) == RD_DockRule_WorkspaceSubject);
  p.host.has_workspace_subject = 1;
  p.host.kind = RD_DockHostKind_WorkspaceRegion; Check(rd_dock_check(&custom, p) == RD_DockRule_HostAcceptance);
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
  Check(rd_dock_host_kind_from_panel_owner((RD_PanelOwner){RD_PanelOwnerKind_Sidebar}) == RD_DockHostKind_Sidebar);
  Check(rd_dock_host_kind_from_panel_owner(test_owner) == RD_DockHostKind_WorkspaceRegion);

  // These are the very queries made by split, boundary and catch-all drop
  // sites. The selector is the window's one Control Surface.
  rd_dock_instance_count = test_instance_count;
  test_instances = 1;
  RD_DockView selecting_view = dock_view("workspace_selector", WORKSPACE(640));
  RD_DockView terminal = dock_view("terminal", WORKSPACE(640));
  RD_DockView text = dock_view("text", SIDEBAR(640));
  Check(!rd_dock_drag_target(selecting_view, WORKSPACE(640)));
  Check(!rd_dock_drag_target(selecting_view, OWN(640)));
  Check(rd_dock_drag_target(selecting_view, SIDEBAR(640)));
  Check(rd_dock_drag_target(terminal, WORKSPACE(640)));
  Check(!rd_dock_drag_target(text, SIDEBAR(640)));
  Check(!rd_dock_can_close(selecting_view));
  Check(rd_dock_can_close(terminal));
  // Creating content is allowed; creating or duplicating the selector would
  // introduce a second Control Surface into its Controlled Split. File
  // loading and property inspectors use shell-dispatched Views as well as
  // visualizer hooks. They must remain creatable and movable.
  Check(rd_dock_can_create(str8_lit("terminal"), WORKSPACE(RD_DOCK_UNMEASURED_WIDTH)));
  Check(rd_dock_can_create(str8_lit("pending"), WORKSPACE(RD_DOCK_UNMEASURED_WIDTH)));
  Check(rd_dock_can_create(str8_lit("watch"), WORKSPACE(RD_DOCK_UNMEASURED_WIDTH)));
  Check(rd_dock_can_create(str8_lit("getting_started"), WORKSPACE(RD_DOCK_UNMEASURED_WIDTH)));
  Check(!rd_dock_can_create(str8_lit("workspace_selector"), SIDEBAR(RD_DOCK_UNMEASURED_WIDTH)));
  // Fleet sections belong to the root Controlled Split, regardless of a
  // destination's tab styling or the workspace currently selected there.
  RD_DockView section = dock_view("sidebar_section", SIDEBAR(640));
  Check(rd_dock_drag_target(section, SIDEBAR(640)));
  Check(!rd_dock_drag_target(section, WORKSPACE(640)));
  Check(!rd_dock_can_create(str8_lit("sidebar_section"), WORKSPACE(RD_DOCK_UNMEASURED_WIDTH)));
  Check(!rd_dock_drag_target(section, OWN(640)));
  Check(!rd_dock_drag_target(terminal, host(RD_DockHostKind_WorkspaceRegion, 30, 0, 640)));
  RD_DockHost other_sidebar = SIDEBAR(640);
  other_sidebar.controlled_split = other_sidebar.level = 9;
  Check(!rd_dock_drag_target(section, other_sidebar));
  // The selector cannot cross Controlled Splits, which would leave one split
  // without its Control Surface and give the other two.
  Check(!rd_dock_drag_target(selecting_view, other_sidebar));
  // A second copy of a singleton makes the move invalid.
  test_instances = 2;
  Check(!rd_dock_drag_target(dock_view("workspace_selector", SIDEBAR(640)), SIDEBAR(640)));
  test_instances = 1;
  // The registered scroll-region diagnostic fixture requires 128px. These
  // boundary widths exercise the production drag query and registry lookup,
  // so bypassing width checks in the drag adapter cannot survive.
  F32 query_widths[] = {0, 127, 128, 129, 640};
  B32 expected_widths[] = {0, 0, 1, 1, 1};
  for(U64 i = 0; i < ArrayCount(query_widths); i++)
  { Check(rd_dock_drag_target(dock_view("scroll_region_fixture", WORKSPACE(640)), WORKSPACE(query_widths[i])) == expected_widths[i]); }
  // Unknown saved content must remain removable even though it cannot be
  // created, duplicated or offered a docking target without declared traits.
  RD_DockView unknown_saved = dock_view("unknown_saved_view", WORKSPACE(640));
  Check(rd_dock_can_close(unknown_saved));
  Check(!rd_dock_drag_target(unknown_saved, WORKSPACE(640)));
  RD_DockView nothing = {0};
  RD_DockHost no_host = {RD_DockHostKind_COUNT};
  Check(!rd_dock_can_close(nothing));
  Check(rd_dock_creation(str8_lit("unknown_saved_view"), WORKSPACE(RD_DOCK_UNMEASURED_WIDTH)) == RD_DockRule_RegisteredView);
  Check(rd_dock_rule_message(RD_DockRule_RegisteredView).size != 0);
  Check(!rd_dock_drag_target(nothing, WORKSPACE(640)));
  Check(!rd_dock_drag_target(terminal, no_host));

  Arena *arena = arena_alloc();
  cfg = cfg_state_alloc();
  cfg_ctx_select(cfg_state_ctx(cfg));
  // A window's arrangements as restore reads them: its sidebar, its own
  // panels and a child workspace's.
#define RESTORE_SETUP(sidebar_text, workspace_text) \
  RD_DockDocuments docs = {arena}; \
  RD_Arrangement *sidebar = build(arena, sidebar_text), *workspace = build(arena, workspace_text); \
  docs.sidebar = rd_dock_documents_add(&docs, sidebar, SIDEBAR(RD_DOCK_UNMEASURED_WIDTH)); \
  docs.workspace = rd_dock_documents_add(&docs, workspace, WORKSPACE(RD_DOCK_UNMEASURED_WIDTH));
#define DIRTY() (docs.sidebar->dirty || docs.workspace->dirty)

  // Repair old cross-level placements into a leaf, preserving identity: a
  // section moves to the sidebar, which places it by its hints.
  {
    RESTORE_SETUP("panels:{1:{sidebar_section}}", "panels:{workspace_selector terminal sidebar_section:{selected}}");
    RD_ViewAddress section = find(workspace, "sidebar_section");
    RD_ViewAddressList pending = rd_dock_restore(&docs, test_registration);
    Check(pending.count == 1 && rd_view_address_match(pending.first->v, section));
    Check(panel_of(sidebar, section) == sidebar->root->first->id && panel_of(workspace, section) == 0);
    Check(count(sidebar, "sidebar_section") == 2 && rd_view_address_match(sidebar->root->first->last_tab->view, section));
    // The panel it left would go with it; the selector, refused in the
    // workspace, goes to the sidebar too.
    Check(workspace->root->restore_emptied && count(sidebar, "workspace_selector") == 1);
    Check(count(workspace, "terminal") == 1 && DIRTY());
    // Repair is idempotent.
    docs.sidebar->dirty = docs.workspace->dirty = 0;
    pending = rd_dock_restore(&docs, test_registration);
    Check(pending.count == 0 && !DIRTY());
  }
  // A hand-edited invalid placement returns to its declared default without
  // losing identity. Valid Views stay put.
  {
    RESTORE_SETUP("panels:{workspace_selector text}", "panels:{terminal}");
    RD_ViewAddress selector_view = find(sidebar, "workspace_selector"), text_view = find(sidebar, "text"), terminal_view = find(workspace, "terminal");
    rd_dock_restore(&docs, test_registration);
    Check(panel_of(sidebar, selector_view) == sidebar->root->id);
    Check(panel_of(workspace, text_view) == workspace->root->id && panel_of(workspace, terminal_view) == workspace->root->id);
    Check(rd_view_address_match(workspace->root->last_tab->view, text_view));
    docs.sidebar->dirty = docs.workspace->dirty = 0;
    rd_dock_restore(&docs, test_registration);
    Check(!DIRTY());
  }
  // Duplicate singleton declarations cannot create a second Control Surface.
  // Restore prefers the valid placement even when an invalid copy comes first.
  {
    RESTORE_SETUP("panels:{workspace_selector}", "panels:{workspace_selector terminal}");
    RD_ViewAddress kept = find(sidebar, "workspace_selector"), duplicate = find(workspace, "workspace_selector");
    rd_dock_restore(&docs, test_registration);
    Check(count(workspace, "workspace_selector") == 0 && count(sidebar, "workspace_selector") == 1);
    Check(panel_of(sidebar, kept) != 0);
    // The duplicate goes: setting its arrangement releases its View.
    B32 released = 0;
    for(RD_ArrangementTab *t = workspace->first_removed; t; t = t->next) { released |= rd_view_address_match(t->view, duplicate); }
    Check(released);
  }
  // Invalid content under the sidebar falls back to the first leaf of a
  // split workspace.
  {
    RESTORE_SETUP("panels:{terminal}", "panels:{0.5:{text} 0.5:{jackstay}}");
    RD_ViewAddress misplaced = find(sidebar, "terminal");
    rd_dock_restore(&docs, test_registration);
    Check(panel_of(workspace, misplaced) == workspace->root->first->id);
    Check(rd_view_address_match(workspace->root->first->last_tab->view, misplaced));
  }
  // When all copies are invalid, tree order is the deterministic tie-breaker;
  // an empty sidebar gets a panel for the one that moves.
  {
    RESTORE_SETUP("", "panels:{workspace_selector workspace_selector}");
    RD_ViewAddress first = workspace->root->first_tab->view;
    rd_dock_restore(&docs, test_registration);
    Check(sidebar->root != &rd_nil_arrangement_panel && rd_view_address_match(sidebar->root->first_tab->view, first));
    Check(count(workspace, "workspace_selector") == 0 && count(sidebar, "workspace_selector") == 1);
  }
  // A View restore moves between two arrangements leaves the one it was in
  // with its panel and other Views, and removes nothing.
  {
    RESTORE_SETUP("panels:{0.5:{sidebar_section:{selected} terminal} 0.5:{sidebar_section}}", "panels:{0.5:{terminal} 0.5:{text}}");
    RD_PanelID source = sidebar->root->first->id;
    RD_ViewAddress misplaced = sidebar->root->first->last_tab->view;
    rd_dock_restore(&docs, test_registration);
    Check(panel_of(workspace, misplaced) == workspace->root->first->id && rd_view_address_match(workspace->root->first->last_tab->view, misplaced));
    Check(rd_arrangement_panel_from_id(sidebar, source)->tab_count == 1 && count(sidebar, "terminal") == 0);
    Check(sidebar->first_removed == 0 && workspace->first_removed == 0);
    Check(rd_arrangement_problem(arena, sidebar).size == 0 && rd_arrangement_problem(arena, workspace).size == 0);
    docs.sidebar->dirty = docs.workspace->dirty = 0;
    rd_dock_restore(&docs, test_registration);
    Check(!DIRTY());
  }
  // Where a View lands, its panel keeps its Selected View unless the View
  // was itself selected where it was; the panel it left keeps its own, or
  // has none if it left.
  for(U32 marked = 0; marked < 2; marked++)
  {
    RESTORE_SETUP(marked ? "panels:{1:{sidebar_section jackstay:{selected}}}" : "panels:{1:{sidebar_section:{selected} jackstay}}",
                  "panels:{terminal:{selected} text}");
    RD_ViewAddress jackstay = find(sidebar, "jackstay"), selected_terminal = find(workspace, "terminal"), section = find(sidebar, "sidebar_section");
    rd_dock_restore(&docs, test_registration);
    Check(panel_of(workspace, jackstay) == workspace->root->id && rd_view_address_match(workspace->root->last_tab->view, jackstay));
    Check(rd_view_address_match(workspace->root->selected, marked ? jackstay : selected_terminal));
    Check(rd_view_address_match(sidebar->root->first->selected, marked ? rd_nil_view_address : section));
  }
  // Empty layouts remain empty; there is already an implicit root Control
  // Surface and restore must not manufacture content Views.
  {
    RESTORE_SETUP("", "");
    RD_ViewAddressList pending = rd_dock_restore(&docs, test_registration);
    Check(pending.count == 0 && !DIRTY() && sidebar->root == &rd_nil_arrangement_panel && workspace->root == &rd_nil_arrangement_panel);
  }
#undef RESTORE_SETUP
#undef DIRTY
  cfg_state_release(cfg);
  arena_release(arena);
  fprintf(stderr, "Docking validity: %u failures\n", failures);
  exit(failures != 0);
}
