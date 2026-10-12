// A Workspace's (or the sidebar's) arrangement as one typed document
// (ADR 0012, "Arrangements are documents"): panels with stable IDs, each
// panel's tabs and Selected View, and relative split weights. Docking
// previews edit a copy; commands edit a loaded document and save it back to
// the config nodes it came from.
//
// Config shape. For a workspace this is Wheelhouse's live copy: Andamento's
// workspace record keeps the document, which the presentation file leaves
// out (uishell_workspace_store.c), and each View carries its Slot's key.
//   panels:{id:1 0.6:{id:2 terminal:{slot:"u:1" selected ...}} 0.4:{id:3 text:{...}}}
// The root is `panels`, the sidebar's `control_views`, or a Floating Panel's
// node in its `floating_panels` host, which keeps its name; its owner's
// `split_x` (or `control_views_split_x`) gives the root's axis, and each
// level below alternates. Where a Floating Panel floats is not in it. A child panel's node string is its weight. `id`
// holds the panel's stable ID: panels without one (older files, hand edits,
// reset layouts) get the next free IDs in tree order when loaded, and keep
// them once saved. A duplicated ID is replaced the same way.
#ifndef SHELL_ARRANGEMENT_H
#define SHELL_ARRANGEMENT_H

// Stable within its arrangement; 0 is no panel.
typedef U64 RD_PanelID;

// A Panel ID is unique only within its arrangement, so a panel is addressed
// by its arrangement's owner and its ID (CONTEXT.md, "Panel ID"). Whatever
// refers to a panel (the renderer's panel tree, UI keys, command registers,
// a window's Active Panel, a drag) does so by address; the config node a
// panel is mirrored to is reached through it (rd_cfg_from_panel).
typedef enum RD_PanelOwnerKind
{
  RD_PanelOwnerKind_Null,
  // A Workspace's panels. A window's own panels are a Workspace too.
  RD_PanelOwnerKind_Workspace,
  // The sidebar's dock: the Dashboard's arrangement, as one host shows it.
  RD_PanelOwnerKind_Sidebar,
  // A Floating Panel's arrangement.
  RD_PanelOwnerKind_Floating,
} RD_PanelOwnerKind;

typedef struct RD_PanelOwner RD_PanelOwner;
struct RD_PanelOwner
{
  RD_PanelOwnerKind kind;
  // A Workspace's Workspace ID. For the sidebar and Floating Panels, their
  // host's: its window's Window ID, or the Workspace ID of a workspace that
  // hosts them.
  UIShell_WorkspaceId id;
  // A Floating Panel's place among its host's, from 1, as the sidebar's
  // document names them (uishell_sidebar_store.c); 0 for other owners.
  U64 floating;
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
// State) does so by address; the config node a View is mirrored to is
// reached through it (rd_cfg_from_view). Its key is saved on that node:
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
// saving an arrangement gives a View without a key the next free one
// (rd_view_key_assign).
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
  // The View's config node, which saving moves but never recreates. Its key
  // names it in its owner (its View Address), and a Slot's names it in
  // Andamento's document.
  CFG_ID view;
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
  // loaded weights are kept as saved, even hand-edited ones that do not.
  F32 weight;
  RD_ArrangementTab *first_tab;
  RD_ArrangementTab *last_tab;
  U64 tab_count;
  // The Selected View: one of the tabs, or 0.
  CFG_ID selected;
  // The config node it was loaded from or saved to; 0 until first saved.
  CFG_ID cfg;
  // A panel that took another's tabs (rd_arrangement_add) takes that panel's
  // Presentation State with them when first saved: the panel it moves from.
  RD_PanelID options_from;
};

typedef struct RD_Arrangement RD_Arrangement;
struct RD_Arrangement
{
  Arena *arena;
  RD_ArrangementPanel *root;
  Axis2 root_axis;
  RD_PanelID next_id;
  // Where it saves: the root's owner, the root's name and axis key, and the
  // root panel node it was loaded from (saving releases the panel nodes under
  // it that the arrangement no longer uses).
  CFG_ID owner;
  String8 root_name;
  String8 axis_key;
  CFG_ID saved_root;
  // Tabs removed since the last save, whose View nodes saving releases.
  RD_ArrangementTab *first_removed;
};

// A caller's rule for which tabs count, such as the Views shown in the
// current project. The module keeps no such policy of its own.
typedef B32 RD_ArrangementTabRule(CFG_ID view);

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

// The config keys around a root panel node, by the root's name.
typedef struct RD_ArrangementKeys RD_ArrangementKeys;
struct RD_ArrangementKeys
{
  CFG_Node *owner;
  String8 root_name;
  String8 axis_key;
};

