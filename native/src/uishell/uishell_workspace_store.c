//- Workspace arrangements and Slots in the core (ADR 0012; wheelhouse.h,
// "Slots and arrangement documents"). The core keeps each workspace's Slots,
// with their View Specs, and its arrangement document; this device keeps the
// rest. Wheelhouse's live copy is the workspace's typed arrangement
// (shell_core.c, "Live arrangements"), whose tabs name its Views by View
// Address. A View's node, in the workspace's `views`, holds its logical
// settings; runtime state (live terminals, Jackstay sessions) is keyed by its
// address. Each View of the arrangement is a Slot, named by its `slot` key;
// panels keep their IDs.
//
//   load    A workspace saved since has no Views in the presentation file,
//           only `arrangement_presentation`. Once the window's core has
//           imported its records, its arrangement follows the core's
//           document and slots (the getters), with this device's settings
//           merged back in (uishell_workspace_store_load).
//   commit  Once its arrangement was set, or its Views' specs changed, and no
//           gesture is in flight, a workspace is committed: new and changed
//           slots (wheelhouse_slot_set), then the document at the generation
//           last seen (wheelhouse_arrangement_commit), then the user's slots it
//           no longer tabs are removed. A gesture edits a copy (a boundary
//           drag's rd_boundary_resize, a tab drag's drop) and sets nothing
//           until it ends, so a drag commits once, at its end.
//   follow  When the core changed the document itself (it placed a slot the
//           document had no tab for, or the provider's baseline changed), the
//           arrangement follows it (uishell_store_follow).
//   plan    Then every Slot is planned (wheelhouse_slot_plan), and an update
//           the core has for it is applied to its View ("Slot plans"
//           below): a spec another host changed, or a provider's new target,
//           reaches a View that is already open.
//
// A stale commit (the generation moved since it was read) is committed again
// at the new generation. Only the core's own reconciliation moves it, when
// the slot set changed from the provider's side, and recommitting runs that
// reconciliation again: new slots are placed and gone ones reported, so
// neither the user's edit nor the provider's change is lost. Dropping the
// edit instead would undo a gesture the user saw for a change they didn't.
//
// View kinds and their View Specs (CONTEXT.md, "View Spec"):
//
//   terminal   with a command       local recipe: the command (a shell line) and cwd
//              with a cleat_session local recipe: that Cleat endpoint's session
//                                   (cleat_transport, cleat_host, ...); it
//                                   is attached to, never started in place
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
//       panel: {id: 2 doc_id: "main"}            the number a provider's panel has here
//       view: {slot: "u:3" daemon}               a View's logical settings its spec doesn't hold
//       resolution: {slot: "u:3" session: ..}    a daemon session the core hasn't saved
//       previous: {terminal: {previous_of: ..}}  a Slot's previous instance, whole
//     }
//     view_presentation: {                       each View's Presentation State, by View
//       workspace: {key: "u:3" label: "Shell"}   Address (shell_core.c, "View Presentation
//     }                                          State"): its label, its machine-local
//     panel_presentation: {                      Target Resolutions, ...; and each panel's,
//       workspace: {panel: 2 focused}            by panel address (shell_core.c, "Panel
//   } }                                          Presentation State")
//
// A View whose slot follows its provider (`follows_provider`) keeps all of
// its logical settings here: what it runs is the provider's content as this
// device last resolved it, and its next plan applies the provider's again.
// `floating` and `kept` entries, saved before Floating Panels were removed,
// go with the rest of `arrangement_presentation` when it is followed.
//
// Target Resolutions (CONTEXT.md): which are portable is Wheelhouse's call.
// A terminal's Cleat daemon session is: its `session` and `daemon_name`,
// with this machine's name, are saved with the Slot in the core
// (wheelhouse_slot_resolution_set, kind "cleat-session"), and a View that
// starts without an instance (a restart, another device) attaches through
// it before starting afresh ("Portable Target Resolutions" below). A session
// saved from another machine is left alone: Cleat reaches daemons on this
// one only. The rest are machine-local, the View's Presentation State:
// `attach_token` and `porthole_session` (a capture session on this machine)
// and `managed_target` (the target this device last applied). A Jackstay
// endpoint is part of its View Spec, so the core has it already. So is a web
// View's page; where it got to (`web_url`) is portable too.

StaticAssert(WH_Content_Command == 1<<WHEELHOUSE_SLOT_COMMAND && WH_Content_File == 1<<WHEELHOUSE_SLOT_FILE &&
             WH_Content_Url == 1<<WHEELHOUSE_SLOT_URL && WH_Content_Jackstay == 1<<WHEELHOUSE_SLOT_JACKSTAY &&
             WH_Content_Facet == 1<<WHEELHOUSE_SLOT_FACET && WH_Content_CleatSession == 1<<WHEELHOUSE_SLOT_CLEAT_SESSION,
             uishell_store_content_kinds);

// Terminal View settings that name a Cleat endpoint, in the order of
// UIShell_ViewSpec's cleat_* fields. A terminal with a `cleat_session` shows
// that session (docs/design/remote-terminals.md).
read_only global String8 uishell_store_cleat_keys[] =
{
  str8_lit_comp("cleat_transport"), str8_lit_comp("cleat_host"), str8_lit_comp("cleat_runtime_root"),
  str8_lit_comp("cleat_daemon"), str8_lit_comp("cleat_session"),
};

// The most settings one View's spec takes (uishell_store_spec_from_view):
// a Cleat endpoint's, with the expression and cwd.
#define UISHELL_STORE_TAKEN_MAX 8

internal String8 *
uishell_store_cleat_fields(UIShell_ViewSpec *spec, U64 i)
{
  String8 *fields[] = {&spec->cleat_transport, &spec->cleat_host, &spec->cleat_runtime_root, &spec->cleat_daemon, &spec->cleat_session};
  StaticAssert(ArrayCount(fields) == ArrayCount(uishell_store_cleat_keys), uishell_store_cleat_field_count);
  return fields[i];
}

typedef struct UIShell_StoreFlag UIShell_StoreFlag;
struct UIShell_StoreFlag
{
  String8 key;
  // WHEELHOUSE_SLOT_FLAG_*
  U32 flags;
};

typedef struct UIShell_StoreEntry UIShell_StoreEntry;
struct UIShell_StoreEntry
{
  UIShell_WorkspaceId id;
  // The arrangement's version, and its Views' specs, as last committed or
  // followed, and the document's generation then.
  U64 version;
  U64 specs;
  U64 generation;
  // That document's slot keys, hashed: the tabs this device has seen. A tab
  // the stored document has and this one lacks was closed here only if it is
  // among them; otherwise it arrived since and this device never showed it.
  U64 *seen;
  U64 seen_count;
  U64 seen_capacity;
  // What the core says of its Workspace Overlay, as last read
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
  str8_lit_comp("managed_target"), str8_lit_comp("porthole_session"), str8_lit_comp("web_url"),
};
// In WH_TargetResolution's order (uishell_store_view_slot).
StaticAssert(ArrayCount(uishell_store_resolution_keys) == 6, uishell_store_resolution_fields);

