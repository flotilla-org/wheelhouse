// Native andamento control surface. Included after the workspace helpers.
#include "andamento.h"
#include "uishell/uishell_sidebar_chips.h"
#include "ingress/ingress.h"
global WheelhouseIngress *uishell_ingress;
global String8 uishell_sidebar_live_config;
global B32 uishell_sidebar_live;
global B32 uishell_sidebar_fixture;
global B32 uishell_sidebar_subject_fixture;
global U64 uishell_sidebar_last_tick;
// Unity-build benchmark controls: declared here for the adapter and set by
// uishell_main.c only for native diagnostics. Normal frames avoid clock queries.
global B32 uishell_sidebar_benchmark_active;
global B32 uishell_sidebar_benchmark_uncached;
global U64 uishell_sidebar_analysis_us, uishell_sidebar_context_us;
global U64 uishell_sidebar_benchmark_issues = 100;
// Optional fixture-only hit rectangles for the external mouse/clipboard test.
global String8 uishell_sidebar_subject_geometry_path;
global String8List uishell_sidebar_subject_geometry;

// The first resource is primary. This descriptor is local fixture input, not
// an andamento wire format. Persist IDs on tabs independently of their labels.
typedef struct UIShell_SidebarResource UIShell_SidebarResource;
struct UIShell_SidebarResource
{
  char *id;
  char *label;
  char *command;
  char *windows_command;
};
#include "uishell/generated/sidebar_fixture.h"

typedef struct UIShell_SidebarSection UIShell_SidebarSection;
struct UIShell_SidebarSection
{
  UIShell_SidebarSection *next;
  String8 key;
  F32 height_px;
  F32 content_height;
  B32 has_controls;
  B32 collapsed;
  // Its make footers' height last frame (they open on hover and push rows down).
  F32 footer_extra;
};

typedef enum UIShell_CardPlacement
{
  UIShell_CardPlacement_Transient,
  UIShell_CardPlacement_Float,
  UIShell_CardPlacement_Inline,
  UIShell_CardPlacement_Pinned,
} UIShell_CardPlacement;

typedef struct UIShell_HoverCard UIShell_HoverCard;
struct UIShell_HoverCard
{
  UIShell_HoverCard *next;
  UI_HoverCardMask mask;
  CFG_ID saved;
  UIShell_CardPlacement placement, requested;
  B32 move_requested, moving, drag_released;
  // The card's own context menu, which mustn't dismiss it like other menus.
  UI_Key menu;
  Vec2F32 move_origin;
  String8 source_key, source_row, retained_label;
  Arena *arena, *label_arena;
  AndamentoEntity *path;
  AndamentoEntity previous, candidate;
  String8 context;
  U64 depth, capacity, candidate_since, changed_at, left_at;
  UI_Key source, dismissed;
  Rng2F32 source_rect, rect;
  Vec2F32 departure, last_mouse, glide_from;
  // The pointer's x when the card opened for its subject: Near placement's
  // horizontal anchor.
  F32 anchor_x;
  B32 open, engaged, focused, contains_current, source_seen, corridor_active, enriched;
  F32 scroll, content_height;
  // The cap holding the card's controls (#269): its box and bounds when it
  // showed this build, which edge it joins, and whether the entity actions
  // fell back to a footer for lack of room.
  UI_HoverCardMask cap;
  B32 cap_shown, cap_drawn, cap_below, cap_footer;
};

typedef struct UIShell_SidebarLabel UIShell_SidebarLabel;
struct UIShell_SidebarLabel { String8 identity, label; };

// A record as last written (uishell_sidebar_records.c).
typedef struct UIShell_SidebarRecord UIShell_SidebarRecord;
struct UIShell_SidebarRecord
{
  UIShell_SidebarRecord *next;
  String8 name;
  U64 generation;
};

// An entity as last sent to Andamento: "kind/id", and a hash of what was sent.
typedef struct UIShell_SidebarPublished UIShell_SidebarPublished;
struct UIShell_SidebarPublished
{
  UIShell_SidebarPublished *next;
  String8 entry;
  U64 hash;
};
typedef struct UIShell_KdlNode UIShell_KdlNode;
typedef struct UIShell_SectionPlacement UIShell_SectionPlacement;

struct UIShell_SidebarState
{
  // The window it is the sidebar of (0 for a diagnostic fixture's).
  CFG_ID window;
  // Andamento's records (uishell_sidebar_records.c): the window's records
  // directory (empty: not saved), each record as last written, and when the
  // changed ones are due. The dashboard record as read last, by generation.
  Arena *records_arena;
  String8 records_dir;
  UIShell_SidebarRecord *records;
  U64 records_due;
  // Snapshots taken (uishell_sidebar_replace_snapshot), and how many had been
  // when records were last compared with Andamento's.
  U64 snapshot_count, records_snapshot_count;
  Arena *dashboard_arena;
  UIShell_KdlNode *dashboard;
  U64 dashboard_generation;
  Arena *labels_arena;
  UIShell_SidebarLabel *labels;
  U64 labels_capacity;
  AndamentoSnapshot *labels_snapshot;
  UIShell_SidebarSection *sections;
  UIShell_HoverCard cards[2];
  UIShell_HoverCard *detached;
  CFG_ID pin_before, pin_reveal;
  UIShell_HoverCard *drag_card;
  // One sidebar drag at a time, of a card (drag_card) or of a row: the row's
  // key, sibling run, entity, label and rect, copied when it starts.
  Arena *row_drag_arena;
  String8 row_drag_key, row_drag_loop, row_drag_label;
  AndamentoEntity row_drag_entity;
  Rng2F32 row_drag_rect;
  // The row row_begin is building, so a drag from its label or a chip lifts
  // the whole row.
  UI_Box *building_row;
  B32 row_drag_released;
  // The workspace a row drag carries (its live workspace), and the move a
  // local one claimed: the project it would live with (empty: the Workspaces
  // group), copied into its own arena, and the build that claimed it.
  CFG_ID row_drag_workspace;
  Arena *home_arena;
  String8 home_project;
  U64 home_build;
  // The local group a sidebar drag claimed an insertion point in: its id and
  // item run (copied), the index among its items, the build and the area.
  Arena *group_claim_arena;
  String8 group_claim_id, group_claim_loop;
  U64 group_claim_index, group_claim_build;
  // A drag carrying groups (a group's header, or a local section's View)
  // claims the gap between a local section's groups instead: the section,
  // and the group the dropped ones go after (empty: first) (#282).
  Arena *groups_claim_arena;
  String8 groups_claim_section, groups_claim_after;
  U64 groups_claim_build;
  Rng2F32 groups_claim_rect;
  Rng2F32 group_claim_rect;
  // The reorder a row drag claimed over its sibling run: before or after
  // the anchor sibling (copied into its own arena), and the build.
  Arena *reorder_arena;
  AndamentoEntity reorder_anchor;
  B32 reorder_after;
  U64 reorder_build;
  // The control held down, and since when (see uishell_sidebar_held).
  UI_Key hold_key;
  U64 hold_us;
  // A group header double-clicked to rename: the release that ends the
  // double click is not another collapse.
  UI_Key header_click_swallow;
  // A group header's ⋯, clicked this build: its menu opens there.
  UI_Key header_more_anchor;
  B32 header_more_open;
  // A drop or Reset order waits for the end of render, which owns the snapshot.
  B32 order_pending;
  String8 order_loop;
  AndamentoEntity *order;
  U64 order_count;
  // Renaming a local section or group (uishell_local_groups.c): its node,
  // the edit, and whether the field still needs focus. A delete awaiting
  // confirmation, because workspaces would move.
  CFG_ID rename_node;
  U8 rename_text[256];            // a longer name is cut to fit
  U64 rename_size;
  TxtPt rename_cursor, rename_mark;
  B32 rename_focus;
  CFG_ID confirm_delete;
  // The make footer being named (uishell_sidebar_make_footer), by its key's
  // hash (0: none), which of its actions, and the build it was last shown
  // in: one no longer shown stops naming.
  U64 make_key;
  U32 make_index;
  U64 make_build;
  // The docking site a sidebar drag was dropped on (drag_panel_drop): on a
  // tab strip (no direction), the tab it goes after (0: first).
  CFG_ID drop_panel;
  Dir2 drop_direction;
  CFG_ID drop_previous_tab;
  // A whole section dragged by its grip and dropped (section_drop_commit),
  // applied once the window's Views have built (section_drop_apply).
  B32 section_drop;
  CFG_ID section_drop_source, section_drop_panel, section_drop_prev, section_drop_selected;
  Dir2 section_drop_direction;
  U64 pin_cfg_generation;
  Rng2F32 rect;
  B32 card_escape_down;
  AndamentoEntity card_action_target;
  String8 card_action_intent;
  B32 card_has_action;
  Andamento *core;
  AndamentoSnapshot *snapshot;
  Arena *placement_arena;
  AndamentoSnapshot *placement_snapshot;
  UIShell_SectionPlacement *placement_regions;
  U64 placement_count, placement_cache_builds;
  UI_Key revealed_chip;
  F32 revealed_chip_width;
  U64 revealed_chip_build_index;
  U64 topology_hash;
  // How the renderer reads each snapshot node (uishell_sidebar_node_at),
  // cached per snapshot.
  AndamentoSnapshot *roles_snapshot;
  Arena *roles_arena;
  U8 *roles;
  String8 *role_keys;
  B32 roles_default_section;      // the default local section is placed
  // Local sections, groups and pins as last sent to Andamento, and a hash
  // of the working copy they were sent from (uishell_sidebar_local_publish).
  Arena *local_arena;
  UIShell_SidebarPublished *local_published;
  U64 local_source_hash;
  // Local workspaces as last published as host entities (.workspace), and a
  // hash of what that read (uishell_sidebar_publish_workspaces).
  Arena *workspaces_arena;
  UIShell_SidebarPublished *workspaces_published;
  U64 workspaces_source_hash;
  U64 workdirs_hash;
  U64 workdirs_retry_at;
  U64 managed_cfg_generation;
  UI_State *render_ui;
  U64 render_build_index;
  B32 managed_dirty;
  UIShell_WorkspaceId managed_error_workspace;
  // The window's workspaces at the last topology observation, so one that
  // goes unkept is forgotten (uishell_sidebar_register).
  Arena *observed_arena;
  UIShell_WorkspaceId *observed;
  U64 observed_count;
  B32 initialized;
  U64 reveal_workspace_id;
  U8 error[512];
  U8 inspection[2048];
};

internal AndamentoText
uishell_sidebar_text(String8 s)
{
  AndamentoText result = {s.str, s.size};
  return result;
}

internal String8
uishell_sidebar_string(AndamentoText s)
{
  return str8((U8 *)s.data, s.len);
}

internal AndamentoWorkspaceId
uishell_sidebar_workspace(UIShell_WorkspaceId id)
{
  AndamentoWorkspaceId result = {0};
  MemoryCopy(result.bytes, id.v, sizeof(result.bytes));
  return result;
}

internal UIShell_WorkspaceId
uishell_workspace_id_from_andamento(AndamentoWorkspaceId id)
{
  UIShell_WorkspaceId result = {0};
  MemoryCopy(result.v, id.bytes, sizeof(result.v));
  return result;
}

// Andamento names a workspace by its Workspace ID (ABI 3), and ABI 2's
// AndamentoNode/AndamentoDetail.workspace_id reads 0 for one. Docking and
// rendering key workspaces by CFG_ID until state model step 7, so the host
// reads nodes and details through these, which put the CFG_ID of the
// workspace shown there (0: none, or one this process never indexed).
internal uint32_t
uishell_sidebar_snapshot_node(const AndamentoSnapshot *snapshot, size_t index, AndamentoNode *out)
{
  uint32_t ok = andamento_snapshot_node(snapshot, index, out);
  AndamentoWorkspaceId id = {0};
  out->workspace_id = ok && andamento_snapshot_node_workspace(snapshot, index, &id) ?
    uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_andamento(id)) : 0;
  return ok;
}

internal uint32_t
uishell_sidebar_snapshot_detail(const AndamentoSnapshot *snapshot, size_t index, AndamentoDetail *out)
{
  uint32_t ok = andamento_snapshot_detail(snapshot, index, out);
  AndamentoWorkspaceId id = {0};
  out->workspace_id = ok && andamento_snapshot_detail_workspace(snapshot, index, &id) ?
    uishell_workspace_cfg_id_from_id(uishell_workspace_id_from_andamento(id)) : 0;
  return ok;
}

internal void
uishell_sidebar_subject_hit(AndamentoNode node, UI_Box *box, char *action, B32 menu)
{
  if(!uishell_sidebar_subject_fixture || !uishell_sidebar_subject_geometry_path.size ||
     dim_2f32(box->rect).x <= 0 || dim_2f32(box->rect).y <= 0) { return; }
  // Rectangles come from the previous layout; fixture consumers poll until
  // it settles. The embedded subject IDs are fixed ASCII identifiers.
  str8_list_pushf(ui_build_arena(), &uishell_sidebar_subject_geometry,
    "{\"id\":\"%S\",\"chip\":%s,\"action\":\"%s\",\"menu\":%s,\"rect\":[%g,%g,%g,%g]}",
    uishell_sidebar_string(node.entity_id),
    str8_match(uishell_sidebar_string(node.layout), str8_lit("inline"), 0) ? "true" : "false", action, menu ? "true" : "false",
    box->rect.x0, box->rect.y0, box->rect.x1, box->rect.y1);
}

internal void
uishell_sidebar_set_error(UIShell_SidebarState *state, String8 message)
{
  U64 size = Min(message.size, sizeof(state->error)-1);
  MemoryCopy(state->error, message.str, size);
  state->error[size] = 0;
}

// Copy failures into the fixed status buffer, then free the owned ABI error
// with andamento_string_free. Repeated errors overwrite the same buffer; this
// helper neither emits a log nor appends status entries.
internal B32
uishell_sidebar_result(UIShell_SidebarState *state, B32 ok, char *error)
{
  if(!ok)
  {
    String8 message = error ? str8_cstring(error) : str8_lit("Sidebar operation failed");
    uishell_sidebar_set_error(state, message);
  }
  andamento_string_free(error);
  return ok;
}

#include "uishell/uishell_managed_content.c"

// Labels contain borrowed snapshot text. Drop them before replacing/releasing
// that snapshot, even if an allocator subsequently reuses its address.
internal void
uishell_sidebar_labels_invalidate(UIShell_SidebarState *state)
{
  // Deliberately rebuild O(n) on the next lookup after dispatch; borrow only current snapshot labels.
  state->labels_snapshot = 0;
  state->labels = 0;
  state->labels_capacity = 0;
  if(state->labels_arena) { arena_clear(state->labels_arena); }
}

internal B32
uishell_sidebar_dispatch(UIShell_SidebarState *state, size_t action, char **error)
{
  uishell_sidebar_labels_invalidate(state);
  return andamento_dispatch(state->core, state->snapshot, action, error);
}

// Identity alone is the existing display contract (not kind + identity).
// Insert in snapshot order, retaining the first non-section placement, including
// an empty label. Font, width, animations and scroll do not affect this lookup.
internal String8
uishell_sidebar_context_label(UIShell_SidebarState *state, AndamentoNode *nodes, U64 count, String8 identity)
{
  if(uishell_sidebar_benchmark_uncached)
  {
    for(U64 i = 0; i < count; i++)
    {
      if(!nodes[i].is_section && str8_match(identity, uishell_sidebar_string(nodes[i].entity_id), 0))
      { return uishell_sidebar_string(nodes[i].label); }
    }
    return identity;
  }
  if(state->labels_snapshot != state->snapshot || !state->labels)
  {
    uishell_sidebar_labels_invalidate(state);
    if(!state->labels_arena) { state->labels_arena = arena_alloc(); }
    U64 capacity = 2;
    while(capacity < count*2) { capacity *= 2; }
    state->labels = push_array(state->labels_arena, UIShell_SidebarLabel, capacity);
    state->labels_capacity = capacity;
    for(U64 i = 0; i < count; i++)
    {
      String8 key = uishell_sidebar_string(nodes[i].entity_id);
      if(nodes[i].is_section || !key.size) { continue; }
      U64 slot = u64_hash_from_str8(key)&(capacity-1);
      while(state->labels[slot].identity.size && !str8_match(state->labels[slot].identity, key, 0))
      { slot = (slot+1)&(capacity-1); }
      if(!state->labels[slot].identity.size)
      { state->labels[slot] = (UIShell_SidebarLabel){key, uishell_sidebar_string(nodes[i].label)}; }
    }
    state->labels_snapshot = state->snapshot;
  }
  U64 slot = u64_hash_from_str8(identity)&(state->labels_capacity-1);
  while(state->labels[slot].identity.size)
  {
    if(str8_match(state->labels[slot].identity, identity, 0)) { return state->labels[slot].label; }
    slot = (slot+1)&(state->labels_capacity-1);
  }
  return identity;
}

internal void uishell_sidebar_local_publish(UIShell_SidebarState *state, CFG_ID window);
internal void uishell_sidebar_records_save(UIShell_SidebarState *state, U64 now, B32 flush);
internal CFG_Node *uishell_sidebar_local_tree_from_id(CFG_ID window);

internal void
uishell_sidebar_release(UIShell_SidebarState *state)
{
  if(state != 0)
  {
    // What the window's sidebar holds is saved before its core goes, local
    // sections edited since the last publish included.
    if(state->window && state->core)
    {
      uishell_sidebar_local_publish(state, state->window);
      uishell_sidebar_records_save(state, 0, 1);
    }
    if(state->window) { cfg_node_release(rd_state->cfg, uishell_sidebar_local_tree_from_id(state->window)); }
    state->window = 0;
    for(U64 i = 0; i < ArrayCount(state->cards); i++)
    {
      if(state->cards[i].arena) { arena_release(state->cards[i].arena); }
      if(state->cards[i].label_arena) { arena_release(state->cards[i].label_arena); }
      MemoryZeroStruct(&state->cards[i]);
    }
    if(state->records_arena) { arena_release(state->records_arena); state->records_arena = 0; }
    state->records_dir = str8_zero(); state->records = 0; state->records_due = 0;
    if(state->dashboard_arena) { arena_release(state->dashboard_arena); state->dashboard_arena = 0; }
    state->dashboard = 0; state->dashboard_generation = 0;
    if(state->row_drag_arena) { arena_release(state->row_drag_arena); state->row_drag_arena = 0; }
    if(state->reorder_arena) { arena_release(state->reorder_arena); state->reorder_arena = 0; }
    if(state->local_arena) { arena_release(state->local_arena); state->local_arena = 0; }
    state->local_published = 0; state->local_source_hash = 0;
    if(state->workspaces_arena) { arena_release(state->workspaces_arena); state->workspaces_arena = 0; }
    state->workspaces_published = 0; state->workspaces_source_hash = 0;
    if(state->observed_arena) { arena_release(state->observed_arena); state->observed_arena = 0; }
    if(state->home_arena) { arena_release(state->home_arena); state->home_arena = 0; }
    if(state->group_claim_arena) { arena_release(state->group_claim_arena); state->group_claim_arena = 0; }
    if(state->groups_claim_arena) { arena_release(state->groups_claim_arena); state->groups_claim_arena = 0; }
    if(state->roles_arena) { arena_release(state->roles_arena); state->roles_arena = 0; }
    state->roles_snapshot = 0;
    if(state->placement_arena) { arena_release(state->placement_arena); state->placement_arena = 0; }
    state->placement_snapshot = 0;
    state->placement_regions = 0;
    state->placement_count = 0;
    uishell_sidebar_labels_invalidate(state);
    if(state->labels_arena) { arena_release(state->labels_arena); state->labels_arena = 0; }

    for(UIShell_HoverCard *c = state->detached; c; c = c->next)
    {
      if(c->arena) { arena_release(c->arena); }
      if(c->label_arena) { arena_release(c->label_arena); }
    }
    state->detached = state->drag_card = 0;
    andamento_snapshot_release(state->snapshot);
    andamento_destroy(state->core);
    state->snapshot = 0;
    state->core = 0;
  }
}

// Generated identities exercise hash collisions and duplicates independently of
// placement labels. Text remains borrowed until explicit snapshot invalidation.
internal B32
uishell_sidebar_labels_diagnostics(void)
{
  Temp scratch = scratch_begin(0, 0);
  B32 saved_uncached = uishell_sidebar_benchmark_uncached, ok = 1;
  uishell_sidebar_benchmark_uncached = 0;
  AndamentoNode nodes[6] = {0};
  UIShell_SidebarState state = {0};
  String8 first = str8_lit("label-first"), collision = {0};
  for(U64 i = 0; i < 1024 && !collision.size; i++)
  {
    String8 candidate = push_str8f(scratch.arena, "label-collision-%I64u", i);
    if((u64_hash_from_str8(candidate)&15) == (u64_hash_from_str8(first)&15)) { collision = candidate; }
  }
  if(!collision.size) { scratch_end(scratch); uishell_sidebar_benchmark_uncached = saved_uncached; return 0; }
  nodes[0] = (AndamentoNode){.is_section = 1, .entity_id = uishell_sidebar_text(first), .label = uishell_sidebar_text(str8_lit("section"))};
  nodes[1] = (AndamentoNode){.entity_id = uishell_sidebar_text(first), .label = uishell_sidebar_text(str8_lit("first"))};
  nodes[2] = (AndamentoNode){.entity_id = uishell_sidebar_text(collision), .label = uishell_sidebar_text(str8_lit("collision"))};
  nodes[3] = (AndamentoNode){.entity_id = uishell_sidebar_text(first), .label = uishell_sidebar_text(str8_lit("later"))};
  nodes[4] = (AndamentoNode){.entity_id = uishell_sidebar_text(str8_lit("empty-label"))};
  nodes[5] = (AndamentoNode){.entity_id = uishell_sidebar_text(str8_lit("empty-label")), .label = uishell_sidebar_text(str8_lit("later"))};
  // First non-section match wins, including an empty label; collisions and
  // unknown identities must behave exactly like the original linear scan.
  String8 identities[] = {first, collision, str8_lit("empty-label"), str8_lit("missing"), str8_zero()};
  for(U64 i = 0; i < ArrayCount(identities); i++)
  {
    uishell_sidebar_benchmark_uncached = 1;
    String8 expected = uishell_sidebar_context_label(&state, nodes, ArrayCount(nodes), identities[i]);
    uishell_sidebar_benchmark_uncached = 0;
    String8 actual = uishell_sidebar_context_label(&state, nodes, ArrayCount(nodes), identities[i]);
    ok &= str8_match(expected, actual, 0);
  }
  // Invalidation must drop borrowed labels even when a snapshot's address is
  // reused. An empty snapshot and a later repopulation also stay correct.
  uishell_sidebar_labels_invalidate(&state);
  ok &= !state.labels && !state.labels_capacity && !state.labels_snapshot;
  nodes[1].label = uishell_sidebar_text(str8_lit("replacement"));
  ok &= str8_match(uishell_sidebar_context_label(&state, nodes, ArrayCount(nodes), first), str8_lit("replacement"), 0);
  uishell_sidebar_labels_invalidate(&state);
  ok &= str8_match(uishell_sidebar_context_label(&state, 0, 0, first), first, 0);
  uishell_sidebar_labels_invalidate(&state);
  // Released state can be reused by native fixture lifecycles without a
  // dangling arena, including an idempotent second release.
  UIShell_HoverCard detached[2] = {0};
  detached[0].next = &detached[1]; state.detached = &detached[0]; state.drag_card = &detached[1];
  for(U64 i = 0; i < ArrayCount(detached); i++)
  {
    detached[i].arena = arena_alloc(); detached[i].label_arena = arena_alloc();
    state.cards[i].arena = arena_alloc(); state.cards[i].label_arena = arena_alloc();
  }
  uishell_sidebar_release(&state);
  ok &= !state.labels_arena && !state.detached && !state.drag_card && !state.cards[0].arena && !state.cards[1].label_arena;
  uishell_sidebar_release(&state);
  ok &= str8_match(uishell_sidebar_context_label(&state, nodes, ArrayCount(nodes), first), str8_lit("replacement"), 0);
  uishell_sidebar_release(&state);
  uishell_sidebar_benchmark_uncached = saved_uncached;
  scratch_end(scratch);
  if(!ok) { fprintf(stderr, "FAIL sidebar context labels\n"); }
  return ok;
}

internal void
uishell_sidebar_replace_snapshot(UIShell_SidebarState *state, AndamentoSnapshot *snapshot)
{
  // Invalidate before releasing: the allocator may reuse the snapshot address.
  state->placement_snapshot = 0;
  state->roles_snapshot = 0;
  uishell_sidebar_labels_invalidate(state);
  andamento_snapshot_release(state->snapshot);
  state->snapshot = snapshot;
  state->snapshot_count++;
}

internal void
uishell_sidebar_refresh(UIShell_SidebarState *state)
{
  char *error = 0;
  B32 current = andamento_snapshot_is_current(state->core, state->snapshot, &error);
  if(error != 0) { uishell_sidebar_result(state, 0, error); return; }
  if(current) { return; }
  AndamentoSnapshot *next = andamento_snapshot_acquire(state->core, &error);
  if(uishell_sidebar_result(state, next != 0, error))
  {
    uishell_sidebar_replace_snapshot(state, next);
    state->managed_dirty = 1;
  }
}

typedef struct UIShell_Workdir UIShell_Workdir;
struct UIShell_Workdir { UIShell_Workdir *next; WheelhouseWorkdir value; UIShell_WorkspaceId workspace; };
typedef struct { UIShell_Workdir *first, *last; U64 count; } UIShell_Workdirs;

internal WheelhouseIngressText
uishell_ingress_text(String8 text)
{
  return (WheelhouseIngressText){text.str, text.size};
}

// The declared boolean display variables that persist, one control at a
// time. Andamento keeps their values in its dashboard record.
typedef struct UIShell_DisplayControlIterator UIShell_DisplayControlIterator;
struct UIShell_DisplayControlIterator
{
  AndamentoSnapshot *snapshot;
  U64 node;
  U64 control;
};

internal B32
uishell_sidebar_next_persistent_control(UIShell_DisplayControlIterator *it,
                                       AndamentoControl *control, AndamentoText *name)
{
  if(!it->snapshot) { return 0; }
  U64 count = andamento_snapshot_node_count(it->snapshot);
  for(; it->node < count; it->node++, it->control = 0)
  {
    AndamentoNode node = {0};
    uishell_sidebar_snapshot_node(it->snapshot, it->node, &node);
    for(; it->control < node.control_count;)
    {
      U64 index = node.first_control + it->control++;
      U32 persist = 0;
      if(andamento_snapshot_control(it->snapshot, index, control) && control->value_kind == 1 &&
         andamento_snapshot_control_variable(it->snapshot, index, name, &persist) && persist)
      { return 1; }
    }
  }
  return 0;
}

// Row reorder (drag-model.md, "Reorder"): rows move only among the siblings
// of one loop invocation. A drop sets the run's full order, keyed by its
// loop key, and Andamento keeps it in its dashboard record; it merges rows
// the list doesn't name.
internal String8
uishell_sidebar_loop_key(AndamentoSnapshot *snapshot, U64 index)
{
  AndamentoText loop = {0};
  if(!snapshot || !andamento_snapshot_node_loop_key(snapshot, index, &loop)) { return str8_zero(); }
  return uishell_sidebar_string(loop);
}

// Siblings in displayed order, which is snapshot order for one loop key.
internal U64
uishell_sidebar_siblings(Arena *arena, AndamentoSnapshot *snapshot, String8 loop, AndamentoEntity **out)
{
  U64 count = 0, total = loop.size ? andamento_snapshot_node_count(snapshot) : 0;
  for(U64 i = 0; i < total; i++)
  { if(str8_match(uishell_sidebar_loop_key(snapshot, i), loop, 0)) { count++; } }
  *out = push_array(arena, AndamentoEntity, count);
  for(U64 i = 0, n = 0; n < count && i < total; i++)
  {
    if(!str8_match(uishell_sidebar_loop_key(snapshot, i), loop, 0)) { continue; }
    AndamentoNode node = {0}; uishell_sidebar_snapshot_node(snapshot, i, &node);
    (*out)[n++] = (AndamentoEntity){node.entity_kind, node.entity_id};
  }
  return count;
}

// An empty order returns the run to data order and forgets the saved list.
internal void
uishell_sidebar_set_order(UIShell_SidebarState *state, String8 loop, AndamentoEntity *entities, U64 count)
{
  if(!state->core || !loop.size) { return; }
  char *error = 0;
  B32 ok = andamento_set_sibling_order(state->core, uishell_sidebar_text(loop), entities, count, &error);
  uishell_sidebar_result(state, ok, error);
  uishell_sidebar_refresh(state);
  rd_request_frame();
}

// Includes unselected tabs. Cleat has no live cwd report in its current ABI;
// leave live_cwd empty rather than relabeling the saved launch directory.
internal UIShell_Workdirs
uishell_sidebar_workdirs(Arena *arena, UIShell_ControlledSplit *split)
{
  UIShell_Workdirs result = {0};
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
  {
    CFG_Node *workspace = w->mount.owner_cfg;
    CFG_Node *panels = cfg_node_child_from_string(workspace, str8_lit("panels"));
    CFG_PanelTree tree = rd_panel_tree_from_cfg(arena, panels);
    for(CFG_PanelNode *p = tree.root; p != &cfg_nil_panel_node; p = cfg_panel_node_rec__depth_first_pre(tree.root, p).next)
    {
      for(CFG_NodePtrNode *tab = p->tabs.first; tab; tab = tab->next)
      {
        if(!str8_match(tab->v->string, str8_lit("terminal"), 0)) { continue; }
        CFG_Node *cwd_node = cfg_node_child_from_string(tab->v, str8_lit("cwd"));
        if(cwd_node == &cfg_nil_node || cwd_node->first == &cfg_nil_node) { continue; }
        String8 cwd = cwd_node->first->string;
        if(!cwd.size) { continue; }
        UIShell_Workdir *dir = push_array(arena, UIShell_Workdir, 1);
        dir->value = (WheelhouseWorkdir){w->id, tab->v->id,
          uishell_ingress_text(cfg_node_child_from_string(workspace, str8_lit("sidebar_entity_kind"))->first->string),
          uishell_ingress_text(cfg_node_child_from_string(workspace, str8_lit("sidebar_entity_id"))->first->string),
          uishell_ingress_text(cwd), {0}};
        dir->workspace = w->workspace_id;
        SLLQueuePush(result.first, result.last, dir);
        result.count++;
      }
    }
  }
  return result;
}

// A local (subjectless) workspace's own entity id: its Workspace ID.
internal String8
uishell_sidebar_local_entity(CFG_Node *workspace)
{
  return uishell_workspace_id_text_from_cfg(workspace);
}

// The project a local workspace lives with ("lives with project X"), saved
// on the workspace; empty when it lives in the Workspaces group.
internal String8
uishell_sidebar_local_home(CFG_Node *workspace)
{
  return cfg_node_child_from_string(workspace, str8_lit("lives_with"))->first->string;
}

// How the sidebar reads a snapshot node. A `layout="section"` node (a
// section someone made) is a section of its own, as a region is, keyed
// `.section:<id>`; the region holding them is a container that isn't shown;
// a section's only group passes through, so its items are the section's rows
// and it draws no header (drag-model.md, "Titles").
typedef enum UIShell_SidebarRole
{
  UIShell_SidebarRole_Node,
  UIShell_SidebarRole_LocalSection,
  UIShell_SidebarRole_Container,
  UIShell_SidebarRole_PassThrough,
}
UIShell_SidebarRole;

// A section someone made is keyed `.section:<id>` among sections; the
// default (Workspaces) one is `.section:workspaces`.
read_only global String8 uishell_sidebar_local_prefix = str8_lit_comp(".section:");
read_only global String8 uishell_sidebar_default_section_key = str8_lit_comp(".section:workspaces");

internal String8
uishell_sidebar_local_key(Arena *arena, String8 id)
{
  return push_str8f(arena, "%S%S", uishell_sidebar_local_prefix, id);
}

// The section id a key names, or empty when it isn't a section someone made.
internal String8
uishell_sidebar_local_key_id(String8 key)
{
  if(!str8_match(str8_prefix(key, uishell_sidebar_local_prefix.size), uishell_sidebar_local_prefix, 0)) { return str8_zero(); }
  return str8_skip(key, uishell_sidebar_local_prefix.size);
}

internal void
uishell_sidebar_roles(UIShell_SidebarState *state)
{
  if(state->roles_snapshot == state->snapshot) { return; }
  if(!state->roles_arena) { state->roles_arena = arena_alloc(); }
  arena_clear(state->roles_arena);
  state->roles_snapshot = state->snapshot;
  state->roles_default_section = 0;
  U64 count = state->snapshot ? andamento_snapshot_node_count(state->snapshot) : 0;
  state->roles = push_array(state->roles_arena, U8, count);
  state->role_keys = push_array(state->roles_arena, String8, count);
  U64 *groups = push_array(state->roles_arena, U64, count);
  for(U64 i = 0; i < count; i++)
  {
    AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    if(!str8_match(uishell_sidebar_string(node.layout), str8_lit("section"), 0)) { continue; }
    state->roles[i] = UIShell_SidebarRole_LocalSection;
    state->role_keys[i] = uishell_sidebar_local_key(state->roles_arena, uishell_sidebar_string(node.entity_id));
    state->roles_default_section |= str8_match(state->role_keys[i], uishell_sidebar_default_section_key, 0);
    // The section loop's parent is the container. Sections don't nest: one
    // placed inside another would make that one a container instead.
    if(node.parent != ANDAMENTO_NONE) { state->roles[node.parent] = UIShell_SidebarRole_Container; }
  }
  // A section's only group passes through; only `.group` children count.
  for(U64 i = 0; i < count; i++)
  {
    AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    B32 group = str8_match(uishell_sidebar_string(node.entity_kind), str8_lit(".group"), 0);
    if(group && node.parent != ANDAMENTO_NONE && state->roles[node.parent] == UIShell_SidebarRole_LocalSection) { groups[node.parent]++; }
  }
  for(U64 i = 0; i < count; i++)
  {
    AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    B32 group = str8_match(uishell_sidebar_string(node.entity_kind), str8_lit(".group"), 0);
    if(group && node.parent != ANDAMENTO_NONE && state->roles[node.parent] == UIShell_SidebarRole_LocalSection && groups[node.parent] == 1)
    { state->roles[i] = UIShell_SidebarRole_PassThrough; }
  }
}

// Whether node `i` has children (they follow it in the snapshot's order).
internal B32
uishell_sidebar_has_children(UIShell_SidebarState *state, U64 i)
{
  if(i+1 >= andamento_snapshot_node_count(state->snapshot)) { return 0; }
  AndamentoNode next = {0}; uishell_sidebar_snapshot_node(state->snapshot, i+1, &next);
  return next.parent == i;
}

// The leftover section stands aside, while empty, for the default local
// section (Workspaces), which hosts New workspace instead. Without one (a
// host that publishes no local sections) it stays, as the workspace fallback.
internal B32
uishell_sidebar_leftover_hidden(UIShell_SidebarState *state, U64 i, AndamentoNode node)
{
  if(!str8_match(uishell_sidebar_string(node.key), str8_lit(".unplaced"), 0) || uishell_sidebar_has_children(state, i)) { return 0; }
  uishell_sidebar_roles(state);
  return state->roles_default_section;
}

// Reads node `i` as the sidebar shows it (see UIShell_SidebarRole).
internal UIShell_SidebarRole
uishell_sidebar_node_at(UIShell_SidebarState *state, U64 i, AndamentoNode *out)
{
  uishell_sidebar_snapshot_node(state->snapshot, i, out);
  uishell_sidebar_roles(state);
  UIShell_SidebarRole role = (UIShell_SidebarRole)state->roles[i];
  if(role == UIShell_SidebarRole_LocalSection) { out->is_section = 1; out->key = uishell_sidebar_text(state->role_keys[i]); }
  return role;
}

//- Sections and groups people make (drag-model.md, "Sections and groups as
// data"). Andamento owns them, as local `.section`, `.group` and `.ref`
// entities that the shipped KDL places (each section is then a sidebar
// section of its own), and keeps them in its dashboard record. It has no
// getter for them yet, so the editors here read and change a working copy:
//
//   section  (id, label)
//     group  (id, label, default)
//       card (ghost, kind, entity, label, source, compact): a pin
//
// made from the record when the sidebar starts (uishell_sidebar_local_load),
// and sent back when it changes (uishell_sidebar_local_publish). It is
// transient config, one tree per window, never saved to the user file.
// A local workspace lives in a group (`lives_in`) or with a project
// (`lives_with`); with neither it lives in the default group.

read_only global String8 uishell_sidebar_default_local_id = str8_lit_comp("workspaces");

internal void uishell_sidebar_pin_migrate(CFG_Node *window);
internal String8 uishell_sidebar_local_title(Arena *arena, CFG_Node *section);
internal void uishell_sidebar_local_borrow_titles(CFG_Node *root);

// The working copy of the window with this config ID, or nil.
internal CFG_Node *
uishell_sidebar_local_tree_from_id(CFG_ID window)
{
  if(!window) { return &cfg_nil_node; }
  CFG_Node *trees = cfg_node_child_from_string(cfg_node_child_from_string(cfg_node_root(), str8_lit("transient")), str8_lit("sidebar_local"));
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *tree = cfg_node_child_from_string(trees, push_str8f(scratch.arena, "%I64x", window));
  scratch_end(scratch);
  return tree;
}

// The window's working copy, or nil when it has none.
internal CFG_Node *
uishell_sidebar_local_tree(CFG_Node *window)
{
  return uishell_sidebar_local_tree_from_id(window->id);
}

internal CFG_Node *
uishell_sidebar_local_tree_alloc(CFG_Node *window)
{
  CFG_Node *tree = uishell_sidebar_local_tree(window);
  if(tree == &cfg_nil_node && window != &cfg_nil_node)
  {
    CFG_Node *transient = cfg_node_child_from_string_or_alloc(rd_state->cfg, cfg_node_root(), str8_lit("transient"));
    CFG_Node *trees = cfg_node_child_from_string_or_alloc(rd_state->cfg, transient, str8_lit("sidebar_local"));
    Temp scratch = scratch_begin(0, 0);
    tree = cfg_node_new(rd_state->cfg, trees, push_str8f(scratch.arena, "%I64x", window->id));
    scratch_end(scratch);
  }
  return tree;
}

