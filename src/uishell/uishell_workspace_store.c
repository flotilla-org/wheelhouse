//- Workspace arrangements and Slots in Andamento (ADR 0012; andamento.h,
// "Slots and arrangement documents"). Andamento keeps each workspace's Slots,
// with their View Specs, and its arrangement document; this device keeps the
// rest. Wheelhouse's live copy is the workspace's panel tree in config
// (shell_arrangement.h), whose View nodes runtime state (live terminals,
// Jackstay sessions) is keyed by. Each View in it is a Slot, named by its
// `slot` setting; panels keep their IDs.
//
//   load    A workspace this step saved has no panel tree in the presentation
//           file, only `arrangement_presentation`. Once the window's core has
//           imported its records, the tree is built from Andamento's
//           arrangement and slots (the getters), with this device's settings
//           merged back in (uishell_workspace_store_load).
//   import  A workspace saved before it (state model step 4) still has its
//           panel tree: the first sync commits it whole, slots and
//           arrangement, and the next save writes it without one.
//   commit  Once config has changed and no gesture is in flight, each
//           workspace whose document or View Specs changed is committed: new
//           and changed slots (andamento_slot_set), then the document at the
//           generation last seen (andamento_set_arrangement), then the user's
//           slots it no longer tabs are removed. A gesture edits a copy (a
//           boundary drag's rd_boundary_resize, a tab drag's drop) and makes
//           no call until it ends, so a drag commits once, at its end.
//   pull    When Andamento changed the document itself (it placed a slot the
//           document had no tab for, or the provider's baseline changed), the
//           panel tree follows it.
//
// A stale commit (the generation moved since it was read) is committed again
// at the new generation. Only Andamento's own reconciliation moves it, when
// the slot set changed from the provider's side, and recommitting runs that
// reconciliation again: new slots are placed and gone ones reported, so
// neither the user's edit nor the provider's change is lost. Dropping the
// edit instead would undo a gesture the user saw for a change they didn't.
//
// View kinds and their View Specs (CONTEXT.md, "View Spec"):
//
//   terminal   with a command       local recipe: the command (a shell line) and cwd
//              without one          wheelhouse:terminal[?cwd=..]
//              the `primary` slot of a subject publishing workspace.primary.*:
//                                   the provider's facet; Wheelhouse never sets
//                                   a slot that follows its provider
//   jackstay   with an endpoint     local recipe: Jackstay; the launcher is the
//                                   setting naming the endpoint (source_endpoint, ...)
//              without one          wheelhouse:jackstay
//   text       of a file            local recipe: the file
//   any other                       wheelhouse:<kind>[?expression=..]
//
// A `wheelhouse:` URL is this frontend's own page: another frontend shows a
// placeholder for it. Content Wheelhouse can't show (another facet, a web
// page, another frontend's launcher, or a slot that has gone) gets a
// placeholder View naming it, and its spec is never rewritten from it.
//
// Device-local (the presentation file; uishell_workspace_store_window_text):
//
//   workspace: { ... arrangement_generation: 7
//     arrangement_presentation: {
//       panel: {id: 2 selected tabs_on_bottom}   Presentation State, by panel ID
//       view: {slot: "u:3" label: "Shell"}       a View's settings its spec doesn't hold
//       resolution: {slot: "u:3" session: ..}    machine-local Target Resolutions
//   } }
//
// A View whose slot follows its provider (`follows_provider`) keeps all of
// its settings here: what it runs is the provider's content as this device
// last resolved it. Andamento has nowhere yet for portable Target
// Resolutions (andamento#155, "Limits"), so every resolution stays on this
// device. Floating Panels are Presentation State, and their Views are not
// Slots.

typedef struct UIShell_ViewSpec UIShell_ViewSpec;
struct UIShell_ViewSpec
{
  U32 content;
  String8 provider, kind, id, facet;
  // A shell line; argv from another host is joined with spaces.
  String8 command;
  B32 has_cwd;
  String8 cwd, path, url, launcher, endpoint, presentation;
};

typedef struct UIShell_StoreEntry UIShell_StoreEntry;
struct UIShell_StoreEntry
{
  UIShell_WorkspaceId id;
  // The document and specs last committed or built, and its generation.
  U64 hash;
  U64 generation;
};

// Jackstay View settings that name the endpoint it connects to, in the order
// its View reads them.
read_only global String8 uishell_store_jackstay_keys[] =
{
  str8_lit_comp("source_endpoint"), str8_lit_comp("source_socket"),
  str8_lit_comp("media_endpoint"), str8_lit_comp("media_socket"),
  str8_lit_comp("d3d11_endpoint"), str8_lit_comp("porthole_endpoint"),
};

// View settings that are Target Resolutions: how this machine reached the
// content last, disposable and never part of a View Spec.
read_only global String8 uishell_store_resolution_keys[] =
{
  str8_lit_comp("session"), str8_lit_comp("daemon_name"), str8_lit_comp("attach_token"),
  str8_lit_comp("managed_target"), str8_lit_comp("porthole_session"),
};

#define UIShell_StoreCall(state, call) ((state)->store_calls += 1, (call))

//- Settings

internal String8
uishell_store_setting(CFG_Node *node, String8 key)
{
  return cfg_node_child_from_string(node, key)->first->string;
}

// Sets `node`'s `key` to `value` unless it is already.
internal void
uishell_store_set_setting(CFG_Node *node, String8 key, String8 value)
{
  CFG_Node *setting = cfg_node_child_from_string(node, key);
  if(setting != &cfg_nil_node && setting->first != &cfg_nil_node && setting->first == setting->last &&
     setting->first->first == &cfg_nil_node && str8_match(setting->first->string, value, 0)) { return; }
  cfg_node_new_replace(rd_state->cfg, cfg_node_child_from_string_or_alloc(rd_state->cfg, node, key), value);
}

// Marks a View whose slot follows its provider (`follows_provider`): what it
// runs is the provider's content as this device last resolved it, so all
// of it is device-local, and Andamento's spec for it is the provider's facet.
internal void
uishell_store_mark_follows(CFG_Node *view, B32 follows)
{
  CFG_Node *mark = cfg_node_child_from_string(view, str8_lit("follows_provider"));
  if(follows && mark == &cfg_nil_node) { cfg_node_new(rd_state->cfg, view, str8_lit("follows_provider")); }
  if(!follows && mark != &cfg_nil_node) { cfg_node_release(rd_state->cfg, mark); }
}

internal B32
uishell_store_key_in(String8 key, String8 *keys, U64 count)
{
  for(U64 i = 0; i < count; i++) { if(str8_match(key, keys[i], 0)) { return 1; } }
  return 0;
}

//- wheelhouse: URLs

internal String8
uishell_store_url_encode(Arena *arena, String8 s)
{
  String8List parts = {0};
  for(U64 i = 0; i < s.size; i++)
  {
    U8 c = s.str[i];
    if(char_is_alpha(c) || char_is_digit(c, 10) || c == '-' || c == '.' || c == '_' || c == '~') { str8_list_pushf(arena, &parts, "%c", c); }
    else { str8_list_pushf(arena, &parts, "%%%02X", (U32)c); }
  }
  return str8_list_join(arena, &parts, 0);
}

