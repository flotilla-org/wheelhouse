// Headless tests of the arrangement module through its interface, against
// the real configuration parser and the renderer's panel tree.
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
global U32 failures;
global CFG_State *cfg;

// Parses one top-level config tree, as the user file loader does.
internal CFG_Node *
fixture(Arena *arena, char *text)
{
  CFG_SchemaNode *slot = 0;
  CFG_SchemaTable schemas = {&slot, 1};
  CFG_NodePtrList parsed = cfg_node_ptr_list_from_string(arena, cfg, &schemas, str8_zero(), str8_cstring(text));
  for(CFG_NodePtrNode *n = parsed.first; n; n = n->next) { cfg_node_insert_child(cfg, cfg_node_root(), cfg_node_root()->last, n->v); }
  return parsed.first->v;
}

internal CFG_Node *
child(CFG_Node *node, char *name)
{
  return cfg_node_child_from_string(node, str8_cstring(name));
}

// The renderer's reading of the saved tree.
internal CFG_PanelTree
render_tree(Arena *arena, CFG_Node *root)
{
  return rd_panel_tree_from_cfg(arena, root);
}

// A Floating Panel is its own root.
internal CFG_Node *
panels_of(CFG_Node *owner)
{
  if(rd_dock_floating_panel_from_cfg(owner) == owner) { return owner; }
  CFG_Node *root = child(owner, "panels");
  return root != &cfg_nil_node ? root : child(owner, "control_views");
}

// A saved and reloaded arrangement equals the edited one, and the renderer
// lays every panel out where the arrangement says.
internal void
check_saved(Arena *arena, RD_Arrangement *edited, CFG_Node *owner)
{
  Temp temp = temp_begin(arena);
  Check(rd_arrangement_problem(arena, edited).size == 0);
  rd_arrangement_save(cfg, edited);
  CFG_Node *root = panels_of(owner);
  RD_Arrangement *loaded = rd_arrangement_from_cfg(arena, root);
  Check(loaded->root_axis == edited->root_axis && loaded->next_id <= edited->next_id);
  RD_ArrangementPanel *a = edited->root, *b = loaded->root;
  for(; a != &rd_nil_arrangement_panel && b != &rd_nil_arrangement_panel; a = rd_arrangement_next(edited->root, a), b = rd_arrangement_next(loaded->root, b))
  {
    Check(a->id == b->id && a->cfg == b->cfg && a->weight == b->weight && a->child_count == b->child_count);
    Check(a->tab_count == b->tab_count && a->selected == b->selected);
    for(RD_ArrangementTab *x = a->first_tab, *y = b->first_tab; x && y; x = x->next, y = y->next) { Check(x->view == y->view); }
  }
  Check(a == &rd_nil_arrangement_panel && b == &rd_nil_arrangement_panel);
  // Saving what was loaded writes nothing.
  U64 gen = cfg_change_gen();
  rd_arrangement_save(cfg, loaded);
  Check(cfg_change_gen() == gen);
  CFG_PanelTree tree = render_tree(arena, root);
  Rng2F32 areas[] = {r2f32p(0, 0, 1000, 700), r2f32p(3.5f, 7.25f, 1283.75f, 911.5f)};
  for(RD_ArrangementPanel *p = edited->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(edited->root, p))
  {
    CFG_PanelNode *node = cfg_panel_node_from_tree_cfg(tree.root, cfg_node_from_id(p->cfg));
    Check(node != &cfg_nil_panel_node && node->split_axis == rd_arrangement_split_axis(edited, p) && node->pct_of_parent == p->weight);
    Check(node->tabs.count == p->tab_count && node->selected_tab->id == p->selected);
    for(U64 i = 0; i < ArrayCount(areas); i++)
    {
      Rng2F32 x = rd_arrangement_rect(edited, areas[i], p), y = cfg_target_rect_from_panel_node(areas[i], tree.root, node);
      Check(MemoryMatchStruct(&x, &y));
    }
  }
  temp_end(temp);
}

internal U64
next_random(U64 *seed)
{
  *seed = *seed*6364136223846793005ull + 1442695040888963407ull;
  return *seed >> 33;
}

internal RD_ArrangementPanel *
random_panel(RD_Arrangement *arrangement, U64 *seed, B32 leaf)
{
  U64 count = 0;
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  { count += !leaf || p->first == &rd_nil_arrangement_panel; }
  U64 pick = next_random(seed) % count;
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    if((!leaf || p->first == &rd_nil_arrangement_panel) && pick-- == 0) { return p; }
  }
  return &rd_nil_arrangement_panel;
}

