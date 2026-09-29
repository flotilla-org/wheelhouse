// Native menu policy regressions: real configuration parser, map and WM policy.
// Licensed under the MIT license (https://opensource.org/license/mit/)
#define BUILD_CONSOLE_INTERFACE 1
#define WM_STUB 1
#include "base/base_inc.h"
#include "mdesk/mdesk.h"
#include "window_manager/window_manager_inc.h"
#include "config/config_inc.h"
#include "base/base_inc.c"
#include "mdesk/mdesk.c"
#include "window_manager/window_manager_inc.c"
#include "config/config_inc.c"

#include "native_menu_policy.c"

internal void
entry_point(CmdLine *cmdline)
{
  exit(!cfg_native_menu_diagnostics());
}
