// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Bytecode Globals

read_only global U16 e_bytecode_op_ctrlbits_table[E_BytecodeOp_COUNT+1] =
{
  E_CTRLBITS(0, 0, 0),
  E_CTRLBITS(0, 0, 0),
  E_CTRLBITS(2, 1, 0),
  E_CTRLBITS(2, 0, 0),
  E_CTRLBITS(1, 1, 1),
  E_CTRLBITS(4, 0, 1),
  E_CTRLBITS(0, 1, 1),
  E_CTRLBITS(8, 0, 1),
  E_CTRLBITS(4, 0, 1),
  E_CTRLBITS(4, 0, 1),
  E_CTRLBITS(0, 0, 0),
  E_CTRLBITS(0, 0, 0),
  E_CTRLBITS(1, 0, 1),
  E_CTRLBITS(2, 0, 1),
  E_CTRLBITS(4, 0, 1),
  E_CTRLBITS(8, 0, 1),
  E_CTRLBITS(16, 0, 1),
  E_CTRLBITS(1, 0, 1),
  E_CTRLBITS(1, 1, 1),
  E_CTRLBITS(1, 1, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(2, 2, 1),
  E_CTRLBITS(2, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 1, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 1, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 1, 1),
  E_CTRLBITS(1, 1, 1),
  E_CTRLBITS(2, 1, 1),
  E_CTRLBITS(1, 0, 1),
  E_CTRLBITS(0, 1, 0),
  E_CTRLBITS(1, 0, 0),
  E_CTRLBITS(1, 2, 1),
  E_CTRLBITS(1, 1, 1),
  E_CTRLBITS(4, 0, 0),
  E_CTRLBITS(4, 0, 0),
  E_CTRLBITS(8, 0, 0),
  E_CTRLBITS(0, 2, 2),
  E_CTRLBITS(0, 0, 1),
  E_CTRLBITS(0, 0, 0),
};

read_only global struct
{
  E_ConversionKind dst_typegroups[E_TypeGroup_COUNT];
}
e_typegroup_conversion_kind_matrix[E_TypeGroup_COUNT+1] =
{
  {{E_ConversionKind_OtherToOther, E_ConversionKind_FromOther, E_ConversionKind_FromOther, E_ConversionKind_FromOther, E_ConversionKind_FromOther}},
  {{E_ConversionKind_ToOther, E_ConversionKind_Noop, E_ConversionKind_Noop, E_ConversionKind_Legal, E_ConversionKind_Legal}},
  {{E_ConversionKind_ToOther, E_ConversionKind_Noop, E_ConversionKind_Noop, E_ConversionKind_Legal, E_ConversionKind_Legal}},
  {{E_ConversionKind_ToOther, E_ConversionKind_Legal, E_ConversionKind_Legal, E_ConversionKind_Noop, E_ConversionKind_Legal}},
  {{E_ConversionKind_ToOther, E_ConversionKind_Legal, E_ConversionKind_Legal, E_ConversionKind_Legal, E_ConversionKind_Noop}},
  {{E_ConversionKind_ToOther, E_ConversionKind_Legal, E_ConversionKind_Legal, E_ConversionKind_Legal, E_ConversionKind_Legal}},
  {{E_ConversionKind_ToOther, E_ConversionKind_Legal, E_ConversionKind_Legal, E_ConversionKind_Legal, E_ConversionKind_Legal}},
  {{E_ConversionKind_Noop, E_ConversionKind_Noop, E_ConversionKind_Noop, E_ConversionKind_Noop, E_ConversionKind_Noop}},
};

read_only global struct
{
  U8 *str;
  U64 size;
}
e_conversion_kind_message_string_table[E_ConversionKind_COUNT+1] =
{
  {(U8 *)"", sizeof("")},
  {(U8 *)"", sizeof("")},
  {(U8 *)"Cannot convert between these types.", sizeof("Cannot convert between these types.")},
  {(U8 *)"Cannot convert to this type.", sizeof("Cannot convert to this type.")},
  {(U8 *)"Cannot convert this type.", sizeof("Cannot convert this type.")},
  {(U8 *)"", sizeof("")},
};

////////////////////////////////
//~ rjf: Bytecode Functions

internal E_ConversionKind
e_conversion_kind_from_typegroups(E_TypeGroup in, E_TypeGroup out)
{
  E_ConversionKind result = E_ConversionKind_Noop;
  if(in < E_TypeGroup_COUNT && out < E_TypeGroup_COUNT)
  {
    result = e_typegroup_conversion_kind_matrix[in].dst_typegroups[out];
  }
  return result;
}

internal B32
e_bytecode_op_typegroup_are_compatible(E_BytecodeOp op, E_TypeGroup group)
{
  B32 result = 0;
  switch(op)
  {
    case E_BytecodeOp_Neg: case E_BytecodeOp_Add: case E_BytecodeOp_Sub:
    case E_BytecodeOp_Mul: case E_BytecodeOp_Div:
    case E_BytecodeOp_EqEq:case E_BytecodeOp_NtEq:
    case E_BytecodeOp_LsEq:case E_BytecodeOp_GrEq:
    case E_BytecodeOp_Less:case E_BytecodeOp_Grtr:
    {
      if(group != E_TypeGroup_Other)
      {
        result = 1;
      }
    }break;
    case E_BytecodeOp_Mod:case E_BytecodeOp_LShift:case E_BytecodeOp_RShift:
    case E_BytecodeOp_BitNot:case E_BytecodeOp_BitAnd:case E_BytecodeOp_BitXor:
    case E_BytecodeOp_BitOr:case E_BytecodeOp_LogNot:case E_BytecodeOp_LogAnd:
    case E_BytecodeOp_LogOr:
    {
      if(group == E_TypeGroup_S || group == E_TypeGroup_U)
      {
        result = 1;
      }
    }break;
  }
  return result;
}

internal U8 *
e_explanation_string_from_conversion_kind(E_ConversionKind kind, U64 *size_out)
{
  U8 *result = 0;
  *size_out = 0;
  if(kind < E_ConversionKind_COUNT)
  {
    result = e_conversion_kind_message_string_table[kind].str;
    *size_out = e_conversion_kind_message_string_table[kind].size;
  }
  return result;
}
