//- Andamento's records (ADR 0012; andamento.h, "named records"). Andamento
// owns the sidebar's logical state: display values, row collapse, sibling
// orders, and local sections, groups and pins (the `dashboard` record); and
// each workspace it knows, with its subject and the rows on its path as last
// seen (`workspace/<id>`). It hands them out as records, bytes private to
// it, and takes them back into a fresh core before the first observe;
// Wheelhouse only stores them, in the Dashboard directory
// (uishell_dashboard.c). Each window still has its own core, so only the
// first window's are the Dashboard's own:
//
//   <Dashboard>/                     the first window's
//     dashboard.record
//     workspace-<Workspace ID>.record
//   <Dashboard>/windows/<andamento_records id>/   any other window's
//
// A record is written when its generation changes: a second after the first
// change, through a temporary file renamed over the old one, and at once
// when the sidebar is released (its window closing, or quitting). Nothing
// here reads one: what Wheelhouse needs of the Dashboard's comes from
// andamento_dashboard_get, and of a workspace's from its own getters.

enum { UIShell_RecordsDelayMs = 1000 };

//- Files

// Bytes Andamento returned, copied into `arena` and freed.
internal String8
uishell_sidebar_bytes_copy(Arena *arena, AndamentoBytes bytes)
{
  String8 result = push_str8_copy(arena, str8(bytes.data, bytes.len));
  andamento_bytes_free(bytes);
  return result;
}

internal String8
uishell_sidebar_record_get(Arena *arena, Andamento *core, String8 name)
{
  AndamentoBytes bytes = {0};
  if(!core || !andamento_record_get(core, uishell_sidebar_text(name), &bytes, 0)) { return str8_zero(); }
  return uishell_sidebar_bytes_copy(arena, bytes);
}

// The core's record names.
internal String8List
uishell_sidebar_record_names(Arena *arena, Andamento *core)
{
  AndamentoBytes bytes = {0};
  String8 names = core && andamento_record_names(core, &bytes, 0) ? uishell_sidebar_bytes_copy(arena, bytes) : str8_zero();
  U8 newline = '\n';
  return str8_split(arena, names, &newline, 1, 0);
}

internal U64
uishell_sidebar_record_generation(Andamento *core, String8 name)
{
  return andamento_record_generation(core, uishell_sidebar_text(name), 0);
}

// The file a record is stored in: `dashboard.record`, `workspace-<id>.record`.
// Bytes a file name can't hold become '_' (no record names have them yet).
internal String8
uishell_sidebar_record_file(Arena *arena, String8 dir, String8 name)
{
  String8 file = push_str8_copy(arena, name);
  for(U64 i = 0; i < file.size; i++)
  {
    U8 c = file.str[i];
    if(c == '/') { file.str[i] = '-'; }
    else if(!char_is_alpha(c) && !char_is_digit(c, 10) && c != '-' && c != '.' && c != '_') { file.str[i] = '_'; }
  }
  return push_str8f(arena, "%S/%S.record", dir, file);
}

// The record a file in the directory holds; empty for any other file.
internal String8
uishell_sidebar_record_name(Arena *arena, String8 file)
{
  String8 workspace = str8_lit("workspace-"), extension = str8_lit(".record");
  if(!str8_match(str8_postfix(file, extension.size), extension, 0)) { return str8_zero(); }
  String8 stem = str8_chop(file, extension.size);
  if(str8_match(stem, str8_lit("dashboard"), 0)) { return push_str8_copy(arena, stem); }
  if(str8_match(str8_prefix(stem, workspace.size), workspace, 0) && stem.size > workspace.size)
  { return push_str8f(arena, "workspace/%S", str8_skip(stem, workspace.size)); }
  return str8_zero();
}

