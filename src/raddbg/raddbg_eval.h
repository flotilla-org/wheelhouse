// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef RADDBG_EVAL_H
#define RADDBG_EVAL_H

////////////////////////////////
//~ rjf: Schema Type Hooks

E_TYPE_IREXT_FUNCTION_DEF(schema);
E_TYPE_ACCESS_FUNCTION_DEF(schema);
E_TYPE_EXPAND_INFO_FUNCTION_DEF(schema);
E_TYPE_EXPAND_RANGE_FUNCTION_DEF(schema);

////////////////////////////////
//~ rjf: Config Type Hooks

E_TYPE_ACCESS_FUNCTION_DEF(cfgs);

////////////////////////////////
//~ rjf: Config Slice Type Hooks

E_TYPE_IREXT_FUNCTION_DEF(cfgs_slice);
E_TYPE_EXPAND_INFO_FUNCTION_DEF(cfgs_query);
E_TYPE_ACCESS_FUNCTION_DEF(cfgs_slice);
E_TYPE_EXPAND_INFO_FUNCTION_DEF(cfgs_slice);
E_TYPE_EXPAND_RANGE_FUNCTION_DEF(cfgs_slice);
E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_DEF(cfgs_slice);
E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_DEF(cfgs_slice);

////////////////////////////////
//~ rjf: `environment` Type Hooks
//
// Retained for the shell as a generic editable config string-list shape.
// In RAD this was used by debugger targets for process launch environment
// strings. The shell does not currently expose a target schema, but the
// underlying list behavior is useful future scaffolding.

E_TYPE_IREXT_FUNCTION_DEF(environment);
E_TYPE_ACCESS_FUNCTION_DEF(environment);
E_TYPE_EXPAND_INFO_FUNCTION_DEF(environment);
E_TYPE_EXPAND_RANGE_FUNCTION_DEF(environment);
E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_DEF(environment);
E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_DEF(environment);

////////////////////////////////
//~ rjf: `watches` Type Hooks

E_TYPE_IREXT_FUNCTION_DEF(watches);
E_TYPE_ACCESS_FUNCTION_DEF(watches);
E_TYPE_EXPAND_INFO_FUNCTION_DEF(watches);
E_TYPE_EXPAND_RANGE_FUNCTION_DEF(watches);
E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_DEF(watches);
E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_DEF(watches);

////////////////////////////////
//~ rjf: `peek_types` Type Hooks

E_TYPE_IREXT_FUNCTION_DEF(peek_types);
E_TYPE_ACCESS_FUNCTION_DEF(peek_types);
E_TYPE_EXPAND_INFO_FUNCTION_DEF(peek_types);
E_TYPE_EXPAND_RANGE_FUNCTION_DEF(peek_types);
E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_DEF(peek_types);
E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_DEF(peek_types);

#endif // RADDBG_EVAL_H
