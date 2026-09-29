// Licensed under the MIT license (https://opensource.org/license/mit/)
// Adapted from RAD regressions in 0cff14e4, 7b3c7388 and 1ad634d6.
// Uses the normal initialized shell; fixture files belong to the caller's temp dir.
#include "config/tests/native_menu_policy.c"

global String8 uishell_import_fixture_dir;
#define UIImportCheck(expr) do { if(!(expr)) { *failures += 1; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); } } while(0)

internal String8
uishell_import_fixture_path(Arena *arena, String8 root, String8 relative)
{
  return push_str8f(arena, "%S/%S", root, relative);
}

internal void
uishell_check_config_directory_roundtrip(Arena *arena, U32 *failures, CFG_State *state)
{
  CFG_SchemaTable schemas = {0};
  schemas.slots_count = 16;
  schemas.slots = push_array(arena, CFG_SchemaNode *, schemas.slots_count);
  cfg_schema_table_insert(arena, &schemas, str8_lit("target"),
                         md_tree_from_string(arena, str8_lit("x: { working_directory: @folder path }"))->first);
  String8 roots[] = {str8_lit("/tmp/project/"), str8_lit("C:/project/")};
  String8 moved_roots[] = {str8_lit("/tmp/moved/"), str8_lit("C:/moved/")};
  for(U64 root_idx = 0; root_idx < ArrayCount(roots); root_idx += 1)
  {
    String8 root = roots[root_idx];
    String8 paths[] = {str8_chop(root, 1), root, str8f(arena, "%Ssub/", root), str8_zero()};
    for(U64 path_idx = 0; path_idx < ArrayCount(paths); path_idx += 1)
    {
      CFG_Node *target = cfg_node_new(state, &cfg_nil_node, str8_lit("target"));
      cfg_node_new(state, cfg_node_new(state, target, str8_lit("working_directory")), paths[path_idx]);
      for(U32 pass_idx = 0; pass_idx < 2; pass_idx += 1)
      {
        String8 saved = cfg_string_from_tree(arena, &schemas, root, target);
        CFG_NodePtrList loaded = cfg_node_ptr_list_from_string(arena, state, &schemas, root, saved);
        UIImportCheck(loaded.count == 1);
        target = loaded.first->v;
        String8 value = cfg_node_child_from_string(target, str8_lit("working_directory"))->first->string;
        if(paths[path_idx].size == 0)
        {
          UIImportCheck(value.size == 0);
        }
        else
        {
          UIImportCheck(path_match_normalized(value, paths[path_idx]));
        }
        if(path_idx == 1 && pass_idx == 0)
        {
          CFG_NodePtrList moved = cfg_node_ptr_list_from_string(arena, state, &schemas, moved_roots[root_idx], saved);
          UIImportCheck(moved.count == 1);
          String8 moved_value = cfg_node_child_from_string(moved.first->v, str8_lit("working_directory"))->first->string;
          UIImportCheck(path_match_normalized(moved_value, moved_roots[root_idx]));
        }
      }
    }
  }
}

