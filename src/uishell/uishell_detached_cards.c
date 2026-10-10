internal void uishell_sidebar_manual_sizing(CFG_Node *window, B32 manual);
// Detached presentation belongs to the card controller. Pinned areas are
// ordinary registered Views in the RAD-derived panel tree, never KDL edits.
internal B32
uishell_sidebar_card_target_valid(RD_WindowState *ws, UIShell_CardPlacement placement, F32 width)
{
  RD_DockProposal p = {0};
  p.host.kind = placement == UIShell_CardPlacement_Float ? RD_DockHostKind_FloatingPanel : RD_DockHostKind_Sidebar;
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
uishell_sidebar_drag_panel_drop(CFG_ID destination, Dir2 direction, CFG_ID previous_tab)
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
  ws->sidebar->drop_panel = 0; ws->sidebar->drop_previous_tab = 0; ws->sidebar->reorder_build = 0; ws->sidebar->group_claim_build = 0;
  UIShell_RegsScope(.window = ws->cfg_id, .view = 0, .panel = 0, .tab = 0) { rd_drag_begin(UIShell_ContextRegSlot_View); }
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

internal CFG_Node *uishell_sidebar_pin_find(CFG_Node *root, AndamentoEntity entity, B32 area_only);

internal CFG_Node *uishell_sidebar_pin_target(CFG_Node *window);

// Pin reveals the subject's pin in the group Pin uses (the topmost showing
// one) rather than duplicating it, or adds one there.
internal void
uishell_sidebar_card_pin_control(UIShell_HoverCard *card)
{
  RD_WindowState *ws = rd_window_state_from_os_handle(ui_state->window);
  B32 pinned = 0;
  if(ws != &rd_nil_window_state && card->depth)
  {
    CFG_Node *target = uishell_sidebar_pin_target(cfg_node_from_id(ws->cfg_id));
    for(CFG_Node *c = target->first; c != &cfg_nil_node && !pinned; c = c->next)
    {
      AndamentoEntity saved = {uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("kind"))->first->string),
                               uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("entity"))->first->string)};
      pinned = str8_match(c->string, str8_lit("card"), 0) && uishell_sidebar_card_entity_match(saved, card->path[card->depth-1]);
    }
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
    if(c->open || c->saved) { continue; }
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
  c->path = push_array(c->arena, AndamentoEntity, c->capacity);
  c->depth = source->depth;
  for(U64 i = 0; i < c->depth; i++) { c->path[i] = uishell_sidebar_card_entity_copy(c->arena, source->path[i]); }
  c->source_key = push_str8_copy(c->arena, source->source_key);
  uishell_sidebar_card_retain_label(c, source->retained_label);
  c->source = source->source; c->source_rect = source->source_rect; c->rect = source->rect;
  c->open = c->engaged = 1; c->focused = source->focused; c->enriched = source->enriched;
  return c;
}

// Missing identity fields resolve through CFG's nil sentinel to empty strings;
// malformed saved entries stay available to render their retained/missing label.
// Pins (ghosts) live in the window's local groups (uishell_sidebar_local_tree). With
// `area_only`, the first group that isn't the default one (where Pin puts
// things); otherwise the first ghost of `entity`.
internal CFG_Node *
uishell_sidebar_pin_find(CFG_Node *window, AndamentoEntity entity, B32 area_only)
{
  CFG_Node *root = uishell_sidebar_local_tree(window);
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    for(CFG_Node *group = section->first; group != &cfg_nil_node; group = group->next)
    {
      if(!str8_match(group->string, str8_lit("group"), 0)) { continue; }
      if(area_only)
      {
        if(cfg_node_child_from_string(group, str8_lit("default")) == &cfg_nil_node) { return group; }
        continue;
      }
      for(CFG_Node *c = group->first; c != &cfg_nil_node; c = c->next)
      {
        if(!str8_match(c->string, str8_lit("card"), 0)) { continue; }
        AndamentoEntity saved = {uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("kind"))->first->string),
                                 uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("entity"))->first->string)};
        if(uishell_sidebar_card_entity_match(saved, entity)) { return c; }
      }
    }
  }
  return &cfg_nil_node;
}

// The ghost with this ghost id, or nil.
internal CFG_Node *
uishell_sidebar_pin_by_ghost(CFG_Node *window, String8 ghost)
{
  CFG_Node *root = uishell_sidebar_local_tree(window);
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    for(CFG_Node *group = section->first; group != &cfg_nil_node; group = group->next)
    {
      for(CFG_Node *c = group->first; c != &cfg_nil_node; c = c->next)
      {
        if(str8_match(c->string, str8_lit("card"), 0) &&
           str8_match(cfg_node_child_from_string(c, str8_lit("ghost"))->first->string, ghost, 0)) { return c; }
      }
    }
  }
  return &cfg_nil_node;
}

// Where Pin puts a pin: the first group, other than the default one, of the
// topmost local section that is showing (its View open and its panel's
// selected tab), in docking order. Nil when none is showing.
internal CFG_Node *uishell_sidebar_local_view_group(CFG_Node *window, CFG_Node *view);

