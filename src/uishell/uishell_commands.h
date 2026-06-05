// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UISHELL_COMMANDS_H
#define UISHELL_COMMANDS_H

////////////////////////////////
//~ rjf: Shell Command Metadata

typedef enum UIShell_RegSlot
{
  UIShell_RegSlot_Null,
  UIShell_RegSlot_FilePath,
  UIShell_RegSlot_Cfg,
  UIShell_RegSlot_CmdName,
  UIShell_RegSlot_String,
  UIShell_RegSlot_Cursor,
  UIShell_RegSlot_Vaddr,
  UIShell_RegSlot_COUNT
}
UIShell_RegSlot;

typedef struct UIShell_Query UIShell_Query;
struct UIShell_Query
{
  UIShell_QueryFlags flags;
  UIShell_RegSlot slot;
  String8 expr;
  String8 view_name;
};

typedef struct UIShell_CmdInfo UIShell_CmdInfo;
struct UIShell_CmdInfo
{
  String8 string;
  String8 display_name;
  RD_IconKind icon_kind;
  String8 description;
  String8 search_tags;
  String8 ctx_filter;
  UIShell_CmdFlags flags;
  UIShell_Query query;
};

read_only global UIShell_CmdInfo uishell_nil_cmd_info = {0};

#define UISHELL_CMD_FLAG_UI  UIShell_CmdFlag_ListInUI
#define UISHELL_CMD_FLAG_TAB (UIShell_CmdFlag_ListInUI|UIShell_CmdFlag_ListInTab)
#define UISHELL_Q_NONE       {0, UIShell_RegSlot_Null, {0}, {0}}
#define UISHELL_Q_FILE       {UIShell_QueryFlag_AllowFiles|UIShell_QueryFlag_Floating|UIShell_QueryFlag_Required, UIShell_RegSlot_FilePath, str8_lit_comp("folder:\"$input\""), {0}}
#define UISHELL_Q_FILE_OPT   {UIShell_QueryFlag_AllowFiles|UIShell_QueryFlag_Floating, UIShell_RegSlot_Null, {0}, {0}}
#define UISHELL_Q_RECENT     {UIShell_QueryFlag_Floating|UIShell_QueryFlag_Required, UIShell_RegSlot_Cfg, str8_lit_comp("query:recent_projects"), {0}}
#define UISHELL_Q_COMMANDS   {UIShell_QueryFlag_Floating|UIShell_QueryFlag_Required, UIShell_RegSlot_CmdName, str8_lit_comp("query:commands"), str8_lit_comp("commands")}
#define UISHELL_Q_TABS       {UIShell_QueryFlag_Floating|UIShell_QueryFlag_Required, UIShell_RegSlot_CmdName, str8_lit_comp("query:tab_commands"), str8_lit_comp("commands")}
#define UISHELL_Q_STRING     {UIShell_QueryFlag_CodeInput|UIShell_QueryFlag_KeepOldInput|UIShell_QueryFlag_SelectOldInput|UIShell_QueryFlag_Required, UIShell_RegSlot_String, {0}, {0}}
#define UISHELL_Q_CURSOR     {UIShell_QueryFlag_CodeInput|UIShell_QueryFlag_Required, UIShell_RegSlot_Cursor, {0}, {0}}
#define UISHELL_Q_ADDR       {UIShell_QueryFlag_CodeInput|UIShell_QueryFlag_Required, UIShell_RegSlot_Vaddr, {0}, {0}}
#define UISHELL_CMD(name, display, icon, desc, tags, flags, query) {str8_lit_comp(name), str8_lit_comp(display), RD_IconKind_##icon, str8_lit_comp(desc), str8_lit_comp(tags), {0}, flags, query}

