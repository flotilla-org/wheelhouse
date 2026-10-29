// A Workspace's (or the sidebar's, or a Floating Panel's) arrangement as one
// typed document (ADR 0012, "Arrangements are documents"): panels with
// stable IDs, each panel's tabs (by View Address) and Selected View, and
// relative split weights. The root's axis is the document's; each level
// below alternates.
//
// The document's type and the rules that edit it, and its validity, are the
// core's (crates/wheelhouse-core/src/arrangement.rs, #516): each operation
// below sends this arrangement to the core as its edit document, applies one
// rule there and takes the result back, keeping the nodes of the panels that
// stay. What the shell keeps is the live copy of each owner's arrangement,
// which the renderer reads (rd_arrangement_set, shell_core.c "Live
// arrangements"), the read-only queries and the geometry. Commands and
// gestures change a live arrangement by intent (rd_arrangement_apply_intent);
// docking previews edit a copy they discard. A workspace's and the sidebar's
// are committed to the core whole, at the end of a gesture, and follow what
// the core makes of them (uishell_workspace_store.c, uishell_sidebar_store.c).
#ifndef SHELL_ARRANGEMENT_H
#define SHELL_ARRANGEMENT_H

#include "wheelhouse.h"

// Stable within its arrangement; 0 is no panel.
typedef U64 RD_PanelID;

// A Panel ID is unique only within its arrangement, so a panel is addressed
// by its arrangement's owner and its ID (CONTEXT.md, "Panel ID"). Whatever
// refers to a panel (the renderer's panel tree, UI keys, command registers,
// a window's Active Panel, a drag) does so by address.
typedef enum RD_PanelOwnerKind
{
  RD_PanelOwnerKind_Null,
  // A Workspace's panels. A window's own panels are a Workspace too.
  RD_PanelOwnerKind_Workspace,
  // The sidebar's dock: the Dashboard's arrangement, as one host shows it.
  RD_PanelOwnerKind_Sidebar,
} RD_PanelOwnerKind;

typedef struct RD_PanelOwner RD_PanelOwner;
struct RD_PanelOwner
{
  RD_PanelOwnerKind kind;
  // A Workspace's Workspace ID. For the sidebar, its window's Window ID.
  UIShell_WorkspaceId id;
};

typedef struct RD_PanelAddress RD_PanelAddress;
struct RD_PanelAddress
{
  RD_PanelOwner owner;
  RD_PanelID id;
};

read_only global RD_PanelAddress rd_nil_panel_address = {0};

// A View is addressed by its owner, its key in the owner's namespace, and
// its instance (CONTEXT.md, "View Address"). Whatever refers to a View (a
// panel's tabs, UI keys, command registers, its view state and Presentation
// State) does so by address; the View's config node, which holds its logical
// settings, is reached through it (rd_cfg_from_view). Its key is saved on
// that node:
//   Workspace  a Slot's View, `slot "<slot key>"`; its previous instance,
//              `previous_of "<slot key>"`; a View of a Floating Panel the
//              workspace hosts, `slot "u:<n>"`; a View made for a provider's
//              slot, until the workspace store keys it, `resource_id`
//   Dashboard  a sidebar section, `section "<section key>"`
//   Window     any other View of the window's sidebar or of a Floating Panel
//              the window hosts, `slot "u:<n>"`
//   Transient  a View of an immediate tree (rd_immediate_cfg_from_key): the
//              tree's key and the View's kind, which nothing saves
// The "u:<n>" keys of a Workspace, and of a window, are one namespace each:
// a View made there takes the next free one (rd_view_alloc), and one that
// moves there keeps its own unless a View there has it.
typedef enum RD_ViewOwnerKind
{
  RD_ViewOwnerKind_Null,
  RD_ViewOwnerKind_Workspace,
  RD_ViewOwnerKind_Dashboard,
  RD_ViewOwnerKind_Window,
  RD_ViewOwnerKind_Transient,
} RD_ViewOwnerKind;

typedef enum RD_ViewInstance
{
  RD_ViewInstance_Current,
  // The instance a keep-previous rebind kept beside its Slot's View.
  RD_ViewInstance_Previous,
} RD_ViewInstance;

