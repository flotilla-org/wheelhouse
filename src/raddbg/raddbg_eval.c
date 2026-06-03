// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: `commands` Type Hooks

E_TYPE_ACCESS_FUNCTION_DEF(commands)
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

E_TYPE_EXPAND_INFO_FUNCTION_DEF(commands)
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

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(commands)
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

////////////////////////////////
//~ rjf: `themes` Type Hooks

E_TYPE_ACCESS_FUNCTION_DEF(themes)
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

E_TYPE_EXPAND_INFO_FUNCTION_DEF(themes)
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

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(themes)
{
  U64 out_idx = 0;
  String8Array *accel = (String8Array *)user_data;
  for(U64 idx = idx_range.min; idx < idx_range.max; idx += 1, out_idx += 1)
  {
    String8 name = accel->v[idx];
    evals_out[out_idx] = e_eval_wrapf(eval, "$[\"%S\"]", name);
  }
}

////////////////////////////////
//~ rjf: `views` Type Hooks

E_TYPE_ACCESS_FUNCTION_DEF(views)
{
  E_IRTreeAndType result = {&e_irnode_nil};
  if(expr->kind == E_ExprKind_ArrayIndex &&
     expr->first->next->kind == E_ExprKind_LeafStringLiteral)
  {
    Temp scratch = scratch_begin(&arena, 1);
    String8 view_name = expr->first->next->string;
    E_TypeKey view_type = e_type_key_cons(.kind = E_TypeKind_U64, .name = str8_lit("view"));
    result.type_key = view_type;
    result.mode = E_Mode_Null;
    result.root = e_irtree_set_space(arena, e_space_make(RD_EvalSpaceKind_MetaView), e_irtree_const_u(arena, e_id_from_string(view_name)));
    scratch_end(scratch);
  }
  return result;
}

E_TYPE_EXPAND_INFO_FUNCTION_DEF(views)
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

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(views)
{
  U64 out_idx = 0;
  String8Array *accel = (String8Array *)user_data;
  for(U64 idx = idx_range.min; idx < idx_range.max; idx += 1, out_idx += 1)
  {
    String8 name = accel->v[idx];
    evals_out[out_idx] = e_eval_from_string(name);
  }
}

////////////////////////////////
//~ rjf: Schema Type Hooks

typedef struct RD_SchemaIRExt RD_SchemaIRExt;
struct RD_SchemaIRExt
{
  CFG_Node *cfg;
  MD_NodePtrList schemas;
};

E_TYPE_IREXT_FUNCTION_DEF(schema)
{
  RD_SchemaIRExt *ext = push_array(arena, RD_SchemaIRExt, 1);
  {
    Temp scratch = scratch_begin(&arena, 1);
    E_OpList oplist = e_oplist_from_irtree(scratch.arena, irtree->root);
    String8 bytecode = e_bytecode_from_oplist(scratch.arena, &oplist);
    E_Interpretation interpret = e_interpret(bytecode);
    E_TypeKey type_key = irtree->type_key;
    E_Type *type = e_type_from_key(type_key);
    ext->cfg = rd_cfg_from_eval_space(interpret.space);
    ext->schemas = cfg_schemas_from_name(arena, rd_state->cfg_schema_table, type->name);
    scratch_end(scratch);
  }
  E_IRExt result = {ext};
  return result;
}

