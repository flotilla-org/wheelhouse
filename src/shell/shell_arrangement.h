// A Workspace's (or the sidebar's) arrangement as one typed document
// (ADR 0012, "Arrangements are documents"): panels with stable IDs, each
// panel's tabs and Selected View, and relative split weights. Docking
// previews edit a copy; commands edit a loaded document and save it back to
// the config nodes it came from.
//
// Saved shape, until arrangements are committed to Andamento (#309):
//   panels:{id:1 0.6:{id:2 terminal:{selected ...}} 0.4:{id:3 text:{...}}}
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

typedef struct RD_ArrangementTab RD_ArrangementTab;
struct RD_ArrangementTab
{
  RD_ArrangementTab *next;
  RD_ArrangementTab *prev;
  // The View's config node, which saving moves but never recreates (its
  // runtime state, such as a live terminal, is keyed by it). Slot keys
  // replace this in #309.
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
  // Presentation State with them when first saved: the node it moves from.
  CFG_ID options_from;
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
internal RD_Arrangement *rd_arrangement_copy(Arena *arena, RD_Arrangement *src);
// Edits the loaded config nodes in place: panel and View nodes keep their
// identity and their other children, unchanged values are not rewritten, and
// only removed tabs' and closed panels' Views are released.
internal void rd_arrangement_save(CFG_State *state, RD_Arrangement *arrangement);

//- The renderer's panel tree
// The arrangement's panels and tabs as config nodes, with the Presentation
// State saved on each panel's node: the focus mark (`selected`; the last
// marked panel in tree order has focus) and `tabs_on_bottom`. Valid until
// the config changes.
internal CFG_PanelTree rd_panel_tree_from_arrangement(Arena *arena, RD_Arrangement *arrangement);
// Loads `panels_root` and returns its panel tree.
internal CFG_PanelTree rd_panel_tree_from_cfg(Arena *arena, CFG_Node *panels_root);

//- Queries; each returns the nil panel when there is none
internal RD_ArrangementPanel *rd_arrangement_panel_from_id(RD_Arrangement *arrangement, RD_PanelID id);
internal RD_ArrangementPanel *rd_arrangement_panel_from_cfg(RD_Arrangement *arrangement, CFG_ID cfg);
internal RD_ArrangementPanel *rd_arrangement_panel_from_view(RD_Arrangement *arrangement, CFG_ID view);
internal Axis2 rd_arrangement_split_axis(RD_Arrangement *arrangement, RD_ArrangementPanel *panel);
// The rectangle the renderer lays the panel out in, rounded as it rounds.
internal Rng2F32 rd_arrangement_rect(RD_Arrangement *arrangement, Rng2F32 area, RD_ArrangementPanel *panel);
// The first broken invariant, or empty. Operations keep a valid arrangement valid.
internal String8 rd_arrangement_problem(Arena *arena, RD_Arrangement *arrangement);

//- Operations
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
