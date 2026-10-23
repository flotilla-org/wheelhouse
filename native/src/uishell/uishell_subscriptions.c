//- The Dashboard's provider subscriptions (CONTEXT.md, Dashboard; ADR 0012).
// Each names a Content Provider the Dashboard subscribes to: a subscription
// ID (UUIDv7), made when it is added, which is the provider Andamento stamps
// on every fact it publishes; its kind (only "flotilla" so far); and how to
// connect (the Flotilla daemon endpoint, empty for Flotilla's default, as
// `--daemon` / FLOTILLA_DAEMON chose it for the daily driver).
//
// They are kept in the Dashboard directory, in a file of Wheelhouse's own,
// in its config syntax:
//
//   <Dashboard>/subscriptions.wheelhouse
//     subscription:
//     {
//       id: "<subscription ID>"
//       kind: "flotilla"
//       daemon: "ssh://host/path"
//     }
//
// not in Andamento's dashboard record, which is Andamento's alone: each
// window still has a core (and records) of its own while subscriptions
// belong to the process, and the subscription runner that will own them is
// a host-side crate, not the core (wheelhouse#305).
//
// Live (with --andamento_socket), each subscription gets its own ingress
// endpoint, named after the local one: "<local endpoint>-<subscription ID>",
// a Unix socket beside it in its private directory or a named pipe, served
// as ADR 0011 says. Patches arriving there are applied with that
// subscription as their provider; the local endpoint's (one-off scripts, the
// git watcher) are the host's own ("wheelhouse"). Wheelhouse runs
// `flotilla pm connect --wheelhouse-socket <its endpoint>` for each and
// restarts it when it exits (connector.rs). While a connector is down its
// provider is stale: its facts are kept and marked, rather than expiring,
// until it publishes again. Removing a subscription stops its connector and
// retracts its facts; workspaces open on them are kept, retained.

internal void
uishell_subscription_set_stale(UIShell_Subscription *s, B32 stale)
{
  s->stale = stale;
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  {
    UIShell_SidebarState *state = uishell_sidebar_init(ws);
    if(state->core == 0) { continue; }
    char *error = 0;
    uishell_sidebar_result(state, andamento_provider_set_stale(state->core, uishell_sidebar_text(s->id), stale, &error), error);
    uishell_sidebar_refresh(state);
  }
  rd_request_frame();
}

// A new core: a subscription that is stale now is stale there too. A
// connector sends its catalog once, as a snapshot when it starts, then only
// changes, so a window opened after another gets its facts by restarting the
// connectors: their snapshots replace each provider's facts in every window.
// (Until one core serves every window.)
internal void
uishell_subscriptions_prepare_core(Andamento *core)
{
  B32 others = 0;
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  { if(ws->sidebar && ws->sidebar->core && ws->sidebar->core != core) { others = 1; } }
  for(UIShell_Subscription *s = uishell_subscriptions.first; s; s = s->next)
  {
    if(s->stale) { andamento_provider_set_stale(core, uishell_sidebar_text(s->id), 1, 0); }
    if(others) { wheelhouse_connector_restart(s->connector); }
  }
}

// Its endpoint and connector, when live and of a kind Wheelhouse connects.
internal void
uishell_subscription_connect(UIShell_Subscription *s)
{
  UIShell_Subscriptions *subs = &uishell_subscriptions;
  if(!subs->live || s->ingress || !str8_match(s->kind, str8_lit("flotilla"), 0)) { return; }
  s->endpoint = push_str8f(s->arena, "%S-%S", subs->local_endpoint, s->id);
  U8 error[512] = {0};
  String8 recording = subs->record ? push_str8f(s->arena, "%S/ingress-%S.jsonl", subs->log_dir, s->id) : str8_zero();
  s->ingress = wheelhouse_ingress_start_recorded(s->endpoint.str, s->endpoint.size, wm_send_wakeup_event,
    recording.str, recording.size, subs->record_bytes, subs->record_files, error, sizeof(error));
  if(!s->ingress)
  {
    s->error = push_str8f(s->arena, "Subscription %S's endpoint: %s", s->id, error);
    fprintf(stderr, "%.*s\n", str8_varg(s->error));
    return;
  }
  Temp scratch = scratch_begin(0, 0);
  WheelhouseIngressText argv[] =
  {
    {subs->flotilla_bin.str, subs->flotilla_bin.size}, {(U8 *)"pm", 2}, {(U8 *)"connect", 7},
    {(U8 *)"--wheelhouse-socket", 19}, {s->endpoint.str, s->endpoint.size},
    {(U8 *)"--flotilla-bin", 14}, {subs->flotilla_bin.str, subs->flotilla_bin.size},
  };
  // Anything it starts publishes to its endpoint too. Flotilla's default
  // daemon is the one without FLOTILLA_DAEMON, whatever Wheelhouse has.
  String8 env[] =
  {
    push_str8f(scratch.arena, "WHEELHOUSE_SOCKET=%S", s->endpoint),
    s->daemon.size ? push_str8f(scratch.arena, "FLOTILLA_DAEMON=%S", s->daemon) : str8_lit("FLOTILLA_DAEMON"),
  };
  WheelhouseIngressText envs[ArrayCount(env)] = {0};
  U64 env_count = 0;
  for(U64 i = 0; i < ArrayCount(env); i++) { envs[env_count++] = (WheelhouseIngressText){env[i].str, env[i].size}; }
  String8 log = push_str8f(scratch.arena, "%S/flotilla-%S.log", subs->log_dir, s->id);
  s->connector = wheelhouse_connector_start(argv, ArrayCount(argv), envs, env_count, (WheelhouseIngressText){log.str, log.size}, 0,
    wm_send_wakeup_event, error, sizeof(error));
  if(!s->connector)
  {
    s->error = push_str8f(s->arena, "Subscription %S's connector: %s", s->id, error);
    fprintf(stderr, "%.*s\n", str8_varg(s->error));
  }
  scratch_end(scratch);
}

