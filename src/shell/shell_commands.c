// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Shell Menu Metadata

#include "shell_app_hooks.h"

internal RD_AppMenuSpecList
uishell_shell_window_menu_specs(void)
{
#define UIShell_MenuCmd(name, cp) {0, str8_lit_comp(name), cp}
#define UIShell_MenuSep()        {1, {0}, 0}
  local_persist RD_AppMenuItemSpec window_items[] =
  {
    UIShell_MenuCmd("open_window", 'w'),
    UIShell_MenuCmd("close_window", 'c'),
    UIShell_MenuCmd("toggle_fullscreen", 'f'),
    UIShell_MenuSep(),
    UIShell_MenuCmd("workspace_settings", 'p'),
    UIShell_MenuCmd("window_settings", 's'),
  };
  local_persist RD_AppMenuSpec specs[] =
  {
    {str8_lit_comp("Window"), 'w', WM_Key_W, ArrayCount(window_items), window_items},
  };
  RD_AppMenuSpecList result = {ArrayCount(specs), specs};
#undef UIShell_MenuSep
#undef UIShell_MenuCmd
  return result;
}

internal RD_AppMenuSpecList
uishell_shell_panel_menu_specs(void)
{
#define UIShell_MenuCmd(name, cp) {0, str8_lit_comp(name), cp}
#define UIShell_MenuSep()        {1, {0}, 0}
  local_persist RD_AppMenuItemSpec panel_items[] =
  {
    UIShell_MenuCmd("new_panel_up", 'u'),
    UIShell_MenuCmd("new_panel_down", 'd'),
    UIShell_MenuCmd("new_panel_right", 'r'),
    UIShell_MenuCmd("new_panel_left", 'l'),
    UIShell_MenuCmd("close_panel", 'o'),
    UIShell_MenuSep(),
    UIShell_MenuCmd("next_panel", 'n'),
    UIShell_MenuCmd("prev_panel", 'p'),
    UIShell_MenuSep(),
    UIShell_MenuCmd("tab_bar_top", 0),
    UIShell_MenuCmd("tab_bar_bottom", 0),
  };
  local_persist RD_AppMenuSpec specs[] =
  {
    {str8_lit_comp("Panel"), 'p', WM_Key_P, ArrayCount(panel_items), panel_items},
  };
  RD_AppMenuSpecList result = {ArrayCount(specs), specs};
#undef UIShell_MenuSep
#undef UIShell_MenuCmd
  return result;
}

internal RD_AppMenuSpecList
uishell_shell_tab_menu_specs(void)
{
#define UIShell_MenuCmd(name, cp) {0, str8_lit_comp(name), cp}
#define UIShell_MenuSep()        {1, {0}, 0}
  local_persist RD_AppMenuItemSpec tab_items[] =
  {
    UIShell_MenuCmd("close_tab", 'c'),
    UIShell_MenuCmd("duplicate_tab", 'd'),
    UIShell_MenuSep(),
    UIShell_MenuCmd("move_tab_left", 'l'),
    UIShell_MenuCmd("move_tab_right", 'r'),
    UIShell_MenuSep(),
    UIShell_MenuCmd("next_tab", 'n'),
    UIShell_MenuCmd("prev_tab", 'p'),
  };
  local_persist RD_AppMenuSpec specs[] =
  {
    {str8_lit_comp("Tab"), 'b', WM_Key_V, ArrayCount(tab_items), tab_items},
  };
  RD_AppMenuSpecList result = {ArrayCount(specs), specs};
#undef UIShell_MenuSep
#undef UIShell_MenuCmd
  return result;
}

internal RD_AppMenuSpecList
uishell_shell_help_menu_specs(void)
{
  local_persist RD_AppMenuSpec specs[] =
  {
    {str8_lit_comp("Help"), 'h', WM_Key_H, 0, 0},
  };
  RD_AppMenuSpecList result = {ArrayCount(specs), specs};
  return result;
}

////////////////////////////////
//~ rjf: Shell Command Dispatch Helpers

internal void
uishell_push_window_ui_event(UI_Event *event)
{
  CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
  RD_WindowState *ws = rd_window_state_from_cfg(window);
  if(ws != &rd_nil_window_state)
  {
    ui_event_list_push(rd_frame_arena(), &ws->ui_events, event);
  }
}

internal F32
uishell_panel_anim(UI_Key key, F32 target, F32 initial, B32 reset)
{
  UI_AnimParams params =
  {
    .target = target,
    .initial = initial,
    .reset = reset,
    .rate = rd_state->menu_animation_rate,
  };
  return ui_anim_(key, &params);
}

internal B32
uishell_dispatch_app_command(String8 name)
{
  B32 result = 1;

  if(str8_match(name, str8_lit("exit"), 0))
  {
    UISHELL_APP_SAVE_BEFORE_EXIT();
    rd_state->quit = 1;
  }
  else if(str8_match(name, str8_lit("wm_event"), 0))
  {
    WM_Event *wm_event = uishell_regs()->wm_event;
    if(wm_event != 0)
    {
      RD_WindowState *ws = rd_window_state_from_os_handle(wm_event->window);
      if(ws != &rd_nil_window_state)
      {
        UI_Event ui_event = zero_struct;
        UI_EventKind kind = UI_EventKind_Null;
        switch(wm_event->kind)
        {
          default:{}break;
          case WM_EventKind_Press:     {kind = UI_EventKind_Press;}break;
          case WM_EventKind_Release:   {kind = UI_EventKind_Release;}break;
          case WM_EventKind_MouseMove: {kind = UI_EventKind_MouseMove;}break;
          case WM_EventKind_Text:      {kind = UI_EventKind_Text;}break;
          case WM_EventKind_Scroll:    {kind = UI_EventKind_Scroll;}break;
          case WM_EventKind_FileDrop:  {kind = UI_EventKind_FileDrop;}break;
        }
        ui_event.kind         = kind;
        ui_event.key          = wm_event->key;
        ui_event.modifiers    = wm_event->modifiers;
        ui_event.string       = wm_event->character ? str8_from_32(ui_build_arena(), str32(&wm_event->character, 1)) : str8_zero();
        ui_event.paths        = str8_list_copy(ui_build_arena(), &wm_event->strings);
        ui_event.pos          = wm_event->pos;
        ui_event.delta_2f32   = wm_event->delta;
        ui_event.timestamp_us = wm_event->timestamp_us;
        CFG_Node *focused_view = cfg_node_from_id(uishell_regs()->view);
        B32 terminal_claims_keyboard_input = (!rd_state->popup_active &&
                                              !ws->query_is_active &&
                                              !ws->menu_bar_focused &&
                                              str8_match(focused_view->string, str8_lit("terminal"), 0) &&
                                              !(wm_event->modifiers & WM_Modifier_Super) &&
                                              (wm_event->kind == WM_EventKind_Press ||
                                               wm_event->kind == WM_EventKind_Release ||
                                               wm_event->kind == WM_EventKind_Text));
        if(terminal_claims_keyboard_input)
        {
          ui_event.flags |= UI_EventFlag_SkipDefaultFocusNav;
        }
        ui_event_list_push(rd_frame_arena(), &ws->ui_events, &ui_event);
      }
    }
  }
  else if(str8_match(name, str8_lit("undo"), 0) ||
          str8_match(name, str8_lit("redo"), 0) ||
          str8_match(name, str8_lit("go_back"), 0) ||
          str8_match(name, str8_lit("go_forward"), 0))
  {
    // Reserved app-level command names. No shell-global behavior yet.
  }
  else
  {
    result = 0;
  }

  return result;
}

