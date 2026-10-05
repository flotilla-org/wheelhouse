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
  return rd_dock_check(rd_dock_view_from_name(str8_lit("pinned_cards")), p) == RD_DockRule_Valid;
}

internal void
uishell_sidebar_card_request(UIShell_HoverCard *card, UIShell_CardPlacement placement)
{
  card->requested = placement;
  card->move_requested = 1;
  rd_request_frame();
}

// The prototype uses 16px outline paths. Draw those strokes directly so these
// controls share one light weight independent of the filled symbol font.
internal void
uishell_sidebar_card_icon_stroke(Vec2F32 a, Vec2F32 b, Vec4F32 color)
{
  Vec2F32 delta = sub_2f32(b, a);
  F32 length = length_2f32(delta);
  if(length == 0) { return; }
  Vec2F32 direction = scale_2f32(delta, 1.f/length);
  Mat3x3F32 transform = mat_3x3f32(1);
  transform.v[0][0] = direction.x; transform.v[0][1] = direction.y;
  transform.v[1][0] = -direction.y; transform.v[1][1] = direction.x;
  transform.v[2][0] = a.x; transform.v[2][1] = a.y;
  DR_XForm2DScope(mul_3x3f32(dr_top_xform2d(), transform))
  { dr_rect(r2f32p(-.65f, -.65f, length+.65f, .65f), color, .65f, 0, .5f); }
}

UI_BOX_CUSTOM_DRAW(uishell_sidebar_card_icon_draw)
{
  RD_IconKind kind = (RD_IconKind)IntFromPtr(user_data);
  F32 size = Min(14.f, Min(dim_2f32(box->rect).x, dim_2f32(box->rect).y)-4.f);
  if(size <= 0) { return; }
  Vec2F32 origin = sub_2f32(center_2f32(box->rect), v2f32(size/2, size/2));
  Mat3x3F32 transform = mul_3x3f32(make_translate_3x3f32(origin), make_scale_3x3f32(v2f32(size/16, size/16)));
  Vec4F32 color = box->text_color;
  DR_XForm2DScope(mul_3x3f32(dr_top_xform2d(), transform))
  {
    Vec2F32 points[12] = {0};
    U64 count = 0;
    switch(kind)
    {
      case RD_IconKind_Pin:
      {
        Vec2F32 path[] = {{6,2.5f},{10,2.5f},{9.5f,6.5f},{12,9},{4,9},{6.5f,6.5f},{6,2.5f}};
        count = ArrayCount(path); MemoryCopy(points, path, sizeof(path));
        uishell_sidebar_card_icon_stroke(v2f32(8,9), v2f32(8,13.5f), color);
      }break;
      case RD_IconKind_DownArrow:
      {
        Vec2F32 path[] = {{5,7.5f},{8,10.5f},{11,7.5f}};
        count = ArrayCount(path); MemoryCopy(points, path, sizeof(path));
        uishell_sidebar_card_icon_stroke(v2f32(8,2.5f), v2f32(8,10.5f), color);
        uishell_sidebar_card_icon_stroke(v2f32(2.5f,13.5f), v2f32(13.5f,13.5f), color);
      }break;
      case RD_IconKind_Window:
      {
        Vec2F32 path[] = {{3,5},{11,5},{11,13},{3,13},{3,5}};
        count = ArrayCount(path); MemoryCopy(points, path, sizeof(path));
        uishell_sidebar_card_icon_stroke(v2f32(6,2.5f), v2f32(13.5f,2.5f), color);
        uishell_sidebar_card_icon_stroke(v2f32(13.5f,2.5f), v2f32(13.5f,10), color);
      }break;
      case RD_IconKind_X:
      {
        uishell_sidebar_card_icon_stroke(v2f32(4,4), v2f32(12,12), color);
        uishell_sidebar_card_icon_stroke(v2f32(12,4), v2f32(4,12), color);
      }break;
      case RD_IconKind_Info:
      {
        dr_rect(r2f32p(2.5f,2.5f,13.5f,13.5f), color, 5.5f, 1.3f, .5f);
        dr_rect(r2f32p(7.35f,4.5f,8.65f,5.8f), color, .65f, 0, .5f);
        uishell_sidebar_card_icon_stroke(v2f32(8,7.5f), v2f32(8,11.5f), color);
      }break;
      default: break;
    }
    for(U64 i = 1; i < count; i++) { uishell_sidebar_card_icon_stroke(points[i-1], points[i], color); }
  }
}

