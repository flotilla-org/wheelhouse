// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Helpers

internal LNX_WM_Window *
lnx_window_from_x11window(Window window)
{
  LNX_WM_Window *result = 0;
  for(LNX_WM_Window *w = lnx_wm_state->first_window; w != 0; w = w->next)
  {
    if(w->window == window)
    {
      result = w;
      break;
    }
  }
  return result;
}

// The shell asks for these states every frame, so they are cached and
// re-read only when the window manager changes the property.
internal void
lnx_wm_window_read_state(LNX_WM_Window *w, Atom property)
{
  Atom actual_type = 0;
  int actual_format = 0;
  unsigned long count = 0;
  unsigned long bytes_after = 0;
  if(property == lnx_wm_state->net_wm_state_atom)
  {
    B32 horz = 0, vert = 0;
    w->fullscreen = 0;
    Atom *props = 0;
    if(XGetWindowProperty(lnx_wm_state->display, w->window, property, 0, 1024, 0, XA_ATOM,
                          &actual_type, &actual_format, &count, &bytes_after, (U8 **)&props) == Success && props != 0)
    {
      for(unsigned long idx = 0; idx < count; idx += 1)
      {
        horz |= props[idx] == lnx_wm_state->net_wm_state_maximized_horz_atom;
        vert |= props[idx] == lnx_wm_state->net_wm_state_maximized_vert_atom;
        w->fullscreen |= props[idx] == lnx_wm_state->net_wm_state_fullscreen_atom;
      }
      XFree(props);
    }
    w->maximized = horz && vert;
  }
  else if(property == lnx_wm_state->wm_state_atom)
  {
    w->minimized = 0;
    long *state = 0;
    if(XGetWindowProperty(lnx_wm_state->display, w->window, property, 0, 2, 0, property,
                          &actual_type, &actual_format, &count, &bytes_after, (U8 **)&state) == Success && state != 0)
    {
      w->minimized = (count > 0 && state[0] == IconicState);
      XFree(state);
    }
  }
}

internal void
lnx_wm_window_send_net_wm_state(LNX_WM_Window *w, B32 set, Atom first, Atom second)
{
  XEvent evt = {0};
  evt.xclient.type = ClientMessage;
  evt.xclient.window = w->window;
  evt.xclient.message_type = lnx_wm_state->net_wm_state_atom;
  evt.xclient.format = 32;
  evt.xclient.data.l[0] = !!set;
  evt.xclient.data.l[1] = first;
  evt.xclient.data.l[2] = second;
  evt.xclient.data.l[3] = 1;
  XSendEvent(lnx_wm_state->display, DefaultRootWindow(lnx_wm_state->display), 0,
             SubstructureNotifyMask|SubstructureRedirectMask, &evt);
  XFlush(lnx_wm_state->display);
}

// The window manager draws no frame around a custom-border window, so the
// edges and title bar the shell reports stand in for it: a press there hands
// the drag to the window manager with _NET_WM_MOVERESIZE. -1 is client area.
internal int
lnx_wm_moveresize_code_from_pos(LNX_WM_Window *w, Vec2F32 pos)
{
  if(!w->custom_border) { return -1; }
  WM_Window handle = {(U64)w};
  Rng2F32 window_rect = wm_client_rect_from_window(handle);
  F32 edge = w->custom_edge_thickness;
  Rng2F32 inside_edges = pad_2f32(window_rect, -edge);
  Rng2F32 title_bar = r2f32p(inside_edges.x0, inside_edges.y0, inside_edges.x1, inside_edges.y0 + w->custom_title_bar_thickness);
  if(!contains_2f32(window_rect, pos)) { return -1; }
  if(contains_2f32(inside_edges, pos) && !contains_2f32(title_bar, pos)) { return -1; }
  for(LNX_WM_WindowClientArea *n = w->first_client_area; n != 0; n = n->next)
  {
    if(contains_2f32(n->rect, pos)) { return -1; }
  }
  if(contains_2f32(title_bar, pos)) { return _NET_WM_MOVERESIZE_MOVE; }
  // A maximized window has no edges to drag; they act as title bar.
  if(wm_window_is_maximized(handle)) { return _NET_WM_MOVERESIZE_MOVE; }
  B32 left = pos.x < inside_edges.x0, right = pos.x >= inside_edges.x1;
  B32 top = pos.y < inside_edges.y0, bottom = pos.y >= inside_edges.y1;
  if(top && left)     { return _NET_WM_MOVERESIZE_SIZE_TOPLEFT; }
  if(top && right)    { return _NET_WM_MOVERESIZE_SIZE_TOPRIGHT; }
  if(bottom && left)  { return _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT; }
  if(bottom && right) { return _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT; }
  if(left)            { return _NET_WM_MOVERESIZE_SIZE_LEFT; }
  if(right)           { return _NET_WM_MOVERESIZE_SIZE_RIGHT; }
  if(top)             { return _NET_WM_MOVERESIZE_SIZE_TOP; }
  return _NET_WM_MOVERESIZE_SIZE_BOTTOM;
}

// _NET_WM_SYNC_REQUEST: the window manager holds the next step of an
// interactive resize until the counter reaches the value it sent, so the
// counter moves only once a frame at the new size has been swapped.
internal void
lnx_wm_window_presented(LNX_WM_Window *w)
{
  if(w->sync_pending)
  {
    w->sync_pending = 0;
    XSyncValue value;
    XSyncIntsToValue(&value, (unsigned int)(w->counter_value & 0xffffffffu), (int)(w->counter_value >> 32));
    XSyncSetCounter(lnx_wm_state->display, w->counter_xid, value);
    XFlush(lnx_wm_state->display);
  }
}

