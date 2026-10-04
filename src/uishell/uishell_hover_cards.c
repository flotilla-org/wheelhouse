// Structured cards retain exact entity identities, never snapshot pointers or
// action indices, across frames. Placement remains an anchor, not a relation.
global B32 uishell_hover_cards_outside;
enum
{
  UIShell_HoverCardOpenDelayUS = 300000,
  UIShell_HoverCardLeaveDelayUS = 400000,
  UIShell_HoverCardTransitionUS = 100000,
  UIShell_HoverCardCorridorPaddingPT = 12,
  UIShell_HoverCardNearGapPT = 16,
  UIShell_HoverCardInitialPathCapacity = 32,
};
StaticAssert(UIShell_HoverCardNearGapPT > UIShell_HoverCardCorridorPaddingPT, hover_card_scan_gap);

internal AndamentoEntity
uishell_sidebar_card_entity(AndamentoNode node)
{ return (AndamentoEntity){node.entity_kind, node.entity_id}; }

internal B32
uishell_sidebar_card_entity_match(AndamentoEntity a, AndamentoEntity b)
{
  return str8_match(uishell_sidebar_string(a.kind), uishell_sidebar_string(b.kind), 0) &&
         str8_match(uishell_sidebar_string(a.id), uishell_sidebar_string(b.id), 0);
}

internal AndamentoEntity
uishell_sidebar_card_entity_copy(Arena *arena, AndamentoEntity entity)
{
  return (AndamentoEntity){uishell_sidebar_text(push_str8_copy(arena, uishell_sidebar_string(entity.kind))),
                           uishell_sidebar_text(push_str8_copy(arena, uishell_sidebar_string(entity.id)))};
}

// A detail target need not have any placement. Adapt the catalog's preview
// identity to the existing preview drawing path without looking for a tree row.
internal U64
uishell_sidebar_card_find(UIShell_SidebarState *state, AndamentoEntity entity, AndamentoNode *out)
{
  U64 index = state->snapshot ? andamento_snapshot_detail_find(state->snapshot, entity.kind, entity.id) : ANDAMENTO_NONE;
  AndamentoDetail detail = {0};
  if(index != ANDAMENTO_NONE && out && andamento_snapshot_detail(state->snapshot, index, &detail))
  {
    *out = (AndamentoNode){.entity_kind = detail.entity.kind, .entity_id = detail.entity.id,
      .label = detail.label, .activate = detail.activate, .detail_count = detail.field_count,
      .state = detail.has_workspace ? ANDAMENTO_LIVE : ANDAMENTO_LATENT, .workspace_id = detail.workspace_id};
  }
  return index;
}

// Resolve semantic intent only after sidebar reconciliation/reveal has replaced
// snapshots. A disappearing or changed control cancels the pending action.
internal void
uishell_sidebar_card_queue_action(UIShell_SidebarState *state, AndamentoNode node, size_t action)
{
  U64 index = uishell_sidebar_card_find(state, uishell_sidebar_card_entity(node), 0);
  for(U64 i = 0; i < 2; i++)
  {
    AndamentoDetailAction control = {0};
    if(andamento_snapshot_detail_action(state->snapshot, index, i, &control) && control.action == action)
    {
      state->card_action_target = uishell_sidebar_card_entity_copy(ui_build_arena(), control.entity);
      state->card_action_intent = push_str8_copy(ui_build_arena(), uishell_sidebar_string(control.intent));
      state->card_has_action = 1;
      break;
    }
  }
}

internal size_t
uishell_sidebar_card_take_action(UIShell_SidebarState *state)
{
  B32 pending = state->card_has_action;
  AndamentoEntity target = state->card_action_target;
  String8 intent = state->card_action_intent;
  state->card_has_action = 0;
  state->card_action_target = (AndamentoEntity){0};
  state->card_action_intent = str8_zero();
  U64 index = pending ? uishell_sidebar_card_find(state, target, 0) : ANDAMENTO_NONE;
  if(index == ANDAMENTO_NONE) { return ANDAMENTO_NONE; }
  for(U64 i = 0; i < 2; i++)
  {
    AndamentoDetailAction control = {0};
    if(andamento_snapshot_detail_action(state->snapshot, index, i, &control) &&
       str8_match(intent, uishell_sidebar_string(control.intent), 0)) { return control.action; }
  }
  return ANDAMENTO_NONE;
}