// Card furniture follows the prototype header: a quiet grip at the left,
// compact destination icons at the right, and labelled entity actions below.
internal UI_Signal
uishell_sidebar_card_icon_button(String8 glyph, String8 key, String8 description)
{
  UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText|
    UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects|UI_BoxFlag_DisableTruncatedHover,
    "%S###%S", glyph, key);
  // Retain the widget label and resolved text color; the outline owns paint.
  box->flags &= ~UI_BoxFlag_DrawText;
  RD_IconKind kinds[] = {RD_IconKind_Info, RD_IconKind_Window, RD_IconKind_DownArrow, RD_IconKind_Pin, RD_IconKind_X};
  for(U64 i = 0; i < ArrayCount(kinds); i++)
  {
    if(str8_match(glyph, rd_icon_kind_text_table[kinds[i]], 0))
    { ui_box_equip_custom_draw(box, uishell_sidebar_card_icon_draw, PtrFromInt((U64)kinds[i])); break; }
  }
  UI_Signal signal = ui_signal_from_box(box);
  if(ui_hovering(signal)) UI_Tooltip
  {
    ui_state->tooltip_anchor_key = box->key;
    RD_Font(RD_FontSlot_Main) { ui_label(description); }
  }
  return signal;
}

internal void
uishell_sidebar_card_panel_drop(CFG_ID destination, Dir2 direction, CFG_ID previous_tab)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(cfg_node_from_id(rd_state->drag_drop_regs->window));
  if(ws == &rd_nil_window_state || !ws->sidebar || !ws->sidebar->drag_card) { return; }
  ws->sidebar->card_drop_panel = destination;
  ws->sidebar->card_drop_direction = direction;
}

