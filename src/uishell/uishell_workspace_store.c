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
//   plan    Then every Slot is planned (andamento_slot_plan), and an update
//           Andamento has for it is applied to its View ("Slot plans"
//           below): a spec another host changed, or a provider's new target,
//           reaches a View that is already open.
//
// Config can change without its generation moving (moving a node keeps it),
// so whether to sync is decided by a hash of the window's workspaces' nodes
// as well (uishell_store_tree_hash).
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
//       resolution: {slot: "u:3" attach_token: ..}  machine-local Target Resolutions
//   } }
//
// A View whose slot follows its provider (`follows_provider`) keeps all of
// its settings here: what it runs is the provider's content as this device
// last resolved it, and its next plan applies the provider's again. Floating
// Panels are Presentation State, and their Views are not Slots.
//
// Target Resolutions (CONTEXT.md): which are portable is Wheelhouse's call.
// A terminal's Cleat daemon session is: its `session` and `daemon_name`,
// with this machine's name, are saved with the Slot in Andamento
// (andamento_slot_resolution_set, kind "cleat-session"), and a View that
// starts without an instance (a restart, another device) attaches through
// it before starting afresh ("Portable Target Resolutions" below). A session
// saved from another machine is left alone: Cleat reaches daemons on this
// one only. The rest are machine-local and stay here: `attach_token` and
// `porthole_session` (a capture session on this machine) and
// `managed_target` (the target this device last applied). A Jackstay
// endpoint is part of its View Spec, so Andamento has it already.

StaticAssert(WH_Content_Command == 1<<ANDAMENTO_SLOT_COMMAND && WH_Content_File == 1<<ANDAMENTO_SLOT_FILE &&
             WH_Content_Url == 1<<ANDAMENTO_SLOT_URL && WH_Content_Jackstay == 1<<ANDAMENTO_SLOT_JACKSTAY &&
             WH_Content_Facet == 1<<ANDAMENTO_SLOT_FACET, uishell_store_content_kinds);

typedef struct UIShell_StoreFlag UIShell_StoreFlag;
struct UIShell_StoreFlag
{
  String8 key;
  // ANDAMENTO_SLOT_FLAG_*
  U32 flags;
};

typedef struct UIShell_StoreEntry UIShell_StoreEntry;
struct UIShell_StoreEntry
{
  UIShell_WorkspaceId id;
  // The document and specs last committed or built, and its generation.
  U64 hash;
  U64 generation;
  // What Andamento says of its Workspace Overlay, as last read
  // (uishell_store_read_overlay): the flags of each flagged Slot, the keys
  // tombstones hide, and the arrangement's flags.
  UIShell_StoreFlag *flags;
  U64 flag_count;
  String8 *tombstones;
  U64 tombstone_count;
  // Untouched slots the provider removed, whose instances follow their
  // rebind policy until released: their keys, and the policies in `flags`.
  UIShell_StoreFlag *departed;
  U64 departed_count;
  U32 arrangement_flags;
};

internal UIShell_StoreEntry *uishell_store_entry_find(UIShell_SidebarState *state, UIShell_WorkspaceId id);

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
// In WH_TargetResolution's order (uishell_store_view_slot).
StaticAssert(ArrayCount(uishell_store_resolution_keys) == 5, uishell_store_resolution_fields);

// Runtime-only View settings: this process's progress through its Slot's
// plan, never saved (uishell_workspace_store_window_text drops them), so a
// restart plans afresh from what Andamento keeps.
read_only global String8 uishell_store_runtime_keys[] =
{
  // The resolution identity the View applied, as andamento_slot_plan takes it.
  str8_lit_comp("applied"),
  // An `ask` rebind waiting for the user's answer: what it would show.
  str8_lit_comp("asks"),
  // The user declined that update, so focusing the workspace doesn't retry it.
  str8_lit_comp("declined"),
  // Applying an update failed; focusing the workspace retries it.
  str8_lit_comp("update_failed"),
  // The session and daemon Andamento has saved for it ("session|daemon").
  str8_lit_comp("portable"),
  // A hash of the content it showed once it applied that resolution: a View
  // following its provider whose content differs since, the user edited.
  str8_lit_comp("applied_spec"),
};

internal U64 uishell_store_view_spec_hash(CFG_Node *view);

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

