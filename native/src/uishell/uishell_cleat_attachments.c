////////////////////////////////
//~ uishell: Cleat connections and attachments (#417)
//
// A Connection is one link to a Cleat daemon over a route (the local socket,
// or ssh to `cleat proxy` on a host): one provider, carrying many sessions.
// An Attachment is Wheelhouse's subscription to one session over a
// connection: one update stream. A terminal View showing a directly attached
// session (a `cleat_session` endpoint, #104) holds a reference to the
// session's attachment and opens its own Cleat view on it, with its own role,
// size and history position (Cleat ADR 0009). Views of one session share its
// attachment; attachments on one daemon share its connection.
//
// - Connections are keyed by route and daemon and counted by their users:
//   attachments, and daemon-backed Views that keep their own session handle.
//   One with no users lingers (uishell_cleat_connection_linger_us) and then
//   closes. A command transport isn't respawned by Cleat: when one that has
//   streamed fails, the connection is reopened and its attachments
//   re-attached; one that fails before streaming gives up until the user
//   reconnects.
// - Attachments are counted by Views and closed at the end of the frame their
//   count reaches zero (uishell_cleat_attachments_end_frame), so a View
//   released and another acquired in one frame keeps the stream. Each attaches
//   with no default view (CLEAT_ROLE_NO_VIEW); a daemon that predates views
//   fails the attach, and its Views say so.
// - The role policy maps an attachment's Views to Cleat roles.
//
// Everything here runs on the UI thread. Providers only wake it.

internal void
uishell_terminal_provider_wake(void *user_data)
{
  (void)user_data;
  wm_send_wakeup_event();
}

#define UISHELL_CLEAT_PROVIDER_FEATURES (CLEAT_PROVIDER_FEATURE_CELL_SNAPSHOTS|CLEAT_PROVIDER_FEATURE_STRUCTURED_MOUSE_INPUT|\
                                         CLEAT_PROVIDER_FEATURE_RENDER_UPDATES|CLEAT_PROVIDER_FEATURE_IMAGE_STATE)

// Where a daemon is: `ssh` reaches `host` (running `command`, the Cleat
// there, when it isn't the one on the remote PATH); `local` is this host's
// socket. Runtime root and daemon name select the daemon; empty is Cleat's
// default.
typedef struct UIShell_CleatRoute UIShell_CleatRoute;
struct UIShell_CleatRoute
{
  B32 ssh;
  String8 host;
  String8 command;
  String8 runtime_root;
  String8 daemon;
};

typedef struct UIShell_CleatConnection UIShell_CleatConnection;
typedef struct UIShell_CleatAttachment UIShell_CleatAttachment;
typedef struct UIShell_CleatViewRef UIShell_CleatViewRef;

struct UIShell_CleatConnection
{
  UIShell_CleatConnection *next;
  Arena *arena;
  UIShell_CleatRoute route;
  cleat_provider *provider;
  U64 users;
  U64 idle_since_us;
  // Times its provider has been opened: a reopen starts a new connection
  // under the same route, and Views log its warning and give-up again.
  U64 opens;
  // Since this open, some view of it streamed.
  B32 streamed;
  // A command transport that failed before streaming: down until reconnect.
  B32 given_up;
  B32 reconnect;
};

struct UIShell_CleatAttachment
{
  UIShell_CleatAttachment *next;
  Arena *arena;
  UIShell_CleatConnection *connection;
  String8 session_id;
  cleat_session_colors colors;
  cleat_session *session;
  // Times it has attached: once, and again with each reopen of its connection.
  U64 attaches;
  UIShell_CleatViewRef *first_ref, *last_ref;
  U64 ref_count;
};

// A View's reference to an attachment, and the Cleat view it shows it with.
// The view is reopened with its last size and role when the attachment
// re-attaches.
struct UIShell_CleatViewRef
{
  UIShell_CleatViewRef *next, *prev;
  UIShell_CleatAttachment *attachment;
  cleat_view *view;
  U64 touch_seq;
  U32 requested_role;
  U64 role_requested_us;
  U16 cols, rows;
  F32 cell_width_px, cell_height_px;
};