internal String8
uishell_sidebar_local_field(CFG_Node *node, String8 name)
{
  return cfg_node_child_from_string(node, name)->first->string;
}

internal void
uishell_sidebar_local_set_field(CFG_Node *node, String8 name, String8 value)
{
  CFG_Node *field = cfg_node_child_from_string_or_alloc(rd_state->cfg, node, name);
  cfg_node_new_replace(rd_state->cfg, field, value);
}

// The window's local sections, with the default Workspaces section and group
// made when missing; they can be renamed and moved but not deleted.
internal CFG_Node *
uishell_sidebar_local_root(CFG_Node *window)
{
  CFG_Node *root = uishell_sidebar_local_tree(window);
  B32 has_default = 0;
  for(CFG_Node *section = root->first; section != &cfg_nil_node && !has_default; section = section->next)
  {
    for(CFG_Node *group = section->first; group != &cfg_nil_node && !has_default; group = group->next)
    { has_default = str8_match(group->string, str8_lit("group"), 0) && cfg_node_child_from_string(group, str8_lit("default")) != &cfg_nil_node; }
  }
  if(!has_default)
  {
    root = uishell_sidebar_local_tree_alloc(window);
    CFG_Node *section = cfg_node_new(rd_state->cfg, root, str8_lit("section"));
    uishell_sidebar_local_set_field(section, str8_lit("id"), uishell_sidebar_default_local_id);
    uishell_sidebar_local_set_field(section, str8_lit("label"), str8_lit("Workspaces"));
    CFG_Node *group = cfg_node_new(rd_state->cfg, section, str8_lit("group"));
    uishell_sidebar_local_set_field(group, str8_lit("id"), uishell_sidebar_default_local_id);
    uishell_sidebar_local_set_field(group, str8_lit("label"), str8_lit("Workspaces"));
    cfg_node_new(rd_state->cfg, group, str8_lit("default"));
  }
  uishell_sidebar_local_borrow_titles(root);
  return root;
}

// Whether `key` (".section:<id>") names a section in the window's data.
internal B32
uishell_sidebar_local_section_exists(CFG_Node *window, String8 key)
{
  String8 id = uishell_sidebar_local_key_id(key);
  if(!id.size) { return 0; }
  CFG_Node *root = uishell_sidebar_local_tree(window);
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  { if(str8_match(uishell_sidebar_local_field(section, str8_lit("id")), id, 0)) { return 1; } }
  return 0;
}

// The local group with this id, or nil.
internal CFG_Node *
uishell_sidebar_local_group(CFG_Node *window, String8 id)
{
  CFG_Node *root = uishell_sidebar_local_tree(window);
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    for(CFG_Node *group = section->first; group != &cfg_nil_node; group = group->next)
    {
      if(str8_match(group->string, str8_lit("group"), 0) && str8_match(uishell_sidebar_local_field(group, str8_lit("id")), id, 0))
      { return group; }
    }
  }
  return &cfg_nil_node;
}

// JSON string contents: runs of plain bytes are kept as they are, with only
// quotes, backslashes and control characters escaped.
internal String8
uishell_sidebar_json_text(Arena *arena, String8 text)
{
  String8List parts = {0};
  U64 run = 0;
  for(U64 i = 0; i < text.size; i++)
  {
    U8 c = text.str[i];
    if(c != '"' && c != '\\' && c >= 0x20) { continue; }
    if(i > run) { str8_list_push(arena, &parts, str8(text.str+run, i-run)); }
    if(c < 0x20) { str8_list_pushf(arena, &parts, "\\u%04x", c); }
    else { str8_list_pushf(arena, &parts, "\\%c", c); }
    run = i+1;
  }
  if(parts.node_count == 0) { return text; }
  if(text.size > run) { str8_list_push(arena, &parts, str8(text.str+run, text.size-run)); }
  return str8_list_join(arena, &parts, 0);
}

internal String8
uishell_sidebar_json_fact_text(Arena *arena, String8 key, String8 value)
{
  return push_str8f(arena, "\"%S\":{\"value\":{\"type\":\"text\",\"value\":\"%S\"}}", key, uishell_sidebar_json_text(arena, value));
}

// A label and a position: the entity's place among its siblings, which the
// shipped KDL orders by (`.position`) until a host-owned order replaces it.
internal String8
uishell_sidebar_json_label(Arena *arena, String8 value, U64 position)
{
  return push_str8f(arena, "%S,\".position\":{\"value\":{\"type\":\"text\",\"value\":\"%I64u\"}}",
    uishell_sidebar_json_fact_text(arena, str8_lit("display.label"), value), position);
}

internal String8
uishell_sidebar_json_fact_ref(Arena *arena, String8 key, String8 kind, String8 id)
{
  return push_str8f(arena, "\"%S\":{\"value\":{\"type\":\"entity-refs\",\"value\":[{\"kind\":\"%S\",\"id\":\"%S\"}]}}",
    key, uishell_sidebar_json_text(arena, kind), uishell_sidebar_json_text(arena, id));
}

// One patch setting `set` (comma-joined facts) and unsetting `unset` (quoted,
// comma-joined keys) on a published entity.
internal String8
uishell_sidebar_json_entity_patch(Arena *arena, String8 kind, String8 id, String8 set, String8 unset)
{
  return push_str8f(arena,
    "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"%S\",\"id\":\"%S\"}},\"source_id\":\"wheelhouse.local\",\"set\":{%S},\"unset\":[%S]}",
    kind, uishell_sidebar_json_text(arena, id), set, unset);
}

// Every fact a workspace's host entity may have, quoted; unset together to
// retract it.
read_only global String8 uishell_sidebar_workspace_keys = str8_lit_comp("\"display.label\",\".position\",\"flotilla.project\",\".group\"");

// Whether "kind/id" is already in `ids`: a corrupt layout repeating an id
// publishes the first and skips the rest, rather than merging them.
internal B32
uishell_sidebar_local_seen(String8List *ids, String8 entry)
{
  for(String8Node *n = ids->first; n; n = n->next) { if(str8_match(n->string, entry, 0)) { return 1; } }
  return 0;
}

internal U64
uishell_sidebar_hash_node(U64 hash, CFG_Node *node)
{
  for(CFG_Node *n = node->first; n != &cfg_nil_node; n = n->next)
  {
    for(U64 i = 0; i < n->string.size; i++) { hash = hash*33 + n->string.str[i]; }
    hash = hash*33 + '(';
    hash = uishell_sidebar_hash_node(hash, n);
    hash = hash*33 + ')';
  }
  return hash;
}

internal U64
uishell_sidebar_hash_text(U64 hash, String8 text)
{
  for(U64 i = 0; i < text.size; i++) { hash = hash*33 + text.str[i]; }
  return hash*33 + '|';
}

//- What was last sent to Andamento, one "kind/id" entry each with a hash of
// what was sent, so only what changed is sent again and what went is removed.

internal void
uishell_sidebar_published_clear(Arena **arena, UIShell_SidebarPublished **list)
{
  if(!*arena) { *arena = arena_alloc(); }
  arena_clear(*arena);
  *list = 0;
}

internal void
uishell_sidebar_published_add(Arena *arena, UIShell_SidebarPublished **list, String8 entry, U64 hash)
{
  UIShell_SidebarPublished *p = push_array(arena, UIShell_SidebarPublished, 1);
  p->entry = push_str8_copy(arena, entry);
  p->hash = hash;
  p->next = *list;
  *list = p;
}

internal UIShell_SidebarPublished *
uishell_sidebar_published_find(UIShell_SidebarPublished *list, String8 entry)
{
  for(UIShell_SidebarPublished *p = list; p; p = p->next) { if(str8_match(p->entry, entry, 0)) { return p; } }
  return 0;
}

internal void
uishell_sidebar_local_published_clear(UIShell_SidebarState *state)
{
  uishell_sidebar_published_clear(&state->local_arena, &state->local_published);
  state->local_source_hash = 0;
}

internal void
uishell_sidebar_local_published_add(UIShell_SidebarState *state, String8 entry, U64 hash)
{
  if(!state->local_arena) { state->local_arena = arena_alloc(); }
  uishell_sidebar_published_add(state->local_arena, &state->local_published, entry, hash);
}

//- Local sections, groups and pins, as Andamento owns them: entities whose
// facts Wheelhouse sets and removes (andamento_local_set/remove).
//
//   .section  display.label (its title), .position
//   .group    display.label, .position, .section, .default
//   .ref      .position (after the group's workspaces), .group, .target,
//             and the pin's own: .label (shown when its target is gone),
//             .source, .compact (shown as a row)

typedef struct UIShell_LocalEntity UIShell_LocalEntity;
struct UIShell_LocalEntity
{
  UIShell_LocalEntity *next;
  String8 kind, id, entry;
  AndamentoLocalFact facts[8];
  U64 count;
  U64 hash;
};

internal void
uishell_sidebar_local_fact_text(UIShell_LocalEntity *e, String8 key, String8 value)
{
  AndamentoLocalFact *f = &e->facts[e->count++];
  f->key = uishell_sidebar_text(key); f->kind = ANDAMENTO_FACT_TEXT; f->text = uishell_sidebar_text(value);
  e->hash = uishell_sidebar_hash_text(uishell_sidebar_hash_text(e->hash, key), value);
}

internal void
uishell_sidebar_local_fact_bool(UIShell_LocalEntity *e, String8 key, B32 value)
{
  AndamentoLocalFact *f = &e->facts[e->count++];
  f->key = uishell_sidebar_text(key); f->kind = ANDAMENTO_FACT_BOOL; f->integer = !!value;
  e->hash = uishell_sidebar_hash_text(e->hash, key)*33 + 'b' + !!value;
}

internal void
uishell_sidebar_local_fact_ref(UIShell_LocalEntity *e, String8 key, String8 kind, String8 id)
{
  AndamentoLocalFact *f = &e->facts[e->count++];
  f->key = uishell_sidebar_text(key); f->kind = ANDAMENTO_FACT_ENTITY;
  f->entity = (AndamentoEntity){uishell_sidebar_text(kind), uishell_sidebar_text(id)};
  e->hash = uishell_sidebar_hash_text(uishell_sidebar_hash_text(uishell_sidebar_hash_text(e->hash, key), kind), id);
}

internal UIShell_LocalEntity *
uishell_sidebar_local_entity_push(Arena *arena, UIShell_LocalEntity **first, UIShell_LocalEntity **last,
                                  String8List *ids, String8 kind, String8 id)
{
  String8 entry = push_str8f(arena, "%S/%S", kind, id);
  if(!id.size || uishell_sidebar_local_seen(ids, entry)) { return 0; }
  str8_list_push(arena, ids, entry);
  UIShell_LocalEntity *e = push_array(arena, UIShell_LocalEntity, 1);
  e->kind = kind; e->id = id; e->entry = entry; e->hash = 5381;
  SLLQueuePush(*first, *last, e);
  return e;
}

// The local entities the window's working copy describes, in order.
internal UIShell_LocalEntity *
uishell_sidebar_local_entities(Arena *arena, CFG_Node *root)
{
  UIShell_LocalEntity *first = 0, *last = 0;
  String8List ids = {0};
  String8 section_kind = str8_lit(".section"), group_kind = str8_lit(".group");
  U64 section_position = 0;
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    if(!str8_match(section->string, str8_lit("section"), 0)) { continue; }
    String8 section_id = uishell_sidebar_local_field(section, str8_lit("id"));
    UIShell_LocalEntity *s = uishell_sidebar_local_entity_push(arena, &first, &last, &ids, section_kind, section_id);
    if(!s) { continue; }
    uishell_sidebar_local_fact_text(s, str8_lit("display.label"), uishell_sidebar_local_title(arena, section));
    uishell_sidebar_local_fact_text(s, str8_lit(".position"), push_str8f(arena, "%I64u", section_position++));
    U64 group_position = 0;
    for(CFG_Node *group = section->first; group != &cfg_nil_node; group = group->next)
    {
      if(!str8_match(group->string, str8_lit("group"), 0)) { continue; }
      String8 group_id = uishell_sidebar_local_field(group, str8_lit("id"));
      UIShell_LocalEntity *g = uishell_sidebar_local_entity_push(arena, &first, &last, &ids, group_kind, group_id);
      if(!g) { continue; }
      uishell_sidebar_local_fact_text(g, str8_lit("display.label"), uishell_sidebar_local_field(group, str8_lit("label")));
      uishell_sidebar_local_fact_text(g, str8_lit(".position"), push_str8f(arena, "%I64u", group_position++));
      uishell_sidebar_local_fact_ref(g, str8_lit(".section"), section_kind, section_id);
      uishell_sidebar_local_fact_bool(g, str8_lit(".default"), cfg_node_child_from_string(group, str8_lit("default")) != &cfg_nil_node);
      // Its ghosts (pins), as `.ref`s presenting their targets, after its
      // workspaces in data order.
      U64 ref_position = 1000000;
      for(CFG_Node *card = group->first; card != &cfg_nil_node; card = card->next)
      {
        if(!str8_match(card->string, str8_lit("card"), 0)) { continue; }
        UIShell_LocalEntity *r = uishell_sidebar_local_entity_push(arena, &first, &last, &ids, str8_lit(".ref"),
          uishell_sidebar_local_field(card, str8_lit("ghost")));
        if(!r) { continue; }
        uishell_sidebar_local_fact_text(r, str8_lit(".position"), push_str8f(arena, "%I64u", ref_position++));
        uishell_sidebar_local_fact_ref(r, str8_lit(".group"), group_kind, group_id);
        // A pin missing its target still shows, as no longer present.
        String8 target_kind = uishell_sidebar_local_field(card, str8_lit("kind")), target_id = uishell_sidebar_local_field(card, str8_lit("entity"));
        if(target_kind.size && target_id.size) { uishell_sidebar_local_fact_ref(r, str8_lit(".target"), target_kind, target_id); }
        String8 label = uishell_sidebar_local_field(card, str8_lit("label")), source = uishell_sidebar_local_field(card, str8_lit("source"));
        if(label.size) { uishell_sidebar_local_fact_text(r, str8_lit(".label"), label); }
        if(source.size) { uishell_sidebar_local_fact_text(r, str8_lit(".source"), source); }
        if(cfg_node_child_from_string(card, str8_lit("compact")) != &cfg_nil_node) { uishell_sidebar_local_fact_bool(r, str8_lit(".compact"), 1); }
      }
    }
  }
  return first;
}

// Sends Andamento the local entities that changed in the window's working
// copy, and removes those that went (a deleted group, a removed pin).
// Nothing is built while the copy is unchanged.
internal void
uishell_sidebar_local_publish(UIShell_SidebarState *state, CFG_ID window)
{
  if(!state->core) { return; }
  CFG_Node *root = uishell_sidebar_local_tree_from_id(window);
  U64 source = uishell_sidebar_hash_node(5381, root);
  if(state->local_source_hash && state->local_source_hash == source) { return; }
  Temp scratch = scratch_begin(0, 0);
  UIShell_LocalEntity *entities = uishell_sidebar_local_entities(scratch.arena, root);
  B32 ok = 1, changed = 0;
  for(UIShell_LocalEntity *e = entities; e && ok; e = e->next)
  {
    UIShell_SidebarPublished *sent = uishell_sidebar_published_find(state->local_published, e->entry);
    if(sent && sent->hash == e->hash) { continue; }
    char *error = 0;
    ok = uishell_sidebar_result(state, andamento_local_set(state->core, uishell_sidebar_text(e->kind), uishell_sidebar_text(e->id),
      e->facts, e->count, &error), error);
    changed = 1;
  }
  for(UIShell_SidebarPublished *p = state->local_published; p && ok; p = p->next)
  {
    B32 kept = 0;
    for(UIShell_LocalEntity *e = entities; e && !kept; e = e->next) { kept = str8_match(e->entry, p->entry, 0); }
    if(kept) { continue; }
    // "kind/id"; kinds have no '/'.
    U64 slash = str8_find_needle(p->entry, 0, str8_lit("/"), 0);
    char *error = 0;
    ok = uishell_sidebar_result(state, andamento_local_remove(state->core, uishell_sidebar_text(str8_prefix(p->entry, slash)),
      uishell_sidebar_text(str8_skip(p->entry, slash+1)), &error), error);
    changed = 1;
  }
  if(ok)
  {
    uishell_sidebar_local_published_clear(state);
    for(UIShell_LocalEntity *e = entities; e; e = e->next) { uishell_sidebar_local_published_add(state, e->entry, e->hash); }
    state->local_source_hash = source;
  }
  if(changed) { uishell_sidebar_refresh(state); rd_request_frame(); }
  scratch_end(scratch);
}

// Everything publishing workspaces reads, hashed without allocating: the
// window's local groups, and each open workspace's id, name and home. A
// config generation can't stand in, as moving a node keeps it.
internal U64
uishell_sidebar_workspaces_source_hash(UIShell_ControlledSplit *split)
{
  U64 hash = uishell_sidebar_hash_node(5381, uishell_sidebar_local_tree(split->owner_cfg));
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
  {
    hash = hash*33 + w->id;
    for(U64 i = 0; i < sizeof(w->workspace_id.v); i++) { hash = hash*33 + w->workspace_id.v[i]; }
    hash = uishell_sidebar_hash_text(hash, w->display_name);
    hash = uishell_sidebar_hash_text(hash, uishell_sidebar_local_home(w->mount.owner_cfg));
    hash = uishell_sidebar_hash_text(hash, uishell_sidebar_local_field(w->mount.owner_cfg, str8_lit("lives_in")));
    hash = uishell_sidebar_hash_text(hash, uishell_workspace_cfg_has_subject(w->mount.owner_cfg) ? str8_lit("subject") : str8_lit("local"));
  }
  return hash;
}

// Publishes each local workspace as a host entity (.workspace) with its home,
// tagging its tab .host.* so its rows are live there; it follows the open
// workspaces, so the host publishes it rather than Andamento keeping it. One
// that closed is retracted. Nothing is built while what it reads is
// unchanged, and only what changed is sent.
internal void
uishell_sidebar_publish_workspaces(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  U64 source = uishell_sidebar_workspaces_source_hash(split);
  if(state->workspaces_source_hash && state->workspaces_source_hash == source) { return; }
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  CFG_Node *window = split->owner_cfg;
  CFG_Node *root = uishell_sidebar_local_tree(window);
  String8 group_kind = str8_lit(".group"), workspace_kind = str8_lit(".workspace");
  // A workspace whose group is gone lives in the default group, whatever its
  // id (local_root makes one only when no group is marked default).
  String8 default_group_id = uishell_sidebar_default_local_id;
  for(CFG_Node *section = root->first; section != &cfg_nil_node; section = section->next)
  {
    for(CFG_Node *g = section->first; g != &cfg_nil_node; g = g->next)
    {
      if(str8_match(g->string, str8_lit("group"), 0) && cfg_node_child_from_string(g, str8_lit("default")) != &cfg_nil_node)
      { default_group_id = uishell_sidebar_local_field(g, str8_lit("id")); }
    }
  }
  U64 now = wheelhouse_ingress_now_ms(), w_position = 0;
  B32 ok = 1, changed = 0;
  String8List ids = {0};
  UIShell_SidebarPublished *published = 0;
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w && ok; w = w->next)
  {
    CFG_Node *workspace = w->mount.owner_cfg;
    if(uishell_workspace_cfg_has_subject(workspace)) { continue; }
    String8 id = uishell_string_from_workspace_id(arena, w->workspace_id);
    // One home: a project, else its group, else the default group.
    String8 project = uishell_sidebar_local_home(workspace);
    String8 group = uishell_sidebar_local_field(workspace, str8_lit("lives_in"));
    if(uishell_sidebar_local_group(window, group) == &cfg_nil_node) { group = default_group_id; }
    // Workspaces keep their tab order within their home.
    String8 label = uishell_sidebar_json_label(arena, w->display_name, w_position++);
    String8 set = project.size ? push_str8f(arena, "%S,%S", label, uishell_sidebar_json_fact_text(arena, str8_lit("flotilla.project"), project)) :
      push_str8f(arena, "%S,%S", label, uishell_sidebar_json_fact_ref(arena, str8_lit(".group"), group_kind, group));
    String8 patch = uishell_sidebar_json_entity_patch(arena, workspace_kind, id, set,
      project.size ? str8_lit("\".group\"") : str8_lit("\"flotilla.project\""));
    String8 entry = push_str8f(arena, "%S/%S", workspace_kind, id);
    U64 hash = u64_hash_from_str8(patch);
    str8_list_push(arena, &ids, entry);
    uishell_sidebar_published_add(arena, &published, entry, hash);
    UIShell_SidebarPublished *sent = uishell_sidebar_published_find(state->workspaces_published, entry);
    if(sent && sent->hash == hash) { continue; }
    char *error = 0;
    ok = uishell_sidebar_result(state, andamento_apply_patch_json(state->core, now, uishell_sidebar_text(patch), &error), error);
    // Its tab names its host entity, whose id is its Workspace ID.
    AndamentoFact facts[2] = {0};
    facts[0].key = uishell_sidebar_text(str8_lit(".host.kind"));
    facts[0].kind = ANDAMENTO_FACT_TEXT;
    facts[0].text = uishell_sidebar_text(workspace_kind);
    facts[1].key = uishell_sidebar_text(str8_lit(".host.id"));
    facts[1].kind = ANDAMENTO_FACT_TEXT;
    facts[1].text = uishell_sidebar_text(id);
    error = 0;
    ok = ok && uishell_sidebar_result(state, andamento_apply_workspace(state->core, now, uishell_sidebar_workspace(w->workspace_id),
      uishell_sidebar_text(str8_lit("wheelhouse.local")), facts, ArrayCount(facts), &error), error);
    changed = 1;
  }
  // Retract what is no longer published.
  for(UIShell_SidebarPublished *p = state->workspaces_published; p && ok; p = p->next)
  {
    if(uishell_sidebar_local_seen(&ids, p->entry)) { continue; }
    String8 id = str8_skip(p->entry, workspace_kind.size+1);
    char *error = 0;
    ok = uishell_sidebar_result(state, andamento_apply_patch_json(state->core, now,
      uishell_sidebar_text(uishell_sidebar_json_entity_patch(arena, workspace_kind, id, str8_zero(), uishell_sidebar_workspace_keys)), &error), error);
    changed = 1;
  }
  if(ok)
  {
    uishell_sidebar_published_clear(&state->workspaces_arena, &state->workspaces_published);
    for(UIShell_SidebarPublished *p = published; p; p = p->next)
    { uishell_sidebar_published_add(state->workspaces_arena, &state->workspaces_published, p->entry, p->hash); }
    state->workspaces_source_hash = source;
  }
  if(changed) { uishell_sidebar_refresh(state); rd_request_frame(); }
  scratch_end(scratch);
}

// Publishes the window's local sections, groups and pins, then its local
// workspaces, which live in its groups.
internal void
uishell_sidebar_publish(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  // The default section and group are made first, so they are published.
  uishell_sidebar_local_root(split->owner_cfg);
  uishell_sidebar_local_publish(state, split->owner_cfg->id);
  uishell_sidebar_publish_workspaces(state, split);
}

// Andamento keeps the workspaces the host made itself: local ones are
// registered here, while a subject's registers when its MATERIALIZE
// completes. One that left the window without being kept is forgotten.
internal void
uishell_sidebar_register(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  B32 ok = 1;
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w && ok; w = w->next)
  {
    if(uishell_workspace_cfg_has_subject(w->mount.owner_cfg)) { continue; }
    B32 known = 0;
    for(U64 i = 0; i < state->observed_count && !known; i++) { known = uishell_workspace_id_match(state->observed[i], w->workspace_id); }
    char *error = 0;
    if(!known) { ok = uishell_sidebar_result(state, andamento_workspace_register(state->core, uishell_sidebar_workspace(w->workspace_id), &error), error); }
  }
  for(U64 i = 0; i < state->observed_count && ok; i++)
  {
    B32 open = 0, kept = 0;
    for(UIShell_MaterializedWorkspace *w = split->inventory.first; w && !open; w = w->next) { open = uishell_workspace_id_match(w->workspace_id, state->observed[i]); }
    for(CFG_Node *c = split->owner_cfg->first; c != &cfg_nil_node && !open && !kept; c = c->next)
    {
      kept = str8_match(c->string, str8_lit("detached_workspace"), 0) &&
        uishell_workspace_id_match(uishell_workspace_id_from_cfg(c), state->observed[i]);
    }
    char *error = 0;
    if(!open && !kept) { ok = uishell_sidebar_result(state, andamento_workspace_forget(state->core, uishell_sidebar_workspace(state->observed[i]), &error), error); }
  }
  if(!ok) { return; }
  if(!state->observed_arena) { state->observed_arena = arena_alloc(); }
  arena_clear(state->observed_arena);
  state->observed = push_array(state->observed_arena, UIShell_WorkspaceId, split->inventory.count);
  state->observed_count = 0;
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next) { state->observed[state->observed_count++] = w->workspace_id; }
}

internal void
uishell_sidebar_observe(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  Temp scratch = scratch_begin(0, 0);
  AndamentoWorkspace3 *items = push_array(scratch.arena, AndamentoWorkspace3, split->inventory.count);
  U64 hash = 5381, count = 0;
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
  {
    items[count] = (AndamentoWorkspace3){uishell_sidebar_workspace(w->workspace_id), count, uishell_sidebar_text(w->display_name), w == split->inventory.selected};
    for(U64 i = 0; i < sizeof(w->workspace_id.v); i++) { hash = hash*33 + w->workspace_id.v[i]; }
    hash = hash*33 + items[count].selected;
    for(U64 i = 0; i < w->display_name.size; i++) { hash = hash*33 + w->display_name.str[i]; }
    count++;
  }
  UIShell_Workdirs dirs = uishell_sidebar_workdirs(scratch.arena, split);
  AndamentoWorkdir3 *observed = push_array(scratch.arena, AndamentoWorkdir3, dirs.count);
  U64 index = 0, dirs_hash = 5381;
  for(UIShell_Workdir *dir = dirs.first; dir; dir = dir->next)
  {
    WheelhouseIngressText cwd = dir->value.live_cwd.len ? dir->value.live_cwd : dir->value.cwd;
    observed[index++] = (AndamentoWorkdir3){uishell_sidebar_workspace(dir->workspace), {cwd.data, cwd.len}};
    for(U64 i = 0; i < sizeof(dir->workspace.v); i++) { dirs_hash = dirs_hash*33 + dir->workspace.v[i]; }
    dirs_hash = dirs_hash*33 + dir->value.view_id;
    dirs_hash = dirs_hash*33 + cwd.len;
    for(U64 i = 0; i < cwd.len; i++) { dirs_hash = dirs_hash*33 + cwd.data[i]; }
  }
  // Each core controls one window. HTTP discovery unions all windows, while
  // focus and derived open state stay local to the window owning this split.
  B32 changed = 0, topology_ready = 1;
  if(hash != state->topology_hash)
  {
    char *error = 0;
    topology_ready = andamento_observe3(state->core, items, count, 0, 0, &error);
    if(uishell_sidebar_result(state, topology_ready, error))
    { state->topology_hash = hash; changed = 1; uishell_sidebar_register(state, split); }
  }
  U64 now = wheelhouse_ingress_now_ms();
  if(topology_ready && dirs_hash != state->workdirs_hash && now >= state->workdirs_retry_at)
  {
    char *error = 0;
    B32 ok = andamento_observe_workdirs3(state->core, observed, dirs.count, &error);
    if(uishell_sidebar_result(state, ok, error))
    { state->workdirs_hash = dirs_hash; state->workdirs_retry_at = 0; changed = 1; }
    else { state->workdirs_retry_at = now+1000; }
  }
  if(changed) { uishell_sidebar_refresh(state); rd_request_frame(); }
  if(topology_ready) { uishell_sidebar_publish(state, split); }
  scratch_end(scratch);
}

#include "uishell/uishell_sidebar_records.c"

internal UIShell_SidebarState *
uishell_sidebar_init(RD_WindowState *ws)
{
  if(ws->sidebar == 0) { ws->sidebar = push_array(ws->arena, UIShell_SidebarState, 1); }
  UIShell_SidebarState *state = ws->sidebar;
  if(!state->initialized)
  {
    state->initialized = 1;
    char *error = 0;
    // ABI 3 adds Workspace IDs to ABI 2, and this host names workspaces only
    // by them.
    if(andamento_abi_version() != 3)
    {
      Temp scratch = scratch_begin(0, 0);
      uishell_sidebar_set_error(state, push_str8f(scratch.arena, "Andamento ABI 3 is required; this library has ABI %u",
        (U32)andamento_abi_version()));
      scratch_end(scratch);
      return state;
    }
    String8 config = uishell_sidebar_live ? uishell_sidebar_live_config :
      str8_cstring((char *)(uishell_sidebar_subject_fixture ? uishell_sidebar_daily_config : uishell_sidebar_fixture ? uishell_sidebar_fixture_config : uishell_sidebar_local_config));
    state->core = andamento_create(config.str, config.size, &error);
    if(uishell_sidebar_result(state, state->core != 0, error))
    {
      String8 patches = uishell_sidebar_fixture && !uishell_sidebar_live ? str8_cstring((char *)uishell_sidebar_fixture_patches) : str8_zero();
      for(U64 start = 0; start < patches.size;)
      {
        U64 end = start;
        while(end < patches.size && patches.str[end] != '\n') { end++; }
        if(end > start)
        {
          error = 0;
          B32 ok = andamento_apply_patch_json(state->core, 0, uishell_sidebar_text(str8(patches.str+start, end-start)), &error);
          if(!uishell_sidebar_result(state, ok, error)) { break; }
        }
        start = end+1;
      }
#if OS_WINDOWS
      if(uishell_sidebar_fixture && !uishell_sidebar_live)
      {
      AndamentoFact recipe = {0};
      recipe.key = uishell_sidebar_text(str8_lit("action.primary.recipe"));
      recipe.kind = ANDAMENTO_FACT_TEXT;
      recipe.text = uishell_sidebar_text(str8_lit("cmd.exe /K echo Wheelhouse sidebar fixture"));
      error = 0;
      B32 ok = andamento_apply_entity(state->core, 0, uishell_sidebar_text(str8_lit("vessel")), uishell_sidebar_text(str8_lit("v")), uishell_sidebar_text(str8_lit("fixture")), &recipe, 1, &error);
      uishell_sidebar_result(state, ok, error);
      }
#endif
      // Workspaces saved before Workspace IDs get theirs now, before records
      // name them.
      CFG_Node *window = cfg_node_from_id(ws->cfg_id);
      for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next)
      {
        if(str8_match(c->string, str8_lit("workspace"), 0) || str8_match(c->string, str8_lit("detached_workspace"), 0))
        { uishell_workspace_id_from_cfg(c); }
      }
      state->window = ws->cfg_id;
      uishell_sidebar_records_load(state, window);
      uishell_sidebar_refresh(state);
    }
  }
  return state;
}

// Populate ordinary workspace configuration once. Focus and restoration reuse
// that configuration, including any panel moves and tab selections by the user.
// Returns the primary resource's view tab.
internal CFG_Node *
uishell_sidebar_populate(CFG_Node *workspace, const UIShell_SidebarResource *resources, U64 count, String8 cwd)
{
  CFG_Node *primary_tab = &cfg_nil_node;
  CFG_Node *panels = cfg_node_new(rd_state->cfg, workspace, str8_lit("panels"));
  CFG_Node *primary = panels, *overflow = panels;
  if(count > 1)
  {
    cfg_node_new(rd_state->cfg, workspace, str8_lit("split_x"));
    primary = cfg_node_new(rd_state->cfg, panels, str8_lit("0.6"));
    overflow = cfg_node_new(rd_state->cfg, panels, str8_lit("0.4"));
  }
  cfg_node_new(rd_state->cfg, primary, str8_lit("selected"));
  for(U64 i = 0; i < count; i++)
  {
    const UIShell_SidebarResource *resource = &resources[i];
    char *command = resource->command;
#if OS_WINDOWS
    command = resource->windows_command;
#endif
    CFG_Node *tab = rd_cfg_new_view_tab(i == 0 ? primary : overflow, str8_lit("terminal"), str8_cstring(command), i <= 1);
    if(i == 0) { primary_tab = tab; }
    CFG_Node *id = cfg_node_new(rd_state->cfg, tab, str8_lit("resource_id"));
    cfg_node_new(rd_state->cfg, id, str8_cstring(resource->id));
    CFG_Node *label = cfg_node_new(rd_state->cfg, tab, str8_lit("label"));
    cfg_node_new(rd_state->cfg, label, str8_cstring(resource->label));
    if(cwd.size)
    {
      CFG_Node *dir = cfg_node_new(rd_state->cfg, tab, str8_lit("cwd"));
      cfg_node_new(rd_state->cfg, dir, cwd);
    }
  }
  return primary_tab;
}

// Effects change the real config tree. Bind identity before the terminal view
// can start, and acknowledge creation before the next topology observation.
internal void
uishell_sidebar_effects(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  char *error = 0;
  AndamentoEffects *effects = andamento_effects_take(state->core, &error);
  if(!uishell_sidebar_result(state, effects != 0, error)) { return; }
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  for(U64 i = 0; i < andamento_effects_count(effects); i++)
  {
    AndamentoEffect effect = {0};
    if(!andamento_effects_get(effects, i, &effect)) { continue; }
    CFG_Node *workspace = &cfg_nil_node;
    uint32_t outcome = ANDAMENTO_COMPLETE_ERROR;
    String8 failure = str8_lit("Workspace is no longer available");
    if(effect.kind == ANDAMENTO_EFFECT_OPEN_URL || effect.kind == ANDAMENTO_EFFECT_COPY_URL)
    {
      String8 url = uishell_sidebar_string(effect.recipe);
      // Andamento subject_url accepts only http:// or https:// before emitting this effect.
      if(effect.kind == ANDAMENTO_EFFECT_OPEN_URL) { wm_open_in_browser(url); }
      else { wm_set_clipboard_text(url); }
      continue; // URL actions own no workspace request and need no completion.
    }
    if(effect.kind == ANDAMENTO_EFFECT_MATERIALIZE)
    {
      // Reuse a previously created fixture workspace after switching modes or
      // restarting the app; its persisted identity is independent of its label.
      // A detached workspace reattaches with the layout it was detached with.
      for(CFG_Node *c = split->owner_cfg->first; c != &cfg_nil_node; c = c->next)
      {
        if((str8_match(c->string, str8_lit("workspace"), 0) || str8_match(c->string, str8_lit("detached_workspace"), 0)) &&
           str8_match(cfg_node_child_from_string(c, str8_lit("sidebar_entity_kind"))->first->string, uishell_sidebar_string(effect.entity_kind), 0) &&
           str8_match(cfg_node_child_from_string(c, str8_lit("sidebar_entity_id"))->first->string, uishell_sidebar_string(effect.entity_id), 0))
        { workspace = c; break; }
      }
      if(str8_match(workspace->string, str8_lit("detached_workspace"), 0))
      { cfg_node_equip_string(rd_state->cfg, workspace, str8_lit("workspace")); }
      if(workspace == &cfg_nil_node && effect.recipe.len != 0)
      {
        workspace = cfg_node_new(rd_state->cfg, split->owner_cfg, str8_lit("workspace"));
        uishell_workspace_id_from_cfg(workspace);
        CFG_Node *label = cfg_node_new(rd_state->cfg, workspace, str8_lit("label"));
        cfg_node_new(rd_state->cfg, label, uishell_sidebar_string(effect.name));
        CFG_Node *kind = cfg_node_new(rd_state->cfg, workspace, str8_lit("sidebar_entity_kind"));
        cfg_node_new(rd_state->cfg, kind, uishell_sidebar_string(effect.entity_kind));
        CFG_Node *id = cfg_node_new(rd_state->cfg, workspace, str8_lit("sidebar_entity_id"));
        cfg_node_new(rd_state->cfg, id, uishell_sidebar_string(effect.entity_id));
        String8 cwd = effect.has_cwd ? uishell_sidebar_string(effect.cwd) : str8_zero();
        if(uishell_sidebar_fixture && !uishell_sidebar_live && str8_match(uishell_sidebar_string(effect.entity_kind), str8_lit("vessel"), 0) &&
           str8_match(uishell_sidebar_string(effect.entity_id), str8_lit("multi"), 0))
        {
          uishell_sidebar_populate(workspace, uishell_sidebar_fixture_resources,
                                   ArrayCount(uishell_sidebar_fixture_resources), cwd);
        }
        else
        {
          Temp scratch = scratch_begin(0, 0);
          String8 command = uishell_sidebar_string(effect.recipe);
          U8 *command_z = push_array(scratch.arena, U8, command.size+1);
          MemoryCopy(command_z, command.str, command.size);
          UIShell_SidebarResource resource = {"primary", "Terminal", (char *)command_z, (char *)command_z};
          CFG_Node *tab = uishell_sidebar_populate(workspace, &resource, 1, cwd);
          // Content opened from the current managed resolution is already
          // current; recording its target stops the first plan restarting it.
          AndamentoText managed_target = {0};
          if(andamento_effects_primary_target(effects, i, &managed_target))
          { uishell_managed_set(tab, str8_lit("managed_target"), uishell_sidebar_string(managed_target)); }
          scratch_end(scratch);
        }
      }
      failure = str8_lit("No terminal command is available for this entry");
      if(workspace != &cfg_nil_node) { outcome = ANDAMENTO_COMPLETE_MATERIALIZE; }
    }
    else if(effect.kind == ANDAMENTO_EFFECT_FOCUS)
    {
      AndamentoWorkspaceId focus = {0};
      andamento_effects_workspace(effects, i, &focus);
      for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
      { if(uishell_workspace_id_match(w->workspace_id, uishell_workspace_id_from_andamento(focus))) { workspace = w->mount.owner_cfg; break; } }
      if(workspace != &cfg_nil_node)
      {
        outcome = ANDAMENTO_COMPLETE_FOCUS;
        char *retry_error = 0;
        andamento_content_retry3(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(workspace)), &retry_error);
        state->managed_dirty = 1;
        andamento_string_free(retry_error);
      }
    }
    else if(effect.kind == ANDAMENTO_EFFECT_INSPECT)
    {
      Temp scratch = scratch_begin(0, 0);
      String8 message = push_str8f(scratch.arena, "%S: %S", uishell_sidebar_string(effect.entity_kind), uishell_sidebar_string(effect.entity_id));
      U64 size = Min(message.size, sizeof(state->inspection)-1);
      MemoryCopy(state->inspection, message.str, size);
      state->inspection[size] = 0;
      scratch_end(scratch);
      continue;
    }
    if(workspace != &cfg_nil_node)
    {
      ws->root_controlled_split_initialized = 1;
      ws->root_controlled_split_selected_workspace_id = workspace->id;
      ws->active_panel_id = 0;
      ws->window_layout_reset = 1;
    }
    error = 0;
    // A subject's workspace keeps its Workspace ID when it is found again.
    UIShell_WorkspaceId completed = workspace == &cfg_nil_node ? (UIShell_WorkspaceId){0} : uishell_workspace_id_from_cfg(workspace);
    B32 ok = andamento_complete3(state->core, effect.request_id, outcome, uishell_sidebar_workspace(completed), uishell_sidebar_text(failure), &error);
    uishell_sidebar_result(state, ok, error);
  }
  andamento_effects_release(effects);
}