internal String8
uishell_store_url_decode(Arena *arena, String8 s)
{
  U8 *out = push_array(arena, U8, s.size+1);
  U64 size = 0;
  for(U64 i = 0; i < s.size; i++)
  {
    U8 c = s.str[i];
    if(c == '%' && i+2 < s.size && char_is_digit(s.str[i+1], 16) && char_is_digit(s.str[i+2], 16))
    {
      out[size++] = (U8)u64_from_str8(str8(s.str+i+1, 2), 16);
      i += 2;
    }
    else { out[size++] = c; }
  }
  return str8(out, size);
}

// "wheelhouse:<kind>" with a query of `count` name/value pairs; an empty
// value is left out.
internal String8
uishell_store_page_url(Arena *arena, String8 kind, String8 *names, String8 *values, U64 count)
{
  String8List parts = {0};
  str8_list_pushf(arena, &parts, "wheelhouse:%S", kind);
  B32 first = 1;
  for(U64 i = 0; i < count; i++)
  {
    if(!values[i].size) { continue; }
    str8_list_pushf(arena, &parts, "%s%S=%S", first ? "?" : "&", names[i], uishell_store_url_encode(arena, values[i]));
    first = 0;
  }
  return str8_list_join(arena, &parts, 0);
}

// A wheelhouse: URL's kind, or empty for any other URL.
internal String8
uishell_store_page_kind(String8 url)
{
  String8 scheme = str8_lit("wheelhouse:");
  if(!str8_match(str8_prefix(url, scheme.size), scheme, 0)) { return str8_zero(); }
  String8 rest = str8_skip(url, scheme.size);
  return str8_prefix(rest, str8_find_needle(rest, 0, str8_lit("?"), 0));
}

internal String8
uishell_store_page_param(Arena *arena, String8 url, String8 name)
{
  U64 q = str8_find_needle(url, 0, str8_lit("?"), 0);
  String8 query = q < url.size ? str8_skip(url, q+1) : str8_zero();
  for(U64 start = 0; start < query.size;)
  {
    U64 end = str8_find_needle(query, start, str8_lit("&"), 0);
    String8 part = str8_substr(query, r1u64(start, end));
    U64 eq = str8_find_needle(part, 0, str8_lit("="), 0);
    if(eq < part.size && str8_match(str8_prefix(part, eq), name, 0)) { return uishell_store_url_decode(arena, str8_skip(part, eq+1)); }
    start = end+1;
  }
  return str8_zero();
}

//- View Specs

// The file a text View shows, from its `file:"<path>".data` expression.
internal String8
uishell_store_file_from_expr(Arena *arena, String8 expr)
{
  String8 prefix = str8_lit("file:\""), suffix = str8_lit("\".data");
  if(expr.size <= prefix.size+suffix.size || !str8_match(str8_prefix(expr, prefix.size), prefix, 0) ||
     !str8_match(str8_postfix(expr, suffix.size), suffix, 0)) { return str8_zero(); }
  return raw_from_escaped_str8(arena, str8_substr(expr, r1u64(prefix.size, expr.size-suffix.size)));
}

// What defines `view`'s content, and which of its settings that took (at
// most four). Returns 0 for a placeholder, whose slot keeps the spec it was
// made from.
internal B32
uishell_store_spec_from_view(Arena *arena, CFG_Node *view, UIShell_ViewSpec *spec, String8 *taken, U64 *taken_count)
{
  MemoryZeroStruct(spec);
  *taken_count = 0;
  String8 kind = view->string;
  if(str8_match(kind, str8_lit("placeholder"), 0)) { return 0; }
  String8 expr = rd_expr_from_cfg(view);
  CFG_Node *cwd = cfg_node_child_from_string(view, str8_lit("cwd"));
  spec->presentation = kind;
  spec->content = ANDAMENTO_SLOT_URL;
  taken[(*taken_count)++] = str8_lit("expression");
  if(str8_match(kind, str8_lit("terminal"), 0))
  {
    taken[(*taken_count)++] = str8_lit("cwd");
    if(expr.size)
    {
      spec->content = ANDAMENTO_SLOT_COMMAND;
      spec->command = expr;
      spec->has_cwd = cwd != &cfg_nil_node;
      spec->cwd = cwd->first->string;
    }
    else
    {
      String8 name = str8_lit("cwd"), value = cwd->first->string;
      spec->url = uishell_store_page_url(arena, kind, &name, &value, 1);
    }
    return 1;
  }
  if(str8_match(kind, str8_lit("jackstay"), 0))
  {
    for(U64 i = 0; i < ArrayCount(uishell_store_jackstay_keys); i++)
    {
      String8 endpoint = uishell_store_setting(view, uishell_store_jackstay_keys[i]);
      if(!endpoint.size) { continue; }
      spec->content = ANDAMENTO_SLOT_JACKSTAY;
      spec->launcher = uishell_store_jackstay_keys[i];
      spec->endpoint = endpoint;
      taken[(*taken_count)++] = uishell_store_jackstay_keys[i];
      return 1;
    }
    spec->url = uishell_store_page_url(arena, kind, 0, 0, 0);
    return 1;
  }
  String8 path = str8_match(kind, str8_lit("text"), 0) ? uishell_store_file_from_expr(arena, expr) : str8_zero();
  if(path.size)
  {
    spec->content = ANDAMENTO_SLOT_FILE;
    spec->path = path;
    return 1;
  }
  String8 name = str8_lit("expression");
  spec->url = uishell_store_page_url(arena, kind, &name, &expr, 1);
  return 1;
}

internal UIShell_ViewSpec
uishell_store_spec_from_andamento(Arena *arena, AndamentoViewSpec *s)
{
  UIShell_ViewSpec spec = {0};
  spec.content = s->content;
  spec.provider = uishell_sidebar_string(s->entity.provider);
  spec.kind = uishell_sidebar_string(s->entity.kind);
  spec.id = uishell_sidebar_string(s->entity.id);
  spec.facet = uishell_sidebar_string(s->facet);
  spec.command = uishell_sidebar_string(s->command);
  if(s->argc)
  {
    String8List argv = {0};
    for(U64 i = 0; i < s->argc; i++) { str8_list_push(arena, &argv, uishell_sidebar_string(s->argv[i])); }
    StringJoin join = {.sep = str8_lit(" ")};
    spec.command = str8_list_join(arena, &argv, &join);
  }
  spec.has_cwd = s->has_cwd != 0;
  spec.cwd = uishell_sidebar_string(s->cwd);
  spec.path = uishell_sidebar_string(s->path);
  spec.url = uishell_sidebar_string(s->url);
  spec.launcher = uishell_sidebar_string(s->launcher);
  spec.endpoint = uishell_sidebar_string(s->endpoint);
  spec.presentation = s->has_presentation ? uishell_sidebar_string(s->presentation) : str8_zero();
  return spec;
}