E_TYPE_ACCESS_FUNCTION_DEF(schema)
{
  RD_SchemaIRExt *ext = (RD_SchemaIRExt *)lhs_irtree->user_data;
  E_IRTreeAndType irtree = {&e_irnode_nil};
  if(expr->kind == E_ExprKind_MemberAccess)
  {
    MD_Node *child_schema = &md_nil_node;
    for(MD_NodePtrNode *n = ext->schemas.first; n != 0; n = n->next)
    {
      for MD_EachNode(child, n->v->first)
      {
        if(str8_match(child->string, expr->first->next->string, 0))
        {
          child_schema = child;
          break;
        }
      }
    }
    if(child_schema != &md_nil_node)
    {
      CFG_Node *cfg = ext->cfg;
      CFG_Node *child = cfg_node_child_from_string(cfg, child_schema->string);
      E_TypeKey child_type_key = zero_struct;
      B32 wrap_child_w_meta_expr = 0;
      B32 is_query_child = md_node_has_tag(child_schema, str8_lit("query"), 0);
      E_TypeFlags type_flags = (!!is_query_child * E_TypeFlag_IsNotEditable);
      if(0){}
      
      //- rjf: cfg members
      else if(str8_match(child_schema->first->string, str8_lit("code_string"), 0) ||
              str8_match(child_schema->first->string, str8_lit("expr_string"), 0))
      {
        child_type_key = e_type_key_cons_array(e_type_key_basic(E_TypeKind_U8), child->first->string.size, type_flags|E_TypeFlag_IsCodeText);
      }
      else if(str8_match(child_schema->first->string, str8_lit("path"), 0) ||
              str8_match(child_schema->first->string, str8_lit("path_pt"), 0))
      {
        child_type_key = e_type_key_cons_array(e_type_key_basic(E_TypeKind_U8), child->first->string.size, type_flags|E_TypeFlag_IsPathText);
      }
      
      else if(str8_match(child_schema->first->string, str8_lit("string"), 0))
      {
        child_type_key = e_type_key_cons_array(e_type_key_basic(E_TypeKind_U8), child->first->string.size, type_flags|E_TypeFlag_IsPlainText);
      }
      
      //- rjf: catchall cases
      else if(str8_match(child_schema->first->string, str8_lit("u64"), 0))
      {
        child_type_key = e_type_key_basic(E_TypeKind_U64);
        wrap_child_w_meta_expr = 1;
      }
      else if(str8_match(child_schema->first->string, str8_lit("u32"), 0))
      {
        child_type_key = e_type_key_basic(E_TypeKind_U32);
        wrap_child_w_meta_expr = 1;
      }
      else if(str8_match(child_schema->first->string, str8_lit("f32"), 0))
      {
        child_type_key = e_type_key_basic(E_TypeKind_F32);
        wrap_child_w_meta_expr = 1;
      }
      else if(str8_match(child_schema->first->string, str8_lit("bool"), 0))
      {
        child_type_key = e_type_key_basic(E_TypeKind_Bool);
        wrap_child_w_meta_expr = 1;
      }
      else if(str8_match(child_schema->first->string, str8_lit("vaddr_range"), 0))
      {
        Temp scratch = scratch_begin(&arena, 1);
        E_MemberList vaddr_range_members_list = {0};
        e_member_list_push_new(scratch.arena, &vaddr_range_members_list, .type_key = e_type_key_basic(E_TypeKind_U64), .name = str8_lit("min"), .off = 0);
        e_member_list_push_new(scratch.arena, &vaddr_range_members_list, .type_key = e_type_key_basic(E_TypeKind_U64), .name = str8_lit("max"), .off = 8);
        E_MemberArray vaddr_range_members = e_member_array_from_list(scratch.arena, &vaddr_range_members_list);
        child_type_key = e_type_key_cons(.kind = E_TypeKind_Struct, .name = str8_lit("vaddr_range"), .count = vaddr_range_members.count, .members = vaddr_range_members.v);
        scratch_end(scratch);
      }
      else if(str8_match(child_schema->first->string, str8_lit("set"), 0))
      {
        child_type_key = e_string2typekey_map_lookup(rd_state->meta_name2type_map, child_schema->string);
      }
      
      //- rjf: extend child type with meta-expression information
      if(wrap_child_w_meta_expr)
      {
        Temp scratch = scratch_begin(&arena, 1);
        E_Expr *expr = e_parse_from_string(child->first->string).expr;
        B32 expr_is_simple = 0;
        if(expr->kind == E_ExprKind_LeafU64 ||
           expr->kind == E_ExprKind_LeafF64 ||
           expr->kind == E_ExprKind_LeafF32)
        {
          expr_is_simple = 1;
        }
        if((expr->kind == E_ExprKind_Pos || expr->kind == E_ExprKind_Neg) &&
           expr->first == expr->last &&
           (expr->first->kind == E_ExprKind_LeafU64 ||
            expr->first->kind == E_ExprKind_LeafF64 ||
            expr->first->kind == E_ExprKind_LeafF32))
        {
          expr_is_simple = 1;
        }
        if(expr->kind == E_ExprKind_LeafIdentifier &&
           (str8_match(expr->string, str8_lit("true"), 0) ||
            str8_match(expr->string, str8_lit("false"), 0)))
        {
          expr_is_simple = 1;
        }
        if(!expr_is_simple && expr != &e_expr_nil)
        {
          child_type_key = e_type_key_cons_meta_expr(child_type_key, child->first->string);
        }
        scratch_end(scratch);
      }
      
      //- rjf: extend child type with decorative meta info
      {
        MD_Node *display_name = md_tag_from_string(child_schema, str8_lit("display_name"), 0);
        MD_Node *description = md_tag_from_string(child_schema, str8_lit("description"), 0);
        if(!md_node_is_nil(display_name))
        {
          child_type_key = e_type_key_cons_meta_display_name(child_type_key, display_name->first->string);
        }
        if(!md_node_is_nil(description))
        {
          child_type_key = e_type_key_cons_meta_description(child_type_key, description->first->string);
        }
      }
      
      //- rjf: extend child type with hex lens
      {
        MD_Node *hex = md_tag_from_string(child_schema->first, str8_lit("hex"), 0);
        if(!md_node_is_nil(hex))
        {
          child_type_key = e_type_key_cons(.kind = E_TypeKind_Lens,
                                           .name = str8_lit("hex"),
                                           .direct_key = child_type_key);
        }
      }
      
      //- rjf: extend child type with color lens
      {
        MD_Node *color = md_tag_from_string(child_schema->first, str8_lit("color"), 0);
        if(!md_node_is_nil(color))
        {
          child_type_key = e_type_key_cons(.kind = E_TypeKind_Lens,
                                           .name = str8_lit("color"),
                                           .direct_key = child_type_key);
        }
      }
      
      //- rjf: extend child type with ranges
      {
        MD_Node *range = md_tag_from_string(child_schema->first, str8_lit("range"), 0);
        if(!md_node_is_nil(range))
        {
          E_Expr *min_bound = e_parse_from_string(range->first->string).expr;
          E_Expr *max_bound = e_parse_from_string(range->first->next->string).expr;
          E_Expr *args[] =
          {
            min_bound,
            max_bound,
          };
          child_type_key = e_type_key_cons(.kind = E_TypeKind_Lens,
                                           .name = str8_lit("range1"),
                                           .direct_key = child_type_key,
                                           .count = 2,
                                           .args = args);
        }
      }
      
      //- rjf: evaluate
      E_Space child_eval_space = zero_struct;
      if(cfg != &cfg_nil_node)
      {
        child_eval_space = e_space_make(RD_EvalSpaceKind_MetaCfg);
        child_eval_space.u64s[0] = cfg->id;
        child_eval_space.u64s[1] = e_id_from_string(child_schema->string);
      }
      irtree.root     = e_irtree_set_space(arena, child_eval_space, e_push_irnode(arena, RDI_EvalOp_ConstU64));
      irtree.type_key = child_type_key;
      irtree.mode     = E_Mode_Offset;
    }
  }
  return irtree;
}

