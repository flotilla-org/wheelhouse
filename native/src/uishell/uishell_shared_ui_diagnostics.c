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

// The blur kernel every backend uploads (r_core_blur_uniforms) stays inside
// its 32 slots and copies the pass parameters through, at any blur size.
internal void
uishell_check_blur_kernel_bounds(U32 *failures)
{
  F32 sizes[] = {64, 0, 1, 2, 3, 10, 32, 62, 62.49f, 62.5f, 63, 63.49f, 63.5f, 128};
  R_PassParams_Blur params = {0};
  params.rect = r2f32p(1, 2, 80, 90);
  params.corner_radii[0] = 4;
  Vec2F32 viewport = v2f32(100, 100);
  for(U64 size_idx = 0; size_idx < ArrayCount(sizes); size_idx++)
  {
    params.blur_size = sizes[size_idx];
    R_CoreBlurUniforms uniforms = r_core_blur_uniforms(&params, viewport, params.blur_size, 0);
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
      R_CoreBlurUniforms capped = r_core_blur_uniforms(&params, viewport, params.blur_size, 0);
      UIImportCheck(MemoryMatch(&uniforms, &capped, sizeof(capped)));
    }
  }
}

// Blur a field of one-pixel stripes through a rounded rect and read it back,
// on whichever backend this build has. Where the blur applies, neighbouring
// pixels converge; where it doesn't, they keep the stripes' full contrast.
// Catches a corner mask that keeps the wrong side of the rounded rect (GL
// discarded the inside), sampling that doesn't average (GL sampled nearest),
// and a clip that leaves nothing on the target blurring the rect anyway.
internal S32
uishell_blur_contrast(R_Readback *pixels, S32 x, S32 y)
{
  U8 *a = pixels->data.str + ((U64)y*pixels->size.x + x)*4;
  U8 *b = a + 4;
  S32 result = (S32)a[1] - (S32)b[1];
  return result < 0 ? -result : result;
}

internal void
uishell_check_blur_mask(U32 *failures)
{
  Temp scratch = scratch_begin(0, 0);
  Vec2S32 size = v2s32(96, 64);
  Rng2F32 blur_rect = r2f32p(24, 16, 72, 48);
  Rng2F32 clips[] =
  {
    {0},                         // unclipped
    r2f32p(200, 200, 300, 300),  // entirely off the target: blurs nothing
  };
  for EachElement(clip_idx, clips)
  {
    DR_Bucket *bucket = dr_bucket_make();
    DR_BucketScope(bucket)
    {
      for(S32 x = 0; x < size.x; x += 1)
      {
        F32 v = (F32)(x & 1);
        dr_rect(r2f32p((F32)x, 0, (F32)x + 1, (F32)size.y), v4f32(v, v, v, 1), 0, 0, 0);
      }
      DR_ClipScope(clips[clip_idx])
      {
        dr_blur(blur_rect, 8, 8);
      }
    }
    R_Readback pixels = r_pass_list_readback(scratch.arena, size, &bucket->passes);
    UIImportCheck(pixels.format == R_Tex2DFormat_BGRA8 && pixels.size.x == size.x && pixels.size.y == size.y &&
                  pixels.data.size >= (U64)size.x*size.y*4);
    if(pixels.format != R_Tex2DFormat_BGRA8 || pixels.data.size < (U64)size.x*size.y*4)
    {
      continue;
    }
    S32 outside = uishell_blur_contrast(&pixels, 8, 32);
    S32 center = uishell_blur_contrast(&pixels, 48, 32);
    S32 corner = uishell_blur_contrast(&pixels, 25, 17);  // past the rounded corner
    fprintf(stderr, "%.*s blur clip %d: stripe contrast outside %d, center %d, corner %d\n",
            str8_varg(r_backend_name()), (int)clip_idx, outside, center, corner);
    UIImportCheck(outside > 200);
    UIImportCheck(corner > 200);
    if(clip_idx == 0)
    {
      UIImportCheck(center < 64);
    }
    else
    {
      UIImportCheck(center > 200);
    }
  }
  scratch_end(scratch);
}

