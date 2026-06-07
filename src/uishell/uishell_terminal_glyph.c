////////////////////////////////
//~ rjf: Terminal Glyph Renderer

internal Vec4F32
uishell_terminal_rgba_from_rgb(cleat_rgb rgb)
{
  Vec4F32 result = linear_from_srgba(v4f32((F32)rgb.r/255.f, (F32)rgb.g/255.f, (F32)rgb.b/255.f, 1.f));
  return result;
}

internal B32
uishell_terminal_rgb_match(cleat_rgb a, cleat_rgb b)
{
  B32 result = (a.r == b.r && a.g == b.g && a.b == b.b);
  return result;
}

internal String8
uishell_terminal_string_from_cell(Arena *arena, cleat_cell const *cell)
{
  U8 *buffer = push_array(arena, U8, cell->grapheme_count*4 + 1);
  U64 size = 0;
  for(U64 idx = 0; idx < cell->grapheme_count; idx += 1)
  {
    size += utf8_encode(buffer + size, cell->graphemes[idx]);
  }
  String8 result = str8(buffer, size);
  return result;
}

internal cleat_rgb
uishell_terminal_cell_fg(cleat_cell const *cell)
{
  cleat_rgb result = (cell->flags & CLEAT_CELL_FLAG_INVERSE) ? cell->bg : cell->fg;
  return result;
}

internal cleat_rgb
uishell_terminal_cell_bg(cleat_cell const *cell)
{
  cleat_rgb result = (cell->flags & CLEAT_CELL_FLAG_INVERSE) ? cell->fg : cell->bg;
  return result;
}

internal Vec4F32
uishell_terminal_text_color_from_cell(cleat_cell const *cell)
{
  Vec4F32 result = uishell_terminal_rgba_from_rgb(uishell_terminal_cell_fg(cell));
  if(cell->flags & CLEAT_CELL_FLAG_FAINT)
  {
    result.w *= 0.55f;
  }
  return result;
}

internal B32
uishell_terminal_cell_is_spacer(cleat_cell const *cell)
{
  B32 result = (cell->width == CLEAT_CELL_WIDTH_SPACER_HEAD ||
                cell->width == CLEAT_CELL_WIDTH_SPACER_TAIL);
  return result;
}

internal U64
uishell_terminal_cell_display_cols(cleat_cell const *cell)
{
  U64 result = 1;
  if(cell->width == CLEAT_CELL_WIDTH_WIDE)
  {
    result = 2;
  }
  return result;
}

internal U64
uishell_terminal_hash_from_font_set(UIShell_TerminalFontSet *font_set)
{
  U64 result = u64_hash_from_str8(str8_struct(&font_set->primary_font));
  result = u64_hash_from_seed_str8(result, str8_struct(&font_set->fallback_font_count));
  for(U64 idx = 0; idx < font_set->fallback_font_count; idx += 1)
  {
    result = u64_hash_from_seed_str8(result, str8_struct(&font_set->fallback_fonts[idx]));
  }
  return result;
}

internal void
uishell_terminal_sync_font_cache(UIShell_TerminalGlyphCache *cache, UIShell_TerminalFontSet *font_set)
{
  if(cache->arena == 0)
  {
    cache->arena = arena_alloc(.name = "terminal glyph cache");
  }
  U64 hash = uishell_terminal_hash_from_font_set(font_set);
  if(cache->font_set_hash != hash)
  {
    cache->font_set_hash = hash;
    arena_clear(cache->arena);
    MemoryZeroArray(cache->slots);
  }
}

internal void
uishell_terminal_font_set_push_fallback(UIShell_TerminalFontSet *font_set, FNT_Tag font)
{
  if(!fnt_tag_match(font, fnt_tag_zero()) &&
     !fnt_tag_match(font, font_set->primary_font) &&
     font_set->fallback_font_count < font_set->fallback_font_cap)
  {
    B32 duplicate = 0;
    for(U64 idx = 0; idx < font_set->fallback_font_count; idx += 1)
    {
      if(fnt_tag_match(font_set->fallback_fonts[idx], font))
      {
        duplicate = 1;
        break;
      }
    }
    if(!duplicate)
    {
      font_set->fallback_fonts[font_set->fallback_font_count] = font;
      font_set->fallback_font_count += 1;
    }
  }
}

internal void
uishell_terminal_font_set_push_fallback_static_data(UIShell_TerminalFontSet *font_set, String8 *data_ptr)
{
  if(data_ptr != 0 && data_ptr->size != 0)
  {
    uishell_terminal_font_set_push_fallback(font_set, fnt_tag_from_static_data_string(data_ptr));
  }
}

internal void
uishell_terminal_font_set_push_fallback_paths(Arena *scratch_arena, UIShell_TerminalFontSet *font_set, String8 fallback_setting)
{
  U8 split_chars[] = {',', ';', '\n'};
  String8List fallback_paths = str8_split(scratch_arena, fallback_setting, split_chars, ArrayCount(split_chars), 0);
  for(String8Node *node = fallback_paths.first; node != 0; node = node->next)
  {
    String8 path = str8_skip_chop_whitespace(node->string);
    if(path.size != 0)
    {
      uishell_terminal_font_set_push_fallback(font_set, fnt_tag_from_path(path));
    }
  }
}

internal UIShell_TerminalFontSet
uishell_terminal_font_set_from_fonts(Arena *scratch_arena, FNT_Tag primary_font, FNT_RasterFlags raster_flags, F32 font_size, FNT_Tag main_fallback_font, String8 fallback_setting, String8 **embedded_fallback_data, U64 embedded_fallback_count)
{
  UIShell_TerminalFontSet result =
  {
    .primary_font = primary_font,
    .fallback_font_cap = 32 + embedded_fallback_count,
    .raster_flags = raster_flags,
    .font_size = font_size,
  };
  result.fallback_fonts = push_array(scratch_arena, FNT_Tag, result.fallback_font_cap);
  uishell_terminal_font_set_push_fallback_paths(scratch_arena, &result, fallback_setting);
  for(U64 idx = 0; idx < embedded_fallback_count; idx += 1)
  {
    uishell_terminal_font_set_push_fallback_static_data(&result, embedded_fallback_data[idx]);
  }
  uishell_terminal_font_set_push_fallback(&result, main_fallback_font);
  return result;
}

enum
{
  UIShell_TerminalGlyphPresentation_Unspecified,
  UIShell_TerminalGlyphPresentation_Text,
  UIShell_TerminalGlyphPresentation_Emoji,
};

internal B32
uishell_terminal_codepoint_is_variation_selector(U32 codepoint)
{
  B32 result = ((0xFE00 <= codepoint && codepoint <= 0xFE0F) ||
                (0xE0100 <= codepoint && codepoint <= 0xE01EF));
  return result;
}

internal U64
uishell_terminal_hash_from_codepoints(U32 const *codepoints, U64 count)
{
  U64 result = 5381;
  for(U64 idx = 0; idx < count; idx += 1)
  {
    result = ((result << 5) + result) + codepoints[idx];
  }
  return result;
}

internal UIShell_TerminalGlyphKey
uishell_terminal_glyph_key_from_codepoints(U32 const *codepoints, U64 count, U32 style_flags, U32 cell_width)
{
  UIShell_TerminalGlyphKey result =
  {
    .codepoint_hash = uishell_terminal_hash_from_codepoints(codepoints, count),
    .codepoint_count = count,
    .style_flags = style_flags,
    .presentation = UIShell_TerminalGlyphPresentation_Unspecified,
    .cell_width = cell_width,
  };
  for(U64 idx = 0; idx < count; idx += 1)
  {
    if(codepoints[idx] == 0xFE0E)
    {
      result.presentation = UIShell_TerminalGlyphPresentation_Text;
    }
    else if(codepoints[idx] == 0xFE0F)
    {
      result.presentation = UIShell_TerminalGlyphPresentation_Emoji;
    }
  }
  return result;
}

internal UIShell_TerminalGlyphKey
uishell_terminal_glyph_key_from_cell(cleat_cell const *cell)
{
  UIShell_TerminalGlyphKey result = uishell_terminal_glyph_key_from_codepoints(cell->graphemes, cell->grapheme_count, cell->flags, cell->width);
  return result;
}

internal B32
uishell_terminal_glyph_key_match(UIShell_TerminalGlyphKey a, UIShell_TerminalGlyphKey b)
{
  B32 result = (a.codepoint_hash == b.codepoint_hash &&
                a.codepoint_count == b.codepoint_count &&
                a.style_flags == b.style_flags &&
                a.presentation == b.presentation &&
                a.cell_width == b.cell_width);
  return result;
}

internal B32
uishell_terminal_glyph_cache_node_match(UIShell_TerminalGlyphCacheNode *node, UIShell_TerminalGlyphKey key, U32 const *codepoints)
{
  B32 result = uishell_terminal_glyph_key_match(node->key, key);
  if(result)
  {
    for(U64 idx = 0; idx < key.codepoint_count; idx += 1)
    {
      if(node->codepoints[idx] != codepoints[idx])
      {
        result = 0;
        break;
      }
    }
  }
  return result;
}

internal U64
uishell_terminal_hash_from_glyph_key(UIShell_TerminalGlyphKey key)
{
  U64 result = u64_hash_from_str8(str8_struct(&key.codepoint_hash));
  result = u64_hash_from_seed_str8(result, str8_struct(&key.codepoint_count));
  result = u64_hash_from_seed_str8(result, str8_struct(&key.style_flags));
  result = u64_hash_from_seed_str8(result, str8_struct(&key.presentation));
  result = u64_hash_from_seed_str8(result, str8_struct(&key.cell_width));
  return result;
}

internal B32
uishell_terminal_font_has_glyph_key(FNT_Tag font, UIShell_TerminalGlyphKey key, U32 const *codepoints)
{
  B32 result = 1;
  for(U64 idx = 0; idx < key.codepoint_count; idx += 1)
  {
    U32 codepoint = codepoints[idx];
    if(!uishell_terminal_codepoint_is_variation_selector(codepoint) &&
       !fnt_tag_has_codepoint(font, codepoint))
    {
      result = 0;
      break;
    }
  }
  return result;
}

internal FNT_Tag uishell_terminal_font_from_glyph_key(UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalGlyphKey key, U32 const *codepoints);

internal FNT_Tag
uishell_terminal_font_from_glyph_key_uncached(UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalGlyphKey key, U32 const *codepoints)
{
  UIShell_TerminalFontSet *font_set = &renderer->font_set;
  FNT_Tag result = font_set->primary_font;
  if(!uishell_terminal_font_has_glyph_key(result, key, codepoints))
  {
    result = fnt_tag_zero();
    for(U64 idx = 0; idx < font_set->fallback_font_count; idx += 1)
    {
      FNT_Tag fallback_font = font_set->fallback_fonts[idx];
      if(uishell_terminal_font_has_glyph_key(fallback_font, key, codepoints))
      {
        result = fallback_font;
        break;
      }
    }
  }
  return result;
}

