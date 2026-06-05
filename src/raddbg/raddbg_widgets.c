// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: UI Widgets: Fancy Title Strings

internal DR_FStrList
rd_title_fstrs_from_cfg(Arena *arena, CFG_Node *cfg, B32 include_extras)
{
  DR_FStrList result = {0};
  {
    Temp scratch = scratch_begin(&arena, 1);
    
    //- rjf: unpack config
    B32 is_disabled = rd_disabled_from_cfg(cfg);
    RD_Location loc = rd_location_from_cfg(cfg);
    String8 name_string = rd_name_from_cfg(cfg);
    String8 label_string = rd_label_from_cfg(cfg);
    String8 expr_string = rd_expr_from_cfg(cfg);
    String8 collection_name = {0};
    String8 file_path = rd_path_from_cfg(cfg);
    Vec4F32 rgba = rd_color_from_cfg(cfg);
    if(rgba.w == 0)
    {
      rgba = ui_color_from_name(str8_lit("text"));
    }
    Vec4F32 rgba_secondary = rgba;
    UI_TagF("weak")
    {
      rgba_secondary = ui_color_from_name(str8_lit("text"));
    }
    RD_IconKind icon_kind = rd_icon_kind_from_code_name(cfg->string);
    B32 is_from_command_line = 0;
    {
      CFG_Node *cmd_line_root = cfg_node_child_from_string(cfg_node_root(), str8_lit("command_line"));
      for(CFG_Node *p = cfg->parent; p != &cfg_nil_node; p = p->parent)
      {
        if(p == cmd_line_root)
        {
          is_from_command_line = 1;
          break;
        }
      }
    }
    B32 is_within_window = 0;
    {
      for(CFG_Node *p = cfg->parent; p != &cfg_nil_node; p = p->parent)
      {
        if(str8_match(p->string, str8_lit("window"), 0))
        {
          is_within_window = 1;
          break;
        }
      }
    }
    if(expr_string.size != 0)
    {
      String8 query_name = rd_query_from_eval_string(arena, expr_string);
      if(query_name.size != 0)
      {
        String8 query_code_name = query_name;
        String8 query_display_name = rd_display_from_code_name(query_code_name);
        collection_name = query_display_name;
        if(query_display_name.size == 0)
        {
          query_code_name = rd_singular_from_code_name_plural(query_name);
          collection_name = rd_display_plural_from_code_name(query_code_name);
        }
        RD_IconKind query_icon_kind = rd_icon_kind_from_code_name(query_code_name);
        if(query_icon_kind != RD_IconKind_Null)
        {
          icon_kind = query_icon_kind;
        }
      }
      else
      {
        file_path = rd_file_path_from_eval_string(arena, expr_string);
        if(file_path.size != 0)
        {
          icon_kind = RD_IconKind_FileOutline;
        }
      }
    }
    
    //- rjf: set up color/size for all parts of the title
    //
    // the "running" part implies that it changes as things are added - 
    // so if a primary title is pushed, we can make the rest of the title
    // more faded/smaller, but only after a primary title is pushed,
    // which could be caused by many different potential parts of a cfg.
    //
    DR_FStrParams params = {rd_font_from_slot(RD_FontSlot_Main), rd_raster_flags_from_slot(RD_FontSlot_Main), rgba, ui_top_font_size()};
    B32 running_is_secondary = 0;
#define start_secondary() if(!running_is_secondary){running_is_secondary = 1; params.color = rgba_secondary; params.size = ui_top_font_size()*0.95f;}
    
    //- rjf: disabled? -> soften color
    if(is_disabled)
    {
      params.color = rgba_secondary;
    }
    
    //- rjf: push icon
    if(icon_kind != RD_IconKind_Null)
    {
      dr_fstrs_push_new(arena, &result, &params, rd_icon_kind_text_table[icon_kind], .font = rd_font_from_slot(RD_FontSlot_Icons), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Icons), .color = rgba_secondary);
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
    }
    
    //- rjf: push warning icon for command-line entities
    if(is_from_command_line)
    {
      dr_fstrs_push_new(arena, &result, &params, rd_icon_kind_text_table[RD_IconKind_Info], .font = rd_font_from_slot(RD_FontSlot_Icons), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Icons), .color = rgba_secondary);
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
    }
    
    //- rjf: push view title, if from window, and no file path, and no label
    if(is_within_window && file_path.size == 0 && collection_name.size == 0 && label_string.size == 0)
    {
      String8 view_display_name = rd_display_from_code_name(cfg->string);
      if(view_display_name.size != 0)
      {
        dr_fstrs_push_new(arena, &result, &params, view_display_name);
        dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
        start_secondary();
      }
    }
    
    //- rjf: push bucket name
    if(cfg->parent == cfg_node_root())
    {
      if(str8_match(cfg->string, str8_lit("user"), 0))
      {
        dr_fstrs_push_new(arena, &result, &params, str8_lit("User"), .font = rd_font_from_slot(RD_FontSlot_Main), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Main));
        dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
        start_secondary();
      }
      else if(str8_match(cfg->string, str8_lit("project"), 0))
      {
        dr_fstrs_push_new(arena, &result, &params, str8_lit("Project"), .font = rd_font_from_slot(RD_FontSlot_Main), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Main));
        dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
        start_secondary();
      }
    }
    
    //- rjf: push name
    if(name_string.size != 0)
    {
      dr_fstrs_push_new(arena, &result, &params, name_string);
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
      start_secondary();
    }
    
    //- rjf: push label
    if(label_string.size != 0)
    {
      dr_fstrs_push_new(arena, &result, &params, label_string, .font = rd_font_from_slot(RD_FontSlot_Code), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Code));
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
      start_secondary();
    }
    
    //- rjf: push collection name
    if(collection_name.size != 0)
    {
      dr_fstrs_push_new(arena, &result, &params, collection_name);
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
      start_secondary();
    }
    
    //- rjf: query is file path - do specific file name strings
    else if(file_path.size != 0)
    {
      // rjf: compute disambiguated file name
      String8List qualifiers = {0};
      String8 file_name = str8_skip_last_slash(file_path);
      if(rd_state->ambiguous_path_slots_count != 0)
      {
        U64 hash = u64_djb2_hash_from_str8__case_insensitive(file_name);
        U64 slot_idx = hash%rd_state->ambiguous_path_slots_count;
        RD_AmbiguousPathNode *node = 0;
        {
          for(RD_AmbiguousPathNode *n = rd_state->ambiguous_path_slots[slot_idx];
              n != 0;
              n = n->next)
          {
            if(str8_match(n->name, file_name, StringMatchFlag_CaseInsensitive))
            {
              node = n;
              break;
            }
          }
        }
        if(node != 0 && node->paths.node_count > 1)
        {
          // rjf: get all colliding paths
          String8Array collisions = str8_array_from_list(scratch.arena, &node->paths);
          
          // rjf: get all reversed path parts for each collision
          String8List *collision_parts_reversed = push_array(scratch.arena, String8List, collisions.count);
          for EachIndex(idx, collisions.count)
          {
            String8List parts = str8_split_path(scratch.arena, collisions.v[idx]);
            for(String8Node *n = parts.first; n != 0; n = n->next)
            {
              str8_list_push_front(scratch.arena, &collision_parts_reversed[idx], n->string);
            }
          }
          
          // rjf: get the search path & its reversed parts
          String8List parts = str8_split_path(scratch.arena, file_path);
          String8List parts_reversed = {0};
          for(String8Node *n = parts.first; n != 0; n = n->next)
          {
            str8_list_push_front(scratch.arena, &parts_reversed, n->string);
          }
          
          // rjf: iterate all collision part reversed lists, in lock-step with
          // search path; disqualify until we only have one path remaining; gather
          // qualifiers
          {
            U64 num_collisions_left = collisions.count;
            String8Node **collision_nodes = push_array(scratch.arena, String8Node *, collisions.count);
            for EachIndex(idx, collisions.count)
            {
              collision_nodes[idx] = collision_parts_reversed[idx].first;
            }
            for(String8Node *n = parts_reversed.first; num_collisions_left > 1 && n != 0; n = n->next)
            {
              B32 part_is_qualifier = 0;
              for EachIndex(idx, collisions.count)
              {
                if(collision_nodes[idx] != 0 && !str8_match(collision_nodes[idx]->string, n->string, StringMatchFlag_CaseInsensitive))
                {
                  collision_nodes[idx] = 0;
                  num_collisions_left -= 1;
                  part_is_qualifier = 1;
                }
                else if(collision_nodes[idx] != 0)
                {
                  collision_nodes[idx] = collision_nodes[idx]->next;
                }
              }
              if(part_is_qualifier)
              {
                str8_list_push_front(scratch.arena, &qualifiers, n->string);
              }
            }
          }
        }
      }
      
      // rjf: push qualifiers
      if(qualifiers.node_count != 0) UI_TagF("weak")
      {
        for(String8Node *n = qualifiers.first; n != 0; n = n->next)
        {
          String8 string = push_str8f(arena, "<%S> ", n->string);
          dr_fstrs_push_new(arena, &result, &params, string, .color = ui_color_from_name(str8_lit("text")));
        }
      }
      
      // rjf: push file name
      dr_fstrs_push_new(arena, &result, &params, push_str8_copy(arena, str8_skip_last_slash(file_path)));
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
      start_secondary();
    }
    
    //- rjf: cfg has expression attached -> use that
    else if(expr_string.size != 0)
    {
      dr_fstrs_push_new(arena, &result, &params, expr_string, .font = rd_font_from_slot(RD_FontSlot_Code), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Code));
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
      start_secondary();
    }
    
    //- rjf: push text location
    if(loc.file_path.size != 0)
    {
      String8 path = loc.file_path;
      if(!include_extras)
      {
        path = str8_skip_last_slash(loc.file_path);
      }
      String8 location_string = push_str8f(arena, "%S:%I64d:%I64d", path, loc.pt.line, loc.pt.column);
      dr_fstrs_push_new(arena, &result, &params, location_string);
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
      start_secondary();
    }
    
    //- rjf: push address location
    if(loc.expr.size != 0)
    {
      RD_Font(RD_FontSlot_Code)
      {
        DR_FStrList fstrs = rd_fstrs_from_code_string(arena, 1.f, 0, params.color, loc.expr);
        dr_fstrs_concat_in_place(&result, &fstrs);
      }
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
      start_secondary();
    }
    
    //- rjf: push conditions
    {
      String8 condition = cfg_node_child_from_string(cfg, str8_lit("condition"))->first->string;
      if(condition.size != 0)
      {
        dr_fstrs_push_new(arena, &result, &params, str8_lit("if "), .font = rd_font_from_slot(RD_FontSlot_Code), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Code));
        RD_Font(RD_FontSlot_Code)
        {
          DR_FStrList fstrs = rd_fstrs_from_code_string(arena, 1.f, 0, params.color, condition);
          dr_fstrs_concat_in_place(&result, &fstrs);
        }
        dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
      }
    }
    
    //- rjf: push disabled marker
    if(is_disabled)
    {
      dr_fstrs_push_new(arena, &result, &params, str8_lit("(Disabled)"));
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
    }
    
    //- rjf: push hit count
    {
      String8 hit_count_value_string = cfg_node_child_from_string(cfg, str8_lit("hit_count"))->first->string;
      U64 hit_count = 0;
      if(try_u64_from_str8_c_rules(hit_count_value_string, &hit_count) && hit_count != 0)
      {
        String8 hit_count_text = push_str8f(arena, "(%I64u hit%s)", hit_count, hit_count == 1 ? "" : "s");
        dr_fstrs_push_new(arena, &result, &params, hit_count_text);
      }
    }
    
    //- rjf: special case: colors
    if(str8_match(cfg->string, str8_lit("theme_color"), 0))
    {
      String8 tags = cfg_node_child_from_string(cfg, str8_lit("tags"))->first->string;
      String8 color_string = cfg_node_child_from_string(cfg, str8_lit("value"))->first->string;
      U32 color_u32 = e_value_from_stringf("(uint32)(%S)", color_string).u32;
      Vec4F32 color = linear_from_srgba(rgba_from_u32(color_u32));
      if(tags.size != 0)
      {
        dr_fstrs_push_new(arena, &result, &params, tags);
      }
      else
      {
        dr_fstrs_push_new(arena, &result, &params, str8_lit("Color"), .color = rgba_secondary);
      }
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
      dr_fstrs_push_new(arena, &result, &params, rd_icon_kind_text_table[RD_IconKind_CircleFilled], .font = rd_font_from_slot(RD_FontSlot_Icons), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Icons), .color = color);
    }
    
#undef start_secondary
    scratch_end(scratch);
  }
  return result;
}

internal DR_FStrList
rd_title_fstrs_from_code_name(Arena *arena, String8 code_name)
{
  DR_FStrList result = {0};
  {
    RD_VocabInfo *info = rd_vocab_info_from_code_name(code_name);
    
    //- rjf: set up color/size for all parts of the title
    //
    // the "running" part implies that it changes as things are added - 
    // so if a primary title is pushed, we can make the rest of the title
    // more faded/smaller, but only after a primary title is pushed,
    // which could be caused by many different potential parts of a cfg.
    //
    DR_FStrParams params = {rd_font_from_slot(RD_FontSlot_Main), rd_raster_flags_from_slot(RD_FontSlot_Main), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
    
    //- rjf: push icon
    if(info->icon_kind != RD_IconKind_Null) UI_Tag(str8_lit("weak"))
    {
      dr_fstrs_push_new(arena, &result, &params, rd_icon_kind_text_table[info->icon_kind], .font = rd_font_from_slot(RD_FontSlot_Icons), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Icons), .color = ui_color_from_name(str8_lit("text")));
      dr_fstrs_push_new(arena, &result, &params, str8_lit("  "));
    }
    
    //- rjf: push display name
    if(info->display_name.size != 0)
    {
      dr_fstrs_push_new(arena, &result, &params, info->display_name);
    }
    
    //- rjf: push code name as a fallback
    else
    {
      dr_fstrs_push_new(arena, &result, &params, code_name, .font = rd_font_from_slot(RD_FontSlot_Code), .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Code));
    }
  }
  return result;
}

internal DR_FStrList
rd_title_fstrs_from_file_path(Arena *arena, String8 file_path, B32 include_folder)
{
  DR_FStrList fstrs = {0};
  String8 file_name = str8_skip_last_slash(file_path);
  FileProperties props = properties_from_file_path(file_path);
  RD_IconKind icon_kind = RD_IconKind_FileOutline;
  if(props.flags & FilePropertyFlag_IsFolder)
  {
    icon_kind = RD_IconKind_FolderClosedFilled;
  }
  if(file_path.size == 0 || str8_match(file_path, str8_lit("/"), StringMatchFlag_SlashInsensitive))
  {
    icon_kind = RD_IconKind_Machine;
    file_name = str8_lit("File System");
  }
  DR_FStrParams params = {rd_font_from_slot(RD_FontSlot_Main), rd_raster_flags_from_slot(RD_FontSlot_Main), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
  UI_TagF("weak")
  {
    dr_fstrs_push_new(arena, &fstrs, &params,
                      rd_icon_kind_text_table[icon_kind],
                      .font = rd_font_from_slot(RD_FontSlot_Icons),
                      .raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Icons),
                      .color = ui_color_from_name(str8_lit("text")));
  }
  dr_fstrs_push_new(arena, &fstrs, &params, str8_lit("  "));
  dr_fstrs_push_new(arena, &fstrs, &params, file_name);
  if(include_folder)
  {
    dr_fstrs_push_new(arena, &fstrs, &params, str8_lit("  "));
    UI_TagF("weak")
    {
      dr_fstrs_push_new(arena, &fstrs, &params,
                        str8_chop_last_slash(file_path),
                        .size = params.size*0.9f,
                        .color = ui_color_from_name(str8_lit("text")));
    }
  }
  return fstrs;
}

////////////////////////////////
//~ rjf: UI Widgets: Loading Overlay