////////////////////////////////
//~ rjf: @os_hooks Main Initialization API (Implemented Per-OS)

internal void
wm_init(void)
{
  //- rjf: initialize basics
  Arena *arena = arena_alloc();
  lnx_wm_state = push_array(arena, LNX_WM_State, 1);
  lnx_wm_state->arena = arena;
  lnx_wm_state->display = XOpenDisplay(0);
  if(lnx_wm_state->display == 0)
  {
    char *name = getenv("DISPLAY");
    fprintf(stderr, "Could not open X display \"%s\". Check DISPLAY and XAUTHORITY.\n", name ? name : "");
    exit(1);
  }
  if(pipe2(lnx_wm_state->wake_pipe, O_NONBLOCK|O_CLOEXEC) != 0) { perror("window wake pipe"); abort(); }
  
  //- rjf: calculate atoms
  lnx_wm_state->wm_delete_window_atom        = XInternAtom(lnx_wm_state->display, "WM_DELETE_WINDOW", 0);
  lnx_wm_state->wm_sync_request_atom         = XInternAtom(lnx_wm_state->display, "_NET_WM_SYNC_REQUEST", 0);
  lnx_wm_state->wm_sync_request_counter_atom = XInternAtom(lnx_wm_state->display, "_NET_WM_SYNC_REQUEST_COUNTER", 0);
  lnx_wm_state->motif_wm_hints_atom              = XInternAtom(lnx_wm_state->display, "_MOTIF_WM_HINTS", 0);
  lnx_wm_state->net_wm_moveresize_atom           = XInternAtom(lnx_wm_state->display, "_NET_WM_MOVERESIZE", 0);
  lnx_wm_state->net_wm_state_atom                = XInternAtom(lnx_wm_state->display, "_NET_WM_STATE", 0);
  lnx_wm_state->net_wm_state_maximized_horz_atom = XInternAtom(lnx_wm_state->display, "_NET_WM_STATE_MAXIMIZED_HORZ", 0);
  lnx_wm_state->net_wm_state_maximized_vert_atom = XInternAtom(lnx_wm_state->display, "_NET_WM_STATE_MAXIMIZED_VERT", 0);
  lnx_wm_state->net_wm_state_fullscreen_atom     = XInternAtom(lnx_wm_state->display, "_NET_WM_STATE_FULLSCREEN", 0);
  lnx_wm_state->wm_state_atom                    = XInternAtom(lnx_wm_state->display, "WM_STATE", 0);
  
  lnx_wm_state->clipboard_atom = XInternAtom(lnx_wm_state->display, "CLIPBOARD", 0);
  lnx_wm_state->clipboard_utf8_atom = XInternAtom(lnx_wm_state->display, "UTF8_STRING", 0);
  lnx_wm_state->clipboard_targets_atom = XInternAtom(lnx_wm_state->display, "TARGETS", 0);
  // A private owner survives closing any one of the application's windows.
  lnx_wm_state->clipboard_owner = XCreateSimpleWindow(lnx_wm_state->display,
    DefaultRootWindow(lnx_wm_state->display), 0, 0, 1, 1, 0, 0, 0);

  //- rjf: open im
  lnx_wm_state->xim = XOpenIM(lnx_wm_state->display, 0, 0, 0);
  
  //- rjf: fill out gfx info
  lnx_wm_state->gfx_info.double_click_time = 0.5f;
  lnx_wm_state->gfx_info.caret_blink_time = 0.5f;
  lnx_wm_state->gfx_info.default_refresh_rate = 60.f;
  
  //- rjf: fill out cursors
  {
    struct
    {
      WM_Cursor cursor;
      unsigned int id;
    }
    map[] =
    {
      {WM_Cursor_Pointer,         XC_left_ptr},
      {WM_Cursor_IBar,            XC_xterm},
      {WM_Cursor_LeftRight,       XC_sb_h_double_arrow},
      {WM_Cursor_UpDown,          XC_sb_v_double_arrow},
      {WM_Cursor_DownRight,       XC_bottom_right_corner},
      {WM_Cursor_UpRight,         XC_top_right_corner},
      {WM_Cursor_UpDownLeftRight, XC_fleur},
      {WM_Cursor_HandPoint,       XC_hand1},
      {WM_Cursor_Disabled,        XC_X_cursor},
    };
    for EachElement(idx, map)
    {
      lnx_wm_state->cursors[map[idx].cursor] = XCreateFontCursor(lnx_wm_state->display, map[idx].id);
    }
  }
}

////////////////////////////////
//~ rjf: @os_hooks Graphics System Info (Implemented Per-OS)

internal WM_SystemInfo *
wm_get_system_info(void)
{
  return &lnx_wm_state->gfx_info;
}

////////////////////////////////
//~ rjf: @os_hooks Clipboards (Implemented Per-OS)

internal void
wm_set_clipboard_text_impl(String8 string)
{
  if(lnx_wm_state->clipboard_arena == 0) { lnx_wm_state->clipboard_arena = arena_alloc(); }
  arena_clear(lnx_wm_state->clipboard_arena);
  lnx_wm_state->clipboard_text = push_str8_copy(lnx_wm_state->clipboard_arena, string);
  lnx_wm_state->clipboard_ascii = 1;
  for(U64 i = 0; i < string.size; i++) { lnx_wm_state->clipboard_ascii &= string.str[i] < 128; }
  XSetSelectionOwner(lnx_wm_state->display, lnx_wm_state->clipboard_atom,
    lnx_wm_state->clipboard_owner, CurrentTime);
  lnx_wm_state->clipboard_owned = XGetSelectionOwner(lnx_wm_state->display, lnx_wm_state->clipboard_atom) == lnx_wm_state->clipboard_owner;
  XFlush(lnx_wm_state->display);
}

