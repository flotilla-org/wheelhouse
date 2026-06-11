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

// Kitty Unicode placeholder (U+10EEEE): marks a cell whose content is a slice of
// a virtual image placement, not a glyph. The image is drawn by the placement
// path; the cell itself must render nothing (no glyph, no missing-glyph box),
// otherwise the placeholder tofu draws over the image.
#define UISHELL_TERMINAL_KITTY_PLACEHOLDER_CODEPOINT 0x10EEEE
internal B32
uishell_terminal_cell_is_kitty_placeholder(cleat_cell const *cell)
{
  B32 result = (cell->grapheme_count >= 1 &&
                cell->graphemes[0] == UISHELL_TERMINAL_KITTY_PLACEHOLDER_CODEPOINT);
  return result;
}

// Extract the text of a stream selection (sel_min..sel_max inclusive, line=row
// column=col over the feed grid): each row's selected columns joined, trailing
// whitespace trimmed (terminal rows are space-padded), rows joined with '\n'.
internal String8
uishell_terminal_selection_text_from_feed(Arena *arena, UIShell_TerminalCellFeed const *feed, TxtPt sel_min, TxtPt sel_max)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8List lines = {0};
  for(S64 r = sel_min.line; r <= sel_max.line; r += 1)
  {
    if(r < 0 || r >= (S64)feed->rows) { continue; }
    S64 start_col = (r == sel_min.line) ? sel_min.column : 0;
    S64 end_col = (r == sel_max.line) ? sel_max.column : (S64)feed->cols - 1;
    String8List cells = {0};
    for(S64 c = start_col; c <= end_col && c < (S64)feed->cols; c += 1)
    {
      cleat_cell const *cell = &feed->cells[r*(S64)feed->cols + c];
      if(uishell_terminal_cell_is_spacer(cell) || uishell_terminal_cell_is_kitty_placeholder(cell))
      {
        continue;
      }
      String8 cell_string = (cell->grapheme_count == 0) ? str8_lit(" ") : uishell_terminal_string_from_cell(scratch.arena, cell);
      str8_list_push(scratch.arena, &cells, cell_string);
    }
    String8 line = str8_list_join(scratch.arena, &cells, 0);
    while(line.size > 0 && (line.str[line.size-1] == ' ' || line.str[line.size-1] == '\t'))
    {
      line.size -= 1;
    }
    str8_list_push(arena, &lines, push_str8_copy(arena, line));
  }
  StringJoin join = {0};
  join.sep = str8_lit("\n");
  String8 result = str8_list_join(arena, &lines, &join);
  scratch_end(scratch);
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
  result = u64_hash_from_seed_str8(result, str8_struct(&font_set->color_emoji_font_count));
  for(U64 idx = 0; idx < font_set->color_emoji_font_count; idx += 1)
  {
    result = u64_hash_from_seed_str8(result, str8_struct(&font_set->color_emoji_fonts[idx]));
  }
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
uishell_terminal_font_set_push_font(FNT_Tag *fonts, U64 *count, U64 cap, FNT_Tag primary_font, FNT_Tag font)
{
  if(!fnt_tag_match(font, fnt_tag_zero()) &&
     !fnt_tag_match(font, primary_font) &&
     *count < cap)
  {
    B32 duplicate = 0;
    for(U64 idx = 0; idx < *count; idx += 1)
    {
      if(fnt_tag_match(fonts[idx], font))
      {
        duplicate = 1;
        break;
      }
    }
    if(!duplicate)
    {
      fonts[*count] = font;
      *count += 1;
    }
  }
}

internal void
uishell_terminal_font_set_push_fallback(UIShell_TerminalFontSet *font_set, FNT_Tag font)
{
  uishell_terminal_font_set_push_font(font_set->fallback_fonts, &font_set->fallback_font_count, font_set->fallback_font_cap, font_set->primary_font, font);
}

internal void
uishell_terminal_font_set_push_color_emoji(UIShell_TerminalFontSet *font_set, FNT_Tag font)
{
  uishell_terminal_font_set_push_font(font_set->color_emoji_fonts, &font_set->color_emoji_font_count, font_set->color_emoji_font_cap, font_set->primary_font, font);
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
uishell_terminal_font_set_push_color_emoji_static_data(UIShell_TerminalFontSet *font_set, String8 *data_ptr)
{
  if(data_ptr != 0 && data_ptr->size != 0)
  {
    uishell_terminal_font_set_push_color_emoji(font_set, fnt_tag_from_static_data_string(data_ptr));
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
uishell_terminal_font_set_from_fonts(Arena *scratch_arena, FNT_Tag primary_font, FNT_RasterFlags raster_flags, F32 font_size, FNT_Tag main_fallback_font, String8 fallback_setting, String8 **embedded_color_emoji_data, U64 embedded_color_emoji_count, String8 **embedded_fallback_data, U64 embedded_fallback_count)
{
  UIShell_TerminalFontSet result =
  {
    .primary_font = primary_font,
    .fallback_font_cap = 32 + embedded_fallback_count,
    .color_emoji_font_cap = 8 + embedded_color_emoji_count,
    .raster_flags = raster_flags,
    .font_size = font_size,
  };
  result.fallback_fonts = push_array(scratch_arena, FNT_Tag, result.fallback_font_cap);
  result.color_emoji_fonts = push_array(scratch_arena, FNT_Tag, result.color_emoji_font_cap);
  uishell_terminal_font_set_push_fallback_paths(scratch_arena, &result, fallback_setting);
#if OS_MAC
  uishell_terminal_font_set_push_color_emoji(&result, fnt_tag_from_path(str8_lit("/System/Library/Fonts/Apple Color Emoji.ttc")));
#endif
  for(U64 idx = 0; idx < embedded_color_emoji_count; idx += 1)
  {
    uishell_terminal_font_set_push_color_emoji_static_data(&result, embedded_color_emoji_data[idx]);
  }
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

typedef struct UIShell_TerminalCodepointRange UIShell_TerminalCodepointRange;
struct UIShell_TerminalCodepointRange
{
  U32 first;
  U32 last;
};

typedef struct UIShell_TerminalEmojiSequence UIShell_TerminalEmojiSequence;
struct UIShell_TerminalEmojiSequence
{
  U64 hash;
  U32 codepoint_off;
  U16 codepoint_count;
};

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

// Generated from Unicode UCD latest emoji-data.txt Emoji_Presentation property, fetched 2026-06-07.
// Source: https://www.unicode.org/Public/UCD/latest/ucd/emoji/emoji-data.txt
read_only global UIShell_TerminalCodepointRange uishell_terminal_emoji_presentation_ranges[] =
{
  {0x231A, 0x231B},
  {0x23E9, 0x23EC},
  {0x23F0, 0x23F0},
  {0x23F3, 0x23F3},
  {0x25FD, 0x25FE},
  {0x2614, 0x2615},
  {0x2648, 0x2653},
  {0x267F, 0x267F},
  {0x2693, 0x2693},
  {0x26A1, 0x26A1},
  {0x26AA, 0x26AB},
  {0x26BD, 0x26BE},
  {0x26C4, 0x26C5},
  {0x26CE, 0x26CE},
  {0x26D4, 0x26D4},
  {0x26EA, 0x26EA},
  {0x26F2, 0x26F3},
  {0x26F5, 0x26F5},
  {0x26FA, 0x26FA},
  {0x26FD, 0x26FD},
  {0x2705, 0x2705},
  {0x270A, 0x270B},
  {0x2728, 0x2728},
  {0x274C, 0x274C},
  {0x274E, 0x274E},
  {0x2753, 0x2755},
  {0x2757, 0x2757},
  {0x2795, 0x2797},
  {0x27B0, 0x27B0},
  {0x27BF, 0x27BF},
  {0x2B1B, 0x2B1C},
  {0x2B50, 0x2B50},
  {0x2B55, 0x2B55},
  {0x1F004, 0x1F004},
  {0x1F0CF, 0x1F0CF},
  {0x1F18E, 0x1F18E},
  {0x1F191, 0x1F19A},
  {0x1F1E6, 0x1F1FF},
  {0x1F201, 0x1F201},
  {0x1F21A, 0x1F21A},
  {0x1F22F, 0x1F22F},
  {0x1F232, 0x1F236},
  {0x1F238, 0x1F23A},
  {0x1F250, 0x1F251},
  {0x1F300, 0x1F30C},
  {0x1F30D, 0x1F30E},
  {0x1F30F, 0x1F30F},
  {0x1F310, 0x1F310},
  {0x1F311, 0x1F311},
  {0x1F312, 0x1F312},
  {0x1F313, 0x1F315},
  {0x1F316, 0x1F318},
  {0x1F319, 0x1F319},
  {0x1F31A, 0x1F31A},
  {0x1F31B, 0x1F31B},
  {0x1F31C, 0x1F31C},
  {0x1F31D, 0x1F31E},
  {0x1F31F, 0x1F320},
  {0x1F32D, 0x1F32F},
  {0x1F330, 0x1F331},
  {0x1F332, 0x1F333},
  {0x1F334, 0x1F335},
  {0x1F337, 0x1F34A},
  {0x1F34B, 0x1F34B},
  {0x1F34C, 0x1F34F},
  {0x1F350, 0x1F350},
  {0x1F351, 0x1F37B},
  {0x1F37C, 0x1F37C},
  {0x1F37E, 0x1F37F},
  {0x1F380, 0x1F393},
  {0x1F3A0, 0x1F3C4},
  {0x1F3C5, 0x1F3C5},
  {0x1F3C6, 0x1F3C6},
  {0x1F3C7, 0x1F3C7},
  {0x1F3C8, 0x1F3C8},
  {0x1F3C9, 0x1F3C9},
  {0x1F3CA, 0x1F3CA},
  {0x1F3CF, 0x1F3D3},
  {0x1F3E0, 0x1F3E3},
  {0x1F3E4, 0x1F3E4},
  {0x1F3E5, 0x1F3F0},
  {0x1F3F4, 0x1F3F4},
  {0x1F3F8, 0x1F407},
  {0x1F408, 0x1F408},
  {0x1F409, 0x1F40B},
  {0x1F40C, 0x1F40E},
  {0x1F40F, 0x1F410},
  {0x1F411, 0x1F412},
  {0x1F413, 0x1F413},
  {0x1F414, 0x1F414},
  {0x1F415, 0x1F415},
  {0x1F416, 0x1F416},
  {0x1F417, 0x1F429},
  {0x1F42A, 0x1F42A},
  {0x1F42B, 0x1F43E},
  {0x1F440, 0x1F440},
  {0x1F442, 0x1F464},
  {0x1F465, 0x1F465},
  {0x1F466, 0x1F46B},
  {0x1F46C, 0x1F46D},
  {0x1F46E, 0x1F4AC},
  {0x1F4AD, 0x1F4AD},
  {0x1F4AE, 0x1F4B5},
  {0x1F4B6, 0x1F4B7},
  {0x1F4B8, 0x1F4EB},
  {0x1F4EC, 0x1F4ED},
  {0x1F4EE, 0x1F4EE},
  {0x1F4EF, 0x1F4EF},
  {0x1F4F0, 0x1F4F4},
  {0x1F4F5, 0x1F4F5},
  {0x1F4F6, 0x1F4F7},
  {0x1F4F8, 0x1F4F8},
  {0x1F4F9, 0x1F4FC},
  {0x1F4FF, 0x1F502},
  {0x1F503, 0x1F503},
  {0x1F504, 0x1F507},
  {0x1F508, 0x1F508},
  {0x1F509, 0x1F509},
  {0x1F50A, 0x1F514},
  {0x1F515, 0x1F515},
  {0x1F516, 0x1F52B},
  {0x1F52C, 0x1F52D},
  {0x1F52E, 0x1F53D},
  {0x1F54B, 0x1F54E},
  {0x1F550, 0x1F55B},
  {0x1F55C, 0x1F567},
  {0x1F57A, 0x1F57A},
  {0x1F595, 0x1F596},
  {0x1F5A4, 0x1F5A4},
  {0x1F5FB, 0x1F5FF},
  {0x1F600, 0x1F600},
  {0x1F601, 0x1F606},
  {0x1F607, 0x1F608},
  {0x1F609, 0x1F60D},
  {0x1F60E, 0x1F60E},
  {0x1F60F, 0x1F60F},
  {0x1F610, 0x1F610},
  {0x1F611, 0x1F611},
  {0x1F612, 0x1F614},
  {0x1F615, 0x1F615},
  {0x1F616, 0x1F616},
  {0x1F617, 0x1F617},
  {0x1F618, 0x1F618},
  {0x1F619, 0x1F619},
  {0x1F61A, 0x1F61A},
  {0x1F61B, 0x1F61B},
  {0x1F61C, 0x1F61E},
  {0x1F61F, 0x1F61F},
  {0x1F620, 0x1F625},
  {0x1F626, 0x1F627},
  {0x1F628, 0x1F62B},
  {0x1F62C, 0x1F62C},
  {0x1F62D, 0x1F62D},
  {0x1F62E, 0x1F62F},
  {0x1F630, 0x1F633},
  {0x1F634, 0x1F634},
  {0x1F635, 0x1F635},
  {0x1F636, 0x1F636},
  {0x1F637, 0x1F640},
  {0x1F641, 0x1F644},
  {0x1F645, 0x1F64F},
  {0x1F680, 0x1F680},
  {0x1F681, 0x1F682},
  {0x1F683, 0x1F685},
  {0x1F686, 0x1F686},
  {0x1F687, 0x1F687},
  {0x1F688, 0x1F688},
  {0x1F689, 0x1F689},
  {0x1F68A, 0x1F68B},
  {0x1F68C, 0x1F68C},
  {0x1F68D, 0x1F68D},
  {0x1F68E, 0x1F68E},
  {0x1F68F, 0x1F68F},
  {0x1F690, 0x1F690},
  {0x1F691, 0x1F693},
  {0x1F694, 0x1F694},
  {0x1F695, 0x1F695},
  {0x1F696, 0x1F696},
  {0x1F697, 0x1F697},
  {0x1F698, 0x1F698},
  {0x1F699, 0x1F69A},
  {0x1F69B, 0x1F6A1},
  {0x1F6A2, 0x1F6A2},
  {0x1F6A3, 0x1F6A3},
  {0x1F6A4, 0x1F6A5},
  {0x1F6A6, 0x1F6A6},
  {0x1F6A7, 0x1F6AD},
  {0x1F6AE, 0x1F6B1},
  {0x1F6B2, 0x1F6B2},
  {0x1F6B3, 0x1F6B5},
  {0x1F6B6, 0x1F6B6},
  {0x1F6B7, 0x1F6B8},
  {0x1F6B9, 0x1F6BE},
  {0x1F6BF, 0x1F6BF},
  {0x1F6C0, 0x1F6C0},
  {0x1F6C1, 0x1F6C5},
  {0x1F6CC, 0x1F6CC},
  {0x1F6D0, 0x1F6D0},
  {0x1F6D1, 0x1F6D2},
  {0x1F6D5, 0x1F6D5},
  {0x1F6D6, 0x1F6D7},
  {0x1F6D8, 0x1F6D8},
  {0x1F6DC, 0x1F6DC},
  {0x1F6DD, 0x1F6DF},
  {0x1F6EB, 0x1F6EC},
  {0x1F6F4, 0x1F6F6},
  {0x1F6F7, 0x1F6F8},
  {0x1F6F9, 0x1F6F9},
  {0x1F6FA, 0x1F6FA},
  {0x1F6FB, 0x1F6FC},
  {0x1F7E0, 0x1F7EB},
  {0x1F7F0, 0x1F7F0},
  {0x1F90C, 0x1F90C},
  {0x1F90D, 0x1F90F},
  {0x1F910, 0x1F918},
  {0x1F919, 0x1F91E},
  {0x1F91F, 0x1F91F},
  {0x1F920, 0x1F927},
  {0x1F928, 0x1F92F},
  {0x1F930, 0x1F930},
  {0x1F931, 0x1F932},
  {0x1F933, 0x1F93A},
  {0x1F93C, 0x1F93E},
  {0x1F93F, 0x1F93F},
  {0x1F940, 0x1F945},
  {0x1F947, 0x1F94B},
  {0x1F94C, 0x1F94C},
  {0x1F94D, 0x1F94F},
  {0x1F950, 0x1F95E},
  {0x1F95F, 0x1F96B},
  {0x1F96C, 0x1F970},
  {0x1F971, 0x1F971},
  {0x1F972, 0x1F972},
  {0x1F973, 0x1F976},
  {0x1F977, 0x1F978},
  {0x1F979, 0x1F979},
  {0x1F97A, 0x1F97A},
  {0x1F97B, 0x1F97B},
  {0x1F97C, 0x1F97F},
  {0x1F980, 0x1F984},
  {0x1F985, 0x1F991},
  {0x1F992, 0x1F997},
  {0x1F998, 0x1F9A2},
  {0x1F9A3, 0x1F9A4},
  {0x1F9A5, 0x1F9AA},
  {0x1F9AB, 0x1F9AD},
  {0x1F9AE, 0x1F9AF},
  {0x1F9B0, 0x1F9B9},
  {0x1F9BA, 0x1F9BF},
  {0x1F9C0, 0x1F9C0},
  {0x1F9C1, 0x1F9C2},
  {0x1F9C3, 0x1F9CA},
  {0x1F9CB, 0x1F9CB},
  {0x1F9CC, 0x1F9CC},
  {0x1F9CD, 0x1F9CF},
  {0x1F9D0, 0x1F9E6},
  {0x1F9E7, 0x1F9FF},
  {0x1FA70, 0x1FA73},
  {0x1FA74, 0x1FA74},
  {0x1FA75, 0x1FA77},
  {0x1FA78, 0x1FA7A},
  {0x1FA7B, 0x1FA7C},
  {0x1FA80, 0x1FA82},
  {0x1FA83, 0x1FA86},
  {0x1FA87, 0x1FA88},
  {0x1FA89, 0x1FA89},
  {0x1FA8A, 0x1FA8A},
  {0x1FA8E, 0x1FA8E},
  {0x1FA8F, 0x1FA8F},
  {0x1FA90, 0x1FA95},
  {0x1FA96, 0x1FAA8},
  {0x1FAA9, 0x1FAAC},
  {0x1FAAD, 0x1FAAF},
  {0x1FAB0, 0x1FAB6},
  {0x1FAB7, 0x1FABA},
  {0x1FABB, 0x1FABD},
  {0x1FABE, 0x1FABE},
  {0x1FABF, 0x1FABF},
  {0x1FAC0, 0x1FAC2},
  {0x1FAC3, 0x1FAC5},
  {0x1FAC6, 0x1FAC6},
  {0x1FAC8, 0x1FAC8},
  {0x1FACD, 0x1FACD},
  {0x1FACE, 0x1FACF},
  {0x1FAD0, 0x1FAD6},
  {0x1FAD7, 0x1FAD9},
  {0x1FADA, 0x1FADB},
  {0x1FADC, 0x1FADC},
  {0x1FADF, 0x1FADF},
  {0x1FAE0, 0x1FAE7},
  {0x1FAE8, 0x1FAE8},
  {0x1FAE9, 0x1FAE9},
  {0x1FAEA, 0x1FAEA},
  {0x1FAEF, 0x1FAEF},
  {0x1FAF0, 0x1FAF6},
  {0x1FAF7, 0x1FAF8},
};

// Generated from Unicode emoji sequence data, fetched 2026-06-07.
// Sources: https://www.unicode.org/Public/emoji/latest/emoji-sequences.txt
//          https://www.unicode.org/Public/emoji/latest/emoji-zwj-sequences.txt
read_only global U32 uishell_terminal_emoji_sequence_codepoints[] =
{
  0x00A9, 0xFE0F, 0x00AE, 0xFE0F, 0x203C, 0xFE0F, 0x2049, 0xFE0F,
  0x2122, 0xFE0F, 0x2139, 0xFE0F, 0x2194, 0xFE0F, 0x2195, 0xFE0F,
  0x2196, 0xFE0F, 0x2197, 0xFE0F, 0x2198, 0xFE0F, 0x2199, 0xFE0F,
  0x21A9, 0xFE0F, 0x21AA, 0xFE0F, 0x2328, 0xFE0F, 0x23CF, 0xFE0F,
  0x23ED, 0xFE0F, 0x23EE, 0xFE0F, 0x23EF, 0xFE0F, 0x23F1, 0xFE0F,
  0x23F2, 0xFE0F, 0x23F8, 0xFE0F, 0x23F9, 0xFE0F, 0x23FA, 0xFE0F,
  0x24C2, 0xFE0F, 0x25AA, 0xFE0F, 0x25AB, 0xFE0F, 0x25B6, 0xFE0F,
  0x25C0, 0xFE0F, 0x25FB, 0xFE0F, 0x25FC, 0xFE0F, 0x2600, 0xFE0F,
  0x2601, 0xFE0F, 0x2602, 0xFE0F, 0x2603, 0xFE0F, 0x2604, 0xFE0F,
  0x260E, 0xFE0F, 0x2611, 0xFE0F, 0x2618, 0xFE0F, 0x261D, 0xFE0F,
  0x2620, 0xFE0F, 0x2622, 0xFE0F, 0x2623, 0xFE0F, 0x2626, 0xFE0F,
  0x262A, 0xFE0F, 0x262E, 0xFE0F, 0x262F, 0xFE0F, 0x2638, 0xFE0F,
  0x2639, 0xFE0F, 0x263A, 0xFE0F, 0x2640, 0xFE0F, 0x2642, 0xFE0F,
  0x265F, 0xFE0F, 0x2660, 0xFE0F, 0x2663, 0xFE0F, 0x2665, 0xFE0F,
  0x2666, 0xFE0F, 0x2668, 0xFE0F, 0x267B, 0xFE0F, 0x267E, 0xFE0F,
  0x2692, 0xFE0F, 0x2694, 0xFE0F, 0x2695, 0xFE0F, 0x2696, 0xFE0F,
  0x2697, 0xFE0F, 0x2699, 0xFE0F, 0x269B, 0xFE0F, 0x269C, 0xFE0F,
  0x26A0, 0xFE0F, 0x26A7, 0xFE0F, 0x26B0, 0xFE0F, 0x26B1, 0xFE0F,
  0x26C8, 0xFE0F, 0x26CF, 0xFE0F, 0x26D1, 0xFE0F, 0x26D3, 0xFE0F,
  0x26E9, 0xFE0F, 0x26F0, 0xFE0F, 0x26F1, 0xFE0F, 0x26F4, 0xFE0F,
  0x26F7, 0xFE0F, 0x26F8, 0xFE0F, 0x26F9, 0xFE0F, 0x2702, 0xFE0F,
  0x2708, 0xFE0F, 0x2709, 0xFE0F, 0x270C, 0xFE0F, 0x270D, 0xFE0F,
  0x270F, 0xFE0F, 0x2712, 0xFE0F, 0x2714, 0xFE0F, 0x2716, 0xFE0F,
  0x271D, 0xFE0F, 0x2721, 0xFE0F, 0x2733, 0xFE0F, 0x2734, 0xFE0F,
  0x2744, 0xFE0F, 0x2747, 0xFE0F, 0x2763, 0xFE0F, 0x2764, 0xFE0F,
  0x27A1, 0xFE0F, 0x2934, 0xFE0F, 0x2935, 0xFE0F, 0x2B05, 0xFE0F,
  0x2B06, 0xFE0F, 0x2B07, 0xFE0F, 0x261D, 0x1F3FB, 0x261D, 0x1F3FC,
  0x261D, 0x1F3FD, 0x261D, 0x1F3FE, 0x261D, 0x1F3FF, 0x26F9, 0x1F3FB,
  0x26F9, 0x1F3FC, 0x26F9, 0x1F3FD, 0x26F9, 0x1F3FE, 0x26F9, 0x1F3FF,
  0x270A, 0x1F3FB, 0x270A, 0x1F3FC, 0x270A, 0x1F3FD, 0x270A, 0x1F3FE,
  0x270A, 0x1F3FF, 0x270B, 0x1F3FB, 0x270B, 0x1F3FC, 0x270B, 0x1F3FD,
  0x270B, 0x1F3FE, 0x270B, 0x1F3FF, 0x270C, 0x1F3FB, 0x270C, 0x1F3FC,
  0x270C, 0x1F3FD, 0x270C, 0x1F3FE, 0x270C, 0x1F3FF, 0x270D, 0x1F3FB,
  0x270D, 0x1F3FC, 0x270D, 0x1F3FD, 0x270D, 0x1F3FE, 0x270D, 0x1F3FF,
  0x3030, 0xFE0F, 0x303D, 0xFE0F, 0x3297, 0xFE0F, 0x3299, 0xFE0F,
  0x1F170, 0xFE0F, 0x1F171, 0xFE0F, 0x1F17E, 0xFE0F, 0x1F17F, 0xFE0F,
  0x1F202, 0xFE0F, 0x1F237, 0xFE0F, 0x1F321, 0xFE0F, 0x1F324, 0xFE0F,
  0x1F325, 0xFE0F, 0x1F326, 0xFE0F, 0x1F327, 0xFE0F, 0x1F328, 0xFE0F,
  0x1F329, 0xFE0F, 0x1F32A, 0xFE0F, 0x1F32B, 0xFE0F, 0x1F32C, 0xFE0F,
  0x1F336, 0xFE0F, 0x1F37D, 0xFE0F, 0x1F396, 0xFE0F, 0x1F397, 0xFE0F,
  0x1F399, 0xFE0F, 0x1F39A, 0xFE0F, 0x1F39B, 0xFE0F, 0x1F39E, 0xFE0F,
  0x1F39F, 0xFE0F, 0x1F3CB, 0xFE0F, 0x1F3CC, 0xFE0F, 0x1F3CD, 0xFE0F,
  0x1F3CE, 0xFE0F, 0x1F3D4, 0xFE0F, 0x1F3D5, 0xFE0F, 0x1F3D6, 0xFE0F,
  0x1F3D7, 0xFE0F, 0x1F3D8, 0xFE0F, 0x1F3D9, 0xFE0F, 0x1F3DA, 0xFE0F,
  0x1F3DB, 0xFE0F, 0x1F3DC, 0xFE0F, 0x1F3DD, 0xFE0F, 0x1F3DE, 0xFE0F,
  0x1F3DF, 0xFE0F, 0x1F3F3, 0xFE0F, 0x1F3F5, 0xFE0F, 0x1F3F7, 0xFE0F,
  0x1F43F, 0xFE0F, 0x1F441, 0xFE0F, 0x1F4FD, 0xFE0F, 0x1F549, 0xFE0F,
  0x1F54A, 0xFE0F, 0x1F56F, 0xFE0F, 0x1F570, 0xFE0F, 0x1F573, 0xFE0F,
  0x1F574, 0xFE0F, 0x1F575, 0xFE0F, 0x1F576, 0xFE0F, 0x1F577, 0xFE0F,
  0x1F578, 0xFE0F, 0x1F579, 0xFE0F, 0x1F587, 0xFE0F, 0x1F58A, 0xFE0F,
  0x1F58B, 0xFE0F, 0x1F58C, 0xFE0F, 0x1F58D, 0xFE0F, 0x1F590, 0xFE0F,
  0x1F5A5, 0xFE0F, 0x1F5A8, 0xFE0F, 0x1F5B1, 0xFE0F, 0x1F5B2, 0xFE0F,
  0x1F5BC, 0xFE0F, 0x1F5C2, 0xFE0F, 0x1F5C3, 0xFE0F, 0x1F5C4, 0xFE0F,
  0x1F5D1, 0xFE0F, 0x1F5D2, 0xFE0F, 0x1F5D3, 0xFE0F, 0x1F5DC, 0xFE0F,
  0x1F5DD, 0xFE0F, 0x1F5DE, 0xFE0F, 0x1F5E1, 0xFE0F, 0x1F5E3, 0xFE0F,
  0x1F5E8, 0xFE0F, 0x1F5EF, 0xFE0F, 0x1F5F3, 0xFE0F, 0x1F5FA, 0xFE0F,
  0x1F6CB, 0xFE0F, 0x1F6CD, 0xFE0F, 0x1F6CE, 0xFE0F, 0x1F6CF, 0xFE0F,
  0x1F6E0, 0xFE0F, 0x1F6E1, 0xFE0F, 0x1F6E2, 0xFE0F, 0x1F6E3, 0xFE0F,
  0x1F6E4, 0xFE0F, 0x1F6E5, 0xFE0F, 0x1F6E9, 0xFE0F, 0x1F6F0, 0xFE0F,
  0x1F6F3, 0xFE0F, 0x1F1E6, 0x1F1E8, 0x1F1E6, 0x1F1E9, 0x1F1E6, 0x1F1EA,
  0x1F1E6, 0x1F1EB, 0x1F1E6, 0x1F1EC, 0x1F1E6, 0x1F1EE, 0x1F1E6, 0x1F1F1,
  0x1F1E6, 0x1F1F2, 0x1F1E6, 0x1F1F4, 0x1F1E6, 0x1F1F6, 0x1F1E6, 0x1F1F7,
  0x1F1E6, 0x1F1F8, 0x1F1E6, 0x1F1F9, 0x1F1E6, 0x1F1FA, 0x1F1E6, 0x1F1FC,
  0x1F1E6, 0x1F1FD, 0x1F1E6, 0x1F1FF, 0x1F1E7, 0x1F1E6, 0x1F1E7, 0x1F1E7,
  0x1F1E7, 0x1F1E9, 0x1F1E7, 0x1F1EA, 0x1F1E7, 0x1F1EB, 0x1F1E7, 0x1F1EC,
  0x1F1E7, 0x1F1ED, 0x1F1E7, 0x1F1EE, 0x1F1E7, 0x1F1EF, 0x1F1E7, 0x1F1F1,
  0x1F1E7, 0x1F1F2, 0x1F1E7, 0x1F1F3, 0x1F1E7, 0x1F1F4, 0x1F1E7, 0x1F1F6,
  0x1F1E7, 0x1F1F7, 0x1F1E7, 0x1F1F8, 0x1F1E7, 0x1F1F9, 0x1F1E7, 0x1F1FB,
  0x1F1E7, 0x1F1FC, 0x1F1E7, 0x1F1FE, 0x1F1E7, 0x1F1FF, 0x1F1E8, 0x1F1E6,
  0x1F1E8, 0x1F1E8, 0x1F1E8, 0x1F1E9, 0x1F1E8, 0x1F1EB, 0x1F1E8, 0x1F1EC,
  0x1F1E8, 0x1F1ED, 0x1F1E8, 0x1F1EE, 0x1F1E8, 0x1F1F0, 0x1F1E8, 0x1F1F1,
  0x1F1E8, 0x1F1F2, 0x1F1E8, 0x1F1F3, 0x1F1E8, 0x1F1F4, 0x1F1E8, 0x1F1F5,
  0x1F1E8, 0x1F1F6, 0x1F1E8, 0x1F1F7, 0x1F1E8, 0x1F1FA, 0x1F1E8, 0x1F1FB,
  0x1F1E8, 0x1F1FC, 0x1F1E8, 0x1F1FD, 0x1F1E8, 0x1F1FE, 0x1F1E8, 0x1F1FF,
  0x1F1E9, 0x1F1EA, 0x1F1E9, 0x1F1EC, 0x1F1E9, 0x1F1EF, 0x1F1E9, 0x1F1F0,
  0x1F1E9, 0x1F1F2, 0x1F1E9, 0x1F1F4, 0x1F1E9, 0x1F1FF, 0x1F1EA, 0x1F1E6,
  0x1F1EA, 0x1F1E8, 0x1F1EA, 0x1F1EA, 0x1F1EA, 0x1F1EC, 0x1F1EA, 0x1F1ED,
  0x1F1EA, 0x1F1F7, 0x1F1EA, 0x1F1F8, 0x1F1EA, 0x1F1F9, 0x1F1EA, 0x1F1FA,
  0x1F1EB, 0x1F1EE, 0x1F1EB, 0x1F1EF, 0x1F1EB, 0x1F1F0, 0x1F1EB, 0x1F1F2,
  0x1F1EB, 0x1F1F4, 0x1F1EB, 0x1F1F7, 0x1F1EC, 0x1F1E6, 0x1F1EC, 0x1F1E7,
  0x1F1EC, 0x1F1E9, 0x1F1EC, 0x1F1EA, 0x1F1EC, 0x1F1EB, 0x1F1EC, 0x1F1EC,
  0x1F1EC, 0x1F1ED, 0x1F1EC, 0x1F1EE, 0x1F1EC, 0x1F1F1, 0x1F1EC, 0x1F1F2,
  0x1F1EC, 0x1F1F3, 0x1F1EC, 0x1F1F5, 0x1F1EC, 0x1F1F6, 0x1F1EC, 0x1F1F7,
  0x1F1EC, 0x1F1F8, 0x1F1EC, 0x1F1F9, 0x1F1EC, 0x1F1FA, 0x1F1EC, 0x1F1FC,
  0x1F1EC, 0x1F1FE, 0x1F1ED, 0x1F1F0, 0x1F1ED, 0x1F1F2, 0x1F1ED, 0x1F1F3,
  0x1F1ED, 0x1F1F7, 0x1F1ED, 0x1F1F9, 0x1F1ED, 0x1F1FA, 0x1F1EE, 0x1F1E8,
  0x1F1EE, 0x1F1E9, 0x1F1EE, 0x1F1EA, 0x1F1EE, 0x1F1F1, 0x1F1EE, 0x1F1F2,
  0x1F1EE, 0x1F1F3, 0x1F1EE, 0x1F1F4, 0x1F1EE, 0x1F1F6, 0x1F1EE, 0x1F1F7,
  0x1F1EE, 0x1F1F8, 0x1F1EE, 0x1F1F9, 0x1F1EF, 0x1F1EA, 0x1F1EF, 0x1F1F2,
  0x1F1EF, 0x1F1F4, 0x1F1EF, 0x1F1F5, 0x1F1F0, 0x1F1EA, 0x1F1F0, 0x1F1EC,
  0x1F1F0, 0x1F1ED, 0x1F1F0, 0x1F1EE, 0x1F1F0, 0x1F1F2, 0x1F1F0, 0x1F1F3,
  0x1F1F0, 0x1F1F5, 0x1F1F0, 0x1F1F7, 0x1F1F0, 0x1F1FC, 0x1F1F0, 0x1F1FE,
  0x1F1F0, 0x1F1FF, 0x1F1F1, 0x1F1E6, 0x1F1F1, 0x1F1E7, 0x1F1F1, 0x1F1E8,
  0x1F1F1, 0x1F1EE, 0x1F1F1, 0x1F1F0, 0x1F1F1, 0x1F1F7, 0x1F1F1, 0x1F1F8,
  0x1F1F1, 0x1F1F9, 0x1F1F1, 0x1F1FA, 0x1F1F1, 0x1F1FB, 0x1F1F1, 0x1F1FE,
  0x1F1F2, 0x1F1E6, 0x1F1F2, 0x1F1E8, 0x1F1F2, 0x1F1E9, 0x1F1F2, 0x1F1EA,
  0x1F1F2, 0x1F1EB, 0x1F1F2, 0x1F1EC, 0x1F1F2, 0x1F1ED, 0x1F1F2, 0x1F1F0,
  0x1F1F2, 0x1F1F1, 0x1F1F2, 0x1F1F2, 0x1F1F2, 0x1F1F3, 0x1F1F2, 0x1F1F4,
  0x1F1F2, 0x1F1F5, 0x1F1F2, 0x1F1F6, 0x1F1F2, 0x1F1F7, 0x1F1F2, 0x1F1F8,
  0x1F1F2, 0x1F1F9, 0x1F1F2, 0x1F1FA, 0x1F1F2, 0x1F1FB, 0x1F1F2, 0x1F1FC,
  0x1F1F2, 0x1F1FD, 0x1F1F2, 0x1F1FE, 0x1F1F2, 0x1F1FF, 0x1F1F3, 0x1F1E6,
  0x1F1F3, 0x1F1E8, 0x1F1F3, 0x1F1EA, 0x1F1F3, 0x1F1EB, 0x1F1F3, 0x1F1EC,
  0x1F1F3, 0x1F1EE, 0x1F1F3, 0x1F1F1, 0x1F1F3, 0x1F1F4, 0x1F1F3, 0x1F1F5,
  0x1F1F3, 0x1F1F7, 0x1F1F3, 0x1F1FA, 0x1F1F3, 0x1F1FF, 0x1F1F4, 0x1F1F2,
  0x1F1F5, 0x1F1E6, 0x1F1F5, 0x1F1EA, 0x1F1F5, 0x1F1EB, 0x1F1F5, 0x1F1EC,
  0x1F1F5, 0x1F1ED, 0x1F1F5, 0x1F1F0, 0x1F1F5, 0x1F1F1, 0x1F1F5, 0x1F1F2,
  0x1F1F5, 0x1F1F3, 0x1F1F5, 0x1F1F7, 0x1F1F5, 0x1F1F8, 0x1F1F5, 0x1F1F9,
  0x1F1F5, 0x1F1FC, 0x1F1F5, 0x1F1FE, 0x1F1F6, 0x1F1E6, 0x1F1F7, 0x1F1EA,
  0x1F1F7, 0x1F1F4, 0x1F1F7, 0x1F1F8, 0x1F1F7, 0x1F1FA, 0x1F1F7, 0x1F1FC,
  0x1F1F8, 0x1F1E6, 0x1F1F8, 0x1F1E7, 0x1F1F8, 0x1F1E8, 0x1F1F8, 0x1F1E9,
  0x1F1F8, 0x1F1EA, 0x1F1F8, 0x1F1EC, 0x1F1F8, 0x1F1ED, 0x1F1F8, 0x1F1EE,
  0x1F1F8, 0x1F1EF, 0x1F1F8, 0x1F1F0, 0x1F1F8, 0x1F1F1, 0x1F1F8, 0x1F1F2,
  0x1F1F8, 0x1F1F3, 0x1F1F8, 0x1F1F4, 0x1F1F8, 0x1F1F7, 0x1F1F8, 0x1F1F8,
  0x1F1F8, 0x1F1F9, 0x1F1F8, 0x1F1FB, 0x1F1F8, 0x1F1FD, 0x1F1F8, 0x1F1FE,
  0x1F1F8, 0x1F1FF, 0x1F1F9, 0x1F1E6, 0x1F1F9, 0x1F1E8, 0x1F1F9, 0x1F1E9,
  0x1F1F9, 0x1F1EB, 0x1F1F9, 0x1F1EC, 0x1F1F9, 0x1F1ED, 0x1F1F9, 0x1F1EF,
  0x1F1F9, 0x1F1F0, 0x1F1F9, 0x1F1F1, 0x1F1F9, 0x1F1F2, 0x1F1F9, 0x1F1F3,
  0x1F1F9, 0x1F1F4, 0x1F1F9, 0x1F1F7, 0x1F1F9, 0x1F1F9, 0x1F1F9, 0x1F1FB,
  0x1F1F9, 0x1F1FC, 0x1F1F9, 0x1F1FF, 0x1F1FA, 0x1F1E6, 0x1F1FA, 0x1F1EC,
  0x1F1FA, 0x1F1F2, 0x1F1FA, 0x1F1F3, 0x1F1FA, 0x1F1F8, 0x1F1FA, 0x1F1FE,
  0x1F1FA, 0x1F1FF, 0x1F1FB, 0x1F1E6, 0x1F1FB, 0x1F1E8, 0x1F1FB, 0x1F1EA,
  0x1F1FB, 0x1F1EC, 0x1F1FB, 0x1F1EE, 0x1F1FB, 0x1F1F3, 0x1F1FB, 0x1F1FA,
  0x1F1FC, 0x1F1EB, 0x1F1FC, 0x1F1F8, 0x1F1FD, 0x1F1F0, 0x1F1FE, 0x1F1EA,
  0x1F1FE, 0x1F1F9, 0x1F1FF, 0x1F1E6, 0x1F1FF, 0x1F1F2, 0x1F1FF, 0x1F1FC,
  0x1F385, 0x1F3FB, 0x1F385, 0x1F3FC, 0x1F385, 0x1F3FD, 0x1F385, 0x1F3FE,
  0x1F385, 0x1F3FF, 0x1F3C2, 0x1F3FB, 0x1F3C2, 0x1F3FC, 0x1F3C2, 0x1F3FD,
  0x1F3C2, 0x1F3FE, 0x1F3C2, 0x1F3FF, 0x1F3C3, 0x1F3FB, 0x1F3C3, 0x1F3FC,
  0x1F3C3, 0x1F3FD, 0x1F3C3, 0x1F3FE, 0x1F3C3, 0x1F3FF, 0x1F3C4, 0x1F3FB,
  0x1F3C4, 0x1F3FC, 0x1F3C4, 0x1F3FD, 0x1F3C4, 0x1F3FE, 0x1F3C4, 0x1F3FF,
  0x1F3C7, 0x1F3FB, 0x1F3C7, 0x1F3FC, 0x1F3C7, 0x1F3FD, 0x1F3C7, 0x1F3FE,
  0x1F3C7, 0x1F3FF, 0x1F3CA, 0x1F3FB, 0x1F3CA, 0x1F3FC, 0x1F3CA, 0x1F3FD,
  0x1F3CA, 0x1F3FE, 0x1F3CA, 0x1F3FF, 0x1F3CB, 0x1F3FB, 0x1F3CB, 0x1F3FC,
  0x1F3CB, 0x1F3FD, 0x1F3CB, 0x1F3FE, 0x1F3CB, 0x1F3FF, 0x1F3CC, 0x1F3FB,
  0x1F3CC, 0x1F3FC, 0x1F3CC, 0x1F3FD, 0x1F3CC, 0x1F3FE, 0x1F3CC, 0x1F3FF,
  0x1F442, 0x1F3FB, 0x1F442, 0x1F3FC, 0x1F442, 0x1F3FD, 0x1F442, 0x1F3FE,
  0x1F442, 0x1F3FF, 0x1F443, 0x1F3FB, 0x1F443, 0x1F3FC, 0x1F443, 0x1F3FD,
  0x1F443, 0x1F3FE, 0x1F443, 0x1F3FF, 0x1F446, 0x1F3FB, 0x1F446, 0x1F3FC,
  0x1F446, 0x1F3FD, 0x1F446, 0x1F3FE, 0x1F446, 0x1F3FF, 0x1F447, 0x1F3FB,
  0x1F447, 0x1F3FC, 0x1F447, 0x1F3FD, 0x1F447, 0x1F3FE, 0x1F447, 0x1F3FF,
  0x1F448, 0x1F3FB, 0x1F448, 0x1F3FC, 0x1F448, 0x1F3FD, 0x1F448, 0x1F3FE,
  0x1F448, 0x1F3FF, 0x1F449, 0x1F3FB, 0x1F449, 0x1F3FC, 0x1F449, 0x1F3FD,
  0x1F449, 0x1F3FE, 0x1F449, 0x1F3FF, 0x1F44A, 0x1F3FB, 0x1F44A, 0x1F3FC,
  0x1F44A, 0x1F3FD, 0x1F44A, 0x1F3FE, 0x1F44A, 0x1F3FF, 0x1F44B, 0x1F3FB,
  0x1F44B, 0x1F3FC, 0x1F44B, 0x1F3FD, 0x1F44B, 0x1F3FE, 0x1F44B, 0x1F3FF,
  0x1F44C, 0x1F3FB, 0x1F44C, 0x1F3FC, 0x1F44C, 0x1F3FD, 0x1F44C, 0x1F3FE,
  0x1F44C, 0x1F3FF, 0x1F44D, 0x1F3FB, 0x1F44D, 0x1F3FC, 0x1F44D, 0x1F3FD,
  0x1F44D, 0x1F3FE, 0x1F44D, 0x1F3FF, 0x1F44E, 0x1F3FB, 0x1F44E, 0x1F3FC,
  0x1F44E, 0x1F3FD, 0x1F44E, 0x1F3FE, 0x1F44E, 0x1F3FF, 0x1F44F, 0x1F3FB,
  0x1F44F, 0x1F3FC, 0x1F44F, 0x1F3FD, 0x1F44F, 0x1F3FE, 0x1F44F, 0x1F3FF,
  0x1F450, 0x1F3FB, 0x1F450, 0x1F3FC, 0x1F450, 0x1F3FD, 0x1F450, 0x1F3FE,
  0x1F450, 0x1F3FF, 0x1F466, 0x1F3FB, 0x1F466, 0x1F3FC, 0x1F466, 0x1F3FD,
  0x1F466, 0x1F3FE, 0x1F466, 0x1F3FF, 0x1F467, 0x1F3FB, 0x1F467, 0x1F3FC,
  0x1F467, 0x1F3FD, 0x1F467, 0x1F3FE, 0x1F467, 0x1F3FF, 0x1F468, 0x1F3FB,
  0x1F468, 0x1F3FC, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FF,
  0x1F469, 0x1F3FB, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FE,
  0x1F469, 0x1F3FF, 0x1F46B, 0x1F3FB, 0x1F46B, 0x1F3FC, 0x1F46B, 0x1F3FD,
  0x1F46B, 0x1F3FE, 0x1F46B, 0x1F3FF, 0x1F46C, 0x1F3FB, 0x1F46C, 0x1F3FC,
  0x1F46C, 0x1F3FD, 0x1F46C, 0x1F3FE, 0x1F46C, 0x1F3FF, 0x1F46D, 0x1F3FB,
  0x1F46D, 0x1F3FC, 0x1F46D, 0x1F3FD, 0x1F46D, 0x1F3FE, 0x1F46D, 0x1F3FF,
  0x1F46E, 0x1F3FB, 0x1F46E, 0x1F3FC, 0x1F46E, 0x1F3FD, 0x1F46E, 0x1F3FE,
  0x1F46E, 0x1F3FF, 0x1F46F, 0x1F3FB, 0x1F46F, 0x1F3FC, 0x1F46F, 0x1F3FD,
  0x1F46F, 0x1F3FE, 0x1F46F, 0x1F3FF, 0x1F470, 0x1F3FB, 0x1F470, 0x1F3FC,
  0x1F470, 0x1F3FD, 0x1F470, 0x1F3FE, 0x1F470, 0x1F3FF, 0x1F471, 0x1F3FB,
  0x1F471, 0x1F3FC, 0x1F471, 0x1F3FD, 0x1F471, 0x1F3FE, 0x1F471, 0x1F3FF,
  0x1F472, 0x1F3FB, 0x1F472, 0x1F3FC, 0x1F472, 0x1F3FD, 0x1F472, 0x1F3FE,
  0x1F472, 0x1F3FF, 0x1F473, 0x1F3FB, 0x1F473, 0x1F3FC, 0x1F473, 0x1F3FD,
  0x1F473, 0x1F3FE, 0x1F473, 0x1F3FF, 0x1F474, 0x1F3FB, 0x1F474, 0x1F3FC,
  0x1F474, 0x1F3FD, 0x1F474, 0x1F3FE, 0x1F474, 0x1F3FF, 0x1F475, 0x1F3FB,
  0x1F475, 0x1F3FC, 0x1F475, 0x1F3FD, 0x1F475, 0x1F3FE, 0x1F475, 0x1F3FF,
  0x1F476, 0x1F3FB, 0x1F476, 0x1F3FC, 0x1F476, 0x1F3FD, 0x1F476, 0x1F3FE,
  0x1F476, 0x1F3FF, 0x1F477, 0x1F3FB, 0x1F477, 0x1F3FC, 0x1F477, 0x1F3FD,
  0x1F477, 0x1F3FE, 0x1F477, 0x1F3FF, 0x1F478, 0x1F3FB, 0x1F478, 0x1F3FC,
  0x1F478, 0x1F3FD, 0x1F478, 0x1F3FE, 0x1F478, 0x1F3FF, 0x1F47C, 0x1F3FB,
  0x1F47C, 0x1F3FC, 0x1F47C, 0x1F3FD, 0x1F47C, 0x1F3FE, 0x1F47C, 0x1F3FF,
  0x1F481, 0x1F3FB, 0x1F481, 0x1F3FC, 0x1F481, 0x1F3FD, 0x1F481, 0x1F3FE,
  0x1F481, 0x1F3FF, 0x1F482, 0x1F3FB, 0x1F482, 0x1F3FC, 0x1F482, 0x1F3FD,
  0x1F482, 0x1F3FE, 0x1F482, 0x1F3FF, 0x1F483, 0x1F3FB, 0x1F483, 0x1F3FC,
  0x1F483, 0x1F3FD, 0x1F483, 0x1F3FE, 0x1F483, 0x1F3FF, 0x1F485, 0x1F3FB,
  0x1F485, 0x1F3FC, 0x1F485, 0x1F3FD, 0x1F485, 0x1F3FE, 0x1F485, 0x1F3FF,
  0x1F486, 0x1F3FB, 0x1F486, 0x1F3FC, 0x1F486, 0x1F3FD, 0x1F486, 0x1F3FE,
  0x1F486, 0x1F3FF, 0x1F487, 0x1F3FB, 0x1F487, 0x1F3FC, 0x1F487, 0x1F3FD,
  0x1F487, 0x1F3FE, 0x1F487, 0x1F3FF, 0x1F48F, 0x1F3FB, 0x1F48F, 0x1F3FC,
  0x1F48F, 0x1F3FD, 0x1F48F, 0x1F3FE, 0x1F48F, 0x1F3FF, 0x1F491, 0x1F3FB,
  0x1F491, 0x1F3FC, 0x1F491, 0x1F3FD, 0x1F491, 0x1F3FE, 0x1F491, 0x1F3FF,
  0x1F4AA, 0x1F3FB, 0x1F4AA, 0x1F3FC, 0x1F4AA, 0x1F3FD, 0x1F4AA, 0x1F3FE,
  0x1F4AA, 0x1F3FF, 0x1F574, 0x1F3FB, 0x1F574, 0x1F3FC, 0x1F574, 0x1F3FD,
  0x1F574, 0x1F3FE, 0x1F574, 0x1F3FF, 0x1F575, 0x1F3FB, 0x1F575, 0x1F3FC,
  0x1F575, 0x1F3FD, 0x1F575, 0x1F3FE, 0x1F575, 0x1F3FF, 0x1F57A, 0x1F3FB,
  0x1F57A, 0x1F3FC, 0x1F57A, 0x1F3FD, 0x1F57A, 0x1F3FE, 0x1F57A, 0x1F3FF,
  0x1F590, 0x1F3FB, 0x1F590, 0x1F3FC, 0x1F590, 0x1F3FD, 0x1F590, 0x1F3FE,
  0x1F590, 0x1F3FF, 0x1F595, 0x1F3FB, 0x1F595, 0x1F3FC, 0x1F595, 0x1F3FD,
  0x1F595, 0x1F3FE, 0x1F595, 0x1F3FF, 0x1F596, 0x1F3FB, 0x1F596, 0x1F3FC,
  0x1F596, 0x1F3FD, 0x1F596, 0x1F3FE, 0x1F596, 0x1F3FF, 0x1F645, 0x1F3FB,
  0x1F645, 0x1F3FC, 0x1F645, 0x1F3FD, 0x1F645, 0x1F3FE, 0x1F645, 0x1F3FF,
  0x1F646, 0x1F3FB, 0x1F646, 0x1F3FC, 0x1F646, 0x1F3FD, 0x1F646, 0x1F3FE,
  0x1F646, 0x1F3FF, 0x1F647, 0x1F3FB, 0x1F647, 0x1F3FC, 0x1F647, 0x1F3FD,
  0x1F647, 0x1F3FE, 0x1F647, 0x1F3FF, 0x1F64B, 0x1F3FB, 0x1F64B, 0x1F3FC,
  0x1F64B, 0x1F3FD, 0x1F64B, 0x1F3FE, 0x1F64B, 0x1F3FF, 0x1F64C, 0x1F3FB,
  0x1F64C, 0x1F3FC, 0x1F64C, 0x1F3FD, 0x1F64C, 0x1F3FE, 0x1F64C, 0x1F3FF,
  0x1F64D, 0x1F3FB, 0x1F64D, 0x1F3FC, 0x1F64D, 0x1F3FD, 0x1F64D, 0x1F3FE,
  0x1F64D, 0x1F3FF, 0x1F64E, 0x1F3FB, 0x1F64E, 0x1F3FC, 0x1F64E, 0x1F3FD,
  0x1F64E, 0x1F3FE, 0x1F64E, 0x1F3FF, 0x1F64F, 0x1F3FB, 0x1F64F, 0x1F3FC,
  0x1F64F, 0x1F3FD, 0x1F64F, 0x1F3FE, 0x1F64F, 0x1F3FF, 0x1F6A3, 0x1F3FB,
  0x1F6A3, 0x1F3FC, 0x1F6A3, 0x1F3FD, 0x1F6A3, 0x1F3FE, 0x1F6A3, 0x1F3FF,
  0x1F6B4, 0x1F3FB, 0x1F6B4, 0x1F3FC, 0x1F6B4, 0x1F3FD, 0x1F6B4, 0x1F3FE,
  0x1F6B4, 0x1F3FF, 0x1F6B5, 0x1F3FB, 0x1F6B5, 0x1F3FC, 0x1F6B5, 0x1F3FD,
  0x1F6B5, 0x1F3FE, 0x1F6B5, 0x1F3FF, 0x1F6B6, 0x1F3FB, 0x1F6B6, 0x1F3FC,
  0x1F6B6, 0x1F3FD, 0x1F6B6, 0x1F3FE, 0x1F6B6, 0x1F3FF, 0x1F6C0, 0x1F3FB,
  0x1F6C0, 0x1F3FC, 0x1F6C0, 0x1F3FD, 0x1F6C0, 0x1F3FE, 0x1F6C0, 0x1F3FF,
  0x1F6CC, 0x1F3FB, 0x1F6CC, 0x1F3FC, 0x1F6CC, 0x1F3FD, 0x1F6CC, 0x1F3FE,
  0x1F6CC, 0x1F3FF, 0x1F90C, 0x1F3FB, 0x1F90C, 0x1F3FC, 0x1F90C, 0x1F3FD,
  0x1F90C, 0x1F3FE, 0x1F90C, 0x1F3FF, 0x1F90F, 0x1F3FB, 0x1F90F, 0x1F3FC,
  0x1F90F, 0x1F3FD, 0x1F90F, 0x1F3FE, 0x1F90F, 0x1F3FF, 0x1F918, 0x1F3FB,
  0x1F918, 0x1F3FC, 0x1F918, 0x1F3FD, 0x1F918, 0x1F3FE, 0x1F918, 0x1F3FF,
  0x1F919, 0x1F3FB, 0x1F919, 0x1F3FC, 0x1F919, 0x1F3FD, 0x1F919, 0x1F3FE,
  0x1F919, 0x1F3FF, 0x1F91A, 0x1F3FB, 0x1F91A, 0x1F3FC, 0x1F91A, 0x1F3FD,
  0x1F91A, 0x1F3FE, 0x1F91A, 0x1F3FF, 0x1F91B, 0x1F3FB, 0x1F91B, 0x1F3FC,
  0x1F91B, 0x1F3FD, 0x1F91B, 0x1F3FE, 0x1F91B, 0x1F3FF, 0x1F91C, 0x1F3FB,
  0x1F91C, 0x1F3FC, 0x1F91C, 0x1F3FD, 0x1F91C, 0x1F3FE, 0x1F91C, 0x1F3FF,
  0x1F91D, 0x1F3FB, 0x1F91D, 0x1F3FC, 0x1F91D, 0x1F3FD, 0x1F91D, 0x1F3FE,
  0x1F91D, 0x1F3FF, 0x1F91E, 0x1F3FB, 0x1F91E, 0x1F3FC, 0x1F91E, 0x1F3FD,
  0x1F91E, 0x1F3FE, 0x1F91E, 0x1F3FF, 0x1F91F, 0x1F3FB, 0x1F91F, 0x1F3FC,
  0x1F91F, 0x1F3FD, 0x1F91F, 0x1F3FE, 0x1F91F, 0x1F3FF, 0x1F926, 0x1F3FB,
  0x1F926, 0x1F3FC, 0x1F926, 0x1F3FD, 0x1F926, 0x1F3FE, 0x1F926, 0x1F3FF,
  0x1F930, 0x1F3FB, 0x1F930, 0x1F3FC, 0x1F930, 0x1F3FD, 0x1F930, 0x1F3FE,
  0x1F930, 0x1F3FF, 0x1F931, 0x1F3FB, 0x1F931, 0x1F3FC, 0x1F931, 0x1F3FD,
  0x1F931, 0x1F3FE, 0x1F931, 0x1F3FF, 0x1F932, 0x1F3FB, 0x1F932, 0x1F3FC,
  0x1F932, 0x1F3FD, 0x1F932, 0x1F3FE, 0x1F932, 0x1F3FF, 0x1F933, 0x1F3FB,
  0x1F933, 0x1F3FC, 0x1F933, 0x1F3FD, 0x1F933, 0x1F3FE, 0x1F933, 0x1F3FF,
  0x1F934, 0x1F3FB, 0x1F934, 0x1F3FC, 0x1F934, 0x1F3FD, 0x1F934, 0x1F3FE,
  0x1F934, 0x1F3FF, 0x1F935, 0x1F3FB, 0x1F935, 0x1F3FC, 0x1F935, 0x1F3FD,
  0x1F935, 0x1F3FE, 0x1F935, 0x1F3FF, 0x1F936, 0x1F3FB, 0x1F936, 0x1F3FC,
  0x1F936, 0x1F3FD, 0x1F936, 0x1F3FE, 0x1F936, 0x1F3FF, 0x1F937, 0x1F3FB,
  0x1F937, 0x1F3FC, 0x1F937, 0x1F3FD, 0x1F937, 0x1F3FE, 0x1F937, 0x1F3FF,
  0x1F938, 0x1F3FB, 0x1F938, 0x1F3FC, 0x1F938, 0x1F3FD, 0x1F938, 0x1F3FE,
  0x1F938, 0x1F3FF, 0x1F939, 0x1F3FB, 0x1F939, 0x1F3FC, 0x1F939, 0x1F3FD,
  0x1F939, 0x1F3FE, 0x1F939, 0x1F3FF, 0x1F93C, 0x1F3FB, 0x1F93C, 0x1F3FC,
  0x1F93C, 0x1F3FD, 0x1F93C, 0x1F3FE, 0x1F93C, 0x1F3FF, 0x1F93D, 0x1F3FB,
  0x1F93D, 0x1F3FC, 0x1F93D, 0x1F3FD, 0x1F93D, 0x1F3FE, 0x1F93D, 0x1F3FF,
  0x1F93E, 0x1F3FB, 0x1F93E, 0x1F3FC, 0x1F93E, 0x1F3FD, 0x1F93E, 0x1F3FE,
  0x1F93E, 0x1F3FF, 0x1F977, 0x1F3FB, 0x1F977, 0x1F3FC, 0x1F977, 0x1F3FD,
  0x1F977, 0x1F3FE, 0x1F977, 0x1F3FF, 0x1F9B5, 0x1F3FB, 0x1F9B5, 0x1F3FC,
  0x1F9B5, 0x1F3FD, 0x1F9B5, 0x1F3FE, 0x1F9B5, 0x1F3FF, 0x1F9B6, 0x1F3FB,
  0x1F9B6, 0x1F3FC, 0x1F9B6, 0x1F3FD, 0x1F9B6, 0x1F3FE, 0x1F9B6, 0x1F3FF,
  0x1F9B8, 0x1F3FB, 0x1F9B8, 0x1F3FC, 0x1F9B8, 0x1F3FD, 0x1F9B8, 0x1F3FE,
  0x1F9B8, 0x1F3FF, 0x1F9B9, 0x1F3FB, 0x1F9B9, 0x1F3FC, 0x1F9B9, 0x1F3FD,
  0x1F9B9, 0x1F3FE, 0x1F9B9, 0x1F3FF, 0x1F9BB, 0x1F3FB, 0x1F9BB, 0x1F3FC,
  0x1F9BB, 0x1F3FD, 0x1F9BB, 0x1F3FE, 0x1F9BB, 0x1F3FF, 0x1F9CD, 0x1F3FB,
  0x1F9CD, 0x1F3FC, 0x1F9CD, 0x1F3FD, 0x1F9CD, 0x1F3FE, 0x1F9CD, 0x1F3FF,
  0x1F9CE, 0x1F3FB, 0x1F9CE, 0x1F3FC, 0x1F9CE, 0x1F3FD, 0x1F9CE, 0x1F3FE,
  0x1F9CE, 0x1F3FF, 0x1F9CF, 0x1F3FB, 0x1F9CF, 0x1F3FC, 0x1F9CF, 0x1F3FD,
  0x1F9CF, 0x1F3FE, 0x1F9CF, 0x1F3FF, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FC,
  0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FF, 0x1F9D2, 0x1F3FB,
  0x1F9D2, 0x1F3FC, 0x1F9D2, 0x1F3FD, 0x1F9D2, 0x1F3FE, 0x1F9D2, 0x1F3FF,
  0x1F9D3, 0x1F3FB, 0x1F9D3, 0x1F3FC, 0x1F9D3, 0x1F3FD, 0x1F9D3, 0x1F3FE,
  0x1F9D3, 0x1F3FF, 0x1F9D4, 0x1F3FB, 0x1F9D4, 0x1F3FC, 0x1F9D4, 0x1F3FD,
  0x1F9D4, 0x1F3FE, 0x1F9D4, 0x1F3FF, 0x1F9D5, 0x1F3FB, 0x1F9D5, 0x1F3FC,
  0x1F9D5, 0x1F3FD, 0x1F9D5, 0x1F3FE, 0x1F9D5, 0x1F3FF, 0x1F9D6, 0x1F3FB,
  0x1F9D6, 0x1F3FC, 0x1F9D6, 0x1F3FD, 0x1F9D6, 0x1F3FE, 0x1F9D6, 0x1F3FF,
  0x1F9D7, 0x1F3FB, 0x1F9D7, 0x1F3FC, 0x1F9D7, 0x1F3FD, 0x1F9D7, 0x1F3FE,
  0x1F9D7, 0x1F3FF, 0x1F9D8, 0x1F3FB, 0x1F9D8, 0x1F3FC, 0x1F9D8, 0x1F3FD,
  0x1F9D8, 0x1F3FE, 0x1F9D8, 0x1F3FF, 0x1F9D9, 0x1F3FB, 0x1F9D9, 0x1F3FC,
  0x1F9D9, 0x1F3FD, 0x1F9D9, 0x1F3FE, 0x1F9D9, 0x1F3FF, 0x1F9DA, 0x1F3FB,
  0x1F9DA, 0x1F3FC, 0x1F9DA, 0x1F3FD, 0x1F9DA, 0x1F3FE, 0x1F9DA, 0x1F3FF,
  0x1F9DB, 0x1F3FB, 0x1F9DB, 0x1F3FC, 0x1F9DB, 0x1F3FD, 0x1F9DB, 0x1F3FE,
  0x1F9DB, 0x1F3FF, 0x1F9DC, 0x1F3FB, 0x1F9DC, 0x1F3FC, 0x1F9DC, 0x1F3FD,
  0x1F9DC, 0x1F3FE, 0x1F9DC, 0x1F3FF, 0x1F9DD, 0x1F3FB, 0x1F9DD, 0x1F3FC,
  0x1F9DD, 0x1F3FD, 0x1F9DD, 0x1F3FE, 0x1F9DD, 0x1F3FF, 0x1FAC3, 0x1F3FB,
  0x1FAC3, 0x1F3FC, 0x1FAC3, 0x1F3FD, 0x1FAC3, 0x1F3FE, 0x1FAC3, 0x1F3FF,
  0x1FAC4, 0x1F3FB, 0x1FAC4, 0x1F3FC, 0x1FAC4, 0x1F3FD, 0x1FAC4, 0x1F3FE,
  0x1FAC4, 0x1F3FF, 0x1FAC5, 0x1F3FB, 0x1FAC5, 0x1F3FC, 0x1FAC5, 0x1F3FD,
  0x1FAC5, 0x1F3FE, 0x1FAC5, 0x1F3FF, 0x1FAF0, 0x1F3FB, 0x1FAF0, 0x1F3FC,
  0x1FAF0, 0x1F3FD, 0x1FAF0, 0x1F3FE, 0x1FAF0, 0x1F3FF, 0x1FAF1, 0x1F3FB,
  0x1FAF1, 0x1F3FC, 0x1FAF1, 0x1F3FD, 0x1FAF1, 0x1F3FE, 0x1FAF1, 0x1F3FF,
  0x1FAF2, 0x1F3FB, 0x1FAF2, 0x1F3FC, 0x1FAF2, 0x1F3FD, 0x1FAF2, 0x1F3FE,
  0x1FAF2, 0x1F3FF, 0x1FAF3, 0x1F3FB, 0x1FAF3, 0x1F3FC, 0x1FAF3, 0x1F3FD,
  0x1FAF3, 0x1F3FE, 0x1FAF3, 0x1F3FF, 0x1FAF4, 0x1F3FB, 0x1FAF4, 0x1F3FC,
  0x1FAF4, 0x1F3FD, 0x1FAF4, 0x1F3FE, 0x1FAF4, 0x1F3FF, 0x1FAF5, 0x1F3FB,
  0x1FAF5, 0x1F3FC, 0x1FAF5, 0x1F3FD, 0x1FAF5, 0x1F3FE, 0x1FAF5, 0x1F3FF,
  0x1FAF6, 0x1F3FB, 0x1FAF6, 0x1F3FC, 0x1FAF6, 0x1F3FD, 0x1FAF6, 0x1F3FE,
  0x1FAF6, 0x1F3FF, 0x1FAF7, 0x1F3FB, 0x1FAF7, 0x1F3FC, 0x1FAF7, 0x1F3FD,
  0x1FAF7, 0x1F3FE, 0x1FAF7, 0x1F3FF, 0x1FAF8, 0x1F3FB, 0x1FAF8, 0x1F3FC,
  0x1FAF8, 0x1F3FD, 0x1FAF8, 0x1F3FE, 0x1FAF8, 0x1F3FF, 0x0023, 0xFE0F,
  0x20E3, 0x002A, 0xFE0F, 0x20E3, 0x0030, 0xFE0F, 0x20E3, 0x0031,
  0xFE0F, 0x20E3, 0x0032, 0xFE0F, 0x20E3, 0x0033, 0xFE0F, 0x20E3,
  0x0034, 0xFE0F, 0x20E3, 0x0035, 0xFE0F, 0x20E3, 0x0036, 0xFE0F,
  0x20E3, 0x0037, 0xFE0F, 0x20E3, 0x0038, 0xFE0F, 0x20E3, 0x0039,
  0xFE0F, 0x20E3, 0x1F344, 0x200D, 0x1F7EB, 0x1F34B, 0x200D, 0x1F7E9,
  0x1F408, 0x200D, 0x2B1B, 0x1F426, 0x200D, 0x2B1B, 0x1F415, 0x200D,
  0x1F9BA, 0x1F426, 0x200D, 0x1F525, 0x1F468, 0x200D, 0x1F33E, 0x1F468,
  0x200D, 0x1F373, 0x1F468, 0x200D, 0x1F37C, 0x1F468, 0x200D, 0x1F393,
  0x1F468, 0x200D, 0x1F3A4, 0x1F468, 0x200D, 0x1F3A8, 0x1F468, 0x200D,
  0x1F3EB, 0x1F468, 0x200D, 0x1F3ED, 0x1F468, 0x200D, 0x1F466, 0x1F468,
  0x200D, 0x1F467, 0x1F468, 0x200D, 0x1F4BB, 0x1F468, 0x200D, 0x1F4BC,
  0x1F468, 0x200D, 0x1F527, 0x1F468, 0x200D, 0x1F52C, 0x1F468, 0x200D,
  0x1F680, 0x1F468, 0x200D, 0x1F692, 0x1F469, 0x200D, 0x1F33E, 0x1F469,
  0x200D, 0x1F373, 0x1F469, 0x200D, 0x1F37C, 0x1F469, 0x200D, 0x1F393,
  0x1F469, 0x200D, 0x1F3A4, 0x1F469, 0x200D, 0x1F3A8, 0x1F469, 0x200D,
  0x1F3EB, 0x1F469, 0x200D, 0x1F3ED, 0x1F469, 0x200D, 0x1F466, 0x1F469,
  0x200D, 0x1F467, 0x1F469, 0x200D, 0x1F4BB, 0x1F469, 0x200D, 0x1F4BC,
  0x1F469, 0x200D, 0x1F527, 0x1F469, 0x200D, 0x1F52C, 0x1F468, 0x200D,
  0x1F9AF, 0x1F468, 0x200D, 0x1F9B0, 0x1F468, 0x200D, 0x1F9B1, 0x1F468,
  0x200D, 0x1F9B2, 0x1F468, 0x200D, 0x1F9B3, 0x1F468, 0x200D, 0x1F9BC,
  0x1F468, 0x200D, 0x1F9BD, 0x1F469, 0x200D, 0x1F680, 0x1F469, 0x200D,
  0x1F692, 0x1F469, 0x200D, 0x1F9AF, 0x1F469, 0x200D, 0x1F9B0, 0x1F469,
  0x200D, 0x1F9B1, 0x1F469, 0x200D, 0x1F9B2, 0x1F469, 0x200D, 0x1F9B3,
  0x1F469, 0x200D, 0x1F9BC, 0x1F469, 0x200D, 0x1F9BD, 0x1F62E, 0x200D,
  0x1F4A8, 0x1F635, 0x200D, 0x1F4AB, 0x1F9D1, 0x200D, 0x1F33E, 0x1F9D1,
  0x200D, 0x1F373, 0x1F9D1, 0x200D, 0x1F37C, 0x1F9D1, 0x200D, 0x1F384,
  0x1F9D1, 0x200D, 0x1F393, 0x1F9D1, 0x200D, 0x1F3A4, 0x1F9D1, 0x200D,
  0x1F3A8, 0x1F9D1, 0x200D, 0x1F3EB, 0x1F9D1, 0x200D, 0x1F3ED, 0x1F9D1,
  0x200D, 0x1F4BB, 0x1F9D1, 0x200D, 0x1F4BC, 0x1F9D1, 0x200D, 0x1F527,
  0x1F9D1, 0x200D, 0x1F52C, 0x1F9D1, 0x200D, 0x1F680, 0x1F9D1, 0x200D,
  0x1F692, 0x1F9D1, 0x200D, 0x1F9AF, 0x1F9D1, 0x200D, 0x1F9B0, 0x1F9D1,
  0x200D, 0x1F9B1, 0x1F9D1, 0x200D, 0x1F9B2, 0x1F9D1, 0x200D, 0x1F9B3,
  0x1F9D1, 0x200D, 0x1F9BC, 0x1F9D1, 0x200D, 0x1F9BD, 0x1F9D1, 0x200D,
  0x1F9D2, 0x1F9D1, 0x200D, 0x1FA70, 0x26D3, 0xFE0F, 0x200D, 0x1F4A5,
  0x2764, 0xFE0F, 0x200D, 0x1F525, 0x2764, 0xFE0F, 0x200D, 0x1FA79,
  0x1F3C3, 0x200D, 0x2640, 0xFE0F, 0x1F3C3, 0x200D, 0x2642, 0xFE0F,
  0x1F3C3, 0x200D, 0x27A1, 0xFE0F, 0x1F3C4, 0x200D, 0x2640, 0xFE0F,
  0x1F3C4, 0x200D, 0x2642, 0xFE0F, 0x1F3CA, 0x200D, 0x2640, 0xFE0F,
  0x1F3CA, 0x200D, 0x2642, 0xFE0F, 0x1F3F4, 0x200D, 0x2620, 0xFE0F,
  0x1F43B, 0x200D, 0x2744, 0xFE0F, 0x1F468, 0x200D, 0x2695, 0xFE0F,
  0x1F468, 0x200D, 0x2696, 0xFE0F, 0x1F468, 0x200D, 0x2708, 0xFE0F,
  0x1F469, 0x200D, 0x2695, 0xFE0F, 0x1F469, 0x200D, 0x2696, 0xFE0F,
  0x1F469, 0x200D, 0x2708, 0xFE0F, 0x1F46E, 0x200D, 0x2640, 0xFE0F,
  0x1F46E, 0x200D, 0x2642, 0xFE0F, 0x1F46F, 0x200D, 0x2640, 0xFE0F,
  0x1F46F, 0x200D, 0x2642, 0xFE0F, 0x1F470, 0x200D, 0x2640, 0xFE0F,
  0x1F470, 0x200D, 0x2642, 0xFE0F, 0x1F471, 0x200D, 0x2640, 0xFE0F,
  0x1F471, 0x200D, 0x2642, 0xFE0F, 0x1F473, 0x200D, 0x2640, 0xFE0F,
  0x1F473, 0x200D, 0x2642, 0xFE0F, 0x1F477, 0x200D, 0x2640, 0xFE0F,
  0x1F477, 0x200D, 0x2642, 0xFE0F, 0x1F481, 0x200D, 0x2640, 0xFE0F,
  0x1F481, 0x200D, 0x2642, 0xFE0F, 0x1F482, 0x200D, 0x2640, 0xFE0F,
  0x1F482, 0x200D, 0x2642, 0xFE0F, 0x1F486, 0x200D, 0x2640, 0xFE0F,
  0x1F486, 0x200D, 0x2642, 0xFE0F, 0x1F487, 0x200D, 0x2640, 0xFE0F,
  0x1F487, 0x200D, 0x2642, 0xFE0F, 0x1F642, 0x200D, 0x2194, 0xFE0F,
  0x1F642, 0x200D, 0x2195, 0xFE0F, 0x1F645, 0x200D, 0x2640, 0xFE0F,
  0x1F645, 0x200D, 0x2642, 0xFE0F, 0x1F646, 0x200D, 0x2640, 0xFE0F,
  0x1F646, 0x200D, 0x2642, 0xFE0F, 0x1F647, 0x200D, 0x2640, 0xFE0F,
  0x1F647, 0x200D, 0x2642, 0xFE0F, 0x1F64B, 0x200D, 0x2640, 0xFE0F,
  0x1F64B, 0x200D, 0x2642, 0xFE0F, 0x1F64D, 0x200D, 0x2640, 0xFE0F,
  0x1F64D, 0x200D, 0x2642, 0xFE0F, 0x1F64E, 0x200D, 0x2640, 0xFE0F,
  0x1F64E, 0x200D, 0x2642, 0xFE0F, 0x1F636, 0x200D, 0x1F32B, 0xFE0F,
  0x1F6A3, 0x200D, 0x2640, 0xFE0F, 0x1F6A3, 0x200D, 0x2642, 0xFE0F,
  0x1F6B4, 0x200D, 0x2640, 0xFE0F, 0x1F6B4, 0x200D, 0x2642, 0xFE0F,
  0x1F6B5, 0x200D, 0x2640, 0xFE0F, 0x1F6B5, 0x200D, 0x2642, 0xFE0F,
  0x1F6B6, 0x200D, 0x2640, 0xFE0F, 0x1F6B6, 0x200D, 0x2642, 0xFE0F,
  0x1F6B6, 0x200D, 0x27A1, 0xFE0F, 0x1F926, 0x200D, 0x2640, 0xFE0F,
  0x1F926, 0x200D, 0x2642, 0xFE0F, 0x1F935, 0x200D, 0x2640, 0xFE0F,
  0x1F935, 0x200D, 0x2642, 0xFE0F, 0x1F937, 0x200D, 0x2640, 0xFE0F,
  0x1F937, 0x200D, 0x2642, 0xFE0F, 0x1F938, 0x200D, 0x2640, 0xFE0F,
  0x1F938, 0x200D, 0x2642, 0xFE0F, 0x1F939, 0x200D, 0x2640, 0xFE0F,
  0x1F939, 0x200D, 0x2642, 0xFE0F, 0x1F93C, 0x200D, 0x2640, 0xFE0F,
  0x1F93C, 0x200D, 0x2642, 0xFE0F, 0x1F93D, 0x200D, 0x2640, 0xFE0F,
  0x1F93D, 0x200D, 0x2642, 0xFE0F, 0x1F93E, 0x200D, 0x2640, 0xFE0F,
  0x1F93E, 0x200D, 0x2642, 0xFE0F, 0x1F9B8, 0x200D, 0x2640, 0xFE0F,
  0x1F9B8, 0x200D, 0x2642, 0xFE0F, 0x1F9B9, 0x200D, 0x2640, 0xFE0F,
  0x1F9B9, 0x200D, 0x2642, 0xFE0F, 0x1F9CD, 0x200D, 0x2640, 0xFE0F,
  0x1F9CD, 0x200D, 0x2642, 0xFE0F, 0x1F9CE, 0x200D, 0x2640, 0xFE0F,
  0x1F9CE, 0x200D, 0x2642, 0xFE0F, 0x1F9CE, 0x200D, 0x27A1, 0xFE0F,
  0x1F9CF, 0x200D, 0x2640, 0xFE0F, 0x1F9CF, 0x200D, 0x2642, 0xFE0F,
  0x1F9D1, 0x200D, 0x2695, 0xFE0F, 0x1F9D1, 0x200D, 0x2696, 0xFE0F,
  0x1F9D1, 0x200D, 0x2708, 0xFE0F, 0x1F9D4, 0x200D, 0x2640, 0xFE0F,
  0x1F9D4, 0x200D, 0x2642, 0xFE0F, 0x1F9D6, 0x200D, 0x2640, 0xFE0F,
  0x1F9D6, 0x200D, 0x2642, 0xFE0F, 0x1F9D7, 0x200D, 0x2640, 0xFE0F,
  0x1F9D7, 0x200D, 0x2642, 0xFE0F, 0x1F9D8, 0x200D, 0x2640, 0xFE0F,
  0x1F9D8, 0x200D, 0x2642, 0xFE0F, 0x1F9D9, 0x200D, 0x2640, 0xFE0F,
  0x1F9D9, 0x200D, 0x2642, 0xFE0F, 0x1F9DA, 0x200D, 0x2640, 0xFE0F,
  0x1F9DA, 0x200D, 0x2642, 0xFE0F, 0x1F9DB, 0x200D, 0x2640, 0xFE0F,
  0x1F9DB, 0x200D, 0x2642, 0xFE0F, 0x1F9DC, 0x200D, 0x2640, 0xFE0F,
  0x1F9DC, 0x200D, 0x2642, 0xFE0F, 0x1F9DD, 0x200D, 0x2640, 0xFE0F,
  0x1F9DD, 0x200D, 0x2642, 0xFE0F, 0x1F9DE, 0x200D, 0x2640, 0xFE0F,
  0x1F9DE, 0x200D, 0x2642, 0xFE0F, 0x1F9DF, 0x200D, 0x2640, 0xFE0F,
  0x1F9DF, 0x200D, 0x2642, 0xFE0F, 0x1F3F3, 0xFE0F, 0x200D, 0x1F308,
  0x1F468, 0x1F3FB, 0x200D, 0x1F33E, 0x1F468, 0x1F3FB, 0x200D, 0x1F373,
  0x1F468, 0x1F3FB, 0x200D, 0x1F37C, 0x1F468, 0x1F3FB, 0x200D, 0x1F393,
  0x1F468, 0x1F3FB, 0x200D, 0x1F3A4, 0x1F468, 0x1F3FB, 0x200D, 0x1F3A8,
  0x1F468, 0x1F3FB, 0x200D, 0x1F3EB, 0x1F468, 0x1F3FB, 0x200D, 0x1F3ED,
  0x1F468, 0x1F3FB, 0x200D, 0x1F4BB, 0x1F468, 0x1F3FB, 0x200D, 0x1F4BC,
  0x1F468, 0x1F3FB, 0x200D, 0x1F527, 0x1F468, 0x1F3FB, 0x200D, 0x1F52C,
  0x1F468, 0x1F3FB, 0x200D, 0x1F680, 0x1F468, 0x1F3FB, 0x200D, 0x1F692,
  0x1F468, 0x1F3FC, 0x200D, 0x1F33E, 0x1F468, 0x1F3FC, 0x200D, 0x1F373,
  0x1F468, 0x1F3FC, 0x200D, 0x1F37C, 0x1F468, 0x1F3FC, 0x200D, 0x1F393,
  0x1F468, 0x1F3FC, 0x200D, 0x1F3A4, 0x1F468, 0x1F3FC, 0x200D, 0x1F3A8,
  0x1F468, 0x1F3FC, 0x200D, 0x1F3EB, 0x1F468, 0x1F3FC, 0x200D, 0x1F3ED,
  0x1F468, 0x1F3FC, 0x200D, 0x1F4BB, 0x1F468, 0x1F3FC, 0x200D, 0x1F4BC,
  0x1F468, 0x1F3FC, 0x200D, 0x1F527, 0x1F468, 0x1F3FC, 0x200D, 0x1F52C,
  0x1F468, 0x1F3FB, 0x200D, 0x1F9AF, 0x1F468, 0x1F3FB, 0x200D, 0x1F9B0,
  0x1F468, 0x1F3FB, 0x200D, 0x1F9B1, 0x1F468, 0x1F3FB, 0x200D, 0x1F9B2,
  0x1F468, 0x1F3FB, 0x200D, 0x1F9B3, 0x1F468, 0x1F3FB, 0x200D, 0x1F9BC,
  0x1F468, 0x1F3FB, 0x200D, 0x1F9BD, 0x1F468, 0x1F3FC, 0x200D, 0x1F680,
  0x1F468, 0x1F3FC, 0x200D, 0x1F692, 0x1F468, 0x1F3FD, 0x200D, 0x1F33E,
  0x1F468, 0x1F3FD, 0x200D, 0x1F373, 0x1F468, 0x1F3FD, 0x200D, 0x1F37C,
  0x1F468, 0x1F3FD, 0x200D, 0x1F393, 0x1F468, 0x1F3FD, 0x200D, 0x1F3A4,
  0x1F468, 0x1F3FD, 0x200D, 0x1F3A8, 0x1F468, 0x1F3FD, 0x200D, 0x1F3EB,
  0x1F468, 0x1F3FD, 0x200D, 0x1F3ED, 0x1F468, 0x1F3FD, 0x200D, 0x1F4BB,
  0x1F468, 0x1F3FD, 0x200D, 0x1F4BC, 0x1F468, 0x1F3FD, 0x200D, 0x1F527,
  0x1F468, 0x1F3FD, 0x200D, 0x1F52C, 0x1F468, 0x1F3FC, 0x200D, 0x1F9AF,
  0x1F468, 0x1F3FC, 0x200D, 0x1F9B0, 0x1F468, 0x1F3FC, 0x200D, 0x1F9B1,
  0x1F468, 0x1F3FC, 0x200D, 0x1F9B2, 0x1F468, 0x1F3FC, 0x200D, 0x1F9B3,
  0x1F468, 0x1F3FC, 0x200D, 0x1F9BC, 0x1F468, 0x1F3FC, 0x200D, 0x1F9BD,
  0x1F468, 0x1F3FD, 0x200D, 0x1F680, 0x1F468, 0x1F3FD, 0x200D, 0x1F692,
  0x1F468, 0x1F3FE, 0x200D, 0x1F33E, 0x1F468, 0x1F3FE, 0x200D, 0x1F373,
  0x1F468, 0x1F3FE, 0x200D, 0x1F37C, 0x1F468, 0x1F3FE, 0x200D, 0x1F393,
  0x1F468, 0x1F3FE, 0x200D, 0x1F3A4, 0x1F468, 0x1F3FE, 0x200D, 0x1F3A8,
  0x1F468, 0x1F3FE, 0x200D, 0x1F3EB, 0x1F468, 0x1F3FE, 0x200D, 0x1F3ED,
  0x1F468, 0x1F3FE, 0x200D, 0x1F4BB, 0x1F468, 0x1F3FE, 0x200D, 0x1F4BC,
  0x1F468, 0x1F3FE, 0x200D, 0x1F527, 0x1F468, 0x1F3FE, 0x200D, 0x1F52C,
  0x1F468, 0x1F3FD, 0x200D, 0x1F9AF, 0x1F468, 0x1F3FD, 0x200D, 0x1F9B0,
  0x1F468, 0x1F3FD, 0x200D, 0x1F9B1, 0x1F468, 0x1F3FD, 0x200D, 0x1F9B2,
  0x1F468, 0x1F3FD, 0x200D, 0x1F9B3, 0x1F468, 0x1F3FD, 0x200D, 0x1F9BC,
  0x1F468, 0x1F3FD, 0x200D, 0x1F9BD, 0x1F468, 0x1F3FE, 0x200D, 0x1F680,
  0x1F468, 0x1F3FE, 0x200D, 0x1F692, 0x1F468, 0x1F3FF, 0x200D, 0x1F33E,
  0x1F468, 0x1F3FF, 0x200D, 0x1F373, 0x1F468, 0x1F3FF, 0x200D, 0x1F37C,
  0x1F468, 0x1F3FF, 0x200D, 0x1F393, 0x1F468, 0x1F3FF, 0x200D, 0x1F3A4,
  0x1F468, 0x1F3FF, 0x200D, 0x1F3A8, 0x1F468, 0x1F3FF, 0x200D, 0x1F3EB,
  0x1F468, 0x1F3FF, 0x200D, 0x1F3ED, 0x1F468, 0x1F3FF, 0x200D, 0x1F4BB,
  0x1F468, 0x1F3FF, 0x200D, 0x1F4BC, 0x1F468, 0x1F3FF, 0x200D, 0x1F527,
  0x1F468, 0x1F3FF, 0x200D, 0x1F52C, 0x1F468, 0x1F3FE, 0x200D, 0x1F9AF,
  0x1F468, 0x1F3FE, 0x200D, 0x1F9B0, 0x1F468, 0x1F3FE, 0x200D, 0x1F9B1,
  0x1F468, 0x1F3FE, 0x200D, 0x1F9B2, 0x1F468, 0x1F3FE, 0x200D, 0x1F9B3,
  0x1F468, 0x1F3FE, 0x200D, 0x1F9BC, 0x1F468, 0x1F3FE, 0x200D, 0x1F9BD,
  0x1F468, 0x1F3FF, 0x200D, 0x1F680, 0x1F468, 0x1F3FF, 0x200D, 0x1F692,
  0x1F468, 0x1F3FF, 0x200D, 0x1F9AF, 0x1F468, 0x1F3FF, 0x200D, 0x1F9B0,
  0x1F468, 0x1F3FF, 0x200D, 0x1F9B1, 0x1F468, 0x1F3FF, 0x200D, 0x1F9B2,
  0x1F468, 0x1F3FF, 0x200D, 0x1F9B3, 0x1F468, 0x1F3FF, 0x200D, 0x1F9BC,
  0x1F468, 0x1F3FF, 0x200D, 0x1F9BD, 0x1F469, 0x1F3FB, 0x200D, 0x1F33E,
  0x1F469, 0x1F3FB, 0x200D, 0x1F373, 0x1F469, 0x1F3FB, 0x200D, 0x1F37C,
  0x1F469, 0x1F3FB, 0x200D, 0x1F393, 0x1F469, 0x1F3FB, 0x200D, 0x1F3A4,
  0x1F469, 0x1F3FB, 0x200D, 0x1F3A8, 0x1F469, 0x1F3FB, 0x200D, 0x1F3EB,
  0x1F469, 0x1F3FB, 0x200D, 0x1F3ED, 0x1F469, 0x1F3FB, 0x200D, 0x1F4BB,
  0x1F469, 0x1F3FB, 0x200D, 0x1F4BC, 0x1F469, 0x1F3FB, 0x200D, 0x1F527,
  0x1F469, 0x1F3FB, 0x200D, 0x1F52C, 0x1F469, 0x1F3FB, 0x200D, 0x1F680,
  0x1F469, 0x1F3FB, 0x200D, 0x1F692, 0x1F469, 0x1F3FC, 0x200D, 0x1F33E,
  0x1F469, 0x1F3FC, 0x200D, 0x1F373, 0x1F469, 0x1F3FC, 0x200D, 0x1F37C,
  0x1F469, 0x1F3FC, 0x200D, 0x1F393, 0x1F469, 0x1F3FC, 0x200D, 0x1F3A4,
  0x1F469, 0x1F3FC, 0x200D, 0x1F3A8, 0x1F469, 0x1F3FC, 0x200D, 0x1F3EB,
  0x1F469, 0x1F3FC, 0x200D, 0x1F3ED, 0x1F469, 0x1F3FC, 0x200D, 0x1F4BB,
  0x1F469, 0x1F3FC, 0x200D, 0x1F4BC, 0x1F469, 0x1F3FC, 0x200D, 0x1F527,
  0x1F469, 0x1F3FC, 0x200D, 0x1F52C, 0x1F469, 0x1F3FB, 0x200D, 0x1F9AF,
  0x1F469, 0x1F3FB, 0x200D, 0x1F9B0, 0x1F469, 0x1F3FB, 0x200D, 0x1F9B1,
  0x1F469, 0x1F3FB, 0x200D, 0x1F9B2, 0x1F469, 0x1F3FB, 0x200D, 0x1F9B3,
  0x1F469, 0x1F3FB, 0x200D, 0x1F9BC, 0x1F469, 0x1F3FB, 0x200D, 0x1F9BD,
  0x1F469, 0x1F3FC, 0x200D, 0x1F680, 0x1F469, 0x1F3FC, 0x200D, 0x1F692,
  0x1F469, 0x1F3FD, 0x200D, 0x1F33E, 0x1F469, 0x1F3FD, 0x200D, 0x1F373,
  0x1F469, 0x1F3FD, 0x200D, 0x1F37C, 0x1F469, 0x1F3FD, 0x200D, 0x1F393,
  0x1F469, 0x1F3FD, 0x200D, 0x1F3A4, 0x1F469, 0x1F3FD, 0x200D, 0x1F3A8,
  0x1F469, 0x1F3FD, 0x200D, 0x1F3EB, 0x1F469, 0x1F3FD, 0x200D, 0x1F3ED,
  0x1F469, 0x1F3FD, 0x200D, 0x1F4BB, 0x1F469, 0x1F3FD, 0x200D, 0x1F4BC,
  0x1F469, 0x1F3FD, 0x200D, 0x1F527, 0x1F469, 0x1F3FD, 0x200D, 0x1F52C,
  0x1F469, 0x1F3FC, 0x200D, 0x1F9AF, 0x1F469, 0x1F3FC, 0x200D, 0x1F9B0,
  0x1F469, 0x1F3FC, 0x200D, 0x1F9B1, 0x1F469, 0x1F3FC, 0x200D, 0x1F9B2,
  0x1F469, 0x1F3FC, 0x200D, 0x1F9B3, 0x1F469, 0x1F3FC, 0x200D, 0x1F9BC,
  0x1F469, 0x1F3FC, 0x200D, 0x1F9BD, 0x1F469, 0x1F3FD, 0x200D, 0x1F680,
  0x1F469, 0x1F3FD, 0x200D, 0x1F692, 0x1F469, 0x1F3FE, 0x200D, 0x1F33E,
  0x1F469, 0x1F3FE, 0x200D, 0x1F373, 0x1F469, 0x1F3FE, 0x200D, 0x1F37C,
  0x1F469, 0x1F3FE, 0x200D, 0x1F393, 0x1F469, 0x1F3FE, 0x200D, 0x1F3A4,
  0x1F469, 0x1F3FE, 0x200D, 0x1F3A8, 0x1F469, 0x1F3FE, 0x200D, 0x1F3EB,
  0x1F469, 0x1F3FE, 0x200D, 0x1F3ED, 0x1F469, 0x1F3FE, 0x200D, 0x1F4BB,
  0x1F469, 0x1F3FE, 0x200D, 0x1F4BC, 0x1F469, 0x1F3FE, 0x200D, 0x1F527,
  0x1F469, 0x1F3FE, 0x200D, 0x1F52C, 0x1F469, 0x1F3FD, 0x200D, 0x1F9AF,
  0x1F469, 0x1F3FD, 0x200D, 0x1F9B0, 0x1F469, 0x1F3FD, 0x200D, 0x1F9B1,
  0x1F469, 0x1F3FD, 0x200D, 0x1F9B2, 0x1F469, 0x1F3FD, 0x200D, 0x1F9B3,
  0x1F469, 0x1F3FD, 0x200D, 0x1F9BC, 0x1F469, 0x1F3FD, 0x200D, 0x1F9BD,
  0x1F469, 0x1F3FE, 0x200D, 0x1F680, 0x1F469, 0x1F3FE, 0x200D, 0x1F692,
  0x1F469, 0x1F3FF, 0x200D, 0x1F33E, 0x1F469, 0x1F3FF, 0x200D, 0x1F373,
  0x1F469, 0x1F3FF, 0x200D, 0x1F37C, 0x1F469, 0x1F3FF, 0x200D, 0x1F393,
  0x1F469, 0x1F3FF, 0x200D, 0x1F3A4, 0x1F469, 0x1F3FF, 0x200D, 0x1F3A8,
  0x1F469, 0x1F3FF, 0x200D, 0x1F3EB, 0x1F469, 0x1F3FF, 0x200D, 0x1F3ED,
  0x1F469, 0x1F3FF, 0x200D, 0x1F4BB, 0x1F469, 0x1F3FF, 0x200D, 0x1F4BC,
  0x1F469, 0x1F3FF, 0x200D, 0x1F527, 0x1F469, 0x1F3FF, 0x200D, 0x1F52C,
  0x1F469, 0x1F3FE, 0x200D, 0x1F9AF, 0x1F469, 0x1F3FE, 0x200D, 0x1F9B0,
  0x1F469, 0x1F3FE, 0x200D, 0x1F9B1, 0x1F469, 0x1F3FE, 0x200D, 0x1F9B2,
  0x1F469, 0x1F3FE, 0x200D, 0x1F9B3, 0x1F469, 0x1F3FE, 0x200D, 0x1F9BC,
  0x1F469, 0x1F3FE, 0x200D, 0x1F9BD, 0x1F469, 0x1F3FF, 0x200D, 0x1F680,
  0x1F469, 0x1F3FF, 0x200D, 0x1F692, 0x1F469, 0x1F3FF, 0x200D, 0x1F9AF,
  0x1F469, 0x1F3FF, 0x200D, 0x1F9B0, 0x1F469, 0x1F3FF, 0x200D, 0x1F9B1,
  0x1F469, 0x1F3FF, 0x200D, 0x1F9B2, 0x1F469, 0x1F3FF, 0x200D, 0x1F9B3,
  0x1F469, 0x1F3FF, 0x200D, 0x1F9BC, 0x1F469, 0x1F3FF, 0x200D, 0x1F9BD,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F33E, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F373,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F37C, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F384,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F393, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F3A4,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F3A8, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F3EB,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F3ED, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F4BB,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F4BC, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F527,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F52C, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F680,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F692, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F33E,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F373, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F37C,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F384, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F393,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F3A4, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F3A8,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F3EB, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F3ED,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F4BB, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F4BC,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F527, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F52C,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F9AF, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F9B0,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F9B1, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F9B2,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F9B3, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F9BC,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F9BD, 0x1F9D1, 0x1F3FB, 0x200D, 0x1FA70,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F680, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F692,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F33E, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F373,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F37C, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F384,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F393, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F3A4,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F3A8, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F3EB,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F3ED, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F4BB,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F4BC, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F527,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F52C, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F9AF,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F9B0, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F9B1,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F9B2, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F9B3,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F9BC, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F9BD,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1FA70, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F680,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F692, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F33E,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F373, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F37C,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F384, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F393,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F3A4, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F3A8,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F3EB, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F3ED,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F4BB, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F4BC,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F527, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F52C,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F9AF, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F9B0,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F9B1, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F9B2,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F9B3, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F9BC,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F9BD, 0x1F9D1, 0x1F3FD, 0x200D, 0x1FA70,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F680, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F692,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F33E, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F373,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F37C, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F384,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F393, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F3A4,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F3A8, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F3EB,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F3ED, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F4BB,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F4BC, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F527,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F52C, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F9AF,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F9B0, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F9B1,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F9B2, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F9B3,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F9BC, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F9BD,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1FA70, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F680,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F692, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F9AF,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F9B0, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F9B1,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F9B2, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F9B3,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F9BC, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F9BD,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1FA70, 0x26F9, 0xFE0F, 0x200D, 0x2640,
  0xFE0F, 0x26F9, 0xFE0F, 0x200D, 0x2642, 0xFE0F, 0x26F9, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x26F9, 0x1F3FB, 0x200D, 0x2642, 0xFE0F,
  0x26F9, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x26F9, 0x1F3FC, 0x200D,
  0x2642, 0xFE0F, 0x26F9, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x26F9,
  0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x26F9, 0x1F3FE, 0x200D, 0x2640,
  0xFE0F, 0x26F9, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x26F9, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x26F9, 0x1F3FF, 0x200D, 0x2642, 0xFE0F,
  0x1F468, 0x200D, 0x1F466, 0x200D, 0x1F466, 0x1F468, 0x200D, 0x1F467,
  0x200D, 0x1F466, 0x1F468, 0x200D, 0x1F467, 0x200D, 0x1F467, 0x1F468,
  0x200D, 0x1F468, 0x200D, 0x1F466, 0x1F468, 0x200D, 0x1F468, 0x200D,
  0x1F467, 0x1F468, 0x200D, 0x1F469, 0x200D, 0x1F466, 0x1F468, 0x200D,
  0x1F469, 0x200D, 0x1F467, 0x1F469, 0x200D, 0x1F466, 0x200D, 0x1F466,
  0x1F469, 0x200D, 0x1F467, 0x200D, 0x1F466, 0x1F469, 0x200D, 0x1F467,
  0x200D, 0x1F467, 0x1F469, 0x200D, 0x1F469, 0x200D, 0x1F466, 0x1F469,
  0x200D, 0x1F469, 0x200D, 0x1F467, 0x1F9D1, 0x200D, 0x1F91D, 0x200D,
  0x1F9D1, 0x1F9D1, 0x200D, 0x1F9D1, 0x200D, 0x1F9D2, 0x1F9D1, 0x200D,
  0x1F9D2, 0x200D, 0x1F9D2, 0x1F3CB, 0xFE0F, 0x200D, 0x2640, 0xFE0F,
  0x1F3CB, 0xFE0F, 0x200D, 0x2642, 0xFE0F, 0x1F3CC, 0xFE0F, 0x200D,
  0x2640, 0xFE0F, 0x1F3CC, 0xFE0F, 0x200D, 0x2642, 0xFE0F, 0x1F3F3,
  0xFE0F, 0x200D, 0x26A7, 0xFE0F, 0x1F441, 0xFE0F, 0x200D, 0x1F5E8,
  0xFE0F, 0x1F575, 0xFE0F, 0x200D, 0x2640, 0xFE0F, 0x1F575, 0xFE0F,
  0x200D, 0x2642, 0xFE0F, 0x1F3C3, 0x1F3FB, 0x200D, 0x2640, 0xFE0F,
  0x1F3C3, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F3C3, 0x1F3FB, 0x200D,
  0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F3C3,
  0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F3C3, 0x1F3FC, 0x200D, 0x27A1,
  0xFE0F, 0x1F3C3, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F3C3, 0x1F3FD,
  0x200D, 0x2642, 0xFE0F, 0x1F3C3, 0x1F3FD, 0x200D, 0x27A1, 0xFE0F,
  0x1F3C3, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F3C3, 0x1F3FE, 0x200D,
  0x2642, 0xFE0F, 0x1F3C3, 0x1F3FE, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3,
  0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F3C3, 0x1F3FF, 0x200D, 0x2642,
  0xFE0F, 0x1F3C3, 0x1F3FF, 0x200D, 0x27A1, 0xFE0F, 0x1F3C4, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x1F3C4, 0x1F3FB, 0x200D, 0x2642, 0xFE0F,
  0x1F3C4, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F3C4, 0x1F3FC, 0x200D,
  0x2642, 0xFE0F, 0x1F3C4, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F3C4,
  0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F3C4, 0x1F3FE, 0x200D, 0x2640,
  0xFE0F, 0x1F3C4, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F3C4, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x1F3C4, 0x1F3FF, 0x200D, 0x2642, 0xFE0F,
  0x1F3CA, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F3CA, 0x1F3FB, 0x200D,
  0x2642, 0xFE0F, 0x1F3CA, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F3CA,
  0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F3CA, 0x1F3FD, 0x200D, 0x2640,
  0xFE0F, 0x1F3CA, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F3CA, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x1F3CA, 0x1F3FE, 0x200D, 0x2642, 0xFE0F,
  0x1F3CA, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F3CA, 0x1F3FF, 0x200D,
  0x2642, 0xFE0F, 0x1F3CB, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F3CB,
  0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F3CB, 0x1F3FC, 0x200D, 0x2640,
  0xFE0F, 0x1F3CB, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F3CB, 0x1F3FD,
  0x200D, 0x2640, 0xFE0F, 0x1F3CB, 0x1F3FD, 0x200D, 0x2642, 0xFE0F,
  0x1F3CB, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F3CB, 0x1F3FE, 0x200D,
  0x2642, 0xFE0F, 0x1F3CB, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F3CB,
  0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F3CC, 0x1F3FB, 0x200D, 0x2640,
  0xFE0F, 0x1F3CC, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F3CC, 0x1F3FC,
  0x200D, 0x2640, 0xFE0F, 0x1F3CC, 0x1F3FC, 0x200D, 0x2642, 0xFE0F,
  0x1F3CC, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F3CC, 0x1F3FD, 0x200D,
  0x2642, 0xFE0F, 0x1F3CC, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F3CC,
  0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F3CC, 0x1F3FF, 0x200D, 0x2640,
  0xFE0F, 0x1F3CC, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F468, 0x1F3FB,
  0x200D, 0x2695, 0xFE0F, 0x1F468, 0x1F3FB, 0x200D, 0x2696, 0xFE0F,
  0x1F468, 0x1F3FB, 0x200D, 0x2708, 0xFE0F, 0x1F468, 0x1F3FC, 0x200D,
  0x2695, 0xFE0F, 0x1F468, 0x1F3FC, 0x200D, 0x2696, 0xFE0F, 0x1F468,
  0x1F3FC, 0x200D, 0x2708, 0xFE0F, 0x1F468, 0x1F3FD, 0x200D, 0x2695,
  0xFE0F, 0x1F468, 0x1F3FD, 0x200D, 0x2696, 0xFE0F, 0x1F468, 0x1F3FD,
  0x200D, 0x2708, 0xFE0F, 0x1F468, 0x1F3FE, 0x200D, 0x2695, 0xFE0F,
  0x1F468, 0x1F3FE, 0x200D, 0x2696, 0xFE0F, 0x1F468, 0x1F3FE, 0x200D,
  0x2708, 0xFE0F, 0x1F468, 0x1F3FF, 0x200D, 0x2695, 0xFE0F, 0x1F468,
  0x1F3FF, 0x200D, 0x2696, 0xFE0F, 0x1F468, 0x1F3FF, 0x200D, 0x2708,
  0xFE0F, 0x1F469, 0x1F3FB, 0x200D, 0x2695, 0xFE0F, 0x1F469, 0x1F3FB,
  0x200D, 0x2696, 0xFE0F, 0x1F469, 0x1F3FB, 0x200D, 0x2708, 0xFE0F,
  0x1F469, 0x1F3FC, 0x200D, 0x2695, 0xFE0F, 0x1F469, 0x1F3FC, 0x200D,
  0x2696, 0xFE0F, 0x1F469, 0x1F3FC, 0x200D, 0x2708, 0xFE0F, 0x1F469,
  0x1F3FD, 0x200D, 0x2695, 0xFE0F, 0x1F469, 0x1F3FD, 0x200D, 0x2696,
  0xFE0F, 0x1F469, 0x1F3FD, 0x200D, 0x2708, 0xFE0F, 0x1F469, 0x1F3FE,
  0x200D, 0x2695, 0xFE0F, 0x1F469, 0x1F3FE, 0x200D, 0x2696, 0xFE0F,
  0x1F469, 0x1F3FE, 0x200D, 0x2708, 0xFE0F, 0x1F469, 0x1F3FF, 0x200D,
  0x2695, 0xFE0F, 0x1F469, 0x1F3FF, 0x200D, 0x2696, 0xFE0F, 0x1F469,
  0x1F3FF, 0x200D, 0x2708, 0xFE0F, 0x1F46E, 0x1F3FB, 0x200D, 0x2640,
  0xFE0F, 0x1F46E, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F46E, 0x1F3FC,
  0x200D, 0x2640, 0xFE0F, 0x1F46E, 0x1F3FC, 0x200D, 0x2642, 0xFE0F,
  0x1F46E, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F46E, 0x1F3FD, 0x200D,
  0x2642, 0xFE0F, 0x1F46E, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F46E,
  0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F46E, 0x1F3FF, 0x200D, 0x2640,
  0xFE0F, 0x1F46E, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F46F, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x1F46F, 0x1F3FB, 0x200D, 0x2642, 0xFE0F,
  0x1F46F, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F46F, 0x1F3FC, 0x200D,
  0x2642, 0xFE0F, 0x1F46F, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F46F,
  0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F46F, 0x1F3FE, 0x200D, 0x2640,
  0xFE0F, 0x1F46F, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F46F, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x1F46F, 0x1F3FF, 0x200D, 0x2642, 0xFE0F,
  0x1F470, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F470, 0x1F3FB, 0x200D,
  0x2642, 0xFE0F, 0x1F470, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F470,
  0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F470, 0x1F3FD, 0x200D, 0x2640,
  0xFE0F, 0x1F470, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F470, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x1F470, 0x1F3FE, 0x200D, 0x2642, 0xFE0F,
  0x1F470, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F470, 0x1F3FF, 0x200D,
  0x2642, 0xFE0F, 0x1F471, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F471,
  0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F471, 0x1F3FC, 0x200D, 0x2640,
  0xFE0F, 0x1F471, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F471, 0x1F3FD,
  0x200D, 0x2640, 0xFE0F, 0x1F471, 0x1F3FD, 0x200D, 0x2642, 0xFE0F,
  0x1F471, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F471, 0x1F3FE, 0x200D,
  0x2642, 0xFE0F, 0x1F471, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F471,
  0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F473, 0x1F3FB, 0x200D, 0x2640,
  0xFE0F, 0x1F473, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F473, 0x1F3FC,
  0x200D, 0x2640, 0xFE0F, 0x1F473, 0x1F3FC, 0x200D, 0x2642, 0xFE0F,
  0x1F473, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F473, 0x1F3FD, 0x200D,
  0x2642, 0xFE0F, 0x1F473, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F473,
  0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F473, 0x1F3FF, 0x200D, 0x2640,
  0xFE0F, 0x1F473, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F477, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x1F477, 0x1F3FB, 0x200D, 0x2642, 0xFE0F,
  0x1F477, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F477, 0x1F3FC, 0x200D,
  0x2642, 0xFE0F, 0x1F477, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F477,
  0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F477, 0x1F3FE, 0x200D, 0x2640,
  0xFE0F, 0x1F477, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F477, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x1F477, 0x1F3FF, 0x200D, 0x2642, 0xFE0F,
  0x1F481, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F481, 0x1F3FB, 0x200D,
  0x2642, 0xFE0F, 0x1F481, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F481,
  0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F481, 0x1F3FD, 0x200D, 0x2640,
  0xFE0F, 0x1F481, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F481, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x1F481, 0x1F3FE, 0x200D, 0x2642, 0xFE0F,
  0x1F481, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F481, 0x1F3FF, 0x200D,
  0x2642, 0xFE0F, 0x1F482, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F482,
  0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F482, 0x1F3FC, 0x200D, 0x2640,
  0xFE0F, 0x1F482, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F482, 0x1F3FD,
  0x200D, 0x2640, 0xFE0F, 0x1F482, 0x1F3FD, 0x200D, 0x2642, 0xFE0F,
  0x1F482, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F482, 0x1F3FE, 0x200D,
  0x2642, 0xFE0F, 0x1F482, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F482,
  0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F486, 0x1F3FB, 0x200D, 0x2640,
  0xFE0F, 0x1F486, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F486, 0x1F3FC,
  0x200D, 0x2640, 0xFE0F, 0x1F486, 0x1F3FC, 0x200D, 0x2642, 0xFE0F,
  0x1F486, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F486, 0x1F3FD, 0x200D,
  0x2642, 0xFE0F, 0x1F486, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F486,
  0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F486, 0x1F3FF, 0x200D, 0x2640,
  0xFE0F, 0x1F486, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F487, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x1F487, 0x1F3FB, 0x200D, 0x2642, 0xFE0F,
  0x1F487, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F487, 0x1F3FC, 0x200D,
  0x2642, 0xFE0F, 0x1F487, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F487,
  0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F487, 0x1F3FE, 0x200D, 0x2640,
  0xFE0F, 0x1F487, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F487, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x1F487, 0x1F3FF, 0x200D, 0x2642, 0xFE0F,
  0x1F575, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F575, 0x1F3FB, 0x200D,
  0x2642, 0xFE0F, 0x1F575, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F575,
  0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F575, 0x1F3FD, 0x200D, 0x2640,
  0xFE0F, 0x1F575, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F575, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x1F575, 0x1F3FE, 0x200D, 0x2642, 0xFE0F,
  0x1F575, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F575, 0x1F3FF, 0x200D,
  0x2642, 0xFE0F, 0x1F645, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F645,
  0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F645, 0x1F3FC, 0x200D, 0x2640,
  0xFE0F, 0x1F645, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F645, 0x1F3FD,
  0x200D, 0x2640, 0xFE0F, 0x1F645, 0x1F3FD, 0x200D, 0x2642, 0xFE0F,
  0x1F645, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F645, 0x1F3FE, 0x200D,
  0x2642, 0xFE0F, 0x1F645, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F645,
  0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F646, 0x1F3FB, 0x200D, 0x2640,
  0xFE0F, 0x1F646, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F646, 0x1F3FC,
  0x200D, 0x2640, 0xFE0F, 0x1F646, 0x1F3FC, 0x200D, 0x2642, 0xFE0F,
  0x1F646, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F646, 0x1F3FD, 0x200D,
  0x2642, 0xFE0F, 0x1F646, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F646,
  0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F646, 0x1F3FF, 0x200D, 0x2640,
  0xFE0F, 0x1F646, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F647, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x1F647, 0x1F3FB, 0x200D, 0x2642, 0xFE0F,
  0x1F647, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F647, 0x1F3FC, 0x200D,
  0x2642, 0xFE0F, 0x1F647, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F647,
  0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F647, 0x1F3FE, 0x200D, 0x2640,
  0xFE0F, 0x1F647, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F647, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x1F647, 0x1F3FF, 0x200D, 0x2642, 0xFE0F,
  0x1F64B, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F64B, 0x1F3FB, 0x200D,
  0x2642, 0xFE0F, 0x1F64B, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F64B,
  0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F64B, 0x1F3FD, 0x200D, 0x2640,
  0xFE0F, 0x1F64B, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F64B, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x1F64B, 0x1F3FE, 0x200D, 0x2642, 0xFE0F,
  0x1F64B, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F64B, 0x1F3FF, 0x200D,
  0x2642, 0xFE0F, 0x1F64D, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F64D,
  0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F64D, 0x1F3FC, 0x200D, 0x2640,
  0xFE0F, 0x1F64D, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F64D, 0x1F3FD,
  0x200D, 0x2640, 0xFE0F, 0x1F64D, 0x1F3FD, 0x200D, 0x2642, 0xFE0F,
  0x1F64D, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F64D, 0x1F3FE, 0x200D,
  0x2642, 0xFE0F, 0x1F64D, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F64D,
  0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F64E, 0x1F3FB, 0x200D, 0x2640,
  0xFE0F, 0x1F64E, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F64E, 0x1F3FC,
  0x200D, 0x2640, 0xFE0F, 0x1F64E, 0x1F3FC, 0x200D, 0x2642, 0xFE0F,
  0x1F64E, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F64E, 0x1F3FD, 0x200D,
  0x2642, 0xFE0F, 0x1F64E, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F64E,
  0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F64E, 0x1F3FF, 0x200D, 0x2640,
  0xFE0F, 0x1F64E, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F6A3, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x1F6A3, 0x1F3FB, 0x200D, 0x2642, 0xFE0F,
  0x1F6A3, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F6A3, 0x1F3FC, 0x200D,
  0x2642, 0xFE0F, 0x1F6A3, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F6A3,
  0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F6A3, 0x1F3FE, 0x200D, 0x2640,
  0xFE0F, 0x1F6A3, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F6A3, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x1F6A3, 0x1F3FF, 0x200D, 0x2642, 0xFE0F,
  0x1F6B4, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F6B4, 0x1F3FB, 0x200D,
  0x2642, 0xFE0F, 0x1F6B4, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F6B4,
  0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F6B4, 0x1F3FD, 0x200D, 0x2640,
  0xFE0F, 0x1F6B4, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F6B4, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x1F6B4, 0x1F3FE, 0x200D, 0x2642, 0xFE0F,
  0x1F6B4, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F6B4, 0x1F3FF, 0x200D,
  0x2642, 0xFE0F, 0x1F6B5, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F6B5,
  0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F6B5, 0x1F3FC, 0x200D, 0x2640,
  0xFE0F, 0x1F6B5, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F6B5, 0x1F3FD,
  0x200D, 0x2640, 0xFE0F, 0x1F6B5, 0x1F3FD, 0x200D, 0x2642, 0xFE0F,
  0x1F6B5, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F6B5, 0x1F3FE, 0x200D,
  0x2642, 0xFE0F, 0x1F6B5, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F6B5,
  0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F6B6, 0x1F3FB, 0x200D, 0x2640,
  0xFE0F, 0x1F6B6, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F6B6, 0x1F3FB,
  0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FC, 0x200D, 0x2640, 0xFE0F,
  0x1F6B6, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F6B6, 0x1F3FC, 0x200D,
  0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F6B6,
  0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F6B6, 0x1F3FD, 0x200D, 0x27A1,
  0xFE0F, 0x1F6B6, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F6B6, 0x1F3FE,
  0x200D, 0x2642, 0xFE0F, 0x1F6B6, 0x1F3FE, 0x200D, 0x27A1, 0xFE0F,
  0x1F6B6, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F6B6, 0x1F3FF, 0x200D,
  0x2642, 0xFE0F, 0x1F6B6, 0x1F3FF, 0x200D, 0x27A1, 0xFE0F, 0x1F926,
  0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F926, 0x1F3FB, 0x200D, 0x2642,
  0xFE0F, 0x1F926, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F926, 0x1F3FC,
  0x200D, 0x2642, 0xFE0F, 0x1F926, 0x1F3FD, 0x200D, 0x2640, 0xFE0F,
  0x1F926, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F926, 0x1F3FE, 0x200D,
  0x2640, 0xFE0F, 0x1F926, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F926,
  0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F926, 0x1F3FF, 0x200D, 0x2642,
  0xFE0F, 0x1F935, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F935, 0x1F3FB,
  0x200D, 0x2642, 0xFE0F, 0x1F935, 0x1F3FC, 0x200D, 0x2640, 0xFE0F,
  0x1F935, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F935, 0x1F3FD, 0x200D,
  0x2640, 0xFE0F, 0x1F935, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F935,
  0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F935, 0x1F3FE, 0x200D, 0x2642,
  0xFE0F, 0x1F935, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F935, 0x1F3FF,
  0x200D, 0x2642, 0xFE0F, 0x1F937, 0x1F3FB, 0x200D, 0x2640, 0xFE0F,
  0x1F937, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F937, 0x1F3FC, 0x200D,
  0x2640, 0xFE0F, 0x1F937, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F937,
  0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F937, 0x1F3FD, 0x200D, 0x2642,
  0xFE0F, 0x1F937, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F937, 0x1F3FE,
  0x200D, 0x2642, 0xFE0F, 0x1F937, 0x1F3FF, 0x200D, 0x2640, 0xFE0F,
  0x1F937, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F938, 0x1F3FB, 0x200D,
  0x2640, 0xFE0F, 0x1F938, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F938,
  0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F938, 0x1F3FC, 0x200D, 0x2642,
  0xFE0F, 0x1F938, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F938, 0x1F3FD,
  0x200D, 0x2642, 0xFE0F, 0x1F938, 0x1F3FE, 0x200D, 0x2640, 0xFE0F,
  0x1F938, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F938, 0x1F3FF, 0x200D,
  0x2640, 0xFE0F, 0x1F938, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F939,
  0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F939, 0x1F3FB, 0x200D, 0x2642,
  0xFE0F, 0x1F939, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F939, 0x1F3FC,
  0x200D, 0x2642, 0xFE0F, 0x1F939, 0x1F3FD, 0x200D, 0x2640, 0xFE0F,
  0x1F939, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F939, 0x1F3FE, 0x200D,
  0x2640, 0xFE0F, 0x1F939, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F939,
  0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F939, 0x1F3FF, 0x200D, 0x2642,
  0xFE0F, 0x1F93C, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F93C, 0x1F3FB,
  0x200D, 0x2642, 0xFE0F, 0x1F93C, 0x1F3FC, 0x200D, 0x2640, 0xFE0F,
  0x1F93C, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F93C, 0x1F3FD, 0x200D,
  0x2640, 0xFE0F, 0x1F93C, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F93C,
  0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F93C, 0x1F3FE, 0x200D, 0x2642,
  0xFE0F, 0x1F93C, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F93C, 0x1F3FF,
  0x200D, 0x2642, 0xFE0F, 0x1F93D, 0x1F3FB, 0x200D, 0x2640, 0xFE0F,
  0x1F93D, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F93D, 0x1F3FC, 0x200D,
  0x2640, 0xFE0F, 0x1F93D, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F93D,
  0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F93D, 0x1F3FD, 0x200D, 0x2642,
  0xFE0F, 0x1F93D, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F93D, 0x1F3FE,
  0x200D, 0x2642, 0xFE0F, 0x1F93D, 0x1F3FF, 0x200D, 0x2640, 0xFE0F,
  0x1F93D, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F93E, 0x1F3FB, 0x200D,
  0x2640, 0xFE0F, 0x1F93E, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F93E,
  0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F93E, 0x1F3FC, 0x200D, 0x2642,
  0xFE0F, 0x1F93E, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F93E, 0x1F3FD,
  0x200D, 0x2642, 0xFE0F, 0x1F93E, 0x1F3FE, 0x200D, 0x2640, 0xFE0F,
  0x1F93E, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F93E, 0x1F3FF, 0x200D,
  0x2640, 0xFE0F, 0x1F93E, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F9B8,
  0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F9B8, 0x1F3FB, 0x200D, 0x2642,
  0xFE0F, 0x1F9B8, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F9B8, 0x1F3FC,
  0x200D, 0x2642, 0xFE0F, 0x1F9B8, 0x1F3FD, 0x200D, 0x2640, 0xFE0F,
  0x1F9B8, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F9B8, 0x1F3FE, 0x200D,
  0x2640, 0xFE0F, 0x1F9B8, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F9B8,
  0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F9B8, 0x1F3FF, 0x200D, 0x2642,
  0xFE0F, 0x1F9B9, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F9B9, 0x1F3FB,
  0x200D, 0x2642, 0xFE0F, 0x1F9B9, 0x1F3FC, 0x200D, 0x2640, 0xFE0F,
  0x1F9B9, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F9B9, 0x1F3FD, 0x200D,
  0x2640, 0xFE0F, 0x1F9B9, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F9B9,
  0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F9B9, 0x1F3FE, 0x200D, 0x2642,
  0xFE0F, 0x1F9B9, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F9B9, 0x1F3FF,
  0x200D, 0x2642, 0xFE0F, 0x1F9CD, 0x1F3FB, 0x200D, 0x2640, 0xFE0F,
  0x1F9CD, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F9CD, 0x1F3FC, 0x200D,
  0x2640, 0xFE0F, 0x1F9CD, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F9CD,
  0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F9CD, 0x1F3FD, 0x200D, 0x2642,
  0xFE0F, 0x1F9CD, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F9CD, 0x1F3FE,
  0x200D, 0x2642, 0xFE0F, 0x1F9CD, 0x1F3FF, 0x200D, 0x2640, 0xFE0F,
  0x1F9CD, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F9CE, 0x1F3FB, 0x200D,
  0x2640, 0xFE0F, 0x1F9CE, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F9CE,
  0x1F3FB, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FC, 0x200D, 0x2640,
  0xFE0F, 0x1F9CE, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F9CE, 0x1F3FC,
  0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FD, 0x200D, 0x2640, 0xFE0F,
  0x1F9CE, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F9CE, 0x1F3FD, 0x200D,
  0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F9CE,
  0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F9CE, 0x1F3FE, 0x200D, 0x27A1,
  0xFE0F, 0x1F9CE, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F9CE, 0x1F3FF,
  0x200D, 0x2642, 0xFE0F, 0x1F9CE, 0x1F3FF, 0x200D, 0x27A1, 0xFE0F,
  0x1F9CF, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F9CF, 0x1F3FB, 0x200D,
  0x2642, 0xFE0F, 0x1F9CF, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F9CF,
  0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F9CF, 0x1F3FD, 0x200D, 0x2640,
  0xFE0F, 0x1F9CF, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F9CF, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x1F9CF, 0x1F3FE, 0x200D, 0x2642, 0xFE0F,
  0x1F9CF, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F9CF, 0x1F3FF, 0x200D,
  0x2642, 0xFE0F, 0x1F9D1, 0x1F3FB, 0x200D, 0x2695, 0xFE0F, 0x1F9D1,
  0x1F3FB, 0x200D, 0x2696, 0xFE0F, 0x1F9D1, 0x1F3FB, 0x200D, 0x2708,
  0xFE0F, 0x1F9D1, 0x1F3FC, 0x200D, 0x2695, 0xFE0F, 0x1F9D1, 0x1F3FC,
  0x200D, 0x2696, 0xFE0F, 0x1F9D1, 0x1F3FC, 0x200D, 0x2708, 0xFE0F,
  0x1F9D1, 0x1F3FD, 0x200D, 0x2695, 0xFE0F, 0x1F9D1, 0x1F3FD, 0x200D,
  0x2696, 0xFE0F, 0x1F9D1, 0x1F3FD, 0x200D, 0x2708, 0xFE0F, 0x1F9D1,
  0x1F3FE, 0x200D, 0x2695, 0xFE0F, 0x1F9D1, 0x1F3FE, 0x200D, 0x2696,
  0xFE0F, 0x1F9D1, 0x1F3FE, 0x200D, 0x2708, 0xFE0F, 0x1F9D1, 0x1F3FF,
  0x200D, 0x2695, 0xFE0F, 0x1F9D1, 0x1F3FF, 0x200D, 0x2696, 0xFE0F,
  0x1F9D1, 0x1F3FF, 0x200D, 0x2708, 0xFE0F, 0x1F9D4, 0x1F3FB, 0x200D,
  0x2640, 0xFE0F, 0x1F9D4, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F9D4,
  0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F9D4, 0x1F3FC, 0x200D, 0x2642,
  0xFE0F, 0x1F9D4, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F9D4, 0x1F3FD,
  0x200D, 0x2642, 0xFE0F, 0x1F9D4, 0x1F3FE, 0x200D, 0x2640, 0xFE0F,
  0x1F9D4, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F9D4, 0x1F3FF, 0x200D,
  0x2640, 0xFE0F, 0x1F9D4, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F9D6,
  0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F9D6, 0x1F3FB, 0x200D, 0x2642,
  0xFE0F, 0x1F9D6, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F9D6, 0x1F3FC,
  0x200D, 0x2642, 0xFE0F, 0x1F9D6, 0x1F3FD, 0x200D, 0x2640, 0xFE0F,
  0x1F9D6, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F9D6, 0x1F3FE, 0x200D,
  0x2640, 0xFE0F, 0x1F9D6, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F9D6,
  0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F9D6, 0x1F3FF, 0x200D, 0x2642,
  0xFE0F, 0x1F9D7, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F9D7, 0x1F3FB,
  0x200D, 0x2642, 0xFE0F, 0x1F9D7, 0x1F3FC, 0x200D, 0x2640, 0xFE0F,
  0x1F9D7, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F9D7, 0x1F3FD, 0x200D,
  0x2640, 0xFE0F, 0x1F9D7, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F9D7,
  0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F9D7, 0x1F3FE, 0x200D, 0x2642,
  0xFE0F, 0x1F9D7, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F9D7, 0x1F3FF,
  0x200D, 0x2642, 0xFE0F, 0x1F9D8, 0x1F3FB, 0x200D, 0x2640, 0xFE0F,
  0x1F9D8, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F9D8, 0x1F3FC, 0x200D,
  0x2640, 0xFE0F, 0x1F9D8, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F9D8,
  0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F9D8, 0x1F3FD, 0x200D, 0x2642,
  0xFE0F, 0x1F9D8, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F9D8, 0x1F3FE,
  0x200D, 0x2642, 0xFE0F, 0x1F9D8, 0x1F3FF, 0x200D, 0x2640, 0xFE0F,
  0x1F9D8, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F9D9, 0x1F3FB, 0x200D,
  0x2640, 0xFE0F, 0x1F9D9, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F9D9,
  0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F9D9, 0x1F3FC, 0x200D, 0x2642,
  0xFE0F, 0x1F9D9, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F9D9, 0x1F3FD,
  0x200D, 0x2642, 0xFE0F, 0x1F9D9, 0x1F3FE, 0x200D, 0x2640, 0xFE0F,
  0x1F9D9, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F9D9, 0x1F3FF, 0x200D,
  0x2640, 0xFE0F, 0x1F9D9, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F9DA,
  0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F9DA, 0x1F3FB, 0x200D, 0x2642,
  0xFE0F, 0x1F9DA, 0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F9DA, 0x1F3FC,
  0x200D, 0x2642, 0xFE0F, 0x1F9DA, 0x1F3FD, 0x200D, 0x2640, 0xFE0F,
  0x1F9DA, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F9DA, 0x1F3FE, 0x200D,
  0x2640, 0xFE0F, 0x1F9DA, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F9DA,
  0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F9DA, 0x1F3FF, 0x200D, 0x2642,
  0xFE0F, 0x1F9DB, 0x1F3FB, 0x200D, 0x2640, 0xFE0F, 0x1F9DB, 0x1F3FB,
  0x200D, 0x2642, 0xFE0F, 0x1F9DB, 0x1F3FC, 0x200D, 0x2640, 0xFE0F,
  0x1F9DB, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F9DB, 0x1F3FD, 0x200D,
  0x2640, 0xFE0F, 0x1F9DB, 0x1F3FD, 0x200D, 0x2642, 0xFE0F, 0x1F9DB,
  0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F9DB, 0x1F3FE, 0x200D, 0x2642,
  0xFE0F, 0x1F9DB, 0x1F3FF, 0x200D, 0x2640, 0xFE0F, 0x1F9DB, 0x1F3FF,
  0x200D, 0x2642, 0xFE0F, 0x1F9DC, 0x1F3FB, 0x200D, 0x2640, 0xFE0F,
  0x1F9DC, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F9DC, 0x1F3FC, 0x200D,
  0x2640, 0xFE0F, 0x1F9DC, 0x1F3FC, 0x200D, 0x2642, 0xFE0F, 0x1F9DC,
  0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F9DC, 0x1F3FD, 0x200D, 0x2642,
  0xFE0F, 0x1F9DC, 0x1F3FE, 0x200D, 0x2640, 0xFE0F, 0x1F9DC, 0x1F3FE,
  0x200D, 0x2642, 0xFE0F, 0x1F9DC, 0x1F3FF, 0x200D, 0x2640, 0xFE0F,
  0x1F9DC, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1F9DD, 0x1F3FB, 0x200D,
  0x2640, 0xFE0F, 0x1F9DD, 0x1F3FB, 0x200D, 0x2642, 0xFE0F, 0x1F9DD,
  0x1F3FC, 0x200D, 0x2640, 0xFE0F, 0x1F9DD, 0x1F3FC, 0x200D, 0x2642,
  0xFE0F, 0x1F9DD, 0x1F3FD, 0x200D, 0x2640, 0xFE0F, 0x1F9DD, 0x1F3FD,
  0x200D, 0x2642, 0xFE0F, 0x1F9DD, 0x1F3FE, 0x200D, 0x2640, 0xFE0F,
  0x1F9DD, 0x1F3FE, 0x200D, 0x2642, 0xFE0F, 0x1F9DD, 0x1F3FF, 0x200D,
  0x2640, 0xFE0F, 0x1F9DD, 0x1F3FF, 0x200D, 0x2642, 0xFE0F, 0x1FAF1,
  0x1F3FB, 0x200D, 0x1FAF2, 0x1F3FC, 0x1FAF1, 0x1F3FB, 0x200D, 0x1FAF2,
  0x1F3FD, 0x1FAF1, 0x1F3FB, 0x200D, 0x1FAF2, 0x1F3FE, 0x1FAF1, 0x1F3FB,
  0x200D, 0x1FAF2, 0x1F3FF, 0x1FAF1, 0x1F3FC, 0x200D, 0x1FAF2, 0x1F3FB,
  0x1FAF1, 0x1F3FC, 0x200D, 0x1FAF2, 0x1F3FD, 0x1FAF1, 0x1F3FC, 0x200D,
  0x1FAF2, 0x1F3FE, 0x1FAF1, 0x1F3FC, 0x200D, 0x1FAF2, 0x1F3FF, 0x1FAF1,
  0x1F3FD, 0x200D, 0x1FAF2, 0x1F3FB, 0x1FAF1, 0x1F3FD, 0x200D, 0x1FAF2,
  0x1F3FC, 0x1FAF1, 0x1F3FD, 0x200D, 0x1FAF2, 0x1F3FE, 0x1FAF1, 0x1F3FD,
  0x200D, 0x1FAF2, 0x1F3FF, 0x1FAF1, 0x1F3FE, 0x200D, 0x1FAF2, 0x1F3FB,
  0x1FAF1, 0x1F3FE, 0x200D, 0x1FAF2, 0x1F3FC, 0x1FAF1, 0x1F3FE, 0x200D,
  0x1FAF2, 0x1F3FD, 0x1FAF1, 0x1F3FE, 0x200D, 0x1FAF2, 0x1F3FF, 0x1FAF1,
  0x1F3FF, 0x200D, 0x1FAF2, 0x1F3FB, 0x1FAF1, 0x1F3FF, 0x200D, 0x1FAF2,
  0x1F3FC, 0x1FAF1, 0x1F3FF, 0x200D, 0x1FAF2, 0x1F3FD, 0x1FAF1, 0x1F3FF,
  0x200D, 0x1FAF2, 0x1F3FE, 0x1F468, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F468, 0x1F469, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F469,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F468, 0x200D, 0x1F9AF,
  0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x200D, 0x1F9BC, 0x200D, 0x27A1,
  0xFE0F, 0x1F468, 0x200D, 0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F469,
  0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x200D, 0x1F9BC,
  0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x200D, 0x1F9BD, 0x200D, 0x27A1,
  0xFE0F, 0x1F9D1, 0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F9D1,
  0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F, 0x1F9D1, 0x200D, 0x1F9BD,
  0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x200D, 0x2640, 0xFE0F, 0x200D,
  0x27A1, 0xFE0F, 0x1F3C3, 0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1,
  0xFE0F, 0x1F468, 0x200D, 0x1F468, 0x200D, 0x1F466, 0x200D, 0x1F466,
  0x1F468, 0x200D, 0x1F468, 0x200D, 0x1F467, 0x200D, 0x1F466, 0x1F468,
  0x200D, 0x1F468, 0x200D, 0x1F467, 0x200D, 0x1F467, 0x1F468, 0x200D,
  0x1F469, 0x200D, 0x1F466, 0x200D, 0x1F466, 0x1F468, 0x200D, 0x1F469,
  0x200D, 0x1F467, 0x200D, 0x1F466, 0x1F468, 0x200D, 0x1F469, 0x200D,
  0x1F467, 0x200D, 0x1F467, 0x1F469, 0x200D, 0x1F469, 0x200D, 0x1F466,
  0x200D, 0x1F466, 0x1F469, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x200D,
  0x1F466, 0x1F469, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x200D, 0x1F467,
  0x1F6B6, 0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x200D,
  0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x200D, 0x2642,
  0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9D1, 0x200D, 0x1F9D1, 0x200D,
  0x1F9D2, 0x200D, 0x1F9D2, 0x1F468, 0x1F3FB, 0x200D, 0x1F430, 0x200D,
  0x1F468, 0x1F3FC, 0x1F468, 0x1F3FB, 0x200D, 0x1F430, 0x200D, 0x1F468,
  0x1F3FD, 0x1F468, 0x1F3FB, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FE,
  0x1F468, 0x1F3FB, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FF, 0x1F468,
  0x1F3FC, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FC,
  0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FC, 0x200D,
  0x1F430, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FC, 0x200D, 0x1F430,
  0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FB, 0x200D, 0x1F91D, 0x200D,
  0x1F468, 0x1F3FC, 0x1F468, 0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F468,
  0x1F3FD, 0x1F468, 0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FE,
  0x1F468, 0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FF, 0x1F468,
  0x1F3FB, 0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FB,
  0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FB, 0x200D,
  0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FB, 0x200D, 0x1FAEF,
  0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FB, 0x200D, 0x1FAEF, 0x200D,
  0x1F468, 0x1F3FD, 0x1F468, 0x1F3FB, 0x200D, 0x1FAEF, 0x200D, 0x1F468,
  0x1F3FE, 0x1F468, 0x1F3FB, 0x200D, 0x1FAEF, 0x200D, 0x1F468, 0x1F3FF,
  0x1F468, 0x1F3FD, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FB, 0x1F468,
  0x1F3FD, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FD,
  0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FD, 0x200D,
  0x1F430, 0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FC, 0x200D, 0x1F91D,
  0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FC, 0x200D, 0x1F91D, 0x200D,
  0x1F468, 0x1F3FD, 0x1F468, 0x1F3FC, 0x200D, 0x1F91D, 0x200D, 0x1F468,
  0x1F3FE, 0x1F468, 0x1F3FC, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FF,
  0x1F468, 0x1F3FC, 0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F468,
  0x1F3FC, 0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FC,
  0x200D, 0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FC, 0x200D,
  0x1FAEF, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FC, 0x200D, 0x1FAEF,
  0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FC, 0x200D, 0x1FAEF, 0x200D,
  0x1F468, 0x1F3FE, 0x1F468, 0x1F3FC, 0x200D, 0x1FAEF, 0x200D, 0x1F468,
  0x1F3FF, 0x1F468, 0x1F3FE, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FB,
  0x1F468, 0x1F3FE, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FC, 0x1F468,
  0x1F3FE, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FE,
  0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FD, 0x200D,
  0x1F91D, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FD, 0x200D, 0x1F91D,
  0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FD, 0x200D, 0x1F91D, 0x200D,
  0x1F468, 0x1F3FE, 0x1F468, 0x1F3FD, 0x200D, 0x1F91D, 0x200D, 0x1F468,
  0x1F3FF, 0x1F468, 0x1F3FD, 0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F,
  0x1F468, 0x1F3FD, 0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F, 0x1F468,
  0x1F3FD, 0x200D, 0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FD,
  0x200D, 0x1FAEF, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FD, 0x200D,
  0x1FAEF, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FD, 0x200D, 0x1FAEF,
  0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FD, 0x200D, 0x1FAEF, 0x200D,
  0x1F468, 0x1F3FF, 0x1F468, 0x1F3FF, 0x200D, 0x1F430, 0x200D, 0x1F468,
  0x1F3FB, 0x1F468, 0x1F3FF, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FC,
  0x1F468, 0x1F3FF, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FD, 0x1F468,
  0x1F3FF, 0x200D, 0x1F430, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FE,
  0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FE, 0x200D,
  0x1F91D, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FE, 0x200D, 0x1F91D,
  0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FE, 0x200D, 0x1F91D, 0x200D,
  0x1F468, 0x1F3FF, 0x1F468, 0x1F3FE, 0x200D, 0x1F9AF, 0x200D, 0x27A1,
  0xFE0F, 0x1F468, 0x1F3FE, 0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F,
  0x1F468, 0x1F3FE, 0x200D, 0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F468,
  0x1F3FE, 0x200D, 0x1FAEF, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FE,
  0x200D, 0x1FAEF, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FE, 0x200D,
  0x1FAEF, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FE, 0x200D, 0x1FAEF,
  0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FF, 0x200D, 0x1F91D, 0x200D,
  0x1F468, 0x1F3FB, 0x1F468, 0x1F3FF, 0x200D, 0x1F91D, 0x200D, 0x1F468,
  0x1F3FC, 0x1F468, 0x1F3FF, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FD,
  0x1F468, 0x1F3FF, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FE, 0x1F468,
  0x1F3FF, 0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FF,
  0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FF, 0x200D,
  0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FF, 0x200D, 0x1FAEF,
  0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FF, 0x200D, 0x1FAEF, 0x200D,
  0x1F468, 0x1F3FC, 0x1F468, 0x1F3FF, 0x200D, 0x1FAEF, 0x200D, 0x1F468,
  0x1F3FD, 0x1F468, 0x1F3FF, 0x200D, 0x1FAEF, 0x200D, 0x1F468, 0x1F3FE,
  0x1F469, 0x1F3FB, 0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FC, 0x1F469,
  0x1F3FB, 0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FB,
  0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FB, 0x200D,
  0x1F430, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FC, 0x200D, 0x1F430,
  0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FC, 0x200D, 0x1F430, 0x200D,
  0x1F469, 0x1F3FD, 0x1F469, 0x1F3FC, 0x200D, 0x1F430, 0x200D, 0x1F469,
  0x1F3FE, 0x1F469, 0x1F3FC, 0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FF,
  0x1F469, 0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FC, 0x1F469,
  0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FB,
  0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FE, 0x1F469, 0x1F3FB, 0x200D,
  0x1F91D, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FB, 0x200D, 0x1F91D,
  0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FB, 0x200D, 0x1F91D, 0x200D,
  0x1F469, 0x1F3FD, 0x1F469, 0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F469,
  0x1F3FE, 0x1F469, 0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F469, 0x1F3FF,
  0x1F469, 0x1F3FB, 0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F469,
  0x1F3FB, 0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x1F3FB,
  0x200D, 0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x1F3FB, 0x200D,
  0x1FAEF, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FB, 0x200D, 0x1FAEF,
  0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FB, 0x200D, 0x1FAEF, 0x200D,
  0x1F469, 0x1F3FE, 0x1F469, 0x1F3FB, 0x200D, 0x1FAEF, 0x200D, 0x1F469,
  0x1F3FF, 0x1F469, 0x1F3FD, 0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FB,
  0x1F469, 0x1F3FD, 0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FC, 0x1F469,
  0x1F3FD, 0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FD,
  0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FC, 0x200D,
  0x1F91D, 0x200D, 0x1F468, 0x1F3FB, 0x1F469, 0x1F3FC, 0x200D, 0x1F91D,
  0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FC, 0x200D, 0x1F91D, 0x200D,
  0x1F468, 0x1F3FE, 0x1F469, 0x1F3FC, 0x200D, 0x1F91D, 0x200D, 0x1F468,
  0x1F3FF, 0x1F469, 0x1F3FC, 0x200D, 0x1F91D, 0x200D, 0x1F469, 0x1F3FB,
  0x1F469, 0x1F3FC, 0x200D, 0x1F91D, 0x200D, 0x1F469, 0x1F3FD, 0x1F469,
  0x1F3FC, 0x200D, 0x1F91D, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FC,
  0x200D, 0x1F91D, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FC, 0x200D,
  0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x1F3FC, 0x200D, 0x1F9BC,
  0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x1F3FC, 0x200D, 0x1F9BD, 0x200D,
  0x27A1, 0xFE0F, 0x1F469, 0x1F3FC, 0x200D, 0x1FAEF, 0x200D, 0x1F469,
  0x1F3FB, 0x1F469, 0x1F3FC, 0x200D, 0x1FAEF, 0x200D, 0x1F469, 0x1F3FD,
  0x1F469, 0x1F3FC, 0x200D, 0x1FAEF, 0x200D, 0x1F469, 0x1F3FE, 0x1F469,
  0x1F3FC, 0x200D, 0x1FAEF, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FE,
  0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FE, 0x200D,
  0x1F430, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FE, 0x200D, 0x1F430,
  0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FE, 0x200D, 0x1F430, 0x200D,
  0x1F469, 0x1F3FF, 0x1F469, 0x1F3FD, 0x200D, 0x1F91D, 0x200D, 0x1F468,
  0x1F3FB, 0x1F469, 0x1F3FD, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FC,
  0x1F469, 0x1F3FD, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FE, 0x1F469,
  0x1F3FD, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FD,
  0x200D, 0x1F91D, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FD, 0x200D,
  0x1F91D, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FD, 0x200D, 0x1F91D,
  0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FD, 0x200D, 0x1F91D, 0x200D,
  0x1F469, 0x1F3FF, 0x1F469, 0x1F3FD, 0x200D, 0x1F9AF, 0x200D, 0x27A1,
  0xFE0F, 0x1F469, 0x1F3FD, 0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F,
  0x1F469, 0x1F3FD, 0x200D, 0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F469,
  0x1F3FD, 0x200D, 0x1FAEF, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FD,
  0x200D, 0x1FAEF, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FD, 0x200D,
  0x1FAEF, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FD, 0x200D, 0x1FAEF,
  0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FF, 0x200D, 0x1F430, 0x200D,
  0x1F469, 0x1F3FB, 0x1F469, 0x1F3FF, 0x200D, 0x1F430, 0x200D, 0x1F469,
  0x1F3FC, 0x1F469, 0x1F3FF, 0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FD,
  0x1F469, 0x1F3FF, 0x200D, 0x1F430, 0x200D, 0x1F469, 0x1F3FE, 0x1F469,
  0x1F3FE, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FB, 0x1F469, 0x1F3FE,
  0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FE, 0x200D,
  0x1F91D, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FE, 0x200D, 0x1F91D,
  0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FE, 0x200D, 0x1F91D, 0x200D,
  0x1F469, 0x1F3FB, 0x1F469, 0x1F3FE, 0x200D, 0x1F91D, 0x200D, 0x1F469,
  0x1F3FC, 0x1F469, 0x1F3FE, 0x200D, 0x1F91D, 0x200D, 0x1F469, 0x1F3FD,
  0x1F469, 0x1F3FE, 0x200D, 0x1F91D, 0x200D, 0x1F469, 0x1F3FF, 0x1F469,
  0x1F3FE, 0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x1F3FE,
  0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x1F3FE, 0x200D,
  0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x1F3FE, 0x200D, 0x1FAEF,
  0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FE, 0x200D, 0x1FAEF, 0x200D,
  0x1F469, 0x1F3FC, 0x1F469, 0x1F3FE, 0x200D, 0x1FAEF, 0x200D, 0x1F469,
  0x1F3FD, 0x1F469, 0x1F3FE, 0x200D, 0x1FAEF, 0x200D, 0x1F469, 0x1F3FF,
  0x1F469, 0x1F3FF, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FB, 0x1F469,
  0x1F3FF, 0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FF,
  0x200D, 0x1F91D, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FF, 0x200D,
  0x1F91D, 0x200D, 0x1F468, 0x1F3FE, 0x1F469, 0x1F3FF, 0x200D, 0x1F91D,
  0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FF, 0x200D, 0x1F91D, 0x200D,
  0x1F469, 0x1F3FC, 0x1F469, 0x1F3FF, 0x200D, 0x1F91D, 0x200D, 0x1F469,
  0x1F3FD, 0x1F469, 0x1F3FF, 0x200D, 0x1F91D, 0x200D, 0x1F469, 0x1F3FE,
  0x1F469, 0x1F3FF, 0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F469,
  0x1F3FF, 0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x1F3FF,
  0x200D, 0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F469, 0x1F3FF, 0x200D,
  0x1FAEF, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FF, 0x200D, 0x1FAEF,
  0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FF, 0x200D, 0x1FAEF, 0x200D,
  0x1F469, 0x1F3FD, 0x1F469, 0x1F3FF, 0x200D, 0x1FAEF, 0x200D, 0x1F469,
  0x1F3FE, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F430, 0x200D, 0x1F9D1, 0x1F3FC,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F430, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1,
  0x1F3FB, 0x200D, 0x1F430, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FB,
  0x200D, 0x1F430, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FC, 0x200D,
  0x1F430, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F430,
  0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F430, 0x200D,
  0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F430, 0x200D, 0x1F9D1,
  0x1F3FF, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FB,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1,
  0x1F3FB, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FB,
  0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FB, 0x200D,
  0x1F91D, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F9AF,
  0x200D, 0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F9BC, 0x200D,
  0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FB, 0x200D, 0x1F9BD, 0x200D, 0x27A1,
  0xFE0F, 0x1F9D1, 0x1F3FB, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FC,
  0x1F9D1, 0x1F3FB, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1,
  0x1F3FB, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FB,
  0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FD, 0x200D,
  0x1F430, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F430,
  0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F430, 0x200D,
  0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F430, 0x200D, 0x1F9D1,
  0x1F3FF, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FB,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1,
  0x1F3FC, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FC,
  0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FC, 0x200D,
  0x1F91D, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F9AF,
  0x200D, 0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F9BC, 0x200D,
  0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FC, 0x200D, 0x1F9BD, 0x200D, 0x27A1,
  0xFE0F, 0x1F9D1, 0x1F3FC, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FB,
  0x1F9D1, 0x1F3FC, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1,
  0x1F3FC, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FC,
  0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FE, 0x200D,
  0x1F430, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F430,
  0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F430, 0x200D,
  0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F430, 0x200D, 0x1F9D1,
  0x1F3FF, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FB,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1,
  0x1F3FD, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FD,
  0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FD, 0x200D,
  0x1F91D, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F9AF,
  0x200D, 0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F9BC, 0x200D,
  0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FD, 0x200D, 0x1F9BD, 0x200D, 0x27A1,
  0xFE0F, 0x1F9D1, 0x1F3FD, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FB,
  0x1F9D1, 0x1F3FD, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1,
  0x1F3FD, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FD,
  0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FF, 0x200D,
  0x1F430, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F430,
  0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F430, 0x200D,
  0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F430, 0x200D, 0x1F9D1,
  0x1F3FE, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FB,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1,
  0x1F3FE, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FE,
  0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FE, 0x200D,
  0x1F91D, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F9AF,
  0x200D, 0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F9BC, 0x200D,
  0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FE, 0x200D, 0x1F9BD, 0x200D, 0x27A1,
  0xFE0F, 0x1F9D1, 0x1F3FE, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FB,
  0x1F9D1, 0x1F3FE, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1,
  0x1F3FE, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FE,
  0x200D, 0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FF, 0x200D,
  0x1F91D, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F91D,
  0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F91D, 0x200D,
  0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F91D, 0x200D, 0x1F9D1,
  0x1F3FE, 0x1F9D1, 0x1F3FF, 0x200D, 0x1F91D, 0x200D, 0x1F9D1, 0x1F3FF,
  0x1F9D1, 0x1F3FF, 0x200D, 0x1F9AF, 0x200D, 0x27A1, 0xFE0F, 0x1F9D1,
  0x1F3FF, 0x200D, 0x1F9BC, 0x200D, 0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FF,
  0x200D, 0x1F9BD, 0x200D, 0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FF, 0x200D,
  0x1FAEF, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FF, 0x200D, 0x1FAEF,
  0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FF, 0x200D, 0x1FAEF, 0x200D,
  0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FF, 0x200D, 0x1FAEF, 0x200D, 0x1F9D1,
  0x1F3FE, 0x1F3F4, 0xE0067, 0xE0062, 0xE0065, 0xE006E, 0xE0067, 0xE007F,
  0x1F3F4, 0xE0067, 0xE0062, 0xE0073, 0xE0063, 0xE0074, 0xE007F, 0x1F3F4,
  0xE0067, 0xE0062, 0xE0077, 0xE006C, 0xE0073, 0xE007F, 0x1F468, 0x200D,
  0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F469, 0x200D,
  0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F469, 0x200D,
  0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3C3, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FB,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FC,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FC,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FD,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FD,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FE,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F3C3, 0x1F3FF,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F468, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FB, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FE, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F469, 0x1F3FF, 0x1F6B6, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FB,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FC,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FC,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FD,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FD,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FE,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F6B6, 0x1F3FF,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FB,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FB,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FC,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FC,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FD,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FD,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FE,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FE,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FF,
  0x200D, 0x2640, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9CE, 0x1F3FF,
  0x200D, 0x2642, 0xFE0F, 0x200D, 0x27A1, 0xFE0F, 0x1F9D1, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F9D1, 0x1F3FE, 0x1F468, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FB,
  0x1F468, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FC, 0x1F468, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FB, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FF,
  0x1F468, 0x1F3FC, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FB, 0x1F468, 0x1F3FC, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FC, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FE,
  0x1F468, 0x1F3FC, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FF, 0x1F468, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FD, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FC, 0x1F468, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FD,
  0x1F468, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FE, 0x1F468, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FE, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FB, 0x1F468, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FC,
  0x1F468, 0x1F3FE, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FD, 0x1F468, 0x1F3FE, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FE, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FF, 0x1F468, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FB,
  0x1F468, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FC, 0x1F468, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FD, 0x1F468, 0x1F3FF, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FE, 0x1F468, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FF,
  0x1F469, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FB, 0x1F469, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FB, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FE,
  0x1F469, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FF, 0x1F469, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FB, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FD,
  0x1F469, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F469, 0x1F3FE, 0x1F469, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FC, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FB, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FC,
  0x1F469, 0x1F3FC, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FD, 0x1F469, 0x1F3FC, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FE, 0x1F469, 0x1F3FC, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FB,
  0x1F469, 0x1F3FC, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F469, 0x1F3FC, 0x1F469, 0x1F3FC, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FC, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FF,
  0x1F469, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FB, 0x1F469, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FD, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FE,
  0x1F469, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FF, 0x1F469, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FD, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FD,
  0x1F469, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F469, 0x1F3FE, 0x1F469, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F469, 0x1F3FF, 0x1F469, 0x1F3FE, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FB, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FC,
  0x1F469, 0x1F3FE, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FD, 0x1F469, 0x1F3FE, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FE, 0x1F469, 0x1F3FE, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FF, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FB,
  0x1F469, 0x1F3FE, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F469, 0x1F3FC, 0x1F469, 0x1F3FE, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F469, 0x1F3FD, 0x1F469, 0x1F3FE, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FE, 0x1F469, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FF,
  0x1F469, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FB, 0x1F469, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F468, 0x1F3FC, 0x1F469, 0x1F3FF, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FD, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F468, 0x1F3FE,
  0x1F469, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F468, 0x1F3FF, 0x1F469, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F469, 0x1F3FB, 0x1F469, 0x1F3FF, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FC, 0x1F469, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F469, 0x1F3FD,
  0x1F469, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F469, 0x1F3FE, 0x1F469, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F469, 0x1F3FF, 0x1F9D1, 0x1F3FB, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FC, 0x1F9D1, 0x1F3FB,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FD,
  0x1F9D1, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FB, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FC, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FC,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FD,
  0x1F9D1, 0x1F3FC, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FC, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FD, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FD,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FC,
  0x1F9D1, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F9D1, 0x1F3FE, 0x1F9D1, 0x1F3FD, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FE, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FE,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FC,
  0x1F9D1, 0x1F3FE, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FE, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F9D1, 0x1F3FF, 0x1F9D1, 0x1F3FF, 0x200D, 0x2764,
  0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FB, 0x1F9D1, 0x1F3FF,
  0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D, 0x1F9D1, 0x1F3FC,
  0x1F9D1, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D, 0x1F48B, 0x200D,
  0x1F9D1, 0x1F3FD, 0x1F9D1, 0x1F3FF, 0x200D, 0x2764, 0xFE0F, 0x200D,
  0x1F48B, 0x200D, 0x1F9D1, 0x1F3FE,
};

read_only global UIShell_TerminalEmojiSequence uishell_terminal_emoji_sequences[] =
{
  {0x00000000005A7E1D, 0, 2},
  {0x00000000005A7EC2, 2, 2},
  {0x00000000005E9010, 4, 2},
  {0x00000000005E91BD, 6, 2},
  {0x00000000005EADB6, 8, 2},
  {0x00000000005EB0AD, 10, 2},
  {0x00000000005EBC68, 12, 2},
  {0x00000000005EBC89, 14, 2},
  {0x00000000005EBCAA, 16, 2},
  {0x00000000005EBCCB, 18, 2},
  {0x00000000005EBCEC, 20, 2},
  {0x00000000005EBD0D, 22, 2},
  {0x00000000005EBF1D, 24, 2},
  {0x00000000005EBF3E, 26, 2},
  {0x00000000005EF07C, 28, 2},
  {0x00000000005F0603, 30, 2},
  {0x00000000005F09E1, 32, 2},
  {0x00000000005F0A02, 34, 2},
  {0x00000000005F0A23, 36, 2},
  {0x00000000005F0A65, 38, 2},
  {0x00000000005F0A86, 40, 2},
  {0x00000000005F0B4C, 42, 2},
  {0x00000000005F0B6D, 44, 2},
  {0x00000000005F0B8E, 46, 2},
  {0x00000000005F2556, 48, 2},
  {0x00000000005F433E, 50, 2},
  {0x00000000005F435F, 52, 2},
  {0x00000000005F44CA, 54, 2},
  {0x00000000005F4614, 56, 2},
  {0x00000000005F4DAF, 58, 2},
  {0x00000000005F4DD0, 60, 2},
  {0x00000000005F4E54, 62, 2},
  {0x00000000005F4E75, 64, 2},
  {0x00000000005F4E96, 66, 2},
  {0x00000000005F4EB7, 68, 2},
  {0x00000000005F4ED8, 70, 2},
  {0x00000000005F5022, 72, 2},
  {0x00000000005F5085, 74, 2},
  {0x00000000005F516C, 76, 2},
  {0x00000000005F5211, 78, 2},
  {0x00000000005F5274, 80, 2},
  {0x00000000005F52B6, 82, 2},
  {0x00000000005F52D7, 84, 2},
  {0x00000000005F533A, 86, 2},
  {0x00000000005F53BE, 88, 2},
  {0x00000000005F5442, 90, 2},
  {0x00000000005F5463, 92, 2},
  {0x00000000005F558C, 94, 2},
  {0x00000000005F55AD, 96, 2},
  {0x00000000005F55CE, 98, 2},
  {0x00000000005F5694, 100, 2},
  {0x00000000005F56D6, 102, 2},
  {0x00000000005F5A93, 104, 2},
  {0x00000000005F5AB4, 106, 2},
  {0x00000000005F5B17, 108, 2},
  {0x00000000005F5B59, 110, 2},
  {0x00000000005F5B7A, 112, 2},
  {0x00000000005F5BBC, 114, 2},
  {0x00000000005F5E2F, 116, 2},
  {0x00000000005F5E92, 118, 2},
  {0x00000000005F6126, 120, 2},
  {0x00000000005F6168, 122, 2},
  {0x00000000005F6189, 124, 2},
  {0x00000000005F61AA, 126, 2},
  {0x00000000005F61CB, 128, 2},
  {0x00000000005F620D, 130, 2},
  {0x00000000005F624F, 132, 2},
  {0x00000000005F6270, 134, 2},
  {0x00000000005F62F4, 136, 2},
  {0x00000000005F63DB, 138, 2},
  {0x00000000005F6504, 140, 2},
  {0x00000000005F6525, 142, 2},
  {0x00000000005F681C, 144, 2},
  {0x00000000005F6903, 146, 2},
  {0x00000000005F6945, 148, 2},
  {0x00000000005F6987, 150, 2},
  {0x00000000005F6C5D, 152, 2},
  {0x00000000005F6D44, 154, 2},
  {0x00000000005F6D65, 156, 2},
  {0x00000000005F6DC8, 158, 2},
  {0x00000000005F6E2B, 160, 2},
  {0x00000000005F6E4C, 162, 2},
  {0x00000000005F6E6D, 164, 2},
  {0x00000000005F6F96, 166, 2},
  {0x00000000005F705C, 168, 2},
  {0x00000000005F707D, 170, 2},
  {0x00000000005F70E0, 172, 2},
  {0x00000000005F7101, 174, 2},
  {0x00000000005F7143, 176, 2},
  {0x00000000005F71A6, 178, 2},
  {0x00000000005F71E8, 180, 2},
  {0x00000000005F722A, 182, 2},
  {0x00000000005F7311, 184, 2},
  {0x00000000005F7395, 186, 2},
  {0x00000000005F75E7, 188, 2},
  {0x00000000005F7608, 190, 2},
  {0x00000000005F7818, 192, 2},
  {0x00000000005F787B, 194, 2},
  {0x00000000005F7C17, 196, 2},
  {0x00000000005F7C38, 198, 2},
  {0x00000000005F8415, 200, 2},
  {0x00000000005FB808, 202, 2},
  {0x00000000005FB829, 204, 2},
  {0x00000000005FF3F9, 206, 2},
  {0x00000000005FF41A, 208, 2},
  {0x00000000005FF43B, 210, 2},
  {0x00000000006047FD, 212, 2},
  {0x00000000006047FE, 214, 2},
  {0x00000000006047FF, 216, 2},
  {0x0000000000604800, 218, 2},
  {0x0000000000604801, 220, 2},
  {0x0000000000606459, 222, 2},
  {0x000000000060645A, 224, 2},
  {0x000000000060645B, 226, 2},
  {0x000000000060645C, 228, 2},
  {0x000000000060645D, 230, 2},
  {0x000000000060668A, 232, 2},
  {0x000000000060668B, 234, 2},
  {0x000000000060668C, 236, 2},
  {0x000000000060668D, 238, 2},
  {0x000000000060668E, 240, 2},
  {0x00000000006066AB, 242, 2},
  {0x00000000006066AC, 244, 2},
  {0x00000000006066AD, 246, 2},
  {0x00000000006066AE, 248, 2},
  {0x00000000006066AF, 250, 2},
  {0x00000000006066CC, 252, 2},
  {0x00000000006066CD, 254, 2},
  {0x00000000006066CE, 256, 2},
  {0x00000000006066CF, 258, 2},
  {0x00000000006066D0, 260, 2},
  {0x00000000006066ED, 262, 2},
  {0x00000000006066EE, 264, 2},
  {0x00000000006066EF, 266, 2},
  {0x00000000006066F0, 268, 2},
  {0x00000000006066F1, 270, 2},
  {0x0000000000609E84, 272, 2},
  {0x000000000060A031, 274, 2},
  {0x000000000060EDCB, 276, 2},
  {0x000000000060EE0D, 278, 2},
  {0x00000000009A87C4, 280, 2},
  {0x00000000009A87E5, 282, 2},
  {0x00000000009A8992, 284, 2},
  {0x00000000009A89B3, 286, 2},
  {0x00000000009A9A96, 288, 2},
  {0x00000000009AA16B, 290, 2},
  {0x00000000009ABF95, 292, 2},
  {0x00000000009ABFF8, 294, 2},
  {0x00000000009AC019, 296, 2},
  {0x00000000009AC03A, 298, 2},
  {0x00000000009AC05B, 300, 2},
  {0x00000000009AC07C, 302, 2},
  {0x00000000009AC09D, 304, 2},
  {0x00000000009AC0BE, 306, 2},
  {0x00000000009AC0DF, 308, 2},
  {0x00000000009AC100, 310, 2},
  {0x00000000009AC24A, 312, 2},
  {0x00000000009ACB71, 314, 2},
  {0x00000000009ACEAA, 316, 2},
  {0x00000000009ACECB, 318, 2},
  {0x00000000009ACF0D, 320, 2},
  {0x00000000009ACF2E, 322, 2},
  {0x00000000009ACF4F, 324, 2},
  {0x00000000009ACFB2, 326, 2},
  {0x00000000009ACFD3, 328, 2},
  {0x00000000009AD57F, 330, 2},
  {0x00000000009AD5A0, 332, 2},
  {0x00000000009AD5C1, 334, 2},
  {0x00000000009AD5E2, 336, 2},
  {0x00000000009AD6A8, 338, 2},
  {0x00000000009AD6C9, 340, 2},
  {0x00000000009AD6EA, 342, 2},
  {0x00000000009AD70B, 344, 2},
  {0x00000000009AD72C, 346, 2},
  {0x00000000009AD74D, 348, 2},
  {0x00000000009AD76E, 350, 2},
  {0x00000000009AD78F, 352, 2},
  {0x00000000009AD7B0, 354, 2},
  {0x00000000009AD7D1, 356, 2},
  {0x00000000009AD7F2, 358, 2},
  {0x00000000009AD813, 360, 2},
  {0x00000000009ADAA7, 362, 2},
  {0x00000000009ADAE9, 364, 2},
  {0x00000000009ADB2B, 366, 2},
  {0x00000000009AE473, 368, 2},
  {0x00000000009AE4B5, 370, 2},
  {0x00000000009AFCF1, 372, 2},
  {0x00000000009B06BD, 374, 2},
  {0x00000000009B06DE, 376, 2},
  {0x00000000009B0BA3, 378, 2},
  {0x00000000009B0BC4, 380, 2},
  {0x00000000009B0C27, 382, 2},
  {0x00000000009B0C48, 384, 2},
  {0x00000000009B0C69, 386, 2},
  {0x00000000009B0C8A, 388, 2},
  {0x00000000009B0CAB, 390, 2},
  {0x00000000009B0CCC, 392, 2},
  {0x00000000009B0CED, 394, 2},
  {0x00000000009B0EBB, 396, 2},
  {0x00000000009B0F1E, 398, 2},
  {0x00000000009B0F3F, 400, 2},
  {0x00000000009B0F60, 402, 2},
  {0x00000000009B0F81, 404, 2},
  {0x00000000009B0FE4, 406, 2},
  {0x00000000009B1299, 408, 2},
  {0x00000000009B12FC, 410, 2},
  {0x00000000009B1425, 412, 2},
  {0x00000000009B1446, 414, 2},
  {0x00000000009B1590, 416, 2},
  {0x00000000009B1656, 418, 2},
  {0x00000000009B1677, 420, 2},
  {0x00000000009B1698, 422, 2},
  {0x00000000009B1845, 424, 2},
  {0x00000000009B1866, 426, 2},
  {0x00000000009B1887, 428, 2},
  {0x00000000009B19B0, 430, 2},
  {0x00000000009B19D1, 432, 2},
  {0x00000000009B19F2, 434, 2},
  {0x00000000009B1A55, 436, 2},
  {0x00000000009B1A97, 438, 2},
  {0x00000000009B1B3C, 440, 2},
  {0x00000000009B1C23, 442, 2},
  {0x00000000009B1CA7, 444, 2},
  {0x00000000009B1D8E, 446, 2},
  {0x00000000009B387F, 448, 2},
  {0x00000000009B38C1, 450, 2},
  {0x00000000009B38E2, 452, 2},
  {0x00000000009B3903, 454, 2},
  {0x00000000009B3B34, 456, 2},
  {0x00000000009B3B55, 458, 2},
  {0x00000000009B3B76, 460, 2},
  {0x00000000009B3B97, 462, 2},
  {0x00000000009B3BB8, 464, 2},
  {0x00000000009B3BD9, 466, 2},
  {0x00000000009B3C5D, 468, 2},
  {0x00000000009B3D44, 470, 2},
  {0x00000000009B3DA7, 472, 2},
  {0x00000000009B8AD3, 474, 2},
  {0x00000000009B8AD4, 476, 2},
  {0x00000000009B8AD5, 478, 2},
  {0x00000000009B8AD6, 480, 2},
  {0x00000000009B8AD7, 482, 2},
  {0x00000000009B8AD9, 484, 2},
  {0x00000000009B8ADC, 486, 2},
  {0x00000000009B8ADD, 488, 2},
  {0x00000000009B8ADF, 490, 2},
  {0x00000000009B8AE1, 492, 2},
  {0x00000000009B8AE2, 494, 2},
  {0x00000000009B8AE3, 496, 2},
  {0x00000000009B8AE4, 498, 2},
  {0x00000000009B8AE5, 500, 2},
  {0x00000000009B8AE7, 502, 2},
  {0x00000000009B8AE8, 504, 2},
  {0x00000000009B8AEA, 506, 2},
  {0x00000000009B8AF2, 508, 2},
  {0x00000000009B8AF3, 510, 2},
  {0x00000000009B8AF5, 512, 2},
  {0x00000000009B8AF6, 514, 2},
  {0x00000000009B8AF7, 516, 2},
  {0x00000000009B8AF8, 518, 2},
  {0x00000000009B8AF9, 520, 2},
  {0x00000000009B8AFA, 522, 2},
  {0x00000000009B8AFB, 524, 2},
  {0x00000000009B8AFD, 526, 2},
  {0x00000000009B8AFE, 528, 2},
  {0x00000000009B8AFF, 530, 2},
  {0x00000000009B8B00, 532, 2},
  {0x00000000009B8B02, 534, 2},
  {0x00000000009B8B03, 536, 2},
  {0x00000000009B8B04, 538, 2},
  {0x00000000009B8B05, 540, 2},
  {0x00000000009B8B07, 542, 2},
  {0x00000000009B8B08, 544, 2},
  {0x00000000009B8B0A, 546, 2},
  {0x00000000009B8B0B, 548, 2},
  {0x00000000009B8B13, 550, 2},
  {0x00000000009B8B15, 552, 2},
  {0x00000000009B8B16, 554, 2},
  {0x00000000009B8B18, 556, 2},
  {0x00000000009B8B19, 558, 2},
  {0x00000000009B8B1A, 560, 2},
  {0x00000000009B8B1B, 562, 2},
  {0x00000000009B8B1D, 564, 2},
  {0x00000000009B8B1E, 566, 2},
  {0x00000000009B8B1F, 568, 2},
  {0x00000000009B8B20, 570, 2},
  {0x00000000009B8B21, 572, 2},
  {0x00000000009B8B22, 574, 2},
  {0x00000000009B8B23, 576, 2},
  {0x00000000009B8B24, 578, 2},
  {0x00000000009B8B27, 580, 2},
  {0x00000000009B8B28, 582, 2},
  {0x00000000009B8B29, 584, 2},
  {0x00000000009B8B2A, 586, 2},
  {0x00000000009B8B2B, 588, 2},
  {0x00000000009B8B2C, 590, 2},
  {0x00000000009B8B38, 592, 2},
  {0x00000000009B8B3A, 594, 2},
  {0x00000000009B8B3D, 596, 2},
  {0x00000000009B8B3E, 598, 2},
  {0x00000000009B8B40, 600, 2},
  {0x00000000009B8B42, 602, 2},
  {0x00000000009B8B4D, 604, 2},
  {0x00000000009B8B55, 606, 2},
  {0x00000000009B8B57, 608, 2},
  {0x00000000009B8B59, 610, 2},
  {0x00000000009B8B5B, 612, 2},
  {0x00000000009B8B5C, 614, 2},
  {0x00000000009B8B66, 616, 2},
  {0x00000000009B8B67, 618, 2},
  {0x00000000009B8B68, 620, 2},
  {0x00000000009B8B69, 622, 2},
  {0x00000000009B8B7E, 624, 2},
  {0x00000000009B8B7F, 626, 2},
  {0x00000000009B8B80, 628, 2},
  {0x00000000009B8B82, 630, 2},
  {0x00000000009B8B84, 632, 2},
  {0x00000000009B8B87, 634, 2},
  {0x00000000009B8B97, 636, 2},
  {0x00000000009B8B98, 638, 2},
  {0x00000000009B8B9A, 640, 2},
  {0x00000000009B8B9B, 642, 2},
  {0x00000000009B8B9C, 644, 2},
  {0x00000000009B8B9D, 646, 2},
  {0x00000000009B8B9E, 648, 2},
  {0x00000000009B8B9F, 650, 2},
  {0x00000000009B8BA2, 652, 2},
  {0x00000000009B8BA3, 654, 2},
  {0x00000000009B8BA4, 656, 2},
  {0x00000000009B8BA6, 658, 2},
  {0x00000000009B8BA7, 660, 2},
  {0x00000000009B8BA8, 662, 2},
  {0x00000000009B8BA9, 664, 2},
  {0x00000000009B8BAA, 666, 2},
  {0x00000000009B8BAB, 668, 2},
  {0x00000000009B8BAD, 670, 2},
  {0x00000000009B8BAF, 672, 2},
  {0x00000000009B8BC2, 674, 2},
  {0x00000000009B8BC4, 676, 2},
  {0x00000000009B8BC5, 678, 2},
  {0x00000000009B8BC9, 680, 2},
  {0x00000000009B8BCB, 682, 2},
  {0x00000000009B8BCC, 684, 2},
  {0x00000000009B8BDB, 686, 2},
  {0x00000000009B8BDC, 688, 2},
  {0x00000000009B8BDD, 690, 2},
  {0x00000000009B8BE4, 692, 2},
  {0x00000000009B8BE5, 694, 2},
  {0x00000000009B8BE6, 696, 2},
  {0x00000000009B8BE7, 698, 2},
  {0x00000000009B8BE9, 700, 2},
  {0x00000000009B8BEA, 702, 2},
  {0x00000000009B8BEB, 704, 2},
  {0x00000000009B8BEC, 706, 2},
  {0x00000000009B8BFE, 708, 2},
  {0x00000000009B8C06, 710, 2},
  {0x00000000009B8C08, 712, 2},
  {0x00000000009B8C09, 714, 2},
  {0x00000000009B8C1F, 716, 2},
  {0x00000000009B8C21, 718, 2},
  {0x00000000009B8C22, 720, 2},
  {0x00000000009B8C23, 722, 2},
  {0x00000000009B8C27, 724, 2},
  {0x00000000009B8C28, 726, 2},
  {0x00000000009B8C2A, 728, 2},
  {0x00000000009B8C2C, 730, 2},
  {0x00000000009B8C31, 732, 2},
  {0x00000000009B8C33, 734, 2},
  {0x00000000009B8C34, 736, 2},
  {0x00000000009B8C3C, 738, 2},
  {0x00000000009B8C3D, 740, 2},
  {0x00000000009B8C3E, 742, 2},
  {0x00000000009B8C44, 744, 2},
  {0x00000000009B8C46, 746, 2},
  {0x00000000009B8C4D, 748, 2},
  {0x00000000009B8C4E, 750, 2},
  {0x00000000009B8C4F, 752, 2},
  {0x00000000009B8C50, 754, 2},
  {0x00000000009B8C51, 756, 2},
  {0x00000000009B8C54, 758, 2},
  {0x00000000009B8C5D, 760, 2},
  {0x00000000009B8C5F, 762, 2},
  {0x00000000009B8C60, 764, 2},
  {0x00000000009B8C61, 766, 2},
  {0x00000000009B8C62, 768, 2},
  {0x00000000009B8C63, 770, 2},
  {0x00000000009B8C64, 772, 2},
  {0x00000000009B8C67, 774, 2},
  {0x00000000009B8C68, 776, 2},
  {0x00000000009B8C69, 778, 2},
  {0x00000000009B8C6A, 780, 2},
  {0x00000000009B8C6B, 782, 2},
  {0x00000000009B8C6C, 784, 2},
  {0x00000000009B8C6D, 786, 2},
  {0x00000000009B8C6E, 788, 2},
  {0x00000000009B8C6F, 790, 2},
  {0x00000000009B8C70, 792, 2},
  {0x00000000009B8C71, 794, 2},
  {0x00000000009B8C72, 796, 2},
  {0x00000000009B8C73, 798, 2},
  {0x00000000009B8C74, 800, 2},
  {0x00000000009B8C75, 802, 2},
  {0x00000000009B8C76, 804, 2},
  {0x00000000009B8C7E, 806, 2},
  {0x00000000009B8C80, 808, 2},
  {0x00000000009B8C82, 810, 2},
  {0x00000000009B8C83, 812, 2},
  {0x00000000009B8C84, 814, 2},
  {0x00000000009B8C86, 816, 2},
  {0x00000000009B8C89, 818, 2},
  {0x00000000009B8C8C, 820, 2},
  {0x00000000009B8C8D, 822, 2},
  {0x00000000009B8C8F, 824, 2},
  {0x00000000009B8C92, 826, 2},
  {0x00000000009B8C97, 828, 2},
  {0x00000000009B8CAB, 830, 2},
  {0x00000000009B8CC0, 832, 2},
  {0x00000000009B8CC4, 834, 2},
  {0x00000000009B8CC5, 836, 2},
  {0x00000000009B8CC6, 838, 2},
  {0x00000000009B8CC7, 840, 2},
  {0x00000000009B8CCA, 842, 2},
  {0x00000000009B8CCB, 844, 2},
  {0x00000000009B8CCC, 846, 2},
  {0x00000000009B8CCD, 848, 2},
  {0x00000000009B8CD1, 850, 2},
  {0x00000000009B8CD2, 852, 2},
  {0x00000000009B8CD3, 854, 2},
  {0x00000000009B8CD6, 856, 2},
  {0x00000000009B8CD8, 858, 2},
  {0x00000000009B8CE1, 860, 2},
  {0x00000000009B8D06, 862, 2},
  {0x00000000009B8D10, 864, 2},
  {0x00000000009B8D14, 866, 2},
  {0x00000000009B8D16, 868, 2},
  {0x00000000009B8D18, 870, 2},
  {0x00000000009B8D23, 872, 2},
  {0x00000000009B8D24, 874, 2},
  {0x00000000009B8D25, 876, 2},
  {0x00000000009B8D26, 878, 2},
  {0x00000000009B8D27, 880, 2},
  {0x00000000009B8D29, 882, 2},
  {0x00000000009B8D2A, 884, 2},
  {0x00000000009B8D2B, 886, 2},
  {0x00000000009B8D2C, 888, 2},
  {0x00000000009B8D2D, 890, 2},
  {0x00000000009B8D2E, 892, 2},
  {0x00000000009B8D2F, 894, 2},
  {0x00000000009B8D30, 896, 2},
  {0x00000000009B8D31, 898, 2},
  {0x00000000009B8D34, 900, 2},
  {0x00000000009B8D35, 902, 2},
  {0x00000000009B8D36, 904, 2},
  {0x00000000009B8D38, 906, 2},
  {0x00000000009B8D3A, 908, 2},
  {0x00000000009B8D3B, 910, 2},
  {0x00000000009B8D3C, 912, 2},
  {0x00000000009B8D44, 914, 2},
  {0x00000000009B8D46, 916, 2},
  {0x00000000009B8D47, 918, 2},
  {0x00000000009B8D49, 920, 2},
  {0x00000000009B8D4A, 922, 2},
  {0x00000000009B8D4B, 924, 2},
  {0x00000000009B8D4D, 926, 2},
  {0x00000000009B8D4E, 928, 2},
  {0x00000000009B8D4F, 930, 2},
  {0x00000000009B8D50, 932, 2},
  {0x00000000009B8D51, 934, 2},
  {0x00000000009B8D52, 936, 2},
  {0x00000000009B8D55, 938, 2},
  {0x00000000009B8D57, 940, 2},
  {0x00000000009B8D59, 942, 2},
  {0x00000000009B8D5A, 944, 2},
  {0x00000000009B8D5D, 946, 2},
  {0x00000000009B8D65, 948, 2},
  {0x00000000009B8D6B, 950, 2},
  {0x00000000009B8D71, 952, 2},
  {0x00000000009B8D72, 954, 2},
  {0x00000000009B8D77, 956, 2},
  {0x00000000009B8D7D, 958, 2},
  {0x00000000009B8D7E, 960, 2},
  {0x00000000009B8D86, 962, 2},
  {0x00000000009B8D88, 964, 2},
  {0x00000000009B8D8A, 966, 2},
  {0x00000000009B8D8C, 968, 2},
  {0x00000000009B8D8E, 970, 2},
  {0x00000000009B8D93, 972, 2},
  {0x00000000009B8D9A, 974, 2},
  {0x00000000009B8DAC, 976, 2},
  {0x00000000009B8DB9, 978, 2},
  {0x00000000009B8DD2, 980, 2},
  {0x00000000009B8DED, 982, 2},
  {0x00000000009B8DFC, 984, 2},
  {0x00000000009B8E0A, 986, 2},
  {0x00000000009B8E16, 988, 2},
  {0x00000000009B8E20, 990, 2},
  {0x00000000009BC265, 992, 2},
  {0x00000000009BC266, 994, 2},
  {0x00000000009BC267, 996, 2},
  {0x00000000009BC268, 998, 2},
  {0x00000000009BC269, 1000, 2},
  {0x00000000009BCA42, 1002, 2},
  {0x00000000009BCA43, 1004, 2},
  {0x00000000009BCA44, 1006, 2},
  {0x00000000009BCA45, 1008, 2},
  {0x00000000009BCA46, 1010, 2},
  {0x00000000009BCA63, 1012, 2},
  {0x00000000009BCA64, 1014, 2},
  {0x00000000009BCA65, 1016, 2},
  {0x00000000009BCA66, 1018, 2},
  {0x00000000009BCA67, 1020, 2},
  {0x00000000009BCA84, 1022, 2},
  {0x00000000009BCA85, 1024, 2},
  {0x00000000009BCA86, 1026, 2},
  {0x00000000009BCA87, 1028, 2},
  {0x00000000009BCA88, 1030, 2},
  {0x00000000009BCAE7, 1032, 2},
  {0x00000000009BCAE8, 1034, 2},
  {0x00000000009BCAE9, 1036, 2},
  {0x00000000009BCAEA, 1038, 2},
  {0x00000000009BCAEB, 1040, 2},
  {0x00000000009BCB4A, 1042, 2},
  {0x00000000009BCB4B, 1044, 2},
  {0x00000000009BCB4C, 1046, 2},
  {0x00000000009BCB4D, 1048, 2},
  {0x00000000009BCB4E, 1050, 2},
  {0x00000000009BCB6B, 1052, 2},
  {0x00000000009BCB6C, 1054, 2},
  {0x00000000009BCB6D, 1056, 2},
  {0x00000000009BCB6E, 1058, 2},
  {0x00000000009BCB6F, 1060, 2},
  {0x00000000009BCB8C, 1062, 2},
  {0x00000000009BCB8D, 1064, 2},
  {0x00000000009BCB8E, 1066, 2},
  {0x00000000009BCB8F, 1068, 2},
  {0x00000000009BCB90, 1070, 2},
  {0x00000000009BDAC2, 1072, 2},
  {0x00000000009BDAC3, 1074, 2},
  {0x00000000009BDAC4, 1076, 2},
  {0x00000000009BDAC5, 1078, 2},
  {0x00000000009BDAC6, 1080, 2},
  {0x00000000009BDAE3, 1082, 2},
  {0x00000000009BDAE4, 1084, 2},
  {0x00000000009BDAE5, 1086, 2},
  {0x00000000009BDAE6, 1088, 2},
  {0x00000000009BDAE7, 1090, 2},
  {0x00000000009BDB46, 1092, 2},
  {0x00000000009BDB47, 1094, 2},
  {0x00000000009BDB48, 1096, 2},
  {0x00000000009BDB49, 1098, 2},
  {0x00000000009BDB4A, 1100, 2},
  {0x00000000009BDB67, 1102, 2},
  {0x00000000009BDB68, 1104, 2},
  {0x00000000009BDB69, 1106, 2},
  {0x00000000009BDB6A, 1108, 2},
  {0x00000000009BDB6B, 1110, 2},
  {0x00000000009BDB88, 1112, 2},
  {0x00000000009BDB89, 1114, 2},
  {0x00000000009BDB8A, 1116, 2},
  {0x00000000009BDB8B, 1118, 2},
  {0x00000000009BDB8C, 1120, 2},
  {0x00000000009BDBA9, 1122, 2},
  {0x00000000009BDBAA, 1124, 2},
  {0x00000000009BDBAB, 1126, 2},
  {0x00000000009BDBAC, 1128, 2},
  {0x00000000009BDBAD, 1130, 2},
  {0x00000000009BDBCA, 1132, 2},
  {0x00000000009BDBCB, 1134, 2},
  {0x00000000009BDBCC, 1136, 2},
  {0x00000000009BDBCD, 1138, 2},
  {0x00000000009BDBCE, 1140, 2},
  {0x00000000009BDBEB, 1142, 2},
  {0x00000000009BDBEC, 1144, 2},
  {0x00000000009BDBED, 1146, 2},
  {0x00000000009BDBEE, 1148, 2},
  {0x00000000009BDBEF, 1150, 2},
  {0x00000000009BDC0C, 1152, 2},
  {0x00000000009BDC0D, 1154, 2},
  {0x00000000009BDC0E, 1156, 2},
  {0x00000000009BDC0F, 1158, 2},
  {0x00000000009BDC10, 1160, 2},
  {0x00000000009BDC2D, 1162, 2},
  {0x00000000009BDC2E, 1164, 2},
  {0x00000000009BDC2F, 1166, 2},
  {0x00000000009BDC30, 1168, 2},
  {0x00000000009BDC31, 1170, 2},
  {0x00000000009BDC4E, 1172, 2},
  {0x00000000009BDC4F, 1174, 2},
  {0x00000000009BDC50, 1176, 2},
  {0x00000000009BDC51, 1178, 2},
  {0x00000000009BDC52, 1180, 2},
  {0x00000000009BDC6F, 1182, 2},
  {0x00000000009BDC70, 1184, 2},
  {0x00000000009BDC71, 1186, 2},
  {0x00000000009BDC72, 1188, 2},
  {0x00000000009BDC73, 1190, 2},
  {0x00000000009BDC90, 1192, 2},
  {0x00000000009BDC91, 1194, 2},
  {0x00000000009BDC92, 1196, 2},
  {0x00000000009BDC93, 1198, 2},
  {0x00000000009BDC94, 1200, 2},
  {0x00000000009BDF66, 1202, 2},
  {0x00000000009BDF67, 1204, 2},
  {0x00000000009BDF68, 1206, 2},
  {0x00000000009BDF69, 1208, 2},
  {0x00000000009BDF6A, 1210, 2},
  {0x00000000009BDF87, 1212, 2},
  {0x00000000009BDF88, 1214, 2},
  {0x00000000009BDF89, 1216, 2},
  {0x00000000009BDF8A, 1218, 2},
  {0x00000000009BDF8B, 1220, 2},
  {0x00000000009BDFA8, 1222, 2},
  {0x00000000009BDFA9, 1224, 2},
  {0x00000000009BDFAA, 1226, 2},
  {0x00000000009BDFAB, 1228, 2},
  {0x00000000009BDFAC, 1230, 2},
  {0x00000000009BDFC9, 1232, 2},
  {0x00000000009BDFCA, 1234, 2},
  {0x00000000009BDFCB, 1236, 2},
  {0x00000000009BDFCC, 1238, 2},
  {0x00000000009BDFCD, 1240, 2},
  {0x00000000009BE00B, 1242, 2},
  {0x00000000009BE00C, 1244, 2},
  {0x00000000009BE00D, 1246, 2},
  {0x00000000009BE00E, 1248, 2},
  {0x00000000009BE00F, 1250, 2},
  {0x00000000009BE02C, 1252, 2},
  {0x00000000009BE02D, 1254, 2},
  {0x00000000009BE02E, 1256, 2},
  {0x00000000009BE02F, 1258, 2},
  {0x00000000009BE030, 1260, 2},
  {0x00000000009BE04D, 1262, 2},
  {0x00000000009BE04E, 1264, 2},
  {0x00000000009BE04F, 1266, 2},
  {0x00000000009BE050, 1268, 2},
  {0x00000000009BE051, 1270, 2},
  {0x00000000009BE06E, 1272, 2},
  {0x00000000009BE06F, 1274, 2},
  {0x00000000009BE070, 1276, 2},
  {0x00000000009BE071, 1278, 2},
  {0x00000000009BE072, 1280, 2},
  {0x00000000009BE08F, 1282, 2},
  {0x00000000009BE090, 1284, 2},
  {0x00000000009BE091, 1286, 2},
  {0x00000000009BE092, 1288, 2},
  {0x00000000009BE093, 1290, 2},
  {0x00000000009BE0B0, 1292, 2},
  {0x00000000009BE0B1, 1294, 2},
  {0x00000000009BE0B2, 1296, 2},
  {0x00000000009BE0B3, 1298, 2},
  {0x00000000009BE0B4, 1300, 2},
  {0x00000000009BE0D1, 1302, 2},
  {0x00000000009BE0D2, 1304, 2},
  {0x00000000009BE0D3, 1306, 2},
  {0x00000000009BE0D4, 1308, 2},
  {0x00000000009BE0D5, 1310, 2},
  {0x00000000009BE0F2, 1312, 2},
  {0x00000000009BE0F3, 1314, 2},
  {0x00000000009BE0F4, 1316, 2},
  {0x00000000009BE0F5, 1318, 2},
  {0x00000000009BE0F6, 1320, 2},
  {0x00000000009BE113, 1322, 2},
  {0x00000000009BE114, 1324, 2},
  {0x00000000009BE115, 1326, 2},
  {0x00000000009BE116, 1328, 2},
  {0x00000000009BE117, 1330, 2},
  {0x00000000009BE134, 1332, 2},
  {0x00000000009BE135, 1334, 2},
  {0x00000000009BE136, 1336, 2},
  {0x00000000009BE137, 1338, 2},
  {0x00000000009BE138, 1340, 2},
  {0x00000000009BE155, 1342, 2},
  {0x00000000009BE156, 1344, 2},
  {0x00000000009BE157, 1346, 2},
  {0x00000000009BE158, 1348, 2},
  {0x00000000009BE159, 1350, 2},
  {0x00000000009BE176, 1352, 2},
  {0x00000000009BE177, 1354, 2},
  {0x00000000009BE178, 1356, 2},
  {0x00000000009BE179, 1358, 2},
  {0x00000000009BE17A, 1360, 2},
  {0x00000000009BE197, 1362, 2},
  {0x00000000009BE198, 1364, 2},
  {0x00000000009BE199, 1366, 2},
  {0x00000000009BE19A, 1368, 2},
  {0x00000000009BE19B, 1370, 2},
  {0x00000000009BE1B8, 1372, 2},
  {0x00000000009BE1B9, 1374, 2},
  {0x00000000009BE1BA, 1376, 2},
  {0x00000000009BE1BB, 1378, 2},
  {0x00000000009BE1BC, 1380, 2},
  {0x00000000009BE23C, 1382, 2},
  {0x00000000009BE23D, 1384, 2},
  {0x00000000009BE23E, 1386, 2},
  {0x00000000009BE23F, 1388, 2},
  {0x00000000009BE240, 1390, 2},
  {0x00000000009BE2E1, 1392, 2},
  {0x00000000009BE2E2, 1394, 2},
  {0x00000000009BE2E3, 1396, 2},
  {0x00000000009BE2E4, 1398, 2},
  {0x00000000009BE2E5, 1400, 2},
  {0x00000000009BE302, 1402, 2},
  {0x00000000009BE303, 1404, 2},
  {0x00000000009BE304, 1406, 2},
  {0x00000000009BE305, 1408, 2},
  {0x00000000009BE306, 1410, 2},
  {0x00000000009BE323, 1412, 2},
  {0x00000000009BE324, 1414, 2},
  {0x00000000009BE325, 1416, 2},
  {0x00000000009BE326, 1418, 2},
  {0x00000000009BE327, 1420, 2},
  {0x00000000009BE365, 1422, 2},
  {0x00000000009BE366, 1424, 2},
  {0x00000000009BE367, 1426, 2},
  {0x00000000009BE368, 1428, 2},
  {0x00000000009BE369, 1430, 2},
  {0x00000000009BE386, 1432, 2},
  {0x00000000009BE387, 1434, 2},
  {0x00000000009BE388, 1436, 2},
  {0x00000000009BE389, 1438, 2},
  {0x00000000009BE38A, 1440, 2},
  {0x00000000009BE3A7, 1442, 2},
  {0x00000000009BE3A8, 1444, 2},
  {0x00000000009BE3A9, 1446, 2},
  {0x00000000009BE3AA, 1448, 2},
  {0x00000000009BE3AB, 1450, 2},
  {0x00000000009BE4AF, 1452, 2},
  {0x00000000009BE4B0, 1454, 2},
  {0x00000000009BE4B1, 1456, 2},
  {0x00000000009BE4B2, 1458, 2},
  {0x00000000009BE4B3, 1460, 2},
  {0x00000000009BE4F1, 1462, 2},
  {0x00000000009BE4F2, 1464, 2},
  {0x00000000009BE4F3, 1466, 2},
  {0x00000000009BE4F4, 1468, 2},
  {0x00000000009BE4F5, 1470, 2},
  {0x00000000009BE82A, 1472, 2},
  {0x00000000009BE82B, 1474, 2},
  {0x00000000009BE82C, 1476, 2},
  {0x00000000009BE82D, 1478, 2},
  {0x00000000009BE82E, 1480, 2},
  {0x00000000009C0234, 1482, 2},
  {0x00000000009C0235, 1484, 2},
  {0x00000000009C0236, 1486, 2},
  {0x00000000009C0237, 1488, 2},
  {0x00000000009C0238, 1490, 2},
  {0x00000000009C0255, 1492, 2},
  {0x00000000009C0256, 1494, 2},
  {0x00000000009C0257, 1496, 2},
  {0x00000000009C0258, 1498, 2},
  {0x00000000009C0259, 1500, 2},
  {0x00000000009C02FA, 1502, 2},
  {0x00000000009C02FB, 1504, 2},
  {0x00000000009C02FC, 1506, 2},
  {0x00000000009C02FD, 1508, 2},
  {0x00000000009C02FE, 1510, 2},
  {0x00000000009C05D0, 1512, 2},
  {0x00000000009C05D1, 1514, 2},
  {0x00000000009C05D2, 1516, 2},
  {0x00000000009C05D3, 1518, 2},
  {0x00000000009C05D4, 1520, 2},
  {0x00000000009C0675, 1522, 2},
  {0x00000000009C0676, 1524, 2},
  {0x00000000009C0677, 1526, 2},
  {0x00000000009C0678, 1528, 2},
  {0x00000000009C0679, 1530, 2},
  {0x00000000009C0696, 1532, 2},
  {0x00000000009C0697, 1534, 2},
  {0x00000000009C0698, 1536, 2},
  {0x00000000009C0699, 1538, 2},
  {0x00000000009C069A, 1540, 2},
  {0x00000000009C1D25, 1542, 2},
  {0x00000000009C1D26, 1544, 2},
  {0x00000000009C1D27, 1546, 2},
  {0x00000000009C1D28, 1548, 2},
  {0x00000000009C1D29, 1550, 2},
  {0x00000000009C1D46, 1552, 2},
  {0x00000000009C1D47, 1554, 2},
  {0x00000000009C1D48, 1556, 2},
  {0x00000000009C1D49, 1558, 2},
  {0x00000000009C1D4A, 1560, 2},
  {0x00000000009C1D67, 1562, 2},
  {0x00000000009C1D68, 1564, 2},
  {0x00000000009C1D69, 1566, 2},
  {0x00000000009C1D6A, 1568, 2},
  {0x00000000009C1D6B, 1570, 2},
  {0x00000000009C1DEB, 1572, 2},
  {0x00000000009C1DEC, 1574, 2},
  {0x00000000009C1DED, 1576, 2},
  {0x00000000009C1DEE, 1578, 2},
  {0x00000000009C1DEF, 1580, 2},
  {0x00000000009C1E0C, 1582, 2},
  {0x00000000009C1E0D, 1584, 2},
  {0x00000000009C1E0E, 1586, 2},
  {0x00000000009C1E0F, 1588, 2},
  {0x00000000009C1E10, 1590, 2},
  {0x00000000009C1E2D, 1592, 2},
  {0x00000000009C1E2E, 1594, 2},
  {0x00000000009C1E2F, 1596, 2},
  {0x00000000009C1E30, 1598, 2},
  {0x00000000009C1E31, 1600, 2},
  {0x00000000009C1E4E, 1602, 2},
  {0x00000000009C1E4F, 1604, 2},
  {0x00000000009C1E50, 1606, 2},
  {0x00000000009C1E51, 1608, 2},
  {0x00000000009C1E52, 1610, 2},
  {0x00000000009C1E6F, 1612, 2},
  {0x00000000009C1E70, 1614, 2},
  {0x00000000009C1E71, 1616, 2},
  {0x00000000009C1E72, 1618, 2},
  {0x00000000009C1E73, 1620, 2},
  {0x00000000009C2943, 1622, 2},
  {0x00000000009C2944, 1624, 2},
  {0x00000000009C2945, 1626, 2},
  {0x00000000009C2946, 1628, 2},
  {0x00000000009C2947, 1630, 2},
  {0x00000000009C2B74, 1632, 2},
  {0x00000000009C2B75, 1634, 2},
  {0x00000000009C2B76, 1636, 2},
  {0x00000000009C2B77, 1638, 2},
  {0x00000000009C2B78, 1640, 2},
  {0x00000000009C2B95, 1642, 2},
  {0x00000000009C2B96, 1644, 2},
  {0x00000000009C2B97, 1646, 2},
  {0x00000000009C2B98, 1648, 2},
  {0x00000000009C2B99, 1650, 2},
  {0x00000000009C2BB6, 1652, 2},
  {0x00000000009C2BB7, 1654, 2},
  {0x00000000009C2BB8, 1656, 2},
  {0x00000000009C2BB9, 1658, 2},
  {0x00000000009C2BBA, 1660, 2},
  {0x00000000009C2D00, 1662, 2},
  {0x00000000009C2D01, 1664, 2},
  {0x00000000009C2D02, 1666, 2},
  {0x00000000009C2D03, 1668, 2},
  {0x00000000009C2D04, 1670, 2},
  {0x00000000009C2E8C, 1672, 2},
  {0x00000000009C2E8D, 1674, 2},
  {0x00000000009C2E8E, 1676, 2},
  {0x00000000009C2E8F, 1678, 2},
  {0x00000000009C2E90, 1680, 2},
  {0x00000000009C78CC, 1682, 2},
  {0x00000000009C78CD, 1684, 2},
  {0x00000000009C78CE, 1686, 2},
  {0x00000000009C78CF, 1688, 2},
  {0x00000000009C78D0, 1690, 2},
  {0x00000000009C792F, 1692, 2},
  {0x00000000009C7930, 1694, 2},
  {0x00000000009C7931, 1696, 2},
  {0x00000000009C7932, 1698, 2},
  {0x00000000009C7933, 1700, 2},
  {0x00000000009C7A58, 1702, 2},
  {0x00000000009C7A59, 1704, 2},
  {0x00000000009C7A5A, 1706, 2},
  {0x00000000009C7A5B, 1708, 2},
  {0x00000000009C7A5C, 1710, 2},
  {0x00000000009C7A79, 1712, 2},
  {0x00000000009C7A7A, 1714, 2},
  {0x00000000009C7A7B, 1716, 2},
  {0x00000000009C7A7C, 1718, 2},
  {0x00000000009C7A7D, 1720, 2},
  {0x00000000009C7A9A, 1722, 2},
  {0x00000000009C7A9B, 1724, 2},
  {0x00000000009C7A9C, 1726, 2},
  {0x00000000009C7A9D, 1728, 2},
  {0x00000000009C7A9E, 1730, 2},
  {0x00000000009C7ABB, 1732, 2},
  {0x00000000009C7ABC, 1734, 2},
  {0x00000000009C7ABD, 1736, 2},
  {0x00000000009C7ABE, 1738, 2},
  {0x00000000009C7ABF, 1740, 2},
  {0x00000000009C7ADC, 1742, 2},
  {0x00000000009C7ADD, 1744, 2},
  {0x00000000009C7ADE, 1746, 2},
  {0x00000000009C7ADF, 1748, 2},
  {0x00000000009C7AE0, 1750, 2},
  {0x00000000009C7AFD, 1752, 2},
  {0x00000000009C7AFE, 1754, 2},
  {0x00000000009C7AFF, 1756, 2},
  {0x00000000009C7B00, 1758, 2},
  {0x00000000009C7B01, 1760, 2},
  {0x00000000009C7B1E, 1762, 2},
  {0x00000000009C7B1F, 1764, 2},
  {0x00000000009C7B20, 1766, 2},
  {0x00000000009C7B21, 1768, 2},
  {0x00000000009C7B22, 1770, 2},
  {0x00000000009C7B3F, 1772, 2},
  {0x00000000009C7B40, 1774, 2},
  {0x00000000009C7B41, 1776, 2},
  {0x00000000009C7B42, 1778, 2},
  {0x00000000009C7B43, 1780, 2},
  {0x00000000009C7C26, 1782, 2},
  {0x00000000009C7C27, 1784, 2},
  {0x00000000009C7C28, 1786, 2},
  {0x00000000009C7C29, 1788, 2},
  {0x00000000009C7C2A, 1790, 2},
  {0x00000000009C7D70, 1792, 2},
  {0x00000000009C7D71, 1794, 2},
  {0x00000000009C7D72, 1796, 2},
  {0x00000000009C7D73, 1798, 2},
  {0x00000000009C7D74, 1800, 2},
  {0x00000000009C7D91, 1802, 2},
  {0x00000000009C7D92, 1804, 2},
  {0x00000000009C7D93, 1806, 2},
  {0x00000000009C7D94, 1808, 2},
  {0x00000000009C7D95, 1810, 2},
  {0x00000000009C7DB2, 1812, 2},
  {0x00000000009C7DB3, 1814, 2},
  {0x00000000009C7DB4, 1816, 2},
  {0x00000000009C7DB5, 1818, 2},
  {0x00000000009C7DB6, 1820, 2},
  {0x00000000009C7DD3, 1822, 2},
  {0x00000000009C7DD4, 1824, 2},
  {0x00000000009C7DD5, 1826, 2},
  {0x00000000009C7DD6, 1828, 2},
  {0x00000000009C7DD7, 1830, 2},
  {0x00000000009C7DF4, 1832, 2},
  {0x00000000009C7DF5, 1834, 2},
  {0x00000000009C7DF6, 1836, 2},
  {0x00000000009C7DF7, 1838, 2},
  {0x00000000009C7DF8, 1840, 2},
  {0x00000000009C7E15, 1842, 2},
  {0x00000000009C7E16, 1844, 2},
  {0x00000000009C7E17, 1846, 2},
  {0x00000000009C7E18, 1848, 2},
  {0x00000000009C7E19, 1850, 2},
  {0x00000000009C7E36, 1852, 2},
  {0x00000000009C7E37, 1854, 2},
  {0x00000000009C7E38, 1856, 2},
  {0x00000000009C7E39, 1858, 2},
  {0x00000000009C7E3A, 1860, 2},
  {0x00000000009C7E57, 1862, 2},
  {0x00000000009C7E58, 1864, 2},
  {0x00000000009C7E59, 1866, 2},
  {0x00000000009C7E5A, 1868, 2},
  {0x00000000009C7E5B, 1870, 2},
  {0x00000000009C7E78, 1872, 2},
  {0x00000000009C7E79, 1874, 2},
  {0x00000000009C7E7A, 1876, 2},
  {0x00000000009C7E7B, 1878, 2},
  {0x00000000009C7E7C, 1880, 2},
  {0x00000000009C7E99, 1882, 2},
  {0x00000000009C7E9A, 1884, 2},
  {0x00000000009C7E9B, 1886, 2},
  {0x00000000009C7E9C, 1888, 2},
  {0x00000000009C7E9D, 1890, 2},
  {0x00000000009C7EFC, 1892, 2},
  {0x00000000009C7EFD, 1894, 2},
  {0x00000000009C7EFE, 1896, 2},
  {0x00000000009C7EFF, 1898, 2},
  {0x00000000009C7F00, 1900, 2},
  {0x00000000009C7F1D, 1902, 2},
  {0x00000000009C7F1E, 1904, 2},
  {0x00000000009C7F1F, 1906, 2},
  {0x00000000009C7F20, 1908, 2},
  {0x00000000009C7F21, 1910, 2},
  {0x00000000009C7F3E, 1912, 2},
  {0x00000000009C7F3F, 1914, 2},
  {0x00000000009C7F40, 1916, 2},
  {0x00000000009C7F41, 1918, 2},
  {0x00000000009C7F42, 1920, 2},
  {0x00000000009C8697, 1922, 2},
  {0x00000000009C8698, 1924, 2},
  {0x00000000009C8699, 1926, 2},
  {0x00000000009C869A, 1928, 2},
  {0x00000000009C869B, 1930, 2},
  {0x00000000009C8E95, 1932, 2},
  {0x00000000009C8E96, 1934, 2},
  {0x00000000009C8E97, 1936, 2},
  {0x00000000009C8E98, 1938, 2},
  {0x00000000009C8E99, 1940, 2},
  {0x00000000009C8EB6, 1942, 2},
  {0x00000000009C8EB7, 1944, 2},
  {0x00000000009C8EB8, 1946, 2},
  {0x00000000009C8EB9, 1948, 2},
  {0x00000000009C8EBA, 1950, 2},
  {0x00000000009C8EF8, 1952, 2},
  {0x00000000009C8EF9, 1954, 2},
  {0x00000000009C8EFA, 1956, 2},
  {0x00000000009C8EFB, 1958, 2},
  {0x00000000009C8EFC, 1960, 2},
  {0x00000000009C8F19, 1962, 2},
  {0x00000000009C8F1A, 1964, 2},
  {0x00000000009C8F1B, 1966, 2},
  {0x00000000009C8F1C, 1968, 2},
  {0x00000000009C8F1D, 1970, 2},
  {0x00000000009C8F5B, 1972, 2},
  {0x00000000009C8F5C, 1974, 2},
  {0x00000000009C8F5D, 1976, 2},
  {0x00000000009C8F5E, 1978, 2},
  {0x00000000009C8F5F, 1980, 2},
  {0x00000000009C91AD, 1982, 2},
  {0x00000000009C91AE, 1984, 2},
  {0x00000000009C91AF, 1986, 2},
  {0x00000000009C91B0, 1988, 2},
  {0x00000000009C91B1, 1990, 2},
  {0x00000000009C91CE, 1992, 2},
  {0x00000000009C91CF, 1994, 2},
  {0x00000000009C91D0, 1996, 2},
  {0x00000000009C91D1, 1998, 2},
  {0x00000000009C91D2, 2000, 2},
  {0x00000000009C91EF, 2002, 2},
  {0x00000000009C91F0, 2004, 2},
  {0x00000000009C91F1, 2006, 2},
  {0x00000000009C91F2, 2008, 2},
  {0x00000000009C91F3, 2010, 2},
  {0x00000000009C9231, 2012, 2},
  {0x00000000009C9232, 2014, 2},
  {0x00000000009C9233, 2016, 2},
  {0x00000000009C9234, 2018, 2},
  {0x00000000009C9235, 2020, 2},
  {0x00000000009C9252, 2022, 2},
  {0x00000000009C9253, 2024, 2},
  {0x00000000009C9254, 2026, 2},
  {0x00000000009C9255, 2028, 2},
  {0x00000000009C9256, 2030, 2},
  {0x00000000009C9273, 2032, 2},
  {0x00000000009C9274, 2034, 2},
  {0x00000000009C9275, 2036, 2},
  {0x00000000009C9276, 2038, 2},
  {0x00000000009C9277, 2040, 2},
  {0x00000000009C9294, 2042, 2},
  {0x00000000009C9295, 2044, 2},
  {0x00000000009C9296, 2046, 2},
  {0x00000000009C9297, 2048, 2},
  {0x00000000009C9298, 2050, 2},
  {0x00000000009C92B5, 2052, 2},
  {0x00000000009C92B6, 2054, 2},
  {0x00000000009C92B7, 2056, 2},
  {0x00000000009C92B8, 2058, 2},
  {0x00000000009C92B9, 2060, 2},
  {0x00000000009C92D6, 2062, 2},
  {0x00000000009C92D7, 2064, 2},
  {0x00000000009C92D8, 2066, 2},
  {0x00000000009C92D9, 2068, 2},
  {0x00000000009C92DA, 2070, 2},
  {0x00000000009C92F7, 2072, 2},
  {0x00000000009C92F8, 2074, 2},
  {0x00000000009C92F9, 2076, 2},
  {0x00000000009C92FA, 2078, 2},
  {0x00000000009C92FB, 2080, 2},
  {0x00000000009C9318, 2082, 2},
  {0x00000000009C9319, 2084, 2},
  {0x00000000009C931A, 2086, 2},
  {0x00000000009C931B, 2088, 2},
  {0x00000000009C931C, 2090, 2},
  {0x00000000009C9339, 2092, 2},
  {0x00000000009C933A, 2094, 2},
  {0x00000000009C933B, 2096, 2},
  {0x00000000009C933C, 2098, 2},
  {0x00000000009C933D, 2100, 2},
  {0x00000000009C935A, 2102, 2},
  {0x00000000009C935B, 2104, 2},
  {0x00000000009C935C, 2106, 2},
  {0x00000000009C935D, 2108, 2},
  {0x00000000009C935E, 2110, 2},
  {0x00000000009C937B, 2112, 2},
  {0x00000000009C937C, 2114, 2},
  {0x00000000009C937D, 2116, 2},
  {0x00000000009C937E, 2118, 2},
  {0x00000000009C937F, 2120, 2},
  {0x00000000009C939C, 2122, 2},
  {0x00000000009C939D, 2124, 2},
  {0x00000000009C939E, 2126, 2},
  {0x00000000009C939F, 2128, 2},
  {0x00000000009C93A0, 2130, 2},
  {0x00000000009C93BD, 2132, 2},
  {0x00000000009C93BE, 2134, 2},
  {0x00000000009C93BF, 2136, 2},
  {0x00000000009C93C0, 2138, 2},
  {0x00000000009C93C1, 2140, 2},
  {0x00000000009CB163, 2142, 2},
  {0x00000000009CB164, 2144, 2},
  {0x00000000009CB165, 2146, 2},
  {0x00000000009CB166, 2148, 2},
  {0x00000000009CB167, 2150, 2},
  {0x00000000009CB184, 2152, 2},
  {0x00000000009CB185, 2154, 2},
  {0x00000000009CB186, 2156, 2},
  {0x00000000009CB187, 2158, 2},
  {0x00000000009CB188, 2160, 2},
  {0x00000000009CB1A5, 2162, 2},
  {0x00000000009CB1A6, 2164, 2},
  {0x00000000009CB1A7, 2166, 2},
  {0x00000000009CB1A8, 2168, 2},
  {0x00000000009CB1A9, 2170, 2},
  {0x00000000009CB730, 2172, 2},
  {0x00000000009CB731, 2174, 2},
  {0x00000000009CB732, 2176, 2},
  {0x00000000009CB733, 2178, 2},
  {0x00000000009CB734, 2180, 2},
  {0x00000000009CB751, 2182, 2},
  {0x00000000009CB752, 2184, 2},
  {0x00000000009CB753, 2186, 2},
  {0x00000000009CB754, 2188, 2},
  {0x00000000009CB755, 2190, 2},
  {0x00000000009CB772, 2192, 2},
  {0x00000000009CB773, 2194, 2},
  {0x00000000009CB774, 2196, 2},
  {0x00000000009CB775, 2198, 2},
  {0x00000000009CB776, 2200, 2},
  {0x00000000009CB793, 2202, 2},
  {0x00000000009CB794, 2204, 2},
  {0x00000000009CB795, 2206, 2},
  {0x00000000009CB796, 2208, 2},
  {0x00000000009CB797, 2210, 2},
  {0x00000000009CB7B4, 2212, 2},
  {0x00000000009CB7B5, 2214, 2},
  {0x00000000009CB7B6, 2216, 2},
  {0x00000000009CB7B7, 2218, 2},
  {0x00000000009CB7B8, 2220, 2},
  {0x00000000009CB7D5, 2222, 2},
  {0x00000000009CB7D6, 2224, 2},
  {0x00000000009CB7D7, 2226, 2},
  {0x00000000009CB7D8, 2228, 2},
  {0x00000000009CB7D9, 2230, 2},
  {0x00000000009CB7F6, 2232, 2},
  {0x00000000009CB7F7, 2234, 2},
  {0x00000000009CB7F8, 2236, 2},
  {0x00000000009CB7F9, 2238, 2},
  {0x00000000009CB7FA, 2240, 2},
  {0x00000000009CB817, 2242, 2},
  {0x00000000009CB818, 2244, 2},
  {0x00000000009CB819, 2246, 2},
  {0x00000000009CB81A, 2248, 2},
  {0x00000000009CB81B, 2250, 2},
  {0x00000000009CB838, 2252, 2},
  {0x00000000009CB839, 2254, 2},
  {0x00000000009CB83A, 2256, 2},
  {0x00000000009CB83B, 2258, 2},
  {0x00000000009CB83C, 2260, 2},
  {0x000000000BA8289A, 2262, 3},
  {0x000000000BA84661, 2265, 3},
  {0x000000000BA85FE7, 2268, 3},
  {0x000000000BA86428, 2271, 3},
  {0x000000000BA86869, 2274, 3},
  {0x000000000BA86CAA, 2277, 3},
  {0x000000000BA870EB, 2280, 3},
  {0x000000000BA8752C, 2283, 3},
  {0x000000000BA8796D, 2286, 3},
  {0x000000000BA87DAE, 2289, 3},
  {0x000000000BA881EF, 2292, 3},
  {0x000000000BA88630, 2295, 3},
  {0x0000000013D8A0C1, 2298, 3},
  {0x0000000013D8BE86, 2301, 3},
  {0x0000000013DA15B5, 2304, 3},
  {0x0000000013DA9553, 2307, 3},
  {0x0000000013DC1BA1, 2310, 3},
  {0x0000000013DC5F5D, 2313, 3},
  {0x0000000013DD7638, 2316, 3},
  {0x0000000013DD766D, 2319, 3},
  {0x0000000013DD7676, 2322, 3},
  {0x0000000013DD768D, 2325, 3},
  {0x0000000013DD769E, 2328, 3},
  {0x0000000013DD76A2, 2331, 3},
  {0x0000000013DD76E5, 2334, 3},
  {0x0000000013DD76E7, 2337, 3},
  {0x0000000013DD7760, 2340, 3},
  {0x0000000013DD7761, 2343, 3},
  {0x0000000013DD77B5, 2346, 3},
  {0x0000000013DD77B6, 2349, 3},
  {0x0000000013DD7821, 2352, 3},
  {0x0000000013DD7826, 2355, 3},
  {0x0000000013DD797A, 2358, 3},
  {0x0000000013DD798C, 2361, 3},
  {0x0000000013DD7A79, 2364, 3},
  {0x0000000013DD7AAE, 2367, 3},
  {0x0000000013DD7AB7, 2370, 3},
  {0x0000000013DD7ACE, 2373, 3},
  {0x0000000013DD7ADF, 2376, 3},
  {0x0000000013DD7AE3, 2379, 3},
  {0x0000000013DD7B26, 2382, 3},
  {0x0000000013DD7B28, 2385, 3},
  {0x0000000013DD7BA1, 2388, 3},
  {0x0000000013DD7BA2, 2391, 3},
  {0x0000000013DD7BF6, 2394, 3},
  {0x0000000013DD7BF7, 2397, 3},
  {0x0000000013DD7C62, 2400, 3},
  {0x0000000013DD7C67, 2403, 3},
  {0x0000000013DD7CA9, 2406, 3},
  {0x0000000013DD7CAA, 2409, 3},
  {0x0000000013DD7CAB, 2412, 3},
  {0x0000000013DD7CAC, 2415, 3},
  {0x0000000013DD7CAD, 2418, 3},
  {0x0000000013DD7CB6, 2421, 3},
  {0x0000000013DD7CB7, 2424, 3},
  {0x0000000013DD7DBB, 2427, 3},
  {0x0000000013DD7DCD, 2430, 3},
  {0x0000000013DD80EA, 2433, 3},
  {0x0000000013DD80EB, 2436, 3},
  {0x0000000013DD80EC, 2439, 3},
  {0x0000000013DD80ED, 2442, 3},
  {0x0000000013DD80EE, 2445, 3},
  {0x0000000013DD80F7, 2448, 3},
  {0x0000000013DD80F8, 2451, 3},
  {0x0000000013E502E8, 2454, 3},
  {0x0000000013E520B2, 2457, 3},
  {0x0000000013F479E1, 2460, 3},
  {0x0000000013F47A16, 2463, 3},
  {0x0000000013F47A1F, 2466, 3},
  {0x0000000013F47A27, 2469, 3},
  {0x0000000013F47A36, 2472, 3},
  {0x0000000013F47A47, 2475, 3},
  {0x0000000013F47A4B, 2478, 3},
  {0x0000000013F47A8E, 2481, 3},
  {0x0000000013F47A90, 2484, 3},
  {0x0000000013F47B5E, 2487, 3},
  {0x0000000013F47B5F, 2490, 3},
  {0x0000000013F47BCA, 2493, 3},
  {0x0000000013F47BCF, 2496, 3},
  {0x0000000013F47D23, 2499, 3},
  {0x0000000013F47D35, 2502, 3},
  {0x0000000013F48052, 2505, 3},
  {0x0000000013F48053, 2508, 3},
  {0x0000000013F48054, 2511, 3},
  {0x0000000013F48055, 2514, 3},
  {0x0000000013F48056, 2517, 3},
  {0x0000000013F4805F, 2520, 3},
  {0x0000000013F48060, 2523, 3},
  {0x0000000013F48075, 2526, 3},
  {0x0000000013F48113, 2529, 3},
  {0x0000000195E5FD99, 2532, 4},
  {0x000000019635810A, 2536, 4},
  {0x000000019635865E, 2540, 4},
  {0x000000028EF75404, 2544, 4},
  {0x000000028EF75446, 2548, 4},
  {0x000000028EF78185, 2552, 4},
  {0x000000028EF7E065, 2556, 4},
  {0x000000028EF7E0A7, 2560, 4},
  {0x000000028EFB2AAB, 2564, 4},
  {0x000000028EFB2AED, 2568, 4},
  {0x000000028F122E75, 2572, 4},
  {0x000000028F394300, 2576, 4},
  {0x000000028F51D97E, 2580, 4},
  {0x000000028F51D99F, 2584, 4},
  {0x000000028F51E851, 2588, 4},
  {0x000000028F5265DF, 2592, 4},
  {0x000000028F526600, 2596, 4},
  {0x000000028F5274B2, 2600, 4},
  {0x000000028F5518CF, 2604, 4},
  {0x000000028F551911, 2608, 4},
  {0x000000028F55A530, 2612, 4},
  {0x000000028F55A572, 2616, 4},
  {0x000000028F563191, 2620, 4},
  {0x000000028F5631D3, 2624, 4},
  {0x000000028F56BDF2, 2628, 4},
  {0x000000028F56BE34, 2632, 4},
  {0x000000028F57D6B4, 2636, 4},
  {0x000000028F57D6F6, 2640, 4},
  {0x000000028F5A0838, 2644, 4},
  {0x000000028F5A087A, 2648, 4},
  {0x000000028F5F8402, 2652, 4},
  {0x000000028F5F8444, 2656, 4},
  {0x000000028F601063, 2660, 4},
  {0x000000028F6010A5, 2664, 4},
  {0x000000028F6241E7, 2668, 4},
  {0x000000028F624229, 2672, 4},
  {0x000000028F62CE48, 2676, 4},
  {0x000000028F62CE8A, 2680, 4},
  {0x0000000290551FF7, 2684, 4},
  {0x0000000290552018, 2688, 4},
  {0x0000000290575F46, 2692, 4},
  {0x0000000290575F88, 2696, 4},
  {0x000000029057EBA7, 2700, 4},
  {0x000000029057EBE9, 2704, 4},
  {0x0000000290587808, 2708, 4},
  {0x000000029058784A, 2712, 4},
  {0x00000002905AA98C, 2716, 4},
  {0x00000002905AA9CE, 2720, 4},
  {0x00000002905BC24E, 2724, 4},
  {0x00000002905BC290, 2728, 4},
  {0x00000002905C4EAF, 2732, 4},
  {0x00000002905C4EF1, 2736, 4},
  {0x00000002908A8FE2, 2740, 4},
  {0x00000002908AEAE4, 2744, 4},
  {0x00000002908AEB26, 2748, 4},
  {0x0000000290943D55, 2752, 4},
  {0x0000000290943D97, 2756, 4},
  {0x000000029094C9B6, 2760, 4},
  {0x000000029094C9F8, 2764, 4},
  {0x0000000290955617, 2768, 4},
  {0x0000000290955659, 2772, 4},
  {0x0000000290958398, 2776, 4},
  {0x0000000291EB8287, 2780, 4},
  {0x0000000291EB82C9, 2784, 4},
  {0x0000000291F3BC36, 2788, 4},
  {0x0000000291F3BC78, 2792, 4},
  {0x0000000291F4D4F8, 2796, 4},
  {0x0000000291F4D53A, 2800, 4},
  {0x0000000291F56159, 2804, 4},
  {0x0000000291F5619B, 2808, 4},
  {0x0000000291F5EDBA, 2812, 4},
  {0x0000000291F5EDFC, 2816, 4},
  {0x0000000291F792DD, 2820, 4},
  {0x0000000291F7931F, 2824, 4},
  {0x0000000291F81F3E, 2828, 4},
  {0x0000000291F81F80, 2832, 4},
  {0x0000000291F8AB9F, 2836, 4},
  {0x0000000291F8ABE1, 2840, 4},
  {0x00000002923B91D9, 2844, 4},
  {0x00000002923B921B, 2848, 4},
  {0x00000002923C1E3A, 2852, 4},
  {0x00000002923C1E7C, 2856, 4},
  {0x00000002924715CE, 2860, 4},
  {0x0000000292471610, 2864, 4},
  {0x000000029247A22F, 2868, 4},
  {0x000000029247A271, 2872, 4},
  {0x000000029247CFB0, 2876, 4},
  {0x0000000292482E90, 2880, 4},
  {0x0000000292482ED2, 2884, 4},
  {0x0000000292495247, 2888, 4},
  {0x0000000292495268, 2892, 4},
  {0x000000029249611A, 2896, 4},
  {0x00000002924AEC75, 2900, 4},
  {0x00000002924AECB7, 2904, 4},
  {0x00000002924C0537, 2908, 4},
  {0x00000002924C0579, 2912, 4},
  {0x00000002924C9198, 2916, 4},
  {0x00000002924C91DA, 2920, 4},
  {0x00000002924D1DF9, 2924, 4},
  {0x00000002924D1E3B, 2928, 4},
  {0x00000002924DAA5A, 2932, 4},
  {0x00000002924DAA9C, 2936, 4},
  {0x00000002924E36BB, 2940, 4},
  {0x00000002924E36FD, 2944, 4},
  {0x00000002924EC31C, 2948, 4},
  {0x00000002924EC35E, 2952, 4},
  {0x00000002924F4F7D, 2956, 4},
  {0x00000002924F4FBF, 2960, 4},
  {0x00000002924FDBDE, 2964, 4},
  {0x00000002924FDC20, 2968, 4},
  {0x000000029250683F, 2972, 4},
  {0x0000000292506881, 2976, 4},
  {0x000000029250F4A0, 2980, 4},
  {0x000000029250F4E2, 2984, 4},
  {0x0000000292C2351C, 2988, 4},
  {0x0000000297187E93, 2992, 4},
  {0x0000000297187EC8, 2996, 4},
  {0x0000000297187ED1, 3000, 4},
  {0x0000000297187EE8, 3004, 4},
  {0x0000000297187EF9, 3008, 4},
  {0x0000000297187EFD, 3012, 4},
  {0x0000000297187F40, 3016, 4},
  {0x0000000297187F42, 3020, 4},
  {0x0000000297188010, 3024, 4},
  {0x0000000297188011, 3028, 4},
  {0x000000029718807C, 3032, 4},
  {0x0000000297188081, 3036, 4},
  {0x00000002971881D5, 3040, 4},
  {0x00000002971881E7, 3044, 4},
  {0x00000002971882D4, 3048, 4},
  {0x0000000297188309, 3052, 4},
  {0x0000000297188312, 3056, 4},
  {0x0000000297188329, 3060, 4},
  {0x000000029718833A, 3064, 4},
  {0x000000029718833E, 3068, 4},
  {0x0000000297188381, 3072, 4},
  {0x0000000297188383, 3076, 4},
  {0x0000000297188451, 3080, 4},
  {0x0000000297188452, 3084, 4},
  {0x00000002971884BD, 3088, 4},
  {0x00000002971884C2, 3092, 4},
  {0x0000000297188504, 3096, 4},
  {0x0000000297188505, 3100, 4},
  {0x0000000297188506, 3104, 4},
  {0x0000000297188507, 3108, 4},
  {0x0000000297188508, 3112, 4},
  {0x0000000297188511, 3116, 4},
  {0x0000000297188512, 3120, 4},
  {0x0000000297188616, 3124, 4},
  {0x0000000297188628, 3128, 4},
  {0x0000000297188715, 3132, 4},
  {0x000000029718874A, 3136, 4},
  {0x0000000297188753, 3140, 4},
  {0x000000029718876A, 3144, 4},
  {0x000000029718877B, 3148, 4},
  {0x000000029718877F, 3152, 4},
  {0x00000002971887C2, 3156, 4},
  {0x00000002971887C4, 3160, 4},
  {0x0000000297188892, 3164, 4},
  {0x0000000297188893, 3168, 4},
  {0x00000002971888FE, 3172, 4},
  {0x0000000297188903, 3176, 4},
  {0x0000000297188945, 3180, 4},
  {0x0000000297188946, 3184, 4},
  {0x0000000297188947, 3188, 4},
  {0x0000000297188948, 3192, 4},
  {0x0000000297188949, 3196, 4},
  {0x0000000297188952, 3200, 4},
  {0x0000000297188953, 3204, 4},
  {0x0000000297188A57, 3208, 4},
  {0x0000000297188A69, 3212, 4},
  {0x0000000297188B56, 3216, 4},
  {0x0000000297188B8B, 3220, 4},
  {0x0000000297188B94, 3224, 4},
  {0x0000000297188BAB, 3228, 4},
  {0x0000000297188BBC, 3232, 4},
  {0x0000000297188BC0, 3236, 4},
  {0x0000000297188C03, 3240, 4},
  {0x0000000297188C05, 3244, 4},
  {0x0000000297188CD3, 3248, 4},
  {0x0000000297188CD4, 3252, 4},
  {0x0000000297188D3F, 3256, 4},
  {0x0000000297188D44, 3260, 4},
  {0x0000000297188D86, 3264, 4},
  {0x0000000297188D87, 3268, 4},
  {0x0000000297188D88, 3272, 4},
  {0x0000000297188D89, 3276, 4},
  {0x0000000297188D8A, 3280, 4},
  {0x0000000297188D93, 3284, 4},
  {0x0000000297188D94, 3288, 4},
  {0x0000000297188E98, 3292, 4},
  {0x0000000297188EAA, 3296, 4},
  {0x0000000297188F97, 3300, 4},
  {0x0000000297188FCC, 3304, 4},
  {0x0000000297188FD5, 3308, 4},
  {0x0000000297188FEC, 3312, 4},
  {0x0000000297188FFD, 3316, 4},
  {0x0000000297189001, 3320, 4},
  {0x0000000297189044, 3324, 4},
  {0x0000000297189046, 3328, 4},
  {0x0000000297189114, 3332, 4},
  {0x0000000297189115, 3336, 4},
  {0x0000000297189180, 3340, 4},
  {0x0000000297189185, 3344, 4},
  {0x00000002971891C7, 3348, 4},
  {0x00000002971891C8, 3352, 4},
  {0x00000002971891C9, 3356, 4},
  {0x00000002971891CA, 3360, 4},
  {0x00000002971891CB, 3364, 4},
  {0x00000002971891D4, 3368, 4},
  {0x00000002971891D5, 3372, 4},
  {0x00000002971892D9, 3376, 4},
  {0x00000002971892EB, 3380, 4},
  {0x0000000297189608, 3384, 4},
  {0x0000000297189609, 3388, 4},
  {0x000000029718960A, 3392, 4},
  {0x000000029718960B, 3396, 4},
  {0x000000029718960C, 3400, 4},
  {0x0000000297189615, 3404, 4},
  {0x0000000297189616, 3408, 4},
  {0x0000000297190AF4, 3412, 4},
  {0x0000000297190B29, 3416, 4},
  {0x0000000297190B32, 3420, 4},
  {0x0000000297190B49, 3424, 4},
  {0x0000000297190B5A, 3428, 4},
  {0x0000000297190B5E, 3432, 4},
  {0x0000000297190BA1, 3436, 4},
  {0x0000000297190BA3, 3440, 4},
  {0x0000000297190C71, 3444, 4},
  {0x0000000297190C72, 3448, 4},
  {0x0000000297190CDD, 3452, 4},
  {0x0000000297190CE2, 3456, 4},
  {0x0000000297190E36, 3460, 4},
  {0x0000000297190E48, 3464, 4},
  {0x0000000297190F35, 3468, 4},
  {0x0000000297190F6A, 3472, 4},
  {0x0000000297190F73, 3476, 4},
  {0x0000000297190F8A, 3480, 4},
  {0x0000000297190F9B, 3484, 4},
  {0x0000000297190F9F, 3488, 4},
  {0x0000000297190FE2, 3492, 4},
  {0x0000000297190FE4, 3496, 4},
  {0x00000002971910B2, 3500, 4},
  {0x00000002971910B3, 3504, 4},
  {0x000000029719111E, 3508, 4},
  {0x0000000297191123, 3512, 4},
  {0x0000000297191165, 3516, 4},
  {0x0000000297191166, 3520, 4},
  {0x0000000297191167, 3524, 4},
  {0x0000000297191168, 3528, 4},
  {0x0000000297191169, 3532, 4},
  {0x0000000297191172, 3536, 4},
  {0x0000000297191173, 3540, 4},
  {0x0000000297191277, 3544, 4},
  {0x0000000297191289, 3548, 4},
  {0x0000000297191376, 3552, 4},
  {0x00000002971913AB, 3556, 4},
  {0x00000002971913B4, 3560, 4},
  {0x00000002971913CB, 3564, 4},
  {0x00000002971913DC, 3568, 4},
  {0x00000002971913E0, 3572, 4},
  {0x0000000297191423, 3576, 4},
  {0x0000000297191425, 3580, 4},
  {0x00000002971914F3, 3584, 4},
  {0x00000002971914F4, 3588, 4},
  {0x000000029719155F, 3592, 4},
  {0x0000000297191564, 3596, 4},
  {0x00000002971915A6, 3600, 4},
  {0x00000002971915A7, 3604, 4},
  {0x00000002971915A8, 3608, 4},
  {0x00000002971915A9, 3612, 4},
  {0x00000002971915AA, 3616, 4},
  {0x00000002971915B3, 3620, 4},
  {0x00000002971915B4, 3624, 4},
  {0x00000002971916B8, 3628, 4},
  {0x00000002971916CA, 3632, 4},
  {0x00000002971917B7, 3636, 4},
  {0x00000002971917EC, 3640, 4},
  {0x00000002971917F5, 3644, 4},
  {0x000000029719180C, 3648, 4},
  {0x000000029719181D, 3652, 4},
  {0x0000000297191821, 3656, 4},
  {0x0000000297191864, 3660, 4},
  {0x0000000297191866, 3664, 4},
  {0x0000000297191934, 3668, 4},
  {0x0000000297191935, 3672, 4},
  {0x00000002971919A0, 3676, 4},
  {0x00000002971919A5, 3680, 4},
  {0x00000002971919E7, 3684, 4},
  {0x00000002971919E8, 3688, 4},
  {0x00000002971919E9, 3692, 4},
  {0x00000002971919EA, 3696, 4},
  {0x00000002971919EB, 3700, 4},
  {0x00000002971919F4, 3704, 4},
  {0x00000002971919F5, 3708, 4},
  {0x0000000297191AF9, 3712, 4},
  {0x0000000297191B0B, 3716, 4},
  {0x0000000297191BF8, 3720, 4},
  {0x0000000297191C2D, 3724, 4},
  {0x0000000297191C36, 3728, 4},
  {0x0000000297191C4D, 3732, 4},
  {0x0000000297191C5E, 3736, 4},
  {0x0000000297191C62, 3740, 4},
  {0x0000000297191CA5, 3744, 4},
  {0x0000000297191CA7, 3748, 4},
  {0x0000000297191D75, 3752, 4},
  {0x0000000297191D76, 3756, 4},
  {0x0000000297191DE1, 3760, 4},
  {0x0000000297191DE6, 3764, 4},
  {0x0000000297191E28, 3768, 4},
  {0x0000000297191E29, 3772, 4},
  {0x0000000297191E2A, 3776, 4},
  {0x0000000297191E2B, 3780, 4},
  {0x0000000297191E2C, 3784, 4},
  {0x0000000297191E35, 3788, 4},
  {0x0000000297191E36, 3792, 4},
  {0x0000000297191F3A, 3796, 4},
  {0x0000000297191F4C, 3800, 4},
  {0x0000000297192269, 3804, 4},
  {0x000000029719226A, 3808, 4},
  {0x000000029719226B, 3812, 4},
  {0x000000029719226C, 3816, 4},
  {0x000000029719226D, 3820, 4},
  {0x0000000297192276, 3824, 4},
  {0x0000000297192277, 3828, 4},
  {0x000000029A0FF75C, 3832, 4},
  {0x000000029A0FF791, 3836, 4},
  {0x000000029A0FF79A, 3840, 4},
  {0x000000029A0FF7A2, 3844, 4},
  {0x000000029A0FF7B1, 3848, 4},
  {0x000000029A0FF7C2, 3852, 4},
  {0x000000029A0FF7C6, 3856, 4},
  {0x000000029A0FF809, 3860, 4},
  {0x000000029A0FF80B, 3864, 4},
  {0x000000029A0FF8D9, 3868, 4},
  {0x000000029A0FF8DA, 3872, 4},
  {0x000000029A0FF945, 3876, 4},
  {0x000000029A0FF94A, 3880, 4},
  {0x000000029A0FFA9E, 3884, 4},
  {0x000000029A0FFAB0, 3888, 4},
  {0x000000029A0FFB9D, 3892, 4},
  {0x000000029A0FFBD2, 3896, 4},
  {0x000000029A0FFBDB, 3900, 4},
  {0x000000029A0FFBE3, 3904, 4},
  {0x000000029A0FFBF2, 3908, 4},
  {0x000000029A0FFC03, 3912, 4},
  {0x000000029A0FFC07, 3916, 4},
  {0x000000029A0FFC4A, 3920, 4},
  {0x000000029A0FFC4C, 3924, 4},
  {0x000000029A0FFD1A, 3928, 4},
  {0x000000029A0FFD1B, 3932, 4},
  {0x000000029A0FFD86, 3936, 4},
  {0x000000029A0FFD8B, 3940, 4},
  {0x000000029A0FFDCD, 3944, 4},
  {0x000000029A0FFDCE, 3948, 4},
  {0x000000029A0FFDCF, 3952, 4},
  {0x000000029A0FFDD0, 3956, 4},
  {0x000000029A0FFDD1, 3960, 4},
  {0x000000029A0FFDDA, 3964, 4},
  {0x000000029A0FFDDB, 3968, 4},
  {0x000000029A0FFE8E, 3972, 4},
  {0x000000029A0FFEDF, 3976, 4},
  {0x000000029A0FFEF1, 3980, 4},
  {0x000000029A0FFFDE, 3984, 4},
  {0x000000029A100013, 3988, 4},
  {0x000000029A10001C, 3992, 4},
  {0x000000029A100024, 3996, 4},
  {0x000000029A100033, 4000, 4},
  {0x000000029A100044, 4004, 4},
  {0x000000029A100048, 4008, 4},
  {0x000000029A10008B, 4012, 4},
  {0x000000029A10008D, 4016, 4},
  {0x000000029A10015B, 4020, 4},
  {0x000000029A10015C, 4024, 4},
  {0x000000029A1001C7, 4028, 4},
  {0x000000029A1001CC, 4032, 4},
  {0x000000029A10020E, 4036, 4},
  {0x000000029A10020F, 4040, 4},
  {0x000000029A100210, 4044, 4},
  {0x000000029A100211, 4048, 4},
  {0x000000029A100212, 4052, 4},
  {0x000000029A10021B, 4056, 4},
  {0x000000029A10021C, 4060, 4},
  {0x000000029A1002CF, 4064, 4},
  {0x000000029A100320, 4068, 4},
  {0x000000029A100332, 4072, 4},
  {0x000000029A10041F, 4076, 4},
  {0x000000029A100454, 4080, 4},
  {0x000000029A10045D, 4084, 4},
  {0x000000029A100465, 4088, 4},
  {0x000000029A100474, 4092, 4},
  {0x000000029A100485, 4096, 4},
  {0x000000029A100489, 4100, 4},
  {0x000000029A1004CC, 4104, 4},
  {0x000000029A1004CE, 4108, 4},
  {0x000000029A10059C, 4112, 4},
  {0x000000029A10059D, 4116, 4},
  {0x000000029A100608, 4120, 4},
  {0x000000029A10060D, 4124, 4},
  {0x000000029A10064F, 4128, 4},
  {0x000000029A100650, 4132, 4},
  {0x000000029A100651, 4136, 4},
  {0x000000029A100652, 4140, 4},
  {0x000000029A100653, 4144, 4},
  {0x000000029A10065C, 4148, 4},
  {0x000000029A10065D, 4152, 4},
  {0x000000029A100710, 4156, 4},
  {0x000000029A100761, 4160, 4},
  {0x000000029A100773, 4164, 4},
  {0x000000029A100860, 4168, 4},
  {0x000000029A100895, 4172, 4},
  {0x000000029A10089E, 4176, 4},
  {0x000000029A1008A6, 4180, 4},
  {0x000000029A1008B5, 4184, 4},
  {0x000000029A1008C6, 4188, 4},
  {0x000000029A1008CA, 4192, 4},
  {0x000000029A10090D, 4196, 4},
  {0x000000029A10090F, 4200, 4},
  {0x000000029A1009DD, 4204, 4},
  {0x000000029A1009DE, 4208, 4},
  {0x000000029A100A49, 4212, 4},
  {0x000000029A100A4E, 4216, 4},
  {0x000000029A100A90, 4220, 4},
  {0x000000029A100A91, 4224, 4},
  {0x000000029A100A92, 4228, 4},
  {0x000000029A100A93, 4232, 4},
  {0x000000029A100A94, 4236, 4},
  {0x000000029A100A9D, 4240, 4},
  {0x000000029A100A9E, 4244, 4},
  {0x000000029A100B51, 4248, 4},
  {0x000000029A100BA2, 4252, 4},
  {0x000000029A100BB4, 4256, 4},
  {0x000000029A100ED1, 4260, 4},
  {0x000000029A100ED2, 4264, 4},
  {0x000000029A100ED3, 4268, 4},
  {0x000000029A100ED4, 4272, 4},
  {0x000000029A100ED5, 4276, 4},
  {0x000000029A100EDE, 4280, 4},
  {0x000000029A100EDF, 4284, 4},
  {0x000000029A100F92, 4288, 4},
  {0x00000034551AB6E9, 4292, 5},
  {0x00000034551AB72B, 4297, 5},
  {0x00000034DBF4F555, 4302, 5},
  {0x00000034DBF4F597, 4307, 5},
  {0x00000034DBF581B6, 4312, 5},
  {0x00000034DBF581F8, 4317, 5},
  {0x00000034DBF60E17, 4322, 5},
  {0x00000034DBF60E59, 4327, 5},
  {0x00000034DBF69A78, 4332, 5},
  {0x00000034DBF69ABA, 4337, 5},
  {0x00000034DBF726D9, 4342, 5},
  {0x00000034DBF7271B, 4347, 5},
  {0x00000054811EE573, 4352, 5},
  {0x00000054811EE9B4, 4357, 5},
  {0x00000054811EE9B5, 4362, 5},
  {0x00000054811EEDF5, 4367, 5},
  {0x00000054811EEDF6, 4372, 5},
  {0x00000054811EF236, 4377, 5},
  {0x00000054811EF237, 4382, 5},
  {0x000000548130FDF4, 4387, 5},
  {0x0000005481310235, 4392, 5},
  {0x0000005481310236, 4397, 5},
  {0x0000005481310AB7, 4402, 5},
  {0x0000005481310AB8, 4407, 5},
  {0x00000054E3198B3E, 4412, 5},
  {0x00000054E31C88F3, 4417, 5},
  {0x00000054E31C8D34, 4422, 5},
  {0x00000054E7FA9CBB, 4427, 5},
  {0x00000054E7FA9CFD, 4432, 5},
  {0x00000054E80CB53C, 4437, 5},
  {0x00000054E80CB57E, 4442, 5},
  {0x00000054EACE7E2A, 4447, 5},
  {0x00000054F08DACD9, 4452, 5},
  {0x0000005506176365, 4457, 5},
  {0x00000055061763A7, 4462, 5},
  {0x000000556E44171F, 4467, 5},
  {0x000000556E441761, 4472, 5},
  {0x000000556E4444A0, 4477, 5},
  {0x000000556E44A380, 4482, 5},
  {0x000000556E44A3C2, 4487, 5},
  {0x000000556E44D101, 4492, 5},
  {0x000000556E452FE1, 4497, 5},
  {0x000000556E453023, 4502, 5},
  {0x000000556E455D62, 4507, 5},
  {0x000000556E45BC42, 4512, 5},
  {0x000000556E45BC84, 4517, 5},
  {0x000000556E45E9C3, 4522, 5},
  {0x000000556E4648A3, 4527, 5},
  {0x000000556E4648E5, 4532, 5},
  {0x000000556E467624, 4537, 5},
  {0x000000556E562FA0, 4542, 5},
  {0x000000556E562FE2, 4547, 5},
  {0x000000556E56BC01, 4552, 5},
  {0x000000556E56BC43, 4557, 5},
  {0x000000556E574862, 4562, 5},
  {0x000000556E5748A4, 4567, 5},
  {0x000000556E57D4C3, 4572, 5},
  {0x000000556E57D505, 4577, 5},
  {0x000000556E586124, 4582, 5},
  {0x000000556E586166, 4587, 5},
  {0x000000556EC2C2A6, 4592, 5},
  {0x000000556EC2C2E8, 4597, 5},
  {0x000000556EC34F07, 4602, 5},
  {0x000000556EC34F49, 4607, 5},
  {0x000000556EC3DB68, 4612, 5},
  {0x000000556EC3DBAA, 4617, 5},
  {0x000000556EC467C9, 4622, 5},
  {0x000000556EC4680B, 4627, 5},
  {0x000000556EC4F42A, 4632, 5},
  {0x000000556EC4F46C, 4637, 5},
  {0x000000556ED4DB27, 4642, 5},
  {0x000000556ED4DB69, 4647, 5},
  {0x000000556ED56788, 4652, 5},
  {0x000000556ED567CA, 4657, 5},
  {0x000000556ED5F3E9, 4662, 5},
  {0x000000556ED5F42B, 4667, 5},
  {0x000000556ED6804A, 4672, 5},
  {0x000000556ED6808C, 4677, 5},
  {0x000000556ED70CAB, 4682, 5},
  {0x000000556ED70CED, 4687, 5},
  {0x000000556EE6F3A8, 4692, 5},
  {0x000000556EE6F3EA, 4697, 5},
  {0x000000556EE78009, 4702, 5},
  {0x000000556EE7804B, 4707, 5},
  {0x000000556EE80C6A, 4712, 5},
  {0x000000556EE80CAC, 4717, 5},
  {0x000000556EE898CB, 4722, 5},
  {0x000000556EE8990D, 4727, 5},
  {0x000000556EE9252C, 4732, 5},
  {0x000000556EE9256E, 4737, 5},
  {0x0000005579EDED39, 4742, 5},
  {0x0000005579EDED5A, 4747, 5},
  {0x0000005579EDFC0C, 4752, 5},
  {0x0000005579EE799A, 4757, 5},
  {0x0000005579EE79BB, 4762, 5},
  {0x0000005579EE886D, 4767, 5},
  {0x0000005579EF05FB, 4772, 5},
  {0x0000005579EF061C, 4777, 5},
  {0x0000005579EF14CE, 4782, 5},
  {0x0000005579EF925C, 4787, 5},
  {0x0000005579EF927D, 4792, 5},
  {0x0000005579EFA12F, 4797, 5},
  {0x0000005579F01EBD, 4802, 5},
  {0x0000005579F01EDE, 4807, 5},
  {0x0000005579F02D90, 4812, 5},
  {0x000000557A0005BA, 4817, 5},
  {0x000000557A0005DB, 4822, 5},
  {0x000000557A00148D, 4827, 5},
  {0x000000557A00921B, 4832, 5},
  {0x000000557A00923C, 4837, 5},
  {0x000000557A00A0EE, 4842, 5},
  {0x000000557A011E7C, 4847, 5},
  {0x000000557A011E9D, 4852, 5},
  {0x000000557A012D4F, 4857, 5},
  {0x000000557A01AADD, 4862, 5},
  {0x000000557A01AAFE, 4867, 5},
  {0x000000557A01B9B0, 4872, 5},
  {0x000000557A02373E, 4877, 5},
  {0x000000557A02375F, 4882, 5},
  {0x000000557A024611, 4887, 5},
  {0x000000557A5A754A, 4892, 5},
  {0x000000557A5A758C, 4897, 5},
  {0x000000557A5B01AB, 4902, 5},
  {0x000000557A5B01ED, 4907, 5},
  {0x000000557A5B8E0C, 4912, 5},
  {0x000000557A5B8E4E, 4917, 5},
  {0x000000557A5C1A6D, 4922, 5},
  {0x000000557A5C1AAF, 4927, 5},
  {0x000000557A5CA6CE, 4932, 5},
  {0x000000557A5CA710, 4937, 5},
  {0x000000557A6C8DCB, 4942, 5},
  {0x000000557A6C8E0D, 4947, 5},
  {0x000000557A6D1A2C, 4952, 5},
  {0x000000557A6D1A6E, 4957, 5},
  {0x000000557A6DA68D, 4962, 5},
  {0x000000557A6DA6CF, 4967, 5},
  {0x000000557A6E32EE, 4972, 5},
  {0x000000557A6E3330, 4977, 5},
  {0x000000557A6EBF4F, 4982, 5},
  {0x000000557A6EBF91, 4987, 5},
  {0x000000557A7EA64C, 4992, 5},
  {0x000000557A7EA68E, 4997, 5},
  {0x000000557A7F32AD, 5002, 5},
  {0x000000557A7F32EF, 5007, 5},
  {0x000000557A7FBF0E, 5012, 5},
  {0x000000557A7FBF50, 5017, 5},
  {0x000000557A804B6F, 5022, 5},
  {0x000000557A804BB1, 5027, 5},
  {0x000000557A80D7D0, 5032, 5},
  {0x000000557A80D812, 5037, 5},
  {0x000000557A90BECD, 5042, 5},
  {0x000000557A90BF0F, 5047, 5},
  {0x000000557A914B2E, 5052, 5},
  {0x000000557A914B70, 5057, 5},
  {0x000000557A91D78F, 5062, 5},
  {0x000000557A91D7D1, 5067, 5},
  {0x000000557A9263F0, 5072, 5},
  {0x000000557A926432, 5077, 5},
  {0x000000557A92F051, 5082, 5},
  {0x000000557A92F093, 5087, 5},
  {0x000000557AB4EFCF, 5092, 5},
  {0x000000557AB4F011, 5097, 5},
  {0x000000557AB57C30, 5102, 5},
  {0x000000557AB57C72, 5107, 5},
  {0x000000557AB60891, 5112, 5},
  {0x000000557AB608D3, 5117, 5},
  {0x000000557AB694F2, 5122, 5},
  {0x000000557AB69534, 5127, 5},
  {0x000000557AB72153, 5132, 5},
  {0x000000557AB72195, 5137, 5},
  {0x000000557AFD51D3, 5142, 5},
  {0x000000557AFD5215, 5147, 5},
  {0x000000557AFDDE34, 5152, 5},
  {0x000000557AFDDE76, 5157, 5},
  {0x000000557AFE6A95, 5162, 5},
  {0x000000557AFE6AD7, 5167, 5},
  {0x000000557AFEF6F6, 5172, 5},
  {0x000000557AFEF738, 5177, 5},
  {0x000000557AFF8357, 5182, 5},
  {0x000000557AFF8399, 5187, 5},
  {0x000000557BB246DD, 5192, 5},
  {0x000000557BB2471F, 5197, 5},
  {0x000000557BB2D33E, 5202, 5},
  {0x000000557BB2D380, 5207, 5},
  {0x000000557BB35F9F, 5212, 5},
  {0x000000557BB35FE1, 5217, 5},
  {0x000000557BB3EC00, 5222, 5},
  {0x000000557BB3EC42, 5227, 5},
  {0x000000557BB47861, 5232, 5},
  {0x000000557BB478A3, 5237, 5},
  {0x000000557BC45F5E, 5242, 5},
  {0x000000557BC45FA0, 5247, 5},
  {0x000000557BC4EBBF, 5252, 5},
  {0x000000557BC4EC01, 5257, 5},
  {0x000000557BC57820, 5262, 5},
  {0x000000557BC57862, 5267, 5},
  {0x000000557BC60481, 5272, 5},
  {0x000000557BC604C3, 5277, 5},
  {0x000000557BC690E2, 5282, 5},
  {0x000000557BC69124, 5287, 5},
  {0x000000557C0CC162, 5292, 5},
  {0x000000557C0CC1A4, 5297, 5},
  {0x000000557C0D4DC3, 5302, 5},
  {0x000000557C0D4E05, 5307, 5},
  {0x000000557C0DDA24, 5312, 5},
  {0x000000557C0DDA66, 5317, 5},
  {0x000000557C0E6685, 5322, 5},
  {0x000000557C0E66C7, 5327, 5},
  {0x000000557C0EF2E6, 5332, 5},
  {0x000000557C0EF328, 5337, 5},
  {0x000000557C1ED9E3, 5342, 5},
  {0x000000557C1EDA25, 5347, 5},
  {0x000000557C1F6644, 5352, 5},
  {0x000000557C1F6686, 5357, 5},
  {0x000000557C1FF2A5, 5362, 5},
  {0x000000557C1FF2E7, 5367, 5},
  {0x000000557C207F06, 5372, 5},
  {0x000000557C207F48, 5377, 5},
  {0x000000557C210B67, 5382, 5},
  {0x000000557C210BA9, 5387, 5},
  {0x000000558CF1A1D1, 5392, 5},
  {0x000000558CF1A213, 5397, 5},
  {0x000000558CF22E32, 5402, 5},
  {0x000000558CF22E74, 5407, 5},
  {0x000000558CF2BA93, 5412, 5},
  {0x000000558CF2BAD5, 5417, 5},
  {0x000000558CF346F4, 5422, 5},
  {0x000000558CF34736, 5427, 5},
  {0x000000558CF3D355, 5432, 5},
  {0x000000558CF3D397, 5437, 5},
  {0x000000559BA58AA1, 5442, 5},
  {0x000000559BA58AE3, 5447, 5},
  {0x000000559BA61702, 5452, 5},
  {0x000000559BA61744, 5457, 5},
  {0x000000559BA6A363, 5462, 5},
  {0x000000559BA6A3A5, 5467, 5},
  {0x000000559BA72FC4, 5472, 5},
  {0x000000559BA73006, 5477, 5},
  {0x000000559BA7BC25, 5482, 5},
  {0x000000559BA7BC67, 5487, 5},
  {0x000000559BB7A322, 5492, 5},
  {0x000000559BB7A364, 5497, 5},
  {0x000000559BB82F83, 5502, 5},
  {0x000000559BB82FC5, 5507, 5},
  {0x000000559BB8BBE4, 5512, 5},
  {0x000000559BB8BC26, 5517, 5},
  {0x000000559BB94845, 5522, 5},
  {0x000000559BB94887, 5527, 5},
  {0x000000559BB9D4A6, 5532, 5},
  {0x000000559BB9D4E8, 5537, 5},
  {0x000000559BC9BBA3, 5542, 5},
  {0x000000559BC9BBE5, 5547, 5},
  {0x000000559BCA4804, 5552, 5},
  {0x000000559BCA4846, 5557, 5},
  {0x000000559BCAD465, 5562, 5},
  {0x000000559BCAD4A7, 5567, 5},
  {0x000000559BCB60C6, 5572, 5},
  {0x000000559BCB6108, 5577, 5},
  {0x000000559BCBED27, 5582, 5},
  {0x000000559BCBED69, 5587, 5},
  {0x000000559C121DA7, 5592, 5},
  {0x000000559C121DE9, 5597, 5},
  {0x000000559C12AA08, 5602, 5},
  {0x000000559C12AA4A, 5607, 5},
  {0x000000559C133669, 5612, 5},
  {0x000000559C1336AB, 5617, 5},
  {0x000000559C13C2CA, 5622, 5},
  {0x000000559C13C30C, 5627, 5},
  {0x000000559C144F2B, 5632, 5},
  {0x000000559C144F6D, 5637, 5},
  {0x000000559C364EA9, 5642, 5},
  {0x000000559C364EEB, 5647, 5},
  {0x000000559C36DB0A, 5652, 5},
  {0x000000559C36DB4C, 5657, 5},
  {0x000000559C37676B, 5662, 5},
  {0x000000559C3767AD, 5667, 5},
  {0x000000559C37F3CC, 5672, 5},
  {0x000000559C37F40E, 5677, 5},
  {0x000000559C38802D, 5682, 5},
  {0x000000559C38806F, 5687, 5},
  {0x000000559C48672A, 5692, 5},
  {0x000000559C48676C, 5697, 5},
  {0x000000559C48F38B, 5702, 5},
  {0x000000559C48F3CD, 5707, 5},
  {0x000000559C497FEC, 5712, 5},
  {0x000000559C49802E, 5717, 5},
  {0x000000559C4A0C4D, 5722, 5},
  {0x000000559C4A0C8F, 5727, 5},
  {0x000000559C4A98AE, 5732, 5},
  {0x000000559C4A98F0, 5737, 5},
  {0x00000055A24A89FF, 5742, 5},
  {0x00000055A24A8A41, 5747, 5},
  {0x00000055A24B1660, 5752, 5},
  {0x00000055A24B16A2, 5757, 5},
  {0x00000055A24BA2C1, 5762, 5},
  {0x00000055A24BA303, 5767, 5},
  {0x00000055A24C2F22, 5772, 5},
  {0x00000055A24C2F64, 5777, 5},
  {0x00000055A24CBB83, 5782, 5},
  {0x00000055A24CBBC5, 5787, 5},
  {0x00000055A37E2A90, 5792, 5},
  {0x00000055A37E2AD2, 5797, 5},
  {0x00000055A37EB6F1, 5802, 5},
  {0x00000055A37EB733, 5807, 5},
  {0x00000055A37F4352, 5812, 5},
  {0x00000055A37F4394, 5817, 5},
  {0x00000055A37FCFB3, 5822, 5},
  {0x00000055A37FCFF5, 5827, 5},
  {0x00000055A3805C14, 5832, 5},
  {0x00000055A3805C56, 5837, 5},
  {0x00000055A3904311, 5842, 5},
  {0x00000055A3904353, 5847, 5},
  {0x00000055A390CF72, 5852, 5},
  {0x00000055A390CFB4, 5857, 5},
  {0x00000055A3915BD3, 5862, 5},
  {0x00000055A3915C15, 5867, 5},
  {0x00000055A391E834, 5872, 5},
  {0x00000055A391E876, 5877, 5},
  {0x00000055A3927495, 5882, 5},
  {0x00000055A39274D7, 5887, 5},
  {0x00000055A3A25B92, 5892, 5},
  {0x00000055A3A25BD4, 5897, 5},
  {0x00000055A3A28913, 5902, 5},
  {0x00000055A3A2E7F3, 5907, 5},
  {0x00000055A3A2E835, 5912, 5},
  {0x00000055A3A31574, 5917, 5},
  {0x00000055A3A37454, 5922, 5},
  {0x00000055A3A37496, 5927, 5},
  {0x00000055A3A3A1D5, 5932, 5},
  {0x00000055A3A400B5, 5937, 5},
  {0x00000055A3A400F7, 5942, 5},
  {0x00000055A3A42E36, 5947, 5},
  {0x00000055A3A48D16, 5952, 5},
  {0x00000055A3A48D58, 5957, 5},
  {0x00000055A3A4BA97, 5962, 5},
  {0x00000055CFBE1602, 5967, 5},
  {0x00000055CFBE1644, 5972, 5},
  {0x00000055CFBEA263, 5977, 5},
  {0x00000055CFBEA2A5, 5982, 5},
  {0x00000055CFBF2EC4, 5987, 5},
  {0x00000055CFBF2F06, 5992, 5},
  {0x00000055CFBFBB25, 5997, 5},
  {0x00000055CFBFBB67, 6002, 5},
  {0x00000055CFC04786, 6007, 5},
  {0x00000055CFC047C8, 6012, 5},
  {0x00000055D0CD8591, 6017, 5},
  {0x00000055D0CD85D3, 6022, 5},
  {0x00000055D0CE11F2, 6027, 5},
  {0x00000055D0CE1234, 6032, 5},
  {0x00000055D0CE9E53, 6037, 5},
  {0x00000055D0CE9E95, 6042, 5},
  {0x00000055D0CF2AB4, 6047, 5},
  {0x00000055D0CF2AF6, 6052, 5},
  {0x00000055D0CFB715, 6057, 5},
  {0x00000055D0CFB757, 6062, 5},
  {0x00000055D0F1B693, 6067, 5},
  {0x00000055D0F1B6D5, 6072, 5},
  {0x00000055D0F242F4, 6077, 5},
  {0x00000055D0F24336, 6082, 5},
  {0x00000055D0F2CF55, 6087, 5},
  {0x00000055D0F2CF97, 6092, 5},
  {0x00000055D0F35BB6, 6097, 5},
  {0x00000055D0F35BF8, 6102, 5},
  {0x00000055D0F3E817, 6107, 5},
  {0x00000055D0F3E859, 6112, 5},
  {0x00000055D103CF14, 6117, 5},
  {0x00000055D103CF56, 6122, 5},
  {0x00000055D1045B75, 6127, 5},
  {0x00000055D1045BB7, 6132, 5},
  {0x00000055D104E7D6, 6137, 5},
  {0x00000055D104E818, 6142, 5},
  {0x00000055D1057437, 6147, 5},
  {0x00000055D1057479, 6152, 5},
  {0x00000055D1060098, 6157, 5},
  {0x00000055D10600DA, 6162, 5},
  {0x00000055D115E795, 6167, 5},
  {0x00000055D115E7D7, 6172, 5},
  {0x00000055D11673F6, 6177, 5},
  {0x00000055D1167438, 6182, 5},
  {0x00000055D1170057, 6187, 5},
  {0x00000055D1170099, 6192, 5},
  {0x00000055D1178CB8, 6197, 5},
  {0x00000055D1178CFA, 6202, 5},
  {0x00000055D1181919, 6207, 5},
  {0x00000055D118195B, 6212, 5},
  {0x00000055D14C3118, 6217, 5},
  {0x00000055D14C315A, 6222, 5},
  {0x00000055D14CBD79, 6227, 5},
  {0x00000055D14CBDBB, 6232, 5},
  {0x00000055D14D49DA, 6237, 5},
  {0x00000055D14D4A1C, 6242, 5},
  {0x00000055D14DD63B, 6247, 5},
  {0x00000055D14DD67D, 6252, 5},
  {0x00000055D14E629C, 6257, 5},
  {0x00000055D14E62DE, 6262, 5},
  {0x00000055D15E4999, 6267, 5},
  {0x00000055D15E49DB, 6272, 5},
  {0x00000055D15ED5FA, 6277, 5},
  {0x00000055D15ED63C, 6282, 5},
  {0x00000055D15F625B, 6287, 5},
  {0x00000055D15F629D, 6292, 5},
  {0x00000055D15FEEBC, 6297, 5},
  {0x00000055D15FEEFE, 6302, 5},
  {0x00000055D1607B1D, 6307, 5},
  {0x00000055D1607B5F, 6312, 5},
  {0x00000055D170621A, 6317, 5},
  {0x00000055D170625C, 6322, 5},
  {0x00000055D170EE7B, 6327, 5},
  {0x00000055D170EEBD, 6332, 5},
  {0x00000055D1717ADC, 6337, 5},
  {0x00000055D1717B1E, 6342, 5},
  {0x00000055D172073D, 6347, 5},
  {0x00000055D172077F, 6352, 5},
  {0x00000055D172939E, 6357, 5},
  {0x00000055D17293E0, 6362, 5},
  {0x00000055DA100F94, 6367, 5},
  {0x00000055DA100FD6, 6372, 5},
  {0x00000055DA109BF5, 6377, 5},
  {0x00000055DA109C37, 6382, 5},
  {0x00000055DA112856, 6387, 5},
  {0x00000055DA112898, 6392, 5},
  {0x00000055DA11B4B7, 6397, 5},
  {0x00000055DA11B4F9, 6402, 5},
  {0x00000055DA124118, 6407, 5},
  {0x00000055DA12415A, 6412, 5},
  {0x00000055DA222815, 6417, 5},
  {0x00000055DA222857, 6422, 5},
  {0x00000055DA22B476, 6427, 5},
  {0x00000055DA22B4B8, 6432, 5},
  {0x00000055DA2340D7, 6437, 5},
  {0x00000055DA234119, 6442, 5},
  {0x00000055DA23CD38, 6447, 5},
  {0x00000055DA23CD7A, 6452, 5},
  {0x00000055DA245999, 6457, 5},
  {0x00000055DA2459DB, 6462, 5},
  {0x00000055DB8C1229, 6467, 5},
  {0x00000055DB8C126B, 6472, 5},
  {0x00000055DB8C9E8A, 6477, 5},
  {0x00000055DB8C9ECC, 6482, 5},
  {0x00000055DB8D2AEB, 6487, 5},
  {0x00000055DB8D2B2D, 6492, 5},
  {0x00000055DB8DB74C, 6497, 5},
  {0x00000055DB8DB78E, 6502, 5},
  {0x00000055DB8E43AD, 6507, 5},
  {0x00000055DB8E43EF, 6512, 5},
  {0x00000055DB9E2AAA, 6517, 5},
  {0x00000055DB9E2AEC, 6522, 5},
  {0x00000055DB9E582B, 6527, 5},
  {0x00000055DB9EB70B, 6532, 5},
  {0x00000055DB9EB74D, 6537, 5},
  {0x00000055DB9EE48C, 6542, 5},
  {0x00000055DB9F436C, 6547, 5},
  {0x00000055DB9F43AE, 6552, 5},
  {0x00000055DB9F70ED, 6557, 5},
  {0x00000055DB9FCFCD, 6562, 5},
  {0x00000055DB9FD00F, 6567, 5},
  {0x00000055DB9FFD4E, 6572, 5},
  {0x00000055DBA05C2E, 6577, 5},
  {0x00000055DBA05C70, 6582, 5},
  {0x00000055DBA089AF, 6587, 5},
  {0x00000055DBB0432B, 6592, 5},
  {0x00000055DBB0436D, 6597, 5},
  {0x00000055DBB0CF8C, 6602, 5},
  {0x00000055DBB0CFCE, 6607, 5},
  {0x00000055DBB15BED, 6612, 5},
  {0x00000055DBB15C2F, 6617, 5},
  {0x00000055DBB1E84E, 6622, 5},
  {0x00000055DBB1E890, 6627, 5},
  {0x00000055DBB274AF, 6632, 5},
  {0x00000055DBB274F1, 6637, 5},
  {0x00000055DBD47F22, 6642, 5},
  {0x00000055DBD47F43, 6647, 5},
  {0x00000055DBD48DF5, 6652, 5},
  {0x00000055DBD50B83, 6657, 5},
  {0x00000055DBD50BA4, 6662, 5},
  {0x00000055DBD51A56, 6667, 5},
  {0x00000055DBD597E4, 6672, 5},
  {0x00000055DBD59805, 6677, 5},
  {0x00000055DBD5A6B7, 6682, 5},
  {0x00000055DBD62445, 6687, 5},
  {0x00000055DBD62466, 6692, 5},
  {0x00000055DBD63318, 6697, 5},
  {0x00000055DBD6B0A6, 6702, 5},
  {0x00000055DBD6B0C7, 6707, 5},
  {0x00000055DBD6BF79, 6712, 5},
  {0x00000055DC0ABDB0, 6717, 5},
  {0x00000055DC0ABDF2, 6722, 5},
  {0x00000055DC0B4A11, 6727, 5},
  {0x00000055DC0B4A53, 6732, 5},
  {0x00000055DC0BD672, 6737, 5},
  {0x00000055DC0BD6B4, 6742, 5},
  {0x00000055DC0C62D3, 6747, 5},
  {0x00000055DC0C6315, 6752, 5},
  {0x00000055DC0CEF34, 6757, 5},
  {0x00000055DC0CEF76, 6762, 5},
  {0x00000055DC2EEEB2, 6767, 5},
  {0x00000055DC2EEEF4, 6772, 5},
  {0x00000055DC2F7B13, 6777, 5},
  {0x00000055DC2F7B55, 6782, 5},
  {0x00000055DC300774, 6787, 5},
  {0x00000055DC3007B6, 6792, 5},
  {0x00000055DC3093D5, 6797, 5},
  {0x00000055DC309417, 6802, 5},
  {0x00000055DC312036, 6807, 5},
  {0x00000055DC312078, 6812, 5},
  {0x00000055DC410733, 6817, 5},
  {0x00000055DC410775, 6822, 5},
  {0x00000055DC419394, 6827, 5},
  {0x00000055DC4193D6, 6832, 5},
  {0x00000055DC421FF5, 6837, 5},
  {0x00000055DC422037, 6842, 5},
  {0x00000055DC42AC56, 6847, 5},
  {0x00000055DC42AC98, 6852, 5},
  {0x00000055DC4338B7, 6857, 5},
  {0x00000055DC4338F9, 6862, 5},
  {0x00000055DC531FB4, 6867, 5},
  {0x00000055DC531FF6, 6872, 5},
  {0x00000055DC53AC15, 6877, 5},
  {0x00000055DC53AC57, 6882, 5},
  {0x00000055DC543876, 6887, 5},
  {0x00000055DC5438B8, 6892, 5},
  {0x00000055DC54C4D7, 6897, 5},
  {0x00000055DC54C519, 6902, 5},
  {0x00000055DC555138, 6907, 5},
  {0x00000055DC55517A, 6912, 5},
  {0x00000055DC653835, 6917, 5},
  {0x00000055DC653877, 6922, 5},
  {0x00000055DC65C496, 6927, 5},
  {0x00000055DC65C4D8, 6932, 5},
  {0x00000055DC6650F7, 6937, 5},
  {0x00000055DC665139, 6942, 5},
  {0x00000055DC66DD58, 6947, 5},
  {0x00000055DC66DD9A, 6952, 5},
  {0x00000055DC6769B9, 6957, 5},
  {0x00000055DC6769FB, 6962, 5},
  {0x00000055DC7750B6, 6967, 5},
  {0x00000055DC7750F8, 6972, 5},
  {0x00000055DC77DD17, 6977, 5},
  {0x00000055DC77DD59, 6982, 5},
  {0x00000055DC786978, 6987, 5},
  {0x00000055DC7869BA, 6992, 5},
  {0x00000055DC78F5D9, 6997, 5},
  {0x00000055DC78F61B, 7002, 5},
  {0x00000055DC79823A, 7007, 5},
  {0x00000055DC79827C, 7012, 5},
  {0x00000055DC896937, 7017, 5},
  {0x00000055DC896979, 7022, 5},
  {0x00000055DC89F598, 7027, 5},
  {0x00000055DC89F5DA, 7032, 5},
  {0x00000055DC8A81F9, 7037, 5},
  {0x00000055DC8A823B, 7042, 5},
  {0x00000055DC8B0E5A, 7047, 5},
  {0x00000055DC8B0E9C, 7052, 5},
  {0x00000055DC8B9ABB, 7057, 5},
  {0x00000055DC8B9AFD, 7062, 5},
  {0x00000055DC9B81B8, 7067, 5},
  {0x00000055DC9B81FA, 7072, 5},
  {0x00000055DC9C0E19, 7077, 5},
  {0x00000055DC9C0E5B, 7082, 5},
  {0x00000055DC9C9A7A, 7087, 5},
  {0x00000055DC9C9ABC, 7092, 5},
  {0x00000055DC9D26DB, 7097, 5},
  {0x00000055DC9D271D, 7102, 5},
  {0x00000055DC9DB33C, 7107, 5},
  {0x00000055DC9DB37E, 7112, 5},
  {0x00000055DCAD9A39, 7117, 5},
  {0x00000055DCAD9A7B, 7122, 5},
  {0x00000055DCAE269A, 7127, 5},
  {0x00000055DCAE26DC, 7132, 5},
  {0x00000055DCAEB2FB, 7137, 5},
  {0x00000055DCAEB33D, 7142, 5},
  {0x00000055DCAF3F5C, 7147, 5},
  {0x00000055DCAF3F9E, 7152, 5},
  {0x00000055DCAFCBBD, 7157, 5},
  {0x00000055DCAFCBFF, 7162, 5},
  {0x00000055F06D662C, 7167, 5},
  {0x00000055F06D662D, 7172, 5},
  {0x00000055F06D662E, 7177, 5},
  {0x00000055F06D662F, 7182, 5},
  {0x00000055F06DF28C, 7187, 5},
  {0x00000055F06DF28E, 7192, 5},
  {0x00000055F06DF28F, 7197, 5},
  {0x00000055F06DF290, 7202, 5},
  {0x00000055F06E7EED, 7207, 5},
  {0x00000055F06E7EEE, 7212, 5},
  {0x00000055F06E7EF0, 7217, 5},
  {0x00000055F06E7EF1, 7222, 5},
  {0x00000055F06F0B4E, 7227, 5},
  {0x00000055F06F0B4F, 7232, 5},
  {0x00000055F06F0B50, 7237, 5},
  {0x00000055F06F0B52, 7242, 5},
  {0x00000055F06F97AF, 7247, 5},
  {0x00000055F06F97B0, 7252, 5},
  {0x00000055F06F97B1, 7257, 5},
  {0x00000055F06F97B2, 7262, 5},
  {0x00000AE3ABA5C982, 7267, 6},
  {0x00000AE3ADFAF223, 7273, 6},
  {0x00000AE3ADFAF224, 7279, 6},
  {0x00000AE4A7A71926, 7285, 6},
  {0x00000AE4A7AE3A13, 7291, 6},
  {0x00000AE4A7AEC674, 7297, 6},
  {0x00000AE4A9FC41C7, 7303, 6},
  {0x00000AE4AA0362B4, 7309, 6},
  {0x00000AE4AA03EF15, 7315, 6},
  {0x00000AF1465FE82F, 7321, 6},
  {0x00000AF14667091C, 7327, 6},
  {0x00000AF14667957D, 7333, 6},
  {0x000167276E3776A1, 7339, 7},
  {0x000167276E5BA7A3, 7346, 7},
  {0x0001677944985548, 7353, 7},
  {0x0001677944985989, 7360, 7},
  {0x000167794498598A, 7367, 7},
  {0x0001677944AA6DC9, 7374, 7},
  {0x0001677944AA720A, 7381, 7},
  {0x0001677944AA720B, 7388, 7},
  {0x0001677991A4AA8A, 7395, 7},
  {0x0001677991A4AECB, 7402, 7},
  {0x0001677991A4AECC, 7409, 7},
  {0x0001680A7438A3D4, 7416, 7},
  {0x0001680A745CD4D6, 7423, 7},
  {0x000168F89A6498EC, 7430, 7},
  {0x000168F89A88C9EE, 7437, 7},
  {0x0001691A1C68AD32, 7444, 7},
  {0x00016B9CA6CFDA16, 7451, 7},
  {0x00016B9CA6CFDA17, 7458, 7},
  {0x00016B9CA6CFDA18, 7465, 7},
  {0x00016B9CA6CFDA19, 7472, 7},
  {0x00016B9CA92502B6, 7479, 7},
  {0x00016B9CA92502B8, 7486, 7},
  {0x00016B9CA92502B9, 7493, 7},
  {0x00016B9CA92502BA, 7500, 7},
  {0x00016B9CA98353E3, 7507, 7},
  {0x00016B9CA98353E4, 7514, 7},
  {0x00016B9CA98353E5, 7521, 7},
  {0x00016B9CA98353E6, 7528, 7},
  {0x00016B9CA99707A1, 7535, 7},
  {0x00016B9CA99E288E, 7542, 7},
  {0x00016B9CA99EB4EF, 7549, 7},
  {0x00016B9CAA82DC75, 7556, 7},
  {0x00016B9CAA82DC76, 7563, 7},
  {0x00016B9CAA82DC77, 7570, 7},
  {0x00016B9CAA82DC78, 7577, 7},
  {0x00016B9CAB7A2B57, 7584, 7},
  {0x00016B9CAB7A2B58, 7591, 7},
  {0x00016B9CAB7A2B5A, 7598, 7},
  {0x00016B9CAB7A2B5B, 7605, 7},
  {0x00016B9CABD87C83, 7612, 7},
  {0x00016B9CABD87C85, 7619, 7},
  {0x00016B9CABD87C86, 7626, 7},
  {0x00016B9CABD87C87, 7633, 7},
  {0x00016B9CABEC3042, 7640, 7},
  {0x00016B9CABF3512F, 7647, 7},
  {0x00016B9CABF3DD90, 7654, 7},
  {0x00016B9CACD80515, 7661, 7},
  {0x00016B9CACD80517, 7668, 7},
  {0x00016B9CACD80518, 7675, 7},
  {0x00016B9CACD80519, 7682, 7},
  {0x00016B9CADCF53F8, 7689, 7},
  {0x00016B9CADCF53F9, 7696, 7},
  {0x00016B9CADCF53FA, 7703, 7},
  {0x00016B9CADCF53FC, 7710, 7},
  {0x00016B9CAE2DA524, 7717, 7},
  {0x00016B9CAE2DA525, 7724, 7},
  {0x00016B9CAE2DA527, 7731, 7},
  {0x00016B9CAE2DA528, 7738, 7},
  {0x00016B9CAE4158E3, 7745, 7},
  {0x00016B9CAE4879D0, 7752, 7},
  {0x00016B9CAE490631, 7759, 7},
  {0x00016B9CAF2D2DB6, 7766, 7},
  {0x00016B9CAF2D2DB7, 7773, 7},
  {0x00016B9CAF2D2DB9, 7780, 7},
  {0x00016B9CAF2D2DBA, 7787, 7},
  {0x00016B9CB0247C99, 7794, 7},
  {0x00016B9CB0247C9A, 7801, 7},
  {0x00016B9CB0247C9B, 7808, 7},
  {0x00016B9CB0247C9C, 7815, 7},
  {0x00016B9CB082CDC5, 7822, 7},
  {0x00016B9CB082CDC6, 7829, 7},
  {0x00016B9CB082CDC7, 7836, 7},
  {0x00016B9CB082CDC9, 7843, 7},
  {0x00016B9CB0968184, 7850, 7},
  {0x00016B9CB09DA271, 7857, 7},
  {0x00016B9CB09E2ED2, 7864, 7},
  {0x00016B9CB1825657, 7871, 7},
  {0x00016B9CB1825658, 7878, 7},
  {0x00016B9CB1825659, 7885, 7},
  {0x00016B9CB182565B, 7892, 7},
  {0x00016B9CB2D7F666, 7899, 7},
  {0x00016B9CB2D7F667, 7906, 7},
  {0x00016B9CB2D7F668, 7913, 7},
  {0x00016B9CB2D7F669, 7920, 7},
  {0x00016B9CB2EBAA25, 7927, 7},
  {0x00016B9CB2F2CB12, 7934, 7},
  {0x00016B9CB2F35773, 7941, 7},
  {0x00016B9CB3D77EF8, 7948, 7},
  {0x00016B9CB3D77EF9, 7955, 7},
  {0x00016B9CB3D77EFA, 7962, 7},
  {0x00016B9CB3D77EFB, 7969, 7},
  {0x00016B9CF3CA16F8, 7976, 7},
  {0x00016B9CF3CA16F9, 7983, 7},
  {0x00016B9CF3CA16FA, 7990, 7},
  {0x00016B9CF3CA16FB, 7997, 7},
  {0x00016B9CF61F3F98, 8004, 7},
  {0x00016B9CF61F3F9A, 8011, 7},
  {0x00016B9CF61F3F9B, 8018, 7},
  {0x00016B9CF61F3F9C, 8025, 7},
  {0x00016B9CF67D90A4, 8032, 7},
  {0x00016B9CF67D90A5, 8039, 7},
  {0x00016B9CF67D90A6, 8046, 7},
  {0x00016B9CF67D90A7, 8053, 7},
  {0x00016B9CF67D90C5, 8060, 7},
  {0x00016B9CF67D90C6, 8067, 7},
  {0x00016B9CF67D90C7, 8074, 7},
  {0x00016B9CF67D90C8, 8081, 7},
  {0x00016B9CF6914462, 8088, 7},
  {0x00016B9CF698654F, 8095, 7},
  {0x00016B9CF698F1B0, 8102, 7},
  {0x00016B9CF77D1957, 8109, 7},
  {0x00016B9CF77D1958, 8116, 7},
  {0x00016B9CF77D1959, 8123, 7},
  {0x00016B9CF77D195A, 8130, 7},
  {0x00016B9CF8746839, 8137, 7},
  {0x00016B9CF874683A, 8144, 7},
  {0x00016B9CF874683C, 8151, 7},
  {0x00016B9CF874683D, 8158, 7},
  {0x00016B9CF8D2B944, 8165, 7},
  {0x00016B9CF8D2B946, 8172, 7},
  {0x00016B9CF8D2B947, 8179, 7},
  {0x00016B9CF8D2B948, 8186, 7},
  {0x00016B9CF8D2B965, 8193, 7},
  {0x00016B9CF8D2B967, 8200, 7},
  {0x00016B9CF8D2B968, 8207, 7},
  {0x00016B9CF8D2B969, 8214, 7},
  {0x00016B9CF8E66D03, 8221, 7},
  {0x00016B9CF8ED8DF0, 8228, 7},
  {0x00016B9CF8EE1A51, 8235, 7},
  {0x00016B9CF9D241F7, 8242, 7},
  {0x00016B9CF9D241F9, 8249, 7},
  {0x00016B9CF9D241FA, 8256, 7},
  {0x00016B9CF9D241FB, 8263, 7},
  {0x00016B9CFAC990DA, 8270, 7},
  {0x00016B9CFAC990DB, 8277, 7},
  {0x00016B9CFAC990DC, 8284, 7},
  {0x00016B9CFAC990DE, 8291, 7},
  {0x00016B9CFB27E1E5, 8298, 7},
  {0x00016B9CFB27E1E6, 8305, 7},
  {0x00016B9CFB27E1E8, 8312, 7},
  {0x00016B9CFB27E1E9, 8319, 7},
  {0x00016B9CFB27E206, 8326, 7},
  {0x00016B9CFB27E207, 8333, 7},
  {0x00016B9CFB27E209, 8340, 7},
  {0x00016B9CFB27E20A, 8347, 7},
  {0x00016B9CFB3B95A4, 8354, 7},
  {0x00016B9CFB42B691, 8361, 7},
  {0x00016B9CFB4342F2, 8368, 7},
  {0x00016B9CFC276A98, 8375, 7},
  {0x00016B9CFC276A99, 8382, 7},
  {0x00016B9CFC276A9B, 8389, 7},
  {0x00016B9CFC276A9C, 8396, 7},
  {0x00016B9CFD1EB97B, 8403, 7},
  {0x00016B9CFD1EB97C, 8410, 7},
  {0x00016B9CFD1EB97D, 8417, 7},
  {0x00016B9CFD1EB97E, 8424, 7},
  {0x00016B9CFD7D0A86, 8431, 7},
  {0x00016B9CFD7D0A87, 8438, 7},
  {0x00016B9CFD7D0A88, 8445, 7},
  {0x00016B9CFD7D0A8A, 8452, 7},
  {0x00016B9CFD7D0AA7, 8459, 7},
  {0x00016B9CFD7D0AA8, 8466, 7},
  {0x00016B9CFD7D0AA9, 8473, 7},
  {0x00016B9CFD7D0AAB, 8480, 7},
  {0x00016B9CFD90BE45, 8487, 7},
  {0x00016B9CFD97DF32, 8494, 7},
  {0x00016B9CFD986B93, 8501, 7},
  {0x00016B9CFE7C9339, 8508, 7},
  {0x00016B9CFE7C933A, 8515, 7},
  {0x00016B9CFE7C933B, 8522, 7},
  {0x00016B9CFE7C933D, 8529, 7},
  {0x00016B9CFFD23327, 8536, 7},
  {0x00016B9CFFD23328, 8543, 7},
  {0x00016B9CFFD23329, 8550, 7},
  {0x00016B9CFFD2332A, 8557, 7},
  {0x00016B9CFFD23348, 8564, 7},
  {0x00016B9CFFD23349, 8571, 7},
  {0x00016B9CFFD2334A, 8578, 7},
  {0x00016B9CFFD2334B, 8585, 7},
  {0x00016B9CFFE5E6E6, 8592, 7},
  {0x00016B9CFFED07D3, 8599, 7},
  {0x00016B9CFFED9434, 8606, 7},
  {0x00016B9D00D1BBDA, 8613, 7},
  {0x00016B9D00D1BBDB, 8620, 7},
  {0x00016B9D00D1BBDC, 8627, 7},
  {0x00016B9D00D1BBDD, 8634, 7},
  {0x00016D3D1CA33CC8, 8641, 7},
  {0x00016D3D1CA33CC9, 8648, 7},
  {0x00016D3D1CA33CCA, 8655, 7},
  {0x00016D3D1CA33CCB, 8662, 7},
  {0x00016D3D1EF86568, 8669, 7},
  {0x00016D3D1EF8656A, 8676, 7},
  {0x00016D3D1EF8656B, 8683, 7},
  {0x00016D3D1EF8656C, 8690, 7},
  {0x00016D3D1F56B694, 8697, 7},
  {0x00016D3D1F56B695, 8704, 7},
  {0x00016D3D1F56B696, 8711, 7},
  {0x00016D3D1F56B697, 8718, 7},
  {0x00016D3D1F56B698, 8725, 7},
  {0x00016D3D1F69B7CA, 8732, 7},
  {0x00016D3D1F70D8B7, 8739, 7},
  {0x00016D3D1F716518, 8746, 7},
  {0x00016D3D20563F27, 8753, 7},
  {0x00016D3D20563F28, 8760, 7},
  {0x00016D3D20563F29, 8767, 7},
  {0x00016D3D20563F2A, 8774, 7},
  {0x00016D3D214D8E09, 8781, 7},
  {0x00016D3D214D8E0A, 8788, 7},
  {0x00016D3D214D8E0C, 8795, 7},
  {0x00016D3D214D8E0D, 8802, 7},
  {0x00016D3D21ABDF35, 8809, 7},
  {0x00016D3D21ABDF36, 8816, 7},
  {0x00016D3D21ABDF37, 8823, 7},
  {0x00016D3D21ABDF38, 8830, 7},
  {0x00016D3D21ABDF39, 8837, 7},
  {0x00016D3D21BEE06B, 8844, 7},
  {0x00016D3D21C60158, 8851, 7},
  {0x00016D3D21C68DB9, 8858, 7},
  {0x00016D3D22AB67C7, 8865, 7},
  {0x00016D3D22AB67C9, 8872, 7},
  {0x00016D3D22AB67CA, 8879, 7},
  {0x00016D3D22AB67CB, 8886, 7},
  {0x00016D3D23A2B6AA, 8893, 7},
  {0x00016D3D23A2B6AB, 8900, 7},
  {0x00016D3D23A2B6AC, 8907, 7},
  {0x00016D3D23A2B6AE, 8914, 7},
  {0x00016D3D240107D6, 8921, 7},
  {0x00016D3D240107D7, 8928, 7},
  {0x00016D3D240107D8, 8935, 7},
  {0x00016D3D240107D9, 8942, 7},
  {0x00016D3D240107DA, 8949, 7},
  {0x00016D3D2414090C, 8956, 7},
  {0x00016D3D241B29F9, 8963, 7},
  {0x00016D3D241BB65A, 8970, 7},
  {0x00016D3D25009068, 8977, 7},
  {0x00016D3D25009069, 8984, 7},
  {0x00016D3D2500906B, 8991, 7},
  {0x00016D3D2500906C, 8998, 7},
  {0x00016D3D25F7DF4B, 9005, 7},
  {0x00016D3D25F7DF4C, 9012, 7},
  {0x00016D3D25F7DF4D, 9019, 7},
  {0x00016D3D25F7DF4E, 9026, 7},
  {0x00016D3D26563077, 9033, 7},
  {0x00016D3D26563078, 9040, 7},
  {0x00016D3D26563079, 9047, 7},
  {0x00016D3D2656307A, 9054, 7},
  {0x00016D3D2656307B, 9061, 7},
  {0x00016D3D266931AD, 9068, 7},
  {0x00016D3D2670529A, 9075, 7},
  {0x00016D3D2670DEFB, 9082, 7},
  {0x00016D3D2755B909, 9089, 7},
  {0x00016D3D2755B90A, 9096, 7},
  {0x00016D3D2755B90B, 9103, 7},
  {0x00016D3D2755B90D, 9110, 7},
  {0x00016D3D28AB5918, 9117, 7},
  {0x00016D3D28AB5919, 9124, 7},
  {0x00016D3D28AB591A, 9131, 7},
  {0x00016D3D28AB591B, 9138, 7},
  {0x00016D3D28AB591C, 9145, 7},
  {0x00016D3D28BE5A4E, 9152, 7},
  {0x00016D3D28C57B3B, 9159, 7},
  {0x00016D3D28C6079C, 9166, 7},
  {0x00016D3D29AAE1AA, 9173, 7},
  {0x00016D3D29AAE1AB, 9180, 7},
  {0x00016D3D29AAE1AC, 9187, 7},
  {0x00016D3D29AAE1AD, 9194, 7},
  {0x0001889698ABB67B, 9201, 7},
  {0x0001889698B336AB, 9208, 7},
  {0x0001889698B58E57, 9215, 7},
  {0x002E527D2C44DCFA, 9222, 8},
  {0x002E52871886B1DB, 9230, 8},
  {0x002E52871886B1DC, 9238, 8},
  {0x002ED8AC050027DC, 9246, 8},
  {0x002ED8AC052458DE, 9254, 8},
  {0x002ED8AC51FA649D, 9262, 8},
  {0x002ED8AC521E959F, 9270, 8},
  {0x002ED8AC9EF4A15E, 9278, 8},
  {0x002ED8AC9F18D260, 9286, 8},
  {0x002ED8ACEBEEDE1F, 9294, 8},
  {0x002ED8ACEC130F21, 9302, 8},
  {0x002ED8AD38E91AE0, 9310, 8},
  {0x002ED8AD390D4BE2, 9318, 8},
  {0x002EDF11604EAB98, 9326, 8},
  {0x002EDF11604EAB99, 9334, 8},
  {0x002EDF11604EAB9A, 9342, 8},
  {0x002EDF11604EAB9B, 9350, 8},
  {0x002EDF11604EAB9C, 9358, 8},
  {0x002EDF11AD48E859, 9366, 8},
  {0x002EDF11AD48E85A, 9374, 8},
  {0x002EDF11AD48E85B, 9382, 8},
  {0x002EDF11AD48E85C, 9390, 8},
  {0x002EDF11AD48E85D, 9398, 8},
  {0x002EDF11FA43251A, 9406, 8},
  {0x002EDF11FA43251B, 9414, 8},
  {0x002EDF11FA43251C, 9422, 8},
  {0x002EDF11FA43251D, 9430, 8},
  {0x002EDF11FA43251E, 9438, 8},
  {0x002EDF12473D61DB, 9446, 8},
  {0x002EDF12473D61DC, 9454, 8},
  {0x002EDF12473D61DD, 9462, 8},
  {0x002EDF12473D61DE, 9470, 8},
  {0x002EDF12473D61DF, 9478, 8},
  {0x002EDF1294379E9C, 9486, 8},
  {0x002EDF1294379E9D, 9494, 8},
  {0x002EDF1294379E9E, 9502, 8},
  {0x002EDF1294379E9F, 9510, 8},
  {0x002EDF1294379EA0, 9518, 8},
  {0x002EDF1B4C908079, 9526, 8},
  {0x002EDF1B4C90807A, 9534, 8},
  {0x002EDF1B4C90807B, 9542, 8},
  {0x002EDF1B4C90807C, 9550, 8},
  {0x002EDF1B4C90807D, 9558, 8},
  {0x002EDF1B4C90809A, 9566, 8},
  {0x002EDF1B4C90809B, 9574, 8},
  {0x002EDF1B4C90809C, 9582, 8},
  {0x002EDF1B4C90809D, 9590, 8},
  {0x002EDF1B4C90809E, 9598, 8},
  {0x002EDF1B998ABD3A, 9606, 8},
  {0x002EDF1B998ABD3B, 9614, 8},
  {0x002EDF1B998ABD3C, 9622, 8},
  {0x002EDF1B998ABD3D, 9630, 8},
  {0x002EDF1B998ABD3E, 9638, 8},
  {0x002EDF1B998ABD5B, 9646, 8},
  {0x002EDF1B998ABD5C, 9654, 8},
  {0x002EDF1B998ABD5D, 9662, 8},
  {0x002EDF1B998ABD5E, 9670, 8},
  {0x002EDF1B998ABD5F, 9678, 8},
  {0x002EDF1BE684F9FB, 9686, 8},
  {0x002EDF1BE684F9FC, 9694, 8},
  {0x002EDF1BE684F9FD, 9702, 8},
  {0x002EDF1BE684F9FE, 9710, 8},
  {0x002EDF1BE684F9FF, 9718, 8},
  {0x002EDF1BE684FA1C, 9726, 8},
  {0x002EDF1BE684FA1D, 9734, 8},
  {0x002EDF1BE684FA1E, 9742, 8},
  {0x002EDF1BE684FA1F, 9750, 8},
  {0x002EDF1BE684FA20, 9758, 8},
  {0x002EDF1C337F36BC, 9766, 8},
  {0x002EDF1C337F36BD, 9774, 8},
  {0x002EDF1C337F36BE, 9782, 8},
  {0x002EDF1C337F36BF, 9790, 8},
  {0x002EDF1C337F36C0, 9798, 8},
  {0x002EDF1C337F36DD, 9806, 8},
  {0x002EDF1C337F36DE, 9814, 8},
  {0x002EDF1C337F36DF, 9822, 8},
  {0x002EDF1C337F36E0, 9830, 8},
  {0x002EDF1C337F36E1, 9838, 8},
  {0x002EDF1C8079737D, 9846, 8},
  {0x002EDF1C8079737E, 9854, 8},
  {0x002EDF1C8079737F, 9862, 8},
  {0x002EDF1C80797380, 9870, 8},
  {0x002EDF1C80797381, 9878, 8},
  {0x002EDF1C8079739E, 9886, 8},
  {0x002EDF1C8079739F, 9894, 8},
  {0x002EDF1C807973A0, 9902, 8},
  {0x002EDF1C807973A1, 9910, 8},
  {0x002EDF1C807973A2, 9918, 8},
  {0x002EF5EFCB26FB6F, 9926, 8},
  {0x002EF5EFCB4B2C71, 9934, 8},
  {0x002EF5F018213830, 9942, 8},
  {0x002EF5F018456932, 9950, 8},
  {0x002EF5F0651B74F1, 9958, 8},
  {0x002EF5F0653FA5F3, 9966, 8},
  {0x002EF5F0B215B1B2, 9974, 8},
  {0x002EF5F0B239E2B4, 9982, 8},
  {0x002EF5F0FF0FEE73, 9990, 8},
  {0x002EF5F0FF341F75, 9998, 8},
  {0x002F14A2B6D19387, 10006, 8},
  {0x002F14A2B6F5C489, 10014, 8},
  {0x002F14A303CBD048, 10022, 8},
  {0x002F14A303F0014A, 10030, 8},
  {0x002F14A350C60D09, 10038, 8},
  {0x002F14A350EA3E0B, 10046, 8},
  {0x002F14A39DC049CA, 10054, 8},
  {0x002F14A39DE47ACC, 10062, 8},
  {0x002F14A3EABA868B, 10070, 8},
  {0x002F14A3EADEB78D, 10078, 8},
  {0x002F14C09078136B, 10086, 8},
  {0x002F14C09078136C, 10094, 8},
  {0x002F14C09078136D, 10102, 8},
  {0x002F14C09078136E, 10110, 8},
  {0x002F14C0DD72502B, 10118, 8},
  {0x002F14C0DD72502D, 10126, 8},
  {0x002F14C0DD72502E, 10134, 8},
  {0x002F14C0DD72502F, 10142, 8},
  {0x002F14C12A6C8CEC, 10150, 8},
  {0x002F14C12A6C8CED, 10158, 8},
  {0x002F14C12A6C8CEF, 10166, 8},
  {0x002F14C12A6C8CF0, 10174, 8},
  {0x002F14C17766C9AD, 10182, 8},
  {0x002F14C17766C9AE, 10190, 8},
  {0x002F14C17766C9AF, 10198, 8},
  {0x002F14C17766C9B1, 10206, 8},
  {0x002F14C1C461066E, 10214, 8},
  {0x002F14C1C461066F, 10222, 8},
  {0x002F14C1C4610670, 10230, 8},
  {0x002F14C1C4610671, 10238, 8},
  {0xC762E8EAA73710D0, 10246, 10},
  {0xC762E8EAA73710D1, 10256, 10},
  {0xC762E8EAA73710D2, 10266, 10},
  {0xC762E8EAA73710D3, 10276, 10},
  {0xC762E8EAA73710D4, 10286, 10},
  {0xC762EA321BB381D1, 10296, 10},
  {0xC762EA321BB381D2, 10306, 10},
  {0xC762EA321BB381D3, 10316, 10},
  {0xC762EA321BB381D4, 10326, 10},
  {0xC762EA321BB381D5, 10336, 10},
  {0xC762EB79902FF2D2, 10346, 10},
  {0xC762EB79902FF2D3, 10356, 10},
  {0xC762EB79902FF2D4, 10366, 10},
  {0xC762EB79902FF2D5, 10376, 10},
  {0xC762EB79902FF2D6, 10386, 10},
  {0xC762ECC104AC63D3, 10396, 10},
  {0xC762ECC104AC63D4, 10406, 10},
  {0xC762ECC104AC63D5, 10416, 10},
  {0xC762ECC104AC63D6, 10426, 10},
  {0xC762ECC104AC63D7, 10436, 10},
  {0xC762EE087928D4D4, 10446, 10},
  {0xC762EE087928D4D5, 10456, 10},
  {0xC762EE087928D4D6, 10466, 10},
  {0xC762EE087928D4D7, 10476, 10},
  {0xC762EE087928D4D8, 10486, 10},
  {0xC7631320AB41A1F1, 10496, 10},
  {0xC7631320AB41A1F2, 10506, 10},
  {0xC7631320AB41A1F3, 10516, 10},
  {0xC7631320AB41A1F4, 10526, 10},
  {0xC7631320AB41A1F5, 10536, 10},
  {0xC7631320AB41A212, 10546, 10},
  {0xC7631320AB41A213, 10556, 10},
  {0xC7631320AB41A214, 10566, 10},
  {0xC7631320AB41A215, 10576, 10},
  {0xC7631320AB41A216, 10586, 10},
  {0xC76314681FBE12F2, 10596, 10},
  {0xC76314681FBE12F3, 10606, 10},
  {0xC76314681FBE12F4, 10616, 10},
  {0xC76314681FBE12F5, 10626, 10},
  {0xC76314681FBE12F6, 10636, 10},
  {0xC76314681FBE1313, 10646, 10},
  {0xC76314681FBE1314, 10656, 10},
  {0xC76314681FBE1315, 10666, 10},
  {0xC76314681FBE1316, 10676, 10},
  {0xC76314681FBE1317, 10686, 10},
  {0xC76315AF943A83F3, 10696, 10},
  {0xC76315AF943A83F4, 10706, 10},
  {0xC76315AF943A83F5, 10716, 10},
  {0xC76315AF943A83F6, 10726, 10},
  {0xC76315AF943A83F7, 10736, 10},
  {0xC76315AF943A8414, 10746, 10},
  {0xC76315AF943A8415, 10756, 10},
  {0xC76315AF943A8416, 10766, 10},
  {0xC76315AF943A8417, 10776, 10},
  {0xC76315AF943A8418, 10786, 10},
  {0xC76316F708B6F4F4, 10796, 10},
  {0xC76316F708B6F4F5, 10806, 10},
  {0xC76316F708B6F4F6, 10816, 10},
  {0xC76316F708B6F4F7, 10826, 10},
  {0xC76316F708B6F4F8, 10836, 10},
  {0xC76316F708B6F515, 10846, 10},
  {0xC76316F708B6F516, 10856, 10},
  {0xC76316F708B6F517, 10866, 10},
  {0xC76316F708B6F518, 10876, 10},
  {0xC76316F708B6F519, 10886, 10},
  {0xC763183E7D3365F5, 10896, 10},
  {0xC763183E7D3365F6, 10906, 10},
  {0xC763183E7D3365F7, 10916, 10},
  {0xC763183E7D3365F8, 10926, 10},
  {0xC763183E7D3365F9, 10936, 10},
  {0xC763183E7D336616, 10946, 10},
  {0xC763183E7D336617, 10956, 10},
  {0xC763183E7D336618, 10966, 10},
  {0xC763183E7D336619, 10976, 10},
  {0xC763183E7D33661A, 10986, 10},
  {0xC84747268462EEE3, 10996, 10},
  {0xC84747268462EEE4, 11006, 10},
  {0xC84747268462EEE5, 11016, 10},
  {0xC84747268462EEE6, 11026, 10},
  {0xC847486DF8DF5FE3, 11036, 10},
  {0xC847486DF8DF5FE5, 11046, 10},
  {0xC847486DF8DF5FE6, 11056, 10},
  {0xC847486DF8DF5FE7, 11066, 10},
  {0xC84749B56D5BD0E4, 11076, 10},
  {0xC84749B56D5BD0E5, 11086, 10},
  {0xC84749B56D5BD0E7, 11096, 10},
  {0xC84749B56D5BD0E8, 11106, 10},
  {0xC8474AFCE1D841E5, 11116, 10},
  {0xC8474AFCE1D841E6, 11126, 10},
  {0xC8474AFCE1D841E7, 11136, 10},
  {0xC8474AFCE1D841E9, 11146, 10},
  {0xC8474C445654B2E6, 11156, 10},
  {0xC8474C445654B2E7, 11166, 10},
  {0xC8474C445654B2E8, 11176, 10},
  {0xC8474C445654B2E9, 11186, 10},
};

internal B32
uishell_terminal_codepoint_defaults_to_emoji(U32 codepoint)
{
  B32 result = 0;
  U64 min = 0;
  U64 opl = ArrayCount(uishell_terminal_emoji_presentation_ranges);
  while(min < opl)
  {
    U64 mid = (min + opl)/2;
    UIShell_TerminalCodepointRange range = uishell_terminal_emoji_presentation_ranges[mid];
    if(codepoint < range.first)
    {
      opl = mid;
    }
    else if(range.last < codepoint)
    {
      min = mid + 1;
    }
    else
    {
      result = 1;
      break;
    }
  }
  return result;
}

internal B32
uishell_terminal_codepoint_sequence_match(U32 const *a, U32 const *b, U64 count)
{
  B32 result = 1;
  for(U64 idx = 0; idx < count; idx += 1)
  {
    if(a[idx] != b[idx])
    {
      result = 0;
      break;
    }
  }
  return result;
}

internal B32
uishell_terminal_codepoint_sequence_defaults_to_emoji(U32 const *codepoints, U64 count)
{
  B32 result = 0;
  if(count > 1)
  {
    U64 hash = uishell_terminal_hash_from_codepoints(codepoints, count);
    U64 min = 0;
    U64 opl = ArrayCount(uishell_terminal_emoji_sequences);
    while(min < opl)
    {
      U64 mid = (min + opl)/2;
      if(uishell_terminal_emoji_sequences[mid].hash < hash)
      {
        min = mid + 1;
      }
      else
      {
        opl = mid;
      }
    }
    for(U64 idx = min; idx < ArrayCount(uishell_terminal_emoji_sequences); idx += 1)
    {
      UIShell_TerminalEmojiSequence sequence = uishell_terminal_emoji_sequences[idx];
      if(sequence.hash != hash)
      {
        break;
      }
      if(sequence.codepoint_count == count &&
         sequence.codepoint_off + sequence.codepoint_count <= ArrayCount(uishell_terminal_emoji_sequence_codepoints) &&
         uishell_terminal_codepoint_sequence_match(codepoints, uishell_terminal_emoji_sequence_codepoints + sequence.codepoint_off, count))
      {
        result = 1;
        break;
      }
    }
  }
  return result;
}

internal B32
uishell_terminal_glyph_key_wants_color_emoji(UIShell_TerminalGlyphKey key, U32 const *codepoints)
{
  B32 result = (key.presentation == UIShell_TerminalGlyphPresentation_Emoji);
  if(key.presentation == UIShell_TerminalGlyphPresentation_Unspecified)
  {
    result = uishell_terminal_codepoint_sequence_defaults_to_emoji(codepoints, key.codepoint_count);
    for(U64 idx = 0; !result && idx < key.codepoint_count; idx += 1)
    {
      if(uishell_terminal_codepoint_defaults_to_emoji(codepoints[idx]))
      {
        result = 1;
        break;
      }
    }
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
  B32 wants_color_emoji = uishell_terminal_glyph_key_wants_color_emoji(key, codepoints);
  if(wants_color_emoji)
  {
    result = fnt_tag_zero();
    for(U64 idx = 0; idx < font_set->color_emoji_font_count; idx += 1)
    {
      FNT_Tag color_font = font_set->color_emoji_fonts[idx];
      if(uishell_terminal_font_has_glyph_key(color_font, key, codepoints))
      {
        result = color_font;
        break;
      }
    }
  }
  if(fnt_tag_match(result, fnt_tag_zero()))
  {
    result = font_set->primary_font;
  }
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

////////////////////////////////
//~ terminal image cache (Kitty images)

typedef struct UIShell_TerminalImageBytes UIShell_TerminalImageBytes;
struct UIShell_TerminalImageBytes
{
  Arena *arena;
  U8 *data;
  U64 size;
};

internal bool
uishell_terminal_image_resource_data_copy(void *user_data, const uint8_t *data, size_t data_len)
{
  UIShell_TerminalImageBytes *out = (UIShell_TerminalImageBytes *)user_data;
  // Copy bytes out during the synchronous borrow; decode happens after the
  // callback returns so we never hold the session across a decode.
  if(data != 0 && data_len != 0)
  {
    out->data = push_array_no_zero(out->arena, U8, data_len);
    MemoryCopy(out->data, data, data_len);
    out->size = (U64)data_len;
  }
  return 1;
}

internal UIShell_TerminalImageResource *
uishell_terminal_image_cache_resource_from_id(UIShell_TerminalImageCache *cache, U32 image_id)
{
  UIShell_TerminalImageResource *result = 0;
  for(UIShell_TerminalImageResource *r = cache->first_resource; r != 0; r = r->next)
  {
    if(r->image_id == image_id)
    {
      result = r;
      break;
    }
  }
  return result;
}

// Decode an image resource payload into a freshly-allocated straight-alpha RGBA8
// buffer (alloc'd from `arena`). Returns 0 on any malformed/unsupported input.
// First slice supports uncompressed RGB/RGBA and PNG only.
internal U8 *
uishell_terminal_image_decode_rgba8(Arena *arena, cleat_image_resource const *meta, U8 const *bytes, U64 byte_count, U32 *out_w, U32 *out_h)
{
  U8 *result = 0;
  U32 w = meta->width_px;
  U32 h = meta->height_px;
  if(meta->compression != CLEAT_IMAGE_COMPRESSION_NONE)
  {
    // zlib deflate deferred to a later slice.
    return 0;
  }
  switch(meta->format)
  {
    case CLEAT_IMAGE_FORMAT_RGBA:
    {
      if(w != 0 && h != 0 && byte_count >= (U64)w*(U64)h*4)
      {
        result = push_array_no_zero(arena, U8, (U64)w*(U64)h*4);
        MemoryCopy(result, bytes, (U64)w*(U64)h*4);
        *out_w = w;
        *out_h = h;
      }
    }break;
    case CLEAT_IMAGE_FORMAT_RGB:
    {
      if(w != 0 && h != 0 && byte_count >= (U64)w*(U64)h*3)
      {
        U64 px = (U64)w*(U64)h;
        result = push_array_no_zero(arena, U8, px*4);
        for(U64 i = 0; i < px; i += 1)
        {
          result[i*4 + 0] = bytes[i*3 + 0];
          result[i*4 + 1] = bytes[i*3 + 1];
          result[i*4 + 2] = bytes[i*3 + 2];
          result[i*4 + 3] = 255;
        }
        *out_w = w;
        *out_h = h;
      }
    }break;
    case CLEAT_IMAGE_FORMAT_PNG:
    {
      int dw = 0;
      int dh = 0;
      int comp = 0;
      stbi_uc *decoded = stbi_load_from_memory(bytes, (int)byte_count, &dw, &dh, &comp, 4);
      if(decoded != 0 && dw > 0 && dh > 0)
      {
        U64 size = (U64)dw*(U64)dh*4;
        result = push_array_no_zero(arena, U8, size);
        MemoryCopy(result, decoded, size);
        *out_w = (U32)dw;
        *out_h = (U32)dh;
      }
      if(decoded != 0)
      {
        stbi_image_free(decoded);
      }
    }break;
    default:
    {
      // GRAY / GRAY_ALPHA deferred to a later slice.
    }break;
  }
  return result;
}

internal void
uishell_terminal_image_cache_apply_render_update(UIShell_TerminalImageCache *cache, cleat_session *session, cleat_render_update const *update)
{
  if(cache == 0 || update == 0)
  {
    return;
  }
  if(cache->arena == 0)
  {
    cache->arena = arena_alloc(.name = "terminal image resources");
  }
  cache->update_counter += 1;

  // Upload any resources we have not yet seen, or whose generation changed.
  // Placements reference resources by (image_id, generation); a placement may
  // name a resource we never uploaded, which the draw path skips. Resources no
  // placement (or re-transmission) has named for a grace window are evicted
  // below - streaming producers (e.g. katzensteg frames as fresh image ids)
  // would otherwise accumulate one texture per frame for the session lifetime
  // (observed: 46GB / 12k textures of graphics footprint -> jetsam kill).
  if(session != 0)
  {
    for(U64 i = 0; i < update->image_resource_count; i += 1)
    {
      cleat_image_resource const *meta = &update->image_resources[i];
      UIShell_TerminalImageResource *res = uishell_terminal_image_cache_resource_from_id(cache, meta->image_id);
      if(res != 0 && res->valid && res->generation == meta->generation)
      {
        res->last_referenced_update = cache->update_counter;
        continue; // already resident
      }

      Temp scratch = scratch_begin(0, 0);
      UIShell_TerminalImageBytes bytes = { .arena = scratch.arena };
      R_Handle texture = r_handle_zero();
      U32 tex_w = 0;
      U32 tex_h = 0;
      if(cleat_session_with_image_resource_data(session, meta->image_id, meta->generation, uishell_terminal_image_resource_data_copy, &bytes) &&
         bytes.data != 0 && bytes.size != 0)
      {
        U8 *rgba = uishell_terminal_image_decode_rgba8(scratch.arena, meta, bytes.data, bytes.size, &tex_w, &tex_h);
        if(rgba != 0 && tex_w != 0 && tex_h != 0)
        {
          texture = r_tex2d_alloc(R_ResourceKind_Static, v2s32((S32)tex_w, (S32)tex_h), R_Tex2DFormat_RGBA8, rgba);
        }
      }
      scratch_end(scratch);

      B32 uploaded = !r_handle_match(texture, r_handle_zero());
      if(res == 0)
      {
        res = cache->free_resource;
        if(res != 0)
        {
          SLLStackPop(cache->free_resource);
          MemoryZeroStruct(res);
        }
        else
        {
          res = push_array(cache->arena, UIShell_TerminalImageResource, 1);
        }
        res->image_id = meta->image_id;
        SLLQueuePush(cache->first_resource, cache->last_resource, res);
      }
      else if(!r_handle_match(res->texture, r_handle_zero()))
      {
        // Superseded generation: release the old texture before replacing.
        r_tex2d_release(res->texture);
        res->texture = r_handle_zero();
      }
      res->generation = meta->generation;
      res->width_px = tex_w;
      res->height_px = tex_h;
      res->texture = texture;
      res->valid = uploaded;
      res->last_referenced_update = cache->update_counter;
    }
  }

  // Copy placements out of the update (pointers are invalid after release).
  // Honor each struct's `size` ABI guard rather than assuming our layout.
  if(cache->placement_arena != 0)
  {
    arena_clear(cache->placement_arena);
  }
  else
  {
    cache->placement_arena = arena_alloc(.name = "terminal image placements");
  }
  cache->placements = 0;
  cache->placement_count = 0;
  if(update->image_placement_count != 0 && update->image_placements != 0)
  {
    cache->placements = push_array(cache->placement_arena, UIShell_TerminalImagePlacement, update->image_placement_count);
    U8 const *base = (U8 const *)update->image_placements;
    // Stride by the provider's reported element size (ABI guard), not our
    // compile-time sizeof: a newer Cleat may grow the struct, in which case we
    // still read only the prefix fields we know.
    U64 stride = update->image_placements[0].size;
    if(stride < sizeof(cleat_image_placement))
    {
      stride = sizeof(cleat_image_placement);
    }
    for(U64 i = 0; i < update->image_placement_count; i += 1)
    {
      cleat_image_placement const *src = (cleat_image_placement const *)(base + i*stride);
      UIShell_TerminalImagePlacement *dst = &cache->placements[i];
      dst->image_id = src->image_id;
      dst->generation = src->generation;
      dst->placement_id = src->placement_id;
      dst->z = src->z;
      dst->viewport_col = src->viewport_col;
      dst->viewport_row = src->viewport_row;
      dst->grid_cols = src->grid_cols;
      dst->grid_rows = src->grid_rows;
      dst->pixel_width = src->pixel_width;
      dst->pixel_height = src->pixel_height;
      dst->source_x = src->source_x;
      dst->source_y = src->source_y;
      dst->source_width = src->source_width;
      dst->source_height = src->source_height;
      dst->x_offset_px = src->x_offset_px;
      dst->y_offset_px = src->y_offset_px;
      dst->order = i;
    }
    cache->placement_count = update->image_placement_count;
  }
  cache->render_generation = update->render_generation;

  // Mark every placement-referenced resource live, then evict resources
  // unreferenced for the grace window (transmit-then-place-later flows survive
  // because uploads mark too). ~4s at 60 updates/s.
  {
    for(U64 i = 0; i < cache->placement_count; i += 1)
    {
      UIShell_TerminalImageResource *res = uishell_terminal_image_cache_resource_from_id(cache, cache->placements[i].image_id);
      if(res != 0)
      {
        res->last_referenced_update = cache->update_counter;
      }
    }
    U64 grace = 240;
    UIShell_TerminalImageResource *prev = 0;
    for(UIShell_TerminalImageResource *r = cache->first_resource, *next = 0; r != 0; r = next)
    {
      next = r->next;
      if(cache->update_counter - r->last_referenced_update > grace)
      {
        if(!r_handle_match(r->texture, r_handle_zero()))
        {
          r_tex2d_release(r->texture);
        }
        if(prev != 0) { prev->next = next; }
        else          { cache->first_resource = next; }
        if(cache->last_resource == r) { cache->last_resource = prev; }
        MemoryZeroStruct(r);
        SLLStackPush(cache->free_resource, r);
      }
      else
      {
        prev = r;
      }
    }
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
uishell_terminal_vec4_near(Vec4F32 a, Vec4F32 b, F32 epsilon)
{
  B32 result = (abs_f32(a.x - b.x) <= epsilon &&
                abs_f32(a.y - b.y) <= epsilon &&
                abs_f32(a.z - b.z) <= epsilon &&
                abs_f32(a.w - b.w) <= epsilon);
  return result;
}

internal B32
uishell_terminal_codepoint_is_simple_text_run(U32 codepoint)
{
  B32 result = (0x20 <= codepoint && codepoint <= 0x7E);
  return result;
}

internal B32 uishell_terminal_cell_text_run_info(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, UIShell_TerminalCellFeed const *feed, U64 cell_count, UIShell_TerminalCursorArray cursors, U64 row_idx, U64 col_idx, FNT_Tag *font_out, Vec4F32 *fg_out, String8 *string_out);

internal B32
uishell_terminal_run_has_source_color(FNT_Run run)
{
  B32 result = 0;
  for(U64 piece_idx = 0; piece_idx < run.pieces.count; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    if(piece->kind == FNT_RasterKind_RGBA)
    {
      result = 1;
      break;
    }
  }
  return result;
}

internal B32
uishell_terminal_run_has_mask(FNT_Run run)
{
  B32 result = 0;
  for(U64 piece_idx = 0; piece_idx < run.pieces.count; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    if(piece->kind == FNT_RasterKind_Mask)
    {
      result = 1;
      break;
    }
  }
  return result;
}

internal Rng2F32
uishell_terminal_dr_text_piece_dst(Vec2F32 text_p, F32 run_descent, FNT_Piece *piece, F32 advance)
{
  (void)run_descent;
  Vec2F32 size = piece->draw_dim;
  Rng2F32 result = r2f32p(text_p.x + piece->offset.x + advance,
                          text_p.y + piece->offset.y,
                          text_p.x + piece->offset.x + advance + size.x,
                          text_p.y + piece->offset.y + size.y);
  return result;
}

internal String8
uishell_terminal_trace_codepoints_string(Arena *arena, cleat_cell const *cell)
{
  String8List list = {0};
  for(U64 idx = 0; idx < cell->grapheme_count; idx += 1)
  {
    if(idx != 0)
    {
      str8_list_push(arena, &list, str8_lit(" "));
    }
    str8_list_push(arena, &list, push_str8f(arena, "U+%04X", cell->graphemes[idx]));
  }
  StringJoin join = {0};
  String8 result = str8_list_join(arena, &list, &join);
  return result;
}

internal void
uishell_terminal_draw_run_at_row_baseline(FNT_Run run, Vec2F32 text_p, F32 row_descent, Vec4F32 color)
{
  Vec2F32 run_text_p = v2f32(text_p.x, text_p.y + run.descent - row_descent);
  F32 advance = 0;
  for(U64 piece_idx = 0; piece_idx < run.pieces.count; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    R_Handle texture = piece->texture;
    Rng2F32 src = r2f32p((F32)piece->subrect.x0, (F32)piece->subrect.y0, (F32)piece->subrect.x1, (F32)piece->subrect.y1);
    Rng2F32 dst = uishell_terminal_dr_text_piece_dst(run_text_p, run.descent, piece, advance);
    if(piece->draw_dim.x != 0 && piece->draw_dim.y != 0 && !r_handle_match(texture, r_handle_zero()))
    {
      Vec4F32 piece_color = (piece->kind == FNT_RasterKind_RGBA ? v4f32(1, 1, 1, color.w) : color);
      dr_img(dst, src, texture, piece_color, 0, 0, 0);
    }
    advance += piece->advance;
  }
}

internal Rng2F32
uishell_terminal_run_visible_rect_at_row_baseline(FNT_Run run, Vec2F32 text_p, F32 row_descent)
{
  Vec2F32 run_text_p = v2f32(text_p.x, text_p.y + run.descent - row_descent);
  Rng2F32 result = {0};
  B32 got_rect = 0;
  F32 advance = 0;
  for(U64 piece_idx = 0; piece_idx < run.pieces.count; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    if(piece->draw_dim.x != 0 && piece->draw_dim.y != 0 && !r_handle_match(piece->texture, r_handle_zero()))
    {
      Rng2F32 dst = uishell_terminal_dr_text_piece_dst(run_text_p, run.descent, piece, advance);
      if(got_rect)
      {
        result.x0 = Min(result.x0, dst.x0);
        result.y0 = Min(result.y0, dst.y0);
        result.x1 = Max(result.x1, dst.x1);
        result.y1 = Max(result.y1, dst.y1);
      }
      else
      {
        result = dst;
        got_rect = 1;
      }
    }
    advance += piece->advance;
  }
  return result;
}

typedef enum UIShell_TerminalCellTextPath
{
  UIShell_TerminalCellTextPath_Missing,
  UIShell_TerminalCellTextPath_NormalMask,
  UIShell_TerminalCellTextPath_SourceColor,
}
UIShell_TerminalCellTextPath;

typedef struct UIShell_TerminalCellTextDecision UIShell_TerminalCellTextDecision;
struct UIShell_TerminalCellTextDecision
{
  UIShell_TerminalCellTextPath path;
  FNT_Tag font;
  FNT_RasterFlags raster_flags;
  FNT_Run run;
  String8 string;
  B32 wants_source_color_cell;
};

internal Vec2F32
uishell_terminal_source_color_text_p_for_cell(UIShell_TerminalCellTextDecision decision, Rng2F32 cell_rect, F32 text_p_y, F32 row_descent)
{
  Vec2F32 result = v2f32(cell_rect.x0, text_p_y);
  Rng2F32 visible_rect = uishell_terminal_run_visible_rect_at_row_baseline(decision.run, result, row_descent);
  F32 visible_width = visible_rect.x1 - visible_rect.x0;
  if(visible_width > 0)
  {
    F32 visible_center = (visible_rect.x0 + visible_rect.x1)*0.5f;
    F32 cell_center = (cell_rect.x0 + cell_rect.x1)*0.5f;
    result.x += round_f32(cell_center - visible_center);
  }
  return result;
}

internal String8
uishell_terminal_trace_path_string(UIShell_TerminalCellTextPath path)
{
  String8 result = str8_lit("missing");
  switch(path)
  {
    default:{}break;
    case UIShell_TerminalCellTextPath_Missing:     {result = str8_lit("missing");}break;
    case UIShell_TerminalCellTextPath_NormalMask:  {result = str8_lit("mask");}break;
    case UIShell_TerminalCellTextPath_SourceColor: {result = str8_lit("source_color");}break;
  }
  return result;
}

internal String8
uishell_terminal_trace_font_role_string(UIShell_TerminalGlyphRenderer *renderer, FNT_Tag font)
{
  String8 result = str8_lit("unknown");
  if(fnt_tag_match(font, renderer->font_set.primary_font))
  {
    result = str8_lit("primary");
  }
  else
  {
    for(U64 idx = 0; idx < renderer->font_set.color_emoji_font_count; idx += 1)
    {
      if(fnt_tag_match(font, renderer->font_set.color_emoji_fonts[idx]))
      {
        result = str8_lit("color_emoji");
        goto done;
      }
    }
    for(U64 idx = 0; idx < renderer->font_set.fallback_font_count; idx += 1)
    {
      if(fnt_tag_match(font, renderer->font_set.fallback_fonts[idx]))
      {
        result = str8_lit("fallback");
        goto done;
      }
    }
  }
  done:;
  return result;
}

internal void
uishell_terminal_trace_run(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, String8 label, U64 row_idx, U64 col_start, U64 col_opl, cleat_cell const *cell, UIShell_TerminalCellTextPath path, FNT_Tag font, FNT_RasterFlags raster_flags, String8 string, FNT_Run run, Vec2F32 text_p, F32 row_descent, Vec4F32 color)
{
  Temp scratch = scratch_begin(&arena, 1);
  String8 codepoints = str8_lit("-");
  U32 cell_flags = 0;
  U32 cell_width = 0;
  if(cell != 0)
  {
    codepoints = uishell_terminal_trace_codepoints_string(scratch.arena, cell);
    cell_flags = cell->flags;
    cell_width = cell->width;
  }
  Vec2F32 run_text_p = v2f32(text_p.x, text_p.y + run.descent - row_descent);
  F32 row_baseline_y = text_p.y - row_descent;
  String8 font_role = uishell_terminal_trace_font_role_string(renderer, font);
  String8 font_path = fnt_path_from_tag(font);
  if(font_path.size == 0)
  {
    font_path = str8_lit("<embedded-or-unknown>");
  }
  log_infof("terminal glyph trace: gen=%I64u row=%I64u cols=%I64u..%I64u label=%S path=%S text=\"%S\" cps=[%S] cell_flags=0x%x cell_width=%u font_role=%S font=(0x%I64x,0x%I64x) font_path=\"%S\" raster_flags=0x%x color=(%.3f,%.3f,%.3f,%.3f) text_y=%.3f row_descent=%.3f row_baseline_y=%.3f run_ascent=%.3f run_descent=%.3f run_text_y=%.3f pieces=%I64u",
            renderer->trace_generation,
            row_idx,
            col_start,
            col_opl,
            label,
            uishell_terminal_trace_path_string(path),
            string,
            codepoints,
            cell_flags,
            cell_width,
            font_role,
            font.u64[0],
            font.u64[1],
            font_path,
            raster_flags,
            color.x,
            color.y,
            color.z,
            color.w,
            text_p.y,
            row_descent,
            row_baseline_y,
            run.ascent,
            run.descent,
            run_text_p.y,
            run.pieces.count);
  F32 advance = 0;
  for(U64 piece_idx = 0; piece_idx < run.pieces.count; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    Rng2F32 dst = uishell_terminal_dr_text_piece_dst(run_text_p, run.descent, piece, advance);
    F32 piece_baseline_y = dst.y0 + piece->baseline_from_top;
    F32 baseline_delta = piece_baseline_y - row_baseline_y;
    log_infof("terminal glyph trace piece: gen=%I64u row=%I64u cols=%I64u..%I64u piece=%I64u kind=%S advance_before=%.3f advance=%.3f offset=(%.3f,%.3f) origin_from_left=%.3f baseline_from_top=%.3f draw_dim=(%.3f,%.3f) dst=(%.3f,%.3f)-(%.3f,%.3f) piece_baseline_y=%.3f baseline_delta=%.6f decode_size=%u texture=0x%I64x",
              renderer->trace_generation,
              row_idx,
              col_start,
              col_opl,
              piece_idx,
              piece->kind == FNT_RasterKind_RGBA ? str8_lit("rgba") : str8_lit("mask"),
              advance,
              piece->advance,
              piece->offset.x,
              piece->offset.y,
              piece->origin_from_left,
              piece->baseline_from_top,
              piece->draw_dim.x,
              piece->draw_dim.y,
              dst.x0,
              dst.y0,
              dst.x1,
              dst.y1,
              piece_baseline_y,
              baseline_delta,
              piece->decode_size,
              piece->texture.u64[0]);
    advance += piece->advance;
  }
  scratch_end(scratch);
}

internal UIShell_TerminalCellTextDecision
uishell_terminal_cell_text_decision_from_cell(Arena *arena, UIShell_TerminalGlyphRenderer *renderer, cleat_cell const *cell)
{
  UIShell_TerminalCellTextDecision result = {0};
  result.path = UIShell_TerminalCellTextPath_Missing;
  result.string = uishell_terminal_string_from_cell(arena, cell);
  result.font = uishell_terminal_font_from_cell(renderer, cell);
  if(!fnt_tag_match(result.font, fnt_tag_zero()))
  {
    UIShell_TerminalGlyphKey key = uishell_terminal_glyph_key_from_cell(cell);
    result.wants_source_color_cell = uishell_terminal_glyph_key_wants_color_emoji(key, cell->graphemes);
    result.raster_flags = renderer->font_set.raster_flags;
    if(cell->grapheme_count > 1)
    {
      result.raster_flags |= FNT_RasterFlag_SinglePiece;
    }
    if(result.wants_source_color_cell)
    {
      FNT_RasterFlags source_color_flags = result.raster_flags|FNT_RasterFlag_TightBounds;
      FNT_Run source_color_run = dr_fnt_run_from_string(result.font, renderer->font_set.font_size, 0, 0, source_color_flags, result.string);
      if(uishell_terminal_run_has_source_color(source_color_run))
      {
        result.path = UIShell_TerminalCellTextPath_SourceColor;
        result.raster_flags = source_color_flags;
        result.run = source_color_run;
      }
    }
    if(result.path == UIShell_TerminalCellTextPath_Missing)
    {
      result.path = UIShell_TerminalCellTextPath_NormalMask;
      result.run = dr_fnt_run_from_string(result.font, renderer->font_set.font_size, 0, 0, result.raster_flags, result.string);
    }
  }
  return result;
}

internal void
uishell_terminal_draw_text_decision_in_cell(UIShell_TerminalCellTextDecision decision, Rng2F32 cell_rect, F32 text_p_y, F32 row_descent, Vec4F32 color)
{
  if(decision.path == UIShell_TerminalCellTextPath_NormalMask ||
     decision.path == UIShell_TerminalCellTextPath_SourceColor)
  {
    Vec2F32 text_p = v2f32(cell_rect.x0, text_p_y);
    if(decision.path == UIShell_TerminalCellTextPath_SourceColor)
    {
      text_p = uishell_terminal_source_color_text_p_for_cell(decision, cell_rect, text_p_y, row_descent);
    }
    uishell_terminal_draw_run_at_row_baseline(decision.run, text_p, row_descent, color);
  }
}

internal B32
uishell_terminal_diagnostic_check_run_placement(FNT_Run run, String8 label)
{
  B32 result = 1;
  Vec2F32 text_p = v2f32(32.f, 48.f);
  if(run.pieces.count == 0)
  {
    log_user_errorf("terminal glyph diagnostics failed: %S produced no font pieces", label);
    result = 0;
  }
  F32 advance = 0;
  for(U64 piece_idx = 0; piece_idx < run.pieces.count; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    Vec2F32 expected_offset = v2f32(-piece->origin_from_left,
                                    -run.descent - piece->baseline_from_top);
    if(abs_f32(piece->offset.x - expected_offset.x) > 0.001f ||
       abs_f32(piece->offset.y - expected_offset.y) > 0.001f)
    {
      log_user_errorf("terminal glyph diagnostics failed: %S piece offset does not match provider origin/baseline metadata", label);
      result = 0;
      break;
    }
    Rng2F32 terminal_dst = uishell_terminal_dr_text_piece_dst(text_p, run.descent, piece, advance);
    Rng2F32 dr_dst = r2f32p(text_p.x + piece->offset.x + advance,
                            text_p.y + piece->offset.y,
                            text_p.x + piece->offset.x + advance + piece->draw_dim.x,
                            text_p.y + piece->offset.y + piece->draw_dim.y);
    if(terminal_dst.x0 != dr_dst.x0 || terminal_dst.y0 != dr_dst.y0 ||
       terminal_dst.x1 != dr_dst.x1 || terminal_dst.y1 != dr_dst.y1)
    {
      log_user_errorf("terminal glyph diagnostics failed: %S placement differs from dr_text placement", label);
      result = 0;
      break;
    }
    advance += piece->advance;
  }
  return result;
}

internal B32
uishell_terminal_diagnostic_check_run_draw_contract(FNT_Run run, String8 label)
{
  B32 result = 1;
  Vec2F32 text_p = v2f32(32.f, 48.f);
  F32 baseline_y = text_p.y - run.descent;
  F32 advance = 0;
  if(run.pieces.count == 0)
  {
    log_user_errorf("terminal glyph diagnostics failed: %S produced no font pieces for draw contract", label);
    result = 0;
  }
  for(U64 piece_idx = 0; piece_idx < run.pieces.count; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    F32 pen_x = text_p.x + advance;
    Rng2F32 dst = uishell_terminal_dr_text_piece_dst(text_p, run.descent, piece, advance);
    if(abs_f32((dst.x0 + piece->origin_from_left) - pen_x) > 0.001f ||
       abs_f32((dst.y0 + piece->baseline_from_top) - baseline_y) > 0.001f)
    {
      log_user_errorf("terminal glyph diagnostics failed: %S does not preserve the public pen/baseline draw contract", label);
      result = 0;
      break;
    }
    advance += piece->advance;
  }
  return result;
}

internal B32
uishell_terminal_diagnostic_check_normal_run_placement(FNT_Run run, String8 label)
{
  B32 result = 1;
  if(uishell_terminal_run_has_source_color(run))
  {
    log_user_errorf("terminal glyph diagnostics failed: %S was classified as source-color", label);
    result = 0;
  }
  if(!uishell_terminal_diagnostic_check_run_placement(run, label))
  {
    result = 0;
  }
  if(!uishell_terminal_diagnostic_check_run_draw_contract(run, label))
  {
    result = 0;
  }
  for(U64 piece_idx = 0; piece_idx < run.pieces.count; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    Vec2F32 old_face_box_offset = v2f32(0, -(run.ascent + run.descent));
    if(abs_f32(piece->origin_from_left) > 0.001f ||
       abs_f32(piece->baseline_from_top - run.ascent) > 0.001f ||
       abs_f32(piece->offset.x - old_face_box_offset.x) > 0.001f ||
       abs_f32(piece->offset.y - old_face_box_offset.y) > 0.001f)
    {
      log_user_errorf("terminal glyph diagnostics failed: %S normal mask text did not preserve the established face-box text placement", label);
      result = 0;
      break;
    }
  }
  return result;
}

internal B32
uishell_terminal_diagnostic_check_normal_text_decision(UIShell_TerminalCellTextDecision decision, String8 label)
{
  B32 result = 1;
  if(decision.path != UIShell_TerminalCellTextPath_NormalMask)
  {
    log_user_errorf("terminal glyph diagnostics failed: %S did not use the normal mask-text path", label);
    result = 0;
  }
  if(decision.wants_source_color_cell)
  {
    log_user_errorf("terminal glyph diagnostics failed: %S requested a source-color font path", label);
    result = 0;
  }
  if(!uishell_terminal_diagnostic_check_normal_run_placement(decision.run, label))
  {
    result = 0;
  }
  return result;
}

internal String8
uishell_terminal_diagnostic_string_from_codepoints(Arena *arena, U32 *codepoints, U64 count)
{
  cleat_cell cell = {0};
  cell.graphemes = codepoints;
  cell.grapheme_count = count;
  String8 result = uishell_terminal_string_from_cell(arena, &cell);
  return result;
}

internal cleat_cell
uishell_terminal_diagnostic_cell_from_codepoints(U32 *codepoints, U64 count, U32 flags, U32 width)
{
  cleat_cell result = {0};
  cleat_rgb fg = {188, 209, 191};
  cleat_rgb bg = {4, 4, 4};
  result.graphemes = codepoints;
  result.grapheme_count = count;
  result.fg = fg;
  result.bg = bg;
  result.flags = flags;
  result.width = width;
  return result;
}

internal void
uishell_terminal_diagnostic_put_ascii_string(Arena *arena, cleat_cell *cells, U64 cell_count, U64 col, String8 string, cleat_rgb fg, cleat_rgb bg, U32 flags)
{
  for(U64 byte_idx = 0; byte_idx < string.size && col < cell_count; byte_idx += 1, col += 1)
  {
    U32 *grapheme = push_array(arena, U32, 1);
    grapheme[0] = string.str[byte_idx];
    cells[col] = uishell_terminal_diagnostic_cell_from_codepoints(grapheme, 1, flags, CLEAT_CELL_WIDTH_NARROW);
    cells[col].fg = fg;
    cells[col].bg = bg;
  }
}

typedef struct UIShell_TerminalRenderBucketStats UIShell_TerminalRenderBucketStats;
struct UIShell_TerminalRenderBucketStats
{
  U64 ui_pass_count;
  U64 rect_group_count;
  U64 textured_group_count;
  U64 rect_inst_count;
  U64 textured_inst_count;
  U64 source_color_like_inst_count;
};

internal UIShell_TerminalRenderBucketStats
uishell_terminal_render_bucket_stats_from_bucket(DR_Bucket *bucket)
{
  UIShell_TerminalRenderBucketStats result = {0};
  for(R_PassNode *pass_node = bucket->passes.first; pass_node != 0; pass_node = pass_node->next)
  {
    R_Pass *pass = &pass_node->v;
    if(pass->kind == R_PassKind_UI && pass->params_ui != 0)
    {
      result.ui_pass_count += 1;
      for(R_BatchGroup2DNode *group = pass->params_ui->rects.first; group != 0; group = group->next)
      {
        B32 textured = !r_handle_match(group->params.tex, r_handle_zero());
        if(textured)
        {
          result.textured_group_count += 1;
        }
        else
        {
          result.rect_group_count += 1;
        }
        for(R_BatchNode *batch_node = group->batches.first; batch_node != 0; batch_node = batch_node->next)
        {
          U64 inst_count = batch_node->v.byte_count/group->batches.bytes_per_inst;
          if(textured)
          {
            result.textured_inst_count += inst_count;
            R_Rect2DInst *insts = (R_Rect2DInst *)batch_node->v.v;
            for(U64 inst_idx = 0; inst_idx < inst_count; inst_idx += 1)
            {
              R_Rect2DInst *inst = &insts[inst_idx];
              Vec4F32 color = inst->colors[Corner_00];
              if(color.x == 1.f && color.y == 1.f && color.z == 1.f && color.w > 0.f)
              {
                result.source_color_like_inst_count += 1;
              }
            }
          }
          else
          {
            result.rect_inst_count += inst_count;
          }
        }
      }
    }
  }
  return result;
}

internal U64
uishell_terminal_render_bucket_textured_instance_count_with_color(DR_Bucket *bucket, Vec4F32 color)
{
  U64 result = 0;
  for(R_PassNode *pass_node = bucket->passes.first; pass_node != 0; pass_node = pass_node->next)
  {
    R_Pass *pass = &pass_node->v;
    if(pass->kind == R_PassKind_UI && pass->params_ui != 0)
    {
      for(R_BatchGroup2DNode *group = pass->params_ui->rects.first; group != 0; group = group->next)
      {
        if(!r_handle_match(group->params.tex, r_handle_zero()))
        {
          for(R_BatchNode *batch_node = group->batches.first; batch_node != 0; batch_node = batch_node->next)
          {
            U64 inst_count = batch_node->v.byte_count/group->batches.bytes_per_inst;
            R_Rect2DInst *insts = (R_Rect2DInst *)batch_node->v.v;
            for(U64 inst_idx = 0; inst_idx < inst_count; inst_idx += 1)
            {
              R_Rect2DInst *inst = &insts[inst_idx];
              if(uishell_terminal_vec4_near(inst->colors[Corner_00], color, 0.001f) &&
                 uishell_terminal_vec4_near(inst->colors[Corner_01], color, 0.001f) &&
                 uishell_terminal_vec4_near(inst->colors[Corner_10], color, 0.001f) &&
                 uishell_terminal_vec4_near(inst->colors[Corner_11], color, 0.001f))
              {
                result += 1;
              }
            }
          }
        }
      }
    }
  }
  return result;
}

internal U64
uishell_terminal_readback_visible_pixel_count_in_rect(R_Readback readback, Rng2S32 rect)
{
  U64 result = 0;
  if(readback.format == R_Tex2DFormat_BGRA8 && readback.data.size >= (U64)readback.size.x*(U64)readback.size.y*4)
  {
    rect.x0 = Clamp(0, rect.x0, readback.size.x);
    rect.x1 = Clamp(0, rect.x1, readback.size.x);
    rect.y0 = Clamp(0, rect.y0, readback.size.y);
    rect.y1 = Clamp(0, rect.y1, readback.size.y);
    U8 *data = readback.data.str;
    for(S32 y = rect.y0; y < rect.y1; y += 1)
    {
      for(S32 x = rect.x0; x < rect.x1; x += 1)
      {
        U64 idx = ((U64)y*(U64)readback.size.x + (U64)x)*4;
        U8 b = data[idx + 0];
        U8 g = data[idx + 1];
        U8 r = data[idx + 2];
        U8 a = data[idx + 3];
        U8 max_c = Max(r, Max(g, b));
        if(a > 0 && max_c > 32)
        {
          result += 1;
        }
      }
    }
  }
  return result;
}

internal U64
uishell_terminal_readback_visible_pixel_count(R_Readback readback)
{
  U64 result = uishell_terminal_readback_visible_pixel_count_in_rect(readback, r2s32p(0, 0, readback.size.x, readback.size.y));
  return result;
}

typedef struct UIShell_TerminalReadbackVisibleBounds UIShell_TerminalReadbackVisibleBounds;
struct UIShell_TerminalReadbackVisibleBounds
{
  B32 has_pixels;
  Rng2S32 rect;
};

internal UIShell_TerminalReadbackVisibleBounds
uishell_terminal_readback_visible_bounds_in_rect(R_Readback readback, Rng2S32 rect)
{
  UIShell_TerminalReadbackVisibleBounds result = {0};
  result.rect = r2s32p(max_S32, max_S32, min_S32, min_S32);
  if(readback.format == R_Tex2DFormat_BGRA8 && readback.data.size >= (U64)readback.size.x*(U64)readback.size.y*4)
  {
    rect.x0 = Clamp(0, rect.x0, readback.size.x);
    rect.x1 = Clamp(0, rect.x1, readback.size.x);
    rect.y0 = Clamp(0, rect.y0, readback.size.y);
    rect.y1 = Clamp(0, rect.y1, readback.size.y);
    U8 *data = readback.data.str;
    for(S32 y = rect.y0; y < rect.y1; y += 1)
    {
      for(S32 x = rect.x0; x < rect.x1; x += 1)
      {
        U64 idx = ((U64)y*(U64)readback.size.x + (U64)x)*4;
        U8 b = data[idx + 0];
        U8 g = data[idx + 1];
        U8 r = data[idx + 2];
        U8 a = data[idx + 3];
        U8 max_c = Max(r, Max(g, b));
        if(a > 0 && max_c > 32)
        {
          result.has_pixels = 1;
          result.rect.x0 = Min(result.rect.x0, x);
          result.rect.y0 = Min(result.rect.y0, y);
          result.rect.x1 = Max(result.rect.x1, x + 1);
          result.rect.y1 = Max(result.rect.y1, y + 1);
        }
      }
    }
  }
  if(!result.has_pixels)
  {
    result.rect = r2s32p(0, 0, 0, 0);
  }
  return result;
}

internal U64
uishell_terminal_readback_chromatic_pixel_count_in_rect(R_Readback readback, Rng2S32 rect)
{
  U64 result = 0;
  if(readback.format == R_Tex2DFormat_BGRA8 && readback.data.size >= (U64)readback.size.x*(U64)readback.size.y*4)
  {
    rect.x0 = Clamp(0, rect.x0, readback.size.x);
    rect.x1 = Clamp(0, rect.x1, readback.size.x);
    rect.y0 = Clamp(0, rect.y0, readback.size.y);
    rect.y1 = Clamp(0, rect.y1, readback.size.y);
    U8 *data = readback.data.str;
    for(S32 y = rect.y0; y < rect.y1; y += 1)
    {
      for(S32 x = rect.x0; x < rect.x1; x += 1)
      {
        U64 idx = ((U64)y*(U64)readback.size.x + (U64)x)*4;
        U8 b = data[idx + 0];
        U8 g = data[idx + 1];
        U8 r = data[idx + 2];
        U8 a = data[idx + 3];
        U8 max_c = Max(r, Max(g, b));
        U8 min_c = Min(r, Min(g, b));
        if(a > 0 && max_c > 32 && max_c > min_c + 16)
        {
          result += 1;
        }
      }
    }
  }
  return result;
}

internal B32
uishell_terminal_diagnostic_check_run_at_row_baseline_draw(FNT_Run run, Vec2F32 text_p, F32 row_descent, String8 label)
{
  B32 result = 1;
  Vec4F32 color = v4f32(0.25f, 0.60f, 0.85f, 0.70f);
  DR_Bucket *bucket = dr_bucket_make();
  DR_BucketScope(bucket)
  {
    uishell_terminal_draw_run_at_row_baseline(run, text_p, row_descent, color);
  }
  F32 advance = 0;
  U64 piece_idx = 0;
  for(R_PassNode *pass_node = bucket->passes.first; pass_node != 0; pass_node = pass_node->next)
  {
    R_Pass *pass = &pass_node->v;
    if(pass->kind == R_PassKind_UI && pass->params_ui != 0)
    {
      for(R_BatchGroup2DNode *group = pass->params_ui->rects.first; group != 0; group = group->next)
      {
        if(!r_handle_match(group->params.tex, r_handle_zero()))
        {
          for(R_BatchNode *batch_node = group->batches.first; batch_node != 0; batch_node = batch_node->next)
          {
            U64 inst_count = batch_node->v.byte_count/group->batches.bytes_per_inst;
            R_Rect2DInst *insts = (R_Rect2DInst *)batch_node->v.v;
            for(U64 inst_idx = 0; inst_idx < inst_count; inst_idx += 1)
            {
              for(; piece_idx < run.pieces.count;)
              {
                FNT_Piece *piece = &run.pieces.v[piece_idx];
                if(piece->draw_dim.x != 0 && piece->draw_dim.y != 0 && !r_handle_match(piece->texture, r_handle_zero()))
                {
                  break;
                }
                advance += piece->advance;
                piece_idx += 1;
              }
              if(piece_idx >= run.pieces.count)
              {
                log_user_errorf("terminal glyph diagnostics failed: %S emitted more textured instances than font pieces", label);
                result = 0;
                break;
              }
              FNT_Piece *piece = &run.pieces.v[piece_idx];
              Rng2F32 expected_dst =
              {
                text_p.x + advance - piece->origin_from_left,
                text_p.y - row_descent - piece->baseline_from_top,
                text_p.x + advance - piece->origin_from_left + piece->draw_dim.x,
                text_p.y - row_descent - piece->baseline_from_top + piece->draw_dim.y,
              };
              R_Rect2DInst *inst = &insts[inst_idx];
              if(abs_f32(inst->dst.x0 - expected_dst.x0) > 0.001f ||
                 abs_f32(inst->dst.y0 - expected_dst.y0) > 0.001f ||
                 abs_f32(inst->dst.x1 - expected_dst.x1) > 0.001f ||
                 abs_f32(inst->dst.y1 - expected_dst.y1) > 0.001f)
              {
                log_user_errorf("terminal glyph diagnostics failed: %S did not draw on the terminal row baseline", label);
                result = 0;
                break;
              }
              advance += piece->advance;
              piece_idx += 1;
            }
          }
        }
      }
    }
  }
  for(; piece_idx < run.pieces.count; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    if(piece->draw_dim.x != 0 && piece->draw_dim.y != 0 && !r_handle_match(piece->texture, r_handle_zero()))
    {
      log_user_errorf("terminal glyph diagnostics failed: %S emitted fewer textured instances than font pieces", label);
      result = 0;
      break;
    }
  }
  return result;
}

internal B32
uishell_terminal_diagnostic_check_terminal_row_baseline_draw(UIShell_TerminalCellTextDecision decision, F32 text_p_y, F32 row_descent, String8 label)
{
  Rng2F32 cell_rect = r2f32p(8, 0, 128, 48);
  B32 result = uishell_terminal_diagnostic_check_run_at_row_baseline_draw(decision.run, v2f32(cell_rect.x0, text_p_y), row_descent, label);
  return result;
}

internal B32
uishell_terminal_rect2_match(Rng2F32 a, Rng2F32 b, F32 epsilon)
{
  B32 result = (abs_f32(a.x0 - b.x0) <= epsilon &&
                abs_f32(a.y0 - b.y0) <= epsilon &&
                abs_f32(a.x1 - b.x1) <= epsilon &&
                abs_f32(a.y1 - b.y1) <= epsilon);
  return result;
}

internal U64
uishell_terminal_diagnostic_push_run_rects(Rng2F32 *rects, U64 count, U64 cap, FNT_Run run, Vec2F32 text_p, F32 row_descent)
{
  F32 advance = 0;
  for(U64 piece_idx = 0; piece_idx < run.pieces.count && count < cap; piece_idx += 1)
  {
    FNT_Piece *piece = &run.pieces.v[piece_idx];
    if(piece->draw_dim.x != 0 && piece->draw_dim.y != 0 && !r_handle_match(piece->texture, r_handle_zero()))
    {
      rects[count] = r2f32p(text_p.x + advance - piece->origin_from_left,
                            text_p.y - row_descent - piece->baseline_from_top,
                            text_p.x + advance - piece->origin_from_left + piece->draw_dim.x,
                            text_p.y - row_descent - piece->baseline_from_top + piece->draw_dim.y);
      count += 1;
    }
    advance += piece->advance;
  }
  return count;
}

internal U64
uishell_terminal_diagnostic_collect_textured_rects(DR_Bucket *bucket, Rng2F32 *rects, U64 cap)
{
  U64 count = 0;
  for(R_PassNode *pass_node = bucket->passes.first; pass_node != 0; pass_node = pass_node->next)
  {
    R_Pass *pass = &pass_node->v;
    if(pass->kind == R_PassKind_UI && pass->params_ui != 0)
    {
      for(R_BatchGroup2DNode *group = pass->params_ui->rects.first; group != 0; group = group->next)
      {
        if(!r_handle_match(group->params.tex, r_handle_zero()))
        {
          for(R_BatchNode *batch_node = group->batches.first; batch_node != 0; batch_node = batch_node->next)
          {
            U64 inst_count = batch_node->v.byte_count/group->batches.bytes_per_inst;
            R_Rect2DInst *insts = (R_Rect2DInst *)batch_node->v.v;
            for(U64 inst_idx = 0; inst_idx < inst_count && count < cap; inst_idx += 1)
            {
              rects[count] = insts[inst_idx].dst;
              count += 1;
            }
          }
        }
      }
    }
  }
  return count;
}

internal B32
uishell_terminal_diagnostic_check_rect_set(Rng2F32 *expected, U64 expected_count, Rng2F32 *actual, U64 actual_count, String8 label)
{
  B32 result = 1;
  B32 matched[128] = {0};
  if(actual_count > ArrayCount(matched))
  {
    log_user_errorf("terminal glyph diagnostics failed: %S emitted too many textured instances to compare", label);
    result = 0;
  }
  if(expected_count != actual_count)
  {
    log_user_errorf("terminal glyph diagnostics failed: %S emitted unexpected textured instance count (expected=%I64u actual=%I64u)", label, expected_count, actual_count);
    result = 0;
  }
  for(U64 expected_idx = 0; expected_idx < expected_count; expected_idx += 1)
  {
    B32 found = 0;
    for(U64 actual_idx = 0; actual_idx < actual_count && actual_idx < ArrayCount(matched); actual_idx += 1)
    {
      if(!matched[actual_idx] && uishell_terminal_rect2_match(expected[expected_idx], actual[actual_idx], 0.001f))
      {
        matched[actual_idx] = 1;
        found = 1;
        break;
      }
    }
    if(!found)
    {
      Rng2F32 r = expected[expected_idx];
      log_user_errorf("terminal glyph diagnostics failed: %S missing expected row-baseline text rectangle (%f,%f,%f,%f)", label, r.x0, r.y0, r.x1, r.y1);
      result = 0;
    }
  }
  return result;
}

typedef U32 UIShell_TerminalReadbackPixelClass;
enum
{
  UIShell_TerminalReadbackPixelClass_Dark,
  UIShell_TerminalReadbackPixelClass_Light,
  UIShell_TerminalReadbackPixelClass_GreenDominant,
  UIShell_TerminalReadbackPixelClass_RedDominant,
};

internal U64
uishell_terminal_readback_pixel_class_count_in_rect(R_Readback readback, Rng2S32 rect, UIShell_TerminalReadbackPixelClass pixel_class)
{
  U64 result = 0;
  if(readback.format == R_Tex2DFormat_BGRA8 && readback.data.size >= (U64)readback.size.x*(U64)readback.size.y*4)
  {
    rect.x0 = Clamp(0, rect.x0, readback.size.x);
    rect.x1 = Clamp(0, rect.x1, readback.size.x);
    rect.y0 = Clamp(0, rect.y0, readback.size.y);
    rect.y1 = Clamp(0, rect.y1, readback.size.y);
    U8 *data = readback.data.str;
    for(S32 y = rect.y0; y < rect.y1; y += 1)
    {
      for(S32 x = rect.x0; x < rect.x1; x += 1)
      {
        U64 idx = ((U64)y*(U64)readback.size.x + (U64)x)*4;
        U8 b = data[idx + 0];
        U8 g = data[idx + 1];
        U8 r = data[idx + 2];
        U8 a = data[idx + 3];
        U8 max_c = Max(r, Max(g, b));
        B32 match = 0;
        switch(pixel_class)
        {
          case UIShell_TerminalReadbackPixelClass_Dark:
          {
            match = (a > 0 && max_c <= 32);
          }break;
          case UIShell_TerminalReadbackPixelClass_Light:
          {
            match = (a > 0 && r >= 96 && g >= 96 && b >= 96);
          }break;
          case UIShell_TerminalReadbackPixelClass_GreenDominant:
          {
            match = (a > 0 && g >= 96 && g > r + 16 && g > b + 16);
          }break;
          case UIShell_TerminalReadbackPixelClass_RedDominant:
          {
            match = (a > 0 && r >= 96 && r > g + 16 && r > b + 16);
          }break;
        }
        if(match)
        {
          result += 1;
        }
      }
    }
  }
  return result;
}

internal Rng2S32
uishell_terminal_readback_cell_rect(F32 cell_width, F32 cell_height, F32 col0, F32 row0, F32 col1, F32 row1)
{
  Rng2S32 result = r2s32p((S32)floor_f32(cell_width*col0),
                          (S32)floor_f32(cell_height*row0),
                          (S32)ceil_f32(cell_width*col1),
                          (S32)ceil_f32(cell_height*row1));
  return result;
}

internal B32
uishell_terminal_write_fixture_ppm(String8 path, FNT_Tag primary_font, FNT_Tag main_fallback_font, F32 font_size, FNT_RasterFlags raster_flags, String8 **embedded_color_emoji_data, U64 embedded_color_emoji_count, String8 **embedded_fallback_data, U64 embedded_fallback_count)
{
  B32 result = 0;
  if(path.size != 0)
  {
    Temp scratch = scratch_begin(0, 0);
    UIShell_TerminalFontSet font_set = uishell_terminal_font_set_from_fonts(scratch.arena,
                                                                            primary_font,
                                                                            raster_flags,
                                                                            font_size,
                                                                            main_fallback_font,
                                                                            str8_zero(),
                                                                            embedded_color_emoji_data,
                                                                            embedded_color_emoji_count,
                                                                            embedded_fallback_data,
                                                                            embedded_fallback_count);
    UIShell_TerminalGlyphCache cache = {0};
    uishell_terminal_sync_font_cache(&cache, &font_set);
    UIShell_TerminalGlyphRenderer renderer =
    {
      .font_set = font_set,
      .cache = &cache,
    };
    cleat_snapshot fixture = uishell_terminal_fixture_snapshot(scratch.arena, 80, 24);
    FNT_Metrics metrics = fnt_metrics_from_tag_size(primary_font, font_size);
    F32 cell_width = Max(1.f, fnt_dim_from_tag_size_string(primary_font, font_size, 0, 0, str8_lit("H")).x);
    F32 cell_height = ceil_f32(ClampBot(1.f, fnt_line_height_from_metrics(&metrics)*1.2f));
    cleat_rgb fixture_bg = {4, 4, 4};
    UIShell_TerminalDrawParams draw_params =
    {
      .canvas_rect = r2f32p(0, 0, cell_width*(F32)fixture.cols, cell_height*(F32)fixture.rows),
      .background_color = uishell_terminal_rgba_from_rgb(fixture_bg),
      .cell_width_px = cell_width,
      .cell_height_px = cell_height,
    };
    UIShell_TerminalCursorArray cursors = uishell_terminal_fixture_cursor_array(scratch.arena, fixture.cols, fixture.rows);
    DR_Bucket *bucket = dr_bucket_make();
    DR_BucketScope(bucket)
    {
      uishell_terminal_glyph_renderer_draw_snapshot_with_cursors(scratch.arena, &renderer, &draw_params, &fixture, cursors);
    }
    Vec2S32 readback_size = v2s32((S32)ceil_f32(draw_params.canvas_rect.x1 - draw_params.canvas_rect.x0),
                                  (S32)ceil_f32(draw_params.canvas_rect.y1 - draw_params.canvas_rect.y0));
    R_Readback readback = r_pass_list_readback(scratch.arena, readback_size, &bucket->passes);
    if(readback.format == R_Tex2DFormat_BGRA8 &&
       readback.size.x > 0 &&
       readback.size.y > 0 &&
       readback.data.size >= (U64)readback.size.x*(U64)readback.size.y*4)
    {
      U64 pixel_count = (U64)readback.size.x*(U64)readback.size.y;
      U8 *rgb = push_array_no_zero(scratch.arena, U8, pixel_count*3);
      U8 *bgra = readback.data.str;
      for(U64 idx = 0; idx < pixel_count; idx += 1)
      {
        rgb[idx*3 + 0] = bgra[idx*4 + 2];
        rgb[idx*3 + 1] = bgra[idx*4 + 1];
        rgb[idx*3 + 2] = bgra[idx*4 + 0];
      }
      String8List output = {0};
      str8_list_push(scratch.arena, &output, push_str8f(scratch.arena, "P6\n%i %i\n255\n", readback.size.x, readback.size.y));
      str8_list_push(scratch.arena, &output, str8(rgb, pixel_count*3));
      result = write_data_list_to_file_path(path, output);
      if(result)
      {
        log_infof("terminal glyph fixture PPM written to %S", path);
      }
      else
      {
        log_user_errorf("terminal glyph fixture PPM failed to write to %S", path);
      }
    }
    else
    {
      log_user_errorf("terminal glyph fixture PPM readback unavailable (size=%ix%i format=%u bytes=%I64u)", readback.size.x, readback.size.y, readback.format, readback.data.size);
    }
    scratch_end(scratch);
  }
  return result;
}

internal B32
uishell_terminal_diagnostic_check_draw_command_color(UIShell_TerminalCellTextDecision decision, Vec4F32 draw_color, Vec4F32 expected_piece_color, String8 label)
{
  B32 result = 1;
  DR_Bucket *bucket = dr_bucket_make();
  DR_BucketScope(bucket)
  {
    uishell_terminal_draw_text_decision_in_cell(decision, r2f32p(0, 0, 64, 32), 24.f, decision.run.descent, draw_color);
  }
  U64 expected_color_count = uishell_terminal_render_bucket_textured_instance_count_with_color(bucket, expected_piece_color);
  if(expected_color_count == 0)
  {
    log_user_errorf("terminal glyph diagnostics failed: %S did not emit a textured draw command with the expected piece color", label);
    result = 0;
  }
  return result;
}

internal B32
uishell_terminal_glyph_diagnostics(FNT_Tag primary_font, FNT_Tag main_fallback_font, F32 font_size, FNT_RasterFlags raster_flags, String8 **embedded_color_emoji_data, U64 embedded_color_emoji_count, String8 **embedded_fallback_data, U64 embedded_fallback_count)
{
  B32 result = 1;
  if(fnt_tag_match(primary_font, fnt_tag_zero()))
  {
    log_user_errorf("terminal glyph diagnostics failed: primary font did not open");
    result = 0;
  }
  else
  {
    Temp scratch = scratch_begin(0, 0);
    UIShell_TerminalFontSet font_set = uishell_terminal_font_set_from_fonts(scratch.arena,
                                                                            primary_font,
                                                                            raster_flags,
                                                                            font_size,
                                                                            main_fallback_font,
                                                                            str8_zero(),
                                                                            embedded_color_emoji_data,
                                                                            embedded_color_emoji_count,
                                                                            embedded_fallback_data,
                                                                            embedded_fallback_count);
    UIShell_TerminalGlyphCache cache = {0};
    uishell_terminal_sync_font_cache(&cache, &font_set);
    UIShell_TerminalGlyphRenderer renderer =
    {
      .font_set = font_set,
      .cache = &cache,
    };
    FNT_Metrics primary_metrics = fnt_metrics_from_tag_size(primary_font, font_size);
    F32 diagnostic_text_y = 32.f;

    {
      typedef struct PresentationPolicyCase PresentationPolicyCase;
      struct PresentationPolicyCase
      {
        U32 codepoints[10];
        U64 codepoint_count;
        B32 expect_wants_source_color;
        String8 label;
      };
      PresentationPolicyCase policy_cases[] =
      {
        {{0x231A},                 1, 1, str8_lit_comp("Unicode Emoji_Presentation watch")},
        {{0x263A},                 1, 0, str8_lit_comp("Unicode text-default smiling face")},
        {{0x263A, 0xFE0E},         2, 0, str8_lit_comp("explicit text-presentation smiling face")},
        {{0x263A, 0xFE0F},         2, 1, str8_lit_comp("explicit emoji-presentation smiling face")},
        {{0x1F642},                1, 1, str8_lit_comp("Unicode Emoji_Presentation slightly smiling face")},
        {{0x0031, 0xFE0F, 0x20E3}, 3, 1, str8_lit_comp("Unicode emoji keycap sequence")},
        {{0x1F1EC, 0x1F1E7},       2, 1, str8_lit_comp("Unicode RGI flag sequence")},
        {{0x1F44B, 0x1F3FB},       2, 1, str8_lit_comp("Unicode RGI modifier sequence")},
      };
      for EachElement(case_idx, policy_cases)
      {
        PresentationPolicyCase *policy_case = &policy_cases[case_idx];
        cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(policy_case->codepoints, policy_case->codepoint_count, 0, CLEAT_CELL_WIDTH_NARROW);
        UIShell_TerminalGlyphKey key = uishell_terminal_glyph_key_from_cell(&cell);
        B32 wants_source_color = uishell_terminal_glyph_key_wants_color_emoji(key, cell.graphemes);
        if(wants_source_color != policy_case->expect_wants_source_color)
        {
          log_user_errorf("terminal glyph diagnostics failed: %S presentation policy mismatch", policy_case->label);
          result = 0;
        }
      }

      typedef struct SequencePolicyCase SequencePolicyCase;
      struct SequencePolicyCase
      {
        U32 codepoints[10];
        U64 codepoint_count;
        B32 expect_sequence_match;
        String8 label;
      };
      SequencePolicyCase sequence_cases[] =
      {
        {{0x0031, 0xFE0F, 0x20E3},                           3, 1, str8_lit_comp("exact emoji keycap sequence")},
        {{0x1F1EC, 0x1F1E7},                                  2, 1, str8_lit_comp("exact RGI flag sequence")},
        {{0x1F44B, 0x1F3FB},                                  2, 1, str8_lit_comp("exact RGI modifier sequence")},
        {{0x1F469, 0x200D, 0x1F4BB},                          3, 1, str8_lit_comp("exact RGI ZWJ technologist sequence")},
        {{0x1F469, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x200D, 0x1F466}, 7, 1, str8_lit_comp("exact RGI ZWJ family sequence")},
        {{0x0031, 0x20E3},                                    2, 0, str8_lit_comp("non-RGI keycap sequence without emoji presentation")},
        {{0x263A, 0x0301},                                    2, 0, str8_lit_comp("non-emoji combining text sequence")},
      };
      for EachElement(case_idx, sequence_cases)
      {
        SequencePolicyCase *sequence_case = &sequence_cases[case_idx];
        B32 sequence_match = uishell_terminal_codepoint_sequence_defaults_to_emoji(sequence_case->codepoints, sequence_case->codepoint_count);
        if(sequence_match != sequence_case->expect_sequence_match)
        {
          log_user_errorf("terminal glyph diagnostics failed: %S emoji sequence policy mismatch", sequence_case->label);
          result = 0;
        }
      }
    }

    typedef struct StyledNormalTextCase StyledNormalTextCase;
    struct StyledNormalTextCase
    {
      U32 flags;
      String8 label;
    };
    StyledNormalTextCase styled_cases[] =
    {
      {0, str8_lit_comp("plain normal text")},
      {CLEAT_CELL_FLAG_BOLD, str8_lit_comp("bold normal text")},
      {CLEAT_CELL_FLAG_FAINT, str8_lit_comp("faint normal text")},
      {CLEAT_CELL_FLAG_INVERSE, str8_lit_comp("inverse normal text")},
      {CLEAT_CELL_FLAG_UNDERLINE, str8_lit_comp("underline normal text")},
      {CLEAT_CELL_FLAG_STRIKETHROUGH, str8_lit_comp("strike normal text")},
      {CLEAT_CELL_FLAG_OVERLINE, str8_lit_comp("overline normal text")},
    };
    for EachElement(case_idx, styled_cases)
    {
      U32 codepoints[] = {'o'};
      cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(codepoints, ArrayCount(codepoints), styled_cases[case_idx].flags, CLEAT_CELL_WIDTH_NARROW);
      UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cell);
      if(!uishell_terminal_diagnostic_check_normal_text_decision(decision, styled_cases[case_idx].label))
      {
        result = 0;
      }
    }

    {
      Vec4F32 probe_color = v4f32(0.25f, 0.60f, 0.85f, 0.70f);
      Vec4F32 source_color_piece_color = v4f32(1.f, 1.f, 1.f, probe_color.w);
      U32 codepoints[] = {'o'};
      cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(codepoints, ArrayCount(codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
      UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cell);
      if(!uishell_terminal_diagnostic_check_draw_command_color(decision, probe_color, probe_color, str8_lit("normal mask text draw command color")))
      {
        result = 0;
      }
      DR_Bucket *bucket = dr_bucket_make();
      DR_BucketScope(bucket)
      {
        uishell_terminal_draw_text_decision_in_cell(decision, r2f32p(0, 0, 64, 32), 24.f, decision.run.descent, probe_color);
      }
      if(uishell_terminal_render_bucket_textured_instance_count_with_color(bucket, source_color_piece_color) != 0)
      {
        log_user_errorf("terminal glyph diagnostics failed: normal mask text emitted source-color draw tint");
        result = 0;
      }
    }

    {
      U32 text_presentation_codepoints[] = {0x263A, 0xFE0E};
      cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(text_presentation_codepoints, ArrayCount(text_presentation_codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
      UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cell);
      if(!uishell_terminal_diagnostic_check_normal_text_decision(decision, str8_lit("text-presentation emoji")))
      {
        result = 0;
      }
    }

    {
      U32 combining_codepoints[] = {'e', 0x0301};
      cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(combining_codepoints, ArrayCount(combining_codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
      UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cell);
      if(!(decision.raster_flags & FNT_RasterFlag_SinglePiece))
      {
        log_user_errorf("terminal glyph diagnostics failed: combining cluster did not request SinglePiece rastering");
        result = 0;
      }
      if(!uishell_terminal_diagnostic_check_normal_text_decision(decision, str8_lit("combining normal cluster")))
      {
        result = 0;
      }
    }

    {
      U32 longer_combining_codepoints[] = {'a', 0x0301, 0x0327};
      cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(longer_combining_codepoints, ArrayCount(longer_combining_codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
      UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cell);
      if(!(decision.raster_flags & FNT_RasterFlag_SinglePiece))
      {
        log_user_errorf("terminal glyph diagnostics failed: longer combining cluster did not request SinglePiece rastering");
        result = 0;
      }
      if(!uishell_terminal_diagnostic_check_normal_text_decision(decision, str8_lit("longer combining normal cluster")))
      {
        result = 0;
      }
    }

    {
      U32 cell0_codepoints[] = {'a'};
      U32 cell1_codepoints[] = {'b'};
      cleat_cell cells[2] =
      {
        uishell_terminal_diagnostic_cell_from_codepoints(cell0_codepoints, ArrayCount(cell0_codepoints), 0, CLEAT_CELL_WIDTH_NARROW),
        uishell_terminal_diagnostic_cell_from_codepoints(cell1_codepoints, ArrayCount(cell1_codepoints), 0, CLEAT_CELL_WIDTH_NARROW),
      };
      UIShell_TerminalCellFeed feed =
      {
        .cols = 2,
        .rows = 1,
        .cells = cells,
        .cell_count = ArrayCount(cells),
      };
      UIShell_TerminalCursorArray cursors = {0};
      FNT_Tag font0 = {0};
      FNT_Tag font1 = {0};
      Vec4F32 fg0 = {0};
      Vec4F32 fg1 = {0};
      String8 string0 = {0};
      String8 string1 = {0};
      B32 cell0_is_run = uishell_terminal_cell_text_run_info(scratch.arena, &renderer, &feed, ArrayCount(cells), cursors, 0, 0, &font0, &fg0, &string0);
      B32 cell1_is_run = uishell_terminal_cell_text_run_info(scratch.arena, &renderer, &feed, ArrayCount(cells), cursors, 0, 1, &font1, &fg1, &string1);
      if(!cell0_is_run || !cell1_is_run || !fnt_tag_match(font0, font1) || !uishell_terminal_vec4_match(fg0, fg1))
      {
        log_user_errorf("terminal glyph diagnostics failed: adjacent ASCII cells did not classify as one compatible normal text run");
        result = 0;
      }
      else
      {
        String8List parts = {0};
        str8_list_push(scratch.arena, &parts, string0);
        str8_list_push(scratch.arena, &parts, string1);
        String8 joined = str8_list_join(scratch.arena, &parts, 0);
        FNT_Run joined_run = dr_fnt_run_from_string(font0, font_size, 0, 0, raster_flags, joined);
        if(!uishell_terminal_diagnostic_check_run_at_row_baseline_draw(joined_run, v2f32(8.f, diagnostic_text_y), primary_metrics.descent, str8_lit("batched ASCII terminal row baseline")))
        {
          result = 0;
        }
      }
    }

    {
      U32 fallback_candidates[] = {0x23CE, 0x2388, 0x25CC, 0x2300, 0x21AF};
      B32 found_fallback_case = 0;
      B32 found_fallback_cluster_case = 0;
      B32 found_shaped_fallback_cluster_case = 0;
      B32 found_complex_shaped_fallback_cluster_case = 0;
      for EachElement(candidate_idx, fallback_candidates)
      {
        U32 codepoints[] = {fallback_candidates[candidate_idx]};
        cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(codepoints, ArrayCount(codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
        UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cell);
        if(decision.path == UIShell_TerminalCellTextPath_NormalMask && !fnt_tag_match(decision.font, primary_font))
        {
          found_fallback_case = 1;
          if(!uishell_terminal_diagnostic_check_normal_text_decision(decision, str8_lit("fallback normal text")))
          {
            result = 0;
          }
          if(!uishell_terminal_diagnostic_check_terminal_row_baseline_draw(decision, diagnostic_text_y, primary_metrics.descent, str8_lit("fallback normal text terminal row baseline")))
          {
            result = 0;
          }
          U32 cluster_codepoints[] = {fallback_candidates[candidate_idx], 0xFE0E};
          cleat_cell cluster_cell = uishell_terminal_diagnostic_cell_from_codepoints(cluster_codepoints, ArrayCount(cluster_codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
          UIShell_TerminalCellTextDecision cluster_decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cluster_cell);
          if(cluster_decision.path != UIShell_TerminalCellTextPath_Missing && !fnt_tag_match(cluster_decision.font, primary_font))
          {
            found_fallback_cluster_case = 1;
            if(!(cluster_decision.raster_flags & FNT_RasterFlag_SinglePiece))
            {
              log_user_errorf("terminal glyph diagnostics failed: fallback text-presentation cluster did not request SinglePiece rastering");
              result = 0;
            }
            if(!uishell_terminal_diagnostic_check_normal_text_decision(cluster_decision, str8_lit("fallback text-presentation cluster")))
            {
              result = 0;
            }
            if(!uishell_terminal_diagnostic_check_terminal_row_baseline_draw(cluster_decision, diagnostic_text_y, primary_metrics.descent, str8_lit("fallback text-presentation cluster terminal row baseline")))
            {
              result = 0;
            }
          }
          break;
        }
      }
      if(!found_fallback_case)
      {
        log_user_errorf("terminal glyph diagnostics failed: could not find a normal-text fallback glyph from embedded terminal fallbacks");
        result = 0;
      }
      if(!found_fallback_cluster_case)
      {
        log_user_errorf("terminal glyph diagnostics failed: could not find a normal-text fallback cluster from embedded terminal fallbacks");
        result = 0;
      }
      {
        U32 shaped_cluster_codepoints[] = {0x2388, 0x0301};
        cleat_cell shaped_cluster_cell = uishell_terminal_diagnostic_cell_from_codepoints(shaped_cluster_codepoints, ArrayCount(shaped_cluster_codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
        UIShell_TerminalCellTextDecision shaped_cluster_decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &shaped_cluster_cell);
        if(shaped_cluster_decision.path != UIShell_TerminalCellTextPath_Missing && !fnt_tag_match(shaped_cluster_decision.font, primary_font))
        {
          found_shaped_fallback_cluster_case = 1;
          if(!(shaped_cluster_decision.raster_flags & FNT_RasterFlag_SinglePiece))
          {
            log_user_errorf("terminal glyph diagnostics failed: shaped fallback cluster did not request SinglePiece rastering");
            result = 0;
          }
          if(!uishell_terminal_diagnostic_check_normal_text_decision(shaped_cluster_decision, str8_lit("shaped fallback combining cluster")))
          {
            result = 0;
          }
          if(!uishell_terminal_diagnostic_check_terminal_row_baseline_draw(shaped_cluster_decision, diagnostic_text_y, primary_metrics.descent, str8_lit("shaped fallback combining cluster terminal row baseline")))
          {
            result = 0;
          }
        }
      }
      if(!found_shaped_fallback_cluster_case)
      {
        log_user_errorf("terminal glyph diagnostics failed: could not resolve shaped fallback combining cluster through embedded terminal fallbacks");
        result = 0;
      }
      {
        struct ComplexShapedFallbackCase
        {
          U32 codepoints[4];
          U64 codepoint_count;
          String8 label;
        };
        struct ComplexShapedFallbackCase complex_cases[] =
        {
          {{0x2300, 0x20DD, 0x20DE, 0}, 3, str8_lit_comp("complex shaped fallback enclosing cluster")},
          {{0x2300, 0x20DD, 0x20DF, 0}, 3, str8_lit_comp("complex shaped fallback double-enclosing cluster")},
          {{0x23CE, 0x0338, 0xFE0E, 0}, 3, str8_lit_comp("complex shaped fallback slash cluster")},
        };
        for EachElement(case_idx, complex_cases)
        {
          struct ComplexShapedFallbackCase *complex_case = &complex_cases[case_idx];
          B32 primary_has_all = 1;
          for(U64 codepoint_idx = 0; codepoint_idx < complex_case->codepoint_count; codepoint_idx += 1)
          {
            U32 codepoint = complex_case->codepoints[codepoint_idx];
            if(!uishell_terminal_codepoint_is_variation_selector(codepoint) &&
               !fnt_tag_has_codepoint(primary_font, codepoint))
            {
              primary_has_all = 0;
              break;
            }
          }
          if(primary_has_all)
          {
            continue;
          }
          B32 fallback_has_all = 0;
          for(U64 fallback_idx = 0; fallback_idx < font_set.fallback_font_count; fallback_idx += 1)
          {
            FNT_Tag fallback_font = font_set.fallback_fonts[fallback_idx];
            fallback_has_all = 1;
            for(U64 codepoint_idx = 0; codepoint_idx < complex_case->codepoint_count; codepoint_idx += 1)
            {
              U32 codepoint = complex_case->codepoints[codepoint_idx];
              if(!uishell_terminal_codepoint_is_variation_selector(codepoint) &&
                 !fnt_tag_has_codepoint(fallback_font, codepoint))
              {
                fallback_has_all = 0;
                break;
              }
            }
            if(fallback_has_all)
            {
              break;
            }
          }
          if(!fallback_has_all)
          {
            continue;
          }
          cleat_cell complex_cell = uishell_terminal_diagnostic_cell_from_codepoints(complex_case->codepoints, complex_case->codepoint_count, 0, CLEAT_CELL_WIDTH_NARROW);
          UIShell_TerminalCellTextDecision complex_decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &complex_cell);
          if(complex_decision.path != UIShell_TerminalCellTextPath_Missing && !fnt_tag_match(complex_decision.font, primary_font))
          {
            found_complex_shaped_fallback_cluster_case = 1;
            if(!(complex_decision.raster_flags & FNT_RasterFlag_SinglePiece))
            {
              log_user_errorf("terminal glyph diagnostics failed: %S did not request SinglePiece rastering", complex_case->label);
              result = 0;
            }
            if(!uishell_terminal_diagnostic_check_normal_text_decision(complex_decision, complex_case->label))
            {
              result = 0;
            }
            if(!uishell_terminal_diagnostic_check_terminal_row_baseline_draw(complex_decision, diagnostic_text_y, primary_metrics.descent, str8_lit("complex shaped fallback cluster terminal row baseline")))
            {
              result = 0;
            }
            break;
          }
        }
      }
      if(!found_complex_shaped_fallback_cluster_case)
      {
        log_user_errorf("terminal glyph diagnostics failed: could not resolve a complex shaped fallback cluster through embedded terminal fallbacks");
        result = 0;
      }
    }

    {
      B32 color_emoji_can_raster_source_color = 0;
      U32 smile_codepoints[] = {0x1F642};
      String8 smile = uishell_terminal_diagnostic_string_from_codepoints(scratch.arena, smile_codepoints, ArrayCount(smile_codepoints));
      for(U64 font_idx = 0; font_idx < font_set.color_emoji_font_count; font_idx += 1)
      {
        FNT_Tag color_font = font_set.color_emoji_fonts[font_idx];
        FNT_Run emoji_run = dr_fnt_run_from_string(color_font, font_size, 0, 0, raster_flags|FNT_RasterFlag_TightBounds|FNT_RasterFlag_SinglePiece, smile);
        if(uishell_terminal_run_has_source_color(emoji_run))
        {
          color_emoji_can_raster_source_color = 1;
          break;
        }
      }
      cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(smile_codepoints, ArrayCount(smile_codepoints), 0, CLEAT_CELL_WIDTH_WIDE);
      UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cell);
      if(!decision.wants_source_color_cell)
      {
        log_user_errorf("terminal glyph diagnostics failed: emoji-default cell did not request a source-color font path");
        result = 0;
      }
      if(color_emoji_can_raster_source_color && decision.path != UIShell_TerminalCellTextPath_SourceColor)
      {
        log_user_errorf("terminal glyph diagnostics failed: source-color-capable emoji cell did not use the source-color path");
        result = 0;
      }
      if(!color_emoji_can_raster_source_color)
      {
#if OS_MAC
        log_user_errorf("terminal glyph diagnostics failed: macOS did not raster U+1F642 through a configured source-color emoji font");
        result = 0;
#else
        log_infof("terminal glyph diagnostics: no configured color emoji font rastered U+1F642 as source-color on this platform");
#endif
      }
      if(decision.path == UIShell_TerminalCellTextPath_SourceColor)
      {
        if(!uishell_terminal_diagnostic_check_run_placement(decision.run, str8_lit("source-color emoji")))
        {
          result = 0;
        }
        if(!uishell_terminal_diagnostic_check_run_draw_contract(decision.run, str8_lit("source-color emoji")))
        {
          result = 0;
        }
        Vec4F32 probe_color = v4f32(0.25f, 0.60f, 0.85f, 0.70f);
        Vec4F32 source_color_piece_color = v4f32(1.f, 1.f, 1.f, probe_color.w);
        if(!uishell_terminal_diagnostic_check_draw_command_color(decision, probe_color, source_color_piece_color, str8_lit("source-color emoji draw command color")))
        {
          result = 0;
        }
        if(!uishell_terminal_diagnostic_check_terminal_row_baseline_draw(decision, diagnostic_text_y, primary_metrics.descent, str8_lit("source-color emoji terminal row baseline")))
        {
          result = 0;
        }
        DR_Bucket *bucket = dr_bucket_make();
        DR_BucketScope(bucket)
        {
          uishell_terminal_draw_text_decision_in_cell(decision, r2f32p(0, 0, 64, 32), 24.f, decision.run.descent, probe_color);
        }
        if(uishell_terminal_render_bucket_textured_instance_count_with_color(bucket, probe_color) != 0)
        {
          log_user_errorf("terminal glyph diagnostics failed: source-color emoji was foreground-tinted in draw commands");
          result = 0;
        }
      }

      U32 cloud_codepoints[] = {0x2601, 0xFE0F};
      cleat_cell cloud_cell = uishell_terminal_diagnostic_cell_from_codepoints(cloud_codepoints, ArrayCount(cloud_codepoints), 0, CLEAT_CELL_WIDTH_WIDE);
      UIShell_TerminalCellTextDecision cloud_decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cloud_cell);
      if(!cloud_decision.wants_source_color_cell)
      {
        log_user_errorf("terminal glyph diagnostics failed: U+2601 U+FE0F did not request a source-color font path");
        result = 0;
      }
      if(color_emoji_can_raster_source_color && cloud_decision.path != UIShell_TerminalCellTextPath_SourceColor)
      {
        log_user_errorf("terminal glyph diagnostics failed: U+2601 U+FE0F did not use the source-color path");
        result = 0;
      }
      if(cloud_decision.path == UIShell_TerminalCellTextPath_SourceColor)
      {
        Rng2F32 cloud_cell_rect = r2f32p(0, 0, 64, 32);
        Vec2F32 cloud_text_p = uishell_terminal_source_color_text_p_for_cell(cloud_decision, cloud_cell_rect, 24.f, cloud_decision.run.descent);
        Rng2F32 cloud_visible_rect = uishell_terminal_run_visible_rect_at_row_baseline(cloud_decision.run, cloud_text_p, cloud_decision.run.descent);
        F32 cloud_visible_center = (cloud_visible_rect.x0 + cloud_visible_rect.x1)*0.5f;
        F32 cloud_cell_center = (cloud_cell_rect.x0 + cloud_cell_rect.x1)*0.5f;
        if(abs_f32(cloud_visible_center - cloud_cell_center) > 0.501f)
        {
          log_user_errorf("terminal glyph diagnostics failed: U+2601 U+FE0F source-color bounds were not centered in the terminal cell span");
          result = 0;
        }
      }
    }

    {
      U32 mixed_probe_codepoints[] = {0x1F642, 'x'};
      cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(mixed_probe_codepoints, ArrayCount(mixed_probe_codepoints), 0, CLEAT_CELL_WIDTH_WIDE);
      UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cell);
      if(decision.path == UIShell_TerminalCellTextPath_SourceColor &&
         uishell_terminal_run_has_source_color(decision.run) &&
         uishell_terminal_run_has_mask(decision.run))
      {
        if(!uishell_terminal_diagnostic_check_run_placement(decision.run, str8_lit("mixed source-color/mask cluster")))
        {
          result = 0;
        }
        if(!uishell_terminal_diagnostic_check_run_draw_contract(decision.run, str8_lit("mixed source-color/mask cluster")))
        {
          result = 0;
        }
        if(!uishell_terminal_diagnostic_check_terminal_row_baseline_draw(decision, diagnostic_text_y, primary_metrics.descent, str8_lit("mixed source-color/mask cluster terminal row baseline")))
        {
          result = 0;
        }
      }
      else
      {
        log_infof("terminal glyph diagnostics: mixed source-color/mask cluster probe did not produce a mixed run on this platform");
      }
    }

    {
      U32 normal_codepoints[] = {'o'};
      U32 emoji_codepoints[] = {0x1F642};
      cleat_cell normal_cell = uishell_terminal_diagnostic_cell_from_codepoints(normal_codepoints, ArrayCount(normal_codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
      cleat_cell emoji_cell = uishell_terminal_diagnostic_cell_from_codepoints(emoji_codepoints, ArrayCount(emoji_codepoints), 0, CLEAT_CELL_WIDTH_WIDE);
      UIShell_TerminalCellTextDecision normal_decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &normal_cell);
      UIShell_TerminalCellTextDecision emoji_decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &emoji_cell);
      if(normal_decision.path == UIShell_TerminalCellTextPath_NormalMask &&
         emoji_decision.path == UIShell_TerminalCellTextPath_SourceColor &&
         normal_decision.run.pieces.count != 0 &&
         emoji_decision.run.pieces.count != 0)
      {
        U64 mixed_piece_count = normal_decision.run.pieces.count + emoji_decision.run.pieces.count;
        FNT_Piece *mixed_pieces = push_array(scratch.arena, FNT_Piece, mixed_piece_count);
        MemoryCopy(mixed_pieces, normal_decision.run.pieces.v, sizeof(FNT_Piece)*normal_decision.run.pieces.count);
        MemoryCopy(mixed_pieces + normal_decision.run.pieces.count, emoji_decision.run.pieces.v, sizeof(FNT_Piece)*emoji_decision.run.pieces.count);
        FNT_Run mixed_run =
        {
          .pieces = {mixed_pieces, mixed_piece_count},
          .ascent = Max(normal_decision.run.ascent, emoji_decision.run.ascent),
          .descent = Max(normal_decision.run.descent, emoji_decision.run.descent),
        };
        for(U64 piece_idx = 0; piece_idx < mixed_piece_count; piece_idx += 1)
        {
          mixed_run.dim.x += mixed_pieces[piece_idx].advance;
          mixed_run.dim.y = Max(mixed_run.dim.y, mixed_pieces[piece_idx].draw_dim.y);
        }
        Vec4F32 probe_color = v4f32(0.25f, 0.60f, 0.85f, 0.70f);
        Vec4F32 source_color_piece_color = v4f32(1.f, 1.f, 1.f, probe_color.w);
        DR_Bucket *bucket = dr_bucket_make();
        DR_BucketScope(bucket)
        {
          dr_text_run(v2f32(0, 32), probe_color, mixed_run);
        }
        if(uishell_terminal_render_bucket_textured_instance_count_with_color(bucket, probe_color) == 0 ||
           uishell_terminal_render_bucket_textured_instance_count_with_color(bucket, source_color_piece_color) == 0)
        {
          log_user_errorf("terminal glyph diagnostics failed: synthetic mixed mask/RGBA run did not emit both mask-tinted and source-color textured draw commands");
          result = 0;
        }
      }
      else
      {
        log_infof("terminal glyph diagnostics: synthetic mixed mask/RGBA run skipped because source-color emoji was unavailable");
      }
    }

    {
      enum { MixedRowCols = 16 };
      U32 invisible_space[] = {' '};
      cleat_cell cells[MixedRowCols] = {0};
      for(U64 idx = 0; idx < ArrayCount(cells); idx += 1)
      {
        cells[idx] = uishell_terminal_diagnostic_cell_from_codepoints(invisible_space, ArrayCount(invisible_space), CLEAT_CELL_FLAG_INVISIBLE, CLEAT_CELL_WIDTH_NARROW);
      }

      U32 a_cp[] = {'a'};
      U32 b_cp[] = {'b'};
      U32 c_cp[] = {'c'};
      U32 d_cp[] = {'d'};
      U32 e_cp[] = {'e'};
      U32 smile_cp[] = {0x1F642};
      cleat_rgb red = {255, 96, 96};
      cells[0] = uishell_terminal_diagnostic_cell_from_codepoints(a_cp, ArrayCount(a_cp), 0, CLEAT_CELL_WIDTH_NARROW);
      cells[1] = uishell_terminal_diagnostic_cell_from_codepoints(b_cp, ArrayCount(b_cp), 0, CLEAT_CELL_WIDTH_NARROW);
      cells[3] = uishell_terminal_diagnostic_cell_from_codepoints(c_cp, ArrayCount(c_cp), 0, CLEAT_CELL_WIDTH_NARROW);
      cells[3].fg = red;
      cells[5] = uishell_terminal_diagnostic_cell_from_codepoints(d_cp, ArrayCount(d_cp), CLEAT_CELL_FLAG_BOLD, CLEAT_CELL_WIDTH_NARROW);
      cells[7] = uishell_terminal_diagnostic_cell_from_codepoints(e_cp, ArrayCount(e_cp), CLEAT_CELL_FLAG_FAINT, CLEAT_CELL_WIDTH_NARROW);
      cells[11] = uishell_terminal_diagnostic_cell_from_codepoints(smile_cp, ArrayCount(smile_cp), 0, CLEAT_CELL_WIDTH_WIDE);
      cells[12].width = CLEAT_CELL_WIDTH_SPACER_TAIL;

      U32 fallback_candidates[] = {0x23CE, 0x2388, 0x25CC, 0x2300, 0x21AF};
      U32 fallback_cp = 0;
      for EachElement(candidate_idx, fallback_candidates)
      {
        U32 codepoints[] = {fallback_candidates[candidate_idx]};
        cleat_cell cell = uishell_terminal_diagnostic_cell_from_codepoints(codepoints, ArrayCount(codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
        UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cell);
        if(decision.path == UIShell_TerminalCellTextPath_NormalMask && !fnt_tag_match(decision.font, primary_font))
        {
          fallback_cp = fallback_candidates[candidate_idx];
          break;
        }
      }
      if(fallback_cp == 0)
      {
        log_user_errorf("terminal glyph diagnostics failed: mixed row could not find an embedded fallback glyph");
        result = 0;
      }
      else
      {
        U32 fallback_codepoints[] = {fallback_cp};
        cells[9] = uishell_terminal_diagnostic_cell_from_codepoints(fallback_codepoints, ArrayCount(fallback_codepoints), 0, CLEAT_CELL_WIDTH_NARROW);
      }

      UIShell_TerminalCellFeed feed =
      {
        .cols = MixedRowCols,
        .rows = 1,
        .cells = cells,
        .cell_count = ArrayCount(cells),
      };
      F32 cell_width = Max(1.f, fnt_dim_from_tag_size_string(primary_font, font_size, 0, 0, str8_lit("H")).x);
      F32 cell_height = ceil_f32(ClampBot(1.f, fnt_line_height_from_metrics(&primary_metrics)*1.2f));
      Rng2F32 canvas_rect = r2f32p(0, 0, cell_width*(F32)MixedRowCols, cell_height);
      F32 text_y = floor_f32((canvas_rect.y0 + canvas_rect.y1)/2.f + primary_metrics.ascent/2.f - primary_metrics.descent/2.f);
      UIShell_TerminalDrawParams draw_params =
      {
        .canvas_rect = canvas_rect,
        .background_color = uishell_terminal_rgba_from_rgb(cells[0].bg),
        .cell_width_px = cell_width,
        .cell_height_px = cell_height,
      };
      UIShell_TerminalCursorArray cursors = {0};

      Rng2F32 expected[128] = {0};
      U64 expected_count = 0;
      {
        FNT_Run run = dr_fnt_run_from_string(uishell_terminal_font_from_cell(&renderer, &cells[0]), font_size, 0, 0, raster_flags, str8_lit("ab"));
        expected_count = uishell_terminal_diagnostic_push_run_rects(expected, expected_count, ArrayCount(expected), run, v2f32(canvas_rect.x0 + cell_width*0.f, text_y), primary_metrics.descent);
      }
      {
        FNT_Run run = dr_fnt_run_from_string(uishell_terminal_font_from_cell(&renderer, &cells[3]), font_size, 0, 0, raster_flags, str8_lit("c"));
        expected_count = uishell_terminal_diagnostic_push_run_rects(expected, expected_count, ArrayCount(expected), run, v2f32(canvas_rect.x0 + cell_width*3.f, text_y), primary_metrics.descent);
      }
      {
        UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cells[5]);
        if(decision.path == UIShell_TerminalCellTextPath_Missing)
        {
          log_user_errorf("terminal glyph diagnostics failed: mixed row bold cell did not resolve to text");
          result = 0;
        }
        else
        {
          expected_count = uishell_terminal_diagnostic_push_run_rects(expected, expected_count, ArrayCount(expected), decision.run, v2f32(canvas_rect.x0 + cell_width*5.f, text_y), primary_metrics.descent);
          if(decision.path != UIShell_TerminalCellTextPath_SourceColor)
          {
            expected_count = uishell_terminal_diagnostic_push_run_rects(expected, expected_count, ArrayCount(expected), decision.run, v2f32(canvas_rect.x0 + cell_width*5.f + 1.f, text_y), primary_metrics.descent);
          }
        }
      }
      {
        FNT_Run run = dr_fnt_run_from_string(uishell_terminal_font_from_cell(&renderer, &cells[7]), font_size, 0, 0, raster_flags, str8_lit("e"));
        expected_count = uishell_terminal_diagnostic_push_run_rects(expected, expected_count, ArrayCount(expected), run, v2f32(canvas_rect.x0 + cell_width*7.f, text_y), primary_metrics.descent);
      }
      if(fallback_cp != 0)
      {
        UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cells[9]);
        if(decision.path == UIShell_TerminalCellTextPath_Missing)
        {
          log_user_errorf("terminal glyph diagnostics failed: mixed row fallback cell did not resolve to text");
          result = 0;
        }
        else
        {
          expected_count = uishell_terminal_diagnostic_push_run_rects(expected, expected_count, ArrayCount(expected), decision.run, v2f32(canvas_rect.x0 + cell_width*9.f, text_y), primary_metrics.descent);
        }
      }
      {
        UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, &cells[11]);
        if(decision.path == UIShell_TerminalCellTextPath_Missing)
        {
          log_user_errorf("terminal glyph diagnostics failed: mixed row emoji cell did not resolve to text");
          result = 0;
        }
        else
        {
          Rng2F32 emoji_cell_rect = r2f32p(canvas_rect.x0 + cell_width*11.f,
                                           canvas_rect.y0,
                                           canvas_rect.x0 + cell_width*13.f,
                                           canvas_rect.y1);
          Vec2F32 emoji_text_p = (decision.path == UIShell_TerminalCellTextPath_SourceColor ?
                                  uishell_terminal_source_color_text_p_for_cell(decision, emoji_cell_rect, text_y, primary_metrics.descent) :
                                  v2f32(emoji_cell_rect.x0, text_y));
          expected_count = uishell_terminal_diagnostic_push_run_rects(expected, expected_count, ArrayCount(expected), decision.run, emoji_text_p, primary_metrics.descent);
        }
      }

      DR_Bucket *bucket = dr_bucket_make();
      DR_BucketScope(bucket)
      {
        uishell_terminal_glyph_renderer_draw_cell_feed_with_cursors(scratch.arena, &renderer, &draw_params, &feed, cursors);
      }
      Rng2F32 raw_actual[128] = {0};
      Rng2F32 actual[128] = {0};
      U64 raw_actual_count = uishell_terminal_diagnostic_collect_textured_rects(bucket, raw_actual, ArrayCount(raw_actual));
      U64 actual_count = 0;
      for(U64 actual_idx = 0; actual_idx < raw_actual_count && actual_count < ArrayCount(actual); actual_idx += 1)
      {
        if(!uishell_terminal_rect2_match(raw_actual[actual_idx], canvas_rect, 0.001f))
        {
          actual[actual_count] = raw_actual[actual_idx];
          actual_count += 1;
        }
      }
      if(!uishell_terminal_diagnostic_check_rect_set(expected, expected_count, actual, actual_count, str8_lit("mixed terminal row renderer baseline")))
      {
        result = 0;
      }
    }

    {
      String8 baseline_probe = str8_lit("oooo");
      FNT_Run normal_run = dr_fnt_run_from_string(primary_font, font_size, 0, 0, raster_flags, baseline_probe);
      FNT_Run tight_run = dr_fnt_run_from_string(primary_font, font_size, 0, 0, raster_flags|FNT_RasterFlag_TightBounds, baseline_probe);
      DR_Bucket *bucket = dr_bucket_make();
      DR_BucketScope(bucket)
      {
        dr_text_run(v2f32(8, 56), v4f32(1, 0.25f, 0.25f, 1), normal_run);
        dr_text_run(v2f32(96, 56), v4f32(0.25f, 1, 0.25f, 1), tight_run);
      }
      R_Readback readback = r_pass_list_readback(scratch.arena, v2s32(176, 80), &bucket->passes);
      if(readback.data.size == 0)
      {
#if OS_MAC
        log_user_errorf("terminal glyph diagnostics failed: baseline contract readback returned no pixels");
        result = 0;
#else
        log_infof("terminal glyph diagnostics: baseline contract readback is not implemented on this platform");
#endif
      }
      else
      {
        UIShell_TerminalReadbackVisibleBounds normal_bounds = uishell_terminal_readback_visible_bounds_in_rect(readback, r2s32p(0, 0, 88, 80));
        UIShell_TerminalReadbackVisibleBounds tight_bounds = uishell_terminal_readback_visible_bounds_in_rect(readback, r2s32p(88, 0, 176, 80));
        if(!normal_bounds.has_pixels || !tight_bounds.has_pixels ||
           abs_s64((S64)normal_bounds.rect.y0 - (S64)tight_bounds.rect.y0) > 1 ||
           abs_s64((S64)normal_bounds.rect.y1 - (S64)tight_bounds.rect.y1) > 1)
        {
          log_user_errorf("terminal glyph diagnostics failed: tight-bound and face-box text do not preserve baseline-equivalent ink bounds (normal=%i,%i,%i,%i tight=%i,%i,%i,%i)",
                          normal_bounds.rect.x0,
                          normal_bounds.rect.y0,
                          normal_bounds.rect.x1,
                          normal_bounds.rect.y1,
                          tight_bounds.rect.x0,
                          tight_bounds.rect.y0,
                          tight_bounds.rect.x1,
                          tight_bounds.rect.y1);
          result = 0;
        }
      }
    }

    {
      F32 one_o_advance = Max(1.f, fnt_dim_from_tag_size_string(primary_font, font_size, 0, 0, str8_lit("o")).x);
      DR_Bucket *bucket = dr_bucket_make();
      DR_BucketScope(bucket)
      {
        dr_text(primary_font, font_size, 0, 0, raster_flags, v2f32(8, 56), v4f32(1, 0.25f, 0.25f, 1), str8_lit("oooo"));
        for(U64 idx = 0; idx < 4; idx += 1)
        {
          dr_text(primary_font, font_size, 0, 0, raster_flags, v2f32(96 + one_o_advance*(F32)idx, 56), v4f32(0.25f, 1, 0.25f, 1), str8_lit("o"));
        }
      }
      R_Readback readback = r_pass_list_readback(scratch.arena, v2s32(176, 80), &bucket->passes);
      if(readback.data.size == 0)
      {
#if OS_MAC
        log_user_errorf("terminal glyph diagnostics failed: joined-vs-split text readback returned no pixels");
        result = 0;
#else
        log_infof("terminal glyph diagnostics: joined-vs-split text readback is not implemented on this platform");
#endif
      }
      else
      {
        UIShell_TerminalReadbackVisibleBounds joined_bounds = uishell_terminal_readback_visible_bounds_in_rect(readback, r2s32p(0, 0, 88, 80));
        UIShell_TerminalReadbackVisibleBounds split_bounds = uishell_terminal_readback_visible_bounds_in_rect(readback, r2s32p(88, 0, 176, 80));
        if(!joined_bounds.has_pixels || !split_bounds.has_pixels ||
           abs_s64((S64)joined_bounds.rect.y0 - (S64)split_bounds.rect.y0) > 1 ||
           abs_s64((S64)joined_bounds.rect.y1 - (S64)split_bounds.rect.y1) > 1)
        {
          log_user_errorf("terminal glyph diagnostics failed: joined and split text runs do not preserve baseline-equivalent ink bounds (joined=%i,%i,%i,%i split=%i,%i,%i,%i)",
                          joined_bounds.rect.x0,
                          joined_bounds.rect.y0,
                          joined_bounds.rect.x1,
                          joined_bounds.rect.y1,
                          split_bounds.rect.x0,
                          split_bounds.rect.y0,
                          split_bounds.rect.x1,
                          split_bounds.rect.y1);
          result = 0;
        }
      }
    }

    {
      enum { SegmentCols = 58, SegmentGroupCount = 2 };
      U32 invisible_space[] = {' '};
      cleat_cell cells[SegmentCols] = {0};
      for(U64 idx = 0; idx < ArrayCount(cells); idx += 1)
      {
        cells[idx] = uishell_terminal_diagnostic_cell_from_codepoints(invisible_space, ArrayCount(invisible_space), CLEAT_CELL_FLAG_INVISIBLE, CLEAT_CELL_WIDTH_NARROW);
      }

      cleat_rgb bg = {4, 4, 4};
      cleat_rgb fg = {188, 209, 191};
      cleat_rgb cyan = {96, 210, 230};
      cleat_rgb gold = {238, 198, 95};
      struct SegmentCase
      {
        U64 col;
        String8 text;
        cleat_rgb fg;
        U32 flags;
        U32 group_idx;
        String8 label;
      }
      segment_cases[] =
      {
        {0,  str8_lit_comp("oooo"), fg,   0,                    0, str8_lit_comp("terminal normal oooo segment")},
        {6,  str8_lit_comp("oooo"), cyan, 0,                    0, str8_lit_comp("terminal color oooo segment")},
        {12, str8_lit_comp("oooo"), fg,   CLEAT_CELL_FLAG_BOLD, 0, str8_lit_comp("terminal bold oooo segment")},
        {18, str8_lit_comp("oooo"), gold, CLEAT_CELL_FLAG_FAINT, 0, str8_lit_comp("terminal faint oooo segment")},
        {30, str8_lit_comp("onon"), fg,   0,                    1, str8_lit_comp("terminal normal onon segment")},
        {36, str8_lit_comp("onon"), cyan, 0,                    1, str8_lit_comp("terminal color onon segment")},
        {42, str8_lit_comp("onon"), fg,   CLEAT_CELL_FLAG_BOLD, 1, str8_lit_comp("terminal bold onon segment")},
        {48, str8_lit_comp("onon"), gold, CLEAT_CELL_FLAG_FAINT, 1, str8_lit_comp("terminal faint onon segment")},
      };
      for EachElement(case_idx, segment_cases)
      {
        struct SegmentCase *segment = &segment_cases[case_idx];
        uishell_terminal_diagnostic_put_ascii_string(scratch.arena, cells, ArrayCount(cells), segment->col, segment->text, segment->fg, bg, segment->flags);
      }

      F32 cell_width = Max(1.f, fnt_dim_from_tag_size_string(primary_font, font_size, 0, 0, str8_lit("H")).x);
      F32 cell_height = ceil_f32(ClampBot(1.f, fnt_line_height_from_metrics(&primary_metrics)*1.2f));
      Rng2F32 canvas_rect = r2f32p(0, 0, cell_width*(F32)SegmentCols, cell_height);
      UIShell_TerminalCellFeed feed =
      {
        .cols = SegmentCols,
        .rows = 1,
        .cells = cells,
        .cell_count = ArrayCount(cells),
      };
      UIShell_TerminalDrawParams draw_params =
      {
        .canvas_rect = canvas_rect,
        .background_color = uishell_terminal_rgba_from_rgb(bg),
        .cell_width_px = cell_width,
        .cell_height_px = cell_height,
      };
      DR_Bucket *bucket = dr_bucket_make();
      DR_BucketScope(bucket)
      {
        UIShell_TerminalCursorArray cursors = {0};
        uishell_terminal_glyph_renderer_draw_cell_feed_with_cursors(scratch.arena, &renderer, &draw_params, &feed, cursors);
      }
      R_Readback readback = r_pass_list_readback(scratch.arena, v2s32((S32)ceil_f32(canvas_rect.x1), (S32)ceil_f32(canvas_rect.y1)), &bucket->passes);
      if(readback.data.size == 0)
      {
#if OS_MAC
        log_user_errorf("terminal glyph diagnostics failed: terminal styled segment baseline readback returned no pixels");
        result = 0;
#else
        log_infof("terminal glyph diagnostics: terminal styled segment baseline readback is not implemented on this platform");
#endif
      }
      else
      {
        UIShell_TerminalReadbackVisibleBounds reference_bounds[SegmentGroupCount] = {0};
        B32 have_reference[SegmentGroupCount] = {0};
        for EachElement(case_idx, segment_cases)
        {
          struct SegmentCase *segment = &segment_cases[case_idx];
          if(segment->group_idx >= SegmentGroupCount)
          {
            log_user_errorf("terminal glyph diagnostics failed: %S used an invalid styled segment baseline group", segment->label);
            result = 0;
            continue;
          }
          S32 x0 = (S32)floor_f32(canvas_rect.x0 + cell_width*(F32)segment->col);
          S32 x1 = (S32)ceil_f32(canvas_rect.x0 + cell_width*(F32)(segment->col + segment->text.size));
          UIShell_TerminalReadbackVisibleBounds bounds = uishell_terminal_readback_visible_bounds_in_rect(readback, r2s32p(x0, 0, x1, (S32)ceil_f32(canvas_rect.y1)));
          if(!bounds.has_pixels)
          {
            log_user_errorf("terminal glyph diagnostics failed: %S produced no visible pixels in styled segment baseline probe", segment->label);
            result = 0;
            continue;
          }
          if(!have_reference[segment->group_idx])
          {
            reference_bounds[segment->group_idx] = bounds;
            have_reference[segment->group_idx] = 1;
          }
          else if(abs_s64((S64)bounds.rect.y0 - (S64)reference_bounds[segment->group_idx].rect.y0) > 1 ||
                  abs_s64((S64)bounds.rect.y1 - (S64)reference_bounds[segment->group_idx].rect.y1) > 1)
          {
            log_user_errorf("terminal glyph diagnostics failed: %S moved visible ink off the terminal row baseline (reference=%i,%i,%i,%i segment=%i,%i,%i,%i)",
                            segment->label,
                            reference_bounds[segment->group_idx].rect.x0,
                            reference_bounds[segment->group_idx].rect.y0,
                            reference_bounds[segment->group_idx].rect.x1,
                            reference_bounds[segment->group_idx].rect.y1,
                            bounds.rect.x0,
                            bounds.rect.y0,
                            bounds.rect.x1,
                            bounds.rect.y1);
            result = 0;
          }
        }
      }
    }

    {
      cleat_snapshot fixture = uishell_terminal_fixture_snapshot(scratch.arena, 80, 24);
      struct FixtureSemanticCase
      {
        U64 row;
        U64 col;
        String8 label;
        B32 expect_wants_source_color;
        B32 expect_single_piece;
        B32 expect_normal_mask;
      }
      fixture_cases[] =
      {
        {13, 12, str8_lit_comp("fixture combining cell"), 0, 1, 1},
        {15, 14, str8_lit_comp("fixture default text-presentation cell"), 0, 0, 1},
        {15, 27, str8_lit_comp("fixture text-presentation cell"), 0, 1, 1},
        {14, 12, str8_lit_comp("fixture wide emoji cell"), 1, 0, 0},
        {15, 38, str8_lit_comp("fixture emoji-presentation cell"), 1, 1, 0},
        {17, 13, str8_lit_comp("fixture ZWJ cluster cell"), 1, 1, 0},
        {18, 13, str8_lit_comp("fixture family ZWJ cluster cell"), 1, 1, 0},
        {21, 12, str8_lit_comp("fixture keycap sequence cell"), 1, 1, 0},
        {21, 16, str8_lit_comp("fixture flag sequence cell"), 1, 1, 0},
        {21, 20, str8_lit_comp("fixture modifier sequence cell"), 1, 1, 0},
      };
      for EachElement(case_idx, fixture_cases)
      {
        struct FixtureSemanticCase *semantic_case = &fixture_cases[case_idx];
        U64 cell_idx = semantic_case->row*(U64)fixture.cols + semantic_case->col;
        if(cell_idx >= fixture.cell_count)
        {
          log_user_errorf("terminal glyph diagnostics failed: %S is outside fixture bounds", semantic_case->label);
          result = 0;
          continue;
        }
        cleat_cell const *cell = &fixture.cells[cell_idx];
        UIShell_TerminalCellTextDecision decision = uishell_terminal_cell_text_decision_from_cell(scratch.arena, &renderer, cell);
        if(decision.wants_source_color_cell != semantic_case->expect_wants_source_color)
        {
          log_user_errorf("terminal glyph diagnostics failed: %S source-color intent mismatch", semantic_case->label);
          result = 0;
        }
        if(semantic_case->expect_single_piece && !(decision.raster_flags & FNT_RasterFlag_SinglePiece))
        {
          log_user_errorf("terminal glyph diagnostics failed: %S did not request SinglePiece rastering", semantic_case->label);
          result = 0;
        }
        if(semantic_case->expect_normal_mask)
        {
          if(!uishell_terminal_diagnostic_check_normal_text_decision(decision, semantic_case->label))
          {
            result = 0;
          }
        }
        else if(decision.path == UIShell_TerminalCellTextPath_Missing)
        {
          log_user_errorf("terminal glyph diagnostics failed: %S resolved to missing text", semantic_case->label);
          result = 0;
        }
      }
    }

    {
      cleat_snapshot fixture = uishell_terminal_fixture_snapshot(scratch.arena, 80, 24);
      FNT_Metrics metrics = fnt_metrics_from_tag_size(primary_font, font_size);
      F32 cell_width = Max(1.f, fnt_dim_from_tag_size_string(primary_font, font_size, 0, 0, str8_lit("H")).x);
      F32 cell_height = ceil_f32(ClampBot(1.f, fnt_line_height_from_metrics(&metrics)*1.2f));
      cleat_rgb fixture_bg = {4, 4, 4};
      UIShell_TerminalDrawParams draw_params =
      {
        .canvas_rect = r2f32p(0, 0, cell_width*(F32)fixture.cols, cell_height*(F32)fixture.rows),
        .background_color = uishell_terminal_rgba_from_rgb(fixture_bg),
        .cell_width_px = cell_width,
        .cell_height_px = cell_height,
      };
      UIShell_TerminalCursorArray cursors = uishell_terminal_fixture_cursor_array(scratch.arena, fixture.cols, fixture.rows);
      DR_Bucket *bucket = dr_bucket_make();
      DR_BucketScope(bucket)
      {
        uishell_terminal_glyph_renderer_draw_snapshot_with_cursors(scratch.arena, &renderer, &draw_params, &fixture, cursors);
      }
      UIShell_TerminalRenderBucketStats stats = uishell_terminal_render_bucket_stats_from_bucket(bucket);
      U64 total_inst_count = stats.rect_inst_count + stats.textured_inst_count;
      if(stats.ui_pass_count == 0 ||
         total_inst_count < 128 ||
         stats.source_color_like_inst_count == 0)
      {
        log_user_errorf("terminal glyph diagnostics failed: fixture render bucket was incomplete (ui=%I64u total=%I64u rects=%I64u textured=%I64u source_like=%I64u)",
                        stats.ui_pass_count,
                        total_inst_count,
                        stats.rect_inst_count,
                        stats.textured_inst_count,
                        stats.source_color_like_inst_count);
        result = 0;
      }

      Vec2S32 readback_size = v2s32((S32)ceil_f32(draw_params.canvas_rect.x1 - draw_params.canvas_rect.x0),
                                    (S32)ceil_f32(draw_params.canvas_rect.y1 - draw_params.canvas_rect.y0));
      R_Readback readback = r_pass_list_readback(scratch.arena, readback_size, &bucket->passes);
      if(readback.data.size == 0)
      {
#if OS_MAC
        log_user_errorf("terminal glyph diagnostics failed: Metal fixture readback returned no pixels");
        result = 0;
#else
        log_infof("terminal glyph diagnostics: backend fixture readback is not implemented on this platform");
#endif
      }
      else
      {
        U64 visible_pixel_count = uishell_terminal_readback_visible_pixel_count(readback);
        Rng2S32 title_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 0, 0, 80, 1);
        Rng2S32 ascii_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 0, 1, 48, 2);
        Rng2S32 empty_bg_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 64, 1, 78, 2);
        Rng2S32 blocks_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 8, 9, 31, 10);
        Rng2S32 red_text_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 0, 16, 22, 17);
        Rng2S32 emoji_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 12, 14, 14, 15);
        Rng2S32 align_normal_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 8, 4, 12, 5);
        Rng2S32 align_color_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 16, 4, 20, 5);
        Rng2S32 align_bold_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 24, 4, 28, 5);
        Rng2S32 align_faint_rect = uishell_terminal_readback_cell_rect(cell_width, cell_height, 32, 4, 36, 5);
        U64 title_visible_pixel_count = uishell_terminal_readback_visible_pixel_count_in_rect(readback,
                                                                                              title_rect);
        U64 ascii_light_pixel_count = uishell_terminal_readback_pixel_class_count_in_rect(readback,
                                                                                         ascii_rect,
                                                                                         UIShell_TerminalReadbackPixelClass_Light);
        U64 empty_bg_dark_pixel_count = uishell_terminal_readback_pixel_class_count_in_rect(readback,
                                                                                           empty_bg_rect,
                                                                                           UIShell_TerminalReadbackPixelClass_Dark);
        U64 blocks_green_pixel_count = uishell_terminal_readback_pixel_class_count_in_rect(readback,
                                                                                          blocks_rect,
                                                                                          UIShell_TerminalReadbackPixelClass_GreenDominant);
        U64 red_text_pixel_count = uishell_terminal_readback_pixel_class_count_in_rect(readback,
                                                                                      red_text_rect,
                                                                                      UIShell_TerminalReadbackPixelClass_RedDominant);
        U64 emoji_visible_pixel_count = uishell_terminal_readback_visible_pixel_count_in_rect(readback,
                                                                                              emoji_rect);
        U64 emoji_chromatic_pixel_count = uishell_terminal_readback_chromatic_pixel_count_in_rect(readback,
                                                                                                  emoji_rect);
        UIShell_TerminalReadbackVisibleBounds align_normal_bounds = uishell_terminal_readback_visible_bounds_in_rect(readback, align_normal_rect);
        UIShell_TerminalReadbackVisibleBounds align_color_bounds = uishell_terminal_readback_visible_bounds_in_rect(readback, align_color_rect);
        UIShell_TerminalReadbackVisibleBounds align_bold_bounds = uishell_terminal_readback_visible_bounds_in_rect(readback, align_bold_rect);
        UIShell_TerminalReadbackVisibleBounds align_faint_bounds = uishell_terminal_readback_visible_bounds_in_rect(readback, align_faint_rect);
        U64 empty_bg_area = (U64)Max(0, empty_bg_rect.x1 - empty_bg_rect.x0)*
                            (U64)Max(0, empty_bg_rect.y1 - empty_bg_rect.y0);
        B32 align_bounds_match = (align_normal_bounds.has_pixels &&
                                  align_color_bounds.has_pixels &&
                                  align_bold_bounds.has_pixels &&
                                  align_faint_bounds.has_pixels &&
                                  abs_s64((S64)align_normal_bounds.rect.y0 - (S64)align_color_bounds.rect.y0) <= 1 &&
                                  abs_s64((S64)align_normal_bounds.rect.y1 - (S64)align_color_bounds.rect.y1) <= 1 &&
                                  abs_s64((S64)align_normal_bounds.rect.y0 - (S64)align_bold_bounds.rect.y0) <= 1 &&
                                  abs_s64((S64)align_normal_bounds.rect.y1 - (S64)align_bold_bounds.rect.y1) <= 1 &&
                                  abs_s64((S64)align_normal_bounds.rect.y0 - (S64)align_faint_bounds.rect.y0) <= 1 &&
                                  abs_s64((S64)align_normal_bounds.rect.y1 - (S64)align_faint_bounds.rect.y1) <= 1);
        if(readback.size.x != readback_size.x ||
           readback.size.y != readback_size.y ||
           readback.format != R_Tex2DFormat_BGRA8 ||
           visible_pixel_count < 128 ||
           title_visible_pixel_count < 16 ||
           ascii_light_pixel_count < 32 ||
           empty_bg_dark_pixel_count < empty_bg_area*9/10 ||
           blocks_green_pixel_count < 16 ||
           red_text_pixel_count < 16 ||
           emoji_visible_pixel_count < 4 ||
           emoji_chromatic_pixel_count < 4 ||
           !align_bounds_match)
        {
          log_user_errorf("terminal glyph diagnostics failed: fixture backend readback was incomplete or misaligned (size=%ix%i format=%u bytes=%I64u visible=%I64u title=%I64u ascii_light=%I64u bg_dark=%I64u/%I64u blocks_green=%I64u red=%I64u emoji=%I64u emoji_chroma=%I64u align=%i normal=%i,%i,%i,%i color=%i,%i,%i,%i bold=%i,%i,%i,%i faint=%i,%i,%i,%i)",
                          readback.size.x,
                          readback.size.y,
                          readback.format,
                          readback.data.size,
                          visible_pixel_count,
                          title_visible_pixel_count,
                          ascii_light_pixel_count,
                          empty_bg_dark_pixel_count,
                          empty_bg_area,
                          blocks_green_pixel_count,
                          red_text_pixel_count,
                          emoji_visible_pixel_count,
                          emoji_chromatic_pixel_count,
                          align_bounds_match,
                          align_normal_bounds.rect.x0,
                          align_normal_bounds.rect.y0,
                          align_normal_bounds.rect.x1,
                          align_normal_bounds.rect.y1,
                          align_color_bounds.rect.x0,
                          align_color_bounds.rect.y0,
                          align_color_bounds.rect.x1,
                          align_color_bounds.rect.y1,
                          align_bold_bounds.rect.x0,
                          align_bold_bounds.rect.y0,
                          align_bold_bounds.rect.x1,
                          align_bold_bounds.rect.y1,
                          align_faint_bounds.rect.x0,
                          align_faint_bounds.rect.y0,
                          align_faint_bounds.rect.x1,
                          align_faint_bounds.rect.y1);
          result = 0;
        }
      }
    }
    scratch_end(scratch);
  }
  if(result)
  {
    log_infof("terminal glyph diagnostics passed");
  }
  return result;
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

enum
{
  UIShell_TerminalFixtureGraphemeCap = 16,
};

internal void
uishell_terminal_fixture_clear(cleat_snapshot *snapshot, cleat_cell *cells, U32 *graphemes)
{
  U64 cell_count = (U64)snapshot->cols*(U64)snapshot->rows;
  cleat_rgb fg = uishell_terminal_fixture_rgb(188, 209, 191);
  cleat_rgb bg = uishell_terminal_fixture_rgb(4, 4, 4);
  for(U64 idx = 0; idx < cell_count; idx += 1)
  {
    U32 *cell_graphemes = graphemes + idx*UIShell_TerminalFixtureGraphemeCap;
    for(U64 grapheme_idx = 0; grapheme_idx < UIShell_TerminalFixtureGraphemeCap; grapheme_idx += 1)
    {
      cell_graphemes[grapheme_idx] = 0;
    }
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
    U32 *cell_graphemes = graphemes + cell_idx*UIShell_TerminalFixtureGraphemeCap;
    U64 capped_count = Clamp(1, codepoint_count, UIShell_TerminalFixtureGraphemeCap);
    for(U64 idx = 0; idx < capped_count; idx += 1)
    {
      cell_graphemes[idx] = codepoints[idx];
    }
    for(U64 idx = capped_count; idx < UIShell_TerminalFixtureGraphemeCap; idx += 1)
    {
      cell_graphemes[idx] = 0;
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
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 10, 20, CLEAT_CURSOR_STYLE_BLOCK, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 20, 20, CLEAT_CURSOR_STYLE_BAR, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 34, 20, CLEAT_CURSOR_STYLE_UNDERLINE, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 48, 20, CLEAT_CURSOR_STYLE_BLOCK_HOLLOW, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 12, 22, CLEAT_CURSOR_STYLE_BLOCK, 0);
  uishell_terminal_fixture_cursor_array_push(&result, cols, rows, 15, 22, CLEAT_CURSOR_STYLE_BAR, 1);
  return result;
}

internal cleat_snapshot
uishell_terminal_fixture_snapshot(Arena *arena, U16 cols, U16 rows)
{
  cols = ClampBot(1, cols);
  rows = ClampBot(1, rows);
  U64 cell_count = (U64)cols*(U64)rows;
  cleat_cell *cells = push_array(arena, cleat_cell, cell_count);
  U32 *graphemes = push_array(arena, U32, cell_count*UIShell_TerminalFixtureGraphemeCap);
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

  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 4, 0, str8_lit("Align:"), dim, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 4, 8, str8_lit("oooo"), fg, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 4, 16, str8_lit("oooo"), accent, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 4, 24, str8_lit("oooo"), fg, bg, CLEAT_CELL_FLAG_BOLD);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 4, 32, str8_lit("oooo"), fg, bg, CLEAT_CELL_FLAG_FAINT);

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

  U32 presentation_default[] = {0x263A};
  U32 presentation_text[] = {0x263A, 0xFE0E};
  U32 presentation_emoji[] = {0x263A, 0xFE0F};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 15, 0, str8_lit("Presentation:"), fg, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 15, 14, presentation_default, ArrayCount(presentation_default), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(15 < rows && 15 < cols)
  {
    cells[15*(U64)cols + 15].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 15, 17, str8_lit("default"), dim, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 15, 27, presentation_text, ArrayCount(presentation_text), fg, bg, 0, CLEAT_CELL_WIDTH_NARROW);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 15, 29, str8_lit("text"), dim, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 15, 38, presentation_emoji, ArrayCount(presentation_emoji), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(15 < rows && 39 < cols)
  {
    cells[15*(U64)cols + 39].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 15, 41, str8_lit("emoji"), dim, bg, 0);

  U32 unimplemented[] = {0x256D, 0x256E, 0x256F, 0x2570, ' ', 0x2571, 0x2572, 0x2573};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 16, 0, str8_lit("Text fallback sprites:"), red, bg, 0);
  uishell_terminal_fixture_put_codepoints(&snapshot, cells, graphemes, 16, 23, unimplemented, ArrayCount(unimplemented), red, bg, 0);

  U32 zwj_cluster[] = {0x1F469, 0x200D, 0x1F4BB};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 17, 0, str8_lit("ZWJ cluster:"), fg, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 17, 13, zwj_cluster, ArrayCount(zwj_cluster), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(17 < rows && 14 < cols)
  {
    cells[17*(U64)cols + 14].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 17, 16, str8_lit("single cell U+1F469 ZWJ U+1F4BB"), dim, bg, 0);

  U32 zwj_family[] = {0x1F469, 0x200D, 0x1F469, 0x200D, 0x1F467, 0x200D, 0x1F466};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 18, 0, str8_lit("ZWJ family:"), fg, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 18, 13, zwj_family, ArrayCount(zwj_family), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(18 < rows && 14 < cols)
  {
    cells[18*(U64)cols + 14].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 18, 16, str8_lit("7 scalars in one cell"), dim, bg, 0);

  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 19, 0, str8_lit("Cursors:"), fg, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 19, 10, str8_lit("block"), dim, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 19, 20, str8_lit("bar"), dim, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 19, 34, str8_lit("underline"), dim, bg, 0);
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 19, 48, str8_lit("hollow"), dim, bg, 0);
  uishell_terminal_fixture_put_cp(&snapshot, cells, graphemes, 20, 10, '@', fg, bg, 0);
  uishell_terminal_fixture_put_cp(&snapshot, cells, graphemes, 20, 20, '@', fg, bg, 0);
  uishell_terminal_fixture_put_cp(&snapshot, cells, graphemes, 20, 34, '@', fg, bg, 0);
  uishell_terminal_fixture_put_cp(&snapshot, cells, graphemes, 20, 48, '@', fg, bg, 0);

  U32 keycap_sequence[] = {'1', 0xFE0F, 0x20E3};
  U32 flag_sequence[] = {0x1F1EC, 0x1F1E7};
  U32 modifier_sequence[] = {0x1F44B, 0x1F3FB};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 21, 0, str8_lit("Emoji seq:"), fg, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 21, 12, keycap_sequence, ArrayCount(keycap_sequence), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(21 < rows && 13 < cols)
  {
    cells[21*(U64)cols + 13].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 21, 16, flag_sequence, ArrayCount(flag_sequence), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(21 < rows && 17 < cols)
  {
    cells[21*(U64)cols + 17].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 21, 20, modifier_sequence, ArrayCount(modifier_sequence), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(21 < rows && 21 < cols)
  {
    cells[21*(U64)cols + 21].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 21, 24, str8_lit("keycap / flag / modifier"), dim, bg, 0);

  U32 wide_cursor_sample[] = {0x25A0};
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 22, 0, str8_lit("Wide cursors:"), fg, bg, 0);
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 22, 12, wide_cursor_sample, ArrayCount(wide_cursor_sample), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(22 < rows && 13 < cols)
  {
    cells[22*(U64)cols + 13].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_cell(&snapshot, cells, graphemes, 22, 15, wide_cursor_sample, ArrayCount(wide_cursor_sample), fg, bg, 0, CLEAT_CELL_WIDTH_WIDE);
  if(22 < rows && 16 < cols)
  {
    cells[22*(U64)cols + 16].width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  }
  uishell_terminal_fixture_put_string(&snapshot, cells, graphemes, 22, 18, str8_lit("block head / bar tail"), dim, bg, 0);

  return snapshot;
}

// Draw order: lower z first; ties broken by lower image_id, then stable
// submission order (Kitty: equal-z overlap resolves to the lower image id).
internal int
uishell_terminal_image_placement_compare(UIShell_TerminalImagePlacement const *a, UIShell_TerminalImagePlacement const *b)
{
  if(a->z != b->z) { return a->z < b->z ? -1 : 1; }
  if(a->image_id != b->image_id) { return a->image_id < b->image_id ? -1 : 1; }
  if(a->order != b->order) { return a->order < b->order ? -1 : 1; }
  return 0;
}

// Draw one z-plane of image placements. First-slice two-plane split: below_text
// covers z<0 (above backgrounds, below text); !below_text covers z>=0 (over
// text). The z<INT32_MIN/2 under-background plane is deferred.
internal void
uishell_terminal_draw_image_plane(Arena *arena, UIShell_TerminalDrawParams const *params, B32 below_text)
{
  UIShell_TerminalImageCache const *cache = params->image_cache;
  if(cache == 0 || cache->placement_count == 0)
  {
    return;
  }
  Rng2F32 canvas = params->canvas_rect;
  F32 cw = params->cell_width_px;
  F32 ch = params->cell_height_px;

  Temp scratch = scratch_begin(&arena, 1);
  UIShell_TerminalImagePlacement **list = push_array(scratch.arena, UIShell_TerminalImagePlacement *, cache->placement_count);
  U64 count = 0;
  for(U64 i = 0; i < cache->placement_count; i += 1)
  {
    UIShell_TerminalImagePlacement *p = &cache->placements[i];
    B32 in_plane = below_text ? (p->z < 0) : (p->z >= 0);
    if(in_plane)
    {
      list[count] = p;
      count += 1;
    }
  }
  // Insertion sort: placement counts are small; keeps this dependency-free.
  for(U64 i = 1; i < count; i += 1)
  {
    UIShell_TerminalImagePlacement *key = list[i];
    U64 j = i;
    while(j > 0 && uishell_terminal_image_placement_compare(list[j-1], key) > 0)
    {
      list[j] = list[j-1];
      j -= 1;
    }
    list[j] = key;
  }

  for(U64 i = 0; i < count; i += 1)
  {
    UIShell_TerminalImagePlacement *p = list[i];
    UIShell_TerminalImageResource *res = uishell_terminal_image_cache_resource_from_id((UIShell_TerminalImageCache *)cache, p->image_id);
    // Skip cleanly if the resource is not resident at this generation.
    if(res == 0 || !res->valid || res->generation != p->generation ||
       res->width_px == 0 || res->height_px == 0 ||
       r_handle_match(res->texture, r_handle_zero()))
    {
      continue;
    }

    F32 dst_w = p->pixel_width != 0 ? (F32)p->pixel_width : (F32)p->grid_cols*cw;
    F32 dst_h = p->pixel_height != 0 ? (F32)p->pixel_height : (F32)p->grid_rows*ch;
    if(dst_w <= 0 || dst_h <= 0)
    {
      continue;
    }
    F32 dx0 = canvas.x0 + (F32)p->viewport_col*cw + (F32)p->x_offset_px;
    F32 dy0 = canvas.y0 + (F32)p->viewport_row*ch + (F32)p->y_offset_px;
    Rng2F32 dst = r2f32p(dx0, dy0, dx0 + dst_w, dy0 + dst_h);

    // Source rect in texel space. Cleat reports the already-clamped crop (the
    // intersection of the requested source rectangle with the image); an
    // uncropped placement reports the full image extent, never zero. So a zero
    // source width/height means the crop is entirely outside the image and
    // nothing should be drawn (matching Kitty/Ghostty clamping semantics).
    if(p->source_width == 0 || p->source_height == 0)
    {
      continue;
    }
    F32 sx0 = (F32)p->source_x;
    F32 sy0 = (F32)p->source_y;
    F32 sx1 = sx0 + (F32)p->source_width;
    F32 sy1 = sy0 + (F32)p->source_height;
    Rng2F32 src = r2f32p(sx0, sy0, sx1, sy1);

    // Clip the destination to the content canvas, cropping the source rect
    // proportionally so partially-offscreen placements render correctly.
    Rng2F32 vis = intersect_2f32(dst, canvas);
    if(vis.x1 <= vis.x0 || vis.y1 <= vis.y0)
    {
      continue;
    }
    F32 dwid = dst.x1 - dst.x0;
    F32 dhei = dst.y1 - dst.y0;
    F32 fx0 = (vis.x0 - dst.x0)/dwid;
    F32 fx1 = (vis.x1 - dst.x0)/dwid;
    F32 fy0 = (vis.y0 - dst.y0)/dhei;
    F32 fy1 = (vis.y1 - dst.y0)/dhei;
    F32 swid = src.x1 - src.x0;
    F32 shei = src.y1 - src.y0;
    Rng2F32 src_c = r2f32p(src.x0 + fx0*swid, src.y0 + fy0*shei,
                           src.x0 + fx1*swid, src.y0 + fy1*shei);
    // Straight-alpha sRGB texture, opaque white tint preserves source colour.
    // Sample linearly so scaled images (Kitty placements are usually upscaled
    // from a smaller source) interpolate smoothly instead of showing texel
    // banding; the terminal's default sample kind is Nearest for crisp glyphs.
    DR_Tex2DSampleKindScope(R_Tex2DSampleKind_Linear)
    {
      dr_img(vis, src_c, res->texture, v4f32(1, 1, 1, 1), 0, 0, 0);
    }
  }
  scratch_end(scratch);
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
    }

    // Image plane below text (z < 0), drawn above cell backgrounds. The loop is
    // split into a background pass and a text pass precisely so this seam exists.
    uishell_terminal_draw_image_plane(arena, params, 1);

    for(U64 row_idx = 0; row_idx < feed->rows; row_idx += 1)
    {
      F32 row_y0 = floor_f32(canvas_rect.y0 + (F32)row_idx*cell_height_px);
      F32 row_y1 = ceil_f32(canvas_rect.y0 + (F32)(row_idx + 1)*cell_height_px);
      if(row_y0 >= canvas_rect.y1 || row_y1 <= canvas_rect.y0)
      {
        continue;
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
            FNT_Run joined_run = dr_fnt_run_from_string(run_font, font_size, 0, 0, raster_flags, joined);
            if(renderer->trace_enabled && (renderer->trace_all_rows || renderer->trace_row == row_idx))
            {
              uishell_terminal_trace_run(arena, renderer, str8_lit("batched"), row_idx, run_col_start, run_col_opl, &feed->cells[row_idx*(U64)feed->cols + run_col_start], UIShell_TerminalCellTextPath_NormalMask, run_font, raster_flags, joined, joined_run, text_pos, font_metrics.descent, run_fg);
            }
            uishell_terminal_draw_run_at_row_baseline(joined_run, text_pos, font_metrics.descent, run_fg);
          }
          col_idx = run_col_opl;
          continue;
        }

        if(cell->grapheme_count != 0 &&
           !uishell_terminal_cell_is_spacer(cell) &&
           !uishell_terminal_cell_is_kitty_placeholder(cell) &&
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
            UIShell_TerminalCellTextDecision text_decision = uishell_terminal_cell_text_decision_from_cell(arena, renderer, cell);
            if(text_decision.path == UIShell_TerminalCellTextPath_Missing)
            {
              uishell_terminal_draw_missing_glyph(cell_rect, fg);
            }
            else
            {
              if(renderer->trace_enabled && (renderer->trace_all_rows || renderer->trace_row == row_idx))
              {
                uishell_terminal_trace_run(arena, renderer, str8_lit("cell"), row_idx, col_idx, col_idx + cell_cols, cell, text_decision.path, text_decision.font, text_decision.raster_flags, text_decision.string, text_decision.run, v2f32(cell_rect.x0, text_y), font_metrics.descent, fg);
              }
              uishell_terminal_draw_text_decision_in_cell(text_decision, cell_rect, text_y, font_metrics.descent, fg);
              if((cell->flags & CLEAT_CELL_FLAG_BOLD) && text_decision.path != UIShell_TerminalCellTextPath_SourceColor)
              {
                Rng2F32 bold_rect = shift_2f32(cell_rect, v2f32(1.f, 0));
                uishell_terminal_draw_text_decision_in_cell(text_decision, bold_rect, text_y, font_metrics.descent, fg);
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

    // Image plane over text (z >= 0).
    uishell_terminal_draw_image_plane(arena, params, 0);

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
