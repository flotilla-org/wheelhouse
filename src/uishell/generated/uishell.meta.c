// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- GENERATED CODE

global B32 DEV_always_refresh = 0;
global B32 DEV_simulate_lag = 0;
global B32 DEV_draw_ui_text_pos = 0;
global B32 DEV_draw_ui_focus_debug = 0;
global B32 DEV_draw_ui_box_heatmap = 0;
global B32 DEV_draw_panel_surface = 0;
global B32 DEV_draw_view_surfaces = 0;
global B32 DEV_draw_surface_previews = 0;
global B32 DEV_crt_views = 0;
global B32 DEV_eval_compiler_tooltips = 0;
global B32 DEV_eval_watch_key_tooltips = 0;
global B32 DEV_cmd_context_tooltips = 0;
global B32 DEV_updating_indicator = 0;
struct {B32 *value_ptr; String8 name;} DEV_toggle_table[] =
{
{&DEV_always_refresh, str8_lit_comp("always_refresh")},
{&DEV_simulate_lag, str8_lit_comp("simulate_lag")},
{&DEV_draw_ui_text_pos, str8_lit_comp("draw_ui_text_pos")},
{&DEV_draw_ui_focus_debug, str8_lit_comp("draw_ui_focus_debug")},
{&DEV_draw_ui_box_heatmap, str8_lit_comp("draw_ui_box_heatmap")},
{&DEV_draw_panel_surface, str8_lit_comp("draw_panel_surface")},
{&DEV_draw_view_surfaces, str8_lit_comp("draw_view_surfaces")},
{&DEV_draw_surface_previews, str8_lit_comp("draw_surface_previews")},
{&DEV_crt_views, str8_lit_comp("crt_views")},
{&DEV_eval_compiler_tooltips, str8_lit_comp("eval_compiler_tooltips")},
{&DEV_eval_watch_key_tooltips, str8_lit_comp("eval_watch_key_tooltips")},
{&DEV_cmd_context_tooltips, str8_lit_comp("cmd_context_tooltips")},
{&DEV_updating_indicator, str8_lit_comp("updating_indicator")},
};
C_LINKAGE_BEGIN
RD_VocabInfo uishell_vocab_info_table[42] =
{
{str8_lit_comp("view"), str8_lit_comp("views"), str8_lit_comp("View"), str8_lit_comp("Views"), RD_IconKind_Binoculars},
{str8_lit_comp("window"), str8_lit_comp("windows"), str8_lit_comp("Window"), str8_lit_comp("Windows"), RD_IconKind_Window},
{str8_lit_comp("panel"), str8_lit_comp("panels"), str8_lit_comp("Panel"), str8_lit_comp("Panels"), RD_IconKind_Null},
{str8_lit_comp("tab"), str8_lit_comp("tabs"), str8_lit_comp("Tab"), str8_lit_comp("Tabs"), RD_IconKind_Null},
{str8_lit_comp("user"), str8_lit_comp("users"), str8_lit_comp("User"), str8_lit_comp("Users"), RD_IconKind_Person},
{str8_lit_comp("project"), str8_lit_comp("projects"), str8_lit_comp("Project"), str8_lit_comp("Projects"), RD_IconKind_Briefcase},
{str8_lit_comp("recent_project"), str8_lit_comp("recent_projects"), str8_lit_comp("Recent Project"), str8_lit_comp("Recent Projects"), RD_IconKind_Briefcase},
{str8_lit_comp("theme_color"), str8_lit_comp("theme_colors"), str8_lit_comp("Theme Color"), str8_lit_comp("Theme Colors"), RD_IconKind_Palette},
{str8_lit_comp("theme"), str8_lit_comp("themes"), str8_lit_comp("Theme"), str8_lit_comp("Themes"), RD_IconKind_Palette},
{str8_lit_comp("color"), str8_lit_comp("colors"), str8_lit_comp("Color"), str8_lit_comp("Colors"), RD_IconKind_Palette},
{str8_lit_comp("file"), str8_lit_comp("files"), str8_lit_comp("File"), str8_lit_comp("Files"), RD_IconKind_FileOutline},
{str8_lit_comp("folder"), str8_lit_comp("folders"), str8_lit_comp("Folder"), str8_lit_comp("Folders"), RD_IconKind_FolderClosedFilled},
{str8_lit_comp("path"), str8_lit_comp(""), str8_lit_comp("Path"), str8_lit_comp(""), RD_IconKind_FileOutline},
{str8_lit_comp("current_path"), str8_lit_comp(""), str8_lit_comp("Current Path"), str8_lit_comp(""), RD_IconKind_FileOutline},
{str8_lit_comp("name"), str8_lit_comp("names"), str8_lit_comp("Name"), str8_lit_comp("Names"), RD_IconKind_Null},
{str8_lit_comp("label"), str8_lit_comp("labels"), str8_lit_comp("Label"), str8_lit_comp("Labels"), RD_IconKind_Null},
{str8_lit_comp("string"), str8_lit_comp("strings"), str8_lit_comp("String"), str8_lit_comp("Strings"), RD_IconKind_Null},
{str8_lit_comp("bool"), str8_lit_comp("bools"), str8_lit_comp("Boolean"), str8_lit_comp("Booleans"), RD_IconKind_Null},
{str8_lit_comp("expression"), str8_lit_comp("expressions"), str8_lit_comp("Expression"), str8_lit_comp("Expressions"), RD_IconKind_Null},
{str8_lit_comp("expr"), str8_lit_comp("exprs"), str8_lit_comp("Expression"), str8_lit_comp("Expressions"), RD_IconKind_Null},
{str8_lit_comp("lang"), str8_lit_comp("langs"), str8_lit_comp("Language"), str8_lit_comp("Languages"), RD_IconKind_Null},
{str8_lit_comp("font_size"), str8_lit_comp(""), str8_lit_comp("Font Size"), str8_lit_comp(""), RD_IconKind_Null},
{str8_lit_comp("row_height"), str8_lit_comp(""), str8_lit_comp("Row Height"), str8_lit_comp(""), RD_IconKind_Null},
{str8_lit_comp("tab_height"), str8_lit_comp(""), str8_lit_comp("Tab Height"), str8_lit_comp(""), RD_IconKind_Null},
{str8_lit_comp("show_line_numbers"), str8_lit_comp(""), str8_lit_comp("Show Line Numbers"), str8_lit_comp(""), RD_IconKind_Null},
{str8_lit_comp("line_wrapping"), str8_lit_comp(""), str8_lit_comp("Line Wrapping"), str8_lit_comp(""), RD_IconKind_Null},
{str8_lit_comp("scroll_to_bottom_on_change"), str8_lit_comp(""), str8_lit_comp("Scroll To Bottom On Change"), str8_lit_comp(""), RD_IconKind_Null},
{str8_lit_comp("auto"), str8_lit_comp(""), str8_lit_comp("Transient"), str8_lit_comp(""), RD_IconKind_Null},
{str8_lit_comp("num_columns"), str8_lit_comp(""), str8_lit_comp("Number of Columns"), str8_lit_comp(""), RD_IconKind_Null},
{str8_lit_comp("auto_columns"), str8_lit_comp(""), str8_lit_comp("Automatically Size Columns"), str8_lit_comp(""), RD_IconKind_Null},
{str8_lit_comp("address"), str8_lit_comp("addresses"), str8_lit_comp("Address"), str8_lit_comp("Addresses"), RD_IconKind_Null},
{str8_lit_comp("offset"), str8_lit_comp("offsets"), str8_lit_comp("Offset"), str8_lit_comp("Offsets"), RD_IconKind_Null},
{str8_lit_comp("text"), str8_lit_comp(""), str8_lit_comp("Text"), str8_lit_comp(""), RD_IconKind_FileOutline},
{str8_lit_comp("terminal"), str8_lit_comp(""), str8_lit_comp("Terminal"), str8_lit_comp(""), RD_IconKind_Machine},
{str8_lit_comp("terminal_fixture"), str8_lit_comp(""), str8_lit_comp("Terminal Fixture"), str8_lit_comp(""), RD_IconKind_Machine},
{str8_lit_comp("binary"), str8_lit_comp(""), str8_lit_comp("Binary"), str8_lit_comp(""), RD_IconKind_Grid},
{str8_lit_comp("output"), str8_lit_comp("outputs"), str8_lit_comp("Output"), str8_lit_comp("Outputs"), RD_IconKind_List},
{str8_lit_comp("command"), str8_lit_comp("commands"), str8_lit_comp("Command"), str8_lit_comp("Commands"), RD_IconKind_Palette},
{str8_lit_comp("tab_command"), str8_lit_comp("tab_commands"), str8_lit_comp("Tab Command"), str8_lit_comp("Tab Commands"), RD_IconKind_Palette},
{str8_lit_comp("user_settings"), str8_lit_comp(""), str8_lit_comp("User Settings"), str8_lit_comp(""), RD_IconKind_Gear},
{str8_lit_comp("project_settings"), str8_lit_comp(""), str8_lit_comp("Project Settings"), str8_lit_comp(""), RD_IconKind_Gear},
{str8_lit_comp("keybinding"), str8_lit_comp("keybindings"), str8_lit_comp("Keybinding"), str8_lit_comp("Keybindings"), RD_IconKind_Null},
};

String8 uishell_binding_version_remap_old_name_table[5] =
{
str8_lit_comp("commands"),
str8_lit_comp("load_user"),
str8_lit_comp("load_profile"),
str8_lit_comp("load_project"),
str8_lit_comp("open_profile"),
};

String8 uishell_binding_version_remap_new_name_table[5] =
{
str8_lit_comp("run_command"),
str8_lit_comp("open_user"),
str8_lit_comp("open_profile"),
str8_lit_comp("open_project"),
str8_lit_comp("open_project"),
};

C_LINKAGE_END