E_TYPE_EXPAND_INFO_FUNCTION_DEF(schema)
{
  E_TypeExpandInfo result = {0};
  {
    // rjf: unpack
    RD_SchemaIRExt *ext = (RD_SchemaIRExt *)eval.irtree.user_data;

    // rjf: build accelerator for lookups
    UIShell_EvalSchemaChildren *accel = push_array(arena, UIShell_EvalSchemaChildren, 1);
    *accel = uishell_eval_schema_children_from_cfg_and_schemas(arena, ext->cfg, ext->schemas, eval.key, filter);
    
    // rjf: fill result
    result.user_data = accel;
    result.expr_count = accel->children_count + accel->commands.count;
  }
  return result;
}

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(schema)
{
  UIShell_EvalSchemaChildren *accel = (UIShell_EvalSchemaChildren *)user_data;
  Rng1U64 cmds_idx_range = r1u64(0, accel->commands.count);
  Rng1U64 chld_idx_range = r1u64(cmds_idx_range.max, cmds_idx_range.max + accel->children_count);
  U64 out_idx = 0;
  
  // rjf: read commands
  {
    Rng1U64 read_range = intersect_1u64(idx_range, cmds_idx_range);
    for(U64 idx = read_range.min; idx < read_range.max; idx += 1, out_idx += 1)
    {
      evals_out[out_idx] = e_eval_from_stringf("query:commands.%S", accel->commands.v[idx - cmds_idx_range.min]);
    }
  }
  
  // rjf: read children
  {
    Rng1U64 read_range = intersect_1u64(idx_range, chld_idx_range);
    for(U64 idx = read_range.min; idx < read_range.max; idx += 1, out_idx += 1)
    {
      MD_Node *child_schema = accel->children[idx - chld_idx_range.min];
      evals_out[out_idx] = e_eval_wrapf(eval, "$.%S", child_schema->string);
    }
  }
}

