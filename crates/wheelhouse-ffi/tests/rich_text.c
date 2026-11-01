/* A C host following Rich Text's delta stream through the ABI (#558).
 *
 * It opens markdown, reloads it through a sequence of edits, and keeps a
 * model built only from deltas for a subscriber that polls every step and
 * one that polls rarely. After each step both must equal a fresh
 * subscription's snapshot. Then it
 * drives a View: the window, heights, scrolling, collapse, selection, copy,
 * find and reveal. Last it opens a conversation in pieces, appends to it
 * and backfills it, and reads its Containers. Behaviour is tested in Rust;
 * this checks what C sees.
 *
 * Arguments: markdown fixture files, and a conversation's .jsonl. */
#include "wheelhouse.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *error;

static void ok(uint32_t result) {
    if (!result) fprintf(stderr, "ABI error: %s\n", error ? error : "(none)");
    assert(result && error == NULL);
}
static void fails(uint32_t result) {
    assert(!result && error);
    wheelhouse_string_free(error);
    error = NULL;
}
static WheelhouseText text(const char *s) { return (WheelhouseText){(const uint8_t *)s, strlen(s)}; }
static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    assert(f);
    assert(fseek(f, 0, SEEK_END) == 0);
    long len = ftell(f);
    assert(len >= 0);
    rewind(f);
    char *data = malloc((size_t)len + 1);
    assert(data && fread(data, 1, (size_t)len, f) == (size_t)len);
    data[len] = 0;
    fclose(f);
    return data;
}

/* A node as a model keeps it: its content and its child list. */
typedef struct {
    uint64_t key;
    uint32_t kind, level, hidden, task, alert, ordered, tight, role, section, tool, status;
    uint64_t version, start;
    char *text;
    size_t text_len;
    WheelhouseRichSpan *spans;
    size_t span_count;
    uint64_t *children;
    size_t child_count;
} Node;

typedef struct {
    Node *nodes;
    size_t count;
    uint64_t generation;
} Model;

static Node *lookup(Model *m, uint64_t key) {
    for (size_t i = 0; i < m->count; ++i)
        if (m->nodes[i].key == key) return &m->nodes[i];
    return NULL;
}

static void drop_node(Node *n) {
    free(n->text);
    free(n->spans);
    free(n->children);
}

static void clear(Model *m) {
    for (size_t i = 0; i < m->count; ++i) drop_node(&m->nodes[i]);
    free(m->nodes);
    m->nodes = NULL;
    m->count = 0;
}

static void set_content(Node *n, const WheelhouseRichNode *c) {
    free(n->text);
    free(n->spans);
    n->key = c->key;
    n->kind = c->kind;
    n->level = c->level;
    n->hidden = c->hidden;
    n->task = c->task;
    n->alert = c->alert;
    n->ordered = c->ordered;
    n->tight = c->tight;
    n->version = c->version;
    n->start = c->start;
    n->role = c->role;
    n->section = c->section;
    n->tool = c->tool;
    n->status = c->status;
    assert(c->name.data && c->title.data);
    assert(c->text.data);
    n->text = malloc(c->text.len + 1);
    memcpy(n->text, c->text.data, c->text.len);
    n->text_len = c->text.len;
    assert(c->spans);
    n->spans = malloc(sizeof *n->spans * (c->span_count + 1));
    memcpy(n->spans, c->spans, sizeof *n->spans * c->span_count);
    n->span_count = c->span_count;
    for (size_t i = 0; i < c->span_count; ++i) {
        assert(c->spans[i].start + c->spans[i].len <= c->text.len);
        if (c->spans[i].attr == WHEELHOUSE_RICH_ATTR_LINK) {
            assert(c->spans[i].value < c->string_count);
            assert(c->strings[c->spans[i].value].data);
        }
    }
    if (c->kind == WHEELHOUSE_RICH_KIND_EMBED && c->column_count) {
        for (size_t i = 0; i < c->row_count * c->column_count; ++i)
            assert(c->cells[i].start + c->cells[i].len <= c->text.len);
    }
}