internal B32
uishell_dispatch_ui_event_command(String8 name)
{
  B32 result = 1;
  UI_Event event = zero_struct;
  B32 push_event = 1;

  if(str8_match(name, str8_lit("edit"), 0))
  {
    event.kind = UI_EventKind_Press;
    event.slot = UI_EventActionSlot_Edit;
  }
  else if(str8_match(name, str8_lit("accept"), 0))
  {
    event.kind = UI_EventKind_Press;
    event.slot = UI_EventActionSlot_Accept;
  }
  else if(str8_match(name, str8_lit("cancel"), 0))
  {
    event.kind = UI_EventKind_Press;
    event.slot = UI_EventActionSlot_Cancel;
  }
  else if(str8_match(name, str8_lit("focus_menu"), 0))
  {
    event.kind = UI_EventKind_Press;
    event.slot = UI_EventActionSlot_FocusMenu;
  }
  else if(str8_match(name, str8_lit("move_left"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_PickSelectSide|UI_EventFlag_ZeroDeltaOnSelect|UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(-1, +0);
  }
  else if(str8_match(name, str8_lit("move_right"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_PickSelectSide|UI_EventFlag_ZeroDeltaOnSelect|UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+1, +0);
  }
  else if(str8_match(name, str8_lit("move_up"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_ExplicitDirectional|UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else if(str8_match(name, str8_lit("move_down"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_ExplicitDirectional|UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_left_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(-1, +0);
  }
  else if(str8_match(name, str8_lit("move_right_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+1, +0);
  }
  else if(str8_match(name, str8_lit("move_up_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional|UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else if(str8_match(name, str8_lit("move_down_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional|UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_left_chunk"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(-1, +0);
  }
  else if(str8_match(name, str8_lit("move_right_chunk"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(+1, +0);
  }
  else if(str8_match(name, str8_lit("move_up_chunk"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_ExplicitDirectional|UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else if(str8_match(name, str8_lit("move_down_chunk"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_ExplicitDirectional|UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_up_page"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Page;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else if(str8_match(name, str8_lit("move_down_page"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Page;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_up_whole"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Whole;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else if(str8_match(name, str8_lit("move_down_whole"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Whole;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_left_chunk_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(-1, +0);
  }
  else if(str8_match(name, str8_lit("move_right_chunk_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(+1, +0);
  }
  else if(str8_match(name, str8_lit("move_up_chunk_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else if(str8_match(name, str8_lit("move_down_chunk_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark|UI_EventFlag_ExplicitDirectional;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_up_page_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark;
    event.delta_unit = UI_EventDeltaUnit_Page;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else if(str8_match(name, str8_lit("move_down_page_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark;
    event.delta_unit = UI_EventDeltaUnit_Page;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_up_whole_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark;
    event.delta_unit = UI_EventDeltaUnit_Whole;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else if(str8_match(name, str8_lit("move_down_whole_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark;
    event.delta_unit = UI_EventDeltaUnit_Whole;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_up_reorder"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_Reorder;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else if(str8_match(name, str8_lit("move_down_reorder"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_Reorder;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_home"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.delta_unit = UI_EventDeltaUnit_Line;
    event.delta_2s32 = v2s32(-1, +0);
  }
  else if(str8_match(name, str8_lit("move_end"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.delta_unit = UI_EventDeltaUnit_Line;
    event.delta_2s32 = v2s32(+1, +0);
  }
  else if(str8_match(name, str8_lit("move_home_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark;
    event.delta_unit = UI_EventDeltaUnit_Line;
    event.delta_2s32 = v2s32(-1, +0);
  }
  else if(str8_match(name, str8_lit("move_end_select"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_KeepMark;
    event.delta_unit = UI_EventDeltaUnit_Line;
    event.delta_2s32 = v2s32(+1, +0);
  }
  else if(str8_match(name, str8_lit("select_all"), 0))
  {
    UI_Event event1 = zero_struct;
    event1.kind = UI_EventKind_Navigate;
    event1.delta_unit = UI_EventDeltaUnit_Whole;
    event1.delta_2s32 = v2s32(-1, +0);
    uishell_push_window_ui_event(&event1);

    UI_Event event2 = zero_struct;
    event2.kind = UI_EventKind_Navigate;
    event2.flags = UI_EventFlag_KeepMark;
    event2.delta_unit = UI_EventDeltaUnit_Whole;
    event2.delta_2s32 = v2s32(+1, +0);
    uishell_push_window_ui_event(&event2);
    push_event = 0;
  }
  // NOTE: deletion edits carry a delta describing text direction, NOT
  // navigation intent — without SkipDefaultFocusNav, a default-focus-nav
  // root that builds before the focused text field (the sidebar) consumes
  // them as widget cycling & the field never sees the deletion
  else if(str8_match(name, str8_lit("delete_single"), 0))
  {
    event.kind = UI_EventKind_Edit;
    event.flags = UI_EventFlag_Delete|UI_EventFlag_ZeroDeltaOnSelect|UI_EventFlag_SkipDefaultFocusNav;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+1, +0);
  }
  else if(str8_match(name, str8_lit("delete_chunk"), 0))
  {
    event.kind = UI_EventKind_Edit;
    event.flags = UI_EventFlag_Delete|UI_EventFlag_ZeroDeltaOnSelect|UI_EventFlag_SkipDefaultFocusNav;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(+1, +0);
  }
  else if(str8_match(name, str8_lit("backspace_single"), 0))
  {
    event.kind = UI_EventKind_Edit;
    event.flags = UI_EventFlag_Delete|UI_EventFlag_ZeroDeltaOnSelect|UI_EventFlag_SkipDefaultFocusNav;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(-1, +0);
  }
  else if(str8_match(name, str8_lit("backspace_chunk"), 0))
  {
    event.kind = UI_EventKind_Edit;
    event.flags = UI_EventFlag_Delete|UI_EventFlag_ZeroDeltaOnSelect|UI_EventFlag_SkipDefaultFocusNav;
    event.delta_unit = UI_EventDeltaUnit_Word;
    event.delta_2s32 = v2s32(-1, +0);
  }
  else if(str8_match(name, str8_lit("copy"), 0))
  {
    event.kind = UI_EventKind_Edit;
    event.flags = UI_EventFlag_Copy|UI_EventFlag_KeepMark;
  }
  else if(str8_match(name, str8_lit("cut"), 0))
  {
    event.kind = UI_EventKind_Edit;
    event.flags = UI_EventFlag_Copy|UI_EventFlag_Delete;
  }
  else if(str8_match(name, str8_lit("paste"), 0))
  {
    event.kind = UI_EventKind_Text;
    event.flags = UI_EventFlag_Paste;
    event.string = wm_get_clipboard_text(rd_frame_arena());
  }
  else if(str8_match(name, str8_lit("insert_text"), 0))
  {
    event.kind = UI_EventKind_Text;
    event.string = uishell_regs()->string;
  }
  else if(str8_match(name, str8_lit("move_next"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+0, +1);
  }
  else if(str8_match(name, str8_lit("move_prev"), 0))
  {
    event.kind = UI_EventKind_Navigate;
    event.flags = UI_EventFlag_Secondary;
    event.delta_unit = UI_EventDeltaUnit_Char;
    event.delta_2s32 = v2s32(+0, -1);
  }
  else
  {
    result = 0;
    push_event = 0;
  }

  if(push_event)
  {
    uishell_push_window_ui_event(&event);
  }
  return result;
}

internal B32
uishell_dispatch_command_palette_command(String8 name)
{
  B32 result = 1;

  if(str8_match(name, str8_lit("open_palette"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_current_regs(scratch.arena);
    CFG_Node *window = workspace_mount.window_cfg;
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_Node *tab = panel_tree.focused->selected_tab;
    String8List exprs = {0};
    str8_list_pushf(scratch.arena, &exprs, "query:commands");
    if(tab != &cfg_nil_node)
    {
      str8_list_pushf(scratch.arena, &exprs, "query:config.$%I64x", tab->id);
    }
    if(window != &cfg_nil_node)
    {
      str8_list_pushf(scratch.arena, &exprs, "query:config.$%I64x", window->id);
    }
    UISHELL_APP_PUSH_PALETTE_QUERY_ROOTS(scratch.arena, &exprs);
    String8 expr = str8_list_join(scratch.arena, &exprs, &(StringJoin){.sep = str8_lit(", ")});
    UIShell_RegsScope(.expr = expr, .do_implicit_root = 1, .do_lister = 1, .do_big_rows = 1, .view = tab->id, .tab = tab->id)
    {
      uishell_push_cmd_current(str8_lit("push_query"));
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("run_command"), 0) ||
          str8_match(name, str8_lit("open_tab"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    UIShell_AppCmdInfo info = uishell_app_cmd_info_from_string(uishell_regs()->cmd_name);
    if(info.string.size == 0)
    {
      result = 0;
    }
    else if(!(info.query_flags & UIShell_QueryFlag_Required))
    {
      String8 cmd_name = uishell_regs()->cmd_name;
      UIShell_RegsScope(.cmd_name = str8_zero())
      {
        uishell_push_cmd_current(cmd_name);
      }
    }
    else if(info.query_slot == UIShell_AppRegSlot_FilePath && rd_setting_b32_from_name(str8_lit("use_native_file_system_dialog")))
    {
      CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
      CFG_Node *current_path = cfg_node_child_from_string(user, str8_lit("current_path"));
      String8 current_path_string = current_path->first->string;
      if(current_path_string.size == 0)
      {
        current_path_string = path_normalized_from_string(scratch.arena, get_current_path(scratch.arena));
      }
      String8 file_path = wm_graphical_pick_file(scratch.arena, current_path_string);
      file_path = path_normalized_from_string(scratch.arena, file_path);
      if(file_path.size != 0)
      {
        String8 cmd_name = uishell_regs()->cmd_name;
        UIShell_RegsScope(.cmd_name = str8_zero(), .file_path = file_path)
        {
          uishell_push_cmd_current(cmd_name);
        }
        UIShell_RegsScope(.file_path = str8_chop_last_slash(file_path))
        {
          uishell_push_cmd_current(str8_lit("set_current_path"));
        }
      }
    }
    else
    {
      UIShell_RegsScope(.do_implicit_root = 1, .do_lister = info.query_expr.size != 0)
      {
        uishell_push_cmd_current(str8_lit("push_query"));
      }
    }
    scratch_end(scratch);
  }
  else
  {
    result = 0;
  }

  return result;
}

internal B32
uishell_dispatch_tab_command(String8 name)
{
  B32 result = 1;

  if(str8_match(name, str8_lit("focus_tab"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *tab = cfg_node_from_id(uishell_regs()->tab);
    CFG_Node *panel = tab->parent;
    if(panel == &cfg_nil_node)
    {
      panel = cfg_node_from_id(uishell_regs()->panel);
    }
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, panel);
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_PanelNode *panel_node = cfg_panel_node_from_tree_cfg(panel_tree.root, panel);
    CFG_Node *selection_cfg = &cfg_nil_node;
    for(CFG_NodePtrNode *n = panel_node->tabs.first; n != 0; n = n->next)
    {
      CFG_Node *tab_selection_cfg = cfg_node_child_from_string(n->v, str8_lit("selected"));
      if(selection_cfg == &cfg_nil_node)
      {
        selection_cfg = tab_selection_cfg;
        cfg_node_unhook(rd_state->cfg, n->v, selection_cfg);
      }
      else for(CFG_Node *s = tab_selection_cfg; s != &cfg_nil_node; s = cfg_node_child_from_string(n->v, str8_lit("selected")))
      {
        cfg_node_release(rd_state->cfg, s);
      }
    }
    if(selection_cfg == &cfg_nil_node)
    {
      selection_cfg = cfg_node_alloc(rd_state->cfg);
      cfg_node_equip_string(rd_state->cfg, selection_cfg, str8_lit("selected"));
    }
    if(tab != &cfg_nil_node)
    {
      cfg_node_insert_child(rd_state->cfg, tab, &cfg_nil_node, selection_cfg);
    }
    else
    {
      cfg_node_release(rd_state->cfg, selection_cfg);
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("next_tab"), 0) ||
          str8_match(name, str8_lit("prev_tab"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    B32 is_next = str8_match(name, str8_lit("next_tab"), 0);
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_current_regs(scratch.arena);
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_PanelNode *focused = panel_tree.focused;
    CFG_NodePtrNode *selected_tab_n = 0;
    if(is_next)
    {
      for(CFG_NodePtrNode *n = focused->tabs.first; n != 0; n = n->next)
      {
        if(n->v == focused->selected_tab)
        {
          selected_tab_n = n;
          break;
        }
      }
    }
    else
    {
      for(CFG_NodePtrNode *n = focused->tabs.last; n != 0; n = n->prev)
      {
        if(n->v == focused->selected_tab)
        {
          selected_tab_n = n;
          break;
        }
      }
    }
    CFG_Node *next_selected_tab = &cfg_nil_node;
    U64 idx = 0;
    for(CFG_NodePtrNode *tab_n = selected_tab_n;
        tab_n != 0 && (tab_n != selected_tab_n || idx == 0);
        (is_next ? ((tab_n->next == 0) ? (tab_n = focused->tabs.first) : (tab_n = tab_n->next))
                 : ((tab_n->prev == 0) ? (tab_n = focused->tabs.last) : (tab_n = tab_n->prev))), idx += 1)
    {
      if(!rd_cfg_is_project_filtered(tab_n->v) && tab_n != selected_tab_n)
      {
        next_selected_tab = tab_n->v;
        break;
      }
    }
    if(next_selected_tab != &cfg_nil_node)
    {
      UIShell_RegsScope(.tab = next_selected_tab->id)
      {
        uishell_push_cmd_current(str8_lit("focus_tab"));
      }
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("move_tab_right"), 0) ||
          str8_match(name, str8_lit("move_tab_left"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *tab = cfg_node_from_id(uishell_regs()->tab);
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, tab);
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_PanelNode *panel = cfg_panel_node_from_tree_cfg(panel_tree.root, tab->parent);
    CFG_NodePtrList filtered_tabs = {0};
    for(CFG_NodePtrNode *n = panel->tabs.first; n != 0; n = n->next)
    {
      if(rd_cfg_is_project_filtered(n->v))
      {
        continue;
      }
      cfg_node_ptr_list_push(scratch.arena, &filtered_tabs, n->v);
    }
    CFG_Node *tab_prev2 = &cfg_nil_node;
    CFG_Node *tab_prev = &cfg_nil_node;
    CFG_Node *tab_next = &cfg_nil_node;
    {
      CFG_Node *prev2 = &cfg_nil_node;
      CFG_Node *prev = &cfg_nil_node;
      CFG_Node *next = &cfg_nil_node;
      for(CFG_NodePtrNode *n = filtered_tabs.first; n != 0; (prev2 = prev, prev = n->v, n = n->next))
      {
        next = n->next ? n->next->v : &cfg_nil_node;
        if(n->v == tab)
        {
          tab_prev2 = prev2;
          tab_prev = prev;
          tab_next = next;
          break;
        }
      }
    }
    CFG_Node *new_prev = (str8_match(name, str8_lit("move_tab_right"), 0) ? tab_next : tab_prev2);
    if(new_prev == tab_prev && filtered_tabs.last)
    {
      new_prev = filtered_tabs.last->v;
    }
    UIShell_RegsScope(.dst_panel = panel->cfg->id, .view = tab->id, .prev_tab = new_prev->id)
    {
      uishell_push_cmd_current(str8_lit("move_view"));
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("build_tab"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    String8 expr_file_path = rd_file_path_from_eval_string(scratch.arena, uishell_regs()->expr);
    CFG_Node *panel = cfg_node_from_id(uishell_regs()->panel);
    if(panel == &cfg_nil_node)
    {
      CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
      if(window == &cfg_nil_node)
      {
        window = cfg_node_from_id(rd_state->last_focused_window);
      }
      if(window == &cfg_nil_node)
      {
        CFG_NodePtrList windows = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("window"));
        if(windows.first != 0)
        {
          window = windows.first->v;
        }
      }
      if(window != &cfg_nil_node)
      {
        UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, window);
        CFG_PanelTree panel_tree = workspace_mount.panel_tree;
        panel = panel_tree.focused->cfg;
      }
    }
    if(panel == &cfg_nil_node)
    {
      log_user_errorf("Couldn't open a new tab because no panel was available.");
    }
    else
    {
      CFG_Node *tab = cfg_node_new(rd_state->cfg, panel, uishell_regs()->string);
      CFG_Node *expr = cfg_node_new(rd_state->cfg, tab, str8_lit("expression"));
      cfg_node_new(rd_state->cfg, expr, uishell_regs()->expr);
      if(expr_file_path.size != 0)
      {
        CFG_Node *project = cfg_node_new(rd_state->cfg, tab, str8_lit("project"));
        cfg_node_new(rd_state->cfg, project, rd_state->project_path);
      }
      UIShell_RegsScope(.tab = tab->id)
      {
        uishell_push_cmd_current(str8_lit("focus_tab"));
      }
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("duplicate_tab"), 0))
  {
    CFG_Node *src = cfg_node_from_id(uishell_regs()->tab);
    CFG_Node *dst = cfg_node_deep_copy(rd_state->cfg, src);
    cfg_node_insert_child(rd_state->cfg, src->parent, src, dst);
    UIShell_RegsScope(.tab = dst->id)
    {
      uishell_push_cmd_current(str8_lit("focus_tab"));
    }
  }
  else if(str8_match(name, str8_lit("close_tab"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *tab = cfg_node_from_id(uishell_regs()->tab);
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, tab);
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_PanelNode *panel = cfg_panel_node_from_tree_cfg(panel_tree.root, tab->parent);
    if(panel->selected_tab == tab)
    {
      B32 found_selected = 0;
      CFG_Node *next_selected_tab = &cfg_nil_node;
      for(CFG_NodePtrNode *n = panel->tabs.first; n != 0; n = n->next)
      {
        if(n->v == panel->selected_tab)
        {
          found_selected = 1;
        }
        else if(!rd_cfg_is_project_filtered(n->v))
        {
          next_selected_tab = n->v;
          if(found_selected)
          {
            break;
          }
        }
      }
      UIShell_RegsScope(.tab = next_selected_tab->id)
      {
        uishell_push_cmd_current(str8_lit("focus_tab"));
      }
    }
    cfg_node_release(rd_state->cfg, tab);
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("move_view"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
    CFG_Node *prev_tab = cfg_node_from_id(uishell_regs()->prev_tab);
    CFG_Node *src_panel = view->parent;
    CFG_Node *dst_panel = cfg_node_from_id(uishell_regs()->dst_panel);
    if(dst_panel != &cfg_nil_node && prev_tab != view)
    {
      cfg_node_unhook(rd_state->cfg, src_panel, view);
      cfg_node_insert_child(rd_state->cfg, dst_panel, prev_tab, view);
      UIShell_RegsScope(.panel = dst_panel->id, .tab = view->id)
      {
        uishell_push_cmd_current(str8_lit("focus_tab"));
      }
      UIShell_RegsScope(.panel = dst_panel->id)
      {
        uishell_push_cmd_current(str8_lit("focus_panel"));
      }
      UIShell_WorkspaceMount src_workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, src_panel);
      CFG_PanelTree src_panel_tree = src_workspace_mount.panel_tree;
      CFG_PanelNode *src_panel_node = cfg_panel_node_from_tree_cfg(src_panel_tree.root, src_panel);
      B32 src_panel_is_empty = 0;
      if(src_panel != dst_panel)
      {
        src_panel_is_empty = 1;
        for(CFG_NodePtrNode *n = src_panel_node->tabs.first; n != 0; n = n->next)
        {
          if(!rd_cfg_is_project_filtered(n->v))
          {
            UIShell_RegsScope(.panel = src_panel->id, .tab = n->v->id)
            {
              uishell_push_cmd_current(str8_lit("focus_tab"));
            }
            src_panel_is_empty = 0;
            break;
          }
        }
      }
      if(src_panel_is_empty)
      {
        UIShell_RegsScope(.panel = src_panel->id)
        {
          uishell_push_cmd_current(str8_lit("close_panel"));
        }
      }
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("copy_tab_full_path"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *tab = cfg_node_from_id(uishell_regs()->tab);
    String8 expr = rd_expr_from_cfg(tab);
    String8 full_path = rd_file_path_from_eval_string(scratch.arena, expr);
    wm_set_clipboard_text(full_path);
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("tab_bar_top"), 0))
  {
    CFG_Node *panel = cfg_node_from_id(uishell_regs()->panel);
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(panel, str8_lit("tabs_on_bottom")));
  }
  else if(str8_match(name, str8_lit("tab_bar_bottom"), 0))
  {
    CFG_Node *panel = cfg_node_from_id(uishell_regs()->panel);
    cfg_node_child_from_string_or_alloc(rd_state->cfg, panel, str8_lit("tabs_on_bottom"));
  }
  else if(str8_match(name, str8_lit("tab_settings"), 0))
  {
    String8 expr = push_str8f(rd_frame_arena(), "query:config.$%I64x", uishell_regs()->tab);
    UIShell_RegsScope(.expr = expr, .do_implicit_root = 1, .do_big_rows = 1, .do_lister = 1)
    {
      uishell_push_cmd_current(str8_lit("push_query"));
    }
  }
  else
  {
    result = 0;
  }

  return result;
}

internal B32
uishell_dispatch_panel_command(String8 name)
{
  B32 result = 1;

  if(str8_match(name, str8_lit("reset_to_default_panels"), 0) ||
     str8_match(name, str8_lit("reset_to_compact_panels"), 0) ||
     str8_match(name, str8_lit("reset_to_simple_panels"), 0))
  {
    CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
    UISHELL_APP_RESET_PANELS(window);
  }
  else if(str8_match(name, str8_lit("new_panel_left"), 0) ||
          str8_match(name, str8_lit("new_panel_up"), 0) ||
          str8_match(name, str8_lit("new_panel_right"), 0) ||
          str8_match(name, str8_lit("new_panel_down"), 0) ||
          str8_match(name, str8_lit("split_panel"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    Dir2 split_dir = Dir2_Invalid;
    CFG_Node *split_panel = &cfg_nil_node;
    B32 do_dragdrop_split = 0;
    if(str8_match(name, str8_lit("new_panel_left"), 0))
    {
      split_dir = Dir2_Left;
    }
    else if(str8_match(name, str8_lit("new_panel_up"), 0))
    {
      split_dir = Dir2_Up;
    }
    else if(str8_match(name, str8_lit("new_panel_right"), 0))
    {
      split_dir = Dir2_Right;
    }
    else if(str8_match(name, str8_lit("new_panel_down"), 0))
    {
      split_dir = Dir2_Down;
    }
    else
    {
      split_dir = uishell_regs()->dir2;
      split_panel = cfg_node_from_id(uishell_regs()->dst_panel);
      do_dragdrop_split = 1;
    }

    if(split_dir != Dir2_Invalid)
    {
      Axis2 split_axis = axis2_from_dir2(split_dir);
      Side split_side = side_from_dir2(split_dir);
      if(split_panel == &cfg_nil_node)
      {
        split_panel = cfg_node_from_id(uishell_regs()->panel);
      }
      CFG_Node *new_panel_cfg = &cfg_nil_node;
      UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, split_panel);
      CFG_PanelTree panel_tree = workspace_mount.panel_tree;
      CFG_PanelNode *panel_root = panel_tree.root;
      CFG_PanelNode *panel = cfg_panel_node_from_tree_cfg(panel_root, split_panel);
      CFG_PanelNode *parent = panel->parent;

      if(parent != &cfg_nil_panel_node && parent->split_axis == split_axis)
      {
        CFG_Node *parent_cfg = parent->cfg;
        CFG_Node *panel_cfg = panel->cfg;
        CFG_Node *new_cfg = cfg_node_alloc(rd_state->cfg);
        cfg_node_insert_child(rd_state->cfg, parent_cfg, split_side == Side_Max ? panel_cfg : panel_cfg->prev, new_cfg);
        cfg_node_equip_stringf(rd_state->cfg, new_cfg, "%f", 1.f/(parent->child_count+1));
        for(CFG_PanelNode *child = parent->first; child != &cfg_nil_panel_node; child = child->next)
        {
          F32 old_pct = child->pct_of_parent;
          F32 new_pct = old_pct * ((F32)(parent->child_count) / (parent->child_count+1));
          cfg_node_equip_stringf(rd_state->cfg, child->cfg, "%f", new_pct);
        }
        new_panel_cfg = new_cfg;
      }
      else
      {
        CFG_Node *split_panel_prev = panel->prev->cfg;
        CFG_Node *new_parent = cfg_node_alloc(rd_state->cfg);
        CFG_Node *new_sibling = cfg_node_alloc(rd_state->cfg);
        cfg_node_equip_string(rd_state->cfg, new_parent, split_panel->string);
        cfg_node_equip_string(rd_state->cfg, split_panel, str8_lit("0.5"));
        cfg_node_equip_string(rd_state->cfg, new_sibling, str8_lit("0.5"));
        if(parent->cfg != &cfg_nil_node)
        {
          cfg_node_unhook(rd_state->cfg, parent->cfg, split_panel);
          cfg_node_insert_child(rd_state->cfg, parent->cfg, split_panel_prev, new_parent);
        }
        else
        {
          cfg_node_equip_string(rd_state->cfg, new_parent, str8_lit("panels"));
          CFG_Node *panels_owner = workspace_mount.owner_cfg;
          cfg_node_insert_child(rd_state->cfg, panels_owner, panels_owner->last, new_parent);
          if(split_axis == Axis2_X)
          {
            cfg_node_child_from_string_or_alloc(rd_state->cfg, panels_owner, str8_lit("split_x"));
          }
          else
          {
            cfg_node_release(rd_state->cfg, cfg_node_child_from_string(panels_owner, str8_lit("split_x")));
          }
        }
        CFG_Node *min = split_panel;
        CFG_Node *max = new_sibling;
        if(split_side == Side_Min)
        {
          Swap(CFG_Node *, min, max);
        }
        cfg_node_insert_child(rd_state->cfg, new_parent, new_parent->last, min);
        cfg_node_insert_child(rd_state->cfg, new_parent, new_parent->last, max);
        new_panel_cfg = new_sibling;
      }

      {
        RD_WindowState *ws = rd_window_state_from_cfg(new_panel_cfg);
        if(ws != &rd_nil_window_state)
        {
          ui_select_state(ws->ui);
          UIShell_WorkspaceMount new_workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, new_panel_cfg);
          CFG_PanelTree new_panel_tree = new_workspace_mount.panel_tree;
          CFG_PanelNode *new_panel = cfg_panel_node_from_tree_cfg(new_panel_tree.root, new_panel_cfg);
          Rng2F32 stub_content_rect = r2f32p(0, 0, 1000, 1000);
          Vec2F32 stub_content_rect_dim = dim_2f32(stub_content_rect);
          Rng2F32 new_rect_px  = cfg_target_rect_from_panel_node(stub_content_rect, new_panel_tree.root, new_panel);
          Rng2F32 new_rect_pct = r2f32p(new_rect_px.x0/stub_content_rect_dim.x,
                                        new_rect_px.y0/stub_content_rect_dim.y,
                                        new_rect_px.x1/stub_content_rect_dim.x,
                                        new_rect_px.y1/stub_content_rect_dim.y);
          if(new_panel->prev != &cfg_nil_panel_node)
          {
            Rng2F32 target_prev_rect_px  = cfg_target_rect_from_panel_node(stub_content_rect, panel_tree.root, cfg_panel_node_from_tree_cfg(panel_tree.root, new_panel->prev->cfg));
            Rng2F32 target_prev_rect_pct = r2f32p(target_prev_rect_px.x0/stub_content_rect_dim.x,
                                                  target_prev_rect_px.y0/stub_content_rect_dim.y,
                                                  target_prev_rect_px.x1/stub_content_rect_dim.x,
                                                  target_prev_rect_px.y1/stub_content_rect_dim.y);
            Rng2F32 prev_rect_pct = r2f32p(uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x0", new_panel->prev->cfg), target_prev_rect_pct.x0, target_prev_rect_pct.x0, 0),
                                           uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y0", new_panel->prev->cfg), target_prev_rect_pct.y0, target_prev_rect_pct.y0, 0),
                                           uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x1", new_panel->prev->cfg), target_prev_rect_pct.x1, target_prev_rect_pct.x1, 0),
                                           uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y1", new_panel->prev->cfg), target_prev_rect_pct.y1, target_prev_rect_pct.y1, 0));
            new_rect_pct = prev_rect_pct;
            new_rect_pct.p0.v[split_axis] = new_rect_pct.p1.v[split_axis];
          }
          if(new_panel->next != &cfg_nil_panel_node)
          {
            Rng2F32 target_next_rect_px  = cfg_target_rect_from_panel_node(stub_content_rect, panel_tree.root, cfg_panel_node_from_tree_cfg(panel_tree.root, new_panel->next->cfg));
            Rng2F32 target_next_rect_pct = r2f32p(target_next_rect_px.x0/stub_content_rect_dim.x,
                                                  target_next_rect_px.y0/stub_content_rect_dim.y,
                                                  target_next_rect_px.x1/stub_content_rect_dim.x,
                                                  target_next_rect_px.y1/stub_content_rect_dim.y);
            Rng2F32 next_rect_pct = r2f32p(uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x0", new_panel->next->cfg), target_next_rect_pct.x0, target_next_rect_pct.x0, 0),
                                           uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y0", new_panel->next->cfg), target_next_rect_pct.y0, target_next_rect_pct.y0, 0),
                                           uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x1", new_panel->next->cfg), target_next_rect_pct.x1, target_next_rect_pct.x1, 0),
                                           uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y1", new_panel->next->cfg), target_next_rect_pct.y1, target_next_rect_pct.y1, 0));
            new_rect_pct = next_rect_pct;
            new_rect_pct.p1.v[split_axis] = new_rect_pct.p0.v[split_axis];
          }
          uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x0", new_panel->cfg), new_rect_pct.x0, new_rect_pct.x0, 1);
          uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x1", new_panel->cfg), new_rect_pct.x1, new_rect_pct.x1, 1);
          uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y0", new_panel->cfg), new_rect_pct.y0, new_rect_pct.y0, 1);
          uishell_panel_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y1", new_panel->cfg), new_rect_pct.y1, new_rect_pct.y1, 1);
        }
      }

      CFG_Node *dragdrop_origin_panel_cfg = cfg_node_from_id(uishell_regs()->panel);
      CFG_Node *dragdrop_tab = cfg_node_from_id(uishell_regs()->view);
      if(do_dragdrop_split &&
         new_panel_cfg != &cfg_nil_node && dragdrop_tab != &cfg_nil_node && dragdrop_origin_panel_cfg != &cfg_nil_node)
      {
        cfg_node_unhook(rd_state->cfg, dragdrop_origin_panel_cfg, dragdrop_tab);
        cfg_node_insert_child(rd_state->cfg, new_panel_cfg, new_panel_cfg->last, dragdrop_tab);
        UIShell_WorkspaceMount origin_workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, dragdrop_origin_panel_cfg);
        CFG_PanelTree origin_panel_tree = origin_workspace_mount.panel_tree;
        CFG_PanelNode *origin_panel = cfg_panel_node_from_tree_cfg(origin_panel_tree.root, dragdrop_origin_panel_cfg);
        if(origin_panel->selected_tab == &cfg_nil_node)
        {
          for(CFG_NodePtrNode *n = origin_panel->tabs.first; n != 0; n = n->next)
          {
            if(!rd_cfg_is_project_filtered(n->v))
            {
              UIShell_RegsScope(.panel = origin_panel->cfg->id, .tab = n->v->id)
              {
                uishell_push_cmd_current(str8_lit("focus_tab"));
              }
              break;
            }
          }
        }
        if(origin_panel->cfg != split_panel && origin_panel->tabs.count == 0)
        {
          uishell_push_cmd_current(str8_lit("close_panel"));
        }
        UIShell_RegsScope(.panel = new_panel_cfg->id, .tab = dragdrop_tab->id)
        {
          uishell_push_cmd_current(str8_lit("focus_tab"));
        }
      }

      if(new_panel_cfg != &cfg_nil_node)
      {
        UIShell_RegsScope(.panel = new_panel_cfg->id)
        {
          uishell_push_cmd_current(str8_lit("focus_panel"));
        }
      }

      if(panel->tab_side == Side_Max && split_axis == Axis2_X)
      {
        UIShell_RegsScope(.panel = new_panel_cfg->id)
        {
          uishell_push_cmd_current(str8_lit("tab_bar_bottom"));
        }
      }
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("close_panel"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_current_regs(scratch.arena);
    CFG_Node *window = workspace_mount.window_cfg;
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_PanelNode *panel = cfg_panel_node_from_tree_cfg(panel_tree.root, cfg_node_from_id(uishell_regs()->panel));
    CFG_PanelNode *parent = panel->parent;
    if(parent != &cfg_nil_panel_node)
    {
      if(parent->child_count == 2)
      {
        CFG_PanelNode *discard_child = panel;
        CFG_PanelNode *keep_child = (panel == parent->first ? parent->last : parent->first);
        CFG_PanelNode *grandparent = parent->parent;
        CFG_PanelNode *parent_prev = parent->prev;
        F32 pct_of_parent = parent->pct_of_parent;

        cfg_node_unhook(rd_state->cfg, parent->cfg, keep_child->cfg);
        if(grandparent != &cfg_nil_panel_node)
        {
          cfg_node_unhook(rd_state->cfg, grandparent->cfg, parent->cfg);
        }
        cfg_node_release(rd_state->cfg, parent->cfg);

        if(grandparent == &cfg_nil_panel_node)
        {
          if(keep_child->split_axis == Axis2_X)
          {
            cfg_node_child_from_string_or_alloc(rd_state->cfg, workspace_mount.owner_cfg, str8_lit("split_x"));
          }
          else
          {
            cfg_node_release(rd_state->cfg, cfg_node_child_from_string(workspace_mount.owner_cfg, str8_lit("split_x")));
          }
          cfg_node_equip_string(rd_state->cfg, keep_child->cfg, str8_lit("panels"));
          cfg_node_insert_child(rd_state->cfg, workspace_mount.owner_cfg, workspace_mount.owner_cfg->last, keep_child->cfg);
        }
        else
        {
          cfg_node_insert_child(rd_state->cfg, grandparent->cfg, parent_prev->cfg, keep_child->cfg);
          cfg_node_equip_stringf(rd_state->cfg, keep_child->cfg, "%f", pct_of_parent);
        }

        if(grandparent != &cfg_nil_panel_node && grandparent->split_axis == keep_child->split_axis && keep_child->first != &cfg_nil_panel_node)
        {
          cfg_node_unhook(rd_state->cfg, grandparent->cfg, keep_child->cfg);
          CFG_Node *prev = parent_prev->cfg;
          for(CFG_PanelNode *child = keep_child->first, *next = &cfg_nil_panel_node; child != &cfg_nil_panel_node; child = next)
          {
            next = child->next;
            cfg_node_unhook(rd_state->cfg, keep_child->cfg, child->cfg);
            cfg_node_insert_child(rd_state->cfg, grandparent->cfg, prev, child->cfg);
            prev = child->cfg;
            F32 old_pct = child->pct_of_parent;
            F32 new_pct = old_pct * pct_of_parent;
            cfg_node_equip_stringf(rd_state->cfg, child->cfg, "%f", new_pct);
          }
          cfg_node_release(rd_state->cfg, keep_child->cfg);
        }

        if(panel_tree.focused == discard_child)
        {
          UIShell_WorkspaceMount new_workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, window);
          CFG_PanelTree new_panel_tree = new_workspace_mount.panel_tree;
          CFG_PanelNode *new_focused = cfg_panel_node_from_tree_cfg(panel_tree.root, keep_child->cfg);
          for(CFG_PanelNode *grandchild = new_focused; grandchild != &cfg_nil_panel_node; grandchild = grandchild->first)
          {
            new_focused = grandchild;
          }
          UIShell_RegsScope(.panel = new_focused->cfg->id)
          {
            uishell_push_cmd_current(str8_lit("focus_panel"));
          }
        }
      }
      else
      {
        CFG_PanelNode *next = &cfg_nil_panel_node;
        F32 removed_size_pct = panel->pct_of_parent;
        if(next == &cfg_nil_panel_node) { next = panel->prev; }
        if(next == &cfg_nil_panel_node) { next = panel->next; }
        cfg_node_unhook(rd_state->cfg, parent->cfg, panel->cfg);
        cfg_node_release(rd_state->cfg, panel->cfg);

        {
          UIShell_WorkspaceMount new_workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, window);
          CFG_PanelTree new_panel_tree = new_workspace_mount.panel_tree;
          CFG_PanelNode *new_parent = cfg_panel_node_from_tree_cfg(new_panel_tree.root, parent->cfg);
          for(CFG_PanelNode *child = new_parent->first; child != &cfg_nil_panel_node; child = child->next)
          {
            CFG_Node *cfg = child->cfg;
            F32 old_pct = child->pct_of_parent;
            F32 new_pct = old_pct / (1.f-removed_size_pct);
            cfg_node_equip_stringf(rd_state->cfg, cfg, "%f", new_pct);
          }
        }

        if(panel_tree.focused == panel)
        {
          UIShell_WorkspaceMount new_workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, window);
          CFG_PanelTree new_panel_tree = new_workspace_mount.panel_tree;
          CFG_PanelNode *new_focused = cfg_panel_node_from_tree_cfg(panel_tree.root, next->cfg);
          for(CFG_PanelNode *grandchild = new_focused; grandchild != &cfg_nil_panel_node; grandchild = grandchild->first)
          {
            new_focused = grandchild;
          }
          UIShell_RegsScope(.panel = new_focused->cfg->id)
          {
            uishell_push_cmd_current(str8_lit("focus_panel"));
          }
        }
      }
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("rotate_panel_columns"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *panel_cfg = cfg_node_from_id(uishell_regs()->panel);
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, panel_cfg);
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_PanelNode *panel = cfg_panel_node_from_tree_cfg(panel_tree.root, panel_cfg);
    CFG_PanelNode *parent = &cfg_nil_panel_node;
    for(CFG_PanelNode *p = panel->parent; p != &cfg_nil_panel_node; p = p->parent)
    {
      if(p->split_axis == Axis2_X)
      {
        parent = p;
        break;
      }
    }
    if(parent != &cfg_nil_panel_node && parent->child_count > 1)
    {
      CFG_Node *rotated = parent->first->cfg;
      cfg_node_unhook(rd_state->cfg, parent->cfg, parent->first->cfg);
      cfg_node_insert_child(rd_state->cfg, parent->cfg, parent->last->cfg, rotated);
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("next_panel"), 0) ||
          str8_match(name, str8_lit("prev_panel"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    U64 panel_sib_off = str8_match(name, str8_lit("next_panel"), 0) ? OffsetOf(CFG_PanelNode, next) : OffsetOf(CFG_PanelNode, prev);
    U64 panel_child_off = str8_match(name, str8_lit("next_panel"), 0) ? OffsetOf(CFG_PanelNode, first) : OffsetOf(CFG_PanelNode, last);
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_current_regs(scratch.arena);
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_PanelNode *next_focused = &cfg_nil_panel_node;
    for(CFG_PanelNode *p = panel_tree.focused;
        p != &cfg_nil_panel_node;
        p = cfg_panel_node_rec__depth_first(panel_tree.root, p, panel_sib_off, panel_child_off).next)
    {
      if(p != panel_tree.focused && p->first == &cfg_nil_panel_node)
      {
        next_focused = p;
        break;
      }
    }
    if(next_focused == &cfg_nil_panel_node)
    {
      for(CFG_PanelNode *p = panel_tree.root;
          p != &cfg_nil_panel_node;
          p = cfg_panel_node_rec__depth_first(panel_tree.root, p, panel_sib_off, panel_child_off).next)
      {
        if(p != panel_tree.focused && p->first == &cfg_nil_panel_node)
        {
          next_focused = p;
          break;
        }
      }
    }
    UIShell_RegsScope(.panel = next_focused->cfg->id)
    {
      uishell_push_cmd_current(str8_lit("focus_panel"));
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("focus_panel"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *panel = cfg_node_from_id(uishell_regs()->panel);
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, panel);
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_Node *selection_cfg = &cfg_nil_node;
    for(CFG_PanelNode *p = panel_tree.root;
        p != &cfg_nil_panel_node;
        p = cfg_panel_node_rec__depth_first_pre(panel_tree.root, p).next)
    {
      CFG_Node *p_cfg = p->cfg;
      CFG_Node *p_selection = cfg_node_child_from_string(p_cfg, str8_lit("selected"));
      if(selection_cfg == &cfg_nil_node)
      {
        selection_cfg = p_selection;
      }
      else for(CFG_Node *s = p_selection; s != &cfg_nil_node; s = cfg_node_child_from_string(p_cfg, str8_lit("selected")))
      {
        cfg_node_release(rd_state->cfg, s);
      }
    }
    if(selection_cfg == &cfg_nil_node)
    {
      selection_cfg = cfg_node_alloc(rd_state->cfg);
      cfg_node_equip_string(rd_state->cfg, selection_cfg, str8_lit("selected"));
    }
    if(panel != &cfg_nil_node)
    {
      cfg_node_insert_child(rd_state->cfg, panel, &cfg_nil_node, selection_cfg);
      CFG_Node *window = rd_window_from_cfg(panel);
      RD_WindowState *ws = rd_window_state_from_cfg(window);
      ws->menu_bar_focused = 0;
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("focus_panel_right"), 0) ||
          str8_match(name, str8_lit("focus_panel_left"), 0) ||
          str8_match(name, str8_lit("focus_panel_up"), 0) ||
          str8_match(name, str8_lit("focus_panel_down"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    Vec2S32 panel_change_dir = {0};
    if(str8_match(name, str8_lit("focus_panel_right"), 0))
    {
      panel_change_dir = v2s32(+1, +0);
    }
    else if(str8_match(name, str8_lit("focus_panel_left"), 0))
    {
      panel_change_dir = v2s32(-1, +0);
    }
    else if(str8_match(name, str8_lit("focus_panel_up"), 0))
    {
      panel_change_dir = v2s32(+0, -1);
    }
    else
    {
      panel_change_dir = v2s32(+0, +1);
    }
    UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_current_regs(scratch.arena);
    CFG_PanelTree panel_tree = workspace_mount.panel_tree;
    CFG_PanelNode *src_panel = panel_tree.focused;
    Rng2F32 src_panel_rect = cfg_target_rect_from_panel_node(r2f32(v2f32(0, 0), v2f32(1000, 1000)), panel_tree.root, src_panel);
    Vec2F32 src_panel_center = center_2f32(src_panel_rect);
    Vec2F32 src_panel_half_dim = scale_2f32(dim_2f32(src_panel_rect), 0.5f);
    Vec2F32 travel_dim = add_2f32(src_panel_half_dim, v2f32(10.f, 10.f));
    Vec2F32 travel_dst = add_2f32(src_panel_center, mul_2f32(travel_dim, v2f32((F32)panel_change_dir.x, (F32)panel_change_dir.y)));
    CFG_PanelNode *dst_root = &cfg_nil_panel_node;
    for(CFG_PanelNode *p = panel_tree.root; p != &cfg_nil_panel_node; p = cfg_panel_node_rec__depth_first_pre(panel_tree.root, p).next)
    {
      if(p == src_panel || p->first != &cfg_nil_panel_node)
      {
        continue;
      }
      Rng2F32 p_rect = cfg_target_rect_from_panel_node(r2f32(v2f32(0, 0), v2f32(1000, 1000)), panel_tree.root, p);
      if(contains_2f32(p_rect, travel_dst))
      {
        dst_root = p;
        break;
      }
    }
    if(dst_root != &cfg_nil_panel_node)
    {
      CFG_PanelNode *dst_panel = &cfg_nil_panel_node;
      for(CFG_PanelNode *p = dst_root; p != &cfg_nil_panel_node; p = cfg_panel_node_rec__depth_first_pre(dst_root, p).next)
      {
        if(p->first == &cfg_nil_panel_node && p != src_panel)
        {
          dst_panel = p;
          break;
        }
      }
      UIShell_RegsScope(.panel = dst_panel->cfg->id)
      {
        uishell_push_cmd_current(str8_lit("focus_panel"));
      }
    }
    scratch_end(scratch);
  }
  else
  {
    result = 0;
  }

  return result;
}

internal B32
uishell_dispatch_font_command(String8 name)
{
  B32 result = 1;
  CFG_Node *cfg = &cfg_nil_node;
  F32 delta = 0.f;
  B32 window_scope = 0;

  if(str8_match(name, str8_lit("inc_window_font_size"), 0))
  {
    cfg = cfg_node_from_id(uishell_regs()->window);
    delta = +1.f;
    window_scope = 1;
  }
  else if(str8_match(name, str8_lit("inc_view_font_size"), 0))
  {
    cfg = cfg_node_from_id(uishell_regs()->view);
    delta = +1.f;
  }
  else if(str8_match(name, str8_lit("dec_window_font_size"), 0))
  {
    cfg = cfg_node_from_id(uishell_regs()->window);
    delta = -1.f;
    window_scope = 1;
  }
  else if(str8_match(name, str8_lit("dec_view_font_size"), 0))
  {
    cfg = cfg_node_from_id(uishell_regs()->view);
    delta = -1.f;
  }
  else
  {
    result = 0;
  }

  if(result && cfg != &cfg_nil_node)
  {
    fnt_reset();
    U64 old_view = uishell_regs()->view;
    U64 old_tab = uishell_regs()->tab;
    if(window_scope)
    {
      uishell_regs()->view = 0;
      uishell_regs()->tab = 0;
    }
    F32 current_font_size = rd_font_size();
    uishell_regs()->view = old_view;
    uishell_regs()->tab = old_tab;
    F32 new_font_size = Clamp(6.f, current_font_size + delta, 72.f);
    CFG_Node *font_size_cfg = cfg_node_child_from_string_or_alloc(rd_state->cfg, cfg, str8_lit("font_size"));
    cfg_node_new_replacef(rd_state->cfg, font_size_cfg, "%I64u", (U64)new_font_size);
  }

  return result;
}

internal B32
uishell_dispatch_window_command(String8 name)
{
  B32 result = 1;

  if(str8_match(name, str8_lit("open_window"), 0))
  {
    CFG_Node *old_window = cfg_node_from_id(uishell_regs()->window);
    CFG_Node *bucket = old_window->parent;
    if(bucket == &cfg_nil_node)
    {
      bucket = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
    }
    CFG_Node *new_window = cfg_node_new(rd_state->cfg, bucket, str8_lit("window"));
    CFG_Node *size = cfg_node_new(rd_state->cfg, new_window, str8_lit("size"));
    cfg_node_newf(rd_state->cfg, size, "1280");
    cfg_node_newf(rd_state->cfg, size, "720");
    for(CFG_Node *old_child = old_window->first; old_child != &cfg_nil_node; old_child = old_child->next)
    {
      if(!str8_match(old_child->string, str8_lit("panels"), 0) &&
         !str8_match(old_child->string, str8_lit("workspace"), 0) &&
         !str8_match(old_child->string, str8_lit("size"), 0) &&
         !str8_match(old_child->string, str8_lit("pos"), 0) &&
         !str8_match(old_child->string, str8_lit("monitor"), 0) &&
         !str8_match(old_child->string, str8_lit("fullscreen"), 0) &&
         !str8_match(old_child->string, str8_lit("maximized"), 0))
      {
        CFG_Node *new_child = cfg_node_deep_copy(rd_state->cfg, old_child);
        cfg_node_insert_child(rd_state->cfg, new_window, new_window->last, new_child);
      }
    }
    CFG_Node *panels = cfg_node_new(rd_state->cfg, new_window, str8_lit("panels"));
    cfg_node_child_from_string_or_alloc(rd_state->cfg, panels, str8_lit("selected"));
  }
  else if(str8_match(name, str8_lit("new_workspace"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
    if(window == &cfg_nil_node)
    {
      window = cfg_node_from_id(rd_state->last_focused_window);
    }
    if(window != &cfg_nil_node)
    {
      CFG_NodePtrList workspaces = cfg_node_child_list_from_string(scratch.arena, window, str8_lit("workspace"));
      CFG_Node *workspace = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
      CFG_Node *label = cfg_node_new(rd_state->cfg, workspace, str8_lit("label"));
      cfg_node_newf(rd_state->cfg, label, "Workspace %I64u", workspaces.count+1);
      RD_WindowState *ws = rd_window_state_from_cfg(window);
      if(ws != &rd_nil_window_state)
      {
        ws->root_controlled_split_initialized = 1;
        ws->root_controlled_split_selected_workspace_id = workspace->id;
      }
      UISHELL_APP_RESET_PANELS(window);
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("select_workspace"), 0))
  {
    CFG_Node *workspace = cfg_node_from_id(uishell_regs()->cfg);
    CFG_Node *window = workspace != &cfg_nil_node ? rd_window_from_cfg(workspace) : cfg_node_from_id(uishell_regs()->window);
    RD_WindowState *ws = rd_window_state_from_cfg(window);
    if(ws != &rd_nil_window_state && workspace != &cfg_nil_node)
    {
      ws->root_controlled_split_initialized = 1;
      ws->root_controlled_split_selected_workspace_id = workspace->id;
      ws->window_layout_reset = 1;
    }
  }
  else if(str8_match(name, str8_lit("close_workspace"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *workspace = cfg_node_from_id(uishell_regs()->cfg);
    CFG_Node *window = workspace != &cfg_nil_node ? rd_window_from_cfg(workspace) : cfg_node_from_id(uishell_regs()->window);
    if(window != &cfg_nil_node &&
       workspace != &cfg_nil_node &&
       (str8_match(workspace->string, str8_lit("workspace"), 0) ||
        workspace == window))
    {
      UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
      if(split.inventory.count > 1)
      {
        UIShell_MaterializedWorkspace *closing = 0;
        for(UIShell_MaterializedWorkspace *w = split.inventory.first; w != 0; w = w->next)
        {
          if(w->id == workspace->id)
          {
            closing = w;
            break;
          }
        }
        if(uishell_controlled_split_workspace_can_close(&split, closing))
        {
          UIShell_MaterializedWorkspace *next_selected = split.inventory.selected;
          if(next_selected == 0 || next_selected == closing)
          {
            next_selected = closing->next;
            if(next_selected == 0)
            {
              next_selected = closing->prev;
            }
          }
          if(next_selected == 0 || next_selected == closing)
          {
            for(UIShell_MaterializedWorkspace *w = split.inventory.first; w != 0; w = w->next)
            {
              if(w != closing)
              {
                next_selected = w;
                break;
              }
            }
          }
          RD_WindowState *ws = rd_window_state_from_cfg(window);
          if(ws != &rd_nil_window_state)
          {
            ws->root_controlled_split_initialized = 1;
            ws->root_controlled_split_selected_workspace_id = next_selected != 0 ? next_selected->id : window->id;
            ws->window_layout_reset = 1;
          }
          if(workspace == window)
          {
            // The window-backed workspace is the window's legacy layout payload.
            // Closing it removes that payload; window-level metadata such as
            // `label` intentionally remains attached to the window node.
            cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("panels")));
            cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("split_x")));
          }
          else
          {
            cfg_node_release(rd_state->cfg, workspace);
          }
        }
      }
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("workspace_settings"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    if(split.inventory.selected != 0)
    {
      String8 expr = push_str8f(rd_frame_arena(), "query:config.$%I64x", split.inventory.selected->id);
      UIShell_RegsScope(.expr = expr, .do_implicit_root = 1, .do_big_rows = 1, .do_lister = 1)
      { uishell_push_cmd_current(str8_lit("push_query")); }
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("window_settings"), 0))
  {
    String8 expr = push_str8f(rd_frame_arena(), "query:config.$%I64x", uishell_regs()->window);
    UIShell_RegsScope(.expr = expr, .do_implicit_root = 1, .do_big_rows = 1, .do_lister = 1)
    {
      uishell_push_cmd_current(str8_lit("push_query"));
    }
  }
  else if(str8_match(name, str8_lit("close_window"), 0) ||
          str8_match(name, str8_lit("window_close_menu"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_NodePtrList all_windows = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("window"));
    CFG_Node *wcfg = cfg_node_from_id(uishell_regs()->window);
    if(all_windows.count == 1 && all_windows.first->v == wcfg)
    {
      uishell_push_cmd_current(str8_lit("exit"));
    }
    else
    {
      cfg_node_release(rd_state->cfg, wcfg);
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("toggle_fullscreen"), 0))
  {
    CFG_Node *wcfg = cfg_node_from_id(uishell_regs()->window);
    RD_WindowState *ws = rd_window_state_from_cfg(wcfg);
    if(ws != &rd_nil_window_state)
    {
      wm_window_set_fullscreen(ws->os, !wm_window_is_fullscreen(ws->os));
    }
  }
  else if(str8_match(name, str8_lit("toggle_dev_menu"), 0))
  {
    CFG_Node *wcfg = cfg_node_from_id(uishell_regs()->window);
    RD_WindowState *ws = rd_window_state_from_cfg(wcfg);
    if(ws != &rd_nil_window_state)
    {
      ws->dev_menu_is_open ^= 1;
    }
  }
  else if(str8_match(name, str8_lit("bring_to_front"), 0))
  {
    CFG_Node *last_focused_wcfg = cfg_node_from_id(rd_state->last_focused_window);
    RD_WindowState *last_focused_ws = rd_window_state_from_cfg(last_focused_wcfg);
    if(last_focused_ws == &rd_nil_window_state)
    {
      last_focused_ws = rd_state->first_window_state;
    }
    if(last_focused_ws != &rd_nil_window_state)
    {
      wm_window_set_minimized(last_focused_ws->os, 0);
      wm_window_focus(last_focused_ws->os);
    }
  }
  else if(str8_match(name, str8_lit("popup_accept"), 0))
  {
    rd_state->popup_active = 0;
    rd_state->popup_key = ui_key_zero();
    for(UIShell_CmdNode *n = rd_state->popup_cmds.first; n != 0; n = n->next)
    {
      uishell_push_stored_cmd(n->cmd.name, n->cmd.regs);
    }
  }
  else if(str8_match(name, str8_lit("popup_cancel"), 0))
  {
    rd_state->popup_active = 0;
    rd_state->popup_key = ui_key_zero();
  }
  else if(str8_match(name, str8_lit("reset_to_default_bindings"), 0))
  {
    CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
    Temp scratch = scratch_begin(0, 0);
    CFG_NodePtrList all_keybindings = cfg_node_child_list_from_string(scratch.arena, user, str8_lit("keybindings"));
    for(CFG_NodePtrNode *n = all_keybindings.first; n != 0; n = n->next)
    {
      cfg_node_release(rd_state->cfg, n->v);
    }
    scratch_end(scratch);

    CFG_Node *keybindings = cfg_node_new(rd_state->cfg, user, str8_lit("keybindings"));
    for(UIShell_CmdPack *pack = rd_state->first_cmd_pack; pack != 0; pack = pack->next)
    {
      if(pack->binding_count != 0 && pack->binding_from_index != 0)
      {
        U64 binding_count = pack->binding_count();
        for(U64 idx = 0; idx < binding_count; idx += 1)
        {
          UIShell_DefaultBinding default_binding = pack->binding_from_index(idx);
          String8 binding_name = default_binding.string;
          CFG_Binding binding = default_binding.binding;
          CFG_Node *binding_root = cfg_node_new(rd_state->cfg, keybindings, str8_zero());
          cfg_node_new(rd_state->cfg, binding_root, binding_name);
          cfg_node_new(rd_state->cfg, binding_root, wm_key_cfg_name_table[binding.key]);
          if(binding.modifiers & WM_Modifier_Ctrl)  {cfg_node_newf(rd_state->cfg, binding_root, "ctrl");}
          if(binding.modifiers & WM_Modifier_Shift) {cfg_node_newf(rd_state->cfg, binding_root, "shift");}
          if(binding.modifiers & WM_Modifier_Alt)   {cfg_node_newf(rd_state->cfg, binding_root, "alt");}
          if(binding.modifiers & WM_Modifier_Super) {cfg_node_newf(rd_state->cfg, binding_root, "super");}
        }
      }
    }
  }
  else
  {
    result = 0;
  }

  return result;
}
internal B32
uishell_dispatch_query_command(String8 name)
{
  B32 result = 1;

  if(str8_match(name, str8_lit("push_query"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    String8 cmd_name = uishell_regs()->cmd_name;
    UIShell_AppCmdInfo cmd_kind_info = uishell_app_cmd_info_from_string(cmd_name);

    // rjf: close existing context menus
    {
      CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
      RD_WindowState *ws = rd_window_state_from_cfg(window);
      ui_ctx_menu_close();
      ws->menu_bar_focused = 0;
    }

    // rjf: floating queries -> set up window to build immediate-mode top-level query
    CFG_Node *view = &cfg_nil_node;
    B32 is_floating = (cmd_name.size == 0 || cmd_kind_info.query_flags & UIShell_QueryFlag_Floating);
    if(is_floating)
    {
      CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
      RD_WindowState *ws = rd_window_state_from_cfg(window);
      if(ws != &rd_nil_window_state)
      {
        ws->query_is_active = 1;
        arena_clear(ws->query_arena);
        ws->query_regs = push_array(ws->query_arena, UIShell_Regs, 1);
        ws->query_regs[0] = uishell_regs_copy(ws->query_arena, uishell_regs());
      }
      CFG_Node *window_query = rd_immediate_cfg_from_keyf("window_query_%p", window);
      cfg_node_release_all_children(rd_state->cfg, window_query);
      view = cfg_node_child_from_string_or_alloc(rd_state->cfg, window_query, str8_lit("watch"));
      CFG_Node *expr = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("expression"));
      cfg_node_new_replace(rd_state->cfg, expr, uishell_regs()->expr);
    }

    // rjf: non-floating -> embed in view
    else
    {
      view = cfg_node_from_id(uishell_regs()->view);
    }

    // rjf: determine if the target view is a lister (and thus already has a command)
    B32 view_is_lister = (cfg_node_child_from_string(view, str8_lit("lister")) != &cfg_nil_node);

    // rjf: target view is a lister -> do not do anything - cannot replace the command
    if(!view_is_lister)
    {
      // rjf: unpack view's query info
      CFG_Node *query = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("query"));
      CFG_Node *cmd = cfg_node_child_from_string_or_alloc(rd_state->cfg, query, str8_lit("cmd"));
      CFG_Node *input = cfg_node_child_from_string_or_alloc(rd_state->cfg, query, str8_lit("input"));
      if(is_floating)
      {
        if(uishell_regs()->do_implicit_root)
        {
          cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("explicit_root")));
        }
        else
        {
          cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("explicit_root"));
        }
        if(!uishell_regs()->do_lister)
        {
          cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("lister")));
        }
        else
        {
          cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("lister"));
        }
        if(!uishell_regs()->activate_with_single_click)
        {
          cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("activate_with_single_click")));
        }
        else
        {
          cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("activate_with_single_click"));
        }
      }

      // rjf: choose initial input string
      String8 initial_input = {0};
      if(cmd_name.size != 0)
      {
        if(cmd_kind_info.query_slot == UIShell_AppRegSlot_FilePath)
        {
          CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
          CFG_Node *current_path = cfg_node_child_from_string(user, str8_lit("current_path"));
          String8 current_path_string = current_path->first->string;
          if(current_path_string.size == 0)
          {
            current_path_string = path_normalized_from_string(scratch.arena, get_current_path(scratch.arena));
          }
          initial_input = current_path_string;
          initial_input = push_str8f(scratch.arena, "%S/", initial_input);
        }
        else if(cmd_kind_info.query_flags & UIShell_QueryFlag_KeepOldInput)
        {
          initial_input = input->first->string;
        }
      }

      // rjf: build query state
      String8 current_query_cmd_name = cmd->first->string;
      cfg_node_new_replace(rd_state->cfg, input, initial_input);
      cfg_node_new_replace(rd_state->cfg, cmd, cmd_name);
      RD_ViewState *vs = rd_view_state_from_cfg(view);
      if(cmd_name.size != 0)
      {
        if(!vs->query_is_open && cmd_kind_info.query_flags & UIShell_QueryFlag_SelectOldInput)
        {
          vs->query_cursor = txt_pt(1, 1+input->first->string.size);
          vs->query_mark = txt_pt(1, 1);
        }
        else
        {
          vs->query_cursor = txt_pt(1, 1+input->first->string.size);
          vs->query_mark = vs->query_cursor;
        }
        if(!str8_match(current_query_cmd_name, cmd_name, 0))
        {
          vs->query_is_open = 1;
        }
        else
        {
          vs->query_is_open ^= 1;
        }
      }
      if(uishell_regs()->do_lister)
      {
        vs->query_is_open = 1;
      }
      vs->contents_are_focused = 0;
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("complete_query"), 0))
  {
    // rjf: unpack params
    CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
    RD_WindowState *ws = rd_window_state_from_cfg(window);
    CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
    String8 cmd_name = rd_view_query_cmd();

    // rjf: find out if this view is a lister
    B32 is_lister = (cfg_node_child_from_string(view, str8_lit("lister")) != &cfg_nil_node);

    // rjf: push command
    if(cmd_name.size != 0) UIShell_RegsScope()
    {
      if(is_lister)
      {
        uishell_regs()->view = ws->query_regs->view;
      }
      uishell_push_cmd_current(cmd_name);
    }

    // rjf: complete query, either by closing the query popup, or closing the
    // tab-embedded query edit
    UIShell_AppCmdInfo cmd_kind_info = uishell_app_cmd_info_from_string(cmd_name);
    if(is_lister)
    {
      ws->query_is_active = 0;
    }
    else if(!(cmd_kind_info.query_flags & UIShell_QueryFlag_KeepOldInput))
    {
      RD_ViewState *vs = rd_view_state_from_cfg(view);
      vs->query_is_open = 0;
      vs->query_string_size = 0;
    }
  }
  else if(str8_match(name, str8_lit("cancel_query"), 0))
  {
    CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
    RD_WindowState *ws = rd_window_state_from_cfg(window);
    if(ws != &rd_nil_window_state)
    {
      ws->query_is_active = 0;
      arena_clear(ws->query_arena);
      ws->query_regs = 0;
    }
  }
  else if(str8_match(name, str8_lit("update_query"), 0))
  {
    CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
    CFG_Node *query = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("query"));
    CFG_Node *input = cfg_node_child_from_string_or_alloc(rd_state->cfg, query, str8_lit("input"));
    cfg_node_new_replace(rd_state->cfg, input, uishell_regs()->string);
    RD_ViewState *vs = rd_view_state_from_cfg(view);
    vs->query_cursor = vs->query_mark = txt_pt(1, uishell_regs()->string.size+1);
    vs->query_string_size = Min(sizeof(vs->query_buffer), uishell_regs()->string.size);
    MemoryCopy(vs->query_buffer, uishell_regs()->string.str, vs->query_string_size);
  }
  else
  {
    result = 0;
  }

  return result;
}
internal B32
uishell_shell_query_cmd_pack_dispatch(String8 name)
{
  B32 result = (uishell_dispatch_command_palette_command(name) ||
                uishell_dispatch_query_command(name));
  return result;
}
internal void
uishell_register_shell_cmd_packs(void)
{
  local_persist UIShell_CmdPack shell_window_pack =
  {
    .name = str8_lit_comp("shell_window"),
    .cmd_count = uishell_shell_window_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_shell_window_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_shell_window_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_shell_window_cmd_pack_binding_count,
    .binding_from_index = uishell_shell_window_cmd_pack_binding_from_index,
    .menu_specs = uishell_shell_window_menu_specs,
    .dispatch = uishell_dispatch_window_command,
  };
  local_persist UIShell_CmdPack shell_panel_pack =
  {
    .name = str8_lit_comp("shell_panel"),
    .cmd_count = uishell_shell_panel_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_shell_panel_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_shell_panel_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_shell_panel_cmd_pack_binding_count,
    .binding_from_index = uishell_shell_panel_cmd_pack_binding_from_index,
    .menu_specs = uishell_shell_panel_menu_specs,
    .dispatch = uishell_dispatch_panel_command,
  };
  local_persist UIShell_CmdPack shell_tab_pack =
  {
    .name = str8_lit_comp("shell_tab"),
    .cmd_count = uishell_shell_tab_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_shell_tab_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_shell_tab_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_shell_tab_cmd_pack_binding_count,
    .binding_from_index = uishell_shell_tab_cmd_pack_binding_from_index,
    .menu_specs = uishell_shell_tab_menu_specs,
    .dispatch = uishell_dispatch_tab_command,
  };
  local_persist UIShell_CmdPack shell_app_pack =
  {
    .name = str8_lit_comp("shell_app"),
    .cmd_count = uishell_shell_app_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_shell_app_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_shell_app_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_shell_app_cmd_pack_binding_count,
    .binding_from_index = uishell_shell_app_cmd_pack_binding_from_index,
    .menu_specs = uishell_shell_help_menu_specs,
    .dispatch = uishell_dispatch_app_command,
  };
  local_persist UIShell_CmdPack shell_query_pack =
  {
    .name = str8_lit_comp("shell_query"),
    .cmd_count = uishell_shell_query_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_shell_query_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_shell_query_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_shell_query_cmd_pack_binding_count,
    .binding_from_index = uishell_shell_query_cmd_pack_binding_from_index,
    .dispatch = uishell_shell_query_cmd_pack_dispatch,
  };
  local_persist UIShell_CmdPack shell_ui_event_pack =
  {
    .name = str8_lit_comp("shell_ui_event"),
    .cmd_count = uishell_shell_ui_event_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_shell_ui_event_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_shell_ui_event_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_shell_ui_event_cmd_pack_binding_count,
    .binding_from_index = uishell_shell_ui_event_cmd_pack_binding_from_index,
    .dispatch = uishell_dispatch_ui_event_command,
  };
  local_persist UIShell_CmdPack shell_font_pack =
  {
    .name = str8_lit_comp("shell_font"),
    .cmd_count = uishell_shell_font_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_shell_font_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_shell_font_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_shell_font_cmd_pack_binding_count,
    .binding_from_index = uishell_shell_font_cmd_pack_binding_from_index,
    .dispatch = uishell_dispatch_font_command,
  };

  uishell_register_cmd_pack(&shell_window_pack);
  uishell_register_cmd_pack(&shell_panel_pack);
  uishell_register_cmd_pack(&shell_tab_pack);
  uishell_register_cmd_pack(&shell_app_pack);
  uishell_register_cmd_pack(&shell_query_pack);
  uishell_register_cmd_pack(&shell_ui_event_pack);
  uishell_register_cmd_pack(&shell_font_pack);
}