// Runtime-only View settings: this process's progress through its Slot's
// plan, never saved (uishell_workspace_store_window_text drops them), so a
// restart plans afresh from what the core keeps.
read_only global String8 uishell_store_runtime_keys[] =
{
  // The resolution identity the View applied, as wheelhouse_slot_plan takes it.
  str8_lit_comp("applied"),
  // An `ask` rebind waiting for the user's answer: what it would show.
  str8_lit_comp("asks"),
  // The user declined that update, so focusing the workspace doesn't retry it.
  str8_lit_comp("declined"),
  // Applying an update failed; focusing the workspace retries it.
  str8_lit_comp("update_failed"),
  // The session and daemon the core has saved for it ("session|daemon").
  str8_lit_comp("portable"),
  // A hash of the content it showed once it applied that resolution: a View
  // following its provider whose content differs since, the user edited.
  str8_lit_comp("applied_spec"),
  // Why its Slot's content isn't resolved, as its last plan said
  // ("unavailable", "held" or "failed"); absent while it resolves. The Slot
  // Host reads it (uishell_store_view_slot).
  str8_lit_comp("content_state"),
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
// of it is device-local, and the core's spec for it is the provider's facet.
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
// most UISHELL_STORE_TAKEN_MAX). Returns 0 for a placeholder, whose slot
// keeps the spec it was made from.
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
  spec->content = WHEELHOUSE_SLOT_URL;
  taken[(*taken_count)++] = str8_lit("expression");
  if(str8_match(kind, str8_lit("terminal"), 0))
  {
    taken[(*taken_count)++] = str8_lit("cwd");
    if(uishell_store_setting(view, str8_lit("cleat_session")).size)
    {
      spec->content = WHEELHOUSE_SLOT_CLEAT_SESSION;
      for(U64 i = 0; i < ArrayCount(uishell_store_cleat_keys); i++)
      {
        *uishell_store_cleat_fields(spec, i) = uishell_store_setting(view, uishell_store_cleat_keys[i]);
        taken[(*taken_count)++] = uishell_store_cleat_keys[i];
      }
      return 1;
    }
    if(expr.size)
    {
      spec->content = WHEELHOUSE_SLOT_COMMAND;
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
  if(str8_match(kind, str8_lit("web"), 0))
  {
    // The page it opens: its URL, or the start page.
    String8 url = uishell_store_setting(view, str8_lit("url"));
    taken[(*taken_count)++] = str8_lit("url");
    spec->url = url.size ? url : uishell_store_page_url(arena, kind, 0, 0, 0);
    return 1;
  }
  if(str8_match(kind, str8_lit("jackstay"), 0))
  {
    for(U64 i = 0; i < ArrayCount(uishell_store_jackstay_keys); i++)
    {
      String8 endpoint = uishell_store_setting(view, uishell_store_jackstay_keys[i]);
      if(!endpoint.size) { continue; }
      spec->content = WHEELHOUSE_SLOT_JACKSTAY;
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
    spec->content = WHEELHOUSE_SLOT_FILE;
    spec->path = path;
    return 1;
  }
  String8 name = str8_lit("expression");
  spec->url = uishell_store_page_url(arena, kind, &name, &expr, 1);
  return 1;
}

// The workspace whose arrangement has `view` as a tab: a workspace node, or
// a window whose own panels are its workspace. Nil for a View outside one (a
// control view, an immediate tree's).
internal CFG_Node *
uishell_store_view_owner(CFG_Node *view)
{
  CFG_Node *host = &cfg_nil_node;
  if(rd_view_owner_kind_from_cfg(view, &host) != RD_ViewOwnerKind_Workspace) { return &cfg_nil_node; }
  return rd_panel_from_cfg(view).owner.kind == RD_PanelOwnerKind_Workspace ? host : &cfg_nil_node;
}

// `owner`'s arrangement's owner: a workspace's, or a window's own panels'.
internal RD_PanelOwner
uishell_store_owner(CFG_Node *owner)
{
  return rd_panel_owner_from_host(RD_PanelOwnerKind_Workspace, owner);
}

// Whether `view` has nothing of its own to show: made for a facet its
// provider hasn't resolved (uishell_store_view_from_spec), it follows its
// provider and has no command, file or page yet.
internal B32
uishell_store_view_unfilled(CFG_Node *view)
{
  return cfg_node_child_from_string(view, str8_lit("follows_provider")) != &cfg_nil_node &&
         rd_expr_from_cfg(view).size == 0 && uishell_store_setting(view, str8_lit("url")).size == 0;
}

// The wait a View's `content_state` names.
internal WH_SlotWait
uishell_store_slot_wait(String8 content_state)
{
  if(str8_match(content_state, str8_lit("unavailable"), 0)) { return WH_SlotWait_Unavailable; }
  if(str8_match(content_state, str8_lit("held"), 0)) { return WH_SlotWait_Held; }
  if(str8_match(content_state, str8_lit("failed"), 0)) { return WH_SlotWait_Failed; }
  return WH_SlotWait_None;
}

internal void
uishell_store_view_slot(Arena *arena, CFG_Node *view, WH_SlotAddress *slot, UIShell_ViewSpec const **spec,
                        WH_SlotStatus *status, WH_SlotWait *wait_out, WH_TargetResolution *resolution)
{
  String8 *resolved[] = {&resolution->session, &resolution->daemon_name, &resolution->attach_token,
                         &resolution->managed_target, &resolution->porthole_session, &resolution->web_url};
  // A machine-local one is the View's Presentation State.
  for(U64 i = 0; i < ArrayCount(resolved); i++) { *resolved[i] = rd_cfg_setting_child(view, uishell_store_resolution_keys[i])->first->string; }
  if(str8_match(view->string, str8_lit("placeholder"), 0)) { *status |= WH_SlotStatus_Placeholder; }
  // A Slot with nothing to show waits; one with a live instance whose content
  // stops resolving keeps it, unverified. A View that has content of its own
  // (opened from a recipe, or restored) shows it, as it always has: held
  // content stays as it is. A primary opened without a recipe waits before
  // the store has keyed it.
  WH_SlotWait wait = uishell_store_slot_wait(uishell_store_setting(view, str8_lit("content_state")));
  B32 has_instance = uishell_store_setting(view, str8_lit("applied")).size != 0 || resolution->session.size != 0;
  if(wait != WH_SlotWait_None && !has_instance && uishell_store_view_unfilled(view)) { *status |= WH_SlotStatus_Waiting; *wait_out = wait; }
  else if(wait == WH_SlotWait_Unavailable && has_instance) { *status |= WH_SlotStatus_Unverified; *wait_out = wait; }
  // (The wait above is read first, so a View the store hasn't keyed yet
  // still waits.)
  CFG_Node *owner = uishell_store_view_owner(view);
  String8 key = uishell_store_setting(view, str8_lit("slot"));
  if(owner == &cfg_nil_node || key.size == 0) { return; }
  // Read, never assigned: an arrangement's workspace has had its ID since
  // the inventory first listed it.
  uishell_workspace_id_from_string(uishell_store_setting(owner, str8_lit("workspace_id")), &slot->workspace);
  slot->key = key;
  *status |= WH_SlotStatus_Slot;
  // A Slot following its provider has the provider's spec, which the core
  // keeps; this device holds only what it last resolved it to.
  if(cfg_node_child_from_string(view, str8_lit("follows_provider")) != &cfg_nil_node)
  {
    *status |= WH_SlotStatus_FollowsProvider;
    return;
  }
  UIShell_ViewSpec *view_spec = push_array(arena, UIShell_ViewSpec, 1);
  String8 taken[UISHELL_STORE_TAKEN_MAX];
  U64 taken_count = 0;
  if(uishell_store_spec_from_view(arena, view, view_spec, taken, &taken_count)) { *spec = view_spec; }
}

internal B32
uishell_store_char_in(U8 c, String8 set)
{
  for(U64 i = 0; i < set.size; i++) { if(set.str[i] == c) { return 1; } }
  return 0;
}

// `argv` as a command line for the shell Cleat runs a terminal's command
// in, each argument reaching the program as it is. Cleat takes a command
// line, not argv: `$SHELL -lc <line>` on POSIX, so an argument that needs
// it is single-quoted (a ' inside as '\''; sh, bash, zsh and fish read it
// alike, though fish also unescapes \\ inside); `cmd.exe /D /C <line>` on
// Windows, so an argument that needs it is quoted as the C runtime parses
// argv, and every cmd.exe metacharacter, quotes included, is escaped with ^
// so that cmd.exe passes the quoting through. (Cleat itself quotes that line
// for cmd.exe as if for the C runtime, \" for each quote, which cmd.exe
// doesn't undo: until it passes the line as is, a Windows argument that
// needs quoting doesn't arrive intact.)
internal String8
uishell_store_command_line(Arena *arena, String8 *argv, U64 argc)
{
  String8List parts = {0};
  for(U64 i = 0; i < argc; i++)
  {
    String8 arg = argv[i];
    String8List out = {0};
#if OS_WINDOWS
    B32 quote = arg.size == 0;
    for(U64 j = 0; j < arg.size; j++) { quote = quote || arg.str[j] == ' ' || arg.str[j] == '\t' || arg.str[j] == '"'; }
    String8List quoted = {0};
    if(quote) { str8_list_push(arena, &quoted, str8_lit("\"")); }
    U64 backslashes = 0;
    for(U64 j = 0; j < arg.size; j++)
    {
      U8 c = arg.str[j];
      if(c == '\\') { backslashes++; continue; }
      // Backslashes before a quote are doubled, and the quote escaped.
      U64 n = c == '"' ? backslashes*2+1 : backslashes;
      for(U64 k = 0; k < n; k++) { str8_list_push(arena, &quoted, str8_lit("\\")); }
      str8_list_push(arena, &quoted, push_str8_copy(arena, str8(&arg.str[j], 1)));
      backslashes = 0;
    }
    // ... as are those before the closing quote.
    for(U64 k = 0; k < (quote ? backslashes*2 : backslashes); k++) { str8_list_push(arena, &quoted, str8_lit("\\")); }
    if(quote) { str8_list_push(arena, &quoted, str8_lit("\"")); }
    String8 text = str8_list_join(arena, &quoted, 0);
    for(U64 j = 0; j < text.size; j++)
    {
      U8 c = text.str[j];
      if(uishell_store_char_in(c, str8_lit("()%!^\"<>&|"))) { str8_list_push(arena, &out, str8_lit("^")); }
      str8_list_push(arena, &out, str8(&text.str[j], 1));
    }
#else
    B32 plain = arg.size != 0;
    for(U64 j = 0; j < arg.size; j++)
    {
      U8 c = arg.str[j];
      plain = plain && (char_is_alpha(c) || char_is_digit(c, 10) || uishell_store_char_in(c, str8_lit("_-+=@%:,./")));
    }
    if(plain) { str8_list_push(arena, &out, arg); }
    else
    {
      str8_list_push(arena, &out, str8_lit("'"));
      U64 start = 0;
      for(U64 j = 0; j <= arg.size; j++)
      {
        if(j < arg.size && arg.str[j] != '\'') { continue; }
        str8_list_push(arena, &out, str8_substr(arg, r1u64(start, j)));
        if(j < arg.size) { str8_list_push(arena, &out, str8_lit("'\\''")); }
        start = j+1;
      }
      str8_list_push(arena, &out, str8_lit("'"));
    }
#endif
    str8_list_push(arena, &parts, str8_list_join(arena, &out, 0));
  }
  StringJoin join = {.sep = str8_lit(" ")};
  return str8_list_join(arena, &parts, &join);
}

internal UIShell_ViewSpec
uishell_store_spec_from_core(Arena *arena, WheelhouseViewSpec *s)
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
    spec.argv = push_array(arena, String8, s->argc);
    spec.argc = s->argc;
    for(U64 i = 0; i < s->argc; i++) { spec.argv[i] = push_str8_copy(arena, uishell_sidebar_string(s->argv[i])); }
    spec.command = uishell_store_command_line(arena, spec.argv, spec.argc);
  }
  spec.has_cwd = s->has_cwd != 0;
  spec.cwd = uishell_sidebar_string(s->cwd);
  spec.path = uishell_sidebar_string(s->path);
  spec.url = uishell_sidebar_string(s->url);
  spec.launcher = uishell_sidebar_string(s->launcher);
  spec.endpoint = uishell_sidebar_string(s->endpoint);
  spec.presentation = s->has_presentation ? uishell_sidebar_string(s->presentation) : str8_zero();
  WheelhouseText cleat[] = {s->cleat_transport, s->cleat_host, s->cleat_runtime_root, s->cleat_daemon, s->cleat_session};
  for(U64 i = 0; i < ArrayCount(cleat); i++) { *uishell_store_cleat_fields(&spec, i) = uishell_sidebar_string(cleat[i]); }
  return spec;
}

// `spec` as the core takes it. A View holds only the command line it runs:
// when that is still the line of `was`'s argv (the slot's spec, if any), the
// slot keeps its argv rather than becoming this host's shell line.
internal WheelhouseViewSpec
uishell_store_core_spec(Arena *arena, UIShell_ViewSpec *spec, WheelhouseViewSpec *was)
{
  WheelhouseViewSpec s = {0};
  s.content = spec->content;
  s.entity = uishell_sidebar_entity(spec->provider, spec->kind, spec->id);
  s.facet = uishell_sidebar_text(spec->facet);
  s.command = uishell_sidebar_text(spec->command);
  if(spec->content == WHEELHOUSE_SLOT_COMMAND && was && was->content == WHEELHOUSE_SLOT_COMMAND && was->argc)
  {
    UIShell_ViewSpec prior = uishell_store_spec_from_core(arena, was);
    if(str8_match(prior.command, spec->command, 0))
    {
      WheelhouseText *argv = push_array(arena, WheelhouseText, prior.argc);
      for(U64 i = 0; i < prior.argc; i++) { argv[i] = uishell_sidebar_text(prior.argv[i]); }
      s.command = (WheelhouseText){0};
      s.argv = argv;
      s.argc = prior.argc;
    }
  }
  s.has_cwd = spec->has_cwd;
  s.cwd = uishell_sidebar_text(spec->cwd);
  s.path = uishell_sidebar_text(spec->path);
  s.url = uishell_sidebar_text(spec->url);
  s.launcher = uishell_sidebar_text(spec->launcher);
  s.endpoint = uishell_sidebar_text(spec->endpoint);
  s.has_presentation = spec->presentation.size != 0;
  s.presentation = uishell_sidebar_text(spec->presentation);
  WheelhouseText *cleat[] = {&s.cleat_transport, &s.cleat_host, &s.cleat_runtime_root, &s.cleat_daemon, &s.cleat_session};
  for(U64 i = 0; i < ArrayCount(cleat); i++) { *cleat[i] = uishell_sidebar_text(*uishell_store_cleat_fields(spec, i)); }
  return s;
}

internal B32
uishell_store_spec_match(UIShell_ViewSpec *a, UIShell_ViewSpec *b)
{
  return (a->content == b->content && str8_match(a->provider, b->provider, 0) && str8_match(a->kind, b->kind, 0) &&
          str8_match(a->id, b->id, 0) && str8_match(a->facet, b->facet, 0) && str8_match(a->command, b->command, 0) &&
          a->has_cwd == b->has_cwd && str8_match(a->cwd, b->cwd, 0) && str8_match(a->path, b->path, 0) &&
          str8_match(a->url, b->url, 0) && str8_match(a->launcher, b->launcher, 0) &&
          str8_match(a->endpoint, b->endpoint, 0) && str8_match(a->presentation, b->presentation, 0) &&
          str8_match(a->cleat_transport, b->cleat_transport, 0) && str8_match(a->cleat_host, b->cleat_host, 0) &&
          str8_match(a->cleat_runtime_root, b->cleat_runtime_root, 0) && str8_match(a->cleat_daemon, b->cleat_daemon, 0) &&
          str8_match(a->cleat_session, b->cleat_session, 0));
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
                      spec->url, spec->launcher, spec->endpoint, spec->presentation, spec->cleat_transport,
                      spec->cleat_host, spec->cleat_runtime_root, spec->cleat_daemon, spec->cleat_session};
  for(U64 i = 0; i < ArrayCount(fields); i++) { hash = uishell_store_hash_text(hash, fields[i]); }
  return hash*33 + spec->has_cwd;
}

// A View's kind and content as one hash ("applied_spec").
internal U64
uishell_store_view_spec_hash(CFG_Node *view)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_ViewSpec spec = {0};
  String8 taken[UISHELL_STORE_TAKEN_MAX];
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
    case WHEELHOUSE_SLOT_COMMAND: case WHEELHOUSE_SLOT_FILE: return str8_zero();
    case WHEELHOUSE_SLOT_CLEAT_SESSION:
    {
      // Cleat's ssh bridge is Unix only, and a transport Wheelhouse doesn't
      // know is never guessed at.
      B32 ssh = str8_match(spec->cleat_transport, str8_lit("ssh"), 0);
      if(!ssh && !str8_match(spec->cleat_transport, str8_lit("local"), 0))
      { return push_str8f(arena, "Cleat session %S over transport \"%S\"", spec->cleat_session, spec->cleat_transport); }
      if(OS_WINDOWS && ssh)
      { return push_str8f(arena, "Cleat session %S on %S", spec->cleat_session, spec->cleat_host); }
      return str8_zero();
    }
    case WHEELHOUSE_SLOT_JACKSTAY:
    {
      if(uishell_store_key_in(spec->launcher, uishell_store_jackstay_keys, ArrayCount(uishell_store_jackstay_keys))) { return str8_zero(); }
      return push_str8f(arena, "Jackstay launcher \"%S\" for %S", spec->launcher, spec->endpoint);
    }
    case WHEELHOUSE_SLOT_URL:
    {
      String8 kind = uishell_store_page_kind(spec->url);
      if(kind.size && (wh_renderer_from_name(kind)->content & WH_Content_Page)) { return str8_zero(); }
      // Another URL, when a Handler takes its scheme.
      if(!kind.size && wh_handler_from_url(spec->url)->name.size) { return str8_zero(); }
      return push_str8_copy(arena, spec->url);
    }
    case WHEELHOUSE_SLOT_FACET:
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

