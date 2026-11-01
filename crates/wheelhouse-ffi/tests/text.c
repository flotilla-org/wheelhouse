/* A C host laying out text through wh_text_layout: an arena allocator,
 * the bundled fonts from the data directory (argv[1]), and the output
 * arrays read back as C structs. Layout behaviour is tested in Rust
 * (crates/wheelhouse-text/tests); this checks what only C sees. */
/* The checks are asserts: keep them in a release build too. */
#undef NDEBUG
#include "wheelhouse.h"
#include <assert.h>
#include <stdalign.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A bump arena: wh_text_layout's arrays land here and go when it resets. */
typedef struct {
    unsigned char *base;
    size_t used, size, calls;
} Arena;

static void *arena_alloc(void *user, size_t size, size_t align) {
    Arena *a = user;
    size_t at = (a->used + align - 1) / align * align;
    if (at + size > a->size) return NULL;
    a->used = at + size;
    a->calls++;
    return a->base + at;
}

static WhTextAllocator allocator(Arena *a) { return (WhTextAllocator){arena_alloc, a}; }

/* Fails its fail_at'th allocation. */
typedef struct {
    Arena *arena;
    size_t calls, fail_at;
} Failing;

static void *failing_alloc(void *user, size_t size, size_t align) {
    Failing *f = user;
    if (++f->calls == f->fail_at) return NULL;
    return arena_alloc(f->arena, size, align);
}

/* Breaks the rule that alloc must not call back into the library. */
typedef struct {
    Arena *arena;
    const WhText *ctx;
    const WhTextInput *input;
    int calls, nested_failed;
} Reentrant;

static void *reentrant_alloc(void *user, size_t size, size_t align) {
    Reentrant *r = user;
    if (r->calls++ == 0) {
        WhTextLayout inner;
        char *error = NULL;
        r->nested_failed = wh_text_layout(r->ctx, r->input, 200.0f, allocator(r->arena), &inner, &error) == 0 && error;
        wheelhouse_string_free(error);
    }
    return arena_alloc(r->arena, size, align);
}

static unsigned char *read_file(const char *dir, const char *name, size_t *len) {
    char path[4096];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long n = ftell(f);
    assert(n > 0);
    rewind(f);
    unsigned char *data = malloc((size_t)n);
    assert(data && fread(data, 1, (size_t)n, f) == (size_t)n);
    fclose(f);
    *len = (size_t)n;
    return data;
}

static WheelhouseText text(const char *s) { return (WheelhouseText){(const uint8_t *)s, strlen(s)}; }

static const char *FONTS[] = {
    "AtkinsonHyperlegibleNext-Regular.ttf", "AtkinsonHyperlegibleNext-SemiBold.ttf", "JetBrainsMonoNerdFont-Regular.ttf",
    "icons.ttf", "NotoSansSymbols2-Regular.ttf", "NotoSansMath-Regular.ttf", "NotoSansSymbols.ttf", "NotoColorEmoji.ttf",
};

