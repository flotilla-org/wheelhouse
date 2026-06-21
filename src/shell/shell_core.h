// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef SHELL_CORE_H
#define SHELL_CORE_H

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
//~ rjf: Shell Command Metadata Types

typedef U32 UIShell_QueryFlags;
enum
{
  UIShell_QueryFlag_AllowFiles       = (1<<0),
  UIShell_QueryFlag_AllowFolders     = (1<<1),
  UIShell_QueryFlag_CodeInput        = (1<<2),
  UIShell_QueryFlag_KeepOldInput     = (1<<3),
  UIShell_QueryFlag_SelectOldInput   = (1<<4),
  UIShell_QueryFlag_Floating         = (1<<5),
  UIShell_QueryFlag_Required         = (1<<6),
};

typedef U32 UIShell_CmdFlags;
enum
{
  UIShell_CmdFlag_ListInUI      = (1<<0),
  UIShell_CmdFlag_ListInTab     = (1<<2),
  UIShell_CmdFlag_ListInTextPt  = (1<<3),
  UIShell_CmdFlag_ListInTextRng = (1<<4),
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
//~ rjf: Shell Context Register Types

typedef enum UIShell_ContextRegSlot
{
  UIShell_ContextRegSlot_Null,
  UIShell_ContextRegSlot_Window,
  UIShell_ContextRegSlot_Panel,
  UIShell_ContextRegSlot_Tab,
  UIShell_ContextRegSlot_View,
  UIShell_ContextRegSlot_PrevTab,
  UIShell_ContextRegSlot_DstPanel,
  UIShell_ContextRegSlot_Cfg,
  UIShell_ContextRegSlot_CfgList,
  UIShell_ContextRegSlot_FilePath,
  UIShell_ContextRegSlot_Cursor,
  UIShell_ContextRegSlot_Mark,
  UIShell_ContextRegSlot_TextKey,
  UIShell_ContextRegSlot_LangKind,
  UIShell_ContextRegSlot_Vaddr,
  UIShell_ContextRegSlot_Expr,
  UIShell_ContextRegSlot_UIKey,
  UIShell_ContextRegSlot_OffPx,
  UIShell_ContextRegSlot_RegSlot,
  UIShell_ContextRegSlot_ForceConfirm,
  UIShell_ContextRegSlot_ForceFocus,
  UIShell_ContextRegSlot_DoImplicitRoot,
  UIShell_ContextRegSlot_DoLister,
  UIShell_ContextRegSlot_DoBigRows,
  UIShell_ContextRegSlot_NonGraphical,
  UIShell_ContextRegSlot_PreferNewTab,
  UIShell_ContextRegSlot_ActivateWithSingleClick,
  UIShell_ContextRegSlot_Dir2,
  UIShell_ContextRegSlot_String,
  UIShell_ContextRegSlot_CmdName,
  UIShell_ContextRegSlot_WMEvent,
  UIShell_ContextRegSlot_COUNT,
}
UIShell_ContextRegSlot;

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
  UIShell_ContextRegSlot reg_slot;
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
.window = uishell_regs()->window,\
.panel = uishell_regs()->panel,\
.tab = uishell_regs()->tab,\
.view = uishell_regs()->view,\
.prev_tab = uishell_regs()->prev_tab,\
.dst_panel = uishell_regs()->dst_panel,\
.cfg = uishell_regs()->cfg,\
.cfg_list = uishell_regs()->cfg_list,\
.file_path = uishell_regs()->file_path,\
.cursor = uishell_regs()->cursor,\
.mark = uishell_regs()->mark,\
.text_key = uishell_regs()->text_key,\
.lang_kind = uishell_regs()->lang_kind,\
.vaddr = uishell_regs()->vaddr,\
.expr = uishell_regs()->expr,\
.ui_key = uishell_regs()->ui_key,\
.off_px = uishell_regs()->off_px,\
.reg_slot = uishell_regs()->reg_slot,\
.force_confirm = uishell_regs()->force_confirm,\
.force_focus = uishell_regs()->force_focus,\
.do_implicit_root = uishell_regs()->do_implicit_root,\
.do_lister = uishell_regs()->do_lister,\
.do_big_rows = uishell_regs()->do_big_rows,\
.non_graphical = uishell_regs()->non_graphical,\
.prefer_new_tab = uishell_regs()->prefer_new_tab,\
.activate_with_single_click = uishell_regs()->activate_with_single_click,\
.dir2 = uishell_regs()->dir2,\
.string = uishell_regs()->string,\
.cmd_name = uishell_regs()->cmd_name,\
.wm_event = uishell_regs()->wm_event,

#include "generated/shell.meta.h"

typedef struct RD_VocabInfo RD_VocabInfo;
struct RD_VocabInfo
{
  String8 code_name;
  String8 code_name_plural;
  String8 display_name;
  String8 display_name_plural;
  RD_IconKind icon_kind;
};

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

typedef struct UIShell_Cmd UIShell_Cmd;
struct UIShell_Cmd
{
  String8 name;
  UIShell_Regs *regs;
};

typedef struct UIShell_CmdNode UIShell_CmdNode;
struct UIShell_CmdNode
{
  UIShell_CmdNode *next;
  UIShell_CmdNode *prev;
  UIShell_Cmd cmd;
};

typedef struct UIShell_CmdList UIShell_CmdList;
struct UIShell_CmdList
{
  UIShell_CmdNode *first;
  UIShell_CmdNode *last;
  U64 count;
};

typedef struct UIShell_DefaultBinding UIShell_DefaultBinding;
struct UIShell_DefaultBinding
{
  String8 string;
  CFG_Binding binding;
};

typedef struct UIShell_AppCmdInfo UIShell_AppCmdInfo;
struct UIShell_AppCmdInfo
{
  String8 string;
  String8 display_name;
  RD_IconKind icon_kind;
  String8 description;
  String8 search_tags;
  String8 ctx_filter;
  UIShell_CmdFlags flags;
  UIShell_QueryFlags query_flags;
  UIShell_AppRegSlot query_slot;
  String8 query_expr;
  String8 query_view_name;
};

typedef U64 UIShell_CmdPackCmdCountFunction(void);
typedef UIShell_AppCmdInfo UIShell_CmdPackCmdInfoFromIndexFunction(U64 idx);
typedef UIShell_AppCmdInfo UIShell_CmdPackCmdInfoFromStringFunction(String8 string);
typedef U64 UIShell_CmdPackBindingCountFunction(void);
typedef UIShell_DefaultBinding UIShell_CmdPackBindingFromIndexFunction(U64 idx);
typedef RD_AppMenuSpecList UIShell_CmdPackMenuSpecsFunction(void);
typedef B32 UIShell_CmdPackDispatchFunction(String8 name);

typedef struct UIShell_CmdPack UIShell_CmdPack;
struct UIShell_CmdPack
{
  UIShell_CmdPack *next;
  UIShell_CmdPack *prev;
  String8 name;
  UIShell_CmdPackCmdCountFunction *cmd_count;
  UIShell_CmdPackCmdInfoFromIndexFunction *cmd_info_from_index;
  UIShell_CmdPackCmdInfoFromStringFunction *cmd_info_from_string;
  UIShell_CmdPackBindingCountFunction *binding_count;
  UIShell_CmdPackBindingFromIndexFunction *binding_from_index;
  UIShell_CmdPackMenuSpecsFunction *menu_specs;
  UIShell_CmdPackDispatchFunction *dispatch;
};

////////////////////////////////
//~ rjf: Context Register Types

typedef struct UIShell_RegsNode UIShell_RegsNode;
struct UIShell_RegsNode
{
  UIShell_RegsNode *next;
  UIShell_Regs v;
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

typedef struct RD_WorkspaceSurfaceEntry RD_WorkspaceSurfaceEntry;
struct RD_WorkspaceSurfaceEntry
{
  U64 box_key;
  U64 workspace_id;
  B32 composite; // visible workspace composites to the stage; preview-scale ones render offscreen only
  B32 full_res;  // non-composite, but rendered at full resolution (the selected child in the zoom view, for seamless open/close)
  U64 content_version_accum; // versions declared by the views built inside this workspace (terminal generations, text content hashes)
  B32 has_unversioned_views; // a view without a declared version was built -> shape-only preservation is unsafe; keep byte hashing
};

typedef struct RD_WorkspacePreviewDemand RD_WorkspacePreviewDemand;
struct RD_WorkspacePreviewDemand
{
  RD_WorkspacePreviewDemand *next;
  U64 workspace_id;
  F32 width_pt;          // largest width any consumer wants this preview at
  U64 frame_index;
};

typedef struct RD_SurfaceCacheNode RD_SurfaceCacheNode;
struct RD_SurfaceCacheNode
{
  RD_SurfaceCacheNode *next;
  U64 key;
  R_Handle texture;
  Vec2S32 size;
  U64 last_use_frame_index;
  U64 rendered_hash;       // content hash of what's in the texture (0 = invalid, must render)
  B32 last_was_preserved;  // dev visibility: last frame skipped the render
  B32 retained;            // survives eviction when undemanded; shows stale content (e.g. workspace previews)
};

////////////////////////////////
//~ rjf: Chrome Placement (ADR-0006)

typedef struct RD_WindowState RD_WindowState;

// Chrome niches: anchored slots across the chrome hosts an element may land in.
// Each belongs to a host (title bar, or the sidebar/control-surface). Hidden is
// the terminal placement.
typedef enum RD_ChromeNiche
{
  RD_ChromeNiche_Hidden,
  RD_ChromeNiche_TitleBarMenu,      // owner-drawn menu bar
  RD_ChromeNiche_TitleBarLeading,   // leading buttons ("a")
  RD_ChromeNiche_TitleBarTrailing,  // trailing buttons ("b")
  RD_ChromeNiche_SidebarActions,    // the control surface's action row (relocation fallback)
  RD_ChromeNiche_COUNT
}
RD_ChromeNiche;

typedef enum RD_ChromeElementKind
{
  RD_ChromeElementKind_Menu,
  RD_ChromeElementKind_ProjectSelector,
  RD_ChromeElementKind_SidebarCollapse,
  RD_ChromeElementKind_NewWorkspace,
  RD_ChromeElementKind_OverviewToggle,
  RD_ChromeElementKind_COUNT
}
RD_ChromeElementKind;

// a chrome element as fed to resolution: its measured width, its priority
// (lowest sheds first when a host overflows), and its placement chain (ordered
// candidate niches, walked on overflow; the last should be a terminal the host
// can always accept — a roomy host like the sidebar, or Hidden).
typedef struct RD_ChromeElement RD_ChromeElement;
struct RD_ChromeElement
{
  RD_ChromeElementKind kind;
  F32 width_px;
  S32 priority;
  RD_ChromeNiche chain[RD_ChromeNiche_COUNT];
  U64 chain_count;
};

internal RD_ChromeNiche rd_chrome_niche_host_is_title_bar(RD_ChromeNiche niche);
// resolves elements into niches (written to niche_out, indexed by element kind),
// given each host's width budget (indexed by a host id derived from niche).
internal void rd_chrome_resolve(RD_ChromeElement *elements, U64 count, F32 title_bar_budget_px, RD_ChromeNiche *niche_out);
// element build callbacks — emit the control under the current UI parent &
// return its signal (title-bar callers register the rect as custom-title-bar
// client area so the window manager doesn't eat clicks as window drags)
internal UI_Signal rd_chrome_build_new_workspace(CFG_Node *owner_cfg);
internal UI_Signal rd_chrome_build_overview_toggle(RD_WindowState *ws);
internal UI_Signal rd_chrome_build_sidebar_collapse(CFG_Node *owner_cfg);

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