internal String8
wm_get_clipboard_text_impl(Arena *arena)
{
  String8 result = {0};
  return result;
}

// NOTE: process-local selection buffer; X11 PRIMARY selection integration
// and external clipboard reading are still unimplemented
internal void
wm_set_selection_text(String8 string)
{
  if(lnx_wm_state->selection_arena == 0)
  {
    lnx_wm_state->selection_arena = arena_alloc();
  }
  arena_clear(lnx_wm_state->selection_arena);
  lnx_wm_state->selection_text = push_str8_copy(lnx_wm_state->selection_arena, string);
}

internal String8
wm_get_selection_text(Arena *arena)
{
  return push_str8_copy(arena, lnx_wm_state->selection_text);
}

internal B32
wm_apply_clipboard_write(U32 destination, B32 clear, String8 text)
{
  if(destination > 1 || !lnx_wm_state || !lnx_wm_state->display) { return 0; }
  // Selection is process-local, not X11 PRIMARY. See wheelhouse#150.
  if(destination == 1) { wm_set_selection_text(clear ? str8_zero() : text); return 1; }
  // UI-thread dispatch is serialized with SelectionClear/SelectionRequest
  // handling in wm_get_events; no provider worker touches the clipboard arena.
  if(clear)
  {
    XSetSelectionOwner(lnx_wm_state->display, lnx_wm_state->clipboard_atom, None, CurrentTime);
    lnx_wm_state->clipboard_owned = 0;
    if(lnx_wm_state->clipboard_arena) { arena_clear(lnx_wm_state->clipboard_arena); }
    lnx_wm_state->clipboard_text = str8_zero();
    XFlush(lnx_wm_state->display);
    return 1;
  }
  // Existing X11 adapter serves bounded UTF8_STRING requests without INCR.
  // Reject text it cannot serve before changing clipboard ownership/content.
  U64 max_bytes = (U64)Max(0, XMaxRequestSize(lnx_wm_state->display)-128)*4;
  if(text.size > Min(max_bytes, (U64)KB(64))) { return 0; }
  wm_set_clipboard_text(text);
  return lnx_wm_state->clipboard_owned;
}

////////////////////////////////
//~ rjf: @os_hooks Windows (Implemented Per-OS)

internal WM_Window
wm_window_open(Rng2F32 rect, WM_WindowFlags flags, String8 title)
{
  Vec2F32 resolution = dim_2f32(rect);
  
  //- rjf: allocate window
  LNX_WM_Window *w = lnx_wm_state->free_window;
  if(w)
  {
    SLLStackPop(lnx_wm_state->free_window);
  }
  else
  {
    w = push_array_no_zero(lnx_wm_state->arena, LNX_WM_Window, 1);
  }
  MemoryZeroStruct(w);
  DLLPushBack(lnx_wm_state->first_window, lnx_wm_state->last_window, w);
  
  //- rjf: create window & equip with x11 info
  w->window = XCreateWindow(lnx_wm_state->display,
                            XDefaultRootWindow(lnx_wm_state->display),
                            0, 0, resolution.x, resolution.y,
                            0,
                            CopyFromParent,
                            InputOutput,
                            CopyFromParent,
                            0,
                            0);
  XSelectInput(lnx_wm_state->display, w->window,
               ExposureMask|
               PointerMotionMask|
               ButtonPressMask|
               ButtonReleaseMask|
               KeyPressMask|
               KeyReleaseMask|
               StructureNotifyMask|
               PropertyChangeMask|
               FocusChangeMask);
  Atom protocols[] =
  {
    lnx_wm_state->wm_delete_window_atom,
    lnx_wm_state->wm_sync_request_atom,
  };
  XSetWMProtocols(lnx_wm_state->display, w->window, protocols, ArrayCount(protocols));
  {
    XSyncValue initial_value;
    XSyncIntToValue(&initial_value, 0);
    w->counter_xid = XSyncCreateCounter(lnx_wm_state->display, initial_value);
  }
  XChangeProperty(lnx_wm_state->display, w->window, lnx_wm_state->wm_sync_request_counter_atom, XA_CARDINAL, 32, PropModeReplace, (U8 *)&w->counter_xid, 1);
  w->configured_size = v2s32((S32)resolution.x, (S32)resolution.y);
  
  //- rjf: custom border -> ask the window manager for no decorations
  if(flags & WM_WindowFlag_CustomBorder)
  {
    w->custom_border = 1;
    // MWM_HINTS_DECORATIONS, with no decorations. Format-32 properties are longs in Xlib.
    long hints[5] = {1<<1, 0, 0, 0, 0};
    XChangeProperty(lnx_wm_state->display, w->window, lnx_wm_state->motif_wm_hints_atom, lnx_wm_state->motif_wm_hints_atom,
                    32, PropModeReplace, (U8 *)hints, ArrayCount(hints));
  }
  
  //- rjf: create xic
  w->xic = XCreateIC(lnx_wm_state->xim,
                     XNInputStyle, XIMPreeditNothing|XIMStatusNothing,
                     XNClientWindow, w->window,
                     XNFocusWindow, w->window,
                     NULL);
  
  //- rjf: attach name
  Temp scratch = scratch_begin(0, 0);
  String8 title_copy = push_str8_copy(scratch.arena, title);
  XStoreName(lnx_wm_state->display, w->window, (char *)title_copy.str);
  scratch_end(scratch);
  
  //- rjf: convert to handle & return
  WM_Window handle = {(U64)w};
  return handle;
}

internal void
wm_window_close(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  XDestroyWindow(lnx_wm_state->display, w->window);
}

