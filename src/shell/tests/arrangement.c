// Headless tests of the arrangement module through its interface, and of
// the renderer's panel tree read from it. Fixtures are described in the
// config syntax (a panel is a weight, anything else a tab) and built with
// the module's own operations.
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
global CFG_State *cfg;

// The shell's Workspace and Window IDs aren't in these tests: every panel
// and View is addressed as one zero-ID owner's.
read_only global RD_PanelOwner test_owner = {RD_PanelOwnerKind_Workspace};

// The shell keeps panels' Presentation State beside their arrangement
// (rd_panel_presentation_hook); these tests keep one panel's focus and one's
// tab bar side.
global RD_PanelID test_focused, test_tabs_on_bottom;

internal RD_PanelPresentation
test_presentation(RD_PanelAddress panel)
{
  RD_PanelPresentation result = {panel.id == test_focused, panel.id == test_tabs_on_bottom};
  return result;
}

// Each fixture tab is a View of its own: its kind and a count.
global U64 view_count;

internal RD_ViewAddress
new_view(char *kind)
{
  Temp scratch = scratch_begin(0, 0);
  RD_ViewAddress result = rd_view_address_make(RD_ViewOwnerKind_Workspace, (UIShell_WorkspaceId){0},
                                               push_str8f(scratch.arena, "%s#%I64u", kind, ++view_count), RD_ViewInstance_Current);
  scratch_end(scratch);
  return result;
}

internal B32
is_weight(String8 string)
{
  return string.size && (char_is_digit(string.str[0], 10) || string.str[0] == '.');
}

internal void
build_panel(RD_Arrangement *arrangement, RD_PanelID id, CFG_Node *node)
{
  for(CFG_Node *c = node->first; c != &cfg_nil_node; c = c->next)
  {
    RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(arrangement, id);
    if(is_weight(c->string))
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

// An arrangement as `text` describes it: `split_x` makes the root split
// along x, and `panels` holds the root.
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
    if(str8_match(n->v->string, str8_lit("split_x"), 0)) { arrangement->root_axis = Axis2_X; }
    if(str8_match(n->v->string, str8_lit("panels"), 0)) { build_panel(arrangement, rd_arrangement_clear(arrangement), n->v); }
  }
  for(CFG_NodePtrNode *n = parsed.first; n; n = n->next) { cfg_node_release(cfg, n->v); }
  scratch_end(scratch);
  return arrangement;
}

// The tab of `panel` at `index`.
internal RD_ViewAddress
tab_at(RD_ArrangementPanel *panel, U64 index)
{
  RD_ArrangementTab *t = panel->first_tab;
  for(U64 i = 0; t && i < index; i++) { t = t->next; }
  return t ? t->view : rd_nil_view_address;
}

internal B32
removed(RD_Arrangement *arrangement, RD_ViewAddress view)
{
  for(RD_ArrangementTab *t = arrangement->first_removed; t; t = t->next) { if(rd_view_address_match(t->view, view)) { return 1; } }
  return 0;
}

