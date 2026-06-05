// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Binary View

typedef struct UIShell_BinaryViewState UIShell_BinaryViewState;
struct UIShell_BinaryViewState
{
  B32 initialized;
  B32 contain_cursor;
  B32 center_cursor;
  U64 cursor_off;
  U64 mark_off;
};

internal B32
uishell_byte_is_printable_ascii(U8 byte)
{
  B32 result = (32 <= byte && byte <= 126);
  return result;
}

////////////////////////////////
//~ rjf: Generic Property/List View

typedef enum UIShell_WatchCellKind
{
  UIShell_WatchCellKind_Eval,
  UIShell_WatchCellKind_ViewUI,
}
UIShell_WatchCellKind;

typedef U32 UIShell_WatchCellFlags;
enum
{
  UIShell_WatchCellFlag_Expr                    = (1<<0),
  UIShell_WatchCellFlag_NoEval                  = (1<<1),
  UIShell_WatchCellFlag_Button                  = (1<<2),
  UIShell_WatchCellFlag_Background              = (1<<3),
  UIShell_WatchCellFlag_ActivateWithSingleClick = (1<<4),
  UIShell_WatchCellFlag_IsNonCode               = (1<<5),
  UIShell_WatchCellFlag_Indented                = (1<<6),
  UIShell_WatchCellFlag_CanEdit                 = (1<<7),
};

typedef struct UIShell_WatchCell UIShell_WatchCell;
struct UIShell_WatchCell
{
  UIShell_WatchCell *next;
  UIShell_WatchCellKind kind;
  UIShell_WatchCellFlags flags;
  U64 index;
  E_Eval eval;
  F32 default_pct;
  F32 pct;
  F32 px;
};

typedef struct UIShell_WatchCellList UIShell_WatchCellList;
struct UIShell_WatchCellList
{
  UIShell_WatchCell *first;
  UIShell_WatchCell *last;
  U64 count;
  F32 pct_sum;
};

typedef struct UIShell_WatchRowInfo UIShell_WatchRowInfo;
struct UIShell_WatchRowInfo
{
  B32 expr_is_editable;
  B32 can_expand;
  String8 group_cfg_name;
  CFG_Node *group_cfg_parent;
  CFG_Node *group_cfg_child;
  String8 cell_style_key;
  UIShell_WatchCellList cells;
  RD_ViewUIRule *view_ui_rule;
};

typedef struct UIShell_WatchRowCellInfo UIShell_WatchRowCellInfo;
struct UIShell_WatchRowCellInfo
{
  UIShell_WatchCellFlags flags;
  CFG_Node *cfg;
  String8 cmd_name;
  String8 file_path;
  DR_FStrList expr_fstrs;
  DR_FStrList eval_fstrs;
  String8 description;
  RD_ViewUIRule *view_ui_rule;
};

typedef struct UIShell_WatchPt UIShell_WatchPt;
struct UIShell_WatchPt
{
  EV_Key parent_key;
  EV_Key key;
  U64 cell_id;
};

typedef struct UIShell_WatchViewTextEditState UIShell_WatchViewTextEditState;
struct UIShell_WatchViewTextEditState
{
  UIShell_WatchViewTextEditState *pt_hash_next;
  UIShell_WatchPt pt;
  TxtPt cursor;
  TxtPt mark;
  U8 input_buffer[1024];
  U64 input_size;
  U8 initial_buffer[1024];
  U64 initial_size;
};

typedef struct UIShell_WatchViewState UIShell_WatchViewState;
struct UIShell_WatchViewState
{
  B32 initialized;
  Arena *filter_arena;
  String8 last_filter;
  UIShell_WatchPt cursor;
  UIShell_WatchPt mark;
  UIShell_WatchPt next_cursor;
  UIShell_WatchPt next_mark;
  Arena *text_edit_arena;
  U64 text_edit_state_slots_count;
  UIShell_WatchViewTextEditState dummy_text_edit_state;
  UIShell_WatchViewTextEditState **text_edit_state_slots;
  B32 text_editing;
};

internal U64
uishell_id_from_watch_cell(UIShell_WatchCell *cell)
{
  U64 result = 5381;
  result = e_hash_from_string(result, str8_struct(&cell->kind));
  result = e_hash_from_string(result, str8_struct(&cell->index));
  if(cell->index != 0)
  {
    result = e_hash_from_string(result, str8_struct(&cell->default_pct));
  }
  return result;
}

internal UIShell_WatchCell *
uishell_watch_cell_list_push_new_(Arena *arena, UIShell_WatchCellList *list, UIShell_WatchCell *params)
{
  UIShell_WatchCell *cell = push_array(arena, UIShell_WatchCell, 1);
  {
    cell->index = list->count;
    SLLQueuePush(list->first, list->last, cell);
    list->count += 1;
  }
  U64 index = cell->index;
  MemoryCopyStruct(cell, params);
  cell->index = index;
  if(cell->pct == 0)
  {
    cell->pct = cell->default_pct;
  }
  list->pct_sum += cell->pct;
  cell->next = 0;
  return cell;
}
#define uishell_watch_cell_list_push_new(arena, list, kind_, eval_, ...) uishell_watch_cell_list_push_new_((arena), (list), &(UIShell_WatchCell){.kind = (kind_), .eval = (eval_), __VA_ARGS__})

internal B32
uishell_watch_pt_match(UIShell_WatchPt a, UIShell_WatchPt b)
{
  return (ev_key_match(a.parent_key, b.parent_key) &&
          ev_key_match(a.key, b.key) &&
          a.cell_id == b.cell_id);
}

internal UIShell_WatchRowInfo uishell_watch_row_info_from_row(Arena *arena, EV_Row *row);
internal UIShell_WatchRowCellInfo uishell_info_from_watch_row_cell(Arena *arena, EV_Row *row, EV_StringFlags string_flags, UIShell_WatchRowInfo *row_info, UIShell_WatchCell *cell, FNT_Tag font, F32 font_size, F32 max_size_px);

internal UIShell_WatchPt
uishell_watch_pt_from_tbl(EV_BlockRangeList *block_ranges, Vec2S64 tbl)
{
  UIShell_WatchPt pt = zero_struct;
  {
    Temp scratch = scratch_begin(0, 0);
    EV_Row *row = ev_row_from_num(scratch.arena, rd_view_eval_view(), block_ranges, (U64)tbl.y);
    UIShell_WatchRowInfo row_info = uishell_watch_row_info_from_row(scratch.arena, row);
    {
      S64 x = 0;
      for(UIShell_WatchCell *cell = row_info.cells.first; cell != 0; cell = cell->next, x += 1)
      {
        if(x == tbl.x)
        {
          pt.cell_id = uishell_id_from_watch_cell(cell);
          break;
        }
      }
    }
    pt.key        = row->key;
    pt.parent_key = row->block->key;
    scratch_end(scratch);
  }
  return pt;
}

internal Vec2S64
uishell_tbl_from_watch_pt(EV_BlockRangeList *block_ranges, UIShell_WatchPt pt)
{
  Vec2S64 tbl = {0};
  {
    Temp scratch = scratch_begin(0, 0);
    U64 num = ev_num_from_key(block_ranges, pt.key);
    EV_Row *row = ev_row_from_num(scratch.arena, rd_view_eval_view(), block_ranges, num);
    UIShell_WatchRowInfo row_info = uishell_watch_row_info_from_row(scratch.arena, row);
    tbl.x = 0;
    {
      S64 x = 0;
      for(UIShell_WatchCell *cell = row_info.cells.first; cell != 0; cell = cell->next, x += 1)
      {
        U64 cell_id = uishell_id_from_watch_cell(cell);
        if(cell_id == pt.cell_id)
        {
          tbl.x = x;
          break;
        }
      }
    }
    tbl.y = (S64)num;
    scratch_end(scratch);
  }
  return tbl;
}

internal F32
uishell_watch_take_pct(CFG_Node **cfg_ptr)
{
  F32 result = 0;
  if(*cfg_ptr != &cfg_nil_node)
  {
    result = (F32)f64_from_str8((*cfg_ptr)->string);
    *cfg_ptr = (*cfg_ptr)->next;
  }
  return result;
}

internal UIShell_WatchViewTextEditState *
uishell_watch_view_text_edit_state_from_pt(UIShell_WatchViewState *wv, UIShell_WatchPt pt)
{
  UIShell_WatchViewTextEditState *result = &wv->dummy_text_edit_state;
  if(wv->text_edit_state_slots_count != 0 && wv->text_editing)
  {
    U64 hash = ev_hash_from_key(pt.key);
    U64 slot_idx = hash%wv->text_edit_state_slots_count;
    for(UIShell_WatchViewTextEditState *s = wv->text_edit_state_slots[slot_idx]; s != 0; s = s->pt_hash_next)
    {
      if(uishell_watch_pt_match(pt, s->pt))
      {
        result = s;
        break;
      }
    }
  }
  return result;
}

internal UIShell_WatchRowInfo
uishell_watch_row_info_from_row(Arena *arena, EV_Row *row)
{
  UIShell_WatchRowInfo info =
  {
    .can_expand = ev_row_is_expandable(row),
    .group_cfg_parent = &cfg_nil_node,
    .group_cfg_child = &cfg_nil_node,
    .view_ui_rule = &rd_nil_view_ui_rule,
  };
  Temp scratch = scratch_begin(&arena, 1);
  
  E_Type *row_type = e_type_from_key(row->eval.irtree.type_key);
  EV_Block *block = row->block;
  B32 block_is_root = (block->parent == &ev_nil_block);
  E_Eval block_eval = e_eval_from_key(row->block->eval.key);
  E_TypeKey block_type_key = e_type_key_unwrap(block_eval.irtree.type_key, E_TypeUnwrapFlag_Meta);
  E_TypeKind block_type_kind = e_type_kind_from_key(block_type_key);
  E_Type *block_type = e_type_from_key(block_type_key);
  CFG_Node *evalled_cfg = rd_cfg_from_eval_space(row->eval.space);
  
  B32 is_top_level = 0;
  if(evalled_cfg != &cfg_nil_node)
  {
    E_TypeKey top_level_type_key = e_string2typekey_map_lookup(rd_state->meta_name2type_map, evalled_cfg->string);
    is_top_level = (row->eval.value.u64 == 0 && e_type_key_match(top_level_type_key, row->eval.irtree.type_key));
  }
  
  if(block_type->flags & E_TypeFlag_EditableChildren ||
     (e_key_match(row->eval.key, e_key_zero()) && row->eval.expr == &e_expr_nil))
  {
    info.expr_is_editable = 1;
  }
  
  if(!block_is_root)
  {
    if(block_type_kind == E_TypeKind_Set && (block_eval.space.kind == RD_EvalSpaceKind_MetaQuery ||
                                             block_eval.space.kind == RD_EvalSpaceKind_MetaCfg))
    {
      CFG_Node *parent_cfg = rd_cfg_from_eval_space(block_eval.space);
      info.group_cfg_parent = parent_cfg;
      String8 singular_name = rd_singular_from_code_name_plural(block_type->name);
      info.group_cfg_name = singular_name.size != 0 ? singular_name : block_type->name;
      if(info.group_cfg_name.size != 0 &&
         (block_type->expand.id_from_num == E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_NAME(cfgs_slice) ||
          block_type->expand.id_from_num == E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_NAME(environment)))
      {
        (void)parent_cfg;
        info.group_cfg_child = cfg_node_from_id(row->key.child_id);
      }
    }
  }
  
  info.view_ui_rule = rd_view_ui_rule_from_string(row->block->viz_expand_rule->string);
  
  E_Type *maybe_table_type = block_type;
  for(;;)
  {
    if(maybe_table_type->kind == E_TypeKind_Lens &&
       str8_match(maybe_table_type->name, str8_lit("columns"), 0))
    {
      break;
    }
    else if(maybe_table_type->kind == E_TypeKind_Lens)
    {
      maybe_table_type = e_type_from_key(maybe_table_type->direct_type_key);
      continue;
    }
    else
    {
      break;
    }
  }
  
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  
  if(block->parent != &ev_nil_block && maybe_table_type->kind == E_TypeKind_Lens &&
     str8_match(maybe_table_type->name, str8_lit("columns"), 0) && maybe_table_type->count >= 1)
  {
    U64 column_count = maybe_table_type->count;
    info.cell_style_key = push_str8f(arena, "table_%I64u_cols", column_count);
    CFG_Node *style = cfg_node_child_from_string(view, info.cell_style_key);
    CFG_Node *w_cfg = style->first;
    E_ParentKey(row->eval.key)
    {
      for(U64 idx = 0; idx < maybe_table_type->count; idx += 1)
      {
        E_Eval cell_eval = e_eval_from_expr(maybe_table_type->args[idx]);
        uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, cell_eval,
                                         .default_pct = 1.f/maybe_table_type->count,
                                         .pct = uishell_watch_take_pct(&w_cfg));
      }
    }
    info.can_expand = 0;
  }
  else if(row->eval.space.kind == E_SpaceKind_FileSystem &&
          e_type_kind_from_key(row->eval.irtree.type_key) == E_TypeKind_Set)
  {
    E_Type *type = e_type_from_key(row->eval.irtree.type_key);
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval,
                                     .flags = UIShell_WatchCellFlag_Expr|UIShell_WatchCellFlag_NoEval|UIShell_WatchCellFlag_Indented|UIShell_WatchCellFlag_Button|UIShell_WatchCellFlag_IsNonCode,
                                     .pct = 1.f);
    if(str8_match(type->name, str8_lit("file"), 0))
    {
      info.can_expand = 0;
    }
  }
  else if(cfg_node_child_from_string(view, str8_lit("autocomplete")) != &cfg_nil_node)
  {
    info.can_expand = 0;
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval,
                                     .flags = UIShell_WatchCellFlag_Expr|UIShell_WatchCellFlag_NoEval|UIShell_WatchCellFlag_Button|UIShell_WatchCellFlag_Indented,
                                     .pct = 1.f);
  }
  else if(cfg_node_child_from_string(view, str8_lit("lister")) != &cfg_nil_node)
  {
    info.can_expand = 0;
    UIShell_WatchCellFlags extra_flags = UIShell_WatchCellFlag_Expr;
    if(e_type_kind_from_key(e_type_key_unwrap(row->eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative)) == E_TypeKind_Function)
    {
      extra_flags &= ~UIShell_WatchCellFlag_Expr;
    }
    if(row->eval.msgs.max_kind != E_MsgKind_Null)
    {
      extra_flags = UIShell_WatchCellFlag_Expr|UIShell_WatchCellFlag_NoEval;
    }
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval,
                                     .flags = extra_flags|UIShell_WatchCellFlag_Button|UIShell_WatchCellFlag_Indented,
                                     .pct = 1.f);
  }
  else if(is_top_level && evalled_cfg != &cfg_nil_node)
  {
    CFG_Node *cfg = evalled_cfg;
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval,
                                     .flags = UIShell_WatchCellFlag_Expr|UIShell_WatchCellFlag_NoEval|UIShell_WatchCellFlag_Button|UIShell_WatchCellFlag_Indented,
                                     .pct = 1.f);
    MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, cfg->string);
    for(MD_NodePtrNode *n = schemas.first; n != 0; n = n->next)
    {
      MD_Node *schema = n->v;
      MD_Node *cmds_root = md_tag_from_string(schema, str8_lit("row_commands"), 0);
      for MD_EachNode(cmd, cmds_root->first)
      {
        B32 is_file_only = md_node_has_tag(cmd, str8_lit("file"), 0);
        B32 is_cmd_line_only = md_node_has_tag(cmd, str8_lit("cmd_line"), 0);
        if(is_file_only && e_eval_from_string(rd_expr_from_cfg(evalled_cfg)).space.kind != E_SpaceKind_File)
        {
          continue;
        }
        if(is_cmd_line_only)
        {
          B32 is_cmd_line = 0;
          CFG_Node *cmd_line = cfg_node_child_from_string(cfg_node_root(), str8_lit("command_line"));
          for(CFG_Node *p = evalled_cfg->parent; p != &cfg_nil_node; p = p->parent)
          {
            if(p == cmd_line)
            {
              is_cmd_line = 1;
              break;
            }
          }
          if(!is_cmd_line)
          {
            continue;
          }
        }
        String8 cmd_name = cmd->string;
        UIShell_AppCmdInfo cmd_info = uishell_app_cmd_info_from_string(cmd_name);
        if(cmd_info.string.size != 0)
        {
          uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval,
                                           e_eval_from_stringf("query:commands.%S", cmd_info.string),
                                           .flags = UIShell_WatchCellFlag_ActivateWithSingleClick|UIShell_WatchCellFlag_Button,
                                           .px = floor_f32(ui_top_font_size()*3.f));
        }
      }
    }
  }
  else if(row->eval.space.kind == RD_EvalSpaceKind_MetaQuery ||
          (row->eval.space.kind == RD_EvalSpaceKind_MetaCfg &&
           e_type_kind_from_key(e_type_key_unwrap(row->eval.irtree.type_key, E_TypeUnwrapFlag_Meta)) == E_TypeKind_Set))
  {
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval,
                                     .flags = UIShell_WatchCellFlag_Expr|UIShell_WatchCellFlag_NoEval|UIShell_WatchCellFlag_Indented,
                                     .pct = 1.f);
  }
  else if(row->eval.space.kind == RD_EvalSpaceKind_MetaCmd)
  {
    E_Type *type = e_type_from_key(row->eval.irtree.type_key);
    UIShell_WatchCellFlags flags = UIShell_WatchCellFlag_Expr|UIShell_WatchCellFlag_NoEval|UIShell_WatchCellFlag_Indented;
    if(type->kind != E_TypeKind_Set)
    {
      flags |= UIShell_WatchCellFlag_Button|UIShell_WatchCellFlag_ActivateWithSingleClick;
    }
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval, .flags = flags, .pct = 1.f);
  }
  else if(info.view_ui_rule != &rd_nil_view_ui_rule)
  {
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_ViewUI, row->eval, .pct = 1.f);
  }
  else if(row->eval.expr == &e_expr_nil && info.group_cfg_name.size != 0 && info.group_cfg_child == &cfg_nil_node)
  {
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval,
                                     .flags = UIShell_WatchCellFlag_Expr|UIShell_WatchCellFlag_NoEval|UIShell_WatchCellFlag_Indented,
                                     .pct = 1.f);
  }
  else if(info.group_cfg_child == &cfg_nil_node &&
          e_type_kind_from_key(e_type_key_unwrap(row->eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative)) == E_TypeKind_Bool &&
          (row->eval.space.kind == RD_EvalSpaceKind_MetaCfg ||
           row->eval.space.kind == RD_EvalSpaceKind_MetaCmd))
  {
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval,
                                     .flags = UIShell_WatchCellFlag_Expr|UIShell_WatchCellFlag_Indented,
                                     .pct = 1.f);
  }
  else
  {
    if(row->eval.space.kind == RD_EvalSpaceKind_MetaCfg ||
       row->eval.space.kind == RD_EvalSpaceKind_MetaCmd ||
       row->eval.space.kind == E_SpaceKind_File)
    {
      E_TypeKey substantive_row_eval_type = e_type_key_unwrap(row->eval.irtree.type_key, E_TypeUnwrapFlag_Meta);
      if(e_type_kind_from_key(substantive_row_eval_type) == E_TypeKind_Array &&
         e_type_kind_from_key(e_type_key_direct(substantive_row_eval_type)) == E_TypeKind_U8)
      {
        info.can_expand = 0;
      }
    }
    info.cell_style_key = str8_lit("normal");
    CFG_Node *style = cfg_node_child_from_string(view, info.cell_style_key);
    CFG_Node *w_cfg = style->first;
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval,
                                     .flags = UIShell_WatchCellFlag_Expr|UIShell_WatchCellFlag_NoEval|UIShell_WatchCellFlag_Indented,
                                     .default_pct = 0.35f,
                                     .pct = uishell_watch_take_pct(&w_cfg));
    uishell_watch_cell_list_push_new(arena, &info.cells, UIShell_WatchCellKind_Eval, row->eval,
                                     .default_pct = 0.65f,
                                     .pct = uishell_watch_take_pct(&w_cfg));
  }
  
  if(abs_f32(info.cells.pct_sum - 1.f) > 0.01f)
  {
    F32 sum = info.cells.pct_sum;
    if(sum <= 0)
    {
      sum = 1.f;
    }
    for(UIShell_WatchCell *c = info.cells.first; c != 0; c = c->next)
    {
      c->pct /= sum;
    }
  }
  
  scratch_end(scratch);
  return info;
}