// The caller's visibility rule for drops: one View is filtered out.
global CFG_ID hidden_view;
internal B32
shown_tab(CFG_ID view)
{
  return view != hidden_view;
}

internal void
entry_point(CmdLine *cmdline)
{
  Arena *arena = arena_alloc();
  cfg = cfg_state_alloc();
  cfg_ctx_select(cfg_state_ctx(cfg));

  // Today's shapes load as the renderer reads them, and saving them changes
  // nothing but adding each panel's ID; Views and panels keep their nodes.
  {
    CFG_Node *window = fixture(arena, "window:{split_x workspace:{split_x panels:{selected 0.72:{text:{selected} tabs_on_bottom} "
      "0.28:{0.5:{terminal:{label:{`Build`} command:{`make`}} text:{selected expression:{`query:output`}}} 0.5:{jackstay}}}} "
      "control_views:{0.6:{workspace_selector} 0.4:{sidebar_section:{section:{attention}}}}}");
    CFG_Node *workspace = child(window, "workspace"), *panels = child(workspace, "panels");
    CFG_Node *right = panels->last, *terminal = right->first->first;
    RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, panels);
    Check(rd_arrangement_problem(arena, arrangement).size == 0);
    Check(arrangement->root_axis == Axis2_X && arrangement->root->child_count == 2);
    RD_ArrangementPanel *nested = rd_arrangement_panel_from_view(arrangement, terminal->id);
    Check(nested->cfg == right->first->id && nested->tab_count == 2 && nested->selected == right->first->last->id);
    Check(rd_arrangement_split_axis(arrangement, nested) == Axis2_X && rd_arrangement_split_axis(arrangement, nested->parent) == Axis2_Y);
    // IDs follow tree order when none are saved.
    U64 expected_id = 1;
    for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
    { Check(p->id == expected_id++); }
    CFG_ID terminal_id = terminal->id, right_id = right->id;
    check_saved(arena, arrangement, workspace);
    Check(cfg_node_from_id(terminal_id) == terminal && terminal->parent == right->first && cfg_node_from_id(right_id) == right);
    Check(str8_match(child(terminal, "label")->first->string, str8_lit("Build"), 0));
    Check(str8_match(right->string, str8_lit("0.28"), 0) && child(panels->first->next, "tabs_on_bottom") != &cfg_nil_node);
    Check(child(panels, "selected") != &cfg_nil_node && str8_match(child(right, "id")->first->string, str8_lit("3"), 0));
    // A split's `id` and focus mark are not panels to the renderer.
    CFG_PanelTree tree = render_tree(arena, panels);
    Check(tree.root->child_count == 2 && tree.focused == tree.root && cfg_panel_node_from_tree_cfg(tree.root, child(right, "id")) == &cfg_nil_panel_node);
    // It reads the Presentation State saved beside each panel: the tab bar's
    // side, and focus on the last marked panel in tree order.
    Check(tree.root->first->tab_side == Side_Max && tree.root->last->tab_side == Side_Min);
    CFG_Node *mark = cfg_node_new(cfg, right->first, str8_lit("selected"));
    Check(render_tree(arena, panels).focused->cfg == right->first);
    cfg_node_release(cfg, mark);
    // The sidebar's arrangement saves under its own keys.
    CFG_Node *sidebar = child(window, "control_views");
    RD_Arrangement *side = rd_arrangement_from_cfg(arena, sidebar);
    Check(side->root_axis == Axis2_Y && str8_match(side->axis_key, str8_lit("control_views_split_x"), 0));
    rd_arrangement_split(side, side->root->first->id, Dir2_Right);
    check_saved(arena, side, window);
    Check(child(window, "control_views_split_x") == &cfg_nil_node && child(window, "split_x") != &cfg_nil_node);
    cfg_node_release(cfg, window);
  }

  // Saved IDs survive; duplicated or missing ones are replaced after the
  // largest, and new panels continue from there.
  {
    CFG_Node *window = fixture(arena, "window:{panels:{id:{7} 0.5:{id:{9} text} 0.25:{id:{9} text} 0.25:{text}}}");
    RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    RD_ArrangementPanel *root = arrangement->root;
    Check(root->id == 7 && root->first->id == 9 && root->first->next->id == 10 && root->last->id == 11);
    Check(rd_arrangement_split(arrangement, root->last->id, Dir2_Right) == 12);
    check_saved(arena, arrangement, window);
    RD_Arrangement *reloaded = rd_arrangement_from_cfg(arena, child(window, "panels"));
    Check(reloaded->root->first->next->id == 10 && reloaded->next_id == 14);
    cfg_node_release(cfg, window);
  }

  // Splitting inserts beside a panel along its parent's axis, at %f precision,
  // or bisects it across; closing reverses both.
  {
    CFG_Node *window = fixture(arena, "window:{split_x panels:{0.5:{text} 0.5:{terminal}}}");
    CFG_Node *left = child(window, "panels")->first;
    CFG_ID left_cfg = left->id;
    RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    RD_PanelID left_id = arrangement->root->first->id;
    RD_PanelID middle = rd_arrangement_split(arrangement, left_id, Dir2_Right);
    RD_ArrangementPanel *root = arrangement->root;
    Check(root->child_count == 3 && root->first->next->id == middle);
    Check(root->first->weight == 0.333333f && root->first->next->weight == 0.333333f && root->last->weight == 0.333333f);
    RD_PanelID below = rd_arrangement_split(arrangement, middle, Dir2_Down);
    RD_ArrangementPanel *column = rd_arrangement_panel_from_id(arrangement, below)->parent;
    Check(column->weight == 0.333333f && column->first->id == middle && column->first->weight == 0.5f);
    check_saved(arena, arrangement, window);
    // The saved panel keeps its node; the new split gets one.
    Check(cfg_node_from_id(rd_arrangement_panel_from_id(arrangement, left_id)->cfg) == left);
    // Closing one of two collapses the split into the survivor.
    Check(rd_arrangement_close(arrangement, below) == middle);
    Check(rd_arrangement_panel_from_id(arrangement, middle)->parent == arrangement->root);
    Check(rd_arrangement_panel_from_id(arrangement, middle)->weight == 0.333333f);
    check_saved(arena, arrangement, window);
    // Closing one of three gives its share to the others; focus goes before it.
    Check(rd_arrangement_close(arrangement, middle) == left_id);
    Check(abs_f32(arrangement->root->first->weight-0.5f) < 0.00001f && arrangement->root->first->weight == arrangement->root->last->weight);
    check_saved(arena, arrangement, window);
    // The root's survivor becomes the root, and the root's axis is its own.
    RD_PanelID right = arrangement->root->last->id;
    rd_arrangement_split(arrangement, right, Dir2_Down);
    Check(rd_arrangement_close(arrangement, left_id) == right);
    Check(arrangement->root_axis == Axis2_Y && arrangement->root->child_count == 2 && arrangement->root->weight == 1.f);
    check_saved(arena, arrangement, window);
    Check(child(window, "split_x") == &cfg_nil_node && cfg_node_from_id(left_cfg) == &cfg_nil_node);
    // A split left inside a same-axis parent merges into it, scaled.
    rd_arrangement_close(arrangement, arrangement->root->first->id);
    Check(arrangement->root->first == &rd_nil_arrangement_panel && rd_arrangement_close(arrangement, arrangement->root->id) == 0);
    cfg_node_release(cfg, window);
    window = fixture(arena, "window:{split_x panels:{0.4:{text} 0.6:{0.5:{0.3:{text} 0.7:{text}} 0.5:{text}}}}");
    arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    RD_ArrangementPanel *inner = arrangement->root->last->first;
    RD_PanelID first = inner->first->id;
    Check(rd_arrangement_close(arrangement, arrangement->root->last->last->id) == first);
    Check(arrangement->root->child_count == 3 && arrangement->root->first->next->id == first);
    Check(arrangement->root->first->next->weight == 0.18f && arrangement->root->last->weight == 0.42f);
    check_saved(arena, arrangement, window);
    cfg_node_release(cfg, window);
  }

  // Hand-edited weights are kept exactly, even moved into a new split, and a
  // panel that held its whole parent leaves its siblings equal shares.
  {
    CFG_Node *window = fixture(arena, "window:{split_x panels:{0.3333333333:{text} 1.1:{terminal} 0:{text}}}");
    CFG_Node *first = child(window, "panels")->first;
    RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    Check(rd_arrangement_problem(arena, arrangement).size != 0);
    F32 kept = arrangement->root->first->weight;
    RD_PanelID down = rd_arrangement_split(arrangement, arrangement->root->first->id, Dir2_Down);
    rd_arrangement_save(cfg, arrangement);
    Check(f64_from_str8(first->string) == 0.5);
    Check((F32)f64_from_str8(first->parent->string) == kept && rd_arrangement_panel_from_id(arrangement, down)->parent->weight == kept);
    Check(str8_match(child(window, "panels")->first->next->string, str8_lit("1.1"), 0));
    rd_arrangement_close(arrangement, arrangement->root->first->next->id);
    Check(arrangement->root->first->weight == 0.5f && arrangement->root->last->weight == 0.5f);
    check_saved(arena, arrangement, window);
    cfg_node_release(cfg, window);
  }

  // Moving a tab keeps its View node and settings, selects it where it lands
  // and leaves its old panel without a selection; the saved marks agree.
  {
    CFG_Node *window = fixture(arena, "window:{split_x workspace:{panels:{0.5:{text:{selected label:{`A`}} terminal} 0.5:{jackstay:{selected}}}} "
      "control_views:{sessions:{selected}}}");
    CFG_Node *workspace = child(window, "workspace"), *panels = child(workspace, "panels");
    CFG_Node *left_node = panels->first, *right_node = panels->last;
    CFG_Node *a = left_node->first, *terminal = a->next, *jackstay = right_node->first;
    CFG_ID terminal_id = terminal->id;
    RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, panels);
    RD_PanelID left = arrangement->root->first->id, right = arrangement->root->last->id;
    Check(rd_arrangement_move_tab(arrangement, a->id, right, jackstay->id));
    Check(!rd_arrangement_move_tab(arrangement, a->id, right, a->id) && !rd_arrangement_move_tab(arrangement, a->id, 99, 0));
    Check(rd_arrangement_panel_from_id(arrangement, left)->selected == 0 && rd_arrangement_panel_from_id(arrangement, right)->selected == a->id);
    check_saved(arena, arrangement, workspace);
    Check(a->parent == right_node && a->prev == jackstay && child(a, "selected") != &cfg_nil_node);
    Check(str8_match(child(a, "label")->first->string, str8_lit("A"), 0));
    // First place, and a reorder within a panel.
    Check(rd_arrangement_move_tab(arrangement, a->id, right, 0) && rd_arrangement_select(arrangement, right, jackstay->id));
    check_saved(arena, arrangement, workspace);
    Check(right_node->first == a && a->next == jackstay);
    // A View from the sidebar's arrangement leaves it when this one saves.
    CFG_Node *sessions = child(window, "control_views")->first;
    Check(rd_arrangement_move_tab(arrangement, sessions->id, left, terminal->id));
    check_saved(arena, arrangement, workspace);
    Check(sessions->parent == left_node && terminal->next == sessions);
    Check(rd_arrangement_from_cfg(arena, child(window, "control_views"))->root->tab_count == 0);
    // Closing a panel releases its Views; a copy is edited independently.
    RD_Arrangement *copy = rd_arrangement_copy(arena, arrangement);
    rd_arrangement_close(copy, left);
    Check(rd_arrangement_panel_from_id(arrangement, left)->tab_count == 2 && rd_arrangement_panel_from_id(copy, left) == &rd_nil_arrangement_panel);
    check_saved(arena, copy, workspace);
    Check(cfg_node_from_id(terminal_id) == &cfg_nil_node && a->parent == child(workspace, "panels"));
    cfg_node_release(cfg, window);
  }

  // Tab commands: a new View node is placed where its tab goes, a removed
  // tab's View is released on save, and only the Selected View keeps a mark.
  {
    CFG_Node *window = fixture(arena, "window:{split_x panels:{0.5:{text:{selected label:{`A`}} terminal tabs_on_bottom} 0.5:{jackstay}}}");
    CFG_Node *panels = child(window, "panels"), *left_node = panels->first;
    CFG_Node *a = left_node->first, *terminal = a->next;
    RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, panels);
    RD_PanelID left = arrangement->root->first->id, right = arrangement->root->last->id;
    // A duplicate brings its source's mark; the copy is selected, the source not.
    CFG_Node *copy = cfg_node_deep_copy(cfg, a);
    Check(copy->parent == &cfg_nil_node && child(copy, "selected") != &cfg_nil_node);
    Check(rd_arrangement_move_tab(arrangement, copy->id, left, a->id));
    check_saved(arena, arrangement, window);
    Check(copy->parent == left_node && a->next == copy && copy->next == terminal);
    Check(child(a, "selected") == &cfg_nil_node && child(copy, "selected") != &cfg_nil_node);
    Check(str8_match(child(copy, "label")->first->string, str8_lit("A"), 0));
    // A new View goes after the last tab, before the panel's options.
    CFG_Node *built = cfg_node_new(cfg, &cfg_nil_node, str8_lit("text"));
    Check(rd_arrangement_move_tab(arrangement, built->id, left, terminal->id));
    check_saved(arena, arrangement, window);
    Check(terminal->next == built && child(left_node, "tabs_on_bottom")->prev == built);
    // Selecting is within a panel; 0 selects none, and an unchanged
    // selection writes nothing.
    Check(!rd_arrangement_select(arrangement, right, a->id) && !rd_arrangement_select(arrangement, 99, 0));
    Check(rd_arrangement_select(arrangement, left, a->id));
    check_saved(arena, arrangement, window);
    Check(child(a, "selected") != &cfg_nil_node && child(built, "selected") == &cfg_nil_node);
    U64 gen = cfg_change_gen();
    Check(rd_arrangement_select(arrangement, left, a->id));
    rd_arrangement_save(cfg, arrangement);
    Check(cfg_change_gen() == gen);
    Check(rd_arrangement_select(arrangement, left, 0));
    check_saved(arena, arrangement, window);
    Check(child(a, "selected") == &cfg_nil_node);
    // Removing a tab releases its View on save, not before; a copy made
    // meanwhile releases it too. A tab taken back keeps its View.
    CFG_ID terminal_id = terminal->id, built_id = built->id;
    Check(rd_arrangement_select(arrangement, left, terminal_id));
    Check(rd_arrangement_remove_tab(arrangement, terminal_id) && !rd_arrangement_remove_tab(arrangement, terminal_id));
    Check(rd_arrangement_panel_from_id(arrangement, left)->selected == 0 && rd_arrangement_panel_from_id(arrangement, left)->tab_count == 3);
    Check(cfg_node_from_id(terminal_id) == terminal);
    Check(rd_arrangement_remove_tab(arrangement, built_id) && rd_arrangement_move_tab(arrangement, built_id, right, 0));
    RD_Arrangement *preview = rd_arrangement_copy(arena, arrangement);
    check_saved(arena, arrangement, window);
    Check(cfg_node_from_id(terminal_id) == &cfg_nil_node && built->parent->id == rd_arrangement_panel_from_id(arrangement, right)->cfg);
    Check(arrangement->first_removed == 0 && preview->first_removed != 0);
    // Reordering moves a panel and its weight among its siblings.
    rd_arrangement_split(arrangement, right, Dir2_Right);
    RD_ArrangementPanel *first = arrangement->root->first, *last = arrangement->root->last;
    first->weight = 0.2f; first->next->weight = 0.3f; last->weight = 0.5f;
    Check(rd_arrangement_reorder(arrangement, first->id, last->id));
    Check(arrangement->root->first->id == right && arrangement->root->last->id == left && arrangement->root->last->weight == 0.2f);
    check_saved(arena, arrangement, window);
    Check(rd_arrangement_next_child_cfg(left_node->next, RD_ArrangementChild_Panel) == &cfg_nil_node);
    Check(str8_match(left_node->string, str8_lit("0.200000"), 0));
    Check(rd_arrangement_reorder(arrangement, left, 0) && arrangement->root->first->id == left);
    Check(!rd_arrangement_reorder(arrangement, left, left) && !rd_arrangement_reorder(arrangement, arrangement->root->id, 0));
    rd_arrangement_split(arrangement, left, Dir2_Down);
    Check(!rd_arrangement_reorder(arrangement, left, right));
    check_saved(arena, arrangement, window);
    cfg_node_release(cfg, window);
  }

  // A drop settles the panel its View left in the same edit, by the caller's
  // rule: a move closes a source with no shown tab left, or selects its first
  // shown tab; a split closes it only with no tabs left, and selects only when
  // it has no Selected View.
  {
    char *text = "window:{split_x panels:{0.3:{text:{selected} terminal binary} 0.3:{jackstay} 0.4:{sessions}}}";
    CFG_Node *window = fixture(arena, text);
    CFG_Node *source_node = child(window, "panels")->first;
    CFG_Node *a = source_node->first, *hidden = a->next, *b = hidden->next;
    CFG_ID hidden_id = hidden->id;
    hidden_view = hidden->id;
    RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    RD_PanelID source = arrangement->root->first->id, middle = arrangement->root->first->next->id, last = arrangement->root->last->id;
    // Moving an unselected tab still selects the first shown one; the hidden
    // tab is passed over.
    Check(rd_arrangement_select(arrangement, source, b->id));
    RD_ArrangementDrop drop = rd_arrangement_drop(arrangement, b->id, last, Dir2_Invalid, 0, shown_tab);
    Check(drop.panel == last && drop.source == source && drop.heir == 0);
    Check(rd_arrangement_panel_from_id(arrangement, source)->selected == a->id);
    check_saved(arena, arrangement, window);
    // Moving the last shown tab closes the source, releasing the hidden View;
    // its next sibling is the heir.
    drop = rd_arrangement_drop(arrangement, a->id, middle, Dir2_Invalid, 0, shown_tab);
    Check(drop.panel == middle && drop.source == source && drop.heir == middle);
    Check(rd_arrangement_panel_from_id(arrangement, source) == &rd_nil_arrangement_panel);
    Check(rd_arrangement_panel_from_id(arrangement, middle)->selected == a->id);
    check_saved(arena, arrangement, window);
    Check(cfg_node_from_id(hidden_id) == &cfg_nil_node && a->parent->id == rd_arrangement_panel_from_id(arrangement, middle)->cfg);
    // Moving within a panel settles nothing.
    drop = rd_arrangement_drop(arrangement, a->id, middle, Dir2_Invalid, 0, shown_tab);
    Check(drop.panel == middle && drop.heir == 0 && arrangement->root->child_count == 2);
    cfg_node_release(cfg, window);

    // A split keeps a source with only a hidden tab, without selecting it.
    window = fixture(arena, text);
    source_node = child(window, "panels")->first;
    a = source_node->first; hidden = a->next; b = hidden->next;
    hidden_view = hidden->id;
    arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    source = arrangement->root->first->id; middle = arrangement->root->first->next->id;
    Check(rd_arrangement_select(arrangement, source, b->id));
    drop = rd_arrangement_drop(arrangement, a->id, middle, Dir2_Down, 0, shown_tab);
    Check(drop.panel != 0 && drop.source == source && drop.heir == 0);
    Check(rd_arrangement_panel_from_id(arrangement, source)->selected == b->id);
    Check(rd_arrangement_panel_from_id(arrangement, drop.panel)->selected == a->id);
    rd_arrangement_drop(arrangement, b->id, middle, Dir2_Up, 0, shown_tab);
    Check(rd_arrangement_panel_from_id(arrangement, source)->selected == 0 && rd_arrangement_panel_from_id(arrangement, source)->tab_count == 1);
    check_saved(arena, arrangement, window);
    // Splitting its last tab off a panel keeps that panel, now empty.
    RD_PanelID landed = rd_arrangement_panel_from_view(arrangement, b->id)->id;
    drop = rd_arrangement_drop(arrangement, b->id, landed, Dir2_Right, 0, shown_tab);
    Check(drop.source == landed && drop.heir == 0 && rd_arrangement_panel_from_id(arrangement, landed)->tab_count == 0);
    // A split takes an emptied source away and names its heir.
    RD_PanelID emptied = rd_arrangement_panel_from_view(arrangement, b->id)->id;
    drop = rd_arrangement_drop(arrangement, b->id, source, Dir2_Left, 0, shown_tab);
    Check(drop.source == emptied && drop.heir != 0 && rd_arrangement_panel_from_id(arrangement, emptied) == &rd_nil_arrangement_panel);
    Check(rd_arrangement_panel_from_id(arrangement, landed)->tab_count == 0);
    check_saved(arena, arrangement, window);
    // No View only splits; settling without closing leaves an emptied panel.
    U64 panels_before = 0;
    for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p)) { panels_before += 1; }
    drop = rd_arrangement_drop(arrangement, 0, source, Dir2_Down, 0, shown_tab);
    Check(drop.panel != 0 && drop.source == 0 && rd_arrangement_panel_from_id(arrangement, drop.panel)->tab_count == 0);
    Check(!rd_arrangement_settle(arrangement, drop.panel, Dir2_Invalid, shown_tab, 0));
    Check(rd_arrangement_settle(arrangement, drop.panel, Dir2_Invalid, shown_tab, 1));
    Check(rd_arrangement_panel_from_id(arrangement, drop.panel) == &rd_nil_arrangement_panel);
    U64 panels_after = 0;
    for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p)) { panels_after += 1; }
    Check(panels_after == panels_before);
    check_saved(arena, arrangement, window);
    cfg_node_release(cfg, window);
    hidden_view = 0;
  }

  // Resizing moves one boundary, holding both sides to the floor where they
  // can, after putting drifted siblings right.
  {
    CFG_Node *window = fixture(arena, "window:{split_x panels:{0.2:{text} 0.3:{text} 0.5:{text}}}");
    RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    RD_ArrangementPanel *a = arrangement->root->first, *b = a->next;
    rd_arrangement_resize(arrangement, a->id, 0.25f, 0.05f);
    Check(a->weight == 0.45f && b->weight == 0.05f);
    rd_arrangement_resize(arrangement, a->id, -0.6f, 0.05f);
    Check(a->weight == 0.05f && b->weight == 0.45f);
    rd_arrangement_resize(arrangement, arrangement->root->last->id, 0.1f, 0.05f);
    check_saved(arena, arrangement, window);
    cfg_node_release(cfg, window);
    window = fixture(arena, "window:{panels:{0.4:{text} 0.4:{text} 0.4:{text}}}");
    arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    rd_arrangement_resize(arrangement, arrangement->root->first->id, 0, 0.05f);
    Check(rd_arrangement_problem(arena, arrangement).size == 0 && arrangement->root->first->weight == 0.333333f);
    cfg_node_release(cfg, window);
    // Equalizing splits the two sides' space evenly, after the same rescale.
    window = fixture(arena, "window:{split_x panels:{0.2:{text} 0.3:{text} 0.5:{text}}}");
    arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    rd_arrangement_equalize(arrangement, arrangement->root->first->next->id);
    Check(arrangement->root->first->weight == 0.2f && arrangement->root->first->next->weight == 0.4f &&
          arrangement->root->last->weight == 0.4f);
    check_saved(arena, arrangement, window);
    cfg_node_release(cfg, window);
    window = fixture(arena, "window:{panels:{0.6:{text} 0.6:{text}}}");
    arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    rd_arrangement_equalize(arrangement, arrangement->root->first->id);
    Check(arrangement->root->first->weight == 0.5f && arrangement->root->last->weight == 0.5f);
    rd_arrangement_equalize(arrangement, arrangement->root->last->id);
    Check(arrangement->root->first->weight == 0.5f && rd_arrangement_problem(arena, arrangement).size == 0);
    cfg_node_release(cfg, window);
  }

  // Each Floating Panel is its own arrangement, rooted at its node in the
  // host. Saving one keeps its node's name, and leaves the host's other
  // panels and the owners' axes alone. A View moves between two as a move
  // into one and a settle of the other, which never closes its root.
  {
    CFG_Node *window = fixture(arena, "window:{split_x workspace:{split_x panels:{0.5:{text} 0.5:{terminal}} "
      "floating_panels:{1:{text:{selected} terminal:{label:{`Log`}}} 0.5:{jackstay:{selected} text tabs_on_bottom}}}}");
    CFG_Node *workspace = child(window, "workspace"), *host = child(workspace, "floating_panels");
    CFG_Node *first = host->first, *second = host->last;
    CFG_Node *log = first->last, *jackstay = second->first, *text = jackstay->next;
    CFG_ID text_id = text->id;
    Check(rd_dock_floating_panel_from_cfg(log) == first && rd_dock_floating_panel_from_cfg(second) == second);
    Check(rd_dock_floating_panel_from_cfg(host) == &cfg_nil_node);
    Check(rd_dock_floating_panel_from_cfg(child(workspace, "panels")->first->first) == &cfg_nil_node);
    // A View saved directly in the host is in no Floating Panel.
    Check(rd_dock_floating_panel_from_cfg(cfg_node_new(cfg, host, str8_lit("text"))) == &cfg_nil_node);
    cfg_node_release(cfg, host->last);
    RD_Arrangement *into = rd_arrangement_from_cfg(arena, first);
    Check(rd_arrangement_problem(arena, into).size == 0 && into->root->cfg == first->id);
    Check(into->root->first == &rd_nil_arrangement_panel && into->root->tab_count == 2 && into->root->selected == first->first->id);
    Check(rd_arrangement_panel_from_view(into, jackstay->id) == &rd_nil_arrangement_panel);
    Check(rd_arrangement_move_tab(into, jackstay->id, into->root->id, log->id));
    check_saved(arena, into, first);
    Check(jackstay->parent == first && log->next == jackstay && str8_match(first->string, str8_lit("1"), 0));
    Check(child(jackstay, "selected") != &cfg_nil_node && child(first->first, "selected") == &cfg_nil_node);
    Check(str8_match(child(first, "id")->first->string, str8_lit("1"), 0));
    Check(child(second, "id") == &cfg_nil_node && str8_match(second->string, str8_lit("0.5"), 0));
    Check(child(host, "split_x") == &cfg_nil_node && child(workspace, "split_x") != &cfg_nil_node);
    RD_Arrangement *from = rd_arrangement_from_cfg(arena, second);
    Check(from->root->tab_count == 1 && from->root->selected == 0);
    Check(rd_arrangement_settle(from, from->root->id, Dir2_Invalid, 0, 1) && from->root->selected == text_id);
    check_saved(arena, from, second);
    Check(child(text, "selected") != &cfg_nil_node && host->last == second && str8_match(second->string, str8_lit("0.5"), 0));
    Check(child(second, "tabs_on_bottom") != &cfg_nil_node);
    // Emptied, it stays.
    Check(rd_arrangement_remove_tab(from, text_id) && !rd_arrangement_settle(from, from->root->id, Dir2_Invalid, 0, 1));
    check_saved(arena, from, second);
    Check(cfg_node_from_id(text_id) == &cfg_nil_node && second->parent == host && from->root->tab_count == 0);
    cfg_node_release(cfg, window);
  }

  // A hand-edited deep split chain loads, copies and saves without exhausting
  // the C stack, and a copy is independent of its source.
  {
    Temp deep_temp = temp_begin(arena);
    enum { depth = 250000 };
    CFG_Node *window = fixture(arena, "window:{panels:{}}");
    CFG_Node *node = child(window, "panels");
    for(U32 i = 1; i < depth; i++) { node = cfg_node_newf(cfg, node, "%f", (F32)(i%97)/97); }
    cfg_node_new(cfg, node, str8_lit("text"));
    RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
    RD_Arrangement *copy = rd_arrangement_copy(arena, arrangement);
    RD_ArrangementPanel *a = arrangement->root, *b = copy->root;
    for(U32 i = 0; i < depth; i++)
    {
      Check(b != &rd_nil_arrangement_panel && b != a && b->id == a->id && b->cfg == a->cfg && b->weight == a->weight);
      Check(b->child_count == a->child_count && (b->parent == &rd_nil_arrangement_panel) == (i == 0));
      a = a->first; b = b->first;
    }
    Check(a == &rd_nil_arrangement_panel && b == &rd_nil_arrangement_panel);
    rd_arrangement_save(cfg, copy);
    Check(str8_match(child(child(window, "panels")->first, "id")->first->string, str8_lit("2"), 0));
    cfg_node_release(cfg, window);
    temp_end(deep_temp);
  }

  // Random edits keep a valid arrangement valid, and what is saved lays out
  // as the arrangement does.
  for(U64 seed = 1; seed <= 40; seed++)
  {
    U64 state = seed;
    CFG_Node *window = fixture(arena, seed & 1 ? "window:{split_x panels:{text terminal jackstay sessions binary}}" :
      "window:{panels:{0.25:{text terminal} 0.75:{0.5:{jackstay} 0.5:{sessions binary}}}}");
    for(U64 step = 0; step < 12; step++)
    {
      Temp temp = temp_begin(arena);
      RD_Arrangement *arrangement = rd_arrangement_from_cfg(arena, child(window, "panels"));
      U64 op = next_random(&state) % 5;
      RD_ArrangementPanel *panel = random_panel(arrangement, &state, 1);
      if(op == 0 || op == 1) { rd_arrangement_split(arrangement, panel->id, (Dir2)(next_random(&state) % 4)); }
      if(op == 2 && arrangement->root->first != &rd_nil_arrangement_panel) { rd_arrangement_close(arrangement, panel->id); }
      if(op == 3 || op == 0)
      {
        RD_ArrangementPanel *source = random_panel(arrangement, &state, 1);
        if(source->first_tab) { rd_arrangement_move_tab(arrangement, source->first_tab->view, panel->id, panel->last_tab ? panel->last_tab->view : 0); }
      }
      if(op == 4)
      {
        if(panel->first_tab && next_random(&state) % 2) { rd_arrangement_remove_tab(arrangement, panel->first_tab->view); }
        else { rd_arrangement_reorder(arrangement, panel->id, panel->parent->last->id); }
      }
      if(op == 1 && panel->next != &rd_nil_arrangement_panel)
      { rd_arrangement_resize(arrangement, panel->id, (F32)(next_random(&state) % 100)/100.f - .5f, .05f); }
      check_saved(arena, arrangement, window);
      temp_end(temp);
    }
    cfg_node_release(cfg, window);
  }

  cfg_state_release(cfg);
  arena_release(arena);
  fprintf(stderr, "Arrangement: %u failures\n", failures);
  exit(failures != 0);
}
