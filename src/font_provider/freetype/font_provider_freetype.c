// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Helpers

internal FP_FT_Font
fp_ft_font_from_handle(FP_Handle handle)
{
  FP_FT_Font result = {(FT_Face)handle.u64[0]};
  return result;
}

internal FP_Handle
fp_ft_handle_from_font(FP_FT_Font font)
{
  FP_Handle result = {(U64)font.face};
  return result;
}

internal F32
fp_ft_select_size(FT_Face face, F32 size)
{
  F32 result = 1.f;
  FT_UInt pixel_size = (FT_UInt)((96.f/72.f) * size);
  if(face->num_fixed_sizes > 0 && face->available_sizes != 0)
  {
    FT_Int best_idx = 0;
    S64 best_delta = max_S64;
    S64 requested_26_6 = (S64)pixel_size << 6;
    for(FT_Int idx = 0; idx < face->num_fixed_sizes; idx += 1)
    {
      FT_Bitmap_Size *strike = &face->available_sizes[idx];
      S64 strike_26_6 = (S64)(strike->y_ppem ? strike->y_ppem : ((S64)strike->height << 6));
      S64 delta = abs_s64(strike_26_6 - requested_26_6);
      if(delta < best_delta)
      {
        best_delta = delta;
        best_idx = idx;
      }
    }
    FT_Select_Size(face, best_idx);
    FT_Bitmap_Size *strike = &face->available_sizes[best_idx];
    S64 strike_26_6 = (S64)(strike->y_ppem ? strike->y_ppem : ((S64)strike->height << 6));
    if(strike_26_6 != 0)
    {
      result = (F32)requested_26_6/(F32)strike_26_6;
    }
  }
  else
  {
    FT_Set_Pixel_Sizes(face, 0, pixel_size);
  }
  return result;
}

internal U8
fp_ft_u8_from_unit_f32(F32 x)
{
  U8 result = (U8)Clamp(0, (S32)round_f32(Clamp(0.f, x, 1.f)*255.f), 255);
  return result;
}

internal U8
fp_ft_unpremultiply_u8(U8 c, U8 a)
{
  U8 result = 0;
  if(a != 0)
  {
    result = (U8)Clamp(0, ((S32)c*255 + (S32)a/2)/(S32)a, 255);
  }
  return result;
}

internal void
fp_ft_sample_straight_rgba_scaled(U8 *src_atlas, Vec2S16 src_dim, F32 src_x, F32 src_y, U8 *dst)
{
  S32 x0 = Clamp(0, (S32)floor_f32(src_x), src_dim.x - 1);
  S32 y0 = Clamp(0, (S32)floor_f32(src_y), src_dim.y - 1);
  S32 x1 = Clamp(0, x0 + 1, src_dim.x - 1);
  S32 y1 = Clamp(0, y0 + 1, src_dim.y - 1);
  F32 tx = Clamp(0.f, src_x - (F32)x0, 1.f);
  F32 ty = Clamp(0.f, src_y - (F32)y0, 1.f);
  F32 weights[4] =
  {
    (1.f - tx)*(1.f - ty),
    tx*(1.f - ty),
    (1.f - tx)*ty,
    tx*ty,
  };
  S32 xs[4] = {x0, x1, x0, x1};
  S32 ys[4] = {y0, y0, y1, y1};
  F32 premul_r = 0;
  F32 premul_g = 0;
  F32 premul_b = 0;
  F32 alpha = 0;
  for(U64 idx = 0; idx < ArrayCount(weights); idx += 1)
  {
    U8 *px = src_atlas + ((U64)ys[idx]*(U64)src_dim.x + (U64)xs[idx])*4;
    F32 a = (F32)px[3]/255.f;
    F32 w = weights[idx];
    premul_r += ((F32)px[0]/255.f)*a*w;
    premul_g += ((F32)px[1]/255.f)*a*w;
    premul_b += ((F32)px[2]/255.f)*a*w;
    alpha += a*w;
  }
  F32 r = 0;
  F32 g = 0;
  F32 b = 0;
  if(alpha > 0)
  {
    r = premul_r/alpha;
    g = premul_g/alpha;
    b = premul_b/alpha;
  }
  dst[0] = fp_ft_u8_from_unit_f32(r);
  dst[1] = fp_ft_u8_from_unit_f32(g);
  dst[2] = fp_ft_u8_from_unit_f32(b);
  dst[3] = fp_ft_u8_from_unit_f32(alpha);
}