internal FNT_Tag
uishell_terminal_any_presentation_font_from_glyph_key(UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalGlyphKey key, U32 const *codepoints)
{
  FNT_Tag result = fnt_tag_zero();
  if(key.presentation != UIShell_TerminalGlyphPresentation_Unspecified &&
     key.codepoint_count != 0)
  {
    Temp scratch = scratch_begin(0, 0);
    U32 *any_codepoints = push_array(scratch.arena, U32, key.codepoint_count);
    U64 any_count = 0;
    for(U64 idx = 0; idx < key.codepoint_count; idx += 1)
    {
      U32 codepoint = codepoints[idx];
      if(!uishell_terminal_codepoint_is_variation_selector(codepoint))
      {
        any_codepoints[any_count] = codepoint;
        any_count += 1;
      }
    }
    if(any_count != 0 && any_count != key.codepoint_count)
    {
      UIShell_TerminalGlyphKey any_key = uishell_terminal_glyph_key_from_codepoints(any_codepoints, any_count, key.style_flags, key.cell_width);
      result = uishell_terminal_font_from_glyph_key(renderer, any_key, any_codepoints);
    }
    scratch_end(scratch);
  }
  return result;
}

internal FNT_Tag
uishell_terminal_font_from_glyph_key(UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalGlyphKey key, U32 const *codepoints)
{
  UIShell_TerminalFontSet *font_set = &renderer->font_set;
  UIShell_TerminalGlyphCache *cache = renderer->cache;
  FNT_Tag result = font_set->primary_font;
  if(key.codepoint_hash != 0 && cache != 0)
  {
    U64 slot_idx = uishell_terminal_hash_from_glyph_key(key) % ArrayCount(cache->slots);
    UIShell_TerminalGlyphCacheSlot *slot = &cache->slots[slot_idx];
    UIShell_TerminalGlyphCacheNode *match = 0;
    for(UIShell_TerminalGlyphCacheNode *node = slot->first; node != 0; node = node->next)
    {
      if(uishell_terminal_glyph_cache_node_match(node, key, codepoints))
      {
        match = node;
        break;
      }
    }
    if(match != 0)
    {
      result = match->font;
    }
    else
    {
      result = uishell_terminal_font_from_glyph_key_uncached(renderer, key, codepoints);
      if(fnt_tag_match(result, fnt_tag_zero()))
      {
        result = uishell_terminal_any_presentation_font_from_glyph_key(renderer, key, codepoints);
      }
      UIShell_TerminalGlyphCacheNode *node = push_array(cache->arena, UIShell_TerminalGlyphCacheNode, 1);
      node->key = key;
      node->codepoints = push_array(cache->arena, U32, key.codepoint_count);
      MemoryCopy(node->codepoints, codepoints, sizeof(U32)*key.codepoint_count);
      node->font = result;
      SLLStackPush(slot->first, node);
    }
  }
  return result;
}

internal FNT_Tag
uishell_terminal_font_from_cell(UIShell_TerminalGlyphRenderer *renderer, cleat_cell const *cell)
{
  FNT_Tag result = renderer->font_set.primary_font;
  if(cell->grapheme_count != 0)
  {
    UIShell_TerminalGlyphKey key = uishell_terminal_glyph_key_from_cell(cell);
    result = uishell_terminal_font_from_glyph_key(renderer, key, cell->graphemes);
  }
  return result;
}

internal void
uishell_terminal_draw_missing_glyph(Rng2F32 cell_rect, Vec4F32 color)
{
  F32 cell_width = ClampBot(1.f, cell_rect.x1 - cell_rect.x0);
  F32 cell_height = ClampBot(1.f, cell_rect.y1 - cell_rect.y0);
  F32 thickness = Clamp(1.f, floor_f32(Min(cell_width, cell_height)*0.08f), 2.f);
  F32 inset_x = Clamp(1.f, floor_f32(cell_width*0.18f), cell_width*0.35f);
  F32 inset_y = Clamp(1.f, floor_f32(cell_height*0.20f), cell_height*0.35f);
  Rng2F32 r =
  {
    floor_f32(cell_rect.x0 + inset_x),
    floor_f32(cell_rect.y0 + inset_y),
    ceil_f32(cell_rect.x1 - inset_x),
    ceil_f32(cell_rect.y1 - inset_y),
  };
  dr_rect(r2f32p(r.x0, r.y0, r.x1, r.y0 + thickness), color, 0, 0, 0);
  dr_rect(r2f32p(r.x0, r.y1 - thickness, r.x1, r.y1), color, 0, 0, 0);
  dr_rect(r2f32p(r.x0, r.y0, r.x0 + thickness, r.y1), color, 0, 0, 0);
  dr_rect(r2f32p(r.x1 - thickness, r.y0, r.x1, r.y1), color, 0, 0, 0);
}

internal B32
uishell_terminal_cursor_is_filled_block(cleat_cursor cursor)
{
  B32 result = (cursor.visible && cursor.style == CLEAT_CURSOR_STYLE_BLOCK);
  return result;
}

internal UIShell_TerminalCellFeed
uishell_terminal_cell_feed_from_cleat_snapshot(cleat_snapshot const *snapshot)
{
  UIShell_TerminalCellFeed result =
  {
    .cols = snapshot->cols,
    .rows = snapshot->rows,
    .cells = snapshot->cells,
    .cell_count = snapshot->cell_count,
    .cursor = snapshot->cursor,
  };
  return result;
}

internal void
uishell_terminal_cell_copy_from_cleat_cell(Arena *arena, cleat_cell *dst, cleat_cell const *src)
{
  *dst = *src;
  if(src->grapheme_count != 0 && src->graphemes != 0)
  {
    dst->graphemes = push_array(arena, U32, src->grapheme_count);
    MemoryCopy((void *)dst->graphemes, src->graphemes, sizeof(U32)*src->grapheme_count);
  }
}

internal void
uishell_terminal_cell_copy_from_render_cell(Arena *arena, cleat_cell *dst, cleat_render_cell const *src)
{
  MemoryZeroStruct(dst);
  dst->fg = src->style.fg;
  dst->bg = src->style.bg;
  dst->flags = src->style.flags;
  dst->width = src->style.width;
  dst->grapheme_count = src->grapheme_count;
  if(src->grapheme_count != 0 && src->graphemes != 0)
  {
    dst->graphemes = push_array(arena, U32, src->grapheme_count);
    MemoryCopy((void *)dst->graphemes, src->graphemes, sizeof(U32)*src->grapheme_count);
  }
}

internal void
uishell_terminal_cell_cache_apply_render_row(Arena *arena, UIShell_TerminalCellCache *cache, cleat_render_row const *row)
{
  if(row != 0 && row->row < cache->rows && row->cells != 0)
  {
    U64 col_count = Min((U64)cache->cols, row->cell_count);
    U64 row_start = (U64)row->row*(U64)cache->cols;
    for(U64 col_idx = 0; col_idx < col_count; col_idx += 1)
    {
      uishell_terminal_cell_copy_from_render_cell(arena, &cache->cells[row_start + col_idx], &row->cells[col_idx]);
    }
  }
}

internal UIShell_TerminalCellFeed
uishell_terminal_cell_feed_from_cache(UIShell_TerminalCellCache *cache)
{
  UIShell_TerminalCellFeed result =
  {
    .cols = cache->cols,
    .rows = cache->rows,
    .cells = cache->cells,
    .cell_count = cache->cell_count,
    .cursor = cache->cursor,
  };
  return result;
}

internal void
uishell_terminal_cell_cache_apply_render_update(UIShell_TerminalCellCache *cache, cleat_render_update const *update)
{
  if(cache != 0 && update != 0 && update->cols != 0 && update->rows != 0)
  {
    if(update->op_count == 0 &&
       cache->cells != 0 &&
       cache->cols == update->cols &&
       cache->rows == update->rows)
    {
      cache->cursor = update->cursor;
      cache->scrollbar = update->scrollbar;
      cache->render_generation = update->render_generation;
      return;
    }

    Arena *new_arena = arena_alloc(.name = "terminal cell cache");
    U64 cell_count = (U64)update->cols*(U64)update->rows;
    cleat_cell *new_cells = push_array(new_arena, cleat_cell, cell_count);
    B32 can_copy_old = (cache->cells != 0 &&
                        cache->cols == update->cols &&
                        cache->rows == update->rows &&
                        cache->cell_count == cell_count);
    B32 has_full_replace = 0;
    for(U64 op_idx = 0; op_idx < update->op_count; op_idx += 1)
    {
      if(update->ops[op_idx].kind == CLEAT_RENDER_OP_FULL_VISIBLE_REPLACE)
      {
        has_full_replace = 1;
        break;
      }
    }
    if(can_copy_old && !has_full_replace)
    {
      for(U64 cell_idx = 0; cell_idx < cell_count; cell_idx += 1)
      {
        uishell_terminal_cell_copy_from_cleat_cell(new_arena, &new_cells[cell_idx], &cache->cells[cell_idx]);
      }
    }

    UIShell_TerminalCellCache new_cache =
    {
      .arena = new_arena,
      .cols = update->cols,
      .rows = update->rows,
      .cells = new_cells,
      .cell_count = cell_count,
      .cursor = update->cursor,
      .scrollbar = update->scrollbar,
      .render_generation = update->render_generation,
    };

    for(U64 op_idx = 0; op_idx < update->op_count; op_idx += 1)
    {
      cleat_render_update_op const *op = &update->ops[op_idx];
      switch(op->kind)
      {
        case CLEAT_RENDER_OP_FULL_VISIBLE_REPLACE:
        case CLEAT_RENDER_OP_ROW_REPLACE:
        {
          if(op->rows != 0)
          {
            for(U64 row_idx = 0; row_idx < op->row_desc_count; row_idx += 1)
            {
              uishell_terminal_cell_cache_apply_render_row(new_arena, &new_cache, &op->rows[row_idx]);
            }
          }
          else if(op->cells != 0)
          {
            U64 first_row = Min((U64)op->first_row, (U64)new_cache.rows);
            U64 row_count = Min((U64)op->row_count, (U64)new_cache.rows - first_row);
            U64 col_count = Min((U64)op->col_count, (U64)new_cache.cols);
            for(U64 row_idx = 0; row_idx < row_count; row_idx += 1)
            {
              for(U64 col_idx = 0; col_idx < col_count; col_idx += 1)
              {
                U64 src_idx = row_idx*(U64)op->col_count + col_idx;
                U64 dst_idx = (first_row + row_idx)*(U64)new_cache.cols + col_idx;
                if(src_idx < op->cell_count && dst_idx < new_cache.cell_count)
                {
                  uishell_terminal_cell_copy_from_render_cell(new_arena, &new_cache.cells[dst_idx], &op->cells[src_idx]);
                }
              }
            }
          }
        }break;
        case CLEAT_RENDER_OP_SCROLL_COPY:
        {
          if(op->src_row < new_cache.rows && op->dst_row < new_cache.rows)
          {
            U64 row_count = Min((U64)op->row_count, (U64)new_cache.rows - Max(op->src_row, op->dst_row));
            for(U64 row_num = 0; row_num < row_count; row_num += 1)
            {
              U64 row_idx = (op->dst_row > op->src_row) ? row_count - 1 - row_num : row_num;
              U64 src_row = (U64)op->src_row + row_idx;
              U64 dst_row = (U64)op->dst_row + row_idx;
              for(U64 col_idx = 0; col_idx < new_cache.cols; col_idx += 1)
              {
                U64 src_idx = src_row*(U64)new_cache.cols + col_idx;
                U64 dst_idx = dst_row*(U64)new_cache.cols + col_idx;
                if(src_idx < new_cache.cell_count && dst_idx < new_cache.cell_count)
                {
                  uishell_terminal_cell_copy_from_cleat_cell(new_arena, &new_cache.cells[dst_idx], &new_cache.cells[src_idx]);
                }
              }
            }
          }
        }break;
      }
    }

    if(cache->arena != 0)
    {
      arena_release(cache->arena);
    }
    *cache = new_cache;
  }
}

