internal size_t
action_node_copy(UIShell_SidebarState *state, U64 index)
{
  AndamentoDetail detail = {0};
  return andamento_snapshot_detail(state->snapshot, index, &detail) ? detail.copy_url : ANDAMENTO_NONE;
}

// Commit through the same creation-drag routing and finish phases as a center
// drop on the panel showing local group `area`'s section.
internal void
uishell_hover_card_test_center_drop(RD_WindowState *ws, UIShell_HoverCard *card, CFG_Node *area)
{
  CFG_Node *view = uishell_sidebar_local_view(cfg_node_from_id(ws->cfg_id), area);
  card->open = card->moving = card->drag_released = card->engaged = 1;
  ws->sidebar->drag_card = card;
  UIShell_RegsScope(.window = ws->cfg_id, .view = 0, .panel = 0)
  { rd_drag_begin(UIShell_ContextRegSlot_View); }
  rd_state->drag_drop_creation_name = str8_lit("sidebar_section");
  rd_state->drag_drop_commit = uishell_sidebar_drag_panel_drop;
  rd_state->drag_drop_state = RD_DragDropState_Dropping;
  if(rd_drag_drop()) { rd_panel_drag_drop(view->parent->id, Dir2_Invalid, view->id); }
  UI_EventList events = {0}; UI_AnimationInfo animation = {0};
  UI_IconInfo icons = ws->ui->icon_info;
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = v2f32(500, 100);
  uishell_sidebar_drag_finish(ws);
  uishell_sidebar_detached_finish(ws);
  ui_end_build();
}

// Click an actual related/Back widget through press and release frames.
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
      UI_Rect(r2f32p(0, 0, 400, 700)) UI_Focus(UI_FocusKind_On)
      { body = ui_build_box_from_key(UI_BoxFlag_DefaultFocusNavY, ui_key_make(9090)); }
      UI_Parent(body) UI_FocusHot(UI_FocusKind_Root) UI_FocusActive(UI_FocusKind_Root)
      { uishell_sidebar_card_content(state, ws, card, 0, node, index, 400, 1); }
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