// What a rounded rect means, read back from this backend. The hand-written
// rect shaders differ between backends; these are the cases a single-source
// shader has to settle (ADR 0015 §5). Each case prints what it measured,
// pass or fail, so runs on the three backends can be compared line by line.
internal F32
uishell_rect_semantics_linear(R_Readback *pixels, S32 x, S32 y, U32 channel)
{
  // BGRA bytes. OpenGL reads back its linear stage and D3D11 copies its
  // linear stage into a UNORM target; only Metal's final target is sRGB.
  U8 *p = pixels->data.str + ((U64)y*pixels->size.x + x)*4;
  U8 bgra_index[] = {2, 1, 0, 3}; // channel 0..3 = r, g, b, a
  F32 v = (F32)p[bgra_index[channel]]/255.f;
#if R_BACKEND == R_BACKEND_METAL
  if(channel < 3)
  {
    v = (v <= 0.04045f) ? v/12.92f : pow_f32((v + 0.055f)/1.055f, 2.4f);
  }
#endif
  return v;
}

// A Geo3D pass through readback: one textured quad, drawn into a viewport
// that covers only the right two thirds of the target, then composited.
// Probes catch a missing instance transform, a misplaced or flipped
// composite, an ignored albedo remap and a missing albedo:
//
//   target 96x64; viewport x 32..96. The mesh quad spans clip x -0.5..0.5,
//   y 0..1 (the top half); the instance moves it right by 0.5, to x 0..1,
//   which is target x 64..96, y 0..32. The albedo is 2x1, red then green,
//   remapped to its left half, so the whole quad is red.
internal void
uishell_check_geo3d(U32 *failures)
{
  Temp scratch = scratch_begin(0, 0);
  Vec2S32 size = v2s32(96, 64);
  F32 quad[] =
  {
    // position          normal     uv     rgb
    -0.5f, 1.f, 0.f,     0, 0, 1,   0, 0,  1, 1, 1,
     0.5f, 1.f, 0.f,     0, 0, 1,   1, 0,  1, 1, 1,
     0.5f, 0.f, 0.f,     0, 0, 1,   1, 1,  1, 1, 1,
    -0.5f, 0.f, 0.f,     0, 0, 1,   0, 1,  1, 1, 1,
  };
  U32 indices[] = {0, 1, 2, 0, 2, 3};
  U32 albedo_pixels[] = {0xff0000ffu, 0xff00ff00u}; // RGBA8: red, green
  R_Handle vertices = r_buffer_alloc(R_ResourceKind_Static, sizeof(quad), quad);
  R_Handle index_buffer = r_buffer_alloc(R_ResourceKind_Static, sizeof(indices), indices);
  R_Handle albedo = r_tex2d_alloc(R_ResourceKind_Static, v2s32(2, 1), R_Tex2DFormat_RGBA8, albedo_pixels);
  DR_Bucket *bucket = dr_bucket_make();
  DR_BucketScope(bucket)
  {
    dr_rect(r2f32p(0, 0, (F32)size.x, (F32)size.y), v4f32(0, 0, 0, 1), 0, 0, 0);
    dr_geo3d_begin(r2f32p(32, 0, 96, 64), mat_4x4f32(1.f), mat_4x4f32(1.f));
    DR_Tex2DSampleKindScope(R_Tex2DSampleKind_Nearest)
    {
      R_Mesh3DInst *inst = dr_mesh(vertices, index_buffer, R_GeoTopologyKind_Triangles, R_GeoVertexFlag_TexCoord|R_GeoVertexFlag_RGB,
                                   albedo, 0, make_translate_4x4f32(v3f32(0.5f, 0, 0)));
      inst->albedo_tex_remap = v4f32(0, 0, 0.5f, 1);
    }
  }
  R_Readback pixels = r_pass_list_readback(scratch.arena, size, &bucket->passes);
  B32 ok = (pixels.format == R_Tex2DFormat_BGRA8 && pixels.size.x == size.x && pixels.size.y == size.y &&
            pixels.data.size >= (U64)size.x*size.y*4);
  UIImportCheck(ok);
  if(ok)
  {
    String8 backend = r_backend_name();
#define Geo3DCase(label, x, y, want_r, want_g) do { \
F32 _r = uishell_rect_semantics_linear(&pixels, (x), (y), 0), _g = uishell_rect_semantics_linear(&pixels, (x), (y), 1); \
B32 _pass = ((want_r) ? _r > 0.8f : _r < 0.1f) && ((want_g) ? _g > 0.8f : _g < 0.1f); if(!_pass) { *failures += 1; } \
fprintf(stderr, "%.*s geo3d %s: %s (rg %.2f %.2f)\n", str8_varg(backend), (label), _pass ? "ok" : "DIFFERS", _r, _g); } while(0)
    Geo3DCase("textured quad is drawn", 72, 16, 1, 0);
    Geo3DCase("albedo remap keeps to the red texel", 92, 16, 1, 0);
    Geo3DCase("quad is the right way up", 72, 48, 0, 0);
    Geo3DCase("instance transform moves the quad", 52, 16, 0, 0);
    Geo3DCase("nothing outside the viewport", 16, 16, 0, 0);
#undef Geo3DCase
  }
  r_tex2d_release(albedo);
  r_buffer_release(index_buffer);
  r_buffer_release(vertices);
  scratch_end(scratch);
}

