// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- GENERATED CODE

#ifndef SHELL_META_H
#define SHELL_META_H

typedef enum RD_IconKind
{
RD_IconKind_Null,
RD_IconKind_FolderOpenOutline,
RD_IconKind_FolderClosedOutline,
RD_IconKind_FolderOpenFilled,
RD_IconKind_FolderClosedFilled,
RD_IconKind_FileOutline,
RD_IconKind_FileFilled,
RD_IconKind_Play,
RD_IconKind_PlayStepForward,
RD_IconKind_Pause,
RD_IconKind_Stop,
RD_IconKind_Info,
RD_IconKind_WarningSmall,
RD_IconKind_WarningBig,
RD_IconKind_Unlocked,
RD_IconKind_Locked,
RD_IconKind_LeftArrow,
RD_IconKind_RightArrow,
RD_IconKind_UpArrow,
RD_IconKind_DownArrow,
RD_IconKind_Gear,
RD_IconKind_Pencil,
RD_IconKind_Trash,
RD_IconKind_Pin,
RD_IconKind_RadioHollow,
RD_IconKind_RadioFilled,
RD_IconKind_CheckHollow,
RD_IconKind_CheckFilled,
RD_IconKind_Check,
RD_IconKind_LeftCaret,
RD_IconKind_RightCaret,
RD_IconKind_UpCaret,
RD_IconKind_DownCaret,
RD_IconKind_UpScroll,
RD_IconKind_DownScroll,
RD_IconKind_LeftScroll,
RD_IconKind_RightScroll,
RD_IconKind_Add,
RD_IconKind_Minus,
RD_IconKind_Thread,
RD_IconKind_Threads,
RD_IconKind_Machine,
RD_IconKind_CircleFilled,
RD_IconKind_X,
RD_IconKind_Refresh,
RD_IconKind_Undo,
RD_IconKind_Redo,
RD_IconKind_Save,
RD_IconKind_Window,
RD_IconKind_Target,
RD_IconKind_Clipboard,
RD_IconKind_Scheduler,
RD_IconKind_Module,
RD_IconKind_XSplit,
RD_IconKind_YSplit,
RD_IconKind_ClosePanel,
RD_IconKind_StepInto,
RD_IconKind_StepOver,
RD_IconKind_StepOut,
RD_IconKind_Find,
RD_IconKind_Palette,
RD_IconKind_Thumbnails,
RD_IconKind_Glasses,
RD_IconKind_Binoculars,
RD_IconKind_List,
RD_IconKind_Grid,
RD_IconKind_QuestionMark,
RD_IconKind_Person,
RD_IconKind_Briefcase,
RD_IconKind_Dot,
RD_IconKind_Bitmap,
RD_IconKind_Cube,
RD_IconKind_WindowRestore,
RD_IconKind_WindowMinimize,
RD_IconKind_Duplicate,
RD_IconKind_COUNT,
} RD_IconKind;

typedef enum RD_CodeColorSlot
{
RD_CodeColorSlot_CodeDefault,
RD_CodeColorSlot_CodeSymbol,
RD_CodeColorSlot_CodeType,
RD_CodeColorSlot_CodeLocal,
RD_CodeColorSlot_CodeRegister,
RD_CodeColorSlot_CodeKeyword,
RD_CodeColorSlot_CodeDelimiterOperator,
RD_CodeColorSlot_CodeNumeric,
RD_CodeColorSlot_CodeNumericAltDigitGroup,
RD_CodeColorSlot_CodeString,
RD_CodeColorSlot_CodeMeta,
RD_CodeColorSlot_CodeComment,
RD_CodeColorSlot_CodeLineNumbers,
RD_CodeColorSlot_CodeLineNumbersSelected,
RD_CodeColorSlot_COUNT,
} RD_CodeColorSlot;

typedef enum RD_ThemePreset
{
RD_ThemePreset_DefaultDark,
RD_ThemePreset_DefaultLight,
RD_ThemePreset_VSDark,
RD_ThemePreset_VSLight,
RD_ThemePreset_SolarizedDark,
RD_ThemePreset_SolarizedLight,
RD_ThemePreset_HandmadeHero,
RD_ThemePreset_Naysayer,
RD_ThemePreset_FourCoder,
RD_ThemePreset_Grove,
RD_ThemePreset_FarManager,
RD_ThemePreset_COUNT,
} RD_ThemePreset;

C_LINKAGE_BEGIN
extern String8 rd_icon_kind_text_table[75];
extern String8 rd_code_color_slot_name_table[14];
extern String8 rd_theme_preset_display_string_table[11];
extern String8 rd_theme_preset_code_string_table[11];
extern String8 rd_theme_preset_cfg_string_table[11];
extern String8 rd_icon_font_bytes;
extern String8 rd_default_main_font_bytes;
extern String8 rd_default_main_semibold_font_bytes;
extern String8 rd_default_code_font_bytes;
extern String8 rd_terminal_noto_emoji_font_bytes;
extern String8 rd_terminal_noto_color_emoji_font_bytes;
extern String8 rd_terminal_noto_symbols_font_bytes;
extern String8 rd_terminal_noto_symbols_2_font_bytes;
extern String8 rd_terminal_noto_math_font_bytes;
extern String8 rd_icon_file_bytes;

C_LINKAGE_END

#endif // SHELL_META_H