internal UIShell_WatchRowCellInfo
uishell_info_from_watch_row_cell(Arena *arena, EV_Row *row, EV_StringFlags string_flags, UIShell_WatchRowInfo *row_info, UIShell_WatchCell *cell, FNT_Tag font, F32 font_size, F32 max_size_px)
{
  Temp scratch = scratch_begin(&arena, 1);
  UIShell_WatchRowCellInfo result =
  {
    .flags = cell->flags,
    .view_ui_rule = &rd_nil_view_ui_rule,
    .cfg = &cfg_nil_node,
  };
  
  E_Type *block_type = e_type_from_key(row->block->eval.irtree.type_key);
  E_Type *cell_type = e_type_from_key(cell->eval.irtree.type_key);
  MD_NodePtrList cell_schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, cell_type->name);
  if(cell->eval.space.u64s[1] == 0 && cell_schemas.count != 0)
  {
    result.cfg = rd_cfg_from_eval_space(cell->eval.space);
  }
  result.cmd_name = rd_cmd_name_from_eval(cell->eval);
  result.file_path = rd_file_path_from_eval(arena, cell->eval);
  for(E_Type *type = cell_type; type->kind == E_TypeKind_Lens; type = e_type_from_key(type->direct_type_key))
  {
    RD_ViewUIRule *view_ui_rule = rd_view_ui_rule_from_string(type->name);
    if(view_ui_rule != &rd_nil_view_ui_rule)
    {
      result.view_ui_rule = view_ui_rule;
      break;
    }
  }
  for(E_Type *type = cell_type; type->kind != E_TypeKind_Null; type = e_type_from_key(type->direct_type_key))
  {
    if(type->kind == E_TypeKind_MetaDescription)
    {
      result.description = type->name;
      break;
    }
  }
  
  if(cell->kind == UIShell_WatchCellKind_Eval)
  {
    if(cell->flags & UIShell_WatchCellFlag_Expr && cell->flags & UIShell_WatchCellFlag_NoEval)
    {
      if(row_info->expr_is_editable)
      {
        result.flags |= UIShell_WatchCellFlag_CanEdit;
      }
    }
    else if(ev_type_key_is_editable(cell->eval.irtree.type_key) && cell->eval.irtree.mode == E_Mode_Offset)
    {
      result.flags |= UIShell_WatchCellFlag_CanEdit;
    }
  }
  
  if(cell->eval.msgs.max_kind > E_MsgKind_Null && !(cell->flags & UIShell_WatchCellFlag_NoEval))
  {
    RD_Font(RD_FontSlot_Main)
    {
      DR_FStrParams params = {rd_font_from_slot(RD_FontSlot_Main), rd_raster_flags_from_slot(RD_FontSlot_Main), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
      UI_TagF("weak")
      {
        dr_fstrs_push_new(arena, &result.expr_fstrs, &params,
                          rd_icon_kind_text_table[RD_IconKind_WarningBig],
                          .font = rd_font_from_slot(RD_FontSlot_Icons),
                          .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Icons),
                          .color = ui_color_from_name(str8_lit("text")));
        dr_fstrs_push_new(arena, &result.expr_fstrs, &params, str8_lit("  "));
        for(E_Msg *msg = cell->eval.msgs.first; msg != 0; msg = msg->next)
        {
          DR_FStrList msg_fstrs = rd_fstrs_from_rich_string(arena, msg->text);
          dr_fstrs_concat_in_place(&result.expr_fstrs, &msg_fstrs);
          if(msg->next)
          {
            dr_fstrs_push_new(arena, &result.expr_fstrs, &params, str8_lit(" "));
          }
        }
      }
    }
  }
  else if(result.cfg != &cfg_nil_node)
  {
    result.expr_fstrs = rd_title_fstrs_from_cfg(arena, result.cfg, 0);
    result.flags |= UIShell_WatchCellFlag_Button;
  }
  else if(result.cmd_name.size != 0)
  {
    if(cell->px != 0)
    {
      DR_FStrParams params = {rd_font_from_slot(RD_FontSlot_Main), rd_raster_flags_from_slot(RD_FontSlot_Main), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
      DR_FStrList fstrs = {0};
      UI_TagF("weak")
      {
        dr_fstrs_push_new(arena, &fstrs, &params,
                          rd_icon_kind_text_table[rd_icon_kind_from_code_name(result.cmd_name)],
                          .font = rd_font_from_slot(RD_FontSlot_Icons),
                          .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Icons),
                          .color = ui_color_from_name(str8_lit("text")));
      }
      result.expr_fstrs = fstrs;
    }
    else
    {
      result.expr_fstrs = rd_title_fstrs_from_code_name(arena, result.cmd_name);
    }
    result.flags |= UIShell_WatchCellFlag_Button;
  }
  else if(result.file_path.size != 0)
  {
    B32 need_folder = !str8_match(row_info->group_cfg_name, str8_lit("folder"), 0);
    result.expr_fstrs = rd_title_fstrs_from_file_path(arena, result.file_path, need_folder);
    result.flags |= UIShell_WatchCellFlag_Button;
  }
  else
  {
    DR_FStrList expr_fstrs = {0};
    if(cell->flags & UIShell_WatchCellFlag_Expr)
    {
      B32 is_non_code = 0;
      String8 expr_string = {0};
      for(E_Type *t = e_type_from_key(cell->eval.irtree.type_key); t != &e_type_nil; t = e_type_from_key(t->direct_type_key))
      {
        if(t->kind == E_TypeKind_MetaDisplayName)
        {
          is_non_code = 1;
          expr_string = t->name;
          break;
        }
      }
      if(expr_string.size == 0)
      {
        expr_string = cell->eval.string;
        if(!e_key_match(cell->eval.parent_key, e_key_zero()) &&
           !(block_type->flags & E_TypeFlag_EditableChildren) &&
           cell->eval.msgs.max_kind == E_MsgKind_Null)
        {
          E_Expr *notable_expr = cell->eval.expr;
          for(B32 good = 0; !good;)
          {
            switch(notable_expr->kind)
            {
              default:{good = 1;}break;
              case E_ExprKind_Address:
              case E_ExprKind_Deref:
              case E_ExprKind_Cast:{notable_expr = notable_expr->last;}break;
              case E_ExprKind_Ref:{notable_expr = notable_expr->ref;}break;
            }
          }
          switch(notable_expr->kind)
          {
            default:{}break;
            case E_ExprKind_ArrayIndex:
            {
              expr_string = push_str8f(arena, "[%S]", e_string_from_expr(arena, notable_expr->last, str8_zero()));
            }break;
            case E_ExprKind_MemberAccess:
            {
              String8 member_name = notable_expr->first->next->string;
              String8 fancy_name = {0};
              if(cell->eval.space.kind == RD_EvalSpaceKind_MetaCfg ||
                 cell->eval.space.kind == E_SpaceKind_File ||
                 cell->eval.space.kind == E_SpaceKind_FileSystem)
              {
                fancy_name = rd_display_from_code_name(member_name);
              }
              if(fancy_name.size != 0)
              {
                is_non_code = 1;
                expr_string = fancy_name;
              }
              else if(member_name.size != 0)
              {
                expr_string = push_str8f(arena, ".%S", member_name);
              }
            }break;
          }
        }
      }
      if(is_non_code)
      {
        DR_FStrParams params = {rd_font_from_slot(RD_FontSlot_Main), rd_raster_flags_from_slot(RD_FontSlot_Main), ui_color_from_name(str8_lit("text")), font_size, 0, 0};
        dr_fstrs_push_new(arena, &expr_fstrs, &params, expr_string);
      }
      else
      {
        expr_fstrs = rd_fstrs_from_code_string(arena, 1, 0, ui_color_from_name(str8_lit("text")), expr_string);
      }
    }
    
    DR_FStrList eval_fstrs = {0};
    if(!(cell->flags & UIShell_WatchCellFlag_NoEval))
    {
      EV_StringParams string_params = {string_flags, 10};
      if(cell->eval.space.kind == RD_EvalSpaceKind_MetaCfg)
      {
        string_params.flags |= EV_StringFlag_DisableStringQuotes|EV_StringFlag_DisableAddresses;
      }
      B32 is_code = 1;
      {
        E_Type *type = e_type_from_key(e_type_key_unwrap(cell->eval.irtree.type_key, E_TypeUnwrapFlag_Meta));
        if(type->flags & (E_TypeFlag_IsPlainText|E_TypeFlag_IsPathText))
        {
          is_code = 0;
        }
      }
      String8 string = rd_value_string_from_eval(arena, rd_view_query_input(), &string_params, font, font_size, max_size_px, cell->eval);
      if(is_code)
      {
        eval_fstrs = rd_fstrs_from_code_string(arena, 1, 0, ui_color_from_name(str8_lit("text")), string);
      }
      else UI_TagF("weak")
      {
        DR_FStrParams params = {rd_font_from_slot(RD_FontSlot_Main), rd_raster_flags_from_slot(RD_FontSlot_Main), ui_color_from_name(str8_lit("text")), font_size, 0, 0};
        dr_fstrs_push_new(arena, &eval_fstrs, &params, string);
        result.flags |= UIShell_WatchCellFlag_IsNonCode;
      }
    }
    
    if(cell->flags & UIShell_WatchCellFlag_NoEval)
    {
      result.eval_fstrs = expr_fstrs;
    }
    else
    {
      result.expr_fstrs = expr_fstrs;
      result.eval_fstrs = eval_fstrs;
    }
  }
  
  if(cell->eval.space.kind == RD_EvalSpaceKind_MetaTheme)
  {
    String8 name = e_string_from_id(cell->eval.value.u64);
    DR_FStrParams params = {rd_font_from_slot(RD_FontSlot_Main), rd_raster_flags_from_slot(RD_FontSlot_Main), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
    DR_FStrList fstrs = {0};
    UI_TagF("weak")
    {
      dr_fstrs_push_new(arena, &fstrs, &params,
                        rd_icon_kind_text_table[RD_IconKind_Palette],
                        .font = rd_font_from_slot(RD_FontSlot_Icons),
                        .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Icons),
                        .color = ui_color_from_name(str8_lit("text")));
    }
    dr_fstrs_push_new(arena, &fstrs, &params, str8_lit("  "));
    dr_fstrs_push_new(arena, &fstrs, &params, name);
    result.eval_fstrs = fstrs;
  }
  
  scratch_end(scratch);
  return result;
}

internal void
uishell_watch_complete_or_activate(E_Eval eval, String8 cmd_name)
{
  Temp scratch = scratch_begin(0, 0);
  if(cmd_name.size != 0)
  {
    switch(eval.space.kind)
    {
      default:
      {
        String8 string = e_string_from_expr(scratch.arena, eval.expr, str8_zero());
        UIShell_RegsScope(.string = string)
        {
          uishell_cmd("complete_query");
        }
      }break;
      case E_SpaceKind_File:
      case E_SpaceKind_FileSystem:
      {
        E_Type *type = e_type_from_key(eval.irtree.type_key);
        String8 file = rd_file_path_from_eval(scratch.arena, eval);
        if(str8_match(type->name, str8_lit("folder"), 0))
        {
          String8 new_input_string = push_str8f(scratch.arena, "%S/", file);
          UIShell_RegsScope(.string = new_input_string)
          {
            uishell_cmd("update_query");
          }
        }
        else
        {
          UIShell_RegsScope(.file_path = file)
          {
            uishell_cmd("complete_query");
          }
        }
      }break;
      case RD_EvalSpaceKind_MetaCfg:
      {
        CFG_Node *cfg = rd_cfg_from_eval_space(eval.space);
        UIShell_RegsScope(.cfg = cfg->id)
        {
          uishell_cmd("complete_query");
        }
      }break;
      case RD_EvalSpaceKind_MetaCmd:
      {
        String8 selected_cmd_name = rd_cmd_name_from_eval(eval);
        UIShell_RegsScope(.cmd_name = selected_cmd_name)
        {
          uishell_cmd("complete_query");
        }
      }break;
      case RD_EvalSpaceKind_MetaTheme:
      {
        String8 name = e_string_from_id(eval.value.u64);
        UIShell_RegsScope(.string = name)
        {
          uishell_cmd("complete_query");
        }
      }break;
    }
  }
  else
  {
    B32 did_cmd = 1;
    switch(eval.space.kind)
    {
      default:
      {
        String8 expr = e_full_expr_string_from_key(scratch.arena, eval.key);
        UIShell_RegsScope(.expr = expr, .do_implicit_root = 0)
        {
          uishell_cmd("push_query");
        }
      }break;
      case E_SpaceKind_File:
      case E_SpaceKind_FileSystem:
      {
        String8 file = rd_file_path_from_eval(scratch.arena, eval);
        UIShell_RegsScope(.file_path = file)
        {
          uishell_cmd("open");
        }
      }break;
      case RD_EvalSpaceKind_MetaCfg:
      {
        CFG_Node *cfg = rd_cfg_from_eval_space(eval.space);
        if(str8_match(cfg->string, str8_lit("recent_project"), 0))
        {
          UIShell_RegsScope(.cfg = cfg->id)
          {
            uishell_cmd("open_recent_project");
          }
        }
        else if(e_type_kind_from_key(e_type_key_unwrap(eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative)) == E_TypeKind_Set)
        {
          String8 expr = e_full_expr_string_from_key(scratch.arena, eval.key);
          UIShell_RegsScope(.expr = expr, .do_implicit_root = 0)
          {
            uishell_cmd("push_query");
          }
        }
        else
        {
          did_cmd = 0;
        }
      }break;
      case RD_EvalSpaceKind_MetaQuery:
      {
        String8 expr = e_full_expr_string_from_key(scratch.arena, eval.key);
        UIShell_RegsScope(.expr = expr, .do_implicit_root = 0)
        {
          uishell_cmd("push_query");
        }
      }break;
      case RD_EvalSpaceKind_MetaCmd:
      {
        String8 selected_cmd_name = rd_cmd_name_from_eval(eval);
        UIShell_RegsScope(.cmd_name = selected_cmd_name)
        {
          uishell_cmd("run_command");
        }
      }break;
    }
    if(did_cmd)
    {
      uishell_cmd("complete_query");
    }
  }
  scratch_end(scratch);
}


