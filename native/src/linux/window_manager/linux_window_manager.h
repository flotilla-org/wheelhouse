// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef LINUX_WINDOW_MANAGER_H
#define LINUX_WINDOW_MANAGER_H

////////////////////////////////
//~ rjf: Includes

#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/cursorfont.h>
#include <X11/extensions/sync.h>
#include <X11/extensions/Xrandr.h>
#include <X11/Xutil.h>
// XKBstr.h has a field named `internal`, which base_core.h defines as `static`.
#pragma push_macro("internal")
#undef internal
#include <X11/XKBlib.h>
#pragma pop_macro("internal")
#include <X11/keysym.h>
#include <X11/keysymdef.h>

////////////////////////////////
//~ rjf: EWMH _NET_WM_MOVERESIZE directions

#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT      0
#define _NET_WM_MOVERESIZE_SIZE_TOP          1
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT     2
#define _NET_WM_MOVERESIZE_SIZE_RIGHT        3
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT  4
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM       5
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT   6
#define _NET_WM_MOVERESIZE_SIZE_LEFT         7
#define _NET_WM_MOVERESIZE_MOVE              8

////////////////////////////////
//~ rjf: Window State

typedef struct LNX_WM_WindowClientArea LNX_WM_WindowClientArea;
struct LNX_WM_WindowClientArea
{
  LNX_WM_WindowClientArea *next;
  Rng2F32 rect;
};

typedef struct LNX_WM_Window LNX_WM_Window;
struct LNX_WM_Window
{
  LNX_WM_Window *next;
  LNX_WM_Window *prev;
  Window window;
  XIC xic;
  XID counter_xid;
  U64 counter_value;
  B32 sync_pending;
  Vec2S32 configured_size;
  B32 maximized;
  B32 minimized;
  B32 fullscreen;
  B32 custom_border;
  F32 custom_title_bar_thickness;
  F32 custom_edge_thickness;
  LNX_WM_WindowClientArea *first_client_area;
  LNX_WM_WindowClientArea *last_client_area;
  Time last_title_bar_press_time;
};

////////////////////////////////
//~ rjf: State Bundle

typedef struct LNX_WM_State LNX_WM_State;
struct LNX_WM_State
{
  Arena *arena;
  Display *display;
  int wake_pipe[2];
  XIM xim;
  LNX_WM_Window *first_window;
  LNX_WM_Window *last_window;
  LNX_WM_Window *free_window;
  Atom wm_delete_window_atom;
  Atom wm_sync_request_atom;
  Atom wm_sync_request_counter_atom;
  Atom motif_wm_hints_atom;
  Atom net_wm_moveresize_atom;
  Atom net_wm_state_atom;
  Atom net_wm_state_maximized_horz_atom;
  Atom net_wm_state_maximized_vert_atom;
  Atom net_wm_state_fullscreen_atom;
  Atom wm_state_atom;
  LNX_WM_WindowClientArea *free_client_area;
  Cursor cursors[WM_Cursor_COUNT];
  WM_Cursor last_set_cursor;
  WM_SystemInfo gfx_info;
  Arena *clipboard_arena;
  String8 clipboard_text;
  String8 clipboard_html;
  B32 clipboard_owned;
  B32 clipboard_ascii;
  Window clipboard_owner;
  Atom clipboard_atom;
  Atom clipboard_utf8_atom;
  Atom clipboard_targets_atom;
  Atom clipboard_html_atom;
  Atom clipboard_incr_atom;
  Atom clipboard_property_atom;
  Atom net_wm_name_atom;
  Atom net_wm_icon_name_atom;
  Atom net_wm_icon_atom;
  long *icon_data;
  U64 icon_data_count;
  U64 icon_data_capacity;
  WM_Modifiers modifiers;
  U64 key_down[(WM_Key_COUNT+63)/64];
  Arena *selection_arena;
  String8 selection_text;
  // PRIMARY, served from selection_text.
  B32 selection_owned;
  B32 selection_ascii;
};

////////////////////////////////
//~ rjf: Globals

global LNX_WM_State *lnx_wm_state = 0;

////////////////////////////////
//~ rjf: Helpers

internal LNX_WM_Window *lnx_window_from_x11window(Window window);
internal void lnx_wm_window_presented(LNX_WM_Window *window);

#endif // LINUX_WINDOW_MANAGER_H