// Removes `node`'s `key` if it has one. (Releasing the nil node still counts
// as a change.)
internal void
uishell_store_drop_setting(CFG_Node *node, String8 key)
{
  CFG_Node *setting = cfg_node_child_from_string(node, key);
  if(setting != &cfg_nil_node) { cfg_node_release(rd_state->cfg, setting); }
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

// The workspace whose arrangement holds `view`: a workspace node, or a window
// whose own panels are its workspace. Nil for a View outside one (a control
// view, a Floating Panel's, an immediate tree's).
internal CFG_Node *
uishell_store_view_owner(CFG_Node *view)
{
  CFG_Node *child = view;
  for(CFG_Node *p = view->parent; p != &cfg_nil_node; child = p, p = p->parent)
  {
    if(str8_match(p->string, str8_lit("workspace"), 0) || str8_match(p->string, str8_lit("detached_workspace"), 0) ||
       str8_match(p->string, str8_lit("window"), 0))
    {
      return str8_match(child->string, str8_lit("panels"), 0) ? p : &cfg_nil_node;
    }
  }
  return &cfg_nil_node;
}

internal void
uishell_store_view_slot(Arena *arena, CFG_Node *view, WH_SlotAddress *slot, UIShell_ViewSpec const **spec,
                        WH_SlotStatus *status, WH_TargetResolution *resolution)
{
  String8 *resolved[] = {&resolution->session, &resolution->daemon_name, &resolution->attach_token,
                         &resolution->managed_target, &resolution->porthole_session};
  for(U64 i = 0; i < ArrayCount(resolved); i++) { *resolved[i] = uishell_store_setting(view, uishell_store_resolution_keys[i]); }
  if(str8_match(view->string, str8_lit("placeholder"), 0)) { *status |= WH_SlotStatus_Placeholder; }
  CFG_Node *owner = uishell_store_view_owner(view);
  String8 key = uishell_store_setting(view, str8_lit("slot"));
  if(owner == &cfg_nil_node || key.size == 0) { return; }
  // Read, never assigned: an arrangement's workspace has had its ID since
  // the inventory first listed it.
  uishell_workspace_id_from_string(uishell_store_setting(owner, str8_lit("workspace_id")), &slot->workspace);
  slot->key = key;
  *status |= WH_SlotStatus_Slot;
  // A Slot following its provider has the provider's spec, which Andamento
  // keeps; this device holds only what it last resolved it to.
  if(cfg_node_child_from_string(view, str8_lit("follows_provider")) != &cfg_nil_node)
  {
    *status |= WH_SlotStatus_FollowsProvider;
    return;
  }
  UIShell_ViewSpec *view_spec = push_array(arena, UIShell_ViewSpec, 1);
  String8 taken[4];
  U64 taken_count = 0;
  if(uishell_store_spec_from_view(arena, view, view_spec, taken, &taken_count)) { *spec = view_spec; }
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

// A View's kind and content as one hash ("applied_spec").
internal U64
uishell_store_view_spec_hash(CFG_Node *view)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_ViewSpec spec = {0};
  String8 taken[4];
  U64 taken_count = 0;
  U64 hash = uishell_store_hash_text(5381, view->string);
  if(uishell_store_spec_from_view(scratch.arena, view, &spec, taken, &taken_count)) { hash = uishell_store_spec_hash(hash, &spec); }
  scratch_end(scratch);
  return hash;
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
      if(kind.size && (wh_renderer_from_name(kind)->content & WH_Content_Page)) { return str8_zero(); }
      return push_str8_copy(arena, spec->url);
    }
    case ANDAMENTO_SLOT_FACET:
    {
      // A facet resolves to a recipe its plan applies (a terminal unless it
      // asks to be presented otherwise); until then, what it waits for.
      B32 terminal = spec->presentation.size == 0 || str8_match(spec->presentation, str8_lit("terminal"), 0);
      if(terminal) { return str8_zero(); }
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
  String8 kind = wh_renderer_from_content(WH_Content_Unshown)->name, expr = str8_zero(), cwd = str8_zero();
  B32 has_cwd = 0;
  if(!unshown.size)
  {
    // The Renderer for the content: the first in the registry that shows it.
    kind = wh_renderer_from_content(1<<spec->content)->name;
    switch(spec->content)
    {
      case ANDAMENTO_SLOT_COMMAND: { expr = spec->command; has_cwd = spec->has_cwd; cwd = spec->cwd; } break;
      case ANDAMENTO_SLOT_FILE: { expr = rd_eval_string_from_file_path(scratch.arena, spec->path); } break;
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
  if(spec && spec->content == ANDAMENTO_SLOT_FACET)
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

// A Slot's previous instance, kept beside its View by a keep-previous rebind
// ("Slot plans"): a tab of the panel tree, but not a Slot, so not in the
// document.
internal String8
uishell_store_previous_of(CFG_Node *view)
{
  return uishell_store_setting(view, str8_lit("previous_of"));
}

// The owner's panel tree as an arrangement document, its tabs named by
// their Views' slot keys.
internal UIShell_StoreDoc
uishell_store_doc(Arena *arena, CFG_Node *owner)
{
  UIShell_StoreDoc doc = {0};
  RD_Arrangement *a = doc.arrangement = rd_arrangement_from_owner(arena, owner, str8_lit("panels"));
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    doc.panel_count += 1;
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next) { doc.tab_count += !uishell_store_previous_of(cfg_node_from_id(t->view)).size; }
  }
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
    // A provider's panel keeps the ID its document gave it (`doc_id`).
    String8 doc_id = uishell_store_setting(cfg_node_from_id(p->cfg), str8_lit("doc_id"));
    out->id = uishell_sidebar_text(doc_id.size ? doc_id : push_str8f(arena, "%I64u", p->id));
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
    // A previous instance selected stands for its Slot's View.
    String8 selected_previous = uishell_store_previous_of(cfg_node_from_id(p->selected));
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
    {
      CFG_Node *view = cfg_node_from_id(t->view);
      if(uishell_store_previous_of(view).size) { continue; }
      doc.views[tab] = view;
      doc.keys[tab] = uishell_store_setting(doc.views[tab], str8_lit("slot"));
      doc.tabs[tab].slot = uishell_sidebar_text(doc.keys[tab]);
      if(t->view == p->selected || (selected_previous.size && str8_match(selected_previous, doc.keys[tab], 0)))
      { out->selected = tab-out->first_tab; }
      tab++;
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

//- Portable Target Resolutions
//
// A Cleat daemon session is saved with its Slot as kind "cleat-session",
// fields `host` (this machine's name), `session` and `daemon` (its
// daemon_name; none for the default daemon), made for the resolution the View
// applied. A View marks what Andamento has (`portable`), so the presentation
// file leaves those settings out (uishell_workspace_store_window_text).

// This machine's name, which a saved Cleat session names its daemon's host by.
internal String8
uishell_store_host(void)
{
  String8 name = get_system_info()->machine_name;
  return name.size ? name : str8_lit("localhost");
}

internal String8
uishell_store_resolution_field(AndamentoResolution *r, String8 name)
{
  for(U64 i = 0; i < r->field_count; i++)
  { if(str8_match(uishell_sidebar_string(r->fields[i].name), name, 0)) { return uishell_sidebar_string(r->fields[i].value); } }
  return str8_zero();
}

// Whether `r` is a Cleat session this machine saved: one it can attach to.
internal B32
uishell_store_resolution_ours(AndamentoResolution *r)
{
  return str8_match(uishell_sidebar_string(r->kind), str8_lit("cleat-session"), 0) &&
         str8_match(uishell_store_resolution_field(r, str8_lit("host")), uishell_store_host(), 0);
}

internal String8
uishell_store_portable_text(Arena *arena, String8 session, String8 daemon)
{
  return push_str8f(arena, "%S|%S", session, daemon);
}

// Gives terminal `view`, which hasn't a session, the saved session `r` when
// this machine can attach to it. Returns whether it did.
internal B32
uishell_store_restore_resolution(CFG_Node *view, AndamentoResolution *r)
{
  String8 session = uishell_store_resolution_field(r, str8_lit("session"));
  if(!str8_match(view->string, str8_lit("terminal"), 0) || !uishell_store_resolution_ours(r) || !session.size ||
     uishell_store_setting(view, str8_lit("session")).size) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  String8 daemon = uishell_store_resolution_field(r, str8_lit("daemon"));
  uishell_store_set_setting(view, str8_lit("session"), session);
  if(daemon.size) { uishell_store_set_setting(view, str8_lit("daemon_name"), daemon); }
  else { uishell_store_drop_setting(view, str8_lit("daemon_name")); }
  uishell_store_set_setting(view, str8_lit("portable"), uishell_store_portable_text(scratch.arena, session, daemon));
  scratch_end(scratch);
  return 1;
}

// Saves terminal `view`'s daemon session with Slot `key`, for the resolution
// it applied (`applied`, which its plan reports current), when Andamento
// has none or this machine's older one (`saved`, from the plan; 0 for none);
// clears this machine's once the View has none. Another machine's is left
// alone.
internal void
uishell_store_save_resolution(UIShell_SidebarState *state, AndamentoWorkspaceId workspace, String8 key, CFG_Node *view,
                              String8 applied, AndamentoResolution *saved)
{
  if(!str8_match(view->string, str8_lit("terminal"), 0) || !applied.size) { return; }
  Temp scratch = scratch_begin(0, 0);
  String8 session = uishell_store_setting(view, str8_lit("session"));
  String8 daemon = uishell_store_setting(view, str8_lit("daemon_name"));
  String8 portable = uishell_store_portable_text(scratch.arena, session, daemon);
  B32 ours = saved && uishell_store_resolution_ours(saved);
  B32 same = ours && str8_match(uishell_store_resolution_field(saved, str8_lit("session")), session, 0) &&
             str8_match(uishell_store_resolution_field(saved, str8_lit("daemon")), daemon, 0);
  char *error = 0;
  if(same || (saved && !ours))
  {
    // Saved already, or another machine's.
  }
  else if(!session.size)
  {
    if(saved)
    {
      uint32_t result = UIShell_StoreCall(state, andamento_slot_resolution_clear(state->core, workspace, uishell_sidebar_text(key),
                                                                                saved->generation, 0, &error));
      uishell_sidebar_result(state, result != ANDAMENTO_RESOLUTION_INVALID, error);
      error = 0;
    }
  }
  else
  {
    AndamentoResolutionField fields[3] = {0};
    U64 count = 0;
    if(daemon.size) { fields[count++] = (AndamentoResolutionField){uishell_sidebar_text(str8_lit("daemon")), uishell_sidebar_text(daemon)}; }
    fields[count++] = (AndamentoResolutionField){uishell_sidebar_text(str8_lit("host")), uishell_sidebar_text(uishell_store_host())};
    fields[count++] = (AndamentoResolutionField){uishell_sidebar_text(str8_lit("session")), uishell_sidebar_text(session)};
    uint32_t result = UIShell_StoreCall(state, andamento_slot_resolution_set(state->core, workspace, uishell_sidebar_text(key),
      uishell_sidebar_text(str8_lit("cleat-session")), fields, count, uishell_sidebar_text(applied),
      saved ? saved->generation : 0, 0, &error));
    // Stale: another save came first; the next plan carries it.
    same = result == ANDAMENTO_RESOLUTION_COMMITTED;
    uishell_sidebar_result(state, result != ANDAMENTO_RESOLUTION_INVALID, error);
    error = 0;
  }
  if(same) { uishell_store_set_setting(view, str8_lit("portable"), portable); }
  else { uishell_store_drop_setting(view, str8_lit("portable")); }
  scratch_end(scratch);
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
    // Previous instances, which go back beside their Slots' Views.
    CFG_NodePtrList previous = {0};
    U64 n = 0;
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p), n++)
    {
      old_ids[n] = p->id; old_cfgs[n] = p->cfg;
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        CFG_Node *view = cfg_node_from_id(t->view);
        if(uishell_store_previous_of(view).size) { cfg_node_ptr_list_push(arena, &previous, view); }
      }
    }
    UIShell_StorePanel *root = uishell_store_panels(arena, stored, info.panel_count);
    AndamentoTab *tabs = push_array(arena, AndamentoTab, info.tab_count);
    for(U64 i = 0; i < info.tab_count; i++) { andamento_arrangement_tab(stored, i, &tabs[i]); }

    // Andamento's IDs are this device's numbers once it has committed; a
    // provider's hint names panels in words, which get numbers here, and
    // keep their words (`doc_id`) so a commit names them as the provider
    // does. The number a word had is this device's: on its panel's node, or
    // this device's settings for it.
    U64 max_id = 0;
    String8 *old_words = push_array(arena, String8, old_count);
    for(U64 i = 0; i < old_count; i++)
    {
      max_id = Max(max_id, old_ids[i]);
      old_words[i] = uishell_store_setting(cfg_node_from_id(old_cfgs[i]), str8_lit("doc_id"));
    }
    for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next)
    {
      String8 number = uishell_store_setting(c, str8_lit("id"));
      if(str8_match(c->string, str8_lit("panel"), 0) && str8_is_integer(number, 10) && number.size < 18) { max_id = Max(max_id, u64_from_str8(number, 10)); }
    }
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
    String8 *words = push_array(arena, String8, info.panel_count+1);
    for(struct Pending *item = queue; item; item = item->next)
    {
      UIShell_StorePanel *src = item->panel;
      RD_PanelID panel_id = item->parent ? rd_arrangement_add(a, item->parent, (F32)src->weight) : a->root->id;
      RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(a, panel_id);
      B32 word = !(str8_is_integer(src->id, 10) && src->id.size < 18);
      RD_PanelID wanted = word ? 0 : u64_from_str8(src->id, 10);
      for(U64 i = 0; i < old_count && word && !wanted; i++) { if(str8_match(old_words[i], src->id, 0)) { wanted = old_ids[i]; } }
      for(CFG_Node *c = device->first; c != &cfg_nil_node && word && !wanted; c = c->next)
      {
        String8 number = uishell_store_setting(c, str8_lit("id"));
        if(str8_match(c->string, str8_lit("panel"), 0) && str8_match(uishell_store_setting(c, str8_lit("doc_id")), src->id, 0) &&
           str8_is_integer(number, 10) && number.size < 18) { wanted = u64_from_str8(number, 10); }
      }
      for(U64 i = 0; i < ids_used && wanted; i++) { if(ids[i] == wanted) { wanted = 0; } }
      if(!wanted) { wanted = fresh++; }
      panel->id = wanted;
      words[ids_used] = word ? src->id : str8_zero();
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
          // A portable resolution Andamento saved, tried before the View
          // starts anything of its own.
          if(known)
          {
            AndamentoSlotResolution *saved = UIShell_StoreCall(state, andamento_slot_resolution_acquire(state->core, workspace, tabs[t].slot, 0));
            AndamentoResolution resolution = {0};
            if(saved && andamento_slot_resolution_get(saved, &resolution)) { uishell_store_restore_resolution(view, &resolution); }
            andamento_slot_resolution_release(saved);
          }
        }
        // Whether it follows its provider, as committing would mark it.
        AndamentoSlot placed = {0};
        if(!tabs[t].gone && uishell_store_slot_find(slots, key, &placed) && !str8_match(view->string, str8_lit("placeholder"), 0))
        { uishell_store_mark_follows(view, (placed.in_baseline && !placed.detached) || placed.spec.content == ANDAMENTO_SLOT_FACET); }
        rd_arrangement_insert_tab(a, view->id, wanted, prev, 0);
        if(t-src->first_tab == src->selected) { selected = view->id; }
        prev = view->id;
        for(CFG_NodePtrNode *p = previous.first; p; p = p->next)
        {
          if(!str8_match(uishell_store_previous_of(p->v), key, 0)) { continue; }
          rd_arrangement_insert_tab(a, p->v->id, wanted, prev, 0);
          prev = p->v->id;
        }
      }
      rd_arrangement_select(a, wanted, selected);
    }
    a->next_id = fresh;
    for(U64 i = 0; i < ids_used; i++) { a->next_id = Max(a->next_id, ids[i]+1); }
    rd_arrangement_save(rd_state->cfg, a);
    for(U64 i = 0; i < ids_used; i++)
    {
      CFG_Node *node = cfg_node_from_id(rd_arrangement_panel_from_id(a, ids[i])->cfg);
      if(node == &cfg_nil_node) { continue; }
      if(words[i].size) { uishell_store_set_setting(node, str8_lit("doc_id"), words[i]); }
      else { uishell_store_drop_setting(node, str8_lit("doc_id")); }
    }
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

