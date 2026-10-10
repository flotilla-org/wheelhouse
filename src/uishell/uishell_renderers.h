// Wheelhouse's Renderers (CONTEXT.md, "Renderer"): one entry each. Adding a
// Renderer is one entry here and its UI function, `ui(WH_ViewContext *ctx)`
// (shell_core.h, "Renderers"). Docking validity, rendering, the view lister,
// expand rules and config schemas all read this list; nothing else names
// View kinds. It needs no other types, so the headless docking tests can
// include it (src/shell/tests/docking.c).
#ifndef UISHELL_RENDERERS_H
#define UISHELL_RENDERERS_H

// The View Spec content a Renderer can show. The first five follow
// ANDAMENTO_SLOT_*'s order (checked in uishell_workspace_store.c).
typedef U32 WH_ContentKinds;
enum
{
  WH_Content_Command   = 1<<0, // local recipe: a command and cwd
  WH_Content_File      = 1<<1, // local recipe: a file
  WH_Content_Url       = 1<<2, // a URL other than this Wheelhouse's own pages
  WH_Content_Jackstay  = 1<<3, // local recipe: a Jackstay launcher
  WH_Content_Facet     = 1<<4, // a provider facet
  // `wheelhouse:<kind>`: this frontend's own page of the Renderer's kind.
  WH_Content_Page      = 1<<5,
  // Content no other Renderer here can show, named rather than dropped.
  WH_Content_Unshown   = 1<<6,
  // A control view: the Dashboard's arrangement, not a Slot's content.
  WH_Content_Dashboard = 1<<7,
};

// Presentation-settings schemas: the config schema of a Renderer's Views.
#define UISHELL_TEXT_SETTINGS \
  "@inherit(tab)" \
  "x:{" \
  "@description(\"An expression to describe data which should be viewed as text or code.\") 'expression': expr_string," \
  "@optional @description(\"The language that the text should be interpreted as being within.\") 'lang': code_string," \
  "@no_callee_helper @default(1) @description(\"Controls whether or not line numbers are shown.\") 'show_line_numbers': bool," \
  "@no_callee_helper @default(1) @display_name('Line Wrapping') @description(\"Splits textual lines into multiple visual lines.\") 'line_wrapping': bool," \
  "@no_callee_helper @default(0) @display_name('Scroll To Bottom On Change') @description(\"Scrolls to the bottom if the text is changed.\") 'scroll_to_bottom_on_change': bool," \
  "@no_callee_helper @no_revert @default(0) @display_name('Transient') @description(\"Controls whether or not this tab will be automatically replaced.\") 'auto': bool," \
  "}"
#define UISHELL_BINARY_SETTINGS \
  "@inherit(tab)" \
  "x:{" \
  "@description(\"An expression to describe data which should be viewed as binary.\") 'expression': expr_string," \
  "@optional @expand_if(\"!$.auto_columns\") @default(16) @description(\"The number of byte columns to build before building a new row.\") 'num_columns': @range[1, 64] u64," \
  "@no_callee_helper @default(0) @display_name(\"Automatically Size Columns\") @description(\"Determines the number of byte columns based on the available space.\") 'auto_columns': bool," \
  "}"
#define UISHELL_TERMINAL_SETTINGS \
  "@inherit(tab)" \
  "x:{" \
  "@no_callee_helper @runtime_value(terminal_hosting) @display_name('Hosting') 'hosting': string," \
  "@no_callee_helper @runtime_action(terminal_hosting) @display_name('Hosting Action') @description('Hand to daemon or Adopt the current terminal.') 'hosting_action': string," \
  "@no_callee_helper @default(0) @display_name('Show Hosting Overlay') @description('Show terminal hosting metadata and its action on the canvas.') 'show_hosting_overlay': bool," \
  "@optional @description(\"The command to run when a provider is attached.\") 'command': string," \
  "@optional @description(\"The working directory to use when a provider is attached.\") 'cwd': path," \
  "}"
#define UISHELL_TERMINAL_FIXTURE_SETTINGS "@inherit(terminal) x:{}"
#define UISHELL_SCROLL_REGION_FIXTURE_SETTINGS "@inherit(tab) x:{}"
#define UISHELL_NO_SETTINGS ""

// Column helpers, so an entry can say it has none.
#define WH_STATE(T) sizeof(T)
#define WH_NO_STATE 0
#define WH_EXPAND(name) EV_EXPAND_RULE_INFO_FUNCTION_NAME(name)
#define WH_NO_EXPAND 0

