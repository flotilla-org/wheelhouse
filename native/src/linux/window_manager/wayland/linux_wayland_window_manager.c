// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// A native Wayland window manager beside the X11 one (ADR 0015), chosen with
// --window_manager:wayland. Not yet here, and so still X11's under Xwayland:
// fractional scale, the clipboard and primary selection shared with other
// clients, text-input-v3, activation and a themed cursor fallback.

#include "linux/window_manager/wayland/generated/wayland-protocol.c"
#include "linux/window_manager/wayland/generated/xdg-shell-protocol.c"
#include "linux/window_manager/wayland/generated/xdg-decoration-unstable-v1-protocol.c"
#include "linux/window_manager/wayland/generated/tablet-v2-protocol.c"
#include "linux/window_manager/wayland/generated/cursor-shape-v1-protocol.c"

// The display wait ends after this long without a frame callback (a hidden
// window gets none), as Metal's does without a display link.
#define WAY_WM_FRAME_WAIT_LIMIT_US 100000

////////////////////////////////
//~ Helpers

internal WAY_WM_Window *
way_wm_window_from_handle(WM_Window handle)
{
  return (WAY_WM_Window *)handle.u64[0];
}

internal WM_Window
way_wm_handle_from_window(WAY_WM_Window *w)
{
  WM_Window handle = {(U64)w};
  return handle;
}

internal WM_Event *
way_wm_push_event(WM_EventKind kind, WAY_WM_Window *w)
{
  // Listeners also run in roundtrips outside wm_get_events, which have
  // nowhere to put events.
  if(way_wm_state->events == 0) { return 0; }
  WM_Event *e = wm_event_list_push_new(way_wm_state->events_arena, way_wm_state->events, kind);
  e->window = way_wm_handle_from_window(w);
  e->modifiers = way_wm_state->modifiers;
  return e;
}

internal void
way_wm_set_key_down(WM_Key key, B32 down)
{
  if(key == WM_Key_Null) { return; }
  if(down) { way_wm_state->key_down[key/64] |= (1ull << (key%64)); }
  else     { way_wm_state->key_down[key/64] &= ~(1ull << (key%64)); }
}

internal int
way_wm_moveresize_code_from_pos(WAY_WM_Window *w, Vec2F32 pos)
{
  if(!w->custom_border || w->server_decorated) { return -1; }
  B32 in_client_area = 0;
  for(U32 idx = 0; idx < w->client_area_count; idx += 1)
  {
    in_client_area |= contains_2f32(w->client_areas[idx], pos);
  }
  Rng2F32 rect = r2f32p(0, 0, (F32)w->size.x, (F32)w->size.y);
  return lnx_wm_moveresize_code_from_border(rect, w->custom_edge_thickness, w->custom_title_bar_thickness,
                                            w->maximized || w->fullscreen, in_client_area, pos);
}

internal U32
way_wm_resize_edge_from_moveresize_code(int code)
{
  switch(code)
  {
    default:{}break;
    case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:     {return XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT;}
    case _NET_WM_MOVERESIZE_SIZE_TOP:         {return XDG_TOPLEVEL_RESIZE_EDGE_TOP;}
    case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:    {return XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT;}
    case _NET_WM_MOVERESIZE_SIZE_RIGHT:       {return XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;}
    case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT: {return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT;}
    case _NET_WM_MOVERESIZE_SIZE_BOTTOM:      {return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;}
    case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:  {return XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT;}
    case _NET_WM_MOVERESIZE_SIZE_LEFT:        {return XDG_TOPLEVEL_RESIZE_EDGE_LEFT;}
  }
  return XDG_TOPLEVEL_RESIZE_EDGE_NONE;
}

internal void
way_wm_apply_cursor(B32 force)
{
  WAY_WM_State *s = way_wm_state;
  if(s->cursor_shape_device == 0 || s->pointer_window == 0) { return; }
  if(!force && s->shown_cursor == s->cursor) { return; }
  U32 shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
  switch(s->cursor)
  {
    default:{}break;
    case WM_Cursor_IBar:            {shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT;}break;
    case WM_Cursor_LeftRight:       {shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE;}break;
    case WM_Cursor_UpDown:          {shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE;}break;
    case WM_Cursor_DownRight:       {shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NWSE_RESIZE;}break;
    case WM_Cursor_UpRight:         {shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NESW_RESIZE;}break;
    case WM_Cursor_UpDownLeftRight: {shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_ALL_SCROLL;}break;
    case WM_Cursor_HandPoint:       {shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER;}break;
    case WM_Cursor_Disabled:        {shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED;}break;
  }
  wp_cursor_shape_device_v1_set_shape(s->cursor_shape_device, s->pointer_enter_serial, shape);
  s->shown_cursor = s->cursor;
}

////////////////////////////////
//~ Listeners: frames, surfaces, shell

internal void
way_wm_frame_done(void *data, struct wl_callback *callback, uint32_t time)
{
  WAY_WM_Window *w = (WAY_WM_Window *)data;
  if(callback == w->frame_wait)
  {
    w->frame_wait = 0;
    if(way_wm_state->events != 0) { way_wm_state->events->frame_ready = 1; }
  }
  else if(callback == w->frame_next)
  {
    w->frame_next = 0;
  }
  wl_callback_destroy(callback);
}

read_only global struct wl_callback_listener way_wm_frame_listener = {way_wm_frame_done};

internal void
way_wm_surface_enter(void *data, struct wl_surface *surface, struct wl_output *output)
{
  WAY_WM_Window *w = (WAY_WM_Window *)data;
  w->output = (WAY_WM_Output *)wl_output_get_user_data(output);
}

internal void
way_wm_surface_leave(void *data, struct wl_surface *surface, struct wl_output *output)
{
}

internal void
way_wm_surface_preferred_buffer_scale(void *data, struct wl_surface *surface, int32_t factor)
{
}

