// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DBG_ENGINE_USER_H
#define DBG_ENGINE_USER_H

////////////////////////////////
//~ rjf: Basic Helpers

internal U64 d_hash_from_seed_string(U64 seed, String8 string);
internal U64 d_hash_from_string(String8 string);
internal U64 d_hash_from_seed_string__case_insensitive(U64 seed, String8 string);
internal U64 d_hash_from_string__case_insensitive(String8 string);

#endif // DBG_ENGINE_USER_H
