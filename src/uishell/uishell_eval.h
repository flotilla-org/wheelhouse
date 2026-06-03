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

////////////////////////////////
//~ rjf: Shell Query Provider Hooks

internal String8Array uishell_eval_command_names_from_filter(Arena *arena, RD_CmdKindFlags required_flags, String8 filter);
internal String8Array uishell_eval_view_names_from_filter(Arena *arena, String8 filter);
internal UIShell_EvalProvider *uishell_eval_provider_from_namespace(String8 namespace_name);

#endif // UISHELL_EVAL_H