internal AndamentoViewSpec
uishell_store_andamento_spec(UIShell_ViewSpec *spec)
{
  AndamentoViewSpec s = {0};
  s.content = spec->content;
  s.entity = (AndamentoEntity3){uishell_sidebar_text(spec->provider), uishell_sidebar_text(spec->kind), uishell_sidebar_text(spec->id)};
  s.facet = uishell_sidebar_text(spec->facet);
  s.command = uishell_sidebar_text(spec->command);
  s.has_cwd = spec->has_cwd;
  s.cwd = uishell_sidebar_text(spec->cwd);
  s.path = uishell_sidebar_text(spec->path);
  s.url = uishell_sidebar_text(spec->url);
  s.launcher = uishell_sidebar_text(spec->launcher);
  s.endpoint = uishell_sidebar_text(spec->endpoint);
  s.has_presentation = spec->presentation.size != 0;
  s.presentation = uishell_sidebar_text(spec->presentation);
  return s;
}

internal B32
uishell_store_spec_match(UIShell_ViewSpec *a, UIShell_ViewSpec *b)
{
  return (a->content == b->content && str8_match(a->provider, b->provider, 0) && str8_match(a->kind, b->kind, 0) &&
          str8_match(a->id, b->id, 0) && str8_match(a->facet, b->facet, 0) && str8_match(a->command, b->command, 0) &&
          a->has_cwd == b->has_cwd && str8_match(a->cwd, b->cwd, 0) && str8_match(a->path, b->path, 0) &&
          str8_match(a->url, b->url, 0) && str8_match(a->launcher, b->launcher, 0) &&
          str8_match(a->endpoint, b->endpoint, 0) && str8_match(a->presentation, b->presentation, 0));
}

internal U64
uishell_store_hash_text(U64 hash, String8 s)
{
  hash = hash*33 + s.size;
  for(U64 i = 0; i < s.size; i++) { hash = hash*33 + s.str[i]; }
  return hash;
}

internal U64
uishell_store_spec_hash(U64 hash, UIShell_ViewSpec *spec)
{
  hash = hash*33 + spec->content;
  String8 fields[] = {spec->provider, spec->kind, spec->id, spec->facet, spec->command, spec->cwd, spec->path,
                      spec->url, spec->launcher, spec->endpoint, spec->presentation};
  for(U64 i = 0; i < ArrayCount(fields); i++) { hash = uishell_store_hash_text(hash, fields[i]); }
  return hash*33 + spec->has_cwd;
}

// What Wheelhouse can't show of a spec, for its placeholder; empty when it
// can show it.
internal String8
uishell_store_unshown(Arena *arena, String8 key, UIShell_ViewSpec *spec)
{
  switch(spec->content)
  {
    case ANDAMENTO_SLOT_COMMAND: case ANDAMENTO_SLOT_FILE: return str8_zero();
    case ANDAMENTO_SLOT_JACKSTAY:
    {
      if(uishell_store_key_in(spec->launcher, uishell_store_jackstay_keys, ArrayCount(uishell_store_jackstay_keys))) { return str8_zero(); }
      return push_str8f(arena, "Jackstay launcher \"%S\" for %S", spec->launcher, spec->endpoint);
    }
    case ANDAMENTO_SLOT_URL:
    {
      String8 kind = uishell_store_page_kind(spec->url);
      RD_ViewRegistration *registration = kind.size ? rd_dock_view_from_name(kind) : 0;
      if(registration && registration->default_host == RD_DockHostKind_WorkspaceRegion) { return str8_zero(); }
      return push_str8_copy(arena, spec->url);
    }
    case ANDAMENTO_SLOT_FACET:
    {
      if(str8_match(spec->facet, str8_lit("primary"), 0) && str8_match(key, str8_lit("primary"), 0)) { return str8_zero(); }
      return push_str8f(arena, "the %S of %S/%S", spec->facet, spec->kind, spec->id);
    }
  }
  return str8_lit("content of a kind this Wheelhouse doesn't know");
}

// A new View node, in no panel, showing slot `key`'s content, or a
// placeholder naming what it can't show (`spec` 0: the slot has gone).
internal CFG_Node *
uishell_store_view_from_spec(String8 key, UIShell_ViewSpec *spec)
{
  Temp scratch = scratch_begin(0, 0);
  String8 unshown = spec ? uishell_store_unshown(scratch.arena, key, spec) : str8_lit("a slot that has gone");
  String8 kind = str8_lit("placeholder"), expr = str8_zero(), cwd = str8_zero();
  B32 has_cwd = 0;
  if(!unshown.size)
  {
    switch(spec->content)
    {
      case ANDAMENTO_SLOT_COMMAND: { kind = str8_lit("terminal"); expr = spec->command; has_cwd = spec->has_cwd; cwd = spec->cwd; } break;
      case ANDAMENTO_SLOT_FILE: { kind = str8_lit("text"); expr = rd_eval_string_from_file_path(scratch.arena, spec->path); } break;
      case ANDAMENTO_SLOT_JACKSTAY: { kind = str8_lit("jackstay"); } break;
      case ANDAMENTO_SLOT_FACET: { kind = str8_lit("terminal"); } break;
      case ANDAMENTO_SLOT_URL:
      {
        kind = uishell_store_page_kind(spec->url);
        expr = uishell_store_page_param(scratch.arena, spec->url, str8_lit("expression"));
        cwd = uishell_store_page_param(scratch.arena, spec->url, str8_lit("cwd"));
        has_cwd = cwd.size != 0;
      } break;
    }
  }
  CFG_Node *view = rd_cfg_new_view_tab(&cfg_nil_node, kind, expr, 0);
  uishell_store_set_setting(view, str8_lit("slot"), key);
  if(has_cwd) { uishell_store_set_setting(view, str8_lit("cwd"), cwd); }
  if(!unshown.size && spec->content == ANDAMENTO_SLOT_JACKSTAY) { uishell_store_set_setting(view, spec->launcher, spec->endpoint); }
  if(!unshown.size && spec->content == ANDAMENTO_SLOT_FACET)
  {
    uishell_store_set_setting(view, str8_lit("resource_id"), key);
    uishell_store_mark_follows(view, 1);
  }
  if(unshown.size)
  {
    uishell_store_set_setting(view, str8_lit("content"), unshown);
    if(spec && spec->content == ANDAMENTO_SLOT_URL) { uishell_store_set_setting(view, str8_lit("url"), spec->url); }
  }
  scratch_end(scratch);
  return view;
}

//- Slots

internal B32
uishell_store_user_key_valid(String8 key)
{
  String8 prefix = str8_lit("u:");
  if(key.size <= prefix.size || key.size > prefix.size+64 || !str8_match(str8_prefix(key, prefix.size), prefix, 0)) { return 0; }
  for(U64 i = prefix.size; i < key.size; i++)
  {
    U8 c = key.str[i];
    B32 ok = (c >= 'a' && c <= 'z') || char_is_digit(c, 10) || (i > prefix.size && (c == '-' || c == '_'));
    if(!ok) { return 0; }
  }
  return 1;
}

