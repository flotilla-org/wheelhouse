internal void uishell_sidebar_manual_sizing(CFG_Node *window, B32 manual);
// Detached presentation belongs to the card controller. Pinned areas are
// ordinary registered Views in the RAD-derived panel tree, never KDL edits.
internal B32
uishell_sidebar_card_target_valid(RD_WindowState *ws, F32 width)
{
  // A card shows a sidebar section wherever it is: floating or pinned, it
  // takes what the sidebar takes.
  RD_DockProposal p = {0};
  p.host.kind = RD_DockHostKind_Sidebar;
  p.host.level = p.host.controlled_split = ws->cfg_id;
  p.host.available_width = width;
  p.view_level = ws->cfg_id;
  p.instances_after = p.control_surfaces_after = 1;
  return rd_dock_check(rd_dock_view_from_name(str8_lit("sidebar_section")), p) == RD_DockRule_Valid;
}

internal void
uishell_sidebar_card_request(UIShell_HoverCard *card, UIShell_CardPlacement placement)
{
  card->requested = placement;
  card->move_requested = 1;
  rd_request_frame();
}

internal void
uishell_sidebar_drag_panel_drop(RD_PanelAddress destination, Dir2 direction, RD_ViewAddress previous_tab)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(cfg_node_from_id(rd_state->drag_drop_regs->window));
  if(ws == &rd_nil_window_state || !ws->sidebar || (!ws->sidebar->drag_card && !ws->sidebar->row_drag_key.size)) { return; }
  ws->sidebar->drop_panel = destination;
  ws->sidebar->drop_direction = direction;
  ws->sidebar->drop_previous_tab = previous_tab;
}

// Every sidebar drag, of a card or a row, is one creation drag of a pinned
// area: RAD's docking sites can make a new area, and pinned areas and the
// row's sibling run claim positioned drops. uishell_sidebar_drag_finish
// resolves the release.
internal void
uishell_sidebar_drag_begin(RD_WindowState *ws)
{
  ws->sidebar->drop_panel = rd_nil_panel_address; ws->sidebar->drop_previous_tab = rd_nil_view_address; ws->sidebar->reorder_build = 0; ws->sidebar->group_claim_build = 0;
  UIShell_RegsScope(.window = ws->cfg_id, .view = rd_nil_view_address, .panel = rd_nil_panel_address, .tab = rd_nil_view_address) { rd_drag_begin(UIShell_ContextRegSlot_View); }
  rd_state->drag_drop_creation_name = str8_lit("sidebar_section");
  rd_state->drag_drop_commit = uishell_sidebar_drag_panel_drop;
}

// A card drags from its title line, past the shared threshold
// (drag-model.md, decision 4). Card focus is decided from raw press events
// over the card's rect, so a clickable title does not change it.
internal void
uishell_sidebar_card_drag_from(UIShell_HoverCard *card, UI_Signal drag)
{
  if(ui_pressed(drag)) { card->move_origin = card->rect.p0; }
  if(ui_dragging(drag) && length_2f32(ui_drag_delta()) > UIShell_DragThresholdPT)
  {
    if(!card->moving && !rd_drag_is_active())
    {
      RD_WindowState *ws = rd_window_state_from_os_handle(ui_state->window);
      if(ws != &rd_nil_window_state && ws->sidebar)
      {
        ws->sidebar->drag_card = card;
        uishell_sidebar_drag_begin(ws);
      }
    }
    card->moving = 1;
    Vec2F32 size = dim_2f32(card->rect), pos = add_2f32(card->move_origin, ui_drag_delta());
    card->rect = r2f32p(pos.x, pos.y, pos.x+size.x, pos.y+size.y);
    rd_request_frame();
  }
  if(ui_released(drag) && card->moving)
  { card->drag_released = 1; rd_request_frame(); }
}

// The card's title line, drawn like the label it replaces.
internal void
uishell_sidebar_card_title_handle(UIShell_HoverCard *card, String8 text)
{
  UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|UI_BoxFlag_MouseClickable, "%S###card_title", text);
  uishell_sidebar_card_drag_from(card, ui_signal_from_box(box));
}

internal UIShell_LocalGroup *uishell_sidebar_pin_target(CFG_Node *window);

// A pinned card's pin, by its ghost id; empty for any other card.
internal String8
uishell_sidebar_card_pin_id(UIShell_HoverCard *card)
{
  return str8(card->pin, card->pin_size);
}

// Makes a card the card of pin `ghost` (empty: of none). A ghost id too long
// to keep (every one Wheelhouse makes fits) leaves it of none.
internal void
uishell_sidebar_card_set_pin(UIShell_HoverCard *card, String8 ghost)
{
  card->pin_size = ghost.size <= sizeof(card->pin) ? ghost.size : 0;
  MemoryCopy(card->pin, ghost.str, card->pin_size);
}

// Pin reveals the subject's pin in the group Pin uses (the topmost showing
// one) rather than duplicating it, or adds one there.
internal void
uishell_sidebar_card_pin_control(UIShell_HoverCard *card)
{
  RD_WindowState *ws = rd_window_state_from_os_handle(ui_state->window);
  B32 pinned = 0;
  if(ws != &rd_nil_window_state && card->depth)
  {
    UIShell_LocalGroup *target = uishell_sidebar_pin_target(cfg_node_from_id(ws->cfg_id));
    for(U64 p = 0; target && p < target->pin_count && !pinned; p++)
    { pinned = uishell_sidebar_card_entity_match(uishell_sidebar_pin_entity(&target->pins[p]), card->path[card->depth-1]); }
  }
  UI_Signal sig = uishell_sidebar_header_button(str8_lit("🖈"), str8_lit("card_pin"), 0, 1,
    pinned ? str8_lit("Show pin") : str8_lit("Pin"), pinned ? str8_lit("Already pinned in the sidebar") : str8_zero());
  if(ui_clicked(sig)) { uishell_sidebar_card_request(card, UIShell_CardPlacement_Pinned); }
}

internal UIShell_HoverCard *
uishell_sidebar_detached_alloc(RD_WindowState *ws)
{
  // Window-arena slots track the peak simultaneous detached-card count and
  // are reused thereafter. Keep them uncapped so opening a card never evicts one.
  UIShell_HoverCard *c = ws->sidebar->detached;
  for(; c; c = c->next)
  {
    if(c->open || c->pin_size) { continue; }
    // A closed float can still protect its painted bounds during this build.
    // Recycling it would zero mask.next and truncate the live mask chain.
    B32 linked = 0;
    for(UI_HoverCardMask *m = ws->ui->hover_card_extra; m; m = m->next)
    { linked |= m == &c->mask || m == &c->cap; }
    if(!linked) { break; }
  }
  if(!c)
  {
    c = push_array(ws->arena, UIShell_HoverCard, 1);
    c->next = ws->sidebar->detached; ws->sidebar->detached = c;
  }
  // Reused slots are newly opened cards too. Put them at the front so the
  // newest float is both painted and hit-tested above older floats.
  if(c != ws->sidebar->detached)
  {
    UIShell_HoverCard *previous = ws->sidebar->detached;
    while(previous->next != c) { previous = previous->next; }
    previous->next = c->next;
    c->next = ws->sidebar->detached;
    ws->sidebar->detached = c;
  }
  UIShell_HoverCard *next = c->next;
  if(c->arena) { arena_release(c->arena); }
  if(c->label_arena) { arena_release(c->label_arena); }
  MemoryZeroStruct(c); c->next = next;
  return c;
}