internal CFG_Node *
uishell_sidebar_pin_target(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *sidebar = rd_arrangement_from_owner(scratch.arena, window, RD_DOCK_SIDEBAR_ROOT);
  CFG_Node *result = &cfg_nil_node;
  for(RD_ArrangementPanel *p = sidebar->root; p != &rd_nil_arrangement_panel && result == &cfg_nil_node; p = rd_arrangement_next(sidebar->root, p))
  {
    CFG_Node *v = cfg_node_from_id(p->selected);
    if(!str8_match(v->string, str8_lit("sidebar_section"), 0)) { continue; }
    CFG_Node *group = uishell_sidebar_local_view_group(window, v);
    for(; group != &cfg_nil_node && result == &cfg_nil_node; group = group->next)
    {
      if(str8_match(group->string, str8_lit("group"), 0) && cfg_node_child_from_string(group, str8_lit("default")) == &cfg_nil_node)
      { result = group; }
    }
  }
  scratch_end(scratch);
  return result;
}

internal void
uishell_sidebar_pin_set_field(CFG_Node *node, String8 name, String8 value)
{
  CFG_Node *field = cfg_node_child_from_string_or_alloc(rd_state->cfg, node, name);
  cfg_node_new_replace(rd_state->cfg, field, value);
}

// Each saved card is a ghost: its own object referring to an entity
// (drag-model.md, decision 1), so any number may refer to one entity. Its
// ghost id is what copied layouts deduplicate on.
internal String8
uishell_sidebar_pin_ghost(CFG_Node *card)
{
  return cfg_node_child_from_string(card, str8_lit("ghost"))->first->string;
}

internal void
uishell_sidebar_pin_new_ghost(CFG_Node *card)
{
  Temp scratch = scratch_begin(0, 0);
  uishell_sidebar_pin_set_field(card, str8_lit("ghost"), string_from_guid(scratch.arena, make_guid()));
  scratch_end(scratch);
}

internal void
uishell_sidebar_pin_cards(Arena *arena, CFG_Node *window, CFG_NodePtrList *out)
{
  CFG_Node *root = uishell_sidebar_local_tree(window);
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    for(CFG_Node *group = section->first; group != &cfg_nil_node; group = group->next)
    {
      for(CFG_Node *c = group->first; c != &cfg_nil_node; c = c->next)
      { if(str8_match(c->string, str8_lit("card"), 0)) { cfg_node_ptr_list_push(arena, out, c); } }
    }
  }
}

internal CFG_Node *uishell_sidebar_local_new_group(CFG_Node *window, String8 label);

// Saved pinned areas (pinned_cards Views) become local sections: each a
// section holding one group with the area's pins, keeping their ghost ids
// and forms, and its View a section View showing it in the same place
// (drag-model.md, "Migration"). Best-effort: unknown children are dropped.
// A pinned area saved outside any panel goes to the sidebar's first.
internal void
uishell_sidebar_pin_migrate(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarDocks docks = uishell_sidebar_docks(scratch.arena, window);
  for(RD_DockSavedView *saved = docks.views.first; saved != 0; saved = saved->next)
  {
    CFG_Node *v = saved->view;
    if(!str8_match(v->string, str8_lit("pinned_cards"), 0)) { continue; }
    String8 label = cfg_node_child_from_string(v, str8_lit("label"))->first->string;
    CFG_Node *group = uishell_sidebar_local_new_group(window, label.size ? label : str8_lit("Pinned"));
    B32 selected = cfg_node_child_from_string(v, str8_lit("selected")) != &cfg_nil_node;
    CFG_Node *view = cfg_node_new(rd_state->cfg, &cfg_nil_node, str8_lit("sidebar_section"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("section")),
      uishell_sidebar_local_key(scratch.arena, uishell_sidebar_local_field(group->parent, str8_lit("id"))));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("label")), uishell_sidebar_local_title(scratch.arena, group->parent));
    if(selected) { cfg_node_new(rd_state->cfg, view, str8_lit("selected")); }
    // The area's own state: its collapsed header. Its other children were
    // its label and selection, carried above; a pin's form travels with it.
    if(cfg_node_child_from_string(v, str8_lit("section_collapsed")) != &cfg_nil_node)
    { cfg_node_new(rd_state->cfg, view, str8_lit("section_collapsed")); }
    for(CFG_Node *c = v->first, *after; c != &cfg_nil_node; c = after)
    {
      after = c->next;
      if(str8_match(c->string, str8_lit("card"), 0)) { cfg_node_insert_child(rd_state->cfg, group, group->last, c); }
    }
    // The section View takes the area's place: after it in its panel, or
    // after the tab a stray area was saved inside.
    RD_DockDocument *document = saved->document ? saved->document : docks.sidebar;
    RD_ArrangementPanel *panel = saved->document ? saved->panel : rd_dock_restore_leaf(document->arrangement);
    CFG_ID prev = rd_dock_saved_view_is_tab(saved) ? v->id : saved->holder ? saved->holder : panel->last_tab ? panel->last_tab->view : 0;
    rd_arrangement_insert_tab(document->arrangement, view->id, panel->id, prev, selected);
    document->dirty = 1;
    uishell_sidebar_docks_remove(&docks, saved);
  }
  uishell_sidebar_docks_save(&docks);
  scratch_end(scratch);
}

