// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UISHELL_META_H
#define UISHELL_META_H

#include "uishell/generated/uishell.meta.c"

#define RD_APP_BINDING_VERSION_REMAP_OLD_NAME_TABLE uishell_binding_version_remap_old_name_table
#define RD_APP_BINDING_VERSION_REMAP_NEW_NAME_TABLE uishell_binding_version_remap_new_name_table

internal B32
uishell_view_name_is_listed(String8 name)
{
  B32 result = (str8_match(name, str8_lit("text"), 0) ||
                str8_match(name, str8_lit("binary"), 0));
  return result;
}

typedef struct UIShell_NameSchemaInfo UIShell_NameSchemaInfo;
struct UIShell_NameSchemaInfo
{
  String8 name;
  B32 is_view;
  String8 schema;
};

read_only global UIShell_NameSchemaInfo uishell_name_schema_info_table[] =
{
  {str8_lit_comp("user"), 0, str8_lit_comp(
    "x:{"
    "@display_name('Animations') @description(\"Enables animations.\") @default(1) 'animations': bool,"
    "@display_name('Scrolling Animations') @description(\"Enables scrolling animations.\") @expand_if(\"$.animations\") @default(1) 'scrolling_animations': bool,"
    "@display_name('Tooltip Animations') @description(\"Enables tooltip animations.\") @expand_if(\"$.animations\") @default(1) 'tooltip_animations': bool,"
    "@display_name('Menu Animations') @description(\"Enables menu animations.\") @expand_if(\"$.animations\") @default(1) 'menu_animations': bool,"
    "@display_name('UI Font') @description(\"The name of, or path to, the font used when displaying non-code UI elements.\") @default('') 'main_font': string,"
    "@display_name('Code Font') @description(\"The name of, or path to, the font used when displaying code.\") @default('') 'code_font': string,"
    "@default(\"Default (Dark)\") @display_name('User Theme') @description(\"The user's theme, which describes all colors used throughout the UI.\") 'theme': string,"
    "@no_expand @display_name('User Theme') 'theme_colors': set,"
    "@display_name('Autocompletion Lister') @description(\"Enables the autocompletion lister while typing expressions.\") @default(1) 'autocompletion_lister': bool,"
    "@display_name('View Call Argument Helper') @description(\"Enables the view call argument helper while typing expressions.\") @default(1) 'view_call_argument_helper': bool,"
    "@default(1) @display_name('Cursor Scope Lines') @description(\"Controls whether or not scopes containing the cursor in text views are drawn.\") 'cursor_scope_lines': bool,"
    "@default(1) @display_name('Cursor Scope End Annotations') @description(\"Controls whether or not ending annotations for scopes containing the cursor are drawn.\") 'cursor_scope_end_annotations': bool,"
    "@default(1) @display_name('Cursor Trail') @description(\"Controls whether or not a movement trail of the cursor is drawn.\") 'cursor_trail': bool,"
    "@default(0) @display_name('Opaque Backgrounds') @description(\"Controls whether or not all floating background colors are forced to be fully opaque.\") 'opaque_backgrounds': bool,"
    "@default(1) @display_name('Background Blur') @description(\"Controls whether or not occluded regions behind floating elements are blurred.\") 'background_blur': bool,"
    "@default(1) @display_name('Drop Shadows') @description(\"Controls whether or not drop shadows are drawn.\") 'drop_shadows': bool,"
    "@default(1.f) @display_name('Rounded Corner Amount') @description(\"Controls the degree to which UI corners are rounded.\") 'rounded_corner_amount': @range[0, 1] f32,"
    "@default(1) @display_name('Native Window Decorations') @description(\"Shows native traffic lights in custom window title bars.\") 'mac_window_decorations': bool,"
    "@default(0) @display_name('Native Menu Bar') @description(\"Uses the macOS menu bar instead of RAD's per-window menu.\") 'mac_native_menu_bar': bool,"
    "@default(2) @display_name('User Tab Width') 'tab_width': @range[1, 32] u64,"
    "@default(1) @display_name('Focus Menu Bar With Alt') @description(\"Mimics standard Windows behavior of focusing the menu bar using the Alt key.\") 'focus_menu_bar_with_alt': bool,"
    "@default(0) @display_name('Use Native File System Dialog') @description(\"Uses the operating system's file system dialog box.\") 'use_native_file_system_dialog': bool,"
    "@default(1) @display_name('Transient Tabs') @description(\"When snapping to source code locations, opens new files in a transient tab if they are not already open.\") 'transient_tabs': bool,"
    "}"
  )},
  {str8_lit_comp("project"), 0, str8_lit_comp(
    "x:{"
    "@display_name('Project Name') 'name': string,"
    "@default(2) @display_name('Project Tab Width') 'tab_width': @range[1, 32] u64,"
    "@display_name('Display Pointer Addresses Before Contents') @description(\"When visualizing pointers, always shows the address first.\") @default(0) display_pointer_addresses_before_contents: bool,"
    "@default(\"None\") @display_name('Project Theme') @description(\"The project's theme, which can override the user's theme.\") 'theme': string,"
    "@no_expand @display_name('Project Theme') @description(\"The project's theme colors, which can override the user's theme.\") 'theme_colors': set,"
    "}"
  )},
  {str8_lit_comp("theme_color"), 0, str8_lit_comp(
    "x:{"
    "@display_name('Tags') tags: string,"
    "@display_name('Value') value: @color @hex u32,"
    "}"
  )},
  {str8_lit_comp("window"), 0, str8_lit_comp(
    "x:{"
    "@default(1) @display_name('Smooth UI Text') @description(\"Controls whether or not UI text is fully anti-aliased.\") 'smooth_ui_text': bool,"
    "@default(1) @display_name('Hint UI Text') @description(\"Controls whether or not UI text is hinted.\") 'hint_ui_text': bool,"
    "@default(0) @display_name('Smooth Code Text') @description(\"Controls whether or not code text is fully anti-aliased.\") 'smooth_code_text': bool,"
    "@default(1) @display_name('Hint Code Text') @description(\"Controls whether or not code text is hinted.\") 'hint_code_text': bool,"
    "@default(11) @display_name('Window Font Size') @description(\"Controls the window's default font size.\") 'font_size': @range[6, 72] u64,"
    "@default(3.f) @display_name('Window Row Height') @description(\"Controls the window's default row height, in multiples of the font size.\") 'row_height': @range[1.75f, 5.f] f32,"
    "@default(3.f) @description(\"Controls the height of tabs, in multiples of the font size.\") 'tab_height': @range[1.75f, 5.f] f32,"
    "@default(1) @display_name('Use Project Theme') @description(\"Prefer using the project theme for this window, if any.\") 'use_project_theme': bool,"
    "}"
  )},
  {str8_lit_comp("tab"), 0, str8_lit_comp(
    "@row_commands(@file copy_tab_full_path, @file show_file_in_explorer, duplicate_tab, close_tab)"
    "x:{"
    "@override @display_name('Tab Font Size') @description(\"Controls the tab's font size.\") @no_callee_helper 'font_size': @range[6, 72] u64,"
    "}"
  )},
  {str8_lit_comp("text"), 1, str8_lit_comp(
    "@inherit(tab)"
    "x:{"
    "@description(\"An expression to describe data which should be viewed as text or code.\") 'expression': expr_string,"
    "@optional @description(\"The language that the text should be interpreted as being within.\") 'lang': code_string,"
    "@no_callee_helper @default(1) @description(\"Controls whether or not line numbers are shown.\") 'show_line_numbers': bool,"
    "@no_callee_helper @default(1) @display_name('Line Wrapping') @description(\"Splits textual lines into multiple visual lines.\") 'line_wrapping': bool,"
    "@no_callee_helper @default(0) @display_name('Scroll To Bottom On Change') @description(\"Scrolls to the bottom if the text is changed.\") 'scroll_to_bottom_on_change': bool,"
    "@no_callee_helper @no_revert @default(0) @display_name('Transient') @description(\"Controls whether or not this tab will be automatically replaced.\") 'auto': bool,"
    "}"
  )},
  {str8_lit_comp("binary"), 1, str8_lit_comp(
    "@inherit(tab)"
    "x:{"
    "@description(\"An expression to describe data which should be viewed as binary.\") 'expression': expr_string,"
    "@optional @expand_if(\"!$.auto_columns\") @default(16) @description(\"The number of byte columns to build before building a new row.\") 'num_columns': @range[1, 64] u64,"
    "@no_callee_helper @default(0) @display_name(\"Automatically Size Columns\") @description(\"Determines the number of byte columns based on the available space.\") 'auto_columns': bool,"
    "}"
  )},
  {str8_lit_comp("recent_project"), 0, str8_lit_comp("x:{'path':path, 'name':string}")},
};