// How an attachment's Views map to Cleat roles. last-touched: the View most
// recently focused, typed or clicked in is the controller and the others
// watch, so the grid takes its size; when it goes, the most recently touched
// remaining View takes over. Before any is touched, the first acquired is.
// Another policy (all controllers, a role per crew vessel) goes here.
typedef enum UIShell_CleatRolePolicy
{
  UIShell_CleatRolePolicy_LastTouchedController,
} UIShell_CleatRolePolicy;

global UIShell_CleatRolePolicy uishell_cleat_role_policy = UIShell_CleatRolePolicy_LastTouchedController;
global U64 uishell_cleat_connection_linger_us = 30*Million(1);
global UIShell_CleatConnection *uishell_cleat_connections = 0;
global UIShell_CleatAttachment *uishell_cleat_attachments = 0;
global Arena *uishell_cleat_ref_arena = 0;
global UIShell_CleatViewRef *uishell_cleat_free_refs = 0;
global U64 uishell_cleat_touch_seq = 0;

internal B32
uishell_cleat_route_match(UIShell_CleatRoute a, UIShell_CleatRoute b)
{
  return a.ssh == b.ssh && str8_match(a.host, b.host, 0) && str8_match(a.command, b.command, 0) &&
    str8_match(a.runtime_root, b.runtime_root, 0) && str8_match(a.daemon, b.daemon, 0);
}

////////////////////////////////
//- uishell: ssh routes
//
// A Cleat endpoint on another host (#104) is reached through
// `ssh <host> cleat proxy`, whose stdio carries the packet stream; the user's
// ssh configuration is the authentication, and Wheelhouse stores no
// credentials (docs/design/remote-terminals.md).

// The argv of the command a connection reaches `host`'s daemon through:
// Wheelhouse's own route, ssh, around the `cleat proxy` command line Cleat
// quotes for the far shell (cleat_proxy_command). BatchMode makes ssh fail
// with a reason instead of prompting: there is no terminal for it to ask on.
// 0 when Cleat refuses the target (a NUL no shell word can carry).
internal U64
uishell_cleat_remote_argv(Arena *arena, String8 host, String8 cleat, String8 runtime_root, String8 daemon, String8 *argv, U64 cap)
{
  cleat_proxy_target target =
  {
    .size = sizeof(target),
    .cleat = {cleat.str, cleat.size},
    .daemon = {daemon.str, daemon.size},
    .runtime_root = {runtime_root.str, runtime_root.size},
  };
  size_t len = 0;
  if(!cleat_proxy_command(&target, 0, 0, &len)) { return 0; }
  U8 *command = push_array_no_zero(arena, U8, len);
  if(!cleat_proxy_command(&target, command, len, &len)) { return 0; }
  String8 words[] = {str8_lit("ssh"), str8_lit("-T"), str8_lit("-o"), str8_lit("BatchMode=yes"), str8_lit("--"), host, str8(command, len)};
  Assert(cap >= ArrayCount(words));
  U64 count = Min(cap, ArrayCount(words));
  for(U64 i = 0; i < count; i++) { argv[i] = words[i]; }
  return count;
}

