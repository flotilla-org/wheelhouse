// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Context Selection Functions (Selection Required For All Subsequent APIs)

internal void
e_select_interpret_ctx(E_InterpretCtx *ctx)
{
  e_interpret_ctx = ctx;
}

////////////////////////////////
//~ rjf: Space Reading Helpers

internal U64
e_space_gen(E_Space space)
{
  U64 result = 0;
  if(e_base_ctx->space_gen != 0)
  {
    result = e_base_ctx->space_gen(space);
  }
  return result;
}

internal B32
e_space_read(E_Space space, void *out, E_SpaceRangeInfo *out_range_info, Rng1U64 range)
{
  ProfBeginFunction();
  B32 result = 0;
  {
    switch(space.kind)
    {
      //- rjf: reads from hash store key
      case E_SpaceKind_HashStoreKey:
      {
        C_Root root = {space.u64_0};
        C_ID id = {space.u128};
        C_Key key = c_key_make(root, id);
        U128 hash = c_hash_from_key(key, 0);
        Access *access = access_open();
        {
          String8 data = c_data_from_hash(access, hash);
          Rng1U64 legal_range = r1u64(0, data.size);
          Rng1U64 read_range = intersect_1u64(range, legal_range);
          if(read_range.min < read_range.max)
          {
            result = 1;
            MemoryCopy(out, data.str + read_range.min, dim_1u64(read_range));
          }
        }
        access_close(access);
      }break;
      
      //- rjf: file reads
      case E_SpaceKind_File:
      {
        Access *access = access_open();
        
        // rjf: unpack space/path
        U64 file_path_string_id = space.u64_0;
        String8 file_path = e_string_from_id(file_path_string_id);
        
        // rjf: find containing chunk range
        U64 chunk_size = KB(4);
        Rng1U64 containing_range = range;
        containing_range.min -= containing_range.min%chunk_size;
        containing_range.max += chunk_size-1;
        containing_range.max -= containing_range.max%chunk_size;
        
        // rjf: map to hashes
        C_Key key = fs_key_from_path_range(file_path, containing_range, 0);
        U128 hash = {0};
        U128 prev_hash = {0};
        U64 desired_hash_count = 1;
        if(out_range_info != 0 && out_range_info->byte_changed_flags != 0)
        {
          desired_hash_count = 2;
        }
        {
          U64 hashes_count = 0;
          U128 hashes[2] = {0};
          for(U64 rewind_idx = 0; rewind_idx < C_KEY_HASH_HISTORY_COUNT && hashes_count < ArrayCount(hashes) && hashes_count < desired_hash_count; rewind_idx += 1)
          {
            U128 h = c_hash_from_key(key, rewind_idx);
            if(!u128_match(u128_zero(), h))
            {
              hashes[hashes_count] = h;
              hashes_count += 1;
            }
          }
          hash = hashes[0];
          prev_hash = hashes[1];
        }
        
        // rjf: unpack hashes
        String8 data = c_data_from_hash(access, hash);
        String8 prev_data = c_data_from_hash(access, prev_hash);
        
        // rjf: unpack read range
        Rng1U64 legal_range = r1u64(containing_range.min, containing_range.min + data.size);
        Rng1U64 read_range = intersect_1u64(range, legal_range);
        
        // rjf: fill out byte bad flags
        if(out_range_info != 0 && out_range_info->byte_bad_flags != 0)
        {
          // TODO(rjf): need to know whole space range here
        }
        
        // rjf: fill out byte changed flags
        if(out_range_info != 0 && out_range_info->byte_changed_flags != 0)
        {
          U64 num_bytes_read = dim_1u64(read_range);
          if(data.size >= num_bytes_read && data.size == prev_data.size)
          {
            U64 byte_base_idx = read_range.min - containing_range.min;
            for(U64 byte_idx = 0; byte_idx < num_bytes_read; byte_idx += 1)
            {
              if(data.str[byte_base_idx + byte_idx] != prev_data.str[byte_base_idx + byte_idx])
              {
                out_range_info->byte_changed_flags[byte_idx/64] |= (1ull<<(byte_idx%64));
                out_range_info->flags |= E_SpaceRangeFlag_AnyByteChanged;
              }
            }
          }
        }
        
        // rjf: fill output data from data
        if(read_range.min < read_range.max)
        {
          result = 1;
          MemoryCopy(out, data.str + read_range.min - containing_range.min, dim_1u64(read_range));
        }
        
        access_close(access);
      }break;
      
      //- rjf: default -> use hooks
      default:
      if(e_base_ctx->space_read != 0)
      {
        result = e_base_ctx->space_read(space, out, out_range_info, range);
      }break;
    }
  }
  ProfEnd();
  return result;
}

