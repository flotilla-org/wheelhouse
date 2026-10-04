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
  AndamentoNode a = {.key = uishell_sidebar_text(str8_lit("a"))};
  AndamentoNode b = {.key = uishell_sidebar_text(str8_lit("b"))};
  UIShell_HoverCard *card = &fixture.cards[0];
  test->mouse = v2f32(50, 115);
  uishell_sidebar_card_source_at(&fixture, a, hover, str8_zero(), 0, 1000000);
  CardCheck(!card->open && !test->hover_card_focus, "first hover starts without focus");
  uishell_sidebar_card_source_at(&fixture, a, hover, str8_zero(), 0, 1299999);
  CardCheck(!card->open, "first hover waits 300ms");
  uishell_sidebar_card_source_at(&fixture, a, hover, str8_zero(), 0, 1300000);
  CardCheck(card->open && !card->engaged && !card->focused, "300ms opens an informational peek");
  uishell_sidebar_card_source_at(&fixture, b, hover, str8_zero(), 0, 1300001);
  CardCheck(card->open && str8_match(card->path[0], str8_lit("b"), 0) &&
            str8_match(card->previous, str8_lit("a"), 0), "next source swaps immediately and retains outgoing content");
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
  uishell_sidebar_card_navigate(card, str8_lit("b"), 3300001);
  CardCheck(card->depth == 2 && str8_match(card->path[0], str8_lit("a"), 0) &&
            str8_match(card->path[1], str8_lit("b"), 0), "related navigation retains a back path");
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
  for(U64 i = 0; i < 40; i++) { uishell_sidebar_card_navigate(card, str8_lit("additional target"), 3500001+i); }
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
          title |= str8_match(str8_prefix(text, 6), str8_lit("Title:"), 0);
          actions |= str8_match(text, str8_lit("Copy URL"), 0);
        }
        CardCheck(title, "current flat title field remains rendered");
        CardCheck(actions == engaged, "actions appear only when engaged");
      }
      AndamentoNode parent = {0}; andamento_snapshot_node(fixture.snapshot, live.parent, &parent);
      String8 parent_label = uishell_sidebar_string(parent.label);
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, parent_label, 0) && card->depth == 2,
                "clicking a related widget navigates within the card");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("← Back"), 0) && card->depth == 1 &&
                str8_match(card->path[0], uishell_sidebar_string(live.key), 0), "Back widget restores the source target");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, parent_label, WM_Modifier_Ctrl) && card->depth == 1 &&
                fixture.cards[1].open && fixture.cards[1].focused &&
                str8_match(fixture.cards[1].path[0], uishell_sidebar_string(parent.key), 0),
                "modifier-click opens a separate focused card without changing the original path");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, card, str8_lit("Close"), 0) && !card->open &&
                fixture.cards[1].open && fixture.cards[1].focused, "closing the original leaves the separate card open and focused");
      CardCheck(uishell_hover_card_test_click(ws, &fixture, &fixture.cards[1], str8_lit("Close"), 0) &&
                !fixture.cards[1].open, "the separate card closes through its own action");
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
        { uishell_sidebar_cards_ui_at(ws, now_time_us(), frame != 2); }
        if(frame == 2) { CardCheck(card->open && !card->focused, "hover remains informational when the window is not the keyboard target"); }
        ui_end_build();
        UI_Key root_key = ui_key_from_stringf(ui_key_zero(), "###sidebar_card_%I64u", (U64)0);
        UI_Box *root = ui_box_from_key(root_key);
        CardCheck(!ui_box_is_nil(root) && dim_2f32(root->rect).y > 40, "full card measures its content on the first frame");
        F32 previous_y = -1; U64 lines = 0;
        for(UI_Box *box = root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, root).next)
        {
          B32 outgoing = 0;
          for(UI_Box *p = box; !ui_box_is_nil(p); p = p->parent) { outgoing |= !!(p->flags & UI_BoxFlag_IgnoreInteraction); }
          if(outgoing || !(box->flags & UI_BoxFlag_DrawText)) { continue; }
          CardCheck(box->rect.y0 >= previous_y && dim_2f32(box->rect).y < dim_2f32(root->rect).y,
                    "fields and controls occupy individual rows in full card layout");
          previous_y = box->rect.y1; lines++;
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
      uishell_sidebar_card_close(card); uishell_sidebar_card_close(&fixture.cards[1]);
      UI_EventList events = {0}; ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      uishell_sidebar_cards_ui_at(ws, now_time_us(), 1); ui_end_build();
      CardCheck(!test->hover_card_focus, "closing the last card clears focus before View event consumers");
    }
  }
  ws->sidebar = saved_sidebar; ws->ui = saved_window_ui;
  uishell_sidebar_release(&fixture); ui_select_state(saved_ui); ui_state_release(test);
  fprintf(stderr, "Hover card diagnostics: %u failures\n", failures);
#undef CardCheck
  return failures == 0;
}
