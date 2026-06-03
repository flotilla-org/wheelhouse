// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Shell Eval Provider Helpers

E_TYPE_ACCESS_FUNCTION_DEF(uishell_commands)
{
  E_IRTreeAndType result = {&e_irnode_nil};
  if(expr->kind == E_ExprKind_MemberAccess)
  {
    String8 cmd_name = expr->first->next->string;
    UIShell_CmdInfo *cmd_info = uishell_cmd_info_from_name(cmd_name);
    E_TypeKey cmd_type = e_type_key_cons(.kind = E_TypeKind_U64, .name = str8_lit("command"));
    cmd_type = e_type_key_cons_meta_description(cmd_type, cmd_info->description);
    result.type_key = cmd_type;
    result.mode = E_Mode_Value;
    result.root = e_irtree_set_space(arena, e_space_make(RD_EvalSpaceKind_MetaCmd), e_irtree_const_u(arena, e_id_from_string(cmd_name)));
  }
  return result;
}

E_TYPE_EXPAND_INFO_FUNCTION_DEF(uishell_commands)
{
  E_TypeExpandInfo result = {0};
  {
    E_Type *type = e_type_from_key(eval.irtree.type_key);
    RD_CmdKindFlags required_flags = RD_CmdKindFlag_ListInUI;
    if(str8_match(type->name, str8_lit("text_pt_commands"), 0))
    {
      required_flags |= RD_CmdKindFlag_ListInTextPt;
    }
    if(str8_match(type->name, str8_lit("text_range_commands"), 0))
    {
      required_flags |= RD_CmdKindFlag_ListInTextRng;
    }
    if(str8_match(type->name, str8_lit("tab_commands"), 0))
    {
      required_flags |= RD_CmdKindFlag_ListInTab;
    }
    UIShell_EvalContext ctx = {.required_cmd_flags = required_flags};
    UIShell_EvalProvider *provider = uishell_eval_provider_from_namespace(str8_lit("query:commands"));
    String8Array *accel = push_array(arena, String8Array, 1);
    *accel = provider->children(arena, &ctx, filter);
    result.user_data = accel;
    result.expr_count = accel->count;
  }
  return result;
}

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(uishell_commands)
{
  U64 out_idx = 0;
  String8Array *accel = (String8Array *)user_data;
  for(U64 idx = idx_range.min; idx < idx_range.max; idx += 1, out_idx += 1)
  {
    String8 cmd_name = accel->v[idx];
    E_Eval cmd_eval = e_eval_from_stringf("query:commands.%S", cmd_name);
    evals_out[out_idx] = cmd_eval;
  }
}

E_TYPE_ACCESS_FUNCTION_DEF(uishell_themes)
{
  E_IRTreeAndType result = {&e_irnode_nil};
  if(expr->kind == E_ExprKind_ArrayIndex &&
     expr->first->next->kind == E_ExprKind_LeafStringLiteral)
  {
    String8 theme_name = expr->first->next->string;
    E_TypeKey theme_type = e_type_key_cons(.kind = E_TypeKind_U64, .name = str8_lit("theme"));
    result.type_key = theme_type;
    result.mode = E_Mode_Value;
    result.root = e_irtree_set_space(arena, e_space_make(RD_EvalSpaceKind_MetaTheme), e_irtree_const_u(arena, e_id_from_string(theme_name)));
  }
  return result;
}

E_TYPE_EXPAND_INFO_FUNCTION_DEF(uishell_themes)
{
  E_TypeExpandInfo result = {0};
  {
    UIShell_EvalProvider *provider = uishell_eval_provider_from_namespace(str8_lit("query:themes"));
    String8Array *accel = push_array(arena, String8Array, 1);
    *accel = provider->children(arena, 0, filter);
    result.user_data = accel;
    result.expr_count = accel->count;
  }
  return result;
}

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(uishell_themes)
{
  U64 out_idx = 0;
  String8Array *accel = (String8Array *)user_data;
  for(U64 idx = idx_range.min; idx < idx_range.max; idx += 1, out_idx += 1)
  {
    String8 name = accel->v[idx];
    evals_out[out_idx] = e_eval_wrapf(eval, "$[\"%S\"]", name);
  }
}

E_TYPE_ACCESS_FUNCTION_DEF(uishell_views)
{
  E_IRTreeAndType result = {&e_irnode_nil};
  if(expr->kind == E_ExprKind_ArrayIndex &&
     expr->first->next->kind == E_ExprKind_LeafStringLiteral)
  {
    String8 view_name = expr->first->next->string;
    E_TypeKey view_type = e_type_key_cons(.kind = E_TypeKind_U64, .name = str8_lit("view"));
    result.type_key = view_type;
    result.mode = E_Mode_Null;
    result.root = e_irtree_set_space(arena, e_space_make(RD_EvalSpaceKind_MetaView), e_irtree_const_u(arena, e_id_from_string(view_name)));
  }
  return result;
}

