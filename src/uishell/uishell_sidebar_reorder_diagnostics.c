// Row reorder (drag-model.md, "Reorder"): drags through the production
// renderer reorder siblings only, save the run's order per window, restore it
// into a fresh core, snap back outside the run, and reset from the row menu.
#define ReorderCheck(cond, what) do { if(!(cond)) { fprintf(stderr, "FAIL reorder: %s\n", what); ok = 0; } } while(0)

internal UI_Box *
uishell_sidebar_reorder_box(UI_Box *box, String8 needle)
{
  for(UI_Box *b = box; !ui_box_is_nil(b); b = b->next)
  {
    if(b->string.size >= needle.size && str8_match(str8_postfix(b->string, needle.size), needle, 0)) { return b; }
    UI_Box *found = uishell_sidebar_reorder_box(b->first, needle);
    if(!ui_box_is_nil(found)) { return found; }
  }
  return &ui_nil_box;
}

// Entity ids of one kind, in displayed order, joined by spaces.
internal String8
uishell_sidebar_reorder_ids(Arena *arena, UIShell_SidebarState *state, String8 kind)
{
  String8List ids = {0};
  for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && str8_match(uishell_sidebar_string(node.entity_kind), kind, 0))
    { str8_list_push(arena, &ids, uishell_sidebar_string(node.entity_id)); }
  }
  StringJoin join = {.sep = str8_lit(" ")};
  return str8_list_join(arena, &ids, &join);
}

internal AndamentoNode
uishell_sidebar_reorder_node(UIShell_SidebarState *state, String8 id)
{
  AndamentoNode node = {0};
  for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    andamento_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && str8_match(uishell_sidebar_string(node.entity_id), id, 0)) { return node; }
  }
  return (AndamentoNode){0};
}

internal U64
uishell_sidebar_reorder_section_count(CFG_Node *window)
{
  U64 count = 0;
  CFG_Node *root = cfg_node_child_from_string(window, str8_lit("sidebar_local"));
  for(CFG_Node *n = root->first; n != &cfg_nil_node; n = n->next) { count += str8_match(n->string, str8_lit("section"), 0); }
  return count;
}

// The group of the local section a drop made (the one after `before`), or nil.
internal CFG_Node *
uishell_sidebar_reorder_new_group(CFG_Node *window, U64 before)
{
  CFG_Node *root = cfg_node_child_from_string(window, str8_lit("sidebar_local"));
  U64 index = 0;
  for(CFG_Node *n = root->first; n != &cfg_nil_node; n = n->next)
  {
    if(!str8_match(n->string, str8_lit("section"), 0)) { continue; }
    if(index++ == before) { return cfg_node_child_from_string(n, str8_lit("group")); }
  }
  return &cfg_nil_node;
}

// Removes a section a test made, and its View.
internal void
uishell_sidebar_reorder_drop_section(CFG_Node *window, CFG_Node *group)
{
  if(group == &cfg_nil_node) { return; }
  CFG_Node *view = uishell_sidebar_local_view(window, group);
  if(view != &cfg_nil_node) { cfg_node_release(rd_state->cfg, view); }
  cfg_node_release(rd_state->cfg, group->parent);
}

typedef struct UIShell_ReorderDrag UIShell_ReorderDrag;
struct UIShell_ReorderDrag
{
  B32 started, line, menu, ghost_target, lifted, lift_text_inside;
};

typedef enum UIShell_ReorderMode
{
  UIShell_ReorderMode_Drag,
  UIShell_ReorderMode_RightClick,
  // Cancel as the shared Esc handler does, then release over the source row.
  UIShell_ReorderMode_Cancel,
  // Release on a docking site below the tree's panel, as RAD commits one.
  UIShell_ReorderMode_Dock,
}
UIShell_ReorderMode;