typedef enum UIShell_AppRegSlot
{
  UIShell_AppRegSlot_Null,
  UIShell_AppRegSlot_Window,
  UIShell_AppRegSlot_Panel,
  UIShell_AppRegSlot_Tab,
  UIShell_AppRegSlot_View,
  UIShell_AppRegSlot_PrevTab,
  UIShell_AppRegSlot_DstPanel,
  UIShell_AppRegSlot_Cfg,
  UIShell_AppRegSlot_CfgList,
  UIShell_AppRegSlot_FilePath,
  UIShell_AppRegSlot_Cursor,
  UIShell_AppRegSlot_Mark,
  UIShell_AppRegSlot_TextKey,
  UIShell_AppRegSlot_LangKind,
  UIShell_AppRegSlot_Vaddr,
  UIShell_AppRegSlot_Expr,
  UIShell_AppRegSlot_UIKey,
  UIShell_AppRegSlot_OffPx,
  UIShell_AppRegSlot_RegSlot,
  UIShell_AppRegSlot_ForceConfirm,
  UIShell_AppRegSlot_ForceFocus,
  UIShell_AppRegSlot_DoImplicitRoot,
  UIShell_AppRegSlot_DoLister,
  UIShell_AppRegSlot_DoBigRows,
  UIShell_AppRegSlot_NonGraphical,
  UIShell_AppRegSlot_PreferNewTab,
  UIShell_AppRegSlot_ActivateWithSingleClick,
  UIShell_AppRegSlot_Dir2,
  UIShell_AppRegSlot_String,
  UIShell_AppRegSlot_CmdName,
  UIShell_AppRegSlot_WMEvent,
  UIShell_AppRegSlot_COUNT
}
UIShell_AppRegSlot;

