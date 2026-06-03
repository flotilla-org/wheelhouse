// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Shell Eval Provider Helpers

internal void
uishell_eval_cmd_names_push_filtered(Arena *arena, String8List *cmd_names, UIShell_CmdInfo *info, RD_CmdKindFlags required_flags, String8 filter)
{
  Temp scratch = scratch_begin(&arena, 1);
  RD_CmdKindFlags info_flags = rd_cmd_flags_from_uishell_cmd_flags(info->flags);
  if((info_flags & required_flags) == required_flags)
  {
    String8 display_name = rd_display_from_code_name(info->string);
    FuzzyMatchRangeList desc_matches = fuzzy_match_find(scratch.arena, filter, info->description);
    FuzzyMatchRangeList name_matches = fuzzy_match_find(scratch.arena, filter, display_name);
    FuzzyMatchRangeList tags_matches = fuzzy_match_find(scratch.arena, filter, info->search_tags);
    B32 binding_matches_good = 0;
    CFG_KeyMapNodePtrList bindings = cfg_key_map_node_ptr_list_from_name(scratch.arena, rd_state->key_map, info->string);
    for(CFG_KeyMapNodePtr *n = bindings.first; n != 0; n = n->next)
    {
      String8 binding_text = wm_string_from_modifiers_key(scratch.arena, n->v->binding.modifiers, n->v->binding.key);
      FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, filter, binding_text);
      if(matches.count == matches.needle_part_count)
      {
        binding_matches_good = 1;
        break;
      }
    }
    if(name_matches.count == name_matches.needle_part_count ||
       desc_matches.count == desc_matches.needle_part_count ||
       tags_matches.count == tags_matches.needle_part_count ||
       binding_matches_good)
    {
      str8_list_push(arena, cmd_names, info->string);
    }
  }
  scratch_end(scratch);
}

internal String8Array
uishell_eval_command_names_from_filter(Arena *arena, RD_CmdKindFlags required_flags, String8 filter)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List cmd_names = {0};
  for EachElement(idx, uishell_cmd_info_table)
  {
    UIShell_CmdInfo *info = &uishell_cmd_info_table[idx];
    uishell_eval_cmd_names_push_filtered(scratch.arena, &cmd_names, info, required_flags, filter);
  }
  String8Array result = str8_array_from_list(arena, &cmd_names);
  scratch_end(scratch);
  return result;
}

internal String8Array
uishell_eval_view_names_from_filter(Arena *arena, String8 filter)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List names = {0};
  for EachElement(idx, uishell_name_schema_info_table)
  {
    if(uishell_name_schema_info_table[idx].is_view &&
       rd_view_name_is_listed_in_app(uishell_name_schema_info_table[idx].name))
    {
      String8 name = uishell_name_schema_info_table[idx].name;
      FuzzyMatchRangeList name_matches = fuzzy_match_find(scratch.arena, filter, name);
      if(name_matches.count == name_matches.needle_part_count)
      {
        str8_list_push(scratch.arena, &names, name);
      }
    }
  }
  String8Array result = str8_array_from_list(arena, &names);
  scratch_end(scratch);
  return result;
}

internal String8Array
uishell_eval_collection_command_names_from_cfg_name(Arena *arena, String8 cfg_name)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List cmds = {0};
  MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, cfg_name);
  for(MD_NodePtrNode *n = schemas.first; n != 0; n = n->next)
  {
    MD_Node *schema = n->v;
    MD_Node *collection_cmds_root = md_tag_from_string(schema, str8_lit("collection_commands"), 0);
    for MD_EachNode(cmd, collection_cmds_root->first)
    {
      str8_list_push(scratch.arena, &cmds, cmd->string);
    }
  }
  String8Array result = str8_array_from_list(arena, &cmds);
  scratch_end(scratch);
  return result;
}

internal CFG_NodePtrArray
uishell_eval_top_level_cfgs_from_name(Arena *arena, String8 cfg_name)
{
  Temp scratch = scratch_begin(&arena, 1);
  CFG_NodePtrList cfgs = {0};
  CFG_NodePtrList all_cfgs = cfg_node_top_level_list_from_string(scratch.arena, cfg_name);
  for EachNode(n, CFG_NodePtrNode, all_cfgs.first)
  {
    if(rd_cfg_is_project_filtered(n->v))
    {
      continue;
    }
    cfg_node_ptr_list_push(scratch.arena, &cfgs, n->v);
  }
  CFG_NodePtrArray result = cfg_node_ptr_array_from_list(arena, &cfgs);
  scratch_end(scratch);
  return result;
}