//- Slot plans
//
// Every Slot follows Andamento's plan/token protocol (andamento.h, "each
// slot follows managed primary content's protocol"). After each sync, each
// of a workspace's Views that is a Slot is planned with the resolution
// identity it applied (`applied`: empty for none, as after a restart, so a
// fresh process starts from what Andamento has saved), and:
//
//   CURRENT       nothing changes; its daemon session is saved if it has one
//   UPDATING      the recipe is applied to the View, which may show it
//                 already (a user's own slot, a restart), then completed:
//     replace     a terminal swaps its session in place (prepared before
//                 the token is checked); any other change makes a new View
//                 node in the old one's place
//     keep-previous  the old View stays beside the new one as the Slot's
//                 previous instance (`previous_of`), not a Slot itself,
//                 until the user closes it, which releases it
//                 (andamento_slot_release_previous)
//     ask         nothing until the user answers the View's prompt (`asks`);
//                 declining fails the update until the resolution changes
//   HELD, UNAVAILABLE, FAILED   the View keeps what it shows; focusing the
//                 workspace retries a failed update (uishell_store_retry)
//
// A Cleat daemon's session is never replaced in place: whatever the policy,
// it stays as the previous instance.

// What an update would show, for its prompt.
internal String8
uishell_store_recipe_text(UIShell_ViewSpec *recipe)
{
  switch(recipe->content)
  {
    case ANDAMENTO_SLOT_COMMAND: return recipe->command.size ? recipe->command : str8_lit("a shell");
    case ANDAMENTO_SLOT_FILE: return recipe->path;
    case ANDAMENTO_SLOT_URL: return recipe->url;
    case ANDAMENTO_SLOT_JACKSTAY: return recipe->endpoint;
  }
  return str8_lit("new content");
}

// The View kind a recipe shows as, and what of it Wheelhouse can't show.
internal String8
uishell_store_recipe_kind(Arena *arena, String8 key, UIShell_ViewSpec *recipe, String8 *unshown)
{
  *unshown = recipe->content == ANDAMENTO_SLOT_FACET ? str8_lit("content its provider hasn't resolved") :
    uishell_store_unshown(arena, key, recipe);
  if(unshown->size) { return wh_renderer_from_content(WH_Content_Unshown)->name; }
  if(recipe->content == ANDAMENTO_SLOT_URL) { return uishell_store_page_kind(recipe->url); }
  return wh_renderer_from_content(1<<recipe->content)->name;
}

// Whether `view` shows `recipe` already.
internal B32
uishell_store_view_shows(Arena *arena, CFG_Node *view, String8 key, UIShell_ViewSpec *recipe)
{
  String8 unshown = str8_zero();
  String8 kind = uishell_store_recipe_kind(arena, key, recipe, &unshown);
  if(!str8_match(view->string, kind, 0)) { return 0; }
  if(unshown.size) { return str8_match(uishell_store_setting(view, str8_lit("content")), unshown, 0); }
  UIShell_ViewSpec current = {0};
  String8 taken[4];
  U64 taken_count = 0;
  if(!uishell_store_spec_from_view(arena, view, &current, taken, &taken_count)) { return 0; }
  current.presentation = str8_zero();
  UIShell_ViewSpec wanted = *recipe;
  wanted.presentation = str8_zero();
  return uishell_store_spec_match(&current, &wanted);
}

// The tab before `view` in its panel (0: it is first).
internal CFG_ID
uishell_store_tab_before(RD_Arrangement *a, CFG_ID view)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_view(a, view);
  CFG_ID prev = 0;
  for(RD_ArrangementTab *t = panel->first_tab; t && t->view != view; t = t->next) { prev = t->view; }
  return prev;
}

// Applies an Updating plan's recipe to `view`, the View of Slot `key` in
// `owner`, if its token is still valid. Returns the View that shows it now (a
// new node when the old one couldn't take it in place), or nil when the
// update failed or its token went stale.
internal CFG_Node *
uishell_store_apply(UIShell_SidebarState *state, CFG_Node *owner, CFG_Node *view, String8 key,
                    AndamentoSlotContent *content, B32 keep_previous)
{
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  AndamentoWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  UIShell_ViewSpec recipe = uishell_store_spec_from_andamento(arena, &content->recipe);
  String8 unshown = str8_zero();
  String8 kind = uishell_store_recipe_kind(arena, key, &recipe, &unshown);
  B32 shows = uishell_store_view_shows(arena, view, key, &recipe);
  B32 terminal = str8_match(view->string, str8_lit("terminal"), 0);
  if(!shows && terminal && uishell_managed_daemon_backed(view)) { keep_previous = 1; }
  if(str8_match(view->string, str8_lit("placeholder"), 0)) { keep_previous = 0; }
  B32 in_place = !shows && terminal && str8_match(kind, view->string, 0) && !keep_previous;
  UIShell_ManagedPrepared prepared = {0};
  B32 ok = !in_place || uishell_managed_prepare(rd_view_state_from_cfg(view)->user_data, recipe.command, recipe.has_cwd, recipe.cwd, &prepared);
  char *error = 0;
  ok = ok && UIShell_StoreCall(state, andamento_slot_valid(state->core, workspace, uishell_sidebar_text(key), content->token, &error));
  andamento_string_free(error);
  CFG_Node *result = &cfg_nil_node;
  if(!ok) { uishell_managed_discard(&prepared); }
  else if(shows) { result = view; }
  else if(in_place)
  {
    uishell_managed_install(view, &prepared, recipe.command, recipe.has_cwd, recipe.cwd, content->has_target, uishell_sidebar_string(content->target));
    result = view;
  }
  else
  {
    CFG_Node *next = uishell_store_view_from_spec(key, &recipe);
    // What the old View carried that is this device's: its label, that it
    // follows its provider, and the daemon hosting a terminal.
    String8 carried[] = {str8_lit("label"), str8_lit("resource_id"), str8_lit("follows_provider"), str8_lit("daemon"), str8_lit("daemon_name")};
    U64 carried_count = str8_match(next->string, str8_lit("terminal"), 0) ? ArrayCount(carried) : 3;
    for(U64 i = 0; i < carried_count; i++)
    {
      CFG_Node *c = cfg_node_child_from_string(view, carried[i]);
      if(c == &cfg_nil_node || cfg_node_child_from_string(next, carried[i]) != &cfg_nil_node) { continue; }
      cfg_node_insert_child(rd_state->cfg, next, next->last, cfg_node_deep_copy(rd_state->cfg, c));
    }
    if(content->has_target) { uishell_store_set_setting(next, str8_lit("managed_target"), uishell_sidebar_string(content->target)); }
    RD_Arrangement *a = rd_arrangement_from_owner(arena, owner, str8_lit("panels"));
    RD_ArrangementPanel *panel = rd_arrangement_panel_from_view(a, view->id);
    rd_arrangement_insert_tab(a, next->id, panel->id, uishell_store_tab_before(a, view->id), panel->selected == view->id);
    if(keep_previous)
    {
      uishell_store_drop_setting(view, str8_lit("slot"));
      uishell_store_set_setting(view, str8_lit("previous_of"), key);
      for(U64 i = 0; i < ArrayCount(uishell_store_runtime_keys); i++)
      { uishell_store_drop_setting(view, uishell_store_runtime_keys[i]); }
    }
    else { rd_arrangement_remove_tab(a, view->id); }
    rd_arrangement_save(rd_state->cfg, a);
    result = next;
  }
  scratch_end(scratch);
  return result;
}

