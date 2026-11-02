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

internal int
lnx_wm_moveresize_code_from_pos(LNX_WM_Window *w, Vec2F32 pos)
{
  if(!w->custom_border) { return -1; }
  WM_Window handle = {(U64)w};
  B32 in_client_area = 0;
  for(LNX_WM_WindowClientArea *n = w->first_client_area; n != 0; n = n->next)
  {
    in_client_area |= contains_2f32(n->rect, pos);
  }
  return lnx_wm_moveresize_code_from_border(wm_client_rect_from_window(handle), w->custom_edge_thickness, w->custom_title_bar_thickness,
                                            wm_window_is_maximized(handle), in_client_area, pos);
}

internal WM_Modifiers
lnx_wm_modifiers_from_state(unsigned int state)
{
  WM_Modifiers modifiers = 0;
  if(state & ShiftMask)   { modifiers |= WM_Modifier_Shift; }
  if(state & ControlMask) { modifiers |= WM_Modifier_Ctrl; }
  if(state & Mod1Mask)    { modifiers |= WM_Modifier_Alt; }
  if(state & Mod4Mask)    { modifiers |= WM_Modifier_Super; }
  return modifiers;
}

internal void
lnx_wm_set_key_down(WM_Key key, B32 down)
{
  if(key == WM_Key_Null) { return; }
  if(down) { lnx_wm_state->key_down[key/64] |= (1ull << (key%64)); }
  else     { lnx_wm_state->key_down[key/64] &= ~(1ull << (key%64)); }
}

// XStoreName is Latin-1; window managers read _NET_WM_NAME as UTF-8.
internal void
lnx_wm_window_set_title(LNX_WM_Window *w, String8 title)
{
  Temp scratch = scratch_begin(0, 0);
  String8 title_copy = push_str8_copy(scratch.arena, title);
  XStoreName(lnx_wm_state->display, w->window, (char *)title_copy.str);
  XChangeProperty(lnx_wm_state->display, w->window, lnx_wm_state->net_wm_name_atom, lnx_wm_state->clipboard_utf8_atom,
                  8, PropModeReplace, title_copy.str, (int)title_copy.size);
  XChangeProperty(lnx_wm_state->display, w->window, lnx_wm_state->net_wm_icon_name_atom, lnx_wm_state->clipboard_utf8_atom,
                  8, PropModeReplace, title_copy.str, (int)title_copy.size);
  scratch_end(scratch);
}

internal void
lnx_wm_window_apply_icon(LNX_WM_Window *w)
{
  if(lnx_wm_state->icon_data_count != 0)
  {
    XChangeProperty(lnx_wm_state->display, w->window, lnx_wm_state->net_wm_icon_atom, XA_CARDINAL, 32, PropModeReplace,
                    (U8 *)lnx_wm_state->icon_data, (int)lnx_wm_state->icon_data_count);
  }
}

// The refresh rate of the primary output, or of the first lit one.
internal F32
lnx_wm_refresh_rate(void)
{
  F32 result = 0;
  Display *dpy = lnx_wm_state->display;
  Window root = DefaultRootWindow(dpy);
  // Without RandR the request is an error that Xlib's default handler exits on.
  int event_base = 0, error_base = 0;
  if(!XRRQueryExtension(dpy, &event_base, &error_base)) { return 0; }
  XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
  if(res != 0)
  {
    RROutput primary = XRRGetOutputPrimary(dpy, root);
    for(int idx = 0; idx < res->noutput; idx += 1)
    {
      XRROutputInfo *output = XRRGetOutputInfo(dpy, res, res->outputs[idx]);
      if(output == 0) { continue; }
      if(output->connection == RR_Connected && output->crtc != None && (result == 0 || res->outputs[idx] == primary))
      {
        XRRCrtcInfo *crtc = XRRGetCrtcInfo(dpy, res, output->crtc);
        for(int mode_idx = 0; crtc != 0 && mode_idx < res->nmode; mode_idx += 1)
        {
          XRRModeInfo *mode = &res->modes[mode_idx];
          if(mode->id == crtc->mode && mode->hTotal != 0 && mode->vTotal != 0)
          {
            result = (F32)((F64)mode->dotClock / ((F64)mode->hTotal * (F64)mode->vTotal));
          }
        }
        if(crtc != 0) { XRRFreeCrtcInfo(crtc); }
      }
      XRRFreeOutputInfo(output);
    }
    XRRFreeScreenResources(res);
  }
  return result;
}

