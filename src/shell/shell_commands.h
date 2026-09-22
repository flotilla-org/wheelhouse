// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef SHELL_COMMANDS_H
#define SHELL_COMMANDS_H

////////////////////////////////
//~ rjf: Shell Command Metadata

#define UISHELL_SHELL_CMD_FLAG_UI UIShell_CmdFlag_ListInUI
#define UISHELL_SHELL_Q_NONE     0, UIShell_AppRegSlot_Null, {0}, {0}
#define UISHELL_SHELL_Q_COMMANDS UIShell_QueryFlag_Floating|UIShell_QueryFlag_Required, UIShell_AppRegSlot_CmdName, str8_lit_comp("query:commands"), str8_lit_comp("commands")
#define UISHELL_SHELL_Q_TABS     UIShell_QueryFlag_Floating|UIShell_QueryFlag_Required, UIShell_AppRegSlot_CmdName, str8_lit_comp("query:tab_commands"), str8_lit_comp("commands")
#define UISHELL_SHELL_CMD(name, display, icon, desc, tags, flags, query) {str8_lit_comp(name), str8_lit_comp(display), RD_IconKind_##icon, str8_lit_comp(desc), str8_lit_comp(tags), {0}, flags, query}

read_only global UIShell_AppCmdInfo uishell_shell_app_cmd_info_table[] =
{
  UISHELL_SHELL_CMD("exit", "Exit", X, "Exits the app.", "quit,close", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("undo", "Undo", Undo, "Undoes the last edit.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("redo", "Redo", Redo, "Redoes the last edit.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("go_back", "Go Back", LeftArrow, "Moves backward in focus history.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("go_forward", "Go Forward", RightArrow, "Moves forward in focus history.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
};

read_only global UIShell_AppCmdInfo uishell_shell_query_cmd_info_table[] =
{
  UISHELL_SHELL_CMD("open_palette", "Open Palette", List, "Opens the palette.", "help,cmd,lister", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("run_command", "Run Command", Null, "Runs a command from the command palette.", "help,cmd", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_COMMANDS),
  UISHELL_SHELL_CMD("open_tab", "Open New Tab", Null, "Opens a new tab.", "tab,view", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_TABS),
};

read_only global UIShell_AppCmdInfo uishell_shell_font_cmd_info_table[] =
{
  UISHELL_SHELL_CMD("inc_window_font_size", "Increase Window Font Size", Null, "Increases the window font size.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("dec_window_font_size", "Decrease Window Font Size", Null, "Decreases the window font size.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("inc_view_font_size", "Increase View Font Size", Null, "Increases the view font size.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("dec_view_font_size", "Decrease View Font Size", Null, "Decreases the view font size.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
};

read_only global UIShell_AppCmdInfo uishell_shell_window_cmd_info_table[] =
{
  UISHELL_SHELL_CMD("open_window", "Open New Window", Window, "Opens a new window.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("new_workspace", "New Workspace", Add, "Creates a new workspace in the current window.", "workspace", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("select_workspace", "Select Workspace", Null, "Selects a workspace in the current window.", "workspace", 0, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("close_workspace", "Close Workspace", X, "Closes a workspace in the current window.", "workspace", 0, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("workspace_settings", "Workspace Settings", Gear, "Edits the current workspace's properties.", "workspace,rename", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("window_settings", "Window Settings", Gear, "Opens settings for a window.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("close_window", "Close Window", Window, "Closes the current window.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("window_close_menu", "Window Close Menu", Null, "Closes the current window.", "", 0, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("toggle_fullscreen", "Toggle Fullscreen", Window, "Toggles fullscreen view.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("toggle_dev_menu", "Toggle Developer Menu", Gear, "Toggles the developer menu, containing developer settings & toggles.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("reset_to_default_bindings", "Reset To Default Bindings", Null, "Resets all keybindings to their defaults.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
};

read_only global UIShell_AppCmdInfo uishell_shell_panel_cmd_info_table[] =
{
  UISHELL_SHELL_CMD("reset_to_default_panels", "Reset To Default Panel Layout", Window, "Resets the window to the default panel layout.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("reset_to_compact_panels", "Reset To Compact Panel Layout", Window, "Resets the window to the compact panel layout.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("reset_to_simple_panels", "Reset To Simple Panel Layout", Window, "Resets the window to the simple panel layout.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("new_panel_left", "Split Panel Left", XSplit, "Creates a new panel to the left.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("new_panel_up", "Split Panel Up", YSplit, "Creates a new panel above.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("new_panel_right", "Split Panel Right", XSplit, "Creates a new panel to the right.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("new_panel_down", "Split Panel Down", YSplit, "Creates a new panel below.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("split_panel", "Split Panel", Null, "Splits the current panel.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("rotate_panel_columns", "Rotate Panel Columns", Null, "Rotates panel columns.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("next_panel", "Focus Next Panel", RightArrow, "Focuses the next panel.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("prev_panel", "Focus Previous Panel", LeftArrow, "Focuses the previous panel.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("focus_panel", "Focus Panel", Null, "Focuses a panel.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("focus_panel_right", "Focus Panel Right", RightArrow, "Focuses the panel to the right.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("focus_panel_left", "Focus Panel Left", LeftArrow, "Focuses the panel to the left.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("focus_panel_up", "Focus Panel Up", UpArrow, "Focuses the panel above.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("focus_panel_down", "Focus Panel Down", DownArrow, "Focuses the panel below.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("close_panel", "Close Panel", ClosePanel, "Closes the current panel.", "panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
};

read_only global UIShell_AppCmdInfo uishell_shell_tab_cmd_info_table[] =
{
  UISHELL_SHELL_CMD("focus_tab", "Focus Tab", Null, "Focuses a tab.", "tab", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("next_tab", "Focus Next Tab", RightArrow, "Focuses the next tab.", "tab", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("prev_tab", "Focus Previous Tab", LeftArrow, "Focuses the previous tab.", "tab", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_tab_right", "Move Tab Right", RightArrow, "Moves the current tab right.", "tab", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_tab_left", "Move Tab Left", LeftArrow, "Moves the current tab left.", "tab", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("duplicate_tab", "Duplicate Tab", Duplicate, "Duplicates the current tab.", "tab", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("copy_tab_full_path", "Copy Full Path", Clipboard, "Copies the current tab path.", "tab,path", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("close_tab", "Close Tab", X, "Closes the current tab.", "tab", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_view", "Move View", Null, "Moves the current view.", "tab,panel", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("tab_bar_top", "Anchor Tab Bar To Top", UpArrow, "Moves tab bars to the top.", "tab", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("tab_bar_bottom", "Anchor Tab Bar To Bottom", DownArrow, "Moves tab bars to the bottom.", "tab", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("tab_settings", "Selected Tab Settings", Gear, "Opens settings for a tab.", "tab,view,options", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
};

read_only global UIShell_AppCmdInfo uishell_shell_ui_event_cmd_info_table[] =
{
  UISHELL_SHELL_CMD("edit", "Edit", Pencil, "Begins editing.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("accept", "Accept", CheckFilled, "Accepts the active interaction.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("cancel", "Cancel", X, "Cancels the active interaction.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("focus_menu", "Focus Menu", List, "Focuses the active menu.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_left", "Move Left", Null, "Moves left.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_right", "Move Right", Null, "Moves right.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_up", "Move Up", Null, "Moves up.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_down", "Move Down", Null, "Moves down.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_left_select", "Move Left Select", Null, "Extends selection left.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_right_select", "Move Right Select", Null, "Extends selection right.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_up_select", "Move Up Select", Null, "Extends selection up.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_down_select", "Move Down Select", Null, "Extends selection down.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_left_chunk", "Move Left Chunk", Null, "Moves left by a chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_right_chunk", "Move Right Chunk", Null, "Moves right by a chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_up_chunk", "Move Up Chunk", Null, "Moves up by a chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_down_chunk", "Move Down Chunk", Null, "Moves down by a chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_up_page", "Move Up Page", Null, "Moves up by a page.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_down_page", "Move Down Page", Null, "Moves down by a page.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_up_whole", "Move Up Whole", Null, "Moves to the top.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_down_whole", "Move Down Whole", Null, "Moves to the bottom.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_left_chunk_select", "Move Left Chunk Select", Null, "Extends selection left by a chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_right_chunk_select", "Move Right Chunk Select", Null, "Extends selection right by a chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_up_chunk_select", "Move Up Chunk Select", Null, "Extends selection up by a chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_down_chunk_select", "Move Down Chunk Select", Null, "Extends selection down by a chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_up_page_select", "Move Up Page Select", Null, "Extends selection up by a page.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_down_page_select", "Move Down Page Select", Null, "Extends selection down by a page.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_up_whole_select", "Move Up Whole Select", Null, "Extends selection to the top.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_down_whole_select", "Move Down Whole Select", Null, "Extends selection to the bottom.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_up_reorder", "Move Up Reorder", Null, "Moves the current item up.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_down_reorder", "Move Down Reorder", Null, "Moves the current item down.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_home", "Move Home", Null, "Moves to the beginning.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_end", "Move End", Null, "Moves to the end.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_home_select", "Move Home Select", Null, "Extends selection to the beginning.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_end_select", "Move End Select", Null, "Extends selection to the end.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("select_all", "Select All", Null, "Selects all.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("delete_single", "Delete Single", Null, "Deletes one item.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("delete_chunk", "Delete Chunk", Null, "Deletes one chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("backspace_single", "Backspace Single", Null, "Backspaces one item.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("backspace_chunk", "Backspace Chunk", Null, "Backspaces one chunk.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("copy", "Copy", Clipboard, "Copies the current selection.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("cut", "Cut", Clipboard, "Cuts the current selection.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("paste", "Paste", Clipboard, "Pastes clipboard contents.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_next", "Move Next", Null, "Moves to the next item.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
  UISHELL_SHELL_CMD("move_prev", "Move Previous", Null, "Moves to the previous item.", "", UISHELL_SHELL_CMD_FLAG_UI, UISHELL_SHELL_Q_NONE),
};

#define UISHELL_SHELL_CMD_PACK_INFO_FUNCTIONS(prefix, table) \
internal U64 \
prefix##_cmd_count(void) \
{ \
  return ArrayCount(table); \
} \
internal UIShell_AppCmdInfo \
prefix##_cmd_info_from_index(U64 idx) \
{ \
  UIShell_AppCmdInfo result = {0}; \
  if(idx < ArrayCount(table)) \
  { \
    result = table[idx]; \
  } \
  return result; \
} \
internal UIShell_AppCmdInfo \
prefix##_cmd_info_from_string(String8 string) \
{ \
  UIShell_AppCmdInfo result = {0}; \
  for EachElement(idx, table) \
  { \
    if(str8_match(string, table[idx].string, 0)) \
    { \
      result = table[idx]; \
      break; \
    } \
  } \
  return result; \
}

UISHELL_SHELL_CMD_PACK_INFO_FUNCTIONS(uishell_shell_app_cmd_pack, uishell_shell_app_cmd_info_table)
UISHELL_SHELL_CMD_PACK_INFO_FUNCTIONS(uishell_shell_query_cmd_pack, uishell_shell_query_cmd_info_table)
UISHELL_SHELL_CMD_PACK_INFO_FUNCTIONS(uishell_shell_font_cmd_pack, uishell_shell_font_cmd_info_table)
UISHELL_SHELL_CMD_PACK_INFO_FUNCTIONS(uishell_shell_window_cmd_pack, uishell_shell_window_cmd_info_table)
UISHELL_SHELL_CMD_PACK_INFO_FUNCTIONS(uishell_shell_panel_cmd_pack, uishell_shell_panel_cmd_info_table)
UISHELL_SHELL_CMD_PACK_INFO_FUNCTIONS(uishell_shell_tab_cmd_pack, uishell_shell_tab_cmd_info_table)
UISHELL_SHELL_CMD_PACK_INFO_FUNCTIONS(uishell_shell_ui_event_cmd_pack, uishell_shell_ui_event_cmd_info_table)

#undef UISHELL_SHELL_CMD_PACK_INFO_FUNCTIONS

#undef UISHELL_SHELL_CMD
#undef UISHELL_SHELL_Q_TABS
#undef UISHELL_SHELL_Q_COMMANDS
#undef UISHELL_SHELL_Q_NONE
#undef UISHELL_SHELL_CMD_FLAG_UI

////////////////////////////////
//~ rjf: Shell Default Bindings

#define UISHELL_BIND(name, key, mods) {str8_lit_comp(name), {WM_Key_##key, mods}}

read_only global UIShell_DefaultBinding uishell_shell_font_default_binding_table[] =
{
  UISHELL_BIND("inc_window_font_size", Equal, WM_Modifier_Alt),
  UISHELL_BIND("dec_window_font_size", Minus, WM_Modifier_Alt),
};

read_only global UIShell_DefaultBinding uishell_shell_window_default_binding_table[] =
{
  UISHELL_BIND("toggle_fullscreen", Return, WM_Modifier_Accel),
  UISHELL_BIND("toggle_dev_menu", Tick, WM_Modifier_Accel|WM_Modifier_Shift),
};

read_only global UIShell_DefaultBinding uishell_shell_panel_default_binding_table[] =
{
  UISHELL_BIND("new_panel_right", P, WM_Modifier_Accel),
  UISHELL_BIND("new_panel_down", Minus, WM_Modifier_Accel),
  UISHELL_BIND("rotate_panel_columns", 2, WM_Modifier_Accel),
  UISHELL_BIND("next_panel", Comma, WM_Modifier_Accel),
  UISHELL_BIND("prev_panel", Comma, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("focus_panel_right", Right, WM_Modifier_Accel|WM_Modifier_Alt),
  UISHELL_BIND("focus_panel_left", Left, WM_Modifier_Accel|WM_Modifier_Alt),
  UISHELL_BIND("focus_panel_up", Up, WM_Modifier_Accel|WM_Modifier_Alt),
  UISHELL_BIND("focus_panel_down", Down, WM_Modifier_Accel|WM_Modifier_Alt),
  UISHELL_BIND("close_panel", P, WM_Modifier_Accel|WM_Modifier_Shift|WM_Modifier_Alt),
};

read_only global UIShell_DefaultBinding uishell_shell_app_default_binding_table[] =
{
  UISHELL_BIND("undo", Z, WM_Modifier_Accel),
  UISHELL_BIND("redo", Y, WM_Modifier_Accel),
  UISHELL_BIND("go_back", Left, WM_Modifier_Alt),
  UISHELL_BIND("go_forward", Right, WM_Modifier_Alt),
};

read_only global UIShell_DefaultBinding uishell_shell_tab_default_binding_table[] =
{
  UISHELL_BIND("next_tab", PageDown, WM_Modifier_Accel),
  UISHELL_BIND("prev_tab", PageUp, WM_Modifier_Accel),
  UISHELL_BIND("next_tab", Tab, WM_Modifier_Accel),
  UISHELL_BIND("prev_tab", Tab, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("move_tab_right", PageDown, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("move_tab_left", PageUp, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("close_tab", W, WM_Modifier_Accel),
  UISHELL_BIND("tab_bar_top", Up, WM_Modifier_Accel|WM_Modifier_Shift|WM_Modifier_Alt),
  UISHELL_BIND("tab_bar_bottom", Down, WM_Modifier_Accel|WM_Modifier_Shift|WM_Modifier_Alt),
  UISHELL_BIND("open_tab", T, WM_Modifier_Accel),
  UISHELL_BIND("tab_settings", T, WM_Modifier_Accel|WM_Modifier_Alt),
};

read_only global UIShell_DefaultBinding uishell_shell_ui_event_default_binding_table[] =
{
  UISHELL_BIND("edit", F2, 0),
  UISHELL_BIND("accept", Return, 0),
  UISHELL_BIND("accept", Space, 0),
  UISHELL_BIND("cancel", Esc, 0),
  UISHELL_BIND("focus_menu", D, WM_Modifier_Alt),

  UISHELL_BIND("move_left", Left, 0),
  UISHELL_BIND("move_right", Right, 0),
  UISHELL_BIND("move_up", Up, 0),
  UISHELL_BIND("move_down", Down, 0),
  UISHELL_BIND("move_left_select", Left, WM_Modifier_Shift),
  UISHELL_BIND("move_right_select", Right, WM_Modifier_Shift),
  UISHELL_BIND("move_up_select", Up, WM_Modifier_Shift),
  UISHELL_BIND("move_down_select", Down, WM_Modifier_Shift),
  UISHELL_BIND("move_left_chunk", Left, WM_Modifier_Accel),
  UISHELL_BIND("move_right_chunk", Right, WM_Modifier_Accel),
  UISHELL_BIND("move_up_chunk", Up, WM_Modifier_Accel),
  UISHELL_BIND("move_down_chunk", Down, WM_Modifier_Accel),
  UISHELL_BIND("move_up_page", PageUp, 0),
  UISHELL_BIND("move_down_page", PageDown, 0),
  UISHELL_BIND("move_up_whole", Home, WM_Modifier_Accel),
  UISHELL_BIND("move_down_whole", End, WM_Modifier_Accel),
  UISHELL_BIND("move_left_chunk_select", Left, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("move_right_chunk_select", Right, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("move_up_chunk_select", Up, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("move_down_chunk_select", Down, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("move_up_page_select", PageUp, WM_Modifier_Shift),
  UISHELL_BIND("move_down_page_select", PageDown, WM_Modifier_Shift),
  UISHELL_BIND("move_up_whole_select", Home, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("move_down_whole_select", End, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("move_up_reorder", Up, WM_Modifier_Alt),
  UISHELL_BIND("move_down_reorder", Down, WM_Modifier_Alt),
  UISHELL_BIND("move_home", Home, 0),
  UISHELL_BIND("move_end", End, 0),
  UISHELL_BIND("move_home_select", Home, WM_Modifier_Shift),
  UISHELL_BIND("move_end_select", End, WM_Modifier_Shift),
  UISHELL_BIND("select_all", A, WM_Modifier_Accel),
  UISHELL_BIND("delete_single", Delete, 0),
  UISHELL_BIND("delete_chunk", Delete, WM_Modifier_Accel),
  UISHELL_BIND("backspace_single", Backspace, 0),
  UISHELL_BIND("backspace_chunk", Backspace, WM_Modifier_Accel),
  UISHELL_BIND("copy", C, WM_Modifier_Accel),
  UISHELL_BIND("copy", Insert, WM_Modifier_Accel),
  UISHELL_BIND("cut", X, WM_Modifier_Accel),
  UISHELL_BIND("paste", V, WM_Modifier_Accel),
  UISHELL_BIND("paste", Insert, WM_Modifier_Shift),
  UISHELL_BIND("insert_text", Null, 0),
  UISHELL_BIND("move_next", Tab, 0),
  UISHELL_BIND("move_prev", Tab, WM_Modifier_Shift),
};

read_only global UIShell_DefaultBinding uishell_shell_query_default_binding_table[] =
{
  UISHELL_BIND("open_palette", F1, 0),
  UISHELL_BIND("open_palette", P, WM_Modifier_Accel|WM_Modifier_Shift),
};

#define UISHELL_SHELL_CMD_PACK_BINDING_FUNCTIONS(prefix, table) \
internal U64 \
prefix##_binding_count(void) \
{ \
  return ArrayCount(table); \
} \
internal UIShell_DefaultBinding \
prefix##_binding_from_index(U64 idx) \
{ \
  UIShell_DefaultBinding result = {0}; \
  if(idx < ArrayCount(table)) \
  { \
    result = table[idx]; \
  } \
  return result; \
}

UISHELL_SHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_shell_app_cmd_pack, uishell_shell_app_default_binding_table)
UISHELL_SHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_shell_query_cmd_pack, uishell_shell_query_default_binding_table)
UISHELL_SHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_shell_font_cmd_pack, uishell_shell_font_default_binding_table)
UISHELL_SHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_shell_window_cmd_pack, uishell_shell_window_default_binding_table)
UISHELL_SHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_shell_panel_cmd_pack, uishell_shell_panel_default_binding_table)
UISHELL_SHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_shell_tab_cmd_pack, uishell_shell_tab_default_binding_table)
UISHELL_SHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_shell_ui_event_cmd_pack, uishell_shell_ui_event_default_binding_table)

#undef UISHELL_SHELL_CMD_PACK_BINDING_FUNCTIONS

#undef UISHELL_BIND

////////////////////////////////
//~ rjf: Shell Command Pack Registration

internal void uishell_register_shell_cmd_packs(void);

#endif // SHELL_COMMANDS_H
