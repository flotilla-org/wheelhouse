// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef RADDBG_CORE_H
#define RADDBG_CORE_H

////////////////////////////////
//~ rjf: Evaluation Spaces

typedef U64 RD_EvalSpaceKind;
enum
{
  RD_EvalSpaceKind_MetaQuery = E_SpaceKind_FirstUserDefined,
  RD_EvalSpaceKind_MetaCfg,
  RD_EvalSpaceKind_MetaCmd,
  RD_EvalSpaceKind_MetaTheme,
  RD_EvalSpaceKind_MetaView,
};

////////////////////////////////
//~ rjf: View UI Hook Types

#define RD_VIEW_UI_FUNCTION_SIG(name) void name(E_Eval eval, Rng2F32 rect)
#define RD_VIEW_UI_FUNCTION_NAME(name) rd_view_ui__##name
#define RD_VIEW_UI_FUNCTION_DEF(name) internal RD_VIEW_UI_FUNCTION_SIG(RD_VIEW_UI_FUNCTION_NAME(name))
typedef RD_VIEW_UI_FUNCTION_SIG(RD_ViewUIFunctionType);

typedef struct RD_ViewUIRule RD_ViewUIRule;
struct RD_ViewUIRule
{
  String8 name;
  RD_ViewUIFunctionType *ui;
};

typedef struct RD_ViewUIRuleNode RD_ViewUIRuleNode;
struct RD_ViewUIRuleNode
{
  RD_ViewUIRuleNode *next;
  RD_ViewUIRule v;
};

typedef struct RD_ViewUIRuleSlot RD_ViewUIRuleSlot;
struct RD_ViewUIRuleSlot
{
  RD_ViewUIRuleNode *first;
  RD_ViewUIRuleNode *last;
};

typedef struct RD_ViewUIRuleMap RD_ViewUIRuleMap;
struct RD_ViewUIRuleMap
{
  RD_ViewUIRuleSlot *slots;
  U64 slots_count;
};

////////////////////////////////
//~ rjf: Drag/Drop Types

typedef enum RD_DragDropState
{
  RD_DragDropState_Null,
  RD_DragDropState_Dragging,
  RD_DragDropState_Dropping,
  RD_DragDropState_COUNT
}
RD_DragDropState;

////////////////////////////////
//~ rjf: Command Kind Types

typedef U32 RD_QueryFlags;
enum
{
  RD_QueryFlag_AllowFiles       = (1<<0),
  RD_QueryFlag_AllowFolders     = (1<<1),
  RD_QueryFlag_CodeInput        = (1<<2),
  RD_QueryFlag_KeepOldInput     = (1<<3),
  RD_QueryFlag_SelectOldInput   = (1<<4),
  RD_QueryFlag_Floating         = (1<<5),
  RD_QueryFlag_Required         = (1<<6),
};

typedef U32 RD_CmdKindFlags;
enum
{
  RD_CmdKindFlag_ListInUI      = (1<<0),
  RD_CmdKindFlag_ListInTab     = (1<<1),
  RD_CmdKindFlag_ListInTextPt  = (1<<2),
  RD_CmdKindFlag_ListInTextRng = (1<<3),
};

typedef enum RD_AppRegSlot
{
  RD_AppRegSlot_Null,
  RD_AppRegSlot_Window,
  RD_AppRegSlot_Panel,
  RD_AppRegSlot_Tab,
  RD_AppRegSlot_View,
  RD_AppRegSlot_PrevTab,
  RD_AppRegSlot_DstPanel,
  RD_AppRegSlot_Cfg,
  RD_AppRegSlot_CfgList,
  RD_AppRegSlot_FilePath,
  RD_AppRegSlot_Cursor,
  RD_AppRegSlot_Mark,
  RD_AppRegSlot_TextKey,
  RD_AppRegSlot_LangKind,
  RD_AppRegSlot_Vaddr,
  RD_AppRegSlot_Expr,
  RD_AppRegSlot_UIKey,
  RD_AppRegSlot_OffPx,
  RD_AppRegSlot_RegSlot,
  RD_AppRegSlot_ForceConfirm,
  RD_AppRegSlot_ForceFocus,
  RD_AppRegSlot_DoImplicitRoot,
  RD_AppRegSlot_DoLister,
  RD_AppRegSlot_DoBigRows,
  RD_AppRegSlot_NonGraphical,
  RD_AppRegSlot_PreferNewTab,
  RD_AppRegSlot_ActivateWithSingleClick,
  RD_AppRegSlot_Dir2,
  RD_AppRegSlot_String,
  RD_AppRegSlot_CmdName,
  RD_AppRegSlot_WMEvent,
  RD_AppRegSlot_COUNT
}
RD_AppRegSlot;