typedef struct UIShell_Regs UIShell_Regs;
struct UIShell_Regs
{
  CFG_ID window;
  CFG_ID panel;
  CFG_ID tab;
  CFG_ID view;
  CFG_ID prev_tab;
  CFG_ID dst_panel;
  CFG_ID cfg;
  CFG_IDList cfg_list;
  String8 file_path;
  TxtPt cursor;
  TxtPt mark;
  C_Key text_key;
  TXT_LangKind lang_kind;
  U64 vaddr;
  String8 expr;
  UI_Key ui_key;
  Vec2F32 off_px;
  RD_RegSlot reg_slot;
  B32 force_confirm;
  B32 force_focus;
  B32 do_implicit_root;
  B32 do_lister;
  B32 do_big_rows;
  B32 non_graphical;
  B32 prefer_new_tab;
  B32 activate_with_single_click;
  Dir2 dir2;
  String8 string;
  String8 cmd_name;
  WM_Event *wm_event;
};

#define UISHELL_REGS_LIT_INIT_TOP \
.window = rd_regs()->window,\
.panel = rd_regs()->panel,\
.tab = rd_regs()->tab,\
.view = rd_regs()->view,\
.prev_tab = rd_regs()->prev_tab,\
.dst_panel = rd_regs()->dst_panel,\
.cfg = rd_regs()->cfg,\
.cfg_list = rd_regs()->cfg_list,\
.file_path = rd_regs()->file_path,\
.cursor = rd_regs()->cursor,\
.mark = rd_regs()->mark,\
.text_key = rd_regs()->text_key,\
.lang_kind = rd_regs()->lang_kind,\
.vaddr = rd_regs()->vaddr,\
.expr = rd_regs()->expr,\
.ui_key = rd_regs()->ui_key,\
.off_px = rd_regs()->off_px,\
.reg_slot = rd_regs()->reg_slot,\
.force_confirm = rd_regs()->force_confirm,\
.force_focus = rd_regs()->force_focus,\
.do_implicit_root = rd_regs()->do_implicit_root,\
.do_lister = rd_regs()->do_lister,\
.do_big_rows = rd_regs()->do_big_rows,\
.non_graphical = rd_regs()->non_graphical,\
.prefer_new_tab = rd_regs()->prefer_new_tab,\
.activate_with_single_click = rd_regs()->activate_with_single_click,\
.dir2 = rd_regs()->dir2,\
.string = rd_regs()->string,\
.cmd_name = rd_regs()->cmd_name,\
.wm_event = rd_regs()->wm_event,