internal void
uishell_check_folder_picker_paths(Arena *arena, U32 *failures, String8 artifacts_path)
{
#if OS_LINUX || OS_MAC
  String8 inputs[] = {str8_lit("/"), str8_lit("/u"), str8_lit(""), str8_lit("u")};
  for(U64 input_idx = 0; input_idx < ArrayCount(inputs); input_idx += 1)
  {
    String8 filter = inputs[input_idx];
    E_Eval root = e_eval_from_string(str8f(arena, "folder:\"%S\"", filter));
    E_TypeExpandRule *rule = e_expand_rule_from_type_key(root.irtree.type_key);
    E_TypeExpandInfo info = rule->info(arena, root, filter);
    UIImportCheck(info.expr_count != 0);
    E_Eval *children = push_array(arena, E_Eval, info.expr_count);
    rule->range(arena, info.user_data, root, filter, r1u64(0, info.expr_count), children);
    B32 found_usr = 0;
    for(U64 idx = 0; idx < info.expr_count; idx += 1)
    {
      E_Eval child = children[idx];
      String8 path = rd_file_path_from_eval(arena, child);
      if(str8_match(filter, str8_lit("/u"), 0) || str8_match(filter, str8_lit("u"), 0))
      {
        UIImportCheck(!str8_match(path, str8_lit("/bin"), 0));
      }
      if(str8_match(path, str8_lit("/usr"), 0))
      {
        found_usr = 1;
        UIImportCheck(e_type_key_match(child.irtree.type_key, e_type_key_folder()));
      }
    }
    UIImportCheck(found_usr);
  }
#elif OS_WINDOWS
  E_Eval roots = e_eval_from_string(str8_lit("folder:\"\""));
  UIImportCheck(e_type_key_match(roots.irtree.type_key, e_type_key_folder()));
  UIImportCheck(e_string_from_id(roots.value.u64).size == 0);
  E_TypeExpandRule *rule = e_expand_rule_from_type_key(roots.irtree.type_key);
  E_TypeExpandInfo info = rule->info(arena, roots, str8_zero());
  UIImportCheck(info.expr_count != 0);
  E_Eval *drives = push_array(arena, E_Eval, info.expr_count);
  rule->range(arena, info.user_data, roots, str8_zero(), r1u64(0, info.expr_count), drives);
  String8 current_drive = str8_prefix(artifacts_path, 2);
  B32 found_drive = 0;
  for(U64 idx = 0; idx < info.expr_count; idx += 1)
  {
    String8 path = rd_file_path_from_eval(arena, drives[idx]);
    if(str8_match(path, current_drive, StringMatchFlag_CaseInsensitive))
    {
      found_drive = 1;
      UIImportCheck(e_type_key_match(drives[idx].irtree.type_key, e_type_key_folder()));
    }
  }
  UIImportCheck(found_drive);
#endif

  // Use the test's own artifacts for file/folder navigation and filtering.
  UIImportCheck(make_directory(uishell_import_fixture_path(arena, artifacts_path, str8_lit("parent"))));
  UIImportCheck(make_directory(uishell_import_fixture_path(arena, artifacts_path, str8_lit("parent/child"))));
  UIImportCheck(write_data_to_file_path(uishell_import_fixture_path(arena, artifacts_path, str8_lit("parent/child/item.txt")), str8_lit("fixture")));
  String8 parent = uishell_import_fixture_path(arena, artifacts_path, str8_lit("parent"));
  String8 child_path = uishell_import_fixture_path(arena, artifacts_path, str8_lit("parent/child"));
  String8 item_path = uishell_import_fixture_path(arena, artifacts_path, str8_lit("parent/child/item.txt"));
  String8 suffixes[] = {str8_lit("/"), str8_lit("/ch"), str8_lit("/child"), str8_lit("/child/")};
  for(U64 idx = 0; idx < ArrayCount(suffixes); idx += 1)
  {
    String8 input = str8f(arena, "%S%S", parent, suffixes[idx]);
    E_Eval folder = e_eval_from_string(str8f(arena, "folder:\"%S\"", escaped_from_raw_str8(arena, input)));
    UIImportCheck(e_type_key_match(folder.irtree.type_key, e_type_key_folder()));
    B32 inside_child = (idx == 3);
    UIImportCheck(path_match_normalized(rd_file_path_from_eval(arena, folder), inside_child ? child_path : parent));
    E_TypeExpandRule *expand = e_expand_rule_from_type_key(folder.irtree.type_key);
    E_TypeExpandInfo expanded = expand->info(arena, folder, input);
    UIImportCheck(expanded.expr_count == 1);
    E_Eval entry = {0};
    expand->range(arena, expanded.user_data, folder, input, r1u64(0, 1), &entry);
    UIImportCheck(e_type_key_match(entry.irtree.type_key, inside_child ? e_type_key_file() : e_type_key_folder()));
    UIImportCheck(path_match_normalized(rd_file_path_from_eval(arena, entry), inside_child ? item_path : child_path));
  }
}

