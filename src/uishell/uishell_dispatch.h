// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef UISHELL_DISPATCH_H
#define UISHELL_DISPATCH_H

internal void
uishell_write_config_data(Arena *arena, String8 dst_path, String8 bucket_name)
{
  if(dst_path.size != 0)
  {
    B32 dst_exists = (properties_from_file_path(dst_path).created != 0);
    String8 temp_path = push_str8f(arena, "%S.temp", dst_path);
    String8 overwritten_path = push_str8f(arena, "%S.old", dst_path);
    CFG_Node *tree_root = cfg_node_child_from_string(cfg_node_root(), bucket_name);
    String8List strings = {0};
    str8_list_pushf(arena, &strings, "%s%s %S file\n\n", RD_APP_CONFIG_MAGIC, BUILD_VERSION_STRING_LITERAL, bucket_name);
    for(CFG_Node *child = tree_root->first; child != &cfg_nil_node; child = child->next)
    {
      str8_list_push(arena, &strings, cfg_string_from_tree(arena, rd_state->cfg_schema_table, str8_chop_last_slash(dst_path), child));
    }
    String8 data = str8_list_join(arena, &strings, 0);
    B32 temp_write_good = write_data_to_file_path(temp_path, data);
    B32 old_del_good    = (temp_write_good && delete_file_at_path(overwritten_path));
    B32 old_move_good   = (temp_write_good && (!dst_exists || move_file_path(overwritten_path, dst_path)));
    B32 new_move_good   = (old_move_good && move_file_path(dst_path, temp_path));
    if(new_move_good && dst_exists)
    {
      delete_file_at_path(overwritten_path);
    }
    else if(!new_move_good && old_move_good && dst_exists)
    {
      move_file_path(dst_path, overwritten_path);
    }
  }
}

