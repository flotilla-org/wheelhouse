// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// Uses a separate UI state and synthetic events; never sends input to the OS.
internal UI_ScrollRegionSignal
uishell_scroll_test_frame(RD_WindowState *ws, UI_ScrollRegion *region, UI_ScrollRegionAxis axes[Axis2_COUNT],
                          Vec2F32 mouse, UI_Event event, UI_Signal *content_signal)
{
  UI_IconInfo icons = ws->ui->icon_info;
  UI_AnimationInfo animation = {0};
  animation.scroll_animation_rate = animation.hot_animation_rate = 1.f;
  UI_EventNode node = {.v = event};
  UI_EventList events = {0};
  if(event.kind != UI_EventKind_Null) { events.first = events.last = &node; events.count = 1; }
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = mouse;
  UI_ScrollRegionSignal result = {0};
  UI_Font(fnt_tag_from_static_data_string(&rd_default_main_font_bytes)) UI_FontSize(16)
  {
    result = ui_scroll_region_build(ui_top_parent(), ui_key_make(1001), region, axes, UI_BoxFlag_Clickable|UI_BoxFlag_Scroll|UI_BoxFlag_ScrollPrecise);
    *content_signal = ui_signal_from_box(result.content_box);
  }
  ui_end_build();
  return result;
}

// A preview overlaps live content at full layout size before being composited.
// Its IgnoreInteraction ancestor must also suppress pointer-driven decoration.
internal B32
uishell_scroll_preview_diagnostics(RD_WindowState *ws)
{
  UI_State *saved = ui_state, *test = ui_state_alloc();
  ui_select_state(test);
  U32 failures = 0;
  UI_Key live_bar_key = ui_key_from_stringf(ui_key_make(201), "scroll_region_bar_%i", Axis2_Y);
  UI_Key track_key = ui_key_from_stringf(live_bar_key, "##_scroll_area_%i", Axis2_Y);
  UI_Key thumb_key = ui_key_from_stringf(track_key, "##_scroller_%i", Axis2_Y);
  Vec2F32 mouse = v2f32(290, 180);
  for(U32 frame = 0; frame < 8; frame++)
  {
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    animation.scroll_animation_rate = animation.hot_animation_rate = 1.f;
    UI_EventList events = {0};
    UI_EventNode press = {.v = {.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton}};
    if(frame == 4)
    {
      UI_Box *thumb = ui_box_from_key(thumb_key);
      if(ui_box_is_nil(thumb)) { failures++; break; }
      mouse = center_2f32(thumb->rect);
      press.v.pos = mouse;
      events.first = events.last = &press; events.count = 1;
    }
    if(frame >= 5) { mouse.y += 10; }
    if(frame == 6)
    {
      press.v = (UI_Event){.kind = UI_EventKind_Scroll, .pos = mouse,
                           .delta_2f32 = {0, 0.25f}, .scroll_is_precise = 1};
      events.first = events.last = &press; events.count = 1;
    }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = mouse;
    for(U32 preview = 0; preview < 2; preview++)
    {
      B32 inert = preview || frame >= 5;
      ui_set_next_rect(r2f32p(100, 100, 400, 300));
      UI_Box *wrapper = ui_build_box_from_key(inert ? UI_BoxFlag_IgnoreInteraction : 0, ui_key_make(101+preview));
      UI_ScrollRegionParams params = ui_scroll_region_params(r2f32p(0, 0, 300, 200), UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Always);
      params.style = UI_ScrollBarStyle_Overlay;
      UI_ScrollRegion region = ui_scroll_region_layout(params);
      UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
      axes[Axis2_Y] = (UI_ScrollRegionAxis){ui_scroll_pt(0, 0), r1s64(0, 600), 200};
      axes[Axis2_Y].position.target_off = 0.375f;
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(16)
      UI_Parent(wrapper) UI_Focus(inert ? UI_FocusKind_Off : UI_FocusKind_On)
      {
        UI_ScrollRegionSignal sig = ui_scroll_region_build(wrapper, ui_key_make(201+preview), &region, axes, UI_BoxFlag_Clickable|UI_BoxFlag_Scroll|UI_BoxFlag_ScrollPrecise);
        UI_Signal content = ui_signal_from_box(sig.content_box);
        if(inert && (sig.position.y.idx != axes[Axis2_Y].position.idx || sig.position.y.off != axes[Axis2_Y].position.off || sig.position.y.target_off != axes[Axis2_Y].position.target_off))
        { fprintf(stderr, "FAIL: inert scrollbar changes scroll position\n"); failures++; }
        if(inert && (ui_mouse_over(content) || ui_hovering(content) || ui_dragging(content)))
        { fprintf(stderr, "FAIL: preview content participates in input\n"); failures++; }
      }
    }
    ui_end_build();
    if(frame == 6 && events.count != 1)
    { fprintf(stderr, "FAIL: inert preview consumed precise scroll\n"); failures++; }
    if(frame == 4 && !ui_key_match(ui_active_key(UI_MouseButtonKind_Left), thumb_key))
    { fprintf(stderr, "FAIL: live scrollbar drag did not start\n"); failures++; }
    // The omitted thumb is pruned at end-build; next begin-build clears its key.
    if(frame == 7 && !ui_key_match(ui_active_key(UI_MouseButtonKind_Left), ui_key_zero()))
    { fprintf(stderr, "FAIL: inert preview retained scrollbar drag\n"); failures++; }
    if(frame == 3)
    {
      UI_Key live_bar = ui_key_from_stringf(ui_key_make(201), "scroll_region_bar_%i", Axis2_Y);
      UI_Key preview_bar = ui_key_from_stringf(ui_key_make(202), "scroll_region_bar_%i", Axis2_Y);
      B32 live_visible = !ui_box_is_nil(ui_box_from_key(live_bar));
      B32 preview_visible = !ui_box_is_nil(ui_box_from_key(preview_bar));
      fprintf(stderr, "Overlay hover: live=%i preview=%i (expected 1,0)\n", live_visible, preview_visible);
      failures += !live_visible || preview_visible;
    }
  }
  ui_select_state(saved);
  ui_state_release(test);
  return failures == 0;
}

