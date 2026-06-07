// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UISHELL_COMMANDS_H
#define UISHELL_COMMANDS_H

////////////////////////////////
//~ rjf: App Command Metadata

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
#define UISHELL_Q_STRING     {UIShell_QueryFlag_CodeInput|UIShell_QueryFlag_KeepOldInput|UIShell_QueryFlag_SelectOldInput|UIShell_QueryFlag_Required, UIShell_RegSlot_String, {0}, {0}}
#define UISHELL_Q_CURSOR     {UIShell_QueryFlag_CodeInput|UIShell_QueryFlag_Required, UIShell_RegSlot_Cursor, {0}, {0}}
#define UISHELL_Q_ADDR       {UIShell_QueryFlag_CodeInput|UIShell_QueryFlag_Required, UIShell_RegSlot_Vaddr, {0}, {0}}
#define UISHELL_CMD(name, display, icon, desc, tags, flags, query) {str8_lit_comp(name), str8_lit_comp(display), RD_IconKind_##icon, str8_lit_comp(desc), str8_lit_comp(tags), {0}, flags, query}

read_only global UIShell_CmdInfo uishell_app_config_cmd_info_table[] =
{
  UISHELL_CMD("new_user", "New User", Add, "Creates a new user file.", "user,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE_OPT),
  UISHELL_CMD("new_project", "New Project", Add, "Creates a new project file.", "project,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE_OPT),
  UISHELL_CMD("open_user", "Open User", Person, "Opens a user file.", "user,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("open_project", "Open Project", Briefcase, "Opens a project file.", "project,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("open_recent_project", "Open Recent Project", Briefcase, "Opens a recently used project file.", "project,recent", UISHELL_CMD_FLAG_UI, UISHELL_Q_RECENT),
  UISHELL_CMD("save_user", "Save User", Save, "Saves user data to a file.", "user,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("save_project", "Save Project", Save, "Saves project data to a file.", "project,config", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("user_settings", "User Settings", Gear, "Opens user settings.", "settings", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
  UISHELL_CMD("project_settings", "Project Settings", Gear, "Opens project settings.", "settings", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
};

read_only global UIShell_CmdInfo uishell_app_file_cmd_info_table[] =
{
  UISHELL_CMD("set_current_path", "Set Current Path", FileOutline, "Sets the current file browsing path.", "path", 0, UISHELL_Q_NONE),
  UISHELL_CMD("open", "Open", FileOutline, "Opens a file.", "file,open", UISHELL_CMD_FLAG_UI, UISHELL_Q_FILE),
  UISHELL_CMD("show_file_in_explorer", "Show File In Explorer", FolderClosedFilled, "Shows a file in the system file explorer.", "file,path", UISHELL_CMD_FLAG_UI, UISHELL_Q_NONE),
};

read_only global UIShell_CmdInfo uishell_app_viewer_cmd_info_table[] =
{
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
  UISHELL_CMD("terminal", "Terminal", Machine, "Opens a Terminal tab.", "tab,terminal,shell", UISHELL_CMD_FLAG_TAB, UISHELL_Q_NONE),
  UISHELL_CMD("terminal_fixture", "Terminal Fixture", Machine, "Opens a deterministic terminal glyph fixture tab.", "tab,terminal,glyph,fixture", UISHELL_CMD_FLAG_TAB, UISHELL_Q_NONE),
  UISHELL_CMD("binary", "Binary", Grid, "Opens a Binary tab.", "tab,file,hex", UISHELL_CMD_FLAG_TAB, UISHELL_Q_NONE),
};

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

#define UISHELL_CMD_PACK_INFO_FUNCTIONS(prefix, table) \
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
    result = uishell_app_cmd_info_from_cmd_info(&table[idx]); \
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
      result = uishell_app_cmd_info_from_cmd_info(&table[idx]); \
      break; \
    } \
  } \
  return result; \
}

UISHELL_CMD_PACK_INFO_FUNCTIONS(uishell_app_config_cmd_pack, uishell_app_config_cmd_info_table)
UISHELL_CMD_PACK_INFO_FUNCTIONS(uishell_app_file_cmd_pack, uishell_app_file_cmd_info_table)
UISHELL_CMD_PACK_INFO_FUNCTIONS(uishell_app_viewer_cmd_pack, uishell_app_viewer_cmd_info_table)

#undef UISHELL_CMD_PACK_INFO_FUNCTIONS

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

#undef UISHELL_CMD
#undef UISHELL_Q_ADDR
#undef UISHELL_Q_CURSOR
#undef UISHELL_Q_STRING
#undef UISHELL_Q_RECENT
#undef UISHELL_Q_FILE_OPT
#undef UISHELL_Q_FILE
#undef UISHELL_Q_NONE
#undef UISHELL_CMD_FLAG_TAB
#undef UISHELL_CMD_FLAG_UI

////////////////////////////////
//~ rjf: App Default Bindings

#define UISHELL_BIND(name, key, mods) {str8_lit_comp(name), {WM_Key_##key, mods}}

read_only global UIShell_DefaultBinding uishell_app_config_default_binding_table[] =
{
  UISHELL_BIND("new_project", N, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("open_project", O, WM_Modifier_Accel|WM_Modifier_Shift),
  UISHELL_BIND("save_project", S, WM_Modifier_Accel|WM_Modifier_Shift),
};

read_only global UIShell_DefaultBinding uishell_app_file_default_binding_table[] =
{
  UISHELL_BIND("open", O, WM_Modifier_Accel),
};

read_only global UIShell_DefaultBinding uishell_app_viewer_default_binding_table[] =
{
  UISHELL_BIND("goto_line", G, WM_Modifier_Accel),
  UISHELL_BIND("goto_address", G, WM_Modifier_Alt),
  UISHELL_BIND("search", F, WM_Modifier_Accel),
  UISHELL_BIND("search_backwards", R, WM_Modifier_Accel),
  UISHELL_BIND("find_next", F3, 0),
  UISHELL_BIND("find_prev", F3, WM_Modifier_Accel),
};

#define UISHELL_CMD_PACK_BINDING_FUNCTIONS(prefix, table) \
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

UISHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_app_config_cmd_pack, uishell_app_config_default_binding_table)
UISHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_app_file_cmd_pack, uishell_app_file_default_binding_table)
UISHELL_CMD_PACK_BINDING_FUNCTIONS(uishell_app_viewer_cmd_pack, uishell_app_viewer_default_binding_table)

#undef UISHELL_CMD_PACK_BINDING_FUNCTIONS

#undef UISHELL_BIND

#endif // UISHELL_COMMANDS_H
