// Remote terminal diagnostics (#104): a Terminal View attaches to a Cleat
// endpoint over its connection's ssh bridge, round-trips input and resize,
// keeps the admission warning out of the content, re-attaches to the same
// live session when a bridge that streamed dies, reconnects when asked, and
// shows a bridge that never streams as disconnected with a reconnect, and
// keeps the last frame through a resize once its host is gone.
//
// It needs a running endpoint and a host ssh reaches, named by environment
// (tools/test-remote-terminal.sh sets them up, with a PATH `ssh` shim when no
// sshd is at hand):
//   WH_REMOTE_HOST     the ssh destination
//   WH_REMOTE_CLEAT    the Cleat binary there, also run here to inspect it
//   WH_REMOTE_ROOT     its daemon's runtime root
//   WH_REMOTE_SESSION  a live session in that daemon
//   WH_REMOTE_DOWN     optional: a path whose existence makes the host
//                      unreachable (the ssh shim's); without it the
//                      host-gone check is skipped
// Remote endpoints are Unix only, like Cleat's command transport.

#if !OS_WINDOWS

internal String8
uishell_remote_diag_run(Arena *arena, char *fmt, ...)
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

// Whether `needle` is in the View's stored frame, read as ASCII row by row.
internal B32
uishell_remote_diag_frame_has(UIShell_TerminalCellCache *cache, String8 needle)
{
  if(cache->cells == 0) { return 0; }
  Temp scratch = scratch_begin(0, 0);
  U8 *text = push_array(scratch.arena, U8, cache->cell_count);
  for(U64 i = 0; i < cache->cell_count; i++)
  {
    cleat_cell *cell = &cache->cells[i];
    text[i] = cell->grapheme_count != 0 && cell->graphemes[0] < 128 ? (U8)cell->graphemes[0] : ' ';
  }
  B32 found = str8_find_needle(str8(text, cache->cell_count), 0, needle, 0) < cache->cell_count;
  scratch_end(scratch);
  return found;
}

// Builds `view` once at `size`, as the canvas would, and ends the frame;
// returns the connection pill's text, empty when none was built.
internal String8
uishell_remote_diag_build(Arena *arena, RD_WindowState *ws, CFG_Node *view, Vec2F32 size)
{
  String8 pill_text = str8_zero();
  UI_EventList events = {0};
  UI_AnimationInfo animation = {0};
  ui_begin_build(ws->os, &events, &ws->ui->icon_info, ws->theme, &animation, 1.f/60, 1.f/60);
  UI_Key root_key = ui_key_from_string(ui_active_seed_key(), str8_lit("terminal_root"));
  UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
  {
    wh_view_ui_direct(wh_renderer_from_name(str8_lit("terminal")), rd_view_address_from_cfg(view), r2f32p(0, 0, size.x, size.y));
  }
  UI_Box *pill = ui_box_from_key(ui_key_from_string(root_key, str8_lit("terminal_connection_status")));
  if(!ui_box_is_nil(pill) && pill->last_touched_build_index == ui_state->build_index) { pill_text = push_str8_copy(arena, pill->string); }
  ui_end_build();
  // The frame ends, as uishell's does: connections watch their transports.
  uishell_cleat_attachments_end_frame();
  return pill_text;
}

