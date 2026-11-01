// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef LINUX_WAYLAND_WINDOW_MANAGER_H
#define LINUX_WAYLAND_WINDOW_MANAGER_H

////////////////////////////////
//~ libwayland-client, loaded at run time

// libwayland-client is loaded with dlopen, so a system without it still runs
// the X11 window manager (ADR 0015). Its functions are pointers, filled in by
// way_wm_load: each name below turns the header's declaration of the function
// into a declaration of the pointer, and every call into a call through it.
#define wl_proxy_marshal_flags      (*way_wl_proxy_marshal_flags)
#define wl_proxy_destroy            (*way_wl_proxy_destroy)
#define wl_proxy_add_listener       (*way_wl_proxy_add_listener)
#define wl_proxy_set_user_data      (*way_wl_proxy_set_user_data)
#define wl_proxy_get_user_data      (*way_wl_proxy_get_user_data)
#define wl_proxy_get_version        (*way_wl_proxy_get_version)
#define wl_display_connect          (*way_wl_display_connect)
#define wl_display_disconnect       (*way_wl_display_disconnect)
#define wl_display_get_fd           (*way_wl_display_get_fd)
#define wl_display_dispatch_pending (*way_wl_display_dispatch_pending)
#define wl_display_flush            (*way_wl_display_flush)
#define wl_display_roundtrip        (*way_wl_display_roundtrip)
#define wl_display_prepare_read     (*way_wl_display_prepare_read)
#define wl_display_read_events      (*way_wl_display_read_events)
#define wl_display_cancel_read      (*way_wl_display_cancel_read)
#define wl_display_get_error        (*way_wl_display_get_error)

#define WAY_WM_WaylandXList \
X(wl_proxy_marshal_flags)\
X(wl_proxy_destroy)\
X(wl_proxy_add_listener)\
X(wl_proxy_set_user_data)\
X(wl_proxy_get_user_data)\
X(wl_proxy_get_version)\
X(wl_display_connect)\
X(wl_display_disconnect)\
X(wl_display_get_fd)\
X(wl_display_dispatch_pending)\
X(wl_display_flush)\
X(wl_display_roundtrip)\
X(wl_display_prepare_read)\
X(wl_display_read_events)\
X(wl_display_cancel_read)\
X(wl_display_get_error)

// wl_registry_listener has a field named `global`, which base_core.h defines as `static`.
#pragma push_macro("global")
#undef global
#include "third_party/wayland/wayland-client-core.h"
#include "linux/window_manager/wayland/generated/wayland-protocol.h"
#include "linux/window_manager/wayland/generated/xdg-shell-protocol.h"
#include "linux/window_manager/wayland/generated/xdg-decoration-unstable-v1-protocol.h"
#include "linux/window_manager/wayland/generated/tablet-v2-protocol.h"
#include "linux/window_manager/wayland/generated/cursor-shape-v1-protocol.h"
#pragma pop_macro("global")
#include <linux/input-event-codes.h>
#include <sys/mman.h>

////////////////////////////////
//~ libxkbcommon, loaded at run time

// The few declarations of xkbcommon.h this uses; its ABI has been stable
// since 0.5. Keysyms are X11's numbers (linux_wm_shared.h).
struct xkb_context;
struct xkb_keymap;
struct xkb_state;
#define WAY_XKB_CONTEXT_NO_FLAGS         0
#define WAY_XKB_KEYMAP_FORMAT_TEXT_V1    1
#define WAY_XKB_KEYMAP_COMPILE_NO_FLAGS  0
#define WAY_XKB_STATE_MODS_EFFECTIVE     (1 << 3)

#define WAY_WM_XkbXList \
X(struct xkb_context *, xkb_context_new, (int flags))\
X(void, xkb_context_unref, (struct xkb_context *context))\
X(struct xkb_keymap *, xkb_keymap_new_from_string, (struct xkb_context *context, const char *string, int format, int flags))\
X(void, xkb_keymap_unref, (struct xkb_keymap *keymap))\
X(int, xkb_keymap_key_repeats, (struct xkb_keymap *keymap, U32 key))\
X(struct xkb_state *, xkb_state_new, (struct xkb_keymap *keymap))\
X(void, xkb_state_unref, (struct xkb_state *state))\
X(int, xkb_state_update_mask, (struct xkb_state *state, U32 depressed_mods, U32 latched_mods, U32 locked_mods, U32 depressed_layout, U32 latched_layout, U32 locked_layout))\
X(U32, xkb_state_key_get_one_sym, (struct xkb_state *state, U32 key))\
X(U32, xkb_state_key_get_utf32, (struct xkb_state *state, U32 key))\
X(int, xkb_state_mod_name_is_active, (struct xkb_state *state, const char *name, int type))