internal U64
uishell_terminal_cursor_cols(UIShell_TerminalCellFeed const *feed, U64 cell_count, cleat_cursor cursor)
{
  U64 result = 1;
  if(cursor.visible &&
     cursor.row < feed->rows &&
     cursor.col < feed->cols)
  {
    U64 cell_idx = (U64)cursor.row*(U64)feed->cols + cursor.col;
    if(cell_idx < cell_count)
    {
      cleat_cell const *cell = &feed->cells[cell_idx];
      result = uishell_terminal_cell_display_cols(cell);
      if(cursor.wide_tail || cell->width == CLEAT_CELL_WIDTH_SPACER_TAIL)
      {
        result = 1;
      }
      result = Min(result, (U64)feed->cols - (U64)cursor.col);
    }
  }
  return result;
}

internal B32
uishell_terminal_cursor_cell_is_filled(UIShell_TerminalCellFeed const *feed, U64 cell_count, cleat_cursor cursor, U64 row, U64 col)
{
  U64 cursor_cols = uishell_terminal_cursor_cols(feed, cell_count, cursor);
  B32 result = (uishell_terminal_cursor_is_filled_block(cursor) &&
                cursor.row == row &&
                (U64)cursor.col <= col &&
                col < (U64)cursor.col + cursor_cols);
  return result;
}

internal void
uishell_terminal_cursor_array_from_feed(UIShell_TerminalCellFeed const *feed, UIShell_TerminalCursorArray *cursors)
{
  if(cursors->count == 0 && feed->cursor.visible)
  {
    cursors->v = (cleat_cursor *)&feed->cursor;
    cursors->count = 1;
  }
}

internal B32
uishell_terminal_cursor_array_cell_is_filled(UIShell_TerminalCellFeed const *feed, U64 cell_count, UIShell_TerminalCursorArray cursors, U64 row, U64 col)
{
  uishell_terminal_cursor_array_from_feed(feed, &cursors);
  B32 result = 0;
  for(U64 idx = 0; idx < cursors.count; idx += 1)
  {
    if(uishell_terminal_cursor_cell_is_filled(feed, cell_count, cursors.v[idx], row, col))
    {
      result = 1;
      break;
    }
  }
  return result;
}

internal void
uishell_terminal_draw_cursor_overlay(UIShell_TerminalCellFeed const *feed, U64 cell_count, UIShell_TerminalCursorArray cursors, Rng2F32 canvas_rect, F32 cell_width_px, F32 cell_height_px)
{
  uishell_terminal_cursor_array_from_feed(feed, &cursors);
  for(U64 cursor_idx = 0; cursor_idx < cursors.count; cursor_idx += 1)
  {
    cleat_cursor cursor = cursors.v[cursor_idx];
    if(!cursor.visible ||
       cursor.row >= feed->rows ||
       cursor.col >= feed->cols)
    {
      continue;
    }

    U64 cell_idx = (U64)cursor.row*(U64)feed->cols + cursor.col;
    if(cell_idx < cell_count)
    {
      cleat_cell const *cell = &feed->cells[cell_idx];
      Vec4F32 cursor_color = uishell_terminal_text_color_from_cell(cell);
      U64 cursor_cols = uishell_terminal_cursor_cols(feed, cell_count, cursor);
      Rng2F32 cell_rect =
      {
        floor_f32(canvas_rect.x0 + (F32)cursor.col*cell_width_px),
        floor_f32(canvas_rect.y0 + (F32)cursor.row*cell_height_px),
        ceil_f32(canvas_rect.x0 + (F32)(cursor.col + cursor_cols)*cell_width_px),
        ceil_f32(canvas_rect.y0 + (F32)(cursor.row + 1)*cell_height_px),
      };
      switch(cursor.style)
      {
        default:
        case CLEAT_CURSOR_STYLE_BLOCK:
        {
          // Filled block is drawn by swapping the cursor cell foreground/background.
        }break;
        case CLEAT_CURSOR_STYLE_BAR:
        {
          F32 thickness = Clamp(1.f, floor_f32(cell_width_px*0.16f), 3.f);
          dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, cell_rect.x0 + thickness, cell_rect.y1), cursor_color, 0, 0, 0);
        }break;
        case CLEAT_CURSOR_STYLE_UNDERLINE:
        {
          F32 thickness = Clamp(1.f, floor_f32(cell_height_px*0.14f), 3.f);
          dr_rect(r2f32p(cell_rect.x0, cell_rect.y1 - thickness, cell_rect.x1, cell_rect.y1), cursor_color, 0, 0, 0);
        }break;
        case CLEAT_CURSOR_STYLE_BLOCK_HOLLOW:
        {
          F32 thickness = 1.f;
          dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, cell_rect.x1, cell_rect.y0 + thickness), cursor_color, 0, 0, 0);
          dr_rect(r2f32p(cell_rect.x0, cell_rect.y1 - thickness, cell_rect.x1, cell_rect.y1), cursor_color, 0, 0, 0);
          dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, cell_rect.x0 + thickness, cell_rect.y1), cursor_color, 0, 0, 0);
          dr_rect(r2f32p(cell_rect.x1 - thickness, cell_rect.y0, cell_rect.x1, cell_rect.y1), cursor_color, 0, 0, 0);
        }break;
      }
    }
  }
}

internal void
uishell_terminal_draw_quadrants(Rng2F32 cell_rect, Vec4F32 color, U32 mask)
{
  F32 mid_x = floor_f32((cell_rect.x0 + cell_rect.x1)*0.5f);
  F32 mid_y = floor_f32((cell_rect.y0 + cell_rect.y1)*0.5f);
  if(mask & (1<<0)) { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, mid_x,       mid_y),       color, 0, 0, 0); }
  if(mask & (1<<1)) { dr_rect(r2f32p(mid_x,        cell_rect.y0, cell_rect.x1, mid_y),       color, 0, 0, 0); }
  if(mask & (1<<2)) { dr_rect(r2f32p(cell_rect.x0, mid_y,        mid_x,       cell_rect.y1), color, 0, 0, 0); }
  if(mask & (1<<3)) { dr_rect(r2f32p(mid_x,        mid_y,        cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }
}

internal B32
uishell_terminal_draw_block_element(U32 codepoint, Rng2F32 cell_rect, Vec4F32 color)
{
  B32 result = 1;
  F32 w = dim_2f32(cell_rect).x;
  F32 h = dim_2f32(cell_rect).y;
  switch(codepoint)
  {
    case 0x2580: { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, cell_rect.x1, floor_f32(cell_rect.y0 + h*0.5f)), color, 0, 0, 0); }break;
    case 0x2581: { dr_rect(r2f32p(cell_rect.x0, floor_f32(cell_rect.y1 - h*0.125f), cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2582: { dr_rect(r2f32p(cell_rect.x0, floor_f32(cell_rect.y1 - h*0.250f), cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2583: { dr_rect(r2f32p(cell_rect.x0, floor_f32(cell_rect.y1 - h*0.375f), cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2584: { dr_rect(r2f32p(cell_rect.x0, floor_f32(cell_rect.y1 - h*0.500f), cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2585: { dr_rect(r2f32p(cell_rect.x0, floor_f32(cell_rect.y1 - h*0.625f), cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2586: { dr_rect(r2f32p(cell_rect.x0, floor_f32(cell_rect.y1 - h*0.750f), cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2587: { dr_rect(r2f32p(cell_rect.x0, floor_f32(cell_rect.y1 - h*0.875f), cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2588: { dr_rect(cell_rect, color, 0, 0, 0); }break;
    case 0x2589: { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, floor_f32(cell_rect.x0 + w*0.875f), cell_rect.y1), color, 0, 0, 0); }break;
    case 0x258A: { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, floor_f32(cell_rect.x0 + w*0.750f), cell_rect.y1), color, 0, 0, 0); }break;
    case 0x258B: { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, floor_f32(cell_rect.x0 + w*0.625f), cell_rect.y1), color, 0, 0, 0); }break;
    case 0x258C: { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, floor_f32(cell_rect.x0 + w*0.500f), cell_rect.y1), color, 0, 0, 0); }break;
    case 0x258D: { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, floor_f32(cell_rect.x0 + w*0.375f), cell_rect.y1), color, 0, 0, 0); }break;
    case 0x258E: { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, floor_f32(cell_rect.x0 + w*0.250f), cell_rect.y1), color, 0, 0, 0); }break;
    case 0x258F: { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, floor_f32(cell_rect.x0 + w*0.125f), cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2590: { dr_rect(r2f32p(floor_f32(cell_rect.x1 - w*0.500f), cell_rect.y0, cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2591: { color.w *= 0.25f; dr_rect(cell_rect, color, 0, 0, 0); }break;
    case 0x2592: { color.w *= 0.50f; dr_rect(cell_rect, color, 0, 0, 0); }break;
    case 0x2593: { color.w *= 0.75f; dr_rect(cell_rect, color, 0, 0, 0); }break;
    case 0x2594: { dr_rect(r2f32p(cell_rect.x0, cell_rect.y0, cell_rect.x1, floor_f32(cell_rect.y0 + h*0.125f)), color, 0, 0, 0); }break;
    case 0x2595: { dr_rect(r2f32p(floor_f32(cell_rect.x1 - w*0.125f), cell_rect.y0, cell_rect.x1, cell_rect.y1), color, 0, 0, 0); }break;
    case 0x2596: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<2)); }break;
    case 0x2597: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<3)); }break;
    case 0x2598: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<0)); }break;
    case 0x2599: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<0)|(1<<2)|(1<<3)); }break;
    case 0x259A: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<0)|(1<<3)); }break;
    case 0x259B: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<0)|(1<<1)|(1<<2)); }break;
    case 0x259C: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<0)|(1<<1)|(1<<3)); }break;
    case 0x259D: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<1)); }break;
    case 0x259E: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<1)|(1<<2)); }break;
    case 0x259F: { uishell_terminal_draw_quadrants(cell_rect, color, (1<<1)|(1<<2)|(1<<3)); }break;
    default: { result = 0; }break;
  }
  return result;
}

