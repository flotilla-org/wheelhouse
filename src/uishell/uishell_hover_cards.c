// Phase 1 cards keep the ABI 2 flat detail fields. No snapshot pointers or
// action indices survive a frame; paths own stable placement keys instead.
global B32 uishell_hover_cards_outside;

internal U64
uishell_sidebar_card_find(UIShell_SidebarState *state, String8 key, AndamentoNode *out)
{
  U64 count = state->snapshot ? andamento_snapshot_node_count(state->snapshot) : 0;
  for(U64 i = 0; i < count; i++)
  {
    AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && str8_match(uishell_sidebar_string(node.key), key, 0))
    { if(out) { *out = node; } return i; }
  }
  return ANDAMENTO_NONE;
}

internal void
uishell_sidebar_card_close(UIShell_HoverCard *card)
{
  card->dismissed = card->source;
  card->open = card->engaged = card->focused = 0;
  card->candidate = str8_zero();
  card->candidate_since = card->left_at = 0;
  card->corridor_active = 0;
}

// A bounded triangle toward the nearest card edge protects diagonal travel.
// Vertical scanning and motion away from the card never acquire the corridor.
internal B32
uishell_sidebar_card_corridor(UIShell_HoverCard *card, Vec2F32 mouse, U64 now)
{
  if(!card->open || !card->left_at || now-card->left_at > 400000) { return 0; }
  F32 edge = card->rect.x0 >= card->departure.x ? card->rect.x0 : card->rect.x1;
  F32 dx = edge-card->departure.x;
  F32 progress = (mouse.x-card->last_mouse.x)*dx;
  if(abs_f32(dx) < 1 || progress < 0 ||
     (progress == 0 && (mouse.y != card->last_mouse.y || !card->corridor_active))) { return 0; }
  F32 t = (mouse.x-card->departure.x)/dx;
  if(t < 0 || t > 1) { return 0; }
  F32 top = card->departure.y + (card->rect.y0-12-card->departure.y)*t;
  F32 bottom = card->departure.y + (card->rect.y1+12-card->departure.y)*t;
  return mouse.y >= top && mouse.y <= bottom;
}

internal void
uishell_sidebar_card_set(UIShell_HoverCard *card, AndamentoNode node, UI_Key source,
                        String8 context, B32 contains_current, U64 now)
{
  Temp scratch = scratch_begin(0, 0);
  String8 previous = card->open && card->depth ? push_str8_copy(scratch.arena, card->path[card->depth-1]) : str8_zero();
  if(!card->arena) { card->arena = arena_alloc(); }
  arena_clear(card->arena);
  card->previous = push_str8_copy(card->arena, previous);
  card->capacity = 32;
  card->path = push_array(card->arena, String8, card->capacity);
  card->path[0] = push_str8_copy(card->arena, uishell_sidebar_string(node.key));
  card->depth = 1;
  card->context = push_str8_copy(card->arena, context);
  card->contains_current = contains_current;
  card->source = source;
  card->dismissed = ui_key_zero();
  card->candidate = card->path[0];
  card->changed_at = now;
  card->glide_from = card->rect.p0;
  card->left_at = 0;
  card->corridor_active = 0;
  card->scroll = 0;
  card->open = 1;
  card->engaged = card->focused = 0;
  scratch_end(scratch);
}

internal void
uishell_sidebar_card_source_at(UIShell_SidebarState *state, AndamentoNode node,
                               UI_Signal sig, String8 context, B32 contains_current, U64 now)
{
  if(ui_any_ctx_menu_is_open() || !ui_hovering(sig)) { return; }
  UIShell_HoverCard *card = &state->cards[0];
  if(card->focused || ui_key_match(card->dismissed, sig.box->key)) { return; }
  if(card->corridor_active) { return; }
  String8 key = uishell_sidebar_string(node.key);
  if(card->open && card->depth && str8_match(card->path[0], key, 0))
  { card->source_seen = 1; return; }
  if(!str8_match(card->candidate, key, 0))
  {
    if(card->open)
    { uishell_sidebar_card_set(card, node, sig.box->key, context, contains_current, now); }
    else
    {
      if(!card->arena) { card->arena = arena_alloc(); }
      arena_clear(card->arena);
      card->candidate = push_str8_copy(card->arena, key);
      card->candidate_since = now;
    }
  }
  if(!card->open && now-card->candidate_since >= 300000)
  { uishell_sidebar_card_set(card, node, sig.box->key, context, contains_current, now); }
  card->source_seen = 1;
  card->source_rect = sig.box->rect;
  card->departure = ui_state->mouse;
  rd_request_frame();
}