internal void
uishell_watch_view_ui(Rng2F32 rect)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Font(RD_FontSlot_Code)
  {
    CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
    RD_ViewState *vs = rd_view_state_from_cfg(view);
    String8 expr_string = rd_expr_from_cfg(view);
    if(expr_string.size == 0)
    {
      expr_string = str8_lit("query:views");
    }
    E_Eval eval = e_eval_from_string(expr_string);
    UIShell_WatchViewState *wv = rd_view_state(UIShell_WatchViewState);
    UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
    B32 is_first_frame = 0;
    if(wv->initialized == 0)
    {
      is_first_frame = 1;
      wv->initialized = 1;
      wv->filter_arena = rd_push_view_arena();
      wv->text_edit_arena = rd_push_view_arena();
    }
    
    EV_View *eval_view = rd_view_eval_view();
    F32 row_height_px = ui_top_px_height();
    S64 num_possible_visible_rows = (S64)(dim_2f32(rect).y/row_height_px);
    F32 row_string_max_size_px = dim_2f32(rect).x;
    EV_StringFlags string_flags = EV_StringFlag_ReadOnlyDisplayRules|rd_state->eval_viz_base_string_flags;
    String8 filter = rd_view_query_input();
    B32 implicit_root = (cfg_node_child_from_string(view, str8_lit("explicit_root")) == &cfg_nil_node);
    B32 prefer_single_click_to_activate = (cfg_node_child_from_string(view, str8_lit("activate_with_single_click")) != &cfg_nil_node);
    B32 is_lister = (cfg_node_child_from_string(view, str8_lit("lister")) != &cfg_nil_node);
    B32 is_autocomplete = (cfg_node_child_from_string(view, str8_lit("autocomplete")) != &cfg_nil_node);
    
    if(!str8_match(filter, wv->last_filter, 0))
    {
      MemoryZeroStruct(&wv->cursor);
      MemoryZeroStruct(&wv->mark);
      MemoryZeroStruct(&wv->next_cursor);
      MemoryZeroStruct(&wv->next_mark);
      arena_clear(wv->filter_arena);
      wv->last_filter = push_str8_copy(wv->filter_arena, filter);
    }
    
    EV_BlockTree block_tree = {0};
    EV_BlockRangeList block_ranges = {0};
    UI_ScrollListRowBlockArray row_blocks = {0};
    Vec2S64 cursor_tbl = {0};
    Vec2S64 mark_tbl = {0};
    Rng2S64 selection_tbl = {0};
    Rng2S64 cursor_tbl_range = {0};
    
    UI_Focus(UI_FocusKind_On)
    {
      B32 state_dirty = 1;
      B32 snap_to_cursor = 0;
      B32 cursor_dirty__tbl = 0;
      for(UI_Event *event = 0;;)
      {
        if(state_dirty)
        {
          eval = e_eval_from_string(eval.string);
          MemoryZeroStruct(&block_tree);
          MemoryZeroStruct(&block_ranges);
          if(implicit_root || is_first_frame)
          {
            ev_key_set_expansion(eval_view, ev_key_root(), ev_key_make(ev_hash_from_key(ev_key_root()), 1), 1);
          }
          block_tree = ev_block_tree_from_eval(scratch.arena, eval_view, filter, eval);
          block_ranges = ev_block_range_list_from_tree(scratch.arena, &block_tree);
          if(implicit_root && block_ranges.first != 0)
          {
            block_ranges.count -= 1;
            block_ranges.first = block_ranges.first->next;
          }
          state_dirty = 0;
        }
        
        UI_ScrollListRowBlockChunkList row_block_chunks = {0};
        for(EV_BlockRangeNode *n = block_ranges.first; n != 0; n = n->next)
        {
          UI_ScrollListRowBlock block = {0};
          block.row_count = dim_1u64(n->v.range);
          block.item_count = n->v.block->viz_expand_info.single_item ? 1 : dim_1u64(n->v.range);
          ui_scroll_list_row_block_chunk_list_push(scratch.arena, &row_block_chunks, 256, &block);
        }
        row_blocks = ui_scroll_list_row_block_array_from_chunk_list(scratch.arena, &row_block_chunks);
        
        if(cursor_dirty__tbl)
        {
          cursor_dirty__tbl = 0;
          struct { UIShell_WatchPt *pt_state; Vec2S64 pt_tbl; } points[] =
          {
            {&wv->cursor, cursor_tbl},
            {&wv->mark, mark_tbl},
          };
          for(U64 point_idx = 0; point_idx < ArrayCount(points); point_idx += 1)
          {
            EV_Key last_key = points[point_idx].pt_state->key;
            EV_Key last_parent_key = points[point_idx].pt_state->parent_key;
            points[point_idx].pt_state[0] = uishell_watch_pt_from_tbl(&block_ranges, points[point_idx].pt_tbl);
            if(ev_key_match(ev_key_zero(), points[point_idx].pt_state->key) && points[point_idx].pt_tbl.y != 0)
            {
              points[point_idx].pt_state->key = last_parent_key;
              EV_ExpandNode *node = ev_expand_node_from_key(eval_view, last_parent_key);
              for(EV_ExpandNode *n = node; n != 0; n = n->parent)
              {
                points[point_idx].pt_state->key = n->key;
                if(n->expanded == 0)
                {
                  break;
                }
              }
            }
            if(point_idx == 0 &&
               (!ev_key_match(wv->cursor.key, last_key) ||
                !ev_key_match(wv->cursor.parent_key, last_parent_key)))
            {
              wv->text_editing = 0;
            }
          }
          wv->next_cursor = wv->cursor;
          wv->next_mark = wv->mark;
        }
        
        cursor_tbl = uishell_tbl_from_watch_pt(&block_ranges, wv->cursor);
        mark_tbl = uishell_tbl_from_watch_pt(&block_ranges, wv->mark);
        Rng1S64 cursor_x_range = {0};
        {
          EV_Row *row = ev_row_from_num(scratch.arena, eval_view, &block_ranges, mark_tbl.y);
          UIShell_WatchRowInfo row_info = uishell_watch_row_info_from_row(scratch.arena, row);
          cursor_x_range = r1s64(0, (S64)row_info.cells.count-1);
        }
        cursor_tbl_range = r2s64(v2s64(cursor_x_range.min, 0),
                                 v2s64(cursor_x_range.max, (S64)Max(block_tree.total_item_count - !!implicit_root, 0)));
        for EachEnumVal(Axis2, axis)
        {
          cursor_tbl.v[axis] = clamp_1s64(r1s64(cursor_tbl_range.min.v[axis], cursor_tbl_range.max.v[axis]), cursor_tbl.v[axis]);
          mark_tbl.v[axis] = clamp_1s64(r1s64(cursor_tbl_range.min.v[axis], cursor_tbl_range.max.v[axis]), mark_tbl.v[axis]);
        }
        selection_tbl = r2s64p(Min(cursor_tbl.x, mark_tbl.x), Min(cursor_tbl.y, mark_tbl.y),
                               Max(cursor_tbl.x, mark_tbl.x), Max(cursor_tbl.y, mark_tbl.y));
        
        if(snap_to_cursor)
        {
          Rng1S64 global_vnum_range = r1s64(1, block_tree.total_row_count+1);
          if(contains_1s64(global_vnum_range, cursor_tbl.y))
          {
            UI_ScrollPt *scroll_pt = &scroll_pos.y;
            Rng1S64 visible_row_num_range = r1s64(scroll_pt->idx + 1 - !!(scroll_pt->off < 0),
                                                  scroll_pt->idx + 1 + num_possible_visible_rows);
            Rng1S64 cursor_visibility_row_num_range = {0};
            cursor_visibility_row_num_range.min = ev_vnum_from_num(&block_ranges, cursor_tbl.y) - 1;
            cursor_visibility_row_num_range.max = cursor_visibility_row_num_range.min + 3;
            S64 min_delta = Min(0, cursor_visibility_row_num_range.min-visible_row_num_range.min);
            S64 max_delta = Max(0, cursor_visibility_row_num_range.max-visible_row_num_range.max);
            S64 new_num = (S64)scroll_pt->idx + 1 + min_delta + max_delta;
            new_num = clamp_1s64(global_vnum_range, new_num);
            if(new_num > 0)
            {
              ui_scroll_pt_target_idx(scroll_pt, (U64)(new_num - 1));
            }
          }
        }
        
        B32 cursor_rugpull = 0;
        if(!uishell_watch_pt_match(wv->cursor, wv->next_cursor))
        {
          cursor_rugpull = 1;
          wv->cursor = wv->next_cursor;
          wv->mark = wv->next_mark;
        }
        
        B32 next_event_good = ui_next_event(&event);
        if(!cursor_rugpull && (!next_event_good || !ui_is_focus_active()))
        {
          break;
        }
        UI_Event dummy_evt = zero_struct;
        UI_Event *evt = next_event_good ? event : &dummy_evt;
        B32 taken = 0;
        
        if(evt->kind == UI_EventKind_Press &&
           evt->slot == UI_EventActionSlot_Accept &&
           selection_tbl.min.y == selection_tbl.max.y &&
           is_lister)
        {
          EV_Row *row = (selection_tbl.min.y == 0 && selection_tbl.max.y == 0
                         ? ev_row_from_num(scratch.arena, eval_view, &block_ranges, 1)
                         : ev_row_from_num(scratch.arena, eval_view, &block_ranges, selection_tbl.min.y));
          if(row->eval.expr != &e_expr_nil)
          {
            taken = 1;
            uishell_watch_complete_or_activate(row->eval, rd_view_query_cmd());
          }
        }
        
        if(!wv->text_editing &&
           (evt->kind == UI_EventKind_Text ||
            evt->flags & UI_EventFlag_Paste ||
            (evt->kind == UI_EventKind_Press && evt->slot == UI_EventActionSlot_Edit)) &&
           selection_tbl.min.x == selection_tbl.max.x &&
           (selection_tbl.min.y != 0 || selection_tbl.max.y != 0))
        {
          Vec2S64 selection_dim = dim_2s64(selection_tbl);
          arena_clear(wv->text_edit_arena);
          wv->text_edit_state_slots_count = u64_up_to_pow2(selection_dim.y+1);
          wv->text_edit_state_slots_count = Max(wv->text_edit_state_slots_count, 64);
          wv->text_edit_state_slots = push_array(wv->text_edit_arena, UIShell_WatchViewTextEditState*, wv->text_edit_state_slots_count);
          EV_WindowedRowList rows = ev_rows_from_num_range(scratch.arena, eval_view, &block_ranges, r1u64(selection_tbl.min.y, selection_tbl.max.y+1));
          EV_WindowedRowNode *row_node = rows.first;
          B32 any_edits_started = 0;
          for(S64 y = selection_tbl.min.y; row_node != 0 && y <= selection_tbl.max.y; y += 1, row_node = row_node->next)
          {
            EV_Row *row = &row_node->row;
            UIShell_WatchRowInfo row_info = uishell_watch_row_info_from_row(scratch.arena, row);
            S64 cell_x = 0;
            for(UIShell_WatchCell *cell = row_info.cells.first; cell != 0; cell = cell->next, cell_x += 1)
            {
              if(cell_x < selection_tbl.min.x || selection_tbl.max.x < cell_x)
              {
                continue;
              }
              UIShell_WatchRowCellInfo cell_info = uishell_info_from_watch_row_cell(scratch.arena, row, string_flags & (~EV_StringFlag_ReadOnlyDisplayRules), &row_info, cell, ui_top_font(), ui_top_font_size(), row_string_max_size_px);
              if(cell_info.flags & UIShell_WatchCellFlag_CanEdit)
              {
                any_edits_started = 1;
                String8 string = (cell_info.flags & UIShell_WatchCellFlag_NoEval) ? cell->eval.string : dr_string_from_fstrs(scratch.arena, &cell_info.eval_fstrs);
                string.size = Min(string.size, sizeof(wv->dummy_text_edit_state.input_buffer));
                UIShell_WatchPt pt = {row->block->key, row->key, uishell_id_from_watch_cell(cell)};
                U64 slot_idx = ev_hash_from_key(pt.key)%wv->text_edit_state_slots_count;
                UIShell_WatchViewTextEditState *edit_state = push_array(wv->text_edit_arena, UIShell_WatchViewTextEditState, 1);
                SLLStackPush_N(wv->text_edit_state_slots[slot_idx], edit_state, pt_hash_next);
                edit_state->pt = pt;
                edit_state->cursor = txt_pt(1, string.size+1);
                edit_state->mark = txt_pt(1, 1);
                edit_state->input_size = string.size;
                MemoryCopy(edit_state->input_buffer, string.str, string.size);
                edit_state->initial_size = string.size;
                MemoryCopy(edit_state->initial_buffer, string.str, string.size);
              }
            }
          }
          wv->text_editing = any_edits_started;
        }
        
        if(wv->text_editing)
        {
          B32 editing_complete = ((evt->kind == UI_EventKind_Press && (evt->slot == UI_EventActionSlot_Cancel || evt->slot == UI_EventActionSlot_Accept)) ||
                                  (evt->kind == UI_EventKind_Navigate && evt->delta_2s32.y != 0) ||
                                  cursor_rugpull);
          rd_state->text_edit_mode = 1;
          if(editing_complete ||
             ((evt->kind == UI_EventKind_Edit ||
               evt->kind == UI_EventKind_Navigate ||
               evt->kind == UI_EventKind_Text) &&
              evt->delta_2s32.y == 0))
          {
            taken = 1;
            EV_WindowedRowList rows = ev_rows_from_num_range(scratch.arena, eval_view, &block_ranges, r1u64(selection_tbl.min.y, selection_tbl.max.y+1));
            EV_WindowedRowNode *row_node = rows.first;
            for(S64 y = selection_tbl.min.y; row_node != 0 && y <= selection_tbl.max.y; y += 1, row_node = row_node->next)
            {
              EV_Row *row = &row_node->row;
              UIShell_WatchRowInfo row_info = uishell_watch_row_info_from_row(scratch.arena, row);
              S64 cell_x = 0;
              for(UIShell_WatchCell *cell = row_info.cells.first; cell != 0; cell = cell->next, cell_x += 1)
              {
                if(cell_x < selection_tbl.min.x || selection_tbl.max.x < cell_x)
                {
                  continue;
                }
                UIShell_WatchPt pt = {row->block->key, row->key, uishell_id_from_watch_cell(cell)};
                UIShell_WatchViewTextEditState *edit_state = uishell_watch_view_text_edit_state_from_pt(wv, pt);
                String8 string = str8(edit_state->input_buffer, edit_state->input_size);
                UI_TxtOp op = ui_single_line_txt_op_from_event(scratch.arena, evt, string, edit_state->cursor, edit_state->mark);
                if(op.flags & UI_TxtOpFlag_Copy && selection_tbl.min.x == selection_tbl.max.x && selection_tbl.min.y == selection_tbl.max.y)
                {
                  wm_set_clipboard_text(op.copy);
                }
                if(editing_complete && evt->slot == UI_EventActionSlot_Cancel)
                {
                  string = str8(edit_state->initial_buffer, edit_state->initial_size);
                }
                String8 new_string = string;
                if(!txt_pt_match(op.range.min, op.range.max) || op.replace.size != 0)
                {
                  new_string = ui_push_string_replace_range(scratch.arena, string, r1s64(op.range.min.column, op.range.max.column), op.replace);
                }
                new_string.size = Min(new_string.size, sizeof(edit_state->input_buffer));
                MemoryCopy(edit_state->input_buffer, new_string.str, new_string.size);
                edit_state->input_size = new_string.size;
                edit_state->cursor = op.cursor;
                edit_state->mark = op.mark;
                
                if(cell->kind == UIShell_WatchCellKind_Eval)
                {
                  if(cell->flags & UIShell_WatchCellFlag_Expr && cell->flags & UIShell_WatchCellFlag_NoEval)
                  {
                    CFG_Node *cfg = row_info.group_cfg_child;
                    String8 child_key = {0};
                    if(cfg == &cfg_nil_node && editing_complete && new_string.size != 0)
                    {
                      CFG_Node *new_cfg_parent = row_info.group_cfg_parent;
                      if(new_cfg_parent != &cfg_nil_node)
                      {
                        child_key = str8_zero();
                      }
                      if(new_cfg_parent == &cfg_nil_node)
                      {
                        CFG_NodePtrList all_cfgs = cfg_node_top_level_list_from_string(scratch.arena, row_info.group_cfg_name);
                        new_cfg_parent = cfg_node_ptr_list_last(&all_cfgs)->parent;
                      }
                      if(new_cfg_parent == &cfg_nil_node)
                      {
                        new_cfg_parent = cfg_node_child_from_string(cfg_node_root(), str8_lit("project"));
                      }
                      cfg = cfg_node_new(rd_state->cfg, new_cfg_parent, row_info.group_cfg_name);
                      state_dirty = 1;
                      snap_to_cursor = 1;
                    }
                    if(cfg != &cfg_nil_node)
                    {
                      CFG_Node *expr = child_key.size != 0 ? cfg_node_child_from_string_or_alloc(rd_state->cfg, cfg, child_key) : cfg;
                      cfg_node_new_replace(rd_state->cfg, expr, new_string);
                    }
                  }
                  else
                  {
                    B32 should_commit_asap = editing_complete;
                    if(cell->eval.space.kind == RD_EvalSpaceKind_MetaCfg)
                    {
                      should_commit_asap = 1;
                    }
                    else if(evt->slot != UI_EventActionSlot_Cancel)
                    {
                      should_commit_asap = editing_complete;
                    }
                    if(should_commit_asap)
                    {
                      B32 success = rd_commit_eval_value_string(cell->eval, new_string);
                      state_dirty = 1;
                      if(!success)
                      {
                        log_user_error(str8_lit("Could not commit value successfully."));
                      }
                    }
                  }
                }
              }
            }
          }
          if(editing_complete)
          {
            wv->text_editing = 0;
          }
        }
        
        if(!wv->text_editing && !(evt->flags & UI_EventFlag_Delete) && !(evt->flags & UI_EventFlag_Reorder))
        {
          B32 cursor_tbl_min_is_empty_selection[Axis2_COUNT] = {0, 1};
          Vec2S32 delta = evt->delta_2s32;
          if(evt->flags & UI_EventFlag_PickSelectSide && !MemoryMatchStruct(&selection_tbl.min, &selection_tbl.max))
          {
            if(delta.x > 0 || delta.y > 0)
            {
              cursor_tbl.x = selection_tbl.max.x;
              cursor_tbl.y = selection_tbl.max.y;
            }
            else if(delta.x < 0 || delta.y < 0)
            {
              cursor_tbl.x = selection_tbl.min.x;
              cursor_tbl.y = selection_tbl.min.y;
            }
          }
          if(evt->flags & UI_EventFlag_ZeroDeltaOnSelect && !MemoryMatchStruct(&selection_tbl.min, &selection_tbl.max))
          {
            MemoryZeroStruct(&delta);
          }
          B32 moved = 1;
          switch(evt->delta_unit)
          {
            default:{moved = 0;}break;
            case UI_EventDeltaUnit_Char:
            {
              for EachEnumVal(Axis2, axis)
              {
                cursor_tbl.v[axis] += delta.v[axis];
                if(cursor_tbl.v[axis] < cursor_tbl_range.min.v[axis]) { cursor_tbl.v[axis] = cursor_tbl_range.max.v[axis]; }
                if(cursor_tbl.v[axis] > cursor_tbl_range.max.v[axis]) { cursor_tbl.v[axis] = cursor_tbl_range.min.v[axis]; }
                cursor_tbl.v[axis] = clamp_1s64(r1s64(cursor_tbl_range.min.v[axis], cursor_tbl_range.max.v[axis]), cursor_tbl.v[axis]);
              }
            }break;
            case UI_EventDeltaUnit_Word:
            case UI_EventDeltaUnit_Line:
            case UI_EventDeltaUnit_Page:
            {
              cursor_tbl.x = (delta.x > 0 ? cursor_tbl_range.max.x :
                              delta.x < 0 ? cursor_tbl_range.min.x + !!cursor_tbl_min_is_empty_selection[Axis2_X] :
                              cursor_tbl.x);
              cursor_tbl.y += ((delta.y > 0 ? +(num_possible_visible_rows-3) :
                                delta.y < 0 ? -(num_possible_visible_rows-3) :
                                0));
              cursor_tbl.y = clamp_1s64(r1s64(cursor_tbl_range.min.y + !!cursor_tbl_min_is_empty_selection[Axis2_Y],
                                              cursor_tbl_range.max.y),
                                        cursor_tbl.y);
            }break;
            case UI_EventDeltaUnit_Whole:
            {
              for EachEnumVal(Axis2, axis)
              {
                cursor_tbl.v[axis] = (delta.v[0] > 0 ? cursor_tbl_range.max.v[axis] :
                                      delta.v[0] < 0 ? cursor_tbl_range.min.v[axis] + !!cursor_tbl_min_is_empty_selection[axis] :
                                      cursor_tbl.v[axis]);
              }
            }break;
          }
          if(moved)
          {
            taken = 1;
            cursor_dirty__tbl = 1;
            snap_to_cursor = 1;
          }
        }
        if(!wv->text_editing && taken && !(evt->flags & UI_EventFlag_KeepMark))
        {
          mark_tbl = cursor_tbl;
        }
        if(taken && evt != &dummy_evt)
        {
          ui_eat_event(evt);
        }
      }
    }
    
    if(uishell_watch_pt_match(wv->cursor, wv->mark) && is_autocomplete)
    {
      U64 row_num = ev_num_from_key(&block_ranges, wv->cursor.key);
      EV_Row *row = ev_row_from_num(scratch.arena, rd_view_eval_view(), &block_ranges, row_num);
      UIShell_WatchRowInfo row_info = uishell_watch_row_info_from_row(scratch.arena, row);
      UIShell_WatchCell *cell = row_info.cells.first;
      if(cell != 0)
      {
        UIShell_WatchRowCellInfo cell_info = uishell_info_from_watch_row_cell(scratch.arena, row, 0, &row_info, cell, ui_top_font(), ui_top_font_size(), dim_2f32(rect).y);
        String8 string = dr_string_from_fstrs(ui_build_arena(), &cell_info.eval_fstrs);
        if(string.size != 0)
        {
          ui_set_autocomplete_string(string);
        }
      }
    }
    
    B32 pressed = 0;
    Vec2F32 rect_dim = dim_2f32(rect);
    F32 contents_width_px = Max(0, rect_dim.x - floor_f32(ui_bottom_font_size()*1.5f));
    Rng1S64 visible_row_rng = {0};
    UI_ScrollListParams scroll_list_params = {0};
    scroll_list_params.flags = UI_ScrollListFlag_All;
    scroll_list_params.row_height_px = row_height_px;
    scroll_list_params.dim_px = rect_dim;
    scroll_list_params.cursor_range = r2s64(v2s64(0, 0), v2s64(0, 0));
    scroll_list_params.item_range = r1s64(0, block_tree.total_row_count - !!implicit_root);
    scroll_list_params.cursor_min_is_empty_selection[Axis2_Y] = 1;
    scroll_list_params.row_blocks = row_blocks;
    
    UI_ScrollListSignal scroll_list_sig = {0};
    UI_Focus(UI_FocusKind_On)
      UI_ScrollList(&scroll_list_params, &scroll_pos.y, 0, 0, &visible_row_rng, &scroll_list_sig)
      UI_Focus(UI_FocusKind_Null)
    {
      ui_set_next_pref_height(ui_children_sum(1));
      ui_set_next_child_layout_axis(Axis2_Y);
      UI_Box *table = ui_build_box_from_string(0, str8_lit("table"));
      UI_Parent(table)
      {
        EV_WindowedRowList rows = ev_windowed_row_list_from_block_range_list(scratch.arena, eval_view, &block_ranges, r1u64(visible_row_rng.min+1, visible_row_rng.max+2));
        UIShell_WatchRowInfo *row_infos = push_array(scratch.arena, UIShell_WatchRowInfo, rows.count);
        U64 idx = 0;
        for(EV_WindowedRowNode *row_node = rows.first; row_node != 0; row_node = row_node->next, idx += 1)
        {
          row_infos[idx] = uishell_watch_row_info_from_row(scratch.arena, &row_node->row);
        }
        
        U64 local_row_idx = 0;
        U64 global_row_idx = rows.count_before_semantic;
        UIShell_WatchRowInfo last_row_info = {0};
        for(EV_WindowedRowNode *row_node = rows.first; row_node != 0; row_node = row_node->next, local_row_idx += 1, global_row_idx += 1)
        {
          EV_Row *row = &row_node->row;
          UIShell_WatchRowInfo *row_info = &row_infos[local_row_idx];
          U64 row_hash = ev_hash_from_key(row->key);
          U64 row_depth = ev_depth_from_block(row->block);
          B32 row_selected = (selection_tbl.min.y <= global_row_idx+1 && global_row_idx+1 <= selection_tbl.max.y);
          B32 row_expanded = ev_expansion_from_key(eval_view, row->key);
          B32 next_row_expanded = row_expanded;
          if(implicit_root && row_depth > 0)
          {
            row_depth -= 1;
          }
          
          B32 row_matches_last_row_topology = 1;
          if(row_node != rows.first)
          {
            for(UIShell_WatchCell *last_cell = last_row_info.cells.first, *this_cell = row_info->cells.first;;
                last_cell = last_cell->next, this_cell = this_cell->next)
            {
              if(last_cell == 0 && this_cell == 0) { break; }
              if((last_cell == 0 && this_cell != 0) || (last_cell != 0 && this_cell == 0) ||
                 uishell_id_from_watch_cell(last_cell) != uishell_id_from_watch_cell(this_cell))
              {
                row_matches_last_row_topology = 0;
                break;
              }
            }
          }
          last_row_info = *row_info;
          
          UI_BoxFlags row_flags = UI_BoxFlag_DisableFocusOverlay;
          if(global_row_idx & 1)
          {
            ui_set_next_tag(str8_lit("alt"));
            row_flags |= UI_BoxFlag_DrawBackground;
          }
          if(!row_matches_last_row_topology)
          {
            row_flags |= UI_BoxFlag_DrawSideTop;
          }
          ui_set_next_pref_width(ui_px(contents_width_px, 1.f));
          ui_set_next_pref_height(ui_px(row_height_px*row->visual_size, 1.f));
          ui_set_next_focus_hot(row_selected ? UI_FocusKind_On : UI_FocusKind_Off);
          UI_Box *row_box = ui_build_box_from_stringf(row_flags|((!row_node->next)*UI_BoxFlag_DrawSideBottom)|UI_BoxFlag_Clickable, "row_%I64x", row_hash);
          
          UI_Parent(row_box)
          {
            S64 cell_x = 0;
            F32 cell_x_px = 0;
            for(UIShell_WatchCell *cell = row_info->cells.first; cell != 0; cell = cell->next, cell_x += 1)
            {
              if(row_depth > 0) { ui_push_tagf("weak"); }
              F32 cell_width_px = cell->px != 0 ? cell->px : cell->pct*contents_width_px;
              F32 next_cell_x_px = cell_x_px + cell_width_px;
              F32 cell_width_strictness = cell->px != 0 ? 1.f : 0.f;
              F32 visual_row_string_max_size_px = cell_width_px * 1.5f;
              if(cell->flags & UIShell_WatchCellFlag_Expr && !(cell->flags & UIShell_WatchCellFlag_NoEval))
              {
                visual_row_string_max_size_px /= 2.f;
              }
              U64 cell_id = uishell_id_from_watch_cell(cell);
              UIShell_WatchPt cell_pt = {row->block->key, row->key, cell_id};
              UIShell_WatchViewTextEditState *cell_edit_state = uishell_watch_view_text_edit_state_from_pt(wv, cell_pt);
              B32 cell_selected = (row_selected && selection_tbl.min.x <= cell_x && cell_x <= selection_tbl.max.x);
              UIShell_WatchRowCellInfo cell_info = uishell_info_from_watch_row_cell(scratch.arena, row, string_flags, row_info, cell, ui_top_font(), ui_top_font_size(), visual_row_string_max_size_px);
              E_Type *cell_type = e_type_from_key(cell->eval.irtree.type_key);
              E_Eval cell_value_eval = e_value_eval_from_eval(cell->eval);
              B32 cell_toggled = (cell_value_eval.value.u64 != 0);
              B32 next_cell_toggled = cell_toggled;
              
              E_Value cell_slider_min = zero_struct;
              E_Value cell_slider_max = zero_struct;
              E_TypeKind slider_value_type_kind = E_TypeKind_Null;
              F32 cell_slider_value = 0.f;
              if(str8_match(cell_type->name, str8_lit("range1"), 0) && cell_type->args != 0 && cell_type->count >= 2)
              {
                E_Key min_key = e_key_from_expr(cell_type->args[0]);
                E_Key max_key = e_key_from_expr(cell_type->args[1]);
                E_ParentKey(cell->eval.key)
                {
                  E_TypeKey slider_value_type = e_type_key_unwrap(cell_type->direct_type_key, E_TypeUnwrapFlag_AllDecorative);
                  slider_value_type_kind = e_type_kind_from_key(slider_value_type);
                  String8 slider_type_name = e_type_string_from_key(scratch.arena, slider_value_type);
                  cell_slider_min = e_value_from_key(e_key_wrapf(min_key, "(%S)$", slider_type_name));
                  cell_slider_max = e_value_from_key(e_key_wrapf(max_key, "(%S)$", slider_type_name));
                }
              }
              switch(slider_value_type_kind)
              {
                default:
                if(e_type_kind_is_integer(slider_value_type_kind))
                {
                  cell_slider_value = ((F32)(cell_value_eval.value.s64 - cell_slider_min.s64)) / (cell_slider_max.s64 - cell_slider_min.s64);
                }break;
                case E_TypeKind_F32:
                {
                  cell_slider_value = (cell_value_eval.value.f32 - cell_slider_min.f32) / (cell_slider_max.f32 - cell_slider_min.f32);
                }break;
                case E_TypeKind_F64:
                {
                  cell_slider_value = (F32)((cell_value_eval.value.f64 - cell_slider_min.f64) / (cell_slider_max.f64 - cell_slider_min.f64));
                }break;
              }
              F32 next_cell_slider_value = cell_slider_value;
              
              UI_Box *cell_box = &ui_nil_box;
              UI_PrefWidth(ui_px(cell_width_px, cell_width_strictness))
              {
                ui_set_next_fixed_height(floor_f32(row->visual_size * row_height_px));
                cell_box = ui_build_box_from_stringf(UI_BoxFlag_DrawSideLeft, "cell_%I64x_%I64x", row_hash, cell_id);
              }
              
              B32 revert_cell = 0;
              UI_Signal sig = {0};
              UI_Parent(cell_box)
                UI_FocusHot(cell_selected ? UI_FocusKind_On : UI_FocusKind_Off)
                UI_FocusActive((cell_selected && wv->text_editing) ? UI_FocusKind_On : UI_FocusKind_Off)
                RD_Font(RD_FontSlot_Code)
                UI_TagF("weak")
              {
                if(cell->kind == UIShell_WatchCellKind_ViewUI && cell_info.view_ui_rule != &rd_nil_view_ui_rule)
                {
                  Rng2F32 cell_rect = r2f32p(cell_x_px, 0, next_cell_x_px, row_height_px*row->visual_size);
                  UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clip|UI_BoxFlag_Clickable, "###val_%I64x", row_hash);
                  UI_Parent(box) E_ParentKey(cell->eval.key)
                  {
                    cell_info.view_ui_rule->ui(cell->eval, cell_rect);
                  }
                  sig = ui_signal_from_box(box);
                }
                else
                {
                  B32 cell_has_fancy_editors = (!(cell->flags & UIShell_WatchCellFlag_NoEval));
                  B32 is_button = !!(cell_info.flags & UIShell_WatchCellFlag_Button);
                  B32 has_background = !!(cell_info.flags & UIShell_WatchCellFlag_Background);
                  B32 is_toggle_switch = (cell_has_fancy_editors && cell->eval.irtree.mode != E_Mode_Null && e_type_kind_from_key(e_type_key_unwrap(cell->eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative)) == E_TypeKind_Bool);
                  B32 is_slider = (cell_has_fancy_editors && cell->eval.irtree.mode != E_Mode_Null && cell_type->kind == E_TypeKind_Lens && str8_match(cell_type->name, str8_lit("range1"), 0));
                  B32 is_activated_on_single_click = !!(cell_info.flags & UIShell_WatchCellFlag_ActivateWithSingleClick);
                  B32 is_non_code = !!(cell_info.flags & UIShell_WatchCellFlag_IsNonCode);
                  String8 ghost_text = {0};
                  if(cell_selected && wv->text_editing && cell->flags & UIShell_WatchCellFlag_Expr && cell->flags & UIShell_WatchCellFlag_NoEval)
                  {
                    is_non_code = 0;
                    is_button = 0;
                    is_activated_on_single_click = 0;
                  }
                  
                  String8 needle = rd_view_query_input();
                  if(cell->eval.space.kind == E_SpaceKind_FileSystem)
                  {
                    needle = str8_skip_last_slash(needle);
                  }
                  
                  UI_Key line_edit_key = {0};
                  RD_CellParams cell_params = {0};
                  cell_params.flags = RD_CellFlag_KeyboardClickable|RD_CellFlag_NoBackground|RD_CellFlag_CodeContents;
                  cell_params.depth = (cell->flags & UIShell_WatchCellFlag_Indented) ? (S32)row_depth : 0;
                  cell_params.cursor = &cell_edit_state->cursor;
                  cell_params.mark = &cell_edit_state->mark;
                  cell_params.edit_buffer = cell_edit_state->input_buffer;
                  cell_params.edit_buffer_size = sizeof(cell_edit_state->input_buffer);
                  cell_params.edit_string_size_out = &cell_edit_state->input_size;
                  cell_params.line_edit_key_out = &line_edit_key;
                  cell_params.expanded_out = &next_row_expanded;
                  cell_params.search_needle = needle;
                  cell_params.meta_fstrs = cell_info.expr_fstrs;
                  cell_params.value_fstrs = cell_info.eval_fstrs;
                  if(row_height_px > ui_top_font_size()*3.5f)
                  {
                    cell_params.description = cell_info.description;
                  }
                  cell_params.revert_out = &revert_cell;
                  if(cell_selected && wv->text_editing && cell->flags & UIShell_WatchCellFlag_NoEval)
                  {
                    MemoryZeroStruct(&cell_params.meta_fstrs);
                    MemoryZeroStruct(&cell_params.description);
                  }
                  if(cell->eval.space.kind == RD_EvalSpaceKind_MetaCfg)
                  {
                    cell_params.flags |= RD_CellFlag_EmptyEditButton;
                  }
                  if(cell->eval.space.kind == RD_EvalSpaceKind_MetaCfg && !(cell->flags & UIShell_WatchCellFlag_NoEval))
                  {
                    CFG_Node *cfg = rd_cfg_from_eval_space(cell->eval.space);
                    String8 child_key = e_string_from_id(cell->eval.space.u64s[1]);
                    CFG_Node *child_cfg = cfg_node_child_from_string(cfg, child_key);
                    if(child_cfg != &cfg_nil_node)
                    {
                      MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, cfg->string);
                      if(schemas.count != 0)
                      {
                        MD_Node *child_schema = &md_nil_node;
                        for(MD_NodePtrNode *n = schemas.first; md_node_is_nil(child_schema) && n != 0; n = n->next)
                        {
                          child_schema = md_child_from_string(n->v, child_key, 0);
                        }
                        if((md_node_has_tag(child_schema, str8_lit("override"), 0) ||
                            md_node_has_tag(child_schema, str8_lit("default"), 0)) &&
                           !md_node_has_tag(child_schema, str8_lit("no_revert"), 0))
                        {
                          cell_params.flags |= RD_CellFlag_RevertButton;
                        }
                      }
                    }
                  }
                  if(is_toggle_switch)
                  {
                    cell_params.flags |= RD_CellFlag_ToggleSwitch;
                    cell_params.toggled_out = &next_cell_toggled;
                  }
                  if(is_slider)
                  {
                    cell_params.flags |= RD_CellFlag_Slider;
                    cell_params.slider_value_out = &next_cell_slider_value;
                  }
                  if(cell->px == 0 && cell->eval.space.kind == RD_EvalSpaceKind_MetaCmd)
                  {
                    cell_params.flags |= RD_CellFlag_Bindings;
                    cell_params.bindings_name = rd_cmd_name_from_eval(cell->eval);
                  }
                  if(is_non_code) { cell_params.flags &= ~RD_CellFlag_CodeContents; }
                  if(is_button)
                  {
                    cell_params.flags |= RD_CellFlag_Button;
                    cell_params.flags &= ~RD_CellFlag_NoBackground;
                  }
                  if(is_activated_on_single_click) { cell_params.flags |= RD_CellFlag_SingleClickActivate; }
                  if(has_background) { cell_params.flags &= ~RD_CellFlag_NoBackground; }
                  if(row_info->can_expand && cell == row_info->cells.first && !(cell_selected && wv->text_editing))
                  {
                    cell_params.flags |= RD_CellFlag_Expander;
                    cell_params.expanded_out = &next_row_expanded;
                  }
                  else if(row_depth != 0 && cell == row_info->cells.first)
                  {
                    cell_params.flags |= RD_CellFlag_ExpanderSpace;
                  }
                  
                  UI_TextAlignment(cell->px != 0 ? UI_TextAlign_Center : UI_TextAlign_Left)
                    RD_Font(is_non_code ? RD_FontSlot_Main : RD_FontSlot_Code)
                  {
                    sig = rd_cellf(&cell_params, "%S###%I64x_row_%I64x", ghost_text, cell_x, row_hash);
                  }
                  if(ui_is_focus_active() &&
                     selection_tbl.min.x == selection_tbl.max.x && selection_tbl.min.y == selection_tbl.max.y &&
                     txt_pt_match(cell_edit_state->cursor, cell_edit_state->mark))
                  {
                    String8 input = str8(cell_edit_state->input_buffer, cell_edit_state->input_size);
                    rd_set_autocomp_regs(cell->eval, .ui_key = line_edit_key, .string = input, .cursor = cell_edit_state->cursor);
                  }
                }
                
                if(!(cell_info.flags & UIShell_WatchCellFlag_ActivateWithSingleClick) && ui_pressed(sig))
                {
                  wv->next_cursor = wv->next_mark = cell_pt;
                  pressed = 1;
                }
                if(revert_cell && cell->eval.space.kind == RD_EvalSpaceKind_MetaCfg)
                {
                  CFG_Node *cfg = rd_cfg_from_eval_space(cell->eval.space);
                  String8 child_key = e_string_from_id(cell->eval.space.u64s[1]);
                  cfg_node_release(rd_state->cfg, cfg_node_child_from_string(cfg, child_key));
                }
                if((!(cell_info.flags & UIShell_WatchCellFlag_ActivateWithSingleClick) && ui_double_clicked(sig)) ||
                   ((cell_info.flags & UIShell_WatchCellFlag_ActivateWithSingleClick) && ui_clicked(sig)) ||
                   (prefer_single_click_to_activate && ui_pressed(sig)) ||
                   sig.f & UI_SignalFlag_KeyboardPressed)
                {
                  if(!(cell_info.flags & UIShell_WatchCellFlag_ActivateWithSingleClick))
                  {
                    ui_kill_action();
                  }
                  if(is_lister || is_autocomplete)
                  {
                    wv->next_cursor = wv->next_mark = cell_pt;
                    if(cell_info.flags & UIShell_WatchCellFlag_CanEdit)
                    {
                      uishell_cmd("edit");
                      uishell_cmd("edit");
                    }
                    else
                    {
                      uishell_cmd("edit");
                      wv->next_cursor = wv->next_mark = cell_pt;
                      uishell_cmd("accept");
                    }
                  }
                  else if(cell_info.cmd_name.size != 0)
                  {
                    CFG_Node *cfg = rd_cfg_from_eval_space(row->eval.space);
                    if(cfg == &cfg_nil_node)
                    {
                      cfg = rd_cfg_from_eval_space(row->block->eval.space);
                    }
                    UIShell_RegsScope(.cfg = cfg->id)
                    {
                      if(cfg != &cfg_nil_node)
                      {
                        UIShell_WorkspaceMount workspace_mount = uishell_workspace_mount_from_cfg(scratch.arena, cfg);
                        CFG_PanelTree panels = workspace_mount.panel_tree;
                        CFG_PanelNode *parent_panel_node = cfg_panel_node_from_tree_cfg(panels.root, cfg->parent);
                        if(parent_panel_node != &cfg_nil_panel_node)
                        {
                          uishell_regs()->tab = cfg->id;
                          uishell_regs()->view = cfg->id;
                        }
                      }
                      uishell_push_cmd_current(cell_info.cmd_name);
                    }
                  }
                  else if(!(sig.f & UI_SignalFlag_KeyboardPressed) && cell_info.flags & UIShell_WatchCellFlag_CanEdit)
                  {
                    wv->next_cursor = wv->next_mark = cell_pt;
                    if(!uishell_watch_pt_match(wv->cursor, cell_pt))
                    {
                      uishell_cmd("edit");
                    }
                    uishell_cmd("edit");
                  }
                  else if(sig.f & UI_SignalFlag_KeyboardPressed && row_info->can_expand)
                  {
                    next_row_expanded = !row_expanded;
                  }
                  else
                  {
                    uishell_watch_complete_or_activate(cell->eval, rd_view_query_cmd());
                  }
                }
              }
              
              if(next_cell_toggled != cell_toggled)
              {
                rd_commit_eval_value_string(cell->eval, next_cell_toggled ? str8_lit("1") : str8_lit("0"));
              }
              if(next_cell_slider_value != cell_slider_value)
              {
                String8 new_value_string = {0};
                switch(slider_value_type_kind)
                {
                  default:
                  if(e_type_kind_is_integer(slider_value_type_kind))
                  {
                    S64 new_value = (S64)((next_cell_slider_value * (cell_slider_max.s64 - cell_slider_min.s64)) + cell_slider_min.s64);
                    new_value = Clamp(cell_slider_min.s64, new_value, cell_slider_max.s64);
                    new_value_string = push_str8f(scratch.arena, "%I64d", new_value);
                  }break;
                  case E_TypeKind_F32:
                  {
                    F32 new_value = (next_cell_slider_value * (cell_slider_max.f32 - cell_slider_min.f32)) + cell_slider_min.f32;
                    new_value = Clamp(cell_slider_min.f32, new_value, cell_slider_max.f32);
                    new_value_string = push_str8f(scratch.arena, "%f", new_value);
                  }break;
                  case E_TypeKind_F64:
                  {
                    F64 new_value = (F64)((next_cell_slider_value * (cell_slider_max.f64 - cell_slider_min.f64)) + cell_slider_min.f64);
                    new_value = Clamp(cell_slider_min.f64, new_value, cell_slider_max.f64);
                    new_value_string = push_str8f(scratch.arena, "%f", new_value);
                  }break;
                }
                rd_commit_eval_value_string(cell->eval, new_value_string);
              }
              
              cell_x_px = next_cell_x_px;
              if(row_depth > 0) { ui_pop_tag(); }
            }
            if(next_row_expanded != row_expanded && !ev_key_match(ev_key_root(), row->key))
            {
              ev_key_set_expansion(eval_view, row->block->key, row->key, next_row_expanded);
            }
          }
        }
      }
    }
    
    if(pressed)
    {
      uishell_cmd("focus_panel");
    }
    vs->contents_are_focused = wv->text_editing;
    rd_store_view_scroll_pos(scroll_pos);
  }
  scratch_end(scratch);
}