internal void
rd_loading_overlay(Rng2F32 rect, F32 loading_t, U64 progress_v, U64 progress_v_target)
{
  if(loading_t >= 0.001f) UI_Focus(UI_FocusKind_Off)
  {
    // rjf: set up dimensions
    F32 edge_padding = 30.f;
    F32 width = ui_top_font_size() * 10;
    F32 height = ui_top_font_size() * 1.f;
    F32 min_thickness = ui_top_font_size()/2;
    F32 trail = ui_top_font_size() * 4;
    F32 t = pow_f32(sin_f32((F32)rd_state->time_in_seconds / 1.8f), 2.f);
    F64 v = 1.f - abs_f32(0.5f - t);
    
    // rjf: build indicator
    UI_CornerRadius(height/3.f) UI_Transparency(1-loading_t)
    {
      // rjf: rects
      Rng2F32 indicator_region_rect =
        r2f32p((rect.x0 + rect.x1)/2 - width/2  - rect.x0,
               (rect.y0 + rect.y1)/2 - height/2 - rect.y0,
               (rect.x0 + rect.x1)/2 + width/2  - rect.x0,
               (rect.y0 + rect.y1)/2 + height/2 - rect.y0);
      Rng2F32 indicator_rect =
        r2f32p(indicator_region_rect.x0 + width*t - min_thickness/2 - trail*v,
               indicator_region_rect.y0,
               indicator_region_rect.x0 + width*t + min_thickness/2 + trail*v,
               indicator_region_rect.y1);
      indicator_rect.x0 = Clamp(indicator_region_rect.x0, indicator_rect.x0, indicator_region_rect.x1);
      indicator_rect.x1 = Clamp(indicator_region_rect.x0, indicator_rect.x1, indicator_region_rect.x1);
      indicator_rect = pad_2f32(indicator_rect, -1.f);
      
      // rjf: does the view have loading *progress* info? -> draw extra progress layer
      if(progress_v != progress_v_target) UI_TagF("drop_site")
      {
        F64 pct_done_f64 = ((F64)progress_v/(F64)progress_v_target);
        F32 pct_done = (F32)pct_done_f64;
        Rng2F32 pct_rect = r2f32p(indicator_region_rect.x0,
                                  indicator_region_rect.y0,
                                  indicator_region_rect.x0 + (indicator_region_rect.x1 - indicator_region_rect.x0)*pct_done,
                                  indicator_region_rect.y1);
        UI_Rect(pct_rect)
          ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Floating, ui_key_zero());
      }
      
      // rjf: fill
      UI_TagF("pop") UI_Rect(indicator_rect)
        ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Floating, ui_key_zero());
      
      // rjf: animated bar
      UI_Rect(indicator_region_rect)
      {
        UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder|UI_BoxFlag_Floating|UI_BoxFlag_Clickable, "bg_system_status");
        UI_Signal sig = ui_signal_from_box(box);
      }
    }
    
    // rjf: build background
    UI_WidthFill UI_HeightFill UI_Transparency(1-loading_t) UI_BlurSize(10.f*loading_t)
    {
      ui_set_next_blur_size(10.f*loading_t);
      ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBackgroundBlur|UI_BoxFlag_Floating, ui_key_zero());
    }
  }
}

////////////////////////////////
//~ rjf: UI Widgets: Fancy Buttons

internal void
rd_cmd_binding_buttons(String8 name, String8 filter, B32 add_new)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_KeyMapNodePtrList key_map_nodes = cfg_key_map_node_ptr_list_from_name(scratch.arena, rd_state->key_map, name);
  
  //- rjf: build buttons for each binding
  UI_CornerRadius(ui_top_font_size()*0.5f) for(CFG_KeyMapNodePtr *n = key_map_nodes.first; n != 0; n = n->next)
  {
    ui_spacer(ui_em(1.f, 1.f));
    CFG_Binding binding = n->v->binding;
    B32 rebinding_active_for_this_binding = (rd_state->bind_change_active &&
                                             str8_match(rd_state->bind_change_cmd_name, name, 0) &&
                                             n->v->cfg_id == rd_state->bind_change_binding_id);
    
    //- rjf: grab all conflicts
    B32 has_conflicts = 0;
    CFG_KeyMapNodePtrList nodes_with_this_binding = cfg_key_map_node_ptr_list_from_binding(scratch.arena, rd_state->key_map, binding);
    {
      for(CFG_KeyMapNodePtr *n2 = nodes_with_this_binding.first; n2 != 0; n2 = n2->next)
      {
        if(!str8_match(n->v->name, n2->v->name, 0))
        {
          has_conflicts = 1;
          break;
        }
      }
    }
    
    //- rjf: form binding string
    String8 keybinding_str = {0};
    {
      if(binding.key != WM_Key_Null)
      {
        keybinding_str = wm_string_from_modifiers_key(scratch.arena, binding.modifiers, binding.key);
      }
      else
      {
        keybinding_str = str8_lit("- no binding -");
      }
    }
    
    //- rjf: compute fuzzy matches
    FuzzyMatchRangeList matches = {0};
    if(filter.size != 0)
    {
      matches = fuzzy_match_find(scratch.arena, filter, keybinding_str);
    }
    
    //- rjf: build box
    ui_set_next_tag(has_conflicts ? str8_lit("bad_pop") : rebinding_active_for_this_binding ? str8_lit("pop") : str8_zero());
    ui_set_next_text_alignment(UI_TextAlign_Center);
    ui_set_next_group_key(ui_key_zero());
    ui_set_next_pref_width(ui_text_dim(ui_top_font_size()*1.f, 1));
    UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|
                                            UI_BoxFlag_Clickable|
                                            UI_BoxFlag_DrawActiveEffects|
                                            UI_BoxFlag_DrawHotEffects|
                                            UI_BoxFlag_DrawBorder|
                                            UI_BoxFlag_DrawBackground,
                                            "%S###bind_btn_%S_%x_%x", keybinding_str, name, binding.key, binding.modifiers);
    ui_box_equip_fuzzy_match_ranges(box, &matches);
    
    //- rjf: interaction
    UI_Signal sig = ui_signal_from_box(box);
    {
      // rjf: click => toggle activity
      if(!rd_state->bind_change_active && ui_clicked(sig))
      {
        if((binding.key == WM_Key_Esc || binding.key == WM_Key_Delete) && binding.modifiers == 0)
        {
          log_user_error(str8_lit("Cannot rebind; this command uses a reserved keybinding."));
        }
        else
        {
          arena_clear(rd_state->bind_change_arena);
          rd_state->bind_change_active = 1;
          rd_state->bind_change_cmd_name = push_str8_copy(rd_state->bind_change_arena, name);
          rd_state->bind_change_binding_id = n->v->cfg_id;
        }
      }
      else if(rd_state->bind_change_active && ui_clicked(sig))
      {
        rd_state->bind_change_active = 0;
      }
      
      // rjf: hover w/ conflicts => show conflicts
      if(ui_hovering(sig) && has_conflicts) UI_Tooltip
      {
        UI_PrefWidth(ui_children_sum(1)) rd_error_label(str8_lit("This binding conflicts with those for:"));
        for(CFG_KeyMapNodePtr *n2 = nodes_with_this_binding.first; n2 != 0; n2 = n2->next)
        {
          if(!str8_match(n2->v->name, n->v->name, 0))
          {
            String8 display_name = rd_display_from_code_name(n2->v->name);
            ui_labelf("%S", display_name);
          }
        }
      }
    }
    
    //- rjf: delete button
    if(rebinding_active_for_this_binding)
      UI_PrefWidth(ui_em(2.5f, 1.f))
      UI_TagF("bad_pop")
    {
      ui_set_next_group_key(ui_key_zero());
      UI_Signal sig = rd_icon_button(RD_IconKind_X, 0, str8_lit("###delete_binding"));
      if(ui_clicked(sig))
      {
        cfg_node_release(rd_state->cfg, cfg_node_from_id(rd_state->bind_change_binding_id));
        rd_state->bind_change_active = 0;
      }
    }
  }
  
  //- rjf: build "add new binding" button
  if(add_new)
  {
    B32 adding_new_binding = (rd_state->bind_change_active &&
                              str8_match(rd_state->bind_change_cmd_name, name, 0) &&
                              rd_state->bind_change_binding_id == 0);
    ui_spacer(ui_em(1.f, 1.f));
    RD_Font(RD_FontSlot_Icons) UI_TagF(adding_new_binding ? "pop" : "") UI_CornerRadius(ui_top_font_size()*0.5f)
    {
      ui_set_next_text_alignment(UI_TextAlign_Center);
      ui_set_next_group_key(ui_key_zero());
      ui_set_next_pref_width(ui_text_dim(ui_top_font_size()*1.5f, 1));
      UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|
                                              UI_BoxFlag_Clickable|
                                              UI_BoxFlag_DrawActiveEffects|
                                              UI_BoxFlag_DrawHotEffects|
                                              UI_BoxFlag_DrawBorder|
                                              UI_BoxFlag_DrawBackground,
                                              "%S###add_binding", rd_icon_kind_text_table[RD_IconKind_Add]);
      UI_Signal sig = ui_signal_from_box(box);
      if(ui_clicked(sig))
      {
        if(!adding_new_binding && ui_clicked(sig))
        {
          arena_clear(rd_state->bind_change_arena);
          rd_state->bind_change_active = 1;
          rd_state->bind_change_cmd_name = push_str8_copy(rd_state->bind_change_arena, name);
          rd_state->bind_change_binding_id = 0;
        }
        else if(adding_new_binding && ui_clicked(sig))
        {
          rd_state->bind_change_active = 0;
        }
      }
    }
  }
  
  scratch_end(scratch);
}

internal UI_Signal
rd_menu_bar_button(String8 string)
{
  UI_Box *box = ui_build_box_from_string(UI_BoxFlag_DrawText|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_Clickable|UI_BoxFlag_DrawHotEffects, string);
  UI_Signal sig = ui_signal_from_box(box);
  return sig;
}

internal UI_Signal
rd_cmd_spec_button(String8 name)
{
  ui_set_next_child_layout_axis(Axis2_X);
  UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|
                                          UI_BoxFlag_DrawBackground|
                                          UI_BoxFlag_DrawHotEffects|
                                          UI_BoxFlag_DrawActiveEffects|
                                          UI_BoxFlag_Clickable,
                                          "###cmd_%S", name);
  UI_Parent(box) UI_HeightFill UI_Padding(ui_em(1.f, 1.f))
  {
    RD_IconKind canonical_icon = rd_icon_kind_from_code_name(name);
    if(canonical_icon != RD_IconKind_Null)
    {
      RD_Font(RD_FontSlot_Icons)
        UI_PrefWidth(ui_em(2.f, 1.f))
        UI_TextAlignment(UI_TextAlign_Center)
        UI_TagF("weak")
      {
        ui_label(rd_icon_kind_text_table[canonical_icon]);
      }
    }
    UI_PrefWidth(ui_text_dim(10, 1.f))
    {
      UI_Flags(UI_BoxFlag_DrawTextFastpathCodepoint)
        UI_FastpathCodepoint(box->fastpath_codepoint)
        ui_label(rd_display_from_code_name(name));
      ui_spacer(ui_pct(1, 0));
      ui_set_next_flags(UI_BoxFlag_Clickable);
      ui_set_next_group_key(ui_key_zero());
      UI_PrefWidth(ui_children_sum(1))
        UI_FontSize(ui_top_font_size()*0.95f) UI_HeightFill
        UI_NamedRow(str8_lit("###bindings"))
        UI_TagF("weak")
        UI_FastpathCodepoint(0)
      {
        rd_cmd_binding_buttons(name, str8_zero(), 1);
      }
    }
  }
  UI_Signal sig = ui_signal_from_box(box);
  return sig;
}

internal void
rd_cmd_list_menu_buttons(U64 count, String8 *cmd_names, U32 *fastpath_codepoints)
{
  Temp scratch = scratch_begin(0, 0);
  for EachIndex(idx, count)
  {
    if(cmd_names[idx].size == 0)
    {
      UI_TagF("floating") ui_divider(ui_em(1.f, 1.f));
    }
    else
    {
      ui_set_next_fastpath_codepoint(fastpath_codepoints[idx]);
      UI_Signal sig = rd_cmd_spec_button(cmd_names[idx]);
      if(ui_clicked(sig))
      {
        uishell_cmd("run_command", .cmd_name = cmd_names[idx]);
        ui_ctx_menu_close();
        CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
        RD_WindowState *ws = rd_window_state_from_cfg(window);
        ws->menu_bar_focused = 0;
      }
    }
  }
  scratch_end(scratch);
}

internal UI_Signal
rd_icon_button(RD_IconKind kind, FuzzyMatchRangeList *matches, String8 string)
{
  String8 display_string = ui_display_part_from_key_string(string);
  ui_set_next_child_layout_axis(Axis2_X);
  UI_Box *box = ui_build_box_from_string(UI_BoxFlag_Clickable|
                                         UI_BoxFlag_DrawBorder|
                                         UI_BoxFlag_DrawBackground|
                                         UI_BoxFlag_DrawHotEffects|
                                         UI_BoxFlag_DrawActiveEffects,
                                         string);
  UI_Parent(box)
  {
    if(display_string.size == 0)
    {
      ui_spacer(ui_pct(1, 0));
    }
    else
    {
      ui_spacer(ui_em(1.f, 1.f));
    }
    UI_TextAlignment(UI_TextAlign_Center)
      RD_Font(RD_FontSlot_Icons)
      UI_PrefWidth(ui_em(2.f, 1.f))
      UI_PrefHeight(ui_pct(1, 0))
      UI_FlagsAdd(UI_BoxFlag_DisableTextTrunc)
      UI_TagF("weak")
      ui_label(rd_icon_kind_text_table[kind]);
    if(display_string.size != 0)
    {
      UI_PrefWidth(ui_pct(1.f, 0.f))
      {
        UI_Box *box = ui_label(display_string).box;
        if(matches != 0)
        {
          ui_box_equip_fuzzy_match_ranges(box, matches);
        }
      }
    }
    if(display_string.size == 0)
    {
      ui_spacer(ui_pct(1, 0));
    }
    else
    {
      ui_spacer(ui_em(1.f, 1.f));
    }
  }
  UI_Signal result = ui_signal_from_box(box);
  return result;
}

internal UI_Signal
rd_icon_buttonf(RD_IconKind kind, FuzzyMatchRangeList *matches, char *fmt, ...)
{
  Temp scratch = scratch_begin(0, 0);
  va_list args;
  va_start(args, fmt);
  String8 string = push_str8fv(scratch.arena, fmt, args);
  va_end(args);
  UI_Signal sig = rd_icon_button(kind, matches, string);
  scratch_end(scratch);
  return sig;
}

////////////////////////////////
//~ rjf: UI Widgets: Text View

