// The Rich Text View (#561): a markdown file as Rich Text. The core
// (wheelhouse_rich_text_*) parses it, virtualizes its Blocks and owns the
// Scroll Anchor, selection and find; this View keeps a model built from its
// deltas, lays each Block in the Window out with wh_text_layout, stacks the
// Blocks itself and draws them from the glyph cache. Layout, boxes and
// buckets follow #544's decisions 5 to 8; the table Embed, #542's contract.

////////////////////////////////
//~ Text context

// One per process: fonts and fallbacks are the app's, not a View's.
global WhText *uishell_rt_text_ctx = 0;
global Arena *uishell_rt_font_arena = 0;
// FNT_Tags by wh_text font id, filled as runs name them.
global FNT_Tag *uishell_rt_font_tags = 0;
global B32 *uishell_rt_font_known = 0;
global U32 uishell_rt_font_cap = 0;

internal WheelhouseText
uishell_rt_wt(String8 s)
{
  return (WheelhouseText){s.str, s.size};
}

internal String8
uishell_rt_s8(WheelhouseText t)
{
  return str8((U8 *)t.data, t.len);
}

internal WhText *
uishell_rt_text(void)
{
  if(uishell_rt_text_ctx == 0)
  {
    WhText *ctx = wh_text_create(1);
    // Registered by the address of their bytes, so a run's font becomes the
    // tag the rest of the app has for them.
    String8 *bundled[] =
    {
      &rd_default_main_font_bytes, &rd_default_main_semibold_font_bytes, &rd_default_code_font_bytes,
      &rd_terminal_noto_symbols_2_font_bytes, &rd_terminal_noto_math_font_bytes, &rd_terminal_noto_symbols_font_bytes,
      &rd_terminal_noto_color_emoji_font_bytes, &rd_terminal_noto_emoji_font_bytes,
    };
    for EachElement(idx, bundled)
    {
      wh_text_register_font(ctx, bundled[idx]->str, bundled[idx]->size, (U64)bundled[idx]);
    }
    // docs/research/fontique-parity.md: the bundled symbol, math and emoji
    // faces, tried before fontique's own fallback. CJK seeding is #592's.
    WheelhouseText fallbacks[] =
    {
      uishell_rt_wt(str8_lit("Noto Sans Symbols 2")), uishell_rt_wt(str8_lit("Noto Sans Math")),
      uishell_rt_wt(str8_lit("Noto Sans Symbols")), uishell_rt_wt(str8_lit("Noto Color Emoji")),
      uishell_rt_wt(str8_lit("Noto Emoji")),
    };
    wh_text_set_fallbacks(ctx, fallbacks, ArrayCount(fallbacks));
    // Styles are in points, as the UI's font size is; layout is in the UI's
    // pixels at 96 DPI, which fnt_metrics_from_tag_size also assumes.
    wh_text_set_scale(ctx, 96.f/72.f);
    uishell_rt_font_arena = arena_alloc();
    uishell_rt_text_ctx = ctx;
  }
  return uishell_rt_text_ctx;
}

internal FNT_Tag
uishell_rt_tag_from_font(U32 font)
{
  if(font >= uishell_rt_font_cap)
  {
    U32 cap = Max(64, uishell_rt_font_cap*2);
    while(cap <= font) { cap *= 2; }
    FNT_Tag *tags = push_array(uishell_rt_font_arena, FNT_Tag, cap);
    B32 *known = push_array(uishell_rt_font_arena, B32, cap);
    if(uishell_rt_font_cap)
    {
      MemoryCopy(tags, uishell_rt_font_tags, sizeof(FNT_Tag)*uishell_rt_font_cap);
      MemoryCopy(known, uishell_rt_font_known, sizeof(B32)*uishell_rt_font_cap);
    }
    uishell_rt_font_tags = tags;
    uishell_rt_font_known = known;
    uishell_rt_font_cap = cap;
  }
  if(!uishell_rt_font_known[font])
  {
    WhTextFont f = {0};
    FNT_Tag tag = fnt_tag_zero();
    // A font's bytes live as long as the context, as the tag needs them to.
    if(wh_text_font(uishell_rt_text(), font, &f))
    {
      tag = (f.kind == WH_TEXT_FONT_BYTES && f.face_index == 0) ? fnt_tag_from_static_data_string((String8 *)f.bytes_id)
                                                                 : fnt_tag_from_memory_face(str8((U8 *)f.data, f.data_len), f.face_index);
    }
    uishell_rt_font_tags[font] = tag;
    uishell_rt_font_known[font] = 1;
  }
  return uishell_rt_font_tags[font];
}

internal void *
uishell_rt_wh_alloc(void *user, size_t size, size_t align)
{
  return arena_push((Arena *)user, size, Max(align, 1), 0);
}

internal WhTextAllocator
uishell_rt_allocator(Arena *arena)
{
  return (WhTextAllocator){uishell_rt_wh_alloc, arena};
}

////////////////////////////////
//~ Blob pool (#544, decision 5)

// Blobs come in power-of-two classes, 64 bytes to a page, carved from 1 MB
// pages; a bigger one has its own reservation.
#define UISHELL_RT_PAGE_SIZE MB(1)
#define UISHELL_RT_MIN_CLASS_LOG2 6
#define UISHELL_RT_PAGE_CLASS_LOG2 20
#define UISHELL_RT_CLASS_COUNT 64

typedef struct UIShell_RtFreeBlob UIShell_RtFreeBlob;
struct UIShell_RtFreeBlob
{
  UIShell_RtFreeBlob *next;
};

typedef struct UIShell_RtPage UIShell_RtPage;
struct UIShell_RtPage
{
  UIShell_RtPage *next;
  U8 *base;
};

typedef struct UIShell_RtBlobPool UIShell_RtBlobPool;
struct UIShell_RtBlobPool
{
  Arena *arena;
  UIShell_RtFreeBlob *free[UISHELL_RT_CLASS_COUNT];
  UIShell_RtPage *pages;
  U8 *carve;
  U64 carve_left;
  U64 page_count;
  // Class bytes handed out and not yet freed.
  U64 bytes;
};

internal U32
uishell_rt_class_from_size(U64 size)
{
  U64 rounded = u64_up_to_pow2(Max(size, 1ull<<UISHELL_RT_MIN_CLASS_LOG2));
  return (U32)ctz64(rounded);
}

// What carving leaves of a page goes to the smaller classes' free lists, so
// a page wastes less than its smallest class.
internal void
uishell_rt_pool_spill(UIShell_RtBlobPool *pool)
{
  while(pool->carve_left >= (1ull<<UISHELL_RT_MIN_CLASS_LOG2))
  {
    U32 c = (U32)(63 - clz64(pool->carve_left));
    UIShell_RtFreeBlob *blob = (UIShell_RtFreeBlob *)pool->carve;
    blob->next = pool->free[c];
    pool->free[c] = blob;
    pool->carve += 1ull<<c;
    pool->carve_left -= 1ull<<c;
  }
}

internal U8 *
uishell_rt_blob_alloc(UIShell_RtBlobPool *pool, U64 size, U32 *class_out)
{
  U32 c = uishell_rt_class_from_size(size);
  U64 class_size = 1ull<<c;
  U8 *result = 0;
  if(c > UISHELL_RT_PAGE_CLASS_LOG2)
  {
    result = (U8 *)reserve_memory(class_size);
    if(result && !commit_memory(result, class_size)) { release_memory(result, class_size); result = 0; }
  }
  else if(pool->free[c] != 0)
  {
    result = (U8 *)pool->free[c];
    pool->free[c] = pool->free[c]->next;
  }
  else
  {
    if(pool->carve_left < class_size)
    {
      uishell_rt_pool_spill(pool);
      U8 *base = (U8 *)reserve_memory(UISHELL_RT_PAGE_SIZE);
      if(base && !commit_memory(base, UISHELL_RT_PAGE_SIZE)) { release_memory(base, UISHELL_RT_PAGE_SIZE); base = 0; }
      if(base == 0) { return 0; }
      if(pool->arena == 0) { pool->arena = arena_alloc(.reserve_size = KB(64), .commit_size = KB(4)); }
      UIShell_RtPage *page = push_array(pool->arena, UIShell_RtPage, 1);
      page->base = base;
      SLLStackPush(pool->pages, page);
      pool->page_count += 1;
      pool->carve = base;
      pool->carve_left = UISHELL_RT_PAGE_SIZE;
    }
    result = pool->carve;
    pool->carve += class_size;
    pool->carve_left -= class_size;
  }
  if(result != 0)
  {
    pool->bytes += class_size;
    *class_out = c;
  }
  return result;
}

internal void
uishell_rt_blob_free(UIShell_RtBlobPool *pool, U8 *blob, U32 c)
{
  if(blob == 0) { return; }
  pool->bytes -= 1ull<<c;
  if(c > UISHELL_RT_PAGE_CLASS_LOG2)
  {
    release_memory(blob, 1ull<<c);
    return;
  }
  UIShell_RtFreeBlob *free_blob = (UIShell_RtFreeBlob *)blob;
  free_blob->next = pool->free[c];
  pool->free[c] = free_blob;
}

internal void
uishell_rt_pool_release(UIShell_RtBlobPool *pool)
{
  for(UIShell_RtPage *page = pool->pages; page != 0; page = page->next)
  {
    release_memory(page->base, UISHELL_RT_PAGE_SIZE);
  }
  if(pool->arena) { arena_release(pool->arena); }
  MemoryZeroStruct(pool);
}

////////////////////////////////
//~ Model: the document as the deltas build it

typedef struct UIShell_RtNode UIShell_RtNode;
struct UIShell_RtNode
{
  UIShell_RtNode *hash_next;
  U64 key;
  U64 parent;
  U32 kind;
  U32 level;
  U32 hidden;
  U32 task;
  U32 alert;
  U32 ordered;
  U32 tight;
  U64 version;
  U64 start;
  String8 text;
  String8 language;
  String8 label;
  String8 media_type;
  WheelhouseRichSpan *spans;
  U64 span_count;
  String8 *strings;
  U64 string_count;
  U32 *alignments;
  U64 column_count;
  WheelhouseRichCell *cells;
  U64 row_count;
  U64 *children;
  U64 child_count;
  U64 child_cap;
  // A Block's place in document order, for comparing selection ends.
  U64 order;
};

typedef struct UIShell_RtModel UIShell_RtModel;
struct UIShell_RtModel
{
  Arena *arena;
  UIShell_RtNode **slots;
  U64 slots_count;
  U64 count;
  UIShell_RtNode *free_node;
  U64 generation;
  // What the live nodes hold, against what the arena holds: replaced
  // content is garbage until the next compaction.
  U64 live_bytes;
  U64 block_count;
  // Blocks' keys in document order.
  U64 *block_keys;
};

internal UIShell_RtNode *
uishell_rt_node(UIShell_RtModel *m, U64 key)
{
  if(m->slots_count == 0) { return 0; }
  for(UIShell_RtNode *n = m->slots[key & (m->slots_count-1)]; n != 0; n = n->hash_next)
  {
    if(n->key == key) { return n; }
  }
  return 0;
}

internal void
uishell_rt_model_rehash(UIShell_RtModel *m, U64 slots_count)
{
  UIShell_RtNode **slots = push_array(m->arena, UIShell_RtNode *, slots_count);
  for(U64 i = 0; i < m->slots_count; i += 1)
  {
    for(UIShell_RtNode *n = m->slots[i], *next = 0; n != 0; n = next)
    {
      next = n->hash_next;
      UIShell_RtNode **slot = &slots[n->key & (slots_count-1)];
      n->hash_next = *slot;
      *slot = n;
    }
  }
  m->slots = slots;
  m->slots_count = slots_count;
}

internal UIShell_RtNode *
uishell_rt_node_alloc(UIShell_RtModel *m, U64 key)
{
  if(m->arena == 0) { m->arena = arena_alloc(); }
  if(m->count+1 > m->slots_count/2) { uishell_rt_model_rehash(m, Max(256, m->slots_count*2)); }
  UIShell_RtNode *n = m->free_node;
  if(n) { m->free_node = n->hash_next; }
  else { n = push_array_no_zero(m->arena, UIShell_RtNode, 1); }
  MemoryZeroStruct(n);
  n->key = key;
  UIShell_RtNode **slot = &m->slots[key & (m->slots_count-1)];
  n->hash_next = *slot;
  *slot = n;
  m->count += 1;
  return n;
}

internal void
uishell_rt_node_remove(UIShell_RtModel *m, U64 key)
{
  if(m->slots_count == 0) { return; }
  for(UIShell_RtNode **at = &m->slots[key & (m->slots_count-1)]; *at != 0; at = &(*at)->hash_next)
  {
    if((*at)->key == key)
    {
      UIShell_RtNode *n = *at;
      *at = n->hash_next;
      n->hash_next = m->free_node;
      m->free_node = n;
      m->count -= 1;
      return;
    }
  }
}

internal U64
uishell_rt_node_bytes(UIShell_RtNode *n)
{
  return (n->text.size + n->language.size + n->label.size + n->media_type.size +
          n->span_count*sizeof(WheelhouseRichSpan) + n->string_count*sizeof(String8) +
          n->column_count*sizeof(U32) + n->column_count*n->row_count*sizeof(WheelhouseRichCell) +
          n->child_cap*sizeof(U64));
}

internal void
uishell_rt_node_copy_content(Arena *arena, UIShell_RtNode *n)
{
  n->text = push_str8_copy(arena, n->text);
  n->language = push_str8_copy(arena, n->language);
  n->label = push_str8_copy(arena, n->label);
  n->media_type = push_str8_copy(arena, n->media_type);
  WheelhouseRichSpan *spans = push_array_no_zero(arena, WheelhouseRichSpan, n->span_count);
  MemoryCopy(spans, n->spans, sizeof(*spans)*n->span_count);
  n->spans = spans;
  String8 *strings = push_array_no_zero(arena, String8, n->string_count);
  for(U64 i = 0; i < n->string_count; i += 1) { strings[i] = push_str8_copy(arena, n->strings[i]); }
  n->strings = strings;
  U32 *alignments = push_array_no_zero(arena, U32, n->column_count);
  MemoryCopy(alignments, n->alignments, sizeof(U32)*n->column_count);
  n->alignments = alignments;
  WheelhouseRichCell *cells = push_array_no_zero(arena, WheelhouseRichCell, n->column_count*n->row_count);
  MemoryCopy(cells, n->cells, sizeof(*cells)*n->column_count*n->row_count);
  n->cells = cells;
  U64 *children = push_array_no_zero(arena, U64, n->child_cap);
  MemoryCopy(children, n->children, sizeof(U64)*n->child_count);
  n->children = children;
}

internal void
uishell_rt_node_set(UIShell_RtModel *m, UIShell_RtNode *n, WheelhouseRichNode *c)
{
  m->live_bytes -= Min(m->live_bytes, uishell_rt_node_bytes(n));
  n->kind = c->kind;
  n->level = c->level;
  n->hidden = c->hidden;
  n->task = c->task;
  n->alert = c->alert;
  n->ordered = c->ordered;
  n->tight = c->tight;
  n->version = c->version;
  n->start = c->start;
  n->text = uishell_rt_s8(c->text);
  n->language = uishell_rt_s8(c->language);
  n->label = uishell_rt_s8(c->label);
  n->media_type = uishell_rt_s8(c->media_type);
  n->spans = (WheelhouseRichSpan *)c->spans;
  n->span_count = c->span_count;
  Temp scratch = scratch_begin(0, 0);
  n->strings = push_array(scratch.arena, String8, c->string_count);
  for(U64 i = 0; i < c->string_count; i += 1) { n->strings[i] = uishell_rt_s8(c->strings[i]); }
  n->string_count = c->string_count;
  n->alignments = (U32 *)c->alignments;
  n->column_count = c->column_count;
  n->cells = (WheelhouseRichCell *)c->cells;
  n->row_count = c->row_count;
  // The child list is the node's own: it changes by CHILDREN ops only.
  U64 *children = n->children;
  uishell_rt_node_copy_content(m->arena, n);
  n->children = children;
  scratch_end(scratch);
  m->live_bytes += uishell_rt_node_bytes(n);
}

internal void
uishell_rt_children_reserve(UIShell_RtModel *m, UIShell_RtNode *n, U64 count)
{
  if(count <= n->child_cap) { return; }
  U64 cap = Max(4, n->child_cap*2);
  while(cap < count) { cap *= 2; }
  U64 *children = push_array_no_zero(m->arena, U64, cap);
  MemoryCopy(children, n->children, sizeof(U64)*n->child_count);
  m->live_bytes += (cap - n->child_cap)*sizeof(U64);
  n->children = children;
  n->child_cap = cap;
}

// Copies every live node into a fresh arena once garbage outweighs them.
internal void
uishell_rt_model_compact(UIShell_RtModel *m)
{
  if(m->arena == 0 || arena_pos(m->arena) < 4*m->live_bytes + MB(4)) { return; }
  Arena *old = m->arena;
  UIShell_RtModel next = {0};
  next.arena = arena_alloc();
  next.generation = m->generation;
  for(U64 i = 0; i < m->slots_count; i += 1)
  {
    for(UIShell_RtNode *n = m->slots[i]; n != 0; n = n->hash_next)
    {
      UIShell_RtNode *copy = uishell_rt_node_alloc(&next, n->key);
      UIShell_RtNode *hash_next = copy->hash_next;
      *copy = *n;
      copy->hash_next = hash_next;
      uishell_rt_node_copy_content(next.arena, copy);
      next.live_bytes += uishell_rt_node_bytes(copy);
    }
  }
  next.block_count = m->block_count;
  next.block_keys = push_array_no_zero(next.arena, U64, Max(m->block_count, 1));
  MemoryCopy(next.block_keys, m->block_keys, sizeof(U64)*m->block_count);
  arena_release(old);
  *m = next;
}

internal void
uishell_rt_model_release(UIShell_RtModel *m)
{
  if(m->arena) { arena_release(m->arena); }
  MemoryZeroStruct(m);
}

