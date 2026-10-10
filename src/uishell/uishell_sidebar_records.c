//- Andamento's records (ADR 0012; andamento.h, "named records"). Andamento
// owns the sidebar's logical state: display values, row collapse, sibling
// orders, and local sections, groups and pins (the `dashboard` record); and
// each workspace it knows, with its subject and the rows on its path as last
// seen (`workspace/<id>`). It exports them as records and imports them into
// a fresh core before the first observe; Wheelhouse only stores them. Each
// window still has its own core, so each has its own directory, beside the
// user file until Dashboard directories replace it (roadmap, step 4):
//
//   <user file>.andamento/<the window's andamento_records id>/
//     dashboard.kdl
//     workspace-<Workspace ID>.kdl
//
// A record is written when its generation changes: a second after the first
// change, through a temporary file renamed over the old one, and at once
// when the sidebar is released (its window closing, or quitting).

enum { UIShell_RecordsDelayMs = 1000 };

//- A reader for records' KDL (KDL 1.0, as Andamento writes it), for what
// Andamento has no getter for yet: its local entities, read once to make
// the editors' working copy, and which sibling runs it keeps an order for.
// Type annotations and properties are skipped.

struct UIShell_KdlNode
{
  UIShell_KdlNode *next, *first, *last;
  String8 name;
  // Arguments' text: strings unescaped, numbers and keywords as written.
  String8 args[4];
  U64 arg_count;
};

typedef struct UIShell_KdlReader UIShell_KdlReader;
struct UIShell_KdlReader
{
  Arena *arena;
  String8 text;
  U64 at;
  B32 failed;
};

internal B32
uishell_kdl_starts(UIShell_KdlReader *r, String8 prefix)
{
  return str8_match(str8_prefix(str8_skip(r->text, r->at), prefix.size), prefix, 0);
}

internal B32
uishell_kdl_ident_byte(U8 c)
{
  return c > 0x20 && c != '\\' && c != '/' && c != '(' && c != ')' && c != '{' && c != '}' && c != '<' &&
    c != '>' && c != ';' && c != '[' && c != ']' && c != '=' && c != ',' && c != '"';
}

// Skips spaces and comments; with `lines`, newlines too. A line
// continuation (`\` before a newline) is always skipped.
internal void
uishell_kdl_skip(UIShell_KdlReader *r, B32 lines)
{
  while(r->at < r->text.size && !r->failed)
  {
    U8 c = r->text.str[r->at];
    if(c == ' ' || c == '\t' || (lines && (c == '\n' || c == '\r'))) { r->at++; }
    else if(uishell_kdl_starts(r, str8_lit("\xEF\xBB\xBF"))) { r->at += 3; }
    else if(c == '\\')
    {
      r->at++;
      uishell_kdl_skip(r, 0);
      if(uishell_kdl_starts(r, str8_lit("\r"))) { r->at++; }
      if(uishell_kdl_starts(r, str8_lit("\n"))) { r->at++; }
    }
    else if(uishell_kdl_starts(r, str8_lit("//"))) { while(r->at < r->text.size && r->text.str[r->at] != '\n') { r->at++; } }
    else if(uishell_kdl_starts(r, str8_lit("/*")))
    {
      U64 depth = 0;
      do
      {
        if(uishell_kdl_starts(r, str8_lit("/*"))) { depth++; r->at += 2; }
        else if(uishell_kdl_starts(r, str8_lit("*/"))) { depth--; r->at += 2; }
        else { r->at++; }
      } while(depth && r->at < r->text.size);
      r->failed = depth != 0;
    }
    else { break; }
  }
}

