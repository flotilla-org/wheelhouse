// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef FONT_PROVIDER_H
#define FONT_PROVIDER_H

#define fp_hook C_LINKAGE

////////////////////////////////
//~ rjf: Types

typedef U32 FP_RasterFlags;
enum
{
  FP_RasterFlag_Smooth  = (1<<0),
  FP_RasterFlag_Hinted  = (1<<1),
  FP_RasterFlag_TightBounds = (1<<2),
};

typedef U32 FP_RasterKind;
enum
{
  FP_RasterKind_Mask,
  FP_RasterKind_RGBA,
};

typedef struct FP_Handle FP_Handle;
struct FP_Handle
{
  U64 u64[2];
};

typedef struct FP_Metrics FP_Metrics;
struct FP_Metrics
{
  F32 design_units_per_em;
  F32 ascent;
  F32 descent;
  F32 line_gap;
  F32 capital_height;
};

typedef struct FP_RasterResult FP_RasterResult;
struct FP_RasterResult
{
  Vec2S16 atlas_dim;
  void *atlas;
  F32 advance;
  F32 origin_from_left;
  F32 baseline_from_top;
  F32 face_box_origin_from_left;
  FP_RasterKind kind;
};

// A system font looked up by family name. `path` is the font file the provider
// resolved for `family`, or empty when this host does not have that family.
typedef struct FP_SystemFont FP_SystemFont;
struct FP_SystemFont
{
  String8 family;
  String8 path;
};

// Glyph-id rasterization (#560): layout places glyphs, so there is no
// horizontal hinting, only grayscale coverage.
typedef struct FP_GlyphRasterParams FP_GlyphRasterParams;
struct FP_GlyphRasterParams
{
  U32 glyph_id;
  F32 size_px;
  // The pen's fraction of a pixel, [0, 1).
  F32 subpixel_x;
  // Normalized (2.14), in the font's fvar axis order.
  S16 *coords;
  U64 coord_count;
  B32 embolden;
  // Synthetic italic, in degrees; 0 for none.
  F32 skew;
  // Smooth: antialiased. Hinted: vertically only.
  FP_RasterFlags flags;
};

// Mask pixels are coverage, one byte each; RGBA pixels are premultiplied.
// left and top place the bitmap's top-left from the pen on the baseline
// (top is up).
typedef struct FP_GlyphRaster FP_GlyphRaster;
struct FP_GlyphRaster
{
  FP_RasterKind kind;
  Vec2S16 dim;
  S16 left;
  S16 top;
  U8 *pixels;
};

typedef struct FP_AxisValue FP_AxisValue;
struct FP_AxisValue
{
  U32 tag;
  F32 value;
};

typedef struct FP_SystemFontArray FP_SystemFontArray;
struct FP_SystemFontArray
{
  FP_SystemFont *v;
  U64 count;
};

////////////////////////////////
//~ rjf: Basic Type Functions

internal FP_Handle fp_handle_zero(void);
internal B32 fp_handle_match(FP_Handle a, FP_Handle b);

////////////////////////////////
//~ Font Tables

internal U32 fp_tag_from_string(char *tag);
internal U64 fp_user_axis_values_from_normalized(String8 fvar, String8 avar, S16 *coords, U64 coord_count, FP_AxisValue *out, U64 out_cap);
internal B32 fp_colr_glyph_has_layers(String8 colr, U32 glyph_id);
internal FP_GlyphRaster fp_glyph_raster_scaled(Arena *arena, FP_GlyphRaster src, F32 scale);

////////////////////////////////
//~ rjf: Backend Hooks

fp_hook void fp_init(void);
fp_hook FP_Handle fp_font_open(String8 path);
fp_hook FP_Handle fp_font_open_from_static_data_string(String8 *data_ptr);
// A face of a collection; data must outlive the handle.
fp_hook FP_Handle fp_font_open_face(String8 path, U32 face_index);
fp_hook FP_Handle fp_font_open_face_from_memory(String8 data, U32 face_index);
fp_hook void fp_font_close(FP_Handle handle);
fp_hook FP_Metrics fp_metrics_from_font(FP_Handle font);
fp_hook B32 fp_font_has_codepoint(FP_Handle font, U32 codepoint);
fp_hook ASAN_NO_ADDR FP_RasterResult fp_raster(Arena *arena, FP_Handle font, F32 size, FP_RasterFlags flags, String8 string);
fp_hook FP_GlyphRaster fp_raster_glyph(Arena *arena, FP_Handle font, FP_GlyphRasterParams *params);
// The platform's colour emoji families, in preference order, each looked up by
// family name and resolved to a font file this provider can open. Resolved once;
// the provider owns the result, which stays valid for the life of the process.
fp_hook FP_SystemFontArray fp_system_color_emoji_fonts(void);

#endif // FONT_PROVIDER_H
