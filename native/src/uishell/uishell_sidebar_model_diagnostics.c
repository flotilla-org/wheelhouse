// The sidebar model (uishell_sidebar_model.c) against fresh snapshots: a
// model built only from deltas lists, after every change, what a snapshot
// taken then lists, node for node, with fields, details and controls. The
// changes are those a sidebar sees: facts, renames that reorder, collapse,
// activation and its failure, display variables, a template that adds and
// moves sections, a stale provider and a retracted one. It runs twice:
// once compacting on every apply, once as production does.

internal B32
uishell_sidebar_model_text_match(WheelhouseText a, WheelhouseText b)
{
  return str8_match(uishell_sidebar_string(a), uishell_sidebar_string(b), 0);
}

internal B32
uishell_sidebar_model_field_match(WheelhouseField a, WheelhouseField b)
{
  return uishell_sidebar_model_text_match(a.text, b.text) && a.class_ == b.class_ &&
    a.has_priority == b.has_priority && (!a.has_priority || a.priority == b.priority);
}

internal B32
uishell_sidebar_model_entity_match(WheelhouseEntity a, WheelhouseEntity b)
{
  return uishell_sidebar_model_text_match(a.provider, b.provider) && uishell_sidebar_model_text_match(a.kind, b.kind) &&
    uishell_sidebar_model_text_match(a.id, b.id);
}

// Where the model and a fresh snapshot differ, or empty.
internal String8
uishell_sidebar_model_mismatch(Arena *arena, UIShell_SidebarModel *model, WheelhouseCore *core)
{
  WheelhouseSnapshot *s = wheelhouse_snapshot_acquire(core, 0);
  if(!s) { return str8_lit("no snapshot"); }
  String8 result = str8_zero();
  U64 count = 0;
  UIShell_SidebarModelNode **nodes = uishell_sidebar_model_preorder(model, &count);
  if(count != wheelhouse_snapshot_node_count(s))
  { result = push_str8f(arena, "%I64u nodes, the snapshot %I64u", count, (U64)wheelhouse_snapshot_node_count(s)); }
  for(U64 i = 0; !result.size && i < count; i++)
  {
    UIShell_SidebarModelNode *m = nodes[i];
    WheelhouseStreamNode v = m->view;
    WheelhouseNode n = {0};
    wheelhouse_snapshot_node(s, i, &n);
    // A section's snapshot key and label are its name.
    WheelhouseText key = v.is_section ? uishell_sidebar_text(m->section->name) : v.key;
    U64 parent = m->parent ? m->parent->index : WHEELHOUSE_NONE;
    B32 same = v.is_section == n.is_section && uishell_sidebar_model_text_match(key, n.key) && parent == n.parent &&
      uishell_sidebar_model_text_match(v.label, n.label) && uishell_sidebar_model_entity_match(v.entity, n.entity) &&
      v.stale == n.stale && uishell_sidebar_model_text_match(v.layout, n.layout) &&
      uishell_sidebar_model_text_match(v.form, n.form) && uishell_sidebar_model_text_match(v.loop_key, n.loop_key) &&
      v.state == n.state && MemoryMatchStruct(&v.workspace_id, &n.workspace_id) && v.selected == n.selected &&
      v.openable == n.openable && v.collapsed == n.collapsed && v.pinned == n.pinned &&
      uishell_sidebar_model_text_match(v.default_host, n.default_host) && v.has_order == n.has_order &&
      (!v.has_order || v.order == n.order) && v.field_count == n.field_count && v.detail_count == n.detail_count &&
      v.control_count == n.control_count;
    for(U64 f = 0; same && f < v.field_count; f++)
    {
      WheelhouseField field = {0};
      same = wheelhouse_snapshot_field(s, n.first_field+f, &field) && uishell_sidebar_model_field_match(m->fields[f], field);
    }
    for(U64 f = 0; same && f < v.detail_count; f++)
    {
      WheelhouseField field = {0};
      same = wheelhouse_snapshot_field(s, n.first_detail+f, &field) && uishell_sidebar_model_field_match(m->details[f], field);
    }
    for(U64 c = 0; same && c < v.control_count; c++)
    {
      WheelhouseControl a = uishell_sidebar_model_control(model, m, c), b = {0};
      same = wheelhouse_snapshot_control(s, n.first_control+c, &b) && a.kind == b.kind &&
        uishell_sidebar_model_text_match(a.label, b.label) && uishell_sidebar_model_text_match(a.glyph, b.glyph) &&
        a.value_kind == b.value_kind && a.checked == b.checked && uishell_sidebar_model_text_match(a.value, b.value) &&
        uishell_sidebar_model_text_match(a.variable, b.variable) && a.persist == b.persist;
    }
    if(!same) { result = push_str8f(arena, "node %I64u (%S) differs", i, uishell_sidebar_string(n.label)); }
  }
  // The snapshot's diagnostics end with the activation errors.
  if(!result.size && wheelhouse_snapshot_diagnostic_count(s) != model->diagnostic_count+model->error_count)
  { result = push_str8f(arena, "%I64u diagnostics and errors, the snapshot %I64u", model->diagnostic_count+model->error_count, (U64)wheelhouse_snapshot_diagnostic_count(s)); }
  for(U64 i = 0; !result.size && i < model->diagnostic_count; i++)
  {
    WheelhouseText text = {0};
    if(!wheelhouse_snapshot_diagnostic(s, i, &text) || !uishell_sidebar_model_text_match(text, model->diagnostics[i]))
    { result = push_str8f(arena, "diagnostic %I64u differs", i); }
  }
  for(U64 i = 0; !result.size && i < model->error_count; i++)
  {
    WheelhouseText text = {0};
    WheelhouseActivationError e = model->errors[i];
    String8 expected = push_str8f(arena, "%S:%S: %S", uishell_sidebar_string(e.entity.kind), uishell_sidebar_string(e.entity.id), uishell_sidebar_string(e.message));
    if(!wheelhouse_snapshot_diagnostic(s, model->diagnostic_count+i, &text) || !str8_match(uishell_sidebar_string(text), expected, 0))
    { result = push_str8f(arena, "error %I64u differs", i); }
  }
  wheelhouse_snapshot_release(s);
  return result;
}