internal void
uishell_subscription_disconnect(UIShell_Subscription *s)
{
  wheelhouse_connector_stop(s->connector);
  wheelhouse_ingress_stop(s->ingress);
  s->connector = 0;
  s->ingress = 0;
  s->running = 0;
  s->starts = 0;
}

internal UIShell_Subscription *
uishell_subscription_push(String8 id, String8 kind, String8 daemon)
{
  UIShell_Subscriptions *subs = &uishell_subscriptions;
  Arena *arena = arena_alloc();
  UIShell_Subscription *s = push_array(arena, UIShell_Subscription, 1);
  s->arena = arena;
  s->id = push_str8_copy(arena, id);
  s->kind = push_str8_copy(arena, kind);
  s->daemon = push_str8_copy(arena, daemon);
  SLLQueuePush(subs->first, subs->last, s);
  subs->count++;
  return s;
}

// Stops every subscription's connector and endpoint and forgets them. The
// connectors stop together, each within its grace (connector.rs), so
// quitting waits for the slowest rather than their sum.
internal void
uishell_subscriptions_close(void)
{
  UIShell_Subscriptions *subs = &uishell_subscriptions;
  for(UIShell_Subscription *s = subs->first; s; s = s->next) { wheelhouse_connector_stop_begin(s->connector); }
  for(UIShell_Subscription *s = subs->first, *next = 0; s; s = next)
  {
    next = s->next;
    uishell_subscription_disconnect(s);
    arena_release(s->arena);
  }
  subs->first = subs->last = 0;
  subs->count = 0;
}

internal String8
uishell_subscriptions_path(Arena *arena)
{
  return uishell_dashboard.dir.size ? push_str8f(arena, "%S/subscriptions.wheelhouse", uishell_dashboard.dir) : str8_zero();
}

// The Dashboard's subscriptions, in place of any it had (a launch, or
// another Dashboard opened), connected if live.
internal void
uishell_subscriptions_load(void)
{
  uishell_subscriptions_close();
  Temp scratch = scratch_begin(0, 0);
  String8 path = uishell_subscriptions_path(scratch.arena);
  String8 text = path.size ? data_from_file_path(scratch.arena, path) : str8_zero();
  MD_Node *root = text.size ? md_tree_from_string(scratch.arena, text) : &md_nil_node;
  for MD_EachNode(n, root->first)
  {
    String8 fields[3] = {0};
    String8 names[3] = {str8_lit("id"), str8_lit("kind"), str8_lit("daemon")};
    for(U64 i = 0; i < ArrayCount(names); i++)
    { fields[i] = raw_from_escaped_str8(scratch.arena, md_child_from_string(n, names[i], 0)->first->string); }
    if(!str8_match(n->string, str8_lit("subscription"), 0) || !uishell_dashboard_id_is_valid(fields[0])) { continue; }
    uishell_subscription_push(fields[0], fields[1].size ? fields[1] : str8_lit("flotilla"), fields[2]);
  }
  for(UIShell_Subscription *s = uishell_subscriptions.first; s; s = s->next) { uishell_subscription_connect(s); }
  scratch_end(scratch);
}

internal void
uishell_subscriptions_save(void)
{
  if(!uishell_dashboard.dir.size) { return; }
  Temp scratch = scratch_begin(0, 0);
  String8List lines = {0};
  str8_list_push(scratch.arena, &lines, str8_lit("// The Dashboard's provider subscriptions, kept by Wheelhouse (uishell_subscriptions.c).\n"));
  for(UIShell_Subscription *s = uishell_subscriptions.first; s; s = s->next)
  {
    str8_list_pushf(scratch.arena, &lines, "subscription:\n{\n  id: \"%S\"\n  kind: \"%S\"\n", escaped_from_raw_str8(scratch.arena, s->id),
      escaped_from_raw_str8(scratch.arena, s->kind));
    if(s->daemon.size) { str8_list_pushf(scratch.arena, &lines, "  daemon: \"%S\"\n", escaped_from_raw_str8(scratch.arena, s->daemon)); }
    str8_list_push(scratch.arena, &lines, str8_lit("}\n"));
  }
  uishell_dashboard_save_identity();
  if(!uishell_dashboard_make_directories(uishell_dashboard.dir) ||
     !uishell_sidebar_record_write(uishell_subscriptions_path(scratch.arena), str8_list_join(scratch.arena, &lines, 0)))
  { log_user_errorf("Couldn't save the Dashboard's subscriptions in \"%S\".", uishell_dashboard.dir); }
  scratch_end(scratch);
}

