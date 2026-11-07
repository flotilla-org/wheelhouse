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
  // A table cell laid out with tabular figures (uishell_rt_cell_layout).
  U32 tabular;
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
  // A Turn's role; a Section's kind, tool, status, name, title and
  // duration; a Tool Run's summary and failures.
  U32 role;
  U32 section;
  U32 tool;
  U32 status;
  String8 name;
  String8 title;
  U64 duration_ms;
  String8 summary;
  U32 failed;
  U32 failed_at;
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
  return (n->text.size + n->language.size + n->label.size + n->media_type.size + n->name.size + n->title.size + n->summary.size +
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
  n->name = push_str8_copy(arena, n->name);
  n->title = push_str8_copy(arena, n->title);
  n->summary = push_str8_copy(arena, n->summary);
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
  n->role = c->role;
  n->section = c->section;
  n->tool = c->tool;
  n->status = c->status;
  n->name = uishell_rt_s8(c->name);
  n->title = uishell_rt_s8(c->title);
  n->duration_ms = c->duration_ms;
  n->summary = uishell_rt_s8(c->summary);
  n->failed = c->failed;
  n->failed_at = c->failed_at;
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
  UIShell_RtInk_Wash       = (1<<6),
};

// A literal colour: 0xRRGGBB, a palette index with bit 24 set, or the
// theme's foreground (0) or background (1) with bit 25 set.
#define UISHELL_RT_PALETTE_BIT (1u<<24)
#define UISHELL_RT_DEFAULT_BIT (1u<<25)

typedef struct UIShell_RtStyleInk UIShell_RtStyleInk;
struct UIShell_RtStyleInk
{
  UIShell_RtInk ink;
  // For UIShell_RtInk_Literal.
  U32 color;
  // For UIShell_RtInk_Wash: a literal background.
  U32 background;
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
  Vec4F32 background;
  // A shell call's output, darker than the page; a diff's added and removed
  // lines; a failure's mark.
  Vec4F32 terminal;
  Vec4F32 good;
  Vec4F32 bad;
  // ANSI's sixteen, which a log's palette colours map through (0xRRGGBB).
  U32 palette[16];
  U64 colors_hash;
};

internal UIShell_RtTheme
uishell_rt_theme_sized(F32 font_size)
{
  UIShell_RtTheme t = {0};
  t.size = font_size;
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
  t.background = background;
  t.terminal = mix_4f32(background, v4f32(0, 0, 0, 1), 0.22f);
  String8 good[] = {str8_lit_comp("good"), str8_lit_comp("text")}, bad[] = {str8_lit_comp("bad"), str8_lit_comp("text")};
  t.good = ui_color_from_tags_key_extras(ui_top_tags_key(), (String8Array){good, ArrayCount(good)});
  t.bad = ui_color_from_tags_key_extras(ui_top_tags_key(), (String8Array){bad, ArrayCount(bad)});
  U32 palette[16] =
  {
    0x000000, 0xcd3131, 0x0dbc79, 0xe5e510, 0x2472c8, 0xbc3fbc, 0x11a8cd, 0xe5e5e5,
    0x666666, 0xf14c4c, 0x23d18b, 0xf5f543, 0x3b8eea, 0xd670d6, 0x29b8db, 0xffffff,
  };
  MemoryCopyArray(t.palette, palette);
  t.colors_hash = XXH3_64bits(&t.text, OffsetOf(UIShell_RtTheme, colors_hash) - OffsetOf(UIShell_RtTheme, text));
  return t;
}

// A literal colour: the theme's sixteen, then xterm's 6x6x6 cube and its
// greys.
internal Vec4F32
uishell_rt_literal_color(UIShell_RtTheme *t, U32 color)
{
  if(color & UISHELL_RT_DEFAULT_BIT) { return (color & 1) ? t->background : t->text; }
  if(color & UISHELL_RT_PALETTE_BIT)
  {
    U32 index = color & 0xff;
    if(index < 16) { color = t->palette[index]; }
    else if(index < 232)
    {
      U32 cube = index - 16, levels[6] = {0, 95, 135, 175, 215, 255};
      color = (levels[cube/36]<<16) | (levels[(cube/6)%6]<<8) | levels[cube%6];
    }
    else
    {
      U32 grey = 8 + (index - 232)*10;
      color = (grey<<16) | (grey<<8) | grey;
    }
  }
  return v4f32(((color>>16)&0xff)/255.f, ((color>>8)&0xff)/255.f, (color&0xff)/255.f, t->text.w);
}

internal UIShell_RtTheme
uishell_rt_theme(void)
{
  return uishell_rt_theme_sized(rd_font_size());
}

internal Vec4F32
uishell_rt_ink_color(UIShell_RtTheme *t, UIShell_RtStyleInk *s)
{
  if(s->ink & UIShell_RtInk_Literal) { return uishell_rt_literal_color(t, s->color); }
  if(s->ink & UIShell_RtInk_Link) { return t->link; }
  if(s->ink & UIShell_RtInk_Weak) { return t->weak; }
  return t->text;
}

////////////////////////////////
//~ Container Styles (CONTEXT.md)

// What a Container Style draws around a Container's Blocks.
typedef U32 UIShell_RtChrome;
enum
{
  UIShell_RtChrome_Background = (1<<0),
  // A header row: an icon for the kind, the name, the title, the status.
  UIShell_RtChrome_Header     = (1<<1),
  // Collapsed when the View first sees it; its header toggles it.
  UIShell_RtChrome_Collapsed  = (1<<2),
  // Its header opens its thread as a View of its own, in place of toggling.
  UIShell_RtChrome_Thread     = (1<<3),
};

typedef enum UIShell_RtAlign
{
  UIShell_RtAlign_Start,
  UIShell_RtAlign_End,
}
UIShell_RtAlign;

// A rule for the Containers of `kind`, and of `role` and `section` unless
// they are 0 (WHEELHOUSE_RICH_ROLE_NONE, _SECTION_NONE: any). Lengths are
// in ems of the theme's size.
typedef struct UIShell_RtContainerStyle UIShell_RtContainerStyle;
struct UIShell_RtContainerStyle
{
  U32 kind;
  U32 role;
  U32 section;
  // Between the chrome's edges and the Blocks.
  F32 inset_x;
  F32 inset_top;
  F32 inset_bottom;
  // At most this share of the column it sits in, 0 for all of it; content
  // narrower than that shrinks it to fit.
  F32 max_width;
  UIShell_RtAlign align;
  UIShell_RtChrome chrome;
  F32 radius;
  // The WHEELHOUSE_RICH_ACTION_* its hover offers: in its header row if it
  // has one, else in a row under it.
  U32 actions;
  // Below everything, before the next Container.
  F32 gap;
};

// A View's set, picked as View state. Another set (a Slack- or WhatsApp-
// like one) is another table here; a control its bubbles offer on hover
// (rewind, fork) is another action bit.
typedef struct UIShell_RtStyleSet UIShell_RtStyleSet;
struct UIShell_RtStyleSet
{
  String8 name;
  UIShell_RtContainerStyle *rules;
  U64 rule_count;
};

// First match wins.
global UIShell_RtContainerStyle uishell_rt_chat_rules[] =
{
  {WHEELHOUSE_RICH_KIND_TURN, WHEELHOUSE_RICH_ROLE_USER, 0, .inset_x = 0.8f, .inset_top = 0.5f, .inset_bottom = 0.5f, .max_width = 0.75f,
   .align = UIShell_RtAlign_End, .chrome = UIShell_RtChrome_Background, .radius = 0.9f, .actions = WHEELHOUSE_RICH_ACTION_MESSAGE, .gap = 0.3f},
  {WHEELHOUSE_RICH_KIND_TURN, 0, 0, .actions = WHEELHOUSE_RICH_ACTION_MESSAGE, .gap = 0.3f},
  {WHEELHOUSE_RICH_KIND_TOOL_RUN, 0, 0, .inset_x = 1.2f, .inset_bottom = 0.3f,
   .chrome = UIShell_RtChrome_Header|UIShell_RtChrome_Collapsed, .gap = 0.2f},
  {WHEELHOUSE_RICH_KIND_SECTION, 0, WHEELHOUSE_RICH_SECTION_TOOL, .inset_x = 1.2f, .inset_bottom = 0.3f,
   .chrome = UIShell_RtChrome_Header|UIShell_RtChrome_Collapsed,
   .actions = WHEELHOUSE_RICH_ACTION_TOOL_OUTPUT|WHEELHOUSE_RICH_ACTION_COMMAND, .gap = 0.2f},
  {WHEELHOUSE_RICH_KIND_SECTION, 0, WHEELHOUSE_RICH_SECTION_REASONING, .inset_x = 1.2f, .inset_bottom = 0.3f,
   .chrome = UIShell_RtChrome_Header|UIShell_RtChrome_Collapsed, .gap = 0.2f},
  {WHEELHOUSE_RICH_KIND_SECTION, 0, WHEELHOUSE_RICH_SECTION_SUBAGENT,
   .chrome = UIShell_RtChrome_Header|UIShell_RtChrome_Collapsed|UIShell_RtChrome_Thread, .gap = 0.2f},
};

// "document" has no rules: markdown presents its Containers plainly.
global UIShell_RtStyleSet uishell_rt_style_sets[] =
{
  {str8_lit_comp("document"), 0, 0},
  {str8_lit_comp("chat"), uishell_rt_chat_rules, ArrayCount(uishell_rt_chat_rules)},
};

internal UIShell_RtStyleSet *
uishell_rt_style_set_from_name(String8 name)
{
  for EachElement(i, uishell_rt_style_sets)
  {
    if(str8_match(uishell_rt_style_sets[i].name, name, StringMatchFlag_CaseInsensitive)) { return &uishell_rt_style_sets[i]; }
  }
  return 0;
}

internal UIShell_RtContainerStyle *
uishell_rt_container_style(UIShell_RtStyleSet *set, UIShell_RtNode *n)
{
  if(set == 0 || n == 0 || n->kind >= WHEELHOUSE_RICH_KIND_PARAGRAPH || n->key == WHEELHOUSE_RICH_ROOT_KEY) { return 0; }
  for(U64 i = 0; i < set->rule_count; i += 1)
  {
    UIShell_RtContainerStyle *r = &set->rules[i];
    if(r->kind == n->kind && (r->role == 0 || r->role == n->role) && (r->section == 0 || r->section == n->section)) { return r; }
  }
  return 0;
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
  // A code Block shown as an Embedded View of the text view (#622), not
  // laid out here.
  B32 code_embed;
  B32 rule;
  B32 table;
  // A break in a conversation: its text centred between two rules.
  B32 divider;
  B32 weak;
  // A tool call's input line.
  B32 input;
  // A shell call's output, styled as a terminal: the first and last of its
  // Log Blocks carry its box's top and bottom.
  B32 terminal;
  B32 terminal_first;
  B32 terminal_last;
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

// A code Block's media type: it is an Embedded View while a Handler shows
// it, laid out as text otherwise.
#define UISHELL_RT_CODE_MEDIA_TYPE "application/vnd.flotilla.code"

// A diff is laid out here, its lines coloured by their first character
// (#667): the code view has no lexer for one.
internal B32
uishell_rt_is_diff(UIShell_RtNode *n)
{
  return n->kind == WHEELHOUSE_RICH_KIND_CODE && str8_match(n->language, str8_lit("diff"), StringMatchFlag_CaseInsensitive);
}

internal B32
uishell_rt_code_embedded(UIShell_RtNode *n)
{
  return n->kind == WHEELHOUSE_RICH_KIND_CODE && !uishell_rt_is_diff(n) &&
         wh_handler_from_media_type(WH_Content_Embed, str8_lit(UISHELL_RT_CODE_MEDIA_TYPE))->renderer.size != 0;
}

// A shell call's output Block, and where it is in its run of Log Blocks.
internal B32
uishell_rt_terminal(UIShell_RtModel *m, UIShell_RtNode *n, U64 *index_out, U64 *count_out)
{
  UIShell_RtNode *parent = uishell_rt_node(m, n->parent);
  if(n->kind != WHEELHOUSE_RICH_KIND_LOG || parent == 0 || parent->section != WHEELHOUSE_RICH_SECTION_TOOL ||
     parent->tool != WHEELHOUSE_RICH_TOOL_SHELL) { return 0; }
  U64 index = 0, count = 0;
  for(U64 i = 0; i < parent->child_count; i += 1)
  {
    UIShell_RtNode *c = uishell_rt_node(m, parent->children[i]);
    if(c == 0 || c->kind != WHEELHOUSE_RICH_KIND_LOG) { continue; }
    if(c == n) { index = count; }
    count += 1;
  }
  if(index_out) { *index_out = index; }
  if(count_out) { *count_out = count; }
  return 1;
}

// Selected whole or not at all (#546; #607, decision 2).
internal B32
uishell_rt_atomic(UIShell_RtNode *n)
{
  return n->kind == WHEELHOUSE_RICH_KIND_EMBED || n->kind == WHEELHOUSE_RICH_KIND_RULE || uishell_rt_code_embedded(n);
}

// A code Embed's hover controls, and the gap around them.
internal F32
uishell_rt_control_h(UIShell_RtTheme *t)
{
  return floor_f32(uishell_rt_px(t)*1.3f);
}

internal F32
uishell_rt_control_gap(UIShell_RtTheme *t)
{
  return floor_f32(uishell_rt_px(t)*0.25f);
}

internal UIShell_RtFrame
uishell_rt_frame(Arena *arena, UIShell_RtModel *m, UIShell_RtTheme *t, UIShell_RtStyleSet *styles, UIShell_RtNode *n)
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
      f.code_embed = uishell_rt_code_embedded(n);
      f.inset_x = floor_f32(px*0.7f);
      f.inset_y = floor_f32(px*0.5f);
      // An embed's hover controls sit outside it, above its top right: room
      // is kept for them, so showing them moves nothing.
      f.pad_top = f.code_embed ? uishell_rt_control_h(t) + 2*uishell_rt_control_gap(t) : floor_f32(px*0.15f);
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
    // A log's chunks run on as one block of output.
    case WHEELHOUSE_RICH_KIND_LOG:
    {
      U64 index = 0, count = 0;
      if(uishell_rt_terminal(m, n, &index, &count))
      {
        f.terminal = 1;
        f.terminal_first = index == 0;
        f.terminal_last = index + 1 == count;
        f.inset_x = floor_f32(px*0.7f);
        f.pad_top = f.terminal_first ? floor_f32(px*0.45f) : 0;
        f.pad_bottom = f.terminal_last ? floor_f32(px*0.45f) : 0;
      }
    } break;
    case WHEELHOUSE_RICH_KIND_INPUT:
    {
      f.input = 1;
      f.inset_x = floor_f32(px*0.7f);
      f.inset_y = floor_f32(px*0.3f);
      f.pad_bottom = floor_f32(px*0.4f);
    } break;
    case WHEELHOUSE_RICH_KIND_DIVIDER:
    {
      f.divider = 1;
      f.weak = 1;
      f.pad_top = floor_f32(px*0.6f);
      f.pad_bottom = floor_f32(px*0.6f);
    } break;
    case WHEELHOUSE_RICH_KIND_NOTICE:
    {
      f.weak = 1;
      f.pad_bottom = floor_f32(px*0.4f);
    } break;
  }
  if(f.label_above) { f.inset_y += uishell_rt_line_px(t); }
  // The last Block in a styled Container leaves its gap out: the Container's
  // bottom inset is the space below it, as CSS keeps a last child's margin
  // out of its parent's padding.
  for(UIShell_RtNode *c = n, *a = parent; a != 0 && a->child_count != 0 && a->children[a->child_count-1] == c->key; c = a, a = uishell_rt_node(m, a->parent))
  {
    if(uishell_rt_container_style(styles, a) != 0) { f.pad_bottom = 0; break; }
  }
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
  UIShell_RtAttr_Background = (1<<7),
};

// Ligatures off for code: its glyphs are its characters.
read_only global WhTextFeature uishell_rt_no_ligatures[] =
{
  {{'l', 'i', 'g', 'a'}, 0}, {{'c', 'a', 'l', 't'}, 0}, {{'d', 'l', 'i', 'g'}, 0},
};

// A column of figures: every digit one width, so right-aligned numbers line
// up (#624).
read_only global WhTextFeature uishell_rt_tabular_figures[] =
{
  {{'t', 'n', 'u', 'm'}, 1},
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
  else if(n->kind == WHEELHOUSE_RICH_KIND_CODE || n->kind == WHEELHOUSE_RICH_KIND_LOG || n->kind == WHEELHOUSE_RICH_KIND_INPUT)
  {
    s.family = WH_TEXT_FAMILY_CODE;
    s.size = t->size*t->code_scale;
    s.features = uishell_rt_no_ligatures;
    s.feature_count = ArrayCount(uishell_rt_no_ligatures);
  }
  else if(n->tabular)
  {
    s.features = uishell_rt_tabular_figures;
    s.feature_count = ArrayCount(uishell_rt_tabular_figures);
  }
  return s;
}

internal UIShell_RtStyled
uishell_rt_styled(Arena *arena, UIShell_RtTheme *t, UIShell_RtNode *n)
{
  UIShell_RtStyled result = {0};
  // A diff's three line styles, or a style per span's start and end.
  U64 cap = 4 + 2*n->span_count;
  result.styles = push_array(arena, WhTextStyle, cap);
  result.inks = push_array(arena, UIShell_RtStyleInk, cap);
  UIShell_RtAttrs *style_attrs = push_array(arena, UIShell_RtAttrs, cap);
  U32 *style_colors = push_array(arena, U32, cap);
  U32 *style_backgrounds = push_array(arena, U32, cap);
  result.styles[0] = uishell_rt_base_style(t, n);
  result.inks[0].ink = (n->kind == WHEELHOUSE_RICH_KIND_CODE ? UIShell_RtInk_Code :
                        n->kind == WHEELHOUSE_RICH_KIND_DIVIDER || n->kind == WHEELHOUSE_RICH_KIND_NOTICE ? UIShell_RtInk_Weak : 0);
  result.style_count = 1;
  if(uishell_rt_is_diff(n))
  {
    // Added lines green, removed red, hunk headers weak: by the first
    // character. A file's header is a `--- ` line with a `+++ ` line after
    // it, left plain: a removed line reading `-- ` is not one.
    U64 lines = 1;
    for(U64 i = 0; i < n->text.size; i += 1) { lines += n->text.str[i] == '\n'; }
    result.runs = push_array(arena, WhTextStyleRun, lines);
    U32 inks[] = {UIShell_RtInk_Literal, UIShell_RtInk_Literal, UIShell_RtInk_Weak};
    U32 colors[] = {UISHELL_RT_PALETTE_BIT | 2, UISHELL_RT_PALETTE_BIT | 1, 0};
    for(U32 i = 0; i < ArrayCount(inks); i += 1)
    {
      result.styles[1+i] = result.styles[0];
      result.inks[1+i] = (UIShell_RtStyleInk){.ink = inks[i], .color = colors[i]};
    }
    result.style_count = 4;
    B32 after_header = 0;
    for(U64 start = 0; start < n->text.size;)
    {
      U64 end = start;
      while(end < n->text.size && n->text.str[end] != '\n') { end += 1; }
      String8 line = str8_substr(n->text, r1u64(start, end));
      String8 next = str8_skip(n->text, Min(end + 1, n->text.size));
      B32 header = (str8_match(str8_prefix(line, 4), str8_lit("--- "), 0) && str8_match(str8_prefix(next, 4), str8_lit("+++ "), 0)) ||
                   (after_header && str8_match(str8_prefix(line, 4), str8_lit("+++ "), 0));
      after_header = header && line.str[0] == '-';
      U32 style = header ? 0 : line.size && line.str[0] == '+' ? 1 : line.size && line.str[0] == '-' ? 2 :
                  str8_match(str8_prefix(line, 2), str8_lit("@@"), 0) ? 3 : 0;
      if(style && end > start) { result.runs[result.run_count++] = (WhTextStyleRun){(U32)start, (U32)(end - start), style}; }
      start = end + 1;
    }
    return result;
  }
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
    U32 color = 0, background = 0;
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
        case WHEELHOUSE_RICH_ATTR_COLOR_PALETTE: { attrs |= UIShell_RtAttr_Color; color = UISHELL_RT_PALETTE_BIT | (s->value & 0xff); } break;
        case WHEELHOUSE_RICH_ATTR_COLOR_DEFAULT: { attrs |= UIShell_RtAttr_Color; color = UISHELL_RT_DEFAULT_BIT | (s->value & 1); } break;
        case WHEELHOUSE_RICH_ATTR_BACKGROUND_RGB: { attrs |= UIShell_RtAttr_Background; background = s->value & 0xffffff; } break;
        case WHEELHOUSE_RICH_ATTR_BACKGROUND_PALETTE: { attrs |= UIShell_RtAttr_Background; background = UISHELL_RT_PALETTE_BIT | (s->value & 0xff); } break;
        case WHEELHOUSE_RICH_ATTR_BACKGROUND_DEFAULT: { attrs |= UIShell_RtAttr_Background; background = UISHELL_RT_DEFAULT_BIT | (s->value & 1); } break;
      }
    }
    if(attrs == 0) { continue; }
    U64 style = 0;
    for(U64 i = 1; i < result.style_count && style == 0; i += 1)
    {
      if(style_attrs[i] == attrs && style_colors[i] == color && style_backgrounds[i] == background) { style = i; }
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
      if(attrs & UIShell_RtAttr_Background) { ink.ink |= UIShell_RtInk_Wash; ink.background = background; }
      result.styles[style] = s;
      result.inks[style] = ink;
      style_attrs[style] = attrs;
      style_colors[style] = color;
      style_backgrounds[style] = background;
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
  U32 max_lines;
  UIShell_RtLayout *layout;
  U32 size_class;
  U64 bytes;
  // Unique per layout made: a draw bucket built from it remembers it.
  U64 stamp;
};

