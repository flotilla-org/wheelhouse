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
  for(U64 i = 0; i < wheelhouse_snapshot_node_count(state->snapshot); i++)
  {
    WheelhouseNode node = {0}; wheelhouse_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && str8_match(uishell_sidebar_string(node.entity.kind), kind, 0))
    { str8_list_push(arena, &ids, uishell_sidebar_string(node.entity.id)); }
  }
  StringJoin join = {.sep = str8_lit(" ")};
  return str8_list_join(arena, &ids, &join);
}

internal WheelhouseNode
uishell_sidebar_reorder_node(UIShell_SidebarState *state, String8 id)
{
  WheelhouseNode node = {0};
  for(U64 i = 0; i < wheelhouse_snapshot_node_count(state->snapshot); i++)
  {
    wheelhouse_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && str8_match(uishell_sidebar_string(node.entity.id), id, 0)) { return node; }
  }
  return (WheelhouseNode){0};
}

internal U64
uishell_sidebar_reorder_section_count(CFG_Node *window)
{
  return uishell_sidebar_window_locals(window)->section_count;
}

// The group of the local section a drop made (the one after `before`), or 0.
internal UIShell_LocalGroup *
uishell_sidebar_reorder_new_group(CFG_Node *window, U64 before)
{
  UIShell_Locals *locals = uishell_sidebar_window_locals(window);
  return before < locals->section_count && locals->sections[before].group_count ? &locals->sections[before].groups[0] : 0;
}

// Removes a section a test made, and its View.
internal void
uishell_sidebar_reorder_drop_section(CFG_Node *window, UIShell_LocalGroup *group)
{
  if(!group) { return; }
  Temp scratch = scratch_begin(0, 0);
  String8 section = push_str8_copy(scratch.arena, group->section->id);
  CFG_Node *view = uishell_sidebar_local_view(window, section);
  if(view != &cfg_nil_node) { cfg_node_release(rd_state->cfg, view); }
  uishell_sidebar_local_forget_section(window, section);
  scratch_end(scratch);
}

// The first pin of `group` (an id) whose target's id is `entity`, or 0.
internal UIShell_LocalPin *
uishell_sidebar_reorder_pin(CFG_Node *window, String8 group, String8 entity)
{
  UIShell_LocalGroup *g = uishell_sidebar_local_group(window, group);
  for(U64 p = 0; g && p < g->pin_count; p++) { if(str8_match(uishell_sidebar_string(g->pins[p].target.id), entity, 0)) { return &g->pins[p]; } }
  return 0;
}