// The window's records directory, giving the window an id for it when it
// has none; empty without a Dashboard.
internal String8
uishell_sidebar_records_dir(Arena *arena, CFG_Node *window)
{
  if(!uishell_dashboard.dir.size || window == &cfg_nil_node) { return str8_zero(); }
  CFG_Node *id = cfg_node_child_from_string(window, str8_lit("andamento_records"));
  if(id->first->string.size == 0)
  {
    Temp scratch = scratch_begin(&arena, 1);
    id = cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("andamento_records"));
    cfg_node_new_replace(rd_state->cfg, id, uishell_string_from_workspace_id(scratch.arena, uishell_workspace_id_make()));
    scratch_end(scratch);
  }
  return uishell_dashboard_records_dir(arena, window, id->first->string);
}

internal B32
uishell_sidebar_record_write(String8 path, String8 data)
{
  Temp scratch = scratch_begin(0, 0);
  String8 temp = push_str8f(scratch.arena, "%S.temp", path);
  // Windows won't rename over an existing file.
  B32 ok = write_data_to_file_path(temp, data) &&
    (move_file_path(path, temp) || (delete_file_at_path(path) && move_file_path(path, temp)));
  scratch_end(scratch);
  return ok;
}

//- Saving

internal UIShell_SidebarRecord *
uishell_sidebar_record_written(UIShell_SidebarState *state, String8 name)
{
  for(UIShell_SidebarRecord *r = state->records; r; r = r->next) { if(str8_match(r->name, name, 0)) { return r; } }
  return 0;
}

// Notes `name` as written at `generation`; 0 forgets it.
internal void
uishell_sidebar_record_note(UIShell_SidebarState *state, String8 name, U64 generation)
{
  UIShell_SidebarRecord **link = &state->records;
  while(*link && !str8_match((*link)->name, name, 0)) { link = &(*link)->next; }
  if(*link) { if(generation) { (*link)->generation = generation; } else { *link = (*link)->next; } return; }
  if(!generation) { return; }
  UIShell_SidebarRecord *record = push_array(state->records_arena, UIShell_SidebarRecord, 1);
  record->name = push_str8_copy(state->records_arena, name);
  record->generation = generation;
  record->next = state->records;
  state->records = record;
}

// Writes the records whose generation moved since they were written, and
// deletes those Andamento no longer has (a forgotten workspace's), once due:
// a second after a change was first seen, or with `flush`, now. Generations
// are read only when one may have moved: after a new snapshot, when the
// names changed, or while due.
internal void
uishell_sidebar_records_save(UIShell_SidebarState *state, U64 now, B32 flush)
{
  if(!state->core || !state->records_dir.size) { return; }
  Temp scratch = scratch_begin(0, 0);
  String8List names = uishell_sidebar_record_names(scratch.arena, state->core);
  U64 present = 0, written = 0;
  for(String8Node *n = names.first; n; n = n->next) { present += uishell_sidebar_record_written(state, n->string) != 0; }
  for(UIShell_SidebarRecord *r = state->records; r; r = r->next) { written++; }
  B32 changed = present != written || present != names.node_count;
  if(!changed && !flush && !state->records_due && state->records_snapshot_count == state->snapshot_count)
  { scratch_end(scratch); return; }
  state->records_snapshot_count = state->snapshot_count;
  U64 *generations = push_array(scratch.arena, U64, names.node_count);
  U64 index = 0;
  for(String8Node *n = names.first; n; n = n->next, index++)
  {
    generations[index] = uishell_sidebar_record_generation(state->core, n->string);
    UIShell_SidebarRecord *record = uishell_sidebar_record_written(state, n->string);
    changed |= !record || record->generation != generations[index];
  }
  if(!changed) { state->records_due = 0; }
  else if(!flush && !state->records_due) { state->records_due = now+UIShell_RecordsDelayMs; }
  // A frame comes when it's due, without input.
  if(changed && !flush && now < state->records_due) { rd_request_frame(); }
  if(changed && (flush || now >= state->records_due))
  {
    state->records_due = 0;
    uishell_dashboard_make_directories(state->records_dir);
    index = 0;
    for(String8Node *n = names.first; n; n = n->next, index++)
    {
      UIShell_SidebarRecord *record = uishell_sidebar_record_written(state, n->string);
      if(record && record->generation == generations[index]) { continue; }
      String8 text = uishell_sidebar_record_get(scratch.arena, state->core, n->string);
      if(text.size && uishell_sidebar_record_write(uishell_sidebar_record_file(scratch.arena, state->records_dir, n->string), text))
      { uishell_sidebar_record_note(state, n->string, generations[index]); }
    }
    String8List gone = {0};
    for(UIShell_SidebarRecord *r = state->records; r; r = r->next)
    {
      B32 kept = 0;
      for(String8Node *n = names.first; n && !kept; n = n->next) { kept = str8_match(n->string, r->name, 0); }
      if(!kept) { str8_list_push(scratch.arena, &gone, r->name); }
    }
    for(String8Node *n = gone.first; n; n = n->next)
    {
      String8 path = uishell_sidebar_record_file(scratch.arena, state->records_dir, n->string);
      if(delete_file_at_path(path) || !file_path_exists(path)) { uishell_sidebar_record_note(state, n->string, 0); }
    }
  }
  scratch_end(scratch);
}