// A provider's key (no ':'), or one of the user's.
internal B32
uishell_store_key_valid(String8 key)
{
  if(uishell_store_user_key_valid(key)) { return 1; }
  if(!key.size || key.size > 64) { return 0; }
  for(U64 i = 0; i < key.size; i++)
  {
    U8 c = key.str[i];
    B32 ok = (c >= 'a' && c <= 'z') || char_is_digit(c, 10) || (i > 0 && (c == '-' || c == '_'));
    if(!ok) { return 0; }
  }
  return 1;
}

internal B32
uishell_store_slot_find(AndamentoSlots *slots, String8 key, AndamentoSlot *out)
{
  for(U64 i = 0; slots && i < andamento_slots_count(slots); i++)
  {
    AndamentoSlot slot = {0};
    if(andamento_slots_get(slots, i, &slot) && str8_match(uishell_sidebar_string(slot.key), key, 0))
    { if(out) { *out = slot; } return 1; }
  }
  return 0;
}

// The n of a "u:<n>" key, else 0.
internal U64
uishell_store_user_number(String8 key)
{
  String8 digits = str8_skip(key, 2);
  return uishell_store_user_key_valid(key) && str8_is_integer(digits, 10) && digits.size < 18 ? u64_from_str8(digits, 10) : 0;
}

//- Documents

typedef struct UIShell_StoreDoc UIShell_StoreDoc;
struct UIShell_StoreDoc
{
  RD_Arrangement *arrangement;
  AndamentoPanel *panels;
  U64 panel_count;
  AndamentoTab *tabs;
  CFG_Node **views;
  String8 *keys;
  U64 tab_count;
};

// The owner's panel tree as an arrangement document, its tabs named by
// their Views' slot keys.
internal UIShell_StoreDoc
uishell_store_doc(Arena *arena, CFG_Node *owner)
{
  UIShell_StoreDoc doc = {0};
  RD_Arrangement *a = doc.arrangement = rd_arrangement_from_owner(arena, owner, str8_lit("panels"));
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  { doc.panel_count += 1; doc.tab_count += p->tab_count; }
  doc.panels = push_array(arena, AndamentoPanel, doc.panel_count);
  doc.tabs = push_array(arena, AndamentoTab, doc.tab_count);
  doc.views = push_array(arena, CFG_Node *, doc.tab_count);
  doc.keys = push_array(arena, String8, doc.tab_count);
  RD_ArrangementPanel **order = push_array(arena, RD_ArrangementPanel *, doc.panel_count);
  U64 index = 0, tab = 0;
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p), index++)
  {
    order[index] = p;
    AndamentoPanel *out = &doc.panels[index];
    out->parent = ANDAMENTO_NONE;
    for(U64 j = index; j > 0 && out->parent == ANDAMENTO_NONE; j--) { if(order[j-1] == p->parent) { out->parent = j-1; } }
    out->id = uishell_sidebar_text(push_str8f(arena, "%I64u", p->id));
    // Andamento takes finite positive weights; the root's means nothing. A
    // weight is written as config writes it (0.7, not 0.699999988), which
    // reads back as the same F32.
    F32 weight = p == a->root ? 1.f : p->weight;
    out->weight = (weight > 0 && weight < 1e30f) ? f64_from_str8(rd_arrangement_weight_string(arena, weight)) : 0.01;
    out->selected = ANDAMENTO_NONE;
    if(p->first != &rd_nil_arrangement_panel)
    {
      out->kind = ANDAMENTO_PANEL_SPLIT;
      out->axis = rd_arrangement_split_axis(a, p) == Axis2_X ? ANDAMENTO_AXIS_ROW : ANDAMENTO_AXIS_COLUMN;
      continue;
    }
    out->kind = ANDAMENTO_PANEL_TABS;
    out->first_tab = tab;
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next, tab++)
    {
      doc.views[tab] = cfg_node_from_id(t->view);
      doc.keys[tab] = uishell_store_setting(doc.views[tab], str8_lit("slot"));
      doc.tabs[tab].slot = uishell_sidebar_text(doc.keys[tab]);
      if(t->view == p->selected) { out->selected = tab-out->first_tab; }
    }
    out->tab_count = tab-out->first_tab;
  }
  return doc;
}

// The document, and each View's kind and spec, as one hash.
internal U64
uishell_store_doc_hash(UIShell_StoreDoc *doc)
{
  Temp scratch = scratch_begin(0, 0);
  U64 hash = 5381;
  for(U64 i = 0; i < doc->panel_count; i++)
  {
    AndamentoPanel *p = &doc->panels[i];
    U64 weight_bits = 0;
    MemoryCopy(&weight_bits, &p->weight, sizeof(weight_bits));
    hash = uishell_store_hash_text(hash, uishell_sidebar_string(p->id));
    hash = hash*33 + p->parent;
    hash = hash*33 + weight_bits;
    hash = hash*33 + p->kind*7 + p->axis;
    hash = hash*33 + p->first_tab;
    hash = hash*33 + p->tab_count;
    hash = hash*33 + p->selected;
  }
  for(U64 i = 0; i < doc->tab_count; i++)
  {
    hash = uishell_store_hash_text(hash, doc->keys[i]);
    hash = uishell_store_hash_text(hash, doc->views[i]->string);
    UIShell_ViewSpec spec = {0};
    String8 taken[4];
    U64 taken_count = 0;
    if(uishell_store_spec_from_view(scratch.arena, doc->views[i], &spec, taken, &taken_count)) { hash = uishell_store_spec_hash(hash, &spec); }
  }
  scratch_end(scratch);
  return hash;
}

// Gives each tab a slot key no other tab of the workspace has: the one it
// has, else the key of the provider's slot it was made for (its
// resource_id) when Andamento has that slot, else "u:<resource_id>", else
// the next "u:<n>". Returns whether any changed; a View's key is saved on it.
internal B32
uishell_store_assign_keys(Arena *arena, UIShell_StoreDoc *doc, AndamentoSlots *slots)
{
  B32 changed = 0;
  U64 next = 1;
  for(U64 i = 0; i < doc->tab_count; i++) { next = Max(next, uishell_store_user_number(doc->keys[i])+1); }
  for(U64 i = 0; slots && i < andamento_slots_count(slots); i++)
  {
    AndamentoSlot slot = {0};
    if(andamento_slots_get(slots, i, &slot)) { next = Max(next, uishell_store_user_number(uishell_sidebar_string(slot.key))+1); }
  }
  for(U64 i = 0; i < doc->tab_count; i++)
  {
    B32 unique = uishell_store_key_valid(doc->keys[i]) && !uishell_store_key_in(doc->keys[i], doc->keys, i);
    if(unique) { continue; }
    String8 resource = uishell_store_setting(doc->views[i], str8_lit("resource_id"));
    String8 candidates[2] = {resource, push_str8f(arena, "u:%S", resource)};
    String8 key = str8_zero();
    if(resource.size && uishell_store_slot_find(slots, resource, 0) && !uishell_store_key_in(resource, doc->keys, doc->tab_count)) { key = candidates[0]; }
    else if(resource.size && uishell_store_user_key_valid(candidates[1]) && !uishell_store_key_in(candidates[1], doc->keys, doc->tab_count))
    { key = candidates[1]; }
    else
    {
      for(;; next++)
      {
        key = push_str8f(arena, "u:%I64u", next);
        if(!uishell_store_key_in(key, doc->keys, doc->tab_count)) { next++; break; }
      }
    }
    doc->keys[i] = key;
    doc->tabs[i].slot = uishell_sidebar_text(key);
    uishell_store_set_setting(doc->views[i], str8_lit("slot"), key);
    changed = 1;
  }
  return changed;
}