// Reveal is a navigation request, never an activation or materialization.
// Choose the deepest occurrence, preferring the first section on equal depth.
internal U64
uishell_sidebar_reveal_target(UIShell_SidebarState *state, U64 workspace_id)
{
  if(state == 0 || state->snapshot == 0) { return ANDAMENTO_NONE; }
  U64 result = ANDAMENTO_NONE, best_depth = 0;
  U64 count = andamento_snapshot_node_count(state->snapshot);
  for(U64 i = 0; i < count; i++)
  {
    AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    if(node.is_section || node.state != ANDAMENTO_LIVE || node.workspace_id != workspace_id) { continue; }
    U64 depth = 0;
    for(U64 parent = node.parent; parent != ANDAMENTO_NONE; depth++)
    {
      AndamentoNode ancestor = {0}; uishell_sidebar_snapshot_node(state->snapshot, parent, &ancestor);
      parent = ancestor.parent;
    }
    if(result == ANDAMENTO_NONE || depth > best_depth) { result = i; best_depth = depth; }
  }
  return result;
}

// Use the same occurrence as Reveal. Snapshot labels belong to the placement,
// not to the workspace title; the latter is only the unplaced fallback.
internal String8
uishell_sidebar_workspace_path(Arena *arena, UIShell_SidebarState *state,
                               U64 workspace_id, String8 fallback, String8 *leaf_out)
{
  U64 target = uishell_sidebar_reveal_target(state, workspace_id);
  if(leaf_out) { *leaf_out = fallback; }
  if(target == ANDAMENTO_NONE) { return push_str8_copy(arena, fallback); }
  U64 count = andamento_snapshot_node_count(state->snapshot);
  U64 *path = push_array(arena, U64, count);
  U64 length = 0;
  for(U64 at = target; at != ANDAMENTO_NONE && length < count;)
  {
    AndamentoNode node = {0};
    if(at >= count) { break; }
    // Sections, including ones someone made, and a section's only group
    // (which shows no header) aren't part of the path.
    UIShell_SidebarRole role = uishell_sidebar_node_at(state, at, &node);
    if(!node.is_section && role != UIShell_SidebarRole_PassThrough) { path[length++] = at; }
    at = node.parent;
  }
  String8 result = str8_zero();
  for(U64 i = length; i > 0; i--)
  {
    AndamentoNode node = {0};
    uishell_sidebar_snapshot_node(state->snapshot, path[i-1], &node);
    String8 label = uishell_sidebar_string(node.label);
    if(i == 1 && label.size && leaf_out) { *leaf_out = label; }
    if(label.size) { result = result.size ? push_str8f(arena, "%S  ›  %S", result, label) : push_str8_copy(arena, label); }
  }
  return result.size ? result : push_str8_copy(arena, fallback);
}

internal void
uishell_sidebar_expand_reveal(UIShell_SidebarState *state)
{
  // Each toggle changes the snapshot generation. Acquire a fresh snapshot
  // before choosing another ancestor action; never reuse a stale action ref.
  U64 limit = andamento_snapshot_node_count(state->snapshot);
  for(U64 step = 0; state->reveal_workspace_id && step < limit; step++)
  {
    U64 target = uishell_sidebar_reveal_target(state, state->reveal_workspace_id);
    if(target == ANDAMENTO_NONE) { state->reveal_workspace_id = 0; break; }
    AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, target, &node);
    size_t toggle = ANDAMENTO_NONE;
    for(U64 parent = node.parent; parent != ANDAMENTO_NONE;)
    {
      AndamentoNode ancestor = {0}; uishell_sidebar_snapshot_node(state->snapshot, parent, &ancestor);
      if(!ancestor.is_section && ancestor.collapsed) { toggle = ancestor.toggle; break; }
      parent = ancestor.parent;
    }
    if(toggle == ANDAMENTO_NONE) { break; }
    char *error = 0;
    B32 ok = uishell_sidebar_dispatch(state, toggle, &error);
    if(!uishell_sidebar_result(state, ok, error)) { state->reveal_workspace_id = 0; break; }
    uishell_sidebar_refresh(state);
  }
}

// Interactive compact-sidebar prototype; see docs/design/andamento-sidebar-prototype.md.
// Borderless controls retain the toolkit's keyboard activation and focus cues.
internal UI_Signal
uishell_sidebar_button(String8 text)
{
  UI_Box *box = ui_build_box_from_string(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText|
    UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects, text);
  return ui_signal_from_box(box);
}

// Sections and cards share a left-side grip; the owner supplies the drag action.
internal UI_Signal
uishell_sidebar_grip(String8 key, String8 description)
{
  UI_Signal signal = {0};
  UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center) UI_TagF("weak")
  UI_HoverCursor(WM_Cursor_HandPoint) RD_Font(RD_FontSlot_Main)
  {
    UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText|
      UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects|UI_BoxFlag_DisableTruncatedHover,
      "⋮⋮###%S", key);
    signal = ui_signal_from_box(box);
    if(ui_hovering(signal)) UI_Tooltip
    { ui_state->tooltip_anchor_key = box->key; RD_Font(RD_FontSlot_Main) { ui_label(description); } }
  }
  return signal;
}

// Section and pinned-area headers share one quiet control style (#210): no
// border, a hover fill, a soft fill while on. A hidden control keeps its box
// and width (so the title never jumps) but draws nothing and takes no input.
// The tooltip is built outside the control's style scope; inside it, the
// tooltip inherited the control's background and width. Card caps use it too
// (#269), so every header control shares one glyph set and minimum hit width.
enum { UIShell_ControlMinimumPT = 22 };
internal Vec4F32 uishell_sidebar_selection_fill(B32 exact_action);
internal UI_Signal
uishell_sidebar_header_button(String8 glyph, String8 key, B32 on, B32 shown, String8 title, String8 detail)
{
  UI_Box *box;
  UI_PrefWidth(ui_px(Max((F32)UIShell_ControlMinimumPT, floor_f32(ui_top_font_size()*1.7f)), 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center)
  UI_CornerRadius(3.f) UI_BackgroundColor(uishell_sidebar_selection_fill(0))
  {
    // DrawText stays set even when hidden: the box keeps its last string
    // otherwise, and a hidden control would keep showing its glyph.
    box = ui_build_box_from_stringf(UI_BoxFlag_DisableTruncatedHover|UI_BoxFlag_DrawText|
      (shown ? UI_BoxFlag_Clickable|UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects : 0)|
      (shown && on ? UI_BoxFlag_DrawBackground : 0), "%S###%S", shown ? glyph : str8_zero(), key);
  }
  UI_Signal sig = ui_signal_from_box(box);
  if(shown && ui_hovering(sig)) UI_Tooltip RD_Font(RD_FontSlot_Main)
  {
    ui_state->tooltip_anchor_key = box->key;
    ui_label(title);
    if(detail.size) UI_TagF("weak") { ui_label(detail); }
  }
  return sig;
}

// Use the shell's icon-font expander, not a text-font '>' in a narrow label.
// Padding and truncation are inappropriate for this fixed-size glyph.
internal UI_Signal
uishell_sidebar_disclosure(B32 expanded, String8 key)
{
  UI_Signal result = {0};
  UI_PrefWidth(ui_em(1.5f, 1)) UI_TextPadding(0)
  {
    result = ui_expander(expanded, key);
    result.box->flags |= UI_BoxFlag_DisableTextTrunc;
  }
  return result;
}

// How much further in than its group's title a group's rows' icons start.
global F32 uishell_sidebar_group_row_inset = 0.6f;

// The room before a group header's title, after its 3px accent: its text
// starts where its rows' icons do, `indent` ems in.
internal F32
uishell_sidebar_group_title_lead(F32 indent)
{
  return Max(0.f, ui_top_font_size()*indent-3.f-ui_top_text_padding());
}

// A group header's title stands out from its rows by weight, or by size when
// the UI font (one chosen in Settings) has no semibold of its own.
internal FNT_Tag
uishell_sidebar_group_title_font(void)
{
  return rd_font_from_slot(RD_FontSlot_MainSemibold);
}

internal F32
uishell_sidebar_group_title_size(void)
{
  B32 has_semibold = !fnt_tag_match(rd_font_from_slot(RD_FontSlot_MainSemibold), rd_font_from_slot(RD_FontSlot_Main));
  return has_semibold ? ui_top_font_size() : floor_f32(ui_top_font_size()*1.08f);
}

// Presentation defaults are local policy, independent of role/group identity.
// A future metadata suggestion/local override can replace this resolver.
internal Vec4F32
uishell_sidebar_project_accent(String8 identity)
{
  Vec4F32 colors[] = {
    {0.40f, 0.66f, 0.83f, 1.f}, {0.66f, 0.55f, 0.79f, 1.f},
    {0.74f, 0.64f, 0.40f, 1.f}, {0.43f, 0.72f, 0.61f, 1.f},
    {0.78f, 0.53f, 0.49f, 1.f}, {0.46f, 0.69f, 0.72f, 1.f},
  };
  return colors[u64_hash_from_str8(identity)%ArrayCount(colors)];
}

// A card's accent: a project's own colour; a local group's is neutral.
internal Vec4F32
uishell_sidebar_card_accent(AndamentoNode node)
{
  if(str8_match(uishell_sidebar_string(node.entity_kind), str8_lit(".group"), 0))
  { Vec4F32 text = ui_color_from_name(str8_lit("text")); text.w = 0.35f; return text; }
  return uishell_sidebar_project_accent(uishell_sidebar_string(node.entity_id));
}

// Press and hold: true on the frame a press on `sig` reaches the hold time,
// for a control's secondary menu. Any release ends the hold, including one
// dragged off the control, and the release after a hold is not a click.
enum { UIShell_HoldUS = 450000 };

internal B32
uishell_sidebar_held(UIShell_SidebarState *state, UI_Signal sig)
{
  U64 now = now_time_us();
  if(ui_pressed(sig)) { state->hold_key = sig.box->key; state->hold_us = now; }
  if(!ui_key_match(state->hold_key, sig.box->key)) { return 0; }
  if(!ui_dragging(sig)) { state->hold_key = ui_key_zero(); return 0; }
  if(now-state->hold_us < UIShell_HoldUS) { rd_request_frame(); return 0; }
  ui_kill_action();
  state->hold_key = ui_key_zero();
  return 1;
}

// One row look for tree rows, ghost rows and the drag lift. row_begin builds
// the slot and the row up to its label; the caller may add chips; row_end adds
// the status slot and closes the row, leaving the slot open for the caller's
// trailing margin (pop it with ui_pop_parent).
typedef struct UIShell_SidebarRow UIShell_SidebarRow;
struct UIShell_SidebarRow
{
  AndamentoNode node;
  B32 present;                    // the node is in the snapshot
  String8 key;                    // unique suffix for the row's boxes
  String8 text;                   // the label column
  String8 status;
  F32 height, width, indent;      // width 0 fills; indent in em
  B32 project, selected, contains_current;
  B32 disclosure, expanded;       // a disclosure leads the row, else a spacer
  B32 entry, icon_entry;          // the label, and the icon, are the row's buttons
  B32 clickable;                  // the whole row takes clicks (ghost rows)
  B32 reference;                  // ↗: a reference to a row that lives elsewhere
  B32 renaming;                   // the label is a field renaming the row (a local group)
  U64 count;                      // a group header's items, shown while collapsed
  F32 title_max;                  // a group header's room for its title, after its chips
  UI_Box *slot, *row;
  UI_Signal toggle, icon_sig, entry_sig, row_sig;
};

// Sidebar menus are compact: a hairline above and below, as wide as their
// longest item, with even padding either side of the text, and items styled
// as the shell's own menus ("implicit": no borders).
#define UIShell_SidebarMenu(key, width) UI_CtxMenuCompact(key) UI_TagF("implicit") UI_PrefWidth(ui_px((width), 1)) \
  UI_PrefHeight(ui_em(1.8f, 1)) UI_TextPadding(floor_f32(ui_top_font_size()*0.75f))

internal F32
uishell_sidebar_menu_width(String8 *labels, U64 count)
{
  F32 widest = 0;
  for(U64 i = 0; i < count; i++)
  { widest = Max(widest, fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, labels[i]).x); }
  return ceil_f32(widest + 2*floor_f32(ui_top_font_size()*0.75f) + 2.f);
}


// Opens a menu where a right-click or press happened, inside a large target;
// a small control's menu opens at its edge instead (offset from its box).
internal void
uishell_sidebar_menu_open_at_pointer(UI_Key menu, UI_Box *anchor)
{
  ui_ctx_menu_open(menu, anchor->key, sub_2f32(ui_mouse(), anchor->rect.p0));
}

internal void uishell_sidebar_row_begin(UIShell_SidebarState *state, UIShell_SidebarRow *r);
internal void uishell_sidebar_row_end(UIShell_SidebarState *state, UIShell_SidebarRow *r);

internal CFG_Node *uishell_sidebar_local_section(CFG_Node *window, String8 id);

// A section's menu, by its key: its header builds it, or one of its panel's
// other titles while it switches to it.
internal UI_Key
uishell_sidebar_section_menu_key(String8 section_key)
{ return ui_key_from_stringf(ui_key_zero(), "section_menu_%S", section_key); }
internal void uishell_sidebar_local_section_menu(UIShell_SidebarState *state, CFG_Node *window, CFG_Node *section, CFG_Node *view,
                                                 String8 loop, UI_Key menu);

//~ A sidebar section, its panel, collapses as a whole (sidebar-headers.md):
// the state is a panel option. A layout from before kept it on a View; the
// View's own still counts until the section next opens or closes; drop that
// fallback once layouts saved before #268 no longer need to load.

internal B32
uishell_sidebar_section_collapsed(CFG_Node *view)
{
  return cfg_node_child_from_string(view->parent, str8_lit("section_collapsed")) != &cfg_nil_node ||
    cfg_node_child_from_string(view, str8_lit("section_collapsed")) != &cfg_nil_node;
}

internal void
uishell_sidebar_section_set_collapsed(CFG_Node *view, B32 collapsed)
{
  CFG_Node *panel = view->parent;
  if(panel == &cfg_nil_node) { return; }
  for(CFG_Node *v = panel->first; v != &cfg_nil_node; v = v->next)
  { cfg_node_release(rd_state->cfg, cfg_node_child_from_string(v, str8_lit("section_collapsed"))); }
  if(collapsed) { cfg_node_child_from_string_or_alloc(rd_state->cfg, panel, str8_lit("section_collapsed")); }
  else { cfg_node_release(rd_state->cfg, cfg_node_child_from_string(panel, str8_lit("section_collapsed"))); }
}

//~ A section's header holds all its panel's Views (sidebar-headers.md). The
// selected one is the header; the others sit beside it as compact titles, in
// tab order, and those that don't fit go behind a "+N" chip. The row is the
// panel's tab strip: tab drops land between its entries.

typedef struct UIShell_HeaderTabs UIShell_HeaderTabs;
struct UIShell_HeaderTabs
{
  CFG_Node *panel;
  CFG_Node **v;      // the panel's tabs, in order
  String8 *titles;
  B32 *hidden;       // behind the "+N" chip
  UI_Box **boxes;    // each shown entry's box this build; the header's own is its title
  U64 count;
  U64 selected;      // the header's own View
  F32 width;         // each compact title's width
  B32 any_built;     // an entry is already in the row, so the next is separated
  UIShell_SidebarState *state;
  CFG_Node *window;
  UI_Box *anchor;    // a box that outlives the switch of header, for that menu
};

// A hairline between two of a header's entries, like an unselected tab's edge.
internal void
uishell_sidebar_header_separator(UIShell_HeaderTabs *tabs)
{
  if(tabs->count > 1 && tabs->any_built)
  {
    // A drawn line, centred in a slot, half the row's height.
    Vec4F32 color = ui_color_from_name(str8_lit("text"));
    color.w = 0.25f;
    UI_Box *slot, *band;
    UI_PrefWidth(ui_em(0.9f, 1)) UI_PrefHeight(ui_pct(1, 1)) UI_ChildLayoutAxis(Axis2_Y)
    { slot = ui_build_box_from_key(0, ui_key_zero()); }
    UI_Parent(slot)
    {
      ui_spacer(ui_pct(0.25f, 1));
      UI_PrefWidth(ui_pct(1, 1)) UI_PrefHeight(ui_pct(0.5f, 1)) UI_ChildLayoutAxis(Axis2_X)
      { band = ui_build_box_from_key(0, ui_key_zero()); }
      UI_Parent(band)
      {
        ui_spacer(ui_pct(1, 0));
        UI_PrefWidth(ui_px(1.f, 1)) UI_PrefHeight(ui_pct(1, 1)) UI_BackgroundColor(color)
        { ui_build_box_from_key(UI_BoxFlag_DrawBackground, ui_key_zero()); }
        ui_spacer(ui_pct(1, 0));
      }
    }
  }
  tabs->any_built = 1;
}

// A View's title, as its tab would show it.
internal String8
uishell_sidebar_view_title(Arena *arena, CFG_Node *view)
{
  String8 label = rd_label_from_cfg(view);
  if(label.size) { return label; }
  DR_FStrList fstrs = rd_title_fstrs_from_cfg(arena, view, 0);
  return dr_string_from_fstrs(arena, &fstrs);
}

// Whether a panel's child is one of its tabs, as rd_arrangement_from_cfg
// reads them: an identifier that isn't a panel option.
internal B32
uishell_sidebar_is_tab(CFG_Node *c)
{
  U8 first = c->string.size ? c->string.str[0] : 0;
  return (char_is_alpha(first) || first == '_') && !str8_match(c->string, str8_lit("selected"), 0) &&
    !cfg_panel_child_is_option(c->string) &&
    !rd_cfg_is_project_filtered(c);
}

// A whole section, dragged by its grip, drops all its Views together
// (sidebar-headers.md): onto a header row they go in order at the gap, so
// a three-View section joins a one-View one as four; at an edge they make
// the new panel there. The panel tree is in use while drops are taken, so
// the move waits for uishell_sidebar_section_drop_apply.
internal void
uishell_sidebar_section_drop_commit(CFG_ID destination, Dir2 direction, CFG_ID previous_tab)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(cfg_node_from_id(rd_state->drag_drop_regs->window));
  if(ws == &rd_nil_window_state || !ws->sidebar || destination == rd_state->drag_drop_regs->panel) { return; }
  UIShell_SidebarState *state = ws->sidebar;
  state->section_drop = 1;
  state->section_drop_source = rd_state->drag_drop_regs->panel;
  state->section_drop_selected = rd_state->drag_drop_regs->view;
  state->section_drop_panel = destination;
  state->section_drop_direction = direction;
  state->section_drop_prev = previous_tab;
}

internal void
uishell_sidebar_section_drop_apply(RD_WindowState *ws)
{
  UIShell_SidebarState *state = ws->sidebar;
  if(!state || !state->section_drop) { return; }
  state->section_drop = 0;
  CFG_Node *source = cfg_node_from_id(state->section_drop_source), *destination = cfg_node_from_id(state->section_drop_panel);
  if(source == &cfg_nil_node || destination == &cfg_nil_node) { return; }
  Temp scratch = scratch_begin(0, 0);
  CFG_NodePtrList tabs = {0};
  for(CFG_Node *c = source->first; c != &cfg_nil_node; c = c->next) { if(uishell_sidebar_is_tab(c)) { cfg_node_ptr_list_push(scratch.arena, &tabs, c); } }
  B32 collapsed = cfg_node_child_from_string(source, str8_lit("section_collapsed")) != &cfg_nil_node;
  CFG_ID prev = state->section_drop_prev;
  CFG_NodePtrNode *n = tabs.first;
  if(n && state->section_drop_direction != Dir2_Invalid)
  {
    UIShell_RegsScope(.window = ws->cfg_id, .panel = source->id, .dst_panel = destination->id, .view = n->v->id, .dir2 = state->section_drop_direction)
    { uishell_dispatch_panel_command(str8_lit("split_panel")); }
    destination = n->v->parent;
    // The section keeps its collapse in its new place.
    if(collapsed) { cfg_node_child_from_string_or_alloc(rd_state->cfg, destination, str8_lit("section_collapsed")); }
    prev = n->v->id;
    n = n->next;
  }
  for(; n; n = n->next)
  {
    if(n->v->parent == destination) { continue; }
    UIShell_RegsScope(.window = ws->cfg_id, .panel = n->v->parent->id, .dst_panel = destination->id, .view = n->v->id, .prev_tab = prev)
    { uishell_dispatch_tab_command(str8_lit("move_view")); }
    prev = n->v->id;
  }
  CFG_Node *selected = cfg_node_from_id(state->section_drop_selected);
  if(selected->parent == destination)
  { UIShell_RegsScope(.window = ws->cfg_id, .panel = destination->id, .view = selected->id, .tab = selected->id) { uishell_cmd("focus_tab"); } }
  rd_request_frame();
  scratch_end(scratch);
}

// `view`'s panel's tabs, as rd_arrangement_from_cfg reads them. Compact
// titles share `budget`, each at least 3em wide.
internal UIShell_HeaderTabs
uishell_sidebar_header_tabs(Arena *arena, CFG_Node *view, F32 budget)
{
  UIShell_HeaderTabs tabs = {view->parent};
  U64 cap = 0;
  for(CFG_Node *c = view->parent->first; c != &cfg_nil_node; c = c->next) { cap += 1; }
  tabs.v = push_array(arena, CFG_Node *, cap);
  tabs.titles = push_array(arena, String8, cap);
  tabs.hidden = push_array(arena, B32, cap);
  tabs.boxes = push_array(arena, UI_Box *, cap);
  for(CFG_Node *c = view->parent->first; c != &cfg_nil_node; c = c->next)
  {
    if(!uishell_sidebar_is_tab(c)) { continue; }
    if(c == view) { tabs.selected = tabs.count; }
    tabs.titles[tabs.count] = upper_from_str8(arena, uishell_sidebar_view_title(arena, c));
    tabs.boxes[tabs.count] = &ui_nil_box;
    tabs.v[tabs.count++] = c;
  }
  F32 em = ui_top_font_size(), least = em*3.f;
  U64 others = tabs.count ? tabs.count-1 : 0, fit = others;
  if(others*least > budget) { fit = (U64)Max(0.f, (budget - em*1.7f)/least); }
  for(U64 i = 0, shown = 0; i < tabs.count; i++)
  {
    if(i == tabs.selected) { continue; }
    tabs.hidden[i] = shown >= fit;
    shown += 1;
  }
  tabs.width = fit ? Max(least, Min(em*10.f, budget/(F32)(fit + (fit < others)))) : 0;
  return tabs;
}

// Choosing a View shows it: a collapsed section opens.
internal void
uishell_sidebar_header_select(CFG_Node *panel, CFG_Node *tab)
{
  uishell_sidebar_section_set_collapsed(tab, 0);
  UIShell_RegsScope(.panel = panel->id, .view = tab->id, .tab = tab->id) { uishell_cmd("focus_tab"); }
}

// Compact titles for tabs [from, to): a click selects one, a drag moves its
// View, and a middle click closes it.
internal void
uishell_sidebar_header_tabs_ui(UIShell_HeaderTabs *tabs, U64 from, U64 to)
{
  for(U64 i = from; i < to && i < tabs->count; i++)
  {
    if(i == tabs->selected || tabs->hidden[i]) { continue; }
    uishell_sidebar_header_separator(tabs);
    CFG_Node *tab = tabs->v[i];
    F32 text = fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, tabs->titles[i]).x + ui_top_font_size();
    UI_Box *box;
    UI_PrefWidth(ui_px(Min(text, tabs->width), 1)) UI_TagF("weak") UI_CornerRadius(3.f)
    {
      box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText|UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects,
                                      "%S###header_tab_%p", tabs->titles[i], tab);
    }
    UI_Signal sig = ui_signal_from_box(box);
    // A right click shows the View and opens its section's menu, as the
    // selected title's does; its header builds the menu from next frame.
    String8 section_key = cfg_node_child_from_string(tab, str8_lit("section"))->first->string;
    CFG_Node *local = tabs->window ? uishell_sidebar_local_section(tabs->window, uishell_sidebar_local_key_id(section_key)) : &cfg_nil_node;
    if(local != &cfg_nil_node && tabs->anchor)
    {
      UI_Key menu = uishell_sidebar_section_menu_key(section_key);
      if(ui_right_clicked(sig))
      {
        tabs->state->confirm_delete = 0;
        uishell_sidebar_header_select(tabs->panel, tab);
        ui_ctx_menu_open(menu, tabs->anchor->key, sub_2f32(ui_mouse(), tabs->anchor->rect.p0));
      }
      // Until its header takes over, the title keeps the menu built.
      uishell_sidebar_local_section_menu(tabs->state, tabs->window, local, tab, str8_zero(), menu);
    }
    if(ui_dragging(sig) && !rd_drag_is_active() && length_2f32(ui_drag_delta()) > UIShell_DragThresholdPT)
    { UIShell_RegsScope(.panel = tabs->panel->id, .view = tab->id, .tab = tab->id) { rd_drag_begin(UIShell_ContextRegSlot_View); } }
    else if(ui_clicked(sig) && !rd_drag_is_active()) { uishell_sidebar_header_select(tabs->panel, tab); }
    else if(ui_middle_clicked(sig) && rd_dock_can_close(tab))
    { UIShell_RegsScope(.panel = tabs->panel->id, .view = tab->id, .tab = tab->id) { uishell_cmd("close_tab"); } }
    tabs->boxes[i] = box;
  }
}

// The "+N" chip and its menu of the titles that didn't fit.
internal void
uishell_sidebar_header_more_ui(UIShell_HeaderTabs *tabs)
{
  Temp scratch = scratch_begin(0, 0);
  U64 hidden = 0;
  String8 *labels = push_array(scratch.arena, String8, tabs->count+1);
  for(U64 i = 0; i < tabs->count; i++) { if(tabs->hidden[i]) { labels[hidden++] = tabs->titles[i]; } }
  if(hidden)
  {
    UI_Signal sig = uishell_sidebar_header_button(push_str8f(scratch.arena, "+%I64u", hidden),
      push_str8f(scratch.arena, "header_more_%p", tabs->panel), 0, 1, str8_lit("More views"), str8_zero());
    UI_Key menu = ui_key_from_stringf(sig.box->key, "menu");
    if(ui_clicked(sig)) { ui_ctx_menu_open(menu, sig.box->key, v2f32(0, dim_2f32(sig.box->rect).y)); }
    UIShell_SidebarMenu(menu, uishell_sidebar_menu_width(labels, hidden))
    {
      for(U64 i = 0; i < tabs->count; i++)
      {
        if(!tabs->hidden[i]) { continue; }
        if(ui_clicked(ui_button(push_str8f(scratch.arena, "%S###more_%p", tabs->titles[i], tabs->v[i]))))
        { uishell_sidebar_header_select(tabs->panel, tabs->v[i]); ui_ctx_menu_close(); }
      }
    }
  }
  scratch_end(scratch);
}

// While the panel's tab drop site (the header row, rd_panel_area_ui) has a
// View drag, the gap between entries nearest the pointer takes it, marked
// with an accent bar. Dropping a View beside itself changes nothing.
internal void
uishell_sidebar_header_drop(UIShell_HeaderTabs *tabs, UI_Box *header)
{
  if(!tabs->count || !rd_drag_is_active() || rd_state->drag_drop_regs_slot != UIShell_ContextRegSlot_View ||
     !ui_key_match(ui_drop_hot_key(), rd_panel_catchall_drop_site_key(tabs->panel))) { return; }
  if(rd_state->drag_drop_commit == uishell_sidebar_section_drop_commit && rd_state->drag_drop_regs->panel == tabs->panel->id) { return; }
  // The gap after the last shown entry left of the pointer's x.
  U64 gap = 0;
  B32 any = 0;
  for(U64 i = 0; i < tabs->count; i++)
  {
    UI_Box *box = tabs->boxes[i];
    if(ui_box_is_nil(box) || dim_2f32(box->rect).x <= 0) { continue; }
    any = 1;
    if(ui_mouse().x > center_2f32(box->rect).x) { gap = i+1; }
  }
  if(!any) { return; }
  // The bar sits between the entries either side of the gap.
  UI_Box *before = gap ? tabs->boxes[gap-1] : &ui_nil_box, *after = &ui_nil_box;
  for(U64 i = gap; i < tabs->count && ui_box_is_nil(after); i++)
  { if(!ui_box_is_nil(tabs->boxes[i]) && dim_2f32(tabs->boxes[i]->rect).x > 0) { after = tabs->boxes[i]; } }
  F32 at = ui_box_is_nil(before) ? after->rect.x0 - 2.f : ui_box_is_nil(after) ? before->rect.x1 + 2.f :
    (before->rect.x1 + after->rect.x0)*0.5f;
  CFG_Node *prev = gap ? tabs->v[gap-1] : &cfg_nil_node;
  CFG_Node *next = gap < tabs->count ? tabs->v[gap] : &cfg_nil_node;
  CFG_ID dragged = rd_state->drag_drop_regs->view;
  if(dragged && (prev->id == dragged || next->id == dragged)) { return; }
  F32 height = dim_2f32(header->rect).y;
  UI_Parent(header) UI_FixedX(at - header->rect.x0 - 1.5f) UI_FixedY(height*0.15f) UI_PrefWidth(ui_px(3.f, 1))
    UI_PrefHeight(ui_px(height*0.7f, 1)) UI_CornerRadius(1.5f) UI_BackgroundColor(rd_drop_accent(1.f))
  { ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Floating, ui_key_from_stringf(header->key, "tab_drop")); }
  rd_request_frame();
  if(rd_drag_drop()) { rd_panel_drag_drop(tabs->panel->id, Dir2_Invalid, prev->id); }
}

#include "uishell/uishell_hover_cards.c"
#include "uishell/uishell_local_groups.c"

// Native entry templates declare label/kind/status before optional context.
internal String8
uishell_sidebar_node_status(UIShell_SidebarState *state, AndamentoNode node)
{
  AndamentoField field = {0};
  if(node.field_count > 2)
  { andamento_snapshot_field(state->snapshot, node.first_field+2, &field); }
  return uishell_sidebar_string(field.text);
}

typedef enum UIShell_SidebarCloseKind
{
  UIShell_SidebarCloseKind_None,
  UIShell_SidebarCloseKind_Detach,
  UIShell_SidebarCloseKind_Destroy,
  // A ghost's margin control removes the ghost, never its source.
  UIShell_SidebarCloseKind_RemovePin,
}
UIShell_SidebarCloseKind;

// The affordance says what closing loses. A workspace with a live or unobserved
// subject detaches: its row stays and reopens the same layout. An ended subject
// or a subjectless workspace cannot come back, so closing destroys it.
internal UIShell_SidebarCloseKind
uishell_sidebar_close_kind(AndamentoNode node, String8 status)
{
  if(str8_match(uishell_sidebar_string(node.entity_kind), str8_lit(".ref"), 0)) { return UIShell_SidebarCloseKind_RemovePin; }
  if(node.is_section || node.state != ANDAMENTO_LIVE || node.workspace_id == 0) { return UIShell_SidebarCloseKind_None; }
  CFG_Node *workspace = cfg_node_from_id(node.workspace_id);
  if(workspace == &cfg_nil_node) { return UIShell_SidebarCloseKind_None; }
  if(str8_match(status, str8_lit("ended"), 0) || !uishell_workspace_cfg_has_subject(workspace))
  { return UIShell_SidebarCloseKind_Destroy; }
  return UIShell_SidebarCloseKind_Detach;
}

// Whether the sidebar shows this open workspace's subject as authoritatively
// ended. Commands use it so a palette detach never keeps an unreachable layout.
internal B32
uishell_sidebar_workspace_subject_ended(RD_WindowState *ws, CFG_ID workspace_id)
{
  UIShell_SidebarState *state = ws != &rd_nil_window_state ? ws->sidebar : 0;
  for(U64 i = 0; state && state->snapshot && i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && node.state == ANDAMENTO_LIVE && node.workspace_id == workspace_id &&
       str8_match(uishell_sidebar_node_status(state, node), str8_lit("ended"), 0))
    { return 1; }
  }
  return 0;
}

internal String8
uishell_sidebar_close_label(UIShell_SidebarCloseKind kind)
{
  return kind == UIShell_SidebarCloseKind_Detach ? str8_lit("Detach workspace") :
    kind == UIShell_SidebarCloseKind_RemovePin ? str8_lit("Remove pin") : str8_lit("Close workspace");
}

internal B32 uishell_sidebar_is_subject(AndamentoNode node);

// A row menu's width, from the items that row's menu shows
// (uishell_sidebar_entry_signal).
internal F32
uishell_sidebar_row_menu_width(AndamentoNode node, UIShell_SidebarCloseKind close)
{
  String8 labels[6] = {str8_lit("Reset order")};
  U64 count = 1;
  if(close == UIShell_SidebarCloseKind_RemovePin)
  {
    labels[count++] = str8_lit("Go to source"); labels[count++] = str8_lit("Show as card"); labels[count++] = str8_lit("Remove pin");
  }
  else
  {
    if(uishell_sidebar_is_subject(node)) { labels[count++] = str8_lit("Open in browser"); labels[count++] = str8_lit("Copy reference"); }
    if(close != UIShell_SidebarCloseKind_None) { labels[count++] = uishell_sidebar_close_label(close); }
    if(close == UIShell_SidebarCloseKind_Detach) { labels[count++] = str8_lit("Close and discard layout"); }
  }
  return uishell_sidebar_menu_width(labels, count);
}

internal void
uishell_sidebar_close_workspace(RD_WindowState *ws, AndamentoNode node, UIShell_SidebarCloseKind kind)
{
  // uishell_cmd takes a string literal: str8_lit of a ternary is a pointer's size.
  if(kind == UIShell_SidebarCloseKind_Detach) { uishell_cmd("detach_workspace", .window = ws->cfg_id, .cfg = node.workspace_id); }
  if(kind == UIShell_SidebarCloseKind_Destroy) { uishell_cmd("close_workspace", .window = ws->cfg_id, .cfg = node.workspace_id); }
  if(kind == UIShell_SidebarCloseKind_RemovePin)
  {
    CFG_Node *saved = uishell_sidebar_pin_by_ghost(cfg_node_from_id(ws->cfg_id), uishell_sidebar_string(node.entity_id));
    if(saved != &cfg_nil_node) { cfg_node_release(rd_state->cfg, saved); rd_request_frame(); }
  }
}

// A detached workspace keeps its layout; its menu also offers to discard it.
internal void
uishell_sidebar_discard_button(RD_WindowState *ws, AndamentoNode node, UIShell_SidebarCloseKind close_kind)
{
  if(close_kind == UIShell_SidebarCloseKind_Detach && ui_clicked(ui_button(str8_lit("Close and discard layout"))))
  { uishell_sidebar_close_workspace(ws, node, UIShell_SidebarCloseKind_Destroy); ui_ctx_menu_close(); }
}

// Rows keep their status mark; close and detach live in the right margin
// beside the row. A click runs the default (detach keeps the layout, ×
// discards it); holding opens the row's menu, which offers the other close.
// Only the button belongs in the caller's floating, sized scope; the tooltip
// and menu must not inherit it, so uishell_sidebar_margin_close handles them.
internal UI_Signal
uishell_sidebar_margin_button(AndamentoNode node, UIShell_SidebarCloseKind close)
{
  Temp scratch = scratch_begin(0, 0);
  UI_Signal sig = {0};
  UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center) UI_CornerRadius(3.f)
  {
    sig = uishell_sidebar_button(push_str8f(scratch.arena, "%S###close_%S",
      close == UIShell_SidebarCloseKind_Detach ? str8_zero() : str8_lit("×"), uishell_sidebar_string(node.key)));
  }
  if(close == UIShell_SidebarCloseKind_Detach) { ui_box_equip_custom_draw(sig.box, rd_workspace_detach_icon_draw, 0); }
  scratch_end(scratch);
  return sig;
}

internal void
uishell_sidebar_margin_close(UIShell_SidebarState *state, RD_WindowState *ws, AndamentoNode node,
                             UIShell_SidebarCloseKind close, UI_Signal sig, UI_Key entry_key, UI_Key menu_key)
{
  F32 em = ui_top_font_size();
  if(ui_hovering(sig) && !ui_dragging(sig)) UI_Tooltip
  {
    ui_state->tooltip_anchor_key = sig.box->key;
    ui_label(uishell_sidebar_close_label(close));
    B32 unplaced = str8_match(uishell_sidebar_string(node.entity_kind), str8_lit(".workspace"), 0);
    UI_TagF("weak")
    {
      ui_label(close == UIShell_SidebarCloseKind_RemovePin ? str8_lit("Its source is untouched.") :
               close == UIShell_SidebarCloseKind_Destroy ? str8_lit("The workspace and its layout are discarded.") :
               unplaced ? str8_lit("Its layout reopens when its subject is seen again.") :
               str8_lit("Its layout reopens from this entry."));
      ui_label(str8_lit("Hold for more"));
    }
  }
  // Below the button, right-aligned to it, so the menu stays over the sidebar.
  if(uishell_sidebar_held(state, sig))
  { ui_ctx_menu_open(menu_key, sig.box->key, v2f32(dim_2f32(sig.box->rect).x-uishell_sidebar_row_menu_width(node, close), dim_2f32(sig.box->rect).y)); }
  if(ui_clicked(sig)) { uishell_sidebar_close_workspace(ws, node, close); }
}

