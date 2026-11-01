// Headless checks of glyph-id rasterization in this OS's font provider (#560),
// into CPU buffers. Glyphs come from wh_text layouts, so the faces, synthesis
// and variations are the ones fontique reports. The test fonts are synthetic
// (native/src/font_cache/tests/fixtures/make_fixtures.py): their glyph 1 is a
// plain box at "I", glyph 2 the box or colour glyph at "A".

typedef struct UISHELL_GlyphCoverage UISHELL_GlyphCoverage;
struct UISHELL_GlyphCoverage
{
  F32 sum;
  // Coverage-weighted, in pixels from the pen.
  F32 centroid_x;
  F32 top_half_x;
  F32 bottom_half_x;
  B32 chroma;
};

internal UISHELL_GlyphCoverage
uishell_glyph_coverage(FP_GlyphRaster *r)
{
  UISHELL_GlyphCoverage result = {0};
  U64 bpp = (r->kind == FP_RasterKind_RGBA ? 4 : 1);
  F32 top_sum = 0, bottom_sum = 0;
  for(S32 y = 0; y < r->dim.y; y += 1)
  {
    for(S32 x = 0; x < r->dim.x; x += 1)
    {
      U8 *px = r->pixels + ((U64)y*r->dim.x + x)*bpp;
      F32 a = (F32)px[bpp-1]/255.f;
      F32 px_x = (F32)(r->left + x) + 0.5f;
      result.sum += a;
      result.centroid_x += a*px_x;
      if(y < r->dim.y/2) { top_sum += a; result.top_half_x += a*px_x; }
      else { bottom_sum += a; result.bottom_half_x += a*px_x; }
      if(bpp == 4 && Max(px[0], Max(px[1], px[2])) > Min(px[0], Min(px[1], px[2])) + 16) { result.chroma = 1; }
    }
  }
  if(result.sum > 0) { result.centroid_x /= result.sum; }
  if(top_sum > 0) { result.top_half_x /= top_sum; }
  if(bottom_sum > 0) { result.bottom_half_x /= bottom_sum; }
  return result;
}

internal B32
uishell_glyph_check(B32 ok, char *name, String8 detail)
{
  fprintf(stderr, "%s glyph raster: %s (%.*s)\n", ok ? "PASS" : "FAIL", name, str8_varg(detail));
  return ok;
}

internal void *
uishell_glyph_wh_alloc(void *user, size_t size, size_t align)
{
  return arena_push((Arena *)user, size, align, 1);
}

typedef struct UISHELL_GlyphRun UISHELL_GlyphRun;
struct UISHELL_GlyphRun
{
  B32 ok;
  FNT_Tag tag;
  WhTextRun run;
  U32 glyph_id;
  S16 *coords;
  U64 coord_count;
};

// The first glyph of `text` laid out in `family` at 48px, as a tag and the
// run's synthesis and variations.
internal UISHELL_GlyphRun
uishell_glyph_run(Arena *arena, WhText *ctx, String8 family, String8 text, F32 weight, B32 italic)
{
  UISHELL_GlyphRun result = {0};
  WheelhouseText name = {family.str, family.size};
  wh_text_set_family(ctx, WH_TEXT_FAMILY_MAIN, name);
  WhTextStyle style = {WH_TEXT_FAMILY_MAIN, 48.f, weight, (U32)italic};
  WhTextInput input = {0};
  input.text = (WheelhouseText){text.str, text.size};
  input.styles = &style;
  input.style_count = 1;
  WhTextAllocator alloc = {uishell_glyph_wh_alloc, arena};
  WhTextLayout layout = {0};
  char *error = 0;
  if(wh_text_layout(ctx, &input, INFINITY, alloc, &layout, &error) && layout.run_count > 0 && layout.glyph_count > 0)
  {
    WhTextFont font = {0};
    result.run = layout.runs[0];
    result.glyph_id = layout.glyphs[result.run.glyph_start].id;
    if(wh_text_font(ctx, result.run.font, &font))
    {
      // Faces fontique resolves become tags: a registered font by the bytes
      // native registered, so it isn't opened twice.
      result.tag = (font.kind == WH_TEXT_FONT_BYTES ?
                    fnt_tag_from_static_data_string((String8 *)font.bytes_id) :
                    fnt_tag_from_memory_face(str8((U8 *)font.data, font.data_len), font.face_index));
      const int16_t *coords = 0;
      result.coord_count = wh_text_coords(ctx, result.run.coords, &coords);
      result.coords = (S16 *)coords;
      result.ok = !fnt_tag_match(result.tag, fnt_tag_zero());
    }
  }
  if(error != 0) { wheelhouse_string_free(error); }
  return result;
}