// X(name, ui, state, content, traits, minimum_width, default_host, expand, settings)
//   name           the View kind, as config and View Specs spell it
//   ui             its UI function, WH_VIEW_UI_FUNCTION_DEF(ui)
//   state          its per-view state type, WH_STATE(T), or WH_NO_STATE; the
//                  size is checked where the state is read (wh_view_state)
//   content        the View Spec content it renders (WH_Content_*)
//   traits         RD_ViewTrait_* (shell_docking.h)
//   minimum_width  the narrowest host it docks in, in pixels (0: any)
//   default_host   RD_DockHostKind_<default_host>
//   expand         its expand rule for evaluated values, or WH_NO_EXPAND
//   settings       its presentation-settings schema, or UISHELL_NO_SETTINGS
//
// The first five keep the view lister's order. Jackstay keeps its sessions in
// its own map (uishell_jackstay.c), because closing one finishes after its
// View has gone. The terminal fixture is the terminal with fixture input.
#define WH_RENDERERS(X) \
  X(text, shell_text, WH_STATE(UIShell_TextViewState), WH_Content_File|WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_LiveDragPreview|RD_ViewTrait_Visualizer|RD_ViewTrait_LegacyEval|RD_ViewTrait_VersionedSurface|RD_ViewTrait_Listed, \
    0, WorkspaceRegion, WH_EXPAND(shell_text), UISHELL_TEXT_SETTINGS) \
  X(binary, binary, WH_STATE(UIShell_BinaryViewState), WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_LiveDragPreview|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop|RD_ViewTrait_LegacyEval|RD_ViewTrait_Listed, \
    0, WorkspaceRegion, WH_NO_EXPAND, UISHELL_BINARY_SETTINGS) \
  X(terminal, terminal, WH_STATE(UIShell_TerminalViewState), WH_Content_Command|WH_Content_Facet|WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop|RD_ViewTrait_VersionedSurface|RD_ViewTrait_Listed, \
    0, WorkspaceRegion, WH_NO_EXPAND, UISHELL_TERMINAL_SETTINGS) \
  X(terminal_fixture, terminal, WH_STATE(UIShell_TerminalViewState), WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop|RD_ViewTrait_VersionedSurface|RD_ViewTrait_Listed, \
    0, WorkspaceRegion, WH_NO_EXPAND, UISHELL_TERMINAL_FIXTURE_SETTINGS) \
  X(scroll_region_fixture, scroll_region_fixture, WH_STATE(UIShell_ScrollRegionFixtureState), WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop|RD_ViewTrait_Listed, \
    128, WorkspaceRegion, WH_NO_EXPAND, UISHELL_SCROLL_REGION_FIXTURE_SETTINGS) \
  X(jackstay, jackstay, WH_NO_STATE, WH_Content_Jackstay|WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop, \
    0, WorkspaceRegion, WH_NO_EXPAND, UISHELL_NO_SETTINGS) \
  X(sessions, sessions, WH_NO_STATE, WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_Section|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop, \
    0, WorkspaceRegion, WH_NO_EXPAND, UISHELL_NO_SETTINGS) \
  X(bitmap, bitmap, WH_NO_STATE, WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop|RD_ViewTrait_LegacyEval, \
    0, WorkspaceRegion, WH_EXPAND(bitmap), UISHELL_NO_SETTINGS) \
  X(color, color, WH_STATE(UIShell_ColorViewState), WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop|RD_ViewTrait_LegacyEval, \
    0, WorkspaceRegion, WH_EXPAND(color), UISHELL_NO_SETTINGS) \
  X(geo3d, geo3d, WH_STATE(UIShell_Geo3DViewState), WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop|RD_ViewTrait_LegacyEval, \
    0, WorkspaceRegion, WH_EXPAND(geo3d), UISHELL_NO_SETTINGS) \
  X(placeholder, placeholder, WH_NO_STATE, WH_Content_Page|WH_Content_Unshown, \
    RD_ViewTrait_Content|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop, \
    0, WorkspaceRegion, WH_NO_EXPAND, UISHELL_NO_SETTINGS) \
  X(getting_started, getting_started, WH_NO_STATE, WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_QueryFocus, \
    0, WorkspaceRegion, WH_NO_EXPAND, UISHELL_NO_SETTINGS) \
  X(pending, pending, WH_STATE(UIShell_PendingViewState), WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_QueryFocus|RD_ViewTrait_LegacyEval, \
    0, WorkspaceRegion, WH_NO_EXPAND, UISHELL_NO_SETTINGS) \
  X(watch, watch, WH_STATE(UIShell_WatchViewState), WH_Content_Page, \
    RD_ViewTrait_Content|RD_ViewTrait_QueryFocus, \
    0, WorkspaceRegion, WH_NO_EXPAND, UISHELL_NO_SETTINGS) \
  X(sidebar_section, sidebar_section, WH_NO_STATE, WH_Content_Dashboard, \
    RD_ViewTrait_Content|RD_ViewTrait_Section|RD_ViewTrait_ControlSplitScope|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop|RD_ViewTrait_Control, \
    0, Sidebar, WH_NO_EXPAND, UISHELL_NO_SETTINGS) \
  X(pinned_cards, pinned_cards, WH_NO_STATE, WH_Content_Dashboard, \
    RD_ViewTrait_Content|RD_ViewTrait_Section|RD_ViewTrait_ControlSplitScope|RD_ViewTrait_Visualizer|RD_ViewTrait_ExpressionDrop|RD_ViewTrait_Control, \
    0, Sidebar, WH_NO_EXPAND, UISHELL_NO_SETTINGS) \
  X(workspace_selector, null, WH_NO_STATE, WH_Content_Dashboard, \
    RD_ViewTrait_Content|RD_ViewTrait_Section|RD_ViewTrait_SelectsWorkspaces|RD_ViewTrait_Singleton|RD_ViewTrait_ControlSplitScope|RD_ViewTrait_Control, \
    0, Sidebar, WH_NO_EXPAND, UISHELL_NO_SETTINGS)

// Each Renderer's place in the list.
typedef enum WH_RendererIndex
{
#define WH_RENDERER_INDEX(name, ui, state, content, traits, width, host, expand, settings) WH_RendererIndex_##name,
  WH_RENDERERS(WH_RENDERER_INDEX)
#undef WH_RENDERER_INDEX
  WH_RendererIndex_COUNT,
} WH_RendererIndex;

#endif // UISHELL_RENDERERS_H