// Plain data, without padding: copied, compared and hashed whole.
typedef struct RD_ViewAddress RD_ViewAddress;
struct RD_ViewAddress
{
  RD_ViewOwnerKind owner;
  RD_ViewInstance instance;
  // The owner's ID: a Workspace ID; for the Dashboard, and a window, the
  // Window ID of the window that shows it; zero for a transient host.
  UIShell_WorkspaceId id;
  // The key, hashed (rd_view_key_hash).
  U128 key;
};
StaticAssert(sizeof(RD_ViewAddress) == sizeof(RD_ViewOwnerKind)+sizeof(RD_ViewInstance)+sizeof(UIShell_WorkspaceId)+sizeof(U128), rd_view_address_unpadded);

read_only global RD_ViewAddress rd_nil_view_address = {0};

typedef struct RD_ViewAddressNode RD_ViewAddressNode;
struct RD_ViewAddressNode
{
  RD_ViewAddressNode *next;
  RD_ViewAddress v;
};

typedef struct RD_ViewAddressList RD_ViewAddressList;
struct RD_ViewAddressList
{
  RD_ViewAddressNode *first;
  RD_ViewAddressNode *last;
  U64 count;
};

typedef struct RD_ArrangementTab RD_ArrangementTab;
struct RD_ArrangementTab
{
  RD_ArrangementTab *next;
  RD_ArrangementTab *prev;
  RD_ViewAddress view;
};

typedef struct RD_ArrangementPanel RD_ArrangementPanel;
struct RD_ArrangementPanel
{
  RD_ArrangementPanel *first;
  RD_ArrangementPanel *last;
  RD_ArrangementPanel *next;
  RD_ArrangementPanel *prev;
  RD_ArrangementPanel *parent;
  U64 child_count;
  RD_PanelID id;
  // Relative to its siblings. Operations write weights at the config
  // formatter's %f precision and keep a split's weights summing to one;
  // weights a document came with are kept as it had them.
  F32 weight;
  RD_ArrangementTab *first_tab;
  RD_ArrangementTab *last_tab;
  U64 tab_count;
  // The Selected View: one of the tabs, or nil.
  RD_ViewAddress selected;
  // A panel that took another's tabs (rd_arrangement_add) takes that panel's
  // Presentation State with them when the arrangement is next set: the panel
  // it moves from.
  RD_PanelID options_from;
  // The ID a provider's document gave the panel, when it named it in words
  // (uishell_workspace_store.c); empty for one Wheelhouse numbered.
  String8 doc_id;
  // A safety restore took a sidebar section out of it (rd_dock_restore), so
  // the sidebar's next commit leaves it out with the section
  // (uishell_sidebar_store.c).
  B32 restore_emptied;
};

typedef struct RD_Arrangement RD_Arrangement;
struct RD_Arrangement
{
  Arena *arena;
  // Whose it is: its live copy is kept by owner.
  RD_PanelOwner owner;
  RD_ArrangementPanel *root;
  Axis2 root_axis;
  RD_PanelID next_id;
  // Tabs removed since it was copied, whose Views go when it is set unless
  // an arrangement has them again.
  RD_ArrangementTab *first_removed;
};

// A caller's rule for which tabs count, such as the Views shown in the
// current project. The module keeps no such policy of its own.
typedef B32 RD_ArrangementTabRule(RD_ViewAddress view);

typedef struct RD_ArrangementDrop RD_ArrangementDrop;
struct RD_ArrangementDrop
{
  // The panel the View landed in; 0 when the drop was refused.
  RD_PanelID panel;
  // The panel it left, when this arrangement has it.
  RD_PanelID source;
  // When the source closed, the leaf that takes focus if the source had it;
  // otherwise 0.
  RD_PanelID heir;
};

read_only global RD_ArrangementPanel rd_nil_arrangement_panel =
{
  &rd_nil_arrangement_panel,
  &rd_nil_arrangement_panel,
  &rd_nil_arrangement_panel,
  &rd_nil_arrangement_panel,
  &rd_nil_arrangement_panel,
};