////////////////////////////////
//~ rjf: Config Type Hooks

E_TYPE_ACCESS_FUNCTION_DEF(cfgs)
{
  E_IRTreeAndType result = {&e_irnode_nil};
  E_Expr *rhs = expr->first->next;
  if(rhs->kind == E_ExprKind_LeafIdentifier &&
     str8_match(str8_prefix(rhs->string, 1), str8_lit("$"), 0))
  {
    String8 numeric_part = str8_skip(rhs->string, 1);
    CFG_ID id = u64_from_str8(numeric_part, 16);
    CFG_Node *cfg = cfg_node_from_id(id);
    E_Space space = rd_eval_space_from_cfg(cfg);
    result.root = e_irtree_set_space(arena, space, e_irtree_const_u(arena, 0));
    result.type_key = e_string2typekey_map_lookup(rd_state->meta_name2type_map, cfg->string);
    result.mode = E_Mode_Offset;
  }
  return result;
}

////////////////////////////////
//~ rjf: Config Collection Type Hooks

typedef struct RD_CfgsIRExt RD_CfgsIRExt;
struct RD_CfgsIRExt
{
  String8 cfg_name;
  String8Array cmds;
  CFG_NodePtrArray cfgs;
  Rng1U64 cmds_idx_range;
  Rng1U64 cfgs_idx_range;
};

E_TYPE_IREXT_FUNCTION_DEF(cfgs_slice)
{
  RD_CfgsIRExt *ext = push_array(arena, RD_CfgsIRExt, 1);
  {
    //- rjf: determine which key we'll be gathering
    E_TypeKey type_key = irtree->type_key;
    E_Type *type = e_type_from_key(type_key);
    String8 cfg_name = rd_singular_from_code_name_plural(type->name);
    UIShell_EvalCfgChildren children = uishell_eval_cfg_children_from_name(arena, cfg_name);
    
    //- rjf: package & fill
    ext->cfg_name = cfg_name;
    ext->cfgs = children.cfgs;
    ext->cmds = children.cmds;
  }
  E_IRExt result = {ext};
  return result;
}

E_TYPE_ACCESS_FUNCTION_DEF(cfgs_slice)
{
  E_IRTreeAndType result = {&e_irnode_nil};
  CFG_Node *cfg = &cfg_nil_node;
  RD_CfgsIRExt *ext = (RD_CfgsIRExt *)lhs_irtree->user_data;
  switch(expr->kind)
  {
    default:{}break;
    case E_ExprKind_ArrayIndex:
    {
      E_Value rhs_value = e_value_from_expr(expr->first->next);
      U64 rhs_idx = rhs_value.u64;
      if(0 <= rhs_idx && rhs_idx < ext->cfgs.count)
      {
        cfg = ext->cfgs.v[rhs_idx];
      }
    }break;
    case E_ExprKind_MemberAccess:
    {
      String8 rhs_name = expr->first->next->string;
      CFG_ID id = 0;
      if(str8_match(str8_prefix(rhs_name, 1), str8_lit("$"), 0))
      {
        id = u64_from_str8(str8_skip(rhs_name, 1), 16);
        cfg = cfg_node_from_id(id);
      }
    }break;
  }
  if(cfg != &cfg_nil_node)
  {
    result.root = e_irtree_set_space(arena, rd_eval_space_from_cfg(cfg), e_irtree_const_u(arena, 0));
    result.mode = E_Mode_Offset;
    result.type_key = e_string2typekey_map_lookup(rd_state->meta_name2type_map, ext->cfg_name);
  }
  return result;
}

typedef struct RD_CfgsExpandAccel RD_CfgsExpandAccel;
struct RD_CfgsExpandAccel
{
  String8Array cmds;
  CFG_NodePtrArray cfgs;
  Rng1U64 cmds_idx_range;
  Rng1U64 cfgs_idx_range;
};

