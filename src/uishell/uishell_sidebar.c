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
  B32 open, engaged, focused, contains_current, source_seen, corridor_active, enriched;
  F32 scroll, content_height;
};

typedef struct UIShell_SidebarLabel UIShell_SidebarLabel;
struct UIShell_SidebarLabel { String8 identity, label; };

typedef struct UIShell_DisplayWake UIShell_DisplayWake;
struct UIShell_DisplayWake
{
  U32 references, fired, delay_ms;
};

internal void
uishell_sidebar_display_wake_release(UIShell_DisplayWake *wake)
{
  if(wake && ins_atomic_u32_dec_eval(&wake->references) == 0) { free(wake); }
}

typedef struct UIShell_DisplayRestore UIShell_DisplayRestore;
struct UIShell_DisplayRestore
{
  UIShell_DisplayRestore *next;
  String8 name; // declaration identity; never retain snapshot action indices
  // Pending means unresolved; attempts at the limit stop dispatch, not reconciliation.
  B32 desired, observed, pending;
  U32 attempts;
  U64 retry_at;
};
typedef struct UIShell_SectionPlacement UIShell_SectionPlacement;

struct UIShell_SidebarState
{
  Arena *display_restore_arena;
  UIShell_DisplayRestore *display_restores;
  U64 display_wakeup_at;
  UIShell_DisplayWake *display_wakeup;
  Arena *labels_arena;
  UIShell_SidebarLabel *labels;
  U64 labels_capacity;
  AndamentoSnapshot *labels_snapshot;
  UIShell_SidebarSection *sections;
  UIShell_HoverCard cards[2];
  UIShell_HoverCard *detached;
  CFG_ID pin_before, pin_reveal;
  UIShell_HoverCard *drag_card;
  CFG_ID card_drop_panel;
  Dir2 card_drop_direction;
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
  U64 workdirs_hash;
  U64 workdirs_retry_at;
  U64 managed_cfg_generation;
  UI_State *render_ui;
  U64 render_build_index;
  B32 managed_dirty;
  U64 managed_error_workspace;
  B32 initialized;
  B32 restored;
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

internal void
uishell_sidebar_release(UIShell_SidebarState *state)
{
  if(state != 0)
  {
    for(U64 i = 0; i < ArrayCount(state->cards); i++)
    {
      if(state->cards[i].arena) { arena_release(state->cards[i].arena); }
      if(state->cards[i].label_arena) { arena_release(state->cards[i].label_arena); }
      MemoryZeroStruct(&state->cards[i]);
    }
    uishell_sidebar_display_wake_release(state->display_wakeup);
    state->display_wakeup = 0;
    if(state->display_restore_arena) { arena_release(state->display_restore_arena); }
    if(state->placement_arena) { arena_release(state->placement_arena); state->placement_arena = 0; }
    state->placement_snapshot = 0;
    state->placement_regions = 0;
    state->placement_count = 0;
    state->display_restore_arena = 0;
    state->display_restores = 0;
    state->display_wakeup_at = 0;
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
  uishell_sidebar_labels_invalidate(state);
  andamento_snapshot_release(state->snapshot);
  state->snapshot = snapshot;
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
struct UIShell_Workdir { UIShell_Workdir *next; WheelhouseWorkdir value; };
typedef struct { UIShell_Workdir *first, *last; U64 count; } UIShell_Workdirs;

internal WheelhouseIngressText
uishell_ingress_text(String8 text)
{
  return (WheelhouseIngressText){text.str, text.size};
}

// Persist only declared boolean display variables. Storage belongs to the
// window node in the saved user configuration; dispatch is Andamento's action.
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
    andamento_snapshot_node(it->snapshot, it->node, &node);
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

internal void
uishell_sidebar_save_display(UIShell_SidebarState *state, CFG_Node *window)
{
  if(!state->snapshot) { return; }
  UIShell_DisplayControlIterator it = {state->snapshot};
  AndamentoControl control = {0}; AndamentoText name = {0};
  while(uishell_sidebar_next_persistent_control(&it, &control, &name))
  {
    CFG_Node *saved = cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("sidebar_display"));
    CFG_Node *value = cfg_node_child_from_string_or_alloc(rd_state->cfg, saved, uishell_sidebar_string(name));
    UIShell_DisplayRestore *restore = state->display_restores;
    for(; restore; restore = restore->next)
    { if(str8_match(restore->name, uishell_sidebar_string(name), 0)) { break; } }
    // Saving another action must not erase an unresolved saved preference.
    // A changed live value is newer user intent and supersedes recovery.
    if(restore && restore->pending)
    {
      // Unchanged live state does not authorize erasing failed saved intent.
      if(restore->observed == !!control.checked) { continue; }
      restore->pending = 0;
    }
    String8 text = control.checked ? str8_lit("true") : str8_lit("false");
    if(!str8_match(value->first->string, text, 0)) { cfg_node_new_replace(rd_state->cfg, value, text); }
  }
}

// State and timer each own one reference to a completion token. It contains
// no sidebar/window/snapshot pointers and outlives a released sidebar. A fired
// token is consumed even before its deadline, allowing an early wake to re-arm.
// Heap storage outlives the sidebar arena. Raw OS threads avoid the base helper's
// detachable entity lifetime; each worker owns a token reference until return.
#if OS_WINDOWS
internal DWORD WINAPI
uishell_sidebar_display_wakeup(void *data)
#else
internal void *
uishell_sidebar_display_wakeup(void *data)
#endif
{
  UIShell_DisplayWake *wake = data;
  sleep_ms(wake->delay_ms);
  ins_atomic_u32_eval_assign(&wake->fired, 1);
  wm_send_wakeup_event();
  uishell_sidebar_display_wake_release(wake);
  return 0;
}

internal UIShell_DisplayWake *
uishell_sidebar_display_wake_after(U32 delay)
{
  UIShell_DisplayWake *wake = calloc(1, sizeof(*wake));
  if(!wake) { return 0; }
  wake->references = 2; wake->delay_ms = delay;
#if OS_WINDOWS
  HANDLE thread = CreateThread(0, 0, uishell_sidebar_display_wakeup, wake, 0, 0);
  if(!thread) { free(wake); return 0; }
  CloseHandle(thread);
#else
  pthread_t thread;
  if(pthread_create(&thread, 0, uishell_sidebar_display_wakeup, wake) != 0)
  { free(wake); return 0; }
  pthread_detach(thread);
#endif
  return wake;
}

enum { UIShell_DisplayRetryLimit = 3, UIShell_DisplayRetryStepMs = 1000 };

// Three total attempts, spaced one and two seconds apart. Exhaustion retains
// the saved preference and surfaced error until a new explicit restore/session.
// Reconciliation uses fresh declarations and stops for newer saved/live intent.
internal void
uishell_sidebar_retry_display(UIShell_SidebarState *state, CFG_Node *window, U64 now)
{
  if(!state->core) { return; }
  if(state->display_wakeup && ins_atomic_u32_eval(&state->display_wakeup->fired))
  {
    uishell_sidebar_display_wake_release(state->display_wakeup);
    state->display_wakeup = 0;
    state->display_wakeup_at = 0;
  }
  uishell_sidebar_refresh(state);
  // FFI acquisition always returns a snapshot for a healthy core. Null means
  // invalid/poisoned core (recreation required), not a transient ingress failure;
  // another timer cannot repair that core. Local polling needs no ingress.
  if(!state->snapshot) { return; }
  for(UIShell_DisplayRestore *r = state->display_restores; r; r = r->next)
  {
    if(!r->pending) { continue; }
    CFG_Node *saved = cfg_node_child_from_string(window, str8_lit("sidebar_display"));
    CFG_Node *value = cfg_node_child_from_string(saved, r->name);
    B32 desired = str8_match(value->first->string, str8_lit("true"), 0);
    if(!desired && !str8_match(value->first->string, str8_lit("false"), 0))
    { r->pending = 0; continue; }
    if(desired != r->desired) { r->desired = desired; r->attempts = 0; }
    UIShell_DisplayControlIterator it = {state->snapshot};
    AndamentoControl control = {0}; AndamentoText name = {0};
    B32 found = 0;
    while(uishell_sidebar_next_persistent_control(&it, &control, &name))
    { if(str8_match(r->name, uishell_sidebar_string(name), 0)) { found = 1; break; } }
    if(found && !!control.checked == r->desired) { r->pending = 0; continue; }
    // Polling observes values, never attributes an external change to user intent.
    // Explicit UI actions save intent through save_display; retry preserves it.
    if(r->attempts >= UIShell_DisplayRetryLimit || now < r->retry_at) { continue; }
    r->attempts++;
    char *error = 0;
    B32 ok = found && control.action != ANDAMENTO_NONE &&
      uishell_sidebar_dispatch(state, control.action, &error);
    if(!found)
    {
      Temp scratch = scratch_begin(0, 0);
      String8 message = push_str8f(scratch.arena, "Sidebar display declaration unavailable: %S", r->name);
      uishell_sidebar_set_error(state, message);
      scratch_end(scratch);
    }
    else
    {
      uishell_sidebar_result(state, ok, error);
    }
    uishell_sidebar_refresh(state);
    rd_request_frame();
    if(ok) { r->pending = 0; }
    else { r->retry_at = now + UIShell_DisplayRetryStepMs*r->attempts; }
  }
  U64 deadline = 0;
  for(UIShell_DisplayRestore *r = state->display_restores; r; r = r->next)
  {
    if(r->pending && r->attempts < UIShell_DisplayRetryLimit && (!deadline || r->retry_at < deadline))
    { deadline = r->retry_at; }
  }
  if(deadline != state->display_wakeup_at)
  {
    uishell_sidebar_display_wake_release(state->display_wakeup);
    state->display_wakeup = 0;
    state->display_wakeup_at = 0;
    if(deadline)
    {
      U64 delay = deadline > now ? deadline-now : 1;
      state->display_wakeup = uishell_sidebar_display_wake_after((U32)delay);
      if(state->display_wakeup) { state->display_wakeup_at = deadline; }
      else { uishell_sidebar_result(state, 0, 0); }
    }
  }
}

internal void
uishell_sidebar_restore_display(UIShell_SidebarState *state, CFG_Node *window)
{
  if(!state->core) { return; }
  uishell_sidebar_refresh(state);
  if(!state->snapshot) { return; }
  if(!state->display_restore_arena) { state->display_restore_arena = arena_alloc(); }
  arena_clear(state->display_restore_arena);
  state->display_restores = 0;
  CFG_Node *saved = cfg_node_child_from_string(window, str8_lit("sidebar_display"));
  for(CFG_Node *value = saved->first; value != &cfg_nil_node; value = value->next)
  {
    B32 desired = str8_match(value->first->string, str8_lit("true"), 0);
    if(!desired && !str8_match(value->first->string, str8_lit("false"), 0)) { continue; }
    UIShell_DisplayControlIterator it = {state->snapshot};
    AndamentoControl control = {0}; AndamentoText name = {0};
    while(uishell_sidebar_next_persistent_control(&it, &control, &name))
    {
      if(!str8_match(value->string, uishell_sidebar_string(name), 0) || desired == !!control.checked) { continue; }
      UIShell_DisplayRestore *r = push_array(state->display_restore_arena, UIShell_DisplayRestore, 1);
      r->name = push_str8_copy(state->display_restore_arena, value->string);
      r->desired = desired; r->observed = !!control.checked; r->pending = 1;
      SLLStackPush(state->display_restores, r);
      break;
    }
  }
  uishell_sidebar_retry_display(state, window, wheelhouse_ingress_now_ms());
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
    CFG_PanelTree tree = cfg_panel_tree_from_panels_cfg(arena, panels, Axis2_X);
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
        SLLQueuePush(result.first, result.last, dir);
        result.count++;
      }
    }
  }
  return result;
}

