// Included beside the cache implementation; exercises the production cache with
// deterministic byte/texture collaborators, without allocating real GPU assets.
typedef struct UIShell_ImageTest UIShell_ImageTest;
struct UIShell_ImageTest
{
  U64 lookups, uploads, releases;
  U32 pixel;
  B32 unavailable, fail_upload;
};

internal B32
uishell_image_test_lookup(void *context, cleat_image_resource const *meta, UIShell_TerminalImageBytes *bytes)
{
  UIShell_ImageTest *test = context;
  test->lookups++;
  if(test->unavailable) { return 0; }
  U32 pixel = (U32)meta->generation;
  return uishell_terminal_image_resource_data_copy(bytes, (U8 *)&pixel, sizeof(pixel));
}

internal R_Handle
uishell_image_test_upload(void *context, U32 w, U32 h, U8 *rgba)
{
  UIShell_ImageTest *test = context;
  test->uploads++;
  MemoryCopy(&test->pixel, rgba, 4);
  R_Handle result = {{test->fail_upload ? 0 : test->uploads}};
  return result;
}

internal void
uishell_image_test_release(void *context, R_Handle texture)
{
  UIShell_ImageTest *test = context;
  test->releases++;
}

internal B32
uishell_terminal_image_diagnostics(void)
{
  B32 ok = 1;
#define IMAGE_CHECK(condition, message) do { if(!(condition)) { log_user_errorf("image cache diagnostics: %s", message); ok = 0; } } while(0)
  UIShell_ImageTest test = {0};
  UIShell_TerminalImageOps ops = {&test, uishell_image_test_lookup, uishell_image_test_upload, uishell_image_test_release, 4};
  UIShell_TerminalImageCache cache = {0};
  cleat_image_resource resource = {.image_id=7, .generation=1, .width_px=1, .height_px=1,
    .format=CLEAT_IMAGE_FORMAT_RGBA, .compression=CLEAT_IMAGE_COMPRESSION_NONE};
  cleat_image_placement placement = {.size=sizeof(cleat_image_placement), .image_id=7, .generation=1,
    .source_width=1, .source_height=1, .grid_cols=1, .grid_rows=1};
  cleat_render_update update = {.image_resources=&resource, .image_resource_count=1,
    .image_placements=&placement, .image_placement_count=1};
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(test.lookups == 1 && test.uploads == 1 && test.pixel == 1, "initial upload");
  placement.viewport_col = 3;
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(test.lookups == 1 && test.uploads == 1 && cache.placements[0].viewport_col == 3, "placement-only update must reuse texture");
  resource.generation = placement.generation = 2;
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(test.uploads == 2 && test.releases == 1 && test.pixel == 2, "new generation replaces pixels and releases old texture");
  test.unavailable = 1;
  resource.generation = placement.generation = 3;
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(!cache.first_resource->valid && test.releases == 2, "missing new generation must not display old pixels");
  test.unavailable = 0;
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(cache.first_resource->valid && test.pixel == 3 && test.uploads == 3, "unavailable bytes retried at same generation");
  cleat_render_update empty = {0};
  uishell_terminal_image_cache_apply_with_ops(&cache, &empty, &ops);
  IMAGE_CHECK(cache.placement_count == 0 && cache.first_resource->valid && test.releases == 2, "placement removal retains asset below quota");
  resource.image_id = placement.image_id = 8;
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(uishell_terminal_image_cache_resource_from_id(&cache, 7) == 0 && test.releases == 3, "unreferenced resource evicted over quota");
  resource.image_id = placement.image_id = 7;
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(test.uploads == 5 && cache.first_resource->image_id == 7 && cache.first_resource->valid, "evicted resource can be fetched again");
  ops.quota_bytes = 0;
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(cache.first_resource != 0 && test.uploads == 5, "current references exempt from quota");
  uishell_terminal_image_cache_apply_with_ops(&cache, &empty, &ops);
  IMAGE_CHECK(cache.first_resource == 0 && test.releases == 5, "unreferenced texture released exactly once");
  test.fail_upload = 1;
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(!cache.first_resource->valid, "failed allocation stays retryable");
  test.fail_upload = 0;
  uishell_terminal_image_cache_apply_with_ops(&cache, &update, &ops);
  IMAGE_CHECK(cache.first_resource->valid && test.uploads == 7, "allocation retry succeeds without generation change");
  uishell_terminal_image_cache_apply_with_ops(&cache, &empty, &ops);
  arena_release(cache.arena);
  arena_release(cache.placement_arena);

  Rng2F32 visible, source;
  IMAGE_CHECK(uishell_terminal_image_clip(r2f32p(-10,-20,30,60), r2f32p(20,30,100,190),
    r2f32p(0,0,20,40), &visible, &source), "partially visible crop");
  IMAGE_CHECK(source.x0 == 40 && source.y0 == 70 && source.x1 == 80 && source.y1 == 150,
    "source crop tracks both axes and canvas clipping");
  IMAGE_CHECK(!uishell_terminal_image_clip(r2f32p(-20,0,-10,10), r2f32p(0,0,1,1),
    r2f32p(0,0,20,40), &visible, &source), "outside canvas emits no image");
#undef IMAGE_CHECK
  if(ok) { log_infof("terminal image cache diagnostics passed"); }
  return ok;
}