// A new View of `owner`'s, in no panel, showing slot `key`'s content, or a
// placeholder naming what it can't show (`spec` 0: the slot has gone).
internal CFG_Node *
uishell_store_view_from_spec(CFG_Node *owner, String8 key, UIShell_ViewSpec *spec)
{
  Temp scratch = scratch_begin(0, 0);
  String8 unshown = spec ? uishell_store_unshown(scratch.arena, key, spec) : str8_lit("a slot that has gone");
  String8 kind = wh_renderer_from_content(WH_Content_Unshown)->name, expr = str8_zero(), cwd = str8_zero(), url = str8_zero();
  B32 has_cwd = 0;
  if(!unshown.size)
  {
    // The Renderer for the content: the first in the registry that shows it.
    kind = wh_renderer_from_content(1<<spec->content)->name;
    switch(spec->content)
    {
      case WHEELHOUSE_SLOT_COMMAND: { expr = spec->command; has_cwd = spec->has_cwd; cwd = spec->cwd; } break;
      case WHEELHOUSE_SLOT_FILE: { expr = rd_eval_string_from_file_path(scratch.arena, spec->path); } break;
      case WHEELHOUSE_SLOT_URL:
      {
        kind = uishell_store_page_kind(spec->url);
        // Another URL: its Handler's Renderer shows it, there.
        if(!kind.size) { kind = wh_handler_from_url(spec->url)->renderer; url = spec->url; }
        expr = uishell_store_page_param(scratch.arena, spec->url, str8_lit("expression"));
        cwd = uishell_store_page_param(scratch.arena, spec->url, str8_lit("cwd"));
        has_cwd = cwd.size != 0;
      } break;
    }
  }
  CFG_Node *view = rd_cfg_new_view_tab(rd_view_namespace(owner, RD_ViewOwnerKind_Workspace), kind, expr, 0);
  uishell_store_set_setting(view, str8_lit("slot"), key);
  if(has_cwd) { uishell_store_set_setting(view, str8_lit("cwd"), cwd); }
  if(url.size) { uishell_store_set_setting(view, str8_lit("url"), url); }
  if(!unshown.size && spec->content == WHEELHOUSE_SLOT_JACKSTAY) { uishell_store_set_setting(view, spec->launcher, spec->endpoint); }
  if(!unshown.size && spec->content == WHEELHOUSE_SLOT_CLEAT_SESSION)
  {
    for(U64 i = 0; i < ArrayCount(uishell_store_cleat_keys); i++)
    {
      String8 value = *uishell_store_cleat_fields(spec, i);
      if(value.size) { uishell_store_set_setting(view, uishell_store_cleat_keys[i], value); }
    }
  }
  if(spec && spec->content == WHEELHOUSE_SLOT_FACET)
  {
    uishell_store_set_setting(view, str8_lit("resource_id"), key);
    uishell_store_mark_follows(view, 1);
    // A facet has nothing to show until its plan resolves it, so its Slot
    // Host waits rather than start a terminal with no command (a shell).
    uishell_store_set_setting(view, str8_lit("content_state"), str8_lit("unavailable"));
  }
  if(unshown.size)
  {
    uishell_store_set_setting(view, str8_lit("content"), unshown);
    if(spec && spec->content == WHEELHOUSE_SLOT_URL) { uishell_store_set_setting(view, str8_lit("url"), spec->url); }
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

// A provider's key (no ':'), a workspace template's ("t:" and a provider
// key), or one of the user's.
internal B32
uishell_store_key_valid(String8 key)
{
  if(uishell_store_user_key_valid(key)) { return 1; }
  if(str8_match(str8_prefix(key, 2), str8_lit("t:"), 0)) { key = str8_skip(key, 2); }
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
uishell_store_slot_find(WheelhouseSlots *slots, String8 key, WheelhouseSlot *out)
{
  for(U64 i = 0; slots && i < slots->slot_count; i++)
  {
    if(!str8_match(uishell_sidebar_string(slots->slots[i].key), key, 0)) { continue; }
    if(out) { *out = slots->slots[i]; }
    return 1;
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
  // In preorder, each naming its parent by index.
  WheelhouseDocPanel *panels;
  U64 panel_count;
  // Slot keys in tree order, each panel's adjacent (its `tabs`), and their
  // Views.
  WheelhouseText *tabs;
  CFG_Node **views;
  String8 *keys;
  U64 tab_count;
};

// A Slot's previous instance, kept beside its View by a keep-previous rebind
// ("Slot plans"): a tab of the arrangement, but not a Slot, so not in the
// document.
internal String8
uishell_store_previous_of(CFG_Node *view)
{
  return uishell_store_setting(view, str8_lit("previous_of"));
}

// The View of Slot `key` in `owner`, or (RD_ViewInstance_Previous) the
// previous instance kept beside it, by address.
internal RD_ViewAddress
uishell_store_slot_view(CFG_Node *owner, String8 key, RD_ViewInstance instance)
{
  // Read, never assigned: an arrangement's workspace has had its ID since
  // the inventory first listed it.
  UIShell_WorkspaceId id = {0};
  uishell_workspace_id_from_string(uishell_store_setting(owner, str8_lit("workspace_id")), &id);
  return rd_view_address_make(RD_ViewOwnerKind_Workspace, id, key, instance);
}

// `a`, an arrangement of a workspace's, as the core's document, its tabs
// named by their Views' slot keys.
internal UIShell_StoreDoc
uishell_store_doc(Arena *arena, RD_Arrangement *a)
{
  UIShell_StoreDoc doc = {a};
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    doc.panel_count += 1;
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next) { doc.tab_count += !uishell_store_previous_of(rd_cfg_from_view(t->view)).size; }
  }
  doc.panels = push_array(arena, WheelhouseDocPanel, doc.panel_count);
  doc.tabs = push_array(arena, WheelhouseText, doc.tab_count);
  doc.views = push_array(arena, CFG_Node *, doc.tab_count);
  doc.keys = push_array(arena, String8, doc.tab_count);
  RD_ArrangementPanel **order = push_array(arena, RD_ArrangementPanel *, doc.panel_count);
  U64 index = 0, tab = 0;
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p), index++)
  {
    order[index] = p;
    WheelhouseDocPanel *out = &doc.panels[index];
    out->parent = WHEELHOUSE_NONE;
    for(U64 j = index; j > 0 && out->parent == WHEELHOUSE_NONE; j--) { if(order[j-1] == p->parent) { out->parent = j-1; } }
    // A provider's panel keeps the ID its document gave it.
    out->id = uishell_sidebar_text(p->doc_id.size ? p->doc_id : push_str8f(arena, "%I64u", p->id));
    // The core takes finite positive weights; the root's means nothing. A
    // weight is written as config writes it (0.7, not 0.699999988), which
    // reads back as the same F32.
    F32 weight = p == a->root ? 1.f : p->weight;
    out->weight = (weight > 0 && weight < 1e30f) ? f64_from_str8(rd_arrangement_weight_string(arena, weight)) : 0.01;
    out->selected = WHEELHOUSE_NONE;
    if(p->first != &rd_nil_arrangement_panel)
    {
      out->kind = WHEELHOUSE_PANEL_SPLIT;
      out->axis = rd_arrangement_split_axis(a, p) == Axis2_X ? WHEELHOUSE_AXIS_ROW : WHEELHOUSE_AXIS_COLUMN;
      continue;
    }
    out->kind = WHEELHOUSE_PANEL_TABS;
    out->tabs = doc.tabs+tab;
    // A previous instance selected stands for its Slot's View.
    String8 selected_previous = uishell_store_previous_of(rd_cfg_from_view(p->selected));
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
    {
      CFG_Node *view = rd_cfg_from_view(t->view);
      if(uishell_store_previous_of(view).size) { continue; }
      doc.views[tab] = view;
      doc.keys[tab] = uishell_store_setting(view, str8_lit("slot"));
      doc.tabs[tab] = uishell_sidebar_text(doc.keys[tab]);
      if(rd_view_address_match(t->view, p->selected) || (selected_previous.size && str8_match(selected_previous, doc.keys[tab], 0)))
      { out->selected = out->tab_count; }
      out->tab_count += 1;
      tab++;
    }
  }
  return doc;
}

// Each tab's Slot key, View kind and spec, as one hash: what a commit sends
// of its Views. A View's spec is its node's, which config edits change.
internal U64
uishell_store_specs(UIShell_StoreDoc *doc)
{
  Temp scratch = scratch_begin(0, 0);
  U64 hash = 5381;
  for(U64 i = 0; i < doc->tab_count; i++)
  {
    hash = uishell_store_hash_text(hash, doc->keys[i]);
    hash = uishell_store_hash_text(hash, doc->views[i]->string);
    UIShell_ViewSpec spec = {0};
    String8 taken[UISHELL_STORE_TAKEN_MAX];
    U64 taken_count = 0;
    if(uishell_store_spec_from_view(scratch.arena, doc->views[i], &spec, taken, &taken_count)) { hash = uishell_store_spec_hash(hash, &spec); }
  }
  scratch_end(scratch);
  return hash;
}

// Gives each tab a slot key no other tab of the workspace has: the one it
// has, else the key of the provider's slot it was made for (its
// resource_id) when the core has that slot, else "u:<resource_id>", else
// the next "u:<n>". A View's key is saved on it, and its tab follows it to
// its address. Returns whether any changed.
internal B32
uishell_store_assign_keys(Arena *arena, CFG_Node *owner, UIShell_StoreDoc *doc, WheelhouseSlots *slots)
{
  B32 changed = 0;
  // Past every key of the workspace's namespace (the Presentation State it
  // keeps included), so no View takes another's.
  U64 next = rd_view_user_number_max(owner, RD_ViewOwnerKind_Workspace)+1;
  for(U64 i = 0; i < doc->tab_count; i++) { next = Max(next, uishell_store_user_number(doc->keys[i])+1); }
  for(U64 i = 0; slots && i < slots->slot_count; i++) { next = Max(next, uishell_store_user_number(uishell_sidebar_string(slots->slots[i].key))+1); }
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
    doc->tabs[i] = uishell_sidebar_text(key);
    RD_ViewAddress was = rd_view_address_from_cfg(doc->views[i]);
    uishell_store_set_setting(doc->views[i], str8_lit("slot"), key);
    RD_ViewAddress now = rd_view_address_from_cfg(doc->views[i]);
    rd_view_readdress(was, now);
    rd_arrangement_readdress(doc->arrangement, was, now);
    changed = 1;
  }
  return changed;
}

//- Portable Target Resolutions
//
// A Cleat daemon session is saved with its Slot as kind "cleat-session",
// fields `host` (this machine's name), `session` and `daemon` (its
// daemon_name; none for the default daemon), made for the resolution the View
// applied. A View marks what the core has (`portable`), so the presentation
// file leaves those settings out (uishell_workspace_store_window_text).
// The page a web View reached is saved as kind "web-location", field `url`
// (its `web_url`): any machine can open it.

// This machine's name, which a saved Cleat session names its daemon's host by.
internal String8
uishell_store_host(void)
{
  String8 name = get_system_info()->machine_name;
  return name.size ? name : str8_lit("localhost");
}

internal String8
uishell_store_resolution_field(WheelhouseResolution *r, String8 name)
{
  for(U64 i = 0; i < r->field_count; i++)
  { if(str8_match(uishell_sidebar_string(r->fields[i].name), name, 0)) { return uishell_sidebar_string(r->fields[i].value); } }
  return str8_zero();
}

// Whether `r` is a Cleat session this machine saved: one it can attach to.
internal B32
uishell_store_resolution_ours(WheelhouseResolution *r)
{
  return str8_match(uishell_sidebar_string(r->kind), str8_lit("cleat-session"), 0) &&
         str8_match(uishell_store_resolution_field(r, str8_lit("host")), uishell_store_host(), 0);
}

internal String8
uishell_store_portable_text(Arena *arena, String8 session, String8 daemon)
{
  return push_str8f(arena, "%S|%S", session, daemon);
}

// What a View marks as saved with its Slot (`portable`): a terminal's session
// and daemon, or the page a web View reached.
internal String8
uishell_store_view_portable_text(Arena *arena, CFG_Node *view)
{
  if(str8_match(view->string, str8_lit("web"), 0)) { return uishell_store_setting(view, str8_lit("web_url")); }
  return uishell_store_portable_text(arena, uishell_store_setting(view, str8_lit("session")),
                                     uishell_store_setting(view, str8_lit("daemon_name")));
}

// Gives web `view`, which hasn't reached a page, the page `r` saved.
internal B32
uishell_store_restore_web_location(CFG_Node *view, WheelhouseResolution *r)
{
  String8 url = uishell_store_resolution_field(r, str8_lit("url"));
  if(!str8_match(uishell_sidebar_string(r->kind), str8_lit("web-location"), 0) || !url.size ||
     uishell_store_setting(view, str8_lit("web_url")).size) { return 0; }
  uishell_store_set_setting(view, str8_lit("web_url"), url);
  uishell_store_set_setting(view, str8_lit("portable"), url);
  return 1;
}

// Gives terminal `view`, which hasn't a session, the saved session `r` when
// this machine can attach to it, or web `view` the page it reached. Returns
// whether it did.
internal B32
uishell_store_restore_resolution(CFG_Node *view, WheelhouseResolution *r)
{
  if(str8_match(view->string, str8_lit("web"), 0)) { return uishell_store_restore_web_location(view, r); }
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

// Saves web `view`'s page with Slot `key`, as for a terminal's session below,
// except that any machine's is replaced.
internal void
uishell_store_save_web_location(UIShell_SidebarState *state, WheelhouseWorkspaceId workspace, String8 key, CFG_Node *view,
                                String8 applied, WheelhouseResolution *saved)
{
  String8 url = uishell_store_setting(view, str8_lit("web_url"));
  B32 same = saved && str8_match(uishell_sidebar_string(saved->kind), str8_lit("web-location"), 0) &&
             str8_match(uishell_store_resolution_field(saved, str8_lit("url")), url, 0);
  char *error = 0;
  if(same) {}
  else if(!url.size)
  {
    if(saved)
    {
      uint32_t result = UIShell_StoreCall(state, wheelhouse_slot_resolution_clear(state->core, workspace, uishell_sidebar_text(key),
                                                                                saved->generation, 0, &error));
      uishell_sidebar_result(state, result != WHEELHOUSE_INVALID, error);
    }
  }
  else
  {
    WheelhouseResolutionField field = {uishell_sidebar_text(str8_lit("url")), uishell_sidebar_text(url)};
    uint32_t result = UIShell_StoreCall(state, wheelhouse_slot_resolution_set(state->core, workspace, uishell_sidebar_text(key),
      uishell_sidebar_text(str8_lit("web-location")), &field, 1, uishell_sidebar_text(applied),
      saved ? saved->generation : 0, 0, &error));
    // Stale: another save came first; the next plan carries it.
    same = result == WHEELHOUSE_COMMITTED;
    uishell_sidebar_result(state, result != WHEELHOUSE_INVALID, error);
  }
  if(same) { uishell_store_set_setting(view, str8_lit("portable"), url); }
  else { uishell_store_drop_setting(view, str8_lit("portable")); }
}

// Saves terminal `view`'s daemon session with Slot `key`, for the resolution
// it applied (`applied`, which its plan reports current), when the core
// has none or this machine's older one (`saved`, from the plan; 0 for none);
// clears this machine's once the View has none. Another machine's is left
// alone.
internal void
uishell_store_save_resolution(UIShell_SidebarState *state, WheelhouseWorkspaceId workspace, String8 key, CFG_Node *view,
                              String8 applied, WheelhouseResolution *saved)
{
  if(!applied.size) { return; }
  if(str8_match(view->string, str8_lit("web"), 0)) { uishell_store_save_web_location(state, workspace, key, view, applied, saved); return; }
  if(!str8_match(view->string, str8_lit("terminal"), 0)) { return; }
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
      uint32_t result = UIShell_StoreCall(state, wheelhouse_slot_resolution_clear(state->core, workspace, uishell_sidebar_text(key),
                                                                                saved->generation, 0, &error));
      uishell_sidebar_result(state, result != WHEELHOUSE_INVALID, error);
      error = 0;
    }
  }
  else
  {
    WheelhouseResolutionField fields[3] = {0};
    U64 count = 0;
    if(daemon.size) { fields[count++] = (WheelhouseResolutionField){uishell_sidebar_text(str8_lit("daemon")), uishell_sidebar_text(daemon)}; }
    fields[count++] = (WheelhouseResolutionField){uishell_sidebar_text(str8_lit("host")), uishell_sidebar_text(uishell_store_host())};
    fields[count++] = (WheelhouseResolutionField){uishell_sidebar_text(str8_lit("session")), uishell_sidebar_text(session)};
    uint32_t result = UIShell_StoreCall(state, wheelhouse_slot_resolution_set(state->core, workspace, uishell_sidebar_text(key),
      uishell_sidebar_text(str8_lit("cleat-session")), fields, count, uishell_sidebar_text(applied),
      saved ? saved->generation : 0, 0, &error));
    // Stale: another save came first; the next plan carries it.
    same = result == WHEELHOUSE_COMMITTED;
    uishell_sidebar_result(state, result != WHEELHOUSE_INVALID, error);
    error = 0;
  }
  if(same) { uishell_store_set_setting(view, str8_lit("portable"), portable); }
  else { uishell_store_drop_setting(view, str8_lit("portable")); }
  scratch_end(scratch);
}

//- Following the core's document
//
// Both arrangements cross as the same document (WheelhouseArrangementDoc):
// panels in preorder, each naming its parent by index. A typed arrangement's
// splits alternate axes, so a split along its parent's axis gives its
// children to the parent, each scaled to the share it had. Following is one
// pass over the panels: each that makes a panel is added to its parent's.

