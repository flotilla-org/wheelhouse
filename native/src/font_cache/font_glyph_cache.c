////////////////////////////////
//~ Keys

internal FNT_GlyphKey
fnt_glyph_key_from_params(FNT_GlyphParams *params)
{
  FNT_GlyphKey key = {0};
  key.tag = params->tag;
  key.glyph_id = params->glyph_id;
  key.size_64 = (S32)round_f32(params->size_px*64.f);
  S32 bin = (S32)floor_f32(params->subpixel_x*FNT_GLYPH_SUBPIXEL_BINS + 0.5f);
  key.subpixel_bin = (U32)Clamp(0, bin, FNT_GLYPH_SUBPIXEL_BINS-1);
  if(params->coord_count != 0)
  {
    key.coords_hash = XXH3_64bits(params->coords, params->coord_count*sizeof(params->coords[0]));
  }
  key.synthesis = params->synthesis;
  key.skew_64 = (S32)round_f32(params->skew*64.f);
  key.hinting = params->hinting;
  return key;
}

internal B32
fnt_glyph_key_match(FNT_GlyphKey *a, FNT_GlyphKey *b)
{
  B32 result = (a->tag.u64[0] == b->tag.u64[0] &&
                a->tag.u64[1] == b->tag.u64[1] &&
                a->tag.face_index == b->tag.face_index &&
                a->glyph_id == b->glyph_id &&
                a->size_64 == b->size_64 &&
                a->subpixel_bin == b->subpixel_bin &&
                a->coords_hash == b->coords_hash &&
                a->synthesis == b->synthesis &&
                a->skew_64 == b->skew_64 &&
                a->hinting == b->hinting);
  return result;
}

internal U64
fnt_glyph_hash_from_key(FNT_GlyphKey *key)
{
  U64 fields[] =
  {
    key->tag.u64[0],
    key->tag.u64[1],
    key->tag.face_index,
    key->glyph_id,
    (U64)(U32)key->size_64,
    key->subpixel_bin,
    key->coords_hash,
    key->synthesis,
    (U64)(U32)key->skew_64,
    key->hinting,
  };
  U64 result = XXH3_64bits(fields, sizeof(fields));
  return result;
}

// Rounds x to a quarter pixel: draw the glyph at whole, rasterized at
// subpixel_x.
internal void
fnt_glyph_pen_split(F32 x, S32 *whole_out, F32 *subpixel_x_out)
{
  F32 quarters = round_f32(x*FNT_GLYPH_SUBPIXEL_BINS);
  F32 whole = floor_f32(quarters/FNT_GLYPH_SUBPIXEL_BINS);
  *whole_out = (S32)whole;
  *subpixel_x_out = (quarters - whole*FNT_GLYPH_SUBPIXEL_BINS)/FNT_GLYPH_SUBPIXEL_BINS;
}

// The nearest size of the form 2^(k/4): an animated camera rasterizes few
// sizes, each reused across frames.
internal F32
fnt_zoom_bucket_from_size(F32 size)
{
  F32 result = 0;
  if(size > 0)
  {
    result = exp2f(round_f32(log2f(size)*4.f)/4.f);
  }
  return result;
}

////////////////////////////////
//~ Cache

internal FNT_GlyphCacheParams
fnt_glyph_cache_default_params(void)
{
  FNT_GlyphCacheParams params = {0};
  params.page_dim[FNT_GlyphKind_Mask] = v2s16(2048, 2048);
  params.page_dim[FNT_GlyphKind_Color] = v2s16(1024, 1024);
  params.page_budget[FNT_GlyphKind_Mask] = 6;
  params.page_budget[FNT_GlyphKind_Color] = 4;
  // A starting point; the look is tuned on each OS in #562.
  params.gamma = 1.2f;
  return params;
}

internal FNT_GlyphCache *
fnt_glyph_cache_alloc(FNT_GlyphBackend backend, FNT_GlyphCacheParams *params)
{
  Arena *arena = arena_alloc();
  FNT_GlyphCache *cache = push_array(arena, FNT_GlyphCache, 1);
  cache->arena = arena;
  cache->backend = backend;
  cache->frame_index = 1;
  cache->slots_count = 4096;
  cache->slots = push_array(arena, FNT_GlyphEntry *, cache->slots_count);
  for EachIndex(kind, FNT_GlyphKind_COUNT)
  {
    FNT_GlyphAtlas *atlas = &cache->atlases[kind];
    atlas->page_dim = params->page_dim[kind];
    atlas->page_budget = Clamp(1, params->page_budget[kind], FNT_GLYPH_PAGE_MAX);
  }
  fnt_glyph_cache_set_gamma(cache, params->gamma);
  return cache;
}

internal void
fnt_glyph_cache_release(FNT_GlyphCache *cache)
{
  arena_release(cache->arena);
}