internal UIShell_Regs
uishell_regs_from_rd_regs(Arena *arena, RD_Regs *src)
{
  UIShell_Regs dst = {0};
  dst.window = src->window;
  dst.panel = src->panel;
  dst.tab = src->tab;
  dst.view = src->view;
  dst.prev_tab = src->prev_tab;
  dst.dst_panel = src->dst_panel;
  dst.cfg = src->cfg;
  dst.cfg_list = cfg_id_list_copy(arena, &src->cfg_list);
  dst.file_path = push_str8_copy(arena, src->file_path);
  dst.cursor = src->cursor;
  dst.mark = src->mark;
  dst.text_key = src->text_key;
  dst.lang_kind = src->lang_kind;
  dst.vaddr = src->vaddr;
  dst.expr = push_str8_copy(arena, src->expr);
  dst.ui_key = src->ui_key;
  dst.off_px = src->off_px;
  dst.reg_slot = src->reg_slot;
  dst.force_confirm = src->force_confirm;
  dst.force_focus = src->force_focus;
  dst.do_implicit_root = src->do_implicit_root;
  dst.do_lister = src->do_lister;
  dst.do_big_rows = src->do_big_rows;
  dst.non_graphical = src->non_graphical;
  dst.prefer_new_tab = src->prefer_new_tab;
  dst.activate_with_single_click = src->activate_with_single_click;
  dst.dir2 = src->dir2;
  dst.string = push_str8_copy(arena, src->string);
  dst.cmd_name = push_str8_copy(arena, src->cmd_name);
  dst.wm_event = src->wm_event;
  if(dst.cfg_list.count == 0 && dst.cfg != 0)
  {
    cfg_id_list_push(arena, &dst.cfg_list, dst.cfg);
  }
  return dst;
}

internal void
uishell_regs_into_rd_regs(RD_Regs *dst, UIShell_Regs *src)
{
  MemoryZeroStruct(dst);
  dst->window = src->window;
  dst->panel = src->panel;
  dst->tab = src->tab;
  dst->view = src->view;
  dst->prev_tab = src->prev_tab;
  dst->dst_panel = src->dst_panel;
  dst->cfg = src->cfg;
  dst->cfg_list = src->cfg_list;
  dst->file_path = src->file_path;
  dst->cursor = src->cursor;
  dst->mark = src->mark;
  dst->text_key = src->text_key;
  dst->lang_kind = src->lang_kind;
  dst->vaddr = src->vaddr;
  dst->expr = src->expr;
  dst->ui_key = src->ui_key;
  dst->off_px = src->off_px;
  dst->reg_slot = src->reg_slot;
  dst->force_confirm = src->force_confirm;
  dst->force_focus = src->force_focus;
  dst->do_implicit_root = src->do_implicit_root;
  dst->do_lister = src->do_lister;
  dst->do_big_rows = src->do_big_rows;
  dst->non_graphical = src->non_graphical;
  dst->prefer_new_tab = src->prefer_new_tab;
  dst->activate_with_single_click = src->activate_with_single_click;
  dst->dir2 = src->dir2;
  dst->string = src->string;
  dst->cmd_name = src->cmd_name;
  dst->wm_event = src->wm_event;
}

internal UIShell_Regs
uishell_regs_copy(Arena *arena, UIShell_Regs *src)
{
  RD_Regs tmp = {0};
  uishell_regs_into_rd_regs(&tmp, src);
  UIShell_Regs dst = uishell_regs_from_rd_regs(arena, &tmp);
  return dst;
}

typedef struct UIShell_AppRegSlotInfo UIShell_AppRegSlotInfo;
struct UIShell_AppRegSlotInfo
{
  String8 code_name;
};