// Completes an update `apply` made (`shown`, or nil when it failed) and
// records it on the View. `saved` is the portable resolution the plan
// carried, tried first by a View starting without an instance.
internal void
uishell_store_complete(UIShell_SidebarState *state, CFG_Node *owner, String8 key, CFG_Node *view, CFG_Node *shown,
                       AndamentoSlotContent *content, AndamentoResolution *saved)
{
  AndamentoWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  B32 ok = shown != &cfg_nil_node;
  if(ok && saved) { uishell_store_restore_resolution(shown, saved); }
  char *error = 0;
  UIShell_StoreCall(state, andamento_slot_complete(state->core, workspace, uishell_sidebar_text(key), content->token, ok, &error));
  andamento_string_free(error);
  String8 cleared[] = {str8_lit("asks"), str8_lit("declined"), str8_lit("update_failed")};
  if(ok)
  {
    uishell_store_set_setting(shown, str8_lit("applied"), uishell_sidebar_string(content->resolution));
    Temp scratch = scratch_begin(0, 0);
    uishell_store_set_setting(shown, str8_lit("applied_spec"), push_str8f(scratch.arena, "%I64x", uishell_store_view_spec_hash(shown)));
    scratch_end(scratch);
    for(U64 i = 0; i < ArrayCount(cleared); i++) { uishell_store_drop_setting(shown, cleared[i]); }
    if(uishell_workspace_id_match(state->managed_error_workspace, uishell_workspace_id_from_cfg(owner)))
    { MemoryZeroStruct(&state->managed_error_workspace); state->error[0] = 0; }
  }
  else if(cfg_node_from_id(view->id) == view)
  {
    uishell_store_set_setting(view, str8_lit("update_failed"), str8_lit("1"));
    state->managed_error_workspace = uishell_workspace_id_from_cfg(owner);
    uishell_sidebar_set_error(state, str8_lit("A View's update failed; select its workspace to retry"));
  }
}

// Each of `owner`'s Views that is a Slot is planned, and what Andamento has
// for it applied.
internal void
uishell_store_plan_owner(UIShell_SidebarState *state, CFG_Node *owner)
{
  if(!state->core || cfg_node_child_from_string(owner, str8_lit("arrangement_presentation")) != &cfg_nil_node) { return; }
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  AndamentoWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  UIShell_StoreDoc doc = uishell_store_doc(arena, owner);
  String8 *previous = push_array(arena, String8, 1);
  U64 previous_count = 0, previous_cap = 1;
  for(RD_ArrangementPanel *p = doc.arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(doc.arrangement->root, p))
  {
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
    {
      String8 of = uishell_store_previous_of(cfg_node_from_id(t->view));
      if(!of.size) { continue; }
      if(previous_count == previous_cap)
      {
        String8 *grown = push_array(arena, String8, previous_cap*2);
        MemoryCopy(grown, previous, sizeof(*previous)*previous_count);
        previous = grown;
        previous_cap *= 2;
      }
      previous[previous_count++] = push_str8_copy(arena, of);
    }
  }
  for(U64 i = 0; i < doc.tab_count; i++)
  {
    CFG_Node *view = doc.views[i];
    String8 key = push_str8_copy(arena, doc.keys[i]);
    if(!uishell_store_key_valid(key) || cfg_node_from_id(view->id) != view) { continue; }
    String8 applied = push_str8_copy(arena, uishell_store_setting(view, str8_lit("applied")));
    char *error = 0;
    AndamentoSlotPlan *plan = UIShell_StoreCall(state, andamento_slot_plan(state->core, workspace, uishell_sidebar_text(key),
                                                                           uishell_sidebar_text(applied), &error));
    andamento_string_free(error);
    AndamentoSlotContent content = {0};
    if(!plan || !andamento_slot_plan_get(plan, &content)) { andamento_slot_plan_release(plan); continue; }
    AndamentoResolution saved_value = {0};
    AndamentoResolution *saved = andamento_slot_plan_resolution(plan, &saved_value) ? &saved_value : 0;
    String8 asks = str8_zero();
    if(content.state == ANDAMENTO_CONTENT_UPDATING)
    {
      // A rebind's policy is about the instance it replaces: a View that
      // applied nothing yet (just made, or after a restart) has none.
      UIShell_ViewSpec recipe = uishell_store_spec_from_andamento(arena, &content.recipe);
      if(applied.size && content.rebind == ANDAMENTO_REBIND_ASK && !uishell_store_view_shows(arena, view, key, &recipe))
      { asks = uishell_store_recipe_text(&recipe); }
      else
      {
        CFG_Node *shown = uishell_store_apply(state, owner, view, key, &content, applied.size && content.rebind == ANDAMENTO_REBIND_KEEP_PREVIOUS);
        // Only a View starting without an instance tries a saved resolution.
        uishell_store_complete(state, owner, key, view, shown, &content, applied.size ? 0 : saved);
        if(shown != &cfg_nil_node) { view = shown; }
      }
    }
    else if(content.state == ANDAMENTO_CONTENT_CURRENT)
    {
      uishell_store_save_resolution(state, workspace, key, view, applied, saved);
    }
    if(asks.size) { uishell_store_set_setting(view, str8_lit("asks"), asks); }
    else { uishell_store_drop_setting(view, str8_lit("asks")); }
    // A previous instance the user closed, or none kept (a restart, content
    // that couldn't keep one), is released.
    if(content.has_previous && !uishell_store_key_in(key, previous, previous_count))
    {
      error = 0;
      UIShell_StoreCall(state, andamento_slot_release_previous(state->core, workspace, uishell_sidebar_text(key), &error));
      andamento_string_free(error);
    }
    andamento_slot_plan_release(plan);
  }
  // A slot its provider removed: its instance closes (replace) or stays
  // until the user closes it (keep-previous, ask); either way, once it is
  // closed it is released.
  UIShell_StoreEntry *entry = uishell_store_entry_find(state, uishell_workspace_id_from_cfg(owner));
  for(U64 i = 0; entry && i < entry->departed_count;)
  {
    String8 key = entry->departed[i].key;
    CFG_Node *view = &cfg_nil_node;
    for(U64 t = 0; t < doc.tab_count && view == &cfg_nil_node; t++) { if(str8_match(doc.keys[t], key, 0)) { view = doc.views[t]; } }
    if(view != &cfg_nil_node && entry->departed[i].flags != ANDAMENTO_REBIND_REPLACE) { i++; continue; }
    if(view != &cfg_nil_node && cfg_node_from_id(view->id) == view)
    {
      RD_Arrangement *a = rd_arrangement_from_owner(arena, owner, str8_lit("panels"));
      rd_arrangement_remove_tab(a, view->id);
      rd_arrangement_save(rd_state->cfg, a);
    }
    char *error = 0;
    UIShell_StoreCall(state, andamento_slot_release_previous(state->core, workspace, uishell_sidebar_text(key), &error));
    andamento_string_free(error);
    // Released: no longer departed.
    entry->departed[i] = entry->departed[--entry->departed_count];
  }
  scratch_end(scratch);
}

// Whether Slot `key` of `owner` is one its provider removed, kept until the
// user closes it.
internal B32
uishell_store_departed(UIShell_SidebarState *state, CFG_Node *owner, String8 key)
{
  UIShell_StoreEntry *entry = state ? uishell_store_entry_find(state, uishell_workspace_id_from_cfg(owner)) : 0;
  for(U64 i = 0; entry && i < entry->departed_count; i++) { if(str8_match(entry->departed[i].key, key, 0)) { return 1; } }
  return 0;
}

// The window state whose sidebar keeps `view`'s workspace, and that
// workspace; nil when it is in none.
internal UIShell_SidebarState *
uishell_store_state_from_view(CFG_Node *view, CFG_Node **owner_out)
{
  CFG_Node *owner = uishell_store_view_owner(view);
  *owner_out = owner;
  if(owner == &cfg_nil_node) { return 0; }
  RD_WindowState *ws = rd_window_state_from_cfg__existing(rd_window_from_cfg(owner));
  return ws != &rd_nil_window_state && ws->sidebar && ws->sidebar->core ? ws->sidebar : 0;
}