// Press on a row, move by `offset` or to a fraction down row `to_id` (from last
// frame's layout), then release.
internal UIShell_ReorderDrag
uishell_sidebar_reorder_gesture(RD_WindowState *ws, UIShell_ControlledSplit *split, CFG_Node *view,
                                UIShell_SidebarState *state, String8 id, Vec2F32 offset, String8 to_id,
                                F32 to_fraction, UIShell_ReorderMode mode, CFG_Node *pinned_area)
{
  UIShell_ReorderDrag result = {0};
  B32 right_click = mode == UIShell_ReorderMode_RightClick;
  UI_State *saved_ui = ui_state;
  UI_State *test_ui = ui_state_alloc(); ui_select_state(test_ui);
  UI_IconInfo icons = ws->ui->icon_info; UI_AnimationInfo animation = {0};
  Vec2F32 start = v2f32(-100, -100), pointer = start;
  // Rendering refreshes the snapshot, so keys are copied.
  Temp scratch = scratch_begin(0, 0);
  String8 key = push_str8_copy(scratch.arena, uishell_sidebar_string(uishell_sidebar_reorder_node(state, id).key));
  String8 to_key = to_id.size ? push_str8_copy(scratch.arena, uishell_sidebar_string(uishell_sidebar_reorder_node(state, to_id).key)) : str8_zero();
  for(U32 frame = 0; frame < 6; frame++)
  {
    UI_EventList events = {0}; UI_EventNode event = {0};
    Rng2F32 pinned_rect = r2f32p(0, 620, 320, 820);
    if(frame == 4 && mode == UIShell_ReorderMode_Cancel) { pointer = start; }
    else if(frame >= 2 && pinned_area != &cfg_nil_node) { pointer = offset.x ? offset : center_2f32(pinned_rect); }
    else if(frame >= 2 && !right_click)
    {
      UI_Box *to = to_key.size ? uishell_sidebar_reorder_box(test_ui->root, push_str8f(ui_build_arena(), "###entry_%S", to_key)) : &ui_nil_box;
      pointer = ui_box_is_nil(to) ? add_2f32(start, offset) :
        v2f32(start.x, to->rect.y0+dim_2f32(to->rect).y*to_fraction);
    }
    if(frame == 4 && mode == UIShell_ReorderMode_Dock) { uishell_sidebar_drag_panel_drop(view->parent->id, Dir2_Down, 0); }
    if(frame == 1 || frame == 4)
    {
      event.v = (UI_Event){.kind = frame == 1 ? UI_EventKind_Press : UI_EventKind_Release,
        .key = right_click ? WM_Key_RightMouseButton : WM_Key_LeftMouseButton, .pos = pointer};
      events.first = events.last = &event; events.count = 1;
    }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = pointer;
    state->managed_cfg_generation = cfg_change_gen(); state->managed_dirty = 0;
    uishell_sidebar_row_lift(state);
    UIShell_RegsScope(.window = split->owner_cfg->id, .view = view->id, .panel = view->parent->id)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_sidebar_render(r2f32p(0, 0, 320, 600), split, (UIShell_SidebarRenderParams){UIShell_SidebarRenderMode_SectionPanel, str8_lit("tree")}); }
    // The local group's section, as its View renders it, in a stand-in panel
    // (the window) whose own catch-all site has the pointer first, as in a
    // window.
    if(pinned_area != &cfg_nil_node)
    UIShell_RegsScope(.window = split->owner_cfg->id, .view = 0, .panel = split->owner_cfg->id)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    {
      ui_state->drop_hot_box_key = ui_key_from_stringf(ui_key_zero(), "catchall_drop_site_%p", split->owner_cfg);
      String8 local_key = push_str8f(ui_build_arena(), ".section:%S", uishell_sidebar_local_field(pinned_area->parent, str8_lit("id")));
      UI_Box *view_parent;
      UI_Rect(pinned_rect) { view_parent = ui_build_box_from_key(UI_BoxFlag_Clip, ui_key_make(119168)); }
      UI_Parent(view_parent) { uishell_sidebar_render(pinned_rect, split, (UIShell_SidebarRenderParams){UIShell_SidebarRenderMode_SectionPanel, local_key}); }
    }
    uishell_sidebar_drag_finish(ws);
    ui_end_build();
    if(frame == 0)
    {
      UI_Box *entry = uishell_sidebar_reorder_box(test_ui->root, push_str8f(ui_build_arena(), "###entry_%S", key));
      if(!ui_box_is_nil(entry)) { start = pointer = center_2f32(entry->rect); }
    }
    if(frame == 3)
    {
      result.started = rd_drag_is_active() && rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_View &&
        rd_state->drag_drop_commit == uishell_sidebar_drag_panel_drop;
      result.line = !ui_box_is_nil(ui_box_from_key(ui_key_from_stringf(ui_key_zero(), "sidebar_row_drop_line")));
      result.ghost_target = pinned_area != &cfg_nil_node &&
        !ui_box_is_nil(ui_box_from_key(ui_key_from_stringf(ui_key_zero(), "group_drop_line_%S", uishell_sidebar_local_field(pinned_area, str8_lit("id")))));
      UI_Box *lift = ui_box_from_key(ui_key_from_stringf(ui_key_zero(), "sidebar_row_lift"));
      result.lifted = !ui_box_is_nil(lift);
      // The copy is the row, drawn as its home row is: its icon and label lay
      // out inside it, not offset by the copy's rect.
      UI_Box *label = result.lifted ? uishell_sidebar_reorder_box(lift->first, id) : &ui_nil_box;
      UI_Box *icon = result.lifted ? uishell_sidebar_reorder_box(lift->first, rd_icon_kind_text_table[RD_IconKind_Threads]) : &ui_nil_box;
      Rng2F32 inside = pad_2f32(lift->rect, 0.5f);
      result.lift_text_inside = !ui_box_is_nil(label) && !ui_box_is_nil(icon) && dim_2f32(label->rect).x > 0 &&
        contains_2f32(inside, label->rect.p0) && contains_2f32(inside, label->rect.p1) &&
        contains_2f32(inside, icon->rect.p0) && contains_2f32(inside, icon->rect.p1) &&
        (icon->flags & UI_BoxFlag_DisableTextTrunc);
      if(mode == UIShell_ReorderMode_Cancel) { rd_drag_kill(); ui_kill_action(); }
    }
    if(frame == 5) { result.menu = ui_any_ctx_menu_is_open(); }
  }
  rd_drag_kill(); ui_kill_action(); ui_ctx_menu_close();
  ui_select_state(saved_ui); ui_state_release(test_ui);
  scratch_end(scratch);
  return result;
}