E_TYPE_EXPAND_INFO_FUNCTION_DEF(uishell_views)
{
  E_TypeExpandInfo result = {0};
  {
    UIShell_EvalProvider *provider = uishell_eval_provider_from_namespace(str8_lit("query:views"));
    String8Array *accel = push_array(arena, String8Array, 1);
    *accel = provider->children(arena, 0, filter);
    result.user_data = accel;
    result.expr_count = accel->count;
  }
  return result;
}

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(uishell_views)
{
  U64 out_idx = 0;
  String8Array *accel = (String8Array *)user_data;
  for(U64 idx = idx_range.min; idx < idx_range.max; idx += 1, out_idx += 1)
  {
    String8 name = accel->v[idx];
    evals_out[out_idx] = e_eval_from_string(name);
  }
}

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
uishell_eval_theme_names_from_filter(Arena *arena, String8 filter)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List names = {0};

  //- rjf: gather presets
  for EachEnumVal(RD_ThemePreset, p)
  {
    String8 name = rd_theme_preset_display_string_table[p];
    FuzzyMatchRangeList name_matches = fuzzy_match_find(scratch.arena, filter, name);
    if(name_matches.count == name_matches.needle_part_count)
    {
      str8_list_push(scratch.arena, &names, name);
    }
  }

  //- rjf: gather theme files
  {
    String8 theme_folder = push_str8f(scratch.arena, "%S/themes", rd_app_data_folder(scratch.arena));
    FileIter *it = file_iter_begin(scratch.arena, theme_folder, FileIterFlag_SkipFolders);
    for(FileInfo info = {0}; file_iter_next(scratch.arena, it, &info);)
    {
      String8 name = info.name;
      FuzzyMatchRangeList name_matches = fuzzy_match_find(scratch.arena, filter, name);
      if(name_matches.count == name_matches.needle_part_count)
      {
        str8_list_push(scratch.arena, &names, str8_copy(arena, name));
      }
    }
    file_iter_end(it);
  }

  String8Array result = str8_array_from_list(arena, &names);
  scratch_end(scratch);
  return result;
}