internal void
uishell_sidebar_card_drag_control(UIShell_HoverCard *card)
{
  UI_Signal drag = uishell_sidebar_grip(str8_lit("card_drag"), str8_lit("Drag card"));
  if(ui_pressed(drag)) { card->move_origin = card->rect.p0; }
  if(ui_dragging(drag) && length_2f32(ui_drag_delta()) > UIShell_HoverCardDragThresholdPT)
  {
    if(!card->moving && !rd_drag_is_active())
    {
      RD_WindowState *ws = rd_window_state_from_os_handle(ui_state->window);
      if(ws != &rd_nil_window_state && ws->sidebar)
      {
        ws->sidebar->drag_card = card; ws->sidebar->card_drop_panel = 0;
        UIShell_RegsScope(.window = ws->cfg_id, .view = 0, .panel = 0, .tab = 0) { rd_drag_begin(UIShell_ContextRegSlot_View); }
        rd_state->drag_drop_creation_name = str8_lit("pinned_cards");
        rd_state->drag_drop_commit = uishell_sidebar_card_panel_drop;
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

internal void
uishell_sidebar_card_move_controls(UIShell_HoverCard *card, F32 width)
{
  UI_PrefWidth(ui_em(1.4f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center)
  UI_TagF("weak") RD_Font(RD_FontSlot_Icons)
  {
    if(card->placement != UIShell_CardPlacement_Float &&
       ui_clicked(uishell_sidebar_card_icon_button(rd_icon_kind_text_table[RD_IconKind_Window], str8_lit("card_float"), str8_lit("Float"))))
    { uishell_sidebar_card_request(card, UIShell_CardPlacement_Float); }
    if(card->placement != UIShell_CardPlacement_Inline)
    {
      UI_Flags(card->source_key.size ? 0 : UI_BoxFlag_Disabled)
      { if(ui_clicked(uishell_sidebar_card_icon_button(rd_icon_kind_text_table[RD_IconKind_DownArrow], str8_lit("card_inline"), str8_lit("Dock under source"))))
        { uishell_sidebar_card_request(card, UIShell_CardPlacement_Inline); } }
    }
    if(card->placement != UIShell_CardPlacement_Pinned &&
       ui_clicked(uishell_sidebar_card_icon_button(rd_icon_kind_text_table[RD_IconKind_Pin], str8_lit("card_pin"), str8_lit("Pin"))))
    { uishell_sidebar_card_request(card, UIShell_CardPlacement_Pinned); }
    if(ui_clicked(uishell_sidebar_card_icon_button(rd_icon_kind_text_table[RD_IconKind_X], str8_lit("card_close"), str8_lit("Close"))))
    { uishell_sidebar_card_close(card); rd_request_frame(); }
  }
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
    { linked |= m == &c->mask; }
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
internal CFG_Node *
uishell_sidebar_pin_find(CFG_Node *root, AndamentoEntity entity, B32 area_only)
{
  for(CFG_Node *v = root->first; v != &cfg_nil_node; v = v->next)
  {
    if(str8_match(v->string, str8_lit("pinned_cards"), 0))
    {
      if(area_only) { return v; }
      for(CFG_Node *c = v->first; c != &cfg_nil_node; c = c->next)
      {
        if(!str8_match(c->string, str8_lit("card"), 0)) { continue; }
        AndamentoEntity saved = {uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("kind"))->first->string),
                                 uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("entity"))->first->string)};
        if(uishell_sidebar_card_entity_match(saved, entity)) { return c; }
      }
    }
    else if(rd_dock_is_container(v))
    {
      CFG_Node *found = uishell_sidebar_pin_find(v, entity, area_only);
      if(found != &cfg_nil_node) { return found; }
    }
  }
  return &cfg_nil_node;
}

// As in pin_find, missing kind/entity fields read as empty through the nil sentinel.
internal void
uishell_sidebar_pin_deduplicate(CFG_Node *window, CFG_Node *root)
{
  for(CFG_Node *v = root->first; v != &cfg_nil_node; v = v->next)
  {
    if(str8_match(v->string, str8_lit("pinned_cards"), 0))
    {
      for(CFG_Node *c = v->first, *next; c != &cfg_nil_node; c = next)
      {
        next = c->next;
        if(!str8_match(c->string, str8_lit("card"), 0)) { continue; }
        AndamentoEntity entity = {uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("kind"))->first->string),
                                 uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("entity"))->first->string)};
        if(uishell_sidebar_pin_find(window, entity, 0) != c) { cfg_node_release(rd_state->cfg, c); }
      }
    }
    else if(rd_dock_is_container(v)) { uishell_sidebar_pin_deduplicate(window, v); }
  }
}

internal void
uishell_sidebar_pin_set_field(CFG_Node *node, String8 name, String8 value)
{
  CFG_Node *field = cfg_node_child_from_string_or_alloc(rd_state->cfg, node, name);
  cfg_node_new_replace(rd_state->cfg, field, value);
}

