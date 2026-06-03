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