internal void
uishell_register_view_ui_rules(Arena *arena, RD_ViewUIRuleMap *map)
{
  rd_view_ui_rule_map_insert(arena, map, str8_lit("text"), RD_VIEW_UI_FUNCTION_NAME(shell_text));
  rd_view_ui_rule_map_insert(arena, map, str8_lit("binary"), RD_VIEW_UI_FUNCTION_NAME(binary));
  rd_view_ui_rule_map_insert(arena, map, str8_lit("bitmap"), RD_VIEW_UI_FUNCTION_NAME(bitmap));
  rd_view_ui_rule_map_insert(arena, map, str8_lit("color"), RD_VIEW_UI_FUNCTION_NAME(color));
  rd_view_ui_rule_map_insert(arena, map, str8_lit("geo3d"), RD_VIEW_UI_FUNCTION_NAME(geo3d));
}

internal void
uishell_register_expand_rule_infos(Arena *arena, EV_ExpandRuleTable *table)
{
  ev_expand_rule_table_push_new(arena, table, str8_lit("text"), EV_EXPAND_RULE_INFO_FUNCTION_NAME(shell_text));
  ev_expand_rule_table_push_new(arena, table, str8_lit("bitmap"), EV_EXPAND_RULE_INFO_FUNCTION_NAME(bitmap));
  ev_expand_rule_table_push_new(arena, table, str8_lit("color"), EV_EXPAND_RULE_INFO_FUNCTION_NAME(color));
  ev_expand_rule_table_push_new(arena, table, str8_lit("geo3d"), EV_EXPAND_RULE_INFO_FUNCTION_NAME(geo3d));
}

internal Rng1U64
uishell_binary_selection_from_state(UIShell_BinaryViewState *bv)
{
  Rng1U64 result = r1u64(Min(bv->cursor_off, bv->mark_off),
                         Max(bv->cursor_off, bv->mark_off)+1);
  return result;
}

////////////////////////////////
//~ rjf: Text View

EV_EXPAND_RULE_INFO_FUNCTION_DEF(shell_text)
{
  EV_ExpandInfo info = {0};
  info.row_count = 8;
  info.single_item = 1;
  return info;
}

typedef struct UIShell_TextViewState UIShell_TextViewState;
struct UIShell_TextViewState
{
  B32 initialized;
  B32 contain_cursor;
  B32 center_cursor;
  TxtPt cursor;
  TxtPt mark;
  S64 preferred_column;
};

typedef struct UIShell_TextVisualLine UIShell_TextVisualLine;
struct UIShell_TextVisualLine
{
  S64 line_num;
  Rng1U64 range;
  B32 first_in_line;
};

typedef struct UIShell_TextVisualLineNode UIShell_TextVisualLineNode;
struct UIShell_TextVisualLineNode
{
  UIShell_TextVisualLineNode *next;
  UIShell_TextVisualLine v;
};

typedef struct UIShell_TextVisualLineList UIShell_TextVisualLineList;
struct UIShell_TextVisualLineList
{
  UIShell_TextVisualLineNode *first;
  UIShell_TextVisualLineNode *last;
  U64 count;
};

typedef struct UIShell_TextVisualLineArray UIShell_TextVisualLineArray;
struct UIShell_TextVisualLineArray
{
  UIShell_TextVisualLine *v;
  U64 count;
};

typedef struct UIShell_TextSliceParams UIShell_TextSliceParams;
struct UIShell_TextSliceParams
{
  UIShell_TextVisualLine *lines;
  U64 line_count;
  TXT_TextInfo *text_info;
  String8 text_data;
  FNT_Tag font;
  F32 font_size;
  F32 tab_size;
  String8 search_query;
  F32 line_height_px;
  F32 line_num_width_px;
  F32 line_text_max_width_px;
  B32 show_line_numbers;
};

typedef struct UIShell_TextSliceSignal UIShell_TextSliceSignal;
struct UIShell_TextSliceSignal
{
  UI_Signal base;
  TxtPt mouse_pt;
  TxtRng mouse_expr_rng;
};

internal Rng1U64
uishell_text_range_without_line_end(String8 data, Rng1U64 range)
{
  while(range.max > range.min &&
        (data.str[range.max-1] == '\n' || data.str[range.max-1] == '\r'))
  {
    range.max -= 1;
  }
  return range;
}

internal void
uishell_text_visual_line_list_push(Arena *arena, UIShell_TextVisualLineList *list, UIShell_TextVisualLine v)
{
  UIShell_TextVisualLineNode *n = push_array(arena, UIShell_TextVisualLineNode, 1);
  n->v = v;
  SLLQueuePush(list->first, list->last, n);
  list->count += 1;
}

internal UIShell_TextVisualLineArray
uishell_text_visual_line_array_from_list(Arena *arena, UIShell_TextVisualLineList *list)
{
  UIShell_TextVisualLineArray array = {0};
  array.count = list->count;
  array.v = push_array(arena, UIShell_TextVisualLine, array.count);
  U64 idx = 0;
  for(UIShell_TextVisualLineNode *n = list->first; n != 0; n = n->next, idx += 1)
  {
    array.v[idx] = n->v;
  }
  return array;
}

internal void
uishell_text_push_visual_lines_for_line(Arena *arena, UIShell_TextVisualLineList *list, String8 data, TXT_TextInfo *info, S64 line_num, U64 max_bytes_per_visual_line, B32 do_wrap)
{
  Rng1U64 line_range = uishell_text_range_without_line_end(data, info->lines_ranges[line_num-1]);
  U64 line_size = dim_1u64(line_range);
  if(!do_wrap || line_size <= max_bytes_per_visual_line || max_bytes_per_visual_line <= 4)
  {
    uishell_text_visual_line_list_push(arena, list, (UIShell_TextVisualLine){line_num, line_range, 1});
  }
  else
  {
    U64 line_start = line_range.min;
    U64 line_opl = line_range.max;
    for(U64 start = line_start, visual_idx = 0; start < line_opl; visual_idx += 1)
    {
      U64 target = ClampTop(start + max_bytes_per_visual_line, line_opl);
      U64 split = target;
      if(target < line_opl)
      {
        for(U64 off = target; off > start+1; off -= 1)
        {
          if(char_is_space(data.str[off-1]))
          {
            split = off-1;
            break;
          }
        }
        if(split <= start)
        {
          split = target;
        }
      }
      uishell_text_visual_line_list_push(arena, list, (UIShell_TextVisualLine){line_num, r1u64(start, split), visual_idx == 0});
      start = split;
      while(start < line_opl && char_is_space(data.str[start]))
      {
        start += 1;
      }
    }
  }
}

internal UIShell_TextVisualLineArray
uishell_text_visual_line_array_from_info(Arena *arena, String8 data, TXT_TextInfo *info, U64 max_bytes_per_visual_line, B32 do_wrap)
{
  UIShell_TextVisualLineList list = {0};
  for(S64 line_num = 1; line_num <= (S64)info->lines_count; line_num += 1)
  {
    uishell_text_push_visual_lines_for_line(arena, &list, data, info, line_num, max_bytes_per_visual_line, do_wrap);
  }
  UIShell_TextVisualLineArray array = uishell_text_visual_line_array_from_list(arena, &list);
  return array;
}

internal TxtRng
uishell_text_rng_from_visual_line(TXT_TextInfo *info, UIShell_TextVisualLine *line)
{
  Rng1U64 source_line_range = info->lines_ranges[line->line_num-1];
  TxtRng result = txt_rng(txt_pt(line->line_num, (S64)(line->range.min-source_line_range.min)+1),
                          txt_pt(line->line_num, (S64)(line->range.max-source_line_range.min)+1));
  return result;
}

internal TXT_TokenArray
uishell_text_token_array_from_visual_line(Arena *arena, TXT_TextInfo *info, UIShell_TextVisualLine *visual_line)
{
  TXT_TokenArray line_tokens = txt_token_array_from_info_line_num__linear_scan(info, visual_line->line_num);
  TXT_TokenArray result = {0};
  if(line_tokens.count != 0)
  {
    result.v = push_array(arena, TXT_Token, line_tokens.count);
    for(U64 token_idx = 0; token_idx < line_tokens.count; token_idx += 1)
    {
      TXT_Token *token = &line_tokens.v[token_idx];
      Rng1U64 token_x_visual = intersect_1u64(token->range, visual_line->range);
      if(token_x_visual.max > token_x_visual.min)
      {
        result.v[result.count] = *token;
        result.v[result.count].range = token_x_visual;
        result.count += 1;
      }
    }
  }
  return result;
}

internal DR_FStrList
uishell_text_fstrs_from_visual_line(Arena *arena, TXT_TextInfo *info, String8 data, UIShell_TextVisualLine *visual_line, FNT_Tag font, F32 font_size)
{
  DR_FStrList result = {0};
  DR_FStrParams fstr_params =
  {
    font,
    rd_raster_flags_from_slot(RD_FontSlot_Code),
    rd_rgba_from_code_color_slot(RD_CodeColorSlot_CodeDefault),
    font_size,
  };
  TXT_TokenArray tokens = uishell_text_token_array_from_visual_line(arena, info, visual_line);
  U64 cursor = visual_line->range.min;
  for(U64 token_idx = 0; token_idx < tokens.count; token_idx += 1)
  {
    TXT_Token *token = &tokens.v[token_idx];
    if(cursor < token->range.min)
    {
      dr_fstrs_push_new(arena, &result, &fstr_params, str8_substr(data, r1u64(cursor, token->range.min)));
    }
    RD_CodeColorSlot token_color_slot = rd_code_color_slot_from_txt_token_kind(token->kind);
    dr_fstrs_push_new(arena, &result, &fstr_params, str8_substr(data, token->range),
                      .color = rd_rgba_from_code_color_slot(token_color_slot));
    cursor = token->range.max;
  }
  if(cursor < visual_line->range.max || result.node_count == 0)
  {
    dr_fstrs_push_new(arena, &result, &fstr_params, str8_substr(data, r1u64(cursor, visual_line->range.max)));
  }
  return result;
}