#define X(ret, name, params) global ret (*way_##name) params = 0;
WAY_WM_XkbXList
#undef X

////////////////////////////////
//~ Window State

#define WAY_WM_MAX_CLIENT_AREAS 64

typedef struct WAY_WM_Output WAY_WM_Output;
struct WAY_WM_Output
{
  WAY_WM_Output *next;
  struct wl_output *output;
  U32 registry_name;
  String8 name;
  U8 name_buffer[64];
  Vec2S32 size;
  F32 refresh_rate;
  S32 scale;
};

typedef struct WAY_WM_Window WAY_WM_Window;
struct WAY_WM_Window
{
  WAY_WM_Window *next;
  WAY_WM_Window *prev;
  struct wl_surface *surface;
  struct xdg_surface *xdg_surface;
  struct xdg_toplevel *toplevel;
  struct zxdg_toplevel_decoration_v1 *decoration;
  WAY_WM_Output *output;

  // Size in surface coordinates, which are buffer pixels until the window
  // learns its scale.
  Vec2S32 size;
  Vec2S32 requested_size;
  Vec2S32 configure_size;
  B32 configure_maximized;
  B32 configure_fullscreen;
  B32 configure_activated;
  B32 configured;
  B32 maximized;
  B32 fullscreen;
  B32 activated;
  B32 server_decorated;

  B32 custom_border;
  F32 custom_title_bar_thickness;
  F32 custom_edge_thickness;
  Rng2F32 client_areas[WAY_WM_MAX_CLIENT_AREAS];
  U32 client_area_count;
  U32 last_title_bar_press_time;

  // Frame callbacks: frame_next was requested after the last frame was
  // presented and goes out with the next one; frame_wait went out with the
  // last one, and the compositor has not yet asked for another.
  struct wl_callback *frame_next;
  struct wl_callback *frame_wait;
  U64 frame_wait_since_us;
};

////////////////////////////////
//~ State Bundle

typedef struct WAY_WM_State WAY_WM_State;
struct WAY_WM_State
{
  Arena *arena;
  void *wayland_library;
  void *xkb_library;
  struct wl_display *display;
  struct wl_registry *registry;
  struct wl_compositor *compositor;
  struct xdg_wm_base *wm_base;
  struct wl_seat *seat;
  struct wl_pointer *pointer;
  struct wl_keyboard *keyboard;
  struct zxdg_decoration_manager_v1 *decoration_manager;
  struct wp_cursor_shape_manager_v1 *cursor_shape_manager;
  struct wp_cursor_shape_device_v1 *cursor_shape_device;
  int wake_pipe[2];
  WM_SystemInfo gfx_info;

  WAY_WM_Window *first_window;
  WAY_WM_Window *last_window;
  WAY_WM_Window *free_window;
  WAY_WM_Output *first_output;
  WAY_WM_Output *last_output;

  // Events pushed by the listeners while wm_get_events dispatches.
  Arena *events_arena;
  WM_EventList *events;

  // Pointer
  WAY_WM_Window *pointer_window;
  Vec2F32 pointer_pos;
  U32 pointer_enter_serial;
  U32 last_input_serial;
  Vec2F32 scroll_value;
  Vec2F32 scroll_value120;
  B32 scroll_has_value120;
  WM_Cursor cursor;
  WM_Cursor shown_cursor;

  // Keyboard
  WAY_WM_Window *keyboard_window;
  struct xkb_context *xkb_context;
  struct xkb_keymap *xkb_keymap;
  struct xkb_state *xkb_state;
  WM_Modifiers modifiers;
  U64 key_down[(WM_Key_COUNT+63)/64];
  S32 repeat_rate;
  S32 repeat_delay_ms;
  U32 repeat_keycode;
  U64 repeat_next_us;

  // Copy and paste stay within this process until the data-device protocols
  // are in (ADR 0015).
  Arena *clipboard_arena;
  String8 clipboard_text;
  Arena *selection_arena;
  String8 selection_text;
};

////////////////////////////////
//~ Globals

global WAY_WM_State *way_wm_state = 0;

#endif // LINUX_WAYLAND_WINDOW_MANAGER_H