//- system font lookup through fontconfig, loaded at runtime so builds and
// hosts without it still work (the caller then falls back to embedded fonts)

typedef struct FP_FT_FcPattern FP_FT_FcPattern;
typedef struct FP_FT_FcConfig FP_FT_FcConfig;
typedef FP_FT_FcConfig  *FP_FT_FcInitLoadConfigAndFontsFunction(void);
typedef void             FP_FT_FcConfigDestroyFunction(FP_FT_FcConfig *config);
typedef FP_FT_FcPattern *FP_FT_FcNameParseFunction(U8 const *name);
typedef int              FP_FT_FcConfigSubstituteFunction(FP_FT_FcConfig *config, FP_FT_FcPattern *pattern, int kind);
typedef void             FP_FT_FcDefaultSubstituteFunction(FP_FT_FcPattern *pattern);
typedef FP_FT_FcPattern *FP_FT_FcFontMatchFunction(FP_FT_FcConfig *config, FP_FT_FcPattern *pattern, int *result);
typedef int              FP_FT_FcPatternGetStringFunction(FP_FT_FcPattern const *pattern, char const *object, int n, U8 **value);
typedef int              FP_FT_FcPatternGetIntegerFunction(FP_FT_FcPattern const *pattern, char const *object, int n, int *value);
typedef int              FP_FT_FcPatternGetBoolFunction(FP_FT_FcPattern const *pattern, char const *object, int n, int *value);
typedef void             FP_FT_FcPatternDestroyFunction(FP_FT_FcPattern *pattern);