// One sibling of the dragged row as built this frame, with last frame's rects.
// The extent covers the sibling's project box or descendant rows.
typedef struct UIShell_RowDragSibling UIShell_RowDragSibling;
struct UIShell_RowDragSibling
{
  U64 index;
  Rng2F32 row, extent;
};

// Rows drag from their body past the shared threshold, as the same sidebar
// drag as cards (uishell_sidebar_drag_begin); render claims a reorder.
internal void
uishell_sidebar_row_drag_from(UIShell_SidebarState *state, RD_WindowState *ws,
                              AndamentoNode node, U64 node_index, UI_Signal sig)
{
  String8 key = uishell_sidebar_string(node.key);
  if(ui_dragging(sig) && !rd_drag_is_active() && !state->row_drag_key.size &&
     length_2f32(ui_drag_delta()) > UIShell_DragThresholdPT)
  {
    Temp scratch = scratch_begin(0, 0);
    String8 loop = uishell_sidebar_loop_key(state->snapshot, node_index);
    AndamentoEntity *siblings = 0;
    // A lone row can't reorder but can still be dropped as a ghost.
    if(uishell_sidebar_siblings(scratch.arena, state->snapshot, loop, &siblings) > 0)
    {
      if(!state->row_drag_arena) { state->row_drag_arena = arena_alloc(); }
      arena_clear(state->row_drag_arena);
      state->row_drag_key = push_str8_copy(state->row_drag_arena, key);
      state->row_drag_loop = push_str8_copy(state->row_drag_arena, loop);
      state->row_drag_label = push_str8_copy(state->row_drag_arena, uishell_sidebar_string(node.label));
      state->row_drag_entity = uishell_sidebar_card_entity_copy(state->row_drag_arena, (AndamentoEntity){node.entity_kind, node.entity_id});
      state->row_drag_rect = state->building_row ? state->building_row->rect : sig.box->rect;
      state->row_drag_workspace = node.state == ANDAMENTO_LIVE ? node.workspace_id : 0;
      state->row_drag_released = 0;
      uishell_sidebar_drag_begin(ws);
      UIShell_HoverCard *card = &state->cards[0];
      if(card->open && card->placement == UIShell_CardPlacement_Transient && !card->focused)
      { uishell_sidebar_card_close(card); }
      rd_request_frame();
    }
    scratch_end(scratch);
  }
  if(ui_released(sig) && state->row_drag_key.size && str8_match(state->row_drag_key, key, 0))
  { state->row_drag_released = 1; rd_request_frame(); }
}

internal void
uishell_sidebar_order_reset_button(UIShell_SidebarState *state, String8 loop)
{
  if(ui_clicked(ui_button(str8_lit("Reset order"))))
  {
    state->order_pending = 1;
    state->order_loop = push_str8_copy(ui_build_arena(), loop);
    state->order = 0; state->order_count = 0;
    ui_ctx_menu_close();
  }
}

internal size_t
uishell_sidebar_entry_signal(UIShell_SidebarState *state, RD_WindowState *ws,
                             AndamentoNode node, U64 node_index, UI_Signal sig, String8 context,
                             B32 contains_current, B32 menu, B32 header)
{
  size_t action = ANDAMENTO_NONE;
  F32 em = ui_top_font_size();
  String8 full_label = uishell_sidebar_string(node.label);
  String8 kind = uishell_sidebar_string(node.entity_kind);
  B32 subject = str8_match(kind, str8_lit("change_request"), 0) || str8_match(kind, str8_lit("issue"), 0);
  size_t copy_url = subject ? andamento_snapshot_copy_url_action(state->snapshot, node_index) : ANDAMENTO_NONE;
  B32 ended = str8_match(uishell_sidebar_node_status(state, node), str8_lit("ended"), 0);
  UIShell_SidebarCloseKind close_kind = uishell_sidebar_close_kind(node, uishell_sidebar_node_status(state, node));
  B32 can_activate = node.activate != ANDAMENTO_NONE && (node.openable || node.state == ANDAMENTO_LIVE || copy_url != ANDAMENTO_NONE);
  Temp scratch = scratch_begin(0, 0);
  sig.box->flags |= UI_BoxFlag_DisableTruncatedHover;
  // A row drag ends in a release; it must not also activate the row.
  B32 row_drag = state->row_drag_key.size != 0;
  if(!menu) { uishell_sidebar_row_drag_from(state, ws, node, node_index, sig); }
  // A run the window has reordered offers Reset order in the row's menu.
  String8 loop = str8_zero(); B32 ordered = 0;
  if(!menu && (ui_right_clicked(sig) || ui_any_ctx_menu_is_open()))
  {
    loop = uishell_sidebar_loop_key(state->snapshot, node_index);
    ordered = uishell_sidebar_order_saved(state, loop);
  }
  // A group header's click collapses it; the release ending a double click
  // that renames it does not.
  B32 swallow = header && ui_key_match(state->header_click_swallow, sig.box->key);
  if(swallow && ui_released(sig)) { state->header_click_swallow = ui_key_zero(); }
  if(header)
  {
    if(ui_clicked(sig) && !row_drag && !swallow) { action = node.toggle; }
  }
  else if(ui_clicked(sig) && !row_drag)
  {
    if(can_activate) { action = node.activate; }
    else
    {
      String8 detail = subject ? push_str8f(scratch.arena, "%S — URL unavailable", full_label) :
        push_str8f(scratch.arena, "%S (%S) — no opening recipe", full_label, kind);
      U64 size = Min(detail.size, sizeof(state->inspection)-1);
      MemoryCopy(state->inspection, detail.str, size); state->inspection[size] = 0;
      rd_request_frame();
    }
  }
  if(!menu && str8_match(uishell_sidebar_string(node.entity_kind), str8_lit(".group"), 0))
  {
    // A local group's header: its menu (uishell_local_groups.c), with its
    // items' loop for Reset order.
    CFG_Node *window = cfg_node_from_id(ws->cfg_id);
    String8 items_loop = str8_zero();
    AndamentoNode first = {0};
    if(uishell_sidebar_snapshot_node(state->snapshot, node_index+1, &first) && first.parent == node_index)
    { items_loop = uishell_sidebar_loop_key(state->snapshot, node_index+1); }
    UI_Key menu_key = ui_key_from_stringf(sig.box->key, "group_menu");
    CFG_Node *group = uishell_sidebar_local_group(window, uishell_sidebar_string(node.entity_id));
    uishell_sidebar_local_group_menu(state, window, group, items_loop, menu_key);
    if(ui_right_clicked(sig)) { state->confirm_delete = 0; uishell_sidebar_menu_open_at_pointer(menu_key, sig.box); }
    if(header && state->header_more_open)
    {
      state->header_more_open = 0; state->confirm_delete = 0;
      ui_ctx_menu_open(menu_key, state->header_more_anchor, v2f32(0, em*1.8f));
    }
    // Double-clicking it renames it in place. The double click's first click
    // collapsed it, so it opens again, and its closing release is no click.
    if(ui_double_clicked(sig) && group != &cfg_nil_node)
    {
      uishell_sidebar_local_begin_rename(state, group, uishell_sidebar_local_field(group, str8_lit("label")));
      if(header) { action = node.toggle; state->header_click_swallow = sig.box->key; }
    }
  }
  else if(header)
  {
    // A project's header: New workspace here (it lives with the project),
    // and Reset order once its rows have been reordered.
    UI_Key menu_key = ui_key_from_stringf(sig.box->key, "project_menu");
    String8 items_loop = str8_zero();
    AndamentoNode first = {0};
    if(uishell_sidebar_snapshot_node(state->snapshot, node_index+1, &first) && first.parent == node_index)
    { items_loop = uishell_sidebar_loop_key(state->snapshot, node_index+1); }
    String8 labels[] = {str8_lit("New workspace here"), str8_lit("Reset order")};
    UIShell_SidebarMenu(menu_key, uishell_sidebar_menu_width(labels, ArrayCount(labels)))
    {
      if(ui_clicked(ui_button(str8_lit("New workspace here"))))
      {
        uishell_sidebar_make(cfg_node_from_id(ws->cfg_id),
          (UIShell_MakeAction){UIShell_Make_ProjectWorkspace, str8_lit("New workspace"), 0, uishell_sidebar_string(node.entity_id)}, str8_zero());
        ui_ctx_menu_close();
      }
      if(items_loop.size && uishell_sidebar_order_saved(state, items_loop)) { uishell_sidebar_order_reset_button(state, items_loop); }
    }
    if(ui_right_clicked(sig)) { uishell_sidebar_menu_open_at_pointer(menu_key, sig.box); }
    if(state->header_more_open) { state->header_more_open = 0; ui_ctx_menu_open(menu_key, state->header_more_anchor, v2f32(0, em*1.8f)); }
  }
  else if(subject)
  {
    uishell_sidebar_subject_hit(node, sig.box, "subject", menu);
    UI_Key menu_key = ui_key_from_stringf(sig.box->key, "subject_menu");
    UIShell_SidebarMenu(menu_key, uishell_sidebar_row_menu_width(node, close_kind))
    {
      // The retained snapshot gives every placed subject an activate action.
      if(copy_url != ANDAMENTO_NONE && node.activate != ANDAMENTO_NONE && ui_clicked(ui_button(str8_lit("Open in browser"))))
      { action = node.activate; ui_ctx_menu_close(); }
      UI_Signal copy = ui_button(copy_url != ANDAMENTO_NONE ? str8_lit("Copy URL") : str8_lit("Copy reference"));
      uishell_sidebar_subject_hit(node, copy.box, "copy", 1);
      if(ui_clicked(copy))
      {
        if(copy_url != ANDAMENTO_NONE) { action = copy_url; }
        else { wm_set_clipboard_text(full_label); }
        ui_ctx_menu_close();
      }
      if(close_kind != UIShell_SidebarCloseKind_None && ui_clicked(ui_button(uishell_sidebar_close_label(close_kind))))
      { uishell_sidebar_close_workspace(ws, node, close_kind); ui_ctx_menu_close(); }
      uishell_sidebar_discard_button(ws, node, close_kind);
      if(ordered) { uishell_sidebar_order_reset_button(state, loop); }
    }
    if(ui_right_clicked(sig)) { uishell_sidebar_menu_open_at_pointer(menu_key, sig.box); }
  }
  else if(close_kind == UIShell_SidebarCloseKind_RemovePin)
  {
    // A ghost: go to what it refers to, change its form, or remove it.
    CFG_Node *saved = uishell_sidebar_pin_by_ghost(cfg_node_from_id(ws->cfg_id), uishell_sidebar_string(node.entity_id));
    B32 expanded = saved != &cfg_nil_node && uishell_sidebar_pin_expanded(saved);
    UI_Key menu_key = ui_key_from_stringf(sig.box->key, "workspace_menu");
    UIShell_SidebarMenu(menu_key, uishell_sidebar_row_menu_width(node, close_kind))
    {
      if(node.activate != ANDAMENTO_NONE && ui_clicked(ui_button(str8_lit("Go to source"))))
      { action = node.activate; ui_ctx_menu_close(); }
      if(saved != &cfg_nil_node && ui_clicked(ui_button(expanded ? str8_lit("Show as row") : str8_lit("Show as card"))))
      { uishell_sidebar_ghost_set_expanded(saved, !expanded); ui_ctx_menu_close(); }
      if(ui_clicked(ui_button(str8_lit("Remove pin"))))
      { uishell_sidebar_close_workspace(ws, node, close_kind); ui_ctx_menu_close(); }
      if(ordered) { uishell_sidebar_order_reset_button(state, loop); }
    }
    if(ui_right_clicked(sig)) { uishell_sidebar_menu_open_at_pointer(menu_key, sig.box); }
  }
  else if(close_kind != UIShell_SidebarCloseKind_None)
  {
    // Rows show this on hover too; inline action chips have only this menu.
    UI_Key menu_key = ui_key_from_stringf(sig.box->key, "workspace_menu");
    UIShell_SidebarMenu(menu_key, uishell_sidebar_row_menu_width(node, close_kind))
    {
      if(ui_clicked(ui_button(uishell_sidebar_close_label(close_kind))))
      { uishell_sidebar_close_workspace(ws, node, close_kind); ui_ctx_menu_close(); }
      uishell_sidebar_discard_button(ws, node, close_kind);
      if(ordered) { uishell_sidebar_order_reset_button(state, loop); }
    }
    if(ui_right_clicked(sig)) { uishell_sidebar_menu_open_at_pointer(menu_key, sig.box); }
  }
  else if(!menu && ordered)
  {
    // `ordered` is only computed on a right click or while a menu is open.
    UI_Key menu_key = ui_key_from_stringf(sig.box->key, "order_menu");
    UIShell_SidebarMenu(menu_key, uishell_sidebar_row_menu_width(node, close_kind))
    { uishell_sidebar_order_reset_button(state, loop); }
    if(ui_right_clicked(sig)) { uishell_sidebar_menu_open_at_pointer(menu_key, sig.box); }
  }
  // A ghost's hover card is its subject's, as on its home row; Pin there
  // pins the subject, not the ghost.
  AndamentoNode hover_node = node;
  if(close_kind == UIShell_SidebarCloseKind_RemovePin)
  {
    CFG_Node *saved = uishell_sidebar_pin_by_ghost(cfg_node_from_id(ws->cfg_id), uishell_sidebar_string(node.entity_id));
    if(saved != &cfg_nil_node)
    {
      hover_node.entity_kind = uishell_sidebar_text(push_str8_copy(scratch.arena, cfg_node_child_from_string(saved, str8_lit("kind"))->first->string));
      hover_node.entity_id = uishell_sidebar_text(push_str8_copy(scratch.arena, cfg_node_child_from_string(saved, str8_lit("entity"))->first->string));
    }
  }
  uishell_sidebar_card_source(state, hover_node, sig, context, contains_current);
  scratch_end(scratch);
  return action;
}

// Current-workspace state is persistent navigation context, not keyboard focus.
// Desaturate the theme selection colour toward normal text, then blend it into
// the sidebar background. Exact actions use a stronger fill than containing rows.
internal Vec4F32
uishell_sidebar_selection_fill(B32 exact_action)
{
  Vec4F32 tint = mix_4f32(ui_color_from_name(str8_lit("selection")),
                          ui_color_from_name(str8_lit("text")), 0.65f);
  Vec4F32 color = mix_4f32(ui_color_from_name(str8_lit("background")), tint,
                           exact_action ? 0.30f : 0.14f);
  color.w = 1.f;
  return color;
}

// Ended subject text uses the same contrast in rows and inline actions.
internal Vec4F32
uishell_sidebar_ended_color(void)
{
  return mix_4f32(ui_color_from_name(str8_lit("background")), ui_color_from_name(str8_lit("text")), 0.55f);
}

// Workspace controls need more contrast than passive container separators.
internal Vec4F32
uishell_sidebar_action_border(void)
{
  Vec4F32 color = mix_4f32(ui_color_from_name(str8_lit("border")),
                           ui_color_from_name(str8_lit("text")), 0.20f);
  color.w = 1.f;
  return color;
}

// Inline actions and rows share terminal-state precedence. Project rows keep
// their own spacer at the call site rather than displaying a workspace marker.
internal String8
uishell_sidebar_status_mark(AndamentoNode node, String8 status)
{
  if(str8_match(status, str8_lit("ended"), 0)) { return str8_lit("×"); }
  if(node.state == ANDAMENTO_OPENING) { return str8_lit("…"); }
  if(str8_match(status, str8_lit("failed"), 0)) { return str8_lit("!"); }
  if(str8_match(status, str8_lit("waiting"), 0)) { return str8_lit("◷"); }
  return node.state == ANDAMENTO_LIVE ? str8_lit("•") : str8_zero();
}

// The native template reserves seven chip-presentation fields after its normal
// fields. A literal marker identifies that block. Producer labels and other
// field values are never interpreted as presentation directives.
// TODO: Replace this text convention with typed presentation metadata when
// the shared ABI exposes field identities, then retire the reserved prefixes.
internal U64
uishell_sidebar_chip_fields_start(UIShell_SidebarState *state, AndamentoNode node)
{
  String8 kind = uishell_sidebar_string(node.entity_kind);
  U64 start = str8_match(kind, str8_lit("project"), 0) ? 1 :
    str8_match(kind, str8_lit("role"), 0) ? 4 : 3;
  AndamentoField marker = {0};
  if(start < node.field_count && andamento_snapshot_field(state->snapshot, node.first_field+start, &marker) &&
     str8_match(uishell_sidebar_string(marker.text), str8_lit("chip-fields:1"), 0)) { return start; }
  return ANDAMENTO_NONE;
}

internal B32
uishell_sidebar_chip_field(UIShell_SidebarState *state, AndamentoNode node, U64 f, String8 text)
{
  U64 start = uishell_sidebar_chip_fields_start(state, node);
  if(start == ANDAMENTO_NONE || f < start || f >= start+7) { return 0; }
  String8 prefixes[] = {str8_lit("chip-fields:"), str8_lit("chip-medium:"), str8_lit("chip-short:"),
    str8_lit("chip-icon:"), str8_lit("chip-icon-override:"), str8_lit("chip-attention:"), str8_lit("chip-status:")};
  for(U64 i = 0; i < ArrayCount(prefixes); i++)
  { if(str8_match(str8_prefix(text, prefixes[i].size), prefixes[i], 0)) { return 1; } }
  return 0;
}

internal String8
uishell_sidebar_chip_fact(UIShell_SidebarState *state, AndamentoNode node, String8 prefix)
{
  U64 start = uishell_sidebar_chip_fields_start(state, node);
  for(U64 f = start; start != ANDAMENTO_NONE && f < Min(node.field_count, start+7); f++)
  {
    AndamentoField field = {0}; andamento_snapshot_field(state->snapshot, node.first_field+f, &field);
    String8 text = uishell_sidebar_string(field.text);
    if(str8_match(str8_prefix(text, prefix.size), prefix, 0)) { return str8_skip(text, prefix.size); }
  }
  return str8_zero();
}

internal String8
uishell_sidebar_chip_status(UIShell_SidebarState *state, AndamentoNode node)
{
  String8 status = uishell_sidebar_node_status(state, node);
  // Retained workspaces get their terminal status from the core. Producer
  // activity metadata must not override that authoritative end.
  if(str8_match(status, str8_lit("ended"), 0)) { return status; }
  String8 activity = uishell_sidebar_chip_fact(state, node, str8_lit("chip-status:"));
  if(activity.size) { return activity; }
  return status;
}

internal B32
uishell_sidebar_is_subject(AndamentoNode node)
{
  String8 kind = uishell_sidebar_string(node.entity_kind);
  return str8_match(kind, str8_lit("change_request"), 0) || str8_match(kind, str8_lit("issue"), 0);
}

internal B32
uishell_sidebar_chip_attention(UIShell_SidebarState *state, AndamentoNode node)
{
  String8 status = uishell_sidebar_chip_status(state, node);
  if(uishell_sidebar_is_subject(node))
  { return str8_match(status, str8_lit("ready_to_merge"), 0) || str8_match(status, str8_lit("ci_failing"), 0); }
  return node.selected || node.state == ANDAMENTO_OPENING ||
    (node.state == ANDAMENTO_LIVE && (str8_match(uishell_sidebar_chip_fact(state, node, str8_lit("chip-attention:")), str8_lit("true"), 0) ||
     str8_match(status, str8_lit("active"), 0) || str8_match(status, str8_lit("working"), 0) || str8_match(status, str8_lit("failed"), 0)));
}

internal Vec4F32
uishell_sidebar_subject_color(String8 status)
{
  Vec4F32 text = ui_color_from_name(str8_lit("text")), color = text;
  String8 tag = str8_zero();
  if(str8_match(status, str8_lit("ready_to_merge"), 0)) { tag = str8_lit("good"); }
  else if(str8_match(status, str8_lit("ci_failing"), 0) || str8_match(status, str8_lit("conflicting"), 0)) { tag = str8_lit("bad"); }
  else if(str8_match(status, str8_lit("awaiting_review_response"), 0) || str8_match(status, str8_lit("merged_not_landed"), 0)) { tag = str8_lit("neutral"); }
  if(tag.size) UI_TagF("%S", tag) { color = mix_4f32(ui_color_from_name(str8_lit("text")), text, 0.2f); }
  if(str8_match(status, str8_lit("draft"), 0) || str8_match(status, str8_lit("closed"), 0)) { color.w = 0.65f; }
  return color;
}

// An entity's one icon, for its row, its chip, its ghost and its drag lift:
// the producer's presentation.icon (or the template's override), else one by
// kind. A node without fields (a card's detail) reads them from the tree.
internal String8
uishell_sidebar_node_icon(UIShell_SidebarState *state, AndamentoNode node, B32 *icon_font)
{
  if(!node.field_count && state->snapshot)
  {
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode n = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &n);
      if(!n.is_section && n.field_count && uishell_sidebar_card_entity_match(uishell_sidebar_card_entity(n), uishell_sidebar_card_entity(node)))
      { node = n; break; }
    }
  }
  String8 icon = uishell_sidebar_chip_fact(state, node, str8_lit("chip-icon:"));
  if(!icon.size) { icon = uishell_sidebar_chip_fact(state, node, str8_lit("chip-icon-override:")); }
  *icon_font = 1;
  // Supported symbolic suggestions use the existing icon font. Other values
  // are literal glyphs rendered with the normal font/fallback chain.
  struct { char *name; RD_IconKind kind; } icons[] = {
    {"terminal", RD_IconKind_Machine}, {"overview", RD_IconKind_Thumbnails},
    {"role", RD_IconKind_Threads}, {"threads", RD_IconKind_Threads},
    {"gear", RD_IconKind_Gear}, {"code", RD_IconKind_Module},
    {"review", RD_IconKind_Glasses}, {"workspace", RD_IconKind_Window},
  };
  for(U64 i = 0; i < ArrayCount(icons); i++)
  { if(str8_match(icon, str8_cstring(icons[i].name), 0)) { return rd_icon_kind_text_table[icons[i].kind]; } }
  if(icon.size) { *icon_font = 0; return icon; }
  String8 kind = uishell_sidebar_string(node.entity_kind);
  RD_IconKind fallback = str8_match(kind, str8_lit("project"), 0) ? RD_IconKind_FolderClosedOutline :
    str8_match(kind, str8_lit("convoy"), 0) || str8_match(kind, str8_lit("role"), 0) ? RD_IconKind_Threads : RD_IconKind_Machine;
  return rd_icon_kind_text_table[fallback];
}

// A chip is its entity's icon, except a project's own chip, which opens
// its overview.
internal String8
uishell_sidebar_chip_icon(UIShell_SidebarState *state, AndamentoNode node, B32 *icon_font)
{
  String8 icon = uishell_sidebar_node_icon(state, node, icon_font);
  if(str8_match(uishell_sidebar_string(node.entity_kind), str8_lit("project"), 0) &&
     str8_match(icon, rd_icon_kind_text_table[RD_IconKind_FolderClosedOutline], 0))
  { icon = rd_icon_kind_text_table[RD_IconKind_Thumbnails]; }
  return icon;
}

internal String8
uishell_sidebar_chip_label(UIShell_SidebarState *state, AndamentoNode node)
{
  String8 label = uishell_sidebar_string(node.label);
  if(str8_match(uishell_sidebar_chip_status(state, node), str8_lit("ended"), 0))
  { label = push_str8f(ui_build_arena(), "%S ×", label); }
  return label;
}

internal F32
uishell_sidebar_chip_width(UIShell_SidebarState *state, AndamentoNode node)
{
  F32 em = ui_top_font_size();
  String8 label = uishell_sidebar_is_subject(node) ? uishell_sidebar_chip_label(state, node) : str8_zero();
  return uishell_sidebar_is_subject(node) ?
    fnt_dim_from_tag_size_string(ui_top_font(), em*0.9f, 0, 0, label).x+em*0.9f+4.f : em*2.f+4.f;
}

// Reveal keyboard targets in the chip viewport, including its overflow button.
// A stationary focus does not counteract pointer scrolling.
internal void
uishell_sidebar_reveal_chip(UIShell_SidebarState *state, UI_Box *box)
{
  if((box->flags & UI_BoxFlag_FocusHot) && !(box->flags & UI_BoxFlag_FocusHotDisabled))
  {
    UI_Box *viewport = box->parent;
    if(ui_box_is_nil(viewport) || !(viewport->flags & UI_BoxFlag_ViewScrollX)) { return; }
    F32 width = dim_2f32(viewport->rect).x;
    if(!ui_key_match(state->revealed_chip, box->key) || state->revealed_chip_width != width ||
       state->revealed_chip_build_index+1 != ui_state->build_index)
    {
      state->revealed_chip = box->key;
      state->revealed_chip_width = width;
      // Rects retain the preceding layout. Apply its offset before revealing
      // the focused chip, including focus reached through a clipped edge.
      F32 left = box->rect.x0-viewport->rect.x0+floor_f32(viewport->view_off.x);
      F32 right = box->rect.x1-viewport->rect.x0+floor_f32(viewport->view_off.x);
      viewport->view_off_target.x = Max(0.f, Min(left, Max(viewport->view_off_target.x, right-width)));
      rd_request_frame();
    }
    // A missing or unfocused frame invalidates the cached reveal, even when
    // a rebuilt section later reuses the same chip key and viewport width.
    state->revealed_chip_build_index = ui_state->build_index;
  }
}

internal size_t
uishell_sidebar_inline_action(UIShell_SidebarState *state, RD_WindowState *ws,
                              AndamentoNode node, U64 node_index, String8 context, F32 row_height, B32 menu)
{
  Temp scratch = scratch_begin(0, 0);
  F32 em = ui_top_font_size();
  B32 subject = uishell_sidebar_is_subject(node), icon_font = 0;
  String8 label = subject ? uishell_sidebar_chip_label(state, node) : uishell_sidebar_chip_icon(state, node, &icon_font);
  String8 status = uishell_sidebar_chip_status(state, node);
  Vec4F32 border = subject ? uishell_sidebar_subject_color(status) : uishell_sidebar_action_border();
  Vec4F32 fill = node.selected ? uishell_sidebar_selection_fill(1) :
    mix_4f32(ui_color_from_name(str8_lit("background")), border, node.state == ANDAMENTO_LIVE ? 0.23f : 0.06f);
  UI_Signal sig = {0};
  ui_spacer(ui_px(4.f, 1));
  UI_CornerRadius(subject ? em*0.7f : 3.f) UI_PrefHeight(ui_px(em*1.6f, 1))
  UI_FixedY(Max(0.f, (row_height-4.f-em*1.6f)*0.5f))
  UI_PrefWidth(menu ? ui_pct(1, 0) : ui_px(uishell_sidebar_chip_width(state, node)-4.f, 1))
  UI_BackgroundColor(fill) UI_TextColor(str8_match(status, str8_lit("ended"), 0) ?
    uishell_sidebar_ended_color() :
    subject ? border : ui_color_from_name(str8_lit("text")))
  UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center) UI_FontSize(em*0.9f)
  {
    ui_set_next_border_color(border);
    UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|
      UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects|UI_BoxFlag_DisableTextTrunc,
      "###action_%S", uishell_sidebar_string(node.key));
    UI_Parent(box) UI_PrefHeight(ui_pct(1, 1)) UI_PrefWidth(ui_pct(1, 0))
    {
      if(menu) { ui_label(uishell_sidebar_chip_label(state, node)); }
      else if(!subject && (str8_match(status, str8_lit("ended"), 0) || node.state == ANDAMENTO_OPENING))
      { ui_label(uishell_sidebar_status_mark(node, status)); }
      else if(icon_font) RD_Font(RD_FontSlot_Icons) { ui_label(label); }
      else { ui_label(label); }
    }
    if(!subject && node.state == ANDAMENTO_LIVE) { box->flags |= UI_BoxFlag_DrawSideLeft; }
    sig = ui_signal_from_box(box);
    if(!menu) { uishell_sidebar_reveal_chip(state, box); }
  }
  size_t action = uishell_sidebar_entry_signal(state, ws, node, node_index, sig, context, 0, menu, 0);
  scratch_end(scratch);
  return action;
}

// Template field classes supply a generic width ladder: optional fields go
// first, then lower-priority fields; required fields retain normal UI elision.
internal String8
uishell_sidebar_fields(Arena *arena, AndamentoSnapshot *snapshot, AndamentoNode node, F32 width)
{
  B32 *keep = push_array(arena, B32, node.field_count);
  AndamentoField *fields = push_array(arena, AndamentoField, node.field_count);
  F32 *widths = push_array(arena, F32, node.field_count);
  String8 separator = str8_lit(" · ");
  F32 separator_width = fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, separator).x;
  F32 measured = 0;
  U64 kept = 0;
  for(U64 i = 0; i < node.field_count; i++)
  {
    andamento_snapshot_field(snapshot, node.first_field+i, &fields[i]);
    String8 text = uishell_sidebar_string(fields[i].text);
    if(!text.size) { continue; }
    keep[i] = 1;
    widths[i] = fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, text).x;
    measured += widths[i] + (kept++ ? separator_width : 0);
  }
  while(measured > width)
  {
    U64 remove = ANDAMENTO_NONE;
    S64 lowest = max_S64;
    for(U64 i = 0; i < node.field_count; i++)
    {
      AndamentoField field = fields[i];
      if(keep[i] && field.class_ != ANDAMENTO_FIELD_REQUIRED)
      {
        S64 priority = field.class_ == ANDAMENTO_FIELD_OPTIONAL ? min_S64 : field.has_priority ? field.priority : 0;
        if(remove == ANDAMENTO_NONE || priority < lowest) { remove = i; lowest = priority; }
      }
    }
    if(remove == ANDAMENTO_NONE) { break; }
    keep[remove] = 0;
    measured -= widths[remove] + (--kept ? separator_width : 0);
  }
  String8List parts = {0};
  for(U64 i = 0; i < node.field_count; i++)
  { if(keep[i]) { str8_list_push(arena, &parts, uishell_sidebar_string(fields[i].text)); } }
  StringJoin join = {.sep = separator};
  return str8_list_join(arena, &parts, &join);
}

typedef enum UIShell_SidebarRenderMode
{
  UIShell_SidebarRenderMode_DiagnosticAggregate,
  UIShell_SidebarRenderMode_SectionPanel,
} UIShell_SidebarRenderMode;

typedef struct UIShell_SidebarRenderParams UIShell_SidebarRenderParams;
struct UIShell_SidebarRenderParams
{
  UIShell_SidebarRenderMode mode;
  String8 section_key;
};

internal F32 uishell_sidebar_footer_height(RD_WindowState *ws);
internal void uishell_sidebar_footer_ui(Rng2F32 rect, UIShell_ControlledSplit *split);

// Andamento's fallback section holds subjectless workspaces. Its header hosts
// workspace creation, so it stays visible even with no rows or controls.
internal B32
uishell_sidebar_section_hosts_chrome(String8 key)
{
  // The default local section (Workspaces), or the leftover section when
  // something is left over.
  return str8_match(key, uishell_sidebar_default_section_key, 0) || str8_match(key, str8_lit(".unplaced"), 0);
}

internal void
uishell_sidebar_row_begin(UIShell_SidebarState *state, UIShell_SidebarRow *r)
{
  Arena *arena = ui_build_arena();
  UI_PrefWidth(r->width > 0 ? ui_px(r->width, 1) : ui_pct(1, 0)) UI_PrefHeight(ui_px(r->height, 1)) UI_ChildLayoutAxis(Axis2_X)
  { r->slot = ui_build_box_from_stringf(0, "###row_slot_%S", r->key); }
  ui_push_parent(r->slot);
  ui_spacer(ui_px(4.f, 1));
  UI_Box *column;
  UI_ChildLayoutAxis(Axis2_Y) UI_PrefWidth(ui_pct(1, 0)) UI_PrefHeight(ui_pct(1, 1))
  { column = ui_build_box_from_stringf(0, "###row_column_%S", r->key); }
  ui_push_parent(column);
  ui_spacer(ui_px(2.f, 1));
  if(r->selected) { ui_set_next_background_color(uishell_sidebar_selection_fill(0)); }
  if(r->contains_current) { ui_set_next_border_color(uishell_sidebar_selection_fill(1)); }
  // A group header (r->project) takes clicks and drags across its width
  // without lighting up as a row does.
  UI_BoxFlags flags = (r->selected ? UI_BoxFlag_DrawBackground : 0)|(r->contains_current ? UI_BoxFlag_DrawBorder : 0)|
    (r->clickable ? UI_BoxFlag_Clickable|(r->project ? 0 : UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects) : 0);
  UI_PrefWidth(ui_pct(1, 0)) UI_PrefHeight(ui_px(r->height-4.f, 1)) UI_CornerRadius(3.f) UI_ChildLayoutAxis(Axis2_X)
  {
    // A group header's row is its entry (what its label button was before).
    r->row = ui_build_box_from_stringf(flags, r->project ? "###entry_%S" : "###sidebar_row_%S", r->key);
    if(r->project) { ui_box_equip_display_string(r->row, push_str8f(arena, "###entry_%S", r->key)); }
  }
  state->building_row = r->row;
  ui_push_parent(r->row);
  ui_push_pref_height(ui_pct(1, 1));
  if(r->project)
  {
    UI_BackgroundColor(uishell_sidebar_card_accent(r->node)) UI_PrefWidth(ui_px(3.f, 1))
    { ui_build_box_from_stringf(UI_BoxFlag_DrawBackground, "###accent_%S", r->key); }
  }
  if(r->project)
  {
    // A group's header (docs/design/sidebar-headers.md): no icon; the title
    // starts where its rows' icons do (r->indent is theirs), just after the
    // accent, and is semibold (or larger, for a Settings font); a collapsed
    // group's count follows it, then the collapse indicator, shown while the
    // header is hovered. The row itself takes clicks.
    ui_spacer(ui_px(uishell_sidebar_group_title_lead(r->indent), 1));
    UI_Font(uishell_sidebar_group_title_font()) UI_FontSize(uishell_sidebar_group_title_size())
    {
      if(r->renaming) UI_PrefWidth(ui_em(12.f, 1))
      { r->entry_sig = uishell_sidebar_local_rename_field(state, push_str8f(arena, "###rename_%S", r->key)); }
      else
      {
        // Its own width, up to the room its chips leave: the filler after it
        // absorbs the rest of the row, so the title never shrinks with it.
        F32 width = fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, r->text).x + 4.f + 2*ui_top_text_padding();
        if(r->title_max > 0) { width = Min(width, r->title_max); }
        UI_PrefWidth(ui_px(width, 1)) { ui_label(r->text); }
      }
    }
    if(!r->expanded && r->count) UI_PrefWidth(ui_text_dim(4.f, 1)) UI_TextColor(uishell_sidebar_ended_color())
    { ui_label(push_str8f(arena, "%I64u", r->count)); }
    B32 hovered = contains_2f32(r->row->rect, ui_mouse());
    if(r->disclosure) UI_Transparency(hovered || r->renaming ? 0.f : 1.f) UI_PrefWidth(ui_em(1.2f, 1))
    { r->toggle = uishell_sidebar_disclosure(r->expanded, push_str8f(arena, "###toggle_%S", r->key)); }
    ui_spacer(ui_pct(1, 0));
    return;
  }
  // A row's disclosure trails it (uishell_sidebar_row_end), so its icon
  // lines up with its group's title.
  ui_spacer(ui_em(r->indent, 1));
  // The entity's icon, as its chip shows it, never truncated to nothing.
  B32 icon_font = 1;
  String8 icon = uishell_sidebar_node_icon(state, r->node, &icon_font);
  UI_TagF("weak") UI_PrefWidth(ui_em(1.2f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center)
  UI_FontSize(ui_top_font_size()*0.9f) UI_Font(icon_font ? rd_font_from_slot(RD_FontSlot_Icons) : ui_top_font())
  {
    // With chips in the name column, the icon keeps the row's identity and
    // hover card reachable.
    if(r->icon_entry) { r->icon_sig = uishell_sidebar_button(push_str8f(arena, "%S###identity_%S", icon, r->key)); }
    else
    {
      UI_Box *box = ui_build_box_from_key(UI_BoxFlag_DrawText|UI_BoxFlag_DisableTextTrunc, ui_key_zero());
      ui_box_equip_display_string(box, icon);
    }
    if(r->icon_entry) { r->icon_sig.box->flags |= UI_BoxFlag_DisableTextTrunc|UI_BoxFlag_DisableTruncatedHover; }
  }
  UI_PrefWidth(ui_pct(1, 0))
  {
    if(!r->present || str8_match(r->status, str8_lit("ended"), 0)) { ui_set_next_text_color(uishell_sidebar_ended_color()); }
    if(r->renaming) { r->entry_sig = uishell_sidebar_local_rename_field(state, push_str8f(arena, "###rename_%S", r->key)); }
    else if(r->entry) { r->entry_sig = uishell_sidebar_button(push_str8f(arena, "%S###entry_%S", r->text, r->key)); }
    else { ui_label(r->text); }
  }
}

