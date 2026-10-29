// Shared terminal diagnostics (#417): Views of one Cleat session share one
// attachment, attachments on one daemon share one connection, and the
// last-touched View drives (uishell_cleat_attachments.c).
//
// It needs a private Cleat daemon with three live sessions and a host ssh
// reaches, named by environment (tools/test-shared-terminals.sh sets them up,
// with a PATH `ssh` shim when no sshd is at hand, which makes the command
// transport killable):
//   WH_SHARED_HOST      the ssh destination
//   WH_SHARED_CLEAT     the Cleat binary, run here and there
//   WH_SHARED_ROOT      its daemon's runtime root
//   WH_SHARED_SESSIONS  three live sessions in that daemon, comma separated;
//                       the third is killed
// Remote endpoints are Unix only, like Cleat's command transport.

#if !OS_WINDOWS

typedef struct UIShell_SharedDiag UIShell_SharedDiag;
struct UIShell_SharedDiag
{
  RD_WindowState *ws;
  RD_PanelAddress panel;
  String8 host, cleat, root;
};

// A terminal View of `session` over `transport`, or one running `command`
// when session is empty (the recipe path).
internal CFG_Node *
uishell_shared_diag_view(UIShell_SharedDiag *d, String8 transport, String8 session, String8 command)
{
  CFG_Node *view = rd_fixture_view(d->panel, str8_lit("terminal"), command, 1);
  if(session.size != 0)
  {
    String8 settings[][2] =
    {
      {str8_lit("cleat_transport"), transport}, {str8_lit("cleat_host"), d->host},
      {str8_lit("cleat_runtime_root"), d->root}, {str8_lit("cleat_session"), session}, {str8_lit("cleat_command"), d->cleat},
    };
    for(U64 i = 0; i < ArrayCount(settings); i++)
    { cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, settings[i][0]), settings[i][1]); }
  }
  return view;
}

internal UIShell_TerminalViewState *
uishell_shared_diag_state(UIShell_SharedDiag *d, CFG_Node *view)
{
  UIShell_TerminalViewState *tv = 0;
  UIShell_RegsScope(.window = d->ws->cfg_id, .panel = d->panel, .tab = rd_view_address_from_cfg(view), .view = rd_view_address_from_cfg(view))
  { tv = rd_view_state(UIShell_TerminalViewState); }
  return tv;
}

// Builds `view` once at `size`, as the canvas would, in a panel without
// focus unless `focused`.
internal void
uishell_shared_diag_build_focus(UIShell_SharedDiag *d, CFG_Node *view, Vec2F32 size, B32 focused)
{
  UIShell_RegsScope(.window = d->ws->cfg_id, .panel = d->panel, .tab = rd_view_address_from_cfg(view), .view = rd_view_address_from_cfg(view))
  {
    UI_EventList events = {0};
    UI_AnimationInfo animation = {0};
    ui_begin_build(d->ws->os, &events, &d->ws->ui->icon_info, d->ws->theme, &animation, 1.f/60, 1.f/60);
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12) UI_Focus(focused ? UI_FocusKind_On : UI_FocusKind_Off)
    {
      wh_view_ui_direct(wh_renderer_from_name(str8_lit("terminal")), rd_view_address_from_cfg(view), r2f32p(0, 0, size.x, size.y));
    }
    ui_end_build();
  }
}

internal void
uishell_shared_diag_build(UIShell_SharedDiag *d, CFG_Node *view, Vec2F32 size)
{
  uishell_shared_diag_build_focus(d, view, size, 0);
}

// One frame: each View is built, then the frame ends as uishell's does.
internal void
uishell_shared_diag_frame(UIShell_SharedDiag *d, CFG_Node **views, U64 count)
{
  for(U64 i = 0; i < count; i++) { if(views[i] != 0) { uishell_shared_diag_build(d, views[i], v2f32(640, 480)); } }
  uishell_cleat_attachments_end_frame();
}