////////////////////////////////
//~ rjf: Autocompletion Cursor Info Type

typedef struct RD_AutocompCursorInfo RD_AutocompCursorInfo;
struct RD_AutocompCursorInfo
{
  String8 list_expr;
  String8 filter;
  Rng1U64 replaced_range;
  String8 callee_expr;
  MD_Node *arg_schema;
};

////////////////////////////////
//~ rjf: Compatibility Types + Generated Code

typedef enum RD_RegSlot
{
  RD_RegSlot_Null,
  RD_RegSlot_Window,
  RD_RegSlot_Panel,
  RD_RegSlot_Tab,
  RD_RegSlot_View,
  RD_RegSlot_PrevTab,
  RD_RegSlot_DstPanel,
  RD_RegSlot_Cfg,
  RD_RegSlot_CfgList,
  RD_RegSlot_FilePath,
  RD_RegSlot_Cursor,
  RD_RegSlot_Mark,
  RD_RegSlot_TextKey,
  RD_RegSlot_LangKind,
  RD_RegSlot_Vaddr,
  RD_RegSlot_Expr,
  RD_RegSlot_UIKey,
  RD_RegSlot_OffPx,
  RD_RegSlot_RegSlot,
  RD_RegSlot_ForceConfirm,
  RD_RegSlot_ForceFocus,
  RD_RegSlot_DoImplicitRoot,
  RD_RegSlot_DoLister,
  RD_RegSlot_DoBigRows,
  RD_RegSlot_NonGraphical,
  RD_RegSlot_PreferNewTab,
  RD_RegSlot_ActivateWithSingleClick,
  RD_RegSlot_Dir2,
  RD_RegSlot_String,
  RD_RegSlot_CmdName,
  RD_RegSlot_WMEvent,
  RD_RegSlot_COUNT,
}
RD_RegSlot;

typedef struct RD_Regs RD_Regs;
struct RD_Regs
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

#include "generated/raddbg.meta.h"

typedef struct RD_VocabInfo RD_VocabInfo;
struct RD_VocabInfo
{
  String8 code_name;
  String8 code_name_plural;
  String8 display_name;
  String8 display_name_plural;
  RD_IconKind icon_kind;
};

# define RD_APP_REGS_LIT_INIT_TOP \
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

////////////////////////////////
//~ rjf: App Menu Specs

typedef struct RD_AppMenuItemSpec RD_AppMenuItemSpec;
struct RD_AppMenuItemSpec
{
  B32 separator;
  String8 command_name;
  U32 codepoint;
};

typedef struct RD_AppMenuSpec RD_AppMenuSpec;
struct RD_AppMenuSpec
{
  String8 label;
  U32 codepoint;
  WM_Key key;
  U64 item_count;
  RD_AppMenuItemSpec *items;
};

typedef struct RD_AppMenuSpecList RD_AppMenuSpecList;
struct RD_AppMenuSpecList
{
  U64 count;
  RD_AppMenuSpec *v;
};

////////////////////////////////
//~ rjf: View State Types

typedef struct RD_ArenaExt RD_ArenaExt;
struct RD_ArenaExt
{
  RD_ArenaExt *next;
  Arena *arena;
};

typedef struct RD_ViewState RD_ViewState;
struct RD_ViewState
{
  // rjf: hash links & key
  RD_ViewState *hash_next;
  RD_ViewState *hash_prev;
  CFG_ID cfg_id;
  