internal B32
uishell_sidebar_reorder_diagnostics(RD_WindowState *ws, UIShell_ControlledSplit *host_split)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *saved = ws->sidebar;
  B32 ok = 1;
  char *error = 0;
  UIShell_SidebarState state = {.initialized = 1, .restored = 1};
  String8 config = str8_lit(
    "region \"tree\" root-template=\"title\" placement=\"tree\"\n"
    "template \"title\" slot=\"compact\" node-kind=\"entity\" { field \"label\" source=\"literal\" value=\"Projects\"; }\n"
    "placement \"tree\" { for \"project\" kind=\"project\" { apply-template \"project\"; }; }\n"
    "template \"project\" { field \"label\" key=\"display.label\"; for \"convoy\" kind=\"convoy\" { match \"flotilla.project\" of=\"project\"; apply-template \"convoy\"; }; }\n"
    "template \"convoy\" { field \"label\" key=\"display.label\"; }\n"
    "region \"local\" root-template=\"local/title\" placement=\"local\"\n"
    "template \"local/title\" slot=\"compact\" node-kind=\"entity\" { field \"label\" source=\"literal\" value=\"Local\"; }\n"
    "placement \"local\" { for \"section\" kind=\".section\" layout=\"section\" { order \".position\" natural=true; apply-template \"section/local\"; }; }\n"
    "template \"section/local\" { field \"label\" key=\"display.label\"; for \"group\" kind=\".group\" { match \".section\" of=\"section\"; apply-template \"group/local\"; }; }\n"
    "template \"group/local\" { field \"label\" key=\"display.label\"; for \"item\" { match \".group\" of=\"group\"; order \".position\" natural=true; apply-template \"convoy\"; }; }\n");
  state.core = andamento_create(config.str, config.size, &error);
  ok &= uishell_sidebar_result(&state, state.core != 0, error);
  if(!state.core) { uishell_sidebar_release(&state); scratch_end(scratch); return 0; }
  char *entities[][3] = {{"project", "p", "p"}, {"project", "q", "q"},
    {"convoy", "c1", "p"}, {"convoy", "c2", "p"}, {"convoy", "c3", "p"}};
  for(U64 i = 0; i < ArrayCount(entities); i++)
  {
    String8 patch = push_str8f(scratch.arena,
      "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"%s\",\"id\":\"%s\"}},\"source_id\":\"reorder-test\",\"set\":{"
      "\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"%s\"}},"
      "\"flotilla.project\":{\"value\":{\"type\":\"text\",\"value\":\"%s\"}}},\"unset\":[]}",
      entities[i][0], entities[i][1], entities[i][1], entities[i][2]);
    ok &= andamento_apply_patch_json(state.core, 0, uishell_sidebar_text(patch), 0);
  }
  uishell_sidebar_refresh(&state);
  UIShell_ControlledSplit split = *host_split;
  MemoryZeroStruct(&split.inventory);
  UIShell_SidebarSection section = {.key = str8_lit("tree")};
  state.sections = &section;
  ws->sidebar = &state;
  CFG_Node *window = host_split->owner_cfg;
  CFG_Node *view = uishell_sidebar_region_view(window, str8_lit("tree"));
  String8 convoy = str8_lit("convoy"), project = str8_lit("project");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c1 c2 c3"), 0), "data order");

  // Within the threshold nothing drags, and a click never reorders. These
  // rows have no recipe, so an activation leaves an inspection message.
  state.inspection[0] = 0;
  UIShell_ReorderDrag still = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c1"), v2f32(0, 3), str8_zero(), 0, UIShell_ReorderMode_Drag, &cfg_nil_node);
  ReorderCheck(!still.started && !still.line, "sub-threshold press stays a click");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c1 c2 c3"), 0), "click keeps order");
  ReorderCheck(state.inspection[0] != 0, "a click activates the row");

  // Esc mid-drag, then a release back over the row: no reorder, no activation.
  state.inspection[0] = 0;
  UIShell_ReorderDrag cancel = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c1"), v2f32(0, 0), str8_lit("c3"), 0.8f, UIShell_ReorderMode_Cancel, &cfg_nil_node);
  ReorderCheck(cancel.started, "cancelled drag had started");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c1 c2 c3"), 0), "cancel keeps order");
  ReorderCheck(state.inspection[0] == 0, "release after cancel doesn't activate");

  // Below c3's midpoint lands c1 last; the drag shows the line meanwhile.
  UIShell_ReorderDrag down = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c1"), v2f32(0, 0), str8_lit("c3"), 0.8f, UIShell_ReorderMode_Drag, &cfg_nil_node);
  ReorderCheck(down.started, "row drag starts as the shared sidebar drag");
  ReorderCheck(down.line, "insertion line while over a moving gap");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c2 c3 c1"), 0), "drop below c3 reorders");
  String8 loop = uishell_sidebar_loop_key(state.snapshot, 0);
  for(U64 i = 0; i < andamento_snapshot_node_count(state.snapshot); i++)
  {
    AndamentoNode node = {0}; andamento_snapshot_node(state.snapshot, i, &node);
    if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("c1"), 0))
    { loop = push_str8_copy(scratch.arena, uishell_sidebar_loop_key(state.snapshot, i)); }
  }
  CFG_Node *run = cfg_node_child_from_string(cfg_node_child_from_string(window, str8_lit("sidebar_order")), loop);
  String8List leaves = {0};
  for(CFG_Node *n = run->first; n != &cfg_nil_node; n = n->next) { str8_list_push(scratch.arena, &leaves, n->string); }
  StringJoin join = {.sep = str8_lit(" ")};
  ReorderCheck(str8_match(str8_list_join(scratch.arena, &leaves, &join), str8_lit("convoy c2 convoy c3 convoy c1"), 0),
    "window saves the run's full order");

  // A gap that leaves the row where it is shows no line and changes nothing.
  UIShell_ReorderDrag same = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c2"), v2f32(0, 0), str8_lit("c3"), 0.2f, UIShell_ReorderMode_Drag, &cfg_nil_node);
  ReorderCheck(same.started && !same.line, "no line over the row's own gaps");
  // Off to the side of the run is no target, so the release snaps back.
  UIShell_ReorderDrag away = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c2"), v2f32(400, 0), str8_zero(), 0, UIShell_ReorderMode_Drag, &cfg_nil_node);
  ReorderCheck(away.started && !away.line, "no line outside the run");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c2 c3 c1"), 0), "snap back keeps order");

  // Projects reorder at their own level, keeping their convoys' order.
  UIShell_ReorderDrag up = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("q"), v2f32(0, 0), str8_lit("p"), 0.2f, UIShell_ReorderMode_Drag, &cfg_nil_node);
  ReorderCheck(up.line, "line between projects");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, project), str8_lit("q p"), 0), "project drop above p");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c2 c3 c1"), 0), "convoys unaffected");

  // The saved text restores into a fresh core, and new rows merge by data order.
  CFG_State *persisted_cfg = cfg_state_alloc();
  String8 saved_text = cfg_string_from_tree(scratch.arena, rd_state->cfg_schema_table, str8_zero(), window);
  CFG_NodePtrList loaded = cfg_node_ptr_list_from_string(scratch.arena, persisted_cfg, rd_state->cfg_schema_table, str8_zero(), saved_text);
  ReorderCheck(loaded.count == 1, "window text round-trips");
  UIShell_SidebarState restored = {.initialized = 1, .restored = 1};
  error = 0; restored.core = andamento_create(config.str, config.size, &error);
  ok &= uishell_sidebar_result(&restored, restored.core != 0, error);
  if(restored.core && loaded.count == 1)
  {
    uishell_sidebar_restore_orders(&restored, loaded.first->v);
    for(U64 i = 0; i < ArrayCount(entities)+1; i++)
    {
      char **e = i < ArrayCount(entities) ? entities[i] : (char *[]){"convoy", "c4", "p"};
      String8 patch = push_str8f(scratch.arena,
        "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"%s\",\"id\":\"%s\"}},\"source_id\":\"reorder-test\",\"set\":{"
        "\"flotilla.project\":{\"value\":{\"type\":\"text\",\"value\":\"%s\"}}},\"unset\":[]}", e[0], e[1], e[2]);
      ok &= andamento_apply_patch_json(restored.core, 0, uishell_sidebar_text(patch), 0);
    }
    uishell_sidebar_refresh(&restored);
    ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &restored, convoy), str8_lit("c2 c3 c4 c1"), 0), "restored order merges c4 after c3");
    ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &restored, project), str8_lit("q p"), 0), "restored project order");
  }
  uishell_sidebar_release(&restored);
  cfg_state_release(persisted_cfg);

  // A dragged row lifts and follows the pointer. Over a local group it gets
  // an insertion point between that group's items; the release adds a ghost
  // of the row there, as a row, at that index, and its run keeps its order.
  {
    CFG_Node *area = uishell_sidebar_local_new_group(window, str8_lit("Pinned"));
    char *existing[] = {"first-pin", "second-pin"};
    String8 ghosts[2];
    for(U64 i = 0; i < 2; i++)
    {
      CFG_Node *pin = cfg_node_new(rd_state->cfg, area, str8_lit("card"));
      uishell_sidebar_pin_new_ghost(pin);
      uishell_sidebar_pin_set_field(pin, str8_lit("kind"), str8_lit("convoy"));
      uishell_sidebar_pin_set_field(pin, str8_lit("entity"), str8_cstring(existing[i]));
      cfg_node_new(rd_state->cfg, pin, str8_lit("compact"));
      ghosts[i] = push_str8_copy(scratch.arena, uishell_sidebar_pin_ghost(pin));
    }
    uishell_sidebar_publish_local(&state, &split);
    uishell_sidebar_refresh(&state);
    String8 before = push_str8_copy(scratch.arena, uishell_sidebar_reorder_ids(scratch.arena, &state, convoy));
    // Two compact items below a header: just past the first item's midpoint.
    F32 row = floor_f32(11*2.2f);
    Vec2F32 between = v2f32(160, 620+row+2+row*0.5f+3.f);
    UIShell_ReorderDrag ghost = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c2"), between, str8_zero(), 0, UIShell_ReorderMode_Drag, area);
    CFG_Node *added = &cfg_nil_node;
    U64 pin_count = 0;
    for(CFG_Node *c = area->first; c != &cfg_nil_node; c = c->next)
    {
      if(!str8_match(c->string, str8_lit("card"), 0)) { continue; }
      pin_count++;
      if(str8_match(cfg_node_child_from_string(c, str8_lit("entity"))->first->string, str8_lit("c2"), 0)) { added = c; }
    }
    // The group's items, in the order it shows them.
    String8 shown[3] = {0};
    U64 shown_count = 0;
    uishell_sidebar_refresh(&state);
    for(U64 i = 0; i < andamento_snapshot_node_count(state.snapshot); i++)
    {
      AndamentoNode n = {0}; andamento_snapshot_node(state.snapshot, i, &n);
      if(str8_match(uishell_sidebar_string(n.entity_kind), str8_lit(".ref"), 0) && shown_count < 3)
      { shown[shown_count++] = push_str8_copy(scratch.arena, uishell_sidebar_string(n.entity_id)); }
    }
    ReorderCheck(ghost.started && ghost.lifted, "a dragged row lifts and follows the pointer");
    ReorderCheck(ghost.lift_text_inside, "the lifted copy is the row: its whole icon and label inside it");
    ReorderCheck(ghost.ghost_target && !ghost.line, "a local group shows an insertion point, not the reorder line");
    ReorderCheck(pin_count == 3 && added != &cfg_nil_node && !uishell_sidebar_pin_expanded(added) && shown_count == 3 &&
                 str8_match(shown[0], ghosts[0], 0) && str8_match(shown[1], uishell_sidebar_pin_ghost(added), 0) && str8_match(shown[2], ghosts[1], 0),
                 "the drop adds a ghost of the row, as a row, at the insertion point");
    ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), before, 0), "a ghost drop leaves the run's order");
    // An edge site committed in the release frame wins over the group's claim.
    U64 sections_before = uishell_sidebar_reorder_section_count(window);
    UIShell_ReorderDrag edge = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c2"), between, str8_zero(), 0, UIShell_ReorderMode_Dock, area);
    U64 area_pins = 0;
    for(CFG_Node *c = area->first; c != &cfg_nil_node; c = c->next) { area_pins += str8_match(c->string, str8_lit("card"), 0); }
    CFG_Node *split_group = uishell_sidebar_reorder_new_group(window, sections_before);
    ReorderCheck(edge.started && area_pins == 3 && cfg_node_child_from_string(split_group, str8_lit("card")) != &cfg_nil_node,
                 "an edge site taken in the release frame wins over the group's claim");
    uishell_sidebar_reorder_drop_section(window, split_group);
    uishell_sidebar_manual_sizing(window, 0);
    cfg_node_release(rd_state->cfg, area->parent);
  }

  // A row dropped on a docking site makes a new pinned area there holding a
  // ghost of the row, as a row: the same creation drag as a card's.
  {
    String8 before = push_str8_copy(scratch.arena, uishell_sidebar_reorder_ids(scratch.arena, &state, convoy));
    U64 sections_before = uishell_sidebar_reorder_section_count(window);
    UIShell_ReorderDrag dock = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c3"), v2f32(0, 400), str8_zero(), 0, UIShell_ReorderMode_Dock, &cfg_nil_node);
    CFG_Node *area = uishell_sidebar_reorder_new_group(window, sections_before);
    CFG_Node *pin = cfg_node_child_from_string(area, str8_lit("card"));
    ReorderCheck(dock.started && !dock.line, "a row drag away from its run claims no reorder");
    ReorderCheck(pin != &cfg_nil_node && str8_match(cfg_node_child_from_string(pin, str8_lit("entity"))->first->string, str8_lit("c3"), 0) &&
                 !uishell_sidebar_pin_expanded(pin) && uishell_sidebar_local_view(window, area) != &cfg_nil_node,
                 "a docking-site drop makes a section, in a View there, whose group holds the row's ghost, as a row");
    ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), before, 0), "a docking-site drop leaves the run's order");
    uishell_sidebar_reorder_drop_section(window, area);
    uishell_sidebar_manual_sizing(window, 0);
  }

  // A reordered run's row offers Reset order, which returns it to data order.
  UIShell_ReorderDrag menu = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c3"), v2f32(0, 0), str8_zero(), 0, UIShell_ReorderMode_RightClick, &cfg_nil_node);
  ReorderCheck(menu.menu, "reordered row opens its menu");
  uishell_sidebar_set_order(&state, window, loop, 0, 0);
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c1 c2 c3"), 0), "reset returns data order");
  ReorderCheck(!uishell_sidebar_order_saved(window, loop), "reset forgets the saved run");

  CFG_Node *orders = cfg_node_child_from_string(window, str8_lit("sidebar_order"));
  if(orders != &cfg_nil_node) { cfg_node_release(rd_state->cfg, orders); }
  ws->sidebar = saved;
  uishell_sidebar_release(&state);
  scratch_end(scratch);
  fprintf(stderr, "Sidebar reorder diagnostics: %s (threshold, click, cancel, rows and projects, line, snap back, lift, ghost drop, docking-site drop, save, restore and merge, reset)\n", ok ? "passed" : "FAILED");
  return ok;
}