//- Loading and saving
internal RD_ArrangementKeys rd_arrangement_keys(Arena *arena, CFG_Node *root);
// Empty (no root panel) when `panels_root` is nil.
internal RD_Arrangement *rd_arrangement_from_cfg(Arena *arena, CFG_Node *panels_root);
// The arrangement saved as `owner`'s `root_name` child. Without one, it is
// empty and saves there once it has panels.
internal RD_Arrangement *rd_arrangement_from_owner(Arena *arena, CFG_Node *owner, String8 root_name);
// An empty arrangement that saves as a new `root_name` child of `owner`, after
// any it has, once it has panels: a new Floating Panel in its host.
internal RD_Arrangement *rd_arrangement_new(Arena *arena, CFG_Node *owner, String8 root_name);
internal RD_Arrangement *rd_arrangement_copy(Arena *arena, RD_Arrangement *src);
// Edits the loaded config nodes in place: panel and View nodes keep their
// identity and their other children, unchanged values are not rewritten, and
// only removed tabs' and closed panels' Views are released. An arrangement
// with no panels writes nothing, but still releases the Views removed from it,
// and the root node it was loaded from when it was discarded.
internal void rd_arrangement_save(CFG_State *state, RD_Arrangement *arrangement);

//- Panel addresses
internal B32 rd_panel_owner_match(RD_PanelOwner a, RD_PanelOwner b);
internal B32 rd_panel_address_match(RD_PanelAddress a, RD_PanelAddress b);
internal RD_PanelAddress rd_panel_address(RD_PanelOwner owner, RD_PanelID id);

//- View addresses
// Where a host keeps its Views' Presentation State (rd_view_presentation):
// `view_presentation:{<owner kind name>:{key:"<key>" [previous] <settings>}}`.
#define RD_VIEW_PRESENTATION_ROOT str8_lit("view_presentation")
internal RD_ViewAddress rd_view_address_make(RD_ViewOwnerKind owner, UIShell_WorkspaceId id, String8 key, RD_ViewInstance instance);
internal B32 rd_view_address_match(RD_ViewAddress a, RD_ViewAddress b);
internal B32 rd_view_address_is_nil(RD_ViewAddress view);
internal U128 rd_view_key_hash(String8 key);
internal String8 rd_view_owner_kind_name(RD_ViewOwnerKind kind);
internal void rd_view_address_list_push(Arena *arena, RD_ViewAddressList *list, RD_ViewAddress view);
// The key saved on a View node, and its instance; empty when it has none.
internal String8 rd_view_key_from_cfg(CFG_Node *view, RD_ViewInstance *instance_out);
// Which owner `view`, a tab of an arrangement, belongs to, and the node its
// namespace is kept on: its workspace, or a window (also when the window's
// own panels are its workspace). Null outside an arrangement.
internal RD_ViewOwnerKind rd_view_owner_kind_from_tab(CFG_Node *view, CFG_Node **host_out);
// The address of `view`, a tab of an arrangement whose panels are `owner`'s;
// nil when it has no key.
internal RD_ViewAddress rd_view_address_from_tab(RD_PanelOwner owner, CFG_Node *view);
// The Views of `host`'s namespace for `owner`: the tabs of its arrangements
// that are `owner`'s, in tree order.
internal CFG_NodePtrList rd_view_namespace_views(Arena *arena, CFG_Node *host, RD_ViewOwnerKind owner);
// The highest n of a "u:<n>" key among `host`'s Views of `owner` and the
// Presentation State it keeps for them; 0 for none.
internal U64 rd_view_user_number_max(CFG_Node *host, RD_ViewOwnerKind owner);
// Gives `view`, a tab of a Workspace or a window, the next free "u:<n>" of
// its owner's namespace when it has no key or, with `unique`, when another
// View there has its key. Sections keep theirs. Returns whether it did.
internal B32 rd_view_key_assign(CFG_State *state, CFG_Node *view, B32 unique);
// Told of each View node a save moves from elsewhere, before the move
// (`moved` 0) and after it: the shell keeps a View's state and Presentation
// State with it when the move changes its address.
typedef void RD_ArrangementViewMoveHook(CFG_Node *view, B32 moved);
global RD_ArrangementViewMoveHook *rd_arrangement_view_move_hook = 0;

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
// Told of each arrangement a save wrote, `root` its root panel node (before
// a discarded one goes): the shell moves a lifted panel's Presentation State
// to it (`options_from`), and drops that of panels it no longer has.
typedef void RD_ArrangementSavedHook(CFG_Node *root, RD_Arrangement *arrangement);
global RD_ArrangementSavedHook *rd_arrangement_saved_hook = 0;
internal String8 rd_panel_owner_kind_name(RD_PanelOwnerKind kind);