internal UIShell_HoverCard *
uishell_sidebar_detached_copy(RD_WindowState *ws, UIShell_HoverCard *source)
{
  UIShell_HoverCard *c = uishell_sidebar_detached_alloc(ws);
  c->arena = arena_alloc();
  c->capacity = Max(UIShell_HoverCardInitialPathCapacity, source->depth);
  c->path = push_array(c->arena, WheelhouseEntity, c->capacity);
  c->depth = source->depth;
  for(U64 i = 0; i < c->depth; i++) { c->path[i] = uishell_sidebar_card_entity_copy(c->arena, source->path[i]); }
  c->source_key = push_str8_copy(c->arena, source->source_key);
  uishell_sidebar_card_retain_label(c, source->retained_label);
  c->source = source->source; c->source_rect = source->source_rect; c->rect = source->rect;
  c->open = c->engaged = 1; c->focused = source->focused; c->enriched = source->enriched;
  return c;
}

// Pins (ghosts) live in the Dashboard's local groups; a pin missing fields
// still shows, with its retained label or as missing. The first pin of
// `entity`, or 0.
internal UIShell_LocalPin *
uishell_sidebar_pin_find(CFG_Node *window, WheelhouseEntity entity)
{
  UIShell_Locals *locals = uishell_sidebar_window_locals(window);
  for(U64 s = 0; s < locals->section_count; s++)
  for(U64 g = 0; g < locals->sections[s].group_count; g++)
  for(U64 p = 0; p < locals->sections[s].groups[g].pin_count; p++)
  {
    UIShell_LocalPin *pin = &locals->sections[s].groups[g].pins[p];
    if(uishell_sidebar_card_entity_match(uishell_sidebar_pin_entity(pin), entity)) { return pin; }
  }
  return 0;
}

// Where Pin puts a pin: the first group, other than the default one, of the
// topmost local section that is showing (its View open and its panel's
// selected tab), in docking order. 0 when none is showing.
internal UIShell_LocalGroup *uishell_sidebar_local_view_group(CFG_Node *window, CFG_Node *view);

internal UIShell_LocalGroup *
uishell_sidebar_pin_target(CFG_Node *window)
{
  RD_Arrangement *sidebar = rd_arrangement_live(rd_panel_owner_from_host(RD_PanelOwnerKind_Sidebar, window));
  UIShell_LocalGroup *result = 0;
  for(RD_ArrangementPanel *p = sidebar->root; p != &rd_nil_arrangement_panel && !result; p = rd_arrangement_next(sidebar->root, p))
  {
    CFG_Node *v = rd_cfg_from_view(p->selected);
    if(!str8_match(v->string, str8_lit("sidebar_section"), 0)) { continue; }
    UIShell_LocalGroup *first = uishell_sidebar_local_view_group(window, v);
    for(U64 g = 0; first && g < first->section->group_count && !result; g++)
    { if(!first->section->groups[g].is_default) { result = &first->section->groups[g]; } }
  }
  return result;
}

internal UIShell_LocalSection *uishell_sidebar_local_new_section(CFG_Node *window);
internal UIShell_LocalGroup *uishell_sidebar_local_new_group(CFG_Node *window, String8 label);
internal UIShell_LocalGroup *uishell_sidebar_local_add_group(CFG_Node *window, String8 section_id, String8 label);

// Saved pinned areas (pinned_cards Views) become local sections: each a
// section holding one group with the area's pins, keeping their ghost ids
// and forms, and its View a section View showing it in the same place
// (drag-model.md, "Migration"). A copied layout's repeated ghost id keeps
// the first; of pins saved before ghost ids, the first per entity is kept
// and given one.
// Best-effort: unknown children are dropped. A pinned area saved outside
// any panel goes to the sidebar's first.
internal void
uishell_sidebar_pin_migrate(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_SidebarDocks docks = uishell_sidebar_docks(scratch.arena, window);
  CFG_NodePtrList collapsed = {0};
  for(UIShell_SidebarDockView *saved = docks.first; saved != 0; saved = saved->next)
  {
    CFG_Node *v = saved->node;
    if(!str8_match(v->string, str8_lit("pinned_cards"), 0)) { continue; }
    String8 label = cfg_node_child_from_string(v, str8_lit("label"))->first->string;
    UIShell_LocalGroup *group = uishell_sidebar_local_new_group(window, label.size ? label : str8_lit("Pinned"));
    if(!group) { continue; }
    String8 group_id = push_str8_copy(scratch.arena, group->id), section_id = push_str8_copy(scratch.arena, group->section->id);
    RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(saved->arrangement, saved->panel);
    B32 selected = rd_view_address_match(panel->selected, saved->view);
    CFG_Node *view = cfg_node_new(rd_state->cfg, rd_view_namespace(window, RD_ViewOwnerKind_Dashboard), str8_lit("sidebar_section"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("section")), uishell_sidebar_local_key(scratch.arena, section_id));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("label")), uishell_sidebar_local_title(scratch.arena, group->section));
    // The area's own state: its collapsed header, which its panel takes once
    // it is placed. Its other children were its label and selection, carried
    // above; a pin's form travels with it.
    if(cfg_node_child_from_string(v, str8_lit("section_collapsed")) != &cfg_nil_node)
    { cfg_node_ptr_list_push(scratch.arena, &collapsed, view); }
    U64 index = 0;
    String8List legacy = {0};
    for(CFG_Node *c = v->first; c != &cfg_nil_node; c = c->next)
    {
      if(!str8_match(c->string, str8_lit("card"), 0)) { continue; }
      UIShell_LocalPin pin = {0};
      pin.ghost = cfg_node_child_from_string(c, str8_lit("ghost"))->first->string;
      String8 provider = cfg_node_child_from_string(c, str8_lit("provider"))->first->string;
      pin.target = uishell_sidebar_entity(provider.size ? provider : uishell_sidebar_host_provider,
        cfg_node_child_from_string(c, str8_lit("kind"))->first->string, cfg_node_child_from_string(c, str8_lit("entity"))->first->string);
      if(pin.ghost.size && uishell_sidebar_pin_by_ghost(window, pin.ghost)) { continue; }
      if(!pin.ghost.size)
      {
        String8 entry = push_str8f(scratch.arena, "%S/%S/%S", provider, uishell_sidebar_string(pin.target.kind), uishell_sidebar_string(pin.target.id));
        if(uishell_sidebar_local_seen(&legacy, entry)) { continue; }
        str8_list_push(scratch.arena, &legacy, entry);
        pin.ghost = string_from_guid(scratch.arena, make_guid());
      }
      pin.label = cfg_node_child_from_string(c, str8_lit("label"))->first->string;
      pin.source = cfg_node_child_from_string(c, str8_lit("source"))->first->string;
      pin.compact = cfg_node_child_from_string(c, str8_lit("compact")) != &cfg_nil_node;
      uishell_sidebar_local_put_pin(state, &pin, group_id, index++);
    }
    // The section View takes the area's place, after it in its panel.
    rd_arrangement_insert_tab(saved->arrangement, rd_view_address_from_cfg(view), panel->id, saved->view, selected);
    uishell_sidebar_docks_remove(&docks, saved);
  }
  uishell_sidebar_docks_set(&docks);
  for(CFG_NodePtrNode *n = collapsed.first; n != 0; n = n->next) { uishell_sidebar_section_set_collapsed(n->v, 1); }
  scratch_end(scratch);
}

