// The glyph cache (font_glyph_cache.c) over CPU pages, and the providers'
// shared font-table helpers (font_provider.c), for
// crates/wheelhouse-glyph-cache's tests. Its rasterizer draws a pattern the
// tests recompute: pixel (x, y) of glyph g at size s is
// (g*31 + x*7 + y*13 + s) % 255 + 1, in every channel for colour glyphs.
#define BUILD_CONSOLE_INTERFACE 1
#include "base/base_inc.h"
#include "font_cache/font_glyph_cache.h"
#include "font_provider/font_provider.h"
#include "base/base_inc.c"
#include "font_provider/font_provider.c"
#define XXH_IMPLEMENTATION
#define XXH_STATIC_LINKING_ONLY
#include "third_party/xxHash/xxhash.h"
#include "font_cache/font_glyph_cache.c"

// The base layer's own `main` is renamed out of the way (build.rs); its
// entry point is never run.
internal void entry_point(CmdLine *cmd_line) {}

#define GC_EXPORT __attribute__((visibility("default")))

typedef struct GC_Request GC_Request;
struct GC_Request
{
  U64 tag[2];
  U32 face_index;
  U32 glyph_id;
  F32 size_px;
  F32 subpixel_x;
  S16 *coords;
  U64 coord_count;
  U32 synthesis;
  F32 skew;
  U32 hinting;
  // What the rasterizer gives on a miss.
  U32 kind;
  S32 width;
  S32 height;
};

typedef struct GC_Result GC_Result;
struct GC_Result
{
  U32 ok;
  U32 kind;
  U64 page;
  S32 x0, y0, x1, y1;
  S32 left, top;
  U32 rasterized;
};

typedef struct GC_Page GC_Page;
struct GC_Page
{
  FNT_GlyphKind kind;
  Vec2S16 dim;
  U8 *pixels;
};

typedef struct GC_Harness GC_Harness;
struct GC_Harness
{
  Arena *arena;
  FNT_GlyphCache *cache;
  GC_Page pages[2*FNT_GLYPH_PAGE_MAX];
  U64 page_count;
  GC_Request *request;
  U32 rasterized;
};

internal void
gc_init(void)
{
  local_persist B32 system_ready = 0;
  if(!system_ready)
  {
#if OS_LINUX
    SystemInfo *info = &lnx_state.system_info;
#elif OS_MAC
    SystemInfo *info = &mac_state.system_info;
#endif
    info->logical_processor_count = 1;
    info->page_size = (U64)getpagesize();
    info->large_page_size = MB(2);
    info->allocation_granularity = info->page_size;
    system_ready = 1;
  }
  if(tctx_selected() == 0) { tctx_select(tctx_alloc()); }
}

internal FNT_GlyphRaster
gc_rasterize(void *user, Arena *arena, FNT_GlyphKey *key, FNT_GlyphParams *params)
{
  GC_Harness *h = (GC_Harness *)user;
  GC_Request *r = h->request;
  h->rasterized = 1;
  FNT_GlyphRaster raster = {0};
  raster.kind = r->kind;
  raster.dim = v2s16((S16)r->width, (S16)r->height);
  raster.left = (S16)(r->glyph_id % 3);
  raster.top = (S16)r->height;
  U64 bpp = (r->kind == FNT_GlyphKind_Color ? 4 : 1);
  if(r->width > 0 && r->height > 0)
  {
    raster.pixels = push_array(arena, U8, (U64)r->width*(U64)r->height*bpp);
    for(S32 y = 0; y < r->height; y += 1)
    {
      for(S32 x = 0; x < r->width; x += 1)
      {
        U8 v = (U8)((key->glyph_id*31 + x*7 + y*13 + (U32)key->size_64) % 255 + 1);
        for EachIndex(c, bpp) { raster.pixels[((U64)y*(U64)r->width + (U64)x)*bpp + c] = v; }
      }
    }
  }
  return raster;
}

internal U64
gc_page_alloc(void *user, FNT_GlyphKind kind, Vec2S16 dim)
{
  GC_Harness *h = (GC_Harness *)user;
  U64 result = 0;
  if(h->page_count < ArrayCount(h->pages))
  {
    GC_Page *page = &h->pages[h->page_count];
    page->kind = kind;
    page->dim = dim;
    page->pixels = push_array(h->arena, U8, (U64)dim.x*(U64)dim.y*(kind == FNT_GlyphKind_Color ? 4 : 1));
    h->page_count += 1;
    result = h->page_count;
  }
  return result;
}

internal void
gc_page_fill(void *user, U64 handle, FNT_GlyphKind kind, Rng2S16 rect, U8 *pixels)
{
  GC_Harness *h = (GC_Harness *)user;
  GC_Page *page = &h->pages[handle-1];
  U64 bpp = (kind == FNT_GlyphKind_Color ? 4 : 1);
  U64 w = (U64)(rect.x1 - rect.x0);
  for(S16 y = rect.y0; y < rect.y1; y += 1)
  {
    MemoryCopy(page->pixels + ((U64)y*(U64)page->dim.x + (U64)rect.x0)*bpp, pixels + (U64)(y - rect.y0)*w*bpp, w*bpp);
  }
}

internal U64
gc_collide(FNT_GlyphKey *key)
{
  return 42;
}