// WM -> shell -> shared list, including the actual event-consumption seam.
internal B32
uishell_precise_list_diagnostics(RD_WindowState *ws)
{
  UI_State *saved = ui_state, *test = ui_state_alloc();
  UI_EventList saved_events = ws->ui_events;
  ui_select_state(test);
  U32 failures = 0;
  for(U32 height = 0; height < 2; height++)
  {
    F32 row_height = height ? 23.f : 17.f;
    UI_ScrollPt pt = ui_scroll_pt(10, 0);
    Vec2S64 cursor = v2s64(0, 1);
    for(U32 frame = 0; frame < 60; frame++)
    {
      UI_IconInfo icons = ws->ui->icon_info;
      UI_AnimationInfo animation = {0};
      animation.scroll_animation_rate = 1;
      ws->ui_events = (UI_EventList){0};
      ui_begin_build(ws->os, &ws->ui_events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      ui_state->mouse = v2f32(50, 50);
      if(frame == 48) { ui_scroll_pt_target_idx(&pt, 50); }
      if((frame >= 3 && frame < 11) || (frame >= 20 && frame < 28) || frame == 31 ||
         (frame >= 40 && frame < 44) || frame == 49)
      {
        WM_Event event = {.kind = WM_EventKind_Scroll, .window = ws->os,
                         .pos = {50, 50}, .delta = {0, 0.25f}, .scroll_is_precise = 1};
        if(frame >= 20 && frame < 28) { event.delta.y = -0.25f; }
        if(frame == 31) { event.delta.y = 0.25f; event.scroll_is_precise = 0; }
        if(frame >= 40 && frame < 44) { event.delta.y = row_height*0.25f; }
        UIShell_RegsScope(.wm_event = &event) { uishell_dispatch_app_command(str8_lit("wm_event")); }
        if(!ws->ui_events.first || ws->ui_events.first->v.scroll_is_precise != event.scroll_is_precise) { failures++; }
      }
      if(frame == 52)
      {
        UI_Event navigate = {.kind = UI_EventKind_Navigate, .delta_unit = UI_EventDeltaUnit_Whole, .delta_2s32 = {0, 1}};
        ui_event_list_push(ui_build_arena(), &ws->ui_events, &navigate);
      }
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(16) UI_Focus(UI_FocusKind_On)
      {
        ui_top_parent()->flags |= UI_BoxFlag_Scroll|UI_BoxFlag_ScrollPrecise;
        UI_ScrollListParams params = {.dim_px = {200, 150}, .row_height_px = row_height, .item_range = {0, 100}};
        params.flags = UI_ScrollListFlag_Nav|UI_ScrollListFlag_Snap;
        params.cursor_range = r2s64p(0, 1, 0, 100);
        params.snap_scroll = frame >= 40 && frame < 44;
        if(frame >= 55) { params.item_range.max = frame < 57 ? 3 : 0; }
        Rng1S64 visible = {0};
        UI_ScrollList(&params, &pt, &cursor, 0, &visible, 0)
        {
          if(frame >= 11 && frame < 20 && abs_f32(ui_top_parent()->view_off.y - 2.f) > 0.00001f) { failures++; }
          for(S64 row = visible.min; row < visible.max; row++) { ui_spacer(ui_px(row_height, 1)); }
        }
        // A containing consumer must not see the event a second time.
        UI_Signal parent = ui_signal_from_box(ui_top_parent());
        if(ws->ui_events.count != 0 || parent.scroll.y != 0 || parent.scroll_px.y != 0) { failures++; }
      }
      ui_end_build();
      if(frame >= 11 && frame < 20 && (pt.idx != 10 || abs_f64(pt.target_off - 2.f/row_height) > 0.00001f)) { failures++; }
      if(frame == 28 && abs_f64((F64)(pt.idx-10)+pt.target_off) > 0.00001f) { failures++; }
      if(frame == 31 && (pt.idx != 11 || pt.target_off != 0)) { failures++; }
      if(frame == 43 && (pt.idx != 12 || pt.target_off != 0 || pt.remainder != 0)) { failures++; }
      if(frame == 49 && (pt.idx != 50 || abs_f64(pt.target_off-0.25f/row_height) > 0.00001f)) { failures++; }
      if(frame == 52 && (cursor.y != 100 || pt.idx < 90 || pt.target_off != 0 || pt.remainder != 0)) { failures++; }
      if(frame == 55 && (pt.idx != 2 || pt.target_off != 0 || pt.remainder != 0 || pt.off != 0)) { failures++; }
      if(frame == 57 && (pt.idx != 0 || pt.target_off != 0 || pt.off != 0)) { failures++; }
      pt.off *= 0.5f; // Same displacement decay used by the shell's view state.
    }
  }
  ws->ui_events = saved_events;
  ui_select_state(saved);
  ui_state_release(test);
  fprintf(stderr, "Precise list diagnostics: %u failures\n", failures);
  return failures == 0;
}

internal B32
uishell_scroll_region_diagnostics(RD_WindowState *ws)
{
  U32 failures = 0;
#define ScrollCheck(condition, name) do { if(!(condition)) { fprintf(stderr, "FAIL: %s\n", name); failures += 1; } } while(0)
  // Targets, animation and row-snap input carry three different quantities.
  Rng1S64 bounds = r1s64(0, 100);
  UI_ScrollPt pt = ui_scroll_pt(10, 0);
  for(U32 i = 0; i < 100; i++) { ui_scroll_pt_scroll(&pt, 0.25f/17.f, bounds, 0); }
  ScrollCheck(pt.idx == 11 && abs_f64(pt.target_off - (25.f/17.f-1)) < 0.00001f, "noninteger pixel-to-row accumulation");
  for(U32 i = 0; i < 100; i++) { ui_scroll_pt_scroll(&pt, -0.25f/17.f, bounds, 0); }
  ScrollCheck(abs_f64((F64)(pt.idx-10)+pt.target_off) < 0.00001f, "opposite movement has zero net displacement");
  UI_ScrollPt before_zero = pt;
  ui_scroll_pt_scroll(&pt, 0, bounds, 0);
  ScrollCheck(MemoryMatch(&pt, &before_zero, sizeof(pt)), "zero movement preserves target");
  F32 invalid_deltas[] = {inf32(), neg_inf32(), inf32()-inf32(), 1.e30f, -1.e30f};
  for(U32 i = 0; i < ArrayCount(invalid_deltas); i++)
  {
    ui_scroll_pt_scroll(&pt, invalid_deltas[i], bounds, 0);
    ScrollCheck(MemoryMatch(&pt, &before_zero, sizeof(pt)), "invalid or unrepresentable input preserves target");
  }
  pt = ui_scroll_pt(10, 0);
  for(U32 i = 0; i < 3; i++) { ui_scroll_pt_scroll(&pt, 0.25f, bounds, 1); }
  ScrollCheck(pt.idx == 10 && pt.target_off == 0 && pt.remainder == 0.75f, "snap accumulates sub-row input");
  ui_scroll_pt_scroll(&pt, -0.25f, bounds, 1);
  ui_scroll_pt_scroll(&pt, 0.5f, bounds, 1);
  ScrollCheck(pt.idx == 11 && pt.remainder == 0 && pt.off == -1, "snap reversal consumes remainder into animated step");
  ui_scroll_pt_scroll(&pt, 0.25f, bounds, 0);
  ScrollCheck(pt.idx == 11 && pt.target_off == 0.25f && pt.off == 0, "precise target is independent of transient displacement");
  ui_scroll_pt_target_idx(&pt, 50);
  ScrollCheck(pt.idx == 50 && pt.target_off == 0 && pt.remainder == 0, "programmatic target clears fractional state");
  ui_scroll_pt_scroll(&pt, 0.25f, bounds, 0);
  ScrollCheck(pt.idx == 50 && pt.target_off == 0.25f, "new input does not resurrect pre-jump animation");
  ui_scroll_pt_scroll(&pt, 1000, bounds, 0);
  ScrollCheck(pt.idx == 100 && pt.target_off == 0 && pt.off == 0, "upper bound discards overshoot");
  ui_scroll_pt_scroll(&pt, -1000, bounds, 0);
  ScrollCheck(pt.idx == 0 && pt.target_off == 0 && pt.off == 0, "lower bound discards overshoot");
  ui_scroll_pt_scroll(&pt, -0.75f, bounds, 1);
  ui_scroll_pt_scroll(&pt, 1, bounds, 1);
  ScrollCheck(pt.idx == 1 && pt.remainder == 0, "outward snap remainder does not delay reversal");
  pt = (UI_ScrollPt){.idx = 90, .target_off = 0.75f, .off = -10, .remainder = 0.5f};
  ui_scroll_pt_clamp_idx(&pt, r1s64(0, 20));
  ScrollCheck(pt.idx == 20 && pt.target_off == 0 && pt.off == 0 && pt.remainder == 0, "content shrink clears stale motion at nearest bound");
  ui_scroll_pt_clamp_idx(&pt, r1s64(0, 0));
  ScrollCheck(pt.idx == 0 && pt.target_off == 0 && pt.off == 0 && pt.remainder == 0, "empty content resets all motion");
  S64 large = (S64)1 << 54;
  pt = ui_scroll_pt(large, 0);
  bounds = r1s64(large-100, large+100);
  ui_scroll_pt_scroll(&pt, -0.25f, bounds, 0);
  ScrollCheck(pt.idx == large-1 && pt.target_off == 0.75f, "large integer index retains reverse fraction");
  ui_scroll_pt_scroll(&pt, 0.5f, bounds, 0);
  ScrollCheck(pt.idx == large && pt.target_off == 0.25f, "large integer index retains forward fraction");

  UI_ScrollRegionParams params = {0};
  params.rect = r2f32p(20, 30, 220, 230);
  params.gutter_px = 20;
  params.overlay_rest_px = 7;
  params.overlay_hover_px = 14;
  params.overlay_inset_px = 3;
  params.axis[0] = params.axis[1] = UI_ScrollAxisPolicy_Auto;
  params.content_dim_px = v2f32(195, 205);
  UI_ScrollRegion region = ui_scroll_region_layout(params);
  ScrollCheck(region.bar_enabled[0] && region.bar_enabled[1], "vertical gutter induces horizontal overflow");
  ScrollCheck(region.viewport.x1 == 200 && region.viewport.y1 == 210, "both classic gutters reduce viewport");
  params.content_dim_px = v2f32(205, 195);
  region = ui_scroll_region_layout(params);
  ScrollCheck(region.bar_enabled[0] && region.bar_enabled[1], "horizontal gutter induces vertical overflow");
  params.content_dim_px = v2f32(200, 200);
  region = ui_scroll_region_layout(params);
  ScrollCheck(!region.bar_enabled[0] && !region.bar_enabled[1], "exact fit has no auto gutters");
  params.style = UI_ScrollBarStyle_Overlay;
  params.content_dim_px = v2f32(195, 205);
  region = ui_scroll_region_layout(params);
  ScrollCheck(!region.bar_enabled[0] && region.bar_enabled[1] && region.viewport.x1 == 220 && region.viewport.y1 == 230,
              "overlay doesn't introduce overflow on other axis");
  params.axis[0] = params.axis[1] = UI_ScrollAxisPolicy_Always;
  region = ui_scroll_region_layout(params);
  Rng2F32 xbar = ui_scroll_region_bar_rect(&region, Axis2_X, 14, 1);
  Rng2F32 ybar = ui_scroll_region_bar_rect(&region, Axis2_Y, 14, 1);
  ScrollCheck(xbar.x1 < ybar.x0 && ybar.y1 < xbar.y0, "expanded overlays have separate corner hit areas");
  params.rect = r2f32p(10, 10, 12, 11);
  for(U32 style = 0; style < 2; style += 1)
  {
    params.style = (UI_ScrollBarStyle)style;
    region = ui_scroll_region_layout(params);
    ScrollCheck(region.viewport.x1 >= region.viewport.x0 && region.viewport.y1 >= region.viewport.y0, "tiny viewport stays nonnegative");
    for EachEnumVal(Axis2, axis)
    {
      Rng2F32 bar = ui_scroll_region_bar_rect(&region, axis, 14, 1);
      ScrollCheck(bar.x0 >= 10 && bar.x1 <= 12 && bar.y0 >= 10 && bar.y1 <= 11 && bar.x1 >= bar.x0 && bar.y1 >= bar.y0,
                  "tiny bar remains within bounds");
    }
  }

  UI_State *saved = ui_state;
  for(U32 style = 0; style < 2; style += 1)
  {
    UI_State *test_ui = ui_state_alloc();
    ui_select_state(test_ui);
    params.rect = r2f32p(100, 100, 400, 300);
    params.style = (UI_ScrollBarStyle)style;
    region = ui_scroll_region_layout(params);
    UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
    axes[0] = (UI_ScrollRegionAxis){ui_scroll_pt(0, 0), r1s64(0, 600), 300};
    axes[1] = (UI_ScrollRegionAxis){ui_scroll_pt(0, 0), r1s64(0, 600), 200};
    axes[0].position.target_off = 0.625f;
    axes[1].position.target_off = 0.375f;
    UI_Signal content = {0};
    UI_Event none = {0};
    Vec2F32 mouse = v2f32(200, 180);
    for(U32 frame = 0; frame < 3; frame += 1)
    {
      uishell_scroll_test_frame(ws, &region, axes, mouse, none, &content);
    }
    for EachEnumVal(Axis2, axis)
    {
      UI_Key bar_key = ui_key_from_stringf(ui_key_make(1001), "scroll_region_bar_%i", axis);
      UI_Key track_key = ui_key_from_stringf(bar_key, "##_scroll_area_%i", axis);
      UI_Key thumb_key = ui_key_from_stringf(track_key, "##_scroller_%i", axis);
      UI_Box *thumb = ui_box_from_key(thumb_key);
      ScrollCheck(!ui_box_is_nil(thumb), "thumb built for each axis/style");
      if(ui_box_is_nil(thumb)) { continue; }
      Vec2F32 press = scale_2f32(add_2f32(thumb->rect.p0, thumb->rect.p1), 0.5f);
      // Settle hover expansion before the press.
      for(U32 frame = 0; frame < 3; frame += 1)
      {
        uishell_scroll_test_frame(ws, &region, axes, press, none, &content);
      }
      thumb = ui_box_from_key(thumb_key);
      press = scale_2f32(add_2f32(thumb->rect.p0, thumb->rect.p1), 0.5f);
      UI_Box *track = thumb->parent;
      ScrollCheck(thumb->rect.x0 >= track->rect.x0 && thumb->rect.x1 <= track->rect.x1 &&
                  thumb->rect.y0 >= track->rect.y0 && thumb->rect.y1 <= track->rect.y1, "thumb lies within track");
      F32 expected_length = dim_2f32(track->rect).v[axis] * axes[axis].visible / (600.f + axes[axis].visible);
      ScrollCheck(abs_f32(dim_2f32(thumb->rect).v[axis] - expected_length) <= 2.f, "thumb length reflects visible fraction on both styles");
      UI_Event event = {.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = press};
      UI_ScrollRegionSignal stationary = uishell_scroll_test_frame(ws, &region, axes, press, event, &content);
      ScrollCheck(stationary.position.v[axis].target_off == axes[axis].position.target_off, "stationary thumb press preserves fractional target");
      ScrollCheck(ui_key_match(ui_active_key(UI_MouseButtonKind_Left), thumb_key) && !ui_pressed(content), "thumb takes press above content");
      Vec2F32 outside = v2f32(1000, 1000);
      UI_ScrollRegionSignal drag = uishell_scroll_test_frame(ws, &region, axes, outside, none, &content);
      ScrollCheck(drag.position.v[axis].idx == 600 && drag.position.v[axis2_flip(axis)].idx == 0, "drag outside moves only selected axis to its limit");
      for(U32 frame = 0; frame < 4; frame += 1)
      {
        drag = uishell_scroll_test_frame(ws, &region, axes, outside, none, &content);
      }
      ScrollCheck(ui_key_match(ui_active_key(UI_MouseButtonKind_Left), thumb_key), "drag survives pointer leaving region and fade");
      event = (UI_Event){.kind = UI_EventKind_Release, .key = WM_Key_LeftMouseButton, .pos = outside};
      uishell_scroll_test_frame(ws, &region, axes, outside, event, &content);
      ScrollCheck(ui_key_match(ui_active_key(UI_MouseButtonKind_Left), ui_key_zero()), "release outside ends drag");
      for(U32 frame = 0; frame < 3; frame += 1)
      {
        uishell_scroll_test_frame(ws, &region, axes, mouse, none, &content);
      }
    }
    UI_Event wheel = {.kind = UI_EventKind_Scroll, .pos = mouse, .delta_2f32 = {0, 30}};
    uishell_scroll_test_frame(ws, &region, axes, mouse, wheel, &content);
    ScrollCheck(content.scroll.y == 1 && content.scroll.x == 0, "content receives wheel exactly once");
    wheel.modifiers = WM_Modifier_Shift;
    uishell_scroll_test_frame(ws, &region, axes, mouse, wheel, &content);
    ScrollCheck(content.scroll.x == 1 && content.scroll.y == 0, "content retains Shift-wheel axis routing");
    wheel.scroll_is_precise = 1;
    wheel.delta_2f32.y = 0.25f;
    uishell_scroll_test_frame(ws, &region, axes, mouse, wheel, &content);
    ScrollCheck(content.scroll_px.x == 0.25f && content.scroll_px.y == 0 && content.scroll.x == 0,
                "Shift routes precise pixels without discrete steps");
    wheel.modifiers = 0;
    wheel.delta_2f32.y = -0.25f;
    uishell_scroll_test_frame(ws, &region, axes, mouse, wheel, &content);
    ScrollCheck(content.scroll_px.y == -0.25f && content.scroll.y == 0, "negative precise signal retains magnitude");
    wheel.delta_2f32.y = 0;
    uishell_scroll_test_frame(ws, &region, axes, mouse, wheel, &content);
    ScrollCheck(content.scroll_px.y == 0 && content.scroll.y == 0, "zero precise event produces no movement");
    // No scroll range: overlays disappear; classic keeps disabled furniture.
    axes[0].range.max = axes[1].range.max = 0;
    uishell_scroll_test_frame(ws, &region, axes, mouse, none, &content);
    UI_Key bar_key = ui_key_from_stringf(ui_key_make(1001), "scroll_region_bar_%i", Axis2_Y);
    UI_Box *bar = ui_box_from_key(bar_key);
    B32 touched = !ui_box_is_nil(bar) && bar->last_touched_build_index == test_ui->build_index-1;
    ScrollCheck(touched == (style == UI_ScrollBarStyle_Classic), "empty range follows style visibility policy");
    ui_select_state(saved);
    ui_state_release(test_ui);
  }
  failures += !uishell_terminal_selection_ui_diagnostics(ws);
  failures += !uishell_terminal_override_ui_diagnostics(ws);
#if OS_MAC
  failures += !uishell_terminal_override_appkit_diagnostics();
#endif
  failures += !uishell_scroll_preview_diagnostics(ws);
  failures += !uishell_precise_list_diagnostics(ws);
  fprintf(stderr, "Scroll region diagnostics: %u failures\n", failures);
#undef ScrollCheck
  return failures == 0;
}
