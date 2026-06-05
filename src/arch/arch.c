// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Abstracted Architecture Functions

internal ARCH_Info *
arch_info_from_arch(Arch arch)
{
  ARCH_Info *result = &arch_info_nil;
  switch(arch)
  {
    default:{}break;
  }
  return result;
}

internal ARCH_RegCode
arch_reg_code_from_name(ARCH_Info *arch_info, String8 name)
{
  ARCH_RegCode result = 0;
  for EachIndex(code, arch_info->reg_code_count)
  {
    if(str8_match(arch_info->reg_code_name_table[code], name, 0))
    {
      result = code;
      break;
    }
  }
  return result;
}

internal U64
arch_trap_instruction_size_from_code(Arch arch, String8 code)
{
  U64 result = 0;
  switch(arch)
  {
    case Arch_x64:
    case Arch_x86:
    {
      if(code.size >= 1 && code.str[0] == 0xcc)
      {
        result = 1;
      }
    }break;
    case Arch_arm64:
    {
      if(code.size >= 4)
      {
        U32 inst = ((U32)code.str[0] <<  0 |
                    (U32)code.str[1] <<  8 |
                    (U32)code.str[2] << 16 |
                    (U32)code.str[3] << 24);
        if((inst & 0xffe0001fu) == 0xd4200000u)
        {
          result = 4;
        }
      }
    }break;
    default:{}break;
  }
  return result;
}

internal U64
arch_software_breakpoint_pc_offset(OperatingSystem os, Arch arch)
{
  ARCH_Info *arch_info = arch_info_from_arch(arch);
  U64 result = 0;
  switch(arch)
  {
    case Arch_x64:
    case Arch_x86:
    {
      result = arch_info->trap_instruction.size;
    }break;
    case Arch_arm64:
    case Arch_arm32:
    {
      switch(os)
      {
        case OperatingSystem_Windows:
        {
          result = arch_info->trap_instruction.size;
        }break;
        case OperatingSystem_Linux:
        case OperatingSystem_Mac:
        case OperatingSystem_Null:
        default:
        {
          result = 0;
        }break;
      }
    }break;
    default: break;
  }
  return result;
}

internal B32
arch_reg_block_read_range(ARCH_Info *arch_info, void *block, Rng1U16 range, void *dst)
{
  B32 result = 0;
  {
    Rng1U16 legal_range = r1u16(0, arch_info->reg_block_size);
    Rng1U16 try_range = range;
    Rng1U16 read_range = intersect_1u16(legal_range, try_range);
    U64 read_size = dim_1u16(read_range);
    if(read_size != 0)
    {
      MemoryCopy(dst, (U8 *)block + read_range.min, read_size);
      result = 1;
    }
  }
  return result;
}

internal B32
arch_reg_block_write_range(ARCH_Info *arch_info, void *block, Rng1U16 range, void *src)
{
  B32 result = 0;
  Rng1U16 legal_range = r1u16(0, arch_info->reg_block_size);
  Rng1U16 try_range = range;
  Rng1U16 write_range = intersect_1u16(legal_range, try_range);
  U64 write_size = dim_1u16(write_range);
  if(write_size != 0)
  {
    MemoryCopy((U8 *)block + write_range.min, src, write_size);
    result = 1;
  }
  return result;
}

internal U64
arch_ip_from_reg_block(ARCH_Info *arch_info, void *block)
{
  U64 result = 0;
  ARCH_RegCode reg_code = arch_info->instruction_pointer_reg_code;
  if(reg_code < arch_info->reg_code_count)
  {
    arch_reg_block_read_range(arch_info, block, arch_info->reg_code_rng_table[reg_code], &result);
  }
  return result;
}

internal U64
arch_sp_from_reg_block(ARCH_Info *arch_info, void *block)
{
  U64 result = 0;
  ARCH_RegCode reg_code = arch_info->stack_pointer_reg_code;
  if(reg_code < arch_info->reg_code_count)
  {
    arch_reg_block_read_range(arch_info, block, arch_info->reg_code_rng_table[reg_code], &result);
  }
  return result;
}

internal B32
arch_reg_block_write_ip(ARCH_Info *arch_info, void *block, U64 ip)
{
  B32 result = 0;
  ARCH_RegCode reg_code = arch_info->instruction_pointer_reg_code;
  if(reg_code < arch_info->reg_code_count)
  {
    result = arch_reg_block_write_range(arch_info, block, arch_info->reg_code_rng_table[reg_code], &ip);
  }
  return result;
}

internal B32
arch_reg_block_write_sp(ARCH_Info *arch_info, void *block, U64 sp)
{
  B32 result = 0;
  ARCH_RegCode reg_code = arch_info->stack_pointer_reg_code;
  if(reg_code < arch_info->reg_code_count)
  {
    result = arch_reg_block_write_range(arch_info, block, arch_info->reg_code_rng_table[reg_code], &sp);
  }
  return result;
}
