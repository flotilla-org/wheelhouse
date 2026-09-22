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
    result = ui_scroll_region_build(ui_top_parent(), ui_key_make(1001), region, axes, UI_BoxFlag_Clickable|UI_BoxFlag_Scroll);
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
  for(U32 frame = 0; frame < 4; frame++)
  {
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    animation.scroll_animation_rate = animation.hot_animation_rate = 1.f;
    UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = v2f32(290, 180);
    for(U32 preview = 0; preview < 2; preview++)
    {
      ui_set_next_rect(r2f32p(100, 100, 400, 300));
      UI_Box *wrapper = ui_build_box_from_key(preview ? UI_BoxFlag_IgnoreInteraction : 0, ui_key_make(101+preview));
      UI_ScrollRegionParams params = ui_scroll_region_params(r2f32p(0, 0, 300, 200), UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Always);
      params.style = UI_ScrollBarStyle_Overlay;
      UI_ScrollRegion region = ui_scroll_region_layout(params);
      UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
      axes[Axis2_Y] = (UI_ScrollRegionAxis){ui_scroll_pt(0, 0), r1s64(0, 600), 200};
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(16)
      UI_Parent(wrapper) UI_Focus(preview ? UI_FocusKind_Off : UI_FocusKind_On)
      {
        UI_ScrollRegionSignal sig = ui_scroll_region_build(wrapper, ui_key_make(201+preview), &region, axes, UI_BoxFlag_Clickable|UI_BoxFlag_Scroll);
        UI_Signal content = ui_signal_from_box(sig.content_box);
        if(preview && (ui_mouse_over(content) || ui_hovering(content) || ui_dragging(content)))
        { fprintf(stderr, "FAIL: preview content participates in input\n"); failures++; }
      }
    }
    ui_end_build();
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

internal B32
uishell_scroll_region_diagnostics(RD_WindowState *ws)
{
  U32 failures = 0;
#define ScrollCheck(condition, name) do { if(!(condition)) { fprintf(stderr, "FAIL: %s\n", name); failures += 1; } } while(0)
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
      uishell_scroll_test_frame(ws, &region, axes, press, event, &content);
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
  failures += !uishell_scroll_preview_diagnostics(ws);
  fprintf(stderr, "Scroll region diagnostics: %u failures\n", failures);
#undef ScrollCheck
  return failures == 0;
}
