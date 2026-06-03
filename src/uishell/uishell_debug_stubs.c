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
di_init(CmdLine *cmdline)
{
}

internal RDI_Parsed *
di_rdi_from_key(Access *access, DI_Key key, B32 high_priority, U64 endt_us)
{
  return &rdi_parsed_nil;
}

internal void di_async_tick(void) {}

internal DI_Match
di_match_from_string(String8 string, U64 match_index, DI_Key preferred_dbgi_key, U64 endt_us)
{
  DI_Match result = {0};
  return result;
}