internal void
uishell_sidebar_card_source(UIShell_SidebarState *state, AndamentoNode node,
                            UI_Signal sig, String8 context, B32 contains_current)
{
  uishell_sidebar_card_source_at(state, node, sig, context, contains_current, now_time_us());
}

internal void
uishell_sidebar_card_tick(UIShell_HoverCard *card, Vec2F32 mouse, U64 now)
{
  B32 inside = contains_2f32(card->rect, mouse), at_source = contains_2f32(card->source_rect, mouse);
  if(inside) { card->engaged = 1; }
  if(inside || at_source)
  {
    card->left_at = 0;
    if(at_source && !inside) { card->departure = mouse; }
  }
  else if(!card->left_at) { card->left_at = now; }
  if(!card->focused && card->left_at && now-card->left_at >= 400000)
  { uishell_sidebar_card_close(card); }
  card->corridor_active = uishell_sidebar_card_corridor(card, mouse, now);
}

// Run before physical terminal routing. Click focus persists on mouse-out and
// returns to the previous View on an outside click or Escape. Own both Escape
// edges even though its press removes the card.
internal B32
uishell_sidebar_card_wm_event(RD_WindowState *ws, WM_Event *event)
{
  if(!ws || ws == &rd_nil_window_state || !ws->sidebar || !ws->ui) { return 0; }
  UIShell_SidebarState *state = ws->sidebar;
  if(event->kind == WM_EventKind_WindowLoseFocus)
  {
    state->card_escape_down = 0;
    for(U64 i = 0; i < ArrayCount(state->cards); i++)
    { if(state->cards[i].focused) { uishell_sidebar_card_close(&state->cards[i]); } }
    ws->ui->hover_card_focus = 0;
    return 0;
  }
  if(event->key == WM_Key_Esc &&
     (event->kind == WM_EventKind_Press || event->kind == WM_EventKind_Release))
  {
    if(state->card_escape_down)
    {
      if(event->kind == WM_EventKind_Release) { state->card_escape_down = 0; }
      return 1;
    }
    if(ws->ui->hover_card_focus && event->kind == WM_EventKind_Press)
    {
      state->card_escape_down = 1;
      for(U64 i = 0; i < ArrayCount(state->cards); i++)
      { if(state->cards[i].focused) { uishell_sidebar_card_close(&state->cards[i]); } }
      ws->ui->hover_card_focus = 0;
      rd_request_frame();
      return 1;
    }
  }
  if(event->kind == WM_EventKind_Press &&
     (event->key == WM_Key_LeftMouseButton || event->key == WM_Key_RightMouseButton || event->key == WM_Key_MiddleMouseButton))
  {
    S64 hit = -1;
    for(U64 i = 0; i < ArrayCount(state->cards); i++)
    { if(state->cards[i].open && contains_2f32(state->cards[i].rect, event->pos)) { hit = (S64)i; } }
    for(U64 i = 0; i < ArrayCount(state->cards); i++)
    {
      UIShell_HoverCard *card = &state->cards[i];
      if(card->focused && hit < 0) { uishell_sidebar_card_close(card); }
      card->focused = card->open && hit == (S64)i;
    }
    ws->ui->hover_card_focus = hit >= 0;
  }
  return 0;
}

