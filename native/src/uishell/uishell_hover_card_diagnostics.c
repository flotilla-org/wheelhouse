internal size_t
action_node_copy(UIShell_SidebarState *state, U64 index)
{
  AndamentoDetail detail = {0};
  return andamento_snapshot_detail(state->snapshot, index, &detail) ? detail.copy_url : ANDAMENTO_NONE;
}

// Commit through the same creation-drag routing and finish phases as a center
// drop on the panel showing local group `area`'s section.
internal void
uishell_hover_card_test_center_drop(RD_WindowState *ws, UIShell_HoverCard *card, String8 area)
{
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  UIShell_LocalGroup *group = uishell_sidebar_local_group(window, area);
  CFG_Node *view = group ? uishell_sidebar_local_view(window, group->section->id) : &cfg_nil_node;
  card->open = card->moving = card->drag_released = card->engaged = 1;
  ws->sidebar->drag_card = card;
  UIShell_RegsScope(.window = ws->cfg_id, .view = rd_nil_view_address, .panel = rd_nil_panel_address)
  { rd_drag_begin(UIShell_ContextRegSlot_View); }
  rd_state->drag_drop_creation_name = str8_lit("sidebar_section");
  rd_state->drag_drop_commit = uishell_sidebar_drag_panel_drop;
  rd_state->drag_drop_state = RD_DragDropState_Dropping;
  if(rd_drag_drop()) { rd_panel_drag_drop(rd_panel_from_cfg(view), Dir2_Invalid, rd_view_address_from_cfg(view)); }
  UI_EventList events = {0}; UI_AnimationInfo animation = {0};
  UI_IconInfo icons = ws->ui->icon_info;
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = v2f32(500, 100);
  uishell_sidebar_drag_finish(ws);
  uishell_sidebar_detached_finish(ws);
  ui_end_build();
}

// The center of the first card title line (its drag handle) under `root`.
internal Vec2F32
uishell_hover_card_test_title(UI_Box *root)
{
  String8 suffix = str8_lit("###card_title");
  for(UI_Box *box = root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, root).next)
  {
    if(box->string.size >= suffix.size && str8_match(str8_postfix(box->string, suffix.size), suffix, 0))
    { return center_2f32(box->rect); }
  }
  return v2f32(0, 0);
}

// Click an actual related/Back widget, or a cap control, through press and
// release frames. The card is engaged, so it shows its cap.
internal B32
uishell_hover_card_test_click(RD_WindowState *ws, UIShell_SidebarState *state,
                             UIShell_HoverCard *card, String8 label, WM_Modifiers modifiers)
{
  UI_IconInfo icons = ws->ui->icon_info;
  UI_AnimationInfo animation = {0};
  Rng2F32 hit = {0};
  B32 found = 0;
  for(U64 frame = 0; frame < 3; frame++)
  {
    UI_EventList events = {0};
    Vec2F32 mouse = center_2f32(hit);
    UI_Event event = {.kind = frame == 2 ? UI_EventKind_Release : UI_EventKind_Press,
                     .key = WM_Key_LeftMouseButton, .pos = mouse, .modifiers = modifiers};
    if(frame) { ui_event_list_push(ws->ui->arena, &events, &event); }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = mouse; MemoryZeroArray(ui_state->hover_card_keys); ui_state->hover_card_extra = 0;
    AndamentoNode node = {0};
    U64 index = uishell_sidebar_card_find(state, card->path[card->depth-1], &node);
    if(index == ANDAMENTO_NONE) { ui_end_build(); return 0; }
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    UI_PrefWidth(ui_px(400, 1)) UI_PrefHeight(ui_em(1.6f, 1)) UI_ChildLayoutAxis(Axis2_Y)
    {
      UI_Box *body;
      Rng2F32 rect = r2f32p(0, 40, 400, 740);
      UI_Rect(rect) UI_Focus(UI_FocusKind_On)
      { body = ui_build_box_from_key(UI_BoxFlag_DefaultFocusNavY, ui_key_make(9090)); }
      UI_Parent(body) UI_FocusHot(UI_FocusKind_Root) UI_FocusActive(UI_FocusKind_Root)
      {
        uishell_sidebar_card_cap(state, card, index, rect, r2f32p(0, 0, 800, 800), rect.p0, 3.f, 1.f, 1);
        uishell_sidebar_card_content(state, ws, card, 0, node, index, 400, 1);
      }
    }
    ui_end_build();
    if(frame == 0)
    {
      AndamentoNode source = {0};
      if(card->depth > 1 && uishell_sidebar_card_find(state, card->path[0], &source) != ANDAMENTO_NONE)
      {
        for(UI_Box *box = ui_state->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, ui_state->root).next)
        {
          if((box->flags & UI_BoxFlag_MouseClickable) &&
             str8_match(ui_box_display_string(box), uishell_sidebar_string(source.label), 0)) { return 0; }
        }
      }
      for(UI_Box *box = ui_state->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, ui_state->root).next)
      {
        if((box->flags & UI_BoxFlag_MouseClickable) && str8_match(ui_box_display_string(box), label, 0))
        { hit = box->rect; found = 1; break; }
      }
      if(!found) { return 0; }
    }
  }
  return found;
}

// The pin of `kind`/`id` as a restart reads it back: `state`'s dashboard
// record imported into a new core, whose Dashboard has it; copied into
// `arena`, or an empty one without it.
internal UIShell_LocalPin
uishell_hover_card_test_restart_pin(Arena *arena, UIShell_SidebarState *state, String8 kind, String8 id)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8 record = uishell_sidebar_record_get(scratch.arena, state->core, str8_lit("dashboard"));
  String8 config = str8_cstring((char *)uishell_sidebar_local_config);
  UIShell_SidebarState fresh = {0};
  fresh.core = andamento_create(config.str, config.size, 0);
  UIShell_LocalPin result = {0};
  if(fresh.core && andamento_record_put(fresh.core, uishell_sidebar_text(str8_lit("dashboard")), uishell_sidebar_text(record), 0))
  {
    UIShell_Locals *locals = uishell_sidebar_locals(&fresh);
    for(U64 s = 0; s < locals->section_count && !result.ghost.size; s++)
    for(U64 g = 0; g < locals->sections[s].group_count && !result.ghost.size; g++)
    for(U64 p = 0; p < locals->sections[s].groups[g].pin_count && !result.ghost.size; p++)
    {
      UIShell_LocalPin *pin = &locals->sections[s].groups[g].pins[p];
      if(!str8_match(uishell_sidebar_string(pin->target.kind), kind, 0) || !str8_match(uishell_sidebar_string(pin->target.id), id, 0)) { continue; }
      result.ghost = push_str8_copy(arena, pin->ghost);
      result.label = push_str8_copy(arena, pin->label);
      result.source = push_str8_copy(arena, pin->source);
      result.target = uishell_sidebar_card_entity_copy(arena, pin->target);
      result.compact = pin->compact;
    }
  }
  uishell_sidebar_release(&fresh);
  scratch_end(scratch);
  return result;
}

// The window's pins, counted.
internal U64
uishell_hover_card_test_pin_count(CFG_Node *window)
{
  U64 count = 0;
  UIShell_Locals *locals = uishell_sidebar_window_locals(window);
  for(U64 s = 0; s < locals->section_count; s++)
  for(U64 g = 0; g < locals->sections[s].group_count; g++) { count += locals->sections[s].groups[g].pin_count; }
  return count;
}

// A pin of host entity `kind`/`id` (both may be empty) added last to local
// group `group`, as a saved layout has one; its ghost id, in `arena`.
internal String8
uishell_hover_card_test_put_pin(Arena *arena, RD_WindowState *ws, String8 group, String8 kind, String8 id, String8 label)
{
  UIShell_LocalGroup *g = uishell_sidebar_local_group(cfg_node_from_id(ws->cfg_id), group);
  UIShell_LocalPin pin = {.ghost = string_from_guid(arena, make_guid()), .label = label,
    .target = uishell_sidebar_entity(uishell_sidebar_host_provider, kind, id)};
  if(g) { uishell_sidebar_local_put_pin(ws->sidebar, &pin, group, g->pin_count); }
  return pin.ghost;
}