internal void
way_wm_surface_preferred_buffer_transform(void *data, struct wl_surface *surface, uint32_t transform)
{
}

read_only global struct wl_surface_listener way_wm_surface_listener =
{
  way_wm_surface_enter,
  way_wm_surface_leave,
  way_wm_surface_preferred_buffer_scale,
  way_wm_surface_preferred_buffer_transform,
};

internal void
way_wm_wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial)
{
  xdg_wm_base_pong(wm_base, serial);
}

read_only global struct xdg_wm_base_listener way_wm_wm_base_listener = {way_wm_wm_base_ping};

// The size and states arrive in xdg_toplevel.configure and take effect at
// the xdg_surface.configure that closes the sequence.
internal void
way_wm_xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial)
{
  WAY_WM_Window *w = (WAY_WM_Window *)data;
  if(w->configure_size.x > 0 && w->configure_size.y > 0) { w->size = w->configure_size; }
  else if(!w->configured)                                 { w->size = w->requested_size; }
  w->maximized = w->configure_maximized;
  w->fullscreen = w->configure_fullscreen;
  w->activated = w->configure_activated;
  w->configured = 1;
  // The next frame is drawn at this size, so it answers this configure.
  xdg_surface_ack_configure(xdg_surface, serial);
  way_wm_push_event(WM_EventKind_Wakeup, w);
}

read_only global struct xdg_surface_listener way_wm_xdg_surface_listener = {way_wm_xdg_surface_configure};

internal void
way_wm_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height, struct wl_array *states)
{
  WAY_WM_Window *w = (WAY_WM_Window *)data;
  w->configure_size = v2s32(width, height);
  w->configure_maximized = w->configure_fullscreen = w->configure_activated = 0;
  U32 *state = 0;
  wl_array_for_each(state, states)
  {
    switch(*state)
    {
      default:{}break;
      case XDG_TOPLEVEL_STATE_MAXIMIZED: {w->configure_maximized = 1;}break;
      case XDG_TOPLEVEL_STATE_FULLSCREEN:{w->configure_fullscreen = 1;}break;
      case XDG_TOPLEVEL_STATE_ACTIVATED: {w->configure_activated = 1;}break;
    }
  }
}

internal void
way_wm_toplevel_close(void *data, struct xdg_toplevel *toplevel)
{
  way_wm_push_event(WM_EventKind_WindowClose, (WAY_WM_Window *)data);
}

internal void
way_wm_toplevel_configure_bounds(void *data, struct xdg_toplevel *toplevel, int32_t width, int32_t height)
{
}

internal void
way_wm_toplevel_wm_capabilities(void *data, struct xdg_toplevel *toplevel, struct wl_array *capabilities)
{
}

read_only global struct xdg_toplevel_listener way_wm_toplevel_listener =
{
  way_wm_toplevel_configure,
  way_wm_toplevel_close,
  way_wm_toplevel_configure_bounds,
  way_wm_toplevel_wm_capabilities,
};

// A compositor that insists on drawing the frame gets no second set of
// window controls from the shell.
internal void
way_wm_decoration_configure(void *data, struct zxdg_toplevel_decoration_v1 *decoration, uint32_t mode)
{
  WAY_WM_Window *w = (WAY_WM_Window *)data;
  w->server_decorated = w->custom_border && mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
}

read_only global struct zxdg_toplevel_decoration_v1_listener way_wm_decoration_listener = {way_wm_decoration_configure};

////////////////////////////////
//~ Listeners: pointer

internal void
way_wm_pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
  WAY_WM_State *s = way_wm_state;
  s->pointer_window = surface ? (WAY_WM_Window *)wl_surface_get_user_data(surface) : 0;
  s->pointer_enter_serial = serial;
  s->pointer_pos = v2f32((F32)wl_fixed_to_double(x), (F32)wl_fixed_to_double(y));
  way_wm_apply_cursor(1);
}

internal void
way_wm_pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface)
{
  way_wm_state->pointer_window = 0;
}

internal void
way_wm_pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t x, wl_fixed_t y)
{
  WAY_WM_State *s = way_wm_state;
  s->pointer_pos = v2f32((F32)wl_fixed_to_double(x), (F32)wl_fixed_to_double(y));
  WAY_WM_Window *w = s->pointer_window;
  WM_Event *e = way_wm_push_event(WM_EventKind_MouseMove, w);
  if(e != 0) { e->pos = s->pointer_pos; }
  if(w != 0)
  {
    s->cursor = lnx_wm_cursor_from_moveresize_code(way_wm_moveresize_code_from_pos(w, s->pointer_pos), s->cursor);
    way_wm_apply_cursor(0);
  }
}

internal void
way_wm_pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state)
{
  WAY_WM_State *s = way_wm_state;
  WAY_WM_Window *w = s->pointer_window;
  B32 pressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
  s->last_input_serial = serial;

  // A press on the title bar or an edge goes to the compositor.
  if(w != 0 && pressed && (button == BTN_LEFT || button == BTN_RIGHT))
  {
    int code = way_wm_moveresize_code_from_pos(w, s->pointer_pos);
    if(button == BTN_RIGHT && code == _NET_WM_MOVERESIZE_MOVE)
    {
      xdg_toplevel_show_window_menu(w->toplevel, s->seat, serial, (S32)s->pointer_pos.x, (S32)s->pointer_pos.y);
      return;
    }
    if(button == BTN_LEFT && code == _NET_WM_MOVERESIZE_MOVE)
    {
      if(time - w->last_title_bar_press_time < (U32)(s->gfx_info.double_click_time*1000))
      {
        w->last_title_bar_press_time = 0;
        if(w->maximized) { xdg_toplevel_unset_maximized(w->toplevel); }
        else             { xdg_toplevel_set_maximized(w->toplevel); }
        return;
      }
      w->last_title_bar_press_time = time;
      xdg_toplevel_move(w->toplevel, s->seat, serial);
      return;
    }
    if(button == BTN_LEFT && code >= 0)
    {
      xdg_toplevel_resize(w->toplevel, s->seat, serial, way_wm_resize_edge_from_moveresize_code(code));
      return;
    }
  }

  WM_Key key = WM_Key_Null;
  switch(button)
  {
    default:{}break;
    case BTN_LEFT:  {key = WM_Key_LeftMouseButton;}break;
    case BTN_MIDDLE:{key = WM_Key_MiddleMouseButton;}break;
    case BTN_RIGHT: {key = WM_Key_RightMouseButton;}break;
  }
  if(key == WM_Key_Null) { return; }
  way_wm_set_key_down(key, pressed);
  WM_Event *e = way_wm_push_event(pressed ? WM_EventKind_Press : WM_EventKind_Release, w);
  if(e != 0)
  {
    e->key = key;
    e->pos = s->pointer_pos;
  }
}