internal void
wm_window_set_title(WM_Window handle, String8 title)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  Temp scratch = scratch_begin(0, 0);
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  String8 title_copy = push_str8_copy(scratch.arena, title);
  XStoreName(lnx_wm_state->display, w->window, (char *)title_copy.str);
  scratch_end(scratch);
}

internal void
wm_window_first_paint(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  XMapWindow(lnx_wm_state->display, w->window);
}

internal void
wm_window_focus(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  XSetInputFocus(lnx_wm_state->display, w->window, RevertToNone, CurrentTime);
}

internal B32
wm_window_is_focused(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 0;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  Window focused_window = 0;
  int revert_to = 0;
  XGetInputFocus(lnx_wm_state->display, &focused_window, &revert_to);
  B32 result = (w->window == focused_window);
  return result;
}

internal B32
wm_window_is_fullscreen(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 0;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  return w->fullscreen;
}

internal void
wm_window_set_fullscreen(WM_Window handle, B32 fullscreen)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  lnx_wm_window_send_net_wm_state(w, fullscreen, lnx_wm_state->net_wm_state_fullscreen_atom, 0);
}

internal B32
wm_window_is_maximized(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 0;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  return w->maximized;
}

internal void
wm_window_set_maximized(WM_Window handle, B32 maximized)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  lnx_wm_window_send_net_wm_state(w, maximized, lnx_wm_state->net_wm_state_maximized_horz_atom, lnx_wm_state->net_wm_state_maximized_vert_atom);
}

internal B32
wm_window_is_minimized(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 0;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  return w->minimized;
}

internal void
wm_window_set_minimized(WM_Window handle, B32 minimized)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  if(minimized) { XIconifyWindow(lnx_wm_state->display, w->window, DefaultScreen(lnx_wm_state->display)); }
  else          { XMapWindow(lnx_wm_state->display, w->window); }
  XFlush(lnx_wm_state->display);
}

internal void
wm_window_bring_to_front(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  // TODO(rjf)
}

internal void
wm_window_set_monitor(WM_Window handle, WM_Monitor monitor)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  // TODO(rjf)
}

internal void
wm_window_clear_custom_border_data(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  if(w->last_client_area != 0)
  {
    w->last_client_area->next = lnx_wm_state->free_client_area;
    lnx_wm_state->free_client_area = w->first_client_area;
  }
  w->first_client_area = w->last_client_area = 0;
  w->custom_title_bar_thickness = 0;
  w->custom_edge_thickness = 0;
}

internal void
wm_window_push_custom_title_bar(WM_Window handle, F32 thickness)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  w->custom_title_bar_thickness = thickness;
}

internal void
wm_window_push_custom_edges(WM_Window handle, F32 thickness)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  w->custom_edge_thickness = thickness;
}

internal void
wm_window_push_custom_title_bar_client_area(WM_Window handle, Rng2F32 rect)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  LNX_WM_WindowClientArea *n = lnx_wm_state->free_client_area;
  if(n != 0) { SLLStackPop(lnx_wm_state->free_client_area); }
  else       { n = push_array_no_zero(lnx_wm_state->arena, LNX_WM_WindowClientArea, 1); }
  n->next = 0;
  n->rect = rect;
  SLLQueuePush(w->first_client_area, w->last_client_area, n);
}

internal B32
wm_window_should_draw_custom_title_bar_controls(WM_Window window)
{
  return 1;
}

internal F32
wm_window_native_title_bar_left_padding(WM_Window window)
{
  return 0;
}

internal Rng2F32
wm_rect_from_window(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return r2f32p(0, 0, 0, 0);}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  XWindowAttributes atts = {0};
  Status s = XGetWindowAttributes(lnx_wm_state->display, w->window, &atts);
  Rng2F32 result = r2f32p((F32)atts.x, (F32)atts.y, (F32)atts.x + (F32)atts.width, (F32)atts.y + (F32)atts.height);
  return result;
}

internal Rng2F32
wm_client_rect_from_window(WM_Window handle)
{
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  XWindowAttributes atts = {0};
  Status s = XGetWindowAttributes(lnx_wm_state->display, w->window, &atts);
  Rng2F32 result = r2f32p(0, 0, (F32)atts.width, (F32)atts.height);
  return result;
}

internal F32
wm_dpi_from_window(WM_Window handle)
{
  // TODO(rjf)
  return 96.f;
}

internal F32
wm_layout_scale_from_window(WM_Window handle)
{
  F32 result = wm_dpi_from_window(handle)/96.f;
  result = ClampBot(1.f/16.f, result);
  return result;
}

internal F32
wm_backing_scale_from_window(WM_Window handle)
{
  return 1.f;
}

////////////////////////////////
//~ rjf: @os_hooks External Windows (Implemented Per-OS)

internal WM_ExtWindow
wm_focused_external_window(void)
{
  WM_ExtWindow result = {0};
  // TODO(rjf)
  return result;
}

internal void
wm_focus_external_window(WM_ExtWindow handle)
{
  // TODO(rjf)
}

////////////////////////////////
//~ rjf: @os_hooks Monitors (Implemented Per-OS)

internal WM_MonitorArray
wm_push_monitors_array(Arena *arena)
{
  WM_MonitorArray result = {0};
  // TODO(rjf)
  return result;
}

internal WM_Monitor
wm_primary_monitor(void)
{
  WM_Monitor result = {0};
  // TODO(rjf)
  return result;
}

internal WM_Monitor
wm_monitor_from_window(WM_Window window)
{
  WM_Monitor result = {0};
  // TODO(rjf)
  return result;
}

internal String8
wm_name_from_monitor(Arena *arena, WM_Monitor monitor)
{
  // TODO(rjf)
  return str8_zero();
}