internal void
uishell_check_rect_semantics(U32 *failures)
{
  Temp scratch = scratch_begin(0, 0);
  Vec2S32 size = v2s32(192, 64);
  DR_Bucket *bucket = dr_bucket_make();
  DR_BucketScope(bucket)
  {
    dr_rect(r2f32p(0, 0, (F32)size.x, (F32)size.y), v4f32(0, 0, 0, 1), 0, 0, 0);

    // A: per-corner radius and colour. Only the top-left corner (Corner_00)
    // is rounded; each corner has its own colour.
    R_Rect2DInst *a = dr_rect(r2f32p(8, 8, 56, 56), v4f32(1, 1, 1, 1), 0, 0, 0);
    a->corner_radii[Corner_00] = 16;
    a->colors[Corner_00] = v4f32(1, 0, 0, 1);
    a->colors[Corner_01] = v4f32(0, 1, 0, 1); // x0, y1: bottom left
    a->colors[Corner_10] = v4f32(0, 0, 1, 1); // x1, y0: top right
    a->colors[Corner_11] = v4f32(1, 1, 1, 1);

    // B: a radius with zero softness; smoothstep(0, 0, x) is undefined in
    // GLSL and HLSL.
    dr_rect(r2f32p(64, 8, 88, 32), v4f32(1, 1, 1, 1), 8, 0, 0);

    // C: a transform that doubles size doubles the radius and softness too,
    // so zoomed content looks the same, only larger. (66,2)-(86,22) lands at
    // (132,4)-(172,44) with a radius of 16 and softness 2 on screen. The
    // probe 5.5 px in from the corner along the diagonal is cut by that, but
    // filled by an unscaled radius of 8 and softness of 1. (The softness inset
    // matters: a probe nearer the corner is cut either way.)
    DR_XForm2DScope(make_scale_3x3f32(v2f32(2, 2)))
    {
      dr_rect(r2f32p(66, 2, 86, 22), v4f32(1, 1, 1, 1), 8, 0, 1);
    }

    // D: group transparency 0.5 halves coverage once, not colour and
    // coverage both.
    DR_TransparencyScope(0.5f)
    {
      dr_rect(r2f32p(92, 8, 120, 32), v4f32(1, 1, 1, 1), 0, 0, 0);
    }

    // E: a 2px border with no fill shows the border and leaves the inside.
    dr_rect(r2f32p(64, 36, 88, 60), v4f32(1, 1, 1, 1), 4, 2, 1);
  }
  R_Readback pixels = r_pass_list_readback(scratch.arena, size, &bucket->passes);
  B32 ok = (pixels.format == R_Tex2DFormat_BGRA8 && pixels.size.x == size.x && pixels.size.y == size.y &&
            pixels.data.size >= (U64)size.x*size.y*4);
  UIImportCheck(ok);
  if(ok)
  {
    String8 backend = r_backend_name();
#define RectCase(label, expr, fmt, ...) do { B32 _pass = (expr); if(!_pass) { *failures += 1; } \
fprintf(stderr, "%.*s rect %s: %s (" fmt ")\n", str8_varg(backend), (label), _pass ? "ok" : "DIFFERS", __VA_ARGS__); } while(0)
#define L(x, y, c) uishell_rect_semantics_linear(&pixels, (x), (y), (c))
    // A
    RectCase("A rounded top-left corner is cut", L(9, 9, 0) < 0.1f && L(9, 9, 1) < 0.1f && L(9, 9, 2) < 0.1f,
             "rgb %.2f %.2f %.2f", L(9, 9, 0), L(9, 9, 1), L(9, 9, 2));
    RectCase("A square top-right corner is filled and blue", L(54, 9, 2) > 0.8f && L(54, 9, 0) < 0.2f,
             "rgb %.2f %.2f %.2f", L(54, 9, 0), L(54, 9, 1), L(54, 9, 2));
    RectCase("A square bottom-left corner is filled and green", L(9, 54, 1) > 0.8f && L(9, 54, 0) < 0.2f,
             "rgb %.2f %.2f %.2f", L(9, 54, 0), L(9, 54, 1), L(9, 54, 2));
    RectCase("A square bottom-right corner is filled and white", L(54, 54, 0) > 0.8f && L(54, 54, 1) > 0.8f && L(54, 54, 2) > 0.8f,
             "rgb %.2f %.2f %.2f", L(54, 54, 0), L(54, 54, 1), L(54, 54, 2));
    // B
    RectCase("B zero softness keeps the inside", L(76, 20, 0) > 0.9f, "r %.2f", L(76, 20, 0));
    RectCase("B zero softness cuts the corner", L(65, 9, 0) < 0.1f, "r %.2f", L(65, 9, 0));
    // C
    RectCase("C scaled rect fills its middle", L(152, 24, 0) > 0.9f, "r %.2f", L(152, 24, 0));
    // A control: filled whether or not the radius scales. Failing it means
    // something other than scaling is wrong with C.
    RectCase("C scaled corner fills further in", L(142, 14, 0) > 0.9f, "r 10.5 px in %.2f", L(142, 14, 0));
    RectCase("C radius scales with the transform", L(137, 9, 0) < 0.2f, "r 5.5 px in %.2f", L(137, 9, 0));
    // D
    RectCase("D transparency 0.5 gives half", L(106, 20, 0) > 0.42f && L(106, 20, 0) < 0.58f, "r %.2f", L(106, 20, 0));
    // E
    RectCase("E border is drawn", L(66, 48, 0) > 0.5f, "r at edge %.2f", L(66, 48, 0));
    RectCase("E inside a border is empty", L(76, 48, 0) < 0.1f, "r inside %.2f", L(76, 48, 0));
#undef L
#undef RectCase
  }
  scratch_end(scratch);
}