// A frame's axis events add up to one scroll. Wheels report notches in
// 120ths, as WM_MOUSEWHEEL does; continuous scrolling (touchpads) in surface
// units, about 15 to a notch.
internal void
way_wm_pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value)
{
  WAY_WM_State *s = way_wm_state;
  F32 v = (F32)wl_fixed_to_double(value);
  if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL) { s->scroll_value.y += v; }
  else                                        { s->scroll_value.x += v; }
}

internal void
way_wm_pointer_frame(void *data, struct wl_pointer *pointer)
{
  WAY_WM_State *s = way_wm_state;
  Vec2F32 delta = s->scroll_has_value120 ? s->scroll_value120 : scale_2f32(s->scroll_value, 8.f);
  if(delta.x != 0 || delta.y != 0)
  {
    WM_Event *e = way_wm_push_event(WM_EventKind_Scroll, s->pointer_window);
    if(e != 0)
    {
      e->delta = delta;
      e->pos = s->pointer_pos;
    }
  }
  s->scroll_value = s->scroll_value120 = v2f32(0, 0);
  s->scroll_has_value120 = 0;
}

internal void
way_wm_pointer_axis_source(void *data, struct wl_pointer *pointer, uint32_t source)
{
}

internal void
way_wm_pointer_axis_stop(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis)
{
}

internal void
way_wm_pointer_axis_discrete(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t discrete)
{
}

internal void
way_wm_pointer_axis_value120(void *data, struct wl_pointer *pointer, uint32_t axis, int32_t value120)
{
  WAY_WM_State *s = way_wm_state;
  if(axis == WL_POINTER_AXIS_VERTICAL_SCROLL) { s->scroll_value120.y += (F32)value120; }
  else                                        { s->scroll_value120.x += (F32)value120; }
  s->scroll_has_value120 = 1;
}

internal void
way_wm_pointer_axis_relative_direction(void *data, struct wl_pointer *pointer, uint32_t axis, uint32_t direction)
{
}

internal void
way_wm_pointer_warp(void *data, struct wl_pointer *pointer, wl_fixed_t x, wl_fixed_t y)
{
}

read_only global struct wl_pointer_listener way_wm_pointer_listener =
{
  way_wm_pointer_enter,
  way_wm_pointer_leave,
  way_wm_pointer_motion,
  way_wm_pointer_button,
  way_wm_pointer_axis,
  way_wm_pointer_frame,
  way_wm_pointer_axis_source,
  way_wm_pointer_axis_stop,
  way_wm_pointer_axis_discrete,
  way_wm_pointer_axis_value120,
  way_wm_pointer_axis_relative_direction,
  way_wm_pointer_warp,
};

////////////////////////////////
//~ Listeners: keyboard

internal void
way_wm_keyboard_keymap(void *data, struct wl_keyboard *keyboard, uint32_t format, int32_t fd, uint32_t size)
{
  WAY_WM_State *s = way_wm_state;
  if(format == WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1)
  {
    char *text = (char *)mmap(0, size, PROT_READ, MAP_PRIVATE, fd, 0);
    if(text != MAP_FAILED)
    {
      struct xkb_keymap *keymap = way_xkb_keymap_new_from_string(s->xkb_context, text, WAY_XKB_KEYMAP_FORMAT_TEXT_V1, WAY_XKB_KEYMAP_COMPILE_NO_FLAGS);
      munmap(text, size);
      if(keymap != 0)
      {
        if(s->xkb_state != 0)  { way_xkb_state_unref(s->xkb_state); }
        if(s->xkb_keymap != 0) { way_xkb_keymap_unref(s->xkb_keymap); }
        s->xkb_keymap = keymap;
        s->xkb_state = way_xkb_state_new(keymap);
      }
    }
  }
  close(fd);
}

internal void
way_wm_keyboard_enter(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface, struct wl_array *keys)
{
  way_wm_state->keyboard_window = surface ? (WAY_WM_Window *)wl_surface_get_user_data(surface) : 0;
}

internal void
way_wm_keyboard_leave(void *data, struct wl_keyboard *keyboard, uint32_t serial, struct wl_surface *surface)
{
  WAY_WM_State *s = way_wm_state;
  WAY_WM_Window *w = surface ? (WAY_WM_Window *)wl_surface_get_user_data(surface) : 0;
  // Keys released while another window has focus never reach us.
  MemoryZeroArray(s->key_down);
  s->modifiers = 0;
  s->repeat_keycode = 0;
  s->keyboard_window = 0;
  way_wm_push_event(WM_EventKind_WindowLoseFocus, w);
}