internal void
uishell_sidebar_observe(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  Temp scratch = scratch_begin(0, 0);
  AndamentoWorkspace *items = push_array(scratch.arena, AndamentoWorkspace, split->inventory.count);
  U64 hash = 5381, count = 0;
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
  {
    items[count] = (AndamentoWorkspace){w->id, count, uishell_sidebar_text(w->display_name), w == split->inventory.selected};
    hash = hash*33 + w->id;
    hash = hash*33 + items[count].selected;
    for(U64 i = 0; i < w->display_name.size; i++) { hash = hash*33 + w->display_name.str[i]; }
    count++;
  }
  UIShell_Workdirs dirs = uishell_sidebar_workdirs(scratch.arena, split);
  AndamentoWorkdir *observed = push_array(scratch.arena, AndamentoWorkdir, dirs.count);
  U64 index = 0, dirs_hash = 5381;
  for(UIShell_Workdir *dir = dirs.first; dir; dir = dir->next)
  {
    WheelhouseIngressText cwd = dir->value.live_cwd.len ? dir->value.live_cwd : dir->value.cwd;
    observed[index++] = (AndamentoWorkdir){dir->value.workspace_id, {cwd.data, cwd.len}};
    dirs_hash = dirs_hash*33 + dir->value.workspace_id;
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
    topology_ready = andamento_observe(state->core, items, count, 0, 0, &error);
    if(uishell_sidebar_result(state, topology_ready, error))
    { state->topology_hash = hash; changed = 1; }
  }
  U64 now = wheelhouse_ingress_now_ms();
  if(topology_ready && dirs_hash != state->workdirs_hash && now >= state->workdirs_retry_at)
  {
    char *error = 0;
    B32 ok = andamento_observe_workdirs(state->core, observed, dirs.count, &error);
    if(uishell_sidebar_result(state, ok, error))
    { state->workdirs_hash = dirs_hash; state->workdirs_retry_at = 0; changed = 1; }
    else { state->workdirs_retry_at = now+1000; }
  }
  if(changed) { uishell_sidebar_refresh(state); rd_request_frame(); }
  scratch_end(scratch);
}