// Moves pin `ghost` into `group` (from wherever it is), before the pin at
// `index`, or last. `index` counts the group's pins as the insertion line
// does, the pin moved included, so a pin moved down lands where the line
// showed.
internal void
uishell_sidebar_pin_place(CFG_Node *window, String8 ghost, String8 group_id, U64 index)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_Locals *before = uishell_sidebar_locals(state);
  UIShell_LocalGroup *group = uishell_sidebar_local_group(window, group_id);
  if(!group || !uishell_sidebar_pin_by_ghost(window, ghost)) { return; }
  Temp scratch = scratch_begin(0, 0);
  String8 *order = push_array(scratch.arena, String8, group->pin_count+1);
  String8 at = index < group->pin_count ? group->pins[index].ghost : str8_zero();
  U64 count = 0;
  for(U64 p = 0; p < group->pin_count; p++)
  {
    // Before itself, it stays where it is.
    B32 self = str8_match(group->pins[p].ghost, ghost, 0);
    if(self && !str8_match(at, ghost, 0)) { continue; }
    if(!self && at.size && str8_match(group->pins[p].ghost, at, 0)) { order[count++] = ghost; }
    order[count++] = group->pins[p].ghost;
  }
  if(!at.size) { order[count++] = ghost; }
  uishell_sidebar_local_order_pins(state, group->id, order, count);
  uishell_sidebar_local_settle(state, before);
  scratch_end(scratch);
}

// Removes pin `ghost`.
internal void
uishell_sidebar_pin_remove(CFG_Node *window, String8 ghost)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_Locals *before = uishell_sidebar_locals(state);
  if(!uishell_sidebar_pin_by_ghost(window, ghost)) { return; }
  uishell_sidebar_local_remove(state, str8_lit(".ref"), ghost);
  uishell_sidebar_local_settle(state, before);
}

// Sets pin `ghost`'s own facts, as `values` gives them (its ghost, group and
// position are ignored).
internal void
uishell_sidebar_pin_update(CFG_Node *window, String8 ghost, UIShell_LocalPin values)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  UIShell_LocalPin *pin = uishell_sidebar_pin_by_ghost(window, ghost);
  if(!pin) { return; }
  values.ghost = pin->ghost;
  uishell_sidebar_local_put_pin(state, &values, pin->group->id, pin - pin->group->pins);
}

// A sidebar View may claim a positioned drop unless a docking site has the
// pointer: its panel's edge pills and tab strip sit outside its body
// (drag-model.md, "Drop-target visuals").
internal B32
uishell_sidebar_drop_claimable(void)
{
  return ui_key_match(ui_drop_hot_key(), ui_key_zero());
}

// Adds a ghost of `entity` (a new pin, with a ghost id of its own) to local
// group `group` before the pin at `index`, or last, and reveals it. A
// dropped row becomes a row (`compact`); a pinned card stays a card.
internal UIShell_LocalPin *
uishell_sidebar_pin_add(RD_WindowState *ws, String8 group, U64 index, WheelhouseEntity entity,
                        String8 fallback_label, String8 source, B32 compact)
{
  UIShell_SidebarState *state = ws->sidebar;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  UIShell_LocalGroup *area = uishell_sidebar_local_group(window, group);
  if(!area) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarNode node = {0};
  UIShell_LocalPin pin = {0};
  pin.ghost = string_from_guid(scratch.arena, make_guid());
  pin.target = entity;
  pin.label = uishell_sidebar_card_find(state, entity, &node) != WHEELHOUSE_NONE ? uishell_sidebar_string(node.label) : fallback_label;
  pin.source = source;
  pin.compact = compact;
  uishell_sidebar_local_put_pin(state, &pin, area->id, area->pin_count);
  uishell_sidebar_pin_place(window, pin.ghost, area->id, index);
  state->pin_reveal = u64_hash_from_str8(pin.ghost);
  UIShell_LocalPin *added = uishell_sidebar_pin_by_ghost(window, pin.ghost);
  scratch_end(scratch);
  return added;
}

internal UIShell_LocalGroup *uishell_sidebar_pin_area(RD_WindowState *ws, B32 new_area);
internal CFG_Node *uishell_sidebar_local_view(CFG_Node *window, String8 section);
internal CFG_Node *uishell_sidebar_section_view_new(CFG_Node *window, RD_Arrangement *arrangement, RD_PanelID panel, RD_ViewAddress prev, String8 key, String8 label);
internal RD_ViewAddress uishell_sidebar_last_tab(RD_Arrangement *arrangement, RD_PanelID panel);
internal void uishell_sidebar_select_view(CFG_Node *view);

// Pins a card; its pin, or 0.
internal UIShell_LocalPin *
uishell_sidebar_card_pin(RD_WindowState *ws, UIShell_HoverCard *card, B32 new_area)
{
  UIShell_SidebarState *state = ws->sidebar;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  WheelhouseEntity entity = card->path[card->depth-1];
  // A pinned card moves its own ghost. Pin on any other card reveals the
  // entity's ghost in the group Pin uses (the topmost showing one), or adds
  // one there; a drop is explicit placement, so it always adds one.
  UIShell_LocalPin *saved = uishell_sidebar_pin_by_ghost(window, uishell_sidebar_card_pin_id(card));
  UIShell_LocalPin *reveal = saved;
  UIShell_LocalGroup *target = uishell_sidebar_pin_target(window);
  for(U64 p = 0; target && !reveal && p < target->pin_count; p++)
  { if(uishell_sidebar_card_entity_match(uishell_sidebar_pin_entity(&target->pins[p]), entity)) { reveal = &target->pins[p]; } }
  if(reveal && !new_area)
  {
    saved = reveal;
    // Revealing an existing tab changes selection, not placement: clear its
    // siblings (including non-pinned tabs) without a creation/close check.
    CFG_Node *view = uishell_sidebar_local_view(window, saved->group->section->id);
    state->pin_reveal = u64_hash_from_str8(saved->ghost);
    if(view != &cfg_nil_node)
    {
      uishell_sidebar_select_view(view);
      uishell_sidebar_section_set_collapsed(view, 0);
    }
    for(UIShell_HoverCard *c = state->detached; c; c = c->next)
    { if(str8_match(uishell_sidebar_card_pin_id(c), saved->ghost, 0)) { c->scroll = 0; c->focused = 1; } }
    return saved;
  }
  UIShell_LocalGroup *area = uishell_sidebar_pin_area(ws, new_area);
  if(!area) { return 0; }
  if(saved)
  {
    // A center drop onto its current area reveals the pin in place.
    Temp scratch = scratch_begin(0, 0);
    String8 ghost = push_str8_copy(scratch.arena, saved->ghost);
    if(!str8_match(saved->group->id, area->id, 0)) { uishell_sidebar_pin_place(window, ghost, area->id, max_U64); }
    state->pin_reveal = u64_hash_from_str8(ghost);
    saved = uishell_sidebar_pin_by_ghost(window, ghost);
    scratch_end(scratch);
    return saved;
  }
  return uishell_sidebar_pin_add(ws, area->id, max_U64, entity, card->retained_label, card->source_key, 0);
}

// The section View local section `section` shows in, if it is open.
internal CFG_Node *uishell_sidebar_region_view(CFG_Node *owner, String8 key);

internal CFG_Node *
uishell_sidebar_local_view(CFG_Node *window, String8 section)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *view = uishell_sidebar_region_view(window, uishell_sidebar_local_key(scratch.arena, section));
  scratch_end(scratch);
  return view;
}

// The first group of the local section a View shows, or 0.
internal UIShell_LocalGroup *
uishell_sidebar_local_view_group(CFG_Node *window, CFG_Node *view)
{
  if(!str8_match(view->string, str8_lit("sidebar_section"), 0)) { return 0; }
  String8 key = cfg_node_child_from_string(view, str8_lit("section"))->first->string;
  UIShell_LocalSection *section = uishell_sidebar_local_section(window, uishell_sidebar_local_key_id(key));
  return section && section->group_count ? &section->groups[0] : 0;
}

// A new, empty local section, last among the sidebar's (after the default
// one, made first if need be).
internal UIShell_LocalSection *
uishell_sidebar_local_new_section(CFG_Node *window)
{
  UIShell_SidebarState *state = uishell_sidebar_window_state(window);
  uishell_sidebar_local_ensure_default(state);
  UIShell_Locals *locals = uishell_sidebar_locals(state);
  Temp scratch = scratch_begin(0, 0);
  String8 id = string_from_guid(scratch.arena, make_guid());
  U64 position = locals->section_count ? locals->sections[locals->section_count-1].position+1 : 0;
  uishell_sidebar_local_put_section(state, id, str8_lit("Section"), position);
  UIShell_LocalSection *section = uishell_sidebar_local_section(window, id);
  scratch_end(scratch);
  return section;
}

