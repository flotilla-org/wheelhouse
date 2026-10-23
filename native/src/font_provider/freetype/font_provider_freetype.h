// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef FONT_PROVIDER_FREETYPE_H
#define FONT_PROVIDER_FREETYPE_H

////////////////////////////////
//~ rjf: Freetype Includes

#undef internal
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_GLYPH_H
#define internal static

////////////////////////////////
//~ rjf: State Types

typedef struct FP_FT_Font FP_FT_Font;
struct FP_FT_Font
{
  FT_Face face;
};

typedef struct FP_FT_State FP_FT_State;
struct FP_FT_State
{
  Arena *arena;
  FT_Library library;
  B32 system_color_emoji_fonts_resolved;
  FP_SystemFontArray system_color_emoji_fonts;
};

////////////////////////////////
//~ rjf: Globals

global FP_FT_State *fp_ft_state = 0;

////////////////////////////////
//~ rjf: Helpers

internal FP_FT_Font fp_ft_font_from_handle(FP_Handle handle);
internal FP_Handle fp_ft_handle_from_font(FP_FT_Font font);
internal String8 fp_ft_fontconfig_color_font_path_from_family(Arena *arena, String8 family);

#endif // FONT_PROVIDER_FREETYPE_H