// The user's answer to `view`'s prompt for an `ask` update: yes applies it,
// replacing what the View shows; no declines it, which fails the update
// until the Slot's resolution changes again.
internal void
uishell_store_answer(CFG_Node *view, B32 accept)
{
  CFG_Node *owner = &cfg_nil_node;
  UIShell_SidebarState *state = uishell_store_state_from_view(view, &owner);
  String8 key = uishell_store_setting(view, str8_lit("slot"));
  if(!state || !key.size) { return; }
  Temp scratch = scratch_begin(0, 0);
  key = push_str8_copy(scratch.arena, key);
  AndamentoWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  AndamentoSlotPlan *plan = UIShell_StoreCall(state, andamento_slot_plan(state->core, workspace, uishell_sidebar_text(key),
    uishell_sidebar_text(push_str8_copy(scratch.arena, uishell_store_setting(view, str8_lit("applied")))), 0));
  AndamentoSlotContent content = {0};
  if(plan && andamento_slot_plan_get(plan, &content) && content.state == ANDAMENTO_CONTENT_UPDATING)
  {
    if(accept)
    {
      CFG_Node *shown = uishell_store_apply(state, owner, view, key, &content, 0);
      uishell_store_complete(state, owner, key, view, shown, &content, 0);
    }
    else
    {
      UIShell_StoreCall(state, andamento_slot_complete(state->core, workspace, uishell_sidebar_text(key), content.token, 0, 0));
      uishell_store_set_setting(view, str8_lit("declined"), str8_lit("1"));
      uishell_store_drop_setting(view, str8_lit("asks"));
    }
  }
  andamento_slot_plan_release(plan);
  rd_request_frame();
  scratch_end(scratch);
}

// Closes the previous instance a Slot keeps, `view` itself or the one kept
// beside `view`, and releases it.
internal void
uishell_store_release_previous(CFG_Node *view)
{
  CFG_Node *owner = &cfg_nil_node;
  UIShell_SidebarState *state = uishell_store_state_from_view(view, &owner);
  String8 key = uishell_store_previous_of(view);
  if(!key.size) { key = uishell_store_setting(view, str8_lit("slot")); }
  if(!state || !key.size) { return; }
  Temp scratch = scratch_begin(0, 0);
  key = push_str8_copy(scratch.arena, key);
  RD_Arrangement *a = rd_arrangement_from_owner(scratch.arena, owner, str8_lit("panels"));
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    for(RD_ArrangementTab *t = p->first_tab, *next = 0; t; t = next)
    {
      next = t->next;
      if(!str8_match(uishell_store_previous_of(cfg_node_from_id(t->view)), key, 0)) { continue; }
      if(p->selected == t->view) { rd_arrangement_select(a, p->id, t->prev ? t->prev->view : next ? next->view : 0); }
      rd_arrangement_remove_tab(a, t->view);
    }
  }
  rd_arrangement_save(rd_state->cfg, a);
  UIShell_StoreCall(state, andamento_slot_release_previous(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner)),
                                                           uishell_sidebar_text(key), 0));
  rd_request_frame();
  scratch_end(scratch);
}

// Focusing a workspace retries its Views' failed updates, but not ones the
// user declined.
internal void
uishell_store_retry(UIShell_SidebarState *state, CFG_Node *owner)
{
  Temp scratch = scratch_begin(0, 0);
  AndamentoWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  UIShell_StoreDoc doc = uishell_store_doc(scratch.arena, owner);
  for(U64 i = 0; i < doc.tab_count; i++)
  {
    CFG_Node *failed = cfg_node_child_from_string(doc.views[i], str8_lit("update_failed"));
    if(failed == &cfg_nil_node) { continue; }
    UIShell_StoreCall(state, andamento_slot_retry(state->core, workspace, uishell_sidebar_text(doc.keys[i]), 0));
    cfg_node_release(rd_state->cfg, failed);
  }
  scratch_end(scratch);
}

//- The Workspace Overlay (ADR 0013)
//
// The user's edits of a workspace are Andamento's edit set: its Slots
// (above), its arrangement (soft and structural commits), and its name and
// mood, which are the workspace's label and theme here. A workspace marks
// the name and mood Andamento has (`named`, `mooded`; runtime-only), so the
// presentation file leaves them out. After each sync the overlay's flags are
// read for the notices: each Slot's (DETACHED, CHANGED, REMOVED, REBIND),
// the arrangement's (PROVIDER_CHANGED, SOFT, UNRESOLVED), the slots
// tombstones hide, and Dashboard pins whose View went away.

read_only global String8 uishell_store_owner_runtime_keys[] = {str8_lit_comp("named"), str8_lit_comp("mooded")};

// Sends a label or theme the user changed as the workspace's name or mood,
// or takes Andamento's when only Andamento's changed (another host's edit,
// or a restart, which reads them from the workspace record).
internal void
uishell_store_sync_name(UIShell_SidebarState *state, CFG_Node *owner)
{
  if(str8_match(owner->string, str8_lit("window"), 0)) { return; }
  AndamentoWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  if(!andamento_workspace_registered(state->core, workspace, 0)) { return; }
  Temp scratch = scratch_begin(0, 0);
  AndamentoOverlay *overlay = UIShell_StoreCall(state, andamento_overlay_acquire(state->core, workspace, 0));
  AndamentoOverlayInfo info = {0};
  if(overlay && andamento_overlay_info(overlay, &info))
  {
    String8 settings[2] = {str8_lit("label"), str8_lit("theme")};
    B32 has_remote[2] = {info.has_name != 0, info.has_mood != 0};
    String8 remote[2] = {uishell_sidebar_string(info.name), uishell_sidebar_string(info.mood)};
    for(U64 i = 0; i < 2; i++)
    {
      String8 mark_key = uishell_store_owner_runtime_keys[i];
      CFG_Node *mark = cfg_node_child_from_string(owner, mark_key);
      B32 has_mark = mark != &cfg_nil_node;
      String8 marked = push_str8_copy(scratch.arena, mark->first->string);
      String8 local = push_str8_copy(scratch.arena, uishell_store_setting(owner, settings[i]));
      B32 edited = has_mark ? !str8_match(local, marked, 0) : local.size != 0;
      B32 moved = has_remote[i] != has_mark || !str8_match(remote[i], marked, 0);
      if(!edited && moved && has_remote[i])
      {
        // Andamento's changed: it is the workspace's now.
        uishell_store_set_setting(owner, settings[i], remote[i]);
        uishell_store_set_setting(owner, mark_key, remote[i]);
      }
      else if(edited || (moved && local.size))
      {
        char *error = 0;
        B32 ok = i == 0 ? andamento_workspace_set_name(state->core, workspace, local.size != 0, uishell_sidebar_text(local), &error) :
                          andamento_workspace_set_mood(state->core, workspace, local.size != 0, uishell_sidebar_text(local), &error);
        state->store_calls += 1;
        if(uishell_sidebar_result(state, ok, error))
        {
          if(local.size) { uishell_store_set_setting(owner, mark_key, local); }
          else { uishell_store_drop_setting(owner, mark_key); }
        }
      }
    }
  }
  andamento_overlay_release(overlay);
  scratch_end(scratch);
}

internal UIShell_StoreEntry *
uishell_store_entry_find(UIShell_SidebarState *state, UIShell_WorkspaceId id)
{
  for(U64 i = 0; i < state->store_entry_count; i++)
  { if(uishell_workspace_id_match(state->store_entries[i].id, id)) { return &state->store_entries[i]; } }
  return 0;
}

internal UIShell_StoreEntry *uishell_store_entry(UIShell_SidebarState *state, UIShell_WorkspaceId id);
internal CFG_NodePtrList uishell_store_owners(Arena *arena, CFG_Node *window);

// Reads what Andamento says of each workspace's overlay, and of the
// Dashboard's pins, for the notices.
internal void
uishell_store_read_overlay(UIShell_SidebarState *state, CFG_Node *window)
{
  if(!state->store_overlay_arena) { state->store_overlay_arena = arena_alloc(); }
  Arena *arena = state->store_overlay_arena;
  arena_clear(arena);
  Temp scratch = scratch_begin(0, 0);
  CFG_NodePtrList owners = uishell_store_owners(scratch.arena, window);
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next)
  {
    UIShell_StoreEntry *entry = uishell_store_entry(state, uishell_workspace_id_from_cfg(n->v));
    entry->flags = 0; entry->flag_count = 0; entry->tombstones = 0; entry->tombstone_count = 0; entry->arrangement_flags = 0;
    AndamentoWorkspaceId workspace = uishell_sidebar_workspace(entry->id);
    AndamentoSlots *slots = UIShell_StoreCall(state, andamento_slots_acquire(state->core, workspace, 0));
    U64 count = slots ? andamento_slots_count(slots) : 0;
    entry->flags = push_array(arena, UIShell_StoreFlag, count);
    for(U64 i = 0; i < count; i++)
    {
      AndamentoSlot slot = {0};
      U32 flags = andamento_slots_flags(slots, i);
      if(!flags || !andamento_slots_get(slots, i, &slot)) { continue; }
      entry->flags[entry->flag_count++] = (UIShell_StoreFlag){push_str8_copy(arena, uishell_sidebar_string(slot.key)), flags};
    }
    andamento_slots_release(slots);
    AndamentoArrangement *arrangement = slots ? UIShell_StoreCall(state, andamento_arrangement_acquire(state->core, workspace, 0)) : 0;
    entry->arrangement_flags = andamento_arrangement_flags(arrangement);
    andamento_arrangement_release(arrangement);
    AndamentoOverlay *overlay = slots ? UIShell_StoreCall(state, andamento_overlay_acquire(state->core, workspace, 0)) : 0;
    AndamentoOverlayInfo info = {0};
    entry->departed = 0;
    entry->departed_count = 0;
    if(overlay && andamento_overlay_info(overlay, &info))
    {
      entry->tombstones = push_array(arena, String8, info.note_count);
      entry->departed = push_array(arena, UIShell_StoreFlag, info.note_count);
      for(U64 i = 0; i < info.note_count; i++)
      {
        AndamentoOverlayNote note = {0};
        if(!andamento_overlay_note(overlay, i, &note)) { continue; }
        String8 key = push_str8_copy(arena, uishell_sidebar_string(note.key));
        if(note.kind == ANDAMENTO_OVERLAY_TOMBSTONED) { entry->tombstones[entry->tombstone_count++] = key; }
        if(note.kind == ANDAMENTO_OVERLAY_DEPARTED) { entry->departed[entry->departed_count++] = (UIShell_StoreFlag){key, note.rebind}; }
      }
    }
    andamento_overlay_release(overlay);
  }
  state->store_pins_gone = 0;
  state->store_pin_gone_count = 0;
  AndamentoDashboardOverlay *dashboard = UIShell_StoreCall(state, andamento_dashboard_overlay_acquire(state->core, 0));
  AndamentoDashboardOverlayInfo info = {0};
  if(dashboard && andamento_dashboard_overlay_info(dashboard, &info))
  {
    state->store_pins_gone = push_array(arena, String8, info.note_count);
    for(U64 i = 0; i < info.note_count; i++)
    {
      AndamentoDashboardNote note = {0};
      if(andamento_dashboard_overlay_note(dashboard, i, &note) && note.kind == ANDAMENTO_DASHBOARD_PIN)
      { state->store_pins_gone[state->store_pin_gone_count++] = push_str8_copy(arena, uishell_sidebar_string(note.key)); }
    }
  }
  andamento_dashboard_overlay_release(dashboard);
  scratch_end(scratch);
}