internal Vec2F32
wm_dim_from_monitor(WM_Monitor monitor)
{
  // TODO(rjf)
  return v2f32(0, 0);
}

internal F32
wm_dpi_from_monitor(WM_Monitor monitor)
{
  // TODO(rjf)
  return 96.f;
}

////////////////////////////////
//~ rjf: @os_hooks Events (Implemented Per-OS)

internal void
wm_send_wakeup_event(void)
{
  // A full pipe already guarantees a wakeup. No Xlib calls from worker threads.
  if(lnx_wm_state != 0)
  {
    char byte = 1;
    ssize_t result;
    do { result = write(lnx_wm_state->wake_pipe[1], &byte, 1); } while(result < 0 && errno == EINTR);
  }
}

internal WM_EventList
wm_get_events(Arena *arena, B32 wait)
{
  WM_EventList evts = {0};
  U64 timeout_us = wait ? wm_take_wait_timeout_us() : 0;
  U64 until_us = timeout_us == max_U64 ? max_U64 : now_time_us() + timeout_us;
  for(;XPending(lnx_wm_state->display) > 0 || (wait && evts.count == 0);)
  {
    if(XPending(lnx_wm_state->display) == 0)
    {
      struct pollfd fds[] = {{ConnectionNumber(lnx_wm_state->display), POLLIN, 0}, {lnx_wm_state->wake_pipe[0], POLLIN, 0}};
      int result;
      do
      {
        int timeout_ms = -1;
        if(until_us != max_U64)
        {
          U64 now = now_time_us();
          timeout_ms = until_us > now ? (int)Min((until_us - now + 999)/1000, (U64)max_S32) : 0;
        }
        result = poll(fds, 2, timeout_ms);
      } while(result < 0 && errno == EINTR);
      if(result <= 0) { break; }
      if(fds[1].revents & POLLIN)
      {
        char bytes[128];
        while(read(lnx_wm_state->wake_pipe[0], bytes, sizeof(bytes)) > 0) {}
        break;
      }
      if(XPending(lnx_wm_state->display) == 0) { continue; }
    }
    XEvent evt = {0};
    XNextEvent(lnx_wm_state->display, &evt);
    B32 set_mouse_cursor = 0;
    switch(evt.type)
    {
      default:{}break;
      
      case SelectionClear:
      {
        if(evt.xselectionclear.selection == lnx_wm_state->clipboard_atom)
        {
          lnx_wm_state->clipboard_owned = 0;
          if(lnx_wm_state->clipboard_arena) { arena_clear(lnx_wm_state->clipboard_arena); }
          lnx_wm_state->clipboard_text = str8_zero();
        }
      }break;

      // Serve small copied references directly. Large transfers require INCR,
      // so reject them rather than overflowing the server's request limit.
      // Handled before window-table lookup: clipboard_owner is a private window.
      // XMaxRequestSize is in four-byte units; reserve 128 bytes for request overhead.
      case SelectionRequest:
      {
        XSelectionRequestEvent *request = &evt.xselectionrequest;
        XEvent response = {0};
        response.xselection.type = SelectionNotify;
        response.xselection.display = request->display;
        response.xselection.requestor = request->requestor;
        response.xselection.selection = request->selection;
        response.xselection.target = request->target;
        response.xselection.time = request->time;
        response.xselection.property = None;
        Atom property = request->property != None ? request->property : request->target;
        String8 text = lnx_wm_state->clipboard_text;
        B32 ascii = lnx_wm_state->clipboard_ascii;
        if(request->selection == lnx_wm_state->clipboard_atom &&
           request->owner == lnx_wm_state->clipboard_owner && lnx_wm_state->clipboard_owned)
        {
          if(request->target == lnx_wm_state->clipboard_targets_atom)
          {
            Atom targets[] = {lnx_wm_state->clipboard_targets_atom, lnx_wm_state->clipboard_utf8_atom, XA_STRING};
            XChangeProperty(lnx_wm_state->display, request->requestor, property,
              XA_ATOM, 32, PropModeReplace, (U8 *)targets, ascii ? 3 : 2);
            response.xselection.property = property;
          }
          else if((request->target == lnx_wm_state->clipboard_utf8_atom || (ascii && request->target == XA_STRING)) &&
                  text.size < (U64)XMaxRequestSize(lnx_wm_state->display)*4-128)
          {
            XChangeProperty(lnx_wm_state->display, request->requestor, property,
              request->target, 8, PropModeReplace, text.str, (int)text.size);
            response.xselection.property = property;
          }
        }
        XSendEvent(lnx_wm_state->display, request->requestor, False, 0, &response);
        XFlush(lnx_wm_state->display);
      }break;

      //- rjf: key presses/releases
      case KeyPress:
      case KeyRelease:
      {
        // rjf: determine flags
        WM_Modifiers modifiers = 0;
        if(evt.xkey.state & ShiftMask)   { modifiers |= WM_Modifier_Shift; }
        if(evt.xkey.state & ControlMask) { modifiers |= WM_Modifier_Ctrl; }
        if(evt.xkey.state & Mod1Mask)    { modifiers |= WM_Modifier_Alt; }
        if(evt.xkey.state & Mod4Mask)    { modifiers |= WM_Modifier_Super; }
        
        // rjf: map keycode -> keysym & codepoint
        LNX_WM_Window *window = lnx_window_from_x11window(evt.xkey.window);
        KeySym keysym = 0;
        U8 text[256] = {0};
        U64 text_size = Xutf8LookupString(window->xic, &evt.xkey, (char *)text, sizeof(text), &keysym, 0);
        
        // rjf: map keysym -> WM_Key
        B32 is_right_sided = 0;
        WM_Key key = WM_Key_Null;
        switch(keysym)
        {
          default:
          {
            if(0){}
            else if(XK_F1 <= keysym && keysym <= XK_F24) { key = (WM_Key)(WM_Key_F1 + (keysym - XK_F1)); }
            else if('0' <= keysym && keysym <= '9')      { key = WM_Key_0 + (keysym-'0'); }
          }break;
          case XK_Escape:{key = WM_Key_Esc;};break;
          case XK_BackSpace:{key = WM_Key_Backspace;}break;
          case XK_Delete:{key = WM_Key_Delete;}break;
          case XK_Return:{key = WM_Key_Return;}break;
          case XK_Pause:{key = WM_Key_Pause;}break;
          case XK_Tab:{key = WM_Key_Tab;}break;
          case XK_Left:{key = WM_Key_Left;}break;
          case XK_Right:{key = WM_Key_Right;}break;
          case XK_Up:{key = WM_Key_Up;}break;
          case XK_Down:{key = WM_Key_Down;}break;
          case XK_Home:{key = WM_Key_Home;}break;
          case XK_End:{key = WM_Key_End;}break;
          case XK_Page_Up:{key = WM_Key_PageUp;}break;
          case XK_Page_Down:{key = WM_Key_PageDown;}break;
          case XK_Alt_L:{ key = WM_Key_Alt; }break;
          case XK_Alt_R:{ key = WM_Key_Alt; is_right_sided = 1;}break;
          case XK_Shift_L:{ key = WM_Key_Shift; }break;
          case XK_Shift_R:{ key = WM_Key_Shift; is_right_sided = 1;}break;
          case XK_Control_L:{ key = WM_Key_Ctrl; }break;
          case XK_Control_R:{ key = WM_Key_Ctrl; is_right_sided = 1;}break;
          case '-':{key = WM_Key_Minus;}break;
          case '=':{key = WM_Key_Equal;}break;
          case '[':{key = WM_Key_LeftBracket;}break;
          case ']':{key = WM_Key_RightBracket;}break;
          case ';':{key = WM_Key_Semicolon;}break;
          case '\'':{key = WM_Key_Quote;}break;
          case '.':{key = WM_Key_Period;}break;
          case ',':{key = WM_Key_Comma;}break;
          case '/':{key = WM_Key_Slash;}break;
          case '\\':{key = WM_Key_BackSlash;}break;
          case '\t':{key = WM_Key_Tab;}break;
          case 'a':case 'A':{key = WM_Key_A;}break;
          case 'b':case 'B':{key = WM_Key_B;}break;
          case 'c':case 'C':{key = WM_Key_C;}break;
          case 'd':case 'D':{key = WM_Key_D;}break;
          case 'e':case 'E':{key = WM_Key_E;}break;
          case 'f':case 'F':{key = WM_Key_F;}break;
          case 'g':case 'G':{key = WM_Key_G;}break;
          case 'h':case 'H':{key = WM_Key_H;}break;
          case 'i':case 'I':{key = WM_Key_I;}break;
          case 'j':case 'J':{key = WM_Key_J;}break;
          case 'k':case 'K':{key = WM_Key_K;}break;
          case 'l':case 'L':{key = WM_Key_L;}break;
          case 'm':case 'M':{key = WM_Key_M;}break;
          case 'n':case 'N':{key = WM_Key_N;}break;
          case 'o':case 'O':{key = WM_Key_O;}break;
          case 'p':case 'P':{key = WM_Key_P;}break;
          case 'q':case 'Q':{key = WM_Key_Q;}break;
          case 'r':case 'R':{key = WM_Key_R;}break;
          case 's':case 'S':{key = WM_Key_S;}break;
          case 't':case 'T':{key = WM_Key_T;}break;
          case 'u':case 'U':{key = WM_Key_U;}break;
          case 'v':case 'V':{key = WM_Key_V;}break;
          case 'w':case 'W':{key = WM_Key_W;}break;
          case 'x':case 'X':{key = WM_Key_X;}break;
          case 'y':case 'Y':{key = WM_Key_Y;}break;
          case 'z':case 'Z':{key = WM_Key_Z;}break;
          case ' ':{key = WM_Key_Space;}break;
        }
        
        // rjf: push key event
        {
          WM_Event *e = wm_event_list_push_new(arena, &evts, evt.type == KeyPress ? WM_EventKind_Press : WM_EventKind_Release);
          e->window.u64[0] = (U64)window;
          e->modifiers = modifiers;
          e->key = key;
          e->right_sided = is_right_sided;
        }
        // rjf: push text event
        if(evt.type == KeyPress && text_size != 0)
        {
          for(U64 off = 0; off < text_size;)
          {
            UnicodeDecode decode = utf8_decode(text+off, text_size-off);
            if(decode.codepoint != 0 && (decode.codepoint >= 32 || decode.codepoint == '\t'))
            {
              WM_Event *e = wm_event_list_push_new(arena, &evts, WM_EventKind_Text);
              e->window.u64[0] = (U64)window;
              e->character = decode.codepoint;
              e->source_key = key;
              e->modifiers = modifiers;
            }
            if(decode.inc == 0)
            {
              break;
            }
            off += decode.inc;
          }
        }
        
      }break;
      
      //- rjf: mouse button presses/releases
      case ButtonPress:
      case ButtonRelease:
      {
        // rjf: determine flags
        WM_Modifiers modifiers = 0;
        if(evt.xbutton.state & ShiftMask)   { modifiers |= WM_Modifier_Shift; }
        if(evt.xbutton.state & ControlMask) { modifiers |= WM_Modifier_Ctrl; }
        if(evt.xbutton.state & Mod1Mask)    { modifiers |= WM_Modifier_Alt; }
        if(evt.xbutton.state & Mod4Mask)    { modifiers |= WM_Modifier_Super; }
        
        // rjf: map button -> WM_Key
        WM_Key key = WM_Key_Null;
        switch(evt.xbutton.button)
        {
          default:{}break;
          case Button1:{key = WM_Key_LeftMouseButton;}break;
          case Button2:{key = WM_Key_MiddleMouseButton;}break;
          case Button3:{key = WM_Key_RightMouseButton;}break;
        }
        
        // rjf: left press on the title bar or an edge -> the window manager moves or resizes
        LNX_WM_Window *window = lnx_window_from_x11window(evt.xbutton.window);
        if(window != 0 && evt.type == ButtonPress && evt.xbutton.button == Button1)
        {
          int moveresize_code = lnx_wm_moveresize_code_from_pos(window, v2f32((F32)evt.xbutton.x, (F32)evt.xbutton.y));
          if(moveresize_code == _NET_WM_MOVERESIZE_MOVE &&
             evt.xbutton.time - window->last_title_bar_press_time < (Time)(lnx_wm_state->gfx_info.double_click_time*1000))
          {
            window->last_title_bar_press_time = 0;
            WM_Window handle = {(U64)window};
            wm_window_set_maximized(handle, !wm_window_is_maximized(handle));
            break;
          }
          if(moveresize_code >= 0)
          {
            if(moveresize_code == _NET_WM_MOVERESIZE_MOVE) { window->last_title_bar_press_time = evt.xbutton.time; }
            // The press holds an implicit pointer grab, which the window manager needs.
            XUngrabPointer(lnx_wm_state->display, evt.xbutton.time);
            XEvent xev = {0};
            xev.xclient.type = ClientMessage;
            xev.xclient.window = window->window;
            xev.xclient.message_type = lnx_wm_state->net_wm_moveresize_atom;
            xev.xclient.format = 32;
            xev.xclient.data.l[0] = evt.xbutton.x_root;
            xev.xclient.data.l[1] = evt.xbutton.y_root;
            xev.xclient.data.l[2] = moveresize_code;
            xev.xclient.data.l[3] = Button1;
            xev.xclient.data.l[4] = 1; // source: application
            XSendEvent(lnx_wm_state->display, DefaultRootWindow(lnx_wm_state->display), 0,
                       SubstructureRedirectMask|SubstructureNotifyMask, &xev);
            XFlush(lnx_wm_state->display);
            break;
          }
        }
        
        // rjf: push event
        if(key != WM_Key_Null)
        {
          WM_Event *e = wm_event_list_push_new(arena, &evts, evt.type == ButtonPress ? WM_EventKind_Press : WM_EventKind_Release);
          e->window.u64[0] = (U64)window;
          e->modifiers = modifiers;
          e->key = key;
          e->pos = v2f32((F32)evt.xbutton.x, (F32)evt.xbutton.y);
        }
        else if(evt.xbutton.button == Button4 ||
                evt.xbutton.button == Button5)
        {
          WM_Event *e = wm_event_list_push_new(arena, &evts, WM_EventKind_Scroll);
          e->window.u64[0] = (U64)window;
          e->modifiers = modifiers;
          e->delta = v2f32(0, evt.xbutton.button == Button4 ? -1.f : +1.f);
          e->pos = v2f32((F32)evt.xbutton.x, (F32)evt.xbutton.y);
        }
      }break;
      
      //- rjf: mouse motion
      case MotionNotify:
      {
        LNX_WM_Window *window = lnx_window_from_x11window(evt.xclient.window);
        WM_Event *e = wm_event_list_push_new(arena, &evts, WM_EventKind_MouseMove);
        e->window.u64[0] = (U64)window;
        e->pos.x = (F32)evt.xmotion.x;
        e->pos.y = (F32)evt.xmotion.y;
        set_mouse_cursor = 1;
        switch(window != 0 ? lnx_wm_moveresize_code_from_pos(window, e->pos) : -1)
        {
          default:{}break;
          case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
          case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:{lnx_wm_state->last_set_cursor = WM_Cursor_DownRight;}break;
          case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
          case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:{lnx_wm_state->last_set_cursor = WM_Cursor_UpRight;}break;
          case _NET_WM_MOVERESIZE_SIZE_LEFT:
          case _NET_WM_MOVERESIZE_SIZE_RIGHT:{lnx_wm_state->last_set_cursor = WM_Cursor_LeftRight;}break;
          case _NET_WM_MOVERESIZE_SIZE_TOP:
          case _NET_WM_MOVERESIZE_SIZE_BOTTOM:{lnx_wm_state->last_set_cursor = WM_Cursor_UpDown;}break;
        }
      }break;
      
      //- rjf: resizes & exposures -> draw a frame
      // Every ConfigureNotify in the queue is drained before the frame, so a
      // drag's burst of sizes coalesces into one frame at the latest size.
      case ConfigureNotify:
      {
        LNX_WM_Window *window = lnx_window_from_x11window(evt.xconfigure.window);
        Vec2S32 size = v2s32(evt.xconfigure.width, evt.xconfigure.height);
        if(window != 0 && (size.x != window->configured_size.x || size.y != window->configured_size.y))
        {
          window->configured_size = size;
          WM_Event *e = wm_event_list_push_new(arena, &evts, WM_EventKind_Wakeup);
          e->window.u64[0] = (U64)window;
        }
      }break;
      case PropertyNotify:
      {
        LNX_WM_Window *window = lnx_window_from_x11window(evt.xproperty.window);
        if(window != 0) { lnx_wm_window_read_state(window, evt.xproperty.atom); }
      }break;
      case Expose:
      {
        LNX_WM_Window *window = lnx_window_from_x11window(evt.xexpose.window);
        if(window != 0 && evt.xexpose.count == 0)
        {
          WM_Event *e = wm_event_list_push_new(arena, &evts, WM_EventKind_Wakeup);
          e->window.u64[0] = (U64)window;
        }
      }break;
      
      //- rjf: window focus/unfocus
      case FocusIn:
      {
      }break;
      case FocusOut:
      {
        LNX_WM_Window *window = lnx_window_from_x11window(evt.xfocus.window);
        WM_Event *e = wm_event_list_push_new(arena, &evts, WM_EventKind_WindowLoseFocus);
        e->window.u64[0] = (U64)window;
      }break;
      
      //- rjf: client messages
      case ClientMessage:
      {
        if((Atom)evt.xclient.data.l[0] == lnx_wm_state->wm_delete_window_atom)
        {
          LNX_WM_Window *window = lnx_window_from_x11window(evt.xclient.window);
          WM_Event *e = wm_event_list_push_new(arena, &evts, WM_EventKind_WindowClose);
          e->window.u64[0] = (U64)window;
        }
        else if((Atom)evt.xclient.data.l[0] == lnx_wm_state->wm_sync_request_atom)
        {
          LNX_WM_Window *window = lnx_window_from_x11window(evt.xclient.window);
          if(window != 0)
          {
            // Xlib sign-extends the 32-bit halves into longs.
            window->counter_value = (U64)(U32)evt.xclient.data.l[2] | ((U64)(U32)evt.xclient.data.l[3] << 32);
            window->sync_pending = 1;
            WM_Event *e = wm_event_list_push_new(arena, &evts, WM_EventKind_Wakeup);
            e->window.u64[0] = (U64)window;
          }
        }
      }break;
    }
    if(set_mouse_cursor)
    {
      Window root_window = 0;
      Window child_window = 0;
      int root_rel_x = 0;
      int root_rel_y = 0;
      int child_rel_x = 0;
      int child_rel_y = 0;
      unsigned int mask = 0;
      if(XQueryPointer(lnx_wm_state->display, XDefaultRootWindow(lnx_wm_state->display), &root_window, &child_window, &root_rel_x, &root_rel_y, &child_rel_x, &child_rel_y, &mask))
      {
        XDefineCursor(lnx_wm_state->display, root_window, lnx_wm_state->cursors[lnx_wm_state->last_set_cursor]);
        XFlush(lnx_wm_state->display);
      }
    }
  }
  return evts;
}