// Copied layouts duplicate ghost ids; the first of each is kept. Pins saved
// before ghost ids were unique per entity: the first per entity is kept and
// given an id. As in pin_find, missing fields read as empty. Quadratic in the
// window's pins, which are few; it runs only when the cfg generation changes.
internal void
uishell_sidebar_pin_deduplicate(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_NodePtrList cards = {0};
  uishell_sidebar_pin_cards(scratch.arena, window, &cards);
  B32 *legacy = push_array(scratch.arena, B32, cards.count);
  CFG_Node **kept = push_array(scratch.arena, CFG_Node *, cards.count);
  U64 index = 0, kept_count = 0;
  for(CFG_NodePtrNode *n = cards.first; n; n = n->next, index++) { legacy[index] = uishell_sidebar_pin_ghost(n->v).size == 0; }
  index = 0;
  for(CFG_NodePtrNode *n = cards.first; n; n = n->next, index++)
  {
    CFG_Node *c = n->v;
    AndamentoEntity entity = {uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("kind"))->first->string),
                              uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("entity"))->first->string)};
    B32 duplicate = 0;
    U64 k = 0;
    for(CFG_NodePtrNode *m = cards.first; m != n && !duplicate; m = m->next, k++)
    {
      B32 earlier_kept = 0;
      for(U64 j = 0; j < kept_count; j++) { earlier_kept |= kept[j] == m->v; }
      if(!earlier_kept) { continue; }
      if(legacy[index])
      {
        AndamentoEntity other = {uishell_sidebar_text(cfg_node_child_from_string(m->v, str8_lit("kind"))->first->string),
                                 uishell_sidebar_text(cfg_node_child_from_string(m->v, str8_lit("entity"))->first->string)};
        duplicate = legacy[k] && uishell_sidebar_card_entity_match(entity, other);
      }
      else { duplicate = str8_match(uishell_sidebar_pin_ghost(c), uishell_sidebar_pin_ghost(m->v), 0); }
    }
    if(duplicate) { cfg_node_release(rd_state->cfg, c); continue; }
    if(legacy[index]) { uishell_sidebar_pin_new_ghost(c); }
    kept[kept_count++] = c;
  }
  scratch_end(scratch);
}

// Moves `saved` among `area`'s pins to sit before the pin at `index`, or
// last. `index` counts the area's pins as the insertion line does, `saved`
// included, so a pin moved down lands where the line showed. The area's
// other children (label, selected) keep their places.
internal void
uishell_sidebar_pin_place(CFG_Node *area, CFG_Node *saved, U64 index)
{
  CFG_Node *before = &cfg_nil_node;
  U64 i = 0;
  for(CFG_Node *c = area->first; c != &cfg_nil_node; c = c->next)
  {
    if(!str8_match(c->string, str8_lit("card"), 0)) { continue; }
    if(i++ == index) { before = c; break; }
  }
  // Already in place (the line on either side of it); also never insert a
  // node after itself.
  if(before == saved) { return; }
  if(saved->parent == area && (before != &cfg_nil_node ? before->prev == saved : area->last == saved)) { return; }
  cfg_node_insert_child(rd_state->cfg, area, before != &cfg_nil_node ? before->prev : area->last, saved);
}

// A sidebar View may claim a positioned drop unless a docking site has the
// pointer: its panel's edge pills and tab strip sit outside its body
// (drag-model.md, "Drop-target visuals").
internal B32
uishell_sidebar_drop_claimable(CFG_Node *panel)
{
  (void)panel;
  return ui_key_match(ui_drop_hot_key(), ui_key_zero());
}

// Adds a ghost of `entity` to a pinned area and reveals it. A dropped row
// becomes a row (`compact`); a pinned card stays a card.
internal CFG_Node *
uishell_sidebar_pin_add(UIShell_SidebarState *state, CFG_Node *area, U64 index, AndamentoEntity entity,
                        String8 fallback_label, String8 source, B32 compact)
{
  CFG_Node *saved = cfg_node_new(rd_state->cfg, area, str8_lit("card"));
  uishell_sidebar_pin_place(area, saved, index);
  uishell_sidebar_pin_new_ghost(saved);
  uishell_sidebar_pin_set_field(saved, str8_lit("kind"), uishell_sidebar_string(entity.kind));
  uishell_sidebar_pin_set_field(saved, str8_lit("entity"), uishell_sidebar_string(entity.id));
  AndamentoNode node = {0};
  String8 label = uishell_sidebar_card_find(state, entity, &node) != ANDAMENTO_NONE ? uishell_sidebar_string(node.label) : fallback_label;
  uishell_sidebar_pin_set_field(saved, str8_lit("label"), label);
  uishell_sidebar_pin_set_field(saved, str8_lit("source"), source);
  if(compact) { cfg_node_new(rd_state->cfg, saved, str8_lit("compact")); }
  state->pin_reveal = saved->id;
  return saved;
}

internal CFG_Node *uishell_sidebar_pin_area(RD_WindowState *ws, B32 new_area);
internal CFG_Node *uishell_sidebar_local_view(CFG_Node *window, CFG_Node *group);
internal CFG_Node *uishell_sidebar_section_view_new(RD_Arrangement *arrangement, RD_PanelID panel, CFG_ID prev, String8 key, String8 label);
internal CFG_ID uishell_sidebar_last_tab(RD_Arrangement *arrangement, RD_PanelID panel);
internal void uishell_sidebar_select_view(CFG_Node *view);