internal void
way_wm_key_event(U32 keycode, B32 pressed, B32 repeat)
{
  WAY_WM_State *s = way_wm_state;
  if(s->xkb_state == 0) { return; }
  B32 right_sided = 0;
  WM_Key key = lnx_wm_key_from_keysym(way_xkb_state_key_get_one_sym(s->xkb_state, keycode), &right_sided);
  way_wm_set_key_down(key, pressed);
  WM_Event *e = way_wm_push_event(pressed ? WM_EventKind_Press : WM_EventKind_Release, s->keyboard_window);
  if(e != 0)
  {
    e->key = key;
    e->right_sided = right_sided;
    e->is_repeat = repeat;
  }
  U32 codepoint = pressed ? way_xkb_state_key_get_utf32(s->xkb_state, keycode) : 0;
  if(codepoint >= 32 && codepoint != 127 || codepoint == '\t')
  {
    WM_Event *t = way_wm_push_event(WM_EventKind_Text, s->keyboard_window);
    if(t != 0)
    {
      t->character = codepoint;
      t->source_key = key;
    }
  }
}

// Wayland leaves key repeat to the client: held keys repeat as presses alone,
// as X11's detectable auto-repeat gives them.
internal void
way_wm_keyboard_key(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state)
{
  WAY_WM_State *s = way_wm_state;
  U32 keycode = key + 8;
  B32 pressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;
  s->last_input_serial = serial;
  way_wm_key_event(keycode, pressed, 0);
  if(pressed && s->repeat_rate > 0 && s->xkb_keymap != 0 && way_xkb_keymap_key_repeats(s->xkb_keymap, keycode))
  {
    s->repeat_keycode = keycode;
    s->repeat_next_us = now_time_us() + (U64)s->repeat_delay_ms*1000;
  }
  else if(!pressed && keycode == s->repeat_keycode)
  {
    s->repeat_keycode = 0;
  }
}

internal void
way_wm_keyboard_modifiers(void *data, struct wl_keyboard *keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group)
{
  WAY_WM_State *s = way_wm_state;
  if(s->xkb_state == 0) { return; }
  way_xkb_state_update_mask(s->xkb_state, depressed, latched, locked, 0, 0, group);
  WM_Modifiers modifiers = 0;
  if(way_xkb_state_mod_name_is_active(s->xkb_state, "Shift",   WAY_XKB_STATE_MODS_EFFECTIVE) > 0) { modifiers |= WM_Modifier_Shift; }
  if(way_xkb_state_mod_name_is_active(s->xkb_state, "Control", WAY_XKB_STATE_MODS_EFFECTIVE) > 0) { modifiers |= WM_Modifier_Ctrl; }
  if(way_xkb_state_mod_name_is_active(s->xkb_state, "Mod1",    WAY_XKB_STATE_MODS_EFFECTIVE) > 0) { modifiers |= WM_Modifier_Alt; }
  if(way_xkb_state_mod_name_is_active(s->xkb_state, "Mod4",    WAY_XKB_STATE_MODS_EFFECTIVE) > 0) { modifiers |= WM_Modifier_Super; }
  s->modifiers = modifiers;
}

internal void
way_wm_keyboard_repeat_info(void *data, struct wl_keyboard *keyboard, int32_t rate, int32_t delay)
{
  way_wm_state->repeat_rate = rate;
  way_wm_state->repeat_delay_ms = delay;
}

read_only global struct wl_keyboard_listener way_wm_keyboard_listener =
{
  way_wm_keyboard_keymap,
  way_wm_keyboard_enter,
  way_wm_keyboard_leave,
  way_wm_keyboard_key,
  way_wm_keyboard_modifiers,
  way_wm_keyboard_repeat_info,
};

////////////////////////////////
//~ Listeners: seat, outputs, registry

internal void
way_wm_seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities)
{
  WAY_WM_State *s = way_wm_state;
  if((capabilities & WL_SEAT_CAPABILITY_POINTER) && s->pointer == 0)
  {
    s->pointer = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(s->pointer, &way_wm_pointer_listener, 0);
    if(s->cursor_shape_manager != 0)
    {
      s->cursor_shape_device = wp_cursor_shape_manager_v1_get_pointer(s->cursor_shape_manager, s->pointer);
    }
  }
  if((capabilities & WL_SEAT_CAPABILITY_KEYBOARD) && s->keyboard == 0)
  {
    s->keyboard = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(s->keyboard, &way_wm_keyboard_listener, 0);
  }
}

internal void
way_wm_seat_name(void *data, struct wl_seat *seat, const char *name)
{
}

read_only global struct wl_seat_listener way_wm_seat_listener = {way_wm_seat_capabilities, way_wm_seat_name};

internal void
way_wm_output_geometry(void *data, struct wl_output *output, int32_t x, int32_t y, int32_t physical_width, int32_t physical_height,
                       int32_t subpixel, const char *make, const char *model, int32_t transform)
{
}

internal void
way_wm_output_mode(void *data, struct wl_output *output, uint32_t flags, int32_t width, int32_t height, int32_t refresh)
{
  WAY_WM_Output *o = (WAY_WM_Output *)data;
  if(flags & WL_OUTPUT_MODE_CURRENT)
  {
    o->size = v2s32(width, height);
    o->refresh_rate = refresh / 1000.f;
  }
}

internal void
way_wm_output_done(void *data, struct wl_output *output)
{
}

internal void
way_wm_output_scale(void *data, struct wl_output *output, int32_t factor)
{
  ((WAY_WM_Output *)data)->scale = factor;
}

internal void
way_wm_output_name(void *data, struct wl_output *output, const char *name)
{
  WAY_WM_Output *o = (WAY_WM_Output *)data;
  String8 copy = str8_cstring((char *)name);
  copy.size = Min(copy.size, sizeof(o->name_buffer));
  MemoryCopy(o->name_buffer, copy.str, copy.size);
  o->name = str8(o->name_buffer, copy.size);
}

internal void
way_wm_output_description(void *data, struct wl_output *output, const char *description)
{
}

read_only global struct wl_output_listener way_wm_output_listener =
{
  way_wm_output_geometry,
  way_wm_output_mode,
  way_wm_output_done,
  way_wm_output_scale,
  way_wm_output_name,
  way_wm_output_description,
};