//- The Dashboard's local entities and saved orders (andamento_dashboard_get)

// Andamento's Dashboard, read again when the dashboard record's generation
// moves.
internal AndamentoDashboard *
uishell_sidebar_dashboard(UIShell_SidebarState *state)
{
  if(!state->core) { return 0; }
  U64 generation = uishell_sidebar_record_generation(state->core, str8_lit("dashboard"));
  if(state->dashboard && state->dashboard->generation == generation) { return state->dashboard; }
  andamento_dashboard_free(state->dashboard);
  state->dashboard = andamento_dashboard_get(state->core, 0);
  return state->dashboard;
}

// Whether Andamento keeps an order for the sibling run keyed `loop`, which
// offers Reset order.
internal B32
uishell_sidebar_order_saved(UIShell_SidebarState *state, String8 loop)
{
  AndamentoDashboard *dashboard = loop.size ? uishell_sidebar_dashboard(state) : 0;
  for(U64 i = 0; dashboard && i < dashboard->order_count; i++)
  { if(str8_match(uishell_sidebar_string(dashboard->orders[i].loop_key), loop, 0)) { return 1; } }
  return 0;
}

//- Loading

internal AndamentoLocalFact *
uishell_sidebar_dashboard_fact(AndamentoLocalEntity *local, String8 key)
{
  for(U64 i = 0; i < local->fact_count; i++)
  { if(str8_match(uishell_sidebar_string(local->facts[i].key), key, 0)) { return (AndamentoLocalFact *)&local->facts[i]; } }
  return 0;
}

internal String8
uishell_sidebar_dashboard_text(AndamentoLocalEntity *local, String8 key)
{
  AndamentoLocalFact *fact = uishell_sidebar_dashboard_fact(local, key);
  return fact && fact->kind == ANDAMENTO_FACT_TEXT ? uishell_sidebar_string(fact->text) : str8_zero();
}

internal B32
uishell_sidebar_dashboard_true(AndamentoLocalEntity *local, String8 key)
{
  AndamentoLocalFact *fact = uishell_sidebar_dashboard_fact(local, key);
  return fact && fact->kind == ANDAMENTO_FACT_BOOL && fact->integer != 0;
}

// An entity-reference fact's entity; an empty one without it.
internal AndamentoEntity
uishell_sidebar_dashboard_entity(AndamentoLocalEntity *local, String8 key)
{
  AndamentoLocalFact *fact = uishell_sidebar_dashboard_fact(local, key);
  return fact && fact->kind == ANDAMENTO_FACT_ENTITY ? fact->entity : (AndamentoEntity){0};
}

internal U64
uishell_sidebar_local_position(AndamentoLocalEntity *local)
{
  U64 position = max_U64;
  try_u64_from_str8_c_rules(uishell_sidebar_dashboard_text(local, str8_lit(".position")), &position);
  return position;
}

