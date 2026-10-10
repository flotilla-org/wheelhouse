// Workspace IDs (CONTEXT.md, "Workspace ID"; ADR 0012): a UUIDv7 assigned
// once, when a workspace is first saved, and kept on its config node as
// `workspace_id "<uuid>"`. Andamento knows workspaces only by these IDs.
// Docking and rendering still key workspaces by CFG_ID (until state model
// step 7), so an index maps between the two.
#ifndef SHELL_WORKSPACE_ID_H
#define SHELL_WORKSPACE_ID_H

// The 16 bytes in RFC 9562 order: big-endian, as written.
typedef struct UIShell_WorkspaceId UIShell_WorkspaceId;
struct UIShell_WorkspaceId
{
  U8 v[16];
};

internal UIShell_WorkspaceId uishell_workspace_id_make(void);
internal B32 uishell_workspace_id_match(UIShell_WorkspaceId a, UIShell_WorkspaceId b);
internal B32 uishell_workspace_id_is_zero(UIShell_WorkspaceId id);
// Lowercase and hyphenated; parsing also takes uppercase and 32 bare digits.
internal String8 uishell_string_from_workspace_id(Arena *arena, UIShell_WorkspaceId id);
internal B32 uishell_workspace_id_from_string(String8 string, UIShell_WorkspaceId *out);

// A workspace node's ID, assigning and saving one first if it has none: a
// workspace node (`workspace`, `detached_workspace`) or a window whose own
// panels are its workspace. Indexes it either way.
internal UIShell_WorkspaceId uishell_workspace_id_from_cfg(CFG_Node *workspace);
// The saved ID's text, as uishell_workspace_id_from_cfg leaves it; owned by
// the node.
internal String8 uishell_workspace_id_text_from_cfg(CFG_Node *workspace);
// The CFG_ID of the workspace with `id`, or 0 if no indexed node has it.
internal CFG_ID uishell_workspace_cfg_id_from_id(UIShell_WorkspaceId id);
internal void uishell_workspace_index_insert(UIShell_WorkspaceId id, CFG_ID cfg_id);

#endif // SHELL_WORKSPACE_ID_H
