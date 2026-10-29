/* A C host against the generated header: one happy path per entry point,
 * and what only C sees: struct layouts, ownership, NULL handling and error
 * strings. Behaviour is tested in Rust. */
#include "wheelhouse.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define T(s) ((WheelhouseText){(const uint8_t *)(s), sizeof(s) - 1})
#define PROVIDER T("sub-1")
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
static int eq(WheelhouseText a, const char *b) {
    return a.data && a.len == strlen(b) && memcmp(a.data, b, a.len) == 0;
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
static WheelhouseCore *create(const char *config) {
    WheelhouseCore *h = wheelhouse_create((const uint8_t *)config, strlen(config), &error);
    assert(h && !error);
    return h;
}
static void apply_lines(WheelhouseCore *h, const char *path, uint64_t now) {
    char *patches = read_file(path);
    for (char *line = strtok(patches, "\n"); line; line = strtok(NULL, "\n"))
        ok(wheelhouse_apply_patch_json(h, now, PROVIDER, text(line), &error));
    free(patches);
}
static WheelhouseSnapshot *snapshot(WheelhouseCore *h) {
    WheelhouseSnapshot *s = wheelhouse_snapshot_acquire(h, &error);
    assert(s && !error);
    return s;
}
static size_t find(WheelhouseSnapshot *s, const char *kind, WheelhouseNode *out) {
    for (size_t i = 0; i < wheelhouse_snapshot_node_count(s); ++i) {
        assert(wheelhouse_snapshot_node(s, i, out));
        if (!out->is_section && eq(out->entity.kind, kind)) return i;
    }
    assert(!"missing node");
    return WHEELHOUSE_NONE;
}
static WheelhouseWorkspaceId uuid(uint8_t tail) {
    return (WheelhouseWorkspaceId){{0x01, 0x92, 0x0a, 0x6b, 0x7c, 0x3d, 0x7e, 0x4f,
                                   0x8a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f, 0x6a, tail}};
}
static int same(WheelhouseWorkspaceId a, WheelhouseWorkspaceId b) {
    return memcmp(a.bytes, b.bytes, sizeof a.bytes) == 0;
}

/* Facts, topology, the snapshot, dispatch, effects and completion. */
static void check_sidebar(const char *config_path, const char *patches_path) {
    assert(wheelhouse_abi_version() == 12);
    char *config = read_file(config_path);
    WheelhouseCore *h = create(config);
    ok(wheelhouse_configure(h, text(config), &error));
    free(config);
    apply_lines(h, patches_path, 100);
    /* Every entity names its provider; "local" is the core's own. */
    fails(wheelhouse_apply_patch_json(h, 100, T(""), T("{}"), &error));
    fails(wheelhouse_apply_patch_json(h, 100, T("local"), T("{\"target\":{\"kind\":\"root\"},\"source_id\":\"x\",\"set\":{}}"), &error));
    WheelhouseFact facts[] = {{.key = T("display.label"), .kind = WHEELHOUSE_FACT_TEXT, .text = T("Pee")},
                             {.key = T("gone"), .kind = WHEELHOUSE_FACT_UNSET}};
    ok(wheelhouse_apply_entity(h, 100, PROVIDER, T("project"), T("q"), T("fixture"), facts, 2, &error));
    WheelhouseFact twice[] = {facts[0], facts[0]};
    fails(wheelhouse_apply_entity(h, 100, PROVIDER, T("project"), T("q"), T("fixture"), twice, 2, &error));
    WheelhouseWorkspaceId notes = uuid(0x7b), worker = uuid(0x7c);
    ok(wheelhouse_workspace_register(h, notes, &error));
    assert(wheelhouse_workspace_registered(h, notes, &error) && !error);
    WheelhouseWorkspace open[] = {{notes, 0, T("Notes"), 1}};
    ok(wheelhouse_observe(h, open, 1, NULL, 0, &error));
    fails(wheelhouse_observe(h, NULL, 1, NULL, 0, &error));
    WheelhouseFact host = {.key = T(".host.kind"), .kind = WHEELHOUSE_FACT_TEXT, .text = T(".workspace")};
    ok(wheelhouse_apply_workspace(h, 100, PROVIDER, notes, T("host"), &host, 1, &error));
    ok(wheelhouse_tick(h, 101, &error));

    WheelhouseSnapshot *s = snapshot(h);
    ok(wheelhouse_snapshot_is_current(h, s, &error));
    WheelhouseNode section, n;
    assert(wheelhouse_snapshot_node(s, 0, &section) && section.is_section && section.parent == WHEELHOUSE_NONE);
    assert(section.entity.kind.len == 0 && section.entity.kind.data != NULL);
    assert(!wheelhouse_snapshot_node(s, WHEELHOUSE_NONE, &n) && !wheelhouse_snapshot_node(s, 0, NULL));
    size_t vessel = find(s, "vessel", &n);
    assert(eq(n.entity.provider, "sub-1") && eq(n.entity.id, "v") && n.state == WHEELHOUSE_LATENT);
    assert(n.loop_key.len && n.copy_url == WHEELHOUSE_NONE && !n.stale);
    WheelhouseField field;
    assert(wheelhouse_snapshot_field(s, n.first_field, &field) && field.class_ <= WHEELHOUSE_FIELD_PRIORITY);
    WheelhouseControl control = {0};
    for (size_t i = 0; i < wheelhouse_snapshot_node_count(s); ++i) {
        WheelhouseNode m;
        assert(wheelhouse_snapshot_node(s, i, &m));
        for (size_t j = 0; j < m.control_count; ++j)
            if (wheelhouse_snapshot_control(s, m.first_control + j, &control) &&
                control.kind == WHEELHOUSE_CONTROL_DISPLAY_VARIABLE) goto found;
    }
    assert(!"no display control");
found:
    assert(eq(control.variable, "show-issues") && control.persist && control.value_kind == WHEELHOUSE_VALUE_BOOL);
    WheelhouseText diagnostic;
    for (size_t i = 0; i < wheelhouse_snapshot_diagnostic_count(s); ++i)
        assert(wheelhouse_snapshot_diagnostic(s, i, &diagnostic) && diagnostic.len);

    /* Activating v materializes it; completing gives its workspace. */
    ok(wheelhouse_dispatch(h, s, n.activate, &error));
    fails(wheelhouse_dispatch(h, s, WHEELHOUSE_NONE, &error));
    WheelhouseEffects *effects = wheelhouse_effects_take(h, &error);
    assert(effects && !error && wheelhouse_effects_count(effects) == 1);
    WheelhouseEffect e;
    assert(wheelhouse_effects_get(effects, 0, &e) && e.kind == WHEELHOUSE_EFFECT_MATERIALIZE);
    assert(eq(e.entity.provider, "sub-1") && eq(e.entity.id, "v") && eq(e.recipe, "printf hello"));
    assert(!wheelhouse_effects_get(effects, 1, &e));
    ok(wheelhouse_complete(h, e.request_id, WHEELHOUSE_COMPLETE_MATERIALIZE, worker, T(""), &error));
    wheelhouse_effects_release(effects);
    wheelhouse_snapshot_release(s); /* the dispatch made it stale */
    WheelhouseWorkspace both[] = {{notes, 0, T("Notes"), 0}, {worker, 1, T("Worker"), 1}};
    WheelhousePane pane = {worker, 7, WHEELHOUSE_PANE_TERMINAL, 1, 1, 0};
    ok(wheelhouse_observe(h, both, 2, &pane, 1, &error));
    WheelhouseWorkdir dir = {worker, T("/repo")};
    ok(wheelhouse_observe_workdirs(h, &dir, 1, &error));

    s = wheelhouse_snapshot_acquire_details(h, &error);
    assert(s && !error);
    vessel = find(s, "vessel", &n);
    assert(n.state == WHEELHOUSE_LIVE && same(n.workspace_id, worker));
    WheelhouseEntity v = {T("sub-1"), T("vessel"), T("v")};
    size_t detail = wheelhouse_snapshot_detail_find(s, v);
    WheelhouseDetail card;
    assert(wheelhouse_snapshot_detail(s, detail, &card) && card.has_workspace && same(card.workspace_id, worker));
    WheelhouseDetailAction action;
    assert(wheelhouse_snapshot_detail_action(s, detail, 0, &action) && eq(action.intent, "focus-workspace"));
    WheelhouseDetailField detail_field;
    assert(wheelhouse_snapshot_detail_field(s, detail, 0, &detail_field));
    WheelhouseEntity other = {T("sub-2"), T("vessel"), T("v")};
    assert(wheelhouse_snapshot_detail_find(s, other) == WHEELHOUSE_NONE);
    assert(wheelhouse_snapshot_detail_request(h, s, v, &error) == detail && !error);
    ok(wheelhouse_dispatch(h, s, card.activate, &error));
    effects = wheelhouse_effects_take(h, &error);
    assert(wheelhouse_effects_get(effects, 0, &e) && e.kind == WHEELHOUSE_EFFECT_FOCUS && same(e.workspace_id, worker));
    ok(wheelhouse_complete(h, e.request_id, WHEELHOUSE_COMPLETE_FOCUS, e.workspace_id, T(""), &error));
    fails(wheelhouse_complete(h, e.request_id, 9, worker, T(""), &error));
    wheelhouse_effects_release(effects);
    fails(wheelhouse_copy_subject_url(h, (WheelhouseEntity){T(""), T("vessel"), T("v")}, &error));

    /* The snapshot outlives the sidebar; its text stays readable. */
    wheelhouse_destroy(h);
    assert(wheelhouse_snapshot_node(s, vessel, &n) && eq(n.entity.id, "v"));
    wheelhouse_snapshot_release(s);
}

/* Records, display variables, local entities and sibling orders. */
static void check_dashboard(const char *config_path, const char *patches_path) {
    char *config = read_file(config_path);
    WheelhouseCore *h = create(config);
    apply_lines(h, patches_path, 100);
    ok(wheelhouse_set_display_variable(h, T("show-issues"), T("false"), &error));
    fails(wheelhouse_set_display_variable(h, T("show-issues"), T("maybe"), &error));
    WheelhouseLocalFact label = {.key = T("display.label"), .kind = WHEELHOUSE_FACT_TEXT, .text = T("Mine")};
    ok(wheelhouse_local_set(h, T(".section"), T("s1"), &label, 1, &error));
    WheelhouseLocalFact group[] = {
        {.key = T(".section"), .kind = WHEELHOUSE_FACT_ENTITY, .entity = {T("local"), T(".section"), T("s1")}},
        {.key = T(".position"), .kind = WHEELHOUSE_FACT_INTEGER, .integer = 2},
    };
    ok(wheelhouse_local_set(h, T(".group"), T("g1"), group, 2, &error));
    group[0].entity.provider = T("");
    fails(wheelhouse_local_set(h, T(".group"), T("g2"), group, 1, &error));
    fails(wheelhouse_local_set(h, T("vessel"), T("v"), &label, 1, &error));
    ok(wheelhouse_local_remove(h, T(".group"), T("never"), &error));
    WheelhouseSnapshot *s = snapshot(h);
    WheelhouseNode n;
    find(s, "vessel", &n);
    WheelhouseEntity order[] = {{T("sub-1"), T("vessel"), T("v")}};
    ok(wheelhouse_set_sibling_order(h, n.loop_key, order, 1, &error));
    fails(wheelhouse_set_sibling_order(h, T("not a loop key"), order, 1, &error));
    /* What the Dashboard keeps: local entities with their facts, and orders. */
    WheelhouseDashboard *kept = wheelhouse_dashboard_get(h, &error);
    assert(kept && !error && kept->generation == wheelhouse_record_generation(h, T("dashboard"), &error));
    assert(kept->local_count == 2 && kept->order_count == 1);
    const WheelhouseLocalEntity *g1 = &kept->locals[0];
    assert(eq(g1->entity.provider, "local") && eq(g1->entity.kind, ".group") && g1->fact_count == 2);
    assert(eq(g1->facts[0].key, ".position") && g1->facts[0].kind == WHEELHOUSE_FACT_INTEGER && g1->facts[0].integer == 2);
    assert(g1->facts[1].kind == WHEELHOUSE_FACT_ENTITY && eq(g1->facts[1].entity.id, "s1"));
    assert(eq(kept->locals[1].facts[0].text, "Mine"));
    assert(kept->orders[0].loop_key.len == n.loop_key.len &&
           memcmp(kept->orders[0].loop_key.data, n.loop_key.data, n.loop_key.len) == 0);
    assert(kept->orders[0].entity_count == 1 && eq(kept->orders[0].entities[0].id, "v"));
    wheelhouse_dashboard_free(kept);
    wheelhouse_dashboard_free(NULL);
    wheelhouse_snapshot_release(s);
    ok(wheelhouse_workspace_register(h, uuid(0x7d), &error));

    WheelhouseBytes names = {0};
    ok(wheelhouse_record_names(h, &names, &error));
    const char *expected = "dashboard\nworkspace/01920a6b-7c3d-7e4f-8a1b-2c3d4e5f6a7d";
    assert(names.len == strlen(expected) && memcmp(names.data, expected, names.len) == 0);
    wheelhouse_bytes_free(names);
    WheelhouseText workspace = T("workspace/01920a6b-7c3d-7e4f-8a1b-2c3d4e5f6a7d");
    uint64_t generation = wheelhouse_record_generation(h, T("dashboard"), &error);
    assert(generation && !error && wheelhouse_record_generation(h, T("dashboard"), &error) == generation);
    assert(wheelhouse_record_generation(h, T("workspace/9"), &error) == 0);
    fails(0);
    WheelhouseBytes dashboard = {0}, record = {0}, none = {0};
    ok(wheelhouse_record_get(h, T("dashboard"), &dashboard, &error));
    ok(wheelhouse_record_get(h, workspace, &record, &error));
    fails(wheelhouse_record_get(h, T("nonsense"), &none, &error));
    fails(wheelhouse_record_get(h, T("dashboard"), NULL, &error));
    assert(none.data == NULL);

    WheelhouseCore *fresh = create(config);
    free(config);
    ok(wheelhouse_record_put(fresh, T("dashboard"), (WheelhouseText){dashboard.data, dashboard.len}, &error));
    ok(wheelhouse_record_put(fresh, workspace, (WheelhouseText){record.data, record.len}, &error));
    assert(wheelhouse_workspace_registered(fresh, uuid(0x7d), &error));
    fails(wheelhouse_record_put(fresh, T("dashboard"), T("{"), &error));
    ok(wheelhouse_workspace_forget(fresh, uuid(0x7d), &error));
    ok(wheelhouse_workspace_forget(fresh, uuid(0x7d), &error));
    wheelhouse_bytes_free(dashboard);
    wheelhouse_bytes_free(record);
    wheelhouse_bytes_free(none);
    wheelhouse_destroy(fresh);
    wheelhouse_destroy(h);
}

/* Providers: two subscriptions' same entity, staleness and retraction. */
static void check_providers(void) {
    WheelhouseCore *h = create("region \"tree\" root-template=\"flotilla/region/tree\" placement=\"tree\"\n"
                          "placement \"tree\" { for \"p\" kind=\"project\"; }\n");
    WheelhouseFact label = {.key = T("display.label"), .kind = WHEELHOUSE_FACT_TEXT, .text = T("One")};
    ok(wheelhouse_apply_entity(h, 100, T("sub-1"), T("project"), T("p"), T("x"), &label, 1, &error));
    ok(wheelhouse_apply_entity(h, 100, T("sub-2"), T("project"), T("p"), T("x"), &label, 1, &error));
    ok(wheelhouse_provider_set_stale(h, T("sub-1"), 1, &error));
    ok(wheelhouse_provider_retract(h, T("sub-2"), &error));
    WheelhouseSnapshot *s = snapshot(h);
    size_t projects = 0;
    for (size_t i = 0; i < wheelhouse_snapshot_node_count(s); ++i) {
        WheelhouseNode n;
        assert(wheelhouse_snapshot_node(s, i, &n));
        if (n.is_section) continue;
        projects++;
        assert(eq(n.entity.provider, "sub-1") && n.stale); /* sub-2's retracted */
    }
    assert(projects == 1);
    wheelhouse_snapshot_release(s);
    ok(wheelhouse_provider_retract(h, T("sub-1"), &error));
    fails(wheelhouse_provider_retract(h, T(""), &error));
    wheelhouse_destroy(h);
}

/* Slots, plans, resolutions, the overlay and arrangements. */
static void check_workspace(const char *config_path) {
    char *config = read_file(config_path);
    WheelhouseCore *h = create(config);
    free(config);
    WheelhouseWorkspaceId ws = uuid(0x7e);
    ok(wheelhouse_workspace_register(h, ws, &error));
    WheelhouseWorkspace open = {ws, 0, T("Scratch"), 1};
    ok(wheelhouse_observe(h, &open, 1, NULL, 0, &error));
    uint64_t revision = wheelhouse_workspace_content_revision(h, &error);
    WheelhouseText argv[] = {T("htop"), T("-d")};
    WheelhouseViewSpec spec = {.content = WHEELHOUSE_SLOT_COMMAND, .argv = argv, .argc = 2,
                              .has_presentation = 1, .presentation = T("terminal"),
                              .has_role = 1, .role = T("shell")};
    ok(wheelhouse_slot_set(h, ws, T("u:1"), &spec, WHEELHOUSE_REBIND_KEEP_PREVIOUS, &error));
    WheelhouseViewSpec facet = {.content = WHEELHOUSE_SLOT_FACET, .entity = {T("sub-1"), T("vessel"), T("v")},
                               .facet = T("primary")};
    ok(wheelhouse_slot_set(h, ws, T("u:2"), &facet, WHEELHOUSE_REBIND_ASK, &error));
    facet.entity.provider = T("");
    fails(wheelhouse_slot_set(h, ws, T("u:3"), &facet, WHEELHOUSE_REBIND_ASK, &error));
    fails(wheelhouse_slot_set(h, ws, T("u:3"), &spec, 7, &error));
    fails(wheelhouse_slot_set(h, uuid(0x01), T("u:3"), &spec, WHEELHOUSE_REBIND_REPLACE, &error));
    assert(wheelhouse_workspace_content_revision(h, &error) > revision);

    WheelhouseSlots *slots = wheelhouse_slots_get(h, ws, &error);
    assert(slots && !error && slots->slot_count == 2);
    const WheelhouseSlot *slot = &slots->slots[0];
    assert(eq(slot->key, "u:1") && slot->flags == 0 && !slot->has_saved);
    assert(slot->spec.argc == 2 && eq(slot->spec.argv[1], "-d") && eq(slot->spec.presentation, "terminal"));
    assert(slot->spec.has_role && eq(slot->spec.role, "shell") && !slots->slots[1].spec.has_role);
    assert(eq(slots->slots[1].spec.entity.provider, "sub-1"));
    wheelhouse_slots_free(slots);
    assert(!wheelhouse_slots_get(h, uuid(0x01), &error));
    fails(0);

    /* Plan, validate, complete; save a portable resolution for it. */
    WheelhouseSlotContent *plan = wheelhouse_slot_plan(h, ws, T("u:1"), T(""), &error);
    assert(plan && !error && plan->state == WHEELHOUSE_CONTENT_UPDATING);
    assert(plan->recipe.argc == 2 && !plan->has_saved);
    assert(wheelhouse_slot_valid(h, ws, T("u:1"), plan->token, &error));
    assert(wheelhouse_slot_complete(h, ws, T("u:1"), plan->token, 1, &error));
    WheelhouseResolutionField fields[] = {{T("session"), T("S")}, {T("host"), T("feta")}};
    uint64_t generation = 0;
    assert(wheelhouse_slot_resolution_set(h, ws, T("u:1"), T("cleat-session"), fields, 2, plan->resolution, 0,
                                         &generation, &error) == WHEELHOUSE_COMMITTED && generation);
    assert(wheelhouse_slot_resolution_set(h, ws, T("u:1"), T("cleat-session"), fields, 1, plan->resolution, 0,
                                         &generation, &error) == WHEELHOUSE_STALE && !error);
    assert(wheelhouse_slot_resolution_set(h, ws, T("u:1"), T("Bad Kind"), fields, 2, plan->resolution,
                                         generation, NULL, &error) == WHEELHOUSE_INVALID);
    fails(0);
    WheelhouseSlotContent *current = wheelhouse_slot_plan(h, ws, T("u:1"), plan->resolution, &error);
    assert(current && current->state == WHEELHOUSE_CONTENT_CURRENT);
    assert(current->has_saved && current->saved.field_count == 2 && eq(current->saved.fields[0].name, "host"));
    wheelhouse_slot_plan_free(current);
    wheelhouse_slot_plan_free(plan);
    slots = wheelhouse_slots_get(h, ws, &error);
    assert(slots->slots[0].has_saved && slots->slots[0].saved.generation == generation);
    wheelhouse_slots_free(slots);
    assert(wheelhouse_slot_resolution_clear(h, ws, T("u:1"), generation, &generation, &error) == WHEELHOUSE_COMMITTED);
    ok(wheelhouse_slot_retry(h, ws, T("u:1"), &error));
    assert(!wheelhouse_slot_release_previous(h, ws, T("u:1"), &error) && !error);

    /* The overlay and the user's name and mood. */
    ok(wheelhouse_workspace_set_name(h, ws, 1, T("Build"), &error));
    ok(wheelhouse_workspace_set_mood(h, ws, 0, T(""), &error));
    ok(wheelhouse_slot_remove(h, ws, T("u:2"), &error));
    ok(wheelhouse_slot_reattach(h, ws, T("u:1"), &error));
    WheelhouseOverlay *overlay = wheelhouse_overlay_get(h, ws, &error);
    assert(overlay && !error && !overlay->has_baseline && eq(overlay->name, "Build"));
    assert(!overlay->has_mood && overlay->mood.data && overlay->note_count == 0);
    assert(!overlay->has_subject_name && overlay->subject_name.data); /* No subject. */
    wheelhouse_overlay_free(overlay);
    assert(!wheelhouse_overlay_get(h, uuid(0x01), &error));
    fails(0);

    /* The arrangement: panels name their parents; commit, stale, invalid. */
    WheelhouseWorkspaceArrangement *a = wheelhouse_arrangement_get(h, ws, &error);
    assert(a && !error && a->doc.panel_count == 0 && a->generation == 0 && !a->has_layout && a->layout.data);
    wheelhouse_arrangement_free(a);

    /* Without a subject there are no layouts to list or choose. */
    WheelhouseLayoutList *layouts = wheelhouse_layouts_get(h, ws, &error);
    assert(layouts && !error && layouts->count == 0);
    wheelhouse_layouts_free(layouts);
    assert(!wheelhouse_layouts_get(h, uuid(0x01), &error));
    fails(0);
    fails(wheelhouse_layout_choose(h, ws, 1, T("work"), &error));
    ok(wheelhouse_layout_choose(h, ws, 0, T(""), &error));
    WheelhouseText tabs[] = {T("u:1")};
    WheelhouseDocPanel panels[] = {
        {WHEELHOUSE_NONE, T("root"), 1.0, WHEELHOUSE_PANEL_SPLIT, WHEELHOUSE_AXIS_ROW, NULL, 0, WHEELHOUSE_NONE},
        {0, T("1"), 1.0, WHEELHOUSE_PANEL_TABS, 0, tabs, 1, 0},
    };
    WheelhouseArrangementDoc doc = {panels, 2};
    a = wheelhouse_arrangement_commit(h, ws, &doc, 0, &error);
    assert(a && !error && a->status == WHEELHOUSE_COMMITTED && a->owned && a->doc.panel_count == 2);
    assert(a->doc.panels[1].parent == 0 && eq(a->doc.panels[1].tabs[0], "u:1") && a->doc.panels[1].selected == 0);
    uint64_t at = a->generation;
    wheelhouse_arrangement_free(a);
    a = wheelhouse_arrangement_commit(h, ws, &doc, 0, &error);
    assert(a && !error && a->status == WHEELHOUSE_STALE && a->generation == at);
    wheelhouse_arrangement_free(a);
    panels[1].parent = 1;
    assert(!wheelhouse_arrangement_commit(h, ws, &doc, at, &error));
    fails(0);
    assert(wheelhouse_arrangement_resolve(h, ws, WHEELHOUSE_ARRANGEMENT_KEEP, at, &generation, &error) ==
           WHEELHOUSE_COMMITTED);

    /* The sidebar's arrangement: the same panels, in a dock. */
    WheelhouseSidebarArrangement *sidebar = wheelhouse_sidebar_arrangement_get(h, &error);
    assert(sidebar && !error && sidebar->doc.dock.panel_count && sidebar->note_count);
    assert(sidebar->generation == wheelhouse_sidebar_arrangement_generation(h, &error));
    WheelhouseSidebarArrangement *committed =
        wheelhouse_sidebar_arrangement_commit(h, &sidebar->doc, sidebar->generation, &error);
    assert(committed && !error && committed->status == WHEELHOUSE_COMMITTED && committed->owned);
    wheelhouse_sidebar_arrangement_free(committed);
    committed = wheelhouse_sidebar_reset(h, sidebar->generation, &error);
    assert(committed && !error && committed->status == WHEELHOUSE_STALE);
    generation = committed->generation;
    wheelhouse_sidebar_arrangement_free(committed);
    wheelhouse_sidebar_arrangement_free(sidebar);
    assert(!wheelhouse_sidebar_restore_section(h, T("undeclared"), generation, &error));
    fails(0);
    committed = wheelhouse_sidebar_reset(h, generation, &error);
    assert(committed && committed->status == WHEELHOUSE_COMMITTED && !committed->owned);
    wheelhouse_sidebar_arrangement_free(committed);

    wheelhouse_slots_free(NULL);
    wheelhouse_slot_plan_free(NULL);
    wheelhouse_overlay_free(NULL);
    wheelhouse_arrangement_free(NULL);
    wheelhouse_sidebar_arrangement_free(NULL);
    wheelhouse_destroy(h);
}

/* Handles, NULLs and poisoning. */
static void check_handles(void) {
    assert(!wheelhouse_create(NULL, 1, &error));
    fails(0);
    assert(!wheelhouse_create((const uint8_t *)"region", 6, &error));
    fails(0);
    fails(wheelhouse_tick(NULL, 0, &error));
    assert(wheelhouse_snapshot_node_count(NULL) == 0 && wheelhouse_effects_count(NULL) == 0);
    wheelhouse_snapshot_release(NULL);
    wheelhouse_effects_release(NULL);
    wheelhouse_destroy(NULL);
    wheelhouse_string_free(NULL);
    wheelhouse_bytes_free((WheelhouseBytes){0});
    WheelhouseCore *h = wheelhouse_create(NULL, 0, NULL);
    assert(h && wheelhouse_tick(h, 0, NULL));
    wheelhouse_destroy(h);
}

/* A stale sidebar commit names every section placed since the host last
   committed, not only what the last reconcile placed (wheelhouse#372): the
   host's retry would otherwise close a section placed mid-gesture. */
static int placed(const WheelhouseSidebarArrangement *a, const char *key) {
    for (size_t i = 0; i < a->note_count; ++i)
        if (a->notes[i].kind == WHEELHOUSE_NOTE_PLACED && eq(a->notes[i].key, key)) return 1;
    return 0;
}
static void check_stale_sidebar_placements(void) {
    const char *tree = "region \"tree\" root-template=\"flotilla/region/tree\" placement=\"tree\" order=10\n"
                       "placement \"tree\" { for \"p\" kind=\"project\"; }\n";
    const char *more = "region \"tree\" root-template=\"flotilla/region/tree\" placement=\"tree\" order=10\n"
                       "placement \"tree\" { for \"p\" kind=\"project\"; }\n"
                       "region \"more\" root-template=\"flotilla/region/tree\" placement=\"more\" order=20\n"
                       "placement \"more\" { for \"p\" kind=\"project\"; }\n";
    WheelhouseCore *h = create(tree);
    WheelhouseSidebarArrangement *a = wheelhouse_sidebar_arrangement_get(h, &error);
    assert(a && !error);
    WheelhouseSidebarArrangement *host = wheelhouse_sidebar_arrangement_commit(h, &a->doc, a->generation, &error);
    assert(host && !error && host->status == WHEELHOUSE_COMMITTED && !placed(host, "more"));
    wheelhouse_sidebar_arrangement_free(a);
    /* Mid-gesture: "more" is declared and placed, then a reconcile that
       places nothing (configure always reconciles; the second has nothing
       new to place, so its report's placed set is empty). */
    ok(wheelhouse_configure(h, text(more), &error));
    ok(wheelhouse_configure(h, text(more), &error));
    WheelhouseSidebarArrangement *stale = wheelhouse_sidebar_arrangement_commit(h, &host->doc, host->generation, &error);
    assert(stale && !error && stale->status == WHEELHOUSE_STALE && placed(stale, "more"));
    wheelhouse_sidebar_arrangement_free(stale);
    wheelhouse_sidebar_arrangement_free(host);
    wheelhouse_destroy(h);
}

/* The change stream: subscribe, poll a delta and read it, apply an intent
 * by a key the delta gave, and see it listed; NULL handling. */
static void check_stream(const char *config_path, const char *patches_path) {
    char *config = read_file(config_path);
    WheelhouseCore *h = create(config);
    free(config);
    uint64_t subscription = 0;
    ok(wheelhouse_subscribe(h, &subscription, &error));
    apply_lines(h, patches_path, 100);
    assert(wheelhouse_changed_since(h, subscription, &error) && !error);
    WheelhouseDelta *d = wheelhouse_poll(h, subscription, &error);
    assert(d && !error && wheelhouse_delta_generation(d) > 0);
    WheelhouseText key = {0};
    for (size_t i = 0; i < wheelhouse_delta_op_count(d); ++i) {
        WheelhouseDeltaOp op;
        assert(wheelhouse_delta_op(d, i, &op));
        if (op.kind != WHEELHOUSE_OP_UPSERT) continue;
        WheelhouseStreamNode node;
        assert(wheelhouse_delta_node(d, op.first, &node) && !node.is_section);
        assert(node.key.len == op.key.len && memcmp(node.key.data, op.key.data, op.key.len) == 0);
        assert(node.status.data && node.icon.data && node.short_label.data && node.medium_label.data);
        for (size_t f = node.first_field; f < node.first_field + node.field_count; ++f) {
            WheelhouseField field;
            assert(wheelhouse_delta_field(d, f, &field) && field.text.data);
        }
        if (!key.len) key = op.key;
    }
    assert(key.len);
    WheelhouseIntent intent = {WHEELHOUSE_INTENT_TOGGLE_PLACEMENT, key, {T(""), T(""), T("")}, T(""), 0, T("")};
    uint64_t request = 0;
    assert(wheelhouse_apply_intent(h, &intent, &request, &error) == WHEELHOUSE_INTENT_ACCEPTED && !error && request);
    wheelhouse_delta_release(d); /* the key was borrowed from it until now */
    d = wheelhouse_poll(h, subscription, &error);
    assert(d && !error && wheelhouse_delta_intent_count(d) == 1);
    uint64_t listed = 0;
    assert(wheelhouse_delta_intent(d, 0, &listed) && listed == request);
    assert(!wheelhouse_delta_intent(d, 1, &listed));
    wheelhouse_delta_release(d);
    assert(!wheelhouse_poll(h, subscription, &error) && !error);
    intent.kind = 99;
    assert(wheelhouse_apply_intent(h, &intent, &request, &error) == WHEELHOUSE_INTENT_INVALID);
    fails(0);
    ok(wheelhouse_unsubscribe(h, subscription, &error));
    assert(wheelhouse_delta_op_count(NULL) == 0 && wheelhouse_delta_generation(NULL) == 0);
    wheelhouse_delta_release(NULL);
    assert(!wheelhouse_poll(NULL, 0, &error));
    fails(0);
    wheelhouse_destroy(h);
}
int main(int argc, char **argv) {
    assert(argc == 3);
    check_sidebar(argv[1], argv[2]);
    check_dashboard(argv[1], argv[2]);
    check_providers();
    check_stale_sidebar_placements();
    check_workspace(argv[1]);
    check_handles();
    check_stream(argv[1], argv[2]);
    puts("C ABI smoke test passed");
    return 0;
}
