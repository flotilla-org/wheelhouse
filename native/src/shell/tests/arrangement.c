// Headless tests of what the shell keeps of arrangements: its live copy's
// nodes across the core's edits, copies and matching, weight strings, and
// the renderer's panel tree and geometry read from it. The edit rules are
// the core's and are tested there (crates/wheelhouse-core/src/arrangement/
// tests.rs, and against the C they replaced in
// crates/wheelhouse-arrangement-oracle). Fixtures are described in the
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

  // A weight to write is %f's; one that %f would change keeps more digits.
  {
    F32 kept = (F32)f64_from_str8(str8_lit("0.3333333333"));
    Check(str8_match(rd_arrangement_weight_string(arena, 0.5f), str8_lit("0.500000"), 0));
    Check((F32)f64_from_str8(rd_arrangement_weight_string(arena, kept)) == kept);
    Check(rd_arrangement_quantize(1.f/3.f) == 0.333333f);
  }

  // An edit is the core's; the shell keeps its live copy's nodes. A panel
  // the edit keeps is the node it was, so a pointer to it stays good; one it
  // takes away is left unlinked and empty; the removed Views are listed
  // newest first.
  {
    RD_Arrangement *arrangement = build(arena, "split_x panels:{0.5:{text:{selected} terminal} 0.5:{jackstay}}");
    RD_ArrangementPanel *left = arrangement->root->first, *right = arrangement->root->last;
    RD_ViewAddress text = tab_at(left, 0), terminal = tab_at(left, 1), jackstay = tab_at(right, 0);
    Check(rd_arrangement_remove_tab(arrangement, text) && rd_arrangement_remove_tab(arrangement, terminal));
    Check(arrangement->root->first == left && left->tab_count == 0 && rd_view_address_is_nil(left->selected));
    Check(rd_view_address_match(arrangement->first_removed->view, terminal) && rd_view_address_match(arrangement->first_removed->next->view, text));
    RD_PanelID heir = rd_arrangement_close(arrangement, left->id);
    Check(heir == right->id && arrangement->root == right && right->weight == 1.f && right->parent == &rd_nil_arrangement_panel);
    Check(left->parent == &rd_nil_arrangement_panel && left->next == &rd_nil_arrangement_panel && left->prev == &rd_nil_arrangement_panel);
    Check(rd_view_address_match(right->first_tab->view, jackstay) && right->tab_count == 1);
    RD_PanelID made = rd_arrangement_split(arrangement, right->id, Dir2_Down);
    Check(rd_arrangement_panel_from_id(arrangement, made)->parent->first == right && arrangement->root_axis == Axis2_Y);
    check_layout(arena, arrangement);
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

  // After random edits (the core's), the renderer lays an arrangement out as
  // the arrangement does.
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