internal void
uishell_rt_model_order(UIShell_RtModel *m)
{
  Temp scratch = scratch_begin(0, 0);
  // Depth first from the root, with an explicit stack: lists nest deeply.
  U64 cap = 64, depth = 0, order = 0;
  U64 *stack = push_array(scratch.arena, U64, cap);
  stack[depth++] = WHEELHOUSE_RICH_ROOT_KEY;
  while(depth > 0)
  {
    UIShell_RtNode *n = uishell_rt_node(m, stack[--depth]);
    if(n == 0) { continue; }
    if(n->kind >= WHEELHOUSE_RICH_KIND_PARAGRAPH) { n->order = order++; }
    if(depth + n->child_count > cap)
    {
      U64 new_cap = Max(cap*2, depth + n->child_count);
      U64 *bigger = push_array(scratch.arena, U64, new_cap);
      MemoryCopy(bigger, stack, sizeof(U64)*depth);
      stack = bigger;
      cap = new_cap;
    }
    for(U64 i = n->child_count; i > 0; i -= 1)
    {
      UIShell_RtNode *child = uishell_rt_node(m, n->children[i-1]);
      if(child) { child->parent = n->key; }
      stack[depth++] = n->children[i-1];
    }
  }
  m->block_count = order;
  m->block_keys = push_array_no_zero(m->arena, U64, Max(order, 1));
  for(U64 i = 0; i < m->slots_count; i += 1)
  {
    for(UIShell_RtNode *n = m->slots[i]; n != 0; n = n->hash_next)
    {
      if(n->kind >= WHEELHOUSE_RICH_KIND_PARAGRAPH && n->order < order) { m->block_keys[n->order] = n->key; }
    }
  }
  scratch_end(scratch);
}

internal void
uishell_rt_model_clear(UIShell_RtModel *m)
{
  if(m->arena) { arena_clear(m->arena); }
  m->slots = 0;
  m->slots_count = 0;
  m->count = 0;
  m->free_node = 0;
  m->live_bytes = 0;
  m->block_count = 0;
  m->block_keys = 0;
}

// Returns whether anything changed.
internal B32
uishell_rt_model_apply(UIShell_RtModel *m, WheelhouseRichDelta *d)
{
  if(wheelhouse_rich_delta_is_snapshot(d)) { uishell_rt_model_clear(m); }
  m->generation = wheelhouse_rich_delta_generation(d);
  U64 op_count = wheelhouse_rich_delta_op_count(d);
  for(U64 i = 0; i < op_count; i += 1)
  {
    WheelhouseRichOp op = {0};
    WheelhouseRichNode content = {0};
    if(!wheelhouse_rich_delta_op(d, i, &op)) { continue; }
    UIShell_RtNode *n = uishell_rt_node(m, op.key);
    switch(op.kind)
    {
      case WHEELHOUSE_RICH_OP_UPSERT:
      {
        if(!wheelhouse_rich_delta_node(d, i, &content)) { break; }
        if(n == 0) { n = uishell_rt_node_alloc(m, op.key); }
        uishell_rt_node_set(m, n, &content);
      } break;
      case WHEELHOUSE_RICH_OP_REMOVE:
      {
        if(n) { m->live_bytes -= Min(m->live_bytes, uishell_rt_node_bytes(n)); }
        uishell_rt_node_remove(m, op.key);
      } break;
      case WHEELHOUSE_RICH_OP_CHILDREN:
      {
        if(n == 0) { break; }
        for(U64 j = 0; j < op.list_count; j += 1)
        {
          WheelhouseRichListOp l = op.list[j];
          if(l.kind == WHEELHOUSE_LIST_INSERT && l.at <= n->child_count)
          {
            uishell_rt_children_reserve(m, n, n->child_count+1);
            MemoryCopy(&n->children[l.at+1], &n->children[l.at], sizeof(U64)*(n->child_count - l.at));
            n->children[l.at] = l.key;
            n->child_count += 1;
          }
          else if(l.kind == WHEELHOUSE_LIST_REMOVE && l.at < n->child_count)
          {
            MemoryCopy(&n->children[l.at], &n->children[l.at+1], sizeof(U64)*(n->child_count - l.at - 1));
            n->child_count -= 1;
          }
          else if(l.kind == WHEELHOUSE_LIST_MOVE && l.from < n->child_count && l.to < n->child_count)
          {
            U64 key = n->children[l.from];
            MemoryCopy(&n->children[l.from], &n->children[l.from+1], sizeof(U64)*(n->child_count - l.from - 1));
            MemoryCopy(&n->children[l.to+1], &n->children[l.to], sizeof(U64)*(n->child_count - 1 - l.to));
            n->children[l.to] = key;
          }
        }
      } break;
    }
  }
  uishell_rt_model_order(m);
  uishell_rt_model_compact(m);
  return op_count != 0;
}

////////////////////////////////
//~ Theme (#544, decision 7)

// What a run draws with, beyond its glyphs.
typedef U32 UIShell_RtInk;
enum
{
  UIShell_RtInk_Link       = (1<<0),
  UIShell_RtInk_Strike     = (1<<1),
  UIShell_RtInk_CodeWash   = (1<<2),
  UIShell_RtInk_Literal    = (1<<3),
  UIShell_RtInk_Weak       = (1<<4),
  UIShell_RtInk_Code       = (1<<5),
};

typedef struct UIShell_RtStyleInk UIShell_RtStyleInk;
struct UIShell_RtStyleInk
{
  UIShell_RtInk ink;
  // For UIShell_RtInk_Literal: 0xRRGGBB, or a palette index with bit 24 set.
  U32 color;
};

// Fields that change layout are apart from the colours: a theme change lays
// out again only when metrics_hash changes.
typedef struct UIShell_RtTheme UIShell_RtTheme;
struct UIShell_RtTheme
{
  F32 size;
  F32 heading_scale[6];
  F32 code_scale;
  U64 metrics_hash;
  Vec4F32 text;
  Vec4F32 weak;
  Vec4F32 link;
  Vec4F32 code_wash;
  Vec4F32 code_block;
  Vec4F32 rule;
  Vec4F32 selection;
  Vec4F32 find;
  Vec4F32 find_current;
  Vec4F32 focus;
  U64 colors_hash;
};

internal UIShell_RtTheme
uishell_rt_theme(void)
{
  UIShell_RtTheme t = {0};
  t.size = rd_font_size();
  F32 headings[6] = {1.9f, 1.55f, 1.3f, 1.15f, 1.f, 0.92f};
  MemoryCopyArray(t.heading_scale, headings);
  t.code_scale = 0.92f;
  t.metrics_hash = XXH3_64bits(&t, OffsetOf(UIShell_RtTheme, metrics_hash));
  Vec4F32 background = ui_color_from_name(str8_lit("background"));
  t.text = ui_color_from_name(str8_lit("text"));
  t.weak = t.text; t.weak.w *= 0.6f;
  t.link = rd_accent_color();
  t.code_wash = mix_4f32(background, t.text, 0.08f);
  t.code_block = mix_4f32(background, t.text, 0.05f);
  t.rule = ui_color_from_name(str8_lit("border"));
  t.selection = ui_color_from_name(str8_lit("selection")); t.selection.w = Max(t.selection.w*0.5f, 0.3f);
  t.find = t.link; t.find.w = 0.25f;
  t.find_current = t.link; t.find_current.w = 0.55f;
  t.focus = t.link;
  t.colors_hash = XXH3_64bits(&t.text, sizeof(Vec4F32)*10);
  return t;
}

internal Vec4F32
uishell_rt_ink_color(UIShell_RtTheme *t, UIShell_RtStyleInk *s)
{
  if(s->ink & UIShell_RtInk_Literal)
  {
    if(!(s->color & (1u<<24)))
    {
      return v4f32(((s->color>>16)&0xff)/255.f, ((s->color>>8)&0xff)/255.f, (s->color&0xff)/255.f, t->text.w);
    }
    // ANSI's eight, for palette indices.
    local_persist U32 palette[8] = {0x000000, 0xcd3131, 0x0dbc79, 0xe5e510, 0x2472c8, 0xbc3fbc, 0x11a8cd, 0xe5e5e5};
    UIShell_RtStyleInk literal = {UIShell_RtInk_Literal, palette[s->color & 7]};
    return uishell_rt_ink_color(t, &literal);
  }
  if(s->ink & UIShell_RtInk_Link) { return t->link; }
  if(s->ink & UIShell_RtInk_Weak) { return t->weak; }
  return t->text;
}

////////////////////////////////
//~ Block geometry

// Where a Block sits: its text's inset from the content's left, the space
// outside it, a code Block's padding, and what decorates it.
typedef struct UIShell_RtFrame UIShell_RtFrame;
struct UIShell_RtFrame
{
  F32 indent;
  F32 pad_top;
  F32 pad_bottom;
  F32 inset_x;
  F32 inset_y;
  U32 quote_depth;
  F32 quote_x[8];
  U32 alert;
  // A list item's marker or a footnote's label, set left of its first line;
  // a caption, set above (mermaid's).
  String8 label;
  B32 label_above;
  B32 code;
  B32 rule;
  B32 table;
};

internal F32
uishell_rt_px(UIShell_RtTheme *t)
{
  return t->size*(96.f/72.f);
}

internal F32
uishell_rt_line_px(UIShell_RtTheme *t)
{
  return floor_f32(uishell_rt_px(t)*1.45f);
}

internal UIShell_RtFrame
uishell_rt_frame(Arena *arena, UIShell_RtModel *m, UIShell_RtTheme *t, UIShell_RtNode *n)
{
  UIShell_RtFrame f = {0};
  F32 px = uishell_rt_px(t);
  F32 list_indent = floor_f32(px*1.6f), quote_indent = floor_f32(px*1.0f);
  B32 tight = 0;
  // A Block first in its list item carries the item's marker.
  UIShell_RtNode *parent = uishell_rt_node(m, n->parent);
  if(parent && parent->kind == WHEELHOUSE_RICH_KIND_LIST_ITEM && parent->child_count && parent->children[0] == n->key)
  {
    UIShell_RtNode *list = uishell_rt_node(m, parent->parent);
    if(parent->task == WHEELHOUSE_RICH_TASK_OPEN) { f.label = str8_lit("\xE2\x98\x90"); }
    else if(parent->task == WHEELHOUSE_RICH_TASK_DONE) { f.label = str8_lit("\xE2\x98\x91"); }
    else if(list && list->ordered)
    {
      U64 index = 0;
      for(U64 i = 0; i < list->child_count; i += 1) { if(list->children[i] == parent->key) { index = i; } }
      f.label = push_str8f(arena, "%llu.", (unsigned long long)(list->start + index));
    }
    else { f.label = str8_lit("\xE2\x80\xA2"); }
  }
  if(parent && parent->kind == WHEELHOUSE_RICH_KIND_FOOTNOTE && parent->child_count && parent->children[0] == n->key)
  {
    f.label = push_str8f(arena, "%S.", parent->label);
  }
  // Indents, outermost last: count them innermost first, then flip x.
  F32 depth_x[8] = {0};
  U32 quotes = 0;
  for(UIShell_RtNode *a = parent; a != 0 && a->key != WHEELHOUSE_RICH_ROOT_KEY; a = uishell_rt_node(m, a->parent))
  {
    switch(a->kind)
    {
      case WHEELHOUSE_RICH_KIND_LIST: { f.indent += list_indent; tight |= a->tight; } break;
      case WHEELHOUSE_RICH_KIND_FOOTNOTE: { f.indent += list_indent; } break;
      case WHEELHOUSE_RICH_KIND_QUOTE:
      {
        if(quotes < ArrayCount(depth_x)) { depth_x[quotes] = f.indent; }
        quotes += 1;
        f.indent += quote_indent;
        if(f.alert == 0) { f.alert = a->alert; }
      } break;
    }
  }
  f.quote_depth = Min(quotes, ArrayCount(f.quote_x));
  for(U32 i = 0; i < f.quote_depth; i += 1)
  {
    // depth_x counted from the Block outwards; the bar of quote i sits at
    // the indent outside it, measured from the left.
    f.quote_x[i] = f.indent - quote_indent - depth_x[i];
  }
  switch(n->kind)
  {
    default:
    case WHEELHOUSE_RICH_KIND_PARAGRAPH:
    {
      f.pad_bottom = floor_f32(px*(tight ? 0.2f : 0.65f));
    } break;
    case WHEELHOUSE_RICH_KIND_HEADING:
    {
      f.pad_top = floor_f32(px*(n->level <= 2 ? 0.9f : 0.6f));
      f.pad_bottom = floor_f32(px*0.4f);
    } break;
    case WHEELHOUSE_RICH_KIND_CODE:
    {
      f.code = 1;
      f.inset_x = floor_f32(px*0.7f);
      f.inset_y = floor_f32(px*0.5f);
      f.pad_top = floor_f32(px*0.15f);
      f.pad_bottom = floor_f32(px*0.75f);
      if(str8_match(n->language, str8_lit("mermaid"), StringMatchFlag_CaseInsensitive))
      {
        f.label = str8_lit("Mermaid diagram, shown as its source");
        f.label_above = 1;
      }
    } break;
    case WHEELHOUSE_RICH_KIND_RULE:
    {
      f.rule = 1;
      f.pad_top = floor_f32(px*0.5f);
      f.pad_bottom = floor_f32(px*0.5f);
    } break;
    case WHEELHOUSE_RICH_KIND_EMBED:
    {
      f.table = n->column_count != 0;
      f.pad_top = floor_f32(px*0.15f);
      f.pad_bottom = floor_f32(px*0.75f);
    } break;
  }
  if(f.label_above) { f.inset_y += uishell_rt_line_px(t); }
  return f;
}

////////////////////////////////
//~ Styles: semantic spans to the style table (#544, decision 7)

typedef U32 UIShell_RtAttrs;
enum
{
  UIShell_RtAttr_Emphasis  = (1<<0),
  UIShell_RtAttr_Strong    = (1<<1),
  UIShell_RtAttr_Code      = (1<<2),
  UIShell_RtAttr_Strike    = (1<<3),
  UIShell_RtAttr_Link      = (1<<4),
  UIShell_RtAttr_Footnote  = (1<<5),
  UIShell_RtAttr_Color     = (1<<6),
};

// Ligatures off for code: its glyphs are its characters.
read_only global WhTextFeature uishell_rt_no_ligatures[] =
{
  {{'l', 'i', 'g', 'a'}, 0}, {{'c', 'a', 'l', 't'}, 0}, {{'d', 'l', 'i', 'g'}, 0},
};

typedef struct UIShell_RtStyled UIShell_RtStyled;
struct UIShell_RtStyled
{
  WhTextStyleRun *runs;
  U64 run_count;
  WhTextStyle *styles;
  UIShell_RtStyleInk *inks;
  U64 style_count;
};

internal int
uishell_rt_u32_compare(const void *a, const void *b)
{
  U32 x = *(const U32 *)a, y = *(const U32 *)b;
  return x < y ? -1 : x > y ? 1 : 0;
}

internal WhTextStyle
uishell_rt_base_style(UIShell_RtTheme *t, UIShell_RtNode *n)
{
  WhTextStyle s = {WH_TEXT_FAMILY_MAIN, t->size, 400.f, 0, 0, 0, 0.f};
  if(n->kind == WHEELHOUSE_RICH_KIND_HEADING)
  {
    s.size = t->size*t->heading_scale[Clamp(1, n->level, 6)-1];
    s.weight = 700.f;
  }
  else if(n->kind == WHEELHOUSE_RICH_KIND_CODE)
  {
    s.family = WH_TEXT_FAMILY_CODE;
    s.size = t->size*t->code_scale;
    s.features = uishell_rt_no_ligatures;
    s.feature_count = ArrayCount(uishell_rt_no_ligatures);
  }
  return s;
}

internal UIShell_RtStyled
uishell_rt_styled(Arena *arena, UIShell_RtTheme *t, UIShell_RtNode *n)
{
  UIShell_RtStyled result = {0};
  U64 cap = 1 + 2*n->span_count;
  result.styles = push_array(arena, WhTextStyle, cap);
  result.inks = push_array(arena, UIShell_RtStyleInk, cap);
  UIShell_RtAttrs *style_attrs = push_array(arena, UIShell_RtAttrs, cap);
  U32 *style_colors = push_array(arena, U32, cap);
  result.styles[0] = uishell_rt_base_style(t, n);
  result.inks[0].ink = n->kind == WHEELHOUSE_RICH_KIND_CODE ? UIShell_RtInk_Code : 0;
  result.style_count = 1;
  if(n->span_count == 0) { return result; }
  U32 *bounds = push_array(arena, U32, 2*n->span_count);
  for(U64 i = 0; i < n->span_count; i += 1)
  {
    bounds[2*i] = n->spans[i].start;
    bounds[2*i+1] = n->spans[i].start + n->spans[i].len;
  }
  qsort(bounds, 2*n->span_count, sizeof(U32), uishell_rt_u32_compare);
  result.runs = push_array(arena, WhTextStyleRun, 2*n->span_count);
  for(U64 b = 0; b+1 < 2*n->span_count; b += 1)
  {
    U32 start = bounds[b], end = bounds[b+1];
    if(start == end || end > n->text.size) { continue; }
    UIShell_RtAttrs attrs = 0;
    U32 color = 0;
    for(U64 i = 0; i < n->span_count; i += 1)
    {
      WheelhouseRichSpan *s = &n->spans[i];
      if(s->start > start || s->start + s->len < end) { continue; }
      switch(s->attr)
      {
        case WHEELHOUSE_RICH_ATTR_EMPHASIS: { attrs |= UIShell_RtAttr_Emphasis; } break;
        case WHEELHOUSE_RICH_ATTR_STRONG: { attrs |= UIShell_RtAttr_Strong; } break;
        case WHEELHOUSE_RICH_ATTR_CODE:
        case WHEELHOUSE_RICH_ATTR_MATH:
        case WHEELHOUSE_RICH_ATTR_DISPLAY_MATH: { attrs |= UIShell_RtAttr_Code; } break;
        case WHEELHOUSE_RICH_ATTR_STRIKETHROUGH: { attrs |= UIShell_RtAttr_Strike; } break;
        case WHEELHOUSE_RICH_ATTR_LINK: { attrs |= UIShell_RtAttr_Link; } break;
        case WHEELHOUSE_RICH_ATTR_FOOTNOTE_REFERENCE: { attrs |= UIShell_RtAttr_Footnote; } break;
        case WHEELHOUSE_RICH_ATTR_COLOR_RGB: { attrs |= UIShell_RtAttr_Color; color = s->value & 0xffffff; } break;
        case WHEELHOUSE_RICH_ATTR_COLOR_PALETTE: { attrs |= UIShell_RtAttr_Color; color = (1u<<24) | (s->value & 0xff); } break;
      }
    }
    if(attrs == 0) { continue; }
    U64 style = 0;
    for(U64 i = 1; i < result.style_count && style == 0; i += 1)
    {
      if(style_attrs[i] == attrs && style_colors[i] == color) { style = i; }
    }
    if(style == 0)
    {
      style = result.style_count++;
      WhTextStyle s = result.styles[0];
      UIShell_RtStyleInk ink = result.inks[0];
      if(attrs & UIShell_RtAttr_Emphasis) { s.italic = 1; }
      if(attrs & UIShell_RtAttr_Strong) { s.weight = 700.f; }
      if((attrs & UIShell_RtAttr_Code) && s.family != WH_TEXT_FAMILY_CODE)
      {
        s.family = WH_TEXT_FAMILY_CODE;
        s.size *= t->code_scale;
        s.features = uishell_rt_no_ligatures;
        s.feature_count = ArrayCount(uishell_rt_no_ligatures);
        ink.ink |= UIShell_RtInk_CodeWash;
      }
      if(attrs & (UIShell_RtAttr_Link|UIShell_RtAttr_Footnote)) { ink.ink |= UIShell_RtInk_Link; }
      if(attrs & UIShell_RtAttr_Strike) { ink.ink |= UIShell_RtInk_Strike; }
      if(attrs & UIShell_RtAttr_Color) { ink.ink |= UIShell_RtInk_Literal; ink.color = color; }
      result.styles[style] = s;
      result.inks[style] = ink;
      style_attrs[style] = attrs;
      style_colors[style] = color;
    }
    result.runs[result.run_count++] = (WhTextStyleRun){start, end - start, (U32)style};
  }
  return result;
}

