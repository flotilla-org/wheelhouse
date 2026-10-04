// Declared View/host validity, independent of gestures and rendering.
#ifndef SHELL_DOCKING_H
#define SHELL_DOCKING_H

// Restore and command execution have no measured geometry. Drag queries pass
// the proposed target width; a resize is not a structural layout repair.
#define RD_DOCK_UNMEASURED_WIDTH 3.402823466e38f

typedef enum RD_DockHostKind
{
  RD_DockHostKind_Sidebar,
  RD_DockHostKind_WorkspaceRegion,
  RD_DockHostKind_FloatingPanel,
  RD_DockHostKind_COUNT,
} RD_DockHostKind;

typedef U32 RD_ViewTraits;
enum
{
  RD_ViewTrait_Content = 1<<0,
  RD_ViewTrait_Section = 1<<1,
  RD_ViewTrait_SelectsWorkspaces = 1<<2,
  RD_ViewTrait_Singleton = 1<<3,
  RD_ViewTrait_NeedsWorkspaceSubject = 1<<4,
  RD_ViewTrait_NeedsHost = 1<<5,
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
};

// This is also the UI registration list. Adding a View requires declaring its
// traits here, so enumeration and rendering cannot acquire separate lists.
#define RD_DOCK_RENDERED_VIEWS(X) \
  X(text, shell_text, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(jackstay, jackstay, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(terminal, terminal, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(scroll_region_fixture, scroll_region_fixture, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(terminal_fixture, terminal, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(sessions, sessions, RD_ViewTrait_Content|RD_ViewTrait_Section, 0, WorkspaceRegion) \
  X(binary, binary, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(bitmap, bitmap, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(color, color, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(geo3d, geo3d, RD_ViewTrait_Content, 0, WorkspaceRegion)

// Shell-dispatched Views have no visualizer hook, but still declare validity.
#define RD_DOCK_VIEW_REGISTRATIONS(X) \
  RD_DOCK_RENDERED_VIEWS(X) \
  X(getting_started, null, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(pending, null, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(watch, null, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(workspace_selector, null, RD_ViewTrait_Content|RD_ViewTrait_Section|RD_ViewTrait_SelectsWorkspaces|RD_ViewTrait_Singleton, 0, Sidebar)

#define RD_DOCK_DECLARE(name, ui, traits, width, host) \
  {str8_lit_comp(#name), traits, width, RD_DockHostKind_##host},
read_only global RD_ViewRegistration rd_view_registrations[] =
{
  RD_DOCK_VIEW_REGISTRATIONS(RD_DOCK_DECLARE)
};
#undef RD_DOCK_DECLARE

internal RD_ViewRegistration *rd_dock_view_from_name(String8 name);
internal RD_DockRule rd_dock_check(RD_ViewRegistration *view, RD_DockProposal proposal);
internal RD_DockHost rd_dock_host_from_cfg(CFG_Node *cfg, F32 width);
internal RD_DockRule rd_dock_placement(CFG_Node *view, CFG_Node *destination, F32 width);
// The production drag-target query, shared by all View drop sites.
internal B32 rd_dock_drag_target(CFG_Node *view, CFG_Node *destination, F32 width);
internal B32 rd_dock_can_create(String8 name, CFG_Node *destination);
internal B32 rd_dock_can_close(CFG_Node *view);
internal void rd_dock_restore_window(CFG_State *state, CFG_Node *window);

#endif