// A valid arrangement, its copy one document with it, and the renderer lays
// every panel out where the arrangement says.
internal void
check_layout(Arena *arena, RD_Arrangement *arrangement)
{
  Temp temp = temp_begin(arena);
  Check(rd_arrangement_problem(arena, arrangement).size == 0);
  RD_Arrangement *copy = rd_arrangement_copy(arena, arrangement);
  Check(rd_arrangement_match(arrangement, copy) && rd_arrangement_problem(arena, copy).size == 0);
  RD_PanelTree tree = rd_panel_tree_from_arrangement(arena, test_owner, arrangement);
  Rng2F32 areas[] = {r2f32p(0, 0, 1000, 700), r2f32p(3.5f, 7.25f, 1283.75f, 911.5f)};
  for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
  {
    RD_PanelNode *node = rd_panel_node_from_id(tree.root, p->id);
    Check(node != &rd_nil_panel_node && node->split_axis == rd_arrangement_split_axis(arrangement, p) && node->pct_of_parent == p->weight);
    Check(node->tabs.count == p->tab_count && rd_view_address_match(node->selected_tab, p->selected));
    RD_ViewAddressNode *n = node->tabs.first;
    for(RD_ArrangementTab *t = p->first_tab; t && n; t = t->next, n = n->next) { Check(rd_view_address_match(t->view, n->v)); }
    for(U64 i = 0; i < ArrayCount(areas); i++)
    {
      Rng2F32 x = rd_arrangement_rect(arrangement, areas[i], p), y = rd_target_rect_from_panel_node(areas[i], tree.root, node);
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
global RD_ViewAddress hidden_view;
internal B32
shown_tab(RD_ViewAddress view)
{
  return !rd_view_address_match(view, hidden_view);
}

internal void
entry_point(CmdLine *cmdline)
{
  Arena *arena = arena_alloc();
  cfg = cfg_state_alloc();
  cfg_ctx_select(cfg_state_ctx(cfg));
  rd_panel_presentation_hook = test_presentation;

  // A built arrangement has its panels in tree order, the axes alternating
  // from the root's, and the renderer reads it as it is.
  {
    RD_Arrangement *arrangement = build(arena, "split_x panels:{0.72:{text:{selected}} "
      "0.28:{0.5:{terminal text:{selected}} 0.5:{jackstay}}}");
    Check(rd_arrangement_problem(arena, arrangement).size == 0);
    Check(arrangement->root_axis == Axis2_X && arrangement->root->child_count == 2);
    RD_ArrangementPanel *nested = arrangement->root->last->first;
    Check(nested->tab_count == 2 && rd_view_address_match(nested->selected, tab_at(nested, 1)));
    Check(rd_arrangement_panel_from_view(arrangement, tab_at(nested, 0)) == nested);
    Check(rd_arrangement_split_axis(arrangement, nested) == Axis2_X && rd_arrangement_split_axis(arrangement, nested->parent) == Axis2_Y);
    U64 expected_id = 1;
    for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p))
    { Check(p->id == expected_id++); }
    check_layout(arena, arrangement);
    RD_PanelTree tree = rd_panel_tree_from_arrangement(arena, test_owner, arrangement);
    U64 tree_count = 0;
    for(RD_PanelNode *p = tree.root; p != &rd_nil_panel_node; p = rd_panel_node_rec__depth_first_pre(tree.root, p).next) { tree_count += 1; }
    Check(tree.root->child_count == 2 && tree.focused == &rd_nil_panel_node && tree_count == 5);
    // It reads each panel's Presentation State beside it: the tab bar's
    // side, and focus.
    test_tabs_on_bottom = tree.root->first->address.id;
    test_focused = nested->id;
    tree = rd_panel_tree_from_arrangement(arena, test_owner, arrangement);
    Check(tree.root->first->tab_side == Side_Max && tree.root->last->tab_side == Side_Min);
    Check(tree.focused->address.id == test_focused);
    test_focused = test_tabs_on_bottom = 0;
  }

  // Splitting inserts beside a panel along its parent's axis, at %f precision,
  // or bisects it across; closing reverses both.
  {
    RD_Arrangement *arrangement = build(arena, "split_x panels:{0.5:{text} 0.5:{terminal}}");
    RD_PanelID left_id = arrangement->root->first->id;
    RD_PanelID middle = rd_arrangement_split(arrangement, left_id, Dir2_Right);
    RD_ArrangementPanel *root = arrangement->root;
    Check(root->child_count == 3 && root->first->next->id == middle);
    Check(root->first->weight == 0.333333f && root->first->next->weight == 0.333333f && root->last->weight == 0.333333f);
    RD_PanelID below = rd_arrangement_split(arrangement, middle, Dir2_Down);
    RD_ArrangementPanel *column = rd_arrangement_panel_from_id(arrangement, below)->parent;
    Check(column->weight == 0.333333f && column->first->id == middle && column->first->weight == 0.5f);
    check_layout(arena, arrangement);
    // Closing one of two collapses the split into the survivor.
    Check(rd_arrangement_close(arrangement, below) == middle);
    Check(rd_arrangement_panel_from_id(arrangement, middle)->parent == arrangement->root);
    Check(rd_arrangement_panel_from_id(arrangement, middle)->weight == 0.333333f);
    check_layout(arena, arrangement);
    // Closing one of three gives its share to the others; focus goes before it.
    Check(rd_arrangement_close(arrangement, middle) == left_id);
    Check(abs_f32(arrangement->root->first->weight-0.5f) < 0.00001f && arrangement->root->first->weight == arrangement->root->last->weight);
    check_layout(arena, arrangement);
    // The root's survivor becomes the root, and the root's axis is its own.
    RD_PanelID right = arrangement->root->last->id;
    RD_ViewAddress left_tab = tab_at(arrangement->root->first, 0);
    rd_arrangement_split(arrangement, right, Dir2_Down);
    Check(rd_arrangement_close(arrangement, left_id) == right);
    Check(arrangement->root_axis == Axis2_Y && arrangement->root->child_count == 2 && arrangement->root->weight == 1.f);
    Check(removed(arrangement, left_tab));
    check_layout(arena, arrangement);
    rd_arrangement_close(arrangement, arrangement->root->first->id);
    Check(arrangement->root->first == &rd_nil_arrangement_panel && rd_arrangement_close(arrangement, arrangement->root->id) == 0);
    // A split left inside a same-axis parent merges into it, scaled.
    arrangement = build(arena, "split_x panels:{0.4:{text} 0.6:{0.5:{0.3:{text} 0.7:{text}} 0.5:{text}}}");
    RD_ArrangementPanel *inner = arrangement->root->last->first;
    RD_PanelID first = inner->first->id;
    Check(rd_arrangement_close(arrangement, arrangement->root->last->last->id) == first);
    Check(arrangement->root->child_count == 3 && arrangement->root->first->next->id == first);
    Check(arrangement->root->first->next->weight == 0.18f && arrangement->root->last->weight == 0.42f);
    check_layout(arena, arrangement);
  }

  // Hand-edited weights are kept exactly, even moved into a new split, and a
  // panel that held its whole parent leaves its siblings equal shares.
  {
    RD_Arrangement *arrangement = build(arena, "split_x panels:{0.3333333333:{text} 1.1:{terminal} 0:{text}}");
    Check(rd_arrangement_problem(arena, arrangement).size != 0);
    F32 kept = arrangement->root->first->weight;
    RD_PanelID first = arrangement->root->first->id;
    RD_PanelID down = rd_arrangement_split(arrangement, first, Dir2_Down);
    Check(rd_arrangement_panel_from_id(arrangement, first)->weight == 0.5f);
    Check(rd_arrangement_panel_from_id(arrangement, down)->parent->weight == kept && arrangement->root->first->next->weight == 1.1f);
    rd_arrangement_close(arrangement, arrangement->root->first->next->id);
    Check(arrangement->root->first->weight == 0.5f && arrangement->root->last->weight == 0.5f);
    check_layout(arena, arrangement);
    // A weight to write is %f's; one that %f would change keeps more digits.
    Check(str8_match(rd_arrangement_weight_string(arena, 0.5f), str8_lit("0.500000"), 0));
    Check((F32)f64_from_str8(rd_arrangement_weight_string(arena, kept)) == kept);
  }

  // Moving a tab selects it where it lands and leaves its old panel without
  // a selection; a View from elsewhere joins as any tab does.
  {
    RD_Arrangement *arrangement = build(arena, "split_x panels:{0.5:{text:{selected} terminal} 0.5:{jackstay:{selected}}}");
    RD_PanelID left = arrangement->root->first->id, right = arrangement->root->last->id;
    RD_ViewAddress a = tab_at(arrangement->root->first, 0), terminal = tab_at(arrangement->root->first, 1);
    RD_ViewAddress jackstay = tab_at(arrangement->root->last, 0);
    Check(rd_arrangement_move_tab(arrangement, a, right, jackstay));
    Check(!rd_arrangement_move_tab(arrangement, a, right, a) && !rd_arrangement_move_tab(arrangement, a, 99, rd_nil_view_address));
    Check(rd_view_address_is_nil(rd_arrangement_panel_from_id(arrangement, left)->selected));
    Check(rd_view_address_match(rd_arrangement_panel_from_id(arrangement, right)->selected, a));
    Check(rd_view_address_match(tab_at(rd_arrangement_panel_from_id(arrangement, right), 1), a));
    check_layout(arena, arrangement);
    // First place, and a reorder within a panel.
    Check(rd_arrangement_move_tab(arrangement, a, right, rd_nil_view_address) && rd_arrangement_select(arrangement, right, jackstay));
    Check(rd_view_address_match(tab_at(rd_arrangement_panel_from_id(arrangement, right), 0), a));
    RD_ViewAddress sessions = new_view("sessions");
    Check(rd_arrangement_move_tab(arrangement, sessions, left, terminal));
    Check(rd_view_address_match(tab_at(rd_arrangement_panel_from_id(arrangement, left), 1), sessions));
    check_layout(arena, arrangement);
    // Closing a panel removes its Views; a copy is edited independently.
    RD_Arrangement *copy = rd_arrangement_copy(arena, arrangement);
    rd_arrangement_close(copy, left);
    Check(rd_arrangement_panel_from_id(arrangement, left)->tab_count == 2 && rd_arrangement_panel_from_id(copy, left) == &rd_nil_arrangement_panel);
    Check(removed(copy, terminal) && removed(copy, sessions) && !removed(arrangement, terminal));
    check_layout(arena, copy);
  }

  // Selecting is within a panel; nil selects none. Removing a tab names its
  // View, for setting to release, and a copy made meanwhile keeps the list.
  {
    RD_Arrangement *arrangement = build(arena, "split_x panels:{0.5:{text:{selected} terminal} 0.5:{jackstay}}");
    RD_PanelID left = arrangement->root->first->id, right = arrangement->root->last->id;
    RD_ViewAddress a = tab_at(arrangement->root->first, 0), terminal = tab_at(arrangement->root->first, 1);
    Check(!rd_arrangement_select(arrangement, right, a) && !rd_arrangement_select(arrangement, 99, rd_nil_view_address));
    Check(rd_arrangement_select(arrangement, left, terminal) && rd_view_address_match(arrangement->root->first->selected, terminal));
    Check(rd_arrangement_select(arrangement, left, rd_nil_view_address) && rd_view_address_is_nil(arrangement->root->first->selected));
    Check(rd_arrangement_select(arrangement, left, terminal));
    Check(rd_arrangement_remove_tab(arrangement, terminal) && !rd_arrangement_remove_tab(arrangement, terminal));
    Check(rd_view_address_is_nil(arrangement->root->first->selected) && arrangement->root->first->tab_count == 1);
    Check(removed(arrangement, terminal));
    RD_Arrangement *preview = rd_arrangement_copy(arena, arrangement);
    Check(removed(preview, terminal));
    check_layout(arena, arrangement);
    // Reordering moves a panel and its weight among its siblings.
    rd_arrangement_split(arrangement, right, Dir2_Right);
    RD_ArrangementPanel *first = arrangement->root->first, *last = arrangement->root->last;
    first->weight = 0.2f; first->next->weight = 0.3f; last->weight = 0.5f;
    Check(rd_arrangement_reorder(arrangement, first->id, last->id));
    Check(arrangement->root->first->id == right && arrangement->root->last->id == left && arrangement->root->last->weight == 0.2f);
    check_layout(arena, arrangement);
    Check(rd_arrangement_reorder(arrangement, left, 0) && arrangement->root->first->id == left);
    Check(!rd_arrangement_reorder(arrangement, left, left) && !rd_arrangement_reorder(arrangement, arrangement->root->id, 0));
    rd_arrangement_split(arrangement, left, Dir2_Down);
    Check(!rd_arrangement_reorder(arrangement, left, right));
    check_layout(arena, arrangement);
    // A View's tabs follow it to a new address, and its selection.
    RD_ViewAddress moved = new_view("text");
    Check(rd_arrangement_select(arrangement, left, a));
    Check(rd_arrangement_readdress(arrangement, a, moved) && !rd_arrangement_readdress(arrangement, a, moved));
    Check(rd_arrangement_panel_from_view(arrangement, moved)->id == left && rd_arrangement_panel_from_view(arrangement, a) == &rd_nil_arrangement_panel);
    Check(rd_view_address_match(rd_arrangement_panel_from_id(arrangement, left)->selected, moved));
  }

  // A drop settles the panel its View left in the same edit, by the caller's
  // rule: a move closes a source with no shown tab left, or selects its first
  // shown tab; a split closes it only with no tabs left, and selects only when
  // it has no Selected View.
  {
    char *text = "split_x panels:{0.3:{text:{selected} terminal binary} 0.3:{jackstay} 0.4:{sessions}}";
    RD_Arrangement *arrangement = build(arena, text);
    RD_PanelID source = arrangement->root->first->id, middle = arrangement->root->first->next->id, last = arrangement->root->last->id;
    RD_ViewAddress a = tab_at(arrangement->root->first, 0), hidden = tab_at(arrangement->root->first, 1), b = tab_at(arrangement->root->first, 2);
    hidden_view = hidden;
    // Moving an unselected tab still selects the first shown one; the hidden
    // tab is passed over.
    Check(rd_arrangement_select(arrangement, source, b));
    RD_ArrangementDrop drop = rd_arrangement_drop(arrangement, b, last, Dir2_Invalid, rd_nil_view_address, shown_tab);
    Check(drop.panel == last && drop.source == source && drop.heir == 0);
    Check(rd_view_address_match(rd_arrangement_panel_from_id(arrangement, source)->selected, a));
    check_layout(arena, arrangement);
    // Moving the last shown tab closes the source, removing the hidden View;
    // its next sibling is the heir.
    drop = rd_arrangement_drop(arrangement, a, middle, Dir2_Invalid, rd_nil_view_address, shown_tab);
    Check(drop.panel == middle && drop.source == source && drop.heir == middle);
    Check(rd_arrangement_panel_from_id(arrangement, source) == &rd_nil_arrangement_panel);
    Check(rd_view_address_match(rd_arrangement_panel_from_id(arrangement, middle)->selected, a));
    Check(removed(arrangement, hidden) && !removed(arrangement, a));
    check_layout(arena, arrangement);
    // Moving within a panel settles nothing.
    drop = rd_arrangement_drop(arrangement, a, middle, Dir2_Invalid, rd_nil_view_address, shown_tab);
    Check(drop.panel == middle && drop.heir == 0 && arrangement->root->child_count == 2);

    // A split keeps a source with only a hidden tab, without selecting it.
    arrangement = build(arena, text);
    source = arrangement->root->first->id; middle = arrangement->root->first->next->id;
    a = tab_at(arrangement->root->first, 0); hidden = tab_at(arrangement->root->first, 1); b = tab_at(arrangement->root->first, 2);
    hidden_view = hidden;
    Check(rd_arrangement_select(arrangement, source, b));
    drop = rd_arrangement_drop(arrangement, a, middle, Dir2_Down, rd_nil_view_address, shown_tab);
    Check(drop.panel != 0 && drop.source == source && drop.heir == 0);
    Check(rd_view_address_match(rd_arrangement_panel_from_id(arrangement, source)->selected, b));
    Check(rd_view_address_match(rd_arrangement_panel_from_id(arrangement, drop.panel)->selected, a));
    rd_arrangement_drop(arrangement, b, middle, Dir2_Up, rd_nil_view_address, shown_tab);
    Check(rd_view_address_is_nil(rd_arrangement_panel_from_id(arrangement, source)->selected) && rd_arrangement_panel_from_id(arrangement, source)->tab_count == 1);
    check_layout(arena, arrangement);
    // Splitting its last tab off a panel keeps that panel, now empty.
    RD_PanelID landed = rd_arrangement_panel_from_view(arrangement, b)->id;
    drop = rd_arrangement_drop(arrangement, b, landed, Dir2_Right, rd_nil_view_address, shown_tab);
    Check(drop.source == landed && drop.heir == 0 && rd_arrangement_panel_from_id(arrangement, landed)->tab_count == 0);
    // A split takes an emptied source away and names its heir.
    RD_PanelID emptied = rd_arrangement_panel_from_view(arrangement, b)->id;
    drop = rd_arrangement_drop(arrangement, b, source, Dir2_Left, rd_nil_view_address, shown_tab);
    Check(drop.source == emptied && drop.heir != 0 && rd_arrangement_panel_from_id(arrangement, emptied) == &rd_nil_arrangement_panel);
    Check(rd_arrangement_panel_from_id(arrangement, landed)->tab_count == 0);
    check_layout(arena, arrangement);
    // No View only splits; settling without closing leaves an emptied panel.
    U64 panels_before = 0;
    for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p)) { panels_before += 1; }
    drop = rd_arrangement_drop(arrangement, rd_nil_view_address, source, Dir2_Down, rd_nil_view_address, shown_tab);
    Check(drop.panel != 0 && drop.source == 0 && rd_arrangement_panel_from_id(arrangement, drop.panel)->tab_count == 0);
    Check(!rd_arrangement_settle(arrangement, drop.panel, Dir2_Invalid, shown_tab, 0));
    Check(rd_arrangement_settle(arrangement, drop.panel, Dir2_Invalid, shown_tab, 1));
    Check(rd_arrangement_panel_from_id(arrangement, drop.panel) == &rd_nil_arrangement_panel);
    U64 panels_after = 0;
    for(RD_ArrangementPanel *p = arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(arrangement->root, p)) { panels_after += 1; }
    Check(panels_after == panels_before);
    check_layout(arena, arrangement);
    hidden_view = rd_nil_view_address;
  }

  // Resizing moves one boundary, holding both sides to the floor where they
  // can, after putting drifted siblings right.
  {
    RD_Arrangement *arrangement = build(arena, "split_x panels:{0.2:{text} 0.3:{text} 0.5:{text}}");
    RD_ArrangementPanel *a = arrangement->root->first, *b = a->next;
    rd_arrangement_resize(arrangement, a->id, 0.25f, 0.05f);
    Check(a->weight == 0.45f && b->weight == 0.05f);
    rd_arrangement_resize(arrangement, a->id, -0.6f, 0.05f);
    Check(a->weight == 0.05f && b->weight == 0.45f);
    rd_arrangement_resize(arrangement, arrangement->root->last->id, 0.1f, 0.05f);
    check_layout(arena, arrangement);
    arrangement = build(arena, "panels:{0.4:{text} 0.4:{text} 0.4:{text}}");
    rd_arrangement_resize(arrangement, arrangement->root->first->id, 0, 0.05f);
    Check(rd_arrangement_problem(arena, arrangement).size == 0 && arrangement->root->first->weight == 0.333333f);
    // Equalizing splits the two sides' space evenly, after the same rescale.
    arrangement = build(arena, "split_x panels:{0.2:{text} 0.3:{text} 0.5:{text}}");
    rd_arrangement_equalize(arrangement, arrangement->root->first->next->id);
    Check(arrangement->root->first->weight == 0.2f && arrangement->root->first->next->weight == 0.4f &&
          arrangement->root->last->weight == 0.4f);
    check_layout(arena, arrangement);
    arrangement = build(arena, "panels:{0.6:{text} 0.6:{text}}");
    rd_arrangement_equalize(arrangement, arrangement->root->first->id);
    Check(arrangement->root->first->weight == 0.5f && arrangement->root->last->weight == 0.5f);
    rd_arrangement_equalize(arrangement, arrangement->root->last->id);
    Check(arrangement->root->first->weight == 0.5f && rd_arrangement_problem(arena, arrangement).size == 0);
  }

  // A Floating Panel's arrangement is a leaf of tabs. A View moves between
  // two as a move into one and a settle of the other, which never closes its
  // root; emptied, it stays until it is discarded.
  {
    RD_Arrangement *into = build(arena, "panels:{text:{selected} terminal}");
    RD_Arrangement *from = build(arena, "panels:{jackstay:{selected} text}");
    RD_ViewAddress log = tab_at(into->root, 1), jackstay = tab_at(from->root, 0), text = tab_at(from->root, 1);
    Check(into->root->first == &rd_nil_arrangement_panel && into->root->tab_count == 2 && rd_view_address_match(into->root->selected, tab_at(into->root, 0)));
    Check(rd_arrangement_detach_tab(from, jackstay) && rd_arrangement_move_tab(into, jackstay, into->root->id, log));
    Check(rd_view_address_match(tab_at(into->root, 2), jackstay) && rd_view_address_match(into->root->selected, jackstay));
    check_layout(arena, into);
    Check(from->root->tab_count == 1 && rd_view_address_is_nil(from->root->selected) && !removed(from, jackstay));
    Check(rd_arrangement_settle(from, from->root->id, Dir2_Invalid, 0, 1) && rd_view_address_match(from->root->selected, text));
    check_layout(arena, from);
    Check(rd_arrangement_remove_tab(from, text) && !rd_arrangement_settle(from, from->root->id, Dir2_Invalid, 0, 1));
    Check(from->root != &rd_nil_arrangement_panel && from->root->tab_count == 0 && removed(from, text));
    rd_arrangement_discard(from);
    Check(from->root == &rd_nil_arrangement_panel && rd_arrangement_problem(arena, from).size == 0);
  }

  // The sidebar's edits. An empty arrangement's first panel makes the root;
  // sections arrive at weight 1 or at an equal share.
  {
    RD_Arrangement *sidebar = rd_arrangement_alloc(arena, test_owner);
    Check(sidebar->root == &rd_nil_arrangement_panel && sidebar->root_axis == Axis2_Y);
    RD_PanelID first = rd_arrangement_add(sidebar, 0, 1.f);
    RD_PanelID second = rd_arrangement_add(sidebar, sidebar->root->id, 1.f);
    Check(first != 0 && second != 0 && sidebar->root->child_count == 2 && sidebar->root->first->id == first);
    Check(rd_arrangement_add(sidebar, 0, 1.f) == 0 && rd_arrangement_add(sidebar, 99, 1.f) == 0);
    Check(sidebar->root->first->weight == 1.f && sidebar->root->last->weight == 1.f);
    RD_PanelID third = rd_arrangement_add(sidebar, sidebar->root->id, 0);
    for(RD_ArrangementPanel *p = sidebar->root->first; p != &rd_nil_arrangement_panel; p = p->next) { Check(p->weight == 0.333333f); }
    Check(sidebar->root->last->id == third);
    check_layout(arena, sidebar);
    // An equal share counts malformed or zero weights as 0.01.
    sidebar->root->first->weight = 0;
    rd_arrangement_add(sidebar, sidebar->root->id, 0);
    for(RD_ArrangementPanel *p = sidebar->root->first; p != &rd_nil_arrangement_panel; p = p->next) { Check(p->weight > 0); }
    check_layout(arena, sidebar);
  }

  // A sidebar merged into one panel keeps its tabs, Selected View and
  // Presentation State in a panel of their own; the root keeps its ID.
  {
    RD_Arrangement *sidebar = build(arena, "panels:{text:{selected} terminal}");
    RD_PanelID root = sidebar->root->id;
    RD_ViewAddress text = tab_at(sidebar->root, 0);
    RD_PanelID added = rd_arrangement_add(sidebar, root, 0);
    RD_ArrangementPanel *lifted = sidebar->root->first;
    Check(sidebar->root->id == root && sidebar->root->tab_count == 0 && rd_view_address_is_nil(sidebar->root->selected));
    Check(lifted->tab_count == 2 && rd_view_address_match(lifted->selected, text) && lifted->weight == 0.5f && sidebar->root->last->id == added);
    // The shell is told the lifted panel takes the root's Presentation State.
    Check(lifted->options_from == root && lifted->id != root);
    check_layout(arena, sidebar);
    RD_PanelTree tree = rd_panel_tree_from_arrangement(arena, test_owner, sidebar);
    Check(rd_view_address_match(tree.root->first->selected_tab, text));
  }

  // Removing a panel leaves its siblings' weights, and a split it leaves with
  // one panel, as they are; a split it empties goes too, short of the root.
  {
    RD_Arrangement *sidebar = build(arena, "panels:{0.5:{text} 0.3:{0.5:{terminal} 0.5:{}} 0.2:{jackstay}}");
    RD_ArrangementPanel *split = sidebar->root->first->next;
    RD_PanelID split_id = split->id;
    RD_ViewAddress terminal = tab_at(split->first, 0);
    Check(!rd_arrangement_remove(sidebar, sidebar->root->id) && !rd_arrangement_remove(sidebar, 99));
    Check(rd_arrangement_remove(sidebar, split->last->id));
    Check(split->child_count == 1 && sidebar->root->child_count == 3 && sidebar->root->first->weight == 0.5f);
    Check(rd_arrangement_remove(sidebar, rd_arrangement_panel_from_view(sidebar, terminal)->id));
    Check(sidebar->root->child_count == 2 && sidebar->root->first->weight == 0.5f && sidebar->root->last->weight == 0.2f);
    Check(rd_arrangement_panel_from_id(sidebar, split_id) == &rd_nil_arrangement_panel && removed(sidebar, terminal));
  }

  // A layout producer replaces a document: clearing leaves one empty root
  // with a new ID, keeps its axis until it splits, and removes every View,
  // which a tab can take back.
  {
    RD_Arrangement *arrangement = build(arena, "split_x panels:{0.5:{text:{selected} terminal} 0.5:{jackstay}}");
    RD_PanelID old_last = arrangement->root->last->id;
    RD_ViewAddress text = tab_at(arrangement->root->first, 0), kept = tab_at(arrangement->root->last, 0);
    RD_PanelID root = rd_arrangement_clear(arrangement);
    Check(arrangement->root->id == root && root > old_last && arrangement->root->tab_count == 0 && arrangement->root_axis == Axis2_X);
    Check(removed(arrangement, text) && removed(arrangement, kept));
    Check(rd_arrangement_problem(arena, arrangement).size == 0);
    RD_PanelID side = rd_arrangement_split(arrangement, root, Dir2_Right);
    rd_arrangement_resize(arrangement, root, 0.22f, 0);
    Check(rd_arrangement_panel_from_id(arrangement, root)->weight == 0.72f && rd_arrangement_panel_from_id(arrangement, side)->weight == 0.28f);
    RD_ViewAddress made = new_view("text");
    Check(rd_arrangement_move_tab(arrangement, made, side, rd_nil_view_address) && rd_arrangement_move_tab(arrangement, kept, root, rd_nil_view_address));
    Check(rd_arrangement_panel_from_view(arrangement, kept)->id == arrangement->root->first->id);
    check_layout(arena, arrangement);
    // A new owner's document has a panel once it is cleared; one that never
    // splits keeps the axis it had.
    RD_Arrangement *fresh = rd_arrangement_alloc(arena, test_owner);
    root = rd_arrangement_clear(fresh);
    RD_ViewAddress tab = new_view("terminal");
    Check(rd_arrangement_move_tab(fresh, tab, root, rd_nil_view_address) && rd_view_address_match(fresh->root->selected, tab));
    Check(fresh->root_axis == Axis2_Y);
    check_layout(arena, fresh);
  }

  // Restore's operations. A View detached from one arrangement and inserted
  // into another changes neither's other panels, and lands selected only if
  // it was.
  {
    RD_Arrangement *panels = build(arena, "panels:{0.5:{terminal:{selected} text} 0.5:{jackstay}}");
    RD_Arrangement *sidebar = build(arena, "panels:{sidebar_section:{selected} text}");
    for(U32 source_first = 0; source_first < 2; source_first++)
    {
      RD_Arrangement *source = source_first ? sidebar : panels, *destination = source_first ? panels : sidebar;
      RD_ViewAddress moving = source_first ? sidebar->root->last_tab->view : panels->root->first->last_tab->view;
      RD_ArrangementPanel *leaf = source_first ? panels->root->first : sidebar->root;
      RD_ViewAddress kept_selected = leaf->selected;
      Check(rd_arrangement_detach_tab(source, moving) && !rd_arrangement_detach_tab(source, moving));
      Check(rd_arrangement_panel_from_view(source, moving) == &rd_nil_arrangement_panel && !removed(source, moving));
      Check(rd_arrangement_insert_tab(destination, moving, leaf->id, leaf->last_tab->view, 0));
      Check(rd_view_address_match(leaf->selected, kept_selected) && rd_view_address_match(leaf->last_tab->view, moving));
      check_layout(arena, source);
      check_layout(arena, destination);
    }
    RD_ViewAddress chosen = panels->root->first->first_tab->view;
    RD_ArrangementPanel *right = panels->root->last;
    Check(rd_arrangement_insert_tab(panels, chosen, right->id, rd_nil_view_address, 1));
    Check(rd_view_address_match(right->selected, chosen) && rd_view_address_match(right->first_tab->view, chosen));
    Check(rd_view_address_is_nil(panels->root->first->selected));
    check_layout(arena, panels);
  }

  // Two arrangements are one document when their panels, IDs, weights,
  // tabs and Selected Views are.
  {
    RD_Arrangement *a = build(arena, "split_x panels:{0.5:{text:{selected}} 0.5:{terminal}}");
    RD_Arrangement *b = rd_arrangement_copy(arena, a);
    Check(rd_arrangement_match(a, b));
    b->root->first->weight = 0.4f;
    Check(!rd_arrangement_match(a, b));
    b = rd_arrangement_copy(arena, a);
    rd_arrangement_select(b, b->root->first->id, rd_nil_view_address);
    Check(!rd_arrangement_match(a, b));
    b = rd_arrangement_copy(arena, a);
    rd_arrangement_move_tab(b, new_view("text"), b->root->last->id, rd_nil_view_address);
    Check(!rd_arrangement_match(a, b));
  }

  // A deep split chain copies without exhausting the C stack, and a copy is
  // independent of its source.
  {
    Temp deep_temp = temp_begin(arena);
    enum { depth = 250000 };
    // Built as a hand-edited document would load: by the module's own
    // allocation and linking, not an operation per panel.
    RD_Arrangement *arrangement = rd_arrangement_alloc(arena, test_owner);
    rd_arrangement_clear(arrangement);
    RD_ArrangementPanel *node = arrangement->root;
    for(U32 i = 1; i < depth; i++)
    {
      RD_ArrangementPanel *child = rd_arrangement_panel_alloc(arrangement);
      child->weight = (F32)(i%97)/97;
      rd_arrangement_insert(node, &rd_nil_arrangement_panel, child);
      node = child;
    }
    rd_arrangement_insert_tab(arrangement, new_view("text"), node->id, rd_nil_view_address, 1);
    RD_Arrangement *copy = rd_arrangement_copy(arena, arrangement);
    RD_ArrangementPanel *a = arrangement->root, *b = copy->root;
    for(U32 i = 0; i < depth; i++)
    {
      Check(b != &rd_nil_arrangement_panel && b != a && b->id == a->id && b->weight == a->weight);
      Check(b->child_count == a->child_count && (b->parent == &rd_nil_arrangement_panel) == (i == 0));
      a = a->first; b = b->first;
    }
    Check(a == &rd_nil_arrangement_panel && b == &rd_nil_arrangement_panel);
    Check(rd_arrangement_match(arrangement, copy));
    temp_end(deep_temp);
  }

  // Random edits keep a valid arrangement valid, and the renderer lays it
  // out as the arrangement does.
  for(U64 seed = 1; seed <= 40; seed++)
  {
    U64 state = seed;
    RD_Arrangement *arrangement = build(arena, seed & 1 ? "split_x panels:{text terminal jackstay sessions binary}" :
      "panels:{0.25:{text terminal} 0.75:{0.5:{jackstay} 0.5:{sessions binary}}}");
    for(U64 step = 0; step < 12; step++)
    {
      U64 op = next_random(&state) % 5;
      RD_ArrangementPanel *panel = random_panel(arrangement, &state, 1);
      if(op == 0 || op == 1) { rd_arrangement_split(arrangement, panel->id, (Dir2)(next_random(&state) % 4)); }
      if(op == 2 && arrangement->root->first != &rd_nil_arrangement_panel) { rd_arrangement_close(arrangement, panel->id); }
      if(op == 3 || op == 0)
      {
        RD_ArrangementPanel *source = random_panel(arrangement, &state, 1);
        if(source->first_tab) { rd_arrangement_move_tab(arrangement, source->first_tab->view, panel->id, panel->last_tab ? panel->last_tab->view : rd_nil_view_address); }
      }
      if(op == 4)
      {
        if(panel->first_tab && next_random(&state) % 2) { rd_arrangement_remove_tab(arrangement, panel->first_tab->view); }
        else { rd_arrangement_reorder(arrangement, panel->id, panel->parent->last->id); }
      }
      if(op == 1 && panel->next != &rd_nil_arrangement_panel)
      { rd_arrangement_resize(arrangement, panel->id, (F32)(next_random(&state) % 100)/100.f - .5f, .05f); }
      check_layout(arena, arrangement);
    }
  }

  cfg_state_release(cfg);
  arena_release(arena);
  fprintf(stderr, "Arrangement: %u failures\n", failures);
  exit(failures != 0);
}