//- Building the panel tree from Andamento's document

typedef struct UIShell_StorePanel UIShell_StorePanel;
struct UIShell_StorePanel
{
  UIShell_StorePanel *first, *last, *next;
  String8 id;
  F64 weight;
  U32 kind, axis;
  U64 first_tab, tab_count, selected;
};

// Andamento's document as trees whose splits alternate axes, as a panel
// tree's must: a split along its parent's axis gives its children to the
// parent, each scaled to the share it had. Each panel by its index: a root
// is one whose parent is NONE (the sidebar's document has several).
internal UIShell_StorePanel **
uishell_store_panel_array(Arena *arena, AndamentoArrangement *arrangement, U64 count)
{
  UIShell_StorePanel **all = push_array(arena, UIShell_StorePanel *, count);
  for(U64 i = 0; i < count; i++)
  {
    AndamentoPanel p = {0};
    andamento_arrangement_panel(arrangement, i, &p);
    UIShell_StorePanel *panel = all[i] = push_array(arena, UIShell_StorePanel, 1);
    panel->id = push_str8_copy(arena, uishell_sidebar_string(p.id));
    panel->weight = p.weight;
    panel->kind = p.kind;
    panel->axis = p.axis;
    panel->first_tab = p.first_tab;
    panel->tab_count = p.tab_count;
    panel->selected = p.selected;
    if(p.parent < i) { SLLQueuePush(all[p.parent]->first, all[p.parent]->last, panel); }
  }
  for(U64 i = count; i > 0; i--)
  {
    UIShell_StorePanel *parent = all[i-1];
    if(parent->kind != ANDAMENTO_PANEL_SPLIT) { continue; }
    UIShell_StorePanel *first = 0, *last = 0;
    for(UIShell_StorePanel *child = parent->first, *next = 0; child; child = next)
    {
      next = child->next;
      child->next = 0;
      if(child->kind == ANDAMENTO_PANEL_SPLIT && child->axis == parent->axis)
      {
        F64 total = 0;
        for(UIShell_StorePanel *g = child->first; g; g = g->next) { total += g->weight; }
        for(UIShell_StorePanel *g = child->first, *g_next = 0; g; g = g_next)
        {
          g_next = g->next;
          g->next = 0;
          g->weight = total > 0 ? child->weight*g->weight/total : child->weight;
          SLLQueuePush(first, last, g);
        }
        continue;
      }
      SLLQueuePush(first, last, child);
    }
    parent->first = first;
    parent->last = last;
  }
  return all;
}

internal UIShell_StorePanel *
uishell_store_panels(Arena *arena, AndamentoArrangement *arrangement, U64 count)
{
  return count ? uishell_store_panel_array(arena, arrangement, count)[0] : 0;
}