typedef struct UIShell_DocShape UIShell_DocShape;
struct UIShell_DocShape
{
  // Per panel: whether it is a split lifted into its parent (it makes no
  // panel), the panel it is added to (an earlier one that makes a panel, or
  // WHEELHOUSE_NONE for a root), and its weight there.
  B32 *lifted;
  U64 *parent;
  F32 *weight;
};

internal UIShell_DocShape
uishell_store_doc_shape(Arena *arena, WheelhouseArrangementDoc const *doc)
{
  U64 count = doc->panel_count;
  UIShell_DocShape shape = {push_array(arena, B32, count+1), push_array(arena, U64, count+1), push_array(arena, F32, count+1)};
  F64 *weight = push_array(arena, F64, count+1), *children = push_array(arena, F64, count+1);
  for(U64 i = 0; i < count; i++)
  {
    U64 up = doc->panels[i].parent;
    if(up < i) { children[up] += doc->panels[i].weight; }
  }
  for(U64 i = 0; i < count; i++)
  {
    WheelhouseDocPanel const *p = &doc->panels[i];
    U64 up = p->parent < i ? p->parent : WHEELHOUSE_NONE;
    weight[i] = p->weight;
    // A lifted split's children are its parent's, sharing its weight.
    if(up != WHEELHOUSE_NONE && shape.lifted[up])
    {
      weight[i] = children[up] > 0 ? weight[up]*p->weight/children[up] : weight[up];
      up = shape.parent[up];
    }
    shape.lifted[i] = up != WHEELHOUSE_NONE && p->kind == WHEELHOUSE_PANEL_SPLIT && doc->panels[up].kind == WHEELHOUSE_PANEL_SPLIT &&
      p->axis == doc->panels[up].axis;
    shape.parent[i] = up;
    shape.weight[i] = (F32)weight[i];
  }
  return shape;
}

// Whether the core noted `key` as `kind` (WHEELHOUSE_NOTE_*).
internal B32
uishell_store_noted(WheelhouseWorkspaceArrangement *a, U32 kind, String8 key)
{
  for(U64 i = 0; a && i < a->note_count; i++)
  { if(a->notes[i].kind == kind && str8_match(uishell_sidebar_string(a->notes[i].key), key, 0)) { return 1; } }
  return 0;
}

// `owner`'s arrangement follows the core's document: the View of every
// slot it still tabs stays, as does every panel whose ID it keeps; a View
// made for a slot new here takes its settings, and Slots their previous
// instances, from the device-local `arrangement_presentation`, which goes.
// Returns whether the core had a document to follow.
internal B32
uishell_store_follow(UIShell_SidebarState *state, CFG_Node *owner, UIShell_WorkspaceId id, U64 *generation_out)
{
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(id);
  WheelhouseSlots *slots = UIShell_StoreCall(state, wheelhouse_slots_get(state->core, workspace, 0));
  WheelhouseWorkspaceArrangement *stored = slots ? UIShell_StoreCall(state, wheelhouse_arrangement_get(state->core, workspace, 0)) : 0;
  B32 built = stored != 0;
  if(built && generation_out) { *generation_out = stored->generation; }
  CFG_Node *device = cfg_node_child_from_string(owner, str8_lit("arrangement_presentation"));
  if(built && stored->doc.panel_count == 0 && device == &cfg_nil_node) { built = 0; }
  if(built)
  {
    // What this device kept, and the Views and panels it has now.
    RD_Arrangement *a = rd_arrangement_from_panel_owner(arena, uishell_store_owner(owner));
    UIShell_StoreDoc current = uishell_store_doc(arena, a);
    U64 old_count = current.panel_count;
    RD_PanelID *old_ids = push_array(arena, RD_PanelID, old_count);
    String8 *old_words = push_array(arena, String8, old_count);
    // Previous instances, which go back beside their Slots' Views.
    CFG_NodePtrList previous = {0};
    U64 n = 0;
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p), n++)
    {
      old_ids[n] = p->id;
      old_words[n] = p->doc_id;
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        CFG_Node *view = rd_cfg_from_view(t->view);
        if(uishell_store_previous_of(view).size) { cfg_node_ptr_list_push(arena, &previous, view); }
      }
    }
    // And those this device kept, for Slots that have none beside (a
    // restart): one per Slot.
    CFG_NodePtrList restored = {0};
    for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next)
    {
      String8 of = uishell_store_previous_of(c->first);
      if(!str8_match(c->string, str8_lit("previous"), 0) || !of.size) { continue; }
      B32 kept = 0;
      for(CFG_NodePtrNode *p = previous.first; p && !kept; p = p->next) { kept = str8_match(uishell_store_previous_of(p->v), of, 0); }
      if(kept) { continue; }
      CFG_Node *view = cfg_node_deep_copy(rd_state->cfg, c->first);
      CFG_Node *home = rd_view_namespace(owner, RD_ViewOwnerKind_Workspace);
      cfg_node_insert_child(rd_state->cfg, home, home->last, view);
      cfg_node_ptr_list_push(arena, &previous, view);
      cfg_node_ptr_list_push(arena, &restored, view);
    }
    WheelhouseArrangementDoc *doc = &stored->doc;
    UIShell_DocShape shape = uishell_store_doc_shape(arena, doc);
    U64 max_id = 0;
    for(U64 i = 0; i < doc->panel_count; i++)
    {
      String8 text = uishell_sidebar_string(doc->panels[i].id);
      if(str8_is_integer(text, 10) && text.size < 18) { max_id = Max(max_id, u64_from_str8(text, 10)); }
    }

    // The core's IDs are this device's numbers once it has committed; a
    // provider's hint names panels in words, which get numbers here, and
    // keep their words (`doc_id`) so a commit names them as the provider
    // does. The number a word had is this device's: its panel's, or this
    // device's settings for it.
    for(U64 i = 0; i < old_count; i++) { max_id = Max(max_id, old_ids[i]); }
    for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next)
    {
      String8 number = uishell_store_setting(c, str8_lit("id"));
      if(str8_match(c->string, str8_lit("panel"), 0) && str8_is_integer(number, 10) && number.size < 18) { max_id = Max(max_id, u64_from_str8(number, 10)); }
    }
    U64 fresh = max_id+1;
    if(!doc->panel_count) { rd_arrangement_discard(a); }
    else { rd_arrangement_clear(a); }
    // Allocations stay clear of the IDs set below until they are all set.
    a->next_id = max_id + (1ull << 40);
    if(doc->panel_count && doc->panels[0].kind == WHEELHOUSE_PANEL_SPLIT)
    { a->root_axis = doc->panels[0].axis == WHEELHOUSE_AXIS_ROW ? Axis2_X : Axis2_Y; }
    U64 ids_used = 0;
    RD_PanelID *ids = push_array(arena, RD_PanelID, doc->panel_count+1);
    // Each panel's panel in the arrangement.
    RD_PanelID *made = push_array(arena, RD_PanelID, doc->panel_count+1);
    for(U64 index = 0; index < doc->panel_count; index++)
    {
      WheelhouseDocPanel *src = &doc->panels[index];
      U64 up = shape.parent[index];
      if(index && up == WHEELHOUSE_NONE) { continue; }
      if(shape.lifted[index]) { made[index] = made[up]; continue; }
      RD_PanelID panel_id = up != WHEELHOUSE_NONE ? rd_arrangement_add(a, made[up], shape.weight[index]) : a->root->id;
      RD_ArrangementPanel *panel = rd_arrangement_panel_from_id(a, panel_id);
      String8 src_id = uishell_sidebar_string(src->id);
      B32 word = !(str8_is_integer(src_id, 10) && src_id.size < 18);
      RD_PanelID wanted = word ? 0 : u64_from_str8(src_id, 10);
      for(U64 i = 0; i < old_count && word && !wanted; i++) { if(str8_match(old_words[i], src_id, 0)) { wanted = old_ids[i]; } }
      for(CFG_Node *c = device->first; c != &cfg_nil_node && word && !wanted; c = c->next)
      {
        String8 number = uishell_store_setting(c, str8_lit("id"));
        if(str8_match(c->string, str8_lit("panel"), 0) && str8_match(uishell_store_setting(c, str8_lit("doc_id")), src_id, 0) &&
           str8_is_integer(number, 10) && number.size < 18) { wanted = u64_from_str8(number, 10); }
      }
      for(U64 i = 0; i < ids_used && wanted; i++) { if(ids[i] == wanted) { wanted = 0; } }
      if(!wanted) { wanted = fresh++; }
      panel->id = wanted;
      panel->doc_id = word ? push_str8_copy(arena, src_id) : str8_zero();
      made[index] = wanted;
      ids[ids_used++] = wanted;
      if(src->kind != WHEELHOUSE_PANEL_TABS) { continue; }
      RD_ViewAddress prev = rd_nil_view_address, selected = rd_nil_view_address;
      for(U64 t = 0; t < src->tab_count; t++)
      {
        String8 key = uishell_sidebar_string(src->tabs[t]);
        B32 gone = uishell_store_noted(stored, WHEELHOUSE_NOTE_GONE, key);
        CFG_Node *view = &cfg_nil_node;
        for(U64 i = 0; i < current.tab_count && view == &cfg_nil_node; i++)
        { if(str8_match(current.keys[i], key, 0)) { view = current.views[i]; } }
        WheelhouseSlot slot = {0};
        B32 known = !gone && uishell_store_slot_find(slots, key, &slot);
        if(view == &cfg_nil_node)
        {
          UIShell_ViewSpec spec = {0};
          if(known) { spec = uishell_store_spec_from_core(arena, &slot.spec); }
          view = uishell_store_view_from_spec(owner, key, known ? &spec : 0);
          // This device's part of its logical settings (a daemon session
          // the core hasn't saved, the daemon hosting it). Its Presentation
          // State is its owner's (rd_view_presentation_from_view).
          for(CFG_Node *c = device->first; c != &cfg_nil_node; c = c->next)
          {
            B32 settings = str8_match(c->string, str8_lit("view"), 0) || str8_match(c->string, str8_lit("resolution"), 0);
            if(!settings || !str8_match(uishell_store_setting(c, str8_lit("slot")), key, 0)) { continue; }
            for(CFG_Node *setting = c->first; setting != &cfg_nil_node; setting = setting->next)
            {
              if(str8_match(setting->string, str8_lit("slot"), 0) || !rd_view_setting_is_logical(setting->string)) { continue; }
              cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, setting->string));
              cfg_node_insert_child(rd_state->cfg, view, view->last, cfg_node_deep_copy(rd_state->cfg, setting));
            }
          }
          // A portable resolution the core saved, tried before the View
          // starts anything of its own.
          if(known && slot.has_saved) { uishell_store_restore_resolution(view, &slot.saved); }
        }
        // Whether it follows its provider, as committing would mark it.
        if(known && !str8_match(view->string, str8_lit("placeholder"), 0))
        { uishell_store_mark_follows(view, (slot.in_baseline && !slot.detached) || slot.spec.content == WHEELHOUSE_SLOT_FACET); }
        RD_ViewAddress address = rd_view_address_from_cfg(view);
        rd_arrangement_insert_tab(a, address, wanted, prev, 0);
        if(t == src->selected) { selected = address; }
        prev = address;
        for(CFG_NodePtrNode *p = previous.first; p; p = p->next)
        {
          if(!str8_match(uishell_store_previous_of(p->v), key, 0)) { continue; }
          RD_ViewAddress kept = rd_view_address_from_cfg(p->v);
          rd_arrangement_insert_tab(a, kept, wanted, prev, 0);
          prev = kept;
        }
      }
      rd_arrangement_select(a, wanted, selected);
    }
    a->next_id = fresh;
    for(U64 i = 0; i < ids_used; i++) { a->next_id = Max(a->next_id, ids[i]+1); }
    // The core's weights are relative to siblings (a layout's are integers,
    // agents 3 beside files 2); a panel's here is its share of its parent.
    // The core refuses weights that aren't positive, so a total is too.
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    {
      F32 total = 0;
      for(RD_ArrangementPanel *c = p->first; c != &rd_nil_arrangement_panel; c = c->next) { total += c->weight; }
      if(total <= 0 || abs_f32(total-1.f) <= .0001f) { continue; }
      for(RD_ArrangementPanel *c = p->first; c != &rd_nil_arrangement_panel; c = c->next)
      { c->weight = rd_arrangement_quantize(c->weight/total); }
    }
    rd_arrangement_set(a);
    // A kept previous instance whose Slot the document no longer tabs goes.
    for(CFG_NodePtrNode *p = restored.first; p; p = p->next)
    {
      if(rd_panel_from_cfg(p->v).id == 0) { cfg_node_release(rd_state->cfg, p->v); }
    }
    if(device != &cfg_nil_node) { cfg_node_release(rd_state->cfg, device); }
    uishell_store_set_setting(owner, str8_lit("arrangement_generation"), push_str8f(arena, "%I64u", stored->generation));
  }
  wheelhouse_arrangement_free(stored);
  wheelhouse_slots_free(slots);
  scratch_end(scratch);
  return built;
}

//- Slot plans
//
// Every Slot follows the core's plan/token protocol (wheelhouse.h, "each
// slot follows managed primary content's protocol"). After each sync, each
// of a workspace's Views that is a Slot is planned with the resolution
// identity it applied (`applied`: empty for none, as after a restart, so a
// fresh process starts from what the core has saved), and:
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
//                 (wheelhouse_slot_release_previous)
//     ask         nothing until the user answers the View's prompt (`asks`);
//                 declining fails the update until the resolution changes
//   HELD, UNAVAILABLE, FAILED   the View keeps what it shows; focusing the
//                 workspace retries a failed update (uishell_store_retry).
//                 The state is kept on the View (`content_state`) for its
//                 Slot Host: a View with nothing to show yet shows a
//                 placeholder, and a live one whose content is unavailable
//                 is marked unverified (shell_core.c, "Slot Host")
//
// A Cleat daemon's session is never replaced in place: whatever the policy,
// it stays as the previous instance.