// Slot `key`'s flags in `owner`, as last read; 0 for none.
internal U32
uishell_store_slot_flags(UIShell_SidebarState *state, CFG_Node *owner, String8 key)
{
  UIShell_StoreEntry *entry = state ? uishell_store_entry_find(state, uishell_workspace_id_from_cfg(owner)) : 0;
  for(U64 i = 0; entry && i < entry->flag_count; i++) { if(str8_match(entry->flags[i].key, key, 0)) { return entry->flags[i].flags; } }
  return 0;
}

internal U32
uishell_store_arrangement_flags(UIShell_SidebarState *state, CFG_Node *owner)
{
  UIShell_StoreEntry *entry = state ? uishell_store_entry_find(state, uishell_workspace_id_from_cfg(owner)) : 0;
  return entry ? entry->arrangement_flags : 0;
}

// Whether the pin (a local ref) with ID `ref` names a View that went away.
internal B32
uishell_store_pin_gone(UIShell_SidebarState *state, String8 ref)
{
  for(U64 i = 0; state && i < state->store_pin_gone_count; i++) { if(str8_match(state->store_pins_gone[i], ref, 0)) { return 1; } }
  return 0;
}

// Whether `view` is a View of `owner`'s first tab panel, which shows the
// workspace's own notices.
internal B32
uishell_store_view_leads(CFG_Node *owner, CFG_Node *view)
{
  Temp scratch = scratch_begin(0, 0);
  RD_Arrangement *a = rd_arrangement_from_owner(scratch.arena, owner, str8_lit("panels"));
  RD_ArrangementPanel *first = a->root;
  while(first != &rd_nil_arrangement_panel && first->first != &rd_nil_arrangement_panel) { first = first->first; }
  B32 leads = first != &rd_nil_arrangement_panel && rd_arrangement_panel_from_view(a, view->id) == first;
  scratch_end(scratch);
  return leads;
}

// Resolves a provider's change to an arrangement the user owns: keep the
// user's, or follow the provider's again.
internal void
uishell_store_resolve_arrangement(CFG_Node *view, U32 choice)
{
  CFG_Node *owner = &cfg_nil_node;
  UIShell_SidebarState *state = uishell_store_state_from_view(view, &owner);
  if(!state) { return; }
  UIShell_WorkspaceId id = uishell_workspace_id_from_cfg(owner);
  AndamentoWorkspaceId workspace = uishell_sidebar_workspace(id);
  AndamentoArrangement *stored = UIShell_StoreCall(state, andamento_arrangement_acquire(state->core, workspace, 0));
  AndamentoArrangementInfo info = {0};
  if(stored && andamento_arrangement_info(stored, &info))
  {
    char *error = 0;
    uint32_t result = UIShell_StoreCall(state, andamento_arrangement_resolve(state->core, workspace, choice, info.generation, 0, &error));
    uishell_sidebar_result(state, result != ANDAMENTO_ARRANGEMENT_INVALID, error);
  }
  andamento_arrangement_release(stored);
}

// Drops the user's edit of `view`'s Slot: the provider's content, or the
// key it reused, is the Slot's again.
internal void
uishell_store_reattach(CFG_Node *view)
{
  CFG_Node *owner = &cfg_nil_node;
  UIShell_SidebarState *state = uishell_store_state_from_view(view, &owner);
  String8 key = uishell_store_setting(view, str8_lit("slot"));
  if(!state || !key.size) { return; }
  char *error = 0;
  B32 ok = UIShell_StoreCall(state, andamento_slot_reattach(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner)),
                                                             uishell_sidebar_text(key), &error));
  // It follows its provider again; its next plan shows the provider's
  // content. A slot a tombstone hid comes back at the next commit, which
  // places it.
  if(uishell_sidebar_result(state, ok, error))
  {
    uishell_store_mark_follows(view, 1);
    UIShell_StoreEntry *entry = uishell_store_entry_find(state, uishell_workspace_id_from_cfg(owner));
    if(entry) { entry->hash = 0; }
  }
}

// Keeps the user's edit of `view`'s Slot against the provider's new
// content, which settles its CHANGED flag.
internal void
uishell_store_keep(CFG_Node *view)
{
  CFG_Node *owner = &cfg_nil_node;
  UIShell_SidebarState *state = uishell_store_state_from_view(view, &owner);
  String8 key = uishell_store_setting(view, str8_lit("slot"));
  UIShell_ViewSpec spec = {0};
  String8 taken[4];
  U64 taken_count = 0;
  Temp scratch = scratch_begin(0, 0);
  if(state && key.size && uishell_store_spec_from_view(scratch.arena, view, &spec, taken, &taken_count))
  {
    AndamentoWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
    AndamentoSlots *slots = UIShell_StoreCall(state, andamento_slots_acquire(state->core, workspace, 0));
    AndamentoSlot slot = {0};
    U32 rebind = uishell_store_slot_find(slots, key, &slot) ? slot.rebind : ANDAMENTO_REBIND_REPLACE;
    andamento_slots_release(slots);
    AndamentoViewSpec out = uishell_store_andamento_spec(&spec);
    char *error = 0;
    uishell_sidebar_result(state, UIShell_StoreCall(state, andamento_slot_set(state->core, workspace, uishell_sidebar_text(key), &out, rebind, &error)), error);
  }
  scratch_end(scratch);
}

// Closes `view`'s tab: its Slot goes, a provider's as a tombstone.
internal void
uishell_store_remove(CFG_Node *view)
{
  if(view->parent == &cfg_nil_node) { return; }
  UIShell_RegsScope(.window = rd_window_from_cfg(view)->id, .panel = view->parent->id, .view = view->id, .tab = view->id)
  { uishell_cmd("close_tab"); }
}

// Pins `view`, a workspace's View, where Pin puts a card: a pin of its
// workspace's entity that names the View (`view`: "<workspace-id>/<slot>").
// When the View goes, Andamento flags the pin rather than dropping it.
internal CFG_Node *uishell_sidebar_pin_area(RD_WindowState *ws, B32 new_area);
internal CFG_Node *uishell_sidebar_pin_add(UIShell_SidebarState *state, CFG_Node *area, U64 index, AndamentoEntity entity,
                                           String8 fallback_label, String8 source, B32 compact);
internal CFG_Node *
uishell_store_pin_view(RD_WindowState *ws, CFG_Node *view)
{
  CFG_Node *owner = uishell_store_view_owner(view);
  String8 key = uishell_store_setting(view, str8_lit("slot"));
  if(owner == &cfg_nil_node || str8_match(owner->string, str8_lit("window"), 0) || !key.size || !ws->sidebar) { return &cfg_nil_node; }
  CFG_Node *area = uishell_sidebar_pin_area(ws, 0);
  if(area == &cfg_nil_node) { return &cfg_nil_node; }
  Temp scratch = scratch_begin(0, 0);
  String8 id = uishell_workspace_id_text_from_cfg(owner);
  AndamentoEntity entity = {uishell_sidebar_text(str8_lit(".workspace")), uishell_sidebar_text(id)};
  if(uishell_workspace_cfg_has_subject(owner))
  {
    entity.kind = uishell_sidebar_text(uishell_store_setting(owner, str8_lit("sidebar_entity_kind")));
    entity.id = uishell_sidebar_text(uishell_store_setting(owner, str8_lit("sidebar_entity_id")));
  }
  // Named for the View, not only its workspace's entity.
  String8 label = rd_label_from_cfg(view);
  label = push_str8f(scratch.arena, "%S: %S", rd_label_from_cfg(owner), label.size ? label : key);
  CFG_Node *pin = uishell_sidebar_pin_add(ws->sidebar, area, max_U64, entity, label, str8_zero(), 1);
  uishell_store_set_setting(pin, str8_lit("label"), label);
  uishell_store_set_setting(pin, str8_lit("view"), push_str8f(scratch.arena, "%S/%S", id, key));
  scratch_end(scratch);
  return pin;
}