// The renderer's view of an arrangement (rd_panel_tree_from_arrangement).
typedef struct RD_PanelNode RD_PanelNode;
struct RD_PanelNode
{
  RD_PanelNode *first;
  RD_PanelNode *last;
  RD_PanelNode *next;
  RD_PanelNode *prev;
  RD_PanelNode *parent;
  U64 child_count;
  RD_PanelAddress address;

  // rjf: split data
  Axis2 split_axis;
  F32 pct_of_parent;

  // rjf: tab params
  Side tab_side;

  // rjf: which tabs are attached
  RD_ViewAddressList tabs;
  // Nil when it has none.
  RD_ViewAddress selected_tab;
};

typedef struct RD_PanelTree RD_PanelTree;
struct RD_PanelTree
{
  RD_PanelNode *root;
  RD_PanelNode *focused;
};

typedef struct RD_PanelNodeRec RD_PanelNodeRec;
struct RD_PanelNodeRec
{
  RD_PanelNode *next;
  S32 push_count;
  S32 pop_count;
};

read_only global RD_PanelNode rd_nil_panel_node =
{
  &rd_nil_panel_node,
  &rd_nil_panel_node,
  &rd_nil_panel_node,
  &rd_nil_panel_node,
  &rd_nil_panel_node,
};

//- Documents
// An empty arrangement of `owner`'s: no panels until one is added.
internal RD_Arrangement *rd_arrangement_alloc(Arena *arena, RD_PanelOwner owner);
internal RD_Arrangement *rd_arrangement_copy(Arena *arena, RD_Arrangement *src);
// Whether two arrangements are one document: panels and their IDs, weights,
// tabs, Selected Views and Presentation State moves, and the root's axis.
internal B32 rd_arrangement_match(RD_Arrangement *a, RD_Arrangement *b);
// The %f string of a weight an operation wrote, or more digits for one a
// document came with that %f would change.
internal String8 rd_arrangement_weight_string(Arena *arena, F32 weight);

//- Panel addresses
internal B32 rd_panel_owner_match(RD_PanelOwner a, RD_PanelOwner b);
internal B32 rd_panel_address_match(RD_PanelAddress a, RD_PanelAddress b);
internal RD_PanelAddress rd_panel_address(RD_PanelOwner owner, RD_PanelID id);
internal String8 rd_panel_owner_kind_name(RD_PanelOwnerKind kind);

//- View addresses
internal RD_ViewAddress rd_view_address_make(RD_ViewOwnerKind owner, UIShell_WorkspaceId id, String8 key, RD_ViewInstance instance);
internal B32 rd_view_address_match(RD_ViewAddress a, RD_ViewAddress b);
internal B32 rd_view_address_is_nil(RD_ViewAddress view);
internal U128 rd_view_key_hash(String8 key);
internal String8 rd_view_owner_kind_name(RD_ViewOwnerKind kind);
internal void rd_view_address_list_push(Arena *arena, RD_ViewAddressList *list, RD_ViewAddress view);

//- Panel Presentation State
// What the shell keeps beside an arrangement for each of its panels
// (shell_core.c, "Panel Presentation State"), by panel address.
typedef struct RD_PanelPresentation RD_PanelPresentation;
struct RD_PanelPresentation
{
  // The focus mark: the last marked panel in tree order has focus.
  B32 focused;
  B32 tabs_on_bottom;
};
typedef RD_PanelPresentation RD_PanelPresentationHook(RD_PanelAddress panel);
global RD_PanelPresentationHook *rd_panel_presentation_hook = 0;

//- The renderer's panel tree
// The arrangement's panels, addressed as `owner`'s, with their tabs and each
// panel's Presentation State (rd_panel_presentation_hook).
internal RD_PanelTree rd_panel_tree_from_arrangement(Arena *arena, RD_PanelOwner owner, RD_Arrangement *arrangement);
internal RD_PanelNodeRec rd_panel_node_rec__depth_first(RD_PanelNode *root, RD_PanelNode *panel, U64 sib_off, U64 child_off);
#define rd_panel_node_rec__depth_first_pre(root, p)     rd_panel_node_rec__depth_first((root), (p), OffsetOf(RD_PanelNode, next), OffsetOf(RD_PanelNode, first))
#define rd_panel_node_rec__depth_first_pre_rev(root, p) rd_panel_node_rec__depth_first((root), (p), OffsetOf(RD_PanelNode, prev), OffsetOf(RD_PanelNode, last))
// The panel with `id` under `root`, or the nil panel.
internal RD_PanelNode *rd_panel_node_from_id(RD_PanelNode *root, RD_PanelID id);
internal Rng2F32 rd_target_rect_from_panel_node_child(Rng2F32 parent_rect, RD_PanelNode *parent, RD_PanelNode *panel);
internal Rng2F32 rd_target_rect_from_panel_node(Rng2F32 root_rect, RD_PanelNode *root, RD_PanelNode *panel);