  // rjf: font raster flags (recomputed each frame)
  FNT_RasterFlags font_slot_raster_flags[RD_FontSlot_COUNT];

  // rjf: dev interface state
  B32 dev_menu_is_open;

  // rjf: chrome placement (recomputed each frame, before the title bar & the
  // control surface build, so both read the same resolution) — ADR-0006
  RD_ChromeNiche chrome_niche[RD_ChromeElementKind_COUNT];
  F32 chrome_leading_px;  // pixel extent of the title bar's left zone (decorations + leading buttons)
  F32 chrome_trailing_px; // pixel extent of the right zone (trailing buttons + window controls)

  // rjf: menu bar state
  B32 menu_bar_focused;
  B32 menu_bar_focused_on_press;
  B32 menu_bar_key_held;
  B32 menu_bar_focus_press_started;

  // rjf: root controlled split runtime state
  B32 root_controlled_split_initialized;
  CFG_ID root_controlled_split_selected_workspace_id;
  CFG_ID root_controlled_split_renaming_workspace_id;
  U8 root_controlled_split_rename_buffer[256];
  U64 root_controlled_split_rename_size;
  TxtPt root_controlled_split_rename_cursor;
  TxtPt root_controlled_split_rename_mark;

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

  // surface render-target cache, keyed by content (box key); see
  // UI_BoxFlag_RenderToSurface & the draw-walk surface bracketing
  RD_SurfaceCacheNode *first_surface_cache_node;
  RD_SurfaceCacheNode *free_surface_cache_node;