//- Notices
//
// What a View says about its Slot: a word in its tab's title
// (uishell_slot_badge) and a one-line banner above its content
// (uishell_slot_banner) with at most two actions (uishell_store_act). The
// same notices are what the behaviour harness drives.

read_only global String8 uishell_slot_action_labels[UIShell_SlotAction_COUNT] =
{
  str8_lit_comp(""),
  str8_lit_comp("Update"),
  str8_lit_comp("Not now"),
  str8_lit_comp("Release"),
  str8_lit_comp("Retry"),
  str8_lit_comp("Use provider's"),
  str8_lit_comp("Keep mine"),
  str8_lit_comp("Remove"),
  str8_lit_comp("Keep mine"),
  str8_lit_comp("Use provider's"),
};

// Whether a Slot of `owner` keeps a previous instance of `key` beside its View.
internal B32
uishell_store_keeps_previous(CFG_Node *owner, String8 key)
{
  CFG_Node *panels = cfg_node_child_from_string(owner, str8_lit("panels"));
  for(CFG_Node *n = panels; n != &cfg_nil_node; n = cfg_node_rec__depth_first(panels, n).next)
  {
    if(str8_match(n->string, str8_lit("previous_of"), 0) && str8_match(n->first->string, key, 0)) { return 1; }
  }
  return 0;
}

// `view`'s notices, most pressing first; at most `cap` of them.
internal U64
uishell_store_notices(Arena *arena, CFG_Node *view, UIShell_SlotNotice *out, U64 cap)
{
  U64 count = 0;
#define UIShell_SlotNoticePush(...) do { if(count < cap) { out[count++] = (UIShell_SlotNotice){__VA_ARGS__}; } } while(0)
  String8 key = uishell_store_setting(view, str8_lit("slot"));
  String8 previous_of = uishell_store_previous_of(view);
  if(!key.size && !previous_of.size) { return 0; }
  CFG_Node *owner = uishell_store_view_owner(view);
  if(owner == &cfg_nil_node) { return 0; }
  String8 asks = uishell_store_setting(view, str8_lit("asks"));
  if(previous_of.size)
  {
    UIShell_SlotNoticePush(.badge = str8_lit("previous"),
                           .text = str8_lit("This is what this View showed before its content changed."),
                           .actions = {UIShell_SlotAction_ReleasePrevious});
  }
  if(asks.size)
  {
    UIShell_SlotNoticePush(.badge = str8_lit("update available"),
                           .text = push_str8f(arena, "Its provider has new content: %S", asks),
                           .actions = {UIShell_SlotAction_Update, UIShell_SlotAction_Decline});
  }
  if(cfg_node_child_from_string(view, str8_lit("update_failed")) != &cfg_nil_node)
  {
    UIShell_SlotNoticePush(.badge = str8_lit("update failed"), .text = str8_lit("Updating this View's content failed."),
                           .actions = {UIShell_SlotAction_Retry});
  }
  if(key.size && uishell_store_keeps_previous(owner, key))
  {
    UIShell_SlotNoticePush(.text = str8_lit("What it showed before is kept in the tab beside it."),
                           .actions = {UIShell_SlotAction_ReleasePrevious});
  }
  // The Workspace Overlay's flags.
  CFG_Node *state_owner = &cfg_nil_node;
  UIShell_SidebarState *state = uishell_store_state_from_view(view, &state_owner);
  U32 flags = key.size ? uishell_store_slot_flags(state, owner, key) : 0;
  if(flags & ANDAMENTO_SLOT_FLAG_REMOVED)
  {
    UIShell_SlotNoticePush(.badge = str8_lit("removed"), .text = str8_lit("Its provider removed it; it is kept as yours."),
                           .actions = {UIShell_SlotAction_Remove});
  }
  else if((flags & ANDAMENTO_SLOT_FLAG_CHANGED) && (flags & ANDAMENTO_SLOT_FLAG_DETACHED))
  {
    UIShell_SlotNoticePush(.badge = str8_lit("changed"), .text = str8_lit("Its provider changed it since you edited it."),
                           .actions = {UIShell_SlotAction_Reattach, UIShell_SlotAction_KeepMine});
  }
  else if(flags & ANDAMENTO_SLOT_FLAG_CHANGED)
  {
    UIShell_SlotNoticePush(.badge = str8_lit("changed"), .text = str8_lit("You closed it; its provider has put new content here since."),
                           .actions = {UIShell_SlotAction_Reattach, UIShell_SlotAction_Remove});
  }
  else if(flags & ANDAMENTO_SLOT_FLAG_DETACHED)
  {
    UIShell_SlotNoticePush(.badge = str8_lit("edited"), .text = str8_lit("You changed its content, so it no longer follows its provider."),
                           .actions = {UIShell_SlotAction_Reattach});
  }
  if(flags & ANDAMENTO_SLOT_FLAG_REBIND) { UIShell_SlotNoticePush(.badge = str8_lit("rebind")); }
  if(key.size && uishell_store_departed(state, owner, key))
  {
    UIShell_SlotNoticePush(.badge = str8_lit("removed"), .text = str8_lit("Its provider removed this View; it stays until you close it."),
                           .actions = {UIShell_SlotAction_Remove});
  }
  U32 arrangement = uishell_store_arrangement_flags(state, owner);
  if((arrangement & (ANDAMENTO_ARRANGEMENT_FLAG_PROVIDER_CHANGED|ANDAMENTO_ARRANGEMENT_FLAG_UNRESOLVED)) && uishell_store_view_leads(owner, view))
  {
    if(arrangement & ANDAMENTO_ARRANGEMENT_FLAG_PROVIDER_CHANGED)
    {
      UIShell_SlotNoticePush(.text = str8_lit("Its provider changed this workspace's layout since you arranged it."),
                             .actions = {UIShell_SlotAction_KeepLayout, UIShell_SlotAction_FollowLayout});
    }
    else
    {
      UIShell_SlotNoticePush(.text = str8_lit("A size or tab you chose is for a panel its provider no longer has."),
                             .actions = {UIShell_SlotAction_FollowLayout});
    }
  }
#undef UIShell_SlotNoticePush
  return count;
}

internal void
uishell_store_act(CFG_Node *view, UIShell_SlotAction action)
{
  switch(action)
  {
    default: break;
    case UIShell_SlotAction_Update: { uishell_store_answer(view, 1); } break;
    case UIShell_SlotAction_Decline: { uishell_store_answer(view, 0); } break;
    case UIShell_SlotAction_ReleasePrevious: { uishell_store_release_previous(view); } break;
    case UIShell_SlotAction_Reattach: { uishell_store_reattach(view); } break;
    case UIShell_SlotAction_KeepMine: { uishell_store_keep(view); } break;
    case UIShell_SlotAction_Remove: { uishell_store_remove(view); } break;
    case UIShell_SlotAction_KeepLayout: { uishell_store_resolve_arrangement(view, ANDAMENTO_ARRANGEMENT_KEEP); } break;
    case UIShell_SlotAction_FollowLayout: { uishell_store_resolve_arrangement(view, ANDAMENTO_ARRANGEMENT_FOLLOW); } break;
    case UIShell_SlotAction_Retry:
    {
      CFG_Node *owner = &cfg_nil_node;
      UIShell_SidebarState *state = uishell_store_state_from_view(view, &owner);
      if(state) { uishell_store_retry(state, owner); }
    } break;
  }
  rd_request_frame();
}

internal String8
uishell_slot_badge(Arena *arena, CFG_Node *view)
{
  if(cfg_node_child_from_string(view, str8_lit("slot")) == &cfg_nil_node &&
     cfg_node_child_from_string(view, str8_lit("previous_of")) == &cfg_nil_node) { return str8_zero(); }
  UIShell_SlotNotice notices[8];
  U64 count = uishell_store_notices(arena, view, notices, ArrayCount(notices));
  for(U64 i = 0; i < count; i++) { if(notices[i].badge.size) { return notices[i].badge; } }
  return str8_zero();
}

