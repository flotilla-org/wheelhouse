// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Build Options

#define BUILD_TITLE "UI Shell"
#define BUILD_VERSION_MAJOR 0
#define BUILD_VERSION_MINOR 1
#define BUILD_VERSION_PATCH 0
#define BUILD_RELEASE_PHASE_STRING_LITERAL "EXPERIMENTAL"
#define BUILD_ISSUES_LINK_STRING_LITERAL "the UI Shell project issue tracker"
#define BUILD_IPC_NAME_PREFIX "uishell"
#define BUILD_CRASH_DUMP_FILE_NAME_WIDE L"uishell_crash_dump.dmp"
#define RD_APP_STORAGE_DIR "uishell"
#define RD_APP_CONFIG_MAGIC "// uishell "
#define RD_APP_USER_FILE_NAME "default.uishell_user"
#define RD_APP_PROJECT_FILE_NAME "default.uishell_project"
#define RD_APP_LAST_USER_FILE_NAME "last_user"
#define RD_APP_LOG_FILE_NAME "ui_thread.uishell_log"
#define OS_FEATURE_GRAPHICAL 1

// Keep RAD-derived shell layers under explicit startup control.
#define D_INIT_MANUAL 1
#define WM_INIT_MANUAL 1
#define FP_INIT_MANUAL 1
#define R_INIT_MANUAL 1
#define FNT_INIT_MANUAL 1
#define RD_INIT_MANUAL 1

#define ARENA_TABLE_DEBUG BUILD_DEBUG

////////////////////////////////
//~ rjf: Includes

//- rjf: [h]
#include "base/base_inc.h"
#include "x64/x64.h"
#include "arm64/arm64.h"
#include "win32/win32_inc.h"
#include "artifact_cache/artifact_cache.h"
#include "rdi/rdi_local.h"
#include "mdesk/mdesk.h"
#include "window_manager/window_manager_inc.h"
#include "config/config_inc.h"
#include "content/content.h"
#include "file_stream/file_stream.h"
#include "text/text.h"
#include "mutable_text/mutable_text.h"
#include "arch/arch_inc.h"
#include "dbg_info/dbg_info.h"
#include "eval/eval_inc.h"
#include "dbg_engine/generated/dbg_engine.meta.h"
#include "dbg_engine/dbg_engine_user.h"
#include "eval_visualization/eval_visualization_inc.h"
#include "font_provider/font_provider_inc.h"
#include "render/render_inc.h"
#include "font_cache/font_cache.h"
#include "draw/draw.h"
#include "ui/ui_inc.h"
#include "raddbg/raddbg_inc.h"
#include "uishell/uishell_commands.h"
#include "uishell/uishell_meta.h"
#include "uishell/uishell_eval.h"
#include "uishell/uishell_dispatch.h"
#include "uishell/uishell_views.h"

//- rjf: [c]
#include "base/base_inc.c"
#include "x64/x64.c"
#include "arm64/arm64.c"
#include "win32/win32_inc.c"
#include "artifact_cache/artifact_cache.c"
#include "rdi/rdi_local.c"
#include "mdesk/mdesk.c"
#include "window_manager/window_manager_inc.c"
#include "config/config_inc.c"
#include "content/content.c"
#include "file_stream/file_stream.c"
#include "text/text.c"
#include "mutable_text/mutable_text.c"
#include "arch/arch_inc.c"
#include "uishell/uishell_debug_stubs.c"
#include "eval/eval_inc.c"
#include "eval_visualization/eval_visualization_inc.c"
#include "font_provider/font_provider_inc.c"
#include "render/render_inc.c"
#include "font_cache/font_cache.c"
#include "draw/draw.c"
#include "ui/ui_inc.c"
#include "uishell/uishell_eval.c"
#include "uishell/uishell_views.c"
#include "raddbg/raddbg_inc.c"

////////////////////////////////
//~ rjf: Top-Level Execution Types

typedef enum ExecMode
{
  ExecMode_Normal,
  ExecMode_Help,
}
ExecMode;

////////////////////////////////
//~ rjf: Per-Frame Entry Point

internal B32
frame(void)
{
  rd_frame();
  return rd_state->quit;
}

////////////////////////////////
//~ rjf: Entry Point

internal void
entry_point(CmdLine *cmd_line)
{
  ExecMode exec_mode = ExecMode_Normal;
  if(cmd_line_has_flag(cmd_line, str8_lit("?")) ||
     cmd_line_has_flag(cmd_line, str8_lit("help")))
  {
    exec_mode = ExecMode_Help;
  }

  switch(exec_mode)
  {
    default:
    case ExecMode_Normal:
    {
      wm_init();
      fp_init();
      r_init(cmd_line);
      fnt_init();
      rd_init(cmd_line);

      for(B32 quit = 0; !quit;)
      {
        quit = update();
      }
    }break;

    case ExecMode_Help:
    {
      wm_graphical_message(0,
                           str8_lit("UI Shell - Help"),
                           str8_lit("UI Shell is a native app-shell experiment derived from the RAD Debugger UI stack.\n\n"
                                    "--user:<path>\n"
                                    "Use to specify the location of a user file for window, panel, keybinding, theme, and visual settings.\n\n"
                                    "--project:<path>\n"
                                    "Use to specify the location of a project file for app-specific settings.\n\n"));
    }break;
  }
}
