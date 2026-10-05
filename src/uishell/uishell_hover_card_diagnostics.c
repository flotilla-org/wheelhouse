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
    F32 nil_scroll_before = ui_nil_box.view_off_target.y;
    for(U64 frame = 0; frame < 3; frame++)
    {
      UI_EventList events = {0}; ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      test->mouse = v2f32(-100, -100); test->hover_card_extra = 0; MemoryZeroArray(test->hover_card_keys);
      UIShell_RegsScope(.window = window->id, .view = saved->parent->id)
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      { RD_VIEW_UI_FUNCTION_NAME(pinned_cards)((E_Eval){0}, r2f32p(17, 29, 297, 329)); }
      ui_end_build();
      CardCheck(ui_nil_box.view_off_target.y == nil_scroll_before,
                "first-frame pin reveal never writes the shared nil box");
      if(frame == 0)
      { CardCheck(fixture.pin_reveal == saved_id && pinned->focused, "new pin keeps its reveal pending until measured"); }
      if(frame == 1)
      { CardCheck(!fixture.pin_reveal, "measured new pin completes its reveal on the next frame"); }
      UI_Box *body = ui_box_from_key(pinned->mask.key);
      CardCheck(!ui_box_is_nil(body) && body->rect.y0 >= 29+24 && body->fixed_size.y > 40,
                "pinned View renders a measured body below its ordinary section header");
      uishell_sidebar_detached_bounds(ws);
      CardCheck(pinned->rect.x0 >= 17 && pinned->rect.x1 <= 297 && pinned->rect.y1 <= 329,
                "pinned WM hit geometry is clipped to the section viewport");
    }
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
    CFG_Node *moved = uishell_sidebar_card_pin(ws, original, 1);
    CardCheck(moved->id == saved_id && moved->parent != area, "dragging to a new pinned area moves the unique card");
    B32 has_card = 0;
    for(CFG_Node *n = area->first; n != &cfg_nil_node; n = n->next)
    { has_card |= str8_match(n->string, str8_lit("card"), 0); }
    CardCheck(!has_card && cfg_node_from_id(area->id) == area && rd_dock_can_close(area),
              "moving the last pin preserves an empty area that can be closed");
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
      uishell_sidebar_card_drag_finish(ws);
      ui_end_build();
      if(frame == 0)
      {
        for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
        { if(str8_match(ui_box_display_string(box), str8_lit("⋮⋮"), 0)) { drag_start = center_2f32(box->rect); break; } }
        CardCheck(drag_start.x > 0, "engaged production overlay exposes the drag control");
      }
      if(frame == 2) { CardCheck(original->moving && original->open, "native drag motion detaches the overlay from its anchor"); }
    }
    CardCheck(!original->open && fixture.detached->open && fixture.detached->placement == UIShell_CardPlacement_Float,
              "native drag release outside the sidebar creates a float");
    uishell_sidebar_card_close(fixture.detached);

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
    uishell_sidebar_pin_deduplicate(window, window);
    CardCheck(cfg_node_from_id(copy_id) == &cfg_nil_node && cfg_node_from_id(unique->id) == unique,
              "layout copies reconcile to one pinned card per entity");
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
    uishell_sidebar_pin_deduplicate(window, window);
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
    UI_EventList tolerance_events = {0};
    ui_begin_build(ws->os, &tolerance_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    test->hover_card_extra = 0; MemoryZeroArray(test->hover_card_keys);
    UIShell_RegsScope(.window = window->id, .view = tolerance_area->id)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    { RD_VIEW_UI_FUNCTION_NAME(pinned_cards)((E_Eval){0}, r2f32p(17, 29, 297, 629)); }
    ui_end_build();
    U64 missing_markers = 0;
    for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
    { missing_markers += str8_match(ui_box_display_string(box), str8_lit("No longer present"), 0); }
    CardCheck(missing_markers >= 2 && uishell_sidebar_saved_card(ws, unknown)->open && uishell_sidebar_saved_card(ws, incomplete)->open,
              "unknown and malformed saved pins render a missing marker and remain explicitly closable");
    cfg_node_release(rd_state->cfg, unknown); cfg_node_release(rd_state->cfg, incomplete);
    cfg_node_release(rd_state->cfg, unique);
    // A wrong controlled-split owner rejects creation before any layout edit.
    CFG_Node *invalid_owner = cfg_node_new(rd_state->cfg, window, str8_lit("invalid_owner"));
    CFG_Node *invalid_root = cfg_node_new(rd_state->cfg, invalid_owner, RD_DOCK_SIDEBAR_ROOT);
    cfg_node_new(rd_state->cfg, invalid_root, str8_lit("sidebar_section"));
    Temp failed_scratch = scratch_begin(0, 0);
    String8 before_failure = cfg_string_from_tree(failed_scratch.arena, rd_state->cfg_schema_table, str8_zero(), invalid_owner);
    CFG_ID valid_window_id = ws->cfg_id;
    ws->cfg_id = invalid_owner->id;
    CFG_Node *failed_pin = uishell_sidebar_card_pin(ws, original, 1);
    ws->cfg_id = valid_window_id;
    String8 after_failure = cfg_string_from_tree(failed_scratch.arena, rd_state->cfg_schema_table, str8_zero(), invalid_owner);
    CardCheck(failed_pin == &cfg_nil_node && str8_match(before_failure, after_failure, 0),
              "rejected pin creation preserves merged tabs and panel ratios exactly");
    scratch_end(failed_scratch);
    cfg_node_release(rd_state->cfg, invalid_owner);
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
    for(U64 d = 0; d < ArrayCount(directions); d++)
    {
      CFG_Node *host = cfg_node_new(rd_state->cfg, window, RD_DOCK_SIDEBAR_ROOT);
      CFG_Node *existing = cfg_node_new(rd_state->cfg, host, str8_lit("pinned_cards"));
      uishell_sidebar_card_set(original, entity, ui_key_zero(), str8_zero(), 0, now_time_us());
      original->engaged = original->focused = original->moving = original->drag_released = 1;
      fixture.drag_card = original;
      UIShell_RegsScope(.window = window->id, .view = 0, .panel = 0)
      { rd_drag_begin(UIShell_ContextRegSlot_View); }
      rd_state->drag_drop_creation_name = str8_lit("pinned_cards");
      rd_state->drag_drop_commit = uishell_sidebar_card_panel_drop;
      CardCheck(rd_panel_drag_target(&cfg_nil_node, host, 320), "creation drag uses the registered panel validity checker");
      rd_state->drag_drop_state = RD_DragDropState_Dropping;
      UIShell_CmdNode *before_drop = rd_state->cmds[0].last;
      if(rd_drag_drop()) { rd_panel_drag_drop(host->id, directions[d], existing->id); }
      UI_EventList drop_events = {0};
      ui_begin_build(ws->os, &drop_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      test->mouse = v2f32(500, 100);
      uishell_sidebar_card_drag_finish(ws);
      ui_end_build();
      CFG_Node *entry = uishell_sidebar_pin_find(window, uishell_sidebar_card_entity(entity), 0);
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
      if(directions[d] == Dir2_Invalid)
      { CardCheck(area == existing && drop_root->child_count == 0, "center drop joins the exact existing pinned area"); }
      else
      {
        Side side = side_from_dir2(directions[d]);
        CFG_PanelNode *placed = side == Side_Min ? drop_root->first : drop_root->last;
        CardCheck(drop_root->child_count == 2 && drop_root->split_axis == axis2_from_dir2(directions[d]) &&
          placed->cfg == area->parent && abs_f32(placed->pct_of_parent-.5f) < .0001f,
          "directional card drop matches the ordinary half-panel split and side");
      }
      scratch_end(drop_scratch);
      cfg_node_release(rd_state->cfg, settled_host);
      rd_state->drag_drop_creation_name = str8_zero(); rd_state->drag_drop_commit = 0;
    }
    uishell_sidebar_manual_sizing(window, sized_before);
    cfg_node_insert_child(rd_state->cfg, window, window->last, old_host);

    // The empty ordinary areas are confined to this diagnostic's disposable profile.
    uishell_sidebar_card_close(original);
  }

  fprintf(stderr, "Hover card diagnostics: cleanup\n");
  ws->sidebar = saved_sidebar; ws->ui = saved_window_ui;
  uishell_sidebar_release(&fixture); ui_select_state(saved_ui); ui_state_release(test);
  fprintf(stderr, "Hover card diagnostics: %u failures\n", failures);
#undef CardCheck
  return failures == 0;
}
