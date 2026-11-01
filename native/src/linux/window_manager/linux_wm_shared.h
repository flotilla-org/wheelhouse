// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef LINUX_WM_SHARED_H
#define LINUX_WM_SHARED_H

// What the X11 and Wayland window managers share.

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

internal WM_Key lnx_wm_key_from_keysym(U32 keysym, B32 *right_sided);
internal int lnx_wm_moveresize_code_from_border(Rng2F32 window_rect, F32 edge, F32 title_bar_thickness, B32 maximized, B32 in_client_area, Vec2F32 pos);
internal WM_Cursor lnx_wm_cursor_from_moveresize_code(int code, WM_Cursor otherwise);
internal void lnx_wm_open_in_browser(String8 url);

#endif // LINUX_WM_SHARED_H