typedef struct LNX_WM_PropertyMatch LNX_WM_PropertyMatch;
struct LNX_WM_PropertyMatch
{
  int type;
  Window window;
  Atom atom;
};

internal Bool
lnx_wm_event_matches(Display *display, XEvent *evt, XPointer user)
{
  LNX_WM_PropertyMatch *m = (LNX_WM_PropertyMatch *)user;
  if(evt->type != m->type || evt->xany.window != m->window) { return False; }
  if(m->type == PropertyNotify) { return evt->xproperty.atom == m->atom && evt->xproperty.state == PropertyNewValue; }
  return True;
}

// Waits for one event the clipboard read needs. Other events stay queued for wm_get_events.
internal B32
lnx_wm_wait_for_event(LNX_WM_PropertyMatch match, XEvent *out, U64 timeout_us)
{
  U64 deadline_us = now_time_us() + timeout_us;
  for(;;)
  {
    if(XCheckIfEvent(lnx_wm_state->display, out, lnx_wm_event_matches, (XPointer)&match)) { return 1; }
    U64 now = now_time_us();
    if(now >= deadline_us) { return 0; }
    struct pollfd fd = {ConnectionNumber(lnx_wm_state->display), POLLIN, 0};
    poll(&fd, 1, (int)((deadline_us - now + 999)/1000));
  }
}

////////////////////////////////
//~ rjf: @os_hooks Main Initialization API (Implemented Per-OS)

internal B32
wm_init(CmdLine *cmdln)
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
    arena_release(arena);
    lnx_wm_state = 0;
    return 0;
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
  lnx_wm_state->clipboard_html_atom = XInternAtom(lnx_wm_state->display, "text/html", 0);
  lnx_wm_state->clipboard_incr_atom = XInternAtom(lnx_wm_state->display, "INCR", 0);
  lnx_wm_state->clipboard_property_atom = XInternAtom(lnx_wm_state->display, "WHEELHOUSE_CLIPBOARD", 0);
  lnx_wm_state->net_wm_name_atom = XInternAtom(lnx_wm_state->display, "_NET_WM_NAME", 0);
  lnx_wm_state->net_wm_icon_name_atom = XInternAtom(lnx_wm_state->display, "_NET_WM_ICON_NAME", 0);
  lnx_wm_state->net_wm_icon_atom = XInternAtom(lnx_wm_state->display, "_NET_WM_ICON", 0);
  // A private owner survives closing any one of the application's windows.
  lnx_wm_state->clipboard_owner = XCreateSimpleWindow(lnx_wm_state->display,
    DefaultRootWindow(lnx_wm_state->display), 0, 0, 1, 1, 0, 0, 0);
  // Reading a large clipboard (INCR) is driven by property changes on this window.
  XSelectInput(lnx_wm_state->display, lnx_wm_state->clipboard_owner, PropertyChangeMask);

  //- rjf: open im
  lnx_wm_state->xim = XOpenIM(lnx_wm_state->display, 0, 0, 0);
  
  //- rjf: held keys repeat as presses alone, as on Windows, instead of release+press pairs
  XkbSetDetectableAutoRepeat(lnx_wm_state->display, True, 0);
  
  //- rjf: fill out gfx info
  lnx_wm_state->gfx_info.double_click_time = 0.5f;
  lnx_wm_state->gfx_info.caret_blink_time = 0.5f;
  lnx_wm_state->gfx_info.default_refresh_rate = lnx_wm_refresh_rate();
  if(lnx_wm_state->gfx_info.default_refresh_rate < 1.f) { lnx_wm_state->gfx_info.default_refresh_rate = 60.f; }
  
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
  return 1;
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
wm_set_clipboard_text_impl(String8 string, String8 html)
{
  if(lnx_wm_state->clipboard_arena == 0) { lnx_wm_state->clipboard_arena = arena_alloc(); }
  arena_clear(lnx_wm_state->clipboard_arena);
  lnx_wm_state->clipboard_text = push_str8_copy(lnx_wm_state->clipboard_arena, string);
  lnx_wm_state->clipboard_html = push_str8_copy(lnx_wm_state->clipboard_arena, html);
  lnx_wm_state->clipboard_ascii = 1;
  for(U64 i = 0; i < string.size; i++) { lnx_wm_state->clipboard_ascii &= string.str[i] < 128; }
  XSetSelectionOwner(lnx_wm_state->display, lnx_wm_state->clipboard_atom,
    lnx_wm_state->clipboard_owner, CurrentTime);
  lnx_wm_state->clipboard_owned = XGetSelectionOwner(lnx_wm_state->display, lnx_wm_state->clipboard_atom) == lnx_wm_state->clipboard_owner;
  XFlush(lnx_wm_state->display);
}

