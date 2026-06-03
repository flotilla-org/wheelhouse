// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

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

////////////////////////////////
//~ rjf: Debug Info Shell Stubs

internal void
di_init(CmdLine *cmdline)
{
}

internal void di_async_tick(void) {}