E_TYPE_EXPAND_INFO_FUNCTION_DEF(cfgs_slice)
{
  RD_CfgsExpandAccel *accel = push_array(arena, RD_CfgsExpandAccel, 1);
  E_TypeExpandInfo info = {accel};
  {
    //- rjf: unpack
    RD_CfgsIRExt *ext = (RD_CfgsIRExt *)eval.irtree.user_data;
    
    //- rjf: filter cfgs
    CFG_NodePtrArray cfgs__filtered = uishell_eval_cfg_array_from_filter(arena, ext->cfgs, filter);
    
    //- rjf: fill
    // TODO(rjf): @cleanup don't smuggle this through like this...
    if(cfg_node_child_from_string(cfg_node_from_id(rd_regs()->view), str8_lit("lister")) == &cfg_nil_node)
    {
      accel->cmds = ext->cmds;
      accel->cmds_idx_range = r1u64(0, accel->cmds.count);
    }
    accel->cfgs = cfgs__filtered;
    accel->cfgs_idx_range = r1u64(accel->cmds_idx_range.max, accel->cmds_idx_range.max + accel->cfgs.count);
    info.expr_count = (accel->cmds.count + accel->cfgs.count);
  }
  return info;
}

E_TYPE_EXPAND_INFO_FUNCTION_DEF(cfgs_query)
{
  RD_CfgsExpandAccel *accel = push_array(arena, RD_CfgsExpandAccel, 1);
  {
    CFG_Node *root_cfg = rd_cfg_from_eval_space(eval.space);
    String8 child_key = e_string_from_id(eval.space.u64s[1]);
    UIShell_EvalCfgChildren children = uishell_eval_cfg_children_from_parent(arena, root_cfg, child_key, filter);
    accel->cmds = children.cmds;
    accel->cmds_idx_range = r1u64(0, accel->cmds.count);
    accel->cfgs = children.cfgs;
    accel->cfgs_idx_range = r1u64(accel->cmds.count + 0, accel->cmds.count + accel->cfgs.count);
  }
  E_TypeExpandInfo info = {accel, accel->cfgs.count + accel->cmds.count};
  return info;
}

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(cfgs_slice)
{
  RD_CfgsExpandAccel *accel = (RD_CfgsExpandAccel *)user_data;
  Rng1U64 cmds_idx_range = accel->cmds_idx_range;
  Rng1U64 cfgs_idx_range = accel->cfgs_idx_range;
  U64 dst_idx = 0;
  
  // rjf: fill commands
  {
    Rng1U64 read_range = intersect_1u64(cmds_idx_range, idx_range);
    U64 read_count = dim_1u64(read_range);
    E_Eval cmds_eval = e_eval_from_stringf("query:commands");
    for(U64 idx = 0; idx < read_count; idx += 1, dst_idx += 1)
    {
      String8 cmd_name = accel->cmds.v[idx + read_range.min - cmds_idx_range.min];
      E_Eval cmd_eval = e_eval_wrapf(cmds_eval, "$.%S", cmd_name);
      evals_out[dst_idx] = cmd_eval;
    }
  }
  
  // rjf: fill cfgs
  {
    Rng1U64 read_range = intersect_1u64(cfgs_idx_range, idx_range);
    U64 read_count = dim_1u64(read_range);
    for(U64 idx = 0; idx < read_count; idx += 1, dst_idx += 1)
    {
      CFG_Node *cfg = accel->cfgs.v[idx + read_range.min - cfgs_idx_range.min];
      evals_out[dst_idx] = e_eval_from_stringf("query:config.$%I64x", cfg->id);
    }
  }
}

E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_DEF(cfgs_slice)
{
  U64 id = 0;
  RD_CfgsExpandAccel *accel = (RD_CfgsExpandAccel *)user_data;
  if(num != 0)
  {
    U64 idx = num-1;
    if(contains_1u64(accel->cfgs_idx_range, idx))
    {
      CFG_Node *cfg = accel->cfgs.v[idx - accel->cfgs_idx_range.min];
      id = cfg->id;
    }
    else if(contains_1u64(accel->cmds_idx_range, idx))
    {
      id = num;
      id |= (1ull<<63);
    }
  }
  return id;
}

E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_DEF(cfgs_slice)
{
  U64 num = 0;
  RD_CfgsExpandAccel *accel = (RD_CfgsExpandAccel *)user_data;
  if(id != 0)
  {
    if(id & (1ull<<63))
    {
      num = id;
      num &= ~(1ull<<63);
    }
    else for EachIndex(idx, accel->cfgs.count)
    {
      if(accel->cfgs.v[idx]->id == id)
      {
        num = idx + accel->cfgs_idx_range.min + 1;
        break;
      }
    }
  }
  return num;
}

////////////////////////////////
//~ rjf: `environment` Type Hooks

typedef struct RD_EnvironmentAccel RD_EnvironmentAccel;
struct RD_EnvironmentAccel
{
  CFG_NodePtrArray cfgs;
};

