// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BUILD_CONSOLE_INTERFACE 1
#define R_BACKEND R_BACKEND_STUB
#define WM_STUB 1
#include "base/base_inc.h"
#include "window_manager/window_manager_inc.h"
#include "render/render_inc.h"
#include "font_provider/font_provider_inc.h"
#include "font_cache/font_cache.h"
#include "draw/draw.h"
#include "base/base_inc.c"
#include "window_manager/window_manager_inc.c"
#include "render/render_inc.c"
#include "font_provider/font_provider_inc.c"
#include "font_cache/font_cache.c"
#include "draw/draw.c"

internal void
entry_point(CmdLine *cmdline)
{
  fnt_frame();
  dr_begin_frame(fnt_tag_zero());
  String8 fonts[] = {str8_lit("data/segoeui.ttf"),
                    str8_lit("data/Inconsolata-Regular.ttf"),
                    str8_lit("data/JetBrainsMono-Regular.ttf")};
  F32 sizes[] = {13, 17, 17.5f, 24};
  FNT_RasterFlags flags[] = {0, FNT_RasterFlag_Smooth, FNT_RasterFlag_Hinted,
                             FNT_RasterFlag_TightBounds,
                             FNT_RasterFlag_TightBounds|FNT_RasterFlag_Smooth,
                             FNT_RasterFlag_TightBounds|FNT_RasterFlag_Hinted};
  String8 samples[] = {str8_lit("/M"), str8_lit("/MMMMMMMMMMMMMMMM"),
                      str8_lit("/iiiiiiiiiiiiiiii"), str8_lit("/Users/robert/")};
  U32 failures = 0;
  U32 cases = 0;
  for(U64 font_idx = 0; font_idx < ArrayCount(fonts); font_idx += 1)
  {
    FNT_Tag font = fnt_tag_from_path(fonts[font_idx]);
    for(U64 size_idx = 0; size_idx < ArrayCount(sizes); size_idx += 1)
    for(U64 flags_idx = 0; flags_idx < ArrayCount(flags); flags_idx += 1)
    for(U64 sample_idx = 0; sample_idx < ArrayCount(samples); sample_idx += 1)
    for(U32 scale = 1; scale <= 2; scale += 1)
    {
      F32 size = sizes[size_idx];
      String8 string = samples[sample_idx];
      dr_set_raster_scale((F32)scale);
      FNT_Run drawn = dr_fnt_run_from_string(font, size, 0, 0, flags[flags_idx], string);
      B32 pass = (drawn.dim.x > 0 && drawn.pieces.count > 0);
      F32 drawn_x = 0;
      U64 offset = 0;
      for(U64 piece_idx = 0; piece_idx <= drawn.pieces.count; piece_idx += 1)
      {
        F32 measured_x = fnt_dim_from_tag_size_string(font, size, 0, 0, str8_prefix(string, offset)).x;
        U64 hit = fnt_char_pos_from_tag_size_string_p(font, size, 0, 0, string, drawn_x);
        pass = pass && abs_f32(measured_x - drawn_x) < 0.01f && hit == offset;
        if(piece_idx < drawn.pieces.count)
        {
          drawn_x += drawn.pieces.v[piece_idx].advance;
          offset += drawn.pieces.v[piece_idx].decode_size;
        }
      }
      pass = pass && offset == string.size;
      cases += 1;
      failures += !pass;
      if(!pass)
      {
        F32 measured_x = fnt_dim_from_tag_size_string(font, size, 0, 0, string).x;
        fprintf(stderr, "FAIL font=%.*s size=%.1f flags=%u scale=%u text=%.*s measured=%.3f drawn=%.3f\n",
                str8_varg(fonts[font_idx]), size, flags[flags_idx], scale,
                str8_varg(string), measured_x, drawn.dim.x);
      }
    }
  }
  printf("Text positions: %u cases, %u failures\n", cases, failures);
  U32 position_failures = failures;
  cases = failures = 0;
  // The flat bottom of H sits on the baseline in these fonts. Inspect the
  // actual raster coverage, then apply the placement returned to drawing.
  Arena *arena = arena_alloc();
  for(U64 font_idx = 0; font_idx < ArrayCount(fonts); font_idx += 1)
  {
    FP_Handle handle = fp_font_open(fonts[font_idx]);
    FNT_Tag font = fnt_tag_from_path(fonts[font_idx]);
    for(U64 size_idx = 0; size_idx < ArrayCount(sizes); size_idx += 1)
    for(U64 flags_idx = 0; flags_idx < ArrayCount(flags); flags_idx += 1)
    for(U32 scale = 1; scale <= 2; scale += 1)
    {
      Temp temp = temp_begin(arena);
      F32 size = sizes[size_idx];
      FP_RasterFlags fp_flags = 0;
      if(flags[flags_idx] & FNT_RasterFlag_Smooth) { fp_flags |= FP_RasterFlag_Smooth; }
      if(flags[flags_idx] & FNT_RasterFlag_Hinted) { fp_flags |= FP_RasterFlag_Hinted; }
      if(flags[flags_idx] & FNT_RasterFlag_TightBounds) { fp_flags |= FP_RasterFlag_TightBounds; }
      FP_RasterResult raster = fp_raster(arena, handle, floor_f32(size)*scale, fp_flags, str8_lit("H"));
      dr_set_raster_scale((F32)scale);
      FNT_Run drawn = dr_fnt_run_from_string(font, size, 0, 0, flags[flags_idx], str8_lit("H"));
      // CoreText returns four-byte RGBA pixels; byte 3 holds alpha/coverage.
      S32 bottom = -1;
      for(S32 y = 0; y < raster.atlas_dim.y; y += 1)
      for(S32 x = 0; x < raster.atlas_dim.x; x += 1)
      {
        U8 alpha = ((U8 *)raster.atlas)[4*(y*raster.atlas_dim.x+x)+3];
        if(alpha >= 128) { bottom = Max(bottom, y); }
      }
      F32 baseline_error = 0;
      B32 pass = (bottom >= 0 && drawn.pieces.count == 1);
      if(pass)
      {
        // +1 converts the last covered row index to its lower pixel edge.
        baseline_error = drawn.pieces.v[0].offset.y*scale + bottom + 1;
      }
      pass = pass && abs_f32(baseline_error) <= 0.5f;
      cases += 1;
      failures += !pass;
      if(!pass)
      {
        fprintf(stderr, "FAIL baseline font=%.*s size=%.1f flags=%u scale=%u error=%.3f raster pixels\n",
                str8_varg(fonts[font_idx]), size, flags[flags_idx], scale, baseline_error);
      }
      temp_end(temp);
    }
    fp_font_close(handle);
  }
  // The sidebar overview button uses the Machine glyph (M). Its raster must
  // fit the compact button when positioned using the shared UI baseline rule.
  FNT_Tag icon_font = fnt_tag_from_path(str8_lit("data/icons.ttf"));
  FP_Handle icon_handle = fp_font_open(str8_lit("data/icons.ttf"));
  U32 icon_failures = 0, icon_cases = 0;
  if(fnt_tag_match(icon_font, fnt_tag_zero()) || fp_handle_match(icon_handle, fp_handle_zero()))
  {
    fprintf(stderr, "FAIL could not open data/icons.ttf\n");
    exit(1);
  }
  for(U32 size = 13; size <= 25; size += 4)
  for(U32 scale = 1; scale <= 2; scale++)
  {
    Temp temp = temp_begin(arena);
    FP_RasterResult raster = fp_raster(arena, icon_handle, (F32)(size*scale), FP_RasterFlag_Smooth, str8_lit("M"));
    FNT_Run run = fnt_run_from_string_scaled(icon_font, (F32)size, (F32)scale, 0, 0, FNT_RasterFlag_Smooth, str8_lit("M"));
    FNT_Metrics metrics = fnt_metrics_from_tag_size(icon_font, (F32)size);
    F32 height = ceil_f32(size*1.6f);
    F32 baseline = floor_f32(height/2.f + metrics.ascent/2.f - metrics.descent/2.f);
    S32 top = raster.atlas_dim.y, bottom = -1;
    for(S32 y = 0; y < raster.atlas_dim.y; y++)
    for(S32 x = 0; x < raster.atlas_dim.x; x++)
    {
      if(((U8 *)raster.atlas)[4*(y*raster.atlas_dim.x+x)+3] >= 128)
      { top = Min(top, y); bottom = Max(bottom, y); }
    }
    B32 pass = bottom >= top && run.pieces.count == 1;
    F32 ink_top = pass ? baseline + run.pieces.v[0].offset.y + top/(F32)scale : -1;
    F32 ink_bottom = pass ? baseline + run.pieces.v[0].offset.y + (bottom+1)/(F32)scale : height+1;
    pass = pass && ink_top >= 1.f && ink_bottom <= height-1.f;
    icon_cases += 1;
    icon_failures += !pass;
    if(!pass) fprintf(stderr, "FAIL workspace icon size=%u scale=%u ink=%.2f..%.2f button=1..%.2f\n", size, scale, ink_top, ink_bottom, height-1.f);
    temp_end(temp);
  }
  fp_font_close(icon_handle);
  printf("Workspace icon bounds: %u cases, %u failures\n", icon_cases, icon_failures);
  dr_set_raster_scale(1.f);
  arena_release(arena);
  printf("Raster baselines: %u cases, %u failures\n", cases, failures);
  exit(position_failures != 0 || failures != 0 || icon_failures != 0);
}