internal RD_CodeSliceSignal
rd_code_slice(RD_CodeSliceParams *params, TxtPt *cursor, TxtPt *mark, S64 *preferred_column, String8 string)
{
  RD_CodeSliceSignal result = {0};
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  B32 is_focused = ui_is_focus_active();
  B32 ctrlified = (wm_get_modifiers() & WM_Modifier_Ctrl);
  F32 line_num_padding_px = ui_top_font_size()*1.f;
  B32 do_scope_lines = rd_setting_b32_from_name(s("cursor_scope_lines"));
  B32 do_scope_end_annotations = rd_setting_b32_from_name(s("cursor_scope_end_annotations"));
  B32 do_cursor_trail = rd_setting_b32_from_name(s("cursor_trail"));
  Vec4F32 pop_color = {0};
  UI_TagF("pop")
  {
    pop_color = ui_color_from_name(s("background"));
  }
  Vec4F32 highlight_color = {0};
  UI_TagF("focus")
  {
    highlight_color = ui_color_from_name(s("border"));
  }
  
  //////////////////////////////
  //- rjf: build top-level container
  //
  UI_Box *top_container_box = &ui_nil_box;
  Rng2F32 clipped_top_container_rect = {0};
  {
    ui_set_next_child_layout_axis(Axis2_X);
    ui_set_next_pref_width(ui_px(params->line_text_max_width_px, 1));
    ui_set_next_pref_height(ui_children_sum(1));
    top_container_box = ui_build_box_from_string(UI_BoxFlag_DisableFocusEffects|UI_BoxFlag_DrawBorder, string);
    clipped_top_container_rect = top_container_box->rect;
    for(UI_Box *b = top_container_box; !ui_box_is_nil(b); b = b->parent)
    {
      if(b->flags & UI_BoxFlag_Clip)
      {
        clipped_top_container_rect = intersect_2f32(b->rect, clipped_top_container_rect);
      }
    }
  }
  
  //////////////////////////////
  //- rjf: dragging expressions? -> drop site
  //
  B32 drop_can_hit_lines = 0;
  UI_Key drop_site_key = ui_key_from_stringf(top_container_box->key, "drop_site");
  if(rd_drag_is_active())
  {
    if(rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_Expr)
    {
      drop_can_hit_lines = 1;
    }
    if(drop_can_hit_lines) UI_WidthFill UI_HeightFill
    {
      UI_Box *drop_site_box = ui_build_box_from_key(UI_BoxFlag_DropSite|UI_BoxFlag_Floating, drop_site_key);
      ui_signal_from_box(drop_site_box);
    }
  }
  
  //////////////////////////////
  //- rjf: build per-line background colors
  //
  Vec4F32 *line_bg_colors = push_array(scratch.arena, Vec4F32, dim_1s64(params->line_num_range)+1);
  
  //////////////////////////////
  //- rjf: build priority margin
  //
  UI_Box *priority_margin_container_box = &ui_nil_box;
  if(params->flags & RD_CodeSliceFlag_PriorityMargin) UI_Focus(UI_FocusKind_Off) UI_Parent(top_container_box) ProfScope("build priority margins")
  {
    if(params->margin_float_off_px != 0)
    {
      ui_set_next_pref_width(ui_px(params->priority_margin_width_px, 1));
      ui_set_next_pref_height(ui_px(params->line_height_px*(dim_1s64(params->line_num_range)+1), 1.f));
      ui_build_box_from_key(0, ui_key_zero());
      ui_set_next_fixed_x(floor_f32(params->margin_float_off_px));
    }
    ui_set_next_pref_width(ui_px(params->priority_margin_width_px, 1));
    ui_set_next_pref_height(ui_px(params->line_height_px*(dim_1s64(params->line_num_range)+1), 1.f));
    ui_set_next_child_layout_axis(Axis2_Y);
    priority_margin_container_box = ui_build_box_from_string(UI_BoxFlag_Clickable*!!(params->flags & RD_CodeSliceFlag_Clickable), s("priority_margin_container"));
    UI_Parent(priority_margin_container_box) UI_PrefHeight(ui_px(params->line_height_px, 1.f))
    {
      U64 line_idx = 0;
      for(S64 line_num = params->line_num_range.min;
          line_num <= params->line_num_range.max;
          line_num += 1, line_idx += 1)
      {
        ui_set_next_hover_cursor(WM_Cursor_HandPoint);
        UI_Box *line_margin_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable*!!(params->flags & RD_CodeSliceFlag_Clickable)|UI_BoxFlag_DrawActiveEffects, "line_margin_%I64x", line_num);
        (void)line_margin_box;
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build catchall margin
  //
  UI_Box *catchall_margin_container_box = &ui_nil_box;
  if(params->flags & RD_CodeSliceFlag_CatchallMargin) UI_Focus(UI_FocusKind_Off) UI_Parent(top_container_box) ProfScope("build catchall margins")
    UI_TagF("floating")
  {
    if(params->margin_float_off_px != 0)
    {
      ui_set_next_pref_width(ui_px(params->catchall_margin_width_px, 1));
      ui_set_next_pref_height(ui_px(params->line_height_px*(dim_1s64(params->line_num_range)+1), 1.f));
      ui_build_box_from_key(0, ui_key_zero());
      ui_set_next_fixed_x(floor_f32(params->margin_float_off_px + params->priority_margin_width_px));
    }
    ui_set_next_pref_width(ui_px(params->catchall_margin_width_px, 1));
    ui_set_next_pref_height(ui_px(params->line_height_px*(dim_1s64(params->line_num_range)+1), 1.f));
    ui_set_next_child_layout_axis(Axis2_Y);
    catchall_margin_container_box = ui_build_box_from_string(UI_BoxFlag_DrawSideRight|UI_BoxFlag_DrawSideLeft|UI_BoxFlag_Clickable*!!(params->flags & RD_CodeSliceFlag_Clickable), s("catchall_margin_container"));
    UI_Parent(catchall_margin_container_box) UI_PrefHeight(ui_px(params->line_height_px, 1.f))
    {
      U64 line_idx = 0;
      for(S64 line_num = params->line_num_range.min;
          line_num <= params->line_num_range.max;
          line_num += 1, line_idx += 1)
      {
        ui_set_next_hover_cursor(WM_Cursor_HandPoint);
        ui_set_next_background_color(v4f32(0, 0, 0, 0));
        UI_Box *line_margin_box = ui_build_box_from_stringf(UI_BoxFlag_Clickable*!!(params->flags & RD_CodeSliceFlag_Clickable)|UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawActiveEffects, "line_margin_%I64x", line_num);
        
        // rjf: empty margin interaction
        UI_Signal line_margin_sig = ui_signal_from_box(line_margin_box);
        if(ui_clicked(line_margin_sig))
        {
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build line numbers
  //
  if(params->flags & RD_CodeSliceFlag_LineNums) UI_Parent(top_container_box) ProfScope("build line numbers") UI_Focus(UI_FocusKind_Off)
    UI_TagF("floating")
  {
    TxtRng select_rng = txt_rng(*cursor, *mark);
    ui_set_next_fixed_x(floor_f32(params->margin_float_off_px + params->priority_margin_width_px + params->catchall_margin_width_px));
    ui_set_next_pref_width(ui_px(params->line_num_width_px, 1.f));
    ui_set_next_pref_height(ui_px(params->line_height_px*(dim_1s64(params->line_num_range)+1), 1.f));
    ui_set_next_flags(UI_BoxFlag_DrawSideRight);
    UI_Column
      UI_PrefHeight(ui_px(params->line_height_px, 1.f))
      RD_Font(RD_FontSlot_Code)
      UI_FontSize(params->font_size)
      UI_CornerRadius(0)
    {
      U64 line_idx = 0;
      for(S64 line_num = params->line_num_range.min;
          line_num <= params->line_num_range.max;
          line_num += 1, line_idx += 1)
      {
        B32 line_is_selected = (select_rng.min.line <= line_num && line_num <= select_rng.max.line);
        Vec4F32 bg_color = v4f32(0, 0, 0, 0);
        
        // rjf: build line num box
        UI_TagF(line_is_selected ? "" : "weak") UI_BackgroundColor(bg_color)
          ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%I64u##line_num", line_num);
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build background for line numbers & margins
  //
  {
    UI_Parent(top_container_box) UI_TagF("floating")
    {
      ui_set_next_pref_width(ui_px(params->priority_margin_width_px + params->catchall_margin_width_px + params->line_num_width_px, 1));
      ui_set_next_pref_height(ui_px(params->line_height_px*(dim_1s64(params->line_num_range)+1), 1.f));
      ui_set_next_fixed_x(floor_f32(params->margin_float_off_px));
      ui_build_box_from_key(UI_BoxFlag_DrawBackgroundBlur|UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawDropShadow, ui_key_zero());
    }
  }
  
  //////////////////////////////
  //- rjf: build main text container box, for mouse interaction on both lines & line numbers
  //
  UI_Box *text_container_box = &ui_nil_box;
  UI_Parent(top_container_box) UI_Focus(UI_FocusKind_Off)
  {
    ui_set_next_hover_cursor(ctrlified ? WM_Cursor_HandPoint : WM_Cursor_IBar);
    ui_set_next_pref_height(ui_px(params->line_height_px*(dim_1s64(params->line_num_range)+1), 1.f));
    text_container_box = ui_build_box_from_string(UI_BoxFlag_Clickable*!!(params->flags & RD_CodeSliceFlag_Clickable), s("text_container"));
  }
  
  //////////////////////////////
  //- rjf: mouse -> text coordinates
  //
  TxtPt mouse_pt = {0};
  ProfScope("mouse -> text coordinates")
  {
    Vec2F32 mouse = ui_mouse();
    
    // rjf: mouse y => index
    U64 mouse_y_line_idx = (U64)((mouse.y - text_container_box->rect.y0) / params->line_height_px);
    
    // rjf: index => line num
    S64 line_num = (params->line_num_range.min + mouse_y_line_idx);
    String8 line_string = (params->line_num_range.min <= line_num && line_num <= params->line_num_range.max) ? (params->line_text[mouse_y_line_idx]) : str8_zero();
    
    // rjf: mouse x * string => column
    S64 column = fnt_char_pos_from_tag_size_string_p(params->font, params->font_size, 0, params->tab_size, line_string, mouse.x-text_container_box->rect.x0-params->line_num_width_px-line_num_padding_px)+1;
    
    // rjf: bundle
    mouse_pt = txt_pt(line_num, column);
    
    // rjf: clamp
    {
      U64 last_line_size = params->line_text[dim_1s64(params->line_num_range)].size;
      TxtRng legal_pt_rng = txt_rng(txt_pt(params->line_num_range.min, 1),
                                    txt_pt(params->line_num_range.max, last_line_size+1));
      if(txt_pt_less_than(mouse_pt, legal_pt_rng.min))
      {
        mouse_pt = legal_pt_rng.min;
      }
      if(txt_pt_less_than(legal_pt_rng.max, mouse_pt))
      {
        mouse_pt = legal_pt_rng.max;
      }
    }
    
    result.mouse_pt = mouse_pt;
  }
  
  //////////////////////////////
  //- rjf: mouse point -> mouse token range, mouse line range
  //
  TxtRng mouse_token_rng = txt_rng(mouse_pt, mouse_pt);
  TxtRng mouse_line_rng = txt_rng(mouse_pt, mouse_pt);
  if(contains_1s64(params->line_num_range, mouse_pt.line))
  {
    TXT_TokenArray *line_tokens = &params->line_tokens[mouse_pt.line-params->line_num_range.min];
    Rng1U64 line_range = params->line_ranges[mouse_pt.line-params->line_num_range.min];
    U64 mouse_pt_off = (mouse_pt.column-1) + line_range.min;
    for(U64 line_token_idx = 0; line_token_idx < line_tokens->count; line_token_idx += 1)
    {
      TXT_Token *line_token = &line_tokens->v[line_token_idx];
      if(contains_1u64(line_token->range, mouse_pt_off))
      {
        Rng1U64 line_token_range_clamped = intersect_1u64(line_token->range, line_range);
        mouse_token_rng = txt_rng(txt_pt(mouse_pt.line, 1+line_token_range_clamped.min-line_range.min), txt_pt(mouse_pt.line, 1+line_token_range_clamped.max-line_range.min));
        break;
      }
    }
    mouse_line_rng = txt_rng(txt_pt(mouse_pt.line, 1), txt_pt(mouse_pt.line, 1+(line_range.max-line_range.min)));
  }
  
  //////////////////////////////
  //- rjf: interact with margin box & text box
  //
  B32 search_query_invalidated = 0;
  UI_Signal priority_margin_container_sig = ui_signal_from_box(priority_margin_container_box);
  UI_Signal catchall_margin_container_sig = ui_signal_from_box(catchall_margin_container_box);
  UI_Signal text_container_sig = ui_signal_from_box(text_container_box);
  {
    //- rjf: determine mouse drag range
    TxtRng mouse_drag_rng = txt_rng(mouse_pt, mouse_pt);
    if(text_container_sig.f & UI_SignalFlag_LeftTripleDragging)
    {
      mouse_drag_rng = mouse_line_rng;
    }
    else if(text_container_sig.f & UI_SignalFlag_LeftDoubleDragging)
    {
      mouse_drag_rng = mouse_token_rng;
    }
    
    //- rjf: clicking/dragging over the text container
    if(!ctrlified && ui_dragging(text_container_sig))
    {
      if(mouse_pt.line == 0)
      {
        mouse_pt.column = 1;
        if(ui_mouse().y <= top_container_box->rect.y0)
        {
          mouse_pt.line = params->line_num_range.min - 2;
        }
        else if(ui_mouse().y >= top_container_box->rect.y1)
        {
          mouse_pt.line = params->line_num_range.max + 2;
        }
      }
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
    
    //- rjf: dragging will invalidate the search string, so we don't want to draw it while dragging/releasing
    if(ui_dragging(text_container_sig) || ui_released(text_container_sig))
    {
      search_query_invalidated = 1;
    }
    
    //- rjf: right-click => code context menu
    if(ui_right_clicked(text_container_sig))
    {
      if(txt_pt_match(*cursor, *mark))
      {
        *cursor = *mark = mouse_pt;
      }
      U64 vaddr = 0;
      if(params->line_num_range.min <= cursor->line && cursor->line < params->line_num_range.max)
      {
        vaddr = params->line_vaddrs[cursor->line - params->line_num_range.min];
      }
      uishell_cmd("focus_panel");
      uishell_cmd("push_query",
             .expr = txt_pt_match(*cursor, *mark) ? s("query:text_pt_commands") : s("query:text_range_commands"),
             .do_implicit_root = 1,
             .do_lister = 1,
             .activate_with_single_click = 1,
             .ui_key = ui_get_selected_state()->root->key,
             .off_px = ui_mouse(),
             .cursor = *cursor,
             .mark = *mark,
             .vaddr = vaddr);
    }
    
    //- rjf: drop target is dropped -> process
    if(drop_can_hit_lines && ui_key_match(ui_drop_hot_key(), drop_site_key) && rd_drag_drop())
    {
      if(rd_state->drag_drop_regs_slot == UIShell_ContextRegSlot_Expr)
      {
      }
    }
    
    //- rjf: commit text container signal to main output
    result.base = text_container_sig;
  }
  
  //////////////////////////////
  //- rjf: cursor -> scope info
  //
  TXT_ScopeNode *cursor_scope_node = &txt_scope_node_nil;
  if(params->text_info != 0)
  {
    cursor_scope_node = txt_scope_node_from_info_pt(params->text_info, uishell_regs()->cursor);
  }
  
  //////////////////////////////
  //- rjf: equip cursor scope rendering info
  //
  if(do_scope_lines && cursor_scope_node != &txt_scope_node_nil)
  {
    F32 scope_line_thickness = params->font_size*0.1f;
    scope_line_thickness = Max(scope_line_thickness, 1.f);
    DR_Bucket *bucket = dr_bucket_make();
    DR_BucketScope(bucket)
    {
      Vec2F32 text_base_pos = v2f32(text_container_box->rect.x0 + params->line_num_width_px + line_num_padding_px,
                                    text_container_box->rect.y0);
      F32 ancestor_chain_depth = 0;
      for(TXT_ScopeNode *scope_n = cursor_scope_node;
          scope_n != &txt_scope_node_nil;
          scope_n = txt_scope_node_from_info_num(params->text_info, scope_n->parent_num), ancestor_chain_depth += 1)
      {
        Vec4F32 scope_line_color = highlight_color;
        F32 scope_line_color_target = highlight_color.w;
        scope_line_color_target *= 1 - ancestor_chain_depth / 6.f;
        scope_line_color_target = Max(0.2f, scope_line_color_target);
        F32 scope_line_color_t = ui_anim(ui_key_from_stringf(text_container_box->key, "###scope_depth_%I64x_%I64x", scope_n->token_idx_range.min, scope_n->token_idx_range.max), scope_line_color_target, .rate = rd_state->menu_animation_rate__slow);
        scope_line_color.w = scope_line_color_t*0.5f;
        Rng1U64 token_idx_range = scope_n->token_idx_range;
        Rng1U64 off_range = r1u64(params->text_info->tokens.v[token_idx_range.min].range.min, params->text_info->tokens.v[token_idx_range.max].range.min);
        TxtRng txt_range = txt_rng(txt_pt_from_info_off__linear_scan(params->text_info, off_range.min), txt_pt_from_info_off__linear_scan(params->text_info, off_range.max));
        
        //- rjf: single-line scopes (underline)
        if(txt_range.min.line == txt_range.max.line && contains_1s64(params->line_num_range, txt_range.min.line))
        {
          S64 line_num = txt_range.min.line;
          U64 line_idx = (U64)(line_num - params->line_num_range.min);
          String8 line_string = params->line_text[line_idx];
          Rng1U64 line_off_range = r1u64(off_range.min - params->line_ranges[line_idx].min, off_range.max+1 - params->line_ranges[line_idx].min);
          Rng1F32 x_px_range = r1f32(fnt_dim_from_tag_size_string(params->font, params->font_size, 0, params->tab_size, str8_prefix(line_string, line_off_range.min)).x,
                                     fnt_dim_from_tag_size_string(params->font, params->font_size, 0, params->tab_size, str8_prefix(line_string, line_off_range.max)).x);
          F32 line_y = line_idx*params->line_height_px;
          Rng2F32 underline_rect = r2f32p(text_base_pos.x + x_px_range.min,
                                          text_base_pos.y + line_y + params->line_height_px*0.5f,
                                          text_base_pos.x + x_px_range.max+1,
                                          text_base_pos.y + line_y + params->line_height_px + params->font_size*0.1f);
          F32 midpoint = center_1f32(r1f32(underline_rect.x0, underline_rect.x1));
          F32 t = ui_anim(ui_key_from_stringf(text_container_box->key, "###scope_%I64x_%I64x", scope_n->token_idx_range.min, scope_n->token_idx_range.max), 1.f, .rate = rd_state->catchall_animation_rate);
          Rng2F32 underline_clip = {0};
          underline_clip.x0 = mix_1f32(midpoint, underline_rect.x0 - params->font_size, t);
          underline_clip.x1 = mix_1f32(midpoint, underline_rect.x1 + params->font_size, t);
          underline_clip.y0 = underline_rect.y0 + (underline_rect.y1 - underline_rect.y0) * 0.65f;
          underline_clip.y1 = 10000;
          DR_ClipScope(underline_clip)
          {
            dr_rect(underline_rect, scope_line_color, params->font_size*0.1f, scope_line_thickness, 1.f);
          }
        }
        
        //- rjf: cross-line scopes
        if(txt_range.min.line != txt_range.max.line && params->line_num_range.max > txt_range.min.line && params->line_num_range.min < txt_range.max.line)
        {
          String8 opener_line = txt_string_from_info_data_line_num(params->text_info, params->text_data, txt_range.min.line);
          String8 closer_line = txt_string_from_info_data_line_num(params->text_info, params->text_data, txt_range.max.line);
          String8 opener_line_pre_opener = str8_prefix(opener_line, txt_range.min.column-1);
          String8 closer_line_pre_closer = str8_prefix(closer_line, txt_range.max.column-1);
          F32 opener_line_pre_opener_px = fnt_dim_from_tag_size_string(params->font, params->font_size, 0, params->tab_size, opener_line_pre_opener).x;
          F32 closer_line_pre_closer_px = fnt_dim_from_tag_size_string(params->font, params->font_size, 0, params->tab_size, closer_line_pre_closer).x;
          F32 indent_depth_px = Min(opener_line_pre_opener_px, closer_line_pre_closer_px);
          Rng1F32 scope_range_y_px = r1f32(0, dim_2f32(text_container_box->rect).y);
          if(contains_1s64(params->line_num_range, txt_range.min.line))
          {
            scope_range_y_px.min = (txt_range.min.line - params->line_num_range.min) * params->line_height_px;
          }
          if(contains_1s64(params->line_num_range, txt_range.max.line))
          {
            scope_range_y_px.max = ((txt_range.max.line - params->line_num_range.min) + 1) * params->line_height_px;
          }
          F32 midpoint = center_1f32(scope_range_y_px);
          F32 t = ui_anim(ui_key_from_stringf(text_container_box->key, "###scope_%I64x_%I64x", scope_n->token_idx_range.min, scope_n->token_idx_range.max), 1.f, .rate = rd_state->catchall_animation_rate);
          Rng2F32 scope_rect = r2f32p(text_base_pos.x + indent_depth_px - params->font_size*0.2f,
                                      text_base_pos.y + scope_range_y_px.min,
                                      text_base_pos.x + indent_depth_px - params->font_size*0.2f + params->font_size*1.f,
                                      text_base_pos.y + scope_range_y_px.max);
          Rng2F32 scope_clip_rect = {0};
          {
            scope_clip_rect.x0 = scope_rect.x0 - params->font_size*10.f;
            scope_clip_rect.x1 = scope_rect.x0 + (scope_rect.x1 - scope_rect.x0)*0.4f;
            scope_clip_rect.y0 = mix_1f32(midpoint, scope_rect.y0 - params->font_size*0.1f, t);
            scope_clip_rect.y1 = mix_1f32(midpoint, scope_rect.y1 + params->font_size*0.1f, t);
          }
          DR_ClipScope(scope_clip_rect)
          {
            dr_rect(scope_rect, scope_line_color, params->font_size*0.1f, scope_line_thickness, 1.f);
          }
        }
        
        //- rjf: scope ending annotations
        if(do_scope_end_annotations && txt_range.min.line != txt_range.max.line && contains_1s64(params->line_num_range, txt_range.max.line)) UI_TagF("weak")
        {
          String8 opener_line = str8_skip_chop_whitespace(txt_string_from_info_data_line_num(params->text_info, params->text_data, txt_range.min.line));
          String8 scope_title_string = opener_line;
          if(str8_match(opener_line, s("{"), 0) ||
             str8_match(opener_line, s("["), 0) ||
             str8_match(opener_line, s("("), 0))
          {
            scope_title_string = str8_skip_chop_whitespace(txt_string_from_info_data_line_num(params->text_info, params->text_data, txt_range.min.line-1));
          }
          if(!str8_match(scope_title_string, s("{"), 0) &&
             !str8_match(scope_title_string, s("["), 0) &&
             !str8_match(scope_title_string, s("("), 0))
          {
            F32 t = ui_anim(ui_key_from_stringf(text_container_box->key, "###scope_end_annotation_%I64x_%I64x", scope_n->token_idx_range.min, scope_n->token_idx_range.max), 1.f, .rate = rd_state->catchall_animation_rate);
            String8 closer_line = txt_string_from_info_data_line_num(params->text_info, params->text_data, txt_range.max.line);
            F32 closer_line_px = fnt_dim_from_tag_size_string(params->font, params->font_size, 0, params->tab_size, closer_line).x;
            Vec4F32 color = ui_color_from_name(s("text"));
            color.w *= 0.5f*t;
            dr_text(params->font, params->font_size * 0.85f, 0, 0, ui_top_text_raster_flags(),
                    v2f32(text_base_pos.x + closer_line_px + ui_top_font_size()*0.5f*t,
                          text_base_pos.y + (txt_range.max.line - params->line_num_range.min) * params->line_height_px + params->line_height_px*0.7f),
                    color, scope_title_string);
          }
        }
      }
    }
    ui_box_equip_draw_bucket(text_container_box, bucket);
  }
  
  //////////////////////////////
  //- rjf: produce fancy strings for each line
  //
  DR_FStrList *lines_fstrs = push_array(scratch.arena, DR_FStrList, dim_1s64(params->line_num_range)+1);
  {
    DR_FStrParams fstr_params =
    {
      params->font,
      rd_raster_flags_from_slot(RD_FontSlot_Code),
      rd_rgba_from_code_color_slot(RD_CodeColorSlot_CodeDefault),
      params->font_size,
    };
    U64 line_idx = 0;
    for(S64 line_num = params->line_num_range.min;
        line_num <= params->line_num_range.max;
        line_num += 1, line_idx += 1)
    {
      String8 line_string = params->line_text[line_idx];
      Rng1U64 line_range = params->line_ranges[line_idx];
      TXT_TokenArray *line_tokens = &params->line_tokens[line_idx];
      DR_FStrList fstrs = {0};
      if(line_tokens->count == 0)
      {
        dr_fstrs_push_new(scratch.arena, &fstrs, &fstr_params, line_string);
      }
      else
      {
        TXT_Token *line_tokens_first = line_tokens->v;
        TXT_Token *line_tokens_opl = line_tokens->v + line_tokens->count;
        B32 preceded_by_dot = 0;
        for(TXT_Token *token = line_tokens_first; token < line_tokens_opl; token += 1)
        {
          // rjf: token -> token string
          String8 token_string = {0};
          {
            Rng1U64 token_range = r1u64(0, line_string.size);
            if(token->range.min > line_range.min)
            {
              token_range.min += token->range.min-line_range.min;
            }
            if(token->range.max < line_range.max)
            {
              token_range.max = token->range.max-line_range.min;
            }
            token_string = str8_substr(line_string, token_range);
          }
          
          // rjf: token -> token color
          RD_CodeColorSlot token_color_slot = rd_code_color_slot_from_txt_token_kind(token->kind);
          RD_CodeColorSlot lookup_color_slot = preceded_by_dot ? token_color_slot : rd_code_color_slot_from_txt_token_kind_lookup_string(token->kind, token_string, 0, 0);
          Vec4F32 token_color = rd_rgba_from_code_color_slot(token_color_slot);
          if(lookup_color_slot != RD_CodeColorSlot_CodeDefault)
          {
            Vec4F32 lookup_color = rd_rgba_from_code_color_slot(lookup_color_slot);
            F32 lookup_color_mix_t = ui_anim(ui_key_from_stringf(ui_key_zero(), "%S_lookup", token_string), 1.f);
            token_color = mix_4f32(token_color, lookup_color, lookup_color_mix_t);
          }
          
          // rjf: scope endpoints enclosing cursor -> highlight
          for(TXT_ScopeNode *scope_n = cursor_scope_node;
              scope_n != &txt_scope_node_nil;
              scope_n = txt_scope_node_from_info_num(params->text_info, scope_n->parent_num))
          {
            if(params->text_info->tokens.v[scope_n->token_idx_range.min].range.min == token->range.min ||
               params->text_info->tokens.v[scope_n->token_idx_range.max].range.min == token->range.min)
            {
              token_color = highlight_color;
              break;
            }
          }
          
          // rjf: push fancy string
          dr_fstrs_push_new(scratch.arena, &fstrs, &fstr_params, token_string, .color = token_color);
          
          // rjf: . -> mark next token as preceded by dot
          preceded_by_dot = (token->kind == TXT_TokenKind_Symbol && str8_match(token_string, s("."), 0));
        }
      }
      lines_fstrs[line_idx] = fstrs;
    }
  }
  
  //////////////////////////////
  //- rjf: mouse -> expression range info
  //
  TxtRng mouse_expr_rng = {0};
  Vec2F32 mouse_expr_baseline_pos = {0};
  String8 mouse_expr = {0};
  B32 mouse_expr_is_explicit = 0;
  if(ui_hovering(text_container_sig) && contains_1s64(params->line_num_range, mouse_pt.line)) ProfScope("mouse -> expression range")
  {
    TxtRng selected_rng = txt_rng(*cursor, *mark);
    if(!txt_pt_match(*cursor, *mark) && cursor->line == mark->line &&
       ((txt_pt_less_than(selected_rng.min, mouse_pt) || txt_pt_match(selected_rng.min, mouse_pt)) &&
        txt_pt_less_than(mouse_pt, selected_rng.max)))
    {
      U64 line_slice_idx = mouse_pt.line-params->line_num_range.min;
      String8 line_text = params->line_text[line_slice_idx];
      F32 expr_hoff_px = params->line_num_width_px + fnt_dim_from_tag_size_string(params->font, params->font_size, 0, params->tab_size, str8_prefix(line_text, selected_rng.min.column-1)).x;
      result.mouse_expr_rng = mouse_expr_rng = selected_rng;
      mouse_expr_baseline_pos = v2f32(text_container_box->rect.x0+expr_hoff_px,
                                      text_container_box->rect.y0+line_slice_idx*params->line_height_px + params->line_height_px*0.85f);
      mouse_expr = str8_substr(line_text, r1u64(selected_rng.min.column-1, selected_rng.max.column-1));
      mouse_expr_is_explicit = 1;
    }
    else
    {
      U64 line_slice_idx = mouse_pt.line-params->line_num_range.min;
      String8 line_text = params->line_text[line_slice_idx];
      TXT_TokenArray line_tokens = params->line_tokens[line_slice_idx];
      Rng1U64 line_range = params->line_ranges[line_slice_idx];
      U64 mouse_pt_off = line_range.min + (mouse_pt.column-1);
      Rng1U64 expr_off_rng = txt_expr_off_range_from_line_off_range_string_tokens(mouse_pt_off, line_range, line_text, &line_tokens);
      if(expr_off_rng.max != expr_off_rng.min)
      {
        F32 expr_hoff_px = params->line_num_width_px + fnt_dim_from_tag_size_string(params->font, params->font_size, 0, params->tab_size, str8_prefix(line_text, expr_off_rng.min-line_range.min)).x;
        result.mouse_expr_rng = mouse_expr_rng = txt_rng(txt_pt(mouse_pt.line, 1+(expr_off_rng.min-line_range.min)), txt_pt(mouse_pt.line, 1+(expr_off_rng.max-line_range.min)));
        mouse_expr_baseline_pos = v2f32(text_container_box->rect.x0+expr_hoff_px,
                                        text_container_box->rect.y0+line_slice_idx*params->line_height_px + params->line_height_px*0.85f);
        mouse_expr = str8_substr(line_text, r1u64(expr_off_rng.min-line_range.min, expr_off_rng.max-line_range.min));
      }
    }
  }
  
  //////////////////////////////
  //- rjf: mouse -> set global frontend hovered line info
  //
  
  //////////////////////////////
  //- rjf: hover eval
  //
  if(!ui_dragging(text_container_sig) && text_container_sig.event_flags == 0 && mouse_expr.size != 0) E_ParentKey(e_key_zero())
  {
    E_Eval eval = e_eval_from_string(mouse_expr);
    B32 eval_implicit_hover = 0;
    if(eval.msgs.max_kind == E_MsgKind_Null && (eval_implicit_hover || mouse_expr_is_explicit))
    {
      U64 line_vaddr = 0;
      if(contains_1s64(params->line_num_range, mouse_pt.line))
      {
        U64 line_idx = mouse_pt.line-params->line_num_range.min;
        line_vaddr = params->line_vaddrs[line_idx];
      }
      rd_set_hover_eval(mouse_expr_baseline_pos, mouse_expr);
    }
  }
  
  //////////////////////////////
  //- rjf: dragging/dropping which applies to lines over this slice -> visualize
  //
  if(drop_can_hit_lines && ui_key_match(drop_site_key, ui_drop_hot_key()))
  {
    DR_Bucket *bucket = dr_bucket_make();
    DR_BucketScope(bucket)
    {
      Vec4F32 color = pop_color;
      color.w *= 0.2f;
      Rng2F32 drop_line_rect = r2f32p(top_container_box->rect.x0,
                                      top_container_box->rect.y0 + (mouse_pt.line - params->line_num_range.min) * params->line_height_px,
                                      top_container_box->rect.x1,
                                      top_container_box->rect.y0 + (mouse_pt.line - params->line_num_range.min + 1) * params->line_height_px);
      R_Rect2DInst *inst = dr_rect(drop_line_rect, color, 0, 0, 1.f);
      inst->colors[Corner_10] = inst->colors[Corner_11] = v4f32(color.x, color.y, color.z, 0);
    }
    ui_box_equip_draw_bucket(text_container_box, bucket);
  }
  
  //////////////////////////////
  //- rjf: (cursor*mark*list(flash_range)) -> list(text_range*color)
  //
  typedef struct TxtRngColorPairNode TxtRngColorPairNode;
  struct TxtRngColorPairNode
  {
    TxtRngColorPairNode *next;
    TxtRng rng;
    Vec4F32 color;
  };
  TxtRngColorPairNode *first_txt_rng_color_pair = 0;
  TxtRngColorPairNode *last_txt_rng_color_pair = 0;
  {
    // rjf: push initial for cursor/mark
    {
      TxtRngColorPairNode *n = push_array(scratch.arena, TxtRngColorPairNode, 1);
      n->rng = txt_rng(*cursor, *mark);
      n->color = ui_color_from_name(s("selection"));
      SLLQueuePush(first_txt_rng_color_pair, last_txt_rng_color_pair, n);
    }
    
    // rjf: push for ctrlified mouse expr
    if(ctrlified && !txt_pt_match(result.mouse_expr_rng.max, result.mouse_expr_rng.min)) UI_Tag(s("pop"))
    {
      TxtRngColorPairNode *n = push_array(scratch.arena, TxtRngColorPairNode, 1);
      n->rng = result.mouse_expr_rng;
      n->color = ui_color_from_name(s("background"));
      n->color.w *= 0.2f;
      SLLQueuePush(first_txt_rng_color_pair, last_txt_rng_color_pair, n);
    }
  }
  
  //////////////////////////////
  //- rjf: build line numbers region (line number interaction should be basically identical to lines)
  //
  if(params->flags & RD_CodeSliceFlag_LineNums) UI_Parent(text_container_box) ProfScope("build line number interaction box") UI_Focus(UI_FocusKind_Off)
  {
    ui_set_next_pref_width(ui_px(params->line_num_width_px, 1.f));
    ui_set_next_pref_height(ui_px(params->line_height_px*(dim_1s64(params->line_num_range)+1), 1.f));
    ui_build_box_from_key(0, ui_key_zero());
  }
  
  //////////////////////////////
  //- rjf: build line text
  //
  UI_Parent(text_container_box) ProfScope("build line text") UI_Focus(UI_FocusKind_Off)
  {
    ui_set_next_pref_height(ui_px(params->line_height_px*(dim_1s64(params->line_num_range)+1), 1.f));
    UI_WidthFill
      UI_Column
      UI_PrefHeight(ui_px(params->line_height_px, 1.f))
      RD_Font(RD_FontSlot_Code)
      UI_FontSize(params->font_size)
      UI_CornerRadius(0)
    {
      U64 line_idx = 0;
      for(S64 line_num = params->line_num_range.min;
          line_num <= params->line_num_range.max; line_num += 1, line_idx += 1)
      {
        String8 line_string = params->line_text[line_idx];
        Rng1U64 line_range = params->line_ranges[line_idx];
        DR_FStrList line_fstrs = lines_fstrs[line_idx];
        ui_set_next_text_padding(line_num_padding_px);
        UI_Key line_key = ui_key_from_stringf(top_container_box->key, "ln_%I64x", line_num);
        Vec4F32 line_bg_color = line_bg_colors[line_idx];
        if(line_bg_color.w != 0)
        {
          ui_set_next_flags(UI_BoxFlag_DrawBackground);
          ui_set_next_background_color(line_bg_color);
        }
        ui_set_next_tab_size(params->tab_size);
        UI_Box *line_box = ui_build_box_from_key(UI_BoxFlag_DisableTextTrunc|UI_BoxFlag_DrawText|UI_BoxFlag_DisableIDString, line_key);
        DR_Bucket *line_bucket = dr_bucket_make();
        dr_push_bucket(line_bucket);
        ui_box_equip_display_fstrs(line_box, &line_fstrs);
        
        // rjf: extra rendering for strings that are currently being searched for
        if(!search_query_invalidated && params->search_query.size != 0)
        {
          for(U64 needle_pos = 0; needle_pos < line_string.size;)
          {
            needle_pos = str8_find_needle(line_string, needle_pos, params->search_query, StringMatchFlag_CaseInsensitive);
            if(needle_pos < line_string.size)
            {
              Rng1U64 match_range = r1u64(needle_pos, needle_pos+params->search_query.size);
              Rng1F32 match_column_pixel_off_range =
              {
                fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, match_range.min)).x,
                fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, match_range.max)).x,
              };
              Rng2F32 match_rect =
              {
                line_box->rect.x0+line_num_padding_px+match_column_pixel_off_range.min,
                line_box->rect.y0,
                line_box->rect.x0+line_num_padding_px+match_column_pixel_off_range.max+2.f,
                line_box->rect.y1,
              };
              Vec4F32 color = pop_color;
              if(!is_focused)
              {
                color.w *= 0.5f;
              }
              color.w *= 0.2f;
              dr_rect(match_rect, color, 4.f, 0, 1.f);
              needle_pos += 1;
            }
          }
        }
        
        // rjf: extra rendering for list(text_range*color)
        {
          U64 prev_line_size = (line_idx > 0) ? params->line_text[line_idx-1].size : 0;
          U64 next_line_size = (line_idx+1 < dim_1s64(params->line_num_range)) ? params->line_text[line_idx+1].size : 0;
          for(TxtRngColorPairNode *n = first_txt_rng_color_pair; n != 0; n = n->next)
          {
            TxtRng select_range = n->rng;
            TxtRng line_range = txt_rng(txt_pt(line_num, 1), txt_pt(line_num, line_string.size+1));
            TxtRng select_range_in_line = txt_rng_intersect(select_range, line_range);
            if(!txt_pt_match(select_range_in_line.min, select_range_in_line.max) &&
               txt_pt_less_than(select_range_in_line.min, select_range_in_line.max))
            {
              TxtRng prev_line_range = txt_rng(txt_pt(line_num-1, 1), txt_pt(line_num-1, prev_line_size+1));
              TxtRng next_line_range = txt_rng(txt_pt(line_num+1, 1), txt_pt(line_num+1, next_line_size+1));
              TxtRng select_range_in_prev_line = txt_rng_intersect(prev_line_range, select_range);
              TxtRng select_range_in_next_line = txt_rng_intersect(next_line_range, select_range);
              B32 prev_line_good = (!txt_pt_match(select_range_in_prev_line.min, select_range_in_prev_line.max) &&
                                    txt_pt_less_than(select_range_in_prev_line.min, select_range_in_prev_line.max));
              B32 next_line_good = (!txt_pt_match(select_range_in_next_line.min, select_range_in_next_line.max) &&
                                    txt_pt_less_than(select_range_in_next_line.min, select_range_in_next_line.max));
              Rng1S64 select_column_range_in_line =
              {
                (select_range.min.line == line_num) ? select_range.min.column : 1,
                (select_range.max.line == line_num) ? select_range.max.column : (S64)(line_string.size+1),
              };
              Rng1F32 select_column_pixel_off_range =
              {
                fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, select_column_range_in_line.min-1)).x,
                fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, select_column_range_in_line.max-1)).x,
              };
              Rng2F32 select_rect =
              {
                line_box->rect.x0+line_num_padding_px+select_column_pixel_off_range.min-2.f,
                floor_f32(line_box->rect.y0) - 1.f,
                line_box->rect.x0+line_num_padding_px+select_column_pixel_off_range.max+2.f,
                ceil_f32(line_box->rect.y1) + 1.f,
              };
              Vec4F32 color = n->color;
              if(!is_focused)
              {
                color.w *= 0.5f;
              }
              F32 rounded_radius = params->font_size*0.4f;
              R_Rect2DInst *inst = dr_rect(select_rect, color, rounded_radius, 0, 1);
              inst->corner_radii[Corner_00] = !prev_line_good || select_range_in_prev_line.min.column > select_range_in_line.min.column ? rounded_radius : 0.f;
              inst->corner_radii[Corner_10] = (!prev_line_good || select_range_in_line.max.column > select_range_in_prev_line.max.column || select_range_in_line.max.column < select_range_in_prev_line.min.column) ? rounded_radius : 0.f;
              inst->corner_radii[Corner_01] = (!next_line_good || select_range_in_next_line.min.column > select_range_in_line.min.column || select_range_in_next_line.max.column < select_range_in_line.min.column) ? rounded_radius : 0.f;
              inst->corner_radii[Corner_11] = !next_line_good || select_range_in_line.max.column > select_range_in_next_line.max.column ? rounded_radius : 0.f;
            }
          }
        }
        
        // rjf: extra rendering for cursor position
        if(cursor->line == line_num)
        {
          S64 column = cursor->column;
          Vec2F32 advance = fnt_dim_from_tag_size_string(line_box->font, line_box->font_size, 0, params->tab_size, str8_prefix(line_string, column-1));
          F32 cursor_y = line_box->rect.y0-params->font_size*0.125f;
          F32 cursor_y__animated = ui_anim(ui_key_from_stringf(text_container_box->key, "cursor_y_px"), cursor_y);
          F32 cursor_off_pixels = advance.x;
          F32 cursor_off_pixels__animated = ui_anim(ui_key_from_stringf(text_container_box->key, "cursor_off_px"), cursor_off_pixels);
          F32 cursor_thickness = ClampBot(1.f, floor_f32(line_box->font_size/10.f));
          Rng2F32 cursor_rect =
          {
            ui_box_text_position(line_box).x+cursor_off_pixels,
            line_box->rect.y0-params->font_size*0.125f,
            ui_box_text_position(line_box).x+cursor_off_pixels+cursor_thickness,
            line_box->rect.y1+params->font_size*0.125f,
          };
          Rng1F32 trail_off_span = r1f32(cursor_off_pixels__animated, cursor_off_pixels);
          Rng2F32 trail_rect =
          {
            ui_box_text_position(line_box).x+trail_off_span.min,
            line_box->rect.y0-params->font_size*0.125f,
            ui_box_text_position(line_box).x+trail_off_span.max,
            line_box->rect.y1+params->font_size*0.125f,
          };
          Vec4F32 cursor_color = ui_color_from_name(s("cursor"));
          Vec4F32 trail_color = cursor_color;
          if(!is_focused)
          {
            cursor_color.w *= 0.5f;
          }
          trail_color.w *= 0.25f;
          dr_rect(cursor_rect, cursor_color, 1.f, 0, 0.f);
          if(do_cursor_trail && !ui_key_match(ui_active_key(UI_MouseButtonKind_Left), text_container_box->key))
          {
            R_Rect2DInst *trail_inst = dr_rect(trail_rect, trail_color, ui_top_font_size()*0.2f, 0, 1.f);
            trail_inst->shear = cursor_y - cursor_y__animated;
            if(cursor_off_pixels > cursor_off_pixels__animated)
            {
              trail_inst->dst = shift_2f32(trail_inst->dst, v2f32(0, -trail_inst->shear));
              trail_inst->colors[Corner_00].w *= 0.1f;
              trail_inst->colors[Corner_01].w *= 0.1f;
            }
            else
            {
              trail_inst->shear *= -1;
              trail_inst->colors[Corner_10].w *= 0.1f;
              trail_inst->colors[Corner_11].w *= 0.1f;
            }
          }
        }
        // rjf: equip bucket
        if(line_bucket->passes.count != 0)
        {
          ui_box_equip_draw_bucket(line_box, line_bucket);
        }
        
        dr_pop_bucket();
      }
    }
  }
  
  scratch_end(scratch);
  ProfEnd();
  return result;
}

internal RD_CodeSliceSignal
rd_code_slicef(RD_CodeSliceParams *params, TxtPt *cursor, TxtPt *mark, S64 *preferred_column, char *fmt, ...)
{
  Temp scratch = scratch_begin(0, 0);
  va_list args;
  va_start(args, fmt);
  String8 string = push_str8fv(scratch.arena, fmt, args);
  RD_CodeSliceSignal sig = rd_code_slice(params, cursor, mark, preferred_column, string);
  va_end(args);
  scratch_end(scratch);
  return sig;
}

internal B32
rd_do_txt_controls(TXT_TextInfo *info, String8 data, U64 line_count_per_page, TxtPt *cursor, TxtPt *mark, S64 *preferred_column)
{
  Temp scratch = scratch_begin(0, 0);
  B32 change = 0;
  for(UI_Event *evt = 0; ui_next_event(&evt);)
  {
    if(evt->kind != UI_EventKind_Navigate && evt->kind != UI_EventKind_Edit)
    {
      continue;
    }
    B32 taken = 0;
    String8 line = txt_string_from_info_data_line_num(info, data, cursor->line);
    UI_TxtOp single_line_op = ui_single_line_txt_op_from_event(scratch.arena, evt, line, *cursor, *mark);
    
    //- rjf: invalid single-line op or endpoint units => try multiline
    if(evt->delta_unit == UI_EventDeltaUnit_Whole || single_line_op.flags & UI_TxtOpFlag_Invalid)
    {
      U64 line_count = info->lines_count;
      String8 prev_line = txt_string_from_info_data_line_num(info, data, cursor->line-1);
      String8 next_line = txt_string_from_info_data_line_num(info, data, cursor->line+1);
      Vec2S32 delta = evt->delta_2s32;
      
      //- rjf: wrap lines right
      if(evt->delta_unit != UI_EventDeltaUnit_Whole && delta.x > 0 && cursor->column == line.size+1 && cursor->line+1 <= line_count)
      {
        cursor->line += 1;
        cursor->column = 1;
        *preferred_column = 1;
        change = 1;
        taken = 1;
      }
      
      //- rjf: wrap lines left
      if(evt->delta_unit != UI_EventDeltaUnit_Whole && delta.x < 0 && cursor->column == 1 && cursor->line-1 >= 1)
      {
        cursor->line -= 1;
        cursor->column = prev_line.size+1;
        *preferred_column = prev_line.size+1;
        change = 1;
        taken = 1;
      }
      
      //- rjf: movement down (plain)
      if(evt->delta_unit == UI_EventDeltaUnit_Char && delta.y > 0 && cursor->line+1 <= line_count)
      {
        cursor->line += 1;
        cursor->column = Min(*preferred_column, next_line.size+1);
        change = 1;
        taken = 1;
      }
      
      //- rjf: movement up (plain)
      if(evt->delta_unit == UI_EventDeltaUnit_Char && delta.y < 0 && cursor->line-1 >= 1)
      {
        cursor->line -= 1;
        cursor->column = Min(*preferred_column, prev_line.size+1);
        change = 1;
        taken = 1;
      }
      
      //- rjf: movement down (chunk)
      if(evt->delta_unit == UI_EventDeltaUnit_Word && delta.y > 0 && cursor->line+1 <= line_count)
      {
        for(S64 line_num = cursor->line+1; line_num <= line_count; line_num += 1)
        {
          String8 line = txt_string_from_info_data_line_num(info, data, line_num);
          U64 line_size = line.size;
          if(line_size == 0)
          {
            cursor->line = line_num;
            cursor->column = 1;
            break;
          }
          else if(line_num == line_count)
          {
            cursor->line = line_num;
            cursor->column = line_size+1;
          }
        }
        change = 1;
        taken = 1;
      }
      
      //- rjf: movement up (chunk)
      if(evt->delta_unit == UI_EventDeltaUnit_Word && delta.y < 0 && cursor->line-1 >= 1)
      {
        for(S64 line_num = cursor->line-1; line_num > 0; line_num -= 1)
        {
          String8 line = txt_string_from_info_data_line_num(info, data, line_num);
          U64 line_size = line.size;
          if(line_size == 0)
          {
            cursor->line = line_num;
            cursor->column = 1;
            break;
          }
          else if(line_num == 1)
          {
            cursor->line = line_num;
            cursor->column = 1;
          }
        }
        change = 1;
        taken = 1;
      }
      
      //- rjf: movement down (page)
      if(evt->delta_unit == UI_EventDeltaUnit_Page && delta.y > 0)
      {
        cursor->line += line_count_per_page;
        cursor->column = 1;
        cursor->line = Clamp(1, cursor->line, line_count);
        change = 1;
        taken = 1;
      }
      
      //- rjf: movement up (page)
      if(evt->delta_unit == UI_EventDeltaUnit_Page && delta.y < 0)
      {
        cursor->line -= line_count_per_page;
        cursor->column = 1;
        cursor->line = Clamp(1, cursor->line, line_count);
        change = 1;
        taken = 1;
      }
      
      //- rjf: movement to endpoint (+)
      if(evt->delta_unit == UI_EventDeltaUnit_Whole && (delta.y > 0 || delta.x > 0))
      {
        *cursor = txt_pt(line_count, info->lines_count ? dim_1u64(info->lines_ranges[info->lines_count-1])+1 : 1);
        change = 1;
        taken = 1;
      }
      
      //- rjf: movement to endpoint (-)
      if(evt->delta_unit == UI_EventDeltaUnit_Whole && (delta.y < 0 || delta.x < 0))
      {
        *cursor = txt_pt(1, 1);
        change = 1;
        taken = 1;
      }
      
      //- rjf: stick mark to cursor, when we don't want to keep it in the same spot
      if(!(evt->flags & UI_EventFlag_KeepMark))
      {
        *mark = *cursor;
      }
    }
    
    //- rjf: valid single-line op => do single-line op
    else
    {
      *cursor = single_line_op.cursor;
      *mark = single_line_op.mark;
      *preferred_column = cursor->column;
      change = 1;
      taken = 1;
    }
    
    //- rjf: copy
    if(evt->flags & UI_EventFlag_Copy)
    {
      String8 text = txt_string_from_info_data_txt_rng(info, data, txt_rng(*cursor, *mark));
      wm_set_clipboard_text(text);
      taken = 1;
    }
    
    //- rjf: consume
    if(taken)
    {
      ui_eat_event(evt);
    }
  }
  
  scratch_end(scratch);
  return change;
}

////////////////////////////////
//~ rjf: UI Widgets: Fancy Labels

internal DR_FStrList
rd_fstrs_from_rich_string(Arena *arena, String8 string)
{
  Temp scratch = scratch_begin(&arena, 1);
  typedef U32 StringPartFlags;
  enum
  {
    StringPartFlag_Code      = (1<<0),
    StringPartFlag_Underline = (1<<1),
    StringPartFlag_Bright    = (1<<2),
  };
  typedef struct StringPart StringPart;
  struct StringPart
  {
    StringPart *next;
    StringPartFlags flags;
    String8 string;
  };
  StringPart *first_part = 0;
  StringPart *last_part = 0;
  U64 active_part_start_idx = 0;
  StringPartFlags active_part_flags = 0;
  for(U64 idx = 0; idx <= string.size; idx += 1)
  {
    if(idx == string.size)
    {
      StringPart *p = push_array(scratch.arena, StringPart, 1);
      p->flags = active_part_flags;
      p->string = str8_substr(string, r1u64(active_part_start_idx, idx));
      SLLQueuePush(first_part, last_part, p);
    }
    else if(string.str[idx] == '`')
    {
      StringPart *p = push_array(scratch.arena, StringPart, 1);
      p->flags = active_part_flags;
      p->string = str8_substr(string, r1u64(active_part_start_idx, idx));
      SLLQueuePush(first_part, last_part, p);
      active_part_start_idx = idx+1;
      active_part_flags ^= StringPartFlag_Code;
    }
  }
  DR_FStrList fstrs = {0};
  for(StringPart *p = first_part; p != 0; p = p->next)
  {
    DR_FStr fstr = {0};
    {
      fstr.string = p->string;
      fstr.params.font   = ui_top_font();
      fstr.params.color  = ui_color_from_name(str8_lit("text"));
      fstr.params.size   = ui_top_font_size();
      fstr.params.raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Main);
      if(p->flags & StringPartFlag_Code)
      {
        fstr.params.font = rd_font_from_slot(RD_FontSlot_Code);
        fstr.params.raster_flags = rd_raster_flags_from_slot(RD_FontSlot_Code);
        fstr.params.color = rd_rgba_from_code_color_slot(RD_CodeColorSlot_CodeDefault);
      }
    }
    dr_fstrs_push(arena, &fstrs, &fstr);
  }
  scratch_end(scratch);
  return fstrs;
}

internal UI_Signal
rd_label(String8 string)
{
  Temp scratch = scratch_begin(0, 0);
  DR_FStrList fstrs = rd_fstrs_from_rich_string(scratch.arena, string);
  UI_Box *box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
  ui_box_equip_display_fstrs(box, &fstrs);
  UI_Signal sig = ui_signal_from_box(box);
  scratch_end(scratch);
  return sig;
}

internal UI_Signal
rd_error_label(String8 string)
{
  UI_Box *box = ui_build_box_from_key(0, ui_key_zero());
  UI_Signal sig = ui_signal_from_box(box);
  UI_Parent(box)
  {
    ui_set_next_font(rd_font_from_slot(RD_FontSlot_Icons));
    ui_set_next_text_raster_flags(FNT_RasterFlag_Smooth);
    ui_set_next_text_alignment(UI_TextAlign_Center);
    UI_TagF("weak") UI_PrefWidth(ui_em(2.25f, 1.f)) ui_label(rd_icon_kind_text_table[RD_IconKind_WarningBig]);
    UI_PrefWidth(ui_text_dim(10, 0)) rd_label(string);
  }
  return sig;
}

internal B32
rd_help_label(String8 string)
{
  B32 result = 0;
  UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable, "###%S_help_label", string);
  UI_Signal sig = ui_signal_from_box(box);
  UI_Parent(box)
  {
    UI_PrefWidth(ui_pct(1, 0)) ui_label(string);
    if(ui_hovering(sig)) UI_PrefWidth(ui_em(2.25f, 1))
    {
      result = 1;
      ui_set_next_font(rd_font_from_slot(RD_FontSlot_Icons));
      ui_set_next_text_raster_flags(FNT_RasterFlag_Smooth);
      ui_set_next_text_alignment(UI_TextAlign_Center);
      UI_Box *help_hoverer = ui_build_box_from_stringf(UI_BoxFlag_DrawText|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawHotEffects, "###help_hoverer_%S", string);
      ui_box_equip_display_string(help_hoverer, rd_icon_kind_text_table[RD_IconKind_QuestionMark]);
      if(!contains_2f32(help_hoverer->rect, ui_mouse()))
      {
        result = 0;
      }
    }
  }
  return result;
}