internal cleat_provider *
uishell_cleat_route_open(UIShell_CleatRoute route)
{
  cleat_provider_desc desc =
  {
    .abi_version = CLEAT_PROVIDER_ABI_VERSION,
    .requested_features = UISHELL_CLEAT_PROVIDER_FEATURES,
    .backend = CLEAT_PROVIDER_BACKEND_DAEMON,
  };
  cleat_provider *provider = 0;
  if(!route.ssh)
  {
    desc.runtime_root = route.runtime_root.size != 0 ? route.runtime_root.str : 0;
    desc.runtime_root_len = route.runtime_root.size;
    desc.daemon_name = route.daemon.size != 0 ? route.daemon.str : 0;
    desc.daemon_name_len = route.daemon.size;
    provider = cleat_provider_open(&desc);
  }
  else
  {
    Temp scratch = scratch_begin(0, 0);
    String8 argv[8];
    U64 argc = uishell_cleat_remote_argv(scratch.arena, route.host, route.command, route.runtime_root, route.daemon, argv, ArrayCount(argv));
    if(argc == 0) { log_user_errorf("Remote terminal: Cleat refused the endpoint for %S", route.host); }
    else
    {
      cleat_str args[8];
      for(U64 i = 0; i < argc; i++) { args[i] = (cleat_str){argv[i].str, argv[i].size}; }
      cleat_provider_transport transport = {.size = sizeof(transport), .kind = CLEAT_TRANSPORT_COMMAND, .argv = args, .argc = argc};
      provider = cleat_provider_open_transport(&desc, &transport);
      if(provider == 0)
      {
        StringJoin join = {.sep = str8_lit(" ")};
        String8List list = {0};
        for(U64 i = 0; i < argc; i++) { str8_list_push(scratch.arena, &list, argv[i]); }
        log_user_errorf("Remote terminal: could not start %S", str8_list_join(scratch.arena, &list, &join));
      }
    }
    scratch_end(scratch);
  }
  if(provider != 0) { cleat_provider_set_wake_callback(provider, uishell_terminal_provider_wake, 0); }
  return provider;
}

////////////////////////////////
//- uishell: connections

internal void
uishell_cleat_connection_open(UIShell_CleatConnection *connection)
{
  connection->provider = uishell_cleat_route_open(connection->route);
  connection->opens++;
  connection->streamed = 0;
  // A provider that didn't start has nothing to wait for.
  connection->given_up = connection->provider == 0;
  connection->reconnect = 0;
}

// The connection for `route`, opened if there is none. Its users count is the
// caller's to change.
internal UIShell_CleatConnection *
uishell_cleat_connection_from_route(UIShell_CleatRoute route)
{
  for(UIShell_CleatConnection *c = uishell_cleat_connections; c != 0; c = c->next)
  {
    if(uishell_cleat_route_match(c->route, route)) { return c; }
  }
  Arena *arena = arena_alloc();
  UIShell_CleatConnection *connection = push_array(arena, UIShell_CleatConnection, 1);
  connection->arena = arena;
  connection->route.ssh = route.ssh;
  connection->route.host = push_str8_copy(arena, route.host);
  connection->route.command = push_str8_copy(arena, route.command);
  connection->route.runtime_root = push_str8_copy(arena, route.runtime_root);
  connection->route.daemon = push_str8_copy(arena, route.daemon);
  connection->idle_since_us = now_time_us();
  uishell_cleat_connection_open(connection);
  connection->next = uishell_cleat_connections;
  uishell_cleat_connections = connection;
  return connection;
}

internal UIShell_CleatConnection *
uishell_cleat_connection_acquire(UIShell_CleatRoute route)
{
  UIShell_CleatConnection *connection = uishell_cleat_connection_from_route(route);
  connection->users++;
  return connection;
}

// Wakes the UI thread when a linger may have run out, so an idle Wheelhouse
// still drops the connection.
internal void
uishell_cleat_linger_wake(void *p)
{
  U64 linger_us = (U64)(uintptr_t)p;
  sleep_ms((U32)Min(linger_us/1000 + 50, (U64)max_U32));
  wm_send_wakeup_event();
}

internal void
uishell_cleat_connection_release(UIShell_CleatConnection *connection)
{
  if(connection == 0 || connection->users == 0) { return; }
  connection->users--;
  if(connection->users == 0)
  {
    connection->idle_since_us = now_time_us();
    thread_detach(thread_launch(uishell_cleat_linger_wake, (void *)(uintptr_t)uishell_cleat_connection_linger_us));
  }
}

// A provider for a reader that holds no session on it (the Sessions View's
// directory): the local daemon's connection, kept while it is read and
// lingering after.
internal cleat_provider *
uishell_cleat_daemon_provider_from_name(String8 daemon_name)
{
  UIShell_CleatRoute route = {.daemon = daemon_name};
  UIShell_CleatConnection *connection = uishell_cleat_connection_from_route(route);
  if(connection->users == 0) { connection->idle_since_us = now_time_us(); }
  return connection->provider;
}