static void apply(Model *m, WheelhouseRichDelta *d) {
    if (wheelhouse_rich_delta_is_snapshot(d)) clear(m);
    uint64_t generation = wheelhouse_rich_delta_generation(d);
    assert(generation >= m->generation);
    m->generation = generation;
    WheelhouseRichOp op;
    WheelhouseRichNode node;
    for (size_t i = 0; i < wheelhouse_rich_delta_op_count(d); ++i) {
        assert(wheelhouse_rich_delta_op(d, i, &op));
        assert(wheelhouse_rich_delta_node(d, i, &node) == (op.kind == WHEELHOUSE_RICH_OP_UPSERT));
        Node *n = lookup(m, op.key);
        switch (op.kind) {
        case WHEELHOUSE_RICH_OP_UPSERT:
            assert(node.key == op.key);
            if (!n) {
                m->nodes = realloc(m->nodes, sizeof *m->nodes * (m->count + 1));
                n = &m->nodes[m->count++];
                memset(n, 0, sizeof *n);
            }
            set_content(n, &node);
            break;
        case WHEELHOUSE_RICH_OP_REMOVE:
            assert(n);
            drop_node(n);
            *n = m->nodes[--m->count];
            break;
        case WHEELHOUSE_RICH_OP_CHILDREN:
            assert(n);
            for (size_t j = 0; j < op.list_count; ++j) {
                WheelhouseRichListOp l = op.list[j];
                if (l.kind == WHEELHOUSE_LIST_INSERT) {
                    assert(l.at <= n->child_count && l.from == WHEELHOUSE_NONE && l.to == WHEELHOUSE_NONE);
                    n->children = realloc(n->children, sizeof *n->children * (n->child_count + 1));
                    memmove(&n->children[l.at + 1], &n->children[l.at], sizeof *n->children * (n->child_count - l.at));
                    n->children[l.at] = l.key;
                    n->child_count++;
                } else if (l.kind == WHEELHOUSE_LIST_REMOVE) {
                    assert(l.at < n->child_count);
                    memmove(&n->children[l.at], &n->children[l.at + 1], sizeof *n->children * (n->child_count - l.at - 1));
                    n->child_count--;
                } else {
                    assert(l.kind == WHEELHOUSE_LIST_MOVE && l.from < n->child_count && l.to < n->child_count);
                    uint64_t key = n->children[l.from];
                    memmove(&n->children[l.from], &n->children[l.from + 1], sizeof *n->children * (n->child_count - l.from - 1));
                    memmove(&n->children[l.to + 1], &n->children[l.to], sizeof *n->children * (n->child_count - 1 - l.to));
                    n->children[l.to] = key;
                }
            }
            break;
        default:
            assert(0 && "unknown op");
        }
    }
}

static void same_node(const Node *a, const Node *b) {
    assert(a->key == b->key && a->kind == b->kind && a->version == b->version);
    assert(a->level == b->level && a->hidden == b->hidden && a->task == b->task && a->alert == b->alert);
    assert(a->ordered == b->ordered && a->tight == b->tight && a->start == b->start);
    assert(a->role == b->role && a->section == b->section && a->tool == b->tool && a->status == b->status);
    assert(a->text_len == b->text_len && memcmp(a->text, b->text, a->text_len) == 0);
    assert(a->span_count == b->span_count);
    for (size_t i = 0; i < a->span_count; ++i) {
        assert(a->spans[i].start == b->spans[i].start && a->spans[i].len == b->spans[i].len);
        assert(a->spans[i].attr == b->spans[i].attr && a->spans[i].value == b->spans[i].value);
    }
    assert(a->child_count == b->child_count);
    for (size_t i = 0; i < a->child_count; ++i) assert(a->children[i] == b->children[i]);
}

