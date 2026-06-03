// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Generated Metadata

#include "dbg_engine/generated/dbg_engine.meta.c"

////////////////////////////////
//~ rjf: Debug Engine Shell Stubs

#ifndef DBG_ENGINE_CTRL_H
read_only global D_Entity d_entity_nil =
{
  &d_entity_nil,
  &d_entity_nil,
  &d_entity_nil,
  &d_entity_nil,
  &d_entity_nil,
};
#define d_entity_list_first(list) ((list)->first ? (list)->first->v : &d_entity_nil)
#define d_entity_array_first(array) ((array)->count != 0 ? (array)->v[0] : &d_entity_nil)
#endif

internal U64
d_hash_from_seed_string(U64 seed, String8 string)
{
  U64 result = seed;
  for(U64 idx = 0; idx < string.size; idx += 1)
  {
    result = ((result << 5) + result) + string.str[idx];
  }
  return result;
}

internal U64
d_hash_from_string(String8 string)
{
  U64 result = d_hash_from_seed_string(5381, string);
  return result;
}

internal U64
d_hash_from_seed_string__case_insensitive(U64 seed, String8 string)
{
  U64 result = seed;
  for(U64 idx = 0; idx < string.size; idx += 1)
  {
    result = ((result << 5) + result) + lower_from_char(string.str[idx]);
  }
  return result;
}

internal U64
d_hash_from_string__case_insensitive(String8 string)
{
  U64 result = d_hash_from_seed_string__case_insensitive(5381, string);
  return result;
}

internal B32
d_handle_match(D_Handle a, D_Handle b)
{
  B32 result = (a.machine_id == b.machine_id &&
                a.controller_kind == b.controller_kind &&
                a.entity_id == b.entity_id);
  return result;
}

internal D_Handle
d_handle_from_string(String8 string)
{
  D_Handle result = {0};
  Temp scratch = scratch_begin(0, 0);
  String8List parts = str8_split_by_string_chars(scratch.arena, string, str8_lit("."), 0);
  if(parts.node_count >= 3)
  {
    U64 machine_id = 0;
    U64 controller_kind = 0;
    U64 entity_id = 0;
    try_u64_from_str8_c_rules(parts.first->string, &machine_id);
    try_u64_from_str8_c_rules(parts.first->next->string, &controller_kind);
    try_u64_from_str8_c_rules(parts.first->next->next->string, &entity_id);
    result.machine_id = (D_MachineID)machine_id;
    result.controller_kind = (D_ControllerKind)controller_kind;
    result.entity_id = entity_id;
  }
  scratch_end(scratch);
  return result;
}

internal String8
d_string_from_handle(Arena *arena, D_Handle handle)
{
  String8 result = push_str8f(arena, "%u.%u.%I64u", handle.machine_id, handle.controller_kind, handle.entity_id);
  return result;
}

internal void
d_entity_list_push(Arena *arena, D_EntityList *list, D_Entity *entity)
{
  D_EntityNode *node = push_array(arena, D_EntityNode, 1);
  SLLQueuePush(list->first, list->last, node);
  list->count += 1;
  node->v = entity;
}

internal D_EntityArray
d_entity_array_from_list(Arena *arena, D_EntityList *list)
{
  D_EntityArray result = {0};
  result.count = list->count;
  result.v = push_array(arena, D_Entity *, result.count);
  U64 idx = 0;
  for(D_EntityNode *n = list->first; n != 0; n = n->next, idx += 1)
  {
    result.v[idx] = n->v;
  }
  return result;
}

internal D_Entity *
d_entity_from_handle(D_Handle handle)
{
  return &d_entity_nil;
}

internal D_Entity *
d_entity_child_from_kind(D_Entity *parent, D_EntityKind kind)
{
  return &d_entity_nil;
}

internal D_Entity *
d_entity_ancestor_from_kind(D_Entity *entity, D_EntityKind kind)
{
  return &d_entity_nil;
}