internal CFG_Node *
uishell_sidebar_card_pin(RD_WindowState *ws, UIShell_HoverCard *card, B32 new_area)
{
  UIShell_SidebarState *state = ws->sidebar;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  AndamentoEntity entity = card->path[card->depth-1];
  // A pinned card moves its own ghost. Pin on any other card reveals the
  // entity's ghost in the group Pin uses (the topmost showing one), or adds
  // one there; a drop is explicit placement, so it always adds one.
  CFG_Node *saved = card->saved ? cfg_node_from_id(card->saved) : &cfg_nil_node;
  CFG_Node *reveal = saved;
  CFG_Node *target = uishell_sidebar_pin_target(window);
  for(CFG_Node *c = target->first; reveal == &cfg_nil_node && c != &cfg_nil_node; c = c->next)
  {
    if(!str8_match(c->string, str8_lit("card"), 0)) { continue; }
    AndamentoEntity pinned = {uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("kind"))->first->string),
                              uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("entity"))->first->string)};
    if(uishell_sidebar_card_entity_match(pinned, entity)) { reveal = c; }
  }
  if(reveal != &cfg_nil_node && !new_area)
  {
    saved = reveal;
    // Revealing an existing tab changes selection, not placement: clear its
    // siblings (including non-pinned tabs) without a creation/close check.
    CFG_Node *view = uishell_sidebar_local_view(window, saved->parent);
    state->pin_reveal = saved->id;
    if(view != &cfg_nil_node)
    {
      uishell_sidebar_select_view(view);
      uishell_sidebar_section_set_collapsed(view, 0);
    }
    for(UIShell_HoverCard *c = state->detached; c; c = c->next)
    { if(c->saved == saved->id) { c->scroll = 0; c->focused = 1; } }
    return saved;
  }
  CFG_Node *area = uishell_sidebar_pin_area(ws, new_area);
  if(area == &cfg_nil_node) { return &cfg_nil_node; }
  if(saved != &cfg_nil_node)
  {
    // A center drop onto its current area reveals the pin in place. Passing
    // the last child as both predecessor and inserted node corrupts the list.
    if(saved->parent != area) { cfg_node_insert_child(rd_state->cfg, area, area->last, saved); }
    state->pin_reveal = saved->id;
    return saved;
  }
  return uishell_sidebar_pin_add(state, area, max_U64, entity, card->retained_label, card->source_key, 0);
}

// The section View a local group shows in, if it is open.
internal CFG_Node *uishell_sidebar_region_view(CFG_Node *owner, String8 key);

internal CFG_Node *
uishell_sidebar_local_view(CFG_Node *window, CFG_Node *group)
{
  Temp scratch = scratch_begin(0, 0);
  String8 key = uishell_sidebar_local_key(scratch.arena, uishell_sidebar_local_field(group->parent, str8_lit("id")));
  CFG_Node *view = uishell_sidebar_region_view(window, key);
  scratch_end(scratch);
  return view;
}

// The first group of the local section a View shows, or nil.
internal CFG_Node *
uishell_sidebar_local_view_group(CFG_Node *window, CFG_Node *view)
{
  if(!str8_match(view->string, str8_lit("sidebar_section"), 0)) { return &cfg_nil_node; }
  String8 key = cfg_node_child_from_string(view, str8_lit("section"))->first->string;
  String8 id = uishell_sidebar_local_key_id(key);
  if(!id.size) { return &cfg_nil_node; }
  CFG_Node *root = uishell_sidebar_local_tree(window);
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    if(!str8_match(uishell_sidebar_local_field(section, str8_lit("id")), id, 0)) { continue; }
    for(CFG_Node *group = section->first; group != &cfg_nil_node; group = group->next)
    { if(str8_match(group->string, str8_lit("group"), 0)) { return group; } }
  }
  return &cfg_nil_node;
}

// A new, empty local section, last among the sidebar's.
internal CFG_Node *
uishell_sidebar_local_new_section(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *section = cfg_node_new(rd_state->cfg, uishell_sidebar_local_root(window), str8_lit("section"));
  uishell_sidebar_local_set_field(section, str8_lit("id"), string_from_guid(scratch.arena, make_guid()));
  scratch_end(scratch);
  return section;
}

// A new local section holding one group named `label`, whose name the
// section borrows (uishell_sidebar_local_title); returns the group.
internal CFG_Node *
uishell_sidebar_local_new_group(CFG_Node *window, String8 label)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *section = uishell_sidebar_local_new_section(window);
  CFG_Node *group = cfg_node_new(rd_state->cfg, section, str8_lit("group"));
  uishell_sidebar_local_set_field(group, str8_lit("id"), string_from_guid(scratch.arena, make_guid()));
  uishell_sidebar_local_set_field(group, str8_lit("label"), label);
  scratch_end(scratch);
  return group;
}

// A selected section View showing local `section`, in `panel`, after
// `prev` (nil: first).
internal CFG_Node *
uishell_sidebar_local_new_view_after(CFG_Node *panel, CFG_Node *section, CFG_Node *prev)
{
  Temp scratch = scratch_begin(0, 0);
  String8 key = uishell_sidebar_local_key(scratch.arena, uishell_sidebar_local_field(section, str8_lit("id")));
  String8 label = uishell_sidebar_local_title(scratch.arena, section);
  RD_Arrangement *arrangement = uishell_arrangement_from_cfg(scratch.arena, panel);
  RD_PanelID panel_id = rd_arrangement_panel_from_cfg(arrangement, panel->id)->id;
  CFG_Node *view = &cfg_nil_node;
  if(panel_id != 0)
  {
    view = uishell_sidebar_section_view_new(arrangement, panel_id, prev->id, key, label);
    rd_arrangement_save(rd_state->cfg, arrangement);
  }
  scratch_end(scratch);
  return view;
}