// Replaces `owner`'s panel tree with Andamento's document, keeping the View
// node of every slot it still tabs and the node (with its Presentation
// State) of every panel whose ID it keeps. New Views take their settings
// from the device-local `arrangement_presentation`, which goes. Returns
// whether Andamento had a document to build.
internal B32
uishell_store_build(UIShell_SidebarState *state, CFG_Node *owner, UIShell_WorkspaceId id, U64 *generation_out)
{
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  AndamentoWorkspaceId workspace = uishell_sidebar_workspace(id);
  AndamentoSlots *slots = UIShell_StoreCall(state, andamento_slots_acquire(state->core, workspace, 0));
  AndamentoArrangement *stored = slots ? UIShell_StoreCall(state, andamento_arrangement_acquire(state->core, workspace, 0)) : 0;
  AndamentoArrangementInfo info = {0};
  B32 built = stored && andamento_arrangement_info(stored, &info);
  if(built && generation_out) { *generation_out = info.generation; }
  CFG_Node *device = cfg_node_child_from_string(owner, str8_lit("arrangement_presentation"));
  if(built && info.panel_count == 0 && device == &cfg_nil_node) { built = 0; }
  if(built)
  {
    // What this device kept, and the Views and panels config has now.
    UIShell_StoreDoc current = uishell_store_doc(arena, owner);
    RD_Arrangement *a = current.arrangement;
    U64 old_count = current.panel_count;
    RD_PanelID *old_ids = push_array(arena, RD_PanelID, old_count);
    CFG_ID *old_cfgs = push_array(arena, CFG_ID, old_count);
    U64 n = 0;
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p), n++)
    { old_ids[n] = p->id; old_cfgs[n] = p->cfg; }
    UIShell_StorePanel *root = uishell_store_panels(arena, stored, info.panel_count);
    AndamentoTab *tabs = push_array(arena, AndamentoTab, info.tab_count);
    for(U64 i = 0; i < info.tab_count; i++) { andamento_arrangement_tab(stored, i, &tabs[i]); }

    // Andamento's IDs are this device's numbers once it has committed; a
    // provider's hint names panels in words, which get numbers here.
    U64 max_id = 0;
    for(U64 i = 0; i < old_count; i++) { max_id = Max(max_id, old_ids[i]); }
    for(U64 i = 0; i < info.panel_count; i++)
    {
      AndamentoPanel p = {0};
      andamento_arrangement_panel(stored, i, &p);
      String8 text = uishell_sidebar_string(p.id);
      if(str8_is_integer(text, 10) && text.size < 18) { max_id = Max(max_id, u64_from_str8(text, 10)); }
    }
    U64 fresh = max_id+1;
    if(!root) { rd_arrangement_discard(a); }
    else { rd_arrangement_clear(a); }
    // Allocations stay clear of the IDs set below until they are all set.
    a->next_id = max_id + (1ull << 40);
    struct Pending { struct Pending *next; UIShell_StorePanel *panel; RD_PanelID parent; } *queue = 0, *queue_last = 0;
    if(root)
    {
      struct Pending *start = push_array(arena, struct Pending, 1);
      start->panel = root;
      SLLQueuePush(queue, queue_last, start);
    }
    if(root && root->kind == ANDAMENTO_PANEL_SPLIT) { a->root_axis = root->axis == ANDAMENTO_AXIS_ROW ? Axis2_X : Axis2_Y; }
    U64 ids_used = 0;
    RD_PanelID *ids = push_array(arena, RD_PanelID, info.panel_count+1);
    for(struct Pending *item = queue; item; item = item->next)
    {
      UIShell_StorePanel *src = item->panel;
      RD_PanelID panel_id = item->parent ? rd_arrangement_add(a, item->parent, (F32)src->weight) : a->root->id;
      RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(a, panel_id);
      RD_PanelID wanted = str8_is_integer(src->id, 10) && src->id.size < 18 ? u64_from_str8(src->id, 10) : 0;
      for(U64 i = 0; i < ids_used && wanted; i++) { if(ids[i] == wanted) { wanted = 0; } }
      if(!wanted) { wanted = fresh++; }
      panel->id = wanted;
      ids[ids_used++] = wanted;
      for(U64 i = 0; i < old_count; i++) { if(old_ids[i] == wanted) { panel->cfg = old_cfgs[i]; } }
      for(UIShell_StorePanel *child = src->first; child; child = child->next)
      {
        struct Pending *pending = push_array(arena, struct Pending, 1);
        pending->panel = child;
        pending->parent = wanted;
        SLLQueuePush(queue, queue_last, pending);
      }
      if(src->kind != ANDAMENTO_PANEL_TABS) { continue; }
      CFG_ID prev = 0, selected = 0;
      for(U64 t = src->first_tab; t < src->first_tab+src->tab_count && t < info.tab_count; t++)
      {
        String8 key = uishell_sidebar_string(tabs[t].slot);
        CFG_Node *view = &cfg_nil_node;
        for(U64 i = 0; i < current.tab_count && view == &cfg_nil_node; i++)
        { if(str8_match(current.keys[i], key, 0)) { view = current.views[i]; } }
        if(view == &cfg_nil_node)
        {
          AndamentoSlot slot = {0};
          UIShell_ViewSpec spec = {0};
          B32 known = !tabs[t].gone && uishell_store_slot_find(slots, key, &slot);
          if(known) { spec = uishell_store_spec_from_andamento(arena, &slot.spec); }
          view = uishell_store_view_from_spec(key, known ? &spec : 0);
          // This device's settings for it, Target Resolutions included.
          for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next)
          {
            B32 settings = str8_match(c->string, str8_lit("view"), 0) || str8_match(c->string, str8_lit("resolution"), 0);
            if(!settings || !str8_match(uishell_store_setting(c, str8_lit("slot")), key, 0)) { continue; }
            for(CFG_Node *s = c->first; s != &cfg_nil_node; s = s->next)
            {
              if(str8_match(s->string, str8_lit("slot"), 0)) { continue; }
              cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, s->string));
              cfg_node_insert_child(rd_state->cfg, view, view->last, cfg_node_deep_copy(rd_state->cfg, s));
            }
          }
        }
        rd_arrangement_insert_tab(a, view->id, wanted, prev, 0);
        if(t-src->first_tab == src->selected) { selected = view->id; }
        prev = view->id;
      }
      rd_arrangement_select(a, wanted, selected);
    }
    a->next_id = fresh;
    for(U64 i = 0; i < ids_used; i++) { a->next_id = Max(a->next_id, ids[i]+1); }
    rd_arrangement_save(rd_state->cfg, a);
    // Presentation State for panels this tree had no node for.
    for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next)
    {
      if(!str8_match(c->string, str8_lit("panel"), 0)) { continue; }
      String8 panel_id = uishell_store_setting(c, str8_lit("id"));
      RD_ArrangementPanel *panel = str8_is_integer(panel_id, 10) ? rd_arrangement_panel_from_id(a, u64_from_str8(panel_id, 10)) : &rd_nil_arrangement_panel;
      CFG_Node *node = cfg_node_from_id(panel->cfg);
      if(node == &cfg_nil_node) { continue; }
      for(CFG_Node *s = c->first; s != &cfg_nil_node; s = s->next)
      {
        if(str8_match(s->string, str8_lit("id"), 0) || cfg_node_child_from_string(node, s->string) != &cfg_nil_node) { continue; }
        cfg_node_insert_child(rd_state->cfg, node, node->last, cfg_node_deep_copy(rd_state->cfg, s));
      }
    }
    if(device != &cfg_nil_node) { cfg_node_release(rd_state->cfg, device); }
    uishell_store_set_setting(owner, str8_lit("arrangement_generation"), push_str8f(arena, "%I64u", info.generation));
  }
  andamento_arrangement_release(stored);
  andamento_slots_release(slots);
  scratch_end(scratch);
  return built;
}

//- Sync

internal UIShell_StoreEntry *
uishell_store_entry(UIShell_SidebarState *state, UIShell_WorkspaceId id)
{
  for(U64 i = 0; i < state->store_entry_count; i++)
  { if(uishell_workspace_id_match(state->store_entries[i].id, id)) { return &state->store_entries[i]; } }
  if(state->store_entry_count == state->store_entry_capacity)
  {
    U64 capacity = Max(16, state->store_entry_capacity*2);
    UIShell_StoreEntry *entries = push_array(state->store_arena, UIShell_StoreEntry, capacity);
    MemoryCopy(entries, state->store_entries, sizeof(*entries)*state->store_entry_count);
    state->store_entries = entries;
    state->store_entry_capacity = capacity;
  }
  UIShell_StoreEntry *entry = &state->store_entries[state->store_entry_count++];
  MemoryZeroStruct(entry);
  entry->id = id;
  return entry;
}

// The window's workspaces with an arrangement: its own layout once it is
// one, and its open and kept workspaces.
internal CFG_NodePtrList
uishell_store_owners(Arena *arena, CFG_Node *window)
{
  CFG_NodePtrList owners = {0};
  if(cfg_node_child_from_string(window, str8_lit("panels")) != &cfg_nil_node ||
     cfg_node_child_from_string(window, str8_lit("arrangement_generation")) != &cfg_nil_node ||
     cfg_node_child_from_string(window, str8_lit("arrangement_presentation")) != &cfg_nil_node)
  { cfg_node_ptr_list_push(arena, &owners, window); }
  for(CFG_Node *c = window->first; c != &cfg_nil_node; c = c->next)
  {
    if(str8_match(c->string, str8_lit("workspace"), 0) || str8_match(c->string, str8_lit("detached_workspace"), 0))
    { cfg_node_ptr_list_push(arena, &owners, c); }
  }
  return owners;
}

// A gesture is in flight: a tab or card drag, or a panel boundary drag.
// Nothing reaches Andamento until it ends.
internal B32
uishell_store_gesture_in_flight(void)
{
  return rd_drag_is_active() || rd_state->boundary_resize.arena != 0;
}

