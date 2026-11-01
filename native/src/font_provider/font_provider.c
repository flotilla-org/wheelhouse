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