static void same(Model *a, Model *b) {
    assert(a->count == b->count);
    for (size_t i = 0; i < a->count; ++i) {
        Node *other = lookup(b, a->nodes[i].key);
        assert(other);
        same_node(&a->nodes[i], other);
    }
}

static void follow(WheelhouseRichText *h, uint64_t sub, Model *m) {
    WheelhouseRichDelta *d = wheelhouse_rich_text_poll(h, sub, &error);
    assert(!error);
    if (d) {
        apply(m, d);
        wheelhouse_rich_delta_release(d);
    }
    assert(!wheelhouse_rich_text_changed_since(h, sub, &error) && !error);
    assert(m->generation == wheelhouse_rich_text_generation(h));
}

static Model fresh(WheelhouseRichText *h) {
    uint64_t sub;
    ok(wheelhouse_rich_text_subscribe(h, &sub, &error));
    Model m = {0};
    WheelhouseRichDelta *d = wheelhouse_rich_text_poll(h, sub, &error);
    assert(d && !error && wheelhouse_rich_delta_is_snapshot(d));
    apply(&m, d);
    wheelhouse_rich_delta_release(d);
    ok(wheelhouse_rich_text_unsubscribe(h, sub, &error));
    return m;
}

static void check(WheelhouseRichText *h, Model *followed) {
    Model snapshot = fresh(h);
    same(followed, &snapshot);
    clear(&snapshot);
}

/* A document made by cutting and joining the fixtures at blank lines. */
static char *variant(char **fixtures, int count, unsigned step) {
    const char *a = fixtures[step % count];
    const char *b = fixtures[(step / 2 + 1) % count];
    size_t cut_a = strlen(a), cut_b = 0;
    /* Cut a at its (step % 5)th blank line from the end; take b from its
     * (step % 3)th blank line. */
    for (unsigned n = step % 5; n > 0 && cut_a > 0; --n) {
        const char *p = NULL;
        for (const char *q = a; (q = strstr(q, "\n\n")) && (size_t)(q - a) < cut_a - 1; ++q) p = q;
        if (!p) break;
        cut_a = (size_t)(p - a) + 1;
    }
    for (unsigned n = step % 3; n > 0; --n) {
        const char *q = strstr(b + cut_b, "\n\n");
        if (!q) break;
        cut_b = (size_t)(q - b) + 2;
    }
    size_t len_b = step % 2 ? strlen(b) - cut_b : 0;
    char *out = malloc(cut_a + 2 + len_b + 1);
    memcpy(out, a, cut_a);
    memcpy(out + cut_a, "\n\n", 2);
    memcpy(out + cut_a + 2, b + cut_b, len_b);
    out[cut_a + 2 + len_b] = 0;
    return out;
}

static WheelhouseBytes copy(WheelhouseRichText *h, uint64_t sub, uint32_t format) {
    WheelhouseBytes out = {0};
    ok(wheelhouse_rich_text_copy(h, sub, format, &out, &error));
    return out;
}

static int contains(WheelhouseBytes b, const char *s) {
    size_t n = strlen(s);
    for (size_t i = 0; i + n <= b.len; ++i)
        if (memcmp(b.data + i, s, n) == 0) return 1;
    return 0;
}

