// Exercise real native presentation admission in a running application. No
// synthetic OS input: the existing mounted Jackstay diagnostics cover routing.
#if OS_MAC
internal B32
uishell_presentation_wait(WM_Window window, R_Handle renderer)
{
  Temp scratch = scratch_begin(0, 0);
  U64 end = now_time_us()+3000000;
  while(!r_window_frame_ready(window, renderer) && now_time_us() < end)
  {
    wm_get_events(scratch.arena, 0);
    r_window_request_frame(window, renderer, 1);
    sleep_ms(1);
  }
  B32 ready = r_window_frame_ready(window, renderer);
  scratch_end(scratch);
  return ready;
}

internal void
uishell_presentation_submit(WM_Window window, R_Handle renderer)
{
  R_PassList passes = {0};
  r_begin_frame();
  r_window_begin_frame(window, renderer);
  r_window_submit(window, renderer, &passes);
  r_window_end_frame(window, renderer);
  r_end_frame();
}
#endif

// Exercise the nested tracking-loop path without synthesizing OS input. The
// real AppKit window changes continuously, and the real resize/display clocks
// must present before the tracking scope ends, not only after mouse release.
internal B32
uishell_live_resize_diagnostics(void)
{
#if OS_MAC
  RD_WindowState *ws=rd_state->first_window_state;
  if(ws == &rd_nil_window_state) { return 0; }
  MAC_WM_Window *window=mac_wm_window_from_handle(ws->os);
  R_MTL_Window *renderer=r_mtl_window_from_handle(ws->r);
  if(!renderer->display_link) { return 0; }
  if(r_mtl_state->presentation_log_fd<0)
  { r_mtl_state->presentation_log_fd = open("/dev/null", O_WRONLY); }
  U64 first=r_mtl_state->presentation_log_serial;
  U64 depth=rd_state->frame_depth;
  // AppKit normally enters tracking from inside wm_get_events in rd_frame.
  rd_state->frame_depth=1;
  rd_request_frame();
  ws->render_pending = 1;
  NSSize original=window->ns_window.contentView.frame.size;
  __block U32 step=0;
  NSTimer *timer=[NSTimer timerWithTimeInterval:.001 repeats:YES block:^(NSTimer *t) {
    step++;
    [window->ns_window setContentSize:NSMakeSize(700+step%180,500+step%100)];
  }];
  [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
  [window->delegate windowWillStartLiveResize:0];
  U64 end=now_time_us()+750000;
  while(now_time_us()<end)
    [[NSRunLoop mainRunLoop] runMode:NSEventTrackingRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:.01]];
  U64 during=r_mtl_state->presentation_log_serial-first;
  [timer invalidate];
  [window->delegate macLiveResizeStopDisplayLink];
  rd_state->frame_depth=depth;
  [window->ns_window setContentSize:original];
  fprintf(stderr,"live resize: %llu submissions during %u geometry changes: %s\n",during,step,during>=3?"passed":"FAILED");
  return during>=3;
#else
  return 0;
#endif
}


// A built frame is presented, or it leaves no trace. Drive the real loop with
// the display link: a hover-like preview demand switches the visible workspace
// to a new full-window surface, whose first build must not be marked drawn
// unless it reached the screen.
internal B32 update(void);
internal R_Readback uishell_overview_benchmark_readback(Arena *arena, RD_WindowState *ws);
global U64 uishell_skipped_frame_step;
global U64 uishell_skipped_frame_demand_from;
global U64 uishell_skipped_frame_forced;
global U64 uishell_skipped_frame_unpresented;

internal void
uishell_skipped_frame_prepare(RD_WindowState *ws)
{
  uishell_skipped_frame_step += 1;
  rd_request_frame();
  if(uishell_skipped_frame_demand_from && uishell_skipped_frame_step >= uishell_skipped_frame_demand_from)
  {
    // Any recent demand makes every workspace build through a surface.
    rd_workspace_preview_demand_push(ws, max_U64, 240.f);
    // Input that arrived between display callbacks, before this fix.
    if(uishell_skipped_frame_step == uishell_skipped_frame_demand_from && uishell_skipped_frame_forced)
    { ws->render_ready = 0; }
  }
  if(!ws->render_ready) { uishell_skipped_frame_unpresented += 1; }
}