internal CFG_Node *
uishell_sidebar_card_pin(RD_WindowState *ws, UIShell_HoverCard *card, B32 new_area)
{
  UIShell_SidebarState *state = ws->sidebar;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  AndamentoEntity entity = card->path[card->depth-1];
  CFG_Node *saved = uishell_sidebar_pin_find(window, entity, 0);
  if(saved != &cfg_nil_node && !new_area)
  {
    // Revealing an existing tab changes selection, not placement: clear its
    // siblings (including non-pinned tabs) without a creation/close check.
    CFG_Node *area = saved->parent;
    state->pin_reveal = saved->id;
    for(CFG_Node *v = area->parent->first; v != &cfg_nil_node; v = v->next)
    {
      CFG_Node *selected = cfg_node_child_from_string(v, str8_lit("selected"));
      if(selected != &cfg_nil_node) { cfg_node_release(rd_state->cfg, selected); }
    }
    cfg_node_child_from_string_or_alloc(rd_state->cfg, area, str8_lit("selected"));
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(area, str8_lit("section_collapsed")));
    for(UIShell_HoverCard *c = state->detached; c; c = c->next)
    { if(c->saved == saved->id) { c->scroll = 0; c->focused = 1; } }
    return saved;
  }
  CFG_Node *destination = cfg_node_from_id(state->card_drop_panel);
  CFG_Node *area = new_area ? &cfg_nil_node : uishell_sidebar_pin_find(window, (AndamentoEntity){0}, 1);
  if(new_area && destination != &cfg_nil_node)
  {
    if(!rd_dock_can_create(str8_lit("pinned_cards"), destination)) { return &cfg_nil_node; }
    if(state->card_drop_direction == Dir2_Invalid)
    {
      for(CFG_Node *v = destination->first; v != &cfg_nil_node; v = v->next)
      { if(str8_match(v->string, str8_lit("pinned_cards"), 0)) { area = v; break; } }
    }
    if(area == &cfg_nil_node)
    {
      area = cfg_node_new(rd_state->cfg, destination, str8_lit("pinned_cards"));
      uishell_sidebar_pin_set_field(area, str8_lit("label"), str8_lit("Pinned"));
      cfg_node_new(rd_state->cfg, area, str8_lit("selected"));
      if(state->card_drop_direction != Dir2_Invalid)
      {
        uishell_cmd("split_panel", .window = ws->cfg_id, .dst_panel = destination->id,
          .panel = destination->id, .view = area->id, .dir2 = state->card_drop_direction);
      }
    }
    // Keep the allocation selected by the same split geometry shown during drag.
    uishell_sidebar_manual_sizing(window, 1);
  }
  if(area == &cfg_nil_node)
  {
    B32 root_exists = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT) != &cfg_nil_node;
    CFG_Node *root = cfg_node_child_from_string_or_alloc(rd_state->cfg, window, RD_DOCK_SIDEBAR_ROOT);
    // Validate a tentative destination before lifting tabs or changing ratios.
    CFG_Node *panel = cfg_node_new(rd_state->cfg, root, str8_lit("1"));
    if(!rd_dock_can_create(str8_lit("pinned_cards"), panel))
    {
      cfg_node_release(rd_state->cfg, panel);
      if(!root_exists) { cfg_node_release(rd_state->cfg, root); }
      return &cfg_nil_node;
    }
    // A merged sidebar can be a leaf itself. Preserve its tabs by lifting
    // them into a child before adding a sibling panel.
    B32 split = 0;
    for(CFG_Node *n = root->first; n != &cfg_nil_node; n = n->next)
    { if(n != panel && rd_dock_is_container(n)) { split = 1; break; } }
    if(!split && (root->first != panel || panel->next != &cfg_nil_node))
    {
      CFG_Node *old = cfg_node_new(rd_state->cfg, root, str8_lit("1"));
      for(CFG_Node *n = root->first, *next; n != &cfg_nil_node; n = next)
      {
        next = n->next;
        if(n != old && n != panel) { cfg_node_insert_child(rd_state->cfg, old, old->last, n); }
      }
    }
    F32 total = 0; U64 count = 0;
    for(CFG_Node *n = root->first; n != &cfg_nil_node; n = n->next)
    {
      if(n != panel && rd_dock_is_container(n))
      {
        total += (F32)f64_from_str8(n->string);
        count++;
      }
    }
    F32 fraction = 1.f/(count+1);
    for(CFG_Node *n = root->first; n != &cfg_nil_node; n = n->next)
    {
      if(n != panel && rd_dock_is_container(n))
      {
        F32 share = total > 0 ? (F32)f64_from_str8(n->string)/total : 1.f/Max(1, count);
        cfg_node_equip_stringf(rd_state->cfg, n, "%f", (1-fraction)*share);
      }
    }
    if(root->last != panel) { cfg_node_insert_child(rd_state->cfg, root, root->last, panel); }
    CFG_Node *before = cfg_node_from_id(state->pin_before);
    cfg_node_equip_stringf(rd_state->cfg, panel, "%f", fraction);
    if(before != &cfg_nil_node && before->parent == root)
    { cfg_node_insert_child(rd_state->cfg, root, before->prev, panel); }
    area = cfg_node_new(rd_state->cfg, panel, str8_lit("pinned_cards"));
    uishell_sidebar_pin_set_field(area, str8_lit("label"), str8_lit("Pinned"));
    cfg_node_new(rd_state->cfg, area, str8_lit("selected"));
  }
  if(saved != &cfg_nil_node)
  {
    cfg_node_insert_child(rd_state->cfg, area, area->last, saved);
    state->pin_reveal = saved->id;
    return saved;
  }
  saved = cfg_node_new(rd_state->cfg, area, str8_lit("card"));
  uishell_sidebar_pin_set_field(saved, str8_lit("kind"), uishell_sidebar_string(entity.kind));
  uishell_sidebar_pin_set_field(saved, str8_lit("entity"), uishell_sidebar_string(entity.id));
  AndamentoNode node = {0};
  String8 label = uishell_sidebar_card_find(state, entity, &node) != ANDAMENTO_NONE ? uishell_sidebar_string(node.label) : card->retained_label;
  uishell_sidebar_pin_set_field(saved, str8_lit("label"), label);
  uishell_sidebar_pin_set_field(saved, str8_lit("source"), card->source_key);
  state->pin_reveal = saved->id;
  return saved;
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
  {
    UI_PrefWidth(ui_em(1.4f, 1)) { uishell_sidebar_card_drag_control(card); }
    U64 control_count = card->placement == UIShell_CardPlacement_Transient ? 4 : 3;
    UI_PrefWidth(ui_px(Max(0.f, width-ui_top_font_size()*1.4f*(control_count+1)), 1)) { ui_label(card->retained_label); }
    uishell_sidebar_card_move_controls(card, width);
  }
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
        AndamentoNode node = {0}; andamento_snapshot_node(ws->sidebar->snapshot, i, &node);
        if(str8_match(c->source_key, uishell_sidebar_string(node.key), 0) &&
           str8_match(uishell_sidebar_string(node.layout), str8_lit("inline"), 0) && node.parent != ANDAMENTO_NONE)
        {
          AndamentoNode parent = {0}; andamento_snapshot_node(ws->sidebar->snapshot, node.parent, &parent);
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

internal void
uishell_sidebar_card_drag_finish(RD_WindowState *ws)
{
  UIShell_SidebarState *state = ws->sidebar;
  UIShell_HoverCard *card = state->drag_card;
  if(!card) { return; }
  if(!card->open || (!rd_drag_is_active() && !card->drag_released && !state->card_drop_panel))
  {
    if(rd_state->drag_drop_commit == uishell_sidebar_card_panel_drop && rd_state->drag_drop_regs->window == ws->cfg_id)
    { rd_drag_kill(); }
    card->moving = card->drag_released = 0;
    state->drag_card = 0; state->card_drop_panel = 0;
    return;
  }
  if(rd_state->drag_drop_state == RD_DragDropState_Dropping) { card->drag_released = 1; }
  if(!card->drag_released) { return; }
  UIShell_CardPlacement placement = uishell_sidebar_card_drag_target(ws, card, ui_mouse());
  if(placement != UIShell_CardPlacement_Inline)
  { placement = state->card_drop_panel ? UIShell_CardPlacement_Pinned : UIShell_CardPlacement_Float; }
  rd_drag_kill();
  state->drag_card = 0;
  card->drag_released = 0;
  // A site creates or joins its exact panel; an unmatched release floats.
  card->moving = 0;
  uishell_sidebar_card_request(card, placement);
  if(placement == UIShell_CardPlacement_Pinned)
  {
    CFG_Node *saved = uishell_sidebar_card_pin(ws, card, 1);
    if(saved != &cfg_nil_node && card->saved != saved->id) { uishell_sidebar_card_close(card); }
    card->move_requested = 0;
  }
  else { uishell_sidebar_detached_apply(ws, card); }
  state->card_drop_panel = 0;
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
    if(c->placement == UIShell_CardPlacement_Inline && c->open)
    {
      B32 present = 0;
      for(U64 i = 0; ws->sidebar->snapshot && i < andamento_snapshot_node_count(ws->sidebar->snapshot); i++)
      {
        AndamentoNode node = {0}; andamento_snapshot_node(ws->sidebar->snapshot, i, &node);
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
    UI_PrefWidth(ui_px(card_width, 1)) UI_PrefHeight(ui_children_sum(1))
    UI_ChildLayoutAxis(Axis2_Y) UI_CornerRadius(5.f)
    UI_BackgroundColor(mix_4f32(ui_color_from_name(str8_lit("background")), ui_color_from_name(str8_lit("text")), .025f))
    UI_Focus(c->focused ? UI_FocusKind_On : UI_FocusKind_Off)
    {
      ui_set_next_fixed_x(6.f);
      UI_Box *root = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_DefaultFocusNavY|
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

RD_VIEW_UI_FUNCTION_DEF(pinned_cards)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  RD_WindowState *ws = rd_window_state_from_cfg__existing(rd_window_from_cfg(view));
  UIShell_SidebarState *state = ws->sidebar;
  if(!state) { return; }
  if(state->core) { uishell_sidebar_refresh(state); }
  F32 width = dim_2f32(rect).x, em = ui_top_font_size();
  F32 header_height = floor_f32(em*2.2f);
  UI_Box *root;
  // The View parent already sits at rect.p0 in window coordinates.
  UI_Rect(r2f32p(0, 0, width, dim_2f32(rect).y)) UI_ChildLayoutAxis(Axis2_Y)
  { root = ui_build_box_from_stringf(UI_BoxFlag_Clip, "###pinned_area_%I64u", view->id); }
  UI_Parent(root) UI_PrefHeight(ui_px(header_height, 1)) UI_Row
  UI_FontSize(floor_f32(em*0.82f)) UI_TagF("weak") RD_Font(RD_FontSlot_Main)
  {
    ui_spacer(ui_em(0.3f, 1));
    UI_PrefWidth(ui_em(1.5f, 1))
    {
      UI_Signal drag = uishell_sidebar_grip(str8_lit("pinned_drag"), str8_lit("Drag pinned area"));
      if(ui_dragging(drag) && !rd_drag_is_active() && length_2f32(ui_drag_delta()) > UIShell_HoverCardDragThresholdPT)
      { rd_drag_begin(UIShell_ContextRegSlot_View); }
    }
    UI_PrefWidth(ui_pct(1, 0)) { ui_label(str8_lit("PINNED")); }
    UI_PrefWidth(ui_em(1.5f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center)
    {
      if(ui_clicked(uishell_sidebar_button(str8_lit("×###pinned_close"))) && rd_dock_can_close(view))
      { uishell_cmd("close_tab"); }
    }
    ui_spacer(ui_px(4.f, 1));
  }
  UI_ScrollRegionParams params = ui_scroll_region_params(r2f32p(0, header_height, width, dim_2f32(rect).y),
    UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Auto);
  UI_Key key = ui_key_from_stringf(root->key, "body");
  UI_Box *old = ui_box_from_key(key);
  F32 height = 0;
  for(CFG_Node *saved = view->first; saved != &cfg_nil_node; saved = saved->next)
  { if(str8_match(saved->string, str8_lit("card"), 0)) { height += uishell_sidebar_pinned_card_extent(uishell_sidebar_saved_card(ws, saved), em); } }
  params.content_dim_px = v2f32(width, height);
  UI_ScrollRegion region = ui_scroll_region_layout(params);
  F32 target = ui_box_is_nil(old) ? 0 : old->view_off_target.y;
  F32 reveal_y = 0; B32 measured = 1;
  for(CFG_Node *saved = view->first; saved != &cfg_nil_node; saved = saved->next)
  {
    if(!str8_match(saved->string, str8_lit("card"), 0)) { continue; }
    UIShell_HoverCard *c = uishell_sidebar_saved_card(ws, saved);
    measured &= c->content_height > 0;
    if(saved->id == state->pin_reveal)
    {
      target = reveal_y; c->focused = 1;
      if(measured) { state->pin_reveal = 0; } else { rd_request_frame(); }
      break;
    }
    reveal_y += uishell_sidebar_pinned_card_extent(c, em);
  }
  UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
  axes[Axis2_Y] = (UI_ScrollRegionAxis){ui_scroll_pt((S64)target, target-(S64)target),
    r1s64(0, Max(0, (S64)(height-dim_2f32(region.viewport).y))), (S64)dim_2f32(region.viewport).y};
  UI_ScrollRegionSignal scroll = ui_scroll_region_build(root, key, &region, axes,
    UI_BoxFlag_ViewScrollY|UI_BoxFlag_ViewClamp|UI_BoxFlag_AllowOverflowY);
  scroll.content_box->view_off_target.y = (F32)scroll.position.y.idx+scroll.position.y.target_off;
  scroll.content_box->child_layout_axis = Axis2_Y;
  F32 card_width = Max(0.f, dim_2f32(region.viewport).x-12.f);
  F32 content_width = Max(0.f, card_width-12.f);
  UI_Parent(scroll.content_box) UI_PrefWidth(ui_px(card_width, 1))
  {
    for(CFG_Node *saved = view->first; saved != &cfg_nil_node; saved = saved->next)
    {
      if(!str8_match(saved->string, str8_lit("card"), 0)) { continue; }
      UIShell_HoverCard *c = uishell_sidebar_saved_card(ws, saved);
      // Keep the source allocation while the overlay owns the moving card.
      if(c->moving) { ui_spacer(ui_px(uishell_sidebar_pinned_card_extent(c, em), 1)); continue; }
      UI_PrefHeight(ui_children_sum(1)) UI_ChildLayoutAxis(Axis2_Y)
      UI_Focus(c->focused ? UI_FocusKind_On : UI_FocusKind_Off) UI_CornerRadius(5.f)
      UI_BackgroundColor(mix_4f32(ui_color_from_name(str8_lit("background")), ui_color_from_name(str8_lit("text")), .025f))
      {
        ui_set_next_fixed_x(6.f);
        UI_Box *body = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_DefaultFocusNavY|
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
          String8 label = cfg_node_child_from_string(saved, str8_lit("label"))->first->string;
          if(c->depth == 1 && !str8_match(label, c->retained_label, 0))
          { cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string(saved, str8_lit("label")), c->retained_label); }
          if(!c->moving) { c->rect = body->rect; c->rect.y1 = c->rect.y0+body->fixed_size.y; }
          body->flags |= UI_BoxFlag_MouseClickable; ui_signal_from_box(body);
        }
      }
      ui_spacer(ui_px(8, 1));
    }
    if(!height) { UI_PrefHeight(ui_em(2, 1)) UI_TagF("weak") { ui_label(str8_lit("Drag or pin a card here")); } }
    ui_signal_from_box(scroll.content_box);
  }
}