static void drive_view(const char *markdown) {
    WheelhouseRichText *h = wheelhouse_rich_text_open(text(markdown), &error);
    assert(h && !error);
    uint64_t sub;
    ok(wheelhouse_rich_text_subscribe(h, &sub, &error));
    ok(wheelhouse_rich_text_set_metrics(h, sub, 400.0f, 10.0f, 40.0f, &error));
    ok(wheelhouse_rich_text_set_viewport(h, sub, 100.0, &error));
    fails(wheelhouse_rich_text_set_viewport(h, sub, NAN, &error));
    fails(wheelhouse_rich_text_set_metrics(h, sub, INFINITY, 10.0f, 40.0f, &error));

    WheelhouseRichWindow window;
    ok(wheelhouse_rich_text_window(h, sub, &window, &error));
    assert(window.top == 0.0 && window.viewport == 100.0 && window.total > 100.0 && window.entry_count > 0);
    /* Measure what's in the window: each Block one line taller than its
     * estimate. */
    double grown = 0;
    WheelhouseRichWindowEntry first = window.entries[0];
    size_t measured = window.entry_count;
    WheelhouseRichWindowEntry *entries = malloc(sizeof *entries * measured);
    memcpy(entries, window.entries, sizeof *entries * measured);
    for (size_t i = 0; i < measured; ++i) {
        assert(!entries[i].measured);
        ok(wheelhouse_rich_text_set_height(h, sub, entries[i].key, entries[i].part, entries[i].height + 10.0f, 400.0f, &error));
        grown += 10.0;
    }
    ok(wheelhouse_rich_text_window(h, sub, &window, &error));
    assert(window.entries[0].key == first.key && window.entries[0].measured);
    double total = window.total;
    (void)grown;
    free(entries);
    fails(wheelhouse_rich_text_set_height(h, sub, 12345, WHEELHOUSE_RICH_PART_BLOCK, 1.0f, 400.0f, &error));
    fails(wheelhouse_rich_text_set_height(h, sub, first.key, 7, 1.0f, 400.0f, &error));

    /* Scroll down, then grow everything above the anchor: it holds. */
    ok(wheelhouse_rich_text_scroll_by(h, sub, 150.0, &error));
    WheelhouseRichAnchor anchor;
    assert(wheelhouse_rich_text_anchor(h, sub, &anchor, &error) && !error);
    ok(wheelhouse_rich_text_window(h, sub, &window, &error));
    assert(window.top == 150.0);
    ok(wheelhouse_rich_text_set_height(h, sub, first.key, first.part, 200.0f, 400.0f, &error));
    ok(wheelhouse_rich_text_window(h, sub, &window, &error));
    assert(window.total > total);
    WheelhouseRichAnchor after;
    assert(wheelhouse_rich_text_anchor(h, sub, &after, &error));
    assert(after.key == anchor.key && after.byte == anchor.byte);
    if (first.key != anchor.key) assert(window.top > 150.0);

    WheelhouseRichThumb thumb;
    ok(wheelhouse_rich_text_thumb(h, sub, &thumb, &error));
    assert(thumb.size > 0 && thumb.size < 1 && thumb.estimated > 0 && thumb.estimated < 1);
    ok(wheelhouse_rich_text_scroll_to_end(h, sub, &error));
    ok(wheelhouse_rich_text_window(h, sub, &window, &error));
    assert(window.at_end && window.top == window.total - window.viewport);
    ok(wheelhouse_rich_text_scroll_by(h, sub, -5.0, &error));
    ok(wheelhouse_rich_text_window(h, sub, &window, &error));
    assert(!window.at_end);
    WheelhouseRichAnchor to = {first.key, WHEELHOUSE_RICH_PART_BLOCK, WHEELHOUSE_RICH_POSITION_TEXT, 0, 0.0f, 0.0f, 0.0};
    ok(wheelhouse_rich_text_scroll_to(h, sub, &to, &error));
    ok(wheelhouse_rich_text_set_anchor_text(h, sub, 0, 0.0f, &error));
    ok(wheelhouse_rich_text_relayout_anchor(h, sub, 0.0f, &error));
    ok(wheelhouse_rich_text_window(h, sub, &window, &error));
    assert(window.top == 0.0);

    /* Selection and copy. */
    WheelhouseRichSelection selection;
    assert(!wheelhouse_rich_text_selection(h, sub, &selection, &error) && !error);
    WheelhouseBytes none = copy(h, sub, WHEELHOUSE_RICH_COPY_PLAIN);
    assert(none.len == 0);
    wheelhouse_bytes_free(none);
    ok(wheelhouse_rich_text_select_all(h, sub, &error));
    assert(wheelhouse_rich_text_selection(h, sub, &selection, &error));
    WheelhouseBytes plain = copy(h, sub, WHEELHOUSE_RICH_COPY_PLAIN);
    WheelhouseBytes html = copy(h, sub, WHEELHOUSE_RICH_COPY_HTML);
    WheelhouseBytes md = copy(h, sub, WHEELHOUSE_RICH_COPY_MARKDOWN);
    assert(plain.len > 0 && contains(html, "<h1>") && md.len > 0);
    fails(wheelhouse_rich_text_copy(h, sub, 9, &plain, &error));
    wheelhouse_bytes_free(plain);
    wheelhouse_bytes_free(html);
    wheelhouse_bytes_free(md);
    ok(wheelhouse_rich_text_select(h, sub, selection.anchor_key, 0, selection.anchor_key, 3, &error));
    ok(wheelhouse_rich_text_extend(h, sub, selection.anchor_key, 5, &error));
    assert(wheelhouse_rich_text_selection(h, sub, &selection, &error));
    assert(selection.anchor_byte == 0 && selection.focus_byte == 5);
    ok(wheelhouse_rich_text_select_block(h, sub, selection.anchor_key, &error));
    WheelhouseBytes block = {0};
    ok(wheelhouse_rich_text_copy_block(h, selection.anchor_key, WHEELHOUSE_RICH_COPY_PLAIN, &block, &error));
    WheelhouseBytes selected = copy(h, sub, WHEELHOUSE_RICH_COPY_PLAIN);
    assert(block.len == selected.len && memcmp(block.data, selected.data, block.len) == 0);
    wheelhouse_bytes_free(block);
    wheelhouse_bytes_free(selected);
    ok(wheelhouse_rich_text_clear_selection(h, sub, &error));
    fails(wheelhouse_rich_text_select(h, sub, WHEELHOUSE_RICH_ROOT_KEY, 0, WHEELHOUSE_RICH_ROOT_KEY, 0, &error));

    /* Find, collapse and reveal: the first list holds "item". */
    ok(wheelhouse_rich_text_find(h, sub, text("ITEM"), 0, &error));
    WheelhouseRichFindStatus status;
    ok(wheelhouse_rich_text_find_step(h, sub, 1, &status, &error));
    while (!status.done) ok(wheelhouse_rich_text_find_step(h, sub, 1, &status, &error));
    assert(status.count > 0 && !status.capped && status.current == WHEELHOUSE_NONE);
    WheelhouseRichMatch match;
    ok(wheelhouse_rich_text_find_match(h, sub, 0, &match, &error));
    fails(wheelhouse_rich_text_find_match(h, sub, status.count, &match, &error));
    uint64_t list = 0;
    Model model = fresh(h);
    Node *root = lookup(&model, WHEELHOUSE_RICH_ROOT_KEY);
    for (size_t i = 0; root && i < root->child_count && !list; ++i)
        if (lookup(&model, root->children[i])->kind == WHEELHOUSE_RICH_KIND_LIST) list = root->children[i];
    clear(&model);
    assert(list);
    ok(wheelhouse_rich_text_set_collapsed(h, sub, list, 1, &error));
    WheelhouseRichMatch moved;
    assert(wheelhouse_rich_text_find_next(h, sub, 1, &moved, &error));
    assert(moved.key == match.key && moved.start == match.start);
    ok(wheelhouse_rich_text_find_step(h, sub, 1, &status, &error));
    assert(status.current == 0);
    assert(wheelhouse_rich_text_anchor(h, sub, &anchor, &error));
    assert(anchor.key == match.key && anchor.viewport_offset > 33.0 && anchor.viewport_offset < 34.0);
    ok(wheelhouse_rich_text_reveal(h, sub, first.key, 0, &error));
    ok(wheelhouse_rich_text_find_end(h, sub, &error));
    fails(wheelhouse_rich_text_find_step(h, sub, 1, &status, &error));
    fails(wheelhouse_rich_text_find(h, sub, text("("), WHEELHOUSE_RICH_FIND_REGEX, &error));
    fails(wheelhouse_rich_text_set_collapsed(h, sub, WHEELHOUSE_RICH_ROOT_KEY, 1, &error));

    fails(wheelhouse_rich_text_window(h, sub + 100, &window, &error));
    ok(wheelhouse_rich_text_unsubscribe(h, sub, &error));
    fails(wheelhouse_rich_text_poll(h, sub, &error) != NULL);
    wheelhouse_rich_text_free(h);
}

