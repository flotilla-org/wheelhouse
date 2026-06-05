// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef EVAL_BYTECODE_H
#define EVAL_BYTECODE_H

////////////////////////////////
//~ rjf: Bytecode Types

typedef U8 E_BytecodeOp;
typedef enum E_BytecodeOpEnum
{
  E_BytecodeOp_Stop            = 0,
  E_BytecodeOp_Noop            = 1,
  E_BytecodeOp_Cond            = 2,
  E_BytecodeOp_Skip            = 3,
  E_BytecodeOp_MemRead         = 4,
  E_BytecodeOp_RegRead         = 5,
  E_BytecodeOp_RegReadDyn      = 6,
  E_BytecodeOp_FrameOff        = 7,
  E_BytecodeOp_ModuleOff       = 8,
  E_BytecodeOp_TLSOff          = 9,
  E_BytecodeOp_ObjectOff       = 10,
  E_BytecodeOp_CFA             = 11,
  E_BytecodeOp_ConstU8         = 12,
  E_BytecodeOp_ConstU16        = 13,
  E_BytecodeOp_ConstU32        = 14,
  E_BytecodeOp_ConstU64        = 15,
  E_BytecodeOp_ConstU128       = 16,
  E_BytecodeOp_ConstString     = 17,
  E_BytecodeOp_Abs             = 18,
  E_BytecodeOp_Neg             = 19,
  E_BytecodeOp_Add             = 20,
  E_BytecodeOp_Sub             = 21,
  E_BytecodeOp_Mul             = 22,
  E_BytecodeOp_Div             = 23,
  E_BytecodeOp_Mod             = 24,
  E_BytecodeOp_LShift          = 25,
  E_BytecodeOp_RShift          = 26,
  E_BytecodeOp_BitAnd          = 27,
  E_BytecodeOp_BitOr           = 28,
  E_BytecodeOp_BitXor          = 29,
  E_BytecodeOp_BitNot          = 30,
  E_BytecodeOp_LogAnd          = 31,
  E_BytecodeOp_LogOr           = 32,
  E_BytecodeOp_LogNot          = 33,
  E_BytecodeOp_EqEq            = 34,
  E_BytecodeOp_NtEq            = 35,
  E_BytecodeOp_LsEq            = 36,
  E_BytecodeOp_GrEq            = 37,
  E_BytecodeOp_Less            = 38,
  E_BytecodeOp_Grtr            = 39,
  E_BytecodeOp_Trunc           = 40,
  E_BytecodeOp_TruncSigned     = 41,
  E_BytecodeOp_Convert         = 42,
  E_BytecodeOp_Pick            = 43,
  E_BytecodeOp_Pop             = 44,
  E_BytecodeOp_Insert          = 45,
  E_BytecodeOp_ValueRead       = 46,
  E_BytecodeOp_ByteSwap        = 47,
  E_BytecodeOp_CallSiteValue   = 48,
  E_BytecodeOp_PartialValue    = 49,
  E_BytecodeOp_PartialValueBit = 50,
  E_BytecodeOp_Swap            = 51,
  E_BytecodeOp_PushCfa         = 52,
  E_BytecodeOp_COUNT           = 53,
}
E_BytecodeOpEnum;

typedef U8 E_TypeGroup;
typedef enum E_TypeGroupEnum
{
  E_TypeGroup_Other = 0,
  E_TypeGroup_U     = 1,
  E_TypeGroup_S     = 2,
  E_TypeGroup_F32   = 3,
  E_TypeGroup_F64   = 4,
  E_TypeGroup_F80   = 5,
  E_TypeGroup_F128  = 6,
  E_TypeGroup_COUNT = 7,
}
E_TypeGroupEnum;

typedef U8 E_ConversionKind;
typedef enum E_ConversionKindEnum
{
  E_ConversionKind_Noop         = 0,
  E_ConversionKind_Legal        = 1,
  E_ConversionKind_OtherToOther = 2,
  E_ConversionKind_ToOther      = 3,
  E_ConversionKind_FromOther    = 4,
  E_ConversionKind_COUNT        = 5,
}
E_ConversionKindEnum;

#define E_BytecodeOp_XList \
X(Stop)\
X(Noop)\
X(Cond)\
X(Skip)\
X(MemRead)\
X(RegRead)\
X(RegReadDyn)\
X(FrameOff)\
X(ModuleOff)\
X(TLSOff)\
X(ObjectOff)\
X(CFA)\
X(ConstU8)\
X(ConstU16)\
X(ConstU32)\
X(ConstU64)\
X(ConstU128)\
X(ConstString)\
X(Abs)\
X(Neg)\
X(Add)\
X(Sub)\
X(Mul)\
X(Div)\
X(Mod)\
X(LShift)\
X(RShift)\
X(BitAnd)\
X(BitOr)\
X(BitXor)\
X(BitNot)\
X(LogAnd)\
X(LogOr)\
X(LogNot)\
X(EqEq)\
X(NtEq)\
X(LsEq)\
X(GrEq)\
X(Less)\
X(Grtr)\
X(Trunc)\
X(TruncSigned)\
X(Convert)\
X(Pick)\
X(Pop)\
X(Insert)\
X(ValueRead)\
X(ByteSwap)\
X(CallSiteValue)\
X(PartialValue)\
X(PartialValueBit)\
X(Swap)\
X(PushCfa)\

#define E_CTRLBITS(decodeN,popN,pushN) (((decodeN) << 8) | ((popN) << 4) | ((pushN) << 0))
#define E_DECODEN_FROM_CTRLBITS(ctrlbits) (((ctrlbits) >> 8) & 0xff)
#define E_POPN_FROM_CTRLBITS(ctrlbits) (((ctrlbits) >> 4) & 0xf)
#define E_PUSHN_FROM_CTRLBITS(ctrlbits) (((ctrlbits) >> 0) & 0xf)

////////////////////////////////
//~ rjf: Bytecode Globals

read_only global U16 e_bytecode_op_ctrlbits_table[E_BytecodeOp_COUNT+1];

////////////////////////////////
//~ rjf: Bytecode Functions

internal E_ConversionKind e_conversion_kind_from_typegroups(E_TypeGroup in, E_TypeGroup out);
internal B32 e_bytecode_op_typegroup_are_compatible(E_BytecodeOp op, E_TypeGroup group);
internal U8 *e_explanation_string_from_conversion_kind(E_ConversionKind kind, U64 *size_out);

#endif // EVAL_BYTECODE_H