  // rjf: touch info
  U64 last_frame_index_touched;
  U64 last_frame_index_built;
  
  // rjf: loading indicator info
  F32 loading_t;
  F32 loading_t_target;
  U64 loading_progress_v;
  U64 loading_progress_v_target;
  
  // rjf: scroll position
  UI_ScrollPt2 scroll_pos;
  
  // rjf: eval visualization view state
  EV_View *ev_view;
  
  // rjf: view-lifetime allocation & user data extensions
  Arena *arena;
  U64 arena_reset_pos;
  RD_ArenaExt *first_arena_ext;
  RD_ArenaExt *last_arena_ext;
  void *user_data;
  
  // rjf: query state
  B32 query_is_open;
  TxtPt query_cursor;
  TxtPt query_mark;
  U8 query_buffer[KB(1)];
  U64 query_string_size;
  
  // rjf: contents are focused (disables query focus)
  B32 contents_are_focused;
};

typedef struct RD_ViewStateSlot RD_ViewStateSlot;
struct RD_ViewStateSlot
{
  RD_ViewState *first;
  RD_ViewState *last;
};

////////////////////////////////
//~ rjf: Vocabulary Map

typedef struct RD_VocabInfoMapNode RD_VocabInfoMapNode;
struct RD_VocabInfoMapNode
{
  RD_VocabInfoMapNode *single_next;
  RD_VocabInfoMapNode *plural_next;
  RD_VocabInfo v;
};

typedef struct RD_VocabInfoMapSlot RD_VocabInfoMapSlot;
struct RD_VocabInfoMapSlot
{
  RD_VocabInfoMapNode *first;
  RD_VocabInfoMapNode *last;
};

typedef struct RD_VocabInfoMap RD_VocabInfoMap;
struct RD_VocabInfoMap
{
  U64 single_slots_count;
  RD_VocabInfoMapSlot *single_slots;
  U64 plural_slots_count;
  RD_VocabInfoMapSlot *plural_slots;
};

////////////////////////////////
//~ rjf: Structured Locations, Parsed From Config Trees

typedef struct RD_Location RD_Location;
struct RD_Location
{
  String8 file_path;
  TxtPt pt;
  String8 expr;
};

////////////////////////////////
//~ rjf: Command Types

typedef struct UIShell_Regs UIShell_Regs;
typedef UIShell_Regs RD_CmdRegs;

typedef struct RD_Cmd RD_Cmd;
struct RD_Cmd
{
  String8 name;
  RD_CmdRegs *regs;
};

typedef struct RD_CmdNode RD_CmdNode;
struct RD_CmdNode
{
  RD_CmdNode *next;
  RD_CmdNode *prev;
  RD_Cmd cmd;
};

typedef struct RD_CmdList RD_CmdList;
struct RD_CmdList
{
  RD_CmdNode *first;
  RD_CmdNode *last;
  U64 count;
};

////////////////////////////////
//~ rjf: Context Register Types

typedef struct RD_RegsNode RD_RegsNode;
struct RD_RegsNode
{
  RD_RegsNode *next;
  RD_Regs v;
};

////////////////////////////////
//~ rjf: Structured Theme Types, Parsed From Config

typedef enum RD_FontSlot
{
  RD_FontSlot_Main,
  RD_FontSlot_Code,
  RD_FontSlot_Icons,
  RD_FontSlot_COUNT
}
RD_FontSlot;

////////////////////////////////
//~ rjf: Per-Window State

typedef struct RD_DropCompletionTask RD_DropCompletionTask;
struct RD_DropCompletionTask
{
  RD_DropCompletionTask *next;
  B32 exe;
  B32 dbg;
  B32 cfg;
  String8List paths;
};

typedef struct RD_WindowState RD_WindowState;
struct RD_WindowState
{
  // rjf: links & metadata
  RD_WindowState *order_next;
  RD_WindowState *order_prev;
  RD_WindowState *hash_next;
  RD_WindowState *hash_prev;
  CFG_ID cfg_id;
  U64 frames_alive;
  U64 last_frame_index_touched;
  