internal D_Entity *
d_process_from_entity(D_Entity *entity)
{
  return &d_entity_nil;
}

internal DI_Key
d_dbgi_key_from_module(D_Entity *module)
{
  DI_Key result = {0};
  return result;
}

internal B32
d_entity_tree_is_frozen(D_Entity *root)
{
  return 0;
}

internal D_EntityArray
d_entity_array_from_kind(D_EntityKind kind)
{
  D_EntityArray result = {0};
  return result;
}

internal D_EntityKind
d_entity_kind_from_string(String8 string)
{
  D_EntityKind result = D_EntityKind_Null;
  for EachNonZeroEnumVal(D_EntityKind, k)
  {
    if(str8_match(d_entity_kind_code_name_table[k], string, 0))
    {
      result = k;
      break;
    }
  }
  return result;
}

internal U64 d_run_gen(void) { return 0; }
internal U64 d_mem_gen(void) { return 0; }

internal B32
d_process_memory_read(D_Handle process, Rng1U64 range, B32 *is_stale_out, void *out, U64 endt_us)
{
  if(is_stale_out != 0) { *is_stale_out = 0; }
  return 0;
}

////////////////////////////////
//~ rjf: Debug Info Shell Stubs

internal DI_Key
di_key_zero(void)
{
  DI_Key result = {0};
  return result;
}

internal B32
di_key_match(DI_Key a, DI_Key b)
{
  B32 result = (a.u64[0] == b.u64[0] && a.u64[1] == b.u64[1]);
  return result;
}

internal void
di_key_list_push(Arena *arena, DI_KeyList *list, DI_Key key)
{
  DI_KeyNode *node = push_array(arena, DI_KeyNode, 1);
  SLLQueuePush(list->first, list->last, node);
  list->count += 1;
  node->v = key;
}

internal DI_KeyArray
di_key_array_from_list(Arena *arena, DI_KeyList *list)
{
  DI_KeyArray result = {0};
  result.count = list->count;
  result.v = push_array(arena, DI_Key, result.count);
  U64 idx = 0;
  for(DI_KeyNode *n = list->first; n != 0; n = n->next, idx += 1)
  {
    result.v[idx] = n->v;
  }
  return result;
}

internal void
di_init(CmdLine *cmdline)
{
}

internal DI_Key
di_key_from_path_timestamp(String8 path, U64 min_timestamp)
{
  DI_Key result = {0};
  result.u64[0] = d_hash_from_string(path);
  result.u64[1] = min_timestamp;
  return result;
}

internal void di_open(DI_Key key) {}
internal void di_close(DI_Key key, B32 force_closed) {}
internal U64 di_load_gen(void) { return 0; }
internal U64 di_load_count(void) { return 0; }

internal DI_KeyArray
di_push_all_loaded_keys(Arena *arena)
{
  DI_KeyArray result = {0};
  return result;
}

internal RDI_Parsed *
di_rdi_from_key(Access *access, DI_Key key, B32 high_priority, U64 endt_us)
{
  return &rdi_parsed_nil;
}

internal DI_EventList
di_get_events(Arena *arena)
{
  DI_EventList result = {0};
  return result;
}

internal void di_async_tick(void) {}
internal void di_signal_completion(void) {}
internal void di_conversion_completion_signal_receiver_thread_entry_point(void *p) {}

internal AC_Artifact
di_search_artifact_create(String8 key, B32 *cancel_signal, B32 *retry_out, U64 *gen_out)
{
  AC_Artifact result = {0};
  return result;
}

internal void
di_search_artifact_destroy(AC_Artifact artifact)
{
}

internal AC_Artifact
di_match_artifact_create(String8 key, B32 *cancel_signal, B32 *retry_out, U64 *gen_out)
{
  AC_Artifact result = {0};
  return result;
}

internal DI_Match
di_match_from_string(String8 string, U64 match_index, DI_Key preferred_dbgi_key, U64 endt_us)
{
  DI_Match result = {0};
  return result;
}