// Pixels brighter than the stage's clear colour.
internal U64
uishell_skipped_frame_lit_pixels(R_Readback image)
{
  U64 lit = 0;
  for(U64 i = 0; i+3 < image.data.size; i += 4)
  {
    U8 *p = image.data.str+i;
    if(p[0] > 8 || p[1] > 8 || p[2] > 8) { lit += 1; }
  }
  return lit;
}

internal B32
uishell_skipped_frame_diagnostics(void)
{
#if OS_MAC
  RD_WindowState *ws = rd_state->first_window_state;
  if(ws == &rd_nil_window_state) { return 0; }
  if(!r_mtl_window_from_handle(ws->r)->display_link)
  { fprintf(stderr, "skipped frames: FAIL no display link\n"); return 0; }
  Temp scratch = scratch_begin(0, 0);
  B32 ok = 1;
  rd_state->frame_replay.prepare_window = uishell_skipped_frame_prepare;
  for(U64 i = 0; i < 30; i += 1) { update(); }
  U64 baseline = uishell_skipped_frame_lit_pixels(uishell_overview_benchmark_readback(scratch.arena, ws));

  // An unpresented build where the demand starts, then presented frames.
  uishell_skipped_frame_forced = 1;
  uishell_skipped_frame_demand_from = uishell_skipped_frame_step+1;
  for(U64 i = 0; i < 10; i += 1) { update(); }
  U64 after = uishell_skipped_frame_lit_pixels(uishell_overview_benchmark_readback(scratch.arena, ws));
  B32 kept = baseline > 0 && after*10 >= baseline*9;
  fprintf(stderr, "skipped frames: stage lit pixels %llu before demand, %llu after an unpresented first surface frame: %s\n",
          baseline, after, kept ? "passed" : "FAILED");
  ok = ok && kept;

  // Wakes far faster than the display: every build must have a drawable.
  uishell_skipped_frame_forced = 0;
  uishell_skipped_frame_unpresented = 0;
  NSTimer *timer = [NSTimer timerWithTimeInterval:.001 repeats:YES block:^(NSTimer *t) { wm_send_wakeup_event(); }];
  [[NSRunLoop mainRunLoop] addTimer:timer forMode:NSRunLoopCommonModes];
  U64 steps = uishell_skipped_frame_step, end = now_time_us()+500000;
  while(now_time_us() < end) { update(); }
  [timer invalidate];
  steps = uishell_skipped_frame_step-steps;
  fprintf(stderr, "skipped frames: %llu of %llu builds under 1 kHz wakes had no drawable: %s\n",
          uishell_skipped_frame_unpresented, steps, uishell_skipped_frame_unpresented == 0 ? "passed" : "FAILED");
  ok = ok && uishell_skipped_frame_unpresented == 0 && steps > 0;
  rd_state->frame_replay.prepare_window = 0;
  scratch_end(scratch);
  return ok;
#else
  fprintf(stderr, "skipped frame diagnostics require macOS\n");
  return 0;
#endif
}