E_TYPE_IREXT_FUNCTION_DEF(environment)
{
  RD_EnvironmentAccel *accel = push_array(arena, RD_EnvironmentAccel, 1);
  {
    Temp scratch = scratch_begin(&arena, 1);
    E_OpList oplist = e_oplist_from_irtree(scratch.arena, irtree->root);
    String8 bytecode = e_bytecode_from_oplist(scratch.arena, &oplist);
    E_Interpretation interpret = e_interpret(bytecode);
    E_Space space = interpret.space;
    CFG_Node *target = rd_cfg_from_eval_space(space);
    CFG_NodePtrList env_strings = {0};
    for(CFG_Node *child = target->first; child != &cfg_nil_node; child = child->next)
    {
      if(str8_match(child->string, str8_lit("environment"), 0))
      {
        cfg_node_ptr_list_push(scratch.arena, &env_strings, child);
      }
    }
    accel->cfgs = cfg_node_ptr_array_from_list(arena, &env_strings);
    scratch_end(scratch);
  }
  E_IRExt result = {accel};
  return result;
}

E_TYPE_ACCESS_FUNCTION_DEF(environment)
{
  E_IRTreeAndType result = {&e_irnode_nil};
  if(expr->kind == E_ExprKind_ArrayIndex)
  {
    RD_EnvironmentAccel *accel = (RD_EnvironmentAccel *)lhs_irtree->user_data;
    CFG_NodePtrArray *cfgs = &accel->cfgs;
    E_Value rhs_value = e_value_from_expr(expr->first->next);
    if(0 <= rhs_value.u64 && rhs_value.u64 < cfgs->count)
    {
      CFG_Node *cfg = cfgs->v[rhs_value.u64];
      result.root      = e_irtree_set_space(arena, rd_eval_space_from_cfg(cfg), e_irtree_const_u(arena, 0));
      result.type_key  = e_type_key_cons_array(e_type_key_basic(E_TypeKind_U8), cfg->first->string.size, E_TypeFlag_IsCodeText);
      result.mode      = E_Mode_Offset;
    }
  }
  return result;
}

E_TYPE_EXPAND_INFO_FUNCTION_DEF(environment)
{
  RD_EnvironmentAccel *accel = (RD_EnvironmentAccel *)eval.irtree.user_data;
  E_TypeExpandInfo result = {accel, accel->cfgs.count + 1};
  return result;
}

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(environment)
{
  RD_EnvironmentAccel *accel = (RD_EnvironmentAccel *)user_data;
  Rng1U64 legal_idx_range = r1u64(0, accel->cfgs.count);
  Rng1U64 read_range = intersect_1u64(idx_range, legal_idx_range);
  U64 read_range_count = dim_1u64(read_range);
  for(U64 idx = 0; idx < read_range_count; idx += 1)
  {
    U64 cfg_idx = read_range.min + idx;
    if(cfg_idx < accel->cfgs.count)
    {
      evals_out[idx] = e_eval_wrapf(eval, "$[%I64u]", cfg_idx);
    }
  }
}

E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_DEF(environment)
{
  U64 id = 0;
  RD_EnvironmentAccel *accel = (RD_EnvironmentAccel *)user_data;
  if(1 <= num && num <= accel->cfgs.count)
  {
    U64 idx = (num-1);
    id = accel->cfgs.v[idx]->id;
  }
  else if(num == accel->cfgs.count+1)
  {
    id = max_U64;
  }
  return id;
}

E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_DEF(environment)
{
  U64 num = 0;
  RD_EnvironmentAccel *accel = (RD_EnvironmentAccel *)user_data;
  if(id != 0 && id != max_U64)
  {
    for EachIndex(idx, accel->cfgs.count)
    {
      if(accel->cfgs.v[idx]->id == id)
      {
        num = idx+1;
        break;
      }
    }
  }
  else if(id == max_U64)
  {
    num = accel->cfgs.count + 1;
  }
  return num;
}

////////////////////////////////
//~ rjf: `watches` Type Hooks

typedef struct RD_WatchesAccel RD_WatchesAccel;
struct RD_WatchesAccel
{
  CFG_NodePtrArray cfgs;
};