// The line of `text` that starts with `start`, with its newline.
internal String8
uishell_sidebar_model_line(String8 text, String8 start)
{
  U64 at = str8_find_needle(text, 0, start, 0);
  if(at >= text.size || (at > 0 && text.str[at-1] != '\n')) { return str8_zero(); }
  U64 end = str8_find_needle(text, at, str8_lit("\n"), 0);
  return str8(text.str+at, Min(end+1, text.size)-at);
}

typedef struct UIShell_SidebarModelRun UIShell_SidebarModelRun;
struct UIShell_SidebarModelRun
{
  WheelhouseCore *core;
  U64 subscription;
  UIShell_SidebarModel model;
  U64 steps;
  U64 failures;
  U64 moves;
};

internal void
uishell_sidebar_model_follow(Arena *arena, UIShell_SidebarModelRun *run, char *step)
{
  run->steps++;
  if(!wheelhouse_changed_since(run->core, run->subscription, 0)) { return; }
  WheelhouseDelta *delta = wheelhouse_poll(run->core, run->subscription, 0);
  if(!delta) { return; }
  for(U64 i = 0; i < wheelhouse_delta_op_count(delta); i++)
  {
    WheelhouseDeltaOp op = {0}; wheelhouse_delta_op(delta, i, &op);
    if(op.kind != WHEELHOUSE_OP_CHILDREN && op.kind != WHEELHOUSE_OP_SECTIONS) { continue; }
    for(U64 j = 0; j < op.count; j++)
    { WheelhouseListOp l = {0}; wheelhouse_delta_list_op(delta, op.first+j, &l); run->moves += l.kind == WHEELHOUSE_LIST_MOVE; }
  }
  B32 applied = uishell_sidebar_model_apply(&run->model, delta);
  wheelhouse_delta_release(delta);
  String8 mismatch = applied ? uishell_sidebar_model_mismatch(arena, &run->model, run->core) : str8_lit("the delta doesn't fit");
  if(mismatch.size) { fprintf(stderr, "FAIL sidebar model after %s: %.*s\n", step, str8_varg(mismatch)); run->failures++; }
}

internal void
uishell_sidebar_model_patch(UIShell_SidebarModelRun *run, U64 now, String8 patch)
{
  wheelhouse_apply_patch_json(run->core, now, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(patch), 0);
}

internal void
uishell_sidebar_model_rename(Arena *arena, UIShell_SidebarModelRun *run, char *kind, char *id, String8 label)
{
  uishell_sidebar_model_patch(run, 0, push_str8f(arena, "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"%s\",\"id\":\"%s\"}},\"source_id\":\"fixture\",\"set\":{\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"%S\"}}},\"unset\":[]}", kind, id, label));
  uishell_sidebar_model_follow(arena, run, "a rename");
}