// Commits `owner`'s panel tree if it changed since it was last committed or
// built, then follows anything Andamento placed; else, if Andamento moved
// the document itself, follows it.
internal void
uishell_store_sync_owner(UIShell_SidebarState *state, CFG_Node *owner, B32 revision_moved)
{
  // Not built yet: committing would replace what Andamento keeps.
  if(cfg_node_child_from_string(owner, str8_lit("arrangement_presentation")) != &cfg_nil_node) { return; }
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  UIShell_WorkspaceId id = uishell_workspace_id_from_cfg(owner);
  AndamentoWorkspaceId workspace = uishell_sidebar_workspace(id);
  UIShell_StoreDoc doc = uishell_store_doc(arena, owner);
  B32 keyed = 1;
  for(U64 i = 0; i < doc.tab_count && keyed; i++)
  { keyed = uishell_store_key_valid(doc.keys[i]) && !uishell_store_key_in(doc.keys[i], doc.keys, i); }
  U64 hash = uishell_store_doc_hash(&doc);
  UIShell_StoreEntry *entry = uishell_store_entry(state, id);
  B32 marked = cfg_node_child_from_string(owner, str8_lit("arrangement_generation")) != &cfg_nil_node;
  B32 changed = !keyed || entry->hash != hash;
  // An empty workspace never committed has nothing to keep.
  if(changed && !marked && doc.panel_count == 0) { changed = 0; entry->hash = hash; }
  if(!changed && !revision_moved) { scratch_end(scratch); return; }
  AndamentoSlots *slots = UIShell_StoreCall(state, andamento_slots_acquire(state->core, workspace, 0));
  AndamentoArrangement *stored = slots ? UIShell_StoreCall(state, andamento_arrangement_acquire(state->core, workspace, 0)) : 0;
  AndamentoArrangementInfo info = {0};
  // A workspace Andamento doesn't know yet (a subject's registers once it
  // has opened) is committed when it does.
  if(!stored || !andamento_arrangement_info(stored, &info))
  {
    andamento_arrangement_release(stored);
    andamento_slots_release(slots);
    scratch_end(scratch);
    return;
  }
  if(!changed)
  {
    // Andamento moved the document itself.
    if(entry->generation != info.generation && marked)
    {
      uishell_store_build(state, owner, id, &entry->generation);
      UIShell_StoreDoc rebuilt = uishell_store_doc(arena, owner);
      entry->hash = uishell_store_doc_hash(&rebuilt);
    }
    andamento_arrangement_release(stored);
    andamento_slots_release(slots);
    scratch_end(scratch);
    return;
  }
  if(uishell_store_assign_keys(arena, &doc, slots)) { hash = uishell_store_doc_hash(&doc); }

  // New and changed slots. A slot following its provider is the provider's,
  // and a placeholder's keeps the spec it was made from.
  B32 ok = 1;
  for(U64 i = 0; i < doc.tab_count && ok; i++)
  {
    UIShell_ViewSpec spec = {0};
    String8 taken[4];
    U64 taken_count = 0;
    if(!uishell_store_spec_from_view(arena, doc.views[i], &spec, taken, &taken_count)) { continue; }
    AndamentoSlot slot = {0};
    B32 exists = uishell_store_slot_find(slots, doc.keys[i], &slot);
    B32 follows = exists && slot.in_baseline && !slot.detached;
    uishell_store_mark_follows(doc.views[i], follows);
    if(follows) { continue; }
    UIShell_ViewSpec current = exists ? uishell_store_spec_from_andamento(arena, &slot.spec) : (UIShell_ViewSpec){0};
    if(exists && uishell_store_spec_match(&spec, &current)) { continue; }
    AndamentoViewSpec out = uishell_store_andamento_spec(&spec);
    char *error = 0;
    ok = uishell_sidebar_result(state, UIShell_StoreCall(state, andamento_slot_set(state->core, workspace, uishell_sidebar_text(doc.keys[i]),
                                                                                    &out, ANDAMENTO_REBIND_REPLACE, &error)), error);
  }

  // The document, at the generation this device last saw.
  U64 expected = marked ? entry->generation : info.generation;
  if(marked && !entry->generation)
  {
    String8 saved = uishell_store_setting(owner, str8_lit("arrangement_generation"));
    expected = str8_is_integer(saved, 10) ? u64_from_str8(saved, 10) : info.generation;
  }
  U64 generation = 0;
  uint32_t result = ANDAMENTO_ARRANGEMENT_INVALID;
  char *error = 0;
  for(U32 attempt = 0; ok && attempt < 2; attempt++)
  {
    error = 0;
    result = UIShell_StoreCall(state, andamento_set_arrangement(state->core, workspace, doc.panels, doc.panel_count, doc.tabs, doc.tab_count,
                                                                expected, &generation, &error));
    state->store_commits += 1;
    if(result != ANDAMENTO_ARRANGEMENT_STALE) { break; }
    // Stale: Andamento reconciled a provider's change since. Committing at
    // the new generation reconciles it again, keeping both.
    expected = generation;
  }
  if(ok && result != ANDAMENTO_ARRANGEMENT_COMMITTED)
  { uishell_sidebar_set_error(state, error ? str8_cstring(error) : str8_lit("The workspace's arrangement was not committed")); }
  andamento_string_free(error);
  B32 committed = ok && result == ANDAMENTO_ARRANGEMENT_COMMITTED;

  // The user's slots whose tabs it closed go; a provider's stays its own,
  // and a slot it never had a tab for (another host's, just added) stays
  // for Andamento to place.
  String8 *closed = push_array(arena, String8, info.tab_count);
  U64 closed_count = 0;
  for(U64 i = 0; committed && i < info.tab_count; i++)
  {
    AndamentoTab tab = {0};
    if(!andamento_arrangement_tab(stored, i, &tab)) { continue; }
    String8 key = push_str8_copy(arena, uishell_sidebar_string(tab.slot));
    if(!uishell_store_key_in(key, doc.keys, doc.tab_count)) { closed[closed_count++] = key; }
  }
  for(U64 i = 0; committed && i < andamento_slots_count(slots); i++)
  {
    AndamentoSlot slot = {0};
    if(!andamento_slots_get(slots, i, &slot) || slot.in_baseline) { continue; }
    String8 key = uishell_sidebar_string(slot.key);
    if(!uishell_store_key_in(key, closed, closed_count)) { continue; }
    char *remove_error = 0;
    uishell_sidebar_result(state, UIShell_StoreCall(state, andamento_slot_remove(state->core, workspace, slot.key, &remove_error)), remove_error);
  }
  andamento_arrangement_release(stored);
  andamento_slots_release(slots);
  entry->hash = hash;
  if(committed)
  {
    entry->generation = generation;
    uishell_store_set_setting(owner, str8_lit("arrangement_generation"), push_str8f(arena, "%I64u", generation));
    // Anything Andamento placed (slots the document had no tab for) joins
    // the panel tree.
    AndamentoArrangement *now = UIShell_StoreCall(state, andamento_arrangement_acquire(state->core, workspace, 0));
    AndamentoArrangementInfo now_info = {0};
    B32 placed = 0;
    for(U64 i = 0; now && andamento_arrangement_info(now, &now_info) && i < now_info.tab_count && !placed; i++)
    {
      AndamentoTab tab = {0};
      placed = andamento_arrangement_tab(now, i, &tab) && tab.placed;
    }
    andamento_arrangement_release(now);
    if(placed)
    {
      uishell_store_build(state, owner, id, &entry->generation);
      UIShell_StoreDoc rebuilt = uishell_store_doc(arena, owner);
      entry->hash = uishell_store_doc_hash(&rebuilt);
    }
  }
  scratch_end(scratch);
}