// A new local section holding one group named `label`, whose name the
// section borrows (uishell_sidebar_local_title); returns the group.
internal UIShell_LocalGroup *
uishell_sidebar_local_new_group(CFG_Node *window, String8 label)
{
  UIShell_LocalSection *section = uishell_sidebar_local_new_section(window);
  return section ? uishell_sidebar_local_add_group(window, section->id, label) : 0;
}

// A selected section View showing local section `section`, in `panel`,
// after `prev` (nil: first).
internal CFG_Node *
uishell_sidebar_local_new_view_after(RD_PanelAddress panel, String8 section, CFG_Node *prev)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *window = rd_window_from_cfg(rd_cfg_from_panel_owner(panel.owner));
  UIShell_LocalSection *local = uishell_sidebar_local_section(window, section);
  String8 key = uishell_sidebar_local_key(scratch.arena, section);
  String8 label = local ? uishell_sidebar_local_title(scratch.arena, local) : str8_lit("Section");
  RD_Arrangement *arrangement = rd_arrangement_from_panel_owner(scratch.arena, panel.owner);
  CFG_Node *view = &cfg_nil_node;
  if(rd_arrangement_panel_from_id(arrangement, panel.id) != &rd_nil_arrangement_panel)
  {
    view = uishell_sidebar_section_view_new(window, arrangement, panel.id, rd_view_address_from_cfg(prev), key, label);
    rd_arrangement_set(arrangement);
  }
  scratch_end(scratch);
  return view;
}

// The same, after `panel`'s last tab.
internal CFG_Node *
uishell_sidebar_local_new_view(RD_PanelAddress panel, String8 section)
{
  CFG_Node *prev = rd_cfg_from_view(uishell_sidebar_last_tab(rd_arrangement_live(panel.owner), panel.id));
  return uishell_sidebar_local_new_view_after(panel, section, prev);
}

// The local group a pin goes to (drag-model.md, "Sections and groups as
// data"). With `new_area`, the one at the drag's docking site: a centre drop
// joins the first group of a section shown there; otherwise a new section
// and group, shown in a View at the site. Without, the group Pin uses (the
// topmost showing one, uishell_sidebar_pin_target), or a new "Pinned"
// section at the top of the sidebar. 0 when the destination can't take one.
internal UIShell_LocalGroup *
uishell_sidebar_pin_area(RD_WindowState *ws, B32 new_area)
{
  UIShell_SidebarState *state = ws->sidebar;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  RD_PanelAddress destination = state->drop_panel;
  UIShell_LocalGroup *area = new_area ? 0 : uishell_sidebar_pin_target(window);
  if(new_area && rd_panel_exists(destination))
  {
    if(!rd_dock_can_create(str8_lit("sidebar_section"), rd_dock_host_from_panel(destination, RD_DOCK_UNMEASURED_WIDTH))) { return 0; }
    if(state->drop_direction == Dir2_Invalid)
    {
      RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(rd_arrangement_live(destination.owner), destination.id);
      for(RD_ArrangementTab *t = panel->first_tab; t && !area; t = t->next)
      { area = uishell_sidebar_local_view_group(window, rd_cfg_from_view(t->view)); }
    }
    if(!area)
    {
      area = uishell_sidebar_local_new_group(window, str8_lit("Pinned"));
      if(!area) { return 0; }
      CFG_Node *view = uishell_sidebar_local_new_view(destination, area->section->id);
      if(state->drop_direction != Dir2_Invalid)
      {
        uishell_cmd("split_panel", .window = ws->cfg_id, .dst_panel = destination,
          .panel = destination, .view = rd_view_address_from_cfg(view), .dir2 = state->drop_direction);
      }
    }
    // Keep the allocation selected by the same split geometry shown during drag.
    uishell_sidebar_manual_sizing(window, 1);
  }
  if(!area)
  {
    // Validate a tentative destination before lifting tabs or changing ratios.
    RD_PanelOwner sidebar_owner = rd_panel_owner_from_host(RD_PanelOwnerKind_Sidebar, window);
    if(!rd_dock_can_create(str8_lit("sidebar_section"), rd_dock_host_from_owner(sidebar_owner, RD_DOCK_UNMEASURED_WIDTH))) { return 0; }
    // An equal share of the sidebar (a sidebar merged into one panel keeps
    // its tabs in a panel of their own), before a requested panel, else at
    // the top, where a new pin is seen.
    area = uishell_sidebar_local_new_group(window, str8_lit("Pinned"));
    if(!area) { return 0; }
    Temp scratch = scratch_begin(0, 0);
    RD_Arrangement *sidebar = rd_arrangement_from_panel_owner(scratch.arena, sidebar_owner);
    RD_PanelID panel = rd_arrangement_add(sidebar, sidebar->root->id, 0);
    RD_ArrangementPanel *before = rd_arrangement_panel_from_id(sidebar, state->pin_before);
    rd_arrangement_reorder(sidebar, panel, before->parent == sidebar->root ? before->prev->id : 0);
    uishell_sidebar_section_view_new(window, sidebar, panel, rd_nil_view_address, uishell_sidebar_local_key(scratch.arena, area->section->id),
                                     uishell_sidebar_local_title(scratch.arena, area->section));
    rd_arrangement_set(sidebar);
    scratch_end(scratch);
  }
  return area;
}

internal size_t
uishell_sidebar_detached_content(UIShell_SidebarState *state, RD_WindowState *ws, UIShell_HoverCard *card,
                                U64 slot, UIShell_SidebarNode node, U64 index, F32 width, B32 interactive)
{
  if(index != WHEELHOUSE_NONE)
  {
    String8 label = uishell_sidebar_string(node.label);
    uishell_sidebar_card_retain_label(card, label);
    return uishell_sidebar_card_content(state, ws, card, slot, node, index, width, interactive);
  }
  UI_Row UI_FontSize(floor_f32(ui_top_font_size()*0.82f)) UI_TagF("weak") RD_Font(RD_FontSlot_Main)
  UI_PrefWidth(ui_px(width, 1))
  { uishell_sidebar_card_title_handle(card, card->retained_label); }
  UI_TagF("weak") { ui_label(str8_lit("No longer present")); }
  ui_label_multiline(width, uishell_sidebar_string(card->path[card->depth-1].id));
  return WHEELHOUSE_NONE;
}

internal UIShell_CardPlacement
uishell_sidebar_card_drag_target(RD_WindowState *ws, UIShell_HoverCard *card, Vec2F32 mouse)
{
  if(!contains_2f32(ws->sidebar->rect, mouse)) { return UIShell_CardPlacement_Float; }
  UI_Box *source = ui_box_from_key(card->source);
  if(card->source_key.size && !ui_box_is_nil(source) &&
     source->last_touched_build_index+1 >= ui_state->build_index)
  {
    Rng2F32 rect = source->rect;
    for(UI_Box *p = source->parent; !ui_box_is_nil(p); p = p->parent)
    { if(p->flags & UI_BoxFlag_Clip) { rect = intersect_2f32(rect, p->rect); } }
    if(dim_2f32(rect).x > 0 && dim_2f32(rect).y > 0 && contains_2f32(pad_2f32(rect, UIShell_HoverCardSourceDropPaddingPT), mouse))
    { return UIShell_CardPlacement_Inline; }
  }
  return UIShell_CardPlacement_Pinned;
}