typedef struct UIShell_ReorderDrag UIShell_ReorderDrag;
struct UIShell_ReorderDrag
{
  B32 started, line, menu, ghost_target, lifted, lift_text_inside;
  Rng2F32 ghost_line;
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
                                F32 to_fraction, UIShell_ReorderMode mode, String8 pinned_area)
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
    else if(frame >= 2 && pinned_area.size) { pointer = offset.x ? offset : center_2f32(pinned_rect); }
    else if(frame >= 2 && !right_click)
    {
      UI_Box *to = to_key.size ? uishell_sidebar_reorder_box(test_ui->root, push_str8f(ui_build_arena(), "###entry_%S", to_key)) : &ui_nil_box;
      pointer = ui_box_is_nil(to) ? add_2f32(start, offset) :
        v2f32(start.x, to->rect.y0+dim_2f32(to->rect).y*to_fraction);
    }
    if(frame == 4 && mode == UIShell_ReorderMode_Dock) { uishell_sidebar_drag_panel_drop(rd_panel_from_cfg(view), Dir2_Down, rd_nil_view_address); }
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
    UIShell_RegsScope(.window = split->owner_cfg->id, .view = rd_view_address_from_cfg(view), .panel = rd_panel_from_cfg(view))
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_sidebar_render(r2f32p(0, 0, 320, 600), split, (UIShell_SidebarRenderParams){UIShell_SidebarRenderMode_SectionPanel, str8_lit("tree")}); }
    // The local group's section, as its View renders it, outside any panel.
    if(pinned_area.size)
    UIShell_RegsScope(.window = split->owner_cfg->id, .view = rd_nil_view_address, .panel = rd_nil_panel_address)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    {
      UIShell_LocalGroup *area = uishell_sidebar_local_group(split->owner_cfg, pinned_area);
      String8 local_key = push_str8f(ui_build_arena(), ".section:%S", area ? area->section->id : str8_zero());
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
      UI_Box *ghost_line = pinned_area.size ?
        ui_box_from_key(ui_key_from_stringf(ui_key_zero(), "group_drop_line_%S", pinned_area)) : &ui_nil_box;
      result.ghost_target = !ui_box_is_nil(ghost_line);
      result.ghost_line = ghost_line->rect;
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
  UIShell_SidebarState state = {.initialized = 1};
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
  state.core = wheelhouse_create(config.str, config.size, &error);
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
    ok &= wheelhouse_apply_patch_json(state.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(patch), 0);
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
  UIShell_ReorderDrag still = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c1"), v2f32(0, 3), str8_zero(), 0, UIShell_ReorderMode_Drag, str8_zero());
  ReorderCheck(!still.started && !still.line, "sub-threshold press stays a click");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c1 c2 c3"), 0), "click keeps order");
  ReorderCheck(state.inspection[0] != 0, "a click activates the row");

  // Esc mid-drag, then a release back over the row: no reorder, no activation.
  state.inspection[0] = 0;
  UIShell_ReorderDrag cancel = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c1"), v2f32(0, 0), str8_lit("c3"), 0.8f, UIShell_ReorderMode_Cancel, str8_zero());
  ReorderCheck(cancel.started, "cancelled drag had started");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c1 c2 c3"), 0), "cancel keeps order");
  ReorderCheck(state.inspection[0] == 0, "release after cancel doesn't activate");

  // Below c3's midpoint lands c1 last; the drag shows the line meanwhile.
  UIShell_ReorderDrag down = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c1"), v2f32(0, 0), str8_lit("c3"), 0.8f, UIShell_ReorderMode_Drag, str8_zero());
  ReorderCheck(down.started, "row drag starts as the shared sidebar drag");
  ReorderCheck(down.line, "insertion line while over a moving gap");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c2 c3 c1"), 0), "drop below c3 reorders");
  String8 loop = uishell_sidebar_loop_key(state.snapshot, 0);
  for(U64 i = 0; i < wheelhouse_snapshot_node_count(state.snapshot); i++)
  {
    WheelhouseNode node = {0}; wheelhouse_snapshot_node(state.snapshot, i, &node);
    if(str8_match(uishell_sidebar_string(node.entity.id), str8_lit("c1"), 0))
    { loop = push_str8_copy(scratch.arena, uishell_sidebar_loop_key(state.snapshot, i)); }
  }
  // The core keeps the run's full order in its dashboard record.
  String8List leaves = {0};
  WheelhouseDashboard *dashboard = uishell_sidebar_dashboard(&state);
  for(U64 o = 0; dashboard && o < dashboard->order_count; o++)
  {
    WheelhouseSiblingOrder *order = (WheelhouseSiblingOrder *)&dashboard->orders[o];
    for(U64 e = 0; e < order->entity_count; e++)
    {
      str8_list_pushf(scratch.arena, &leaves, "%S %S", uishell_sidebar_string(order->entities[e].kind), uishell_sidebar_string(order->entities[e].id));
    }
  }
  StringJoin join = {.sep = str8_lit(" ")};
  ReorderCheck(str8_match(str8_list_join(scratch.arena, &leaves, &join), str8_lit("convoy c2 convoy c3 convoy c1"), 0),
    "the dashboard record keeps the run's full order");
  ReorderCheck(uishell_sidebar_order_saved(&state, loop), "a saved order offers Reset order");

  // A gap that leaves the row where it is shows no line and changes nothing.
  UIShell_ReorderDrag same = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c2"), v2f32(0, 0), str8_lit("c3"), 0.2f, UIShell_ReorderMode_Drag, str8_zero());
  ReorderCheck(same.started && !same.line, "no line over the row's own gaps");
  // Off to the side of the run is no target, so the release snaps back.
  UIShell_ReorderDrag away = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c2"), v2f32(400, 0), str8_zero(), 0, UIShell_ReorderMode_Drag, str8_zero());
  ReorderCheck(away.started && !away.line, "no line outside the run");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c2 c3 c1"), 0), "snap back keeps order");

  // Projects reorder at their own level, keeping their convoys' order.
  UIShell_ReorderDrag up = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("q"), v2f32(0, 0), str8_lit("p"), 0.2f, UIShell_ReorderMode_Drag, str8_zero());
  ReorderCheck(up.line, "line between projects");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, project), str8_lit("q p"), 0), "project drop above p");
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c2 c3 c1"), 0), "convoys unaffected");

  // The record restores into a fresh core before any rows, and new rows
  // merge by data order.
  String8 record = uishell_sidebar_record_get(scratch.arena, state.core, str8_lit("dashboard"));
  UIShell_SidebarState restored = {.initialized = 1};
  error = 0; restored.core = wheelhouse_create(config.str, config.size, &error);
  ok &= uishell_sidebar_result(&restored, restored.core != 0, error);
  if(restored.core)
  {
    error = 0;
    ReorderCheck(uishell_sidebar_result(&restored, wheelhouse_record_put(restored.core, uishell_sidebar_text(str8_lit("dashboard")),
      uishell_sidebar_text(record), &error), error), "the dashboard record imports");
    for(U64 i = 0; i < ArrayCount(entities)+1; i++)
    {
      char **e = i < ArrayCount(entities) ? entities[i] : (char *[]){"convoy", "c4", "p"};
      String8 patch = push_str8f(scratch.arena,
        "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"%s\",\"id\":\"%s\"}},\"source_id\":\"reorder-test\",\"set\":{"
        "\"flotilla.project\":{\"value\":{\"type\":\"text\",\"value\":\"%s\"}}},\"unset\":[]}", e[0], e[1], e[2]);
      ok &= wheelhouse_apply_patch_json(restored.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(patch), 0);
    }
    uishell_sidebar_refresh(&restored);
    ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &restored, convoy), str8_lit("c2 c3 c4 c1"), 0), "restored order merges c4 after c3");
    ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &restored, project), str8_lit("q p"), 0), "restored project order");
  }
  uishell_sidebar_release(&restored);

  // A dragged row lifts and follows the pointer. Over a local group it gets
  // an insertion point between that group's items; the release adds a ghost
  // of the row there, as a row, at that index, and its run keeps its order.
  {
    UIShell_LocalGroup *made = uishell_sidebar_local_new_group(window, str8_lit("Pinned"));
    String8 area = push_str8_copy(scratch.arena, made->id), area_section = push_str8_copy(scratch.arena, made->section->id);
    char *existing[] = {"first-pin", "second-pin"};
    String8 ghosts[2];
    for(U64 i = 0; i < 2; i++)
    {
      UIShell_LocalPin pin = {.ghost = string_from_guid(scratch.arena, make_guid()), .compact = 1,
        .target = uishell_sidebar_entity(uishell_sidebar_host_provider, str8_lit("convoy"), str8_cstring(existing[i]))};
      uishell_sidebar_local_put_pin(&state, &pin, area, i);
      ghosts[i] = pin.ghost;
    }
    uishell_sidebar_publish(&state, &split);
    uishell_sidebar_refresh(&state);
    String8 before = push_str8_copy(scratch.arena, uishell_sidebar_reorder_ids(scratch.arena, &state, convoy));
    // Two compact items below a header: just past the first item's midpoint.
    F32 row = floor_f32(11*2.2f);
    Vec2F32 between = v2f32(160, 620+row+2+row*0.5f+3.f);
    UIShell_ReorderDrag ghost = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c2"), between, str8_zero(), 0, UIShell_ReorderMode_Drag, area);
    UIShell_LocalPin *added = uishell_sidebar_reorder_pin(window, area, str8_lit("c2"));
    U64 pin_count = uishell_sidebar_local_group(window, area) ? uishell_sidebar_local_group(window, area)->pin_count : 0;
    // The group's items, in the order it shows them.
    String8 shown[3] = {0};
    U64 shown_count = 0;
    uishell_sidebar_refresh(&state);
    for(U64 i = 0; i < wheelhouse_snapshot_node_count(state.snapshot); i++)
    {
      WheelhouseNode n = {0}; wheelhouse_snapshot_node(state.snapshot, i, &n);
      if(str8_match(uishell_sidebar_string(n.entity.kind), str8_lit(".ref"), 0) && shown_count < 3)
      { shown[shown_count++] = push_str8_copy(scratch.arena, uishell_sidebar_string(n.entity.id)); }
    }
    ReorderCheck(ghost.started && ghost.lifted, "a dragged row lifts and follows the pointer");
    ReorderCheck(ghost.lift_text_inside, "the lifted copy is the row: its whole icon and label inside it");
    ReorderCheck(ghost.ghost_target && !ghost.line, "a local group shows an insertion point, not the reorder line");
    // Drawn in the section, at the gap after the first item, across the list.
    F32 gap_y = 620+row+2+row;
    ReorderCheck(abs_f32(center_2f32(ghost.ghost_line).y-gap_y) < 3 && ghost.ghost_line.x0 >= 0 && ghost.ghost_line.x1 <= 320 &&
                 dim_2f32(ghost.ghost_line).x > 200, "the insertion line sits at the gap, inside the section");
    ReorderCheck(pin_count == 3 && added && !uishell_sidebar_pin_expanded(added) && shown_count == 3 &&
                 str8_match(shown[0], ghosts[0], 0) && str8_match(shown[1], added->ghost, 0) && str8_match(shown[2], ghosts[1], 0),
                 "the drop adds a ghost of the row, as a row, at the insertion point");
    ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), before, 0), "a ghost drop leaves the run's order");
    // An edge site committed in the release frame wins over the group's claim.
    U64 sections_before = uishell_sidebar_reorder_section_count(window);
    UIShell_ReorderDrag edge = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c2"), between, str8_zero(), 0, UIShell_ReorderMode_Dock, area);
    U64 area_pins = uishell_sidebar_local_group(window, area) ? uishell_sidebar_local_group(window, area)->pin_count : 0;
    UIShell_LocalGroup *split_group = uishell_sidebar_reorder_new_group(window, sections_before);
    ReorderCheck(edge.started && area_pins == 3 && split_group && split_group->pin_count,
                 "an edge site taken in the release frame wins over the group's claim");
    uishell_sidebar_reorder_drop_section(window, split_group);
    uishell_sidebar_manual_sizing(window, 0);
    uishell_sidebar_local_forget_section(window, area_section);
  }

  // An empty local section opens a row while a drag is over it, so it still
  // shows an insertion line and takes the drop.
  {
    UIShell_LocalGroup *made = uishell_sidebar_local_new_group(window, str8_lit("Empty"));
    String8 area = push_str8_copy(scratch.arena, made->id), area_section = push_str8_copy(scratch.arena, made->section->id);
    uishell_sidebar_publish(&state, &split);
    uishell_sidebar_refresh(&state);
    UIShell_ReorderDrag empty = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c2"), v2f32(160, 640), str8_zero(), 0, UIShell_ReorderMode_Drag, area);
    UIShell_LocalGroup *group = uishell_sidebar_local_group(window, area);
    UIShell_LocalPin *pin = group && group->pin_count ? &group->pins[0] : 0;
    ReorderCheck(empty.ghost_target && dim_2f32(empty.ghost_line).x > 200 && empty.ghost_line.y0 > 620,
                 "an empty section shows an insertion line in its opened row");
    ReorderCheck(pin && str8_match(uishell_sidebar_string(pin->target.id), str8_lit("c2"), 0),
                 "a drop into an empty section adds the row's ghost");
    uishell_sidebar_manual_sizing(window, 0);
    uishell_sidebar_local_forget_section(window, area_section);
  }

  // A row dropped on a docking site makes a new pinned area there holding a
  // ghost of the row, as a row: the same creation drag as a card's.
  {
    String8 before = push_str8_copy(scratch.arena, uishell_sidebar_reorder_ids(scratch.arena, &state, convoy));
    U64 sections_before = uishell_sidebar_reorder_section_count(window);
    UIShell_ReorderDrag dock = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c3"), v2f32(0, 400), str8_zero(), 0, UIShell_ReorderMode_Dock, str8_zero());
    UIShell_LocalGroup *area = uishell_sidebar_reorder_new_group(window, sections_before);
    UIShell_LocalPin *pin = area && area->pin_count ? &area->pins[0] : 0;
    ReorderCheck(dock.started && !dock.line, "a row drag away from its run claims no reorder");
    ReorderCheck(pin && str8_match(uishell_sidebar_string(pin->target.id), str8_lit("c3"), 0) &&
                 !uishell_sidebar_pin_expanded(pin) && uishell_sidebar_local_view(window, area->section->id) != &cfg_nil_node,
                 "a docking-site drop makes a section, in a View there, whose group holds the row's ghost, as a row");
    ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), before, 0), "a docking-site drop leaves the run's order");
    uishell_sidebar_reorder_drop_section(window, area);
    uishell_sidebar_manual_sizing(window, 0);
  }

  // Labels reach the core intact, quotes, backslashes and all.
  {
    UIShell_LocalGroup *quoted = uishell_sidebar_local_new_group(window, str8_lit("Say \"hi\" \\ there"));
    String8 id = push_str8_copy(scratch.arena, quoted->section->id);
    uishell_sidebar_publish(&state, &split);
    uishell_sidebar_refresh(&state);
    U64 published = 0;
    String8 label = str8_zero();
    for(U64 i = 0; i < wheelhouse_snapshot_node_count(state.snapshot); i++)
    {
      WheelhouseNode n = {0}; wheelhouse_snapshot_node(state.snapshot, i, &n);
      if(!str8_match(uishell_sidebar_string(n.entity.kind), str8_lit(".section"), 0) || !str8_match(uishell_sidebar_string(n.entity.id), id, 0)) { continue; }
      published++;
      label = push_str8_copy(scratch.arena, uishell_sidebar_string(n.label));
    }
    ReorderCheck(published == 1 && str8_match(label, str8_lit("Say \"hi\" \\ there"), 0),
                 "a label with quotes and a backslash reaches the core intact");
    uishell_sidebar_local_forget_section(window, id);
    uishell_sidebar_publish(&state, &split);
  }

  // A saved pinned area migrates in place, in a panel of its own in the
  // dock: its View becomes its section's View, keeping its label, selection
  // and collapse, and its pins move into the section's one group.
  {
    RD_Arrangement *dock = rd_arrangement_from_panel_owner(scratch.arena, rd_panel_owner_from_host(RD_PanelOwnerKind_Sidebar, window));
    RD_PanelID panel = rd_arrangement_add(dock, dock->root->id, 0.125f);
    CFG_Node *old_area = rd_cfg_from_view(rd_new_view_tab(dock, panel, str8_lit("pinned_cards"), str8_zero()));
    rd_arrangement_set(dock);
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, old_area, str8_lit("label")), str8_lit("Docked pins"));
    cfg_node_new(rd_state->cfg, old_area, str8_lit("section_collapsed"));
    CFG_Node *card = cfg_node_new(rd_state->cfg, old_area, str8_lit("card"));
    char *fields[][2] = {{"ghost", "docked-pin"}, {"kind", "convoy"}, {"entity", "c1"}};
    for(U64 f = 0; f < ArrayCount(fields); f++)
    { cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, card, str8_cstring(fields[f][0])), str8_cstring(fields[f][1])); }
    cfg_node_new(rd_state->cfg, card, str8_lit("compact"));
    uishell_sidebar_pin_migrate(window);
    RD_ArrangementPanel *migrated_panel = rd_arrangement_panel_from_id(rd_arrangement_live(dock->owner), panel);
    CFG_Node *migrated = migrated_panel->tab_count == 1 ? rd_cfg_from_view(migrated_panel->first_tab->view) : &cfg_nil_node;
    UIShell_LocalGroup *group = uishell_sidebar_local_view_group(window, migrated);
    UIShell_LocalPin *pin = group && group->pin_count == 1 ? &group->pins[0] : 0;
    ReorderCheck(str8_match(migrated->string, str8_lit("sidebar_section"), 0) &&
                 rd_view_address_match(migrated_panel->selected, rd_view_address_from_cfg(migrated)) &&
                 uishell_sidebar_section_collapsed(migrated) &&
                 group && str8_match(uishell_sidebar_local_title(scratch.arena, group->section), str8_lit("Docked pins"), 0) &&
                 pin && str8_match(pin->ghost, str8_lit("docked-pin"), 0) && str8_match(uishell_sidebar_string(pin->target.id), str8_lit("c1"), 0) &&
                 !uishell_sidebar_pin_expanded(pin),
                 "a docked pinned area migrates in place into a section and group, keeping its pins and state");
    if(group) { uishell_sidebar_local_forget_section(window, push_str8_copy(scratch.arena, group->section->id)); }
    rd_fixture_remove_panel(rd_panel_address(dock->owner, panel));
  }

  // A reordered run's row offers Reset order, which returns it to data order.
  UIShell_ReorderDrag menu = uishell_sidebar_reorder_gesture(ws, &split, view, &state, str8_lit("c3"), v2f32(0, 0), str8_zero(), 0, UIShell_ReorderMode_RightClick, str8_zero());
  ReorderCheck(menu.menu, "reordered row opens its menu");
  uishell_sidebar_set_order(&state, loop, 0, 0);
  ReorderCheck(str8_match(uishell_sidebar_reorder_ids(scratch.arena, &state, convoy), str8_lit("c1 c2 c3"), 0), "reset returns data order");
  ReorderCheck(!uishell_sidebar_order_saved(&state, loop), "reset forgets the saved run");

  ws->sidebar = saved;
  uishell_sidebar_release(&state);
  scratch_end(scratch);
  fprintf(stderr, "Sidebar reorder diagnostics: %s (threshold, click, cancel, rows and projects, line, snap back, lift, ghost drop, docking-site drop, save, restore and merge, reset)\n", ok ? "passed" : "FAILED");
  return ok;
}