internal void
uishell_shared_diag_release(UIShell_SharedDiag *d, CFG_Node *view)
{
  UIShell_RegsScope(.window = d->ws->cfg_id, .panel = d->panel, .tab = rd_view_address_from_cfg(view), .view = rd_view_address_from_cfg(view))
  { rd_view_states_release_view(rd_view_address_from_cfg(view)); }
}

internal B32
uishell_shared_diag_streaming(UIShell_TerminalViewState *tv)
{
  return tv != 0 && uishell_terminal_connection_state(tv) == CLEAT_SESSION_STREAMING;
}

internal U32
uishell_shared_diag_role(UIShell_TerminalViewState *tv)
{
  return tv != 0 && tv->shared != 0 && tv->shared->view != 0 ? cleat_view_role(tv->shared->view) : CLEAT_ROLE_UNKNOWN;
}

internal U64
uishell_shared_diag_ssh_connections(void)
{
  U64 count = 0;
  for(UIShell_CleatConnection *c = uishell_cleat_connections; c != 0; c = c->next) { count += c->route.ssh; }
  return count;
}

internal String8
uishell_shared_diag_run(Arena *arena, char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  String8 command = push_str8fv(arena, fmt, args);
  va_end(args);
  String8List out = {0};
  FILE *pipe = popen((char *)command.str, "r");
  if(pipe == 0) { return str8_zero(); }
  U8 buffer[4096];
  for(size_t n = 0; (n = fread(buffer, 1, sizeof(buffer), pipe)) > 0;) { str8_list_push(arena, &out, push_str8_copy(arena, str8(buffer, n))); }
  pclose(pipe);
  return str8_list_join(arena, &out, 0);
}

// The ssh bridges to this run's daemon now running: `cleat proxy` processes.
internal U64
uishell_shared_diag_bridges(Arena *arena, UIShell_SharedDiag *d)
{
  // Anchored at the command, so neither the shell running pgrep nor the ssh
  // (or shim) wrapping the proxy counts.
  String8 out = uishell_shared_diag_run(arena, "pgrep -f -- '^[^ ]*cleat proxy --runtime-root=%S' | wc -l", d->root);
  U64 count = 0;
  try_u64_from_str8_c_rules(str8_skip_chop_whitespace(out), &count);
  return count;
}

// Waits, building `views` each frame, until `condition` holds or 15 s pass.
#define SharedWait(views, count, condition) do { \
    U64 shared_deadline = now_time_us()+15*Million(1); \
    for(;;) { uishell_shared_diag_frame(&d, (views), (count)); if((condition) || now_time_us() > shared_deadline) { break; } sleep_ms(20); } \
  } while(0)