////////////////////////////////
//- uishell: attachments and their Views

internal UIShell_CleatViewRef *
uishell_cleat_attachment_controller(UIShell_CleatAttachment *attachment)
{
  UIShell_CleatViewRef *result = 0;
  switch(uishell_cleat_role_policy)
  {
    case UIShell_CleatRolePolicy_LastTouchedController:
    {
      for(UIShell_CleatViewRef *ref = attachment->first_ref; ref != 0; ref = ref->next)
      {
        if(result == 0 || ref->touch_seq > result->touch_seq) { result = ref; }
      }
    }break;
  }
  return result;
}

internal U32
uishell_cleat_policy_role(UIShell_CleatViewRef *ref)
{
  return uishell_cleat_attachment_controller(ref->attachment) == ref ? CLEAT_ROLE_CONTROLLER : CLEAT_ROLE_WATCHER;
}

// Asks Cleat for each View's role under the policy, where it changed. A role
// is shared driving, never taken: another client driving the session keeps
// driving it, and the View offers to take control.
internal void
uishell_cleat_attachment_apply_policy(UIShell_CleatAttachment *attachment)
{
  for(UIShell_CleatViewRef *ref = attachment->first_ref; ref != 0; ref = ref->next)
  {
    U32 role = uishell_cleat_policy_role(ref);
    if(ref->view != 0 && ref->requested_role != role)
    {
      cleat_view_set_role(ref->view, role, 0);
      ref->requested_role = role;
      ref->role_requested_us = now_time_us();
    }
  }
}

internal void
uishell_cleat_view_open(UIShell_CleatViewRef *ref)
{
  UIShell_CleatAttachment *attachment = ref->attachment;
  if(attachment->session == 0 || ref->view != 0) { return; }
  U32 role = uishell_cleat_policy_role(ref);
  cleat_view_desc desc =
  {
    .cols = Max(ref->cols, 1),
    .rows = Max(ref->rows, 1),
    .cell_width_px = ref->cell_width_px,
    .cell_height_px = ref->cell_height_px,
    .role = role,
  };
  ref->view = cleat_session_open_view(attachment->session, &desc);
  ref->requested_role = role;
  ref->role_requested_us = now_time_us();
}

internal void
uishell_cleat_attachment_attach(UIShell_CleatAttachment *attachment)
{
  cleat_provider *provider = attachment->connection->provider;
  if(provider == 0 || attachment->session != 0) { return; }
  cleat_session_desc desc =
  {
    .id = attachment->session_id.str,
    .id_len = attachment->session_id.size,
    .vt_engine = CLEAT_PROVIDER_VT_GHOSTTY,
    .colors = &attachment->colors,
    .role = CLEAT_ROLE_NO_VIEW,
  };
  if(attachment->first_ref != 0)
  {
    desc.cols = Max(attachment->first_ref->cols, 1);
    desc.rows = Max(attachment->first_ref->rows, 1);
    desc.cell_width_px = attachment->first_ref->cell_width_px;
    desc.cell_height_px = attachment->first_ref->cell_height_px;
  }
  attachment->session = cleat_session_attach(provider, &desc);
  attachment->attaches++;
  for(UIShell_CleatViewRef *ref = attachment->first_ref; ref != 0; ref = ref->next) { uishell_cleat_view_open(ref); }
}

internal void
uishell_cleat_attachment_detach(UIShell_CleatAttachment *attachment)
{
  for(UIShell_CleatViewRef *ref = attachment->first_ref; ref != 0; ref = ref->next)
  {
    if(ref->view != 0) { cleat_view_destroy(ref->view); ref->view = 0; }
  }
  if(attachment->session != 0) { cleat_session_destroy(attachment->session); attachment->session = 0; }
}