// The same, after `panel`'s last tab.
internal CFG_Node *
uishell_sidebar_local_new_view(CFG_Node *panel, CFG_Node *section)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *arrangement = uishell_arrangement_from_cfg(scratch.arena, panel);
  RD_PanelID panel_id = rd_arrangement_panel_from_cfg(arrangement, panel->id)->id;
  CFG_Node *prev = cfg_node_from_id(uishell_sidebar_last_tab(arrangement, panel_id));
  scratch_end(scratch);
  return uishell_sidebar_local_new_view_after(panel, section, prev);
}

// The local group a pin goes to (drag-model.md, "Sections and groups as
// data"). With `new_area`, the one at the drag's docking site: a centre drop
// joins the first group of a section shown there; otherwise a new section
// and group, shown in a View at the site. Without, the group Pin uses (the
// topmost showing one, uishell_sidebar_pin_target), or a new "Pinned"
// section at the top of the sidebar. Nil when the destination can't take one.
internal CFG_Node *
uishell_sidebar_pin_area(RD_WindowState *ws, B32 new_area)
{
  UIShell_SidebarState *state = ws->sidebar;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *destination = cfg_node_from_id(state->drop_panel);
  CFG_Node *area = new_area ? &cfg_nil_node : uishell_sidebar_pin_target(window);
  if(new_area && destination != &cfg_nil_node)
  {
    if(!rd_dock_can_create(str8_lit("sidebar_section"), destination)) { return &cfg_nil_node; }
    if(state->drop_direction == Dir2_Invalid)
    {
      for(CFG_Node *v = destination->first; v != &cfg_nil_node && area == &cfg_nil_node; v = v->next)
      { area = uishell_sidebar_local_view_group(window, v); }
    }
    if(area == &cfg_nil_node)
    {
      area = uishell_sidebar_local_new_group(window, str8_lit("Pinned"));
      CFG_Node *view = uishell_sidebar_local_new_view(destination, area->parent);
      if(state->drop_direction != Dir2_Invalid)
      {
        uishell_cmd("split_panel", .window = ws->cfg_id, .dst_panel = destination->id,
          .panel = destination->id, .view = view->id, .dir2 = state->drop_direction);
      }
    }
    // Keep the allocation selected by the same split geometry shown during drag.
    uishell_sidebar_manual_sizing(window, 1);
  }
  if(area == &cfg_nil_node)
  {
    // Validate a tentative destination before lifting tabs or changing ratios.
    CFG_Node prospective_root = {.string = RD_DOCK_SIDEBAR_ROOT, .parent = window};
    CFG_Node prospective_panel = {.string = str8_lit("1"), .parent = &prospective_root};
    if(!rd_dock_can_create(str8_lit("sidebar_section"), &prospective_panel)) { return &cfg_nil_node; }
    // An equal share of the sidebar (a sidebar merged into one panel keeps
    // its tabs in a panel of their own), before a requested panel, else at
    // the top, where a new pin is seen.
    Temp scratch = scratch_begin(0, 0);
    RD_Arrangement *sidebar = rd_arrangement_from_owner(scratch.arena, window, RD_DOCK_SIDEBAR_ROOT);
    RD_PanelID panel = rd_arrangement_add(sidebar, sidebar->root->id, 0);
    RD_ArrangementPanel *before = rd_arrangement_panel_from_cfg(sidebar, state->pin_before);
    rd_arrangement_reorder(sidebar, panel, before->parent == sidebar->root ? before->prev->id : 0);
    area = uishell_sidebar_local_new_group(window, str8_lit("Pinned"));
    String8 key = uishell_sidebar_local_key(scratch.arena, uishell_sidebar_local_field(area->parent, str8_lit("id")));
    uishell_sidebar_section_view_new(sidebar, panel, 0, key, uishell_sidebar_local_title(scratch.arena, area->parent));
    rd_arrangement_save(rd_state->cfg, sidebar);
    scratch_end(scratch);
  }
  return area;
}