internal void
way_wm_registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
  WAY_WM_State *s = way_wm_state;
  String8 iface = str8_cstring((char *)interface);
  if(str8_match(iface, str8_cstring((char *)wl_compositor_interface.name), 0))
  {
    s->compositor = (struct wl_compositor *)wl_registry_bind(registry, name, &wl_compositor_interface, Min(version, 4));
  }
  else if(str8_match(iface, str8_cstring((char *)xdg_wm_base_interface.name), 0))
  {
    s->wm_base = (struct xdg_wm_base *)wl_registry_bind(registry, name, &xdg_wm_base_interface, Min(version, 2));
    xdg_wm_base_add_listener(s->wm_base, &way_wm_wm_base_listener, 0);
  }
  else if(str8_match(iface, str8_cstring((char *)wl_seat_interface.name), 0) && s->seat == 0)
  {
    // Version 8 brings axis_value120; frame (5) is in it.
    s->seat = (struct wl_seat *)wl_registry_bind(registry, name, &wl_seat_interface, Min(version, 8));
    wl_seat_add_listener(s->seat, &way_wm_seat_listener, 0);
  }
  else if(str8_match(iface, str8_cstring((char *)wl_output_interface.name), 0))
  {
    WAY_WM_Output *o = push_array(s->arena, WAY_WM_Output, 1);
    o->registry_name = name;
    o->scale = 1;
    o->output = (struct wl_output *)wl_registry_bind(registry, name, &wl_output_interface, Min(version, 4));
    wl_output_add_listener(o->output, &way_wm_output_listener, o);
    SLLQueuePush(s->first_output, s->last_output, o);
  }
  else if(str8_match(iface, str8_cstring((char *)zxdg_decoration_manager_v1_interface.name), 0))
  {
    s->decoration_manager = (struct zxdg_decoration_manager_v1 *)wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
  }
  else if(str8_match(iface, str8_cstring((char *)wp_cursor_shape_manager_v1_interface.name), 0))
  {
    s->cursor_shape_manager = (struct wp_cursor_shape_manager_v1 *)wl_registry_bind(registry, name, &wp_cursor_shape_manager_v1_interface, 1);
  }
}

internal void
way_wm_registry_global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
  WAY_WM_State *s = way_wm_state;
  WAY_WM_Output *prev = 0;
  for(WAY_WM_Output *o = s->first_output; o != 0; prev = o, o = o->next)
  {
    if(o->registry_name != name) { continue; }
    for(WAY_WM_Window *w = s->first_window; w != 0; w = w->next)
    {
      if(w->output == o) { w->output = 0; }
    }
    if(prev != 0) { prev->next = o->next; } else { s->first_output = o->next; }
    if(s->last_output == o) { s->last_output = prev; }
    wl_output_destroy(o->output);
    break;
  }
}

read_only global struct wl_registry_listener way_wm_registry_listener = {way_wm_registry_global, way_wm_registry_global_remove};

////////////////////////////////
//~ Loading

internal B32
way_wm_load_libraries(void)
{
  WAY_WM_State *s = way_wm_state;
  s->wayland_library = dlopen("libwayland-client.so.0", RTLD_NOW|RTLD_LOCAL);
  s->xkb_library = dlopen("libxkbcommon.so.0", RTLD_NOW|RTLD_LOCAL);
  if(s->wayland_library == 0 || s->xkb_library == 0)
  {
    fprintf(stderr, "Wayland needs libwayland-client.so.0 and libxkbcommon.so.0: %s\n", dlerror());
    return 0;
  }
  B32 result = 1;
#define X(name) *(void **)&way_##name = dlsym(s->wayland_library, #name); result &= way_##name != 0;
  WAY_WM_WaylandXList
#undef X
#define X(ret, name, params) *(void **)&way_##name = dlsym(s->xkb_library, #name); result &= way_##name != 0;
  WAY_WM_XkbXList
#undef X
  if(!result) { fprintf(stderr, "libwayland-client or libxkbcommon is too old.\n"); }
  return result;
}

////////////////////////////////
//~ @os_hooks Main Initialization API (Implemented Per-OS)

internal B32
wm_init(CmdLine *cmdln)
{
  Arena *arena = arena_alloc();
  way_wm_state = push_array(arena, WAY_WM_State, 1);
  way_wm_state->arena = arena;
  WAY_WM_State *s = way_wm_state;
  B32 ok = way_wm_load_libraries();
  if(ok)
  {
    s->display = wl_display_connect(0);
    if(s->display == 0)
    {
      char *name = getenv("WAYLAND_DISPLAY");
      fprintf(stderr, "Could not connect to Wayland display \"%s\".\n", name ? name : "wayland-0");
      ok = 0;
    }
  }
  if(ok)
  {
    s->xkb_context = way_xkb_context_new(WAY_XKB_CONTEXT_NO_FLAGS);
    s->registry = wl_display_get_registry(s->display);
    wl_registry_add_listener(s->registry, &way_wm_registry_listener, 0);
    // The first roundtrip announces the globals; the second, what they say on binding.
    wl_display_roundtrip(s->display);
    wl_display_roundtrip(s->display);
    if(s->compositor == 0 || s->wm_base == 0 || s->xkb_context == 0)
    {
      fprintf(stderr, "The Wayland compositor has no xdg-shell.\n");
      ok = 0;
    }
  }
  if(ok && pipe2(s->wake_pipe, O_NONBLOCK|O_CLOEXEC) != 0) { perror("window wake pipe"); ok = 0; }
  if(!ok)
  {
    if(s->display != 0)         { wl_display_disconnect(s->display); }
    if(s->wayland_library != 0) { dlclose(s->wayland_library); }
    if(s->xkb_library != 0)     { dlclose(s->xkb_library); }
    arena_release(arena);
    way_wm_state = 0;
    return 0;
  }
  s->repeat_rate = 25;
  s->repeat_delay_ms = 600;
  s->gfx_info.double_click_time = 0.5f;
  s->gfx_info.caret_blink_time = 0.5f;
  s->gfx_info.default_refresh_rate = s->first_output != 0 ? s->first_output->refresh_rate : 0;
  if(s->gfx_info.default_refresh_rate < 1.f) { s->gfx_info.default_refresh_rate = 60.f; }
  return 1;
}

