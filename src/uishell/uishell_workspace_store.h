// Workspace arrangements and Slots, kept by Andamento (uishell_workspace_store.c).
#ifndef UISHELL_WORKSPACE_STORE_H
#define UISHELL_WORKSPACE_STORE_H

// `window` as this device saves it in the Dashboard's presentation file: each
// workspace whose arrangement Andamento keeps loses its panel tree and keeps
// only what is this device's (uishell_workspace_store.c, "Device-local").
internal String8 uishell_workspace_store_window_text(Arena *arena, String8 root_path, CFG_Node *window);

#endif // UISHELL_WORKSPACE_STORE_H