internal void
uishell_sidebar_row_end(UIShell_SidebarState *state, UIShell_SidebarRow *r)
{
  if(r->disclosure && !r->project)
  { r->toggle = uishell_sidebar_disclosure(r->expanded, push_str8f(ui_build_arena(), "###toggle_%S", r->key)); }
  if(r->reference) UI_PrefWidth(ui_em(1.2f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center) UI_TagF("weak")
  { ui_label(str8_lit("↗")); }
  // Reserve the same trailing slot at every level. Project-wide status
  // belongs here once supplied; workspace state stays on the overview action
  // rather than being duplicated in this slot.
  B32 change_request = str8_match(uishell_sidebar_string(r->node.entity_kind), str8_lit("change_request"), 0);
  UI_PrefWidth(ui_em(change_request && r->present ? 10.f : 1.2f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center)
  {
    String8 status = r->status;
    if(!r->present) { ui_label(str8_lit("?")); }
    else if(change_request)
    {
      String8 badge = str8_lit("~ pending");
      Vec4F32 color = ui_color_from_name(str8_lit("text"));
      if(str8_match(status, str8_lit("ready_to_merge"), 0)) { badge = str8_lit("+ ready to merge"); color = v4f32(0.4f, 0.85f, 0.55f, 1.f); }
      else if(str8_match(status, str8_lit("ci_failing"), 0)) { badge = str8_lit("! CI failing"); color = v4f32(1.f, 0.4f, 0.35f, 1.f); }
      else if(str8_match(status, str8_lit("conflicting"), 0)) { badge = str8_lit("! conflicting"); color = v4f32(1.f, 0.7f, 0.3f, 1.f); }
      else if(str8_match(status, str8_lit("awaiting_review_response"), 0)) { badge = str8_lit("~ review response"); color = v4f32(0.9f, 0.75f, 0.4f, 1.f); }
      else if(str8_match(status, str8_lit("draft"), 0)) { badge = str8_lit("o draft"); color.w = 0.65f; }
      else if(str8_match(status, str8_lit("merged_not_landed"), 0)) { badge = str8_lit("+ merged"); color = v4f32(0.65f, 0.6f, 0.95f, 1.f); }
      else if(str8_match(status, str8_lit("closed"), 0)) { badge = str8_lit("x closed"); color.w = 0.65f; }
      UI_TextColor(color) UI_TextAlignment(UI_TextAlign_Left) { ui_label(badge); }
    }
    else if(r->project) { ui_spacer(ui_em(1.2f, 1)); }
    else { ui_label(uishell_sidebar_status_mark(r->node, status)); }
  }
  ui_pop_pref_height();
  ui_pop_parent(); // row
  state->building_row = 0;
  // After its children, so a disclosure gets its own clicks.
  if(r->clickable) { r->row_sig = ui_signal_from_box(r->row); }
  ui_spacer(ui_px(2.f, 1));
  ui_pop_parent(); // column
}

// The local (subjectless) workspace a row drag carries, if any: it may move
// between groups, unlike data rows (drag-model.md, drop table).
internal CFG_Node *
uishell_sidebar_drag_local(UIShell_SidebarState *state)
{
  CFG_Node *workspace = state->row_drag_key.size && state->row_drag_workspace ? cfg_node_from_id(state->row_drag_workspace) : &cfg_nil_node;
  if(workspace == &cfg_nil_node || uishell_workspace_cfg_has_subject(workspace)) { return &cfg_nil_node; }
  return workspace;
}

// A local workspace dragged over a group it doesn't live in claims a move
// there: a project group (`project` is its id) or the Workspaces group
// (empty). The group lights up; uishell_sidebar_drag_finish applies it.
internal void
uishell_sidebar_home_claim(UIShell_SidebarState *state, Rng2F32 rect, String8 project, B32 local)
{
  CFG_Node *workspace = uishell_sidebar_drag_local(state);
  if(workspace == &cfg_nil_node || !contains_2f32(rect, ui_mouse()) ||
     str8_match(uishell_sidebar_local_home(workspace), project, 0) ||
     uishell_sidebar_group_claimed(state) ||
     !uishell_sidebar_drop_claimable(cfg_node_from_id(uishell_regs()->panel))) { return; }
  if(!state->home_arena) { state->home_arena = arena_alloc(); }
  arena_clear(state->home_arena);
  state->home_project = push_str8_copy(state->home_arena, project);
  state->home_build = ui_state->build_index;
  // The drop-site colours are barely visible; the target takes the accent, as
  // insertion lines do (uishell_sidebar_drop_line).
  Vec4F32 border = rd_accent_color();
  Vec4F32 fill = border;
  fill.w = 0.12f;
  UI_Parent(ui_state->root) UI_TagF("drop_site") UI_Rect(rect) UI_CornerRadius(5.f)
  DeferLoop(ui_push_border_color(border), ui_pop_border_color()) UI_BackgroundColor(fill)
  {
    ui_build_box_from_key(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground|UI_BoxFlag_Floating,
      ui_key_from_stringf(ui_key_zero(), local ? "sidebar_home_workspaces" : "sidebar_home_project"));
  }
}

// An insertion line at `line` (screen space, from last frame's layout) drawn
// inside a section's body, so it paints above the section rather than under
// the panels, as a line on the window root would. A brighter segment sweeps
// along it so it reads against the lifted row.
internal void
uishell_sidebar_drop_line(UI_Box *body, Rng2F32 line, UI_Key key, B32 refused)
{
  Vec2F32 at = add_2f32(sub_2f32(line.p0, body->rect.p0), body->view_off);
  F32 width = dim_2f32(line).x, height = dim_2f32(line).y;
  F32 sweep = Min(width*0.3f, 48.f);
  F32 phase = (F32)((now_time_us()/1000) % 1400)/1400.f;
  F32 sweep_x = at.x + (width+sweep)*phase - sweep;
  F32 x0 = Max(at.x, sweep_x), x1 = Min(at.x+width, sweep_x+sweep);
  // The drop-site fill is nearly transparent, made for whole areas; a line
  // needs the accent at full strength.
  Vec4F32 color = refused ? rd_drag_refusal_color() : rd_accent_color();
  Vec4F32 bright = mix_4f32(color, ui_color_from_name(str8_lit("text")), 0.6f);
  bright.w = 1.f;
  UI_Parent(body) UI_TagF("drop_site") UI_CornerRadius(height*0.5f) UI_PrefHeight(ui_px(height, 1))
  {
    UI_FixedX(at.x) UI_FixedY(at.y) UI_PrefWidth(ui_px(width, 1)) UI_BackgroundColor(color)
    { ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Floating, key); }
    if(x1 > x0) UI_FixedX(x0) UI_FixedY(at.y) UI_PrefWidth(ui_px(x1-x0, 1)) UI_BackgroundColor(bright)
    { ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Floating, ui_key_from_stringf(key, "sweep")); }
  }
  rd_request_frame();
}

// A drop the pointer is over but that can't happen (#282): the reason goes to
// the dragged item's badge, and a line in the refusal colour marks where it
// would have gone, under the pointer. Docking sites take precedence.
internal void
uishell_sidebar_refuse(UI_Box *body, Rng2F32 area, String8 reason)
{
  Vec2F32 mouse = ui_mouse();
  if(!contains_2f32(area, mouse) || !uishell_sidebar_drop_claimable(cfg_node_from_id(uishell_regs()->panel))) { return; }
  rd_drag_refuse(reason);
  uishell_sidebar_drop_line(body, r2f32p(area.x0+6.f, mouse.y-1.5f, area.x1-6.f, mouse.y+1.5f),
    ui_key_from_stringf(ui_key_zero(), "sidebar_refused_line"), 1);
}

// A sidebar drag over a local group claims the gap between its items under
// the pointer, by their midpoints, and shows a line there; the drag's own
// sibling run reorders instead (uishell_sidebar_row_drop).
internal void
uishell_sidebar_group_claim(UIShell_SidebarState *state, UI_Box *body, AndamentoNode *nodes, U64 group,
                            UIShell_RowDragSibling *items, U64 item_count, Rng2F32 area, F32 row_height)
{
  Vec2F32 mouse = ui_mouse();
  if(dim_2f32(area).y <= 0 || !contains_2f32(area, mouse) ||
     !uishell_sidebar_drop_claimable(cfg_node_from_id(uishell_regs()->panel))) { return; }
  U64 index = 0, first = item_count, last = item_count;
  String8 loop = str8_zero();
  for(U64 k = 0; k < item_count; k++)
  {
    if(nodes[items[k].index].parent != group) { continue; }
    if(first == item_count) { first = k; loop = uishell_sidebar_loop_key(state->snapshot, items[k].index); }
    last = k;
    if(mouse.y > center_2f32(items[k].row).y) { index++; }
  }
  // A row's own run reorders instead; so does a group header over its own
  // section's groups.
  if(state->row_drag_key.size && loop.size && str8_match(loop, state->row_drag_loop, 0)) { return; }
  if(state->row_drag_key.size && str8_match(uishell_sidebar_loop_key(state->snapshot, group), state->row_drag_loop, 0)) { return; }
  // Workspaces holds only local workspaces: no ghosts (drag-model.md, drop table).
  CFG_Node *target_cfg = uishell_sidebar_local_group(cfg_node_from_id(uishell_regs()->window), uishell_sidebar_string(nodes[group].entity_id));
  if(uishell_sidebar_local_is_default(target_cfg) && (state->drag_card || uishell_sidebar_drag_local(state) == &cfg_nil_node))
  { uishell_sidebar_refuse(body, area, str8_lit("only workspaces go in Workspaces")); return; }
  CFG_Node *section_group = uishell_sidebar_section_drag_group(cfg_node_from_id(uishell_regs()->window));
  if(section_group != &cfg_nil_node &&
     str8_match(uishell_sidebar_local_field(section_group, str8_lit("id")), uishell_sidebar_string(nodes[group].entity_id), 0)) { return; }
  F32 y = area.y0+row_height*0.5f;
  if(first < item_count)
  {
    U64 at = 0;
    y = items[first].row.y0;
    for(U64 k = first; k <= last; k++)
    {
      if(nodes[items[k].index].parent != group) { continue; }
      if(at++ < index) { y = items[k].row.y1; }
    }
  }
  if(!state->group_claim_arena) { state->group_claim_arena = arena_alloc(); }
  arena_clear(state->group_claim_arena);
  state->group_claim_id = push_str8_copy(state->group_claim_arena, uishell_sidebar_string(nodes[group].entity_id));
  state->group_claim_loop = push_str8_copy(state->group_claim_arena, loop);
  state->group_claim_index = index;
  state->group_claim_build = ui_state->build_index;
  state->group_claim_rect = area;
  uishell_sidebar_drop_line(body, r2f32p(area.x0+6.f, y-1.5f, area.x1-6.f, y+1.5f),
    ui_key_from_stringf(ui_key_zero(), "group_drop_line_%S", state->group_claim_id), 0);
}

// Why a section View being dragged can't join a list, if it is one that
// can't (#282): a section of several tabs dragged by its grip, a section of
// data (Projects, Attention…), or the one holding Workspaces.
internal String8
uishell_sidebar_section_drag_refusal(CFG_Node *window)
{
  if(!rd_drag_is_active() || rd_state->drag_drop_regs_slot != UIShell_ContextRegSlot_View ||
     rd_state->drag_drop_regs->window != window->id || uishell_sidebar_section_drag_section(window) != &cfg_nil_node) { return str8_zero(); }
  CFG_Node *view = cfg_node_from_id(rd_state->drag_drop_regs->view);
  if(!str8_match(view->string, str8_lit("sidebar_section"), 0)) { return str8_zero(); }
  if(rd_state->drag_drop_commit == uishell_sidebar_section_drop_commit) { return str8_lit("a section of several tabs can't join a list"); }
  if(uishell_sidebar_local_view_group(window, view) == &cfg_nil_node) { return str8_lit("this section's rows stay in it"); }
  return str8_lit("Workspaces stays in its own section");
}

// Whether a sidebar drag carries local groups (#282): a local section's View
// (all its groups, `*section`), or a local group's header (`*group`).
internal B32
uishell_sidebar_drag_carries_groups(UIShell_SidebarState *state, CFG_Node *window, CFG_Node **section, CFG_Node **group)
{
  *section = uishell_sidebar_section_drag_section(window);
  *group = &cfg_nil_node;
  if(*section == &cfg_nil_node && state->row_drag_key.size &&
     str8_match(uishell_sidebar_string(state->row_drag_entity.kind), str8_lit(".group"), 0))
  {
    *group = uishell_sidebar_local_group(window, uishell_sidebar_string(state->row_drag_entity.id));
    if(uishell_sidebar_local_is_default(*group)) { *group = &cfg_nil_node; }
  }
  return *section != &cfg_nil_node || *group != &cfg_nil_node;
}

// A drag carrying groups, over another local section, claims the gap between
// its groups under the pointer, by their midpoints, and shows a line there
// (#282). A section showing one group (no group headers) has a gap above and
// below it: dropped there, it becomes a list of groups. Groups [first, end)
// of `nodes` whose parent is `section` are that section's.
internal void
uishell_sidebar_groups_claim(UIShell_SidebarState *state, UI_Box *body, CFG_Node *window, AndamentoNode *nodes,
                             U64 section, U64 first, U64 end, B32 *passed, Rng2F32 *group_rects,
                             UIShell_RowDragSibling *items, U64 item_count, Rng2F32 viewport)
{
  CFG_Node *dragged_section, *dragged_group;
  Vec2F32 mouse = ui_mouse();
  if(!contains_2f32(viewport, mouse) || !uishell_sidebar_drop_claimable(cfg_node_from_id(uishell_regs()->panel)) ||
     !uishell_sidebar_drag_carries_groups(state, window, &dragged_section, &dragged_group)) { return; }
  Temp scratch = scratch_begin(0, 0);
  CFG_Node **groups = push_array(scratch.arena, CFG_Node *, end-first);
  Rng2F32 *rects = push_array(scratch.arena, Rng2F32, end-first);
  U64 count = 0;
  for(U64 g = first; g < end; g++)
  {
    if(nodes[g].parent != section || !str8_match(uishell_sidebar_string(nodes[g].entity_kind), str8_lit(".group"), 0)) { continue; }
    CFG_Node *cfg = uishell_sidebar_local_group(window, uishell_sidebar_string(nodes[g].entity_id));
    if(cfg == &cfg_nil_node) { continue; }
    groups[count] = cfg;
    rects[count] = group_rects[g];
    // A section's only group has no card: it is the section's rows, and
    // splits above or below at their middle, not the viewport's.
    if(passed[g])
    {
      rects[count] = r2f32p(viewport.x0, viewport.y0, viewport.x1, viewport.y0);
      for(U64 k = 0; k < item_count; k++)
      {
        if(nodes[items[k].index].parent != g) { continue; }
        rects[count].y0 = rects[count].y1 > rects[count].y0 ? Min(rects[count].y0, items[k].row.y0) : items[k].row.y0;
        rects[count].y1 = Max(rects[count].y1, items[k].row.y1);
      }
      if(rects[count].y1 <= rects[count].y0) { rects[count] = viewport; }
    }
    count++;
  }
  // Not a local section; or the dragged groups' own, where a group header
  // reorders instead (uishell_sidebar_row_drop) and a section is already there.
  CFG_Node *target = count ? groups[0]->parent : &cfg_nil_node;
  if(target == &cfg_nil_node)
  { uishell_sidebar_refuse(body, viewport, str8_lit("groups only go into your own sections")); scratch_end(scratch); return; }
  if(target == dragged_section || (dragged_group != &cfg_nil_node && target == dragged_group->parent))
  { scratch_end(scratch); return; }
  U64 index = 0;
  // Over a group's middle counts as after it, as a drop onto it reads.
  for(U64 k = 0; k < count; k++) { if(mouse.y >= center_2f32(rects[k]).y) { index = k+1; } }
  F32 y = index == 0 ? rects[0].y0 : index == count ? rects[count-1].y1 : (rects[index-1].y1+rects[index].y0)*0.5f;
  y = Clamp(viewport.y0+2.f, y, viewport.y1-2.f);
  if(!state->groups_claim_arena) { state->groups_claim_arena = arena_alloc(); }
  arena_clear(state->groups_claim_arena);
  state->groups_claim_section = push_str8_copy(state->groups_claim_arena, uishell_sidebar_local_field(target, str8_lit("id")));
  state->groups_claim_after = index ? push_str8_copy(state->groups_claim_arena, uishell_sidebar_local_field(groups[index-1], str8_lit("id"))) : str8_zero();
  state->groups_claim_build = ui_state->build_index;
  state->groups_claim_rect = viewport;
  uishell_sidebar_drop_line(body, r2f32p(viewport.x0+6.f, y-1.5f, viewport.x1-6.f, y+1.5f),
    ui_key_from_stringf(ui_key_zero(), "groups_drop_line_%S", state->groups_claim_section), 0);
  scratch_end(scratch);
}

// The pointer picks the gap between siblings by their rows' midpoints. Within
// the run's extent, a gap that moves the row shows an insertion line and
// claims the reorder for uishell_sidebar_drag_finish.
internal void
uishell_sidebar_row_drop(UIShell_SidebarState *state, UI_Box *body, AndamentoNode *nodes,
                         UIShell_RowDragSibling *siblings, U64 count, F32 row_height)
{
  Vec2F32 mouse = ui_mouse();
  U64 source = count, target = 0;
  for(U64 k = 0; k < count; k++)
  {
    if(str8_match(uishell_sidebar_string(nodes[siblings[k].index].key), state->row_drag_key, 0)) { source = k; }
    if(mouse.y > center_2f32(siblings[k].row).y) { target = k+1; }
  }
  Rng2F32 span = r2f32p(siblings[0].extent.x0, siblings[0].extent.y0-row_height*0.5f,
                        siblings[0].extent.x1, siblings[count-1].extent.y1+row_height*0.5f);
  B32 moves = source < count &&
    uishell_sidebar_drop_claimable(cfg_node_from_id(uishell_regs()->panel)) &&
    contains_2f32(span, mouse) && target != source && target != source+1;
  state->reorder_build = 0;
  if(!moves) { return; }
  AndamentoNode anchor = nodes[siblings[target < count ? target : count-1].index];
  if(!state->reorder_arena) { state->reorder_arena = arena_alloc(); }
  arena_clear(state->reorder_arena);
  state->reorder_anchor = uishell_sidebar_card_entity_copy(state->reorder_arena, (AndamentoEntity){anchor.entity_kind, anchor.entity_id});
  state->reorder_after = target == count;
  state->reorder_build = ui_state->build_index;
  F32 y = target == 0 ? siblings[0].extent.y0-1.f :
    target == count ? siblings[count-1].extent.y1+1.f :
    (siblings[target-1].extent.y1+siblings[target].extent.y0)*0.5f;
  uishell_sidebar_drop_line(body, r2f32p(span.x0+4.f, y-1.5f, span.x1-4.f, y+1.5f),
    ui_key_from_stringf(ui_key_zero(), "sidebar_row_drop_line"), 0);
}

// The dragged row lifts: a translucent copy of its row follows the pointer,
// drawn as its home row is (row_begin), at the row's size even when the drag
// started on a chip. It builds with the cards, ahead of the Views, so it
// paints above them.
internal void
uishell_sidebar_row_lift(UIShell_SidebarState *state)
{
  if(!state->row_drag_key.size) { return; }
  // The copy is the row's slot: the row plus the slot's insets.
  Rng2F32 lift = state->row_drag_rect;
  F32 height = floor_f32(ui_top_font_size()*2.2f);
  lift.x0 -= 4.f; lift.x1 += 4.f;
  lift.y0 = center_2f32(lift).y-height*0.5f; lift.y1 = lift.y0+height;
  lift = r2f32(add_2f32(lift.p0, ui_drag_delta()), add_2f32(lift.p1, ui_drag_delta()));
  AndamentoNode node = {0};
  B32 present = uishell_sidebar_card_find(state, state->row_drag_entity, &node) != ANDAMENTO_NONE;
  if(!present) { node.entity_kind = state->row_drag_entity.kind; }
  UI_Box *box;
  UI_Parent(ui_state->root) UI_Rect(lift) UI_CornerRadius(3.f) UI_Transparency(0.25f)
  UI_BackgroundColor(uishell_sidebar_selection_fill(0)) UI_ChildLayoutAxis(Axis2_Y)
  {
    box = ui_build_box_from_key(UI_BoxFlag_Floating|UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawDropShadow,
      ui_key_from_stringf(ui_key_zero(), "sidebar_row_lift"));
  }
  // The row lays out inside the copy; it must not inherit the copy's rect.
  UI_Parent(box)
  {
    UIShell_SidebarRow r = {.node = node, .present = present, .key = str8_lit("lift"), .text = state->row_drag_label,
      .status = present ? uishell_sidebar_chip_status(state, node) : str8_zero(), .height = height,
      .width = dim_2f32(lift).x, .indent = 0.3f};
    uishell_sidebar_row_begin(state, &r);
    uishell_sidebar_row_end(state, &r);
    ui_pop_parent();
  }
  rd_drag_refusal_badge_ui(lift);
}

// The saved ghost a tree row presents (a `.ref`), or nil.
internal CFG_Node *
uishell_sidebar_tree_ghost(CFG_Node *window, AndamentoNode node)
{
  if(!str8_match(uishell_sidebar_string(node.entity_kind), str8_lit(".ref"), 0)) { return &cfg_nil_node; }
  return uishell_sidebar_pin_by_ghost(window, uishell_sidebar_string(node.entity_id));
}

// The Workspaces group's first row creates a workspace, which appears in it.
// It shares the row slot's insets, so it lines up with the rows below.
internal void
uishell_sidebar_new_workspace_entry(UIShell_ControlledSplit *split, F32 row_height, F32 side_margin)
{
  UI_Signal sig = {0};
  UI_Box *slot;
  UI_ChildLayoutAxis(Axis2_X) { slot = ui_build_box_from_stringf(0, "###new_workspace_slot"); }
  UI_Parent(slot)
  {
    ui_spacer(ui_px(4.f, 1));
    UI_Box *column;
    UI_ChildLayoutAxis(Axis2_Y) UI_PrefHeight(ui_pct(1, 1)) UI_PrefWidth(ui_pct(1, 0))
    { column = ui_build_box_from_stringf(0, "###new_workspace_column"); }
    UI_Parent(column)
    {
      ui_spacer(ui_px(2.f, 1));
      UI_Box *row;
      UI_PrefHeight(ui_px(row_height-4.f, 1)) UI_CornerRadius(3.f) UI_ChildLayoutAxis(Axis2_X)
      {
        row = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects,
          "###new_workspace");
      }
      sig = ui_signal_from_box(row);
      UI_Parent(row) UI_PrefHeight(ui_pct(1, 1)) UI_TagF("weak")
      {
        ui_spacer(ui_em(0.7f, 1));
        UI_PrefWidth(ui_em(1.2f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center) { ui_label(str8_lit("+")); }
        UI_PrefWidth(ui_pct(1, 0)) { ui_label(str8_lit("New workspace")); }
      }
    }
    ui_spacer(ui_px(4.f+side_margin, 1));
  }
  if(ui_hovering(sig)) UI_Tooltip
  {
    ui_state->tooltip_anchor_key = sig.box->key;
    ui_label(str8_lit("New workspace"));
    UI_TagF("weak") { ui_label(str8_lit("Opens an empty workspace in this group")); }
  }
  if(ui_clicked(sig)) { uishell_cmd("new_workspace", .window = split->owner_cfg->id); }
}

// A clicked row, header, toggle or control: dispatch it against the snapshot
// it was shown from, and run what it asks of the host.
internal void
uishell_sidebar_perform(UIShell_SidebarState *state, UIShell_ControlledSplit *split, size_t action)
{
  char *error = 0;
  B32 ok = uishell_sidebar_dispatch(state, action, &error);
  if(uishell_sidebar_result(state, ok, error)) { uishell_sidebar_effects(state, split); }
  uishell_sidebar_refresh(state);
  rd_request_frame();
}