internal FP_GlyphRaster
uishell_glyph_raster(Arena *arena, UISHELL_GlyphRun *g, F32 subpixel_x, B32 embolden, F32 skew, B32 varied)
{
  FP_GlyphRasterParams params = {0};
  params.glyph_id = g->glyph_id;
  params.size_px = g->run.size;
  params.subpixel_x = subpixel_x;
  params.coords = varied ? g->coords : 0;
  params.coord_count = varied ? g->coord_count : 0;
  params.embolden = embolden;
  params.skew = skew;
  params.flags = FP_RasterFlag_Smooth|FP_RasterFlag_Hinted;
  return fp_raster_glyph(arena, fnt_handle_from_tag(g->tag), &params);
}

internal FP_GlyphRaster
uishell_glyph_raster_id(Arena *arena, FNT_Tag tag, U32 glyph_id, F32 size)
{
  FP_GlyphRasterParams params = {0};
  params.glyph_id = glyph_id;
  params.size_px = size;
  params.flags = FP_RasterFlag_Smooth;
  return fp_raster_glyph(arena, fnt_handle_from_tag(tag), &params);
}

internal B32
uishell_glyph_raster_diagnostics(void)
{
  fp_init();
  fnt_init();
  Arena *arena = arena_alloc();
  B32 ok = 1;
  String8 fixtures = push_str8f(arena, "%S/../native/src/font_cache/tests/fixtures", get_process_info()->binary_path);
  String8 var_name = str8_lit("wheelhouse-test-var.ttf");
  String8 colr_name = str8_lit("wheelhouse-test-colr.ttf");
  String8 sbix_name = str8_lit("wheelhouse-test-sbix.ttf");
  String8 *var = push_array(arena, String8, 1);
  String8 *colr = push_array(arena, String8, 1);
  String8 *sbix = push_array(arena, String8, 1);
  *var = data_from_file_path(arena, push_str8f(arena, "%S/%S", fixtures, var_name));
  *colr = data_from_file_path(arena, push_str8f(arena, "%S/%S", fixtures, colr_name));
  *sbix = data_from_file_path(arena, push_str8f(arena, "%S/%S", fixtures, sbix_name));
  ok = uishell_glyph_check(var->size && colr->size && sbix->size, "test fonts load", fixtures) && ok;

  WhText *ctx = wh_text_create(0);
  String8 *registered[] = {&rd_default_main_font_bytes, &rd_terminal_noto_color_emoji_font_bytes, var, colr, sbix};
  for EachElement(idx, registered)
  {
    wh_text_register_font(ctx, registered[idx]->str, registered[idx]->size, (U64)registered[idx]);
  }
  wh_text_set_fallbacks(ctx, 0, 0);

  //- bounds and coverage, across a sample of glyphs
  {
    String8 sample = str8_lit("AgW@");
    for EachIndex(idx, sample.size)
    {
      UISHELL_GlyphRun g = uishell_glyph_run(arena, ctx, str8_lit("Atkinson Hyperlegible Next"), str8_substr(sample, r1u64(idx, idx+1)), 400, 0);
      FP_GlyphRaster r = g.ok ? uishell_glyph_raster(arena, &g, 0, 0, 0, 0) : (FP_GlyphRaster){0};
      UISHELL_GlyphCoverage c = uishell_glyph_coverage(&r);
      B32 bounded = (r.dim.x > 0 && r.dim.y > 0 && r.dim.y <= 2*g.run.size && r.top > 0 && r.top <= 1.5f*g.run.size &&
                     r.left > -g.run.size/2 && r.left < g.run.size);
      ok = uishell_glyph_check(g.ok && bounded && c.sum > 4.f && r.kind == FP_RasterKind_Mask, "a glyph's bounds and coverage",
                               push_str8f(arena, "'%c' glyph %u: %ix%i at %i,%i, coverage %.1f", sample.str[idx], g.glyph_id, r.dim.x, r.dim.y, r.left, r.top, c.sum)) && ok;
    }
  }

  //- the subpixel offset moves the coverage, by about as much
  {
    UISHELL_GlyphRun g = uishell_glyph_run(arena, ctx, str8_lit("Atkinson Hyperlegible Next"), str8_lit("l"), 400, 0);
    UISHELL_GlyphCoverage c0 = {0}, c1 = {0};
    if(g.ok)
    {
      FP_GlyphRaster r0 = uishell_glyph_raster(arena, &g, 0, 0, 0, 0);
      FP_GlyphRaster r1 = uishell_glyph_raster(arena, &g, 0.5f, 0, 0, 0);
      c0 = uishell_glyph_coverage(&r0);
      c1 = uishell_glyph_coverage(&r1);
    }
    F32 shift = c1.centroid_x - c0.centroid_x;
    ok = uishell_glyph_check(g.ok && shift > 0.3f && shift < 0.7f, "a half-pixel offset moves coverage half a pixel",
                             push_str8f(arena, "moved %.3f", shift)) && ok;
  }

  //- synthesis and variations, as fontique reports them
  {
    UISHELL_GlyphRun regular = uishell_glyph_run(arena, ctx, str8_lit("Wheelhouse Test COLR"), str8_lit("I"), 400, 0);
    UISHELL_GlyphRun bold = uishell_glyph_run(arena, ctx, str8_lit("Wheelhouse Test COLR"), str8_lit("I"), 700, 0);
    UISHELL_GlyphRun italic = uishell_glyph_run(arena, ctx, str8_lit("Wheelhouse Test COLR"), str8_lit("I"), 400, 1);
    F32 plain = 0, emboldened = 0, lean = 0;
    if(regular.ok && bold.ok && italic.ok)
    {
      FP_GlyphRaster r = uishell_glyph_raster(arena, &regular, 0, 0, 0, 0);
      FP_GlyphRaster b = uishell_glyph_raster(arena, &bold, 0, bold.run.embolden, 0, 0);
      FP_GlyphRaster i = uishell_glyph_raster(arena, &italic, 0, 0, italic.run.skew, 0);
      plain = uishell_glyph_coverage(&r).sum;
      emboldened = uishell_glyph_coverage(&b).sum;
      UISHELL_GlyphCoverage ic = uishell_glyph_coverage(&i);
      lean = ic.top_half_x - ic.bottom_half_x;
    }
    ok = uishell_glyph_check(bold.run.embolden && emboldened > plain*1.1f, "a bold that fontique synthesises emboldens",
                             push_str8f(arena, "embolden %u, coverage %.1f against %.1f", bold.run.embolden, emboldened, plain)) && ok;
    ok = uishell_glyph_check(italic.run.skew != 0 && lean > 2.f, "an italic that fontique synthesises leans right",
                             push_str8f(arena, "skew %.1f degrees, top half %.2fpx right of the bottom", italic.run.skew, lean)) && ok;

    // wght 700 normalizes to 0.6, which avar maps to 0.4: a stem of
    // 100 + 0.4*200 units, at 48px per 1000.
    UISHELL_GlyphRun heavy = uishell_glyph_run(arena, ctx, str8_lit("Wheelhouse Test Var"), str8_lit("I"), 700, 0);
    UISHELL_GlyphRun normal = uishell_glyph_run(arena, ctx, str8_lit("Wheelhouse Test Var"), str8_lit("I"), 400, 0);
    F32 stem = 0, default_stem = 0;
    if(heavy.ok && normal.ok)
    {
      FP_GlyphRaster h = uishell_glyph_raster(arena, &heavy, 0, heavy.run.embolden, 0, 1);
      FP_GlyphRaster n = uishell_glyph_raster(arena, &normal, 0, 0, 0, 1);
      F32 height = 700.f*heavy.run.size/1000.f;
      stem = uishell_glyph_coverage(&h).sum/height;
      default_stem = uishell_glyph_coverage(&n).sum/height;
    }
    F32 want = 180.f*48.f/1000.f;
    ok = uishell_glyph_check(heavy.coord_count == 1 && !heavy.run.embolden && abs_f32(stem - want) < 1.f && abs_f32(default_stem - 4.8f) < 1.f,
                             "a variable font's axes come out as fontique reports them",
                             push_str8f(arena, "%llu coords (%i), stem %.2fpx (want %.2f), default %.2fpx (want 4.80)",
                                        (unsigned long long)heavy.coord_count, heavy.coord_count ? heavy.coords[0] : 0, stem, want, default_stem)) && ok;
  }

  //- colour: CBDT, COLRv0 and sbix come back as colour; a plain glyph of a colour font doesn't
  {
    UISHELL_GlyphRun emoji = uishell_glyph_run(arena, ctx, str8_lit("Noto Color Emoji"), str8_lit("\xF0\x9F\x98\x80"), 400, 0);
    FP_GlyphRaster cbdt = emoji.ok ? uishell_glyph_raster(arena, &emoji, 0, 0, 0, 0) : (FP_GlyphRaster){0};
    FNT_Tag colr_tag = fnt_tag_from_static_data_string(colr);
    FNT_Tag sbix_tag = fnt_tag_from_static_data_string(sbix);
    FP_GlyphRaster layers = uishell_glyph_raster_id(arena, colr_tag, 2, 48);
    FP_GlyphRaster plain = uishell_glyph_raster_id(arena, colr_tag, 1, 48);
    FP_GlyphRaster strike = uishell_glyph_raster_id(arena, sbix_tag, 2, 48);
    struct { char *name; FP_GlyphRaster *r; FP_RasterKind kind; } cases[] =
    {
      {"CBDT emoji", &cbdt, FP_RasterKind_RGBA},
      {"COLRv0 layers", &layers, FP_RasterKind_RGBA},
      {"a COLR font's plain glyph", &plain, FP_RasterKind_Mask},
      {"sbix strike", &strike, FP_RasterKind_RGBA},
    };
    for EachElement(idx, cases)
    {
      UISHELL_GlyphCoverage c = uishell_glyph_coverage(cases[idx].r);
      B32 right = (cases[idx].r->kind == cases[idx].kind && c.sum > 4.f && (cases[idx].kind == FP_RasterKind_Mask || c.chroma));
      ok = uishell_glyph_check(right, cases[idx].name, push_str8f(arena, "kind %u, %ix%i, coverage %.1f, chroma %u",
                                                                  cases[idx].r->kind, cases[idx].r->dim.x, cases[idx].r->dim.y, c.sum, c.chroma)) && ok;
    }
  }

  //- a mask page's sample is its coverage in every channel, so a glyph's
  // uncovered pixels draw nothing (#561: black cells behind code)
  {
    // As every backend applies it: out[j] = sum over i of sample[i]*map[i][j].
    Mat4x4F32 map = r_sample_channel_map_from_tex2dformat(R_Tex2DFormat_R8);
    F32 coverage = 0.25f, sample[4] = {coverage, 0, 0, 1}, out[4] = {0};
    for EachIndex(j, 4) { for EachIndex(i, 4) { out[j] += sample[i]*map.v[i][j]; } }
    B32 right = 1;
    for EachIndex(j, 4) { right = right && out[j] == coverage; }
    ok = uishell_glyph_check(right, "an R8 mask samples as its coverage, alpha too",
                             push_str8f(arena, "(%.2f, %.2f, %.2f, %.2f)", out[0], out[1], out[2], out[3])) && ok;
  }

  wh_text_free(ctx);
  fprintf(stderr, "glyph raster diagnostics %s\n", ok ? "passed" : "failed");
  return ok;
}