// Removes a pinned card's pin, which makes it a card of none.
internal void
uishell_sidebar_card_saved_release(RD_WindowState *ws, UIShell_HoverCard *card)
{
  uishell_sidebar_pin_remove(cfg_node_from_id(ws->cfg_id), uishell_sidebar_card_pin_id(card));
  card->pin_size = 0;
}

internal void
uishell_sidebar_detached_apply(RD_WindowState *ws, UIShell_HoverCard *card)
{
  if(!card->move_requested || !card->open || (card->moving && ws->sidebar->drag_card == card)) { return; }
  UIShell_CardPlacement placement = card->requested;
  card->move_requested = 0;
  B32 drag = card->moving;
  if(drag) { placement = uishell_sidebar_card_drag_target(ws, card, ui_mouse()); }
  card->moving = 0;
  F32 width = placement == UIShell_CardPlacement_Float ? dim_2f32(card->rect).x : dim_2f32(ws->sidebar->rect).x;
  if(!uishell_sidebar_card_target_valid(ws, width)) { return; }
  if(placement == UIShell_CardPlacement_Inline && !card->source_key.size) { return; }
  if(placement == UIShell_CardPlacement_Pinned)
  {
    UIShell_LocalPin *saved = uishell_sidebar_card_pin(ws, card, drag);
    if(!saved) { return; }
    if(str8_match(uishell_sidebar_card_pin_id(card), saved->ghost, 0)) { return; }
    uishell_sidebar_card_close(card);
  }
  else
  {
    UIShell_HoverCard *c = card;
    if(card->placement == UIShell_CardPlacement_Transient)
    { c = uishell_sidebar_detached_copy(ws, card); uishell_sidebar_card_close(card); }
    if(c->pin_size)
    { uishell_sidebar_card_saved_release(ws, c); }
    c->placement = placement;
    if(placement == UIShell_CardPlacement_Inline)
    {
      c->source_row = c->source_key;
      for(U64 i = 0; uishell_sidebar_viewing(ws->sidebar) && i < uishell_sidebar_node_count(ws->sidebar); i++)
      {
        UIShell_SidebarNode node = {0}; uishell_sidebar_node(ws->sidebar, i, &node);
        if(str8_match(c->source_key, uishell_sidebar_string(node.key), 0) &&
           str8_match(uishell_sidebar_string(node.layout), str8_lit("inline"), 0) && node.parent != WHEELHOUSE_NONE)
        {
          UIShell_SidebarNode parent = {0}; uishell_sidebar_node(ws->sidebar, node.parent, &parent);
          c->source_row = push_str8_copy(c->arena, uishell_sidebar_string(parent.key)); break;
        }
      }
    }
    c->source_seen = 0;
    c->previous = (WheelhouseEntity){0};
  }
}

// WM routing runs before the next build. Read settled geometry from the UI
// that rendered this window, including clipping within its scrolling section.
internal void
uishell_sidebar_detached_bounds(RD_WindowState *ws)
{
  UI_State *previous = ui_state;
  ui_select_state(ws->ui);
  for(UIShell_HoverCard *c = ws->sidebar->detached; c; c = c->next)
  {
    if(!c->open || c->moving || c->placement == UIShell_CardPlacement_Float) { continue; }
    // Each card stores its actual key when rendered under the panel's seed.
    UI_Box *box = ui_box_from_key(c->mask.key);
    if(ui_box_is_nil(box) || box->last_touched_build_index+1 < ui_state->build_index)
    { c->rect = (Rng2F32){0}; continue; }
    c->rect = box->rect;
    for(UI_Box *p = box->parent; !ui_box_is_nil(p); p = p->parent)
    { if(p->flags & UI_BoxFlag_Clip) { c->rect = intersect_2f32(c->rect, p->rect); } }
  }
  ui_select_state(previous);
}

internal CFG_Node *uishell_sidebar_drag_local(UIShell_SidebarState *state);
internal void uishell_sidebar_local_move_group(CFG_Node *window, String8 group, String8 section, String8 after);
internal UIShell_LocalGroup *uishell_sidebar_section_drag_group(CFG_Node *window);
internal UIShell_LocalSection *uishell_sidebar_section_drag_section(CFG_Node *window);
internal void uishell_sidebar_local_drop_groups(CFG_Node *window, String8 from_section, String8 group, String8 section, String8 after);

internal void
uishell_sidebar_drag_clear(UIShell_SidebarState *state)
{
  if(state->drag_card) { state->drag_card->moving = state->drag_card->drag_released = 0; }
  state->drag_card = 0;
  state->row_drag_key = state->row_drag_loop = state->row_drag_label = str8_zero();
  state->row_drag_entity = (WheelhouseEntity){0};
  state->row_drag_released = 0;
  state->drop_panel = rd_nil_panel_address; state->reorder_build = 0;
  state->row_drag_workspace = 0; state->home_build = 0; state->group_claim_build = 0;
}

// Diagnostics hold Option/Alt here (drop a workspace as a ghost).
global WM_Modifiers uishell_sidebar_test_modifiers;

// The group insertion point a drag claimed, this build or the last, while the
// pointer is still over that group.
internal B32
uishell_sidebar_group_claimed(UIShell_SidebarState *state)
{
  return state->group_claim_build && state->group_claim_build+1 >= ui_state->build_index &&
    contains_2f32(state->group_claim_rect, ui_mouse());
}

// The gap between a section's groups a drag carrying groups claimed, this
// build or the last, while the pointer is still over that section (#282).
internal B32
uishell_sidebar_groups_claimed(UIShell_SidebarState *state)
{
  return state->groups_claim_build && state->groups_claim_build+1 >= ui_state->build_index &&
    contains_2f32(state->groups_claim_rect, ui_mouse());
}

// Applies that claim: the carried groups (all of section `section`'s, or
// group `group`) go into the section, at the gap.
internal B32
uishell_sidebar_groups_drop(UIShell_SidebarState *state, CFG_Node *window, String8 section, String8 group)
{
  if(!uishell_sidebar_groups_claimed(state)) { return 0; }
  UIShell_LocalSection *target = uishell_sidebar_local_section(window, state->groups_claim_section);
  if(!target || str8_match(target->id, section, 0)) { return 0; }
  String8 after = uishell_sidebar_local_group(window, state->groups_claim_after) ? state->groups_claim_after : str8_zero();
  uishell_sidebar_local_drop_groups(window, section, group, target->id, after);
  state->groups_claim_build = 0;
  return 1;
}

// Saves the claimed group's item order with `placed` at the claimed index.
// An empty group has no run yet, and its first item needs no order.
internal void
uishell_sidebar_group_order(UIShell_SidebarState *state, WheelhouseEntity placed)
{
  if(!state->group_claim_loop.size) { return; }
  Arena *arena = ui_build_arena();
  WheelhouseEntity *all = 0;
  U64 total = uishell_sidebar_siblings(arena, state, state->group_claim_loop, &all);
  U64 index = state->group_claim_index;
  WheelhouseEntity *order = push_array(arena, WheelhouseEntity, total+1);
  U64 n = 0;
  for(U64 k = 0; k < total; k++)
  {
    // The index counts the item itself where it is, as the line does.
    if(uishell_sidebar_card_entity_match(all[k], placed)) { if(k < index) { index--; } continue; }
    order[n++] = uishell_sidebar_card_entity_copy(arena, all[k]);
  }
  index = Min(index, n);
  for(U64 k = n; k > index; k--) { order[k] = order[k-1]; }
  order[index] = uishell_sidebar_card_entity_copy(arena, placed);
  state->order_pending = 1;
  state->order_loop = push_str8_copy(arena, state->group_claim_loop);
  state->order = order; state->order_count = n+1;
}