// Asks fontconfig for its best colour font for `family` and returns that file's
// path, or empty if fontconfig is unavailable, the match is not a colour font,
// or the match is not the file's first face (fp_font_open opens face 0).
internal String8
fp_ft_fontconfig_color_font_path_from_family(Arena *arena, String8 family)
{
  String8 result = {0};
  Library fc = library_open(str8_lit("libfontconfig.so.1"));
  if(fc.u64[0] != 0)
  {
    FP_FT_FcInitLoadConfigAndFontsFunction *FcInitLoadConfigAndFonts = (FP_FT_FcInitLoadConfigAndFontsFunction *)library_load_proc(fc, str8_lit("FcInitLoadConfigAndFonts"));
    FP_FT_FcConfigDestroyFunction *FcConfigDestroy = (FP_FT_FcConfigDestroyFunction *)library_load_proc(fc, str8_lit("FcConfigDestroy"));
    FP_FT_FcNameParseFunction *FcNameParse = (FP_FT_FcNameParseFunction *)library_load_proc(fc, str8_lit("FcNameParse"));
    FP_FT_FcConfigSubstituteFunction *FcConfigSubstitute = (FP_FT_FcConfigSubstituteFunction *)library_load_proc(fc, str8_lit("FcConfigSubstitute"));
    FP_FT_FcDefaultSubstituteFunction *FcDefaultSubstitute = (FP_FT_FcDefaultSubstituteFunction *)library_load_proc(fc, str8_lit("FcDefaultSubstitute"));
    FP_FT_FcFontMatchFunction *FcFontMatch = (FP_FT_FcFontMatchFunction *)library_load_proc(fc, str8_lit("FcFontMatch"));
    FP_FT_FcPatternGetStringFunction *FcPatternGetString = (FP_FT_FcPatternGetStringFunction *)library_load_proc(fc, str8_lit("FcPatternGetString"));
    FP_FT_FcPatternGetIntegerFunction *FcPatternGetInteger = (FP_FT_FcPatternGetIntegerFunction *)library_load_proc(fc, str8_lit("FcPatternGetInteger"));
    FP_FT_FcPatternGetBoolFunction *FcPatternGetBool = (FP_FT_FcPatternGetBoolFunction *)library_load_proc(fc, str8_lit("FcPatternGetBool"));
    FP_FT_FcPatternDestroyFunction *FcPatternDestroy = (FP_FT_FcPatternDestroyFunction *)library_load_proc(fc, str8_lit("FcPatternDestroy"));
    if(FcInitLoadConfigAndFonts && FcConfigDestroy && FcNameParse && FcConfigSubstitute && FcDefaultSubstitute &&
       FcFontMatch && FcPatternGetString && FcPatternGetInteger && FcPatternGetBool && FcPatternDestroy)
    {
      Temp scratch = scratch_begin(&arena, 1);
      FP_FT_FcConfig *config = FcInitLoadConfigAndFonts();
      String8 name = push_str8f(scratch.arena, "%S:color=True", family);
      FP_FT_FcPattern *pattern = FcNameParse(name.str);
      if(config != 0 && pattern != 0)
      {
        int const fc_match_pattern = 0;
        int const fc_result_match = 0;
        int match_result = 0;
        FcConfigSubstitute(config, pattern, fc_match_pattern);
        FcDefaultSubstitute(pattern);
        FP_FT_FcPattern *match = FcFontMatch(config, pattern, &match_result);
        if(match != 0)
        {
          U8 *file = 0;
          int index = 0;
          int color = 0;
          if(FcPatternGetBool(match, "color", 0, &color) == fc_result_match && color &&
             FcPatternGetString(match, "file", 0, &file) == fc_result_match && file != 0 &&
             (FcPatternGetInteger(match, "index", 0, &index) != fc_result_match || index == 0))
          {
            result = push_str8_copy(arena, str8_cstring((char *)file));
          }
          FcPatternDestroy(match);
        }
      }
      if(pattern != 0) { FcPatternDestroy(pattern); }
      if(config != 0)  { FcConfigDestroy(config); }
      scratch_end(scratch);
    }
    library_close(fc);
  }
  return result;
}

////////////////////////////////
//~ rjf: Backend Implementations

fp_hook void
fp_init(void)
{
  Arena *arena = arena_alloc();
  fp_ft_state = push_array(arena, FP_FT_State, 1);
  fp_ft_state->arena = arena;
  FT_Init_FreeType(&fp_ft_state->library);
}

fp_hook FP_Handle
fp_font_open(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  String8 path_copy = push_str8_copy(scratch.arena, path);
  FP_FT_Font font = {0};
  FT_New_Face(fp_ft_state->library, (char *)path_copy.str, 0, &font.face);
  FP_Handle handle = fp_ft_handle_from_font(font);
  scratch_end(scratch);
  return handle;
}

fp_hook FP_Handle
fp_font_open_from_static_data_string(String8 *data_ptr)
{
  FP_FT_Font font = {0};
  FT_New_Memory_Face(fp_ft_state->library, data_ptr->str, (FT_Long)data_ptr->size, 0, &font.face);
  FP_Handle handle = fp_ft_handle_from_font(font);
  return handle;
}

fp_hook void
fp_font_close(FP_Handle handle)
{
  FP_FT_Font font = fp_ft_font_from_handle(handle);
  if(font.face != 0)
  {
    FT_Done_Face(font.face);
  }
}

