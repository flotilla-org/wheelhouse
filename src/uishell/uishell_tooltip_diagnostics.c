// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// Exercise actual tooltip construction and end-of-frame placement, not a copy
// of the bounds arithmetic. No synthetic events are sent to the OS.
internal B32
uishell_tooltip_diagnostics(RD_WindowState *ws)
{
  UI_State *saved = ui_state, *test = ui_state_alloc();
  ui_select_state(test);
  U32 failures = 0;
  Rng2F32 window = wm_client_rect_from_window(ws->os);
  Vec2F32 size = dim_2f32(window);
  for(U32 scenario = 0; scenario < 5; scenario++)
  {
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    Rng2F32 anchor_rect = r2f32p(window.x1-30, window.y1-30, window.x1-10, window.y1-10);
    if(scenario == 1) { anchor_rect = r2f32p(20, 20, 40, 40); }
    UI_Box *anchor;
    UI_Rect(anchor_rect) UI_FontSize(12)
    { anchor = ui_build_box_from_string(0, str8_lit("tooltip_test_anchor")); }
    ui_state->tooltip_anchor_key = anchor->key;
    if(scenario == 2)
    {
      // The unanchored path starts at the cursor plus the ordinary offset.
      ui_state->tooltip_anchor_key = ui_key_zero();
      ui_state->tooltip_root->fixed_position = v2f32(window.x1+10, window.y1+10);
    }
    ui_state->tooltip_can_overflow_window = scenario == 4;
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12) UI_Tooltip
    UI_PrefWidth(ui_px(scenario == 3 ? size.x+100 : 160, 1))
    UI_PrefHeight(ui_px(scenario == 3 ? size.y+100 : 80, 1))
    { ui_label(str8_lit("Tooltip geometry fixture")); }
    ui_end_build();
    UI_Box *root = test->tooltip_root, *card = root->first;
    B32 ok = !ui_box_is_nil(card) && root->fixed_size.x >= 160 && root->fixed_size.y >= 80;
    if(scenario != 4)
    {
      ok &= card->rect.x0 >= window.x0 && card->rect.y0 >= window.y0 &&
        card->rect.x1 <= window.x1 && card->rect.y1 <= window.y1;
      if(scenario == 0) { ok &= card->rect.y1 <= anchor->rect.y0; }
      if(scenario == 1) { ok &= card->rect.y0 >= anchor->rect.y1; }
      if(scenario == 3) { ok &= !!(card->flags & UI_BoxFlag_Clip); }
    }
    else { ok &= card->rect.x1 > window.x1 && card->rect.y1 > window.y1; }
    if(!ok)
    {
      fprintf(stderr, "FAIL tooltip scenario %u: root %gx%g, card [%g,%g,%g,%g], window [%g,%g,%g,%g]\n",
        scenario, root->fixed_size.x, root->fixed_size.y,
        card->rect.x0, card->rect.y0, card->rect.x1, card->rect.y1,
        window.x0, window.y0, window.x1, window.y1);
      failures++;
    }
  }
  ui_select_state(saved);
  ui_state_release(test);
  fprintf(stderr, "Tooltip diagnostics: %u failures\n", failures);
  return failures == 0;
}