internal void
uishell_sidebar_model_intent(Arena *arena, UIShell_SidebarModelRun *run, U32 kind, WheelhouseText key, WheelhouseText name, char *step)
{
  WheelhouseIntent intent = {.kind = kind, .key = key, .name = name};
  if(wheelhouse_apply_intent(run->core, &intent, 0, 0) != WHEELHOUSE_INTENT_ACCEPTED)
  { fprintf(stderr, "FAIL sidebar model: %s was not accepted\n", step); run->failures++; }
  // Activations fail, as a browser's would: the errors arrive as deltas.
  WheelhouseEffects *effects = wheelhouse_effects_take(run->core, 0);
  for(U64 i = 0; i < wheelhouse_effects_count(effects); i++)
  {
    WheelhouseEffect effect = {0};
    wheelhouse_effects_get(effects, i, &effect);
    if(effect.kind == WHEELHOUSE_EFFECT_MATERIALIZE || effect.kind == WHEELHOUSE_EFFECT_FOCUS)
    { wheelhouse_complete(run->core, effect.request_id, WHEELHOUSE_COMPLETE_ERROR, (WheelhouseWorkspaceId){0}, uishell_sidebar_text(str8_lit("no workspaces here")), 0); }
  }
  wheelhouse_effects_release(effects);
  uishell_sidebar_model_follow(arena, run, step);
}