// What an update would show, for its prompt.
internal String8
uishell_store_recipe_text(Arena *arena, UIShell_ViewSpec *recipe)
{
  switch(recipe->content)
  {
    case WHEELHOUSE_SLOT_COMMAND: return recipe->command.size ? recipe->command : str8_lit("a shell");
    case WHEELHOUSE_SLOT_FILE: return recipe->path;
    case WHEELHOUSE_SLOT_URL: return recipe->url;
    case WHEELHOUSE_SLOT_JACKSTAY: return recipe->endpoint;
    case WHEELHOUSE_SLOT_CLEAT_SESSION:
    {
      if(str8_match(recipe->cleat_transport, str8_lit("ssh"), 0))
      { return push_str8f(arena, "session %S on %S", recipe->cleat_session, recipe->cleat_host); }
      return push_str8f(arena, "session %S", recipe->cleat_session);
    }
  }
  return str8_lit("new content");
}

// The View kind a recipe shows as, and what of it Wheelhouse can't show.
internal String8
uishell_store_recipe_kind(Arena *arena, String8 key, UIShell_ViewSpec *recipe, String8 *unshown)
{
  *unshown = recipe->content == WHEELHOUSE_SLOT_FACET ? str8_lit("content its provider hasn't resolved") :
    uishell_store_unshown(arena, key, recipe);
  if(unshown->size) { return wh_renderer_from_content(WH_Content_Unshown)->name; }
  if(recipe->content == WHEELHOUSE_SLOT_URL) { return uishell_store_page_kind(recipe->url); }
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
  String8 taken[UISHELL_STORE_TAKEN_MAX];
  U64 taken_count = 0;
  if(!uishell_store_spec_from_view(arena, view, &current, taken, &taken_count)) { return 0; }
  current.presentation = str8_zero();
  UIShell_ViewSpec wanted = *recipe;
  wanted.presentation = str8_zero();
  return uishell_store_spec_match(&current, &wanted);
}

// The tab before `view` in its panel (nil: it is first).
internal RD_ViewAddress
uishell_store_tab_before(RD_Arrangement *a, RD_ViewAddress view)
{
  RD_ArrangementPanel *panel = rd_arrangement_panel_from_view(a, view);
  RD_ViewAddress prev = rd_nil_view_address;
  for(RD_ArrangementTab *t = panel->first_tab; t && !rd_view_address_match(t->view, view); t = t->next) { prev = t->view; }
  return prev;
}

// Applies an Updating plan's recipe to `view`, the View of Slot `key` in
// `owner`, if its token is still valid. Returns the View that shows it now (a
// new node when the old one couldn't take it in place), or nil when the
// update failed or its token went stale.
internal CFG_Node *
uishell_store_apply(UIShell_SidebarState *state, CFG_Node *owner, CFG_Node *view, String8 key,
                    WheelhouseSlotContent *content, B32 keep_previous)
{
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  UIShell_ViewSpec recipe = uishell_store_spec_from_core(arena, &content->recipe);
  String8 unshown = str8_zero();
  String8 kind = uishell_store_recipe_kind(arena, key, &recipe, &unshown);
  B32 shows = uishell_store_view_shows(arena, view, key, &recipe);
  B32 terminal = str8_match(view->string, str8_lit("terminal"), 0);
  if(!shows && terminal && uishell_managed_daemon_backed(view)) { keep_previous = 1; }
  if(str8_match(view->string, str8_lit("placeholder"), 0)) { keep_previous = 0; }
  // A Cleat session is attached to, not started: its View is a new one.
  B32 in_place = !shows && terminal && str8_match(kind, view->string, 0) && !keep_previous &&
                 recipe.content != WHEELHOUSE_SLOT_CLEAT_SESSION;
  UIShell_ManagedPrepared prepared = {0};
  B32 ok = !in_place || uishell_managed_prepare(rd_view_state_from_view(rd_view_address_from_cfg(view), 0)->user_data, recipe.command, recipe.has_cwd, recipe.cwd, &prepared);
  char *error = 0;
  ok = ok && UIShell_StoreCall(state, wheelhouse_slot_valid(state->core, workspace, uishell_sidebar_text(key), content->token, &error));
  wheelhouse_string_free(error);
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
    RD_ViewAddress replaced = rd_view_address_from_cfg(view);
    CFG_Node *next = uishell_store_view_from_spec(owner, key, &recipe);
    // What the old View carried that is this device's: its label (its
    // Presentation State's), that it follows its provider, and the daemon
    // hosting a terminal.
    CFG_Node *label = cfg_node_child_from_string(rd_view_presentation_from_view(replaced), str8_lit("label"));
    String8 carried_label = push_str8_copy(arena, label->first->string);
    String8 carried[] = {str8_lit("resource_id"), str8_lit("follows_provider"), str8_lit("daemon"), str8_lit("daemon_name")};
    U64 carried_count = str8_match(next->string, str8_lit("terminal"), 0) ? ArrayCount(carried) : 2;
    for(U64 i = 0; i < carried_count; i++)
    {
      CFG_Node *c = cfg_node_child_from_string(view, carried[i]);
      if(c == &cfg_nil_node || cfg_node_child_from_string(next, carried[i]) != &cfg_nil_node) { continue; }
      cfg_node_insert_child(rd_state->cfg, next, next->last, cfg_node_deep_copy(rd_state->cfg, c));
    }
    RD_Arrangement *a = rd_arrangement_from_panel_owner(arena, uishell_store_owner(owner));
    RD_ArrangementPanel *panel = rd_arrangement_panel_from_view(a, replaced);
    RD_ViewAddress before = uishell_store_tab_before(a, replaced);
    B32 selected = rd_view_address_match(panel->selected, replaced);
    if(keep_previous)
    {
      // A Slot keeps one previous instance: an older one closes, with its
      // state and Presentation State, so the replaced View can take its
      // address.
      RD_ViewAddress previous = uishell_store_slot_view(owner, key, RD_ViewInstance_Previous);
      CFG_Node *older = rd_cfg_from_view(previous);
      if(older != &cfg_nil_node && older != view)
      {
        RD_ArrangementPanel *older_panel = rd_arrangement_panel_from_view(a, previous);
        if(rd_view_address_match(older_panel->selected, previous))
        {
          RD_ArrangementTab *t = rd_arrangement_tab_from_view(older_panel, previous);
          rd_arrangement_select(a, older_panel->id, t->prev ? t->prev->view : t->next ? t->next->view : rd_nil_view_address);
        }
        rd_arrangement_detach_tab(a, previous);
        rd_view_states_release_view(previous);
        rd_view_presentation_release(previous);
        cfg_node_release(rd_state->cfg, older);
      }
      // The instance it replaced keeps its state and Presentation State at
      // the previous instance's address.
      uishell_store_drop_setting(view, str8_lit("slot"));
      uishell_store_set_setting(view, str8_lit("previous_of"), key);
      for(U64 i = 0; i < ArrayCount(uishell_store_runtime_keys); i++)
      { uishell_store_drop_setting(view, uishell_store_runtime_keys[i]); }
      RD_ViewAddress kept = rd_view_address_from_cfg(view);
      rd_view_readdress(replaced, kept);
      rd_arrangement_readdress(a, replaced, kept);
    }
    else
    {
      // The instance it replaced loses them with its node.
      rd_arrangement_detach_tab(a, replaced);
      rd_view_states_release_view(replaced);
      rd_view_presentation_release(replaced);
      cfg_node_release(rd_state->cfg, view);
    }
    // The new View takes the Slot's address, where the replaced one was.
    RD_ViewAddress shown = rd_view_address_from_cfg(next);
    rd_arrangement_insert_tab(a, shown, panel->id, before, selected);
    rd_arrangement_set(a);
    if(label != &cfg_nil_node) { rd_view_setting_set(shown, str8_lit("label"), carried_label); }
    if(content->has_target) { rd_view_setting_set(shown, str8_lit("managed_target"), uishell_sidebar_string(content->target)); }
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
                       WheelhouseSlotContent *content, WheelhouseResolution *saved)
{
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  B32 ok = shown != &cfg_nil_node;
  if(ok && saved) { uishell_store_restore_resolution(shown, saved); }
  char *error = 0;
  UIShell_StoreCall(state, wheelhouse_slot_complete(state->core, workspace, uishell_sidebar_text(key), content->token, ok, &error));
  wheelhouse_string_free(error);
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

// Each of `owner`'s Views that is a Slot is planned, and what the core has
// for it applied.
internal void
uishell_store_plan_owner(UIShell_SidebarState *state, CFG_Node *owner)
{
  if(!state->core || cfg_node_child_from_string(owner, str8_lit("arrangement_presentation")) != &cfg_nil_node) { return; }
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  UIShell_StoreDoc doc = uishell_store_doc(arena, rd_arrangement_from_panel_owner(arena, uishell_store_owner(owner)));
  String8 *previous = push_array(arena, String8, 1);
  U64 previous_count = 0, previous_cap = 1;
  for(RD_ArrangementPanel *p = doc.arrangement->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(doc.arrangement->root, p))
  {
    for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
    {
      String8 of = uishell_store_previous_of(rd_cfg_from_view(t->view));
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
    WheelhouseSlotContent *plan = UIShell_StoreCall(state, wheelhouse_slot_plan(state->core, workspace, uishell_sidebar_text(key),
                                                                              uishell_sidebar_text(applied), &error));
    wheelhouse_string_free(error);
    if(!plan) { continue; }
    WheelhouseSlotContent content = *plan;
    WheelhouseResolution *saved = plan->has_saved ? &plan->saved : 0;
    String8 asks = str8_zero();
    // What the Slot Host is told: why the content isn't resolved, if it isn't.
    String8 content_state = content.state == WHEELHOUSE_CONTENT_UNAVAILABLE ? str8_lit("unavailable") :
                            content.state == WHEELHOUSE_CONTENT_HELD ? str8_lit("held") :
                            content.state == WHEELHOUSE_CONTENT_FAILED ? str8_lit("failed") : str8_zero();
    if(content.state == WHEELHOUSE_CONTENT_UPDATING)
    {
      // A rebind's policy is about the instance it replaces: a View that
      // applied nothing yet (just made, or after a restart) has none.
      UIShell_ViewSpec recipe = uishell_store_spec_from_core(arena, &content.recipe);
      if(applied.size && content.rebind == WHEELHOUSE_REBIND_ASK && !uishell_store_view_shows(arena, view, key, &recipe))
      { asks = uishell_store_recipe_text(arena, &recipe); }
      else
      {
        CFG_Node *shown = uishell_store_apply(state, owner, view, key, &content, applied.size && content.rebind == WHEELHOUSE_REBIND_KEEP_PREVIOUS);
        // Only a View starting without an instance tries a saved resolution.
        uishell_store_complete(state, owner, key, view, shown, &content, applied.size ? 0 : saved);
        if(shown != &cfg_nil_node) { view = shown; }
        else { content_state = str8_lit("failed"); }
      }
    }
    else if(content.state == WHEELHOUSE_CONTENT_CURRENT)
    {
      uishell_store_save_resolution(state, workspace, key, view, applied, saved);
    }
    // A View released while it was applied keeps no settings; its previous
    // instance is still released below.
    if(cfg_node_from_id(view->id) == view)
    {
      if(asks.size) { uishell_store_set_setting(view, str8_lit("asks"), asks); }
      else { uishell_store_drop_setting(view, str8_lit("asks")); }
      if(content_state.size) { uishell_store_set_setting(view, str8_lit("content_state"), content_state); }
      else { uishell_store_drop_setting(view, str8_lit("content_state")); }
    }
    // A previous instance the user closed, or none kept (content that
    // couldn't keep one), is released.
    if(content.has_previous && !uishell_store_key_in(key, previous, previous_count))
    {
      error = 0;
      UIShell_StoreCall(state, wheelhouse_slot_release_previous(state->core, workspace, uishell_sidebar_text(key), &error));
      wheelhouse_string_free(error);
    }
    wheelhouse_slot_plan_free(plan);
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
    if(view != &cfg_nil_node && entry->departed[i].flags != WHEELHOUSE_REBIND_REPLACE) { i++; continue; }
    if(view != &cfg_nil_node && cfg_node_from_id(view->id) == view)
    {
      RD_Arrangement *a = rd_arrangement_from_panel_owner(arena, uishell_store_owner(owner));
      rd_arrangement_remove_tab(a, rd_view_address_from_cfg(view));
      rd_arrangement_set(a);
    }
    // A failed release is reported; the next overlay read finds the slot
    // still departed, and this tries again.
    char *error = 0;
    uishell_sidebar_result(state, UIShell_StoreCall(state, wheelhouse_slot_release_previous(state->core, workspace, uishell_sidebar_text(key), &error)), error);
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
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  WheelhouseSlotContent *plan = UIShell_StoreCall(state, wheelhouse_slot_plan(state->core, workspace, uishell_sidebar_text(key),
    uishell_sidebar_text(push_str8_copy(scratch.arena, uishell_store_setting(view, str8_lit("applied")))), 0));
  WheelhouseSlotContent content = plan ? *plan : (WheelhouseSlotContent){0};
  if(plan && content.state == WHEELHOUSE_CONTENT_UPDATING)
  {
    if(accept)
    {
      CFG_Node *shown = uishell_store_apply(state, owner, view, key, &content, 0);
      uishell_store_complete(state, owner, key, view, shown, &content, 0);
    }
    else
    {
      UIShell_StoreCall(state, wheelhouse_slot_complete(state->core, workspace, uishell_sidebar_text(key), content.token, 0, 0));
      uishell_store_set_setting(view, str8_lit("declined"), str8_lit("1"));
      uishell_store_drop_setting(view, str8_lit("asks"));
    }
  }
  wheelhouse_slot_plan_free(plan);
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
  RD_Arrangement *a = rd_arrangement_from_panel_owner(scratch.arena, uishell_store_owner(owner));
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
  {
    for(RD_ArrangementTab *t = p->first_tab, *next = 0; t; t = next)
    {
      next = t->next;
      if(!str8_match(uishell_store_previous_of(rd_cfg_from_view(t->view)), key, 0)) { continue; }
      if(rd_view_address_match(p->selected, t->view)) { rd_arrangement_select(a, p->id, t->prev ? t->prev->view : next ? next->view : rd_nil_view_address); }
      rd_arrangement_remove_tab(a, t->view);
    }
  }
  rd_arrangement_set(a);
  UIShell_StoreCall(state, wheelhouse_slot_release_previous(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner)),
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
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  UIShell_StoreDoc doc = uishell_store_doc(scratch.arena, rd_arrangement_live(uishell_store_owner(owner)));
  for(U64 i = 0; i < doc.tab_count; i++)
  {
    CFG_Node *failed = cfg_node_child_from_string(doc.views[i], str8_lit("update_failed"));
    if(failed == &cfg_nil_node) { continue; }
    UIShell_StoreCall(state, wheelhouse_slot_retry(state->core, workspace, uishell_sidebar_text(doc.keys[i]), 0));
    cfg_node_release(rd_state->cfg, failed);
  }
  scratch_end(scratch);
}

//- The Workspace Overlay (ADR 0013)
//
// The user's edits of a workspace are the core's edit set: its Slots
// (above), its arrangement (soft and structural commits), and its name and
// mood, which are the workspace's label and theme here. A workspace marks
// the name and mood the core has (`named`, `mooded`; runtime-only), so the
// presentation file leaves them out. After each sync the overlay's flags are
// read for the notices: each Slot's (DETACHED, CHANGED, REMOVED, REBIND),
// the arrangement's (PROVIDER_CHANGED, SOFT, UNRESOLVED), and the slots
// tombstones hide.

read_only global String8 uishell_store_owner_runtime_keys[] = {str8_lit_comp("named"), str8_lit_comp("mooded")};

// Sends a label or theme the user changed as the workspace's name or mood,
// or takes the core's when only the core's changed (another host's edit,
// or a restart, which reads them from the workspace record). A workspace
// without the user's name is named by its subject, and follows it: its
// label isn't the user's until the user changes it.
internal void
uishell_store_sync_name(UIShell_SidebarState *state, CFG_Node *owner)
{
  if(str8_match(owner->string, str8_lit("window"), 0)) { return; }
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  if(!wheelhouse_workspace_registered(state->core, workspace, 0)) { return; }
  Temp scratch = scratch_begin(0, 0);
  WheelhouseOverlay *overlay = UIShell_StoreCall(state, wheelhouse_overlay_get(state->core, workspace, 0));
  if(overlay)
  {
    String8 settings[2] = {str8_lit("label"), str8_lit("theme")};
    B32 has_remote[2] = {overlay->has_name != 0, overlay->has_mood != 0};
    String8 remote[2] = {uishell_sidebar_string(overlay->name), uishell_sidebar_string(overlay->mood)};
    if(!has_remote[0] && overlay->has_subject_name)
    {
      has_remote[0] = 1;
      remote[0] = uishell_sidebar_string(overlay->subject_name);
    }
    for(U64 i = 0; i < 2; i++)
    {
      String8 mark_key = uishell_store_owner_runtime_keys[i];
      CFG_Node *mark = cfg_node_child_from_string(owner, mark_key);
      B32 has_mark = mark != &cfg_nil_node;
      String8 marked = push_str8_copy(scratch.arena, mark->first->string);
      String8 local = push_str8_copy(scratch.arena, uishell_store_setting(owner, settings[i]));
      // Never taken from the core: the user's, unless it is what the core
      // has (a subject workspace opens with its subject's name).
      B32 edited = has_mark ? !str8_match(local, marked, 0) : local.size != 0 && !(has_remote[i] && str8_match(local, remote[i], 0));
      B32 moved = has_remote[i] != has_mark || !str8_match(remote[i], marked, 0);
      if(!edited && moved && has_remote[i])
      {
        // The core's changed: it is the workspace's now.
        uishell_store_set_setting(owner, settings[i], remote[i]);
        uishell_store_set_setting(owner, mark_key, remote[i]);
      }
      else if(edited || (moved && local.size))
      {
        char *error = 0;
        B32 ok = i == 0 ? wheelhouse_workspace_set_name(state->core, workspace, local.size != 0, uishell_sidebar_text(local), &error) :
                          wheelhouse_workspace_set_mood(state->core, workspace, local.size != 0, uishell_sidebar_text(local), &error);
        state->store_calls += 1;
        if(uishell_sidebar_result(state, ok, error))
        {
          if(local.size) { uishell_store_set_setting(owner, mark_key, local); }
          else { uishell_store_drop_setting(owner, mark_key); }
        }
      }
    }
  }
  wheelhouse_overlay_free(overlay);
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

// Reads what the core says of each workspace's overlay, for the notices.
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
    WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(entry->id);
    WheelhouseSlots *slots = UIShell_StoreCall(state, wheelhouse_slots_get(state->core, workspace, 0));
    U64 count = slots ? slots->slot_count : 0;
    entry->flags = push_array(arena, UIShell_StoreFlag, count);
    for(U64 i = 0; i < count; i++)
    {
      WheelhouseSlot *slot = &slots->slots[i];
      if(!slot->flags) { continue; }
      entry->flags[entry->flag_count++] = (UIShell_StoreFlag){push_str8_copy(arena, uishell_sidebar_string(slot->key)), slot->flags};
    }
    WheelhouseWorkspaceArrangement *arrangement = slots ? UIShell_StoreCall(state, wheelhouse_arrangement_get(state->core, workspace, 0)) : 0;
    entry->arrangement_flags = arrangement ? arrangement->flags : 0;
    wheelhouse_arrangement_free(arrangement);
    WheelhouseOverlay *overlay = slots ? UIShell_StoreCall(state, wheelhouse_overlay_get(state->core, workspace, 0)) : 0;
    wheelhouse_slots_free(slots);
    entry->departed = 0;
    entry->departed_count = 0;
    if(overlay)
    {
      entry->tombstones = push_array(arena, String8, overlay->note_count);
      entry->departed = push_array(arena, UIShell_StoreFlag, overlay->note_count);
      for(U64 i = 0; i < overlay->note_count; i++)
      {
        WheelhouseOverlayNote *note = &overlay->notes[i];
        String8 key = push_str8_copy(arena, uishell_sidebar_string(note->key));
        if(note->kind == WHEELHOUSE_OVERLAY_TOMBSTONED) { entry->tombstones[entry->tombstone_count++] = key; }
        if(note->kind == WHEELHOUSE_OVERLAY_DEPARTED) { entry->departed[entry->departed_count++] = (UIShell_StoreFlag){key, note->rebind}; }
      }
    }
    wheelhouse_overlay_free(overlay);
  }
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

// Whether `view` is a View of `owner`'s first tab panel, which shows the
// workspace's own notices.
internal B32
uishell_store_view_leads(CFG_Node *owner, CFG_Node *view)
{
  RD_Arrangement *a = rd_arrangement_live(uishell_store_owner(owner));
  RD_ArrangementPanel *first = a->root;
  while(first != &rd_nil_arrangement_panel && first->first != &rd_nil_arrangement_panel) { first = first->first; }
  return first != &rd_nil_arrangement_panel && rd_arrangement_panel_from_view(a, rd_view_address_from_cfg(view)) == first;
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
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(id);
  WheelhouseWorkspaceArrangement *stored = UIShell_StoreCall(state, wheelhouse_arrangement_get(state->core, workspace, 0));
  if(stored)
  {
    char *error = 0;
    uint32_t result = UIShell_StoreCall(state, wheelhouse_arrangement_resolve(state->core, workspace, choice, stored->generation, 0, &error));
    uishell_sidebar_result(state, result != WHEELHOUSE_INVALID, error);
  }
  wheelhouse_arrangement_free(stored);
}

//- Workspace Layouts
//
// A subject whose kind has a template has layouts to choose between
// (docs/core/sidebar-design/workspace-layouts.md). Choosing one, or
// resetting it, moves the arrangement's generation, which the next sync
// follows; slots the layout leaves out are unplaced, and "Show …" places one.

internal void uishell_store_sync_owner(UIShell_SidebarState *state, CFG_Node *owner, B32 revision_moved);
internal void uishell_store_saw_live(UIShell_SidebarState *state, UIShell_StoreEntry *entry, CFG_Node *owner);

internal UIShell_SidebarState *
uishell_store_state_from_owner(CFG_Node *owner)
{
  if(owner == &cfg_nil_node) { return 0; }
  RD_WindowState *ws = rd_window_state_from_cfg__existing(rd_window_from_cfg(owner));
  return ws != &rd_nil_window_state && ws->sidebar && ws->sidebar->core ? ws->sidebar : 0;
}

internal UIShell_WorkspaceLayouts
uishell_store_layouts(Arena *arena, CFG_Node *owner)
{
  UIShell_WorkspaceLayouts result = {0};
  UIShell_SidebarState *state = uishell_store_state_from_owner(owner);
  if(!state) { return result; }
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  WheelhouseLayoutList *list = UIShell_StoreCall(state, wheelhouse_layouts_get(state->core, workspace, 0));
  if(list && list->count)
  {
    result.names = push_array(arena, String8, list->count);
    result.glyphs = push_array(arena, String8, list->count);
    result.count = list->count;
    result.current = result.fallback = list->count;
    for(U64 i = 0; i < list->count; i++)
    {
      result.names[i] = push_str8_copy(arena, uishell_sidebar_string(list->layouts[i].name));
      result.glyphs[i] = push_str8_copy(arena, uishell_sidebar_string(list->layouts[i].glyph));
      if(list->layouts[i].flags & WHEELHOUSE_LAYOUT_CURRENT) { result.current = i; }
      if(list->layouts[i].flags & WHEELHOUSE_LAYOUT_DEFAULT) { result.fallback = i; }
    }
  }
  wheelhouse_layouts_free(list);
  WheelhouseWorkspaceArrangement *stored = result.count ? UIShell_StoreCall(state, wheelhouse_arrangement_get(state->core, workspace, 0)) : 0;
  result.owned = stored && stored->owned;
  wheelhouse_arrangement_free(stored);
  return result;
}

// Shows layout `name` in `owner`, or (empty) its default. What this device
// hasn't committed goes first, so the layout it leaves keeps it.
internal void
uishell_store_choose_layout(CFG_Node *owner, String8 name)
{
  UIShell_SidebarState *state = uishell_store_state_from_owner(owner);
  if(!state) { return; }
  uishell_store_sync_owner(state, owner, 0);
  char *error = 0;
  B32 ok = UIShell_StoreCall(state, wheelhouse_layout_choose(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner)),
                                                             name.size != 0, uishell_sidebar_text(name), &error));
  uishell_sidebar_result(state, ok, error);
  rd_request_frame();
}

// Drops the current layout's arrangement edits: it follows its template again.
internal void
uishell_store_reset_layout(CFG_Node *owner)
{
  UIShell_SidebarState *state = uishell_store_state_from_owner(owner);
  if(!state) { return; }
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
  WheelhouseWorkspaceArrangement *stored = UIShell_StoreCall(state, wheelhouse_arrangement_get(state->core, workspace, 0));
  if(stored)
  {
    char *error = 0;
    uint32_t result = UIShell_StoreCall(state, wheelhouse_arrangement_resolve(state->core, workspace, WHEELHOUSE_ARRANGEMENT_FOLLOW, stored->generation, 0, &error));
    uishell_sidebar_result(state, result != WHEELHOUSE_INVALID, error);
  }
  wheelhouse_arrangement_free(stored);
  rd_request_frame();
}

// The slots `owner`'s layout leaves out, in slot order.
internal String8Array
uishell_store_unplaced(Arena *arena, CFG_Node *owner)
{
  String8List keys = {0};
  UIShell_SidebarState *state = uishell_store_state_from_owner(owner);
  WheelhouseWorkspaceArrangement *stored = state ? UIShell_StoreCall(state, wheelhouse_arrangement_get(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner)), 0)) : 0;
  for(U64 i = 0; stored && stored->has_layout && i < stored->note_count; i++)
  {
    if(stored->notes[i].kind == WHEELHOUSE_NOTE_UNPLACED)
    { str8_list_push(arena, &keys, push_str8_copy(arena, uishell_sidebar_string(stored->notes[i].key))); }
  }
  wheelhouse_arrangement_free(stored);
  return str8_array_from_list(arena, &keys);
}