////////////////////////////////
//~ Layout cache (#544, decisions 5 and 6)

// A Block's layout, copied out of the scratch it was laid out in into one
// blob: this header, then every array it points to.
typedef struct UIShell_RtLayout UIShell_RtLayout;
struct UIShell_RtLayout
{
  WhTextLayout text;
  UIShell_RtStyleInk *inks;
  U64 style_count;
  // The frame's label, laid out on its own; no lines if none.
  WhTextLayout label;
};

typedef struct UIShell_RtLayoutEntry UIShell_RtLayoutEntry;
struct UIShell_RtLayoutEntry
{
  UIShell_RtLayoutEntry *hash_next;
  UIShell_RtLayoutEntry *lru_prev;
  UIShell_RtLayoutEntry *lru_next;
  U64 key;
  U64 version;
  U32 width_bits;
  U64 epoch;
  U64 metrics_hash;
  // A list item's number lives in its list, not its Block's version.
  U64 label_hash;
  UIShell_RtLayout *layout;
  U32 size_class;
  U64 bytes;
  // Unique per layout made: a draw bucket built from it remembers it.
  U64 stamp;
};

#define UISHELL_RT_LAYOUT_SLOTS 4096
// Laid-out Blocks kept, by their blobs' class bytes.
#define UISHELL_RT_LAYOUT_BUDGET MB(24)

typedef struct UIShell_RtLayoutCache UIShell_RtLayoutCache;
struct UIShell_RtLayoutCache
{
  Arena *arena;
  UIShell_RtBlobPool pool;
  UIShell_RtLayoutEntry *slots[UISHELL_RT_LAYOUT_SLOTS];
  UIShell_RtLayoutEntry *lru_first;
  UIShell_RtLayoutEntry *lru_last;
  UIShell_RtLayoutEntry *free_entry;
  U64 budget;
  U64 next_stamp;
  U64 entry_count;
  // Since the View opened: Blocks laid out, and the time it took.
  U64 layouts;
  U64 layout_us;
  U64 evictions;
};

internal U64
uishell_rt_layout_hash(U64 key, U64 version, U32 width_bits, U64 epoch, U64 metrics_hash, U64 label_hash)
{
  U64 fields[] = {key, version, width_bits, epoch, metrics_hash, label_hash};
  return XXH3_64bits(fields, sizeof(fields));
}

internal void
uishell_rt_layout_evict(UIShell_RtLayoutCache *cache, UIShell_RtLayoutEntry *e)
{
  U64 hash = uishell_rt_layout_hash(e->key, e->version, e->width_bits, e->epoch, e->metrics_hash, e->label_hash);
  for(UIShell_RtLayoutEntry **at = &cache->slots[hash % UISHELL_RT_LAYOUT_SLOTS]; *at != 0; at = &(*at)->hash_next)
  {
    if(*at == e) { *at = e->hash_next; break; }
  }
  DLLRemove_NP(cache->lru_first, cache->lru_last, e, lru_next, lru_prev);
  uishell_rt_blob_free(&cache->pool, (U8 *)e->layout, e->size_class);
  e->hash_next = cache->free_entry;
  cache->free_entry = e;
  cache->entry_count -= 1;
  cache->evictions += 1;
}

internal void
uishell_rt_layout_cache_release(UIShell_RtLayoutCache *cache)
{
  uishell_rt_pool_release(&cache->pool);
  if(cache->arena) { arena_release(cache->arena); }
  MemoryZeroStruct(cache);
}

// Copies `src`'s arrays to `at` and points `dst` at the copies.
internal U8 *
uishell_rt_copy_layout(U8 *at, WhTextLayout *dst, WhTextLayout *src)
{
  *dst = *src;
#define UISHELL_RT_COPY_ARRAY(field, count) \
  if(src->count) { at = (U8 *)AlignPow2((U64)at, 8); MemoryCopy(at, src->field, sizeof(*src->field)*src->count); dst->field = (void *)at; at += sizeof(*src->field)*src->count; }
  UISHELL_RT_COPY_ARRAY(lines, line_count);
  UISHELL_RT_COPY_ARRAY(runs, run_count);
  UISHELL_RT_COPY_ARRAY(glyphs, glyph_count);
  UISHELL_RT_COPY_ARRAY(clusters, cluster_count);
  UISHELL_RT_COPY_ARRAY(boxes, box_count);
#undef UISHELL_RT_COPY_ARRAY
  return at;
}

internal U64
uishell_rt_layout_arrays_size(WhTextLayout *l)
{
  return (sizeof(WhTextLine)*l->line_count + sizeof(WhTextRun)*l->run_count + sizeof(WhTextGlyph)*l->glyph_count +
          sizeof(WhTextCluster)*l->cluster_count + sizeof(WhTextBox)*l->box_count + 5*8);
}

// The Block's layout at text width `width`, from the cache or laid out now.
internal UIShell_RtLayoutEntry *
uishell_rt_layout(UIShell_RtLayoutCache *cache, UIShell_RtTheme *t, UIShell_RtNode *n, UIShell_RtFrame *frame, F32 width)
{
  WhText *ctx = uishell_rt_text();
  U64 epoch = wh_text_epoch(ctx);
  width = Max(width, 1.f);
  U32 width_bits = 0;
  MemoryCopy(&width_bits, &width, sizeof(width_bits));
  U64 label_hash = frame->label.size ? XXH3_64bits(frame->label.str, frame->label.size) : 0;
  U64 hash = uishell_rt_layout_hash(n->key, n->version, width_bits, epoch, t->metrics_hash, label_hash);
  UIShell_RtLayoutEntry **slot = &cache->slots[hash % UISHELL_RT_LAYOUT_SLOTS];
  for(UIShell_RtLayoutEntry *e = *slot; e != 0; e = e->hash_next)
  {
    if(e->key == n->key && e->version == n->version && e->width_bits == width_bits && e->epoch == epoch &&
       e->metrics_hash == t->metrics_hash && e->label_hash == label_hash)
    {
      DLLRemove_NP(cache->lru_first, cache->lru_last, e, lru_next, lru_prev);
      DLLPushBack_NP(cache->lru_first, cache->lru_last, e, lru_next, lru_prev);
      return e;
    }
  }
  U64 begin_us = now_time_us();
  Temp scratch = scratch_begin(0, 0);
  UIShell_RtStyled styled = uishell_rt_styled(scratch.arena, t, n);
  WhTextInput input = {0};
  input.text = uishell_rt_wt(n->text);
  input.runs = styled.runs;
  input.run_count = styled.run_count;
  input.styles = styled.styles;
  input.style_count = styled.style_count;
  input.align = WH_TEXT_ALIGN_START;
  WhTextLayout text = {0}, label = {0};
  char *error = 0;
  if(!wh_text_layout(ctx, &input, width, uishell_rt_allocator(scratch.arena), &text, &error))
  {
    if(error) { wheelhouse_string_free(error); error = 0; }
    MemoryZeroStruct(&text);
  }
  if(frame->label.size)
  {
    WhTextStyle label_style = uishell_rt_base_style(t, n);
    if(frame->label_above) { label_style.family = WH_TEXT_FAMILY_MAIN; label_style.size = t->size*0.85f; label_style.features = 0; label_style.feature_count = 0; }
    WhTextInput label_input = {0};
    label_input.text = uishell_rt_wt(frame->label);
    label_input.styles = &label_style;
    label_input.style_count = 1;
    label_input.align = frame->label_above ? WH_TEXT_ALIGN_START : WH_TEXT_ALIGN_END;
    if(!wh_text_layout(ctx, &label_input, INFINITY, uishell_rt_allocator(scratch.arena), &label, &error))
    {
      if(error) { wheelhouse_string_free(error); error = 0; }
      MemoryZeroStruct(&label);
    }
  }
  U64 bytes = (sizeof(UIShell_RtLayout) + uishell_rt_layout_arrays_size(&text) + uishell_rt_layout_arrays_size(&label) +
               sizeof(UIShell_RtStyleInk)*styled.style_count + 8);
  // Make room under the budget, least recently used first.
  if(cache->budget == 0) { cache->budget = UISHELL_RT_LAYOUT_BUDGET; }
  while(cache->lru_first != 0 && cache->pool.bytes + u64_up_to_pow2(bytes) > cache->budget)
  {
    uishell_rt_layout_evict(cache, cache->lru_first);
  }
  U32 size_class = 0;
  U8 *blob = uishell_rt_blob_alloc(&cache->pool, bytes, &size_class);
  UIShell_RtLayoutEntry *e = 0;
  if(blob != 0)
  {
    UIShell_RtLayout *layout = (UIShell_RtLayout *)blob;
    MemoryZeroStruct(layout);
    U8 *at = blob + sizeof(UIShell_RtLayout);
    at = uishell_rt_copy_layout(at, &layout->text, &text);
    at = uishell_rt_copy_layout(at, &layout->label, &label);
    at = (U8 *)AlignPow2((U64)at, 8);
    layout->inks = (UIShell_RtStyleInk *)at;
    MemoryCopy(at, styled.inks, sizeof(UIShell_RtStyleInk)*styled.style_count);
    layout->style_count = styled.style_count;
    if(cache->arena == 0) { cache->arena = arena_alloc(.reserve_size = MB(8), .commit_size = KB(64)); }
    e = cache->free_entry;
    if(e) { cache->free_entry = e->hash_next; }
    else { e = push_array_no_zero(cache->arena, UIShell_RtLayoutEntry, 1); }
    MemoryZeroStruct(e);
    e->key = n->key;
    e->version = n->version;
    e->width_bits = width_bits;
    e->epoch = epoch;
    e->metrics_hash = t->metrics_hash;
    e->label_hash = label_hash;
    e->layout = layout;
    e->size_class = size_class;
    e->bytes = bytes;
    e->stamp = ++cache->next_stamp;
    e->hash_next = *slot;
    *slot = e;
    DLLPushBack_NP(cache->lru_first, cache->lru_last, e, lru_next, lru_prev);
    cache->entry_count += 1;
  }
  scratch_end(scratch);
  cache->layouts += 1;
  cache->layout_us += now_time_us() - begin_us;
  return e;
}

////////////////////////////////
//~ View state

// A table Embed (#542): a binnacle Grid over the table's cells, kept while
// its Block lives, scrolled out of view or not.
typedef struct UIShell_RtTable UIShell_RtTable;
struct UIShell_RtTable
{
  UIShell_RtTable *next;
  U64 key;
  U64 version;
  WheelhouseTable *table;
  B32 expanded;
  Vec2F32 last_pointer;
  B32 pointer_inside;
  U64 last_frame;
};

// A Block's retained draw bucket, in the Block's own coordinates.
typedef struct UIShell_RtBucket UIShell_RtBucket;
struct UIShell_RtBucket
{
  UIShell_RtBucket *next;
  U64 key;
  Arena *arena;
  DR_Bucket *bucket;
  // What it was built from: rebuilt when any changes.
  U64 layout_stamp;
  U64 colors_hash;
  U64 glyph_epoch;
  U32 width_bits;
  B32 incomplete;
  U64 last_frame;
  // Glyph pages it draws from, kept from eviction while it shows.
  U64 pages[16];
  U32 page_count;
  B32 pages_overflow;
};

// A focusable thing in the Block order, for Tab: a link, an Embed, a code
// Block's copy button or a capped table's expand toggle.
typedef U32 UIShell_RtStopKind;
enum
{
  UIShell_RtStop_Link,
  UIShell_RtStop_Embed,
  UIShell_RtStop_CopyButton,
  UIShell_RtStop_Expand,
};

typedef struct UIShell_RtStop UIShell_RtStop;
struct UIShell_RtStop
{
  UIShell_RtStopKind kind;
  U64 key;
  U32 start;
  U32 end;
};

typedef struct UIShell_RtStats UIShell_RtStats;
struct UIShell_RtStats
{
  U64 frames;
  U64 blocks_boxed;
  U64 bucket_builds;
  // This frame's layout: how many Blocks, and its time.
  U64 frame_layouts;
  U64 frame_layout_us;
};

typedef struct UIShell_RichTextState UIShell_RichTextState;
struct UIShell_RichTextState
{
  B32 initialized;
  Arena *arena;
  WheelhouseRichText *doc;
  U64 sub;
  U128 loaded_hash;
  U8 path[1024];
  U64 path_size;
  String8 load_error;
  UIShell_RtModel model;
  UIShell_RtLayoutCache layouts;
  UIShell_RtBucket *buckets;
  UIShell_RtBucket *free_bucket;
  UIShell_RtTable *tables;
  UIShell_RtTheme theme;
  F32 width;
  F64 viewport;
  // Pointer selection: the unit a drag extends by (1 char, 2 word, 3 Block).
  U32 select_unit;
  B32 selecting;
  B32 drag_moved;
  U64 press_key;
  U32 press_byte;
  U32 word_start;
  U32 word_end;
  // The selection last sent to PRIMARY, so a drag sends it once.
  B32 selection_to_primary;
  // Keyboard: the selection's focus moves from here.
  U64 caret_key;
  U32 caret_byte;
  B32 has_caret;
  // The focused table Embed, or 0.
  U64 focus_embed;
  // The Tab stop with focus, or -1.
  S64 focus_stop;
  UIShell_RtStop focus_stop_value;
  // Find: the query scanned for, and the matches so far, in order.
  U8 find_query[256];
  U64 find_query_size;
  B32 find_active;
  B32 find_forward;
  WheelhouseRichMatch *matches;
  U64 match_count;
  U64 match_cap;
  U64 match_current;
  Arena *match_arena;
  B32 find_pending_step;
  // The anchor as of the last frame, and the layout it was on: a new
  // layout of its Block keeps its line in place.
  WheelhouseRichAnchor last_anchor;
  B32 has_last_anchor;
  U64 anchor_layout_stamp;
  // Presses, for double and triple clicks.
  U64 last_press_us;
  Vec2F32 last_press_pos;
  U32 press_count;
  B32 press_link;
  // The table Embed a press went to, which gets the drag and release.
  U64 table_capture;
  UIShell_RtStats stats;
  B32 show_copied;
  U64 copied_key;
  U64 copied_until_us;
};

internal void
uishell_rt_free_tables(UIShell_RichTextState *s)
{
  for(UIShell_RtTable *t = s->tables; t != 0; t = t->next)
  {
    if(t->table) { wheelhouse_table_free(t->table); t->table = 0; }
  }
  s->tables = 0;
}

internal void
uishell_rt_release(void *user_data)
{
  UIShell_RichTextState *s = (UIShell_RichTextState *)user_data;
  uishell_rt_free_tables(s);
  for(UIShell_RtBucket *b = s->buckets; b != 0; b = b->next) { if(b->arena) { arena_release(b->arena); } }
  for(UIShell_RtBucket *b = s->free_bucket; b != 0; b = b->next) { if(b->arena) { arena_release(b->arena); } }
  s->buckets = s->free_bucket = 0;
  uishell_rt_layout_cache_release(&s->layouts);
  uishell_rt_model_release(&s->model);
  if(s->match_arena) { arena_release(s->match_arena); s->match_arena = 0; }
  if(s->doc) { wheelhouse_rich_text_free(s->doc); s->doc = 0; }
  if(s->arena) { arena_release(s->arena); s->arena = 0; }
}

internal String8
uishell_rt_error(char **error)
{
  String8 result = str8_zero();
  if(*error)
  {
    result = push_str8_copy(rd_frame_arena(), str8_cstring(*error));
    wheelhouse_string_free(*error);
    *error = 0;
  }
  return result;
}

////////////////////////////////
//~ Table Embeds

internal UIShell_RtTable *
uishell_rt_table(UIShell_RichTextState *s, UIShell_RtNode *n, F32 line, F32 digit)
{
  UIShell_RtTable *t = 0;
  for(t = s->tables; t != 0 && t->key != n->key; t = t->next) {}
  if(t == 0)
  {
    t = push_array(s->arena, UIShell_RtTable, 1);
    t->key = n->key;
    SLLStackPush(s->tables, t);
  }
  if(t->table == 0 || t->version != n->version)
  {
    if(t->table) { wheelhouse_table_free(t->table); }
    Temp scratch = scratch_begin(0, 0);
    U64 count = n->column_count*n->row_count;
    WheelhouseText *cells = push_array(scratch.arena, WheelhouseText, Max(count, 1));
    for(U64 i = 0; i < count; i += 1)
    {
      WheelhouseRichCell c = n->cells[i];
      cells[i] = uishell_rt_wt(str8_substr(n->text, r1u64(c.start, c.start + c.len)));
    }
    U32 *right = push_array(scratch.arena, U32, Max(n->column_count, 1));
    for(U64 c = 0; c < n->column_count; c += 1) { right[c] = n->alignments[c] == WHEELHOUSE_RICH_ALIGN_RIGHT; }
    t->table = wheelhouse_table_open_cells(cells, n->column_count, n->row_count, right, line, digit);
    t->version = n->version;
    scratch_end(scratch);
  }
  return t;
}