internal B32
uishell_terminal_draw_braille_pattern(U32 codepoint, Rng2F32 cell_rect, Vec4F32 color)
{
  B32 result = 0;
  if(0x2800 <= codepoint && codepoint <= 0x28FF)
  {
    result = 1;
    U32 pattern = codepoint - 0x2800;
    S32 width = (S32)floor_f32(dim_2f32(cell_rect).x);
    S32 height = (S32)floor_f32(dim_2f32(cell_rect).y);
    S32 dot_w = Min(width/4, height/8);
    S32 x_spacing = width/4;
    S32 y_spacing = height/8;
    S32 x_margin = x_spacing/2;
    S32 y_margin = y_spacing/2;
    S32 x_px_left = width - 2*x_margin - x_spacing - 2*dot_w;
    S32 y_px_left = height - 2*y_margin - 3*y_spacing - 4*dot_w;

    if(x_px_left >= 2 && y_px_left >= 4 && dot_w == 0)
    {
      dot_w += 1;
      x_px_left -= 2;
      y_px_left -= 4;
    }
    if(x_px_left >= 2 && x_margin == 0)
    {
      x_margin += 1;
      x_px_left -= 2;
    }
    if(y_px_left >= 2 && y_margin == 0)
    {
      y_margin += 1;
      y_px_left -= 2;
    }
    if(x_px_left >= 1)
    {
      x_spacing += 1;
      x_px_left -= 1;
    }
    if(y_px_left >= 3)
    {
      y_spacing += 1;
      y_px_left -= 3;
    }
    if(x_px_left >= 2)
    {
      x_margin += 1;
      x_px_left -= 2;
    }
    if(y_px_left >= 2)
    {
      y_margin += 1;
      y_px_left -= 2;
    }
    if(x_px_left >= 2 && y_px_left >= 4)
    {
      dot_w += 1;
      x_px_left -= 2;
      y_px_left -= 4;
    }

    S32 x[2] =
    {
      x_margin,
      x_margin + dot_w + x_spacing,
    };
    S32 y[4] =
    {
      y_margin,
      y_margin + dot_w + y_spacing,
      y_margin + 2*dot_w + 2*y_spacing,
      y_margin + 3*dot_w + 3*y_spacing,
    };
    U8 bit_from_dot_idx[8] = {0x01, 0x08, 0x02, 0x10, 0x04, 0x20, 0x40, 0x80};
    for(U64 dot_idx = 0; dot_idx < ArrayCount(bit_from_dot_idx); dot_idx += 1)
    {
      if(pattern & bit_from_dot_idx[dot_idx])
      {
        U64 col = dot_idx%2;
        U64 row = dot_idx/2;
        F32 x0 = cell_rect.x0 + (F32)x[col];
        F32 y0 = cell_rect.y0 + (F32)y[row];
        dr_rect(r2f32p(x0, y0, x0 + (F32)dot_w, y0 + (F32)dot_w), color, 0, 0, 0);
      }
    }
  }
  return result;
}

typedef U8 UIShell_TerminalLineStyle;
enum
{
  UIShell_TerminalLineStyle_None,
  UIShell_TerminalLineStyle_Light,
  UIShell_TerminalLineStyle_Heavy,
  UIShell_TerminalLineStyle_Double,
};

typedef struct UIShell_TerminalBoxLines UIShell_TerminalBoxLines;
struct UIShell_TerminalBoxLines
{
  UIShell_TerminalLineStyle up;
  UIShell_TerminalLineStyle right;
  UIShell_TerminalLineStyle down;
  UIShell_TerminalLineStyle left;
};

internal F32
uishell_terminal_line_thickness_from_style(UIShell_TerminalLineStyle style, F32 cell_min)
{
  F32 light = ClampBot(1.f, floor_f32(cell_min*0.10f));
  F32 result = light;
  if(style == UIShell_TerminalLineStyle_Heavy)
  {
    result = Max(light + 1.f, floor_f32(cell_min*0.18f));
  }
  return result;
}