#if R_BACKEND == R_BACKEND_METAL
internal void
uishell_check_metal_blur_kernel_bounds(U32 *failures)
{
  F32 sizes[] = {64, 0, 1, 2, 3, 10, 32, 62, 62.49f, 62.5f, 63, 63.49f, 63.5f, 128};
  R_PassParams_Blur params = {0};
  params.rect = r2f32p(1, 2, 80, 90);
  params.corner_radii[0] = 4;
  Vec2F32 viewport = v2f32(100, 100);
  for(U64 size_idx = 0; size_idx < ArrayCount(sizes); size_idx++)
  {
    params.blur_size = sizes[size_idx];
    R_MTL_BlurUniforms uniforms = r_mtl_blur_uniforms_from_params(&params, viewport);
    UIImportCheck(uniforms.blur_count >= 1 && uniforms.blur_count <= ArrayCount(uniforms.kernel));
    UIImportCheck(MemoryMatch(&uniforms.rect, &params.rect, sizeof(params.rect)));
    UIImportCheck(uniforms.corner_radii.x == 4 && uniforms.viewport_size.x == 100);
    for(U32 idx = 0; idx < Min(uniforms.blur_count, ArrayCount(uniforms.kernel)); idx++)
    {
      UIImportCheck(uniforms.kernel[idx].x >= 0 && uniforms.kernel[idx].x <= 1);
      UIImportCheck(uniforms.kernel[idx].y >= 0 && uniforms.kernel[idx].y <= 62);
    }
    if(sizes[size_idx] >= 63)
    {
      params.blur_size = 63;
      R_MTL_BlurUniforms capped = r_mtl_blur_uniforms_from_params(&params, viewport);
      UIImportCheck(MemoryMatch(&uniforms, &capped, sizeof(capped)));
    }
  }
}
#endif

internal B32
uishell_shared_ui_diagnostics(RD_WindowState *ws)
{
  (void)ws;
  Temp scratch = scratch_begin(0, 0);
  String8 parent = uishell_import_fixture_path(scratch.arena, uishell_import_fixture_dir, str8_lit("parent"));
  if(!folder_path_exists(uishell_import_fixture_dir) || folder_path_exists(parent) || file_path_exists(parent))
  {
    fprintf(stderr, "shared UI fixture directory must exist and have no parent entry\n");
    scratch_end(scratch);
    return 0;
  }
  U32 failures = !cfg_native_menu_diagnostics();
  CFG_State *state = cfg_state_alloc();
  uishell_check_config_directory_roundtrip(scratch.arena, &failures, state);
  cfg_state_release(state);
  fprintf(stderr, "config directory roundtrip: %u failures\n", failures);

  E_BaseCtx *old_base_ctx = e_base_ctx;
  E_IRCtx *old_ir_ctx = e_ir_ctx;
  E_InterpretCtx *old_interpret_ctx = e_interpret_ctx;
  E_Cache *old_cache = e_cache;
  E_Cache *cache = e_cache_alloc();
  E_BaseCtx base_ctx = {0};
  E_IRCtx ir_ctx = {0};
  E_InterpretCtx interpret_ctx = {0};
  e_select_cache(cache);
  e_select_base_ctx(&base_ctx);
  e_select_ir_ctx(&ir_ctx);
  e_select_interpret_ctx(&interpret_ctx);
  uishell_check_folder_picker_paths(scratch.arena, &failures, uishell_import_fixture_dir);
  e_base_ctx = old_base_ctx;
  e_ir_ctx = old_ir_ctx;
  e_interpret_ctx = old_interpret_ctx;
  e_select_cache(old_cache);
  e_cache_release(cache);
  fprintf(stderr, "config and folder paths: %u failures\n", failures);
#if R_BACKEND == R_BACKEND_METAL
  uishell_check_metal_blur_kernel_bounds(&failures);
#endif
  fprintf(stderr, "shared UI diagnostics: %s (%u failures)\n", failures ? "FAIL" : "PASS", failures);
  scratch_end(scratch);
  return failures == 0;
}
#undef UIImportCheck