//- Queries; each returns the nil panel when there is none
// The panel after `panel` in tree order (pre-order) under `root`.
internal RD_ArrangementPanel *rd_arrangement_next(RD_ArrangementPanel *root, RD_ArrangementPanel *panel);
internal RD_ArrangementPanel *rd_arrangement_panel_from_id(RD_Arrangement *arrangement, RD_PanelID id);
internal RD_ArrangementPanel *rd_arrangement_panel_from_view(RD_Arrangement *arrangement, RD_ViewAddress view);
// `view`'s tab in `panel`, or 0.
internal RD_ArrangementTab *rd_arrangement_tab_from_view(RD_ArrangementPanel *panel, RD_ViewAddress view);
internal Axis2 rd_arrangement_split_axis(RD_Arrangement *arrangement, RD_ArrangementPanel *panel);
// The rectangle the renderer lays the panel out in, rounded as it rounds.
internal Rng2F32 rd_arrangement_rect(RD_Arrangement *arrangement, Rng2F32 area, RD_ArrangementPanel *panel);
// The first broken invariant, or empty. Operations keep a valid arrangement valid.
internal String8 rd_arrangement_problem(Arena *arena, RD_Arrangement *arrangement);

//- The core's edit document
// The arrangement as the core's edit document (wheelhouse.h), in `arena`; a
// tab is its View Address.
internal WheelhouseEditDoc rd_core_doc_from_arrangement(Arena *arena, RD_Arrangement *arrangement);
// Takes an edited document back into `arrangement`. A panel that stays
// keeps its node; one the edit took away is left unlinked and empty.
internal void rd_arrangement_take(RD_Arrangement *arrangement, const WheelhouseEditDoc *doc);
internal WheelhouseTab rd_core_tab(RD_ViewAddress view);
internal RD_ViewAddress rd_view_from_core_tab(const WheelhouseTab *tab);