  // rjf: top-level info & handles
  Arena *arena;
  WM_Window os;
  R_Handle r;
  UI_State *ui;
  F32 last_layout_scale;
  F32 last_backing_scale;
  B32 window_temporarily_focused_ipc;
  B32 window_layout_reset;
  Rng2F32 last_window_rect;
  
  // rjf: theme (recomputed each frame)
  UI_Theme *theme;
  Vec4F32 theme_code_colors[RD_CodeColorSlot_COUNT];
  
  // rjf: font raster flags (recomputed each frame)
  FNT_RasterFlags font_slot_raster_flags[RD_FontSlot_COUNT];
  
  // rjf: dev interface state
  B32 dev_menu_is_open;
  
  // rjf: menu bar state
  B32 menu_bar_focused;
  B32 menu_bar_focused_on_press;
  B32 menu_bar_key_held;
  B32 menu_bar_focus_press_started;
  
  // rjf: drop-completion state
  Arena *drop_completion_arena;
  CFG_ID drop_completion_panel;
  RD_DropCompletionTask *top_drop_completion_task;
  
  // rjf: query state
  B32 query_is_active;
  Arena *query_arena;
  UIShell_Regs *query_regs;
  CFG_ID query_view_id;
  CFG_ID query_last_view_id;
  
  // rjf: hover eval state
  B32 hover_eval_focused;
  Arena *hover_eval_arena;
  Vec2F32 hover_eval_spawn_pos;
  String8 hover_eval_string;
  U64 hover_eval_firstt_us;
  U64 hover_eval_lastt_us;
  
  // rjf: autocompletion state
  U64 autocomp_last_frame_index;
  Arena *autocomp_arena;
  UIShell_Regs *autocomp_regs;
  RD_AutocompCursorInfo autocomp_cursor_info;
  
  // rjf: error state
  U8 error_buffer[512];
  U64 error_string_size;
  F32 error_t;
  
  // rjf: per-frame ui events state
  UI_EventList ui_events;
  
  // rjf: per-frame drawing state
  DR_Bucket *draw_bucket;
};

typedef struct RD_WindowStateSlot RD_WindowStateSlot;
struct RD_WindowStateSlot
{
  RD_WindowState *first;
  RD_WindowState *last;
};

////////////////////////////////
//~ rjf: Main Per-Process Graphical State

typedef struct RD_AmbiguousPathNode RD_AmbiguousPathNode;
struct RD_AmbiguousPathNode
{
  RD_AmbiguousPathNode *next;
  String8 name;
  String8List paths;
};

typedef struct RD_State RD_State;
struct RD_State
{
  // rjf: basics
  Arena *arena;
  B32 quit;
  B32 quit_after_success;
  S32 frame_depth;
  U64 frame_eval_memread_endt_us;
  
  // rjf: config bucket paths
  Arena *user_path_arena;
  String8 user_path;
  Arena *project_path_arena;
  String8 project_path;
  Arena *theme_path_arena;
  String8 theme_path;
  
  // rjf: unpacked settings (cached, because they need to be used
  // earlier than setting evaluation is legal in a frame)
  B32 alt_menu_bar_enabled;
  EV_StringFlags eval_viz_base_string_flags;
  
  // rjf: animation rates
  F32 catchall_animation_rate;
  F32 menu_animation_rate;
  F32 menu_animation_rate__slow;
  F32 entity_alive_animation_rate;
  F32 rich_hover_animation_rate;
  F32 scrolling_animation_rate;
  F32 tooltip_animation_rate;
  
  // rjf: serialized config debug string keys
  C_Key user_cfg_string_key;
  C_Key project_cfg_string_key;
  C_Key cmdln_cfg_string_key;
  C_Key transient_cfg_string_key;
  C_Key shell_output_key;
  
  // rjf: default theme table
  MD_Node *theme_preset_trees[RD_ThemePreset_COUNT];
  
  // rjf: vocab table
  RD_VocabInfoMap vocab_info_map;
  
  // rjf: log
  Log *log;
  String8 log_path;
  
  // rjf: frame history info
  U64 frame_index;
  Arena *frame_arenas[2];
  U64 frame_time_us_history[64];
  U64 num_frames_requested;
  F64 time_in_seconds;
  U64 time_in_us;
  
