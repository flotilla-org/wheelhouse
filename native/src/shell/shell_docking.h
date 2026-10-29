// Declared View/host validity, independent of gestures and rendering. The
// rule, and restore's repair, are the core's (crates/wheelhouse-core/src/
// dock.rs, through wheelhouse.h): the shell supplies its Renderers'
// registrations, hosts measured in pixels and config, and instance counts.
#ifndef SHELL_DOCKING_H
#define SHELL_DOCKING_H

#include "wheelhouse.h"

// Restore deliberately ignores physical width: resizing is not structural repair.
#define RD_DOCK_UNMEASURED_WIDTH 3.402823466e38f

typedef enum RD_DockHostKind
{
  RD_DockHostKind_Sidebar = WHEELHOUSE_DOCK_HOST_SIDEBAR,
  RD_DockHostKind_WorkspaceRegion = WHEELHOUSE_DOCK_HOST_WORKSPACE_REGION,
  // No host kind: no destination.
  RD_DockHostKind_COUNT = WHEELHOUSE_DOCK_HOST_NONE,
} RD_DockHostKind;

// Presentation belongs to the host; validity remains an independent query.
typedef enum RD_DockPresentation
{
  RD_DockPresentation_Tabs,
  RD_DockPresentation_SectionHeader,
} RD_DockPresentation;

internal RD_DockPresentation rd_dock_presentation(RD_DockHostKind host, U64 tab_count);

