// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UISHELL_EVAL_H
#define UISHELL_EVAL_H

////////////////////////////////
//~ rjf: Shell Eval Provider Types

typedef struct UIShell_EvalContext UIShell_EvalContext;
struct UIShell_EvalContext
{
  RD_CmdKindFlags required_cmd_flags;
};

typedef void UIShell_EvalResolveFunc(void);
typedef String8Array UIShell_EvalChildrenFunc(Arena *arena, UIShell_EvalContext *ctx, String8 filter);
typedef void UIShell_EvalReadFunc(void);
typedef void UIShell_EvalWriteFunc(void);

typedef struct UIShell_EvalProvider UIShell_EvalProvider;
struct UIShell_EvalProvider
{
  String8 namespace_name;
  UIShell_EvalResolveFunc *resolve;
  UIShell_EvalChildrenFunc *children;
  UIShell_EvalReadFunc *read;
  UIShell_EvalWriteFunc *write;
};

typedef struct UIShell_EvalCfgChildren UIShell_EvalCfgChildren;
struct UIShell_EvalCfgChildren
{
  String8Array cmds;
  CFG_NodePtrArray cfgs;
};

typedef struct UIShell_EvalSchemaChildren UIShell_EvalSchemaChildren;
struct UIShell_EvalSchemaChildren
{
  String8Array commands;
  MD_Node **children;
  U64 children_count;
};

////////////////////////////////
//~ rjf: Shell Query Provider Hooks

internal String8Array uishell_eval_command_names_from_filter(Arena *arena, RD_CmdKindFlags required_flags, String8 filter);
internal String8Array uishell_eval_view_names_from_filter(Arena *arena, String8 filter);
internal String8Array uishell_eval_theme_names_from_filter(Arena *arena, String8 filter);
internal UIShell_EvalSchemaChildren uishell_eval_schema_children_from_cfg_and_schemas(Arena *arena, CFG_Node *cfg, MD_NodePtrList schemas, E_Key parent_key, String8 filter);
internal UIShell_EvalCfgChildren uishell_eval_cfg_children_from_name(Arena *arena, String8 cfg_name);
internal CFG_NodePtrArray uishell_eval_cfg_array_from_filter(Arena *arena, CFG_NodePtrArray cfgs, String8 filter);
internal UIShell_EvalCfgChildren uishell_eval_cfg_children_from_parent(Arena *arena, CFG_Node *root_cfg, String8 child_key, String8 filter);
internal void uishell_eval_register_query_macros(Arena *arena, Arena *type_arena, E_String2ExprMap *macro_map, E_String2TypeKeyMap *type_map);
internal UIShell_EvalProvider *uishell_eval_provider_from_namespace(String8 namespace_name);

#endif // UISHELL_EVAL_H
