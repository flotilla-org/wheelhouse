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
  // A shell line; argv from another host is joined with spaces.
  String8 command;
  B32 has_cwd;
  String8 cwd, path, url, launcher, endpoint, presentation;
};

// What a Renderer is told about `view`'s Slot: its address, its View Spec
// (zero for a placeholder, whose Slot keeps the spec it was made from),
// status and Target Resolution. A View outside a workspace's arrangement is
// not a Slot, and gets only its resolution.
internal void uishell_store_view_slot(Arena *arena, CFG_Node *view, WH_SlotAddress *slot, UIShell_ViewSpec const **spec,
                                      WH_SlotStatus *status, WH_TargetResolution *resolution);

#endif // UISHELL_WORKSPACE_STORE_H