//- Operations
// Each applies one of the core's rules (arrangement::Edit) to the
// arrangement in place.
// Replaces every panel with one empty root panel and returns it, as a layout
// producer starts a document (a reset, a new workspace, a Suggested Layout).
// The root's axis is kept until it splits. Every View it had goes when it is
// set, but those a tab takes back.
internal RD_PanelID rd_arrangement_clear(RD_Arrangement *arrangement);
// Inserts a sibling beside `panel` when its parent splits along `dir`'s axis,
// rescaling the siblings; otherwise bisects `panel`. Returns the new panel.
internal RD_PanelID rd_arrangement_split(RD_Arrangement *arrangement, RD_PanelID panel, Dir2 dir);
// Adds an empty panel as `parent`'s last child and returns it; with no
// panels, `parent` 0 makes the root first. A parent with tabs first moves
// them, its Selected View and its Presentation State into a new first child
// of weight 1, keeping its own ID. With `weight` positive the new panel
// takes it and its siblings keep theirs, as the sidebar's sections do until
// it sizes them; with 0 it takes an equal share, and its siblings scale to
// make room, each counted as at least 0.01.
internal RD_PanelID rd_arrangement_add(RD_Arrangement *arrangement, RD_PanelID parent, F32 weight);
// Removes `panel` and its Views as the sidebar's reconciliation does: its
// siblings keep their weights, and a split it leaves with one panel stays a
// split. A split it leaves with none goes too, short of the root. Refuses
// the root.
internal B32 rd_arrangement_remove(RD_Arrangement *arrangement, RD_PanelID panel);
// Removes `panel` and its Views. Its siblings grow into its space; a split
// left with one child is replaced by it, merging into a parent along the same
// axis. Returns the leaf that takes focus if `panel` had it; 0 for the root.
internal RD_PanelID rd_arrangement_close(RD_Arrangement *arrangement, RD_PanelID panel);
// Whether `panel` goes when its last tab does (#358). One a provider's
// document named (`doc_id`, a Workspace Layout's `files`, say) is part of
// that document's plan, which may leave it empty, so it stays (#445).
internal B32 rd_arrangement_closes_when_emptied(RD_ArrangementPanel *panel);
// Moves `view` into `destination` after `prev_view` (nil or not there:
// first) and selects it there. The View may be another arrangement's, or a
// new View in none: setting this arrangement makes it this owner's. A panel
// it leaves while selected has no Selected View until one is selected.
internal B32 rd_arrangement_move_tab(RD_Arrangement *arrangement, RD_ViewAddress view, RD_PanelID destination, RD_ViewAddress prev_view);
// As move_tab, but `destination` keeps its Selected View unless `select`, as
// a restore moves a View that was not selected where it was.
internal B32 rd_arrangement_insert_tab(RD_Arrangement *arrangement, RD_ViewAddress view, RD_PanelID destination, RD_ViewAddress prev_view, B32 select);
// Removes `view`'s tab but keeps its View, for another arrangement to take
// with move_tab. A panel it leaves while selected has no Selected View until
// one is selected.
internal B32 rd_arrangement_detach_tab(RD_Arrangement *arrangement, RD_ViewAddress view);
// Drops `view` into `destination` (`dir` invalid) or into the new panel
// splitting `destination` along `dir` makes, after `prev_view` as move_tab
// places it, and settles the panel it left, when this arrangement has it, in
// the same edit (rd_arrangement_settle). A move within one panel settles
// nothing; a split of the source itself never closes it. With no View (nil)
// it only splits, which is what measuring a drop needs before one is chosen.
internal RD_ArrangementDrop rd_arrangement_drop(RD_Arrangement *arrangement, RD_ViewAddress view, RD_PanelID destination, Dir2 dir, RD_ViewAddress prev_view, RD_ArrangementTabRule *shown);
// Settles `panel` after a drop took a View out of it. After a move (`dir`
// invalid) it selects its first tab `shown` accepts, and closes it as close
// does when there is none. After a split it selects that tab only when it has
// no Selected View, and closes it only when it has no tabs left. `shown` 0
// accepts every tab; with `may_close` 0 the panel stays open. Returns whether
// anything changed.
internal B32 rd_arrangement_settle(RD_Arrangement *arrangement, RD_PanelID panel, Dir2 dir, RD_ArrangementTabRule *shown, B32 may_close);
// Removes `view`'s tab; setting the arrangement releases its View, unless an
// arrangement has it again by then. A panel it leaves while selected has no
// Selected View until one is selected.
internal B32 rd_arrangement_remove_tab(RD_Arrangement *arrangement, RD_ViewAddress view);
// Removes every panel and View: an emptied Floating Panel goes when it is set.
internal void rd_arrangement_discard(RD_Arrangement *arrangement);
// Makes `view`, one of `panel`'s tabs, its Selected View; nil selects none.
internal B32 rd_arrangement_select(RD_Arrangement *arrangement, RD_PanelID panel, RD_ViewAddress view);
// Moves `panel` among its siblings to after `prev` (0: first), with its
// weight.
internal B32 rd_arrangement_reorder(RD_Arrangement *arrangement, RD_PanelID panel, RD_PanelID prev);
// Moves the boundary after `panel` by `delta` of its parent, keeping both
// sides at least `floor` where they can. Siblings whose weights drifted from
// summing to one are rescaled first.
internal void rd_arrangement_resize(RD_Arrangement *arrangement, RD_PanelID panel, F32 delta, F32 floor);
// Gives the two sides of the boundary after `panel` equal shares of their
// space, rescaling drifted siblings as resizing does.
internal void rd_arrangement_equalize(RD_Arrangement *arrangement, RD_PanelID panel);
// Every tab of `from` (and a Selected View) becomes `to`: the View moved to
// another owner, or its key changed. Returns whether it had one.
internal B32 rd_arrangement_readdress(RD_Arrangement *arrangement, RD_ViewAddress from, RD_ViewAddress to);

#endif