internal DR_FStrList
rd_fstrs_from_code_string(Arena *arena, F32 alpha, B32 indirection_size_change, Vec4F32 base_color, String8 string)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(&arena, 1);
  DR_FStrList fstrs = {0};
  TXT_TokenArray tokens = txt_token_array_from_string__c_cpp(scratch.arena, 0, string);
  TXT_Token *tokens_opl = tokens.v+tokens.count;
  S32 indirection_counter = 0;
  indirection_size_change = 0;
  B32 preceded_by_dot = 0;
  for(TXT_Token *token = tokens.v; token < tokens_opl; token += 1)
  {
    RD_CodeColorSlot token_color_slot = rd_code_color_slot_from_txt_token_kind(token->kind);
    Vec4F32 token_color_rgba = rd_rgba_from_code_color_slot(token_color_slot);
    String8 token_string = str8_substr(string, token->range);
    if(str8_match(token_string, str8_lit("{"), 0)) { indirection_counter += 1; }
    if(str8_match(token_string, str8_lit("["), 0)) { indirection_counter += 1; }
    indirection_counter = ClampBot(0, indirection_counter);
    switch(token->kind)
    {
      default:
      {
        token_color_rgba.w *= alpha;
        DR_FStr fstr =
        {
          token_string,
          {
            ui_top_font(),
            ui_top_text_raster_flags(),
            token_color_rgba,
            ui_top_font_size() * (1.f - !!indirection_size_change*(indirection_counter/10.f)),
          }
        };
        dr_fstrs_push(arena, &fstrs, &fstr);
      }break;
      case TXT_TokenKind_Identifier:
      case TXT_TokenKind_Keyword:
      {
        RD_CodeColorSlot lookup_theme_color_slot = RD_CodeColorSlot_CodeDefault;
        B32 is_called = (token+1 < tokens_opl && token[1].kind == TXT_TokenKind_Symbol && str8_match(str8_substr(string, token[1].range), str8_lit("("), 0));
        if(!preceded_by_dot)
        {
          lookup_theme_color_slot = rd_code_color_slot_from_txt_token_kind_lookup_string(token->kind, token_string, 1, is_called);
        }
        if(lookup_theme_color_slot != RD_CodeColorSlot_CodeDefault)
        {
          Vec4F32 lookup_color = rd_rgba_from_code_color_slot(lookup_theme_color_slot);
          F32 lookup_color_mix_t = ui_anim(ui_key_from_stringf(ui_key_zero(), "%S_lookup", token_string), 1.f);
          token_color_rgba = mix_4f32(token_color_rgba, lookup_color, lookup_color_mix_t);
        }
        token_color_rgba.w *= alpha;
        DR_FStr fstr =
        {
          token_string,
          {
            ui_top_font(),
            ui_top_text_raster_flags(),
            token_color_rgba,
            ui_top_font_size() * (1.f - !!indirection_size_change*(indirection_counter/10.f)),
          },
        };
        dr_fstrs_push(arena, &fstrs, &fstr);
      }break;
      case TXT_TokenKind_Numeric:
      {
        token_color_rgba.w *= alpha;
        Vec4F32 token_color_rgba_alt = rd_rgba_from_code_color_slot(RD_CodeColorSlot_CodeNumericAltDigitGroup);
        token_color_rgba_alt.w *= alpha;
        F32 font_size = ui_top_font_size() * (1.f - !!indirection_size_change*(indirection_counter/10.f));
        
        // rjf: unpack string
        U32 base = 10;
        U64 prefix_skip = 0;
        U64 digit_group_size = 3;
        if(str8_match(str8_prefix(token_string, 2), str8_lit("0x"), StringMatchFlag_CaseInsensitive))
        {
          base = 16;
          prefix_skip = 2;
          digit_group_size = 4;
        }
        else if(str8_match(str8_prefix(token_string, 2), str8_lit("0b"), StringMatchFlag_CaseInsensitive))
        {
          base = 2;
          prefix_skip = 2;
          digit_group_size = 8;
        }
        else if(str8_match(str8_prefix(token_string, 2), str8_lit("0o"), StringMatchFlag_CaseInsensitive))
        {
          base = 8;
          prefix_skip = 2;
          digit_group_size = 2;
        }
        
        // rjf: grab string parts
        U64 dot_pos = str8_find_needle(token_string, 0, str8_lit("."), 0);
        String8 prefix = str8_prefix(token_string, prefix_skip);
        String8 whole = str8_substr(token_string, r1u64(prefix_skip, dot_pos));
        String8 decimal = str8_skip(token_string, dot_pos);
        
        // rjf: determine # of digits
        U64 num_digits = 0;
        for(U64 idx = 0; idx < whole.size; idx += 1)
        {
          num_digits += char_is_digit(whole.str[idx], base);
        }
        
        // rjf: push prefix
        {
          DR_FStr fstr =
          {
            prefix,
            {
              ui_top_font(),
              ui_top_text_raster_flags(),
              token_color_rgba,
              font_size,
            },
          };
          dr_fstrs_push(arena, &fstrs, &fstr);
        }
        
        // rjf: push digit groups
        {
          B32 odd = 0;
          U64 start_idx = 0;
          U64 num_digits_passed = digit_group_size - num_digits%digit_group_size;
          for(U64 idx = 0; idx <= whole.size; idx += 1)
          {
            U8 byte = idx < whole.size ? whole.str[idx] : 0;
            if(num_digits_passed >= digit_group_size || idx == whole.size)
            {
              num_digits_passed = 0;
              if(start_idx < idx)
              {
                DR_FStr fstr =
                {
                  str8_substr(whole, r1u64(start_idx, idx)),
                  {
                    ui_top_font(),
                    ui_top_text_raster_flags(),
                    odd ? token_color_rgba_alt : token_color_rgba,
                    font_size,
                  },
                };
                dr_fstrs_push(arena, &fstrs, &fstr);
                start_idx = idx;
                odd ^= 1;
              }
            }
            if(char_is_digit(byte, base))
            {
              num_digits_passed += 1;
            }
          }
        }
        
        // rjf: push decimal
        {
          DR_FStr fstr =
          {
            decimal,
            {
              ui_top_font(),
              ui_top_text_raster_flags(),
              token_color_rgba,
              font_size,
            },
          };
          dr_fstrs_push(arena, &fstrs, &fstr);
        }
        
      }break;
    }
    if(token->kind == TXT_TokenKind_Symbol && str8_match(token_string, str8_lit("."), 0))
    {
      preceded_by_dot = 1;
    }
    else
    {
      preceded_by_dot = 0;
    }
    if(str8_match(token_string, str8_lit("}"), 0)) { indirection_counter -= 1; }
    if(str8_match(token_string, str8_lit("]"), 0)) { indirection_counter -= 1; }
    indirection_counter = ClampBot(0, indirection_counter);
  }
  scratch_end(scratch);
  ProfEnd();
  return fstrs;
}