// A string, a raw string, or a bare word (identifier, number or keyword).
internal String8
uishell_kdl_word(UIShell_KdlReader *r)
{
  if(r->at >= r->text.size) { r->failed = 1; return str8_zero(); }
  U8 c = r->text.str[r->at];
  if(c == 'r' && (uishell_kdl_starts(r, str8_lit("r\"")) || uishell_kdl_starts(r, str8_lit("r#"))))
  {
    U64 hashes = 0, at = r->at+1;
    while(at < r->text.size && r->text.str[at] == '#') { hashes++; at++; }
    if(at >= r->text.size || r->text.str[at] != '"') { r->failed = 1; return str8_zero(); }
    U64 start = ++at;
    for(; at < r->text.size; at++)
    {
      if(r->text.str[at] != '"') { continue; }
      U64 h = 0;
      while(h < hashes && at+1+h < r->text.size && r->text.str[at+1+h] == '#') { h++; }
      if(h == hashes) { r->at = at+1+hashes; return str8(r->text.str+start, at-start); }
    }
    r->failed = 1;
    return str8_zero();
  }
  if(c == '"')
  {
    String8List parts = {0};
    U64 run = ++r->at;
    for(; r->at < r->text.size; r->at++)
    {
      U8 b = r->text.str[r->at];
      if(b == '"')
      {
        str8_list_push(r->arena, &parts, str8(r->text.str+run, r->at-run));
        r->at++;
        return str8_list_join(r->arena, &parts, 0);
      }
      if(b != '\\') { continue; }
      str8_list_push(r->arena, &parts, str8(r->text.str+run, r->at-run));
      if(++r->at >= r->text.size) { break; }
      U8 e = r->text.str[r->at];
      char *simple = e == 'n' ? "\n" : e == 'r' ? "\r" : e == 't' ? "\t" : e == '\\' ? "\\" : e == '/' ? "/" :
        e == '"' ? "\"" : e == 'b' ? "\b" : e == 'f' ? "\f" : 0;
      if(simple) { str8_list_push(r->arena, &parts, str8_cstring(simple)); run = r->at+1; continue; }
      U64 close = str8_find_needle(r->text, r->at, str8_lit("}"), 0);
      U64 code = 0;
      if(e != 'u' || !uishell_kdl_starts(r, str8_lit("u{")) || close >= r->text.size ||
         !try_u64_from_str8_c_rules(push_str8f(r->arena, "0x%S", str8_substr(r->text, r1u64(r->at+2, close))), &code) ||
         code > 0x10FFFF)
      { break; }
      U8 *utf8 = push_array(r->arena, U8, 4);
      str8_list_push(r->arena, &parts, str8(utf8, utf8_encode(utf8, (U32)code)));
      r->at = close; run = close+1;
    }
    r->failed = 1;
    return str8_zero();
  }
  U64 start = r->at;
  while(r->at < r->text.size && uishell_kdl_ident_byte(r->text.str[r->at])) { r->at++; }
  if(r->at == start) { r->failed = 1; }
  return str8(r->text.str+start, r->at-start);
}

// Skips a type annotation, "(type)", if there is one.
internal void
uishell_kdl_annotation(UIShell_KdlReader *r)
{
  if(!uishell_kdl_starts(r, str8_lit("("))) { return; }
  U64 close = str8_find_needle(r->text, r->at, str8_lit(")"), 0);
  r->failed |= close >= r->text.size;
  r->at = close+1;
}