// The tables whose Blocks have gone go with them (#542, decision 1).
internal void
uishell_rt_prune_tables(UIShell_RichTextState *s)
{
  for(UIShell_RtTable **at = &s->tables; *at != 0;)
  {
    UIShell_RtNode *n = uishell_rt_node(&s->model, (*at)->key);
    if(n == 0 || n->kind != WHEELHOUSE_RICH_KIND_EMBED)
    {
      if((*at)->table) { wheelhouse_table_free((*at)->table); }
      if(s->focus_embed == (*at)->key) { s->focus_embed = 0; }
      *at = (*at)->next;
    }
    else { at = &(*at)->next; }
  }
}

internal UIShell_RtTable *
uishell_rt_table_of(UIShell_RichTextState *s, U64 key)
{
  for(UIShell_RtTable *t = s->tables; t != 0; t = t->next) { if(t->key == key) { return t; } }
  return 0;
}

// Shown at most this many rows inline, and at most 60% of the viewport
// (#542, decision 2), unless expanded.
#define UISHELL_RT_TABLE_INLINE_ROWS 12

internal F32
uishell_rt_table_line(UIShell_RtTheme *t)
{
  return floor_f32(uishell_rt_px(t)*1.6f);
}

// The table's own height, and whether a cap cut it (so it offers "expand").
internal F32
uishell_rt_table_height(UIShell_RtTheme *t, UIShell_RtNode *n, B32 expanded, F64 viewport, B32 *capped_out)
{
  F32 line = uishell_rt_table_line(t);
  U64 body_rows = n->row_count ? n->row_count - 1 : 0;
  // A header band, the rows with their gridlines, and room for a
  // horizontal scroll thumb.
  F32 full = line + body_rows*(line + 1.f) + 4.f;
  F32 cap = Min(line + UISHELL_RT_TABLE_INLINE_ROWS*(line + 1.f) + 4.f, (F32)Max(viewport*0.6, line*3));
  *capped_out = full > cap;
  return expanded ? full : Min(full, cap);
}

////////////////////////////////
//~ Measuring

internal F32
uishell_rt_text_width(UIShell_RtFrame *f, F32 content_width)
{
  return Max(content_width - f->indent - 2*f->inset_x, 8.f);
}

// A Block's height at content width `content_width`, laying it out if it
// isn't already: what set_height reports.
internal F32
uishell_rt_block_height(UIShell_RichTextState *s, UIShell_RtNode *n, F32 content_width, UIShell_RtLayoutEntry **layout_out)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_RtFrame f = uishell_rt_frame(scratch.arena, &s->model, &s->theme, n);
  F32 inner = 0;
  if(layout_out) { *layout_out = 0; }
  if(f.rule)
  {
    inner = floor_f32(uishell_rt_px(&s->theme));
  }
  else if(f.table)
  {
    UIShell_RtTable *t = 0;
    for(t = s->tables; t != 0 && t->key != n->key; t = t->next) {}
    B32 capped = 0;
    inner = uishell_rt_table_height(&s->theme, n, t && t->expanded, s->viewport, &capped);
    // "Expand" or "Collapse" sits on a line under a capped table.
    if(capped || (t && t->expanded)) { inner += uishell_rt_line_px(&s->theme); }
  }
  else
  {
    UIShell_RtLayoutEntry *e = uishell_rt_layout(&s->layouts, &s->theme, n, &f, uishell_rt_text_width(&f, content_width));
    if(e) { inner = e->layout->text.height; }
    if(layout_out) { *layout_out = e; }
  }
  F32 height = ceil_f32(f.pad_top + 2*f.inset_y + inner + f.pad_bottom);
  scratch_end(scratch);
  return height;
}

////////////////////////////////
//~ Drawing (#544, decision 8)

internal U64
uishell_rt_glyph_epoch(void)
{
  FNT_GlyphCache *cache = fnt_glyph_cache();
  U32 gamma_bits = 0;
  MemoryCopy(&gamma_bits, &cache->gamma, sizeof(gamma_bits));
  // A gamma change clears the mask pages without counting an eviction.
  return cache->stats.evictions ^ ((U64)gamma_bits << 32);
}

// A retained bucket draws from pages the cache doesn't see used: mark them
// used each frame it shows, so they aren't the ones evicted.
internal void
uishell_rt_touch_pages(UIShell_RtBucket *b)
{
  FNT_GlyphCache *cache = fnt_glyph_cache();
  for EachIndex(kind, FNT_GlyphKind_COUNT)
  {
    FNT_GlyphAtlas *atlas = &cache->atlases[kind];
    for EachIndex(p, atlas->page_count)
    {
      FNT_GlyphPage *page = &atlas->pages[p];
      B32 used = b->pages_overflow;
      for(U32 i = 0; i < b->page_count && !used; i += 1) { used = b->pages[i] == page->handle; }
      if(used) { page->last_used_frame = cache->frame_index; }
    }
  }
}

internal void
uishell_rt_note_page(UIShell_RtBucket *b, U64 page)
{
  for(U32 i = 0; i < b->page_count; i += 1) { if(b->pages[i] == page) { return; } }
  if(b->page_count < ArrayCount(b->pages)) { b->pages[b->page_count++] = page; }
  else { b->pages_overflow = 1; }
}

// Draws a layout's glyphs at `origin`, in `color` or its runs' inks.
// Returns 0 if a glyph couldn't be placed this frame.
internal B32
uishell_rt_draw_glyphs(UIShell_RtBucket *b, UIShell_RtTheme *t, WhTextLayout *l, UIShell_RtStyleInk *inks, U64 ink_count,
                       Vec2F32 origin, Vec4F32 *color)
{
  B32 complete = 1;
  F32 scale = dr_raster_scale();
  WhText *ctx = uishell_rt_text();
  for(U64 r = 0; r < l->run_count; r += 1)
  {
    WhTextRun *run = &l->runs[r];
    UIShell_RtStyleInk ink = run->style < ink_count ? inks[run->style] : (UIShell_RtStyleInk){0};
    Vec4F32 run_color = color ? *color : uishell_rt_ink_color(t, &ink);
    FNT_GlyphParams params = {0};
    params.tag = uishell_rt_tag_from_font(run->font);
    params.size_px = run->size*scale;
    params.synthesis = run->embolden ? FNT_GlyphSynthesis_Embolden : 0;
    params.skew = run->skew;
    params.hinting = FNT_GlyphHinting_Vertical;
    const int16_t *coords = 0;
    params.coord_count = run->coords ? wh_text_coords(ctx, run->coords, &coords) : 0;
    params.coords = (S16 *)coords;
    if(fnt_tag_match(params.tag, fnt_tag_zero())) { continue; }
    for(U32 g = run->glyph_start; g < run->glyph_start + run->glyph_count && g < l->glyph_count; g += 1)
    {
      WhTextGlyph *glyph = &l->glyphs[g];
      S32 whole = 0;
      fnt_glyph_pen_split((origin.x + glyph->x)*scale, &whole, &params.subpixel_x);
      params.glyph_id = glyph->id;
      FNT_GlyphDraw d = fnt_glyph(&params);
      if(!d.glyph.ok) { complete = 0; continue; }
      if(d.glyph.page == 0) { continue; }
      uishell_rt_note_page(b, d.glyph.page);
      Vec2S16 dim = v2s16(d.glyph.region.x1 - d.glyph.region.x0, d.glyph.region.y1 - d.glyph.region.y0);
      F32 baseline = round_f32((origin.y + glyph->y)*scale);
      Rng2F32 dst = r2f32p((whole + d.glyph.left)/scale, (baseline - d.glyph.top)/scale,
                           (whole + d.glyph.left + dim.x)/scale, (baseline - d.glyph.top + dim.y)/scale);
      Rng2F32 src = r2f32p(d.glyph.region.x0, d.glyph.region.y0, d.glyph.region.x1, d.glyph.region.y1);
      Vec4F32 glyph_color = d.glyph.kind == FNT_GlyphKind_Color ? v4f32(1, 1, 1, run_color.w) : run_color;
      dr_img(dst, src, d.texture, glyph_color, 0, 0, 0);
    }
  }
  return complete;
}

// Inline code's wash, behind its glyphs; strikes and underlines over them.
internal void
uishell_rt_draw_run_marks(UIShell_RtTheme *t, WhTextLayout *l, UIShell_RtStyleInk *inks, U64 ink_count, Vec2F32 origin, B32 washes)
{
  for(U64 r = 0; r < l->run_count; r += 1)
  {
    WhTextRun *run = &l->runs[r];
    if(run->style >= ink_count || run->line >= l->line_count) { continue; }
    UIShell_RtStyleInk ink = inks[run->style];
    WhTextLine *line = &l->lines[run->line];
    F32 x0 = origin.x + run->x, x1 = x0 + run->advance;
    if(washes && (ink.ink & UIShell_RtInk_CodeWash))
    {
      F32 pad = 2.f;
      dr_rect(r2f32p(x0 - pad, origin.y + line->y + 1, x1 + pad, origin.y + line->y + line->height - 1), t->code_wash, 3.f, 0, 1.f);
    }
    if(!washes)
    {
      Vec4F32 color = uishell_rt_ink_color(t, &ink);
      F32 baseline = origin.y + run->y;
      if(ink.ink & UIShell_RtInk_Link)
      {
        F32 y = floor_f32(baseline + run->underline_offset);
        dr_rect(r2f32p(x0, y, x1, y + Max(1.f, run->underline_size)), color, 0, 0, 0);
      }
      if(ink.ink & UIShell_RtInk_Strike)
      {
        F32 y = floor_f32(baseline + run->strikethrough_offset);
        dr_rect(r2f32p(x0, y, x1, y + Max(1.f, run->strikethrough_size)), color, 0, 0, 0);
      }
    }
  }
}

// Builds a Block's bucket in its own coordinates: (0, 0) is its box's top
// left, `width` its box's width.
internal void
uishell_rt_build_bucket(UIShell_RichTextState *s, UIShell_RtBucket *b, UIShell_RtNode *n, UIShell_RtLayoutEntry *e, F32 width, F32 height)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_RtTheme *t = &s->theme;
  UIShell_RtFrame f = uishell_rt_frame(scratch.arena, &s->model, t, n);
  if(b->arena == 0) { b->arena = arena_alloc(.reserve_size = MB(4), .commit_size = KB(16), .name = "rich text block bucket"); }
  else { arena_clear(b->arena); }
  b->bucket = dr_bucket_make_on(b->arena);
  b->page_count = 0;
  b->pages_overflow = 0;
  B32 complete = 1;
  DR_BucketScope(b->bucket)
  {
    for(U32 q = 0; q < f.quote_depth; q += 1)
    {
      F32 x = f.quote_x[q];
      Vec4F32 bar = f.alert ? t->link : t->rule;
      dr_rect(r2f32p(x, 0, x + 3.f, height), bar, 0, 0, 0);
    }
    if(f.rule)
    {
      F32 y = floor_f32(f.pad_top + uishell_rt_px(t)/2);
      dr_rect(r2f32p(f.indent, y, width, y + 1.f), t->rule, 0, 0, 0);
    }
    if(f.code)
    {
      dr_rect(r2f32p(f.indent, f.pad_top, width, height - f.pad_bottom), t->code_block, 4.f, 0, 1.f);
    }
    if(e != 0)
    {
      UIShell_RtLayout *l = e->layout;
      Vec2F32 origin = v2f32(f.indent + f.inset_x, f.pad_top + f.inset_y);
      uishell_rt_draw_run_marks(t, &l->text, l->inks, l->style_count, origin, 1);
      complete &= uishell_rt_draw_glyphs(b, t, &l->text, l->inks, l->style_count, origin, 0);
      uishell_rt_draw_run_marks(t, &l->text, l->inks, l->style_count, origin, 0);
      if(l->label.line_count != 0 && l->text.line_count != 0)
      {
        Vec2F32 at = {0};
        if(f.label_above)
        {
          at = v2f32(origin.x, f.pad_top + floor_f32(uishell_rt_px(t)*0.35f));
        }
        else
        {
          F32 gap = floor_f32(uishell_rt_px(t)*0.45f);
          at = v2f32(origin.x - gap - l->label.width, origin.y + l->text.lines[0].baseline - l->label.lines[0].baseline);
        }
        complete &= uishell_rt_draw_glyphs(b, t, &l->label, 0, 0, at, &t->weak);
      }
    }
  }
  b->incomplete = !complete;
  s->stats.bucket_builds += 1;
  scratch_end(scratch);
}

internal UIShell_RtBucket *
uishell_rt_bucket(UIShell_RichTextState *s, U64 key)
{
  for(UIShell_RtBucket *b = s->buckets; b != 0; b = b->next) { if(b->key == key) { return b; } }
  UIShell_RtBucket *b = s->free_bucket;
  if(b) { s->free_bucket = b->next; }
  else { b = push_array(s->arena, UIShell_RtBucket, 1); }
  Arena *arena = b->arena;
  MemoryZeroStruct(b);
  b->arena = arena;
  b->key = key;
  SLLStackPush(s->buckets, b);
  return b;
}

// Buckets of Blocks not shown for a while go back to the free list.
internal void
uishell_rt_prune_buckets(UIShell_RichTextState *s)
{
  for(UIShell_RtBucket **at = &s->buckets; *at != 0;)
  {
    UIShell_RtBucket *b = *at;
    if(b->last_frame + 120 < rd_state->frame_index)
    {
      *at = b->next;
      if(b->arena) { arena_clear(b->arena); }
      b->bucket = 0;
      b->next = s->free_bucket;
      s->free_bucket = b;
    }
    else { at = &b->next; }
  }
}

// What a Block's box draws: its bucket, then this frame's overlays.
typedef struct UIShell_RtBlockDraw UIShell_RtBlockDraw;
struct UIShell_RtBlockDraw
{
  U64 key;
  U32 kind;
  DR_Bucket *bucket;
  // In the Block's coordinates.
  Rng2F32 *quads;
  Vec4F32 *quad_colors;
  U64 quad_count;
  // Of each kind, for the diagnostics.
  U64 selection_quads;
  U64 find_quads;
  U64 current_find_quads;
  B32 focused;
  Vec4F32 focus_color;
};

internal UI_BOX_CUSTOM_DRAW(uishell_rt_block_draw)
{
  UIShell_RtBlockDraw *d = (UIShell_RtBlockDraw *)user_data;
  Vec2F32 origin = v2f32(floor_f32(box->rect.x0), floor_f32(box->rect.y0));
  DR_XForm2DScope(mul_3x3f32(dr_top_xform2d(), make_translate_3x3f32(origin)))
  {
    if(d->bucket) { dr_sub_bucket(d->bucket); }
    for(U64 i = 0; i < d->quad_count; i += 1)
    {
      dr_rect(d->quads[i], d->quad_colors[i], 0, 0, 0);
    }
  }
  if(d->focused)
  {
    dr_rect(pad_2f32(box->rect, 1.f), d->focus_color, 3.f, 1.5f, 1.f);
  }
}

////////////////////////////////
//~ Where things are this frame

typedef struct UIShell_RtPlaced UIShell_RtPlaced;
struct UIShell_RtPlaced
{
  U64 key;
  U32 part;
  // In the content box's coordinates: y from the viewport's top.
  Rng2F32 rect;
  F64 top;
  F32 height;
  UIShell_RtNode *node;
  UIShell_RtLayoutEntry *layout;
  UIShell_RtFrame frame;
  B32 visible;
};

typedef struct UIShell_RtPlacement UIShell_RtPlacement;
struct UIShell_RtPlacement
{
  UIShell_RtPlaced *v;
  U64 count;
  F64 top;
  F64 total;
  F64 viewport;
  F32 content_x;
  F32 content_width;
};

internal Vec2F32
uishell_rt_text_origin(UIShell_RtPlaced *p)
{
  return v2f32(p->frame.indent + p->frame.inset_x, p->frame.pad_top + p->frame.inset_y);
}

internal U32
uishell_rt_offset_at(UIShell_RtPlaced *p, Vec2F32 local)
{
  if(p->node == 0) { return 0; }
  U32 len = (U32)p->node->text.size;
  if(p->layout == 0) { return local.y < p->height/2 ? 0 : len; }
  Vec2F32 o = uishell_rt_text_origin(p);
  WhTextPosition pos = {0};
  if(!wh_text_offset_at(&p->layout->layout->text, local.x - o.x, local.y - o.y, &pos)) { return 0; }
  return Min(pos.offset, len);
}

// The Block and byte under a point in the content box's coordinates; above
// or below every Block, the nearest end.
internal B32
uishell_rt_hit(UIShell_RtPlacement *pl, Vec2F32 p, U64 *key_out, U32 *byte_out)
{
  UIShell_RtPlaced *first = 0, *last = 0;
  for(U64 i = 0; i < pl->count; i += 1)
  {
    UIShell_RtPlaced *e = &pl->v[i];
    if(e->part != WHEELHOUSE_RICH_PART_BLOCK || e->node == 0) { continue; }
    if(first == 0) { first = e; }
    last = e;
    if(p.y >= e->rect.y0 && p.y < e->rect.y1)
    {
      *key_out = e->key;
      *byte_out = uishell_rt_offset_at(e, v2f32(p.x - e->rect.x0, p.y - e->rect.y0));
      return 1;
    }
  }
  if(first && p.y < first->rect.y0) { *key_out = first->key; *byte_out = 0; return 1; }
  if(last) { *key_out = last->key; *byte_out = (U32)last->node->text.size; return 1; }
  return 0;
}

internal UIShell_RtPlaced *
uishell_rt_placed(UIShell_RtPlacement *pl, U64 key)
{
  for(U64 i = 0; i < pl->count; i += 1) { if(pl->v[i].key == key && pl->v[i].part == WHEELHOUSE_RICH_PART_BLOCK) { return &pl->v[i]; } }
  return 0;
}

internal B32
uishell_rt_word_char(U8 c)
{
  return char_is_alpha(c) || char_is_digit(c, 10) || c == '_' || c >= 0x80;
}

internal Rng1U32
uishell_rt_word_at(String8 text, U32 byte)
{
  U32 start = Min(byte, (U32)text.size), end = start;
  if(start < text.size && !uishell_rt_word_char(text.str[start]))
  {
    // Not in a word: the one character.
    end = start + 1;
    while(end < text.size && (text.str[end] & 0xC0) == 0x80) { end += 1; }
    return rng_1u32(start, end);
  }
  while(start > 0 && uishell_rt_word_char(text.str[start-1])) { start -= 1; }
  while(end < text.size && uishell_rt_word_char(text.str[end])) { end += 1; }
  return rng_1u32(start, end);
}