E_TYPE_IREXT_FUNCTION_DEF(watches)
{
  RD_WatchesAccel *accel = push_array(arena, RD_WatchesAccel, 1);
  {
    Temp scratch = scratch_begin(&arena, 1);
    E_OpList oplist = e_oplist_from_irtree(scratch.arena, irtree->root);
    String8 bytecode = e_bytecode_from_oplist(scratch.arena, &oplist);
    E_Interpretation interpret = e_interpret(bytecode);
    E_Space space = interpret.space;
    CFG_Node *target = rd_cfg_from_eval_space(space);
    CFG_NodePtrList cfgs = {0};
    for(CFG_Node *child = target->first; child != &cfg_nil_node; child = child->next)
    {
      if(rd_cfg_is_project_filtered(child)) {continue;}
      if(str8_match(child->string, str8_lit("watch"), 0))
      {
        cfg_node_ptr_list_push(scratch.arena, &cfgs, child);
      }
    }
    accel->cfgs = cfg_node_ptr_array_from_list(arena, &cfgs);
    scratch_end(scratch);
  }
  E_IRExt result = {accel};
  return result;
}

E_TYPE_ACCESS_FUNCTION_DEF(watches)
{
  E_IRTreeAndType result = {&e_irnode_nil};
  if(expr->kind == E_ExprKind_ArrayIndex)
  {
    RD_WatchesAccel *accel = (RD_WatchesAccel *)lhs_irtree->user_data;
    CFG_NodePtrArray *cfgs = &accel->cfgs;
    E_Value rhs_value = e_value_from_expr(expr->first->next);
    if(0 <= rhs_value.u64 && rhs_value.u64 < cfgs->count)
    {
      CFG_Node *cfg = cfgs->v[rhs_value.u64];
      result.root      = e_irtree_set_space(arena, rd_eval_space_from_cfg(cfg), e_irtree_const_u(arena, 0));
      result.type_key  = e_type_key_cons_array(e_type_key_basic(E_TypeKind_U8), cfg->first->string.size, E_TypeFlag_IsCodeText);
      result.mode      = E_Mode_Offset;
    }
  }
  return result;
}

E_TYPE_EXPAND_INFO_FUNCTION_DEF(watches)
{
  RD_WatchesAccel *ext = (RD_WatchesAccel *)eval.irtree.user_data;
  RD_WatchesAccel *accel = push_array(arena, RD_WatchesAccel, 1);
  {
    CFG_NodePtrArray cfgs__filtered = ext->cfgs;
    if(filter.size != 0)
    {
      Temp scratch = scratch_begin(&arena, 1);
      CFG_NodePtrList cfgs_list__filtered = {0};
      for EachIndex(idx, ext->cfgs.count)
      {
        CFG_Node *watch = ext->cfgs.v[idx];
        String8 string = watch->first->string;
        FuzzyMatchRangeList matches = fuzzy_match_find(scratch.arena, filter, string);
        if(matches.count == matches.needle_part_count)
        {
          cfg_node_ptr_list_push(scratch.arena, &cfgs_list__filtered, watch);
        }
      }
      cfgs__filtered = cfg_node_ptr_array_from_list(arena, &cfgs_list__filtered);
      scratch_end(scratch);
    }
    accel->cfgs = cfgs__filtered;
  }
  E_TypeExpandInfo result = {accel, accel->cfgs.count + 1};
  return result;
}

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(watches)
{
  RD_WatchesAccel *accel = (RD_WatchesAccel *)user_data;
  Rng1U64 legal_idx_range = r1u64(0, accel->cfgs.count);
  Rng1U64 read_range = intersect_1u64(idx_range, legal_idx_range);
  U64 read_range_count = dim_1u64(read_range);
  for(U64 idx = 0; idx < read_range_count; idx += 1)
  {
    U64 cfg_idx = read_range.min + idx;
    if(cfg_idx < accel->cfgs.count)
    {
      CFG_Node *cfg = accel->cfgs.v[cfg_idx];
      evals_out[idx] = e_eval_from_string(cfg->first->string);
    }
  }
}

E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_DEF(watches)
{
  U64 id = 0;
  RD_WatchesAccel *accel = (RD_WatchesAccel *)user_data;
  if(1 <= num && num <= accel->cfgs.count)
  {
    U64 idx = (num-1);
    id = accel->cfgs.v[idx]->id;
  }
  else if(num == accel->cfgs.count+1)
  {
    id = max_U64;
  }
  return id;
}

E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_DEF(watches)
{
  U64 num = 0;
  RD_WatchesAccel *accel = (RD_WatchesAccel *)user_data;
  if(id != 0 && id != max_U64)
  {
    for EachIndex(idx, accel->cfgs.count)
    {
      if(accel->cfgs.v[idx]->id == id)
      {
        num = idx+1;
        break;
      }
    }
  }
  else if(id == max_U64)
  {
    num = accel->cfgs.count + 1;
  }
  return num;
}