// Nodes until `}` (when `nested`) or the end, with their arguments and
// children. A node, argument or block after `/-` is read and left out.
internal UIShell_KdlNode *
uishell_kdl_nodes(UIShell_KdlReader *r, B32 nested)
{
  UIShell_KdlNode *first = 0, *last = 0;
  for(;;)
  {
    uishell_kdl_skip(r, 1);
    while(uishell_kdl_starts(r, str8_lit(";"))) { r->at++; uishell_kdl_skip(r, 1); }
    if(r->failed || r->at >= r->text.size) { r->failed |= nested; break; }
    if(uishell_kdl_starts(r, str8_lit("}"))) { r->failed |= !nested; break; }
    B32 dropped = uishell_kdl_starts(r, str8_lit("/-"));
    if(dropped) { r->at += 2; uishell_kdl_skip(r, 1); }
    UIShell_KdlNode *node = push_array(r->arena, UIShell_KdlNode, 1);
    uishell_kdl_annotation(r);
    node->name = uishell_kdl_word(r);
    for(;;)
    {
      uishell_kdl_skip(r, 0);
      if(r->failed || r->at >= r->text.size) { break; }
      U8 c = r->text.str[r->at];
      if(c == '\n' || c == '\r' || c == ';' || c == '}') { break; }
      B32 slashdash = uishell_kdl_starts(r, str8_lit("/-"));
      if(slashdash) { r->at += 2; uishell_kdl_skip(r, 0); }
      if(uishell_kdl_starts(r, str8_lit("{")))
      {
        r->at++;
        UIShell_KdlNode *children = uishell_kdl_nodes(r, 1);
        if(r->failed) { break; }
        r->at++;
        if(!slashdash)
        {
          node->first = node->last = children;
          while(node->last && node->last->next) { node->last = node->last->next; }
        }
        continue;
      }
      uishell_kdl_annotation(r);
      String8 word = uishell_kdl_word(r);
      // A property: its value is skipped.
      if(!r->failed && uishell_kdl_starts(r, str8_lit("=")))
      {
        r->at++;
        uishell_kdl_annotation(r);
        uishell_kdl_word(r);
        continue;
      }
      if(!r->failed && !slashdash && node->arg_count < ArrayCount(node->args)) { node->args[node->arg_count++] = word; }
    }
    if(r->failed) { break; }
    if(!dropped) { SLLQueuePush(first, last, node); }
  }
  return first;
}

// The nodes of `text`, or 0 when it isn't KDL this reads.
internal UIShell_KdlNode *
uishell_kdl_parse(Arena *arena, String8 text)
{
  UIShell_KdlReader r = {arena, text};
  UIShell_KdlNode *nodes = uishell_kdl_nodes(&r, 0);
  return r.failed ? 0 : nodes;
}

internal UIShell_KdlNode *
uishell_kdl_child(UIShell_KdlNode *node, String8 name)
{
  for(UIShell_KdlNode *c = node ? node->first : 0; c; c = c->next) { if(str8_match(c->name, name, 0)) { return c; } }
  return 0;
}

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
uishell_sidebar_record_export(Arena *arena, Andamento *core, String8 name)
{
  AndamentoBytes bytes = {0};
  if(!core || !andamento_record_export(core, uishell_sidebar_text(name), &bytes, 0)) { return str8_zero(); }
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

// The file a record is stored in: `dashboard.kdl`, `workspace-<id>.kdl`.
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
  return push_str8f(arena, "%S/%S.kdl", dir, file);
}

// The record a file in the directory holds; empty for any other file.
internal String8
uishell_sidebar_record_name(Arena *arena, String8 file)
{
  String8 workspace = str8_lit("workspace-"), kdl = str8_lit(".kdl");
  if(!str8_match(str8_postfix(file, kdl.size), kdl, 0)) { return str8_zero(); }
  String8 stem = str8_chop(file, kdl.size);
  if(str8_match(stem, str8_lit("dashboard"), 0)) { return push_str8_copy(arena, stem); }
  if(str8_match(str8_prefix(stem, workspace.size), workspace, 0) && stem.size > workspace.size)
  { return push_str8f(arena, "workspace/%S", str8_skip(stem, workspace.size)); }
  return str8_zero();
}