internal B32
e_space_write(E_Space space, void *in, Rng1U64 range)
{
  ProfBeginFunction();
  B32 result = 0;
  if(e_base_ctx->space_write != 0)
  {
    switch(space.kind)
    {
      default:
      {
        result = e_base_ctx->space_write(space, in, range);
      }break;
    }
  }
  ProfEnd();
  return result;
}

////////////////////////////////
//~ rjf: Interpretation Functions

internal E_Interpretation
e_interpret(String8 bytecode)
{
  E_Interpretation result = {0};
  Temp scratch = scratch_begin(0, 0);
  
  //- rjf: allocate stack & "registers"
  U64 stack_cap = 128; // TODO(rjf): scan bytecode; determine maximum stack depth
  E_Value *stack = push_array_no_zero(scratch.arena, E_Value, stack_cap);
  U64 stack_count = 0;
  E_Space selected_space = {0};
  if(bytecode.size != 0)
  {
    selected_space = e_interpret_ctx->primary_space;
  }
  //- rjf: iterate bytecode & perform ops
  U8 *ptr = bytecode.str;
  U8 *opl = bytecode.str + bytecode.size;
  for(;ptr < opl;)
  {
    // rjf: consume next opcode
    E_BytecodeOp op = (E_BytecodeOp)*ptr;
    U16 ctrlbits = 0;
    if(op < E_BytecodeOp_COUNT)
    {
      ctrlbits = e_bytecode_op_ctrlbits_table[op];
    }
    else switch(op)
    {
      case E_IRExtKind_SetSpace:{ctrlbits = E_CTRLBITS(32, 0, 0);}break;
      default:
      {
        result.code = E_InterpretationCode_BadOp;
        goto done;
      }break;
    }
    ptr += 1;
    
    // rjf: decode
    E_Value imm = {0};
    {
      U32 decode_size = E_DECODEN_FROM_CTRLBITS(ctrlbits);
      U8 *next_ptr = ptr + decode_size;
      if(next_ptr > opl)
      {
        result.code = E_InterpretationCode_BadOp;
        goto done;
      }
      // TODO(rjf): guarantee 8 bytes padding after the end of serialized
      // bytecode; read 8 bytes and mask
      MemoryCopy(&imm, ptr, decode_size);
      ptr = next_ptr;
    }
    
    // rjf: unpack imm -> type group & arithmetic width
    E_TypeGroup type_group = (E_TypeGroup)imm.u512.u8[0];
    U64 op_arithmetic_size = (U64)imm.u512.u8[1];
    
    // rjf: pop
    E_Value *svals = 0;
    {
      U32 pop_count = E_POPN_FROM_CTRLBITS(ctrlbits);
      if(pop_count > stack_count)
      {
        result.code = E_InterpretationCode_BadOp;
        goto done;
      }
      if(pop_count <= stack_count)
      {
        stack_count -= pop_count;
        svals = stack + stack_count;
      }
    }
    
    // rjf: interpret op, given decodes/pops
    E_Value nval = {0};
    switch(op)
    {
      case E_IRExtKind_SetSpace:
      {
        MemoryCopy(&selected_space, &imm, sizeof(selected_space));
      }break;

      case E_BytecodeOp_Stop:
      {
        goto done;
      }break;
      
      case E_BytecodeOp_Noop:
      {
        // do nothing
      }break;
      
      case E_BytecodeOp_Cond:
      if(svals[0].u64)
      {
        ptr += imm.u64;
      }break;
      
      case E_BytecodeOp_Skip:
      {
        ptr += imm.u64;
      }break;
      
      case E_BytecodeOp_MemRead:
      {
        U64 addr = svals[0].u64;
        U64 size = imm.u64;
        B32 good_read = e_space_read(selected_space, &nval, 0, r1u64(addr, addr+size));
        if(!good_read)
        {
          result.code = E_InterpretationCode_BadMemRead;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_RegRead:
      case E_BytecodeOp_RegReadDyn:
      {
        result.code = E_InterpretationCode_BadRegRead;
        goto done;
      }break;

      case E_BytecodeOp_FrameOff:
      {
        result.code = E_InterpretationCode_BadFrameBase;
        goto done;
      }break;

      case E_BytecodeOp_ModuleOff:
      case E_BytecodeOp_TLSOff:
      {
        result.code = E_InterpretationCode_BadTLSBase;
        goto done;
      }break;
      
      case E_BytecodeOp_ConstU8:
      case E_BytecodeOp_ConstU16:
      case E_BytecodeOp_ConstU32:
      case E_BytecodeOp_ConstU64:
      case E_BytecodeOp_ConstU128:
      {
        nval = imm;
      }break;
      
      case E_BytecodeOp_ConstString:
      {
        MemoryCopy(&nval, ptr, imm.u64);
        ptr += imm.u64;
      }break;
      
      case E_BytecodeOp_Abs:
      {
        if(type_group == E_TypeGroup_F32)
        {
          nval.f32 = svals[0].f32;
          if(svals[0].f32 < 0)
          {
            nval.f32 = -svals[0].f32;
          }
        }
        else if(type_group == E_TypeGroup_F64)
        {
          nval.f64 = svals[0].f64;
          if(svals[0].f64 < 0)
          {
            nval.f64 = -svals[0].f64;
          }
        }
        else
        {
          nval.s64 = svals[0].s64;
          if(svals[0].s64 < 0)
          {
            nval.s64 = -svals[0].s64;
          }
        }
      }break;
      
      case E_BytecodeOp_Neg:
      {
        if(type_group == E_TypeGroup_F32)
        {
          nval.f32 = -svals[0].f32;
        }
        else if(type_group == E_TypeGroup_F64)
        {
          nval.f64 = -svals[0].f64;
        }
        else
        {
          nval.u64 = (~svals[0].u64) + 1;
        }
      }break;
      
      case E_BytecodeOp_Add:
      {
        if(type_group == E_TypeGroup_F32)
        {
          nval.f32 = svals[0].f32 + svals[1].f32;
        }
        else if(type_group == E_TypeGroup_F64)
        {
          nval.f64 = svals[0].f64 + svals[1].f64;
        }
        else
        {
          nval.u64 = svals[0].u64 + svals[1].u64;
        }
      }break;
      
      case E_BytecodeOp_Sub:
      {
        if(type_group == E_TypeGroup_F32)
        {
          nval.f32 = svals[0].f32 - svals[1].f32;
        }
        else if(type_group == E_TypeGroup_F64)
        {
          nval.f64 = svals[0].f64 - svals[1].f64;
        }
        else
        {
          nval.u64 = svals[0].u64 - svals[1].u64;
        }
      }break;
      
      case E_BytecodeOp_Mul:
      {
        if(type_group == E_TypeGroup_F32)
        {
          nval.f32 = svals[0].f32*svals[1].f32;
        }
        else if(type_group == E_TypeGroup_F64)
        {
          nval.f64 = svals[0].f64*svals[1].f64;
        }
        else
        {
          nval.u64 = svals[0].u64*svals[1].u64;
        }
      }break;
      
      case E_BytecodeOp_Div:
      {
        if(type_group == E_TypeGroup_F32)
        {
          if(svals[1].f32 != 0.f)
          {
            nval.f32 = svals[0].f32/svals[1].f32;
          }
          else
          {
            result.code = E_InterpretationCode_DivideByZero;
            goto done;
          }
        }
        else if(type_group == E_TypeGroup_F64)
        {
          if(svals[1].f64 != 0.)
          {
            nval.f64 = svals[0].f64/svals[1].f64;
          }
          else
          {
            result.code = E_InterpretationCode_DivideByZero;
            goto done;
          }
        }
        else if(type_group == E_TypeGroup_U ||
                type_group == E_TypeGroup_S)
        {
          if(svals[1].u64 != 0)
          {
            nval.u64 = svals[0].u64/svals[1].u64;
          }
          else
          {
            result.code = E_InterpretationCode_DivideByZero;
            goto done;
          }
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_Mod:
      {
        if(type_group == E_TypeGroup_U ||
           type_group == E_TypeGroup_S)
        {
          if(svals[1].u64 != 0)
          {
            nval.u64 = svals[0].u64%svals[1].u64;
          }
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_LShift:
      {
        if(type_group == E_TypeGroup_U)
        {
          switch(op_arithmetic_size)
          {
            default:{}break;
            case 1:{nval.u8  = svals[0].u8 << svals[1].u8;}break;
            case 2:{nval.u16 = svals[0].u16 << svals[1].u16;}break;
            case 4:{nval.u32 = svals[0].u32 << svals[1].u32;}break;
            case 8:{nval.u64 = svals[0].u64 << svals[1].u64;}break;
          }
        }
        else if(type_group == E_TypeGroup_S)
        {
          switch(op_arithmetic_size)
          {
            default:{}break;
            case 1:{nval.s8  = svals[0].s8 << svals[1].s8;}break;
            case 2:{nval.s16 = svals[0].s16 << svals[1].s16;}break;
            case 4:{nval.s32 = svals[0].s32 << svals[1].s32;}break;
            case 8:{nval.s64 = svals[0].s64 << svals[1].s64;}break;
          }
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_RShift:
      {
        if(type_group == E_TypeGroup_U)
        {
          switch(op_arithmetic_size)
          {
            default:{}break;
            case 1:{nval.u8  = svals[0].u8 >> svals[1].u8;}break;
            case 2:{nval.u16 = svals[0].u16 >> svals[1].u16;}break;
            case 4:{nval.u32 = svals[0].u32 >> svals[1].u32;}break;
            case 8:{nval.u64 = svals[0].u64 >> svals[1].u64;}break;
          }
        }
        else if(type_group == E_TypeGroup_S)
        {
          switch(op_arithmetic_size)
          {
            default:{}break;
            case 1:{nval.s8  = svals[0].s8 >> svals[1].s8;}break;
            case 2:{nval.s16 = svals[0].s16 >> svals[1].s16;}break;
            case 4:{nval.s32 = svals[0].s32 >> svals[1].s32;}break;
            case 8:{nval.s64 = svals[0].s64 >> svals[1].s64;}break;
          }
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_BitAnd:
      {
        if(type_group == E_TypeGroup_U ||
           type_group == E_TypeGroup_S)
        {
          nval.u64 = svals[0].u64&svals[1].u64;
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_BitOr:
      {
        if(type_group == E_TypeGroup_U ||
           type_group == E_TypeGroup_S)
        {
          nval.u64 = svals[0].u64|svals[1].u64;
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_BitXor:
      {
        if(type_group == E_TypeGroup_U ||
           type_group == E_TypeGroup_S)
        {
          nval.u64 = svals[0].u64^svals[1].u64;
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_BitNot:
      {
        if(type_group == E_TypeGroup_U ||
           type_group == E_TypeGroup_S)
        {
          nval.u64 = ~svals[0].u64;
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_LogAnd:
      {
        if(type_group == E_TypeGroup_U ||
           type_group == E_TypeGroup_S)
        {
          nval.u64 = (svals[0].u64 && svals[1].u64);
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_LogOr:
      {
        if(type_group == E_TypeGroup_U ||
           type_group == E_TypeGroup_S)
        {
          nval.u64 = (svals[0].u64 || svals[1].u64);
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_LogNot:
      {
        if(type_group == E_TypeGroup_U ||
           type_group == E_TypeGroup_S)
        {
          nval.u64 = (!svals[0].u64);
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_EqEq:
      {
        B32 result = MemoryMatchArray(svals[0].u512.u64, svals[1].u512.u64);
        nval.u64 = !!result;
      }break;
      
      case E_BytecodeOp_NtEq:
      {
        B32 result = MemoryMatchArray(svals[0].u512.u64, svals[1].u512.u64);
        nval.u64 = !result;
      }break;
      
      case E_BytecodeOp_LsEq:
      {
        if(type_group == E_TypeGroup_F32)
        {
          nval.u64 = (svals[0].f32 <= svals[1].f32);
        }
        else if(type_group == E_TypeGroup_F64)
        {
          nval.u64 = (svals[0].f64 <= svals[1].f64);
        }
        else if(type_group == E_TypeGroup_U)
        {
          nval.u64 = (svals[0].u64 <= svals[1].u64);
        }
        else if(type_group == E_TypeGroup_S)
        {
          nval.u64 = (svals[0].s64 <= svals[1].s64);
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_GrEq:
      {
        if(type_group == E_TypeGroup_F32)
        {
          nval.u64 = (svals[0].f32 >= svals[1].f32);
        }
        else if(type_group == E_TypeGroup_F64)
        {
          nval.u64 = (svals[0].f64 >= svals[1].f64);
        }
        else if(type_group == E_TypeGroup_U)
        {
          nval.u64 = (svals[0].u64 >= svals[1].u64);
        }
        else if(type_group == E_TypeGroup_S)
        {
          nval.u64 = (svals[0].s64 >= svals[1].s64);
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_Less:
      {
        if(type_group == E_TypeGroup_F32)
        {
          nval.u64 = (svals[0].f32 < svals[1].f32);
        }
        else if(type_group == E_TypeGroup_F64)
        {
          nval.u64 = (svals[0].f64 < svals[1].f64);
        }
        else if(type_group == E_TypeGroup_U)
        {
          nval.u64 = (svals[0].u64 < svals[1].u64);
        }
        else if(type_group == E_TypeGroup_S)
        {
          nval.u64 = (svals[0].s64 < svals[1].s64);
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_Grtr:
      {
        if(type_group == E_TypeGroup_F32)
        {
          nval.u64 = (svals[0].f32 > svals[1].f32);
        }
        else if(type_group == E_TypeGroup_F64)
        {
          nval.u64 = (svals[0].f64 > svals[1].f64);
        }
        else if(type_group == E_TypeGroup_U)
        {
          nval.u64 = (svals[0].u64 > svals[1].u64);
        }
        else if(type_group == E_TypeGroup_S)
        {
          nval.u64 = (svals[0].s64 > svals[1].s64);
        }
        else
        {
          result.code = E_InterpretationCode_BadOpTypes;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_Trunc:
      {
        if(0 < imm.u64)
        {
          U64 mask = 0;
          if(imm.u64 < 64)
          {
            mask = max_U64 >> (64 - imm.u64);
          }
          nval.u64 = svals[0].u64&mask;
        }
      }break;
      
      case E_BytecodeOp_TruncSigned:
      {
        if(0 < imm.u64)
        {
          U64 mask = 0;
          if(imm.u64 < 64)
          {
            mask = max_U64 >> (64 - imm.u64);
          }
          U64 high = 0;
          if(svals[0].u64 & (1 << (imm.u64 - 1)))
          {
            high = ~mask;
          }
          nval.u64 = high|(svals[0].u64&mask);
        }
      }break;
      
      case E_BytecodeOp_Convert:
      {
        U32 in = imm.u64&0xFF;
        U32 out = (imm.u64 >> 8)&0xFF;
        if(in != out)
        {
          switch(in + out*E_TypeGroup_COUNT)
          {
            case E_TypeGroup_F32 + E_TypeGroup_U*E_TypeGroup_COUNT:
            {
              nval.u64 = (U64)svals[0].f32;
            }break;
            case E_TypeGroup_F64 + E_TypeGroup_U*E_TypeGroup_COUNT:
            {
              nval.u64 = (U64)svals[0].f64;
            }break;
            
            case E_TypeGroup_F32 + E_TypeGroup_S*E_TypeGroup_COUNT:
            {
              nval.s64 = (S64)svals[0].f32;
            }break;
            case E_TypeGroup_F64 + E_TypeGroup_S*E_TypeGroup_COUNT:
            {
              nval.s64 = (S64)svals[0].f64;
            }break;
            
            case E_TypeGroup_U + E_TypeGroup_F32*E_TypeGroup_COUNT:
            {
              nval.f32 = (F32)svals[0].u64;
            }break;
            case E_TypeGroup_S + E_TypeGroup_F32*E_TypeGroup_COUNT:
            {
              nval.f32 = (F32)svals[0].s64;
            }break;
            case E_TypeGroup_F64 + E_TypeGroup_F32*E_TypeGroup_COUNT:
            {
              nval.f32 = (F32)svals[0].f64;
            }break;
            
            case E_TypeGroup_U + E_TypeGroup_F64*E_TypeGroup_COUNT:
            {
              nval.f64 = (F64)svals[0].u64;
            }break;
            case E_TypeGroup_S + E_TypeGroup_F64*E_TypeGroup_COUNT:
            {
              nval.f64 = (F64)svals[0].s64;
            }break;
            case E_TypeGroup_F32 + E_TypeGroup_F64*E_TypeGroup_COUNT:
            {
              nval.f64 = (F64)svals[0].f32;
            }break;
          }
        }
      }break;
      
      case E_BytecodeOp_Pick:
      {
        if(stack_count > imm.u64)
        {
          nval = stack[stack_count - imm.u64 - 1];
        }
        else
        {
          result.code = E_InterpretationCode_BadOp;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_Pop:
      {
        // do nothing - the pop is handled by the control bits
      }break;
      
      case E_BytecodeOp_Insert:
      {
        if(stack_count > imm.u64)
        {
          if(imm.u64 > 0)
          {
            E_Value tval = stack[stack_count - 1];
            E_Value *dst = stack + stack_count - 1 - imm.u64;
            E_Value *shift = dst + 1;
            MemoryCopy(shift, dst, imm.u64*sizeof(E_Value));
            *dst = tval;
          }
        }
        else
        {
          result.code = E_InterpretationCode_BadOp;
          goto done;
        }
      }break;
      
      case E_BytecodeOp_ValueRead:
      {
        U64 bytes_to_read = imm.u64;
        U64 offset = svals[0].u64;
        if(offset + bytes_to_read <= sizeof(E_Value))
        {
          E_Value src_val = svals[1];
          MemoryCopy(&nval.u512.u64[0], (U8 *)(&src_val.u512.u64[0]) + offset, bytes_to_read);
        }
      }break;
      
      case E_BytecodeOp_ByteSwap:
      {
        U64 byte_size = imm.u64;
        switch(byte_size)
        {
          default:
          {
            result.code = E_InterpretationCode_BadOp;
            goto done;
          }break;
          case 2:{nval.u16 = bswap_u16(svals[0].u16);}break;
          case 4:{nval.u32 = bswap_u32(svals[0].u32);}break;
          case 8:{nval.u64 = bswap_u64(svals[0].u64);}break;
        }
      }break;
      
      case E_BytecodeOp_PushCfa:
      {
        nval.u64 = e_interpret_ctx->cfa;
      }break;
      
      case E_BytecodeOp_CallSiteValue:
      case E_BytecodeOp_PartialValue:
      case E_BytecodeOp_PartialValueBit:
      case E_BytecodeOp_Swap:
      {
        // TODO(rjf)
        result.code = E_InterpretationCode_BadOp;
        goto done;
      }break;
    }
    
    // rjf: push
    {
      U64 push_count = E_PUSHN_FROM_CTRLBITS(ctrlbits);
      if(push_count == 1)
      {
        if(stack_count < stack_cap)
        {
          stack[stack_count] = nval;
          stack_count += 1;
        }
        else
        {
          result.code = E_InterpretationCode_InsufficientStackSpace;
          goto done;
        }
      }
    }
  }
  done:;
  
  if(stack_count >= 1)
  {
    result.value = stack[0];
  }
  result.space = selected_space;
  scratch_end(scratch);
  return result;
}