// A View's reference to `session_id` over `route`, with its own Cleat view
// of `cols`x`rows` cells. The attachment and connection are shared with every
// other View of the session; the first View's colors are the attachment's.
internal UIShell_CleatViewRef *
uishell_cleat_view_acquire(UIShell_CleatRoute route, String8 session_id, cleat_session_colors colors,
                           U16 cols, U16 rows, F32 cell_width_px, F32 cell_height_px)
{
  UIShell_CleatAttachment *attachment = 0;
  for(UIShell_CleatAttachment *a = uishell_cleat_attachments; a != 0; a = a->next)
  {
    if(str8_match(a->session_id, session_id, 0) && uishell_cleat_route_match(a->connection->route, route)) { attachment = a; break; }
  }
  if(attachment == 0)
  {
    Arena *arena = arena_alloc();
    attachment = push_array(arena, UIShell_CleatAttachment, 1);
    attachment->arena = arena;
    attachment->session_id = push_str8_copy(arena, session_id);
    attachment->colors = colors;
    attachment->connection = uishell_cleat_connection_acquire(route);
    attachment->next = uishell_cleat_attachments;
    uishell_cleat_attachments = attachment;
  }
  if(uishell_cleat_ref_arena == 0) { uishell_cleat_ref_arena = arena_alloc(); }
  UIShell_CleatViewRef *ref = uishell_cleat_free_refs;
  if(ref != 0) { SLLStackPop(uishell_cleat_free_refs); MemoryZeroStruct(ref); }
  else { ref = push_array(uishell_cleat_ref_arena, UIShell_CleatViewRef, 1); }
  ref->attachment = attachment;
  ref->cols = cols;
  ref->rows = rows;
  ref->cell_width_px = cell_width_px;
  ref->cell_height_px = cell_height_px;
  DLLPushBack(attachment->first_ref, attachment->last_ref, ref);
  attachment->ref_count++;
  if(attachment->session == 0 && !attachment->connection->given_up) { uishell_cleat_attachment_attach(attachment); }
  else { uishell_cleat_view_open(ref); }
  return ref;
}

// Closes the View's Cleat view. The attachment stays until the end of the
// frame, closing then if no View has taken it up again.
internal void
uishell_cleat_view_release(UIShell_CleatViewRef *ref)
{
  if(ref == 0) { return; }
  UIShell_CleatAttachment *attachment = ref->attachment;
  if(ref->view != 0) { cleat_view_destroy(ref->view); }
  DLLRemove(attachment->first_ref, attachment->last_ref, ref);
  attachment->ref_count--;
  uishell_cleat_attachment_apply_policy(attachment);
  MemoryZeroStruct(ref);
  SLLStackPush(uishell_cleat_free_refs, ref);
}

// Focus moved into the View, or the user typed or clicked in it.
internal void
uishell_cleat_view_touch(UIShell_CleatViewRef *ref)
{
  if(ref == 0) { return; }
  if(ref->touch_seq != uishell_cleat_touch_seq || ref->touch_seq == 0) { ref->touch_seq = ++uishell_cleat_touch_seq; }
  uishell_cleat_attachment_apply_policy(ref->attachment);
}

// The user asked this View to drive, over any other client's control.
internal void
uishell_cleat_view_take_control(UIShell_CleatViewRef *ref)
{
  if(ref == 0) { return; }
  uishell_cleat_view_touch(ref);
  if(ref->view != 0)
  {
    cleat_view_take_control(ref->view);
    ref->requested_role = CLEAT_ROLE_CONTROLLER;
    ref->role_requested_us = now_time_us();
  }
}

internal void
uishell_cleat_view_resize(UIShell_CleatViewRef *ref, U16 cols, U16 rows)
{
  ref->cols = cols;
  ref->rows = rows;
  if(ref->view != 0) { cleat_view_resize(ref->view, cols, rows); }
}

internal void
uishell_cleat_view_update_geometry(UIShell_CleatViewRef *ref, cleat_terminal_geometry const *geometry)
{
  ref->cell_width_px = geometry->cell_width_px;
  ref->cell_height_px = geometry->cell_height_px;
  if(ref->view != 0) { cleat_view_update_geometry(ref->view, geometry); }
}