// The window's records directory, giving the window an id for it when it
// has none; empty without a user file.
internal String8
uishell_sidebar_records_dir(Arena *arena, CFG_Node *window)
{
  if(!rd_state->user_path.size || window == &cfg_nil_node) { return str8_zero(); }
  CFG_Node *id = cfg_node_child_from_string(window, str8_lit("andamento_records"));
  if(id->first->string.size == 0)
  {
    Temp scratch = scratch_begin(&arena, 1);
    id = cfg_node_child_from_string_or_alloc(rd_state->cfg, window, str8_lit("andamento_records"));
    cfg_node_new_replace(rd_state->cfg, id, uishell_string_from_workspace_id(scratch.arena, uishell_workspace_id_make()));
    scratch_end(scratch);
  }
  return push_str8f(arena, "%S.andamento/%S", rd_state->user_path, id->first->string);
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
// a second after a change was first seen, or with `flush`, now. Reading a
// generation encodes its record, so they are read only when one may have
// moved: after a new snapshot, when the names changed, or while due.
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
    make_directory(str8_chop_last_slash(state->records_dir));
    make_directory(state->records_dir);
    index = 0;
    for(String8Node *n = names.first; n; n = n->next, index++)
    {
      UIShell_SidebarRecord *record = uishell_sidebar_record_written(state, n->string);
      if(record && record->generation == generations[index]) { continue; }
      String8 text = uishell_sidebar_record_export(scratch.arena, state->core, n->string);
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

//- Andamento's dashboard record, read

// The dashboard record's nodes, read again when its generation moves.
internal UIShell_KdlNode *
uishell_sidebar_dashboard(UIShell_SidebarState *state)
{
  if(!state->core) { return 0; }
  U64 generation = uishell_sidebar_record_generation(state->core, str8_lit("dashboard"));
  if(generation && generation == state->dashboard_generation) { return state->dashboard; }
  if(!state->dashboard_arena) { state->dashboard_arena = arena_alloc(); }
  arena_clear(state->dashboard_arena);
  String8 text = uishell_sidebar_record_export(state->dashboard_arena, state->core, str8_lit("dashboard"));
  UIShell_KdlNode *envelope = uishell_kdl_parse(state->dashboard_arena, text);
  state->dashboard = envelope ? envelope->first : 0;
  state->dashboard_generation = generation;
  return state->dashboard;
}

// The entity an `order` node's run is under (the last of its parent key),
// or an empty one for a run directly in a section.
internal AndamentoEntity
uishell_sidebar_order_parent(UIShell_KdlNode *order)
{
  UIShell_KdlNode *parent = uishell_kdl_child(order, str8_lit("parent"));
  UIShell_KdlNode *at = parent ? parent->last : 0;
  if(!at || at->arg_count < 3) { return (AndamentoEntity){0}; }
  return (AndamentoEntity){uishell_sidebar_text(at->args[1]), uishell_sidebar_text(at->args[2])};
}

// A row in a saved order's run: one of the entities it names, under the
// entity it is under. ANDAMENTO_NONE when none is shown.
internal U64
uishell_sidebar_order_row(UIShell_SidebarState *state, UIShell_KdlNode *order)
{
  AndamentoEntity parent = uishell_sidebar_order_parent(order);
  U64 count = state->snapshot ? andamento_snapshot_node_count(state->snapshot) : 0;
  for(U64 i = 0; i < count; i++)
  {
    AndamentoNode node = {0}, up = {0};
    uishell_sidebar_snapshot_node(state->snapshot, i, &node);
    if(node.is_section || node.parent == ANDAMENTO_NONE) { continue; }
    uishell_sidebar_snapshot_node(state->snapshot, node.parent, &up);
    if(!str8_match(uishell_sidebar_string(up.entity_kind), uishell_sidebar_string(parent.kind), 0) ||
       !str8_match(uishell_sidebar_string(up.entity_id), uishell_sidebar_string(parent.id), 0)) { continue; }
    for(UIShell_KdlNode *e = order->first; e; e = e->next)
    {
      if(str8_match(e->name, str8_lit("entity"), 0) && e->arg_count >= 2 &&
         str8_match(e->args[0], uishell_sidebar_string(node.entity_kind), 0) &&
         str8_match(e->args[1], uishell_sidebar_string(node.entity_id), 0)) { return i; }
    }
  }
  return ANDAMENTO_NONE;
}

// Whether Andamento keeps an order for the sibling run keyed `loop`, which
// offers Reset order. Loop keys are opaque, so a saved order is matched to
// the run by the entity it is under and the rows it names.
internal B32
uishell_sidebar_order_saved(UIShell_SidebarState *state, String8 loop)
{
  if(!loop.size || !state->snapshot) { return 0; }
  for(UIShell_KdlNode *order = uishell_sidebar_dashboard(state); order; order = order->next)
  {
    if(!str8_match(order->name, str8_lit("order"), 0)) { continue; }
    U64 row = uishell_sidebar_order_row(state, order);
    if(row != ANDAMENTO_NONE && str8_match(uishell_sidebar_loop_key(state->snapshot, row), loop, 0)) { return 1; }
  }
  return 0;
}

//- Loading

internal String8
uishell_sidebar_local_fact(UIShell_KdlNode *local, String8 key)
{
  for(UIShell_KdlNode *f = local->first; f; f = f->next)
  { if(str8_match(f->name, str8_lit("fact"), 0) && f->arg_count >= 2 && str8_match(f->args[0], key, 0)) { return f->args[1]; } }
  return str8_zero();
}

// An entity-reference fact's first entity.
internal AndamentoEntity
uishell_sidebar_local_fact_entity(UIShell_KdlNode *local, String8 key)
{
  for(UIShell_KdlNode *f = local->first; f; f = f->next)
  {
    if(!str8_match(f->name, str8_lit("fact"), 0) || f->arg_count < 1 || !str8_match(f->args[0], key, 0)) { continue; }
    UIShell_KdlNode *e = uishell_kdl_child(f, str8_lit("entity"));
    if(e && e->arg_count >= 2) { return (AndamentoEntity){uishell_sidebar_text(e->args[0]), uishell_sidebar_text(e->args[1])}; }
  }
  return (AndamentoEntity){0};
}

internal U64
uishell_sidebar_local_position(UIShell_KdlNode *local)
{
  U64 position = max_U64;
  try_u64_from_str8_c_rules(uishell_sidebar_local_fact(local, str8_lit(".position")), &position);
  return position;
}

// Adds to `parent`, in position order, a `name` node for each local entity
// of `kind` whose `link` fact names `owner` (each one, without a link), as
// uishell_sidebar_local_entities publishes them.
internal void
uishell_sidebar_local_seed(UIShell_KdlNode *nodes, CFG_Node *parent, String8 name, String8 kind, String8 link, String8 owner)
{
  Temp scratch = scratch_begin(0, 0);
  U64 count = 0, found = 0;
  for(UIShell_KdlNode *n = nodes; n; n = n->next) { count++; }
  UIShell_KdlNode **matches = push_array(scratch.arena, UIShell_KdlNode *, count);
  for(UIShell_KdlNode *n = nodes; n; n = n->next)
  {
    if(!str8_match(n->name, str8_lit("local"), 0) || n->arg_count < 2 || !str8_match(n->args[0], kind, 0)) { continue; }
    if(link.size && !str8_match(uishell_sidebar_string(uishell_sidebar_local_fact_entity(n, link).id), owner, 0)) { continue; }
    // Insertion keeps them in position order, ties in record order.
    U64 at = found++;
    for(; at > 0 && uishell_sidebar_local_position(matches[at-1]) > uishell_sidebar_local_position(n); at--) { matches[at] = matches[at-1]; }
    matches[at] = n;
  }
  for(U64 i = 0; i < found; i++)
  {
    UIShell_KdlNode *local = matches[i];
    CFG_Node *node = cfg_node_new(rd_state->cfg, parent, name);
    String8 id = local->args[1];
    if(str8_match(kind, str8_lit(".ref"), 0))
    {
      AndamentoEntity target = uishell_sidebar_local_fact_entity(local, str8_lit(".target"));
      uishell_sidebar_local_set_field(node, str8_lit("ghost"), id);
      uishell_sidebar_local_set_field(node, str8_lit("kind"), uishell_sidebar_string(target.kind));
      uishell_sidebar_local_set_field(node, str8_lit("entity"), uishell_sidebar_string(target.id));
      String8 fields[] = {str8_lit("label"), str8_lit("source")}, facts[] = {str8_lit(".label"), str8_lit(".source")};
      for(U64 f = 0; f < ArrayCount(fields); f++)
      {
        String8 value = uishell_sidebar_local_fact(local, facts[f]);
        if(value.size) { uishell_sidebar_local_set_field(node, fields[f], value); }
      }
      if(str8_match(uishell_sidebar_local_fact(local, str8_lit(".compact")), str8_lit("true"), 0))
      { cfg_node_new(rd_state->cfg, node, str8_lit("compact")); }
      continue;
    }
    uishell_sidebar_local_set_field(node, str8_lit("id"), id);
    String8 label = uishell_sidebar_local_fact(local, str8_lit("display.label"));
    if(str8_match(kind, str8_lit(".group"), 0))
    {
      uishell_sidebar_local_set_field(node, str8_lit("label"), label);
      if(str8_match(uishell_sidebar_local_fact(local, str8_lit(".default")), str8_lit("true"), 0))
      { cfg_node_new(rd_state->cfg, node, str8_lit("default")); }
      uishell_sidebar_local_seed(nodes, node, str8_lit("card"), str8_lit(".ref"), str8_lit(".group"), id);
      continue;
    }
    uishell_sidebar_local_seed(nodes, node, str8_lit("group"), str8_lit(".group"), str8_lit(".section"), id);
    // A section is published under its title, a name of its own only when
    // that differs from its groups' names.
    if(label.size && !str8_match(label, uishell_sidebar_local_title(scratch.arena, node), 0))
    { uishell_sidebar_local_set_field(node, str8_lit("label"), label); }
  }
  scratch_end(scratch);
}

// Makes the window's working copy of its local sections, groups and pins
// from the dashboard record Andamento imported.
internal void
uishell_sidebar_local_load(UIShell_SidebarState *state, CFG_Node *window)
{
  cfg_node_release(rd_state->cfg, uishell_sidebar_local_tree(window));
  UIShell_KdlNode *dashboard = uishell_sidebar_dashboard(state);
  B32 any = 0;
  for(UIShell_KdlNode *n = dashboard; n && !any; n = n->next) { any = str8_match(n->name, str8_lit("local"), 0); }
  if(any)
  {
    uishell_sidebar_local_seed(dashboard, uishell_sidebar_local_tree_alloc(window), str8_lit("section"), str8_lit(".section"),
                               str8_zero(), str8_zero());
  }
  // Andamento holds what was last published, so publishing removes any
  // entity the copy left out (one whose section or group had gone).
  uishell_sidebar_local_published_clear(state);
  for(UIShell_KdlNode *n = dashboard; n; n = n->next)
  {
    if(str8_match(n->name, str8_lit("local"), 0) && n->arg_count >= 2)
    {
      Temp scratch = scratch_begin(0, 0);
      uishell_sidebar_local_published_add(state, push_str8f(scratch.arena, "%S/%S", n->args[0], n->args[1]), 0);
      scratch_end(scratch);
    }
  }
}

// Before records, a window saved its sidebar under these keys of the user
// file, and each new core had them replayed into it. A window that has them
// and no records has them imported once; then they go.
read_only global String8 uishell_sidebar_legacy_keys[] =
{
  str8_lit_comp("sidebar_display"), str8_lit_comp("sidebar_order"), str8_lit_comp("sidebar_local"),
};

internal void
uishell_sidebar_records_migrate(UIShell_SidebarState *state, CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *display = cfg_node_child_from_string(window, str8_lit("sidebar_display"));
  for(CFG_Node *value = display->first; value != &cfg_nil_node; value = value->next)
  {
    // A value for a variable this configuration doesn't declare is dropped.
    char *error = 0;
    andamento_set_display_variable(state->core, uishell_sidebar_text(value->string), uishell_sidebar_text(value->first->string), &error);
    andamento_string_free(error);
  }
  CFG_Node *orders = cfg_node_child_from_string(window, str8_lit("sidebar_order"));
  for(CFG_Node *run = orders->first; run != &cfg_nil_node; run = run->next)
  {
    U64 count = 0;
    for(CFG_Node *n = run->first; n != &cfg_nil_node && n->next != &cfg_nil_node; n = n->next->next) { count++; }
    AndamentoEntity *entities = push_array(scratch.arena, AndamentoEntity, count);
    CFG_Node *n = run->first;
    for(U64 i = 0; i < count; i++, n = n->next->next)
    { entities[i] = (AndamentoEntity){uishell_sidebar_text(n->string), uishell_sidebar_text(n->next->string)}; }
    char *error = 0;
    if(count) { andamento_set_sibling_order(state->core, uishell_sidebar_text(run->string), entities, count, &error); }
    andamento_string_free(error);
  }
  // Local sections become the working copy, which publishing sends.
  CFG_Node *local = cfg_node_child_from_string(window, str8_lit("sidebar_local"));
  if(local->first != &cfg_nil_node)
  {
    CFG_Node *tree = uishell_sidebar_local_tree_alloc(window);
    for(CFG_Node *c = local->first, *next; c != &cfg_nil_node; c = next)
    {
      next = c->next;
      cfg_node_unhook(rd_state->cfg, local, c);
      cfg_node_insert_child(rd_state->cfg, tree, tree->last, c);
    }
  }
  // An open subject workspace was bound to its subject by activating its row
  // again on every start; its record binds it now. Until it has one, it is
  // registered with its subject published on it, which its record keeps.
  for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next)
  {
    if(!str8_match(c->string, str8_lit("workspace"), 0) || !uishell_workspace_cfg_has_subject(c)) { continue; }
    AndamentoFact facts[2] = {0};
    facts[0].key = uishell_sidebar_text(str8_lit("entity.kind"));
    facts[0].kind = ANDAMENTO_FACT_TEXT;
    facts[0].text = uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("sidebar_entity_kind"))->first->string);
    facts[1].key = uishell_sidebar_text(str8_lit("entity.id"));
    facts[1].kind = ANDAMENTO_FACT_TEXT;
    facts[1].text = uishell_sidebar_text(cfg_node_child_from_string(c, str8_lit("sidebar_entity_id"))->first->string);
    AndamentoWorkspaceId id = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(c));
    char *error = 0;
    if(andamento_workspace_register(state->core, id, &error))
    { andamento_apply_workspace(state->core, 0, id, uishell_sidebar_text(str8_lit("wheelhouse.local")), facts, ArrayCount(facts), &error); }
    andamento_string_free(error);
  }
  for(U64 i = 0; i < ArrayCount(uishell_sidebar_legacy_keys); i++)
  { cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, uishell_sidebar_legacy_keys[i])); }
  scratch_end(scratch);
}

// Imports the window's records into its new core, before its first observe:
// the dashboard first, then each workspace's (which registers it and binds
// it to its subject). Without records, a window saved before them has its
// old keys imported instead. A record that doesn't import (a newer
// version's, say) is reported, and nothing is saved over it this session.
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
    if(andamento_record_import(state->core, uishell_sidebar_text(name->string), uishell_sidebar_text(text), &error))
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
  B32 legacy = 0;
  for(U64 i = 0; i < ArrayCount(uishell_sidebar_legacy_keys); i++)
  { legacy |= cfg_node_child_from_string(window, uishell_sidebar_legacy_keys[i]) != &cfg_nil_node; }
  if(names.node_count == 0 && legacy) { uishell_sidebar_records_migrate(state, window); }
  else
  {
    if(!failed)
    {
      for(U64 i = 0; i < ArrayCount(uishell_sidebar_legacy_keys); i++)
      { cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, uishell_sidebar_legacy_keys[i])); }
    }
    uishell_sidebar_local_load(state, window);
  }
  scratch_end(scratch);
}