internal Rng2F32
uishell_slot_banner(CFG_Node *view, Rng2F32 rect)
{
  if(cfg_node_child_from_string(view, str8_lit("slot")) == &cfg_nil_node &&
     cfg_node_child_from_string(view, str8_lit("previous_of")) == &cfg_nil_node) { return rect; }
  Temp scratch = scratch_begin(0, 0);
  UIShell_SlotNotice notices[8];
  U64 count = uishell_store_notices(scratch.arena, view, notices, ArrayCount(notices));
  UIShell_SlotNotice *notice = 0;
  for(U64 i = 0; i < count && !notice; i++) { if(notices[i].text.size) { notice = &notices[i]; } }
  if(notice)
  {
    F32 height = floor_f32(ui_top_font_size()*2.2f);
    UI_WidthFill UI_PrefHeight(ui_px(height, 1)) UI_NamedRow(push_str8f(scratch.arena, "slot_banner_%I64x", view->id))
    {
      ui_spacer(ui_em(0.5f, 1));
      UI_PrefWidth(ui_text_dim(8, 0)) UI_TagF("weak") { ui_label(notice->text); }
      ui_spacer(ui_pct(1, 0));
      UI_PrefWidth(ui_text_dim(8, 1))
      {
        for(U64 i = 0; i < ArrayCount(notice->actions); i++)
        {
          UIShell_SlotAction action = notice->actions[i];
          if(action == UIShell_SlotAction_None) { continue; }
          if(ui_clicked(ui_buttonf("%S###slot_action_%I64x_%u", uishell_slot_action_labels[action], view->id, (U32)action)))
          { uishell_store_act(view, action); }
        }
      }
    }
    rect.y0 = Min(rect.y1, rect.y0+height);
  }
  scratch_end(scratch);
  return rect;
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
  uishell_store_sync_name(state, owner);
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
  // A workspace just opened on its subject (`arrangement_fresh`) takes the
  // arrangement its Suggested Layout suggests, keeping the View it opened
  // with; one with no suggestion keeps the panel tree it opened with.
  CFG_Node *fresh = cfg_node_child_from_string(owner, str8_lit("arrangement_fresh"));
  if(fresh != &cfg_nil_node)
  {
    cfg_node_release(rd_state->cfg, fresh);
    if(info.panel_count != 0)
    {
      uishell_store_assign_keys(arena, &doc, slots);
      andamento_arrangement_release(stored);
      andamento_slots_release(slots);
      uishell_store_build(state, owner, id, &entry->generation);
      UIShell_StoreDoc built = uishell_store_doc(arena, owner);
      entry->hash = uishell_store_doc_hash(&built);
      scratch_end(scratch);
      return;
    }
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
    // The provider's own, or a facet's, whose View shows what its plan
    // resolved: the slot's spec is not the View's to set.
    B32 follows = exists && ((slot.in_baseline && !slot.detached) || slot.spec.content == ANDAMENTO_SLOT_FACET);
    // Unless the user changed what it showed since it applied the provider's:
    // that is an override, which detaches it.
    String8 applied_spec = uishell_store_setting(doc.views[i], str8_lit("applied_spec"));
    if(follows && applied_spec.size &&
       !str8_match(applied_spec, push_str8f(arena, "%I64x", uishell_store_view_spec_hash(doc.views[i])), 0)) { follows = 0; }
    uishell_store_mark_follows(doc.views[i], follows);
    if(follows) { continue; }
    UIShell_ViewSpec current = exists ? uishell_store_spec_from_andamento(arena, &slot.spec) : (UIShell_ViewSpec){0};
    if(exists && uishell_store_spec_match(&spec, &current)) { continue; }
    // An override keeps the provider's rebind policy: changing the content
    // isn't changing the policy.
    AndamentoViewSpec out = uishell_store_andamento_spec(&spec);
    char *error = 0;
    ok = uishell_sidebar_result(state, UIShell_StoreCall(state, andamento_slot_set(state->core, workspace, uishell_sidebar_text(doc.keys[i]),
                                                                                    &out, exists ? slot.rebind : ANDAMENTO_REBIND_REPLACE, &error)), error);
  }

  // Slots whose tabs the user closed go first, so the document no longer
  // names them: the user's own are removed, and a provider's is tombstoned,
  // hidden, while its provider keeps it (ADR 0013). A slot the stored
  // document tabs that this one never had (Andamento placed it since) stays.
  for(U64 i = 0; ok && i < info.tab_count; i++)
  {
    AndamentoTab tab = {0};
    if(!andamento_arrangement_tab(stored, i, &tab) || tab.placed || tab.gone) { continue; }
    String8 key = push_str8_copy(arena, uishell_sidebar_string(tab.slot));
    if(uishell_store_key_in(key, doc.keys, doc.tab_count) || !uishell_store_slot_find(slots, key, 0)) { continue; }
    char *remove_error = 0;
    uishell_sidebar_result(state, UIShell_StoreCall(state, andamento_slot_remove(state->core, workspace, uishell_sidebar_text(key), &remove_error)), remove_error);
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

// The window's workspaces' nodes, panel trees included, as one hash: what a
// sync reads. Moving a node (a tab reordered, a panel moved) keeps the
// config generation, so it can't stand in.
internal U64
uishell_store_tree_hash(Arena *arena, CFG_Node *window)
{
  U64 hash = 5381;
  CFG_NodePtrList owners = uishell_store_owners(arena, window);
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next)
  {
    CFG_Node *node = n->v == window ? cfg_node_child_from_string(window, str8_lit("panels")) : n->v;
    hash = uishell_sidebar_hash_text(hash, node->string);
    hash = uishell_sidebar_hash_node(hash, node);
  }
  return hash;
}

// Each frame the sidebar observes its window: commits what changed once no
// gesture is in flight, then plans each workspace's Slots. `force` commits
// even then (the window is closing).
internal void
uishell_workspace_store_sync(UIShell_SidebarState *state, CFG_Node *window, B32 force)
{
  if(!state->core || !state->window || window->id != state->window) { return; }
  if(!force && uishell_store_gesture_in_flight()) { return; }
  if(!state->store_arena) { state->store_arena = arena_alloc(); }
  Temp scratch = scratch_begin(0, 0);
  char *error = 0;
  U64 revision = UIShell_StoreCall(state, andamento_workspace_content_revision(state->core, &error));
  andamento_string_free(error);
  B32 revision_moved = state->store_synced && revision != state->store_content_revision;
  // A new snapshot can mean a workspace Andamento didn't know has registered
  // (a subject's, once it opened), which then commits.
  if(state->store_synced && cfg_change_gen() == state->store_cfg_gen && state->snapshot_count == state->store_snapshot_count &&
     !revision_moved && uishell_store_tree_hash(scratch.arena, window) == state->store_tree_hash)
  {
    scratch_end(scratch);
    return;
  }
  CFG_NodePtrList owners = uishell_store_owners(scratch.arena, window);
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next) { uishell_store_sync_owner(state, n->v, revision_moved); }
  uishell_store_read_overlay(state, window);
  // Kept workspaces aren't materialized: their Slots wait until they open.
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next)
  {
    CFG_Node *owner = n->v;
    if(str8_match(owner->string, str8_lit("detached_workspace"), 0)) { continue; }
    UIShell_StoreEntry *entry = uishell_store_entry(state, uishell_workspace_id_from_cfg(owner));
    UIShell_StoreDoc before = uishell_store_doc(scratch.arena, owner);
    U64 hash = uishell_store_doc_hash(&before);
    uishell_store_plan_owner(state, owner);
    // What an update changed is Andamento's already: no commit follows it.
    if(entry->hash == hash)
    {
      UIShell_StoreDoc after = uishell_store_doc(scratch.arena, owner);
      entry->hash = uishell_store_doc_hash(&after);
    }
  }
  state->store_synced = 1;
  state->store_cfg_gen = cfg_change_gen();
  state->store_tree_hash = uishell_store_tree_hash(scratch.arena, window);
  state->store_snapshot_count = state->snapshot_count;
  state->store_content_revision = UIShell_StoreCall(state, andamento_workspace_content_revision(state->core, 0));
  scratch_end(scratch);
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
  // Names and moods too, which the presentation file leaves to Andamento.
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next) { uishell_store_sync_name(state, n->v); }
  scratch_end(scratch);
}

internal void
uishell_workspace_store_release(UIShell_SidebarState *state)
{
  if(state->store_arena) { arena_release(state->store_arena); }
  if(state->store_overlay_arena) { arena_release(state->store_overlay_arena); }
  state->store_arena = state->store_overlay_arena = 0;
  state->store_pins_gone = 0;
  state->store_pin_gone_count = 0;
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
    // A name or mood Andamento has is its (`named`, `mooded`).
    {
      CFG_Node *dst = copies[owner_index];
      String8 settings[] = {str8_lit("label"), str8_lit("theme")};
      for(U64 i = 0; i < ArrayCount(settings) && owner != window; i++)
      {
        CFG_Node *mark = cfg_node_child_from_string(owner, uishell_store_owner_runtime_keys[i]);
        CFG_Node *setting = cfg_node_child_from_string(dst, settings[i]);
        if(mark != &cfg_nil_node && setting != &cfg_nil_node && str8_match(setting->first->string, mark->first->string, 0))
        { cfg_node_release(copy_state, setting); }
        CFG_Node *copied = cfg_node_child_from_string(dst, uishell_store_owner_runtime_keys[i]);
        if(copied != &cfg_nil_node) { cfg_node_release(copy_state, copied); }
      }
    }
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
        // A daemon session Andamento has saved is its.
        String8 portable = uishell_store_setting(view, str8_lit("portable"));
        B32 saved = portable.size && str8_match(portable, uishell_store_portable_text(scratch.arena, uishell_store_setting(view, str8_lit("session")),
                                                                                     uishell_store_setting(view, str8_lit("daemon_name"))), 0);
        CFG_Node *groups[2] = {&cfg_nil_node, &cfg_nil_node};
        for(CFG_Node *c = view->first; c != &cfg_nil_node; c = c->next)
        {
          if(str8_match(c->string, str8_lit("slot"), 0) || str8_match(c->string, str8_lit("selected"), 0) ||
             uishell_store_key_in(c->string, taken, taken_count) ||
             uishell_store_key_in(c->string, uishell_store_runtime_keys, ArrayCount(uishell_store_runtime_keys)) ||
             (saved && (str8_match(c->string, str8_lit("session"), 0) || str8_match(c->string, str8_lit("daemon_name"), 0)))) { continue; }
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