// Asks the CLIPBOARD owner for UTF8_STRING, then STRING, and waits up to a
// second for each step of the reply, including INCR transfers of large text.
internal String8
wm_get_clipboard_text_impl(Arena *arena)
{
  Display *dpy = lnx_wm_state->display;
  Window owner = XGetSelectionOwner(dpy, lnx_wm_state->clipboard_atom);
  if(owner == None) { return str8_zero(); }
  if(owner == lnx_wm_state->clipboard_owner) { return push_str8_copy(arena, lnx_wm_state->clipboard_text); }
  Window requestor = lnx_wm_state->clipboard_owner;
  Atom property = lnx_wm_state->clipboard_property_atom;
  Atom targets[] = {lnx_wm_state->clipboard_utf8_atom, XA_STRING};
  String8 result = {0};
  for EachElement(target_idx, targets)
  {
    Temp scratch = scratch_begin(&arena, 1);
    XDeleteProperty(dpy, requestor, property);
    XConvertSelection(dpy, lnx_wm_state->clipboard_atom, targets[target_idx], property, requestor, CurrentTime);
    XFlush(dpy);
    XEvent evt = {0};
    LNX_WM_PropertyMatch notify = {SelectionNotify, requestor, 0};
    if(!lnx_wm_wait_for_event(notify, &evt, 1000000)) { scratch_end(scratch); break; }
    if(evt.xselection.property == None) { scratch_end(scratch); continue; }
    String8List chunks = {0};
    B32 incr = 0, complete = 0;
    LNX_WM_PropertyMatch new_value = {PropertyNotify, requestor, property};
    for(B32 first = 1;; first = 0)
    {
      if(!first && !lnx_wm_wait_for_event(new_value, &evt, 1000000)) { break; }
      Atom type = None;
      int format = 0;
      unsigned long count = 0, bytes_after = 0;
      U8 *data = 0;
      // Deleting the property asks an INCR owner for its next chunk.
      if(XGetWindowProperty(dpy, requestor, property, 0, max_S32/4, True, AnyPropertyType,
                            &type, &format, &count, &bytes_after, &data) != Success) { break; }
      // A property-change event can arrive for a property already taken; INCR ends on an empty one.
      if(!first && type == None) { continue; }
      if(first && type == lnx_wm_state->clipboard_incr_atom) { incr = 1; }
      else if(format == 8 && count != 0) { str8_list_push(scratch.arena, &chunks, push_str8_copy(scratch.arena, str8(data, count))); }
      if(data != 0) { XFree(data); }
      if(!incr || (!first && count == 0)) { complete = 1; break; }
    }
    if(complete)
    {
      String8 text = str8_list_join(scratch.arena, &chunks, 0);
      if(targets[target_idx] == XA_STRING)
      {
        // STRING is Latin-1: each byte is its own code point.
        U8 *utf8 = push_array_no_zero(arena, U8, text.size*2);
        U64 size = 0;
        for(U64 i = 0; i < text.size; i += 1) { size += utf8_encode(utf8+size, text.str[i]); }
        result = str8(utf8, size);
      }
      else
      {
        result = push_str8_copy(arena, text);
      }
    }
    scratch_end(scratch);
    if(complete) { break; }
  }
  return result;
}

