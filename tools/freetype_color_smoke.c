// Standalone smoke test for the FreeType color bitmap path used by UIShell.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ft2build.h>
#include FT_FREETYPE_H

static double
select_size(FT_Face face, unsigned int pixel_size)
{
  double result = 1.0;
  if(face->num_fixed_sizes > 0 && face->available_sizes != 0)
  {
    FT_Int best_idx = 0;
    long best_delta = 0x7fffffff;
    long requested_26_6 = (long)pixel_size << 6;
    for(FT_Int idx = 0; idx < face->num_fixed_sizes; idx += 1)
    {
      FT_Bitmap_Size *strike = &face->available_sizes[idx];
      long strike_26_6 = (long)(strike->y_ppem ? strike->y_ppem : ((long)strike->height << 6));
      long delta = strike_26_6 - requested_26_6;
      if(delta < 0)
      {
        delta = -delta;
      }
      if(delta < best_delta)
      {
        best_delta = delta;
        best_idx = idx;
      }
    }
    FT_Select_Size(face, best_idx);
    FT_Bitmap_Size *strike = &face->available_sizes[best_idx];
    long strike_26_6 = (long)(strike->y_ppem ? strike->y_ppem : ((long)strike->height << 6));
    if(strike_26_6 != 0)
    {
      result = (double)requested_26_6/(double)strike_26_6;
    }
  }
  else
  {
    FT_Set_Pixel_Sizes(face, 0, pixel_size);
  }
  return result;
}

int
main(int argc, char **argv)
{
  char const *font_path = (argc > 1 ? argv[1] : "data/NotoColorEmoji.ttf");
  unsigned long codepoint = (argc > 2 ? strtoul(argv[2], 0, 0) : 0x1f642ul);
  int expect_scalable = 0;
  int expect_fixed = 0;
  int expect_no_bgra = 0;
  int expect_load_fail = 0;
  for(int arg_idx = 3; arg_idx < argc; arg_idx += 1)
  {
    if(strcmp(argv[arg_idx], "--expect-scalable") == 0)
    {
      expect_scalable = 1;
    }
    else if(strcmp(argv[arg_idx], "--expect-fixed") == 0)
    {
      expect_fixed = 1;
    }
    else if(strcmp(argv[arg_idx], "--expect-no-bgra") == 0)
    {
      expect_no_bgra = 1;
    }
    else if(strcmp(argv[arg_idx], "--expect-load-fail") == 0)
    {
      expect_load_fail = 1;
    }
  }

  FT_Library library = 0;
  FT_Face face = 0;
  int result = 1;
  if(FT_Init_FreeType(&library) != 0)
  {
    fprintf(stderr, "freetype smoke failed: FT_Init_FreeType\n");
  }
  else if(FT_New_Face(library, font_path, 0, &face) != 0)
  {
    fprintf(stderr, "freetype smoke failed: FT_New_Face %s\n", font_path);
  }
  else
  {
    double fixed_size_scale = select_size(face, 32);
    int is_fixed = (face->num_fixed_sizes > 0);
    FT_UInt glyph_index = FT_Get_Char_Index(face, codepoint);
    if(expect_scalable && is_fixed)
    {
      fprintf(stderr, "freetype smoke failed: %s is fixed-strike, expected scalable color font\n", font_path);
    }
    else if(expect_fixed && !is_fixed)
    {
      fprintf(stderr, "freetype smoke failed: %s is scalable, expected fixed-strike color font\n", font_path);
    }
    else if(glyph_index == 0)
    {
      fprintf(stderr, "freetype smoke failed: missing glyph U+%04lx\n", codepoint);
    }
    else
    {
      FT_Error load_error = FT_Load_Glyph(face, glyph_index, FT_LOAD_COLOR|FT_LOAD_RENDER);
      if(expect_load_fail)
      {
        if(load_error == 0)
        {
          fprintf(stderr, "freetype smoke failed: U+%04lx loaded successfully, expected current FreeType path not to render this color format\n", codepoint);
        }
        else
        {
          printf("freetype smoke: U+%04lx load failed as expected with FreeType error 0x%x\n", codepoint, load_error);
          result = 0;
        }
      }
      else if(load_error != 0)
      {
        fprintf(stderr, "freetype smoke failed: FT_Load_Glyph U+%04lx error=0x%x\n", codepoint, load_error);
      }
      else if(expect_no_bgra)
      {
        if(face->glyph->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA)
        {
          fprintf(stderr,
                  "freetype smoke failed: U+%04lx rendered as BGRA, expected current FreeType path not to expose source-color output\n",
                  codepoint);
        }
        else
        {
          printf("freetype smoke: U+%04lx rendered as non-BGRA pixel_mode=%u %ux%u from %s font\n",
                 codepoint,
                 face->glyph->bitmap.pixel_mode,
                 face->glyph->bitmap.width,
                 face->glyph->bitmap.rows,
                 is_fixed ? "fixed-strike" : "scalable");
          result = 0;
        }
      }
      else if(face->glyph->bitmap.pixel_mode != FT_PIXEL_MODE_BGRA)
      {
        fprintf(stderr,
                "freetype smoke failed: U+%04lx pixel_mode=%u expected FT_PIXEL_MODE_BGRA=%u\n",
                codepoint,
                face->glyph->bitmap.pixel_mode,
                FT_PIXEL_MODE_BGRA);
      }
      else if(face->glyph->bitmap.width == 0 || face->glyph->bitmap.rows == 0)
      {
        fprintf(stderr, "freetype smoke failed: U+%04lx produced an empty bitmap\n", codepoint);
      }
      else
      {
        unsigned int scaled_width = (unsigned int)((double)face->glyph->bitmap.width*fixed_size_scale + 0.999999);
        unsigned int scaled_height = (unsigned int)((double)face->glyph->bitmap.rows*fixed_size_scale + 0.999999);
        if(scaled_width == 0 || scaled_height == 0)
        {
          fprintf(stderr, "freetype smoke failed: U+%04lx scaled to an empty bitmap\n", codepoint);
        }
        else
        {
          printf("freetype smoke: U+%04lx rendered as BGRA %ux%u from %s font, provider scale %.3f -> %ux%u\n",
                 codepoint,
                 face->glyph->bitmap.width,
                 face->glyph->bitmap.rows,
                 is_fixed ? "fixed-strike" : "scalable",
                 fixed_size_scale,
                 scaled_width,
                 scaled_height);
          result = 0;
        }
      }
    }
  }

  if(face != 0)
  {
    FT_Done_Face(face);
  }
  if(library != 0)
  {
    FT_Done_FreeType(library);
  }
  return result;
}