  // workspace surfaces: one entry per workspace built this frame (frame-arena
  // array, sized to the inventory) - the visible child composites to the
  // stage; the others build offscreen at reduced resolution & are visible at
  // preview scale (sidebar rows, zoom view)
  RD_WorkspaceSurfaceEntry *workspace_surface_entries;
  U64 workspace_surface_entry_count;
  RD_WorkspaceSurfaceEntry *active_workspace_surface_entry; // entry being built right now; views declare versions into it

  // workspace zoom view: the controlled split presenting all children as tiles.
  // zoom_t animates open/close; the selected child's composite interpolates
  // between the full workspace presentation & its tile
  B32 workspace_zoom_open;
  F32 workspace_zoom_t;
  F32 workspace_coverflow_t; // cover flow row position, eased toward the selected child's index
  Rng2F32 workspace_content_uv; // workspace-region subrect of the wrapper surfaces, uv space; consumers crop with it

  // preview size demands: consumers register the size they show a workspace
  // preview at; the preview surface is allocated for the largest demand
  RD_WorkspacePreviewDemand *first_workspace_preview_demand;
};

typedef struct RD_WindowStateSlot RD_WindowStateSlot;
struct RD_WindowStateSlot
{
  RD_WindowState *first;
  RD_WindowState *last;
};

////////////////////////////////
//~ rjf: Tweaks

// a code-declared setting: rd_tweak_f32("name", default) is an ordinary
// setting whose default (& metadata) come from the C call site instead of an
// mdesk schema string. this registry is only the dynamic half of the schema
// table — each frame it's projected into the `code_defaults` schema (which
// the `user` schema @inherits), so the rows surface in the standard settings
// UI with the standard editors/revert; values are ordinary cfg settings,
// written wherever the hosting settings UI writes. the one extra affordance
// is write-back: a diverged value can be written to the call site as the new
// compiled default (the unique name string anchors the site)

typedef struct RD_TweakNode RD_TweakNode;
struct RD_TweakNode
{
  RD_TweakNode *hash_next;
  RD_TweakNode *order_next;   // registration order; stable schema ordering
  String8 name;
  String8 file;               // __FILE__ of the call site
  F32 default_value;          // the literal at the call site, as-compiled
  B32 has_range;              // declared range -> @range in the schema -> slider editor
  F32 range_min;
  F32 range_max;
  U64 last_use_frame_index;   // stamped when the call site runs
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
  B32 terminal_glyph_trace_enabled;
  B32 terminal_glyph_trace_all_rows;
  U64 terminal_glyph_trace_row;
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

