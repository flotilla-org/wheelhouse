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