// A row's release in its sibling run saves the run's full new order, which
// render applies (it reads the model).
internal void
uishell_sidebar_reorder_commit(UIShell_SidebarState *state)
{
  Arena *arena = ui_build_arena();
  WheelhouseEntity *all = 0;
  U64 total = uishell_sidebar_siblings(arena, state, state->row_drag_loop, &all);
  WheelhouseEntity *order = push_array(arena, WheelhouseEntity, total+1);
  U64 n = 0;
  for(U64 k = 0; k < total; k++)
  {
    if(uishell_sidebar_card_entity_match(all[k], state->row_drag_entity)) { continue; }
    B32 here = uishell_sidebar_card_entity_match(all[k], state->reorder_anchor);
    if(here && !state->reorder_after) { order[n++] = state->row_drag_entity; }
    order[n++] = all[k];
    if(here && state->reorder_after) { order[n++] = state->row_drag_entity; }
  }
  if(n < total) { order[n++] = state->row_drag_entity; }
  for(U64 k = 0; k < n; k++) { order[k] = uishell_sidebar_card_entity_copy(arena, order[k]); }
  state->order_pending = 1;
  state->order_loop = push_str8_copy(arena, state->row_drag_loop);
  state->order = order; state->order_count = n;
}

// Resolves a sidebar drag's release, of a card or a row, against one set of
// targets, in order (drag-model.md, drop table):
//   - a pinned area's insertion point: a ghost there (a row as a row, a card
//     as a card), or a pinned card's own ghost moves there;
//   - a card back over its source row: inline;
//   - a docking site: a new pinned area holding the ghost;
//   - a local workspace over another group: move there;
//   - a row over its sibling run: reorder.
// Otherwise a card floats and a row snaps back.
internal void
uishell_sidebar_drag_finish(RD_WindowState *ws)
{
  UIShell_SidebarState *state = ws->sidebar;
  UIShell_HoverCard *card = state->drag_card;
  B32 row = state->row_drag_key.size != 0;
  if(!card && !row)
  {
    // A local section's View dropped between another section's groups, and
    // no docking site took it: its groups move there (#282).
    CFG_Node *window = cfg_node_from_id(ws->cfg_id);
    UIShell_LocalSection *dragged = uishell_sidebar_section_drag_section(window);
    if(dragged && rd_state->drag_drop_state == RD_DragDropState_Dropping &&
       uishell_sidebar_groups_drop(state, window, dragged->id, str8_zero()))
    {
      rd_drag_kill();
      rd_request_frame();
    }
    return;
  }
  B32 released = row ? state->row_drag_released : card->drag_released;
  if((card && !card->open) || (!rd_drag_is_active() && !released && !state->drop_panel.id))
  {
    if(rd_state->drag_drop_commit == uishell_sidebar_drag_panel_drop && rd_state->drag_drop_regs->window == ws->cfg_id)
    { rd_drag_kill(); }
    uishell_sidebar_drag_clear(state);
    return;
  }
  if(rd_state->drag_drop_state == RD_DragDropState_Dropping) { released = 1; }
  if(!released) { return; }
  rd_drag_kill();
  rd_request_frame();
  WheelhouseEntity entity = card ? card->path[card->depth-1] : state->row_drag_entity;
  String8 label = card ? card->retained_label : state->row_drag_label;
  String8 source = card ? card->source_key : state->row_drag_key;
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  // The pin a pinned card moves: its own.
  String8 own = card && uishell_sidebar_pin_by_ghost(window, uishell_sidebar_card_pin_id(card)) ?
    push_str8_copy(scratch.arena, uishell_sidebar_card_pin_id(card)) : str8_zero();
  // A local group's insertion point (drag-model.md, drop table): a local
  // workspace moves there (Option/Alt adds a ghost of it instead), a pinned
  // card moves its own ghost there, and anything else becomes a ghost there,
  // a row as a row and a card as a card. A committed edge site still wins.
  B32 edge = state->drop_panel.id && state->drop_direction != Dir2_Invalid;
  UIShell_LocalGroup *group = !edge && uishell_sidebar_group_claimed(state) ? uishell_sidebar_local_group(window, state->group_claim_id) : 0;
  // A local group's header moves the group: over a group in another
  // section, into that section after it; on a docking site, into the
  // section shown there, or a new section of its own, which borrows its
  // name. Within its own section it reorders, below; it never ghosts.
  UIShell_LocalGroup *dragged_group = row && str8_match(uishell_sidebar_string(entity.kind), str8_lit(".group"), 0) ?
    uishell_sidebar_local_group(window, uishell_sidebar_string(entity.id)) : 0;
  if(dragged_group)
  {
    String8 dragged_id = push_str8_copy(scratch.arena, dragged_group->id);
    String8 dragged_section = push_str8_copy(scratch.arena, dragged_group->section->id);
    // Between another section's groups, it goes there (#282).
    if(!edge && uishell_sidebar_groups_drop(state, window, str8_zero(), dragged_id))
    { uishell_sidebar_drag_clear(state); scratch_end(scratch); return; }
    // On a section's tab strip, the group joins as a tab at the gap (#282):
    // a new section of its own, shown after the tab the gap follows.
    RD_PanelAddress strip = state->drop_panel;
    if(!group && rd_panel_exists(strip) && state->drop_direction == Dir2_Invalid &&
       !dragged_group->is_default && rd_dock_can_create(str8_lit("sidebar_section"), rd_dock_host_from_panel(strip, RD_DOCK_UNMEASURED_WIDTH)))
    {
      UIShell_LocalSection *section = uishell_sidebar_local_new_section(window);
      String8 section_id = section ? push_str8_copy(scratch.arena, section->id) : str8_zero();
      uishell_sidebar_local_move_group(window, dragged_id, section_id, str8_zero());
      CFG_Node *previous = rd_cfg_from_view(state->drop_previous_tab);
      uishell_sidebar_local_new_view_after(strip, section_id, rd_panel_address_match(rd_panel_from_cfg(previous), strip) ? previous : &cfg_nil_node);
      uishell_sidebar_drag_clear(state);
      scratch_end(scratch);
      return;
    }
    if(!group && state->drop_panel.id)
    {
      UIShell_Locals *locals = uishell_sidebar_window_locals(window);
      String8 last_section = locals->section_count ? push_str8_copy(scratch.arena, locals->sections[locals->section_count-1].id) : str8_zero();
      UIShell_LocalGroup *made = uishell_sidebar_pin_area(ws, 1);
      if(made && !str8_match(made->section->id, dragged_section, 0))
      {
        // A section made for the drop holds only the group pin_area made for
        // pins; the dragged group replaces it.
        String8 made_id = push_str8_copy(scratch.arena, made->id), section = push_str8_copy(scratch.arena, made->section->id);
        locals = uishell_sidebar_window_locals(window);
        B32 fresh = !str8_match(section, last_section, 0) && locals->section_count &&
          str8_match(locals->sections[locals->section_count-1].id, section, 0);
        if(fresh)
        {
          uishell_sidebar_local_move_group(window, dragged_id, section, str8_zero());
          UIShell_Locals *moved = uishell_sidebar_window_locals(window);
          uishell_sidebar_local_remove(state, str8_lit(".group"), made_id);
          uishell_sidebar_local_settle(state, moved);
        }
        else { uishell_sidebar_local_move_group(window, dragged_id, section, made_id); }
      }
      uishell_sidebar_drag_clear(state);
      scratch_end(scratch);
      return;
    }
    group = 0;
  }
  if(group)
  {
    String8 group_id = push_str8_copy(scratch.arena, group->id);
    CFG_Node *workspace = row ? uishell_sidebar_drag_local(state) : &cfg_nil_node;
    B32 as_ghost = !!((wm_get_modifiers()|uishell_sidebar_test_modifiers) & WM_Modifier_Alt);
    WheelhouseEntity placed = {0};
    if(workspace != &cfg_nil_node && !as_ghost)
    {
      // One home: this group.
      cfg_node_release(rd_state->cfg, cfg_node_child_from_string(workspace, str8_lit("lives_with")));
      CFG_Node *lives_in = cfg_node_child_from_string_or_alloc(rd_state->cfg, workspace, str8_lit("lives_in"));
      cfg_node_new_replace(rd_state->cfg, lives_in, group_id);
      placed = uishell_sidebar_entity(uishell_sidebar_host_provider, str8_lit(".workspace"),
                                      push_str8_copy(scratch.arena, uishell_sidebar_local_entity(workspace)));
    }
    else
    {
      // A ghost's own row moves that ghost, as its card does.
      if(row && str8_match(uishell_sidebar_string(entity.kind), str8_lit(".ref"), 0) &&
         uishell_sidebar_pin_by_ghost(window, uishell_sidebar_string(entity.id)))
      { own = push_str8_copy(scratch.arena, uishell_sidebar_string(entity.id)); }
      String8 ghost = own;
      if(own.size) { uishell_sidebar_pin_place(window, own, group_id, max_U64); state->pin_reveal = u64_hash_from_str8(own); }
      else
      {
        UIShell_LocalPin *added = uishell_sidebar_pin_add(ws, group_id, max_U64, entity, label, source, row);
        ghost = added ? push_str8_copy(scratch.arena, added->ghost) : str8_zero();
        if(card) { uishell_sidebar_card_close(card); }
      }
      placed = uishell_sidebar_entity(uishell_sidebar_local_provider, str8_lit(".ref"), ghost);
    }
    uishell_sidebar_group_order(state, placed);
    uishell_sidebar_drag_clear(state);
    scratch_end(scratch);
    return;
  }
  UIShell_LocalGroup *area = 0;
  U64 index = max_U64;
  UIShell_CardPlacement placement = card ? uishell_sidebar_card_drag_target(ws, card, ui_mouse()) : UIShell_CardPlacement_Float;
  B32 docked = placement != UIShell_CardPlacement_Inline && state->drop_panel.id;
  // A site that can't take a pinned area leaves `area` 0: the drop falls
  // through to the row's or card's own targets below.
  if(docked) { area = uishell_sidebar_pin_area(ws, 1); index = max_U64; }
  if(area)
  {
    if(own.size) { uishell_sidebar_pin_place(window, own, area->id, index); state->pin_reveal = u64_hash_from_str8(own); }
    else
    {
      uishell_sidebar_pin_add(ws, area->id, index, entity, label, source, row);
      if(card) { uishell_sidebar_card_close(card); }
    }
  }
  else if(row)
  {
    // A local workspace moves to the group it was dropped on: "lives with
    // project X", saved on the workspace, or back to Workspaces.
    CFG_Node *workspace = uishell_sidebar_drag_local(state);
    if(workspace != &cfg_nil_node && state->home_build && state->home_build+1 >= ui_state->build_index)
    {
      cfg_node_release(rd_state->cfg, cfg_node_child_from_string(workspace, str8_lit("lives_with")));
      if(state->home_project.size)
      { cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, workspace, str8_lit("lives_with")), state->home_project); }
    }
    else if(state->reorder_build && state->reorder_build+1 >= ui_state->build_index) { uishell_sidebar_reorder_commit(state); }
  }
  else
  {
    if(placement != UIShell_CardPlacement_Inline) { placement = UIShell_CardPlacement_Float; }
    card->moving = card->drag_released = 0;
    uishell_sidebar_card_request(card, placement);
    uishell_sidebar_detached_apply(ws, card);
  }
  uishell_sidebar_drag_clear(state);
  scratch_end(scratch);
}