read_only global UIShell_CmdInfo uishell_cmd_info_table[] =
{
  UISHELL_CMD("exit", "Exit", X, "Exits the app.", "quit,close", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("open_palette", "Open Palette", List, "Opens the palette.", "help,cmd,lister", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("run_command", "Run Command", Null, "Runs a command from the command palette.", "help,cmd", UISHELL_CMD_FLAG_UI, UISHELL_Q_COMMANDS),
  
  UISHELL_CMD("inc_window_font_size", "Increase Window Font Size", Null, "Increases the window font size.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("dec_window_font_size", "Decrease Window Font Size", Null, "Decreases the window font size.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("inc_view_font_size", "Increase View Font Size", Null, "Increases the view font size.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("dec_view_font_size", "Decrease View Font Size", Null, "Decreases the view font size.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  
  UISHELL_CMD("open_window", "Open New Window", Window, "Opens a new window.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("window_settings", "Window Settings", Gear, "Opens settings for a window.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("close_window", "Close Window", Window, "Closes the current window.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("window_close_menu", "Window Close Menu", Null, "Closes the current window.", "", 0, UISHELL_Q_NONE),
  UISHELL_CMD("toggle_fullscreen", "Toggle Fullscreen", Window, "Toggles fullscreen view.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("reset_to_default_bindings", "Reset To Default Bindings", Null, "Resets all keybindings to their defaults.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("reset_to_default_panels", "Reset To Default Panel Layout", Window, "Resets the window to the default panel layout.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("reset_to_compact_panels", "Reset To Compact Panel Layout", Window, "Resets the window to the compact panel layout.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("reset_to_simple_panels", "Reset To Simple Panel Layout", Window, "Resets the window to the simple panel layout.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  
  UISHELL_CMD("new_panel_left", "Split Panel Left", XSplit, "Creates a new panel to the left.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("new_panel_up", "Split Panel Up", YSplit, "Creates a new panel above.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("new_panel_right", "Split Panel Right", XSplit, "Creates a new panel to the right.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("new_panel_down", "Split Panel Down", YSplit, "Creates a new panel below.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("split_panel", "Split Panel", Null, "Splits the current panel.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("rotate_panel_columns", "Rotate Panel Columns", Null, "Rotates panel columns.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("next_panel", "Focus Next Panel", RightArrow, "Focuses the next panel.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("prev_panel", "Focus Previous Panel", LeftArrow, "Focuses the previous panel.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("focus_panel", "Focus Panel", Null, "Focuses a panel.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("focus_panel_right", "Focus Panel Right", RightArrow, "Focuses the panel to the right.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("focus_panel_left", "Focus Panel Left", LeftArrow, "Focuses the panel to the left.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("focus_panel_up", "Focus Panel Up", UpArrow, "Focuses the panel above.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("focus_panel_down", "Focus Panel Down", DownArrow, "Focuses the panel below.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("close_panel", "Close Panel", ClosePanel, "Closes the current panel.", "panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  
  UISHELL_CMD("focus_tab", "Focus Tab", Null, "Focuses a tab.", "tab", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("next_tab", "Focus Next Tab", RightArrow, "Focuses the next tab.", "tab", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("prev_tab", "Focus Previous Tab", LeftArrow, "Focuses the previous tab.", "tab", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_tab_right", "Move Tab Right", RightArrow, "Moves the current tab right.", "tab", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_tab_left", "Move Tab Left", LeftArrow, "Moves the current tab left.", "tab", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("open_tab", "Open New Tab", Null, "Opens a new tab.", "tab,view", UISHELL_CMD_FLAG_UI, UISHELL_Q_TABS),
  UISHELL_CMD("duplicate_tab", "Duplicate Tab", Duplicate, "Duplicates the current tab.", "tab", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("copy_tab_full_path", "Copy Full Path", Clipboard, "Copies the current tab path.", "tab,path", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("close_tab", "Close Tab", X, "Closes the current tab.", "tab", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_view", "Move View", Null, "Moves the current view.", "tab,panel", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("tab_bar_top", "Anchor Tab Bar To Top", UpArrow, "Moves tab bars to the top.", "tab", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("tab_bar_bottom", "Anchor Tab Bar To Bottom", DownArrow, "Moves tab bars to the bottom.", "tab", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("tab_settings", "Selected Tab Settings", Gear, "Opens settings for a tab.", "tab,view,options", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  
  UISHELL_CMD("set_current_path", "Set Current Path", FileOutline, "Sets the current file browsing path.", "path", 0, UISHELL_Q_NONE),
  UISHELL_CMD("open", "Open", FileOutline, "Opens a file.", "file,open", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("show_file_in_explorer", "Show File In Explorer", FolderClosedFilled, "Shows a file in the system file explorer.", "file,path", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("new_user", "New User", Add, "Creates a new user file.", "user,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE_OPT),
  UISHELL_CMD("new_project", "New Project", Add, "Creates a new project file.", "project,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE_OPT),
  UISHELL_CMD("open_user", "Open User", Person, "Opens a user file.", "user,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("open_project", "Open Project", Briefcase, "Opens a project file.", "project,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("open_recent_project", "Open Recent Project", Briefcase, "Opens a recently used project file.", "project,recent", UISHELL_CMD_FLAG_UI, UISHELL_Q_RECENT),
  UISHELL_CMD("save_user", "Save User", Save, "Saves user data to a file.", "user,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("save_project", "Save Project", Save, "Saves project data to a file.", "project,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("user_settings", "User Settings", Gear, "Opens user settings.", "settings", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("project_settings", "Project Settings", Gear, "Opens project settings.", "settings", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  
  UISHELL_CMD("undo", "Undo", Undo, "Undoes the last edit.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("redo", "Redo", Redo, "Redoes the last edit.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("go_back", "Go Back", LeftArrow, "Moves backward in focus history.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("go_forward", "Go Forward", RightArrow, "Moves forward in focus history.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("edit", "Edit", Pencil, "Begins editing.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("accept", "Accept", CheckFilled, "Accepts the active interaction.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("cancel", "Cancel", X, "Cancels the active interaction.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("focus_menu", "Focus Menu", List, "Focuses the active menu.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_left", "Move Left", Null, "Moves left.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_right", "Move Right", Null, "Moves right.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_up", "Move Up", Null, "Moves up.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_down", "Move Down", Null, "Moves down.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_left_select", "Move Left Select", Null, "Extends selection left.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_right_select", "Move Right Select", Null, "Extends selection right.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_up_select", "Move Up Select", Null, "Extends selection up.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_down_select", "Move Down Select", Null, "Extends selection down.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_left_chunk", "Move Left Chunk", Null, "Moves left by a chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_right_chunk", "Move Right Chunk", Null, "Moves right by a chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_up_chunk", "Move Up Chunk", Null, "Moves up by a chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_down_chunk", "Move Down Chunk", Null, "Moves down by a chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_up_page", "Move Up Page", Null, "Moves up by a page.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_down_page", "Move Down Page", Null, "Moves down by a page.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_up_whole", "Move Up Whole", Null, "Moves to the top.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_down_whole", "Move Down Whole", Null, "Moves to the bottom.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_left_chunk_select", "Move Left Chunk Select", Null, "Extends selection left by a chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_right_chunk_select", "Move Right Chunk Select", Null, "Extends selection right by a chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_up_chunk_select", "Move Up Chunk Select", Null, "Extends selection up by a chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_down_chunk_select", "Move Down Chunk Select", Null, "Extends selection down by a chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_up_page_select", "Move Up Page Select", Null, "Extends selection up by a page.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_down_page_select", "Move Down Page Select", Null, "Extends selection down by a page.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_up_whole_select", "Move Up Whole Select", Null, "Extends selection to the top.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_down_whole_select", "Move Down Whole Select", Null, "Extends selection to the bottom.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_up_reorder", "Move Up Reorder", Null, "Moves the current item up.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_down_reorder", "Move Down Reorder", Null, "Moves the current item down.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_home", "Move Home", Null, "Moves to the beginning.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_end", "Move End", Null, "Moves to the end.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_home_select", "Move Home Select", Null, "Extends selection to the beginning.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_end_select", "Move End Select", Null, "Extends selection to the end.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("select_all", "Select All", Null, "Selects all.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("delete_single", "Delete Single", Null, "Deletes one item.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("delete_chunk", "Delete Chunk", Null, "Deletes one chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("backspace_single", "Backspace Single", Null, "Backspaces one item.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("backspace_chunk", "Backspace Chunk", Null, "Backspaces one chunk.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("copy", "Copy", Clipboard, "Copies the current selection.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("cut", "Cut", Clipboard, "Cuts the current selection.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("paste", "Paste", Clipboard, "Pastes clipboard contents.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_next", "Move Next", Null, "Moves to the next item.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("move_prev", "Move Previous", Null, "Moves to the previous item.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("goto_line", "Go To Line", Null, "Jumps to a line number.", "line", UISHELL_CMD_FLAG_UI, UISHELL_Q_CURSOR),
  UISHELL_CMD("center_cursor", "Center Cursor", Null, "Centers the cursor.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("contain_cursor", "Contain Cursor", Null, "Scrolls to contain the cursor.", "", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("find_next", "Find Next", Find, "Finds the next match.", "find,search", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("find_prev", "Find Previous", Find, "Finds the previous match.", "find,search", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("search", "Search", Find, "Begins searching.", "find,search", UISHELL_CMD_FLAG_UI, UISHELL_Q_STRING),
  UISHELL_CMD("search_backwards", "Search Backwards", Find, "Begins searching backwards.", "find,search", UISHELL_CMD_FLAG_UI, UISHELL_Q_STRING),
  UISHELL_CMD("goto_address", "Go To Address", Null, "Jumps to a file offset in the binary view.", "offset,address", UISHELL_CMD_FLAG_UI, UISHELL_Q_ADDR),
  
  UISHELL_CMD("output", "Output", List, "Opens an Output tab.", "tab", UISHELL_CMD_FLAG_TAB, UISHELL_Q_NONE),
  UISHELL_CMD("text", "Text", FileOutline, "Opens a Text tab.", "tab,file", UISHELL_CMD_FLAG_TAB, UISHELL_Q_NONE),
  UISHELL_CMD("binary", "Binary", Grid, "Opens a Binary tab.", "tab,file,hex", UISHELL_CMD_FLAG_TAB, UISHELL_Q_NONE),
};

internal UIShell_CmdInfo *
uishell_cmd_info_from_name(String8 name)
{
  UIShell_CmdInfo *result = &uishell_nil_cmd_info;
  for EachElement(idx, uishell_cmd_info_table)
  {
    if(str8_match(name, uishell_cmd_info_table[idx].string, 0))
    {
      result = &uishell_cmd_info_table[idx];
      break;
    }
  }
  return result;
}

internal UIShell_AppRegSlot uishell_app_reg_slot_from_query_reg_slot(UIShell_RegSlot slot);

internal UIShell_AppCmdInfo
uishell_app_cmd_info_from_cmd_info(UIShell_CmdInfo *info)
{
  UIShell_AppCmdInfo result = {0};
  if(info != &uishell_nil_cmd_info)
  {
    result.string = info->string;
    result.display_name = info->display_name;
    result.icon_kind = info->icon_kind;
    result.description = info->description;
    result.search_tags = info->search_tags;
    result.ctx_filter = info->ctx_filter;
    result.flags = info->flags;
    result.query_flags = info->query.flags;
    result.query_slot = uishell_app_reg_slot_from_query_reg_slot(info->query.slot);
    result.query_expr = info->query.expr;
    result.query_view_name = info->query.view_name;
  }
  return result;
}

internal U64
uishell_cmd_pack_cmd_count(void)
{
  return ArrayCount(uishell_cmd_info_table);
}

internal UIShell_AppCmdInfo
uishell_cmd_pack_cmd_info_from_index(U64 idx)
{
  UIShell_AppCmdInfo result = {0};
  if(idx < ArrayCount(uishell_cmd_info_table))
  {
    result = uishell_app_cmd_info_from_cmd_info(&uishell_cmd_info_table[idx]);
  }
  return result;
}

internal UIShell_AppCmdInfo
uishell_cmd_pack_cmd_info_from_string(String8 string)
{
  UIShell_AppCmdInfo result = uishell_app_cmd_info_from_cmd_info(uishell_cmd_info_from_name(string));
  return result;
}

internal UIShell_AppRegSlot
uishell_app_reg_slot_from_query_reg_slot(UIShell_RegSlot slot)
{
  UIShell_AppRegSlot result = UIShell_AppRegSlot_Null;
  switch(slot)
  {
    default: break;
    case UIShell_RegSlot_Null:     {result = UIShell_AppRegSlot_Null;}break;
    case UIShell_RegSlot_FilePath: {result = UIShell_AppRegSlot_FilePath;}break;
    case UIShell_RegSlot_Cfg:      {result = UIShell_AppRegSlot_Cfg;}break;
    case UIShell_RegSlot_CmdName:  {result = UIShell_AppRegSlot_CmdName;}break;
    case UIShell_RegSlot_String:   {result = UIShell_AppRegSlot_String;}break;
    case UIShell_RegSlot_Cursor:   {result = UIShell_AppRegSlot_Cursor;}break;
    case UIShell_RegSlot_Vaddr:    {result = UIShell_AppRegSlot_Vaddr;}break;
  }
  return result;
}

internal B32
uishell_cmd_name_is_listed(String8 name)
{
  B32 result = (uishell_cmd_info_from_name(name) != &uishell_nil_cmd_info);
  return result;
}

internal B32
uishell_cmd_name_matches_listing_flags(String8 name, UIShell_CmdFlags flags)
{
  UIShell_CmdInfo *info = uishell_cmd_info_from_name(name);
  B32 result = (info != &uishell_nil_cmd_info && (info->flags & flags) == flags);
  return result;
}

internal B32
uishell_cmd_name_is_tab_fast_path(String8 name)
{
  UIShell_CmdInfo *info = uishell_cmd_info_from_name(name);
  B32 result = (info != &uishell_nil_cmd_info && (info->flags & UIShell_CmdFlag_ListInTab) != 0);
  return result;
}

#undef UISHELL_CMD
#undef UISHELL_Q_ADDR
#undef UISHELL_Q_CURSOR
#undef UISHELL_Q_STRING
#undef UISHELL_Q_TABS
#undef UISHELL_Q_COMMANDS
#undef UISHELL_Q_RECENT
#undef UISHELL_Q_FILE_OPT
#undef UISHELL_Q_FILE
#undef UISHELL_Q_NONE
#undef UISHELL_CMD_FLAG_TAB
#undef UISHELL_CMD_FLAG_UI

////////////////////////////////
//~ rjf: Shell Default Bindings

#define UISHELL_BIND(name, key, mods) {str8_lit_comp(name), {WM_Key_##key, mods}}

read_only global UIShell_DefaultBinding uishell_default_binding_table[] =
{
  UISHELL_BIND("inc_window_font_size", Equal, WM_Modifier_Alt),
  UISHELL_BIND("dec_window_font_size", Minus, WM_Modifier_Alt),
  UISHELL_BIND("toggle_fullscreen", Return, WM_Modifier_Ctrl),
  
  UISHELL_BIND("new_panel_right", P, WM_Modifier_Ctrl),
  UISHELL_BIND("new_panel_down", Minus, WM_Modifier_Ctrl),
  UISHELL_BIND("rotate_panel_columns", 2, WM_Modifier_Ctrl),
  UISHELL_BIND("next_panel", Comma, WM_Modifier_Ctrl),
  UISHELL_BIND("prev_panel", Comma, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("focus_panel_right", Right, WM_Modifier_Ctrl|WM_Modifier_Alt),
  UISHELL_BIND("focus_panel_left", Left, WM_Modifier_Ctrl|WM_Modifier_Alt),
  UISHELL_BIND("focus_panel_up", Up, WM_Modifier_Ctrl|WM_Modifier_Alt),
  UISHELL_BIND("focus_panel_down", Down, WM_Modifier_Ctrl|WM_Modifier_Alt),
  
  UISHELL_BIND("undo", Z, WM_Modifier_Ctrl),
  UISHELL_BIND("redo", Y, WM_Modifier_Ctrl),
  UISHELL_BIND("go_back", Left, WM_Modifier_Alt),
  UISHELL_BIND("go_forward", Right, WM_Modifier_Alt),
  
  UISHELL_BIND("close_panel", P, WM_Modifier_Ctrl|WM_Modifier_Shift|WM_Modifier_Alt),
  UISHELL_BIND("next_tab", PageDown, WM_Modifier_Ctrl),
  UISHELL_BIND("prev_tab", PageUp, WM_Modifier_Ctrl),
  UISHELL_BIND("next_tab", Tab, WM_Modifier_Ctrl),
  UISHELL_BIND("prev_tab", Tab, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("move_tab_right", PageDown, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("move_tab_left", PageUp, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("close_tab", W, WM_Modifier_Ctrl),
  UISHELL_BIND("tab_bar_top", Up, WM_Modifier_Ctrl|WM_Modifier_Shift|WM_Modifier_Alt),
  UISHELL_BIND("tab_bar_bottom", Down, WM_Modifier_Ctrl|WM_Modifier_Shift|WM_Modifier_Alt),
  UISHELL_BIND("open_tab", T, WM_Modifier_Ctrl),
  UISHELL_BIND("tab_settings", T, WM_Modifier_Ctrl|WM_Modifier_Alt),
  
  UISHELL_BIND("open", O, WM_Modifier_Ctrl),
  UISHELL_BIND("new_project", N, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("open_project", O, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("save_project", S, WM_Modifier_Ctrl|WM_Modifier_Shift),
  
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
  UISHELL_BIND("move_left_chunk", Left, WM_Modifier_Ctrl),
  UISHELL_BIND("move_right_chunk", Right, WM_Modifier_Ctrl),
  UISHELL_BIND("move_up_chunk", Up, WM_Modifier_Ctrl),
  UISHELL_BIND("move_down_chunk", Down, WM_Modifier_Ctrl),
  UISHELL_BIND("move_up_page", PageUp, 0),
  UISHELL_BIND("move_down_page", PageDown, 0),
  UISHELL_BIND("move_up_whole", Home, WM_Modifier_Ctrl),
  UISHELL_BIND("move_down_whole", End, WM_Modifier_Ctrl),
  UISHELL_BIND("move_left_chunk_select", Left, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("move_right_chunk_select", Right, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("move_up_chunk_select", Up, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("move_down_chunk_select", Down, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("move_up_page_select", PageUp, WM_Modifier_Shift),
  UISHELL_BIND("move_down_page_select", PageDown, WM_Modifier_Shift),
  UISHELL_BIND("move_up_whole_select", Home, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("move_down_whole_select", End, WM_Modifier_Ctrl|WM_Modifier_Shift),
  UISHELL_BIND("move_up_reorder", Up, WM_Modifier_Alt),
  UISHELL_BIND("move_down_reorder", Down, WM_Modifier_Alt),
  UISHELL_BIND("move_home", Home, 0),
  UISHELL_BIND("move_end", End, 0),
  UISHELL_BIND("move_home_select", Home, WM_Modifier_Shift),
  UISHELL_BIND("move_end_select", End, WM_Modifier_Shift),
  UISHELL_BIND("select_all", A, WM_Modifier_Ctrl),
  UISHELL_BIND("delete_single", Delete, 0),
  UISHELL_BIND("delete_chunk", Delete, WM_Modifier_Ctrl),
  UISHELL_BIND("backspace_single", Backspace, 0),
  UISHELL_BIND("backspace_chunk", Backspace, WM_Modifier_Ctrl),
  UISHELL_BIND("copy", C, WM_Modifier_Ctrl),
  UISHELL_BIND("copy", Insert, WM_Modifier_Ctrl),
  UISHELL_BIND("cut", X, WM_Modifier_Ctrl),
  UISHELL_BIND("paste", V, WM_Modifier_Ctrl),
  UISHELL_BIND("paste", Insert, WM_Modifier_Shift),
  UISHELL_BIND("insert_text", Null, 0),
  
  UISHELL_BIND("move_next", Tab, 0),
  UISHELL_BIND("move_prev", Tab, WM_Modifier_Shift),
  UISHELL_BIND("goto_line", G, WM_Modifier_Ctrl),
  UISHELL_BIND("goto_address", G, WM_Modifier_Alt),
  UISHELL_BIND("search", F, WM_Modifier_Ctrl),
  UISHELL_BIND("search_backwards", R, WM_Modifier_Ctrl),
  UISHELL_BIND("find_next", F3, 0),
  UISHELL_BIND("find_prev", F3, WM_Modifier_Ctrl),
  
  UISHELL_BIND("open_palette", F1, 0),
  UISHELL_BIND("open_palette", P, WM_Modifier_Ctrl|WM_Modifier_Shift),
};

internal U64
uishell_cmd_pack_binding_count(void)
{
  return ArrayCount(uishell_default_binding_table);
}

internal UIShell_DefaultBinding
uishell_cmd_pack_binding_from_index(U64 idx)
{
  UIShell_DefaultBinding result = {0};
  if(idx < ArrayCount(uishell_default_binding_table))
  {
    result = uishell_default_binding_table[idx];
  }
  return result;
}

#undef UISHELL_BIND

#endif // UISHELL_COMMANDS_H