  // rjf: frame parameters
  F32 frame_dt;
  Access *frame_access;
  String8 last_window_title;
  
  // rjf: evaluation cache
  E_Cache *eval_cache;
  
  // rjf: ambiguous path table (constructed from-scratch each frame)
  U64 ambiguous_path_slots_count;
  RD_AmbiguousPathNode **ambiguous_path_slots;
  
  // rjf: key map (constructed from-scratch each frame)
  CFG_KeyMap *key_map;
  
  // rjf: slot -> font tag map (constructed from-scratch each frame)
  FNT_Tag font_slot_table[RD_FontSlot_COUNT];
  
  // rjf: meta name -> eval type key map (constructed from-scratch each frame)
  E_String2TypeKeyMap *meta_name2type_map;
  
  // rjf: name -> view ui map (constructed from-scratch each frame)
  RD_ViewUIRuleMap *view_ui_rule_map;
  
  // rjf: registers stack
  RD_RegsNode base_regs;
  RD_RegsNode *top_regs;
  
  // rjf: autosave state
  F32 seconds_until_autosave;
  
  // rjf: commands
  Arena *cmds_arenas[2];
  RD_CmdList cmds[2];
  U64 cmds_gen;
  Arena *cmd_output_arena;
  String8List cmd_outputs;
  
  // rjf: popup state
  UI_Key popup_key;
  B32 popup_active;
  F32 popup_t;
  Arena *popup_arena;
  RD_CmdList popup_cmds;
  String8 popup_title;
  String8 popup_desc;
  
  // rjf: text editing mode state
  B32 text_edit_mode;
  
  // rjf: contextual hover info
  UIShell_Regs *hover_regs;
  RD_RegSlot hover_regs_slot;
  UIShell_Regs *next_hover_regs;
  RD_RegSlot next_hover_regs_slot;
  
  // rjf: icon texture
  R_Handle icon_texture;
  
  // rjf: fixed ui keys
  UI_Key drop_completion_key;
  UI_Key ctx_menu_key;
  
  // rjf: drag/drop state
  Arena *drag_drop_arena;
  UIShell_Regs *drag_drop_regs;
  RD_RegSlot drag_drop_regs_slot;
  RD_DragDropState drag_drop_state;
  
  // rjf: cfg state
  CFG_State *cfg;
  CFG_SchemaTable *cfg_schema_table;
  
  // rjf: window state cache
  U64 window_state_slots_count;
  RD_WindowStateSlot *window_state_slots;
  RD_WindowState *free_window_state;
  CFG_ID last_focused_window;
  RD_WindowState *first_window_state;
  RD_WindowState *last_window_state;
  CFG_ID window_state_last_accessed_id;
  RD_WindowState *window_state_last_accessed;
  
  // rjf: view state cache
  U64 view_state_slots_count;
  RD_ViewStateSlot *view_state_slots;
  RD_ViewState *free_view_state;
  CFG_ID view_state_last_accessed_id;
  RD_ViewState *view_state_last_accessed;
  
  // rjf: bind change
  Arena *bind_change_arena;
  B32 bind_change_active;
  CFG_ID bind_change_binding_id;
  String8 bind_change_cmd_name;
  
  // rjf: pre-stop focused window
  WM_ExtWindow prestop_focused_window;
};

////////////////////////////////
//~ rjf: Globals

read_only global RD_VocabInfo rd_nil_vocab_info = {0};

typedef struct RD_AppCmdInfo RD_AppCmdInfo;
struct RD_AppCmdInfo
{
  String8 string;
  String8 description;
  String8 search_tags;
  String8 ctx_filter;
  RD_CmdKindFlags flags;
  RD_QueryFlags query_flags;
  RD_AppRegSlot query_slot;
  String8 query_expr;
  String8 query_view_name;
};

RD_VIEW_UI_FUNCTION_DEF(null);
read_only global RD_ViewUIRule rd_nil_view_ui_rule =
{
  {0},
  RD_VIEW_UI_FUNCTION_NAME(null),
};