internal void
fnt_glyph_cache_frame(FNT_GlyphCache *cache)
{
  cache->frame_index += 1;
}

internal void
fnt_glyph_page_clear(FNT_GlyphCache *cache, FNT_GlyphPage *page)
{
  for(FNT_GlyphEntry *e = page->first_entry, *next = 0; e != 0; e = next)
  {
    next = e->page_next;
    FNT_GlyphEntry **slot = &cache->slots[e->hash%cache->slots_count];
    if(e->hash_prev != 0) { e->hash_prev->hash_next = e->hash_next; } else { *slot = e->hash_next; }
    if(e->hash_next != 0) { e->hash_next->hash_prev = e->hash_prev; }
    MemoryZeroStruct(e);
    SLLStackPush_N(cache->free_entry, e, hash_next);
  }
  page->first_entry = page->last_entry = 0;
  page->shelf_count = 0;
  page->next_y = 0;
}

// Changing it drops the masks rasterized under the old curve.
internal void
fnt_glyph_cache_set_gamma(FNT_GlyphCache *cache, F32 gamma)
{
  cache->gamma = ClampBot(0.01f, gamma);
  for EachIndex(c, 256)
  {
    cache->gamma_lut[c] = (U8)Clamp(0, (S32)round_f32(255.f*pow_f32((F32)c/255.f, 1.f/cache->gamma)), 255);
  }
  FNT_GlyphAtlas *atlas = &cache->atlases[FNT_GlyphKind_Mask];
  for EachIndex(idx, atlas->page_count)
  {
    fnt_glyph_page_clear(cache, &atlas->pages[idx]);
  }
}

// Shelves: glyphs of a size share a row. Pages are cleared whole, so no
// region is freed alone.
internal B32
fnt_glyph_page_alloc_region(FNT_GlyphCache *cache, FNT_GlyphPage *page, Vec2S16 page_dim, Vec2S16 size, Vec2S16 *p_out)
{
  B32 result = 0;
  FNT_GlyphShelf *best = 0;
  for EachIndex(idx, page->shelf_count)
  {
    FNT_GlyphShelf *shelf = &page->shelves[idx];
    if(shelf->height >= size.y && shelf->height <= size.y + size.y/4 + 2 && page_dim.x - shelf->x >= size.x &&
       (best == 0 || shelf->height < best->height))
    {
      best = shelf;
    }
  }
  if(best == 0 && page_dim.y - page->next_y >= size.y && page_dim.x >= size.x)
  {
    if(page->shelf_count == page->shelf_cap)
    {
      U32 cap = Max(64, page->shelf_cap*2);
      FNT_GlyphShelf *shelves = push_array_no_zero(cache->arena, FNT_GlyphShelf, cap);
      MemoryCopy(shelves, page->shelves, sizeof(FNT_GlyphShelf)*page->shelf_count);
      page->shelves = shelves;
      page->shelf_cap = cap;
    }
    best = &page->shelves[page->shelf_count];
    page->shelf_count += 1;
    best->y = page->next_y;
    best->height = size.y;
    best->x = 0;
    page->next_y += size.y;
  }
  if(best != 0)
  {
    *p_out = v2s16(best->x, best->y);
    best->x += size.x;
    result = 1;
  }
  return result;
}

// A page used this frame is never cleared: the glyphs drawn from it so far
// must still be there when the frame's batches draw.
internal S32
fnt_glyph_atlas_alloc(FNT_GlyphCache *cache, FNT_GlyphKind kind, Vec2S16 size, Vec2S16 *p_out)
{
  FNT_GlyphAtlas *atlas = &cache->atlases[kind];
  S32 result = -1;
  if(size.x <= atlas->page_dim.x && size.y <= atlas->page_dim.y)
  {
    for EachIndex(idx, atlas->page_count)
    {
      if(fnt_glyph_page_alloc_region(cache, &atlas->pages[idx], atlas->page_dim, size, p_out))
      {
        result = (S32)idx;
        break;
      }
    }
    if(result < 0 && atlas->page_count < atlas->page_budget)
    {
      U64 handle = cache->backend.page_alloc(cache->backend.user, kind, atlas->page_dim);
      if(handle != 0)
      {
        FNT_GlyphPage *page = &atlas->pages[atlas->page_count];
        MemoryZeroStruct(page);
        page->handle = handle;
        atlas->page_count += 1;
        if(fnt_glyph_page_alloc_region(cache, page, atlas->page_dim, size, p_out))
        {
          result = (S32)(atlas->page_count - 1);
        }
      }
    }
    if(result < 0)
    {
      S32 lru = -1;
      for EachIndex(idx, atlas->page_count)
      {
        FNT_GlyphPage *page = &atlas->pages[idx];
        if(page->last_used_frame != cache->frame_index && (lru < 0 || page->last_used_frame < atlas->pages[lru].last_used_frame))
        {
          lru = (S32)idx;
        }
      }
      if(lru >= 0)
      {
        fnt_glyph_page_clear(cache, &atlas->pages[lru]);
        cache->stats.evictions += 1;
        if(fnt_glyph_page_alloc_region(cache, &atlas->pages[lru], atlas->page_dim, size, p_out))
        {
          result = lru;
        }
      }
    }
  }
  return result;
}