////////////////////////////////
//~ @os_hooks Graphics System Info (Implemented Per-OS)

internal WM_SystemInfo *
wm_get_system_info(void)
{
  return &way_wm_state->gfx_info;
}

////////////////////////////////
//~ @os_hooks Clipboards (Implemented Per-OS)

internal void
wm_set_clipboard_text_impl(String8 string, String8 html)
{
  WAY_WM_State *s = way_wm_state;
  if(s->clipboard_arena == 0) { s->clipboard_arena = arena_alloc(); }
  arena_clear(s->clipboard_arena);
  s->clipboard_text = push_str8_copy(s->clipboard_arena, string);
}

internal String8
wm_get_clipboard_text_impl(Arena *arena)
{
  return push_str8_copy(arena, way_wm_state->clipboard_text);
}

internal void
wm_set_selection_text(String8 string)
{
  WAY_WM_State *s = way_wm_state;
  if(s->selection_arena == 0) { s->selection_arena = arena_alloc(); }
  arena_clear(s->selection_arena);
  s->selection_text = push_str8_copy(s->selection_arena, string);
}

internal String8
wm_get_selection_text(Arena *arena)
{
  return push_str8_copy(arena, way_wm_state->selection_text);
}

internal B32
wm_apply_clipboard_write(U32 destination, B32 clear, String8 text)
{
  if(destination > 1 || way_wm_state == 0) { return 0; }
  String8 value = clear ? str8_zero() : text;
  if(destination == 1) { wm_set_selection_text(value); }
  else                 { wm_set_clipboard_text_impl(value, str8_zero()); }
  return 1;
}

////////////////////////////////
//~ @os_hooks Windows (Implemented Per-OS)

internal WM_Window
wm_window_open(Rng2F32 rect, WM_WindowFlags flags, String8 title)
{
  WAY_WM_State *s = way_wm_state;
  WAY_WM_Window *w = s->free_window;
  if(w != 0) { SLLStackPop(s->free_window); }
  else       { w = push_array_no_zero(s->arena, WAY_WM_Window, 1); }
  MemoryZeroStruct(w);
  DLLPushBack(s->first_window, s->last_window, w);

  Vec2F32 dim = dim_2f32(rect);
  w->requested_size = v2s32(Max((S32)dim.x, 1), Max((S32)dim.y, 1));
  w->size = w->requested_size;
  w->custom_border = !!(flags & WM_WindowFlag_CustomBorder);
  w->surface = wl_compositor_create_surface(s->compositor);
  wl_surface_add_listener(w->surface, &way_wm_surface_listener, w);
  w->xdg_surface = xdg_wm_base_get_xdg_surface(s->wm_base, w->surface);
  xdg_surface_add_listener(w->xdg_surface, &way_wm_xdg_surface_listener, w);
  w->toplevel = xdg_surface_get_toplevel(w->xdg_surface);
  xdg_toplevel_add_listener(w->toplevel, &way_wm_toplevel_listener, w);
  // The app id lets the desktop match the window to wheelhouse.desktop.
  xdg_toplevel_set_app_id(w->toplevel, "wheelhouse");
  wm_window_set_title(way_wm_handle_from_window(w), title);

  // The shell draws a custom-border window's title bar, so it asks for none.
  if(s->decoration_manager != 0)
  {
    w->decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(s->decoration_manager, w->toplevel);
    zxdg_toplevel_decoration_v1_add_listener(w->decoration, &way_wm_decoration_listener, w);
    zxdg_toplevel_decoration_v1_set_mode(w->decoration, w->custom_border ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE :
                                                                          ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }

  // A surface may show a buffer only once its first configure is acked.
  wl_surface_commit(w->surface);
  while(!w->configured && wl_display_roundtrip(s->display) != -1) {}
  return way_wm_handle_from_window(w);
}

internal void
wm_window_close(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  WAY_WM_State *s = way_wm_state;
  WAY_WM_Window *w = way_wm_window_from_handle(handle);
  if(w->frame_next != 0) { wl_callback_destroy(w->frame_next); }
  if(w->frame_wait != 0) { wl_callback_destroy(w->frame_wait); }
  if(w->decoration != 0) { zxdg_toplevel_decoration_v1_destroy(w->decoration); }
  xdg_toplevel_destroy(w->toplevel);
  xdg_surface_destroy(w->xdg_surface);
  wl_surface_destroy(w->surface);
  if(s->pointer_window == w)  { s->pointer_window = 0; }
  if(s->keyboard_window == w) { s->keyboard_window = 0; }
  DLLRemove(s->first_window, s->last_window, w);
  SLLStackPush(s->free_window, w);
  wl_display_flush(s->display);
}

internal void
wm_window_set_title(WM_Window handle, String8 title)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  Temp scratch = scratch_begin(0, 0);
  String8 title_copy = push_str8_copy(scratch.arena, title);
  xdg_toplevel_set_title(way_wm_window_from_handle(handle)->toplevel, (char *)title_copy.str);
  scratch_end(scratch);
}

// The icon comes from wheelhouse.desktop, matched by app id.
internal void
wm_set_application_icon(Vec2S32 dim, U8 *rgba)
{
}

// The first frame's buffer maps the window.
internal void
wm_window_first_paint(WM_Window handle)
{
}

// Raising a window needs an xdg-activation token (ADR 0015).
internal void
wm_window_focus(WM_Window handle)
{
}

internal B32
wm_window_is_focused(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 0;}
  return way_wm_window_from_handle(handle)->activated;
}

internal B32
wm_window_is_fullscreen(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 0;}
  return way_wm_window_from_handle(handle)->fullscreen;
}