internal void
uishell_terminal_draw_hline_segment(Rng2F32 cell_rect, F32 x0, F32 x1, UIShell_TerminalLineStyle style, Vec4F32 color)
{
  F32 cell_min = Min(dim_2f32(cell_rect).x, dim_2f32(cell_rect).y);
  F32 cy = floor_f32((cell_rect.y0 + cell_rect.y1)*0.5f);
  F32 t = uishell_terminal_line_thickness_from_style(style, cell_min);
  switch(style)
  {
    case UIShell_TerminalLineStyle_Light:
    case UIShell_TerminalLineStyle_Heavy:
    {
      dr_rect(r2f32p(x0, cy - floor_f32(t*0.5f), x1, cy + ceil_f32(t*0.5f)), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Double:
    {
      F32 gap = Max(1.f, t);
      dr_rect(r2f32p(x0, cy - gap - t, x1, cy - gap), color, 0, 0, 0);
      dr_rect(r2f32p(x0, cy + gap,     x1, cy + gap + t), color, 0, 0, 0);
    }break;
  }
}

internal void
uishell_terminal_draw_vline_segment(Rng2F32 cell_rect, F32 y0, F32 y1, UIShell_TerminalLineStyle style, Vec4F32 color)
{
  F32 cell_min = Min(dim_2f32(cell_rect).x, dim_2f32(cell_rect).y);
  F32 cx = floor_f32((cell_rect.x0 + cell_rect.x1)*0.5f);
  F32 t = uishell_terminal_line_thickness_from_style(style, cell_min);
  switch(style)
  {
    case UIShell_TerminalLineStyle_Light:
    case UIShell_TerminalLineStyle_Heavy:
    {
      dr_rect(r2f32p(cx - floor_f32(t*0.5f), y0, cx + ceil_f32(t*0.5f), y1), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Double:
    {
      F32 gap = Max(1.f, t);
      dr_rect(r2f32p(cx - gap - t, y0, cx - gap,     y1), color, 0, 0, 0);
      dr_rect(r2f32p(cx + gap,     y0, cx + gap + t, y1), color, 0, 0, 0);
    }break;
  }
}

internal void
uishell_terminal_draw_box_lines(UIShell_TerminalBoxLines lines, Rng2F32 cell_rect, Vec4F32 color)
{
  F32 w = dim_2f32(cell_rect).x;
  F32 h = dim_2f32(cell_rect).y;
  F32 cell_min = Min(w, h);
  F32 light_px = uishell_terminal_line_thickness_from_style(UIShell_TerminalLineStyle_Light, cell_min);
  F32 heavy_px = uishell_terminal_line_thickness_from_style(UIShell_TerminalLineStyle_Heavy, cell_min);

  F32 h_light_top = cell_rect.y0 + floor_f32((h - light_px)*0.5f);
  F32 h_light_bottom = h_light_top + light_px;
  F32 h_heavy_top = cell_rect.y0 + floor_f32((h - heavy_px)*0.5f);
  F32 h_heavy_bottom = h_heavy_top + heavy_px;
  F32 h_double_top = h_light_top - light_px;
  F32 h_double_bottom = h_light_bottom + light_px;

  F32 v_light_left = cell_rect.x0 + floor_f32((w - light_px)*0.5f);
  F32 v_light_right = v_light_left + light_px;
  F32 v_heavy_left = cell_rect.x0 + floor_f32((w - heavy_px)*0.5f);
  F32 v_heavy_right = v_heavy_left + heavy_px;
  F32 v_double_left = v_light_left - light_px;
  F32 v_double_right = v_light_right + light_px;

  F32 up_bottom = h_light_bottom;
  if(lines.left == UIShell_TerminalLineStyle_Heavy || lines.right == UIShell_TerminalLineStyle_Heavy)
  {
    up_bottom = h_heavy_bottom;
  }
  else if(lines.left != lines.right || lines.down == lines.up)
  {
    up_bottom = (lines.left == UIShell_TerminalLineStyle_Double || lines.right == UIShell_TerminalLineStyle_Double) ? h_double_bottom : h_light_bottom;
  }
  else if(!(lines.left == UIShell_TerminalLineStyle_None && lines.right == UIShell_TerminalLineStyle_None))
  {
    up_bottom = h_light_top;
  }

  F32 down_top = h_light_top;
  if(lines.left == UIShell_TerminalLineStyle_Heavy || lines.right == UIShell_TerminalLineStyle_Heavy)
  {
    down_top = h_heavy_top;
  }
  else if(lines.left != lines.right || lines.up == lines.down)
  {
    down_top = (lines.left == UIShell_TerminalLineStyle_Double || lines.right == UIShell_TerminalLineStyle_Double) ? h_double_top : h_light_top;
  }
  else if(!(lines.left == UIShell_TerminalLineStyle_None && lines.right == UIShell_TerminalLineStyle_None))
  {
    down_top = h_light_bottom;
  }

  F32 left_right = v_light_right;
  if(lines.up == UIShell_TerminalLineStyle_Heavy || lines.down == UIShell_TerminalLineStyle_Heavy)
  {
    left_right = v_heavy_right;
  }
  else if(lines.up != lines.down || lines.left == lines.right)
  {
    left_right = (lines.up == UIShell_TerminalLineStyle_Double || lines.down == UIShell_TerminalLineStyle_Double) ? v_double_right : v_light_right;
  }
  else if(!(lines.up == UIShell_TerminalLineStyle_None && lines.down == UIShell_TerminalLineStyle_None))
  {
    left_right = v_light_left;
  }

  F32 right_left = v_light_left;
  if(lines.up == UIShell_TerminalLineStyle_Heavy || lines.down == UIShell_TerminalLineStyle_Heavy)
  {
    right_left = v_heavy_left;
  }
  else if(lines.up != lines.down || lines.right == lines.left)
  {
    right_left = (lines.up == UIShell_TerminalLineStyle_Double || lines.down == UIShell_TerminalLineStyle_Double) ? v_double_left : v_light_left;
  }
  else if(!(lines.up == UIShell_TerminalLineStyle_None && lines.down == UIShell_TerminalLineStyle_None))
  {
    right_left = v_light_right;
  }

  switch(lines.up)
  {
    case UIShell_TerminalLineStyle_Light:
    {
      dr_rect(r2f32p(v_light_left, cell_rect.y0, v_light_right, up_bottom), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Heavy:
    {
      dr_rect(r2f32p(v_heavy_left, cell_rect.y0, v_heavy_right, up_bottom), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Double:
    {
      F32 left_bottom = (lines.left == UIShell_TerminalLineStyle_Double) ? h_light_top : up_bottom;
      F32 right_bottom = (lines.right == UIShell_TerminalLineStyle_Double) ? h_light_top : up_bottom;
      dr_rect(r2f32p(v_double_left, cell_rect.y0, v_light_left, left_bottom), color, 0, 0, 0);
      dr_rect(r2f32p(v_light_right, cell_rect.y0, v_double_right, right_bottom), color, 0, 0, 0);
    }break;
  }

  switch(lines.right)
  {
    case UIShell_TerminalLineStyle_Light:
    {
      dr_rect(r2f32p(right_left, h_light_top, cell_rect.x1, h_light_bottom), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Heavy:
    {
      dr_rect(r2f32p(right_left, h_heavy_top, cell_rect.x1, h_heavy_bottom), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Double:
    {
      F32 top_left = (lines.up == UIShell_TerminalLineStyle_Double) ? v_light_right : right_left;
      F32 bottom_left = (lines.down == UIShell_TerminalLineStyle_Double) ? v_light_right : right_left;
      dr_rect(r2f32p(top_left, h_double_top, cell_rect.x1, h_light_top), color, 0, 0, 0);
      dr_rect(r2f32p(bottom_left, h_light_bottom, cell_rect.x1, h_double_bottom), color, 0, 0, 0);
    }break;
  }

  switch(lines.down)
  {
    case UIShell_TerminalLineStyle_Light:
    {
      dr_rect(r2f32p(v_light_left, down_top, v_light_right, cell_rect.y1), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Heavy:
    {
      dr_rect(r2f32p(v_heavy_left, down_top, v_heavy_right, cell_rect.y1), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Double:
    {
      F32 left_top = (lines.left == UIShell_TerminalLineStyle_Double) ? h_light_bottom : down_top;
      F32 right_top = (lines.right == UIShell_TerminalLineStyle_Double) ? h_light_bottom : down_top;
      dr_rect(r2f32p(v_double_left, left_top, v_light_left, cell_rect.y1), color, 0, 0, 0);
      dr_rect(r2f32p(v_light_right, right_top, v_double_right, cell_rect.y1), color, 0, 0, 0);
    }break;
  }

  switch(lines.left)
  {
    case UIShell_TerminalLineStyle_Light:
    {
      dr_rect(r2f32p(cell_rect.x0, h_light_top, left_right, h_light_bottom), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Heavy:
    {
      dr_rect(r2f32p(cell_rect.x0, h_heavy_top, left_right, h_heavy_bottom), color, 0, 0, 0);
    }break;
    case UIShell_TerminalLineStyle_Double:
    {
      F32 top_right = (lines.up == UIShell_TerminalLineStyle_Double) ? v_light_left : left_right;
      F32 bottom_right = (lines.down == UIShell_TerminalLineStyle_Double) ? v_light_left : left_right;
      dr_rect(r2f32p(cell_rect.x0, h_double_top, top_right, h_light_top), color, 0, 0, 0);
      dr_rect(r2f32p(cell_rect.x0, h_light_bottom, bottom_right, h_double_bottom), color, 0, 0, 0);
    }break;
  }
}

internal void
uishell_terminal_draw_dashed_line(U32 codepoint, Rng2F32 cell_rect, Vec4F32 color)
{
  F32 w = dim_2f32(cell_rect).x;
  F32 h = dim_2f32(cell_rect).y;
  B32 horizontal = (codepoint == 0x2504 || codepoint == 0x2505 || codepoint == 0x2508 || codepoint == 0x2509 || codepoint == 0x254C || codepoint == 0x254D);
  B32 heavy = (codepoint == 0x2505 || codepoint == 0x2507 || codepoint == 0x2509 || codepoint == 0x250B || codepoint == 0x254D || codepoint == 0x254F);
  U64 dash_count = (codepoint == 0x2508 || codepoint == 0x2509 || codepoint == 0x250A || codepoint == 0x250B) ? 4 : (codepoint == 0x254C || codepoint == 0x254D || codepoint == 0x254E || codepoint == 0x254F) ? 2 : 3;
  UIShell_TerminalLineStyle style = heavy ? UIShell_TerminalLineStyle_Heavy : UIShell_TerminalLineStyle_Light;
  for(U64 idx = 0; idx < dash_count; idx += 1)
  {
    F32 a = (F32)idx/(F32)dash_count;
    F32 b = (F32)(idx + 1)/(F32)dash_count;
    F32 pad = 0.08f;
    if(horizontal)
    {
      F32 x0 = floor_f32(cell_rect.x0 + w*(a + pad/(F32)dash_count));
      F32 x1 = ceil_f32(cell_rect.x0 + w*(b - pad/(F32)dash_count));
      uishell_terminal_draw_hline_segment(cell_rect, x0, x1, style, color);
    }
    else
    {
      F32 y0 = floor_f32(cell_rect.y0 + h*(a + pad/(F32)dash_count));
      F32 y1 = ceil_f32(cell_rect.y0 + h*(b - pad/(F32)dash_count));
      uishell_terminal_draw_vline_segment(cell_rect, y0, y1, style, color);
    }
  }
}

internal B32
uishell_terminal_box_lines_from_codepoint(U32 codepoint, UIShell_TerminalBoxLines *lines_out)
{
#define L UIShell_TerminalLineStyle_Light
#define H UIShell_TerminalLineStyle_Heavy
#define D UIShell_TerminalLineStyle_Double
  B32 result = 1;
  UIShell_TerminalBoxLines lines = {0};
  switch(codepoint)
  {
    case 0x2500: { lines = (UIShell_TerminalBoxLines){0, L, 0, L}; }break;
    case 0x2501: { lines = (UIShell_TerminalBoxLines){0, H, 0, H}; }break;
    case 0x2502: { lines = (UIShell_TerminalBoxLines){L, 0, L, 0}; }break;
    case 0x2503: { lines = (UIShell_TerminalBoxLines){H, 0, H, 0}; }break;
    case 0x250C: { lines = (UIShell_TerminalBoxLines){0, L, L, 0}; }break;
    case 0x250D: { lines = (UIShell_TerminalBoxLines){0, H, L, 0}; }break;
    case 0x250E: { lines = (UIShell_TerminalBoxLines){0, L, H, 0}; }break;
    case 0x250F: { lines = (UIShell_TerminalBoxLines){0, H, H, 0}; }break;
    case 0x2510: { lines = (UIShell_TerminalBoxLines){0, 0, L, L}; }break;
    case 0x2511: { lines = (UIShell_TerminalBoxLines){0, 0, L, H}; }break;
    case 0x2512: { lines = (UIShell_TerminalBoxLines){0, 0, H, L}; }break;
    case 0x2513: { lines = (UIShell_TerminalBoxLines){0, 0, H, H}; }break;
    case 0x2514: { lines = (UIShell_TerminalBoxLines){L, L, 0, 0}; }break;
    case 0x2515: { lines = (UIShell_TerminalBoxLines){L, H, 0, 0}; }break;
    case 0x2516: { lines = (UIShell_TerminalBoxLines){H, L, 0, 0}; }break;
    case 0x2517: { lines = (UIShell_TerminalBoxLines){H, H, 0, 0}; }break;
    case 0x2518: { lines = (UIShell_TerminalBoxLines){L, 0, 0, L}; }break;
    case 0x2519: { lines = (UIShell_TerminalBoxLines){L, 0, 0, H}; }break;
    case 0x251A: { lines = (UIShell_TerminalBoxLines){H, 0, 0, L}; }break;
    case 0x251B: { lines = (UIShell_TerminalBoxLines){H, 0, 0, H}; }break;
    case 0x251C: { lines = (UIShell_TerminalBoxLines){L, L, L, 0}; }break;
    case 0x251D: { lines = (UIShell_TerminalBoxLines){L, H, L, 0}; }break;
    case 0x251E: { lines = (UIShell_TerminalBoxLines){H, L, L, 0}; }break;
    case 0x251F: { lines = (UIShell_TerminalBoxLines){L, L, H, 0}; }break;
    case 0x2520: { lines = (UIShell_TerminalBoxLines){H, L, H, 0}; }break;
    case 0x2521: { lines = (UIShell_TerminalBoxLines){H, H, L, 0}; }break;
    case 0x2522: { lines = (UIShell_TerminalBoxLines){L, H, H, 0}; }break;
    case 0x2523: { lines = (UIShell_TerminalBoxLines){H, H, H, 0}; }break;
    case 0x2524: { lines = (UIShell_TerminalBoxLines){L, 0, L, L}; }break;
    case 0x2525: { lines = (UIShell_TerminalBoxLines){L, 0, L, H}; }break;
    case 0x2526: { lines = (UIShell_TerminalBoxLines){H, 0, L, L}; }break;
    case 0x2527: { lines = (UIShell_TerminalBoxLines){L, 0, H, L}; }break;
    case 0x2528: { lines = (UIShell_TerminalBoxLines){H, 0, H, L}; }break;
    case 0x2529: { lines = (UIShell_TerminalBoxLines){H, 0, L, H}; }break;
    case 0x252A: { lines = (UIShell_TerminalBoxLines){L, 0, H, H}; }break;
    case 0x252B: { lines = (UIShell_TerminalBoxLines){H, 0, H, H}; }break;
    case 0x252C: { lines = (UIShell_TerminalBoxLines){0, L, L, L}; }break;
    case 0x252D: { lines = (UIShell_TerminalBoxLines){0, L, L, H}; }break;
    case 0x252E: { lines = (UIShell_TerminalBoxLines){0, H, L, L}; }break;
    case 0x252F: { lines = (UIShell_TerminalBoxLines){0, H, L, H}; }break;
    case 0x2530: { lines = (UIShell_TerminalBoxLines){0, L, H, L}; }break;
    case 0x2531: { lines = (UIShell_TerminalBoxLines){0, L, H, H}; }break;
    case 0x2532: { lines = (UIShell_TerminalBoxLines){0, H, H, L}; }break;
    case 0x2533: { lines = (UIShell_TerminalBoxLines){0, H, H, H}; }break;
    case 0x2534: { lines = (UIShell_TerminalBoxLines){L, L, 0, L}; }break;
    case 0x2535: { lines = (UIShell_TerminalBoxLines){L, L, 0, H}; }break;
    case 0x2536: { lines = (UIShell_TerminalBoxLines){L, H, 0, L}; }break;
    case 0x2537: { lines = (UIShell_TerminalBoxLines){L, H, 0, H}; }break;
    case 0x2538: { lines = (UIShell_TerminalBoxLines){H, L, 0, L}; }break;
    case 0x2539: { lines = (UIShell_TerminalBoxLines){H, L, 0, H}; }break;
    case 0x253A: { lines = (UIShell_TerminalBoxLines){H, H, 0, L}; }break;
    case 0x253B: { lines = (UIShell_TerminalBoxLines){H, H, 0, H}; }break;
    case 0x253C: { lines = (UIShell_TerminalBoxLines){L, L, L, L}; }break;
    case 0x253D: { lines = (UIShell_TerminalBoxLines){L, L, L, H}; }break;
    case 0x253E: { lines = (UIShell_TerminalBoxLines){L, H, L, L}; }break;
    case 0x253F: { lines = (UIShell_TerminalBoxLines){L, H, L, H}; }break;
    case 0x2540: { lines = (UIShell_TerminalBoxLines){H, L, L, L}; }break;
    case 0x2541: { lines = (UIShell_TerminalBoxLines){L, L, H, L}; }break;
    case 0x2542: { lines = (UIShell_TerminalBoxLines){H, L, H, L}; }break;
    case 0x2543: { lines = (UIShell_TerminalBoxLines){H, L, L, H}; }break;
    case 0x2544: { lines = (UIShell_TerminalBoxLines){H, H, L, L}; }break;
    case 0x2545: { lines = (UIShell_TerminalBoxLines){L, L, H, H}; }break;
    case 0x2546: { lines = (UIShell_TerminalBoxLines){L, H, H, L}; }break;
    case 0x2547: { lines = (UIShell_TerminalBoxLines){H, H, L, H}; }break;
    case 0x2548: { lines = (UIShell_TerminalBoxLines){L, H, H, H}; }break;
    case 0x2549: { lines = (UIShell_TerminalBoxLines){H, L, H, H}; }break;
    case 0x254A: { lines = (UIShell_TerminalBoxLines){H, H, H, L}; }break;
    case 0x254B: { lines = (UIShell_TerminalBoxLines){H, H, H, H}; }break;
    case 0x2550: { lines = (UIShell_TerminalBoxLines){0, D, 0, D}; }break;
    case 0x2551: { lines = (UIShell_TerminalBoxLines){D, 0, D, 0}; }break;
    case 0x2552: { lines = (UIShell_TerminalBoxLines){0, D, L, 0}; }break;
    case 0x2553: { lines = (UIShell_TerminalBoxLines){0, L, D, 0}; }break;
    case 0x2554: { lines = (UIShell_TerminalBoxLines){0, D, D, 0}; }break;
    case 0x2555: { lines = (UIShell_TerminalBoxLines){0, 0, L, D}; }break;
    case 0x2556: { lines = (UIShell_TerminalBoxLines){0, 0, D, L}; }break;
    case 0x2557: { lines = (UIShell_TerminalBoxLines){0, 0, D, D}; }break;
    case 0x2558: { lines = (UIShell_TerminalBoxLines){L, D, 0, 0}; }break;
    case 0x2559: { lines = (UIShell_TerminalBoxLines){D, L, 0, 0}; }break;
    case 0x255A: { lines = (UIShell_TerminalBoxLines){D, D, 0, 0}; }break;
    case 0x255B: { lines = (UIShell_TerminalBoxLines){L, 0, 0, D}; }break;
    case 0x255C: { lines = (UIShell_TerminalBoxLines){D, 0, 0, L}; }break;
    case 0x255D: { lines = (UIShell_TerminalBoxLines){D, 0, 0, D}; }break;
    case 0x255E: { lines = (UIShell_TerminalBoxLines){L, D, L, 0}; }break;
    case 0x255F: { lines = (UIShell_TerminalBoxLines){D, L, D, 0}; }break;
    case 0x2560: { lines = (UIShell_TerminalBoxLines){D, D, D, 0}; }break;
    case 0x2561: { lines = (UIShell_TerminalBoxLines){L, 0, L, D}; }break;
    case 0x2562: { lines = (UIShell_TerminalBoxLines){D, 0, D, L}; }break;
    case 0x2563: { lines = (UIShell_TerminalBoxLines){D, 0, D, D}; }break;
    case 0x2564: { lines = (UIShell_TerminalBoxLines){0, D, L, D}; }break;
    case 0x2565: { lines = (UIShell_TerminalBoxLines){0, L, D, L}; }break;
    case 0x2566: { lines = (UIShell_TerminalBoxLines){0, D, D, D}; }break;
    case 0x2567: { lines = (UIShell_TerminalBoxLines){L, D, 0, D}; }break;
    case 0x2568: { lines = (UIShell_TerminalBoxLines){D, L, 0, L}; }break;
    case 0x2569: { lines = (UIShell_TerminalBoxLines){D, D, 0, D}; }break;
    case 0x256A: { lines = (UIShell_TerminalBoxLines){L, D, L, D}; }break;
    case 0x256B: { lines = (UIShell_TerminalBoxLines){D, L, D, L}; }break;
    case 0x256C: { lines = (UIShell_TerminalBoxLines){D, D, D, D}; }break;
    case 0x2574: { lines = (UIShell_TerminalBoxLines){0, 0, 0, L}; }break;
    case 0x2575: { lines = (UIShell_TerminalBoxLines){L, 0, 0, 0}; }break;
    case 0x2576: { lines = (UIShell_TerminalBoxLines){0, L, 0, 0}; }break;
    case 0x2577: { lines = (UIShell_TerminalBoxLines){0, 0, L, 0}; }break;
    case 0x2578: { lines = (UIShell_TerminalBoxLines){0, 0, 0, H}; }break;
    case 0x2579: { lines = (UIShell_TerminalBoxLines){H, 0, 0, 0}; }break;
    case 0x257A: { lines = (UIShell_TerminalBoxLines){0, H, 0, 0}; }break;
    case 0x257B: { lines = (UIShell_TerminalBoxLines){0, 0, H, 0}; }break;
    case 0x257C: { lines = (UIShell_TerminalBoxLines){0, H, 0, L}; }break;
    case 0x257D: { lines = (UIShell_TerminalBoxLines){L, 0, H, 0}; }break;
    case 0x257E: { lines = (UIShell_TerminalBoxLines){0, L, 0, H}; }break;
    case 0x257F: { lines = (UIShell_TerminalBoxLines){H, 0, L, 0}; }break;
    default: { result = 0; }break;
  }
  *lines_out = lines;
#undef L
#undef H
#undef D
  return result;
}

internal B32
uishell_terminal_draw_box_drawing(U32 codepoint, Rng2F32 cell_rect, Vec4F32 color)
{
  B32 result = 0;
  if((0x2504 <= codepoint && codepoint <= 0x250B) ||
     (0x254C <= codepoint && codepoint <= 0x254F))
  {
    uishell_terminal_draw_dashed_line(codepoint, cell_rect, color);
    result = 1;
  }
  else
  {
    UIShell_TerminalBoxLines lines = {0};
    if(uishell_terminal_box_lines_from_codepoint(codepoint, &lines))
    {
      uishell_terminal_draw_box_lines(lines, cell_rect, color);
      result = 1;
    }
  }
  return result;
}

internal B32
uishell_terminal_codepoint_is_block_element(U32 codepoint)
{
  B32 result = (0x2580 <= codepoint && codepoint <= 0x259F);
  return result;
}

internal B32
uishell_terminal_codepoint_is_direct_sprite(U32 codepoint)
{
  B32 result = 0;
  if((0x2504 <= codepoint && codepoint <= 0x250B) ||
     (0x254C <= codepoint && codepoint <= 0x254F))
  {
    result = 1;
  }
  else
  {
    UIShell_TerminalBoxLines lines = {0};
    result = (uishell_terminal_box_lines_from_codepoint(codepoint, &lines) ||
              uishell_terminal_codepoint_is_block_element(codepoint) ||
              (0x2800 <= codepoint && codepoint <= 0x28FF));
  }
  return result;
}

internal B32
uishell_terminal_draw_direct_sprite(U32 codepoint, Rng2F32 cell_rect, Vec4F32 color)
{
  B32 result = (uishell_terminal_draw_box_drawing(codepoint, cell_rect, color) ||
                uishell_terminal_draw_block_element(codepoint, cell_rect, color) ||
                uishell_terminal_draw_braille_pattern(codepoint, cell_rect, color));
  return result;
}

internal B32
uishell_terminal_vec4_match(Vec4F32 a, Vec4F32 b)
{
  B32 result = (a.x == b.x && a.y == b.y && a.z == b.z && a.w == b.w);
  return result;
}

internal B32
uishell_terminal_codepoint_is_simple_text_run(U32 codepoint)
{
  B32 result = (0x20 <= codepoint && codepoint <= 0x7E);
  return result;
}

internal void
uishell_terminal_draw_text_in_cell(FNT_Tag font, F32 size, FNT_RasterFlags flags, Rng2F32 cell_rect, Vec4F32 color, String8 string)
{
  FNT_Run run = dr_fnt_run_from_string(font, size, 0, 0, flags, string);
  if(run.pieces.count != 0)
  {
    Rng2F32 bounds = r2f32p(inf32(), inf32(), -inf32(), -inf32());
    F32 advance = 0;
    for(U64 piece_idx = 0; piece_idx < run.pieces.count; piece_idx += 1)
    {
      FNT_Piece *piece = &run.pieces.v[piece_idx];
      if(piece->draw_dim.x != 0 && piece->draw_dim.y != 0 && !r_handle_match(piece->texture, r_handle_zero()))
      {
        Rng2F32 piece_rect = r2f32p(piece->offset.x + advance,
                                    piece->offset.y,
                                    piece->offset.x + advance + piece->draw_dim.x,
                                    piece->offset.y + piece->draw_dim.y);
        bounds.x0 = Min(bounds.x0, piece_rect.x0);
        bounds.y0 = Min(bounds.y0, piece_rect.y0);
        bounds.x1 = Max(bounds.x1, piece_rect.x1);
        bounds.y1 = Max(bounds.y1, piece_rect.y1);
      }
      advance += piece->advance;
    }

    if(bounds.x0 != inf32())
    {
      Vec2F32 cell_center = center_2f32(cell_rect);
      Vec2F32 bounds_center = center_2f32(bounds);
      Vec2F32 p = v2f32(floor_f32(cell_center.x - bounds_center.x),
                         floor_f32(cell_center.y - bounds_center.y));
      dr_text_run(p, color, run);
    }
  }
}

internal B32
uishell_terminal_cell_text_run_info(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalCellFeed const *feed, U64 cell_count, UIShell_TerminalCursorArray cursors, U64 row_idx, U64 col_idx, FNT_Tag *font_out, Vec4F32 *fg_out, String8 *string_out)
{
  B32 result = 0;
  U64 cell_idx = row_idx*(U64)feed->cols + col_idx;
  if(cell_idx < cell_count)
  {
    cleat_cell const *cell = &feed->cells[cell_idx];
    if(cell->width == CLEAT_CELL_WIDTH_NARROW &&
       cell->grapheme_count == 1 &&
       uishell_terminal_codepoint_is_simple_text_run(cell->graphemes[0]) &&
       !(cell->flags & (CLEAT_CELL_FLAG_INVISIBLE|
                        CLEAT_CELL_FLAG_BOLD|
                        CLEAT_CELL_FLAG_UNDERLINE|
                        CLEAT_CELL_FLAG_STRIKETHROUGH|
                        CLEAT_CELL_FLAG_OVERLINE)) &&
       !uishell_terminal_cursor_array_cell_is_filled(feed, cell_count, cursors, row_idx, col_idx) &&
       !uishell_terminal_codepoint_is_direct_sprite(cell->graphemes[0]))
    {
      *font_out = uishell_terminal_font_from_cell(renderer, cell);
      if(!fnt_tag_match(*font_out, fnt_tag_zero()))
      {
        *fg_out = uishell_terminal_text_color_from_cell(cell);
        *string_out = uishell_terminal_string_from_cell(arena, cell);
        result = 1;
      }
    }
  }
  return result;
}

internal cleat_rgb
uishell_terminal_fixture_rgb(U8 r, U8 g, U8 b)
{
  cleat_rgb result = {r, g, b};
  return result;
}

internal void
uishell_terminal_fixture_clear(cleat_snapshot *snapshot, cleat_cell *cells, U32 *graphemes)
{
  U64 cell_count = (U64)snapshot->cols*(U64)snapshot->rows;
  cleat_rgb fg = uishell_terminal_fixture_rgb(188, 209, 191);
  cleat_rgb bg = uishell_terminal_fixture_rgb(4, 4, 4);
  for(U64 idx = 0; idx < cell_count; idx += 1)
  {
    U32 *cell_graphemes = graphemes + idx*4;
    cell_graphemes[0] = ' ';
    cells[idx].graphemes = cell_graphemes;
    cells[idx].grapheme_count = 1;
    cells[idx].fg = fg;
    cells[idx].bg = bg;
    cells[idx].flags = 0;
    cells[idx].width = CLEAT_CELL_WIDTH_NARROW;
  }
}

internal void
uishell_terminal_fixture_put_cell(cleat_snapshot *snapshot, cleat_cell *cells, U32 *graphemes, U64 row, U64 col, U32 *codepoints, U64 codepoint_count, cleat_rgb fg, cleat_rgb bg, U32 flags, U32 width)
{
  if(row < snapshot->rows && col < snapshot->cols)
  {
    U64 cell_idx = row*(U64)snapshot->cols + col;
    U32 *cell_graphemes = graphemes + cell_idx*4;
    U64 capped_count = Clamp(1, codepoint_count, 4);
    for(U64 idx = 0; idx < capped_count; idx += 1)
    {
      cell_graphemes[idx] = codepoints[idx];
    }
    cells[cell_idx].graphemes = cell_graphemes;
    cells[cell_idx].grapheme_count = capped_count;
    cells[cell_idx].fg = fg;
    cells[cell_idx].bg = bg;
    cells[cell_idx].flags = flags;
    cells[cell_idx].width = width;
  }
}

internal void
uishell_terminal_fixture_put_cp(cleat_snapshot *snapshot, cleat_cell *cells, U32 *graphemes, U64 row, U64 col, U32 codepoint, cleat_rgb fg, cleat_rgb bg, U32 flags)
{
  U32 codepoints[1] = {codepoint};
  uishell_terminal_fixture_put_cell(snapshot, cells, graphemes, row, col, codepoints, 1, fg, bg, flags, CLEAT_CELL_WIDTH_NARROW);
}

internal U64
uishell_terminal_fixture_put_string(cleat_snapshot *snapshot, cleat_cell *cells, U32 *graphemes, U64 row, U64 col, String8 string, cleat_rgb fg, cleat_rgb bg, U32 flags)
{
  U64 x = col;
  for(U64 idx = 0; idx < string.size && x < snapshot->cols;)
  {
    UnicodeDecode decode = utf8_decode(string.str + idx, string.size - idx);
    U32 codepoint = decode.codepoint;
    U64 advance = Max(1, decode.inc);
    idx += advance;
    if(codepoint == 0 || codepoint == max_U32)
    {
      codepoint = '?';
    }
    uishell_terminal_fixture_put_cp(snapshot, cells, graphemes, row, x, codepoint, fg, bg, flags);
    x += 1;
  }
  return x;
}

internal void
uishell_terminal_fixture_put_codepoints(cleat_snapshot *snapshot, cleat_cell *cells, U32 *graphemes, U64 row, U64 col, U32 *codepoints, U64 codepoint_count, cleat_rgb fg, cleat_rgb bg, U32 flags)
{
  for(U64 idx = 0; idx < codepoint_count && col + idx < snapshot->cols; idx += 1)
  {
    uishell_terminal_fixture_put_cp(snapshot, cells, graphemes, row, col + idx, codepoints[idx], fg, bg, flags);
  }
}

internal void
uishell_terminal_fixture_cursor_array_push(UIShell_TerminalCursorArray *array, U16 cols, U16 rows, U64 col, U64 row, U32 style, B32 wide_tail)
{
  if(row < rows && col < cols)
  {
    cleat_cursor *cursor = &array->v[array->count];
    cursor->col = (U16)col;
    cursor->row = (U16)row;
    cursor->visible = 1;
    cursor->style = style;
    cursor->wide_tail = wide_tail;
    array->count += 1;
  }
}

internal UIShell_TerminalCursorArray
uishell_terminal_fixture_cursor_array(Arena *arena, U16 cols, U16 rows)
{
  UIShell_TerminalCursorArray result = {0};
  result.v = push_array(arena, cleat_cursor, 6);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 10, 19, CLEAT_CURSOR_STYLE_BLOCK, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 20, 19, CLEAT_CURSOR_STYLE_BAR, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 34, 19, CLEAT_CURSOR_STYLE_UNDERLINE, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 48, 19, CLEAT_CURSOR_STYLE_BLOCK_HOLLOW, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 12, 21, CLEAT_CURSOR_STYLE_BLOCK, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 15, 21, CLEAT_CURSOR_STYLE_BAR, 1);
  return result;
}

internal cleat_snapshot
uishell_terminal_fixture_snapshot(Arena *arena, U16 cols, U16 rows)
{
  cols = ClampBot(1, cols);
  rows = ClampBot(1, rows);
  U64 cell_count = (U64)cols*(U64)rows;
  cleat_cell *cells = push_array(arena, cleat_cell, cell_count);
  U32 *graphemes = push_array(arena, U32, cell_count*4);
  cleat_snapshot snapshot =
  {
    .cols = cols,
    .rows = rows,
    .cells = cells,
    .cell_count = cell_count,
    .cursor =
    {
      .visible = 0,
      .style = CLEAT_CURSOR_STYLE_BLOCK,
    },
    .dirty = CLEAT_DIRTY_FULL,
  };
  uishell_terminal_fixture_clear(&snapshot, cells, graphemes);

  cleat_rgb fg = uishell_terminal_fixture_rgb(188, 209, 191);
  cleat_rgb dim = uishell_terminal_fixture_rgb(122, 143, 132);
  cleat_rgb accent = uishell_terminal_fixture_rgb(111, 178, 255);
  cleat_rgb gold = uishell_terminal_fixture_rgb(238, 198, 95);
  cleat_rgb green = uishell_terminal_fixture_rgb(108, 213, 145);
  cleat_rgb red = uishell_terminal_fixture_rgb(231, 112, 112);
  cleat_rgb bg = uishell_terminal_fixture_rgb(4, 4, 4);
  cleat_rgb alt_bg = uishell_terminal_fixture_rgb(30, 35, 42);

  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 0, 0, str8_lit("UIShell terminal glyph fixture"), accent, bg, CLEAT_CELL_FLAG_BOLD);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 1, 0, str8_lit("ASCII run: abcdefghijklmnopqrstuvwxyz 0123456789"), fg, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 2, 0, str8_lit("Styles: bold faint inverse underline strike overline"), dim, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 3, 0, str8_lit("bold"), fg, bg, CLEAT_CELL_FLAG_BOLD);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 3, 8, str8_lit("faint"), fg, bg, CLEAT_CELL_FLAG_FAINT);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 3, 16, str8_lit("inverse"), fg, alt_bg, CLEAT_CELL_FLAG_INVERSE);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 3, 28, str8_lit("underline"), fg, bg, CLEAT_CELL_FLAG_UNDERLINE);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 3, 42, str8_lit("strike"), fg, bg, CLEAT_CELL_FLAG_STRIKETHROUGH);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 3, 52, str8_lit("overline"), fg, bg, CLEAT_CELL_FLAG_OVERLINE);

  U32 box_top[] = {0x250C, 0x2500, 0x2500, 0x252C, 0x2500, 0x2500, 0x2510, ' ', 0x2554, 0x2550, 0x2550, 0x2566, 0x2550, 0x2550, 0x2557};
  U32 box_mid[] = {0x251C, 0x2500, 0x2500, 0x253C, 0x2500, 0x2500, 0x2524, ' ', 0x2560, 0x2550, 0x2550, 0x256C, 0x2550, 0x2550, 0x2563};
  U32 box_bot[] = {0x2514, 0x2500, 0x2500, 0x2534, 0x2500, 0x2500, 0x2518, ' ', 0x255A, 0x2550, 0x2550, 0x2569, 0x2550, 0x2550, 0x255D};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 5, 0, str8_lit("Box:"), gold, bg, 0);
  uishell_terminal_fixture_put_codepoints(&snapshot, cells, graphemes, 5, 6, box_top, ArrayCount(box_top), gold, bg, 0);
  uishell_terminal_fixture_put_codepoints(&snapshot, cells, graphemes, 6, 6, box_mid, ArrayCount(box_mid), gold, bg, 0);
  uishell_terminal_fixture_put_codepoints(&snapshot, cells, graphemes, 7, 6, box_bot, ArrayCount(box_bot), gold, bg, 0);

  U32 blocks[] = {0x2581, 0x2582, 0x2583, 0x2584, 0x2585, 0x2586, 0x2587, 0x2588, ' ', 0x258F, 0x258E, 0x258D, 0x258C, 0x258B, 0x258A, 0x2589, ' ', 0x2596, 0x2597, 0x2598, 0x259D, 0x259A, 0x259E};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 9, 0, str8_lit("Blocks:"), green, bg, 0);
  uishell_terminal_fixture_put_codepoints(&snapshot, cells, graphemes, 9, 8, blocks, ArrayCount(blocks), green, bg, 0);

  U32 braille[] = {0x2801, 0x2803, 0x2807, 0x280F, 0x281F, 0x283F, 0x287F, 0x28FF, ' ', 0x2849, 0x2924, 0x28A5, 0x2852};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 10, 0, str8_lit("Braille:"), accent, bg, 0);
  uishell_terminal_fixture_put_codepoints(&snapshot, cells, graphemes, 10, 9, braille, ArrayCount(braille), accent, bg, 0);

  U32 fallback[] = {0x03BB, ' ', 0x2713, ' ', 0x2605, ' ', 0x2190, 0x2191, 0x2192, 0x2193, ' ', 0xE0B0, 0xE0B1, 0xE0B2, 0xE0B3};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 12, 0, str8_lit("Fallback/PUA:"), fg, bg, 0);
  uishell_terminal_fixture_put_codepoints(&snapshot, cells, graphemes, 12, 14, fallback, ArrayCount(fallback), fg, bg, 0);

  U32 combining[] = {'e', 0x0301};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 13, 0, str8_lit("Combining:"), fg, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 13, 12, combining, ArrayCount(combining), fg, bg, 0, CLEAT_CELL_WIDTH_NARROW);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 13, 14, str8_lit("single cell e + U+0301"), dim, bg, 0);

  U32 emoji[] = {0x1F642};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 14, 0, str8_lit("Wide/emoji:"), fg, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 14, 12, emoji, ArrayCount(emoji), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(14 < rows && 13 < cols)
  {
    cells[14*(U64)cols + 13].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 14, 15, str8_lit("wide cell plus spacer tail"), dim, bg, 0);

  U32 unimplemented[] = {0x256D, 0x256E, 0x256F, 0x2570, ' ', 0x2571, 0x2572, 0x2573};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 16, 0, str8_lit("Text fallback sprites:"), red, bg, 0);
  uishell_terminal_fixture_put_codepoints(&snapshot, cells, graphemes, 16, 23, unimplemented, ArrayCount(unimplemented), red, bg, 0);

  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 18, 0, str8_lit("Cursors:"), fg, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 18, 10, str8_lit("block"), dim, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 18, 20, str8_lit("bar"), dim, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 18, 34, str8_lit("underline"), dim, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 18, 48, str8_lit("hollow"), dim, bg, 0);
  uishell_terminal_fixture_put_cp(&snapshot, cells, graphemes, 19, 10, '@', fg, bg, 0);
  uishell_terminal_fixture_put_cp(&snapshot, cells, graphemes, 19, 20, '@', fg, bg, 0);
  uishell_terminal_fixture_put_cp(&snapshot, cells, graphemes, 19, 34, '@', fg, bg, 0);
  uishell_terminal_fixture_put_cp(&snapshot, cells, graphemes, 19, 48, '@', fg, bg, 0);

  U32 wide_cursor_sample[] = {0x25A0};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 21, 0, str8_lit("Wide cursors:"), fg, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 21, 12, wide_cursor_sample, ArrayCount(wide_cursor_sample), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(21 < rows && 13 < cols)
  {
    cells[21*(U64)cols + 13].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 21, 15, wide_cursor_sample, ArrayCount(wide_cursor_sample), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(21 < rows && 16 < cols)
  {
    cells[21*(U64)cols + 16].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 21, 18, str8_lit("block head / bar tail"), dim, bg, 0);

  return snapshot;
}

internal void
uishell_terminal_glyph_renderer_draw_cell_feed_with_cursors(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalDrawParams *params, UIShell_TerminalCellFeed const *feed, UIShell_TerminalCursorArray cursors)
{
  uishell_terminal_cursor_array_from_feed(feed, &cursors);
  if(feed->cells != 0 && feed->cols != 0 && feed->rows != 0)
  {
    FNT_Tag font = renderer->font_set.primary_font;
    FNT_RasterFlags raster_flags = renderer->font_set.raster_flags;
    F32 font_size = renderer->font_set.font_size;
    F32 cell_width_px = params->cell_width_px;
    F32 cell_height_px = params->cell_height_px;
    FNT_Metrics font_metrics = fnt_metrics_from_tag_size(font, font_size);
    U64 expected_cell_count = (U64)feed->cols*(U64)feed->rows;
    U64 cell_count = Min(feed->cell_count, expected_cell_count);
    Rng2F32 canvas_rect = params->canvas_rect;
    dr_rect(canvas_rect, params->background_color, 0, 0, 0);

    for(U64 row_idx = 0; row_idx < feed->rows; row_idx += 1)
    {
      F32 row_y0 = floor_f32(canvas_rect.y0 + (F32)row_idx*cell_height_px);
      F32 row_y1 = ceil_f32(canvas_rect.y0 + (F32)(row_idx + 1)*cell_height_px);
      if(row_y0 >= canvas_rect.y1 || row_y1 <= canvas_rect.y0)
      {
        continue;
      }

      for(U64 col_start = 0; col_start < feed->cols;)
      {
        U64 cell_idx = row_idx*(U64)feed->cols + col_start;
        if(cell_idx >= cell_count)
        {
          break;
        }
        cleat_cell const *cell = &feed->cells[cell_idx];
        cleat_rgb bg = uishell_terminal_cell_bg(cell);
        if(uishell_terminal_cursor_array_cell_is_filled(feed, cell_count, cursors, row_idx, col_start))
        {
          bg = uishell_terminal_cell_fg(cell);
        }
        U64 col_opl = col_start + 1;
        for(; col_opl < feed->cols; col_opl += 1)
        {
          U64 run_cell_idx = row_idx*(U64)feed->cols + col_opl;
          if(run_cell_idx >= cell_count)
          {
            break;
          }
          cleat_cell const *run_cell = &feed->cells[run_cell_idx];
          cleat_rgb run_bg = uishell_terminal_cell_bg(run_cell);
          if(uishell_terminal_cursor_array_cell_is_filled(feed, cell_count, cursors, row_idx, col_opl))
          {
            run_bg = uishell_terminal_cell_fg(run_cell);
          }
          if(!uishell_terminal_rgb_match(run_bg, bg))
          {
            break;
          }
        }

        F32 x0 = floor_f32(canvas_rect.x0 + (F32)col_start*cell_width_px);
        F32 x1 = ceil_f32(canvas_rect.x0 + (F32)col_opl*cell_width_px);
        dr_rect(r2f32p(x0, row_y0, x1, row_y1), uishell_terminal_rgba_from_rgb(bg), 0, 0, 0);
        col_start = col_opl;
      }

      F32 text_y = floor_f32((row_y0 + row_y1)/2.f + font_metrics.ascent/2.f - font_metrics.descent/2.f);
      for(U64 col_idx = 0; col_idx < feed->cols;)
      {
        U64 cell_idx = row_idx*(U64)feed->cols + col_idx;
        if(cell_idx >= cell_count)
        {
          break;
        }
        cleat_cell const *cell = &feed->cells[cell_idx];
        U64 cell_cols = uishell_terminal_cell_display_cols(cell);
        cell_cols = Min(cell_cols, (U64)feed->cols - col_idx);
        FNT_Tag run_font = {0};
        Vec4F32 run_fg = {0};
        String8 run_string = {0};
        if(uishell_terminal_cell_text_run_info(arena, renderer, feed, cell_count, cursors, row_idx, col_idx, &run_font, &run_fg, &run_string))
        {
          U64 run_col_start = col_idx;
          U64 run_col_opl = col_idx + 1;
          String8List run_parts = {0};
          str8_list_push(arena, &run_parts, run_string);
          for(; run_col_opl < feed->cols; run_col_opl += 1)
          {
            FNT_Tag next_font = {0};
            Vec4F32 next_fg = {0};
            String8 next_string = {0};
            if(!uishell_terminal_cell_text_run_info(arena, renderer, feed, cell_count, cursors, row_idx, run_col_opl, &next_font, &next_fg, &next_string) ||
               !fnt_tag_match(next_font, run_font) ||
               !uishell_terminal_vec4_match(next_fg, run_fg))
            {
              break;
            }
            str8_list_push(arena, &run_parts, next_string);
          }
          String8 joined = str8_list_join(arena, &run_parts, 0);
          if(joined.size != 0)
          {
            Vec2F32 text_pos =
            {
              floor_f32(canvas_rect.x0 + (F32)run_col_start*cell_width_px),
              text_y,
            };
            dr_text(run_font, font_size, 0, 0, raster_flags, text_pos, run_fg, joined);
          }
          col_idx = run_col_opl;
          continue;
        }

        FNT_Tag cell_font = renderer->font_set.primary_font;
        if(cell->grapheme_count != 0 &&
           !uishell_terminal_cell_is_spacer(cell) &&
           !(cell->flags & CLEAT_CELL_FLAG_INVISIBLE))
        {
          String8 string = uishell_terminal_string_from_cell(arena, cell);
          B32 drew_sprite = 0;
          Vec4F32 fg = uishell_terminal_text_color_from_cell(cell);
          if(uishell_terminal_cursor_array_cell_is_filled(feed, cell_count, cursors, row_idx, col_idx))
          {
            fg = uishell_terminal_rgba_from_rgb(uishell_terminal_cell_bg(cell));
          }
          Rng2F32 cell_rect =
          {
            floor_f32(canvas_rect.x0 + (F32)col_idx*cell_width_px),
            row_y0,
            ceil_f32(canvas_rect.x0 + (F32)(col_idx + cell_cols)*cell_width_px),
            row_y1,
          };
          if(cell->grapheme_count == 1)
          {
            drew_sprite = uishell_terminal_draw_direct_sprite(cell->graphemes[0], cell_rect, fg);
          }
          if(!drew_sprite && !(string.size == 1 && string.str[0] == ' '))
          {
            cell_font = uishell_terminal_font_from_cell(renderer, cell);
            if(fnt_tag_match(cell_font, fnt_tag_zero()))
            {
              uishell_terminal_draw_missing_glyph(cell_rect, fg);
            }
            else
            {
              uishell_terminal_draw_text_in_cell(cell_font, font_size, raster_flags, cell_rect, fg, string);
              if(cell->flags & CLEAT_CELL_FLAG_BOLD)
              {
                Rng2F32 bold_rect = shift_2f32(cell_rect, v2f32(1.f, 0));
                uishell_terminal_draw_text_in_cell(cell_font, font_size, raster_flags, bold_rect, fg, string);
              }
            }
          }

          if(cell->flags & (CLEAT_CELL_FLAG_UNDERLINE|CLEAT_CELL_FLAG_STRIKETHROUGH|CLEAT_CELL_FLAG_OVERLINE))
          {
            Vec4F32 line_color = uishell_terminal_text_color_from_cell(cell);
            F32 x0 = floor_f32(canvas_rect.x0 + (F32)col_idx*cell_width_px);
            F32 x1 = ceil_f32(canvas_rect.x0 + (F32)(col_idx + cell_cols)*cell_width_px);
            F32 thickness = Clamp(1.f, floor_f32(cell_height_px*0.08f), 2.f);
            if(cell->flags & CLEAT_CELL_FLAG_UNDERLINE)
            {
              dr_rect(r2f32p(x0, row_y1 - thickness, x1, row_y1), line_color, 0, 0, 0);
            }
            if(cell->flags & CLEAT_CELL_FLAG_STRIKETHROUGH)
            {
              F32 y = floor_f32((row_y0 + row_y1)*0.5f);
              dr_rect(r2f32p(x0, y, x1, y + thickness), line_color, 0, 0, 0);
            }
            if(cell->flags & CLEAT_CELL_FLAG_OVERLINE)
            {
              dr_rect(r2f32p(x0, row_y0, x1, row_y0 + thickness), line_color, 0, 0, 0);
            }
          }
        }
        col_idx += 1;
      }
    }
    uishell_terminal_draw_cursor_overlay(feed, cell_count, cursors, canvas_rect, cell_width_px, cell_height_px);
  }
}

internal void
uishell_terminal_glyph_renderer_draw_cell_feed(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalDrawParams *params, UIShell_TerminalCellFeed const *feed)
{
  UIShell_TerminalCursorArray cursors = {0};
  uishell_terminal_glyph_renderer_draw_cell_feed_with_cursors(arena, renderer, params, feed, cursors);
}

internal void
uishell_terminal_glyph_renderer_draw_snapshot_with_cursors(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalDrawParams *params, cleat_snapshot const *snapshot, UIShell_TerminalCursorArray cursors)
{
  UIShell_TerminalCellFeed feed = uishell_terminal_cell_feed_from_cleat_snapshot(snapshot);
  uishell_terminal_glyph_renderer_draw_cell_feed_with_cursors(arena, renderer, params, &feed, cursors);
}

internal void
uishell_terminal_glyph_renderer_draw_snapshot(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalDrawParams *params, cleat_snapshot const *snapshot)
{
  UIShell_TerminalCellFeed feed = uishell_terminal_cell_feed_from_cleat_snapshot(snapshot);
  uishell_terminal_glyph_renderer_draw_cell_feed(arena, renderer, params, &feed);
}