internal size_t
uishell_sidebar_detached_content(UIShell_SidebarState *state, RD_WindowState *ws, UIShell_HoverCard *card,
                                U64 slot, AndamentoNode node, U64 index, F32 width, B32 interactive)
{
  if(index != ANDAMENTO_NONE)
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
  return ANDAMENTO_NONE;
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

internal void
uishell_sidebar_card_saved_release(UIShell_HoverCard *card)
{
  CFG_Node *saved = cfg_node_from_id(card->saved);
  if(saved != &cfg_nil_node) { cfg_node_release(rd_state->cfg, saved); }
  card->saved = 0;
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
  if(!uishell_sidebar_card_target_valid(ws, placement, width)) { return; }
  if(placement == UIShell_CardPlacement_Inline && !card->source_key.size) { return; }
  if(placement == UIShell_CardPlacement_Pinned)
  {
    CFG_Node *saved = uishell_sidebar_card_pin(ws, card, drag);
    if(saved == &cfg_nil_node) { return; }
    if(card->saved == saved->id) { return; }
    uishell_sidebar_card_close(card);
  }
  else
  {
    UIShell_HoverCard *c = card;
    if(card->placement == UIShell_CardPlacement_Transient)
    { c = uishell_sidebar_detached_copy(ws, card); uishell_sidebar_card_close(card); }
    if(c->saved)
    { uishell_sidebar_card_saved_release(c); }
    c->placement = placement;
    if(placement == UIShell_CardPlacement_Inline)
    {
      c->source_row = c->source_key;
      for(U64 i = 0; ws->sidebar->snapshot && i < andamento_snapshot_node_count(ws->sidebar->snapshot); i++)
      {
        AndamentoNode node = {0}; uishell_sidebar_snapshot_node(ws->sidebar->snapshot, i, &node);
        if(str8_match(c->source_key, uishell_sidebar_string(node.key), 0) &&
           str8_match(uishell_sidebar_string(node.layout), str8_lit("inline"), 0) && node.parent != ANDAMENTO_NONE)
        {
          AndamentoNode parent = {0}; uishell_sidebar_snapshot_node(ws->sidebar->snapshot, node.parent, &parent);
          c->source_row = push_str8_copy(c->arena, uishell_sidebar_string(parent.key)); break;
        }
      }
    }
    c->source_seen = 0;
    c->previous = (AndamentoEntity){0};
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
internal void uishell_sidebar_local_move_group(CFG_Node *window, CFG_Node *group, CFG_Node *section, CFG_Node *after);
internal CFG_Node *uishell_sidebar_section_drag_group(CFG_Node *window);
internal CFG_Node *uishell_sidebar_section_drag_section(CFG_Node *window);
internal void uishell_sidebar_local_drop_groups(CFG_Node *window, CFG_Node *from_section, CFG_Node *group, CFG_Node *section, CFG_Node *after);
internal B32 uishell_sidebar_local_is_default(CFG_Node *group);

internal void
uishell_sidebar_drag_clear(UIShell_SidebarState *state)
{
  if(state->drag_card) { state->drag_card->moving = state->drag_card->drag_released = 0; }
  state->drag_card = 0;
  state->row_drag_key = state->row_drag_loop = state->row_drag_label = str8_zero();
  state->row_drag_entity = (AndamentoEntity){0};
  state->row_drag_released = 0;
  state->drop_panel = 0; state->reorder_build = 0;
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

// Applies that claim: the carried groups go into the section, at the gap.
internal B32
uishell_sidebar_groups_drop(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *section, CFG_Node *group)
{
  if(!uishell_sidebar_groups_claimed(state)) { return 0; }
  CFG_Node *target = uishell_sidebar_local_section(window, state->groups_claim_section);
  if(target == &cfg_nil_node || target == section) { return 0; }
  CFG_Node *after = state->groups_claim_after.size ? uishell_sidebar_local_group(window, state->groups_claim_after) : &cfg_nil_node;
  uishell_sidebar_local_drop_groups(window, section, group, target, after);
  state->groups_claim_build = 0;
  return 1;
}

// Saves the claimed group's item order with `placed` at the claimed index.
// An empty group has no run yet, and its first item needs no order.
internal void
uishell_sidebar_group_order(UIShell_SidebarState *state, AndamentoEntity placed)
{
  if(!state->group_claim_loop.size) { return; }
  Arena *arena = ui_build_arena();
  AndamentoEntity *all = 0;
  U64 total = uishell_sidebar_siblings(arena, state->snapshot, state->group_claim_loop, &all);
  U64 index = state->group_claim_index;
  AndamentoEntity *order = push_array(arena, AndamentoEntity, total+1);
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
// render applies (it owns the snapshot).
internal void
uishell_sidebar_reorder_commit(UIShell_SidebarState *state)
{
  Arena *arena = ui_build_arena();
  AndamentoEntity *all = 0;
  U64 total = uishell_sidebar_siblings(arena, state->snapshot, state->row_drag_loop, &all);
  AndamentoEntity *order = push_array(arena, AndamentoEntity, total+1);
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
    CFG_Node *dragged = uishell_sidebar_section_drag_section(window);
    if(dragged != &cfg_nil_node && rd_state->drag_drop_state == RD_DragDropState_Dropping &&
       uishell_sidebar_groups_drop(state, window, dragged, &cfg_nil_node))
    {
      rd_drag_kill();
      rd_request_frame();
    }
    return;
  }
  B32 released = row ? state->row_drag_released : card->drag_released;
  if((card && !card->open) || (!rd_drag_is_active() && !released && !state->drop_panel))
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
  AndamentoEntity entity = card ? card->path[card->depth-1] : state->row_drag_entity;
  String8 label = card ? card->retained_label : state->row_drag_label;
  String8 source = card ? card->source_key : state->row_drag_key;
  CFG_Node *own = card && card->saved ? cfg_node_from_id(card->saved) : &cfg_nil_node;
  // A local group's insertion point (drag-model.md, drop table): a local
  // workspace moves there (Option/Alt adds a ghost of it instead), a pinned
  // card moves its own ghost there, and anything else becomes a ghost there,
  // a row as a row and a card as a card. A committed edge site still wins.
  B32 edge = state->drop_panel && state->drop_direction != Dir2_Invalid;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *group = !edge && uishell_sidebar_group_claimed(state) ? uishell_sidebar_local_group(window, state->group_claim_id) : &cfg_nil_node;
  // A local group's header moves the group: over a group in another
  // section, into that section after it; on a docking site, into the
  // section shown there, or a new section of its own, which borrows its
  // name. Within its own section it reorders, below; it never ghosts.
  CFG_Node *dragged_group = row && str8_match(uishell_sidebar_string(entity.kind), str8_lit(".group"), 0) ?
    uishell_sidebar_local_group(window, uishell_sidebar_string(entity.id)) : &cfg_nil_node;
  if(dragged_group != &cfg_nil_node)
  {
    // Between another section's groups, it goes there (#282).
    if(!edge && uishell_sidebar_groups_drop(state, window, &cfg_nil_node, dragged_group))
    { uishell_sidebar_drag_clear(state); return; }
    // On a section's tab strip, the group joins as a tab at the gap (#282):
    // a new section of its own, shown after the tab the gap follows.
    CFG_Node *strip = cfg_node_from_id(state->drop_panel);
    if(group == &cfg_nil_node && strip != &cfg_nil_node && state->drop_direction == Dir2_Invalid &&
       !uishell_sidebar_local_is_default(dragged_group) && rd_dock_can_create(str8_lit("sidebar_section"), strip))
    {
      CFG_Node *section = uishell_sidebar_local_new_section(window);
      uishell_sidebar_local_move_group(window, dragged_group, section, &cfg_nil_node);
      CFG_Node *previous = cfg_node_from_id(state->drop_previous_tab);
      uishell_sidebar_local_new_view_after(strip, section, previous->parent == strip ? previous : &cfg_nil_node);
      uishell_sidebar_drag_clear(state);
      return;
    }
    if(group == &cfg_nil_node && state->drop_panel)
    {
      CFG_Node *root = uishell_sidebar_local_tree(window);
      CFG_Node *last_section = root->last;
      CFG_Node *made = uishell_sidebar_pin_area(ws, 1);
      if(made != &cfg_nil_node && made->parent != dragged_group->parent)
      {
        // A section made for the drop holds only the group pin_area made for
        // pins; the dragged group replaces it.
        CFG_Node *section = made->parent;
        B32 fresh = section != last_section && root->last == section;
        if(fresh) { cfg_node_release(rd_state->cfg, made); }
        uishell_sidebar_local_move_group(window, dragged_group, section, fresh ? &cfg_nil_node : made);
      }
      uishell_sidebar_drag_clear(state);
      return;
    }
    group = &cfg_nil_node;
  }
  if(group != &cfg_nil_node)
  {
    String8 group_id = uishell_sidebar_local_field(group, str8_lit("id"));
    CFG_Node *workspace = row ? uishell_sidebar_drag_local(state) : &cfg_nil_node;
    B32 as_ghost = !!((wm_get_modifiers()|uishell_sidebar_test_modifiers) & WM_Modifier_Alt);
    AndamentoEntity placed = {0};
    Temp scratch = scratch_begin(0, 0);
    if(workspace != &cfg_nil_node && !as_ghost)
    {
      // One home: this group.
      cfg_node_release(rd_state->cfg, cfg_node_child_from_string(workspace, str8_lit("lives_with")));
      uishell_sidebar_local_set_field(workspace, str8_lit("lives_in"), group_id);
      placed = (AndamentoEntity){uishell_sidebar_text(str8_lit(".workspace")),
                                 uishell_sidebar_text(push_str8_copy(scratch.arena, uishell_sidebar_local_entity(workspace)))};
    }
    else
    {
      // A ghost's own row moves that ghost, as its card does.
      if(row && str8_match(uishell_sidebar_string(entity.kind), str8_lit(".ref"), 0))
      { own = uishell_sidebar_pin_by_ghost(window, uishell_sidebar_string(entity.id)); }
      CFG_Node *saved = own;
      if(own != &cfg_nil_node) { uishell_sidebar_pin_place(group, own, max_U64); state->pin_reveal = own->id; }
      else
      {
        saved = uishell_sidebar_pin_add(state, group, max_U64, entity, label, source, row);
        if(card) { uishell_sidebar_card_close(card); }
      }
      placed = (AndamentoEntity){uishell_sidebar_text(str8_lit(".ref")),
                                 uishell_sidebar_text(push_str8_copy(scratch.arena, uishell_sidebar_pin_ghost(saved)))};
    }
    uishell_sidebar_group_order(state, placed);
    scratch_end(scratch);
    uishell_sidebar_drag_clear(state);
    return;
  }
  CFG_Node *area = &cfg_nil_node;
  U64 index = max_U64;
  UIShell_CardPlacement placement = card ? uishell_sidebar_card_drag_target(ws, card, ui_mouse()) : UIShell_CardPlacement_Float;
  B32 docked = area == &cfg_nil_node && placement != UIShell_CardPlacement_Inline && state->drop_panel;
  // A site that can't take a pinned area leaves `area` nil: the drop falls
  // through to the row's or card's own targets below.
  if(docked) { area = uishell_sidebar_pin_area(ws, 1); index = max_U64; }
  if(area != &cfg_nil_node)
  {
    if(own != &cfg_nil_node) { uishell_sidebar_pin_place(area, own, index); state->pin_reveal = own->id; }
    else
    {
      uishell_sidebar_pin_add(state, area, index, entity, label, source, row);
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
}

internal void
uishell_sidebar_detached_finish(RD_WindowState *ws)
{
  for(U64 i = 0; i < ArrayCount(ws->sidebar->cards); i++)
  { uishell_sidebar_detached_apply(ws, &ws->sidebar->cards[i]); }
  for(UIShell_HoverCard *c = ws->sidebar->detached; c; c = c->next)
  {
    if(c->saved && cfg_node_from_id(c->saved) == &cfg_nil_node)
    { c->saved = 0; uishell_sidebar_card_close(c); }
    uishell_sidebar_detached_apply(ws, c);
    if(!c->open && c->saved)
    { uishell_sidebar_card_saved_release(c); }
    if(c->open && c->saved && c->depth == 1)
    {
      CFG_Node *saved = cfg_node_from_id(c->saved);
      CFG_Node *label = cfg_node_child_from_string(saved, str8_lit("label"));
      if(!str8_match(label->first->string, c->retained_label, 0))
      { uishell_sidebar_pin_set_field(saved, str8_lit("label"), c->retained_label); }
    }
    if(c->placement == UIShell_CardPlacement_Inline && c->open)
    {
      B32 present = 0;
      for(U64 i = 0; ws->sidebar->snapshot && i < andamento_snapshot_node_count(ws->sidebar->snapshot); i++)
      {
        AndamentoNode node = {0}; uishell_sidebar_snapshot_node(ws->sidebar->snapshot, i, &node);
        if(str8_match(c->source_key, uishell_sidebar_string(node.key), 0)) { present = 1; break; }
      }
      if(!present || uishell_sidebar_card_find(ws->sidebar, c->path[c->depth-1], 0) == ANDAMENTO_NONE)
      { uishell_sidebar_card_close(c); }
    }
  }
}

// Read pinned identities from layout state. Filtering the tree never removes a
// saved card; missing entities use the saved label and an explicit ended state.
internal UIShell_HoverCard *
uishell_sidebar_saved_card(RD_WindowState *ws, CFG_Node *saved)
{
  for(UIShell_HoverCard *c = ws->sidebar->detached; c; c = c->next)
  { if(c->saved == saved->id) { return c; } }
  UIShell_HoverCard *c = uishell_sidebar_detached_alloc(ws);
  c->arena = arena_alloc(); c->capacity = UIShell_HoverCardInitialPathCapacity; c->depth = 1;
  c->path = push_array(c->arena, AndamentoEntity, c->capacity);
  AndamentoEntity entity = {uishell_sidebar_text(cfg_node_child_from_string(saved, str8_lit("kind"))->first->string),
                           uishell_sidebar_text(cfg_node_child_from_string(saved, str8_lit("entity"))->first->string)};
  c->path[0] = uishell_sidebar_card_entity_copy(c->arena, entity);
  c->source_key = push_str8_copy(c->arena, cfg_node_child_from_string(saved, str8_lit("source"))->first->string);
  uishell_sidebar_card_retain_label(c, cfg_node_child_from_string(saved, str8_lit("label"))->first->string);
  c->saved = saved->id; c->placement = UIShell_CardPlacement_Pinned; c->open = c->engaged = 1;
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
          AndamentoNode node = {0}; U64 index = uishell_sidebar_card_find(ws->sidebar, c->path[c->depth-1], &node);
          size_t action = uishell_sidebar_detached_content(ws->sidebar, ws, c, (U64)c, node, index, content_width, 1);
          if(action != ANDAMENTO_NONE) { uishell_sidebar_card_queue_action(ws->sidebar, node, action); }
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
internal String8 uishell_sidebar_status_mark(AndamentoNode node, String8 status);
internal String8 uishell_sidebar_chip_status(UIShell_SidebarState *state, AndamentoNode node);

// Pinning a card keeps the card. Its header's disclosure collapses the ghost
// to a compact row (saved as `compact`), and the row's disclosure restores it.
internal B32
uishell_sidebar_pin_expanded(CFG_Node *saved)
{
  return cfg_node_child_from_string(saved, str8_lit("compact")) == &cfg_nil_node;
}

internal F32
uishell_sidebar_ghost_extent(UIShell_HoverCard *card, CFG_Node *saved, F32 em)
{
  return uishell_sidebar_pin_expanded(saved) ? uishell_sidebar_pinned_card_extent(card, em) : floor_f32(em*2.2f);
}

internal void
uishell_sidebar_ghost_set_expanded(CFG_Node *saved, B32 expanded)
{
  if(!expanded) { cfg_node_child_from_string_or_alloc(rd_state->cfg, saved, str8_lit("compact")); }
  else { cfg_node_release(rd_state->cfg, cfg_node_child_from_string(saved, str8_lit("compact"))); }
  rd_request_frame();
}

// An expanded ghost: its card, `width` wide, drawn in its place in a list.
internal void
uishell_sidebar_ghost_card(UIShell_SidebarState *state, RD_WindowState *ws, UIShell_HoverCard *c, CFG_Node *saved, F32 card_width)
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
        UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder, "###pinned_card_%I64u", saved->id);
      c->mask.key = body->key;
      UI_Parent(body) UI_PrefHeight(ui_em(1.6f, 1)) UI_FocusHot(UI_FocusKind_Root) UI_FocusActive(UI_FocusKind_Root)
      {
        ui_spacer(ui_px(4.f, 1));
        ui_set_next_fixed_x(6.f);
        UI_PrefWidth(ui_px(content_width, 1)) UI_PrefHeight(ui_children_sum(1)) UI_Column UI_PrefHeight(ui_em(1.6f, 1))
        {
          AndamentoNode node = {0}; U64 index = uishell_sidebar_card_find(state, c->path[c->depth-1], &node);
          size_t action = uishell_sidebar_detached_content(state, ws, c, saved->id, node, index, content_width, 1);
          if(action != ANDAMENTO_NONE) { uishell_sidebar_card_queue_action(state, node, action); }
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