  // rjf: code-declared settings registry (projected into the `code_defaults` schema)
  RD_TweakNode *tweak_slots[64];
  RD_TweakNode *first_tweak;
  RD_TweakNode *last_tweak;
  Arena *tweak_schema_arena;
  CFG_SchemaNode *tweak_schema_node;
  U64 tweak_schema_gen;
  U64 tweak_schema_built_gen;

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
  UIShell_RegsNode base_regs;
  UIShell_RegsNode *top_regs;

  // rjf: autosave state
  F32 seconds_until_autosave;

  // rjf: commands
  UIShell_CmdPack *first_cmd_pack;
  UIShell_CmdPack *last_cmd_pack;
  Arena *cmds_arenas[2];
  UIShell_CmdList cmds[2];
  U64 cmds_gen;
  Arena *cmd_output_arena;
  String8List cmd_outputs;

  // rjf: popup state
  UI_Key popup_key;
  B32 popup_active;
  F32 popup_t;
  Arena *popup_arena;
  UIShell_CmdList popup_cmds;
  String8 popup_title;
  String8 popup_desc;

  // rjf: text editing mode state
  B32 text_edit_mode;

  // rjf: contextual hover info
  UIShell_Regs *hover_regs;
  UIShell_ContextRegSlot hover_regs_slot;
  UIShell_Regs *next_hover_regs;
  UIShell_ContextRegSlot next_hover_regs_slot;

