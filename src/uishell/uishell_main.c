// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Build Options

#define BUILD_TITLE "Wheelhouse"
#define BUILD_VERSION_MAJOR 0
#define BUILD_VERSION_MINOR 1
#define BUILD_VERSION_PATCH 0
#define BUILD_RELEASE_PHASE_STRING_LITERAL "EXPERIMENTAL"
#define BUILD_ISSUES_LINK_STRING_LITERAL "the Wheelhouse project issue tracker"
#define BUILD_IPC_NAME_PREFIX "uishell"
#define BUILD_CRASH_DUMP_FILE_NAME_WIDE L"uishell_crash_dump.dmp"
#define RD_APP_STORAGE_DIR "uishell"
#define RD_APP_CONFIG_MAGIC "// uishell "
#define RD_APP_USER_FILE_NAME "default.uishell_user"
#define RD_APP_PROJECT_FILE_NAME "default.uishell_project"
#define RD_APP_LAST_USER_FILE_NAME "last_user"
#define RD_APP_LOG_FILE_NAME "ui_thread.uishell_log"
#define OS_FEATURE_GRAPHICAL 1

// Keep shell layers under explicit startup control.
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
#include "win32/win32_inc.h"
#include "artifact_cache/artifact_cache.h"
#include "mdesk/mdesk.h"
#include "window_manager/window_manager_inc.h"
#include "config/config_inc.h"
#include "content/content.h"
#include "file_stream/file_stream.h"
#include "text/text.h"
#include "mutable_text/mutable_text.h"
#include "eval/eval_inc.h"
#include "eval_visualization/eval_visualization_inc.h"
#include "font_provider/font_provider_inc.h"
#include "render/render_inc.h"
#include "font_cache/font_cache.h"
#include "draw/draw.h"
#include "ui/ui_inc.h"
#include "shell/shell_inc.h"
#include "uishell/uishell_commands.h"
#include "uishell/uishell_meta.h"
#include "uishell/uishell_eval.h"
#include "uishell/uishell_dispatch.h"
#include "uishell/uishell_terminal_provider.h"
#include "uishell/uishell_terminal_glyph.h"
#include "uishell/uishell_views.h"

