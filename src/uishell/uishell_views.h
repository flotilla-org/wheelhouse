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
//~ rjf: Shell View Hooks

RD_VIEW_UI_FUNCTION_DEF(pinned_cards);
RD_VIEW_UI_FUNCTION_DEF(sidebar_section);
RD_VIEW_UI_FUNCTION_DEF(shell_text);
RD_VIEW_UI_FUNCTION_DEF(terminal);
RD_VIEW_UI_FUNCTION_DEF(jackstay);
RD_VIEW_UI_FUNCTION_DEF(scroll_region_fixture);
RD_VIEW_UI_FUNCTION_DEF(sessions);
RD_VIEW_UI_FUNCTION_DEF(binary);
RD_VIEW_UI_FUNCTION_DEF(bitmap);
RD_VIEW_UI_FUNCTION_DEF(color);
RD_VIEW_UI_FUNCTION_DEF(geo3d);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(shell_text);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(bitmap);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(color);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(geo3d);
internal void uishell_watch_view_ui(Rng2F32 rect);
internal void uishell_register_view_ui_rules(Arena *arena, RD_ViewUIRuleMap *map);
internal void uishell_register_expand_rule_infos(Arena *arena, EV_ExpandRuleTable *table);

#endif // UISHELL_VIEWS_H