// What of a Block the selection covers: [start, end) of its text, the
// whole of an Embed or Rule.
internal B32
uishell_rt_selected_range(UIShell_RichTextState *s, WheelhouseRichSelection *sel, UIShell_RtNode *n, U32 *start, U32 *end)
{
  UIShell_RtNode *a = uishell_rt_node(&s->model, sel->anchor_key), *f = uishell_rt_node(&s->model, sel->focus_key);
  if(a == 0 || f == 0) { return 0; }
  U64 lo_order = a->order, hi_order = f->order;
  U32 lo_byte = sel->anchor_byte, hi_byte = sel->focus_byte;
  if(lo_order > hi_order || (lo_order == hi_order && lo_byte > hi_byte))
  {
    Swap(U64, lo_order, hi_order);
    Swap(U32, lo_byte, hi_byte);
  }
  if(n->order < lo_order || n->order > hi_order) { return 0; }
  *start = n->order == lo_order ? lo_byte : 0;
  *end = n->order == hi_order ? hi_byte : (U32)n->text.size;
  if(n->kind == WHEELHOUSE_RICH_KIND_EMBED || n->kind == WHEELHOUSE_RICH_KIND_RULE)
  {
    // Atomic: selected when the range spans it.
    B32 spans = (n->order > lo_order && n->order < hi_order) || (lo_order != hi_order) || *end > *start;
    *start = 0;
    *end = spans ? Max((U32)n->text.size, 1) : 0;
  }
  return *end > *start;
}

////////////////////////////////
//~ Links (#561: links are actions)

// The link span over `byte`, if any: its target.
internal B32
uishell_rt_link_at(UIShell_RtNode *n, U32 byte, String8 *target_out, Rng1U32 *range_out)
{
  for(U64 i = 0; n && i < n->span_count; i += 1)
  {
    WheelhouseRichSpan *sp = &n->spans[i];
    if(sp->attr == WHEELHOUSE_RICH_ATTR_LINK && byte >= sp->start && byte < sp->start + sp->len && sp->value < n->string_count)
    {
      *target_out = n->strings[sp->value];
      if(range_out) { *range_out = rng_1u32(sp->start, sp->start + sp->len); }
      return 1;
    }
  }
  return 0;
}

internal String8
uishell_rt_slug(Arena *arena, String8 text)
{
  String8List parts = {0};
  for(U64 i = 0; i < text.size; i += 1)
  {
    U8 c = text.str[i];
    if(char_is_alpha(c) || char_is_digit(c, 10) || c >= 0x80 || c == '-' || c == '_') { str8_list_push(arena, &parts, push_str8_copy(arena, str8(&c, 1))); }
    else if(c == ' ') { str8_list_push(arena, &parts, str8_lit("-")); }
  }
  return lower_from_str8(arena, str8_list_join(arena, &parts, 0));
}

// %XX decoded; one that would decode to NUL, or isn't hex, stays as it is.
internal String8
uishell_rt_percent_decoded(Arena *arena, String8 s)
{
  U8 *out = push_array_no_zero(arena, U8, s.size + 1);
  U64 size = 0;
  for(U64 i = 0; i < s.size; i += 1)
  {
    U8 c = s.str[i];
    if(c == '%' && i + 2 < s.size && char_is_digit(s.str[i+1], 16) && char_is_digit(s.str[i+2], 16))
    {
      U8 decoded = (U8)u64_from_str8(str8(s.str + i + 1, 2), 16);
      if(decoded != 0) { c = decoded; i += 2; }
    }
    out[size++] = c;
  }
  return str8(out, size);
}

internal B32
uishell_rt_has_scheme(String8 target)
{
  U64 colon = str8_find_needle(target, 0, str8_lit(":"), 0);
  if(colon == 0 || colon >= target.size) { return 0; }
  // A drive letter is a path.
  if(colon == 1 && char_is_alpha(target.str[0])) { return 0; }
  for(U64 i = 0; i < colon; i += 1)
  {
    U8 c = target.str[i];
    if(!(char_is_alpha(c) || char_is_digit(c, 10) || c == '+' || c == '-' || c == '.')) { return 0; }
  }
  return 1;
}

// How a link resolves: a heading here, a page elsewhere, or a file beside
// this one (a wikilink names a note, so it tries `.md`).
typedef U32 UIShell_RtLinkKind;
enum
{
  UIShell_RtLink_None,
  UIShell_RtLink_Heading,
  UIShell_RtLink_External,
  UIShell_RtLink_File,
};

internal UIShell_RtLinkKind
uishell_rt_resolve_link(Arena *arena, UIShell_RichTextState *s, String8 target, String8 *resolved_out, U64 *heading_out)
{
  target = str8_skip_chop_whitespace(target);
  *resolved_out = target;
  if(target.size == 0) { return UIShell_RtLink_None; }
  if(target.str[0] == '#')
  {
    // GitHub's anchors: headings in document order, the nth repeat of a
    // slug taking a "-n" suffix.
    String8 wanted = lower_from_str8(arena, uishell_rt_percent_decoded(arena, str8_skip(target, 1)));
    U64 count = 0;
    String8 *seen = push_array(arena, String8, Max(s->model.block_count, 1));
    for(U64 i = 0; i < s->model.block_count; i += 1)
    {
      UIShell_RtNode *n = uishell_rt_node(&s->model, s->model.block_keys[i]);
      if(n == 0 || n->kind != WHEELHOUSE_RICH_KIND_HEADING) { continue; }
      String8 slug = uishell_rt_slug(arena, n->text);
      U64 repeats = 0;
      for(U64 j = 0; j < count; j += 1) { repeats += str8_match(seen[j], slug, 0); }
      seen[count++] = slug;
      String8 anchor = repeats ? push_str8f(arena, "%S-%llu", slug, (unsigned long long)repeats) : slug;
      if(str8_match(anchor, wanted, 0))
      {
        *heading_out = n->key;
        return UIShell_RtLink_Heading;
      }
    }
    return UIShell_RtLink_None;
  }
  if(uishell_rt_has_scheme(target))
  {
    if(str8_match(str8_prefix(target, 5), str8_lit("file:"), StringMatchFlag_CaseInsensitive))
    {
      String8 path = wh_path_from_file_url(arena, target);
      if(path.size) { *resolved_out = path; return UIShell_RtLink_File; }
      return UIShell_RtLink_None;
    }
    // A document can come from anywhere: only the web and mail leave the
    // app, never another registered protocol (javascript:, smb:, ...).
    String8 external[] = {str8_lit("http:"), str8_lit("https:"), str8_lit("mailto:")};
    for EachElement(i, external)
    {
      if(str8_match(str8_prefix(target, external[i].size), external[i], StringMatchFlag_CaseInsensitive)) { return UIShell_RtLink_External; }
    }
    return UIShell_RtLink_None;
  }
  // A path, percent-encoded as markdown links are, without its query or
  // fragment.
  U64 end = Min(str8_find_needle(target, 0, str8_lit("#"), 0), str8_find_needle(target, 0, str8_lit("?"), 0));
  String8 relative = uishell_rt_percent_decoded(arena, str8_prefix(target, end));
  if(relative.size == 0) { return UIShell_RtLink_None; }
  String8 dir = str8_chop_last_slash(str8(s->path, s->path_size));
  String8 path = path_absolute_dst_from_relative_dst_src(arena, relative, dir);
  if(!file_path_exists(path))
  {
    String8 note = push_str8f(arena, "%S.md", path);
    if(file_path_exists(note)) { path = note; }
  }
  *resolved_out = path;
  return UIShell_RtLink_File;
}

internal void
uishell_rt_follow_link(UIShell_RichTextState *s, String8 target)
{
  Temp scratch = scratch_begin(0, 0);
  String8 resolved = str8_zero();
  U64 heading = 0;
  switch(uishell_rt_resolve_link(scratch.arena, s, target, &resolved, &heading))
  {
    default: break;
    case UIShell_RtLink_Heading:
    {
      char *error = 0;
      wheelhouse_rich_text_reveal(s->doc, s->sub, heading, 0, &error);
      uishell_rt_error(&error);
    } break;
    case UIShell_RtLink_External: { wm_open_in_browser(resolved); } break;
    // A new View, which resolves through the Handlers as any opened file.
    case UIShell_RtLink_File: { uishell_cmd("open", .file_path = push_str8_copy(rd_frame_arena(), resolved)); } break;
  }
  scratch_end(scratch);
}

////////////////////////////////
//~ Copying (#546; #561's formats)

internal String8
uishell_rt_copy(Arena *arena, UIShell_RichTextState *s, U32 format)
{
  WheelhouseBytes bytes = {0};
  char *error = 0;
  String8 result = str8_zero();
  if(s->doc && wheelhouse_rich_text_copy(s->doc, s->sub, format, &bytes, &error))
  {
    result = push_str8_copy(arena, str8(bytes.data, bytes.len));
    wheelhouse_bytes_free(bytes);
  }
  uishell_rt_error(&error);
  return result;
}

internal String8
uishell_rt_copy_block(Arena *arena, UIShell_RichTextState *s, U64 key, U32 format)
{
  WheelhouseBytes bytes = {0};
  char *error = 0;
  String8 result = str8_zero();
  if(s->doc && wheelhouse_rich_text_copy_block(s->doc, key, format, &bytes, &error))
  {
    result = push_str8_copy(arena, str8(bytes.data, bytes.len));
    wheelhouse_bytes_free(bytes);
  }
  uishell_rt_error(&error);
  return result;
}

// Copy: plain text and HTML for the clipboard. A focused table Embed copies
// whole, as TSV.
internal void
uishell_rt_copy_to_clipboard(UIShell_RichTextState *s)
{
  Temp scratch = scratch_begin(0, 0);
  if(s->focus_embed != 0)
  {
    String8 plain = uishell_rt_copy_block(scratch.arena, s, s->focus_embed, WHEELHOUSE_RICH_COPY_PLAIN);
    String8 html = uishell_rt_copy_block(scratch.arena, s, s->focus_embed, WHEELHOUSE_RICH_COPY_HTML);
    wm_set_clipboard_rich_text(plain, html);
  }
  else
  {
    String8 plain = uishell_rt_copy(scratch.arena, s, WHEELHOUSE_RICH_COPY_PLAIN);
    String8 html = uishell_rt_copy(scratch.arena, s, WHEELHOUSE_RICH_COPY_HTML);
    if(plain.size) { wm_set_clipboard_rich_text(plain, html); }
  }
  scratch_end(scratch);
}

// On Linux a selection is also PRIMARY's, as X11 applications expect.
internal void
uishell_rt_selection_to_primary(UIShell_RichTextState *s)
{
#if OS_LINUX
  Temp scratch = scratch_begin(0, 0);
  String8 plain = uishell_rt_copy(scratch.arena, s, WHEELHOUSE_RICH_COPY_PLAIN);
  if(plain.size) { wm_set_selection_text(plain); }
  scratch_end(scratch);
#else
  (void)s;
#endif
}

// The edit commands reach a Rich Text View as they reach a terminal: by
// callback, before the next build (uishell_dispatch_ui_event_command).
internal B32
uishell_rt_edit_enabled(void *user, String8 command)
{
  UIShell_RichTextState *s = (UIShell_RichTextState *)user;
  WheelhouseRichSelection sel = {0};
  char *error = 0;
  B32 selected = s->doc && wheelhouse_rich_text_selection(s->doc, s->sub, &sel, &error);
  uishell_rt_error(&error);
  if(str8_match(command, str8_lit("copy"), 0)) { return selected || s->focus_embed != 0; }
  if(str8_match(command, str8_lit("select_all"), 0) || str8_match(command, str8_lit("clear_selection"), 0)) { return s->doc != 0; }
  return 0;
}

internal void
uishell_rt_edit_dispatch(void *user, String8 command, String8 paste)
{
  (void)paste;
  UIShell_RichTextState *s = (UIShell_RichTextState *)user;
  if(s->doc == 0) { return; }
  char *error = 0;
  if(str8_match(command, str8_lit("copy"), 0)) { uishell_rt_copy_to_clipboard(s); }
  else if(str8_match(command, str8_lit("select_all"), 0))
  {
    s->focus_embed = 0;
    wheelhouse_rich_text_select_all(s->doc, s->sub, &error);
    uishell_rt_selection_to_primary(s);
  }
  else if(str8_match(command, str8_lit("clear_selection"), 0)) { wheelhouse_rich_text_clear_selection(s->doc, s->sub, &error); }
  uishell_rt_error(&error);
  rd_request_frame();
}

internal void
uishell_rt_register_edit_owner(UI_Key key, UIShell_RichTextState *s, RD_ViewAddress view)
{
  ui_state->edit_owner_key = key;
  ui_state->edit_owner_build = ui_state->build_index;
  ui_state->edit_owner_selection = 0;
  ui_state->edit_owner_terminal = 1;
  ui_state->edit_owner_user = s;
  ui_state->edit_owner_enabled = uishell_rt_edit_enabled;
  ui_state->edit_owner_dispatch = uishell_rt_edit_dispatch;
  RD_WindowState *ws = rd_window_state_from_cfg__existing(cfg_node_from_id(uishell_regs()->window));
  if(ws != &rd_nil_window_state) { ws->edit_owner_view = view; }
}

////////////////////////////////
//~ Find (#561: the query bar's search, find_next and find_prev)

internal void
uishell_rt_find_reset(UIShell_RichTextState *s)
{
  s->match_count = 0;
  s->match_current = WHEELHOUSE_NONE;
}

internal void
uishell_rt_find_start(UIShell_RichTextState *s, String8 query)
{
  char *error = 0;
  query = str8_prefix(query, sizeof(s->find_query));
  if(s->find_active && str8_match(query, str8(s->find_query, s->find_query_size), 0)) { return; }
  uishell_rt_find_reset(s);
  MemoryCopy(s->find_query, query.str, query.size);
  s->find_query_size = query.size;
  if(query.size == 0)
  {
    if(s->find_active) { wheelhouse_rich_text_find_end(s->doc, s->sub, &error); }
    s->find_active = 0;
  }
  else
  {
    s->find_active = wheelhouse_rich_text_find(s->doc, s->sub, uishell_rt_wt(query), 0, &error);
  }
  uishell_rt_error(&error);
}

// Scans on, and copies the matches found since the last step. Returns
// whether the scan is done.
internal B32
uishell_rt_find_step(UIShell_RichTextState *s, U64 budget)
{
  if(!s->find_active) { return 1; }
  char *error = 0;
  WheelhouseRichFindStatus status = {0};
  if(!wheelhouse_rich_text_find_step(s->doc, s->sub, budget, &status, &error))
  {
    uishell_rt_error(&error);
    s->find_active = 0;
    return 1;
  }
  // A reload restarts the scan: the matches so far go.
  if(status.count < s->match_count) { s->match_count = 0; }
  if(status.count > s->match_cap)
  {
    U64 cap = Max(256, s->match_cap*2);
    while(cap < status.count) { cap *= 2; }
    Arena *arena = arena_alloc();
    WheelhouseRichMatch *matches = push_array_no_zero(arena, WheelhouseRichMatch, cap);
    MemoryCopy(matches, s->matches, sizeof(*matches)*s->match_count);
    if(s->match_arena) { arena_release(s->match_arena); }
    s->match_arena = arena;
    s->matches = matches;
    s->match_cap = cap;
  }
  for(U64 i = s->match_count; i < status.count; i += 1)
  {
    if(!wheelhouse_rich_text_find_match(s->doc, s->sub, i, &s->matches[i], &error)) { uishell_rt_error(&error); break; }
    s->match_count = i+1;
  }
  s->match_current = status.current;
  return status.done;
}

internal void
uishell_rt_find_move(UIShell_RichTextState *s, B32 forward)
{
  if(!s->find_active) { return; }
  char *error = 0;
  WheelhouseRichMatch match = {0};
  if(wheelhouse_rich_text_find_next(s->doc, s->sub, forward, &match, &error))
  {
    s->find_pending_step = 0;
    s->caret_key = match.key;
    s->caret_byte = match.end;
    s->has_caret = 1;
  }
  else
  {
    // None yet: move once the scan finds one.
    s->find_pending_step = 1;
    s->find_forward = forward;
  }
  uishell_rt_error(&error);
  uishell_rt_find_step(s, 0);
}

////////////////////////////////
//~ Placing the Window's Blocks (#541; #544, decision 8)

internal F32
uishell_rt_line_top_for_byte(UIShell_RtPlaced *p, U32 byte, U32 *line_start_out)
{
  WhTextLayout *l = &p->layout->layout->text;
  U32 line_index = 0;
  for(U64 i = 0; i < l->line_count; i += 1)
  {
    if(l->lines[i].text_start <= byte) { line_index = (U32)i; }
  }
  if(line_start_out) { *line_start_out = l->line_count ? l->lines[line_index].text_start : 0; }
  return uishell_rt_text_origin(p).y + (l->line_count ? l->lines[line_index].y : 0);
}

internal F32
uishell_rt_line_top_at_depth(UIShell_RtPlaced *p, F32 depth, U32 *line_start_out)
{
  WhTextLayout *l = &p->layout->layout->text;
  F32 origin = uishell_rt_text_origin(p).y;
  U32 line_index = 0;
  for(U64 i = 0; i < l->line_count; i += 1)
  {
    if(origin + l->lines[i].y <= depth) { line_index = (U32)i; }
  }
  *line_start_out = l->line_count ? l->lines[line_index].text_start : 0;
  return origin + (l->line_count ? l->lines[line_index].y : 0);
}