//- rjf: [c]
#include "base/base_inc.c"
#include "win32/win32_inc.c"
#include "artifact_cache/artifact_cache.c"
#include "mdesk/mdesk.c"
#include "window_manager/window_manager_inc.c"
#include "config/config_inc.c"
#include "content/content.c"
#include "file_stream/file_stream.c"
#include "text/text.c"
#include "mutable_text/mutable_text.c"
#include "eval/eval_inc.c"
#include "eval_visualization/eval_visualization_inc.c"
#include "font_provider/font_provider_inc.c"
#include "render/render_inc.c"
#include "font_cache/font_cache.c"
#include "draw/draw.c"
#include "ui/ui_inc.c"
#include "uishell/uishell_eval.c"
// stb_image is needed by the terminal glyph layer (Kitty PNG images). Pull the
// implementation in here, before that layer; shell_core.c's identical guarded
// block then becomes a no-op so there is still exactly one implementation.
#if !defined(STBI_INCLUDE_STB_IMAGE_H)
# define STB_IMAGE_IMPLEMENTATION
# define STBI_ONLY_PNG
# define STBI_ONLY_BMP
# include "third_party/stb/stb_image.h"
#endif
#include "uishell/uishell_terminal_glyph.c"
#include "uishell/uishell_views.c"
#include "uishell/uishell_jackstay.c"
#include "shell/shell_inc.c"
#include "uishell/uishell_scroll_diagnostics.c"

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
  uishell_sidebar_poll_live();
  uishell_jackstay_tick(1,0);
  rd_frame();
  uishell_jackstay_tick(0,rd_state->quit);
  return rd_state->quit && !uishell_jackstay_pending();
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

      String8 socket_path = cmd_line_string(cmd_line, str8_lit("andamento_socket"));
      if(socket_path.size != 0)
      {
        String8 config_path = cmd_line_string(cmd_line, str8_lit("andamento_config"));
        uishell_sidebar_live_config = data_from_file_path(rd_state->arena, config_path);
        uishell_sidebar_live = 1;
        U8 error[512] = {0};
        if(uishell_sidebar_live_config.size == 0)
        {
          fprintf(stderr, "Live sidebar requires --andamento_config:<KDL file>\n");
          abort_self(1);
        }
        // Validate configuration before accepting producers, even before a window opens.
        char *config_error = 0;
        Andamento *probe = andamento_create(uishell_sidebar_live_config.str, uishell_sidebar_live_config.size, &config_error);
        if(probe == 0)
        {
          fprintf(stderr, "Invalid live sidebar configuration: %s\n", config_error ? config_error : "unknown error");
          andamento_string_free(config_error);
          abort_self(1);
        }
        andamento_destroy(probe);
        uishell_ingress = wheelhouse_ingress_start(socket_path.str, socket_path.size, wm_send_wakeup_event, error, sizeof(error));
        if(uishell_ingress == 0) { fprintf(stderr, "Sidebar ingress: %s\n", error); abort_self(1); }
      }
      B32 run_scroll_diagnostics = cmd_line_has_flag(cmd_line, str8_lit("scroll_region_diagnostics"));
      B32 run_sidebar_diagnostics = cmd_line_has_flag(cmd_line, str8_lit("sidebar_diagnostics"));
      B32 run_terminal_glyph_diagnostics = cmd_line_has_flag(cmd_line, str8_lit("terminal_glyph_diagnostics"));
      String8 terminal_glyph_fixture_ppm_path = cmd_line_string(cmd_line, str8_lit("terminal_glyph_fixture_ppm"));
      for(B32 quit = 0; !quit;)
      {
        quit = update();
        if(run_scroll_diagnostics)
        {
          RD_WindowState *ws = rd_state->first_window_state;
          B32 ok = ws != &rd_nil_window_state && uishell_scroll_region_diagnostics(ws);
          abort_self(ok ? 0 : 1);
        }
        if(run_sidebar_diagnostics)
        {
          RD_WindowState *ws = rd_state->first_window_state;
          B32 ok = ws != &rd_nil_window_state && uishell_sidebar_diagnostics(cfg_node_from_id(ws->cfg_id));
          abort_self(ok ? 0 : 1);
        }
        if(run_terminal_glyph_diagnostics || terminal_glyph_fixture_ppm_path.size != 0)
        {
          FNT_Tag primary_font = fnt_tag_from_static_data_string(&rd_default_code_font_bytes);
          FNT_Tag main_fallback_font = fnt_tag_from_static_data_string(&rd_default_main_font_bytes);
          String8 *embedded_terminal_color_emoji_fallbacks[] =
          {
            &rd_terminal_noto_color_emoji_font_bytes,
          };
          String8 *embedded_terminal_fallbacks[] =
          {
            &rd_terminal_noto_emoji_font_bytes,
            &rd_terminal_noto_symbols_font_bytes,
            &rd_terminal_noto_symbols_2_font_bytes,
            &rd_terminal_noto_math_font_bytes,
          };
          B32 ok = 1;
          if(run_terminal_glyph_diagnostics)
          {
            ok = ok && uishell_terminal_glyph_diagnostics(primary_font,
                                                          main_fallback_font,
                                                          16.f,
                                                          FNT_RasterFlag_Smooth|FNT_RasterFlag_Hinted,
                                                          embedded_terminal_color_emoji_fallbacks,
                                                          ArrayCount(embedded_terminal_color_emoji_fallbacks),
                                                          embedded_terminal_fallbacks,
                                                          ArrayCount(embedded_terminal_fallbacks));
          }
          if(terminal_glyph_fixture_ppm_path.size != 0)
          {
            ok = ok && uishell_terminal_write_fixture_ppm(terminal_glyph_fixture_ppm_path,
                                                          primary_font,
                                                          main_fallback_font,
                                                          16.f,
                                                          FNT_RasterFlag_Smooth|FNT_RasterFlag_Hinted,
                                                          embedded_terminal_color_emoji_fallbacks,
                                                          ArrayCount(embedded_terminal_color_emoji_fallbacks),
                                                          embedded_terminal_fallbacks,
                                                          ArrayCount(embedded_terminal_fallbacks));
          }
          abort_self(ok ? 0 : 1);
        }
      }
      wheelhouse_ingress_stop(uishell_ingress);
      uishell_ingress = 0;
    }break;

    case ExecMode_Help:
    {
      wm_graphical_message(0,
                           str8_lit("Wheelhouse - Help"),
                           str8_lit("Wheelhouse composes workspaces from tabbed panels and embedded views.\n\n"
                                    "--user:<path>\n"
                                    "Use to specify the location of a user file for window, panel, keybinding, theme, and visual settings.\n\n"
                                    "--project:<path>\n"
                                    "Use to specify the location of a project file for app-specific settings.\n\n"
                                    "--andamento_socket:<path> --andamento_config:<KDL path>\n"
                                    "Accept live metadata over HTTP/UDS using the specified sidebar template.\n\n"
                                    "--scroll_region_fixture\nOpen the two-axis scrollbar fixture.\n\n"
                                    "--scroll_region_diagnostics\nRun scroll layout and interaction checks and exit.\n\n"
                                    "--sidebar_diagnostics\n"
                                    "Check the fixture sidebar workspace bridge and exit (use temporary user/project files).\n\n"
                                    "--terminal_fixture\n"
                                    "Open the deterministic terminal glyph fixture on startup.\n\n"
                                    "--terminal_glyph_diagnostics\n"
                                    "Run terminal glyph placement diagnostics and exit.\n\n"
                                    "--terminal_glyph_fixture_ppm:<path>\n"
                                    "Render the deterministic terminal glyph fixture through backend readback and write a PPM image.\n\n"
                                    "--terminal_glyph_trace\n"
                                    "Log live terminal glyph placement when terminal render generations update. Defaults to all rows.\n\n"
                                    "--terminal_glyph_trace_row:<n|all>\n"
                                    "Limit --terminal_glyph_trace to one visible row, or use all rows.\n\n"));
    }break;
  }
}
