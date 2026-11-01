// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef WINDOW_MANAGER_INC_H
#define WINDOW_MANAGER_INC_H

// Linux builds X11 and Wayland in and chooses one per run (ADR 0015); every
// other build has one window manager and calls it directly.
#if OS_LINUX && !WM_STUB
# define WM_BACKEND_MULTI 1
#else
# define WM_BACKEND_MULTI 0
#endif

#include "window_manager.h"
#if WM_STUB
# include "window_manager_stub.h"
#elif OS_WINDOWS
# include "win32/window_manager/win32_window_manager.h"
#elif OS_LINUX
# include "linux/window_manager/linux_window_manager.h"
#elif OS_MAC
# include "mac/window_manager/mac_window_manager.h"
#else
# error Window manager layer not implemented for this operating system.
#endif

#if WM_BACKEND_MULTI
typedef struct WM_Backend WM_Backend;
struct WM_Backend
{
  String8 name;
  B32 (*init)(CmdLine *cmdln);
  WM_HookFields
};
global WM_Backend *wm_backend = 0;
#endif

#endif // WINDOW_MANAGER_INC_H