// Owns PRIMARY for other applications, small transfers only, as the
// clipboard is served; pasting reads the process-local copy, as reading
// another application's selection is still unimplemented (#150).
internal void
wm_set_selection_text(String8 string)
{
  if(lnx_wm_state->selection_arena == 0)
  {
    lnx_wm_state->selection_arena = arena_alloc();
  }
  arena_clear(lnx_wm_state->selection_arena);
  lnx_wm_state->selection_text = push_str8_copy(lnx_wm_state->selection_arena, string);
  lnx_wm_state->selection_ascii = 1;
  for(U64 i = 0; i < string.size; i++) { lnx_wm_state->selection_ascii &= string.str[i] < 128; }
  if(lnx_wm_state->display == 0) { return; }
  // A drag changes the selection each frame: while PRIMARY is still ours,
  // only its text changes.
  if(string.size != 0 && lnx_wm_state->selection_owned) { return; }
  Window owner = string.size != 0 ? lnx_wm_state->clipboard_owner : None;
  XSetSelectionOwner(lnx_wm_state->display, XA_PRIMARY, owner, CurrentTime);
  lnx_wm_state->selection_owned = owner != None && XGetSelectionOwner(lnx_wm_state->display, XA_PRIMARY) == owner;
  XFlush(lnx_wm_state->display);
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
  // Owns PRIMARY; reading another application's isn't done (#150).
  if(destination == 1) { wm_set_selection_text(clear ? str8_zero() : text); return 1; }
  // UI-thread dispatch is serialized with SelectionClear/SelectionRequest
  // handling in wm_get_events; no provider worker touches the clipboard arena.
  if(clear)
  {
    XSetSelectionOwner(lnx_wm_state->display, lnx_wm_state->clipboard_atom, None, CurrentTime);
    lnx_wm_state->clipboard_owned = 0;
    if(lnx_wm_state->clipboard_arena) { arena_clear(lnx_wm_state->clipboard_arena); }
    lnx_wm_state->clipboard_text = str8_zero();
    lnx_wm_state->clipboard_html = str8_zero();
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
  
  //- rjf: attach name, class & icon
  // The class lets the desktop match the window to wheelhouse.desktop.
  XClassHint class_hint = {"wheelhouse", "Wheelhouse"};
  XSetClassHint(lnx_wm_state->display, w->window, &class_hint);
  lnx_wm_window_set_title(w, title);
  lnx_wm_window_apply_icon(w);
  
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
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
  lnx_wm_window_set_title(w, title);
}

internal void
wm_set_application_icon(Vec2S32 dim, U8 *rgba)
{
  if(lnx_wm_state == 0 || dim.x <= 0 || dim.y <= 0 || rgba == 0) { return; }
  // _NET_WM_ICON is width, height, then ARGB pixels, each in a long, for
  // each size. A 256 px icon overflows a core X request, which Xlib then
  // sends empty, so the image is halved to every size from 128 down to 32.
  Temp scratch = scratch_begin(0, 0);
  S32 w = dim.x, h = dim.y;
  U8 *level = push_array_no_zero(scratch.arena, U8, (U64)w*h*4);
  MemoryCopy(level, rgba, (U64)w*h*4);
  // Both sides must fit, or a long, thin image could still overflow a request.
  U64 capacity = 0;
  for(S32 lw = w, lh = h;; lw /= 2, lh /= 2)
  {
    if(lw <= 128 && lh <= 128) { capacity += 2 + (U64)lw*lh; }
    if(lw/2 < 32 || lh/2 < 32) { break; }
  }
  U64 count = 0;
  long *data = push_array_no_zero(scratch.arena, long, capacity);
  for(;;)
  {
    if(w <= 128 && h <= 128)
    {
      data[count++] = w;
      data[count++] = h;
      for(U64 idx = 0; idx < (U64)w*h; idx += 1)
      {
        U8 *p = level + idx*4;
        data[count++] = ((unsigned long)p[3] << 24) | ((unsigned long)p[0] << 16) | ((unsigned long)p[1] << 8) | p[2];
      }
    }
    if(w/2 < 32 || h/2 < 32) { break; }
    // 2x2 box filter to the next size down.
    S32 hw = w/2, hh = h/2;
    U8 *half = push_array_no_zero(scratch.arena, U8, (U64)hw*hh*4);
    for(S32 y = 0; y < hh; y += 1) for(S32 x = 0; x < hw; x += 1) for(S32 c = 0; c < 4; c += 1)
    {
      U32 sum = level[((2*y)*w + 2*x)*4 + c] + level[((2*y)*w + 2*x+1)*4 + c] +
                level[((2*y+1)*w + 2*x)*4 + c] + level[((2*y+1)*w + 2*x+1)*4 + c];
      half[(y*hw + x)*4 + c] = (U8)((sum + 2)/4);
    }
    level = half; w = hw; h = hh;
  }
  if(count > lnx_wm_state->icon_data_capacity)
  {
    lnx_wm_state->icon_data = push_array_no_zero(lnx_wm_state->arena, long, count);
    lnx_wm_state->icon_data_capacity = count;
  }
  lnx_wm_state->icon_data_count = count;
  MemoryCopy(lnx_wm_state->icon_data, data, count*sizeof(long));
  scratch_end(scratch);
  for(LNX_WM_Window *window = lnx_wm_state->first_window; window != 0; window = window->next) { lnx_wm_window_apply_icon(window); }
  XFlush(lnx_wm_state->display);
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

internal WM_NativeSurface
wm_native_surface(WM_Window handle)
{
  WM_NativeSurface result = {WM_NativeSurfaceKind_X11};
  result.x11.display = lnx_wm_state->display;
  result.x11.visual_id = XVisualIDFromVisual(DefaultVisual(lnx_wm_state->display, DefaultScreen(lnx_wm_state->display)));
  if(!wm_window_match(handle, wm_window_zero()))
  {
    LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
    result.x11.window = w->window;
  }
  return result;
}

// _NET_WM_SYNC_REQUEST: the window manager holds the next step of an
// interactive resize until the counter reaches the value it sent, so the
// counter moves only once a frame at the new size has been swapped.
// X11 has no display-opportunity signal: the swap paces frames.
internal B32
wm_window_frame_ready(WM_Window handle)
{
  return 1;
}

internal B32
wm_window_frame_pending(WM_Window handle)
{
  return 0;
}

internal void
wm_window_presented(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  LNX_WM_Window *w = (LNX_WM_Window *)handle.u64[0];
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
          lnx_wm_state->clipboard_html = str8_zero();
        }
        // Another application's selection: ours stays for pasting here.
        if(evt.xselectionclear.selection == XA_PRIMARY)
        {
          lnx_wm_state->selection_owned = 0;
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
        B32 primary = request->selection == XA_PRIMARY;
        String8 text = primary ? lnx_wm_state->selection_text : lnx_wm_state->clipboard_text;
        String8 html = primary ? str8_zero() : lnx_wm_state->clipboard_html;
        B32 ascii = primary ? lnx_wm_state->selection_ascii : lnx_wm_state->clipboard_ascii;
        B32 owned = primary ? lnx_wm_state->selection_owned :
          (request->selection == lnx_wm_state->clipboard_atom && lnx_wm_state->clipboard_owned);
        U64 max_bytes = (U64)XMaxRequestSize(lnx_wm_state->display)*4-128;
        if(owned && request->owner == lnx_wm_state->clipboard_owner)
        {
          if(request->target == lnx_wm_state->clipboard_targets_atom)
          {
            Atom targets[4] = {lnx_wm_state->clipboard_targets_atom, lnx_wm_state->clipboard_utf8_atom};
            int count = 2;
            if(html.size != 0) { targets[count++] = lnx_wm_state->clipboard_html_atom; }
            if(ascii) { targets[count++] = XA_STRING; }
            XChangeProperty(lnx_wm_state->display, request->requestor, property,
              XA_ATOM, 32, PropModeReplace, (U8 *)targets, count);
            response.xselection.property = property;
          }
          else if((request->target == lnx_wm_state->clipboard_utf8_atom || (ascii && request->target == XA_STRING)) &&
                  text.size < max_bytes)
          {
            XChangeProperty(lnx_wm_state->display, request->requestor, property,
              request->target, 8, PropModeReplace, text.str, (int)text.size);
            response.xselection.property = property;
          }
          else if(html.size != 0 && request->target == lnx_wm_state->clipboard_html_atom && html.size < max_bytes)
          {
            XChangeProperty(lnx_wm_state->display, request->requestor, property,
              request->target, 8, PropModeReplace, html.str, (int)html.size);
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
        WM_Modifiers modifiers = lnx_wm_modifiers_from_state(evt.xkey.state);
        
        // rjf: map keycode -> keysym & codepoint
        LNX_WM_Window *window = lnx_window_from_x11window(evt.xkey.window);
        KeySym keysym = 0;
        U8 text[256] = {0};
        // Xutf8LookupString is undefined for KeyRelease and yields no keysym,
        // which left every key looking held; releases carry no text anyway.
        U64 text_size = 0;
        if(evt.type == KeyPress) { text_size = Xutf8LookupString(window->xic, &evt.xkey, (char *)text, sizeof(text), &keysym, 0); }
        else                     { XLookupString(&evt.xkey, 0, 0, &keysym, 0); }
        
        // rjf: map keysym -> WM_Key
        B32 is_right_sided = 0;
        WM_Key key = lnx_wm_key_from_keysym((U32)keysym, &is_right_sided);
        
        // rjf: track key & modifier state. The event's state is from before
        // this key, so a modifier key adds or removes its own bit.
        lnx_wm_set_key_down(key, evt.type == KeyPress);
        WM_Modifiers key_modifier = 0;
        switch(keysym)
        {
          default:{}break;
          case XK_Shift_L:case XK_Shift_R:{key_modifier = WM_Modifier_Shift;}break;
          case XK_Control_L:case XK_Control_R:{key_modifier = WM_Modifier_Ctrl;}break;
          case XK_Alt_L:case XK_Alt_R:{key_modifier = WM_Modifier_Alt;}break;
          case XK_Super_L:case XK_Super_R:{key_modifier = WM_Modifier_Super;}break;
        }
        lnx_wm_state->modifiers = evt.type == KeyPress ? (modifiers|key_modifier) : (modifiers & ~key_modifier);
        
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
        WM_Modifiers modifiers = lnx_wm_modifiers_from_state(evt.xbutton.state);
        lnx_wm_state->modifiers = modifiers;
        
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
          lnx_wm_set_key_down(key, evt.type == ButtonPress);
          WM_Event *e = wm_event_list_push_new(arena, &evts, evt.type == ButtonPress ? WM_EventKind_Press : WM_EventKind_Release);
          e->window.u64[0] = (U64)window;
          e->modifiers = modifiers;
          e->key = key;
          e->pos = v2f32((F32)evt.xbutton.x, (F32)evt.xbutton.y);
        }
        // Buttons 4-7 are wheel notches, sent as a press and a release; one
        // notch scrolls 120 units, as WM_MOUSEWHEEL does on Windows.
        else if(evt.type == ButtonPress && evt.xbutton.button >= Button4 && evt.xbutton.button <= 7)
        {
          WM_Event *e = wm_event_list_push_new(arena, &evts, WM_EventKind_Scroll);
          e->window.u64[0] = (U64)window;
          e->modifiers = modifiers;
          F32 notch = (evt.xbutton.button == Button4 || evt.xbutton.button == 6) ? -120.f : +120.f;
          e->delta = evt.xbutton.button <= Button5 ? v2f32(0, notch) : v2f32(notch, 0);
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
        lnx_wm_state->modifiers = lnx_wm_modifiers_from_state(evt.xmotion.state);
        set_mouse_cursor = 1;
        int code = window != 0 ? lnx_wm_moveresize_code_from_pos(window, e->pos) : -1;
        lnx_wm_state->last_set_cursor = lnx_wm_cursor_from_moveresize_code(code, lnx_wm_state->last_set_cursor);
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
        // Keys released while another window has focus never reach us.
        MemoryZeroArray(lnx_wm_state->key_down);
        lnx_wm_state->modifiers = 0;
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
  return lnx_wm_state->modifiers;
}

internal B32
wm_key_is_down(WM_Key key)
{
  return key < WM_Key_COUNT && !!(lnx_wm_state->key_down[key/64] & (1ull << (key%64)));
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

internal void
wm_open_in_browser(String8 url)
{
  lnx_wm_open_in_browser(url);
}

// No portable query; desktops differ.
internal B32
wm_reduce_motion(void)
{
  return 0;
}