// Connects every subscription through endpoints named after the local one.
internal void
uishell_subscriptions_go_live(String8 local_endpoint, String8 flotilla_bin, String8 log_dir)
{
  UIShell_Subscriptions *subs = &uishell_subscriptions;
  if(!subs->arena) { subs->arena = arena_alloc(); }
  arena_clear(subs->arena);
  if(!flotilla_bin.size) { flotilla_bin = uishell_dashboard_env(subs->arena, "FLOTILLA_BIN"); }
  if(!flotilla_bin.size) { flotilla_bin = str8_lit("flotilla"); }
  subs->local_endpoint = push_str8_copy(subs->arena, local_endpoint);
  subs->flotilla_bin = push_str8_copy(subs->arena, flotilla_bin);
  subs->log_dir = push_str8_copy(subs->arena, log_dir);
  subs->live = 1;
  for(UIShell_Subscription *s = subs->first; s; s = s->next) { uishell_subscription_connect(s); }
}

internal UIShell_Subscription *
uishell_subscription_add(String8 kind, String8 daemon)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_Subscription *s = uishell_subscription_push(uishell_string_from_workspace_id(scratch.arena, uishell_workspace_id_make()), kind, daemon);
  scratch_end(scratch);
  uishell_subscriptions_save();
  uishell_subscription_connect(s);
  return s;
}

// The subscription of `kind` with `daemon`, added if the Dashboard has none.
internal UIShell_Subscription *
uishell_subscription_ensure(String8 kind, String8 daemon)
{
  for(UIShell_Subscription *s = uishell_subscriptions.first; s; s = s->next)
  { if(str8_match(s->kind, kind, 0) && str8_match(s->daemon, daemon, 0)) { return s; } }
  return uishell_subscription_add(kind, daemon);
}

// The subscription a person named: its ID, or its daemon endpoint
// ("default" for Flotilla's own).
internal UIShell_Subscription *
uishell_subscription_from_text(String8 text)
{
  text = str8_skip_chop_whitespace(text);
  String8 daemon = str8_match(text, str8_lit("default"), 0) ? str8_zero() : text;
  for(UIShell_Subscription *s = uishell_subscriptions.first; s; s = s->next)
  { if(str8_match(s->id, text, StringMatchFlag_CaseInsensitive) || str8_match(s->daemon, daemon, 0)) { return s; } }
  return 0;
}

// Stops its connector, retracts its facts from every window's core (a
// workspace open on one keeps it, retained) and forgets it.
internal void
uishell_subscription_remove(UIShell_Subscription *s)
{
  UIShell_Subscriptions *subs = &uishell_subscriptions;
  uishell_subscription_disconnect(s);
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  {
    UIShell_SidebarState *state = uishell_sidebar_init(ws);
    if(state->core == 0) { continue; }
    char *error = 0;
    uishell_sidebar_result(state, andamento_provider_retract(state->core, uishell_sidebar_text(s->id), &error), error);
    uishell_sidebar_refresh(state);
  }
  UIShell_Subscription *prev = 0;
  for(UIShell_Subscription *c = subs->first; c && c != s; c = c->next) { prev = c; }
  if(prev) { prev->next = s->next; } else { subs->first = s->next; }
  if(subs->last == s) { subs->last = prev; }
  subs->count--;
  arena_release(s->arena);
  uishell_subscriptions_save();
  rd_request_frame();
}

// Between frames: each endpoint's patches applied as its subscription's,
// and a connector that went down since makes its provider stale.
internal void
uishell_subscriptions_poll(void)
{
  for(UIShell_Subscription *s = uishell_subscriptions.first; s; s = s->next)
  {
    if(s->connector)
    {
      U64 starts = 0;
      B32 running = wheelhouse_connector_running(s->connector, &starts);
      // Down now, or down and back since the last poll.
      if(!s->stale && ((s->running && !running) || (s->starts && starts != s->starts))) { uishell_subscription_set_stale(s, 1); }
      s->running = running;
      s->starts = starts;
    }
    if(s->ingress) { wheelhouse_ingress_poll_observed(s->ingress, uishell_sidebar_apply_live, uishell_sidebar_retract_live, uishell_sidebar_observed_workdirs, s); }
  }
}

// How the logical state names a provider: its subscription's position in
// the Dashboard, "local" for Andamento's or the host's own, or "removed" for
// one it no longer has.
internal String8
uishell_subscription_label(Arena *arena, String8 provider)
{
  if(!provider.size || str8_match(provider, uishell_sidebar_local_provider, 0) || str8_match(provider, uishell_sidebar_host_provider, 0))
  { return str8_lit("local"); }
  U64 index = 1;
  for(UIShell_Subscription *s = uishell_subscriptions.first; s; s = s->next, index++)
  { if(str8_match(s->id, provider, 0)) { return push_str8f(arena, "%I64u", index); } }
  return str8_lit("removed");
}