// Measures what the Window holds until the core's heights agree with the
// layouts, keeps the Scroll Anchor on its line, and places the Blocks.
internal UIShell_RtPlacement
uishell_rt_place(Arena *arena, UIShell_RichTextState *s, F32 content_x, F32 content_width)
{
  UIShell_RtPlacement pl = {0};
  pl.content_x = content_x;
  pl.content_width = content_width;
  char *error = 0;
  U64 layouts_before = s->layouts.layouts, layout_us_before = s->layouts.layout_us;
  WheelhouseRichWindowEntry *entries = 0;
  U64 entry_count = 0;
  WheelhouseRichWindow w = {0};
  for(U32 pass = 0; pass < 8; pass += 1)
  {
    if(!wheelhouse_rich_text_window(s->doc, s->sub, &w, &error)) { uishell_rt_error(&error); return pl; }
    // Borrowed until the next call on the handle: set_height is one.
    entry_count = w.entry_count;
    entries = push_array_no_zero(arena, WheelhouseRichWindowEntry, Max(entry_count, 1));
    MemoryCopy(entries, w.entries, sizeof(*entries)*entry_count);
    B32 changed = 0;
    for(U64 i = 0; i < entry_count; i += 1)
    {
      WheelhouseRichWindowEntry *e = &entries[i];
      F32 height = 0;
      if(e->part == WHEELHOUSE_RICH_PART_BLOCK)
      {
        UIShell_RtNode *n = uishell_rt_node(&s->model, e->key);
        if(n == 0) { continue; }
        height = uishell_rt_block_height(s, n, content_width, 0);
      }
      if(!e->measured || abs_f32(height - e->height) > 0.25f)
      {
        changed |= wheelhouse_rich_text_set_height(s->doc, s->sub, e->key, e->part, height, content_width, &error);
        uishell_rt_error(&error);
      }
    }
    if(!changed) { break; }
  }
  pl.top = w.top;
  pl.total = w.total;
  pl.viewport = w.viewport;
  pl.v = push_array(arena, UIShell_RtPlaced, Max(entry_count, 1));
  for(U64 i = 0; i < entry_count; i += 1)
  {
    WheelhouseRichWindowEntry *e = &entries[i];
    UIShell_RtPlaced *p = &pl.v[pl.count++];
    p->key = e->key;
    p->part = e->part;
    p->top = e->top;
    p->height = e->height;
    F32 y = (F32)(e->top - w.top);
    p->rect = r2f32p(content_x, y, content_x + content_width, y + e->height);
    p->visible = p->rect.y1 > 0 && p->rect.y0 < (F32)w.viewport;
    if(e->part != WHEELHOUSE_RICH_PART_BLOCK) { continue; }
    p->node = uishell_rt_node(&s->model, e->key);
    if(p->node == 0) { continue; }
    p->frame = uishell_rt_frame(arena, &s->model, &s->theme, p->node);
    uishell_rt_block_height(s, p->node, content_width, &p->layout);
  }
  // The anchor holds its line: after the anchored Block is laid out anew,
  // the line its byte starts goes where the anchor says; otherwise the
  // core learns which line the viewport's top falls in.
  WheelhouseRichAnchor anchor = {0};
  if(wheelhouse_rich_text_anchor(s->doc, s->sub, &anchor, &error) && anchor.position == WHEELHOUSE_RICH_POSITION_TEXT)
  {
    UIShell_RtPlaced *p = uishell_rt_placed(&pl, anchor.key);
    if(p && p->layout && p->layout->layout->text.line_count)
    {
      B32 same = s->has_last_anchor && s->last_anchor.key == anchor.key && s->last_anchor.byte == anchor.byte;
      if(same && s->anchor_layout_stamp != p->layout->stamp)
      {
        wheelhouse_rich_text_relayout_anchor(s->doc, s->sub, uishell_rt_line_top_for_byte(p, anchor.byte, 0), &error);
      }
      else
      {
        F32 depth = (F32)(pl.top + anchor.viewport_offset - p->top);
        U32 line_start = 0;
        F32 line_top = uishell_rt_line_top_at_depth(p, depth, &line_start);
        F32 core_line_top = depth - anchor.line_offset;
        if(line_start != anchor.byte || abs_f32(core_line_top - line_top) > 0.25f)
        {
          wheelhouse_rich_text_set_anchor_text(s->doc, s->sub, line_start, line_top, &error);
        }
      }
      uishell_rt_error(&error);
      wheelhouse_rich_text_anchor(s->doc, s->sub, &anchor, &error);
      s->anchor_layout_stamp = p->layout->stamp;
    }
  }
  uishell_rt_error(&error);
  s->last_anchor = anchor;
  s->has_last_anchor = anchor.key != 0;
  s->stats.frame_layouts += s->layouts.layouts - layouts_before;
  s->stats.frame_layout_us += s->layouts.layout_us - layout_us_before;
  return pl;
}

////////////////////////////////
//~ Tab stops

internal U64
uishell_rt_stops(Arena *arena, UIShell_RichTextState *s, UIShell_RtPlacement *pl, UIShell_RtStop **out)
{
  U64 count = 0, cap = 64;
  UIShell_RtStop *stops = push_array(arena, UIShell_RtStop, cap);
  for(U64 i = 0; i < pl->count; i += 1)
  {
    UIShell_RtPlaced *p = &pl->v[i];
    if(p->node == 0) { continue; }
    UIShell_RtStop more[64];
    U64 more_count = 0;
    if(p->frame.table)
    {
      more[more_count++] = (UIShell_RtStop){UIShell_RtStop_Embed, p->key, 0, 0};
      UIShell_RtTable *t = uishell_rt_table_of(s, p->key);
      B32 capped = 0;
      uishell_rt_table_height(&s->theme, p->node, t && t->expanded, s->viewport, &capped);
      if(capped || (t && t->expanded)) { more[more_count++] = (UIShell_RtStop){UIShell_RtStop_Expand, p->key, 1, 0}; }
    }
    if(p->frame.code) { more[more_count++] = (UIShell_RtStop){UIShell_RtStop_CopyButton, p->key, 0, 0}; }
    for(U64 sp = 0; sp < p->node->span_count && more_count < ArrayCount(more); sp += 1)
    {
      WheelhouseRichSpan *span = &p->node->spans[sp];
      if(span->attr == WHEELHOUSE_RICH_ATTR_LINK) { more[more_count++] = (UIShell_RtStop){UIShell_RtStop_Link, p->key, span->start, span->start + span->len}; }
    }
    for(U64 m = 0; m < more_count; m += 1)
    {
      if(count == cap)
      {
        UIShell_RtStop *bigger = push_array(arena, UIShell_RtStop, cap*2);
        MemoryCopy(bigger, stops, sizeof(*stops)*count);
        stops = bigger;
        cap *= 2;
      }
      stops[count++] = more[m];
    }
  }
  *out = stops;
  return count;
}

internal B32
uishell_rt_stop_match(UIShell_RtStop *a, UIShell_RtStop *b)
{
  return a->kind == b->kind && a->key == b->key && a->start == b->start;
}

internal void
uishell_rt_activate_stop(UIShell_RichTextState *s, UIShell_RtStop *stop)
{
  switch(stop->kind)
  {
    case UIShell_RtStop_Link:
    {
      String8 target = str8_zero();
      if(uishell_rt_link_at(uishell_rt_node(&s->model, stop->key), stop->start, &target, 0)) { uishell_rt_follow_link(s, target); }
    } break;
    case UIShell_RtStop_Embed: { s->focus_embed = stop->key; } break;
    case UIShell_RtStop_Expand:
    {
      UIShell_RtTable *t = uishell_rt_table_of(s, stop->key);
      if(t) { t->expanded = !t->expanded; }
    } break;
    case UIShell_RtStop_CopyButton:
    {
      Temp scratch = scratch_begin(0, 0);
      wm_set_clipboard_text(uishell_rt_copy_block(scratch.arena, s, stop->key, WHEELHOUSE_RICH_COPY_PLAIN));
      s->copied_key = stop->key;
      s->copied_until_us = now_time_us() + 1500000;
      scratch_end(scratch);
    } break;
  }
}

////////////////////////////////
//~ Pointer and keys

// A code Block's copy button, and a capped table's expand toggle, in a
// Block's coordinates.
internal Rng2F32
uishell_rt_copy_button_rect(UIShell_RtPlaced *p, UIShell_RtTheme *t)
{
  F32 px = uishell_rt_px(t);
  F32 w = floor_f32(px*4.f), h = floor_f32(px*1.5f);
  F32 x1 = dim_2f32(p->rect).x - floor_f32(px*0.3f), y0 = p->frame.pad_top + floor_f32(px*0.3f);
  return r2f32p(x1 - w, y0, x1, y0 + h);
}

internal Rng2F32
uishell_rt_table_rect(UIShell_RtPlaced *p, F32 table_height)
{
  return r2f32p(p->frame.indent, p->frame.pad_top, dim_2f32(p->rect).x, p->frame.pad_top + table_height);
}

internal Rng2F32
uishell_rt_expand_rect(UIShell_RtPlaced *p, UIShell_RtTheme *t, F32 table_height)
{
  F32 y0 = p->frame.pad_top + table_height;
  return r2f32p(p->frame.indent, y0, p->frame.indent + floor_f32(uishell_rt_px(t)*6.f), y0 + uishell_rt_line_px(t));
}

internal U32
uishell_rt_table_mods(WM_Modifiers m)
{
  return ((m & WM_Modifier_Shift) ? BINNACLE_MOD_SHIFT : 0) | ((m & WM_Modifier_Ctrl) ? BINNACLE_MOD_CTRL : 0) |
         ((m & WM_Modifier_Alt) ? BINNACLE_MOD_ALT : 0) | ((m & WM_Modifier_Super) ? BINNACLE_MOD_META : 0);
}

internal void
uishell_rt_select_point(UIShell_RichTextState *s, U64 key, U32 byte, B32 extend)
{
  char *error = 0;
  UIShell_RtNode *n = uishell_rt_node(&s->model, key);
  if(n == 0) { return; }
  if(s->select_unit == 2 && n)
  {
    Rng1U32 word = uishell_rt_word_at(n->text, byte);
    // Extending by words keeps the word pressed whole.
    UIShell_RtNode *press = uishell_rt_node(&s->model, s->press_key);
    B32 after = press && (n->order > press->order || (n->order == press->order && byte >= s->word_start));
    if(extend) { wheelhouse_rich_text_select(s->doc, s->sub, s->press_key, after ? s->word_start : s->word_end, key, after ? word.max : word.min, &error); }
    else
    {
      s->word_start = word.min;
      s->word_end = word.max;
      wheelhouse_rich_text_select(s->doc, s->sub, key, word.min, key, word.max, &error);
    }
  }
  else if(s->select_unit == 3)
  {
    UIShell_RtNode *press = uishell_rt_node(&s->model, s->press_key);
    if(!extend || press == 0) { wheelhouse_rich_text_select_block(s->doc, s->sub, key, &error); }
    else
    {
      B32 after = n->order >= press->order;
      wheelhouse_rich_text_select(s->doc, s->sub, s->press_key, after ? 0 : (U32)press->text.size, key, after ? (U32)n->text.size : 0, &error);
    }
  }
  else if(extend) { wheelhouse_rich_text_extend(s->doc, s->sub, key, byte, &error); }
  else { wheelhouse_rich_text_select(s->doc, s->sub, key, byte, key, byte, &error); }
  uishell_rt_error(&error);
  s->caret_key = key;
  s->caret_byte = byte;
  s->has_caret = 1;
}

// A press, drag or release, a wheel or a click on a control, against what
// was drawn last frame (`content` is where the content box was).
internal void
uishell_rt_pointer(UIShell_RichTextState *s, UIShell_RtPlacement *pl, Rng2F32 content, B32 *focus_out)
{
  char *error = 0;
  UIShell_RtTheme *t = &s->theme;
  F32 line = uishell_rt_line_px(t);
  Vec2F32 mouse = ui_mouse();
  for(UI_Event *evt = 0; ui_next_event(&evt);)
  {
    B32 inside = contains_2f32(content, evt->pos);
    Vec2F32 local = sub_2f32(evt->pos, content.p0);
    UIShell_RtPlaced *under = 0;
    for(U64 i = 0; i < pl->count && under == 0; i += 1)
    {
      if(pl->v[i].node && pl->v[i].part == WHEELHOUSE_RICH_PART_BLOCK && contains_2f32(pl->v[i].rect, local)) { under = &pl->v[i]; }
    }
    Vec2F32 in_block = under ? sub_2f32(local, under->rect.p0) : v2f32(0, 0);
    UIShell_RtTable *table = 0;
    F32 table_height = 0;
    B32 capped = 0;
    if(under && under->frame.table)
    {
      table = uishell_rt_table_of(s, under->key);
      table_height = uishell_rt_table_height(t, under->node, table && table->expanded, s->viewport, &capped);
    }
    B32 in_table = table && contains_2f32(uishell_rt_table_rect(under, table_height), in_block);
    if(evt->kind == UI_EventKind_Scroll && inside)
    {
      Vec2F32 delta = evt->delta_2f32;
      if(evt->modifiers & WM_Modifier_Shift) { Swap(F32, delta.x, delta.y); }
      Vec2F32 px_delta = delta;
      if(!evt->scroll_is_precise)
      {
        px_delta.x = (delta.x == 0 ? 0 : (delta.x > 0 ? 1 : -1)*Max(1.f, abs_f32(delta.x)/30.f))*line*3;
        px_delta.y = (delta.y == 0 ? 0 : (delta.y > 0 ? 1 : -1)*Max(1.f, abs_f32(delta.y)/30.f))*line*3;
      }
      // A table takes the wheel only while focused; sideways, always
      // (#542, decision 4).
      B32 to_table = in_table && table->table && (s->focus_embed == under->key || (px_delta.x != 0 && px_delta.y == 0));
      if(to_table)
      {
        BinnacleAction a = {0};
        a.kind = BINNACLE_ACTION_SCROLL;
        a.args.scroll = (BinnacleScroll){px_delta.x, s->focus_embed == under->key ? px_delta.y : 0};
        uishell_table_act(wheelhouse_table_grid(table->table), a);
      }
      else if(px_delta.y != 0)
      {
        wheelhouse_rich_text_scroll_by(s->doc, s->sub, px_delta.y, &error);
      }
      ui_eat_event(evt);
      continue;
    }
    if(evt->key != WM_Key_LeftMouseButton) { continue; }
    if(evt->kind == UI_EventKind_Press && inside && !ui_any_ctx_menu_is_open())
    {
      *focus_out = 1;
      U64 now = evt->timestamp_us;
      B32 repeat = (now - s->last_press_us <= (U64)(1000000*wm_get_system_info()->double_click_time) &&
                    abs_f32(evt->pos.x - s->last_press_pos.x) < 4 && abs_f32(evt->pos.y - s->last_press_pos.y) < 4);
      s->press_count = repeat ? Min(s->press_count + 1, 3) : 1;
      s->last_press_us = now;
      s->last_press_pos = evt->pos;
      s->drag_moved = 0;
      s->press_link = 0;
      ui_eat_event(evt);
      if(under && under->frame.code && contains_2f32(uishell_rt_copy_button_rect(under, t), in_block))
      {
        UIShell_RtStop stop = {UIShell_RtStop_CopyButton, under->key, 0, 0};
        uishell_rt_activate_stop(s, &stop);
        continue;
      }
      if(table && (capped || table->expanded) && contains_2f32(uishell_rt_expand_rect(under, t, table_height), in_block))
      {
        table->expanded = !table->expanded;
        continue;
      }
      if(in_table)
      {
        // The click that focuses is the table's too (#542, decision 5).
        s->focus_embed = under->key;
        s->focus_stop = -1;
        s->table_capture = under->key;
        Vec2F32 at = sub_2f32(in_block, uishell_rt_table_rect(under, table_height).p0);
        BinnaclePointerEvent e = {BINNACLE_POINTER_DOWN, at.x, at.y, BINNACLE_BUTTON_PRIMARY, uishell_rt_table_mods(evt->modifiers), repeat ? 2u : 1u};
        if(table->table) { binnacle_grid_pointer(wheelhouse_table_grid(table->table), e); }
        continue;
      }
      s->focus_embed = 0;
      s->focus_stop = -1;
      U64 key = 0;
      U32 byte = 0;
      if(!uishell_rt_hit(pl, local, &key, &byte)) { continue; }
      B32 extend = !!(evt->modifiers & WM_Modifier_Shift);
      if(!extend) { s->select_unit = s->press_count; s->press_key = key; s->press_byte = byte; }
      String8 target = str8_zero();
      if(!extend && s->press_count == 1 && uishell_rt_link_at(uishell_rt_node(&s->model, key), byte, &target, 0)) { s->press_link = 1; }
      uishell_rt_select_point(s, key, byte, extend);
      s->selecting = 1;
      continue;
    }
    if(evt->kind == UI_EventKind_Release && s->table_capture)
    {
      UIShell_RtTable *captured = uishell_rt_table_of(s, s->table_capture);
      UIShell_RtPlaced *p = uishell_rt_placed(pl, s->table_capture);
      if(captured && captured->table && p)
      {
        B32 c = 0;
        F32 h = uishell_rt_table_height(t, p->node, captured->expanded, s->viewport, &c);
        Vec2F32 at = sub_2f32(sub_2f32(local, p->rect.p0), uishell_rt_table_rect(p, h).p0);
        BinnaclePointerEvent e = {BINNACLE_POINTER_UP, at.x, at.y, BINNACLE_BUTTON_PRIMARY, uishell_rt_table_mods(evt->modifiers), 0};
        binnacle_grid_pointer(wheelhouse_table_grid(captured->table), e);
      }
      s->table_capture = 0;
      ui_eat_event(evt);
      continue;
    }
    if(evt->kind == UI_EventKind_Release && s->selecting)
    {
      s->selecting = 0;
      ui_eat_event(evt);
      U64 key = 0;
      U32 byte = 0;
      if(s->press_link && !s->drag_moved && uishell_rt_hit(pl, local, &key, &byte) && key == s->press_key)
      {
        String8 target = str8_zero();
        if(uishell_rt_link_at(uishell_rt_node(&s->model, key), byte, &target, 0))
        {
          wheelhouse_rich_text_clear_selection(s->doc, s->sub, &error);
          uishell_rt_follow_link(s, target);
        }
      }
      else
      {
        uishell_rt_selection_to_primary(s);
      }
    }
  }
  // A drag extends the selection, and scrolls past the content's edges.
  if(s->selecting)
  {
    Vec2F32 local = sub_2f32(mouse, content.p0);
    if(abs_f32(mouse.x - s->last_press_pos.x) >= 3 || abs_f32(mouse.y - s->last_press_pos.y) >= 3) { s->drag_moved = 1; }
    if(s->drag_moved)
    {
      F32 over = local.y < 0 ? local.y : local.y > dim_2f32(content).y ? local.y - dim_2f32(content).y : 0;
      if(over != 0)
      {
        wheelhouse_rich_text_scroll_by(s->doc, s->sub, Clamp(-line*2, over/4, line*2), &error);
      }
      U64 key = 0;
      U32 byte = 0;
      if(uishell_rt_hit(pl, v2f32(local.x, Clamp(0, local.y, dim_2f32(content).y)), &key, &byte)) { uishell_rt_select_point(s, key, byte, 1); }
      rd_request_frame();
    }
  }
  if(s->table_capture)
  {
    UIShell_RtTable *captured = uishell_rt_table_of(s, s->table_capture);
    UIShell_RtPlaced *p = uishell_rt_placed(pl, s->table_capture);
    if(captured && captured->table && p)
    {
      B32 c = 0;
      F32 h = uishell_rt_table_height(t, p->node, captured->expanded, s->viewport, &c);
      Vec2F32 at = sub_2f32(sub_2f32(sub_2f32(mouse, content.p0), p->rect.p0), uishell_rt_table_rect(p, h).p0);
      if(at.x != captured->last_pointer.x || at.y != captured->last_pointer.y)
      {
        BinnaclePointerEvent e = {BINNACLE_POINTER_MOVE, at.x, at.y, BINNACLE_BUTTON_PRIMARY, uishell_rt_table_mods(wm_get_modifiers()), 0};
        binnacle_grid_pointer(wheelhouse_table_grid(captured->table), e);
        captured->last_pointer = at;
      }
    }
  }
  uishell_rt_error(&error);
}

