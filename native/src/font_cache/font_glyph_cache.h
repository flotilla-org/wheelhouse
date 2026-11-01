#ifndef FONT_GLYPH_CACHE_H
#define FONT_GLYPH_CACHE_H

// Glyphs by id, as layout places them (#560; decided on #352 and #543). CPU
// only: pages reach the GPU through the backend, so tests run without one.

////////////////////////////////
//~ Font Tags

// The one identity of a font face. face_index picks a face of a collection
// (.ttc); a file's first face has the tag it had before faces were indexed.
typedef struct FNT_Tag FNT_Tag;
struct FNT_Tag
{
  U64 u64[2];
  U32 face_index;
  U32 pad;
};

////////////////////////////////
//~ Keys

#define FNT_GLYPH_SUBPIXEL_BINS 4

typedef U32 FNT_GlyphKind;
enum
{
  FNT_GlyphKind_Mask,
  FNT_GlyphKind_Color,
  FNT_GlyphKind_COUNT,
};

typedef U32 FNT_GlyphHinting;
enum
{
  FNT_GlyphHinting_None,
  // Vertical only: horizontal positions stay where layout put them.
  FNT_GlyphHinting_Vertical,
};

typedef U32 FNT_GlyphSynthesis;
enum
{
  FNT_GlyphSynthesis_Embolden = (1<<0),
};

typedef struct FNT_GlyphParams FNT_GlyphParams;
struct FNT_GlyphParams
{
  FNT_Tag tag;
  U32 glyph_id;
  F32 size_px;
  // The pen's fraction of a pixel, [0, 1); see fnt_glyph_pen_split.
  F32 subpixel_x;
  // Normalized (2.14), in the font's fvar axis order, as wh_text_coords gives them.
  S16 *coords;
  U64 coord_count;
  FNT_GlyphSynthesis synthesis;
  // Synthetic italic, in degrees; 0 for none.
  F32 skew;
  FNT_GlyphHinting hinting;
};

// Compared whole: the hash only picks the slot.
typedef struct FNT_GlyphKey FNT_GlyphKey;
struct FNT_GlyphKey
{
  FNT_Tag tag;
  U32 glyph_id;
  S32 size_64;
  U32 subpixel_bin;
  U64 coords_hash;
  FNT_GlyphSynthesis synthesis;
  S32 skew_64;
  FNT_GlyphHinting hinting;
};

////////////////////////////////
//~ Backend

// Mask pixels are coverage, one byte each; colour pixels are premultiplied
// RGBA8. left and top place the bitmap's top-left from the pen on the
// baseline (top is up).
typedef struct FNT_GlyphRaster FNT_GlyphRaster;
struct FNT_GlyphRaster
{
  FNT_GlyphKind kind;
  Vec2S16 dim;
  S16 left;
  S16 top;
  U8 *pixels;
};

typedef struct FNT_GlyphBackend FNT_GlyphBackend;
struct FNT_GlyphBackend
{
  void *user;
  // The key's subpixel bin and size are the ones to rasterize at.
  FNT_GlyphRaster (*rasterize)(void *user, Arena *arena, FNT_GlyphKey *key, FNT_GlyphParams *params);
  U64 (*page_alloc)(void *user, FNT_GlyphKind kind, Vec2S16 dim);
  // pixels cover rect exactly, in the kind's format.
  void (*page_fill)(void *user, U64 page, FNT_GlyphKind kind, Rng2S16 rect, U8 *pixels);
};

////////////////////////////////
//~ Cache

typedef struct FNT_Glyph FNT_Glyph;
struct FNT_Glyph
{
  // 0 when it couldn't be placed: drawn next frame, or never if bigger than a page.
  B32 ok;
  FNT_GlyphKind kind;
  // The backend's page; 0, with an empty region, for a glyph with no pixels.
  U64 page;
  Rng2S16 region;
  S16 left;
  S16 top;
};

typedef struct FNT_GlyphEntry FNT_GlyphEntry;
struct FNT_GlyphEntry
{
  FNT_GlyphEntry *hash_next;
  FNT_GlyphEntry *hash_prev;
  FNT_GlyphEntry *page_next;
  FNT_GlyphEntry *page_prev;
  U64 hash;
  FNT_GlyphKey key;
  FNT_Glyph glyph;
  // -1 when on no page.
  S32 page_index;
  // The frame it didn't fit in: tried again in the next. 0 otherwise.
  U64 failed_frame;
};

typedef struct FNT_GlyphShelf FNT_GlyphShelf;
struct FNT_GlyphShelf
{
  S16 y;
  S16 height;
  S16 x;
};

typedef struct FNT_GlyphPage FNT_GlyphPage;
struct FNT_GlyphPage
{
  U64 handle;
  U64 last_used_frame;
  FNT_GlyphShelf *shelves;
  U32 shelf_count;
  U32 shelf_cap;
  S16 next_y;
  FNT_GlyphEntry *first_entry;
  FNT_GlyphEntry *last_entry;
};

#define FNT_GLYPH_PAGE_MAX 32

typedef struct FNT_GlyphAtlas FNT_GlyphAtlas;
struct FNT_GlyphAtlas
{
  Vec2S16 page_dim;
  U32 page_budget;
  U32 page_count;
  FNT_GlyphPage pages[FNT_GLYPH_PAGE_MAX];
};

typedef struct FNT_GlyphStats FNT_GlyphStats;
struct FNT_GlyphStats
{
  U64 hits;
  U64 misses;
  U64 evictions;
  U64 failures;
};

typedef struct FNT_GlyphCacheParams FNT_GlyphCacheParams;
struct FNT_GlyphCacheParams
{
  Vec2S16 page_dim[FNT_GlyphKind_COUNT];
  U32 page_budget[FNT_GlyphKind_COUNT];
  F32 gamma;
};

typedef struct FNT_GlyphCache FNT_GlyphCache;
struct FNT_GlyphCache
{
  Arena *arena;
  FNT_GlyphBackend backend;
  U64 frame_index;
  U64 slots_count;
  FNT_GlyphEntry **slots;
  FNT_GlyphEntry *free_entry;
  FNT_GlyphAtlas atlases[FNT_GlyphKind_COUNT];
  F32 gamma;
  U8 gamma_lut[256];
  FNT_GlyphStats stats;
  // Tests force collisions with it; 0 hashes the key.
  U64 (*hash_override)(FNT_GlyphKey *key);
};

////////////////////////////////
//~ Functions

internal FNT_GlyphCacheParams fnt_glyph_cache_default_params(void);
internal FNT_GlyphCache *fnt_glyph_cache_alloc(FNT_GlyphBackend backend, FNT_GlyphCacheParams *params);
internal void fnt_glyph_cache_release(FNT_GlyphCache *cache);
internal void fnt_glyph_cache_frame(FNT_GlyphCache *cache);
internal void fnt_glyph_cache_set_gamma(FNT_GlyphCache *cache, F32 gamma);
internal FNT_GlyphKey fnt_glyph_key_from_params(FNT_GlyphParams *params);
internal B32 fnt_glyph_key_match(FNT_GlyphKey *a, FNT_GlyphKey *b);
internal U64 fnt_glyph_hash_from_key(FNT_GlyphKey *key);
internal FNT_Glyph fnt_glyph_from_params(FNT_GlyphCache *cache, FNT_GlyphParams *params);
internal void fnt_glyph_pen_split(F32 x, S32 *whole_out, F32 *subpixel_x_out);
internal F32 fnt_zoom_bucket_from_size(F32 size);

#endif // FONT_GLYPH_CACHE_H
