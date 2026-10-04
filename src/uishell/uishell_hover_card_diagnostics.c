internal size_t
action_node_copy(UIShell_SidebarState *state, U64 index)
{
  AndamentoDetail detail = {0};
  return andamento_snapshot_detail(state->snapshot, index, &detail) ? detail.copy_url : ANDAMENTO_NONE;
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
    ui_state->mouse = mouse; MemoryZeroArray(ui_state->hover_card_keys);
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
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  UIShell_SidebarState *saved_sidebar = ws->sidebar, fixture = {0};
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
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("Close"), 0) && !card->open &&
                fixture.cards[1].open && fixture.cards[1].focused, "closing the original leaves the separate card open and focused");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, &fixture.cards[1], str8_lit("Close"), 0) &&
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
      // Full production layout catches fixed-rectangle scope leakage into
      // fields and buttons, rather than only testing their existence.
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
        if(frame == 2) { CardCheck(card->open && !card->focused, "hover remains informational when the window is not the keyboard target"); }
        ui_end_build();
        UI_Key root_key = ui_key_from_stringf(ui_key_zero(), "###sidebar_card_%I64u", (U64)0);
        UI_Box *root = ui_box_from_key(root_key);
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
          String8 text = ui_box_display_string(hot);
          saw_back |= str8_match(text, str8_lit("← Back"), 0);
          saw_close |= str8_match(text, str8_lit("Close"), 0);
          saw_related |= !saw_back && !saw_close && !!(hot->flags & UI_BoxFlag_Clickable);
        }
      }
      CardCheck(saw_related && saw_back && saw_close && !card->open && !test->hover_card_focus,
                "Tab reaches Related, Back and Close and Enter activates Close");
      // Hiding the sidebar also dismisses the independent source-less card,
      // before its controls can enqueue an action without a dispatcher.
      uishell_sidebar_card_set(card, live, ui_key_zero(), str8_zero(), 0, now_time_us());
      uishell_sidebar_card_set(&fixture.cards[1], parent, ui_key_zero(), str8_zero(), 0, now_time_us());
      fixture.cards[1].focused = 1; fixture.card_has_action = 1;
      UI_EventList hidden_events = {0};
      ui_begin_build(ws->os, &hidden_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      uishell_sidebar_cards_ui_at(ws, now_time_us(), 1, 0); ui_end_build();
      CardCheck(!card->open && !fixture.cards[1].open && !fixture.card_has_action && !test->hover_card_focus,
                "hidden sidebar closes both cards before an action can be emitted");
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
    UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    MemoryZeroArray(test->hover_card_keys);
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    UI_PrefWidth(ui_px(400, 1)) UI_PrefHeight(ui_em(1.6f, 1)) UI_ChildLayoutAxis(Axis2_Y)
    { uishell_sidebar_card_content(&freshness, ws, fresh_card, 0, node, index, 400, 0); }
    ui_end_build();
    B32 stale = 0, age = 0, empty = 0;
    for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
    {
      String8 text = ui_box_display_string(box);
      stale |= str8_match(text, str8_lit("retained-branch"), 0) && box->transparency >= 0.5f;
      age |= str8_match(text, str8_lit("1m ago"), 0) && box->transparency >= 0.5f;
      empty |= str8_match(text, str8_lit("Upstream"), 0);
    }
    CardCheck(stale && age, "retained stale values and their controller-relative ages are dimmed");
    CardCheck(empty, "known-empty facts retain their separate labels");
    uishell_sidebar_release(&freshness);

  }
  ws->sidebar = saved_sidebar; ws->ui = saved_window_ui;
  uishell_sidebar_release(&fixture); ui_select_state(saved_ui); ui_state_release(test);
  fprintf(stderr, "Hover card diagnostics: %u failures\n", failures);
#undef CardCheck
  return failures == 0;
}