internal void
uishell_sidebar_detached_finish(RD_WindowState *ws)
{
  for(U64 i = 0; i < ArrayCount(ws->sidebar->cards); i++)
  { uishell_sidebar_detached_apply(ws, &ws->sidebar->cards[i]); }
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  for(UIShell_HoverCard *c = ws->sidebar->detached; c; c = c->next)
  {
    // A card whose pin went (removed, or its group or section deleted) goes.
    if(c->pin_size && !uishell_sidebar_pin_by_ghost(window, uishell_sidebar_card_pin_id(c)))
    { c->pin_size = 0; uishell_sidebar_card_close(c); }
    uishell_sidebar_detached_apply(ws, c);
    if(!c->open && c->pin_size)
    { uishell_sidebar_card_saved_release(ws, c); }
    UIShell_LocalPin *pin = c->open && c->pin_size && c->depth == 1 ? uishell_sidebar_pin_by_ghost(window, uishell_sidebar_card_pin_id(c)) : 0;
    if(pin && !str8_match(pin->label, c->retained_label, 0))
    {
      UIShell_LocalPin values = *pin;
      values.label = c->retained_label;
      uishell_sidebar_pin_update(window, pin->ghost, values);
    }
    if(c->placement == UIShell_CardPlacement_Inline && c->open)
    {
      B32 present = 0;
      for(U64 i = 0; uishell_sidebar_viewing(ws->sidebar) && i < uishell_sidebar_node_count(ws->sidebar); i++)
      {
        UIShell_SidebarNode node = {0}; uishell_sidebar_node(ws->sidebar, i, &node);
        if(str8_match(c->source_key, uishell_sidebar_string(node.key), 0)) { present = 1; break; }
      }
      if(!present || uishell_sidebar_card_find(ws->sidebar, c->path[c->depth-1], 0) == WHEELHOUSE_NONE)
      { uishell_sidebar_card_close(c); }
    }
  }
}

// A pin's card, made from the pin when it has none. Filtering the tree never
// removes a pinned card; missing entities use the pin's label and an
// explicit ended state.
internal UIShell_HoverCard *
uishell_sidebar_saved_card(RD_WindowState *ws, UIShell_LocalPin *saved)
{
  for(UIShell_HoverCard *c = ws->sidebar->detached; c; c = c->next)
  { if(c->pin_size && str8_match(uishell_sidebar_card_pin_id(c), saved->ghost, 0)) { return c; } }
  UIShell_HoverCard *c = uishell_sidebar_detached_alloc(ws);
  c->arena = arena_alloc(); c->capacity = UIShell_HoverCardInitialPathCapacity; c->depth = 1;
  c->path = push_array(c->arena, WheelhouseEntity, c->capacity);
  WheelhouseEntity entity = uishell_sidebar_pin_entity(saved);
  c->path[0] = uishell_sidebar_card_entity_copy(c->arena, entity);
  c->source_key = push_str8_copy(c->arena, saved->source);
  uishell_sidebar_card_retain_label(c, saved->label);
  uishell_sidebar_card_set_pin(c, saved->ghost);
  c->placement = UIShell_CardPlacement_Pinned; c->open = c->engaged = 1;
  return c;
}

// Inline cards are actual children of the source row's scrolling tree. Height
// participates in the section and project allocation, including collapse.
internal F32
uishell_sidebar_inline_height(UIShell_SidebarState *state, String8 key)
{
  F32 height = 0;
  for(UIShell_HoverCard *c = state->detached; c; c = c->next)
  {
    if(c->open && c->placement == UIShell_CardPlacement_Inline && str8_match(c->source_row, key, 0))
    { height += (c->content_height > 0 ? c->content_height : ui_top_font_size()*UIShell_HoverCardInlineFallbackHeightEM)+8; }
  }
  return height;
}