internal void
wm_window_set_fullscreen(WM_Window handle, B32 fullscreen)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  WAY_WM_Window *w = way_wm_window_from_handle(handle);
  if(fullscreen) { xdg_toplevel_set_fullscreen(w->toplevel, 0); }
  else           { xdg_toplevel_unset_fullscreen(w->toplevel); }
}

internal B32
wm_window_is_maximized(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 0;}
  return way_wm_window_from_handle(handle)->maximized;
}

internal void
wm_window_set_maximized(WM_Window handle, B32 maximized)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  WAY_WM_Window *w = way_wm_window_from_handle(handle);
  if(maximized) { xdg_toplevel_set_maximized(w->toplevel); }
  else          { xdg_toplevel_unset_maximized(w->toplevel); }
}

// Wayland does not tell a client it is minimized, nor let it restore itself.
internal B32
wm_window_is_minimized(WM_Window handle)
{
  return 0;
}

internal void
wm_window_set_minimized(WM_Window handle, B32 minimized)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  if(minimized) { xdg_toplevel_set_minimized(way_wm_window_from_handle(handle)->toplevel); }
}

internal void
wm_window_bring_to_front(WM_Window handle)
{
}

// The compositor places windows; only a fullscreen one picks its output.
internal void
wm_window_set_monitor(WM_Window handle, WM_Monitor monitor)
{
  if(wm_window_match(handle, wm_window_zero()) || monitor.u64[0] == 0) {return;}
  WAY_WM_Window *w = way_wm_window_from_handle(handle);
  if(w->fullscreen) { xdg_toplevel_set_fullscreen(w->toplevel, ((WAY_WM_Output *)monitor.u64[0])->output); }
}

internal void
wm_window_clear_custom_border_data(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  WAY_WM_Window *w = way_wm_window_from_handle(handle);
  w->client_area_count = 0;
  w->custom_title_bar_thickness = 0;
  w->custom_edge_thickness = 0;
}

internal void
wm_window_push_custom_title_bar(WM_Window handle, F32 thickness)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  way_wm_window_from_handle(handle)->custom_title_bar_thickness = thickness;
}

internal void
wm_window_push_custom_edges(WM_Window handle, F32 thickness)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  way_wm_window_from_handle(handle)->custom_edge_thickness = thickness;
}

internal void
wm_window_push_custom_title_bar_client_area(WM_Window handle, Rng2F32 rect)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  WAY_WM_Window *w = way_wm_window_from_handle(handle);
  if(w->client_area_count < WAY_WM_MAX_CLIENT_AREAS) { w->client_areas[w->client_area_count++] = rect; }
}

internal B32
wm_window_should_draw_custom_title_bar_controls(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 1;}
  return !way_wm_window_from_handle(handle)->server_decorated;
}

internal F32
wm_window_native_title_bar_left_padding(WM_Window window)
{
  return 0;
}

// Wayland has no global positions: a window's rect starts at its own origin.
internal Rng2F32
wm_rect_from_window(WM_Window handle)
{
  return wm_client_rect_from_window(handle);
}

internal Rng2F32
wm_client_rect_from_window(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return r2f32p(0, 0, 0, 0);}
  WAY_WM_Window *w = way_wm_window_from_handle(handle);
  return r2f32p(0, 0, (F32)w->size.x, (F32)w->size.y);
}

internal F32
wm_dpi_from_window(WM_Window handle)
{
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
  WM_NativeSurface result = {WM_NativeSurfaceKind_Wayland};
  result.wayland.display = way_wm_state->display;
  if(!wm_window_match(handle, wm_window_zero()))
  {
    result.wayland.surface = way_wm_window_from_handle(handle)->surface;
  }
  return result;
}

internal B32
wm_window_frame_ready(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 1;}
  WAY_WM_Window *w = way_wm_window_from_handle(handle);
  return w->frame_wait == 0 || now_time_us() >= w->frame_wait_since_us + WAY_WM_FRAME_WAIT_LIMIT_US;
}

internal B32
wm_window_frame_pending(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return 0;}
  return !wm_window_frame_ready(handle);
}

// The frame just committed carried the callback requested after the frame
// before it; the one requested now goes out with the next. Requested any
// earlier, it would go out with whatever commit came first.
internal void
wm_window_presented(WM_Window handle)
{
  if(wm_window_match(handle, wm_window_zero())) {return;}
  WAY_WM_Window *w = way_wm_window_from_handle(handle);
  if(w->frame_wait != 0) { wl_callback_destroy(w->frame_wait); }
  w->frame_wait = w->frame_next;
  w->frame_wait_since_us = now_time_us();
  w->frame_next = wl_surface_frame(w->surface);
  wl_callback_add_listener(w->frame_next, &way_wm_frame_listener, w);
}

////////////////////////////////
//~ @os_hooks External Windows (Implemented Per-OS)

internal WM_ExtWindow
wm_focused_external_window(void)
{
  WM_ExtWindow result = {0};
  return result;
}

internal void
wm_focus_external_window(WM_ExtWindow handle)
{
}

////////////////////////////////
//~ @os_hooks Monitors (Implemented Per-OS)

internal WM_MonitorArray
wm_push_monitors_array(Arena *arena)
{
  WM_MonitorArray result = {0};
  for(WAY_WM_Output *o = way_wm_state->first_output; o != 0; o = o->next) { result.count += 1; }
  result.v = push_array(arena, WM_Monitor, result.count);
  U64 idx = 0;
  for(WAY_WM_Output *o = way_wm_state->first_output; o != 0; o = o->next) { result.v[idx++].u64[0] = (U64)o; }
  return result;
}

internal WM_Monitor
wm_primary_monitor(void)
{
  WM_Monitor result = {(U64)way_wm_state->first_output};
  return result;
}

internal WM_Monitor
wm_monitor_from_window(WM_Window handle)
{
  WM_Monitor result = {0};
  if(!wm_window_match(handle, wm_window_zero())) { result.u64[0] = (U64)way_wm_window_from_handle(handle)->output; }
  return result;
}