// The core's trait bits.
typedef U32 RD_ViewTraits;
enum
{
  RD_ViewTrait_Content = WHEELHOUSE_VIEW_TRAIT_CONTENT,
  RD_ViewTrait_Section = WHEELHOUSE_VIEW_TRAIT_SECTION,
  RD_ViewTrait_SelectsWorkspaces = WHEELHOUSE_VIEW_TRAIT_SELECTS_WORKSPACES,
  RD_ViewTrait_Singleton = WHEELHOUSE_VIEW_TRAIT_SINGLETON,
  RD_ViewTrait_NeedsWorkspaceSubject = WHEELHOUSE_VIEW_TRAIT_NEEDS_WORKSPACE_SUBJECT,
  // Content supplied by the root Controlled Split, not a child workspace.
  RD_ViewTrait_ControlSplitScope = WHEELHOUSE_VIEW_TRAIT_CONTROL_SPLIT_SCOPE,
  // Laying it out again at another size is harmless, so its drag floater
  // shows it live rather than as its texture (#253).
  RD_ViewTrait_LiveDragPreview = WHEELHOUSE_VIEW_TRAIT_LIVE_DRAG_PREVIEW,
  // Draws a value: a Lens type of its name opens it, a watch cell or a hover
  // evaluation can embed it, and headless tests draw it as nothing.
  RD_ViewTrait_Visualizer = WHEELHOUSE_VIEW_TRAIT_VISUALIZER,
  // A dragged expression dropped on it becomes its expression.
  RD_ViewTrait_ExpressionDrop = WHEELHOUSE_VIEW_TRAIT_EXPRESSION_DROP,
  // Draws its evaluated expression, from its context's `legacy_eval`: the
  // evaluator escape hatch until the evaluator is retired (#313).
  RD_ViewTrait_LegacyEval = WHEELHOUSE_VIEW_TRAIT_LEGACY_EVAL,
  // Declares a producer version (terminal: render generation, text: content
  // hash), so shape-only workspace preservation is safe for it.
  RD_ViewTrait_VersionedSurface = WHEELHOUSE_VIEW_TRAIT_VERSIONED_SURFACE,
  // Offered by name where a View is made from an expression (the view lister).
  RD_ViewTrait_Listed = WHEELHOUSE_VIEW_TRAIT_LISTED,
  // A control view: it shows the Dashboard's arrangement, not a Slot.
  RD_ViewTrait_Control = WHEELHOUSE_VIEW_TRAIT_CONTROL,
  // One of the shell's own pages (watch lists, the pending loader, getting
  // started): presses keep focus on its query bar, and floating, it has no
  // pull-out button.
  RD_ViewTrait_QueryFocus = WHEELHOUSE_VIEW_TRAIT_QUERY_FOCUS,
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
  RD_DockRule_Valid = WHEELHOUSE_DOCK_VALID,
  RD_DockRule_RegisteredView = WHEELHOUSE_DOCK_REGISTERED_VIEW,
  RD_DockRule_HostAcceptance = WHEELHOUSE_DOCK_HOST_ACCEPTANCE,
  RD_DockRule_WorkspaceSubject = WHEELHOUSE_DOCK_WORKSPACE_SUBJECT,
  RD_DockRule_MinimumWidth = WHEELHOUSE_DOCK_MINIMUM_WIDTH,
  RD_DockRule_Singleton = WHEELHOUSE_DOCK_SINGLETON,
  RD_DockRule_OneControlSurface = WHEELHOUSE_DOCK_ONE_CONTROL_SURFACE,
  RD_DockRule_SelectorOutsideSelectedRegion = WHEELHOUSE_DOCK_SELECTOR_OUTSIDE_SELECTED_REGION,
  RD_DockRule_SelectorCannotClose = WHEELHOUSE_DOCK_SELECTOR_CANNOT_CLOSE,
  RD_DockRule_ControlSplitLevel = WHEELHOUSE_DOCK_CONTROL_SPLIT_LEVEL,
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

// Docking's part of each Renderer in the registry, in its order.
#include "uishell/uishell_renderers.h"
#define RD_DOCK_DECLARE(name, ui, state, content, traits, width, host, expand, settings, placeholder) \
  {str8_lit_comp(#name), traits, width, RD_DockHostKind_##host},
read_only global RD_ViewRegistration rd_view_registrations[] =
{
  WH_RENDERERS(RD_DOCK_DECLARE)
};
#undef RD_DOCK_DECLARE

// A View as docking checks it: what it is, and where it is now, as the shell
// finds it by its View Address (rd_dock_view_from_address).
typedef struct RD_DockView RD_DockView;
struct RD_DockView
{
  // Whether there is such a View.
  B32 exists;
  // 0 for saved content that isn't a registered View.
  RD_ViewRegistration *registration;
  // The host it is in, and the level (layout owner) it belongs to.
  RD_DockHost source;
  CFG_ID level;
};

// How many Views of `registration` the Controlled Split `controlled_split`
// has: the shell counts the tabs of its window's arrangements.
typedef U32 RD_DockInstanceCount(CFG_ID controlled_split, RD_ViewRegistration *registration);
global RD_DockInstanceCount *rd_dock_instance_count = 0;

internal RD_ViewRegistration *rd_dock_view_from_name(String8 name);
internal RD_DockRule rd_dock_check(RD_ViewRegistration *view, RD_DockProposal proposal);
// The kind of host a panel owner's panels are in.
internal RD_DockHostKind rd_dock_host_kind_from_panel_owner(RD_PanelOwner owner);
// The level a View in `host` belongs to: its host's, or for a View of the
// root Controlled Split's own scope, that split's.
internal CFG_ID rd_dock_view_level(RD_ViewRegistration *registration, RD_DockHost host);
// Placing `view` in a panel of `destination`, the host that panel is in at
// its available width (the shell's rd_dock_host_from_panel); a host of no
// kind (RD_DockHostKind_COUNT) is no destination.
internal RD_DockRule rd_dock_placement(RD_DockView view, RD_DockHost destination);
// The production drag-target query, shared by all View drop sites.
internal B32 rd_dock_drag_target(RD_DockView view, RD_DockHost destination);
internal RD_DockRule rd_dock_creation(String8 name, RD_DockHost destination);
internal RD_DockRule rd_dock_closure(RD_DockView view);
internal String8 rd_dock_rule_message(RD_DockRule rule);
internal B32 rd_dock_can_create(String8 name, RD_DockHost destination);
internal B32 rd_dock_can_close(RD_DockView view);

//- Repair: restore, over a window's arrangements

// An arrangement a restore checks and may edit, with the host its panels
// are in.
typedef struct RD_DockDocument RD_DockDocument;
struct RD_DockDocument
{
  RD_DockDocument *next;
  RD_Arrangement *arrangement;
  RD_DockHost host;
  B32 dirty;
};

typedef struct RD_DockDocuments RD_DockDocuments;
struct RD_DockDocuments
{
  Arena *arena;
  RD_DockDocument *first;
  RD_DockDocument *last;
  // Where refused Views go by default host: the sidebar's dock, and the
  // window's first workspace (else its own panels).
  RD_DockDocument *sidebar;
  RD_DockDocument *workspace;
};

// What a View is, by its address: the shell reads its config node's kind.
typedef RD_ViewRegistration *RD_DockRegistration(RD_ViewAddress view);

internal RD_DockDocument *rd_dock_documents_add(RD_DockDocuments *documents, RD_Arrangement *arrangement, RD_DockHost host);
// The leaf a View restore moves goes to: down the first children of the
// root, which is made when there is none.
internal RD_ArrangementPanel *rd_dock_restore_leaf(RD_Arrangement *arrangement);
// Repairs a window's arrangements, every one in `documents`: one copy of
// each singleton, and each View its host refuses moved to its default host,
// keeping its address. Marks the documents it edits dirty, and returns the
// sidebar sections it moved, which the sidebar places by their hints once
// committed (uishell_sidebar_store.c).
internal RD_ViewAddressList rd_dock_restore(RD_DockDocuments *documents, RD_DockRegistration *registration);

// The body width `destination` (or the panel a split there makes) would have
// once a drop of `view` completes, measured on a copy; `shown` is the rule
// the drop commands settle its source with. With no View (nil) it measures
// the split alone.
internal F32 rd_dock_moving_width(RD_Arrangement *arrangement, RD_PanelID destination, RD_ViewAddress view,
                                 RD_ArrangementTabRule *shown, Rng2F32 area, Dir2 dir, F32 inset);

#endif