internal CFG_NodePtrArray
uishell_eval_cfg_array_from_filter(Arena *arena, CFG_NodePtrArray cfgs, String8 filter)
{
  CFG_NodePtrArray result = {0};
  if(filter.size != 0)
  {
    Temp scratch = scratch_begin(&arena, 1);
    CFG_NodePtrList filtered = {0};
    for EachIndex(idx, cfgs.count)
    {
      CFG_Node *cfg = cfgs.v[idx];
      if(rd_cfg_is_project_filtered(cfg))
      {
        continue;
      }
      DR_FStrList fstrs = rd_title_fstrs_from_cfg(scratch.arena, cfg, 1);
      String8 string = dr_string_from_fstrs(scratch.arena, &fstrs);
      FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, filter, string);
      if(matches.count == matches.needle_part_count)
      {
        cfg_node_ptr_list_push(scratch.arena, &filtered, cfg);
      }
    }
    result = cfg_node_ptr_array_from_list(arena, &filtered);
    scratch_end(scratch);
  }
  else
  {
    result.v = push_array(arena, CFG_Node *, cfgs.count);
    result.count = cfgs.count;
    MemoryCopy(result.v, cfgs.v, sizeof(*result.v)*result.count);
  }
  return result;
}

internal UIShell_EvalCfgChildren
uishell_eval_cfg_children_from_name(Arena *arena, String8 cfg_name)
{
  UIShell_EvalCfgChildren result = {0};
  result.cmds = uishell_eval_collection_command_names_from_cfg_name(arena, cfg_name);
  result.cfgs = uishell_eval_top_level_cfgs_from_name(arena, cfg_name);
  return result;
}

internal UIShell_EvalCfgChildren
uishell_eval_cfg_children_from_parent(Arena *arena, CFG_Node *root_cfg, String8 child_key, String8 filter)
{
  Temp scratch = scratch_begin(&arena, 1);
  UIShell_EvalCfgChildren result = {0};
  String8 child_key_singular = rd_singular_from_code_name_plural(child_key);
  if(child_key_singular.size != 0)
  {
    child_key = child_key_singular;
  }
  result.cmds = uishell_eval_collection_command_names_from_cfg_name(arena, child_key);
  CFG_NodePtrList children = cfg_node_child_list_from_string(scratch.arena, root_cfg, child_key);
  CFG_NodePtrArray children_array = cfg_node_ptr_array_from_list(scratch.arena, &children);
  result.cfgs = uishell_eval_cfg_array_from_filter(arena, children_array, filter);
  scratch_end(scratch);
  return result;
}

////////////////////////////////
//~ rjf: Shell Eval Providers

internal String8Array
uishell_eval_commands_provider_children(Arena *arena, UIShell_EvalContext *ctx, String8 filter)
{
  RD_CmdKindFlags required_flags = RD_CmdKindFlag_ListInUI;
  if(ctx != 0 && ctx->required_cmd_flags != 0)
  {
    required_flags = ctx->required_cmd_flags;
  }
  String8Array result = uishell_eval_command_names_from_filter(arena, required_flags, filter);
  return result;
}

internal String8Array
uishell_eval_views_provider_children(Arena *arena, UIShell_EvalContext *ctx, String8 filter)
{
  String8Array result = uishell_eval_view_names_from_filter(arena, filter);
  return result;
}

read_only global UIShell_EvalProvider uishell_eval_provider_nil = {0};
read_only global UIShell_EvalProvider uishell_eval_provider_table[] =
{
  {str8_lit_comp("query:commands"),     0, uishell_eval_commands_provider_children, 0, 0},
  {str8_lit_comp("query:tab_commands"), 0, uishell_eval_commands_provider_children, 0, 0},
  {str8_lit_comp("query:views"),        0, uishell_eval_views_provider_children,    0, 0},
};

internal UIShell_EvalProvider *
uishell_eval_provider_from_namespace(String8 namespace_name)
{
  UIShell_EvalProvider *result = (UIShell_EvalProvider *)&uishell_eval_provider_nil;
  for EachElement(idx, uishell_eval_provider_table)
  {
    if(str8_match(namespace_name, uishell_eval_provider_table[idx].namespace_name, 0))
    {
      result = (UIShell_EvalProvider *)&uishell_eval_provider_table[idx];
      break;
    }
  }
  return result;
}
