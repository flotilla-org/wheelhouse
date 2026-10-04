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

// Check raster coverage through every platform provider, including descenders.
internal void
uishell_check_raster_baselines(Arena *arena, U32 *failures)
{
  // Diagnostics may run from a temporary directory, as the Windows runner does.
  String8 *fonts[] = {&rd_default_main_font_bytes, &rd_default_code_font_bytes};
  String8 glyphs[] = {str8_lit("H"), str8_lit("g")};
  for EachElement(font_idx, fonts)
  {
    FP_Handle handle = fp_font_open_from_static_data_string(fonts[font_idx]);
    FNT_Tag font = fnt_tag_from_static_data_string(fonts[font_idx]);
    for(U32 scale = 1; scale <= 2; scale++)
    for(U32 size = 17; size <= 24; size += 7)
    for EachElement(glyph_idx, glyphs)
    {
      F32 ink_top[2] = {0}, ink_bottom[2] = {0};
      for(U32 tight = 0; tight < 2; tight++)
      {
        Temp temp = temp_begin(arena);
        FP_RasterFlags fp_flags = FP_RasterFlag_Smooth | (tight ? FP_RasterFlag_TightBounds : 0);
        FNT_RasterFlags flags = FNT_RasterFlag_Smooth | (tight ? FNT_RasterFlag_TightBounds : 0);
        FP_RasterResult raster = fp_raster(arena, handle, (F32)(size*scale), fp_flags, glyphs[glyph_idx]);
        FNT_Run run = fnt_run_from_string_scaled(font, (F32)size, (F32)scale, 0, 0, flags, glyphs[glyph_idx]);
        S32 top = raster.atlas_dim.y, bottom = -1;
        // All providers store four bytes per pixel, with coverage/alpha in byte 3.
        for(S32 y = 0; raster.atlas != 0 && y < raster.atlas_dim.y; y++)
        for(S32 x = 0; x < raster.atlas_dim.x; x++)
        {
          if(((U8 *)raster.atlas)[4*(y*raster.atlas_dim.x+x)+3] >= 128)
          {
            top = Min(top, y);
            bottom = Max(bottom, y);
          }
        }
        UIImportCheck(bottom >= top && run.pieces.count == 1);
        if(bottom >= top && run.pieces.count == 1)
        {
          F32 offset = run.pieces.v[0].offset.y*(F32)scale;
          ink_top[tight] = offset + (F32)top;
          // Pixel indices denote their upper edge; +1 gives the lower ink edge.
          ink_bottom[tight] = offset + (F32)bottom + 1.f;
          if(glyph_idx == 0) { UIImportCheck(abs_f32(ink_bottom[tight]) <= 0.5f); }
          else { UIImportCheck(ink_top[tight] < 0 && ink_bottom[tight] > 0); }
        }
        temp_end(temp);
      }
      UIImportCheck(abs_f32(ink_top[0] - ink_top[1]) <= 0.5f);
      UIImportCheck(abs_f32(ink_bottom[0] - ink_bottom[1]) <= 0.5f);
    }
    fp_font_close(handle);
  }
}

internal void
uishell_check_text_decorations(U32 *failures)
{
  // No glyph texture: the bucket contains just the two actual decoration rects.
  FNT_Piece piece = {.advance = 40};
  DR_FRunNode node = {0};
  node.v.run.pieces.v = &piece;
  node.v.run.pieces.count = 1;
  node.v.run.ascent = 20;
  node.v.run.descent = 8;
  node.v.color = v4f32(1, 1, 1, 1);
  node.v.underline_thickness = node.v.strikethrough_thickness = 1;
  DR_FRunList list = {.first = &node, .last = &node, .node_count = 1, .dim = {40, 28}};
  DR_Bucket *bucket = dr_bucket_make();
  DR_BucketScope(bucket)
  {
    dr_truncated_fancy_run_list(v2f32(10, 50), &list, 100, (FNT_Run){0});
  }
  U32 count = 0;
  for(R_PassNode *pass = bucket->passes.first; pass != 0; pass = pass->next)
  {
    if(pass->v.kind != R_PassKind_UI) { continue; }
    for(R_BatchGroup2DNode *group = pass->v.params_ui->rects.first; group != 0; group = group->next)
    for(R_BatchNode *batch = group->batches.first; batch != 0; batch = batch->next)
    {
      R_Rect2DInst *rects = (R_Rect2DInst *)batch->v.v;
      U64 n = batch->v.byte_count/group->batches.bytes_per_inst;
      for(U64 idx = 0; idx < n; idx++, count++)
      {
        Rng2F32 rect = rects[idx].dst;
        UIImportCheck(rect.x0 == 10 && rect.x1 == 50 && rect.y1-rect.y0 == 1);
        // Underline just below the baseline; strike halfway up the ascent box.
        UIImportCheck(rect.y0 == (count == 0 ? 51.f : 40.f));
      }
    }
  }
  UIImportCheck(count == 2);
}

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
  failures += !uishell_edit_menu_ui_diagnostics(ws);
#if OS_LINUX
  failures += !uishell_edit_x11_text_diagnostics(ws);
#endif
#if OS_MAC
  failures += !uishell_edit_command_diagnostics(1);
#endif
  uishell_check_raster_baselines(scratch.arena, &failures);
  uishell_check_text_decorations(&failures);
  fprintf(stderr, "raster baselines and text decorations: %u failures\n", failures);
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