internal FNT_Glyph
fnt_glyph_from_params(FNT_GlyphCache *cache, FNT_GlyphParams *params)
{
  FNT_GlyphKey key = fnt_glyph_key_from_params(params);
  U64 hash = cache->hash_override ? cache->hash_override(&key) : fnt_glyph_hash_from_key(&key);
  FNT_GlyphEntry **slot = &cache->slots[hash%cache->slots_count];
  FNT_GlyphEntry *entry = 0;
  for(FNT_GlyphEntry *e = *slot; e != 0; e = e->hash_next)
  {
    if(e->hash == hash && fnt_glyph_key_match(&e->key, &key))
    {
      entry = e;
      break;
    }
  }
  FNT_Glyph result = {0};
  if(entry != 0)
  {
    cache->stats.hits += 1;
    if(entry->page_index >= 0)
    {
      cache->atlases[entry->glyph.kind].pages[entry->page_index].last_used_frame = cache->frame_index;
    }
    result = entry->glyph;
  }
  else
  {
    cache->stats.misses += 1;
    Temp scratch = scratch_begin(0, 0);
    FNT_GlyphRaster raster = cache->backend.rasterize(cache->backend.user, scratch.arena, &key, params);
    FNT_GlyphKind kind = (raster.kind == FNT_GlyphKind_Color ? FNT_GlyphKind_Color : FNT_GlyphKind_Mask);
    B32 empty = (raster.dim.x <= 0 || raster.dim.y <= 0 || raster.pixels == 0);
    FNT_GlyphAtlas *atlas = &cache->atlases[kind];
    // One pixel of clear border, so sampling between texels never reads a neighbour.
    Vec2S16 padded = v2s16(raster.dim.x + 2, raster.dim.y + 2);
    B32 too_big = (!empty && (padded.x > atlas->page_dim.x || padded.y > atlas->page_dim.y));
    S32 page_index = -1;
    Vec2S16 p = {0};
    if(!empty && !too_big)
    {
      page_index = fnt_glyph_atlas_alloc(cache, kind, padded, &p);
    }
    result.kind = kind;
    result.left = raster.left;
    result.top = raster.top;
    if(page_index >= 0)
    {
      FNT_GlyphPage *page = &atlas->pages[page_index];
      U64 bpp = (kind == FNT_GlyphKind_Color ? 4 : 1);
      U8 *pixels = push_array(scratch.arena, U8, (U64)padded.x*(U64)padded.y*bpp);
      for(S16 y = 0; y < raster.dim.y; y += 1)
      {
        U8 *src = raster.pixels + (U64)y*(U64)raster.dim.x*bpp;
        U8 *dst = pixels + ((U64)(y+1)*(U64)padded.x + 1)*bpp;
        if(kind == FNT_GlyphKind_Mask)
        {
          for(S16 x = 0; x < raster.dim.x; x += 1) { dst[x] = cache->gamma_lut[src[x]]; }
        }
        else
        {
          MemoryCopy(dst, src, (U64)raster.dim.x*bpp);
        }
      }
      Rng2S16 rect = r2s16p(p.x, p.y, p.x + padded.x, p.y + padded.y);
      cache->backend.page_fill(cache->backend.user, page->handle, kind, rect, pixels);
      page->last_used_frame = cache->frame_index;
      result.ok = 1;
      result.page = page->handle;
      result.region = r2s16p(p.x + 1, p.y + 1, p.x + 1 + raster.dim.x, p.y + 1 + raster.dim.y);
    }
    else if(empty)
    {
      result.ok = 1;
    }
    else
    {
      cache->stats.failures += 1;
    }
    // A glyph that didn't fit this frame is tried again next frame; one
    // bigger than a page is remembered as never fitting.
    if(result.ok || too_big)
    {
      entry = cache->free_entry;
      if(entry != 0) { SLLStackPop_N(cache->free_entry, hash_next); }
      else { entry = push_array_no_zero(cache->arena, FNT_GlyphEntry, 1); }
      MemoryZeroStruct(entry);
      entry->hash = hash;
      entry->key = key;
      entry->glyph = result;
      entry->page_index = page_index;
      entry->hash_next = *slot;
      if(*slot != 0) { (*slot)->hash_prev = entry; }
      *slot = entry;
      if(page_index >= 0)
      {
        FNT_GlyphPage *page = &atlas->pages[page_index];
        DLLPushBack_NP(page->first_entry, page->last_entry, entry, page_next, page_prev);
      }
    }
    scratch_end(scratch);
  }
  return result;
}