//- The renderer's panel tree
// The arrangement's panels, addressed as `owner`'s, and their tabs by View
// Address, with each panel's Presentation State (rd_panel_presentation_hook).
// Valid until the config changes.
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
internal RD_ArrangementPanel *rd_arrangement_panel_from_cfg(RD_Arrangement *arrangement, CFG_ID cfg);
internal RD_ArrangementPanel *rd_arrangement_panel_from_view(RD_Arrangement *arrangement, CFG_ID view);
internal Axis2 rd_arrangement_split_axis(RD_Arrangement *arrangement, RD_ArrangementPanel *panel);
// The rectangle the renderer lays the panel out in, rounded as it rounds.
internal Rng2F32 rd_arrangement_rect(RD_Arrangement *arrangement, Rng2F32 area, RD_ArrangementPanel *panel);
// The first broken invariant, or empty. Operations keep a valid arrangement valid.
internal String8 rd_arrangement_problem(Arena *arena, RD_Arrangement *arrangement);

//- Operations
// Replaces every panel with one empty root panel and returns it, as a layout
// producer starts a document (a reset, a new workspace, a Suggested Layout).
// The root's axis is kept until it splits. Saving releases the old panel
// nodes, with every View no tab took back.
internal RD_PanelID rd_arrangement_clear(RD_Arrangement *arrangement);
// Inserts a sibling beside `panel` when its parent splits along `dir`'s axis,
// rescaling the siblings; otherwise bisects `panel`. Returns the new panel.
internal RD_PanelID rd_arrangement_split(RD_Arrangement *arrangement, RD_PanelID panel, Dir2 dir);
// Adds an empty panel as `parent`'s last child and returns it; with no
// panels, `parent` 0 makes the root first. A parent with tabs first moves
// them, its Selected View and its Presentation State into a new first child
// of weight 1, keeping its own ID and node. With `weight` positive the new
// panel takes it and its siblings keep theirs, as the sidebar's sections do
// until it sizes them; with 0 it takes an equal share, and its siblings
// scale to make room, each counted as at least 0.01.
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
// Moves `view` into `destination` after `prev_view` (0 or not there: first)
// and selects it there. The View may come from another arrangement: saving
// moves its node, and that arrangement no longer has it when next loaded. It
// may also be a new View node in no panel, which saving places. A panel it
// leaves while selected has no Selected View until one is selected.
internal B32 rd_arrangement_move_tab(RD_Arrangement *arrangement, CFG_ID view, RD_PanelID destination, CFG_ID prev_view);
// As move_tab, but `destination` keeps its Selected View unless `select`, as
// restore moves a View that was not selected where it was saved.
internal B32 rd_arrangement_insert_tab(RD_Arrangement *arrangement, CFG_ID view, RD_PanelID destination, CFG_ID prev_view, B32 select);
// Removes `view`'s tab but keeps its View node, for another arrangement to
// take with move_tab: saving leaves the node where it is. A panel it leaves
// while selected has no Selected View until one is selected.
internal B32 rd_arrangement_detach_tab(RD_Arrangement *arrangement, CFG_ID view);
// Drops `view` into `destination` (`dir` invalid) or into the new panel
// splitting `destination` along `dir` makes, after `prev_view` as move_tab
// places it, and settles the panel it left, when this arrangement has it, in
// the same edit (rd_arrangement_settle). A move within one panel settles
// nothing; a split of the source itself never closes it. With no View (0) it
// only splits, which is what measuring a drop needs before one is chosen.
internal RD_ArrangementDrop rd_arrangement_drop(RD_Arrangement *arrangement, CFG_ID view, RD_PanelID destination, Dir2 dir, CFG_ID prev_view, RD_ArrangementTabRule *shown);
// Settles `panel` after a drop took a View out of it. After a move (`dir`
// invalid) it selects its first tab `shown` accepts, and closes it as close
// does when there is none. After a split it selects that tab only when it has
// no Selected View, and closes it only when it has no tabs left. `shown` 0
// accepts every tab; with `may_close` 0 the panel stays open. Returns whether
// anything changed.
internal B32 rd_arrangement_settle(RD_Arrangement *arrangement, RD_PanelID panel, Dir2 dir, RD_ArrangementTabRule *shown, B32 may_close);
// Removes `view`'s tab; saving releases its View node. A panel it leaves
// while selected has no Selected View until one is selected.
internal B32 rd_arrangement_remove_tab(RD_Arrangement *arrangement, CFG_ID view);
// Saving releases `view`, a View node in none of the arrangement's panels: one
// repair found saved outside every arrangement. A tab that takes it first
// keeps it.
internal B32 rd_arrangement_release_orphan(RD_Arrangement *arrangement, CFG_ID view);
// Removes every panel and View. Saving releases the root node it was loaded
// from, as an emptied Floating Panel goes; it saves nothing more until it
// has a panel again.
internal void rd_arrangement_discard(RD_Arrangement *arrangement);
// Makes `view`, one of `panel`'s tabs, its Selected View; 0 selects none.
internal B32 rd_arrangement_select(RD_Arrangement *arrangement, RD_PanelID panel, CFG_ID view);
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

#endif
