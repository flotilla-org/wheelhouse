#ifndef UISHELL_TERMINAL_GLYPH_H
#define UISHELL_TERMINAL_GLYPH_H

typedef struct UIShell_TerminalGlyphKey UIShell_TerminalGlyphKey;
struct UIShell_TerminalGlyphKey
{
  U64 codepoint_hash;
  U64 codepoint_count;
  U32 style_flags;
  U32 presentation;
  U32 cell_width;
};

typedef struct UIShell_TerminalGlyphCacheNode UIShell_TerminalGlyphCacheNode;
struct UIShell_TerminalGlyphCacheNode
{
  UIShell_TerminalGlyphCacheNode *next;
  UIShell_TerminalGlyphKey key;
  U32 *codepoints;
  FNT_Tag font;
};

typedef struct UIShell_TerminalGlyphCacheSlot UIShell_TerminalGlyphCacheSlot;
struct UIShell_TerminalGlyphCacheSlot
{
  UIShell_TerminalGlyphCacheNode *first;
};

typedef struct UIShell_TerminalGlyphCache UIShell_TerminalGlyphCache;
struct UIShell_TerminalGlyphCache
{
  Arena *arena;
  U64 font_set_hash;
  UIShell_TerminalGlyphCacheSlot slots[256];
};

typedef struct UIShell_TerminalFontSet UIShell_TerminalFontSet;
struct UIShell_TerminalFontSet
{
  FNT_Tag primary_font;
  FNT_Tag *fallback_fonts;
  U64 fallback_font_count;
  U64 fallback_font_cap;
  FNT_Tag *color_emoji_fonts;
  U64 color_emoji_font_count;
  U64 color_emoji_font_cap;
  FNT_RasterFlags raster_flags;
  F32 font_size;
};

typedef struct UIShell_TerminalGlyphRenderer UIShell_TerminalGlyphRenderer;
struct UIShell_TerminalGlyphRenderer
{
  UIShell_TerminalFontSet font_set;
  UIShell_TerminalGlyphCache *cache;
  B32 trace_enabled;
  B32 trace_all_rows;
  U64 trace_row;
  U64 trace_generation;
};

typedef struct UIShell_TerminalDrawParams UIShell_TerminalDrawParams;
struct UIShell_TerminalDrawParams
{
  Rng2F32 canvas_rect;
  Vec4F32 background_color;
  F32 cell_width_px;
  F32 cell_height_px;
};

typedef struct UIShell_TerminalCellFeed UIShell_TerminalCellFeed;
struct UIShell_TerminalCellFeed
{
  U16 cols;
  U16 rows;
  cleat_cell const *cells;
  U64 cell_count;
  cleat_cursor cursor;
};

typedef struct UIShell_TerminalCellCache UIShell_TerminalCellCache;
struct UIShell_TerminalCellCache
{
  Arena *arena;
  U16 cols;
  U16 rows;
  cleat_cell *cells;
  U64 cell_count;
  cleat_cursor cursor;
  cleat_terminal_scrollbar_state scrollbar;
  U64 render_generation;
};

typedef struct UIShell_TerminalCursorArray UIShell_TerminalCursorArray;
struct UIShell_TerminalCursorArray
{
  cleat_cursor *v;
  U64 count;
};

internal void uishell_terminal_font_set_push_fallback(UIShell_TerminalFontSet *font_set, FNT_Tag font);
internal void uishell_terminal_font_set_push_color_emoji(UIShell_TerminalFontSet *font_set, FNT_Tag font);
internal void uishell_terminal_font_set_push_fallback_static_data(UIShell_TerminalFontSet *font_set, String8 *data_ptr);
internal void uishell_terminal_font_set_push_color_emoji_static_data(UIShell_TerminalFontSet *font_set, String8 *data_ptr);
internal void uishell_terminal_font_set_push_fallback_paths(Arena *scratch_arena, UIShell_TerminalFontSet *font_set, String8 fallback_setting);
internal UIShell_TerminalFontSet uishell_terminal_font_set_from_fonts(Arena *scratch_arena, FNT_Tag primary_font, FNT_RasterFlags raster_flags, F32 font_size, FNT_Tag main_fallback_font, String8 fallback_setting, String8 **embedded_color_emoji_data, U64 embedded_color_emoji_count, String8 **embedded_fallback_data, U64 embedded_fallback_count);
internal void uishell_terminal_sync_font_cache(UIShell_TerminalGlyphCache *cache, UIShell_TerminalFontSet *font_set);
internal cleat_snapshot uishell_terminal_fixture_snapshot(Arena *arena, U16 cols, U16 rows);
internal UIShell_TerminalCursorArray uishell_terminal_fixture_cursor_array(Arena *arena, U16 cols, U16 rows);
internal UIShell_TerminalCellFeed uishell_terminal_cell_feed_from_cleat_snapshot(cleat_snapshot const *snapshot);
internal UIShell_TerminalCellFeed uishell_terminal_cell_feed_from_cache(UIShell_TerminalCellCache *cache);
internal void uishell_terminal_cell_cache_apply_render_update(UIShell_TerminalCellCache *cache, cleat_render_update const *update);
internal B32 uishell_terminal_write_fixture_ppm(String8 path, FNT_Tag primary_font, FNT_Tag main_fallback_font, F32 font_size, FNT_RasterFlags raster_flags, String8 **embedded_color_emoji_data, U64 embedded_color_emoji_count, String8 **embedded_fallback_data, U64 embedded_fallback_count);
internal B32 uishell_terminal_glyph_diagnostics(FNT_Tag primary_font, FNT_Tag main_fallback_font, F32 font_size, FNT_RasterFlags raster_flags, String8 **embedded_color_emoji_data, U64 embedded_color_emoji_count, String8 **embedded_fallback_data, U64 embedded_fallback_count);
internal void uishell_terminal_glyph_renderer_draw_cell_feed_with_cursors(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalDrawParams *params, UIShell_TerminalCellFeed const *feed, UIShell_TerminalCursorArray cursors);
internal void uishell_terminal_glyph_renderer_draw_cell_feed(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalDrawParams *params, UIShell_TerminalCellFeed const *feed);
internal void uishell_terminal_glyph_renderer_draw_snapshot_with_cursors(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalDrawParams *params, cleat_snapshot const *snapshot, UIShell_TerminalCursorArray cursors);
internal void uishell_terminal_glyph_renderer_draw_snapshot(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalDrawParams *params, cleat_snapshot const *snapshot);

#endif // UISHELL_TERMINAL_GLYPH_H
