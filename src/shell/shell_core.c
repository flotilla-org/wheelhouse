// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#undef LAYER_COLOR
#define LAYER_COLOR 0xf0a215ff

#include "shell_app_hooks.h"

////////////////////////////////
//~ rjf: Generated Code

#include "generated/shell.meta.c"

////////////////////////////////
//~ rjf: Registers Type Functions

internal void
uishell_regs_copy_contents(Arena *arena, UIShell_Regs *dst, UIShell_Regs *src)
{
  dst[0] = uishell_regs_copy(arena, src);
}
////////////////////////////////
//~ rjf: Commands Type Functions

internal void
uishell_cmd_list_push_new(Arena *arena, UIShell_CmdList *cmds, String8 name, UIShell_Regs *regs)
{
  UIShell_CmdNode *n = push_array(arena, UIShell_CmdNode, 1);
  n->cmd.name = push_str8_copy(arena, name);
  n->cmd.regs = push_array(arena, UIShell_Regs, 1);
  n->cmd.regs[0] = uishell_regs_copy(arena, regs);
  DLLPushBack(cmds->first, cmds->last, n);
  cmds->count += 1;
}

////////////////////////////////
//~ rjf: View UI Rule Functions

internal RD_ViewUIRuleMap *
rd_view_ui_rule_map_make(Arena *arena, U64 slots_count)
{
  RD_ViewUIRuleMap *map = push_array(arena, RD_ViewUIRuleMap, 1);
  map->slots_count = slots_count;
  map->slots = push_array(arena, RD_ViewUIRuleSlot, map->slots_count);
  return map;
}

internal void
rd_view_ui_rule_map_insert(Arena *arena, RD_ViewUIRuleMap *map, String8 string, RD_ViewUIFunctionType *ui)
{
  U64 hash = u64_djb2_hash_from_str8(string);
  U64 slot_idx = hash%map->slots_count;
  RD_ViewUIRuleNode *n = push_array(arena, RD_ViewUIRuleNode, 1);
  n->v.name = push_str8_copy(arena, string);
  n->v.ui = ui;
  SLLQueuePush(map->slots[slot_idx].first, map->slots[slot_idx].last, n);
}

internal RD_ViewUIRule *
rd_view_ui_rule_from_string(String8 string)
{
  RD_ViewUIRule *rule = &rd_nil_view_ui_rule;
  {
    RD_ViewUIRuleMap *map = rd_state->view_ui_rule_map;
    U64 hash = u64_djb2_hash_from_str8(string);
    U64 slot_idx = hash%map->slots_count;
    for(RD_ViewUIRuleNode *n = map->slots[slot_idx].first; n != 0; n = n->next)
    {
      if(str8_match(n->v.name, string, 0))
      {
        rule = &n->v;
        break;
      }
    }
  }
  return rule;
}

internal B32
rd_view_name_is_listed_in_app(String8 name)
{
  B32 result = 1;
  result = uishell_view_name_is_listed(name);
  return result;
}

#if !defined(RD_APP_NAME_SCHEMA_INFO_TABLE)
#  define RD_APP_NAME_SCHEMA_INFO_TABLE uishell_name_schema_info_table
#endif

#if !defined(RD_APP_VOCAB_INFO_TABLE)
#  define RD_APP_VOCAB_INFO_TABLE uishell_vocab_info_table
#endif

#if !defined(RD_APP_BINDING_VERSION_REMAP_OLD_NAME_TABLE)
#  define RD_APP_BINDING_VERSION_REMAP_OLD_NAME_TABLE uishell_binding_version_remap_old_name_table
#endif

#if !defined(RD_APP_BINDING_VERSION_REMAP_NEW_NAME_TABLE)
#  define RD_APP_BINDING_VERSION_REMAP_NEW_NAME_TABLE uishell_binding_version_remap_new_name_table
#endif

#define UISHELL_APP_REG_SLOT_X_LIST \
X(Null) \
X(Window) \
X(Panel) \
X(Tab) \
X(View) \
X(PrevTab) \
X(DstPanel) \
X(Cfg) \
X(CfgList) \
X(FilePath) \
X(Cursor) \
X(Mark) \
X(TextKey) \
X(LangKind) \
X(Vaddr) \
X(Expr) \
X(UIKey) \
X(OffPx) \
X(RegSlot) \
X(ForceConfirm) \
X(ForceFocus) \
X(DoImplicitRoot) \
X(DoLister) \
X(DoBigRows) \
X(NonGraphical) \
X(PreferNewTab) \
X(ActivateWithSingleClick) \
X(Dir2) \
X(String) \
X(CmdName) \
X(WMEvent)

internal UIShell_ContextRegSlot
uishell_context_reg_slot_from_app_reg_slot(UIShell_AppRegSlot slot)
{
  UIShell_ContextRegSlot result = UIShell_ContextRegSlot_Null;
  switch(slot)
  {
    default: break;
#define X(name) case UIShell_AppRegSlot_##name: {result = UIShell_ContextRegSlot_##name;}break;
    UISHELL_APP_REG_SLOT_X_LIST
#undef X
  }
  return result;
}

#undef UISHELL_APP_REG_SLOT_X_LIST

////////////////////////////////
//~ rjf: Global Cross-Window UI Interaction State Functions

internal B32
rd_drag_is_active(void)
{
  return ((rd_state->drag_drop_state == RD_DragDropState_Dragging) ||
          (rd_state->drag_drop_state == RD_DragDropState_Dropping));
}

internal void
rd_drag_begin(UIShell_ContextRegSlot slot)
{
  if(!rd_drag_is_active())
  {
    arena_clear(rd_state->drag_drop_arena);
    rd_state->drag_drop_regs = push_array(rd_state->drag_drop_arena, UIShell_Regs, 1);
    rd_state->drag_drop_regs[0] = uishell_regs_copy(rd_state->drag_drop_arena, uishell_regs());
    rd_state->drag_drop_regs_slot = slot;
    rd_state->drag_drop_state = RD_DragDropState_Dragging;
  }
}

internal B32
rd_drag_drop(void)
{
  B32 result = 0;
  if(rd_state->drag_drop_state == RD_DragDropState_Dropping)
  {
    result = 1;
    rd_state->drag_drop_state = RD_DragDropState_Null;
  }
  return result;
}

internal void
rd_drag_kill(void)
{
  rd_state->drag_drop_state = RD_DragDropState_Null;
}

internal void
rd_set_hover_regs(UIShell_ContextRegSlot slot)
{
  rd_state->next_hover_regs = push_array(rd_frame_arena(), UIShell_Regs, 1);
  rd_state->next_hover_regs[0] = uishell_regs_copy(rd_frame_arena(), uishell_regs());
  rd_state->next_hover_regs_slot = slot;
}

////////////////////////////////
//~ rjf: Config Functions

internal B32
rd_cfg_is_project_filtered(CFG_Node *cfg)
{
  CFG_Node *project = cfg_node_child_from_string(cfg, str8_lit("project"));
  B32 result = (project != &cfg_nil_node && project->first->string.size != 0 && !path_match_normalized(rd_state->project_path, project->first->string));
  return result;
}

internal Vec4F32
rd_hsva_from_cfg(CFG_Node *cfg)
{
  Vec4F32 hsva = {0};
  CFG_Node *hsva_root = cfg_node_child_from_string(cfg, str8_lit("hsva"));
  CFG_Node *h = hsva_root->first;
  CFG_Node *s = h->next;
  CFG_Node *v = s->next;
  CFG_Node *a = v->next;
  hsva.x = (F32)f64_from_str8(h->string);
  hsva.y = (F32)f64_from_str8(s->string);
  hsva.z = (F32)f64_from_str8(v->string);
  hsva.w = (F32)f64_from_str8(a->string);
  return hsva;
}

internal Vec4F32
rd_color_from_cfg(CFG_Node *cfg)
{
  Vec4F32 hsva = rd_hsva_from_cfg(cfg);
  Vec4F32 rgba = linear_from_srgba(rgba_from_hsva(hsva));
  return rgba;
}

internal B32
rd_disabled_from_cfg(CFG_Node *cfg)
{
  Temp scratch = scratch_begin(0, 0);
  MD_Node *child_schema = &md_nil_node;
  MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, cfg->string);
  for(MD_NodePtrNode *n = schemas.first; n != 0 && child_schema == &md_nil_node; n = n->next)
  {
    child_schema = md_child_from_string(n->v, str8_lit("enabled"), 0);
  }
  MD_Node *default_tag = md_tag_from_string(child_schema, str8_lit("default"), 0);
  String8 value_string = cfg_node_child_from_string(cfg, str8_lit("enabled"))->first->string;
  if(value_string.size == 0)
  {
    value_string = default_tag->first->string;
  }
  B32 is_enabled = !!e_value_from_string(value_string).u64;
  B32 is_disabled = !is_enabled;
  if(value_string.size == 0)
  {
    is_disabled = 0;
  }
  scratch_end(scratch);
  return is_disabled;
}

internal RD_Location
rd_location_from_cfg(CFG_Node *cfg)
{
  RD_Location dst_loc = {0};
  {
    CFG_Node *src_loc = cfg_node_child_from_string(cfg, str8_lit("source_location"));
    CFG_Node *addr_loc = cfg_node_child_from_string(cfg, str8_lit("address_location"));
    if(src_loc != &cfg_nil_node)
    {
      String8TxtPtPair loc_description = str8_txt_pt_pair_from_string(src_loc->first->string);
      dst_loc.file_path = loc_description.string;
      dst_loc.pt = loc_description.pt;
    }
    else if(addr_loc != &cfg_nil_node)
    {
      dst_loc.expr = addr_loc->first->string;
    }
  }
  return dst_loc;
}

internal String8
rd_name_from_cfg(CFG_Node *cfg)
{
  CFG_Node *name_root = cfg_node_child_from_string(cfg, str8_lit("name"));
  String8 result = name_root->first->string;
  return result;
}

internal String8
rd_label_from_cfg(CFG_Node *cfg)
{
  CFG_Node *label_root = cfg_node_child_from_string(cfg, str8_lit("label"));
  String8 result = label_root->first->string;
  return result;
}

internal String8
rd_expr_from_cfg(CFG_Node *cfg)
{
  CFG_Node *expr_root = cfg_node_child_from_string(cfg, str8_lit("expression"));
  String8 result = expr_root->first->string;
  return result;
}

internal String8
rd_path_from_cfg(CFG_Node *cfg)
{
  CFG_Node *root = cfg_node_child_from_string(cfg, str8_lit("path"));
  String8 result = root->first->string;
  return result;
}

internal String8
rd_default_setting_from_names(String8 schema_name, String8 setting_name)
{
  String8 result = {0};
  {
    Temp scratch = scratch_begin(0, 0);
    MD_Node *setting_schema = &md_nil_node;
    MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, schema_name);
    for(MD_NodePtrNode *n = schemas.first; n != 0 && setting_schema == &md_nil_node; n = n->next)
    {
      setting_schema = md_child_from_string(n->v, setting_name, 0);
    }
    if(setting_schema != &md_nil_node)
    {
      MD_Node *default_tag = md_tag_from_string(setting_schema, str8_lit("default"), 0);
      if(default_tag != &md_nil_node)
      {
        result = default_tag->first->string;
      }
    }
    scratch_end(scratch);
  }
  return result;
}

internal String8
rd_setting_from_name(String8 name)
{
  String8 result = {0};
  if(name.size != 0)
  {
    Temp scratch = scratch_begin(0, 0);
    
    // rjf: find most-granular config scopes to begin looking for the setting
    typedef struct CfgSeedTask CfgSeedTask;
    struct CfgSeedTask
    {
      CfgSeedTask *next;
      CFG_Node *cfg;
      B32 allow_bucket_chains;
    };
    CFG_Node *view_cfg = cfg_node_from_id(uishell_regs()->view);
    if(view_cfg == &cfg_nil_node)
    {
      view_cfg = cfg_node_from_id(uishell_regs()->tab);
    }
    CfgSeedTask panel_task = {0, &cfg_nil_node, 1};
    if(panel_task.cfg == &cfg_nil_node) { panel_task.cfg = cfg_node_from_id(uishell_regs()->panel); }
    if(panel_task.cfg == &cfg_nil_node) { panel_task.cfg = cfg_node_from_id(uishell_regs()->window); }
    CfgSeedTask view_task = {&panel_task, view_cfg, 1};
    CfgSeedTask *first_task = &view_task;
    CfgSeedTask *last_task = &panel_task;
    
    // rjf: for each task, look for the setting, follow parent chain upwards
    CFG_Node *setting = &cfg_nil_node;
    for(CfgSeedTask *t = first_task; t != 0; t = t->next)
    {
      for(CFG_Node *cfg = t->cfg; cfg != &cfg_nil_node; cfg = cfg->parent)
      {
        setting = cfg_node_child_from_string(cfg, name);
        if(setting != &cfg_nil_node)
        {
          goto break_all;
        }
        if(cfg->parent == cfg_node_root() && t->allow_bucket_chains)
        {
          String8 next_bucket = {0};
          B32 allow_bucket_chains = 0;
          if(str8_match(cfg->string, str8_lit("user"), 0))
          {
            next_bucket = str8_lit("project");
          }
          else if(str8_match(cfg->string, str8_lit("project"), 0))
          {
            next_bucket = str8_lit("user");
          }
          else
          {
            allow_bucket_chains = 1;
            next_bucket = str8_lit("user");
          }
          if(next_bucket.size != 0)
          {
            CfgSeedTask *task = push_array(scratch.arena, CfgSeedTask, 1);
            SLLQueuePush(first_task, last_task, task);
            task->cfg = cfg_node_child_from_string(cfg_node_root(), next_bucket);
            task->allow_bucket_chains = allow_bucket_chains;
          }
        }
      }
    }
    break_all:;
    
    // rjf: return resultant child string stored under this key
    result = setting->first->string;
    
    // rjf: no result -> look for default in schemas
    if(result.size == 0)
    {
      for(CfgSeedTask *t = first_task; t != 0; t = t->next)
      {
        for(CFG_Node *cfg = t->cfg; cfg != &cfg_nil_node; cfg = cfg->parent)
        {
          result = rd_default_setting_from_names(cfg->string, name);
          if(result.size != 0)
          {
            goto break_all2;
          }
        }
      }
      break_all2:;
    }
    
    scratch_end(scratch);
  }
  return result;
}

internal B32
rd_setting_b32_from_name(String8 name)
{
  B32 result = 0;
  String8 value = rd_setting_from_name(name);
  if(value.size != 0)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 expr = push_str8f(scratch.arena, "raw((bool)(%S))", value);
    E_Eval eval = e_eval_from_string(expr);
    result = !!e_value_eval_from_eval(eval).value.u64;
    scratch_end(scratch);
  }
  return result;
}

internal U64
rd_setting_u64_from_name(String8 name)
{
  U64 result = 0;
  String8 value = rd_setting_from_name(name);
  if(value.size != 0)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 expr = push_str8f(scratch.arena, "raw((uint64)(%S))", value);
    E_Eval eval = e_eval_from_string(expr);
    result = e_value_eval_from_eval(eval).value.u64;
    scratch_end(scratch);
  }
  return result;
}

internal F32
rd_setting_f32_from_name(String8 name)
{
  F32 result = 0.f;
  String8 value = rd_setting_from_name(name);
  if(value.size != 0)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 expr = push_str8f(scratch.arena, "raw((float32)(%S))", value);
    E_Eval eval = e_eval_from_string(expr);
    result = e_value_eval_from_eval(eval).value.f32;
    scratch_end(scratch);
  }
  return result;
}

////////////////////////////////
//~ rjf: Tweaks

internal RD_TweakNode *
rd_tweak_node_from_name(String8 name, F32 default_value, String8 file)
{
  U64 hash = u64_djb2_hash_from_str8(name);
  U64 slot_idx = hash%ArrayCount(rd_state->tweak_slots);
  RD_TweakNode *node = 0;
  for(RD_TweakNode *n = rd_state->tweak_slots[slot_idx]; n != 0; n = n->hash_next)
  {
    if(str8_match(n->name, name, 0))
    {
      node = n;
      break;
    }
  }
  if(node == 0)
  {
    node = push_array(rd_state->arena, RD_TweakNode, 1);
    node->name = push_str8_copy(rd_state->arena, name);
    node->file = push_str8_copy(rd_state->arena, file);
    node->default_value = default_value;
    node->hash_next = rd_state->tweak_slots[slot_idx];
    rd_state->tweak_slots[slot_idx] = node;
    SLLQueuePush_N(rd_state->first_tweak, rd_state->last_tweak, node, order_next);
    rd_state->tweak_schema_gen += 1;
  }
  node->last_use_frame_index = rd_state->frame_index;
  return node;
}

internal RD_TweakNode *
rd_tweak_node_lookup(String8 name)
{
  U64 hash = u64_djb2_hash_from_str8(name);
  U64 slot_idx = hash%ArrayCount(rd_state->tweak_slots);
  RD_TweakNode *node = 0;
  for(RD_TweakNode *n = rd_state->tweak_slots[slot_idx]; n != 0; n = n->hash_next)
  {
    if(str8_match(n->name, name, 0))
    {
      node = n;
      break;
    }
  }
  return node;
}

internal F32
rd_tweak_f32_value(String8 name, F32 default_value, String8 file)
{
  rd_tweak_node_from_name(name, default_value, file);
  F32 result = default_value;
  String8 value = rd_setting_from_name(name);
  if(value.size != 0)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 expr = push_str8f(scratch.arena, "raw((float32)(%S))", value);
    E_Eval eval = e_eval_from_string(expr);
    result = e_value_eval_from_eval(eval).value.f32;
    scratch_end(scratch);
  }
  return result;
}

internal F32
rd_tweak_f32_range_value(String8 name, F32 default_value, F32 range_min, F32 range_max, String8 file)
{
  RD_TweakNode *node = rd_tweak_node_from_name(name, default_value, file);
  if(!node->has_range)
  {
    node->has_range = 1;
    node->range_min = range_min;
    node->range_max = range_max;
    rd_state->tweak_schema_gen += 1;
  }
  return rd_tweak_f32_value(name, default_value, file);
}

internal B32
rd_tweak_write_default_to_source(RD_TweakNode *tweak, F32 value)
{
  // rewrites the default literal at the tweak's call site, making `value` the
  // new compiled default. the unique name string anchors the site (not
  // file:line, so the file may have shifted around it); the current literal
  // must still parse to the running build's default, otherwise the file has
  // diverged from this binary & we refuse rather than clobber.
  Temp scratch = scratch_begin(0, 0);
  B32 good = 0;
  String8 error = {0};

  //- resolve the recorded __FILE__ against the working directory: the unity
  // build compiles from <repo>/build, so recorded paths look like
  // "../src/..."; probe as-is, then the "src/..." tail walking upward, so
  // launching from the repo root, build/, or a subdirectory all resolve
  String8 resolved = {0};
  String8 data = {0};
  {
    String8 tail = tweak->file;
    while(str8_match(str8_prefix(tail, 3), str8_lit("../"), 0))
    {
      tail = str8_skip(tail, 3);
    }
    String8 candidates[6];
    U64 candidate_count = 0;
    candidates[candidate_count] = tweak->file;
    candidate_count += 1;
    String8 up = str8_lit("");
    for(U64 idx = 0; idx < 5; idx += 1)
    {
      candidates[candidate_count] = push_str8f(scratch.arena, "%S%S", up, tail);
      candidate_count += 1;
      up = push_str8f(scratch.arena, "%S../", up);
    }
    for(U64 idx = 0; idx < candidate_count; idx += 1)
    {
      data = data_from_file_path(scratch.arena, candidates[idx]);
      if(data.size != 0)
      {
        resolved = candidates[idx];
        break;
      }
    }
    if(resolved.size == 0)
    {
      error = push_str8f(scratch.arena, "Could not locate source file \"%S\" from the current directory.", tweak->file);
    }
  }

  //- find the unique call site & the extent of its default literal (the
  // second argument: comma -> next comma for the ranged variant, else the
  // closing paren)
  U64 lit_off = 0;
  U64 lit_opl = 0;
  if(error.size == 0)
  {
    String8 markers[] =
    {
      push_str8f(scratch.arena, "rd_tweak_f32(\"%S\"", tweak->name),
      push_str8f(scratch.arena, "rd_tweak_f32_range(\"%S\"", tweak->name),
    };
    U64 match_count = 0;
    U64 match_off = 0;
    U64 match_marker_size = 0;
    for EachElement(marker_idx, markers)
    {
      String8 marker = markers[marker_idx];
      for(U64 off = 0; off < data.size;)
      {
        U64 pos = str8_find_needle(data, off, marker, 0);
        if(pos >= data.size) { break; }
        match_off = pos;
        match_marker_size = marker.size;
        match_count += 1;
        off = pos + marker.size;
      }
    }
    if(match_count != 1)
    {
      error = push_str8f(scratch.arena, "%s call site%s for \"%S\" in %S.",
                         match_count == 0 ? "No" : "Multiple", match_count == 0 ? "" : "s",
                         tweak->name, resolved);
    }
    else
    {
      U64 comma = str8_find_needle(data, match_off + match_marker_size, str8_lit(","), 0);
      U64 close = str8_find_needle(data, match_off + match_marker_size, str8_lit(")"), 0);
      U64 next_comma = (comma < data.size ? str8_find_needle(data, comma+1, str8_lit(","), 0) : data.size);
      if(next_comma < close) { close = next_comma; }
      if(comma >= data.size || close >= data.size || close < comma)
      {
        error = push_str8f(scratch.arena, "Could not parse the call site for \"%S\" in %S.", tweak->name, resolved);
      }
      else
      {
        lit_off = comma + 1;
        for(;lit_off < close && char_is_space(data.str[lit_off]); lit_off += 1) {}
        lit_opl = close;
        for(;lit_opl > lit_off && char_is_space(data.str[lit_opl-1]); lit_opl -= 1) {}
      }
    }
  }

  //- the literal in the file must still match this binary's compiled default
  if(error.size == 0)
  {
    String8 old_literal = str8_substr(data, r1u64(lit_off, lit_opl));
    F32 parsed = (F32)f64_from_str8(old_literal);
    F32 eps = 1e-5f*ClampBot(abs_f32(tweak->default_value), 1.f);
    if(abs_f32(parsed - tweak->default_value) > eps)
    {
      error = push_str8f(scratch.arena,
                         "Default for \"%S\" in %S is \"%S\", but this build compiled %g — the file changed since this build; rebuild first.",
                         tweak->name, resolved, old_literal, tweak->default_value);
    }
  }

  //- splice the new literal in & write back
  if(error.size == 0)
  {
    String8 num = push_str8f(scratch.arena, "%g", value);
    B32 has_point = 0;
    for(U64 idx = 0; idx < num.size; idx += 1)
    {
      if(num.str[idx] == '.' || num.str[idx] == 'e' || num.str[idx] == 'E')
      {
        has_point = 1;
        break;
      }
    }
    String8 new_literal = push_str8f(scratch.arena, "%S%sf", num, has_point ? "" : ".");
    String8List parts = {0};
    str8_list_push(scratch.arena, &parts, str8_prefix(data, lit_off));
    str8_list_push(scratch.arena, &parts, new_literal);
    str8_list_push(scratch.arena, &parts, str8_skip(data, lit_opl));
    if(write_data_list_to_file_path(resolved, parts))
    {
      tweak->default_value = value;
      rd_state->tweak_schema_gen += 1; // dynamic schema's @default must refresh
      good = 1;
    }
    else
    {
      error = push_str8f(scratch.arena, "Could not write %S.", resolved);
    }
  }

  if(error.size != 0)
  {
    log_user_error(error);
  }
  scratch_end(scratch);
  return good;
}

internal CFG_Node *
rd_immediate_cfg_from_key(String8 string)
{
  CFG_Node *transient = cfg_node_child_from_string(cfg_node_root(), str8_lit("transient"));
  CFG_Node *immediate = &cfg_nil_node;
  CFG_Node *cfg = &cfg_nil_node;
  for(CFG_Node *child = transient->first; child != &cfg_nil_node; child = child->next)
  {
    if(str8_match(child->string, str8_lit("immediate"), 0))
    {
      cfg = cfg_node_child_from_string(child, string);
      if(cfg != &cfg_nil_node)
      {
        immediate = child;
        break;
      }
    }
  }
  if(cfg == &cfg_nil_node)
  {
    immediate = cfg_node_new(rd_state->cfg, transient, str8_lit("immediate"));
    cfg = cfg_node_new(rd_state->cfg, immediate, string);
  }
  cfg_node_child_from_string_or_alloc(rd_state->cfg, immediate, str8_lit("hot"));
  return cfg;
}

internal CFG_Node *
rd_immediate_cfg_from_keyf(char *fmt, ...)
{
  Temp scratch = scratch_begin(0, 0);
  va_list args;
  va_start(args, fmt);
  String8 key = push_str8fv(scratch.arena, fmt, args);
  CFG_Node *result = rd_immediate_cfg_from_key(key);
  va_end(args);
  scratch_end(scratch);
  return result;
}

////////////////////////////////
//~ rjf: Evaluation Spaces

//- rjf: cfg <-> eval space

internal CFG_Node *
rd_cfg_from_eval_space(E_Space space)
{
  CFG_Node *cfg = &cfg_nil_node;
  if(space.kind == RD_EvalSpaceKind_MetaCfg)
  {
    CFG_ID id = space.u64s[0];
    cfg = cfg_node_from_id(id);
  }
  return cfg;
}

internal E_Space
rd_eval_space_from_cfg(CFG_Node *cfg)
{
  E_Space space = e_space_make(RD_EvalSpaceKind_MetaCfg);
  space.u64s[0] = cfg->id;
  return space;
}

//- rjf: command name <-> eval space

internal String8
rd_cmd_name_from_eval(E_Eval eval)
{
  String8 result = {0};
  if(eval.space.kind == RD_EvalSpaceKind_MetaCmd)
  {
    result = e_string_from_id(eval.value.u64);
  }
  return result;
}

//- rjf: eval space reads/writes

internal U64
rd_eval_space_gen(E_Space space)
{
  U64 result = 0;
  switch(space.kind)
  {
    default:{}break;
    case RD_EvalSpaceKind_MetaCfg:
    case RD_EvalSpaceKind_MetaQuery:
    {
      result = cfg_change_gen();
    }break;
  }
  return result;
}

internal B32
rd_eval_space_read(E_Space space, void *out, E_SpaceRangeInfo *out_range_info, Rng1U64 range)
{
  Temp scratch = scratch_begin(0, 0);
  B32 result = 0;
  switch(space.kind)
  {
    default:{}break;
    
    //- rjf: meta-config reads
    case RD_EvalSpaceKind_MetaCfg:
    {
      //- rjf: unpack cfg
      CFG_Node *root_cfg = rd_cfg_from_eval_space(space);
      String8 child_key = e_string_from_id(space.u64s[1]);
      CFG_Node *cfg = root_cfg;
      if(child_key.size != 0)
      {
        cfg = cfg_node_child_from_string(root_cfg, child_key);
      }
      
      //- rjf: determine data to read from, depending on child type in schema
      String8 read_data = {0};
      if(child_key.size != 0)
      {
        // rjf: get schemas for the accessed child
        MD_Node *child_schema = &md_nil_node;
        MD_Node *expr_child_schema = &md_nil_node;
        {
          MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, root_cfg->string);
          for(MD_NodePtrNode *n = schemas.first; n != 0 && child_schema == &md_nil_node; n = n->next)
          {
            child_schema = md_child_from_string(n->v, child_key, 0);
            if(child_schema != &md_nil_node)
            {
              expr_child_schema = md_child_from_string(n->v, str8_lit("expression"), 0);
            }
          }
        }
        String8 child_type_name = child_schema->first->string;
        
        // rjf: get value string (or default fallback)
        String8 value_string = cfg->first->string;
        if(value_string.size == 0)
        {
          value_string = md_tag_from_string(child_schema, str8_lit("default"), 0)->first->string;
        }
        
        // rjf: if this is an override child to a parent, fall back on defaults from parents
        if(value_string.size == 0 && !md_node_is_nil(md_tag_from_string(child_schema, str8_lit("override"), 0)))
        {
          for(CFG_Node *parent = root_cfg->parent; parent != &cfg_nil_node; parent = parent->parent)
          {
            CFG_Node *parent_child_w_key = cfg_node_child_from_string(parent, child_key);
            if(parent_child_w_key != &cfg_nil_node)
            {
              value_string = parent_child_w_key->first->string;
              break;
            }
            value_string = rd_default_setting_from_names(parent->string, child_key);
            if(value_string.size != 0)
            {
              break;
            }
          }
        }
        
        // rjf: textual data
        if(str8_match(child_type_name, str8_lit("path"), 0) ||
           str8_match(child_type_name, str8_lit("path_pt"), 0) ||
           str8_match(child_type_name, str8_lit("code_string"), 0) ||
           str8_match(child_type_name, str8_lit("expr_string"), 0) ||
           str8_match(child_type_name, str8_lit("string"), 0))
        {
          read_data = value_string;
        }
        
        // rjf: non-textual data
        else
        {
          E_Key parent_key = {0};
          if(expr_child_schema != &md_nil_node && child_schema != expr_child_schema)
          {
            parent_key = e_key_from_string(cfg_node_child_from_string(root_cfg, expr_child_schema->string)->first->string);
          }
          E_ParentKey(parent_key)
          {
            if(str8_match(child_type_name, str8_lit("bool"), 0))
            {
              B32 value = !!e_value_from_stringf("(bool)(%S)", value_string).u64;
              read_data = push_str8_copy(scratch.arena, str8_struct(&value));
            }
            else if(str8_match(child_type_name, str8_lit("u64"), 0))
            {
              U64 value = e_value_from_stringf("(uint64)(%S)", value_string).u64;
              read_data = push_str8_copy(scratch.arena, str8_struct(&value));
            }
            else if(str8_match(child_type_name, str8_lit("u32"), 0))
            {
              U64 value = e_value_from_stringf("(uint32)(%S)", value_string).u64;
              read_data = push_str8_copy(scratch.arena, str8_struct(&value));
            }
            else if(str8_match(child_type_name, str8_lit("f32"), 0))
            {
              F32 value = e_value_from_stringf("(float32)(%S)", value_string).f32;
              read_data = push_str8_copy(scratch.arena, str8_struct(&value));
            }
          }
        }
      }
      
      // rjf: if no child key? -> just read from this cfg's child string - first 8 bytes -> offset of string (just 8), then string's content
      if(child_key.size == 0)
      {
        read_data = cfg->first->string;
      }
      
      // rjf: perform read
      Rng1U64 legal_range = r1u64(0, read_data.size);
      Rng1U64 read_range = intersect_1u64(range, legal_range);
      if(read_range.min < read_range.max)
      {
        result = 1;
        MemoryCopy(out, read_data.str + read_range.min, dim_1u64(read_range));
      }
    }break;
    
  }
  scratch_end(scratch);
  return result;
}

internal B32
rd_eval_space_write(E_Space space, void *in, Rng1U64 range)
{
  B32 result = 0;
  switch(space.kind)
  {
    default:{}break;
    
    //- rjf: meta-config writes
    case RD_EvalSpaceKind_MetaCfg:
    {
      result = 1;
      
      // rjf: unpack write info
      String8 write_string = str8_cstring_capped(in, (U8 *)in + dim_1u64(range));
      
      // rjf: unpack cfg
      CFG_Node *root_cfg = rd_cfg_from_eval_space(space);
      String8 child_key = e_string_from_id(space.u64s[1]);
      
      // rjf: no child key? -> overwrite child string
      if(child_key.size == 0)
      {
        cfg_node_new_replace(rd_state->cfg, root_cfg, write_string);
      }
      
      // rjf: child key -> look up & edit child
      else
      {
        // rjf: modifying a label? -> poison this identifier in the macro map
        if(str8_match(child_key, str8_lit("label"), 0))
        {
          String8 pre_edit_label = rd_label_from_cfg(root_cfg);
          if(!str8_match(pre_edit_label, write_string, 0))
          {
            E_Expr *expr = e_string2expr_map_lookup(e_ir_ctx->macro_map, pre_edit_label);
            if(expr != &e_expr_nil)
            {
              e_string2expr_map_inc_poison(e_ir_ctx->macro_map, pre_edit_label);
              e_string2expr_map_insert(e_cache->arena, e_ir_ctx->macro_map, write_string, expr);
            }
          }
        }
        
        // rjf: zero-range? delete child
        if(range.min == range.max)
        {
          cfg_node_release(rd_state->cfg, cfg_node_child_from_string(root_cfg, child_key));
        }
        
        // rjf: non-zero-range? create child if needed & write value
        else
        {
          CFG_Node *child_cfg = cfg_node_child_from_string_or_alloc(rd_state->cfg, root_cfg, child_key);
          cfg_node_new_replace(rd_state->cfg, child_cfg, write_string);
        }
      }
    }break;
  }
  return result;
}

//- rjf: asynchronous streamed reads -> hashes from spaces

internal C_Key
rd_key_from_eval_space_range(E_Space space, Rng1U64 range, B32 zero_terminated)
{
  C_Key result = {0};
  switch(space.kind)
  {
    case E_SpaceKind_HashStoreKey:
    {
      C_Root root = {space.u64_0};
      C_ID id = {space.u128};
      result = c_key_make(root, id);
    }break;
    case E_SpaceKind_File:
    {
      U64 file_path_string_id = space.u64_0;
      String8 file_path = e_string_from_id(file_path_string_id);
      result = fs_key_from_path_range(file_path, range, 0);
    }break;
  }
  return result;
}

//- rjf: space -> entire range

internal Rng1U64
rd_whole_range_from_eval_space(E_Space space)
{
  Rng1U64 result = {0};
  switch(space.kind)
  {
    case E_SpaceKind_HashStoreKey:
    {
      C_Root root = {space.u64_0};
      C_ID id = {space.u128};
      C_Key key = c_key_make(root, id);
      U128 hash = c_hash_from_key(key, 0);
      Access *access = access_open();
      {
        String8 data = c_data_from_hash(access, hash);
        result = r1u64(0, data.size);
      }
      access_close(access);
    }break;
    case E_SpaceKind_File:
    {
      U64 file_path_string_id = space.u64_0;
      String8 file_path = e_string_from_id(file_path_string_id);
      FileProperties props = properties_from_file_path(file_path);
      result = r1u64(0, props.size);
    }break;
  }
  return result;
}

////////////////////////////////
//~ rjf: Evaluation View Visualization & Interaction

//- rjf: writing values back to evaluation spaces

internal B32
rd_commit_eval_value_string(E_Eval dst_eval, String8 string)
{
  B32 result = 0;
  if(dst_eval.irtree.mode == E_Mode_Offset)
  {
    Temp scratch = scratch_begin(0, 0);
    
    //- rjf: unpack type of destination
    E_TypeKey type_key = e_type_key_unwrap(dst_eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative);
    E_TypeKind type_kind = e_type_kind_from_key(type_key);
    E_TypeKey direct_type_key = e_type_key_unwrap(dst_eval.irtree.type_key, E_TypeUnwrapFlag_All);
    E_TypeKind direct_type_kind = e_type_kind_from_key(direct_type_key);
    
    //- rjf: determine data we'll write
    B32 got_commit_data = 0;
    String8 commit_data = {0};
    B32 commit_at_ptr_dest = 0;
    if(!e_type_key_match(e_type_key_zero(), type_key))
    {
      //- rjf: meta evaluations? -> always treat string as textual content, as-is,
      // and commit that.
      if(!got_commit_data && dst_eval.space.kind == RD_EvalSpaceKind_MetaCfg)
      {
        got_commit_data = 1;
        commit_data = string;
      }
      
      //- rjf: basic types or enums? treat string as an expression, cast to the
      // destination type, and compute commit data as being the binary representation
      // of the new value.
      if(!got_commit_data &&
         ((E_TypeKind_FirstBasic <= type_kind && type_kind <= E_TypeKind_LastBasic) ||
          type_kind == E_TypeKind_Enum))
      {
        got_commit_data = 1;
        E_Eval src_eval = e_eval_from_stringf("(%S)(%S)", e_type_string_from_key(scratch.arena, type_key), string);
        commit_data = push_str8_copy(scratch.arena, str8_struct(&src_eval.value));
        commit_data.size = Min(commit_data.size, e_type_byte_size_from_key(type_key));
      }
      
      //- rjf: determine if commit string is quoted
      B32 is_quoted = 0;
      if(string.size >= 1 && string.str[0] == '"')
      {
        string = str8_skip(string, 1);
        is_quoted = 1;
      }
      
      //- rjf: pointer or array to characters/integers? -> try to treat
      // new value string as textual data
      if(!got_commit_data &&
         (((is_quoted && type_kind == E_TypeKind_Ptr) || type_kind == E_TypeKind_Array) &&
          (direct_type_kind == E_TypeKind_Char8 ||
           direct_type_kind == E_TypeKind_Char16 ||
           direct_type_kind == E_TypeKind_Char32 ||
           direct_type_kind == E_TypeKind_UChar8 ||
           direct_type_kind == E_TypeKind_UChar16 ||
           direct_type_kind == E_TypeKind_UChar32 ||
           e_type_kind_is_integer(direct_type_kind))))
      {
        got_commit_data = 1;
        if(string.size >= 1 && string.str[string.size-1] == '"')
        {
          string = str8_chop(string, 1);
        }
        if(is_quoted)
        {
          commit_data = raw_from_escaped_str8(scratch.arena, string);
        }
        else
        {
          commit_data = push_str8_copy(scratch.arena, string);
        }
        commit_data.size += 1;
        if(type_kind == E_TypeKind_Ptr)
        {
          commit_at_ptr_dest = 1;
        }
        switch(direct_type_kind)
        {
          default:{}break;
          case E_TypeKind_S16:
          case E_TypeKind_U16:
          case E_TypeKind_Char16:
          case E_TypeKind_UChar16:
          {
            String16 data16 = str16_from_8(scratch.arena, commit_data);
            commit_data = str8((U8 *)data16.str, data16.size*sizeof(U16));
          }break;
          case E_TypeKind_Char32:
          case E_TypeKind_UChar32:
          case E_TypeKind_S32:
          case E_TypeKind_U32:
          {
            String32 data32 = str32_from_8(scratch.arena, commit_data);
            commit_data = str8((U8 *)data32.str, data32.size*sizeof(U32));
          }break;
        }
      }
      
      //- rjf: pointer? -> try to treat new value as numeric value
      if(!got_commit_data && type_kind == E_TypeKind_Ptr)
      {
        E_Eval src_eval = e_eval_from_string(string);
        E_Eval src_eval_value = e_value_eval_from_eval(src_eval);
        E_TypeKind src_eval_value_type_kind = e_type_kind_from_key(src_eval_value.irtree.type_key);
        if((e_type_kind_is_pointer_or_ref(src_eval_value_type_kind) ||
            e_type_kind_is_integer(src_eval_value_type_kind)) &&
           src_eval_value.irtree.mode == E_Mode_Value)
        {
          got_commit_data = 1;
          commit_data = str8_copy(scratch.arena, str8_struct(&src_eval.value));
          commit_data.size = Max(commit_data.size, e_type_byte_size_from_key(src_eval.irtree.type_key));
          commit_data.size = Max(commit_data.size, e_type_byte_size_from_key(type_key));
        }
      }
    }
    
    //- rjf: determine destination offset we'll write the new data to
    U64 dst_offset = dst_eval.value.u64;
    if(got_commit_data && commit_at_ptr_dest)
    {
      E_Eval dst_value_eval = e_value_eval_from_eval(dst_eval);
      dst_offset = dst_value_eval.value.u64;
    }
    
    //- rjf: if we have commit data, then write that data to the destination offset
    if(got_commit_data)
    {
      result = e_space_write(dst_eval.space, commit_data.str, r1u64(dst_offset, dst_offset + commit_data.size));
    }
    
    scratch_end(scratch);
  }
  return result;
}

//- rjf: eval <-> file path

internal String8
rd_file_path_from_eval(Arena *arena, E_Eval eval)
{
  String8 result = {0};
  switch(eval.space.kind)
  {
    default:{}break;
    case E_SpaceKind_File:
    {
      result = push_str8_copy(arena, e_string_from_id(eval.space.u64_0));
    }break;
    case E_SpaceKind_FileSystem:
    {
      result = push_str8_copy(arena, e_string_from_id(eval.value.u64));
    }break;
  }
  return result;
}

internal String8
rd_file_path_from_eval_string(Arena *arena, String8 string)
{
  String8 result = {0};
  {
    Temp scratch = scratch_begin(&arena, 1);
    E_Eval eval = e_eval_from_string(string);
    result = rd_file_path_from_eval(arena, eval);
    scratch_end(scratch);
  }
  return result;
}

internal String8
rd_eval_string_from_file_path(Arena *arena, String8 string)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8 string_escaped = escaped_from_raw_str8(scratch.arena, string);
  String8 result = push_str8f(arena, "file:\"%S\".data", string_escaped);
  scratch_end(scratch);
  return result;
}

//- rjf: eval -> query

internal String8
rd_query_from_eval_string(Arena *arena, String8 string)
{
  String8 result = {0};
  {
    Temp scratch = scratch_begin(&arena, 1);
    E_Expr *expr = e_parse_from_string(string).expr;
    if(expr->kind == E_ExprKind_LeafIdentifier &&
       str8_match(expr->qualifier, str8_lit("query"), 0))
    {
      result = expr->string;
    }
    scratch_end(scratch);
  }
  return result;
}

////////////////////////////////
//~ rjf: View Functions

internal CFG_Node *
rd_view_from_eval(CFG_Node *parent, E_Eval eval)
{
  Temp scratch = scratch_begin(0, 0);
  E_TypeKey type_key = eval.irtree.type_key;
  E_Type *type = e_type_from_key(type_key);
  String8 schema_name = str8_lit("watch");
  B32 type_is_visualizer = 0;
  if(type->kind == E_TypeKind_Lens)
  {
    RD_ViewUIRule *view_ui_rule = rd_view_ui_rule_from_string(type->name);
    if(view_ui_rule != &rd_nil_view_ui_rule)
    {
      schema_name = type->name;
      type_is_visualizer = 1;
    }
  }
  CFG_Node *view = cfg_node_child_from_string_or_alloc(rd_state->cfg, parent, schema_name);
  cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("selected"));
  {
    // rjf: get expression evaluation
    // TODO(rjf): we need to account for UFCS style expressions here...
    E_Eval expr_eval = eval;
    if(eval.expr->kind == E_ExprKind_Call && type_is_visualizer)
    {
      expr_eval = e_eval_from_expr(eval.expr->first->next);
    }
    
    // rjf: get arguments to view
    E_Expr **args = 0;
    U64 args_count = 0;
    if(type->args != 0)
    {
      args = type->args;
      args_count = type->count;
    }
    
    // rjf: reflect expr & arguments in cfg tree
    CFG_Node *expr_root = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("expression"));
    cfg_node_new_replace(rd_state->cfg, expr_root, e_full_expr_string_from_key(scratch.arena, expr_eval.key));
    {
      MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, schema_name);
      U64 unnamed_order_idx = 0;
      for EachIndex(arg_idx, args_count)
      {
        E_Expr *arg = args[arg_idx];
        String8 param_name = {0};
        E_Expr *arg_expr = arg;
        if(arg->kind == E_ExprKind_Define)
        {
          param_name = arg->first->string;
          arg_expr = arg->first->next;
        }
        else if(schemas.last != 0)
        {
          for MD_EachNode(schema_child, schemas.last->v->first)
          {
            MD_Node *order_tag = md_tag_from_string(schema_child, str8_lit("order"), 0);
            if(order_tag != &md_nil_node)
            {
              U64 schema_child_order_idx = 0;
              try_u64_from_str8_c_rules(order_tag->first->string, &schema_child_order_idx);
              if(schema_child_order_idx == unnamed_order_idx)
              {
                param_name = schema_child->string;
                arg_expr = arg;
                break;
              }
            }
          }
          unnamed_order_idx += 1;
        }
        CFG_Node *arg_root = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, param_name);
        cfg_node_new_replace(rd_state->cfg, arg_root, e_string_from_expr(scratch.arena, arg_expr, str8_zero()));
      }
    }
  }
  scratch_end(scratch);
  return view;
}

internal RD_ViewState *
rd_view_state_from_cfg(CFG_Node *cfg)
{
  RD_ViewState *view_state = &rd_nil_view_state;
  CFG_ID id = cfg->id;
  if(id != 0 &&
     id == rd_state->view_state_last_accessed_id &&
     id == rd_state->view_state_last_accessed->cfg_id)
  {
    view_state = rd_state->view_state_last_accessed;
  }
  else
  {
    U64 hash = u64_djb2_hash_from_str8(str8_struct(&id));
    U64 slot_idx = hash%rd_state->view_state_slots_count;
    RD_ViewStateSlot *slot = &rd_state->view_state_slots[slot_idx];
    for(RD_ViewState *v = slot->first; v != 0; v = v->hash_next)
    {
      if(v->cfg_id == id)
      {
        view_state = v;
        break;
      }
    }
  }
  if(view_state == &rd_nil_view_state)
  {
    view_state = rd_state->free_view_state;
    if(view_state)
    {
      SLLStackPop_N(rd_state->free_view_state, hash_next);
    }
    else
    {
      view_state = push_array(rd_state->arena, RD_ViewState, 1);
    }
    MemoryCopyStruct(view_state, &rd_nil_view_state);
    U64 hash = u64_djb2_hash_from_str8(str8_struct(&id));
    U64 slot_idx = hash%rd_state->view_state_slots_count;
    RD_ViewStateSlot *slot = &rd_state->view_state_slots[slot_idx];
    DLLPushBack_NP(slot->first, slot->last, view_state, hash_next, hash_prev);
    view_state->cfg_id = id;
    view_state->arena = arena_alloc();
    view_state->arena_reset_pos = arena_pos(view_state->arena);
    view_state->ev_view = ev_view_alloc();
  }
  if(view_state != &rd_nil_view_state)
  {
    view_state->last_frame_index_touched = rd_state->frame_index;
  }
  rd_state->view_state_last_accessed = view_state;
  rd_state->view_state_last_accessed_id = id;
  return view_state;
}

RD_VIEW_UI_FUNCTION_DEF(null)
{
  (void)eval;
  (void)rect;
}

internal void
rd_view_ui(Rng2F32 rect)
{
  ProfBeginFunction();
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  RD_ViewState *vs = rd_view_state_from_cfg(view);
  String8 view_name = view->string;
  String8 expr_string = rd_expr_from_cfg(view);

  // only views that declare a producer version (terminal: render generation,
  // text: content hash) are safe for shape-only workspace preservation; any
  // other view kind keeps its workspace on full byte hashing
  if(!str8_match(view_name, str8_lit("terminal"), 0) &&
     !str8_match(view_name, str8_lit("terminal_fixture"), 0) &&
     !str8_match(view_name, str8_lit("text"), 0))
  {
    rd_workspace_surface_mark_unversioned_view();
  }
  B32 view_is_floating = 0;
  for(CFG_Node *p = view->parent; p != &cfg_nil_node; p = p->parent)
  {
    if(str8_match(p->string, str8_lit("immediate"), 0))
    {
      view_is_floating = 1;
      break;
    }
  }
  
  //////////////////////////////
  //- rjf: query extension
  //
  CFG_Node *query_root = cfg_node_child_from_string(view, str8_lit("query"));
  CFG_Node *input_root = cfg_node_child_from_string(query_root, str8_lit("input"));
  CFG_Node *cmd_root = cfg_node_child_from_string(query_root, str8_lit("cmd"));
  String8 current_input = input_root->first->string;
  B32 search_row_is_open = (vs->query_is_open);
  F32 search_row_open_t = ui_anim(ui_key_from_stringf(ui_key_zero(), "search_row_open_%p", view),
                                  (F32)!!search_row_is_open,
                                  .initial = (F32)!!search_row_is_open,
                                  .epsilon = 0.01f,
                                  .rate    = rd_state->menu_animation_rate);
  if(search_row_open_t > 0.001f)
  {
    String8 cmd_name = cmd_root->first->string;
    RD_IconKind icon = rd_icon_kind_from_code_name(cmd_name);
    UIShell_AppCmdInfo cmd_info = uishell_app_cmd_info_from_string(cmd_name);
    
    //- rjf: store cfg's string into view's
    vs->query_string_size = Min(sizeof(vs->query_buffer), current_input.size);
    MemoryCopy(vs->query_buffer, current_input.str, vs->query_string_size);
    
    //- rjf: clamp cursor
    if(vs->query_cursor.column == 0)
    {
      vs->query_mark = txt_pt(1, 1);
      vs->query_cursor = txt_pt(1, vs->query_string_size+1);
    }
    
    //- rjf: determine dimensions
    F32 search_row_height_target = ui_top_px_height();
    F32 search_row_height = search_row_open_t*search_row_height_target;
    search_row_height = Min(search_row_height, dim_2f32(rect).y);
    rect.y0 += search_row_height;
    rect.y0 = floor_f32(rect.y0);
    
    //- rjf: build container
    UI_Box *search_row = &ui_nil_box;
    UI_PrefHeight(ui_px(search_row_height, 1.f))
    {
      search_row = ui_build_box_from_stringf(UI_BoxFlag_DrawSideBottom|UI_BoxFlag_DrawDropShadow, "###search");
    }
    
    //- rjf: build contents
    UI_Parent(search_row) UI_WidthFill UI_HeightFill UI_Focus(vs->query_is_open && !vs->contents_are_focused ? UI_FocusKind_On : UI_FocusKind_Off)
      RD_Font(cmd_info.query_flags & UIShell_QueryFlag_CodeInput ? RD_FontSlot_Code : RD_FontSlot_Main)
    {
      if(cmd_name.size != 0)
      {
        ui_spacer(ui_em(0.5f, 1.f));
        UI_TextAlignment(UI_TextAlign_Center)
          UI_Transparency(1-search_row_open_t)
          UI_PrefWidth(ui_em(3.f, 1.f))
          UI_TagF("weak")
          RD_Font(RD_FontSlot_Icons)
          ui_label(rd_icon_kind_text_table[icon == RD_IconKind_Null ? RD_IconKind_Find : icon]);
        UI_Transparency(1-search_row_open_t)
          RD_Font(RD_FontSlot_Main) UI_PrefWidth(ui_text_dim(1, 1))
          ui_label(rd_display_from_code_name(cmd_name));
        ui_spacer(ui_em(0.5f, 1.f));
      }
      UI_Key line_edit_key = {0};
      RD_CellParams params = {0};
      {
        params.flags |= !!(cmd_info.query_flags & UIShell_QueryFlag_CodeInput) * RD_CellFlag_CodeContents;
        params.flags |= RD_CellFlag_Border;
        params.cursor               = &vs->query_cursor;
        params.mark                 = &vs->query_mark;
        params.edit_buffer          = vs->query_buffer;
        params.edit_string_size_out = &vs->query_string_size;
        params.edit_buffer_size     = sizeof(vs->query_buffer);
        params.pre_edit_value       = current_input;
        params.line_edit_key_out    = &line_edit_key;
      }
      UI_Transparency(1-search_row_open_t)
      {
        UI_Signal sig = rd_cellf(&params, "###search");
#if 0
        // TODO(rjf)
        if(ui_is_focus_active())
        {
          rd_set_autocomp_regs(e_eval_nil,
                               .ui_key = line_edit_key,
                               .string = str8(vs->query_buffer, vs->query_string_size), 
                               .cursor = vs->query_cursor);
        }
#endif
        if(ui_pressed(sig))
        {
          vs->query_is_open = 1;
          vs->contents_are_focused = 0;
          uishell_cmd("focus_panel");
        }
      }
    }
    
    //- rjf: commit string to view
    if(input_root == &cfg_nil_node)
    {
      input_root = cfg_node_child_from_string_or_alloc(rd_state->cfg, query_root, str8_lit("input"));
    }
    cfg_node_new_replace(rd_state->cfg, input_root, str8(vs->query_buffer, vs->query_string_size));
  }
  
  //////////////////////////////
  //- rjf: build main view container
  //
  UI_Box *view_container = &ui_nil_box;
  UI_WidthFill UI_HeightFill
  {
    UI_BoxFlags container_flags = 0;
    UI_Key container_key = ui_key_zero();
    if(DEV_draw_view_surfaces || DEV_crt_views)
    {
      container_flags |= UI_BoxFlag_RenderToSurface;
      container_key = ui_key_from_stringf(ui_key_zero(), "###view_surface_%I64u", uishell_regs()->view);
    }
    view_container = ui_build_box_from_key(container_flags, container_key);
    if(DEV_crt_views)
    {
      view_container->surface_effect = str8_lit("crt");
      view_container->surface_effect_params[0] = v4f32(rd_tweak_f32_range("crt_scanline_intensity", 0.4f, 0.f, 1.f),
                                                       rd_tweak_f32_range("crt_curvature", 0.016981f, 0.f, 0.5f),
                                                       rd_tweak_f32_range("crt_vignette", 0.35f, 0.f, 2.f),
                                                       rd_tweak_f32_range("crt_aperture_mask", 0.5f, 0.f, 1.f));
      view_container->surface_effect_params[1] = v4f32(rd_tweak_f32_range("crt_rgb_shift_px", 0.75f, 0.f, 2.f),
                                                       rd_tweak_f32_range("crt_brightness", 1.15f, 0.5f, 1.8f),
                                                       0, 0);
    }
  }
  
  //////////////////////////////
  //- rjf: fill view container
  //
  UI_Parent(view_container)
    UI_FontSize(rd_font_size())
    UI_PrefHeight(ui_px(floor_f32(ui_top_font_size()*rd_setting_f32_from_name(str8_lit("row_height"))), 1.f))
  {
    ////////////////////////////
    //- rjf: special-case view: "getting started"
    //
    if(0){}
    else if(str8_match(view_name, str8_lit("getting_started"), 0))
    {
      Temp scratch = scratch_begin(0, 0);
      ui_set_next_flags(UI_BoxFlag_DefaultFocusNav);
      UI_Focus(UI_FocusKind_On) UI_WidthFill UI_HeightFill UI_NamedColumn(str8_lit("empty_view"))
        UI_Padding(ui_pct(1, 0)) UI_Focus(UI_FocusKind_Null)
      {
        //- rjf: icon & info
        UI_Padding(ui_em(2.f, 1.f)) UI_TagF("weak")
        {
          //- rjf: icon
          {
            F32 icon_dim = ui_top_font_size()*10.f;
            UI_PrefHeight(ui_px(icon_dim, 1.f))
              UI_Row
              UI_Padding(ui_pct(1, 0))
              UI_PrefWidth(ui_px(icon_dim, 1.f))
            {
              R_Handle texture = rd_state->icon_texture;
              Vec2S32 texture_dim = r_size_from_tex2d(texture);
              ui_image(texture, R_Tex2DSampleKind_Linear, r2f32p(0, 0, texture_dim.x, texture_dim.y), v4f32(1, 1, 1, 1), 0, str8_lit(""));
            }
          }
          
          //- rjf: info
          UI_Padding(ui_em(2.f, 1.f))
            UI_WidthFill UI_PrefHeight(ui_em(2.f, 1.f))
            UI_Row
            UI_Padding(ui_pct(1, 0))
            UI_TextAlignment(UI_TextAlign_Center)
            UI_PrefWidth(ui_text_dim(10, 1))
          {
            ui_label(str8_lit(BUILD_TITLE_STRING_LITERAL));
          }
        }
        
        //- rjf: targets state dependent helper
        B32 helper_built = 0;
        
        //- rjf: or text
        if(helper_built)
        {
          UI_TagF("weak")
            UI_PrefHeight(ui_em(2.25f, 1.f))
            UI_Row
            UI_Padding(ui_pct(1, 0))
            UI_TextAlignment(UI_TextAlign_Center)
            UI_WidthFill
            ui_labelf("- or -");
        }
        
        //- rjf: helper text for command lister activation
        UI_TagF("weak")
          UI_PrefHeight(ui_em(2.25f, 1.f)) UI_Row
          UI_PrefWidth(ui_text_dim(10, 1))
          UI_TextAlignment(UI_TextAlign_Center)
          UI_Padding(ui_pct(1, 0))
        {
          ui_labelf("use");
          UI_TextAlignment(UI_TextAlign_Center) rd_cmd_binding_buttons(str8_lit("open_palette"), str8_zero(), 1);
          ui_labelf("to search for commands and options");
        }
      }
      scratch_end(scratch);
    }
    
    ////////////////////////////
    //- rjf: special-case view: pending
    //
    else if(str8_match(view_name, str8_lit("pending"), 0))
    {
      Temp scratch = scratch_begin(0, 0);
      typedef struct State State;
      struct State
      {
        Arena *deferred_cmd_arena;
        UIShell_CmdList deferred_cmds;
      };
      State *state = rd_view_state(State);
      if(state->deferred_cmd_arena == 0)
      {
        state->deferred_cmd_arena = rd_push_view_arena();
      }
      rd_store_view_loading_info(1, 0, 0);
      
      // rjf: any commands sent to this view need to be deferred until loading is complete
      for(UIShell_Cmd *cmd = 0; uishell_next_view_cmd(&cmd);)
      {
        if(str8_match(cmd->name, str8_lit("goto_line"), 0) ||
           str8_match(cmd->name, str8_lit("goto_address"), 0) ||
           str8_match(cmd->name, str8_lit("center_cursor"), 0) ||
           str8_match(cmd->name, str8_lit("contain_cursor"), 0))
        {
          uishell_cmd_list_push_new(state->deferred_cmd_arena, &state->deferred_cmds, cmd->name, cmd->regs);
        }
      }
      
      // rjf: unpack view's target expression & hash
      E_Eval eval = e_eval_from_string(expr_string);
      Rng1U64 range = r1u64(0, 1024);
      C_Key key = rd_key_from_eval_space_range(eval.space, range, 0);
      U128 hash = c_hash_from_key(key, 0);
      
      // rjf: determine if hash's blob is ready, and which viewer to use
      B32 data_is_ready = 0;
      String8 new_view_name = {0};
      {
        Access *access = access_open();
        if(!u128_match(hash, u128_zero()))
        {
          String8 data = c_data_from_hash(access, hash);
          U64 num_utf8_bytes = 0;
          U64 num_unknown_bytes = 0;
          for(U64 idx = 0; idx < data.size && idx < range.max;)
          {
            UnicodeDecode decode = utf8_decode(data.str+idx, data.size-idx);
            if(decode.codepoint != max_U32 && (decode.inc > 1 ||
                                               (10 <= decode.codepoint && decode.codepoint <= 13) ||
                                               (32 <= decode.codepoint && decode.codepoint <= 126)))
            {
              num_utf8_bytes += decode.inc;
              idx += decode.inc;
            }
            else
            {
              num_unknown_bytes += 1;
              idx += 1;
            }
          }
          data_is_ready = 1;
          new_view_name = uishell_file_view_name_from_probe(num_utf8_bytes, num_unknown_bytes);
        }
        access_close(access);
      }
      
      // rjf: if we don't have a viewer, use the app's binary-data fallback.
      if(new_view_name.size == 0)
      {
        new_view_name = uishell_fallback_file_view_name();
      }
      
      // rjf: if data is ready and we have the name of a new visualizer,
      // dispatch deferred commands & change this view's string to be
      // that of the new visualizer.
      if(data_is_ready && new_view_name.size != 0)
      {
        for(UIShell_CmdNode *cmd_node = state->deferred_cmds.first;
            cmd_node != 0;
            cmd_node = cmd_node->next)
        {
          UIShell_Cmd *cmd = &cmd_node->cmd;
          uishell_push_stored_cmd(cmd->name, cmd->regs);
        }
        CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
        cfg_node_equip_string(rd_state->cfg, view, new_view_name);
        RD_ViewState *vs = rd_view_state_from_cfg(view);
        for(RD_ArenaExt *ext = vs->first_arena_ext; ext != 0; ext = ext->next)
        {
          arena_release(ext->arena);
        }
        arena_pop_to(vs->arena, vs->arena_reset_pos);
        vs->user_data = 0;
        vs->first_arena_ext = vs->last_arena_ext = 0;
      }
      
      // rjf: if we don't have a viewer, for whatever reason, then just
      // close the tab.
      if(data_is_ready && new_view_name.size == 0)
      {
        uishell_cmd("close_tab");
      }
      
      scratch_end(scratch);
    }
    
    ////////////////////////////
    //- rjf: special-case view: generic property/list renderer
    //
    else if(str8_match(view_name, str8_lit("watch"), 0))
    {
      uishell_watch_view_ui(rect);
    }
    
    
    ////////////////////////////
    //- rjf: visualizer hook
    //
    else
    {
      Temp scratch = scratch_begin(0, 0);
      RD_ViewUIRule *view_ui_rule = rd_view_ui_rule_from_string(view_name);
      E_Eval expr_eval = e_eval_from_string(expr_string);
      
      // rjf: peek presses, steal focus from query bar. This reads events
      // directly, so respect inert preview ancestors just as UI signals do.
      B32 interaction_ignored = 0;
      for(UI_Box *p = ui_top_parent(); !ui_box_is_nil(p); p = p->parent)
      {
        if(p->flags & UI_BoxFlag_IgnoreInteraction) { interaction_ignored = 1; break; }
      }
      for(UI_Event *evt = 0; !interaction_ignored && ui_next_event(&evt);)
      {
        if(evt->kind == UI_EventKind_Press && contains_2f32(rect, evt->pos))
        {
          vs->contents_are_focused = 1;
          break;
        }
      }
      
      // rjf: 'pull out' button, if floating
      if(view_is_floating)
      {
        UI_Signal pull_out_sig = {0};
        UI_TagF(".") UI_TagF("tab") UI_Rect(r2f32p(floor_f32(ui_top_font_size()*1.5f),
                                                   floor_f32(ui_top_font_size()*1.5f),
                                                   floor_f32(ui_top_font_size()*1.5f + ui_top_font_size()*3.f),
                                                   floor_f32(ui_top_font_size()*1.5f + ui_top_font_size()*3.f)))
          UI_CornerRadius(floor_f32(ui_top_font_size()*1.5f))
          UI_TextAlignment(UI_TextAlign_Center)
          RD_Font(RD_FontSlot_Icons)
          UI_FontSize(floor_f32(ui_top_font_size()*0.9f))
        {
          UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                                  UI_BoxFlag_Floating|
                                                  UI_BoxFlag_DrawText|
                                                  UI_BoxFlag_DrawBorder|
                                                  UI_BoxFlag_DrawBackground|
                                                  UI_BoxFlag_DrawActiveEffects|
                                                  UI_BoxFlag_DrawHotEffects,
                                                  "%S###pull_out",
                                                  rd_icon_kind_text_table[RD_IconKind_Window]);
          pull_out_sig = ui_signal_from_box(box);
        }
        if(ui_dragging(pull_out_sig) && !contains_2f32(pull_out_sig.box->rect, ui_mouse()))
        {
          rd_drag_begin(UIShell_ContextRegSlot_View);
        }
        if(ui_hovering(pull_out_sig)) UI_Tooltip RD_Font(RD_FontSlot_Main)
        {
          ui_state->tooltip_anchor_key = pull_out_sig.box->key;
          ui_labelf("Pull Out As New Tab");
        }
      }
      
      // rjf: build ui via hook
      E_ParentKey(expr_eval.key)
      {
        view_ui_rule->ui(expr_eval, rect);
      }
      
      scratch_end(scratch);
    }
  }
  
  ////////////////////////////
  //- rjf: catchall completion controls
  //
  if(vs->query_is_open) UI_Focus(UI_FocusKind_On)
  {
    if(ui_is_focus_active() && ui_slot_press(UI_EventActionSlot_Cancel))
    {
      vs->query_is_open = 0;
      vs->query_string_size = 0;
    }
    if(ui_is_focus_active() && ui_slot_press(UI_EventActionSlot_Accept))
    {
      String8 cmd_name = rd_view_query_cmd();
      String8 input = rd_view_query_input();
      UIShell_AppCmdInfo cmd_info = uishell_app_cmd_info_from_string(cmd_name);
      UIShell_RegsScope()
      {
        uishell_regs_fill_slot_from_string(uishell_context_reg_slot_from_app_reg_slot(cmd_info.query_slot), str8_zero(), input);
        uishell_cmd("complete_query");
      }
    }
  }
  
  vs->last_frame_index_built = rd_state->frame_index;
  ProfEnd();
}

////////////////////////////////
//~ rjf: View Building API

//- rjf: view info extraction

internal Arena *
rd_view_arena(void)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  RD_ViewState *view_state = rd_view_state_from_cfg(view);
  return view_state->arena;
}

internal UI_ScrollPt2
rd_view_scroll_pos(void)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  RD_ViewState *view_state = rd_view_state_from_cfg(view);
  return view_state->scroll_pos;
}

internal EV_View *
rd_view_eval_view(void)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  RD_ViewState *view_state = rd_view_state_from_cfg(view);
  return view_state->ev_view;
}

internal String8
rd_view_query_cmd(void)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  CFG_Node *query = cfg_node_child_from_string(view, str8_lit("query"));
  CFG_Node *cmd = cfg_node_child_from_string(query, str8_lit("cmd"));
  String8 string = cmd->first->string;
  return string;
}

internal String8
rd_view_query_input(void)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  CFG_Node *query = cfg_node_child_from_string(view, str8_lit("query"));
  CFG_Node *input = cfg_node_child_from_string(query, str8_lit("input"));
  String8 string = input->first->string;
  return string;
}

internal String8
rd_view_setting_from_name(String8 name)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  String8 result = cfg_node_child_from_string(view, name)->first->string;
  if(result.size == 0)
  {
    result = rd_default_setting_from_names(view->string, name);
  }
  return result;
}

internal E_Value
rd_view_setting_value_from_name(String8 name)
{
  String8 expr = rd_view_setting_from_name(name);
  E_Eval eval = e_eval_from_string(expr);
  E_Value result = e_value_eval_from_eval(eval).value;
  return result;
}

internal B32
rd_view_setting_b32_from_name(String8 name)
{
  String8 string = rd_view_setting_from_name(name);
  B32 result = !!e_value_from_stringf("raw((bool)(%S))", string).u64;
  return result;
}

internal U64
rd_view_setting_u64_from_name(String8 name)
{
  String8 string = rd_view_setting_from_name(name);
  U64 result = e_value_from_stringf("raw((uint64)(%S))", string).u64;
  return result;
}

internal F32
rd_view_setting_f32_from_name(String8 name)
{
  String8 string = rd_view_setting_from_name(name);
  F32 result = e_value_from_stringf("raw((float32)(%S))", string).f32;
  return result;
}

internal U64
rd_view_setting_addr_from_name(String8 name)
{
  U64 result = 0;
  String8 string = rd_view_setting_from_name(name);
  E_Eval eval = e_eval_from_string(string);
  E_TypeKey type_key = e_type_key_unwrap(eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative);
  E_TypeKind type_kind = e_type_kind_from_key(type_key);
  if(eval.irtree.mode == E_Mode_Offset &&
     (type_kind == E_TypeKind_Struct ||
      type_kind == E_TypeKind_Union ||
      type_kind == E_TypeKind_Class))
  {
    result = eval.value.u64;
  }
  else
  {
    result = e_value_eval_from_eval(eval).value.u64;
  }
  return result;
}

//- rjf: evaluation & tag (a view's 'call') parameter extraction

internal Rng1U64
rd_space_range_from_eval(E_Eval eval)
{
  Rng1U64 range = e_range_from_eval(eval);
  U64 size_setting = rd_view_setting_value_from_name(str8_lit("size")).u64;
  if(size_setting != 0)
  {
    range.max = range.min + size_setting;
  }
  return range;
}

internal TXT_LangKind
rd_lang_kind_from_eval(E_Eval eval)
{
  TXT_LangKind lang_kind = TXT_LangKind_Null;
  Temp scratch = scratch_begin(0, 0);
  String8 file_path = rd_file_path_from_eval(scratch.arena, eval);
  if(file_path.size != 0)
  {
    lang_kind = txt_lang_kind_from_extension(str8_skip_last_dot(file_path));
  }
  scratch_end(scratch);
  return lang_kind;
}

//- rjf: pushing/attaching view resources

internal void *
rd_view_state_by_size(U64 size)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  RD_ViewState *view_state = rd_view_state_from_cfg(view);
  if(view_state->user_data == 0)
  {
    view_state->user_data = push_array(view_state->arena, U8, size);
  }
  return view_state->user_data;
}

internal Arena *
rd_push_view_arena(void)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  RD_ViewState *view_state = rd_view_state_from_cfg(view);
  RD_ArenaExt *ext = push_array(view_state->arena, RD_ArenaExt, 1);
  ext->arena = arena_alloc();
  SLLQueuePush(view_state->first_arena_ext, view_state->last_arena_ext, ext);
  return ext->arena;
}

//- rjf: storing view-attached state

internal void
rd_store_view_expr_string(String8 string)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  CFG_Node *expr = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("expression"));
  cfg_node_new_replace(rd_state->cfg, expr, string);
}

internal void
rd_store_view_loading_info(B32 is_loading, U64 progress_u64, U64 progress_u64_target)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  RD_ViewState *view_state = rd_view_state_from_cfg(view);
  B32 loading_state_is_new = (is_loading && view_state->loading_t_target != (F32)!!is_loading);
  view_state->loading_t_target = (F32)!!is_loading;
  view_state->loading_progress_v = progress_u64;
  view_state->loading_progress_v_target = progress_u64_target;
  if(loading_state_is_new || view_state->last_frame_index_built+1 < rd_state->frame_index)
  {
    view_state->loading_t = view_state->loading_t_target;
  }
}

internal void
rd_store_view_scroll_pos(UI_ScrollPt2 pos)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  RD_ViewState *view_state = rd_view_state_from_cfg(view);
  view_state->scroll_pos = pos;
}

internal void
rd_store_view_param(String8 key, String8 value)
{
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  CFG_Node *child = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, key);
  cfg_node_new_replace(rd_state->cfg, child, value);
}

internal void
rd_store_view_paramf(String8 key, char *fmt, ...)
{
  Temp scratch = scratch_begin(0, 0);
  va_list args;
  va_start(args, fmt);
  String8 string = push_str8fv(scratch.arena, fmt, args);
  rd_store_view_param(key, string);
  va_end(args);
  scratch_end(scratch);
}

////////////////////////////////
//~ rjf: Window Functions

internal String8
rd_push_window_title(Arena *arena)
{
  CFG_Node *root = cfg_node_root();
  CFG_Node *project = cfg_node_child_from_string(root, str8_lit("project"));
  CFG_Node *name = cfg_node_child_from_string(project, str8_lit("name"));
  String8 project_name = name->first->string;
  if(project_name.size == 0)
  {
    String8 prof_path = rd_state->project_path;
    prof_path = str8_chop_last_dot(prof_path);
    project_name = str8_skip_last_slash(prof_path);
  }
  String8 result = push_str8f(arena, "%S%s%s", project_name, project_name.size != 0 ? " - " : "", BUILD_TITLE " (" BUILD_VERSION_STRING_LITERAL " " BUILD_RELEASE_PHASE_STRING_LITERAL ")");
  return result;
}

internal CFG_Node *
rd_window_from_cfg(CFG_Node *cfg)
{
  CFG_Node *result = &cfg_nil_node;
  for(CFG_Node *c = cfg; c != &cfg_nil_node; c = c->parent)
  {
    if(c->parent->parent == cfg_node_root() && str8_match(c->string, str8_lit("window"), 0))
    {
      result = c;
      break;
    }
  }
  return result;
}

internal RD_WindowState *
rd_window_state_from_cfg__existing(CFG_Node *cfg)
{
  CFG_Node *window_cfg = rd_window_from_cfg(cfg);
  CFG_ID id = window_cfg->id;
  RD_WindowState *ws = &rd_nil_window_state;
  if(id != 0)
  {
    if(id == rd_state->window_state_last_accessed_id &&
       id == rd_state->window_state_last_accessed->cfg_id)
    {
      ws = rd_state->window_state_last_accessed;
    }
    else
    {
      U64 hash = u64_djb2_hash_from_str8(str8_struct(&id));
      U64 slot_idx = hash%rd_state->window_state_slots_count;
      RD_WindowStateSlot *slot = &rd_state->window_state_slots[slot_idx];
      for(RD_WindowState *w = slot->first; w != 0; w = w->hash_next)
      {
        if(w->cfg_id == id)
        {
          ws = w;
          break;
        }
      }
    }
  }
  return ws;
}

////////////////////////////////
//~ rjf: View Surface Effects

// per-backend translations of each effect, generated by `build.sh effects`
#include "effects/generated/effects_embed.h"

internal R_Handle
rd_effect_from_name(String8 name)
{
  // effects are global & few; a tiny lazily-filled table suffices. misses are
  // remembered too, so a bad name doesn't retry compilation every frame.
  local_persist struct {U64 name_hash; R_Handle handle; B32 attempted;} cache[16] = {0};
  U64 name_hash = u64_hash_from_seed_str8(5381, name);
  R_Handle result = {0};
  for(U64 idx = 0; idx < ArrayCount(cache); idx += 1)
  {
    if(cache[idx].attempted && cache[idx].name_hash == name_hash)
    {
      result = cache[idx].handle;
      break;
    }
    if(!cache[idx].attempted)
    {
      for(U64 efx_idx = 0; efx_idx < ArrayCount(efx_embedded_effects); efx_idx += 1)
      {
        if(str8_match(efx_embedded_effects[efx_idx].name, name, 0))
        {
          result = r_effect_alloc(name, &efx_embedded_effects[efx_idx].sources);
          break;
        }
      }
      cache[idx].name_hash = name_hash;
      cache[idx].handle = result;
      cache[idx].attempted = 1;
      break;
    }
  }
  return result;
}

internal RD_SurfaceCacheNode *
rd_window_surface_node_from_key(RD_WindowState *ws, U64 key, Vec2S32 size)
{
  RD_SurfaceCacheNode *node = 0;
  for(RD_SurfaceCacheNode *n = ws->first_surface_cache_node; n != 0; n = n->next)
  {
    if(n->key == key)
    {
      node = n;
      break;
    }
  }
  if(node != 0 && (node->size.x != size.x || node->size.y != size.y))
  {
    r_tex2d_release(node->texture);
    node->texture = r_tex2d_alloc_render_target(size);
    node->size = size;
    node->rendered_hash = 0;
  }
  if(node == 0)
  {
    node = ws->free_surface_cache_node;
    if(node != 0)
    {
      SLLStackPop(ws->free_surface_cache_node);
    }
    else
    {
      node = push_array(ws->arena, RD_SurfaceCacheNode, 1);
    }
    node->key = key;
    node->texture = r_tex2d_alloc_render_target(size);
    node->size = size;
    node->rendered_hash = 0;
    node->last_was_preserved = 0;
    node->retained = 0;
    SLLStackPush(ws->first_surface_cache_node, node);
  }
  node->last_use_frame_index = rd_state->frame_index;
  return node;
}

internal RD_SurfaceCacheNode *
rd_window_surface_node_lookup(RD_WindowState *ws, U64 key)
{
  RD_SurfaceCacheNode *node = 0;
  for(RD_SurfaceCacheNode *n = ws->first_surface_cache_node; n != 0; n = n->next)
  {
    if(n->key == key)
    {
      node = n;
      break;
    }
  }
  return node;
}

internal RD_WorkspaceSurfaceEntry *
rd_workspace_surface_entry_from_box_key(RD_WindowState *ws, U64 box_key)
{
  RD_WorkspaceSurfaceEntry *entry = 0;
  if(box_key != 0)
  {
    for(U64 i = 0; i < ws->workspace_surface_entry_count; i += 1)
    {
      if(ws->workspace_surface_entries[i].box_key == box_key)
      {
        entry = &ws->workspace_surface_entries[i];
        break;
      }
    }
  }
  return entry;
}

internal void
rd_workspace_surface_contribute_version(U64 version)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(cfg_node_from_id(uishell_regs()->window));
  if(ws != &rd_nil_window_state && ws->active_workspace_surface_entry != 0)
  {
    U64 buffer[2] = {ws->active_workspace_surface_entry->content_version_accum, version};
    ws->active_workspace_surface_entry->content_version_accum = u64_hash_from_str8(str8((U8 *)buffer, sizeof(buffer)));
  }
}

internal void
rd_workspace_surface_mark_unversioned_view(void)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(cfg_node_from_id(uishell_regs()->window));
  if(ws != &rd_nil_window_state && ws->active_workspace_surface_entry != 0)
  {
    ws->active_workspace_surface_entry->has_unversioned_views = 1;
  }
}

internal void
rd_workspace_preview_demand_push(RD_WindowState *ws, U64 workspace_id, F32 width_pt)
{
  RD_WorkspacePreviewDemand *demand = 0;
  for(RD_WorkspacePreviewDemand *d = ws->first_workspace_preview_demand; d != 0; d = d->next)
  {
    if(d->workspace_id == workspace_id)
    {
      demand = d;
      break;
    }
  }
  if(demand == 0)
  {
    demand = push_array(ws->arena, RD_WorkspacePreviewDemand, 1);
    demand->workspace_id = workspace_id;
    SLLStackPush(ws->first_workspace_preview_demand, demand);
  }
  if(demand->frame_index != rd_state->frame_index)
  {
    demand->width_pt = 0;
    demand->frame_index = rd_state->frame_index;
  }
  demand->width_pt = Max(demand->width_pt, width_pt);
}

internal B32
rd_workspace_preview_is_demanded(RD_WindowState *ws, U64 workspace_id)
{
  for(RD_WorkspacePreviewDemand *d = ws->first_workspace_preview_demand; d; d = d->next)
  {
    if(d->workspace_id == workspace_id && d->width_pt > 0 && d->frame_index + 2 >= rd_state->frame_index)
    { return 1; }
  }
  return 0;
}

internal F32
rd_workspace_preview_demand_width(RD_WindowState *ws, U64 workspace_id)
{
  // default when nothing registered recently; demands registered during build
  // are read at draw time the same frame, & last frame's demand bridges gaps
  F32 width_pt = 256.f;
  for(RD_WorkspacePreviewDemand *d = ws->first_workspace_preview_demand; d != 0; d = d->next)
  {
    if(d->workspace_id == workspace_id &&
       d->width_pt > 0 &&
       d->frame_index + 2 >= rd_state->frame_index)
    {
      width_pt = d->width_pt;
      break;
    }
  }
  // quantize to avoid realloc churn while e.g. dragging the sidebar boundary
  width_pt = ceil_f32(width_pt/64.f)*64.f;
  width_pt = Clamp(128.f, width_pt, 1024.f);
  return width_pt;
}

internal U64
rd_workspace_preview_surface_key(U64 workspace_id)
{
  // surface cache keys are usually box keys; previews are cached per workspace
  // rather than per box, so derive the key the same way box keys are made, from
  // a namespacing key string
  return ui_key_from_stringf(ui_key_zero(), "workspace_preview_surface_%I64u", workspace_id).u64[0];
}

typedef struct RD_WorkspacePreviewDraw RD_WorkspacePreviewDraw;
struct RD_WorkspacePreviewDraw
{
  RD_SurfaceCacheNode *node;
  B32 keep_aspect;
  Rng2F32 src_uv; // subrect of the surface to show ({0,0,1,1} = whole)
};

internal UI_BOX_CUSTOM_DRAW(rd_workspace_preview_box_draw)
{
  RD_WorkspacePreviewDraw *draw = (RD_WorkspacePreviewDraw *)user_data;
  if(draw != 0 && draw->node != 0 && !r_handle_match(draw->node->texture, r_handle_zero()))
  {
    Rng2F32 dst = pad_2f32(box->rect, -1.f);
    if(draw->keep_aspect && draw->node->size.x > 0 && draw->node->size.y > 0)
    {
      Vec2F32 available = dim_2f32(dst);
      F32 aspect = (F32)draw->node->size.x/(F32)draw->node->size.y;
      Vec2F32 size = v2f32(Min(available.x, available.y*aspect), Min(available.y, available.x/aspect));
      dst.p0 = add_2f32(dst.p0, scale_2f32(sub_2f32(available, size), 0.5f));
      dst.p1 = add_2f32(dst.p0, size);
    }
    DR_Tex2DSampleKindScope(R_Tex2DSampleKind_Linear)
    {
      dr_surface_img_sub(draw->node->texture, dst, draw->src_uv, v4f32(1, 1, 1, 1), 0, 0, 0);
    }
  }
}

////////////////////////////////
//~ rjf: Workspace Cover Flow

typedef struct RD_CoverFlowCard RD_CoverFlowCard;
struct RD_CoverFlowCard
{
  U64 workspace_id;
  R_Handle texture;
  Mat4x4F32 xform;
  Mat4x4F32 refl_xform;
  F32 row_p;
};

typedef struct RD_CoverFlowDrawData RD_CoverFlowDrawData;
struct RD_CoverFlowDrawData
{
  Mat4x4F32 view;
  Mat4x4F32 projection;
  R_Handle card_vertices;
  R_Handle refl_vertices;
  R_Handle quad_indices;
  Vec4F32 tex_remap; // workspace-content subrect of the wrapper surface
  RD_CoverFlowCard *cards;
  U64 *draw_order;   // far-to-near, for deterministic blending
  U64 card_count;
};

// transform a point by a column-major-stored matrix, exactly as the mesh
// vertex shader does (clip = (P*V) * (inst * v))
internal Vec4F32
rd_coverflow_transform(Mat4x4F32 *m, Vec4F32 v)
{
  Vec4F32 result;
  result.x = m->v[0][0]*v.x + m->v[1][0]*v.y + m->v[2][0]*v.z + m->v[3][0]*v.w;
  result.y = m->v[0][1]*v.x + m->v[1][1]*v.y + m->v[2][1]*v.z + m->v[3][1]*v.w;
  result.z = m->v[0][2]*v.x + m->v[1][2]*v.y + m->v[2][2]*v.z + m->v[3][2]*v.w;
  result.w = m->v[0][3]*v.x + m->v[1][3]*v.y + m->v[2][3]*v.z + m->v[3][3]*v.w;
  return result;
}

// lazily-built shared card geometry. MeshVertex layout: pos3, normal3, tex2, col3.
// the card quad is a unit square centered on the origin (y up, texcoord v=0 at the
// top); the reflection quad shares positions but flips v & fades its vertex colors
// toward black, so it reads as a mirror on a dark floor with no shader support
internal void
rd_coverflow_meshes(R_Handle *card_vertices_out, R_Handle *refl_vertices_out, R_Handle *quad_indices_out)
{
  local_persist B32 made = 0;
  local_persist R_Handle card_vertices = {0};
  local_persist R_Handle refl_vertices = {0};
  local_persist R_Handle quad_indices = {0};
  if(!made)
  {
    made = 1;
    F32 card[] =
    {
      // x      y     z    nx ny nz   u  v    r    g    b
      -0.5f,  0.5f, 0.f,  0, 0, 1,   0, 0,  1.f, 1.f, 1.f,
       0.5f,  0.5f, 0.f,  0, 0, 1,   1, 0,  1.f, 1.f, 1.f,
       0.5f, -0.5f, 0.f,  0, 0, 1,   1, 1,  1.f, 1.f, 1.f,
      -0.5f, -0.5f, 0.f,  0, 0, 1,   0, 1,  1.f, 1.f, 1.f,
    };
    F32 refl_top = 0.16f;
    F32 refl_bot = 0.0f;
    F32 refl[] =
    {
      -0.5f,  0.5f, 0.f,  0, 0, 1,   0, 1,  refl_top, refl_top, refl_top,
       0.5f,  0.5f, 0.f,  0, 0, 1,   1, 1,  refl_top, refl_top, refl_top,
       0.5f, -0.5f, 0.f,  0, 0, 1,   1, 0,  refl_bot, refl_bot, refl_bot,
      -0.5f, -0.5f, 0.f,  0, 0, 1,   0, 0,  refl_bot, refl_bot, refl_bot,
    };
    U32 indices[] = {0, 1, 2, 0, 2, 3};
    card_vertices = r_buffer_alloc(R_ResourceKind_Static, sizeof(card), card);
    refl_vertices = r_buffer_alloc(R_ResourceKind_Static, sizeof(refl), refl);
    quad_indices = r_buffer_alloc(R_ResourceKind_Static, sizeof(indices), indices);
  }
  *card_vertices_out = card_vertices;
  *refl_vertices_out = refl_vertices;
  *quad_indices_out = quad_indices;
}

internal UI_BOX_CUSTOM_DRAW(rd_workspace_coverflow_box_draw)
{
  RD_CoverFlowDrawData *data = (RD_CoverFlowDrawData *)user_data;
  R_PassParams_Geo3D *pass = dr_geo3d_begin(box->rect, data->view, data->projection);
  pass->clip = box->rect;
  DR_Tex2DSampleKindScope(R_Tex2DSampleKind_Linear)
  {
    // far-to-near so alpha blending composites correctly: all reflections
    // first (they sit behind the row), then cards, outermost deck inward
    for(U64 order_idx = 0; order_idx < data->card_count; order_idx += 1)
    {
      RD_CoverFlowCard *card = &data->cards[data->draw_order[order_idx]];
      R_Mesh3DInst *inst = dr_mesh(data->refl_vertices, data->quad_indices, R_GeoTopologyKind_Triangles, R_GeoVertexFlag_TexCoord|R_GeoVertexFlag_RGB, card->texture, 1, card->refl_xform);
      inst->albedo_tex_remap = data->tex_remap;
    }
    for(U64 order_idx = 0; order_idx < data->card_count; order_idx += 1)
    {
      RD_CoverFlowCard *card = &data->cards[data->draw_order[order_idx]];
      R_Mesh3DInst *inst = dr_mesh(data->card_vertices, data->quad_indices, R_GeoTopologyKind_Triangles, R_GeoVertexFlag_TexCoord|R_GeoVertexFlag_RGB, card->texture, 1, card->xform);
      inst->albedo_tex_remap = data->tex_remap;
    }
  }
}

internal void
rd_window_surface_cache_evict(RD_WindowState *ws)
{
  for(RD_SurfaceCacheNode *n = ws->first_surface_cache_node, *next = 0, **prev_next = &ws->first_surface_cache_node;
      n != 0;
      n = next)
  {
    next = n->next;
    if(!n->retained && n->last_use_frame_index < rd_state->frame_index)
    {
      r_tex2d_release(n->texture);
      *prev_next = n->next;
      SLLStackPush(ws->free_surface_cache_node, n);
    }
    else
    {
      prev_next = &n->next;
    }
  }
}

internal RD_WindowState *
rd_window_state_from_cfg(CFG_Node *cfg)
{
  //- rjf: unpack
  CFG_Node *window_cfg = rd_window_from_cfg(cfg);
  CFG_ID id = window_cfg->id;
  
  //- rjf: scan for existing window
  RD_WindowState *ws = rd_window_state_from_cfg__existing(cfg);
  
  //- rjf: allocate/open new window if one was not found
  if(window_cfg != &cfg_nil_node && ws == &rd_nil_window_state)
  {
    Temp scratch = scratch_begin(0, 0);
    
    // rjf: unpack configuration options
    B32 has_pos = 0;
    Vec2F32 pos = {0};
    Vec2F32 size = {0};
    WM_Monitor preferred_monitor = {0};
    {
      CFG_Node *pos_cfg = cfg_node_child_from_string(window_cfg, str8_lit("pos"));
      has_pos = (pos_cfg != &cfg_nil_node);
      CFG_Node *size_cfg = cfg_node_child_from_string(window_cfg, str8_lit("size"));
      CFG_Node *monitor_cfg = cfg_node_child_from_string(window_cfg, str8_lit("monitor"));
      pos.x = (F32)f64_from_str8(pos_cfg->first->string);
      pos.y = (F32)f64_from_str8(pos_cfg->first->next->string);
      size.x = (F32)f64_from_str8(size_cfg->first->string);
      size.y = (F32)f64_from_str8(size_cfg->first->next->string);
      WM_MonitorArray monitors = wm_push_monitors_array(scratch.arena);
      for EachIndex(idx, monitors.count)
      {
        String8 monitor_name = wm_name_from_monitor(scratch.arena, monitors.v[idx]);
        if(str8_match(monitor_name, monitor_cfg->first->string, StringMatchFlag_CaseInsensitive))
        {
          preferred_monitor = monitors.v[idx];
          break;
        }
      }
    }
    
    // rjf: allocate window
    ws = rd_state->free_window_state;
    if(ws != 0)
    {
      SLLStackPop_N(rd_state->free_window_state, order_next);
    }
    else
    {
      ws = push_array_no_zero(rd_state->arena, RD_WindowState, 1);
    }
    MemoryZeroStruct(ws);
    
    // rjf: fill out window
    ws->cfg_id = id;
    ws->arena = arena_alloc();
    ws->root_controlled_split_initialized = 1;
    ws->root_controlled_split_selected_workspace_id = id;
    {
      String8 title = rd_push_window_title(scratch.arena);
      ws->os = wm_window_open(r2f32p(pos.x, pos.y, pos.x+size.x, pos.y+size.y), (!has_pos*WM_WindowFlag_UseDefaultPosition)|WM_WindowFlag_CustomBorder, title);
    }
    ws->r = r_window_equip(ws->os);
    ws->ui = ui_state_alloc();
    ws->drop_completion_arena = arena_alloc();
    ws->query_arena = arena_alloc();
    ws->hover_eval_arena = arena_alloc();
    ws->autocomp_arena = arena_alloc();
    ws->last_layout_scale = wm_layout_scale_from_window(ws->os);
    ws->last_backing_scale = wm_backing_scale_from_window(ws->os);
    WM_Monitor zero_monitor = {0};
    if(!wm_monitor_match(zero_monitor, preferred_monitor))
    {
      wm_window_set_monitor(ws->os, preferred_monitor);
    }
    if(cfg_node_child_from_string(window_cfg, str8_lit("fullscreen")) != &cfg_nil_node)
    {
      wm_window_set_fullscreen(ws->os, 1);
    }
    if(cfg_node_child_from_string(window_cfg, str8_lit("maximized")) != &cfg_nil_node)
    {
      wm_window_set_maximized(ws->os, 1);
    }
    
    // rjf: hook up window links
    U64 hash = u64_djb2_hash_from_str8(str8_struct(&id));
    U64 slot_idx = hash%rd_state->window_state_slots_count;
    RD_WindowStateSlot *slot = &rd_state->window_state_slots[slot_idx];
    DLLPushBack_NPZ(&rd_nil_window_state, rd_state->first_window_state, rd_state->last_window_state, ws, order_next, order_prev);
    DLLPushBack_NP(slot->first, slot->last, ws, hash_next, hash_prev);
    
    scratch_end(scratch);
  }
  
  //- rjf: touch window for this frame
  if(ws != &rd_nil_window_state)
  {
    ws->last_frame_index_touched = rd_state->frame_index;
  }
  
  rd_state->window_state_last_accessed_id = ws->cfg_id;
  rd_state->window_state_last_accessed = ws;
  return ws;
}

internal RD_WindowState *
rd_window_state_from_os_handle(WM_Window os)
{
  RD_WindowState *ws = &rd_nil_window_state;
  {
    for(RD_WindowState *w = rd_state->first_window_state;
        w != &rd_nil_window_state;
        w = w->order_next)
    {
      if(wm_window_match(w->os, os))
      {
        ws = w;
        break;
      }
    }
  }
  return ws;
}

internal CFG_Node *
uishell_workspace_cfg_from_cfg(CFG_Node *cfg)
{
  CFG_Node *workspace = &cfg_nil_node;
  for(CFG_Node *c = cfg; c != &cfg_nil_node; c = c->parent)
  {
    CFG_Node *parent = c->parent;
    if(parent != &cfg_nil_node &&
       parent->parent != &cfg_nil_node &&
       parent->parent->parent == cfg_node_root() &&
       str8_match(parent->string, str8_lit("window"), 0) &&
       str8_match(c->string, str8_lit("workspace"), 0))
    {
      workspace = c;
      break;
    }
  }
  return workspace;
}

internal UIShell_WorkspaceMount
uishell_workspace_mount_from_owner_cfg(Arena *arena, CFG_Node *window, CFG_Node *owner)
{
  CFG_Node *panels_root = cfg_node_child_from_string(owner, str8_lit("panels"));
  Axis2 root_split_axis = cfg_node_child_from_string(owner, str8_lit("split_x")) != &cfg_nil_node ? Axis2_X : Axis2_Y;
  CFG_PanelTree panel_tree = cfg_panel_tree_from_panels_cfg(arena, panels_root, root_split_axis);
  CFG_Node *workspace = str8_match(owner->string, str8_lit("workspace"), 0) ? owner : &cfg_nil_node;
  UIShell_WorkspaceMount mount =
  {
    owner,
    window,
    workspace,
    panels_root,
    root_split_axis,
    panel_tree,
  };
  return mount;
}

internal UIShell_WorkspaceMount
uishell_workspace_mount_from_window(Arena *arena, CFG_Node *window)
{
  CFG_NodePtrList workspaces = cfg_node_child_list_from_string(arena, window, str8_lit("workspace"));
  CFG_Node *owner = window;
  if(workspaces.count != 0)
  {
    RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
    CFG_ID selected_workspace_id = ws != &rd_nil_window_state ? ws->root_controlled_split_selected_workspace_id : 0;
    if(selected_workspace_id != window->id)
    {
      owner = workspaces.first->v;
      for(CFG_NodePtrNode *n = workspaces.first; n != 0; n = n->next)
      {
        if(n->v->id == selected_workspace_id)
        {
          owner = n->v;
          break;
        }
      }
    }
  }
  UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(arena, window, owner);
  return mount;
}

internal UIShell_WorkspaceMount
uishell_workspace_mount_from_cfg(Arena *arena, CFG_Node *cfg)
{
  CFG_Node *window = rd_window_from_cfg(cfg);
  CFG_Node *workspace = uishell_workspace_cfg_from_cfg(cfg);
  CFG_Node *owner = workspace != &cfg_nil_node ? workspace : window;
  UIShell_WorkspaceMount mount = uishell_workspace_mount_from_owner_cfg(arena, window, owner);
  return mount;
}

internal UIShell_WorkspaceMount
uishell_workspace_mount_from_current_regs(Arena *arena)
{
  CFG_Node *cfg = cfg_node_from_id(uishell_regs()->view);
  if(cfg == &cfg_nil_node)
  {
    cfg = cfg_node_from_id(uishell_regs()->tab);
  }
  if(cfg == &cfg_nil_node)
  {
    cfg = cfg_node_from_id(uishell_regs()->panel);
  }
  if(cfg == &cfg_nil_node)
  {
    cfg = cfg_node_from_id(uishell_regs()->window);
  }
  if(cfg == &cfg_nil_node)
  {
    cfg = cfg_node_from_id(rd_state->last_focused_window);
  }
  UIShell_WorkspaceMount mount = uishell_workspace_mount_from_cfg(arena, cfg);
  return mount;
}

internal UIShell_ControlledSplit
uishell_root_controlled_split_from_window(Arena *arena, CFG_Node *window)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  CFG_ID selected_workspace_id = window->id;
  CFG_NodePtrList workspace_cfgs = cfg_node_child_list_from_string(arena, window, str8_lit("workspace"));
  if(workspace_cfgs.count != 0)
  {
    selected_workspace_id = workspace_cfgs.first->v->id;
  }
  if(ws != &rd_nil_window_state)
  {
    if(!ws->root_controlled_split_initialized)
    {
      ws->root_controlled_split_initialized = 1;
      ws->root_controlled_split_selected_workspace_id = selected_workspace_id;
    }
    selected_workspace_id = ws->root_controlled_split_selected_workspace_id;
  }
  
  UIShell_MaterializedWorkspaceInventory inventory = {0};
  CFG_Node *legacy_panels_root = cfg_node_child_from_string(window, str8_lit("panels"));
  if(workspace_cfgs.count == 0 || legacy_panels_root != &cfg_nil_node)
  {
    UIShell_MaterializedWorkspace *workspace = push_array(arena, UIShell_MaterializedWorkspace, 1);
    workspace->id = window->id;
    workspace->display_name = rd_label_from_cfg(window);
    if(workspace->display_name.size == 0)
    {
      workspace->display_name = str8_lit("Workspace");
    }
    workspace->mount = uishell_workspace_mount_from_owner_cfg(arena, window, window);
    DLLPushBack(inventory.first, inventory.last, workspace);
    inventory.count += 1;
    if(workspace->id == selected_workspace_id)
    {
      inventory.selected = workspace;
    }
  }
  if(workspace_cfgs.count != 0)
  {
    for(CFG_NodePtrNode *n = workspace_cfgs.first; n != 0; n = n->next)
    {
      CFG_Node *workspace_cfg = n->v;
      UIShell_MaterializedWorkspace *workspace = push_array(arena, UIShell_MaterializedWorkspace, 1);
      workspace->id = workspace_cfg->id;
      workspace->display_name = rd_label_from_cfg(workspace_cfg);
      if(workspace->display_name.size == 0)
      {
        workspace->display_name = push_str8f(arena, "Workspace %I64u", inventory.count+1);
      }
      workspace->mount = uishell_workspace_mount_from_owner_cfg(arena, window, workspace_cfg);
      DLLPushBack(inventory.first, inventory.last, workspace);
      inventory.count += 1;
      if(workspace->id == selected_workspace_id)
      {
        inventory.selected = workspace;
      }
    }
  }
  if(inventory.selected == 0)
  {
    inventory.selected = inventory.first;
    if(ws != &rd_nil_window_state && inventory.selected != 0)
    {
      ws->root_controlled_split_selected_workspace_id = inventory.selected->id;
    }
  }
  
  UIShell_ControlledSplit split =
  {
    window,
    inventory,
  };
  return split;
}

internal B32
uishell_controlled_split_workspace_can_close(UIShell_ControlledSplit *split, UIShell_MaterializedWorkspace *workspace)
{
  B32 result = (split != 0 &&
                workspace != 0 &&
                split->inventory.count > 1 &&
                (workspace->mount.workspace_cfg != &cfg_nil_node ||
                 workspace->mount.owner_cfg == split->owner_cfg));
  return result;
}

internal UIShell_WorkspaceMount *
uishell_controlled_split_selected_mount(UIShell_ControlledSplit *split)
{
  UIShell_WorkspaceMount *mount = 0;
  if(split->inventory.selected != 0)
  {
    mount = &split->inventory.selected->mount;
  }
  return mount;
}

internal F32
uishell_controlled_split_default_control_width_px(UIShell_ControlledSplit *split, Rng2F32 rect)
{
  F32 rect_width = dim_2f32(rect).x;
  F32 min_width = floor_f32(ui_top_font_size()*4.f);
  F32 desired_width = floor_f32(ui_top_font_size()*32.f);
  F32 max_width = floor_f32(rect_width*0.32f);
  F32 width = 0;
  if(rect_width >= min_width*2.f)
  {
    width = Max(min_width, Min(desired_width, max_width));
  }
  return width;
}

internal Rng1F32
uishell_controlled_split_control_width_range_px(UIShell_ControlledSplit *split, Rng2F32 rect)
{
  (void)split;
  F32 rect_width = dim_2f32(rect).x;
  F32 min_width = floor_f32(ui_top_font_size()*4.f);
  F32 max_width = Max(min_width, floor_f32(rect_width*0.32f));
  Rng1F32 result = r1f32(min_width, max_width);
  if(rect_width < min_width*2.f)
  {
    result = r1f32(0, 0);
  }
  return result;
}

internal F32
uishell_controlled_split_control_width_px(UIShell_ControlledSplit *split, Rng2F32 rect)
{
  // collapsed (sidebar-collapse chrome element): the control surface is hidden &
  // the workspace takes the full width. persisted next to control_split_pct.
  B32 collapsed = (cfg_node_child_from_string(split->owner_cfg, str8_lit("control_split_collapsed")) != &cfg_nil_node);
  F32 rect_width = dim_2f32(rect).x;
  Rng1F32 width_range = uishell_controlled_split_control_width_range_px(split, rect);
  F32 width = 0;
  if(width_range.max > width_range.min)
  {
    F32 default_width = uishell_controlled_split_default_control_width_px(split, rect);
    F32 pct = default_width/rect_width;
    CFG_Node *pct_cfg = cfg_node_child_from_string(split->owner_cfg, str8_lit("control_split_pct"));
    if(pct_cfg != &cfg_nil_node && pct_cfg->first != &cfg_nil_node)
    {
      pct = (F32)f64_from_str8(pct_cfg->first->string);
    }
    width = Clamp(width_range.min, rect_width*pct, width_range.max);
  }
  // animate collapse/expand: the open width above is instant (so divider drags
  // don't lag), and a separate 0..1 factor slides the sidebar in/out from the
  // left. downstream the workspace edge (main_workspace_rect.p0.x) follows this,
  // so the title-bar tab strip glides with it rather than snapping.
  F32 collapse_t = ui_anim(ui_key_from_stringf(ui_key_zero(), "control_split_collapse_t_%p", split->owner_cfg),
                           collapsed ? 0.f : 1.f, .initial = collapsed ? 0.f : 1.f, .rate = rd_state->menu_animation_rate);
  return width*collapse_t;
}

internal Rng2F32
uishell_controlled_split_control_rect(UIShell_ControlledSplit *split, Rng2F32 rect)
{
  F32 control_width = uishell_controlled_split_control_width_px(split, rect);
  Rng2F32 result = r2f32p(rect.x0, rect.y0, rect.x0+control_width, rect.y1);
  if(control_width == 0)
  {
    result = r2f32p(rect.x0, rect.y0, rect.x0, rect.y0);
  }
  return result;
}

internal Rng2F32
uishell_controlled_split_workspace_rect(UIShell_ControlledSplit *split, Rng2F32 rect)
{
  F32 control_width = uishell_controlled_split_control_width_px(split, rect);
  Rng2F32 result = rect;
  if(control_width != 0)
  {
    result.x0 += control_width + 1.f;
  }
  return result;
}

internal B32
uishell_controlled_split_boundary_ui(UIShell_ControlledSplit *split, Rng2F32 rect)
{
  B32 is_changing = 0;
  F32 rect_width = dim_2f32(rect).x;
  F32 control_width = uishell_controlled_split_control_width_px(split, rect);
  Rng1F32 width_range = uishell_controlled_split_control_width_range_px(split, rect);
  if(control_width != 0 && rect_width > 0)
  {
    F32 hit_radius = ui_top_font_size()/3.f;
    Rng2F32 boundary_rect = r2f32p(rect.x0+control_width-hit_radius, rect.y0,
                                   rect.x0+control_width+hit_radius, rect.y1);
    UI_Rect(boundary_rect)
    {
      ui_set_next_hover_cursor(WM_Cursor_LeftRight);
      UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable, "###control_split_boundary_%I64u", split->owner_cfg->id);
      UI_Signal sig = ui_signal_from_box(box);
      if(ui_double_clicked(sig))
      {
        ui_kill_action();
        cfg_node_release(rd_state->cfg, cfg_node_child_from_string(split->owner_cfg, str8_lit("control_split_pct")));
        is_changing = 1;
      }
      else if(ui_pressed(sig))
      {
        ui_store_drag_struct(&control_width);
      }
      else if(ui_dragging(sig))
      {
        F32 start_width = *ui_get_drag_struct(F32);
        F32 new_width = Clamp(width_range.min, start_width + ui_drag_delta().x, width_range.max);
        CFG_Node *pct_cfg = cfg_node_child_from_string_or_alloc(rd_state->cfg, split->owner_cfg, str8_lit("control_split_pct"));
        cfg_node_new_replacef(rd_state->cfg, pct_cfg, "%f", new_width/rect_width);
        is_changing = 1;
      }
    }
  }
  return is_changing;
}

#include "uishell/uishell_sidebar.c"

internal void
uishell_control_surface_ui(Rng2F32 rect, UIShell_ControlledSplit *split)
{
  if(rect.x1 > rect.x0 && rect.y1 > rect.y0)
  {
    // The shell owns the control region's frame, with the same thickness and
    // colour as panel frames. Keep scrolling content inside the top/right edges.
    F32 thickness = floor_f32(Clamp(0.f, rd_setting_f32_from_name(str8_lit("panel_border_px")), 4.f));
    thickness = Min(thickness, Min(dim_2f32(rect).x, dim_2f32(rect).y));
    Rng2F32 inner = r2f32p(rect.x0, rect.y0+thickness, rect.x1-thickness, rect.y1);
    if(inner.x1 > inner.x0 && inner.y1 > inner.y0) { uishell_sidebar_ui(inner, split); }
    if(thickness > 0)
    {
      UI_BackgroundColor(ui_color_from_name(str8_lit("border"))) UI_CornerRadius(0)
      {
        UI_Rect(r2f32p(rect.x0, rect.y0, rect.x1, rect.y0+thickness))
        { ui_build_box_from_key(UI_BoxFlag_DrawBackground, ui_key_zero()); }
        UI_Rect(r2f32p(rect.x1-thickness, rect.y0+thickness, rect.x1, rect.y1))
        { ui_build_box_from_key(UI_BoxFlag_DrawBackground, ui_key_zero()); }
      }
    }
  }
}

////////////////////////////////
//~ rjf: Panel Chrome Frame Segments

typedef struct RD_PanelFrameSegment RD_PanelFrameSegment;
struct RD_PanelFrameSegment
{
  RD_PanelFrameSegment *next;
  CFG_PanelNode *panel;
  Axis2 axis;
  Side side;
  F32 p;
  Rng1F32 span;
  Rng2F32 rect;
};

typedef struct RD_PanelFrameSegmentList RD_PanelFrameSegmentList;
struct RD_PanelFrameSegmentList
{
  RD_PanelFrameSegment *first;
  RD_PanelFrameSegment *last;
  U64 count;
};

typedef struct RD_SelectedTabFrameDrawData RD_SelectedTabFrameDrawData;
struct RD_SelectedTabFrameDrawData
{
  Rng2F32 clip_rect;
  Vec4F32 color;
  F32 border_thickness;
  F32 edge_softness;
  Side tab_side;
  F32 body_edge_p;
};

internal UI_BOX_CUSTOM_DRAW(rd_selected_tab_frame_draw)
{
  RD_SelectedTabFrameDrawData *data = (RD_SelectedTabFrameDrawData *)user_data;
  Rng2F32 top_clip = dr_top_clip();
  Rng2F32 clip = data->clip_rect;
  Rng2F32 cap_clip = clip;
  F32 thickness = data->border_thickness;
  if(data->tab_side == Side_Max)
  {
    cap_clip.y0 = Max(cap_clip.y0, data->body_edge_p + thickness);
  }
  else
  {
    cap_clip.y1 = Min(cap_clip.y1, data->body_edge_p - thickness);
  }
  if(top_clip.x1 != 0 || top_clip.y1 != 0)
  {
    // `dr_top_clip` uses a zero rect as the no-active-clip sentinel.
    clip = intersect_2f32(clip, top_clip);
    cap_clip = intersect_2f32(cap_clip, top_clip);
  }
  Rng2F32 draw_rect = pad_2f32(box->rect, 1.f);
  DR_ClipScope(cap_clip)
  {
    F32 rounded_corner_amount = rd_setting_f32_from_name(str8_lit("rounded_corner_amount"));
    R_Rect2DInst *inst = dr_rect(draw_rect, data->color, 0, thickness, data->edge_softness);
    for EachIndex(idx, Corner_COUNT)
    {
      inst->corner_radii[idx] = box->corner_radii[idx]*rounded_corner_amount;
    }
  }
  DR_ClipScope(clip)
  {
    Rng1F32 join_span = r1f32(data->body_edge_p - thickness, data->body_edge_p + thickness);
    dr_rect(r2f32p(box->rect.x0, join_span.min, box->rect.x0 + thickness, join_span.max), data->color, 0, 0, 0);
    dr_rect(r2f32p(box->rect.x1 - thickness, join_span.min, box->rect.x1, join_span.max), data->color, 0, 0, 0);
  }
}

internal Rng2F32
rd_panel_frame_segment_rect(Axis2 axis, Side side, F32 p, Rng1F32 span, F32 thickness)
{
  Rng2F32 rect = {0};
  if(axis == Axis2_X)
  {
    rect.x0 = (side == Side_Min ? p : p - thickness);
    rect.x1 = (side == Side_Min ? p + thickness : p);
    rect.y0 = span.min;
    rect.y1 = span.max;
  }
  else
  {
    rect.x0 = span.min;
    rect.x1 = span.max;
    rect.y0 = (side == Side_Min ? p : p - thickness);
    rect.y1 = (side == Side_Min ? p + thickness : p);
  }
  return rect;
}

internal void
rd_panel_frame_segment_list_push(Arena *arena, RD_PanelFrameSegmentList *list, CFG_PanelNode *panel, Axis2 axis, Side side, F32 p, Rng1F32 span, F32 thickness)
{
  span.min = round_f32(span.min);
  span.max = round_f32(span.max);
  p = round_f32(p);
  if(span.max - span.min >= 0.5f && thickness >= 0.5f)
  {
    RD_PanelFrameSegment *n = push_array(arena, RD_PanelFrameSegment, 1);
    n->panel = panel;
    n->axis = axis;
    n->side = side;
    n->p = p;
    n->span = span;
    n->rect = rd_panel_frame_segment_rect(axis, side, p, span, thickness);
    SLLQueuePush(list->first, list->last, n);
    list->count += 1;
  }
}

internal void
rd_panel_frame_segment_list_push_edge(Arena *arena, RD_PanelFrameSegmentList *list, CFG_PanelNode *panel, Axis2 axis, Side side, F32 p, Rng1F32 span, F32 thickness, Rng2F32 panel_area_rect, F32 edge_tol, B32 omit_workspace_edges)
{
  B32 on_workspace_edge = (abs_f32(p - panel_area_rect.p0.v[axis]) <= edge_tol ||
                           abs_f32(p - panel_area_rect.p1.v[axis]) <= edge_tol);
  if(!omit_workspace_edges || !on_workspace_edge)
  {
    rd_panel_frame_segment_list_push(arena, list, panel, axis, side, p, span, thickness);
  }
}

internal void
rd_panel_frame_segment_list_push_unique(Arena *arena, RD_PanelFrameSegmentList *dst, RD_PanelFrameSegment *src, F32 thickness)
{
  Rng1F32 remaining[16] = {0};
  U64 remaining_count = 1;
  remaining[0] = src->span;
  for(RD_PanelFrameSegment *accepted = dst->first; accepted != 0 && remaining_count != 0; accepted = accepted->next)
  {
    if(accepted->axis == src->axis && abs_f32(accepted->p - src->p) < 0.5f)
    {
      Rng1F32 next_remaining[16] = {0};
      U64 next_remaining_count = 0;
      for(U64 idx = 0; idx < remaining_count; idx += 1)
      {
        Rng1F32 r = remaining[idx];
        F32 overlap_min = Max(r.min, accepted->span.min);
        F32 overlap_max = Min(r.max, accepted->span.max);
        if(overlap_max <= overlap_min)
        {
          if(next_remaining_count < ArrayCount(next_remaining))
          {
            next_remaining[next_remaining_count++] = r;
          }
          else
          {
            Assert(!"too many frame segment fragments");
          }
        }
        else
        {
          if(r.min < overlap_min && next_remaining_count < ArrayCount(next_remaining))
          {
            next_remaining[next_remaining_count++] = r1f32(r.min, overlap_min);
          }
          else if(r.min < overlap_min)
          {
            Assert(!"too many frame segment fragments");
          }
          if(overlap_max < r.max && next_remaining_count < ArrayCount(next_remaining))
          {
            next_remaining[next_remaining_count++] = r1f32(overlap_max, r.max);
          }
          else if(overlap_max < r.max)
          {
            Assert(!"too many frame segment fragments");
          }
        }
      }
      MemoryCopyArray(remaining, next_remaining);
      remaining_count = next_remaining_count;
    }
  }
  for(U64 idx = 0; idx < remaining_count; idx += 1)
  {
    rd_panel_frame_segment_list_push(arena, dst, src->panel, src->axis, src->side, src->p, remaining[idx], thickness);
  }
}

internal void
rd_panel_area_ui(Temp scratch, Rng2F32 content_rect, Rng2F32 window_rect, RD_WindowState *ws, UIShell_WorkspaceMount *mount, B32 window_is_focused, B32 query_is_open, F32 tab_strip_inset_left, F32 tab_strip_inset_right, B32 tabs_in_title_bar)
{
  CFG_PanelTree panel_tree = mount->panel_tree;
  B32 is_preview = ws->active_workspace_surface_entry != 0 && !ws->active_workspace_surface_entry->composite;
  B32 window_layout_reset = ws->window_layout_reset;
  Rng2F32 panel_area_rect = content_rect; // captured before the per-panel `content_rect` shadows it (for tabs-in-title-bar edge detection)

  typedef struct TabTask TabTask;
  struct TabTask
  {
    TabTask *next;
    CFG_Node *tab;
    DR_FStrList fstrs;
    F32 tab_width;
  };

  typedef struct RD_PanelChromePlan RD_PanelChromePlan;
  struct RD_PanelChromePlan
  {
    RD_PanelChromePlan *next;
    CFG_PanelNode *panel;
    Rng2F32 panel_rect;
    Rng2F32 settled_panel_rect;
    Rng2F32 tab_bar_rect;
    Rng2F32 content_rect;
    Rng2F32 selected_tab_rect;
    B32 selected_tab_visible;
    B32 register_title_bar_tab_strip;
    F32 panel_inset_px;
    F32 tab_bar_rheight;
    F32 tab_bar_vheight;
    F32 tab_bar_rv_diff;
    TabTask *first_tab_task;
    TabTask *last_tab_task;
    U64 tab_task_count;
  };
  
    ////////////////////////////
    //- rjf: @window_ui_part panel non-leaf UI (drag boundaries, drag/drop sites)
    //
    B32 is_changing_panel_boundaries = 0;
    ProfScope("non-leaf panel UI")
      for(CFG_PanelNode *panel = panel_tree.root;
          panel != &cfg_nil_panel_node;
          panel = cfg_panel_node_rec__depth_first_pre(panel_tree.root, panel).next)
    {
      //////////////////////////
      //- rjf: continue on leaf panels
      //
      if(panel->first == &cfg_nil_panel_node)
      {
        continue;
      }
      
      //////////////////////////
      //- rjf: grab info
      //
      Axis2 split_axis = panel->split_axis;
      Rng2F32 panel_rect = cfg_target_rect_from_panel_node(content_rect, panel_tree.root, panel);
      
      //////////////////////////
      //- rjf: boundary tab-drag/drop sites
      //
      {
        CFG_Node *drag_view = cfg_node_from_id(rd_state->drag_drop_regs->view);
        if(rd_drag_is_active() && rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_View && drag_view != &cfg_nil_node)
        {
          //- rjf: params
          F32 drop_site_major_dim_px = ceil_f32(ui_top_font_size()*7.f);
          F32 drop_site_minor_dim_px = ceil_f32(ui_top_font_size()*5.f);
          F32 corner_radius = ui_top_font_size()*0.5f;
          F32 padding = ceil_f32(ui_top_font_size()*0.5f);
          
          //- rjf: special case - build Y boundary drop sites on root panel
          //
          // (this does not naturally follow from the below algorithm, since the
          // root level panel only splits on X)
          if(panel == panel_tree.root) UI_CornerRadius(corner_radius)
          {
            Vec2F32 panel_rect_center = center_2f32(panel_rect);
            Axis2 axis = axis2_flip(panel_tree.root->split_axis);
            for EachEnumVal(Side, side)
            {
              UI_Key key = ui_key_from_stringf(ui_key_zero(), "root_extra_split_%i", side);
              Rng2F32 site_rect = panel_rect;
              site_rect.p0.v[axis2_flip(axis)] = panel_rect_center.v[axis2_flip(axis)] - drop_site_major_dim_px/2;
              site_rect.p1.v[axis2_flip(axis)] = panel_rect_center.v[axis2_flip(axis)] + drop_site_major_dim_px/2;
              site_rect.p0.v[axis] = panel_rect.v[side].v[axis] - drop_site_minor_dim_px/2;
              site_rect.p1.v[axis] = panel_rect.v[side].v[axis] + drop_site_minor_dim_px/2;
              
              // rjf: build
              UI_Box *site_box = &ui_nil_box;
              {
                F32 site_open_t = ui_anim(ui_key_from_stringf(key, "open_t"), 1.f, .rate = rd_state->menu_animation_rate);
                UI_Rect(site_rect) UI_Squish(0.1f-0.1f*site_open_t) UI_Transparency(1-site_open_t)
                {
                  site_box = ui_build_box_from_key(UI_BoxFlag_DropSite|UI_BoxFlag_DrawHotEffects, key);
                  ui_signal_from_box(site_box);
                }
                UI_Box *site_box_viz = &ui_nil_box;
                UI_Parent(site_box) UI_WidthFill UI_HeightFill
                  UI_Padding(ui_px(padding, 1.f))
                  UI_Column
                  UI_Padding(ui_px(padding, 1.f))
                  UI_GroupKey(key)
                {
                  ui_set_next_child_layout_axis(axis2_flip(axis));
                  site_box_viz = ui_build_box_from_key(UI_BoxFlag_DrawBackground|
                                                       UI_BoxFlag_DrawBorder|
                                                       UI_BoxFlag_DrawDropShadow|
                                                       UI_BoxFlag_DrawBackgroundBlur|
                                                       UI_BoxFlag_DrawHotEffects, ui_key_zero());
                }
                UI_Parent(site_box_viz) UI_WidthFill UI_HeightFill UI_Padding(ui_px(padding, 1.f))
                {
                  ui_set_next_child_layout_axis(axis);
                  UI_Box *row_or_column = ui_build_box_from_key(0, ui_key_zero()); UI_Parent(row_or_column) UI_Padding(ui_px(padding, 1.f))
                  {
                    ui_build_box_from_key(UI_BoxFlag_DrawBorder, ui_key_zero());
                    ui_spacer(ui_px(padding, 1.f));
                    ui_build_box_from_key(UI_BoxFlag_DrawBorder, ui_key_zero());
                  }
                }
              }
              
              // rjf: viz
              if(ui_key_match(site_box->key, ui_drop_hot_key()))
              {
                Rng2F32 future_split_rect_target = site_rect;
                future_split_rect_target.p0.v[axis] -= drop_site_major_dim_px;
                future_split_rect_target.p1.v[axis] += drop_site_major_dim_px;
                future_split_rect_target.p0.v[axis2_flip(axis)] = panel_rect.p0.v[axis2_flip(axis)];
                future_split_rect_target.p1.v[axis2_flip(axis)] = panel_rect.p1.v[axis2_flip(axis)];
                future_split_rect_target = pad_2f32(future_split_rect_target, -ui_top_font_size()*2.f);
                Vec2F32 future_split_rect_target_center = center_2f32(future_split_rect_target);
                Rng2F32 future_split_rect =
                {
                  ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v0"), future_split_rect_target.x0, .initial = future_split_rect_target_center.x, .rate = rd_state->menu_animation_rate),
                  ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v1"), future_split_rect_target.y0, .initial = future_split_rect_target_center.y, .rate = rd_state->menu_animation_rate),
                  ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v2"), future_split_rect_target.x1, .initial = future_split_rect_target_center.x, .rate = rd_state->menu_animation_rate),
                  ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v3"), future_split_rect_target.y1, .initial = future_split_rect_target_center.y, .rate = rd_state->menu_animation_rate),
                };
                UI_Rect(future_split_rect) UI_TagF("drop_site") UI_CornerRadius(ui_top_font_size()*2.f)
                {
                  ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder, ui_key_zero());
                }
              }
              
              // rjf: drop
              if(ui_key_match(site_box->key, ui_drop_hot_key()) && rd_drag_drop())
              {
                Dir2 dir = (axis == Axis2_Y ? (side == Side_Min ? Dir2_Up : Dir2_Down) :
                            axis == Axis2_X ? (side == Side_Min ? Dir2_Left : Dir2_Right) :
                            Dir2_Invalid);
                if(dir != Dir2_Invalid)
                {
                  CFG_PanelNode *split_panel = panel;
                  uishell_cmd("split_panel",
                         .dst_panel  = split_panel->cfg->id,
                         .panel      = rd_state->drag_drop_regs->panel,
                         .view      = rd_state->drag_drop_regs->view,
                         .dir2       = dir);
                }
              }
            }
          }
          
          //- rjf: iterate all children, build boundary drop sites
          Axis2 split_axis = panel->split_axis;
          UI_CornerRadius(corner_radius) for(CFG_PanelNode *child = panel->first;; child = child->next)
          {
            // rjf: form rect
            Rng2F32 child_rect = cfg_target_rect_from_panel_node_child(panel_rect, panel, child);
            Vec2F32 child_rect_center = center_2f32(child_rect);
            UI_Key key = ui_key_from_stringf(ui_key_zero(), "drop_boundary_%p_%p", panel->cfg, child->cfg);
            Rng2F32 site_rect = r2f32(child_rect_center, child_rect_center);
            site_rect.p0.v[split_axis] = child_rect.p0.v[split_axis] - drop_site_minor_dim_px/2;
            site_rect.p1.v[split_axis] = child_rect.p0.v[split_axis] + drop_site_minor_dim_px/2;
            site_rect.p0.v[axis2_flip(split_axis)] -= drop_site_major_dim_px/2;
            site_rect.p1.v[axis2_flip(split_axis)] += drop_site_major_dim_px/2;
            
            // rjf: build
            UI_Box *site_box = &ui_nil_box;
            {
              F32 site_open_t = ui_anim(ui_key_from_stringf(key, "open_t"), 1.f, .rate = rd_state->menu_animation_rate);
              UI_Rect(site_rect) UI_Squish(0.1f-0.1f*site_open_t) UI_Transparency(1-site_open_t)
              {
                site_box = ui_build_box_from_key(UI_BoxFlag_DropSite|UI_BoxFlag_DrawHotEffects, key);
                ui_signal_from_box(site_box);
              }
              UI_Box *site_box_viz = &ui_nil_box;
              UI_Parent(site_box) UI_WidthFill UI_HeightFill
                UI_Padding(ui_px(padding, 1.f))
                UI_Column
                UI_Padding(ui_px(padding, 1.f))
                UI_GroupKey(key)
              {
                ui_set_next_child_layout_axis(axis2_flip(split_axis));
                site_box_viz = ui_build_box_from_key(UI_BoxFlag_DrawBackground|
                                                     UI_BoxFlag_DrawBorder|
                                                     UI_BoxFlag_DrawDropShadow|
                                                     UI_BoxFlag_DrawBackgroundBlur|
                                                     UI_BoxFlag_DrawHotEffects, ui_key_zero());
              }
              UI_Parent(site_box_viz) UI_WidthFill UI_HeightFill UI_Padding(ui_px(padding, 1.f))
              {
                ui_set_next_child_layout_axis(split_axis);
                UI_Box *row_or_column = ui_build_box_from_key(0, ui_key_zero()); UI_Parent(row_or_column) UI_Padding(ui_px(padding, 1.f))
                {
                  ui_build_box_from_key(UI_BoxFlag_DrawBorder, ui_key_zero());
                  ui_spacer(ui_px(padding, 1.f));
                  ui_build_box_from_key(UI_BoxFlag_DrawBorder, ui_key_zero());
                }
              }
            }
            
            // rjf: viz
            if(ui_key_match(site_box->key, ui_drop_hot_key()))
            {
              Rng2F32 future_split_rect_target = site_rect;
              future_split_rect_target.p0.v[split_axis] -= drop_site_major_dim_px;
              future_split_rect_target.p1.v[split_axis] += drop_site_major_dim_px;
              future_split_rect_target.p0.v[axis2_flip(split_axis)] = child_rect.p0.v[axis2_flip(split_axis)];
              future_split_rect_target.p1.v[axis2_flip(split_axis)] = child_rect.p1.v[axis2_flip(split_axis)];
              future_split_rect_target = pad_2f32(future_split_rect_target, -ui_top_font_size()*2.f);
              Vec2F32 future_split_rect_target_center = center_2f32(future_split_rect_target);
              Rng2F32 future_split_rect =
              {
                ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v0"), future_split_rect_target.x0, .initial = future_split_rect_target_center.x, .rate = rd_state->menu_animation_rate),
                ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v1"), future_split_rect_target.y0, .initial = future_split_rect_target_center.y, .rate = rd_state->menu_animation_rate),
                ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v2"), future_split_rect_target.x1, .initial = future_split_rect_target_center.x, .rate = rd_state->menu_animation_rate),
                ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v3"), future_split_rect_target.y1, .initial = future_split_rect_target_center.y, .rate = rd_state->menu_animation_rate),
              };
              UI_Rect(future_split_rect) UI_TagF("drop_site") UI_CornerRadius(ui_top_font_size()*2.f)
              {
                ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder, ui_key_zero());
              }
            }
            
            // rjf: drop
            if(ui_key_match(site_box->key, ui_drop_hot_key()) && rd_drag_drop())
            {
              Dir2 dir = (panel->split_axis == Axis2_X ? Dir2_Left : Dir2_Up);
              CFG_PanelNode *split_panel = child;
              if(split_panel == &cfg_nil_panel_node)
              {
                split_panel = panel->last;
                dir = (panel->split_axis == Axis2_X ? Dir2_Right : Dir2_Down);
              }
              uishell_cmd("split_panel",
                     .dst_panel  = split_panel->cfg->id,
                     .panel      = rd_state->drag_drop_regs->panel,
                     .view      = rd_state->drag_drop_regs->view,
                     .dir2       = dir);
            }
            
            // rjf: exit on opl child
            if(child == &cfg_nil_panel_node)
            {
              break;
            }
          }
        }
      }
      
      //////////////////////////
      //- rjf: do UI for drag boundaries between all children
      //
      for(CFG_PanelNode *child = panel->first;
          child != &cfg_nil_panel_node && child->next != &cfg_nil_panel_node;
          child = child->next)
      {
        CFG_PanelNode *min_child = child;
        CFG_PanelNode *max_child = min_child->next;
        Rng2F32 min_child_rect = cfg_target_rect_from_panel_node_child(panel_rect, panel, min_child);
        Rng2F32 max_child_rect = cfg_target_rect_from_panel_node_child(panel_rect, panel, max_child);
        Rng2F32 boundary_rect = {0};
        {
          boundary_rect.p0.v[split_axis] = min_child_rect.p1.v[split_axis] - ui_top_font_size()/3;
          boundary_rect.p1.v[split_axis] = max_child_rect.p0.v[split_axis] + ui_top_font_size()/3;
          boundary_rect.p0.v[axis2_flip(split_axis)] = panel_rect.p0.v[axis2_flip(split_axis)];
          boundary_rect.p1.v[axis2_flip(split_axis)] = panel_rect.p1.v[axis2_flip(split_axis)];
        }
        
        UI_Rect(boundary_rect)
        {
          ui_set_next_hover_cursor(split_axis == Axis2_X ? WM_Cursor_LeftRight : WM_Cursor_UpDown);
          UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable, "###%p_%p", min_child->cfg, max_child->cfg);
          UI_Signal sig = ui_signal_from_box(box);
          if(ui_double_clicked(sig))
          {
            ui_kill_action();
            F32 sum_pct = min_child->pct_of_parent + max_child->pct_of_parent;
            min_child->pct_of_parent = 0.5f * sum_pct;
            max_child->pct_of_parent = 0.5f * sum_pct;
            cfg_node_equip_stringf(rd_state->cfg, min_child->cfg, "%f", min_child->pct_of_parent);
            cfg_node_equip_stringf(rd_state->cfg, max_child->cfg, "%f", max_child->pct_of_parent);
          }
          else if(ui_pressed(sig))
          {
            Vec2F32 v = {min_child->pct_of_parent, max_child->pct_of_parent};
            ui_store_drag_struct(&v);
          }
          else if(ui_dragging(sig))
          {
            Vec2F32 v = *ui_get_drag_struct(Vec2F32);
            Vec2F32 mouse_delta      = ui_drag_delta();
            F32 total_size           = dim_2f32(panel_rect).v[split_axis];
            F32 min_pct__before      = v.v[0];
            F32 min_pixels__before   = min_pct__before * total_size;
            F32 min_pixels__after    = min_pixels__before + mouse_delta.v[split_axis];
            if(min_pixels__after < 50.f)
            {
              min_pixels__after = 50.f;
            }
            F32 min_pct__after       = min_pixels__after / total_size;
            F32 pct_delta            = min_pct__after - min_pct__before;
            F32 max_pct__before      = v.v[1];
            F32 max_pct__after       = max_pct__before - pct_delta;
            F32 max_pixels__after    = max_pct__after * total_size;
            if(max_pixels__after < 50.f)
            {
              max_pixels__after = 50.f;
              max_pct__after = max_pixels__after / total_size;
              pct_delta = -(max_pct__after - max_pct__before);
              min_pct__after = min_pct__before + pct_delta;
            }
            min_child->pct_of_parent = min_pct__after;
            max_child->pct_of_parent = max_pct__after;
            cfg_node_equip_stringf(rd_state->cfg, min_child->cfg, "%f", min_pct__after);
            cfg_node_equip_stringf(rd_state->cfg, max_child->cfg, "%f", max_pct__after);
            is_changing_panel_boundaries = 1;
          }
        }
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part animate panels
    //
    {
      B32 window_is_resizing = (ws->last_window_rect.x1 != window_rect.x1 ||
                                ws->last_window_rect.y1 != window_rect.y1);
      Vec2F32 content_rect_dim = dim_2f32(content_rect);
      if(content_rect_dim.x > 0 && content_rect_dim.y > 0)
      {
        for(CFG_PanelNode *panel = panel_tree.root;
            panel != &cfg_nil_panel_node;
            panel = cfg_panel_node_rec__depth_first_pre(panel_tree.root, panel).next)
        {
          Rng2F32 target_rect_px = cfg_target_rect_from_panel_node(content_rect, panel_tree.root, panel);
          Rng2F32 target_rect_pct = r2f32p(target_rect_px.x0/content_rect_dim.x,
                                           target_rect_px.y0/content_rect_dim.y,
                                           target_rect_px.x1/content_rect_dim.x,
                                           target_rect_px.y1/content_rect_dim.y);
          B32 reset = (window_is_resizing || window_layout_reset || ws->frames_alive < 5 || is_changing_panel_boundaries);
          ui_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x0", panel->cfg), target_rect_pct.x0, .initial = target_rect_pct.x0, .reset = reset, .rate = rd_state->menu_animation_rate);
          ui_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y0", panel->cfg), target_rect_pct.y0, .initial = target_rect_pct.y0, .reset = reset, .rate = rd_state->menu_animation_rate);
          ui_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x1", panel->cfg), target_rect_pct.x1, .initial = target_rect_pct.x1, .reset = reset, .rate = rd_state->menu_animation_rate);
          ui_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y1", panel->cfg), target_rect_pct.y1, .initial = target_rect_pct.y1, .reset = reset, .rate = rd_state->menu_animation_rate);
        }
      }
    }

    ////////////////////////////
    //- uishell: plan leaf-panel chrome & resolve frame segments
    //
    RD_PanelChromePlan *first_chrome_plan = 0;
    RD_PanelChromePlan *last_chrome_plan = 0;
    RD_PanelFrameSegmentList raw_frame_segments = {0};
    RD_PanelFrameSegmentList frame_segments = {0};
    F32 panel_border_px = floor_f32(Clamp(0.f, rd_setting_f32_from_name(str8_lit("panel_border_px")), 4.f));
    F32 edge_tol = ui_top_font_size()*0.5f;
    Vec4F32 panel_body_bg     = ui_color_from_name(str8_lit("background"));
    Vec4F32 panel_frame_color = ui_color_from_name(str8_lit("border"));
    F32 panel_inset_px = floor_f32(ui_top_font_size()*Clamp(0.f, rd_setting_f32_from_name(str8_lit("panel_gap")), 1.f)*0.5f);
    F32 tab_gap_px = floor_f32(ui_top_font_size()*Clamp(0.f, rd_setting_f32_from_name(str8_lit("tab_gap")), 1.f));
    F32 selected_tab_edge_softness = 1.f;
    if(content_rect.x1 > content_rect.x0 && content_rect.y1 > content_rect.y0)
    {
      Vec2F32 content_rect_dim = dim_2f32(content_rect);
      for(CFG_PanelNode *panel = panel_tree.root;
          panel != &cfg_nil_panel_node;
          panel = cfg_panel_node_rec__depth_first_pre(panel_tree.root, panel).next)
      {
        if(panel->first != &cfg_nil_panel_node)
        {
          continue;
        }

        RD_PanelChromePlan *plan = push_array(scratch.arena, RD_PanelChromePlan, 1);
        plan->panel = panel;
        SLLQueuePush(first_chrome_plan, last_chrome_plan, plan);

        Rng2F32 target_rect_px = cfg_target_rect_from_panel_node(content_rect, panel_tree.root, panel);
        Rng2F32 target_rect_pct = r2f32p(target_rect_px.x0 / content_rect_dim.x,
                                         target_rect_px.y0 / content_rect_dim.y,
                                         target_rect_px.x1 / content_rect_dim.x,
                                         target_rect_px.y1 / content_rect_dim.y);
        Rng2F32 panel_rect_pct = r2f32p(ui_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x0", panel->cfg), target_rect_pct.x0, .initial = target_rect_pct.x0, .rate = rd_state->menu_animation_rate),
                                        ui_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y0", panel->cfg), target_rect_pct.y0, .initial = target_rect_pct.y0, .rate = rd_state->menu_animation_rate),
                                        ui_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_x1", panel->cfg), target_rect_pct.x1, .initial = target_rect_pct.x1, .rate = rd_state->menu_animation_rate),
                                        ui_anim(ui_key_from_stringf(ui_key_zero(), "panel_%p_y1", panel->cfg), target_rect_pct.y1, .initial = target_rect_pct.y1, .rate = rd_state->menu_animation_rate));
        plan->panel_rect = r2f32p(panel_rect_pct.x0*content_rect_dim.x,
                                  panel_rect_pct.y0*content_rect_dim.y,
                                  panel_rect_pct.x1*content_rect_dim.x,
                                  panel_rect_pct.y1*content_rect_dim.y);
        plan->panel_rect = pad_2f32(plan->panel_rect, -panel_inset_px);
        plan->panel_rect = r2f32p(round_f32(plan->panel_rect.x0), round_f32(plan->panel_rect.y0), round_f32(plan->panel_rect.x1), round_f32(plan->panel_rect.y1));

        plan->settled_panel_rect = r2f32p(target_rect_pct.x0*content_rect_dim.x,
                                          target_rect_pct.y0*content_rect_dim.y,
                                          target_rect_pct.x1*content_rect_dim.x,
                                          target_rect_pct.y1*content_rect_dim.y);
        plan->settled_panel_rect = pad_2f32(plan->settled_panel_rect, -panel_inset_px);
        plan->settled_panel_rect = r2f32p(round_f32(plan->settled_panel_rect.x0), round_f32(plan->settled_panel_rect.y0), round_f32(plan->settled_panel_rect.x1), round_f32(plan->settled_panel_rect.y1));

        plan->panel_inset_px = panel_inset_px;
        plan->tab_bar_rheight = floor_f32(ui_top_font_size()*3.5f);
        plan->tab_bar_vheight = floor_f32(ui_top_font_size()*rd_setting_f32_from_name(str8_lit("tab_height")));
        plan->tab_bar_rv_diff = plan->tab_bar_rheight - plan->tab_bar_vheight;
        plan->tab_bar_rect = r2f32p(plan->panel_rect.x0, plan->panel_rect.y0, plan->panel_rect.x1, plan->panel_rect.y0 + plan->tab_bar_vheight);
        plan->content_rect = r2f32p(plan->panel_rect.x0, plan->panel_rect.y0+plan->tab_bar_vheight, plan->panel_rect.x1, plan->panel_rect.y1);
        if(panel->tab_side == Side_Max)
        {
          plan->tab_bar_rect.y0 = plan->panel_rect.y1 - plan->tab_bar_vheight;
          plan->tab_bar_rect.y1 = plan->panel_rect.y1;
          plan->content_rect.y0 = plan->panel_rect.y0;
          plan->content_rect.y1 = plan->panel_rect.y1 - plan->tab_bar_vheight;
        }
        plan->tab_bar_rect = intersect_2f32(plan->tab_bar_rect, plan->panel_rect);
        plan->content_rect = intersect_2f32(plan->content_rect, plan->panel_rect);

        if(tabs_in_title_bar && panel->tab_side == Side_Min && plan->settled_panel_rect.p0.y <= panel_area_rect.p0.y + edge_tol)
        {
          plan->tab_bar_rect.p0.x = plan->settled_panel_rect.p0.x;
          plan->tab_bar_rect.p1.x = plan->settled_panel_rect.p1.x;
          if(tab_strip_inset_left  > 0 && plan->settled_panel_rect.p0.x <= panel_area_rect.p0.x + edge_tol) { plan->tab_bar_rect.p0.x += tab_strip_inset_left;  }
          if(tab_strip_inset_right > 0 && plan->settled_panel_rect.p1.x >= panel_area_rect.p1.x - edge_tol) { plan->tab_bar_rect.p1.x -= tab_strip_inset_right; }
          plan->tab_bar_rect.p0.x = Min(plan->tab_bar_rect.p0.x, plan->tab_bar_rect.p1.x);
          plan->register_title_bar_tab_strip = 1;
        }

        if(plan->content_rect.x1 > plan->content_rect.x0 && plan->content_rect.y1 > plan->content_rect.y0) UI_TagF("tab")
        {
          B32 reset = (window_layout_reset || ws->frames_alive < 5 || is_changing_panel_boundaries);
          F32 tab_close_width_px = ui_top_font_size()*2.5f;
          F32 max_tab_width_px = ui_top_font_size()*20.f;
          for(CFG_NodePtrNode *n = panel->tabs.first; n != 0; n = n->next)
          {
            CFG_Node *tab = n->v;
            if(rd_cfg_is_project_filtered(tab))
            {
              continue;
            }
            B32 tab_is_selected = (tab == panel->selected_tab);
            // Selected tabs are integrated with the panel body, so title text
            // resolves from body text rather than tab text.
            UI_TagF(tab_is_selected ? "." : "inactive")
            {
              TabTask *t = push_array(scratch.arena, TabTask, 1);
              t->tab = tab;
              t->fstrs = rd_title_fstrs_from_cfg(scratch.arena, tab, 0);
              F32 tab_width_target = dr_dim_from_fstrs(ui_top_tab_size(), &t->fstrs).x + tab_close_width_px + ui_top_font_size()*1.f;
              if(tab_is_selected && panel_tree.focused == panel)
              {
                tab_width_target += tab_close_width_px;
              }
              tab_width_target = Min(max_tab_width_px, tab_width_target);
              t->tab_width = floor_f32(ui_anim(ui_key_from_stringf(ui_key_zero(), "tab_width_%p", tab), tab_width_target, .initial = reset ? tab_width_target : 0, .rate = rd_state->menu_animation_rate));
              SLLQueuePush(plan->first_tab_task, plan->last_tab_task, t);
              plan->tab_task_count += 1;
            }
          }
        }

        if(panel_border_px >= 1.f && plan->tab_task_count != 0)
        {
          UI_Key tab_bar_key = ui_key_from_stringf(ui_key_zero(), "tab_bar_%p", panel->cfg);
          UI_Box *prev_tab_bar_box = ui_box_from_key(tab_bar_key);
          F32 tab_bar_view_off_x = ui_box_is_nil(prev_tab_bar_box) ? 0.f : floor_f32(prev_tab_bar_box->view_off.x);
          F32 tab_x = plan->tab_bar_rect.x0 + tab_gap_px - tab_bar_view_off_x;
          for(TabTask *task = plan->first_tab_task; task != 0; task = task->next)
          {
            F32 tab_x0 = tab_x;
            F32 tab_x1 = tab_x + task->tab_width;
            if(task->tab == panel->selected_tab)
            {
              F32 visible_x0 = Max(tab_x0, plan->tab_bar_rect.x0);
              F32 visible_x1 = Min(tab_x1, plan->tab_bar_rect.x1);
              UI_Key tab_column_key = ui_key_from_stringf(tab_bar_key, "tab_column_%p", task->tab);
              UI_Key tab_box_key = ui_key_from_stringf(tab_column_key, "tab_%p", task->tab);
              UI_Box *prev_tab_box = ui_box_from_key(tab_box_key);
              if(!ui_box_is_nil(prev_tab_box))
              {
                visible_x0 = Max(prev_tab_box->rect.x0, plan->tab_bar_rect.x0);
                visible_x1 = Min(prev_tab_box->rect.x1, plan->tab_bar_rect.x1);
              }
              if(visible_x1 > visible_x0)
              {
                F32 notch_x0 = Max(visible_x0 + panel_border_px, plan->content_rect.x0);
                F32 notch_x1 = Min(visible_x1 - panel_border_px, plan->content_rect.x1);
                if(notch_x1 < notch_x0)
                {
                  F32 notch_center = (notch_x0 + notch_x1)*0.5f;
                  notch_x0 = notch_center;
                  notch_x1 = notch_center;
                }
                if(panel->tab_side == Side_Max)
                {
                  plan->selected_tab_rect = r2f32p(notch_x0, plan->content_rect.y1 - panel_border_px, notch_x1, plan->tab_bar_rect.y1 - 1.f);
                }
                else
                {
                  plan->selected_tab_rect = r2f32p(notch_x0, plan->tab_bar_rect.y0 + 1.f, notch_x1, plan->content_rect.y0 + panel_border_px);
                }
                plan->selected_tab_visible = 1;
              }
              break;
            }
            tab_x = tab_x1 + tab_gap_px;
          }
        }
      }

      for(RD_PanelChromePlan *plan = first_chrome_plan; plan != 0; plan = plan->next)
      {
        CFG_PanelNode *panel = plan->panel;
        Rng2F32 body = plan->content_rect;
        if(panel_border_px < 1.f || body.x1 <= body.x0 || body.y1 <= body.y0)
        {
          continue;
        }

        B32 top_tabbed = (panel->tab_side != Side_Max);
        B32 omit_workspace_edges = (panel_inset_px < 0.5f);
        rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_X, Side_Min, body.x0, r1f32(body.y0, body.y1), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
        rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_X, Side_Max, body.x1, r1f32(body.y0, body.y1), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
        if(top_tabbed)
        {
          rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_Y, Side_Max, body.y1, r1f32(body.x0, body.x1), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
          if(plan->selected_tab_visible)
          {
            rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_Y, Side_Min, body.y0, r1f32(body.x0, plan->selected_tab_rect.x0), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
            rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_Y, Side_Min, body.y0, r1f32(plan->selected_tab_rect.x1, body.x1), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
          }
          else
          {
            rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_Y, Side_Min, body.y0, r1f32(body.x0, body.x1), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
          }
        }
        else
        {
          rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_Y, Side_Min, body.y0, r1f32(body.x0, body.x1), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
          if(plan->selected_tab_visible)
          {
            rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_Y, Side_Max, body.y1, r1f32(body.x0, plan->selected_tab_rect.x0), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
            rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_Y, Side_Max, body.y1, r1f32(plan->selected_tab_rect.x1, body.x1), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
          }
          else
          {
            rd_panel_frame_segment_list_push_edge(scratch.arena, &raw_frame_segments, panel, Axis2_Y, Side_Max, body.y1, r1f32(body.x0, body.x1), panel_border_px, panel_area_rect, edge_tol, omit_workspace_edges);
          }
        }

        }

      for(RD_PanelFrameSegment *seg = raw_frame_segments.first; seg != 0; seg = seg->next)
      {
        rd_panel_frame_segment_list_push_unique(scratch.arena, &frame_segments, seg, panel_border_px);
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part panel leaf UI
    //
    if(content_rect.x1 > content_rect.x0 && content_rect.y1 > content_rect.y0)
    {
      RD_PanelChromePlan *chrome_plan_cursor = first_chrome_plan;
      ProfScope("leaf panel UI")
        for(CFG_PanelNode *panel = panel_tree.root;
            panel != &cfg_nil_panel_node;
            panel = cfg_panel_node_rec__depth_first_pre(panel_tree.root, panel).next)
      {
        if(panel->first != &cfg_nil_panel_node) {continue;}
        B32 panel_is_focused = (window_is_focused &&
                                !ws->menu_bar_focused &&
                                !query_is_open &&
                                !ui_any_ctx_menu_is_open() &&
                                !ws->hover_eval_focused &&
                                panel_tree.focused == panel);
        CFG_Node *selected_tab = panel->selected_tab;
        RD_ViewState *selected_tab_view_state = rd_view_state_from_cfg(selected_tab);
        ProfScope("leaf panel UI work - %.*s: %.*s", str8_varg(selected_tab->string), str8_varg(rd_expr_from_cfg(selected_tab)))
          UI_Focus(panel_is_focused ? UI_FocusKind_Null : UI_FocusKind_Off)
        {
          //////////////////////////
          //- rjf: unpack planned UI rectangles
          //
          RD_PanelChromePlan *chrome_plan = chrome_plan_cursor;
          if(chrome_plan != 0 && chrome_plan->panel == panel)
          {
            chrome_plan_cursor = chrome_plan_cursor->next;
          }
          else
          {
            // If the cursor is non-null but points at another panel, the prepass
            // and build loop no longer share their expected leaf-panel order.
            Assert(chrome_plan == 0 && "chrome plan cursor order mismatch");
            chrome_plan = 0;
            for(RD_PanelChromePlan *p = first_chrome_plan; p != 0; p = p->next)
            {
              if(p->panel == panel)
              {
                chrome_plan = p;
                break;
              }
            }
          }
          if(chrome_plan == 0) { continue; }
          Rng2F32 panel_rect = chrome_plan->panel_rect;
          Rng2F32 tab_bar_rect = chrome_plan->tab_bar_rect;
          Rng2F32 content_rect = chrome_plan->content_rect;
          F32 tab_bar_rheight = chrome_plan->tab_bar_rheight;
          F32 tab_bar_vheight = chrome_plan->tab_bar_vheight;
          F32 tab_bar_rv_diff = chrome_plan->tab_bar_rv_diff;
          B32 register_title_bar_tab_strip = chrome_plan->register_title_bar_tab_strip;
          
          //////////////////////////
          //- rjf: decide to skip this panel (e.g. if it is too small
          //
          B32 build_panel = (content_rect.x1 > content_rect.x0 && content_rect.y1 > content_rect.y0);
          
          //////////////////////////
          //- rjf: build combined split+movetab drag/drop sites
          //
          if(build_panel)
          {
            CFG_Node *view = cfg_node_from_id(rd_state->drag_drop_regs->view);
            if(rd_drag_is_active() && rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_View && view != &cfg_nil_node && contains_2f32(panel_rect, ui_mouse()) && ui_key_match(ui_drop_hot_key(), ui_key_zero()))
            {
              F32 drop_site_dim_px = ceil_f32(ui_top_font_size()*7.f);
              drop_site_dim_px = Min(drop_site_dim_px, dim_2f32(panel_rect).v[panel->split_axis]/4.f);
              drop_site_dim_px = Max(drop_site_dim_px, ceil_f32(ui_top_font_size()*3.f));
              Vec2F32 drop_site_half_dim = v2f32(drop_site_dim_px/2, drop_site_dim_px/2);
              Vec2F32 panel_center = center_2f32(panel_rect);
              F32 corner_radius = ui_top_font_size()*0.5f;
              F32 padding = ceil_f32(ui_top_font_size()*0.5f);
              struct
              {
                UI_Key key;
                Dir2 split_dir;
                Rng2F32 rect;
              }
              sites[] =
              {
                {
                  ui_key_from_stringf(ui_key_zero(), "drop_split_center_%p", panel->cfg),
                  Dir2_Invalid,
                  r2f32(sub_2f32(panel_center, drop_site_half_dim),
                        add_2f32(panel_center, drop_site_half_dim))
                },
                {
                  ui_key_from_stringf(ui_key_zero(), "drop_split_up_%p", panel->cfg),
                  Dir2_Up,
                  r2f32p(panel_center.x-drop_site_half_dim.x,
                         panel_center.y-drop_site_half_dim.y - drop_site_half_dim.y*2,
                         panel_center.x+drop_site_half_dim.x,
                         panel_center.y+drop_site_half_dim.y - drop_site_half_dim.y*2),
                },
                {
                  ui_key_from_stringf(ui_key_zero(), "drop_split_down_%p", panel->cfg),
                  Dir2_Down,
                  r2f32p(panel_center.x-drop_site_half_dim.x,
                         panel_center.y-drop_site_half_dim.y + drop_site_half_dim.y*2,
                         panel_center.x+drop_site_half_dim.x,
                         panel_center.y+drop_site_half_dim.y + drop_site_half_dim.y*2),
                },
                {
                  ui_key_from_stringf(ui_key_zero(), "drop_split_left_%p", panel->cfg),
                  Dir2_Left,
                  r2f32p(panel_center.x-drop_site_half_dim.x - drop_site_half_dim.x*2,
                         panel_center.y-drop_site_half_dim.y,
                         panel_center.x+drop_site_half_dim.x - drop_site_half_dim.x*2,
                         panel_center.y+drop_site_half_dim.y),
                },
                {
                  ui_key_from_stringf(ui_key_zero(), "drop_split_right_%p", panel->cfg),
                  Dir2_Right,
                  r2f32p(panel_center.x-drop_site_half_dim.x + drop_site_half_dim.x*2,
                         panel_center.y-drop_site_half_dim.y,
                         panel_center.x+drop_site_half_dim.x + drop_site_half_dim.x*2,
                         panel_center.y+drop_site_half_dim.y),
                },
              };
              UI_CornerRadius(corner_radius)
                for(U64 idx = 0; idx < ArrayCount(sites); idx += 1)
              {
                UI_Key key = sites[idx].key;
                Dir2 dir = sites[idx].split_dir;
                Rng2F32 rect = sites[idx].rect;
                Axis2 split_axis = axis2_from_dir2(dir);
                Side split_side = side_from_dir2(dir);
                if(dir != Dir2_Invalid && panel->parent != &cfg_nil_panel_node &&
                   split_axis == panel->parent->split_axis)
                {
                  continue;
                }
                UI_Box *site_box = &ui_nil_box;
                {
                  F32 site_open_t = ui_anim(ui_key_from_stringf(key, "open_t"), 1.f, .rate = rd_state->menu_animation_rate);
                  UI_Rect(rect) UI_Squish(0.1f-0.1f*site_open_t) UI_Transparency(1-site_open_t)
                  {
                    site_box = ui_build_box_from_key(UI_BoxFlag_DropSite|UI_BoxFlag_DrawHotEffects, key);
                    ui_signal_from_box(site_box);
                  }
                  UI_Box *site_box_viz = &ui_nil_box;
                  UI_GroupKey(key)
                    UI_Parent(site_box) UI_WidthFill UI_HeightFill
                    UI_Padding(ui_px(padding, 1.f))
                    UI_Column
                    UI_Padding(ui_px(padding, 1.f))
                  {
                    ui_set_next_child_layout_axis(axis2_flip(split_axis));
                    site_box_viz = ui_build_box_from_key(UI_BoxFlag_DrawBackground|
                                                         UI_BoxFlag_DrawBorder|
                                                         UI_BoxFlag_DrawDropShadow|
                                                         UI_BoxFlag_DrawBackgroundBlur|
                                                         UI_BoxFlag_DrawHotEffects, ui_key_zero());
                  }
                  if(dir != Dir2_Invalid)
                  {
                    UI_Parent(site_box_viz) UI_WidthFill UI_HeightFill UI_Padding(ui_px(padding, 1.f))
                    {
                      ui_set_next_child_layout_axis(split_axis);
                      UI_Box *row_or_column = ui_build_box_from_key(0, ui_key_zero());
                      UI_Parent(row_or_column) UI_Padding(ui_px(padding, 1.f)) UI_TagF("drop_site")
                      {
                        if(split_side == Side_Min) { ui_set_next_flags(UI_BoxFlag_DrawBackground); }
                        ui_build_box_from_key(UI_BoxFlag_DrawBorder, ui_key_zero());
                        ui_spacer(ui_px(padding, 1.f));
                        if(split_side == Side_Max) { ui_set_next_flags(UI_BoxFlag_DrawBackground); }
                        ui_build_box_from_key(UI_BoxFlag_DrawBorder, ui_key_zero());
                      }
                    }
                  }
                  else
                  {
                    UI_Parent(site_box_viz) UI_WidthFill UI_HeightFill UI_Padding(ui_px(padding, 1.f))
                    {
                      ui_set_next_child_layout_axis(split_axis);
                      UI_Box *row_or_column = ui_build_box_from_key(0, ui_key_zero());
                      UI_Parent(row_or_column) UI_Padding(ui_px(padding, 1.f)) UI_TagF("drop_site")
                      {
                        ui_build_box_from_key(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground, ui_key_zero());
                      }
                    }
                  }
                }
                if(ui_key_match(site_box->key, ui_drop_hot_key()) && rd_drag_drop())
                {
                  if(dir != Dir2_Invalid)
                  {
                    uishell_cmd("split_panel",
                           .dst_panel = panel->cfg->id,
                           .panel = rd_state->drag_drop_regs->panel,
                           .view = rd_state->drag_drop_regs->view,
                           .dir2 = dir);
                  }
                  else
                  {
                    uishell_cmd("move_view",
                           .dst_panel = panel->cfg->id,
                           .panel = rd_state->drag_drop_regs->panel,
                           .view = rd_state->drag_drop_regs->view,
                           .prev_tab = cfg_node_ptr_list_last(&panel->tabs)->id);
                  }
                }
              }
              for(U64 idx = 0; idx < ArrayCount(sites); idx += 1)
              {
                B32 is_drop_hot = ui_key_match(ui_drop_hot_key(), sites[idx].key);
                if(is_drop_hot)
                {
                  Axis2 split_axis = axis2_from_dir2(sites[idx].split_dir);
                  Side split_side = side_from_dir2(sites[idx].split_dir);
                  Rng2F32 future_split_rect_target = panel_rect;
                  if(sites[idx].split_dir != Dir2_Invalid)
                  {
                    Vec2F32 panel_center = center_2f32(panel_rect);
                    future_split_rect_target.v[side_flip(split_side)].v[split_axis] = panel_center.v[split_axis];
                  }
                  future_split_rect_target = pad_2f32(future_split_rect_target, -ui_top_font_size()*2.f);
                  Vec2F32 future_split_rect_target_center = center_2f32(future_split_rect_target);
                  Rng2F32 future_split_rect =
                  {
                    ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v0"), future_split_rect_target.x0, .initial = future_split_rect_target_center.x, .rate = rd_state->menu_animation_rate),
                    ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v1"), future_split_rect_target.y0, .initial = future_split_rect_target_center.y, .rate = rd_state->menu_animation_rate),
                    ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v2"), future_split_rect_target.x1, .initial = future_split_rect_target_center.x, .rate = rd_state->menu_animation_rate),
                    ui_anim(ui_key_from_stringf(ui_key_zero(), "drop_site_v3"), future_split_rect_target.y1, .initial = future_split_rect_target_center.y, .rate = rd_state->menu_animation_rate),
                  };
                  UI_Rect(future_split_rect) UI_TagF("drop_site") UI_CornerRadius(ui_top_font_size()*2.f)
                  {
                    ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder, ui_key_zero());
                  }
                }
              }
            }
          }
          
          //////////////////////////
          //- rjf: build catch-all panel drop-site
          //
          UI_Key catchall_drop_site_key = ui_key_from_stringf(ui_key_zero(), "catchall_drop_site_%p", panel->cfg);
          if(build_panel && rd_drag_is_active() && rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_View) UI_Rect(panel_rect)
          {
            UI_Box *catchall_drop_site = ui_build_box_from_key(UI_BoxFlag_DropSite, catchall_drop_site_key);
            ui_signal_from_box(catchall_drop_site);
          }
          
          //////////////////////////
          //- rjf: panel not selected? -> darken
          //
          if(build_panel && !is_preview) if(panel != panel_tree.focused)
          {
            // uishell: dim non-Active panels — a scrim fading the whole panel toward
            // the window background, so inactive content loses contrast and recedes
            // (a black multiply would only darken). Built before panel_box so it
            // draws in front (reverse-order sibling painting); non-clickable.
            F32 inactive_dim = Clamp(0.f, rd_setting_f32_from_name(str8_lit("inactive_panel_dim")), 0.8f);
            if(inactive_dim > 0.001f)
            {
              Vec4F32 dim_color = ui_color_from_name(str8_lit("background"));
              dim_color.w = inactive_dim;
              ui_set_next_background_color(dim_color);
              UI_Rect(panel_rect)
                ui_build_box_from_key(UI_BoxFlag_DrawBackground, ui_key_zero());
            }
          }
          
          //////////////////////////
          //- uishell: dock-drawn panel frame (ADR-0007) — resolved from the
          // frame-bearing regions (body + selected tab handle), not from whole
          // panel rectangles. This removes tab-strip|tab-strip dividers while
          // preserving one owned border for content-bearing boundaries.
          // Emitted here, rather than in one global pass, to preserve the current
          // sibling paint order: inactive-panel scrim below, frame segments above,
          // and panel contents clipped by panel_box.
          //
          if(build_panel)
          {
            for(RD_PanelFrameSegment *seg = frame_segments.first; seg != 0; seg = seg->next)
            {
              if(seg->panel == panel)
              {
                ui_set_next_background_color(panel_frame_color);
                UI_Rect(seg->rect) ui_build_box_from_key(UI_BoxFlag_DrawBackground, ui_key_zero());
              }
            }
          }

          //////////////////////////
          //- rjf: build panel container box
          //
          UI_Box *panel_box = &ui_nil_box;
          if(build_panel) UI_Rect(content_rect) UI_ChildLayoutAxis(Axis2_Y) UI_CornerRadius(0) UI_Focus(UI_FocusKind_On)
          {
            UI_Key panel_key = ui_key_from_stringf(ui_key_zero(), "panel_box_%p", panel->cfg);
            // uishell: panel frame is now drawn by the dock as thin edge segments
            // (above), so the box no longer draws its own border; the focus accent
            // is retired in favour of inactive-panel dimming (always DisableFocusBorder).
            panel_box = ui_build_box_from_key(UI_BoxFlag_MouseClickable|
                                              UI_BoxFlag_Clip|
                                              UI_BoxFlag_DisableFocusOverlay|
                                              UI_BoxFlag_DisableFocusBorder|
                                              ((DEV_draw_panel_surface && panel_tree.focused == panel)*UI_BoxFlag_RenderToSurface),
                                              panel_key);
          }
          
          //////////////////////////
          //- rjf: loading animation for stable view
          //
          UI_Box *loading_overlay_container = &ui_nil_box;
          if(build_panel) UI_Parent(panel_box) UI_WidthFill UI_HeightFill
          {
            loading_overlay_container = ui_build_box_from_key(UI_BoxFlag_Floating, ui_key_zero());
          }
          
          //////////////////////////
          //- rjf: build selected tab view
          //
          if(build_panel)
            UI_Parent(panel_box)
            UI_Focus(panel_is_focused ? UI_FocusKind_Null : UI_FocusKind_Off)
            UI_WidthFill
          {
            //- rjf: push interaction registers, fill with per-view states
            uishell_push_regs(.panel = panel->cfg->id,
                         .tab = selected_tab->id,
                         .view = selected_tab->id);
            {
              String8 view_expr = rd_expr_from_cfg(selected_tab);
              String8 view_file_path = rd_file_path_from_eval_string(rd_frame_arena(), view_expr);
              // NOTE(rjf): we want to only fill out this view's file path slot if it
              // evaluates one - this way, a view can use the slot to know the selected
              // file path (if there is one). this is useful when pushing commandas which
              // apply to a cursor, for example.
              if(view_file_path.size != 0)
              {
                uishell_regs()->file_path = view_file_path;
              }
            }
            
            //- rjf: visualizers -> accept expression drops
            UI_Box *view_drop_site = &ui_nil_box;
            {
              RD_ViewUIRule *view_ui_rule = rd_view_ui_rule_from_string(selected_tab->string);
              if(view_ui_rule != &rd_nil_view_ui_rule && rd_drag_is_active() && rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_Expr &&
                 !str8_match(selected_tab->string, str8_lit("text"), 0))
              {
                UI_FixedSize(dim_2f32(content_rect))
                  view_drop_site = ui_build_box_from_stringf(UI_BoxFlag_DropSite|UI_BoxFlag_Floating, "drop_site_%I64x", selected_tab->id);
              }
            }
            
            //- rjf: build view container
            UI_Box *view_container_box = &ui_nil_box;
            UI_FixedWidth(dim_2f32(content_rect).x)
              UI_FixedHeight(dim_2f32(content_rect).y)
              UI_ChildLayoutAxis(Axis2_Y)
            {
              view_container_box = ui_build_box_from_key(0, ui_key_zero());
            }
            
            //- rjf: build empty view
            UI_Parent(view_container_box) if(selected_tab == &cfg_nil_node && panel->parent != &cfg_nil_panel_node)
            {
              ui_set_next_flags(UI_BoxFlag_DefaultFocusNav);
              UI_Focus(UI_FocusKind_On) UI_WidthFill UI_HeightFill UI_NamedColumn(str8_lit("empty_view")) UI_TagF("weak")
                UI_Padding(ui_pct(1, 0)) UI_Focus(UI_FocusKind_Null)
              {
                UI_PrefHeight(ui_em(3.f, 1.f))
                  UI_Row
                  UI_Padding(ui_pct(1, 0))
                  UI_TextAlignment(UI_TextAlign_Center)
                  UI_PrefWidth(ui_em(15.f, 1.f))
                  UI_CornerRadius(ui_top_font_size()/2.f)
                  UI_TagF("bad_pop")
                {
                  if(ui_clicked(rd_icon_buttonf(RD_IconKind_X, 0, "Close Panel")))
                  {
                    uishell_cmd("close_panel");
                  }
                }
              }
            }
            
            //- rjf: build tab view
            UI_Parent(view_container_box) if(selected_tab != &cfg_nil_node) ProfScope("build tab view")
            {
              rd_view_ui(content_rect);
            }
            
            //- rjf: accept expression drops
            if(view_drop_site != &ui_nil_box)
            {
              UI_Signal sig = ui_signal_from_box(view_drop_site);
              if(ui_key_match(view_drop_site->key, ui_drop_hot_key()))
              {
                UI_Parent(view_drop_site) UI_WidthFill UI_HeightFill UI_TagF("drop_site")
                {
                  ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder, ui_key_zero());
                }
                if(rd_drag_drop())
                {
                  rd_store_view_expr_string(rd_state->drag_drop_regs->expr);
                }
              }
            }
            
            //- rjf: pop interaction registers; commit if this is the selected view
            UIShell_Regs *view_regs = uishell_pop_regs();
            if(panel_is_focused)
            {
              MemoryCopyStruct(uishell_regs(), view_regs);
            }
          }
          
          ////////////////////////
          //- rjf: loading? -> fill loading overlay container
          //
          if(build_panel)
          {
            F32 selected_tab_loading_t = selected_tab_view_state->loading_t;
            if(selected_tab_loading_t > 0.01f) UI_Parent(loading_overlay_container)
            {
              rd_loading_overlay(panel_rect, selected_tab_loading_t, selected_tab_view_state->loading_progress_v, selected_tab_view_state->loading_progress_v_target);
            }
          }
          
          //////////////////////////
          //- rjf: consume panel fallthrough interaction events
          //
          if(build_panel)
          {
            UI_Signal panel_sig = ui_signal_from_box(panel_box);
            if(ui_pressed(panel_sig))
            {
              uishell_cmd("focus_panel", .panel = panel->cfg->id);
            }
          }
          
          //////////////////////////
          //- rjf: unpack tab build tasks
          //
          TabTask *first_tab_task = chrome_plan->first_tab_task;
          F32 tab_close_width_px = ui_top_font_size()*2.5f;
          
          //////////////////////////
          //- rjf: build tab bar container
          //
          UI_Box *tab_bar_box = &ui_nil_box;
          UI_Box *tab_strip_add_button_box = &ui_nil_box;
          if(build_panel) UI_CornerRadius(0) UI_Rect(tab_bar_rect)
          {
            tab_bar_box = ui_build_box_from_stringf(UI_BoxFlag_Clip|
                                                    UI_BoxFlag_AllowOverflowY|
                                                    UI_BoxFlag_ViewClampX|
                                                    UI_BoxFlag_ViewScrollX|
                                                    UI_BoxFlag_Clickable,
                                                    "tab_bar_%p", panel->cfg);
            if(panel->tab_side == Side_Max)
            {
              tab_bar_box->view_off.y = tab_bar_box->view_off_target.y = (tab_bar_rheight - tab_bar_vheight);
            }
            else
            {
              tab_bar_box->view_off.y = tab_bar_box->view_off_target.y = 0;
            }
          }
          
          //////////////////////////
          //- rjf: determine tab drop site
          //
          B32 tab_drop_is_active = rd_drag_is_active() && ui_key_match(ui_drop_hot_key(), catchall_drop_site_key);
          CFG_Node *tab_drop_prev = &cfg_nil_node;
          if(build_panel)
          {
            F32 best_prev_distance_px = 1000000.f;
            TabTask start_boundary_tab_task = {first_tab_task, &cfg_nil_node};
            F32 off = tab_gap_px;
            for(TabTask *task = &start_boundary_tab_task; task != 0; task = task->next)
            {
              if(task->tab != &cfg_nil_node && task != first_tab_task)
              {
                off += tab_gap_px;
              }
              off += task->tab_width;
              Vec2F32 anchor_pt = v2f32(tab_bar_box->rect.x0 + off, tab_bar_box->rect.y1);
              F32 distance = length_2f32(sub_2f32(ui_mouse(), anchor_pt));
              if(distance < best_prev_distance_px)
              {
                best_prev_distance_px = distance;
                tab_drop_prev = task->tab;
              }
            }
          }
          
          //////////////////////////
          //- rjf: turn off drop visualization if this drag would be a no-op
          //
          if(tab_drop_is_active && rd_state->drag_drop_regs->panel == panel->cfg->id)
          {
            TabTask start_boundary_tab_task = {first_tab_task, &cfg_nil_node};
            if(tab_drop_prev->id == rd_state->drag_drop_regs->view)
            {
              tab_drop_is_active = 0;
            }
            if(tab_drop_is_active) for(TabTask *t = &start_boundary_tab_task; t != 0; t = t->next)
            {
              if(t->tab == tab_drop_prev && t->next != 0 && t->next->tab->id == rd_state->drag_drop_regs->view)
              {
                tab_drop_is_active = 0;
                break;
              }
            }
          }
          
          //////////////////////////
          //- rjf: build tab bar contents
          //
          if(build_panel) UI_Focus(UI_FocusKind_Off) UI_Parent(tab_bar_box) UI_Padding(ui_px(tab_gap_px, 1.f)) UI_PrefHeight(ui_pct(1, 0)) UI_TagF("tab")
          {
            F32 corner_radius = ui_top_font_size()*0.6f;
            TabTask start_boundary_tab_task = {first_tab_task, &cfg_nil_node};
            UI_CornerRadius00(panel->tab_side == Side_Min ? corner_radius : 0)
              UI_CornerRadius01(panel->tab_side == Side_Min ? 0 : corner_radius)
              UI_CornerRadius10(panel->tab_side == Side_Min ? corner_radius : 0)
              UI_CornerRadius11(panel->tab_side == Side_Min ? 0 : corner_radius)
              for(TabTask *tab_task = &start_boundary_tab_task; tab_task != 0; tab_task = tab_task->next)
            {
              CFG_Node *tab = tab_task->tab;
              
              //- rjf: build tab
              DR_FStrList tab_fstrs = tab_task->fstrs;
              F32 tab_width_px = tab_task->tab_width;
              if(tab != &cfg_nil_node) UIShell_RegsScope(.panel = panel->cfg->id, .view = tab->id, .tab = tab->id)
              {
                // rjf: gather info for this tab
                B32 tab_is_selected = (tab == panel->selected_tab);
                B32 tab_is_auto = rd_view_setting_b32_from_name(str8_lit("auto"));
                
                // rjf: begin vertical region for this tab
                ui_set_next_child_layout_axis(Axis2_Y);
                ui_set_next_pref_width(ui_px(tab_width_px, 1));
                UI_Box *tab_column_box = ui_build_box_from_stringf(!is_changing_panel_boundaries*UI_BoxFlag_AnimatePosX, "tab_column_%p", tab);
                
                // rjf: choose palette
                B32 omit_name = 0;
                if(rd_drag_is_active() && rd_state->drag_drop_regs->view == tab->id && rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_View)
                {
                  omit_name = 1;
                }
                
                // rjf: build tab container box
                UI_Parent(tab_column_box)
                  UI_PrefHeight(ui_px(tab_bar_vheight, 1))
                  UI_TagF(omit_name ? "hollow" : "")
                  UI_TagF(!omit_name && !tab_is_selected ? "inactive" : "")
                  UI_TagF(!omit_name && tab_is_auto ? "auto" : "")
                {
                  if(panel->tab_side == Side_Max)
                  {
                    ui_spacer(ui_px(tab_bar_rv_diff-1.f, 1.f));
                  }
                  else
                  {
                    ui_spacer(ui_px(1.f, 1.f));
                  }
                  // uishell: selected tab takes the body background and a custom
                  // rounded open outline. The panel frame pass owns the notched body
                  // edge and adjacency dedupe; inactive tab boundaries stay theme-driven.
                  if(tab_is_selected)
                  {
                    ui_set_next_background_color(panel_body_bg);
                  }
                  else if(!omit_name)
                  {
                    F32 inactive_tab_bg_dim = 0.7f; // TODO(rjf): theme metric.
                    Vec4F32 recessed = panel_body_bg;
                    recessed.x *= inactive_tab_bg_dim; recessed.y *= inactive_tab_bg_dim; recessed.z *= inactive_tab_bg_dim;
                    ui_set_next_background_color(recessed);
                  }
                  UI_Box *tab_box = ui_build_box_from_stringf(UI_BoxFlag_DrawHotEffects|
                                                              UI_BoxFlag_DrawBackground|
                                                              (UI_BoxFlag_DrawBorder * (!tab_is_selected && !omit_name))|
                                                              UI_BoxFlag_DisableFocusBorder|
                                                              UI_BoxFlag_Clickable,
                                                              "tab_%p", tab);
                  if(tab_is_selected && panel_border_px >= 1.f && !omit_name)
                  {
                    RD_SelectedTabFrameDrawData *frame_draw = push_array(ui_build_arena(), RD_SelectedTabFrameDrawData, 1);
                    frame_draw->clip_rect = tab_bar_rect;
                    frame_draw->color = panel_frame_color;
                    frame_draw->border_thickness = panel_border_px;
                    frame_draw->edge_softness = selected_tab_edge_softness;
                    frame_draw->tab_side = panel->tab_side;
                    if(panel->tab_side == Side_Max)
                    {
                      frame_draw->body_edge_p = content_rect.y1;
                      frame_draw->clip_rect.y0 = content_rect.y1 - panel_border_px;
                    }
                    else
                    {
                      frame_draw->body_edge_p = content_rect.y0;
                      frame_draw->clip_rect.y1 = content_rect.y0 + panel_border_px;
                    }
                    ui_box_equip_custom_draw(tab_box, rd_selected_tab_frame_draw, frame_draw);
                  }

                  // rjf: build tab contents
                  if(!omit_name) UI_Parent(tab_box)
                  {
                    UI_WidthFill UI_Row
                    {
                      ui_spacer(ui_em(0.5f, 1.f));
                      UI_PrefWidth(ui_text_dim(10, 0))
                      {
                        UI_Box *name_box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
                        ui_box_equip_display_fstrs(name_box, &tab_fstrs);
                      }
                    }
                    if(tab_is_selected && panel_tree.focused == panel)
                    {
                      UI_PrefWidth(ui_px(tab_close_width_px, 1.f)) UI_TextAlignment(UI_TextAlign_Center)
                        RD_Font(RD_FontSlot_Icons)
                        UI_FontSize(ui_top_font_size()*0.75f)
                        UI_TagF(".") UI_TagF("tab") UI_TagF("weak") UI_TagF("implicit")
                        UI_CornerRadius(0)
                      {
                        UI_Box *edit_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                                                     UI_BoxFlag_DrawBorder|
                                                                     UI_BoxFlag_DrawBackground|
                                                                     UI_BoxFlag_DrawText|
                                                                     UI_BoxFlag_DrawHotEffects|
                                                                     UI_BoxFlag_DrawActiveEffects,
                                                                     "%S###edit_view_%p", rd_icon_kind_text_table[RD_IconKind_Gear], tab);
                        UI_Signal sig = ui_signal_from_box(edit_box);
                        if(ui_pressed(sig))
                        {
                          if(ws->query_is_active &&
                             ui_key_match(sig.box->key, ws->query_regs->ui_key))
                          {
                            uishell_cmd("cancel_query");
                          }
                          else
                          {
                            uishell_cmd("push_query",
                                   .ui_key       = sig.box->key,
                                   .expr         = push_str8f(scratch.arena, "query:config.$%I64x", tab->id));
                          }
                        }
                      }
                    }
                    UI_PrefWidth(ui_px(tab_close_width_px, 1.f)) UI_TextAlignment(UI_TextAlign_Center)
                      RD_Font(RD_FontSlot_Icons)
                      UI_FontSize(ui_top_font_size()*0.75f)
                      UI_TagF(".") UI_TagF("tab") UI_TagF("weak") UI_TagF("implicit")
                      UI_CornerRadius00(0)
                      UI_CornerRadius01(0)
                    {
                      UI_Box *close_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                                                    UI_BoxFlag_DrawBorder|
                                                                    UI_BoxFlag_DrawBackground|
                                                                    UI_BoxFlag_DrawText|
                                                                    UI_BoxFlag_DrawHotEffects|
                                                                    UI_BoxFlag_DrawActiveEffects,
                                                                    "%S###close_view_%p", rd_icon_kind_text_table[RD_IconKind_X], tab);
                      UI_Signal sig = ui_signal_from_box(close_box);
                      if(ui_clicked(sig) || ui_middle_clicked(sig))
                      {
                        uishell_cmd("close_tab");
                      }
                    }
                  }
                  
                  // rjf: consume events for tab clicking
                  {
                    UI_Signal sig = ui_signal_from_box(tab_box);
                    if(ui_pressed(sig))
                    {
                      uishell_cmd("focus_tab");
                      uishell_cmd("focus_panel");
                    }
                    else if(ui_dragging(sig) && !rd_drag_is_active() && length_2f32(ui_drag_delta()) > 10.f)
                    {
                      rd_drag_begin(UIShell_ContextRegSlot_View);
                    }
                    else if(ui_right_clicked(sig))
                    {
                      uishell_cmd("push_query",
                             .ui_key       = sig.box->key,
                             .expr         = push_str8f(scratch.arena, "query:config.$%I64x", tab->id));
                    }
                    else if(ui_middle_clicked(sig))
                    {
                      uishell_cmd("close_tab");
                    }
                  }
                }
                
                // rjf: space for next tab
                // uishell: this is the single source of inter-tab spacing. The
                // chrome prepass and tab drop-site math use the same `tab_gap_px`.
                {
                  ui_spacer(ui_px(tab_gap_px, 1.f));
                }
              }
              
              //- rjf: if this is the currently active drop site's previous tab, then build empty space
              // to visualize where tab will be moved once dropped
              if(tab_drop_is_active &&
                 rd_drag_is_active() &&
                 rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_View &&
                 tab == tab_drop_prev)
              {
                // rjf: begin vertical region for this spot
                ui_set_next_child_layout_axis(Axis2_Y);
                ui_set_next_pref_width(ui_px(ui_top_font_size()*4.f, 1));
                UI_Box *tab_column_box = ui_build_box_from_stringf(!is_changing_panel_boundaries*UI_BoxFlag_AnimatePosX, "tab_column_%p", tab);
                
                // rjf: build spot container box
                UI_Parent(tab_column_box)
                  UI_PrefHeight(ui_px(tab_bar_vheight, 1))
                  UI_TagF("hollow")
                {
                  if(panel->tab_side == Side_Max)
                  {
                    ui_spacer(ui_px(tab_bar_rv_diff-1.f, 1.f));
                  }
                  else
                  {
                    ui_spacer(ui_px(1.f, 1.f));
                  }
                  ui_set_next_group_key(catchall_drop_site_key);
                  UI_Box *tab_box = ui_build_box_from_key(UI_BoxFlag_DrawHotEffects|
                                                          UI_BoxFlag_DrawBackground|
                                                          UI_BoxFlag_DrawBorder|
                                                          UI_BoxFlag_Clickable,
                                                          ui_key_zero());
                }
                
                // rjf: space for next tab
                {
                  ui_spacer(ui_px(tab_gap_px, 1.f));
                }
              }
            }
            
            // rjf: build add-new-tab button
            UI_TextAlignment(UI_TextAlign_Center)
              UI_PrefWidth(ui_px(tab_bar_vheight, 1.f))
              UI_PrefHeight(ui_px(tab_bar_vheight, 1.f))
              UI_TagF(".")
            {
              ui_set_next_child_layout_axis(Axis2_Y);
              UI_Box *container = ui_build_box_from_stringf(!is_changing_panel_boundaries*UI_BoxFlag_AnimatePosX, "###add_new_tab");
              tab_strip_add_button_box = container;
              UI_Parent(container)
              {
                if(panel->tab_side == Side_Max)
                {
                  ui_spacer(ui_px(tab_bar_rv_diff-1.f, 1.f));
                }
                else
                {
                  ui_spacer(ui_px(1.f, 1.f));
                }
                UI_CornerRadius00(panel->tab_side == Side_Min ? corner_radius : 0)
                  UI_CornerRadius10(panel->tab_side == Side_Min ? corner_radius : 0)
                  UI_CornerRadius01(panel->tab_side == Side_Max ? corner_radius : 0)
                  UI_CornerRadius11(panel->tab_side == Side_Max ? corner_radius : 0)
                  RD_Font(RD_FontSlot_Icons)
                  UI_FontSize(ui_top_font_size())
                  UI_TagF("implicit")
                  UI_TagF("weak")
                {
                  UI_Box *add_new_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|
                                                                  UI_BoxFlag_DrawBorder|
                                                                  UI_BoxFlag_DrawBackground|
                                                                  UI_BoxFlag_DrawHotEffects|
                                                                  UI_BoxFlag_DrawActiveEffects|
                                                                  UI_BoxFlag_Clickable|
                                                                  UI_BoxFlag_DisableTextTrunc,
                                                                  "%S##add_new_tab_button_%p",
                                                                  rd_icon_kind_text_table[RD_IconKind_Add],
                                                                  panel->cfg);
                  UI_Signal sig = ui_signal_from_box(add_new_box);
                  if(ui_pressed(sig))
                  {
                    uishell_cmd("focus_panel", .panel = panel->cfg->id);
                    if(ws->query_is_active &&
                       ui_key_match(add_new_box->key, ws->query_regs->ui_key))
                    {
                      uishell_cmd("cancel_query");
                    }
                    else
                    {
                      uishell_cmd("push_query",
                             .expr = str8_lit("query:tab_commands"),
                             .panel = panel->cfg->id,
                             .do_implicit_root = 1,
                             .do_lister = 1,
                             .ui_key = add_new_box->key);
                    }
                  }
                }
              }
            }
            
            // rjf: interact with tab bar
            ui_signal_from_box(tab_bar_box);

            // register the title-bar tab strip's interactive extent as custom
            // client area: from the strip's left edge out to the right edge of
            // the add-tab button. the empty space past it stays a window-drag
            // handle (like a browser tab strip). only the visible workspace (or
            // the direct, non-surface render) owns the real title bar — preview
            // children render the same layout but must NOT register on the OS
            // window, or their (differing) strips would blanket the drag space.
            B32 render_is_title_bar_host = (ws->active_workspace_surface_entry == 0 ||
                                            ws->active_workspace_surface_entry->composite);
            if(register_title_bar_tab_strip && render_is_title_bar_host)
            {
              F32 occupied_x1 = tab_strip_add_button_box->rect.x1;
              occupied_x1 = Clamp(tab_bar_rect.x0, occupied_x1, tab_bar_rect.x1);
              Rng2F32 strip_client_rect = r2f32p(tab_bar_rect.x0, tab_bar_rect.y0, occupied_x1, tab_bar_rect.y1);
              wm_window_push_custom_title_bar_client_area(ws->os, strip_client_rect);
            }
          }
          
          //////////////////////////
          //- rjf: accept tab drops
          //
          if(tab_drop_is_active && rd_drag_drop() && rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_View)
          {
            uishell_cmd("move_view",
                   .dst_panel = panel->cfg->id,
                   .panel     = rd_state->drag_drop_regs->panel,
                   .view     = rd_state->drag_drop_regs->view,
                   .prev_tab  = tab_drop_prev->id);
          }
          
          //////////////////////////
          //- rjf: accept file drops only in the live workspace. Preview
          // layouts overlap live coordinates and must not consume OS input.
          //
          if(!is_preview)
          {
            for(UI_Event *evt = 0; ui_next_event(&evt);)
            {
              if(evt->kind == UI_EventKind_FileDrop && contains_2f32(content_rect, evt->pos))
              {
                B32 need_drop_completion = 0;
                arena_clear(ws->drop_completion_arena);
                ws->top_drop_completion_task = 0;
                ws->drop_completion_panel = panel->cfg->id;
                String8List exe_paths = {0};
                String8List dbg_paths = {0};
                for(String8Node *n = evt->paths.first; n != 0; n = n->next)
                {
                  Temp scratch = scratch_begin(0, 0);
                  String8 path = n->string;
                  String8 ext = str8_skip_last_dot(path);
                  if(str8_match(ext, str8_lit("exe"), StringMatchFlag_CaseInsensitive))
                  {
                    str8_list_push(ws->drop_completion_arena, &exe_paths, str8_copy(ws->drop_completion_arena, path));
                  }
                  else if(str8_match(ext, str8_lit("pdb"), StringMatchFlag_CaseInsensitive) ||
                          str8_match(ext, str8_lit("rdi"), StringMatchFlag_CaseInsensitive))
                  {
                    str8_list_push(ws->drop_completion_arena, &dbg_paths, str8_copy(ws->drop_completion_arena, path));
                  }
                  else
                  {
                    uishell_cmd("open", .file_path = path, .panel = panel->cfg->id);
                  }
                  scratch_end(scratch);
                }
                if(dbg_paths.node_count != 0)
                {
                  RD_DropCompletionTask *t = push_array(ws->drop_completion_arena, RD_DropCompletionTask, 1);
                  SLLStackPush(ws->top_drop_completion_task, t);
                  t->dbg = 1;
                  t->paths = dbg_paths;
                }
                if(exe_paths.node_count != 0)
                {
                  RD_DropCompletionTask *t = push_array(ws->drop_completion_arena, RD_DropCompletionTask, 1);
                  SLLStackPush(ws->top_drop_completion_task, t);
                  t->exe = 1;
                  t->paths = exe_paths;
                }
                if(ws->top_drop_completion_task != 0)
                {
                  ui_ctx_menu_open(rd_state->drop_completion_key, ui_key_zero(), evt->pos);
                }
                ui_eat_event(evt);
              }
            }
          }
        }
      }
    }
    
    ws->window_layout_reset = 0;
    
}

////////////////////////////////
//~ rjf: Chrome Placement (ADR-0006)
//
// Measured placement resolution: shell controls (Chrome Elements) compete for a
// chrome host's width. Each starts at its placement chain's first niche; when a
// host overflows, its lowest-priority element advances down its chain to the
// next niche — another (roomier) host, or Hidden — replacing the hardcoded
// 60em/80em title-bar gates. Widths are measured analytically before the build,
// because in this immediate-mode UI layout runs after the build, so which-niche
// (which parent to build an element under) must be decided up front (ADR-0006).
//
// Only the title bar is a constrained host today; the sidebar action row is the
// roomy fallback (treated as effectively unbounded) and Hidden absorbs the rest.
// When a second constrained host appears, give it a real budget here.

internal RD_ChromeNiche
rd_chrome_niche_host_is_title_bar(RD_ChromeNiche niche)
{
  return (niche == RD_ChromeNiche_TitleBarMenu ||
          niche == RD_ChromeNiche_TitleBarLeading ||
          niche == RD_ChromeNiche_TitleBarTrailing);
}

internal void
rd_chrome_resolve(RD_ChromeElement *elements, U64 count, F32 title_bar_budget_px, RD_ChromeNiche *niche_out)
{
  // rjf: every element starts at the head of its chain
  U64 chain_pos[RD_ChromeElementKind_COUNT] = {0};
  for(U64 idx = 0; idx < count; idx += 1)
  {
    chain_pos[idx] = 0;
    niche_out[elements[idx].kind] = (elements[idx].chain_count > 0 ? elements[idx].chain[0] : RD_ChromeNiche_Hidden);
  }

  // rjf: while the title bar (the only bounded host) overflows, advance its
  // lowest-priority *advanceable* element down its chain. an element whose chain
  // terminates in a representation (no further link) is never a victim, so it
  // stays in its terminal niche (clipped if it must) rather than vanishing —
  // this is how a must-always-be-reachable control (the sidebar re-open button)
  // is kept on screen. terminates: every advance is monotonic & only taken when
  // a next link exists; the loop breaks when nothing advanceable remains.
  for(;;)
  {
    F32 title_bar_used = 0;
    U64 victim = max_U64;
    S32 victim_priority = 0;
    for(U64 idx = 0; idx < count; idx += 1)
    {
      if(rd_chrome_niche_host_is_title_bar(niche_out[elements[idx].kind]))
      {
        title_bar_used += elements[idx].width_px;
        B32 can_advance = (chain_pos[idx] + 1 < elements[idx].chain_count);
        if(can_advance && (victim == max_U64 || elements[idx].priority < victim_priority))
        {
          victim = idx;
          victim_priority = elements[idx].priority;
        }
      }
    }
    if(title_bar_used <= title_bar_budget_px || victim == max_U64) { break; }
    chain_pos[victim] += 1;
    niche_out[elements[victim].kind] = elements[victim].chain[chain_pos[victim]];
  }
}

// element build callbacks: each emits its control under the current UI parent,
// so the same element can be built in whichever niche resolution chose for it
// (a title-bar niche, or the sidebar action row).

internal UI_Signal
rd_chrome_build_new_workspace(CFG_Node *owner_cfg)
{
  UI_Signal sig = rd_icon_button(RD_IconKind_Add, 0, str8_lit("###new_workspace"));
  if(ui_hovering(sig)) UI_Tooltip RD_Font(RD_FontSlot_Main)
  {
    ui_state->tooltip_anchor_key = sig.box->key;
    ui_label(str8_lit("New Workspace"));
  }
  if(ui_clicked(sig))
  {
    uishell_cmd("new_workspace", .window = owner_cfg->id);
  }
  return sig;
}

internal UI_Signal
rd_chrome_build_overview_toggle(RD_WindowState *ws)
{
  UI_Signal sig = {0};
  UI_TagF(ws != &rd_nil_window_state && ws->workspace_zoom_open ? "" : "weak")
  {
    sig = rd_icon_button(RD_IconKind_Grid, 0, str8_lit("###workspace_zoom_toggle"));
    if(ui_clicked(sig) && ws != &rd_nil_window_state)
    {
      ws->workspace_zoom_open ^= 1;
    }
  }
  if(ui_hovering(sig)) UI_Tooltip RD_Font(RD_FontSlot_Main)
  {
    ui_state->tooltip_anchor_key = sig.box->key;
    ui_label(str8_lit("Workspace Overview"));
  }
  return sig;
}

// A separated plug/socket distinguishes workspace detachment from window close.
internal UI_BOX_CUSTOM_DRAW(rd_workspace_detach_icon_draw)
{
  F32 unit = Max(1.f, floor_f32(box->font_size/12.f));
  Vec2F32 origin = v2f32(floor_f32((box->rect.x0+box->rect.x1-18.f*unit)*0.5f),
                         floor_f32((box->rect.y0+box->rect.y1-12.f*unit)*0.5f));
  String8 color_tags[] = {str8_lit("weak"), str8_lit("text")};
  Vec4F32 color = ui_color_from_tags_key_extras(box->tags_key, (String8Array){color_tags, ArrayCount(color_tags)});
  Rng2F32 parts[] = {
    {0, 5, 3, 7}, {3, 2, 7, 10}, {7, 3, 10, 4}, {7, 8, 10, 9},
    {12, 2, 13, 10}, {13, 2, 15, 3}, {13, 9, 15, 10}, {15, 5, 18, 7},
  };
  for(U64 i = 0; i < ArrayCount(parts); i++)
  {
    Rng2F32 r = parts[i];
    dr_rect(r2f32p(origin.x+r.x0*unit, origin.y+r.y0*unit,
                  origin.x+r.x1*unit, origin.y+r.y1*unit), color, 0, 0, 0);
  }
}

internal void
rd_chrome_build_workspace_path(CFG_Node *owner_cfg, F32 width_px)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, owner_cfg);
  UIShell_MaterializedWorkspace *workspace = split.inventory.selected;
  RD_WindowState *ws = rd_window_state_from_cfg(owner_cfg);
  UIShell_SidebarState *sidebar = uishell_sidebar_init(ws);
  if(sidebar->core)
  {
    uishell_sidebar_observe(sidebar, &split);
    uishell_sidebar_refresh(sidebar);
  }
  String8 leaf = str8_lit("Workspace");
  String8 path = workspace ? uishell_sidebar_workspace_path(scratch.arena, sidebar, workspace->id,
                                                            workspace->display_name, &leaf) : leaf;
  String8 display = path;
  F32 available = width_px - 16.f;
  B32 shortened = (workspace && fnt_dim_from_tag_size_string(rd_font_from_slot(RD_FontSlot_Main),
                                                              ui_top_font_size(), 0, 0, path).x > available);
  if(shortened)
  { display = leaf; }
  UI_Signal sig = {0};
  UI_TagF("weak") UI_HeightFill UI_TextPadding(8.f) UI_TextAlignment(UI_TextAlign_Left)
  {
    UI_Box *box = ui_build_box_from_string(UI_BoxFlag_DrawText|UI_BoxFlag_DisableTruncatedHover,
      push_str8f(scratch.arena, "%S###workspace_path", display));
    sig = ui_signal_from_box(box);
  }
  if(shortened && ui_mouse_over(sig)) UI_Tooltip RD_Font(RD_FontSlot_Main)
  {
    ui_state->tooltip_anchor_key = sig.box->key;
    ui_label(path);
  }
  scratch_end(scratch);
}

internal UI_Signal
rd_chrome_build_workspace_action(CFG_Node *owner_cfg, B32 close)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, owner_cfg);
  UIShell_MaterializedWorkspace *workspace = split.inventory.selected;
  B32 enabled = workspace != 0 && (!close || uishell_controlled_split_workspace_can_close(&split, workspace));
  UI_Signal sig = {0};
  UI_TagF(enabled ? "" : "weak")
  {
    sig = rd_icon_button(close ? RD_IconKind_Null : RD_IconKind_Target, 0,
      close ? str8_lit("###toolbar_close_workspace") : str8_lit("###toolbar_reveal_workspace"));
    if(close) { ui_box_equip_custom_draw(sig.box, rd_workspace_detach_icon_draw, 0); }
  }
  if(ui_hovering(sig)) UI_Tooltip RD_Font(RD_FontSlot_Main)
  {
    ui_state->tooltip_anchor_key = sig.box->key;
    ui_labelf("%s: %S", close ? "Close workspace" : "Reveal workspace in sidebar", workspace ? workspace->display_name : str8_lit("None"));
    if(close && !enabled) { ui_label(str8_lit("The last workspace cannot be closed")); }
  }
  if(enabled && ui_clicked(sig))
  {
    if(close) { uishell_cmd("close_workspace", .window = owner_cfg->id, .cfg = workspace->id); }
    else
    {
      RD_WindowState *ws = rd_window_state_from_cfg(owner_cfg);
      UIShell_SidebarState *sidebar = uishell_sidebar_init(ws);
      sidebar->reveal_workspace_id = workspace->id;
      CFG_Node *collapsed = cfg_node_child_from_string(owner_cfg, str8_lit("control_split_collapsed"));
      if(collapsed != &cfg_nil_node) { cfg_node_release(rd_state->cfg, collapsed); }
      ws->workspace_zoom_open = 0;
      rd_request_frame();
    }
  }
  scratch_end(scratch);
  return sig;
}

internal UI_Signal
rd_chrome_build_sidebar_collapse(CFG_Node *owner_cfg)
{
  B32 collapsed = (cfg_node_child_from_string(owner_cfg, str8_lit("control_split_collapsed")) != &cfg_nil_node);
  UI_Signal sig = {0};
  UI_TagF(collapsed ? "" : "weak")
  {
    sig = rd_icon_button(RD_IconKind_List, 0, str8_lit("###sidebar_collapse"));
  }
  if(ui_hovering(sig)) UI_Tooltip RD_Font(RD_FontSlot_Main)
  {
    ui_state->tooltip_anchor_key = sig.box->key;
    ui_label(collapsed ? str8_lit("Show Sidebar") : str8_lit("Hide Sidebar"));
  }
  if(ui_clicked(sig))
  {
    CFG_Node *node = cfg_node_child_from_string(owner_cfg, str8_lit("control_split_collapsed"));
    if(node != &cfg_nil_node) { cfg_node_release(rd_state->cfg, node); }
    else                      { cfg_node_new(rd_state->cfg, owner_cfg, str8_lit("control_split_collapsed")); }
    // this is a layout-changing cfg mutation with no command or animation behind
    // it, so under frame-on-demand nothing would re-render it until the next
    // incidental wake — request the frame ourselves (the drag path gets this via
    // continuous motion events).
    rd_request_frame();
  }
  return sig;
}

#if COMPILER_MSVC && !BUILD_DEBUG
NO_OPTIMIZE_BEGIN
#endif

internal void
rd_window_frame(void)
{
  Temp scratch = scratch_begin(0, 0);
  ProfBeginFunction();
  
  //////////////////////////////
  //- rjf: @window_frame_part unpack context
  //
  CFG_Node *window          = cfg_node_from_id(uishell_regs()->window);
  RD_WindowState *ws      = rd_window_state_from_cfg(cfg_node_from_id(uishell_regs()->window));
  UIShell_ControlledSplit root_controlled_split = uishell_root_controlled_split_from_window(scratch.arena, window);
  UIShell_WorkspaceMount *workspace_mount = uishell_controlled_split_selected_mount(&root_controlled_split);
  CFG_PanelTree panel_tree = workspace_mount->panel_tree;
  B32 window_is_focused   = (wm_window_is_focused(ws->os) || ws->window_temporarily_focused_ipc);
  B32 popup_is_open       = (rd_state->popup_active);
  B32 query_is_open       = (ws->query_is_active);
  U64 hover_eval_open_delay_us = 400000;
  B32 hover_eval_is_open  = (!popup_is_open &&
                             !query_is_open &&
                             ws->hover_eval_string.size != 0 &&
                             ws->hover_eval_firstt_us+hover_eval_open_delay_us < ws->hover_eval_lastt_us &&
                             rd_state->time_in_us - ws->hover_eval_lastt_us < hover_eval_open_delay_us);
  if(!window_is_focused || popup_is_open)
  {
    ws->menu_bar_key_held = 0;
  }
  ws->window_temporarily_focused_ipc = 0;
  ui_select_state(ws->ui);
  CFG_NodePtrList theme_color_cfgs = rd_theme_color_cfgs_from_user_project(scratch.arena);
  
  //////////////////////////////
  //- rjf: @window_frame_part fill panel/view interaction registers
  //
  uishell_regs()->panel = panel_tree.focused->cfg->id;
  uishell_regs()->tab   = panel_tree.focused->selected_tab->id;
  uishell_regs()->view = panel_tree.focused->selected_tab->id;
  
  //////////////////////////////
  //- rjf: @window_frame_part compute window's theme
  //
  {
    Access *access = access_open();
    String8 theme_name = rd_window_theme_name_from_settings();
    ws->theme = rd_theme_from_name_and_colors(scratch.arena, access, theme_name, theme_color_cfgs, 1);
    access_close(access);
  }
  
  //////////////////////////////
  //- rjf: @window_frame_part compute window's font raster flags
  //
  {
    ws->font_slot_raster_flags[RD_FontSlot_Icons] = FNT_RasterFlag_Smooth;
    ws->font_slot_raster_flags[RD_FontSlot_Main] = (rd_setting_b32_from_name(str8_lit("smooth_ui_text"))*FNT_RasterFlag_Smooth)|(rd_setting_b32_from_name(str8_lit("hint_ui_text"))*FNT_RasterFlag_Hinted);
    ws->font_slot_raster_flags[RD_FontSlot_Code] = (rd_setting_b32_from_name(str8_lit("smooth_code_text"))*FNT_RasterFlag_Smooth)|(rd_setting_b32_from_name(str8_lit("hint_code_text"))*FNT_RasterFlag_Hinted);
  }
  
  //////////////////////////////
  //- rjf: @window_frame_part pre-emptively rasterize common glyphs on the first frame
  //
  if(rd_state->first_window_state == ws && rd_state->last_window_state == ws && ws->frames_alive == 0)
  {
    F32 font_size = rd_font_size();
    F32 raster_scale = wm_backing_scale_from_window(ws->os);
    RD_FontSlot english_font_slots[] = {RD_FontSlot_Main, RD_FontSlot_Code};
    RD_FontSlot icon_font_slot = RD_FontSlot_Icons;
    for(U64 idx = 0; idx < ArrayCount(english_font_slots); idx += 1)
    {
      Temp scratch = scratch_begin(0, 0);
      RD_FontSlot slot = english_font_slots[idx];
      String8 sample_text = str8_lit("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz1234567890~!@#$%^&*()-_+=[{]}\\|;:'\",<.>/?");
      fnt_run_from_string_scaled(rd_font_from_slot(slot),
                                 font_size,
                                 raster_scale,
                                 0, 0, 0,
                                 sample_text);
      fnt_run_from_string_scaled(rd_font_from_slot(slot),
                                 font_size,
                                 raster_scale,
                                 0, 0, 0,
                                 sample_text);
      scratch_end(scratch);
    }
    for(RD_IconKind icon_kind = RD_IconKind_Null; icon_kind < RD_IconKind_COUNT; icon_kind = (RD_IconKind)(icon_kind+1))
    {
      Temp scratch = scratch_begin(0, 0);
      fnt_run_from_string_scaled(rd_font_from_slot(icon_font_slot),
                                 font_size,
                                 raster_scale,
                                 0, 0, FNT_RasterFlag_Smooth,
                                 rd_icon_kind_text_table[icon_kind]);
      fnt_run_from_string_scaled(rd_font_from_slot(icon_font_slot),
                                 font_size,
                                 raster_scale,
                                 0, 0, FNT_RasterFlag_Smooth,
                                 rd_icon_kind_text_table[icon_kind]);
      fnt_run_from_string_scaled(rd_font_from_slot(icon_font_slot),
                                 font_size,
                                 raster_scale,
                                 0, 0, FNT_RasterFlag_Smooth,
                                 rd_icon_kind_text_table[icon_kind]);
      scratch_end(scratch);
    }
  }
  
  //////////////////////////////
  //- rjf: @window_frame_part commit window's position/status to underlying cfg tree
  //
  {
    Temp scratch = scratch_begin(0, 0);
    B32 is_fullscreen = wm_window_is_fullscreen(ws->os);
    B32 is_maximized = wm_window_is_maximized(ws->os);
    B32 is_minimized = wm_window_is_minimized(ws->os);
    if(is_fullscreen)
    {
      cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("fullscreen"));
    }
    else
    {
      cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("fullscreen")));
    }
    if(is_maximized)
    {
      cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("maximized"));
    }
    else
    {
      cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("maximized")));
    }
    
    //- rjf: scale changes -> xform layout font size and refresh raster caches
    F32 layout_scale = wm_layout_scale_from_window(ws->os);
    F32 backing_scale = wm_backing_scale_from_window(ws->os);
    if(layout_scale != ws->last_layout_scale)
    {
      fnt_reset();
      if(ws->last_layout_scale != 0)
      {
        F32 current_font_size = rd_font_size();
        F32 new_font_size = current_font_size * (layout_scale / ws->last_layout_scale);
        new_font_size = Clamp(6.f, new_font_size, 72.f);
        CFG_Node *font_size_cfg = cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("font_size"));
        cfg_node_new_replacef(rd_state->cfg, font_size_cfg, "%I64u", (U64)new_font_size);
      }
      ws->last_layout_scale = layout_scale;
    }
    if(backing_scale != ws->last_backing_scale)
    {
      fnt_reset();
      ws->last_backing_scale = backing_scale;
    }
    
    //- rjf: commit position
    Rng2F32 window_rect = wm_rect_from_window(ws->os);
    if(!is_fullscreen && !is_maximized && !is_minimized)
    {
      Vec2F32 pos = window_rect.p0;
      CFG_Node *pos_root = cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("pos"));
      if((S32)pos.x != (S32)f64_from_str8(pos_root->first->string) ||
         (S32)pos.y != (S32)f64_from_str8(pos_root->last->string))
      {
        CFG_Node *x = pos_root->first;
        if(x == &cfg_nil_node)
        {
          x= cfg_node_alloc(rd_state->cfg);
          cfg_node_insert_child(rd_state->cfg, pos_root, &cfg_nil_node, x);
        }
        CFG_Node *y = x->next;
        if(y == &cfg_nil_node)
        {
          y = cfg_node_alloc(rd_state->cfg);
          cfg_node_insert_child(rd_state->cfg, pos_root, x, y);
        }
        cfg_node_equip_stringf(rd_state->cfg, x, "%i", (S32)pos.x);
        cfg_node_equip_stringf(rd_state->cfg, y, "%i", (S32)pos.y);
      }
    }
    
    //- rjf: commit size
    if(!is_fullscreen && !is_maximized && !is_minimized)
    {
      Vec2F32 size = dim_2f32(window_rect);
      CFG_Node *size_root = cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("size"));
      if((S32)size.x != (S32)f64_from_str8(size_root->first->string) ||
         (S32)size.y != (S32)f64_from_str8(size_root->last->string))
      {
        CFG_Node *width = size_root->first;
        if(width == &cfg_nil_node)
        {
          width = cfg_node_alloc(rd_state->cfg);
          cfg_node_insert_child(rd_state->cfg, size_root, &cfg_nil_node, width);
        }
        CFG_Node *height = width->next;
        if(height == &cfg_nil_node)
        {
          height = cfg_node_alloc(rd_state->cfg);
          cfg_node_insert_child(rd_state->cfg, size_root, width, height);
        }
        cfg_node_equip_stringf(rd_state->cfg, width, "%i", (S32)size.x);
        cfg_node_equip_stringf(rd_state->cfg, height, "%i", (S32)size.y);
      }
    }
    
    //- rjf: commit monitor
    if(!is_minimized)
    {
      WM_Monitor monitor = wm_monitor_from_window(ws->os);
      String8 monitor_name = wm_name_from_monitor(scratch.arena, monitor);
      CFG_Node *monitor_root = cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("monitor"));
      if(!str8_match(monitor_root->first->string, monitor_name, 0))
      {
        cfg_node_new_replace(rd_state->cfg, monitor_root, monitor_name);
      }
    }
    scratch_end(scratch);
  }
  
  //////////////////////////////
  //- rjf: @window_frame_part build UI
  //
  UI_Box *lister_box = &ui_nil_box;
  ProfScope("build UI")
  {
    ////////////////////////////
    //- rjf: @window_ui_part set up
    //
    {
      // rjf: get top-level font size info
      F32 top_level_font_size = 0;
      UIShell_RegsScope(.view = 0, .tab = 0) top_level_font_size = rd_font_size();
      
      // rjf: build icon info
      UI_IconInfo icon_info = {0};
      {
        icon_info.icon_font = rd_font_from_slot(RD_FontSlot_Icons);
        icon_info.icon_kind_text_map[UI_IconKind_RightArrow]     = rd_icon_kind_text_table[RD_IconKind_RightScroll];
        icon_info.icon_kind_text_map[UI_IconKind_DownArrow]      = rd_icon_kind_text_table[RD_IconKind_DownScroll];
        icon_info.icon_kind_text_map[UI_IconKind_LeftArrow]      = rd_icon_kind_text_table[RD_IconKind_LeftScroll];
        icon_info.icon_kind_text_map[UI_IconKind_UpArrow]        = rd_icon_kind_text_table[RD_IconKind_UpScroll];
        icon_info.icon_kind_text_map[UI_IconKind_RightCaret]     = rd_icon_kind_text_table[RD_IconKind_RightCaret];
        icon_info.icon_kind_text_map[UI_IconKind_DownCaret]      = rd_icon_kind_text_table[RD_IconKind_DownCaret];
        icon_info.icon_kind_text_map[UI_IconKind_LeftCaret]      = rd_icon_kind_text_table[RD_IconKind_LeftCaret];
        icon_info.icon_kind_text_map[UI_IconKind_UpCaret]        = rd_icon_kind_text_table[RD_IconKind_UpCaret];
        icon_info.icon_kind_text_map[UI_IconKind_CheckHollow]    = rd_icon_kind_text_table[RD_IconKind_CheckHollow];
        icon_info.icon_kind_text_map[UI_IconKind_CheckFilled]    = rd_icon_kind_text_table[RD_IconKind_CheckFilled];
      }
      
      // rjf: build animation info
      UI_AnimationInfo animation_info = {0};
      {
        animation_info.hot_animation_rate      = rd_state->catchall_animation_rate;
        animation_info.active_animation_rate   = rd_state->catchall_animation_rate;
        animation_info.focus_animation_rate    = 1.f;
        animation_info.tooltip_animation_rate  = rd_state->tooltip_animation_rate;
        animation_info.menu_animation_rate     = rd_state->menu_animation_rate;
        animation_info.scroll_animation_rate   = rd_state->scrolling_animation_rate;
      }
      
      // uishell: select the scroll-bar rendering style for this frame. Global
      // setting for now; a candidate to become workspace/theme-scoped later.
      ui_set_active_scroll_bar_style(rd_setting_b32_from_name(str8_lit("overlay_scrollbars")) ? UI_ScrollBarStyle_Overlay : UI_ScrollBarStyle_Classic);

      // rjf: begin & push initial stack values
      ui_begin_build(ws->os, &ws->ui_events, &icon_info, ws->theme, &animation_info, rd_state->frame_dt, rd_state->frame_dt);
      ui_push_font(rd_font_from_slot(RD_FontSlot_Main));
      ui_push_font_size(top_level_font_size);
      ui_push_text_padding(floor_f32(ui_top_font_size()*0.3f));
      ui_push_pref_width(ui_px(floor_f32(ui_top_font_size()*20.f), 1.f));
      ui_push_pref_height(ui_px(floor_f32(ui_top_font_size()*3.f), 1.f));
      ui_push_blur_size(10.f);
      FNT_RasterFlags text_raster_flags = 0;
      if(rd_setting_b32_from_name(str8_lit("smooth_ui_text"))) {text_raster_flags |= FNT_RasterFlag_Smooth;}
      if(rd_setting_b32_from_name(str8_lit("hint_ui_text"))) {text_raster_flags |= FNT_RasterFlag_Hinted;}
      ui_push_text_raster_flags(text_raster_flags);
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part calculate top-level rectangles/sizes
    //
    Rng2F32 window_rect = wm_client_rect_from_window(ws->os);
    Vec2F32 window_rect_dim = dim_2f32(window_rect);
    F32 top_bar_dim_px = floor_f32(ui_top_font_size()*3.f);
    // status bar is optional (default off): when hidden it occupies no height &
    // the content area extends to the window bottom.
    B32 show_status_bar = rd_setting_b32_from_name(str8_lit("show_status_bar"));
    F32 bottom_bar_dim_px = show_status_bar ? top_bar_dim_px : 0.f;
    Rng2F32 top_bar_rect = r2f32p(window_rect.x0, window_rect.y0, window_rect.x0+window_rect_dim.x+1, window_rect.y0+top_bar_dim_px);
    Rng2F32 bottom_bar_rect = r2f32p(window_rect.x0, window_rect_dim.y - bottom_bar_dim_px, window_rect.x0+window_rect_dim.x, window_rect.y0+window_rect_dim.y);
    Rng2F32 content_rect = r2f32p(window_rect.x0, top_bar_rect.y1, window_rect.x0+window_rect_dim.x, bottom_bar_rect.y0);
    F32 window_edge_px = 96.f*wm_layout_scale_from_window(ws->os)*0.035f;
    content_rect = pad_2f32(content_rect, -window_edge_px);
    // Window-edge padding belongs at the outer edges, not at the internal seam
    // with title-bar chrome. Panel spacing is applied by the panel layout.
    content_rect.y0 = top_bar_rect.y1;
    
    ////////////////////////////
    //- rjf: @window_ui_part truncated string hover
    //
    if(ui_string_hover_active()) UI_Tooltip
    {
      Temp scratch = scratch_begin(0, 0);
      DR_FStrList fstrs = ui_string_hover_fstrs(scratch.arena);
      UI_Box *box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
      ui_box_equip_display_fstrs(box, &fstrs);
      scratch_end(scratch);
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part rich hover / drag/drop tooltips
    //
    if((rd_state->hover_regs_slot != UIShell_ContextRegSlot_Null) || (rd_state->drag_drop_regs_slot != UIShell_ContextRegSlot_Null && rd_drag_is_active()))
    {
      Temp scratch = scratch_begin(0, 0);
      B32 use_drag_regs = (rd_state->drag_drop_regs_slot != UIShell_ContextRegSlot_Null && rd_drag_is_active());
      UIShell_ContextRegSlot slot = use_drag_regs ? rd_state->drag_drop_regs_slot : rd_state->hover_regs_slot;
      UIShell_Regs *regs = use_drag_regs ? rd_state->drag_drop_regs : rd_state->hover_regs;
      ui_state->tooltip_anchor_key = regs->ui_key;
      ui_state->tooltip_can_overflow_window = rd_drag_is_active();
      switch(slot)
      {
        default:{}break;
        
        ////////////////////////
        //- rjf: command tooltips
        //
        case UIShell_ContextRegSlot_CmdName:
        UI_Tooltip
        {
          String8 cmd_name = regs->cmd_name;
          DR_FStrList fstrs = rd_title_fstrs_from_code_name(scratch.arena, cmd_name);
          UI_PrefWidth(ui_children_sum(1)) UI_Row UI_PrefWidth(ui_text_dim(5, 1))
          {
            UI_Box *box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
            ui_box_equip_display_fstrs(box, &fstrs);
            rd_cmd_binding_buttons(cmd_name, str8_zero(), 0);
          }
        }break;
        
        ////////////////////////
        //- rjf: file path tooltips
        //
        case UIShell_ContextRegSlot_FilePath:
        UI_Tooltip
        {
          FileProperties props = properties_from_file_path(regs->file_path);
          ui_set_next_pref_width(ui_children_sum(1));
          UI_Row
          {
            RD_Font(RD_FontSlot_Icons) ui_label(rd_icon_kind_text_table[props.flags & FilePropertyFlag_IsFolder ? RD_IconKind_FolderClosedFilled : RD_IconKind_FileOutline]);
            ui_label(regs->file_path);
          }
        }break;
        
        ////////////////////////
        //- rjf: cfg tooltips
        //
        case UIShell_ContextRegSlot_Cfg:
        UI_Tooltip
        {
          // rjf: unpack
          CFG_Node *cfg = cfg_node_from_id(regs->cfg);
          DR_FStrList fstrs = rd_title_fstrs_from_cfg(scratch.arena, cfg, 0);
          
          // rjf: title
          UI_PrefWidth(ui_children_sum(1)) UI_Row UI_PrefWidth(ui_text_dim(5, 1))
          {
            UI_Box *box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
            ui_box_equip_display_fstrs(box, &fstrs);
          }
        }break;
        
        ////////////////////////
        //- rjf: expression tooltips
        //
        case UIShell_ContextRegSlot_Expr:
        UI_Tooltip RD_Font(RD_FontSlot_Code)
        {
          ui_set_next_pref_width(ui_children_sum(1));
          UI_Row
          {
            rd_code_label(1.f, 0, ui_color_from_name(str8_lit("text")), regs->expr);
            E_Eval eval = e_eval_from_string(regs->expr);
            if(eval.irtree.mode != E_Mode_Null)
            {
              EV_StringParams string_params = {.flags = EV_StringFlag_ReadOnlyDisplayRules|rd_state->eval_viz_base_string_flags, .radix = 10};
              String8 value_string = rd_value_string_from_eval(scratch.arena, str8_zero(), &string_params, ui_top_font(), ui_top_font_size(), ui_top_font_size()*20.f, eval);
              if(value_string.size != 0)
              {
                ui_spacer(ui_em(2.f, 1.f));
                rd_code_label(1.f, 0, ui_color_from_name(str8_lit("text")), value_string);
              }
            }
          }
        }break;
      }
      scratch_end(scratch);
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part drag/drop visualization tooltips
    //
    if(rd_drag_is_active() && window_is_focused)
      UIShell_RegsScope(.window = rd_state->drag_drop_regs->window,
                   .panel = rd_state->drag_drop_regs->panel,
                   .tab = 0,
                   .view = rd_state->drag_drop_regs->view)
    {
      Temp scratch = scratch_begin(0, 0);
      CFG_Node *view = cfg_node_from_id(rd_state->drag_drop_regs->view);
      {
        //- rjf: tab dragging
        if(rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_View && view != &cfg_nil_node)
        {
          CFG_Node *immediate_parent = &cfg_nil_node;
          for(CFG_Node *p = view->parent; p != &cfg_nil_node; p = p->parent)
          {
            if(str8_match(p->parent->string, str8_lit("immediate"), 0))
            {
              immediate_parent = p->parent;
              break;
            }
          }
          if(immediate_parent != &cfg_nil_node)
          {
            cfg_node_child_from_string_or_alloc(rd_state->cfg, immediate_parent, str8_lit("hot"));
          }
          UI_Size main_width = ui_top_pref_width();
          UI_Size main_height = ui_top_pref_height();
          UI_TextAlign main_text_align = ui_top_text_alignment();
          UI_Tooltip
            UI_PrefWidth(main_width)
            UI_PrefHeight(main_height)
            UI_TextAlignment(main_text_align)
          {
            ui_state->tooltip_can_overflow_window = 1;
            ui_set_next_pref_width(ui_em(60.f, 1.f));
            ui_set_next_pref_height(ui_em(40.f, 1.f));
            ui_set_next_child_layout_axis(Axis2_Y);
            UI_Box *container = ui_build_box_from_key(0, ui_key_zero());
            UI_Parent(container)
            {
              UI_Row UI_PrefWidth(ui_text_dim(10, 1))
              {
                DR_FStrList fstrs = rd_title_fstrs_from_cfg(scratch.arena, view, 0);
                UI_Box *name_box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
                ui_box_equip_display_fstrs(name_box, &fstrs);
              }
              ui_set_next_pref_width(ui_pct(1, 0));
              ui_set_next_pref_height(ui_pct(1, 0));
              ui_set_next_child_layout_axis(Axis2_Y);
              UI_Box *view_preview_container = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_Clip, "###view_preview_container");
              UI_Parent(view_preview_container) UI_Focus(UI_FocusKind_Off) UI_WidthFill
              {
                rd_view_ui(view_preview_container->rect);
              }
            }
          }
        }
      }
      scratch_end(scratch);
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part developer menu
    //
    if(ws->dev_menu_is_open) RD_Font(RD_FontSlot_Code)
    {
      ui_set_next_flags(UI_BoxFlag_ViewScrollY|UI_BoxFlag_AllowOverflowY|UI_BoxFlag_ViewClamp);
      UI_PaneF(r2f32p(30, 30, 30+ui_top_font_size()*100, ui_top_font_size()*60), "###dev_ctx_menu")
      {
        //- close
        if(ui_clicked(ui_buttonf("Close###dev_menu_close")) || ui_slot_press(UI_EventActionSlot_Cancel))
        {
          ws->dev_menu_is_open = 0;
        }

        //- rjf: capture
        if(!ProfIsCapturing() && ui_clicked(ui_buttonf("Begin Profiler Capture###prof_cap")))
        {
          ProfBeginCapture("uishell");
        }
        else if(ProfIsCapturing() && ui_clicked(ui_buttonf("End Profiler Capture###prof_cap")))
        {
          ProfEndCapture();
        }
        
        //- rjf: toggles
        for(U64 idx = 0; idx < ArrayCount(DEV_toggle_table); idx += 1)
        {
          if(ui_clicked(rd_icon_button(*DEV_toggle_table[idx].value_ptr ? RD_IconKind_CheckFilled : RD_IconKind_CheckHollow, 0, DEV_toggle_table[idx].name)))
          {
            *DEV_toggle_table[idx].value_ptr ^= 1;
          }
        }
        
        ui_divider(ui_em(1.f, 1.f));
        
        //- rjf: draw registers
        ui_labelf("hover_reg_slot: %i", rd_state->hover_regs_slot);
        UIShell_Regs top_regs = uishell_regs_copy(scratch.arena, uishell_regs());
        struct
        {
          String8 name;
          UIShell_Regs *regs;
        }
        regs_info[] =
        {
          {str8_lit("regs"),       &top_regs},
          {str8_lit("hover_regs"), rd_state->hover_regs},
        };
        for EachElement(idx, regs_info)
        {
          ui_divider(ui_em(1.f, 1.f));
          ui_label(regs_info[idx].name);
          UIShell_Regs *regs = regs_info[idx].regs;
#define ID(name) ui_labelf("%s: $0x%I64x", #name, (regs->name))
          ID(window);
          ID(panel);
          ID(view);
#undef ID
          ui_labelf("file_path: \"%S\"", regs->file_path);
          ui_labelf("cursor: (L:%I64d, C:%I64d)", regs->cursor.line, regs->cursor.column);
          ui_labelf("mark: (L:%I64d, C:%I64d)", regs->mark.line, regs->mark.column);
          ui_labelf("text_key: [0x%I64x / 0x%I64x:0x%I64x]", regs->text_key.root.u64[0], regs->text_key.id.u128[0].u64[0], regs->text_key.id.u128[0].u64[1]);
          ui_labelf("lang_kind: '%S'", txt_extension_from_lang_kind(regs->lang_kind));
        }
        
        ui_divider(ui_em(1.f, 1.f));
        
        //- rjf: draw per-window stats
        for(RD_WindowState *w = rd_state->first_window_state; w != &rd_nil_window_state; w = w->order_next)
        {
          // rjf: calc ui hash chain length
          F64 avg_ui_hash_chain_length = 0;
          {
            F64 chain_count = 0;
            F64 chain_length_sum = 0;
            for(U64 idx = 0; idx < w->ui->box_table_size; idx += 1)
            {
              F64 chain_length = 0;
              for(UI_Box *b = w->ui->box_table[idx].hash_first; !ui_box_is_nil(b); b = b->hash_next)
              {
                chain_length += 1;
              }
              if(chain_length > 0)
              {
                chain_length_sum += chain_length;
                chain_count += 1;
              }
            }
            avg_ui_hash_chain_length = chain_length_sum / chain_count;
          }
          ui_labelf("Target Hz: %.2f", 1.f/rd_state->frame_dt);
          ui_labelf("Window %p", w);
          ui_set_next_pref_width(ui_children_sum(1));
          ui_set_next_pref_height(ui_children_sum(1));
          UI_Row
          {
            ui_spacer(ui_em(2.f, 1.f));
            ui_labelf("Box Count: %I64u", w->ui->last_build_box_count);
          }
          ui_set_next_pref_width(ui_children_sum(1));
          ui_set_next_pref_height(ui_children_sum(1));
          UI_Row
          {
            ui_spacer(ui_em(2.f, 1.f));
            ui_labelf("Average UI Hash Chain Length: %f", avg_ui_hash_chain_length);
          }
        }
        
        ui_divider(ui_em(1.f, 1.f));
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part drop-completion context menu
    //
    if(ws->top_drop_completion_task != 0)
    {
      RD_DropCompletionTask *task = ws->top_drop_completion_task;
      B32 done = 0;
      UI_CtxMenu(rd_state->drop_completion_key) UI_PrefWidth(ui_em(40.f, 1.f)) UI_TagF("implicit")
      {
        // rjf: file names
        UI_TagF("weak") UI_Row UI_Padding(ui_em(1.25f, 1.f))
        {
          String8List strings = {0};
          U64 idx = 0;
          for(String8Node *n = task->paths.first; n != 0 && idx < 20; n = n->next, idx += 1)
          {
            str8_list_push(scratch.arena, &strings, str8_skip_last_slash(n->string));
            if(idx+1 == 20)
            {
              str8_list_push(scratch.arena, &strings, str8_lit("..."));
            }
          }
          StringJoin join = {.sep = str8_lit(", ")};
          String8 string = str8_list_join(scratch.arena, &strings, &join);
          UI_PrefWidth(ui_pct(1, 0)) ui_label(string);
        }
        
	        
        
        // rjf: option to just open & view the file contents
        if(ui_clicked(rd_icon_buttonf(RD_IconKind_FileOutline, 0, "View file%s contents", (task->paths.node_count > 1) ? "s'" : "")))
        {
          for(String8Node *n = task->paths.first; n != 0; n = n->next)
          {
            uishell_cmd("open", .file_path = n->string);
          }
          done = 1;
        }
      }
      
      // rjf: pop task, close context menu if needed, when done
      if(done)
      {
        SLLStackPop(ws->top_drop_completion_task);
        if(ws->top_drop_completion_task == 0)
        {
          ui_ctx_menu_close();
        }
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part popup
    //
    {
      if(rd_state->popup_t > 0.005f) UI_TextAlignment(UI_TextAlign_Center) UI_Focus(rd_state->popup_active ? UI_FocusKind_Root : UI_FocusKind_Off)
      {
        Vec2F32 window_dim = dim_2f32(window_rect);
        UI_Box *bg_box = &ui_nil_box;
        UI_Rect(window_rect)
          UI_ChildLayoutAxis(Axis2_X)
          UI_Focus(UI_FocusKind_On)
          UI_BlurSize(10*rd_state->popup_t)
          UI_Transparency(1-rd_state->popup_t)
          UI_TagF("floating")
        {
          bg_box = ui_build_box_from_stringf(UI_BoxFlag_FixedSize|
                                             UI_BoxFlag_Floating|
                                             UI_BoxFlag_Clickable|
                                             UI_BoxFlag_Scroll|
                                             UI_BoxFlag_DefaultFocusNav|
                                             UI_BoxFlag_DisableFocusOverlay|
                                             UI_BoxFlag_DrawBackgroundBlur|
                                             UI_BoxFlag_DrawBackground, "###popup_%p", ws);
        }
        if(rd_state->popup_active) UI_Parent(bg_box) UI_Transparency(1-rd_state->popup_t)
        {
          ui_ctx_menu_close();
          UI_WidthFill UI_PrefHeight(ui_children_sum(1.f)) UI_Column UI_Padding(ui_pct(1, 0))
          {
            UI_TextRasterFlags(rd_raster_flags_from_slot(RD_FontSlot_Main)) UI_FontSize(ui_top_font_size()*2.f) UI_PrefHeight(ui_em(3.f, 1.f)) ui_label(rd_state->popup_title);
            UI_PrefHeight(ui_em(3.f, 1.f)) UI_TagF("weak") ui_label(rd_state->popup_desc);
            ui_spacer(ui_em(1.5f, 1.f));
            UI_Row UI_Padding(ui_pct(1.f, 0.f)) UI_PrefWidth(ui_em(16.f, 1.f)) UI_PrefHeight(ui_em(3.5f, 1.f)) UI_CornerRadius(ui_top_font_size()*0.5f)
            {
              UI_TagF("pop")
                if(ui_clicked(ui_buttonf("OK")) || (ui_key_match(bg_box->default_nav_focus_hot_key, ui_key_zero()) && ui_slot_press(UI_EventActionSlot_Accept)))
              {
                uishell_cmd("popup_accept");
              }
              ui_spacer(ui_em(1.f, 1.f));
              if(ui_clicked(ui_buttonf("Cancel")) || ui_slot_press(UI_EventActionSlot_Cancel))
              {
                uishell_cmd("popup_cancel");
              }
            }
            ui_spacer(ui_em(3.f, 1.f));
          }
        }
        ui_signal_from_box(bg_box);
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part build autocompletion callee info helper
    //
    F32 autocomp_callee_helper_height_px = 0;
    if(rd_setting_b32_from_name(str8_lit("view_call_argument_helper")) &&
       ws->autocomp_regs != 0 && ws->autocomp_last_frame_index+1 >= rd_state->frame_index &&
       ws->autocomp_cursor_info.callee_expr.size != 0)
    {
      E_Eval eval = e_eval_from_string(ws->autocomp_cursor_info.callee_expr);
      E_Type *type = e_type_from_key(eval.irtree.type_key);
      if(type->kind == E_TypeKind_LensSpec) UI_TagF("floating")
      {
        F32 open_t = ui_anim(ui_key_from_stringf(ui_key_zero(), "autocomp_callee_helper_t"), 1.f, .rate = rd_state->menu_animation_rate);
        
        //- rjf: determine rects/sizes
        F32 row_height_px = ui_top_font_size()*2.f;
        F32 padding_px = ui_top_font_size()*1.f;
        Vec2F32 callee_helper_pos = {0};
        {
          UI_Box *anchor_box = ui_box_from_key(ws->autocomp_regs->ui_key);
          callee_helper_pos.x = anchor_box->rect.x0;
          callee_helper_pos.y = anchor_box->rect.y1;
        }
        F32 height_px_target = row_height_px*1.f + padding_px*2.f;
        autocomp_callee_helper_height_px = height_px_target * open_t;
        
        //- rjf: build top-level callee helper box
        UI_Box *callee_helper = &ui_nil_box;
        UI_FixedPos(callee_helper_pos)
          UI_Squish(0.1f-0.1f*open_t)
          UI_Transparency(1.f-open_t)
          UI_CornerRadius(ui_top_font_size()*0.25f)
          UI_PrefWidth(ui_children_sum(1))
          UI_PrefHeight(ui_px(height_px_target, 1.f))
        {
          callee_helper = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawDropShadow|
                                                    UI_BoxFlag_DrawBackgroundBlur|UI_BoxFlag_SquishAnchored|UI_BoxFlag_Clickable,
                                                    "top_level_window_callee_helper");
        }
        
        //- rjf: fill helper
        UI_Parent(callee_helper)
          UI_Padding(ui_px(padding_px, 1.f))
          UI_PrefWidth(ui_children_sum(1))
          UI_HeightFill
          UI_Column
          UI_PrefHeight(ui_px(row_height_px, 1.f))
          UI_Padding(ui_px(padding_px, 1.f))
        {
          // rjf: main name / args text
          UI_Row UI_TextPadding(0) UI_PrefWidth(ui_text_dim(0, 1)) RD_Font(RD_FontSlot_Code)
          {
            Vec4F32 code_default = ui_color_from_name(str8_lit("code_default"));
            String8 opener = push_str8f(scratch.arena, "%S(", type->name);
            rd_code_label(1, 0, code_default, opener);
            MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, type->name);
            B32 first = 1;
            UI_TagF(".") for(MD_NodePtrNode *n = schemas.first; n != 0; n = n->next)
            {
              for MD_EachNode(child, n->v->first)
              {
                if(md_node_has_tag(child, str8_lit("no_callee_helper"), 0))
                {
                  continue;
                }
                if(!first)
                {
                  rd_code_label(1, 0, code_default, str8_lit(", "));
                }
                first = 0;
                UI_Key arg_key = ui_key_from_stringf(ui_active_seed_key(), "###arg_%p", child);
                String8 arg_string = child->string;
                B32 is_optional = md_node_has_tag(child, str8_lit("optional"), 0);
                if(is_optional)
                {
                  arg_string = str8f(scratch.arena, "[%S=...]", child->string);
                }
                DR_FStrList arg_fstrs = rd_fstrs_from_code_string(scratch.arena, 1.f, 0, code_default, arg_string);
                if(child == ws->autocomp_cursor_info.arg_schema)
                {
                  ui_set_next_flags(UI_BoxFlag_DrawSideBottom);
                  ui_set_next_tag(str8_lit("good_pop"));
                }
                UI_Box *arg_box = ui_build_box_from_key(UI_BoxFlag_DrawText|UI_BoxFlag_Clickable|UI_BoxFlag_DrawHotEffects, arg_key);
                ui_box_equip_display_fstrs(arg_box, &arg_fstrs);
                UI_Signal arg_sig = ui_signal_from_box(arg_box);
                if(ui_hovering(arg_sig))
                {
                  String8 display_name = md_tag_from_string(child, str8_lit("display_name"), 0)->first->string;
                  String8 desc = md_tag_from_string(child, str8_lit("description"), 0)->first->string;
                  if(desc.size != 0)
                    UI_Tooltip RD_Font(RD_FontSlot_Main)
                  {
                    ui_state->tooltip_anchor_key = arg_box->key;
                    UI_Row
                    {
                      RD_Font(RD_FontSlot_Code) ui_label(child->string);
                      if(display_name.size != 0)
                      {
                        ui_spacer(ui_em(0.5f, 1.f));
                        UI_TagF("weak") ui_label(display_name);
                      }
                      if(is_optional)
                      {
                        ui_spacer(ui_em(0.5f, 1.f));
                        UI_TagF("weak") ui_labelf("(Optional)");
                      }
                    }
                    UI_TagF("weak") rd_label(desc);
                  }
                }
              }
            }
            rd_code_label(1, 0, code_default, str8_lit(")"));
          }
        }
        
        //- rjf: fall-through interactions with helper
        UI_Signal sig = ui_signal_from_box(callee_helper);
        (void)sig;
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part gather all tasks to build floating views
    //
    typedef struct FloatingViewTask FloatingViewTask;
    struct FloatingViewTask
    {
      FloatingViewTask *next;
      CFG_Node *view;
      UIShell_Regs *regs;
      Rng2F32 rect;
      B32 is_focused;
      B32 is_anchored;
      B32 force_inside_window_x;
      B32 force_inside_window_y;
      B32 only_secondary_navigation;
      B32 reset_open;
      UI_Signal signal; // NOTE(rjf): output, from build
      B32 pressed;
      B32 pressed_outside;
    };
    FloatingViewTask *autocomp_floating_view_task = 0;
    FloatingViewTask *hover_eval_floating_view_task = 0;
    FloatingViewTask *query_floating_view_task = 0;
    FloatingViewTask *first_floating_view_task = 0;
    FloatingViewTask *last_floating_view_task = 0;
    RD_Font(RD_FontSlot_Code)
    {
      //- rjf: add autocompletion view task
      if(ws->autocomp_regs != 0 && ws->autocomp_last_frame_index+1 >= rd_state->frame_index)
      {
        // rjf: build view
        CFG_Node *root = rd_immediate_cfg_from_keyf("autocomp_view_%I64x", window->id);
        CFG_Node *view = cfg_node_child_from_string_or_alloc(rd_state->cfg, root, str8_lit("watch"));
        cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("autocomplete"));
        CFG_Node *query = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("query"));
        CFG_Node *input = cfg_node_child_from_string_or_alloc(rd_state->cfg, query, str8_lit("input"));
        cfg_node_new_replace(rd_state->cfg, input, ws->autocomp_cursor_info.filter);
        CFG_Node *expr = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("expression"));
        cfg_node_new_replace(rd_state->cfg, expr, ws->autocomp_cursor_info.list_expr);
        
        // rjf: determine container size
        EV_BlockTree predicted_block_tree = {0};
        UIShell_RegsScope(.view = view->id, .tab = 0)
        {
          String8 expr = rd_expr_from_cfg(view);
          E_Eval list_eval = e_eval_from_string(expr);
          ev_key_set_expansion(rd_view_eval_view(), ev_key_root(), ev_key_make(ev_hash_from_key(ev_key_root()), 1), 1);
          predicted_block_tree = ev_block_tree_from_eval(scratch.arena, rd_view_eval_view(), rd_view_query_input(), list_eval);
        }
        F32 row_height_px = ui_top_px_height();
        U64 max_row_count = (U64)floor_f32(ui_top_font_size()*30.f / row_height_px);
        U64 needed_row_count = Min(max_row_count, predicted_block_tree.total_row_count - 1);
        F32 width_px = floor_f32(30.f*ui_top_font_size());
        F32 height_px = needed_row_count*row_height_px;
        
        // rjf: determine list top-level rect
        Rng2F32 rect = r2f32p(0, 0, 0, 0);
        if(!ui_key_match(ui_key_zero(), ws->autocomp_regs->ui_key))
        {
          UI_Box *anchor_box = ui_box_from_key(ws->autocomp_regs->ui_key);
          rect.x0 = anchor_box->rect.x0;
          rect.y0 = anchor_box->rect.y1 + autocomp_callee_helper_height_px;
          rect.x1 = rect.x0 + width_px;
          rect.y1 = rect.y0 + height_px;
        }
        
        // rjf: push task
        if(predicted_block_tree.total_row_count > 1)
        {
          FloatingViewTask *t = push_array(scratch.arena, FloatingViewTask, 1);
          SLLQueuePush(first_floating_view_task, last_floating_view_task, t);
          autocomp_floating_view_task = t;
          t->view          = view;
          t->rect          = rect;
          t->is_focused    = 1;
          t->is_anchored   = 1;
          t->only_secondary_navigation = 1;
        }
      }
      
      //- rjf: try to add hover eval
      {
        B32 build_hover_eval = (hover_eval_is_open && !rd_drag_is_active());
        
        // rjf: disable hover eval if hovered view is actively scrolling
        if(hover_eval_is_open)
        {
          for(CFG_PanelNode *panel = panel_tree.root;
              panel != &cfg_nil_panel_node;
              panel = cfg_panel_node_rec__depth_first_pre(panel_tree.root, panel).next)
          {
            if(panel->first != &cfg_nil_panel_node) { continue; }
            CFG_Node *tab = panel->selected_tab;
            if(tab != &cfg_nil_node)
            {
              RD_ViewState *vs = rd_view_state_from_cfg(tab);
              Rng2F32 panel_rect = cfg_target_rect_from_panel_node(content_rect, panel_tree.root, panel);
              if(contains_2f32(panel_rect, ui_mouse()) &&
                 (abs_f32(vs->scroll_pos.x.off) > 0.01f ||
                  abs_f32(vs->scroll_pos.y.off) > 0.01f))
              {
                build_hover_eval = 0;
                ws->hover_eval_firstt_us = rd_state->time_in_us;
              }
            }
          }
        }
        
        // rjf: choose hover evaluation expression
        String8 hover_eval_expr = ws->hover_eval_string;
        
        // rjf: evaluate hover evaluation expression, & determine if it evaluates
        // such that we want to build a hover eval.
        E_Eval hover_eval = e_eval_from_string(hover_eval_expr);
        {
          if(hover_eval.msgs.max_kind > E_MsgKind_Null)
          {
            build_hover_eval = 0;
          }
          else if(hover_eval.space.kind == RD_EvalSpaceKind_MetaCfg &&
                  rd_cfg_from_eval_space(hover_eval.space) == &cfg_nil_node)
          {
            build_hover_eval = 0;
          }
        }
        
        // rjf: request frames if we're waiting to open
        if(ws->hover_eval_string.size != 0 &&
           !hover_eval_is_open &&
           ws->hover_eval_lastt_us < ws->hover_eval_firstt_us+hover_eval_open_delay_us &&
           rd_state->time_in_us - ws->hover_eval_lastt_us < hover_eval_open_delay_us*2)
        {
          rd_request_frame();
        }
        
        // rjf: build hover eval task
        if(build_hover_eval)
        {
          // rjf: determine if we have a top-level visualizer
          EV_ExpandRule *expand_rule = ev_expand_rule_from_type_key(hover_eval.irtree.type_key);
          RD_ViewUIRule *view_ui_rule = rd_view_ui_rule_from_string(expand_rule->string);
          
          // rjf: build view
          CFG_Node *root = rd_immediate_cfg_from_keyf("hover_eval_view_%I64x", ws->cfg_id);
          CFG_Node *view = rd_view_from_eval(root, hover_eval);
          cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("explicit_root"));
          
          // rjf: determine size of hover evaluation container
          EV_BlockTree predicted_block_tree = {0};
          UIShell_RegsScope(.view = view->id, .tab = 0)
          {
            ev_key_set_expansion(rd_view_eval_view(), ev_key_root(), ev_key_make(ev_hash_from_key(ev_key_root()), 1), 1);
            predicted_block_tree = ev_block_tree_from_eval(scratch.arena, rd_view_eval_view(), str8_zero(), hover_eval);
          }
          F32 row_height_px = floor_f32(ui_top_font_size()*rd_setting_f32_from_name(str8_lit("row_height")));
          U64 max_row_count = 12;
          U64 needed_row_count = Min(max_row_count, predicted_block_tree.total_row_count);
          F32 width_px = floor_f32(70.f*ui_top_font_size());
          F32 height_px = needed_row_count*row_height_px;
          
          // rjf: if arbitrary visualizer, pick catchall size
          if(view_ui_rule != &rd_nil_view_ui_rule)
          {
            height_px = floor_f32(40.f*ui_top_font_size());
          }
          
          // rjf: determine hover eval top-level rect
          Rng2F32 rect = r2f32p(ws->hover_eval_spawn_pos.x,
                                ws->hover_eval_spawn_pos.y,
                                ws->hover_eval_spawn_pos.x + width_px,
                                ws->hover_eval_spawn_pos.y + height_px);
          
          // rjf: push hover eval task
          {
            FloatingViewTask *t = push_array(scratch.arena, FloatingViewTask, 1);
            SLLQueuePush(first_floating_view_task, last_floating_view_task, t);
            hover_eval_floating_view_task = t;
            t->view          = view;
            t->rect          = rect;
            t->is_focused    = ws->hover_eval_focused;
            t->is_anchored   = 1;
            t->force_inside_window_x = 1;
          }
        }
        
        // rjf: reset focus state if hover eval is not being built
        if(!build_hover_eval || ws->hover_eval_string.size == 0 || !hover_eval_is_open)
        {
          ws->hover_eval_focused = 0;
        }
      }
      
      //- rjf: force-close query, if it's anchored, but box is gone
      if(query_is_open)
      {
        UI_Box *box = ui_box_from_key(ws->query_regs->ui_key);
        if(!ui_key_match(ui_key_zero(), ws->query_regs->ui_key) && ui_box_is_nil(box))
        {
          query_is_open = 0;
          uishell_cmd("cancel_query");
        }
      }
      
      //- rjf: force-close query, if it has an expression, but that expression does not evaluate
      if(query_is_open)
      {
        String8 expr = ws->query_regs->expr;
        E_Eval eval = e_eval_from_string(expr);
        if(eval.msgs.max_kind > E_MsgKind_Null)
        {
          query_is_open = 0;
          uishell_cmd("cancel_query");
        }
        else if(eval.space.kind == RD_EvalSpaceKind_MetaCfg &&
                rd_cfg_from_eval_space(eval.space) == &cfg_nil_node)
        {
          query_is_open = 0;
          uishell_cmd("cancel_query");
        }
      }
      
      //- rjf: try to add opened query
      if(query_is_open)
      {
        // rjf: unpack view for query
        CFG_Node *root = rd_immediate_cfg_from_keyf("window_query_%p", window);
        CFG_Node *view = cfg_node_child_from_string_or_alloc(rd_state->cfg, root, str8_lit("watch"));
        CFG_Node *query = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("query"));
        B32 is_lister = (cfg_node_child_from_string(view, str8_lit("lister")) != &cfg_nil_node);
        B32 root_is_explicit = (cfg_node_child_from_string(view, str8_lit("explicit_root")) != &cfg_nil_node);
        RD_ViewState *vs = rd_view_state_from_cfg(view);
        
        // rjf: did this view ID change? -> reset open animation
        B32 reset_open = 0;
        if(view->id != ws->query_last_view_id)
        {
          ws->query_last_view_id = view->id;
          reset_open = 1;
        }
        
        // rjf: unpack query info
        String8 cmd_name = ws->query_regs->cmd_name;
        UIShell_AppCmdInfo cmd_info = uishell_app_cmd_info_from_string(cmd_name);
        String8 query_expr = ws->query_regs->expr;
        if(query_expr.size == 0 && cmd_name.size != 0)
        {
          query_expr = cmd_info.query_expr;
        }
        B32 query_is_anchored = (!ui_box_is_nil(ui_box_from_key(ws->query_regs->ui_key)));
        B32 size_query_by_expr_eval = (query_is_anchored || query_expr.size == 0);
        
        // rjf: compute query expression
        if(query_expr.size == 0)
        {
          query_expr = str8(vs->query_buffer, vs->query_string_size);
        }
        else
        {
          U64 input_insertion_pos = str8_find_needle(query_expr, 0, str8_lit("$input"), 0);
          if(input_insertion_pos < query_expr.size)
          {
            String8 pre_insertion  = str8_prefix(query_expr, input_insertion_pos);
            String8 post_insertion = str8_skip(query_expr, input_insertion_pos + 6);
            String8 input_text = str8(vs->query_buffer, vs->query_string_size);
            String8 input_text__escaped = escaped_from_raw_str8(scratch.arena, input_text);
            // TODO(rjf): @hack need to escape because this is putting the user's input
            // into a containing "folder:"..."" in all cases. but this is kinda shady
            // and should be replaced long-term with something more solid...
            query_expr = push_str8f(scratch.arena, "%S%S%S", pre_insertion, input_text__escaped, post_insertion);
          }
        }
        
        // rjf: store expression
        CFG_Node *expr = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("expression"));
        cfg_node_new_replace(rd_state->cfg, expr, query_expr);
        
        // rjf: evaluate query expression
        E_Eval query_eval = e_eval_from_string(query_expr);
        
        // rjf: determine & store row-height setting
        if(ws->query_regs->do_big_rows)
        {
          F32 row_height = 5.f;
          F32 row_height_px = row_height * ui_top_font_size();
          CFG_Node *row_height_root = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("row_height"));
          cfg_node_new_replacef(rd_state->cfg, row_height_root, "%f", row_height);
        }
        
        // rjf: compute query view's top-level rectangle
        Rng2F32 rect = {0};
        UIShell_RegsScope(.view = view->id, .tab = 0)
        {
          F32 row_height_px = ui_top_font_size() * rd_setting_f32_from_name(str8_lit("row_height"));
          Vec2F32 content_rect_center = center_2f32(content_rect);
          Vec2F32 content_rect_dim = dim_2f32(content_rect);
          ev_key_set_expansion(rd_view_eval_view(), ev_key_root(), ev_key_make(ev_hash_from_key(ev_key_root()), 1), 1);
          EV_BlockTree predicted_block_tree = ev_block_tree_from_eval(scratch.arena, rd_view_eval_view(), rd_view_query_input(), query_eval);
          F32 query_width_px = floor_f32(content_rect_dim.x * 0.35f);
          F32 max_query_height_px = content_rect_dim.y*0.8f;
          F32 query_height_px = max_query_height_px;
          if(size_query_by_expr_eval)
          {
            F32 search_row_open_t = ui_anim(ui_key_from_stringf(ui_key_zero(), "search_row_open_%p", view),
                                            (F32)!!vs->query_is_open,
                                            .initial = (F32)!!vs->query_is_open,
                                            .epsilon = 0.01f,
                                            .rate    = rd_state->menu_animation_rate);
            query_height_px = row_height_px * (predicted_block_tree.total_row_count - !root_is_explicit) + ui_top_px_height()*search_row_open_t;
            query_height_px = Min(query_height_px, max_query_height_px);
          }
          rect = r2f32p(content_rect_center.x - query_width_px/2,
                        content_rect_center.y - max_query_height_px/2.f,
                        content_rect_center.x + query_width_px/2,
                        content_rect_center.y - max_query_height_px/2.f + query_height_px);
          if(!ui_key_match(ui_key_zero(), ws->query_regs->ui_key))
          {
            UI_Box *anchor_box = ui_box_from_key(ws->query_regs->ui_key);
            if(anchor_box != &ui_nil_box)
            {
              rect.x0 = anchor_box->rect.x0 + ws->query_regs->off_px.x;
              rect.y0 = anchor_box->rect.y1 + ws->query_regs->off_px.y;
              rect.x1 = rect.x0 + ui_top_font_size()*60.f;
              rect.y1 = rect.y0 + query_height_px;
            }
          }
        }
        
        // rjf: push query task
        {
          FloatingViewTask *t = push_array(scratch.arena, FloatingViewTask, 1);
          SLLQueuePush(first_floating_view_task, last_floating_view_task, t);
          query_floating_view_task = t;
          t->view          = view;
          t->regs          = ws->query_regs;
          t->rect          = rect;
          t->is_focused    = 1;
          t->is_anchored   = query_is_anchored;
          t->reset_open    = reset_open;
          t->force_inside_window_x = 1;
          t->force_inside_window_y = 1;
        }
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part build all floating views
    //
    ProfScope("build all floating views")
      RD_Font(RD_FontSlot_Code)
      UI_TagF("floating")
      UI_Focus(ui_any_ctx_menu_is_open() || ws->menu_bar_focused ? UI_FocusKind_Off : UI_FocusKind_Null)
    {
      F32 fast_open_rate = rd_state->menu_animation_rate;
      F32 slow_open_rate = rd_state->menu_animation_rate__slow;
      for(FloatingViewTask *t = first_floating_view_task; t != 0; t = t->next)
      {
        // rjf: unpack
        CFG_Node *view      = t->view;    
        Rng2F32 rect      = t->rect;
        B32 is_focused    = t->is_focused;
        B32 is_anchored   = t->is_anchored;
        B32 only_secondary_navigation = t->only_secondary_navigation;
        F32 open_t        = ui_anim(ui_key_from_stringf(ui_key_zero(), "floating_view_open_%p", view), 1.f,
                                    .rate = is_anchored ? fast_open_rate : slow_open_rate,
                                    .reset = t->reset_open,
                                    .initial = 0.f);
        
        // rjf: force rect inside window if needed
        if(t->force_inside_window_x || t->force_inside_window_y)
        {
          B32 axis_mask[] = {t->force_inside_window_x, t->force_inside_window_y};
          Rng2F32 window_rect = wm_client_rect_from_window(ws->os);
          for EachEnumVal(Axis2, axis)
          {
            if(!axis_mask[axis]) { continue; }
            F32 max_delta = rect.p1.v[axis] - window_rect.p1.v[axis];
            F32 min_delta = window_rect.p0.v[axis] - rect.p0.v[axis];
            F32 total_delta = Max(min_delta, 0) - Max(max_delta, 0);
            rect.p0.v[axis] += total_delta;
            rect.p1.v[axis] += total_delta;
          }
        }
        
        // rjf: push view regs
        uishell_push_regs();
        {
          if(t->regs != 0)
          {
            uishell_regs()->cfg = t->regs->cfg;
          }
          uishell_regs()->view = view->id;
          String8 view_expr = rd_expr_from_cfg(view);
          String8 view_file_path = rd_file_path_from_eval_string(rd_frame_arena(), view_expr);
          // NOTE(rjf): we want to only fill out this view's file path slot if it
          // evaluates one - this way, a view can use the slot to know the selected
          // file path (if there is one). this is useful when pushing commandas which
          // apply to a cursor, for example.
          if(view_file_path.size != 0)
          {
            uishell_regs()->file_path = view_file_path;
          }
        }
        
        // rjf: build
        UI_Focus(is_focused ? UI_FocusKind_On : UI_FocusKind_Off)
          UI_PermissionFlags(only_secondary_navigation ?
                             UI_PermissionFlag_KeyboardSecondary|UI_PermissionFlag_Clicks|UI_PermissionFlag_ScrollX|UI_PermissionFlag_ScrollY :
                             UI_PermissionFlag_All)
        {
          // rjf: build top-level container box
          UI_Box *container = &ui_nil_box;
          UI_Rect(rect) UI_ChildLayoutAxis(Axis2_Y)
            UI_Squish(0.1f-0.1f*open_t)
            UI_Transparency(1.f-open_t)
            UI_CornerRadius(ui_top_font_size()*0.25f)
          {
            container = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                                  UI_BoxFlag_DrawBorder|
                                                  UI_BoxFlag_DrawBackground|
                                                  UI_BoxFlag_DrawBackgroundBlur|
                                                  UI_BoxFlag_RoundChildrenByParent|
                                                  UI_BoxFlag_DisableFocusOverlay|
                                                  UI_BoxFlag_DrawDropShadow|
                                                  (UI_BoxFlag_SquishAnchored*!!is_anchored),
                                                  "floating_view_container_%p", view);
          }
          
          // rjf: peek press inside/outside events
          {
            for(UI_Event *evt = 0; ui_next_event(&evt);)
            {
              if(evt->kind == UI_EventKind_Press &&
                 evt->key == WM_Key_LeftMouseButton)
              {
                if(contains_2f32(container->rect, evt->pos))
                {
                  t->pressed = 1;
                }
                else
                {
                  t->pressed_outside = 1;
                }
              }
            }
          }
          
          // rjf: build overlay container for loading animation
          UI_Box *loading_overlay_container = &ui_nil_box;
          UI_Parent(container) UI_WidthFill UI_HeightFill
          {
            loading_overlay_container = ui_build_box_from_key(UI_BoxFlag_Floating, ui_key_zero());
          }
          
          // rjf: build contents
          UI_Parent(container) UI_Focus(is_focused ? UI_FocusKind_Null : UI_FocusKind_Off)
          {
            ui_set_next_pref_width(ui_pct(1, 0));
            ui_set_next_pref_height(ui_pct(1, 0));
            ui_set_next_child_layout_axis(Axis2_Y);
            UI_Box *view_contents_container = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_Clip, "###view_contents_container");
            UI_Parent(view_contents_container) UI_WidthFill
            {
              rd_view_ui(rect);
            }
          }
          
          // rjf: build loading overlay
          {
            RD_ViewState *vs = rd_view_state_from_cfg(view);
            F32 loading_t = vs->loading_t;
            if(loading_t > 0.01f) UI_Parent(loading_overlay_container)
            {
              rd_loading_overlay(rect, loading_t, vs->loading_progress_v, vs->loading_progress_v_target);
            }
          }
          
          // rjf: interact with container
          UI_Signal sig = ui_signal_from_box(container);
          t->signal = sig;
        }
        
        // rjf: pop interaction registers; commit if this is focused
        UIShell_Regs *view_regs = uishell_pop_regs();
        if(is_focused)
        {
          MemoryCopyStruct(uishell_regs(), view_regs);
        }
        
        // rjf: is not anchored? -> darken rest of screen
        if(!is_anchored)
        {
          UI_TagF("inactive") UI_Transparency(1-open_t) UI_Rect(content_rect) ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Floating, ui_key_zero());
        }
        
        //- rjf: autocompletion view early-closing rules
        if(t == autocomp_floating_view_task)
        {
          B32 has_autocomplete_hint = ui_autocomplete_string().size != 0;
          B32 has_accept_operation = 0;
          for(UI_Event *evt = 0; ui_next_event(&evt);)
          {
            if(evt->kind == UI_EventKind_Press && evt->slot == UI_EventActionSlot_Accept)
            {
              has_accept_operation = 1;
              break;
            }
          }
          if(has_autocomplete_hint && has_accept_operation)
          {
            autocomp_floating_view_task->signal.box->fixed_position = v2f32(10000, 10000);
          }
        }
        
        //- rjf: hover eval focus rules
        if(t == hover_eval_floating_view_task)
        {
          UI_Signal sig = hover_eval_floating_view_task->signal;
          if(ui_pressed(sig) || hover_eval_floating_view_task->pressed)
          {
            ws->hover_eval_focused = 1;
          }
          if(ui_mouse_over(sig) || ws->hover_eval_focused)
          {
            ws->hover_eval_lastt_us = rd_state->time_in_us;
          }
          else if(ws->hover_eval_lastt_us+1000000 < rd_state->time_in_us)
          {
            rd_request_frame();
          }
          if(hover_eval_floating_view_task->pressed_outside || ui_slot_press(UI_EventActionSlot_Cancel))
          {
            ws->hover_eval_focused = 0;
            MemoryZeroStruct(&ws->hover_eval_string);
            arena_clear(ws->hover_eval_arena);
            rd_request_frame();
          }
        }
        
        //- rjf: query interactions
        if(t == query_floating_view_task)
        {
          CFG_Node *view = query_floating_view_task->view;
          RD_ViewState *vs = rd_view_state_from_cfg(query_floating_view_task->view);
          String8 cmd_name = ws->query_regs->cmd_name;
          UIShell_AppCmdInfo cmd_info = uishell_app_cmd_info_from_string(cmd_name);
          
          // rjf: close queries
          if(query_floating_view_task->pressed_outside ||
             (cfg_node_child_from_string(view, str8_lit("lister")) != &cfg_nil_node && !vs->query_is_open) ||
             (cmd_name.size != 0 && !vs->query_is_open) ||
             ui_slot_press(UI_EventActionSlot_Cancel))
          {
            uishell_cmd("cancel_query");
          }
          
          // rjf: any queries which take a file path mutate the shell's current path
          if(cmd_info.query_slot == UIShell_AppRegSlot_FilePath)
          {
            CFG_Node *query = cfg_node_child_from_string(view, str8_lit("query"));
            CFG_Node *input = cfg_node_child_from_string(query, str8_lit("input"));
            if(input != &cfg_nil_node)
            {
              String8 path_chopped = str8_chop_last_slash(input->first->string);
              CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
              CFG_Node *current_path = cfg_node_child_from_string_or_alloc(rd_state->cfg, user, str8_lit("current_path"));
              if(!str8_match(current_path->first->string, path_chopped, 0))
              {
                uishell_cmd("set_current_path", .file_path = path_chopped);
              }
            }
          }
        }
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part top bar
    //
    // tabs-in-title-bar (ADR-0006): gated on a free title-bar row (native menu)
    // + dev toggle. when on, the top bar yields its middle band so the
    // workspace's top-row tab strips render there; see the top-bar container
    // flags below & the panel-area raise/insets further down.
    // tabs in the title bar share the row with whatever menu form is present:
    // the native menu bar (mac — row is free), the compact kebab (trailing
    // niche), or the full owner-drawn menu bar (leading niche — tabs inset past
    // it). overflow when the bar + tabs are both wide is a future concern.
    B32 compact_menu_bar = rd_setting_b32_from_name(str8_lit("compact_menu_bar"));
    B32 tabs_in_title_bar = rd_setting_b32_from_name(str8_lit("tabs_in_title_bar"));
    ProfScope("build top bar")
    {
      F32 workspace_path_w = 0;
      B32 draw_custom_title_bar_controls = wm_window_should_draw_custom_title_bar_controls(ws->os);
      F32 native_title_bar_left_padding = wm_window_native_title_bar_left_padding(ws->os);
      B32 draw_self_menu_bar = !wm_application_menu_bar_is_native();

      //- rjf: chrome placement resolution (ADR-0006). resolves the title-bar &
      // sidebar-action chrome elements into niches, stored on ws so both the
      // title bar (below) and the control surface (later this frame) read the
      // same result. replaces the former 60em/80em gates. widths are measured
      // analytically & deliberately over-estimated, so resolution errs toward
      // shedding an element early rather than overlapping.
      {
        F32 font_size = ui_top_font_size();
        FNT_Tag ui_font  = rd_font_from_slot(RD_FontSlot_Main);
        FNT_Tag icon_font = rd_font_from_slot(RD_FontSlot_Icons);
        F32 bar_h = dim_2f32(top_bar_rect).y;
        F32 icon_button_w = font_size*2.25f; // flat icon buttons (new-workspace, overview)
        // Place the breadcrumb's right edge at the sidebar divider. Keep a
        // small readable slot when the sidebar is collapsed or very narrow.
        F32 leading = (native_title_bar_left_padding > 0 ? native_title_bar_left_padding : bar_h);
        F32 sidebar_right = content_rect.x0 + uishell_controlled_split_control_width_px(&root_controlled_split, content_rect);
        workspace_path_w = Max(font_size*8.f,
          floor_f32(sidebar_right - top_bar_rect.x0 - leading - icon_button_w*2.f));

        // menu bar: compact = a single kebab button; full = sum of menu-title
        // button widths. each button is sized by ui_text_dim(20,1), which
        // resolves to 20 + text_size + text_padding*2 (see UI_SizeKind_TextContent
        // in ui_core.c) — match that exactly so the tabs-in-title-bar leading
        // inset clears the bar (text_padding was previously omitted, so the
        // estimate ran short and tabs drew under the menu).
        F32 menu_button_pad = 20.f + ui_top_text_padding()*2.f;
        F32 menu_w = 0;
        if(compact_menu_bar)
        {
          menu_w = icon_button_w;
        }
        else
        {
          RD_AppMenuSpecList app_menus = rd_app_menu_specs();
          for(U64 idx = 0; idx < app_menus.count; idx += 1)
          {
            menu_w += fnt_dim_from_tag_size_string(ui_font, font_size, 0, 0, app_menus.v[idx].label).x;
            menu_w += menu_button_pad;
          }
        }

        // project selector: briefcase icon + project name + padding
        String8 project_name = {0};
        {
          CFG_Node *project = cfg_node_child_from_string(cfg_node_root(), str8_lit("project"));
          CFG_Node *name = cfg_node_child_from_string(project, str8_lit("name"));
          project_name = name->first->string;
          if(project_name.size == 0) { project_name = str8_skip_last_slash(str8_chop_last_dot(rd_state->project_path)); }
          if(project_name.size == 0) { project_name = str8_lit("Untitled Project"); }
        }
        F32 project_w = (fnt_dim_from_tag_size_string(icon_font, font_size, 0, 0, rd_icon_kind_text_table[RD_IconKind_Briefcase]).x +
                         fnt_dim_from_tag_size_string(ui_font, font_size, 0, 0, project_name).x +
                         font_size*2.f);

        // available title-bar width = bar width minus the platform-reserved ends
        // (leading traffic-lights / app icon, trailing window controls) & a margin
        F32 trailing = (draw_custom_title_bar_controls ? bar_h*3.f : 0.f);
        F32 title_bar_budget = dim_2f32(top_bar_rect).x - leading - trailing - font_size*2.f;

        // elements & chains. menu/project hide when they don't fit; the action
        // buttons relocate to the sidebar action row instead (never hidden).
        // priority = shed order when the title bar overflows (lowest first):
        // project hides, then overview & new-workspace relocate, then menu hides.
        RD_ChromeElement chrome_elements[RD_ChromeElementKind_COUNT];
        U64 chrome_element_count = 0;
        if(draw_self_menu_bar)
        {
          chrome_elements[chrome_element_count++] = (RD_ChromeElement){
            RD_ChromeElementKind_Menu, menu_w, 3,
            {RD_ChromeNiche_TitleBarMenu, RD_ChromeNiche_Hidden}, 2};
        }
        else
        {
          ws->chrome_niche[RD_ChromeElementKind_Menu] = RD_ChromeNiche_Hidden; // native menu bar
        }
        if(rd_setting_b32_from_name(str8_lit("show_project_selector")))
        {
          chrome_elements[chrome_element_count++] = (RD_ChromeElement){
            RD_ChromeElementKind_ProjectSelector, project_w, 0,
            {RD_ChromeNiche_TitleBarTrailing, RD_ChromeNiche_Hidden}, 2};
        }
        else
        {
          ws->chrome_niche[RD_ChromeElementKind_ProjectSelector] = RD_ChromeNiche_Hidden;
        }
        // sidebar-collapse lives only in the title bar — it can't sit in a
        // collapsed sidebar — & is the stickiest element (sheds last). its chain
        // terminates in its representation (TitleBarLeading), never Hidden, so it
        // can never be shed: the sidebar always stays re-openable, even when the
        // title bar is too narrow for everything (it clips rather than vanishes).
        chrome_elements[chrome_element_count++] = (RD_ChromeElement){
          RD_ChromeElementKind_SidebarCollapse, icon_button_w, 5,
          {RD_ChromeNiche_TitleBarLeading}, 1};
        chrome_elements[chrome_element_count++] = (RD_ChromeElement){
          RD_ChromeElementKind_NewWorkspace, icon_button_w, 2,
          {RD_ChromeNiche_TitleBarLeading, RD_ChromeNiche_SidebarActions}, 2};
        chrome_elements[chrome_element_count++] = (RD_ChromeElement){
          RD_ChromeElementKind_WorkspacePath, workspace_path_w, -1,
          {RD_ChromeNiche_TitleBarLeading, RD_ChromeNiche_Hidden}, 2};
        chrome_elements[chrome_element_count++] = (RD_ChromeElement){
          RD_ChromeElementKind_OverviewToggle, icon_button_w, 1,
          {RD_ChromeNiche_TitleBarTrailing, RD_ChromeNiche_SidebarActions}, 2};
        chrome_elements[chrome_element_count++] = (RD_ChromeElement){
          RD_ChromeElementKind_RevealWorkspace, icon_button_w, 1,
          {RD_ChromeNiche_TitleBarTrailing, RD_ChromeNiche_SidebarActions},
          cfg_node_child_from_string(window, str8_lit("control_split_collapsed")) != &cfg_nil_node ? 1 : 2};
        chrome_elements[chrome_element_count++] = (RD_ChromeElement){
          RD_ChromeElementKind_CloseWorkspace, icon_button_w, 1,
          {RD_ChromeNiche_TitleBarTrailing, RD_ChromeNiche_SidebarActions},
          cfg_node_child_from_string(window, str8_lit("control_split_collapsed")) != &cfg_nil_node ? 1 : 2};
        rd_chrome_resolve(chrome_elements, chrome_element_count, title_bar_budget, ws->chrome_niche);

        // pixel extent actually occupied at each end of the title bar, for the
        // tabs-in-title-bar inset (below): leading = decorations/icon + leading
        // buttons; trailing = trailing buttons + project selector + window
        // controls. a small gap keeps tabs off the buttons.
        F32 gap = font_size*0.5f;
        ws->chrome_leading_px = leading +
          (ws->chrome_niche[RD_ChromeElementKind_SidebarCollapse] == RD_ChromeNiche_TitleBarLeading ? icon_button_w : 0) +
          (ws->chrome_niche[RD_ChromeElementKind_NewWorkspace]    == RD_ChromeNiche_TitleBarLeading ? icon_button_w : 0) +
          (ws->chrome_niche[RD_ChromeElementKind_WorkspacePath]    == RD_ChromeNiche_TitleBarLeading ? workspace_path_w : 0) +
          // the full (owner-drawn) menu bar sits in the leading area; tabs inset past it
          (!compact_menu_bar && ws->chrome_niche[RD_ChromeElementKind_Menu] == RD_ChromeNiche_TitleBarMenu ? menu_w : 0) +
          gap;
        ws->chrome_trailing_px = trailing +
          (ws->chrome_niche[RD_ChromeElementKind_OverviewToggle]  == RD_ChromeNiche_TitleBarTrailing ? icon_button_w : 0) +
          (ws->chrome_niche[RD_ChromeElementKind_RevealWorkspace] == RD_ChromeNiche_TitleBarTrailing ? icon_button_w : 0) +
          (ws->chrome_niche[RD_ChromeElementKind_CloseWorkspace] == RD_ChromeNiche_TitleBarTrailing ? icon_button_w : 0) +
          (ws->chrome_niche[RD_ChromeElementKind_ProjectSelector] == RD_ChromeNiche_TitleBarTrailing ? project_w : 0) +
          // the compact (kebab) menu renders as the rightmost trailing element
          (compact_menu_bar && ws->chrome_niche[RD_ChromeElementKind_Menu] == RD_ChromeNiche_TitleBarMenu ? icon_button_w : 0) +
          gap;
      }

      wm_window_clear_custom_border_data(ws->os);
      wm_window_push_custom_edges(ws->os, window_edge_px);
      wm_window_push_custom_title_bar(ws->os, dim_2f32(top_bar_rect).y);
      UI_Focus((ws->menu_bar_focused && window_is_focused && !ui_any_ctx_menu_is_open()) ? UI_FocusKind_On : UI_FocusKind_Null)
        UI_TagF("menu_bar")
      {
        // when tabs are in the title bar, the top bar must not cover or capture
        // the middle band — the workspace's top-row tab strips render there & the
        // reverse-order draw puts the (earlier-built) top bar on top, so drop its
        // background/border/click-capture and keep only the end-zone clusters.
        // window drag in the bare band rides the WM custom title bar.
        UI_BoxFlags top_bar_flags = UI_BoxFlag_Clip|UI_BoxFlag_DefaultFocusNav|UI_BoxFlag_DisableFocusOverlay;
        if(!tabs_in_title_bar) { top_bar_flags |= UI_BoxFlag_Clickable|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground; }
        ui_set_next_child_layout_axis(Axis2_Y);
        ui_set_next_rect(top_bar_rect);
        UI_Box *top_bar_pane = ui_build_box_from_string(top_bar_flags, str8_lit("###top_bar"));
        UI_Parent(top_bar_pane) UI_PrefWidth(ui_pct(1, 0)) UI_WidthFill UI_Row UI_Focus(UI_FocusKind_Null)
      {
        UI_Key menu_bar_group_key = ui_key_from_string(ui_key_zero(), str8_lit("###top_bar_group"));
        MemoryZeroArray(ui_top_parent()->parent->corner_radii);
        
        //- rjf: left column
        {
          ui_set_next_flags(UI_BoxFlag_Clip|UI_BoxFlag_ViewScrollX|UI_BoxFlag_ViewClamp);
          ui_set_next_min_width(native_title_bar_left_padding);
          UI_WidthFill UI_NamedRow(str8_lit("###menu_bar"))
          {
            //- rjf: icon
            if(native_title_bar_left_padding > 0)
            {
              ui_spacer(ui_px(native_title_bar_left_padding, 1));
            }
            else UI_Padding(ui_em(0.5f, 1.f))
            {
              UI_PrefWidth(ui_px(dim_2f32(top_bar_rect).y - ui_top_font_size()*0.8f, 1.f))
                UI_Column
                UI_Padding(ui_em(0.4f, 1.f))
                UI_HeightFill
              {
                R_Handle texture = rd_state->icon_texture;
                Vec2S32 texture_dim = r_size_from_tex2d(texture);
                ui_image(texture, R_Tex2DSampleKind_Linear, r2f32p(0, 0, texture_dim.x, texture_dim.y), v4f32(1, 1, 1, 1), 0, str8_lit(""));
              }
            }

            //- rjf: leading buttons ("a") niche — elements resolved here (ADR-0006)
            if(ws->chrome_niche[RD_ChromeElementKind_SidebarCollapse] == RD_ChromeNiche_TitleBarLeading)
              UI_PrefWidth(ui_em(2.25f, 1.f)) UI_HeightFill
            {
              UI_Signal sig = rd_chrome_build_sidebar_collapse(root_controlled_split.owner_cfg);
              wm_window_push_custom_title_bar_client_area(ws->os, sig.box->rect);
            }
            if(ws->chrome_niche[RD_ChromeElementKind_NewWorkspace] == RD_ChromeNiche_TitleBarLeading)
              UI_PrefWidth(ui_em(2.25f, 1.f)) UI_HeightFill
            {
              UI_Signal sig = rd_chrome_build_new_workspace(root_controlled_split.owner_cfg);
              wm_window_push_custom_title_bar_client_area(ws->os, sig.box->rect);
            }
            if(ws->chrome_niche[RD_ChromeElementKind_WorkspacePath] == RD_ChromeNiche_TitleBarLeading)
              UI_PrefWidth(ui_px(workspace_path_w, 1.f)) UI_HeightFill
            {
              rd_chrome_build_workspace_path(root_controlled_split.owner_cfg, workspace_path_w);
            }
            //- menu items (full bar)
            if(ws->chrome_niche[RD_ChromeElementKind_Menu] == RD_ChromeNiche_TitleBarMenu && !compact_menu_bar)
            {
              ui_set_next_flags(UI_BoxFlag_DrawBackground);
              UI_PrefWidth(ui_children_sum(1)) UI_Row UI_PrefWidth(ui_text_dim(20, 1)) UI_GroupKey(menu_bar_group_key)
              {
                RD_AppMenuSpecList app_menus = rd_app_menu_specs();
                UI_Key menu_keys[16] = {0};
                Assert(app_menus.count <= ArrayCount(menu_keys));
                for(U64 menu_idx = 0; menu_idx < app_menus.count; menu_idx += 1)
                {
                  RD_AppMenuSpec *spec = &app_menus.v[menu_idx];
                  UI_Key menu_key = ui_key_from_stringf(ui_key_zero(), "_app_menu_%I64u_%S_", menu_idx, spec->label);
                  menu_keys[menu_idx] = menu_key;
                  UI_CtxMenu(menu_key) UI_PrefWidth(ui_em(50.f, 1.f)) UI_TagF("implicit")
                  {
                    rd_app_menu_spec_content(spec);
                  }
                }
                
                // rjf: buttons
                UI_TextAlignment(UI_TextAlign_Center) UI_HeightFill
                {
                  // rjf: determine if one of the menus is already open
                  B32 menu_open = 0;
                  U64 open_menu_idx = 0;
                  for(U64 idx = 0; idx < app_menus.count; idx += 1)
                  {
                    if(ui_ctx_menu_is_open(menu_keys[idx]))
                    {
                      menu_open = 1;
                      open_menu_idx = idx;
                      break;
                    }
                  }
                  
                  // rjf: navigate between menus
                  U64 open_menu_idx_prime = open_menu_idx;
                  if(menu_open && ws->menu_bar_focused && window_is_focused)
                  {
                    for(UI_Event *evt = 0; ui_next_event(&evt);)
                    {
                      B32 taken = 0;
                      if(evt->delta_2s32.x > 0)
                      {
                        taken = 1;
                        open_menu_idx_prime += 1;
                        open_menu_idx_prime = open_menu_idx_prime%app_menus.count;
                      }
                      if(evt->delta_2s32.x < 0)
                      {
                        taken = 1;
                        open_menu_idx_prime = open_menu_idx_prime > 0 ? open_menu_idx_prime-1 : (app_menus.count-1);
                      }
                      if(taken)
                      {
                        ui_eat_event(evt);
                      }
                    }
                  }
                  
                  // rjf: make ui
                  for(U64 idx = 0; idx < app_menus.count; idx += 1)
                  {
                    RD_AppMenuSpec *spec = &app_menus.v[idx];
                    ui_set_next_fastpath_codepoint(spec->codepoint);
                    B32 alt_fastpath_key = 0;
                    if(rd_setting_b32_from_name(str8_lit("focus_menu_bar_with_alt")) && ui_key_press(WM_Modifier_Alt, spec->key))
                    {
                      alt_fastpath_key = 1;
                    }
                    if((ws->menu_bar_key_held || ws->menu_bar_focused) && !ui_any_ctx_menu_is_open())
                    {
                      ui_set_next_flags(UI_BoxFlag_DrawTextFastpathCodepoint);
                    }
                    UI_Signal sig = rd_menu_bar_button(spec->label);
                    wm_window_push_custom_title_bar_client_area(ws->os, sig.box->rect);
                    if(menu_open)
                    {
                      if((ui_hovering(sig) && !ui_ctx_menu_is_open(menu_keys[idx])) || (open_menu_idx_prime == idx && open_menu_idx_prime != open_menu_idx))
                      {
                        ui_ctx_menu_open(menu_keys[idx], sig.box->key, v2f32(0, sig.box->rect.y1-sig.box->rect.y0));
                      }
                    }
                    else if(ui_pressed(sig) || alt_fastpath_key)
                    {
                      if(ui_ctx_menu_is_open(menu_keys[idx]))
                      {
                        ui_ctx_menu_close();
                      }
                      else
                      {
                        ui_ctx_menu_open(menu_keys[idx], sig.box->key, v2f32(0, sig.box->rect.y1-sig.box->rect.y0));
                      }
                    }
                  }
                }
              }
            }

          }
        }
        
        
        //- rjf: right column
        UI_WidthFill UI_Row
        {
          B32 do_user_prof = (ws->chrome_niche[RD_ChromeElementKind_ProjectSelector] == RD_ChromeNiche_TitleBarTrailing);

          ui_spacer(ui_pct(1, 0));

          //- rjf: trailing buttons ("b") niche — elements resolved here (ADR-0006)

          for(B32 close = 0; close < 2; close++)
          {
            RD_ChromeElementKind kind = close ? RD_ChromeElementKind_CloseWorkspace : RD_ChromeElementKind_RevealWorkspace;
            if(ws->chrome_niche[kind] == RD_ChromeNiche_TitleBarTrailing)
              UI_PrefWidth(ui_em(2.25f, 1.f)) UI_HeightFill
            {
              UI_Signal sig = rd_chrome_build_workspace_action(window, close);
              wm_window_push_custom_title_bar_client_area(ws->os, sig.box->rect);
            }
          }

          if(ws->chrome_niche[RD_ChromeElementKind_OverviewToggle] == RD_ChromeNiche_TitleBarTrailing)
            UI_PrefWidth(ui_em(2.25f, 1.f)) UI_HeightFill
          {
            UI_Signal sig = rd_chrome_build_overview_toggle(ws);
            wm_window_push_custom_title_bar_client_area(ws->os, sig.box->rect);
          }

          // rjf: loaded user viz
          if(0)
          {
            if(do_user_prof) UI_TagF("pop")
            {
              ui_set_next_pref_width(ui_children_sum(1));
              ui_set_next_child_layout_axis(Axis2_X);
              UI_Box *user_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                                           UI_BoxFlag_DrawBorder|
                                                           UI_BoxFlag_DrawBackground|
                                                           UI_BoxFlag_DrawHotEffects|
                                                           UI_BoxFlag_DrawActiveEffects,
                                                           "###loaded_user_button");
              wm_window_push_custom_title_bar_client_area(ws->os, user_box->rect);
              UI_Parent(user_box) UI_PrefWidth(ui_text_dim(10, 0)) UI_TextAlignment(UI_TextAlign_Center)
              {
                String8 user_path = rd_state->user_path;
                user_path = str8_chop_last_dot(user_path);
                RD_Font(RD_FontSlot_Icons)
                  UI_TextRasterFlags(rd_raster_flags_from_slot(RD_FontSlot_Icons))
                  ui_label(rd_icon_kind_text_table[RD_IconKind_Person]);
                ui_label(str8_skip_last_slash(user_path));
              }
              UI_Signal user_sig = ui_signal_from_box(user_box);
              if(ui_clicked(user_sig))
              {
                String8 cmd_name = UISHELL_APP_OPEN_USER_COMMAND_NAME();
                if(cmd_name.size != 0)
                {
                  uishell_cmd("run_command", .cmd_name = cmd_name);
                }
              }
            }
            
            if(do_user_prof)
            {
              ui_spacer(ui_em(0.75f, 0));
            }
          }
          
          // rjf: loaded project viz
          if(do_user_prof)
          {
            ui_set_next_pref_width(ui_children_sum(1));
            ui_set_next_child_layout_axis(Axis2_X);
            UI_Box *prof_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                                         UI_BoxFlag_DrawBorder|
                                                         UI_BoxFlag_DrawBackground|
                                                         UI_BoxFlag_DrawHotEffects|
                                                         UI_BoxFlag_DrawActiveEffects,
                                                         "###loaded_project_button");
            wm_window_push_custom_title_bar_client_area(ws->os, prof_box->rect);
            UI_Parent(prof_box) UI_PrefWidth(ui_text_dim(10, 0)) UI_TextAlignment(UI_TextAlign_Center) UI_Padding(ui_em(0.5f, 1.f))
            {
              CFG_Node *root = cfg_node_root();
              CFG_Node *project = cfg_node_child_from_string(root, str8_lit("project"));
              CFG_Node *name = cfg_node_child_from_string(project, str8_lit("name"));
              String8 project_name = name->first->string;
              if(project_name.size == 0)
              {
                String8 prof_path = rd_state->project_path;
                prof_path = str8_chop_last_dot(prof_path);
                project_name = str8_skip_last_slash(prof_path);
              }
              if(project_name.size == 0)
              {
                project_name = str8_lit("Untitled Project");
              }
              RD_Font(RD_FontSlot_Icons)
                ui_label(rd_icon_kind_text_table[RD_IconKind_Briefcase]);
              ui_label(project_name);
            }
            UI_Signal prof_sig = ui_signal_from_box(prof_box);
            if(ui_clicked(prof_sig))
            {
              String8 cmd_name = UISHELL_APP_OPEN_PROJECT_COMMAND_NAME();
              if(cmd_name.size != 0)
              {
                uishell_cmd("run_command", .cmd_name = cmd_name);
              }
            }
          }
          
          if(do_user_prof)
          {
            // ui_spacer(ui_em(2.f, 0));
          }

          // rjf: compact (kebab) app menu — rightmost shell chrome element. one
          // button opening a single drop-down that stacks every app menu as a
          // section (the UI has one ctx-menu slot, so it's flat, not nested).
          // composed from three drawn dots (the icon font has no kebab glyph).
          if(ws->chrome_niche[RD_ChromeElementKind_Menu] == RD_ChromeNiche_TitleBarMenu && compact_menu_bar)
          {
            UI_Key kebab_key = ui_key_from_string(ui_key_zero(), str8_lit("###app_menu_kebab"));
            UI_CtxMenu(kebab_key) UI_PrefWidth(ui_em(50.f, 1.f)) UI_TagF("implicit")
            {
              RD_AppMenuSpecList app_menus = rd_app_menu_specs();
              for(U64 menu_idx = 0; menu_idx < app_menus.count; menu_idx += 1)
              {
                RD_AppMenuSpec *spec = &app_menus.v[menu_idx];
                if(menu_idx != 0) { ui_spacer(ui_em(0.5f, 1.f)); }
                UI_TagF("weak") UI_TextAlignment(UI_TextAlign_Left) ui_label(spec->label);
                rd_app_menu_spec_content(spec);
              }
            }
            UI_PrefWidth(ui_em(2.25f, 1.f)) UI_HeightFill
            {
              ui_set_next_child_layout_axis(Axis2_Y);
              UI_Box *kebab_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                                           UI_BoxFlag_DrawHotEffects|
                                                           UI_BoxFlag_DrawActiveEffects,
                                                           "###app_menu_kebab_button");
              UI_Parent(kebab_box)
              {
                F32 dot = floor_f32(ui_top_font_size()*0.24f);
                F32 gap = floor_f32(ui_top_font_size()*0.2f);
                Vec4F32 dot_color = ui_color_from_name(str8_lit("text"));
                ui_spacer(ui_pct(1, 0));
                for(S32 dot_idx = 0; dot_idx < 3; dot_idx += 1)
                {
                  if(dot_idx != 0) { ui_spacer(ui_px(gap, 1.f)); }
                  UI_PrefWidth(ui_pct(1, 0)) UI_PrefHeight(ui_px(dot, 1.f)) UI_Row
                  {
                    ui_spacer(ui_pct(1, 0));
                    ui_set_next_pref_width(ui_px(dot, 1.f));
                    ui_set_next_pref_height(ui_px(dot, 1.f));
                    ui_set_next_background_color(dot_color);
                    UI_CornerRadius(dot*0.5f) ui_build_box_from_stringf(UI_BoxFlag_DrawBackground, "###kebab_dot_%i", dot_idx);
                    ui_spacer(ui_pct(1, 0));
                  }
                }
                ui_spacer(ui_pct(1, 0));
              }
              UI_Signal sig = ui_signal_from_box(kebab_box);
              wm_window_push_custom_title_bar_client_area(ws->os, sig.box->rect);
              if(ui_pressed(sig))
              {
                if(ui_ctx_menu_is_open(kebab_key)) { ui_ctx_menu_close(); }
                else { ui_ctx_menu_open(kebab_key, kebab_box->key, v2f32(0, dim_2f32(kebab_box->rect).y)); }
              }
            }
          }

          // rjf: close dropdown
          UI_Key close_ctx_menu_key = ui_key_from_stringf(ui_key_zero(), "###close_ctx_menu");
          UI_CtxMenu(close_ctx_menu_key) UI_TagF("implicit")
          {
            if(ui_clicked(rd_icon_buttonf(RD_IconKind_Window, 0, "Close Window")))
            {
              uishell_cmd("close_window");
            }
            if(ui_clicked(rd_icon_buttonf(RD_IconKind_X, 0, "Exit")))
            {
              uishell_cmd("exit");
            }
          }

          // rjf: min/max/close buttons
          if(draw_custom_title_bar_controls)
          {
            UI_Signal min_sig = {0};
            UI_Signal max_sig = {0};
            UI_Signal cls_sig = {0};
            Vec2F32 bar_dim = dim_2f32(top_bar_rect);
            F32 button_dim = floor_f32(bar_dim.y);
            UI_PrefWidth(ui_px(button_dim, 1.f))
              UI_FontSize(ui_top_font_size()*0.75f)
            {
              min_sig = rd_icon_buttonf(RD_IconKind_WindowMinimize,  0, "##minimize");
              max_sig = rd_icon_buttonf(wm_window_is_maximized(ws->os) ? RD_IconKind_WindowRestore : RD_IconKind_Window, 0, "##maximize");
            }
            UI_PrefWidth(ui_px(button_dim, 1.f))
              UI_TagF("bad_pop")
            {
              cls_sig = rd_icon_buttonf(RD_IconKind_X,      0, "##close");
            }
            if(ui_clicked(min_sig))
            {
              wm_window_set_minimized(ws->os, 1);
            }
            if(ui_clicked(max_sig))
            {
              wm_window_set_maximized(ws->os, !wm_window_is_maximized(ws->os));
            }
            if(ui_clicked(cls_sig))
            {
              if(ws->order_next != &rd_nil_window_state ||
                 ws->order_prev != &rd_nil_window_state)
              {
                ui_ctx_menu_open(close_ctx_menu_key, cls_sig.box->key, v2f32(0, dim_2f32(cls_sig.box->rect).y));
              }
              else
              {
                uishell_cmd("exit");
              }
            }
            wm_window_push_custom_title_bar_client_area(ws->os, min_sig.box->rect);
            wm_window_push_custom_title_bar_client_area(ws->os, max_sig.box->rect);
            wm_window_push_custom_title_bar_client_area(ws->os, pad_2f32(cls_sig.box->rect, 2.f));
          }
        }
      }
      }
    }

    ////////////////////////////
    //- rjf: @window_ui_part bottom bar
    //
    ProfScope("build bottom bar") if(show_status_bar)
    {
      String8 tag = str8_lit("pop");
      CFG_NodePtrList tasks = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("conversion_task"));
      CFG_NodePtrList long_running_tasks = {0};
      F32 alive_t_rate = 1 - pow_f32(2, (-5.f * rd_state->frame_dt));
      for(CFG_NodePtrNode *n = tasks.first; n != 0; n = n->next)
      {
        CFG_Node *task = n->v;
        F32 task_t = ui_anim(ui_key_from_stringf(ui_key_zero(), "task_anim_%I64u", task->id), 1.f, .rate = alive_t_rate);
        if(task_t > 0.5f)
        {
          cfg_node_ptr_list_push(scratch.arena, &long_running_tasks, task);
        }
      }
      if(rd_state->bind_change_active)
      {
        tag = str8_lit("pop");
      }
      else if(ws->error_t >= 0.01f && ws->error_string_size != 0)
      {
        tag = str8_lit("bad_pop");
      }
      //- rjf: compute fstrs for status explanation
      DR_FStrList status_fstrs = {0};
      {
        if(rd_state->bind_change_active)
        {
          UIShell_AppCmdInfo info = uishell_app_cmd_info_from_string(rd_state->bind_change_cmd_name);
          String8 display_name = rd_display_from_code_name(info.string);
          if(display_name.size == 0)
          {
            display_name = rd_state->bind_change_cmd_name;
          }
          String8 string = push_str8f(scratch.arena, "Currently rebinding \"%S\"", display_name);
          DR_FStrParams params = {ui_top_font(), ui_top_text_raster_flags(), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
          dr_fstrs_push_new(scratch.arena, &status_fstrs, &params, string);
        }
        else if(ws->error_t >= 0.01f && ws->error_string_size != 0)
        {
          String8 error_string = str8(ws->error_buffer, ws->error_string_size);
          ws->error_t -= rd_state->frame_dt/8.f;
          rd_request_frame();
          ui_set_next_pref_width(ui_children_sum(1));
          UI_CornerRadius(4)
            UI_Row
            UI_PrefWidth(ui_text_dim(10, 1))
            UI_TextAlignment(UI_TextAlign_Center)
          {
            DR_FStrList error_fstrs = rd_fstrs_from_rich_string(scratch.arena, error_string);
            DR_FStrParams params = {ui_top_font(), ui_top_text_raster_flags(), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
            dr_fstrs_push_new(scratch.arena, &status_fstrs, &params, rd_icon_kind_text_table[RD_IconKind_WarningBig],
                              .font = rd_font_from_slot(RD_FontSlot_Icons),
                              .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Icons));
            dr_fstrs_push_new(scratch.arena, &status_fstrs, &params, str8_lit("  "));
            dr_fstrs_concat_in_place(&status_fstrs, &error_fstrs);
          }
        }
      }
      
      //- rjf: build bottom bar
      UI_Flags(UI_BoxFlag_DrawBackground) UI_CornerRadius(0)
        UI_Tag(tag)
        UI_Pane(bottom_bar_rect, str8_lit("###bottom_bar")) UI_WidthFill UI_Row
        UI_Flags(0)
      {
        Temp scratch = scratch_begin(0, 0);
        
        // rjf: developer frame-time indicator
        if(DEV_updating_indicator)
        {
          F32 animation_t = pow_f32(sin_f32(rd_state->time_in_seconds/2.f), 2.f);
          ui_spacer(ui_em(0.3f, 1.f));
          ui_spacer(ui_em(1.5f*animation_t, 1.f));
          UI_PrefWidth(ui_text_dim(10, 1)) ui_labelf("*");
          ui_spacer(ui_em(1.5f*(1-animation_t), 1.f));
        }
        
        // rjf: build status
        UI_PrefWidth(ui_text_dim(10, 1))
        {
          ui_spacer(ui_em(1.f, 1.f));
          UI_Box *box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
          ui_box_equip_display_fstrs(box, &status_fstrs);
        }
        
        ui_spacer(ui_pct(1, 0));
        
        // rjf: version
        UI_FontSize(ui_top_font_size()*0.85f)
          UI_PrefWidth(ui_text_dim(10, 1))
          UI_TextAlignment(UI_TextAlign_Center)
        {
          ui_label(str8_lit(BUILD_TITLE_STRING_LITERAL));
        }
        
        scratch_end(scratch);
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part panel area
    //
    Access *workspace_theme_access = access_open();
    UI_Theme *selected_workspace_theme = rd_workspace_theme_from_cfg(scratch.arena,
                                                                     workspace_theme_access,
                                                                     workspace_mount->workspace_cfg,
                                                                     theme_color_cfgs,
                                                                     ws->theme);
    access_close(workspace_theme_access);

    Rng2F32 control_surface_rect = uishell_controlled_split_control_rect(&root_controlled_split, content_rect);
    uishell_control_surface_ui(control_surface_rect, &root_controlled_split);
    B32 control_split_is_changing = uishell_controlled_split_boundary_ui(&root_controlled_split, content_rect);
    if(control_split_is_changing)
    {
      ws->window_layout_reset = 1;
    }
    Rng2F32 workspace_rect = uishell_controlled_split_workspace_rect(&root_controlled_split, content_rect);

    //- rjf: tabs-in-title-bar (ADR-0006, gated on a free title-bar row = native
    // menu). the *main* workspace's top extends up into the title-bar band so its
    // top-row tab strips render there; the sidebar stays below the chrome. the
    // top-row tab strips are inset by the chrome end-zones — right always (the
    // workspace touches the window's right edge), left by however much the
    // leading chrome (decorations + leading buttons) overhangs the sidebar.
    // when the sidebar is wider than the leading chrome the strip already clears
    // it (inset 0); when collapsed the sidebar is 0-wide so the inset is the
    // full leading extent. previews & zoom builds are unaffected.
    // (tabs_in_title_bar computed up at the top bar.)
    Rng2F32 main_workspace_rect = workspace_rect;
    F32 main_tab_inset_left = 0.f;
    F32 main_tab_inset_right = 0.f;
    if(tabs_in_title_bar)
    {
      main_workspace_rect.p0.y = top_bar_rect.p0.y;
      main_tab_inset_right = ws->chrome_trailing_px;
      main_tab_inset_left = Max(0.f, ws->chrome_leading_px - main_workspace_rect.p0.x);
    }

    ws->workspace_surface_entry_count = 0;
    ws->active_workspace_surface_entry = 0; // 0 = direct (non-surface) render; set per-entry below
    //- animate the zoom transition; while open *or* in flight, children build
    // as zoom-mode surfaces & the composites interpolate
    {
      F32 zoom_target = (F32)!!ws->workspace_zoom_open;
      ws->workspace_zoom_t += rd_state->menu_animation_rate*(zoom_target - ws->workspace_zoom_t);
      if(abs_f32(ws->workspace_zoom_t - zoom_target) < 0.005f)
      {
        ws->workspace_zoom_t = zoom_target;
      }
      else
      {
        rd_request_frame();
      }
    }
    F32 workspace_zoom_t = ws->workspace_zoom_t;
    B32 workspace_zoom_open = (ws->workspace_zoom_open || workspace_zoom_t > 0.f);

    //- the workspace-content subrect of the (window-sized, padded) wrapper
    // surfaces; every consumer that presents a workspace preview crops with it,
    // so the transparent sidebar strip never rides along. main_workspace_rect is
    // the content region: it equals workspace_rect, except its top is raised into
    // the title-bar band when tabs-in-title-bar — so the crop includes the raised
    // tab strip (the tab strip is workspace content; window chrome that impinges
    // on the band is a separate composited layer & legitimately not in here).
    {
      Rng2F32 wrapper_surf_rect = pad_2f32(window_rect, 2.f);
      Vec2F32 wrapper_surf_dim = dim_2f32(wrapper_surf_rect);
      if(wrapper_surf_dim.x > 0 && wrapper_surf_dim.y > 0)
      {
        ws->workspace_content_uv = r2f32p((main_workspace_rect.x0 - wrapper_surf_rect.x0)/wrapper_surf_dim.x,
                                          (main_workspace_rect.y0 - wrapper_surf_rect.y0)/wrapper_surf_dim.y,
                                          (main_workspace_rect.x1 - wrapper_surf_rect.x0)/wrapper_surf_dim.x,
                                          (main_workspace_rect.y1 - wrapper_surf_rect.y0)/wrapper_surf_dim.y);
      }
    }
    // workspace surfaces build whenever something demands the previews: the
    // zoom view, or any consumer that registered a preview demand recently
    // (the control surface rows do, every frame they're visible) - no toggle
    B32 workspace_previews_demanded = 0;
    for(RD_WorkspacePreviewDemand *d = ws->first_workspace_preview_demand; d != 0; d = d->next)
    {
      if(d->width_pt > 0 && d->frame_index + 2 >= rd_state->frame_index)
      {
        workspace_previews_demanded = 1;
        break;
      }
    }
    B32 want_workspace_surfaces = (workspace_zoom_open || workspace_previews_demanded);
    ws->workspace_surface_entries = push_array(rd_frame_arena(), RD_WorkspaceSurfaceEntry, root_controlled_split.inventory.count + 1);
    if(want_workspace_surfaces && workspace_mount->owner_cfg != &cfg_nil_node)
    {
      //- visible workspace: builds through its surface & composites to the
      // stage as the normal UI. in the zoom view it instead becomes one
      // preview-scale child among the others below. wrappers are keyed by the
      // mount's owner id (a workspace cfg, or the window itself for the
      // implicit workspace) & span the window origin so the absolutely-
      // positioned panel boxes inside keep their coordinates
      if(!workspace_zoom_open)
      {
        U64 workspace_id = workspace_mount->owner_cfg->id;
        UI_Key wrapper_key = ui_key_from_stringf(ui_key_zero(), "###workspace_surface_%I64u", workspace_id);
        ui_set_next_rect(window_rect);
        UI_Box *wrapper = ui_build_box_from_key(UI_BoxFlag_RenderToSurface, wrapper_key);
        ws->workspace_surface_entries[ws->workspace_surface_entry_count] = (RD_WorkspaceSurfaceEntry){wrapper_key.u64[0], workspace_id, 1};
        ws->active_workspace_surface_entry = &ws->workspace_surface_entries[ws->workspace_surface_entry_count];
        ws->workspace_surface_entry_count += 1;
        rd_workspace_surface_contribute_version(selected_workspace_theme != 0 ? selected_workspace_theme->hash : 0);
        UI_ThemeScope(selected_workspace_theme) UI_Parent(wrapper)
        {
          rd_panel_area_ui(scratch, main_workspace_rect, window_rect, ws, workspace_mount, window_is_focused, query_is_open, main_tab_inset_left, main_tab_inset_right, tabs_in_title_bar);
        }
        ws->active_workspace_surface_entry = 0;
      }

      //- build the other Materialized children as ordinary container children
      // (per ADR-0005): everything in them runs; they render into their own
      // reduced-res surfaces (never composited to the stage), input-inert &
      // focus-off - not hidden, visible at preview scale (sidebar rows, zoom)
      for(UIShell_MaterializedWorkspace *child = root_controlled_split.inventory.first;
          child != 0 && ws->workspace_surface_entry_count < root_controlled_split.inventory.count + 1;
          child = child->next)
      {
        if(child->mount.owner_cfg == &cfg_nil_node ||
           (!workspace_zoom_open && (child->mount.owner_cfg == workspace_mount->owner_cfg ||
                                    !rd_workspace_preview_is_demanded(ws, child->id))))
        {
          continue;
        }
        UI_Key child_key = ui_key_from_stringf(ui_key_zero(), "###workspace_surface_%I64u", child->id);
        ui_set_next_rect(window_rect);
        UI_Box *child_wrapper = ui_build_box_from_key(UI_BoxFlag_RenderToSurface|UI_BoxFlag_IgnoreInteraction, child_key);
        // the zoom view's selected child renders at full resolution: its
        // composite interpolates to/from the full workspace presentation, &
        // sharing the normal wrapper's texture size makes the handoff seamless
        B32 child_full_res = (workspace_zoom_open && child->mount.owner_cfg == workspace_mount->owner_cfg);
        ws->workspace_surface_entries[ws->workspace_surface_entry_count] = (RD_WorkspaceSurfaceEntry){child_key.u64[0], child->id, 0, child_full_res};
        ws->active_workspace_surface_entry = &ws->workspace_surface_entries[ws->workspace_surface_entry_count];
        ws->workspace_surface_entry_count += 1;
        Access *child_workspace_theme_access = access_open();
        UI_Theme *child_workspace_theme = rd_workspace_theme_from_cfg(scratch.arena,
                                                                      child_workspace_theme_access,
                                                                      child->mount.workspace_cfg,
                                                                      theme_color_cfgs,
                                                                      ws->theme);
        access_close(child_workspace_theme_access);
        rd_workspace_surface_contribute_version(child_workspace_theme != 0 ? child_workspace_theme->hash : 0);
        UI_ThemeScope(child_workspace_theme) UI_Parent(child_wrapper) UI_Focus(UI_FocusKind_Off)
        {
          // render the same layout as the visible workspace (raised tab strip +
          // insets when tabs-in-title-bar) so every workspace's preview matches
          // how it presents when active. degrades to the plain workspace_rect
          // layout when the setting is off (main_workspace_rect == workspace_rect,
          // insets 0). the content_uv crop (below) follows the same rect.
          rd_panel_area_ui(scratch, main_workspace_rect, window_rect, ws, &child->mount, 0, 0, main_tab_inset_left, main_tab_inset_right, tabs_in_title_bar);
        }
        ws->active_workspace_surface_entry = 0;
      }

      //- zoom view: present every child as a clickable tile in the workspace
      // region; clicking a tile selects that workspace & zooms back in
      if(workspace_zoom_open)
      {
        U64 tile_count = 0;
        for(UIShell_MaterializedWorkspace *child = root_controlled_split.inventory.first; child != 0; child = child->next)
        {
          tile_count += (child->mount.owner_cfg != &cfg_nil_node);
        }
        if(tile_count != 0 && DEV_coverflow_overview)
        {
          //- cover flow: the same overview, projected as a row of 3d cards;
          // workspace surfaces are the card faces, a faded mirrored quad per
          // card reads as a floor reflection. row position eases toward the
          // selected child's index, so selecting a side card slides the row.
          U64 selected_idx = 0;
          {
            U64 idx_iter = 0;
            for(UIShell_MaterializedWorkspace *child = root_controlled_split.inventory.first; child != 0; child = child->next)
            {
              if(child->mount.owner_cfg == &cfg_nil_node) { continue; }
              if(child->mount.owner_cfg == workspace_mount->owner_cfg)
              {
                selected_idx = idx_iter;
              }
              idx_iter += 1;
            }
          }
          F32 cf_target = (F32)selected_idx;
          ws->workspace_coverflow_t += rd_state->menu_animation_rate*(cf_target - ws->workspace_coverflow_t);
          if(abs_f32(ws->workspace_coverflow_t - cf_target) < 0.005f)
          {
            ws->workspace_coverflow_t = cf_target;
          }
          else
          {
            rd_request_frame();
          }

          //- camera: distance fits the centered card; cards are unit-height,
          // window-aspect-width quads at the origin row
          Vec2F32 region_dim = dim_2f32(workspace_rect);
          // cards show the workspace-content subrect of the wrapper surfaces; the
          // card aspect & texcoords both come from it, so no transparent sidebar/
          // padding band rides along (it depth-killed whatever stacked behind it)
          Rng2F32 content_uv = ws->workspace_content_uv;
          F32 card_aspect = (region_dim.y > 0 ? region_dim.x/region_dim.y : 1.6f);
          F32 cw = card_aspect;
          F32 fov = rd_tweak_f32_range("coverflow_fov", 0.098558f, 0.02f, 0.2f); // NOTE: trig here is in TURNS (base_math convention): 0.10 = 36 degrees
          F32 region_aspect = (region_dim.y > 0 ? region_dim.x/region_dim.y : 1.6f);
          F32 eye_z = (cw*rd_tweak_f32_range("coverflow_zoom_fit", 1.2239f, 0.3f, 2.f))/tan_f32(fov*0.5f);
          F32 row_y = rd_tweak_f32_range("coverflow_row_y", 0.494243f, 0.f, 1.5f);
          // NOTE: the view is a bare translation - camera on the -z side at
          // (0, cam_y, -eye_z), +x right, +y up, scene receding toward +z, which
          // is what make_perspective_4x4f32 expects (w' = +z). the inherited
          // make_look_at_4x4f32 builds its basis from eye-minus-center & rotates
          // the scene 180 degrees; an axis-aligned camera needs none of it
          F32 cam_y = row_y - rd_tweak_f32_range("coverflow_cam_y_offset", 0.027191f, -0.5f, 0.5f);
          Mat4x4F32 view = make_translate_4x4f32(v3f32(0, -cam_y, eye_z));
          Mat4x4F32 projection = make_perspective_4x4f32(fov, region_aspect, 0.1f, 100.f);
          Mat4x4F32 proj_view = mul_4x4f32(projection, view);

          //- collect cards & their transforms
          RD_CoverFlowDrawData *cf_data = push_array(ui_build_arena(), RD_CoverFlowDrawData, 1);
          cf_data->view = view;
          cf_data->projection = projection;
          rd_coverflow_meshes(&cf_data->card_vertices, &cf_data->refl_vertices, &cf_data->quad_indices);
          cf_data->tex_remap = v4f32(content_uv.x0, content_uv.y0,
                                     content_uv.x1 - content_uv.x0, content_uv.y1 - content_uv.y0);
          cf_data->cards = push_array(ui_build_arena(), RD_CoverFlowCard, tile_count);
          F32 cf_center_gap = rd_tweak_f32_range("coverflow_center_gap", 0.62f, 0.2f, 1.2f);
          F32 cf_deck_gap   = rd_tweak_f32_range("coverflow_deck_gap", 0.262894f, 0.05f, 0.8f);
          F32 cf_center_z   = rd_tweak_f32_range("coverflow_center_z", 1.0f, 0.f, 3.f);
          F32 cf_deck_z     = rd_tweak_f32_range("coverflow_deck_z", 0.322589f, 0.f, 0.5f);
          F32 cf_tilt       = rd_tweak_f32_range("coverflow_tilt", 0.11125f, 0.f, 0.25f);
          F32 cf_refl_gap   = rd_tweak_f32_range("coverflow_refl_gap", 0.2f, 0.f, 0.5f);
          {
            U64 card_idx = 0;
            for(UIShell_MaterializedWorkspace *child = root_controlled_split.inventory.first; child != 0; child = child->next)
            {
              if(child->mount.owner_cfg == &cfg_nil_node) { continue; }
              UI_Key wrapper_key = ui_key_from_stringf(ui_key_zero(), "###workspace_surface_%I64u", child->id);
              RD_SurfaceCacheNode *node = rd_window_surface_node_lookup(ws, wrapper_key.u64[0]);
              //- apple-style layout (ratios from surveyed implementations): rotation
              // completes by half a step then clamps, so every side card shares one
              // tilt; x has a wide center gap then a tight deck; z dips in the center
              // zone & recedes slightly per deck card so the depth buffer reproduces
              // the center-on-top stacking css implementations fake with z-index
              F32 p = (F32)card_idx - ws->workspace_coverflow_t;
              F32 pc = Clamp(-1.f, p, 1.f);
              F32 rot_t = Clamp(-1.f, p*2.f, 1.f);
              F32 x = pc*cw*cf_center_gap + (abs_f32(p) > 1.f ? (p - pc)*cw*cf_deck_gap : 0.f);
              F32 z = abs_f32(pc)*cf_center_z + (abs_f32(p) > 1.f ? (abs_f32(p) - 1.f)*cf_deck_z : 0.f);
              F32 ry = rot_t*cf_tilt; // turns, ~50 degrees: outer edge toward the camera, inner edge tucking behind the next card inward (the classic stack)
              Mat4x4F32 xform = mul_4x4f32(make_translate_4x4f32(v3f32(x, row_y, z)),
                                           mul_4x4f32(make_rotate_4x4f32(v3f32(0, 1, 0), ry),
                                                      make_scale_4x4f32(v3f32(cw, 1.f, 1.f))));
              Mat4x4F32 refl_xform = mul_4x4f32(xform, make_translate_4x4f32(v3f32(0, -1.f - cf_refl_gap, 0)));
              rd_workspace_preview_demand_push(ws, child->id, region_dim.x*(abs_f32(p) < 0.5f ? 0.55f : 0.25f));
              cf_data->cards[card_idx].workspace_id = child->id;
              cf_data->cards[card_idx].texture = (node != 0 ? node->texture : r_handle_zero());
              cf_data->cards[card_idx].xform = xform;
              cf_data->cards[card_idx].refl_xform = refl_xform;
              cf_data->cards[card_idx].row_p = p;
              card_idx += 1;
            }
            cf_data->card_count = card_idx;
          }

          //- far-to-near draw order: row_p is monotonic across the cards array, so
          // |p|-descending order is a two-pointer walk in from both ends
          cf_data->draw_order = push_array(ui_build_arena(), U64, cf_data->card_count);
          {
            U64 lo = 0;
            U64 hi = (cf_data->card_count > 0 ? cf_data->card_count - 1 : 0);
            for(U64 order_idx = 0; order_idx < cf_data->card_count; order_idx += 1)
            {
              if(abs_f32(cf_data->cards[lo].row_p) >= abs_f32(cf_data->cards[hi].row_p))
              {
                cf_data->draw_order[order_idx] = lo;
                lo += 1;
              }
              else
              {
                cf_data->draw_order[order_idx] = hi;
                hi -= 1;
              }
            }
          }

          //- the scene box: backdrop + custom draw + input
          ui_set_next_rect(workspace_rect);
          UI_Box *cf_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_Scroll|UI_BoxFlag_DrawBackground, "###workspace_coverflow");
          ui_box_equip_custom_draw(cf_box, rd_workspace_coverflow_box_draw, cf_data);
          UI_Signal cf_sig = ui_signal_from_box(cf_box);
          S64 step = 0;
          if(ui_clicked(cf_sig))
          {
            //- hit test: project each card's corners through the same matrices the
            // mesh shader uses; among hits, the center-most card wins (matches the
            // visual stacking, where side cards tuck behind)
            Vec2F32 mouse = ui_mouse();
            U64 best_idx = max_U64;
            F32 best_abs_p = 0;
            for(U64 idx = 0; idx < cf_data->card_count; idx += 1)
            {
              Vec2F32 corners_screen[4];
              B32 in_front = 1;
              Vec4F32 corners_local[4] =
              {
                v4f32(-0.5f,  0.5f, 0, 1),
                v4f32( 0.5f,  0.5f, 0, 1),
                v4f32( 0.5f, -0.5f, 0, 1),
                v4f32(-0.5f, -0.5f, 0, 1),
              };
              for(U64 corner_idx = 0; corner_idx < 4; corner_idx += 1)
              {
                Vec4F32 world = rd_coverflow_transform(&cf_data->cards[idx].xform, corners_local[corner_idx]);
                Vec4F32 clip = rd_coverflow_transform(&proj_view, world);
                if(clip.w <= 0.001f) { in_front = 0; break; }
                Vec2F32 ndc = v2f32(clip.x/clip.w, clip.y/clip.w);
                corners_screen[corner_idx] = v2f32(workspace_rect.x0 + (ndc.x*0.5f + 0.5f)*region_dim.x,
                                                   workspace_rect.y0 + (0.5f - ndc.y*0.5f)*region_dim.y);
              }
              if(!in_front) { continue; }
              B32 hit = 1;
              F32 sign_ref = 0;
              for(U64 edge_idx = 0; edge_idx < 4; edge_idx += 1)
              {
                Vec2F32 a = corners_screen[edge_idx];
                Vec2F32 b = corners_screen[(edge_idx + 1)%4];
                F32 cross = (b.x - a.x)*(mouse.y - a.y) - (b.y - a.y)*(mouse.x - a.x);
                if(edge_idx == 0) { sign_ref = cross; }
                else if(cross*sign_ref < 0) { hit = 0; break; }
              }
              if(hit)
              {
                F32 abs_p = abs_f32((F32)idx - ws->workspace_coverflow_t);
                if(best_idx == max_U64 || abs_p < best_abs_p)
                {
                  best_idx = idx;
                  best_abs_p = abs_p;
                }
              }
            }
            if(best_idx != max_U64)
            {
              if(best_idx == selected_idx)
              {
                ws->workspace_zoom_open = 0;
              }
              else
              {
                step = (S64)best_idx - (S64)selected_idx;
              }
            }
          }
          if(cf_sig.scroll.y != 0 || cf_sig.scroll.x != 0)
          {
            S16 scroll_amt = (cf_sig.scroll.x != 0 ? cf_sig.scroll.x : cf_sig.scroll.y);
            step += (scroll_amt > 0 ? +1 : -1);
          }
          if(ui_key_press(0, WM_Key_Left))  { step -= 1; }
          if(ui_key_press(0, WM_Key_Right)) { step += 1; }
          if(step != 0)
          {
            S64 next_idx = Clamp(0, (S64)selected_idx + step, (S64)tile_count - 1);
            U64 idx_iter = 0;
            for(UIShell_MaterializedWorkspace *child = root_controlled_split.inventory.first; child != 0; child = child->next)
            {
              if(child->mount.owner_cfg == &cfg_nil_node) { continue; }
              if(idx_iter == (U64)next_idx)
              {
                if(child->mount.owner_cfg != workspace_mount->owner_cfg)
                {
                  uishell_cmd("select_workspace", .window = root_controlled_split.owner_cfg->id, .cfg = child->id);
                }
                break;
              }
              idx_iter += 1;
            }
          }
        }
        else if(tile_count != 0)
        {
          F32 pad = floor_f32(ui_top_font_size()*1.f);
          F32 label_h = floor_f32(ui_top_font_size()*1.5f);
          U64 grid_cols = 1;
          for(; grid_cols*grid_cols < tile_count; grid_cols += 1) {}
          U64 grid_rows = (tile_count + grid_cols - 1)/grid_cols;
          Vec2F32 region_dim = dim_2f32(workspace_rect);
          F32 cell_w = (region_dim.x - pad*(grid_cols+1))/(F32)grid_cols;
          F32 cell_h = (region_dim.y - pad*(grid_rows+1))/(F32)grid_rows;
          F32 aspect = (region_dim.y > 0 ? region_dim.x/region_dim.y : 1.6f);
          //- two passes for z-order during the transition: the selected child's
          // composite interpolates between the full window & its tile, & must
          // draw over the others, which fade with zoom_t
          for(U64 tile_pass = 0; tile_pass < 2; tile_pass += 1)
          {
            U64 tile_idx = 0;
            for(UIShell_MaterializedWorkspace *child = root_controlled_split.inventory.first; child != 0; child = child->next)
            {
              if(child->mount.owner_cfg == &cfg_nil_node)
              {
                continue;
              }
              U64 col = tile_idx%grid_cols;
              U64 row = tile_idx/grid_cols;
              tile_idx += 1;
              B32 selected = (child->mount.owner_cfg == workspace_mount->owner_cfg);
              if((tile_pass == 1) != (selected != 0))
              {
                continue;
              }
              Vec2F32 cell_p0 =
              {
                workspace_rect.x0 + pad + (F32)col*(cell_w + pad),
                workspace_rect.y0 + pad + (F32)row*(cell_h + pad),
              };
              F32 img_w = cell_w;
              F32 img_h = floor_f32(img_w/aspect);
              if(img_h > cell_h - label_h)
              {
                img_h = cell_h - label_h;
                img_w = floor_f32(img_h*aspect);
              }
              Rng2F32 img_rect = r2f32p(cell_p0.x + floor_f32((cell_w - img_w)*0.5f),
                                        cell_p0.y,
                                        cell_p0.x + floor_f32((cell_w - img_w)*0.5f) + img_w,
                                        cell_p0.y + img_h);
              Rng2F32 tile_rect = img_rect;
              if(selected && workspace_zoom_t < 1.f)
              {
                // tiles show the cropped workspace content (workspace_content_uv =
                // main_workspace_rect), so the transition's full-zoom anchor must
                // be that same region — which includes the raised tab band under
                // tabs-in-title-bar. anchoring to workspace_rect (no band) drew the
                // band tabs into the client area mid-zoom, momentarily reverting
                // out of tabs-in-title-bar. (main_workspace_rect == workspace_rect
                // when tabs-in-title-bar is off, so this degrades cleanly.)
                tile_rect.p0 = mix_2f32(main_workspace_rect.p0, img_rect.p0, workspace_zoom_t);
                tile_rect.p1 = mix_2f32(main_workspace_rect.p1, img_rect.p1, workspace_zoom_t);
              }
              Rng2F32 label_rect = r2f32p(img_rect.x0, img_rect.y1, img_rect.x1, img_rect.y1 + label_h);
              rd_workspace_preview_demand_push(ws, child->id, img_w);
              F32 tile_transparency = (selected ? 0.f : 1.f - workspace_zoom_t);
              UI_TagF("tab") UI_TagF(!selected ? "inactive" : "") UI_Transparency(tile_transparency)
              {
                ui_set_next_rect(tile_rect);
                UI_Box *tile_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|
                                                             UI_BoxFlag_DrawBackground|
                                                             UI_BoxFlag_DrawBorder|
                                                             UI_BoxFlag_DrawHotEffects,
                                                             "###workspace_tile_%I64u", child->id);
                UI_Key wrapper_key = ui_key_from_stringf(ui_key_zero(), "###workspace_surface_%I64u", child->id);
                RD_SurfaceCacheNode *node = rd_window_surface_node_lookup(ws, wrapper_key.u64[0]);
                if(node != 0)
                {
                  RD_WorkspacePreviewDraw *tile_draw = push_array(ui_build_arena(), RD_WorkspacePreviewDraw, 1);
                  tile_draw->node = node;
                  tile_draw->src_uv = ws->workspace_content_uv; // crop the transparent sidebar band
                  ui_box_equip_custom_draw(tile_box, rd_workspace_preview_box_draw, tile_draw);
                }
                UI_Signal tile_sig = ui_signal_from_box(tile_box);
                if(ui_clicked(tile_sig))
                {
                  if(!selected)
                  {
                    uishell_cmd("select_workspace", .window = root_controlled_split.owner_cfg->id, .cfg = child->id);
                  }
                  ws->workspace_zoom_open = 0;
                }
                ui_set_next_rect(label_rect);
                UI_TextAlignment(UI_TextAlign_Center) UI_Transparency(1.f - workspace_zoom_t)
                {
                  UI_Box *label_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "###workspace_tile_label_%I64u", child->id);
                  ui_box_equip_display_string(label_box, child->display_name);
                }
              }
            }
          }
        }
      }
    }
    else
    {
      // direct build (no preview surfaces demanded — e.g. sidebar collapsed):
      // gets the same tabs-in-title-bar raise/insets as the surface path above
      UI_ThemeScope(selected_workspace_theme)
      {
        rd_panel_area_ui(scratch, main_workspace_rect, window_rect, ws, workspace_mount, window_is_focused, query_is_open, main_tab_inset_left, main_tab_inset_right, tabs_in_title_bar);
      }
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part drag/drop cancelling
    //
    if(rd_drag_is_active() && ui_slot_press(UI_EventActionSlot_Cancel))
    {
      rd_drag_kill();
      ui_kill_action();
    }

    ////////////////////////////
    //- @window_ui_part workspace zoom view cancelling
    //
    if(ws->workspace_zoom_open && ui_slot_press(UI_EventActionSlot_Cancel))
    {
      ws->workspace_zoom_open = 0;
      rd_request_frame();
    }
    
    ////////////////////////////
    //- rjf: @window_ui_part top-level font size changing
    //
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      if(evt->kind == UI_EventKind_Scroll && evt->modifiers == WM_Modifier_Ctrl)
      {
        ui_eat_event(evt);
        if(evt->delta_2f32.y < 0)
        {
          uishell_cmd("inc_window_font_size");
        }
        else if(evt->delta_2f32.y > 0)
        {
          uishell_cmd("dec_window_font_size");
        }
      }
    }
    
    ui_end_build();
  }
  
  //////////////////////////////
  //- rjf: @window_frame_part hover eval cancelling
  //
  if(ws->hover_eval_string.size != 0 && ui_slot_press(UI_EventActionSlot_Cancel))
  {
    MemoryZeroStruct(&ws->hover_eval_string);
    arena_clear(ws->hover_eval_arena);
    ws->hover_eval_focused = 0;
    rd_request_frame();
  }
  
  //////////////////////////////
  //- rjf: @window_frame_part animate
  //
  if(ui_animating_from_state(ws->ui))
  {
    rd_request_frame();
  }
  
  //////////////////////////////
  //- rjf: @window_frame_part draw UI
  //
  ws->draw_bucket = dr_bucket_make();
  DR_BucketScope(ws->draw_bucket)
    ProfScope("draw UI")
  {
    Temp scratch = scratch_begin(0, 0);
    F32 box_squish_epsilon = 0.001f;
    Rng2F32 window_rect = wm_client_rect_from_window(ws->os);
    
    //- rjf: unpack settings
    F32 rounded_corner_amount = rd_setting_f32_from_name(str8_lit("rounded_corner_amount"));
    F32 border_softness = 1.f;
    // Thin outlines need a one-backing-pixel antialiasing transition. Keep
    // background softness independent: using its two-point transition for a
    // one-point stroke spreads both edges together and weakens the border.
    F32 stroke_softness = 0.5f/Max(1.f, wm_backing_scale_from_window(ws->os));
    B32 do_background_blur = rd_setting_b32_from_name(str8_lit("background_blur"));
    B32 force_opaque_floating_backgrounds = rd_setting_b32_from_name(str8_lit("opaque_backgrounds"));
    B32 do_drop_shadows = 
      rd_setting_b32_from_name(str8_lit("drop_shadows"));
    Vec4F32 base_background_color = ui_color_from_name(str8_lit("background"));
    Vec4F32 base_border_color = ui_color_from_name(str8_lit("border"));
    Vec4F32 drop_shadow_color = ui_color_from_name(str8_lit("drop_shadow"));
    
    //- rjf: set up heatmap buckets
    F32 heatmap_bucket_size = 32.f;
    U64 *heatmap_buckets = 0;
    U64 heatmap_bucket_pitch = 0;
    U64 heatmap_bucket_count = 0;
    if(DEV_draw_ui_box_heatmap)
    {
      Rng2F32 rect = wm_client_rect_from_window(ws->os);
      Vec2F32 size = dim_2f32(rect);
      Vec2S32 buckets_dim = {(S32)(size.x/heatmap_bucket_size), (S32)(size.y/heatmap_bucket_size)};
      heatmap_bucket_pitch = buckets_dim.x;
      heatmap_bucket_count = buckets_dim.x*buckets_dim.y;
      heatmap_buckets = push_array(scratch.arena, U64, heatmap_bucket_count);
    }
    
    //- rjf: draw background color
    {
      dr_rect(wm_client_rect_from_window(ws->os), base_background_color, 0, 0, 0);
    }
    
    //- rjf: draw window border
    {
      dr_rect(wm_client_rect_from_window(ws->os), base_border_color, 0, 1.f, border_softness*0.5f);
    }
    
    //- rjf: recurse & draw
    U64 total_heatmap_sum_count = 0;
    UI_Box *hover_debug_box = &ui_nil_box;
    UI_Box *surface_box_stack[8] = {0};
    RD_SurfaceCacheNode *surface_node_stack[8] = {0};
    B32 surface_child_changed_stack[8] = {0};
    U64 surface_box_count = 0;
    for(UI_Box *box = ui_root_from_state(ws->ui); !ui_box_is_nil(box);)
    {
      // rjf: get corner radii
      F32 box_corner_radii[Corner_COUNT] =
      {
        box->corner_radii[Corner_00]*rounded_corner_amount,
        box->corner_radii[Corner_01]*rounded_corner_amount,
        box->corner_radii[Corner_10]*rounded_corner_amount,
        box->corner_radii[Corner_11]*rounded_corner_amount,
      };
      
      // rjf: get recursion
      UI_BoxRec rec = ui_box_rec_df_post(box, &ui_nil_box);
      
      // rjf: sum to box heatmap
      if(DEV_draw_ui_box_heatmap)
      {
        Vec2F32 center = center_2f32(box->rect);
        Vec2S32 p = v2s32(center.x / heatmap_bucket_size, center.y / heatmap_bucket_size);
        U64 bucket_idx = p.y * heatmap_bucket_pitch + p.x;
        if(bucket_idx < heatmap_bucket_count)
        {
          heatmap_buckets[bucket_idx] += 1;
          total_heatmap_sum_count += 1;
        }
      }
      
      // rjf: grab if debug
      if(box->flags & UI_BoxFlag_Debug && contains_2f32(box->rect, ui_mouse()))
      {
        hover_debug_box = box;
      }
      
      // rjf: push transparency
      if(box->transparency != 0)
      {
        dr_push_transparency(box->transparency);
      }
      
      // rjf: push squish
      if(box->squish > box_squish_epsilon)
      {
        Vec2F32 box_dim = dim_2f32(box->rect);
        Vec2F32 anchor_off = {0};
        if(box->flags & UI_BoxFlag_SquishAnchored)
        {
          anchor_off.x = box_dim.x/2.f;
        }
        else
        {
          anchor_off.y = -box_dim.y/8.f;
        }
        Mat3x3F32 box2origin_xform = make_translate_3x3f32(v2f32(-box->rect.x0 - box_dim.x/2 + anchor_off.x, -box->rect.y0 + anchor_off.y));
        Mat3x3F32 scale_xform = make_scale_3x3f32(v2f32(1-box->squish, 1-box->squish));
        Mat3x3F32 origin2box_xform = make_translate_3x3f32(v2f32(box->rect.x0 + box_dim.x/2 - anchor_off.x, box->rect.y0 - anchor_off.y));
        Mat3x3F32 xform = mul_3x3f32(origin2box_xform, mul_3x3f32(scale_xform, box2origin_xform));
        dr_push_xform2d(xform);
        dr_push_tex2d_sample_kind(R_Tex2DSampleKind_Linear);
      }
      
      // rjf: draw drop shadow
      if(do_drop_shadows && box->flags & UI_BoxFlag_DrawDropShadow)
      {
        Rng2F32 drop_shadow_rect = shift_2f32(pad_2f32(box->rect, 8), v2f32(4, 4));
        R_Rect2DInst *inst = dr_rect(drop_shadow_rect, drop_shadow_color, 0.8f, 0, 8.f);
        MemoryCopyArray(inst->corner_radii, box_corner_radii);
      }
      
      // rjf: blur background
      // (skipped inside a surface bracket - a blur pass would sever the surface's
      // UI pass & its result would composite under, not into, the surface)
      if(do_background_blur && box->flags & UI_BoxFlag_DrawBackgroundBlur && surface_box_count == 0)
      {
        R_PassParams_Blur *params = dr_blur(pad_2f32(box->rect, 1.f), box->blur_size*(1-box->transparency), 0);
        MemoryCopyArray(params->corner_radii, box_corner_radii);
      }

      // begin offscreen surface -> this box & its subtree draw into a cached
      // render-target texture keyed by the box's key, composited back at its rect on pop
      if(box->flags & UI_BoxFlag_RenderToSurface &&
         !ui_key_match(box->key, ui_key_zero()) &&
         surface_box_count < ArrayCount(surface_box_stack))
      {
        Rng2F32 surface_rect = pad_2f32(box->rect, 2.f);
        F32 backing_scale = wm_backing_scale_from_window(ws->os);
        // non-visible workspace surfaces render at reduced resolution: they're
        // only ever consumed as previews, & this quarters their memory
        RD_WorkspaceSurfaceEntry *ws_entry = rd_workspace_surface_entry_from_box_key(ws, box->key.u64[0]);
        if(ws_entry != 0 && !ws_entry->composite && !ws_entry->full_res)
        {
          backing_scale *= 0.5f;
        }
        Vec2F32 surface_dim = dim_2f32(surface_rect);
        Vec2S32 size_px = v2s32((S32)ceil_f32(surface_dim.x*backing_scale),
                                (S32)ceil_f32(surface_dim.y*backing_scale));
        RD_SurfaceCacheNode *node = rd_window_surface_node_from_key(ws, box->key.u64[0], size_px);
        if(node != 0 && !r_handle_match(node->texture, r_handle_zero()))
        {
          surface_box_stack[surface_box_count] = box;
          surface_node_stack[surface_box_count] = node;
          surface_child_changed_stack[surface_box_count] = 0;
          surface_box_count += 1;
          dr_surface_begin(node->texture, surface_rect);
        }
      }
      
      // rjf: compute effective active t
      F32 effective_active_t = box->active_t;
      if(!(box->flags & UI_BoxFlag_DrawActiveEffects))
      {
        effective_active_t = 0;
      }
      F32 t = box->hot_t*(1-effective_active_t);
      
      // rjf: compute background color
      Vec4F32 box_background_color = box->background_color;
      if(force_opaque_floating_backgrounds && box->flags & UI_BoxFlag_Floating && box->flags & UI_BoxFlag_DrawDropShadow)
      {
        box_background_color.w = 1.f;
      }
      
      // rjf: draw background
      if(box->flags & UI_BoxFlag_DrawBackground)
      {
        // rjf: hot effect extension (drop shadow)
        if(box->flags & UI_BoxFlag_DrawHotEffects)
        {
          Rng2F32 drop_shadow_rect = shift_2f32(pad_2f32(box->rect, 8), v2f32(4, 4));
          Vec4F32 color = drop_shadow_color;
          color.w *= t*box_background_color.w;
          dr_rect(drop_shadow_rect, color, 0.8f, 0, 8.f);
        }
        
        // rjf: draw background
        R_Rect2DInst *inst = dr_rect(pad_2f32(box->rect, 1.f), box_background_color, 0, 0, border_softness*1.f);
        MemoryCopyArray(inst->corner_radii, box_corner_radii);
        
        // rjf: hot effect extension
        if(box->flags & UI_BoxFlag_DrawHotEffects)
        {
          B32 is_hot = !ui_key_match(box->key, ui_key_zero()) && ui_key_match(box->key, ui_hot_key());
          Vec4F32 hover_color = ui_color_from_tags_key_name(box->tags_key, str8_lit("hover"));
          
          // rjf: brighten
          {
            Vec4F32 color = hover_color;
            color.w *= 0.05f;
            if(!is_hot)
            {
              color.w *= t;
            }
            R_Rect2DInst *inst = dr_rect(pad_2f32(box->rect, 1.f), v4f32(0, 0, 0, 0), 0, 0, border_softness*1.f);
            inst->colors[Corner_00] = color;
            inst->colors[Corner_10] = color;
            MemoryCopyArray(inst->corner_radii, box_corner_radii);
          }
          
          // rjf: soft circle around mouse
          if(box->hot_t > 0.01f) DR_ClipScope(intersect_2f32(box->rect, dr_top_clip()))
          {
            Vec4F32 color = hover_color;
            color.w *= 0.02f;
            if(!is_hot)
            {
              color.w *= t;
            }
            Vec2F32 center = ui_mouse();
            Vec2F32 box_dim = dim_2f32(box->rect);
            F32 max_dim = Max(box_dim.x, box_dim.y);
            F32 radius = box->font_size*12.f;
            radius = Min(max_dim, radius);
            dr_rect(pad_2f32(r2f32(center, center), radius), color, radius, 0, radius/3.f);
          }
        }
        
        // rjf: active effect extension
        if(box->flags & UI_BoxFlag_DrawActiveEffects)
        {
          Vec4F32 shadow_color = drop_shadow_color;
          shadow_color.w *= 0.5f*box->active_t;
          Vec2F32 shadow_size =
          {
            (box->rect.x1 - box->rect.x0)*0.60f*box->active_t,
            (box->rect.y1 - box->rect.y0)*0.60f*box->active_t,
          };
          shadow_size.x = Clamp(0, shadow_size.x, box->font_size*2.f);
          shadow_size.y = Clamp(0, shadow_size.y, box->font_size*2.f);
          
          // rjf: top -> bottom dark effect
          {
            R_Rect2DInst *inst = dr_rect(r2f32p(box->rect.x0, box->rect.y0, box->rect.x1, box->rect.y0 + shadow_size.y), v4f32(0, 0, 0, 0), 0, 0, 1.f);
            inst->colors[Corner_00] = inst->colors[Corner_10] = shadow_color;
            inst->colors[Corner_01] = inst->colors[Corner_11] = v4f32(0.f, 0.f, 0.f, 0.0f);
            MemoryCopyArray(inst->corner_radii, box_corner_radii);
          }
          
          // rjf: bottom -> top light effect
          {
            R_Rect2DInst *inst = dr_rect(r2f32p(box->rect.x0, box->rect.y1 - shadow_size.y, box->rect.x1, box->rect.y1), v4f32(0, 0, 0, 0), 0, 0, 1.f);
            inst->colors[Corner_00] = inst->colors[Corner_10] = v4f32(0, 0, 0, 0);
            inst->colors[Corner_01] = inst->colors[Corner_11] = v4f32(1.0f, 1.0f, 1.0f, 0.08f*box->active_t);
            MemoryCopyArray(inst->corner_radii, box_corner_radii);
          }
          
          // rjf: left -> right dark effect
          {
            R_Rect2DInst *inst = dr_rect(r2f32p(box->rect.x0, box->rect.y0, box->rect.x0 + shadow_size.x, box->rect.y1), v4f32(0, 0, 0, 0), 0, 0, 1.f);
            inst->colors[Corner_10] = inst->colors[Corner_11] = v4f32(0.f, 0.f, 0.f, 0.f);
            inst->colors[Corner_00] = shadow_color;
            inst->colors[Corner_01] = shadow_color;
            MemoryCopyArray(inst->corner_radii, box_corner_radii);
          }
          
          // rjf: right -> left dark effect
          {
            R_Rect2DInst *inst = dr_rect(r2f32p(box->rect.x1 - shadow_size.x, box->rect.y0, box->rect.x1, box->rect.y1), v4f32(0, 0, 0, 0), 0, 0, 1.f);
            inst->colors[Corner_00] = inst->colors[Corner_01] = v4f32(0.f, 0.f, 0.f, 0.f);
            inst->colors[Corner_10] = shadow_color;
            inst->colors[Corner_11] = shadow_color;
            MemoryCopyArray(inst->corner_radii, box_corner_radii);
          }
        }
      }
      
      // rjf: draw string
      if(box->flags & UI_BoxFlag_DrawText)
      {
        Vec2F32 text_position = ui_box_text_position(box);
        if(DEV_draw_ui_text_pos)
        {
          dr_rect(r2f32p(text_position.x-4, text_position.y-4, text_position.x+4, text_position.y+4),
                  v4f32(1, 0, 1, 1), 1, 0, 1);
        }
        F32 max_x = 100000.f;
        FNT_Run ellipses_run = {0};
        if(!(box->flags & UI_BoxFlag_DisableTextTrunc))
        {
          FNT_Tag ellipses_font = box->font;
          F32 ellipses_size = box->font_size;
          FNT_RasterFlags ellipses_raster_flags = box->text_raster_flags;
          if(box->display_fstrs.last)
          {
            ellipses_font = box->display_fstrs.last->v.params.font;
            ellipses_size = box->display_fstrs.last->v.params.size;
            ellipses_raster_flags = box->display_fstrs.last->v.params.raster_flags;
          }
          max_x = (box->rect.x1-text_position.x);
          ellipses_run = dr_fnt_run_from_string(ellipses_font, ellipses_size, 0, box->tab_size, ellipses_raster_flags, str8_lit("..."));
        }
        if(box->flags & UI_BoxFlag_HasFuzzyMatchRanges) UI_TagF("match")
        {
          Vec4F32 match_color = ui_color_from_tags_key_name(ui_top_tags_key(), str8_lit("background"));
          dr_truncated_fancy_run_fuzzy_matches(text_position, &box->display_fruns, max_x, &box->fuzzy_match_ranges, match_color);
        }
        dr_truncated_fancy_run_list(text_position, &box->display_fruns, max_x, ellipses_run);
      }
      
      // rjf: draw focus viz
      if(DEV_draw_ui_focus_debug)
      {
        B32 focused = (box->flags & (UI_BoxFlag_FocusHot|UI_BoxFlag_FocusActive) &&
                       box->flags & UI_BoxFlag_Clickable);
        B32 disabled = 0;
        for(UI_Box *p = box; !ui_box_is_nil(p); p = p->parent)
        {
          if(p->flags & (UI_BoxFlag_FocusHotDisabled|UI_BoxFlag_FocusActiveDisabled))
          {
            disabled = 1;
            break;
          }
        }
        if(focused)
        {
          Vec4F32 color = v4f32(0.3f, 0.8f, 0.3f, 1.f);
          if(disabled)
          {
            color = v4f32(0.8f, 0.3f, 0.3f, 1.f);
          }
          dr_rect(r2f32p(box->rect.x0-6, box->rect.y0-6, box->rect.x0+6, box->rect.y0+6), color, 2, 0, 1);
          dr_rect(box->rect, color, 2, 2, 1);
        }
        if(box->flags & (UI_BoxFlag_FocusHot|UI_BoxFlag_FocusActive))
        {
          if(box->flags & (UI_BoxFlag_FocusHotDisabled|UI_BoxFlag_FocusActiveDisabled))
          {
            dr_rect(r2f32p(box->rect.x0-6, box->rect.y0-6, box->rect.x0+6, box->rect.y0+6), v4f32(1, 0, 0, 0.2f), 2, 0, 1);
          }
          else
          {
            dr_rect(r2f32p(box->rect.x0-6, box->rect.y0-6, box->rect.x0+6, box->rect.y0+6), v4f32(0, 1, 0, 0.2f), 2, 0, 1);
          }
        }
      }
      
      // rjf: push clip
      if(box->flags & UI_BoxFlag_Clip)
      {
        Rng2F32 top_clip = dr_top_clip();
        Rng2F32 new_clip = pad_2f32(box->rect, -1);
        if(top_clip.x1 != 0 || top_clip.y1 != 0)
        {
          new_clip = intersect_2f32(new_clip, top_clip);
        }
        dr_push_clip(new_clip);
      }
      
      // rjf: custom draw list
      if(box->flags & UI_BoxFlag_DrawBucket)
      {
        Mat3x3F32 xform = make_translate_3x3f32(box->position_delta);
        DR_XForm2DScope(xform)
        {
          dr_sub_bucket(box->draw_bucket);
        }
      }
      
      // rjf: call custom draw callback
      if(box->custom_draw != 0)
      {
        box->custom_draw(box, box->custom_draw_user_data);
      }
      
      // rjf: pop
      {
        S32 pop_idx = 0;
        for(UI_Box *b = box; !ui_box_is_nil(b) && pop_idx <= rec.pop_count; b = b->parent)
        {
          pop_idx += 1;
          if(b == box && rec.push_count != 0)
          {
            continue;
          }
          
          // rjf: pop clips
          if(b->flags & UI_BoxFlag_Clip)
          {
            dr_pop_clip();
          }

          // end offscreen surface -> composite it where the subtree would have
          // drawn; skip the render when content is unchanged, & propagate "changed"
          // to the enclosing surface (its composite bytes don't reflect our content)
          if(surface_box_count != 0 && b == surface_box_stack[surface_box_count-1])
          {
            surface_box_count -= 1;
            RD_SurfaceCacheNode *node = surface_node_stack[surface_box_count];
            B32 force_render = surface_child_changed_stack[surface_box_count];
            RD_WorkspaceSurfaceEntry *ws_entry = rd_workspace_surface_entry_from_box_key(ws, b->key.u64[0]);
            B32 changed = 0;
            if(ws_entry != 0 && !ws_entry->composite)
            {
              // non-visible workspace: render offscreen only; nothing composites.
              // when every view inside declared a producer version, those + the
              // group shapes carry content identity - no instance bytes are read;
              // a single unversioned view keeps the workspace on byte hashing
              B32 shape_only = !ws_entry->has_unversioned_views;
              changed = dr_surface_end_cached(&node->rendered_hash, force_render, shape_only, ws_entry->content_version_accum);
            }
            else if(b->surface_effect.size != 0)
            {
              // effect chain: end the surface without compositing, run the
              // chain into a cached sibling target when the source re-rendered
              // (or the effect/params changed), & composite the chain's output
              R_Handle effect = rd_effect_from_name(b->surface_effect);
              changed = dr_surface_end_cached(&node->rendered_hash, force_render, 0, 0);
              Rng2F32 composite_rect = pad_2f32(b->rect, 2.f);
              R_Handle composite_tex = node->texture;
              if(!r_handle_match(effect, r_handle_zero()))
              {
                Vec2S32 out_px = r_size_from_tex2d(node->texture);
                RD_SurfaceCacheNode *out_node = rd_window_surface_node_from_key(ws, b->key.u64[0] ^ 0xeffec700ull, out_px);
                if(out_node != 0 && !r_handle_match(out_node->texture, r_handle_zero()))
                {
                  U64 chain_hash = u64_hash_from_seed_str8(5381, b->surface_effect);
                  chain_hash = u64_hash_from_seed_str8(chain_hash, str8_struct(&b->surface_effect_params));
                  if(changed || out_node->rendered_hash != chain_hash || out_node->last_was_preserved == 0)
                  {
                    dr_effect(effect, node->texture, out_node->texture,
                              b->surface_effect_params[0], b->surface_effect_params[1]);
                    out_node->rendered_hash = chain_hash;
                    out_node->last_was_preserved = 1;
                    changed = 1;
                  }
                  composite_tex = out_node->texture;
                }
              }
              dr_surface_img(composite_tex, composite_rect, v4f32(1, 1, 1, 1), 0, 0, 0);
            }
            else
            {
              changed = dr_surface_end_composite_cached(&node->rendered_hash, force_render);
            }
            node->last_was_preserved = !changed;
            if(changed && surface_box_count != 0)
            {
              surface_child_changed_stack[surface_box_count-1] = 1;
            }

            // workspace surface re-rendered -> refresh its retained preview, a
            // small downsampled texture shown in the control surface rows (&,
            // for non-visible workspaces, the only consumer of the surface)
            if(changed && ws_entry != 0)
            {
              // minis hold only the workspace-content subrect of the wrapper (no
              // transparent sidebar band), at the content's aspect
              Rng2F32 content_uv = ws->workspace_content_uv;
              Vec2F32 full_dim = dim_2f32(b->rect);
              Vec2F32 content_dim = v2f32(full_dim.x*(content_uv.x1 - content_uv.x0),
                                          full_dim.y*(content_uv.y1 - content_uv.y0));
              if(content_dim.x > 1 && content_dim.y > 1)
              {
                F32 backing_scale = wm_backing_scale_from_window(ws->os);
                F32 mini_w_pt = rd_workspace_preview_demand_width(ws, ws_entry->workspace_id);
                F32 mini_h_pt = floor_f32(mini_w_pt*content_dim.y/content_dim.x);
                Vec2S32 mini_px = v2s32((S32)ceil_f32(mini_w_pt*backing_scale),
                                        (S32)ceil_f32(mini_h_pt*backing_scale));
                U64 mini_key = rd_workspace_preview_surface_key(ws_entry->workspace_id);
                RD_SurfaceCacheNode *mini = rd_window_surface_node_from_key(ws, mini_key, mini_px);
                if(mini != 0 && !r_handle_match(mini->texture, r_handle_zero()))
                {
                  mini->retained = 1;
                  Rng2F32 mini_rect = r2f32p(0, 0, mini_w_pt, mini_h_pt);
                  dr_surface_begin(mini->texture, mini_rect);
                  DR_Tex2DSampleKindScope(R_Tex2DSampleKind_Linear)
                  {
                    dr_surface_img_sub(node->texture, mini_rect, content_uv, v4f32(1, 1, 1, 1), 0, 0, 0);
                  }
                  dr_surface_end();
                }
              }
            }
          }

          // rjf: get corner radii
          F32 b_corner_radii[Corner_COUNT] =
          {
            b->corner_radii[Corner_00]*rounded_corner_amount,
            b->corner_radii[Corner_01]*rounded_corner_amount,
            b->corner_radii[Corner_10]*rounded_corner_amount,
            b->corner_radii[Corner_11]*rounded_corner_amount,
          };
          
          // rjf: draw border
          if(b->flags & UI_BoxFlag_DrawBorder)
          {
            Vec4F32 border_color = b->border_color;
            Rng2F32 b_border_rect = pad_2f32(b->rect, stroke_softness);
            R_Rect2DInst *inst = dr_rect(b_border_rect, border_color, 0, 1.f, stroke_softness);
            MemoryCopyArray(inst->corner_radii, b_corner_radii);
            
            // rjf: hover effect
            if(b->flags & UI_BoxFlag_DrawHotEffects)
            {
              Vec4F32 color = ui_color_from_tags_key_name(box->tags_key, str8_lit("hover"));
              if(ui_key_match(b->key, ui_key_zero()) || !ui_key_match(b->key, ui_hot_key()))
              {
                color.w *= b->hot_t;
              }
              color.w *= 0.01f;
              R_Rect2DInst *inst = dr_rect(b_border_rect, color, 0, 1.f, stroke_softness);
              MemoryCopyArray(inst->corner_radii, b_corner_radii);
            }
          }
          
          // rjf: debug border rendering
          if(b->flags & UI_BoxFlag_Debug)
          {
            R_Rect2DInst *inst = dr_rect(b->rect, v4f32(1*box->pref_size[Axis2_X].strictness, 0, 1, 0.25f), 0, 1.f, 0);
            MemoryCopyArray(inst->corner_radii, b_corner_radii);
          }
          
          // rjf: draw sides
          if(b->flags & (UI_BoxFlag_DrawSideTop|UI_BoxFlag_DrawSideBottom|UI_BoxFlag_DrawSideLeft|UI_BoxFlag_DrawSideRight))
          {
            Vec4F32 border_color = b->border_color;
            Rng2F32 r = b->rect;
            F32 half_thickness = 1.f;
            F32 softness = 0.f;
            if(b->flags & UI_BoxFlag_DrawSideTop)
            {
              dr_rect(r2f32p(r.x0, r.y0, r.x1, r.y0+2*half_thickness), border_color, 0, 0, softness);
            }
            if(b->flags & UI_BoxFlag_DrawSideBottom)
            {
              dr_rect(r2f32p(r.x0, r.y1-2*half_thickness, r.x1, r.y1), border_color, 0, 0, softness);
            }
            if(b->flags & UI_BoxFlag_DrawSideLeft)
            {
              dr_rect(r2f32p(r.x0, r.y0, r.x0+2*half_thickness, r.y1), border_color, 0, 0, softness);
            }
            if(b->flags & UI_BoxFlag_DrawSideRight)
            {
              dr_rect(r2f32p(r.x1-2*half_thickness, r.y0, r.x1, r.y1), border_color, 0, 0, softness);
            }
          }
          
          // rjf: draw focus overlay
          if(b->flags & UI_BoxFlag_Clickable && !(b->flags & UI_BoxFlag_DisableFocusOverlay) && b->focus_hot_t > 0.01f)
          {
            String8 extras[] = {str8_lit("focus"), str8_lit("overlay")};
            String8Array extras_array = {extras, ArrayCount(extras)};
            Vec4F32 color = ui_color_from_tags_key_extras(b->tags_key, extras_array);
            color.w *= b->focus_hot_t;
            R_Rect2DInst *inst = dr_rect(b->rect, color, 0, 0, 0.f);
            MemoryCopyArray(inst->corner_radii, b_corner_radii);
          }
          
          // rjf: draw focus border
          if(b->flags & UI_BoxFlag_Clickable && !(b->flags & UI_BoxFlag_DisableFocusBorder) && b->focus_active_t > 0.01f)
          {
            Rng2F32 rect = b->rect;
            if(b->flags & UI_BoxFlag_Floating)
            {
              rect = pad_2f32(rect, 1.f);
              rect = intersect_2f32(window_rect, rect);
            }
            String8 extras[] = {str8_lit("focus"), str8_lit("border")};
            String8Array extras_array = {extras, ArrayCount(extras)};
            Vec4F32 color = ui_color_from_tags_key_extras(b->tags_key, extras_array);
            color.w *= b->focus_active_t;
            R_Rect2DInst *inst = dr_rect(rect, color, 0, 1.f, border_softness*1.f);
            MemoryCopyArray(inst->corner_radii, b_corner_radii);
          }
          
          // rjf: disabled overlay
          if(b->disabled_t >= 0.005f)
          {
            Vec4F32 disabled_overlay_color = v4f32(base_background_color.x, base_background_color.y, base_background_color.z, b->disabled_t*0.3f);
            R_Rect2DInst *inst = dr_rect(b->rect, disabled_overlay_color, 0, 0, 1);
            MemoryCopyArray(inst->corner_radii, b_corner_radii);
          }
          
          // rjf: pop squish
          if(b->squish > box_squish_epsilon)
          {
            dr_pop_xform2d();
            dr_pop_tex2d_sample_kind();
          }
          
          // rjf: pop transparency
          if(b->transparency != 0)
          {
            dr_pop_transparency();
          }
        }
      }
      
      // rjf: next
      box = rec.next;
    }

    //- safety: never leave surface brackets dangling past the walk
    for(; surface_box_count != 0; surface_box_count -= 1)
    {
      dr_surface_end_composite();
    }

    //- (DEV) draw a thumbnail strip of all live surfaces - second consumers
    // of the same textures, sampled linearly at reduced scale
    if(DEV_draw_surface_previews)
    {
      F32 thumb_height = floor_f32(rd_font_size()*8.f);
      F32 pad = floor_f32(rd_font_size()*0.5f);
      F32 x = window_rect.x1 - pad;
      F32 y = window_rect.y1 - pad - thumb_height;
      DR_Tex2DSampleKindScope(R_Tex2DSampleKind_Linear)
      {
        for(RD_SurfaceCacheNode *n = ws->first_surface_cache_node; n != 0; n = n->next)
        {
          B32 stale = (n->last_use_frame_index < rd_state->frame_index);
          if(stale && !n->retained)
          {
            continue;
          }
          if(rd_workspace_surface_entry_from_box_key(ws, n->key) != 0)
          {
            // workspace wrapper surfaces are the screen itself / offscreen
            // child builds; their previews are the retained minis, so showing
            // both just reads as duplicates
            continue;
          }
          F32 aspect = (n->size.y > 0 ? (F32)n->size.x/(F32)n->size.y : 1.f);
          F32 thumb_width = floor_f32(thumb_height*aspect);
          Rng2F32 dst = r2f32p(x - thumb_width, y, x, y + thumb_height);
          if(dst.x0 < window_rect.x0 + pad)
          {
            break;
          }
          dr_rect(pad_2f32(dst, 6.f), drop_shadow_color, 4.f, 0, 8.f);
          dr_rect(pad_2f32(dst, 1.f), base_background_color, 0, 0, 1.f);
          dr_surface_img(n->texture, dst, v4f32(1, 1, 1, 1), 0, 0, 0);
          Vec4F32 thumb_border_color = (stale ? v4f32(0.95f, 0.7f, 0.2f, 1.f) :
                                        n->last_was_preserved ? v4f32(0.3f, 0.9f, 0.4f, 1.f) :
                                        base_border_color);
          dr_rect(pad_2f32(dst, 1.f), thumb_border_color, 0, 1.f, 1.f);
          x -= thumb_width + pad;
        }
      }
    }

    //- release cached surfaces that nothing demanded this frame
    rd_window_surface_cache_evict(ws);

    //- rjf: draw heatmap
    if(DEV_draw_ui_box_heatmap)
    {
      U64 uniform_dist_count = total_heatmap_sum_count / heatmap_bucket_count;
      uniform_dist_count = ClampBot(uniform_dist_count, 10);
      for(U64 bucket_idx = 0; bucket_idx < heatmap_bucket_count; bucket_idx += 1)
      {
        U64 x = bucket_idx % heatmap_bucket_pitch;
        U64 y = bucket_idx / heatmap_bucket_pitch;
        U64 bucket = heatmap_buckets[bucket_idx];
        F32 pct = (F32)bucket / uniform_dist_count;
        pct = Clamp(0, pct, 1);
        Vec3F32 hsv = v3f32((1-pct) * 0.9411f, 1, 0.5f);
        Vec3F32 rgb = rgb_from_hsv(hsv);
        Rng2F32 rect = r2f32p(x*heatmap_bucket_size, y*heatmap_bucket_size, (x+1)*heatmap_bucket_size, (y+1)*heatmap_bucket_size);
        dr_rect(rect, v4f32(rgb.x, rgb.y, rgb.z, 0.3f), 0, 0, 0);
      }
    }
    
    //- rjf: draw hover debug box
    if(hover_debug_box != &ui_nil_box)
    {
      FNT_Tag font = rd_font_from_slot(RD_FontSlot_Code);
      Vec2F32 p = ui_mouse();
      dr_rect(hover_debug_box->rect, v4f32(1, 1, 1, 0.2f), 0, 0, 0);
      dr_text(font, 12.f, 0, 0, FNT_RasterFlag_Hinted, p, v4f32(1, 1, 1, 1), push_str8f(scratch.arena, "key: 0x%I64x", hover_debug_box->key.u64[0]));
      p.y += 20.f;
      dr_text(font, 12.f, 0, 0, FNT_RasterFlag_Hinted, p, v4f32(1, 1, 1, 1), push_str8f(scratch.arena, "string: '%S'", hover_debug_box->string));
      p.y += 20.f;
    }
    
    //- rjf: draw border/overlay color to signify error
    if(ws->error_t > 0.01f) UI_TagF("bad")
    {
      Vec4F32 color = ui_color_from_name(str8_lit("text"));
      color.w *= ws->error_t;
      Rng2F32 rect = wm_client_rect_from_window(ws->os);
      dr_rect(pad_2f32(rect, 24.f), color, 0, 16.f, 12.f);
      dr_rect(rect, v4f32(color.x, color.y, color.z, color.w*0.025f), 0, 0, 0);
    }
    
    //- rjf: draw border/overlay color to signify rebinding
    if(rd_state->bind_change_active) UI_TagF("pop")
    {
      Vec4F32 color = ui_color_from_name(str8_lit("background"));
      Rng2F32 rect = wm_client_rect_from_window(ws->os);
      dr_rect(pad_2f32(rect, 24.f), color, 0, 16.f, 12.f);
      dr_rect(rect, v4f32(color.x, color.y, color.z, color.w*0.025f), 0, 0, 0);
    }
    
    scratch_end(scratch);
  }
  
  //////////////////////////////
  //- rjf: @window_frame_part update per-window frame counters/info
  //
  ws->frames_alive += 1;
  ws->last_window_rect = wm_client_rect_from_window(ws->os);
  
  ProfEnd();
  scratch_end(scratch);
}

#if COMPILER_MSVC && !BUILD_DEBUG
NO_OPTIMIZE_END
#endif

////////////////////////////////
//~ rjf: Eval Visualization

internal String8
rd_value_string_from_eval(Arena *arena, String8 filter, EV_StringParams *params, FNT_Tag font, F32 font_size, F32 max_size, E_Eval eval)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List strs = {0};
  {
    EV_StringIter *iter = ev_string_iter_begin(scratch.arena, eval, params);
    F32 space_taken_px = 0;
    for(String8 string = {0}; ev_string_iter_next(scratch.arena, iter, &string);)
    {
      if(space_taken_px > max_size)
      {
        str8_list_push(scratch.arena, &strs, str8_lit("..."));
        break;
      }
      else
      {
        str8_list_push(scratch.arena, &strs, string);
        space_taken_px += fnt_dim_from_tag_size_string(font, font_size, 0, 0, string).x;
      }
    }
  }
  String8 result = str8_list_join(arena, &strs, 0);
  scratch_end(scratch);
  return result;
}

////////////////////////////////
//~ rjf: Hover Eval

internal void
rd_set_hover_eval(Vec2F32 pos, String8 string)
{
  CFG_Node *window_cfg = cfg_node_from_id(uishell_regs()->window);
  RD_WindowState *ws = rd_window_state_from_cfg(window_cfg);
  if(ws->hover_eval_lastt_us < rd_state->time_in_us &&
     ui_key_match(ui_active_key(UI_MouseButtonKind_Left), ui_key_zero()) &&
     ui_key_match(ui_active_key(UI_MouseButtonKind_Middle), ui_key_zero()) &&
     ui_key_match(ui_active_key(UI_MouseButtonKind_Right), ui_key_zero()))
  {
    B32 is_new_string = (!str8_match(ws->hover_eval_string, string, 0));
    if(is_new_string)
    {
      ws->hover_eval_firstt_us = ws->hover_eval_lastt_us = rd_state->time_in_us;
      arena_clear(ws->hover_eval_arena);
      ws->hover_eval_string = push_str8_copy(ws->hover_eval_arena, string);
      ws->hover_eval_focused = 0;
    }
    ws->hover_eval_spawn_pos = pos;
    ws->hover_eval_lastt_us = rd_state->time_in_us;
  }
}

////////////////////////////////
//~ rjf: Autocompletion Lister

internal void
rd_set_autocomp_regs_(E_Eval dst_eval, UIShell_Regs *regs)
{
  CFG_Node *window_cfg = cfg_node_from_id(uishell_regs()->window);
  RD_WindowState *ws = rd_window_state_from_cfg(window_cfg);
  if(ws->autocomp_last_frame_index < rd_state->frame_index)
  {
    arena_clear(ws->autocomp_arena);
    
    //- rjf: calculate information about the cursor:
    // * what list should we generate?
    // * what string in the input should we replace?
    // etc.
    B32 is_allowed = 0;
    RD_AutocompCursorInfo cursor_info = {0};
    {
      Temp scratch = scratch_begin(0, 0);
      
      // rjf: calculate most general list expression, given the dst_eval space
      B32 force_allow = 0;
      B32 expr_based_replace = 1;
      String8 list_expr = str8_lit("query:views");
      {
        E_TypeKey maybe_enum_type = e_type_key_unwrap(dst_eval.irtree.type_key, E_TypeUnwrapFlag_AllDecorative & ~E_TypeUnwrapFlag_Enums);
        if(dst_eval.space.kind == RD_EvalSpaceKind_MetaCfg)
        {
          CFG_Node *parent = rd_cfg_from_eval_space(dst_eval.space);
          String8 child_key = e_string_from_id(dst_eval.space.u64s[1]);
          MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, parent->string);
          MD_Node *child_schema = &md_nil_node;
          for(MD_NodePtrNode *n = schemas.first; n != 0 && md_node_is_nil(child_schema); n = n->next)
          {
            child_schema = md_child_from_string(n->v, child_key, 0);
          }
          if(str8_match(child_key, str8_lit("theme"), 0))
          {
            list_expr = str8_lit("query:themes");
            expr_based_replace = 0;
            force_allow = 1;
          }
          else if(!str8_match(child_schema->first->string, str8_lit("expr_string"), 0))
          {
            MemoryZeroStruct(&list_expr);
          }
        }
      }
      
      // rjf: determine if autocompletion lister is allowed
      is_allowed = (force_allow || rd_setting_b32_from_name(str8_lit("autocompletion_lister")));
      
      // rjf: tighten list_expr, and filter / replaced-range, if needed
      String8 filter = regs->string;
      Rng1U64 replaced_range = r1u64(0, filter.size);
      String8 callee_expr = {0};
      U64 cursor_arg_idx = 0;
      if(expr_based_replace)
      {
        U64 cursor_off = (U64)(regs->cursor.column-1);
        E_Parse parse = e_parse_from_string(regs->string);
        
        //- rjf: cursor offset -> cursor containing node
        E_Expr *cursor_expr = &e_expr_nil;
        E_Expr *cursor_expr_parent = &e_expr_nil;
        {
          typedef struct ExprWalkTask ExprWalkTask;
          struct ExprWalkTask
          {
            ExprWalkTask *next;
            E_Expr *parent;
            E_Expr *expr;
            S32 depth;
          };
          ExprWalkTask start_task = {0, &e_expr_nil, parse.expr};
          ExprWalkTask *first_task = &start_task;
          ExprWalkTask *last_task = first_task;
          S32 best_depth = 0;
          for(E_Expr *chain = parse.expr->next; chain != &e_expr_nil; chain = chain->next)
          {
            ExprWalkTask *task = push_array(scratch.arena, ExprWalkTask, 1);
            SLLQueuePush(first_task, last_task, task);
            task->parent = &e_expr_nil;
            task->expr = chain;
          }
          for(ExprWalkTask *t = first_task; t != 0; t = t->next)
          {
            E_Expr *e = t->expr;
            if(t->depth >= best_depth && (contains_1u64(e->range, cursor_off) || cursor_off == e->range.max))
            {
              cursor_expr_parent = t->parent;
              cursor_expr = e;
              best_depth = t->depth;
            }
            for(E_Expr *child = e->first; child != &e_expr_nil; child = child->next)
            {
              ExprWalkTask *task = push_array(scratch.arena, ExprWalkTask, 1);
              SLLQueuePush(first_task, last_task, task);
              task->parent = e;
              task->expr = child;
              task->depth = t->depth+1;
            }
          }
        }
        
        //- rjf: cursor is within a call? -> generate an expression for the callee, determine
        // which argument the cursor is on
        if(cursor_expr_parent->kind == E_ExprKind_Call)
        {
          E_Key callee_key = e_key_from_expr(cursor_expr_parent->first);
          callee_expr = e_full_expr_string_from_key(scratch.arena, callee_key);
          for(E_Expr *arg = cursor_expr->prev; arg != cursor_expr_parent->first && arg != &e_expr_nil; arg = arg->prev)
          {
            cursor_arg_idx += 1;
          }
        }
        else if(cursor_expr->kind == E_ExprKind_Call)
        {
          E_Key callee_key = e_key_from_expr(cursor_expr->first);
          callee_expr = e_full_expr_string_from_key(scratch.arena, callee_key);
          for(E_Expr *arg = cursor_expr->first->next; arg != &e_expr_nil; arg = arg->next)
          {
            cursor_arg_idx += 1;
          }
        }
        
        //- rjf: cursor is on right-hand-side of dot? -> show members of left-hand-side
        B32 did_special_cursor_case = 0;
        if(!did_special_cursor_case)
        {
          E_Expr *dot_expr = &e_expr_nil;
          if(cursor_expr->kind == E_ExprKind_MemberAccess && cursor_off == cursor_expr->range.max)
          {
            dot_expr = cursor_expr;
          }
          else if(cursor_expr_parent->kind == E_ExprKind_MemberAccess && cursor_expr == cursor_expr_parent->first->next)
          {
            dot_expr = cursor_expr_parent;
          }
          if(dot_expr != &e_expr_nil)
          {
            did_special_cursor_case = 1;
            E_Eval lhs_eval = e_eval_from_expr(dot_expr->first);
            E_Eval type_of_lhs_eval = e_eval_wrapf(lhs_eval, "typeof($)");
            list_expr = e_full_expr_string_from_key(scratch.arena, type_of_lhs_eval.key);
            filter = cursor_expr->string;
            replaced_range = union_1u64(dot_expr->range, cursor_expr->range);
          }
        }
        
        //- rjf: cursor is on a leaf-identifier? -> replace just that identifier, keep the original list expression
        if(!did_special_cursor_case && cursor_expr->kind == E_ExprKind_LeafIdentifier)
        {
          did_special_cursor_case = 1;
          filter = str8_prefix(cursor_expr->string, cursor_off - cursor_expr->range.min);
          replaced_range = cursor_expr->range;
        }
      }
      
      // rjf: try to map the cursor, within a call, to some schema
      MD_Node *arg_schema = &md_nil_node;
      if(callee_expr.size != 0)
      {
        E_Eval callee_eval = e_eval_from_stringf("view:%S", callee_expr);
        E_Type *callee_type = e_type_from_key(callee_eval.irtree.type_key);
        if(callee_type->kind == E_TypeKind_LensSpec)
        {
          U64 arg_idx = 0;
          MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, callee_type->name);
          for(MD_NodePtrNode *n = schemas.first; n != 0; n = n->next)
          {
            MD_Node *schema = n->v;
            for MD_EachNode(child, schema->first)
            {
              if(!md_node_has_tag(child, str8_lit("no_callee_helper"), 0))
              {
                if(cursor_arg_idx == arg_idx)
                {
                  arg_schema = child;
                  goto end_schema_search;
                }
                arg_idx += 1;
              }
            }
          }
          end_schema_search:;
        }
      }
      
      // rjf: fill bundle
      cursor_info.list_expr = push_str8_copy(ws->autocomp_arena, list_expr);
      cursor_info.filter = push_str8_copy(ws->autocomp_arena, filter);
      cursor_info.replaced_range = replaced_range;
      cursor_info.callee_expr = str8f(ws->autocomp_arena, "view:%S", callee_expr);
      cursor_info.arg_schema = arg_schema;
      
      scratch_end(scratch);
    }
    
    //- rjf: commit autocompletion info
    if(is_allowed)
    {
      ws->autocomp_last_frame_index = rd_state->frame_index;
      ws->autocomp_regs = push_array(ws->autocomp_arena, UIShell_Regs, 1);
      ws->autocomp_regs[0] = uishell_regs_copy(ws->autocomp_arena, regs);
      ws->autocomp_cursor_info = cursor_info;
    }
  }
}

////////////////////////////////
//~ rjf: Colors, Fonts, Config

//- rjf: colors

internal CFG_NodePtrList
rd_theme_color_cfgs_from_user_project(Arena *arena)
{
  CFG_NodePtrList colors_cfgs = {0};
  CFG_Node *theme_parents[] =
  {
    cfg_node_child_from_string(cfg_node_root(), str8_lit("project")),
    cfg_node_child_from_string(cfg_node_root(), str8_lit("user")),
  };
  for EachIndex(idx, ArrayCount(theme_parents))
  {
    CFG_Node *parent_cfg = theme_parents[idx];
    for(CFG_Node *child = parent_cfg->first; child != &cfg_nil_node; child = child->next)
    {
      if(str8_match(child->string, str8_lit("theme_color"), 0))
      {
        cfg_node_ptr_list_push_front(arena, &colors_cfgs, child);
      }
    }
  }
  return colors_cfgs;
}

internal String8
rd_window_theme_name_from_settings(void)
{
  CFG_Node *theme_parents[] =
  {
    cfg_node_child_from_string(cfg_node_root(), str8_lit("project")),
    cfg_node_child_from_string(cfg_node_root(), str8_lit("user")),
  };
  CFG_Node *theme_cfgs[] =
  {
    &cfg_nil_node,
    &cfg_nil_node,
  };
  for EachIndex(idx, ArrayCount(theme_parents))
  {
    CFG_Node *parent_cfg = theme_parents[idx];
    CFG_Node *possible_theme_cfg = cfg_node_child_from_string(parent_cfg, str8_lit("theme"));
    if(possible_theme_cfg != &cfg_nil_node)
    {
      theme_cfgs[idx] = possible_theme_cfg;
    }
  }

  CFG_Node *theme_cfg = theme_cfgs[1];
  if(rd_setting_b32_from_name(str8_lit("use_project_theme")))
  {
    theme_cfg = theme_cfgs[0];
    if(theme_cfg == &cfg_nil_node)
    {
      theme_cfg = theme_cfgs[1];
    }
  }

  String8 theme_name = str8_zero();
  if(theme_cfg != &cfg_nil_node && theme_cfg->first != &cfg_nil_node)
  {
    theme_name = theme_cfg->first->string;
  }
  return theme_name;
}

internal UI_Theme *
rd_theme_from_name_and_colors(Arena *scratch_arena, Access *access, String8 theme_name, CFG_NodePtrList colors_cfgs, B32 fallback_to_default)
{
  MD_Node *theme_tree = rd_theme_tree_from_name(scratch_arena, access, theme_name);
  if(theme_tree == &md_nil_node)
  {
    if(fallback_to_default)
    {
      theme_tree = rd_state->theme_preset_trees[RD_ThemePreset_DefaultDark];
    }
    else
    {
      return 0;
    }
  }

  typedef struct ThemeTask ThemeTask;
  struct ThemeTask
  {
    ThemeTask *next;
    MD_Node *tree;
  };
  ThemeTask start_task = {0, theme_tree};
  ThemeTask *first_task = &start_task;
  ThemeTask *last_task = first_task;
  U64 theme_hash = u64_hash_from_seed_str8(5381, theme_name);
  for(CFG_NodePtrNode *n = colors_cfgs.first; n != 0; n = n->next)
  {
    String8 color_cfg_string = cfg_string_from_tree(scratch_arena, rd_state->cfg_schema_table, str8_zero(), n->v);
    theme_hash = u64_hash_from_seed_str8(theme_hash, color_cfg_string);
    ThemeTask *t = push_array(scratch_arena, ThemeTask, 1);
    SLLQueuePushFront(first_task, last_task, t);
    t->tree = md_tree_from_string(scratch_arena, color_cfg_string);
  }

  typedef struct ThemePatternNode ThemePatternNode;
  struct ThemePatternNode
  {
    ThemePatternNode *next;
    UI_ThemePattern pattern;
  };
  ThemePatternNode *first_pattern = 0;
  ThemePatternNode *last_pattern = 0;
  U64 pattern_count = 0;
  for(ThemeTask *t = first_task; t != 0; t = t->next)
  {
    MD_Node *tree_root = t->tree;
    for(MD_Node *n = tree_root; !md_node_is_nil(n); n = md_node_rec_depth_first_pre(n, tree_root).next)
    {
      if(str8_match(n->string, str8_lit("theme_color"), 0))
      {
        MD_Node *tags_child = md_child_from_string(n, str8_lit("tags"), 0);
        MD_Node *value_child = md_child_from_string(n, str8_lit("value"), 0);
        U8 split_char = ' ';
        String8List tags = str8_split(scratch_arena, tags_child->first->string, &split_char, 1, 0);
        U32 color_u32 = e_value_from_stringf("raw(%S)", value_child->first->string).u32;
        Vec4F32 color_linear = linear_from_srgba(rgba_from_u32(color_u32));
        ThemePatternNode *node = push_array(scratch_arena, ThemePatternNode, 1);
        node->pattern.tags = str8_array_from_list(rd_frame_arena(), &tags);
        node->pattern.linear = color_linear;
        SLLQueuePush(first_pattern, last_pattern, node);
        pattern_count += 1;
      }
    }
  }

  // Theme results are frame-scoped; scratch_arena is only for parsing and task lists.
  UI_Theme *theme = push_array(rd_frame_arena(), UI_Theme, 1);
  theme->patterns_count = pattern_count;
  theme->patterns = push_array(rd_frame_arena(), UI_ThemePattern, theme->patterns_count);
  theme->hash = theme_hash;
  U64 idx = 0;
  for(ThemePatternNode *n = first_pattern; n != 0; n = n->next, idx += 1)
  {
    theme->patterns[idx] = n->pattern;
  }
  return theme;
}

internal UI_Theme *
rd_workspace_theme_from_cfg(Arena *scratch_arena, Access *access, CFG_Node *workspace_cfg, CFG_NodePtrList colors_cfgs, UI_Theme *fallback_theme)
{
  UI_Theme *theme = fallback_theme;
  if(workspace_cfg != &cfg_nil_node)
  {
    CFG_Node *theme_cfg = cfg_node_child_from_string(workspace_cfg, str8_lit("theme"));
    if(theme_cfg != &cfg_nil_node && theme_cfg->first != &cfg_nil_node && theme_cfg->first->string.size != 0)
    {
      UI_Theme *workspace_theme = rd_theme_from_name_and_colors(scratch_arena, access, theme_cfg->first->string, colors_cfgs, 0);
      if(workspace_theme != 0)
      {
        theme = workspace_theme;
      }
    }
  }
  return theme;
}

internal MD_Node *
rd_theme_tree_from_name(Arena *arena, Access *access, String8 theme_name)
{
  Temp scratch = scratch_begin(&arena, 1);
  MD_Node *theme_tree = &md_nil_node;
  if(theme_name.size != 0)
  {
    for EachEnumVal(RD_ThemePreset, p)
    {
      if(str8_match(theme_name, rd_theme_preset_display_string_table[p], 0))
      {
        theme_tree = rd_state->theme_preset_trees[p];
        break;
      }
    }
    if(theme_tree == &md_nil_node)
    {
      String8 path = str8f(scratch.arena, "%S/themes/%S", rd_app_data_folder(scratch.arena), theme_name);
      U64 endt_us = now_time_us()+100;
      if(rd_state->frame_index <= 5)
      {
        endt_us = now_time_us()+50000;
      }
      U128 hash = fs_hash_from_path_range(path, r1u64(0, max_U64), endt_us);
      String8 data = c_data_from_hash(access, hash);
      theme_tree = md_tree_from_string(arena, data);
    }
  }
  scratch_end(scratch);
  return theme_tree;
}

internal Vec4F32
rd_rgba_from_code_color_slot(RD_CodeColorSlot slot)
{
  Vec4F32 result = ui_color_from_name(rd_code_color_slot_name_table[slot]);
  return result;
}

internal RD_CodeColorSlot
rd_code_color_slot_from_txt_token_kind(TXT_TokenKind kind)
{
  RD_CodeColorSlot color = RD_CodeColorSlot_CodeDefault;
  switch(kind)
  {
    default:break;
    case TXT_TokenKind_Keyword:{color = RD_CodeColorSlot_CodeKeyword;}break;
    case TXT_TokenKind_Numeric:{color = RD_CodeColorSlot_CodeNumeric;}break;
    case TXT_TokenKind_String: {color = RD_CodeColorSlot_CodeString;}break;
    case TXT_TokenKind_Meta:   {color = RD_CodeColorSlot_CodeMeta;}break;
    case TXT_TokenKind_Comment:{color = RD_CodeColorSlot_CodeComment;}break;
    case TXT_TokenKind_Symbol: {color = RD_CodeColorSlot_CodeDelimiterOperator;}break;
  }
  return color;
}

internal RD_CodeColorSlot
rd_code_color_slot_from_txt_token_kind_lookup_string(TXT_TokenKind kind, String8 string, B32 allow_macros, B32 is_called)
{
  RD_CodeColorSlot color = RD_CodeColorSlot_CodeDefault;
  if(kind == TXT_TokenKind_Identifier || kind == TXT_TokenKind_Keyword)
  {
    B32 mapped = 0;
    
    // rjf: try to map as macro
    if((!mapped || is_called) && allow_macros)
    {
      E_Expr *expr = e_string2expr_map_lookup(e_ir_ctx->macro_map, string);
      if(expr != &e_expr_nil)
      {
        mapped = 1;
        color = RD_CodeColorSlot_CodeMeta;
      }
    }
    
  }
  return color;
}

//- rjf: fonts/sizes

internal F32
rd_font_size(void)
{
  F32 size = rd_setting_f32_from_name(str8_lit("font_size"));
  size = Clamp(6.f, size, 72.f);
  return size;
}

internal FNT_Tag
rd_font_from_slot(RD_FontSlot slot)
{
  FNT_Tag tag = rd_state->font_slot_table[slot];
  return tag;
}

internal FNT_RasterFlags
rd_raster_flags_from_slot(RD_FontSlot slot)
{
  CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
  RD_WindowState *ws = rd_window_state_from_cfg(window);
  FNT_RasterFlags flags = ws->font_slot_raster_flags[slot];
  return flags;
}

////////////////////////////////
//~ rjf: Process Control Info Stringification

////////////////////////////////
//~ rjf: Vocab Info Lookups

internal RD_VocabInfo *
rd_vocab_info_from_code_name(String8 code_name)
{
  RD_VocabInfo *result = &rd_nil_vocab_info;
  if(code_name.size != 0)
  {
    U64 hash = u64_djb2_hash_from_str8(code_name);
    U64 slot_idx = hash%rd_state->vocab_info_map.single_slots_count;
    for(RD_VocabInfoMapNode *n = rd_state->vocab_info_map.single_slots[slot_idx].first;
        n != 0;
        n = n->single_next)
    {
      if(str8_match(n->v.code_name, code_name, 0))
      {
        result = &n->v;
        break;
      }
    }
  }
  return result;
}

internal RD_VocabInfo *
rd_vocab_info_from_code_name_plural(String8 code_name_plural)
{
  RD_VocabInfo *result = &rd_nil_vocab_info;
  if(code_name_plural.size != 0)
  {
    U64 hash = u64_djb2_hash_from_str8(code_name_plural);
    U64 slot_idx = hash%rd_state->vocab_info_map.plural_slots_count;
    for(RD_VocabInfoMapNode *n = rd_state->vocab_info_map.plural_slots[slot_idx].first;
        n != 0;
        n = n->plural_next)
    {
      if(str8_match(n->v.code_name_plural, code_name_plural, 0))
      {
        result = &n->v;
        break;
      }
    }
  }
  return result;
}

////////////////////////////////
//~ rjf: Auto Watch Computation

internal String8Array
rd_gather_auto_exprs(Arena *arena)
{
  String8Array result = {0};
  return result;
}

////////////////////////////////
//~ rjf: Continuous Frame Requests

internal void
rd_request_frame(void)
{
  rd_state->num_frames_requested = 4;
}

////////////////////////////////
//~ rjf: Main State Accessors

//- rjf: per-frame arena

internal Arena *
rd_frame_arena(void)
{
  return rd_state->frame_arenas[rd_state->frame_index%ArrayCount(rd_state->frame_arenas)];
}

////////////////////////////////
//~ rjf: Registers

internal UIShell_Regs *
uishell_push_regs_(UIShell_Regs *regs)
{
  UIShell_RegsNode *n = push_array(rd_frame_arena(), UIShell_RegsNode, 1);
  uishell_regs_copy_contents(rd_frame_arena(), &n->v, regs);
  SLLStackPush(rd_state->top_regs, n);
  return &n->v;
}

internal UIShell_Regs *
uishell_pop_regs(void)
{
  UIShell_Regs *regs = &rd_state->top_regs->v;
  SLLStackPop(rd_state->top_regs);
  if(rd_state->top_regs == 0)
  {
    rd_state->top_regs = &rd_state->base_regs;
  }
  return regs;
}

internal void
uishell_regs_fill_slot_from_string(UIShell_ContextRegSlot slot, String8 query_expr, String8 string)
{
  switch(slot)
  {
    //- rjf: basic string cases
    default:
    case UIShell_ContextRegSlot_String:
    case UIShell_ContextRegSlot_FilePath:
    {
      String8TxtPtPair pair = str8_txt_pt_pair_from_string(string);
      uishell_regs()->string = push_str8_copy(rd_frame_arena(), string);
      if(pair.pt.line != 0)
      {
        uishell_regs()->file_path = push_str8_copy(rd_frame_arena(), pair.string);
        uishell_regs()->cursor = pair.pt;
      }
    }break;
    case UIShell_ContextRegSlot_Expr:
    {
      uishell_regs()->expr = push_str8_copy(rd_frame_arena(), string);
    }break;
    case UIShell_ContextRegSlot_CmdName:
    {
      uishell_regs()->cmd_name = push_str8_copy(rd_frame_arena(), string);
    }break;
    
    //- rjf: ctrl entities
    
    //- rjf: cfgs
    case UIShell_ContextRegSlot_Cfg:
    case UIShell_ContextRegSlot_Window:
    case UIShell_ContextRegSlot_Panel:
    case UIShell_ContextRegSlot_Tab:
    case UIShell_ContextRegSlot_View:
    case UIShell_ContextRegSlot_PrevTab:
    case UIShell_ContextRegSlot_DstPanel:
    {
      B32 good = 0;
      if(!good && str8_match(str8_prefix(string, 1), str8_lit("$"), 0))
      {
        String8 numeric_part = str8_skip(string, 1);
        CFG_ID id = u64_from_str8(numeric_part, 16);
        uishell_regs()->cfg = id;
        good = 1;
      }
      if(!good && query_expr.size != 0)
      {
        Temp scratch = scratch_begin(0, 0);
        CFG_Node *immediate = rd_immediate_cfg_from_keyf("###regs_fill_slot_view");
        CFG_Node *view = cfg_node_newf(rd_state->cfg, immediate, "watch");
        cfg_node_newf(rd_state->cfg, view, "lister");
        RD_ViewState *vs = rd_view_state_from_cfg(view);
        EV_View *eval_view = vs->ev_view;
        {
          ev_key_set_expansion(eval_view, ev_key_root(), ev_key_make(ev_hash_from_key(ev_key_root()), 1), 1);
          E_Eval eval = e_eval_from_string(query_expr);
          EV_BlockTree block_tree = {0};
          EV_BlockRangeList block_ranges = {0};
          // TODO(rjf): @cleanup we only need to do this because we implicitly use
          // view info in the block tree build via shell-layer eval hooks, but we
          // should really keep all parameterization info in eval views themselves,
          // to not couple block tree building with frontend state...
          UIShell_RegsScope(.window = 0, .panel = 0, .view = view->id)
          {
            block_tree = ev_block_tree_from_eval(scratch.arena, eval_view, string, eval);
            block_ranges = ev_block_range_list_from_tree(scratch.arena, &block_tree);
            if(block_ranges.first != 0)
            {
              block_ranges.count -= 1;
              block_ranges.first = block_ranges.first->next;
            }
          }
          EV_Row *row = ev_row_from_num(scratch.arena, eval_view, &block_ranges, 1);
          uishell_regs()->cfg = rd_cfg_from_eval_space(row->eval.space)->id;
          good = (uishell_regs()->cfg != 0);
        }
        scratch_end(scratch);
      }
      if(!good)
      {
        E_Eval eval = e_eval_from_string(string);
        uishell_regs()->cfg = rd_cfg_from_eval_space(eval.space)->id;
        good = (uishell_regs()->cfg != 0);
      }
    }break;
    
    //- rjf: line numbers
    case UIShell_ContextRegSlot_Cursor:
    {
      E_Eval eval = e_value_eval_from_eval(e_eval_from_string(string));
      if(eval.msgs.max_kind == E_MsgKind_Null)
      {
        uishell_regs()->cursor.column = 1;
        uishell_regs()->cursor.line   = (S64)eval.value.u64;
      }
      else
      {
        log_user_errorf("Couldn't interpret \"`%S`\" as a line number.", string);
      }
    }break;
    case UIShell_ContextRegSlot_Vaddr: goto use_numeric_eval;
    use_numeric_eval:
    {
      E_Eval eval = e_eval_from_string(string);
      if(eval.msgs.max_kind == E_MsgKind_Null)
      {
        uishell_regs()->vaddr = eval.value.u64;
      }
      else
      {
        log_user_errorf("Couldn't evaluate `%S` as an address.", string);
      }
    }break;
  }
}

////////////////////////////////
//~ rjf: Commands

internal void
uishell_register_cmd_pack(UIShell_CmdPack *pack)
{
  DLLPushBack(rd_state->first_cmd_pack, rd_state->last_cmd_pack, pack);
}

internal UIShell_AppCmdInfo
uishell_app_cmd_info_from_string(String8 string)
{
  UIShell_AppCmdInfo result = {0};
  for(UIShell_CmdPack *pack = rd_state->first_cmd_pack; pack != 0; pack = pack->next)
  {
    if(pack->cmd_info_from_string != 0)
    {
      result = pack->cmd_info_from_string(string);
      if(result.string.size != 0)
      {
        break;
      }
    }
  }
  return result;
}

//- rjf: pushing

internal void
uishell_push_stored_cmd(String8 name, UIShell_Regs *regs)
{
  uishell_cmd_list_push_new(rd_state->cmds_arenas[0], &rd_state->cmds[0], name, regs);
}

//- rjf: iterating

internal B32
uishell_next_cmd(UIShell_Cmd **cmd)
{
  U64 slot = rd_state->cmds_gen%ArrayCount(rd_state->cmds);
  UIShell_CmdNode *start_node = rd_state->cmds[slot].first;
  if(cmd[0] != 0)
  {
    start_node = CastFromMember(UIShell_CmdNode, cmd, cmd[0]);
    start_node = start_node->next;
  }
  cmd[0] = 0;
  if(start_node != 0)
  {
    cmd[0] = &start_node->cmd;
  }
  return !!cmd[0];
}

internal B32
uishell_next_view_cmd(UIShell_Cmd **cmd)
{
  for(;uishell_next_cmd(cmd);)
  {
    if(uishell_regs()->view == cmd[0]->regs->view)
    {
      break;
    }
  }
  B32 result = !!cmd[0];
  return result;
}

internal RD_AppMenuSpecList
rd_app_menu_specs(void)
{
#define RD_MenuCmd(name, cp) {0, str8_lit_comp(name), cp}
#define RD_MenuSep()        {1, {0}, 0}
  RD_AppMenuSpecList result = {0};
  for(UIShell_CmdPack *pack = rd_state->first_cmd_pack; pack != 0; pack = pack->next)
  {
    if(pack->menu_specs != 0)
    {
      RD_AppMenuSpecList pack_specs = pack->menu_specs();
      if(pack_specs.count != 0)
      {
        if(result.count == 0)
        {
          result = pack_specs;
        }
        else
        {
          RD_AppMenuSpec *v = push_array(rd_frame_arena(), RD_AppMenuSpec, result.count + pack_specs.count);
          MemoryCopy(v, result.v, sizeof(RD_AppMenuSpec)*result.count);
          MemoryCopy(v + result.count, pack_specs.v, sizeof(RD_AppMenuSpec)*pack_specs.count);
          result.v = v;
          result.count += pack_specs.count;
        }
      }
    }
  }
#undef RD_MenuSep
#undef RD_MenuCmd
  return result;
}

internal void
rd_app_menu_buttons(RD_AppMenuSpec *spec)
{
  Temp scratch = scratch_begin(0, 0);
  String8 *cmds = push_array(scratch.arena, String8, spec->item_count);
  U32 *codepoints = push_array(scratch.arena, U32, spec->item_count);
  for(U64 idx = 0; idx < spec->item_count; idx += 1)
  {
    RD_AppMenuItemSpec *item = &spec->items[idx];
    if(!item->separator)
    {
      cmds[idx] = item->command_name;
      codepoints[idx] = item->codepoint;
    }
  }
  rd_cmd_list_menu_buttons(spec->item_count, cmds, codepoints);
  scratch_end(scratch);
}

internal void
rd_app_menu_spec_content(RD_AppMenuSpec *spec)
{
  if(spec->item_count != 0)
  {
    rd_app_menu_buttons(spec);
  }
  // the Help menu spec carries no command items; its content is the build
  // string, logo, and app-provided help entries. shared by both menu
  // presentations (full bar & compact kebab) so neither shows an empty Help.
  if(str8_match(spec->label, str8_lit("Help"), 0))
  {
    UI_Row UI_TextAlignment(UI_TextAlign_Center) UI_TagF("weak")
      ui_label(str8_lit(BUILD_TITLE_STRING_LITERAL));
    ui_spacer(ui_em(1.f, 1.f));
    UI_PrefHeight(ui_children_sum(1)) UI_Row UI_Padding(ui_pct(1, 0))
    {
      R_Handle texture = rd_state->icon_texture;
      Vec2S32 texture_dim = r_size_from_tex2d(texture);
      UI_PrefWidth(ui_px(ui_top_font_size()*10.f, 1.f))
        UI_PrefHeight(ui_px(ui_top_font_size()*10.f, 1.f))
        ui_image(texture, R_Tex2DSampleKind_Linear, r2f32p(0, 0, texture_dim.x, texture_dim.y), v4f32(1, 1, 1, 1), 0, str8_lit(""));
    }
    ui_spacer(ui_em(1.f, 1.f));
    UISHELL_APP_BUILD_HELP_MENU();
    ui_spacer(ui_em(0.5f, 1.f));
  }
}

internal void
rd_wm_set_main_menu(void)
{
  RD_AppMenuSpecList specs = rd_app_menu_specs();
  WM_MenuArray menu_array = {0};
  menu_array.count = specs.count;
  menu_array.menus = push_array(rd_state->arena, WM_Menu, menu_array.count);
  for(U64 menu_idx = 0; menu_idx < menu_array.count; menu_idx += 1)
  {
    RD_AppMenuSpec *spec = &specs.v[menu_idx];
    WM_Menu *menu = &menu_array.menus[menu_idx];
    menu->label = spec->label;
    menu->item_count = spec->item_count;
    menu->items = push_array(rd_state->arena, WM_MenuItem, menu->item_count);
    for(U64 item_idx = 0; item_idx < menu->item_count; item_idx += 1)
    {
      RD_AppMenuItemSpec *item_spec = &spec->items[item_idx];
      WM_MenuItem *item = &menu->items[item_idx];
      if(item_spec->separator)
      {
        item->kind = WM_MenuItemKind_Separator;
      }
      else
      {
        UIShell_AppCmdInfo info = uishell_app_cmd_info_from_string(item_spec->command_name);
        item->kind = WM_MenuItemKind_Command;
        item->command_name = item_spec->command_name;
        item->label = rd_display_from_code_name(item_spec->command_name);
        if(item->label.size == 0)
        {
          item->label = item_spec->command_name;
        }
        if(info.string.size != 0)
        {
          item->command_name = info.string;
        }
      }
    }
  }
  wm_set_main_menu(menu_array);
}

internal String8
rd_app_data_folder(Arena *arena)
{
  String8 result = push_str8f(arena, "%S/%S%s",
                              get_process_info()->user_program_data_path,
                              program_data_folder_prefix_from_os(OperatingSystem_CURRENT),
                              RD_APP_STORAGE_DIR);
  return result;
}

internal CFG_Node *
rd_cfg_new_view_tab(CFG_Node *parent, String8 view, String8 expr, B32 selected)
{
  CFG_Node *tab = cfg_node_new(rd_state->cfg, parent, view);
  CFG_Node *expr_cfg = cfg_node_new(rd_state->cfg, tab, str8_lit("expression"));
  cfg_node_new(rd_state->cfg, expr_cfg, expr);
  if(selected)
  {
    cfg_node_new(rd_state->cfg, tab, str8_lit("selected"));
  }
  return tab;
}

internal void
rd_vocab_info_map_insert(Arena *arena, RD_VocabInfoMap *map, RD_VocabInfo *info)
{
  RD_VocabInfoMapNode *n = push_array(arena, RD_VocabInfoMapNode, 1);
  MemoryCopyStruct(&n->v, info);
  U64 single_hash = u64_djb2_hash_from_str8(n->v.code_name);
  U64 plural_hash = u64_djb2_hash_from_str8(n->v.code_name_plural);
  U64 single_slot_idx = single_hash%map->single_slots_count;
  U64 plural_slot_idx = plural_hash%map->plural_slots_count;
  if(n->v.code_name.size != 0)
  {
    SLLQueuePush_N(map->single_slots[single_slot_idx].first, map->single_slots[single_slot_idx].last, n, single_next);
  }
  if(n->v.code_name_plural.size != 0)
  {
    SLLQueuePush_N(map->plural_slots[plural_slot_idx].first, map->plural_slots[plural_slot_idx].last, n, plural_next);
  }
}

////////////////////////////////
//~ Main Layer Top-Level Calls

#if !defined(STBI_INCLUDE_STB_IMAGE_H)
# define STB_IMAGE_IMPLEMENTATION
# define STBI_ONLY_PNG
# define STBI_ONLY_BMP
# include "third_party/stb/stb_image.h"
#endif

internal void
rd_init(CmdLine *cmdln)
{
  Temp scratch = scratch_begin(0, 0);
  ProfBeginFunction();
  Arena *arena = arena_alloc();
  rd_state = push_array(arena, RD_State, 1);
  rd_state->arena = arena;
  rd_state->quit_after_success = (cmd_line_has_flag(cmdln, str8_lit("quit_after_success")) ||
                                  cmd_line_has_flag(cmdln, str8_lit("q")));
  rd_state->terminal_glyph_trace_enabled = (cmd_line_has_flag(cmdln, str8_lit("terminal_glyph_trace")) ||
                                            cmd_line_has_flag(cmdln, str8_lit("terminal-glyph-trace")));
  rd_state->terminal_glyph_trace_all_rows = 1;
  rd_state->terminal_glyph_trace_row = 0;
  {
    String8 trace_row_string = cmd_line_string(cmdln, str8_lit("terminal_glyph_trace_row"));
    if(trace_row_string.size == 0)
    {
      trace_row_string = cmd_line_string(cmdln, str8_lit("terminal-glyph-trace-row"));
    }
    if(trace_row_string.size != 0 && !str8_match(trace_row_string, str8_lit("all"), 0))
    {
      U64 trace_row = 0;
      if(try_u64_from_str8_c_rules(trace_row_string, &trace_row))
      {
        rd_state->terminal_glyph_trace_all_rows = 0;
        rd_state->terminal_glyph_trace_row = trace_row;
      }
    }
  }
  rd_state->user_path_arena = arena_alloc();
  rd_state->project_path_arena = arena_alloc();
  rd_state->theme_path_arena = arena_alloc();
  rd_state->user_cfg_string_key      = c_key_make(c_root_alloc(), c_id_make(0, 0));
  rd_state->project_cfg_string_key   = c_key_make(c_root_alloc(), c_id_make(0, 0));
  rd_state->cmdln_cfg_string_key     = c_key_make(c_root_alloc(), c_id_make(0, 0));
  rd_state->transient_cfg_string_key = c_key_make(c_root_alloc(), c_id_make(0, 0));
  rd_state->shell_output_key         = c_key_make(c_root_alloc(), c_id_make(0, 0));
  {
    Arena *output_arena = arena_alloc();
    String8 output = push_str8f(output_arena, "Wheelhouse output\n");
    c_submit_data(rd_state->shell_output_key, &output_arena, output);
  }
  for(U64 idx = 0; idx < ArrayCount(rd_state->frame_arenas); idx += 1)
  {
    rd_state->frame_arenas[idx] = arena_alloc();
  }
  UISHELL_APP_REGISTER_CMD_PACKS();
  uishell_register_shell_cmd_packs();
  rd_state->log = log_alloc();
  log_select(rd_state->log);
  {
    Temp scratch = scratch_begin(0, 0);
    // an explicit --user keeps the run's state (and logs) beside that file, so
    // isolated runs such as the UI diagnostics never write the shared app data folder
    String8 log_parent_folder = rd_app_data_folder(scratch.arena);
    String8 explicit_user_path = cmd_line_string(cmdln, str8_lit("user"));
    if(explicit_user_path.size != 0)
    {
      String8 abs_user_path = path_absolute_dst_from_relative_dst_src(scratch.arena, explicit_user_path, get_process_info()->initial_path);
      log_parent_folder = str8_chop_last_slash(abs_user_path);
    }
    String8 log_folder = push_str8f(scratch.arena, "%S/logs", log_parent_folder);
    rd_state->log_path = push_str8f(rd_state->arena, "%S/%s", log_folder, RD_APP_LOG_FILE_NAME);
    make_directory(log_parent_folder);
    make_directory(log_folder);
    write_data_to_file_path(rd_state->log_path, str8_zero());
    scratch_end(scratch);
  }
  rd_state->num_frames_requested = 2;
  rd_state->seconds_until_autosave = 0.5f;
  rd_state->eval_cache = e_cache_alloc();
  for(U64 idx = 0; idx < ArrayCount(rd_state->cmds_arenas); idx += 1)
  {
    rd_state->cmds_arenas[idx] = arena_alloc();
  }
  rd_state->cmd_output_arena = arena_alloc();
  rd_state->popup_arena = arena_alloc();
  rd_state->ctx_menu_key = ui_key_from_string(ui_key_zero(), str8_lit("top_level_ctx_menu"));
  rd_state->drop_completion_key = ui_key_from_string(ui_key_zero(), str8_lit("drop_completion_ctx_menu"));
  rd_state->bind_change_arena = arena_alloc();
  rd_state->drag_drop_arena = arena_alloc();
  rd_state->drag_drop_regs = push_array(rd_state->drag_drop_arena, UIShell_Regs, 1);
  rd_state->top_regs = &rd_state->base_regs;
  
  // rjf: set up schemas
  {
    rd_state->cfg_schema_table = push_array(rd_state->arena, CFG_SchemaTable, 1);
    rd_state->cfg_schema_table->slots_count = 4096;
    rd_state->cfg_schema_table->slots = push_array(rd_state->arena, CFG_SchemaNode *, rd_state->cfg_schema_table->slots_count);
    for EachElement(idx, RD_APP_NAME_SCHEMA_INFO_TABLE)
    {
      String8 name = RD_APP_NAME_SCHEMA_INFO_TABLE[idx].name;
      MD_Node *schema = md_tree_from_string(rd_state->arena, RD_APP_NAME_SCHEMA_INFO_TABLE[idx].schema)->first;
      cfg_schema_table_insert(rd_state->arena, rd_state->cfg_schema_table, name, schema);
    }

    // rjf: the `code_defaults` schema (inherited by `user`) is dynamic — it's
    // regenerated from the code-declared-settings registry each time a new
    // call site registers; insert its (initially empty) slot now
    rd_state->tweak_schema_arena = arena_alloc();
    rd_state->tweak_schema_node = cfg_schema_table_insert(rd_state->arena, rd_state->cfg_schema_table, str8_lit("code_defaults"),
                                                          md_tree_from_string(rd_state->arena, str8_lit("x:{}"))->first);
  }
  
  // rjf: set up theme presets
  {
    for EachEnumVal(RD_ThemePreset, p)
    {
      rd_state->theme_preset_trees[p] = md_tree_from_string(rd_state->arena, rd_theme_preset_cfg_string_table[p])->first;
    }
  }
  
  // rjf: set up vocab info map
  {
    rd_state->vocab_info_map.single_slots_count = 1024;
    rd_state->vocab_info_map.single_slots = push_array(rd_state->arena, RD_VocabInfoMapSlot, rd_state->vocab_info_map.single_slots_count);
    rd_state->vocab_info_map.plural_slots_count = 1024;
    rd_state->vocab_info_map.plural_slots = push_array(rd_state->arena, RD_VocabInfoMapSlot, rd_state->vocab_info_map.plural_slots_count);
    for EachElement(idx, RD_APP_VOCAB_INFO_TABLE)
    {
      rd_vocab_info_map_insert(rd_state->arena, &rd_state->vocab_info_map, &RD_APP_VOCAB_INFO_TABLE[idx]);
    }
    for(UIShell_CmdPack *pack = rd_state->first_cmd_pack; pack != 0; pack = pack->next)
    {
      if(pack->cmd_count != 0 && pack->cmd_info_from_index != 0)
      {
        U64 cmd_count = pack->cmd_count();
        for(U64 idx = 0; idx < cmd_count; idx += 1)
        {
          UIShell_AppCmdInfo cmd_info = pack->cmd_info_from_index(idx);
          if(cmd_info.string.size != 0)
          {
            RD_VocabInfo vocab_info =
            {
              cmd_info.string,
              str8_zero(),
              cmd_info.display_name,
              str8_zero(),
              cmd_info.icon_kind,
            };
            rd_vocab_info_map_insert(rd_state->arena, &rd_state->vocab_info_map, &vocab_info);
          }
        }
      }
    }
  }

  rd_wm_set_main_menu();

  // set up top-level config entity trees & tables
  {
    rd_state->cfg = cfg_state_alloc();
    cfg_ctx_select(cfg_state_ctx(rd_state->cfg));
    cfg_node_new(rd_state->cfg, cfg_node_root(), str8_lit("user"));
    cfg_node_new(rd_state->cfg, cfg_node_root(), str8_lit("project"));
    cfg_node_new(rd_state->cfg, cfg_node_root(), str8_lit("command_line"));
    cfg_node_new(rd_state->cfg, cfg_node_root(), str8_lit("transient"));
  }
  
  // rjf: set up window cache
  {
    rd_state->window_state_slots_count = 64;
    rd_state->window_state_slots = push_array(arena, RD_WindowStateSlot, rd_state->window_state_slots_count);
    rd_state->first_window_state = rd_state->last_window_state = &rd_nil_window_state;
  }
  
  // rjf: set up view cache
  {
    rd_state->view_state_slots_count = 4096;
    rd_state->view_state_slots = push_array(arena, RD_ViewStateSlot, rd_state->view_state_slots_count);
  }
  
  //- rjf: setup initial target from command line args
  String8 implicit_user_arg = {0};
  String8 implicit_project_arg = {0};
  String8 initial_open_file_path = {0};
  {
    Temp scratch2 = scratch_begin(&scratch.arena, 1);
    String8List target_args = {0};
    {
      B32 after_first_non_flag = 0;
      for(U64 idx = 1; idx < cmdln->argc; idx += 1)
      {
        String8 arg = str8_cstring(cmdln->argv[idx]);
        B32 is_flag = (str8_match(str8_prefix(arg, 1), str8_lit("-"), 0) ||
                       str8_match(str8_prefix(arg, 1), str8_lit("--"), 0)
#if OS_WINDOWS
                       || str8_match(str8_prefix(arg, 1), str8_lit("/"), 0)
#endif
                       );
        B32 is_cfg = 0;
        if(!is_flag && !after_first_non_flag)
        {
          File file = file_open(AccessFlag_Read|AccessFlag_ShareRead, arg);
          String8 app_cfg_magic = str8_lit(RD_APP_CONFIG_MAGIC);
          U8 file_magic_maybe[64] = {0};
          Assert(app_cfg_magic.size <= sizeof(file_magic_maybe));
          file_read(file, r1u64(0, app_cfg_magic.size), file_magic_maybe);
          if(MemoryMatch(file_magic_maybe, app_cfg_magic.str, app_cfg_magic.size))
          {
            is_cfg = 1;
            U8 header_suffix_buffer[256] = {0};
            String8 header_suffix = {0};
            header_suffix.str = header_suffix_buffer;
            header_suffix.size = file_read(file, r1u64(app_cfg_magic.size, app_cfg_magic.size+256), header_suffix_buffer);
            String8 header_type_suffix = str8_skip(header_suffix, str8_find_needle(header_suffix, 0, str8_lit(" "), 0)+1);
            if(str8_match(header_type_suffix, str8_lit("user"), StringMatchFlag_RightSideSloppy))
            {
              implicit_user_arg = path_absolute_dst_from_relative_dst_src(scratch.arena, arg, get_process_info()->initial_path);
            }
            else if(str8_match(header_type_suffix, str8_lit("project"), StringMatchFlag_RightSideSloppy))
            {
              implicit_project_arg = path_absolute_dst_from_relative_dst_src(scratch.arena, arg, get_process_info()->initial_path);
            }
          }
          file_close(file);
        }
        if(!is_flag)
        {
          after_first_non_flag = 1;
        }
        if(after_first_non_flag && !is_cfg)
        {
          str8_list_push(scratch2.arena, &target_args, arg);
        }
      }
    }
    if(target_args.node_count > 0 && target_args.first->string.size != 0)
    {
      initial_open_file_path = uishell_initial_open_file_path_from_args(scratch.arena, scratch2.arena, &target_args);
    }
    scratch_end(scratch2);
  }
  
  // rjf: set up user / project paths
  {
    Temp scratch2 = scratch_begin(&scratch.arena, 1);
    
    // rjf: unpack command line arguments
    String8 user_path = cmd_line_string(cmdln, str8_lit("user"));
    String8 project_path = cmd_line_string(cmdln, str8_lit("project"));
    {
      if(user_path.size != 0)
      {
        user_path = path_absolute_dst_from_relative_dst_src(scratch2.arena, user_path, get_process_info()->initial_path);
      }
      if(project_path.size != 0)
      {
        project_path = path_absolute_dst_from_relative_dst_src(scratch2.arena, project_path, get_process_info()->initial_path);
      }
    }
    {
      String8 app_data_folder = rd_app_data_folder(scratch2.arena);
      if(user_path.size == 0)
      {
        user_path = implicit_user_arg;
      }
      if(user_path.size == 0)
      {
        make_directory(app_data_folder);
      }
      if(user_path.size == 0)
      {
        String8 last_user_path = push_str8f(scratch2.arena, "%S/%s", app_data_folder, RD_APP_LAST_USER_FILE_NAME);
        user_path = data_from_file_path(scratch2.arena, last_user_path);
      }
      if(user_path.size == 0)
      {
        user_path = push_str8f(scratch2.arena, "%S/%s", app_data_folder, RD_APP_USER_FILE_NAME);
      }
    }
    if(project_path.size == 0)
    {
      project_path = implicit_project_arg;
    }
    if(project_path.size != 0)
    {
      arena_clear(rd_state->project_path_arena);
      rd_state->project_path = push_str8_copy(rd_state->project_path_arena, project_path);
    }
    
    // rjf: do initial app load
    UISHELL_APP_INITIAL_LOAD(user_path, project_path);
    if(cmd_line_has_flag(cmdln, str8_lit("terminal_fixture")) ||
       cmd_line_has_flag(cmdln, str8_lit("terminal-fixture")))
    {
      uishell_cmd("terminal_fixture");
    }
    if(cmd_line_has_flag(cmdln, str8_lit("scroll_region_fixture")))
    {
      uishell_cmd("scroll_region_fixture");
    }
    if(initial_open_file_path.size != 0)
    {
      uishell_cmd("open", .file_path = initial_open_file_path);
    }
    
    scratch_end(scratch2);
  }
  
  // rjf: unpack icon image data
  {
    Temp scratch = scratch_begin(0, 0);
    String8 data = rd_icon_file_bytes;
    U8 *ptr = data.str;
    U8 *opl = ptr+data.size;
    
    // rjf: read header
#pragma pack(push, 1)
    typedef struct ICO_Header ICO_Header;
    struct ICO_Header
    {
      U16 reserved_padding; // must be 0
      U16 image_type; // if 1 -> ICO, if 2 -> CUR
      U16 num_images;
    };
    typedef struct ICO_Entry ICO_Entry;
    struct ICO_Entry
    {
      U8 image_width_px;
      U8 image_height_px;
      U8 num_colors;
      U8 reserved_padding; // should be 0
      union
      {
        U16 ico_color_planes; // in ICO
        U16 cur_hotspot_x_px; // in CUR
      };
      union
      {
        U16 ico_bits_per_pixel; // in ICO
        U16 cur_hotspot_y_px;   // in CUR
      };
      U32 image_data_size;
      U32 image_data_off;
    };
#pragma pack(pop)
    ICO_Header hdr = {0};
    if(ptr+sizeof(hdr) < opl)
    {
      MemoryCopy(&hdr, ptr, sizeof(hdr));
      ptr += sizeof(hdr);
    }
    
    // rjf: read image entries
    U64 entries_count = hdr.num_images;
    ICO_Entry *entries = push_array(scratch.arena, ICO_Entry, hdr.num_images);
    {
      U64 bytes_to_read = sizeof(ICO_Entry)*entries_count;
      bytes_to_read = Min(bytes_to_read, opl-ptr);
      MemoryCopy(entries, ptr, bytes_to_read);
      ptr += bytes_to_read;
    }
    
    // rjf: find largest image
    ICO_Entry *best_entry = 0;
    U64 best_entry_area = 0;
    for(U64 idx = 0; idx < entries_count; idx += 1)
    {
      ICO_Entry *entry = &entries[idx];
      U64 width = entry->image_width_px;
      if(width == 0) { width = 256; }
      U64 height = entry->image_height_px;
      if(height == 0) { height = 256; }
      U64 entry_area = width*height;
      if(entry_area > best_entry_area)
      {
        best_entry = entry;
        best_entry_area = entry_area;
      }
    }
    
    // rjf: deserialize raw image data from best entry's offset
    U8 *image_data = 0;
    Vec2S32 image_dim = {0};
    if(best_entry != 0)
    {
      U8 *file_data_ptr = data.str + best_entry->image_data_off;
      U64 file_data_size = best_entry->image_data_size;
      int width = 0;
      int height = 0;
      int components = 0;
      image_data = stbi_load_from_memory(file_data_ptr, file_data_size, &width, &height, &components, 4);
      image_dim.x = width;
      image_dim.y = height;
    }
    
    // rjf: upload to gpu texture
    rd_state->icon_texture = r_tex2d_alloc(R_ResourceKind_Static, image_dim, R_Tex2DFormat_RGBA8, image_data);
    
    // rjf: release
    stbi_image_free(image_data);
    scratch_end(scratch);
  }
  
  ProfEnd();
  scratch_end(scratch);
}

internal void
rd_frame(void)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  log_scope_begin();
  rd_state->frame_depth += 1;
  
  //////////////////////////////
  //- rjf: (DEBUG) take top-level cfg roots, stringize them, and store them to hash store
  //
#if 0
  {
    struct
    {
      C_Key key;
      String8 name;
    }
    table[] =
    {
      {rd_state->user_cfg_string_key, str8_lit("user")},
      {rd_state->project_cfg_string_key, str8_lit("project")},
      {rd_state->cmdln_cfg_string_key, str8_lit("command_line")},
      {rd_state->transient_cfg_string_key, str8_lit("transient")},
    };
    for EachElement(idx, table)
    {
      Arena *arena = arena_alloc();
      String8 data = cfg_string_from_tree(arena,
                                          rd_state->cfg_schema_table,
                                          str8_zero(),
                                          cfg_node_child_from_string(cfg_node_root(), table[idx].name));
      c_submit_data(table[idx].key, &arena, data);
    }
  }
#endif
  
  //////////////////////////////
  //- rjf: do per-frame resets
  //
  {
    Temp scratch = scratch_begin(0, 0);
    rd_state->top_regs = &rd_state->base_regs;
    uishell_regs_copy_contents(scratch.arena, &rd_state->top_regs->v, &rd_state->top_regs->v);
    arena_clear(rd_frame_arena());
    uishell_regs_copy_contents(rd_frame_arena(), &rd_state->top_regs->v, &rd_state->top_regs->v);
    scratch_end(scratch);
  }
  if(rd_state->next_hover_regs != 0)
  {
    rd_state->hover_regs = push_array(rd_frame_arena(), UIShell_Regs, 1);
    rd_state->hover_regs[0] = uishell_regs_copy(rd_frame_arena(), rd_state->next_hover_regs);
    rd_state->hover_regs_slot = rd_state->next_hover_regs_slot;
    rd_state->next_hover_regs = 0;
  }
  else
  {
    rd_state->hover_regs = push_array(rd_frame_arena(), UIShell_Regs, 1);
    rd_state->hover_regs_slot = UIShell_ContextRegSlot_Null;
  }
  B32 allow_text_hotkeys = !rd_state->text_edit_mode;
  rd_state->text_edit_mode = 0;
  if(rd_state->frame_depth == 1)
  {
    arena_clear(rd_state->cmd_output_arena);
    MemoryZeroStruct(&rd_state->cmd_outputs);
  }
  
  //////////////////////////////
  //- rjf: iterate all materialized workspace tabs, touch their view-states
  //
  if(rd_state->frame_depth == 1)
  {
    Temp scratch = scratch_begin(0, 0);
    CFG_NodePtrList windows = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("window"));
    for(CFG_NodePtrNode *window_n = windows.first; window_n != 0; window_n = window_n->next)
    {
      CFG_Node *window = window_n->v;
      UIShell_ControlledSplit root_controlled_split = uishell_root_controlled_split_from_window(scratch.arena, window);
      for(UIShell_MaterializedWorkspace *workspace = root_controlled_split.inventory.first;
          workspace != 0;
          workspace = workspace->next)
      {
        CFG_PanelTree panel_tree = workspace->mount.panel_tree;
        for(CFG_PanelNode *p = panel_tree.root; p != &cfg_nil_panel_node; p = cfg_panel_node_rec__depth_first_pre(panel_tree.root, p).next)
        {
          CFG_Node *first_unfiltered_tab = &cfg_nil_node;
          for(CFG_NodePtrNode *tab_n = p->tabs.first; tab_n != 0; tab_n = tab_n->next)
          {
            CFG_Node *tab = tab_n->v;
            if(rd_cfg_is_project_filtered(tab))
            {
              continue;
            }
            if(first_unfiltered_tab == &cfg_nil_node)
            {
              first_unfiltered_tab = tab;
            }
            rd_view_state_from_cfg(tab);
          }
          if(workspace == root_controlled_split.inventory.selected &&
             p->selected_tab == &cfg_nil_node &&
             first_unfiltered_tab != &cfg_nil_node)
          {
            uishell_cmd("focus_tab", .panel = p->cfg->id, .tab = first_unfiltered_tab->id);
          }
        }
      }
    }
    scratch_end(scratch);
  }

  //////////////////////////////
  //- rjf: garbage collect untouched immediate cfg trees
  //
  if(rd_state->frame_depth == 1)
  {
    CFG_Node *transient = cfg_node_child_from_string(cfg_node_root(), str8_lit("transient"));
    for(CFG_Node *tln = transient->first, *next = &cfg_nil_node; tln != &cfg_nil_node; tln = next)
    {
      next = tln->next;
      if(str8_match(tln->string, str8_lit("immediate"), 0))
      {
        if(cfg_node_child_from_string(tln, str8_lit("hot")) == &cfg_nil_node)
        {
          cfg_node_release(rd_state->cfg, tln);
        }
      }
    }
    for(CFG_Node *tln = transient->first; tln != &cfg_nil_node; tln = tln->next)
    {
      if(str8_match(tln->string, str8_lit("immediate"), 0))
      {
        for(CFG_Node *child = tln->first, *next = &cfg_nil_node; child != &cfg_nil_node; child = next)
        {
          next = child->next;
          if(str8_match(child->string, str8_lit("hot"), 0))
          {
            cfg_node_release(rd_state->cfg, child);
          }
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: garbage collect untouched view states
  //
  if(rd_state->frame_depth == 1)
  {
    for EachIndex(slot_idx, rd_state->view_state_slots_count)
    {
      for(RD_ViewState *vs = rd_state->view_state_slots[slot_idx].first, *next; vs != 0; vs = next)
      {
        next = vs->hash_next;
        if(vs->last_frame_index_touched+2 < rd_state->frame_index)
        {
          ev_view_release(vs->ev_view);
          for(RD_ArenaExt *ext = vs->first_arena_ext; ext != 0; ext = ext->next)
          {
            arena_release(ext->arena);
          }
          arena_release(vs->arena);
          DLLRemove_NP(rd_state->view_state_slots[slot_idx].first, rd_state->view_state_slots[slot_idx].last, vs, hash_next, hash_prev);
          SLLStackPush_N(rd_state->free_view_state, vs, hash_next);
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: animate all views
  //
  if(rd_state->frame_depth == 1)
  {
    B32 any_window_is_focused = 0;
    for(RD_WindowState *w = rd_state->first_window_state; w != &rd_nil_window_state; w = w->order_next)
    {
      if(wm_window_is_focused(w->os))
      {
        any_window_is_focused = 1;
        break;
      }
    }
    F32 slow_rate = 1 - pow_f32(2, (-10.f * rd_state->frame_dt));
    F32 fast_rate = 1 - pow_f32(2, (-40.f * rd_state->frame_dt));
    for EachIndex(slot_idx, rd_state->view_state_slots_count)
    {
      for(RD_ViewState *vs = rd_state->view_state_slots[slot_idx].first;
          vs != 0;
          vs = vs->hash_next)
      {
        F32 scroll_x_diff = (-vs->scroll_pos.x.off);
        F32 scroll_y_diff = (-vs->scroll_pos.y.off);
        F32 loading_t_diff = (vs->loading_t_target - vs->loading_t);
        vs->scroll_pos.x.off += scroll_x_diff*rd_state->scrolling_animation_rate;
        vs->scroll_pos.y.off += scroll_y_diff*rd_state->scrolling_animation_rate;
        vs->loading_t += loading_t_diff * slow_rate;
        if((any_window_is_focused && abs_f32(loading_t_diff) > 0.01f) ||
           abs_f32(scroll_x_diff) > 0.01f ||
           abs_f32(scroll_y_diff) > 0.01f)
        {
          rd_request_frame();
        }
        if(abs_f32(scroll_x_diff) <= 0.01f)
        {
          vs->scroll_pos.x.off = 0;
        }
        if(abs_f32(scroll_y_diff) <= 0.01f)
        {
          vs->scroll_pos.y.off = 0;
        }
        CFG_Node *vcfg = cfg_node_from_id(vs->cfg_id);
        if(cfg_node_child_from_string(vcfg, str8_lit("selected")) != &cfg_nil_node)
        {
          if(vs->loading_t_target > 0.5f && any_window_is_focused)
          {
            rd_request_frame();
          }
          vs->loading_t_target = 0;
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: get events from the OS
  //
  WM_EventList events = {0};
  if(rd_state->frame_depth == 1)
  {
    events = wm_get_events(scratch.arena, rd_state->num_frames_requested == 0 && !DEV_always_refresh);
  }
  
  //////////////////////////////
  //- rjf: push frame scopes
  //
  Access *frame_access_restore = rd_state->frame_access;
  rd_state->frame_access = access_open();
  
  //////////////////////////////
  //- rjf: calculate avg length in us of last many frames
  //
  U64 frame_time_history_avg_us = 0;
  {
    U64 num_frames_in_history = Min(ArrayCount(rd_state->frame_time_us_history), rd_state->frame_index);
    U64 frame_time_history_sum_us = 0;
    if(num_frames_in_history > 0)
    {
      for(U64 idx = 0; idx < num_frames_in_history; idx += 1)
      {
        frame_time_history_sum_us += rd_state->frame_time_us_history[idx];
      }
      frame_time_history_avg_us = frame_time_history_sum_us/num_frames_in_history;
    }
  }
  
  //////////////////////////////
  //- rjf: pick target hz
  //
  // pick among a number of sensible targets to snap to, given how well
  // we've been performing
  //
  // TODO(rjf): maximize target, given all windows and their monitors
  //
  F32 target_hz = wm_get_system_info()->default_refresh_rate;
  if(rd_state->frame_index > 32)
  {
    F32 possible_alternate_hz_targets[] = {target_hz, 60.f, 75.f, 120.f, 144.f, 165.f, 240.f, 360.f};
    F32 best_target_hz = target_hz;
    S64 best_target_hz_frame_time_us_diff = max_S64;
    for(U64 idx = 0; idx < ArrayCount(possible_alternate_hz_targets); idx += 1)
    {
      F32 candidate = possible_alternate_hz_targets[idx];
      if(candidate <= target_hz)
      {
        U64 candidate_frame_time_us = 1000000/(U64)candidate;
        S64 frame_time_us_diff = (S64)frame_time_history_avg_us - (S64)candidate_frame_time_us;
        if(abs_s64(frame_time_us_diff) < best_target_hz_frame_time_us_diff &&
           frame_time_history_avg_us < candidate_frame_time_us + candidate_frame_time_us/4)
        {
          best_target_hz = candidate;
          best_target_hz_frame_time_us_diff = frame_time_us_diff;
        }
      }
    }
    target_hz = best_target_hz;
  }
  
  //////////////////////////////
  //- rjf: given frame time history, decide on amount of time we're willing to wait for memory read results
  // for evaluations
  //
  {
    rd_state->frame_eval_memread_endt_us = 0;
    U64 frame_time_target_cap_us = (U64)(1000000/target_hz);
    if(frame_time_history_avg_us < frame_time_target_cap_us)
    {
      U64 spare_time = (frame_time_target_cap_us - frame_time_history_avg_us) + 4000;
      rd_state->frame_eval_memread_endt_us = now_time_us() + spare_time;
    }
  }
  
  //////////////////////////////
  //- rjf: target Hz -> delta time
  //
  rd_state->frame_dt = 1.f/target_hz;
  
  //////////////////////////////
  //- rjf: begin measuring actual per-frame work
  //
  U64 begin_time_us = now_time_us();
  
  //////////////////////////////
  //- rjf: bind change
  //
  if(!rd_state->popup_active && rd_state->bind_change_active)
  {
    if(wm_key_press(&events, wm_window_zero(), 0, WM_Key_Esc))
    {
      rd_request_frame();
      rd_state->bind_change_active = 0;
    }
    if(wm_key_press(&events, wm_window_zero(), 0, WM_Key_Delete))
    {
      rd_request_frame();
      cfg_node_release(rd_state->cfg, cfg_node_from_id(rd_state->bind_change_binding_id));
      rd_state->bind_change_active = 0;
    }
    for(WM_Event *event = events.first, *next = 0; event != 0; event = next)
    {
      if(event->kind == WM_EventKind_Press &&
         event->key != WM_Key_Esc &&
         event->key != WM_Key_Return &&
         event->key != WM_Key_Backspace &&
         event->key != WM_Key_Delete &&
         event->key != WM_Key_LeftMouseButton &&
         event->key != WM_Key_RightMouseButton &&
         event->key != WM_Key_MiddleMouseButton &&
         event->key != WM_Key_Ctrl &&
         event->key != WM_Key_Alt &&
         event->key != WM_Key_Shift)
      {
        rd_state->bind_change_active = 0;
        CFG_Node *binding = cfg_node_from_id(rd_state->bind_change_binding_id);
        if(binding == &cfg_nil_node)
        {
          CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
          CFG_Node *keybindings = cfg_node_child_from_string_or_alloc(rd_state->cfg, user, str8_lit("keybindings"));
          binding = cfg_node_new(rd_state->cfg, keybindings, str8_lit(""));
        }
        cfg_node_release_all_children(rd_state->cfg, binding);
        cfg_node_new(rd_state->cfg, binding, rd_state->bind_change_cmd_name);
        cfg_node_new(rd_state->cfg, binding, wm_key_cfg_name_table[event->key]);
        if(event->modifiers & WM_Modifier_Ctrl)  { cfg_node_new(rd_state->cfg, binding, str8_lit("ctrl")); }
        if(event->modifiers & WM_Modifier_Shift) { cfg_node_new(rd_state->cfg, binding, str8_lit("shift")); }
        if(event->modifiers & WM_Modifier_Alt)   { cfg_node_new(rd_state->cfg, binding, str8_lit("alt")); }
        if(event->modifiers & WM_Modifier_Super) { cfg_node_new(rd_state->cfg, binding, str8_lit("super")); }
        U32 codepoint = wm_codepoint_from_modifiers_and_key(event->modifiers, event->key);
        wm_text(&events, event->window, codepoint);
        wm_eat_event(&events, event);
        rd_request_frame();
        break;
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build key map from config
  //
  ProfScope("build key map from config")
  {
    rd_state->key_map = cfg_key_map_from_cfg(rd_frame_arena());
  }
  
  //////////////////////////////
  //- rjf: get fonts from config
  //
  ProfScope("get fonts from config")
  {
    String8 main_font_name = rd_setting_from_name(str8_lit("main_font"));
    String8 code_font_name = rd_setting_from_name(str8_lit("code_font"));
    rd_state->font_slot_table[RD_FontSlot_Main]  = fnt_tag_from_path(main_font_name);
    rd_state->font_slot_table[RD_FontSlot_Code]  = fnt_tag_from_path(code_font_name);
    if(fnt_tag_match(rd_state->font_slot_table[RD_FontSlot_Main], fnt_tag_zero()))
    {
      rd_state->font_slot_table[RD_FontSlot_Main] = fnt_tag_from_static_data_string(&rd_default_main_font_bytes);
    }
    if(fnt_tag_match(rd_state->font_slot_table[RD_FontSlot_Code], fnt_tag_zero()))
    {
      rd_state->font_slot_table[RD_FontSlot_Code] = fnt_tag_from_static_data_string(&rd_default_code_font_bytes);
    }
    rd_state->font_slot_table[RD_FontSlot_Icons] = fnt_tag_from_static_data_string(&rd_icon_font_bytes);
  }
  //////////////////////////////
  //- apply window manager preferences from config
  //
  {
    local_persist B32 initialized = 0;
    local_persist B32 last_mac_window_decorations = 0;
    local_persist B32 last_mac_native_menu_bar = 0;
    B32 mac_window_decorations = rd_setting_b32_from_name(str8_lit("mac_window_decorations"));
    B32 mac_native_menu_bar = rd_setting_b32_from_name(str8_lit("mac_native_menu_bar"));
    if(!initialized || last_mac_window_decorations != mac_window_decorations)
    {
      wm_set_preferred_window_decorations(mac_window_decorations);
      last_mac_window_decorations = mac_window_decorations;
    }
    if(!initialized || last_mac_native_menu_bar != mac_native_menu_bar)
    {
      wm_set_preferred_native_menu_bar(mac_native_menu_bar);
      if(wm_application_menu_bar_is_native())
      {
        rd_wm_set_main_menu();
      }
      last_mac_native_menu_bar = mac_native_menu_bar;
    }
    initialized = 1;
  }

  //////////////////////////////
  //- rjf: consume events
  //
  ProfScope("consume events")
  {
    for(WM_Event *event = events.first, *next = 0;
        event != 0;
        event = next)
      UIShell_RegsScope()
    {
      next = event->next;
      RD_WindowState *ws = rd_window_state_from_os_handle(event->window);
      if(ws != 0 && ws != rd_window_state_from_cfg(cfg_node_from_id(uishell_regs()->window)))
      {
        Temp scratch = scratch_begin(0, 0);
        UIShell_ControlledSplit root_controlled_split = uishell_root_controlled_split_from_window(scratch.arena, cfg_node_from_id(ws->cfg_id));
        UIShell_WorkspaceMount *workspace_mount = uishell_controlled_split_selected_mount(&root_controlled_split);
        CFG_PanelTree panel_tree = workspace_mount->panel_tree;
        uishell_regs()->window = ws->cfg_id;
        uishell_regs()->panel  = panel_tree.focused->cfg->id;
        uishell_regs()->tab    = panel_tree.focused->selected_tab->id;
        uishell_regs()->view   = panel_tree.focused->selected_tab->id;
        scratch_end(scratch);
      }
      CFG_Node *focused_view = cfg_node_from_id(uishell_regs()->view);
      // Terminal views get physical keyboard input before command bindings; Super stays available for app shortcuts.
      B32 terminal_input_is_focused = (ws != 0 &&
                                       ws != &rd_nil_window_state &&
                                       !rd_state->popup_active &&
                                       !ws->query_is_active &&
                                       !ws->menu_bar_focused &&
                                       str8_match(focused_view->string, str8_lit("terminal"), 0));
      B32 terminal_claims_keyboard_input = (terminal_input_is_focused &&
                                            !(event->modifiers & WM_Modifier_Super) &&
                                            (event->kind == WM_EventKind_Press ||
                                             event->kind == WM_EventKind_Release ||
                                             event->kind == WM_EventKind_Text));
      B32 take = 0;
      if(ws != 0 && ws != &rd_nil_window_state &&
         !rd_state->popup_active && !ws->query_is_active && !ws->menu_bar_focused)
        take = uishell_jackstay_event(focused_view->id,event);
      
      //- rjf: try drag/drop drop-kickoff
      if(rd_drag_is_active() && event->kind == WM_EventKind_Release && event->key == WM_Key_LeftMouseButton)
      {
        rd_state->drag_drop_state = RD_DragDropState_Dropping;
      }
      
      //- rjf: try window close
      if(!take && event->kind == WM_EventKind_WindowClose && ws != 0)
      {
        take = 1;
        uishell_cmd("exit");
      }

      if(!take && event->kind == WM_EventKind_MenuCommand && event->string.size != 0)
      {
        take = 1;
        if(ws != &rd_nil_window_state && str8_match(event->string, str8_lit("window_close_menu"), 0))
        {
          uishell_cmd("close_window");
        }
        else
        {
          uishell_cmd("run_command", .cmd_name = event->string);
        }
        rd_request_frame();
      }

      //- try menu bar operations
      if(rd_state->alt_menu_bar_enabled && !wm_application_menu_bar_is_native())
      {
        if(!take && event->kind == WM_EventKind_Press && event->key == WM_Key_Alt && event->modifiers == 0 && event->is_repeat == 0)
        {
          take = 1;
          rd_request_frame();
          ws->menu_bar_focused_on_press = ws->menu_bar_focused;
          ws->menu_bar_key_held = 1;
          ws->menu_bar_focus_press_started = 1;
        }
        if(!take && event->kind == WM_EventKind_Release && event->key == WM_Key_Alt && event->modifiers == 0 && event->is_repeat == 0)
        {
          take = 1;
          rd_request_frame();
          ws->menu_bar_key_held = 0;
        }
        if(ws->menu_bar_focused && event->kind == WM_EventKind_Press && event->key == WM_Key_Alt && event->modifiers == 0 && event->is_repeat == 0)
        {
          take = 1;
          rd_request_frame();
          ws->menu_bar_focused = 0;
        }
        else if(ws->menu_bar_focus_press_started && !ws->menu_bar_focused && event->kind == WM_EventKind_Release && event->modifiers == 0 && event->key == WM_Key_Alt && event->is_repeat == 0)
        {
          take = 1;
          rd_request_frame();
          ws->menu_bar_focused = !ws->menu_bar_focused_on_press;
          ws->menu_bar_focus_press_started = 0;
        }
        else if(event->kind == WM_EventKind_Press && event->key == WM_Key_Esc && ws->menu_bar_focused && !ui_any_ctx_menu_is_open())
        {
          take = 1;
          rd_request_frame();
          ws->menu_bar_focused = 0;
        }
      }
      
      //- rjf: try hotkey presses
      if(!take && event->kind == WM_EventKind_Press && !terminal_claims_keyboard_input)
      {
        CFG_Binding binding = {event->key, event->modifiers};
        CFG_KeyMapNodePtrList key_map_nodes = cfg_key_map_node_ptr_list_from_binding(scratch.arena, rd_state->key_map, binding);
        if(key_map_nodes.first != 0)
        {
          U32 hit_char = wm_codepoint_from_modifiers_and_key(event->modifiers, event->key);
          if(hit_char == 0 || allow_text_hotkeys)
          {
            String8 cmd_name = key_map_nodes.first->v->name;
            for(U64 idx = 0; idx < ArrayCount(RD_APP_BINDING_VERSION_REMAP_OLD_NAME_TABLE); idx += 1)
            {
              if(str8_match(RD_APP_BINDING_VERSION_REMAP_OLD_NAME_TABLE[idx], cmd_name, StringMatchFlag_CaseInsensitive))
              {
                cmd_name = RD_APP_BINDING_VERSION_REMAP_NEW_NAME_TABLE[idx];
              }
            }
            uishell_cmd("run_command", .cmd_name = cmd_name);
            if(allow_text_hotkeys)
            {
              wm_text(&events, event->window, hit_char);
              next = event->next;
            }
            take = 1;
            if(event->modifiers & WM_Modifier_Alt)
            {
              ws->menu_bar_focus_press_started = 0;
            }
          }
        }
        else if(WM_Key_F1 <= event->key && event->key <= WM_Key_F19)
        {
          ws->menu_bar_focus_press_started = 0;
        }
        rd_request_frame();
      }
      
      //- rjf: try text events
      if(!take && event->kind == WM_EventKind_Text && (event->modifiers & WM_Modifier_Super))
      {
        take = 1;
      }
      if(!take && event->kind == WM_EventKind_Text && !terminal_claims_keyboard_input)
      {
        String32 insertion32 = str32(&event->character, 1);
        String8 insertion8 = str8_from_32(scratch.arena, insertion32);
        uishell_cmd("insert_text", .string = insertion8);
        rd_request_frame();
        take = 1;
        if(event->modifiers & WM_Modifier_Alt)
        {
          ws->menu_bar_focus_press_started = 0;
        }
      }
      
      //- rjf: do fall-through
      if(!take)
      {
        take = 1;
        uishell_cmd("wm_event", .wm_event = event);
      }
      
      //- rjf: take
      if(take)
      {
        wm_eat_event(&events, event);
      }
    }
  }
  
  //////////////////////////////
  //- rjf: loop - consume events in core, tick engine, and repeat
  //
  UIShell_Cmd *cmd = 0;
  ProfScope("loop - consume events in core, tick engine, and repeat") for(U64 cmd_process_loop_idx = 0; cmd_process_loop_idx < 3; cmd_process_loop_idx += 1)
  {
    ////////////////////////////
    //- rjf: unpack basic evaluation context
    //
    ProfBegin("unpack eval-dependent info");
    E_Space primary_space = {0};
    ProfEnd();
    
    ////////////////////////////
    //- rjf: begin evaluation
    //
    e_select_cache(rd_state->eval_cache);
    
    ////////////////////////////
    //- rjf: build base evaluation context
    //
    E_BaseCtx *eval_base_ctx = push_array(scratch.arena, E_BaseCtx, 1);
    {
      E_BaseCtx *ctx = eval_base_ctx;
      ctx->address_arch = Arch_CURRENT;
      
      //- rjf: fill space hooks
      ctx->space_gen   = rd_eval_space_gen;
      ctx->space_read  = rd_eval_space_read;
      ctx->space_write = rd_eval_space_write;
    }
    e_select_base_ctx(eval_base_ctx);
    
    ////////////////////////////
    //- rjf: project the code-declared-settings registry into the
    // `code_defaults` schema (inherited by `user`), if it changed. old trees
    // are intentionally not freed — regens are rare (new call-site
    // registration, write-back) & interned eval types may reference strings
    // in prior trees
    //
    if(rd_state->tweak_schema_built_gen != rd_state->tweak_schema_gen)
    {
      rd_state->tweak_schema_built_gen = rd_state->tweak_schema_gen;
      String8List parts = {0};
      str8_list_push(scratch.arena, &parts, str8_lit("x:{"));
      for(RD_TweakNode *t = rd_state->first_tweak; t != 0; t = t->order_next)
      {
        if(t->has_range)
        {
          str8_list_pushf(scratch.arena, &parts, "@default(%g) @code_default '%S': @range[%g, %g] f32,", t->default_value, t->name, t->range_min, t->range_max);
        }
        else
        {
          str8_list_pushf(scratch.arena, &parts, "@default(%g) @code_default '%S': f32,", t->default_value, t->name);
        }
      }
      str8_list_push(scratch.arena, &parts, str8_lit("}"));
      // NOTE: md trees keep string slices into their source text, so the
      // generated schema string must share the tree's lifetime
      String8 schema_string = str8_list_join(rd_state->tweak_schema_arena, &parts, 0);
      rd_state->tweak_schema_node->schema = md_tree_from_string(rd_state->tweak_schema_arena, schema_string)->first;
    }

    ////////////////////////////
    //- rjf: build extra types & maps
    //
    E_String2ExprMap *macro_map = push_array(scratch.arena, E_String2ExprMap, 1);
    macro_map[0] = e_string2expr_map_make(scratch.arena, 512);
    rd_state->meta_name2type_map = push_array(rd_frame_arena(), E_String2TypeKeyMap, 1);
    rd_state->meta_name2type_map[0] = e_string2typekey_map_make(rd_frame_arena(), 256);
    EV_ExpandRuleTable *expand_rule_table = push_array(scratch.arena, EV_ExpandRuleTable, 1);
    rd_state->view_ui_rule_map = rd_view_ui_rule_map_make(scratch.arena, 512);
    ProfScope("build extra types & maps")
    {
      uishell_eval_register_query_macros(scratch.arena, rd_frame_arena(), macro_map, rd_state->meta_name2type_map);
      
      //- rjf: add macros for evallable top-level individual config entity trees -
      // things with names either explicitly attached, or that we can infer
      for EachElement(idx, RD_APP_NAME_SCHEMA_INFO_TABLE)
      {
        String8 name = RD_APP_NAME_SCHEMA_INFO_TABLE[idx].name;
        MD_NodePtrList schemas = cfg_schemas_from_name(scratch.arena, rd_state->cfg_schema_table, name);
        B32 is_individually_evallable = 0;
        for(MD_NodePtrNode *n = schemas.first; n != 0; n = n->next)
        {
          if(md_node_has_child(n->v, str8_lit("label"), 0))
          {
            is_individually_evallable = 1;
            break;
          }
        }
        if(is_individually_evallable)
        {
          E_TypeKey type_key = e_string2typekey_map_lookup(rd_state->meta_name2type_map, name);
          CFG_NodePtrList cfgs = cfg_node_top_level_list_from_string(scratch.arena, name);
          for(CFG_NodePtrNode *n = cfgs.first; n != 0; n = n->next)
          {
            CFG_Node *cfg = n->v;
            String8 label = rd_label_from_cfg(cfg);
            if(label.size != 0)
            {
              E_Space space = rd_eval_space_from_cfg(cfg);
              E_Expr *expr = e_push_expr(scratch.arena, E_ExprKind_LeafOffset, r1u64(0, 0));
              expr->space    = space;
              expr->mode     = E_Mode_Offset;
              expr->type_key = type_key;
              e_string2expr_map_insert(scratch.arena, macro_map, label, expr);
            }
          }
        }
      }
      
      //- rjf: add macros for windows/tabs
      {
        CFG_NodePtrList windows = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("window"));
        for(CFG_NodePtrNode *n = windows.first; n != 0; n = n->next)
        {
          CFG_Node *window = n->v;
          {
            E_TypeKey type_key = e_string2typekey_map_lookup(rd_state->meta_name2type_map, window->string);
            E_Space space = rd_eval_space_from_cfg(window);
            E_Expr *expr = e_push_expr(scratch.arena, E_ExprKind_LeafOffset, r1u64(0, 0));
            expr->space    = space;
            expr->mode     = E_Mode_Offset;
            expr->type_key = type_key;
            e_string2expr_map_insert(scratch.arena, macro_map, push_str8f(scratch.arena, "query:config.$%I64x", window->id), expr);
          }
          UIShell_ControlledSplit root_controlled_split = uishell_root_controlled_split_from_window(scratch.arena, window);
          UIShell_WorkspaceMount *workspace_mount = uishell_controlled_split_selected_mount(&root_controlled_split);
          CFG_PanelTree panel_tree = workspace_mount->panel_tree;
          for(CFG_PanelNode *p = panel_tree.root;
              p != &cfg_nil_panel_node;
              p = cfg_panel_node_rec__depth_first_pre(panel_tree.root, p).next)
          {
            for(CFG_NodePtrNode *tab_n = p->tabs.first; tab_n != 0; tab_n = tab_n->next)
            {
              CFG_Node *tab = tab_n->v;
              E_TypeKey type_key = e_string2typekey_map_lookup(rd_state->meta_name2type_map, tab->string);
              E_Space space = rd_eval_space_from_cfg(tab);
              E_Expr *expr = e_push_expr(scratch.arena, E_ExprKind_LeafOffset, r1u64(0, 0));
              expr->space    = space;
              expr->mode     = E_Mode_Offset;
              expr->type_key = type_key;
              e_string2expr_map_insert(scratch.arena, macro_map, push_str8f(scratch.arena, "query:config.$%I64x", tab->id), expr);
            }
          }
        }
      }
      
      //- rjf: add macros for user/project
      {
        E_TypeKey type_key = e_string2typekey_map_lookup(rd_state->meta_name2type_map, str8_lit("user"));
        E_Space space = rd_eval_space_from_cfg(cfg_node_child_from_string(cfg_node_root(), str8_lit("user")));
        E_Expr *expr = e_push_expr(scratch.arena, E_ExprKind_LeafOffset, r1u64(0, 0));
        expr->space    = space;
        expr->mode     = E_Mode_Offset;
        expr->type_key = type_key;
        e_string2expr_map_insert(scratch.arena, macro_map, str8_lit("user_settings"), expr);
      }
      {
        E_TypeKey type_key = e_string2typekey_map_lookup(rd_state->meta_name2type_map, str8_lit("project"));
        E_Space space = rd_eval_space_from_cfg(cfg_node_child_from_string(cfg_node_root(), str8_lit("project")));
        E_Expr *expr = e_push_expr(scratch.arena, E_ExprKind_LeafOffset, r1u64(0, 0));
        expr->space    = space;
        expr->mode     = E_Mode_Offset;
        expr->type_key = type_key;
        e_string2expr_map_insert(scratch.arena, macro_map, str8_lit("project_settings"), expr);
      }
      
      //- rjf: add types for sets
      {
        e_string2typekey_map_insert(rd_frame_arena(), rd_state->meta_name2type_map, str8_lit("environment"),
                                    e_type_key_cons(.kind = E_TypeKind_Set,
                                                    .name = str8_lit("environment"),
                                                    .irext  = E_TYPE_IREXT_FUNCTION_NAME(environment),
                                                    .access = E_TYPE_ACCESS_FUNCTION_NAME(environment),
                                                    .expand =
                                                    {
                                                      .info        = E_TYPE_EXPAND_INFO_FUNCTION_NAME(environment),
                                                      .range       = E_TYPE_EXPAND_RANGE_FUNCTION_NAME(environment),
                                                      .id_from_num = E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_NAME(environment),
                                                      .num_from_id = E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_NAME(environment),
                                                    }));
        e_string2typekey_map_insert(rd_frame_arena(), rd_state->meta_name2type_map, str8_lit("theme_colors"),
                                    e_type_key_cons(.kind = E_TypeKind_Set,
                                                    .flags = E_TypeFlag_StubSingleLineExpansion,
                                                    .name = str8_lit("theme_colors"),
                                                    .irext  = E_TYPE_IREXT_FUNCTION_NAME(cfgs_slice),
                                                    .access = E_TYPE_ACCESS_FUNCTION_NAME(cfgs_slice),
                                                    .expand =
                                                    {
                                                      .info        = E_TYPE_EXPAND_INFO_FUNCTION_NAME(cfgs_query),
                                                      .range       = E_TYPE_EXPAND_RANGE_FUNCTION_NAME(cfgs_slice),
                                                      .id_from_num = E_TYPE_EXPAND_ID_FROM_NUM_FUNCTION_NAME(cfgs_slice),
                                                      .num_from_id = E_TYPE_EXPAND_NUM_FROM_ID_FUNCTION_NAME(cfgs_slice),
                                                    }));
      }
      
      //- rjf: add macro for output log
      {
        Access *access = access_open();
        C_Key key = rd_state->shell_output_key;
        U128 hash = c_hash_from_key(key, 0);
        String8 data = c_data_from_hash(access, hash);
        E_Space space = e_space_make(E_SpaceKind_HashStoreKey);
        space.u64_0 = key.root.u64[0];
        space.u128 = key.id.u128[0];
        E_Expr *expr = e_push_expr(scratch.arena, E_ExprKind_LeafOffset, r1u64(0, 0));
        expr->space    = space;
        expr->mode     = E_Mode_Offset;
        expr->type_key = e_type_key_cons_array(e_type_key_basic(E_TypeKind_U8), data.size, 0);
        e_string2expr_map_insert(scratch.arena, macro_map, str8_lit("output"), expr);
        access_close(access);
      }
      
      //- rjf: (DEBUG) add macro for cfg strings
#if 0
      {
        struct
        {
          C_Key key;
          String8 name;
        }
        table[] =
        {
          {rd_state->user_cfg_string_key, str8_lit("uishell_user_data")},
          {rd_state->project_cfg_string_key, str8_lit("uishell_project_data")},
          {rd_state->cmdln_cfg_string_key, str8_lit("uishell_command_line_data")},
          {rd_state->transient_cfg_string_key, str8_lit("uishell_transient_data")},
        };
        for EachElement(idx, table)
        {
          Access *access = access_open();
          C_Key key = table[idx].key;
          U128 hash = c_hash_from_key(key, 0);
          String8 data = c_data_from_hash(access, hash);
          E_Space space = e_space_make(E_SpaceKind_HashStoreKey);
          E_Expr *expr = e_push_expr(scratch.arena, E_ExprKind_LeafOffset, r1u64(0, 0));
          space.u64_0 = key.root.u64[0];
          space.u128 = key.id.u128[0];
          expr->space    = space;
          expr->mode     = E_Mode_Offset;
          expr->type_key = e_type_key_cons_array(e_type_key_basic(E_TypeKind_U8), data.size, 0);
          e_string2expr_map_insert(scratch.arena, macro_map, table[idx].name, expr);
          access_close(access);
        }
      }
#endif
      
      //- rjf: choose set of lenses
      // TODO(rjf): @lenses generate via metaprogram
      struct
      {
        String8 name;
        B32 inherited_by_members;
        B32 inherited_by_elements;
        B32 array_like;
        E_TypeIRExtFunctionType *irext;
        E_TypeAccessFunctionType *access;
        E_TypeExpandRule expand;
        RD_ViewUIFunctionType *ui;
        EV_ExpandRuleInfoHookFunctionType *ev_expand;
      }
      lens_table[] =
      {
        {str8_lit("raw"),         0, 0, 0,        0, 0, {0}},
        {str8_lit("bin"),         1, 1, 0,        0, 0, {0}},
        {str8_lit("oct"),         1, 1, 0,        0, 0, {0}},
        {str8_lit("dec"),         1, 1, 0,        0, 0, {0}},
        {str8_lit("hex"),         1, 1, 0,        0, 0, {0}},
        {str8_lit("digits"),      1, 1, 0,        0, 0, {0}},
        {str8_lit("no_string"),   1, 1, 0,        0, 0, {0}},
        {str8_lit("no_char"),     1, 1, 0,        0, 0, {0}},
        {str8_lit("no_addr"),     1, 1, 0,        0, 0, {0}},
        {str8_lit("sequence"),    0, 0, 1,        0, 0, {E_TYPE_EXPAND_INFO_FUNCTION_NAME(sequence), E_TYPE_EXPAND_RANGE_FUNCTION_NAME(sequence)}},
        {str8_lit("rows"),        0, 0, 0,        0, 0, {E_TYPE_EXPAND_INFO_FUNCTION_NAME(rows), E_TYPE_EXPAND_RANGE_FUNCTION_NAME(rows)}},
        {str8_lit("columns"),     0, 0, 0,        0, 0, {0}},
        {str8_lit("flatten"),     0, 0, 0,        0, 0, {0}},
        {str8_lit("omit"),        0, 0, 0,        0, 0, {E_TYPE_EXPAND_INFO_FUNCTION_NAME(omit), E_TYPE_EXPAND_RANGE_FUNCTION_NAME(omit)}},
        {str8_lit("range1"),      0, 0, 0,        0, 0, {0}},
        {str8_lit("array"),       0, 0, 1,        0, 0, {E_TYPE_EXPAND_INFO_FUNCTION_NAME(array), E_TYPE_EXPAND_RANGE_FUNCTION_NAME(array)}},
        {str8_lit("slice"),       0, 0, 1,        E_TYPE_IREXT_FUNCTION_NAME(slice), E_TYPE_ACCESS_FUNCTION_NAME(slice), {E_TYPE_EXPAND_INFO_FUNCTION_NAME(slice), E_TYPE_EXPAND_RANGE_FUNCTION_NAME(slice)}},
        {str8_lit("list"),        0, 0, 1,        E_TYPE_IREXT_FUNCTION_NAME(list), E_TYPE_ACCESS_FUNCTION_NAME(list), {E_TYPE_EXPAND_INFO_FUNCTION_NAME(list), E_TYPE_EXPAND_RANGE_FUNCTION_NAME(list)}},
      };
      
      //- rjf: fill lenses in ev expand rule map, rd view ui rule map
      {
        uishell_register_view_ui_rules(scratch.arena, rd_state->view_ui_rule_map);
        uishell_register_expand_rule_infos(scratch.arena, expand_rule_table);
      }
      
      //- rjf: fill macros w/ types for lenses
      for EachElement(idx, lens_table)
      {
        E_TypeFlags type_flags = 0;
        if(lens_table[idx].inherited_by_members)
        {
          type_flags |= E_TypeFlag_InheritedByMembers;
        }
        if(lens_table[idx].inherited_by_elements)
        {
          type_flags |= E_TypeFlag_InheritedByElements;
        }
        if(lens_table[idx].array_like)
        {
          type_flags |= E_TypeFlag_ArrayLikeExpansion;
        }
        E_Expr *expr = e_push_expr(scratch.arena, E_ExprKind_LeafOffset, r1u64(0, 0));
        expr->type_key = e_type_key_cons(.kind = E_TypeKind_LensSpec,
                                         .flags = type_flags,
                                         .name = lens_table[idx].name,
                                         .irext = lens_table[idx].irext,
                                         .access = lens_table[idx].access,
                                         .expand = lens_table[idx].expand);
        e_string2expr_map_insert(scratch.arena, macro_map, lens_table[idx].name, expr);
      }
    }
    ev_select_expand_rule_table(expand_rule_table);
    
    ////////////////////////////
    //- rjf: build IR evaluation context
    //
    E_IRCtx *ir_ctx = push_array(scratch.arena, E_IRCtx, 1);
    {
      E_IRCtx *ctx = ir_ctx;
      ctx->macro_map      = macro_map;
    }
    e_select_ir_ctx(ir_ctx);
    
    ////////////////////////////
    //- rjf: build eval interpretation context
    //
    E_InterpretCtx *interpret_ctx = push_array(scratch.arena, E_InterpretCtx, 1);
    {
      E_InterpretCtx *ctx = interpret_ctx;
      ctx->primary_space     = primary_space;
    }
    e_select_interpret_ctx(interpret_ctx);
    
    ////////////////////////////
    //- rjf: evaluate unpacked settings (must be used earlier than this point in the frame,
    // but cannot evaluate before this point, so we need to prep for next frame
    //
    rd_state->alt_menu_bar_enabled = rd_setting_b32_from_name(str8_lit("focus_menu_bar_with_alt"));
    rd_state->eval_viz_base_string_flags = 0;
    if(rd_setting_b32_from_name(str8_lit("display_pointer_addresses_before_contents")))
    {
      rd_state->eval_viz_base_string_flags |= EV_StringFlag_AddressesBeforeContent;
    }
    rd_state->eval_viz_base_string_flags |= EV_StringFlag_DisplayAddressUnmappedStatus;
    
    ////////////////////////////
    //- rjf: autosave if needed
    //
    {
      rd_state->seconds_until_autosave -= rd_state->frame_dt;
      if(rd_state->seconds_until_autosave <= 0.f)
      {
        UISHELL_APP_AUTOSAVE();
        rd_state->seconds_until_autosave = 5.f;
      }
    }
    
    ////////////////////////////
    //- rjf: process top-level graphical commands
    //
    if(rd_state->frame_depth == 1) ProfScope("process top-level graphical commands")
    {
      for(;uishell_next_cmd(&cmd);) UIShell_RegsScope()
      {
        // rjf: unpack command
        MemoryCopyStruct(uishell_regs(), cmd->regs);
        
        // rjf: request frame
        rd_request_frame();
        
        // rjf: process command
        for(UIShell_CmdPack *pack = rd_state->first_cmd_pack; pack != 0; pack = pack->next)
        {
          if(pack->dispatch != 0 && pack->dispatch(cmd->name))
          {
            break;
          }
        }
      }
    }
    
    U64 cmd_count_pre_tick = rd_state->cmds[0].count;
    
    ////////////////////////////
    //- rjf: early-out if no new commands
    //
    if(rd_state->cmds[0].count == cmd_count_pre_tick)
    {
      break;
    }
  }
  
	  //////////////////////////////
  //- rjf: update window titles
  //
  if(rd_state->frame_depth == 1)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 window_title = rd_push_window_title(scratch.arena);
    if(!str8_match(window_title, rd_state->last_window_title, 0))
    {
      for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
      {
        wm_window_set_title(ws->os, window_title);
      }
    }
    rd_state->last_window_title = str8_copy(rd_frame_arena(), window_title);
    scratch_end(scratch);
  }
  
  ////////////////////////////
  //- rjf: rotate command slots, bump command gen counter
  //
  // in this step, we rotate the ring buffer of command batches (command
  // arenas & lists). when the cmds_gen (the position of the ring buffer)
  // is even, the command queue is in a "read/write" mode, and this is uniquely
  // usable by the core - this is done so that commands in the core can push
  // other commands, and have those other commands processed on the same frame.
  //
  // in view code, however, they can only use the current command queue in a
  // "read only" mode, because new commands pushed by those views must be
  // processed first by the core. so, before calling into view code, the
  // cmds_gen is incremented to be *odd*. this way, the views will *write*
  // commands into the 0 slot, but *read* from the 1 slot (which will contain
  // this frame's commands).
  //
  // after view code runs, the generation number is incremented back to even.
  // the commands pushed by the view will be in the queue, and the core can
  // treat that queue as r/w again.
  //
  if(rd_state->frame_depth == 1)
  {
    // rjf: rotate
    {
      Arena *first_arena = rd_state->cmds_arenas[0];
      UIShell_CmdList first_cmds = rd_state->cmds[0];
      MemoryCopy(rd_state->cmds_arenas,
                 rd_state->cmds_arenas+1,
                 sizeof(rd_state->cmds_arenas[0])*(ArrayCount(rd_state->cmds_arenas)-1));
      MemoryCopy(rd_state->cmds,
                 rd_state->cmds+1,
                 sizeof(rd_state->cmds[0])*(ArrayCount(rd_state->cmds)-1));
      rd_state->cmds_arenas[ArrayCount(rd_state->cmds_arenas)-1] = first_arena;
      rd_state->cmds[ArrayCount(rd_state->cmds_arenas)-1] = first_cmds;
    }
    
    // rjf: clear next batch
    {
      arena_clear(rd_state->cmds_arenas[0]);
      MemoryZeroStruct(&rd_state->cmds[0]);
    }
    
    // rjf: bump
    {
      rd_state->cmds_gen += 1;
    }
  }
  
  //////////////////////////////
  //- rjf: compute all ambiguous paths from view titles
  //
  ProfScope("compute all ambiguous paths from view titles")
  {
    Temp scratch = scratch_begin(0, 0);
    rd_state->ambiguous_path_slots_count = 512;
    rd_state->ambiguous_path_slots = push_array(rd_frame_arena(), RD_AmbiguousPathNode *, rd_state->ambiguous_path_slots_count);
    for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
    {
      CFG_Node *window = cfg_node_from_id(ws->cfg_id);
      UIShell_ControlledSplit root_controlled_split = uishell_root_controlled_split_from_window(scratch.arena, window);
      UIShell_WorkspaceMount *workspace_mount = uishell_controlled_split_selected_mount(&root_controlled_split);
      CFG_PanelTree panel_tree = workspace_mount->panel_tree;
      for(CFG_PanelNode *p = panel_tree.root; p != &cfg_nil_panel_node; p = cfg_panel_node_rec__depth_first_pre(panel_tree.root, p).next)
      {
        for(CFG_NodePtrNode *tab_n = p->tabs.first; tab_n != 0; tab_n = tab_n->next)
        {
          CFG_Node *tab = tab_n->v;
          if(rd_cfg_is_project_filtered(tab))
          {
            continue;
          }
          UIShell_RegsScope(.tab = tab->id, .view = tab->id)
          {
            String8 eval_string = rd_expr_from_cfg(tab);
            String8 file_path = rd_file_path_from_eval_string(scratch.arena, eval_string);
            if(file_path.size != 0)
            {
              String8 name = str8_skip_last_slash(file_path);
              U64 hash = u64_djb2_hash_from_str8__case_insensitive(name);
              U64 slot_idx = hash%rd_state->ambiguous_path_slots_count;
              RD_AmbiguousPathNode *node = 0;
              for(RD_AmbiguousPathNode *n = rd_state->ambiguous_path_slots[slot_idx];
                  n != 0;
                  n = n->next)
              {
                if(str8_match(n->name, name, StringMatchFlag_CaseInsensitive))
                {
                  node = n;
                  break;
                }
              }
              if(node == 0)
              {
                node = push_array(rd_frame_arena(), RD_AmbiguousPathNode, 1);
                SLLStackPush(rd_state->ambiguous_path_slots[slot_idx], node);
                node->name = push_str8_copy(rd_frame_arena(), name);
              }
              str8_list_push(rd_frame_arena(), &node->paths, push_str8_copy(rd_frame_arena(), file_path));
            }
          }
        }
      }
    }
    scratch_end(scratch);
  }
  
  //////////////////////////////
  //- rjf: compute animation rates, given config
  //
  {
    F32 master_animations_f    = (F32)!!rd_setting_b32_from_name(str8_lit("animations"));
    F32 scrolling_animations_f = (F32)!!rd_setting_b32_from_name(str8_lit("scrolling_animations"));
    F32 tooltip_animations_f   = (F32)!!rd_setting_b32_from_name(str8_lit("tooltip_animations"));
    F32 menu_animations_f      = (F32)!!rd_setting_b32_from_name(str8_lit("menu_animations"));
    // animation-speed multiplier scales every decay constant (lower = slower).
    // the schema surfaces [0.1, 3.0] in the UI; this clamp is just a defensive
    // floor/ceiling for hand-edited configs — off zero so animations never
    // freeze, and a sane ceiling above the surfaced range.
    F32 anim_speed = Clamp(0.1f, rd_setting_f32_from_name(str8_lit("animation_speed")), 8.f);
    rd_state->catchall_animation_rate     = 1 - master_animations_f*pow_f32(2, (-60.f * anim_speed * rd_state->frame_dt));
    rd_state->menu_animation_rate         = 1 - master_animations_f*menu_animations_f*pow_f32(2, (-70.f * anim_speed * rd_state->frame_dt));
    rd_state->menu_animation_rate__slow   = 1 - master_animations_f*menu_animations_f*pow_f32(2, (-50.f * anim_speed * rd_state->frame_dt));
    rd_state->entity_alive_animation_rate = 1 - master_animations_f*menu_animations_f*pow_f32(2, (-30.f * anim_speed * rd_state->frame_dt));
    rd_state->rich_hover_animation_rate   = 1 - master_animations_f*menu_animations_f*pow_f32(2, (-50.f * anim_speed * rd_state->frame_dt));
    rd_state->scrolling_animation_rate    = 1 - master_animations_f*scrolling_animations_f*pow_f32(2, (-60.f * anim_speed * rd_state->frame_dt));
    rd_state->tooltip_animation_rate      = 1 - master_animations_f*tooltip_animations_f*pow_f32(2, (-60.f * anim_speed * rd_state->frame_dt));
  }
  
  //////////////////////////////
  //- rjf: animate confirmation
  //
  {
    F32 rate = rd_setting_b32_from_name(str8_lit("menu_animations")) ? 1 - pow_f32(2, (-30.f * rd_state->frame_dt)) : 1.f;
    B32 popup_open = rd_state->popup_active;
    rd_state->popup_t += rate * ((F32)!!popup_open-rd_state->popup_t);
    if(abs_f32(rd_state->popup_t - (F32)!!popup_open) > 0.005f)
    {
      rd_request_frame();
    }
  }
  
  //////////////////////////////
  //- rjf: update/render all windows
  //
  {
    dr_begin_frame(rd_font_from_slot(RD_FontSlot_Icons));
    CFG_NodePtrList windows = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("window"));
    for(CFG_NodePtrNode *n = windows.first; n != 0; n = n->next)
    {
      CFG_Node *window = n->v;
      RD_WindowState *w = rd_window_state_from_cfg(window);
      B32 window_is_focused = wm_window_is_focused(w->os);
      if(window_is_focused)
      {
        rd_state->last_focused_window = w->cfg_id;
      }
      uishell_push_regs();
      uishell_regs()->window = w->cfg_id;
      rd_window_frame();
      if(rd_state->frame_diagnostic != 0)
      { abort_self(rd_state->frame_diagnostic(w) ? 0 : 1); }
      MemoryZeroStruct(&w->ui_events);
      UIShell_Regs *window_regs = uishell_pop_regs();
      if(rd_state->last_focused_window == w->cfg_id)
      {
        MemoryCopyStruct(uishell_regs(), window_regs);
      }
    }
  }
  
  //////////////////////////////
  //- rjf: garbage collect untouched window states
  //
  {
    for EachIndex(slot_idx, rd_state->window_state_slots_count)
    {
      for(RD_WindowState *ws = rd_state->window_state_slots[slot_idx].first, *next; ws != 0; ws = next)
      {
        next = ws->hash_next;
        CFG_Node *cfg = cfg_node_from_id(ws->cfg_id);
        if(cfg == &cfg_nil_node || ws->last_frame_index_touched < rd_state->frame_index || rd_state->quit)
        {
          uishell_sidebar_release(ws->sidebar);
          ui_state_release(ws->ui);
          r_window_unequip(ws->os, ws->r);
          wm_window_close(ws->os);
          arena_release(ws->drop_completion_arena);
          arena_release(ws->query_arena);
          arena_release(ws->hover_eval_arena);
          arena_release(ws->autocomp_arena);
          arena_release(ws->arena);
          DLLRemove_NPZ(&rd_nil_window_state, rd_state->first_window_state, rd_state->last_window_state, ws, order_next, order_prev);
          DLLRemove_NP(rd_state->window_state_slots[slot_idx].first, rd_state->window_state_slots[slot_idx].last, ws, hash_next, hash_prev);
          SLLStackPush_N(rd_state->free_window_state, ws, order_next);
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: simulate lag
  //
  if(DEV_simulate_lag)
  {
    sleep_ms(300);
  }
  
  //////////////////////////////
  //- rjf: end drag/drop if needed
  //
  if(rd_state->drag_drop_state == RD_DragDropState_Dropping)
  {
    rd_state->drag_drop_state = RD_DragDropState_Null;
  }
  
  //////////////////////////////
  //- rjf: clear frame request state
  //
  if(rd_state->num_frames_requested > 0)
  {
    rd_state->num_frames_requested -= 1;
  }
  
  //////////////////////////////
  //- rjf: close frame scopes
  //
  // NOTE(rjf): this always must happen before the refresh, since that
  // will sleep for vsync, and we do not want to hold handles for long,
  // since eviction threads may be waiting to get rid of stuff.
  //
  access_close(rd_state->frame_access);
  rd_state->frame_access = frame_access_restore;
  
  //////////////////////////////
  //- rjf: submit rendering to all windows
  //
  ProfScope("submit rendering to all windows")
  {
    r_begin_frame();
    for(RD_WindowState *w = rd_state->first_window_state; w != &rd_nil_window_state; w = w->order_next)
    {
      r_window_begin_frame(w->os, w->r);
      dr_submit_bucket(w->os, w->r, w->draw_bucket);
      r_window_end_frame(w->os, w->r);
    }
    r_end_frame();
  }
  
  //////////////////////////////
  //- rjf: show windows after first frame
  //
  if(rd_state->frame_depth == 1)
  {
    CFG_IDList windows_to_show = {0};
    for(RD_WindowState *w = rd_state->first_window_state; w != &rd_nil_window_state; w = w->order_next)
    {
      if(w->frames_alive == 1)
      {
        cfg_id_list_push(scratch.arena, &windows_to_show, w->cfg_id);
      }
    }
    for(CFG_IDNode *n = windows_to_show.first; n != 0; n = n->next)
    {
      CFG_Node *window = cfg_node_from_id(n->v);
      RD_WindowState *ws = rd_window_state_from_cfg(window);
      wm_window_first_paint(ws->os);
    }
  }
  
  //////////////////////////////
  //- rjf: determine frame time, record into history
  //
  U64 end_time_us = now_time_us();
  U64 frame_time_us = end_time_us-begin_time_us;
  rd_state->frame_time_us_history[rd_state->frame_index%ArrayCount(rd_state->frame_time_us_history)] = frame_time_us;
  
  //////////////////////////////
  //- rjf: [windows] clear pages from working set shortly after startup, many of which will not be needed
  //
#if OS_WINDOWS
  if(rd_state->frame_index == 15) ProfScope("SetProcessWorkingSetSize")
  {
    SetProcessWorkingSetSize(GetCurrentProcess(), max_U64, max_U64);
  }
#endif
  
  //////////////////////////////
  //- rjf: bump frame time counters
  //
  rd_state->frame_index += 1;
  rd_state->time_in_seconds += rd_state->frame_dt;
  rd_state->time_in_us += frame_time_us;
  
  //////////////////////////////
  //- rjf: bump command batch ring buffer generation
  //
  if(rd_state->frame_depth == 1)
  {
    rd_state->cmds_gen += 1;
  }
  
  //////////////////////////////
  //- rjf: collect logs
  //
  ProfScope("collect logs")
  {
    LogScopeResult log = log_scope_end(scratch.arena);
    append_data_to_file_path(rd_state->log_path, log.strings[LogMsgKind_Info]);
    if(log.strings[LogMsgKind_UserError].size != 0)
    {
      String8 error_log = log.strings[LogMsgKind_UserError];
      String8 error_log_file = push_str8f(scratch.arena, "user_errors:\n{\n%S}\n", error_log);
      append_data_to_file_path(rd_state->log_path, error_log_file);
      String8List error_log_lines = str8_split(scratch.arena, error_log, (U8 *)"\n", 1, 0);
      String8 error_log_string = str8_list_join(scratch.arena, &error_log_lines, &(StringJoin){.sep = str8_lit(" ")});
      for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
      {
        ws->error_string_size = Min(sizeof(ws->error_buffer), error_log_string.size);
        MemoryCopy(ws->error_buffer, error_log_string.str, ws->error_string_size);
        ws->error_t = 1.f;
      }
    }
  }
  
  rd_state->frame_depth -= 1;
  scratch_end(scratch);
  ProfEnd();
}
