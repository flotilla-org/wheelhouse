// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UISHELL_VIEWS_H
#define UISHELL_VIEWS_H

////////////////////////////////
//~ rjf: Shell View Hooks

RD_VIEW_UI_FUNCTION_DEF(shell_text);
RD_VIEW_UI_FUNCTION_DEF(terminal);
RD_VIEW_UI_FUNCTION_DEF(binary);
RD_VIEW_UI_FUNCTION_DEF(bitmap);
RD_VIEW_UI_FUNCTION_DEF(color);
RD_VIEW_UI_FUNCTION_DEF(geo3d);
RD_VIEW_UI_FUNCTION_DEF(tweaks);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(shell_text);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(bitmap);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(color);
EV_EXPAND_RULE_INFO_FUNCTION_DEF(geo3d);
internal void uishell_watch_view_ui(Rng2F32 rect);
internal void uishell_register_view_ui_rules(Arena *arena, RD_ViewUIRuleMap *map);
internal void uishell_register_expand_rule_infos(Arena *arena, EV_ExpandRuleTable *table);

#endif // UISHELL_VIEWS_H
