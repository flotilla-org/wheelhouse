// Workspace arrangements and Slots, kept by Andamento (uishell_workspace_store.c).
#ifndef UISHELL_WORKSPACE_STORE_H
#define UISHELL_WORKSPACE_STORE_H

// `window` as this device saves it in the Dashboard's presentation file: each
// workspace whose arrangement Andamento keeps loses its panel tree and keeps
// only what is this device's (uishell_workspace_store.c, "Device-local"),
// and its sidebar arrangement likewise (uishell_sidebar_store.c).
internal String8 uishell_workspace_store_window_text(Arena *arena, String8 root_path, CFG_Node *window);

// A Slot's View Spec (CONTEXT.md, "View Spec"), as Wheelhouse holds it.
// `content` is an ANDAMENTO_SLOT_* value.
struct UIShell_ViewSpec
{
  U32 content;
  String8 provider, kind, id, facet;
  // The command line a terminal runs. A spec Andamento holds as argv keeps
  // it, and command is its line for this host's shell
  // (uishell_store_command_line); a View holds only the line.
  String8 command;
  String8 *argv;
  U64 argc;
  B32 has_cwd;
  String8 cwd, path, url, launcher, endpoint, presentation;
};

// What a Renderer is told about `view`'s Slot: its address, its View Spec
// (zero for a placeholder, whose Slot keeps the spec it was made from),
// status, why its content isn't resolved (with WH_SlotStatus_Waiting or
// _Unverified) and Target Resolution. A View outside a workspace's
// arrangement is not a Slot, and gets only its resolution.
internal void uishell_store_view_slot(Arena *arena, CFG_Node *view, WH_SlotAddress *slot, UIShell_ViewSpec const **spec,
                                      WH_SlotStatus *status, WH_SlotWait *wait, WH_TargetResolution *resolution);

// What a View tells the user about its Slot (uishell_workspace_store.c,
// "Notices"): an update waiting for an answer, a previous instance kept, an
// update that failed, and the Workspace Overlay's flags. Each offers at most
// two actions.
typedef enum UIShell_SlotAction
{
  UIShell_SlotAction_None,
  UIShell_SlotAction_Update,           // apply an `ask` update
  UIShell_SlotAction_Decline,          // decline it
  UIShell_SlotAction_ReleasePrevious,  // close the previous instance kept
  UIShell_SlotAction_Retry,            // retry a failed update
  UIShell_SlotAction_Reattach,         // drop the user's edit: the provider's again
  UIShell_SlotAction_KeepMine,         // keep the user's edit against the provider's change
  UIShell_SlotAction_Remove,           // close the View; a provider's Slot is tombstoned
  UIShell_SlotAction_KeepLayout,       // keep the user's arrangement over the provider's change
  UIShell_SlotAction_FollowLayout,     // follow the provider's arrangement again
  UIShell_SlotAction_COUNT
}
UIShell_SlotAction;

typedef struct UIShell_SlotNotice UIShell_SlotNotice;
struct UIShell_SlotNotice
{
  // A word for the tab's title; empty for a notice that has none.
  String8 badge;
  String8 text;
  UIShell_SlotAction actions[2];
};

internal U64 uishell_store_notices(Arena *arena, CFG_Node *view, UIShell_SlotNotice *out, U64 cap);
internal void uishell_store_act(CFG_Node *view, UIShell_SlotAction action);
// The first notice's word, for the tab's title; empty for none.
internal String8 uishell_slot_badge(Arena *arena, CFG_Node *view);
// Builds a one-line banner for `view`'s first notice at the top of `rect`,
// and returns what is left of `rect` for the View.
internal Rng2F32 uishell_slot_banner(CFG_Node *view, Rng2F32 rect);

// A workspace's Workspace Layouts (uishell_workspace_store.c, "Workspace
// Layouts"), in its template's order: none when its subject's kind has no
// template. `current` and `fallback` (the default) index `names`, or are
// `count` for none.
typedef struct UIShell_WorkspaceLayouts UIShell_WorkspaceLayouts;
struct UIShell_WorkspaceLayouts
{
  String8 *names;
  U64 count;
  U64 current;
  U64 fallback;
  // The user has rearranged the current layout: resetting it does something.
  B32 owned;
};

internal UIShell_WorkspaceLayouts uishell_store_layouts(Arena *arena, CFG_Node *owner);
// Shows layout `name`, or (empty) the default.
internal void uishell_store_choose_layout(CFG_Node *owner, String8 name);
internal void uishell_store_reset_layout(CFG_Node *owner);
// The slots the layout leaves out, and showing one in `panel`.
internal String8Array uishell_store_unplaced(Arena *arena, CFG_Node *owner);
internal String8 uishell_store_slot_title(String8 key);
internal void uishell_store_show_slot(CFG_Node *owner, RD_PanelID panel, String8 key);

#endif // UISHELL_WORKSPACE_STORE_H