read_only global RD_ViewState rd_nil_view_state =
{
  &rd_nil_view_state,
  &rd_nil_view_state,
};

read_only global RD_WindowState rd_nil_window_state =
{
  &rd_nil_window_state,
  &rd_nil_window_state,
  &rd_nil_window_state,
  &rd_nil_window_state,
};

global RD_State *rd_state = 0;
global CFG_ID rd_last_drag_drop_panel = 0;
global CFG_ID rd_last_drag_drop_prev_tab = 0;

////////////////////////////////
//~ rjf: Registers Type Functions

internal void rd_regs_copy_contents(Arena *arena, RD_Regs *dst, RD_Regs *src);

////////////////////////////////
//~ rjf: Commands Type Functions

internal void rd_cmd_list_push_new(Arena *arena, RD_CmdList *cmds, String8 name, RD_CmdRegs *regs);

////////////////////////////////
//~ rjf: View UI Rule Functions

internal RD_ViewUIRuleMap *rd_view_ui_rule_map_make(Arena *arena, U64 slots_count);
internal void rd_view_ui_rule_map_insert(Arena *arena, RD_ViewUIRuleMap *map, String8 string, RD_ViewUIFunctionType *ui);

internal RD_ViewUIRule *rd_view_ui_rule_from_string(String8 string);
internal B32 rd_view_name_is_listed_in_app(String8 name);

////////////////////////////////
//~ rjf: Global Cross-Window UI Interaction State Functions

internal B32 rd_drag_is_active(void);
internal void rd_drag_begin(RD_RegSlot slot);
internal B32 rd_drag_drop(void);
internal void rd_drag_kill(void);

internal void rd_set_hover_regs(RD_RegSlot slot);

////////////////////////////////
//~ rjf: Config Functions

internal B32 rd_cfg_is_project_filtered(CFG_Node *cfg);

internal Vec4F32 rd_hsva_from_cfg(CFG_Node *cfg);
internal Vec4F32 rd_color_from_cfg(CFG_Node *cfg);

internal B32 rd_disabled_from_cfg(CFG_Node *cfg);
internal RD_Location rd_location_from_cfg(CFG_Node *cfg);
internal String8 rd_name_from_cfg(CFG_Node *cfg);
internal String8 rd_label_from_cfg(CFG_Node *cfg);
internal String8 rd_expr_from_cfg(CFG_Node *cfg);
internal String8 rd_path_from_cfg(CFG_Node *cfg);

internal String8 rd_default_setting_from_names(String8 schema_name, String8 setting_name);

internal String8 rd_setting_from_name(String8 name);
internal B32 rd_setting_b32_from_name(String8 name);
internal U64 rd_setting_u64_from_name(String8 name);
internal F32 rd_setting_f32_from_name(String8 name);

internal CFG_Node *rd_immediate_cfg_from_key(String8 string);
internal CFG_Node *rd_immediate_cfg_from_keyf(char *fmt, ...);

////////////////////////////////
//~ rjf: Evaluation Spaces

//- rjf: cfg <-> eval space
internal CFG_Node *rd_cfg_from_eval_space(E_Space space);
internal E_Space rd_eval_space_from_cfg(CFG_Node *cfg);

//- rjf: command name <-> eval space
internal String8 rd_cmd_name_from_eval(E_Eval eval);

//- rjf: eval space reads/writes
internal U64 rd_eval_space_gen(E_Space space);
internal B32 rd_eval_space_read(E_Space space, void *out, E_SpaceRangeInfo *out_range_info, Rng1U64 range);
internal B32 rd_eval_space_write(E_Space space, void *in, Rng1U64 range);

//- rjf: asynchronous streamed reads -> hashes from spaces
internal C_Key rd_key_from_eval_space_range(E_Space space, Rng1U64 range, B32 zero_terminated);

//- rjf: space -> entire range
internal Rng1U64 rd_whole_range_from_eval_space(E_Space space);

////////////////////////////////
//~ rjf: Evaluation Visualization

//- rjf: writing values back to child processes
internal B32 rd_commit_eval_value_string(E_Eval dst_eval, String8 string);