internal String8
wm_name_from_monitor(Arena *arena, WM_Monitor monitor)
{
  WAY_WM_Output *o = (WAY_WM_Output *)monitor.u64[0];
  return o != 0 ? push_str8_copy(arena, o->name) : str8_zero();
}

internal Vec2F32
wm_dim_from_monitor(WM_Monitor monitor)
{
  WAY_WM_Output *o = (WAY_WM_Output *)monitor.u64[0];
  return o != 0 ? v2f32((F32)o->size.x, (F32)o->size.y) : v2f32(0, 0);
}

internal F32
wm_dpi_from_monitor(WM_Monitor monitor)
{
  return 96.f;
}

////////////////////////////////
//~ @os_hooks Events (Implemented Per-OS)

internal void
wm_send_wakeup_event(void)
{
  // A full pipe already guarantees a wakeup. No Wayland calls from worker threads.
  if(way_wm_state != 0)
  {
    char byte = 1;
    ssize_t result;
    do { result = write(way_wm_state->wake_pipe[1], &byte, 1); } while(result < 0 && errno == EINTR);
  }
}

internal void
way_wm_push_key_repeats(void)
{
  WAY_WM_State *s = way_wm_state;
  if(s->repeat_keycode == 0 || s->repeat_rate <= 0) { return; }
  U64 now = now_time_us();
  U64 interval_us = 1000000/(U64)s->repeat_rate;
  // After a stall, repeat once and carry on from now rather than in a burst.
  if(now >= s->repeat_next_us + 4*interval_us) { s->repeat_next_us = now; }
  for(; now >= s->repeat_next_us; s->repeat_next_us += interval_us)
  {
    way_wm_key_event(s->repeat_keycode, 1, 1);
  }
}

// When the first window still waiting for its frame callback stops waiting.
internal U64
way_wm_frame_wait_end_us(U64 now)
{
  U64 result = max_U64;
  for(WAY_WM_Window *w = way_wm_state->first_window; w != 0; w = w->next)
  {
    U64 end = w->frame_wait_since_us + WAY_WM_FRAME_WAIT_LIMIT_US;
    if(w->frame_wait != 0 && end > now) { result = Min(result, end); }
  }
  return result;
}

internal WM_EventList
wm_get_events(Arena *arena, B32 wait)
{
  WAY_WM_State *s = way_wm_state;
  WM_EventList evts = {0};
  s->events_arena = arena;
  s->events = &evts;
  U64 timeout_us = wait ? wm_take_wait_timeout_us() : 0;
  U64 until_us = timeout_us == max_U64 ? max_U64 : now_time_us() + timeout_us;
  for(B32 first = 1;; first = 0)
  {
    way_wm_push_key_repeats();
    if(wl_display_dispatch_pending(s->display) < 0) { break; }
    if(evts.count != 0 || evts.frame_ready) { break; }
    if(!first && !wait) { break; }
    U64 now = now_time_us();
    if(wait && now >= until_us) { break; }

    //- block until the compositor, a wakeup, a key repeat or a frame wait's end
    U64 frame_wait_end_us = way_wm_frame_wait_end_us(now);
    U64 repeat_us = s->repeat_keycode != 0 && s->repeat_rate > 0 ? s->repeat_next_us : max_U64;
    U64 deadline_us = Min(Min(wait ? until_us : now, repeat_us), frame_wait_end_us);
    int timeout_ms = -1;
    if(deadline_us != max_U64) { timeout_ms = deadline_us > now ? (int)Min((deadline_us - now + 999)/1000, (U64)max_S32) : 0; }
    while(wl_display_prepare_read(s->display) != 0) { wl_display_dispatch_pending(s->display); }
    wl_display_flush(s->display);
    struct pollfd fds[] = {{wl_display_get_fd(s->display), POLLIN, 0}, {s->wake_pipe[0], POLLIN, 0}};
    int result;
    do { result = poll(fds, 2, timeout_ms); } while(result < 0 && errno == EINTR);
    if(result > 0 && (fds[0].revents & POLLIN)) { wl_display_read_events(s->display); }
    else                                        { wl_display_cancel_read(s->display); }
    if(result > 0 && (fds[1].revents & POLLIN))
    {
      char bytes[128];
      while(read(s->wake_pipe[0], bytes, sizeof(bytes)) > 0) {}
      wl_display_dispatch_pending(s->display);
      break;
    }
    // A window whose frame wait ran out is ready again: the caller decides.
    if(result == 0 && now_time_us() >= frame_wait_end_us) { break; }
  }

  // Like Xlib's default, losing the compositor ends the process.
  if(wl_display_get_error(s->display) != 0)
  {
    fprintf(stderr, "Lost the connection to the Wayland compositor.\n");
    exit(1);
  }
  wl_display_flush(s->display);
  s->events = 0;
  s->events_arena = 0;
  return evts;
}

internal WM_Modifiers
wm_get_modifiers(void)
{
  return way_wm_state->modifiers;
}

internal B32
wm_key_is_down(WM_Key key)
{
  return key < WM_Key_COUNT && !!(way_wm_state->key_down[key/64] & (1ull << (key%64)));
}

internal Vec2F32
wm_mouse_from_window(WM_Window handle)
{
  return way_wm_state->pointer_pos;
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
//~ @os_hooks Cursors (Implemented Per-OS)

internal void
wm_set_cursor(WM_Cursor cursor)
{
  way_wm_state->cursor = cursor;
  way_wm_apply_cursor(0);
}

////////////////////////////////
//~ @os_hooks Native User-Facing Graphical Messages (Implemented Per-OS)

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
//~ @os_hooks Shell Operations

internal void
wm_show_in_filesystem_ui(String8 path)
{
}

internal void
wm_open_in_browser(String8 url)
{
  lnx_wm_open_in_browser(url);
}

internal B32
wm_reduce_motion(void)
{
  return 0;
}