// Adds to `parent`, in position order, a `name` node for each local entity
// of `kind` whose `link` fact names `owner` (each one, without a link), as
// uishell_sidebar_local_entities publishes them.
internal void
uishell_sidebar_local_seed(AndamentoDashboard *dashboard, CFG_Node *parent, String8 name, String8 kind, String8 link, String8 owner)
{
  Temp scratch = scratch_begin(0, 0);
  U64 found = 0;
  AndamentoLocalEntity **matches = push_array(scratch.arena, AndamentoLocalEntity *, dashboard->local_count+1);
  for(U64 i = 0; i < dashboard->local_count; i++)
  {
    AndamentoLocalEntity *n = (AndamentoLocalEntity *)&dashboard->locals[i];
    if(!str8_match(uishell_sidebar_string(n->entity.kind), kind, 0)) { continue; }
    if(link.size && !str8_match(uishell_sidebar_string(uishell_sidebar_dashboard_entity(n, link).id), owner, 0)) { continue; }
    // Insertion keeps them in position order, ties in Andamento's order.
    U64 at = found++;
    for(; at > 0 && uishell_sidebar_local_position(matches[at-1]) > uishell_sidebar_local_position(n); at--) { matches[at] = matches[at-1]; }
    matches[at] = n;
  }
  for(U64 i = 0; i < found; i++)
  {
    AndamentoLocalEntity *local = matches[i];
    CFG_Node *node = cfg_node_new(rd_state->cfg, parent, name);
    String8 id = uishell_sidebar_string(local->entity.id);
    if(str8_match(kind, str8_lit(".ref"), 0))
    {
      uishell_sidebar_local_set_field(node, str8_lit("ghost"), id);
      AndamentoEntity target = uishell_sidebar_dashboard_entity(local, str8_lit(".target"));
      if(target.kind.len) { uishell_sidebar_pin_set_entity(node, target); }
      String8 fields[] = {str8_lit("label"), str8_lit("source")}, facts[] = {str8_lit(".label"), str8_lit(".source")};
      for(U64 f = 0; f < ArrayCount(fields); f++)
      {
        String8 value = uishell_sidebar_dashboard_text(local, facts[f]);
        if(value.size) { uishell_sidebar_local_set_field(node, fields[f], value); }
      }
      if(uishell_sidebar_dashboard_true(local, str8_lit(".compact"))) { cfg_node_new(rd_state->cfg, node, str8_lit("compact")); }
      continue;
    }
    uishell_sidebar_local_set_field(node, str8_lit("id"), id);
    String8 label = uishell_sidebar_dashboard_text(local, str8_lit("display.label"));
    if(str8_match(kind, str8_lit(".group"), 0))
    {
      uishell_sidebar_local_set_field(node, str8_lit("label"), label);
      if(uishell_sidebar_dashboard_true(local, str8_lit(".default"))) { cfg_node_new(rd_state->cfg, node, str8_lit("default")); }
      uishell_sidebar_local_seed(dashboard, node, str8_lit("card"), str8_lit(".ref"), str8_lit(".group"), id);
      continue;
    }
    uishell_sidebar_local_seed(dashboard, node, str8_lit("group"), str8_lit(".group"), str8_lit(".section"), id);
    // A section is published under its title, a name of its own only when
    // that differs from its groups' names.
    if(label.size && !str8_match(label, uishell_sidebar_local_title(scratch.arena, node), 0))
    { uishell_sidebar_local_set_field(node, str8_lit("label"), label); }
  }
  scratch_end(scratch);
}

// Makes the window's working copy of its local sections, groups and pins
// from the Dashboard Andamento restored.
internal void
uishell_sidebar_local_load(UIShell_SidebarState *state, CFG_Node *window)
{
  cfg_node_release(rd_state->cfg, uishell_sidebar_local_tree(window));
  AndamentoDashboard *dashboard = uishell_sidebar_dashboard(state);
  if(dashboard && dashboard->local_count)
  {
    uishell_sidebar_local_seed(dashboard, uishell_sidebar_local_tree_alloc(window), str8_lit("section"), str8_lit(".section"),
                               str8_zero(), str8_zero());
  }
  // Andamento holds what was last published, so publishing removes any
  // entity the copy left out (one whose section or group had gone).
  uishell_sidebar_local_published_clear(state);
  for(U64 i = 0; dashboard && i < dashboard->local_count; i++)
  {
    Temp scratch = scratch_begin(0, 0);
    AndamentoEntity e = dashboard->locals[i].entity;
    uishell_sidebar_local_published_add(state, push_str8f(scratch.arena, "%S/%S", uishell_sidebar_string(e.kind), uishell_sidebar_string(e.id)), 0);
    scratch_end(scratch);
  }
}