// The selection's focus moved by a key, within the placed Blocks: a
// character, a word, a line, or to the document's ends.
internal B32
uishell_rt_moved_point(UIShell_RichTextState *s, UIShell_RtPlacement *pl, UI_Event *evt, U64 *key_io, U32 *byte_io)
{
  UIShell_RtNode *n = uishell_rt_node(&s->model, *key_io);
  if(n == 0) { return 0; }
  String8 text = n->text;
  U32 byte = Min(*byte_io, (U32)text.size);
  S32 dx = evt->delta_2s32.x, dy = evt->delta_2s32.y;
  UIShell_RtModel *m = &s->model;
  if(evt->delta_unit == UI_EventDeltaUnit_Whole)
  {
    if(m->block_count == 0) { return 0; }
    B32 end = dx > 0 || dy > 0;
    UIShell_RtNode *to = uishell_rt_node(m, m->block_keys[end ? m->block_count-1 : 0]);
    if(to == 0) { return 0; }
    *key_io = to->key;
    *byte_io = end ? (U32)to->text.size : 0;
    return 1;
  }
  if(dx != 0 && (evt->delta_unit == UI_EventDeltaUnit_Char || evt->delta_unit == UI_EventDeltaUnit_Word))
  {
    if((dx < 0 && byte == 0) || (dx > 0 && byte >= text.size))
    {
      // Off the Block's end: the neighbouring Block's near end.
      S64 order = (S64)n->order + (dx > 0 ? 1 : -1);
      if(order < 0 || order >= (S64)m->block_count) { return 0; }
      UIShell_RtNode *to = uishell_rt_node(m, m->block_keys[order]);
      if(to == 0) { return 0; }
      *key_io = to->key;
      *byte_io = dx > 0 ? 0 : (U32)to->text.size;
      return 1;
    }
    if(evt->delta_unit == UI_EventDeltaUnit_Word)
    {
      if(dx > 0) { while(byte < text.size && !uishell_rt_word_char(text.str[byte])) { byte += 1; } while(byte < text.size && uishell_rt_word_char(text.str[byte])) { byte += 1; } }
      else { while(byte > 0 && !uishell_rt_word_char(text.str[byte-1])) { byte -= 1; } while(byte > 0 && uishell_rt_word_char(text.str[byte-1])) { byte -= 1; } }
    }
    else if(dx > 0) { byte += 1; while(byte < text.size && (text.str[byte] & 0xC0) == 0x80) { byte += 1; } }
    else { byte -= 1; while(byte > 0 && (text.str[byte] & 0xC0) == 0x80) { byte -= 1; } }
    *byte_io = byte;
    return 1;
  }
  UIShell_RtPlaced *p = uishell_rt_placed(pl, n->key);
  if(p == 0 || p->layout == 0) { return 0; }
  WhTextLayout *l = &p->layout->layout->text;
  WhTextCaret caret = {0};
  if(!wh_text_caret_rect(l, byte, WH_TEXT_DOWNSTREAM, &caret)) { return 0; }
  if(evt->delta_unit == UI_EventDeltaUnit_Page && dy != 0)
  {
    // A viewport's height on, from where the caret is.
    Vec2F32 at = v2f32(p->rect.x0 + uishell_rt_text_origin(p).x + caret.x,
                       p->rect.y0 + uishell_rt_text_origin(p).y + caret.y + caret.height/2 + (dy > 0 ? 1 : -1)*(F32)pl->viewport);
    return uishell_rt_hit(pl, at, key_io, byte_io);
  }
  if(evt->delta_unit == UI_EventDeltaUnit_Line && dx != 0)
  {
    WhTextLine *line = &l->lines[Min(caret.line, (U32)l->line_count-1)];
    U32 end = line->text_end;
    while(end > line->text_start && (text.str[end-1] == '\n' || text.str[end-1] == ' ')) { end -= 1; }
    *byte_io = dx < 0 ? line->text_start : end;
    return 1;
  }
  if(dy != 0)
  {
    S64 target_line = (S64)caret.line + (dy > 0 ? 1 : -1);
    if(target_line < 0 || target_line >= (S64)l->line_count)
    {
      // Above the first line or below the last: the next Block's.
      S64 order = (S64)n->order + (dy > 0 ? 1 : -1);
      if(order < 0 || order >= (S64)m->block_count) { return 0; }
      UIShell_RtNode *to = uishell_rt_node(m, m->block_keys[order]);
      UIShell_RtPlaced *tp = to ? uishell_rt_placed(pl, to->key) : 0;
      if(to == 0) { return 0; }
      *key_io = to->key;
      if(tp && tp->layout && tp->layout->layout->text.line_count)
      {
        WhTextLayout *tl = &tp->layout->layout->text;
        WhTextLine *edge = &tl->lines[dy > 0 ? 0 : tl->line_count-1];
        WhTextPosition pos = {0};
        wh_text_offset_at(tl, caret.x, edge->y + edge->height/2, &pos);
        *byte_io = pos.offset;
      }
      else { *byte_io = dy > 0 ? 0 : (U32)to->text.size; }
      return 1;
    }
    WhTextLine *line = &l->lines[target_line];
    WhTextPosition pos = {0};
    wh_text_offset_at(l, caret.x, line->y + line->height/2, &pos);
    *byte_io = pos.offset;
    return 1;
  }
  return 0;
}

internal void
uishell_rt_keys(UIShell_RichTextState *s, UIShell_RtPlacement *pl, UIShell_RtStop *stops, U64 stop_count)
{
  char *error = 0;
  F32 line = uishell_rt_line_px(&s->theme);
  for(UI_Event *evt = 0; ui_next_event(&evt);)
  {
    B32 nav = evt->kind == UI_EventKind_Navigate;
    B32 tab = nav && evt->delta_unit == UI_EventDeltaUnit_Char && evt->delta_2s32.y != 0 && evt->delta_2s32.x == 0 &&
              !(evt->flags & (UI_EventFlag_ExplicitDirectional|UI_EventFlag_KeepMark));
    B32 cancel = evt->kind == UI_EventKind_Press && (evt->slot == UI_EventActionSlot_Cancel || evt->key == WM_Key_Esc);
    B32 accept = evt->kind == UI_EventKind_Press && (evt->slot == UI_EventActionSlot_Accept || evt->key == WM_Key_Return);
    // A focused Embed owns the keys but Tab and Esc (#542, decision 4).
    if(s->focus_embed != 0 && !tab)
    {
      UIShell_RtTable *table = uishell_rt_table_of(s, s->focus_embed);
      if(cancel)
      {
        wheelhouse_rich_text_select_block(s->doc, s->sub, s->focus_embed, &error);
        s->focus_embed = 0;
        ui_eat_event(evt);
      }
      else if(table && table->table && uishell_table_key(wheelhouse_table_grid(table->table), evt)) { ui_eat_event(evt); }
      continue;
    }
    if(tab)
    {
      S64 at = -1;
      for(U64 i = 0; i < stop_count && at < 0; i += 1) { if(s->focus_stop >= 0 && uishell_rt_stop_match(&stops[i], &s->focus_stop_value)) { at = (S64)i; } }
      if(stop_count != 0)
      {
        S64 next = at < 0 ? (evt->delta_2s32.y > 0 ? 0 : (S64)stop_count-1) : (at + (evt->delta_2s32.y > 0 ? 1 : -1) + (S64)stop_count) % (S64)stop_count;
        s->focus_stop = next;
        s->focus_stop_value = stops[next];
        s->focus_embed = stops[next].kind == UIShell_RtStop_Embed ? stops[next].key : 0;
        UIShell_RtPlaced *p = uishell_rt_placed(pl, stops[next].key);
        if(p == 0 || !p->visible || p->rect.y0 < 0 || p->rect.y1 > pl->viewport)
        {
          wheelhouse_rich_text_reveal(s->doc, s->sub, stops[next].key, stops[next].start, &error);
        }
      }
      ui_eat_event(evt);
      continue;
    }
    if(accept && s->focus_stop >= 0)
    {
      uishell_rt_activate_stop(s, &s->focus_stop_value);
      ui_eat_event(evt);
      continue;
    }
    if(cancel)
    {
      WheelhouseRichSelection sel = {0};
      if(s->focus_stop >= 0) { s->focus_stop = -1; ui_eat_event(evt); }
      else if(wheelhouse_rich_text_selection(s->doc, s->sub, &sel, &error))
      {
        wheelhouse_rich_text_clear_selection(s->doc, s->sub, &error);
        ui_eat_event(evt);
      }
      continue;
    }
    if(!nav) { continue; }
    if(evt->flags & UI_EventFlag_KeepMark)
    {
      // Shift with a key extends the selection from its focus.
      WheelhouseRichSelection sel = {0};
      U64 key = s->caret_key;
      U32 byte = s->caret_byte;
      B32 has = wheelhouse_rich_text_selection(s->doc, s->sub, &sel, &error);
      if(has) { key = sel.focus_key; byte = sel.focus_byte; }
      else if(!s->has_caret)
      {
        // No caret yet: start at the top of what is shown.
        for(U64 i = 0; i < pl->count; i += 1) { if(pl->v[i].node && pl->v[i].visible) { key = pl->v[i].key; byte = 0; break; } }
      }
      U64 anchor_key = has ? sel.anchor_key : key;
      U32 anchor_byte = has ? sel.anchor_byte : byte;
      if(uishell_rt_moved_point(s, pl, evt, &key, &byte))
      {
        wheelhouse_rich_text_select(s->doc, s->sub, anchor_key, anchor_byte, key, byte, &error);
        s->caret_key = key;
        s->caret_byte = byte;
        s->has_caret = 1;
        UIShell_RtPlaced *p = uishell_rt_placed(pl, key);
        if(p == 0 || p->rect.y0 < 0 || p->rect.y1 > pl->viewport) { wheelhouse_rich_text_reveal(s->doc, s->sub, key, byte, &error); }
        uishell_rt_selection_to_primary(s);
      }
      ui_eat_event(evt);
      continue;
    }
    // Without Shift, the keys scroll.
    S32 dy = evt->delta_2s32.y, dx = evt->delta_2s32.x;
    switch(evt->delta_unit)
    {
      default: break;
      case UI_EventDeltaUnit_Char: { if(dy) { wheelhouse_rich_text_scroll_by(s->doc, s->sub, dy*line*2, &error); } } break;
      case UI_EventDeltaUnit_Word:
      case UI_EventDeltaUnit_Page: { if(dy) { wheelhouse_rich_text_scroll_by(s->doc, s->sub, dy*Max(line, (F32)pl->viewport - line), &error); } } break;
      case UI_EventDeltaUnit_Line:
      case UI_EventDeltaUnit_Whole:
      {
        S32 d = dx ? dx : dy;
        if(d > 0) { wheelhouse_rich_text_scroll_to_end(s->doc, s->sub, &error); }
        else if(d < 0) { wheelhouse_rich_text_scroll_by(s->doc, s->sub, -(pl->top + 1), &error); }
      } break;
    }
    ui_eat_event(evt);
  }
  uishell_rt_error(&error);
}

////////////////////////////////
//~ The Renderer

// Find's state on the bar: "3 of 12", "12+" while scanning.
internal String8
uishell_rt_find_label(Arena *arena, UIShell_RichTextState *s)
{
  if(!s->find_active) { return str8_zero(); }
  char *error = 0;
  WheelhouseRichFindStatus status = {0};
  String8 result = str8_zero();
  if(wheelhouse_rich_text_find_step(s->doc, s->sub, 0, &status, &error))
  {
    String8 more = (!status.done || status.capped) ? str8_lit("+") : str8_zero();
    if(status.count == 0 && status.done) { result = push_str8f(arena, "No matches for \"%S\"", str8(s->find_query, s->find_query_size)); }
    else if(status.current != WHEELHOUSE_NONE) { result = push_str8f(arena, "%llu of %llu%S", (unsigned long long)status.current+1, (unsigned long long)status.count, more); }
    else { result = push_str8f(arena, "%llu%S matches", (unsigned long long)status.count, more); }
  }
  uishell_rt_error(&error);
  return result;
}

internal void
uishell_rt_reset(UIShell_RichTextState *s)
{
  uishell_rt_release(s);
  MemoryZeroStruct(s);
  s->initialized = 1;
  s->arena = arena_alloc();
  s->focus_stop = -1;
  s->match_current = WHEELHOUSE_NONE;
}

typedef struct UIShell_RtRange UIShell_RtRange;
struct UIShell_RtRange
{
  U32 start;
  U32 end;
  Vec4F32 color;
  U64 *counter;
};

// Overlays for one Block: the selection, find's matches and the caret, in
// its coordinates (#544, decision 8: never in its bucket).
internal void
uishell_rt_overlays(Arena *arena, UIShell_RichTextState *s, UIShell_RtPlaced *p, UIShell_RtBlockDraw *d,
                    WheelhouseRichSelection *sel, B32 has_sel, B32 focused)
{
  UIShell_RtTheme *t = &s->theme;
  U64 cap = 64;
  d->quads = push_array(arena, Rng2F32, cap);
  d->quad_colors = push_array(arena, Vec4F32, cap);
#define UISHELL_RT_QUAD(r, c) do { if(d->quad_count == cap) { \
    Rng2F32 *q = push_array(arena, Rng2F32, cap*2); Vec4F32 *qc = push_array(arena, Vec4F32, cap*2); \
    MemoryCopy(q, d->quads, sizeof(*q)*cap); MemoryCopy(qc, d->quad_colors, sizeof(*qc)*cap); d->quads = q; d->quad_colors = qc; cap *= 2; } \
    d->quads[d->quad_count] = (r); d->quad_colors[d->quad_count] = (c); d->quad_count += 1; } while(0)
  Vec2F32 o = uishell_rt_text_origin(p);
  WhTextLayout *l = p->layout ? &p->layout->layout->text : 0;
  // Text ranges as the layout's selection rects.
  UIShell_RtRange ranges[3];
  U32 range_count = 0;
  U32 sel_start = 0, sel_end = 0;
  if(has_sel && uishell_rt_selected_range(s, sel, p->node, &sel_start, &sel_end))
  {
    if(l == 0 || p->node->kind == WHEELHOUSE_RICH_KIND_EMBED || p->node->kind == WHEELHOUSE_RICH_KIND_RULE)
    {
      UISHELL_RT_QUAD(r2f32p(p->frame.indent, 0, dim_2f32(p->rect).x, p->height), t->selection);
      d->selection_quads += 1;
    }
    else { ranges[range_count++] = (UIShell_RtRange){sel_start, sel_end, t->selection, &d->selection_quads}; }
  }
  if(l)
  {
    Temp scratch = scratch_begin(&arena, 1);
    for(U32 r = 0; r < range_count; r += 1)
    {
      WhTextRects rects = {0};
      if(wh_text_selection_rects(l, ranges[r].start, ranges[r].end, uishell_rt_allocator(scratch.arena), &rects))
      {
        for(U64 i = 0; i < rects.count; i += 1)
        {
          WhTextRect *rr = &rects.rects[i];
          // An empty line still shows it is selected.
          F32 w = Max(rr->width, 4.f);
          UISHELL_RT_QUAD(r2f32p(o.x + rr->x, o.y + rr->y, o.x + rr->x + w, o.y + rr->y + rr->height), ranges[r].color);
          *ranges[r].counter += 1;
        }
      }
    }
    // Find's matches in this Block.
    for(U64 i = 0; i < s->match_count && p->node->kind != WHEELHOUSE_RICH_KIND_EMBED; i += 1)
    {
      WheelhouseRichMatch *m = &s->matches[i];
      if(m->key != p->key) { continue; }
      B32 current = i == s->match_current;
      WhTextRects rects = {0};
      if(wh_text_selection_rects(l, m->start, m->end, uishell_rt_allocator(scratch.arena), &rects))
      {
        for(U64 j = 0; j < rects.count; j += 1)
        {
          WhTextRect *rr = &rects.rects[j];
          UISHELL_RT_QUAD(r2f32p(o.x + rr->x, o.y + rr->y, o.x + rr->x + rr->width, o.y + rr->y + rr->height), current ? t->find_current : t->find);
          if(current) { d->current_find_quads += 1; } else { d->find_quads += 1; }
        }
      }
    }
    // The caret, where a selection's focus is, while the View has focus.
    if(focused && s->has_caret && s->caret_key == p->key)
    {
      WhTextCaret caret = {0};
      if(wh_text_caret_rect(l, Min(s->caret_byte, (U32)p->node->text.size), WH_TEXT_DOWNSTREAM, &caret))
      {
        UISHELL_RT_QUAD(r2f32p(o.x + caret.x, o.y + caret.y, o.x + caret.x + 1.5f, o.y + caret.y + caret.height), t->text);
      }
    }
    // A link with Tab's focus.
    if(s->focus_stop >= 0 && s->focus_stop_value.kind == UIShell_RtStop_Link && s->focus_stop_value.key == p->key)
    {
      WhTextRects rects = {0};
      Vec4F32 ring = t->focus; ring.w = 0.35f;
      if(wh_text_selection_rects(l, s->focus_stop_value.start, s->focus_stop_value.end, uishell_rt_allocator(scratch.arena), &rects))
      {
        for(U64 j = 0; j < rects.count; j += 1)
        {
          WhTextRect *rr = &rects.rects[j];
          UISHELL_RT_QUAD(r2f32p(o.x + rr->x - 1, o.y + rr->y, o.x + rr->x + rr->width + 1, o.y + rr->y + rr->height), ring);
        }
      }
    }
    scratch_end(scratch);
  }
  // A match in an Embed's text (a table's cell) marks the Embed whole.
  if(l == 0 || p->node->kind == WHEELHOUSE_RICH_KIND_EMBED)
  {
    for(U64 i = 0; i < s->match_count; i += 1)
    {
      if(s->matches[i].key != p->key) { continue; }
      B32 current = i == s->match_current;
      Vec4F32 wash = current ? t->find_current : t->find;
      wash.w *= 0.5f;
      UISHELL_RT_QUAD(r2f32p(p->frame.indent, p->frame.pad_top, dim_2f32(p->rect).x, p->height - p->frame.pad_bottom), wash);
      if(current) { d->current_find_quads += 1; } else { d->find_quads += 1; }
    }
  }