GC_EXPORT GC_Harness *
gc_new(S32 mask_dim, U32 mask_budget, S32 color_dim, U32 color_budget, F32 gamma, U32 collide)
{
  gc_init();
  Arena *arena = arena_alloc();
  GC_Harness *h = push_array(arena, GC_Harness, 1);
  h->arena = arena;
  FNT_GlyphCacheParams params = fnt_glyph_cache_default_params();
  params.page_dim[FNT_GlyphKind_Mask] = v2s16((S16)mask_dim, (S16)mask_dim);
  params.page_dim[FNT_GlyphKind_Color] = v2s16((S16)color_dim, (S16)color_dim);
  params.page_budget[FNT_GlyphKind_Mask] = mask_budget;
  params.page_budget[FNT_GlyphKind_Color] = color_budget;
  params.gamma = gamma;
  FNT_GlyphBackend backend = {h, gc_rasterize, gc_page_alloc, gc_page_fill};
  h->cache = fnt_glyph_cache_alloc(backend, &params);
  if(collide) { h->cache->hash_override = gc_collide; }
  return h;
}

GC_EXPORT void
gc_free(GC_Harness *h)
{
  gc_init();
  fnt_glyph_cache_release(h->cache);
  arena_release(h->arena);
}

GC_EXPORT void
gc_frame(GC_Harness *h)
{
  fnt_glyph_cache_frame(h->cache);
}

GC_EXPORT void
gc_set_gamma(GC_Harness *h, F32 gamma)
{
  gc_init();
  fnt_glyph_cache_set_gamma(h->cache, gamma);
}

GC_EXPORT void
gc_get(GC_Harness *h, GC_Request *r, GC_Result *out)
{
  gc_init();
  FNT_GlyphParams params = {0};
  params.tag.u64[0] = r->tag[0];
  params.tag.u64[1] = r->tag[1];
  params.tag.face_index = r->face_index;
  params.glyph_id = r->glyph_id;
  params.size_px = r->size_px;
  params.subpixel_x = r->subpixel_x;
  params.coords = r->coords;
  params.coord_count = r->coord_count;
  params.synthesis = r->synthesis;
  params.skew = r->skew;
  params.hinting = r->hinting;
  h->request = r;
  h->rasterized = 0;
  FNT_Glyph glyph = fnt_glyph_from_params(h->cache, &params);
  out->ok = glyph.ok;
  out->kind = glyph.kind;
  out->page = glyph.page;
  out->x0 = glyph.region.x0;
  out->y0 = glyph.region.y0;
  out->x1 = glyph.region.x1;
  out->y1 = glyph.region.y1;
  out->left = glyph.left;
  out->top = glyph.top;
  out->rasterized = h->rasterized;
}

// The page's pixels, row by row; the count of bytes, or 0 for no such page.
GC_EXPORT U64
gc_page(GC_Harness *h, U64 handle, U8 **pixels_out, S32 *dim_out, U32 *kind_out)
{
  U64 result = 0;
  if(handle >= 1 && handle <= h->page_count)
  {
    GC_Page *page = &h->pages[handle-1];
    *pixels_out = page->pixels;
    *dim_out = page->dim.x;
    *kind_out = page->kind;
    result = (U64)page->dim.x*(U64)page->dim.y*(page->kind == FNT_GlyphKind_Color ? 4 : 1);
  }
  return result;
}

GC_EXPORT U32
gc_page_count(GC_Harness *h, U32 kind)
{
  return h->cache->atlases[kind].page_count;
}

GC_EXPORT void
gc_stats(GC_Harness *h, U64 *out)
{
  out[0] = h->cache->stats.hits;
  out[1] = h->cache->stats.misses;
  out[2] = h->cache->stats.evictions;
  out[3] = h->cache->stats.failures;
}

GC_EXPORT F32
gc_zoom_bucket(F32 size)
{
  return fnt_zoom_bucket_from_size(size);
}

GC_EXPORT void
gc_pen_split(F32 x, S32 *whole, F32 *subpixel_x)
{
  fnt_glyph_pen_split(x, whole, subpixel_x);
}

GC_EXPORT U64
gc_user_axis_values(U8 *fvar, U64 fvar_size, U8 *avar, U64 avar_size, S16 *coords, U64 count, F32 *values_out)
{
  gc_init();
  FP_AxisValue out[64];
  U64 result = fp_user_axis_values_from_normalized(str8(fvar, fvar_size), str8(avar, avar_size), coords, count, out, ArrayCount(out));
  for EachIndex(idx, result) { values_out[idx] = out[idx].value; }
  return result;
}

// The PNG's offset in cbdt and its size, or 0 for none.
GC_EXPORT U64
gc_cbdt_png(U8 *cblc, U64 cblc_size, U8 *cbdt, U64 cbdt_size, U32 glyph_id, F32 size_px, U64 *offset_out, S32 *left_out, S32 *top_out, U32 *ppem_out)
{
  String8 png = {0};
  S16 left = 0, top = 0;
  U64 result = 0;
  if(fp_cbdt_png(str8(cblc, cblc_size), str8(cbdt, cbdt_size), glyph_id, size_px, &png, &left, &top, ppem_out))
  {
    *offset_out = (U64)(png.str - cbdt);
    *left_out = left;
    *top_out = top;
    result = png.size;
  }
  return result;
}