fp_hook FP_Metrics
fp_metrics_from_font(FP_Handle handle)
{
  FP_FT_Font font = fp_ft_font_from_handle(handle);
  FP_Metrics result = {0};
  if(font.face != 0)
  {
    result.design_units_per_em = (F32)(font.face->units_per_EM);
    result.ascent              = (F32)font.face->ascender;
    result.descent             = -(F32)font.face->descender;
    result.line_gap            = (F32)(font.face->height - font.face->ascender + font.face->descender);
    result.capital_height      = (F32)(font.face->ascender);
    if(result.design_units_per_em <= 0)
    {
      result.design_units_per_em = 1000.f;
      if(result.ascent == 0 && result.descent == 0)
      {
        result.ascent = 800.f;
        result.descent = 200.f;
        result.line_gap = 0.f;
        result.capital_height = result.ascent;
      }
    }
  }
  return result;
}

fp_hook B32
fp_font_has_codepoint(FP_Handle handle, U32 codepoint)
{
  FP_FT_Font font = fp_ft_font_from_handle(handle);
  B32 result = 0;
  if(font.face != 0)
  {
    result = (FT_Get_Char_Index(font.face, codepoint) != 0);
  }
  return result;
}

fp_hook FP_RasterResult
fp_raster(Arena *arena, FP_Handle handle, F32 size, FP_RasterFlags flags, String8 string)
{
  ProfBeginFunction();
  FP_FT_Font font = fp_ft_font_from_handle(handle);
  FP_RasterResult result = {0};
  if(font.face != 0)
  {
    Temp scratch = scratch_begin(&arena, 1);
    
    //- rjf: unpack font
    FT_Face face = font.face;
    F32 fixed_size_scale = fp_ft_select_size(face, size);
    B32 tight_bounds = !!(flags & FP_RasterFlag_TightBounds);
    S64 ascent  = face->size->metrics.ascender >> 6;
    S64 descent = abs_s64(face->size->metrics.descender >> 6);
    S64 height  = face->size->metrics.height >> 6;
    
    //- rjf: unpack string
    String32 string32 = str32_from_8(scratch.arena, string);
    
    //- rjf: measure
    S32 total_width = 0;
    S32 min_x = max_S32;
    S32 min_y = max_S32;
    S32 max_x = min_S32;
    S32 max_y = min_S32;
    S32 baseline = ascent;
    FT_Int32 load_flags = FT_LOAD_RENDER|FT_LOAD_COLOR;
    for EachIndex(idx, string32.size)
    {
      FT_Load_Char(face, string32.str[idx], load_flags);
      S32 left = face->glyph->bitmap_left;
      S32 top = face->glyph->bitmap_top;
      S32 x0 = total_width + left;
      S32 y0 = baseline - top;
      S32 x1 = x0 + (S32)face->glyph->bitmap.width;
      S32 y1 = y0 + (S32)face->glyph->bitmap.rows;
      if(x0 < x1 && y0 < y1)
      {
        min_x = Min(min_x, x0);
        min_y = Min(min_y, y0);
        max_x = Max(max_x, x1);
        max_y = Max(max_y, y1);
      }
      total_width += (face->glyph->advance.x >> 6);
    }

    //- rjf: allocate & fill atlas w/ rasterization
    if(!tight_bounds || min_x >= max_x || min_y >= max_y)
    {
      min_x = 0;
      min_y = 0;
      max_x = total_width + 1;
      max_y = height + 1;
    }
    Vec2S16 dim = {(S16)(max_x - min_x), (S16)(max_y - min_y)};
    U64 atlas_size = dim.x * dim.y * 4;
    U8 *atlas = push_array(arena, U8, atlas_size);
    S32 atlas_write_x = 0;
    B32 has_source_color = 0;
    for EachIndex(idx, string32.size)
    {
      FT_Load_Char(face, string32.str[idx], load_flags);
      FT_Bitmap *bmp = &face->glyph->bitmap;
      S32 top = face->glyph->bitmap_top;
      S32 left = face->glyph->bitmap_left;
      for(S32 row = 0; row < (S32)bmp->rows; row += 1)
      {
        S32 y = baseline - top + row - min_y;
        for(S32 col = 0; col < (S32)bmp->width; col += 1)
        {
          S32 x = atlas_write_x + left + col - min_x;
          U64 off = (y*dim.x + x)*4;
          if(off+4 <= atlas_size)
          {
            S64 pitch = bmp->pitch;
            U8 *src = (pitch >= 0 ?
                       bmp->buffer + row*pitch :
                       bmp->buffer + ((S32)bmp->rows - 1 - row)*-pitch);
            if(bmp->pixel_mode == FT_PIXEL_MODE_BGRA)
            {
              U8 *px = src + col*4;
              atlas[off+0] = fp_ft_unpremultiply_u8(px[2], px[3]);
              atlas[off+1] = fp_ft_unpremultiply_u8(px[1], px[3]);
              atlas[off+2] = fp_ft_unpremultiply_u8(px[0], px[3]);
              atlas[off+3] = px[3];
              has_source_color = 1;
            }
            else
            {
              atlas[off+0] = 255;
              atlas[off+1] = 255;
              atlas[off+2] = 255;
              atlas[off+3] = src[col];
            }
          }
        }
      }
      atlas_write_x += (face->glyph->advance.x >> 6);
    }
    
    //- rjf: scale fixed-strike bitmap output back to the requested size
    if(abs_f32(fixed_size_scale - 1.f) > 0.001f && dim.x > 0 && dim.y > 0)
    {
      Vec2S16 scaled_dim =
      {
        (S16)ClampBot(1, (S32)ceil_f32((F32)dim.x*fixed_size_scale)),
        (S16)ClampBot(1, (S32)ceil_f32((F32)dim.y*fixed_size_scale)),
      };
      U64 scaled_size = (U64)scaled_dim.x*(U64)scaled_dim.y*4;
      U8 *scaled_atlas = push_array(arena, U8, scaled_size);
      for(S32 y = 0; y < scaled_dim.y; y += 1)
      {
        F32 src_y = ((F32)y + 0.5f)/fixed_size_scale - 0.5f;
        for(S32 x = 0; x < scaled_dim.x; x += 1)
        {
          F32 src_x = ((F32)x + 0.5f)/fixed_size_scale - 0.5f;
          U8 *dst = scaled_atlas + ((U64)y*(U64)scaled_dim.x + (U64)x)*4;
          fp_ft_sample_straight_rgba_scaled(atlas, dim, src_x, src_y, dst);
        }
      }
      dim = scaled_dim;
      atlas = scaled_atlas;
    }

    //- rjf: fill result
    result.atlas_dim = dim;
    result.advance   = (F32)total_width*fixed_size_scale;
    result.origin_from_left = (F32)(-min_x)*fixed_size_scale;
    result.baseline_from_top = (F32)(baseline - min_y)*fixed_size_scale;
    result.face_box_origin_from_left = 0;
    result.face_box_baseline_from_top = (F32)baseline*fixed_size_scale;
    result.atlas     = atlas;
    result.kind      = has_source_color ? FP_RasterKind_RGBA : FP_RasterKind_Mask;
    scratch_end(scratch);
  }
  ProfEnd();
  return result;
}

fp_hook FP_SystemFontArray
fp_system_color_emoji_fonts(void)
{
  if(!fp_ft_state->system_color_emoji_fonts_resolved)
  {
    // fontconfig's generic emoji family; callers fall back to embedded Noto.
    String8 families[] = {str8_lit_comp("emoji")};
    FP_SystemFontArray *fonts = &fp_ft_state->system_color_emoji_fonts;
    fonts->v = push_array(fp_ft_state->arena, FP_SystemFont, ArrayCount(families));
    fonts->count = ArrayCount(families);
    for EachElement(idx, families)
    {
      fonts->v[idx].family = families[idx];
      fonts->v[idx].path = fp_ft_fontconfig_color_font_path_from_family(fp_ft_state->arena, families[idx]);
    }
    fp_ft_state->system_color_emoji_fonts_resolved = 1;
  }
  return fp_ft_state->system_color_emoji_fonts;
}
