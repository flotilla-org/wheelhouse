// Declared View/host validity, independent of gestures and rendering.
#ifndef SHELL_DOCKING_H
#define SHELL_DOCKING_H

// Restore deliberately ignores physical width: resizing is not structural repair.
#define RD_DOCK_UNMEASURED_WIDTH 3.402823466e38f

#define RD_DOCK_SIDEBAR_ROOT str8_lit("control_views")

typedef enum RD_DockHostKind
{
  RD_DockHostKind_Sidebar,
  RD_DockHostKind_WorkspaceRegion,
  RD_DockHostKind_FloatingPanel,
  RD_DockHostKind_COUNT,
} RD_DockHostKind;

// Presentation belongs to the host; validity remains an independent query.
typedef enum RD_DockPresentation
{
  RD_DockPresentation_Tabs,
  RD_DockPresentation_SectionHeader,
} RD_DockPresentation;

internal RD_DockPresentation rd_dock_presentation(RD_DockHostKind host, U64 tab_count);

typedef U32 RD_ViewTraits;
enum
{
  RD_ViewTrait_Content = 1<<0,
  RD_ViewTrait_Section = 1<<1,
  RD_ViewTrait_SelectsWorkspaces = 1<<2,
  RD_ViewTrait_Singleton = 1<<3,
  RD_ViewTrait_NeedsWorkspaceSubject = 1<<4,
  RD_ViewTrait_NeedsHost = 1<<5,
  // Content supplied by the root Controlled Split, not a child workspace.
  RD_ViewTrait_ControlSplitScope = 1<<6,
  // Laying it out again at another size is harmless, so its drag floater
  // shows it live rather than as its texture (#253).
  RD_ViewTrait_LiveDragPreview = 1<<7,
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
  // Layout owner identity; independent of visual host kind or screen position.
  CFG_ID level;
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
  RD_DockRule_ControlSplitLevel,
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
  // The View's existing owner, rather than the proposed destination's scope.
  CFG_ID view_level;
};