internal UIShell_SidebarState *
uishell_sidebar_init(RD_WindowState *ws)
{
  if(ws->sidebar == 0) { ws->sidebar = push_array(ws->arena, UIShell_SidebarState, 1); }
  UIShell_SidebarState *state = ws->sidebar;
  if(!state->initialized)
  {
    state->initialized = 1;
    char *error = 0;
    if(andamento_abi_version() != 2)
    {
      uishell_sidebar_result(state, 0, 0);
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
      uishell_sidebar_refresh(state);
      uishell_sidebar_restore_display(state, cfg_node_from_id(ws->cfg_id));
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
      for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
      { if(w->id == effect.workspace_id) { workspace = w->mount.owner_cfg; break; } }
      if(workspace != &cfg_nil_node)
      {
        outcome = ANDAMENTO_COMPLETE_FOCUS;
        char *retry_error = 0;
        andamento_content_retry(state->core, workspace->id, &retry_error);
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
    B32 ok = andamento_complete(state->core, effect.request_id, outcome, workspace == &cfg_nil_node ? 0 : workspace->id, uishell_sidebar_text(failure), &error);
    uishell_sidebar_result(state, ok, error);
  }
  andamento_effects_release(effects);
}

// Rebind persisted fixture identities without launching their recipes again.
internal void
uishell_sidebar_restore(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  if(state->restored || state->snapshot == 0) { return; }
  state->restored = 1;
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  CFG_ID selected = ws->root_controlled_split_selected_workspace_id;
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
  {
    String8 kind = cfg_node_child_from_string(w->mount.owner_cfg, str8_lit("sidebar_entity_kind"))->first->string;
    String8 id = cfg_node_child_from_string(w->mount.owner_cfg, str8_lit("sidebar_entity_id"))->first->string;
    if(kind.size == 0 || id.size == 0) { continue; }
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
      // Inspect cannot rebind a saved workspace. Wait for a recipe rather
      // than invalidating the snapshot on every producer heartbeat.
      if(node.state == ANDAMENTO_LATENT && node.openable && node.activate != ANDAMENTO_NONE &&
         str8_match(kind, uishell_sidebar_string(node.entity_kind), 0) &&
         str8_match(id, uishell_sidebar_string(node.entity_id), 0))
      {
        char *error = 0;
        B32 ok = uishell_sidebar_dispatch(state, node.activate, &error);
        if(uishell_sidebar_result(state, ok, error)) { uishell_sidebar_effects(state, split); }
        uishell_sidebar_refresh(state);
        break;
      }
    }
  }
  ws->root_controlled_split_selected_workspace_id = selected;
  uishell_sidebar_observe(state, split);
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
    AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
    if(node.is_section || node.state != ANDAMENTO_LIVE || node.workspace_id != workspace_id) { continue; }
    U64 depth = 0;
    for(U64 parent = node.parent; parent != ANDAMENTO_NONE; depth++)
    {
      AndamentoNode ancestor = {0}; andamento_snapshot_node(state->snapshot, parent, &ancestor);
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
    if(!andamento_snapshot_node(state->snapshot, at, &node)) { break; }
    if(!node.is_section) { path[length++] = at; }
    at = node.parent;
  }
  String8 result = str8_zero();
  for(U64 i = length; i > 0; i--)
  {
    AndamentoNode node = {0};
    andamento_snapshot_node(state->snapshot, path[i-1], &node);
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
    AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, target, &node);
    size_t toggle = ANDAMENTO_NONE;
    for(U64 parent = node.parent; parent != ANDAMENTO_NONE;)
    {
      AndamentoNode ancestor = {0}; andamento_snapshot_node(state->snapshot, parent, &ancestor);
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

typedef struct UIShell_SidebarProjectRule UIShell_SidebarProjectRule;
struct UIShell_SidebarProjectRule
{
  UI_Box *title;
  Vec4F32 accent;
};

// Follow the laid-out title, including its text padding and font settings.
internal UI_BOX_CUSTOM_DRAW(uishell_sidebar_project_rule_draw)
{
  UIShell_SidebarProjectRule *data = (UIShell_SidebarProjectRule *)user_data;
  F32 x0 = ui_box_text_position(data->title).x;
  F32 x1 = box->rect.x1-4.f;
  if(x0 < x1)
  {
    dr_rect(r2f32p(x0, box->rect.y1-1.f, x1, box->rect.y1), data->accent, 0, 0, 0);
  }
}

#include "uishell/uishell_hover_cards.c"

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
}
UIShell_SidebarCloseKind;

// The affordance says what closing loses. A workspace with a live or unobserved
// subject detaches: its row stays and reopens the same layout. An ended subject
// or a subjectless workspace cannot come back, so closing destroys it.
internal UIShell_SidebarCloseKind
uishell_sidebar_close_kind(AndamentoNode node, String8 status)
{
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
    AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
    if(!node.is_section && node.state == ANDAMENTO_LIVE && node.workspace_id == workspace_id &&
       str8_match(uishell_sidebar_node_status(state, node), str8_lit("ended"), 0))
    { return 1; }
  }
  return 0;
}

internal String8
uishell_sidebar_close_label(UIShell_SidebarCloseKind kind)
{
  return kind == UIShell_SidebarCloseKind_Detach ? str8_lit("Detach workspace") : str8_lit("Close workspace");
}

internal void
uishell_sidebar_close_workspace(RD_WindowState *ws, AndamentoNode node, UIShell_SidebarCloseKind kind)
{
  if(kind == UIShell_SidebarCloseKind_None) { return; }
  uishell_cmd(kind == UIShell_SidebarCloseKind_Detach ? "detach_workspace" : "close_workspace",
              .window = ws->cfg_id, .cfg = node.workspace_id);
}

internal size_t
uishell_sidebar_entry_signal(UIShell_SidebarState *state, RD_WindowState *ws,
                             AndamentoNode node, U64 node_index, UI_Signal sig, String8 context,
                             B32 contains_current, B32 menu)
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
  if(ui_clicked(sig))
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
  if(subject)
  {
    uishell_sidebar_subject_hit(node, sig.box, "subject", menu);
    UI_Key menu_key = ui_key_from_stringf(sig.box->key, "subject_menu");
    UI_CtxMenu(menu_key) UI_PrefWidth(ui_em(18.f, 1)) UI_PrefHeight(ui_em(1.8f, 1))
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
    }
    if(ui_right_clicked(sig)) { ui_ctx_menu_open(menu_key, sig.box->key, v2f32(0, em*1.8f)); }
  }
  else if(close_kind != UIShell_SidebarCloseKind_None)
  {
    // Rows show this on hover too; inline action chips have only this menu.
    UI_Key menu_key = ui_key_from_stringf(sig.box->key, "workspace_menu");
    UI_CtxMenu(menu_key) UI_PrefWidth(ui_em(18.f, 1)) UI_PrefHeight(ui_em(1.8f, 1))
    {
      if(ui_clicked(ui_button(uishell_sidebar_close_label(close_kind))))
      { uishell_sidebar_close_workspace(ws, node, close_kind); ui_ctx_menu_close(); }
    }
    if(ui_right_clicked(sig)) { ui_ctx_menu_open(menu_key, sig.box->key, v2f32(0, em*1.8f)); }
  }
  uishell_sidebar_card_source(state, node, sig, context, contains_current);
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