internal UIShell_TextSliceSignal
uishell_text_slice(UIShell_TextSliceParams *params, TxtPt *cursor, TxtPt *mark, S64 *preferred_column, String8 string)
{
  UIShell_TextSliceSignal result = {0};
  Temp scratch = scratch_begin(0, 0);
  B32 is_focused = ui_is_focus_active();
  F32 text_padding_px = params->font_size*0.75f;
  Vec4F32 selection_color = ui_color_from_name(str8_lit("selection"));
  Vec4F32 cursor_color = ui_color_from_name(str8_lit("cursor"));
  if(!is_focused)
  {
    selection_color.w *= 0.5f;
    cursor_color.w *= 0.5f;
  }
  
  UI_Box *top_container_box = &ui_nil_box;
  {
    ui_set_next_child_layout_axis(Axis2_X);
    ui_set_next_pref_width(ui_px(params->line_text_max_width_px, 1.f));
    ui_set_next_pref_height(ui_px(params->line_height_px*params->line_count, 1.f));
    top_container_box = ui_build_box_from_string(UI_BoxFlag_DisableFocusEffects|UI_BoxFlag_DrawBorder, string);
  }
  
  if(params->show_line_numbers) UI_Parent(top_container_box) UI_Focus(UI_FocusKind_Off)
  {
    ui_set_next_pref_width(ui_px(params->line_num_width_px, 1.f));
    ui_set_next_pref_height(ui_px(params->line_height_px*params->line_count, 1.f));
    ui_set_next_child_layout_axis(Axis2_Y);
    UI_Box *line_num_column = ui_build_box_from_string(UI_BoxFlag_DrawSideRight, str8_lit("line_nums"));
    UI_Parent(line_num_column)
      UI_PrefHeight(ui_px(params->line_height_px, 1.f))
      RD_Font(RD_FontSlot_Code)
      UI_FontSize(params->font_size)
      UI_TextAlignment(UI_TextAlign_Right)
      UI_CornerRadius(0)
    {
      TxtRng selection_rng = txt_rng(*cursor, *mark);
      for(U64 line_idx = 0; line_idx < params->line_count; line_idx += 1)
      {
        UIShell_TextVisualLine *visual_line = &params->lines[line_idx];
        B32 line_is_selected = (selection_rng.min.line <= visual_line->line_num &&
                                visual_line->line_num <= selection_rng.max.line);
        UI_TagF(line_is_selected ? "" : "weak")
        {
          UI_Box *line_num_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "###line_num_%I64u", line_idx);
          if(visual_line->first_in_line)
          {
            ui_box_equip_display_string(line_num_box, push_str8f(scratch.arena, "%I64d", visual_line->line_num));
          }
          else
          {
            ui_box_equip_display_string(line_num_box, str8_zero());
          }
        }
      }
    }
  }
  
  UI_Box *text_container_box = &ui_nil_box;
  UI_Parent(top_container_box) UI_Focus(UI_FocusKind_Off)
  {
    ui_set_next_hover_cursor(WM_Cursor_IBar);
    ui_set_next_pref_width(ui_px(ClampBot(params->font_size*8.f, params->line_text_max_width_px-params->line_num_width_px), 1.f));
    ui_set_next_pref_height(ui_px(params->line_height_px*params->line_count, 1.f));
    text_container_box = ui_build_box_from_string(UI_BoxFlag_Clickable, str8_lit("text_container"));
  }
  
  TxtPt mouse_pt = {0};
  TxtRng mouse_token_rng = {0};
  TxtRng mouse_line_rng = {0};
  if(params->line_count != 0)
  {
    Vec2F32 mouse = ui_mouse();
    S64 mouse_line_idx = (S64)((mouse.y - text_container_box->rect.y0) / params->line_height_px);
    mouse_line_idx = Clamp(0, mouse_line_idx, (S64)params->line_count-1);
    UIShell_TextVisualLine *visual_line = &params->lines[mouse_line_idx];
    String8 visual_string = str8_substr(params->text_data, visual_line->range);
    Rng1U64 source_line_range = params->text_info->lines_ranges[visual_line->line_num-1];
    S64 source_col_min = (S64)(visual_line->range.min-source_line_range.min)+1;
    S64 source_col_max = (S64)(visual_line->range.max-source_line_range.min)+1;
    S64 local_column = fnt_char_pos_from_tag_size_string_p(params->font, params->font_size, 0, params->tab_size, visual_string, mouse.x-text_container_box->rect.x0-text_padding_px)+1;
    mouse_pt = txt_pt(visual_line->line_num, Clamp(source_col_min, source_col_min+local_column-1, source_col_max));
    mouse_line_rng = uishell_text_rng_from_visual_line(params->text_info, visual_line);
    
    TXT_TokenArray tokens = uishell_text_token_array_from_visual_line(scratch.arena, params->text_info, visual_line);
    U64 mouse_off = source_line_range.min + (U64)(mouse_pt.column-1);
    mouse_token_rng = txt_rng(mouse_pt, mouse_pt);
    for(U64 token_idx = 0; token_idx < tokens.count; token_idx += 1)
    {
      TXT_Token *token = &tokens.v[token_idx];
      if(contains_1u64(token->range, mouse_off))
      {
        mouse_token_rng = txt_rng(txt_pt(visual_line->line_num, (S64)(token->range.min-source_line_range.min)+1),
                                  txt_pt(visual_line->line_num, (S64)(token->range.max-source_line_range.min)+1));
        break;
      }
    }
    result.mouse_pt = mouse_pt;
  }
  
  UI_Signal text_container_sig = ui_signal_from_box(text_container_box);
  result.base = text_container_sig;
  if(params->line_count != 0)
  {
    TxtRng mouse_drag_rng = txt_rng(mouse_pt, mouse_pt);
    if(text_container_sig.f & UI_SignalFlag_LeftTripleDragging)
    {
      mouse_drag_rng = mouse_line_rng;
    }
    else if(text_container_sig.f & UI_SignalFlag_LeftDoubleDragging)
    {
      mouse_drag_rng = mouse_token_rng;
    }
    if(ui_dragging(text_container_sig))
    {
      if(ui_pressed(text_container_sig))
      {
        *cursor = mouse_drag_rng.max;
        *mark = mouse_drag_rng.min;
      }
      if(txt_pt_less_than(mouse_pt, *mark))
      {
        *cursor = mouse_drag_rng.min;
      }
      else
      {
        *cursor = mouse_drag_rng.max;
      }
      *preferred_column = cursor->column;
    }
  }
  
  UI_Parent(text_container_box) UI_Focus(UI_FocusKind_Off)
  {
    ui_set_next_pref_height(ui_px(params->line_height_px*params->line_count, 1.f));
    UI_WidthFill
      UI_Column
      UI_PrefHeight(ui_px(params->line_height_px, 1.f))
      RD_Font(RD_FontSlot_Code)
      UI_FontSize(params->font_size)
      UI_CornerRadius(0)
    {
      TxtRng selection_rng = txt_rng(*cursor, *mark);
      for(U64 line_idx = 0; line_idx < params->line_count; line_idx += 1)
      {
        UIShell_TextVisualLine *visual_line = &params->lines[line_idx];
        String8 line_string = str8_substr(params->text_data, visual_line->range);
        DR_FStrList line_fstrs = uishell_text_fstrs_from_visual_line(scratch.arena, params->text_info, params->text_data, visual_line, params->font, params->font_size);
        ui_set_next_text_padding(text_padding_px);
        ui_set_next_tab_size(params->tab_size);
        UI_Box *line_box = ui_build_box_from_stringf(UI_BoxFlag_DisableTextTrunc|UI_BoxFlag_DrawText|UI_BoxFlag_DisableIDString, "line_%I64u", line_idx);
        ui_box_equip_display_fstrs(line_box, &line_fstrs);
        
        DR_Bucket *line_bucket = dr_bucket_make();
        dr_push_bucket(line_bucket);
        
        if(params->search_query.size != 0)
        {
          for(U64 needle_pos = 0; needle_pos < line_string.size;)
          {
            needle_pos = str8_find_needle(line_string, needle_pos, params->search_query, StringMatchFlag_CaseInsensitive);
            if(needle_pos < line_string.size)
            {
              Rng1U64 match_range = r1u64(needle_pos, needle_pos+params->search_query.size);
              Rng1F32 match_px_rng =
              {
                fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, match_range.min)).x,
                fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, match_range.max)).x,
              };
              Vec4F32 color = ui_color_from_name(str8_lit("background"));
              color.w *= 0.25f;
              dr_rect(r2f32p(ui_box_text_position(line_box).x+match_px_rng.min,
                             line_box->rect.y0,
                             ui_box_text_position(line_box).x+match_px_rng.max+2.f,
                             line_box->rect.y1),
                      color, 4.f, 0, 1.f);
              needle_pos += 1;
            }
          }
        }
        
        {
          TxtRng visual_txt_rng = uishell_text_rng_from_visual_line(params->text_info, visual_line);
          TxtRng selection_in_line = txt_rng_intersect(selection_rng, visual_txt_rng);
          if(!txt_pt_match(selection_in_line.min, selection_in_line.max) &&
             txt_pt_less_than(selection_in_line.min, selection_in_line.max))
          {
            Rng1U64 source_line_range = params->text_info->lines_ranges[visual_line->line_num-1];
            S64 visual_col_min = (S64)(visual_line->range.min-source_line_range.min)+1;
            Rng1S64 selection_col_rng =
            {
              Clamp(visual_col_min, selection_in_line.min.column, (S64)(visual_col_min+line_string.size)),
              Clamp(visual_col_min, selection_in_line.max.column, (S64)(visual_col_min+line_string.size)),
            };
            Rng1F32 selection_px_rng =
            {
              fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, selection_col_rng.min-visual_col_min)).x,
              fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, selection_col_rng.max-visual_col_min)).x,
            };
            dr_rect(r2f32p(ui_box_text_position(line_box).x+selection_px_rng.min-2.f,
                           floor_f32(line_box->rect.y0)-1.f,
                           ui_box_text_position(line_box).x+selection_px_rng.max+2.f,
                           ceil_f32(line_box->rect.y1)+1.f),
                    selection_color, params->font_size*0.35f, 0, 1.f);
          }
        }
        
        if(cursor->line == visual_line->line_num)
        {
          Rng1U64 source_line_range = params->text_info->lines_ranges[visual_line->line_num-1];
          S64 visual_col_min = (S64)(visual_line->range.min-source_line_range.min)+1;
          S64 visual_col_max = (S64)(visual_line->range.max-source_line_range.min)+1;
          if(visual_col_min <= cursor->column && cursor->column <= visual_col_max)
          {
            U64 prefix_size = (U64)(cursor->column-visual_col_min);
            Vec2F32 advance = fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, prefix_size));
            F32 cursor_thickness = ClampBot(1.f, floor_f32(line_box->font_size/10.f));
            dr_rect(r2f32p(ui_box_text_position(line_box).x+advance.x,
                           line_box->rect.y0-params->font_size*0.125f,
                           ui_box_text_position(line_box).x+advance.x+cursor_thickness,
                           line_box->rect.y1+params->font_size*0.125f),
                    cursor_color, 1.f, 0, 0.f);
          }
        }
        
        if(line_bucket->passes.count != 0)
        {
          ui_box_equip_draw_bucket(line_box, line_bucket);
        }
        dr_pop_bucket();
      }
    }
  }
  
  scratch_end(scratch);
  return result;
}

internal S64
uishell_text_visual_row_from_line(UIShell_TextVisualLineArray *lines, S64 line_num)
{
  S64 result = 0;
  for(U64 idx = 0; idx < lines->count; idx += 1)
  {
    if(lines->v[idx].line_num == line_num)
    {
      result = (S64)idx;
      break;
    }
  }
  return result;
}

internal S64
uishell_text_line_from_needle(String8 data, TXT_TextInfo *info, String8 needle, S64 start_line, S32 direction, TxtPt *pt_out)
{
  S64 result = 0;
  if(needle.size != 0 && info->lines_count != 0)
  {
    S64 line_count = (S64)info->lines_count;
    start_line = Clamp(1, start_line, line_count);
    for(S64 step_idx = 0; step_idx < line_count; step_idx += 1)
    {
      S64 line_num = start_line + step_idx*direction;
      while(line_num < 1)          { line_num += line_count; }
      while(line_count < line_num) { line_num -= line_count; }
      String8 line = str8_substr(data, info->lines_ranges[line_num-1]);
      U64 needle_pos = str8_find_needle(line, 0, needle, StringMatchFlag_CaseInsensitive);
      if(needle_pos < line.size)
      {
        result = line_num;
        if(pt_out != 0)
        {
          *pt_out = txt_pt(line_num, (S64)needle_pos+1);
        }
        break;
      }
    }
  }
  return result;
}

RD_VIEW_UI_FUNCTION_DEF(shell_text)
{
  Temp scratch = scratch_begin(0, 0);
  Access *access = access_open();
  UIShell_TextViewState *tv = rd_view_state(UIShell_TextViewState);
  
  String8 file_path = rd_file_path_from_eval(scratch.arena, eval);
  FileProperties props = properties_from_file_path(file_path);
  B32 file_is_missing = (file_path.size != 0 && props.modified == 0);
  Rng1U64 range = rd_space_range_from_eval(eval);
  C_Key text_key = rd_key_from_eval_space_range(eval.space, range, 1);
  TXT_LangKind lang_kind = TXT_LangKind_Null;
  String8 lang = rd_view_setting_from_name(str8_lit("lang"));
  if(lang.size != 0)
  {
    lang_kind = txt_lang_kind_from_extension(lang);
  }
  else
  {
    lang_kind = rd_lang_kind_from_eval(eval);
  }
  U128 hash = {0};
  TXT_TextInfo info = txt_text_info_from_key_lang(access, text_key, lang_kind, &hash);
  String8 data = c_data_from_hash(access, hash);
  S64 line_count = (S64)info.lines_count;
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  F32 main_font_size = rd_font_size();
  F32 row_height_px = main_font_size*1.45f;
  F32 bottom_bar_height = main_font_size*2.f;
  F32 top_margin_px = floor_f32(main_font_size*0.35f);
  Vec2F32 rect_dim = dim_2f32(rect);
  Vec2F32 list_dim = v2f32(rect_dim.x, Max(row_height_px, rect_dim.y-bottom_bar_height-top_margin_px));
  FNT_Tag code_font = rd_font_from_slot(RD_FontSlot_Code);
  F32 code_font_size = main_font_size;
  F32 code_glyph_advance = fnt_column_size_from_tag_size(code_font, code_font_size);
  B32 do_line_numbers = rd_view_setting_b32_from_name(str8_lit("show_line_numbers"));
  B32 do_wrap = rd_view_setting_b32_from_name(str8_lit("line_wrapping"));
  F32 scroll_bar_dim = floor_f32(main_font_size*1.5f);
  F32 line_num_width_px = do_line_numbers ? floor_f32(code_glyph_advance*(log10(ClampBot(1, line_count))+3)) : 0;
  F32 text_area_width_px = ClampBot(code_glyph_advance*16, list_dim.x-scroll_bar_dim-line_num_width_px-main_font_size*2.5f);
  U64 max_bytes_per_visual_line = ClampBot(8, (U64)(text_area_width_px/ClampBot(1, code_glyph_advance)));
  
  if(!tv->initialized)
  {
    tv->initialized = 1;
    tv->cursor.line = rd_view_setting_value_from_name(str8_lit("cursor_line")).s64;
    tv->cursor.column = rd_view_setting_value_from_name(str8_lit("cursor_column")).s64;
    tv->mark.line = rd_view_setting_value_from_name(str8_lit("mark_line")).s64;
    tv->mark.column = rd_view_setting_value_from_name(str8_lit("mark_column")).s64;
    tv->preferred_column = tv->cursor.column;
    if(tv->cursor.line == 0) { tv->cursor.line = 1; }
    if(tv->cursor.column == 0) { tv->cursor.column = 1; }
    if(tv->mark.line == 0)   { tv->mark = tv->cursor; }
    if(tv->mark.column == 0) { tv->mark.column = tv->cursor.column; }
  }
  tv->cursor.line = Clamp(1, tv->cursor.line, ClampBot(1, line_count));
  tv->mark.line = Clamp(1, tv->mark.line, ClampBot(1, line_count));
  String8 cursor_line_string = txt_string_from_info_data_line_num(&info, data, tv->cursor.line);
  String8 mark_line_string = txt_string_from_info_data_line_num(&info, data, tv->mark.line);
  tv->cursor.column = Clamp(1, tv->cursor.column, (S64)cursor_line_string.size+1);
  tv->mark.column = Clamp(1, tv->mark.column, (S64)mark_line_string.size+1);
  
  for(UIShell_Cmd *cmd = 0; uishell_next_view_cmd(&cmd);)
  {
    if(str8_match(cmd->name, str8_lit("center_cursor"), 0))
    {
      tv->center_cursor = 1;
    }
    else if(str8_match(cmd->name, str8_lit("contain_cursor"), 0))
    {
      tv->contain_cursor = 1;
    }
    else if(str8_match(cmd->name, str8_lit("goto_line"), 0))
    {
      tv->cursor = tv->mark = txt_pt(Clamp(1, cmd->regs->cursor.line, ClampBot(1, line_count)), 1);
      tv->center_cursor = 1;
    }
    else if(str8_match(cmd->name, str8_lit("search"), 0) ||
            str8_match(cmd->name, str8_lit("find_next"), 0))
    {
      String8 needle = cmd->regs->string.size != 0 ? cmd->regs->string : rd_view_query_input();
      TxtPt found_pt = {0};
      S64 line_num = uishell_text_line_from_needle(data, &info, needle, tv->cursor.line+1, +1, &found_pt);
      if(line_num != 0)
      {
        tv->mark = found_pt;
        tv->cursor = txt_pt(found_pt.line, found_pt.column+(S64)needle.size);
        tv->preferred_column = tv->cursor.column;
        tv->center_cursor = 1;
      }
    }
    else if(str8_match(cmd->name, str8_lit("search_backwards"), 0) ||
            str8_match(cmd->name, str8_lit("find_prev"), 0))
    {
      String8 needle = cmd->regs->string.size != 0 ? cmd->regs->string : rd_view_query_input();
      TxtPt found_pt = {0};
      S64 line_num = uishell_text_line_from_needle(data, &info, needle, tv->cursor.line-1, -1, &found_pt);
      if(line_num != 0)
      {
        tv->mark = found_pt;
        tv->cursor = txt_pt(found_pt.line, found_pt.column+(S64)needle.size);
        tv->preferred_column = tv->cursor.column;
        tv->center_cursor = 1;
      }
    }
  }
  
  UIShell_TextVisualLineArray visual_lines = {0};
  if(info.lines_count != 0)
  {
    visual_lines = uishell_text_visual_line_array_from_info(scratch.arena, data, &info, max_bytes_per_visual_line, do_wrap);
  }
  S64 visible_visual_line_count = ClampBot(1, (S64)(list_dim.y/row_height_px));
  Rng1S64 scroll_idx_rng = r1s64(0, ClampBot(0, (S64)visual_lines.count-1));
  
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active() && info.lines_count != 0)
  {
    B32 changed = rd_do_txt_controls(&info, data, ClampBot(visible_visual_line_count, 10)-10, &tv->cursor, &tv->mark, &tv->preferred_column);
    if(changed)
    {
      tv->contain_cursor = 1;
    }
  }
  
  if(tv->center_cursor && visual_lines.count != 0)
  {
    tv->center_cursor = 0;
    S64 visual_row_idx = uishell_text_visual_row_from_line(&visual_lines, tv->cursor.line);
    ui_scroll_pt_target_idx(&scroll_pos.y, clamp_1s64(scroll_idx_rng, visual_row_idx-visible_visual_line_count/2));
  }
  
  if(tv->contain_cursor && visual_lines.count != 0)
  {
    tv->contain_cursor = 0;
    S64 visual_row_idx = uishell_text_visual_row_from_line(&visual_lines, tv->cursor.line);
    Rng1S64 visible_rng = r1s64(scroll_pos.y.idx, scroll_pos.y.idx + visible_visual_line_count);
    if(visual_row_idx < visible_rng.min)
    {
      ui_scroll_pt_target_idx(&scroll_pos.y, clamp_1s64(scroll_idx_rng, visual_row_idx));
    }
    else if(visible_rng.max <= visual_row_idx)
    {
      ui_scroll_pt_target_idx(&scroll_pos.y, clamp_1s64(scroll_idx_rng, visual_row_idx-visible_visual_line_count+1));
    }
  }
  
  UI_WidthFill UI_HeightFill UI_Column
  {
    if(file_is_missing)
    {
      UI_WidthFill UI_HeightFill UI_Column UI_Padding(ui_pct(1, 0))
      {
        UI_PrefWidth(ui_children_sum(1)) UI_PrefHeight(ui_em(3, 1))
          UI_Row UI_Padding(ui_pct(1, 0))
          UI_PrefWidth(ui_text_dim(1, 1))
          UI_TagF("weak")
        {
          RD_Font(RD_FontSlot_Icons) ui_label(rd_icon_kind_text_table[RD_IconKind_WarningBig]);
          ui_labelf("Could not find \"%S\".", file_path);
        }
      }
    }
    else if(props.size != 0 && info.lines_count == 0 && u128_match(hash, u128_zero()))
    {
      rd_store_view_loading_info(1, info.bytes_processed, info.bytes_to_process);
      UI_WidthFill UI_HeightFill UI_Column UI_Padding(ui_pct(1, 0))
      {
        UI_PrefWidth(ui_children_sum(1)) UI_PrefHeight(ui_em(3, 1))
          UI_Row UI_Padding(ui_pct(1, 0))
          UI_PrefWidth(ui_text_dim(1, 1))
          UI_TagF("weak")
        {
          UI_TagF("weak") ui_label(str8_lit("Loading..."));
        }
      }
    }
    else if(line_count == 0)
    {
      UI_WidthFill UI_HeightFill UI_Column UI_Padding(ui_pct(1, 0))
      {
        UI_PrefWidth(ui_children_sum(1)) UI_PrefHeight(ui_em(3, 1))
          UI_Row UI_Padding(ui_pct(1, 0))
          UI_PrefWidth(ui_text_dim(1, 1))
          UI_TagF("weak")
        {
          UI_TagF("weak") ui_label(str8_lit("Empty file"));
        }
      }
    }
    else
    {
      rd_store_view_loading_info(0, 0, 0);
      Rng1S64 visible_visual_line_rng = {0};
      UI_ScrollListParams scroll_list_params = {0};
      scroll_list_params.flags = UI_ScrollListFlag_All;
      scroll_list_params.row_height_px = row_height_px;
      scroll_list_params.dim_px = list_dim;
      scroll_list_params.item_range = r1s64(0, (S64)visual_lines.count);
      scroll_list_params.cursor_range = r2s64(v2s64(0, 0), v2s64(0, 0));
      UI_ScrollListSignal scroll_list_sig = {0};
      ui_spacer(ui_px(top_margin_px, 1.f));
      UI_ScrollList(&scroll_list_params, &scroll_pos.y, 0, 0, &visible_visual_line_rng, &scroll_list_sig)
      {
        if(visible_visual_line_rng.max > visible_visual_line_rng.min)
        {
          Rng1S64 clamped_visible_rng = r1s64(Clamp(0, visible_visual_line_rng.min, (S64)visual_lines.count),
                                              Clamp(0, visible_visual_line_rng.max, (S64)visual_lines.count));
          U64 visible_count = (U64)dim_1s64(clamped_visible_rng);
          UIShell_TextSliceParams slice_params = {0};
          slice_params.lines                  = visual_lines.v + clamped_visible_rng.min;
          slice_params.line_count             = visible_count;
          slice_params.text_info              = &info;
          slice_params.text_data              = data;
          slice_params.font                   = code_font;
          slice_params.font_size              = code_font_size;
          slice_params.tab_size               = code_glyph_advance*4.f;
          slice_params.search_query           = rd_view_query_input();
          slice_params.line_height_px         = row_height_px;
          slice_params.line_num_width_px      = line_num_width_px;
          slice_params.line_text_max_width_px = ClampBot(code_glyph_advance*32.f, list_dim.x-scroll_bar_dim);
          slice_params.show_line_numbers      = do_line_numbers;
          UI_Focus(UI_FocusKind_On)
          {
            UIShell_TextSliceSignal sig = uishell_text_slice(&slice_params, &tv->cursor, &tv->mark, &tv->preferred_column, str8_lit("text_slice"));
            if(ui_pressed(sig.base))
            {
              uishell_cmd("focus_panel");
            }
            if(ui_dragging(sig.base) && sig.base.event_flags == 0)
            {
              if(!contains_2f32(sig.base.box->rect, ui_mouse()))
              {
                tv->contain_cursor = 1;
              }
            }
          }
        }
      }
      
      UI_FontSize(main_font_size) UI_TagF(".")
      {
        ui_set_next_flags(UI_BoxFlag_DrawBackground);
        UI_PrefHeight(ui_px(bottom_bar_height, 1.f))
          UI_WidthFill
          UI_Row
          UI_TextAlignment(UI_TextAlign_Center)
          UI_PrefWidth(ui_text_dim(10, 1))
          UI_TagF("weak")
        {
          RD_Font(RD_FontSlot_Code)
          {
            if(file_path.size != 0)
            {
              ui_label(file_path);
              ui_spacer(ui_em(1.5f, 1.f));
            }
            ui_labelf("Line: %I64d, Column: %I64d", tv->cursor.line, tv->cursor.column);
            ui_spacer(ui_pct(1, 0));
            ui_labelf("%S", str8_from_memory_size(scratch.arena, props.size));
            ui_labelf("%I64d lines", line_count);
            ui_labelf("(read only)");
            ui_labelf("%s",
                      info.line_end_kind == TXT_LineEndKind_LF   ? "lf" :
                      info.line_end_kind == TXT_LineEndKind_CRLF ? "crlf" :
                      "bin");
          }
        }
      }
    }
  }
  
  uishell_regs()->file_path = file_path;
  uishell_regs()->text_key = text_key;
  uishell_regs()->lang_kind = lang_kind;
  uishell_regs()->cursor = tv->cursor;
  uishell_regs()->mark = tv->mark;
  rd_store_view_scroll_pos(scroll_pos);
  rd_store_view_param_s64(str8_lit("cursor_line"), tv->cursor.line);
  rd_store_view_param_s64(str8_lit("cursor_column"), tv->cursor.column);
  rd_store_view_param_s64(str8_lit("mark_line"), tv->mark.line);
  rd_store_view_param_s64(str8_lit("mark_column"), tv->mark.column);
  access_close(access);
  scratch_end(scratch);
}