// Imports the window's records into its new core, before its first observe:
// the dashboard first, then each workspace's (which registers it and binds
// it to its subject). A record that doesn't import (a newer version's, say)
// is reported, and nothing is saved over it this session.
internal void
uishell_sidebar_records_load(UIShell_SidebarState *state, CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  if(!state->records_arena) { state->records_arena = arena_alloc(); }
  arena_clear(state->records_arena);
  state->records = 0;
  state->records_due = 0;
  String8 dir = uishell_sidebar_records_dir(scratch.arena, window);
  String8List names = {0}, files = {0};
  FileIter *it = dir.size ? file_iter_begin(scratch.arena, dir, FileIterFlag_SkipFolders) : 0;
  for(FileInfo info = {0}; it && file_iter_next(scratch.arena, it, &info);)
  {
    String8 name = uishell_sidebar_record_name(scratch.arena, info.name);
    if(!name.size) { continue; }
    B32 first = str8_match(name, str8_lit("dashboard"), 0);
    if(first) { str8_list_push_front(scratch.arena, &names, name); str8_list_push_front(scratch.arena, &files, info.name); }
    else { str8_list_push(scratch.arena, &names, name); str8_list_push(scratch.arena, &files, info.name); }
  }
  if(it) { file_iter_end(it); }
  B32 failed = 0;
  for(String8Node *name = names.first, *file = files.first; name && file; name = name->next, file = file->next)
  {
    String8 text = data_from_file_path(scratch.arena, push_str8f(scratch.arena, "%S/%S", dir, file->string));
    char *error = 0;
    if(andamento_record_put(state->core, uishell_sidebar_text(name->string), uishell_sidebar_text(text), &error))
    { uishell_sidebar_record_note(state, name->string, uishell_sidebar_record_generation(state->core, name->string)); }
    else
    {
      failed = 1;
      uishell_sidebar_set_error(state, push_str8f(scratch.arena, "Sidebar record %S/%S not restored: %s", dir, file->string,
                                                  error ? error : "unknown error"));
    }
    andamento_string_free(error);
  }
  state->records_dir = failed ? str8_zero() : push_str8_copy(state->records_arena, dir);
  // A workspace the window no longer has (deleted while an older build ran)
  // is forgotten, and its record goes.
  String8List recorded = uishell_sidebar_record_names(scratch.arena, state->core);
  String8 prefix = str8_lit("workspace/");
  for(String8Node *n = recorded.first; n; n = n->next)
  {
    UIShell_WorkspaceId id = {0};
    if(!str8_match(str8_prefix(n->string, prefix.size), prefix, 0) ||
       !uishell_workspace_id_from_string(str8_skip(n->string, prefix.size), &id)) { continue; }
    // The window's own layout has an ID only once it is a workspace.
    B32 kept = cfg_node_child_from_string(window, str8_lit("workspace_id")) != &cfg_nil_node &&
      uishell_workspace_id_match(uishell_workspace_id_from_cfg(window), id);
    for(CFG_Node *c = window->first; c != &cfg_nil_node && !kept; c = c->next)
    {
      kept = (str8_match(c->string, str8_lit("workspace"), 0) || str8_match(c->string, str8_lit("detached_workspace"), 0)) &&
        uishell_workspace_id_match(uishell_workspace_id_from_cfg(c), id);
    }
    if(!kept) { andamento_workspace_forget(state->core, uishell_sidebar_workspace(id), 0); }
  }
  uishell_sidebar_local_load(state, window);
  scratch_end(scratch);
}