// Deterministic time traces use the production transitions; UI and raw-WM
// checks exercise the actual hit exclusion and Escape routing seams.
internal B32
uishell_hover_card_diagnostics(RD_WindowState *ws)
{
  U32 failures = 0;
#define CardCheck(expr, message) do { if(!(expr)) { fprintf(stderr, "FAIL hover card: %s\n", message); failures++; } } while(0)
  fprintf(stderr, "Hover card diagnostics: start\n");
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  // Caps slide out at the menu animation rate; here they arrive at once.
  F32 saved_rate = rd_state->menu_animation_rate;
  rd_state->menu_animation_rate = 1.f;
  // Initialized and restored: rendering must use this fixture's own core.
  UIShell_SidebarState *saved_sidebar = ws->sidebar, fixture = {.initialized = 1};
  ws->sidebar = &fixture;
  UI_State *saved_window_ui = ws->ui;
  ws->ui = test;
  ui_select_state(test);
  UI_Box source = {.key = ui_key_make(700), .rect = r2f32p(10, 100, 100, 130)};
  UI_Signal hover = {.box = &source, .f = UI_SignalFlag_Hovering};
  AndamentoNode a = {.entity.kind = uishell_sidebar_text(str8_lit("issue")), .entity.id = uishell_sidebar_text(str8_lit("a"))};
  AndamentoNode b = {.entity.kind = uishell_sidebar_text(str8_lit("issue")), .entity.id = uishell_sidebar_text(str8_lit("b"))};
  UIShell_HoverCard *card = &fixture.cards[0];
  test->mouse = v2f32(50, 115);
  uishell_sidebar_card_source_at(&fixture, a, hover, str8_zero(), 0, 1000000);
  CardCheck(!card->open && !test->hover_card_focus, "first hover starts without focus");
  uishell_sidebar_card_source_at(&fixture, a, hover, str8_zero(), 0, 1299999);
  CardCheck(!card->open, "first hover waits 300ms");
  uishell_sidebar_card_source_at(&fixture, a, hover, str8_zero(), 0, 1300000);
  CardCheck(card->open && !card->engaged && !card->focused, "300ms opens an informational peek");
  uishell_sidebar_card_source_at(&fixture, b, hover, str8_zero(), 0, 1300001);
  CardCheck(card->open && str8_match(uishell_sidebar_string(card->path[0].id), str8_lit("b"), 0) &&
            str8_match(uishell_sidebar_string(card->previous.id), str8_lit("a"), 0), "next source swaps immediately and retains outgoing content");
  card->rect = r2f32p(180, 90, 400, 300);
  card->departure = v2f32(90, 115); card->last_mouse = card->departure;
  uishell_sidebar_card_tick(card, v2f32(120, 145), 1400000);
  CardCheck(card->corridor_active, "diagonal travel toward the card protects crossed rows");
  card->last_mouse = v2f32(120, 145);
  CardCheck(uishell_sidebar_card_corridor(card, card->last_mouse, 1400001), "pausing inside corridor retains protection");
  CardCheck(!uishell_sidebar_card_corridor(card, v2f32(120, 170), 1400001), "vertical list scanning swaps freely");
  CardCheck(!uishell_sidebar_card_corridor(card, v2f32(110, 145), 1400001), "moving away releases the corridor");
  CardCheck(!uishell_sidebar_card_corridor(card, v2f32(140, 155), 1800001), "corridor cannot hold another source indefinitely");
  uishell_sidebar_card_tick(card, v2f32(200, 150), 1500000);
  CardCheck(card->engaged && !card->focused && !card->left_at, "entering engages without keyboard focus");
  WM_Event escape = {.kind = WM_EventKind_Press, .key = WM_Key_Esc};
  CardCheck(!uishell_sidebar_card_wm_event(ws, &escape) && card->open, "unfocused card leaves Escape for the terminal");
  uishell_sidebar_card_tick(card, v2f32(500, 350), 1600000);
  uishell_sidebar_card_tick(card, v2f32(500, 350), 1999999);
  CardCheck(card->open, "engaged card survives 399ms outside");
  uishell_sidebar_card_tick(card, v2f32(500, 350), 2000000);
  CardCheck(!card->open, "engaged card closes after 400ms outside");
  uishell_sidebar_card_set(card, a, source.key, str8_zero(), 0, 2100000);
  card->rect = r2f32p(180, 90, 400, 300);
  WM_Event click = {.kind = WM_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = v2f32(200, 150)};
  uishell_sidebar_card_wm_event(ws, &click);
  CardCheck(card->focused && test->hover_card_focus, "click focuses the card");
  uishell_sidebar_card_tick(card, v2f32(500, 350), 2200000);
  uishell_sidebar_card_tick(card, v2f32(500, 350), 3200000);
  CardCheck(card->open, "click-focused card stays open on mouse-out");
  CardCheck(uishell_sidebar_card_wm_event(ws, &escape) && !card->open && !test->hover_card_focus, "focused Escape dismisses and restores View focus");
  escape.kind = WM_EventKind_Release;
  CardCheck(uishell_sidebar_card_wm_event(ws, &escape), "Escape release stays owned after dismissal");
  escape.kind = WM_EventKind_Press;
  CardCheck(!uishell_sidebar_card_wm_event(ws, &escape), "next Escape reaches the terminal");
  uishell_sidebar_card_set(card, a, source.key, str8_zero(), 0, 3300000);
  uishell_sidebar_card_navigate(card, uishell_sidebar_card_entity(b), 3300001);
  CardCheck(card->depth == 2 && str8_match(uishell_sidebar_string(card->path[0].id), str8_lit("a"), 0) &&
            str8_match(uishell_sidebar_string(card->path[1].id), str8_lit("b"), 0), "related navigation retains a back path");
  card->rect = r2f32p(180, 90, 400, 300);
  uishell_sidebar_card_wm_event(ws, &click);
  click.pos = v2f32(500, 350); uishell_sidebar_card_wm_event(ws, &click);
  CardCheck(!card->open && !test->hover_card_focus, "outside click dismisses a focused card");
  // Informational cards own no keyboard focus and survive activation changes.
  uishell_sidebar_card_set(card, a, source.key, str8_zero(), 0, 3350000);
  WM_Event deactivate = {.kind = WM_EventKind_WindowLoseFocus};
  uishell_sidebar_card_wm_event(ws, &deactivate);
  CardCheck(card->open && !card->focused && !test->hover_card_focus, "native focus loss preserves a peek without claiming keyboard input");
  // Losing native focus can drop a held Escape release.
  uishell_sidebar_card_set(card, a, source.key, str8_zero(), 0, 3400000);
  card->rect = r2f32p(180, 90, 400, 300); click.pos = v2f32(200, 150);
  uishell_sidebar_card_wm_event(ws, &click);
  escape.kind = WM_EventKind_Press; uishell_sidebar_card_wm_event(ws, &escape);
  WM_Event lost = {.kind = WM_EventKind_WindowLoseFocus};
  uishell_sidebar_card_wm_event(ws, &lost);
  CardCheck(!fixture.card_escape_down && !test->hover_card_focus, "focus loss clears held Escape ownership");
  escape.kind = WM_EventKind_Release;
  CardCheck(!uishell_sidebar_card_wm_event(ws, &escape), "late Escape release after focus loss is not retained by the card");
  escape.kind = WM_EventKind_Press;
  CardCheck(!uishell_sidebar_card_wm_event(ws, &escape), "first Escape after focus loss reaches the View");
  uishell_sidebar_card_set(card, a, source.key, str8_zero(), 0, 3500000);
  for(U64 i = 0; i < 40; i++) { uishell_sidebar_card_navigate(card, uishell_sidebar_card_entity(b), 3500001+i); }
  CardCheck(card->depth == 41, "long navigation paths grow instead of silently refusing a link");
  // Closing while crossing a protected corridor must not suppress future hovers.
  card->corridor_active = 1;
  uishell_sidebar_card_close(card);
  test->mouse = v2f32(50, 115);
  UI_Box next_source = source; next_source.key = ui_key_make(710);
  UI_Signal next_hover = hover; next_hover.box = &next_source;
  uishell_sidebar_card_source_at(&fixture, b, next_hover, str8_zero(), 0, 3550000);
  uishell_sidebar_card_source_at(&fixture, b, next_hover, str8_zero(), 0, 3850000);
  CardCheck(card->open && !card->corridor_active, "closing an active corridor allows a new 300ms hover");
  // Near's row offset leaves adjacent pills reachable by horizontal scanning.
  card->source_rect = source.rect;
  F32 below = uishell_sidebar_card_target_y(card, 200, r2f32p(0, 0, 800, 700), 1);
  CardCheck(below >= source.rect.y1+16, "Near leaves the source row clear below its anchor");
  card->rect = r2f32p(180, below, 400, below+200);
  card->departure = card->last_mouse = v2f32(90, 115); card->left_at = 4000000;
  CardCheck(!uishell_sidebar_card_corridor(card, v2f32(120, 115), 4000001), "horizontal pill scanning does not acquire Near's corridor");
  CardCheck(uishell_sidebar_card_corridor(card, v2f32(120, 140), 4000001), "diagonal entry still crosses the gap into Near");
  card->source_rect = r2f32p(10, 600, 100, 630);
  F32 above = uishell_sidebar_card_target_y(card, 200, r2f32p(0, 0, 800, 700), 1);
  CardCheck(above+200 <= card->source_rect.y0-16, "Near uses space above a row near the window bottom");
  // Overview northwest of a vessel button: entry toward the card crosses the
  // vessel before reaching the card's top edge, past its vertical edge plane.
  card->source_rect = r2f32p(300, 60, 340, 88);
  card->rect = r2f32p(348, 104, 722, 330);
  card->departure = card->last_mouse = v2f32(328, 80); card->left_at = 4100000;
  test->mouse = v2f32(355, 98);
  uishell_sidebar_card_tick(card, test->mouse, 4100001);
  UI_Box vessel_source = {.key = ui_key_make(720), .rect = r2f32p(340, 91, 359, 108)};
  UI_Signal vessel_hover = {.box = &vessel_source, .f = UI_SignalFlag_Hovering};
  uishell_sidebar_card_source_at(&fixture, a, vessel_hover, str8_zero(), 0, 4100001);
  CardCheck(card->corridor_active && str8_match(uishell_sidebar_string(card->path[0].id), str8_lit("b"), 0),
            "diagonal northwest-card entry protects a southeast target crossed before the top edge");
  // Build two real overlapping buttons and send a click over the overlay.
  UI_IconInfo icons = saved_window_ui->icon_info;
  UI_AnimationInfo animation = {0};
  for(U64 frame = 0; frame < 3; frame++)
  {
    UI_EventList events = {0};
    UI_Event press = {.kind = frame == 2 ? UI_EventKind_Release : UI_EventKind_Press,
                      .key = WM_Key_LeftMouseButton, .pos = v2f32(100, 100)};
    if(frame) { ui_event_list_push(test->arena, &events, &press); }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    // A batched click must still be blocked when the final mouse moved away.
    test->mouse = frame == 1 ? v2f32(300, 300) : v2f32(100, 100);
    UI_Key overlay_key = ui_key_make(800);
    test->hover_card_keys[0] = overlay_key; test->hover_card_rects[0] = r2f32p(50, 50, 200, 200);
    UI_Signal under = {0}, above = {0};
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    {
      UI_Rect(r2f32p(50, 50, 200, 200)) { under = ui_signal_from_box(ui_build_box_from_key(UI_BoxFlag_MouseClickable, ui_key_make(801))); }
      UI_Rect(r2f32p(50, 50, 200, 200)) { above = ui_signal_from_box(ui_build_box_from_key(UI_BoxFlag_MouseClickable, overlay_key)); }
    }
    ui_end_build();
    CardCheck(!ui_mouse_over(under) && !ui_pressed(under) && !ui_clicked(under), "overlay excludes underlying hover and activation");
    if(frame == 1) { CardCheck(ui_pressed(above), "overlay receives its own pointer press"); }
    if(frame == 2) { CardCheck(ui_clicked(above), "overlay receives its own pointer release"); }
  }
  // Icon controls must not leak their symbol font into descriptive tooltips.
  for(U64 frame = 0; frame < 2; frame++)
  {
    UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    test->mouse = v2f32(100, 100); MemoryZeroArray(test->hover_card_keys); test->hover_card_extra = 0;
    UI_Rect(r2f32p(50, 50, 150, 150)) UI_FontSize(12) RD_Font(RD_FontSlot_Icons)
    { uishell_sidebar_header_button(str8_lit("🖈"), str8_lit("font_fixture"), 0, 1, str8_lit("Pin font fixture"), str8_zero()); }
    ui_end_build();
    if(frame)
    {
      B32 found = 0;
      for(UI_Box *box = test->tooltip_root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->tooltip_root).next)
      {
        if(!str8_match(ui_box_display_string(box), str8_lit("Pin font fixture"), 0)) { continue; }
        found = 1;
        CardCheck(fnt_tag_match(box->font, rd_font_from_slot(RD_FontSlot_Main)) &&
                  box->text_raster_flags == rd_raster_flags_from_slot(RD_FontSlot_Main),
                  "icon control tooltip uses the main text font and raster settings");
      }
      CardCheck(found, "hovering the actual icon control produces its tooltip");
    }
  }
  // Two cards can overlap after window-edge clamping. The later card owns
  // the overlap, including children; the exposed part of the lower stays live.
  for(U64 frame = 0; frame < 3; frame++)
  {
    UI_EventList events = {0};
    UI_Event event = {.kind = frame == 2 ? UI_EventKind_Release : UI_EventKind_Press,
                      .key = WM_Key_LeftMouseButton, .pos = v2f32(150, 100)};
    if(frame) { ui_event_list_push(test->arena, &events, &event); }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    test->mouse = event.pos;
    test->hover_card_keys[0] = ui_key_make(810); test->hover_card_rects[0] = r2f32p(50, 50, 200, 200);
    test->hover_card_keys[1] = ui_key_make(820); test->hover_card_rects[1] = r2f32p(100, 50, 250, 200);
    UI_Signal lower = {0}, upper = {0}; UI_Box *lower_root, *upper_root;
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    {
      UI_Rect(test->hover_card_rects[0]) { lower_root = ui_build_box_from_key(0, test->hover_card_keys[0]); }
      UI_Parent(lower_root) UI_Rect(r2f32p(0, 0, 150, 150))
      { lower = ui_signal_from_box(ui_build_box_from_key(UI_BoxFlag_MouseClickable, ui_key_make(811))); }
      UI_Rect(test->hover_card_rects[1]) { upper_root = ui_build_box_from_key(0, test->hover_card_keys[1]); }
      UI_Parent(upper_root) UI_Rect(r2f32p(0, 0, 150, 150))
      { upper = ui_signal_from_box(ui_build_box_from_key(UI_BoxFlag_MouseClickable, ui_key_make(821))); }
    }
    ui_end_build();
    CardCheck(!ui_mouse_over(lower) && !ui_pressed(lower) && !ui_clicked(lower), "lower card cannot claim overlapping upper-card hits");
    CardCheck(!ui_hover_card_blocks_pointer(lower_root->first, v2f32(75, 100)), "exposed lower-card controls stay interactive");
    if(frame == 1) { CardCheck(ui_pressed(upper), "upper-card child receives the overlapping press"); }
    if(frame == 2) { CardCheck(ui_clicked(upper), "upper-card child receives the overlapping release"); }
  }
  // Physical outside dismissal must leave both click edges for the target.
  uishell_sidebar_card_set(card, a, source.key, str8_zero(), 0, 3600000);
  card->rect = r2f32p(180, 90, 400, 300); click.pos = v2f32(200, 150);
  uishell_sidebar_card_wm_event(ws, &click);
  for(U64 frame = 0; frame < 3; frame++)
  {
    click.kind = frame == 2 ? WM_EventKind_Release : WM_EventKind_Press; click.pos = v2f32(500, 350);
    B32 taken = frame ? uishell_sidebar_card_wm_event(ws, &click) : 0;
    UI_EventList events = {0};
    UI_Event event = {.kind = frame == 2 ? UI_EventKind_Release : UI_EventKind_Press,
                      .key = WM_Key_LeftMouseButton, .pos = click.pos};
    if(frame && !taken) { ui_event_list_push(test->arena, &events, &event); }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    test->mouse = click.pos; MemoryZeroArray(test->hover_card_keys);
    if(card->open) { test->hover_card_keys[0] = ui_key_make(810); test->hover_card_rects[0] = card->rect; }
    UI_Signal target = {0};
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12) UI_Rect(r2f32p(450, 300, 550, 400))
    { target = ui_signal_from_box(ui_build_box_from_key(UI_BoxFlag_MouseClickable, ui_key_make(830))); }
    ui_end_build();
    if(frame == 1) { CardCheck(!taken && !card->open && ui_pressed(target), "outside dismissal lets the target receive its press"); }
    if(frame == 2) { CardCheck(!taken && ui_clicked(target), "outside dismissal lets the target activate on release"); }
  }
  fprintf(stderr, "Hover card diagnostics: demand lifecycle\n");
  // Real host demand lifecycle: card identities survive a changed revision,
  // while snapshot-owned output and action currency remain core-owned.
  UIShell_SidebarState demand = {0};
  demand.core = andamento_create(0, 0, 0);
  AndamentoEntity demand_entity = uishell_sidebar_entity(uishell_sidebar_host_provider, str8_lit("issue"), str8_lit("demand-only"));
  AndamentoFact demand_fact = {.key = uishell_sidebar_text(str8_lit("display.label")), .kind = ANDAMENTO_FACT_TEXT,
                              .text = uishell_sidebar_text(str8_lit("Before"))};
  CardCheck(andamento_apply_entity(demand.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), demand_entity.kind, demand_entity.id, uishell_sidebar_text(str8_lit("demand")), &demand_fact, 1, 0),
            "demand-only target is published");
  uishell_sidebar_refresh(&demand);
  CardCheck(andamento_snapshot_detail_count(demand.snapshot) == 0, "plain revision starts without details");
  AndamentoNode demand_node = {0};
  CardCheck(uishell_sidebar_card_find(&demand, demand_entity, &demand_node) != ANDAMENTO_NONE,
            "identity without placement can be demanded");
  // One held card, with no navigation: slot cards[0] and root path[0] are
  // deliberate. The host clock supplies only the UI transition timestamp;
  // explicit core times 0/1/2 determine revision/action currency. Each snapshot
  // fixes its core clock at acquisition, including subsequently demanded cards.
  uishell_sidebar_card_set(&demand.cards[0], demand_node, ui_key_zero(), str8_zero(), 0, now_time_us());
  demand_fact.text = uishell_sidebar_text(str8_lit("After"));
  CardCheck(andamento_apply_entity(demand.core, 1, uishell_sidebar_text(uishell_sidebar_host_provider), demand_entity.kind, demand_entity.id, uishell_sidebar_text(str8_lit("demand")), &demand_fact, 1, 0),
            "held target label changes");
  CardCheck(str8_match(uishell_sidebar_string(demand_node.label), str8_lit("Before"), 0) &&
            !andamento_dispatch(demand.core, demand.snapshot, demand_node.activate, 0),
            "old output remains readable with stale actions rejected");
  uishell_sidebar_refresh(&demand);
  CardCheck(andamento_snapshot_detail_count(demand.snapshot) == 0 &&
            uishell_sidebar_card_find(&demand, demand.cards[0].path[0], &demand_node) != ANDAMENTO_NONE &&
            str8_match(uishell_sidebar_string(demand_node.label), str8_lit("After"), 0),
            "held exact identity demands current output after refresh");
  String8 demand_remove = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"issue\",\"id\":\"demand-only\"}},\"source_id\":\"demand\",\"unset\":[\"display.label\"]}");
  CardCheck(andamento_apply_patch_json(demand.core, 2, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(demand_remove), 0), "held target is removed");
  uishell_sidebar_refresh(&demand);
  CardCheck(uishell_sidebar_card_find(&demand, demand.cards[0].path[0], 0) == ANDAMENTO_NONE &&
            andamento_snapshot_detail_count(demand.snapshot) == 0,
            "disappearing held identity is unavailable without eager details");
  uishell_sidebar_release(&demand);

  // Real core/ABI scenario: four changed revisions with no held cards. A
  // never-published identity must stay error-free and append nothing on every
  // repeated host request, while ordinary entities ensure a nonempty catalog.
  UIShell_SidebarState missing = {0};
  missing.core = andamento_create(0, 0, 0);
  CardCheck(missing.core != 0, "missing-target fixture initializes");
  if(missing.core)
  {
    AndamentoEntity absent = uishell_sidebar_entity(uishell_sidebar_host_provider, str8_lit("issue"), str8_lit("never-existing"));
    for(U64 revision = 0; revision < 4; revision++)
    {
      demand_fact.text = uishell_sidebar_text(revision % 2 ? str8_lit("Odd") : str8_lit("Even"));
      CardCheck(andamento_apply_entity(missing.core, revision, uishell_sidebar_text(uishell_sidebar_host_provider), demand_entity.kind, demand_entity.id,
                                      uishell_sidebar_text(str8_lit("demand")), &demand_fact, 1, 0),
                "card-free fixture advances revision");
      uishell_sidebar_refresh(&missing);
      CardCheck(andamento_snapshot_detail_count(missing.snapshot) == 0,
                "card-free refresh stays plain across revisions");
      for(U64 repeat = 0; repeat < 3; repeat++)
      {
        CardCheck(uishell_sidebar_card_find(&missing, absent, 0) == ANDAMENTO_NONE,
                  "never-existing host identity repeatedly returns NONE");
        CardCheck(missing.error[0] == 0, "never-existing host identity has no error");
        CardCheck(andamento_snapshot_detail_count(missing.snapshot) == 0,
                  "never-existing host identity appends zero cards");
      }
    }
    // Real ABI failures own their strings. Identical failures must overwrite
    // the fixed status buffer with stable text rather than accumulate entries.
    char first_error[sizeof(missing.error)] = {0};
    for(U64 repeat = 0; repeat < 3; repeat++)
    {
      char *error = 0;
      B32 accepted = andamento_apply_patch_json(missing.core, 4, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(str8_lit("{")), &error);
      CardCheck(!accepted && error != 0, "malformed patch returns an owned ABI error");
      CardCheck(!uishell_sidebar_result(&missing, accepted, error), "host reports repeated ABI failure");
      CardCheck(missing.error[0] != 0, "ABI failure populates fixed status buffer");
      if(repeat == 0) { MemoryCopy(first_error, missing.error, sizeof(first_error)); }
      else
      {
        CardCheck(str8_match(str8_cstring(first_error), str8_cstring((char *)missing.error), 0),
                  "identical failures keep stable status text");
      }
    }
    uishell_sidebar_release(&missing);
  }

  fprintf(stderr, "Hover card diagnostics: detail rendering\n");
  // Render current detail fields through the production body in both states.
  // A LIVE observation exercises the existing preview demand and drawing box,
  // without starting a test terminal or altering the daily-driver inventory.
  String8 config = str8_cstring((char *)uishell_sidebar_daily_config);
  fixture.core = andamento_create(config.str, config.size, 0);
  CardCheck(fixture.core != 0, "render fixture initializes");
  if(fixture.core)
  {
    String8 patches = str8_cstring((char *)uishell_sidebar_fixture_patches);
    for(U64 start = 0; start < patches.size;)
    {
      U64 end = start;
      while(end < patches.size && patches.str[end] != '\n') { end++; }
      CardCheck(andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(str8(patches.str+start, end-start)), 0), "render fixture patch applies");
      start = end+1;
    }
    uishell_sidebar_refresh(&fixture);
    CardCheck(fixture.snapshot != 0, "render snapshot exists");
    CardCheck(andamento_snapshot_detail_count(fixture.snapshot) == 0,
              "refresh without live cards resolves no catalog details");
    U64 index = ANDAMENTO_NONE; AndamentoNode live = {0};
    for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(fixture.snapshot, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity.id), str8_lit("pr-281"), 0))
      { index = i; live = node; break; }
    }
    CardCheck(index != ANDAMENTO_NONE, "subject details exist");
    if(index != ANDAMENTO_NONE)
    {
      index = uishell_sidebar_card_find(&fixture, uishell_sidebar_card_entity(live), 0);
      CardCheck(index != ANDAMENTO_NONE && andamento_snapshot_detail_count(fixture.snapshot) == 1,
                "first card resolves only its exact identity");
      CardCheck(uishell_sidebar_card_find(&fixture, uishell_sidebar_card_entity(live), 0) == index &&
                andamento_snapshot_detail_count(fixture.snapshot) == 1,
                "repeated card lookup reuses snapshot-owned detail");
      // A workspace this test makes up, read back through the index as 123456.
      UIShell_WorkspaceId previewed = uishell_workspace_id_make();
      uishell_workspace_index_insert(previewed, 123456);
      live.state = ANDAMENTO_LIVE; live.workspace_id = uishell_sidebar_workspace(previewed);
      uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, 4000000);
      for(U64 engaged = 0; engaged < 2; engaged++)
      {
        UI_EventList events = {0};
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        MemoryZeroArray(test->hover_card_keys);
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        UI_PrefWidth(ui_px(400, 1)) UI_PrefHeight(ui_em(1.6f, 1))
        {
          UI_Key preview_key = ui_key_from_stringf(ui_active_seed_key(), "###hover_preview_%I64u_%I64u", (U64)0, (U64)123456);
          uishell_sidebar_card_content(&fixture, ws, card, 0, live, index, 400, engaged);
          CardCheck(!ui_box_is_nil(ui_box_from_key(preview_key)), "both peek and engaged retain the preview box");
        }
        ui_end_build();
        CardCheck(rd_workspace_preview_demand_width(ws, 123456) > 0, "both states request live workspace preview rendering");
        B32 title = 0, actions = 0;
        for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
        {
          String8 text = ui_box_display_string(box);
          title |= str8_match(text, str8_lit("Render subjects"), 0);
          actions |= str8_match(text, str8_lit("Copy URL"), 0);
        }
        CardCheck(title, "structured title is rendered without a prefix");
        CardCheck(!actions, "the card body holds no actions; they're its cap's");
      }
      // The cap holds the entity's actions as glyphs, with their labels as
      // tooltips, then ⓘ, Pin and × on a hover card; Float and Dock under
      // source are gone (#269). A narrow cap folds actions into ⋯.
      for(U64 narrow = 0; narrow < 2; narrow++)
      {
        UI_EventList events = {0};
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        MemoryZeroArray(test->hover_card_keys);
        F32 cap_width = narrow ? 4*UIShell_ControlMinimumPT+8 : 400;
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        { uishell_sidebar_card_cap(&fixture, card, index, r2f32p(0, 100, cap_width, 300), r2f32p(0, 0, 800, 800), v2f32(0, 0), 3.f, 1.f, 1); }
        ui_end_build();
        String8 glyphs[8] = {0}; U64 glyph_count = 0;
        UI_Box *cap = ui_box_from_key(card->cap.key);
        for(UI_Box *box = cap; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, cap).next)
        { if((box->flags & UI_BoxFlag_Clickable) == UI_BoxFlag_Clickable && glyph_count < ArrayCount(glyphs)) { glyphs[glyph_count++] = ui_box_display_string(box); } }
        CardCheck(card->cap_shown && !card->cap_below && card->cap.rect.y1 == 101 && dim_2f32(card->cap.rect).x == cap_width,
                  "the cap joins the card's top edge at the card's width");
        if(narrow)
        {
          CardCheck(glyph_count == 4 && str8_match(glyphs[0], str8_lit("⋯"), 0) && !card->cap_footer,
                    "a narrow cap folds the actions into ⋯");
        }
        else
        {
          CardCheck(glyph_count == 5 && str8_match(glyphs[1], str8_lit("⧉"), 0) && str8_match(glyphs[2], str8_lit("ⓘ"), 0) &&
                    str8_match(glyphs[3], str8_lit("🖈"), 0) && str8_match(glyphs[4], str8_lit("×"), 0),
                    "a hover card's cap holds its actions, ⓘ, Pin and ×");
        }
      }
      {
        UI_EventList events = {0};
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        { uishell_sidebar_card_cap(&fixture, card, index, r2f32p(0, 10, 400, 300), r2f32p(0, 0, 800, 800), v2f32(0, 0), 3.f, 1.f, 1); }
        ui_end_build();
        CardCheck(card->cap_below && card->cap.rect.y0 == 299, "a card at the top takes its cap on its bottom edge");
      }
      AndamentoNode parent = {0}; andamento_snapshot_node(fixture.snapshot, live.parent, &parent);
      String8 parent_label = uishell_sidebar_string(parent.label);
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, parent_label, 0) && card->depth == 2,
                "clicking a related widget navigates within the card");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("← Back"), 0) && card->depth == 1 &&
                str8_match(uishell_sidebar_string(card->path[0].id), uishell_sidebar_string(live.entity.id), 0), "Back widget restores the source target");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, parent_label, WM_Modifier_Ctrl) && card->depth == 1 &&
                fixture.cards[1].open && fixture.cards[1].focused &&
                str8_match(uishell_sidebar_string(fixture.cards[1].path[0].id), uishell_sidebar_string(parent.entity.id), 0),
                "modifier-click opens a separate focused card without changing the original path");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("×"), 0) && !card->open &&
                fixture.cards[1].open && fixture.cards[1].focused, "closing the original leaves the separate card open and focused");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, &fixture.cards[1], str8_lit("×"), 0) &&
                !fixture.cards[1].open, "the separate card closes through its own action");
      // Hidden targets navigate by catalog identity, including cycles and
      // modifier-open. No placement-edge fallback may invent a relation.
      uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, now_time_us());
      B32 hidden_in_tree = 0;
      for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
      {
        AndamentoNode row = {0}; andamento_snapshot_node(fixture.snapshot, i, &row);
        hidden_in_tree |= str8_match(uishell_sidebar_string(row.entity.id), str8_lit("hidden-detail"), 0);
      }
      CardCheck(!hidden_in_tree, "hidden relation target has no tree placement");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("Hidden related issue"), WM_Modifier_Ctrl) &&
                fixture.cards[1].open && card->depth == 1,
                "modifier-open resolves a target without a tree placement");
      uishell_sidebar_card_close(&fixture.cards[1]);
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("Hidden related issue"), 0) && card->depth == 2,
                "typed hidden relation navigates to catalog details");
      CardCheck(!uishell_hover_card_test_click(ws, &fixture, card, str8_lit("!281"), 0),
                "typed relation back to the source is omitted from the path");
      CardCheck(!uishell_hover_card_test_click(ws, &fixture, card, str8_lit("Hidden related issue"), 0),
                "self relation is omitted");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("Example project"), 0) && card->depth == 3,
                "catalog navigation follows typed relations across kinds");
      CardCheck(!uishell_hover_card_test_click(ws, &fixture, card, str8_lit("Hidden related issue"), 0),
                "related mini-rows exclude every ancestor, not only the immediate parent");
      CardCheck(!uishell_hover_card_test_click(ws, &fixture, card, str8_lit("Unavailable"), 0),
                "unavailable relation targets do not expose a navigation button");
      uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, now_time_us());
      CardCheck(!card->enriched && uishell_hover_card_test_click(ws, &fixture, card, str8_lit("ⓘ"), 0) && card->enriched,
                "engaged Details button reveals labels and observation ages");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("ⓘ"), 0) && !card->enriched,
                "Details button returns to compact mode");
      // Full production layout catches fixed-rectangle scope leakage into
      // fields and buttons, rather than only testing their existence.
      // A fresh open, so the card lands on its target without gliding. The
      // pointer is right of the narrow source, as over a row's label.
      uishell_sidebar_card_close(card);
      test->mouse = v2f32(200, 35);
      uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, now_time_us());
      card->source_rect = r2f32p(20, 20, 80, 50); fixture.rect = r2f32p(0, 0, 320, 700);
      for(U64 frame = 0; frame < 3; frame++)
      {
        UI_EventList events = {0};
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        if(frame)
        {
          AndamentoNode next = {0};
          for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
          {
            andamento_snapshot_node(fixture.snapshot, i, &next);
            if(str8_match(uishell_sidebar_string(next.entity.id), str8_lit("pr-1000"), 0)) { break; }
          }
          uishell_sidebar_card_set(card, next, ui_key_zero(), str8_zero(), 0, now_time_us());
          uishell_sidebar_card_set(&fixture.cards[1], parent, ui_key_zero(), str8_zero(), 0, now_time_us());
          fixture.cards[1].source_rect = card->rect;
        }
        if(frame == 2)
        {
          uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, now_time_us());
          uishell_sidebar_card_close(&fixture.cards[1]);
        }
        test->mouse = frame ? center_2f32(card->rect) : v2f32(50, 35);
        card->focused = frame == 1; // Click-focus ownership is covered by the WM trace.
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        { uishell_sidebar_cards_ui_at(ws, now_time_us(), frame != 2, 1); }
        // Near places the card from the pointer where it opened, not the source box.
        if(frame == 0) { CardCheck(abs_f32(card->rect.x0-(200-UIShell_HoverCardNearGapPT)) < 1, "a Near hover card starts just left of where the pointer opened it"); }
        if(frame == 0) { CardCheck(!card->cap_shown, "a hover card at rest is a peek, without its cap"); }
        if(frame == 1) { CardCheck(card->cap_shown && contains_2f32(test->hover_card_rects[0], center_2f32(card->cap.rect)),
                                   "a hover card shows its cap once the pointer moves in, and its cap takes the pointer");
                         UI_Box *card_box = ui_box_from_key(ui_key_from_stringf(ui_key_zero(), "###sidebar_card_%I64u", (U64)0));
                         UI_Box *outline = ui_box_from_key(ui_key_from_stringf(card_box->key, "outline"));
                         CardCheck(!(card_box->flags & UI_BoxFlag_DrawBorder) && !ui_box_is_nil(outline) && outline->parent == card_box &&
                                   outline == card_box->first, "a hover card's outline goes round it and its cap, over both"); }
        if(frame == 2) { CardCheck(card->open && !card->focused, "hover remains informational when the window is not the keyboard target"); }
        ui_end_build();
        UI_Key root_key = ui_key_from_stringf(ui_key_zero(), "###sidebar_card_%I64u", (U64)0);
        UI_Box *root = ui_box_from_key(root_key);
        CardCheck((root->flags & UI_BoxFlag_DisableFocusOverlay) && (root->flags & UI_BoxFlag_DisableFocusBorder),
                  "transient card surface suppresses whole-card focus paint");
        CardCheck(!ui_box_is_nil(root) && dim_2f32(root->rect).y > 40, "full card measures its content on the first frame");
        U64 lines = 0;
        for(UI_Box *box = root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, root).next)
        {
          B32 outgoing = 0;
          for(UI_Box *p = box; !ui_box_is_nil(p); p = p->parent) { outgoing |= !!(p->flags & UI_BoxFlag_IgnoreInteraction); }
          if(outgoing || !(box->flags & UI_BoxFlag_DrawText)) { continue; }
          CardCheck(dim_2f32(box->rect).y > 0 && dim_2f32(box->rect).y < dim_2f32(root->rect).y,
                    "structured text has its own nonzero height in full card layout");
          for(UI_Box *other = box->next; !ui_box_is_nil(other); other = other->next)
          {
            if(other->flags & UI_BoxFlag_DrawText)
            {
              Rng2F32 overlap = intersect_2f32(box->rect, other->rect);
              CardCheck(dim_2f32(overlap).x <= 0 || dim_2f32(overlap).y <= 0,
                        "header and relation text siblings occupy separate grid cells");
            }
          }
          lines++;
        }
        CardCheck(lines > 5, "full card retains the current detail fields");
        if(frame == 1)
        {
          // Match the shell renderer's reverse-sibling traversal, not build order.
          U64 draw_index = 0, original_draw = 0, separate_draw = 0;
          UI_Key separate_key = ui_key_from_stringf(ui_key_zero(), "###sidebar_card_%I64u", (U64)1);
          for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_post(box, &ui_nil_box).next)
          {
            draw_index++;
            if(ui_key_match(box->key, root_key)) { original_draw = draw_index; }
            if(ui_key_match(box->key, separate_key)) { separate_draw = draw_index; }
          }
          CardCheck(original_draw && separate_draw > original_draw, "second card is painted above the original as hit routing expects");
          UI_Box *incoming = ui_box_from_key(ui_key_from_stringf(root_key, "content"));
          UI_Box *outgoing = &ui_nil_box;
          for(UI_Box *box = root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, root).next)
          {
            if((box->flags & UI_BoxFlag_IgnoreInteraction) && box->parent == incoming->parent && !ui_box_is_nil(box->first))
            { outgoing = box; break; }
          }
          CardCheck(!ui_box_is_nil(outgoing) && abs_f32(outgoing->rect.x0-incoming->rect.x0) < 1 &&
                    abs_f32(outgoing->rect.y0-incoming->rect.y0) < 1, "cross-fade layers share the same content origin");
        }
        CardCheck(test->hover_card_focus == (B32)(frame == 1), "full controller preserves peek/click focus distinction");
      }
      // Exercise leave timing through the full controller while deactivated.
      WM_Event deactivate = {.kind = WM_EventKind_WindowLoseFocus};
      uishell_sidebar_card_wm_event(ws, &deactivate);
      CardCheck(card->open && !card->focused, "background leave trace starts with an informational card");
      U64 background_leave = now_time_us()+1000000;
      U64 background_offsets[] = {0, 399999, 400000};
      for(U64 frame = 0; frame < ArrayCount(background_offsets); frame++)
      {
        UI_EventList background_events = {0};
        ui_begin_build(ws->os, &background_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = v2f32(-100, -100);
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        { uishell_sidebar_cards_ui_at(ws, background_leave+background_offsets[frame], 0, 1); }
        ui_end_build();
        CardCheck(card->open == (B32)(frame < 2) && !test->hover_card_focus,
                  "deactivated card survives 399ms outside and closes at 400ms without keyboard focus");
      }
      uishell_sidebar_card_close(card); uishell_sidebar_card_close(&fixture.cards[1]);
      UI_EventList events = {0}; ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); ui_end_build();
      CardCheck(!test->hover_card_focus, "closing the last card clears focus before View event consumers");
      // Its slot opens the next card with the cap in, not sliding back from
      // where the last card left it.
      for(U64 frame = 0; frame < 3; frame++)
      {
        // Out, then closed, then reopened as a peek, sliding at half rate.
        if(frame != 1) { uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, now_time_us()); }
        card->engaged = frame == 0;
        rd_state->menu_animation_rate = frame == 2 ? 0.5f : 1.f;
        UI_EventList reopen_events = {0}; ui_begin_build(ws->os, &reopen_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = frame == 0 ? center_2f32(card->rect) : v2f32(-100, -100);
        uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); ui_end_build();
        if(frame == 0) { CardCheck(card->cap_shown, "an engaged hover card has its cap out"); uishell_sidebar_card_close(card); }
        if(frame == 2) { CardCheck(card->open && !card->engaged && !card->cap_drawn, "a reopened hover card starts with its cap in"); }
      }
      uishell_sidebar_card_close(card);
      rd_state->menu_animation_rate = 1.f;
      // Follow the focused card's production default-navigation path.
      uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, now_time_us());
      uishell_sidebar_card_navigate(card, uishell_sidebar_card_entity(parent), now_time_us());
      card->previous = (AndamentoEntity){0}; card->engaged = card->focused = 1;
      B32 saw_related = 0, saw_back = 0, saw_close = 0;
      for(U64 frame = 0; frame < 32 && card->open; frame++)
      {
        UI_EventList keys = {0};
        UI_Event key = {.kind = UI_EventKind_Press, .key = saw_close ? WM_Key_Return : WM_Key_Tab,
                        .slot = saw_close ? UI_EventActionSlot_Accept : UI_EventActionSlot_Null};
        if(frame) { ui_event_list_push(test->arena, &keys, &key); }
        ui_begin_build(ws->os, &keys, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = center_2f32(card->rect);
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        { uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); }
        ui_end_build();
        UI_Key root_key = ui_key_from_stringf(ui_key_zero(), "###sidebar_card_%I64u", (U64)0);
        UI_Box *root = ui_box_from_key(root_key);
        UI_Box *hot = ui_box_from_key(root->default_nav_focus_hot_key);
        if(!ui_box_is_nil(hot))
        {
          CardCheck(!(hot->flags & (UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder)),
                    "keyboard-focused child keeps its focus indication");
          String8 text = ui_box_display_string(hot);
          saw_back |= str8_match(text, str8_lit("← Back"), 0);
          saw_close |= saw_back && saw_related && str8_match(text, str8_lit("×"), 0);
          saw_related |= !saw_back && !saw_close && str8_find_needle(hot->string, 0, str8_lit("###related_"), 0) < hot->string.size;
        }
      }
      CardCheck(saw_related && saw_back && saw_close && !card->open && !test->hover_card_focus,
                "Tab reaches Related, Back and Close and Enter activates Close");
      // Hiding the sidebar also dismisses the independent source-less card,
      // before its controls can enqueue an action without a dispatcher.
      uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, now_time_us());
      uishell_sidebar_card_set(&fixture.cards[1], parent, ui_key_zero(), str8_zero(), 0, now_time_us());
      fixture.cards[1].focused = 1; fixture.card_escape_down = 1;
      U64 hidden_index = uishell_sidebar_card_find(&fixture, uishell_sidebar_card_entity(live), 0);
      uishell_sidebar_card_queue_action(&fixture, live, action_node_copy(&fixture, hidden_index));
      CardCheck(fixture.card_has_action && fixture.card_action_intent.size, "hidden sidebar trace starts with real queued intent");
      UI_EventList hidden_events = {0};
      ui_begin_build(ws->os, &hidden_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 0); ui_end_build();
      uishell_sidebar_cards_dispatch(ws);
      CardCheck(!card->open && !fixture.cards[1].open && !fixture.card_has_action && !test->hover_card_focus &&
                !fixture.card_action_intent.size && !fixture.card_action_target.id.len && !fixture.card_escape_down,
                "hidden sidebar abandons intent and held Escape before any dispatch");
      // Pinned/inline controls render after the overlay controller in the shell.
      UI_EventList late_events = {0};
      ui_begin_build(ws->os, &late_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1);
      uishell_sidebar_card_queue_action(&fixture, live, action_node_copy(&fixture, hidden_index));
      CardCheck(fixture.card_has_action, "late View can queue card intent after the overlay pass");
      Andamento *dispatch_core = fixture.core; fixture.core = 0;
      uishell_sidebar_cards_dispatch(ws);
      fixture.core = dispatch_core;
      CardCheck(!fixture.card_has_action && !fixture.card_action_intent.size && !fixture.card_action_target.id.len,
                "window-end card dispatch consumes late View intent before the arena advances");
      ui_end_build();
      // A reveal/observe refresh can replace the snapshot after the card emits
      // its intent but before sidebar dispatch. Never retain the old index.
      AndamentoNode action_node = {0};
      U64 action_index = uishell_sidebar_card_find(&fixture, card->path[0], &action_node);
      size_t copy_action = action_node_copy(&fixture, action_index);
      CardCheck(copy_action != ANDAMENTO_NONE, "refresh trace starts with a Copy URL action");
      uishell_sidebar_card_queue_action(&fixture, action_node, copy_action);
      AndamentoFact refreshed_title = {.key = uishell_sidebar_text(str8_lit("flotilla.change_request.title")),
                                       .kind = ANDAMENTO_FACT_TEXT, .text = uishell_sidebar_text(str8_lit("Refreshed title"))};
      CardCheck(andamento_apply_entity(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(str8_lit("change_request")),
                uishell_sidebar_text(str8_lit("pr-281")), uishell_sidebar_text(str8_lit("fixture")), &refreshed_title, 1, 0),
                "refresh trace updates the selected entity");
      uishell_sidebar_refresh(&fixture);
      action_index = uishell_sidebar_card_find(&fixture, fixture.card_action_target, &action_node);
      copy_action = action_node_copy(&fixture, action_index);
      CardCheck(copy_action != ANDAMENTO_NONE && uishell_sidebar_card_take_action(&fixture) == copy_action &&
                !fixture.card_has_action && uishell_sidebar_card_take_action(&fixture) == ANDAMENTO_NONE,
                "Copy URL intent resolves against the refreshed snapshot exactly once");
      uishell_sidebar_card_queue_action(&fixture, action_node, action_node.activate);
      CardCheck(action_node.activate != ANDAMENTO_NONE && uishell_sidebar_card_take_action(&fixture) == action_node.activate,
                "Open intent remains distinct from Copy URL intent");
      uishell_sidebar_card_queue_action(&fixture, action_node, copy_action);
      String8 remove_url = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"change_request\",\"id\":\"pr-281\"}},\"source_id\":\"fixture\",\"set\":{},\"unset\":[\"flotilla.forge\"]}");
      CardCheck(andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(remove_url), 0), "refresh trace removes URL metadata");
      uishell_sidebar_refresh(&fixture);
      action_index = uishell_sidebar_card_find(&fixture, fixture.card_action_target, &action_node);
      CardCheck(action_index != ANDAMENTO_NONE && action_node_copy(&fixture, action_index) == ANDAMENTO_NONE,
                "refresh keeps the entity but removes its Copy URL action");
      CardCheck(uishell_sidebar_card_take_action(&fixture) == ANDAMENTO_NONE && !fixture.card_has_action,
                "refresh drops a removed action rather than dispatching an old snapshot index");
    }
  }
  if(fixture.core)
  {
    struct { char *kind, *id, *title; } cards[] = {
      {"change_request", "pr-281", "Refreshed title"},
      {"issue", "issue-137", "Sidebar subject rows"},
      {"convoy", "build", "Ship sidebar subjects"},
      {"role", "p/governor", "governor"},
      {"project", "p", "Example project"},
      {"worktree", "hover-worktree", "Hover-card worktree"},
    };
    for(U64 i = 0; i < ArrayCount(cards); i++)
    {
      AndamentoEntity entity = uishell_sidebar_entity(uishell_sidebar_host_provider, str8_cstring(cards[i].kind), str8_cstring(cards[i].id));
      AndamentoNode node = {0}; U64 index = uishell_sidebar_card_find(&fixture, entity, &node);
      CardCheck(index != ANDAMENTO_NONE, "all six shipped detail templates have a catalog target");
      uishell_sidebar_card_set(card, node, ui_key_zero(), str8_zero(), 0, now_time_us());
      card->enriched = 1;
      UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      MemoryZeroArray(test->hover_card_keys);
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      UI_PrefWidth(ui_px(400, 1)) UI_PrefHeight(ui_em(1.6f, 1)) UI_ChildLayoutAxis(Axis2_Y)
      { uishell_sidebar_card_content(&fixture, ws, card, 0, node, index, 400, 0); }
      ui_end_build();
      B32 title = 0, identity = 0, facts = 0, expected_facts = 0;
      AndamentoDetail d = {0}; andamento_snapshot_detail(fixture.snapshot, index, &d);
      for(U64 f = 0; f < d.field_count; f++)
      {
        AndamentoDetailField field = {0}; andamento_snapshot_detail_field(fixture.snapshot, index, f, &field);
        expected_facts |= field.role == ANDAMENTO_DETAIL_FACT && field.has_value;
      }
      for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
      {
        String8 text = ui_box_display_string(box);
        title |= str8_match(text, str8_cstring(cards[i].title), 0);
        identity |= str8_match(text, str8_cstring(cards[i].id), 0);
        AndamentoDetail d = {0}; andamento_snapshot_detail(fixture.snapshot, index, &d);
        for(U64 f = 0; f < d.field_count; f++)
        {
          AndamentoDetailField field = {0}; andamento_snapshot_detail_field(fixture.snapshot, index, f, &field);
          facts |= field.role == ANDAMENTO_DETAIL_FACT && field.has_value && str8_match(text, uishell_sidebar_string(field.label), 0);
        }
        CardCheck(!str8_match(str8_prefix(text, 6), str8_lit("Title:"), 0), "typed headers do not display flat prefixes");
      }
      CardCheck(title && identity && facts == expected_facts, "each shipped template renders its header and facts");
    }
    fprintf(stderr, "Hover card diagnostics: subscription entities\n");
    // An entity from a Dashboard subscription is keyed by the subscription's
    // provider, and the host's fixture has the same kinds and IDs: hovering
    // its row opens its own card, and its relations (stamped with its
    // provider) navigate within the subscription.
    {
      UIShell_SidebarState subscribed = {.initialized = 1};
      subscribed.core = andamento_create(config.str, config.size, 0);
      CardCheck(subscribed.core != 0, "subscription fixture initializes");
      String8 provider = str8_lit("01a12a71-c309-714c-bc8f-619c6d7fbb28");
      String8 patches = str8_cstring((char *)uishell_sidebar_fixture_patches);
      for(U64 start = 0; subscribed.core && start < patches.size;)
      {
        U64 end = start;
        while(end < patches.size && patches.str[end] != '\n') { end++; }
        andamento_apply_patch_json(subscribed.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(str8(patches.str+start, end-start)), 0);
        start = end+1;
      }
      String8 subscription_patches[] = {
        str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"project\",\"id\":\"p\"}},\"source_id\":\"connector\",\"unset\":[],\"set\":{"
                 "\"flotilla.project\":{\"value\":{\"type\":\"text\",\"value\":\"p\"}},\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"Subscribed project\"}},"
                 "\"related.entities\":{\"value\":{\"type\":\"entity-refs\",\"value\":[{\"kind\":\"role\",\"id\":\"p/governor\"}]}}}}"),
        str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"role\",\"id\":\"p/governor\"}},\"source_id\":\"connector\",\"unset\":[],\"set\":{"
                 "\"flotilla.project\":{\"value\":{\"type\":\"text\",\"value\":\"p\"}},\"flotilla.role\":{\"value\":{\"type\":\"text\",\"value\":\"p/governor\"}},"
                 "\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"Subscribed governor\"}}}}"),
      };
      for(U64 i = 0; subscribed.core && i < ArrayCount(subscription_patches); i++)
      {
        CardCheck(andamento_apply_patch_json(subscribed.core, 0, uishell_sidebar_text(provider), uishell_sidebar_text(subscription_patches[i]), 0),
                  "subscription fixture patch applies");
      }
      if(subscribed.core) { uishell_sidebar_refresh(&subscribed); }
      AndamentoEntity project = uishell_sidebar_entity(provider, str8_lit("project"), str8_lit("p"));
      AndamentoNode row = {0}; B32 has_row = 0;
      for(U64 i = 0; subscribed.snapshot && i < andamento_snapshot_node_count(subscribed.snapshot) && !has_row; i++)
      {
        andamento_snapshot_node(subscribed.snapshot, i, &row);
        has_row = !row.is_section && uishell_sidebar_card_entity_match(row.entity, project);
      }
      CardCheck(has_row, "a subscription's project has its row, under its provider");
      UIShell_HoverCard *sub_card = &subscribed.cards[0];
      if(has_row)
      {
        test->mouse = v2f32(50, 115);
        uishell_sidebar_card_source_at(&subscribed, row, hover, str8_zero(), 0, 1000000);
        uishell_sidebar_card_source_at(&subscribed, row, hover, str8_zero(), 0, 1000000+UIShell_HoverCardOpenDelayUS);
        CardCheck(sub_card->open && uishell_sidebar_card_entity_match(sub_card->path[0], project),
                  "hovering a subscription entity's row opens its card, keyed by its provider");
        AndamentoNode node = {0};
        U64 index = uishell_sidebar_card_find(&subscribed, project, &node);
        CardCheck(index != ANDAMENTO_NONE && str8_match(uishell_sidebar_string(node.label), str8_lit("Subscribed project"), 0),
                  "a subscription entity's card finds its own details, not the host's same kind and ID");
        B32 title = 0;
        if(index != ANDAMENTO_NONE)
        {
          UI_EventList events = {0};
          ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
          MemoryZeroArray(test->hover_card_keys);
          UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
          UI_PrefWidth(ui_px(400, 1)) UI_PrefHeight(ui_em(1.6f, 1)) UI_ChildLayoutAxis(Axis2_Y)
          { uishell_sidebar_card_content(&subscribed, ws, sub_card, 0, node, index, 400, 0); }
          ui_end_build();
          for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
          { title |= str8_match(ui_box_display_string(box), str8_lit("Subscribed project"), 0); }
        }
        CardCheck(title, "a subscription entity's card renders its details");
        CardCheck(uishell_hover_card_test_click(ws, &subscribed, sub_card, str8_lit("Subscribed governor"), 0) && sub_card->depth == 2 &&
                  uishell_sidebar_card_entity_match(sub_card->path[1], uishell_sidebar_entity(provider, str8_lit("role"), str8_lit("p/governor"))),
                  "a subscription entity's relation navigates to the subscription's entity");
        uishell_sidebar_card_close(sub_card);
      }
      uishell_sidebar_release(&subscribed);
    }
    CardCheck(str8_match(uishell_sidebar_card_age(60000, 0), str8_lit("1m ago"), 0) &&
              str8_match(uishell_sidebar_card_age(3600000, 0), str8_lit("1h ago"), 0) &&
              str8_match(uishell_sidebar_card_age(86400000, 0), str8_lit("1d ago"), 0) &&
              str8_match(uishell_sidebar_card_age(0, 100), str8_lit("0s ago"), 0),
              "relative ages use the controller clock and saturate future observations");

    fprintf(stderr, "Hover card diagnostics: stale provider\n");
    // Exercise a stale provider's retained facts in an isolated controller
    // with the same native card renderer. The fixture's display filtering
    // cannot affect this path.
    String8 freshness_config = str8_lit(
      "region \"tree\" root-template=\"title\" placement=\"tree\"\n"
      "template \"title\" { field \"label\" literal=\"Tree\"; }\n"
      "placement \"tree\" { for \"worktree\" kind=\"worktree\" { field \"label\" key=\"display.label\"; }; }\n"
      "template \"worktree/detail\" slot=\"detail\" node-kind=\"entity\" {\n"
      " field \"branch\" role=\"fact\" section=\"facts\" label=\"Branch\" key=\"git.branch\"\n"
      " field \"upstream\" role=\"fact\" section=\"facts\" label=\"Upstream\" key=\"git.upstream\"\n"
      "}\n");
    UIShell_SidebarState freshness = {0};
    freshness.core = andamento_create(freshness_config.str, freshness_config.size, 0);
    CardCheck(freshness.core != 0, "freshness fixture initializes");
    AndamentoEntity entity = uishell_sidebar_entity(uishell_sidebar_host_provider, str8_lit("worktree"), str8_lit("stale-worktree"));
    AndamentoFact facts[] = {
      {.key = uishell_sidebar_text(str8_lit("display.label")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_lit("Worker"))},
      {.key = uishell_sidebar_text(str8_lit("git.branch")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_lit("retained-branch"))},
      {.key = uishell_sidebar_text(str8_lit("git.upstream")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_zero())},
      {.key = uishell_sidebar_text(str8_lit("action.primary.target")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_lit("worktree:stale-worktree"))},
      {.key = uishell_sidebar_text(str8_lit("action.primary.recipe")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_lit("exec sh"))},
    };
    CardCheck(andamento_apply_entity(freshness.core, 1000, uishell_sidebar_text(uishell_sidebar_host_provider), entity.kind, entity.id, uishell_sidebar_text(str8_lit("fixture")), facts, ArrayCount(facts), 0),
              "retained observation fixture applies");
    uishell_sidebar_refresh(&freshness);
    AndamentoNode node = {0}; U64 index = uishell_sidebar_card_find(&freshness, entity, &node);
    CardCheck(andamento_dispatch(freshness.core, freshness.snapshot, node.activate, 0), "freshness fixture activates its control");
    AndamentoEffects *effects = andamento_effects_take(freshness.core, 0);
    AndamentoEffect effect = {0};
    CardCheck(andamento_effects_get(effects, 0, &effect) && effect.kind == ANDAMENTO_EFFECT_MATERIALIZE,
              "freshness fixture gets a materialize effect");
    // A workspace this test makes up, read back through the index as 123456.
    UIShell_WorkspaceId worker = uishell_workspace_id_make();
    uishell_workspace_index_insert(worker, 123456);
    CardCheck(andamento_complete(freshness.core, effect.request_id, ANDAMENTO_COMPLETE_MATERIALIZE, uishell_sidebar_workspace(worker), uishell_sidebar_text(str8_zero()), 0),
              "freshness fixture binds preview identity");
    andamento_effects_release(effects);
    AndamentoWorkspace workspace = {.id = uishell_sidebar_workspace(worker), .name = uishell_sidebar_text(str8_lit("Worker")), .selected = 1};
    // Entity facts don't expire: their provider's connection drops instead.
    CardCheck(andamento_observe(freshness.core, &workspace, 1, 0, 0, 0) && andamento_tick(freshness.core, 61000, 0) &&
              andamento_provider_set_stale(freshness.core, uishell_sidebar_text(uishell_sidebar_host_provider), 1, 0),
              "retained observation ages and its provider goes stale");
    uishell_sidebar_refresh(&freshness);
    index = uishell_sidebar_card_find(&freshness, entity, &node);
    CardCheck(node.state == ANDAMENTO_LIVE && uishell_sidebar_node_cfg(&node) == 123456, "retained catalog preview identity survives staleness");
    UIShell_HoverCard *fresh_card = &freshness.cards[0];
    uishell_sidebar_card_set(fresh_card, node, ui_key_zero(), str8_zero(), 0, now_time_us());
    for(U64 enriched = 0; enriched < 2; enriched++)
    {
      fresh_card->enriched = enriched;
      UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      MemoryZeroArray(test->hover_card_keys);
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      UI_PrefWidth(ui_px(400, 1)) UI_PrefHeight(ui_em(1.6f, 1)) UI_ChildLayoutAxis(Axis2_Y)
      { uishell_sidebar_card_content(&freshness, ws, fresh_card, 0, node, index, 400, 0); }
      ui_end_build();
      B32 stale = 0, value = 0, age = 0, empty = 0;
      for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
      {
        String8 text = ui_box_display_string(box);
        value |= str8_match(text, str8_lit("retained-branch"), 0);
        stale |= str8_match(text, str8_lit("retained-branch"), 0) && box->transparency >= 0.5f;
        age |= str8_match(text, str8_lit("1m ago"), 0) && box->transparency >= 0.5f;
        empty |= str8_match(text, str8_lit("Upstream"), 0);
      }
      CardCheck(value && stale == enriched && age == enriched, "freshness styling and observation ages appear only in enriched mode");
      CardCheck(empty == enriched, "known-empty fact labels appear only in enriched mode");
    }
    uishell_sidebar_release(&freshness);

  }
  fprintf(stderr, "Hover card diagnostics: detached lifecycle\n");
  // Detached lifecycle and saved-layout round-trip use the same transitions
  // that the move controls request. No KDL or producer data is edited.
  if(fixture.snapshot)
  {
    AndamentoNode entity = {0};
    for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
    { andamento_snapshot_node(fixture.snapshot, i, &entity); if(!entity.is_section && entity.entity.id.len) { break; } }
    // Rendering sections publishes local sections, which replaces the
    // snapshot; keep the entity's text in storage the test owns.
    Arena *entity_arena = arena_alloc();
    entity.entity = uishell_sidebar_card_entity_copy(entity_arena, entity.entity);
    entity.label = uishell_sidebar_text(push_str8_copy(entity_arena, uishell_sidebar_string(entity.label)));
    entity.key = uishell_sidebar_text(push_str8_copy(entity_arena, uishell_sidebar_string(entity.key)));
    UIShell_HoverCard *original = &fixture.cards[0];
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    original->rect = r2f32p(400, 100, 800, 400); original->engaged = original->focused = 1;
    fixture.rect = r2f32p(0, 0, 320, 700);
    CardCheck(uishell_sidebar_card_target_valid(ws, 0),
              "detached targets use the shared checker with zero minimum width");
    // Dragging floats a card; it has no Float control (#269).
    CardCheck(!uishell_hover_card_test_click(ws, &fixture, original, rd_icon_kind_text_table[RD_IconKind_Window], 0) && !original->move_requested,
              "a card has no Float control");
    uishell_sidebar_card_request(original, UIShell_CardPlacement_Float);
    uishell_sidebar_detached_finish(ws);
    UIShell_HoverCard *floating = fixture.detached;
    CardCheck(!original->open && floating && floating->open && floating->placement == UIShell_CardPlacement_Float,
              "float detaches into an independent controller");
    {
      UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      test->mouse = v2f32(-100, -100);
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      { uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); }
      ui_end_build();
      CardCheck(floating->cap_shown && contains_2f32(floating->mask.rect, center_2f32(floating->cap.rect)),
                "a floating card always shows its cap, and its mask covers it");
    }
    floating->focused = 0;
    uishell_sidebar_card_tick(floating, v2f32(-100, -100), 10000000);
    uishell_sidebar_card_tick(floating, v2f32(-100, -100), 11000000);
    CardCheck(floating->open, "unfocused float survives mouse-out timeout");
    // Raw Escape is consumed by the card before the generic UI cancel slot.
    floating->moving = floating->focused = 1; fixture.drag_card = floating;
    UIShell_RegsScope(.window = ws->cfg_id) { rd_drag_begin(UIShell_ContextRegSlot_View); }
    rd_state->drag_drop_creation_name = str8_lit("sidebar_section"); rd_state->drag_drop_commit = uishell_sidebar_drag_panel_drop;
    U64 placement_before = floating->pin_size;
    WM_Event cancel_drag = {.kind = WM_EventKind_Press, .key = WM_Key_Esc};
    CardCheck(uishell_sidebar_card_wm_event(ws, &cancel_drag) && floating->open && !floating->moving &&
      !fixture.drag_card && !rd_drag_is_active() && !rd_state->drag_drop_commit && !rd_state->drag_drop_creation_name.size &&
      floating->placement == UIShell_CardPlacement_Float && floating->pin_size == placement_before,
      "Escape cancels the real card drag without pinning or closing the float");
    cancel_drag.kind = WM_EventKind_Release;
    CardCheck(uishell_sidebar_card_wm_event(ws, &cancel_drag), "drag cancel consumes its Escape release");
    // A normal tab drag after Escape must queue a move, never call the
    // cancelled card's creation callback.
    UIShell_CmdNode *before_normal_drag = rd_state->cmds[0].last;
    RD_PanelAddress normal_source = {{RD_PanelOwnerKind_Workspace}, 123}, normal_destination = {{RD_PanelOwnerKind_Workspace}, 789};
    RD_ViewAddress normal_view = {RD_ViewOwnerKind_Workspace};
    normal_view.key.u64[0] = 456;
    UIShell_RegsScope(.window = ws->cfg_id, .panel = normal_source, .view = normal_view)
    { rd_drag_begin(UIShell_ContextRegSlot_View); }
    rd_panel_drag_drop(normal_destination, Dir2_Invalid, rd_nil_view_address);
    UIShell_CmdNode *normal_move = before_normal_drag ? before_normal_drag->next : rd_state->cmds[0].first;
    CardCheck(normal_move && str8_match(normal_move->cmd.name, str8_lit("move_view"), 0) &&
      rd_panel_address_match(normal_move->cmd.regs->panel, normal_source) && rd_view_address_match(normal_move->cmd.regs->view, normal_view) &&
      rd_panel_address_match(normal_move->cmd.regs->dst_panel, normal_destination) && !rd_state->drag_drop_commit && !rd_state->drag_drop_creation_name.size,
      "normal tab drag after Escape queues its move without the cancelled creation callback");
    rd_drag_kill();
    UIShell_RegsScope(.window = ws->cfg_id) { rd_drag_begin(UIShell_ContextRegSlot_View); }
    rd_state->drag_drop_creation_name = str8_lit("sidebar_section"); rd_state->drag_drop_commit = uishell_sidebar_drag_panel_drop;
    rd_drag_kill_from_window(0);
    CardCheck(rd_drag_is_active() && rd_state->drag_drop_commit, "teardown of another window preserves the drag owner");
    rd_drag_kill_from_window(ws->cfg_id);
    CardCheck(!rd_drag_is_active() && !rd_state->drag_drop_commit && !rd_state->drag_drop_creation_name.size,
      "owner window teardown clears the creation drag callback and identity");
    for(U64 placement = 0; placement < 2; placement++)
    {
      B32 saved_outside = uishell_hover_cards_outside; uishell_hover_cards_outside = placement;
      UI_EventList events = {0}; ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      test->mouse = v2f32(-100, -100); floating->focused = 1;
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      { uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); }
      ui_end_build();
      UI_Box *root = ui_box_from_key(floating->mask.key);
      CardCheck(!ui_box_is_nil(root) && (root->flags & UI_BoxFlag_DisableFocusOverlay) &&
                (root->flags & UI_BoxFlag_DisableFocusBorder), "Near and Outside keep a clicked card surface neutral");
      WM_Event outside = {.kind = WM_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = v2f32(-100, -100)};
      uishell_sidebar_card_wm_event(ws, &outside);
      CardCheck(floating->open && !floating->focused && !test->hover_card_focus, "outside click blurs a float without closing it");
      floating->focused = 1; test->hover_card_focus = 1;
      WM_Event esc = {.kind = WM_EventKind_Press, .key = WM_Key_Esc};
      CardCheck(uishell_sidebar_card_wm_event(ws, &esc) && floating->open && !floating->focused, "focused float owns Escape and retains its placement");
      esc.kind = WM_EventKind_Release;
      CardCheck(uishell_sidebar_card_wm_event(ws, &esc), "float consumes the Escape release");
      uishell_hover_cards_outside = saved_outside;
    }
    uishell_sidebar_card_request(floating, UIShell_CardPlacement_Inline);
    uishell_sidebar_detached_finish(ws);
    CardCheck(floating->open && floating->placement == UIShell_CardPlacement_Inline && floating->source_row.size,
              "under-source transition keeps the placement's stable row identity");
    UI_EventList inline_events = {0}; ui_begin_build(ws->os, &inline_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    { uishell_sidebar_inline_ui(ws, floating->source_row, 280); }
    ui_end_build();
    CardCheck(floating->content_height > 0 && uishell_sidebar_inline_height(&fixture, floating->source_row) > floating->content_height,
              "inline measured height contributes to the tree scroll allocation");
    uishell_sidebar_card_request(floating, UIShell_CardPlacement_Pinned);
    uishell_sidebar_detached_finish(ws);
    CFG_Node *window = cfg_node_from_id(ws->cfg_id);
    UIShell_LocalPin *saved = uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity));
    CardCheck(saved && !floating->open, "pin persists entity identity in the saved panel layout");
    // The pin, by its ghost id, and its group.
    String8 saved_ghost = saved ? push_str8_copy(entity_arena, saved->ghost) : str8_lit("missing");
    String8 center_area = saved ? push_str8_copy(entity_arena, saved->group->id) : str8_zero();
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    // A transient card for an already pinned entity, dropped on that area's
    // center target, adds a second ghost and leaves the first reachable.
    uishell_hover_card_test_center_drop(ws, original, center_area);
    U64 entity_ghosts = 0;
    B32 center_reachable = 0;
    String8 dropped_ghost = str8_zero();
    UIShell_LocalGroup *center_group = uishell_sidebar_local_group(window, center_area);
    for(U64 p = 0; center_group && p < center_group->pin_count; p++)
    {
      UIShell_LocalPin *pin = &center_group->pins[p];
      center_reachable |= str8_match(pin->ghost, saved_ghost, 0);
      if(str8_match(uishell_sidebar_string(pin->target.id), uishell_sidebar_string(entity.entity.id), 0))
      { entity_ghosts++; if(!str8_match(pin->ghost, saved_ghost, 0)) { dropped_ghost = push_str8_copy(entity_arena, pin->ghost); } }
    }
    CardCheck(center_reachable && !original->open, "center drop of the same entity keeps the first ghost reachable");
    CardCheck(entity_ghosts == 2 && dropped_ghost.size && !str8_match(dropped_ghost, saved_ghost, 0),
      "a drop is explicit placement: it adds a second ghost with its own id");
    saved = uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity));
    CardCheck(saved && str8_match(saved->ghost, saved_ghost, 0), "same-entity center drop keeps the first ghost found by entity");
    if(dropped_ghost.size && uishell_sidebar_pin_by_ghost(window, dropped_ghost))
    { uishell_sidebar_card_close(uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, dropped_ghost))); uishell_sidebar_detached_finish(ws); }
    UIShell_HoverCard *center_pin = uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, saved_ghost));
    uishell_hover_card_test_center_drop(ws, center_pin, center_area);
    saved = uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity));
    CardCheck(center_pin->open && !center_pin->moving && str8_match(uishell_sidebar_card_pin_id(center_pin), saved_ghost, 0) &&
      saved && str8_match(saved->ghost, saved_ghost, 0),
      "dropping the pinned card into its own area preserves its controller and saved identity");
    AndamentoNode other_entity = entity;
    other_entity.entity.id = uishell_sidebar_text(str8_lit("center-drop-second"));
    uishell_sidebar_card_set(original, other_entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    uishell_hover_card_test_center_drop(ws, original, center_area);
    UIShell_LocalPin *second_pin = uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(other_entity));
    saved = uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity));
    CardCheck(second_pin && str8_match(second_pin->group->id, center_area, 0) &&
      saved && str8_match(saved->ghost, saved_ghost, 0) && center_pin->open,
      "center drop of a different entity appends a second pin without removing the first");
    if(second_pin) { uishell_sidebar_card_close(uishell_sidebar_saved_card(ws, second_pin)); }
    uishell_sidebar_detached_finish(ws);
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    // A second ghost of the entity (a drop is explicit placement) is its own
    // object with its own card; Pin keeps revealing the first.
    UIShell_LocalPin *another = uishell_sidebar_pin_add(ws, center_area, max_U64, uishell_sidebar_card_entity(entity),
      uishell_sidebar_string(entity.label), uishell_sidebar_string(entity.key), 0);
    String8 another_ghost = another ? push_str8_copy(entity_arena, another->ghost) : str8_zero();
    saved = uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity));
    CardCheck(another && !str8_match(another_ghost, saved_ghost, 0) && saved && str8_match(saved->ghost, saved_ghost, 0),
      "a second ghost of the entity is its own object");
    UIShell_HoverCard *another_card = another ? uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, another_ghost)) : 0;
    UIShell_HoverCard *first_card = uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, saved_ghost));
    CardCheck(another_card && another_card != first_card && str8_match(uishell_sidebar_card_pin_id(another_card), another_ghost, 0) &&
      str8_match(uishell_sidebar_card_pin_id(first_card), saved_ghost, 0),
      "each ghost has its own card");
    if(another_card) { uishell_sidebar_card_close(another_card); }
    uishell_sidebar_detached_finish(ws);
    CardCheck(uishell_sidebar_pin_by_ghost(window, saved_ghost) && !uishell_sidebar_pin_by_ghost(window, another_ghost),
      "closing one ghost leaves the other");
    UIShell_LocalPin *again = uishell_sidebar_card_pin(ws, original, 0);
    CardCheck(again && str8_match(again->ghost, saved_ghost, 0) && fixture.pin_reveal == u64_hash_from_str8(saved_ghost),
      "pinning twice reveals the same card");
    UIShell_LocalPin restored = uishell_hover_card_test_restart_pin(entity_arena, &fixture,
      uishell_sidebar_string(entity.entity.kind), uishell_sidebar_string(entity.entity.id));
    CardCheck(str8_match(restored.ghost, saved_ghost, 0) && str8_match(restored.label, uishell_sidebar_string(entity.label), 0),
              "Andamento's record keeps a pin's identity and label across a restart");
    fprintf(stderr, "Hover card diagnostics: pinned rendering\n");
    UIShell_HoverCard *pinned = uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, saved_ghost));
    String8 layout_second = uishell_hover_card_test_put_pin(entity_arena, ws, center_area, str8_lit("workspace"), str8_lit("layout-second"),
                                                            str8_lit("Second layout card"));
    UIShell_HoverCard *second_layout_card = uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, layout_second));
    F32 nil_scroll_before = ui_nil_box.view_off_target.y;
    F32 pinned_widths[] = {280, 160, 460};
    F32 previous_height = 0;
    for(U64 frame = 0; frame < ArrayCount(pinned_widths)*5; frame++)
    {
      F32 offset = (F32)(frame/5)*100;
      Rng2F32 view_rect = r2f32p(17+offset, 29+offset, 17+offset+pinned_widths[frame/5], 329+offset);
      UI_EventList events = {0}; ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      test->mouse = v2f32(-100, -100); test->hover_card_extra = 0; MemoryZeroArray(test->hover_card_keys);
      // Pins render in their local section's View, as tree rows and cards.
      UIShell_LocalGroup *pin_group = uishell_sidebar_local_group(window, center_area);
      CFG_Node *pin_view = uishell_sidebar_local_view(window, pin_group ? pin_group->section->id : str8_zero());
      UIShell_RegsScope(.window = window->id, .view = rd_view_address_from_cfg(pin_view), .panel = rd_panel_from_cfg(pin_view))
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      {
        // A real View receives window coordinates under a parent already at
        // that position. Its root must use local coordinates exactly once.
        UI_Box *view_parent;
        UI_Rect(view_rect)
        { view_parent = ui_build_box_from_key(UI_BoxFlag_Clip, ui_key_make(119166)); }
        UI_Parent(view_parent)
        { wh_view_ui_direct(wh_renderer_from_name(str8_lit("sidebar_section")), uishell_regs()->view, view_rect); }
      }
      ui_end_build();
      CardCheck(ui_nil_box.view_off_target.y == nil_scroll_before,
                "first-frame pin reveal never writes the shared nil box");
      if(frame == 0)
      { CardCheck(fixture.pin_reveal == u64_hash_from_str8(saved_ghost) && pinned->focused, "new pin keeps its reveal pending until measured"); }
      // Published on the first frame, drawn and measured on the second.
      if(frame == 2)
      { CardCheck(!fixture.pin_reveal, "measured new pin completes its reveal once drawn"); }
      UI_Box *body = ui_box_from_key(pinned->mask.key);
      // From the first frame the cards are published and placed, the upper
      // one sits inset in the section, below its header, at the View's
      // offset only once, and the next follows after the fixed gap.
      UI_Box *second_body = ui_box_from_key(second_layout_card->mask.key);
      if(frame >= 1)
      {
        Rng2F32 upper = body->rect.y0 <= second_body->rect.y0 ? body->rect : second_body->rect;
        Rng2F32 lower = body->rect.y0 <= second_body->rect.y0 ? second_body->rect : body->rect;
        CardCheck(!ui_box_is_nil(body) && !ui_box_is_nil(second_body) && upper.x0 >= view_rect.x0 && upper.x0 <= view_rect.x0+12 &&
                  upper.y0 >= view_rect.y0+floor_f32(12*2.2f) && upper.y0 <= view_rect.y0+floor_f32(12*2.2f)+8 &&
                  upper.x1 <= view_rect.x1 && body->fixed_size.y > 40,
                  "a pinned card sits inset in its section, below the header, without doubling the View offset");
        CardCheck(abs_f32(lower.y0-upper.y1-UIShell_HoverCardPinnedGapPT) < 1,
                  "successive pinned cards retain their fixed gap when the View moves");
      }
      if(frame%5 >= 3)
      { CardCheck(abs_f32(pinned->content_height-previous_height) < .5f,
                  "pinned card height converges after resizing narrow and wide"); }
      previous_height = pinned->content_height;
      uishell_sidebar_detached_bounds(ws);
      if(frame >= 1)
      { CardCheck(pinned->rect.x0 >= view_rect.x0 && pinned->rect.x1 <= view_rect.x1 && pinned->rect.y1 <= view_rect.y1,
                  "pinned WM hit geometry is clipped to the section viewport"); }
      uishell_sidebar_detached_finish(ws);
      UIShell_LocalPin *labelled = uishell_sidebar_pin_by_ghost(window, saved_ghost);
      CardCheck(labelled && str8_match(labelled->label, pinned->retained_label, 0),
                "finish pass persists the live pinned label");
      U64 label_generation = uishell_sidebar_locals(&fixture)->generation;
      uishell_sidebar_detached_finish(ws);
      CardCheck(uishell_sidebar_locals(&fixture)->generation == label_generation, "unchanged pinned label does not rewrite the pin on later finish passes");
    }
    uishell_sidebar_card_close(second_layout_card);
    uishell_sidebar_detached_finish(ws);
    original->rect = pinned->rect; original->open = 1; original->focused = 0;
    WM_Event overlap = {.kind = WM_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = center_2f32(pinned->rect)};
    uishell_sidebar_card_wm_event(ws, &overlap);
    CardCheck(original->focused && !pinned->focused, "transient overlay owns clicks above a pinned section");
    UIShell_HoverCard *top = uishell_sidebar_detached_copy(ws, original);
    top->placement = UIShell_CardPlacement_Float; top->rect = pinned->rect;
    uishell_sidebar_card_wm_event(ws, &overlap);
    CardCheck(top->focused && !original->focused && !pinned->focused, "float owns clicks above transient and pinned cards");
    uishell_sidebar_card_close(top);
    AndamentoSnapshot *snapshot = fixture.snapshot; fixture.snapshot = 0;
    UI_EventList absent_events = {0}; ui_begin_build(ws->os, &absent_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    MemoryZeroArray(test->hover_card_keys); test->hover_card_extra = 0;
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12) UI_PrefWidth(ui_px(280, 1)) UI_PrefHeight(ui_em(1.6f, 1))
    { uishell_sidebar_detached_content(&fixture, ws, pinned, u64_hash_from_str8(saved_ghost), (AndamentoNode){0}, ANDAMENTO_NONE, 280, 1); }
    ui_end_build();
    B32 absent = 0;
    for(UI_Box *b = test->root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, test->root).next)
    { absent |= str8_match(ui_box_display_string(b), str8_lit("No longer present"), 0); }
    CardCheck(absent && pinned->open && uishell_sidebar_pin_by_ghost(window, saved_ghost), "missing subject retains pinned card with an explicit marker");
    fixture.snapshot = snapshot;
    String8 area = center_area;
    // Dragged through the shared finish to a docking site below its panel.
    // The site's split command and sizing are undone below for later checks.
    B32 sized_before_move = cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")) != &cfg_nil_node;
    UIShell_CmdNode *cmds_before_move = rd_state->cmds[0].last; U64 cmd_count_before_move = rd_state->cmds[0].count;
    fixture.drag_card = pinned; pinned->moving = pinned->drag_released = 1;
    UIShell_LocalGroup *area_group = uishell_sidebar_local_group(window, area);
    CFG_Node *area_view = uishell_sidebar_local_view(window, area_group ? area_group->section->id : str8_zero());
    fixture.drop_panel = rd_panel_from_cfg(area_view); fixture.drop_direction = Dir2_Down;
    uishell_sidebar_drag_finish(ws);
    UIShell_LocalPin *moved = uishell_sidebar_pin_by_ghost(window, saved_ghost);
    CardCheck(moved && !str8_match(moved->group->id, area, 0) && pinned->open && !fixture.drag_card,
              "dragging a pinned card to a docking site moves its own ghost to the new area");
    area_group = uishell_sidebar_local_group(window, area);
    CardCheck(area_group && area_group->pin_count == 0 && rd_dock_can_close(rd_dock_view_from_address(rd_view_address_from_cfg(area_view))),
              "moving the last pin preserves an empty group whose section can be closed");
    if(moved && !str8_match(moved->group->id, area, 0))
    {
      String8 new_section = push_str8_copy(entity_arena, moved->group->section->id);
      CFG_Node *new_view = uishell_sidebar_local_view(window, new_section);
      uishell_sidebar_pin_place(window, saved_ghost, area, max_U64);
      if(new_view != &cfg_nil_node) { rd_fixture_remove(new_view); }
      uishell_sidebar_local_forget_section(window, new_section);
    }
    if(cmds_before_move) { cmds_before_move->next = 0; } else { rd_state->cmds[0].first = 0; }
    rd_state->cmds[0].last = cmds_before_move; rd_state->cmds[0].count = cmd_count_before_move;
    uishell_sidebar_manual_sizing(window, sized_before_move);
    // Moving its last pin away and back leaves the area as it was; move it
    // to a new area without a site, as before, for the checks that follow.
    uishell_sidebar_card_pin(ws, pinned, 1);
    U64 label_pos = arena_pos(pinned->arena);
    for(U64 change = 0; change < 100; change++)
    { uishell_sidebar_card_retain_label(pinned, change & 1 ? str8_lit("Live one") : str8_lit("Live two")); }
    CardCheck(arena_pos(pinned->arena) == label_pos && arena_pos(pinned->label_arena) < 4096,
              "live labels reuse separate bounded storage without growing the identity arena");
    uishell_sidebar_card_close(pinned); uishell_sidebar_detached_finish(ws);
    CardCheck(!uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity)),
              "explicit Close removes a pinned card from persisted layout");
    // Float paint order follows the mask and raw-WM priority. A closed card
    // still linked this build cannot be recycled through its mask.next link.
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    original->focused = 1;
    UIShell_HoverCard *lower_float = uishell_sidebar_detached_copy(ws, original);
    UIShell_HoverCard *upper_float = uishell_sidebar_detached_copy(ws, original);
    lower_float->placement = upper_float->placement = UIShell_CardPlacement_Float;
    lower_float->rect = upper_float->rect = r2f32p(400, 100, 800, 400);
    UI_EventList mask_events = {0};
    ui_begin_build(ws->os, &mask_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    test->mouse = v2f32(-100, -100);
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    { uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); }
    ui_end_build();
    U64 paint_index = 0, upper_paint = 0, lower_paint = 0, transient_paint = 0;
    UI_Key transient_key = ui_key_from_stringf(ui_key_zero(), "###sidebar_card_%I64u", (U64)0);
    for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_post(box, &ui_nil_box).next)
    {
      paint_index++;
      if(ui_key_match(box->key, upper_float->mask.key)) { upper_paint = paint_index; }
      if(ui_key_match(box->key, lower_float->mask.key)) { lower_paint = paint_index; }
      if(ui_key_match(box->key, transient_key)) { transient_paint = paint_index; }
    }
    CardCheck(upper_paint > lower_paint && lower_paint > transient_paint && transient_paint > 0,
              "float mask order matches reverse-sibling paint order above transient cards");
    uishell_sidebar_card_close(upper_float);
    UI_HoverCardMask *mask_tail = upper_float->mask.next;
    UIShell_HoverCard *recycled = uishell_sidebar_detached_alloc(ws);
    CardCheck(recycled != upper_float && upper_float->mask.next == mask_tail && mask_tail == &lower_float->mask,
              "same-frame allocation skips a closed float still linked above another live float");
    test->hover_card_extra = 0;
    uishell_sidebar_card_close(lower_float);
    uishell_sidebar_card_close(recycled);
    fprintf(stderr, "Hover card diagnostics: drag control\n");
    // Drive the real drag control through press, motion and release frames.
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    original->source_rect = r2f32p(20, 20, 120, 50); original->engaged = original->focused = 1;
    Vec2F32 drag_start = {0};
    for(U64 frame = 0; frame < 4; frame++)
    {
      UI_EventList events = {0};
      Vec2F32 pointer = frame < 2 ? drag_start : add_2f32(drag_start, v2f32(450, 30));
      UI_Event event = {.kind = frame == 3 ? UI_EventKind_Release : UI_EventKind_Press,
        .key = WM_Key_LeftMouseButton, .pos = pointer};
      if(frame == 1 || frame == 3) { ui_event_list_push(test->arena, &events, &event); }
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      test->mouse = pointer;
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      { uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); }
      uishell_sidebar_drag_finish(ws);
      ui_end_build();
      if(frame == 0)
      {
        drag_start = uishell_hover_card_test_title(ui_box_from_key(test->hover_card_keys[0]));
        CardCheck(drag_start.x > 0, "engaged production overlay exposes its title as the drag handle");
      }
      if(frame == 2) { CardCheck(original->moving && original->open, "native drag motion detaches the overlay from its anchor"); }
    }
    CardCheck(!original->open && fixture.detached->open && fixture.detached->placement == UIShell_CardPlacement_Float,
              "native drag release outside the sidebar creates a float");
    uishell_sidebar_card_close(fixture.detached);
    fprintf(stderr, "Hover card diagnostics: ghost rows\n");
    // Pinning a card keeps the card; collapsed, the ghost is just its row.
    // Clicking the row goes to its source; × removes the ghost, never the source.
    {
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
      UIShell_LocalPin *ghost = uishell_sidebar_card_pin(ws, original, 0);
      CardCheck(ghost && uishell_sidebar_pin_expanded(ghost), "pinning a card keeps it as a card");
      Temp ghost_scratch = scratch_begin(0, 0);
      String8 ghost_guid = ghost ? push_str8_copy(ghost_scratch.arena, ghost->ghost) : str8_lit("missing");
      CFG_Node *ghost_view = ghost ? uishell_sidebar_local_view(window, ghost->group->section->id) : &cfg_nil_node;
      UIShell_HoverCard *ghost_card = ghost ? uishell_sidebar_saved_card(ws, ghost) : &fixture.cards[1];
      uishell_sidebar_ghost_set_expanded(window, ghost_guid, 0);
      String8 card_suffix = push_str8f(ghost_scratch.arena, "###pinned_card_%S", ghost_guid);
      Rng2F32 view_rect = r2f32p(0, 0, 320, 600);
      // Frames: 0 lays out; 1-2 click the disclosure; 3 checks the card;
      // 4-5 click its ⌄ to collapse it again; 6 settles; 7-8 click the row;
      // 9 hovers; 10-11 click ×.
      Vec2F32 expand_at = {0}, row_at = {0}, remove_at = {0}, collapse_at = {0};
      B32 row_seen = 0, card_before = 0, card_after = 0, marker = 0, collapse_control = 0, row_again = 0;
      fixture.card_has_action = 0;
      B32 queued = 0, hover_offered = 0;
      // A ghost row's click opens its target, as its home row does.
      U64 workspaces_before = 0;
      for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next) { workspaces_before += str8_match(c->string, str8_lit("workspace"), 0); }
      uishell_sidebar_card_close(&fixture.cards[0]); fixture.cards[0].candidate = (AndamentoEntity){0};
      // Two settle frames publish and place the ghost before frame 0.
      B32 row_lingers = 0;
      for(U32 step = 0; step < 16; step++)
      {
        U32 frame = step < 2 ? 0 : step-2;
        B32 settled = step >= 2;
        Vec2F32 at = frame == 1 || frame == 2 ? expand_at : frame == 4 || frame == 5 ? collapse_at :
          frame >= 7 && frame <= 9 ? row_at : frame >= 10 ? remove_at : v2f32(-100, -100);
        UI_EventList events = {0};
        UI_Event event = {.kind = (frame == 1 || frame == 4 || frame == 7 || frame == 10) ? UI_EventKind_Press : UI_EventKind_Release,
          .key = WM_Key_LeftMouseButton, .pos = at};
        if(settled && (frame == 1 || frame == 2 || frame == 4 || frame == 5 || frame == 7 || frame == 8 || frame == 10 || frame == 11))
        { ui_event_list_push(test->arena, &events, &event); }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = at; test->hover_card_extra = 0; MemoryZeroArray(test->hover_card_keys);
        UIShell_RegsScope(.window = window->id, .view = rd_view_address_from_cfg(ghost_view), .panel = rd_panel_from_cfg(ghost_view))
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        {
          UI_Box *view_parent;
          UI_Rect(view_rect) { view_parent = ui_build_box_from_key(UI_BoxFlag_Clip, ui_key_make(119167)); }
          UI_Parent(view_parent) { wh_view_ui_direct(wh_renderer_from_name(str8_lit("sidebar_section")), uishell_regs()->view, view_rect); }
        }
        ui_end_build();
        // The ghost's tree row is keyed by its placement.
        String8 node_key = str8_zero();
        for(U64 i = 0; fixture.snapshot && i < andamento_snapshot_node_count(fixture.snapshot); i++)
        {
          AndamentoNode n = {0}; andamento_snapshot_node(fixture.snapshot, i, &n);
          if(str8_match(uishell_sidebar_string(n.entity.id), ghost_guid, 0)) { node_key = push_str8_copy(ghost_scratch.arena, uishell_sidebar_string(n.key)); }
        }
        // After × (frames 10-11), the ghost's row goes too, once republished.
        if(settled && frame == 13) { row_lingers = node_key.size != 0; }
        String8 row_suffix = push_str8f(ghost_scratch.arena, "###sidebar_row_%S", node_key);
        String8 expand_suffix = push_str8f(ghost_scratch.arena, "###toggle_%S", node_key);
        String8 remove_suffix = push_str8f(ghost_scratch.arena, "###close_%S", node_key);
        if(settled && frame == 8)
        {
          U64 workspaces_after = 0;
          for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next) { workspaces_after += str8_match(c->string, str8_lit("workspace"), 0); }
          queued = workspaces_after > workspaces_before || fixture.inspection[0] != 0;
        }
        // Rows and cards draw no text of their own, so match keys by parent seed.
        for(UI_Box *b = test->root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, test->root).next)
        {
          // A key is seeded from its nearest keyed ancestor.
          UI_Box *keyed = b->parent;
          while(!ui_box_is_nil(keyed) && ui_key_match(keyed->key, ui_key_zero())) { keyed = keyed->parent; }
          if(ui_box_is_nil(keyed)) { continue; }
          UI_Key seed = keyed->key;
          if(ui_key_match(b->key, ui_key_from_string(seed, row_suffix)))
          { row_seen = 1; row_at = v2f32(b->rect.x0+(b->rect.x1-b->rect.x0)*0.6f, (b->rect.y0+b->rect.y1)*0.5f); }
          if(ui_key_match(b->key, ui_key_from_string(seed, expand_suffix))) { expand_at = center_2f32(b->rect); }
          if(ui_key_match(b->key, ui_key_from_string(seed, remove_suffix))) { remove_at = center_2f32(b->rect); }
          if(frame == 3 && ui_key_match(b->key, ui_key_from_string(seed, str8_lit("###card_collapse")))) { collapse_control = 1; collapse_at = center_2f32(b->rect); }
          if(settled && frame == 6 && ui_key_match(b->key, ui_key_from_string(seed, expand_suffix))) { row_again = 1; }
          if(ui_key_match(b->key, ui_key_from_string(seed, card_suffix)))
          { if(settled && frame == 0) { card_before = 1; } if(frame == 3) { card_after = 1; } }
          if(settled && frame == 0 && str8_match(ui_box_display_string(b), str8_lit("↗"), 0)) { marker = 1; }
        }
        if(settled && frame == 9) { hover_offered = uishell_sidebar_card_entity_match(fixture.cards[0].candidate, uishell_sidebar_card_entity(entity)) ||
          (fixture.cards[0].open && uishell_sidebar_card_entity_match(fixture.cards[0].path[0], uishell_sidebar_card_entity(entity))); }
      }
      uishell_sidebar_detached_finish(ws);
      CardCheck(row_seen && marker && !card_before, "a collapsed ghost shows its row and lives-elsewhere marker, not its card");
      CardCheck(card_after, "the row's disclosure expands the ghost into its card");
      CardCheck(collapse_control, "a pinned card's header can collapse it to its row");
      CardCheck(row_again, "collapsing the card through its ⌄ gives back its row, with its disclosure");
      CardCheck(!row_lingers, "a removed ghost's row goes away");
      CardCheck(queued, "clicking a ghost row goes to its source");
      CardCheck(hover_offered, "hovering a ghost row offers its subject's hover card");
      CardCheck(!uishell_sidebar_pin_by_ghost(window, ghost_guid) && !ghost_card->open, "× on a ghost row removes the ghost");
      CardCheck(uishell_sidebar_card_find(&fixture, uishell_sidebar_card_entity(entity), 0) != ANDAMENTO_NONE, "removing a ghost leaves its source");
      fixture.card_has_action = 0;
      scratch_end(ghost_scratch);
    }
    fprintf(stderr, "Hover card diagnostics: pin control\n");
    // With the subject already pinned in the group Pin uses, Pin says Show pin
    // and a click reveals it: no menu, and no second ghost. The card runs on
    // its own clock, starting after its 100ms glide into place, so Pin is
    // where frame 0 measured it however fast the machine is.
    {
      U64 t0 = now_time_us();
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, t0);
      UIShell_LocalPin *hold = uishell_sidebar_card_pin(ws, original, 0);
      String8 hold_pin = hold ? push_str8_copy(entity_arena, hold->ghost) : str8_zero();
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, t0);
      original->source_rect = r2f32p(20, 20, 120, 50); original->engaged = original->focused = 1;
      U64 before = uishell_hover_card_test_pin_count(window);
      Vec2F32 pin_at = {0};
      B32 menu_open = 0, show_tip = 0;
      for(U64 frame = 0; frame < 4; frame++)
      {
        UI_EventList events = {0};
        UI_Event event = {.kind = frame == 2 ? UI_EventKind_Release : UI_EventKind_Press,
          .key = WM_Key_LeftMouseButton, .pos = pin_at};
        if(frame == 1 || frame == 2) { ui_event_list_push(test->arena, &events, &event); }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = frame == 0 ? v2f32(-100, -100) : pin_at;
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        { uishell_sidebar_cards_ui_at(ws, t0 + 1000000 + frame*16667, 1, 1); }
        ui_end_build();
        if(frame == 0)
        {
          UI_Box *card_root = ui_box_from_key(test->hover_card_keys[0]);
          for(UI_Box *box = card_root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, card_root).next)
          {
            String8 suffix = str8_lit("###card_pin");
            if(box->string.size >= suffix.size && str8_match(str8_postfix(box->string, suffix.size), suffix, 0))
            { pin_at = center_2f32(box->rect); break; }
          }
          CardCheck(pin_at.x > 0, "engaged card exposes Pin");
        }
        for(UI_Box *b = test->tooltip_root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, test->tooltip_root).next)
        { show_tip |= str8_match(ui_box_display_string(b), str8_lit("Show pin"), 0); }
        menu_open |= ui_any_ctx_menu_is_open();
      }
      uishell_sidebar_detached_finish(ws);
      U64 after = uishell_hover_card_test_pin_count(window);
      CardCheck(show_tip, "Pin on a subject pinned where Pin goes reads Show pin");
      CardCheck(!menu_open, "Pin opens no menu");
      CardCheck(after == before, "Pin on a subject pinned where Pin goes adds no second ghost");
      if(uishell_sidebar_pin_by_ghost(window, hold_pin)) { uishell_sidebar_card_close(uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, hold_pin))); }
      uishell_sidebar_detached_finish(ws);
    }
    RD_PanelOwner sidebar_owner = rd_panel_owner_from_host(RD_PanelOwnerKind_Sidebar, window);
    // Pin goes to the topmost local section that is showing; with none
    // showing, it makes one at the top of the sidebar.
    {
      Temp hide_scratch = scratch_begin(0, 0);
      RD_Arrangement *top_layout = rd_arrangement_from_panel_owner(hide_scratch.arena, sidebar_owner);
      RD_PanelID top_panel = rd_arrangement_add(top_layout, top_layout->root->id, 0.2f);
      rd_arrangement_reorder(top_layout, top_panel, 0);
      rd_arrangement_set(top_layout);
      UIShell_LocalGroup *top_group = uishell_sidebar_local_new_group(window, str8_lit("Top"));
      String8 top_id = push_str8_copy(entity_arena, top_group->id), top_section = push_str8_copy(entity_arena, top_group->section->id);
      CFG_Node *top_view = uishell_sidebar_local_new_view(rd_panel_address(sidebar_owner, top_panel), top_section);
      UIShell_LocalGroup *target = uishell_sidebar_pin_target(window);
      CardCheck(target && str8_match(target->id, top_id, 0), "Pin goes to the topmost showing local section");
      rd_fixture_select(top_view, 0);
      target = uishell_sidebar_pin_target(window);
      CardCheck(!target || !str8_match(target->id, top_id, 0), "a section that isn't its panel's selected tab isn't where Pin goes");
      rd_fixture_remove_panel(rd_panel_address(sidebar_owner, top_panel));
      uishell_sidebar_local_forget_section(window, top_section);
      // Hide every showing local section: Pin makes a new one at the top.
      RD_ViewAddressList hidden = {0};
      for(UIShell_LocalGroup *g = uishell_sidebar_pin_target(window); g; g = uishell_sidebar_pin_target(window))
      {
        CFG_Node *v = uishell_sidebar_local_view(window, g->section->id);
        rd_fixture_select(v, 0);
        rd_view_address_list_push(hide_scratch.arena, &hidden, rd_view_address_from_cfg(v));
      }
      UIShell_LocalGroup *made = uishell_sidebar_pin_area(ws, 0);
      String8 made_section = made ? push_str8_copy(hide_scratch.arena, made->section->id) : str8_zero();
      CFG_Node *made_view = made ? uishell_sidebar_local_view(window, made_section) : &cfg_nil_node;
      RD_PanelAddress made_panel = rd_panel_from_cfg(made_view);
      CardCheck(made && made_view != &cfg_nil_node && rd_arrangement_live(sidebar_owner)->root->first->id == made_panel.id,
                "with no section showing, Pin makes one at the top of the sidebar");
      for(RD_ViewAddressNode *n = hidden.first; n; n = n->next) { rd_fixture_select(rd_cfg_from_view(n->v), 1); }
      if(made_view != &cfg_nil_node) { rd_fixture_remove_panel(made_panel); }
      if(made) { uishell_sidebar_local_forget_section(window, made_section); }
      scratch_end(hide_scratch);
    }

    fprintf(stderr, "Hover card diagnostics: pinned drag with panel targets\n");
    UIShell_LocalPin *drag_pin_entry = uishell_sidebar_card_pin(ws, original, 0);
    UIShell_HoverCard *drag_pin = uishell_sidebar_saved_card(ws, drag_pin_entry);
    original->open = 0;
    // The grip starts a drag in the first View; a later leaf must build drop
    // sites in that same frame, before the next panel-area entry.
    // The pin's group shows in its section's View; publish it so it's placed.
    CFG_Node *drag_view = uishell_sidebar_local_view(window, drag_pin_entry->group->section->id);
    rd_fixture_select(drag_view, 1);
    {
      Temp publish_scratch = scratch_begin(0, 0);
      UIShell_ControlledSplit publish_split = uishell_root_controlled_split_from_window(publish_scratch.arena, window);
      uishell_sidebar_publish(&fixture, &publish_split);
      uishell_sidebar_refresh(&fixture);
      scratch_end(publish_scratch);
    }
    // What the panel area shows: the section's panel's tabs beside an empty
    // panel, split along x whatever the window's sidebar axis.
    RD_Arrangement *drag_layout = rd_arrangement_alloc(test->arena, sidebar_owner);
    drag_layout->root_axis = Axis2_X;
    RD_PanelID drag_panel = 0;
    {
      RD_Arrangement *live = rd_arrangement_live(sidebar_owner);
      RD_ArrangementPanel *source = rd_arrangement_panel_from_view(live, rd_view_address_from_cfg(drag_view));
      RD_PanelID drag_root = rd_arrangement_clear(drag_layout);
      drag_panel = rd_arrangement_add(drag_layout, drag_root, 0.5f);
      rd_arrangement_add(drag_layout, drag_root, 0.5f);
      for(RD_ArrangementTab *t = source->first_tab; t; t = t->next)
      { rd_arrangement_insert_tab(drag_layout, t->view, drag_panel, t->prev ? t->prev->view : rd_nil_view_address, rd_view_address_match(t->view, source->selected)); }
    }
    // Its cap comes out from the title line, not the body; once out, it
    // stays while the pointer is anywhere on the card.
    {
      Rng2F32 pin_rect = {0};
      for(U64 frame = 0; frame < 4; frame++)
      {
        Vec2F32 body = v2f32(center_2f32(pin_rect).x, pin_rect.y1-4), title = v2f32(body.x, pin_rect.y0+6);
        Vec2F32 pointer = frame == 0 ? v2f32(900, 690) : frame == 2 ? title : body;
        UI_EventList events = {0};
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = pointer;
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        {
          uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1);
          Andamento *drag_core = fixture.core; fixture.core = 0;
          Temp panel_scratch = scratch_begin(0, 0);
          UIShell_WorkspaceMount mount = uishell_workspace_mount_from_sidebar(panel_scratch.arena, window);
          mount.arrangement = rd_arrangement_copy(panel_scratch.arena, drag_layout);
          mount.panel_tree = rd_panel_tree_from_arrangement(panel_scratch.arena, mount.owner, mount.arrangement);
          UIShell_RegsScope(.window = window->id)
          { rd_panel_area_ui(panel_scratch, r2f32p(0, 0, 1000, 700), r2f32p(0, 0, 1000, 700), ws, &mount, 1, 0, 0, 0, 0); }
          scratch_end(panel_scratch);
          fixture.core = drag_core;
        }
        ui_end_build();
        if(frame == 0) { pin_rect = ui_box_from_key(drag_pin->mask.key)->rect; }
        if(frame == 1) { CardCheck(pin_rect.y1-pin_rect.y0 > 40 && !drag_pin->cap_shown, "entering a pinned card's body leaves its cap in"); }
        if(frame == 2)
        {
          B32 lifted = 0;
          for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
          { lifted |= (box->flags & UI_BoxFlag_DrawDropShadow) && box->parent->flags & UI_BoxFlag_Clip && box->rect.x0 == pin_rect.x0; }
          CardCheck(drag_pin->cap_shown && lifted, "a pinned card's title line brings out its cap and lifts it");
          // One outline goes round card and cap; neither draws its own.
          UI_Box *outline = ui_box_from_key(ui_key_from_stringf(drag_pin->mask.key, "outline"));
          Rng2F32 both = union_2f32(pin_rect, drag_pin->cap.rect);
          CardCheck(!ui_box_is_nil(outline) && (outline->flags & UI_BoxFlag_DrawBorder) &&
                    length_2f32(sub_2f32(outline->rect.p0, both.p0)) < 1 && length_2f32(sub_2f32(outline->rect.p1, both.p1)) < 1,
                    "one outline goes round a pinned card and its cap");
          CardCheck(!(ui_box_from_key(drag_pin->mask.key)->flags & UI_BoxFlag_DrawBorder), "a pinned card with its cap out draws no border of its own");
        }
        if(frame == 3) { CardCheck(drag_pin->cap_shown, "a pinned card's cap stays while the pointer is on its body"); }
      }
    }
    Vec2F32 pinned_drag_start = {0}, pinned_drag_size = {0};
    for(U64 frame = 0; frame < 5; frame++)
    {
      Vec2F32 pointer = frame < 2 ? pinned_drag_start : add_2f32(pinned_drag_start, v2f32(40, 720));
      UI_EventList events = {0};
      if(frame == 1 || frame == 4)
      {
        WM_Event raw = {.kind = frame == 1 ? WM_EventKind_Press : WM_EventKind_Release,
          .key = WM_Key_LeftMouseButton, .pos = pointer};
        if(!uishell_sidebar_card_wm_event(ws, &raw))
        {
          UI_Event event = {.kind = frame == 1 ? UI_EventKind_Press : UI_EventKind_Release,
            .key = WM_Key_LeftMouseButton, .pos = pointer};
          ui_event_list_push(test->arena, &events, &event);
        }
      }
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      test->mouse = pointer;
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      {
        uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1);
        Andamento *drag_core = fixture.core; fixture.core = 0;
        Temp panel_scratch = scratch_begin(0, 0);
        UIShell_WorkspaceMount mount = uishell_workspace_mount_from_sidebar(panel_scratch.arena, window);
        mount.arrangement = rd_arrangement_copy(panel_scratch.arena, drag_layout);
        mount.panel_tree = rd_panel_tree_from_arrangement(panel_scratch.arena, mount.owner, mount.arrangement);
        UIShell_RegsScope(.window = window->id)
        { rd_panel_area_ui(panel_scratch, r2f32p(0, 0, 1000, 700), r2f32p(0, 0, 1000, 700), ws, &mount, 1, 0, 0, 0, 0); }
        scratch_end(panel_scratch);
        fixture.core = drag_core;
      }
      uishell_sidebar_drag_finish(ws);
      ui_end_build();
      if(frame == 0)
      {
        pinned_drag_start = uishell_hover_card_test_title(ui_box_from_key(drag_pin->mask.key));
        CardCheck(pinned_drag_start.x > 0, "pinned card exposes its own title handle inside the section");
      }
      // The size it's grabbed at: the cap's details can fill it in by then.
      if(frame == 1) { pinned_drag_size = dim_2f32(drag_pin->rect); }
      // A pinned card is a peek until the pointer enters it; its cap then
      // offers × but not Pin, and takes the pointer from what it covers.
      if(frame == 0) { CardCheck(!drag_pin->cap_shown, "a pinned card at rest shows no cap"); }
      if(frame == 1)
      {
        B32 pin = 0, close = 0, masked = 0;
        UI_Box *cap = ui_box_from_key(drag_pin->cap.key);
        for(UI_Box *box = cap; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, cap).next)
        {
          pin |= str8_match(ui_box_display_string(box), str8_lit("🖈"), 0);
          close |= str8_match(ui_box_display_string(box), str8_lit("×"), 0);
        }
        for(UI_HoverCardMask *m = test->hover_card_extra; m; m = m->next) { masked |= m == &drag_pin->cap; }
        CardCheck(drag_pin->cap_shown && close && !pin && masked, "a pinned card's cap shows on pointer enter, with × and no Pin");
      }
      if(frame == 3) { CardCheck(drag_pin->cap_drawn && !drag_pin->cap_shown, "a card being dragged keeps its cap, inert"); }
      if(frame == 2 || frame == 3)
      {
        CardCheck(drag_pin->moving && drag_pin->open && rd_drag_is_active(), "pinned drag stays active while actual panel drop targets build");
        CardCheck(length_2f32(sub_2f32(dim_2f32(drag_pin->rect), pinned_drag_size)) < 1.f &&
          length_2f32(sub_2f32(drag_pin->rect.p0, add_2f32(drag_pin->move_origin, v2f32(40,720)))) < 1.f,
          "moving pin keeps its grabbed size and position instead of being laid out again in its source");
      }
    }
    // Released below the panel area: over the pinned list it would reposition.
    CardCheck(drag_pin->open && drag_pin->placement == UIShell_CardPlacement_Float &&
      !uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity)),
      "dragging an existing pin outside its section produces one float and removes the saved pin");
    // The title line drags a card too (drag-model.md, decision 4): the float
    // follows a title drag exactly as it follows the grip.
    {
      ui_kill_action();
      UI_Key title_seed = ui_key_zero();
      Vec2F32 title_start = {0}, title_origin = {0};
      for(U64 frame = 0; frame < 5; frame++)
      {
        Vec2F32 pointer = frame < 2 ? title_start : add_2f32(title_start, v2f32(60, 40));
        UI_EventList events = {0};
        if(frame == 1 || frame == 4)
        {
          WM_Event raw = {.kind = frame == 1 ? WM_EventKind_Press : WM_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = pointer};
          if(!uishell_sidebar_card_wm_event(ws, &raw))
          {
            UI_Event event = {.kind = frame == 1 ? UI_EventKind_Press : UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = pointer};
            ui_event_list_push(test->arena, &events, &event);
          }
        }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = pointer;
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12) { uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); }
        uishell_sidebar_drag_finish(ws);
        ui_end_build();
        if(frame == 0)
        {
          UI_Box *card_root = ui_box_from_key(drag_pin->mask.key);
          // Rows are unkeyed, so the title's key is seeded by a keyed ancestor.
          for(UI_Box *box = card_root; !ui_box_is_nil(box) && title_start.x == 0; box = ui_box_rec_df_pre(box, card_root).next)
          {
            for(UI_Box *a = box->parent; !ui_box_is_nil(a); a = a->parent)
            { if(ui_key_match(box->key, ui_key_from_string(a->key, str8_lit("###card_title")))) { title_start = center_2f32(box->rect); title_seed = box->key; break; } }
          }
          title_origin = drag_pin->rect.p0;
          CardCheck(title_start.x > 0, "a card exposes its title line as a drag handle");
        }
        if(frame == 2 || frame == 3)
        {
          CardCheck(drag_pin->moving && length_2f32(sub_2f32(drag_pin->rect.p0, add_2f32(title_origin, v2f32(60, 40)))) < 1.f,
                    "dragging a card's title moves it like the grip");
        }
      }
      CardCheck(drag_pin->open && !drag_pin->moving && drag_pin->placement == UIShell_CardPlacement_Float,
                "a title drag of a float ends as a float");
      (void)title_seed;
    }
    // A float's cap is the obvious place to grab it: its background drags.
    {
      ui_kill_action();
      Vec2F32 grab = {0}, origin = {0};
      for(U64 frame = 0; frame < 5; frame++)
      {
        Vec2F32 pointer = frame < 2 ? grab : add_2f32(grab, v2f32(50, 30));
        UI_EventList events = {0};
        if(frame == 1 || frame == 4)
        {
          WM_Event raw = {.kind = frame == 1 ? WM_EventKind_Press : WM_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = pointer};
          if(!uishell_sidebar_card_wm_event(ws, &raw))
          {
            UI_Event event = {.kind = frame == 1 ? UI_EventKind_Press : UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = pointer};
            ui_event_list_push(test->arena, &events, &event);
          }
        }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = pointer;
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12) { uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); }
        uishell_sidebar_drag_finish(ws);
        ui_end_build();
        // Its left end holds no controls.
        if(frame == 0) { grab = v2f32(drag_pin->cap.rect.x0+8, center_2f32(drag_pin->cap.rect).y); origin = drag_pin->rect.p0; }
        if(frame == 0) { CardCheck(drag_pin->cap_shown && dim_2f32(drag_pin->cap.rect).y > 0, "a float shows its cap"); }
        if(frame == 2 || frame == 3)
        {
          CardCheck(drag_pin->moving && length_2f32(sub_2f32(drag_pin->rect.p0, add_2f32(origin, v2f32(50, 30)))) < 1.f,
                    "dragging a float's cap moves the float");
          CardCheck(drag_pin->cap_drawn, "a float keeps its cap while it's dragged");
        }
      }
      CardCheck(drag_pin->open && !drag_pin->moving && drag_pin->placement == UIShell_CardPlacement_Float,
                "a cap drag of a float ends as a float");
    }
    // The pins' section title drags its View, as any section title does.
    {
      ui_kill_action(); rd_drag_kill();
      CFG_Node *area_view = &cfg_nil_node;
      for(RD_ArrangementTab *t = rd_arrangement_panel_from_id(drag_layout, drag_panel)->first_tab; t && area_view == &cfg_nil_node; t = t->next)
      {
        CFG_Node *v = rd_cfg_from_view(t->view);
        if(str8_match(v->string, str8_lit("sidebar_section"), 0)) { area_view = v; }
      }
      String8 area_title = push_str8f(test->arena, "###section_%S", cfg_node_child_from_string(area_view, str8_lit("section"))->first->string);
      CardCheck(area_view != &cfg_nil_node, "the emptied pins' section remains as a View");
      Vec2F32 area_start = {0};
      B32 area_drag = 0;
      for(U64 frame = 0; frame < 4; frame++)
      {
        Vec2F32 pointer = frame < 2 ? area_start : add_2f32(area_start, v2f32(3*UIShell_DragThresholdPT, UIShell_DragThresholdPT));
        UI_EventList events = {0};
        if(frame == 1)
        {
          UI_Event event = {.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = pointer};
          ui_event_list_push(test->arena, &events, &event);
        }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = frame == 0 ? v2f32(-100, -100) : pointer;
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        {
          Andamento *area_core = fixture.core; fixture.core = 0;
          Temp panel_scratch = scratch_begin(0, 0);
          UIShell_WorkspaceMount mount = uishell_workspace_mount_from_sidebar(panel_scratch.arena, window);
          mount.arrangement = rd_arrangement_copy(panel_scratch.arena, drag_layout);
          mount.panel_tree = rd_panel_tree_from_arrangement(panel_scratch.arena, mount.owner, mount.arrangement);
          UIShell_RegsScope(.window = window->id)
          { rd_panel_area_ui(panel_scratch, r2f32p(0, 0, 1000, 700), r2f32p(0, 0, 1000, 700), ws, &mount, 1, 0, 0, 0, 0); }
          scratch_end(panel_scratch);
          fixture.core = area_core;
        }
        ui_end_build();
        if(frame == 0)
        {
          for(UI_Box *box = test->root; !ui_box_is_nil(box) && area_start.x == 0; box = ui_box_rec_df_pre(box, test->root).next)
          {
            for(UI_Box *a = box->parent; !ui_box_is_nil(a); a = a->parent)
            { if(ui_key_match(box->key, ui_key_from_string(a->key, area_title))) { area_start = center_2f32(box->rect); break; } }
          }
          CardCheck(area_start.x > 0, "the pins' section exposes its title as a drag handle");
        }
        if(frame == 3) { area_drag = rd_drag_is_active() && rd_view_address_match(rd_state->drag_drop_regs->view, rd_view_address_from_cfg(area_view)); }
      }
      CardCheck(area_drag, "dragging the pins' section title starts its View's docking drag");
      rd_drag_kill(); ui_kill_action();
    }
    // The raw release is consumed by card ownership before UI events. A
    // former pin's title handle must release too, or the next build restarts drag.
    fprintf(stderr, "Hover card diagnostics: float raw release\n");
    Rng2F32 float_window = wm_client_rect_from_window(ws->os);
    Vec2F32 float_drag_delta = v2f32(dim_2f32(float_window).x+80, 20);
    for(U64 end_kind = 0; end_kind < 3; end_kind++)
    {
      ui_kill_action();
      Vec2F32 float_drag_start = {0}, released_position = {0};
      // End beyond the window edge to exercise the next frame's legitimate
      // clamp on every platform. Test cursor motion after that settled frame.
      for(U64 frame = 0; frame < 6; frame++)
      {
        UI_EventList events = {0};
        Vec2F32 pointer = frame < 2 ? float_drag_start : add_2f32(float_drag_start, float_drag_delta);
        if(frame == 5) { pointer = add_2f32(pointer, v2f32(40, 30)); }
        if(frame == 1 || frame == 3)
        {
          WM_Event raw = {.kind = frame == 1 ? WM_EventKind_Press : WM_EventKind_Release,
            .key = WM_Key_LeftMouseButton, .pos = pointer};
          if(frame == 3 && end_kind == 1) { raw.kind = WM_EventKind_Press; raw.key = WM_Key_Esc; }
          if(frame == 3 && end_kind == 2) { raw.kind = WM_EventKind_WindowLoseFocus; }
          B32 consumed = uishell_sidebar_card_wm_event(ws, &raw);
          if(frame == 3) { CardCheck(consumed == (end_kind != 2), "float drag end retains raw event ownership"); }
          if(!consumed && (frame == 1 || end_kind == 0))
          {
            UI_Event event = {.kind = frame == 1 ? UI_EventKind_Press : UI_EventKind_Release,
              .key = WM_Key_LeftMouseButton, .pos = pointer};
            ui_event_list_push(test->arena, &events, &event);
          }
        }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = pointer;
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        { uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); }
        uishell_sidebar_drag_finish(ws);
        ui_end_build();
        if(frame == 0)
        {
          float_drag_start = uishell_hover_card_test_title(ui_box_from_key(drag_pin->mask.key));
          CardCheck(float_drag_start.x > 0, "former pin exposes its title handle");
        }
        if(frame == 2) { CardCheck(drag_pin->moving && fixture.drag_card == drag_pin, "the title handle starts the real card drag"); }
        if(frame == 3)
        {
          CardCheck(!drag_pin->moving && !fixture.drag_card && !rd_drag_is_active() &&
            ui_key_match(ui_active_key(UI_MouseButtonKind_Left), ui_key_zero()),
            "raw float release clears both card drag ownership and the active grip");
        }
        if(frame == 4)
        {
          released_position = drag_pin->rect.p0;
          CardCheck(!drag_pin->moving && !fixture.drag_card && !rd_drag_is_active() &&
            drag_pin->rect.x0 >= float_window.x0+9 && drag_pin->rect.x1 <= float_window.x1-9,
            "finished float clamps inside the window without restarting its drag");
        }
        if(frame == 5)
        { CardCheck(!drag_pin->moving && !fixture.drag_card && !rd_drag_is_active() &&
            length_2f32(sub_2f32(drag_pin->rect.p0, released_position)) < 1,
            "moving the cursor after release neither restarts the float drag nor moves the card"); }
      }
      rd_drag_kill(); fixture.drag_card = 0; drag_pin->moving = 0; ui_kill_action();
      if(end_kind == 1)
      {
        WM_Event esc_release = {.kind = WM_EventKind_Release, .key = WM_Key_Esc};
        CardCheck(uishell_sidebar_card_wm_event(ws, &esc_release), "float drag Escape owns its release edge");
      }
    }
    uishell_sidebar_card_close(drag_pin);

    // A source-bound card closes when its exact placement is removed.
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    UIShell_HoverCard *removed = uishell_sidebar_detached_copy(ws, original);
    removed->placement = UIShell_CardPlacement_Inline;
    removed->source_key = removed->source_row = str8_lit("removed-placement");
    uishell_sidebar_detached_finish(ws);
    CardCheck(!removed->open, "inline card closes when its source placement disappears");
    // A pin is one per ghost id: set again under its id, it stays one pin.
    UIShell_LocalPin *unique = uishell_sidebar_card_pin(ws, original, 0);
    String8 unique_ghost = unique ? push_str8_copy(entity_arena, unique->ghost) : str8_lit("missing");
    String8 tolerance_area = unique ? push_str8_copy(entity_arena, unique->group->id) : str8_zero();
    U64 pins_before_copy = uishell_hover_card_test_pin_count(window);
    if(unique) { UIShell_LocalPin copy = *unique; uishell_sidebar_local_put_pin(&fixture, &copy, tolerance_area, unique->group->pin_count); }
    CardCheck(uishell_hover_card_test_pin_count(window) == pins_before_copy && uishell_sidebar_pin_by_ghost(window, unique_ghost),
              "layout copies reconcile to one card per ghost");
    // A second ghost of the same entity is deliberate and survives.
    String8 second_ghost = uishell_hover_card_test_put_pin(entity_arena, ws, tolerance_area, uishell_sidebar_string(entity.entity.kind),
                                                           uishell_sidebar_string(entity.entity.id), str8_zero());
    CardCheck(uishell_sidebar_pin_by_ghost(window, second_ghost) && uishell_sidebar_pin_by_ghost(window, unique_ghost),
              "distinct ghosts of one entity both survive reconciliation");
    uishell_sidebar_pin_remove(window, second_ghost);
    // Pins tolerate future kinds and missing identity fields through
    // Andamento's record and render.
    String8 unknown = uishell_hover_card_test_put_pin(entity_arena, ws, tolerance_area, str8_lit("future_kind"), str8_lit("orphan"),
                                                      str8_lit("Future pin"));
    String8 incomplete = uishell_hover_card_test_put_pin(entity_arena, ws, tolerance_area, str8_zero(), str8_zero(), str8_lit("Incomplete pin"));
    CardCheck(uishell_sidebar_pin_by_ghost(window, unknown) && uishell_sidebar_pin_by_ghost(window, incomplete) &&
              uishell_sidebar_pin_by_ghost(window, unique_ghost),
              "unknown kinds and incomplete entries tolerate reconciliation without losing valid pins");
    UIShell_LocalPin future_restored = uishell_hover_card_test_restart_pin(entity_arena, &fixture, str8_lit("future_kind"), str8_lit("orphan"));
    CardCheck(future_restored.ghost.size && str8_match(future_restored.label, str8_lit("Future pin"), 0),
              "a restart through Andamento's record keeps unknown pin kinds and fallback labels");
    // Published and placed, they render in their section's View.
    {
      Temp publish_scratch = scratch_begin(0, 0);
      UIShell_ControlledSplit publish_split = uishell_root_controlled_split_from_window(publish_scratch.arena, window);
      uishell_sidebar_publish(&fixture, &publish_split);
      uishell_sidebar_refresh(&fixture);
      scratch_end(publish_scratch);
    }
    UIShell_LocalGroup *tolerance_group = uishell_sidebar_local_group(window, tolerance_area);
    CFG_Node *tolerance_view = uishell_sidebar_local_view(window, tolerance_group ? tolerance_group->section->id : str8_zero());
    UI_EventList tolerance_events = {0};
    ui_begin_build(ws->os, &tolerance_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    test->hover_card_extra = 0; MemoryZeroArray(test->hover_card_keys);
    UIShell_RegsScope(.window = window->id, .view = rd_view_address_from_cfg(tolerance_view), .panel = rd_panel_from_cfg(tolerance_view))
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    { wh_view_ui_direct(wh_renderer_from_name(str8_lit("sidebar_section")), uishell_regs()->view, r2f32p(17, 29, 297, 1629)); }
    ui_end_build();
    U64 missing_markers = 0;
    for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
    { missing_markers += str8_match(ui_box_display_string(box), str8_lit("No longer present"), 0); }
    CardCheck(missing_markers >= 2 && uishell_sidebar_pin_by_ghost(window, unknown) && uishell_sidebar_pin_by_ghost(window, incomplete) &&
              uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, unknown))->open &&
              uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, incomplete))->open,
              "unknown and malformed saved pins render a missing marker and remain explicitly closable");
    uishell_sidebar_pin_remove(window, unknown); uishell_sidebar_pin_remove(window, incomplete);
    uishell_sidebar_pin_remove(window, unique_ghost);
    // A wrong controlled-split owner rejects creation before any layout edit.
    for(U64 root_exists = 0; root_exists < 2; root_exists++)
    {
      CFG_Node *invalid_owner = cfg_node_new(rd_state->cfg, window, str8_lit("invalid_owner"));
      if(root_exists)
      {
        CFG_Node *invalid_views = cfg_node_new(rd_state->cfg, invalid_owner, str8_lit("window_views"));
        cfg_node_new(rd_state->cfg, invalid_views, str8_lit("sidebar_section"));
      }
      Temp failed_scratch = scratch_begin(0, 0);
      String8 before_failure = cfg_string_from_tree(failed_scratch.arena, rd_state->cfg_schema_table, str8_zero(), invalid_owner);
      CFG_ID valid_window_id = ws->cfg_id;
      ws->cfg_id = invalid_owner->id;
      UIShell_LocalPin *failed_pin = uishell_sidebar_card_pin(ws, original, 1);
      ws->cfg_id = valid_window_id;
      String8 after_failure = cfg_string_from_tree(failed_scratch.arena, rd_state->cfg_schema_table, str8_zero(), invalid_owner);
      CardCheck(!failed_pin && str8_match(before_failure, after_failure, 0),
                "rejected creation removes tentative panels/root and preserves the existing tree");
      scratch_end(failed_scratch);
      cfg_node_release(rd_state->cfg, invalid_owner);
    }
    fprintf(stderr, "Hover card diagnostics: merged layout\n");
    // Adding a new area to a merged root leaf preserves its existing tabs.
    {
      // No docking site from an earlier drag: Pin's own placement.
      fixture.drop_panel = rd_nil_panel_address;
      RD_FixtureSaved saved = rd_fixture_isolate(entity_arena, sidebar_owner);
      RD_Arrangement *merged = rd_arrangement_from_panel_owner(entity_arena, sidebar_owner);
      RD_PanelID merged_root = rd_arrangement_clear(merged);
      CFG_Node *existing_view = uishell_sidebar_section_view_new(window, merged, merged_root, rd_nil_view_address, str8_lit("projects"), str8_lit("Projects"));
      rd_arrangement_set(merged);
      unique = uishell_sidebar_card_pin(ws, original, 1);
      RD_Arrangement *live = rd_arrangement_live(sidebar_owner);
      RD_ArrangementPanel *existing_panel = rd_arrangement_panel_from_view(live, rd_view_address_from_cfg(existing_view));
      CardCheck(unique && existing_panel->id != merged_root &&
                existing_panel->parent->id == merged_root, "new pinned area preserves tabs in a merged sidebar root");
      F32 sum = 0;
      for(RD_ArrangementPanel *n = live->root->first; n != &rd_nil_arrangement_panel; n = n->next) { sum += n->weight; }
      CardCheck(abs_f32(sum-1) < .0001f, "new area normalizes saved panel ratios");
      rd_fixture_restore(saved);
    }

    fprintf(stderr, "Hover card diagnostics: shared panel drops\n");
    // A center joins its target; directional drops use the ordinary split
    // command and retain the measured ratios, rather than global rebalancing.
    RD_FixtureSaved drops_saved = rd_fixture_isolate(entity_arena, sidebar_owner);
    B32 sized_before = cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")) != &cfg_nil_node;
    Dir2 directions[] = {Dir2_Invalid, Dir2_Up, Dir2_Down, Dir2_Left, Dir2_Right};
    for(U64 nested = 0; nested < 2; nested++)
    for(U64 d = 0; d < ArrayCount(directions); d++)
    {
      RD_Arrangement *layout = rd_arrangement_alloc(entity_arena, sidebar_owner);
      layout->root_axis = nested && axis2_from_dir2(directions[d]) == Axis2_Y ? Axis2_X : Axis2_Y;
      RD_PanelID destination = rd_arrangement_clear(layout), neighbour = 0;
      if(nested)
      {
        RD_PanelID host = destination;
        destination = rd_arrangement_add(layout, host, 0.6f);
        neighbour = rd_arrangement_add(layout, host, 0.4f);
        uishell_sidebar_section_view_new(window, layout, neighbour, rd_nil_view_address, str8_lit("fixture-neighbour"), str8_lit("Neighbour"));
      }
      rd_arrangement_set(layout);
      uishell_sidebar_local_ensure_default(&fixture);
      U64 sections_before = uishell_sidebar_window_locals(window)->section_count;
      UIShell_LocalGroup *existing_local = uishell_sidebar_local_new_group(window, str8_lit("Pinned"));
      String8 existing_group = push_str8_copy(entity_arena, existing_local->id);
      RD_PanelAddress destination_panel = rd_panel_address(sidebar_owner, destination);
      CFG_Node *existing = uishell_sidebar_local_new_view(destination_panel, existing_local->section->id);
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
      original->engaged = original->focused = original->moving = original->drag_released = 1;
      fixture.drag_card = original;
      UIShell_RegsScope(.window = window->id, .view = rd_nil_view_address, .panel = rd_nil_panel_address)
      { rd_drag_begin(UIShell_ContextRegSlot_View); }
      rd_state->drag_drop_creation_name = str8_lit("sidebar_section");
      rd_state->drag_drop_commit = uishell_sidebar_drag_panel_drop;
      CardCheck(rd_panel_drag_target(rd_nil_view_address, destination_panel, 320), "creation drag uses the registered panel validity checker");
      rd_state->drag_drop_state = RD_DragDropState_Dropping;
      UIShell_CmdNode *before_drop = rd_state->cmds[0].last;
      if(rd_drag_drop()) { rd_panel_drag_drop(destination_panel, directions[d], rd_view_address_from_cfg(existing)); }
      UI_EventList drop_events = {0};
      ui_begin_build(ws->os, &drop_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      test->mouse = v2f32(500, 100);
      uishell_sidebar_drag_finish(ws);
      ui_end_build();
      // The ghost the drop made: in the existing group (centre), or in the
      // section the site made (an edge); earlier ghosts of the entity stay.
      UIShell_LocalPin *entry = 0;
      {
        UIShell_Locals *locals = uishell_sidebar_window_locals(window);
        for(U64 n = sections_before; n < locals->section_count; n++)
        for(U64 g = 0; g < locals->sections[n].group_count; g++)
        {
          UIShell_LocalGroup *group = &locals->sections[n].groups[g];
          if(group->pin_count && (directions[d] == Dir2_Invalid) == str8_match(group->id, existing_group, 0)) { entry = &group->pins[0]; }
        }
      }
      UIShell_LocalGroup *area = entry ? entry->group : 0;
      for(UIShell_CmdNode *n = before_drop ? before_drop->next : rd_state->cmds[0].first; n; n = n->next)
      {
        if(str8_match(n->cmd.name, str8_lit("split_panel"), 0)) UIShell_RegsScope()
        { MemoryCopyStruct(uishell_regs(), n->cmd.regs); uishell_dispatch_panel_command(n->cmd.name); }
      }
      Temp drop_scratch = scratch_begin(0, 0);
      UIShell_WorkspaceMount drop_mount = uishell_workspace_mount_from_sidebar(drop_scratch.arena, window);
      RD_PanelNode *drop_root = drop_mount.panel_tree.root;
      CardCheck(entry && !original->open &&
        cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")) != &cfg_nil_node,
        "accepted card drop pins once and opts into saved panel sizing");
      // A split across the destination's parent's axis splits it; along it,
      // it adds a sibling there.
      RD_PanelNode *destination_node = rd_panel_node_from_id(drop_root, destination);
      RD_PanelNode *target_root = destination_node->first != &rd_nil_panel_node ? destination_node : destination_node->parent;
      if(nested)
      { CardCheck(drop_root->child_count == 2 && rd_panel_address_match(drop_root->last->address, rd_panel_address(sidebar_owner, neighbour)) &&
          abs_f32(drop_root->last->pct_of_parent-.4f) < .0001f, "nested drop preserves the other panel and outer allocation"); }
      if(directions[d] == Dir2_Invalid)
      { CardCheck(area && str8_match(area->id, existing_group, 0) && destination_node->child_count == 0, "center drop joins the group of the exact existing section"); }
      else
      {
        Side side = side_from_dir2(directions[d]);
        RD_PanelNode *placed = side == Side_Min ? target_root->first : target_root->last;
        CardCheck(target_root->child_count == 2 && target_root->split_axis == axis2_from_dir2(directions[d]) &&
          area && rd_panel_address_match(placed->address, rd_panel_from_cfg(uishell_sidebar_local_view(window, area->section->id))) && abs_f32(placed->pct_of_parent-.5f) < .0001f,
          "directional card drop matches the ordinary half-panel split and side");
      }
      scratch_end(drop_scratch);
      // The sections this case made go with their Views.
      {
        UIShell_Locals *locals = uishell_sidebar_window_locals(window);
        String8List made = {0};
        for(U64 n = sections_before; n < locals->section_count; n++) { str8_list_push(entity_arena, &made, push_str8_copy(entity_arena, locals->sections[n].id)); }
        for(String8Node *n = made.first; n; n = n->next) { uishell_sidebar_local_forget_section(window, n->string); }
      }
      rd_state->drag_drop_creation_name = str8_zero(); rd_state->drag_drop_commit = 0;
    }
    uishell_sidebar_manual_sizing(window, sized_before);
    rd_fixture_restore(drops_saved);

    // Dropping the last workspace tab out of a nested split closes its empty
    // source Panel. Collapsing that branch flattens the surviving split.
    fprintf(stderr, "Hover card diagnostics: focused source collapse\n");
    RD_FixtureSaved collapse_saved = rd_fixture_isolate(entity_arena, sidebar_owner);
    RD_Arrangement *collapse = rd_arrangement_alloc(entity_arena, sidebar_owner);
    collapse->root_axis = Axis2_X;
    RD_PanelID collapse_root = rd_arrangement_clear(collapse);
    RD_PanelID branch = rd_arrangement_add(collapse, collapse_root, 0.6f);
    RD_PanelID other = rd_arrangement_add(collapse, collapse_root, 0.4f);
    uishell_sidebar_section_view_new(window, collapse, other, rd_nil_view_address, str8_lit("fixture-other"), str8_lit("Other"));
    RD_PanelID source_panel = rd_arrangement_add(collapse, branch, 0.5f);
    CFG_Node *moving_section = uishell_sidebar_section_view_new(window, collapse, source_panel, rd_nil_view_address, str8_lit("workspaces"), str8_lit("Workspaces"));
    RD_ViewAddress moving_address = rd_view_address_from_cfg(moving_section);
    RD_PanelID survivor = rd_arrangement_add(collapse, branch, 0.5f);
    RD_PanelID first_survivor = rd_arrangement_add(collapse, survivor, 0.25f);
    rd_arrangement_move_tab(collapse, rd_view_address_from_cfg(rd_view_alloc(sidebar_owner, str8_lit("pinned_cards"), str8_zero())), first_survivor, rd_nil_view_address);
    RD_PanelID last_survivor = rd_arrangement_add(collapse, survivor, 0.75f);
    uishell_sidebar_section_view_new(window, collapse, last_survivor, rd_nil_view_address, str8_lit("fixture-survivor"), str8_lit("Survivor"));
    rd_arrangement_set(collapse);
    rd_panel_focus_mark(rd_panel_address(sidebar_owner, source_panel));
    UIShell_CmdNode *before_collapse = rd_state->cmds[0].last;
    UIShell_RegsScope(.window = window->id, .panel = rd_panel_address(sidebar_owner, source_panel), .view = moving_address, .tab = rd_nil_view_address)
    { rd_drag_begin(UIShell_ContextRegSlot_View); }
    rd_panel_drag_drop(rd_panel_address(sidebar_owner, other), Dir2_Up, rd_nil_view_address);
    rd_drag_kill();
    for(UIShell_CmdNode *n = before_collapse ? before_collapse->next : rd_state->cmds[0].first; n; n = n->next)
    {
      if(str8_match(n->cmd.name, str8_lit("split_panel"), 0)) UIShell_RegsScope()
      {
        MemoryCopyStruct(uishell_regs(), n->cmd.regs);
        uishell_dispatch_panel_command(n->cmd.name);
      }
    }
    {
      RD_Arrangement *live = rd_arrangement_live(sidebar_owner);
      RD_ArrangementPanel *first = rd_arrangement_panel_from_id(live, first_survivor), *last = rd_arrangement_panel_from_id(live, last_survivor);
      CardCheck(!rd_panel_exists(rd_panel_address(sidebar_owner, source_panel)) && rd_panel_from_view(moving_address).id != 0,
        "workspace section drop splits its destination and closes the empty source Panel");
      CardCheck(!rd_panel_exists(rd_panel_address(sidebar_owner, survivor)) &&
        first->parent->id == collapse_root && last->parent->id == collapse_root &&
        abs_f32(first->weight-.15f) < .0001f && abs_f32(last->weight-.45f) < .0001f,
        "empty source collapse flattens survivors and retains their allocation");
    }
    RD_PanelAddress last_focus = rd_nil_panel_address;
    for(UIShell_CmdNode *n = before_collapse ? before_collapse->next : rd_state->cmds[0].first; n; n = n->next)
    { if(str8_match(n->cmd.name, str8_lit("focus_panel"), 0)) { last_focus = n->cmd.regs->panel; } }
    CardCheck(rd_panel_address_match(last_focus, rd_panel_from_view(moving_address)), "closing the focused source leaves focus with the View, in its new panel (#329)");
    rd_fixture_restore(collapse_saved);

    // The empty ordinary areas are confined to this diagnostic's disposable profile.
    uishell_sidebar_card_close(original);

    fprintf(stderr, "Hover card diagnostics: positioned card drops\n");
    // A card released on a local group's insertion point adds a ghost there,
    // as a card, and the group's order puts it at that index; a pinned card
    // dragged there moves its own ghost.
    {
      Temp scratch = scratch_begin(0, 0);
      UIShell_LocalGroup *made = uishell_sidebar_local_new_group(window, str8_lit("Drops"));
      String8 group_id = push_str8_copy(scratch.arena, made->id), group_section = push_str8_copy(scratch.arena, made->section->id);
      String8 ghosts[3] = {0};
      char *ids[] = {"drop-first", "drop-second"};
      for(U64 i = 0; i < 2; i++)
      { ghosts[i] = uishell_hover_card_test_put_pin(scratch.arena, ws, group_id, str8_lit("workspace"), str8_cstring(ids[i]), str8_zero()); }
      // Published and placed, the group's items form one run.
      UIShell_ControlledSplit drop_split = uishell_root_controlled_split_from_window(scratch.arena, window);
      uishell_sidebar_publish(&fixture, &drop_split);
      uishell_sidebar_refresh(&fixture);
      String8 loop = str8_zero();
      for(U64 i = 0; fixture.snapshot && i < andamento_snapshot_node_count(fixture.snapshot); i++)
      {
        AndamentoNode n = {0}; andamento_snapshot_node(fixture.snapshot, i, &n);
        if(str8_match(uishell_sidebar_string(n.entity.id), ghosts[0], 0)) { loop = push_str8_copy(scratch.arena, uishell_sidebar_loop_key(fixture.snapshot, i)); }
      }
      CardCheck(loop.size != 0, "a local group's ghosts are placed as one run");
      if(!fixture.group_claim_arena) { fixture.group_claim_arena = arena_alloc(); }
      fixture.group_claim_id = group_id; fixture.group_claim_loop = loop;
      fixture.group_claim_rect = r2f32p(-1e6, -1e6, 1e6, 1e6);
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
      fixture.drag_card = original; original->moving = original->drag_released = 1;
      fixture.group_claim_index = 1; fixture.group_claim_build = test->build_index;
      uishell_sidebar_drag_finish(ws);
      // The new pin: the group's one that isn't either of the two.
      UIShell_LocalGroup *group = uishell_sidebar_local_group(window, group_id);
      UIShell_LocalPin *added = 0;
      for(U64 p = 0; group && p < group->pin_count; p++)
      { if(!str8_match(group->pins[p].ghost, ghosts[0], 0) && !str8_match(group->pins[p].ghost, ghosts[1], 0)) { added = &group->pins[p]; } }
      ghosts[2] = added ? push_str8_copy(scratch.arena, added->ghost) : str8_zero();
      B32 ordered = fixture.order_pending && fixture.order_count == 3 &&
        str8_match(uishell_sidebar_string(fixture.order[0].id), ghosts[0], 0) &&
        str8_match(uishell_sidebar_string(fixture.order[1].id), ghosts[2], 0) &&
        str8_match(uishell_sidebar_string(fixture.order[2].id), ghosts[1], 0);
      CardCheck(added && str8_match(uishell_sidebar_string(added->target.id), uishell_sidebar_string(entity.entity.id), 0) &&
        uishell_sidebar_pin_expanded(added) && !original->open && !fixture.drag_card && ordered,
        "a card released on a group's insertion point becomes a ghost there, as a card, at that index");
      fixture.order_pending = 0;
      // Down to a middle slot, from the top of [first, second, added]: the
      // line between second and added is index 2 (counting every item, as the
      // line does), so first lands between them.
      uishell_sidebar_publish(&fixture, &drop_split);
      uishell_sidebar_refresh(&fixture);
      UIShell_HoverCard *first = uishell_sidebar_saved_card(ws, uishell_sidebar_pin_by_ghost(window, ghosts[0]));
      fixture.drag_card = first; first->moving = first->drag_released = 1;
      fixture.group_claim_index = 2; fixture.group_claim_build = test->build_index;
      uishell_sidebar_drag_finish(ws);
      UIShell_LocalPin *first_pin = uishell_sidebar_pin_by_ghost(window, ghosts[0]);
      CardCheck(fixture.order_pending && fixture.order_count == 3 && first_pin && str8_match(first_pin->group->id, group_id, 0) && first->open &&
        str8_match(uishell_sidebar_string(fixture.order[1].id), ghosts[0], 0),
        "a pinned card dragged down to a middle insertion point moves its own ghost there, not one past it");
      fixture.order_pending = 0;
      // A stale claim (two builds old), or one the pointer has left, doesn't
      // capture a release.
      fixture.group_claim_build = test->build_index-2;
      CardCheck(!uishell_sidebar_group_claimed(&fixture), "a stale insertion point doesn't capture a release");
      fixture.group_claim_build = test->build_index; fixture.group_claim_rect = r2f32p(-1e6, -1e6, -1e6+1, -1e6+1);
      CardCheck(!uishell_sidebar_group_claimed(&fixture), "an insertion point the pointer has left doesn't capture a release");
      fixture.group_claim_build = 0;
      for(UIShell_HoverCard *c = fixture.detached; c; c = c->next)
      {
        UIShell_LocalPin *pin = uishell_sidebar_pin_by_ghost(window, uishell_sidebar_card_pin_id(c));
        if(c->pin_size && pin && str8_match(pin->group->id, group_id, 0)) { uishell_sidebar_card_close(c); }
      }
      uishell_sidebar_detached_finish(ws);
      CFG_Node *drop_view = uishell_sidebar_local_view(window, group_section);
      if(drop_view != &cfg_nil_node) { rd_fixture_remove(drop_view); }
      uishell_sidebar_local_forget_section(window, group_section);
      scratch_end(scratch);
    }
    arena_release(entity_arena);
  }

  fprintf(stderr, "Hover card diagnostics: cleanup\n");
  ws->sidebar = saved_sidebar; ws->ui = saved_window_ui;
  rd_state->menu_animation_rate = saved_rate;
  uishell_sidebar_release(&fixture); ui_select_state(saved_ui); ui_state_release(test);
  fprintf(stderr, "Hover card diagnostics: %u failures\n", failures);
#undef CardCheck
  return failures == 0;
}

// Full card/panel diagnostics need the live frame's View and evaluator registries.
internal B32
uishell_tooltip_and_card_diagnostics(RD_WindowState *ws)
{
  B32 ok = uishell_tooltip_diagnostics(ws);
  return uishell_hover_card_diagnostics(ws) && ok;
}