internal void
uishell_sidebar_card_navigate(UIShell_HoverCard *card, String8 key, U64 now)
{
  if(card->depth >= card->capacity)
  {
    String8 *path = push_array(card->arena, String8, card->capacity*2);
    MemoryCopy(path, card->path, card->depth*sizeof(String8));
    card->path = path; card->capacity *= 2;
  }
  card->previous = card->path[card->depth-1];
  card->path[card->depth++] = push_str8_copy(card->arena, key);
  card->changed_at = now;
  card->glide_from = card->rect.p0;
  card->scroll = 0;
}

internal size_t
uishell_sidebar_card_content(UIShell_SidebarState *state, RD_WindowState *ws, UIShell_HoverCard *card,
                            U64 slot, AndamentoNode node, U64 index, F32 width, B32 interactive)
{
  size_t action = ANDAMENTO_NONE;
  String8 label = uishell_sidebar_string(node.label), kind = uishell_sidebar_string(node.entity_kind);
  ui_label_multiline(width, label);
  if(card->context.size && card->depth == 1) { ui_label_multiline(width, card->context); }
  if(card->contains_current && card->depth == 1) { ui_label(str8_lit("Contains current workspace")); }
  for(U64 f = 0; f < node.detail_count; f++)
  {
    AndamentoField field = {0}; andamento_snapshot_field(state->snapshot, node.first_detail+f, &field);
    String8 value = uishell_sidebar_string(field.text);
    if(!value.size || str8_match(value, label, 0) || str8_match(value, kind, 0)) { continue; }
    B32 duplicate = 0;
    for(U64 p = 0; p < f; p++)
    {
      AndamentoField other = {0}; andamento_snapshot_field(state->snapshot, node.first_detail+p, &other);
      duplicate |= str8_match(value, uishell_sidebar_string(other.text), 0);
    }
    if(!duplicate) { ui_label_multiline(width, value); }
  }
  AndamentoField status = {0};
  if(node.field_count > 2) { andamento_snapshot_field(state->snapshot, node.first_field+2, &status); }
  UI_TagF("weak")
  {
    if(str8_match(uishell_sidebar_string(status.text), str8_lit("ended"), 0)) { ui_label(str8_lit("Ended workspace")); }
    else if(node.selected) { ui_label(str8_lit("Current workspace")); }
  }
  if(node.state == ANDAMENTO_LIVE && node.workspace_id)
  {
    rd_workspace_preview_demand_push(ws, node.workspace_id, width);
    RD_SurfaceCacheNode *mini = rd_window_surface_node_lookup(ws, rd_workspace_preview_surface_key(node.workspace_id));
    UI_PrefHeight(ui_px(width*0.625f, 1))
    {
      UI_Box *preview_box = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder, "###hover_preview_%I64u_%I64u", slot, node.workspace_id);
      if(mini)
      {
        RD_WorkspacePreviewDraw *preview = push_array(ui_build_arena(), RD_WorkspacePreviewDraw, 1);
        preview->node = mini; preview->src_uv = r2f32p(0, 0, 1, 1); preview->keep_aspect = 1;
        ui_box_equip_custom_draw(preview_box, rd_workspace_preview_box_draw, preview);
      }
      else { rd_request_frame(); }
    }
  }
  if(!interactive && card->depth == 1 && andamento_snapshot_copy_url_action(state->snapshot, index) != ANDAMENTO_NONE &&
     (str8_match(kind, str8_lit("change_request"), 0) || str8_match(kind, str8_lit("issue"), 0)))
  {
    UI_TagF("weak") { ui_label_multiline(width, str8_lit("Source row: click to open · Right-click to copy URL")); }
  }
  // ABI 2 has placement edges, not typed relation fields. Offer its parent and
  // direct children, deduplicated by entity, without parsing resolved prose.
  U64 count = andamento_snapshot_node_count(state->snapshot);
  AndamentoNode *nodes = push_array(ui_build_arena(), AndamentoNode, count);
  B32 *aliases = push_array(ui_build_arena(), B32, count);
  B32 *relations = push_array(ui_build_arena(), B32, count);
  B32 *excluded = push_array(ui_build_arena(), B32, count);
  // Preorder parents index direct children in one pass; alias parents preserve
  // Attention-to-tree navigation without scanning descendants per alias.
  for(U64 i = 0; i < count; i++)
  {
    andamento_snapshot_node(state->snapshot, i, &nodes[i]);
    aliases[i] = !nodes[i].is_section &&
      str8_match(uishell_sidebar_string(nodes[i].entity_kind), kind, 0) &&
      str8_match(uishell_sidebar_string(nodes[i].entity_id), uishell_sidebar_string(node.entity_id), 0);
    if(aliases[i] && nodes[i].parent != ANDAMENTO_NONE) { relations[nodes[i].parent] = 1; }
  }
  for(U64 i = 0; i < count; i++)
  { if(nodes[i].parent != ANDAMENTO_NONE && aliases[nodes[i].parent]) { relations[i] = 1; } }
  for(U64 path = 0; path < card->depth; path++)
  {
    AndamentoNode ancestor = {0};
    if(uishell_sidebar_card_find(state, card->path[path], &ancestor) == ANDAMENTO_NONE) { continue; }
    for(U64 i = 0; i < count; i++)
    {
      excluded[i] |= str8_match(uishell_sidebar_string(ancestor.entity_kind), uishell_sidebar_string(nodes[i].entity_kind), 0) &&
                     str8_match(uishell_sidebar_string(ancestor.entity_id), uishell_sidebar_string(nodes[i].entity_id), 0);
    }
  }
  U64 table_size = 1;
  while(table_size < count*2+1) { table_size *= 2; }
  U64 *seen = push_array(ui_build_arena(), U64, table_size);
  for(U64 i = 0; i < table_size; i++) { seen[i] = ANDAMENTO_NONE; }
  B32 related_label = 0;
  for(U64 i = 0; i < count; i++)
  {
    AndamentoNode related = nodes[i];
    if(related.is_section || !relations[i] || excluded[i]) { continue; }
    String8 related_kind = uishell_sidebar_string(related.entity_kind), related_id = uishell_sidebar_string(related.entity_id);
    U64 hash = u64_hash_from_str8(related_kind) ^ (u64_hash_from_str8(related_id)*0x9e3779b97f4a7c15ull);
    U64 bucket = hash & (table_size-1);
    B32 duplicate = 0;
    while(seen[bucket] != ANDAMENTO_NONE)
    {
      AndamentoNode other = nodes[seen[bucket]];
      if(str8_match(related_kind, uishell_sidebar_string(other.entity_kind), 0) &&
         str8_match(related_id, uishell_sidebar_string(other.entity_id), 0)) { duplicate = 1; break; }
      bucket = (bucket+1) & (table_size-1);
    }
    if(duplicate) { continue; }
    seen[bucket] = i;
    if(!related_label) { UI_TagF("weak") { ui_label(str8_lit("Related")); } related_label = 1; }
    if(!interactive) { ui_label_multiline(width, uishell_sidebar_string(related.label)); continue; }
    UI_Signal link = uishell_sidebar_button(push_str8f(ui_build_arena(), "%S###related_%I64u_%I64u_%S_%S", uishell_sidebar_string(related.label), slot, related_kind.size, related_kind, related_id));
    if(ui_clicked(link))
    {
      if(link.event_flags & (WM_Modifier_Super|WM_Modifier_Ctrl))
      {
        UIShell_HoverCard *separate = &state->cards[1];
        uishell_sidebar_card_set(separate, related, ui_key_zero(), str8_zero(), 0, now_time_us());
        separate->source_rect = card->rect;
        separate->engaged = 1;
        separate->focused = 1;
        card->focused = 0;
      }
      else { uishell_sidebar_card_navigate(card, uishell_sidebar_string(related.key), now_time_us()); }
      rd_request_frame();
    }
  }
  if(interactive)
  {
    if(card->depth > 1 && ui_clicked(uishell_sidebar_button(str8_lit("← Back###card_back"))))
    {
      card->previous = card->path[card->depth-1]; card->depth--;
      card->changed_at = now_time_us(); card->glide_from = card->rect.p0; card->scroll = 0; rd_request_frame();
    }
    B32 subject = str8_match(kind, str8_lit("change_request"), 0) || str8_match(kind, str8_lit("issue"), 0);
    size_t copy = andamento_snapshot_copy_url_action(state->snapshot, index);
    String8 open_label = subject ? str8_lit("Open in browser###card_open") : node.state == ANDAMENTO_LIVE ? str8_lit("Focus workspace###card_open") : str8_lit("Open workspace###card_open");
    if(node.activate != ANDAMENTO_NONE && (node.openable || node.state == ANDAMENTO_LIVE || copy != ANDAMENTO_NONE) &&
       ui_clicked(uishell_sidebar_button(open_label)))
    { action = node.activate; uishell_sidebar_card_close(card); }
    if(copy != ANDAMENTO_NONE && ui_clicked(uishell_sidebar_button(str8_lit("Copy URL###card_copy")))) { action = copy; }
    if(subject && copy == ANDAMENTO_NONE && ui_clicked(uishell_sidebar_button(str8_lit("Copy reference###card_copy"))))
    { wm_set_clipboard_text(label); }
    if(ui_clicked(uishell_sidebar_button(str8_lit("Close###card_close")))) { uishell_sidebar_card_close(card); rd_request_frame(); }
  }
  return action;
}