internal void
uishell_sidebar_render(Rng2F32 rect, UIShell_ControlledSplit *split, UIShell_SidebarRenderParams params)
{
  B32 section_panel = params.mode == UIShell_SidebarRenderMode_SectionPanel;
  String8 only_section = params.section_key;
  if(!section_panel) { MemoryZeroStruct(&uishell_sidebar_subject_geometry); }
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  B32 prepare = state->render_ui != ui_state || state->render_build_index != ui_state->build_index;
  if(prepare)
  {
    state->render_ui = ui_state;
    state->render_build_index = ui_state->build_index;
    // Esc ends a row drag unmoved. A docking site built before this took
    // the release as its drop, also ending the drag: that one finishes.
    if(state->row_drag_key.size && !rd_drag_is_active() && !state->row_drag_released && !state->drop_panel) { uishell_sidebar_drag_clear(state); }
    // A footer being named that stopped being shown (its group collapsed or
    // went, its section closed) is done naming; it opens fresh next time.
    if(state->make_key && state->make_build+1 < ui_state->build_index) { state->make_key = 0; }
    if(state->core && (state->managed_dirty || state->managed_cfg_generation != cfg_change_gen()))
    {
      for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
      { uishell_sidebar_reconcile_workspace(state, w->mount.owner_cfg); }
      state->managed_cfg_generation = cfg_change_gen();
      state->managed_dirty = 0;
    }
    if(state->reveal_workspace_id)
    {
      uishell_sidebar_observe(state, split);
      uishell_sidebar_expand_reveal(state);
    }
  }
  if(!section_panel) { state->rect = rect; }
  // A header/row click is dispatched by its own section. Card intent is
  // consumed after all Views finish rendering, including later pinned areas.
  size_t action = ANDAMENTO_NONE;
  Temp scratch = scratch_begin(0, 0);
  F32 em = ui_top_font_size(), row_height = floor_f32(em*2.2f);
  F32 minimum_name = fnt_dim_from_tag_size_string(ui_top_font(), em, 0, 0, str8_lit("abcdefghij…")).x+em;
  F32 project_gap = 6.f, project_padding = 4.f;
  // Rows and project cards leave a right margin for the close control.
  F32 side_margin = floor_f32(em*1.5f);
  // Group cards are inset from the left as from the right (where the margin
  // controls' lane is), except in a panel too narrow to spare it.
  F32 body_top_padding = 2.f; // Room for the first container's outward border stroke.
  Vec2F32 dim = dim_2f32(rect);
  UI_Box *root;
  UI_Focus(section_panel ? UI_FocusKind_Null : UI_FocusKind_On) UI_Rect(section_panel ? r2f32p(0, 0, dim.x, dim.y) : rect)
  {
    root = ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Clip|UI_BoxFlag_DefaultFocusNav,
      section_panel ? ui_key_from_stringf(ui_key_zero(), "andamento_section_%S", only_section) :
      ui_key_from_string(ui_active_seed_key(), str8_lit("###andamento_sidebar")));
  }
  if(ui_key_match(ui_state->default_nav_root_key, root->key) &&
     !ui_key_match(root->default_nav_focus_hot_key, state->revealed_chip))
  { state->revealed_chip = ui_key_zero(); }
  U64 analysis_start = uishell_sidebar_benchmark_active ? now_time_us() : 0;
  U64 count = state->snapshot ? andamento_snapshot_node_count(state->snapshot) : 0;
  AndamentoNode *nodes = push_array(scratch.arena, AndamentoNode, count);
  B32 *hidden = push_array(scratch.arena, B32, count);
  U64 *depth = push_array(scratch.arena, U64, count);
  U64 *sections = push_array(scratch.arena, U64, count);
  U64 *inline_first = push_array(scratch.arena, U64, count);
  U64 *inline_last = push_array(scratch.arena, U64, count);
  U64 *inline_next = push_array(scratch.arena, U64, count);
  U64 *inline_count = push_array(scratch.arena, U64, count);
  B32 *inlined = push_array(scratch.arena, B32, count);
  B32 *row_children = push_array(scratch.arena, B32, count);
  U64 *project_owner = push_array(scratch.arena, U64, count);
  F32 *project_open = push_array(scratch.arena, F32, count);
  F32 *project_child_heights = push_array(scratch.arena, F32, count);
  // A section's only group passes through: no row, and its items are the
  // section's rows (uishell_sidebar_node_at).
  B32 *passed = push_array(scratch.arena, B32, count);
  // A drag that starts while building rows collects siblings from the next frame.
  B32 row_dragging = state->row_drag_key.size != 0;
  UIShell_RowDragSibling *drag_siblings = push_array(scratch.arena, UIShell_RowDragSibling, row_dragging ? count : 0);
  // Local groups' items as built this frame (last frame's rects), and their
  // cards' rects, for the insertion point a sidebar drag claims.
  // A one-group section dragged by its title is its group's drag too.
  CFG_Node *carried_section, *carried_group;
  B32 groups_dragging = uishell_sidebar_drag_carries_groups(state, split->owner_cfg, &carried_section, &carried_group);
  String8 section_refusal = uishell_sidebar_section_drag_refusal(split->owner_cfg);
  // The dragged row, so a project it isn't in can refuse it.
  U64 drag_row_index = ANDAMENTO_NONE;
  for(U64 i = 0; row_dragging && i < count && drag_row_index == ANDAMENTO_NONE; i++)
  { if(str8_match(uishell_sidebar_string(nodes[i].key), state->row_drag_key, 0)) { drag_row_index = i; } }
  B32 sidebar_dragging = row_dragging || state->drag_card || groups_dragging;
  UIShell_RowDragSibling *group_items = push_array(scratch.arena, UIShell_RowDragSibling, sidebar_dragging ? count : 0);
  Rng2F32 *group_rects = push_array(scratch.arena, Rng2F32, sidebar_dragging ? count : 0);
  U64 group_item_count = 0;
  U64 drag_sibling_count = 0, drag_depth = 0;
  UI_Box *drag_body = 0; // the section body holding the dragged row's run
  B32 drag_subtree = 0;
  for(U64 i = 0; i < count; i++) { inline_first[i] = inline_last[i] = inline_next[i] = ANDAMENTO_NONE; }
  U64 section_count = 0;
  for(U64 i = 0; i < count; i++)
  {
    UIShell_SidebarRole role = uishell_sidebar_node_at(state, i, &nodes[i]);
    passed[i] = role == UIShell_SidebarRole_PassThrough;
    // The leftover section shows only while something is left over.
    B32 empty_leftover = uishell_sidebar_leftover_hidden(state, i, nodes[i]);
    if(nodes[i].is_section && role != UIShell_SidebarRole_Container && !empty_leftover) { sections[section_count++] = i; }
    project_owner[i] = ANDAMENTO_NONE;
    if(nodes[i].parent != ANDAMENTO_NONE)
    {
      U64 parent = nodes[i].parent;
      project_owner[i] = project_owner[parent];
      // A project's children remain available while its viewport closes.
      // Nested collapse state still determines their full, unanimated layout.
      hidden[i] = hidden[parent] || (!nodes[parent].is_section &&
        project_owner[parent] != parent && nodes[parent].collapsed);
      depth[i] = depth[parent] + !(nodes[parent].is_section || passed[parent]);
    }
    // Project groups, and the groups of a section holding several, are cards.
    if(!nodes[i].is_section && !passed[i] && depth[i] == 0 &&
       (str8_match(uishell_sidebar_string(nodes[i].entity_kind), str8_lit("project"), 0) ||
        str8_match(uishell_sidebar_string(nodes[i].entity_kind), str8_lit(".group"), 0)))
    {
      project_owner[i] = i;
      F32 target = nodes[i].collapsed ? 0.f : 1.f;
      project_open[i] = ui_anim(ui_key_from_stringf(root->key, "project_open_%S", uishell_sidebar_string(nodes[i].key)),
        target, .initial = target, .rate = rd_state->menu_animation_rate, .epsilon = 0.0001f,
        .reset = state->reveal_workspace_id != 0);
    }
  }
  for(U64 i = 0; i < count; i++)
  {
    U64 parent = nodes[i].parent;
    B32 leaf = i+1 == count || nodes[i+1].parent != i;
    // Unsupported nested/control-bearing inline content stays in the tree,
    // ensuring it remains reachable rather than silently losing descendants.
    if(parent != ANDAMENTO_NONE && !nodes[parent].is_section && leaf &&
       !nodes[i].control_count && str8_match(uishell_sidebar_string(nodes[i].layout), str8_lit("inline"), 0))
    {
      inlined[i] = 1;
      if(inline_last[parent] != ANDAMENTO_NONE) { inline_next[inline_last[parent]] = i; }
      else { inline_first[parent] = i; }
      inline_last[parent] = i;
      inline_count[parent]++;
    }
    else if(parent != ANDAMENTO_NONE) { row_children[parent] = 1; }
  }
  // One navigation handle per workspace: prefer the deepest selected subject
  // appearance, with stable section order resolving equal-depth duplicates.
  // nodes follows snapshot section order, then each section's traversal order.
  U64 selected_entry = ANDAMENTO_NONE;
  for(U64 i = 0; i < count; i++)
  {
    if(nodes[i].selected && (selected_entry == ANDAMENTO_NONE || depth[i] > depth[selected_entry]))
    { selected_entry = i; }
  }
  for(U64 i = 0; i < count; i++) { nodes[i].selected = i == selected_entry; }
  // Propagate selection only for collapsed ancestors. Their
  // own action and selected state still belong to their exact workspace binding.
  B32 *contains_selected = push_array(scratch.arena, B32, count);
  for(U64 i = count; i > 0; i--)
  {
    U64 idx = i-1;
    contains_selected[idx] |= nodes[idx].selected;
    if(nodes[idx].parent != ANDAMENTO_NONE)
    { contains_selected[nodes[idx].parent] |= contains_selected[idx]; }
  }
  U64 reveal = state->reveal_workspace_id ? uishell_sidebar_reveal_target(state, state->reveal_workspace_id) : ANDAMENTO_NONE;
  if(reveal != ANDAMENTO_NONE && inlined[reveal]) { reveal = nodes[reveal].parent; }
  U64 reveal_section = reveal;
  while(reveal_section != ANDAMENTO_NONE && !nodes[reveal_section].is_section)
  { reveal_section = nodes[reveal_section].parent; }
  UIShell_SidebarSection **states = push_array(scratch.arena, UIShell_SidebarSection *, section_count);
  F32 *heights = push_array(scratch.arena, F32, section_count);
  U64 *rows = push_array(scratch.arena, U64, section_count);
  F32 *content_heights = push_array(scratch.arena, F32, section_count);
  U64 *entries = push_array(scratch.arena, U64, section_count);
  F32 footer_height = section_panel ? 0 : uishell_sidebar_footer_height(ws);
  F32 available = Max(0.f, dim.y-footer_height-section_count*row_height);
  U64 flexible = section_count;
  for(U64 n = 0; n < section_count; n++)
  {
    String8 key = uishell_sidebar_string(nodes[sections[n]].key);
    UIShell_SidebarSection *section = state->sections;
    for(; section; section = section->next)
    {
      if(str8_match(section->key, key, 0)) { break; }
    }
    if(!section)
    {
      section = push_array(ws->arena, UIShell_SidebarSection, 1);
      section->key = push_str8_copy(ws->arena, key);
      section->next = state->sections;
      state->sections = section;
    }
    if(section_panel && str8_match(only_section, key, 0))
    { section->collapsed = uishell_sidebar_section_collapsed(cfg_node_from_id(uishell_regs()->view)); }
    if(sections[n] == reveal_section)
    {
      section->collapsed = 0;
      if(section_panel && str8_match(only_section, key, 0))
      {
        uishell_sidebar_section_set_collapsed(cfg_node_from_id(uishell_regs()->view), 0);
      }
    }
    states[n] = section;
    U64 end = n+1 < section_count ? sections[n+1] : count;
    for(U64 i = sections[n]; i < end; i++)
    {
      U64 parent = nodes[i].parent;
      if(parent == sections[n] || (parent != ANDAMENTO_NONE && passed[parent] && nodes[parent].parent == sections[n])) { entries[n] += !passed[i]; }
      if(hidden[i] || inlined[i] || passed[i]) { continue; }
      U64 node_rows = !nodes[i].is_section;
      if(project_owner[i] == i) { content_heights[n] += project_gap+2*project_padding; }
      // An expanded ghost is its card's height instead of a row's.
      CFG_Node *ghost = uishell_sidebar_tree_ghost(split->owner_cfg, nodes[i]);
      F32 card_height = 0;
      if(ghost != &cfg_nil_node && uishell_sidebar_pin_expanded(ghost))
      { card_height = uishell_sidebar_ghost_extent(uishell_sidebar_saved_card(ws, ghost), ghost, em); node_rows = 0; rows[n]++; }
      for(U64 c = 0; !nodes[i].is_section && c < nodes[i].control_count; c++)
      {
        AndamentoControl control = {0};
        if(andamento_snapshot_control(state->snapshot, nodes[i].first_control+c, &control) && control.action != ANDAMENTO_NONE) { node_rows++; }
      }
      rows[n] += node_rows;
      F32 node_height = node_rows*row_height+card_height;
      node_height += uishell_sidebar_inline_height(state, uishell_sidebar_string(nodes[i].key));
      U64 owner = project_owner[i];
      if(owner != ANDAMENTO_NONE && owner != i)
      {
        project_child_heights[owner] += node_height;
        node_height *= project_open[owner];
      }
      content_heights[n] += node_height;
    }
    // The Workspaces group opens with its new-workspace entry row.
    if(uishell_sidebar_section_hosts_chrome(key)) { rows[n]++; content_heights[n] += row_height; }
    // During a drag an empty local section opens one row, so it has a gap to
    // drop into.
    for(U64 i = sections[n]+1; sidebar_dragging && !rows[n] && i < end; i++)
    {
      if(nodes[i].parent == sections[n] && str8_match(uishell_sidebar_string(nodes[i].entity_kind), str8_lit(".group"), 0))
      { rows[n] = 1; content_heights[n] += row_height; }
    }
    if(rows[n]) { content_heights[n] += body_top_padding; }
    states[n]->content_height = content_heights[n];
    states[n]->has_controls = nodes[sections[n]].control_count != 0;
    if(!section->collapsed && rows[n] && flexible == section_count) { flexible = n; }
  }
  if(uishell_sidebar_benchmark_active) { uishell_sidebar_analysis_us += now_time_us()-analysis_start; }
  // Secondary sections have a bounded body; the first expanded section fills
  // the remainder. Headers stay outside all scrolling content.
  // Empty sections have no useful controls or content to reveal.
  for(U64 n = 0; n < section_count; n++)
  {
    if(!rows[n] && !nodes[sections[n]].control_count &&
       !uishell_sidebar_section_hosts_chrome(uishell_sidebar_string(nodes[sections[n]].key))) { available += row_height; }
  }
  F32 remaining = available;
  for(U64 n = 0; n < section_count; n++)
  {
    if(n == flexible || states[n]->collapsed || !rows[n]) { continue; }
    F32 wanted = states[n]->height_px > 0 ? states[n]->height_px : Min(content_heights[n], 7*row_height);
    heights[n] = Min(wanted, remaining*0.4f);
    remaining -= heights[n];
  }
  if(flexible < section_count) { heights[flexible] = remaining; }
  if(section_panel)
  {
    for(U64 n = 0; n < section_count; n++)
    { heights[n] = states[n]->collapsed ? 0 : Max(0.f, dim.y-row_height); }
  }
  F32 y = 0;
  UI_Parent(root)
  {
    for(U64 n = 0; n < section_count; n++)
    {
      AndamentoNode *section_node = &nodes[sections[n]];
      if(section_panel ? !str8_match(only_section, uishell_sidebar_string(section_node->key), 0) :
         (!rows[n] && !section_node->control_count &&
          !uishell_sidebar_section_hosts_chrome(uishell_sidebar_string(section_node->key)))) { continue; }
      String8 key = uishell_sidebar_string(section_node->key);
      String8 title = uishell_sidebar_string(section_node->label);
      if(section_node->field_count)
      {
        AndamentoField field = {0};
        andamento_snapshot_field(state->snapshot, section_node->first_field, &field);
        title = uishell_sidebar_string(field.text);
      }
      // The local workspace list is Wheelhouse's Workspaces group (drag-model.md);
      // Andamento's fallback name describes it relative to projects. A section
      // you made, the default one included, is titled by its data instead.
      CFG_Node *local_section = uishell_sidebar_local_section(split->owner_cfg, uishell_sidebar_local_key_id(key));
      if(uishell_sidebar_section_hosts_chrome(key) && local_section == &cfg_nil_node) { title = str8_lit("Workspaces"); }
      UI_Box *header;
      if(states[n]->collapsed && contains_selected[sections[n]])
      { ui_set_next_border_color(uishell_sidebar_selection_fill(1)); }
      UI_Rect(r2f32p(0, y+(!section_panel && n != flexible && heights[n] > 0 ? 6.f : 0.f), dim.x, y+row_height)) UI_ChildLayoutAxis(Axis2_X)
      { header = ui_build_box_from_stringf(states[n]->collapsed && contains_selected[sections[n]] ? UI_BoxFlag_DrawBorder : 0, "###section_header_%S", key); }
      // Header chrome (#210): the grip and inactive actions appear while the
      // header is hovered; the section's collapse sits with its × at the
      // right; the count shows only when collapsed. `header` still holds last frame's rect.
      B32 engaged = contains_2f32(header->rect, ui_mouse()) && !rd_drag_is_active();
      // A section you made has a menu (uishell_local_groups.c). Showing one
      // group, its items' loop gives the menu Reset order.
      UI_Key section_menu = uishell_sidebar_section_menu_key(key);
      String8 section_loop = str8_zero();
      for(U64 i = sections[n]+1; local_section != &cfg_nil_node && i < count && !section_loop.size; i++)
      {
        if(nodes[i].parent == ANDAMENTO_NONE || !passed[nodes[i].parent] || nodes[nodes[i].parent].parent != sections[n]) { continue; }
        section_loop = uishell_sidebar_loop_key(state->snapshot, i);
      }
      UI_Parent(header) UI_PrefHeight(ui_pct(1, 1)) UI_FontSize(floor_f32(em*0.82f)) UI_TagF("weak")
      {
        B32 toggle = 0;
        ui_spacer(ui_em(0.3f, 1));
        if(section_panel) UI_PrefWidth(ui_em(UIShell_GripWidthEM, 1))
        {
          if(engaged)
          {
            // The grip drags the whole section; with one View, that's the
            // View's own drag, so a one-group section's group rules hold.
            UI_Signal drag = uishell_sidebar_grip(str8_lit("section_drag"), str8_lit("Drag section"));
            if(ui_dragging(drag) && !rd_drag_is_active() && length_2f32(ui_drag_delta()) > UIShell_DragThresholdPT)
            {
              CFG_Node *panel = cfg_node_from_id(uishell_regs()->panel);
              U64 views = 0;
              for(CFG_Node *c = panel->first; c != &cfg_nil_node; c = c->next) { views += uishell_sidebar_is_tab(c); }
              rd_drag_begin(UIShell_ContextRegSlot_View);
              if(views > 1) { rd_state->drag_drop_commit = uishell_sidebar_section_drop_commit; }
            }
          }
          else { ui_spacer(ui_em(UIShell_GripWidthEM, 1)); }
        }
        // The panel's other Views sit beside this one's title, in tab order;
        // their titles share what the header's own parts leave.
        UIShell_HeaderTabs tabs = {0};
        if(section_panel)
        {
          String8 upper = upper_from_str8(scratch.arena, title);
          F32 own = fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, upper).x +
            em*(UIShell_GripWidthEM + 4.f) + em*1.7f*(section_node->control_count + 2);
          tabs = uishell_sidebar_header_tabs(scratch.arena, cfg_node_from_id(uishell_regs()->view), dim.x - own);
          tabs.state = state;
          tabs.window = split->owner_cfg;
          tabs.anchor = root;
        }
        uishell_sidebar_header_tabs_ui(&tabs, 0, tabs.selected);
        // The title holds its width; the spacer after the indicator absorbs slack.
        // A docked section's title is also its drag handle: past the shared
        // threshold it starts the section's docking drag, and a plain click
        // still collapses (drag-model.md, Gesture). A middle click closes it,
        // as a tab's does.
        uishell_sidebar_header_separator(&tabs);
        UI_Box *title_box = &ui_nil_box;
        if(uishell_sidebar_local_renaming(state, local_section)) UI_PrefWidth(ui_pct(1, 0)) UI_TagF("")
        { title_box = uishell_sidebar_local_rename_field(state, str8_lit("###section_rename")).box; }
        else UI_PrefWidth(ui_text_dim(4.f, 1)) UI_TagF(tabs.count > 1 ? "" : "weak")
        {
          UI_Signal title_sig = uishell_sidebar_button(push_str8f(scratch.arena, "%S###section_%S", upper_from_str8(scratch.arena, title), key));
          title_box = title_sig.box;
          // Beside other titles, the selected one shows full strength over an
          // accent underline.
          if(tabs.count > 1)
          UI_Parent(title_box) UI_FixedX(ui_top_text_padding()) UI_FixedY(dim_2f32(title_box->rect).y-2.f)
            UI_PrefWidth(ui_px(Max(0.f, dim_2f32(title_box->rect).x - 2*ui_top_text_padding()), 1)) UI_PrefHeight(ui_px(2.f, 1))
            UI_BackgroundColor(rd_drop_accent(1.f)) UI_CornerRadius(1.f)
          { ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Floating, ui_key_from_stringf(title_box->key, "selected_mark")); }
          if(section_panel && ui_dragging(title_sig) && !rd_drag_is_active() && length_2f32(ui_drag_delta()) > UIShell_DragThresholdPT)
          { rd_drag_begin(UIShell_ContextRegSlot_View); }
          toggle |= ui_clicked(title_sig) && !rd_drag_is_active();
          CFG_Node *own_view = cfg_node_from_id(uishell_regs()->view);
          if(section_panel && ui_middle_clicked(title_sig) && rd_dock_can_close(own_view)) { uishell_cmd("close_tab"); }
          if(local_section != &cfg_nil_node && ui_right_clicked(title_sig))
          { state->confirm_delete = 0; uishell_sidebar_menu_open_at_pointer(section_menu, title_sig.box); }
          // Double-clicking a section you made renames it in place; the
          // double click's first click collapsed it, so it opens again.
          if(local_section != &cfg_nil_node && ui_double_clicked(title_sig))
          {
            Temp title_scratch = scratch_begin(0, 0);
            uishell_sidebar_local_begin_rename(state, local_section, uishell_sidebar_local_title(title_scratch.arena, local_section));
            scratch_end(title_scratch);
            toggle = !toggle;
          }
        }
        if(states[n]->collapsed) UI_PrefWidth(ui_text_dim(4.f, 1)) UI_TextColor(uishell_sidebar_ended_color())
        { ui_label(push_str8f(scratch.arena, "%I64u", entries[n])); }
        if(tabs.count) { tabs.boxes[tabs.selected] = title_box; }
        uishell_sidebar_header_tabs_ui(&tabs, tabs.selected+1, tabs.count);
        if(tabs.count) { uishell_sidebar_header_more_ui(&tabs); }
        if(tabs.count) { uishell_sidebar_header_drop(&tabs, header); }
        ui_spacer(ui_pct(1, 0));
        // Display toggles form one segment. It stays visible while any toggle
        // is on, so active filters are always shown; otherwise only on hover.
        B32 any_on = 0;
        for(U64 c = 0; c < section_node->control_count; c++)
        {
          AndamentoControl control = {0};
          if(andamento_snapshot_control(state->snapshot, section_node->first_control+c, &control) &&
             control.action != ANDAMENTO_NONE && control.value_kind == 1 && control.checked) { any_on = 1; }
        }
        B32 segment_shown = engaged || any_on;
        UI_Box *segment;
        UI_PrefWidth(ui_children_sum(1)) UI_ChildLayoutAxis(Axis2_X) UI_CornerRadius(4.f)
        { segment = ui_build_box_from_stringf(segment_shown && section_node->control_count ? UI_BoxFlag_DrawBorder : 0, "###controls_%S", key); }
        // Beside other titles, the controls take the selected title's accent
        // underline: they're its View's.
        if(tabs.count > 1 && segment_shown && section_node->control_count)
        UI_Parent(segment) UI_FixedX(2.f) UI_FixedY(dim_2f32(segment->rect).y-2.f)
          UI_PrefWidth(ui_px(Max(0.f, dim_2f32(segment->rect).x-4.f), 1)) UI_PrefHeight(ui_px(2.f, 1))
          UI_BackgroundColor(rd_drop_accent(1.f)) UI_CornerRadius(1.f)
        { ui_build_box_from_key(UI_BoxFlag_DrawBackground|UI_BoxFlag_Floating, ui_key_from_stringf(segment->key, "selected_mark")); }
        UI_Parent(segment)
        for(U64 c = 0; c < section_node->control_count; c++)
        {
          AndamentoControl control = {0};
          if(!andamento_snapshot_control(state->snapshot, section_node->first_control+c, &control) ||
             control.action == ANDAMENTO_NONE) { continue; }
          B32 checked = control.value_kind == 1 && control.checked;
          String8 detail = control.value_kind != 1 ? str8_zero() :
            checked ? str8_lit("On · click to turn off") : str8_lit("Off · click to turn on");
          UI_Signal sig = uishell_sidebar_header_button(uishell_sidebar_string(control.glyph),
            push_str8f(scratch.arena, "control_%S_%I64u", key, c), checked, segment_shown,
            uishell_sidebar_string(control.label), detail);
          if(ui_clicked(sig)) { action = control.action; }
        }
        // The section's own controls sit at the right, beside the grip's
        // opposite end: collapse, then ×. The collapse indicator shows while
        // the header is hovered; a collapsed header shows its count instead
        // (sidebar-headers.md).
        UI_Transparency(engaged ? 0.f : 1.f)
        { toggle |= ui_clicked(uishell_sidebar_disclosure(!states[n]->collapsed, push_str8f(scratch.arena, "###section_toggle_%S", key))); }
        if(section_panel)
        {
          CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
          // A section you made: holding × opens its menu (Delete lives there).
          // × closes the whole section: every View it holds.
          UI_Signal close = uishell_sidebar_header_button(str8_lit("×"), str8_lit("section_close"), 0, engaged,
            str8_lit("Close section"), local_section != &cfg_nil_node ? str8_lit("Restore it from Sections… · hold for more") :
            str8_lit("Restore it from Sections…"));
          if(local_section != &cfg_nil_node && uishell_sidebar_held(state, close))
          {
            state->confirm_delete = 0;
            F32 width = uishell_sidebar_local_menu_width(state, split->owner_cfg, local_section);
            ui_ctx_menu_open(section_menu, close.box->key, v2f32(dim_2f32(close.box->rect).x-width, dim_2f32(close.box->rect).y));
          }
          else if(ui_clicked(close))
          {
            for(CFG_Node *v = view->parent->first; v != &cfg_nil_node; v = v->next)
            {
              if(!uishell_sidebar_is_tab(v) || !rd_dock_can_close(v)) { continue; }
              UIShell_RegsScope(.panel = view->parent->id, .view = v->id, .tab = v->id) { uishell_cmd("close_tab"); }
            }
          }
        }
        if(local_section != &cfg_nil_node)
        {
          uishell_sidebar_local_section_menu(state, split->owner_cfg, local_section,
            section_panel ? cfg_node_from_id(uishell_regs()->view) : &cfg_nil_node, section_loop, section_menu);
        }
        ui_spacer(ui_px(4.f, 1));
        if(toggle)
        {
          states[n]->collapsed = !states[n]->collapsed;
          if(section_panel)
          {
            CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
            uishell_sidebar_section_set_collapsed(view, states[n]->collapsed);
          }
        }
      }
      y += row_height;
      if(heights[n] <= 0) { continue; }
      // The top edge of a secondary body resizes its allocation without
      // changing another section's scroll position or core state.
      if(!section_panel && n != flexible)
      {
        UI_Rect(r2f32p(0, y-row_height, dim.x, y-row_height+6.f)) UI_HoverCursor(WM_Cursor_UpDown)
        {
          UI_Box *divider = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawSideTop, "###resize_%S", key);
          UI_Signal sig = ui_signal_from_box(divider);
          if(ui_dragging(sig))
          {
            if(ui_pressed(sig)) { F32 initial = heights[n]; ui_store_drag_struct(&initial); }
            states[n]->height_px = Clamp(row_height, *ui_get_drag_struct(F32)-ui_drag_delta().y, Max(row_height, available*0.7f));
          }
        }
      }
      // The section body's screen rect, from last frame's root.
      Rng2F32 section_body_rect = r2f32p(root->rect.x0, root->rect.y0+y, root->rect.x1, root->rect.y0+y+heights[n]);
      UI_ScrollRegionParams params = ui_scroll_region_params(r2f32p(0, y, dim.x, y+heights[n]),
        UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Auto);
      // Open make footers lengthen the scroll range, but never the section's
      // allocation: a hover must not reflow the sections around it.
      F32 scroll_content = content_heights[n]+states[n]->footer_extra;
      params.content_dim_px = v2f32(0, scroll_content);
      UI_ScrollRegion region = ui_scroll_region_layout(params);
      F32 card_inset = dim_2f32(region.viewport).x >= em*16.f ? side_margin : 0.f;
      UI_Key content_key = ui_key_from_stringf(root->key, "section_body_%S", key);
      UI_Box *previous = ui_box_from_key(content_key);
      UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
      axes[Axis2_Y].range = r1s64(0, Max(0, (S64)(scroll_content-heights[n])));
      axes[Axis2_Y].visible = (S64)heights[n];
      F32 target = Clamp(0.f, previous->view_off_target.y, (F32)axes[Axis2_Y].range.max);
      axes[Axis2_Y].position.idx = (S64)target;
      axes[Axis2_Y].position.target_off = target - (F32)axes[Axis2_Y].position.idx;
      UI_ScrollRegionSignal scroll = ui_scroll_region_build(root, content_key, &region, axes,
        UI_BoxFlag_ViewScrollY|UI_BoxFlag_ViewClamp|UI_BoxFlag_AllowOverflowY);
      UI_Box *body = scroll.content_box;
      body->view_off_target.y = (F32)scroll.position.y.idx + scroll.position.y.target_off;
      body->view_off.y = Clamp(0.f, body->view_off.y, (F32)axes[Axis2_Y].range.max);
      body->child_layout_axis = Axis2_Y;
      // Drop lines go in this layer, the body's first child: siblings paint
      // last to first, so a line built after a group's card would paint
      // beneath it (#282).
      UI_Box *drop_layer = &ui_nil_box;
      UI_Parent(body) UI_FixedX(0) UI_FixedY(0) UI_PrefWidth(ui_px(0, 1)) UI_PrefHeight(ui_px(0, 1))
      { drop_layer = ui_build_box_from_key(UI_BoxFlag_Floating, ui_key_from_stringf(body->key, "drop_layer")); }
      UI_Parent(body) UI_PrefWidth(ui_pct(1, 0)) UI_PrefHeight(ui_px(row_height, 1))
      {
        U64 end = n+1 < section_count ? sections[n+1] : count;
        UI_Box *project_box = 0;
        U64 project_depth = 0, project_index = ANDAMENTO_NONE;
        F32 project_children_y = 0;
        ui_spacer(ui_px(body_top_padding, 1));
        F32 row_y = body_top_padding;
        // Make footers (sidebar-headers.md): a group's opens when the pointer
        // reaches its last row. In a section you made, the last group card's
        // also offers New group, below the card.
        CFG_Node *make_section = uishell_sidebar_local_section(split->owner_cfg, uishell_sidebar_local_key_id(key));
        U64 last_group = ANDAMENTO_NONE;
        for(U64 g = sections[n]+1; make_section != &cfg_nil_node && g < end; g++)
        {
          if(nodes[g].parent == sections[n] && !passed[g] && str8_match(uishell_sidebar_string(nodes[g].entity_kind), str8_lit(".group"), 0))
          { last_group = g; }
        }
        Rng2F32 card_last_row = {0}, body_last_row = {0};
        F32 footer_extra = 0;
        B32 make_engagable = !rd_drag_is_active() && !ui_any_ctx_menu_is_open();
        // A collapsed body isn't built, which leaves new-workspace to the action row.
        if(uishell_sidebar_section_hosts_chrome(key))
        {
          // Chrome resolution reads this next frame (ADR-0006).
          ws->chrome_section_header_frame = rd_state->frame_index+1;
          // A section you made (Workspaces) offers it in its footer instead.
          if(make_section == &cfg_nil_node)
          {
            uishell_sidebar_new_workspace_entry(split, row_height, side_margin);
            row_y += row_height;
          }
        }
        // One step past the section's last row closes its last card.
        for(U64 i = sections[n]; i <= end; i++)
        {
          if(project_box && (i == end || depth[i] <= project_depth))
          {
            ui_pop_flags();
            ui_pop_parent(); // clipped project children
            F32 card_footer = 0, outer_footer = 0;
            {
              AndamentoNode owner_node = nodes[project_index];
              B32 is_project = str8_match(uishell_sidebar_string(owner_node.entity_kind), str8_lit("project"), 0);
              CFG_Node *group_cfg = is_project ? &cfg_nil_node : uishell_sidebar_local_group(split->owner_cfg, uishell_sidebar_string(owner_node.entity_id));
              B32 outer = project_index == last_group;
              String8 outer_key = push_str8f(scratch.arena, "group_%S", key);
              UI_Box *outer_box = ui_box_from_key(ui_key_from_stringf(body->key, "###make_%S", outer_key));
              Vec2F32 mouse = ui_mouse();
              // New group, after the last group, also opens from anywhere
              // below that card, alone.
              B32 engaged = make_engagable && contains_2f32(project_box->rect, mouse) && mouse.y >= card_last_row.y0;
              B32 outer_engaged = outer && make_engagable && (engaged || contains_2f32(outer_box->rect, mouse) ||
                (contains_2f32(section_body_rect, mouse) && mouse.y >= project_box->rect.y1));
              F32 header_inset = em*(0.3f+0.4f*(depth[project_index]+1)+uishell_sidebar_group_row_inset);
              if(!owner_node.collapsed && (is_project || group_cfg != &cfg_nil_node))
              {
                UIShell_MakeAction make = is_project ?
                  (UIShell_MakeAction){UIShell_Make_ProjectWorkspace, str8_lit("New workspace"), 0, uishell_sidebar_string(owner_node.entity_id)} :
                  (UIShell_MakeAction){UIShell_Make_Workspace, str8_lit("New workspace"), group_cfg->id};
                card_footer = uishell_sidebar_make_footer(state, split->owner_cfg, push_str8f(scratch.arena, "workspace_%S", uishell_sidebar_string(owner_node.key)),
                  &make, 1, engaged, row_height, 4.f+header_inset);
              }
              ui_spacer(ui_px(project_padding, 1));
              ui_pop_parent();
              ui_spacer(ui_px(project_gap, 1));
              if(outer)
              {
                UIShell_MakeAction make = {UIShell_Make_Group, str8_lit("New group"), make_section->id};
                outer_footer = uishell_sidebar_make_footer(state, split->owner_cfg, outer_key, &make, 1, outer_engaged, row_height,
                  2.f+card_inset+4.f+header_inset-em*uishell_sidebar_group_row_inset);
              }
            }
            footer_extra += card_footer + outer_footer;
            row_y = project_children_y + project_child_heights[project_index]*project_open[project_index] + project_padding+project_gap + card_footer+outer_footer;
            project_box = 0;
          }
          if(i == end) { break; }
          U64 owner = project_owner[i];
          if(hidden[i] || inlined[i] || passed[i] ||
             (owner != ANDAMENTO_NONE && owner != i && project_open[owner] == 0.f)) { continue; }
          AndamentoNode node = nodes[i];
          B32 children = row_children[i] || inline_count[i];
          String8 node_key = uishell_sidebar_string(node.key);
          B32 drag_sibling = row_dragging && !node.is_section &&
            str8_match(uishell_sidebar_loop_key(state->snapshot, i), state->row_drag_loop, 0);
          B32 drag_source = drag_sibling && str8_match(node_key, state->row_drag_key, 0);
          if(!drag_sibling && depth[i] <= drag_depth) { drag_subtree = 0; }
          String8 full_label = uishell_sidebar_string(node.label);
          String8 label = full_label;
          String8 kind = uishell_sidebar_string(node.entity_kind);
          String8 status = uishell_sidebar_chip_status(state, node);
          String8 context = str8_zero();
          for(U64 f = 0; f < node.field_count; f++)
          {
            AndamentoField field = {0};
            andamento_snapshot_field(state->snapshot, node.first_field+f, &field);
            String8 value = uishell_sidebar_string(field.text);
            if(uishell_sidebar_chip_field(state, node, f, value))
            {
              continue;
            }
            // Native templates declare the display label first. The core may
            // abbreviate it; node.label remains the full hover/inspection text.
            if(f == 0 && value.size) { label = value; }
            // Native Attention templates append context identities after the
            // label/kind/state fields. Match identities without parsing them.
            if(f >= 3 && value.size && !str8_match(kind, str8_lit("change_request"), 0))
            {
              U64 context_start = uishell_sidebar_benchmark_active ? now_time_us() : 0;
              value = uishell_sidebar_context_label(state, nodes, count, value);
              if(uishell_sidebar_benchmark_active) { uishell_sidebar_context_us += now_time_us()-context_start; }
              if(!str8_match(value, label, 0))
              { context = context.size ? push_str8f(scratch.arena, "%S / %S", context, value) : value; }
            }
          }
          // A group card holds its items like a project; only a project
          // also shows itself (its overview) as its first chip.
          B32 project = project_owner[i] == i;
          B32 project_chip = project && str8_match(kind, str8_lit("project"), 0);
          U64 chip_count = (node.collapsed ? 0 : inline_count[i])+project_chip;
          U64 *members = push_array(scratch.arena, U64, chip_count);
          UIShell_ChipMeasure *chip_measures = push_array(scratch.arena, UIShell_ChipMeasure, chip_count);
          U64 member_count = 0;
          if(project_chip) { members[member_count++] = i; }
          for(U64 j = node.collapsed ? ANDAMENTO_NONE : inline_first[i]; j != ANDAMENTO_NONE; j = inline_next[j]) { members[member_count++] = j; }
          for(U64 c = 0; c < chip_count; c++)
          {
            chip_measures[c].width = uishell_sidebar_chip_width(state, nodes[members[c]]);
            chip_measures[c].unopened_workspace = !uishell_sidebar_is_subject(nodes[members[c]]) &&
              nodes[members[c]].state != ANDAMENTO_LIVE;
            chip_measures[c].attention = uishell_sidebar_chip_attention(state, nodes[members[c]]);
          }
          String8 names[3] = {label,
            uishell_sidebar_chip_fact(state, node, str8_lit("chip-medium:")),
            uishell_sidebar_chip_fact(state, node, str8_lit("chip-short:"))};
          if(!names[1].size) { names[1] = names[0]; }
          if(!names[2].size) { names[2] = names[1]; }
          F32 name_widths[3];
          for(U64 c = 0; chip_count && c < 3; c++)
          { name_widths[c] = fnt_dim_from_tag_size_string(ui_top_font(), em, 0, 0, names[c]).x+em; }
          F32 indent = 0.3f+Min(depth[i], (str8_match(kind, str8_lit("role"), 0) || str8_match(kind, str8_lit("convoy"), 0) || uishell_sidebar_is_subject(node)) ? 3 : 1)*0.4f;
          // A group's rows sit half an icon in from its title.
          if(owner != ANDAMENTO_NONE && owner != i) { indent += uishell_sidebar_group_row_inset; }
          F32 status_width = em*(str8_match(kind, str8_lit("change_request"), 0) ? 10.f : 1.2f);
          F32 row_width = Max(0.f, dim_2f32(region.viewport).x-8.f-side_margin-(owner != ANDAMENTO_NONE ? 4.f+card_inset : 0.f));
          // A group header leads with its title inset (to its rows' icons),
          // then a collapsed count and the 1.2em collapse indicator
          // (uishell_sidebar_row_begin); other rows with indent, disclosure
          // and icon.
          F32 lead = em*(indent+1.5f+1.2f);
          if(project)
          {
            lead = uishell_sidebar_group_title_lead(0.3f+0.4f*(depth[i]+1)) + em*1.2f;
            if(node.collapsed)
            {
              U64 shown = 0;
              for(U64 c = i+1; c < end && depth[c] > depth[i]; c++) { shown += nodes[c].parent == i; }
              if(shown) { lead += fnt_dim_from_tag_size_string(ui_top_font(), em, 0, 0, push_str8f(scratch.arena, "%I64u", shown)).x + 4.f + 2*ui_top_text_padding(); }
            }
          }
          F32 available_width = Max(0.f, row_width-lead-status_width-(project ? 3.f : 0.f));
          F32 overflow_width = chip_count ? Max(em*2.5f,
            fnt_dim_from_tag_size_string(ui_top_font(), em, 0, 0,
              push_str8f(scratch.arena, "+%I64u", chip_count)).x+8.f) : 0;
          UIShell_ChipLayout chip_layout = {0};
          if(chip_count) { chip_layout = uishell_chip_layout(Max(0.f, available_width-2.f), name_widths, minimum_name,
            chip_measures, chip_count, overflow_width); }
          F32 chip_clearance = chip_count ? 2.f : 0.f;
          if(chip_count) { label = names[chip_layout.tier]; }

          if(project)
          {
            Vec4F32 accent = uishell_sidebar_card_accent(node);
            // Keep rounded strokes and their antialiasing inside the viewport clip.
            UI_FixedX(2.f+card_inset) UI_PrefWidth(ui_px(Max(0.f, dim_2f32(region.viewport).x-4.f-side_margin-card_inset), 1))
            UI_PrefHeight(ui_children_sum(1)) UI_ChildLayoutAxis(Axis2_Y) UI_CornerRadius(5.f)
            UI_BackgroundColor(mix_4f32(ui_color_from_name(str8_lit("background")), accent, 0.035f))
            {
              project_box = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground, "###project_%S", node_key);
            }
            if(sidebar_dragging) { group_rects[i] = project_box->rect; }
            B32 is_project = str8_match(uishell_sidebar_string(node.entity_kind), str8_lit("project"), 0);
            if(row_dragging && is_project) { uishell_sidebar_home_claim(state, project_box->rect, uishell_sidebar_string(node.entity_id), 0); }
            // A data row stays in its project: another project refuses it.
            if(row_dragging && is_project && !groups_dragging && uishell_sidebar_drag_local(state) == &cfg_nil_node)
            {
              B32 own = 0;
              for(U64 j = drag_row_index; j != ANDAMENTO_NONE && !own; j = nodes[j].parent) { own = j == i; }
              if(!own) { uishell_sidebar_refuse(drop_layer, project_box->rect, str8_lit("rows stay in their project")); }
            }
            project_depth = depth[i];
            project_index = i;
            ui_push_parent(project_box);
            ui_spacer(ui_px(project_padding, 1));
            row_y += project_padding;
          }
          B32 contains_current = node.collapsed && contains_selected[i] && !node.selected;
          UI_Key entry_key = ui_key_zero();
          if(!node.is_section)
          {
            if(i == reveal)
            {
              body->view_off_target.y = Clamp(0.f, row_y-(heights[n]-row_height)*0.5f, (F32)axes[Axis2_Y].range.max);
              body->view_off.y = body->view_off_target.y;
              state->reveal_workspace_id = 0;
              rd_request_frame();
            }
            // An expanded ghost draws its card in its row's place.
            CFG_Node *ghost = uishell_sidebar_tree_ghost(split->owner_cfg, node);
            // A revealed pin scrolls into view; an expanded one takes focus,
            // and the reveal completes once its card has been measured.
            if(ghost != &cfg_nil_node && ghost->id == state->pin_reveal)
            {
              body->view_off_target.y = Clamp(0.f, row_y, (F32)axes[Axis2_Y].range.max);
              B32 expanded = uishell_sidebar_pin_expanded(ghost);
              UIShell_HoverCard *card = uishell_sidebar_saved_card(ws, ghost);
              card->focused = expanded;
              if(!expanded || card->content_height > 0) { state->pin_reveal = 0; }
              rd_request_frame();
            }
            if(ghost != &cfg_nil_node && uishell_sidebar_pin_expanded(ghost))
            {
              UIShell_HoverCard *card = uishell_sidebar_saved_card(ws, ghost);
              F32 extent = uishell_sidebar_ghost_extent(card, ghost, em);
              if(project_box && owner == project_index) { card_last_row = card->rect; }
              else { body_last_row = card->rect; }
              if(drag_sibling) { drag_body = drop_layer; drag_siblings[drag_sibling_count++] = (UIShell_RowDragSibling){i, card->rect, card->rect}; }
              if(sidebar_dragging) { group_items[group_item_count++] = (UIShell_RowDragSibling){i, card->rect, card->rect}; }
              // The moving card keeps its place until it lands.
              if(card->moving) { ui_spacer(ui_px(extent, 1)); }
              else UI_PrefHeight(ui_children_sum(1))
              {
                uishell_sidebar_ghost_card(state, ws, card, ghost, Max(0.f, dim_2f32(region.viewport).x-12.f-side_margin));
                ui_spacer(ui_px(UIShell_HoverCardPinnedGapPT, 1));
              }
              row_y += extent;
              continue;
            }
            F32 slot_y = row_y;
            row_y += row_height;
            // Text remains available in the tooltip; terse marks distinguish
            // selected/open workspaces from producer activity state.
            // daily-driver declares prefix="orphaned:" on the boolean orphan fact.
            B32 orphaned = 0;
            for(U64 f = 3; str8_match(kind, str8_lit("change_request"), 0) && f < node.field_count; f++)
            {
              AndamentoField field = {0}; andamento_snapshot_field(state->snapshot, node.first_field+f, &field);
              orphaned |= str8_match(uishell_sidebar_string(field.text), str8_lit("orphaned:true"), 0);
            }
            if(orphaned) { label = push_str8f(scratch.arena, "%S · orphaned", label); }
            String8 display = context.size ? push_str8f(scratch.arena, "%S · %S", label, context) : label;
            if(str8_match(uishell_sidebar_string(node.layout), str8_lit("fields"), 0))
            { display = uishell_sidebar_fields(scratch.arena, state->snapshot, node, Max(0.f, dim.x-em*6.f)); }
            B32 icon_entry = chip_count && chip_layout.name_width == 0 && !project;
            // A group header's count: its rows, shown while it is collapsed.
            U64 header_count = 0;
            for(U64 c = i+1; project && node.collapsed && c < end && depth[c] > depth[i]; c++) { header_count += nodes[c].parent == i; }
            UIShell_SidebarRow r = {.node = node, .present = 1, .key = node_key, .text = display, .status = status,
              .height = row_height, .indent = project ? 0.3f+0.4f*(depth[i]+1) : indent, .project = project, .selected = node.selected,
              .clickable = project, .count = header_count,
              .title_max = project ? Max(em*2.f, available_width-(chip_count ? chip_layout.chip_width+chip_clearance : 0.f)) : 0,
              // A compact ghost's disclosure expands it into its card.
              .contains_current = contains_current, .disclosure = (children && node.toggle != ANDAMENTO_NONE) || ghost != &cfg_nil_node,
              .expanded = ghost == &cfg_nil_node && !node.collapsed, .entry = 1, .icon_entry = icon_entry,
              // A ghost is a reference: it lives elsewhere.
              .reference = ghost != &cfg_nil_node,
              .renaming = str8_match(kind, str8_lit(".group"), 0) &&
                uishell_sidebar_local_renaming(state, uishell_sidebar_local_group(split->owner_cfg, uishell_sidebar_string(node.entity_id)))};
            // Persistent workspace selection remains visible while the terminal
            // has focus, without borrowing the keyboard-focus border.
            // Insets keep row selection and action borders inside the container.
            // The dragged row dims in place until the drop lands it.
            if(drag_source) { ui_push_transparency(0.6f); }
            uishell_sidebar_row_begin(state, &r);
            UI_Box *slot = r.slot;
            if(project_box && owner == project_index) { card_last_row = slot->rect; }
            else { body_last_row = slot->rect; }
            if(sidebar_dragging && node.parent != ANDAMENTO_NONE &&
               str8_match(uishell_sidebar_string(nodes[node.parent].entity_kind), str8_lit(".group"), 0))
            { group_items[group_item_count++] = (UIShell_RowDragSibling){i, slot->rect, slot->rect}; }
            if(drag_sibling)
            {
              drag_body = drop_layer;
              drag_siblings[drag_sibling_count++] = (UIShell_RowDragSibling){i, slot->rect, project ? project_box->rect : slot->rect};
              drag_depth = depth[i]; drag_subtree = 1;
            }
            else if(drag_subtree && drag_sibling_count && depth[i] > drag_depth)
            {
              Rng2F32 *extent = &drag_siblings[drag_sibling_count-1].extent;
              extent->y1 = Max(extent->y1, slot->rect.y1);
            }
            if(icon_entry)
            {
              entry_key = r.icon_sig.box->key;
              size_t requested = uishell_sidebar_entry_signal(state, ws, node, i, r.icon_sig, context, contains_current, 0, 0);
              if(requested != ANDAMENTO_NONE) { action = requested; }
            }
            // A group header's signal is its whole row's, read after row_end.
            if(!project)
            {
              UI_Signal sig = r.entry_sig;
              entry_key = sig.box->key;
              // The rich tooltip already includes the full label. Do not also
              // enroll this row in the shell's automatic truncated-text hover.
              sig.box->flags |= UI_BoxFlag_DisableTruncatedHover;
              size_t requested = r.renaming ? ANDAMENTO_NONE : uishell_sidebar_entry_signal(state, ws, node, i, sig, context, contains_current, 0, 0);
              if(requested != ANDAMENTO_NONE) { action = requested; }
            }
            if(chip_count)
            {
              UI_Box *chip_area;
              UI_PrefWidth(ui_px(chip_layout.chip_width+chip_clearance, 1)) UI_ChildLayoutAxis(Axis2_X)
              {
                chip_area = ui_build_box_from_stringf(UI_BoxFlag_Clip|UI_BoxFlag_AllowOverflowX|
                  UI_BoxFlag_ViewScrollX|UI_BoxFlag_ViewClampX, "###chips_%S", node_key);
              }
              UI_Parent(chip_area)
              {
                for(U64 c = 0; c < chip_count; c++)
                {
                  if(chip_measures[c].folded) { continue; }
                  size_t requested = uishell_sidebar_inline_action(state, ws, nodes[members[c]], members[c],
                    members[c] == i ? str8_zero() : full_label, row_height, 0);
                  if(requested != ANDAMENTO_NONE) { action = requested; }
                }
                if(chip_layout.folded)
                {
                  UI_Key menu_key = ui_key_from_stringf(root->key, "overflow_%S", node_key);
                  UI_CtxMenu(menu_key) UI_PrefWidth(ui_em(24.f, 1)) UI_PrefHeight(ui_px(row_height, 1))
                  {
                    for(U64 c = 0; c < chip_count; c++)
                    {
                      if(!chip_measures[c].folded) { continue; }
                      UI_ChildLayoutAxis(Axis2_X)
                      {
                        UI_Box *menu_row = ui_build_box_from_stringf(0, "###overflow_row_%S", uishell_sidebar_string(nodes[members[c]].key));
                        UI_Parent(menu_row)
                        {
                          size_t requested = uishell_sidebar_inline_action(state, ws, nodes[members[c]], members[c], full_label, row_height, 1);
                          if(requested != ANDAMENTO_NONE) { action = requested; ui_ctx_menu_close(); }
                        }
                      }
                    }
                  }
                  ui_spacer(ui_px(4.f, 1));
                  UI_CornerRadius(3.f) UI_FixedY(Max(0.f, (row_height-4.f-em*1.6f)*0.5f))
                  UI_PrefHeight(ui_px(em*1.6f, 1)) UI_PrefWidth(ui_px(overflow_width-4.f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center)
                  {
                    ui_set_next_border_color(uishell_sidebar_action_border());
                    UI_Signal sig = ui_button(push_str8f(scratch.arena, "+%I64u###overflow_%S", chip_layout.folded, node_key));
                    uishell_sidebar_reveal_chip(state, sig.box);
                    if(ui_clicked(sig)) { ui_ctx_menu_open(menu_key, sig.box->key, v2f32(0, row_height)); }
                  }
                }
              }
              UI_Parent(chip_area) { ui_spacer(ui_px(chip_clearance, 1)); }
              ui_signal_from_box(chip_area);
            }
            // A local group header's actions follow it, shown while hovered;
            // a project's (New workspace here) are its footer's and its
            // right-click menu's, so its chips never shift.
            if(project && str8_match(kind, str8_lit(".group"), 0) && contains_2f32(r.row->rect, ui_mouse()) && !rd_drag_is_active())
            {
              UI_Signal more = uishell_sidebar_header_button(str8_lit("⋯"), push_str8f(scratch.arena, "header_more_%S", node_key), 0, 1,
                str8_lit("Group options"), str8_lit("Or right-click the header"));
              if(ui_clicked(more)) { state->header_more_anchor = more.box->key; state->header_more_open = 1; }
            }
            uishell_sidebar_row_end(state, &r);
            if(ui_clicked(r.toggle))
            {
              if(ghost != &cfg_nil_node) { uishell_sidebar_ghost_set_expanded(ghost, 1); rd_request_frame(); }
              else { action = node.toggle; }
            }
            if(project)
            {
              UI_Signal sig = r.row_sig;
              entry_key = sig.box->key;
              size_t requested = r.renaming ? ANDAMENTO_NONE : uishell_sidebar_entry_signal(state, ws, node, i, sig, context, contains_current, 0, 1);
              if(requested != ANDAMENTO_NONE) { action = requested; }
            }
            // Project cards already stop short of the margin.
            ui_spacer(ui_px(4.f+(owner == ANDAMENTO_NONE ? side_margin : 0), 1));
            ui_pop_parent();
            if(drag_source) { ui_pop_transparency(); }
            UIShell_SidebarCloseKind close = uishell_sidebar_close_kind(node, status);
            Rng2F32 zone = r2f32p(slot->rect.x0, slot->rect.y0, body->rect.x1, slot->rect.y1);
            B32 held = ui_key_match(state->hold_key, ui_key_from_stringf(body->key, "###close_%S", node_key));
            if(close != UIShell_SidebarCloseKind_None && !ui_key_match(entry_key, ui_key_zero()) &&
               !ui_any_ctx_menu_is_open() && !rd_drag_is_active() && (held || contains_2f32(zone, ui_mouse())))
            {
              UI_Key menu_key = ui_key_from_stringf(entry_key, uishell_sidebar_is_subject(node) ? "subject_menu" : "workspace_menu");
              UI_Signal sig = {0};
              // The control stops short of an overlay scroll bar's strip at
              // the edge, so the bar never sits over it (ui_scroll_region).
              F32 bar_lane = region.params.style == UI_ScrollBarStyle_Overlay && !region.params.overlay_reserve ?
                region.params.overlay_rest_px+region.params.overlay_inset_px : 0.f;
              UI_Parent(body) UI_FixedX(dim_2f32(region.viewport).x-side_margin) UI_FixedY(slot_y+2.f)
              UI_PrefWidth(ui_px(Max(4.f, side_margin-2.f-bar_lane), 1)) UI_PrefHeight(ui_px(row_height-4.f, 1))
              { sig = uishell_sidebar_margin_button(node, close); }
              uishell_sidebar_margin_close(state, ws, node, close, sig, entry_key, menu_key);
            }
          }
          if(!node.is_section)
          {
            F32 inline_height = uishell_sidebar_inline_height(state, node_key);
            uishell_sidebar_inline_ui(ws, node_key, Max(0.f, dim_2f32(region.viewport).x-8));
            row_y += inline_height;
          }
          for(U64 c = 0; !node.is_section && c < node.control_count; c++)
          {
            AndamentoControl control = {0};
            if(andamento_snapshot_control(state->snapshot, node.first_control+c, &control) && control.action != ANDAMENTO_NONE)
            {
              row_y += row_height;
              if(ui_clicked(uishell_sidebar_button(push_str8f(scratch.arena, "%S###control_%S_%I64u", uishell_sidebar_string(control.label), node_key, c)))) { action = control.action; }
            }
          }
          if(project)
          {
            UI_Box *children_box;
            UI_PrefHeight(ui_px(project_child_heights[i]*project_open[i], 1)) UI_ChildLayoutAxis(Axis2_Y)
            {
              children_box = ui_build_box_from_stringf(UI_BoxFlag_Clip|UI_BoxFlag_AllowOverflowY,
                "###project_children_%S", node_key);
            }
            project_children_y = row_y;
            ui_push_parent(children_box);
            // Semantic collapse disables input immediately, while the previous
            // rows can still be drawn through the shrinking viewport.
            ui_push_flags(ui_top_flags() | (node.collapsed ? UI_BoxFlag_Disabled|UI_BoxFlag_IgnoreInteraction : 0));
          }
        }
        // A section without group cards (one group, or Workspaces): its
        // footer follows its rows, with New workspace and New group side by side.
        CFG_Node *only_group = make_section != &cfg_nil_node && last_group == ANDAMENTO_NONE ? uishell_sidebar_local_only_group(make_section) : &cfg_nil_node;
        if(only_group != &cfg_nil_node)
        {
          String8 make_key = push_str8f(scratch.arena, "section_%S", key);
          Rng2F32 viewport = section_body_rect;
          F32 top = body_last_row.y1 > body_last_row.y0 ? body_last_row.y0 : viewport.y0;
          UI_Box *make_box = ui_box_from_key(ui_key_from_stringf(body->key, "###make_%S", make_key));
          Vec2F32 mouse = ui_mouse();
          B32 engaged = make_engagable && ((contains_2f32(viewport, mouse) && mouse.y >= top) || contains_2f32(make_box->rect, mouse));
          UIShell_MakeAction makes[] = {
            {UIShell_Make_Workspace, str8_lit("New workspace"), only_group->id},
            {UIShell_Make_Group, str8_lit("New group"), make_section->id},
          };
          footer_extra += uishell_sidebar_make_footer(state, split->owner_cfg, make_key, makes, ArrayCount(makes), engaged, row_height, 4.f+em*0.7f);
        }
        states[n]->footer_extra = footer_extra;
      }
      // Children get first refusal; the viewport consumes the remaining wheel
      // input. Header and sibling viewport geometry are outside this box.
      // Each local group in this section offers an insertion point among its
      // items; a section's only group spans the section's viewport.
      U64 end_of_section = n+1 < section_count ? sections[n+1] : count;
      // Groups go between groups instead (#282).
      if(groups_dragging)
      { uishell_sidebar_groups_claim(state, drop_layer, split->owner_cfg, nodes, sections[n], sections[n], end_of_section, passed, group_rects,
                                     group_items, group_item_count, body->parent->rect); }
      else if(section_refusal.size && uishell_regs()->panel != rd_state->drag_drop_regs->panel)
      { uishell_sidebar_refuse(drop_layer, body->parent->rect, section_refusal); }
      for(U64 g = sections[n]; sidebar_dragging && !groups_dragging && g < end_of_section; g++)
      {
        if(!str8_match(uishell_sidebar_string(nodes[g].entity_kind), str8_lit(".group"), 0) || nodes[g].parent != sections[n]) { continue; }
        Rng2F32 area = passed[g] ? body->parent->rect : group_rects[g];
        uishell_sidebar_group_claim(state, drop_layer, nodes, g, group_items, group_item_count, area, row_height);
      }
      ui_signal_from_box(body);
      y += heights[n];
    }
    if(drag_sibling_count && drag_body)
    { uishell_sidebar_row_drop(state, drag_body, nodes, drag_siblings, drag_sibling_count, row_height); }
    if(!section_panel)
    { uishell_sidebar_footer_ui(r2f32p(0, dim.y-footer_height, dim.x, dim.y), split); }
  }
  if(uishell_sidebar_subject_fixture && uishell_sidebar_subject_geometry_path.size)
  {
    // Fixture-only controller state accompanies hit rectangles for diagnostics.
    UIShell_HoverCard *c = &state->cards[0];
    write_data_to_file_path(push_str8f(scratch.arena, "%S.cards", uishell_sidebar_subject_geometry_path),
      push_str8f(scratch.arena, "{\"mouse\":[%g,%g],\"open\":%u,\"engaged\":%u,\"focused\":%u,\"candidate_since\":%I64u,\"now\":%I64u,\"source_seen\":%u,\"rect\":[%g,%g,%g,%g]}",
        ui_state->mouse.x, ui_state->mouse.y, c->open, c->engaged, c->focused,
        c->candidate_since, now_time_us(), c->source_seen, c->rect.x0, c->rect.y0, c->rect.x1, c->rect.y1));
    StringJoin join = {.pre = str8_lit("["), .sep = str8_lit(","), .post = str8_lit("]")};
    write_data_to_file_path(uishell_sidebar_subject_geometry_path,
      str8_list_join(scratch.arena, &uishell_sidebar_subject_geometry, &join));
  }
  scratch_end(scratch);
  // Dispatch only against the snapshot used above, before topology mutations.
  if(state->core != 0)
  {
    if(action != ANDAMENTO_NONE) { uishell_sidebar_perform(state, split, action); }
    if(state->order_pending)
    {
      state->order_pending = 0;
      // An order can name a ghost or workspace the drop just made or moved.
      uishell_sidebar_publish(state, split);
      uishell_sidebar_set_order(state, state->order_loop, state->order, state->order_count);
    }
    if(prepare || action != ANDAMENTO_NONE)
    {
      Temp scratch = scratch_begin(0, 0);
      UIShell_ControlledSplit current = uishell_root_controlled_split_from_window(scratch.arena, split->owner_cfg);
      uishell_sidebar_observe(state, &current);
      scratch_end(scratch);
    }
  }
}