read_only global UIShell_AppRegSlotInfo uishell_app_reg_slot_info_table[UIShell_AppRegSlot_COUNT] =
{
  [UIShell_AppRegSlot_Window]                  = {str8_lit_comp("window")},
  [UIShell_AppRegSlot_Panel]                   = {str8_lit_comp("panel")},
  [UIShell_AppRegSlot_Tab]                     = {str8_lit_comp("tab")},
  [UIShell_AppRegSlot_View]                    = {str8_lit_comp("view")},
  [UIShell_AppRegSlot_PrevTab]                 = {str8_lit_comp("prev_tab")},
  [UIShell_AppRegSlot_DstPanel]                = {str8_lit_comp("dst_panel")},
  [UIShell_AppRegSlot_Cfg]                     = {str8_lit_comp("cfg")},
  [UIShell_AppRegSlot_CfgList]                 = {str8_lit_comp("cfg_list")},
  [UIShell_AppRegSlot_FilePath]                = {str8_lit_comp("file_path")},
  [UIShell_AppRegSlot_Cursor]                  = {str8_lit_comp("cursor")},
  [UIShell_AppRegSlot_Mark]                    = {str8_lit_comp("mark")},
  [UIShell_AppRegSlot_TextKey]                 = {str8_lit_comp("text_key")},
  [UIShell_AppRegSlot_LangKind]                = {str8_lit_comp("lang_kind")},
  [UIShell_AppRegSlot_Vaddr]                   = {str8_lit_comp("vaddr")},
  [UIShell_AppRegSlot_Expr]                    = {str8_lit_comp("expr")},
  [UIShell_AppRegSlot_UIKey]                   = {str8_lit_comp("ui_key")},
  [UIShell_AppRegSlot_OffPx]                   = {str8_lit_comp("off_px")},
  [UIShell_AppRegSlot_RegSlot]                 = {str8_lit_comp("reg_slot")},
  [UIShell_AppRegSlot_ForceConfirm]            = {str8_lit_comp("force_confirm")},
  [UIShell_AppRegSlot_ForceFocus]              = {str8_lit_comp("force_focus")},
  [UIShell_AppRegSlot_DoImplicitRoot]          = {str8_lit_comp("do_implicit_root")},
  [UIShell_AppRegSlot_DoLister]                = {str8_lit_comp("do_lister")},
  [UIShell_AppRegSlot_DoBigRows]               = {str8_lit_comp("do_big_rows")},
  [UIShell_AppRegSlot_NonGraphical]            = {str8_lit_comp("non_graphical")},
  [UIShell_AppRegSlot_PreferNewTab]            = {str8_lit_comp("prefer_new_tab")},
  [UIShell_AppRegSlot_ActivateWithSingleClick] = {str8_lit_comp("activate_with_single_click")},
  [UIShell_AppRegSlot_Dir2]                    = {str8_lit_comp("dir2")},
  [UIShell_AppRegSlot_String]                  = {str8_lit_comp("string")},
  [UIShell_AppRegSlot_CmdName]                 = {str8_lit_comp("cmd_name")},
  [UIShell_AppRegSlot_WMEvent]                 = {str8_lit_comp("wm_event")},
};