internal void
uishell_binary_copy_range(Arena *arena, Access *access, E_Eval eval, Rng1U64 range, U64 num_columns)
{
  range.max = Min(range.max, range.min+MB(1));
  C_Key key = rd_key_from_eval_space_range(eval.space, range, 0);
  U128 hash = c_hash_from_key(key, 0);
  String8 data = c_data_from_hash(access, hash);
  if(u128_match(hash, u128_zero()))
  {
    log_user_errorf("Selected bytes are still loading.");
  }
  else
  {
    String8List data_textified_parts = {0};
    U64 column_num = 1 + range.min%num_columns;
    for EachIndex(idx, data.size)
    {
      str8_list_pushf(arena, &data_textified_parts, "%02X%s", (U8)data.str[idx],
                      idx+1 >= data.size ? "" :
                      column_num+1 <= num_columns ? " " :
                      "\n");
      column_num += 1;
      if(column_num > num_columns)
      {
        column_num = 1;
      }
    }
    String8 data_textified = str8_list_join(arena, &data_textified_parts, 0);
    wm_set_clipboard_text(data_textified);
  }
}

RD_VIEW_UI_FUNCTION_DEF(binary)
{
  Temp scratch = scratch_begin(0, 0);
  Access *access = access_open();
  UIShell_BinaryViewState *bv = rd_view_state(UIShell_BinaryViewState);
  
  String8 file_path = rd_file_path_from_eval(scratch.arena, eval);
  FileProperties props = properties_from_file_path(file_path);
  B32 file_is_missing = (file_path.size != 0 && props.modified == 0);
  UI_ScrollPt2 scroll_pos = rd_view_scroll_pos();
  F32 main_font_size = rd_font_size();
  F32 row_height_px = main_font_size*1.6f;
  FNT_Tag cell_font = rd_font_from_slot(RD_FontSlot_Code);
  FNT_RasterFlags cell_font_raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Code);
  F32 cell_font_size = main_font_size;
  F32 cell_big_glyph_advance = fnt_dim_from_tag_size_string(cell_font, cell_font_size, 0, 0, str8_lit("H")).x;
  F32 cell_width_px = floor_f32(cell_font_size*2.f);
  F32 address_margin_width_px = cell_big_glyph_advance*20.f;
  F32 scroll_bar_dim = floor_f32(main_font_size*1.5f);
  Vec2F32 panel_dim = dim_2f32(rect);
  F32 footer_dim = floor_f32(main_font_size*3.f);
  Rng2F32 addrbar_rect = r2f32p(0, 0, panel_dim.x, main_font_size*3.f);
  Rng2F32 header_rect = r2f32p(0, addrbar_rect.y1, panel_dim.x, addrbar_rect.y1 + row_height_px);
  Rng2F32 footer_rect = r2f32p(0, panel_dim.y-footer_dim, panel_dim.x-scroll_bar_dim, panel_dim.y);
  Rng2F32 content_rect = r2f32p(0, header_rect.y1, panel_dim.x-scroll_bar_dim, panel_dim.y);
  U64 num_columns = rd_view_setting_u64_from_name(str8_lit("num_columns"));
  B32 auto_columns = rd_view_setting_b32_from_name(str8_lit("auto_columns"));
  if(num_columns == 0)
  {
    num_columns = 16;
  }
  if(auto_columns)
  {
    F32 auto_columns_f = (panel_dim.x - address_margin_width_px - cell_big_glyph_advance*4.f) / ClampBot(1.f, cell_width_px + cell_big_glyph_advance);
    num_columns = ClampTop(64, (U64)ClampBot(1.f, auto_columns_f));
  }
  num_columns = ClampBot(1, num_columns);
  S64 row_count = (S64)CeilIntegerDiv(props.size, num_columns);
  S64 num_possible_visible_rows = ClampBot(1, (S64)(panel_dim.y/row_height_px));
  S64 visible_row_count = ClampBot(1, (S64)((dim_2f32(content_rect).y - dim_2f32(footer_rect).y)/row_height_px - 1));
  U64 last_off = props.size != 0 ? props.size-1 : 0;
  Rng1S64 scroll_idx_rng = r1s64(0, ClampBot(0, row_count-1));
  
  if(!bv->initialized)
  {
    bv->initialized = 1;
    bv->cursor_off = rd_view_setting_addr_from_name(str8_lit("cursor"));
    bv->mark_off = rd_view_setting_addr_from_name(str8_lit("mark"));
  }
  bv->cursor_off = Min(bv->cursor_off, last_off);
  bv->mark_off = Min(bv->mark_off, last_off);
  
  for(UIShell_Cmd *cmd = 0; uishell_next_view_cmd(&cmd);)
  {
    if(str8_match(cmd->name, str8_lit("center_cursor"), 0))
    {
      bv->center_cursor = 1;
    }
    else if(str8_match(cmd->name, str8_lit("contain_cursor"), 0))
    {
      bv->contain_cursor = 1;
    }
    else if(str8_match(cmd->name, str8_lit("goto_address"), 0))
    {
      bv->cursor_off = bv->mark_off = Min(cmd->regs->vaddr, last_off);
      bv->center_cursor = 1;
    }
  }
  
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active())
  {
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      B32 good_action = 0;
      S64 byte_delta = 0;
      
      if(evt->flags & UI_EventFlag_Copy)
      {
        if(props.size != 0)
        {
          uishell_binary_copy_range(scratch.arena, access, eval, uishell_binary_selection_from_state(bv), num_columns);
        }
        good_action = 1;
      }
      
      if(evt->delta_2s32.x != 0 || evt->delta_2s32.y != 0)
      {
        switch(evt->delta_unit)
        {
          default:{}break;
          case UI_EventDeltaUnit_Char:
          {
            byte_delta = (S64)evt->delta_2s32.x + (S64)evt->delta_2s32.y*(S64)num_columns;
          }break;
          case UI_EventDeltaUnit_Word:
          {
            byte_delta = (S64)evt->delta_2s32.x*(S64)num_columns + (S64)evt->delta_2s32.y*(S64)num_columns;
          }break;
          case UI_EventDeltaUnit_Line:
          {
            if(evt->delta_2s32.x < 0)
            {
              byte_delta = -(S64)(bv->cursor_off%num_columns);
            }
            else if(evt->delta_2s32.x > 0)
            {
              byte_delta = (S64)(num_columns-1 - bv->cursor_off%num_columns);
            }
          }break;
          case UI_EventDeltaUnit_Page:
          {
            byte_delta = (S64)evt->delta_2s32.y*visible_row_count*(S64)num_columns;
          }break;
          case UI_EventDeltaUnit_Whole:
          {
            if(evt->delta_2s32.x < 0 || evt->delta_2s32.y < 0)
            {
              byte_delta = -(S64)bv->cursor_off;
            }
            else if(evt->delta_2s32.x > 0 || evt->delta_2s32.y > 0)
            {
              byte_delta = (S64)(last_off - bv->cursor_off);
            }
          }break;
        }
        good_action = 1;
      }
      
      if(good_action)
      {
        if(byte_delta < 0)
        {
          bv->cursor_off -= Min(bv->cursor_off, (U64)-byte_delta);
        }
        else if(byte_delta > 0)
        {
          bv->cursor_off = Min(last_off, bv->cursor_off + (U64)byte_delta);
        }
        if(!(evt->flags & UI_EventFlag_KeepMark))
        {
          bv->mark_off = bv->cursor_off;
        }
        bv->contain_cursor = 1;
        ui_eat_event(evt);
      }
    }
  }
  
  if(bv->center_cursor && row_count != 0)
  {
    bv->center_cursor = 0;
    S64 cursor_row_idx = (S64)(bv->cursor_off/num_columns);
    S64 new_idx = clamp_1s64(scroll_idx_rng, cursor_row_idx - visible_row_count/2);
    ui_scroll_pt_target_idx(&scroll_pos.y, new_idx);
  }
  
  if(bv->contain_cursor && row_count != 0)
  {
    bv->contain_cursor = 0;
    S64 cursor_row_idx = (S64)(bv->cursor_off/num_columns);
    Rng1S64 visible_rng = r1s64(scroll_pos.y.idx, scroll_pos.y.idx + visible_row_count);
    if(cursor_row_idx < visible_rng.min)
    {
      ui_scroll_pt_target_idx(&scroll_pos.y, clamp_1s64(scroll_idx_rng, cursor_row_idx));
    }
    else if(visible_rng.max <= cursor_row_idx)
    {
      ui_scroll_pt_target_idx(&scroll_pos.y, clamp_1s64(scroll_idx_rng, cursor_row_idx-visible_row_count+1));
    }
  }
  
  Rng1U64 selection = uishell_binary_selection_from_state(bv);
  Vec4F32 selection_color = ui_color_from_name(str8_lit("selection"));
  Vec4F32 border_color = ui_color_from_name(str8_lit("selection"));
  
  UI_WidthFill UI_HeightFill UI_Column
  {
    if(file_is_missing)
    {
      UI_WidthFill UI_HeightFill UI_Column UI_Padding(ui_pct(1, 0))
      {
        UI_PrefWidth(ui_children_sum(1)) UI_PrefHeight(ui_em(3, 1))
          UI_Row UI_Padding(ui_pct(1, 0))
          UI_PrefWidth(ui_text_dim(1, 1))
          UI_TagF("weak")
        {
          RD_Font(RD_FontSlot_Icons) ui_label(rd_icon_kind_text_table[RD_IconKind_WarningBig]);
          ui_labelf("Could not find \"%S\".", file_path);
        }
      }
    }
    else
    {
      if(row_count == 0)
      {
        UI_WidthFill UI_HeightFill UI_Column UI_Padding(ui_pct(1, 0))
        {
          UI_PrefWidth(ui_children_sum(1)) UI_PrefHeight(ui_em(3, 1))
            UI_Row UI_Padding(ui_pct(1, 0))
            UI_PrefWidth(ui_text_dim(1, 1))
            UI_TagF("weak")
          {
            UI_TagF("weak") ui_label(str8_lit("Empty file"));
          }
        }
      }
      else
      {
        Rng1S64 visible_row_rng = {0};
        {
          visible_row_rng.min = scroll_pos.y.idx + (S64)scroll_pos.y.off - !!(scroll_pos.y.off < 0);
          visible_row_rng.max = scroll_pos.y.idx + (S64)scroll_pos.y.off + num_possible_visible_rows;
          visible_row_rng.min = clamp_1s64(scroll_idx_rng, visible_row_rng.min);
          visible_row_rng.max = clamp_1s64(scroll_idx_rng, visible_row_rng.max);
        }
        if(visible_row_rng.min > 0)
        {
          visible_row_rng.min -= 1;
          content_rect.y0 -= row_height_px;
        }
        Rng1U64 byte_range = r1u64((U64)visible_row_rng.min*num_columns, Min(((U64)visible_row_rng.max+1)*num_columns+1, props.size));
        C_Key key = rd_key_from_eval_space_range(eval.space, byte_range, 0);
        U128 hash = c_hash_from_key(key, 0);
        String8 data = c_data_from_hash(access, hash);
        B32 data_is_ready = (dim_1u64(byte_range) == 0 || !u128_match(hash, u128_zero()));
        if(!data_is_ready)
        {
          rd_store_view_loading_info(1, byte_range.min, byte_range.max);
        }
        else
        {
          rd_store_view_loading_info(0, 0, 0);
        }
        
        DR_FStrList byte_fstrs[256] = {0};
        DR_FStrList byte_fstrs_selected[256] = {0};
        if(data_is_ready)
        {
          Vec4F32 full_color = {0};
          UI_TagF("neutral") full_color = ui_color_from_name(str8_lit("text"));
          Vec4F32 zero_color = {0};
          UI_TagF("weak") zero_color = ui_color_from_name(str8_lit("text"));
          Vec4F32 selected_text_color = {0};
          UI_TagF("weak") selected_text_color = ui_color_from_name(str8_lit("text"));
          for(U64 idx = 0; idx < ArrayCount(byte_fstrs); idx += 1)
          {
            U8 byte = (U8)idx;
            F32 pct = byte/255.f;
            Vec4F32 text_color = mix_4f32(zero_color, full_color, pct);
            if(byte == 0)
            {
              text_color.w *= 0.5f;
            }
            DR_FStr fstr = {push_str8f(scratch.arena, "%02x", byte), {cell_font, cell_font_raster_flags, text_color, cell_font_size, 0, 0}};
            dr_fstrs_push(scratch.arena, &byte_fstrs[idx], &fstr);
            DR_FStr selected_fstr = {push_str8f(scratch.arena, "%02x", byte), {cell_font, cell_font_raster_flags, selected_text_color, cell_font_size, 0, 0}};
            dr_fstrs_push(scratch.arena, &byte_fstrs_selected[idx], &selected_fstr);
          }
        }
        
        UI_Box *container_box = &ui_nil_box;
        {
          ui_set_next_fixed_width(panel_dim.x);
          ui_set_next_fixed_height(panel_dim.y);
          ui_set_next_child_layout_axis(Axis2_Y);
          container_box = ui_build_box_from_stringf(0, "binary_view_container");
        }
        
        UI_Parent(container_box) UI_FontSize(ui_bottom_font_size())
        {
          ui_set_next_fixed_x(content_rect.x1);
          ui_set_next_fixed_y(header_rect.y0);
          ui_set_next_fixed_width(scroll_bar_dim);
          ui_set_next_fixed_height(panel_dim.y - dim_2f32(addrbar_rect).y);
          scroll_pos.y = ui_scroll_bar(Axis2_Y,
                                       ui_px(scroll_bar_dim, 1.f),
                                       scroll_pos.y,
                                       scroll_idx_rng,
                                       num_possible_visible_rows);
        }
        
        UI_Parent(container_box) UI_TagF("floating")
        {
          UI_Box *cursor_bar_box = &ui_nil_box;
          UI_Rect(addrbar_rect)
            cursor_bar_box = ui_build_box_from_string(UI_BoxFlag_DrawSideBottom|
                                                       UI_BoxFlag_DrawBackground|
                                                       UI_BoxFlag_DrawDropShadow|
                                                       UI_BoxFlag_DrawBackgroundBlur|
                                                       UI_BoxFlag_Floating|
                                                       UI_BoxFlag_Clickable, str8_lit("addrbar_box"));
          UI_Parent(cursor_bar_box)
            RD_Font(RD_FontSlot_Code)
            UI_FontSize(cell_font_size)
          {
            UI_Flags(UI_BoxFlag_DrawSideRight)
              UI_TextAlignment(UI_TextAlign_Center)
              UI_PrefWidth(ui_text_dim(10, 1))
              UI_TagF("weak")
              ui_labelf("Cursor Address");
            UI_TextAlignment(UI_TextAlign_Left)
              UI_PrefWidth(ui_pct(1, 0))
              UI_TagF(".")
            {
              ui_spacer(ui_em(0.75f, 1.f));
              ui_labelf("0x%016I64X", bv->cursor_off);
            }
          }
          UI_Signal sig = ui_signal_from_box(cursor_bar_box);
          if(ui_pressed(sig))
          {
            uishell_cmd("focus_panel");
          }
        }
        
        UI_Box *header_box = &ui_nil_box;
        UI_Parent(container_box) UI_FontSize(cell_font_size) UI_TagF("floating")
        {
          UI_Rect(header_rect)
            header_box = ui_build_box_from_string(UI_BoxFlag_DrawSideBottom|
                                                  UI_BoxFlag_DrawSideTop|
                                                  UI_BoxFlag_DrawBackground|
                                                  UI_BoxFlag_DrawDropShadow|
                                                  UI_BoxFlag_DrawBackgroundBlur|
                                                  UI_BoxFlag_Floating|
                                                  UI_BoxFlag_Clickable, str8_lit("table_header"));
          UI_Parent(header_box)
            RD_Font(RD_FontSlot_Code)
            UI_FontSize(cell_font_size)
          {
            UI_PrefWidth(ui_px(address_margin_width_px, 1.f)) ui_labelf("Address");
            UI_PrefWidth(ui_px(cell_width_px, 1.f))
              UI_TextAlignment(UI_TextAlign_Center)
            {
              for(U64 col_idx = 0; col_idx < num_columns; col_idx += 1)
              {
                U64 selection_col_min = selection.min%num_columns;
                U64 selection_col_max = (selection.max != 0 ? selection.max-1 : selection.max)%num_columns;
                B32 selection_spans_rows = (selection.max != 0 && selection.min/num_columns != (selection.max-1)/num_columns);
                B32 column_is_selected = (selection_spans_rows ||
                                          (selection_col_min <= col_idx && col_idx <= selection_col_max));
                UI_TagF(column_is_selected ? "" : "weak") ui_labelf("%I64X", col_idx);
              }
            }
            ui_spacer(ui_px(cell_big_glyph_advance*1.5f, 1.f));
            UI_WidthFill ui_labelf("ASCII");
          }
          UI_Signal sig = ui_signal_from_box(header_box);
          if(ui_pressed(sig))
          {
            uishell_cmd("focus_panel");
          }
        }
        
        UI_Box *footer_box = &ui_nil_box;
        UI_Parent(container_box) UI_FontSize(main_font_size) UI_TagF("floating")
        {
          ui_set_next_fixed_x(footer_rect.x0);
          ui_set_next_fixed_y(footer_rect.y0);
          ui_set_next_fixed_width(dim_2f32(footer_rect).x);
          ui_set_next_fixed_height(dim_2f32(footer_rect).y);
          footer_box = ui_build_box_from_string(UI_BoxFlag_Clickable|
                                                UI_BoxFlag_DrawSideTop|
                                                UI_BoxFlag_DrawBackground|
                                                UI_BoxFlag_DrawBackgroundBlur|
                                                UI_BoxFlag_DrawDropShadow, str8_lit("footer"));
          UI_Parent(footer_box) RD_Font(RD_FontSlot_Code) UI_TextAlignment(UI_TextAlign_Center) UI_PrefWidth(ui_text_dim(10, 1)) UI_TagF("weak")
          {
            if(file_path.size != 0)
            {
              ui_label(file_path);
              ui_spacer(ui_em(1.5f, 1.f));
            }
            ui_labelf("Offset: 0x%I64X", bv->cursor_off);
            ui_spacer(ui_pct(1, 0));
            ui_labelf("%S", str8_from_memory_size(scratch.arena, props.size));
            ui_labelf("%I64d rows", row_count);
            ui_labelf("Selected: %S", str8_from_memory_size(scratch.arena, dim_1u64(selection)));
            ui_labelf("(read only)");
          }
          UI_Signal sig = ui_signal_from_box(footer_box);
          if(ui_pressed(sig))
          {
            uishell_cmd("focus_panel");
          }
        }
        
        UI_Box *scrollable_box = &ui_nil_box;
        UI_Parent(container_box)
        {
          ui_set_next_fixed_x(content_rect.x0);
          ui_set_next_fixed_y(content_rect.y0);
          ui_set_next_fixed_width(dim_2f32(content_rect).x);
          ui_set_next_fixed_height(dim_2f32(content_rect).y);
          scrollable_box = ui_build_box_from_string(UI_BoxFlag_Clip|
                                                    UI_BoxFlag_Scroll|
                                                    UI_BoxFlag_AllowOverflowY,
                                                    str8_lit("scrollable_box"));
          scrollable_box->view_off.y = scrollable_box->view_off_target.y = floor_f32(row_height_px*mod_f32(scroll_pos.y.off, 1.f) + row_height_px*(scroll_pos.y.off < 0));
        }
        
        UI_Box *row_container_box = &ui_nil_box;
        UI_Parent(scrollable_box) UI_WidthFill UI_HeightFill
        {
          ui_set_next_child_layout_axis(Axis2_Y);
          row_container_box = ui_build_box_from_string(UI_BoxFlag_Clickable, str8_lit("binary_row_container"));
        }
        
        if(!data_is_ready)
        {
          UI_Parent(row_container_box) UI_PrefHeight(ui_children_sum(1)) UI_Row UI_Padding(ui_em(0.75f, 1.f))
          {
            UI_TagF("weak") ui_label(str8_lit("Loading..."));
          }
        }
        else
        {
          U64 mouse_hover_byte_num = 0;
          UI_Signal row_container_sig = ui_signal_from_box(row_container_box);
          if(ui_hovering(row_container_sig) || ui_dragging(row_container_sig))
          {
            Vec2F32 mouse_rel = sub_2f32(ui_mouse(), row_container_box->rect.p0);
            U64 row_idx = (U64)ClampBot(0, mouse_rel.y) / (U64)row_height_px;
            
            if(mouse_hover_byte_num == 0)
            {
              U64 col_idx = (U64)ClampBot(mouse_rel.x-address_margin_width_px, 0) / (U64)cell_width_px;
              if(col_idx < num_columns)
              {
                mouse_hover_byte_num = byte_range.min + row_idx*num_columns + col_idx + 1;
              }
            }
            if(mouse_hover_byte_num == 0)
            {
              U64 col_idx = (U64)ClampBot(mouse_rel.x - (address_margin_width_px + cell_width_px*num_columns + cell_big_glyph_advance*1.5f), 0) / (U64)cell_big_glyph_advance;
              col_idx = ClampTop(col_idx, num_columns-1);
              mouse_hover_byte_num = byte_range.min + row_idx*num_columns + col_idx + 1;
            }
            mouse_hover_byte_num = Clamp(1, mouse_hover_byte_num, props.size);
          }
          if(ui_pressed(row_container_sig))
          {
            uishell_cmd("focus_panel");
          }
          if(ui_dragging(row_container_sig) && mouse_hover_byte_num != 0)
          {
            if(!contains_2f32(row_container_sig.box->rect, ui_mouse()))
            {
              bv->contain_cursor = 1;
            }
            bv->cursor_off = mouse_hover_byte_num-1;
            if(ui_pressed(row_container_sig))
            {
              bv->mark_off = bv->cursor_off;
            }
            selection = uishell_binary_selection_from_state(bv);
          }
          
          UI_Parent(row_container_box) RD_Font(RD_FontSlot_Code) UI_FontSize(cell_font_size)
          {
            U8 *row_ascii_buffer = push_array(scratch.arena, U8, num_columns);
            UI_WidthFill UI_PrefHeight(ui_px(row_height_px, 1.f))
              for(S64 row_idx = visible_row_rng.min; row_idx <= visible_row_rng.max; row_idx += 1)
            {
              U64 row_off = (U64)row_idx*num_columns;
              if(row_off >= props.size)
              {
                break;
              }
              U64 row_opl = Min(row_off+num_columns, props.size);
              Rng1U64 row_range = r1u64(row_off, row_opl);
              UI_Box *row = ui_build_box_from_stringf(0, "row_%I64x", row_off);
              UI_Parent(row)
              {
                UI_PrefWidth(ui_px(address_margin_width_px, 1.f))
                {
                  if(dim_1u64(intersect_1u64(selection, row_range)) == 0)
                  {
                    ui_set_next_tag(str8_lit("weak"));
                  }
                  ui_labelf("0x%016I64x", row_off);
                }
                UI_PrefWidth(ui_px(cell_width_px, 1.f))
                  UI_TextAlignment(UI_TextAlign_Center)
                  UI_CornerRadius(0)
                {
                  for(U64 col_idx = 0; col_idx < num_columns; col_idx += 1)
                  {
                    U64 global_byte_idx = row_off + col_idx;
                    if(global_byte_idx >= props.size)
                    {
                      ui_build_box_from_key(0, ui_key_zero());
                    }
                    else
                    {
                      U64 visible_byte_idx = global_byte_idx - byte_range.min;
                      U8 byte_value = visible_byte_idx < data.size ? data.str[visible_byte_idx] : 0;
                      B32 byte_is_selected = contains_1u64(selection, global_byte_idx);
                      UI_BoxFlags cell_flags = UI_BoxFlag_DrawText;
                      Vec4F32 cell_bd_rgba = ui_color_from_name(str8_lit("text"));
                      UI_TagF("weak") cell_bd_rgba = ui_color_from_name(str8_lit("text"));
                      
                      if(mouse_hover_byte_num == global_byte_idx+1)
                      {
                        cell_flags |= UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawDropShadow;
                      }
                      if(selection.min == global_byte_idx)
                      {
                        cell_flags |= UI_BoxFlag_DrawSideLeft;
                      }
                      if(selection.max != 0 && selection.max-1 == global_byte_idx)
                      {
                        cell_flags |= UI_BoxFlag_DrawSideRight;
                      }
                      if(row_idx == (S64)(selection.min/num_columns) && byte_is_selected)
                      {
                        cell_flags |= UI_BoxFlag_DrawSideTop;
                      }
                      if(selection.max != 0 && row_idx == (S64)((selection.max-1)/num_columns) && byte_is_selected)
                      {
                        cell_flags |= UI_BoxFlag_DrawSideBottom;
                      }
                      
                      if(cell_bd_rgba.w != 0)
                      {
                        ui_set_next_border_color(cell_bd_rgba);
                      }
                      UI_Box *cell_box = ui_build_box_from_key(cell_flags, ui_key_zero());
                      if(byte_is_selected)
                      {
                        ui_box_equip_display_fstrs(cell_box, &byte_fstrs_selected[byte_value]);
                      }
                      else
                      {
                        ui_box_equip_display_fstrs(cell_box, &byte_fstrs[byte_value]);
                      }
                    }
                  }
                }
                ui_spacer(ui_px(cell_big_glyph_advance*1.5f, 1.f));
                UI_WidthFill UI_TextPadding(0)
                {
                  MemoryZero(row_ascii_buffer, num_columns);
                  U64 num_bytes_this_row = 0;
                  for(U64 col_idx = 0; col_idx < num_columns; col_idx += 1)
                  {
                    U64 global_byte_idx = row_off + col_idx;
                    if(global_byte_idx < row_opl)
                    {
                      U64 visible_byte_idx = global_byte_idx - byte_range.min;
                      U8 byte_value = visible_byte_idx < data.size ? data.str[visible_byte_idx] : 0;
                      row_ascii_buffer[col_idx] = uishell_byte_is_printable_ascii(byte_value) ? byte_value : '.';
                      num_bytes_this_row += 1;
                    }
                  }
                  String8 ascii_text = str8(row_ascii_buffer, num_bytes_this_row);
                  UI_Box *ascii_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###ascii_row_%I64x", ascii_text, row_off);
                  if(dim_1u64(intersect_1u64(selection, row_range)) != 0)
                  {
                    Rng1U64 selection_in_row = intersect_1u64(row_range, selection);
                    DR_Bucket *bucket = dr_bucket_make();
                    DR_BucketScope(bucket)
                    {
                      Vec2F32 text_pos = ui_box_text_position(ascii_box);
                      dr_rect(r2f32p(text_pos.x + fnt_dim_from_tag_size_string(cell_font, cell_font_size, 0, 0, str8_prefix(ascii_text, selection_in_row.min-row_range.min)).x - cell_font_size/8.f,
                                     ascii_box->rect.y0,
                                     text_pos.x + fnt_dim_from_tag_size_string(cell_font, cell_font_size, 0, 0, str8_prefix(ascii_text, selection_in_row.max-row_range.min)).x + cell_font_size/4.f,
                                     ascii_box->rect.y1),
                              selection_color, 0, 0, 1.f);
                    }
                    ui_box_equip_draw_bucket(ascii_box, bucket);
                  }
                  if(mouse_hover_byte_num != 0 && contains_1u64(row_range, mouse_hover_byte_num-1))
                  {
                    DR_Bucket *bucket = dr_bucket_make();
                    DR_BucketScope(bucket)
                    {
                      Vec2F32 text_pos = ui_box_text_position(ascii_box);
                      U64 hover_off = mouse_hover_byte_num-1-row_range.min;
                      dr_rect(r2f32p(text_pos.x + fnt_dim_from_tag_size_string(cell_font, cell_font_size, 0, 0, str8_prefix(ascii_text, hover_off)).x - cell_font_size/8.f,
                                     ascii_box->rect.y0,
                                     text_pos.x + fnt_dim_from_tag_size_string(cell_font, cell_font_size, 0, 0, str8_prefix(ascii_text, hover_off+1)).x + cell_font_size/4.f,
                                     ascii_box->rect.y1),
                              border_color, 1.f, 3.f, 1.f);
                    }
                    ui_box_equip_draw_bucket(ascii_box, bucket);
                  }
                }
              }
            }
          }
        }
        
        {
          UI_Signal sig = ui_signal_from_box(scrollable_box);
          if(sig.scroll.y != 0)
          {
            S64 new_idx = scroll_pos.y.idx + sig.scroll.y;
            new_idx = clamp_1s64(scroll_idx_rng, new_idx);
            ui_scroll_pt_target_idx(&scroll_pos.y, new_idx);
          }
        }
      }
    }
  }
  
  rd_store_view_scroll_pos(scroll_pos);
  rd_store_view_param_u64(str8_lit("cursor"), bv->cursor_off);
  rd_store_view_param_u64(str8_lit("mark"), bv->mark_off);
  access_close(access);
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: Bitmap View