internal WM_Modifiers
wm_get_modifiers(void)
{
  // TODO(rjf)
  return 0;
}

internal B32
wm_key_is_down(WM_Key key)
{
  // TODO(rjf)
  return 0;
}

internal Vec2F32
wm_mouse_from_window(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return v2f32(0, 0);}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  Vec2F32 result = {0};
  {
    Window root_window = 0;
    Window child_window = 0;
    int root_rel_x = 0;
    int root_rel_y = 0;
    int child_rel_x = 0;
    int child_rel_y = 0;
    unsigned int mask = 0;
    if(XQueryPointer(lnx_wm_state->display, w->window, &root_window, &child_window, &root_rel_x, &root_rel_y, &child_rel_x, &child_rel_y, &mask))
    {
      result.x = child_rel_x;
      result.y = child_rel_y;
    }
  }
  return result;
}

////////////////////////////////
//~ @os_hooks Application Menu (Implemented Per-OS)

internal void
wm_set_main_menu(WM_MenuArray menu_array)
{
}

internal B32
wm_application_menu_bar_is_native(void)
{
  return 0;
}

internal void
wm_set_preferred_window_decorations(B32 enabled)
{
}

internal void
wm_set_preferred_native_menu_bar(B32 enabled)
{
}

////////////////////////////////
//~ rjf: @os_hooks Cursors (Implemented Per-OS)