internal UIShell_AppRegSlot
uishell_app_reg_slot_from_rd_reg_slot(RD_RegSlot slot)
{
  UIShell_AppRegSlot result = UIShell_AppRegSlot_Null;
  switch(slot)
  {
    default: break;
    case RD_RegSlot_Window:                  {result = UIShell_AppRegSlot_Window;}break;
    case RD_RegSlot_Panel:                   {result = UIShell_AppRegSlot_Panel;}break;
    case RD_RegSlot_Tab:                     {result = UIShell_AppRegSlot_Tab;}break;
    case RD_RegSlot_View:                    {result = UIShell_AppRegSlot_View;}break;
    case RD_RegSlot_PrevTab:                 {result = UIShell_AppRegSlot_PrevTab;}break;
    case RD_RegSlot_DstPanel:                {result = UIShell_AppRegSlot_DstPanel;}break;
    case RD_RegSlot_Cfg:                     {result = UIShell_AppRegSlot_Cfg;}break;
    case RD_RegSlot_CfgList:                 {result = UIShell_AppRegSlot_CfgList;}break;
    case RD_RegSlot_FilePath:                {result = UIShell_AppRegSlot_FilePath;}break;
    case RD_RegSlot_Cursor:                  {result = UIShell_AppRegSlot_Cursor;}break;
    case RD_RegSlot_Mark:                    {result = UIShell_AppRegSlot_Mark;}break;
    case RD_RegSlot_TextKey:                 {result = UIShell_AppRegSlot_TextKey;}break;
    case RD_RegSlot_LangKind:                {result = UIShell_AppRegSlot_LangKind;}break;
    case RD_RegSlot_Vaddr:                   {result = UIShell_AppRegSlot_Vaddr;}break;
    case RD_RegSlot_Expr:                    {result = UIShell_AppRegSlot_Expr;}break;
    case RD_RegSlot_UIKey:                   {result = UIShell_AppRegSlot_UIKey;}break;
    case RD_RegSlot_OffPx:                   {result = UIShell_AppRegSlot_OffPx;}break;
    case RD_RegSlot_RegSlot:                 {result = UIShell_AppRegSlot_RegSlot;}break;
    case RD_RegSlot_ForceConfirm:            {result = UIShell_AppRegSlot_ForceConfirm;}break;
    case RD_RegSlot_ForceFocus:              {result = UIShell_AppRegSlot_ForceFocus;}break;
    case RD_RegSlot_DoImplicitRoot:          {result = UIShell_AppRegSlot_DoImplicitRoot;}break;
    case RD_RegSlot_DoLister:                {result = UIShell_AppRegSlot_DoLister;}break;
    case RD_RegSlot_DoBigRows:               {result = UIShell_AppRegSlot_DoBigRows;}break;
    case RD_RegSlot_NonGraphical:            {result = UIShell_AppRegSlot_NonGraphical;}break;
    case RD_RegSlot_PreferNewTab:            {result = UIShell_AppRegSlot_PreferNewTab;}break;
    case RD_RegSlot_ActivateWithSingleClick: {result = UIShell_AppRegSlot_ActivateWithSingleClick;}break;
    case RD_RegSlot_Dir2:                    {result = UIShell_AppRegSlot_Dir2;}break;
    case RD_RegSlot_String:                  {result = UIShell_AppRegSlot_String;}break;
    case RD_RegSlot_CmdName:                 {result = UIShell_AppRegSlot_CmdName;}break;
    case RD_RegSlot_WMEvent:                 {result = UIShell_AppRegSlot_WMEvent;}break;
  }
  return result;
}

internal String8
uishell_reg_slot_code_name_from_rd_reg_slot(RD_RegSlot slot)
{
  UIShell_AppRegSlot app_slot = uishell_app_reg_slot_from_rd_reg_slot(slot);
  String8 result = {0};
  if(app_slot < UIShell_AppRegSlot_COUNT)
  {
    result = uishell_app_reg_slot_info_table[app_slot].code_name;
  }
  return result;
}

internal B32
uishell_cfg_schema_name_is_listed(String8 name)
{
  B32 result = 0;
  for EachElement(idx, uishell_name_schema_info_table)
  {
    if(str8_match(name, uishell_name_schema_info_table[idx].name, 0))
    {
      result = 1;
      break;
    }
  }
  return result;
}

internal String8
uishell_file_view_name_from_probe(U64 num_utf8_bytes, U64 num_unknown_bytes)
{
  String8 result = {0};
  if(num_utf8_bytes > num_unknown_bytes*4 || num_unknown_bytes == 0)
  {
    result = str8_lit("text");
  }
  else
  {
    result = str8_lit("binary");
  }
  return result;
}

internal String8
uishell_fallback_file_view_name(void)
{
  String8 result = str8_lit("binary");
  return result;
}

internal void
uishell_push_palette_query_roots(Arena *arena, String8List *exprs)
{
  str8_list_pushf(arena, exprs, "query:recent_projects");
  str8_list_pushf(arena, exprs, "query:user_settings");
  str8_list_pushf(arena, exprs, "query:project_settings");
}