//- rjf: eval <-> file path
internal String8 rd_file_path_from_eval(Arena *arena, E_Eval eval);
internal String8 rd_file_path_from_eval_string(Arena *arena, String8 string);
internal String8 rd_eval_string_from_file_path(Arena *arena, String8 string);

//- rjf: eval -> query
internal String8 rd_query_from_eval_string(Arena *arena, String8 string);

////////////////////////////////
//~ rjf: View Functions

internal CFG_Node *rd_view_from_eval(CFG_Node *parent, E_Eval eval);
internal RD_ViewState *rd_view_state_from_cfg(CFG_Node *cfg);
internal void rd_view_ui(Rng2F32 rect);

////////////////////////////////
//~ rjf: View Building API

//- rjf: view info extraction
internal Arena *rd_view_arena(void);
internal UI_ScrollPt2 rd_view_scroll_pos(void);
internal EV_View *rd_view_eval_view(void);
internal String8 rd_view_query_cmd(void);
internal String8 rd_view_query_input(void);
internal String8 rd_view_setting_from_name(String8 string);
internal E_Value rd_view_setting_value_from_name(String8 string);
internal B32 rd_view_setting_b32_from_name(String8 string);
internal U64 rd_view_setting_u64_from_name(String8 string);
internal F32 rd_view_setting_f32_from_name(String8 string);
internal U64 rd_view_setting_addr_from_name(String8 string);

//- rjf: evaluation & tag (a view's 'call') parameter extraction
internal Rng1U64 rd_space_range_from_eval(E_Eval eval);
internal TXT_LangKind rd_lang_kind_from_eval(E_Eval eval);
internal Arch rd_arch_from_eval(E_Eval eval);

//- rjf: pushing/attaching view resources
internal void *rd_view_state_by_size(U64 size);
#define rd_view_state(T) (T *)rd_view_state_by_size(sizeof(T))
internal Arena *rd_push_view_arena(void);

//- rjf: storing view-attached state
internal void rd_store_view_expr_string(String8 string);
internal void rd_store_view_loading_info(B32 is_loading, U64 progress_u64, U64 progress_u64_target);
internal void rd_store_view_scroll_pos(UI_ScrollPt2 pos);
internal void rd_store_view_param(String8 key, String8 value);
internal void rd_store_view_paramf(String8 key, char *fmt, ...);
#define rd_store_view_param_f32(key, f32) rd_store_view_paramf((key), "%ff", (f32))
#define rd_store_view_param_s64(key, s64) rd_store_view_paramf((key), "%I64d", (s64))
#define rd_store_view_param_u64(key, u64) rd_store_view_paramf((key), "0x%I64x", (u64))

////////////////////////////////
//~ rjf: Window Functions

internal String8 rd_push_window_title(Arena *arena);
internal CFG_Node *rd_window_from_cfg(CFG_Node *cfg);
internal RD_WindowState *rd_window_state_from_cfg(CFG_Node *cfg);
internal RD_WindowState *rd_window_state_from_os_handle(WM_Window os);
internal void rd_window_frame(void);

////////////////////////////////
//~ rjf: Eval Visualization

internal String8 rd_value_string_from_eval(Arena *arena, String8 filter, EV_StringParams *params, FNT_Tag font, F32 font_size, F32 max_size, E_Eval eval);

////////////////////////////////
//~ rjf: Hover Eval

internal void rd_set_hover_eval(Vec2F32 pos, String8 string);

////////////////////////////////
//~ rjf: Autocompletion Lister

internal void rd_set_autocomp_regs_(E_Eval dst_eval, RD_CmdRegs *regs);
#define rd_set_autocomp_regs(dst_eval, ...) rd_set_autocomp_regs_((dst_eval), &(RD_CmdRegs){UISHELL_REGS_LIT_INIT_TOP __VA_ARGS__})

////////////////////////////////
//~ rjf: Colors, Fonts, Config