internal void
wm_set_cursor(WM_Cursor cursor)
{
  lnx_wm_state->last_set_cursor = cursor;
}

////////////////////////////////
//~ rjf: @os_hooks Native User-Facing Graphical Messages (Implemented Per-OS)

internal void
wm_graphical_message(B32 error, String8 title, String8 message)
{
  if(error)
  {
    fprintf(stderr, "[X] ");
  }
  fprintf(stderr, "%.*s\n", str8_varg(title));
  fprintf(stderr, "%.*s\n\n", str8_varg(message));
}

internal String8
wm_graphical_pick_file(Arena *arena, String8 initial_path)
{
  return str8_zero();
}

////////////////////////////////
//~ rjf: @os_hooks Shell Operations

internal void
wm_show_in_filesystem_ui(String8 path)
{
  // TODO(rjf)
}

internal void *
lnx_wm_reap_browser(void *data)
{
  pid_t pid = (pid_t)(intptr_t)data;
  while(waitpid(pid, 0, 0) < 0 && errno == EINTR) {}
  return 0;
}

internal void
wm_open_in_browser(String8 url)
{
  Temp scratch = scratch_begin(0, 0);
  String8 copy = push_str8_copy(scratch.arena, url);
  char *argv[] = {"xdg-open", (char *)copy.str, 0};
  pid_t pid = 0;
  // URI is one argv element, never shell source.
  if(posix_spawnp(&pid, "xdg-open", 0, 0, argv, environ) == 0)
  {
    pthread_t reaper;
    if(pthread_create(&reaper, 0, lnx_wm_reap_browser, (void *)(intptr_t)pid) == 0) { pthread_detach(reaper); }
    else { lnx_wm_reap_browser((void *)(intptr_t)pid); }
  }
  scratch_end(scratch);
}

// No portable query; desktops differ.
internal B32
wm_reduce_motion(void)
{
  return 0;
}