// This is also the UI registration list. Adding a View requires declaring its
// traits here, so enumeration and rendering cannot acquire separate lists.
#define RD_DOCK_RENDERED_VIEWS(X) \
  X(pinned_cards, pinned_cards, RD_ViewTrait_Content|RD_ViewTrait_Section|RD_ViewTrait_ControlSplitScope, 0, Sidebar) \
  X(sidebar_section, sidebar_section, RD_ViewTrait_Content|RD_ViewTrait_Section|RD_ViewTrait_ControlSplitScope, 0, Sidebar) \
  X(text, shell_text, RD_ViewTrait_Content|RD_ViewTrait_LiveDragPreview, 0, WorkspaceRegion) \
  X(jackstay, jackstay, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(terminal, terminal, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(scroll_region_fixture, scroll_region_fixture, RD_ViewTrait_Content, 128, WorkspaceRegion) \
  X(terminal_fixture, terminal, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(sessions, sessions, RD_ViewTrait_Content|RD_ViewTrait_Section, 0, WorkspaceRegion) \
  X(binary, binary, RD_ViewTrait_Content|RD_ViewTrait_LiveDragPreview, 0, WorkspaceRegion) \
  X(bitmap, bitmap, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(color, color, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(geo3d, geo3d, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(placeholder, placeholder, RD_ViewTrait_Content, 0, WorkspaceRegion)

// Shell-dispatched Views have no visualizer hook, but still declare validity.
#define RD_DOCK_VIEW_REGISTRATIONS(X) \
  RD_DOCK_RENDERED_VIEWS(X) \
  X(getting_started, null, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(pending, null, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(watch, null, RD_ViewTrait_Content, 0, WorkspaceRegion) \
  X(workspace_selector, null, RD_ViewTrait_Content|RD_ViewTrait_Section|RD_ViewTrait_SelectsWorkspaces|RD_ViewTrait_Singleton|RD_ViewTrait_ControlSplitScope, 0, Sidebar)

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
// The Floating Panel `cfg` is in or is: the root of its own arrangement. Nil
// outside Floating Panels, and for a View saved directly in the host.
internal CFG_Node *rd_dock_floating_panel_from_cfg(CFG_Node *cfg);
internal RD_DockRule rd_dock_placement(CFG_Node *view, CFG_Node *destination, F32 width);
// The production drag-target query, shared by all View drop sites.
internal B32 rd_dock_drag_target(CFG_Node *view, CFG_Node *destination, F32 width);
internal RD_DockRule rd_dock_creation(String8 name, CFG_Node *destination);
internal RD_DockRule rd_dock_closure(CFG_Node *view);
internal String8 rd_dock_rule_message(RD_DockRule rule);
internal B32 rd_dock_can_create(String8 name, CFG_Node *destination);
internal B32 rd_dock_can_close(CFG_Node *view);

//- Repair: restore, the sidebar's edits outside docking

// The arrangements a repair edits, each loaded once and saved together.
typedef struct RD_DockDocument RD_DockDocument;
struct RD_DockDocument
{
  RD_DockDocument *next;
  RD_Arrangement *arrangement;
  B32 dirty;
  // It took a View from another arrangement, so it saves first: the panel
  // node that View leaves may be released when its own arrangement saves.
  B32 took;
};

typedef struct RD_DockDocuments RD_DockDocuments;
struct RD_DockDocuments
{
  Arena *arena;
  RD_DockDocument *first;
  RD_DockDocument *last;
};

// A registered View saved under a layout container, as the pre-pass found it.
typedef struct RD_DockSavedView RD_DockSavedView;
struct RD_DockSavedView
{
  RD_DockSavedView *next;
  CFG_Node *view;
  // The arrangement and panel that have it as a tab. A stray, saved in no
  // panel, has the ones whose tab (`holder`) it is saved inside, or none.
  RD_DockDocument *document;
  RD_ArrangementPanel *panel;
  CFG_ID holder;
};

typedef struct RD_DockSavedViewList RD_DockSavedViewList;
struct RD_DockSavedViewList
{
  RD_DockSavedView *first;
  RD_DockSavedView *last;
  U64 count;
};

// The arrangement saved as `owner`'s `root_name` child, loaded (or, with
// none, an empty one that saves there) the first time it is asked for.
internal RD_DockDocument *rd_dock_document_from_owner(RD_DockDocuments *documents, CFG_Node *owner, String8 root_name);
// Adds an arrangement the repair made, such as a new Floating Panel.
internal RD_DockDocument *rd_dock_documents_add(RD_DockDocuments *documents, RD_Arrangement *arrangement);
// Loads each Floating Panel in `host`.
internal void rd_dock_documents_add_floating(RD_DockDocuments *documents, CFG_Node *host);
// The pre-pass: every registered View saved under `container`'s layout
// containers, in config order (a split's tabs and child panels interleaved,
// as the file reads them), and which of the loaded arrangements has it. The
// one walk of saved layout config; it also finds the strays no arrangement
// loads: Views directly under a workspace or a Floating Panels host, in a
// second `panels` or `control_views` root, or inside a tab.
internal RD_DockSavedViewList rd_dock_saved_views(RD_DockDocuments *documents, CFG_Node *container);
// Whether the pre-pass found `view` as a tab of an arrangement.
internal B32 rd_dock_saved_view_is_tab(RD_DockSavedView *view);
// Saves the arrangements marked dirty: those that took Views first, then the
// rest, each in the order they were loaded.
internal void rd_dock_documents_save(CFG_State *state, RD_DockDocuments *documents);
// The leaf a View restore moves goes to: down the first children of the
// root, which is made when there is none.
internal RD_ArrangementPanel *rd_dock_restore_leaf(RD_Arrangement *arrangement);
// Repairs a window's saved layouts through their arrangements: one copy of
// each singleton, and each View saved where the checker refuses it moved to
// its default host.
internal void rd_dock_restore_window(CFG_State *state, CFG_Node *window);

// The body width `destination` (or the panel a split there makes) would have
// once a drop of `view` completes, measured on a copy; `shown` is the rule
// the drop commands settle its source with. With no View (0) it measures the
// split alone.
internal F32 rd_dock_moving_width(RD_Arrangement *arrangement, RD_PanelID destination, CFG_ID view,
                                 RD_ArrangementTabRule *shown, Rng2F32 area, Dir2 dir, F32 inset);

#endif
