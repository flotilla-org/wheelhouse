// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal MAC_FP_Font *
mac_fp_font_from_handle(FP_Handle handle)
{
  MAC_FP_Font *result = (MAC_FP_Font *)handle.u64[0];
  return result;
}

internal FP_Handle
mac_fp_handle_from_font(MAC_FP_Font *font)
{
  FP_Handle result = {(U64)font};
  return result;
}

internal MAC_FP_Font *
mac_fp_font_alloc(void)
{
  MAC_FP_Font *font = mac_fp_state->free_font;
  if(font != 0)
  {
    SLLStackPop(mac_fp_state->free_font);
    MemoryZeroStruct(font);
  }
  else
  {
    font = push_array(mac_fp_state->arena, MAC_FP_Font, 1);
  }
  return font;
}

internal void
mac_fp_font_release(MAC_FP_Font *font)
{
  if(font != 0)
  {
    if(font->cg_font != 0)
    {
      CGFontRelease(font->cg_font);
    }
    if(font->provider != 0)
    {
      CGDataProviderRelease(font->provider);
    }
    MemoryZeroStruct(font);
    SLLStackPush(mac_fp_state->free_font, font);
  }
}

internal U32
mac_fp_table_tag_from_bytes(char a, char b, char c, char d)
{
  U32 result = ((U32)(U8)a << 24) | ((U32)(U8)b << 16) | ((U32)(U8)c << 8) | (U32)(U8)d;
  return result;
}

internal B32
mac_fp_cg_font_has_color_tables(CGFontRef cg_font)
{
  B32 result = 0;
  if(cg_font != 0)
  {
    U32 color_tags[] =
    {
      mac_fp_table_tag_from_bytes('s', 'b', 'i', 'x'),
      mac_fp_table_tag_from_bytes('C', 'O', 'L', 'R'),
      mac_fp_table_tag_from_bytes('C', 'P', 'A', 'L'),
      mac_fp_table_tag_from_bytes('C', 'B', 'D', 'T'),
      mac_fp_table_tag_from_bytes('C', 'B', 'L', 'C'),
      mac_fp_table_tag_from_bytes('S', 'V', 'G', ' '),
    };
    CFArrayRef tags = CGFontCopyTableTags(cg_font);
    if(tags != 0)
    {
      CFIndex tag_count = CFArrayGetCount(tags);
      for(CFIndex idx = 0; idx < tag_count && !result; idx += 1)
      {
        U32 tag = (U32)(uintptr_t)CFArrayGetValueAtIndex(tags, idx);
        for(U64 color_tag_idx = 0; color_tag_idx < ArrayCount(color_tags); color_tag_idx += 1)
        {
          if(tag == color_tags[color_tag_idx])
          {
            result = 1;
            break;
          }
        }
      }
      CFRelease(tags);
    }
  }
  return result;
}

internal U8
mac_fp_unpremultiply_u8(U8 c, U8 a)
{
  U8 result = 0;
  if(a != 0)
  {
    result = (U8)Clamp(0, ((S32)c*255 + (S32)a/2)/(S32)a, 255);
  }
  return result;
}

