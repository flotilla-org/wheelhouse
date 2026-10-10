// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UISHELL_VIEWS_H
#define UISHELL_VIEWS_H

// Shared by sidebar, card and ordinary tab grips in the unity build.
enum { UIShell_DragThresholdPT = 10 };
read_only global F32 UIShell_GripWidthEM = 1.5f;

// Optional terminal measurements, independent of the workload driving the views.
// The collector owns storage and resets it at the start of each measured frame.
typedef struct UIShell_TerminalMetrics UIShell_TerminalMetrics;
struct UIShell_TerminalMetrics
{
  U64 provider_starts, provider_start_us, background_starts;
  U64 provider_resize_us, provider_update_us, empty_layout_resizes;
  U64 background_updates, snapshot_deferred;
  U64 deferred, glyph_us, rebuilds, cells_built, terminal_visits, updates;
};
global UIShell_TerminalMetrics *uishell_terminal_metrics;

////////////////////////////////
//~ rjf: Renderer UI Functions (uishell_renderers.h)

WH_VIEW_UI_FUNCTION_DEF(pinned_cards);
WH_VIEW_UI_FUNCTION_DEF(sidebar_section);
WH_VIEW_UI_FUNCTION_DEF(shell_text);
WH_VIEW_UI_FUNCTION_DEF(terminal);
WH_VIEW_UI_FUNCTION_DEF(jackstay);
WH_VIEW_UI_FUNCTION_DEF(scroll_region_fixture);
WH_VIEW_UI_FUNCTION_DEF(sessions);
WH_VIEW_UI_FUNCTION_DEF(binary);
WH_VIEW_UI_FUNCTION_DEF(bitmap);
WH_VIEW_UI_FUNCTION_DEF(color);
WH_VIEW_UI_FUNCTION_DEF(geo3d);
WH_VIEW_UI_FUNCTION_DEF(placeholder);
WH_VIEW_UI_FUNCTION_DEF(watch);
WH_VIEW_UI_FUNCTION_DEF(getting_started);
WH_VIEW_UI_FUNCTION_DEF(pending);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(shell_text);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(bitmap);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(color);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(geo3d);

// The pending page's state: commands sent to it while it waits, replayed to
// the View it becomes.
typedef struct UIShell_PendingViewState UIShell_PendingViewState;
struct UIShell_PendingViewState
{
  Arena *deferred_cmd_arena;
  UIShell_CmdList deferred_cmds;
};

#endif // UISHELL_VIEWS_H
