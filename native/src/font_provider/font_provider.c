// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ rjf: Basic Type Functions

internal FP_Handle
fp_handle_zero(void)
{
  FP_Handle result = {0};
  return result;
}

internal B32
fp_handle_match(FP_Handle a, FP_Handle b)
{
  return (a.u64[0] == b.u64[0] && a.u64[1] == b.u64[1]);
}

////////////////////////////////
//~ Font Tables

internal U16 fp_be16(String8 s, U64 off) { return (off + 2 <= s.size) ? (U16)((s.str[off] << 8) | s.str[off+1]) : 0; }
internal U32 fp_be32(String8 s, U64 off) { return (off + 4 <= s.size) ? ((U32)fp_be16(s, off) << 16) | fp_be16(s, off+2) : 0; }

internal U32
fp_tag_from_string(char *tag)
{
  return ((U32)(U8)tag[0] << 24) | ((U32)(U8)tag[1] << 16) | ((U32)(U8)tag[2] << 8) | (U32)(U8)tag[3];
}

// DirectWrite and CoreText take variations in user units; layout gives them
// normalized, after avar. Undo avar (its maps are monotonic, so piecewise
// linear the other way), then scale by fvar's range.
internal U64
fp_user_axis_values_from_normalized(String8 fvar, String8 avar, S16 *coords, U64 coord_count, FP_AxisValue *out, U64 out_cap)
{
  U64 axes_offset = fp_be16(fvar, 4);
  U64 axis_count = fp_be16(fvar, 8);
  U64 axis_size = fp_be16(fvar, 10);
  U64 count = Min(Min(axis_count, coord_count), out_cap);
  if(fp_be16(fvar, 0) != 1 || axis_size < 20) { count = 0; }
  U64 avar_off = 8;
  U64 avar_axes = (fp_be16(avar, 0) == 1 ? fp_be16(avar, 6) : 0);
  for EachIndex(idx, count)
  {
    U64 rec = axes_offset + idx*axis_size;
    F32 min = (F32)(S32)fp_be32(fvar, rec + 4)/65536.f;
    F32 def = (F32)(S32)fp_be32(fvar, rec + 8)/65536.f;
    F32 max = (F32)(S32)fp_be32(fvar, rec + 12)/65536.f;
    F32 v = (F32)coords[idx]/16384.f;
    if(idx < avar_axes)
    {
      U64 pairs = fp_be16(avar, avar_off);
      F32 from_prev = 0, to_prev = 0;
      for EachIndex(p, pairs)
      {
        F32 from = (F32)(S16)fp_be16(avar, avar_off + 2 + p*4)/16384.f;
        F32 to = (F32)(S16)fp_be16(avar, avar_off + 4 + p*4)/16384.f;
        if(p > 0 && v <= to && to > to_prev)
        {
          v = from_prev + (v - to_prev)*(from - from_prev)/(to - to_prev);
          break;
        }
        if(v <= to) { v = from; break; }
        from_prev = from;
        to_prev = to;
      }
      avar_off += 2 + pairs*4;
    }
    out[idx].tag = fp_be32(fvar, rec);
    out[idx].value = (v < 0 ? def + v*(def - min) : def + v*(max - def));
  }
  return count;
}

// Whether COLR (v0 records) draws this glyph as coloured layers.
internal B32
fp_colr_glyph_has_layers(String8 colr, U32 glyph_id)
{
  U64 count = fp_be16(colr, 2);
  U64 records = fp_be32(colr, 4);
  B32 result = 0;
  for(U64 lo = 0, hi = count; lo < hi && !result;)
  {
    U64 mid = (lo + hi)/2;
    U32 gid = fp_be16(colr, records + mid*6);
    if(gid == glyph_id) { result = (fp_be16(colr, records + mid*6 + 4) > 0); break; }
    if(gid < glyph_id) { lo = mid + 1; } else { hi = mid; }
  }
  return result;
}

// Bilinear, for colour strikes drawn at another size. Premultiplied pixels
// (and coverage) interpolate directly.
internal FP_GlyphRaster
fp_glyph_raster_scaled(Arena *arena, FP_GlyphRaster src, F32 scale)
{
  FP_GlyphRaster result = src;
  U64 bpp = (src.kind == FP_RasterKind_RGBA ? 4 : 1);
  Vec2S16 dim = v2s16((S16)ClampBot(1, (S32)ceil_f32(src.dim.x*scale)), (S16)ClampBot(1, (S32)ceil_f32(src.dim.y*scale)));
  U8 *pixels = push_array(arena, U8, (U64)dim.x*(U64)dim.y*bpp);
  for(S32 y = 0; y < dim.y; y += 1)
  {
    F32 sy = Clamp(0.f, ((F32)y + 0.5f)/scale - 0.5f, (F32)(src.dim.y - 1));
    S32 y0 = (S32)sy, y1 = Min(y0 + 1, src.dim.y - 1);
    F32 ty = sy - (F32)y0;
    for(S32 x = 0; x < dim.x; x += 1)
    {
      F32 sx = Clamp(0.f, ((F32)x + 0.5f)/scale - 0.5f, (F32)(src.dim.x - 1));
      S32 x0 = (S32)sx, x1 = Min(x0 + 1, src.dim.x - 1);
      F32 tx = sx - (F32)x0;
      for EachIndex(c, bpp)
      {
        F32 a = src.pixels[((U64)y0*src.dim.x + x0)*bpp + c], b = src.pixels[((U64)y0*src.dim.x + x1)*bpp + c];
        F32 d = src.pixels[((U64)y1*src.dim.x + x0)*bpp + c], e = src.pixels[((U64)y1*src.dim.x + x1)*bpp + c];
        F32 v = (a*(1-tx) + b*tx)*(1-ty) + (d*(1-tx) + e*tx)*ty;
        pixels[((U64)y*dim.x + x)*bpp + c] = (U8)Clamp(0, (S32)round_f32(v), 255);
      }
    }
  }
  result.dim = dim;
  result.pixels = pixels;
  result.left = (S16)round_f32(src.left*scale);
  result.top = (S16)round_f32(src.top*scale);
  return result;
}