internal B32
uishell_remote_terminal_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  U32 failures = 0;
#define RemoteCheck(condition, label) do { B32 passed = !!(condition); failures += !passed; fprintf(stderr, "%s remote terminal: %s\n", passed ? "PASS" : "FAIL", label); } while(0)
  char *env_host = getenv("WH_REMOTE_HOST"), *env_cleat = getenv("WH_REMOTE_CLEAT");
  char *env_root = getenv("WH_REMOTE_ROOT"), *env_session = getenv("WH_REMOTE_SESSION");
  if(!env_host || !env_cleat || !env_root || !env_session)
  {
    fprintf(stderr, "FAIL remote terminal: WH_REMOTE_HOST, WH_REMOTE_CLEAT, WH_REMOTE_ROOT and WH_REMOTE_SESSION are required\n");
    scratch_end(scratch);
    return 0;
  }
  String8 host = str8_cstring(env_host), cleat = str8_cstring(env_cleat);
  String8 root = str8_cstring(env_root), session = str8_cstring(env_session);
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *owner = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
  RD_PanelAddress panel = rd_fixture_leaf(rd_fixture_owner(owner));
  CFG_Node *view = rd_fixture_view(panel, str8_lit("terminal"), str8_zero(), 1);
  String8 settings[][2] =
  {
    {str8_lit("cleat_transport"), str8_lit("ssh")}, {str8_lit("cleat_host"), host},
    {str8_lit("cleat_runtime_root"), root}, {str8_lit("cleat_session"), session}, {str8_lit("cleat_command"), cleat},
  };
  for(U64 i = 0; i < ArrayCount(settings); i++)
  { cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, view, settings[i][0]), settings[i][1]); }
  UI_State *saved_ui = ui_state, *test_ui = ui_state_alloc();
  ui_select_state(test_ui);
  UIShell_RegsScope(.window = ws->cfg_id, .panel = panel, .tab = rd_view_address_from_cfg(view), .view = rd_view_address_from_cfg(view))
  {
    UIShell_TerminalViewState *tv = rd_view_state(UIShell_TerminalViewState);
    Vec2F32 size = v2f32(640, 480);
    U64 deadline = now_time_us()+15*Million(1);
    String8 pill = str8_zero();
    for(;;)
    {
      pill = uishell_remote_diag_build(scratch.arena, ws, view, size);
      if(uishell_terminal_connection_state(tv) == CLEAT_SESSION_STREAMING &&
         uishell_terminal_role(tv) == CLEAT_ROLE_CONTROLLER) { break; }
      if(now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    RemoteCheck(tv->remote_backend && tv->shared && uishell_terminal_connection_state(tv) == CLEAT_SESSION_STREAMING,
                "attaches over the ssh bridge");
    RemoteCheck(pill.size == 0, "a streaming controller shows no pill");
    cleat_str hosting = {0};
    RemoteCheck(uishell_terminal_session(tv) && cleat_session_hosting(uishell_terminal_session(tv), &hosting) &&
                str8_match(str8((U8 *)hosting.ptr, hosting.len), str8_lit("remote:command"), 0), "hosting is remote:command");
    UIShell_RuntimeSetting setting = uishell_runtime_setting(scratch.arena, view, str8_lit("terminal_hosting"));
    RemoteCheck(setting.command.size == 0, "remote hosting offers no move");
    UIShell_CleatConnection *connection = tv->shared ? tv->shared->attachment->connection : 0;
    UIShell_CleatTransportState transport = uishell_cleat_transport_state(scratch.arena, connection ? connection->provider : 0);
    RemoteCheck(str8_find_needle(transport.warning, 0, str8_lit("cycle protection"), 0) < transport.warning.size,
                "admission warning reaches the transport diagnostics");

    // Input and resize reach the remote PTY.
    String8 input = str8_lit("echo WH-REMOTE-$((6*7))\r");
    if(uishell_terminal_cleat_view(tv)) { cleat_view_write_bytes(uishell_terminal_cleat_view(tv), input.str, input.size); }
    String8 capture = str8_zero();
    deadline = now_time_us()+10*Million(1);
    for(;;)
    {
      uishell_remote_diag_build(scratch.arena, ws, view, size);
      capture = uishell_remote_diag_run(scratch.arena, "'%S' --runtime-root '%S' capture '%S' 2>&1", cleat, root, session);
      if(str8_find_needle(capture, 0, str8_lit("WH-REMOTE-42"), 0) < capture.size || now_time_us() > deadline) { break; }
      sleep_ms(50);
    }
    RemoteCheck(str8_find_needle(capture, 0, str8_lit("WH-REMOTE-42"), 0) < capture.size, "input reaches the remote PTY");
    RemoteCheck(str8_find_needle(capture, 0, str8_lit("cycle protection"), 0) >= capture.size, "admission warning stays out of the content");
    U16 cols_before = tv->cols;
    size = v2f32(960, 600);
    String8 inspect = str8_zero();
    deadline = now_time_us()+10*Million(1);
    for(;;)
    {
      uishell_remote_diag_build(scratch.arena, ws, view, size);
      inspect = uishell_remote_diag_run(scratch.arena, "'%S' --runtime-root '%S' inspect '%S' --json 2>&1", cleat, root, session);
      String8 cols = push_str8f(scratch.arena, "\"cols\": %u", tv->cols);
      if((tv->cols != cols_before && str8_find_needle(inspect, 0, cols, 0) < inspect.size) || now_time_us() > deadline) { break; }
      sleep_ms(50);
    }
    RemoteCheck(tv->cols != cols_before && str8_find_needle(inspect, 0, push_str8f(scratch.arena, "\"cols\": %u", tv->cols), 0) < inspect.size,
                "resize reaches the remote PTY");

    // A dead bridge that streamed: its connection reopens by itself and
    // re-attaches the same live session. Both patterns name this run's
    // runtime root, so no other ssh session to the host is touched.
    U64 opens = connection ? connection->opens : 0;
    uishell_remote_diag_run(scratch.arena, "pkill -f 'proxy --runtime-root=%S' ; pkill -f 'BatchMode=yes -- %S .*--runtime-root=%S' ; true", root, host, root);
    deadline = now_time_us()+15*Million(1);
    for(;;)
    {
      pill = uishell_remote_diag_build(scratch.arena, ws, view, size);
      if((connection && connection->opens > opens && uishell_terminal_connection_state(tv) == CLEAT_SESSION_STREAMING) || now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    RemoteCheck(connection && connection->opens == opens+1 && uishell_terminal_connection_state(tv) == CLEAT_SESSION_STREAMING,
                "a dead bridge reopens and re-attaches by itself");
    String8 list = uishell_remote_diag_run(scratch.arena, "'%S' --runtime-root '%S' list 2>&1", cleat, root);
    RemoteCheck(str8_find_needle(list, 0, session, 0) < list.size, "the remote session keeps running");

    // Reconnect when asked: a fresh bridge to the same live session.
    opens = connection ? connection->opens : 0;
    uishell_cleat_connection_reconnect(connection);
    deadline = now_time_us()+15*Million(1);
    for(;;)
    {
      pill = uishell_remote_diag_build(scratch.arena, ws, view, size);
      if((connection && connection->opens > opens && uishell_terminal_connection_state(tv) == CLEAT_SESSION_STREAMING) || now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    RemoteCheck(connection && connection->opens == opens+1 && uishell_terminal_connection_state(tv) == CLEAT_SESSION_STREAMING &&
                pill.size == 0, "reconnect restores streaming");
    input = str8_lit("echo WH-AGAIN-$((6*8))\r");
    if(uishell_terminal_cleat_view(tv)) { cleat_view_write_bytes(uishell_terminal_cleat_view(tv), input.str, input.size); }
    deadline = now_time_us()+10*Million(1);
    for(;;)
    {
      uishell_remote_diag_build(scratch.arena, ws, view, size);
      capture = uishell_remote_diag_run(scratch.arena, "'%S' --runtime-root '%S' capture '%S' 2>&1", cleat, root, session);
      if(str8_find_needle(capture, 0, str8_lit("WH-AGAIN-48"), 0) < capture.size || now_time_us() > deadline) { break; }
      sleep_ms(50);
    }
    RemoteCheck(str8_find_needle(capture, 0, str8_lit("WH-AGAIN-48"), 0) < capture.size &&
                str8_find_needle(capture, 0, str8_lit("WH-REMOTE-42"), 0) < capture.size, "reconnect reaches the same live session");

    // The host goes away (a vessel shut down): the bridge dies and can't
    // reopen, so the connection gives up. Shrinking and growing the View
    // then has nothing to ask, and the last frame shows again in full.
    char *env_down = getenv("WH_REMOTE_DOWN");
    if(env_down != 0 && connection != 0)
    {
      deadline = now_time_us()+10*Million(1);
      for(;;)
      {
        uishell_remote_diag_build(scratch.arena, ws, view, size);
        if(uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-AGAIN-48")) || now_time_us() > deadline) { break; }
        sleep_ms(20);
      }
      U16 cols_full = tv->cell_cache.cols, rows_full = tv->cell_cache.rows;
      B32 shown = uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-AGAIN-48"));
      // A host that's gone doesn't refuse: ssh hangs until it times out.
      // Resize while it hangs, then again once it has given up.
      uishell_remote_diag_run(scratch.arena, "echo 4 > '%s' ; pkill -f 'proxy --runtime-root=%S' ; pkill -f 'BatchMode=yes -- %S .*--runtime-root=%S' ; true",
                              env_down, root, host, root);
      for(U64 i = 0; i < 25; i++) { uishell_remote_diag_build(scratch.arena, ws, view, v2f32(size.x*0.5f, size.y*0.5f)); sleep_ms(20); }
      fprintf(stderr, "note remote terminal: while hanging, shrunk: state %u, frame %ux%u, text %d\n", uishell_terminal_connection_state(tv),
              tv->cell_cache.cols, tv->cell_cache.rows, uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-AGAIN-48")));
      for(U64 i = 0; i < 25; i++) { uishell_remote_diag_build(scratch.arena, ws, view, size); sleep_ms(20); }
      fprintf(stderr, "note remote terminal: while hanging, grown: state %u, frame %ux%u, text %d\n", uishell_terminal_connection_state(tv),
              tv->cell_cache.cols, tv->cell_cache.rows, uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-AGAIN-48")));
      deadline = now_time_us()+15*Million(1);
      for(;;)
      {
        pill = uishell_remote_diag_build(scratch.arena, ws, view, size);
        if(connection->given_up || now_time_us() > deadline) { break; }
        sleep_ms(20);
      }
      // The frame that saw it give up built its pill before it did.
      pill = uishell_remote_diag_build(scratch.arena, ws, view, size);
      fprintf(stderr, "note remote terminal: shown %d, given up %d, opens %llu, state %u, pill '%.*s'\n", shown, connection->given_up,
              (unsigned long long)connection->opens, uishell_terminal_connection_state(tv), (int)pill.size, pill.str);
      RemoteCheck(shown && connection->given_up && str8_find_needle(pill, 0, str8_lit("click to reconnect"), 0) < pill.size,
                  "a bridge whose host is gone gives up");
      RemoteCheck(uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-AGAIN-48")), "a bridge that gave up keeps its last frame");
      for(U64 i = 0; i < 10; i++) { uishell_remote_diag_build(scratch.arena, ws, view, v2f32(size.x*0.5f, size.y*0.5f)); sleep_ms(20); }
      for(U64 i = 0; i < 10; i++) { uishell_remote_diag_build(scratch.arena, ws, view, size); sleep_ms(20); }
      fprintf(stderr, "note remote terminal: frame %ux%u before, %ux%u after shrink and grow\n",
              cols_full, rows_full, tv->cell_cache.cols, tv->cell_cache.rows);
      RemoteCheck(tv->cell_cache.cols == cols_full && tv->cell_cache.rows == rows_full &&
                  uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-AGAIN-48")),
                  "a shrink and grow while given up keeps the last frame");
      remove(env_down);
    }
    uishell_terminal_runtime_release(tv);
  }
  // A bridge that fails before it ever streams (here, no Cleat at that
  // path, as a bad host or refused auth would be) gives up with its reason,
  // logged once.
  CFG_Node *bad = rd_fixture_view(panel, str8_lit("terminal"), str8_zero(), 1);
  String8 bad_settings[][2] =
  {
    {str8_lit("cleat_transport"), str8_lit("ssh")}, {str8_lit("cleat_host"), host}, {str8_lit("cleat_runtime_root"), root},
    {str8_lit("cleat_session"), session}, {str8_lit("cleat_command"), str8_lit("/nonexistent/cleat")},
  };
  for(U64 i = 0; i < ArrayCount(bad_settings); i++)
  { cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, bad, bad_settings[i][0]), bad_settings[i][1]); }
  UIShell_RegsScope(.window = ws->cfg_id, .panel = panel, .tab = rd_view_address_from_cfg(bad), .view = rd_view_address_from_cfg(bad))
  {
    UIShell_TerminalViewState *tv = rd_view_state(UIShell_TerminalViewState);
    String8 pill = str8_zero();
    U64 deadline = now_time_us()+10*Million(1);
    for(;;)
    {
      pill = uishell_remote_diag_build(scratch.arena, ws, bad, v2f32(640, 480));
      if(str8_find_needle(pill, 0, str8_lit("click to reconnect"), 0) < pill.size || now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    RemoteCheck(str8_match(str8_prefix(pill, 14), str8_lit("disconnected: "), 0) && str8_find_needle(pill, 0, str8_lit("click to reconnect"), 0) < pill.size,
                "a bridge that never streams gives up with its reason");
    RemoteCheck(tv->remote_disconnect_logged, "the startup failure reaches the log");
    uishell_terminal_runtime_release(tv);
  }
  // An unknown transport opens nothing, so it can't attach a same-named local
  // session; it offers the reconnect that re-reads its settings.
  CFG_Node *typo = rd_fixture_view(panel, str8_lit("terminal"), str8_zero(), 1);
  String8 typo_settings[][2] =
  {
    {str8_lit("cleat_transport"), str8_lit("ssh ")}, {str8_lit("cleat_host"), host}, {str8_lit("cleat_runtime_root"), root},
    {str8_lit("cleat_session"), session}, {str8_lit("cleat_command"), cleat},
  };
  for(U64 i = 0; i < ArrayCount(typo_settings); i++)
  { cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, typo, typo_settings[i][0]), typo_settings[i][1]); }
  UIShell_RegsScope(.window = ws->cfg_id, .panel = panel, .tab = rd_view_address_from_cfg(typo), .view = rd_view_address_from_cfg(typo))
  {
    UIShell_TerminalViewState *tv = rd_view_state(UIShell_TerminalViewState);
    String8 pill = str8_zero();
    for(U64 i = 0; i < 25; i++) { pill = uishell_remote_diag_build(scratch.arena, ws, typo, v2f32(640, 480)); sleep_ms(20); }
    RemoteCheck(tv->provider == 0 && tv->session == 0 && tv->shared == 0 && str8_find_needle(pill, 0, str8_lit("click to reconnect"), 0) < pill.size,
                "an unknown transport attaches nothing and offers reconnect");
    uishell_terminal_runtime_release(tv);
  }
  // A local endpoint attaches through the shared daemon provider for its
  // runtime root, with no bridge.
  CFG_Node *local = rd_fixture_view(panel, str8_lit("terminal"), str8_zero(), 1);
  String8 local_settings[][2] =
  {
    {str8_lit("cleat_transport"), str8_lit("local")}, {str8_lit("cleat_runtime_root"), root}, {str8_lit("cleat_session"), session},
  };
  for(U64 i = 0; i < ArrayCount(local_settings); i++)
  { cfg_node_new(rd_state->cfg, cfg_node_new(rd_state->cfg, local, local_settings[i][0]), local_settings[i][1]); }
  UIShell_RegsScope(.window = ws->cfg_id, .panel = panel, .tab = rd_view_address_from_cfg(local), .view = rd_view_address_from_cfg(local))
  {
    UIShell_TerminalViewState *tv = rd_view_state(UIShell_TerminalViewState);
    U64 deadline = now_time_us()+15*Million(1);
    for(;;)
    {
      uishell_remote_diag_build(scratch.arena, ws, local, v2f32(640, 480));
      if(uishell_terminal_connection_state(tv) == CLEAT_SESSION_STREAMING || now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    cleat_str hosting = {0};
    RemoteCheck(!tv->remote_backend && tv->shared && uishell_terminal_connection_state(tv) == CLEAT_SESSION_STREAMING &&
                cleat_session_hosting(uishell_terminal_session(tv), &hosting) && str8_match(str8_prefix(str8((U8 *)hosting.ptr, hosting.len), 7), str8_lit("daemon:"), 0),
                "a local endpoint attaches through its daemon");
    uishell_terminal_runtime_release(tv);
  }
  // A terminal running a command (a subject's recipe, like `flotilla
  // attach`) whose program exits: shrinking and growing the View keeps the
  // last frame, including the parts the shrink hid.
  CFG_Node *recipe = rd_fixture_view(panel, str8_lit("terminal"),
    str8_lit("sh -c 'printf \"\\033[?1049h%60s\\n\" WH-RIGHT; i=0; while [ $i -lt 18 ]; do echo; i=$((i+1)); done; echo WH-BOTTOM; sleep 3'"), 1);
  UIShell_RegsScope(.window = ws->cfg_id, .panel = panel, .tab = rd_view_address_from_cfg(recipe), .view = rd_view_address_from_cfg(recipe))
  {
    UIShell_TerminalViewState *tv = rd_view_state(UIShell_TerminalViewState);
    Vec2F32 size = v2f32(960, 600);
    U64 deadline = now_time_us()+15*Million(1);
    B32 shown = 0;
    String8 recipe_pill = {0};
    for(;;)
    {
      recipe_pill = uishell_remote_diag_build(scratch.arena, ws, recipe, size);
      shown |= uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-BOTTOM")) && uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-RIGHT"));
      if((shown && uishell_terminal_program_state(tv, 0) == CLEAT_PROGRAM_EXITED) || now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    recipe_pill = uishell_remote_diag_build(scratch.arena, ws, recipe, size);
    U16 cols_full = tv->cell_cache.cols, rows_full = tv->cell_cache.rows;
    fprintf(stderr, "note remote terminal: recipe program %u, frame %ux%u, pill '%.*s'\n", uishell_terminal_program_state(tv, 0),
            cols_full, rows_full, (int)recipe_pill.size, recipe_pill.str);
    RemoteCheck(shown && uishell_terminal_program_state(tv, 0) == CLEAT_PROGRAM_EXITED, "a recipe's program shows its output and exits");
    RemoteCheck(str8_match(recipe_pill, str8_lit("exited (status 0)"), 0), "an exited recipe says how its program exited");
    for(U64 i = 0; i < 10; i++) { uishell_remote_diag_build(scratch.arena, ws, recipe, v2f32(size.x*0.4f, size.y*0.4f)); sleep_ms(20); }
    fprintf(stderr, "note remote terminal: recipe shrunk: frame %ux%u, right %d, bottom %d\n", tv->cell_cache.cols, tv->cell_cache.rows,
            uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-RIGHT")), uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-BOTTOM")));
    for(U64 i = 0; i < 10; i++) { uishell_remote_diag_build(scratch.arena, ws, recipe, size); sleep_ms(20); }
    fprintf(stderr, "note remote terminal: recipe grown: frame %ux%u\n", tv->cell_cache.cols, tv->cell_cache.rows);
    RemoteCheck(tv->cell_cache.cols == cols_full && tv->cell_cache.rows == rows_full &&
                uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-RIGHT")) && uishell_remote_diag_frame_has(&tv->cell_cache, str8_lit("WH-BOTTOM")),
                "an exited recipe keeps its last frame through a shrink and grow");
    uishell_terminal_runtime_release(tv);
  }
  ui_select_state(saved_ui);
  ui_state_release(test_ui);
#undef RemoteCheck
  fprintf(stderr, "remote terminal diagnostics: %u failure(s)\n", failures);
  scratch_end(scratch);
  return failures == 0;
}
#else
internal B32
uishell_remote_terminal_diagnostics(RD_WindowState *ws)
{
  (void)ws;
  fprintf(stderr, "FAIL remote terminal: remote endpoints are unsupported on Windows\n");
  return 0;
}
#endif