////////////////////////////////
//~ rjf: `peek_types` Type Hooks

typedef struct RD_PeekTypesAccel RD_PeekTypesAccel;
struct RD_PeekTypesAccel
{
  CFG_NodePtrArray cfgs;
};

E_TYPE_IREXT_FUNCTION_DEF(peek_types)
{
  RD_PeekTypesAccel *accel = push_array(arena, RD_PeekTypesAccel, 1);
  {
    Temp scratch = scratch_begin(&arena, 1);
    E_OpList oplist = e_oplist_from_irtree(scratch.arena, irtree->root);
    String8 bytecode = e_bytecode_from_oplist(scratch.arena, &oplist);
    E_Interpretation interpret = e_interpret(bytecode);
    E_Space space = interpret.space;
    CFG_Node *target = rd_cfg_from_eval_space(space);
    CFG_NodePtrList env_strings = {0};
    for(CFG_Node *child = target->first; child != &cfg_nil_node; child = child->next)
    {
      if(str8_match(child->string, str8_lit("peek_type"), 0))
      {
        cfg_node_ptr_list_push(scratch.arena, &env_strings, child);
      }
    }
    accel->cfgs = cfg_node_ptr_array_from_list(arena, &env_strings);
    scratch_end(scratch);
  }
  E_IRExt result = {accel};
  return result;
}

E_TYPE_ACCESS_FUNCTION_DEF(peek_types)
{
  E_IRTreeAndType result = {&e_irnode_nil};
  if(expr->kind == E_ExprKind_ArrayIndex)
  {
    RD_PeekTypesAccel *accel = (RD_PeekTypesAccel *)lhs_irtree->user_data;
    CFG_NodePtrArray *cfgs = &accel->cfgs;
    E_Value rhs_value = e_value_from_expr(expr->first->next);
    if(0 <= rhs_value.u64 && rhs_value.u64 < cfgs->count)
    {
      CFG_Node *cfg = cfgs->v[rhs_value.u64];
      result.root      = e_irtree_set_space(arena, rd_eval_space_from_cfg(cfg), e_irtree_const_u(arena, 0));
      result.type_key  = e_type_key_cons_array(e_type_key_basic(E_TypeKind_U8), cfg->first->string.size, E_TypeFlag_IsCodeText);
      result.mode      = E_Mode_Offset;
    }
  }
  return result;
}

E_TYPE_EXPAND_INFO_FUNCTION_DEF(peek_types)
{
  RD_PeekTypesAccel *accel = (RD_PeekTypesAccel *)eval.irtree.user_data;
  E_TypeExpandInfo result = {accel, accel->cfgs.count + 1};
  return result;
}

E_TYPE_EXPAND_RANGE_FUNCTION_DEF(peek_types)
{
  RD_PeekTypesAccel *accel = (RD_PeekTypesAccel *)user_data;
  Rng1U64 legal_idx_range = r1u64(0, accel->cfgs.count);
  Rng1U64 read_range = intersect_1u64(idx_range, legal_idx_range);
  U64 read_range_count = dim_1u64(read_range);
  for(U64 idx = 0; idx < read_range_count; idx += 1)
  {
    U64 cfg_idx = read_range.min + idx;
    if(cfg_idx < accel->cfgs.count)
    {
      evals_out[idx] = e_eval_wrapf(eval, "$[%I64u]", cfg_idx);
    }
  }
}

E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_DEF(peek_types)
{
  U64 id = 0;
  RD_PeekTypesAccel *accel = (RD_PeekTypesAccel *)user_data;
  if(1 <= num && num <= accel->cfgs.count)
  {
    U64 idx = (num-1);
    id = accel->cfgs.v[idx]->id;
  }
  else if(num == accel->cfgs.count+1)
  {
    id = max_U64;
  }
  return id;
}

E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_DEF(peek_types)
{
  U64 num = 0;
  RD_PeekTypesAccel *accel = (RD_PeekTypesAccel *)user_data;
  if(id != 0 && id != max_U64)
  {
    for EachIndex(idx, accel->cfgs.count)
    {
      if(accel->cfgs.v[idx]->id == id)
      {
        num = idx+1;
        break;
      }
    }
  }
  else if(id == max_U64)
  {
    num = accel->cfgs.count + 1;
  }
  return num;
}