// What "Show …" calls an unplaced slot: a template slot by its name.
internal String8
uishell_store_slot_title(String8 key)
{
  return str8_match(str8_prefix(key, 2), str8_lit("t:"), 0) ? str8_skip(key, 2) : key;
}

// Places unplaced slot `key` of `owner` as the selected tab of `panel`, or
// of its first tab panel: a commit that gives it a tab, then a follow,
// which builds its View. With `panel` 0 it goes to the panel the core
// names for it, the one its layout's matchers would give it.
internal void
uishell_store_show_slot(CFG_Node *owner, RD_PanelID panel, String8 key)
{
  UIShell_SidebarState *state = uishell_store_state_from_owner(owner);
  if(!state || !key.size) { return; }
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  key = push_str8_copy(arena, key);
  uishell_store_sync_owner(state, owner, 0);
  UIShell_WorkspaceId id = uishell_workspace_id_from_cfg(owner);
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(id);
  RD_Arrangement *a = rd_arrangement_live(uishell_store_owner(owner));
  UIShell_StoreDoc doc = uishell_store_doc(arena, a);
  WheelhouseWorkspaceArrangement *stored = UIShell_StoreCall(state, wheelhouse_arrangement_get(state->core, workspace, 0));
  String8 home = str8_zero();
  for(U64 i = 0; stored && !panel && i < stored->note_count; i++)
  {
    WheelhouseNote *note = &stored->notes[i];
    if(note->kind == WHEELHOUSE_NOTE_UNPLACED && str8_match(uishell_sidebar_string(note->key), key, 0))
    { home = uishell_sidebar_string(note->panel); }
  }
  // The target: `panel`, or the home panel, if it holds tabs, else the
  // first that does.
  U64 target = doc.panel_count, index = 0;
  for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p), index++)
  {
    if(doc.panels[index].kind != WHEELHOUSE_PANEL_TABS) { continue; }
    B32 wanted = panel ? p->id == panel : home.size && str8_match(uishell_sidebar_string(doc.panels[index].id), home, 0);
    if(target == doc.panel_count || wanted) { target = index; }
    if(wanted) { break; }
  }
  if(stored && target < doc.panel_count && !uishell_store_key_in(key, doc.keys, doc.tab_count))
  {
    WheelhouseDocPanel *out = &doc.panels[target];
    WheelhouseText *tabs = push_array(arena, WheelhouseText, out->tab_count+1);
    MemoryCopy(tabs, out->tabs, sizeof(*tabs)*out->tab_count);
    tabs[out->tab_count] = uishell_sidebar_text(key);
    out->tabs = tabs;
    out->selected = out->tab_count;
    out->tab_count += 1;
    UIShell_StoreEntry *entry = uishell_store_entry(state, id);
    B32 marked = cfg_node_child_from_string(owner, str8_lit("arrangement_generation")) != &cfg_nil_node;
    U64 expected = marked && entry->generation ? entry->generation : stored->generation;
    WheelhouseArrangementDoc commit = {doc.panels, doc.panel_count};
    char *error = 0;
    WheelhouseWorkspaceArrangement *after = 0;
    // Stale, as in uishell_store_sync_owner: committing at the new
    // generation reconciles the core's change again, keeping both.
    for(U32 attempt = 0; attempt < 2; attempt++)
    {
      wheelhouse_arrangement_free(after);
      after = UIShell_StoreCall(state, wheelhouse_arrangement_commit(state->core, workspace, &commit, expected, &error));
      state->store_commits += 1;
      if(!after || after->status != WHEELHOUSE_STALE) { break; }
      expected = after->generation;
    }
    if(after && after->status == WHEELHOUSE_COMMITTED)
    {
      entry->generation = after->generation;
      uishell_store_set_setting(owner, str8_lit("arrangement_generation"), push_str8f(arena, "%I64u", after->generation));
      uishell_store_follow(state, owner, id, &entry->generation);
      uishell_store_saw_live(state, entry, owner);
    }
    else
    { uishell_sidebar_set_error(state, error ? str8_cstring(error) : str8_lit("The workspace's arrangement changed; try again")); }
    wheelhouse_string_free(error);
    wheelhouse_arrangement_free(after);
  }
  wheelhouse_arrangement_free(stored);
  scratch_end(scratch);
  rd_request_frame();
}

