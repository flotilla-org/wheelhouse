// Workspace IDs (shell_workspace_id.h).

typedef struct UIShell_WorkspaceIdSlot UIShell_WorkspaceIdSlot;
struct UIShell_WorkspaceIdSlot
{
  UIShell_WorkspaceIdSlot *next;
  UIShell_WorkspaceId id;
  CFG_ID cfg_id;
};

// ID -> CFG_ID. CFG_ID -> ID reads the node itself. An entry is replaced when
// its workspace is next read from a different node, and goes stale (its
// CFG_ID resolving to nil) when the node is released.
global Arena *uishell_workspace_index_arena = 0;
global UIShell_WorkspaceIdSlot *uishell_workspace_index_slots[256];
global U64 uishell_workspace_id_last_ms = 0;

internal UIShell_WorkspaceId
uishell_workspace_id_make(void)
{
  // UUIDv7: 48 bits of Unix milliseconds, then random bits. The clock here
  // has whole seconds; ordering within a process is kept by advancing past
  // the last ID's time (RFC 9562, 6.2).
  U64 ms = (U64)now_time_unix()*1000;
  if(ms <= uishell_workspace_id_last_ms) { ms = uishell_workspace_id_last_ms+1; }
  uishell_workspace_id_last_ms = ms;
  Guid random = make_guid();
  UIShell_WorkspaceId id = {0};
  MemoryCopy(id.v, random.v, sizeof(id.v));
  for(U64 i = 0; i < 6; i++) { id.v[i] = (U8)(ms >> (8*(5-i))); }
  id.v[6] = (U8)(0x70 | (id.v[6] & 0x0f));
  id.v[8] = (U8)(0x80 | (id.v[8] & 0x3f));
  return id;
}

internal B32
uishell_workspace_id_match(UIShell_WorkspaceId a, UIShell_WorkspaceId b)
{
  return MemoryMatch(a.v, b.v, sizeof(a.v));
}

internal B32
uishell_workspace_id_is_zero(UIShell_WorkspaceId id)
{
  UIShell_WorkspaceId zero = {0};
  return uishell_workspace_id_match(id, zero);
}

internal String8
uishell_string_from_workspace_id(Arena *arena, UIShell_WorkspaceId id)
{
  U8 *v = id.v;
  return push_str8f(arena, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
    v[0], v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[8], v[9], v[10], v[11], v[12], v[13], v[14], v[15]);
}

internal B32
uishell_workspace_id_from_string(String8 string, UIShell_WorkspaceId *out)
{
  B32 hyphenated = string.size == 36;
  if(!hyphenated && string.size != 32) { return 0; }
  UIShell_WorkspaceId id = {0};
  U64 digits = 0;
  for(U64 i = 0; i < string.size; i++)
  {
    U8 c = string.str[i];
    if(hyphenated && (i == 8 || i == 13 || i == 18 || i == 23))
    {
      if(c != '-') { return 0; }
      continue;
    }
    U8 value = (c >= '0' && c <= '9') ? c-'0' : (c >= 'a' && c <= 'f') ? c-'a'+10 : (c >= 'A' && c <= 'F') ? c-'A'+10 : 0xff;
    if(value == 0xff) { return 0; }
    id.v[digits/2] |= (digits%2) ? value : (U8)(value << 4);
    digits++;
  }
  *out = id;
  return 1;
}

internal UIShell_WorkspaceIdSlot **
uishell_workspace_index_bucket(UIShell_WorkspaceId id)
{
  U64 hash = 5381;
  for(U64 i = 0; i < sizeof(id.v); i++) { hash = hash*33 + id.v[i]; }
  return &uishell_workspace_index_slots[hash % ArrayCount(uishell_workspace_index_slots)];
}

internal void
uishell_workspace_index_insert(UIShell_WorkspaceId id, CFG_ID cfg_id)
{
  UIShell_WorkspaceIdSlot **bucket = uishell_workspace_index_bucket(id);
  for(UIShell_WorkspaceIdSlot *s = *bucket; s; s = s->next)
  {
    if(uishell_workspace_id_match(s->id, id)) { s->cfg_id = cfg_id; return; }
  }
  if(!uishell_workspace_index_arena) { uishell_workspace_index_arena = arena_alloc(); }
  UIShell_WorkspaceIdSlot *slot = push_array(uishell_workspace_index_arena, UIShell_WorkspaceIdSlot, 1);
  slot->id = id;
  slot->cfg_id = cfg_id;
  SLLStackPush(*bucket, slot);
}

internal CFG_ID
uishell_workspace_cfg_id_from_id(UIShell_WorkspaceId id)
{
  if(uishell_workspace_id_is_zero(id)) { return 0; }
  for(UIShell_WorkspaceIdSlot *s = *uishell_workspace_index_bucket(id); s; s = s->next)
  {
    if(uishell_workspace_id_match(s->id, id)) { return s->cfg_id; }
  }
  return 0;
}

// Replaces each node under `root` whose string is `from` with `to`.
internal void
uishell_workspace_id_rewrite(CFG_Node *root, String8 from, String8 to)
{
  for(CFG_Node *n = root->first; n != &cfg_nil_node; n = n->next)
  {
    if(str8_match(n->string, from, 0)) { cfg_node_equip_string(rd_state->cfg, n, to); }
    uishell_workspace_id_rewrite(n, from, to);
  }
}

internal UIShell_WorkspaceId
uishell_workspace_id_from_cfg(CFG_Node *workspace)
{
  UIShell_WorkspaceId id = {0};
  if(workspace == &cfg_nil_node) { return id; }
  CFG_Node *saved = cfg_node_child_from_string(workspace, str8_lit("workspace_id"));
  if(!uishell_workspace_id_from_string(saved->first->string, &id))
  {
    Temp scratch = scratch_begin(0, 0);
    id = uishell_workspace_id_make();
    String8 text = uishell_string_from_workspace_id(scratch.arena, id);
    saved = cfg_node_child_from_string_or_alloc(rd_state->cfg, workspace, str8_lit("workspace_id"));
    cfg_node_new_replace(rd_state->cfg, saved, text);
    // A local workspace saved before Workspace IDs was its sidebar entity by a
    // GUID of its own (`local_entity`), which its pins and saved row orders
    // name. The ID takes its place there.
    CFG_Node *legacy = cfg_node_child_from_string(workspace, str8_lit("local_entity"));
    if(legacy != &cfg_nil_node)
    {
      String8 old = push_str8_copy(scratch.arena, legacy->first->string);
      CFG_Node *window = rd_window_from_cfg(workspace);
      if(old.size) { uishell_workspace_id_rewrite(window != &cfg_nil_node ? window : workspace, old, text); }
      cfg_node_release(rd_state->cfg, legacy);
    }
    scratch_end(scratch);
  }
  uishell_workspace_index_insert(id, workspace->id);
  return id;
}

internal String8
uishell_workspace_id_text_from_cfg(CFG_Node *workspace)
{
  uishell_workspace_id_from_cfg(workspace);
  return cfg_node_child_from_string(workspace, str8_lit("workspace_id"))->first->string;
}