// Asks CoreText for a font whose family is exactly `family` and returns the
// path of its file, or empty if this host does not have that family.
internal String8
mac_fp_system_font_path_from_family(Arena *arena, String8 family)
{
  String8 result = {0};
  CFStringRef family_cf = CFStringCreateWithBytes(0, family.str, (CFIndex)family.size, kCFStringEncodingUTF8, 0);
  if(family_cf != 0)
  {
    CFTypeRef keys[] = {kCTFontFamilyNameAttribute};
    CFTypeRef values[] = {family_cf};
    CFDictionaryRef attributes = CFDictionaryCreate(0, (const void **)keys, (const void **)values, 1, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFSetRef mandatory = CFSetCreate(0, (const void **)keys, 1, &kCFTypeSetCallBacks);
    CTFontDescriptorRef descriptor = (attributes != 0) ? CTFontDescriptorCreateWithAttributes(attributes) : 0;
    CTFontDescriptorRef match = (descriptor != 0) ? CTFontDescriptorCreateMatchingFontDescriptor(descriptor, mandatory) : 0;
    CFURLRef url = (match != 0) ? (CFURLRef)CTFontDescriptorCopyAttribute(match, kCTFontURLAttribute) : 0;
    if(url != 0)
    {
      U8 path[4096] = {0};
      if(CFURLGetFileSystemRepresentation(url, 1, path, sizeof(path)))
      {
        result = push_str8_copy(arena, str8_cstring((char *)path));
      }
      CFRelease(url);
    }
    if(match != 0)      { CFRelease(match); }
    if(descriptor != 0) { CFRelease(descriptor); }
    if(mandatory != 0)  { CFRelease(mandatory); }
    if(attributes != 0) { CFRelease(attributes); }
    CFRelease(family_cf);
  }
  return result;
}

fp_hook void
fp_init(void)
{
  Arena *arena = arena_alloc();
  mac_fp_state = push_array(arena, MAC_FP_State, 1);
  mac_fp_state->arena = arena;
}

fp_hook FP_Handle
fp_font_open(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  String8 path_copy = push_str8_copy(scratch.arena, path);
  MAC_FP_Font *font = mac_fp_font_alloc();
  font->provider = CGDataProviderCreateWithFilename((char *)path_copy.str);
  if(font->provider != 0)
  {
    font->cg_font = CGFontCreateWithDataProvider(font->provider);
    font->has_color_tables = mac_fp_cg_font_has_color_tables(font->cg_font);
  }
  if(font->cg_font == 0)
  {
    mac_fp_font_release(font);
    font = 0;
  }
  FP_Handle result = mac_fp_handle_from_font(font);
  scratch_end(scratch);
  return result;
}

fp_hook FP_Handle
fp_font_open_from_static_data_string(String8 *data_ptr)
{
  MAC_FP_Font *font = mac_fp_font_alloc();
  if(data_ptr != 0 && data_ptr->str != 0 && data_ptr->size != 0)
  {
    font->provider = CGDataProviderCreateWithData(0, data_ptr->str, data_ptr->size, 0);
    if(font->provider != 0)
    {
      font->cg_font = CGFontCreateWithDataProvider(font->provider);
      font->has_color_tables = mac_fp_cg_font_has_color_tables(font->cg_font);
    }
  }
  if(font->cg_font == 0)
  {
    mac_fp_font_release(font);
    font = 0;
  }
  FP_Handle result = mac_fp_handle_from_font(font);
  return result;
}

fp_hook void
fp_font_close(FP_Handle handle)
{
  MAC_FP_Font *font = mac_fp_font_from_handle(handle);
  mac_fp_font_release(font);
}

fp_hook FP_Metrics
fp_metrics_from_font(FP_Handle handle)
{
  MAC_FP_Font *font = mac_fp_font_from_handle(handle);
  FP_Metrics result = {0};
  if(font != 0 && font->cg_font != 0)
  {
    result.design_units_per_em = (F32)CGFontGetUnitsPerEm(font->cg_font);
    result.ascent              = (F32)CGFontGetAscent(font->cg_font);
    result.descent             = abs_f32((F32)CGFontGetDescent(font->cg_font));
    result.line_gap            = (F32)CGFontGetLeading(font->cg_font);
    result.capital_height      = (F32)CGFontGetCapHeight(font->cg_font);
  }
  return result;
}

fp_hook B32
fp_font_has_codepoint(FP_Handle handle, U32 codepoint)
{
  MAC_FP_Font *font = mac_fp_font_from_handle(handle);
  B32 result = 0;
  if(font != 0 && font->cg_font != 0)
  {
    CTFontRef ct_font = CTFontCreateWithGraphicsFont(font->cg_font, 12.f, 0, 0);
    if(ct_font != 0)
    {
      U16 buffer[2] = {0};
      U16 encoded_size = utf16_encode(buffer, codepoint);
      if(encoded_size != 0)
      {
        CGGlyph glyphs[2] = {0};
        result = CTFontGetGlyphsForCharacters(ct_font, (UniChar *)buffer, glyphs, encoded_size);
      }
      CFRelease(ct_font);
    }
  }
  return result;
}

fp_hook ASAN_NO_ADDR FP_RasterResult
fp_raster(Arena *arena, FP_Handle handle, F32 size, FP_RasterFlags flags, String8 string)
{
  ProfBeginFunction();
  Temp scratch = scratch_begin(&arena, 1);
  MAC_FP_Font *font = mac_fp_font_from_handle(handle);
  FP_RasterResult result = {0};
  if(font != 0 && font->cg_font != 0)
  {
    F32 pixel_size = (96.f/72.f) * size;
    CTFontRef ct_font = CTFontCreateWithGraphicsFont(font->cg_font, pixel_size, 0, 0);
    if(ct_font != 0)
    {
      B32 tight_bounds = !!(flags & FP_RasterFlag_TightBounds);
      CFStringRef cf_string = CFStringCreateWithBytes(kCFAllocatorDefault, string.str, (CFIndex)string.size, kCFStringEncodingUTF8, false);
      if(cf_string != 0)
      {
        CGColorSpaceRef attr_color_space = CGColorSpaceCreateDeviceRGB();
        CGFloat white_components[] = {1.f, 1.f, 1.f, 1.f};
        CGColorRef white_color = (attr_color_space != 0 ? CGColorCreate(attr_color_space, white_components) : 0);
        CFTypeRef keys[] = {kCTFontAttributeName, kCTForegroundColorAttributeName};
        CFTypeRef values[] = {ct_font, white_color};
        U64 attr_count = (white_color != 0 ? ArrayCount(keys) : 1);
        CFDictionaryRef attrs = CFDictionaryCreate(kCFAllocatorDefault, keys, values, attr_count, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        if(attrs != 0)
        {
          CFAttributedStringRef attr_string = CFAttributedStringCreate(kCFAllocatorDefault, cf_string, attrs);
          if(attr_string != 0)
          {
            CTLineRef line = CTLineCreateWithAttributedString(attr_string);
            if(line != 0)
            {
              CGFloat advance = CTLineGetTypographicBounds(line, 0, 0, 0);
              CGFloat ascent = ceil_f64(CTFontGetAscent(ct_font));
              CGFloat descent = ceil_f64(CTFontGetDescent(ct_font));
              CGFloat leading = ceil_f64(CTFontGetLeading(ct_font));
              CGFloat baseline_from_bottom = ceil_f64(descent + leading);
              CGFloat origin_from_left = (tight_bounds ? ceil_f64(pixel_size) : 0);
              Vec2S16 atlas_dim =
              {
                (S16)ceil_f64(Max(advance + origin_from_left*2.0 + 2.0, 1.0)),
                (S16)ceil_f64(Max(ascent + descent + leading + 2.0, 1.0)),
              };
              if(atlas_dim.x > 0 && atlas_dim.y > 0)
              {
                U64 atlas_size = (U64)atlas_dim.x * (U64)atlas_dim.y * 4;
                U8 *atlas = push_array(arena, U8, atlas_size);
                CGColorSpaceRef color_space = CGColorSpaceCreateDeviceRGB();
                CGContextRef ctx = CGBitmapContextCreate(atlas, atlas_dim.x, atlas_dim.y, 8,
                                                         (size_t)atlas_dim.x*4, color_space,
                                                         kCGImageAlphaPremultipliedLast|kCGBitmapByteOrder32Big);
                if(ctx != 0)
                {
                  B32 smooth = !!(flags & FP_RasterFlag_Smooth);
                  B32 hinted = !!(flags & FP_RasterFlag_Hinted);
                  CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);
                  CGContextSetTextPosition(ctx, origin_from_left, baseline_from_bottom);
                  CGContextSetRGBFillColor(ctx, 1.f, 1.f, 1.f, 1.f);
                  CGContextSetAllowsAntialiasing(ctx, 1);
                  CGContextSetShouldAntialias(ctx, 1);
                  CGContextSetAllowsFontSmoothing(ctx, 1);
                  CGContextSetShouldSmoothFonts(ctx, smooth);
                  CGContextSetAllowsFontSubpixelPositioning(ctx, 1);
                  CGContextSetShouldSubpixelPositionFonts(ctx, !hinted);
                  CGContextSetAllowsFontSubpixelQuantization(ctx, 1);
                  CGContextSetShouldSubpixelQuantizeFonts(ctx, hinted);
                  CTLineDraw(line, ctx);

                  U64 alpha_sum = 0;
                  B32 has_source_color = 0;
                  S64 min_x = atlas_dim.x;
                  S64 min_y = atlas_dim.y;
                  S64 max_x = -1;
                  S64 max_y = -1;
                  for(S64 y = 0; y < atlas_dim.y; y += 1)
                  {
                    for(S64 x = 0; x < atlas_dim.x; x += 1)
                    {
                      U64 idx = ((U64)y*(U64)atlas_dim.x + (U64)x)*4;
                      U8 r = atlas[idx+0];
                      U8 g = atlas[idx+1];
                      U8 b = atlas[idx+2];
                      U8 a = atlas[idx+3];
                      alpha_sum += a;
                      if(a != 0)
                      {
                        min_x = Min(min_x, x);
                        min_y = Min(min_y, y);
                        max_x = Max(max_x, x);
                        max_y = Max(max_y, y);
                        U8 min_c = Min(r, Min(g, b));
                        U8 max_c = Max(r, Max(g, b));
                        if(max_c > min_c + 4)
                        {
                          has_source_color = 1;
                        }
                      }
                    }
                  }
                  if(alpha_sum != 0)
                  {
                    B32 is_source_color = (font->has_color_tables && has_source_color);
                    for(S64 y = 0; y < atlas_dim.y; y += 1)
                    {
                      for(S64 x = 0; x < atlas_dim.x; x += 1)
                      {
                        U8 *px = atlas + ((U64)y*(U64)atlas_dim.x + (U64)x)*4;
                        if(is_source_color)
                        {
                          px[0] = mac_fp_unpremultiply_u8(px[0], px[3]);
                          px[1] = mac_fp_unpremultiply_u8(px[1], px[3]);
                          px[2] = mac_fp_unpremultiply_u8(px[2], px[3]);
                        }
                        else
                        {
                          U8 coverage = Max(px[3], Max(px[0], Max(px[1], px[2])));
                          px[0] = 255;
                          px[1] = 255;
                          px[2] = 255;
                          px[3] = coverage;
                        }
                      }
                    }
                    F32 baseline_from_top = (F32)((CGFloat)atlas_dim.y - baseline_from_bottom);
                    if(tight_bounds && min_x <= max_x && min_y <= max_y)
                    {
                      Vec2S16 tight_dim =
                      {
                        (S16)(max_x - min_x + 1),
                        (S16)(max_y - min_y + 1),
                      };
                      U64 tight_size = (U64)tight_dim.x*(U64)tight_dim.y*4;
                      U8 *tight_atlas = push_array(arena, U8, tight_size);
                      for(S64 y = 0; y < tight_dim.y; y += 1)
                      {
                        U8 *src = atlas + (((U64)(min_y + y)*(U64)atlas_dim.x + (U64)min_x)*4);
                        U8 *dst = tight_atlas + ((U64)y*(U64)tight_dim.x*4);
                        MemoryCopy(dst, src, (U64)tight_dim.x*4);
                      }
                      result.atlas_dim = tight_dim;
                      result.atlas = tight_atlas;
                      result.origin_from_left = (F32)(origin_from_left - (CGFloat)min_x);
                      result.baseline_from_top = baseline_from_top - (F32)min_y;
                    }
                    else
                    {
                      result.atlas_dim = atlas_dim;
                      result.atlas = atlas;
                      result.origin_from_left = (F32)origin_from_left;
                      result.baseline_from_top = baseline_from_top;
                    }
                    result.kind = is_source_color ? FP_RasterKind_RGBA : FP_RasterKind_Mask;
                  }
                  result.advance = ceil_f64(advance);
                  result.face_box_origin_from_left = (F32)origin_from_left;
                  result.face_box_baseline_from_top = (F32)((CGFloat)atlas_dim.y - baseline_from_bottom);

                  CGContextRelease(ctx);
                }
                if(color_space != 0)
                {
                  CGColorSpaceRelease(color_space);
                }
              }
              CFRelease(line);
            }
            CFRelease(attr_string);
          }
          CFRelease(attrs);
        }
        if(white_color != 0)
        {
          CGColorRelease(white_color);
        }
        if(attr_color_space != 0)
        {
          CGColorSpaceRelease(attr_color_space);
        }
        CFRelease(cf_string);
      }
      CFRelease(ct_font);
    }
  }
  scratch_end(scratch);
  ProfEnd();
  return result;
}

fp_hook FP_SystemFontArray
fp_system_color_emoji_fonts(void)
{
  if(!mac_fp_state->system_color_emoji_fonts_resolved)
  {
    String8 families[] = {str8_lit_comp("Apple Color Emoji")};
    FP_SystemFontArray *fonts = &mac_fp_state->system_color_emoji_fonts;
    fonts->v = push_array(mac_fp_state->arena, FP_SystemFont, ArrayCount(families));
    fonts->count = ArrayCount(families);
    for EachElement(idx, families)
    {
      fonts->v[idx].family = families[idx];
      fonts->v[idx].path = mac_fp_system_font_path_from_family(mac_fp_state->arena, families[idx]);
    }
    mac_fp_state->system_color_emoji_fonts_resolved = 1;
  }
  return mac_fp_state->system_color_emoji_fonts;
}