internal U64
uishell_sidebar_model_scenario(B32 compact_always)
{
  Temp scratch = scratch_begin(0, 0);
  U64 saved_floor = uishell_sidebar_model_compact_floor;
  uishell_sidebar_model_compact_floor = compact_always ? 0 : saved_floor;
  String8 config = str8_cstring((char *)uishell_sidebar_daily_config);
  UIShell_SidebarModelRun run = {0};
  run.core = wheelhouse_create(config.str, config.size, 0);
  if(!run.core || !wheelhouse_subscribe(run.core, &run.subscription, 0))
  { fprintf(stderr, "FAIL sidebar model: no core\n"); scratch_end(scratch); return 1; }
  String8 patches = str8_cstring((char *)uishell_sidebar_fixture_patches);
  for(U64 start = 0; start < patches.size;)
  {
    U64 end = start;
    while(end < patches.size && patches.str[end] != '\n') { end++; }
    if(end > start) { uishell_sidebar_model_patch(&run, 0, str8(patches.str+start, end-start)); }
    start = end+1;
  }
  uishell_sidebar_model_follow(scratch.arena, &run, "the fixture");
  U64 count = 0;
  uishell_sidebar_model_preorder(&run.model, &count);
  if(count < 5) { fprintf(stderr, "FAIL sidebar model: the fixture gave %llu nodes\n", (unsigned long long)count); run.failures++; }

  // Collapse every node with children by its key, then open them again.
  for(U64 pass = 0; pass < 2; pass++)
  {
    UIShell_SidebarModelNode **nodes = uishell_sidebar_model_preorder(&run.model, &count);
    Temp temp = temp_begin(scratch.arena);
    String8 *keys = push_array(temp.arena, String8, count);
    U64 key_count = 0;
    for(U64 i = 0; i < count; i++)
    { if(!nodes[i]->view.is_section && nodes[i]->children.count) { keys[key_count++] = push_str8_copy(temp.arena, uishell_sidebar_string(nodes[i]->view.key)); } }
    for(U64 i = 0; i < key_count; i++)
    { uishell_sidebar_model_intent(temp.arena, &run, WHEELHOUSE_INTENT_TOGGLE_PLACEMENT, uishell_sidebar_text(keys[i]), (WheelhouseText){0}, "a toggle"); }
    temp_end(temp);
  }
  // Activate a few latent nodes; their failures show.
  {
    UIShell_SidebarModelNode **nodes = uishell_sidebar_model_preorder(&run.model, &count);
    Temp temp = temp_begin(scratch.arena);
    String8 *keys = push_array(temp.arena, String8, 3);
    U64 key_count = 0;
    for(U64 i = 0; i < count && key_count < 3; i++)
    { if(nodes[i]->view.state == WHEELHOUSE_LATENT && nodes[i]->view.openable) { keys[key_count++] = push_str8_copy(temp.arena, uishell_sidebar_string(nodes[i]->view.key)); } }
    for(U64 i = 0; i < key_count; i++)
    { uishell_sidebar_model_intent(temp.arena, &run, WHEELHOUSE_INTENT_ACTIVATE_PLACEMENT, uishell_sidebar_text(keys[i]), (WheelhouseText){0}, "an activation"); }
    if(!key_count || !run.model.error_count) { fprintf(stderr, "FAIL sidebar model: no activation failed\n"); run.failures++; }
    temp_end(temp);
  }
  // Every boolean display variable, on and back.
  for(U64 pass = 0; pass < 2; pass++)
  for(U64 i = 0; i < run.model.variable_count; i++)
  {
    if(run.model.variables[i].value_kind != WHEELHOUSE_VALUE_BOOL) { continue; }
    String8 name = push_str8_copy(scratch.arena, uishell_sidebar_string(run.model.variables[i].name));
    uishell_sidebar_model_intent(scratch.arena, &run, WHEELHOUSE_INTENT_TOGGLE_DISPLAY_VARIABLE, (WheelhouseText){0}, uishell_sidebar_text(name), "a display variable");
  }
  // Renames that sort nodes to the end of their lists move them.
  char *ids[] = {"v", "multi", "chip-v"};
  for(U64 i = 0; i < ArrayCount(ids); i++) { uishell_sidebar_model_rename(scratch.arena, &run, "vessel", ids[i], push_str8f(scratch.arena, "~%I64u", i)); }
  for(U64 i = 0; i < ArrayCount(ids); i++) { uishell_sidebar_model_rename(scratch.arena, &run, "vessel", ids[i], push_str8f(scratch.arena, "!%I64u", i)); }
  // A template whose attention placement is a tree: sections change and
  // two share a placement, so their nodes share keys.
  String8 needle = str8_lit("placement=\"attention\"");
  U64 at = str8_find_needle(config, 0, needle, 0);
  if(at < config.size)
  {
    String8 tree = push_str8f(scratch.arena, "%Splacement=\"tree\"%S", str8_prefix(config, at), str8_skip(config, at+needle.size));
    wheelhouse_configure(run.core, uishell_sidebar_text(tree), 0);
    uishell_sidebar_model_follow(scratch.arena, &run, "a new template");
    wheelhouse_configure(run.core, uishell_sidebar_text(config), 0);
    uishell_sidebar_model_follow(scratch.arena, &run, "the template again");
  }
  else { fprintf(stderr, "FAIL sidebar model: the template has no attention placement\n"); run.failures++; }
  // One whose tree region comes after attention and which has no sessions
  // region: sections move, go, and come back.
  {
    String8 tree = uishell_sidebar_model_line(config, str8_lit("region \"tree\""));
    String8 sessions = uishell_sidebar_model_line(config, str8_lit("region \"sessions\""));
    String8 attention = uishell_sidebar_model_line(config, str8_lit("region \"attention\""));
    if(tree.size && sessions.size && attention.size && tree.str < sessions.str && sessions.str < attention.str)
    {
      U64 t = tree.str-config.str, a_end = attention.str+attention.size-config.str;
      String8 moved = push_str8f(scratch.arena, "%S%S%S%S", str8_prefix(config, t),
        str8(attention.str, attention.size), tree, str8_skip(config, a_end));
      wheelhouse_configure(run.core, uishell_sidebar_text(moved), 0);
      uishell_sidebar_model_follow(scratch.arena, &run, "a template with sections moved and gone");
      if(uishell_sidebar_model_section(&run.model, str8_lit("sessions"))) { fprintf(stderr, "FAIL sidebar model: the sessions section stayed\n"); run.failures++; }
      wheelhouse_configure(run.core, uishell_sidebar_text(config), 0);
      uishell_sidebar_model_follow(scratch.arena, &run, "the template again");
    }
    else { fprintf(stderr, "FAIL sidebar model: the template's regions changed\n"); run.failures++; }
  }
  // A provider that drops, comes back, and retracts its facts.
  WheelhouseText provider = uishell_sidebar_text(uishell_sidebar_host_provider);
  wheelhouse_provider_set_stale(run.core, provider, 1, 0);
  uishell_sidebar_model_follow(scratch.arena, &run, "a stale provider");
  wheelhouse_provider_set_stale(run.core, provider, 0, 0);
  uishell_sidebar_model_follow(scratch.arena, &run, "a fresh provider");
  wheelhouse_provider_retract(run.core, provider, 0);
  uishell_sidebar_model_follow(scratch.arena, &run, "a retracted provider");
  for(U64 start = 0; start < patches.size;)
  {
    U64 end = start;
    while(end < patches.size && patches.str[end] != '\n') { end++; }
    if(end > start) { uishell_sidebar_model_patch(&run, 1, str8(patches.str+start, end-start)); }
    start = end+1;
  }
  uishell_sidebar_model_follow(scratch.arena, &run, "the fixture again");
  if(!run.moves) { fprintf(stderr, "FAIL sidebar model: nothing moved\n"); run.failures++; }
  if(compact_always && run.model.compactions < run.steps/2) { fprintf(stderr, "FAIL sidebar model: %llu compactions in %llu steps\n", (unsigned long long)run.model.compactions, (unsigned long long)run.steps); run.failures++; }
  uishell_sidebar_model_release(&run.model);
  wheelhouse_destroy(run.core);
  uishell_sidebar_model_compact_floor = saved_floor;
  scratch_end(scratch);
  return run.failures;
}

internal B32
uishell_sidebar_model_diagnostics(void)
{
  U64 failures = uishell_sidebar_model_scenario(1)+uishell_sidebar_model_scenario(0);
  fprintf(stderr, "Sidebar model diagnostics: %llu failures\n", (unsigned long long)failures);
  return failures == 0;
}
