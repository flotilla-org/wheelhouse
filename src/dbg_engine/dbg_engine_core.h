// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef DBG_ENGINE_CORE_H
#define DBG_ENGINE_CORE_H

////////////////////////////////
//~ rjf: ID Types

typedef U32 D_MachineID;
typedef U32 D_ControllerKind;

#define D_MachineID_Local (1)

////////////////////////////////
//~ rjf: Entity Handle Types

typedef struct D_Handle D_Handle;
struct D_Handle
{
  D_MachineID machine_id;
  D_ControllerKind controller_kind;
  U64 entity_id;
};

////////////////////////////////
//~ rjf: Generated Code

#include "generated/dbg_engine.meta.h"

////////////////////////////////
//~ rjf: Entity Types

typedef struct D_Entity D_Entity;
struct D_Entity
{
  D_Entity *first;
  D_Entity *last;
  D_Entity *next;
  D_Entity *prev;
  D_Entity *parent;
  D_EntityKind kind;
  Arch arch;
  U32 rgba;
  D_Handle handle;
  U64 id;
  String8 string;
};

typedef struct D_EntityNode D_EntityNode;
struct D_EntityNode
{
  D_EntityNode *next;
  D_Entity *v;
};

typedef struct D_EntityList D_EntityList;
struct D_EntityList
{
  D_EntityNode *first;
  D_EntityNode *last;
  U64 count;
};

typedef struct D_EntityArray D_EntityArray;
struct D_EntityArray
{
  D_Entity **v;
  U64 count;
};

#endif // DBG_ENGINE_CORE_H