//- Tool strip
//
// What the strip at the workspace's edge lists (docs/design/subject-workspaces.md,
// "Tool strip"): the workspace's slots, placed or not, and what its subject
// kind's `strip` lists, the related workspaces and linked items.

internal UIShell_StripViews
uishell_store_strip_views(Arena *arena, CFG_Node *owner)
{
  UIShell_StripViews result = {0};
  UIShell_SidebarState *state = uishell_store_state_from_owner(owner);
  if(!state) { return result; }
  WheelhouseSlots *slots = UIShell_StoreCall(state, wheelhouse_slots_get(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner)), 0));
  if(slots && slots->slot_count)
  {
    Temp scratch = scratch_begin(&arena, 1);
    UIShell_StoreDoc doc = uishell_store_doc(scratch.arena, rd_arrangement_live(uishell_store_owner(owner)));
    result.v = push_array(arena, UIShell_StripView, slots->slot_count);
    for(U64 i = 0; i < slots->slot_count; i++)
    {
      WheelhouseSlot *slot = &slots->slots[i];
      if(slot->flags & WHEELHOUSE_SLOT_FLAG_REMOVED) { continue; }
      UIShell_StripView *out = &result.v[result.count++];
      out->key = push_str8_copy(arena, uishell_sidebar_string(slot->key));
      out->role = slot->spec.has_role ? push_str8_copy(arena, uishell_sidebar_string(slot->spec.role)) : str8_zero();
      out->presentation = slot->spec.has_presentation ? push_str8_copy(arena, uishell_sidebar_string(slot->spec.presentation)) : str8_zero();
      out->view = &cfg_nil_node;
      for(U64 t = 0; t < doc.tab_count; t++)
      { if(str8_match(doc.keys[t], out->key, 0)) { out->view = doc.views[t]; break; } }
    }
    scratch_end(scratch);
  }
  wheelhouse_slots_free(slots);
  return result;
}

internal WheelhouseEntity
uishell_store_subject(Arena *arena, CFG_Node *owner)
{
  if(owner == &cfg_nil_node || !uishell_workspace_cfg_has_subject(owner)) { return (WheelhouseEntity){0}; }
  String8 kind = cfg_node_child_from_string(owner, str8_lit("sidebar_entity_kind"))->first->string;
  String8 id = cfg_node_child_from_string(owner, str8_lit("sidebar_entity_id"))->first->string;
  return uishell_sidebar_entity(push_str8_copy(arena, uishell_workspace_cfg_subject_provider(owner)), push_str8_copy(arena, kind), push_str8_copy(arena, id));
}

internal UIShell_StripEntries
uishell_store_strip_entries(Arena *arena, CFG_Node *owner)
{
  UIShell_StripEntries result = {0};
  UIShell_SidebarState *state = uishell_store_state_from_owner(owner);
  WheelhouseEntity subject = state ? uishell_store_subject(arena, owner) : (WheelhouseEntity){0};
  if(!subject.kind.len) { return result; }
  WheelhouseStrip *strip = UIShell_StoreCall(state, wheelhouse_strip_get(state->core, subject, 0));
  if(strip && strip->count)
  {
    result.v = push_array(arena, UIShell_StripEntry, strip->count);
    result.count = strip->count;
    for(U64 i = 0; i < strip->count; i++)
    {
      result.v[i].section = strip->entries[i].section;
      result.v[i].entity = uishell_sidebar_card_entity_copy(arena, strip->entries[i].entity);
    }
  }
  wheelhouse_strip_free(strip);
  return result;
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
  B32 ok = UIShell_StoreCall(state, wheelhouse_slot_reattach(state->core, uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner)),
                                                             uishell_sidebar_text(key), &error));
  // It follows its provider again; its next plan shows the provider's
  // content. A slot a tombstone hid comes back at the next commit, which
  // places it.
  if(uishell_sidebar_result(state, ok, error))
  {
    uishell_store_mark_follows(view, 1);
    UIShell_StoreEntry *entry = uishell_store_entry_find(state, uishell_workspace_id_from_cfg(owner));
    if(entry) { entry->version = 0; }
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
  String8 taken[UISHELL_STORE_TAKEN_MAX];
  U64 taken_count = 0;
  Temp scratch = scratch_begin(0, 0);
  if(state && key.size && uishell_store_spec_from_view(scratch.arena, view, &spec, taken, &taken_count))
  {
    WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(uishell_workspace_id_from_cfg(owner));
    WheelhouseSlots *slots = UIShell_StoreCall(state, wheelhouse_slots_get(state->core, workspace, 0));
    WheelhouseSlot slot = {0};
    B32 exists = uishell_store_slot_find(slots, key, &slot);
    U32 rebind = exists ? slot.rebind : WHEELHOUSE_REBIND_REPLACE;
    WheelhouseViewSpec out = uishell_store_core_spec(scratch.arena, &spec, exists ? &slot.spec : 0);
    char *error = 0;
    uishell_sidebar_result(state, UIShell_StoreCall(state, wheelhouse_slot_set(state->core, workspace, uishell_sidebar_text(key), &out, rebind, &error)), error);
    wheelhouse_slots_free(slots);
  }
  scratch_end(scratch);
}