static WheelhouseText bytes(const char *data, size_t len) { return (WheelhouseText){(const uint8_t *)data, len}; }

static void drive_conversation(const char *jsonl) {
    /* Open its first third, then append the rest in pieces, backfilling once
     * a poll as a frontend does. */
    size_t len = strlen(jsonl), head = len / 3;
    WheelhouseRichText *h = wheelhouse_rich_text_open_conversation(bytes(jsonl, head), &error);
    assert(h && !error);
    uint64_t sub, left;
    ok(wheelhouse_rich_text_subscribe(h, &sub, &error));
    Model m = {0};
    follow(h, sub, &m);
    check(h, &m);
    for (size_t at = head; at < len; at += 97) {
        uint32_t changed = 2;
        ok(wheelhouse_rich_text_append(h, bytes(jsonl + at, len - at < 97 ? len - at : 97), &changed, &error));
        assert(changed <= 1);
        ok(wheelhouse_rich_text_backfill(h, 0, &left, &error));
        follow(h, sub, &m);
        check(h, &m);
    }
    assert(left == 0);
    /* A Container's fields read by key as its delta gave them. */
    size_t sections = 0, turns = 0, logs = 0;
    WheelhouseRichContainer c;
    for (size_t i = 0; i < m.count; ++i) {
        Node *n = &m.nodes[i];
        ok(wheelhouse_rich_text_container(h, n->key, &c, &error));
        assert(c.kind == n->kind && c.role == n->role && c.section == n->section && c.tool == n->tool && c.status == n->status);
        sections += n->kind == WHEELHOUSE_RICH_KIND_SECTION && n->section != WHEELHOUSE_RICH_SECTION_NONE && n->status != WHEELHOUSE_RICH_STATUS_NONE;
        turns += n->kind == WHEELHOUSE_RICH_KIND_TURN && n->role != WHEELHOUSE_RICH_ROLE_NONE;
        logs += n->kind == WHEELHOUSE_RICH_KIND_LOG;
    }
    assert(sections > 0 && turns > 0 && logs > 0);
    uint64_t absent = 12345;
    while (lookup(&m, absent)) ++absent;
    fails(wheelhouse_rich_text_container(h, absent, &c, &error));
    clear(&m);
    m.generation = 0;
    wheelhouse_rich_text_free(h);

    /* A large transcript: its tail at once, then backfill to its start. */
    WheelhouseBytes big = {0};
    ok(wheelhouse_rich_text_generate_conversation(7, 4u << 20, &big, &error));
    h = wheelhouse_rich_text_open_conversation(bytes((const char *)big.data, big.len), &error);
    assert(h && !error);
    ok(wheelhouse_rich_text_subscribe(h, &sub, &error));
    follow(h, sub, &m);
    size_t tail = m.count;
    unsigned steps = 0;
    do {
        ok(wheelhouse_rich_text_backfill(h, 0, &left, &error));
        follow(h, sub, &m);
        ++steps;
    } while (left);
    check(h, &m);
    assert(steps > 1 && m.count > tail);
    printf("Conversation: a %zu-byte transcript's tail of %zu nodes, then %zu nodes after %u backfill steps.\n", big.len, tail, m.count, steps);
    clear(&m);
    wheelhouse_bytes_free(big);
    wheelhouse_rich_text_free(h);

    /* Markdown has nothing to append to or backfill. */
    h = wheelhouse_rich_text_open(text("# ka"), &error);
    fails(wheelhouse_rich_text_append(h, text("{}\n"), NULL, &error));
    ok(wheelhouse_rich_text_backfill(h, 0, &left, &error));
    assert(left == 0);
    wheelhouse_rich_text_free(h);
}