#define UISHELL_RT_LAYOUT_SLOTS 4096
// Laid-out Blocks kept, by their blobs' class bytes.
#define UISHELL_RT_LAYOUT_BUDGET MB(24)
// A table View's cell layouts (uishell_rt_cell_text_frame).
#define UISHELL_RT_TABLE_CELL_BUDGET MB(4)

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
uishell_rt_layout_hash(U64 key, U64 version, U32 width_bits, U64 epoch, U64 metrics_hash, U64 label_hash, U32 max_lines)
{
  U64 fields[] = {key, version, width_bits, epoch, metrics_hash, label_hash, max_lines};
  return XXH3_64bits(fields, sizeof(fields));
}

internal void
uishell_rt_layout_evict(UIShell_RtLayoutCache *cache, UIShell_RtLayoutEntry *e)
{
  U64 hash = uishell_rt_layout_hash(e->key, e->version, e->width_bits, e->epoch, e->metrics_hash, e->label_hash, e->max_lines);
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

// The Block's layout at text width `width`, from the cache or laid out now;
// cut to `max_lines` with an ellipsis, unless 0.
internal UIShell_RtLayoutEntry *
uishell_rt_layout(UIShell_RtLayoutCache *cache, UIShell_RtTheme *t, UIShell_RtNode *n, UIShell_RtFrame *frame, F32 width, U32 max_lines)
{
  WhText *ctx = uishell_rt_text();
  U64 epoch = wh_text_epoch(ctx);
  width = Max(width, 1.f);
  U32 width_bits = 0;
  MemoryCopy(&width_bits, &width, sizeof(width_bits));
  U64 label_hash = frame->label.size ? XXH3_64bits(frame->label.str, frame->label.size) : 0;
  U64 hash = uishell_rt_layout_hash(n->key, n->version, width_bits, epoch, t->metrics_hash, label_hash, max_lines);
  UIShell_RtLayoutEntry **slot = &cache->slots[hash % UISHELL_RT_LAYOUT_SLOTS];
  for(UIShell_RtLayoutEntry *e = *slot; e != 0; e = e->hash_next)
  {
    if(e->key == n->key && e->version == n->version && e->width_bits == width_bits && e->epoch == epoch &&
       e->metrics_hash == t->metrics_hash && e->label_hash == label_hash && e->max_lines == max_lines)
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
  input.align = n->kind == WHEELHOUSE_RICH_KIND_DIVIDER ? WH_TEXT_ALIGN_CENTER : WH_TEXT_ALIGN_START;
  input.max_lines = max_lines;
  input.ellipsis = max_lines != 0;
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
    e->max_lines = max_lines;
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

// The cell painter's layouts and theme (UIShell_RtCellText is declared in
// uishell_table.c). Rich Text's table Embeds share their View's; the table
// View keeps its own (uishell_rt_cell_text_frame).
struct UIShell_RtCellText
{
  UIShell_RtLayoutCache *layouts;
  UIShell_RtTheme *theme;
};

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
  // The text line and digit width the table was last sized for, which a
  // zoom changes.
  F32 line;
  F32 digit;
  B32 expanded;
  Vec2F32 last_pointer;
  B32 pointer_inside;
  U64 last_frame;
};

// A code Embed (#607; #622): the text view's lines over the Block's text,
// which is in the content cache under its hash. Kept while its Block lives,
// with what the code view keeps: its cursor, selection and scroll.
typedef struct UIShell_RtCode UIShell_RtCode;
struct UIShell_RtCode
{
  UIShell_RtCode *next;
  U64 key;
  U64 version;
  U128 hash;
  C_Key content;
  TXT_LangKind fence_lang;
  // A language chosen for this Block instead of its fence's.
  B32 has_override;
  TXT_LangKind lang_override;
  B32 expanded;
  U64 line_count;
  Vec2F32 scroll;
  UIShell_TextViewState tv;
  B32 contain_cursor;
  // What the last frame built: its lines, and the content's size.
  Rng1S64 lines_built;
  B32 line_numbers;
  Vec2F32 content_dim;
  F32 text_x;
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
  // A Section's header: it toggles the Section, or opens its thread.
  UIShell_RtStop_Toggle,
  UIShell_RtStop_Thread,
  UIShell_RtStop_Open,
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

// A Container's chrome column this frame, memoized: a bubble's fit to its
// content takes laying its Blocks out.
typedef struct UIShell_RtColumn UIShell_RtColumn;
struct UIShell_RtColumn
{
  U64 key;
  U64 stamp;
  Rng1F32 x;
};
#define UISHELL_RT_COLUMN_SLOTS 1024


// A prompt sent to the producer (#683): sending until the producer accepts
// it, then waiting until it shows in the conversation under its id.
#define UISHELL_RT_PENDING_MAX 8
typedef struct UIShell_RtPending UIShell_RtPending;
struct UIShell_RtPending
{
  char id[32];
  U64 ticket;
  U64 sent_us;
  U8 text[4096];
  U64 text_size;
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
  // Its table Embeds' cell painter: these layouts and theme.
  UIShell_RtCellText cell_text;
  UIShell_RtBucket *buckets;
  UIShell_RtBucket *free_bucket;
  UIShell_RtTable *tables;
  UIShell_RtCode *codes;
  C_Root code_root;
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
  // Which side of caret_byte the caret shows on, where its two sides
  // differ on screen (at a change of direction).
  B32 caret_upstream;
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
  // A press on a code Embed's "open" control: a click opens a new View, a
  // drag carries one out.
  U64 open_press;
  Vec2F32 open_press_pos;
  UIShell_RtStats stats;
  B32 show_copied;
  U64 copied_key;
  U64 copied_until_us;
  // A followed link's missing file, named in the bar until notice_until_us.
  U8 notice[256];
  U64 notice_size;
  U64 notice_until_us;
  // A conversation (#616), and the one thread it shows if a subagent's.
  B32 conversation;
  // On `arena`, whole: an id is the file's, of any length.
  String8 thread;
  // Copy All waits for backfill to finish, a step a frame.
  B32 copy_all_pending;
  // The hovered Container's copy actions, as of this generation.
  U64 actions_key;
  U64 actions_generation;
  U32 actions_mask;
  // The Container Style set it draws with, and where its column is.
  UIShell_RtStyleSet *styles;
  F32 content_x;
  UIShell_RtColumn *columns;
  U64 column_stamp;
  // Capped Blocks opened to their whole: a shell call's output by its
  // Section's key, a diff by its own.
  U64 *opened;
  U64 opened_count;
  U64 opened_cap;
  // Containers already collapsed by default, so a later toggle holds.
  U64 *defaulted;
  U64 defaulted_cap;
  U64 defaulted_count;
  // The Container whose hover actions show, and the action last copied.
  U64 hover_key;
  U32 copied_action;
  // When the conversation was opened, and when its tail first painted.
  U64 opened_us;
  U64 first_tail_us;
  // A conversation follows its file as it grows (#617): the bytes of it
  // the core has, and which file they came from.
  U64 fed;
  FileID file_id;
  // The file as it was when opening it failed.
  U64 failed_size;
  DenseTime failed_modified;
  // A watched agent thread (#699): the watch, and the Local Endpoint's
  // socket and thread its View settings name. Its composer posts to the
  // thread's routes there; `input` is empty while the thread takes none.
  WheelhouseAgentWatch *watch;
  U8 agent_endpoint[512];
  U64 agent_endpoint_size;
  String8 agent_thread;
  U64 agent_version;
  U8 input[512];
  U64 input_size;
  // The composer's draft.
  U8 compose[4096];
  U64 compose_size;
  TxtPt compose_cursor;
  TxtPt compose_mark;
  // A press on the content leaves the composer, on the next build.
  B32 compose_blur;
  // The first line it shows, past 8; and the column Up and Down keep to,
  // or -1.
  U64 compose_first_line;
  F32 compose_goal_x;
  // When the cursor last moved: it stays lit a moment, then blinks.
  U64 compose_moved_us;
  // Prompts sent and not yet in the conversation, oldest first.
  UIShell_RtPending pending[UISHELL_RT_PENDING_MAX];
  U64 pending_count;
  // The last prompt sent, for Up in an empty composer.
  U8 last_prompt[4096];
  U64 last_prompt_size;
  // When the open run was first seen running, for its elapsed time.
  U64 running_since_us;
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
  s->codes = 0;
  if(s->code_root.u64[0] != 0) { c_root_release(s->code_root); s->code_root = (C_Root){0}; }
  for(UIShell_RtBucket *b = s->buckets; b != 0; b = b->next) { if(b->arena) { arena_release(b->arena); } }
  for(UIShell_RtBucket *b = s->free_bucket; b != 0; b = b->next) { if(b->arena) { arena_release(b->arena); } }
  s->buckets = s->free_bucket = 0;
  uishell_rt_layout_cache_release(&s->layouts);
  uishell_rt_model_release(&s->model);
  if(s->match_arena) { arena_release(s->match_arena); s->match_arena = 0; }
  if(s->doc) { wheelhouse_rich_text_free(s->doc); s->doc = 0; }
  if(s->watch) { wheelhouse_agent_watch_free(s->watch); s->watch = 0; }
  if(s->arena) { arena_release(s->arena); s->arena = 0; }
  for(U64 i = 0; i < s->pending_count; i += 1) { wheelhouse_agent_input_forget(s->pending[i].ticket); }
  s->pending_count = 0;
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
//~ Collapsing, and opening a thread

// Whether `key` is newly in the set of Containers already collapsed by
// default. A doubling leaves the old table in `arena`: a geometric series,
// so under twice what the last table takes.
internal B32
uishell_rt_defaulted_insert(UIShell_RichTextState *s, U64 key)
{
  if(s->defaulted_count + 1 > s->defaulted_cap/2)
  {
    U64 cap = Max(1024, s->defaulted_cap*2);
    U64 *keys = push_array(s->arena, U64, cap);
    for(U64 i = 0; i < s->defaulted_cap; i += 1)
    {
      U64 k = s->defaulted[i];
      if(k == 0) { continue; }
      U64 at = k & (cap-1);
      while(keys[at] != 0) { at = (at + 1) & (cap-1); }
      keys[at] = k;
    }
    s->defaulted = keys;
    s->defaulted_cap = cap;
  }
  U64 k = key ? key : 1, at = k & (s->defaulted_cap-1);
  for(; s->defaulted[at] != 0; at = (at + 1) & (s->defaulted_cap-1))
  {
    if(s->defaulted[at] == k) { return 0; }
  }
  s->defaulted[at] = k;
  s->defaulted_count += 1;
  return 1;
}

// A Container its style collapses by default is collapsed the first time
// the View sees it; after that it stays as toggled.
internal void
uishell_rt_collapse_default(UIShell_RichTextState *s, UIShell_RtNode *n)
{
  UIShell_RtContainerStyle *st = uishell_rt_container_style(s->styles, n);
  if(st && (st->chrome & UIShell_RtChrome_Collapsed) && uishell_rt_defaulted_insert(s, n->key))
  {
    char *error = 0;
    wheelhouse_rich_text_set_collapsed(s->doc, s->sub, n->key, 1, &error);
    uishell_rt_error(&error);
  }
}

internal void
uishell_rt_collapse_defaults(UIShell_RichTextState *s)
{
  for(U64 i = 0; i < s->model.slots_count; i += 1)
  {
    for(UIShell_RtNode *n = s->model.slots[i]; n != 0; n = n->hash_next) { uishell_rt_collapse_default(s, n); }
  }
}

internal void
uishell_rt_toggle(UIShell_RichTextState *s, U64 key)
{
  char *error = 0;
  U32 collapsed = 0;
  if(wheelhouse_rich_text_collapsed(s->doc, s->sub, key, &collapsed, &error))
  {
    wheelhouse_rich_text_set_collapsed(s->doc, s->sub, key, !collapsed, &error);
  }
  uishell_rt_error(&error);
}

internal B32
uishell_rt_collapsed(UIShell_RichTextState *s, U64 key)
{
  char *error = 0;
  U32 collapsed = 0;
  wheelhouse_rich_text_collapsed(s->doc, s->sub, key, &collapsed, &error);
  uishell_rt_error(&error);
  return collapsed;
}

// A subagent's thread opens as a View of its own (#616): this View again,
// with the thread as View state, beside it.
internal void
uishell_rt_open_thread(RD_ViewAddress view, String8 thread)
{
  CFG_Node *src = rd_cfg_from_view(view);
  if(src == &cfg_nil_node || thread.size == 0) { return; }
  RD_PanelAddress panel = rd_panel_from_cfg(src);
  CFG_Node *dst = cfg_node_deep_copy(rd_state->cfg, src);
  cfg_node_insert_child(rd_state->cfg, src->parent, src, dst);
  rd_view_key_assign(dst, 1);
  cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string_or_alloc(rd_state->cfg, dst, str8_lit("thread")), thread);
  RD_ViewAddress opened = rd_view_address_from_cfg(dst);
  WheelhouseArrangementIntent intent = rd_arrangement_intent(WHEELHOUSE_INTENT_MOVE_TAB, panel.owner);
  intent.panel = panel.id;
  rd_arrangement_intent_tab(&intent, opened);
  rd_arrangement_intent_after(&intent, view);
  if(!rd_arrangement_apply_intent(intent).applied) { cfg_node_release(rd_state->cfg, dst); return; }
  UIShell_RegsScope(.tab = opened)
  {
    uishell_push_cmd_current(str8_lit("focus_tab"));
  }
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
    t->line = line;
    t->digit = digit;
    scratch_end(scratch);
  }
  else if(t->line != line || t->digit != digit)
  {
    wheelhouse_table_set_metrics(t->table, line, digit);
    t->line = line;
    t->digit = digit;
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
//~ Code Embeds (#607; #622)

// A fence's info string to a lexer: these names, then the extension table;
// anything else is plain.
internal TXT_LangKind
uishell_rt_lang_from_fence(String8 info)
{
  read_only local_persist struct { String8 name; TXT_LangKind kind; } aliases[] =
  {
    {str8_lit_comp("rs"), TXT_LangKind_Rust}, {str8_lit_comp("rust"), TXT_LangKind_Rust},
    {str8_lit_comp("c"), TXT_LangKind_C}, {str8_lit_comp("h"), TXT_LangKind_C},
    {str8_lit_comp("cpp"), TXT_LangKind_CPlusPlus}, {str8_lit_comp("c++"), TXT_LangKind_CPlusPlus},
    {str8_lit_comp("cc"), TXT_LangKind_CPlusPlus}, {str8_lit_comp("cxx"), TXT_LangKind_CPlusPlus},
    {str8_lit_comp("zig"), TXT_LangKind_Zig}, {str8_lit_comp("odin"), TXT_LangKind_Odin}, {str8_lit_comp("jai"), TXT_LangKind_Jai},
  };
  for EachElement(i, aliases)
  {
    if(str8_match(info, aliases[i].name, StringMatchFlag_CaseInsensitive)) { return aliases[i].kind; }
  }
  return txt_lang_kind_from_extension(info);
}

internal UIShell_RtCode *
uishell_rt_code_of(UIShell_RichTextState *s, U64 key)
{
  for(UIShell_RtCode *c = s->codes; c != 0; c = c->next) { if(c->key == key) { return c; } }
  return 0;
}

internal TXT_LangKind
uishell_rt_code_lang(UIShell_RtCode *c)
{
  return c->has_override ? c->lang_override : c->fence_lang;
}

internal U64
uishell_rt_code_line_count(String8 text)
{
  U64 lines = 1;
  for(U64 i = 0; i < text.size; i += 1) { lines += text.str[i] == '\n'; }
  return lines;
}

// The Block's embed, its text in the content cache keyed by its hash: a
// Block still streaming, its fence open, moves to a new key as text arrives.
internal UIShell_RtCode *
uishell_rt_code(UIShell_RichTextState *s, UIShell_RtNode *n)
{
  UIShell_RtCode *c = uishell_rt_code_of(s, n->key);
  if(c == 0)
  {
    c = push_array(s->arena, UIShell_RtCode, 1);
    c->key = n->key;
    c->tv.cursor = c->tv.mark = txt_pt(1, 1);
    c->tv.preferred_column = 1;
    SLLStackPush(s->codes, c);
  }
  if(c->version != n->version || c->content.root.u64[0] == 0)
  {
    U128 hash = u128_hash_from_str8(n->text);
    if(!u128_match(hash, c->hash) || c->content.root.u64[0] == 0)
    {
      if(s->code_root.u64[0] == 0) { s->code_root = c_root_alloc(); }
      if(c->content.root.u64[0] != 0) { c_close_key(c->content); }
      // Per Block: Blocks with the same text don't share a key, which
      // closing one would close for both.
      c->content = c_key_make(s->code_root, c_id_make(n->key, hash.u64[0]));
      Arena *data_arena = arena_alloc();
      c->hash = c_submit_data(c->content, &data_arena, push_str8_copy(data_arena, n->text));
    }
    c->fence_lang = uishell_rt_lang_from_fence(n->language);
    c->line_count = uishell_rt_code_line_count(n->text);
    c->version = n->version;
  }
  return c;
}

internal void
uishell_rt_code_set_lang(UIShell_RichTextState *s, U64 key, TXT_LangKind lang)
{
  UIShell_RtNode *n = uishell_rt_node(&s->model, key);
  if(n == 0 || !uishell_rt_code_embedded(n)) { return; }
  UIShell_RtCode *c = uishell_rt_code(s, n);
  c->has_override = 1;
  c->lang_override = lang;
}

// The embeds whose Blocks have gone go with them, and their content keys.
internal void
uishell_rt_prune_codes(UIShell_RichTextState *s)
{
  for(UIShell_RtCode **at = &s->codes; *at != 0;)
  {
    UIShell_RtNode *n = uishell_rt_node(&s->model, (*at)->key);
    if(n == 0 || !uishell_rt_code_embedded(n))
    {
      if((*at)->content.root.u64[0] != 0) { c_close_key((*at)->content); }
      if(s->focus_embed == (*at)->key) { s->focus_embed = 0; }
      if(s->open_press == (*at)->key) { s->open_press = 0; }
      *at = (*at)->next;
    }
    else { at = &(*at)->next; }
  }
}

// Shown at most this many lines inline, unless expanded.
#define UISHELL_RT_CODE_INLINE_LINES 30

// The code's own height, from its text so it holds before the lexer is
// done, and whether the cap cut it (so it offers "expand").
internal F32
uishell_rt_code_height(UIShell_RtCode *c, B32 *capped_out)
{
  U64 lines = c->line_count;
  B32 expanded = c->expanded;
  *capped_out = lines > UISHELL_RT_CODE_INLINE_LINES;
  return uishell_text_row_height()*(expanded ? lines : Min(lines, UISHELL_RT_CODE_INLINE_LINES));
}

// The text view's columns count bytes, from 1.
internal TxtPt
uishell_rt_pt_from_byte(String8 text, U64 byte)
{
  TxtPt pt = txt_pt(1, 1);
  U64 line_start = 0;
  for(U64 i = 0; i < Min(byte, text.size); i += 1)
  {
    if(text.str[i] == '\n') { pt.line += 1; line_start = i+1; }
  }
  pt.column = (S64)(Min(byte, text.size) - line_start) + 1;
  return pt;
}

internal U64
uishell_rt_byte_from_pt(String8 text, TxtPt pt)
{
  U64 byte = 0;
  for(S64 line = 1; line < pt.line && byte < text.size; byte += 1) { line += text.str[byte] == '\n'; }
  return Min(byte + (U64)Max(pt.column - 1, 0), text.size);
}

// Find hands a match in a code Embed to the code view: its selection on
// the match, scrolled to it, and its own highlighting of the query.
internal void
uishell_rt_code_show_match(UIShell_RichTextState *s, U64 key, U32 start, U32 end)
{
  UIShell_RtNode *n = uishell_rt_node(&s->model, key);
  if(n == 0 || !uishell_rt_code_embedded(n)) { return; }
  UIShell_RtCode *c = uishell_rt_code(s, n);
  c->tv.mark = uishell_rt_pt_from_byte(n->text, start);
  c->tv.cursor = uishell_rt_pt_from_byte(n->text, end);
  c->tv.preferred_column = c->tv.cursor.column;
  c->contain_cursor = 1;
}

internal String8 uishell_rt_copy_block(Arena *arena, UIShell_RichTextState *s, U64 key, U32 format);

// The copy hook (#542, decision 7): what an Embed gives to copy. A code
// Block gives its text, or, unless `whole`, what is selected inside it
// while it has focus; as markdown, its fence with its language. The core
// gives the rest.
internal String8
uishell_rt_embed_copy(Arena *arena, UIShell_RichTextState *s, U64 key, U32 format, B32 whole)
{
  UIShell_RtNode *n = uishell_rt_node(&s->model, key);
  if(n && uishell_rt_code_embedded(n) && format == WHEELHOUSE_RICH_COPY_PLAIN)
  {
    UIShell_RtCode *c = uishell_rt_code(s, n);
    if(!whole && s->focus_embed == key && !txt_pt_match(c->tv.cursor, c->tv.mark))
    {
      TxtRng r = txt_rng(c->tv.cursor, c->tv.mark);
      U64 a = uishell_rt_byte_from_pt(n->text, r.min), b = uishell_rt_byte_from_pt(n->text, r.max);
      return push_str8_copy(arena, str8_substr(n->text, r1u64(a, b)));
    }
    return push_str8_copy(arena, n->text);
  }
  return uishell_rt_copy_block(arena, s, key, format);
}

// The code a creation drag carries to where it drops: drag_drop_commit
// takes nothing of the drag's own. One drag at a time (rd_drag_begin starts
// none while one is active), so one copy serves, its arena kept for the
// process.
global Arena *uishell_rt_code_drag_arena = 0;
global String8 uishell_rt_code_drag_text = {0};
global TXT_LangKind uishell_rt_code_drag_lang = TXT_LangKind_Null;

// "Open in a new View" (#542, decision 10): a text View tab in `panel`
// showing the code by value, as content parsed from the Rich Text is.
internal RD_ViewAddress
uishell_rt_code_open_in(RD_PanelAddress panel, String8 text, TXT_LangKind lang)
{
  RD_ViewAddress tab = rd_nil_view_address;
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *arrangement = rd_arrangement_from_panel_owner(scratch.arena, panel.owner);
  if(rd_arrangement_panel_from_id(arrangement, panel.id) != &rd_nil_arrangement_panel)
  {
    tab = rd_new_view_tab(arrangement, panel.id, str8_lit("text"), str8_zero());
    rd_view_setting_set(tab, str8_lit("text"), text);
    if(lang != TXT_LangKind_Null) { rd_view_setting_set(tab, str8_lit("lang"), txt_extension_from_lang_kind(lang)); }
    rd_arrangement_set(arrangement);
  }
  scratch_end(scratch);
  return tab;
}

internal void
uishell_rt_code_open(UIShell_RichTextState *s, U64 key)
{
  UIShell_RtNode *n = uishell_rt_node(&s->model, key);
  if(n == 0 || !uishell_rt_code_embedded(n)) { return; }
  RD_ViewAddress tab = uishell_rt_code_open_in(uishell_regs()->panel, n->text, uishell_rt_code_lang(uishell_rt_code(s, n)));
  if(!rd_view_address_is_nil(tab)) { uishell_cmd("focus_tab", .view = tab, .tab = tab, .panel = uishell_regs()->panel); }
}

internal void
uishell_rt_code_drop(RD_PanelAddress destination, Dir2 direction, RD_ViewAddress previous_tab)
{
  (void)previous_tab;
  RD_ViewAddress tab = uishell_rt_code_open_in(destination, uishell_rt_code_drag_text, uishell_rt_code_drag_lang);
  if(!rd_view_address_is_nil(tab) && direction != Dir2_Invalid)
  {
    uishell_cmd("split_panel", .dst_panel = destination, .panel = destination, .view = tab, .dir2 = direction);
  }
}

internal void
uishell_rt_code_drag_begin(UIShell_RichTextState *s, U64 key)
{
  UIShell_RtNode *n = uishell_rt_node(&s->model, key);
  if(n == 0 || rd_drag_is_active()) { return; }
  if(uishell_rt_code_drag_arena == 0) { uishell_rt_code_drag_arena = arena_alloc(); }
  arena_clear(uishell_rt_code_drag_arena);
  uishell_rt_code_drag_text = push_str8_copy(uishell_rt_code_drag_arena, n->text);
  uishell_rt_code_drag_lang = uishell_rt_code_lang(uishell_rt_code(s, n));
  rd_drag_begin(UIShell_ContextRegSlot_View);
  rd_state->drag_drop_creation_name = str8_lit("text");
  rd_state->drag_drop_commit = uishell_rt_code_drop;
}

////////////////////////////////
//~ Measuring

internal F32
uishell_rt_text_width(UIShell_RtFrame *f, F32 content_width)
{
  return Max(content_width - f->indent - 2*f->inset_x, 8.f);
}

// A shell call's output shows this many lines inline (#667), unless opened.
#define UISHELL_RT_TERMINAL_INLINE_LINES 15

internal B32
uishell_rt_opened(UIShell_RichTextState *s, U64 key)
{
  for(U64 i = 0; i < s->opened_count; i += 1) { if(s->opened[i] == key) { return 1; } }
  return 0;
}

internal void
uishell_rt_toggle_opened(UIShell_RichTextState *s, U64 key)
{
  for(U64 i = 0; i < s->opened_count; i += 1)
  {
    if(s->opened[i] == key) { s->opened[i] = s->opened[--s->opened_count]; return; }
  }
  if(s->opened_count == s->opened_cap)
  {
    U64 cap = Max(16, s->opened_cap*2);
    U64 *keys = push_array(s->arena, U64, cap);
    MemoryCopy(keys, s->opened, sizeof(U64)*s->opened_count);
    s->opened = keys;
    s->opened_cap = cap;
  }
  s->opened[s->opened_count++] = key;
}

// What a capped Block opens with: its shell call's Section, or a diff
// itself; 0 for a Block without a cap.
internal U64
uishell_rt_cap_owner(UIShell_RichTextState *s, UIShell_RtNode *n)
{
  if(uishell_rt_terminal(&s->model, n, 0, 0)) { return n->parent; }
  if(uishell_rt_is_diff(n)) { return n->key; }
  return 0;
}

typedef struct UIShell_RtCap UIShell_RtCap;
struct UIShell_RtCap
{
  U64 owner;
  B32 open;
  // A shell output's later Log Block while its first is capped.
  B32 hidden;
  // Lines shown, or 0 for all of them.
  U32 max_lines;
  // "Expand" or "Collapse", on a row under its text.
  B32 control;
};

internal UIShell_RtCap
uishell_rt_cap(UIShell_RichTextState *s, UIShell_RtNode *n, UIShell_RtFrame *f, F32 content_width)
{
  UIShell_RtCap c = {0};
  c.owner = uishell_rt_cap_owner(s, n);
  if(c.owner == 0) { return c; }
  U64 index = 0, count = 1;
  U32 lines = f->terminal ? UISHELL_RT_TERMINAL_INLINE_LINES : UISHELL_RT_CODE_INLINE_LINES;
  if(f->terminal) { uishell_rt_terminal(&s->model, n, &index, &count); }
  c.open = uishell_rt_opened(s, c.owner);
  B32 longer = count > 1;
  if(!longer)
  {
    UIShell_RtLayoutEntry *full = uishell_rt_layout(&s->layouts, &s->theme, n, f, uishell_rt_text_width(f, content_width), 0);
    longer = full && full->layout->text.line_count > lines;
  }
  c.hidden = !c.open && index > 0;
  c.max_lines = !c.open && longer ? lines : 0;
  c.control = longer && (c.open ? index + 1 == count : index == 0);
  return c;
}

// A Block's height at content width `content_width`, laying it out if it
// isn't already: what set_height reports.
internal F32
uishell_rt_block_height(UIShell_RichTextState *s, UIShell_RtNode *n, F32 content_width, UIShell_RtLayoutEntry **layout_out)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_RtFrame f = uishell_rt_frame(scratch.arena, &s->model, &s->theme, s->styles, n);
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
  else if(f.code_embed)
  {
    B32 capped = 0;
    inner = uishell_rt_code_height(uishell_rt_code(s, n), &capped);
  }
  else
  {
    UIShell_RtCap cap = uishell_rt_cap(s, n, &f, content_width);
    if(cap.hidden) { scratch_end(scratch); return 0; }
    UIShell_RtLayoutEntry *e = uishell_rt_layout(&s->layouts, &s->theme, n, &f, uishell_rt_text_width(&f, content_width), cap.max_lines);
    if(e) { inner = e->layout->text.height; }
    if(cap.control) { inner += uishell_rt_line_px(&s->theme); }
    if(layout_out) { *layout_out = e; }
  }
  F32 height = ceil_f32(f.pad_top + 2*f.inset_y + inner + f.pad_bottom);
  scratch_end(scratch);
  return height;
}

////////////////////////////////
//~ Columns: where Container Styles put Blocks

internal F32
uishell_rt_em(UIShell_RtTheme *t, F32 ems)
{
  return floor_f32(uishell_rt_px(t)*ems);
}

internal F32
uishell_rt_header_row(UIShell_RtTheme *t)
{
  return floor_f32(uishell_rt_line_px(t)*1.35f);
}

internal F32
uishell_rt_action_row(UIShell_RtTheme *t)
{
  return floor_f32(uishell_rt_line_px(t)*1.3f);
}

// A Container's header and footer rows: its top inset and header, and its
// bottom inset, hover actions and gap. Without a style, none.
internal F32
uishell_rt_header_height(UIShell_RtTheme *t, UIShell_RtContainerStyle *st)
{
  if(st == 0) { return 0; }
  return uishell_rt_em(t, st->inset_top) + ((st->chrome & UIShell_RtChrome_Header) ? uishell_rt_header_row(t) : 0);
}

internal F32
uishell_rt_footer_height(UIShell_RtTheme *t, UIShell_RtContainerStyle *st)
{
  if(st == 0) { return 0; }
  B32 action_row = st->actions != 0 && !(st->chrome & UIShell_RtChrome_Header);
  return uishell_rt_em(t, st->inset_bottom) + (action_row ? uishell_rt_action_row(t) : 0) + uishell_rt_em(t, st->gap);
}

// The nearest styled Container holding `n`, not `n` itself.
internal UIShell_RtNode *
uishell_rt_styled_parent(UIShell_RichTextState *s, UIShell_RtNode *n, UIShell_RtContainerStyle **style_out)
{
  for(UIShell_RtNode *a = uishell_rt_node(&s->model, n->parent); a != 0 && a->key != WHEELHOUSE_RICH_ROOT_KEY; a = uishell_rt_node(&s->model, a->parent))
  {
    UIShell_RtContainerStyle *st = uishell_rt_container_style(s->styles, a);
    if(st) { *style_out = st; return a; }
  }
  return 0;
}

internal Rng1F32 uishell_rt_chrome_column(UIShell_RichTextState *s, UIShell_RtNode *c, UIShell_RtContainerStyle *st);

// A sent prompt's bubble, which the composer's card matches.
internal Vec4F32
uishell_rt_bubble_color(UIShell_RtTheme *t)
{
  return mix_4f32(t->background, t->text, 0.07f);
}

// A pending prompt's bubble: fainter than a sent one's.
internal Vec4F32
uishell_rt_pending_wash(UIShell_RtTheme *t, F32 alpha)
{
  Vec4F32 color = mix_4f32(t->background, t->text, 0.035f);
  color.w = alpha;
  return color;
}

// Whether a Block is in a prompt sent from this View that the producer
// hasn't echoed yet.
internal B32
uishell_rt_in_pending_turn(UIShell_RichTextState *s, UIShell_RtNode *n)
{
  UIShell_RtContainerStyle *st = 0;
  for(UIShell_RtNode *c = uishell_rt_styled_parent(s, n, &st); c != 0; c = uishell_rt_styled_parent(s, c, &st))
  {
    if(c->kind == WHEELHOUSE_RICH_KIND_TURN) { return c->status == WHEELHOUSE_RICH_STATUS_PENDING; }
  }
  return 0;
}

// The column `n` sits in, in the content box: its styled Container's
// inside, or the View's reading column.
internal Rng1F32
uishell_rt_column(UIShell_RichTextState *s, UIShell_RtNode *n)
{
  UIShell_RtContainerStyle *st = 0;
  UIShell_RtNode *c = uishell_rt_styled_parent(s, n, &st);
  if(c == 0) { return rng_1f32(s->content_x, s->content_x + s->width); }
  Rng1F32 chrome = uishell_rt_chrome_column(s, c, st);
  F32 inset = uishell_rt_em(&s->theme, st->inset_x);
  F32 x0 = chrome.min + inset, x1 = Max(chrome.max - inset, x0 + 8.f);
  return rng_1f32(x0, x1);
}

// How wide a capped Container's content wants to be, up to `cap`: its
// widest line, or all of `cap` for what fills its width (code, tables) or
// too many Blocks to measure.
internal F32
uishell_rt_fit_width(UIShell_RichTextState *s, UIShell_RtNode *c, UIShell_RtContainerStyle *st, F32 cap)
{
  F32 inset = 2*uishell_rt_em(&s->theme, st->inset_x), inner = Max(cap - inset, 8.f), widest = 0;
  Temp scratch = scratch_begin(0, 0);
  U64 stack[64], depth = 0, blocks = 0;
  stack[depth++] = c->key;
  B32 full = 0;
  while(depth > 0 && !full)
  {
    UIShell_RtNode *n = uishell_rt_node(&s->model, stack[--depth]);
    if(n == 0 || n->hidden) { continue; }
    if(n->kind < WHEELHOUSE_RICH_KIND_PARAGRAPH)
    {
      if(depth + n->child_count > ArrayCount(stack)) { full = 1; break; }
      for(U64 i = n->child_count; i > 0; i -= 1) { stack[depth++] = n->children[i-1]; }
      continue;
    }
    if(++blocks > 32) { full = 1; break; }
    UIShell_RtFrame f = uishell_rt_frame(scratch.arena, &s->model, &s->theme, s->styles, n);
    if(f.code || f.table || f.rule || f.divider) { full = 1; break; }
    UIShell_RtLayoutEntry *e = uishell_rt_layout(&s->layouts, &s->theme, n, &f, uishell_rt_text_width(&f, inner), 0);
    if(e == 0) { full = 1; break; }
    for(U64 i = 0; i < e->layout->text.line_count; i += 1)
    {
      WhTextLine *line = &e->layout->text.lines[i];
      widest = Max(widest, f.indent + 2*f.inset_x + line->x + line->width);
    }
  }
  scratch_end(scratch);
  return full ? cap : Min(cap, ceil_f32(widest) + inset + 1.f);
}

// Where a styled Container's chrome goes across: its column, capped at its
// share and aligned.
internal Rng1F32
uishell_rt_chrome_column(UIShell_RichTextState *s, UIShell_RtNode *c, UIShell_RtContainerStyle *st)
{
  UIShell_RtColumn *memo = 0;
  if(s->columns)
  {
    for(U64 probe = 0; probe < 8 && memo == 0; probe += 1)
    {
      UIShell_RtColumn *slot = &s->columns[(c->key + probe) & (UISHELL_RT_COLUMN_SLOTS-1)];
      if(slot->key == c->key && slot->stamp == s->column_stamp) { return slot->x; }
      if(slot->stamp != s->column_stamp) { memo = slot; }
    }
  }
  Rng1F32 outer = uishell_rt_column(s, c);
  F32 w = dim_1f32(outer);
  if(st->max_width > 0)
  {
    F32 cap = floor_f32(w*st->max_width);
    w = uishell_rt_fit_width(s, c, st, cap);
  }
  Rng1F32 x = st->align == UIShell_RtAlign_End ? rng_1f32(outer.max - w, outer.max) : rng_1f32(outer.min, outer.min + w);
  if(memo) { *memo = (UIShell_RtColumn){c->key, s->column_stamp, x}; }
  return x;
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
    if(washes && (ink.ink & UIShell_RtInk_Wash))
    {
      dr_rect(r2f32p(x0, origin.y + line->y, x1, origin.y + line->y + line->height), uishell_rt_literal_color(t, ink.background), 0, 0, 0);
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

internal F32
uishell_rt_cell_pad(UIShell_RtTheme *t)
{
  return floor_f32(uishell_rt_px(t)*0.4f);
}

// A table cell's text (#545): laid out as Rich Text is, so with fontique's
// fallback, on one line cut with an ellipsis. Keyed by its text: equal
// cells share a layout. Right-aligned cells are figures, in tabular ones.
internal UIShell_RtLayoutEntry *
uishell_rt_cell_layout(UIShell_RtLayoutCache *layouts, UIShell_RtTheme *t, String8 text, F32 cell_width, B32 right)
{
  UIShell_RtNode n = {0};
  n.kind = WHEELHOUSE_RICH_KIND_PARAGRAPH;
  n.tabular = !!right;
  n.key = XXH3_64bits_withSeed(text.str, text.size, n.tabular);
  n.text = text;
  UIShell_RtFrame f = {0};
  return uishell_rt_layout(layouts, t, &n, &f, cell_width - 2*uishell_rt_cell_pad(t), 1);
}

// Paints a table's cell and header text: the table View's, and a table
// Embed's.
internal void
uishell_rt_table_cell_text(void *user, UI_Box *box, Rng2F32 cell, Rng2F32 clip, String8 text, B32 right, Vec4F32 color)
{
  UIShell_RtCellText *ct = (UIShell_RtCellText *)user;
  Rng2F32 visible = intersect_2f32(cell, clip);
  if(text.size == 0 || visible.x1 <= visible.x0 || visible.y1 <= visible.y0) { return; }
  UIShell_RtTheme *t = ct->theme;
  F32 pad = uishell_rt_cell_pad(t);
  UIShell_RtLayoutEntry *e = uishell_rt_cell_layout(ct->layouts, t, text, dim_2f32(cell).x, right);
  if(e == 0 || e->layout->text.line_count == 0) { return; }
  WhTextLayout *l = &e->layout->text;
  WhTextLine *line = &l->lines[0];
  F32 x = right ? Max(cell.x0 + pad, cell.x1 - pad - (line->x + line->width)) : cell.x0 + pad;
  Vec2F32 origin = v2f32(floor_f32(x), floor_f32(cell.y0 + (dim_2f32(cell).y - line->height)/2 - line->y));
  UIShell_RtBucket pages = {0};
  DR_ClipScope(intersect_2f32(dr_top_clip(), visible))
  {
    if(!uishell_rt_draw_glyphs(&pages, t, l, 0, 0, origin, &color)) { rd_request_frame(); }
  }
}

// The table View's own painter: made on its first frame, its theme taken
// again each frame at the View's font size, which a zoom changes.
typedef struct UIShell_RtCellTextStore UIShell_RtCellTextStore;
struct UIShell_RtCellTextStore
{
  UIShell_RtCellText text;
  Arena *arena;
  UIShell_RtLayoutCache layouts;
  UIShell_RtTheme theme;
};

internal UIShell_RtCellText *
uishell_rt_cell_text_frame(UIShell_RtCellText *ct, F32 font_size)
{
  if(ct == 0)
  {
    Arena *arena = arena_alloc(.reserve_size = KB(128), .commit_size = KB(64), .name = "table cell text");
    UIShell_RtCellTextStore *store = push_array(arena, UIShell_RtCellTextStore, 1);
    store->arena = arena;
    // A screen of cells, not a document's Blocks: one table View among
    // several keeps a smaller cache than a Rich Text View's.
    store->layouts.budget = UISHELL_RT_TABLE_CELL_BUDGET;
    store->text = (UIShell_RtCellText){&store->layouts, &store->theme};
    ct = &store->text;
  }
  *ct->theme = uishell_rt_theme_sized(font_size);
  return ct;
}

internal void
uishell_rt_cell_text_release(UIShell_RtCellText *ct)
{
  if(ct == 0) { return; }
  UIShell_RtCellTextStore *store = (UIShell_RtCellTextStore *)ct;
  uishell_rt_layout_cache_release(&store->layouts);
  arena_release(store->arena);
}

// Builds a Block's bucket in its own coordinates: (0, 0) is its box's top
// left, `width` its box's width.
internal void
uishell_rt_build_bucket(UIShell_RichTextState *s, UIShell_RtBucket *b, UIShell_RtNode *n, UIShell_RtLayoutEntry *e, F32 width, F32 height)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_RtTheme *t = &s->theme;
  UIShell_RtFrame f = uishell_rt_frame(scratch.arena, &s->model, &s->theme, s->styles, n);
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
    if(f.code || f.input)
    {
      dr_rect(r2f32p(f.indent, f.pad_top, width, height - f.pad_bottom), t->code_block, 4.f, 0, 1.f);
    }
    if(f.terminal)
    {
      dr_rect(r2f32p(f.indent, 0, width, height), t->terminal, 0, 0, 1.f);
    }
    if(f.divider && e != 0 && e->layout->text.line_count != 0)
    {
      // Rules either side of the centred text.
      WhTextLine *line = &e->layout->text.lines[0];
      F32 gap = floor_f32(uishell_rt_px(t)*0.6f), y = floor_f32(f.pad_top + f.inset_y + line->y + line->height/2);
      F32 x0 = f.indent + f.inset_x + line->x - gap, x1 = f.indent + f.inset_x + line->x + line->width + gap;
      if(x0 > f.indent) { dr_rect(r2f32p(f.indent, y, x0, y + 1.f), t->rule, 0, 0, 0); }
      if(x1 < width) { dr_rect(r2f32p(x1, y, width, y + 1.f), t->rule, 0, 0, 0); }
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
  // A pending prompt's Blocks fade under a wash of its bubble's colour.
  Vec4F32 fade;
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
  if(d->fade.w > 0) { dr_rect(box->rect, d->fade, 0, 0, 0); }
  if(d->focused)
  {
    dr_rect(pad_2f32(box->rect, 1.f), d->focus_color, 3.f, 1.5f, 1.f);
  }
}

// A styled Container's chrome behind its Blocks: its background.
typedef struct UIShell_RtChromeDraw UIShell_RtChromeDraw;
struct UIShell_RtChromeDraw
{
  U64 key;
  Vec4F32 color;
  F32 radius;
};

internal UI_BOX_CUSTOM_DRAW(uishell_rt_chrome_draw)
{
  UIShell_RtChromeDraw *d = (UIShell_RtChromeDraw *)user_data;
  dr_rect(box->rect, d->color, d->radius, 0, 1.f);
}

// A Section's header row: what it shows, for the diagnostics, and its
// hover and focus.
typedef struct UIShell_RtHeaderDraw UIShell_RtHeaderDraw;
struct UIShell_RtHeaderDraw
{
  U64 key;
  U32 section;
  U32 status;
  B32 collapsed;
  B32 hovered;
  B32 focused;
  Vec4F32 wash;
  Vec4F32 focus_color;
};

internal UI_BOX_CUSTOM_DRAW(uishell_rt_header_draw)
{
  UIShell_RtHeaderDraw *d = (UIShell_RtHeaderDraw *)user_data;
  if(d->hovered) { dr_rect(box->rect, d->wash, 4.f, 0, 1.f); }
  if(d->focused) { dr_rect(pad_2f32(box->rect, 1.f), d->focus_color, 3.f, 1.5f, 1.f); }
}

// A header's mark for its kind, in the font it is drawn in. Never a caret,
// which is the chevron's (#667). The icon font has no terminal, so a shell
// call's is a prompt in the code font.
internal String8
uishell_rt_tool_icon(UIShell_RtNode *n, RD_FontSlot *slot_out)
{
  RD_IconKind icon = RD_IconKind_Gear;
  *slot_out = RD_FontSlot_Icons;
  if(n->kind == WHEELHOUSE_RICH_KIND_TOOL_RUN) { icon = RD_IconKind_List; }
  else if(n->section == WHEELHOUSE_RICH_SECTION_REASONING) { icon = RD_IconKind_Glasses; }
  else if(n->section == WHEELHOUSE_RICH_SECTION_SUBAGENT) { icon = RD_IconKind_Thread; }
  else switch(n->tool)
  {
    default:
    case WHEELHOUSE_RICH_TOOL_OTHER: icon = RD_IconKind_Gear; break;
    case WHEELHOUSE_RICH_TOOL_SHELL: { *slot_out = RD_FontSlot_Code; return str8_lit(">_"); }
    case WHEELHOUSE_RICH_TOOL_EDIT: icon = RD_IconKind_Pencil; break;
    case WHEELHOUSE_RICH_TOOL_READ: icon = RD_IconKind_FileOutline; break;
    case WHEELHOUSE_RICH_TOOL_SEARCH: icon = RD_IconKind_Find; break;
    case WHEELHOUSE_RICH_TOOL_AGENT: icon = RD_IconKind_Threads; break;
    case WHEELHOUSE_RICH_TOOL_WEB: icon = RD_IconKind_Binoculars; break;
    case WHEELHOUSE_RICH_TOOL_MCP: icon = RD_IconKind_Module; break;
  }
  return rd_icon_kind_text_table[icon];
}

// Pending shows a spinner and ok shows nothing (#667); this is the mark for
// the rest.
internal RD_IconKind
uishell_rt_status_icon(U32 status)
{
  switch(status)
  {
    default: break;
    case WHEELHOUSE_RICH_STATUS_FAILED:
    case WHEELHOUSE_RICH_STATUS_CANCELLED: return RD_IconKind_X;
    case WHEELHOUSE_RICH_STATUS_WAITING: return RD_IconKind_QuestionMark;
  }
  return RD_IconKind_Null;
}

internal B32
uishell_rt_status_bad(U32 status)
{
  return status == WHEELHOUSE_RICH_STATUS_FAILED || status == WHEELHOUSE_RICH_STATUS_CANCELLED;
}

// A call's duration as a header shows it.
internal String8
uishell_rt_duration(Arena *arena, U64 ms)
{
  unsigned long long v = ms;
  if(v < 1000) { return push_str8f(arena, "%llums", v); }
  if(v < 60000) { return push_str8f(arena, "%.1fs", v/1000.0); }
  if(v < 3600000) { return push_str8f(arena, "%llum %02llus", v/60000, (v/1000)%60); }
  return push_str8f(arena, "%lluh %02llum", v/3600000, (v/60000)%60);
}

// A pending call's spinner: dots round a circle, the brightest moving on.
#define UISHELL_RT_SPINNER_DOTS 8
#define UISHELL_RT_SPINNER_STEP_US 90000
// How often a conversation's file is looked at for growth, and the most of
// it read in one frame.
#define UISHELL_RT_FOLLOW_POLL_US 250000
#define UISHELL_RT_FOLLOW_STEP MB(4)

typedef struct UIShell_RtSpinnerDraw UIShell_RtSpinnerDraw;
struct UIShell_RtSpinnerDraw
{
  Vec4F32 color;
  U64 phase;
};

internal UI_BOX_CUSTOM_DRAW(uishell_rt_spinner_draw)
{
  UIShell_RtSpinnerDraw *d = (UIShell_RtSpinnerDraw *)user_data;
  Vec2F32 c = center_2f32(box->rect);
  F32 r = Min(dim_2f32(box->rect).x, dim_2f32(box->rect).y)*0.3f, dot = Max(1.5f, r*0.3f);
  for(U64 i = 0; i < UISHELL_RT_SPINNER_DOTS; i += 1)
  {
    F32 a = (F32)i/UISHELL_RT_SPINNER_DOTS;
    U64 behind = (d->phase + UISHELL_RT_SPINNER_DOTS - i) % UISHELL_RT_SPINNER_DOTS;
    Vec4F32 color = d->color;
    color.w *= 1.f - 0.8f*(F32)behind/UISHELL_RT_SPINNER_DOTS;
    Vec2F32 p = v2f32(c.x + r*sin_f32(a), c.y - r*cos_f32(a));
    dr_rect(r2f32p(p.x - dot, p.y - dot, p.x + dot, p.y + dot), color, dot, 0, 1.f);
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
  UIShell_RtCap cap;
  B32 visible;
  // A header or footer row's Container, and its style.
  UIShell_RtNode *container;
  UIShell_RtContainerStyle *style;
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
  B32 at_end;
  U32 new_count;
};

// In a Block's coordinates: the code, and its hover controls right to left
// from the Block's top right, above it.
internal Rng2F32
uishell_rt_code_rect(UIShell_RtPlaced *p, F32 code_height)
{
  F32 y0 = p->frame.pad_top + p->frame.inset_y;
  return r2f32p(p->frame.indent, y0, dim_2f32(p->rect).x, y0 + code_height);
}

// A source/rendered switch, for a language with a Renderer of its own
// (mermaid, LaTeX), would take the next slot.
enum
{
  UIShell_RtControl_Copy,
  UIShell_RtControl_Open,
  UIShell_RtControl_Expand,
};

internal Rng2F32
uishell_rt_control_rect(UIShell_RtPlaced *p, UIShell_RtTheme *t, U32 slot)
{
  F32 w = floor_f32(uishell_rt_px(t)*3.8f), h = uishell_rt_control_h(t), gap = uishell_rt_control_gap(t);
  F32 x1 = dim_2f32(p->rect).x - slot*(w + gap), y1 = p->frame.pad_top - gap;
  return r2f32p(x1 - w, y1 - h, x1, y1);
}

// A capped Block's "Expand" or "Collapse", under its text.
internal Rng2F32
uishell_rt_cap_rect(UIShell_RtPlaced *p, UIShell_RtTheme *t)
{
  F32 x0 = p->frame.indent + p->frame.inset_x, y0 = p->frame.pad_top + p->frame.inset_y + (p->layout ? p->layout->layout->text.height : 0);
  return r2f32p(x0, y0, x0 + floor_f32(uishell_rt_px(t)*6.f), y0 + uishell_rt_line_px(t));
}

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
  if(uishell_rt_atomic(n))
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
    // A new View, which resolves through the Handlers as any opened file;
    // a missing one opens nothing, and the bar says so.
    case UIShell_RtLink_File:
    {
      if(file_path_exists(resolved)) { uishell_cmd("open", .file_path = push_str8_copy(rd_frame_arena(), resolved)); break; }
      String8 notice = push_str8f(scratch.arena, "%S doesn't exist", str8_skip_chop_whitespace(target));
      s->notice_size = Min(notice.size, sizeof(s->notice));
      MemoryCopy(s->notice, notice.str, s->notice_size);
      s->notice_until_us = now_time_us() + 4000000;
    } break;
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

// Copy: plain text and HTML for the clipboard. A focused Embed copies
// through its copy hook: a table whole, as TSV; code what is selected in it,
// or all of it.
internal void
uishell_rt_copy_to_clipboard(UIShell_RichTextState *s)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_RtCode *code = s->focus_embed ? uishell_rt_code_of(s, s->focus_embed) : 0;
  if(code && !txt_pt_match(code->tv.cursor, code->tv.mark))
  {
    // Selected inside a focused code Embed: that, as the code view copies.
    wm_set_clipboard_text(uishell_rt_embed_copy(scratch.arena, s, s->focus_embed, WHEELHOUSE_RICH_COPY_PLAIN, 0));
  }
  else if(s->focus_embed != 0)
  {
    String8 plain = uishell_rt_embed_copy(scratch.arena, s, s->focus_embed, WHEELHOUSE_RICH_COPY_PLAIN, 1);
    String8 html = uishell_rt_embed_copy(scratch.arena, s, s->focus_embed, WHEELHOUSE_RICH_COPY_HTML, 1);
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
    s->caret_upstream = 0;
    s->has_caret = 1;
    uishell_rt_code_show_match(s, match.key, match.start, match.end);
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
      UIShell_RtNode *n = uishell_rt_node(&s->model, e->key);
      if(n == 0) { continue; }
      if(e->part == WHEELHOUSE_RICH_PART_BLOCK)
      {
        height = uishell_rt_block_height(s, n, dim_1f32(uishell_rt_column(s, n)), 0);
      }
      else
      {
        UIShell_RtContainerStyle *st = uishell_rt_container_style(s->styles, n);
        height = e->part == WHEELHOUSE_RICH_PART_HEADER ? uishell_rt_header_height(&s->theme, st) : uishell_rt_footer_height(&s->theme, st);
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
  pl.at_end = w.at_end;
  pl.new_count = w.new_count;
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
    UIShell_RtNode *n = uishell_rt_node(&s->model, e->key);
    if(n == 0) { continue; }
    if(e->part != WHEELHOUSE_RICH_PART_BLOCK)
    {
      p->container = n;
      p->style = uishell_rt_container_style(s->styles, n);
      if(p->style)
      {
        Rng1F32 x = uishell_rt_chrome_column(s, n, p->style);
        p->rect.x0 = x.min;
        p->rect.x1 = x.max;
      }
      continue;
    }
    p->node = n;
    Rng1F32 x = uishell_rt_column(s, n);
    p->rect.x0 = x.min;
    p->rect.x1 = x.max;
    p->frame = uishell_rt_frame(arena, &s->model, &s->theme, s->styles, p->node);
    p->cap = uishell_rt_cap(s, p->node, &p->frame, dim_1f32(x));
    uishell_rt_block_height(s, p->node, dim_1f32(x), &p->layout);
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
    UIShell_RtStop more[64];
    U64 more_count = 0;
    if(p->part == WHEELHOUSE_RICH_PART_HEADER && p->style && (p->style->chrome & UIShell_RtChrome_Header))
    {
      more[more_count++] = (UIShell_RtStop){(p->style->chrome & UIShell_RtChrome_Thread) ? UIShell_RtStop_Thread : UIShell_RtStop_Toggle, p->key, 0, 0};
    }
    if(p->node == 0 && more_count == 0) { continue; }
    if(p->frame.table)
    {
      more[more_count++] = (UIShell_RtStop){UIShell_RtStop_Embed, p->key, 0, 0};
      UIShell_RtTable *t = uishell_rt_table_of(s, p->key);
      B32 capped = 0;
      uishell_rt_table_height(&s->theme, p->node, t && t->expanded, s->viewport, &capped);
      if(capped || (t && t->expanded)) { more[more_count++] = (UIShell_RtStop){UIShell_RtStop_Expand, p->key, 1, 0}; }
    }
    if(p->frame.code_embed && p->node)
    {
      more[more_count++] = (UIShell_RtStop){UIShell_RtStop_Embed, p->key, 0, 0};
      more[more_count++] = (UIShell_RtStop){UIShell_RtStop_CopyButton, p->key, 0, 0};
      more[more_count++] = (UIShell_RtStop){UIShell_RtStop_Open, p->key, 0, 0};
      B32 capped = 0;
      uishell_rt_code_height(uishell_rt_code(s, p->node), &capped);
      if(capped) { more[more_count++] = (UIShell_RtStop){UIShell_RtStop_Expand, p->key, 1, 0}; }
    }
    else if(p->frame.code) { more[more_count++] = (UIShell_RtStop){UIShell_RtStop_CopyButton, p->key, 0, 0}; }
    if(p->node && p->cap.control) { more[more_count++] = (UIShell_RtStop){UIShell_RtStop_Expand, p->key, 1, 0}; }
    for(U64 sp = 0; p->node && sp < p->node->span_count && more_count < ArrayCount(more); sp += 1)
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
      UIShell_RtCode *c = uishell_rt_code_of(s, stop->key);
      UIShell_RtNode *n = uishell_rt_node(&s->model, stop->key);
      U64 owner = n ? uishell_rt_cap_owner(s, n) : 0;
      if(t) { t->expanded = !t->expanded; }
      if(c) { c->expanded = !c->expanded; }
      if(owner) { uishell_rt_toggle_opened(s, owner); }
    } break;
    case UIShell_RtStop_Open: { uishell_rt_code_open(s, stop->key); } break;
    case UIShell_RtStop_CopyButton:
    {
      Temp scratch = scratch_begin(0, 0);
      wm_set_clipboard_text(uishell_rt_embed_copy(scratch.arena, s, stop->key, WHEELHOUSE_RICH_COPY_PLAIN, 1));
      s->copied_key = stop->key;
      s->copied_action = 0;
      s->copied_until_us = now_time_us() + 1500000;
      scratch_end(scratch);
    } break;
    case UIShell_RtStop_Toggle: { uishell_rt_toggle(s, stop->key); } break;
    case UIShell_RtStop_Thread:
    {
      UIShell_RtNode *n = uishell_rt_node(&s->model, stop->key);
      if(n && n->title.size) { uishell_cmd("open_conversation_thread", .string = push_str8_copy(rd_frame_arena(), n->title)); }
    } break;
  }
}

////////////////////////////////
//~ Hover actions (#546)

typedef struct UIShell_RtAction UIShell_RtAction;
struct UIShell_RtAction
{
  U64 key;
  U32 action;
  // In the content box's coordinates.
  Rng2F32 rect;
};

internal String8
uishell_rt_action_label(U32 action)
{
  switch(action)
  {
    default: break;
    case WHEELHOUSE_RICH_ACTION_MESSAGE: return str8_lit("Copy");
    case WHEELHOUSE_RICH_ACTION_TOOL_OUTPUT: return str8_lit("Copy output");
    case WHEELHOUSE_RICH_ACTION_COMMAND: return str8_lit("Copy command");
  }
  return str8_zero();
}

// The Container with hover actions under a point in the content box: a
// row's own, or a Block's nearest that has some.
internal U64
uishell_rt_hovered(UIShell_RichTextState *s, UIShell_RtPlacement *pl, Vec2F32 local)
{
  for(U64 i = 0; i < pl->count; i += 1)
  {
    UIShell_RtPlaced *p = &pl->v[i];
    if(!contains_2f32(p->rect, local)) { continue; }
    if(p->part != WHEELHOUSE_RICH_PART_BLOCK)
    {
      if(p->style && p->style->actions) { return p->key; }
      continue;
    }
    if(p->node == 0) { continue; }
    for(UIShell_RtNode *n = p->node;;)
    {
      UIShell_RtContainerStyle *st = 0;
      UIShell_RtNode *c = uishell_rt_styled_parent(s, n, &st);
      if(c == 0) { break; }
      if(st->actions) { return c->key; }
      n = c;
    }
  }
  return 0;
}

// The hovered Container's actions, right-aligned in its header row, or in
// the row under its Blocks: those its style offers that copy something.
internal U64
uishell_rt_actions(UIShell_RichTextState *s, UIShell_RtPlacement *pl, UIShell_RtAction *out, U64 cap)
{
  if(s->hover_key == 0 || s->doc == 0) { return 0; }
  UIShell_RtTheme *t = &s->theme;
  UIShell_RtPlaced *row = 0;
  for(U64 i = 0; i < pl->count && row == 0; i += 1)
  {
    UIShell_RtPlaced *p = &pl->v[i];
    if(p->key != s->hover_key || p->style == 0) { continue; }
    B32 header = (p->style->chrome & UIShell_RtChrome_Header) != 0;
    if(p->part == (header ? WHEELHOUSE_RICH_PART_HEADER : WHEELHOUSE_RICH_PART_FOOTER)) { row = p; }
  }
  if(row == 0) { return 0; }
  // Asked once per Container and generation, not every frame it is hovered.
  if(s->actions_key != s->hover_key || s->actions_generation != s->model.generation)
  {
    char *error = 0;
    s->actions_mask = 0;
    wheelhouse_rich_text_copy_actions(s->doc, s->hover_key, &s->actions_mask, &error);
    uishell_rt_error(&error);
    s->actions_key = s->hover_key;
    s->actions_generation = s->model.generation;
  }
  U32 mask = s->actions_mask & row->style->actions;
  B32 header = row->part == WHEELHOUSE_RICH_PART_HEADER;
  F32 y0 = row->rect.y0 + uishell_rt_em(t, header ? row->style->inset_top : row->style->inset_bottom);
  F32 h = header ? uishell_rt_header_row(t) : uishell_rt_action_row(t);
  F32 pad = uishell_rt_em(t, 0.5f), x1 = row->rect.x1 - (header ? pad : 0);
  U64 count = 0;
  U32 order[] = {WHEELHOUSE_RICH_ACTION_COMMAND, WHEELHOUSE_RICH_ACTION_TOOL_OUTPUT, WHEELHOUSE_RICH_ACTION_MESSAGE};
  for EachElement(i, order)
  {
    if(!(mask & order[i]) || count == cap) { continue; }
    F32 w = fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, uishell_rt_action_label(order[i])).x + 2*pad;
    out[count++] = (UIShell_RtAction){s->hover_key, order[i], r2f32p(x1 - w, y0 + 2, x1, y0 + h - 2)};
    x1 -= w + pad/2;
  }
  return count;
}

internal void
uishell_rt_do_action(UIShell_RichTextState *s, UIShell_RtAction *a)
{
  WheelhouseBytes bytes = {0};
  char *error = 0;
  if(wheelhouse_rich_text_copy_action(s->doc, a->key, a->action, &bytes, &error))
  {
    wm_set_clipboard_text(str8(bytes.data, bytes.len));
    wheelhouse_bytes_free(bytes);
    s->copied_key = a->key;
    s->copied_action = a->action;
    s->copied_until_us = now_time_us() + 1500000;
  }
  uishell_rt_error(&error);
}

// Copy All: the whole transcript. The core backfills what is left before it
// copies, so the View asks only once its per-frame backfill has finished,
// and the window never waits on a large file's read.
internal void
uishell_rt_copy_all(UIShell_RichTextState *s)
{
  WheelhouseBytes bytes = {0};
  char *error = 0;
  if(s->doc && wheelhouse_rich_text_copy_all(s->doc, &bytes, &error))
  {
    wm_set_clipboard_text(str8(bytes.data, bytes.len));
    wheelhouse_bytes_free(bytes);
  }
  uishell_rt_error(&error);
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
  WheelhouseRichSelection sel = {0};
  if(extend && uishell_rt_atomic(n) && wheelhouse_rich_text_selection(s->doc, s->sub, &sel, &error))
  {
    // A selection that reaches an atomic Block takes all of it: its far end
    // from the anchor.
    UIShell_RtNode *anchor = uishell_rt_node(&s->model, sel.anchor_key);
    byte = anchor && anchor->order > n->order ? 0 : (U32)n->text.size;
  }
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
  s->caret_upstream = 0;
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
  s->hover_key = contains_2f32(content, mouse) ? uishell_rt_hovered(s, pl, sub_2f32(mouse, content.p0)) : 0;
  UIShell_RtAction actions[3];
  U64 action_count = uishell_rt_actions(s, pl, actions, ArrayCount(actions));
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
    UIShell_RtCode *code = 0;
    F32 code_height = 0;
    B32 code_capped = 0;
    if(under && under->frame.code_embed)
    {
      code = uishell_rt_code(s, under->node);
      code_height = uishell_rt_code_height(code, &code_capped);
    }
    B32 in_code = code && contains_2f32(uishell_rt_code_rect(under, code_height), in_block);
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
      // Code scrolls sideways always, and down only while focused and
      // capped: expanded, it is all in the document already.
      B32 code_focused = in_code && s->focus_embed == under->key;
      B32 code_down = code_focused && code_capped && !code->expanded && px_delta.y != 0;
      if(in_code && (code_down || (px_delta.x != 0 && px_delta.y == 0)))
      {
        code->scroll.x += px_delta.x;
        if(code_down) { code->scroll.y += px_delta.y; }
      }
      else if(to_table)
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
      if(in_code)
      {
        // The click that focuses is the code view's too (#542, decision 5):
        // left for its own selection to take.
        s->focus_embed = under->key;
        s->focus_stop = -1;
        s->selecting = 0;
        continue;
      }
      ui_eat_event(evt);
      B32 acted = 0;
      for(U64 i = 0; i < action_count && !acted; i += 1)
      {
        if(contains_2f32(actions[i].rect, local)) { uishell_rt_do_action(s, &actions[i]); acted = 1; }
      }
      // A Section's header toggles it, or opens its thread.
      for(U64 i = 0; i < pl->count && !acted; i += 1)
      {
        UIShell_RtPlaced *p = &pl->v[i];
        if(p->part != WHEELHOUSE_RICH_PART_HEADER || p->style == 0 || !(p->style->chrome & UIShell_RtChrome_Header) || !contains_2f32(p->rect, local)) { continue; }
        UIShell_RtStop stop = {(p->style->chrome & UIShell_RtChrome_Thread) ? UIShell_RtStop_Thread : UIShell_RtStop_Toggle, p->key, 0, 0};
        uishell_rt_activate_stop(s, &stop);
        acted = 1;
      }
      if(acted) { continue; }
      if(code)
      {
        if(contains_2f32(uishell_rt_control_rect(under, t, UIShell_RtControl_Copy), in_block))
        {
          UIShell_RtStop stop = {UIShell_RtStop_CopyButton, under->key, 0, 0};
          uishell_rt_activate_stop(s, &stop);
          continue;
        }
        if(contains_2f32(uishell_rt_control_rect(under, t, UIShell_RtControl_Open), in_block))
        {
          s->open_press = under->key;
          s->open_press_pos = evt->pos;
          continue;
        }
        if(code_capped && contains_2f32(uishell_rt_control_rect(under, t, UIShell_RtControl_Expand), in_block))
        {
          code->expanded = !code->expanded;
          continue;
        }
      }
      if(under && under->frame.code && !under->frame.code_embed && contains_2f32(uishell_rt_copy_button_rect(under, t), in_block))
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
      if(under && under->cap.control && contains_2f32(uishell_rt_cap_rect(under, t), in_block))
      {
        uishell_rt_toggle_opened(s, under->cap.owner);
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
    if(evt->kind == UI_EventKind_Release && s->open_press)
    {
      // Released on "Open" before it became a drag: a click. Any other
      // release ends the press, so a lost one can't open a tab later.
      B32 on_open = code && under->key == s->open_press && contains_2f32(uishell_rt_control_rect(under, t, UIShell_RtControl_Open), in_block);
      B32 moved = length_2f32(sub_2f32(evt->pos, s->open_press_pos)) > UIShell_DragThresholdPT;
      if(on_open && !moved) { uishell_rt_code_open(s, s->open_press); }
      s->open_press = 0;
      ui_eat_event(evt);
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
  // Dragged past the threshold: the code goes where it drops, as a new View.
  if(s->open_press && length_2f32(sub_2f32(mouse, s->open_press_pos)) > UIShell_DragThresholdPT)
  {
    uishell_rt_code_drag_begin(s, s->open_press);
    s->open_press = 0;
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

// A character left or right on screen from a caret, on its line: in a
// right-to-left run, that is backwards or forwards in the text. 0 at the
// line's end, with `rtl_out` the caret's direction.
internal B32
uishell_rt_visual_step(WhTextLayout *l, U32 byte, B32 upstream, S32 dx, U32 *byte_out, B32 *upstream_out, B32 *rtl_out)
{
  WhTextCaret caret = {0};
  if(!wh_text_caret_rect(l, byte, upstream ? WH_TEXT_UPSTREAM : WH_TEXT_DOWNSTREAM, &caret) || caret.line >= l->line_count) { return 0; }
  *rtl_out = !!caret.rtl;
  WhTextLine *line = &l->lines[caret.line];
  WhTextCluster *next = 0;
  for(U64 i = line->cluster_start; i < (U64)line->cluster_start + line->cluster_count && i < l->cluster_count; i += 1)
  {
    WhTextCluster *c = &l->clusters[i];
    if(c->len == 0 || c->advance <= 0.01f || (c->flags & (WH_TEXT_CLUSTER_ELLIPSIS|WH_TEXT_CLUSTER_HARD_BREAK))) { continue; }
    B32 beside = dx < 0 ? c->x + c->advance <= caret.x + 0.01f : c->x >= caret.x - 0.01f;
    if(beside && (next == 0 || (dx < 0 ? c->x > next->x : c->x < next->x))) { next = c; }
  }
  if(next == 0) { return 0; }
  // Past it: a left edge is a left-to-right cluster's start, a
  // right-to-left one's end, which shows on its upstream side.
  B32 end = (dx < 0) == !!(next->flags & WH_TEXT_CLUSTER_RTL);
  *byte_out = end ? next->start + next->len : next->start;
  *upstream_out = end;
  return 1;
}

// The selection's focus moved by a key, within the placed Blocks: a
// character, a word, a line, or to the document's ends.
internal B32
uishell_rt_moved_point(UIShell_RichTextState *s, UIShell_RtPlacement *pl, UI_Event *evt, U64 *key_io, U32 *byte_io, B32 *upstream_io)
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
  B32 upstream = *upstream_io;
  *upstream_io = 0;
  if(uishell_rt_atomic(n) && (dx != 0 || dy != 0))
  {
    // Across an atomic Block in one step: to its far end, then on.
    B32 forward = dx > 0 || dy > 0;
    if(forward ? byte < text.size : byte > 0) { *byte_io = forward ? (U32)text.size : 0; return 1; }
    S64 order = (S64)n->order + (forward ? 1 : -1);
    if(order < 0 || order >= (S64)m->block_count) { return 0; }
    UIShell_RtNode *to = uishell_rt_node(m, m->block_keys[order]);
    if(to == 0) { return 0; }
    *key_io = to->key;
    *byte_io = forward ? 0 : (U32)to->text.size;
    return 1;
  }
  UIShell_RtPlaced *placed = uishell_rt_placed(pl, n->key);
  if(dx != 0 && evt->delta_unit == UI_EventDeltaUnit_Char && placed && placed->layout)
  {
    B32 rtl = 0;
    if(uishell_rt_visual_step(&placed->layout->layout->text, byte, upstream, dx, byte_io, upstream_io, &rtl)) { return 1; }
    // Off the line's end on screen: on in the text, the way the caret's
    // direction runs.
    if(rtl) { dx = -dx; }
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
    B32 accept = evt->kind == UI_EventKind_Press && (evt->slot == UI_EventActionSlot_Accept || evt->key == WM_Key_Return ||
                                                    (evt->key == WM_Key_Space && s->focus_stop >= 0 &&
                                                     (s->focus_stop_value.kind == UIShell_RtStop_Toggle || s->focus_stop_value.kind == UIShell_RtStop_Thread)));
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
      // A code Embed's keys go to the code view, after this loop.
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
      B32 upstream = s->has_caret && s->caret_key == key && s->caret_byte == byte && s->caret_upstream;
      if(uishell_rt_moved_point(s, pl, evt, &key, &byte, &upstream))
      {
        wheelhouse_rich_text_select(s->doc, s->sub, anchor_key, anchor_byte, key, byte, &error);
        s->caret_key = key;
        s->caret_byte = byte;
        s->caret_upstream = upstream;
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
  UIShell_RtCode *code = s->focus_embed ? uishell_rt_code_of(s, s->focus_embed) : 0;
  if(code)
  {
    Access *access = access_open();
    U128 hash = {0};
    TXT_TextInfo info = txt_text_info_from_key_lang(access, code->content, uishell_rt_code_lang(code), &hash);
    String8 data = c_data_from_hash(access, hash);
    S64 page = (S64)(pl->viewport/uishell_text_row_height());
    if(info.lines_count != 0 && rd_do_txt_controls(&info, data, (U64)ClampBot(1, page-1), &code->tv.cursor, &code->tv.mark, &code->tv.preferred_column))
    {
      code->contain_cursor = 1;
    }
    access_close(access);
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
  s->columns = push_array(s->arena, UIShell_RtColumn, UISHELL_RT_COLUMN_SLOTS);
  s->compose_goal_x = -1;
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
    if(l == 0 || uishell_rt_atomic(p->node))
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
      if(wh_text_caret_rect(l, Min(s->caret_byte, (U32)p->node->text.size), s->caret_upstream ? WH_TEXT_UPSTREAM : WH_TEXT_DOWNSTREAM, &caret))
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

// A View of another file or thread, or of its file again.
internal void
uishell_rt_reopen(UIShell_RichTextState *s, String8 path, String8 thread)
{
  // Its file again (rewritten) keeps the composer's draft and its Up.
  Temp scratch = scratch_begin(0, 0);
  B32 same = str8_match(str8(s->path, s->path_size), path, 0);
  String8 draft = same ? push_str8_copy(scratch.arena, str8(s->compose, s->compose_size)) : str8_zero();
  String8 last = same ? push_str8_copy(scratch.arena, str8(s->last_prompt, s->last_prompt_size)) : str8_zero();
  // Prompts on their way go on waiting in the new file; the reset lets go
  // of those another file has.
  U64 pending_count = same ? s->pending_count : 0;
  UIShell_RtPending *pending = push_array(scratch.arena, UIShell_RtPending, UISHELL_RT_PENDING_MAX);
  MemoryCopy(pending, s->pending, sizeof(s->pending));
  if(same) { s->pending_count = 0; }
  uishell_rt_reset(s);
  MemoryCopy(s->pending, pending, sizeof(s->pending));
  s->pending_count = pending_count;
  MemoryCopy(s->compose, draft.str, draft.size);
  s->compose_size = draft.size;
  s->compose_cursor = s->compose_mark = txt_pt(1, 1 + draft.size);
  MemoryCopy(s->last_prompt, last.str, last.size);
  s->last_prompt_size = last.size;
  scratch_end(scratch);
  MemoryCopy(s->path, path.str, path.size);
  s->path_size = path.size;
  s->thread = push_str8_copy(s->arena, thread);
}

internal void uishell_rt_set_notice(UIShell_RichTextState *s, String8 notice);

internal void
uishell_rt_agent_wake(void)
{
  wm_send_wakeup_event();
}

// A watched agent thread (#699) streams from its producer: the watch wakes
// the UI when updates wait, and each frame pumps them into the
// conversation, which asks for older pages as the View nears them. A
// producer that starts over with a fresh snapshot gets a fresh
// conversation; the composer keeps its draft.
internal void
uishell_rt_agent_follow(UIShell_RichTextState *s, String8 endpoint, String8 thread)
{
  char *error = 0;
  if(!str8_match(endpoint, str8(s->agent_endpoint, s->agent_endpoint_size), 0) || !str8_match(thread, s->agent_thread, 0))
  {
    uishell_rt_reopen(s, str8_zero(), str8_zero());
    s->agent_endpoint_size = Min(endpoint.size, sizeof(s->agent_endpoint));
    MemoryCopy(s->agent_endpoint, endpoint.str, s->agent_endpoint_size);
    s->agent_thread = push_str8_copy(s->arena, thread);
  }
  if(s->watch == 0)
  {
    s->watch = wheelhouse_agent_watch_open(uishell_rt_wt(endpoint), uishell_rt_wt(thread), uishell_rt_agent_wake, &error);
    String8 failure = uishell_rt_error(&error);
    s->load_error = failure.size ? push_str8_copy(s->arena, failure) : str8_zero();
    if(s->watch == 0) { return; }
  }
  for(U32 pass = 0; pass < 2; pass += 1)
  {
    if(s->doc == 0)
    {
      s->conversation = 1;
      s->opened_us = now_time_us();
      s->doc = wheelhouse_rich_text_watch_conversation(&error);
      if(s->doc && !wheelhouse_rich_text_subscribe(s->doc, &s->sub, &error)) { wheelhouse_rich_text_free(s->doc); s->doc = 0; }
      if(s->doc) { wheelhouse_rich_text_scroll_to_end(s->doc, s->sub, &error); }
      String8 failure = uishell_rt_error(&error);
      s->load_error = failure.size ? push_str8_copy(s->arena, failure) : str8_zero();
      if(s->doc == 0) { return; }
    }
    U32 flags = wheelhouse_agent_watch_pump(s->watch, s->doc, s->sub, &error);
    uishell_rt_error(&error);
    if(flags & WHEELHOUSE_AGENT_CHANGED) { s->agent_version += 1; }
    if(!(flags & WHEELHOUSE_AGENT_RESTART)) { break; }
    // Its watch goes on; only the conversation starts over.
    WheelhouseAgentWatch *watch = s->watch;
    s->watch = 0;
    U8 endpoint_copy[sizeof(s->agent_endpoint)];
    U64 endpoint_size = s->agent_endpoint_size;
    MemoryCopy(endpoint_copy, s->agent_endpoint, endpoint_size);
    uishell_rt_reopen(s, str8_zero(), str8_zero());
    s->watch = watch;
    MemoryCopy(s->agent_endpoint, endpoint_copy, endpoint_size);
    s->agent_endpoint_size = endpoint_size;
    s->agent_thread = push_str8_copy(s->arena, thread);
    s->agent_version += 1;
  }
  // The composer shows while the thread takes input, and posts to its routes.
  s->input_size = 0;
  if(s->doc && wheelhouse_rich_text_conversation_takes_input(s->doc, &error))
  {
    String8 input = push_str8f(rd_frame_arena(), "unix://%S", endpoint);
    s->input_size = Min(input.size, sizeof(s->input));
    MemoryCopy(s->input, input.str, s->input_size);
  }
  uishell_rt_error(&error);
  // Why it isn't streaming, while it isn't.
  WheelhouseBytes why = {0};
  U32 connection = wheelhouse_agent_watch_connection(s->watch, &why, &error);
  uishell_rt_error(&error);
  String8 reason = why.data ? push_str8_copy(rd_frame_arena(), str8(why.data, why.len)) : str8_zero();
  if(why.data) { wheelhouse_bytes_free(why); }
  if(connection == WHEELHOUSE_AGENT_RETRYING) { uishell_rt_set_notice(s, push_str8f(rd_frame_arena(), "Reconnecting to the agent: %S", reason)); }
  if(connection == WHEELHOUSE_AGENT_FAILED) { uishell_rt_set_notice(s, push_str8f(rd_frame_arena(), "Can't watch this thread: %S", reason)); }
}

// A conversation follows its file (#617). It reads the file itself, not
// through the content cache, so it knows how much the core has, and then
// reads only what was appended, never the whole file again. A file that
// shrinks or is replaced opens again; one that goes missing keeps what
// shows until it is back. A file truncated and rewritten in place past its
// old size within one poll reads as an append: contract files are only
// appended to, or rewritten from the start by the translator.
internal void
uishell_rt_follow(UIShell_RichTextState *s, String8 path, String8 thread, FileProperties props, Arena *scratch)
{
  char *error = 0;
  // Growth shows within a poll, and so does a file that isn't there yet.
  rd_request_frame_at(now_time_us() + UISHELL_RT_FOLLOW_POLL_US);
  if(props.modified == 0) { return; }
  if(s->doc)
  {
    // An id that can't be read (mid-rename, say) is no evidence of a new file.
    FileID id = id_from_file_path(path);
    FileID none = {0};
    B32 replaced = file_id_compare(id, none) != 0 && file_id_compare(id, s->file_id) != 0;
    if(props.size < s->fed || replaced) { uishell_rt_reopen(s, path, thread); }
  }
  // An open that failed waits for the file to change, rather than reading
  // all of it again every frame.
  if(s->doc == 0 && s->failed_size == props.size && s->failed_modified == props.modified) { return; }
  if(s->doc == 0 || props.size > s->fed)
  {
    File file = file_open(AccessFlag_Read|AccessFlag_ShareRead|AccessFlag_ShareWrite, path);
    if(file_match(file, file_zero())) { return; }
    U64 size = properties_from_file(file).size;
    U64 want = s->doc == 0 ? size : Min(size - Min(size, s->fed), UISHELL_RT_FOLLOW_STEP);
    U8 *bytes = push_array_no_zero(scratch, U8, Max(want, 1));
    U64 got = file_read(file, r1u64(s->fed, s->fed + want), bytes);
    String8 data = str8(bytes, got);
    if(s->doc == 0)
    {
      s->file_id = id_from_file(file);
      s->conversation = 1;
      s->opened_us = now_time_us();
      if(thread.size) { s->doc = wheelhouse_rich_text_open_thread(uishell_rt_wt(data), uishell_rt_wt(thread), &error); }
      else { s->doc = wheelhouse_rich_text_open_conversation(uishell_rt_wt(data), &error); }
      if(s->doc && !wheelhouse_rich_text_subscribe(s->doc, &s->sub, &error)) { wheelhouse_rich_text_free(s->doc); s->doc = 0; }
      // A conversation opens at its newest.
      if(s->doc) { wheelhouse_rich_text_scroll_to_end(s->doc, s->sub, &error); }
      String8 failure = uishell_rt_error(&error);
      s->load_error = failure.size ? push_str8_copy(s->arena, failure) : str8_zero();
      s->failed_size = s->doc ? 0 : props.size;
      s->failed_modified = s->doc ? 0 : props.modified;
      s->fed = s->doc ? got : 0;
    }
    else if(wheelhouse_rich_text_append(s->doc, uishell_rt_wt(data), 0, &error)) { s->fed += got; }
    else
    {
      // What the core has no longer matches the file: start again from it.
      uishell_rt_error(&error);
      uishell_rt_reopen(s, path, thread);
    }
    file_close(file);
    // A burst bigger than a step reads on over the next frames.
    if(s->doc && s->fed < size) { rd_request_frame(); }
  }
}

//~ Input to the producer (the agent View prototype)

internal String8
uishell_rt_json_string(Arena *arena, String8 text)
{
  String8List parts = {0};
  str8_list_push(arena, &parts, str8_lit("\""));
  U64 start = 0;
  for(U64 i = 0; i < text.size; i += 1)
  {
    U8 c = text.str[i];
    if(c != '"' && c != '\\' && c >= 0x20) { continue; }
    str8_list_push(arena, &parts, str8_substr(text, r1u64(start, i)));
    if(c == '"' || c == '\\') { str8_list_pushf(arena, &parts, "\\%c", c); }
    else { str8_list_pushf(arena, &parts, "\\u%04x", c); }
    start = i + 1;
  }
  str8_list_push(arena, &parts, str8_substr(text, r1u64(start, text.size)));
  str8_list_push(arena, &parts, str8_lit("\""));
  return str8_list_join(arena, &parts, 0);
}

internal U64
uishell_rt_input_post(UIShell_RichTextState *s, String8 path, String8 body)
{
  char *error = 0;
  String8 route = push_str8f(rd_frame_arena(), "/threads/%S%S", s->agent_thread, path);
  U64 ticket = wheelhouse_agent_input_post(uishell_rt_wt(str8(s->input, s->input_size)), uishell_rt_wt(route), uishell_rt_wt(body), &error);
  uishell_rt_error(&error);
  return ticket;
}

internal void
uishell_rt_set_notice(UIShell_RichTextState *s, String8 notice)
{
  s->notice_size = Min(notice.size, sizeof(s->notice));
  MemoryCopy(s->notice, notice.str, s->notice_size);
  s->notice_until_us = now_time_us() + 6000000;
}

// A prompt that didn't send goes back to an empty composer, so nothing
// typed is lost.
internal void
uishell_rt_unsend(UIShell_RichTextState *s, U64 index, String8 why)
{
  UIShell_RtPending *p = &s->pending[index];
  if(s->compose_size == 0)
  {
    MemoryCopy(s->compose, p->text, p->text_size);
    s->compose_size = p->text_size;
    s->compose_cursor = s->compose_mark = txt_pt(1, 1 + s->compose_size);
  }
  Temp scratch = scratch_begin(0, 0);
  uishell_rt_set_notice(s, push_str8f(scratch.arena, "Couldn't send: %S", why));
  scratch_end(scratch);
  char *error = 0;
  wheelhouse_rich_text_withdraw_user_message(s->doc, uishell_rt_wt(str8_cstring(p->id)), &error);
  uishell_rt_error(&error);
  wheelhouse_agent_input_forget(p->ticket);
  MemoryCopy(p, p + 1, sizeof(*p)*(s->pending_count - index - 1));
  s->pending_count -= 1;
}

// A prompt goes as AG-UI's RunAgentInput, with only the new message: the
// harness keeps the history. It shows at the conversation's end, pending,
// until the conversation has a user message with its id.
internal void
uishell_rt_send_prompt(UIShell_RichTextState *s, String8 text, Arena *scratch)
{
  if(s->pending_count == UISHELL_RT_PENDING_MAX)
  {
    wheelhouse_agent_input_forget(s->pending[0].ticket);
    MemoryCopy(&s->pending[0], &s->pending[1], sizeof(s->pending[0])*(UISHELL_RT_PENDING_MAX - 1));
    s->pending_count -= 1;
  }
  UIShell_RtPending *p = &s->pending[s->pending_count++];
  MemoryZeroStruct(p);
  p->sent_us = now_time_us();
  snprintf(p->id, sizeof(p->id), "wh-%llu", (unsigned long long)p->sent_us);
  p->text_size = Min(text.size, sizeof(p->text));
  MemoryCopy(p->text, text.str, p->text_size);
  s->last_prompt_size = p->text_size;
  MemoryCopy(s->last_prompt, p->text, p->text_size);
  String8 body = push_str8f(scratch, "{\"threadId\":%S,\"runId\":\"%s\",\"messages\":[{\"id\":\"%s\",\"role\":\"user\",\"content\":%S}]}",
                            uishell_rt_json_string(scratch, s->agent_thread), p->id, p->id, uishell_rt_json_string(scratch, text));
  char *error = 0;
  wheelhouse_rich_text_pend_user_message(s->doc, uishell_rt_wt(str8_cstring(p->id)), uishell_rt_wt(str8(p->text, p->text_size)), &error);
  uishell_rt_error(&error);
  p->ticket = uishell_rt_input_post(s, str8_lit("/run"), body);
  if(p->ticket == 0) { uishell_rt_unsend(s, s->pending_count - 1, str8_lit("the producer's input isn't reachable")); }
}

// Pending prompts move on: one in the conversation is done, one that
// failed goes back to the composer.
internal void
uishell_rt_pending_update(UIShell_RichTextState *s)
{
  for(U64 i = 0; i < s->pending_count;)
  {
    UIShell_RtPending *p = &s->pending[i];
    char *error = 0;
    B32 arrived = wheelhouse_rich_text_has_user_message(s->doc, uishell_rt_wt(str8_cstring(p->id)), &error);
    uishell_rt_error(&error);
    if(arrived)
    {
      wheelhouse_agent_input_forget(p->ticket);
      MemoryCopy(p, p + 1, sizeof(*p)*(s->pending_count - i - 1));
      s->pending_count -= 1;
      continue;
    }
    WheelhouseBytes why = {0};
    U32 status = wheelhouse_agent_input_status(p->ticket, &why, &error);
    uishell_rt_error(&error);
    if(status == WHEELHOUSE_AGENT_INPUT_FAILED)
    {
      Temp scratch = scratch_begin(0, 0);
      String8 reason = push_str8_copy(scratch.arena, str8(why.data, why.len));
      if(why.data) { wheelhouse_bytes_free(why); }
      uishell_rt_unsend(s, i, reason);
      scratch_end(scratch);
      continue;
    }
    // A file opened again has lost it: show it there too.
    wheelhouse_rich_text_pend_user_message(s->doc, uishell_rt_wt(str8_cstring(p->id)), uishell_rt_wt(str8(p->text, p->text_size)), &error);
    uishell_rt_error(&error);
    i += 1;
  }
}

//~ The composer (the agent View prototype, #683)

// The composer's text as the lines it shows: wrapped at `width` and at its
// newlines, each a byte range of the text without its newline.
typedef struct UIShell_RtComposeLines UIShell_RtComposeLines;
struct UIShell_RtComposeLines
{
  Rng1U64 *v;
  U64 count;
};

internal UIShell_RtComposeLines
uishell_rt_compose_lines(Arena *arena, String8 text, FNT_Tag font, F32 size, F32 width)
{
  // Each line but the last takes at least a byte, so there are at most
  // one more than the text has.
  UIShell_RtComposeLines lines = {push_array(arena, Rng1U64, text.size + 1), 0};
  for(U64 start = 0; start <= text.size;)
  {
    U64 newline = str8_find_needle(text, start, str8_lit("\n"), 0);
    U64 line_start = start, at = start;
    F32 x = 0;
    while(at < newline)
    {
      // A word and the spaces after it stay on this line if the word fits.
      U64 word_end = at;
      while(word_end < newline && text.str[word_end] != ' ') { word_end += 1; }
      U64 end = word_end;
      while(end < newline && text.str[end] == ' ') { end += 1; }
      F32 word_w = fnt_dim_from_tag_size_string(font, size, 0, 0, str8_substr(text, r1u64(at, word_end))).x;
      if(x > 0 && x + word_w > width)
      {
        lines.v[lines.count++] = r1u64(line_start, at);
        line_start = at;
        x = 0;
        continue;
      }
      if(word_w > width)
      {
        // A word wider than the line breaks where it stops fitting.
        U64 cut = at + utf8_decode(text.str + at, text.size - at).inc;
        while(cut < word_end)
        {
          U64 next = cut + utf8_decode(text.str + cut, text.size - cut).inc;
          if(fnt_dim_from_tag_size_string(font, size, 0, 0, str8_substr(text, r1u64(at, next))).x > width) { break; }
          cut = next;
        }
        if(cut < word_end)
        {
          lines.v[lines.count++] = r1u64(line_start, cut);
          line_start = at = cut;
          continue;
        }
      }
      x += fnt_dim_from_tag_size_string(font, size, 0, 0, str8_substr(text, r1u64(at, end))).x;
      at = end;
    }
    lines.v[lines.count++] = r1u64(line_start, newline);
    start = newline + 1;
  }
  return lines;
}

// The line a byte shows on: where a wrapped line ends, the next one.
internal U64
uishell_rt_compose_line_of(UIShell_RtComposeLines *lines, U64 byte)
{
  U64 i = 0;
  while(i + 1 < lines->count && lines->v[i + 1].min <= byte) { i += 1; }
  return i;
}

// The last place a cursor can be on a line: before the space a wrapped
// line ends with, since after it is the next line's start.
internal U64
uishell_rt_compose_line_end(String8 text, Rng1U64 line)
{
  if(line.max == line.min || line.max >= text.size || text.str[line.max] == '\n') { return line.max; }
  U64 end = line.max - 1;
  while(end > line.min && (text.str[end] & 0xc0) == 0x80) { end -= 1; }
  return end;
}

internal F32
uishell_rt_compose_x(String8 text, Rng1U64 line, U64 byte, FNT_Tag font, F32 size)
{
  return fnt_dim_from_tag_size_string(font, size, 0, 0, str8_substr(text, r1u64(line.min, Clamp(line.min, byte, line.max)))).x;
}

// The place on a line nearest `x`.
internal U64
uishell_rt_compose_byte_at(String8 text, Rng1U64 line, F32 x, FNT_Tag font, F32 size)
{
  F32 left = 0;
  U64 end = uishell_rt_compose_line_end(text, line);
  for(U64 at = line.min; at < end;)
  {
    U64 next = at + utf8_decode(text.str + at, text.size - at).inc;
    F32 right = fnt_dim_from_tag_size_string(font, size, 0, 0, str8_substr(text, r1u64(line.min, next))).x;
    if(x < (left + right)/2) { return at; }
    left = right;
    at = next;
  }
  return end;
}

// The field's selection and cursor, in its box's coordinates.
typedef struct UIShell_RtComposeDraw UIShell_RtComposeDraw;
struct UIShell_RtComposeDraw
{
  Rng2F32 *selection;
  U64 selection_count;
  Rng2F32 cursor;
  Vec4F32 cursor_color;
};

internal UI_BOX_CUSTOM_DRAW(uishell_rt_compose_draw)
{
  UIShell_RtComposeDraw *d = (UIShell_RtComposeDraw *)user_data;
  Vec4F32 select_color = ui_color_from_tags_key_name(box->tags_key, str8_lit("selection"));
  for(U64 i = 0; i < d->selection_count; i += 1)
  {
    dr_rect(shift_2f32(d->selection[i], box->rect.p0), select_color, 2.f, 0, 1.f);
  }
  if(d->cursor_color.w > 0)
  {
    dr_rect(shift_2f32(d->cursor, box->rect.p0), d->cursor_color, 0, 0, 0);
  }
}

#define UISHELL_RT_COMPOSE_LINES 8

// The composer, when the producer takes input: a card at the foot of the
// reading column, its field growing to 8 lines and then scrolling, beside
// one button that sends or, while a run is open and there's nothing to
// send, stops it. Enter sends; Shift+Enter or Ctrl+J starts a line. Above
// it, while the agent works or a prompt is on its way, a row says so.
// Returns the height it takes from the View's bottom.
internal F32
uishell_rt_composer(UIShell_RichTextState *s, UI_Box *root, F32 bottom, F32 column_x, F32 column_w, F32 px, Arena *scratch)
{
  char *error = 0;
  WheelhouseBytes tool = {0};
  B32 running = wheelhouse_rich_text_conversation_running(s->doc, &tool, &error);
  String8 running_tool = str8_zero();
  if(tool.data)
  {
    running_tool = push_str8_copy(scratch, str8(tool.data, tool.len));
    wheelhouse_bytes_free(tool);
  }
  uishell_rt_error(&error);
  if(running && s->running_since_us == 0) { s->running_since_us = now_time_us(); }
  if(!running) { s->running_since_us = 0; }
  uishell_rt_pending_update(s);
  U64 now = now_time_us();
  // Elapsed time, and "Sending" moving to "Waiting", redraw on their own.
  if(running || s->pending_count) { rd_request_frame_at(now + 250000); }

  FNT_Tag font = ui_top_font();
  F32 size = ui_top_font_size();
  F32 line_h = floor_f32(size*1.6f), pad = floor_f32(px*0.5f), gap = floor_f32(px*0.6f);
  F32 button_w = floor_f32(fnt_dim_from_tag_size_string(font, size, 0, 0, str8_lit("Send")).x + size*2.4f);
  F32 button_h = floor_f32(size*2.1f);
  F32 text_w = Max(column_w - 2*pad - button_w - pad, size*4);
  UI_Key key = ui_key_from_string(ui_active_seed_key(), str8_lit("###rich_text_compose"));
  // The card holds the field's focus, and isn't built yet: ask last frame's.
  UI_Key card_key = ui_key_zero();
  UI_Parent(root) { card_key = ui_key_from_string(ui_active_seed_key(), str8_lit("###rich_text_composer")); }
  UI_Box *last_card = ui_box_from_key(card_key);
  if(s->compose_blur && !ui_box_is_nil(last_card)) { last_card->default_nav_focus_next_active_key = ui_key_zero(); }
  s->compose_blur = 0;
  B32 active = (last_card->flags & UI_BoxFlag_FocusActive) && ui_key_match(last_card->default_nav_focus_active_key, key) &&
               !ui_key_match(last_card->default_nav_focus_next_active_key, ui_key_zero());

  //- Keys, while the field has them.
  B32 send = 0, stop = 0;
  UIShell_RtComposeLines lines = uishell_rt_compose_lines(scratch, str8(s->compose, s->compose_size), font, size, text_w);
  if(active)
  {
    for(UI_Event *evt = 0; ui_next_event(&evt);)
    {
      String8 text = str8(s->compose, s->compose_size);
      U64 cursor = (U64)(s->compose_cursor.column - 1);
      B32 taken = 1, vertical = 0, edited = 0;
      B32 newline = evt->kind == UI_EventKind_Press &&
                    ((evt->key == WM_Key_Return && (evt->modifiers & WM_Modifier_Shift)) ||
                     (evt->key == WM_Key_J && evt->modifiers == WM_Modifier_Ctrl));
      if(newline)
      {
        UI_Event insert = {.kind = UI_EventKind_Text, .string = str8_lit("\n")};
        UI_TxtOp op = ui_single_line_txt_op_from_event(scratch, &insert, text, s->compose_cursor, s->compose_mark);
        ui_apply_text_edit_op(scratch, op, s->compose, sizeof(s->compose), &s->compose_size, &s->compose_cursor, &s->compose_mark);
        edited = 1;
      }
      else if(evt->kind == UI_EventKind_Press && (evt->slot == UI_EventActionSlot_Accept || evt->key == WM_Key_Return))
      {
        send = 1;
      }
      else if(evt->kind == UI_EventKind_Press && (evt->slot == UI_EventActionSlot_Cancel || evt->key == WM_Key_Esc))
      {
        // Esc stops a running turn, and otherwise leaves the field.
        if(running) { stop = 1; }
        else if(!ui_box_is_nil(last_card)) { last_card->default_nav_focus_next_active_key = ui_key_zero(); }
      }
      else if(evt->kind == UI_EventKind_Navigate && evt->delta_2s32.y != 0)
      {
        vertical = 1;
        if(evt->delta_2s32.y < 0 && s->compose_size == 0 && s->last_prompt_size)
        {
          // Up in an empty field brings back the last prompt.
          MemoryCopy(s->compose, s->last_prompt, s->last_prompt_size);
          s->compose_size = s->last_prompt_size;
          s->compose_cursor = s->compose_mark = txt_pt(1, 1 + s->compose_size);
          edited = 1;
        }
        else
        {
          // Up and Down keep to the column they started from.
          U64 at = uishell_rt_compose_line_of(&lines, cursor);
          if(s->compose_goal_x < 0) { s->compose_goal_x = uishell_rt_compose_x(text, lines.v[at], cursor, font, size); }
          U64 target = 0;
          if(evt->delta_2s32.y < 0) { target = at == 0 ? 0 : uishell_rt_compose_byte_at(text, lines.v[at - 1], s->compose_goal_x, font, size); }
          else { target = at + 1 == lines.count ? text.size : uishell_rt_compose_byte_at(text, lines.v[at + 1], s->compose_goal_x, font, size); }
          s->compose_cursor = txt_pt(1, 1 + target);
          if(!(evt->flags & UI_EventFlag_KeepMark)) { s->compose_mark = s->compose_cursor; }
        }
      }
      else if(evt->kind == UI_EventKind_Navigate && evt->delta_unit == UI_EventDeltaUnit_Line)
      {
        // Home and End go to the ends of the line the cursor shows on.
        Rng1U64 line = lines.v[uishell_rt_compose_line_of(&lines, cursor)];
        U64 target = evt->delta_2s32.x > 0 ? uishell_rt_compose_line_end(text, line) : line.min;
        s->compose_cursor = txt_pt(1, 1 + target);
        if(!(evt->flags & UI_EventFlag_KeepMark)) { s->compose_mark = s->compose_cursor; }
      }
      else if((evt->kind == UI_EventKind_Edit || evt->kind == UI_EventKind_Navigate || evt->kind == UI_EventKind_Text) && evt->delta_2s32.y == 0)
      {
        // The rest is as a line edit has it, over the whole text.
        UI_TxtOp op = ui_single_line_txt_op_from_event(scratch, evt, text, s->compose_cursor, s->compose_mark);
        ui_apply_text_edit_op(scratch, op, s->compose, sizeof(s->compose), &s->compose_size, &s->compose_cursor, &s->compose_mark);
        edited = 1;
      }
      else
      {
        taken = 0;
      }
      if(taken)
      {
        ui_eat_event(evt);
        s->compose_moved_us = now;
        if(!vertical) { s->compose_goal_x = -1; }
      }
      if(edited) { lines = uishell_rt_compose_lines(scratch, str8(s->compose, s->compose_size), font, size, text_w); }
    }
    ui_register_text_edit_owner(key, s->compose_cursor, s->compose_mark);
  }

  //- Geometry: the card at the foot of the column, its status row above.
  String8 text = str8(s->compose, s->compose_size);
  U64 shown = Clamp(1, lines.count, UISHELL_RT_COMPOSE_LINES);
  F32 card_h = Max(shown*line_h, button_h) + 2*pad;
  B32 status = running || s->pending_count;
  F32 row_h = floor_f32(size*2.f);
  Rng2F32 card_rect = r2f32p(column_x, bottom - gap - card_h, column_x + column_w, bottom - gap);
  U64 cursor = (U64)(s->compose_cursor.column - 1), mark = (U64)(s->compose_mark.column - 1);
  U64 cursor_line = uishell_rt_compose_line_of(&lines, cursor);
  // Past 8 lines the field scrolls, keeping the cursor's line in sight.
  if(cursor_line < s->compose_first_line) { s->compose_first_line = cursor_line; }
  if(cursor_line >= s->compose_first_line + shown) { s->compose_first_line = cursor_line + 1 - shown; }
  s->compose_first_line = Min(s->compose_first_line, lines.count - shown);

  //- The status row.
  if(status)
  {
    String8 left = str8_zero(), right = str8_zero();
    if(running)
    {
      static const char *dots[] = {"   ", ".  ", ".. ", "..."};
      U64 elapsed = (now - s->running_since_us)/1000000;
      String8 what = running_tool.size ? push_str8f(scratch, " · %S", running_tool) : str8_zero();
      left = push_str8f(scratch, "Working%s %llus%S", dots[(now/400000) % 4], (unsigned long long)elapsed, what);
      right = str8_lit("Esc to stop");
    }
    else
    {
      // The newest prompt on its way: sending while the request is out, then
      // waiting on the agent, after a moment so a quick echo never flickers.
      UIShell_RtPending *p = &s->pending[s->pending_count - 1];
      WheelhouseBytes why = {0};
      U32 sent = wheelhouse_agent_input_status(p->ticket, &why, &error);
      if(why.data) { wheelhouse_bytes_free(why); }
      uishell_rt_error(&error);
      B32 waiting = sent == WHEELHOUSE_AGENT_INPUT_ACCEPTED && now - p->sent_us > 1000000;
      left = waiting ? str8_lit("Waiting for agent…") : str8_lit("Sending…");
    }
    UI_Box *row = &ui_nil_box;
    UI_Parent(root) UI_Rect(r2f32p(column_x + pad, card_rect.y0 - row_h, column_x + column_w - pad, card_rect.y0)) UI_ChildLayoutAxis(Axis2_X)
    {
      row = ui_build_box_from_stringf(UI_BoxFlag_Floating, "###rich_text_compose_status");
    }
    UI_Parent(row) UI_PrefHeight(ui_px(row_h, 1)) UI_TagF("weak") UI_TextPadding(0)
    {
      UI_PrefWidth(ui_pct(1, 0)) ui_label(left);
      if(right.size) { UI_PrefWidth(ui_text_dim(1, 1)) ui_label(right); }
    }
  }

  //- The card. The field takes focus through the card's focus-nav box, as
  // the web View's address bar does through that View's root.
  UI_Box *card = &ui_nil_box;
  UI_Parent(root) UI_Rect(card_rect) UI_Focus(UI_FocusKind_On) UI_CornerRadius(floor_f32(size*0.9f))
    UI_BackgroundColor(uishell_rt_bubble_color(&s->theme))
  {
    card = ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_DrawBackground|UI_BoxFlag_DefaultFocusNav|
                                     UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder, "###rich_text_composer");
  }
  UI_Parent(card) UI_Focus(UI_FocusKind_Null)
  {
    F32 field_h = shown*line_h;
    UI_Box *field = &ui_nil_box;
    UI_Rect(r2f32p(pad, pad + (card_h - 2*pad - field_h)/2, pad + text_w, pad + (card_h - 2*pad - field_h)/2 + field_h))
    {
      ui_set_next_hover_cursor(WM_Cursor_IBar);
      // The cursor shows the field has the keys; no focus ring besides. It
      // clips to its own rect, as a plain clip leaves out a pixel each side,
      // and with it the cursor at the start of a line.
      field = ui_build_box_from_key(UI_BoxFlag_Floating|UI_BoxFlag_Clip|UI_BoxFlag_ClipToRect|UI_BoxFlag_MouseClickable|
                                    UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder, key);
    }
    F32 scroll = s->compose_first_line*line_h;
    UIShell_RtComposeDraw *d = push_array(ui_build_arena(), UIShell_RtComposeDraw, 1);
    d->selection = push_array(ui_build_arena(), Rng2F32, lines.count);
    U64 lo = Min(cursor, mark), hi = Max(cursor, mark);
    UI_Parent(field) UI_TextPadding(0) UI_PrefHeight(ui_px(line_h, 1))
    {
      if(text.size == 0 && !active)
      {
        UI_Rect(r2f32p(0, 0, text_w, line_h)) UI_TagF("weak")
        {
          ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_DrawText, "Message the agent###rt_compose_placeholder");
        }
      }
      for(U64 i = s->compose_first_line; i < s->compose_first_line + shown; i += 1)
      {
        Rng1U64 line = lines.v[i];
        F32 y = i*line_h - scroll;
        UI_Rect(r2f32p(0, y, text_w, y + line_h))
        {
          UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_DrawText|UI_BoxFlag_DisableTextTrunc, "###rt_compose_line_%llu", (unsigned long long)i);
          ui_box_equip_display_string(box, str8_substr(text, line));
        }
        // The selection over this line, with a sliver for a newline in it.
        if(lo != hi && lo <= line.max && hi >= line.min && !(hi == line.min && i > 0 && lines.v[i - 1].max == line.min))
        {
          F32 x0 = uishell_rt_compose_x(text, line, lo, font, size);
          F32 x1 = uishell_rt_compose_x(text, line, hi, font, size);
          if(hi > line.max && line.max < text.size && text.str[line.max] == '\n') { x1 += floor_f32(size*0.4f); }
          if(x1 > x0) { d->selection[d->selection_count++] = r2f32p(x0, y + line_h*0.1f, x1, y + line_h*0.9f); }
        }
      }
    }
    F32 cursor_x = uishell_rt_compose_x(text, lines.v[cursor_line], cursor, font, size);
    F32 cursor_y = cursor_line*line_h - scroll;
    d->cursor = r2f32p(cursor_x, cursor_y + line_h*0.15f, cursor_x + ClampBot(1.f, floor_f32(size/10.f)), cursor_y + line_h*0.85f);
    // Lit for half a second after it moves, then on and off each half second.
    if(active)
    {
      U64 since = now - s->compose_moved_us;
      B32 lit = since < 500000 || ((since - 500000)/500000) % 2 == 1;
      if(lit) { d->cursor_color = s->theme.text; }
      rd_request_frame_at(now + 500000 - (since % 500000));
    }
    ui_box_equip_custom_draw(field, uishell_rt_compose_draw, d);

    // A press puts the cursor under the mouse, and a drag selects.
    UI_Signal sig = ui_signal_from_box(field);
    if(ui_pressed(sig) || (active && ui_dragging(sig)))
    {
      Vec2F32 at = sub_2f32(ui_mouse(), field->rect.p0);
      U64 i = (U64)Clamp(0, (S64)floor_f32((at.y + scroll)/line_h), (S64)lines.count - 1);
      U64 byte = uishell_rt_compose_byte_at(text, lines.v[i], at.x, font, size);
      s->compose_cursor = txt_pt(1, 1 + byte);
      if(ui_pressed(sig))
      {
        s->compose_mark = s->compose_cursor;
        s->compose_goal_x = -1;
        s->compose_moved_us = now;
        uishell_cmd("focus_panel");
        if(!active)
        {
          ui_set_auto_focus_active_key(key);
          ui_kill_action();
          active = 1;
        }
      }
    }
    // As the shell's own fields do: keys bound to text (Space is "accept")
    // type while the field is active.
    if(active) { ui_take_text_field_focus(); rd_state->text_edit_mode = 1; }

    // One button: Stop while a run is open and there's nothing to send.
    B32 stop_button = running && str8_skip_chop_whitespace(text).size == 0;
    F32 button_y = card_h - pad - button_h;
    UI_Rect(r2f32p(column_w - pad - button_w, button_y, column_w - pad, button_y + button_h)) UI_CornerRadius(floor_f32(button_h/2))
      UI_TextAlignment(UI_TextAlign_Center)
    {
      UI_Box *button = ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_Clickable|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|
                                                 UI_BoxFlag_DrawText|UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects|
                                                 UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder,
                                                 "%s###rich_text_send", stop_button ? "Stop" : "Send");
      if(ui_clicked(ui_signal_from_box(button))) { if(stop_button) { stop = 1; } else { send = 1; } }
    }
  }

  //- What the keys and the button asked for.
  if(stop) { wheelhouse_agent_input_forget(uishell_rt_input_post(s, str8_lit("/stop"), str8_lit("{}"))); }
  String8 prompt = str8_skip_chop_whitespace(str8(s->compose, s->compose_size));
  if(send && prompt.size)
  {
    uishell_rt_send_prompt(s, prompt, scratch);
    s->compose_size = 0;
    s->compose_cursor = s->compose_mark = txt_pt(1, 1);
    s->compose_first_line = 0;
    // The conversation goes to its end, where the prompt now is.
    wheelhouse_rich_text_scroll_to_end(s->doc, s->sub, &error);
    uishell_rt_error(&error);
  }
  return gap + card_h + gap + (status ? row_h : 0);
}


WH_VIEW_UI_FUNCTION_DEF(rich_text)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_RichTextState *s = wh_view_state(ctx, UIShell_RichTextState);
  rd_view_state_from_view(ctx->settings.view, 0)->release_user_data = uishell_rt_release;
  if(!s->initialized) { uishell_rt_reset(s); }
  char *error = 0;
  s->stats.frames += 1;
  s->stats.frame_layouts = 0;
  s->stats.frame_layout_us = 0;
  s->stats.blocks_boxed = 0;

  //- Markdown through the content cache: saving it changes its hash, and
  // the reload keeps the reading position (the core's Scroll Anchor).
  // A conversation shows from its contract file (#616), one thread of it
  // if the View names a subagent's, and follows it as it grows (#617).
  // A watched agent thread streams from its producer instead (#699).
  String8 agent_endpoint = wh_view_setting(ctx, str8_lit("agent_endpoint"));
  String8 agent_thread = wh_view_setting(ctx, str8_lit("agent_thread"));
  B32 agent = agent_endpoint.size != 0 && agent_thread.size != 0 && agent_endpoint.size <= sizeof(s->agent_endpoint);
  String8 path = agent ? str8_zero() : uishell_rt_path_from_expr(scratch.arena, rd_expr_from_cfg(rd_cfg_from_view(ctx->settings.view)));
  String8 thread = agent ? str8_zero() : wh_view_setting(ctx, str8_lit("thread"));
  if((!str8_match(path, str8(s->path, s->path_size), 0) || !str8_match(thread, s->thread, 0)) && path.size <= sizeof(s->path))
  {
    uishell_rt_reopen(s, path, thread);
  }
  B32 conversation = str8_match(wh_media_type_from_path(path), str8_lit(WH_CONVERSATION_MEDIA_TYPE), 0);
  FileProperties props = properties_from_file_path(path);
  if(agent)
  {
    uishell_rt_agent_follow(s, agent_endpoint, agent_thread);
    rd_workspace_surface_contribute_version(s->agent_version);
    // Not a file: what follows reads as one that is there.
    props.modified = 1;
  }
  else if(conversation)
  {
    uishell_rt_follow(s, path, thread, props, scratch.arena);
    rd_workspace_surface_contribute_version(s->fed ^ s->file_id.v[0] ^ s->file_id.v[1] ^ s->file_id.v[2]);
  }
  else
  {
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
  }

  // After loading, which may have reset the state.
  s->theme = uishell_rt_theme();
  UIShell_RtTheme *t = &s->theme;
  F32 px = uishell_rt_px(t), line = uishell_rt_line_px(t);

  //- The Container Style set: the View's, or by its content.
  UIShell_RtStyleSet *styles = uishell_rt_style_set_from_name(wh_view_setting(ctx, str8_lit("container_styles")));
  if(styles == 0) { styles = uishell_rt_style_set_from_name(s->conversation ? str8_lit("chat") : str8_lit("document")); }
  B32 restyled = styles != s->styles;
  s->styles = styles;
  s->column_stamp += 1;

  //- Backfill a step a frame, within its budget; then the deltas into the
  // model.
  if(s->doc && s->conversation)
  {
    U64 left = 0;
    wheelhouse_rich_text_backfill(s->doc, 0, &left, &error);
    if(left) { rd_request_frame(); }
    if(s->copy_all_pending && left == 0)
    {
      uishell_rt_copy_all(s);
      s->copy_all_pending = 0;
    }
  }
  if(s->doc)
  {
    WheelhouseRichDelta *delta = wheelhouse_rich_text_poll(s->doc, s->sub, &error);
    if(restyled) { uishell_rt_collapse_defaults(s); }
    if(delta)
    {
      uishell_rt_model_apply(&s->model, delta);
      U64 op_count = wheelhouse_rich_delta_op_count(delta);
      for(U64 i = 0; i < op_count; i += 1)
      {
        WheelhouseRichOp op = {0};
        if(wheelhouse_rich_delta_op(delta, i, &op) && op.kind == WHEELHOUSE_RICH_OP_UPSERT)
        {
          UIShell_RtNode *n = uishell_rt_node(&s->model, op.key);
          if(n) { uishell_rt_collapse_default(s, n); }
        }
      }
      wheelhouse_rich_delta_release(delta);
      uishell_rt_prune_tables(s);
      uishell_rt_prune_codes(s);
      for(U64 i = 0; i < s->opened_count;)
      {
        if(uishell_rt_node(&s->model, s->opened[i]) == 0) { s->opened[i] = s->opened[--s->opened_count]; }
        else { i += 1; }
      }
      // A reload starts find's scan again.
      s->match_count = 0;
    }
    uishell_rt_error(&error);
  }

  // A conversation keeps a View Dock at its right (rd_view_dock_ui).
  if(s->conversation) { ctx->rect = rd_view_dock_ui(ctx->settings.view, ctx->rect); }
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
      else if(agent && s->load_error.size) { ui_labelf("Could not watch thread %S: %S", agent_thread, s->load_error); }
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
    if(s->notice_until_us > now_time_us())
    {
      ui_label(str8(s->notice, s->notice_size));
      rd_request_frame_at(s->notice_until_us);
    }
    if(s->conversation && ui_clicked(ui_buttonf("%s###rich_text_copy_all", s->copy_all_pending ? "Copying..." : "Copy All")))
    {
      s->copy_all_pending = 1;
      rd_request_frame();
    }
    if(ui_clicked(ui_buttonf("Source###rich_text_source"))) { uishell_cmd("toggle_markdown_source"); }
  }

  //- Geometry: a reading column, centred, and the composer at its foot
  // when the producer takes input (#683).
  F32 margin = floor_f32(px*1.25f);
  F32 column_dim = dim_2f32(ui_scroll_region_layout(ui_scroll_region_params(r2f32p(0, bar_h, dim.x, dim.y), UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Always)).viewport).x;
  F32 content_width = floor_f32(Clamp(px*8, column_dim - 2*margin, px*48));
  F32 content_x = floor_f32(Max(margin, (column_dim - content_width)/2));
  F32 compose_h = s->input_size ? uishell_rt_composer(s, root, dim.y, content_x, content_width, px, scratch.arena) : 0;
  UI_ScrollRegion region = ui_scroll_region_layout(ui_scroll_region_params(r2f32p(0, bar_h, dim.x, dim.y - compose_h), UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Always));
  Vec2F32 vdim = dim_2f32(region.viewport);
  s->content_x = content_x;
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
      String8 markdown = s->focus_embed ? uishell_rt_embed_copy(scratch.arena, s, s->focus_embed, WHEELHOUSE_RICH_COPY_MARKDOWN, 1)
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

  //- The scroll bar, over the Scroll Anchor's thumb, signalled before the
  // View's own input: an overlay bar lies over the content box, and a press
  // on it is the bar's (#665).
  UI_Key content_key = ui_key_from_stringf(rd_view_ui_key(ctx->settings.view), "###rich_text_content");
  Rng2F32 content_rect = ui_box_from_key(content_key)->rect;
  UIShell_RtPlacement pl = uishell_rt_place(scratch.arena, s, content_x, content_width);
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
  //- "↓ N new" (#541, decision 6), built before the content so it draws
  // over it and takes its press: items arrived below a reader who scrolled
  // up, and a click jumps to the end, which follows again.
  if(!pl.at_end && pl.new_count > 0)
  {
    String8 label = push_str8f(scratch.arena, "%u new", pl.new_count);
    F32 em = ui_top_font_size(), h = floor_f32(line*1.4f);
    F32 w = floor_f32(fnt_dim_from_tag_size_string(ui_top_font(), em, 0, 0, label).x + em*3.2f);
    F32 cx = floor_f32((region.viewport.x0 + region.viewport.x1)/2), bottom = region.viewport.y1 - line;
    UI_Box *jump = &ui_nil_box;
    UI_Parent(root) UI_Rect(r2f32p(cx - w/2, bottom - h, cx + w/2, bottom)) UI_ChildLayoutAxis(Axis2_X)
    {
      jump = ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_Clickable|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|
                                       UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects, "###rich_text_jump_to_end");
    }
    UI_Parent(jump) UI_PrefHeight(ui_px(h, 1)) UI_TextAlignment(UI_TextAlign_Center)
    {
      UI_PrefWidth(ui_em(1.8f, 1)) RD_Font(RD_FontSlot_Icons)
      {
        ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###rt_jump_icon", rd_icon_kind_text_table[RD_IconKind_DownArrow]);
      }
      UI_PrefWidth(ui_pct(1, 0)) ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###rt_jump_label", label);
    }
    if(ui_clicked(ui_signal_from_box(jump)))
    {
      wheelhouse_rich_text_scroll_to_end(s->doc, s->sub, &error);
      uishell_rt_error(&error);
      pl = uishell_rt_place(scratch.arena, s, content_x, content_width);
    }
  }
  UI_ScrollRegionThumbSignal bars = ui_scroll_region_build_thumbs(root, content_key, &region, axes, UI_BoxFlag_Floating);
  UI_ScrollBarSignal bar = bars.bar[Axis2_Y];
  if(bar.kind == UI_ScrollBarActionKind_Drag && bar.drag_moved && bar.drag_origin)
  {
    F64 from = *(F64 *)bar.drag_origin;
    F64 target = Clamp(0.0, from + bar.drag_pct, 1.0)*Max(0.0, pl.total - pl.viewport);
    wheelhouse_rich_text_scroll_by(s->doc, s->sub, target - pl.top, &error);
    pl = uishell_rt_place(scratch.arena, s, content_x, content_width);
  }
  else if(bar.step != 0)
  {
    wheelhouse_rich_text_scroll_by(s->doc, s->sub, bar.page ? bar.step*Max(line, (F32)pl.viewport - line) : bar.step*line*3, &error);
    pl = uishell_rt_place(scratch.arena, s, content_x, content_width);
  }
  uishell_rt_error(&error);

  //- Input, against what was drawn: where the content box was last frame,
  // and the Blocks as they are placed now.
  B32 pressed = 0;
  uishell_rt_pointer(s, &pl, content_rect, &pressed);
  if(pressed)
  {
    uishell_cmd("focus_panel");
    // A press on the content leaves the composer.
    if(s->input_size) { s->compose_blur = 1; }
  }
  B32 focused = 0;
  // While the composer has the keyboard, the content's keys and edit
  // commands are its.
  UI_Focus(UI_FocusKind_On) if(ui_is_focus_active() && !ui_text_field_focus())
  {
    focused = 1;
    UIShell_RtStop *stops = 0;
    U64 stop_count = uishell_rt_stops(scratch.arena, s, &pl, &stops);
    uishell_rt_keys(s, &pl, stops, stop_count);
    uishell_rt_register_edit_owner(content_key, s, ctx->settings.view);
  }

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
  Access *access = access_open();
  Vec2F32 mouse = ui_mouse();
  UI_Parent(bars.content_box)
  {

    //- Headers (#667): the chevron, an icon for the kind, the title, or the
    // name when there is none, then the duration and a status mark. A Tool
    // Run's is its summary, its failures in red.
    B32 spinning = 0;
    for(U64 i = 0; i < pl.count; i += 1)
    {
      UIShell_RtPlaced *p = &pl.v[i];
      if(!p->visible || p->part != WHEELHOUSE_RICH_PART_HEADER || p->style == 0 || !(p->style->chrome & UIShell_RtChrome_Header)) { continue; }
      UIShell_RtNode *n = p->container;
      F32 row = uishell_rt_header_row(t), top = p->rect.y0 + uishell_rt_em(t, p->style->inset_top);
      UIShell_RtHeaderDraw *d = push_array(ui_build_arena(), UIShell_RtHeaderDraw, 1);
      d->key = n->key;
      d->section = n->section;
      d->status = n->status;
      d->collapsed = uishell_rt_collapsed(s, n->key);
      d->hovered = s->hover_key == n->key || contains_2f32(r2f32p(p->rect.x0, top, p->rect.x1, top + row), sub_2f32(ui_mouse(), bars.content_box->rect.p0));
      d->focused = s->focus_stop >= 0 && s->focus_stop_value.key == n->key &&
                   (s->focus_stop_value.kind == UIShell_RtStop_Toggle || s->focus_stop_value.kind == UIShell_RtStop_Thread);
      d->wash = mix_4f32(t->background, t->text, 0.06f);
      d->focus_color = t->focus;
      B32 run = n->kind == WHEELHOUSE_RICH_KIND_TOOL_RUN;
      String8 label = n->title.size ? n->title : n->name, detail = str8_zero(), failed = str8_zero();
      if(n->section == WHEELHOUSE_RICH_SECTION_REASONING) { label = str8_lit("Thinking"); }
      if(n->section == WHEELHOUSE_RICH_SECTION_SUBAGENT) { label = str8_lit("Subagent"); detail = n->title; }
      if(run)
      {
        label = str8_prefix(n->summary, n->failed_at);
        failed = str8_skip(n->summary, n->failed_at);
      }
      String8 duration = n->duration_ms != WHEELHOUSE_RICH_NO_DURATION ? uishell_rt_duration(ui_build_arena(), n->duration_ms) : str8_zero();
      B32 thread = (p->style->chrome & UIShell_RtChrome_Thread) != 0;
      RD_IconKind chevron = thread ? RD_IconKind_RightArrow : d->collapsed ? RD_IconKind_RightCaret : RD_IconKind_DownCaret;
      RD_IconKind status = run ? RD_IconKind_Null : uishell_rt_status_icon(n->status);
      B32 pending = !run && n->status == WHEELHOUSE_RICH_STATUS_PENDING;
      RD_FontSlot icon_slot = RD_FontSlot_Icons;
      String8 icon = uishell_rt_tool_icon(n, &icon_slot);
      UI_Box *box = &ui_nil_box;
      UI_Rect(r2f32p(p->rect.x0, top, p->rect.x1, top + row)) UI_ChildLayoutAxis(Axis2_X)
      {
        box = ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_Clip, "###rt_header_%llx", (unsigned long long)n->key);
      }
      ui_box_equip_custom_draw(box, uishell_rt_header_draw, d);
      UI_Parent(box) UI_PrefHeight(ui_px(row, 1)) UI_TextAlignment(UI_TextAlign_Left)
      {
        // The text takes what the marks leave, as wide as it is and no
        // wider; past that it is cut with an ellipsis.
        F32 em = ui_top_font_size(), pad = floor_f32(em*0.6f);
        FNT_Tag font = ui_top_font();
        F32 duration_w = duration.size ? fnt_dim_from_tag_size_string(font, em, 0, 0, duration).x + pad : 0;
        F32 failed_w = failed.size ? fnt_dim_from_tag_size_string(font, em, 0, 0, failed).x + pad : 0;
        F32 detail_w = detail.size ? fnt_dim_from_tag_size_string(font, em, 0, 0, detail).x + pad : 0;
        F32 room = dim_2f32(box->rect).x - em*(1.6f + 1.6f + 1.8f) - duration_w - failed_w;
        F32 label_w = Clamp(0, fnt_dim_from_tag_size_string(font, em, 0, 0, label).x + pad, room);
        detail_w = Clamp(0, detail_w, room - label_w);
        UI_PrefWidth(ui_em(1.6f, 1)) UI_TagF("weak") RD_Font(RD_FontSlot_Icons) UI_TextAlignment(UI_TextAlign_Center)
        {
          ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###rt_chevron", rd_icon_kind_text_table[chevron]);
        }
        UI_PrefWidth(ui_em(1.6f, 1)) UI_TagF("weak") RD_Font(icon_slot) UI_TextAlignment(UI_TextAlign_Center)
        {
          ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###rt_icon", icon);
        }
        UI_PrefWidth(ui_px(label_w, 1)) ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###rt_label", label);
        if(failed.size) UI_PrefWidth(ui_px(failed_w, 1)) UI_TagF("bad") { ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###rt_failed", failed); }
        if(detail.size) UI_PrefWidth(ui_px(detail_w, 1)) UI_TagF("weak") { ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###rt_detail", detail); }
        ui_spacer(ui_pct(1, 0));
        if(duration.size) UI_PrefWidth(ui_px(duration_w, 1)) UI_TagF("weak") UI_TextAlignment(UI_TextAlign_Right)
        {
          ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###rt_duration", duration);
        }
        if(pending) UI_PrefWidth(ui_em(1.8f, 1))
        {
          UIShell_RtSpinnerDraw *spin = push_array(ui_build_arena(), UIShell_RtSpinnerDraw, 1);
          spin->color = t->weak;
          spin->phase = (now_time_us()/UISHELL_RT_SPINNER_STEP_US) % UISHELL_RT_SPINNER_DOTS;
          ui_box_equip_custom_draw(ui_build_box_from_stringf(0, "###rt_spinner"), uishell_rt_spinner_draw, spin);
          spinning = 1;
        }
        if(status != RD_IconKind_Null) UI_PrefWidth(ui_em(1.8f, 1)) RD_Font(RD_FontSlot_Icons) UI_TextAlignment(UI_TextAlign_Center)
          UI_TagF(uishell_rt_status_bad(n->status) ? "bad" : "weak")
        {
          ui_build_box_from_stringf(UI_BoxFlag_DrawText, "%S###rt_status", rd_icon_kind_text_table[status]);
        }
      }
    }
    if(spinning) { rd_request_frame_at(now_time_us() + UISHELL_RT_SPINNER_STEP_US); }

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
      if(uishell_rt_in_pending_turn(s, p->node)) { d->fade = uishell_rt_pending_wash(t, 0.5f); }
      uishell_rt_overlays(ui_build_arena(), s, p, d, &sel, has_sel, focused);
      UI_Box *box = &ui_nil_box;
      UI_Rect(p->rect)
      {
        box = ui_build_box_from_stringf(UI_BoxFlag_Floating, "###rt_block_%llx", (unsigned long long)p->key);
      }
      ui_box_equip_custom_draw(box, uishell_rt_block_draw, d);
      s->stats.blocks_boxed += 1;
      B32 stop_here = s->focus_stop >= 0 && s->focus_stop_value.key == p->key;
      if(p->frame.code_embed)
      {
        UIShell_RtCode *c = uishell_rt_code(s, p->node);
        B32 capped = 0;
        F32 code_height = uishell_rt_code_height(c, &capped);
        Rng2F32 cr = uishell_rt_code_rect(p, code_height);
        B32 focused_here = s->focus_embed == p->key;
        d->focused = focused_here;
        // Line numbers come on when the whole of it shows.
        c->line_numbers = c->expanded;
        U128 hash = {0};
        TXT_TextInfo info = txt_text_info_from_key_lang(access, c->content, uishell_rt_code_lang(c), &hash);
        String8 data = c_data_from_hash(access, hash);
        F32 row = uishell_text_row_height();
        if(info.lines_count == 0) { rd_request_frame(); }
        else
        {
          // Expanded, it scrolls with the document: only the part in the
          // viewport is built.
          if(c->expanded) { c->scroll.y = 0; }
          if(c->contain_cursor)
          {
            c->contain_cursor = 0;
            F32 y = (c->tv.cursor.line-1)*row;
            if(!c->expanded)
            {
              if(y < c->scroll.y) { c->scroll.y = y; }
              if(y + row > c->scroll.y + code_height) { c->scroll.y = y + row - code_height; }
            }
            F32 advance = fnt_column_size_from_tag_size(rd_font_from_slot(RD_FontSlot_Code), rd_font_size());
            F32 x = c->text_x + (c->tv.cursor.column-1)*advance;
            if(x < c->scroll.x) { c->scroll.x = Max(0, x - advance*4); }
            if(x > c->scroll.x + dim_2f32(cr).x - advance*2) { c->scroll.x = x - dim_2f32(cr).x + advance*4; }
          }
          c->scroll.x = Clamp(0, c->scroll.x, Max(0, c->content_dim.x - dim_2f32(cr).x));
          c->scroll.y = Clamp(0, c->scroll.y, Max(0, row*info.lines_count - code_height));
          F32 viewport_top = -p->rect.y0, viewport_bottom = (F32)pl.viewport - p->rect.y0;
          UIShell_TextEmbedParams params = {0};
          params.info = &info;
          params.data = data;
          params.rect = cr;
          params.visible = r2f32p(cr.x0, Max(cr.y0, viewport_top), cr.x1, Min(cr.y1, viewport_bottom));
          params.scroll = c->scroll;
          params.show_line_numbers = c->line_numbers;
          params.search_query = s->find_active ? str8(s->find_query, s->find_query_size) : str8_zero();
          UIShell_TextEmbedSignal sig = {0};
          UI_Parent(box) { sig = uishell_text_embed(&params, &c->tv, str8_lit("###rt_code")); }
          c->lines_built = sig.lines_built;
          c->content_dim = sig.content_dim;
          c->text_x = sig.text_x;
          if(ui_dragging(sig.base)) { rd_request_frame(); }
        }
        if(p->frame.label_above)
        {
          UI_Parent(box) UI_Rect(r2f32p(p->frame.indent + p->frame.inset_x, p->frame.pad_top, cr.x1, cr.y0)) UI_TagF("weak")
          {
            ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_DrawText, "%S###rt_code_label", p->frame.label);
          }
        }
        // The hover controls, outside it: shown while the pointer is over the
        // Block, or it or one of them has focus.
        Rng2F32 screen = shift_2f32(p->rect, content_rect.p0);
        B32 controls_stop = stop_here && s->focus_stop_value.kind != UIShell_RtStop_Link;
        if(contains_2f32(screen, mouse) || focused_here || controls_stop)
        {
          B32 copied = copied_showing && s->copied_key == p->key;
          struct { U32 slot; UIShell_RtStopKind stop; char *label; char *key; } controls[] =
          {
            {UIShell_RtControl_Copy, UIShell_RtStop_CopyButton, copied ? "Copied" : "Copy", "rt_copy"},
            {UIShell_RtControl_Open, UIShell_RtStop_Open, "Open", "rt_open"},
            {UIShell_RtControl_Expand, UIShell_RtStop_Expand, c->expanded ? "Collapse" : "Expand", "rt_expand"},
          };
          for EachElement(k, controls)
          {
            if(controls[k].slot == UIShell_RtControl_Expand && !capped) { continue; }
            B32 on = stop_here && s->focus_stop_value.kind == controls[k].stop;
            UI_Parent(box) UI_Rect(uishell_rt_control_rect(p, t, controls[k].slot)) UI_TextAlignment(UI_TextAlign_Center) UI_TagF(on ? "hover" : "weak")
            {
              ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_DrawText|UI_BoxFlag_DrawBorder|(on ? UI_BoxFlag_DrawBackground : 0),
                                        "%s###%s", controls[k].label, controls[k].key);
            }
          }
        }
      }
      else if(p->frame.code)
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
          td->draw_text = uishell_rt_table_cell_text;
          s->cell_text = (UIShell_RtCellText){&s->layouts, &s->theme};
          td->text_user = &s->cell_text;
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
      if(p->cap.control)
      {
        B32 toggle_stop = stop_here && s->focus_stop_value.kind == UIShell_RtStop_Expand;
        UI_Parent(box) UI_Rect(uishell_rt_cap_rect(p, t)) UI_TagF(toggle_stop ? "hover" : "weak")
        {
          ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_DrawText|(toggle_stop ? UI_BoxFlag_DrawBorder : 0),
                                    "%s###rt_expand", p->cap.open ? "Collapse" : "Expand");
        }
      }
    }

    //- The hovered Container's actions, over everything.
    UIShell_RtAction actions[3];
    U64 action_count = uishell_rt_actions(s, &pl, actions, ArrayCount(actions));
    for(U64 i = 0; i < action_count; i += 1)
    {
      B32 copied = copied_showing && s->copied_key == actions[i].key && s->copied_action == actions[i].action;
      UI_Rect(actions[i].rect) UI_TextAlignment(UI_TextAlign_Center) UI_TagF("weak")
      {
        ui_build_box_from_stringf(UI_BoxFlag_Floating|UI_BoxFlag_DrawText|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground,
                                  "%S###rt_action_%u", copied ? str8_lit("Copied") : uishell_rt_action_label(actions[i].action), actions[i].action);
      }
    }
    //- Chrome behind the Blocks, built last: RAD draws siblings last to first, so
    // the first box built is drawn on top. A background over a Container's rows,
    // from its header to its bottom inset (or as far as the Window goes).
    {
      U64 cap = 64, count = 0;
      UIShell_RtChromeDraw *chromes = push_array(scratch.arena, UIShell_RtChromeDraw, cap);
      Rng2F32 *rects = push_array(scratch.arena, Rng2F32, cap);
      for(U64 i = 0; i < pl.count; i += 1)
      {
        UIShell_RtPlaced *p = &pl.v[i];
        UIShell_RtNode *n = p->node ? p->node : p->container;
        if(n == 0) { continue; }
        // The row's own Container, then the Containers holding it.
        UIShell_RtContainerStyle *st = p->style;
        UIShell_RtNode *c = p->style ? n : 0;
        if(c == 0) { c = uishell_rt_styled_parent(s, n, &st); }
        for(; c != 0 && count < cap; c = uishell_rt_styled_parent(s, c, &st))
        {
          if(!(st->chrome & UIShell_RtChrome_Background)) { continue; }
          F32 y0 = p->rect.y0, y1 = p->rect.y1;
          if(c == n && p->part == WHEELHOUSE_RICH_PART_FOOTER) { y1 = y0 + uishell_rt_em(t, st->inset_bottom); }
          U64 at = 0;
          for(; at < count && chromes[at].key != c->key; at += 1) {}
          Rng1F32 x = uishell_rt_chrome_column(s, c, st);
          if(at == count)
          {
            B32 pending = c->kind == WHEELHOUSE_RICH_KIND_TURN && c->status == WHEELHOUSE_RICH_STATUS_PENDING;
            Vec4F32 color = pending ? uishell_rt_pending_wash(t, 1) : uishell_rt_bubble_color(t);
            chromes[count] = (UIShell_RtChromeDraw){c->key, color, uishell_rt_em(t, st->radius)};
            rects[count++] = r2f32p(x.min, y0, x.max, y1);
          }
          else
          {
            rects[at].y0 = Min(rects[at].y0, y0);
            rects[at].y1 = Max(rects[at].y1, y1);
          }
        }
      }
      for(U64 i = 0; i < count; i += 1)
      {
        if(rects[i].y1 <= 0 || rects[i].y0 >= pl.viewport) { continue; }
        UIShell_RtChromeDraw *d = push_array(ui_build_arena(), UIShell_RtChromeDraw, 1);
        *d = chromes[i];
        UI_Box *box = &ui_nil_box;
        UI_Rect(rects[i]) { box = ui_build_box_from_stringf(UI_BoxFlag_Floating, "###rt_chrome_%llx", (unsigned long long)d->key); }
        ui_box_equip_custom_draw(box, uishell_rt_chrome_draw, d);
      }
    }
  }
  access_close(access);
  if(s->selecting || s->table_capture || s->open_press) { rd_request_frame(); }
  if(s->conversation && s->first_tail_us == 0 && s->stats.blocks_boxed != 0 && pl.at_end) { s->first_tail_us = now_time_us(); }
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