internal void
uishell_sidebar_ui(Rng2F32 rect, UIShell_ControlledSplit *split)
{ uishell_sidebar_render(rect, split, (UIShell_SidebarRenderParams){UIShell_SidebarRenderMode_DiagnosticAggregate}); }

RD_VIEW_UI_FUNCTION_DEF(sidebar_section)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
  CFG_Node *window = rd_window_from_cfg(view);
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  // Non-composite preview surfaces own their complete presentation; they do
  // not render the root split's fleet controls inside the preview content.
  if(ws->active_workspace_surface_entry && !ws->active_workspace_surface_entry->composite)
  { scratch_end(scratch); return; }
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  String8 key = cfg_node_child_from_string(view, str8_lit("section"))->first->string;
  if(key.size) { uishell_sidebar_render(rect, &split, (UIShell_SidebarRenderParams){UIShell_SidebarRenderMode_SectionPanel, key}); }
  else
  {
    // Keep an invalid saved View visible instead of silently discarding it.
    UI_WidthFill UI_PrefHeight(ui_em(2.2f, 1)) UI_TagF("weak")
    { ui_label(str8_lit("Section unavailable")); }
  }
  scratch_end(scratch);
}

// CFG nil nodes self-link: an absent section setting or value yields an empty
// key, which reconciliation treats as an undeclared/corrupt saved View.
internal String8
uishell_sidebar_section_key(CFG_Node *view)
{
  return cfg_node_child_from_string(view, str8_lit("section"))->first->string;
}

// Saved arrangements use the same panel tree as Workspace Regions. Section
// identity comes from the snapshot; no placement hints are written into KDL.
internal CFG_Node *
uishell_sidebar_find_view(CFG_Node *container, String8 key)
{
  for(CFG_Node *c = container->first; c != &cfg_nil_node; c = c->next)
  {
    if(str8_match(c->string, str8_lit("sidebar_section"), 0) &&
       str8_match(uishell_sidebar_section_key(c), key, 0)) { return c; }
    if(rd_dock_is_container(c))
    {
      CFG_Node *found = uishell_sidebar_find_view(c, key);
      if(found != &cfg_nil_node) { return found; }
    }
  }
  return &cfg_nil_node;
}

// Region inventory belongs to this Controlled Split, alongside its docking
// roots. A known id with no View records an intentional close, not a new region.
#define UISHELL_REGION_INVENTORY str8_lit("section_positions")

struct UIShell_SectionPlacement
{
  String8 key, title, default_host;
  S64 order;
};

internal U64
uishell_sidebar_region_index(UIShell_SectionPlacement *regions, U64 count, String8 key)
{
  for(U64 i = 0; i < count; i++)
  { if(str8_match(regions[i].key, key, 0)) { return i; } }
  return ANDAMENTO_NONE;
}

internal B32
uishell_sidebar_panel_has_content(CFG_Node *panel)
{
  for(CFG_Node *n = panel->first; n != &cfg_nil_node; n = n->next)
  { if(rd_dock_view_from_name(n->string) || rd_dock_is_container(n)) { return 1; } }
  return 0;
}

internal void
uishell_sidebar_prune_empty_panel(CFG_Node *panel)
{
  // The View was found within this level's sidebar/floating host. Remove empty
  // intermediate wrappers, stopping before the host and its owning split.
  while(panel != &cfg_nil_node && rd_dock_is_container(panel) &&
        !str8_match(panel->string, RD_DOCK_SIDEBAR_ROOT, 0) &&
        !str8_match(panel->string, str8_lit("floating_panels"), 0) &&
        !uishell_sidebar_panel_has_content(panel))
  {
    CFG_Node *parent = panel->parent;
    cfg_node_release(rd_state->cfg, panel); panel = parent;
  }
}

// Reconciliation and duplicate repair share empty-wrapper cleanup. Keep
// unrelated contents and their saved allocations; do not rescale siblings.
internal B32
uishell_sidebar_cleanup_region_panel(CFG_Node *panel, B32 removed)
{
  CFG_Node *cleanup = cfg_node_child_from_string(panel, str8_lit("section_hint_cleanup"));
  if((removed || cleanup != &cfg_nil_node) && !uishell_sidebar_panel_has_content(panel))
  { cfg_node_release(rd_state->cfg, panel); return 1; }
  if(cleanup != &cfg_nil_node) { cfg_node_release(rd_state->cfg, cleanup); }
  return 0;
}

internal B32
uishell_sidebar_prune_regions(CFG_Node *owner, CFG_Node *container, UIShell_SectionPlacement *regions, U64 count, B32 reset)
{
  B32 removed = 0;
  for(CFG_Node *c = container->first, *next; c != &cfg_nil_node; c = next)
  {
    next = c->next;
    if(str8_match(c->string, str8_lit("sidebar_section"), 0))
    {
      // CFG nil nodes self-link, so a missing section setting reads as empty.
      // A View without a declared identity is corrupt saved state; drop it.
      String8 key = uishell_sidebar_section_key(c);
      // A section someone made is kept while it exists in the window's data,
      // even before Andamento has placed it (just made, or just migrated).
      B32 local = owner != &cfg_nil_node && uishell_sidebar_local_section_exists(owner, key);
      if(reset || (!local && uishell_sidebar_region_index(regions, count, key) == ANDAMENTO_NONE))
      { cfg_node_release(rd_state->cfg, c); removed = 1; }
    }
    else if(rd_dock_is_container(c))
    {
      B32 child_removed = uishell_sidebar_prune_regions(owner, c, regions, count, reset);
      removed |= child_removed;
      removed |= uishell_sidebar_cleanup_region_panel(c, child_removed || reset);
    }
  }
  return removed;
}

internal void
uishell_sidebar_reset_regions(CFG_Node *owner)
{
  // Only this level's hosts; child Workspace Regions own independent layouts.
  CFG_Node *sidebar = cfg_node_child_from_string(owner, RD_DOCK_SIDEBAR_ROOT);
  CFG_Node *floating = cfg_node_child_from_string(owner, str8_lit("floating_panels"));
  uishell_sidebar_prune_regions(owner, sidebar, 0, 0, 1);
  uishell_sidebar_prune_regions(owner, floating, 0, 0, 1);
  if(sidebar != &cfg_nil_node && sidebar->first == &cfg_nil_node) { cfg_node_release(rd_state->cfg, sidebar); }
  if(floating != &cfg_nil_node && floating->first == &cfg_nil_node) { cfg_node_release(rd_state->cfg, floating); }
  CFG_Node *inventory = cfg_node_child_from_string(owner, UISHELL_REGION_INVENTORY);
  if(inventory != &cfg_nil_node) { cfg_node_release(rd_state->cfg, inventory); }
  // Empty inventory makes every declared id new even if unrelated tabs keep
  // the host alive: reconcile_regions' legacy_saved test must see this node.
  // Avoid releasing absent nodes: cfg_node_release does no tree work for nil,
  // but still increments the configuration change generation.
  cfg_node_new(rd_state->cfg, owner, UISHELL_REGION_INVENTORY);
}

internal CFG_Node *
uishell_sidebar_region_view(CFG_Node *owner, String8 key)
{
  CFG_Node *view = uishell_sidebar_find_view(cfg_node_child_from_string(owner, RD_DOCK_SIDEBAR_ROOT), key);
  if(view == &cfg_nil_node)
  { view = uishell_sidebar_find_view(cfg_node_child_from_string(owner, str8_lit("floating_panels")), key); }
  return view;
}

internal CFG_Node *
uishell_sidebar_place_region(CFG_Node *owner, UIShell_SectionPlacement *regions, U64 count, U64 index, CFG_Node *view)
{
  UIShell_SectionPlacement region = regions[index];
  String8 host_name = str8_match(region.default_host, str8_lit("floating"), 0) ? str8_lit("floating_panels") : RD_DOCK_SIDEBAR_ROOT;
  // Check the prospective ancestry before mutating saved configuration. The
  // checker reads host/owner traits; allocation is only needed after acceptance.
  B32 valid = 0;
  for(U32 attempt = 0; attempt < 2; attempt++)
  {
    CFG_Node prospective_host = {.string = host_name, .parent = owner};
    CFG_Node prospective_panel = {.string = str8_lit("1"), .parent = &prospective_host};
    valid = view == &cfg_nil_node ? rd_dock_can_create(str8_lit("sidebar_section"), &prospective_panel) :
      rd_dock_drag_target(view, &prospective_panel, RD_DOCK_UNMEASURED_WIDTH);
    if(valid || str8_match(host_name, RD_DOCK_SIDEBAR_ROOT, 0)) { break; }
    host_name = RD_DOCK_SIDEBAR_ROOT;
  }
  if(!valid) { return view; }
  CFG_Node *host = cfg_node_child_from_string_or_alloc(rd_state->cfg, owner, host_name);
  CFG_Node *panel = cfg_node_new(rd_state->cfg, host, str8_lit("1"));
  if(view == &cfg_nil_node)
  {
    view = cfg_node_new(rd_state->cfg, panel, str8_lit("sidebar_section"));
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("section")), region.key);
    cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("label")), region.title);
    cfg_node_new(rd_state->cfg, view, str8_lit("selected"));
  }
  else
  {
    CFG_Node *old_panel = view->parent;
    cfg_node_insert_child(rd_state->cfg, panel, panel->last, view);
    uishell_sidebar_prune_empty_panel(old_panel);
  }
  // Insert beside the next hinted neighbour's whole subtree. Existing nested
  // splits keep their structure, tab selection, ratios and View identities.
  for(U64 next = index+1; next < count; next++)
  {
    CFG_Node *neighbour = uishell_sidebar_region_view(owner, regions[next].key);
    if(neighbour == &cfg_nil_node) { continue; }
    CFG_Node *anchor = neighbour->parent;
    while(anchor != &cfg_nil_node && anchor->parent != host) { anchor = anchor->parent; }
    if(anchor != &cfg_nil_node)
    { cfg_node_insert_child(rd_state->cfg, host, anchor->prev, panel); break; }
  }
  return view;
}

// Prefer the first valid saved View in sidebar-then-floating depth-first order.
// If none is valid, keep the first invalid copy for existing placement repair.
// Only this owner's hosts participate; child Workspace Regions are independent.
internal void
uishell_sidebar_choose_region(CFG_Node *container, String8 key, CFG_Node **keeper)
{
  for(CFG_Node *c = container->first; c != &cfg_nil_node; c = c->next)
  {
    if(str8_match(c->string, str8_lit("sidebar_section"), 0) &&
       str8_match(uishell_sidebar_section_key(c), key, 0))
    {
      if(*keeper == &cfg_nil_node ||
         (!rd_dock_saved_placement_valid(*keeper) && rd_dock_saved_placement_valid(c))) { *keeper = c; }
    }
    else if(rd_dock_is_container(c)) { uishell_sidebar_choose_region(c, key, keeper); }
  }
}

internal B32
uishell_sidebar_prune_region_duplicates(CFG_Node *container, String8 key, CFG_Node *keeper)
{
  B32 removed = 0;
  for(CFG_Node *c = container->first, *next; c != &cfg_nil_node; c = next)
  {
    next = c->next;
    if(str8_match(c->string, str8_lit("sidebar_section"), 0) && c != keeper &&
       str8_match(uishell_sidebar_section_key(c), key, 0))
    { cfg_node_release(rd_state->cfg, c); removed = 1; }
    else if(rd_dock_is_container(c))
    {
      B32 child_removed = uishell_sidebar_prune_region_duplicates(c, key, keeper);
      removed |= child_removed;
      // Authoritative prune_regions runs first and consumes saved
      // section_hint_cleanup markers before this duplicate-repair pass.
      removed |= uishell_sidebar_cleanup_region_panel(c, child_removed);
    }
  }
  return removed;
}

// An authoritative empty declaration removes stale Views and close records.
// Missing provider snapshots never reach this function and preserve layout.
internal CFG_Node *
uishell_sidebar_reconcile_regions(CFG_Node *owner, UIShell_SectionPlacement *regions, U64 count)
{
  CFG_Node *root = cfg_node_child_from_string(owner, RD_DOCK_SIDEBAR_ROOT);
  CFG_Node *inventory = cfg_node_child_from_string(owner, UISHELL_REGION_INVENTORY);
  B32 legacy_saved = inventory == &cfg_nil_node && root != &cfg_nil_node;
  if(inventory == &cfg_nil_node) { inventory = cfg_node_new(rd_state->cfg, owner, UISHELL_REGION_INVENTORY); }
  uishell_sidebar_prune_regions(owner, root, regions, count, 0);
  uishell_sidebar_prune_regions(owner, cfg_node_child_from_string(owner, str8_lit("floating_panels")), regions, count, 0);
  for(CFG_Node *c = inventory->first, *next; c != &cfg_nil_node; c = next)
  {
    next = c->next;
    if(uishell_sidebar_region_index(regions, count, c->string) == ANDAMENTO_NONE) { cfg_node_release(rd_state->cfg, c); }
  }
  for(U64 r = 0; r < count; r++)
  {
    String8 key = regions[r].key;
    CFG_Node *record = cfg_node_child_from_string(inventory, key);
    CFG_Node *view = &cfg_nil_node;
    CFG_Node *floating = cfg_node_child_from_string(owner, str8_lit("floating_panels"));
    uishell_sidebar_choose_region(root, key, &view);
    uishell_sidebar_choose_region(floating, key, &view);
    uishell_sidebar_prune_region_duplicates(root, key, view);
    uishell_sidebar_prune_region_duplicates(floating, key, view);
    B32 known = record != &cfg_nil_node;
    if(!known) { record = cfg_node_new(rd_state->cfg, inventory, key); }
    CFG_Node *hint_pending = cfg_node_child_from_string(view, str8_lit("section_hint_pending"));
    B32 invalid = view != &cfg_nil_node && (!rd_dock_saved_placement_valid(view) || hint_pending != &cfg_nil_node);
    if(invalid || (view == &cfg_nil_node && !known && !legacy_saved))
    {
      view = uishell_sidebar_place_region(owner, regions, count, r, view);
      if(hint_pending != &cfg_nil_node && rd_dock_saved_placement_valid(view))
      { cfg_node_release(rd_state->cfg, hint_pending); }
    }
    if(view != &cfg_nil_node)
    {
      // KDL owns titles; a future user rename needs a separate explicit override.
      CFG_Node *label = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("label"));
      if(!str8_match(label->first->string, regions[r].title, 0))
      { cfg_node_new_replace(rd_state->cfg, label, regions[r].title); }
    }
    CFG_Node *closed = cfg_node_child_from_string(record, str8_lit("closed"));
    if(view == &cfg_nil_node && closed == &cfg_nil_node) { cfg_node_new(rd_state->cfg, record, str8_lit("closed")); }
    if(view != &cfg_nil_node && closed != &cfg_nil_node) { cfg_node_release(rd_state->cfg, closed); }
  }
  return cfg_node_child_from_string(owner, RD_DOCK_SIDEBAR_ROOT);
}

internal CFG_Node *
uishell_sidebar_dock_layout(UIShell_ControlledSplit *split)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  if(!state->snapshot) { return cfg_node_child_from_string(split->owner_cfg, RD_DOCK_SIDEBAR_ROOT); }
  if(state->placement_snapshot != state->snapshot)
  {
    if(!state->placement_arena) { state->placement_arena = arena_alloc(); }
    else { arena_clear(state->placement_arena); }
    Arena *arena = state->placement_arena;
    U64 nodes = andamento_snapshot_node_count(state->snapshot), capacity = 0, count = 0;
    // Count first so retained storage scales with regions, not all entity rows.
    for(U64 i = 0; i < nodes; i++)
    {
      AndamentoNode node = {0};
      uishell_sidebar_node_at(state, i, &node);
      capacity += !!node.is_section;
    }
    UIShell_SectionPlacement *regions = capacity ? push_array(arena, UIShell_SectionPlacement, capacity) : 0;
    for(U64 i = 0; i < nodes; i++)
    {
      AndamentoNode node = {0};
      UIShell_SidebarRole role = uishell_sidebar_node_at(state, i, &node);
      if(!node.is_section || role == UIShell_SidebarRole_Container) { continue; }
      // Leftover tabs fill the default group; the leftover section is a
      // region only while something is left over.
      if(uishell_sidebar_leftover_hidden(state, i, node)) { continue; }
      // A section someone made takes its container region's hints.
      AndamentoRegionHints hints = {0};
      andamento_snapshot_region_hints(state->snapshot, role == UIShell_SidebarRole_LocalSection ? node.parent : i, &hints);
      UIShell_SectionPlacement region = {
        push_str8_copy(arena, uishell_sidebar_string(node.key)),
        uishell_sidebar_string(node.label),
        push_str8_copy(arena, uishell_sidebar_string(hints.default_host)),
        hints.has_order ? hints.order : (S64)count};
      // Andamento always emits the unhinted workspace fallback. By index it
      // would sort before every explicit order, so it goes last instead.
      if(!hints.has_order && uishell_sidebar_section_hosts_chrome(region.key)) { region.order = max_S64; }
      if(node.field_count)
      { AndamentoField field = {0}; andamento_snapshot_field(state->snapshot, node.first_field, &field); region.title = uishell_sidebar_string(field.text); }
      region.title = push_str8_copy(arena, region.title);
      U64 at = count;
      // Stable insertion: equal and absent hints retain declaration order.
      while(at && regions[at-1].order > region.order) { regions[at] = regions[at-1]; at--; }
      regions[at] = region; count++;
    }
    state->placement_snapshot = state->snapshot;
    state->placement_regions = regions;
    state->placement_count = count;
    state->placement_cache_builds++;
  }
  // Reconcile declared regions first, then deduplicate saved card identities.
  // Pins are independent Views and survive authoritative section removal.
  uishell_sidebar_pin_migrate(split->owner_cfg);
  CFG_Node *root = uishell_sidebar_reconcile_regions(split->owner_cfg, state->placement_regions, state->placement_count);
  if(state->pin_cfg_generation != cfg_change_gen())
  {
    uishell_sidebar_pin_deduplicate(split->owner_cfg);
    state->pin_cfg_generation = cfg_change_gen();
  }
  return root;
}

// Explicit restore uses this owner's live declarations and the same placement
// checker as initial creation. A saved View, including a floating one, wins.
internal B32
uishell_sidebar_restore_region(UIShell_ControlledSplit *split, String8 key)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  if(ws == &rd_nil_window_state || !ws->sidebar) { return 0; }
  uishell_sidebar_dock_layout(split);
  UIShell_SidebarState *state = ws->sidebar;
  if(!state->snapshot) { return 0; }
  U64 index = uishell_sidebar_region_index(state->placement_regions, state->placement_count, key);
  if(index == ANDAMENTO_NONE) { return 0; }
  CFG_Node *view = uishell_sidebar_region_view(split->owner_cfg, key);
  // Reconciliation above clears any stale closed record for an existing
  // View. Returning success adds no further placement mutation or duplicate.
  if(view != &cfg_nil_node) { return 1; }
  view = uishell_sidebar_place_region(split->owner_cfg, state->placement_regions, state->placement_count, index, &cfg_nil_node);
  if(view == &cfg_nil_node) { return 0; }
  CFG_Node *panel = view->parent, *host = panel->parent;
  // A merged host may be a leaf itself. Lift its tabs and leaf options
  // (tabs_on_bottom and panel selected) together. config_panels.c reads
  // these on the leaf; host axis and sizing settings live on owner_cfg.
  B32 split_host = 0;
  for(CFG_Node *n = host->first; n != &cfg_nil_node; n = n->next)
  { if(n != panel && rd_dock_is_container(n)) { split_host = 1; break; } }
  if(!split_host && (host->first != panel || panel->next != &cfg_nil_node))
  {
    CFG_Node *old = cfg_node_new(rd_state->cfg, host, str8_lit("1"));
    for(CFG_Node *n = host->first, *next; n != &cfg_nil_node; n = next)
    {
      next = n->next;
      if(n != old && n != panel) { cfg_node_insert_child(rd_state->cfg, old, old->last, n); }
    }
  }
  // Match ordinary sibling insertion: allocate one share to the restored
  // panel and scale saved sibling shares together, including nested splits.
  F32 total = 0; U64 count = 0;
  for(CFG_Node *n = host->first; n != &cfg_nil_node; n = n->next)
  {
    if(n != panel && rd_dock_is_container(n))
    { total += Max(.01f, (F32)f64_from_str8(n->string)); count++; }
  }
  F32 fraction = 1.f/(count+1);
  for(CFG_Node *n = host->first; n != &cfg_nil_node; n = n->next)
  {
    if(n != panel && rd_dock_is_container(n))
    {
      // Saved numeric tokens can be malformed or zero. Give each sibling
      // a positive weight before normalization so none becomes unreachable.
      F32 share = Max(.01f, (F32)f64_from_str8(n->string))/total;
      cfg_node_equip_stringf(rd_state->cfg, n, "%f", (1-fraction)*share);
    }
  }
  cfg_node_equip_stringf(rd_state->cfg, panel, "%f", fraction);
  CFG_Node *record = cfg_node_child_from_string(cfg_node_child_from_string(split->owner_cfg, UISHELL_REGION_INVENTORY), key);
  CFG_Node *closed = cfg_node_child_from_string(record, str8_lit("closed"));
  if(closed != &cfg_nil_node) { cfg_node_release(rd_state->cfg, closed); }
  rd_request_frame();
  return 1;
}

// Keep failed restoration visible through the same footer action used by UI.
internal B32
uishell_sidebar_restore_from_menu(UIShell_ControlledSplit *split, String8 key)
{
  if(uishell_sidebar_restore_region(split, key)) { ui_ctx_menu_close(); return 1; }
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  if(ws != &rd_nil_window_state && ws->sidebar)
  { uishell_sidebar_set_error(ws->sidebar, str8_lit("Section could not be restored")); }
  rd_request_frame(); return 0;
}

// A selected View in `panel` showing the section keyed `key`.
internal CFG_Node *
uishell_sidebar_section_view_here(CFG_Node *panel, String8 key, String8 label)
{
  for(CFG_Node *v = panel->first; v != &cfg_nil_node; v = v->next)
  { cfg_node_release(rd_state->cfg, cfg_node_child_from_string(v, str8_lit("selected"))); }
  CFG_Node *view = cfg_node_new(rd_state->cfg, panel, str8_lit("sidebar_section"));
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("section")), key);
  cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, str8_lit("label")), label);
  cfg_node_new(rd_state->cfg, view, str8_lit("selected"));
  return view;
}

// Sections… → New section (sidebar-headers.md): an empty section of its own
// at the sidebar's end, its name field open. A sidebar that is one panel
// takes it as a View beside its others.
internal CFG_Node *
uishell_sidebar_new_section(UIShell_SidebarState *state, CFG_Node *window)
{
  CFG_Node *host = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
  if(host == &cfg_nil_node) { return &cfg_nil_node; }
  B32 split = 0;
  for(CFG_Node *c = host->first; c != &cfg_nil_node; c = c->next)
  {
    MD_TokenizeResult tokens = md_tokenize_from_text(ui_build_arena(), c->string);
    split |= tokens.tokens.count == 1 && tokens.tokens.v[0].flags & MD_TokenFlag_Numeric;
  }
  CFG_Node *panel = split ? cfg_node_new(rd_state->cfg, host, str8_lit("0.25")) : host;
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *group = uishell_sidebar_local_new_group(window, str8_lit("New section"));
  String8 title = uishell_sidebar_local_title(scratch.arena, group->parent);
  CFG_Node *view = uishell_sidebar_section_view_here(panel, uishell_sidebar_local_key(scratch.arena, uishell_sidebar_local_field(group->parent, str8_lit("id"))), title);
  uishell_sidebar_local_begin_rename(state, group->parent, title);
  scratch_end(scratch);
  return view;
}

// Dragging opts into saved ratios. Double-clicking a sidebar boundary or an
// explicit panel reset returns the window to content-based default sizing.
internal void
uishell_sidebar_manual_sizing(CFG_Node *window, B32 manual)
{
  CFG_Node *flag = cfg_node_child_from_string(window, str8_lit("sidebar_layout_sized"));
  if(manual)
  { cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("sidebar_layout_sized")); }
  else if(flag != &cfg_nil_node) { cfg_node_release(rd_state->cfg, flag); }
}

// Default stacked panels keep secondary sections bounded by their content;
// the first expanded section receives the remaining space. Manual panel sizing
// uses the saved split ratios, including nested and horizontal arrangements.
// Whether `panel` shows only collapsed section headers: a leaf whose selected
// tab is a collapsed section, or a split whose children all do. If so,
// *height is what it needs: its headers (and a tab strip where several tabs
// share a leaf) within the panel insets.
// A sidebar row's height, and a collapsed section header's: what layout
// and the boundaries beside collapsed sections agree on.
internal F32
uishell_sidebar_row_height(void)
{
  return floor_f32(ui_top_font_size()*2.2f);
}

internal B32
uishell_sidebar_panel_collapsed(CFG_PanelNode *panel, F32 header, F32 *height)
{
  F32 inset = 2*rd_panel_inset_px(ui_top_font_size());
  if(panel->first == &cfg_nil_panel_node)
  {
    // An empty leaf has nothing to show: it gives all its space away.
    if(panel->tabs.count == 0) { *height = 0; return 1; }
    CFG_Node *view = panel->selected_tab;
    *height = header + inset + (panel->tabs.count > 1 ? header : 0.f);
    return str8_match(view->string, str8_lit("sidebar_section"), 0) && uishell_sidebar_section_collapsed(view);
  }
  F32 most = 0, sum = 0;
  for(CFG_PanelNode *child = panel->first; child != &cfg_nil_panel_node; child = child->next)
  {
    F32 h = 0;
    if(!uishell_sidebar_panel_collapsed(child, header, &h)) { return 0; }
    most = Max(most, h); sum += h;
  }
  *height = panel->split_axis == Axis2_Y ? sum : most;
  return 1;
}

// Collapsed sections give their space back (sidebar-headers.md): along a
// vertical split, a collapsed child keeps only what its headers need and the
// rest goes to the next open child below it (or above, at the end), so
// expanding it again is one change to one neighbour. Once all are
// collapsed, the last takes what's left. A side-by-side split shrinks only
// once all of it is collapsed. In memory only, so saved sizes, set by hand
// or not, return when a section expands.
internal void
uishell_sidebar_collapse_space(CFG_PanelNode *panel, F32 extent, F32 header)
{
  if(panel->first == &cfg_nil_panel_node) { return; }
  if(panel->split_axis == Axis2_Y && extent > 0)
  {
    Temp scratch = scratch_begin(0, 0);
    U64 count = 0;
    for(CFG_PanelNode *child = panel->first; child != &cfg_nil_panel_node; child = child->next) { count++; }
    CFG_PanelNode **children = push_array(scratch.arena, CFG_PanelNode *, count);
    B32 *collapsed = push_array(scratch.arena, B32, count);
    F32 *pct = push_array(scratch.arena, F32, count);
    // Saved sizes needn't sum to one; their shares do.
    F32 total = 0;
    for(CFG_PanelNode *child = panel->first; child != &cfg_nil_panel_node; child = child->next) { total += Max(0.f, child->pct_of_parent); }
    if(total <= 0) { total = 1; }
    F32 *saved = push_array(scratch.arena, F32, count);
    U64 i = 0, open = 0;
    for(CFG_PanelNode *child = panel->first; child != &cfg_nil_panel_node; child = child->next, i++)
    {
      F32 h = 0;
      children[i] = child;
      collapsed[i] = uishell_sidebar_panel_collapsed(child, header, &h);
      saved[i] = Max(0.f, child->pct_of_parent)/total;
      pct[i] = collapsed[i] ? h/extent : saved[i];
      open += !collapsed[i];
    }
    for(i = 0; i < count; i++)
    {
      if(!collapsed[i]) { continue; }
      F32 freed = saved[i] - pct[i];
      U64 to = count;
      for(U64 j = i+1; to == count && j < count; j++) { if(!collapsed[j]) { to = j; } }
      for(U64 j = i; to == count && j > 0; j--) { if(!collapsed[j-1]) { to = j-1; } }
      if(to < count) { pct[to] += freed; }
    }
    if(!open)
    {
      F32 rest = 1.f;
      for(i = 0; i+1 < count; i++) { rest -= pct[i]; }
      pct[count-1] = Max(pct[count-1], rest);
    }
    // Always exactly the space there is, even when headers alone overfill it.
    F32 sum = 0;
    for(i = 0; i < count; i++) { pct[i] = Max(0.f, pct[i]); sum += pct[i]; }
    for(i = 0; i < count; i++) { children[i]->pct_of_parent = sum > 0 ? pct[i]/sum : 1.f/count; }
    scratch_end(scratch);
  }
  for(CFG_PanelNode *child = panel->first; child != &cfg_nil_panel_node; child = child->next)
  { uishell_sidebar_collapse_space(child, panel->split_axis == Axis2_Y ? extent*child->pct_of_parent : extent, header); }
}

internal void uishell_sidebar_size_panels_saved(UIShell_ControlledSplit *split, UIShell_WorkspaceMount *mount, Rng2F32 rect);

internal void
uishell_sidebar_size_panels(UIShell_ControlledSplit *split, UIShell_WorkspaceMount *mount, Rng2F32 rect)
{
  uishell_sidebar_size_panels_saved(split, mount, rect);
  uishell_sidebar_collapse_space(mount->panel_tree.root, dim_2f32(rect).y, uishell_sidebar_row_height());
}