int main(int argc, char **argv) {
    assert(argc > 1);
    char **fixtures = calloc((size_t)argc, sizeof *fixtures);
    char *conversation = NULL;
    int count = 0;
    for (int i = 1; i < argc; ++i) {
        size_t n = strlen(argv[i]);
        if (n > 6 && strcmp(argv[i] + n - 6, ".jsonl") == 0) conversation = read_file(argv[i]);
        else fixtures[count++] = read_file(argv[i]);
    }
    assert(count > 0 && conversation);

    assert(!wheelhouse_rich_text_open((WheelhouseText){NULL, 1}, &error) && error);
    wheelhouse_string_free(error);
    error = NULL;
    WheelhouseRichText *h = wheelhouse_rich_text_open(text(fixtures[0]), &error);
    assert(h && wheelhouse_rich_text_generation(h) == 1);
    uint64_t every, rarely;
    ok(wheelhouse_rich_text_subscribe(h, &every, &error));
    ok(wheelhouse_rich_text_subscribe(h, &rarely, &error));
    assert(every != rarely);
    Model each = {0}, rare = {0};
    follow(h, every, &each);
    check(h, &each);
    size_t ops = 0;
    for (unsigned step = 0; step < 40; ++step) {
        char *source = variant(fixtures, count, step);
        uint32_t changed = 2;
        ok(wheelhouse_rich_text_reload(h, text(source), &changed, &error));
        assert(changed <= 1);
        assert(wheelhouse_rich_text_changed_since(h, every, &error) == changed);
        WheelhouseRichDelta *d = wheelhouse_rich_text_poll(h, every, &error);
        assert(!error && (d != NULL) == changed);
        if (d) {
            ops += wheelhouse_rich_delta_op_count(d);
            apply(&each, d);
            wheelhouse_rich_delta_release(d);
        }
        check(h, &each);
        if (step % 3 == 2) follow(h, rarely, &rare);
        free(source);
    }
    follow(h, rarely, &rare);
    check(h, &rare);
    same(&each, &rare);
    assert(ops > 0);

    /* Reloading the same source changes nothing. */
    uint32_t changed;
    char *source = variant(fixtures, count, 39);
    ok(wheelhouse_rich_text_reload(h, text(source), &changed, &error));
    assert(!changed && !wheelhouse_rich_text_poll(h, every, &error) && !error);
    free(source);

    /* NULL and unknown handles. */
    fails(wheelhouse_rich_text_poll(NULL, every, &error) != NULL);
    fails(wheelhouse_rich_text_changed_since(h, 999, &error));
    assert(!wheelhouse_rich_text_unsubscribe(h, 999, &error) && !error);
    WheelhouseRichNode node;
    assert(!wheelhouse_rich_delta_node(NULL, 0, &node));
    assert(wheelhouse_rich_delta_op_count(NULL) == 0);
    wheelhouse_rich_delta_release(NULL);
    wheelhouse_rich_text_free(NULL);
    clear(&each);
    clear(&rare);
    wheelhouse_rich_text_free(h);

    drive_view(fixtures[0]);
    drive_conversation(conversation);
    for (int i = 0; i < count; ++i) free(fixtures[i]);
    free(fixtures);
    free(conversation);
    printf("Rich Text: followed %zu ops over 40 reloads through the C ABI; models match snapshots.\n", ops);
    return 0;
}