internal UIShell_EvalSchemaChildren
uishell_eval_schema_children_from_cfg_and_schemas(Arena *arena, CFG_Node *cfg, MD_NodePtrList schemas, E_Key parent_key, String8 filter)
{
  Temp scratch = scratch_begin(&arena, 1);
  UIShell_EvalSchemaChildren result = {0};

  // rjf: gather expansion commands
  {
    String8List commands = {0};
    for(MD_NodePtrNode *n = schemas.first; n != 0; n = n->next)
    {
      MD_Node *schema = n->v;
      MD_Node *tag = md_tag_from_string(schema, str8_lit("expand_commands"), 0);
      for MD_EachNode(arg, tag->first)
      {
        B32 filtered = 0;
        if(md_node_has_tag(arg, str8_lit("output"), 0))
        {
          String8 expr = rd_expr_from_cfg(cfg);
          filtered = (!str8_match(expr, str8_lit("query:output"), 0));
        }
        if(!filtered)
        {
          RD_AppCmdInfo cmd_info = rd_app_cmd_info_from_string(arg->string);
          FuzzyMatchRangeList name_matches = fuzzy_match_find(scratch.arena, filter, rd_display_from_code_name(cmd_info.string));
          FuzzyMatchRangeList desc_matches = fuzzy_match_find(scratch.arena, filter, cmd_info.description);
          FuzzyMatchRangeList tags_matches = fuzzy_match_find(scratch.arena, filter, cmd_info.search_tags);
          if(name_matches.count == name_matches.needle_part_count ||
             desc_matches.count == desc_matches.needle_part_count ||
             tags_matches.count == tags_matches.needle_part_count)
          {
            str8_list_push(scratch.arena, &commands, arg->string);
          }
        }
      }
    }
    result.commands = str8_array_from_list(arena, &commands);
  }

  // rjf: gather expansion children
  typedef struct UIShell_EvalSchemaChildNode UIShell_EvalSchemaChildNode;
  struct UIShell_EvalSchemaChildNode
  {
    UIShell_EvalSchemaChildNode *next;
    MD_Node *n;
  };
  UIShell_EvalSchemaChildNode *first_child_node = 0;
  UIShell_EvalSchemaChildNode *last_child_node = 0;
  U64 child_count = 0;
  for(MD_NodePtrNode *n = schemas.first; n != 0; n = n->next)
  {
    MD_Node *schema = n->v;
    for MD_EachNode(child, schema->first)
    {
      if(!md_node_has_tag(child, str8_lit("no_expand"), 0))
      {
        MD_Node *expand_check = md_tag_from_string(child, str8_lit("expand_if"), 0);
        B32 expand_this_child = 1;
        if(!md_node_is_nil(expand_check)) E_ParentKey(parent_key)
        {
          expand_this_child = !!e_value_from_string(expand_check->first->string).u64;
        }
        if(expand_this_child)
        {
          String8 display_name = md_tag_from_string(child, str8_lit("display_name"), 0)->first->string;
          if(display_name.size == 0)
          {
            display_name = rd_display_from_code_name(child->string);
          }
          String8 desc = md_tag_from_string(child, str8_lit("description"), 0)->first->string;
          FuzzyMatchRangeList name_matches         = fuzzy_match_find(scratch.arena, filter, child->string);
          FuzzyMatchRangeList display_name_matches = fuzzy_match_find(scratch.arena, filter, display_name);
          FuzzyMatchRangeList desc_matches         = fuzzy_match_find(scratch.arena, filter, desc);
          if(name_matches.count == name_matches.needle_part_count ||
             display_name_matches.count == display_name_matches.needle_part_count ||
             desc_matches.count == desc_matches.needle_part_count)
          {
            UIShell_EvalSchemaChildNode *child_node = push_array(scratch.arena, UIShell_EvalSchemaChildNode, 1);
            child_node->n = child;
            SLLQueuePush(first_child_node, last_child_node, child_node);
            child_count += 1;
          }
        }
      }
    }
  }

  // rjf: flatten expansion member list
  result.children = push_array(arena, MD_Node *, child_count);
  result.children_count = child_count;
  {
    U64 idx = 0;
    for(UIShell_EvalSchemaChildNode *n = first_child_node; n != 0; n = n->next, idx += 1)
    {
      result.children[idx] = n->n;
    }
  }

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

internal void
uishell_eval_register_query_macros(Arena *arena, Arena *type_arena, E_String2ExprMap *macro_map, E_String2TypeKeyMap *type_map)
{
  //- rjf: add macros for command groups
  {
    String8 names[] =
    {
      str8_lit("commands"),
      str8_lit("tab_commands"),
      str8_lit("text_pt_commands"),
      str8_lit("text_range_commands"),
    };
    for EachElement(idx, names)
    {
      String8 name = names[idx];
      E_TypeKey type_key = e_type_key_cons(.kind = E_TypeKind_Set,
                                           .flags = E_TypeFlag_StubSingleLineExpansion,
                                           .name = name,
                                           .access = E_TYPE_ACCESS_FUNCTION_NAME(uishell_commands),
                                           .expand =
                                           {
                                             .info  = E_TYPE_EXPAND_INFO_FUNCTION_NAME(uishell_commands),
                                             .range = E_TYPE_EXPAND_RANGE_FUNCTION_NAME(uishell_commands),
                                           });
      E_Expr *expr = e_push_expr(arena, E_ExprKind_LeafOffset, r1u64(0, 0));
      expr->type_key = type_key;
      expr->space = e_space_make(RD_EvalSpaceKind_MetaQuery);
      e_string2expr_map_insert(arena, macro_map, name, expr);
    }
  }

  //- rjf: add macro for themes
  {
    String8 names[] =
    {
      str8_lit("themes"),
    };
    for EachElement(idx, names)
    {
      String8 name = names[idx];
      E_TypeKey type_key = e_type_key_cons(.kind = E_TypeKind_Set,
                                           .flags = E_TypeFlag_StubSingleLineExpansion,
                                           .name = name,
                                           .access = E_TYPE_ACCESS_FUNCTION_NAME(uishell_themes),
                                           .expand =
                                           {
                                             .info  = E_TYPE_EXPAND_INFO_FUNCTION_NAME(uishell_themes),
                                             .range = E_TYPE_EXPAND_RANGE_FUNCTION_NAME(uishell_themes),
                                           });
      E_Expr *expr = e_push_expr(arena, E_ExprKind_LeafOffset, r1u64(0, 0));
      expr->type_key = type_key;
      expr->space = e_space_make(RD_EvalSpaceKind_MetaQuery);
      e_string2expr_map_insert(arena, macro_map, name, expr);
    }
  }

  //- rjf: add macro for views
  {
    String8 names[] =
    {
      str8_lit("views"),
    };
    for EachElement(idx, names)
    {
      String8 name = names[idx];
      E_TypeKey type_key = e_type_key_cons(.kind = E_TypeKind_Set,
                                           .flags = E_TypeFlag_StubSingleLineExpansion,
                                           .name = name,
                                           .access = E_TYPE_ACCESS_FUNCTION_NAME(uishell_views),
                                           .expand =
                                           {
                                             .info  = E_TYPE_EXPAND_INFO_FUNCTION_NAME(uishell_views),
                                             .range = E_TYPE_EXPAND_RANGE_FUNCTION_NAME(uishell_views),
                                           });
      E_Expr *expr = e_push_expr(arena, E_ExprKind_LeafOffset, r1u64(0, 0));
      expr->type_key = type_key;
      expr->space = e_space_make(RD_EvalSpaceKind_MetaQuery);
      e_string2expr_map_insert(arena, macro_map, name, expr);
    }
  }

  //- rjf: build schema types & cache (name -> type) mapping
  for EachElement(idx, uishell_name_schema_info_table)
  {
    String8 name = uishell_name_schema_info_table[idx].name;
    E_TypeKey type_key = e_type_key_cons(.name = name,
                                         .kind = E_TypeKind_Set,
                                         .irext  = E_TYPE_IREXT_FUNCTION_NAME(schema),
                                         .access = E_TYPE_ACCESS_FUNCTION_NAME(schema),
                                         .expand =
                                         {
                                           .info  = E_TYPE_EXPAND_INFO_FUNCTION_NAME(schema),
                                           .range = E_TYPE_EXPAND_RANGE_FUNCTION_NAME(schema),
                                         });
    e_string2typekey_map_insert(type_arena, type_map, name, type_key);
  }

  //- rjf: add macro for top-level config root
  {
    String8 name = str8_lit("config");
    E_TypeKey type_key = e_type_key_cons(.name = name,
                                         .kind = E_TypeKind_Set,
                                         .access = E_TYPE_ACCESS_FUNCTION_NAME(cfgs));
    E_Expr *expr = e_push_expr(arena, E_ExprKind_LeafOffset, r1u64(0, 0));
    expr->type_key = type_key;
    expr->space = e_space_make(RD_EvalSpaceKind_MetaQuery);
    e_string2expr_map_insert(arena, macro_map, name, expr);
    e_string2typekey_map_insert(type_arena, type_map, name, type_key);
  }

  //- rjf: add macros for shell config collection queries
  String8 evallable_cfg_names[] =
  {
    str8_lit("recent_project"),
  };
  for EachElement(cfg_name_idx, evallable_cfg_names)
  {
    String8 cfg_name = evallable_cfg_names[cfg_name_idx];
    if(!uishell_cfg_schema_name_is_listed(cfg_name))
    {
      continue;
    }
    String8 collection_name = rd_plural_from_code_name(cfg_name);
    E_TypeKey collection_type_key = e_type_key_cons(.kind = E_TypeKind_Set, .name = collection_name,
                                                    .irext = E_TYPE_IREXT_FUNCTION_NAME(cfgs_slice),
                                                    .access = E_TYPE_ACCESS_FUNCTION_NAME(cfgs_slice),
                                                    .expand =
                                                    {
                                                      .info = E_TYPE_EXPAND_INFO_FUNCTION_NAME(cfgs_slice),
                                                      .range= E_TYPE_EXPAND_RANGE_FUNCTION_NAME(cfgs_slice),
                                                      .id_from_num = E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_NAME(cfgs_slice),
                                                      .num_from_id = E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_NAME(cfgs_slice),
                                                    });
    E_Expr *expr = e_push_expr(arena, E_ExprKind_LeafOffset, r1u64(0, 0));
    expr->type_key = collection_type_key;
    expr->space = e_space_make(RD_EvalSpaceKind_MetaQuery);
    e_string2expr_map_insert(arena, macro_map, collection_name, expr);
    e_string2typekey_map_insert(type_arena, type_map, collection_name, collection_type_key);
  }
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

internal String8Array
uishell_eval_themes_provider_children(Arena *arena, UIShell_EvalContext *ctx, String8 filter)
{
  String8Array result = uishell_eval_theme_names_from_filter(arena, filter);
  return result;
}

read_only global UIShell_EvalProvider uishell_eval_provider_nil = {0};
read_only global UIShell_EvalProvider uishell_eval_provider_table[] =
{
  {str8_lit_comp("query:commands"),     0, uishell_eval_commands_provider_children, 0, 0},
  {str8_lit_comp("query:tab_commands"), 0, uishell_eval_commands_provider_children, 0, 0},
  {str8_lit_comp("query:themes"),       0, uishell_eval_themes_provider_children,   0, 0},
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