// The PNG CBDT holds for a glyph, from the strike nearest above size_px (or
// the largest): CoreText doesn't draw CBDT. left and top are in the strike's
// pixels, as FP_GlyphRaster's are.
internal B32
fp_cbdt_png(String8 cblc, String8 cbdt, U32 glyph_id, F32 size_px, String8 *png_out, S16 *left_out, S16 *top_out, U32 *ppem_out)
{
  B32 result = 0;
  U64 sizes = fp_be32(cblc, 4);
  U64 best = max_U64;
  U32 best_ppem = 0;
  for EachIndex(idx, sizes)
  {
    U64 rec = 8 + idx*48;
    U32 ppem = cblc.size > rec + 45 ? cblc.str[rec + 45] : 0;
    if(glyph_id < fp_be16(cblc, rec + 40) || glyph_id > fp_be16(cblc, rec + 42) || ppem == 0) { continue; }
    B32 better = (best == max_U64 ||
                  (ppem >= size_px && (best_ppem < size_px || ppem < best_ppem)) ||
                  (ppem < size_px && best_ppem < size_px && ppem > best_ppem));
    if(better) { best = rec; best_ppem = ppem; }
  }
  if(best != max_U64)
  {
    U64 array = fp_be32(cblc, best);
    U64 count = fp_be32(cblc, best + 8);
    for EachIndex(idx, count)
    {
      U64 entry = array + idx*8;
      U32 first = fp_be16(cblc, entry), last = fp_be16(cblc, entry + 2);
      if(glyph_id < first || glyph_id > last) { continue; }
      U64 sub = array + fp_be32(cblc, entry + 4);
      U32 index_format = fp_be16(cblc, sub);
      U32 image_format = fp_be16(cblc, sub + 2);
      U64 image_base = fp_be32(cblc, sub + 4);
      U64 offset = 0, end = 0;
      S16 left = 0, top = 0;
      U64 n = glyph_id - first;
      switch(index_format)
      {
        case 1: { offset = fp_be32(cblc, sub + 8 + n*4); end = fp_be32(cblc, sub + 12 + n*4); }break;
        case 3: { offset = fp_be16(cblc, sub + 8 + n*2); end = fp_be16(cblc, sub + 10 + n*2); }break;
        case 4:
        {
          U64 glyphs = fp_be32(cblc, sub + 8);
          for EachIndex(g, glyphs)
          {
            if(fp_be16(cblc, sub + 12 + g*4) == glyph_id)
            {
              offset = fp_be16(cblc, sub + 14 + g*4);
              end = fp_be16(cblc, sub + 18 + g*4);
              break;
            }
          }
        }break;
        case 2:
        {
          U64 image_size = fp_be32(cblc, sub + 8);
          offset = n*image_size;
          end = offset + image_size;
          left = (S8)cblc.str[Min(cblc.size - 1, sub + 14)];
          top = (S8)cblc.str[Min(cblc.size - 1, sub + 15)];
        }break;
      }
      U64 at = image_base + offset;
      U64 data_len = 0, data_at = 0;
      if(end > offset && image_base + end <= cbdt.size)
      {
        switch(image_format)
        {
          case 17: { left = (S8)cbdt.str[at + 2]; top = (S8)cbdt.str[at + 3]; data_len = fp_be32(cbdt, at + 5); data_at = at + 9; }break;
          case 18: { left = (S8)cbdt.str[at + 2]; top = (S8)cbdt.str[at + 3]; data_len = fp_be32(cbdt, at + 8); data_at = at + 12; }break;
          case 19: { data_len = fp_be32(cbdt, at); data_at = at + 4; }break;
        }
      }
      if(data_len != 0 && data_at + data_len <= cbdt.size)
      {
        *png_out = str8(cbdt.str + data_at, data_len);
        *left_out = left;
        *top_out = top;
        *ppem_out = best_ppem;
        result = 1;
      }
      break;
    }
  }
  return result;
}