internal String8
uishell_sidebar_chip_icon(UIShell_SidebarState *state, AndamentoNode node, B32 *icon_font)
{
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
  RD_IconKind fallback = str8_match(kind, str8_lit("project"), 0) ? RD_IconKind_Thumbnails :
    str8_match(kind, str8_lit("role"), 0) ? RD_IconKind_Threads : RD_IconKind_Machine;
  return rd_icon_kind_text_table[fallback];
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
  size_t action = uishell_sidebar_entry_signal(state, ws, node, node_index, sig, context, 0, menu);
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
  return str8_match(key, str8_lit("andamento.unplaced-workspaces"), 0);
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
    uishell_sidebar_restore(state, split);
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
  for(U64 i = 0; i < count; i++) { inline_first[i] = inline_last[i] = inline_next[i] = ANDAMENTO_NONE; }
  U64 section_count = 0;
  for(U64 i = 0; i < count; i++)
  {
    andamento_snapshot_node(state->snapshot, i, &nodes[i]);
    if(nodes[i].is_section) { sections[section_count++] = i; }
    project_owner[i] = ANDAMENTO_NONE;
    if(nodes[i].parent != ANDAMENTO_NONE)
    {
      U64 parent = nodes[i].parent;
      project_owner[i] = project_owner[parent];
      // A project's children remain available while its viewport closes.
      // Nested collapse state still determines their full, unanimated layout.
      hidden[i] = hidden[parent] || (!nodes[parent].is_section &&
        project_owner[parent] != parent && nodes[parent].collapsed);
      depth[i] = depth[parent] + !nodes[parent].is_section;
    }
    if(!nodes[i].is_section && depth[i] == 0 &&
       str8_match(uishell_sidebar_string(nodes[i].entity_kind), str8_lit("project"), 0))
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
    { section->collapsed = cfg_node_child_from_string(cfg_node_from_id(uishell_regs()->view), str8_lit("section_collapsed")) != &cfg_nil_node; }
    if(sections[n] == reveal_section)
    {
      section->collapsed = 0;
      if(section_panel && str8_match(only_section, key, 0))
      {
        CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
        cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("section_collapsed")));
      }
    }
    states[n] = section;
    U64 end = n+1 < section_count ? sections[n+1] : count;
    for(U64 i = sections[n]; i < end; i++)
    {
      if(nodes[i].parent == sections[n]) { entries[n]++; }
      if(hidden[i] || inlined[i]) { continue; }
      U64 node_rows = !nodes[i].is_section;
      if(project_owner[i] == i) { content_heights[n] += project_gap+2*project_padding; }
      for(U64 c = 0; !nodes[i].is_section && c < nodes[i].control_count; c++)
      {
        AndamentoControl control = {0};
        if(andamento_snapshot_control(state->snapshot, nodes[i].first_control+c, &control) && control.action != ANDAMENTO_NONE) { node_rows++; }
      }
      rows[n] += node_rows;
      F32 node_height = node_rows*row_height;
      node_height += uishell_sidebar_inline_height(state, uishell_sidebar_string(nodes[i].key));
      U64 owner = project_owner[i];
      if(owner != ANDAMENTO_NONE && owner != i)
      {
        project_child_heights[owner] += node_height;
        node_height *= project_open[owner];
      }
      content_heights[n] += node_height;
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
      UI_Box *header;
      if(states[n]->collapsed && contains_selected[sections[n]])
      { ui_set_next_border_color(uishell_sidebar_selection_fill(1)); }
      UI_Rect(r2f32p(0, y+(!section_panel && n != flexible && heights[n] > 0 ? 6.f : 0.f), dim.x, y+row_height)) UI_ChildLayoutAxis(Axis2_X)
      { header = ui_build_box_from_stringf(states[n]->collapsed && contains_selected[sections[n]] ? UI_BoxFlag_DrawBorder : 0, "###section_header_%S", key); }
      UI_Parent(header) UI_PrefHeight(ui_pct(1, 1)) UI_FontSize(floor_f32(em*0.82f)) UI_TagF("weak")
      {
        B32 toggle = 0;
        ui_spacer(ui_em(0.3f, 1));
        if(section_panel) UI_PrefWidth(ui_em(UIShell_GripWidthEM, 1))
        {
          UI_Signal drag = uishell_sidebar_grip(str8_lit("section_drag"), str8_lit("Drag section"));
          if(ui_dragging(drag) && !rd_drag_is_active() && length_2f32(ui_drag_delta()) > UIShell_DragThresholdPT)
          { rd_drag_begin(UIShell_ContextRegSlot_View); }
        }
        toggle |= ui_clicked(uishell_sidebar_disclosure(!states[n]->collapsed, push_str8f(scratch.arena, "###section_toggle_%S", key)));
        UI_PrefWidth(ui_pct(1, 0))
        { toggle |= ui_clicked(uishell_sidebar_button(push_str8f(scratch.arena, "%S###section_%S", upper_from_str8(scratch.arena, title), key))); }
        UI_PrefWidth(ui_text_dim(0.6f, 1)) UI_TextAlignment(UI_TextAlign_Right)
        { ui_label(push_str8f(scratch.arena, "%I64u", entries[n])); }
        for(U64 c = 0; c < section_node->control_count; c++)
        {
          AndamentoControl control = {0};
          if(!andamento_snapshot_control(state->snapshot, section_node->first_control+c, &control) ||
             control.action == ANDAMENTO_NONE) { continue; }
          B32 checked = control.value_kind == 1 && control.checked;
          UI_PrefWidth(ui_em(1.7f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center)
          UI_CornerRadius(3.f) UI_BackgroundColor(uishell_sidebar_selection_fill(1))
          {
            UI_Box *button = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText|
              UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects|UI_BoxFlag_DisableTruncatedHover|
              (checked ? UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder : 0),
              "%S###control_%S_%I64u", uishell_sidebar_string(control.glyph), key, c);
            UI_Signal sig = ui_signal_from_box(button);
            if(ui_clicked(sig)) { action = control.action; }
            if(ui_hovering(sig)) UI_Tooltip
            { ui_state->tooltip_anchor_key = button->key; ui_label(uishell_sidebar_string(control.label)); }
          }
        }
        if(uishell_sidebar_section_hosts_chrome(key))
        {
          // Chrome resolution reads this next frame (ADR-0006).
          ws->chrome_section_header_frame = rd_state->frame_index+1;
          if(ws->chrome_niche[RD_ChromeElementKind_NewWorkspace] == RD_ChromeNiche_SectionHeader)
            UI_PrefWidth(ui_em(1.7f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center) UI_TagF("")
          { rd_chrome_build_new_workspace(split->owner_cfg); }
        }
        if(section_panel)
        {
          CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
          UI_Key hover_key = ui_key_from_stringf(root->key, "###section_header_%S", key);
          UI_Box *previous = ui_box_from_key(hover_key);
          B32 engaged = !ui_box_is_nil(previous) && contains_2f32(previous->rect, ui_mouse());
          UI_PrefWidth(ui_em(1.5f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center)
          {
            UI_Signal close = uishell_sidebar_button(engaged ? str8_lit("×###section_close") : str8_lit("###section_close"));
            if(engaged && ui_clicked(close) && rd_dock_can_close(view)) { uishell_cmd("close_tab"); }
          }
        }
        ui_spacer(ui_px(4.f, 1));
        if(toggle)
        {
          states[n]->collapsed = !states[n]->collapsed;
          if(section_panel)
          {
            CFG_Node *view = cfg_node_from_id(uishell_regs()->view);
            if(states[n]->collapsed) { cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("section_collapsed")); }
            else { cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("section_collapsed"))); }
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
      UI_ScrollRegionParams params = ui_scroll_region_params(r2f32p(0, y, dim.x, y+heights[n]),
        UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Auto);
      params.content_dim_px = v2f32(0, content_heights[n]);
      UI_ScrollRegion region = ui_scroll_region_layout(params);
      UI_Key content_key = ui_key_from_stringf(root->key, "section_body_%S", key);
      UI_Box *previous = ui_box_from_key(content_key);
      UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
      axes[Axis2_Y].range = r1s64(0, Max(0, (S64)(content_heights[n]-heights[n])));
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
      UI_Parent(body) UI_PrefWidth(ui_pct(1, 0)) UI_PrefHeight(ui_px(row_height, 1))
      {
        U64 end = n+1 < section_count ? sections[n+1] : count;
        UI_Box *project_box = 0;
        U64 project_depth = 0, project_index = ANDAMENTO_NONE;
        F32 project_children_y = 0;
        ui_spacer(ui_px(body_top_padding, 1));
        F32 row_y = body_top_padding;
        for(U64 i = sections[n]; i < end; i++)
        {
          if(project_box && depth[i] <= project_depth)
          {
            ui_pop_flags();
            ui_pop_parent(); // clipped project children
            ui_spacer(ui_px(project_padding, 1));
            ui_pop_parent();
            ui_spacer(ui_px(project_gap, 1));
            row_y = project_children_y + project_child_heights[project_index]*project_open[project_index] + project_padding+project_gap;
            project_box = 0;
          }
          U64 owner = project_owner[i];
          if(hidden[i] || inlined[i] ||
             (owner != ANDAMENTO_NONE && owner != i && project_open[owner] == 0.f)) { continue; }
          AndamentoNode node = nodes[i];
          B32 children = row_children[i] || inline_count[i];
          String8 node_key = uishell_sidebar_string(node.key);
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
          B32 project = depth[i] == 0 && str8_match(kind, str8_lit("project"), 0);
          U64 chip_count = (node.collapsed ? 0 : inline_count[i])+project;
          U64 *members = push_array(scratch.arena, U64, chip_count);
          UIShell_ChipMeasure *chip_measures = push_array(scratch.arena, UIShell_ChipMeasure, chip_count);
          U64 member_count = 0;
          if(project) { members[member_count++] = i; }
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
          F32 status_width = em*(str8_match(kind, str8_lit("change_request"), 0) ? 10.f : 1.2f);
          F32 row_width = Max(0.f, dim_2f32(region.viewport).x-8.f-(owner != ANDAMENTO_NONE ? 4.f : 0.f));
          F32 available_width = Max(0.f, row_width-em*(indent+1.5f+1.2f)-status_width-(project ? 3.f : 0.f));
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
            Vec4F32 accent = uishell_sidebar_project_accent(uishell_sidebar_string(node.entity_id));
            // Keep rounded strokes and their antialiasing inside the viewport clip.
            UI_FixedX(2.f) UI_PrefWidth(ui_px(Max(0.f, dim_2f32(region.viewport).x-4.f), 1))
            UI_PrefHeight(ui_children_sum(1)) UI_ChildLayoutAxis(Axis2_Y) UI_CornerRadius(5.f)
            UI_BackgroundColor(mix_4f32(ui_color_from_name(str8_lit("background")), accent, 0.035f))
            {
              project_box = ui_build_box_from_stringf(UI_BoxFlag_DrawBorder|UI_BoxFlag_DrawBackground, "###project_%S", node_key);
            }
            project_depth = depth[i];
            project_index = i;
            ui_push_parent(project_box);
            ui_spacer(ui_px(project_padding, 1));
            row_y += project_padding;
          }
          B32 contains_current = node.collapsed && contains_selected[i] && !node.selected;
          if(!node.is_section)
          {
            if(i == reveal)
            {
              body->view_off_target.y = Clamp(0.f, row_y-(heights[n]-row_height)*0.5f, (F32)axes[Axis2_Y].range.max);
              body->view_off.y = body->view_off_target.y;
              state->reveal_workspace_id = 0;
              rd_request_frame();
            }
            row_y += row_height;
            // Persistent workspace selection remains visible while the terminal
            // has focus, without borrowing the keyboard-focus border.
            // Insets keep row selection and action borders inside the container.
            UI_Box *slot;
            UI_ChildLayoutAxis(Axis2_X)
            { slot = ui_build_box_from_stringf(0, "###row_slot_%S", node_key); }
            ui_push_parent(slot);
            ui_spacer(ui_px(4.f, 1));
            UI_Box *column;
            UI_ChildLayoutAxis(Axis2_Y) UI_PrefHeight(ui_pct(1, 1))
            { column = ui_build_box_from_stringf(0, "###row_column_%S", node_key); }
            ui_push_parent(column);
            ui_spacer(ui_px(2.f, 1));
            UI_Box *row;
            if(node.selected) { ui_set_next_background_color(uishell_sidebar_selection_fill(0)); }
            if(contains_current) { ui_set_next_border_color(uishell_sidebar_selection_fill(1)); }
            UI_PrefHeight(ui_px(row_height-4.f, 1)) UI_CornerRadius(3.f) UI_ChildLayoutAxis(Axis2_X)
            { row = ui_build_box_from_stringf((node.selected ? UI_BoxFlag_DrawBackground : 0) | (contains_current ? UI_BoxFlag_DrawBorder : 0), "###sidebar_row_%S", node_key); }
            UI_Parent(row) UI_PrefHeight(ui_pct(1, 1))
            {
              if(project)
              {
                UI_BackgroundColor(uishell_sidebar_project_accent(uishell_sidebar_string(node.entity_id)))
                UI_PrefWidth(ui_px(3.f, 1))
                { ui_build_box_from_stringf(UI_BoxFlag_DrawBackground, "###accent_%S", node_key); }
              }
              ui_spacer(ui_em(indent, 1));
              UI_PrefWidth(ui_em(1.5f, 1))
              {
                if(children && node.toggle != ANDAMENTO_NONE)
                {
                  if(ui_clicked(uishell_sidebar_disclosure(!node.collapsed, push_str8f(scratch.arena, "###toggle_%S", node_key)))) { action = node.toggle; }
                }
                else { ui_spacer(ui_em(1.5f, 1)); }
              }
              UI_TagF("weak") UI_PrefWidth(ui_em(1.2f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center) RD_Font(RD_FontSlot_Icons)
              {
                RD_IconKind icon = RD_IconKind_FileOutline;
                if(str8_match(kind, str8_lit("project"), 0)) { icon = RD_IconKind_FolderClosedOutline; }
                else if(str8_match(kind, str8_lit("convoy"), 0) || str8_match(kind, str8_lit("role"), 0)) { icon = RD_IconKind_Threads; }
                else if(str8_match(kind, str8_lit("vessel"), 0) || str8_match(kind, str8_lit("session"), 0)) { icon = RD_IconKind_Machine; }
                if(chip_count && chip_layout.name_width == 0)
                {
                  // Keep the full row identity and its rich hover card reachable
                  // through the fixed icon when chips consume the name column.
                  UI_Signal sig = uishell_sidebar_button(push_str8f(scratch.arena, "%S###identity_%S", rd_icon_kind_text_table[icon], node_key));
                  size_t requested = uishell_sidebar_entry_signal(state, ws, node, i, sig, context, contains_current, 0);
                  if(requested != ANDAMENTO_NONE) { action = requested; }
                }
                else { ui_label(rd_icon_kind_text_table[icon]); }
              }
              UI_PrefWidth(ui_pct(1, 0))
              {
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
                if(str8_match(status, str8_lit("ended"), 0))
                { ui_set_next_text_color(uishell_sidebar_ended_color()); }
                UI_Signal sig = uishell_sidebar_button(push_str8f(scratch.arena, "%S###entry_%S", display, node_key));
                if(project && project_child_heights[i] > 0 && project_open[i] > 0)
                {
                  UIShell_SidebarProjectRule *rule = push_array(ui_build_arena(), UIShell_SidebarProjectRule, 1);
                  rule->title = sig.box;
                  rule->accent = uishell_sidebar_project_accent(uishell_sidebar_string(node.entity_id));
                  rule->accent.w *= project_open[i];
                  ui_box_equip_custom_draw(slot, uishell_sidebar_project_rule_draw, rule);
                }
                // The rich tooltip already includes the full label. Do not also
                // enroll this row in the shell's automatic truncated-text hover.
                sig.box->flags |= UI_BoxFlag_DisableTruncatedHover;
                size_t requested = uishell_sidebar_entry_signal(state, ws, node, i, sig, context, contains_current, 0);
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
              // Reserve the same trailing slot at every level. Project-wide
              // status belongs here once supplied; workspace state stays on
              // the overview action rather than being duplicated in this slot.
              B32 change_request = str8_match(kind, str8_lit("change_request"), 0);
              UI_PrefWidth(ui_em(change_request ? 10.f : 1.2f, 1)) UI_TextPadding(0)
              {
                if(change_request)
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
                  UI_TextColor(color) { ui_label(badge); }
                }
                else if(project) { ui_spacer(ui_em(1.2f, 1)); }
                else
                {
                  // Hovering the row turns its status mark into the close
                  // affordance: detach keeps the workspace, × destroys it.
                  UIShell_SidebarCloseKind close = uishell_sidebar_close_kind(node, status);
                  B32 engaged = close != UIShell_SidebarCloseKind_None && contains_2f32(row->rect, ui_mouse()) &&
                    !ui_any_ctx_menu_is_open() && !rd_drag_is_active();
                  if(engaged) UI_TextAlignment(UI_TextAlign_Center) UI_CornerRadius(3.f)
                  {
                    UI_Signal sig = uishell_sidebar_button(push_str8f(scratch.arena, "%S###close_%S",
                      close == UIShell_SidebarCloseKind_Destroy ? str8_lit("×") : str8_zero(), node_key));
                    if(close == UIShell_SidebarCloseKind_Detach) { ui_box_equip_custom_draw(sig.box, rd_workspace_detach_icon_draw, 0); }
                    if(ui_hovering(sig)) UI_Tooltip
                    {
                      ui_state->tooltip_anchor_key = sig.box->key;
                      ui_label(uishell_sidebar_close_label(close));
                      B32 unplaced = str8_match(kind, str8_lit("andamento.workspace"), 0);
                      ui_label(close == UIShell_SidebarCloseKind_Destroy ? str8_lit("The workspace and its layout are discarded.") :
                               unplaced ? str8_lit("Its layout reopens when its subject is seen again.") :
                               str8_lit("Its layout reopens from this entry."));
                    }
                    if(ui_clicked(sig)) { uishell_sidebar_close_workspace(ws, node, close); }
                  }
                  else { ui_label(uishell_sidebar_status_mark(node, status)); }
                }
              }
            }
            ui_spacer(ui_px(2.f, 1));
            ui_pop_parent();
            ui_spacer(ui_px(4.f, 1));
            ui_pop_parent();
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
        if(project_box)
        {
          ui_pop_flags();
          ui_pop_parent();
          ui_spacer(ui_px(project_padding, 1));
          ui_pop_parent();
          ui_spacer(ui_px(project_gap, 1));
        }
      }
      // Children get first refusal; the viewport consumes the remaining wheel
      // input. Header and sibling viewport geometry are outside this box.
      ui_signal_from_box(body);
      y += heights[n];
    }
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
    if(action != ANDAMENTO_NONE)
    {
      char *error = 0;
      B32 ok = uishell_sidebar_dispatch(state, action, &error);
      if(uishell_sidebar_result(state, ok, error)) { uishell_sidebar_effects(state, split); }
      uishell_sidebar_refresh(state);
      uishell_sidebar_save_display(state, split->owner_cfg);
      rd_request_frame();
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
uishell_sidebar_prune_regions(CFG_Node *container, UIShell_SectionPlacement *regions, U64 count, B32 reset)
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
      if(reset || uishell_sidebar_region_index(regions, count, key) == ANDAMENTO_NONE)
      { cfg_node_release(rd_state->cfg, c); removed = 1; }
    }
    else if(rd_dock_is_container(c))
    {
      B32 child_removed = uishell_sidebar_prune_regions(c, regions, count, reset);
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
  uishell_sidebar_prune_regions(sidebar, 0, 0, 1);
  uishell_sidebar_prune_regions(floating, 0, 0, 1);
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
  uishell_sidebar_prune_regions(root, regions, count, 0);
  uishell_sidebar_prune_regions(cfg_node_child_from_string(owner, str8_lit("floating_panels")), regions, count, 0);
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
      andamento_snapshot_node(state->snapshot, i, &node);
      capacity += !!node.is_section;
    }
    UIShell_SectionPlacement *regions = capacity ? push_array(arena, UIShell_SectionPlacement, capacity) : 0;
    for(U64 i = 0; i < nodes; i++)
    {
      AndamentoNode node = {0};
      andamento_snapshot_node(state->snapshot, i, &node);
      if(!node.is_section) { continue; }
      AndamentoRegionHints hints = {0};
      andamento_snapshot_region_hints(state->snapshot, i, &hints);
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
  CFG_Node *root = uishell_sidebar_reconcile_regions(split->owner_cfg, state->placement_regions, state->placement_count);
  if(state->pin_cfg_generation != cfg_change_gen())
  {
    uishell_sidebar_pin_deduplicate(split->owner_cfg, split->owner_cfg);
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
internal void
uishell_sidebar_size_panels(UIShell_ControlledSplit *split, UIShell_WorkspaceMount *mount, Rng2F32 rect)
{
  B32 manual = cfg_node_child_from_string(split->owner_cfg, str8_lit("sidebar_layout_sized")) != &cfg_nil_node;
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
      cfg_node_equip_stringf(rd_state->cfg, panel->cfg, "%f", share);
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
    collapsed[n] = cfg_node_child_from_string(view, str8_lit("section_collapsed")) != &cfg_nil_node;
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
  B32 chrome = ws->chrome_niche[RD_ChromeElementKind_NewWorkspace] == RD_ChromeNiche_SidebarActions ||
    ws->chrome_niche[RD_ChromeElementKind_OverviewToggle] == RD_ChromeNiche_SidebarActions ||
    ws->chrome_niche[RD_ChromeElementKind_RevealWorkspace] == RD_ChromeNiche_SidebarActions;
  return floor_f32(ui_top_font_size()*2.2f)*(1+chrome);
}

internal void
uishell_sidebar_footer_ui(Rng2F32 rect, UIShell_ControlledSplit *split)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  if(ws == &rd_nil_window_state || !ws->sidebar) { return; }
  UIShell_SidebarState *state = ws->sidebar;
  F32 row_height = floor_f32(ui_top_font_size()*2.2f), footer = row_height;
  F32 chrome_height = uishell_sidebar_footer_height(ws)-footer;
  F32 controls_height = chrome_height;
  B32 chrome_controls = chrome_height > 0;
  Vec2F32 dim = dim_2f32(rect);
  AndamentoText diagnostic = {0};
  if(state->snapshot && andamento_snapshot_diagnostic_count(state->snapshot))
  { andamento_snapshot_diagnostic(state->snapshot, 0, &diagnostic); }
  UI_Box *root;
  UI_Focus(UI_FocusKind_On) UI_Rect(rect)
  { root = ui_build_box_from_string(UI_BoxFlag_Clip|UI_BoxFlag_DefaultFocusNav, str8_lit("###sidebar_footer")); }
  UI_Parent(root)
  {
    if(chrome_controls)
    {
      UI_Rect(r2f32p(0, dim.y-chrome_height, dim.x, dim.y)) UI_ChildLayoutAxis(Axis2_X)
      {
        UI_Box *bar = ui_build_box_from_string(UI_BoxFlag_DrawSideTop, str8_lit("###workspace_chrome"));
        UI_Parent(bar) UI_PrefWidth(ui_em(2.25f, 1)) UI_PrefHeight(ui_pct(1, 1))
        {
          if(ws->chrome_niche[RD_ChromeElementKind_NewWorkspace] == RD_ChromeNiche_SidebarActions) { rd_chrome_build_new_workspace(split->owner_cfg); }
          ui_spacer(ui_pct(1, 0));
          if(ws->chrome_niche[RD_ChromeElementKind_RevealWorkspace] == RD_ChromeNiche_SidebarActions) { rd_chrome_build_reveal_workspace(split->owner_cfg); }
          if(ws->chrome_niche[RD_ChromeElementKind_OverviewToggle] == RD_ChromeNiche_SidebarActions) { rd_chrome_build_overview_toggle(ws); }
        }
      }
    }
    UI_Box *notice;
    UI_Rect(r2f32p(0, dim.y-controls_height-footer, dim.x, dim.y-controls_height)) UI_ChildLayoutAxis(Axis2_X)
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
  andamento_snapshot_node(state->snapshot, project_index, &project);
  andamento_snapshot_node(state->snapshot, project.parent, &section);
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
      andamento_snapshot_node(state->snapshot, project_index, &project);
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
  B32 coverage_ok = uishell_sidebar_coverage_diagnostics(state, &split);
  U64 before = split.inventory.count;
  size_t activate = ANDAMENTO_NONE;
  for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    if(andamento_snapshot_node(state->snapshot, i, &node) &&
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
    CFG_PanelTree tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
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
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
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
    tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
    ok = ok && tree.root->last->selected_tab->id == tools_id;
    uishell_sidebar_refresh(state);
    // Reveal prefers the nested tree occurrence over Attention, expands its
    // ancestor through fresh snapshot actions, and emits no host effects.
    U64 reveal_target = uishell_sidebar_reveal_target(state, created);
    AndamentoNode reveal_node = {0}, reveal_parent = {0};
    ok = ok && reveal_target != ANDAMENTO_NONE &&
      andamento_snapshot_node(state->snapshot, reveal_target, &reveal_node) &&
      andamento_snapshot_node(state->snapshot, reveal_node.parent, &reveal_parent) &&
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
    ok = ok && andamento_snapshot_node(state->snapshot, reveal_target, &reveal_node) &&
      andamento_snapshot_node(state->snapshot, reveal_node.parent, &reveal_parent) && !reveal_parent.collapsed;
    AndamentoEffects *reveal_effects = andamento_effects_take(state->core, 0);
    ok = ok && reveal_effects && andamento_effects_count(reveal_effects) == 0;
    andamento_effects_release(reveal_effects);
    state->reveal_workspace_id = 0;
    ok = ok && uishell_sidebar_motion_diagnostics(ws, &split, reveal_node.parent, created);
    // Reveal must open a docked View's saved collapse state, not just the
    // aggregate renderer's transient section state.
    reveal_target = uishell_sidebar_reveal_target(state, created);
    AndamentoNode reveal_section_node = {0};
    andamento_snapshot_node(state->snapshot, reveal_target, &reveal_section_node);
    while(!reveal_section_node.is_section && reveal_section_node.parent != ANDAMENTO_NONE)
    { andamento_snapshot_node(state->snapshot, reveal_section_node.parent, &reveal_section_node); }
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
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
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
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
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
    tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
    tools_tab = tree.root->last->tabs.last->v;
    tools_id = tools_tab->id;
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(tree.root->last->selected_tab, str8_lit("selected")));
    cfg_node_new(rd_state->cfg, tools_tab, str8_lit("selected"));
    // A new core instance rebinds persisted identities without duplicating them.
    uishell_sidebar_release(state);
    MemoryZeroStruct(state);
    state = uishell_sidebar_init(ws);
    uishell_sidebar_restore(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    ok = ok && split.inventory.count == before+1;
    B32 restored_live = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
      restored_live |= node.state == ANDAMENTO_LIVE;
    }
    tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
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
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
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
    tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
    ok = ok && split.inventory.count == before+1 &&
         ws->root_controlled_split_selected_workspace_id == workspace->id &&
         tree.root->last->selected_tab->id == tools_id &&
         tree.root->first->tabs.count == 1 && tree.root->last->tabs.count == 2;

    // A saved identity may arrive before its recipe (or lose the recipe).
    // Repeated ingress must not turn restoration into Inspect actions that
    // invalidate snapshots without binding the workspace. A later recipe
    // must still restore the existing layout rather than create another one.
    uishell_sidebar_release(state);
    MemoryZeroStruct(state);
    state = uishell_sidebar_init(ws);
    String8 unavailable_patch = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"vessel\",\"id\":\"multi\"}},\"source_id\":\"fixture\",\"set\":{},\"unset\":[\"action.primary.recipe\"]}");
    error = 0;
    B32 restore_ok = andamento_apply_patch_json(state->core, 0, uishell_sidebar_text(unavailable_patch), &error);
    restore_ok = uishell_sidebar_result(state, restore_ok, error);
    uishell_sidebar_observe(state, &split);
    uishell_sidebar_refresh(state);
    B32 unavailable_seen = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("multi"), 0))
      { unavailable_seen |= node.state == ANDAMENTO_LATENT && !node.openable; }
    }
    restore_ok = unavailable_seen && restore_ok;
    uishell_sidebar_restore(state, &split);
    state->inspection[0] = 0;
    for(U64 cycle = 0; cycle < 3; cycle++)
    {
      error = 0;
      B32 accepted = andamento_apply_patch_json(state->core, cycle+1, uishell_sidebar_text(unavailable_patch), &error);
      restore_ok = uishell_sidebar_result(state, accepted, error) && restore_ok;
      state->restored = 0; // The live ingress callback resets this flag.
      uishell_sidebar_refresh(state);
      AndamentoSnapshot *stable = state->snapshot;
      uishell_sidebar_restore(state, &split);
      restore_ok = restore_ok && state->snapshot == stable && !state->inspection[0];
    }
    AndamentoFact recipe = {0};
    recipe.key = uishell_sidebar_text(str8_lit("action.primary.recipe"));
    recipe.kind = ANDAMENTO_FACT_TEXT;
    recipe.text = uishell_sidebar_text(str8_lit("restore-existing-only"));
    error = 0;
    B32 accepted = andamento_apply_entity(state->core, 4, uishell_sidebar_text(str8_lit("vessel")),
      uishell_sidebar_text(str8_lit("multi")), uishell_sidebar_text(str8_lit("fixture")), &recipe, 1, &error);
    restore_ok = uishell_sidebar_result(state, accepted, error) && restore_ok;
    state->restored = 0;
    uishell_sidebar_refresh(state);
    uishell_sidebar_restore(state, &split);
    B32 recipe_restored = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("multi"), 0))
      { recipe_restored |= node.state == ANDAMENTO_LIVE && node.workspace_id == workspace->id; }
    }
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
    restore_ok = restore_ok && recipe_restored && split.inventory.count == before+1 &&
                 tree.root->last->selected_tab->id == tools_id;
    fprintf(stderr, "Sidebar unavailable restore diagnostics: %s\n", restore_ok ? "passed" : "FAILED");
    ok = restore_ok && ok;
  }
  ok = markers_ok && ok;
  ok = ok && selection_ok && coverage_ok;
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
    if(accepted) { state->restored = 0; state->error[0] = 0; applied = 1; }
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
  // Apply every queued patch and expiry tick before publishing one snapshot.
  // Andamento owns change detection, including action-only changes and leases.
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  {
    UIShell_SidebarState *state = uishell_sidebar_init(ws);
    if(state->core != 0)
    {
      uishell_sidebar_retry_display(state, cfg_node_from_id(ws->cfg_id), now);
    }
  }
}