#undef UISHELL_RT_QUAD
}

// A file View's expression, `file:"<path>".data`, as rd_eval_string_from_file_path writes it.
internal String8
uishell_rt_path_from_expr(Arena *arena, String8 expr)
{
  String8 prefix = str8_lit("file:\""), suffix = str8_lit("\".data");
  if(expr.size <= prefix.size+suffix.size || !str8_match(str8_prefix(expr, prefix.size), prefix, 0) ||
     !str8_match(str8_postfix(expr, suffix.size), suffix, 0)) { return str8_zero(); }
  return raw_from_escaped_str8(arena, str8_substr(expr, r1u64(prefix.size, expr.size-suffix.size)));
}

WH_VIEW_UI_FUNCTION_DEF(rich_text)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_RichTextState *s = wh_view_state(ctx, UIShell_RichTextState);
  rd_view_state_from_view(ctx->settings.view, 0)->release_user_data = uishell_rt_release;
  if(!s->initialized) { uishell_rt_reset(s); }
  s->theme = uishell_rt_theme();
  UIShell_RtTheme *t = &s->theme;
  F32 px = uishell_rt_px(t), line = uishell_rt_line_px(t);
  char *error = 0;
  s->stats.frames += 1;
  s->stats.frame_layouts = 0;
  s->stats.frame_layout_us = 0;
  s->stats.blocks_boxed = 0;

  //- The file, through the content cache: saving it changes its hash, and
  // the reload keeps the reading position (the core's Scroll Anchor).
  String8 path = uishell_rt_path_from_expr(scratch.arena, rd_expr_from_cfg(rd_cfg_from_view(ctx->settings.view)));
  if(!str8_match(path, str8(s->path, s->path_size), 0) && path.size <= sizeof(s->path))
  {
    uishell_rt_reset(s);
    MemoryCopy(s->path, path.str, path.size);
    s->path_size = path.size;
  }
  FileProperties props = properties_from_file_path(path);
  U128 hash = path.size ? c_hash_from_key(fs_key_from_path(path, 0), 0) : u128_zero();
  B32 empty_file = props.modified != 0 && props.size == 0;
  if((!u128_match(hash, u128_zero()) || empty_file) && (s->doc == 0 || !u128_match(hash, s->loaded_hash)))
  {
    Access *access = access_open();
    String8 data = u128_match(hash, u128_zero()) ? str8_zero() : c_data_from_hash(access, hash);
    if(s->doc == 0)
    {
      s->doc = wheelhouse_rich_text_open(uishell_rt_wt(data), &error);
      if(s->doc && !wheelhouse_rich_text_subscribe(s->doc, &s->sub, &error)) { wheelhouse_rich_text_free(s->doc); s->doc = 0; }
    }
    else
    {
      U32 changed = 0;
      wheelhouse_rich_text_reload(s->doc, uishell_rt_wt(data), &changed, &error);
    }
    String8 failure = uishell_rt_error(&error);
    s->load_error = failure.size ? push_str8_copy(s->arena, failure) : str8_zero();
    s->loaded_hash = hash;
    access_close(access);
  }
  rd_workspace_surface_contribute_version(hash.u64[0] ^ hash.u64[1]);

  //- Deltas into the model.
  if(s->doc)
  {
    WheelhouseRichDelta *delta = wheelhouse_rich_text_poll(s->doc, s->sub, &error);
    if(delta)
    {
      uishell_rt_model_apply(&s->model, delta);
      wheelhouse_rich_delta_release(delta);
      uishell_rt_prune_tables(s);
      // A reload starts find's scan again.
      s->match_count = 0;
    }
    uishell_rt_error(&error);
  }

  Vec2F32 dim = dim_2f32(ctx->rect);
  UI_Box *root = &ui_nil_box;
  UI_FixedWidth(dim.x) UI_FixedHeight(dim.y) UI_ChildLayoutAxis(Axis2_Y)
  {
    root = ui_build_box_from_string(UI_BoxFlag_Clip, str8_lit("rich_text_view"));
  }
  if(s->doc == 0)
  {
    rd_store_view_loading_info(props.modified != 0 && s->load_error.size == 0, 0, 0);
    UI_Parent(root) UI_PrefWidth(ui_px(dim.x, 1)) UI_PrefHeight(ui_em(3, 1)) UI_TextAlignment(UI_TextAlign_Center) UI_TagF("weak")
    {
      if(props.modified == 0) { ui_labelf("Could not find \"%S\".", path); }
      else if(s->load_error.size) { ui_labelf("Could not show \"%S\": %S", path, s->load_error); }
      else { ui_label(str8_lit("Loading...")); }
    }
    scratch_end(scratch);
    return;
  }
  rd_store_view_loading_info(0, 0, 0);

  //- The bar: the file, find's state and the source toggle.
  F32 bar_h = floor_f32(line*1.3f);
  UI_Parent(root) UI_PrefHeight(ui_px(bar_h, 1)) UI_PrefWidth(ui_px(dim.x, 1)) UI_Row
    UI_PrefWidth(ui_text_dim(10, 1))
  {
    UI_TagF("weak") ui_label(str8_skip_last_slash(path));
    ui_spacer(ui_pct(1, 0));
    String8 find_label = uishell_rt_find_label(scratch.arena, s);
    if(find_label.size) { UI_TagF("weak") ui_label(find_label); }
    if(ui_clicked(ui_buttonf("Source###rich_text_source"))) { uishell_cmd("toggle_markdown_source"); }
  }

  //- Geometry: a reading column, centred.
  UI_ScrollRegion region = ui_scroll_region_layout(ui_scroll_region_params(r2f32p(0, bar_h, dim.x, dim.y), UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Always));
  Vec2F32 vdim = dim_2f32(region.viewport);
  F32 margin = floor_f32(px*1.25f);
  F32 content_width = floor_f32(Clamp(px*8, vdim.x - 2*margin, px*48));
  F32 content_x = floor_f32(Max(margin, (vdim.x - content_width)/2));
  if((F64)vdim.y != s->viewport)
  {
    s->viewport = vdim.y;
    wheelhouse_rich_text_set_viewport(s->doc, s->sub, vdim.y, &error);
  }
  if(content_width != s->width)
  {
    s->width = content_width;
    wheelhouse_rich_text_set_metrics(s->doc, s->sub, content_width, line, content_width/(px*0.52f), &error);
  }
  uishell_rt_error(&error);

  //- Commands: find, and Copy as Markdown.
  for(UIShell_Cmd *cmd = 0; uishell_next_view_cmd(&cmd);)
  {
    B32 forward = str8_match(cmd->name, str8_lit("search"), 0) || str8_match(cmd->name, str8_lit("find_next"), 0);
    B32 backward = str8_match(cmd->name, str8_lit("search_backwards"), 0) || str8_match(cmd->name, str8_lit("find_prev"), 0);
    if(forward || backward)
    {
      String8 needle = cmd->regs->string.size != 0 ? cmd->regs->string : rd_view_query_input();
      if(needle.size == 0) { needle = str8(s->find_query, s->find_query_size); }
      uishell_rt_find_start(s, needle);
      uishell_rt_find_move(s, forward);
    }
    else if(str8_match(cmd->name, str8_lit("copy_markdown"), 0))
    {
      String8 markdown = s->focus_embed ? uishell_rt_copy_block(scratch.arena, s, s->focus_embed, WHEELHOUSE_RICH_COPY_MARKDOWN)
                                        : uishell_rt_copy(scratch.arena, s, WHEELHOUSE_RICH_COPY_MARKDOWN);
      if(markdown.size) { wm_set_clipboard_text(markdown); }
    }
  }
  // Typing in the query bar highlights as it goes.
  String8 query = rd_view_query_input();
  if(query.size != 0 && !str8_match(query, str8(s->find_query, s->find_query_size), 0)) { uishell_rt_find_start(s, query); }
  if(s->find_active)
  {
    B32 done = uishell_rt_find_step(s, KB(256));
    if(s->find_pending_step && s->match_count) { uishell_rt_find_move(s, s->find_forward); }
    if(!done) { rd_request_frame(); }
  }

  //- Input, against what was drawn: where the content box was last frame,
  // and the Blocks as they are placed now.
  UI_Key content_key = ui_key_from_stringf(rd_view_ui_key(ctx->settings.view), "###rich_text_content");
  Rng2F32 content_rect = ui_box_from_key(content_key)->rect;
  UIShell_RtPlacement pl = uishell_rt_place(scratch.arena, s, content_x, content_width);
  B32 pressed = 0;
  uishell_rt_pointer(s, &pl, content_rect, &pressed);
  if(pressed) { uishell_cmd("focus_panel"); }
  B32 focused = 0;
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active())
  {
    focused = 1;
    UIShell_RtStop *stops = 0;
    U64 stop_count = uishell_rt_stops(scratch.arena, s, &pl, &stops);
    uishell_rt_keys(s, &pl, stops, stop_count);
    uishell_rt_register_edit_owner(content_key, s, ctx->settings.view);
  }

  //- The scroll bar, over the Scroll Anchor's thumb.
  WheelhouseRichThumb thumb = {0};
  wheelhouse_rich_text_thumb(s->doc, s->sub, &thumb, &error);
  uishell_rt_error(&error);
  UI_ScrollRegionThumbAxis axes[Axis2_COUNT] = {0};
  F64 travel = 1.0 - thumb.size;
  F64 fraction = travel > 0 ? Clamp(0.0, thumb.start/travel, 1.0) : 0;
  axes[Axis2_Y].thumb = (UI_ScrollThumb){fraction, thumb.size};
  axes[Axis2_Y].activity = (F32)pl.top;
  axes[Axis2_Y].drag_origin = &fraction;
  axes[Axis2_Y].drag_origin_size = sizeof(fraction);
  UI_ScrollRegionThumbSignal bars = ui_scroll_region_build_thumbs(root, content_key, &region, axes, UI_BoxFlag_Floating);
  UI_ScrollBarSignal bar = bars.bar[Axis2_Y];
  if(bar.kind == UI_ScrollBarActionKind_Drag && bar.drag_moved && bar.drag_origin)
  {
    F64 from = *(F64 *)bar.drag_origin;
    F64 target = Clamp(0.0, from + bar.drag_pct, 1.0)*Max(0.0, pl.total - pl.viewport);
    wheelhouse_rich_text_scroll_by(s->doc, s->sub, target - pl.top, &error);
  }
  else if(bar.step != 0)
  {
    wheelhouse_rich_text_scroll_by(s->doc, s->sub, bar.page ? bar.step*Max(line, (F32)pl.viewport - line) : bar.step*line*3, &error);
  }
  uishell_rt_error(&error);

  //- Place again after the input, and build a box per visible Block.
  pl = uishell_rt_place(scratch.arena, s, content_x, content_width);
  WheelhouseRichSelection sel = {0};
  B32 has_sel = wheelhouse_rich_text_selection(s->doc, s->sub, &sel, &error);
  uishell_rt_error(&error);
  U64 glyph_epoch = uishell_rt_glyph_epoch();
  F32 table_line = uishell_rt_table_line(t);
  F32 digit = Max(1.f, fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, str8_lit("0")).x);
  B32 copied_showing = s->copied_until_us > now_time_us();
  if(copied_showing) { rd_request_frame_at(s->copied_until_us); }
  UI_Parent(bars.content_box)
  {
    for(U64 i = 0; i < pl.count; i += 1)
    {
      UIShell_RtPlaced *p = &pl.v[i];
      if(!p->visible || p->node == 0 || p->part != WHEELHOUSE_RICH_PART_BLOCK) { continue; }
      F32 width = dim_2f32(p->rect).x, height = dim_2f32(p->rect).y;
      UIShell_RtBucket *b = uishell_rt_bucket(s, p->key);
      U32 width_bits = 0;
      MemoryCopy(&width_bits, &width, sizeof(width_bits));
      U64 stamp = p->layout ? p->layout->stamp : p->node->version ^ ((U64)(U32)height << 32);
      if(b->bucket == 0 || b->incomplete || b->layout_stamp != stamp || b->colors_hash != t->colors_hash ||
         b->glyph_epoch != glyph_epoch || b->width_bits != width_bits)
      {
        uishell_rt_build_bucket(s, b, p->node, p->layout, width, height);
        b->layout_stamp = stamp;
        b->colors_hash = t->colors_hash;
        b->glyph_epoch = uishell_rt_glyph_epoch();
        b->width_bits = width_bits;
        // A glyph not placed this frame is drawn by the next.
        if(b->incomplete) { rd_request_frame(); }
      }
      b->last_frame = rd_state->frame_index;
      uishell_rt_touch_pages(b);
      UIShell_RtBlockDraw *d = push_array(ui_build_arena(), UIShell_RtBlockDraw, 1);
      d->key = p->key;
      d->kind = p->node->kind;
      d->bucket = b->bucket;
      d->focus_color = t->focus;
      uishell_rt_overlays(ui_build_arena(), s, p, d, &sel, has_sel, focused);
      UI_Box *box = &ui_nil_box;
      UI_Rect(p->rect)
      {
        box = ui_build_box_from_stringf(UI_BoxFlag_Floating, "###rt_block_%llx", (unsigned long long)p->key);
      }
      ui_box_equip_custom_draw(box, uishell_rt_block_draw, d);
      s->stats.blocks_boxed += 1;
      B32 stop_here = s->focus_stop >= 0 && s->focus_stop_value.key == p->key;
      if(p->frame.code)
      {
        B32 copied = copied_showing && s->copied_key == p->key;
        UI_Parent(box) UI_Rect(uishell_rt_copy_button_rect(p, t)) UI_TextAlignment(UI_TextAlign_Center)
          UI_TagF(stop_here && s->focus_stop_value.kind == UIShell_RtStop_CopyButton ? "hover" : "weak")
        {
          ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_DrawText|UI_BoxFlag_DrawBorder|
                                    (stop_here ? UI_BoxFlag_DrawBackground : 0),
                                    "%s###rt_copy", copied ? "Copied" : "Copy");
        }
      }
      if(p->frame.table)
      {
        UIShell_RtTable *tb = uishell_rt_table(s, p->node, table_line, digit);
        B32 capped = 0;
        F32 th = uishell_rt_table_height(t, p->node, tb->expanded, s->viewport, &capped);
        Rng2F32 tr = uishell_rt_table_rect(p, th);
        d->focused = s->focus_embed == p->key;
        tb->last_frame = rd_state->frame_index;
        if(tb->table)
        {
          UI_Box *grid_box = &ui_nil_box;
          UI_Parent(box) UI_Rect(tr)
          {
            grid_box = ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_Clip, "###rt_table");
          }
          U64 now = now_time_us();
          wheelhouse_table_frame(tb->table, dim_2f32(tr).x, dim_2f32(tr).y, now);
          BinnacleGrid *grid = wheelhouse_table_grid(tb->table);
          UIShell_TableDraw *td = push_array(ui_build_arena(), UIShell_TableDraw, 1);
          td->table = tb->table;
          for(BinnacleEffect effect; binnacle_grid_poll_effect(grid, &effect);) {}
          binnacle_grid_snapshot(grid, &td->snap);
          ui_box_equip_custom_draw(grid_box, uishell_table_draw, td);
          if(td->snap.capturing || td->snap.view_pending) { rd_request_frame(); }
        }
        if(capped || tb->expanded)
        {
          B32 toggle_stop = stop_here && s->focus_stop_value.kind == UIShell_RtStop_Expand;
          UI_Parent(box) UI_Rect(uishell_rt_expand_rect(p, t, th)) UI_TagF(toggle_stop ? "hover" : "weak")
          {
            ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_DrawText|(toggle_stop ? UI_BoxFlag_DrawBorder : 0),
                                      "%s###rt_expand", tb->expanded ? "Collapse" : "Expand");
          }
        }
      }
      else if(stop_here && s->focus_stop_value.kind == UIShell_RtStop_Embed) { d->focused = 1; }
    }
  }
  if(s->selecting || s->table_capture) { rd_request_frame(); }
  uishell_rt_prune_buckets(s);
  scratch_end(scratch);
}

// The source toggle (#561): the same View, its Renderer swapped between
// Rich Text and the code/text view, as the pending page swaps its own.
internal void
uishell_rt_toggle_source(RD_ViewAddress address)
{
  CFG_Node *view = rd_cfg_from_view(address);
  String8 to = str8_zero();
  if(str8_match(view->string, str8_lit("rich_text"), 0)) { to = str8_lit("text"); }
  else if(str8_match(view->string, str8_lit("text"), 0))
  {
    Temp scratch = scratch_begin(0, 0);
    String8 path = uishell_rt_path_from_expr(scratch.arena, rd_expr_from_cfg(view));
    to = wh_handler_from_media_type(WH_Content_File, wh_media_type_from_path(path))->renderer;
    scratch_end(scratch);
  }
  if(to.size == 0 || view == &cfg_nil_node) { return; }
  RD_ViewState *vs = rd_view_state_from_view(address, 0);
  cfg_node_equip_string(rd_state->cfg, view, to);
  for(RD_ArenaExt *ext = vs->first_arena_ext; ext != 0; ext = ext->next) { arena_release(ext->arena); }
  if(vs->release_user_data) { vs->release_user_data(vs->user_data); vs->release_user_data = 0; }
  arena_pop_to(vs->arena, vs->arena_reset_pos);
  vs->user_data = 0;
  vs->user_data_size = 0;
  vs->first_arena_ext = vs->last_arena_ext = 0;
}