internal void
uishell_sidebar_card_close(UIShell_HoverCard *card)
{
  card->dismissed = card->source;
  card->open = card->engaged = card->focused = 0;
  card->candidate = (AndamentoEntity){0};
  card->candidate_since = card->left_at = 0;
  card->corridor_active = 0;
}

// A bounded triangle toward the approaching edge protects diagonal travel.
// Use a top/bottom edge when the vertical gap dominates, so crossing beyond a
// side edge before reaching the card does not release a northwest/southeast path.
internal B32
uishell_sidebar_card_corridor(UIShell_HoverCard *card, Vec2F32 mouse, U64 now)
{
  if(!card->open || !card->left_at || now-card->left_at > UIShell_HoverCardLeaveDelayUS) { return 0; }
  Vec2F32 gap = {0}, delta = sub_2f32(mouse, card->last_mouse);
  for(Axis2 axis = Axis2_X; axis < Axis2_COUNT; axis++)
  {
    gap.v[axis] = Clamp(card->rect.p0.v[axis], card->departure.v[axis], card->rect.p1.v[axis])-card->departure.v[axis];
  }
  B32 stationary = delta.x == 0 && delta.y == 0;
  if(stationary && !card->corridor_active) { return 0; }
  if(!stationary)
  {
    // A card diagonally away requires diagonal intent. Pure row or column
    // scanning remains free to switch sources, as does motion away from it.
    for(Axis2 axis = Axis2_X; axis < Axis2_COUNT; axis++)
    { if(gap.v[axis] != 0 && delta.v[axis]*gap.v[axis] <= 0) { return 0; } }
  }
  Axis2 axis = abs_f32(gap.x) >= abs_f32(gap.y) ? Axis2_X : Axis2_Y;
  Axis2 other = axis2_flip(axis);
  F32 distance = gap.v[axis];
  if(abs_f32(distance) < 1) { return 0; }
  F32 t = (mouse.v[axis]-card->departure.v[axis])/distance;
  if(t < 0 || t > 1) { return 0; }
  F32 low = card->departure.v[other] + (card->rect.p0.v[other]-UIShell_HoverCardCorridorPaddingPT-card->departure.v[other])*t;
  F32 high = card->departure.v[other] + (card->rect.p1.v[other]+UIShell_HoverCardCorridorPaddingPT-card->departure.v[other])*t;
  return mouse.v[other] >= low && mouse.v[other] <= high;
}