internal B32
uishell_presentation_diagnostics(void)
{
#if OS_MAC
  B32 ok = 1;
#define PresentationCheck(test, text) do { if(!(test)) { fprintf(stderr, "presentation: FAIL %s\n", text); ok = 0; } } while(0)
  Temp scratch = scratch_begin(0, 0);
  for(RD_WindowState *w = rd_state->first_window_state; w != &rd_nil_window_state; w = w->order_next)
  { r_window_request_frame(w->os, w->r, 0); }
  WM_Window windows[2] = {wm_window_open(r2f32p(60, 80, 400, 300), 0, str8_lit("Presentation A")),
                          wm_window_open(r2f32p(500, 80, 400, 300), 0, str8_lit("Presentation B"))};
  R_Handle renderers[2] = {r_window_equip(windows[0]), r_window_equip(windows[1])};
  for(U64 i = 0; i < 2; i += 1)
  {
    PresentationCheck(!r_window_frame_ready(windows[i], renderers[i]), "first frame waits for display admission");
    wm_window_first_paint(windows[i]);
    R_MTL_Window *native = r_mtl_window_from_handle(renderers[i]);
    PresentationCheck(native->display_link != 0, "native display link exists");
    r_window_request_frame(windows[i], renderers[i], 0);
    PresentationCheck(!r_window_frame_ready(windows[i], renderers[i]), "cancel leaves no drawable");
    PresentationCheck(native->display_link.paused, "idle link is paused");
    r_window_request_frame(windows[i], renderers[i], 1);
  }
  for(U64 i = 0; i < 2; i += 1)
  {
    PresentationCheck(uishell_presentation_wait(windows[i], renderers[i]), "both windows independently become ready");
    uishell_presentation_submit(windows[i], renderers[i]);
    PresentationCheck(!r_window_frame_ready(windows[i], renderers[i]), "submit consumes the opportunity");
    r_window_request_frame(windows[i], renderers[i], 0);
  }
  // The same pump can return input/content wakes while no drawable is ready.
  wm_get_events(scratch.arena, 0);
  wm_send_wakeup_event();
  WM_EventList events = wm_get_events(scratch.arena, 1);
  PresentationCheck(events.count > 0, "ordinary wake does not wait for presentation");
  PresentationCheck(!events.frame_ready, "paused links do not generate display wakes");

  MAC_WM_Window *window = mac_wm_window_from_handle(windows[0]);
  [window->ns_window setContentSize:NSMakeSize(460, 320)];
  r_window_request_frame(windows[0], renderers[0], 1);
  PresentationCheck(uishell_presentation_wait(windows[0], renderers[0]), "resize produces a new opportunity");
  R_MTL_Window *native = r_mtl_window_from_handle(renderers[0]);
  PresentationCheck(native->ready_drawable.texture.width == (NSUInteger)native->drawable_size.x &&
                    native->ready_drawable.texture.height == (NSUInteger)native->drawable_size.y,
                    "drawable geometry follows resize");
  uishell_presentation_submit(windows[0], renderers[0]);

  WM_MonitorArray monitors = wm_push_monitors_array(scratch.arena);
  for(U64 i = 0; i < monitors.count; i += 1)
  {
    wm_window_set_monitor(windows[0], monitors.v[i]);
    r_window_request_frame(windows[0], renderers[0], 1);
    PresentationCheck(uishell_presentation_wait(windows[0], renderers[0]), "display move resumes presentation");
    PresentationCheck(native->display_hz == [[window->ns_window screen] maximumFramesPerSecond], "display rate follows window");
    PresentationCheck(native->ready_drawable.texture.width == (NSUInteger)native->drawable_size.x &&
                      native->ready_drawable.texture.height == (NSUInteger)native->drawable_size.y,
                      "drawable follows backing scale");
    uishell_presentation_submit(windows[0], renderers[0]);
  }
  wm_window_set_minimized(windows[0], 1);
  // Miniaturization is an asynchronous AppKit/Dock transition.
  U64 minimize_deadline = now_time_us()+3000000;
  while(!wm_window_is_minimized(windows[0]) && now_time_us() < minimize_deadline)
  { wm_get_events(scratch.arena, 0); sleep_ms(1); }
  PresentationCheck(wm_window_is_minimized(windows[0]), "native minimization completed");
  r_window_request_frame(windows[0], renderers[0], 1);
  PresentationCheck(!r_window_frame_ready(windows[0], renderers[0]) && native->display_link.paused,
                    "minimized window pauses without admitting a frame");
  wm_window_set_minimized(windows[0], 0);
  r_window_request_frame(windows[0], renderers[0], 1);
  PresentationCheck(uishell_presentation_wait(windows[0], renderers[0]), "restored window becomes ready");
  for(U64 i = 0; i < 2; i += 1)
  {
    // Close with an outstanding request/drawable; callbacks must be invalidated
    // before the native renderer's window allocation can be reused.
    r_window_unequip(windows[i], renderers[i]);
    wm_window_close(windows[i]);
  }
  wm_get_events(scratch.arena, 0);
  scratch_end(scratch);
#undef PresentationCheck
  fprintf(stderr, "presentation diagnostics %s\n", ok ? "passed" : "failed");
  return ok;
#else
  fprintf(stderr, "presentation diagnostics require macOS\n");
  return 0;
#endif
}