internal UI_Box *
rd_code_label(F32 alpha, B32 indirection_size_change, Vec4F32 base_color, String8 string)
{
  Temp scratch = scratch_begin(0, 0);
  DR_FStrList fstrs = rd_fstrs_from_code_string(scratch.arena, alpha, indirection_size_change, base_color, string);
  UI_Box *box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
  ui_box_equip_display_fstrs(box, &fstrs);
  scratch_end(scratch);
  return box;
}

////////////////////////////////
//~ rjf: UI Widgets: Line Edit

internal UI_Signal
rd_cell(RD_CellParams *params, String8 string)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(0, 0);
  B32 do_cursor_trail = rd_setting_b32_from_name(str8_lit("cursor_trail"));
  
  //////////////////////////////
  //- rjf: unpack visual metrics
  //
  F32 expander_size_px = floor_f32(ui_top_font_size()*2.f);
  
  //////////////////////////////
  //- rjf: make key
  //
  UI_Key key = ui_key_from_string(ui_active_seed_key(), string);
  
  //////////////////////////////
  //- rjf: calculate & push focus
  //
  B32 is_auto_focus_hot = ui_is_key_auto_focus_hot(key);
  B32 is_auto_focus_active = ui_is_key_auto_focus_active(key);
  if(is_auto_focus_hot) { ui_push_focus_hot(UI_FocusKind_On); }
  if(is_auto_focus_active) { ui_push_focus_active(UI_FocusKind_On); }
  B32 is_focus_hot    = ui_is_focus_hot();
  B32 is_focus_active = ui_is_focus_active();
  B32 is_focus_hot_disabled = (!is_focus_hot && ui_top_focus_hot() == UI_FocusKind_On);
  B32 is_focus_active_disabled = (!is_focus_active && ui_top_focus_active() == UI_FocusKind_On);
  
  //////////////////////////////
  //- rjf: determine which sub-cell components we'll build
  //
  // (the base line edit textual label / editor is always built, but this can be enriched
  // with extra widgets & metadata)
  //
  B32 build_toggle_switch = !!(params->flags & RD_CellFlag_ToggleSwitch) && !is_focus_active;
  B32 build_slider        = !!(params->flags & RD_CellFlag_Slider) && !is_focus_active;
  B32 build_bindings      = !!(params->flags & RD_CellFlag_Bindings) && !is_focus_active;
  B32 build_lhs_name_desc = (params->meta_fstrs.node_count != 0 || params->description.size != 0);
  B32 build_line_edit     = (params->pre_edit_value.size != 0 || params->value_fstrs.node_count != 0);
  B32 build_note          = (params->note_fstrs.node_count != 0 && !is_focus_active);
  DR_FStrList lhs_name_fstrs = params->meta_fstrs;
  DR_FStrList value_name_fstrs = params->value_fstrs;
  DR_FStrList note_fstrs = params->note_fstrs;
  
  //////////////////////////////
  //- rjf: determine autocompletion string
  //
  String8 autocomplete_hint_string = {0};
  if(is_focus_active)
  {
    autocomplete_hint_string = ui_autocomplete_string();
  }
  
  //////////////////////////////
  //- rjf: build top-level box
  //
  if(is_focus_active || is_focus_active_disabled)
  {
    ui_set_next_hover_cursor(WM_Cursor_IBar);
  }
  UI_Box *box = ui_build_box_from_key(UI_BoxFlag_MouseClickable|
                                      (!!build_lhs_name_desc*UI_BoxFlag_DisableFocusBorder)|
                                      (!!(params->flags & RD_CellFlag_KeyboardClickable)*UI_BoxFlag_KeyboardClickable)|
                                      UI_BoxFlag_ClickToFocus|
                                      (!!(params->flags & RD_CellFlag_Button)*UI_BoxFlag_DrawHotEffects)|
                                      (!!(params->flags & RD_CellFlag_SingleClickActivate)*UI_BoxFlag_DrawActiveEffects)|
                                      (!(params->flags & RD_CellFlag_NoBackground)*UI_BoxFlag_DrawBackground)|
                                      (!!(params->flags & RD_CellFlag_Border)*UI_BoxFlag_DrawBorder)|
                                      ((is_auto_focus_hot || is_auto_focus_active)*UI_BoxFlag_KeyboardClickable)|
                                      (is_focus_active || is_focus_active_disabled)*(UI_BoxFlag_Clip),
                                      key);
  
  //////////////////////////////
  //- rjf: build indent
  //
  UI_Parent(box) for(S32 idx = 0; idx < params->depth; idx += 1)
  {
    ui_set_next_flags(UI_BoxFlag_DrawSideLeft);
    ui_spacer(ui_em(1.f, 1.f));
  }
  
  //////////////////////////////
  //- rjf: build expander (or placeholder, or space)
  //
  {
    //- rjf: build expander
    if(params->flags & RD_CellFlag_Expander) UI_PrefWidth(ui_px(expander_size_px, 1.f)) UI_Parent(box)
      UI_Flags(UI_BoxFlag_DrawSideLeft)
      UI_Focus(UI_FocusKind_Off)
    {
      UI_Signal expander_sig = ui_expanderf(params->expanded_out[0], "expander");
      if(ui_pressed(expander_sig))
      {
        params->expanded_out[0] ^= 1;
      }
    }
    
    //- rjf: build expander placeholder
    else if(params->flags & RD_CellFlag_ExpanderPlaceholder) UI_Parent(box) UI_PrefWidth(ui_px(expander_size_px, 1.f)) UI_Focus(UI_FocusKind_Off)
    {
      UI_TagF("weak")
        UI_Flags(UI_BoxFlag_DrawSideLeft)
        RD_Font(RD_FontSlot_Icons)
        UI_TextAlignment(UI_TextAlign_Center)
        ui_label(rd_icon_kind_text_table[RD_IconKind_Dot]);
    }
    
    //- rjf: build expander space
    else if(params->flags & RD_CellFlag_ExpanderSpace) UI_Parent(box) UI_Focus(UI_FocusKind_Off)
    {
      UI_Flags(UI_BoxFlag_DrawSideLeft) ui_spacer(ui_px(expander_size_px, 1.f));
    }
  }
  
  //////////////////////////////
  //- rjf: build left-hand-side container box
  //
  UI_Box *lhs_box = &ui_nil_box;
  if(build_lhs_name_desc)
  {
    UI_Parent(box) UI_WidthFill UI_ChildLayoutAxis(Axis2_Y)
    {
      if(ui_top_text_alignment() == UI_TextAlign_Left && (params->flags & (RD_CellFlag_Expander|RD_CellFlag_ExpanderSpace|RD_CellFlag_ExpanderPlaceholder)) == 0)
      {
        ui_spacer(ui_em(1.f, 1.f));
      }
      lhs_box = ui_build_box_from_stringf(0, "lhs_box");
    }
  }
  
  //////////////////////////////
  //- rjf: build left-hand-side name/desc box
  //
  if(build_lhs_name_desc) UI_Parent(lhs_box) UI_Padding(ui_em(3.f, 0.f)) UI_WidthFill UI_HeightFill
  {
    FuzzyMatchRangeList fuzzy_matches = {0};
    if(params->search_needle.size != 0)
    {
      fuzzy_matches = dr_fuzzy_match_find_from_fstrs(scratch.arena, &lhs_name_fstrs, params->search_needle);
    }
    UI_Row
    {
      UI_Box *name_box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
      ui_box_equip_display_fstrs(name_box, &lhs_name_fstrs);
      ui_box_equip_fuzzy_match_ranges(name_box, &fuzzy_matches);
    }
    if(params->description.size != 0) RD_Font(RD_FontSlot_Main) UI_FontSize(ui_top_font_size()*0.85f)
    {
      UI_Row
      {
        UI_Box *desc_box = ui_label(params->description).box;
        FuzzyMatchRangeList desc_fuzzy_matches = fuzzy_match_find(scratch.arena, params->search_needle, params->description);
        ui_box_equip_fuzzy_match_ranges(desc_box, &desc_fuzzy_matches);
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build line edit container box
  //
  UI_Box *edit_box = &ui_nil_box;
  if((is_focus_active || is_focus_active_disabled) || build_line_edit)
    UI_Parent(box)
  {
    B32 is_editing = (is_focus_active || is_focus_active_disabled);
    UI_Size edit_box_size = ui_pct(1, 0);
    if(build_lhs_name_desc)
    {
      if(is_editing)
      {
        edit_box_size = ui_px(floor_f32(dim_2f32(box->rect).x*0.5f), 1.f);
      }
      else
      {
        edit_box_size = ui_children_sum(1);
      }
    }
    UI_PrefWidth(edit_box_size)
    {
      if(ui_top_px_height() > ui_top_font_size()*3.f)
      {
        ui_set_next_pref_width(ui_children_sum(1));
        UI_Column UI_Padding(ui_em(1, 0)) UI_Focus(UI_FocusKind_On)
        {
          UI_PrefHeight(ui_em(3.f, 1.f)) UI_CornerRadius(ui_top_font_size()*0.5f)
            edit_box = ui_build_box_from_stringf((!!is_editing*UI_BoxFlag_DrawBorder)|
                                                 UI_BoxFlag_Clickable|
                                                 UI_BoxFlag_DisableFocusOverlay,
                                                 "edit_box");
          if(params->line_edit_key_out)
          {
            params->line_edit_key_out[0] = edit_box->key;
          }
        }
        if(ui_top_text_alignment() == UI_TextAlign_Left)
        {
          ui_spacer(ui_em(1.f, 1.f));
        }
      }
      else
      {
        edit_box = ui_build_box_from_stringf(0, "edit_box");
        if(params->line_edit_key_out)
        {
          params->line_edit_key_out[0] = edit_box->key;
        }
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build edit-button, if line edit is embedded, and has no string
  //
  B32 edit_started = 0;
  if(params->flags & RD_CellFlag_EmptyEditButton && !is_focus_active && !is_focus_active_disabled && build_lhs_name_desc && build_line_edit && value_name_fstrs.total_size == 0)
  {
    UI_TagF(".")
      UI_TagF("weak")
      UI_TagF("implicit")
      UI_Parent(box)
      UI_PrefWidth(ui_em(2.f, 1.f))
    {
      UI_Column
        UI_Padding(ui_pct(1, 0))
        UI_PrefHeight(ui_em(2.f, 1.f))
        UI_CornerRadius(ui_top_font_size()*0.5f)
        RD_Font(RD_FontSlot_Icons)
        UI_TextAlignment(UI_TextAlign_Center)
      {
        UI_Box *edit_start_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|
                                                           UI_BoxFlag_DrawHotEffects|
                                                           UI_BoxFlag_DrawBorder|
                                                           UI_BoxFlag_DrawBackground|
                                                           UI_BoxFlag_DisableFocusOverlay|
                                                           UI_BoxFlag_DisableFocusBorder|
                                                           UI_BoxFlag_Clickable,
                                                           "%S##edit", rd_icon_kind_text_table[RD_IconKind_Pencil]);
        UI_Signal sig = ui_signal_from_box(edit_start_box);
        if(ui_pressed(sig))
        {
          edit_started = 1;
        }
      }
      ui_spacer(ui_em(1.f, 1.f));
    }
  }
  
  //////////////////////////////
  //- rjf: build scrollable container box
  //
  UI_Box *scrollable_box = &ui_nil_box;
  if(edit_box != &ui_nil_box)
  {
    UI_Parent(edit_box) UI_PrefWidth(ui_children_sum(0))
    {
      scrollable_box = ui_build_box_from_stringf(is_focus_active*(UI_BoxFlag_AllowOverflowX), "scroll_box_%p", params->edit_buffer);
    }
  }
  
  //////////////////////////////
  //- rjf: build revert-button
  //
  if(params->flags & RD_CellFlag_RevertButton && !is_focus_active && !is_focus_active_disabled)
  {
    UI_Parent(edit_box)
      UI_PrefWidth(ui_em(2.f, 1.f))
    {
      UI_TagF(".")
        UI_TagF("weak")
        UI_TagF("implicit")
        UI_Column
        UI_Padding(ui_pct(1, 0))
        UI_PrefHeight(ui_em(2.f, 1.f))
        UI_CornerRadius(ui_top_font_size()*0.5f)
        RD_Font(RD_FontSlot_Icons)
        UI_TextAlignment(UI_TextAlign_Center)
      {
        UI_Box *revert_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText|
                                                       UI_BoxFlag_DrawHotEffects|
                                                       UI_BoxFlag_DrawBorder|
                                                       UI_BoxFlag_DrawBackground|
                                                       UI_BoxFlag_DisableFocusOverlay|
                                                       UI_BoxFlag_DisableFocusBorder|
                                                       UI_BoxFlag_Clickable,
                                                       "%S##revert", rd_icon_kind_text_table[RD_IconKind_Undo]);
        UI_Signal sig = ui_signal_from_box(revert_box);
        if(ui_hovering(sig)) UI_Tooltip RD_Font(RD_FontSlot_Main)
        {
          ui_state->tooltip_anchor_key = revert_box->key;
          ui_label(str8_lit("Revert To Default"));
        }
        if(ui_pressed(sig) && params->revert_out)
        {
          params->revert_out[0] = 1;
        }
      }
      // TODO(rjf): @hack
      if(build_toggle_switch || build_slider)
      {
        ui_spacer(ui_em(1.f, 1.f));
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build toggle-switch
  //
  if(build_toggle_switch) UI_Parent(box)
  {
    B32 is_toggled = !!params->toggled_out[0];
    F32 toggle_t = ui_anim(ui_key_from_stringf(key, "toggled"), (F32)is_toggled, .initial = (F32)is_toggled, .rate = rd_state->menu_animation_rate);
    F32 height_px = ceil_f32(ui_top_font_size() * 1.75f);
    F32 padding_px = ceil_f32((ui_top_px_height() - height_px) / 2.f);
    UI_PrefWidth(ui_children_sum(1.f))
      UI_HeightFill
      UI_Column UI_Padding(ui_px(padding_px, 1.f))
      UI_Row
    {
      if(ui_top_text_alignment() == UI_TextAlign_Center)
      {
        ui_spacer(ui_em(1.f, 0.f));
      }
      UI_PrefWidth(ui_em(3.5f, 1.f))
        UI_PrefHeight(ui_px(height_px, 1.f))
        UI_CornerRadius(floor_f32(height_px/2.f - 1.f))
        UI_TagF(is_toggled ? "good_pop" : "")
        UI_GroupKey(ui_key_from_stringf(ui_key_zero(), "toggle_switch_group_key"))
      {
        UI_Box *switch_box = ui_build_box_from_stringf(UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_Clickable, "toggle_switch");
        UI_Parent(switch_box)
        {
          RD_Font(RD_FontSlot_Icons) UI_PrefWidth(ui_pct(toggle_t, 0)) UI_Transparency(1.f - toggle_t)
          {
            ui_build_box_from_stringf(UI_BoxFlag_DisableTextTrunc | (toggle_t > 0.001f ? UI_BoxFlag_DrawText : 0),
                                      "%S", rd_icon_kind_text_table[RD_IconKind_Check]); 
          }
          UI_BackgroundColor(ui_color_from_name(str8_lit("text")))
            UI_PrefWidth(ui_px(height_px, 1.f))
          {
            F32 extratoggler_padding_px = floor_f32(ui_top_font_size()*0.35f);
            F32 toggler_size_px = ceil_f32(height_px - extratoggler_padding_px*2.f) - 1.f;
            UI_Column UI_Padding(ui_px(extratoggler_padding_px, 1.f))
              UI_Row UI_Padding(ui_px(extratoggler_padding_px, 1.f))
              UI_PrefWidth(ui_px(toggler_size_px, 1.f))
              UI_PrefHeight(ui_px(toggler_size_px, 1.f))
              UI_CornerRadius(floor_f32(toggler_size_px/2.f - 1.f))
            {
              ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawDropShadow, ui_key_zero());
            }
          }
          ui_spacer(ui_pct(1.f-toggle_t, 0));
        }
        UI_Signal switch_sig = ui_signal_from_box(switch_box);
        
        // rjf: press -> toggle, & gather this key
        if(ui_pressed(switch_sig))
        {
          if(ui_dragging(switch_sig))
          {
            ui_store_drag_struct(&switch_box->key);
          }
          params->toggled_out[0] ^= 1;
        }
        
        // rjf: dragging -> check if key is in batch of touched keys. if so, do nothing, otherwise, toggle.
        // always store this new key if not in batch
        if(ui_dragging(switch_sig))
        {
          String8 all_keys_data = ui_get_drag_data(sizeof(UI_Key));
          UI_Key *keys = (UI_Key *)all_keys_data.str;
          U64 keys_count = all_keys_data.size / sizeof(UI_Key);
          B32 key_is_touched = 0;
          for EachIndex(idx, keys_count)
          {
            if(ui_key_match(keys[idx], switch_box->key))
            {
              key_is_touched = 1;
              break;
            }
          }
          if(!key_is_touched)
          {
            params->toggled_out[0] ^= 1;
            UI_Key *new_keys = push_array(scratch.arena, UI_Key, keys_count+1);
            MemoryCopy(new_keys, keys, sizeof(UI_Key)*keys_count);
            new_keys[keys_count] = switch_box->key;
            ui_store_drag_data(str8((U8 *)new_keys, sizeof(UI_Key) * (keys_count+1)));
          }
        }
      }
      if(ui_top_text_alignment() == UI_TextAlign_Center)
      {
        ui_spacer(ui_em(1.f, 0.f));
      }
    }
    if(ui_top_text_alignment() == UI_TextAlign_Left)
    {
      ui_spacer(ui_em(1.f, 1.f));
    }
  }
  
  //////////////////////////////
  //- rjf: build slider
  //
  if(build_slider) UI_Parent(box)
  {
    F32 height_px = ceil_f32(ui_top_font_size() * 1.75f);
    F32 padding_px = ceil_f32((ui_top_px_height() - height_px) / 2.f);
    UI_PrefWidth(ui_children_sum(1.f))
      UI_HeightFill
      UI_Column UI_Padding(ui_px(padding_px, 1.f))
      UI_Row
      UI_PrefWidth(ui_pct(0.5f - 0.2f*(!!build_lhs_name_desc), 0.f))
      UI_PrefHeight(ui_px(height_px, 1.f))
      UI_CornerRadius(floor_f32(height_px/2.f - 1.f))
    {
      F32 extratoggler_padding_px = floor_f32(ui_top_font_size()*0.35f);
      F32 toggler_size_px = ceil_f32(height_px - extratoggler_padding_px*2.f) - 1.f;
      ui_set_next_hover_cursor(WM_Cursor_LeftRight);
      UI_Box *slider_box = ui_build_box_from_stringf(UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_Clickable, "slider");
      UI_Parent(slider_box) UI_TagF("pop")
      {
        UI_Signal sig = ui_signal_from_box(slider_box);
        if(ui_dragging(sig))
        {
          if(ui_pressed(sig))
          {
            ui_store_drag_struct(params->slider_value_out);
          }
          F32 draggable_region_size_px = dim_2f32(slider_box->rect).x - (extratoggler_padding_px*2 + toggler_size_px);
          F32 initial_pct = *ui_get_drag_struct(F32);
          F32 current_pct = initial_pct + (ui_drag_delta().x / draggable_region_size_px);
          params->slider_value_out[0] = current_pct;
        }
        
        UI_Box *fill_box = &ui_nil_box;
        UI_PrefWidth(ui_children_sum(0))
          UI_MinWidth(toggler_size_px + extratoggler_padding_px*2)
          fill_box = ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder, ui_key_zero());
        UI_Parent(fill_box)
        {
          ui_spacer(ui_pct(Clamp(0, params->slider_value_out[0], 1), 0.f));
          UI_BackgroundColor(ui_color_from_name(str8_lit("text")))
            UI_PrefWidth(ui_px(height_px, 1.f))
          {
            UI_Column UI_Padding(ui_px(extratoggler_padding_px, 1.f))
              UI_Row UI_Padding(ui_px(extratoggler_padding_px, 1.f))
              UI_PrefWidth(ui_px(toggler_size_px, 1.f))
              UI_PrefHeight(ui_px(toggler_size_px, 1.f))
              UI_CornerRadius(floor_f32(toggler_size_px/2.f - 1.f))
            {
              ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawDropShadow, ui_key_zero());
            }
          }
        }
        ui_spacer(ui_pct(1-Clamp(0, params->slider_value_out[0], 1), 0.f));
      }
    }
    ui_spacer(ui_em(1.f, 1.f));
  }
  
  //////////////////////////////
  //- rjf: build bindings
  //
  if(build_bindings) UI_Parent(box) RD_Font(RD_FontSlot_Main) UI_PermissionFlags(UI_PermissionFlag_Clicks)
  {
    UI_PrefWidth(ui_children_sum(1)) UI_Column UI_Padding(ui_px(ui_top_px_height()*0.2f, 1.f)) UI_HeightFill
    {
      UI_PrefWidth(ui_children_sum(1)) UI_Row UI_Padding(ui_em(1.f, 1.f))
      {
        rd_cmd_binding_buttons(params->bindings_name, params->search_needle, 1);
      }
    }
  }
  
  //////////////////////////////
  //- rjf: build notes
  //
  if(build_note) UI_Parent(box) UI_PrefWidth(params->note_width)
  {
    UI_Box *note_box = ui_build_box_from_key(UI_BoxFlag_DrawText, ui_key_zero());
    ui_box_equip_display_fstrs(note_box, &note_fstrs);
  }
  
  //////////////////////////////
  //- rjf: do non-textual edits (delete, copy, cut)
  //
  B32 commit = 0;
  if(!is_focus_active && is_focus_hot)
  {
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      if(evt->flags & UI_EventFlag_Copy)
      {
        wm_set_clipboard_text(params->pre_edit_value);
      }
      if(evt->flags & UI_EventFlag_Delete)
      {
        commit = 1;
        params->edit_string_size_out[0] = 0;
      }
    }
  }
  
  //////////////////////////////
  //- rjf: get signal
  //
  UI_Signal sig = ui_signal_from_box(box);
  if(commit)
  {
    sig.f |= UI_SignalFlag_Commit;
  }
  
  //////////////////////////////
  //- rjf: do start/end editing interaction
  //
  B32 focus_started = 0;
  if(!is_focus_active)
  {
    B32 start_editing_via_sig = (ui_double_clicked(sig) || sig.f&UI_SignalFlag_KeyboardPressed);
    B32 start_editing_via_typing = 0;
    if(is_focus_hot)
    {
      for(UI_Event *evt = 0; ui_next_event(&evt);)
      {
        if(evt->string.size != 0 || evt->flags & UI_EventFlag_Paste)
        {
          start_editing_via_typing = 1;
          break;
        }
      }
    }
    if(is_focus_hot && ui_slot_press(UI_EventActionSlot_Edit))
    {
      start_editing_via_typing = 1;
    }
    if(start_editing_via_sig || start_editing_via_typing)
    {
      String8 edit_string = params->pre_edit_value;
      edit_string.size = Min(params->edit_buffer_size, params->pre_edit_value.size);
      MemoryCopy(params->edit_buffer, edit_string.str, edit_string.size);
      params->edit_string_size_out[0] = edit_string.size;
      ui_set_auto_focus_active_key(key);
      if(!(params->flags & RD_CellFlag_Button))
      {
        ui_kill_action();
      }
      params->cursor[0] = txt_pt(1, edit_string.size+1);
      params->mark[0] = txt_pt(1, 1);
      focus_started = 1;
    }
  }
  else if(is_focus_active && sig.f&UI_SignalFlag_KeyboardPressed)
  {
    ui_set_auto_focus_active_key(ui_key_zero());
    sig.f |= UI_SignalFlag_Commit;
  }
  
  //////////////////////////////
  //- rjf: take navigation actions for editing
  //
  B32 changes_made = 0;
  if(!(params->flags & RD_CellFlag_DisableEdit) && (is_focus_active || focus_started))
  {
    Temp scratch = scratch_begin(0, 0);
    rd_state->text_edit_mode = 1;
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      String8 edit_string = str8(params->edit_buffer, params->edit_string_size_out[0]);
      
      // rjf: do not consume anything that doesn't fit a single-line's operations
      B32 is_autocompletion_completion = (autocomplete_hint_string.size != 0 &&
                                          evt->kind == UI_EventKind_Press &&
                                          evt->slot == UI_EventActionSlot_Accept);
      if(!is_autocompletion_completion &&
         ((evt->kind != UI_EventKind_Edit &&
           evt->kind != UI_EventKind_Navigate &&
           evt->kind != UI_EventKind_Text) ||
          evt->delta_2s32.y != 0))
      {
        continue;
      }
      
      // rjf: map this action to an op
      UI_TxtOp op = ui_single_line_txt_op_from_event(scratch.arena, evt, edit_string, params->cursor[0], params->mark[0]);
      
      // rjf: any valid *additive* op & autocomplete hint? -> perform autocomplete first, then re-compute op
      if(!(evt->flags & UI_EventFlag_Delete) && autocomplete_hint_string.size != 0)
      {
        CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
        RD_WindowState *ws = rd_window_state_from_cfg(window);
        RD_AutocompCursorInfo *autocomp_cursor_info = &ws->autocomp_cursor_info;
        String8 new_string = ui_push_string_replace_range(scratch.arena, edit_string, r1s64(autocomp_cursor_info->replaced_range.min+1, autocomp_cursor_info->replaced_range.max+1), autocomplete_hint_string);
        new_string.size = Min(params->edit_buffer_size, new_string.size);
        MemoryCopy(params->edit_buffer, new_string.str, new_string.size);
        params->edit_string_size_out[0] = new_string.size;
        params->cursor[0] = params->mark[0] = txt_pt(1, 1+autocomp_cursor_info->replaced_range.min+autocomplete_hint_string.size);
        edit_string = str8(params->edit_buffer, params->edit_string_size_out[0]);
        op = ui_single_line_txt_op_from_event(scratch.arena, evt, edit_string, params->cursor[0], params->mark[0]);
        MemoryZeroStruct(&autocomplete_hint_string);
      }
      
      // rjf: perform replace range
      if(!txt_pt_match(op.range.min, op.range.max) || op.replace.size != 0)
      {
        String8 new_string = ui_push_string_replace_range(scratch.arena, edit_string, r1s64(op.range.min.column, op.range.max.column), op.replace);
        new_string.size = Min(params->edit_buffer_size, new_string.size);
        MemoryCopy(params->edit_buffer, new_string.str, new_string.size);
        params->edit_string_size_out[0] = new_string.size;
      }
      
      // rjf: perform copy
      if(op.flags & UI_TxtOpFlag_Copy)
      {
        wm_set_clipboard_text(op.copy);
      }
      
      // rjf: commit op's changed cursor & mark to caller-provided state
      params->cursor[0] = op.cursor;
      params->mark[0] = op.mark;
      
      // rjf: consume event
      {
        if(!is_autocompletion_completion)
        {
          ui_eat_event(evt);
        }
        changes_made = 1;
      }
    }
    scratch_end(scratch);
  }
  
  //////////////////////////////
  //- rjf: click-driven "start editing"
  //
  if(edit_started)
  {
    sig.f |= UI_SignalFlag_DoubleClicked;
  }
  
  //////////////////////////////
  //- rjf: compute editable fancy strings
  //
  DR_FStrList fstrs = {0};
  {
    //- rjf: (not editing)
    if(!is_focus_active && !is_focus_active_disabled && value_name_fstrs.total_size != 0)
    {
      fstrs = value_name_fstrs;
    }
    else if(!is_focus_active && !is_focus_active_disabled && params->flags & RD_CellFlag_CodeContents && params->pre_edit_value.size != 0)
    {
      String8 display_string = params->pre_edit_value;
      fstrs = rd_fstrs_from_code_string(scratch.arena, 1, 0, ui_color_from_name(str8_lit("text")), display_string);
    }
    else if(!is_focus_active && !is_focus_active_disabled)
    {
      String8 display_string = params->pre_edit_value;
      if(params->pre_edit_value.size == 0)
      {
        display_string = ui_display_part_from_key_string(string);
      }
      UI_TagF("weak")
      {
        DR_FStrParams params = {ui_top_font(), ui_top_text_raster_flags(), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
        dr_fstrs_push_new(scratch.arena, &fstrs, &params, display_string);
      }
    }
    
    //- rjf: (editing)
    else if(is_focus_active || is_focus_active_disabled)
    {
      String8 edit_string = str8(params->edit_buffer, params->edit_string_size_out[0]);
      DR_FStrList edit_string_fstrs = {0};
      if(params->flags & RD_CellFlag_CodeContents)
      {
        edit_string_fstrs = rd_fstrs_from_code_string(scratch.arena, 1.f, 0, ui_color_from_name(str8_lit("text")), edit_string);
      }
      else
      {
        String8 edit_string = str8(params->edit_buffer, params->edit_string_size_out[0]);
        DR_FStrParams params = {ui_top_font(), ui_top_text_raster_flags(), ui_color_from_name(str8_lit("text")), ui_top_font_size()};
        dr_fstrs_push_new(scratch.arena, &edit_string_fstrs, &params, edit_string);
      }
      if(autocomplete_hint_string.size != 0)
      {
        CFG_Node *window = cfg_node_from_id(uishell_regs()->window);
        RD_WindowState *ws = rd_window_state_from_cfg(window);
        RD_AutocompCursorInfo *autocomp_cursor_info = &ws->autocomp_cursor_info;
        String8 autocomplete_append_string = str8_skip(autocomplete_hint_string, params->cursor->column-1 - autocomp_cursor_info->replaced_range.min);
        U64 off = 0;
        U64 cursor_off = params->cursor->column-1;
        DR_FStrNode *prev_n = 0;
        for(DR_FStrNode *n = edit_string_fstrs.first; n != 0; n = n->next)
        {
          if(off <= cursor_off && cursor_off <= off+n->v.string.size)
          {
            prev_n = n;
            break;
          }
          off += n->v.string.size;
        }
        {
          DR_FStrNode *autocomp_fstr_n = push_array(scratch.arena, DR_FStrNode, 1);
          DR_FStr *fstr = &autocomp_fstr_n->v;
          fstr->string = autocomplete_append_string;
          fstr->params.font = ui_top_font();
          fstr->params.raster_flags = ui_top_text_raster_flags();
          fstr->params.color = ui_color_from_name(str8_lit("text"));
          fstr->params.color.w *= 0.5f;
          fstr->params.size = ui_top_font_size();
          autocomp_fstr_n->next = prev_n ? prev_n->next : 0;
          if(prev_n != 0)
          {
            prev_n->next = autocomp_fstr_n;
          }
          if(prev_n == 0)
          {
            edit_string_fstrs.first = edit_string_fstrs.last = autocomp_fstr_n;
          }
          if(prev_n != 0 && prev_n->next == 0)
          {
            edit_string_fstrs.last = autocomp_fstr_n;
          }
          edit_string_fstrs.node_count += 1;
          edit_string_fstrs.total_size += autocomplete_hint_string.size;
          if(prev_n != 0 && cursor_off - off < prev_n->v.string.size)
          {
            String8 full_string = prev_n->v.string;
            U64 chop_amt = full_string.size - (cursor_off - off);
            prev_n->v.string = str8_chop(full_string, chop_amt);
            edit_string_fstrs.total_size -= chop_amt;
            if(chop_amt != 0)
            {
              String8 post_cursor = str8_skip(full_string, cursor_off - off);
              DR_FStrNode *post_fstr_n = push_array(scratch.arena, DR_FStrNode, 1);
              DR_FStr *post_fstr = &post_fstr_n->v;
              MemoryCopyStruct(post_fstr, &prev_n->v);
              post_fstr->string   = post_cursor;
              if(autocomp_fstr_n->next == 0)
              {
                edit_string_fstrs.last = post_fstr_n;
              }
              post_fstr_n->next = autocomp_fstr_n->next;
              autocomp_fstr_n->next = post_fstr_n;
              edit_string_fstrs.node_count += 1;
              edit_string_fstrs.total_size += post_cursor.size;
            }
          }
        }
      }
      fstrs = edit_string_fstrs;
    }
  }
  
  //////////////////////////////
  //- rjf: build scrolled contents
  //
  TxtPt mouse_pt = {0};
  F32 cursor_off = 0;
  if(scrollable_box != &ui_nil_box) UI_Parent(scrollable_box)
  {
    FuzzyMatchRangeList fuzzy_matches = {0};
    if(params->search_needle.size != 0)
    {
      fuzzy_matches = dr_fuzzy_match_find_from_fstrs(scratch.arena, &fstrs, params->search_needle);
    }
    if(ui_top_text_alignment() == UI_TextAlign_Left && (params->flags & (RD_CellFlag_Expander|RD_CellFlag_ExpanderSpace|RD_CellFlag_ExpanderPlaceholder)) == 0)
    {
      ui_spacer(ui_em(0.5f, 1.f));
    }
    if(is_focus_active)
    {
      ui_set_next_flags(UI_BoxFlag_DisableTextTrunc);
    }
    ui_set_next_pref_width(ui_text_dim(ui_top_font_size()*0.5f, 0));
    UI_Box *text_box = ui_build_box_from_stringf(UI_BoxFlag_DrawText, "###text_box");
    ui_box_equip_display_fstrs(text_box, &fstrs);
    ui_box_equip_fuzzy_match_ranges(text_box, &fuzzy_matches);
    if(is_focus_active || is_focus_active_disabled)
    {
      String8 edit_string = str8(params->edit_buffer, params->edit_string_size_out[0]);
      UI_LineEditDrawData *draw_data = push_array(ui_build_arena(), UI_LineEditDrawData, 1);
      draw_data->edited_string = push_str8_copy(ui_build_arena(), edit_string);
      draw_data->cursor = params->cursor[0];
      draw_data->mark = params->mark[0];
      draw_data->trail = do_cursor_trail && !ui_dragging(sig);
      ui_box_equip_custom_draw(text_box, ui_line_edit_draw, draw_data);
      Vec2F32 text2mouse = sub_2f32(ui_mouse(), ui_box_text_position(text_box));
      FNT_Tag font = ui_top_font();
      F32 font_size = ui_top_font_size();
      if(params->flags & RD_CellFlag_CodeContents)
      {
        font = rd_font_from_slot(RD_FontSlot_Code);
      }
      U64 mouse_pt_off = fnt_char_pos_from_tag_size_string_p(font, font_size, 0, ui_top_tab_size(), edit_string, text2mouse.x);
      mouse_pt = txt_pt(1, 1+mouse_pt_off);
      cursor_off = fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, ui_top_tab_size(), str8_prefix(edit_string, params->cursor->column-1)).x;
    }
  }
  
  //////////////////////////////
  //- rjf: click+drag
  //
  if(is_focus_active && ui_dragging(sig))
  {
    if(ui_pressed(sig))
    {
      params->mark[0] = mouse_pt;
    }
    params->cursor[0] = mouse_pt;
  }
  if(!is_focus_active && is_focus_active_disabled && ui_pressed(sig))
  {
    params->cursor[0] = params->mark[0] = mouse_pt;
  }
  
  //////////////////////////////
  //- rjf: focus cursor
  //
  if(scrollable_box != &ui_nil_box)
  {
    F32 visible_dim_px = dim_2f32(box->rect).x - expander_size_px - ui_top_font_size()*params->depth;
    if(visible_dim_px > 0)
    {
      Rng1F32 cursor_range_px  = r1f32(cursor_off-ui_top_font_size()*2.f, cursor_off+ui_top_font_size()*1.f);
      Rng1F32 visible_range_px = r1f32(scrollable_box->view_off_target.x, scrollable_box->view_off_target.x + visible_dim_px);
      cursor_range_px.min = ClampBot(0, cursor_range_px.min);
      cursor_range_px.max = ClampBot(0, cursor_range_px.max);
      F32 min_delta = cursor_range_px.min-visible_range_px.min;
      F32 max_delta = cursor_range_px.max-visible_range_px.max;
      min_delta = Min(min_delta, 0);
      max_delta = Max(max_delta, 0);
      scrollable_box->view_off_target.x += min_delta;
      scrollable_box->view_off_target.x += max_delta;
    }
    if(!is_focus_active && !is_focus_active_disabled)
    {
      scrollable_box->view_off_target.x = scrollable_box->view_off.x = 0;
    }
  }
  
  //////////////////////////////
  //- rjf: pop focus
  //
  if(is_auto_focus_hot) { ui_pop_focus_hot(); }
  if(is_auto_focus_active) { ui_pop_focus_active(); }
  
  ProfEnd();
  scratch_end(scratch);
  return sig;
}

internal UI_Signal
rd_cellf(RD_CellParams *params, char *fmt, ...)
{
  Temp scratch = scratch_begin(0, 0);
  va_list args;
  va_start(args, fmt);
  String8 string = push_str8fv(scratch.arena, fmt, args);
  va_end(args);
  UI_Signal sig = rd_cell(params, string);
  scratch_end(scratch);
  return sig;
}