  // rjf: icon texture
  R_Handle icon_texture;

  // rjf: fixed ui keys
  UI_Key drop_completion_key;
  UI_Key ctx_menu_key;

  // rjf: drag/drop state
  Arena *drag_drop_arena;
  UIShell_Regs *drag_drop_regs;
  UIShell_ContextRegSlot drag_drop_regs_slot;
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
//~ rjf: Workspace Runtime Types

typedef struct UIShell_WorkspaceMount UIShell_WorkspaceMount;
struct UIShell_WorkspaceMount
{
  CFG_Node *owner_cfg;
  CFG_Node *window_cfg;
  CFG_Node *workspace_cfg;
  CFG_Node *panels_root;
  Axis2 root_split_axis;
  CFG_PanelTree panel_tree;
};

typedef struct UIShell_MaterializedWorkspace UIShell_MaterializedWorkspace;
struct UIShell_MaterializedWorkspace
{
  UIShell_MaterializedWorkspace *next;
  UIShell_MaterializedWorkspace *prev;
  CFG_ID id;
  String8 display_name;
  UIShell_WorkspaceMount mount;
};

typedef struct UIShell_MaterializedWorkspaceInventory UIShell_MaterializedWorkspaceInventory;
struct UIShell_MaterializedWorkspaceInventory
{
  UIShell_MaterializedWorkspace *first;
  UIShell_MaterializedWorkspace *last;
  UIShell_MaterializedWorkspace *selected;
  U64 count;
};

typedef struct UIShell_ControlledSplit UIShell_ControlledSplit;
struct UIShell_ControlledSplit
{
  CFG_Node *owner_cfg;
  UIShell_MaterializedWorkspaceInventory inventory;
};

////////////////////////////////
//~ rjf: Registers Type Functions

internal void uishell_regs_copy_contents(Arena *arena, UIShell_Regs *dst, UIShell_Regs *src);

////////////////////////////////
//~ rjf: Commands Type Functions

internal void uishell_cmd_list_push_new(Arena *arena, UIShell_CmdList *cmds, String8 name, UIShell_Regs *regs);

////////////////////////////////
//~ rjf: View UI Rule Functions

internal RD_ViewUIRuleMap *rd_view_ui_rule_map_make(Arena *arena, U64 slots_count);
internal void rd_view_ui_rule_map_insert(Arena *arena, RD_ViewUIRuleMap *map, String8 string, RD_ViewUIFunctionType *ui);

internal RD_ViewUIRule *rd_view_ui_rule_from_string(String8 string);
internal B32 rd_view_name_is_listed_in_app(String8 name);

////////////////////////////////
//~ rjf: Global Cross-Window UI Interaction State Functions

internal B32 rd_drag_is_active(void);
internal void rd_drag_begin(UIShell_ContextRegSlot slot);
internal B32 rd_drag_drop(void);
internal void rd_drag_kill(void);

internal void rd_set_hover_regs(UIShell_ContextRegSlot slot);

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

internal RD_TweakNode *rd_tweak_node_from_name(String8 name, F32 default_value, String8 file);
internal RD_TweakNode *rd_tweak_node_lookup(String8 name);
internal F32 rd_tweak_f32_value(String8 name, F32 default_value, String8 file);
internal F32 rd_tweak_f32_range_value(String8 name, F32 default_value, F32 range_min, F32 range_max, String8 file);
internal B32 rd_tweak_write_default_to_source(RD_TweakNode *tweak, F32 value);
#define rd_tweak_f32(name, default_value) rd_tweak_f32_value(str8_lit(name), (default_value), str8_lit(__FILE__))
#define rd_tweak_f32_range(name, default_value, range_min, range_max) rd_tweak_f32_range_value(str8_lit(name), (default_value), (range_min), (range_max), str8_lit(__FILE__))

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

//- rjf: writing values back to evaluation spaces
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
internal RD_WindowState *rd_window_state_from_cfg__existing(CFG_Node *cfg);
internal RD_WindowState *rd_window_state_from_cfg(CFG_Node *cfg);
internal RD_SurfaceCacheNode *rd_window_surface_node_from_key(RD_WindowState *ws, U64 key, Vec2S32 size);
internal RD_SurfaceCacheNode *rd_window_surface_node_lookup(RD_WindowState *ws, U64 key);
internal U64 rd_workspace_preview_surface_key(U64 workspace_id);
internal RD_WorkspaceSurfaceEntry *rd_workspace_surface_entry_from_box_key(RD_WindowState *ws, U64 box_key);
internal void rd_workspace_preview_demand_push(RD_WindowState *ws, U64 workspace_id, F32 width_pt);
internal void rd_workspace_surface_contribute_version(U64 version);
internal void rd_workspace_surface_mark_unversioned_view(void);
internal F32 rd_workspace_preview_demand_width(RD_WindowState *ws, U64 workspace_id);
internal void rd_window_surface_cache_evict(RD_WindowState *ws);
internal RD_WindowState *rd_window_state_from_os_handle(WM_Window os);
internal CFG_Node *uishell_workspace_cfg_from_cfg(CFG_Node *cfg);
internal UIShell_WorkspaceMount uishell_workspace_mount_from_owner_cfg(Arena *arena, CFG_Node *window, CFG_Node *owner);
internal UIShell_WorkspaceMount uishell_workspace_mount_from_window(Arena *arena, CFG_Node *window);
internal UIShell_WorkspaceMount uishell_workspace_mount_from_cfg(Arena *arena, CFG_Node *cfg);
internal UIShell_WorkspaceMount uishell_workspace_mount_from_current_regs(Arena *arena);
internal UIShell_ControlledSplit uishell_root_controlled_split_from_window(Arena *arena, CFG_Node *window);
internal B32 uishell_controlled_split_workspace_can_close(UIShell_ControlledSplit *split, UIShell_MaterializedWorkspace *workspace);
internal UIShell_WorkspaceMount *uishell_controlled_split_selected_mount(UIShell_ControlledSplit *split);
internal Rng2F32 uishell_controlled_split_control_rect(UIShell_ControlledSplit *split, Rng2F32 rect);
internal Rng2F32 uishell_controlled_split_workspace_rect(UIShell_ControlledSplit *split, Rng2F32 rect);
internal void rd_window_frame(void);

////////////////////////////////
//~ rjf: Eval Visualization

internal String8 rd_value_string_from_eval(Arena *arena, String8 filter, EV_StringParams *params, FNT_Tag font, F32 font_size, F32 max_size, E_Eval eval);

////////////////////////////////
//~ rjf: Hover Eval

internal void rd_set_hover_eval(Vec2F32 pos, String8 string);

////////////////////////////////
//~ rjf: Autocompletion Lister

internal void rd_set_autocomp_regs_(E_Eval dst_eval, UIShell_Regs *regs);
#define rd_set_autocomp_regs(dst_eval, ...) rd_set_autocomp_regs_((dst_eval), &(UIShell_Regs){UISHELL_REGS_LIT_INIT_TOP __VA_ARGS__})

////////////////////////////////
//~ rjf: Colors, Fonts, Config

//- rjf: colors
internal MD_Node *rd_theme_tree_from_name(Arena *arena, Access *access, String8 theme_name);
internal CFG_NodePtrList rd_theme_color_cfgs_from_user_project(Arena *arena);
internal String8 rd_window_theme_name_from_settings(void);
internal UI_Theme *rd_theme_from_name_and_colors(Arena *scratch_arena, Access *access, String8 theme_name, CFG_NodePtrList colors_cfgs, B32 fallback_to_default);
internal UI_Theme *rd_workspace_theme_from_cfg(Arena *scratch_arena, Access *access, CFG_Node *workspace_cfg, CFG_NodePtrList colors_cfgs, UI_Theme *fallback_theme);
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

#define uishell_regs() (&rd_state->top_regs->v)
#define uishell_base_regs() (&rd_state->base_regs.v)
internal UIShell_Regs *uishell_push_regs_(UIShell_Regs *regs);
#define uishell_push_regs(...) uishell_push_regs_(&(UIShell_Regs){UISHELL_REGS_LIT_INIT_TOP __VA_ARGS__})
internal UIShell_Regs *uishell_pop_regs(void);
#define UIShell_RegsScope(...) DeferLoop(uishell_push_regs(__VA_ARGS__), uishell_pop_regs())
internal void uishell_regs_fill_slot_from_string(UIShell_ContextRegSlot slot, String8 query_expr, String8 string);

////////////////////////////////
//~ rjf: Commands

//- rjf: name -> info
internal void uishell_register_cmd_pack(UIShell_CmdPack *pack);
internal UIShell_AppCmdInfo uishell_app_cmd_info_from_string(String8 string);
internal UIShell_ContextRegSlot uishell_context_reg_slot_from_app_reg_slot(UIShell_AppRegSlot slot);

//- rjf: pushing
internal void uishell_push_stored_cmd(String8 name, UIShell_Regs *regs);
#define uishell_push_cmd_current(name) uishell_push_stored_cmd((name), &(UIShell_Regs){UISHELL_REGS_LIT_INIT_TOP})
#define uishell_cmd(name, ...) uishell_push_stored_cmd(str8_lit(name), &(UIShell_Regs){UISHELL_REGS_LIT_INIT_TOP __VA_ARGS__})

//- rjf: iterating
internal B32 uishell_next_cmd(UIShell_Cmd **cmd);
internal B32 uishell_next_view_cmd(UIShell_Cmd **cmd);

//- rjf: app menus
internal RD_AppMenuSpecList rd_app_menu_specs(void);
internal void rd_app_menu_buttons(RD_AppMenuSpec *spec);
internal void rd_app_menu_spec_content(RD_AppMenuSpec *spec);
internal String8 rd_app_data_folder(Arena *arena);
internal CFG_Node *rd_cfg_new_view_tab(CFG_Node *parent, String8 view, String8 expr, B32 selected);

////////////////////////////////
//~ rjf: Main Layer Top-Level Calls

internal void rd_init(CmdLine *cmdln);
internal void rd_frame(void);

#endif // SHELL_CORE_H