int main(int argc, char **argv) {
    assert(argc == 2);
    WhText *ctx = wh_text_create(0);
    assert(ctx);
    uint64_t epoch = wh_text_epoch(ctx);
    for (size_t i = 0; i < sizeof FONTS / sizeof *FONTS; ++i) {
        size_t len;
        unsigned char *data = read_file(argv[1], FONTS[i], &len);
        assert(wh_text_register_font(ctx, data, len, 100 + i) >= 1);
        /* The context copied the bytes. */
        memset(data, 0, len);
        free(data);
    }
    assert(wh_text_register_font(ctx, (const uint8_t *)"not a font", 10, 1) == 0);
    assert(wh_text_epoch(ctx) > epoch);
    assert(wh_text_set_family(ctx, WH_TEXT_FAMILY_ICONS + 1, text("x")) == 0);
    assert(wh_text_set_family(ctx, WH_TEXT_FAMILY_CODE, text("JetBrainsMono Nerd Font")) == 1);
    assert(wh_text_set_scale(ctx, 0.0f) == 0);
    assert(wh_text_set_scale(ctx, 2.0f) == 1);

    Arena arena = {malloc(1 << 20), 0, 1 << 20, 0};
    char *error = NULL;
    const char *s = "Wide text that wraps, then code()";
    WhTextFeature no_ligatures = {{'l', 'i', 'g', 'a'}, 0};
    WhTextStyle styles[] = {
        {WH_TEXT_FAMILY_MAIN, 16.0f, 400.0f, 0, NULL, 0, 0.0f},
        {WH_TEXT_FAMILY_CODE, 13.0f, 700.0f, 0, &no_ligatures, 1, 0.0f},
    };
    WhTextStyleRun runs[] = {{27, 6, 1}};
    WhTextInlineBox boxes[] = {{42, 5, 10.0f, 10.0f, 10.0f}};
    WhTextInput input = {text(s), runs, 1, styles, 2, boxes, 1, 0, 0, WH_TEXT_ALIGN_START, 0};
    WhTextLayout layout;
    assert(wh_text_layout(ctx, &input, 200.0f, allocator(&arena), &layout, &error) == 1 && !error);
    assert(arena.calls == 5);
    assert(layout.line_count >= 2 && layout.epoch == wh_text_epoch(ctx));

    /* Lines tile the text, and each line's clusters tile its bytes. */
    uint32_t at = 0;
    for (size_t i = 0; i < layout.line_count; ++i) {
        WhTextLine *line = &layout.lines[i];
        assert(line->text_start == at);
        assert(line->width <= 200.0f + 0.01f && line->height > 0.0f);
        assert(line->cluster_start + line->cluster_count <= layout.cluster_count);
        for (uint32_t c = 0; c < line->cluster_count; ++c) {
            WhTextCluster *cluster = &layout.clusters[line->cluster_start + c];
            assert(cluster->line == i && cluster->start == at);
            at += cluster->len;
        }
        assert(line->text_end == at);
    }
    assert(at == strlen(s));

    /* The last run is the code: bold synthesised at twice 13pt. */
    WhTextRun *code = &layout.runs[layout.run_count - 1];
    assert(code->style == 1 && code->size == 26.0f && code->embolden == 1);
    assert(code->glyph_start + code->glyph_count == layout.glyph_count);
    WhTextFont font;
    assert(wh_text_font(ctx, code->font, &font) == 1);
    assert(font.kind == WH_TEXT_FONT_BYTES && font.bytes_id == 102 && font.face_index == 0 && font.data && font.data_len > 0);
    assert(wh_text_font(ctx, 1000, &font) == 0);
    const int16_t *coords;
    assert(wh_text_coords(ctx, 0, &coords) == 0);

    /* The box sits on the first line, after "Wide ", scaled. */
    assert(layout.box_count == 1 && layout.boxes[0].id == 42 && layout.boxes[0].width == 20.0f && layout.boxes[0].line == 0);

    /* A caret hit-tests back to its offset. */
    WhTextCaret caret;
    WhTextPosition hit;
    assert(wh_text_caret_rect(&layout, 2, WH_TEXT_UPSTREAM + 1, &caret) == 0);
    assert(wh_text_caret_rect(&layout, 2, WH_TEXT_DOWNSTREAM, &caret) == 1);
    assert(caret.line == 0 && caret.x > 0.0f && caret.height == layout.lines[0].height && !caret.rtl);
    assert(wh_text_offset_at(&layout, caret.x, caret.y + caret.height / 2, &hit) == 1);
    assert(hit.offset == 2);

    /* A selection of it all covers every line; the box splits the first
     * line's in two. */
    WhTextRects rects;
    assert(wh_text_selection_rects(&layout, 0, (uint32_t)strlen(s), allocator(&arena), &rects) == 1);
    assert(rects.count == layout.line_count + 1);
    assert(rects.rects[0].line == 0 && rects.rects[1].line == 0 && rects.rects[1].x >= layout.boxes[0].x + layout.boxes[0].width);
    for (size_t i = 1; i < rects.count; ++i) assert(rects.rects[i].y == layout.lines[rects.rects[i].line].y);
    WhTextSegments segments;
    assert(wh_text_segments(&layout, allocator(&arena), &segments) == 1);
    assert(segments.count >= layout.line_count && segments.segments[segments.count - 1].style == 1);

    /* Face and coordinates ids, and the bytes they name, outlive rebuilding the collection. */
    const uint8_t *code_data = font.data;
    wh_text_fonts_changed(ctx);
    assert(wh_text_set_scale(ctx, 1.0f) == 1);
    assert(wh_text_font(ctx, code->font, &font) == 1 && font.data == code_data && font.bytes_id == 102);
    WhTextLayout relaid;
    assert(wh_text_layout(ctx, &input, 200.0f, allocator(&arena), &relaid, &error) == 1);
    assert(wh_text_font(ctx, code->font, &font) == 1 && font.data == code_data && font.bytes_id == 102);
    assert(wh_text_set_scale(ctx, 2.0f) == 1);

    /* A nested call from the allocator fails with an error instead of corrupting the outer one. */
    Reentrant reentrant = {&arena, ctx, &input, 0, 0};
    WhTextLayout outer;
    assert(wh_text_layout(ctx, &input, 200.0f, (WhTextAllocator){reentrant_alloc, &reentrant}, &outer, &error) == 1);
    assert(reentrant.nested_failed && reentrant.calls == 5);
    assert(outer.line_count == layout.line_count && outer.glyph_count == layout.glyph_count);

    /* A corrupt layout is refused, not read past or overflowed. */
    WhTextLayout corrupt = layout;
    WhTextCluster bad_cluster = layout.clusters[0];
    bad_cluster.len = UINT32_MAX;
    corrupt.clusters = &bad_cluster;
    corrupt.cluster_count = 1;
    assert(wh_text_caret_rect(&corrupt, 0, WH_TEXT_DOWNSTREAM, &caret) == 0);
    corrupt = layout;
    corrupt.cluster_count = 1;
    assert(wh_text_offset_at(&corrupt, 0.0f, 0.0f, &hit) == 0);

    /* A cut line ends with an ellipsis covering no bytes. */
    input.max_lines = 1;
    input.ellipsis = 1;
    assert(wh_text_layout(ctx, &input, 200.0f, allocator(&arena), &layout, &error) == 1);
    WhTextCluster *last = &layout.clusters[layout.cluster_count - 1];
    assert(layout.line_count == 1 && (last->flags & WH_TEXT_CLUSTER_ELLIPSIS) && last->len == 0);

    /* Errors leave the output alone. */
    WhTextLayout untouched = layout;
    WhTextStyleRun bad[] = {{30, 10, 0}};
    input.runs = bad;
    assert(wh_text_layout(ctx, &input, 200.0f, allocator(&arena), &layout, &error) == 0 && error);
    assert(memcmp(&untouched, &layout, sizeof layout) == 0);
    wheelhouse_string_free(error);
    error = NULL;
    WhTextStyleRun unknown_style[] = {{0, 4, 2}};
    input.runs = unknown_style;
    assert(wh_text_layout(ctx, &input, 200.0f, allocator(&arena), &layout, &error) == 0 && error);
    wheelhouse_string_free(error);
    error = NULL;
    input.runs = runs;
    assert(wh_text_layout(ctx, &input, 200.0f, (WhTextAllocator){NULL, NULL}, &layout, &error) == 0 && error);
    assert(memcmp(&untouched, &layout, sizeof layout) == 0);
    wheelhouse_string_free(error);
    error = NULL;
    for (size_t fail_at = 2; fail_at <= 5; ++fail_at) {
        Failing failing = {&arena, 0, fail_at};
        assert(wh_text_layout(ctx, &input, 200.0f, (WhTextAllocator){failing_alloc, &failing}, &layout, &error) == 0 && error);
        assert(failing.calls == fail_at && memcmp(&untouched, &layout, sizeof layout) == 0);
        wheelhouse_string_free(error);
        error = NULL;
    }
    Arena tiny = {malloc(16), 0, 16, 0};
    assert(wh_text_layout(ctx, &input, 200.0f, allocator(&tiny), &layout, &error) == 0 && error);
    wheelhouse_string_free(error);
    error = NULL;
    assert(wh_text_layout(NULL, &input, 200.0f, allocator(&arena), &layout, &error) == 0 && error);
    wheelhouse_string_free(error);
    error = NULL;
    input.align = WH_TEXT_ALIGN_JUSTIFY + 1;
    assert(wh_text_layout(ctx, &input, 200.0f, allocator(&arena), &layout, &error) == 0 && error);
    wheelhouse_string_free(error);
    error = NULL;
    input.align = WH_TEXT_ALIGN_START;
    no_ligatures.value = 65536;
    assert(wh_text_layout(ctx, &input, 200.0f, allocator(&arena), &layout, &error) == 0 && error);
    wheelhouse_string_free(error);
    error = NULL;
    no_ligatures.value = 0;

    /* A tab is a whitespace cluster to the next stop; RTL text says so, and ends lines at the right. */
    const char *tabbed = "a\tb";
    WhTextInput tabs = {text(tabbed), NULL, 0, styles + 1, 1, NULL, 0, 0, 0, WH_TEXT_ALIGN_END, 2};
    assert(wh_text_layout(ctx, &tabs, 400.0f, allocator(&arena), &layout, &error) == 1);
    WhTextCluster *tab = &layout.clusters[1];
    assert(layout.cluster_count == 3 && (tab->flags & WH_TEXT_CLUSTER_TAB) && (tab->flags & WH_TEXT_CLUSTER_WHITESPACE));
    assert(tab->x + tab->advance - layout.clusters[0].x > layout.clusters[0].advance * 1.99f && !layout.rtl);
    assert(layout.lines[0].x + layout.lines[0].width > 399.0f && layout.runs[0].underline_size > 0.0f);
    const char *hebrew = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d";
    WhTextInput rtl = {text(hebrew), NULL, 0, styles, 1, NULL, 0, 0, 0, WH_TEXT_ALIGN_START, 0};
    assert(wh_text_layout(ctx, &rtl, 400.0f, allocator(&arena), &layout, &error) == 1 && layout.rtl);

    /* Empty text is one empty line, with no arrays to allocate but lines. */
    input = (WhTextInput){text(""), NULL, 0, styles, 1, NULL, 0, 0, 0, WH_TEXT_ALIGN_START, 0};
    arena.calls = 0;
    assert(wh_text_layout(ctx, &input, 200.0f, allocator(&arena), &layout, &error) == 1);
    assert(layout.line_count == 1 && layout.cluster_count == 0 && !layout.clusters && arena.calls == 1);

    free(tiny.base);
    free(arena.base);
    wh_text_free(ctx);
    puts("C text layout test passed");
    return 0;
}