// Deterministic time traces use the production transitions; UI and raw-WM
// checks exercise the actual hit exclusion and Escape routing seams.
internal B32
uishell_hover_card_diagnostics(RD_WindowState *ws)
{
  U32 failures = 0;
#define CardCheck(expr, message) do { if(!(expr)) { fprintf(stderr, "FAIL hover card: %s\n", message); failures++; } } while(0)
  fprintf(stderr, "Hover card diagnostics: start\n");
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  // Initialized and restored: rendering must use this fixture's own core.
  UIShell_SidebarState *saved_sidebar = ws->sidebar, fixture = {.initialized = 1, .restored = 1};
  ws->sidebar = &fixture;
  UI_State *saved_window_ui = ws->ui;
  ws->ui = test;
  ui_select_state(test);
  UI_Box source = {.key = ui_key_make(700), .rect = r2f32p(10, 100, 100, 130)};
  UI_Signal hover = {.box = &source, .f = UI_SignalFlag_Hovering};
  AndamentoNode a = {.entity_kind = uishell_sidebar_text(str8_lit("issue")), .entity_id = uishell_sidebar_text(str8_lit("a"))};
  AndamentoNode b = {.entity_kind = uishell_sidebar_text(str8_lit("issue")), .entity_id = uishell_sidebar_text(str8_lit("b"))};
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
  WM_Event click = {.kind = WM_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = {200, 150}};
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
                      .key = WM_Key_LeftMouseButton, .pos = {100, 100}};
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
    { uishell_sidebar_card_icon_button(rd_icon_kind_text_table[RD_IconKind_Pin], str8_lit("font_fixture"), str8_lit("Pin font fixture")); }
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
                      .key = WM_Key_LeftMouseButton, .pos = {150, 100}};
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
  AndamentoEntity demand_entity = {uishell_sidebar_text(str8_lit("issue")), uishell_sidebar_text(str8_lit("demand-only"))};
  AndamentoFact demand_fact = {.key = uishell_sidebar_text(str8_lit("display.label")), .kind = ANDAMENTO_FACT_TEXT,
                              .text = uishell_sidebar_text(str8_lit("Before"))};
  CardCheck(andamento_apply_entity(demand.core, 0, demand_entity.kind, demand_entity.id, uishell_sidebar_text(str8_lit("demand")), &demand_fact, 1, 0),
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
  CardCheck(andamento_apply_entity(demand.core, 1, demand_entity.kind, demand_entity.id, uishell_sidebar_text(str8_lit("demand")), &demand_fact, 1, 0),
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
  CardCheck(andamento_apply_patch_json(demand.core, 2, uishell_sidebar_text(demand_remove), 0), "held target is removed");
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
    AndamentoEntity absent = {uishell_sidebar_text(str8_lit("issue")), uishell_sidebar_text(str8_lit("never-existing"))};
    for(U64 revision = 0; revision < 4; revision++)
    {
      demand_fact.text = uishell_sidebar_text(revision % 2 ? str8_lit("Odd") : str8_lit("Even"));
      CardCheck(andamento_apply_entity(missing.core, revision, demand_entity.kind, demand_entity.id,
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
      B32 accepted = andamento_apply_patch_json(missing.core, 4, uishell_sidebar_text(str8_lit("{")), &error);
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
      CardCheck(andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(str8(patches.str+start, end-start)), 0), "render fixture patch applies");
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
      if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("pr-281"), 0))
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
      live.state = ANDAMENTO_LIVE; live.workspace_id = 123456;
      uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, 4000000);
      for(U64 engaged = 0; engaged < 2; engaged++)
      {
        UI_EventList events = {0};
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        MemoryZeroArray(test->hover_card_keys);
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        UI_PrefWidth(ui_px(400, 1)) UI_PrefHeight(ui_em(1.6f, 1))
        {
          UI_Key preview_key = ui_key_from_stringf(ui_active_seed_key(), "###hover_preview_%I64u_%I64u", (U64)0, live.workspace_id);
          uishell_sidebar_card_content(&fixture, ws, card, 0, live, index, 400, engaged);
          CardCheck(!ui_box_is_nil(ui_box_from_key(preview_key)), "both peek and engaged retain the preview box");
        }
        ui_end_build();
        CardCheck(rd_workspace_preview_demand_width(ws, live.workspace_id) > 0, "both states request live workspace preview rendering");
        B32 title = 0, actions = 0;
        for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
        {
          String8 text = ui_box_display_string(box);
          title |= str8_match(text, str8_lit("Render subjects"), 0);
          actions |= str8_match(text, str8_lit("Copy URL"), 0);
        }
        CardCheck(title, "structured title is rendered without a prefix");
        CardCheck(actions == engaged, "actions appear only when engaged");
      }
      AndamentoNode parent = {0}; andamento_snapshot_node(fixture.snapshot, live.parent, &parent);
      String8 parent_label = uishell_sidebar_string(parent.label);
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, parent_label, 0) && card->depth == 2,
                "clicking a related widget navigates within the card");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("← Back"), 0) && card->depth == 1 &&
                str8_match(uishell_sidebar_string(card->path[0].id), uishell_sidebar_string(live.entity_id), 0), "Back widget restores the source target");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, parent_label, WM_Modifier_Ctrl) && card->depth == 1 &&
                fixture.cards[1].open && fixture.cards[1].focused &&
                str8_match(uishell_sidebar_string(fixture.cards[1].path[0].id), uishell_sidebar_string(parent.entity_id), 0),
                "modifier-click opens a separate focused card without changing the original path");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, rd_icon_kind_text_table[RD_IconKind_X], 0) && !card->open &&
                fixture.cards[1].open && fixture.cards[1].focused, "closing the original leaves the separate card open and focused");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, &fixture.cards[1], rd_icon_kind_text_table[RD_IconKind_X], 0) &&
                !fixture.cards[1].open, "the separate card closes through its own action");
      // Hidden targets navigate by catalog identity, including cycles and
      // modifier-open. No placement-edge fallback may invent a relation.
      uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, now_time_us());
      B32 hidden_in_tree = 0;
      for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
      {
        AndamentoNode row = {0}; andamento_snapshot_node(fixture.snapshot, i, &row);
        hidden_in_tree |= str8_match(uishell_sidebar_string(row.entity_id), str8_lit("hidden-detail"), 0);
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
      CardCheck(!card->enriched && uishell_hover_card_test_click(ws, &fixture, card, rd_icon_kind_text_table[RD_IconKind_Info], 0) && card->enriched,
                "engaged Details button reveals labels and observation ages");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, rd_icon_kind_text_table[RD_IconKind_Info], 0) && !card->enriched,
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
            if(str8_match(uishell_sidebar_string(next.entity_id), str8_lit("pr-1000"), 0)) { break; }
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
          saw_close |= saw_back && saw_related && str8_match(text, rd_icon_kind_text_table[RD_IconKind_X], 0);
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
      CardCheck(andamento_apply_entity(fixture.core, 0, uishell_sidebar_text(str8_lit("change_request")),
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
      CardCheck(andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(remove_url), 0), "refresh trace removes URL metadata");
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
      AndamentoEntity entity = {uishell_sidebar_text(str8_cstring(cards[i].kind)), uishell_sidebar_text(str8_cstring(cards[i].id))};
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
    CardCheck(str8_match(uishell_sidebar_card_age(60000, 0), str8_lit("1m ago"), 0) &&
              str8_match(uishell_sidebar_card_age(3600000, 0), str8_lit("1h ago"), 0) &&
              str8_match(uishell_sidebar_card_age(86400000, 0), str8_lit("1d ago"), 0) &&
              str8_match(uishell_sidebar_card_age(0, 100), str8_lit("0s ago"), 0),
              "relative ages use the controller clock and saturate future observations");

    fprintf(stderr, "Hover card diagnostics: retained expiry\n");
    // Exercise retained expiry in an isolated controller with the same native
    // card renderer. The fixture's display filtering cannot affect this path.
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
    AndamentoEntity entity = {uishell_sidebar_text(str8_lit("worktree")), uishell_sidebar_text(str8_lit("stale-worktree"))};
    AndamentoFact facts[] = {
      {.key = uishell_sidebar_text(str8_lit("display.label")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_lit("Worker")), .has_ttl = 1, .ttl_ms = 10},
      {.key = uishell_sidebar_text(str8_lit("git.branch")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_lit("retained-branch")), .has_ttl = 1, .ttl_ms = 10},
      {.key = uishell_sidebar_text(str8_lit("git.upstream")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_zero()), .has_ttl = 1, .ttl_ms = 10},
      {.key = uishell_sidebar_text(str8_lit("action.primary.target")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_lit("worktree:stale-worktree")), .has_ttl = 1, .ttl_ms = 10},
      {.key = uishell_sidebar_text(str8_lit("action.primary.recipe")), .kind = ANDAMENTO_FACT_TEXT,
       .text = uishell_sidebar_text(str8_lit("exec sh")), .has_ttl = 1, .ttl_ms = 10},
    };
    CardCheck(andamento_apply_entity(freshness.core, 1000, entity.kind, entity.id, uishell_sidebar_text(str8_lit("fixture")), facts, ArrayCount(facts), 0),
              "retained observation fixture applies");
    uishell_sidebar_refresh(&freshness);
    AndamentoNode node = {0}; U64 index = uishell_sidebar_card_find(&freshness, entity, &node);
    CardCheck(andamento_dispatch(freshness.core, freshness.snapshot, node.activate, 0), "freshness fixture activates its control");
    AndamentoEffects *effects = andamento_effects_take(freshness.core, 0);
    AndamentoEffect effect = {0};
    CardCheck(andamento_effects_get(effects, 0, &effect) && effect.kind == ANDAMENTO_EFFECT_MATERIALIZE,
              "freshness fixture gets a materialize effect");
    CardCheck(andamento_complete(freshness.core, effect.request_id, ANDAMENTO_COMPLETE_MATERIALIZE, 123456, uishell_sidebar_text(str8_zero()), 0),
              "freshness fixture binds preview identity");
    andamento_effects_release(effects);
    AndamentoWorkspace workspace = {.id = 123456, .name = uishell_sidebar_text(str8_lit("Worker")), .selected = 1};
    CardCheck(andamento_observe(freshness.core, &workspace, 1, 0, 0, 0) && andamento_tick(freshness.core, 61000, 0), "retained observation ages");
    uishell_sidebar_refresh(&freshness);
    index = uishell_sidebar_card_find(&freshness, entity, &node);
    CardCheck(node.state == ANDAMENTO_LIVE && node.workspace_id == 123456, "retained catalog preview identity survives expiry");
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
    { andamento_snapshot_node(fixture.snapshot, i, &entity); if(!entity.is_section && entity.entity_id.len) { break; } }
    // Rendering sections publishes local sections, which replaces the
    // snapshot; keep the entity's text in storage the test owns.
    Arena *entity_arena = arena_alloc();
    entity.entity_kind = uishell_sidebar_text(push_str8_copy(entity_arena, uishell_sidebar_string(entity.entity_kind)));
    entity.entity_id = uishell_sidebar_text(push_str8_copy(entity_arena, uishell_sidebar_string(entity.entity_id)));
    entity.label = uishell_sidebar_text(push_str8_copy(entity_arena, uishell_sidebar_string(entity.label)));
    entity.key = uishell_sidebar_text(push_str8_copy(entity_arena, uishell_sidebar_string(entity.key)));
    UIShell_HoverCard *original = &fixture.cards[0];
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    original->rect = r2f32p(400, 100, 800, 400); original->engaged = original->focused = 1;
    fixture.rect = r2f32p(0, 0, 320, 700);
    CardCheck(uishell_sidebar_card_target_valid(ws, UIShell_CardPlacement_Float, 0) &&
              uishell_sidebar_card_target_valid(ws, UIShell_CardPlacement_Inline, 0) &&
              uishell_sidebar_card_target_valid(ws, UIShell_CardPlacement_Pinned, 0),
              "all detached targets use the shared checker with zero minimum width");
    CardCheck(uishell_hover_card_test_click(ws, &fixture, original, rd_icon_kind_text_table[RD_IconKind_Window], 0) && original->move_requested,
              "actual Float control requests detachment");
    uishell_sidebar_detached_finish(ws);
    UIShell_HoverCard *floating = fixture.detached;
    CardCheck(!original->open && floating && floating->open && floating->placement == UIShell_CardPlacement_Float,
              "float detaches into an independent controller");
    floating->focused = 0;
    uishell_sidebar_card_tick(floating, v2f32(-100, -100), 10000000);
    uishell_sidebar_card_tick(floating, v2f32(-100, -100), 11000000);
    CardCheck(floating->open, "unfocused float survives mouse-out timeout");
    // Raw Escape is consumed by the card before the generic UI cancel slot.
    floating->moving = floating->focused = 1; fixture.drag_card = floating;
    UIShell_RegsScope(.window = ws->cfg_id) { rd_drag_begin(UIShell_ContextRegSlot_View); }
    rd_state->drag_drop_creation_name = str8_lit("sidebar_section"); rd_state->drag_drop_commit = uishell_sidebar_drag_panel_drop;
    CFG_ID placement_before = floating->saved;
    WM_Event cancel_drag = {.kind = WM_EventKind_Press, .key = WM_Key_Esc};
    CardCheck(uishell_sidebar_card_wm_event(ws, &cancel_drag) && floating->open && !floating->moving &&
      !fixture.drag_card && !rd_drag_is_active() && !rd_state->drag_drop_commit && !rd_state->drag_drop_creation_name.size &&
      floating->placement == UIShell_CardPlacement_Float && floating->saved == placement_before,
      "Escape cancels the real card drag without pinning or closing the float");
    cancel_drag.kind = WM_EventKind_Release;
    CardCheck(uishell_sidebar_card_wm_event(ws, &cancel_drag), "drag cancel consumes its Escape release");
    // A normal tab drag after Escape must queue a move, never call the
    // cancelled card's creation callback.
    UIShell_CmdNode *before_normal_drag = rd_state->cmds[0].last;
    UIShell_RegsScope(.window = ws->cfg_id, .panel = 123, .view = 456)
    { rd_drag_begin(UIShell_ContextRegSlot_View); }
    rd_panel_drag_drop(789, Dir2_Invalid, 0);
    UIShell_CmdNode *normal_move = before_normal_drag ? before_normal_drag->next : rd_state->cmds[0].first;
    CardCheck(normal_move && str8_match(normal_move->cmd.name, str8_lit("move_view"), 0) &&
      normal_move->cmd.regs->panel == 123 && normal_move->cmd.regs->view == 456 &&
      normal_move->cmd.regs->dst_panel == 789 && !rd_state->drag_drop_commit && !rd_state->drag_drop_creation_name.size,
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
    CFG_Node *saved = uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity), 0);
    CardCheck(saved != &cfg_nil_node && !floating->open, "pin persists entity identity in the saved panel layout");
    CFG_ID saved_id = saved->id;
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    // A transient card for an already pinned entity, dropped on that area's
    // center target, adds a second ghost and leaves the first reachable.
    CFG_Node *center_area = saved->parent;
    CFG_Node *saved_prev = saved->prev, *saved_next = saved->next;
    CFG_Node *area_first = center_area->first, *area_last = center_area->last;
    uishell_hover_card_test_center_drop(ws, original, center_area);
    U64 center_count = 0, entity_ghosts = 0;
    B32 center_reachable = 0;
    CFG_Node *dropped_ghost = &cfg_nil_node;
    for(CFG_Node *n = center_area->first; n != &cfg_nil_node && center_count < 32; n = n->next, center_count++)
    {
      center_reachable |= n == saved;
      if(str8_match(n->string, str8_lit("card"), 0) &&
         str8_match(cfg_node_child_from_string(n, str8_lit("entity"))->first->string, uishell_sidebar_string(entity.entity_id), 0))
      { entity_ghosts++; if(n != saved) { dropped_ghost = n; } }
    }
    CardCheck(center_reachable && saved->id == saved_id && saved->parent == center_area &&
      saved->prev != saved && saved->next != saved && !original->open,
      "center drop of the same entity keeps the first ghost reachable");
    CardCheck(entity_ghosts == 2 && dropped_ghost != &cfg_nil_node && uishell_sidebar_pin_ghost(dropped_ghost).size &&
      !str8_match(uishell_sidebar_pin_ghost(dropped_ghost), uishell_sidebar_pin_ghost(saved), 0),
      "a drop is explicit placement: it adds a second ghost with its own id");
    // Keep later diagnostics runnable even if this regression corrupts links.
    if(!center_reachable || saved->prev == saved || saved->next == saved)
    {
      center_area->first = area_first; center_area->last = area_last;
      saved->prev = saved_prev; saved->next = saved_next; saved->parent = center_area;
      if(saved_prev != &cfg_nil_node) { saved_prev->next = saved; }
      if(saved_next != &cfg_nil_node) { saved_next->prev = saved; }
    }
    CardCheck(uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity), 0) == saved,
      "same-entity center drop keeps the first ghost found by entity");
    if(dropped_ghost != &cfg_nil_node)
    { uishell_sidebar_card_close(uishell_sidebar_saved_card(ws, dropped_ghost)); uishell_sidebar_detached_finish(ws); }
    UIShell_HoverCard *center_pin = uishell_sidebar_saved_card(ws, saved);
    uishell_hover_card_test_center_drop(ws, center_pin, center_area);
    CardCheck(center_pin->open && !center_pin->moving && center_pin->saved == saved_id &&
      uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity), 0) == saved,
      "dropping the pinned card into its own area preserves its controller and saved identity");
    AndamentoNode other_entity = entity;
    other_entity.entity_id = uishell_sidebar_text(str8_lit("center-drop-second"));
    uishell_sidebar_card_set(original, other_entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    uishell_hover_card_test_center_drop(ws, original, center_area);
    CFG_Node *second_pin = uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(other_entity), 0);
    CardCheck(second_pin != &cfg_nil_node && second_pin->parent == center_area &&
      uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity), 0) == saved && center_pin->open,
      "center drop of a different entity appends a second pin without removing the first");
    uishell_sidebar_card_close(uishell_sidebar_saved_card(ws, second_pin));
    uishell_sidebar_detached_finish(ws);
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    // "Pin another" adds a ghost even though one exists; both render as cards.
    original->pin_another = 1;
    CFG_Node *another = uishell_sidebar_card_pin(ws, original, 0);
    CardCheck(another != &cfg_nil_node && another->id != saved_id && !original->pin_another &&
      !str8_match(uishell_sidebar_pin_ghost(another), uishell_sidebar_pin_ghost(saved), 0) &&
      uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity), 0) == saved,
      "pin another adds a second ghost of the entity");
    UIShell_HoverCard *another_card = uishell_sidebar_saved_card(ws, another);
    UIShell_HoverCard *first_card = uishell_sidebar_saved_card(ws, saved);
    CardCheck(another_card != first_card && another_card->saved == another->id && first_card->saved == saved_id,
      "each ghost has its own card");
    uishell_sidebar_card_close(another_card); uishell_sidebar_detached_finish(ws);
    CardCheck(cfg_node_from_id(saved_id) == saved, "closing one ghost leaves the other");
    CFG_Node *again = uishell_sidebar_card_pin(ws, original, 0);
    CardCheck(again->id == saved_id && fixture.pin_reveal == saved_id, "pinning twice reveals the same card");
    Temp saved_scratch = scratch_begin(0, 0);
    CFG_State *loaded_cfg = cfg_state_alloc();
    String8 serialized = cfg_string_from_tree(saved_scratch.arena, rd_state->cfg_schema_table, str8_zero(), window);
    CFG_NodePtrList loaded = cfg_node_ptr_list_from_string(saved_scratch.arena, loaded_cfg, rd_state->cfg_schema_table, str8_zero(), serialized);
    CFG_Node *restored = loaded.count ? uishell_sidebar_pin_find(loaded.first->v, uishell_sidebar_card_entity(entity), 0) : &cfg_nil_node;
    CardCheck(restored != &cfg_nil_node && str8_match(cfg_node_child_from_string(restored, str8_lit("label"))->first->string,
              uishell_sidebar_string(entity.label), 0), "layout serialization and restart preserve pinned identity and label");
    cfg_state_release(loaded_cfg); scratch_end(saved_scratch);
    fprintf(stderr, "Hover card diagnostics: pinned rendering\n");
    UIShell_HoverCard *pinned = uishell_sidebar_saved_card(ws, saved);
    CFG_Node *layout_second = cfg_node_new(rd_state->cfg, saved->parent, str8_lit("card"));
    uishell_sidebar_pin_new_ghost(layout_second);
    uishell_sidebar_pin_set_field(layout_second, str8_lit("kind"), str8_lit("workspace"));
    uishell_sidebar_pin_set_field(layout_second, str8_lit("entity"), str8_lit("layout-second"));
    uishell_sidebar_pin_set_field(layout_second, str8_lit("label"), str8_lit("Second layout card"));
    UIShell_HoverCard *second_layout_card = uishell_sidebar_saved_card(ws, layout_second);
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
      CFG_Node *pin_view = uishell_sidebar_local_view(window, saved->parent);
      UIShell_RegsScope(.window = window->id, .view = pin_view->id, .panel = pin_view->parent->id)
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      {
        // A real View receives window coordinates under a parent already at
        // that position. Its root must use local coordinates exactly once.
        UI_Box *view_parent;
        UI_Rect(view_rect)
        { view_parent = ui_build_box_from_key(UI_BoxFlag_Clip, ui_key_make(119166)); }
        UI_Parent(view_parent)
        { RD_VIEW_UI_FUNCTION_NAME(sidebar_section)((E_Eval){0}, view_rect); }
      }
      ui_end_build();
      CardCheck(ui_nil_box.view_off_target.y == nil_scroll_before,
                "first-frame pin reveal never writes the shared nil box");
      if(frame == 0)
      { CardCheck(fixture.pin_reveal == saved_id && pinned->focused, "new pin keeps its reveal pending until measured"); }
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
      CFG_Node *label = cfg_node_child_from_string(saved, str8_lit("label"));
      CardCheck(str8_match(label->first->string, pinned->retained_label, 0),
                "finish pass persists the live pinned label");
      CFG_ID label_value = label->first->id;
      uishell_sidebar_detached_finish(ws);
      CardCheck(label->first->id == label_value, "unchanged pinned label does not rewrite config on later finish passes");
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
    { uishell_sidebar_detached_content(&fixture, ws, pinned, saved->id, (AndamentoNode){0}, ANDAMENTO_NONE, 280, 1); }
    ui_end_build();
    B32 absent = 0;
    for(UI_Box *b = test->root; !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, test->root).next)
    { absent |= str8_match(ui_box_display_string(b), str8_lit("No longer present"), 0); }
    CardCheck(absent && pinned->open && cfg_node_from_id(saved_id) != &cfg_nil_node, "missing subject retains pinned card with an explicit marker");
    fixture.snapshot = snapshot;
    CFG_Node *area = saved->parent;
    // Dragged through the shared finish to a docking site below its panel.
    // The site's split command and sizing are undone below for later checks.
    B32 sized_before_move = cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")) != &cfg_nil_node;
    UIShell_CmdNode *cmds_before_move = rd_state->cmds[0].last; U64 cmd_count_before_move = rd_state->cmds[0].count;
    fixture.drag_card = pinned; pinned->moving = pinned->drag_released = 1;
    CFG_Node *area_view = uishell_sidebar_local_view(window, area);
    fixture.drop_panel = area_view->parent->id; fixture.drop_direction = Dir2_Down;
    uishell_sidebar_drag_finish(ws);
    CFG_Node *moved = cfg_node_from_id(saved_id);
    CardCheck(moved != &cfg_nil_node && moved->parent != area && str8_match(moved->parent->string, str8_lit("group"), 0) &&
              pinned->open && !fixture.drag_card, "dragging a pinned card to a docking site moves its own ghost to the new area");
    B32 has_card = 0;
    for(CFG_Node *n = area->first; n != &cfg_nil_node; n = n->next)
    { has_card |= str8_match(n->string, str8_lit("card"), 0); }
    CardCheck(!has_card && cfg_node_from_id(area->id) == area && rd_dock_can_close(area_view),
              "moving the last pin preserves an empty group whose section can be closed");
    if(moved != &cfg_nil_node && moved->parent != area)
    {
      CFG_Node *new_area = moved->parent;
      CFG_Node *new_view = uishell_sidebar_local_view(window, new_area);
      cfg_node_insert_child(rd_state->cfg, area, area->last, moved);
      if(new_view != &cfg_nil_node) { cfg_node_release(rd_state->cfg, new_view); }
      cfg_node_release(rd_state->cfg, new_area->parent);
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
    CardCheck(uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity), 0) == &cfg_nil_node,
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
        UI_Box *card_root = ui_box_from_key(test->hover_card_keys[0]);
        for(UI_Box *box = card_root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, card_root).next)
        { if(str8_match(ui_box_display_string(box), str8_lit("⋮⋮"), 0)) { drag_start = center_2f32(box->rect); break; } }
        CardCheck(drag_start.x > 0, "engaged production overlay exposes the drag control");
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
      original->pin_another = 1;
      CFG_Node *ghost = uishell_sidebar_card_pin(ws, original, 0);
      CFG_ID ghost_id = ghost->id;
      UIShell_HoverCard *ghost_card = uishell_sidebar_saved_card(ws, ghost);
      CardCheck(ghost != &cfg_nil_node && uishell_sidebar_pin_expanded(ghost), "pinning a card keeps it as a card");
      uishell_sidebar_ghost_set_expanded(ghost, 0);
      Temp ghost_scratch = scratch_begin(0, 0);
      String8 ghost_guid = push_str8_copy(ghost_scratch.arena, uishell_sidebar_pin_ghost(ghost));
      String8 card_suffix = push_str8f(ghost_scratch.arena, "###pinned_card_%I64u", ghost_id);
      CFG_Node *ghost_view = uishell_sidebar_local_view(window, ghost->parent);
      Rng2F32 view_rect = r2f32p(0, 0, 320, 600);
      // Frames: 0 lays out; 1-2 click the disclosure; 3 checks the card and
      // collapses it again; 4 settles; 5-6 click the row; 7 hovers; 8-9 click ×.
      Vec2F32 expand_at = {0}, row_at = {0}, remove_at = {0};
      B32 row_seen = 0, card_before = 0, card_after = 0, marker = 0, collapse_control = 0;
      fixture.card_has_action = 0;
      B32 queued = 0, hover_offered = 0;
      // A ghost row's click opens its target, as its home row does.
      U64 workspaces_before = 0;
      for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next) { workspaces_before += str8_match(c->string, str8_lit("workspace"), 0); }
      uishell_sidebar_card_close(&fixture.cards[0]); fixture.cards[0].candidate = (AndamentoEntity){0};
      // Two settle frames publish and place the ghost before frame 0.
      for(U32 step = 0; step < 12; step++)
      {
        U32 frame = step < 2 ? 0 : step-2;
        B32 settled = step >= 2;
        Vec2F32 at = frame == 1 || frame == 2 ? expand_at : frame >= 5 && frame <= 7 ? row_at : frame >= 8 ? remove_at : v2f32(-100, -100);
        UI_EventList events = {0};
        UI_Event event = {.kind = (frame == 1 || frame == 5 || frame == 8) ? UI_EventKind_Press : UI_EventKind_Release,
          .key = WM_Key_LeftMouseButton, .pos = at};
        if(settled && (frame == 1 || frame == 2 || frame == 5 || frame == 6 || frame == 8 || frame == 9)) { ui_event_list_push(test->arena, &events, &event); }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = at; test->hover_card_extra = 0; MemoryZeroArray(test->hover_card_keys);
        UIShell_RegsScope(.window = window->id, .view = ghost_view->id, .panel = ghost_view->parent->id)
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        {
          UI_Box *view_parent;
          UI_Rect(view_rect) { view_parent = ui_build_box_from_key(UI_BoxFlag_Clip, ui_key_make(119167)); }
          UI_Parent(view_parent) { RD_VIEW_UI_FUNCTION_NAME(sidebar_section)((E_Eval){0}, view_rect); }
        }
        ui_end_build();
        // The ghost's tree row is keyed by its placement.
        String8 node_key = str8_zero();
        for(U64 i = 0; fixture.snapshot && i < andamento_snapshot_node_count(fixture.snapshot); i++)
        {
          AndamentoNode n = {0}; andamento_snapshot_node(fixture.snapshot, i, &n);
          if(str8_match(uishell_sidebar_string(n.entity_id), ghost_guid, 0)) { node_key = push_str8_copy(ghost_scratch.arena, uishell_sidebar_string(n.key)); }
        }
        String8 row_suffix = push_str8f(ghost_scratch.arena, "###sidebar_row_%S", node_key);
        String8 expand_suffix = push_str8f(ghost_scratch.arena, "###toggle_%S", node_key);
        String8 remove_suffix = push_str8f(ghost_scratch.arena, "###close_%S", node_key);
        if(settled && frame == 6)
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
          if(frame == 3 && ui_key_match(b->key, ui_key_from_string(seed, str8_lit("###card_collapse")))) { collapse_control = 1; }
          if(ui_key_match(b->key, ui_key_from_string(seed, card_suffix)))
          { if(settled && frame == 0) { card_before = 1; } if(frame == 3) { card_after = 1; } }
          if(settled && frame == 0 && str8_match(ui_box_display_string(b), str8_lit("↗"), 0)) { marker = 1; }
        }
        if(settled && frame == 3 && cfg_node_from_id(ghost_id) != &cfg_nil_node)
        { uishell_sidebar_ghost_set_expanded(ghost, 0); }
        if(settled && frame == 7) { hover_offered = uishell_sidebar_card_entity_match(fixture.cards[0].candidate, uishell_sidebar_card_entity(entity)) ||
          (fixture.cards[0].open && uishell_sidebar_card_entity_match(fixture.cards[0].path[0], uishell_sidebar_card_entity(entity))); }
      }
      uishell_sidebar_detached_finish(ws);
      CardCheck(row_seen && marker && !card_before, "a collapsed ghost shows its row and lives-elsewhere marker, not its card");
      CardCheck(card_after, "the row's disclosure expands the ghost into its card");
      CardCheck(collapse_control, "a pinned card's header can collapse it to its row");
      CardCheck(queued, "clicking a ghost row goes to its source");
      CardCheck(hover_offered, "hovering a ghost row offers its subject's hover card");
      CardCheck(cfg_node_from_id(ghost_id) == &cfg_nil_node && !ghost_card->open, "× on a ghost row removes the ghost");
      CardCheck(uishell_sidebar_card_find(&fixture, uishell_sidebar_card_entity(entity), 0) != ANDAMENTO_NONE, "removing a ghost leaves its source");
      fixture.card_has_action = 0;
      scratch_end(ghost_scratch);
    }
    fprintf(stderr, "Hover card diagnostics: pin hold menu\n");
    // With the subject already pinned, holding Pin opens its menu (Show
    // existing pin, Pin another), and the release adds no ghost.
    {
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
      CFG_Node *hold_pin = uishell_sidebar_card_pin(ws, original, 0);
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
      original->source_rect = r2f32p(20, 20, 120, 50); original->engaged = original->focused = 1;
      Temp ghosts_scratch = scratch_begin(0, 0);
      CFG_NodePtrList before = {0}; uishell_sidebar_pin_cards(ghosts_scratch.arena, window, &before);
      Vec2F32 pin_at = {0};
      B32 menu_open = 0, tooltip_over_menu = 0;
      for(U64 frame = 0; frame < 5; frame++)
      {
        UI_EventList events = {0};
        UI_Event event = {.kind = frame == 3 ? UI_EventKind_Release : UI_EventKind_Press,
          .key = WM_Key_LeftMouseButton, .pos = pin_at};
        if(frame == 1 || frame == 3) { ui_event_list_push(test->arena, &events, &event); }
        if(frame == 2) { sleep_ms(UIShell_HoldUS/1000+50); }
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        test->mouse = frame == 0 ? v2f32(-100, -100) : pin_at;
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        { uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 1); }
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
        if(frame == 4) { menu_open = ui_any_ctx_menu_is_open(); }
        // From the hold onwards the menu owns the space under Pin.
        for(UI_Box *b = test->tooltip_root; frame >= 2 && !ui_box_is_nil(b); b = ui_box_rec_df_pre(b, test->tooltip_root).next)
        { tooltip_over_menu |= str8_match(ui_box_display_string(b), str8_lit("Show pin · hold to pin another"), 0); }
      }
      CFG_NodePtrList after = {0}; uishell_sidebar_pin_cards(ghosts_scratch.arena, window, &after);
      CardCheck(menu_open, "holding Pin on a pinned subject opens the pin menu");
      CardCheck(!tooltip_over_menu, "Pin's tooltip doesn't cover its open menu");
      CardCheck(after.count == before.count && original->open, "the release after the hold adds no ghost");
      scratch_end(ghosts_scratch);
      ui_ctx_menu_close();
      uishell_sidebar_card_close(uishell_sidebar_saved_card(ws, hold_pin));
      uishell_sidebar_detached_finish(ws);
    }

    fprintf(stderr, "Hover card diagnostics: pinned drag with panel targets\n");
    CFG_Node *drag_pin_entry = uishell_sidebar_card_pin(ws, original, 0);
    UIShell_HoverCard *drag_pin = uishell_sidebar_saved_card(ws, drag_pin_entry);
    original->open = 0;
    // The grip starts a drag in the first View; a later leaf must build drop
    // sites in that same frame, before the next panel-area entry.
    CFG_Node *drag_root = cfg_node_new(rd_state->cfg, window, RD_DOCK_SIDEBAR_ROOT);
    // The pin's group shows in its section's View; publish it so it's placed.
    CFG_Node *drag_view = uishell_sidebar_local_view(window, drag_pin_entry->parent);
    cfg_node_child_from_string_or_alloc(rd_state->cfg, drag_view, str8_lit("selected"));
    CFG_Node *drag_panel = drag_view->parent;
    {
      Temp publish_scratch = scratch_begin(0, 0);
      UIShell_ControlledSplit publish_split = uishell_root_controlled_split_from_window(publish_scratch.arena, window);
      uishell_sidebar_publish_local(&fixture, &publish_split);
      uishell_sidebar_refresh(&fixture);
      scratch_end(publish_scratch);
    }
    CFG_Node *drag_parent = drag_panel->parent, *drag_previous = drag_panel->prev;
    String8 drag_share = push_str8_copy(test->arena, drag_panel->string);
    cfg_node_insert_child(rd_state->cfg, drag_root, &cfg_nil_node, drag_panel);
    cfg_node_equip_string(rd_state->cfg, drag_panel, str8_lit("0.5"));
    cfg_node_new(rd_state->cfg, drag_root, str8_lit("0.5"));
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
        CFG_Node *host = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
        UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(panel_scratch.arena, window, host);
        mount.panel_tree = cfg_panel_tree_from_panels_cfg(panel_scratch.arena, drag_root, Axis2_X);
        UIShell_RegsScope(.window = window->id)
        { rd_panel_area_ui(panel_scratch, r2f32p(0, 0, 1000, 700), r2f32p(0, 0, 1000, 700), ws, &mount, 1, 0, 0, 0, 0); }
        scratch_end(panel_scratch);
        fixture.core = drag_core;
      }
      uishell_sidebar_drag_finish(ws);
      ui_end_build();
      if(frame == 0)
      {
        UI_Box *card_root = ui_box_from_key(drag_pin->mask.key);
        for(UI_Box *box = card_root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, card_root).next)
        { if(str8_match(ui_box_display_string(box), str8_lit("⋮⋮"), 0)) { pinned_drag_start = center_2f32(box->rect); break; } }
        uishell_sidebar_detached_bounds(ws);
        pinned_drag_size = dim_2f32(drag_pin->rect);
        CardCheck(pinned_drag_start.x > 0, "pinned card exposes its own grip inside the section");
      }
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
      uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity), 0) == &cfg_nil_node,
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
    // The pins' section title drags its View, as any section title does.
    {
      ui_kill_action(); rd_drag_kill();
      CFG_Node *area_view = cfg_node_child_from_string(drag_panel, str8_lit("sidebar_section"));
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
          CFG_Node *host = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
          UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(panel_scratch.arena, window, host);
          mount.panel_tree = cfg_panel_tree_from_panels_cfg(panel_scratch.arena, drag_root, Axis2_X);
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
        if(frame == 3) { area_drag = rd_drag_is_active() && rd_state->drag_drop_regs->view == area_view->id; }
      }
      CardCheck(area_drag, "dragging the pins' section title starts its View's docking drag");
      rd_drag_kill(); ui_kill_action();
    }
    // The raw release is consumed by card ownership before UI events. A
    // former pin's Float grip must release too, or the next build restarts drag.
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
          UI_Box *card_root = ui_box_from_key(drag_pin->mask.key);
          for(UI_Box *box = card_root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, card_root).next)
          { if(str8_match(ui_box_display_string(box), str8_lit("⋮⋮"), 0)) { float_drag_start = center_2f32(box->rect); break; } }
          CardCheck(float_drag_start.x > 0, "former pin exposes its Float grip");
        }
        if(frame == 2) { CardCheck(drag_pin->moving && fixture.drag_card == drag_pin, "Float grip starts the real card drag"); }
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
    cfg_node_insert_child(rd_state->cfg, drag_parent, drag_previous, drag_panel);
    cfg_node_equip_string(rd_state->cfg, drag_panel, drag_share);
    cfg_node_release(rd_state->cfg, drag_root);

    // A source-bound card closes when its exact placement is removed.
    uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
    UIShell_HoverCard *removed = uishell_sidebar_detached_copy(ws, original);
    removed->placement = UIShell_CardPlacement_Inline;
    removed->source_key = removed->source_row = str8_lit("removed-placement");
    uishell_sidebar_detached_finish(ws);
    CardCheck(!removed->open, "inline card closes when its source placement disappears");
    // Duplicating a section must not duplicate the pinned entity.
    CFG_Node *unique = uishell_sidebar_card_pin(ws, original, 0);
    CFG_Node *copy = cfg_node_deep_copy(rd_state->cfg, unique);
    cfg_node_insert_child(rd_state->cfg, unique->parent, unique->parent->last, copy);
    CFG_ID copy_id = copy->id;
    uishell_sidebar_pin_deduplicate(window);
    CardCheck(cfg_node_from_id(copy_id) == &cfg_nil_node && cfg_node_from_id(unique->id) == unique,
              "layout copies reconcile to one card per ghost");
    // A second ghost of the same entity is deliberate and survives.
    CFG_Node *second_ghost = cfg_node_deep_copy(rd_state->cfg, unique);
    cfg_node_insert_child(rd_state->cfg, unique->parent, unique->parent->last, second_ghost);
    uishell_sidebar_pin_new_ghost(second_ghost);
    CFG_ID second_ghost_id = second_ghost->id;
    uishell_sidebar_pin_deduplicate(window);
    CardCheck(cfg_node_from_id(second_ghost_id) == second_ghost && cfg_node_from_id(unique->id) == unique,
              "distinct ghosts of one entity both survive reconciliation");
    cfg_node_release(rd_state->cfg, second_ghost);
    // Pins saved before ghost ids keep the first per entity, which gets an id.
    CFG_Node *legacy = cfg_node_new(rd_state->cfg, unique->parent, str8_lit("card"));
    uishell_sidebar_pin_set_field(legacy, str8_lit("kind"), str8_lit("workspace"));
    uishell_sidebar_pin_set_field(legacy, str8_lit("entity"), str8_lit("legacy-pin"));
    CFG_Node *legacy_copy = cfg_node_deep_copy(rd_state->cfg, legacy);
    cfg_node_insert_child(rd_state->cfg, unique->parent, unique->parent->last, legacy_copy);
    CFG_ID legacy_copy_id = legacy_copy->id;
    uishell_sidebar_pin_deduplicate(window);
    CardCheck(cfg_node_from_id(legacy_copy_id) == &cfg_nil_node && uishell_sidebar_pin_ghost(legacy).size,
              "legacy pins keep one per entity and gain a ghost id");
    CardCheck(uishell_sidebar_pin_expanded(legacy), "legacy pins keep their card form");
    String8 legacy_ghost = push_str8_copy(test->arena, uishell_sidebar_pin_ghost(legacy));
    uishell_sidebar_pin_deduplicate(window);
    CardCheck(str8_match(uishell_sidebar_pin_ghost(legacy), legacy_ghost, 0), "a ghost id is stable once assigned");
    cfg_node_release(rd_state->cfg, legacy);
    // Unversioned saved layouts tolerate future kinds, missing identity
    // fields and duplicate exact identities through reconciliation and render.
    CFG_Node *tolerance_area = unique->parent;
    CFG_Node *unknown = cfg_node_new(rd_state->cfg, tolerance_area, str8_lit("card"));
    uishell_sidebar_pin_set_field(unknown, str8_lit("kind"), str8_lit("future_kind"));
    uishell_sidebar_pin_set_field(unknown, str8_lit("entity"), str8_lit("orphan"));
    uishell_sidebar_pin_set_field(unknown, str8_lit("label"), str8_lit("Future pin"));
    uishell_sidebar_pin_set_field(unknown, str8_lit("future_field"), str8_lit("ignored"));
    CFG_Node *unknown_copy = cfg_node_deep_copy(rd_state->cfg, unknown);
    cfg_node_insert_child(rd_state->cfg, tolerance_area, tolerance_area->last, unknown_copy);
    CFG_ID unknown_copy_id = unknown_copy->id;
    CFG_Node *incomplete = cfg_node_new(rd_state->cfg, tolerance_area, str8_lit("card"));
    uishell_sidebar_pin_set_field(incomplete, str8_lit("label"), str8_lit("Incomplete pin"));
    uishell_sidebar_pin_deduplicate(window);
    CardCheck(cfg_node_from_id(unknown_copy_id) == &cfg_nil_node && cfg_node_from_id(unknown->id) == unknown &&
              cfg_node_from_id(incomplete->id) == incomplete && cfg_node_from_id(unique->id) == unique,
              "unknown kinds and incomplete entries tolerate reconciliation without losing valid pins");
    Temp tolerance_scratch = scratch_begin(0, 0);
    CFG_State *tolerance_cfg = cfg_state_alloc();
    String8 tolerance_text = cfg_string_from_tree(tolerance_scratch.arena, rd_state->cfg_schema_table, str8_zero(), window);
    CFG_NodePtrList tolerance_loaded = cfg_node_ptr_list_from_string(tolerance_scratch.arena, tolerance_cfg,
      rd_state->cfg_schema_table, str8_zero(), tolerance_text);
    AndamentoEntity future_entity = {uishell_sidebar_text(str8_lit("future_kind")), uishell_sidebar_text(str8_lit("orphan"))};
    CFG_Node *future_restored = tolerance_loaded.count ? uishell_sidebar_pin_find(tolerance_loaded.first->v, future_entity, 0) : &cfg_nil_node;
    CardCheck(future_restored != &cfg_nil_node &&
              str8_match(cfg_node_child_from_string(future_restored, str8_lit("label"))->first->string, str8_lit("Future pin"), 0),
              "unversioned layout round-trip retains unknown pin kinds and fallback labels");
    cfg_state_release(tolerance_cfg); scratch_end(tolerance_scratch);
    // Published and placed, they render in their section's View.
    {
      Temp publish_scratch = scratch_begin(0, 0);
      UIShell_ControlledSplit publish_split = uishell_root_controlled_split_from_window(publish_scratch.arena, window);
      uishell_sidebar_publish_local(&fixture, &publish_split);
      uishell_sidebar_refresh(&fixture);
      scratch_end(publish_scratch);
    }
    CFG_Node *tolerance_view = uishell_sidebar_local_view(window, tolerance_area);
    UI_EventList tolerance_events = {0};
    ui_begin_build(ws->os, &tolerance_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    test->hover_card_extra = 0; MemoryZeroArray(test->hover_card_keys);
    UIShell_RegsScope(.window = window->id, .view = tolerance_view->id, .panel = tolerance_view->parent->id)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    { RD_VIEW_UI_FUNCTION_NAME(sidebar_section)((E_Eval){0}, r2f32p(17, 29, 297, 1629)); }
    ui_end_build();
    U64 missing_markers = 0;
    for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
    { missing_markers += str8_match(ui_box_display_string(box), str8_lit("No longer present"), 0); }
    CardCheck(missing_markers >= 2 && uishell_sidebar_saved_card(ws, unknown)->open && uishell_sidebar_saved_card(ws, incomplete)->open,
              "unknown and malformed saved pins render a missing marker and remain explicitly closable");
    cfg_node_release(rd_state->cfg, unknown); cfg_node_release(rd_state->cfg, incomplete);
    cfg_node_release(rd_state->cfg, unique);
    // A wrong controlled-split owner rejects creation before any layout edit.
    for(U64 root_exists = 0; root_exists < 2; root_exists++)
    {
      CFG_Node *invalid_owner = cfg_node_new(rd_state->cfg, window, str8_lit("invalid_owner"));
      if(root_exists)
      {
        CFG_Node *invalid_root = cfg_node_new(rd_state->cfg, invalid_owner, RD_DOCK_SIDEBAR_ROOT);
        cfg_node_new(rd_state->cfg, invalid_root, str8_lit("sidebar_section"));
      }
      Temp failed_scratch = scratch_begin(0, 0);
      String8 before_failure = cfg_string_from_tree(failed_scratch.arena, rd_state->cfg_schema_table, str8_zero(), invalid_owner);
      CFG_ID valid_window_id = ws->cfg_id;
      ws->cfg_id = invalid_owner->id;
      CFG_Node *failed_pin = uishell_sidebar_card_pin(ws, original, 1);
      ws->cfg_id = valid_window_id;
      String8 after_failure = cfg_string_from_tree(failed_scratch.arena, rd_state->cfg_schema_table, str8_zero(), invalid_owner);
      CardCheck(failed_pin == &cfg_nil_node && str8_match(before_failure, after_failure, 0),
                "rejected creation removes tentative panels/root and preserves the existing tree");
      scratch_end(failed_scratch);
      cfg_node_release(rd_state->cfg, invalid_owner);
    }
    fprintf(stderr, "Hover card diagnostics: merged layout\n");
    // Adding a new area to a merged root leaf preserves its existing tabs.
    CFG_Node *old_host = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
    cfg_node_unhook(rd_state->cfg, window, old_host);
    CFG_Node *merged_host = cfg_node_new(rd_state->cfg, window, RD_DOCK_SIDEBAR_ROOT);
    CFG_Node *existing_view = cfg_node_new(rd_state->cfg, merged_host, str8_lit("sidebar_section"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, existing_view, str8_lit("section")), str8_lit("projects"));
    unique = uishell_sidebar_card_pin(ws, original, 1);
    CardCheck(unique != &cfg_nil_node && existing_view->parent != merged_host &&
              existing_view->parent->parent == merged_host, "new pinned area preserves tabs in a merged sidebar root");
    F32 sum = 0;
    for(CFG_Node *n = merged_host->first; n != &cfg_nil_node; n = n->next) { sum += (F32)f64_from_str8(n->string); }
    CardCheck(abs_f32(sum-1) < .0001f, "new area normalizes saved panel ratios");
    cfg_node_release(rd_state->cfg, merged_host);
    cfg_node_insert_child(rd_state->cfg, window, window->last, old_host);

    fprintf(stderr, "Hover card diagnostics: shared panel drops\n");
    // A center joins its target; directional drops use the ordinary split
    // command and retain the measured ratios, rather than global rebalancing.
    cfg_node_unhook(rd_state->cfg, window, old_host);
    B32 sized_before = cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")) != &cfg_nil_node;
    Dir2 directions[] = {Dir2_Invalid, Dir2_Up, Dir2_Down, Dir2_Left, Dir2_Right};
    CFG_Node *saved_drop_axis = cfg_node_child_from_string(window, str8_lit("control_views_split_x"));
    cfg_node_unhook(rd_state->cfg, window, saved_drop_axis);
    for(U64 nested = 0; nested < 2; nested++)
    for(U64 d = 0; d < ArrayCount(directions); d++)
    {
      cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("control_views_split_x")));
      if(nested && axis2_from_dir2(directions[d]) == Axis2_Y)
      { cfg_node_new(rd_state->cfg, window, str8_lit("control_views_split_x")); }
      CFG_Node *host = cfg_node_new(rd_state->cfg, window, RD_DOCK_SIDEBAR_ROOT);
      CFG_Node *destination = host, *neighbour = &cfg_nil_node;
      if(nested)
      {
        destination = cfg_node_new(rd_state->cfg, host, str8_lit("0.6"));
        neighbour = cfg_node_new(rd_state->cfg, host, str8_lit("0.4"));
        cfg_node_new(rd_state->cfg, neighbour, str8_lit("sidebar_section"));
      }
      U64 sections_before = 0;
      for(CFG_Node *n = uishell_sidebar_local_root(window)->first; n != &cfg_nil_node; n = n->next) { sections_before++; }
      CFG_Node *existing_group = uishell_sidebar_local_new_group(window, str8_lit("Pinned"));
      CFG_Node *existing = uishell_sidebar_local_new_view(destination, existing_group->parent);
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
      original->engaged = original->focused = original->moving = original->drag_released = 1;
      fixture.drag_card = original;
      UIShell_RegsScope(.window = window->id, .view = 0, .panel = 0)
      { rd_drag_begin(UIShell_ContextRegSlot_View); }
      rd_state->drag_drop_creation_name = str8_lit("sidebar_section");
      rd_state->drag_drop_commit = uishell_sidebar_drag_panel_drop;
      CardCheck(rd_panel_drag_target(&cfg_nil_node, destination, 320), "creation drag uses the registered panel validity checker");
      rd_state->drag_drop_state = RD_DragDropState_Dropping;
      UIShell_CmdNode *before_drop = rd_state->cmds[0].last;
      if(rd_drag_drop()) { rd_panel_drag_drop(destination->id, directions[d], existing->id); }
      UI_EventList drop_events = {0};
      ui_begin_build(ws->os, &drop_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      test->mouse = v2f32(500, 100);
      uishell_sidebar_drag_finish(ws);
      ui_end_build();
      // The ghost the drop made: in the existing group (centre), or in the
      // section the site made (an edge); earlier ghosts of the entity stay.
      CFG_Node *entry = &cfg_nil_node;
      {
        CFG_Node *local = uishell_sidebar_local_root(window);
        U64 index = 0;
        for(CFG_Node *n = local->first; n != &cfg_nil_node; n = n->next, index++)
        {
          if(index < sections_before) { continue; }
          for(CFG_Node *g = n->first; g != &cfg_nil_node; g = g->next)
          {
            if(!str8_match(g->string, str8_lit("group"), 0)) { continue; }
            CFG_Node *c = cfg_node_child_from_string(g, str8_lit("card"));
            if(c != &cfg_nil_node && (directions[d] == Dir2_Invalid) == (g == existing_group)) { entry = c; }
          }
        }
      }
      CFG_Node *area = entry->parent;
      for(UIShell_CmdNode *n = before_drop ? before_drop->next : rd_state->cmds[0].first; n; n = n->next)
      {
        if(str8_match(n->cmd.name, str8_lit("split_panel"), 0)) UIShell_RegsScope()
        { MemoryCopyStruct(uishell_regs(), n->cmd.regs); uishell_dispatch_panel_command(n->cmd.name); }
      }
      CFG_Node *settled_host = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
      Temp drop_scratch = scratch_begin(0, 0);
      UIShell_WorkspaceMount drop_mount = uishell_workspace_mount_from_owner_cfg(drop_scratch.arena, window, settled_host);
      CFG_PanelNode *drop_root = drop_mount.panel_tree.root;
      CardCheck(entry != &cfg_nil_node && !original->open &&
        cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized")) != &cfg_nil_node,
        "accepted card drop pins once and opts into saved panel sizing");
      CFG_PanelNode *target_root = nested ? drop_root->first : drop_root;
      if(nested)
      { CardCheck(drop_root->child_count == 2 && drop_root->last->cfg == neighbour &&
          abs_f32(drop_root->last->pct_of_parent-.4f) < .0001f, "nested drop preserves the other panel and outer allocation"); }
      if(directions[d] == Dir2_Invalid)
      { CardCheck(area == existing_group && target_root->child_count == 0, "center drop joins the group of the exact existing section"); }
      else
      {
        Side side = side_from_dir2(directions[d]);
        CFG_PanelNode *placed = side == Side_Min ? target_root->first : target_root->last;
        CardCheck(target_root->child_count == 2 && target_root->split_axis == axis2_from_dir2(directions[d]) &&
          placed->cfg == uishell_sidebar_local_view(window, area)->parent && abs_f32(placed->pct_of_parent-.5f) < .0001f,
          "directional card drop matches the ordinary half-panel split and side");
      }
      scratch_end(drop_scratch);
      cfg_node_release(rd_state->cfg, settled_host);
      // The sections this case made go with their Views.
      CFG_Node *local = uishell_sidebar_local_root(window);
      U64 index = 0;
      for(CFG_Node *n = local->first, *next; n != &cfg_nil_node; n = next, index++)
      { next = n->next; if(index >= sections_before) { cfg_node_release(rd_state->cfg, n); } }
      rd_state->drag_drop_creation_name = str8_zero(); rd_state->drag_drop_commit = 0;
    }
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("control_views_split_x")));
    if(saved_drop_axis != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, saved_drop_axis); }
    uishell_sidebar_manual_sizing(window, sized_before);
    cfg_node_insert_child(rd_state->cfg, window, window->last, old_host);

    // Dropping the last workspace tab out of a nested split closes its empty
    // source Panel. Collapsing that branch flattens the surviving split.
    fprintf(stderr, "Hover card diagnostics: focused source collapse\n");
    cfg_node_unhook(rd_state->cfg, window, old_host);
    CFG_Node *collapse_axis = cfg_node_child_from_string(window, str8_lit("control_views_split_x"));
    cfg_node_unhook(rd_state->cfg, window, collapse_axis);
    cfg_node_new(rd_state->cfg, window, str8_lit("control_views_split_x"));
    CFG_Node *collapse_root = cfg_node_new(rd_state->cfg, window, RD_DOCK_SIDEBAR_ROOT);
    CFG_Node *branch = cfg_node_new(rd_state->cfg, collapse_root, str8_lit("0.6"));
    CFG_Node *other = cfg_node_new(rd_state->cfg, collapse_root, str8_lit("0.4"));
    cfg_node_new(rd_state->cfg, other, str8_lit("sidebar_section"));
    CFG_Node *source_panel = cfg_node_new(rd_state->cfg, branch, str8_lit("0.5"));
    cfg_node_new(rd_state->cfg, source_panel, str8_lit("selected"));
    CFG_Node *moving_section = cfg_node_new(rd_state->cfg, source_panel, str8_lit("sidebar_section"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, moving_section, str8_lit("section")), str8_lit("workspaces"));
    CFG_Node *survivor = cfg_node_new(rd_state->cfg, branch, str8_lit("0.5"));
    CFG_Node *first_survivor = cfg_node_new(rd_state->cfg, survivor, str8_lit("0.25"));
    cfg_node_new(rd_state->cfg, first_survivor, str8_lit("pinned_cards"));
    CFG_Node *last_survivor = cfg_node_new(rd_state->cfg, survivor, str8_lit("0.75"));
    cfg_node_new(rd_state->cfg, last_survivor, str8_lit("sidebar_section"));
    CFG_ID survivor_id = survivor->id;
    UIShell_CmdNode *before_collapse = rd_state->cmds[0].last;
    UIShell_RegsScope(.window = window->id, .panel = source_panel->id, .view = moving_section->id, .tab = 0)
    { rd_drag_begin(UIShell_ContextRegSlot_View); }
    rd_panel_drag_drop(other->id, Dir2_Up, 0);
    rd_drag_kill();
    B32 closed_source = 0;
    for(UIShell_CmdNode *n = before_collapse ? before_collapse->next : rd_state->cmds[0].first; n; n = n->next)
    {
      if(str8_match(n->cmd.name, str8_lit("split_panel"), 0) || str8_match(n->cmd.name, str8_lit("close_panel"), 0)) UIShell_RegsScope()
      {
        MemoryCopyStruct(uishell_regs(), n->cmd.regs);
        uishell_dispatch_panel_command(n->cmd.name);
        closed_source |= str8_match(n->cmd.name, str8_lit("close_panel"), 0);
      }
    }
    CardCheck(closed_source && moving_section->parent != source_panel,
      "workspace section drop splits its destination and closes the empty source Panel");
    CardCheck(cfg_node_from_id(survivor_id) == &cfg_nil_node &&
      first_survivor->parent == collapse_root && last_survivor->parent == collapse_root &&
      abs_f32((F32)f64_from_str8(first_survivor->string)-.15f) < .0001f &&
      abs_f32((F32)f64_from_str8(last_survivor->string)-.45f) < .0001f,
      "empty source collapse flattens survivors and retains their allocation");
    B32 focused_survivor = 0;
    for(UIShell_CmdNode *n = before_collapse ? before_collapse->next : rd_state->cmds[0].first; n; n = n->next)
    { if(str8_match(n->cmd.name, str8_lit("focus_panel"), 0))
      { focused_survivor |= n->cmd.regs->panel == first_survivor->id; } }
    CardCheck(focused_survivor, "closing the focused source selects the first surviving leaf, not the released split");
    cfg_node_release(rd_state->cfg, collapse_root);
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("control_views_split_x")));
    if(collapse_axis != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, collapse_axis); }
    cfg_node_insert_child(rd_state->cfg, window, window->last, old_host);

    // The empty ordinary areas are confined to this diagnostic's disposable profile.
    uishell_sidebar_card_close(original);

    fprintf(stderr, "Hover card diagnostics: positioned card drops\n");
    // A card released on a local group's insertion point adds a ghost there,
    // as a card, and the group's order puts it at that index; a pinned card
    // dragged there moves its own ghost.
    {
      Temp scratch = scratch_begin(0, 0);
      CFG_Node *group = uishell_sidebar_local_new_group(window, str8_lit("Drops"));
      String8 group_id = push_str8_copy(scratch.arena, uishell_sidebar_local_field(group, str8_lit("id")));
      CFG_Node *pins[2];
      String8 ghosts[3] = {0};
      char *ids[] = {"drop-first", "drop-second"};
      for(U64 i = 0; i < 2; i++)
      {
        pins[i] = cfg_node_new(rd_state->cfg, group, str8_lit("card"));
        uishell_sidebar_pin_new_ghost(pins[i]);
        uishell_sidebar_pin_set_field(pins[i], str8_lit("kind"), str8_lit("workspace"));
        uishell_sidebar_pin_set_field(pins[i], str8_lit("entity"), str8_cstring(ids[i]));
        ghosts[i] = push_str8_copy(scratch.arena, uishell_sidebar_pin_ghost(pins[i]));
      }
      // Published and placed, the group's items form one run.
      UIShell_ControlledSplit drop_split = uishell_root_controlled_split_from_window(scratch.arena, window);
      uishell_sidebar_publish_local(&fixture, &drop_split);
      uishell_sidebar_refresh(&fixture);
      String8 loop = str8_zero();
      for(U64 i = 0; fixture.snapshot && i < andamento_snapshot_node_count(fixture.snapshot); i++)
      {
        AndamentoNode n = {0}; andamento_snapshot_node(fixture.snapshot, i, &n);
        if(str8_match(uishell_sidebar_string(n.entity_id), ghosts[0], 0)) { loop = push_str8_copy(scratch.arena, uishell_sidebar_loop_key(fixture.snapshot, i)); }
      }
      CardCheck(loop.size != 0, "a local group's ghosts are placed as one run");
      if(!fixture.group_claim_arena) { fixture.group_claim_arena = arena_alloc(); }
      fixture.group_claim_id = group_id; fixture.group_claim_loop = loop;
      fixture.group_claim_rect = r2f32p(-1e6, -1e6, 1e6, 1e6);
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
      fixture.drag_card = original; original->moving = original->drag_released = 1;
      fixture.group_claim_index = 1; fixture.group_claim_build = test->build_index;
      uishell_sidebar_drag_finish(ws);
      CFG_Node *added = group->last;
      ghosts[2] = push_str8_copy(scratch.arena, uishell_sidebar_pin_ghost(added));
      B32 ordered = fixture.order_pending && fixture.order_count == 3 &&
        str8_match(uishell_sidebar_string(fixture.order[0].id), ghosts[0], 0) &&
        str8_match(uishell_sidebar_string(fixture.order[1].id), ghosts[2], 0) &&
        str8_match(uishell_sidebar_string(fixture.order[2].id), ghosts[1], 0);
      CardCheck(added != pins[1] && str8_match(cfg_node_child_from_string(added, str8_lit("entity"))->first->string, uishell_sidebar_string(entity.entity_id), 0) &&
        uishell_sidebar_pin_expanded(added) && !original->open && !fixture.drag_card && ordered,
        "a card released on a group's insertion point becomes a ghost there, as a card, at that index");
      fixture.order_pending = 0;
      // Down to a middle slot, from the top of [first, second, added]: the
      // line between second and added is index 2 (counting every item, as the
      // line does), so first lands between them.
      uishell_sidebar_publish_local(&fixture, &drop_split);
      uishell_sidebar_refresh(&fixture);
      UIShell_HoverCard *first = uishell_sidebar_saved_card(ws, pins[0]);
      fixture.drag_card = first; first->moving = first->drag_released = 1;
      fixture.group_claim_index = 2; fixture.group_claim_build = test->build_index;
      uishell_sidebar_drag_finish(ws);
      CardCheck(fixture.order_pending && fixture.order_count == 3 && cfg_node_from_id(pins[0]->id)->parent == group && first->open &&
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
      for(UIShell_HoverCard *c = fixture.detached; c; c = c->next) { if(c->saved && cfg_node_from_id(c->saved)->parent == group) { uishell_sidebar_card_close(c); } }
      uishell_sidebar_detached_finish(ws);
      CFG_Node *drop_view = uishell_sidebar_local_view(window, group);
      if(drop_view != &cfg_nil_node) { cfg_node_release(rd_state->cfg, drop_view); }
      cfg_node_release(rd_state->cfg, group->parent);
      scratch_end(scratch);
    }
    arena_release(entity_arena);
  }

  fprintf(stderr, "Hover card diagnostics: cleanup\n");
  ws->sidebar = saved_sidebar; ws->ui = saved_window_ui;
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