// Each frame the sidebar observes its window: commits what changed once no
// gesture is in flight. `force` commits even then (the window is closing).
internal void
uishell_workspace_store_sync(UIShell_SidebarState *state, CFG_Node *window, B32 force)
{
  if(!state->core || !state->window || window->id != state->window) { return; }
  if(!force && uishell_store_gesture_in_flight()) { return; }
  if(!state->store_arena) { state->store_arena = arena_alloc(); }
  char *error = 0;
  U64 revision = UIShell_StoreCall(state, andamento_workspace_content_revision(state->core, &error));
  andamento_string_free(error);
  B32 revision_moved = state->store_synced && revision != state->store_content_revision;
  // A new snapshot can mean a workspace Andamento didn't know has registered
  // (a subject's, once it opened), which then commits.
  if(state->store_synced && cfg_change_gen() == state->store_cfg_gen && state->snapshot_count == state->store_snapshot_count &&
     !revision_moved) { return; }
  Temp scratch = scratch_begin(0, 0);
  CFG_NodePtrList owners = uishell_store_owners(scratch.arena, window);
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next) { uishell_store_sync_owner(state, n->v, revision_moved); }
  scratch_end(scratch);
  state->store_synced = 1;
  state->store_cfg_gen = cfg_change_gen();
  state->store_snapshot_count = state->snapshot_count;
  state->store_content_revision = UIShell_StoreCall(state, andamento_workspace_content_revision(state->core, 0));
}

// Once the window's records are imported: each workspace saved without its
// panel tree gets it from Andamento.
internal void
uishell_workspace_store_load(UIShell_SidebarState *state, CFG_Node *window)
{
  if(!state->core) { return; }
  if(!state->store_arena) { state->store_arena = arena_alloc(); }
  Temp scratch = scratch_begin(0, 0);
  CFG_NodePtrList owners = uishell_store_owners(scratch.arena, window);
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next)
  {
    CFG_Node *owner = n->v;
    if(cfg_node_child_from_string(owner, str8_lit("arrangement_presentation")) == &cfg_nil_node) { continue; }
    UIShell_WorkspaceId id = uishell_workspace_id_from_cfg(owner);
    UIShell_StoreEntry *entry = uishell_store_entry(state, id);
    if(uishell_store_build(state, owner, id, &entry->generation))
    {
      UIShell_StoreDoc doc = uishell_store_doc(scratch.arena, owner);
      entry->hash = uishell_store_doc_hash(&doc);
    }
  }
  scratch_end(scratch);
}

internal void
uishell_workspace_store_release(UIShell_SidebarState *state)
{
  if(state->store_arena) { arena_release(state->store_arena); }
  state->store_arena = 0;
  state->store_entries = 0;
  state->store_entry_count = state->store_entry_capacity = 0;
  state->store_synced = 0;
}

//- Device-local

// `window` as the presentation file holds it: each workspace whose
// arrangement Andamento keeps is written without its panel tree, with this
// device's part of it in `arrangement_presentation`.
internal String8
uishell_workspace_store_window_text(Arena *arena, String8 root_path, CFG_Node *window)
{
  Temp scratch = scratch_begin(&arena, 1);
  CFG_NodePtrList owners = uishell_store_owners(scratch.arena, window);
  CFG_State *copy_state = cfg_state_alloc();
  CFG_Node *copy = cfg_node_deep_copy(copy_state, window);
  // Each workspace's copy, found before any copy changes.
  CFG_Node **copies = push_array(scratch.arena, CFG_Node *, owners.count);
  {
    U64 i = 0;
    for(CFG_NodePtrNode *n = owners.first; n; n = n->next, i++)
    {
      copies[i] = copy;
      if(n->v == window) { continue; }
      CFG_Node *src = window->first, *dst = copy->first;
      for(; src != n->v; src = src->next, dst = dst->next) {}
      copies[i] = dst;
    }
  }
  U64 owner_index = 0;
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next, owner_index++)
  {
    CFG_Node *owner = n->v;
    CFG_Node *panels = cfg_node_child_from_string(owner, str8_lit("panels"));
    if(cfg_node_child_from_string(owner, str8_lit("arrangement_generation")) == &cfg_nil_node || panels == &cfg_nil_node) { continue; }
    CFG_Node *dst = copies[owner_index];
    String8 drop[] = {str8_lit("panels"), str8_lit("split_x"), str8_lit("arrangement_presentation")};
    for(U64 i = 0; i < ArrayCount(drop); i++)
    {
      for(CFG_Node *c = cfg_node_child_from_string(dst, drop[i]); c != &cfg_nil_node; c = cfg_node_child_from_string(dst, drop[i]))
      { cfg_node_release(copy_state, c); }
    }
    CFG_Node *device = cfg_node_new(copy_state, dst, str8_lit("arrangement_presentation"));
    RD_Arrangement *a = rd_arrangement_from_cfg(scratch.arena, panels);
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    {
      CFG_Node *node = cfg_node_from_id(p->cfg), *entry = &cfg_nil_node;
      for(CFG_Node *c = node->first; c != &cfg_nil_node; c = c->next)
      {
        if(rd_arrangement_child_from_cfg(c) != RD_ArrangementChild_Other || str8_match(c->string, str8_lit("id"), 0)) { continue; }
        if(entry == &cfg_nil_node)
        {
          entry = cfg_node_new(copy_state, device, str8_lit("panel"));
          cfg_node_new(copy_state, cfg_node_new(copy_state, entry, str8_lit("id")), push_str8f(scratch.arena, "%I64u", p->id));
        }
        cfg_node_insert_child(copy_state, entry, entry->last, cfg_node_deep_copy(copy_state, c));
      }
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        CFG_Node *view = cfg_node_from_id(t->view);
        String8 key = uishell_store_setting(view, str8_lit("slot"));
        if(!key.size) { continue; }
        UIShell_ViewSpec spec = {0};
        String8 taken[4];
        U64 taken_count = 0;
        uishell_store_spec_from_view(scratch.arena, view, &spec, taken, &taken_count);
        if(cfg_node_child_from_string(view, str8_lit("follows_provider")) != &cfg_nil_node) { taken_count = 0; }
        CFG_Node *groups[2] = {&cfg_nil_node, &cfg_nil_node};
        for(CFG_Node *c = view->first; c != &cfg_nil_node; c = c->next)
        {
          if(str8_match(c->string, str8_lit("slot"), 0) || str8_match(c->string, str8_lit("selected"), 0) ||
             uishell_store_key_in(c->string, taken, taken_count)) { continue; }
          U64 g = uishell_store_key_in(c->string, uishell_store_resolution_keys, ArrayCount(uishell_store_resolution_keys)) ? 1 : 0;
          if(groups[g] == &cfg_nil_node)
          {
            groups[g] = cfg_node_new(copy_state, device, g ? str8_lit("resolution") : str8_lit("view"));
            cfg_node_new(copy_state, cfg_node_new(copy_state, groups[g], str8_lit("slot")), key);
          }
          cfg_node_insert_child(copy_state, groups[g], groups[g]->last, cfg_node_deep_copy(copy_state, c));
        }
      }
    }
  }
  // The sidebar's arrangement is the Dashboard's (uishell_sidebar_store.c).
  uishell_sidebar_store_presentation(copy_state, window, copy);
  String8 result = cfg_string_from_tree(arena, rd_state->cfg_schema_table, root_path, copy);
  cfg_state_release(copy_state);
  scratch_end(scratch);
  return result;
}