internal void
uishell_sidebar_cards_ui_at(RD_WindowState *ws, U64 now, B32 window_focused)
{
  UIShell_SidebarState *state = ws->sidebar;
  if(!state) { return; }
  state->card_has_action = 0;
  Rng2F32 window = wm_client_rect_from_window(ws->os);
  Vec2F32 mouse = ui_state->mouse;
  ui_state->hover_card_focus = 0;
  MemoryZeroArray(ui_state->hover_card_keys);
  MemoryZeroArray(ui_state->hover_card_rects);
  B32 outside = uishell_hover_cards_outside || rd_setting_b32_from_name(str8_lit("hover_cards_outside_sidebar"));
  if(!window_focused) { state->card_escape_down = 0; }
  // Seed both previous-frame bounds before building the lower card's controls.
  // Raw WM routing also uses these bounds; each layout refreshes its own mask.
  for(U64 slot = 0; slot < ArrayCount(state->cards); slot++)
  {
    if(state->cards[slot].open && !ui_any_ctx_menu_is_open())
    {
      ui_state->hover_card_keys[slot] = ui_key_from_stringf(ui_key_zero(), "###sidebar_card_%I64u", slot);
      ui_state->hover_card_rects[slot] = state->cards[slot].rect;
    }
  }
  // The renderer traverses siblings in reverse. Build the separate card first
  // so it paints last and shares the topmost ownership used by input routing.
  for(U64 slot = ArrayCount(state->cards); slot-- > 0;)
  {
    UIShell_HoverCard *card = &state->cards[slot];
    UI_Box *dismissed = ui_box_from_key(card->dismissed);
    if(ui_box_is_nil(dismissed) || !contains_2f32(dismissed->rect, mouse)) { card->dismissed = ui_key_zero(); }
    if(!card->source_seen && !card->open) { card->candidate = str8_zero(); }
    card->source_seen = 0;
    if(!card->open) { continue; }
    if(ui_any_ctx_menu_is_open() || (!window_focused && card->focused))
    { uishell_sidebar_card_close(card); continue; }
    AndamentoNode node = {0};
    U64 index = uishell_sidebar_card_find(state, card->path[card->depth-1], &node);
    if(index == ANDAMENTO_NONE) { uishell_sidebar_card_close(card); continue; }
    if(!ui_key_match(card->source, ui_key_zero()))
    {
      UI_Box *source = ui_box_from_key(card->source);
      if(ui_box_is_nil(source) || source->last_touched_build_index+1 < ui_state->build_index)
      { uishell_sidebar_card_close(card); continue; }
      card->source_rect = source->rect;
      for(UI_Box *p = source->parent; !ui_box_is_nil(p); p = p->parent)
      { if(p->flags & UI_BoxFlag_Clip) { card->source_rect = intersect_2f32(card->source_rect, p->rect); } }
      if(dim_2f32(card->source_rect).x <= 0 || dim_2f32(card->source_rect).y <= 0)
      { uishell_sidebar_card_close(card); continue; }
    }
    uishell_sidebar_card_tick(card, mouse, now);
    if(!card->open) { continue; }
    if(card->left_at) { rd_request_frame(); }
    ui_state->hover_card_focus |= card->focused;
    UI_Key key = ui_key_from_stringf(ui_key_zero(), "###sidebar_card_%I64u", slot);
    UI_Key content_key = ui_key_from_stringf(key, "content");
    UI_Box *old_content = ui_box_from_key(content_key);
    F32 em = ui_top_font_size(), width = Min(em*34, dim_2f32(window).x-20);
    F32 content_height = ui_box_is_nil(old_content) ? em*(6+node.detail_count*1.6f) : old_content->fixed_size.y;
    F32 height = Clamp(em*4, content_height+16, dim_2f32(window).y-20);
    F32 x = outside ? state->rect.x1+8 : Min(card->source_rect.x1+8, state->rect.x1-24);
    if(slot) { x = card->source_rect.x1+8; }
    Vec2F32 target = v2f32(Clamp(window.x0+10, x, window.x1-width-10),
                          Clamp(window.y0+10, card->source_rect.y0, window.y1-height-10));
    F32 t = Clamp(0.f, (now-card->changed_at)/100000.f, 1.f), glide = 1-(1-t)*(1-t)*(1-t);
    if(!card->previous.size) { card->glide_from = target; }
    Vec2F32 pos = mix_2f32(card->glide_from, target, glide);
    card->rect = r2f32p(pos.x, pos.y, pos.x+width, pos.y+height);
    ui_state->hover_card_keys[slot] = key;
    ui_state->hover_card_rects[slot] = card->rect;
    card->last_mouse = mouse;
    size_t action = ANDAMENTO_NONE;
    UI_Parent(ui_state->root) UI_TagF("floating")
    UI_Focus(card->focused ? UI_FocusKind_On : UI_FocusKind_Off) UI_CornerRadius(em*0.25f)
    {
      UI_Box *root;
      UI_Rect(card->rect)
      { root = ui_build_box_from_key(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|
          UI_BoxFlag_DrawDropShadow|UI_BoxFlag_DrawBackgroundBlur|UI_BoxFlag_DefaultFocusNavY, key); }
      UI_ScrollRegionParams params = ui_scroll_region_params(r2f32p(8, 8, width-8, height-8), UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Auto);
      params.content_dim_px = v2f32(width-16, content_height);
      UI_ScrollRegion region = ui_scroll_region_layout(params);
      UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
      axes[Axis2_Y] = (UI_ScrollRegionAxis){ui_scroll_pt((S64)card->scroll, 0), r1s64(0, Max(0, (S64)(content_height-dim_2f32(region.viewport).y))), (S64)dim_2f32(region.viewport).y};
      UI_Parent(root) UI_FocusActive(card->focused ? UI_FocusKind_Root : UI_FocusKind_Off)
      UI_FocusHot(card->focused ? UI_FocusKind_Root : UI_FocusKind_Off)
      {
        UI_ScrollRegionSignal scroll = ui_scroll_region_build(root, ui_key_from_stringf(key, "scroll"), &region, axes, UI_BoxFlag_Scroll|UI_BoxFlag_ScrollPrecise);
        F32 content_width = dim_2f32(region.viewport).x;
        card->scroll = (F32)scroll.position.y.idx;
        UI_Box *content;
        UI_Parent(scroll.content_box) UI_PrefWidth(ui_px(content_width, 1))
        UI_PrefHeight(ui_children_sum(1)) UI_ChildLayoutAxis(Axis2_Y)
        {
          UI_FixedX(0) UI_FixedY(-card->scroll) { content = ui_build_box_from_key(0, content_key); }
          UI_Parent(content) UI_PrefWidth(ui_px(content_width, 1)) UI_PrefHeight(ui_em(1.6f, 1))
          UI_TextAlignment(UI_TextAlign_Left) UI_Transparency(card->previous.size ? 1-t : 0)
          { action = uishell_sidebar_card_content(state, ws, card, slot, node, index, content_width, card->engaged); }
        }
        if(t < 1 && card->previous.size)
        {
          AndamentoNode previous = {0}; U64 previous_index = uishell_sidebar_card_find(state, card->previous, &previous);
          if(previous_index != ANDAMENTO_NONE)
          {
            UI_Parent(scroll.content_box) UI_PrefWidth(ui_px(content_width, 1)) UI_PrefHeight(ui_children_sum(1))
            UI_ChildLayoutAxis(Axis2_Y) UI_Transparency(t) UI_Flags(UI_BoxFlag_IgnoreInteraction)
            {
              UI_Box *outgoing;
              UI_FixedX(0) UI_FixedY(-card->scroll) { outgoing = ui_build_box_from_stringf(0, "###outgoing_%I64u", slot); }
              UI_Parent(outgoing) UI_PrefHeight(ui_em(1.6f, 1)) UI_TextAlignment(UI_TextAlign_Left)
              { uishell_sidebar_card_content(state, ws, card, slot+2, previous, previous_index, content_width, 0); }
            }
          }
          rd_request_frame();
        }
        ui_layout_root(content, Axis2_X); ui_layout_root(content, Axis2_Y);
        F32 measured_height = Clamp(em*4, content->fixed_size.y+16, dim_2f32(window).y-20);
        if(abs_f32(measured_height-height) > 0.5f)
        {
          height = measured_height;
          target.y = Clamp(window.y0+10, card->source_rect.y0, window.y1-height-10);
          if(!card->previous.size) { card->glide_from = target; }
          pos = mix_2f32(card->glide_from, target, glide);
          root->fixed_position = pos; root->fixed_size.y = height;
          scroll.content_box->fixed_size.y = height-16;
          card->rect = r2f32p(pos.x, pos.y, pos.x+width, pos.y+height);
          ui_state->hover_card_rects[slot] = card->rect;
          rd_request_frame(); // Update scroll thumb geometry with the measured extent.
        }
        UI_Signal wheel = ui_signal_from_box(scroll.content_box);
        card->scroll = Clamp(0.f, card->scroll+wheel.scroll.y*em*2, (F32)axes[Axis2_Y].range.max);
        // Consume background clicks after child buttons, so a card never opens
        // the sidebar row or sends a mouse press to the terminal underneath.
        root->flags |= UI_BoxFlag_MouseClickable;
        ui_signal_from_box(root);
      }
    }
    if(action != ANDAMENTO_NONE) { state->card_action = action; state->card_has_action = 1; rd_request_frame(); }
  }
  // Buttons can close a card or transfer focus while building its content.
  // Update before the selected View gets any remaining keyboard events.
  ui_state->hover_card_focus = 0;
  for(U64 i = 0; i < ArrayCount(state->cards); i++)
  { ui_state->hover_card_focus |= state->cards[i].open && state->cards[i].focused; }
}

internal void
uishell_sidebar_cards_ui(RD_WindowState *ws)
{
  // UI normally stops polling a background window's pointer after 500ms.
  // An informational card must keep tracking departure independently of the
  // keyboard target, including while a recorder temporarily owns activation.
  if(ws->sidebar && !wm_window_is_focused(ws->os))
  {
    for(U64 i = 0; i < ArrayCount(ws->sidebar->cards); i++)
    {
      if(ws->sidebar->cards[i].open) { ui_state->mouse = wm_mouse_from_window(ws->os); break; }
    }
  }
  uishell_sidebar_cards_ui_at(ws, now_time_us(), wm_window_is_focused(ws->os));
}
