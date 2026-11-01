// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#include "window_manager.c"
#if WM_STUB
# include "window_manager_stub.c"
#elif OS_WINDOWS
# include "win32/window_manager/win32_window_manager.c"
#elif OS_LINUX
// Each backend's wm_* functions compile as wm_<backend>_*.
# undef WM_HOOK_NAME
# define WM_HOOK_NAME(name) wm_x11_##name
WM_HookDecls
# include "linux/window_manager/linux_window_manager.c"
# undef WM_HOOK_NAME
# define WM_HOOK_NAME(name) wm_wayland_##name
WM_HookDecls
# include "linux/window_manager/wayland/linux_wayland_window_manager.c"
# undef WM_HOOK_NAME
# define WM_HOOK_NAME(name) wm_##name
# include "linux/window_manager/linux_wm_shared.c"
#elif OS_MAC
# include "mac/window_manager/mac_window_manager.c"
#else
# error Window manager layer not implemented for this operating system.
#endif

#if WM_BACKEND_MULTI
////////////////////////////////
//~ Backend Choice

// In fallback order: the first that starts is used. A newer backend goes
// after the one it would replace until it has proven itself (ADR 0015).
global WM_Backend wm_backends[] =
{
  {str8_lit_comp("x11"), wm_x11_init, WM_HookTableInit(x11)},
  {str8_lit_comp("wayland"), wm_wayland_init, WM_HookTableInit(wayland)},
};

// --window_manager:<name> (or WHEELHOUSE_WINDOW_MANAGER) names the only one
// to try; otherwise each is tried in order.
internal B32
wm_init(CmdLine *cmdln)
{
  String8 wanted = cmd_line_string(cmdln, str8_lit("window_manager"));
  char *wanted_env = getenv("WHEELHOUSE_WINDOW_MANAGER");
  if(wanted.size == 0 && wanted_env != 0) { wanted = str8_cstring(wanted_env); }
  B32 known = wanted.size == 0;
  for EachElement(idx, wm_backends)
  {
    WM_Backend *backend = &wm_backends[idx];
    if(wanted.size != 0 && !str8_match(wanted, backend->name, 0)) { continue; }
    known = 1;
    wm_backend = backend;
    if(backend->init(cmdln)) { return 1; }
    fprintf(stderr, "The %.*s window manager could not start.\n", str8_varg(backend->name));
  }
  wm_backend = 0;
  if(!known) { fprintf(stderr, "No window manager is called \"%.*s\".\n", str8_varg(wanted)); }
  fprintf(stderr, "No window manager could start.\n");
  exit(1);
  return 0;
}
#endif