// Closes `view`'s tab: its Slot goes, a provider's as a tombstone.
internal void
uishell_store_remove(CFG_Node *view)
{
  RD_PanelAddress panel = rd_panel_from_cfg(view);
  if(panel.id == 0) { return; }
  UIShell_RegsScope(.window = rd_window_from_cfg(view)->id, .panel = panel, .view = rd_view_address_from_cfg(view), .tab = rd_view_address_from_cfg(view))
  { uishell_cmd("close_tab"); }
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
  return rd_cfg_from_view(uishell_store_slot_view(owner, key, RD_ViewInstance_Previous)) != &cfg_nil_node;
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
  if(flags & WHEELHOUSE_SLOT_FLAG_REMOVED)
  {
    UIShell_SlotNoticePush(.badge = str8_lit("removed"), .text = str8_lit("Its provider removed it; it is kept as yours."),
                           .actions = {UIShell_SlotAction_Remove});
  }
  else if((flags & WHEELHOUSE_SLOT_FLAG_CHANGED) && (flags & WHEELHOUSE_SLOT_FLAG_DETACHED))
  {
    UIShell_SlotNoticePush(.badge = str8_lit("changed"), .text = str8_lit("Its provider changed it since you edited it."),
                           .actions = {UIShell_SlotAction_Reattach, UIShell_SlotAction_KeepMine});
  }
  else if(flags & WHEELHOUSE_SLOT_FLAG_CHANGED)
  {
    UIShell_SlotNoticePush(.badge = str8_lit("changed"), .text = str8_lit("You closed it; its provider has put new content here since."),
                           .actions = {UIShell_SlotAction_Reattach, UIShell_SlotAction_Remove});
  }
  else if(flags & WHEELHOUSE_SLOT_FLAG_DETACHED)
  {
    UIShell_SlotNoticePush(.badge = str8_lit("edited"), .text = str8_lit("You changed its content, so it no longer follows its provider."),
                           .actions = {UIShell_SlotAction_Reattach});
  }
  if(flags & WHEELHOUSE_SLOT_FLAG_REBIND) { UIShell_SlotNoticePush(.badge = str8_lit("rebind")); }
  if(key.size && uishell_store_departed(state, owner, key))
  {
    UIShell_SlotNoticePush(.badge = str8_lit("removed"), .text = str8_lit("Its provider removed this View; it stays until you close it."),
                           .actions = {UIShell_SlotAction_Remove});
  }
  // A live instance whose content its provider no longer resolves stays as
  // it is, marked unverified (shell_core.c, "Slot Host").
  if(key.size && uishell_store_slot_wait(uishell_store_setting(view, str8_lit("content_state"))) == WH_SlotWait_Unavailable &&
     (uishell_store_setting(view, str8_lit("applied")).size || uishell_store_setting(view, str8_lit("session")).size))
  {
    // A badge, without a banner: a banner would resize the content each time
    // the resolution comes and goes.
    UIShell_SlotNoticePush(.badge = str8_lit("unverified"));
  }
  U32 arrangement = uishell_store_arrangement_flags(state, owner);
  if((arrangement & (WHEELHOUSE_ARRANGEMENT_FLAG_PROVIDER_CHANGED|WHEELHOUSE_ARRANGEMENT_FLAG_UNRESOLVED)) && uishell_store_view_leads(owner, view))
  {
    if(arrangement & WHEELHOUSE_ARRANGEMENT_FLAG_PROVIDER_CHANGED)
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
    case UIShell_SlotAction_KeepLayout: { uishell_store_resolve_arrangement(view, WHEELHOUSE_ARRANGEMENT_KEEP); } break;
    case UIShell_SlotAction_FollowLayout: { uishell_store_resolve_arrangement(view, WHEELHOUSE_ARRANGEMENT_FOLLOW); } break;
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

// `doc`, of `owner`'s arrangement as it is now, is what this device last
// committed or followed for `entry`.
internal void
uishell_store_saw(UIShell_SidebarState *state, UIShell_StoreEntry *entry, CFG_Node *owner, UIShell_StoreDoc *doc)
{
  entry->version = rd_arrangement_version(uishell_store_owner(owner));
  entry->specs = uishell_store_specs(doc);
  if(doc->tab_count > entry->seen_capacity)
  {
    entry->seen_capacity = Max(16, doc->tab_count*2);
    entry->seen = push_array_no_zero(state->store_arena, U64, entry->seen_capacity);
  }
  for(U64 i = 0; i < doc->tab_count; i++) { entry->seen[i] = uishell_sidebar_hash_text(5381, doc->keys[i]); }
  entry->seen_count = doc->tab_count;
}

// As uishell_store_saw, of `owner`'s arrangement as it is now.
internal void
uishell_store_saw_live(UIShell_SidebarState *state, UIShell_StoreEntry *entry, CFG_Node *owner)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_StoreDoc doc = uishell_store_doc(scratch.arena, rd_arrangement_live(uishell_store_owner(owner)));
  uishell_store_saw(state, entry, owner, &doc);
  scratch_end(scratch);
}

internal B32
uishell_store_has_seen(UIShell_StoreEntry *entry, String8 key)
{
  U64 hash = uishell_sidebar_hash_text(5381, key);
  for(U64 i = 0; i < entry->seen_count; i++) { if(entry->seen[i] == hash) { return 1; } }
  return 0;
}

// The window's workspaces with an arrangement: its own layout once it is
// one, and its open and kept workspaces.
internal CFG_NodePtrList
uishell_store_owners(Arena *arena, CFG_Node *window)
{
  CFG_NodePtrList owners = {0};
  B32 own = cfg_node_child_from_string(window, str8_lit("workspace_id")) != &cfg_nil_node &&
    rd_arrangement_live(uishell_store_owner(window))->root != &rd_nil_arrangement_panel;
  if(own || cfg_node_child_from_string(window, str8_lit("arrangement_generation")) != &cfg_nil_node ||
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
// Nothing reaches the core until it ends.
internal B32
uishell_store_gesture_in_flight(void)
{
  return rd_drag_is_active() || rd_state->boundary_resize.arena != 0;
}

// Commits `owner`'s arrangement if it was set, or its Views' specs changed,
// since it was last committed or followed, then follows anything the core
// placed; else, if the core moved the document itself, follows it.
internal void
uishell_store_sync_owner(UIShell_SidebarState *state, CFG_Node *owner, B32 revision_moved)
{
  // Not followed yet: committing would replace what the core keeps.
  if(cfg_node_child_from_string(owner, str8_lit("arrangement_presentation")) != &cfg_nil_node) { return; }
  uishell_store_sync_name(state, owner);
  Temp scratch = scratch_begin(0, 0);
  Arena *arena = scratch.arena;
  UIShell_WorkspaceId id = uishell_workspace_id_from_cfg(owner);
  WheelhouseWorkspaceId workspace = uishell_sidebar_workspace(id);
  RD_Arrangement *a = rd_arrangement_from_panel_owner(arena, uishell_store_owner(owner));
  UIShell_StoreDoc doc = uishell_store_doc(arena, a);
  B32 keyed = 1;
  for(U64 i = 0; i < doc.tab_count && keyed; i++)
  { keyed = uishell_store_key_valid(doc.keys[i]) && !uishell_store_key_in(doc.keys[i], doc.keys, i); }
  UIShell_StoreEntry *entry = uishell_store_entry(state, id);
  B32 marked = cfg_node_child_from_string(owner, str8_lit("arrangement_generation")) != &cfg_nil_node;
  B32 changed = !keyed || entry->version != rd_arrangement_version(uishell_store_owner(owner)) || entry->specs != uishell_store_specs(&doc);
  // An empty workspace never committed has nothing to keep.
  if(changed && !marked && doc.panel_count == 0) { changed = 0; uishell_store_saw(state, entry, owner, &doc); }
  if(!changed && !revision_moved) { scratch_end(scratch); return; }
  WheelhouseSlots *slots = UIShell_StoreCall(state, wheelhouse_slots_get(state->core, workspace, 0));
  WheelhouseWorkspaceArrangement *stored = slots ? UIShell_StoreCall(state, wheelhouse_arrangement_get(state->core, workspace, 0)) : 0;
  // A workspace the core doesn't know yet (a subject's registers once it
  // has opened) is committed when it does.
  if(!stored)
  {
    wheelhouse_slots_free(slots);
    scratch_end(scratch);
    return;
  }
  // A workspace just opened on its subject (`arrangement_fresh`) takes the
  // arrangement its Suggested Layout suggests, keeping the View it opened
  // with; one with no suggestion keeps the arrangement it opened with. A
  // baseline of the primary alone (every workspace subject has one) suggests
  // nothing, but a Workspace Layout arranges even that, unless the workspace
  // opened with more than its primary.
  CFG_Node *fresh = cfg_node_child_from_string(owner, str8_lit("arrangement_fresh"));
  if(fresh != &cfg_nil_node)
  {
    cfg_node_release(rd_state->cfg, fresh);
    WheelhouseOverlay *overlay = UIShell_StoreCall(state, wheelhouse_overlay_get(state->core, workspace, 0));
    B32 suggests = (overlay && overlay->has_baseline && !overlay->primary_only) || (stored->has_layout && doc.tab_count <= 1);
    wheelhouse_overlay_free(overlay);
    if(stored->doc.panel_count && suggests)
    {
      if(uishell_store_assign_keys(arena, owner, &doc, slots)) { rd_arrangement_set(a); }
      wheelhouse_arrangement_free(stored);
      wheelhouse_slots_free(slots);
      uishell_store_follow(state, owner, id, &entry->generation);
      uishell_store_saw_live(state, entry, owner);
      scratch_end(scratch);
      return;
    }
  }
  if(!changed)
  {
    // The core moved the document itself.
    if(entry->generation != stored->generation && marked)
    {
      uishell_store_follow(state, owner, id, &entry->generation);
      uishell_store_saw_live(state, entry, owner);
    }
    wheelhouse_arrangement_free(stored);
    wheelhouse_slots_free(slots);
    scratch_end(scratch);
    return;
  }
  if(uishell_store_assign_keys(arena, owner, &doc, slots)) { rd_arrangement_set(a); }

  // New and changed slots. A slot following its provider is the provider's,
  // and a placeholder's keeps the spec it was made from.
  B32 ok = 1;
  for(U64 i = 0; i < doc.tab_count && ok; i++)
  {
    UIShell_ViewSpec spec = {0};
    String8 taken[UISHELL_STORE_TAKEN_MAX];
    U64 taken_count = 0;
    if(!uishell_store_spec_from_view(arena, doc.views[i], &spec, taken, &taken_count)) { continue; }
    WheelhouseSlot slot = {0};
    B32 exists = uishell_store_slot_find(slots, doc.keys[i], &slot);
    // The provider's own, or a facet's, whose View shows what its plan
    // resolved: the slot's spec is not the View's to set.
    B32 follows = exists && ((slot.in_baseline && !slot.detached) || slot.spec.content == WHEELHOUSE_SLOT_FACET);
    // Unless the user changed what it showed since it applied the provider's:
    // that is an override, which detaches it.
    String8 applied_spec = uishell_store_setting(doc.views[i], str8_lit("applied_spec"));
    if(follows && applied_spec.size &&
       !str8_match(applied_spec, push_str8f(arena, "%I64x", uishell_store_view_spec_hash(doc.views[i])), 0)) { follows = 0; }
    uishell_store_mark_follows(doc.views[i], follows);
    if(follows) { continue; }
    UIShell_ViewSpec current = exists ? uishell_store_spec_from_core(arena, &slot.spec) : (UIShell_ViewSpec){0};
    if(exists && uishell_store_spec_match(&spec, &current)) { continue; }
    // An override keeps the provider's rebind policy: changing the content
    // isn't changing the policy.
    WheelhouseViewSpec out = uishell_store_core_spec(arena, &spec, exists ? &slot.spec : 0);
    char *error = 0;
    ok = uishell_sidebar_result(state, UIShell_StoreCall(state, wheelhouse_slot_set(state->core, workspace, uishell_sidebar_text(doc.keys[i]),
                                                                                    &out, exists ? slot.rebind : WHEELHOUSE_REBIND_REPLACE, &error)), error);
  }

  // Slots whose tabs the user closed go first, so the document no longer
  // names them: the user's own are removed, and a provider's is tombstoned,
  // hidden, while its provider keeps it (ADR 0013). They must go before the
  // commit, or its reconcile would place them again. A slot the stored
  // document tabs that this device never saw (placed since) stays. In a
  // Workspace Layout closing a tab only unplaces its slot: the commit
  // without the tab says so, and "Show …" places it again.
  String8List stored_keys = {0};
  for(U64 i = 0; i < stored->doc.panel_count && !stored->has_layout; i++)
  {
    WheelhouseDocPanel *p = &stored->doc.panels[i];
    for(U64 t = 0; t < p->tab_count; t++) { str8_list_push(arena, &stored_keys, push_str8_copy(arena, uishell_sidebar_string(p->tabs[t]))); }
  }
  for(String8Node *n = stored_keys.first; ok && n; n = n->next)
  {
    String8 key = n->string;
    if(!uishell_store_has_seen(entry, key)) { continue; }
    if(uishell_store_key_in(key, doc.keys, doc.tab_count) || !uishell_store_slot_find(slots, key, 0)) { continue; }
    char *remove_error = 0;
    uishell_sidebar_result(state, UIShell_StoreCall(state, wheelhouse_slot_remove(state->core, workspace, uishell_sidebar_text(key), &remove_error)), remove_error);
  }

  // The document, at the generation this device last saw.
  U64 expected = marked && entry->generation ? entry->generation : stored->generation;
  wheelhouse_arrangement_free(stored);
  WheelhouseArrangementDoc out = {doc.panels, doc.panel_count};
  WheelhouseWorkspaceArrangement *after = 0;
  char *error = 0;
  for(U32 attempt = 0; ok && attempt < 2; attempt++)
  {
    error = 0;
    wheelhouse_arrangement_free(after);
    after = UIShell_StoreCall(state, wheelhouse_arrangement_commit(state->core, workspace, &out, expected, &error));
    state->store_commits += 1;
    if(!after || after->status != WHEELHOUSE_STALE) { break; }
    // Stale: the core reconciled a provider's change since. Committing at
    // the new generation reconciles it again, keeping both.
    expected = after->generation;
  }
  B32 committed = ok && after && after->status == WHEELHOUSE_COMMITTED;
  if(ok && !committed)
  { uishell_sidebar_set_error(state, error ? str8_cstring(error) : str8_lit("The workspace's arrangement was not committed")); }
  wheelhouse_string_free(error);
  wheelhouse_slots_free(slots);
  // Tried: not tried again until it changes.
  entry->version = rd_arrangement_version(uishell_store_owner(owner));
  entry->specs = uishell_store_specs(&doc);
  if(committed)
  {
    uishell_store_saw(state, entry, owner, &doc);
    entry->generation = after->generation;
    uishell_store_set_setting(owner, str8_lit("arrangement_generation"), push_str8f(arena, "%I64u", after->generation));
    // Anything the core placed (slots the document had no tab for) joins
    // the arrangement.
    B32 placed = 0;
    for(U64 i = 0; i < after->note_count; i++) { placed |= after->notes[i].kind == WHEELHOUSE_NOTE_PLACED; }
    if(placed)
    {
      uishell_store_follow(state, owner, id, &entry->generation);
      uishell_store_saw_live(state, entry, owner);
    }
  }
  wheelhouse_arrangement_free(after);
  scratch_end(scratch);
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
  U64 revision = UIShell_StoreCall(state, wheelhouse_workspace_content_revision(state->core, &error));
  wheelhouse_string_free(error);
  B32 revision_moved = state->store_synced && revision != state->store_content_revision;
  // A new snapshot can mean a workspace the core didn't know has registered
  // (a subject's, once it opened), which then commits. An arrangement set,
  // or a View's config edited, can be a change to commit.
  if(state->store_synced && cfg_change_gen() == state->store_cfg_gen && state->snapshot_count == state->store_snapshot_count &&
     !revision_moved && rd_state->arrangement_version == state->store_arrangement_version)
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
    U64 version = rd_arrangement_version(uishell_store_owner(owner));
    UIShell_StoreDoc before = uishell_store_doc(scratch.arena, rd_arrangement_live(uishell_store_owner(owner)));
    U64 specs = uishell_store_specs(&before);
    uishell_store_plan_owner(state, owner);
    // What an update changed is the core's already: no commit follows it.
    if(entry->version == version && entry->specs == specs) { uishell_store_saw_live(state, entry, owner); }
  }
  state->store_synced = 1;
  state->store_cfg_gen = cfg_change_gen();
  state->store_arrangement_version = rd_state->arrangement_version;
  state->store_snapshot_count = state->snapshot_count;
  state->store_content_revision = UIShell_StoreCall(state, wheelhouse_workspace_content_revision(state->core, 0));
  scratch_end(scratch);
}

// Once the window's records are imported: each workspace saved with this
// device's part of its arrangement follows the core's.
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
    if(uishell_store_follow(state, owner, id, &entry->generation)) { uishell_store_saw_live(state, entry, owner); }
  }
  // Names and moods too, which the presentation file leaves to the core.
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next) { uishell_store_sync_name(state, n->v); }
  scratch_end(scratch);
}

internal void
uishell_workspace_store_release(UIShell_SidebarState *state)
{
  if(state->store_arena) { arena_release(state->store_arena); }
  if(state->store_overlay_arena) { arena_release(state->store_overlay_arena); }
  state->store_arena = state->store_overlay_arena = 0;
  state->store_entries = 0;
  state->store_entry_count = state->store_entry_capacity = 0;
  state->store_synced = 0;
}

//- Device-local

// `window` as the presentation file holds it: a workspace's Views are left
// out, and when the core keeps its arrangement, this device's part of it is
// `arrangement_presentation`.
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
  // Views a workspace that isn't an arrangement's owner (yet) has are none
  // of these.
  for(CFG_Node *c = copy->first; c != &cfg_nil_node; c = c->next)
  {
    if(str8_match(c->string, str8_lit("workspace"), 0) || str8_match(c->string, str8_lit("detached_workspace"), 0))
    { cfg_node_release(copy_state, cfg_node_child_from_string(c, RD_VIEW_NAMESPACE_WORKSPACE)); }
  }
  cfg_node_release(copy_state, cfg_node_child_from_string(copy, RD_VIEW_NAMESPACE_WORKSPACE));
  U64 owner_index = 0;
  for(CFG_NodePtrNode *n = owners.first; n; n = n->next, owner_index++)
  {
    CFG_Node *owner = n->v;
    CFG_Node *dst = copies[owner_index];
    // A name or mood the core has is its (`named`, `mooded`).
    {
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
    // One the core doesn't keep yet keeps what this device had of it, if it
    // has not followed the core's since it was read.
    if(cfg_node_child_from_string(owner, str8_lit("arrangement_generation")) == &cfg_nil_node) { continue; }
    cfg_node_release(copy_state, cfg_node_child_from_string(dst, str8_lit("arrangement_presentation")));
    CFG_Node *device = cfg_node_new(copy_state, dst, str8_lit("arrangement_presentation"));
    RD_Arrangement *a = rd_arrangement_live(uishell_store_owner(owner));
    for(RD_ArrangementPanel *p = a->root; p != &rd_nil_arrangement_panel; p = rd_arrangement_next(a->root, p))
    {
      // The number a provider's panel has here.
      if(p->doc_id.size)
      {
        CFG_Node *entry = cfg_node_new(copy_state, device, str8_lit("panel"));
        cfg_node_new(copy_state, cfg_node_new(copy_state, entry, str8_lit("id")), push_str8f(scratch.arena, "%I64u", p->id));
        cfg_node_new(copy_state, cfg_node_new(copy_state, entry, str8_lit("doc_id")), p->doc_id);
      }
      for(RD_ArrangementTab *t = p->first_tab; t; t = t->next)
      {
        CFG_Node *view = rd_cfg_from_view(t->view);
        String8 key = uishell_store_setting(view, str8_lit("slot"));
        // A Slot's previous instance isn't in the core's document: this
        // device keeps it whole, without what is this run's.
        if(!key.size && uishell_store_previous_of(view).size)
        {
          CFG_Node *kept = cfg_node_deep_copy(copy_state, view);
          for(CFG_Node *c = kept->first, *next = 0; c != &cfg_nil_node; c = next)
          {
            next = c->next;
            if(uishell_store_key_in(c->string, uishell_store_runtime_keys, ArrayCount(uishell_store_runtime_keys)))
            { cfg_node_release(copy_state, c); }
          }
          CFG_Node *entry = cfg_node_new(copy_state, device, str8_lit("previous"));
          cfg_node_insert_child(copy_state, entry, entry->last, kept);
        }
        if(!key.size) { continue; }
        UIShell_ViewSpec spec = {0};
        String8 taken[UISHELL_STORE_TAKEN_MAX];
        U64 taken_count = 0;
        uishell_store_spec_from_view(scratch.arena, view, &spec, taken, &taken_count);
        if(cfg_node_child_from_string(view, str8_lit("follows_provider")) != &cfg_nil_node) { taken_count = 0; }
        // A daemon session or page the core has saved is its.
        String8 portable = uishell_store_setting(view, str8_lit("portable"));
        B32 saved = portable.size && str8_match(portable, uishell_store_view_portable_text(scratch.arena, view), 0);
        CFG_Node *groups[2] = {&cfg_nil_node, &cfg_nil_node};
        for(CFG_Node *c = view->first; c != &cfg_nil_node; c = c->next)
        {
          if(str8_match(c->string, str8_lit("slot"), 0) ||
             uishell_store_key_in(c->string, taken, taken_count) ||
             uishell_store_key_in(c->string, uishell_store_runtime_keys, ArrayCount(uishell_store_runtime_keys)) ||
             (saved && (str8_match(c->string, str8_lit("session"), 0) || str8_match(c->string, str8_lit("daemon_name"), 0) ||
                        str8_match(c->string, str8_lit("web_url"), 0)))) { continue; }
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