internal B32
uishell_shared_terminal_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  U32 failures = 0;
#define SharedCheck(condition, label) do { B32 passed = !!(condition); failures += !passed; fprintf(stderr, "%s shared terminal: %s\n", passed ? "PASS" : "FAIL", label); } while(0)
  char *env_host = getenv("WH_SHARED_HOST"), *env_cleat = getenv("WH_SHARED_CLEAT");
  char *env_root = getenv("WH_SHARED_ROOT"), *env_sessions = getenv("WH_SHARED_SESSIONS");
  if(!env_host || !env_cleat || !env_root || !env_sessions)
  {
    fprintf(stderr, "FAIL shared terminal: WH_SHARED_HOST, WH_SHARED_CLEAT, WH_SHARED_ROOT and WH_SHARED_SESSIONS are required\n");
    scratch_end(scratch);
    return 0;
  }
  UIShell_SharedDiag d = {.ws = ws, .host = str8_cstring(env_host), .cleat = str8_cstring(env_cleat), .root = str8_cstring(env_root)};
  String8 sessions = str8_cstring(env_sessions);
  U64 comma = str8_find_needle(sessions, 0, str8_lit(","), 0);
  U64 comma2 = str8_find_needle(sessions, comma+1, str8_lit(","), 0);
  String8 one = str8_prefix(sessions, comma), two = str8_substr(sessions, r1u64(comma+1, comma2));
  String8 three = str8_skip(sessions, comma2+1);
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *owner = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  d.panel = rd_fixture_leaf(rd_fixture_owner(owner));
  UI_State *saved_ui = ui_state, *test_ui = ui_state_alloc();
  ui_select_state(test_ui);
  uishell_cleat_connection_linger_us = 600*1000;
  String8 local = str8_lit("local"), ssh = str8_lit("ssh");

  //- 1. Two Views of one session make one attachment and one connection.
  CFG_Node *views[3] = {uishell_shared_diag_view(&d, local, one, str8_zero()), uishell_shared_diag_view(&d, local, one, str8_zero()), 0};
  UIShell_TerminalViewState *a = uishell_shared_diag_state(&d, views[0]), *b = uishell_shared_diag_state(&d, views[1]);
  SharedWait(views, 2, uishell_shared_diag_streaming(a) && uishell_shared_diag_streaming(b));
  SharedCheck(uishell_shared_diag_streaming(a) && uishell_shared_diag_streaming(b), "two Views of one session stream");
  SharedCheck(a->shared && b->shared && a->shared->attachment == b->shared->attachment && a->shared->view != b->shared->view,
              "two Views of one session share one attachment, each with its own Cleat view");
  SharedCheck(uishell_cleat_attachment_count() == 1 && uishell_cleat_connection_count() == 1, "one attachment and one connection");
  {
    // Presence counts attachments with a view (Cleat ADR 0009): the daemon
    // sees one client, not two.
    String8 inspect = uishell_shared_diag_run(scratch.arena, "'%S' --runtime-root '%S' inspect '%S' --json 2>&1", d.cleat, d.root, one);
    U64 present = 0;
    for(U64 at = str8_find_needle(inspect, 0, str8_lit("\"identity\""), 0); at < inspect.size;
        at = str8_find_needle(inspect, at+1, str8_lit("\"identity\""), 0)) { present++; }
    SharedCheck(present == 1, "the daemon sees one attachment");
  }

  //- 2. The last-touched View is the only controller.
  views[2] = uishell_shared_diag_view(&d, local, one, str8_zero());
  UIShell_TerminalViewState *c = uishell_shared_diag_state(&d, views[2]);
  SharedWait(views, 3, uishell_shared_diag_streaming(c) && uishell_shared_diag_role(a) == CLEAT_ROLE_CONTROLLER &&
             uishell_shared_diag_role(b) == CLEAT_ROLE_WATCHER && uishell_shared_diag_role(c) == CLEAT_ROLE_WATCHER);
  SharedCheck(uishell_shared_diag_role(a) == CLEAT_ROLE_CONTROLLER && uishell_shared_diag_role(b) == CLEAT_ROLE_WATCHER &&
              uishell_shared_diag_role(c) == CLEAT_ROLE_WATCHER, "before any touch, the first View drives and the others watch");
  // Focus moving into C, then into B.
  uishell_shared_diag_build_focus(&d, views[2], v2f32(640, 480), 1);
  uishell_shared_diag_build(&d, views[2], v2f32(640, 480));
  uishell_shared_diag_build_focus(&d, views[1], v2f32(640, 480), 1);
  uishell_shared_diag_build(&d, views[1], v2f32(640, 480));
  SharedWait(views, 3, uishell_shared_diag_role(b) == CLEAT_ROLE_CONTROLLER && uishell_shared_diag_role(a) == CLEAT_ROLE_WATCHER &&
             uishell_shared_diag_role(c) == CLEAT_ROLE_WATCHER);
  SharedCheck(uishell_shared_diag_role(b) == CLEAT_ROLE_CONTROLLER && uishell_shared_diag_role(a) == CLEAT_ROLE_WATCHER &&
              uishell_shared_diag_role(c) == CLEAT_ROLE_WATCHER, "touching a View moves control to it alone");
  SharedCheck(uishell_terminal_input_available(b) && !uishell_terminal_input_available(a), "only the controller View takes input");
  {
    // The grid takes the controller View's size: a smaller View's vote.
    U16 cols_before = b->cols;
    uishell_shared_diag_build(&d, views[1], v2f32(320, 240));
    U16 small_cols = b->cols;
    String8 want = push_str8f(scratch.arena, "\"cols\": %u", small_cols);
    String8 inspect = str8_zero();
    U64 deadline = now_time_us()+10*Million(1);
    for(;;)
    {
      uishell_shared_diag_build(&d, views[0], v2f32(640, 480));
      uishell_shared_diag_build(&d, views[1], v2f32(320, 240));
      uishell_shared_diag_build(&d, views[2], v2f32(640, 480));
      uishell_cleat_attachments_end_frame();
      inspect = uishell_shared_diag_run(scratch.arena, "'%S' --runtime-root '%S' inspect '%S' --json 2>&1", d.cleat, d.root, one);
      if(str8_find_needle(inspect, 0, want, 0) < inspect.size || now_time_us() > deadline) { break; }
      sleep_ms(50);
    }
    SharedCheck(small_cols < cols_before && str8_find_needle(inspect, 0, want, 0) < inspect.size, "the grid takes the controller View's size");
  }
  uishell_shared_diag_release(&d, views[1]);
  views[1] = 0;
  SharedWait(views, 3, uishell_shared_diag_role(c) == CLEAT_ROLE_CONTROLLER && uishell_shared_diag_role(a) == CLEAT_ROLE_WATCHER);
  SharedCheck(uishell_shared_diag_role(c) == CLEAT_ROLE_CONTROLLER && uishell_shared_diag_role(a) == CLEAT_ROLE_WATCHER,
              "releasing the controller View passes control to the most recently touched remaining View");

  //- 3. Releasing and re-acquiring a View of a session in one frame keeps
  // the attachment; one released for good closes at the end of its frame.
  {
    UIShell_CleatAttachment *attachment = a->shared->attachment;
    U64 attaches = attachment->attaches;
    cleat_session *session = attachment->session;
    uishell_shared_diag_release(&d, views[0]);
    uishell_shared_diag_release(&d, views[2]);
    views[0] = uishell_shared_diag_view(&d, local, one, str8_zero());
    views[2] = 0;
    uishell_shared_diag_build(&d, views[0], v2f32(640, 480));
    a = uishell_shared_diag_state(&d, views[0]);
    uishell_cleat_attachments_end_frame();
    SharedCheck(uishell_cleat_attachment_count() == 1 && a->shared && a->shared->attachment == attachment &&
                attachment->attaches == attaches && attachment->session == session, "a View released and re-acquired in one frame doesn't re-attach");
    SharedWait(views, 1, uishell_shared_diag_streaming(a));
    SharedCheck(uishell_shared_diag_streaming(a), "the re-acquired View streams");
    uishell_shared_diag_release(&d, views[0]);
    views[0] = 0;
    SharedCheck(uishell_cleat_attachment_count() == 1, "a released attachment stays until the end of the frame");
    uishell_cleat_attachments_end_frame();
    SharedCheck(uishell_cleat_attachment_count() == 0, "an attachment no View holds closes at the end of the frame");
  }

  //- 5. Two sessions on one remote daemon share one connection, and when its
  // transport fails both are re-attached.
  CFG_Node *remote[2] = {uishell_shared_diag_view(&d, ssh, one, str8_zero()), uishell_shared_diag_view(&d, ssh, two, str8_zero())};
  UIShell_TerminalViewState *r1 = uishell_shared_diag_state(&d, remote[0]), *r2 = uishell_shared_diag_state(&d, remote[1]);
  SharedWait(remote, 2, uishell_shared_diag_streaming(r1) && uishell_shared_diag_streaming(r2));
  SharedCheck(uishell_shared_diag_streaming(r1) && uishell_shared_diag_streaming(r2), "two remote sessions stream");
  SharedCheck(r1->shared && r2->shared && r1->shared->attachment != r2->shared->attachment &&
              r1->shared->attachment->connection == r2->shared->attachment->connection && uishell_shared_diag_ssh_connections() == 1,
              "two sessions on one remote daemon share one connection");
  SharedCheck(uishell_shared_diag_bridges(scratch.arena, &d) == 1, "one ssh bridge to the daemon");
  {
    UIShell_CleatConnection *connection = r1->shared->attachment->connection;
    U64 opens = connection->opens;
    uishell_shared_diag_run(scratch.arena, "pkill -f -- 'proxy --runtime-root=%S' ; true", d.root);
    SharedWait(remote, 2, connection->opens > opens && uishell_shared_diag_streaming(r1) && uishell_shared_diag_streaming(r2));
    SharedCheck(connection->opens == opens+1 && r1->shared->attachment->attaches == 2 && r2->shared->attachment->attaches == 2,
                "a failed transport reopens its connection and re-attaches both sessions");
    SharedCheck(uishell_shared_diag_streaming(r1) && uishell_shared_diag_streaming(r2) && !connection->given_up, "both stream again");
    String8 input = str8_lit("echo WH-SHARED-$((6*7))\r");
    if(r2->shared->view) { cleat_view_write_bytes(r2->shared->view, input.str, input.size); }
    String8 capture = str8_zero();
    U64 deadline = now_time_us()+10*Million(1);
    for(;;)
    {
      uishell_shared_diag_frame(&d, remote, 2);
      capture = uishell_shared_diag_run(scratch.arena, "'%S' --runtime-root '%S' capture '%S' 2>&1", d.cleat, d.root, two);
      if(str8_find_needle(capture, 0, str8_lit("WH-SHARED-42"), 0) < capture.size || now_time_us() > deadline) { break; }
      sleep_ms(50);
    }
    SharedCheck(str8_find_needle(capture, 0, str8_lit("WH-SHARED-42"), 0) < capture.size, "the re-attached session takes input");

    // A View opening on the live connection, and its session then ending,
    // leave the transport and the other sessions alone.
    CFG_Node *with_third[3] = {remote[0], remote[1], uishell_shared_diag_view(&d, ssh, three, str8_zero())};
    UIShell_TerminalViewState *r3 = uishell_shared_diag_state(&d, with_third[2]);
    opens = connection->opens;
    SharedWait(with_third, 3, uishell_shared_diag_streaming(r3));
    SharedCheck(uishell_shared_diag_streaming(r3) && connection->opens == opens && r3->shared->attachment->connection == connection,
                "a View opening on a live connection doesn't reopen it");
    uishell_shared_diag_run(scratch.arena, "'%S' --runtime-root '%S' kill '%S' >/dev/null 2>&1; true", d.cleat, d.root, three);
    SharedWait(with_third, 3, uishell_terminal_connection_state(r3) == CLEAT_SESSION_CLOSED);
    for(U64 settle = now_time_us()+Million(1); now_time_us() < settle;) { uishell_shared_diag_frame(&d, with_third, 3); sleep_ms(20); }
    SharedCheck(uishell_terminal_connection_state(r3) == CLEAT_SESSION_CLOSED && connection->opens == opens && !connection->given_up &&
                uishell_shared_diag_streaming(r1) && uishell_shared_diag_streaming(r2),
                "a session ending on a live connection leaves the connection and the other sessions streaming");
    uishell_shared_diag_release(&d, with_third[2]);
    uishell_cleat_attachments_end_frame();
  }

  //- 4. After the last attachment on a connection goes, the connection stays
  // for the linger and then closes.
  {
    uishell_shared_diag_release(&d, remote[0]);
    uishell_shared_diag_release(&d, remote[1]);
    uishell_cleat_attachments_end_frame();
    U64 released = now_time_us();
    SharedCheck(uishell_cleat_attachment_count() == 0 && uishell_shared_diag_ssh_connections() == 1 &&
                uishell_shared_diag_bridges(scratch.arena, &d) == 1, "the connection lingers after its last attachment");
    // A View back within the linger takes the same connection up again.
    remote[0] = uishell_shared_diag_view(&d, ssh, one, str8_zero());
    r1 = uishell_shared_diag_state(&d, remote[0]);
    UIShell_CleatConnection *lingering = uishell_cleat_connections;
    for(; lingering != 0 && !lingering->route.ssh; lingering = lingering->next) {}
    U64 opens = lingering ? lingering->opens : 0;
    SharedWait(remote, 1, uishell_shared_diag_streaming(r1));
    SharedCheck(lingering && r1->shared && r1->shared->attachment->connection == lingering && lingering->opens == opens &&
                uishell_shared_diag_streaming(r1), "a View within the linger reuses the connection");
    uishell_shared_diag_release(&d, remote[0]);
    uishell_cleat_attachments_end_frame();
    released = now_time_us();
    while(uishell_shared_diag_ssh_connections() != 0 && now_time_us() < released + 5*Million(1))
    {
      uishell_cleat_attachments_end_frame();
      sleep_ms(20);
    }
    U64 closed_after = now_time_us() - released;
    SharedCheck(uishell_shared_diag_ssh_connections() == 0 && closed_after >= uishell_cleat_connection_linger_us,
                "the connection closes after the linger, not before");
    U64 bridges = 1;
    for(U64 deadline = now_time_us()+5*Million(1); (bridges = uishell_shared_diag_bridges(scratch.arena, &d)) != 0 && now_time_us() < deadline;) { sleep_ms(50); }
    SharedCheck(bridges == 0, "closing the connection ends its ssh bridge");
  }

  //- 6. Scrolling one View into history leaves the other View live.
  {
    CFG_Node *pair[2] = {uishell_shared_diag_view(&d, local, two, str8_zero()), uishell_shared_diag_view(&d, local, two, str8_zero())};
    UIShell_TerminalViewState *live = uishell_shared_diag_state(&d, pair[0]), *history = uishell_shared_diag_state(&d, pair[1]);
    SharedWait(pair, 2, uishell_shared_diag_streaming(live) && uishell_shared_diag_streaming(history) &&
               uishell_shared_diag_role(live) == CLEAT_ROLE_CONTROLLER);
    String8 fill = str8_lit("seq 1 300\r");
    if(live->shared->view) { cleat_view_write_bytes(live->shared->view, fill.str, fill.size); }
    SharedWait(pair, 2, history->cell_cache.scrollbar.total_rows > history->cell_cache.scrollbar.viewport_rows + 100 &&
               live->cell_cache.scrollbar.total_rows > live->cell_cache.scrollbar.viewport_rows + 100);
    uishell_terminal_scroll_rows(history, -50);
    SharedWait(pair, 2, history->selection_viewport_kind == CLEAT_VIEWPORT_NORMAL_SCROLLBACK);
    SharedCheck(history->selection_viewport_kind == CLEAT_VIEWPORT_NORMAL_SCROLLBACK, "a scrolled View shows history");
    for(U64 i = 0; i < 10; i++) { uishell_shared_diag_frame(&d, pair, 2); sleep_ms(20); }
    SharedCheck(live->selection_viewport_kind == CLEAT_VIEWPORT_LIVE_NORMAL &&
                live->cell_cache.scrollbar.viewport_top_row + live->cell_cache.scrollbar.viewport_rows >= live->cell_cache.scrollbar.total_rows,
                "the other View stays at the live bottom");
    U64 top = history->cell_cache.scrollbar.viewport_top_row;
    // New output from outside Wheelhouse, so no View's input moves it.
    uishell_shared_diag_run(scratch.arena, "'%S' --runtime-root '%S' send '%S' 'echo WH-LIVE-$((6*9))' 2>&1", d.cleat, d.root, two);
    B32 marked = 0;
    U64 deadline = now_time_us()+10*Million(1);
    for(;;)
    {
      uishell_shared_diag_frame(&d, pair, 2);
      Temp t = temp_begin(scratch.arena);
      UIShell_TerminalCellFeed feed = uishell_terminal_cell_feed_from_cache(&live->cell_cache);
      String8 text = live->cell_cache.cells ? uishell_terminal_selection_text_from_feed(t.arena, &feed, txt_pt(0, 0),
        txt_pt(live->cell_cache.rows-1, live->cell_cache.cols-1), 0) : str8_zero();
      marked = str8_find_needle(text, 0, str8_lit("WH-LIVE-54"), 0) < text.size;
      temp_end(t);
      if(marked || now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    SharedCheck(marked && live->selection_viewport_kind == CLEAT_VIEWPORT_LIVE_NORMAL, "the other View stays live and shows new output");
    SharedCheck(history->selection_viewport_kind == CLEAT_VIEWPORT_NORMAL_SCROLLBACK && history->cell_cache.scrollbar.viewport_top_row == top,
                "the scrolled View keeps its place in history");
    uishell_shared_diag_release(&d, pair[0]);
    uishell_shared_diag_release(&d, pair[1]);
    uishell_cleat_attachments_end_frame();
  }

  //- 7. With Direct Cleat Attach off, a subject's terminal opens through its
  // attach command (the recipe path): one process per View, no attachment.
  {
    CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
    CFG_Node *setting = cfg_node_child_from_string_or_alloc(rd_state->cfg, user, str8_lit("direct_cleat_attach"));
    cfg_node_new_replace(rd_state->cfg, setting, str8_lit("0"));
    String8 recipe = push_str8f(scratch.arena, "sh -c 'echo WH-PROCESS-$$; exec cat'");
    CFG_Node *pair[2] = {uishell_shared_diag_view(&d, str8_zero(), str8_zero(), recipe), uishell_shared_diag_view(&d, str8_zero(), str8_zero(), recipe)};
    UIShell_TerminalViewState *first = uishell_shared_diag_state(&d, pair[0]), *second = uishell_shared_diag_state(&d, pair[1]);
    String8 pids[2] = {0};
    U64 deadline = now_time_us()+15*Million(1);
    for(;;)
    {
      uishell_shared_diag_frame(&d, pair, 2);
      UIShell_TerminalViewState *tvs[2] = {first, second};
      for(U64 i = 0; i < 2; i++)
      {
        if(pids[i].size != 0 || tvs[i]->cell_cache.cells == 0) { continue; }
        UIShell_TerminalCellFeed feed = uishell_terminal_cell_feed_from_cache(&tvs[i]->cell_cache);
        String8 text = uishell_terminal_selection_text_from_feed(scratch.arena, &feed, txt_pt(0, 0),
          txt_pt(tvs[i]->cell_cache.rows-1, tvs[i]->cell_cache.cols-1), 0);
        U64 at = str8_find_needle(text, 0, str8_lit("WH-PROCESS-"), 0);
        if(at < text.size)
        {
          U64 end = at + 11;
          while(end < text.size && char_is_digit(text.str[end], 10)) { end++; }
          if(end > at + 11) { pids[i] = str8_substr(text, r1u64(at+11, end)); }
        }
      }
      if((pids[0].size != 0 && pids[1].size != 0) || now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    SharedCheck(first->shared == 0 && second->shared == 0 && uishell_cleat_attachment_count() == 0, "the recipe path holds no shared attachment");
    SharedCheck(first->session && second->session && first->session != second->session && first->provider != second->provider,
                "each recipe View has its own session");
    SharedCheck(pids[0].size != 0 && pids[1].size != 0 && !str8_match(pids[0], pids[1], 0), "each recipe View runs its own process");
    uishell_shared_diag_release(&d, pair[0]);
    uishell_shared_diag_release(&d, pair[1]);
  }

  ui_select_state(saved_ui);
  ui_state_release(test_ui);
#undef SharedCheck
  fprintf(stderr, "shared terminal diagnostics: %u failure(s)\n", failures);
  scratch_end(scratch);
  return failures == 0;
}
#undef SharedWait
#else
internal B32
uishell_shared_terminal_diagnostics(RD_WindowState *ws)
{
  (void)ws;
  fprintf(stderr, "FAIL shared terminal: remote endpoints are unsupported on Windows\n");
  return 0;
}
#endif