typedef struct UIShell_BitmapTopology UIShell_BitmapTopology;
struct UIShell_BitmapTopology
{
  Vec2S16 dim;
  R_Tex2DFormat fmt;
};

typedef struct UIShell_BitmapCanvasBoxDrawData UIShell_BitmapCanvasBoxDrawData;
struct UIShell_BitmapCanvasBoxDrawData
{
  Vec2F32 view_center_pos;
  F32 zoom;
};

internal AC_Artifact
uishell_bitmap_artifact_create(String8 key, B32 *cancel_signal, B32 *retry_out, U64 *gen_out)
{
  Access *access = access_open();
  
  U128 hash = {0};
  UIShell_BitmapTopology top = {0};
  {
    U64 key_read_off = 0;
    key_read_off += str8_deserial_read_struct(key, key_read_off, &hash);
    key_read_off += str8_deserial_read_struct(key, key_read_off, &top);
  }
  String8 data = c_data_from_hash(access, hash);
  
  R_Handle texture = {0};
  if(top.dim.x > 0 && top.dim.y > 0 &&
     data.size >= (U64)top.dim.x*(U64)top.dim.y*(U64)r_tex2d_format_bytes_per_pixel_table[top.fmt])
  {
    texture = r_tex2d_alloc(R_ResourceKind_Static, v2s32(top.dim.x, top.dim.y), top.fmt, data.str);
  }
  
  AC_Artifact artifact = {0};
  StaticAssert(sizeof(artifact) >= sizeof(texture), tex_artifact_size_check);
  MemoryCopy(&artifact, &texture, Min(sizeof(texture), sizeof(artifact)));
  
  access_close(access);
  return artifact;
}

internal void
uishell_bitmap_artifact_destroy(AC_Artifact artifact)
{
  R_Handle texture = {0};
  MemoryCopy(&texture, &artifact, Min(sizeof(texture), sizeof(artifact)));
  r_tex2d_release(texture);
}

internal Vec2F32
uishell_bitmap_screen_from_canvas_pos(Vec2F32 view_center_pos, F32 zoom, Rng2F32 rect, Vec2F32 cvs)
{
  Vec2F32 scr =
  {
    (rect.x0+rect.x1)/2 + (cvs.x - view_center_pos.x) * zoom,
    (rect.y0+rect.y1)/2 + (cvs.y - view_center_pos.y) * zoom,
  };
  return scr;
}

internal Rng2F32
uishell_bitmap_screen_from_canvas_rect(Vec2F32 view_center_pos, F32 zoom, Rng2F32 rect, Rng2F32 cvs)
{
  Rng2F32 scr = r2f32(uishell_bitmap_screen_from_canvas_pos(view_center_pos, zoom, rect, cvs.p0),
                      uishell_bitmap_screen_from_canvas_pos(view_center_pos, zoom, rect, cvs.p1));
  return scr;
}

internal Vec2F32
uishell_bitmap_canvas_from_screen_pos(Vec2F32 view_center_pos, F32 zoom, Rng2F32 rect, Vec2F32 scr)
{
  Vec2F32 cvs =
  {
    (scr.x - (rect.x0+rect.x1)/2) / zoom + view_center_pos.x,
    (scr.y - (rect.y0+rect.y1)/2) / zoom + view_center_pos.y,
  };
  return cvs;
}

internal Rng2F32
uishell_bitmap_canvas_from_screen_rect(Vec2F32 view_center_pos, F32 zoom, Rng2F32 rect, Rng2F32 scr)
{
  Rng2F32 cvs = r2f32(uishell_bitmap_canvas_from_screen_pos(view_center_pos, zoom, rect, scr.p0),
                      uishell_bitmap_canvas_from_screen_pos(view_center_pos, zoom, rect, scr.p1));
  return cvs;
}

internal UI_BOX_CUSTOM_DRAW(uishell_bitmap_view_canvas_box_draw)
{
  UIShell_BitmapCanvasBoxDrawData *draw_data = (UIShell_BitmapCanvasBoxDrawData *)user_data;
  Rng2F32 rect_scrn = box->rect;
  Rng2F32 rect_cvs = uishell_bitmap_canvas_from_screen_rect(draw_data->view_center_pos, draw_data->zoom, rect_scrn, rect_scrn);
  F32 grid_cell_size_cvs = box->font_size*10.f;
  F32 grid_line_thickness_px = Max(2.f, box->font_size*0.1f);
  Vec4F32 grid_line_color = {0};
  UI_TagF("weak")
  {
    grid_line_color = ui_color_from_name(str8_lit("text"));
  }
  for EachEnumVal(Axis2, axis)
  {
    for(F32 v = rect_cvs.p0.v[axis] - mod_f32(rect_cvs.p0.v[axis], grid_cell_size_cvs);
        v < rect_cvs.p1.v[axis];
        v += grid_cell_size_cvs)
    {
      Vec2F32 p_cvs = {0};
      p_cvs.v[axis] = v;
      Vec2F32 p_scr = uishell_bitmap_screen_from_canvas_pos(draw_data->view_center_pos, draw_data->zoom, rect_scrn, p_cvs);
      Rng2F32 rect = {0};
      rect.p0.v[axis] = p_scr.v[axis] - grid_line_thickness_px/2;
      rect.p1.v[axis] = p_scr.v[axis] + grid_line_thickness_px/2;
      rect.p0.v[axis2_flip(axis)] = box->rect.p0.v[axis2_flip(axis)];
      rect.p1.v[axis2_flip(axis)] = box->rect.p1.v[axis2_flip(axis)];
      dr_rect(rect, grid_line_color, 0, 0, 1.f);
    }
  }
}

EV_EXPAND_RULE_INFO_FUNCTION_DEF(bitmap)
{
  EV_ExpandInfo info = {0};
  info.row_count = 8;
  info.single_item = 1;
  return info;
}