// The View is the one its policy has drive, but Cleat hasn't granted it (yet).
internal B32
uishell_cleat_view_policy_controller(UIShell_CleatViewRef *ref)
{
  return ref != 0 && ref->requested_role == CLEAT_ROLE_CONTROLLER;
}

internal void
uishell_cleat_connection_reconnect(UIShell_CleatConnection *connection)
{
  if(connection != 0) { connection->reconnect = 1; }
}

internal void
uishell_cleat_connection_reopen(UIShell_CleatConnection *connection)
{
  for(UIShell_CleatAttachment *a = uishell_cleat_attachments; a != 0; a = a->next)
  {
    if(a->connection == connection) { uishell_cleat_attachment_detach(a); }
  }
  if(connection->provider != 0) { cleat_provider_close(connection->provider); }
  uishell_cleat_connection_open(connection);
  for(UIShell_CleatAttachment *a = uishell_cleat_attachments; a != 0; a = a->next)
  {
    if(a->connection == connection && a->ref_count != 0) { uishell_cleat_attachment_attach(a); }
  }
}

// A command transport's state, read only while some view of it isn't
// streaming: one that failed after streaming reopens, one that never
// streamed gives up.
internal void
uishell_cleat_connection_watch(UIShell_CleatConnection *connection)
{
  if(!connection->route.ssh || connection->given_up || connection->provider == 0) { return; }
  B32 all_streaming = 1, any_view = 0;
  for(UIShell_CleatAttachment *a = uishell_cleat_attachments; a != 0; a = a->next)
  {
    if(a->connection != connection) { continue; }
    for(UIShell_CleatViewRef *ref = a->first_ref; ref != 0; ref = ref->next)
    {
      if(ref->view == 0) { continue; }
      any_view = 1;
      B32 streaming = cleat_view_connection_state(ref->view) == CLEAT_SESSION_STREAMING;
      connection->streamed |= streaming;
      all_streaming &= streaming;
    }
  }
  if(!any_view || all_streaming) { return; }
  cleat_transport_state state = {.size = sizeof(state)};
  if(!cleat_provider_transport_state(connection->provider, &state) || state.retrying) { return; }
  if(connection->streamed) { uishell_cleat_connection_reopen(connection); }
  else { connection->given_up = 1; }
}

// End of frame: attachments no View holds close, failed command transports
// reopen or give up, and connections past their linger close.
internal void
uishell_cleat_attachments_end_frame(void)
{
  for(UIShell_CleatAttachment **link = &uishell_cleat_attachments; *link != 0;)
  {
    UIShell_CleatAttachment *attachment = *link;
    if(attachment->ref_count != 0) { link = &attachment->next; continue; }
    *link = attachment->next;
    uishell_cleat_attachment_detach(attachment);
    uishell_cleat_connection_release(attachment->connection);
    arena_release(attachment->arena);
  }
  U64 now = now_time_us();
  for(UIShell_CleatConnection **link = &uishell_cleat_connections; *link != 0;)
  {
    UIShell_CleatConnection *connection = *link;
    if(connection->users == 0 && now >= connection->idle_since_us + uishell_cleat_connection_linger_us)
    {
      *link = connection->next;
      if(connection->provider != 0) { cleat_provider_close(connection->provider); }
      arena_release(connection->arena);
      continue;
    }
    link = &connection->next;
    if(connection->reconnect) { uishell_cleat_connection_reopen(connection); }
    else if(connection->users != 0) { uishell_cleat_connection_watch(connection); }
  }
}

//- uishell: registry counts, for diagnostics

internal U64
uishell_cleat_connection_count(void)
{
  U64 count = 0;
  for(UIShell_CleatConnection *c = uishell_cleat_connections; c != 0; c = c->next) { count++; }
  return count;
}

internal U64
uishell_cleat_attachment_count(void)
{
  U64 count = 0;
  for(UIShell_CleatAttachment *a = uishell_cleat_attachments; a != 0; a = a->next) { count++; }
  return count;
}