// The sizes to lay out from before collapse: manual (saved, repaired) or
// automatic (from content), written to the config.
internal void
uishell_sidebar_size_panels_saved(UIShell_ControlledSplit *split, UIShell_WorkspaceMount *mount, Rng2F32 rect)
{
  // A boundary drag in flight is manual sizing, saved when it ends.
  B32 manual = mount->resizing || cfg_node_child_from_string(split->owner_cfg, str8_lit("sidebar_layout_sized")) != &cfg_nil_node;
  CFG_PanelNode *root = mount->panel_tree.root;
  if(root->split_axis != Axis2_Y || root->child_count == 0) { return; }
  U64 count = root->child_count;
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *state = rd_window_state_from_cfg__existing(split->owner_cfg)->sidebar;
  F32 row_height = floor_f32(ui_top_font_size()*2.2f);
  if(manual)
  {
    // Repair undersized saved leaves in a simple vertical stack. Keep all
    // valid siblings proportional while reserving a visible header per leaf.
    F32 minimum = Min(1.f/Max(1, count), (row_height+2*rd_panel_inset_px(ui_top_font_size()))/Max(1.f, dim_2f32(rect).y));
    F32 total = 0; B32 repair = 0;
    for(CFG_PanelNode *panel = root->first; panel != &cfg_nil_panel_node; panel = panel->next)
    {
      if(panel->first != &cfg_nil_panel_node || panel->tabs.count != 1) { scratch_end(scratch); return; }
      total += Max(0.f, panel->pct_of_parent);
      // Config shares are serialized with six decimals. Ignore sub-pixel
      // roundtrip differences so repaired layouts stay idle on later frames.
      repair |= panel->pct_of_parent < minimum-.000001f;
    }
    repair |= abs_f32(total-1.f) > .0001f;
    if(!repair) { scratch_end(scratch); return; }
    B32 *fixed = push_array(scratch.arena, B32, count);
    F32 remaining = 1, weight = total; U64 unfixed = count;
    // Each fixed-point pass recomputes the scale from remaining weights: a
    // newly clamped leaf can push another proportional share below minimum.
    for(U64 pass = 0; pass < count; pass++)
    {
      B32 changed = 0; U64 i = 0;
      F32 scale = weight > 0 ? remaining/weight : 0;
      F32 equal = remaining/Max(1, unfixed);
      for(CFG_PanelNode *panel = root->first; panel != &cfg_nil_panel_node; panel = panel->next, i++)
      {
        F32 share = weight > 0 ? Max(0.f, panel->pct_of_parent)*scale : equal;
        if(!fixed[i] && share < minimum)
        { fixed[i] = 1; remaining -= minimum; weight -= Max(0.f, panel->pct_of_parent); unfixed--; changed = 1; }
      }
      if(!changed) { break; }
    }
    U64 i = 0;
    for(CFG_PanelNode *panel = root->first; panel != &cfg_nil_panel_node; panel = panel->next, i++)
    {
      F32 share = fixed[i] ? minimum : weight > 0 ? Max(0.f, panel->pct_of_parent)*remaining/weight : remaining/Max(1, unfixed);
      panel->pct_of_parent = share;
      if(!mount->resizing) { cfg_node_equip_stringf(rd_state->cfg, panel->cfg, "%f", share); }
    }
    scratch_end(scratch); return;
  }
  F32 *heights = push_array(scratch.arena, F32, count);
  F32 *contents = push_array(scratch.arena, F32, count);
  F32 *headers = push_array(scratch.arena, F32, count);
  B32 *collapsed = push_array(scratch.arena, B32, count);
  U64 flexible = count, n = 0;
  F32 available = dim_2f32(rect).y;
  for(CFG_PanelNode *panel = root->first; panel != &cfg_nil_panel_node; panel = panel->next, n++)
  {
    if(panel->first != &cfg_nil_panel_node || panel->tabs.count != 1) { scratch_end(scratch); return; }
    CFG_Node *view = panel->tabs.first->v;
    String8 key = cfg_node_child_from_string(view, str8_lit("section"))->first->string;
    collapsed[n] = uishell_sidebar_section_collapsed(view);
    UIShell_SidebarSection *section = state->sections;
    for(; section; section = section->next)
    {
      if(str8_match(section->key, key, 0)) { break; }
    }
    contents[n] = section ? section->content_height : 7*row_height;
    // A placed section still needs its title, movement and close controls
    // when it has no content. Closing its View is the explicit way to hide it.
    headers[n] = row_height;
    available -= headers[n];
    if(!collapsed[n] && contents[n] > 0 && flexible == count) { flexible = n; }
  }
  F32 remaining = Max(0.f, available);
  for(U64 i = 0; i < count; i++)
  {
    if(i == flexible || collapsed[i]) { continue; }
    heights[i] = Min(Min(contents[i], 7*row_height), remaining*0.4f);
    remaining -= heights[i];
  }
  if(flexible < count) { heights[flexible] = remaining; }
  else if(count) { heights[count-1] = remaining; }
  n = 0;
  for(CFG_PanelNode *panel = root->first; panel != &cfg_nil_panel_node; panel = panel->next, n++)
  {
    panel->pct_of_parent = (headers[n]+heights[n])/Max(1.f, dim_2f32(rect).y);
    F32 saved = (F32)f64_from_str8(panel->cfg->string);
    if(abs_f32(saved-panel->pct_of_parent) > 0.0001f)
    { cfg_node_equip_stringf(rd_state->cfg, panel->cfg, "%f", panel->pct_of_parent); }
  }
  scratch_end(scratch);
}

internal F32
uishell_sidebar_footer_height(RD_WindowState *ws)
{
  return floor_f32(ui_top_font_size()*2.2f);
}

internal void
uishell_sidebar_footer_ui(Rng2F32 rect, UIShell_ControlledSplit *split)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  if(ws == &rd_nil_window_state || !ws->sidebar) { return; }
  UIShell_SidebarState *state = ws->sidebar;
  F32 row_height = floor_f32(ui_top_font_size()*2.2f);
  Vec2F32 dim = dim_2f32(rect);
  AndamentoText diagnostic = {0};
  if(state->snapshot && andamento_snapshot_diagnostic_count(state->snapshot))
  { andamento_snapshot_diagnostic(state->snapshot, 0, &diagnostic); }
  UI_Box *root;
  UI_Focus(UI_FocusKind_On) UI_Rect(rect)
  { root = ui_build_box_from_string(UI_BoxFlag_Clip|UI_BoxFlag_DefaultFocusNav, str8_lit("###sidebar_footer")); }
  UI_Parent(root)
  {
    // One row: Sections…, any notice, and the sidebar's chrome actions
    // (ADR-0006: New workspace while Workspaces isn't showing it, and what
    // the title bar has no room for) at its trailing end.
    UI_Box *notice;
    UI_Rect(r2f32p(0, 0, dim.x, dim.y)) UI_ChildLayoutAxis(Axis2_X)
    { notice = ui_build_box_from_string(UI_BoxFlag_DrawSideTop, str8_lit("###sidebar_notice")); }
    // Only the notice uses that absolute rectangle. Its controls and popup
    // must use their own layout, otherwise they inherit a clipped hit area.
    UI_Parent(notice) UI_PrefHeight(ui_pct(1, 1))
    {
      String8 message = state->error[0] ? str8_cstring((char *)state->error) : state->inspection[0] ? str8_cstring((char *)state->inspection) : uishell_sidebar_string(diagnostic);
      // The footer survives an empty control_views tree. Section restoration
      // must remain reachable after closing the last selector section too.
      UI_Key menu_key = ui_key_from_string(root->key, str8_lit("section_restore_menu"));
      F32 menu_width = ui_top_font_size()*24.f;
      if(ui_ctx_menu_is_open(menu_key))
      {
        for(U64 i = 0; state->snapshot && i < state->placement_count; i++)
        {
          UIShell_SectionPlacement region = state->placement_regions[i];
          if(uishell_sidebar_region_view(split->owner_cfg, region.key) != &cfg_nil_node) { continue; }
          String8 text = push_str8f(ui_build_arena(), "Restore %S", region.title);
          menu_width = Max(menu_width, fnt_dim_from_tag_size_string(ui_top_font(), ui_top_font_size(), 0, 0, text).x+2*ui_top_text_padding());
        }
        menu_width = Min(menu_width, dim_2f32(wm_client_rect_from_window(ws->os)).x);
      }
      // Do not wrap UI_CtxMenu in UI_Rect: fixed layout stacks would leak
      // the notice rectangle into its popup rows and clip their hit areas.
      UI_CtxMenu(menu_key) UI_PrefWidth(ui_px(menu_width, 1)) UI_PrefHeight(ui_px(row_height, 1))
      {
        if(ui_clicked(uishell_sidebar_button(str8_lit("New section###section_new"))))
        { uishell_sidebar_new_section(state, split->owner_cfg); ui_ctx_menu_close(); }
        B32 available = 0;
        for(U64 i = 0; state->snapshot && i < state->placement_count; i++)
        {
          UIShell_SectionPlacement region = state->placement_regions[i];
          if(uishell_sidebar_region_view(split->owner_cfg, region.key) != &cfg_nil_node) { continue; }
          available = 1;
          UI_Signal restore = uishell_sidebar_button(push_str8f(ui_build_arena(), "Restore %S###restore_%S", region.title, region.key));
          if(ui_clicked(restore))
          {
            uishell_sidebar_restore_from_menu(split, region.key);
          }
        }
        if(!available) UI_TagF("weak") { ui_label(state->snapshot ? str8_lit("All sections are open") : str8_lit("Sections unavailable")); }
      }
      UI_PrefWidth(ui_text_dim(8, 1))
      {
        UI_Signal sections = uishell_sidebar_button(str8_lit("Sections…###section_restore"));
        if(ui_clicked(sections))
        {
          ui_ctx_menu_open_above(menu_key, sections.box->key);
          rd_request_frame();
        }
      }
      UI_PrefWidth(ui_pct(1, 0)) { ui_label(message); }
      UI_PrefWidth(ui_em(1.5f, 1))
      {
        if((state->error[0] || state->inspection[0]) && ui_clicked(uishell_sidebar_button(str8_lit("×###sidebar_dismiss"))))
        { state->inspection[0] = state->error[0] = 0; rd_request_frame(); }
      }
      UI_PrefWidth(ui_em(2.25f, 1))
      {
        if(ws->chrome_niche[RD_ChromeElementKind_NewWorkspace] == RD_ChromeNiche_SidebarActions) { rd_chrome_build_new_workspace(split->owner_cfg); }
        if(ws->chrome_niche[RD_ChromeElementKind_RevealWorkspace] == RD_ChromeNiche_SidebarActions) { rd_chrome_build_reveal_workspace(split->owner_cfg); }
        if(ws->chrome_niche[RD_ChromeElementKind_OverviewToggle] == RD_ChromeNiche_SidebarActions) { rd_chrome_build_overview_toggle(ws); }
      }
    }
  }
}

// Exercises the native host adapter, not just andamento's own C fixture.
// Exercise disclosure geometry with the same font/padding as the shell.
internal B32
uishell_sidebar_disclosure_diagnostics(RD_WindowState *ws)
{
  UI_State *saved = ui_state;
  UI_State *test_ui = ui_state_alloc();
  ui_select_state(test_ui);
  UI_IconInfo icons = ws->ui->icon_info;
  UI_AnimationInfo animation = {0};
  UI_EventList events = {0};
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  UI_Box *buttons[2] = {0};
  UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
  UI_PrefWidth(ui_em(1.2f, 1)) UI_PrefHeight(ui_em(1.65f, 1))
  {
    buttons[0] = uishell_sidebar_disclosure(1, str8_lit("###disclosure_open")).box;
    buttons[1] = uishell_sidebar_disclosure(0, str8_lit("###disclosure_closed")).box;
  }
  ui_end_build();
  B32 ok = 1;
  for(U64 i = 0; i < 2; i++)
  {
    F32 available = buttons[i]->rect.x1-ui_box_text_position(buttons[i]).x;
    F32 text_width = buttons[i]->display_fruns.dim.x;
    if(text_width <= 0 || text_width > available) { fprintf(stderr, "FAIL disclosure %llu: text=%g available=%g\n", i, text_width, available); }
    ok = ok && text_width > 0 && text_width <= available;
  }
  ui_select_state(saved);
  ui_state_release(test_ui);
  return ok;
}

// Exercise the production sidebar builder across deterministic animation frames.
internal B32
uishell_sidebar_motion_diagnostics(RD_WindowState *ws, UIShell_ControlledSplit *split,
                                   U64 project_index, CFG_ID workspace)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  // The normal host fixture folds its vessels into inline header buttons.
  // Use those same live entities as child rows for the motion/input exercise.
  String8 config = str8_cstring((char *)uishell_sidebar_fixture_config);
  String8 inline_layout = str8_lit(" layout=\"inline\"");
  U64 at = str8_find_needle(config, 0, inline_layout, 0);
  String8 tree_config = push_str8f(scratch.arena, "%S%S", str8_prefix(config, at), str8_skip(config, at+inline_layout.size));
  char *config_error = 0;
  B32 configured = andamento_configure(state->core, uishell_sidebar_text(tree_config), &config_error);
  if(!uishell_sidebar_result(state, configured, config_error)) { scratch_end(scratch); return 0; }
  uishell_sidebar_refresh(state);
  AndamentoNode project = {0}, section = {0};
  uishell_sidebar_snapshot_node(state->snapshot, project_index, &project);
  uishell_sidebar_snapshot_node(state->snapshot, project.parent, &section);
  String8 project_key = push_str8_copy(scratch.arena, uishell_sidebar_string(project.key));
  String8 section_key = push_str8_copy(scratch.arena, uishell_sidebar_string(section.key));
  UI_State *saved_ui = ui_state, *test_ui = ui_state_alloc();
  F32 saved_rate = rd_state->menu_animation_rate;
  ui_select_state(test_ui);
  F32 child_heights[8] = {0}, group_heights[8] = {0}, content_heights[8] = {0};
  B32 ok = 1;
  for(U64 frame = 0; frame < ArrayCount(child_heights); frame++)
  {
    // Start open, close for two frames, reverse, Reveal, then check no-animation.
    if(frame == 1 || frame == 3 || frame == 6 || frame == 7)
    {
      uishell_sidebar_snapshot_node(state->snapshot, project_index, &project);
      char *error = 0;
      B32 dispatched = uishell_sidebar_dispatch(state, project.toggle, &error);
      ok &= uishell_sidebar_result(state, dispatched, error);
      uishell_sidebar_refresh(state);
    }
    if(frame == 5) { state->reveal_workspace_id = workspace; }
    rd_state->menu_animation_rate = frame >= 6 ? 1.f : 0.5f;
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    UI_Key root_key = ui_key_from_string(ui_active_seed_key(), str8_lit("###andamento_sidebar"));
    UI_Key body_key = ui_key_from_stringf(root_key, "section_body_%S", section_key);
    UI_Key group_key = ui_key_from_stringf(body_key, "###project_%S", project_key);
    UI_Key children_key = ui_key_from_stringf(group_key, "###project_children_%S", project_key);
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_sidebar_ui(r2f32p(0, 0, 320, 240), split); }
    ui_end_build();
    UI_Box *children = ui_box_from_key(children_key);
    UI_Box *group = ui_box_from_key(group_key);
    UI_Box *body = ui_box_from_key(body_key);
    if(ui_box_is_nil(children) || ui_box_is_nil(group) || ui_box_is_nil(body))
    { ok = 0; fprintf(stderr, "FAIL project motion: missing layout boxes at frame %llu\n", frame); break; }
    child_heights[frame] = dim_2f32(children->rect).y;
    group_heights[frame] = dim_2f32(group->rect).y;
    content_heights[frame] = body->view_bounds.y;
    if(frame == 0 && group->rect.y0-body->rect.y0 < 2.f)
    {
      ok = 0;
      fprintf(stderr, "FAIL project border: first container has only %g points above its stroke before the scroll clip\n", group->rect.y0-body->rect.y0);
    }
    if(frame == 2)
    {
      ok &= !!(children->flags & UI_BoxFlag_Clip);
      ok &= !ui_box_is_nil(children->first) && dim_2f32(children->first->rect).y == floor_f32(11.f*2.2f);
      U64 disabled_buttons = 0;
      for(UI_Box *box = children->first; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, children).next)
      {
        if(box->flags & UI_BoxFlag_Clickable)
        {
          disabled_buttons++;
          ok &= !!(box->flags & UI_BoxFlag_Disabled) && !!(box->flags & UI_BoxFlag_IgnoreInteraction);
          UI_Event press = {0};
          press.kind = UI_EventKind_Press;
          press.key = WM_Key_LeftMouseButton;
          press.pos = v2f32((box->rect.x0+box->rect.x1)*0.5f, box->rect.y0+0.5f);
          ui_event_list_push(ui_build_arena(), &events, &press);
          ok &= ui_signal_from_box(box).f == 0;
        }
      }
      ok &= disabled_buttons > 0;
    }
  }
  ok &= child_heights[0] > 0 && child_heights[2] > 0 && child_heights[2] < child_heights[0];
  ok &= child_heights[3] > 0 && child_heights[3] < child_heights[2] && child_heights[4] > child_heights[3];
  ok &= child_heights[5] == child_heights[0] && child_heights[6] == 0 && child_heights[7] == child_heights[0];
  // The header does not shrink, and the section's scroll extent follows the clip.
  ok &= abs_f32((group_heights[0]-child_heights[0])-(group_heights[2]-child_heights[2])) < 1.f;
  ok &= abs_f32((content_heights[0]-content_heights[2])-(child_heights[0]-child_heights[2])) < 1.f;
  if(!ok)
  {
    fprintf(stderr, "FAIL project motion: child heights");
    for(U64 i = 0; i < ArrayCount(child_heights); i++) { fprintf(stderr, " %g", child_heights[i]); }
    fprintf(stderr, "\n");
  }
  state->reveal_workspace_id = 0;
  rd_state->menu_animation_rate = saved_rate;
  ui_select_state(saved_ui);
  ui_state_release(test_ui);
  config_error = 0;
  configured = andamento_configure(state->core, uishell_sidebar_text(config), &config_error);
  ok &= uishell_sidebar_result(state, configured, config_error);
  uishell_sidebar_refresh(state);
  scratch_end(scratch);
  return ok;
}

// Real section bodies keep pixel fractions independently, across idle frames.
internal B32
uishell_sidebar_scroll_diagnostics(RD_WindowState *ws, UIShell_ControlledSplit *split)
{
  UI_State *saved = ui_state, *test = ui_state_alloc();
  ui_select_state(test);
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  UI_Key keys[2] = {0};
  F32 expected[2] = {0};
  U32 found = 0, failures = 0;
  for(U32 frame = 0; frame < 36; frame++)
  {
    UI_IconInfo icons = ws->ui->icon_info;
    UI_AnimationInfo animation = {0};
    animation.scroll_animation_rate = 0.5f;
    UI_EventNode node = {0};
    UI_EventList events = {0};
    if(frame >= 4 && frame < 20 && found == 2)
    {
      U32 section = (frame/4)%2;
      UI_Box *body = ui_box_from_key(keys[section]);
      F32 delta = frame < 12 ? 0.25f : -0.125f;
      node.v = (UI_Event){.kind = UI_EventKind_Scroll, .pos = center_2f32(body->rect),
                         .delta_2f32 = {0, delta}, .scroll_is_precise = 1};
      events.first = events.last = &node; events.count = 1;
      expected[section] += delta;
    }
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    ui_state->mouse = node.v.pos;
    UI_Key root_key = ui_key_from_string(ui_active_seed_key(), str8_lit("###andamento_sidebar"));
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_sidebar_ui(r2f32p(0, 0, 320, 180), split); }
    ui_end_build();
    if(frame == 3)
    {
      for(UIShell_SidebarSection *section = state->sections; section && found < 2; section = section->next)
      {
        UI_Key key = ui_key_from_stringf(root_key, "section_body_%S", section->key);
        UI_Box *body = ui_box_from_key(key);
        if(!ui_box_is_nil(body) && dim_2f32(body->rect).y > 2 && body->view_bounds.y > body->fixed_size.y+2)
        { keys[found++] = key; }
      }
      if(found != 2) { fprintf(stderr, "FAIL sidebar scroll fixture: expected two overflowing sections, got %u\n", found); failures++; break; }
    }
    if(frame >= 4)
    {
      if(events.count != 0) { failures++; }
      for(U32 section = 0; section < found; section++)
      {
        UI_Box *body = ui_box_from_key(keys[section]);
        if(abs_f32(body->view_off_target.y-expected[section]) > 0.00001f)
        { fprintf(stderr, "FAIL sidebar fraction frame %u section %u: %g != %g\n", frame, section, body->view_off_target.y, expected[section]); failures++; }
        if(frame == 35 && abs_f32(body->view_off.y-expected[section]) > 0.01f) { failures++; }
      }
    }
  }
  ui_select_state(saved);
  ui_state_release(test);
  fprintf(stderr, "Sidebar precise scroll diagnostics: %u failures\n", failures);
  return failures == 0;
}

#include "uishell/uishell_git_diagnostics.c"
#include "uishell/uishell_chip_diagnostics.c"

#include "uishell/uishell_sidebar_selection_diagnostics.c"
#include "uishell/uishell_sidebar_reorder_diagnostics.c"

internal B32
uishell_sidebar_diagnostics(CFG_Node *window)
{
  if(!uishell_sidebar_labels_diagnostics()) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  if(!uishell_sidebar_disclosure_diagnostics(ws)) { scratch_end(scratch); return 0; }
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  if(state->core == 0 || state->snapshot == 0)
  {
    fprintf(stderr, "Sidebar host diagnostics: FAILED (initialization: %s)\n", state->error);
    scratch_end(scratch);
    return 0;
  }
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  B32 selection_ok = uishell_sidebar_selection_diagnostics(ws, &split);
  B32 reorder_ok = uishell_sidebar_reorder_diagnostics(ws, &split);
  B32 coverage_ok = uishell_sidebar_coverage_diagnostics(state, &split);
  U64 before = split.inventory.count;
  size_t activate = ANDAMENTO_NONE;
  for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    if(uishell_sidebar_snapshot_node(state->snapshot, i, &node) &&
       str8_match(uishell_sidebar_string(node.entity_id), str8_lit("multi"), 0))
    { activate = node.activate; break; }
  }
  char *error = 0;
  B32 ok = state->core != 0 && activate != ANDAMENTO_NONE;
  // Terminal ended markers win over pending opening or live state. Exercise
  // the exact shared renderer helper, including ordinary non-ended markers.
  struct { U32 state; char *status; char *mark; } marks[] = {
    {ANDAMENTO_OPENING, "ended", "×"}, {ANDAMENTO_LIVE, "ended", "×"},
    {ANDAMENTO_LATENT, "ended", "×"}, {ANDAMENTO_OPENING, "failed", "…"},
    {ANDAMENTO_LIVE, "failed", "!"}, {ANDAMENTO_LATENT, "waiting", "◷"},
    {ANDAMENTO_LIVE, "", "•"}, {ANDAMENTO_LATENT, "", ""},
  };
  B32 markers_ok = 1;
  for(U64 i = 0; i < ArrayCount(marks); i++)
  {
    AndamentoNode marker_node = {0}; marker_node.state = marks[i].state;
    markers_ok = markers_ok && str8_match(uishell_sidebar_status_mark(marker_node, str8_cstring(marks[i].status)),
                          str8_cstring(marks[i].mark), 0);
  }
  if(ok) { ok = uishell_sidebar_dispatch(state, activate, &error); }
  ok = uishell_sidebar_result(state, ok, error);
  if(ok)
  {
    uishell_sidebar_effects(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    CFG_ID created = ws->root_controlled_split_selected_workspace_id;
    CFG_Node *workspace = cfg_node_from_id(created);
    ok = split.inventory.count == before+1 && workspace != &cfg_nil_node &&
         cfg_node_child_from_string(workspace, str8_lit("sidebar_entity_id")) != &cfg_nil_node;
    CFG_Node *panels = cfg_node_child_from_string(workspace, str8_lit("panels"));
    CFG_PanelTree tree = rd_panel_tree_from_cfg(scratch.arena, panels);
    ok = ok && cfg_node_child_from_string(workspace, str8_lit("split_x")) != &cfg_nil_node &&
         tree.root->child_count == 2 && tree.root->first->tabs.count == 1 && tree.root->last->tabs.count == 2;
    CFG_Node *tools_tab = tree.root->last->tabs.last->v;
    CFG_ID tools_id = tools_tab->id;
    // Simulate selecting the second overflow tab; refocusing must preserve it.
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(tree.root->last->selected_tab, str8_lit("selected")));
    cfg_node_new(rd_state->cfg, tools_tab, str8_lit("selected"));
    uishell_sidebar_observe(state, &split);
    B32 live = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
      if(node.workspace_id == created && node.state == ANDAMENTO_LIVE)
      { live = 1; activate = node.activate; }
    }
    ok = ok && live;
    // A second activation focuses the same workspace without creating another.
    error = 0;
    ok = ok && uishell_sidebar_dispatch(state, activate, &error);
    uishell_sidebar_result(state, ok, error);
    uishell_sidebar_effects(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    ok = ok && split.inventory.count == before+1 && ws->root_controlled_split_selected_workspace_id == created;
    tree = rd_panel_tree_from_cfg(scratch.arena, panels);
    ok = ok && tree.root->last->selected_tab->id == tools_id;
    uishell_sidebar_refresh(state);
    // Reveal prefers the nested tree occurrence over Attention, expands its
    // ancestor through fresh snapshot actions, and emits no host effects.
    U64 reveal_target = uishell_sidebar_reveal_target(state, created);
    AndamentoNode reveal_node = {0}, reveal_parent = {0};
    ok = ok && reveal_target != ANDAMENTO_NONE &&
      uishell_sidebar_snapshot_node(state->snapshot, reveal_target, &reveal_node) &&
      uishell_sidebar_snapshot_node(state->snapshot, reveal_node.parent, &reveal_parent) &&
      str8_match(uishell_sidebar_string(reveal_parent.entity_kind), str8_lit("project"), 0);
    String8 leaf = str8_zero();
    String8 path = uishell_sidebar_workspace_path(scratch.arena, state, created, str8_lit("Local fallback"), &leaf);
    ok = ok && str8_match(path, str8_lit("Example project  ›  Example workspace"), 0) &&
         str8_match(leaf, str8_lit("Example workspace"), 0);
    // The same placement is chosen while its ancestor is collapsed, and an
    // unplaced workspace remains named without borrowing another path.
    String8 unplaced = uishell_sidebar_workspace_path(scratch.arena, state, max_U64, str8_lit("Local fallback"), &leaf);
    ok = ok && str8_match(unplaced, str8_lit("Local fallback"), 0) &&
         str8_match(leaf, str8_lit("Local fallback"), 0);
    error = 0;
    ok = ok && uishell_sidebar_dispatch(state, reveal_parent.toggle, &error);
    uishell_sidebar_result(state, ok, error);
    uishell_sidebar_refresh(state);
    path = uishell_sidebar_workspace_path(scratch.arena, state, created, str8_lit("Local fallback"), 0);
    ok = ok && str8_match(path, str8_lit("Example project  ›  Example workspace"), 0);
    state->reveal_workspace_id = created;
    uishell_sidebar_expand_reveal(state);
    reveal_target = uishell_sidebar_reveal_target(state, created);
    ok = ok && uishell_sidebar_snapshot_node(state->snapshot, reveal_target, &reveal_node) &&
      uishell_sidebar_snapshot_node(state->snapshot, reveal_node.parent, &reveal_parent) && !reveal_parent.collapsed;
    AndamentoEffects *reveal_effects = andamento_effects_take(state->core, 0);
    ok = ok && reveal_effects && andamento_effects_count(reveal_effects) == 0;
    andamento_effects_release(reveal_effects);
    state->reveal_workspace_id = 0;
    ok = ok && uishell_sidebar_motion_diagnostics(ws, &split, reveal_node.parent, created);
    // Reveal must open a docked View's saved collapse state, not just the
    // aggregate renderer's transient section state.
    reveal_target = uishell_sidebar_reveal_target(state, created);
    AndamentoNode reveal_section_node = {0};
    uishell_sidebar_snapshot_node(state->snapshot, reveal_target, &reveal_section_node);
    while(!reveal_section_node.is_section && reveal_section_node.parent != ANDAMENTO_NONE)
    { uishell_sidebar_snapshot_node(state->snapshot, reveal_section_node.parent, &reveal_section_node); }
    String8 reveal_key = uishell_sidebar_string(reveal_section_node.key);
    CFG_Node *reveal_view = uishell_sidebar_find_view(uishell_sidebar_dock_layout(&split), reveal_key);
    ok = ok && reveal_view != &cfg_nil_node;
    cfg_node_child_from_string_or_alloc(rd_state->cfg, reveal_view, str8_lit("section_collapsed"));
    UI_State *saved_reveal_ui = ui_state, *reveal_ui = ui_state_alloc();
    ui_select_state(reveal_ui);
    state->reveal_workspace_id = created;
    for(U32 frame = 0; frame < 3; frame++)
    {
      UI_IconInfo icons = ws->ui->icon_info;
      UI_AnimationInfo animation = {0}; UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      UIShell_RegsScope(.window = window->id, .view = reveal_view->id, .panel = reveal_view->parent->id)
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
      { uishell_sidebar_render(r2f32p(0, 0, 320, 240), &split,
          (UIShell_SidebarRenderParams){UIShell_SidebarRenderMode_SectionPanel, reveal_key}); }
      ui_end_build();
      B32 revealed = cfg_node_child_from_string(reveal_view, str8_lit("section_collapsed")) == &cfg_nil_node &&
                     state->reveal_workspace_id == 0;
      if(!revealed) { fprintf(stderr, "FAIL docked section Reveal frame %u: still collapsed or request pending\n", frame); }
      ok = revealed && ok;
    }
    state->reveal_workspace_id = 0;
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(reveal_view, str8_lit("section_collapsed")));
    ui_select_state(saved_reveal_ui); ui_state_release(reveal_ui);
    ok = uishell_sidebar_scroll_diagnostics(ws, &split) && ok;
    ok = uishell_sidebar_git_diagnostics(ws, &split) && ok;
    ok = uishell_sidebar_chip_diagnostics(ws, &split) && ok;
    // A pending focus whose target disappears must be completed as a failure.
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
      if(node.workspace_id == created && node.state == ANDAMENTO_LIVE) { activate = node.activate; }
    }
    error = 0;
    ok = ok && uishell_sidebar_dispatch(state, activate, &error);
    uishell_sidebar_result(state, ok, error);
    cfg_node_release(rd_state->cfg, workspace);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_effects(state, &split);
    uishell_sidebar_observe(state, &split);
    path = uishell_sidebar_workspace_path(scratch.arena, state, created, str8_lit("Local fallback"), 0);
    ok = ok && str8_match(path, str8_lit("Local fallback"), 0);
    ok = ok && andamento_snapshot_diagnostic_count(state->snapshot) > 0;
    B32 latent = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("multi"), 0) && node.state == ANDAMENTO_LATENT)
      { latent = 1; activate = node.activate; }
    }
    ok = ok && latent && split.inventory.count == before;
    // Retry after closure/error can create a new presentation of the same entity.
    error = 0;
    ok = ok && uishell_sidebar_dispatch(state, activate, &error);
    uishell_sidebar_result(state, ok, error);
    uishell_sidebar_effects(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    ok = ok && split.inventory.count == before+1;
    workspace = cfg_node_from_id(ws->root_controlled_split_selected_workspace_id);
    panels = cfg_node_child_from_string(workspace, str8_lit("panels"));
    tree = rd_panel_tree_from_cfg(scratch.arena, panels);
    tools_tab = tree.root->last->tabs.last->v;
    tools_id = tools_tab->id;
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(tree.root->last->selected_tab, str8_lit("selected")));
    cfg_node_new(rd_state->cfg, tools_tab, str8_lit("selected"));
    // A new core instance binds saved workspaces from their records without
    // duplicating them. A frame observes the new workspace before then.
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_release(state);
    MemoryZeroStruct(state);
    state = uishell_sidebar_init(ws);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    ok = ok && split.inventory.count == before+1;
    B32 restored_live = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
      restored_live |= node.state == ANDAMENTO_LIVE;
    }
    tree = rd_panel_tree_from_cfg(scratch.arena, panels);
    ok = ok && restored_live && tree.root->last->selected_tab->id == tools_id &&
         tree.root->first->tabs.count == 1 && tree.root->last->tabs.count == 2;
    // Authoritative end and focus retain the host's user-selected overflow tab
    // and panel identities, including content that the producer did not create.
    String8 retained_config = push_str8f(scratch.arena, "%s\ndisplay-variable \"show-finished\" type=\"bool\" default=true label=\"Show finished\" icon=\"F\"\n",
                                        uishell_sidebar_fixture_config);
    error = 0;
    B32 retained_call_ok = andamento_configure(state->core, uishell_sidebar_text(retained_config), &error);
    ok = uishell_sidebar_result(state, retained_call_ok, error) && ok;
    AndamentoFact end_fact = {0};
    end_fact.key = uishell_sidebar_text(str8_lit("flotilla.convoy.phase"));
    end_fact.kind = ANDAMENTO_FACT_TEXT;
    end_fact.text = uishell_sidebar_text(str8_lit("landed"));
    error = 0;
    retained_call_ok = andamento_apply_entity(state->core, 0, uishell_sidebar_text(str8_lit("vessel")),
      uishell_sidebar_text(str8_lit("multi")), uishell_sidebar_text(str8_lit("fixture")), &end_fact, 1, &error);
    ok = uishell_sidebar_result(state, retained_call_ok, error) && ok;
    uishell_sidebar_refresh(state);
    B32 retained_live = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
      if(node.workspace_id == workspace->id && node.state == ANDAMENTO_LIVE &&
         str8_match(uishell_sidebar_string(node.entity_id), str8_lit("multi"), 0))
      {
        retained_live = str8_match(uishell_sidebar_string(node.label), str8_lit("Example workspace"), 0);
        activate = node.activate;
      }
    }
    ok = ok && retained_live;
    error = 0;
    retained_call_ok = uishell_sidebar_dispatch(state, activate, &error);
    ok = uishell_sidebar_result(state, retained_call_ok, error) && ok;
    uishell_sidebar_effects(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    tree = rd_panel_tree_from_cfg(scratch.arena, panels);
    ok = ok && split.inventory.count == before+1 &&
         ws->root_controlled_split_selected_workspace_id == workspace->id &&
         tree.root->last->selected_tab->id == tools_id &&
         tree.root->first->tabs.count == 1 && tree.root->last->tabs.count == 2;

    // A saved workspace's record binds it to its subject before the subject's
    // recipe arrives (or after it is lost): its row is live in that workspace
    // at once, and neither repeated ingress nor the recipe returning opens
    // another or inspects.
    uishell_sidebar_release(state);
    MemoryZeroStruct(state);
    state = uishell_sidebar_init(ws);
    String8 unavailable_patch = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"vessel\",\"id\":\"multi\"}},\"source_id\":\"fixture\",\"set\":{},\"unset\":[\"action.primary.recipe\"]}");
    B32 restore_ok = 1;
    state->inspection[0] = 0;
    for(U64 cycle = 0; cycle < 3; cycle++)
    {
      error = 0;
      B32 accepted = andamento_apply_patch_json(state->core, cycle, uishell_sidebar_text(unavailable_patch), &error);
      restore_ok = uishell_sidebar_result(state, accepted, error) && restore_ok;
      uishell_sidebar_observe(state, &split);
      uishell_sidebar_refresh(state);
      restore_ok = restore_ok && !state->inspection[0];
    }
    // The recipe coming back opens nothing either.
    AndamentoFact recipe = {0};
    recipe.key = uishell_sidebar_text(str8_lit("action.primary.recipe"));
    recipe.kind = ANDAMENTO_FACT_TEXT;
    recipe.text = uishell_sidebar_text(str8_lit("restore-existing-only"));
    error = 0;
    B32 accepted = andamento_apply_entity(state->core, 4, uishell_sidebar_text(str8_lit("vessel")),
      uishell_sidebar_text(str8_lit("multi")), uishell_sidebar_text(str8_lit("fixture")), &recipe, 1, &error);
    restore_ok = uishell_sidebar_result(state, accepted, error) && restore_ok;
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    B32 recipe_restored = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("multi"), 0))
      { recipe_restored |= node.state == ANDAMENTO_LIVE && node.workspace_id == workspace->id; }
    }
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    tree = rd_panel_tree_from_cfg(scratch.arena, panels);
    restore_ok = restore_ok && recipe_restored && split.inventory.count == before+1 &&
                 tree.root->last->selected_tab->id == tools_id;
    fprintf(stderr, "Sidebar unavailable restore diagnostics: %s\n", restore_ok ? "passed" : "FAILED");
    ok = restore_ok && ok;
  }
  ok = markers_ok && ok;
  ok = ok && selection_ok && reorder_ok && coverage_ok;
  fprintf(stderr, "Sidebar host diagnostics: %s (split layout, overflow selection, project motion, reveal, focus, close, failure, retry, restore, ended retention, status glyphs)\n", ok ? "passed" : "FAILED");
  scratch_end(scratch);
  return ok;
}

// Called between frames only: snapshots used by clicks have finished dispatch.
internal U32
uishell_sidebar_apply_live(void *unused, const U8 *data, size_t size)
{
  B32 rejected = 0, unavailable = 0, applied = 0;
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  {
    UIShell_SidebarState *state = uishell_sidebar_init(ws);
    if(state->core == 0) { unavailable = 1; continue; }
    char *error = 0;
    B32 accepted = andamento_apply_patch_json(state->core, wheelhouse_ingress_now_ms(), (AndamentoText){data, size}, &error);
    if(!uishell_sidebar_result(state, accepted, error)) { rejected = 1; }
    if(accepted) { state->error[0] = 0; applied = 1; }
  }
  rd_request_frame();
  return rejected ? 0 : (unavailable || !applied) ? 2 : 1;
}

// The same UI-owned inventory feeds producer discovery and core association.
internal U32
uishell_sidebar_observed_workdirs(void *unused, WheelhouseWorkdirEmit emit, void *context)
{
  Temp scratch = scratch_begin(0, 0);
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  {
    CFG_Node *window = cfg_node_from_id(ws->cfg_id);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    UIShell_Workdirs dirs = uishell_sidebar_workdirs(scratch.arena, &split);
    for(UIShell_Workdir *dir = dirs.first; dir; dir = dir->next) { emit(context, &dir->value); }
  }
  scratch_end(scratch);
  return 1;
}

internal void
uishell_sidebar_poll_live(void)
{
  U64 now = wheelhouse_ingress_now_ms();
  if(uishell_ingress != 0)
  {
    wheelhouse_ingress_poll_observed(uishell_ingress, uishell_sidebar_apply_live, uishell_sidebar_observed_workdirs, 0);
    if(now - uishell_sidebar_last_tick >= 250)
    {
      uishell_sidebar_last_tick = now;
      for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
      {
        UIShell_SidebarState *state = uishell_sidebar_init(ws);
        if(state->core != 0)
        {
          char *error = 0;
          B32 ok = andamento_tick(state->core, now, &error);
          uishell_sidebar_result(state, ok, error);
        }
      }
      rd_request_frame();
    }
  }
  // Records whose generation moved are written once they are due.
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  {
    UIShell_SidebarState *state = uishell_sidebar_init(ws);
    if(state->core != 0) { uishell_sidebar_records_save(state, now, 0); }
  }
}