internal void
uishell_sidebar_inline_ui(RD_WindowState *ws, String8 key, F32 width)
{
  for(UIShell_HoverCard *c = ws->sidebar->detached; c; c = c->next)
  {
    if(!c->open || c->placement != UIShell_CardPlacement_Inline || !str8_match(c->source_row, key, 0)) { continue; }
    if(c->moving) { ui_spacer(ui_px(c->content_height+8.f, 1)); continue; }
    ui_spacer(ui_px(4, 1));
    F32 card_width = Max(0.f, width-12.f), content_width = Max(0.f, card_width-12.f);
    // The cap's edge is square, and the outline round both is the border
    // (uishell_sidebar_card_cap).
    F32 top = c->cap_drawn && !c->cap_below ? 0 : 5.f, bottom = c->cap_drawn && c->cap_below ? 0 : 5.f;
    UI_PrefWidth(ui_px(card_width, 1)) UI_PrefHeight(ui_children_sum(1)) UI_ChildLayoutAxis(Axis2_Y)
    UI_CornerRadius00(top) UI_CornerRadius10(top) UI_CornerRadius01(bottom) UI_CornerRadius11(bottom)
    UI_BackgroundColor(mix_4f32(ui_color_from_name(str8_lit("background")), ui_color_from_name(str8_lit("text")), .025f))
    UI_Focus(c->focused ? UI_FocusKind_On : UI_FocusKind_Off)
    {
      ui_set_next_fixed_x(6.f);
      UI_Box *root = ui_build_box_from_stringf((c->cap_drawn ? 0 : UI_BoxFlag_DrawBorder)|UI_BoxFlag_DrawBackground|UI_BoxFlag_DefaultFocusNavY|
        UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder, "###inline_card_%p", c);
      c->mask.key = root->key;
      UI_Parent(root) UI_FocusHot(UI_FocusKind_Root) UI_FocusActive(UI_FocusKind_Root)
      UI_PrefHeight(ui_em(1.6f, 1))
      {
        ui_spacer(ui_px(4.f, 1));
        ui_set_next_fixed_x(6.f);
        UI_PrefWidth(ui_px(content_width, 1)) UI_PrefHeight(ui_children_sum(1)) UI_Column UI_PrefHeight(ui_em(1.6f, 1))
        {
          UIShell_SidebarNode node = {0}; U64 index = uishell_sidebar_card_find(ws->sidebar, c->path[c->depth-1], &node);
          size_t action = uishell_sidebar_detached_content(ws->sidebar, ws, c, (U64)c, node, index, content_width, 1);
          if(action != WHEELHOUSE_NONE) { uishell_sidebar_card_queue_action(ws->sidebar, node, action); }
        }
        ui_spacer(ui_px(4.f, 1));
        ui_layout_root(root, Axis2_X); ui_layout_root(root, Axis2_Y);
        F32 height = root->fixed_size.y;
        if(abs_f32(c->content_height-height) > .5f) { rd_request_frame(); }
        c->content_height = height;
        if(!c->moving) { c->rect = root->rect; c->rect.y1 = c->rect.y0+height; }
        root->flags |= UI_BoxFlag_MouseClickable; ui_signal_from_box(root);
      }
    }
    ui_spacer(ui_px(4, 1));
  }
}

internal F32
uishell_sidebar_pinned_card_extent(UIShell_HoverCard *card, F32 em)
{
  return Max(em*UIShell_HoverCardPinnedMinimumHeightEM, card->content_height)+UIShell_HoverCardPinnedGapPT;
}

internal Vec4F32 uishell_sidebar_ended_color(void);
internal String8 uishell_sidebar_status_mark(UIShell_SidebarNode node, String8 status);
internal String8 uishell_sidebar_chip_status(UIShell_SidebarState *state, UIShell_SidebarNode node);

// Pinning a card keeps the card. Its header's disclosure collapses the ghost
// to a compact row (saved as `compact`), and the row's disclosure restores it.
internal B32
uishell_sidebar_pin_expanded(UIShell_LocalPin *saved)
{
  return !saved->compact;
}

internal F32
uishell_sidebar_ghost_extent(UIShell_HoverCard *card, UIShell_LocalPin *saved, F32 em)
{
  return uishell_sidebar_pin_expanded(saved) ? uishell_sidebar_pinned_card_extent(card, em) : floor_f32(em*2.2f);
}

// Pin `ghost` of `window`'s sidebar as a card, or as a compact row.
internal void
uishell_sidebar_ghost_set_expanded(CFG_Node *window, String8 ghost, B32 expanded)
{
  UIShell_LocalPin *pin = uishell_sidebar_pin_by_ghost(window, ghost);
  if(!pin || pin->compact == !expanded) { return; }
  UIShell_LocalPin values = *pin;
  values.compact = !expanded;
  uishell_sidebar_pin_update(window, pin->ghost, values);
  rd_request_frame();
}

// An expanded ghost: its card, `width` wide, drawn in its place in a list.
internal void
uishell_sidebar_ghost_card(UIShell_SidebarState *state, RD_WindowState *ws, UIShell_HoverCard *c, UIShell_LocalPin *saved, F32 card_width)
{
  F32 content_width = Max(0.f, card_width-12.f);
  // The cap's edge is square, and the outline round both is the border
  // (uishell_sidebar_card_cap).
  F32 top = c->cap_drawn && !c->cap_below ? 0 : 5.f, bottom = c->cap_drawn && c->cap_below ? 0 : 5.f;
  UI_PrefWidth(ui_px(card_width, 1))
  {
    UI_PrefHeight(ui_children_sum(1)) UI_ChildLayoutAxis(Axis2_Y)
    UI_Focus(c->focused ? UI_FocusKind_On : UI_FocusKind_Off)
    UI_CornerRadius00(top) UI_CornerRadius10(top) UI_CornerRadius01(bottom) UI_CornerRadius11(bottom)
    UI_BackgroundColor(mix_4f32(ui_color_from_name(str8_lit("background")), ui_color_from_name(str8_lit("text")), .025f))
    {
      ui_set_next_fixed_x(6.f);
      UI_Box *body = ui_build_box_from_stringf((c->cap_drawn ? 0 : UI_BoxFlag_DrawBorder)|UI_BoxFlag_DrawBackground|UI_BoxFlag_DefaultFocusNavY|
        UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder, "###pinned_card_%S", saved->ghost);
      c->mask.key = body->key;
      UI_Parent(body) UI_PrefHeight(ui_em(1.6f, 1)) UI_FocusHot(UI_FocusKind_Root) UI_FocusActive(UI_FocusKind_Root)
      {
        ui_spacer(ui_px(4.f, 1));
        ui_set_next_fixed_x(6.f);
        UI_PrefWidth(ui_px(content_width, 1)) UI_PrefHeight(ui_children_sum(1)) UI_Column UI_PrefHeight(ui_em(1.6f, 1))
        {
          UIShell_SidebarNode node = {0}; U64 index = uishell_sidebar_card_find(state, c->path[c->depth-1], &node);
          size_t action = uishell_sidebar_detached_content(state, ws, c, u64_hash_from_str8(saved->ghost), node, index, content_width, 1);
          if(action != WHEELHOUSE_NONE) { uishell_sidebar_card_queue_action(state, node, action); }
        }
        ui_spacer(ui_px(4.f, 1));
        ui_layout_root(body, Axis2_X); ui_layout_root(body, Axis2_Y);
        if(abs_f32(c->content_height-body->fixed_size.y) > .5f) { rd_request_frame(); }
        c->content_height = body->fixed_size.y;
        if(!c->moving) { c->rect = body->rect; c->rect.y1 = c->rect.y0+body->fixed_size.y; }
        body->flags |= UI_BoxFlag_MouseClickable; ui_signal_from_box(body);
      }
    }
  }
}

// Retired: saved pinned areas migrate into local sections before Views
// render (uishell_sidebar_pin_migrate). The type stays registered so a saved
// layout loads until then; remove it, and the migration, once layouts saved
// before 2026-10-08 no longer need loading (drag-model.md, "Migration").
WH_VIEW_UI_FUNCTION_DEF(pinned_cards)
{
  (void)ctx;
}