//- rjf: colors
internal MD_Node *rd_theme_tree_from_name(Arena *arena, Access *access, String8 theme_name);
internal Vec4F32 rd_rgba_from_code_color_slot(RD_CodeColorSlot slot);
internal RD_CodeColorSlot rd_code_color_slot_from_txt_token_kind(TXT_TokenKind kind);
internal RD_CodeColorSlot rd_code_color_slot_from_txt_token_kind_lookup_string(TXT_TokenKind kind, String8 string, B32 allow_macros, B32 is_called);

//- rjf: fonts
internal F32 rd_font_size(void);
internal FNT_Tag rd_font_from_slot(RD_FontSlot slot);
internal FNT_RasterFlags rd_raster_flags_from_slot(RD_FontSlot slot);

//~ rjf: Vocab Info Lookups

internal RD_VocabInfo *rd_vocab_info_from_code_name(String8 code_name);
internal RD_VocabInfo *rd_vocab_info_from_code_name_plural(String8 code_name_plural);
#define rd_plural_from_code_name(code_name) (rd_vocab_info_from_code_name(code_name)->code_name_plural)
#define rd_display_from_code_name(code_name) (rd_vocab_info_from_code_name(code_name)->display_name)
#define rd_display_plural_from_code_name(code_name) (rd_vocab_info_from_code_name(code_name)->display_name_plural)
#define rd_icon_kind_from_code_name(code_name) (rd_vocab_info_from_code_name(code_name)->icon_kind)
#define rd_singular_from_code_name_plural(code_name_plural) (rd_vocab_info_from_code_name_plural(code_name_plural)->code_name)

////////////////////////////////
//~ rjf: Auto Watch Computation

internal String8Array rd_gather_auto_exprs(Arena *arena);

////////////////////////////////
//~ rjf: Continuous Frame Requests

internal void rd_request_frame(void);

////////////////////////////////
//~ rjf: Main State Accessors

//- rjf: per-frame arena
internal Arena *rd_frame_arena(void);

////////////////////////////////
//~ rjf: Registers

#define rd_regs() (&rd_state->top_regs->v)
#define rd_base_regs() (&rd_state->base_regs.v)
internal RD_Regs *rd_push_regs_(RD_Regs *regs);
#define rd_push_regs(...) rd_push_regs_(&(RD_Regs){RD_APP_REGS_LIT_INIT_TOP __VA_ARGS__})
internal RD_Regs *rd_pop_regs(void);
#define RD_RegsScope(...) DeferLoop(rd_push_regs(__VA_ARGS__), rd_pop_regs())
internal void rd_regs_fill_slot_from_string(RD_RegSlot slot, String8 query_expr, String8 string);

////////////////////////////////
//~ rjf: Commands

//- rjf: name -> info
internal RD_AppCmdInfo rd_app_cmd_info_from_string(String8 string);
internal RD_AppRegSlot rd_app_reg_slot_from_rd_reg_slot(RD_RegSlot slot);
internal RD_RegSlot rd_reg_slot_from_app_reg_slot(RD_AppRegSlot slot);

//- rjf: pushing
internal void rd_push_stored_cmd(String8 name, RD_CmdRegs *regs);
#define rd_push_cmd_current(name) rd_push_stored_cmd((name), &(RD_CmdRegs){UISHELL_REGS_LIT_INIT_TOP})
#define rd_cmd_name(name, ...) rd_push_stored_cmd(str8_lit(name), &(RD_CmdRegs){UISHELL_REGS_LIT_INIT_TOP __VA_ARGS__})

//- rjf: iterating
internal B32 rd_next_cmd(RD_Cmd **cmd);
internal B32 rd_next_view_cmd(RD_Cmd **cmd);

//- rjf: app menus
internal RD_AppMenuSpecList rd_app_menu_specs(void);
internal void rd_app_menu_buttons(RD_AppMenuSpec *spec);
internal String8 rd_app_data_folder(Arena *arena);
internal CFG_Node *rd_cfg_new_view_tab(CFG_Node *parent, String8 view, String8 expr, B32 selected);

////////////////////////////////
//~ rjf: Main Layer Top-Level Calls

internal void rd_init(CmdLine *cmdln);
internal void rd_frame(void);

#endif // RADDBG_CORE_H