internal String8
uishell_initial_open_file_path_from_args(Arena *arena, Arena *scratch_arena, String8List *target_args)
{
  String8 result = {0};
  if(target_args->node_count > 0 && target_args->first->string.size != 0)
  {
    String8 file_path = target_args->first->string;
    PathStyle style = path_style_from_str8(file_path);
    if(style == PathStyle_Relative)
    {
      String8 current_path = get_current_path(scratch_arena);
      file_path = push_str8f(scratch_arena, "%S/%S", current_path, file_path);
      file_path = path_normalized_from_string(scratch_arena, file_path);
    }
    result = push_str8_copy(arena, file_path);
  }
  return result;
}

internal void
uishell_build_help_menu(void)
{
  UI_TagF("weak")
    UI_Row UI_TextAlignment(UI_TextAlign_Center) UI_Padding(ui_pct(1, 0))
    UI_PrefWidth(ui_text_dim(ui_top_font_size()*2.f, 1))
  {
    ui_label(str8_lit("Native UI shell experiment"));
  }
}

////////////////////////////////
//~ rjf: Shell App Menu Metadata

internal RD_AppMenuSpecList
uishell_app_menu_specs(void)
{
#define UIShell_MenuCmd(name, cp) {0, str8_lit_comp(name), cp}
#define UIShell_MenuSep()        {1, {0}, 0}
  local_persist RD_AppMenuItemSpec file_items[] =
  {
    UIShell_MenuCmd("open", 'o'),
    UIShell_MenuSep(),
    UIShell_MenuCmd("new_project", 'j'),
    UIShell_MenuCmd("open_project", 'p'),
    UIShell_MenuCmd("open_recent_project", 'r'),
    UIShell_MenuCmd("save_project", 'a'),
    UIShell_MenuCmd("project_settings", 't'),
    UIShell_MenuSep(),
    UIShell_MenuCmd("new_user", 'w'),
    UIShell_MenuCmd("open_user", 'u'),
    UIShell_MenuCmd("save_user", 's'),
    UIShell_MenuCmd("user_settings", 'e'),
    UIShell_MenuSep(),
    UIShell_MenuCmd("exit", 'x'),
  };
  local_persist RD_AppMenuItemSpec window_items[] =
  {
    UIShell_MenuCmd("open_window", 'w'),
    UIShell_MenuCmd("close_window", 'c'),
    UIShell_MenuCmd("toggle_fullscreen", 'f'),
    UIShell_MenuSep(),
    UIShell_MenuCmd("window_settings", 's'),
  };
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
    {str8_lit_comp("File"),   'f', WM_Key_F, ArrayCount(file_items),   file_items},
    {str8_lit_comp("Window"), 'w', WM_Key_W, ArrayCount(window_items), window_items},
    {str8_lit_comp("Panel"),  'p', WM_Key_P, ArrayCount(panel_items),  panel_items},
    {str8_lit_comp("Tab"),    'b', WM_Key_V, ArrayCount(tab_items),    tab_items},
    {str8_lit_comp("Help"),   'h', WM_Key_H, 0,                        0},
  };
  RD_AppMenuSpecList result = {ArrayCount(specs), specs};
#undef UIShell_MenuSep
#undef UIShell_MenuCmd
  return result;
}

////////////////////////////////
//~ rjf: Shell Default Panels

internal void
uishell_reset_panels(CFG_Node *window)
{
  CFG_Node *old_panels = cfg_node_child_from_string(window, str8_lit("panels"));
  cfg_node_release(rd_state->cfg, old_panels);
  cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("split_x"));
  
  CFG_Node *panels = cfg_node_new(rd_state->cfg, window, str8_lit("panels"));
  CFG_Node *main_panel = cfg_node_new(rd_state->cfg, panels, str8_lit("0.72"));
  CFG_Node *side_panel = cfg_node_new(rd_state->cfg, panels, str8_lit("0.28"));
  
  rd_cfg_new_view_tab(main_panel, str8_lit("text"), str8_zero(), 1);
  rd_cfg_new_view_tab(side_panel, str8_lit("text"), str8_lit("query:output"), 1);
  cfg_node_new(rd_state->cfg, main_panel, str8_lit("selected"));
  
  RD_WindowState *ws = rd_window_state_from_cfg(window);
  if(ws != &rd_nil_window_state)
  {
    ws->window_layout_reset = 1;
  }
}

#endif // UISHELL_META_H