RD_VIEW_UI_FUNCTION_DEF(bitmap)
{
  Temp scratch = scratch_begin(0, 0);
  Access *access = access_open();
  
  Vec2S32 dim = v2s32((S32)rd_view_setting_u64_from_name(str8_lit("w")), (S32)rd_view_setting_u64_from_name(str8_lit("h")));
  String8 fmt_string = rd_view_setting_from_name(str8_lit("fmt"));
  R_Tex2DFormat fmt = R_Tex2DFormat_RGBA8;
  for EachEnumVal(R_Tex2DFormat, f)
  {
    if(str8_match(fmt_string, r_tex2d_format_display_string_table[f], StringMatchFlag_CaseInsensitive))
    {
      fmt = f;
      break;
    }
  }
  Rng1U64 eval_range = e_range_from_eval(eval);
  U64 base_offset = eval_range.min;
  U64 expected_size = dim.x*dim.y*r_tex2d_format_bytes_per_pixel_table[fmt];
  Rng1U64 offset_range = r1u64(base_offset, base_offset + expected_size);
  
  F32 zoom = rd_view_setting_value_from_name(str8_lit("zoom")).f32;
  Vec2F32 view_center_pos =
  {
    rd_view_setting_value_from_name(str8_lit("x")).f32,
    rd_view_setting_value_from_name(str8_lit("y")).f32,
  };
  if(zoom == 0)
  {
    F32 available_dim_y = dim_2f32(rect).y;
    F32 image_dim_y = (F32)dim.y;
    zoom = image_dim_y != 0 ? (available_dim_y / image_dim_y) * 0.8f : 1.f;
  }
  
  C_Key texture_key = rd_key_from_eval_space_range(eval.space, offset_range, 0);
  UIShell_BitmapTopology topology = {v2s16(dim.x, dim.y), fmt};
  U128 data_hash = {0};
  R_Handle texture = {0};
  for EachIndex(rewind_idx, C_KEY_HASH_HISTORY_COUNT)
  {
    U128 hash = c_hash_from_key(texture_key, rewind_idx);
#pragma pack(push, 1)
    struct
    {
      U128 hash;
      UIShell_BitmapTopology top;
    }
    key_data = {hash, topology};
#pragma pack(pop)
    String8 key = str8_struct(&key_data);
    AC_Artifact artifact = ac_artifact_from_key(access, key, uishell_bitmap_artifact_create, uishell_bitmap_artifact_destroy, 0);
    R_Handle texture_candidate = {0};
    MemoryCopy(&texture_candidate, &artifact, Min(sizeof(texture_candidate), sizeof(artifact)));
    if(!r_handle_match(texture_candidate, r_handle_zero()))
    {
      data_hash = hash;
      texture = texture_candidate;
      break;
    }
  }
  String8 data = c_data_from_hash(access, data_hash);
  
  if(offset_range.max != offset_range.min &&
     eval.string.size != 0 &&
     eval.msgs.max_kind == E_MsgKind_Null &&
     (u128_match(data_hash, u128_zero()) ||
      r_handle_match(texture, r_handle_zero()) ||
      data.size == 0))
  {
    rd_store_view_loading_info(1, 0, 0);
  }
  
  UI_Box *canvas_box = &ui_nil_box;
  Vec2F32 canvas_dim = dim_2f32(rect);
  Rng2F32 canvas_rect = r2f32p(0, 0, canvas_dim.x, canvas_dim.y);
  UI_Rect(canvas_rect)
  {
    canvas_box = ui_build_box_from_stringf(UI_BoxFlag_Clip|UI_BoxFlag_Clickable|UI_BoxFlag_Scroll, "bmp_canvas");
  }
  
  UI_Signal canvas_sig = ui_signal_from_box(canvas_box);
  {
    if(ui_dragging(canvas_sig))
    {
      if(ui_pressed(canvas_sig))
      {
        uishell_cmd("focus_panel");
        ui_store_drag_struct(&view_center_pos);
      }
      Vec2F32 start_view_center_pos = *ui_get_drag_struct(Vec2F32);
      Vec2F32 drag_delta_scr = ui_drag_delta();
      Vec2F32 drag_delta_cvs = scale_2f32(drag_delta_scr, 1.f/zoom);
      Vec2F32 new_view_center_pos = sub_2f32(start_view_center_pos, drag_delta_cvs);
      view_center_pos = new_view_center_pos;
    }
    if(canvas_sig.scroll.y != 0)
    {
      F32 new_zoom = zoom - zoom*canvas_sig.scroll.y/10.f;
      new_zoom = Clamp(1.f/256.f, new_zoom, 256.f);
      Vec2F32 mouse_scr_pre = sub_2f32(ui_mouse(), rect.p0);
      Vec2F32 mouse_cvs = uishell_bitmap_canvas_from_screen_pos(view_center_pos, zoom, canvas_rect, mouse_scr_pre);
      zoom = new_zoom;
      Vec2F32 mouse_scr_pst = uishell_bitmap_screen_from_canvas_pos(view_center_pos, zoom, canvas_rect, mouse_cvs);
      Vec2F32 drift_scr = sub_2f32(mouse_scr_pst, mouse_scr_pre);
      view_center_pos = add_2f32(view_center_pos, scale_2f32(drift_scr, 1.f/new_zoom));
    }
    if(ui_double_clicked(canvas_sig))
    {
      ui_kill_action();
      MemoryZeroStruct(&view_center_pos);
      zoom = 1.f;
    }
  }
  
  {
    UIShell_BitmapCanvasBoxDrawData *draw_data = push_array(ui_build_arena(), UIShell_BitmapCanvasBoxDrawData, 1);
    draw_data->view_center_pos = view_center_pos;
    draw_data->zoom = zoom;
    ui_box_equip_custom_draw(canvas_box, uishell_bitmap_view_canvas_box_draw, draw_data);
  }
  
  Rng2F32 img_rect_cvs = r2f32p(-topology.dim.x/2, -topology.dim.y/2, +topology.dim.x/2, +topology.dim.y/2);
  Rng2F32 img_rect_scr = uishell_bitmap_screen_from_canvas_rect(view_center_pos, zoom, canvas_rect, img_rect_cvs);
  
  Vec2S32 mouse_bmp = {-1, -1};
  if(ui_hovering(canvas_sig) && !ui_dragging(canvas_sig))
  {
    Vec2F32 mouse_scr = sub_2f32(ui_mouse(), rect.p0);
    Vec2F32 mouse_cvs = uishell_bitmap_canvas_from_screen_pos(view_center_pos, zoom, canvas_rect, mouse_scr);
    if(contains_2f32(img_rect_cvs, mouse_cvs))
    {
      mouse_bmp = v2s32((S32)(mouse_cvs.x-img_rect_cvs.x0), (S32)(mouse_cvs.y-img_rect_cvs.y0));
      S64 off_px = mouse_bmp.y*topology.dim.x + mouse_bmp.x;
      S64 off_bytes = off_px*r_tex2d_format_bytes_per_pixel_table[topology.fmt];
      if(0 <= off_bytes && off_bytes+r_tex2d_format_bytes_per_pixel_table[topology.fmt] <= data.size &&
         r_tex2d_format_bytes_per_pixel_table[topology.fmt] != 0)
      {
        B32 color_is_good = 1;
        Vec4F32 color = {0};
        switch(topology.fmt)
        {
          default:{color_is_good = 0;}break;
          case R_Tex2DFormat_R8:     {color = v4f32(((U8 *)(data.str+off_bytes))[0]/255.f, 0, 0, 1);}break;
          case R_Tex2DFormat_RG8:    {color = v4f32(((U8 *)(data.str+off_bytes))[0]/255.f, ((U8 *)(data.str+off_bytes))[1]/255.f, 0, 1);}break;
          case R_Tex2DFormat_RGBA8:  {color = v4f32(((U8 *)(data.str+off_bytes))[0]/255.f, ((U8 *)(data.str+off_bytes))[1]/255.f, ((U8 *)(data.str+off_bytes))[2]/255.f, ((U8 *)(data.str+off_bytes))[3]/255.f);}break;
          case R_Tex2DFormat_BGRA8:  {color = v4f32(((U8 *)(data.str+off_bytes))[2]/255.f, ((U8 *)(data.str+off_bytes))[1]/255.f, ((U8 *)(data.str+off_bytes))[0]/255.f, ((U8 *)(data.str+off_bytes))[3]/255.f);}break;
          case R_Tex2DFormat_R16:    {color = v4f32(((U16 *)(data.str+off_bytes))[0]/(F32)max_U16, 0, 0, 1);}break;
          case R_Tex2DFormat_RGBA16: {color = v4f32(((U16 *)(data.str+off_bytes))[0]/(F32)max_U16, ((U16 *)(data.str+off_bytes))[1]/(F32)max_U16, ((U16 *)(data.str+off_bytes))[2]/(F32)max_U16, ((U16 *)(data.str+off_bytes))[3]/(F32)max_U16);}break;
          case R_Tex2DFormat_R32:    {color = v4f32(((F32 *)(data.str+off_bytes))[0], 0, 0, 1);}break;
          case R_Tex2DFormat_RG32:   {color = v4f32(((F32 *)(data.str+off_bytes))[0], ((F32 *)(data.str+off_bytes))[1], 0, 1);}break;
          case R_Tex2DFormat_RGBA32: {color = v4f32(((F32 *)(data.str+off_bytes))[0], ((F32 *)(data.str+off_bytes))[1], ((F32 *)(data.str+off_bytes))[2], ((F32 *)(data.str+off_bytes))[3]);}break;
        }
        if(color_is_good)
        {
          Vec4F32 hsva = hsva_from_rgba(color);
          ui_do_color_tooltip_hsva(hsva);
        }
      }
    }
  }
  
  UI_Parent(canvas_box)
  {
    if(0 <= mouse_bmp.x && mouse_bmp.x < dim.x &&
       0 <= mouse_bmp.y && mouse_bmp.y < dim.y)
    {
      F32 pixel_size_scr = 1.f*zoom;
      Rng2F32 indicator_rect_scr = r2f32p(img_rect_scr.x0 + mouse_bmp.x*pixel_size_scr,
                                          img_rect_scr.y0 + mouse_bmp.y*pixel_size_scr,
                                          img_rect_scr.x0 + (mouse_bmp.x+1)*pixel_size_scr,
                                          img_rect_scr.y0 + (mouse_bmp.y+1)*pixel_size_scr);
      UI_Rect(indicator_rect_scr)
      {
        ui_build_box_from_key(UI_BoxFlag_DrawBorder|UI_BoxFlag_Floating, ui_key_zero());
      }
    }
    UI_Rect(img_rect_scr) UI_Flags(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawDropShadow|UI_BoxFlag_Floating)
    {
      ui_image(texture, R_Tex2DSampleKind_Nearest, r2f32p(0, 0, (F32)dim.x, (F32)dim.y), v4f32(1, 1, 1, 1), 0, str8_lit("bmp_image"));
    }
  }
  
  rd_store_view_param_f32(str8_lit("zoom"), zoom);
  rd_store_view_param_f32(str8_lit("x"), view_center_pos.x);
  rd_store_view_param_f32(str8_lit("y"), view_center_pos.y);
  
  access_close(access);
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: Color View

typedef struct UIShell_EvalColor UIShell_EvalColor;
struct UIShell_EvalColor
{
  Vec4F32 rgba;
  E_Eval rgba_evals[4];
};

internal UIShell_EvalColor
uishell_eval_color_from_eval(E_Eval eval)
{
  Temp scratch = scratch_begin(0, 0);
  
  E_Eval component_evals[4] = {0};
  {
    typedef struct LeafTask LeafTask;
    struct LeafTask
    {
      LeafTask *next;
      E_Eval eval;
    };
    U64 num_components_left = 4;
    LeafTask start_task = {0, eval};
    LeafTask *first_task = &start_task;
    LeafTask *last_task = first_task;
    for(LeafTask *t = first_task; t != 0 && num_components_left > 0; t = t->next)
    {
      E_Type *type = e_type_from_key(e_type_key_unwrap(t->eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative));
      switch(type->kind)
      {
        default:{}break;
        case E_TypeKind_U32:
        case E_TypeKind_S32:
        case E_TypeKind_U64:
        case E_TypeKind_S64:
        {
          component_evals[0] = e_value_eval_from_eval(e_eval_wrapf(t->eval, "(float32)(($ & 0xff000000) >> 24) / 255.f"));
          component_evals[1] = e_value_eval_from_eval(e_eval_wrapf(t->eval, "(float32)(($ & 0x00ff0000) >> 16) / 255.f"));
          component_evals[2] = e_value_eval_from_eval(e_eval_wrapf(t->eval, "(float32)(($ & 0x0000ff00) >> 8) / 255.f"));
          component_evals[3] = e_value_eval_from_eval(e_eval_wrapf(t->eval, "(float32)(($ & 0x000000ff) >> 0) / 255.f"));
          num_components_left -= 4;
        }break;
        case E_TypeKind_Array:
        {
          component_evals[0] = e_value_eval_from_eval(e_eval_wrapf(t->eval, "(float32)($[0])"));
          component_evals[1] = e_value_eval_from_eval(e_eval_wrapf(t->eval, "(float32)($[1])"));
          component_evals[2] = e_value_eval_from_eval(e_eval_wrapf(t->eval, "(float32)($[2])"));
          component_evals[3] = e_value_eval_from_eval(e_eval_wrapf(t->eval, "(float32)($[3])"));
          num_components_left -= 4;
        }break;
      }
    }
  }
  
  UIShell_EvalColor result = {0};
  {
    E_Type *lens_type = e_type_from_key(eval.irtree.type_key);
    for(E_Type *t = lens_type; t->kind == E_TypeKind_Lens; t = e_type_from_key(t->direct_type_key))
    {
      if(str8_match(t->name, str8_lit("color"), 0))
      {
        lens_type = t;
        break;
      }
    }
    String8 format_string = str8_lit("rgba");
    if(lens_type->kind == E_TypeKind_Lens && lens_type->count > 0)
    {
      format_string = lens_type->args[0]->string;
    }
    if(str8_match(format_string, str8_lit("rgba"), 0))
    {
      result.rgba_evals[0] = component_evals[0];
      result.rgba_evals[1] = component_evals[1];
      result.rgba_evals[2] = component_evals[2];
      result.rgba_evals[3] = component_evals[3];
    }
    else if(str8_match(format_string, str8_lit("argb"), 0))
    {
      result.rgba_evals[0] = component_evals[1];
      result.rgba_evals[1] = component_evals[2];
      result.rgba_evals[2] = component_evals[3];
      result.rgba_evals[3] = component_evals[0];
    }
    else if(str8_match(format_string, str8_lit("bgra"), 0))
    {
      result.rgba_evals[0] = component_evals[2];
      result.rgba_evals[1] = component_evals[1];
      result.rgba_evals[2] = component_evals[0];
      result.rgba_evals[3] = component_evals[3];
    }
    else if(str8_match(format_string, str8_lit("abgr"), 0))
    {
      result.rgba_evals[0] = component_evals[3];
      result.rgba_evals[1] = component_evals[2];
      result.rgba_evals[2] = component_evals[1];
      result.rgba_evals[3] = component_evals[0];
    }
    for EachIndex(idx, 4)
    {
      result.rgba.v[idx] = e_value_eval_from_eval(result.rgba_evals[idx]).value.f32;
    }
  }
  
  scratch_end(scratch);
  return result;
}

EV_EXPAND_RULE_INFO_FUNCTION_DEF(color)
{
  EV_ExpandInfo info = {0};
  info.row_count = 12;
  info.single_item = 1;
  return info;
}

RD_VIEW_UI_FUNCTION_DEF(color)
{
  Temp scratch = scratch_begin(0, 0);
  
  typedef struct UIShell_ColorViewState UIShell_ColorViewState;
  struct UIShell_ColorViewState
  {
    B32 initialized;
    U32 start_rgba_u32;
    Vec4F32 hsva;
  };
  UIShell_ColorViewState *state = rd_view_state(UIShell_ColorViewState);
  UIShell_EvalColor eval_color = uishell_eval_color_from_eval(eval);
  U32 rgba_u32 = u32_from_rgba(eval_color.rgba);
  if(!state->initialized || rgba_u32 != state->start_rgba_u32)
  {
    Vec4F32 rgba = eval_color.rgba;
    Vec4F32 hsva = hsva_from_rgba(rgba);
    state->initialized = 1;
    state->start_rgba_u32 = rgba_u32;
    state->hsva = hsva;
  }
  Vec4F32 hsva = state->hsva;
  Vec4F32 rgba = rgba_from_hsva(hsva);
  
  Vec2F32 dim = dim_2f32(rect);
  F32 sv_dim_px = Min(dim.x, dim.y);
  F32 padding = sv_dim_px*0.2f;
  sv_dim_px -= padding*2.f;
  sv_dim_px = Min(sv_dim_px, ui_top_font_size()*70.f);
  
  {
    UI_WidthFill UI_HeightFill
      UI_PrefHeight(ui_children_sum(1)) UI_Column UI_Padding(ui_pct(1.f, 0.f))
      UI_PrefHeight(ui_children_sum(1)) UI_Row UI_Padding(ui_pct(1.f, 0.f))
      UI_PrefWidth(ui_px(sv_dim_px, 1.f))
      UI_PrefHeight(ui_px(sv_dim_px, 1.f))
      RD_Font(RD_FontSlot_Code)
    {
      UI_Signal sv_sig = ui_sat_val_pickerf(hsva.x, &hsva.y, &hsva.z, "sat_val_picker");
      UI_Signal h_sig  = {0};
      UI_Signal a_sig  = {0};
      ui_spacer(ui_em(1.f, 1.f));
      UI_PrefWidth(ui_em(3.f, 1.f))
      {
        h_sig  = ui_hue_pickerf(&hsva.x, hsva.y, hsva.z, "hue_picker");
      }
      ui_spacer(ui_em(1.f, 1.f));
      UI_PrefWidth(ui_em(3.f, 1.f))
      {
        a_sig  = ui_alpha_pickerf(&hsva.w, "alpha_picker");
      }
      ui_spacer(ui_em(1.f, 1.f));
      UI_PrefWidth(ui_children_sum(1)) UI_Column
      {
        UI_PrefWidth(ui_em(6.f, 0.f)) UI_PrefHeight(ui_em(6.f, 0.f))
          UI_BackgroundColor(linear_from_srgba(v4f32(rgba.x, rgba.y, rgba.z, 1.f)))
          UI_CornerRadius(4.f)
          UI_PrefWidth(ui_em(6.f, 1.f)) UI_PrefHeight(ui_em(6.f, 1.f))
          ui_build_box_from_string(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground, str8_lit(""));
        ui_spacer(ui_em(2.f, 1.f));
        UI_PrefWidth(ui_children_sum(1)) UI_PrefHeight(ui_children_sum(1)) UI_Row
        {
          UI_PrefWidth(ui_children_sum(1)) UI_Column UI_PrefWidth(ui_text_dim(10, 1)) UI_PrefHeight(ui_em(2.f, 0.f)) RD_Font(RD_FontSlot_Code)
            UI_TagF("weak")
          {
            ui_labelf("Hex");
            ui_labelf("R");
            ui_labelf("G");
            ui_labelf("B");
            ui_labelf("H");
            ui_labelf("S");
            ui_labelf("V");
            ui_labelf("A");
          }
          UI_PrefWidth(ui_children_sum(1)) UI_Column UI_PrefWidth(ui_text_dim(10, 1)) UI_PrefHeight(ui_em(2.f, 0.f)) RD_Font(RD_FontSlot_Code)
          {
            String8 hex_string = hex_string_from_rgba_4f32(scratch.arena, rgba);
            ui_label(hex_string);
            ui_labelf("%.2f", rgba.x);
            ui_labelf("%.2f", rgba.y);
            ui_labelf("%.2f", rgba.z);
            ui_labelf("%.2f", hsva.x);
            ui_labelf("%.2f", hsva.y);
            ui_labelf("%.2f", hsva.z);
            ui_labelf("%.2f", rgba.w);
          }
        }
      }
      if(ui_dragging(h_sig) || ui_dragging(sv_sig) || ui_dragging(a_sig))
      {
        E_Type *type = e_type_from_key(e_type_key_unwrap(eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative));
        if(type->kind == E_TypeKind_U32 ||
           type->kind == E_TypeKind_S32 ||
           type->kind == E_TypeKind_U64 ||
           type->kind == E_TypeKind_S64)
        {
          Vec4F32 new_rgba = rgba_from_hsva(hsva);
          U32 u32 = u32_from_rgba(new_rgba);
          String8 string = push_str8f(scratch.arena, "0x%x", u32);
          if(rd_commit_eval_value_string(eval, string))
          {
            state->start_rgba_u32 = u32;
            state->hsva = hsva;
          }
        }
      }
    }
  }
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: Geo3D View

typedef struct UIShell_Geo3DViewState UIShell_Geo3DViewState;
struct UIShell_Geo3DViewState
{
  F32 yaw;
  F32 pitch;
  F32 zoom;
};

typedef struct UIShell_Geo3DBoxDrawData UIShell_Geo3DBoxDrawData;
struct UIShell_Geo3DBoxDrawData
{
  F32 yaw;
  F32 pitch;
  F32 zoom;
  R_Handle vertex_buffer;
  R_Handle index_buffer;
};

internal AC_Artifact
uishell_geo3d_artifact_create(String8 key, B32 *cancel_signal, B32 *retry_out, U64 *gen_out)
{
  Access *access = access_open();
  U128 hash = {0};
  str8_deserial_read_struct(key, 0, &hash);
  String8 data = c_data_from_hash(access, hash);
  R_Handle buffer = {0};
  if(data.size != 0)
  {
    buffer = r_buffer_alloc(R_ResourceKind_Static, data.size, data.str);
  }
  AC_Artifact artifact = {0};
  MemoryCopy(&artifact, &buffer, Min(sizeof(artifact), sizeof(buffer)));
  access_close(access);
  return artifact;
}

internal void
uishell_geo3d_artifact_destroy(AC_Artifact artifact)
{
  R_Handle buffer = {0};
  MemoryCopy(&buffer, &artifact, Min(sizeof(buffer), sizeof(artifact)));
  r_buffer_release(buffer);
}

internal R_Handle
uishell_geo3d_buffer_from_key(Access *access, C_Key key)
{
  R_Handle result = {0};
  for EachIndex(rewind_idx, C_KEY_HASH_HISTORY_COUNT)
  {
    U128 hash = c_hash_from_key(key, rewind_idx);
    AC_Artifact artifact = ac_artifact_from_key(access, str8_struct(&hash), uishell_geo3d_artifact_create, uishell_geo3d_artifact_destroy, 0);
    R_Handle buffer = {0};
    MemoryCopy(&buffer, &artifact, Min(sizeof(buffer), sizeof(artifact)));
    if(!r_handle_match(buffer, r_handle_zero()))
    {
      result = buffer;
      break;
    }
  }
  return result;
}

internal UI_BOX_CUSTOM_DRAW(uishell_geo3d_box_draw)
{
  UIShell_Geo3DBoxDrawData *draw_data = (UIShell_Geo3DBoxDrawData *)user_data;
  
  Rng2F32 clip = box->rect;
  for(UI_Box *b = box->parent; !ui_box_is_nil(b); b = b->parent)
  {
    if(b->flags & UI_BoxFlag_Clip)
    {
      clip = intersect_2f32(b->rect, clip);
    }
  }
  
  Vec3F32 target = {0};
  Vec3F32 eye = v3f32(draw_data->zoom*cos_f32(draw_data->yaw)*sin_f32(draw_data->pitch),
                      draw_data->zoom*sin_f32(draw_data->yaw)*sin_f32(draw_data->pitch),
                      draw_data->zoom*cos_f32(draw_data->pitch));
  
  Vec2F32 box_dim = dim_2f32(box->rect);
  R_PassParams_Geo3D *pass = dr_geo3d_begin(box->rect,
                                            make_look_at_4x4f32(eye, target, v3f32(0, 0, 1)),
                                            make_perspective_4x4f32(0.25f, box_dim.x/box_dim.y, 0.1f, 500.f));
  pass->clip = clip;
  dr_mesh(draw_data->vertex_buffer, draw_data->index_buffer, R_GeoTopologyKind_Triangles, R_GeoVertexFlag_TexCoord|R_GeoVertexFlag_Normals|R_GeoVertexFlag_RGB, r_handle_zero(), mat_4x4f32(1.f));
}

EV_EXPAND_RULE_INFO_FUNCTION_DEF(geo3d)
{
  EV_ExpandInfo info = {0};
  info.row_count = 16;
  info.single_item = 1;
  return info;
}

RD_VIEW_UI_FUNCTION_DEF(geo3d)
{
  Temp scratch = scratch_begin(0, 0);
  Access *access = access_open();
  UIShell_Geo3DViewState *state = rd_view_state(UIShell_Geo3DViewState);
  
  U64 count         = rd_view_setting_u64_from_name(str8_lit("count"));
  U64 vtx_base_off  = rd_view_setting_addr_from_name(str8_lit("vtx"));
  U64 vtx_size      = rd_view_setting_u64_from_name(str8_lit("vtx_size"));
  F32 yaw_target    = rd_view_setting_f32_from_name(str8_lit("yaw"));
  F32 pitch_target  = rd_view_setting_f32_from_name(str8_lit("pitch"));
  F32 zoom_target   = rd_view_setting_f32_from_name(str8_lit("zoom"));
  
  Rng1U64 eval_range = e_range_from_eval(eval);
  U64 base_offset = eval_range.min;
  Rng1U64 idxs_range = r1u64(base_offset, base_offset+count*sizeof(U32));
  Rng1U64 vtxs_range = r1u64(vtx_base_off, vtx_base_off+vtx_size);
  C_Key idxs_key = rd_key_from_eval_space_range(eval.space, idxs_range, 0);
  C_Key vtxs_key = rd_key_from_eval_space_range(eval.space, vtxs_range, 0);
  R_Handle idxs_buffer = uishell_geo3d_buffer_from_key(access, idxs_key);
  R_Handle vtxs_buffer = uishell_geo3d_buffer_from_key(access, vtxs_key);
  
  if(eval.string.size != 0 &&
     eval.msgs.max_kind == E_MsgKind_Null &&
     (r_handle_match(idxs_buffer, r_handle_zero()) ||
      r_handle_match(vtxs_buffer, r_handle_zero())))
  {
    rd_store_view_loading_info(1, 0, 0);
  }
  
  if(zoom_target == 0)
  {
    yaw_target   = -0.125f;
    pitch_target = -0.125f;
    zoom_target  = 3.5f;
  }
  
  {
    F32 fast_rate = 1 - pow_f32(2, (-60.f * rd_state->frame_dt));
    F32 slow_rate = 1 - pow_f32(2, (-30.f * rd_state->frame_dt));
    state->zoom  += (zoom_target - state->zoom) * slow_rate;
    state->yaw   += (yaw_target - state->yaw) * fast_rate;
    state->pitch += (pitch_target - state->pitch) * fast_rate;
    if(abs_f32(state->zoom  - zoom_target)  > 0.001f ||
       abs_f32(state->yaw   - yaw_target)   > 0.001f ||
       abs_f32(state->pitch - pitch_target) > 0.001f)
    {
      rd_request_frame();
    }
  }
  
  if(count != 0 && !r_handle_match(idxs_buffer, r_handle_zero()) && !r_handle_match(vtxs_buffer, r_handle_zero()))
  {
    Vec2F32 dim = dim_2f32(rect);
    UI_Box *box = &ui_nil_box;
    UI_FixedSize(dim)
    {
      box = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_Clickable|UI_BoxFlag_Scroll, "geo_box");
    }
    UI_Signal sig = ui_signal_from_box(box);
    if(ui_dragging(sig))
    {
      if(ui_pressed(sig))
      {
        uishell_cmd("focus_panel");
        Vec2F32 data = v2f32(yaw_target, pitch_target);
        ui_store_drag_struct(&data);
      }
      Vec2F32 drag_delta      = ui_drag_delta();
      Vec2F32 drag_start_data = *ui_get_drag_struct(Vec2F32);
      yaw_target   = drag_start_data.x + drag_delta.x/dim.x;
      pitch_target = drag_start_data.y + drag_delta.y/dim.y;
    }
    zoom_target += sig.scroll.y;
    zoom_target = Clamp(0.1f, zoom_target, 100.f);
    pitch_target = Clamp(-0.49f, pitch_target, -0.01f);
    UIShell_Geo3DBoxDrawData *draw_data = push_array(ui_build_arena(), UIShell_Geo3DBoxDrawData, 1);
    draw_data->yaw   = state->yaw;
    draw_data->pitch = state->pitch;
    draw_data->zoom  = state->zoom;
    draw_data->vertex_buffer  = vtxs_buffer;
    draw_data->index_buffer   = idxs_buffer;
    ui_box_equip_custom_draw(box, uishell_geo3d_box_draw, draw_data);
  }
  
  rd_store_view_param_f32(str8_lit("yaw"),   yaw_target);
  rd_store_view_param_f32(str8_lit("pitch"), pitch_target);
  rd_store_view_param_f32(str8_lit("zoom"),  zoom_target);
  
  access_close(access);
  scratch_end(scratch);
}