internal B32
uishell_dispatch_config_command(String8 name)
{
  B32 result = 1;
  
  if(str8_match(name, str8_lit("user_settings"), 0) ||
     str8_match(name, str8_lit("project_settings"), 0))
  {
    String8 expr = str8_match(name, str8_lit("user_settings"), 0) ? str8_lit("query:user_settings") : str8_lit("query:project_settings");
    UIShell_RegsScope(.expr = expr, .do_implicit_root = 1, .do_big_rows = 1, .do_lister = 1)
    {
      uishell_push_cmd_current(str8_lit("push_query"));
    }
  }
  else if(str8_match(name, str8_lit("open_recent_project"), 0))
  {
    CFG_Node *cfg = cfg_node_from_id(uishell_regs()->cfg);
    CFG_Node *path = cfg_node_child_from_string(cfg, str8_lit("path"));
    if(str8_match(cfg->string, str8_lit("recent_project"), 0) &&
       path->first->string.size != 0)
    {
      UIShell_RegsScope(.file_path = path->first->string)
      {
        uishell_push_cmd_current(str8_lit("open_project"));
      }
    }
  }
  else if(str8_match(name, str8_lit("open_user"), 0) ||
          str8_match(name, str8_lit("open_project"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    B32 is_user = str8_match(name, str8_lit("open_user"), 0);
    B32 is_project = str8_match(name, str8_lit("open_project"), 0);
    String8 file_root_key = is_user ? str8_lit("user") : is_project ? str8_lit("project") : str8_lit("other");
    CFG_Node *file_root = cfg_node_child_from_string(cfg_node_root(), file_root_key);
    
    String8 file_path = uishell_regs()->file_path;
    String8 file_data = data_from_file_path(scratch.arena, file_path);
    FileProperties file_props = properties_from_file_path(file_path);
    
    B32 file_is_okay = 0;
    {
      String8 stored_path = is_user ? rd_state->user_path : rd_state->project_path;
      String8 app_cfg_magic = str8_lit(RD_APP_CONFIG_MAGIC);
      file_is_okay = ((file_props.size == 0 && file_props.created == 0) ||
                      str8_match(file_path, stored_path, 0) ||
                      stored_path.size == 0 ||
                      str8_match(str8_prefix(file_data, app_cfg_magic.size), app_cfg_magic, 0));
    }
    
    String8 file_version = {0};
    if(file_is_okay && file_props.size != 0)
    {
      file_version = str8_skip(file_data, 10);
      U64 line_end = str8_find_needle(file_version, 0, str8_lit("\n"), 0);
      file_version = str8_prefix(file_version, line_end);
      U64 first_space = str8_find_needle(file_version, 0, str8_lit(" "), 0);
      file_version = str8_prefix(file_version, first_space);
      file_version = str8_skip_chop_whitespace(file_version);
    }
    
    if(!file_is_okay)
    {
      log_user_errorf("\"%S\" appears to refer to an existing file which is not a uishell config file. This would overwrite the file.", file_path);
    }
    
    if(file_is_okay)
    {
      cfg_node_release_all_children(rd_state->cfg, file_root);
    }
    
    CFG_NodePtrList file_cfg_list = {0};
    if(file_is_okay)
    {
      file_cfg_list = cfg_node_ptr_list_from_string(scratch.arena, rd_state->cfg, rd_state->cfg_schema_table, str8_chop_last_slash(file_path), file_data);
    }
    
    if(file_is_okay)
    {
      if(is_user)
      {
        arena_clear(rd_state->user_path_arena);
        rd_state->user_path = str8_copy(rd_state->user_path_arena, file_path);
      }
      else if(is_project)
      {
        arena_clear(rd_state->project_path_arena);
        rd_state->project_path = str8_copy(rd_state->project_path_arena, file_path);
      }
    }
    
    if(file_is_okay)
    {
      for(CFG_NodePtrNode *n = file_cfg_list.first; n != 0; n = n->next)
      {
        cfg_node_insert_child(rd_state->cfg, file_root, file_root->last, n->v);
      }
    }
    
    if(file_is_okay && is_user)
    {
      CFG_NodePtrList all_user_windows = cfg_node_child_list_from_string(scratch.arena, file_root, str8_lit("window"));
      if(all_user_windows.count == 0)
      {
        WM_Monitor monitor   = wm_primary_monitor();
        Vec2F32 monitor_dim  = wm_dim_from_monitor(monitor);
        F32 monitor_dpi      = wm_dpi_from_monitor(monitor);
        Vec2F32 window_dim   = v2f32(monitor_dim.x*4/5, monitor_dim.y*4/5);
        if(window_dim.x == 0 || window_dim.y == 0)
        {
          window_dim = v2f32(1280, 720);
        }
        CFG_Node *new_window = cfg_node_new(rd_state->cfg, file_root, str8_lit("window"));
        CFG_Node *size = cfg_node_new(rd_state->cfg, new_window, str8_lit("size"));
        cfg_node_newf(rd_state->cfg, size, "%f", window_dim.x);
        cfg_node_newf(rd_state->cfg, size, "%f", window_dim.y);
        F32 line_height_guess = 11.f * (monitor_dpi / 96.f);
        F32 num_lines_in_monitor_height = monitor_dim.y / line_height_guess;
        String8 reset_cmd = num_lines_in_monitor_height < 100 ? str8_lit("reset_to_compact_panels") : str8_lit("reset_to_default_panels");
        UIShell_RegsScope(.window = new_window->id)
        {
          uishell_push_cmd_current(reset_cmd);
        }
      }
    }
    
    if(file_is_okay && is_user)
    {
      CFG_NodePtrList all_keybindings = cfg_node_child_list_from_string(scratch.arena, file_root, str8_lit("keybindings"));
      if(all_keybindings.count == 0)
      {
        uishell_push_cmd_current(str8_lit("reset_to_default_bindings"));
      }
    }
    
    if(file_is_okay && is_user && !uishell_regs()->non_graphical)
    {
      uishell_push_cmd_current(str8_lit("record_user_as_last_opened"));
    }
    
    if(file_is_okay && is_project)
    {
      uishell_push_cmd_current(str8_lit("record_project_in_user"));
    }
    
    if(file_is_okay && is_project)
    {
      CFG_NodePtrList windows = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("window"));
      for(CFG_NodePtrNode *n = windows.first; n != 0; n = n->next)
      {
        UIShell_ControlledSplit root_controlled_split = uishell_root_controlled_split_from_window(scratch.arena, n->v);
        UIShell_WorkspaceMount *workspace_mount = uishell_controlled_split_selected_mount(&root_controlled_split);
        CFG_PanelTree panels = workspace_mount->panel_tree;
        for(CFG_PanelNode *panel = panels.root; panel != &cfg_nil_panel_node; panel = cfg_panel_node_rec__depth_first_pre(panels.root, panel).next)
        {
          if(rd_cfg_is_project_filtered(panel->selected_tab))
          {
            CFG_Node *fallback_tab = &cfg_nil_node;
            for(CFG_NodePtrNode *tab_n = panel->tabs.first; tab_n != 0; tab_n = tab_n->next)
            {
              CFG_Node *tab = tab_n->v;
              if(!rd_cfg_is_project_filtered(tab))
              {
                fallback_tab = tab;
                break;
              }
            }
            UIShell_RegsScope(.panel = panel->cfg->id, .tab = fallback_tab->id)
            {
              uishell_push_cmd_current(str8_lit("focus_tab"));
            }
          }
        }
      }
    }
    
    if(is_project)
    {
      String8 new_current_dir = str8_chop_last_slash(uishell_regs()->file_path);
      if(new_current_dir.size != 0)
      {
        UIShell_RegsScope(.file_path = new_current_dir)
        {
          uishell_push_cmd_current(str8_lit("set_current_path"));
        }
      }
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("new_user"), 0))
  {
    UIShell_RegsScope(.file_path = str8_zero())
    {
      uishell_push_cmd_current(str8_lit("open_user"));
    }
  }
  else if(str8_match(name, str8_lit("new_project"), 0))
  {
    UIShell_RegsScope(.file_path = str8_zero())
    {
      uishell_push_cmd_current(str8_lit("open_project"));
    }
  }
  else if(str8_match(name, str8_lit("save_user"), 0) ||
          str8_match(name, str8_lit("save_project"), 0))
  {
    B32 is_user = str8_match(name, str8_lit("save_user"), 0);
    String8 new_path = uishell_regs()->file_path;
    B32 file_will_be_overwritten = (properties_from_file_path(new_path).created != 0);
    UI_Key key = ui_key_from_string(ui_key_zero(), str8_lit("save_config_overwrite_confirm"));
    if(file_will_be_overwritten && !uishell_regs()->force_confirm && !ui_key_match(rd_state->popup_key, key))
    {
      rd_state->popup_key = key;
      rd_state->popup_active = 1;
      arena_clear(rd_state->popup_arena);
      MemoryZeroStruct(&rd_state->popup_cmds);
      rd_state->popup_title = push_str8f(rd_state->popup_arena, "Are you sure you want to save to this path?");
      rd_state->popup_desc = push_str8f(rd_state->popup_arena, "The existing file at '%S' will be overwritten.", new_path);
      UIShell_Regs regs = uishell_regs_copy(rd_state->popup_arena, uishell_regs());
      regs.force_confirm = 1;
      uishell_cmd_list_push_new(rd_state->popup_arena, &rd_state->popup_cmds, name, &regs);
    }
    else if(is_user)
    {
      arena_clear(rd_state->user_path_arena);
      rd_state->user_path = push_str8_copy(rd_state->user_path_arena, new_path);
      uishell_push_cmd_current(str8_lit("write_user_data"));
      uishell_push_cmd_current(str8_lit("record_user_as_last_opened"));
    }
    else
    {
      arena_clear(rd_state->project_path_arena);
      rd_state->project_path = push_str8_copy(rd_state->project_path_arena, new_path);
      uishell_push_cmd_current(str8_lit("write_project_data"));
      uishell_push_cmd_current(str8_lit("record_project_in_user"));
    }
  }
  else if(str8_match(name, str8_lit("record_project_in_user"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    if(uishell_regs()->file_path.size != 0)
    {
      String8 file_path = uishell_regs()->file_path;
      CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
      CFG_NodePtrList recent_projects = cfg_node_child_list_from_string(scratch.arena, user, str8_lit("recent_project"));
      CFG_Node *recent_project = &cfg_nil_node;
      for(CFG_NodePtrNode *n = recent_projects.first; n != 0; n = n->next)
      {
        if(path_match_normalized(rd_path_from_cfg(n->v), file_path))
        {
          recent_project = n->v;
          break;
        }
      }
      if(recent_project == &cfg_nil_node)
      {
        recent_project = cfg_node_new(rd_state->cfg, user, str8_lit("recent_project"));
        CFG_Node *path_root = cfg_node_new(rd_state->cfg, recent_project, str8_lit("path"));
        cfg_node_new(rd_state->cfg, path_root, file_path);
      }
      {
        CFG_Node *root = cfg_node_root();
        CFG_Node *project = cfg_node_child_from_string(root, s("project"));
        CFG_Node *project_name = cfg_node_child_from_string(project, s("name"));
        CFG_Node *recent_project_name_root = cfg_node_child_from_string_or_alloc(rd_state->cfg, recent_project, s("name"));
        cfg_node_new_replace(rd_state->cfg, recent_project_name_root, project_name->first->string);
      }
      cfg_node_unhook(rd_state->cfg, user, recent_project);
      cfg_node_insert_child(rd_state->cfg, user, &cfg_nil_node, recent_project);
      recent_projects = cfg_node_child_list_from_string(scratch.arena, user, str8_lit("recent_project"));
      if(recent_projects.count > 32)
      {
        cfg_node_release(rd_state->cfg, recent_projects.last->v);
      }
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("record_user_as_last_opened"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    String8 file_path = uishell_regs()->file_path;
    String8 last_user_path = str8f(scratch.arena, "%S/%s", rd_app_data_folder(scratch.arena), RD_APP_LAST_USER_FILE_NAME);
    write_data_to_file_path(last_user_path, file_path);
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("write_user_data"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    uishell_write_config_data(scratch.arena, rd_state->user_path, str8_lit("user"));
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("write_project_data"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    uishell_write_config_data(scratch.arena, rd_state->project_path, str8_lit("project"));
    scratch_end(scratch);
  }
  else
  {
    result = 0;
  }
  
  return result;
}

internal B32
uishell_dispatch_file_command(String8 name)
{
  B32 result = 1;
  
  if(str8_match(name, str8_lit("set_current_path"), 0))
  {
    CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
    CFG_Node *current_path = cfg_node_child_from_string_or_alloc(rd_state->cfg, user, str8_lit("current_path"));
    cfg_node_new_replace(rd_state->cfg, current_path, uishell_regs()->file_path);
  }
  else if(str8_match(name, str8_lit("open"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    String8 path = path_absolute_dst_from_relative_dst_src(scratch.arena, uishell_regs()->file_path, get_current_path(scratch.arena));
    FileProperties props = properties_from_file_path(path);
    if(props.created != 0)
    {
      String8 expr = rd_eval_string_from_file_path(scratch.arena, path);
      UIShell_RegsScope(.string = str8_lit("pending"), .expr = expr)
      {
        uishell_push_cmd_current(str8_lit("build_tab"));
      }
    }
    else
    {
      log_user_errorf("Couldn't open file at \"%S\".", path);
    }
    scratch_end(scratch);
  }
  else if(str8_match(name, str8_lit("show_file_in_explorer"), 0))
  {
    if(uishell_regs()->file_path.size != 0)
    {
      wm_show_in_filesystem_ui(uishell_regs()->file_path);
    }
  }
  else
  {
    result = 0;
  }
  
  return result;
}

internal B32
uishell_dispatch_viewer_command(String8 name)
{
  B32 result = 1;

  if(str8_match(name, str8_lit("output"), 0))
  {
    UIShell_RegsScope(.string = str8_lit("text"), .expr = str8_lit("query:output"))
    {
      uishell_push_cmd_current(str8_lit("build_tab"));
    }
  }
  else if(str8_match(name, str8_lit("text"), 0))
  {
    UIShell_RegsScope(.string = str8_lit("text"), .expr = str8_zero())
    {
      uishell_push_cmd_current(str8_lit("build_tab"));
    }
  }
  else if(str8_match(name, str8_lit("binary"), 0))
  {
    UIShell_RegsScope(.string = str8_lit("binary"), .expr = str8_zero())
    {
      uishell_push_cmd_current(str8_lit("build_tab"));
    }
  }
  else if(str8_match(name, str8_lit("terminal"), 0))
  {
    UIShell_RegsScope(.string = str8_lit("terminal"), .expr = str8_zero())
    {
      uishell_push_cmd_current(str8_lit("build_tab"));
    }
  }
  else if(str8_match(name, str8_lit("scroll_region_fixture"), 0))
  {
    UIShell_RegsScope(.string = str8_lit("scroll_region_fixture"), .expr = str8_zero())
    {
      uishell_push_cmd_current(str8_lit("build_tab"));
    }
  }
  else if(str8_match(name, str8_lit("terminal_fixture"), 0))
  {
    UIShell_RegsScope(.string = str8_lit("terminal_fixture"), .expr = str8_zero())
    {
      uishell_push_cmd_current(str8_lit("build_tab"));
    }
  }
  else
  {
    result = 0;
  }

  return result;
}

internal void
uishell_register_app_cmd_packs(void)
{
  local_persist UIShell_CmdPack app_file_pack =
  {
    .name = str8_lit_comp("app_file"),
    .cmd_count = uishell_app_file_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_app_file_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_app_file_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_app_file_cmd_pack_binding_count,
    .binding_from_index = uishell_app_file_cmd_pack_binding_from_index,
    .menu_specs = uishell_app_file_menu_specs,
    .dispatch = uishell_dispatch_file_command,
  };
  local_persist UIShell_CmdPack app_config_pack =
  {
    .name = str8_lit_comp("app_config"),
    .cmd_count = uishell_app_config_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_app_config_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_app_config_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_app_config_cmd_pack_binding_count,
    .binding_from_index = uishell_app_config_cmd_pack_binding_from_index,
    .dispatch = uishell_dispatch_config_command,
  };
  local_persist UIShell_CmdPack app_viewer_pack =
  {
    .name = str8_lit_comp("app_viewer"),
    .cmd_count = uishell_app_viewer_cmd_pack_cmd_count,
    .cmd_info_from_index = uishell_app_viewer_cmd_pack_cmd_info_from_index,
    .cmd_info_from_string = uishell_app_viewer_cmd_pack_cmd_info_from_string,
    .binding_count = uishell_app_viewer_cmd_pack_binding_count,
    .binding_from_index = uishell_app_viewer_cmd_pack_binding_from_index,
    .dispatch = uishell_dispatch_viewer_command,
  };

  uishell_register_cmd_pack(&app_file_pack);
  uishell_register_cmd_pack(&app_config_pack);
  uishell_register_cmd_pack(&app_viewer_pack);
}

#define UISHELL_APP_SAVE_BEFORE_EXIT() do \
{ \
  uishell_push_cmd_current(str8_lit("write_user_data")); \
  uishell_push_cmd_current(str8_lit("write_project_data")); \
} while(0)

#define UISHELL_APP_AUTOSAVE() do \
{ \
  uishell_cmd("write_user_data"); \
  uishell_cmd("write_project_data"); \
} while(0)

#define UISHELL_APP_INITIAL_LOAD(user_path, project_path) do \
{ \
  uishell_cmd("open_user", .file_path = (user_path), .non_graphical = 1); \
  if((project_path).size != 0) \
  { \
    uishell_cmd("open_project", .file_path = (project_path)); \
  } \
} while(0)

#define UISHELL_APP_OPEN_USER_COMMAND_NAME() str8_lit("open_user")
#define UISHELL_APP_OPEN_PROJECT_COMMAND_NAME() str8_lit("open_project")
#define UISHELL_APP_REGISTER_CMD_PACKS() uishell_register_app_cmd_packs()

#endif // UISHELL_DISPATCH_H