// Every embedded effect compiles and links on this backend, through the same
// lookup the view surfaces use. A wrong entry point, binding or uniform block
// for the generated shaders shows up here as a zero handle.
internal void
uishell_check_effects_compile(U32 *failures)
{
  for(U64 idx = 0; idx < ArrayCount(efx_embedded_effects); idx += 1)
  {
    String8 name = efx_embedded_effects[idx].name;
    B32 ok = !r_handle_match(rd_effect_from_name(name), r_handle_zero());
    fprintf(stderr, "%.*s effect %.*s: %s\n", str8_varg(r_backend_name()), str8_varg(name), ok ? "ok" : "FAILED to compile or link");
    UIImportCheck(ok);
  }
}

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

// Symbols the UI font lacks draw from the first fallback that has them, the
// same glyph that fallback draws on its own; every one has a fallback.
internal void
uishell_check_font_fallbacks(U32 *failures)
{
  FNT_Tag main = fnt_tag_from_static_data_string(&rd_default_main_font_bytes);
  String8 symbols[] = {str8_lit("↗"), str8_lit("→"), str8_lit("⌘"), str8_lit("⇧"), str8_lit("⌥"), str8_lit("⌃"), str8_lit("▾"), str8_lit("▸")};
  for EachElement(idx, symbols)
  {
    U32 codepoint = utf8_decode(symbols[idx].str, symbols[idx].size).codepoint;
    UIImportCheck(!fnt_tag_has_codepoint(main, codepoint));
    FNT_Tag source = fnt_tag_zero();
    for(U64 fallback_idx = 0; fallback_idx < fnt_state->fallback_count; fallback_idx += 1)
    {
      if(fnt_tag_has_codepoint(fnt_state->fallbacks[fallback_idx], codepoint))
      {
        source = fnt_state->fallbacks[fallback_idx];
        break;
      }
    }
    UIImportCheck(!fnt_tag_match(source, fnt_tag_zero()));
    FNT_Run run = fnt_run_from_string(main, 16, 0, 0, FNT_RasterFlag_Smooth, symbols[idx]);
    FNT_Run expected = fnt_run_from_string(source, 16, 0, 0, FNT_RasterFlag_Smooth, symbols[idx]);
    UIImportCheck(run.pieces.count == 1 && expected.pieces.count == 1);
    if(run.pieces.count == 1 && expected.pieces.count == 1)
    {
      FNT_Piece *got = &run.pieces.v[0], *want = &expected.pieces.v[0];
      UIImportCheck(got->advance == want->advance && got->draw_dim.x == want->draw_dim.x &&
                    got->draw_dim.y == want->draw_dim.y && got->draw_dim.x > 0);
    }
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
  DR_FRunList list = {.first = &node, .last = &node, .node_count = 1, .dim = v2f32(40, 28)};
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
  failures += !uishell_query_ui_diagnostics(ws);
  failures += !uishell_setting_typing_diagnostics();
#if OS_LINUX
  // It sends X11 key events; the Wayland window manager has none to send.
  if(wm_native_surface(wm_window_zero()).kind == WM_NativeSurfaceKind_X11)
  {
    failures += !uishell_edit_x11_text_diagnostics(ws);
  }
#endif
#if OS_MAC
  failures += !uishell_edit_command_diagnostics(1);
#endif
  uishell_check_raster_baselines(scratch.arena, &failures);
  uishell_check_font_fallbacks(&failures);
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
  uishell_check_blur_kernel_bounds(&failures);
  U32 failures_before_blur = failures;
  uishell_check_blur_mask(&failures);
  fprintf(stderr, "blur mask: %u failures\n", failures - failures_before_blur);
  U32 failures_before_effects = failures;
  uishell_check_effects_compile(&failures);
  fprintf(stderr, "effects: %u failures\n", failures - failures_before_effects);
  U32 failures_before_rect = failures;
  uishell_check_rect_semantics(&failures);
  fprintf(stderr, "rect semantics: %u failures\n", failures - failures_before_rect);
  U32 failures_before_geo3d = failures;
  uishell_check_geo3d(&failures);
  fprintf(stderr, "geo3d: %u failures\n", failures - failures_before_geo3d);
  fprintf(stderr, "shared UI diagnostics: %s (%u failures)\n", failures ? "FAIL" : "PASS", failures);
  scratch_end(scratch);
  return failures == 0;
}
#undef UIImportCheck