internal void
uishell_sidebar_card_set(UIShell_HoverCard *card, AndamentoNode node, UI_Key source,
                        String8 context, B32 contains_current, U64 now)
{
  Temp scratch = scratch_begin(0, 0);
  AndamentoEntity previous = card->open && card->depth ? uishell_sidebar_card_entity_copy(scratch.arena, card->path[card->depth-1]) : (AndamentoEntity){0};
  if(!card->arena) { card->arena = arena_alloc(); }
  arena_clear(card->arena);
  card->previous = uishell_sidebar_card_entity_copy(card->arena, previous);
  card->capacity = UIShell_HoverCardInitialPathCapacity;
  card->path = push_array(card->arena, AndamentoEntity, card->capacity);
  card->path[0] = uishell_sidebar_card_entity_copy(card->arena, uishell_sidebar_card_entity(node));
  card->depth = 1;
  card->context = push_str8_copy(card->arena, context);
  card->contains_current = contains_current;
  card->source = source;
  card->dismissed = ui_key_zero();
  card->candidate = card->path[0];
  card->changed_at = now;
  // Replacement glides from retained bounds; a fresh open has no previous
  // content, so layout resets this origin directly to its target.
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
  AndamentoEntity key = uishell_sidebar_card_entity(node);
  if(card->open && card->depth && uishell_sidebar_card_entity_match(card->path[0], key))
  { card->source_seen = 1; return; }
  if(!uishell_sidebar_card_entity_match(card->candidate, key))
  {
    if(card->open)
    { uishell_sidebar_card_set(card, node, sig.box->key, context, contains_current, now); }
    else
    {
      if(!card->arena) { card->arena = arena_alloc(); }
      arena_clear(card->arena);
      card->candidate = uishell_sidebar_card_entity_copy(card->arena, key);
      card->candidate_since = now;
    }
  }
  if(!card->open && now-card->candidate_since >= UIShell_HoverCardOpenDelayUS)
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
  if(!card->focused && card->left_at && now-card->left_at >= UIShell_HoverCardLeaveDelayUS)
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
uishell_sidebar_card_navigate(UIShell_HoverCard *card, AndamentoEntity key, U64 now)
{
  if(card->depth >= card->capacity)
  {
    AndamentoEntity *path = push_array(card->arena, AndamentoEntity, card->capacity*2);
    MemoryCopy(path, card->path, card->depth*sizeof(AndamentoEntity));
    card->path = path; card->capacity *= 2;
  }
  card->previous = card->path[card->depth-1];
  card->path[card->depth++] = uishell_sidebar_card_entity_copy(card->arena, key);
  card->changed_at = now;
  card->glide_from = card->rect.p0;
  card->scroll = 0;
}

internal RD_IconKind
uishell_sidebar_card_icon(AndamentoText kind)
{
  String8 text = uishell_sidebar_string(kind);
  if(str8_match(text, str8_lit("project"), 0)) { return RD_IconKind_FolderClosedOutline; }
  if(str8_match(text, str8_lit("convoy"), 0) || str8_match(text, str8_lit("role"), 0)) { return RD_IconKind_Threads; }
  if(str8_match(text, str8_lit("change_request"), 0)) { return RD_IconKind_Glasses; }
  if(str8_match(text, str8_lit("worktree"), 0)) { return RD_IconKind_Module; }
  return RD_IconKind_FileOutline;
}

internal String8
uishell_sidebar_card_age(U64 now, U64 observed)
{
  U64 seconds = now > observed ? (now-observed)/1000 : 0;
  if(seconds < 60) { return push_str8f(ui_build_arena(), "%I64us ago", seconds); }
  if(seconds < 3600) { return push_str8f(ui_build_arena(), "%I64um ago", seconds/60); }
  if(seconds < 86400) { return push_str8f(ui_build_arena(), "%I64uh ago", seconds/3600); }
  return push_str8f(ui_build_arena(), "%I64ud ago", seconds/86400);
}

internal String8
uishell_sidebar_card_role_text(UIShell_SidebarState *state, U64 index, U32 role)
{
  AndamentoDetail detail = {0}; andamento_snapshot_detail(state->snapshot, index, &detail);
  for(U64 f = 0; f < detail.field_count; f++)
  {
    AndamentoDetailField field = {0}; andamento_snapshot_detail_field(state->snapshot, index, f, &field);
    if(field.role == role && field.has_value && field.text.len) { return uishell_sidebar_string(field.text); }
  }
  return str8_zero();
}

internal size_t
uishell_sidebar_card_content(UIShell_SidebarState *state, RD_WindowState *ws, UIShell_HoverCard *card,
                            U64 slot, AndamentoNode node, U64 index, F32 width, B32 interactive)
{
  size_t action = ANDAMENTO_NONE;
  AndamentoDetail detail = {0};
  if(!andamento_snapshot_detail(state->snapshot, index, &detail)) { return action; }
  String8 identity = uishell_sidebar_card_role_text(state, index, ANDAMENTO_DETAIL_IDENTITY);
  String8 title = uishell_sidebar_card_role_text(state, index, ANDAMENTO_DETAIL_TITLE);
  String8 badge = uishell_sidebar_card_role_text(state, index, ANDAMENTO_DETAIL_STATE);
  if(!identity.size) { identity = uishell_sidebar_string(detail.entity.id); }
  if(!title.size) { title = uishell_sidebar_string(detail.label); }
  F32 badge_width = badge.size ? Min(width*0.3f, ui_top_font_size()*(badge.size*0.65f+1)) : 0;
  UI_Row
  {
    UI_PrefWidth(ui_em(1.4f, 1)) RD_Font(RD_FontSlot_Icons)
    { ui_label(rd_icon_kind_text_table[uishell_sidebar_card_icon(detail.entity.kind)]); }
    UI_PrefWidth(ui_px(Max(0.f, width-ui_top_font_size()*1.4f-badge_width), 1)) UI_TagF("weak")
    { ui_label(identity); }
    if(badge.size)
    {
      UI_PrefWidth(ui_px(badge_width, 1)) UI_CornerRadius(ui_top_font_size()*0.3f)
      {
        UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder, "%S###state_badge", badge);
        ui_box_equip_display_string(box, badge);
      }
    }
  }
  if(!str8_match(identity, title, 0)) { ui_label_multiline(width, title); }
  if(card->context.size && card->depth == 1) { UI_TagF("weak") { ui_label_multiline(width, card->context); } }
  if(card->contains_current && card->depth == 1) { UI_TagF("weak") { ui_label(str8_lit("Contains current workspace")); } }
  if(detail.error.len) { ui_label_multiline(width, uishell_sidebar_string(detail.error)); }

  // Omit missing facts; known-empty facts keep a label and dash. Labels and
  // observation times come from the typed fields.
  UI_Box *facts_row = 0;
  U64 fact_count = 0;
  F32 cell_width = Max(0.f, (width-12)/2);
  for(U64 f = 0; f < detail.field_count; f++)
  {
    AndamentoDetailField field = {0}; andamento_snapshot_detail_field(state->snapshot, index, f, &field);
    if(field.role != ANDAMENTO_DETAIL_FACT || !field.has_value) { continue; }
    if(fact_count%2 == 0)
    {
      UI_PrefHeight(ui_children_sum(1)) UI_ChildLayoutAxis(Axis2_X)
      { facts_row = ui_build_box_from_stringf(0, "###facts_row_%I64u", fact_count/2); }
    }
    UI_Parent(facts_row) UI_PrefWidth(ui_px(cell_width, 1)) UI_PrefHeight(ui_em(1.5f, 1))
    UI_ChildLayoutAxis(Axis2_X) UI_Transparency(field.stale ? 1-(1-ui_top_transparency())*0.5f : ui_top_transparency())
    {
      UI_Box *cell = ui_build_box_from_stringf(0, "###fact_%I64u", f);
      UI_Parent(cell)
      {
        UI_TagF("weak") UI_FontSize(ui_top_font_size()*0.85f) UI_PrefWidth(ui_text_dim(5, 0))
        { ui_label(uishell_sidebar_string(field.label)); }
        UI_PrefWidth(ui_pct(1, 0))
        { ui_label(field.has_value && field.text.len ? uishell_sidebar_string(field.text) : str8_lit("—")); }
        if(field.has_observation)
        {
          UI_TagF("weak") UI_FontSize(ui_top_font_size()*0.75f) UI_PrefWidth(ui_text_dim(2, 1))
          { ui_label(uishell_sidebar_card_age(detail.now_ms, field.observed_at_ms)); }
        }
      }
    }
    if(fact_count%2 == 0) { UI_Parent(facts_row) { ui_spacer(ui_px(12, 1)); } }
    fact_count++;
  }

  B32 related_label = 0;
  U64 relation_capacity = 0;
  for(U64 f = 0; f < detail.field_count; f++)
  {
    AndamentoDetailField field = {0}; andamento_snapshot_detail_field(state->snapshot, index, f, &field);
    if(field.role == ANDAMENTO_DETAIL_RELATION) { relation_capacity += field.relation_count; }
  }
  AndamentoEntity *seen = push_array(ui_build_arena(), AndamentoEntity, relation_capacity);
  U64 seen_count = 0;
  for(U64 f = 0; f < detail.field_count; f++)
  {
    AndamentoDetailField field = {0}; andamento_snapshot_detail_field(state->snapshot, index, f, &field);
    if(field.role != ANDAMENTO_DETAIL_RELATION) { continue; }
    for(U64 r = 0; r < field.relation_count; r++)
    {
      AndamentoDetailRelation relation = {0};
      if(!andamento_snapshot_detail_relation(state->snapshot, index, f, r, card->path, card->depth, &relation)) { continue; }
      B32 duplicate = 0;
      for(U64 i = 0; i < seen_count; i++) { duplicate |= uishell_sidebar_card_entity_match(seen[i], relation.entity); }
      if(duplicate) { continue; }
      seen[seen_count++] = relation.entity;
      if(!related_label) { UI_TagF("weak") { ui_label(str8_lit("Related")); } related_label = 1; }
      AndamentoNode related = {0};
      B32 available = uishell_sidebar_card_find(state, relation.entity, &related) != ANDAMENTO_NONE;
      UI_Row
      {
        UI_PrefWidth(ui_em(1.5f, 1)) RD_Font(RD_FontSlot_Icons)
        { ui_label(rd_icon_kind_text_table[uishell_sidebar_card_icon(relation.entity.kind)]); }
        F32 badge_width = Min(width*0.3f, ui_top_font_size()*10);
        UI_Signal link = {0};
        UI_PrefWidth(ui_px(Max(0.f, width-ui_top_font_size()*1.5f-badge_width), 1))
        {
          if(interactive && available)
          { link = uishell_sidebar_button(push_str8f(ui_build_arena(), "%S###related_%I64u_%I64u_%S_%I64u_%S", uishell_sidebar_string(relation.display_text), slot, relation.entity.kind.len, uishell_sidebar_string(relation.entity.kind), relation.entity.id.len, uishell_sidebar_string(relation.entity.id))); }
          else { UI_TagF("weak") { ui_label(uishell_sidebar_string(relation.display_text)); } }
        }
        UI_PrefWidth(ui_px(badge_width, 1)) UI_TagF("weak")
        { ui_label(available ? uishell_sidebar_card_role_text(state, relation.detail, ANDAMENTO_DETAIL_STATE) : str8_lit("Unavailable")); }
        if(ui_clicked(link))
        {
          if(link.event_flags & (WM_Modifier_Super|WM_Modifier_Ctrl))
          {
            UIShell_HoverCard *separate = &state->cards[1];
            uishell_sidebar_card_set(separate, related, ui_key_zero(), str8_zero(), 0, now_time_us());
            separate->source_rect = card->rect;
            separate->engaged = separate->focused = 1; card->focused = 0;
          }
          else { uishell_sidebar_card_navigate(card, relation.entity, now_time_us()); }
          rd_request_frame();
        }
      }
    }
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
  if(interactive)
  {
    ui_spacer(ui_em(0.5f, 1));
    if(card->depth > 1 && ui_clicked(uishell_sidebar_button(str8_lit("← Back###card_back"))))
    {
      card->previous = card->path[card->depth-1]; card->depth--;
      card->changed_at = now_time_us(); card->glide_from = card->rect.p0; card->scroll = 0; rd_request_frame();
    }
    // Labels and enabled controls come from Andamento. The footer packs the
    // controls into one row; the text label remains the keyboard target.
    AndamentoDetailAction controls[2] = {0};
    U64 control_count = 0;
    for(U64 i = 0; i < ArrayCount(controls); i++)
    {
      AndamentoDetailAction control = {0};
      if(andamento_snapshot_detail_action(state->snapshot, index, i, &control)) { controls[control_count++] = control; }
    }
    F32 button_width = width/(control_count+1);
    UI_Row
    {
      for(U64 i = 0; i < control_count; i++)
      {
        AndamentoDetailAction control = controls[i];
        String8 intent = uishell_sidebar_string(control.intent);
        RD_IconKind icon = str8_match(intent, str8_lit("copy-url"), 0) ? RD_IconKind_FileOutline : RD_IconKind_Window;
        UI_PrefWidth(ui_px(button_width, 1)) UI_Row
        {
          UI_PrefWidth(ui_em(1.4f, 1)) RD_Font(RD_FontSlot_Icons) { ui_label(rd_icon_kind_text_table[icon]); }
          UI_PrefWidth(ui_px(Max(0.f, button_width-ui_top_font_size()*1.4f), 1))
          {
            if(ui_clicked(uishell_sidebar_button(push_str8f(ui_build_arena(), "%S###card_action_%I64u_%S", uishell_sidebar_string(control.label), i, intent))))
            { action = control.action; if(!str8_match(intent, str8_lit("copy-url"), 0)) { uishell_sidebar_card_close(card); } }
          }
        }
      }
      UI_PrefWidth(ui_px(button_width, 1))
      { if(ui_clicked(uishell_sidebar_button(str8_lit("Close###card_close")))) { uishell_sidebar_card_close(card); rd_request_frame(); } }
    }
  }
  return action;
}

// Leave the source row clear for horizontal scanning in Near placement. The
// 16pt gap exceeds the corridor's 12pt edge padding, so sideways motion along
// the row does not accidentally enter the diagonal corridor.
internal F32
uishell_sidebar_card_target_y(UIShell_HoverCard *card, F32 height, Rng2F32 window, B32 near_placement)
{
  F32 y = card->source_rect.y0;
  if(near_placement)
  {
    y = card->source_rect.y1+UIShell_HoverCardNearGapPT;
    if(y+height > window.y1-10) { y = card->source_rect.y0-height-UIShell_HoverCardNearGapPT; }
  }
  return Clamp(window.y0+10, y, window.y1-height-10);
}

internal void
uishell_sidebar_cards_ui_at(RD_WindowState *ws, U64 now, B32 window_focused, B32 sidebar_visible)
{
  UIShell_SidebarState *state = ws->sidebar;
  if(!state) { return; }
  // Abandon any unconsumed intent from the previous frame. A hidden sidebar
  // closes cards below, before their controls can emit a new intent.
  state->card_has_action = 0;
  state->card_action_target = (AndamentoEntity){0};
  state->card_action_intent = str8_zero();
  Rng2F32 window = wm_client_rect_from_window(ws->os);
  Vec2F32 mouse = ui_state->mouse;
  ui_state->hover_card_focus = 0;
  MemoryZeroArray(ui_state->hover_card_keys);
  MemoryZeroArray(ui_state->hover_card_rects);
  if(!sidebar_visible)
  {
    // Without the sidebar there is no same-frame snapshot action dispatcher.
    // Close before constructing controls, including the source-less second card.
    for(U64 i = 0; i < ArrayCount(state->cards); i++) { uishell_sidebar_card_close(&state->cards[i]); }
    state->card_escape_down = 0;
    return;
  }
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
    if(!card->source_seen && !card->open) { card->candidate = (AndamentoEntity){0}; }
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
                          uishell_sidebar_card_target_y(card, height, window, !outside && slot == 0));
    F32 t = Clamp(0.f, (F32)(now-card->changed_at)/UIShell_HoverCardTransitionUS, 1.f), glide = 1-(1-t)*(1-t)*(1-t);
    if(!card->previous.id.len) { card->glide_from = target; }
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
          UI_TextAlignment(UI_TextAlign_Left) UI_Transparency(card->previous.id.len ? 1-t : 0)
          { action = uishell_sidebar_card_content(state, ws, card, slot, node, index, content_width, card->engaged); }
        }
        if(t < 1 && card->previous.id.len)
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
              // Keep the visible outgoing preview live for the 100ms fade;
              // demand merges by workspace ID and expires with its frame index.
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
          target.y = uishell_sidebar_card_target_y(card, height, window, !outside && slot == 0);
          if(!card->previous.id.len) { card->glide_from = target; }
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
    if(action != ANDAMENTO_NONE) { uishell_sidebar_card_queue_action(state, node, action); rd_request_frame(); }
  }
  // Buttons can close a card or transfer focus while building its content.
  // Update before the selected View gets any remaining keyboard events.
  ui_state->hover_card_focus = 0;
  for(U64 i = 0; i < ArrayCount(state->cards); i++)
  { ui_state->hover_card_focus |= state->cards[i].open && state->cards[i].focused; }
}

internal void
uishell_sidebar_cards_ui(RD_WindowState *ws, B32 sidebar_visible)
{
  // UI normally stops polling a background window's pointer after 500ms.
  // An informational card must keep tracking departure independently of the
  // keyboard target, including while a recorder temporarily owns activation.
  // Restore the shared UI pointer deliberately: source pills and subsequent
  // widgets keep their normal hover feedback; this does not grant keyboard focus.
  if(ws->sidebar && !wm_window_is_focused(ws->os))
  {
    for(U64 i = 0; i < ArrayCount(ws->sidebar->cards); i++)
    {
      if(ws->sidebar->cards[i].open) { ui_state->mouse = wm_mouse_from_window(ws->os); break; }
    }
  }
  uishell_sidebar_cards_ui_at(ws, now_time_us(), wm_window_is_focused(ws->os), sidebar_visible);
}
