// Remote terminal diagnostics (#104): a Terminal View attaches to a Cleat
// endpoint over its ssh bridge, round-trips input and resize, keeps the
// admission warning out of the content, shows a dead bridge as disconnected
// with a reconnect, and reconnects to the same live session.
//
// It needs a running endpoint and a host ssh reaches, named by environment
// (tools/test-remote-terminal.sh sets them up, with a PATH `ssh` shim when no
// sshd is at hand):
//   WH_REMOTE_HOST     the ssh destination
//   WH_REMOTE_CLEAT    the Cleat binary there, also run here to inspect it
//   WH_REMOTE_ROOT     its daemon's runtime root
//   WH_REMOTE_SESSION  a live session in that daemon
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

// Builds `view` once at `size`, as the canvas would; returns the connection
// pill's text, empty when none was built.
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
      if(tv->session && cleat_session_connection_state(tv->session) == CLEAT_SESSION_STREAMING &&
         cleat_session_role(tv->session) == CLEAT_ROLE_CONTROLLER) { break; }
      if(now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    RemoteCheck(tv->remote_backend && tv->session && cleat_session_connection_state(tv->session) == CLEAT_SESSION_STREAMING,
                "attaches over the ssh bridge");
    RemoteCheck(pill.size == 0, "a streaming controller shows no pill");
    cleat_str hosting = {0};
    RemoteCheck(tv->session && cleat_session_hosting(tv->session, &hosting) &&
                str8_match(str8((U8 *)hosting.ptr, hosting.len), str8_lit("remote:command"), 0), "hosting is remote:command");
    UIShell_RuntimeSetting setting = uishell_runtime_setting(scratch.arena, view, str8_lit("terminal_hosting"));
    RemoteCheck(setting.command.size == 0, "remote hosting offers no move");
    UIShell_CleatTransportState transport = uishell_cleat_transport_state(scratch.arena, tv->provider);
    RemoteCheck(str8_find_needle(transport.warning, 0, str8_lit("cycle protection"), 0) < transport.warning.size,
                "admission warning reaches the transport diagnostics");

    // Input and resize reach the remote PTY.
    String8 input = str8_lit("echo WH-REMOTE-$((6*7))\r");
    if(tv->session) { cleat_session_write_bytes(tv->session, input.str, input.size); }
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

    // A dead bridge: disconnected with a reconnect, the session still alive.
    uishell_remote_diag_run(scratch.arena, "pkill -f 'proxy --runtime-root %S' ; pkill -f 'BatchMode=yes -- %S' ; true", root, host);
    U64 killed = now_time_us();
    deadline = killed+10*Million(1);
    for(;;)
    {
      pill = uishell_remote_diag_build(scratch.arena, ws, view, size);
      if(str8_find_needle(pill, 0, str8_lit("click to reconnect"), 0) < pill.size || now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    RemoteCheck(str8_find_needle(pill, 0, str8_lit("click to reconnect"), 0) < pill.size, "a dead bridge offers reconnect");
    RemoteCheck(now_time_us()-killed < 3*Million(1), "the disconnect shows within 3 s");
    RemoteCheck(tv->session && cleat_session_connection_state(tv->session) == CLEAT_SESSION_DISCONNECTED, "the session reads disconnected, not exited");
    String8 list = uishell_remote_diag_run(scratch.arena, "'%S' --runtime-root '%S' list 2>&1", cleat, root);
    RemoteCheck(str8_find_needle(list, 0, session, 0) < list.size, "the remote session keeps running");
    sleep_ms(300);
    pill = uishell_remote_diag_build(scratch.arena, ws, view, size);
    RemoteCheck(str8_find_needle(pill, 0, str8_lit("click to reconnect"), 0) < pill.size, "nothing reconnects until asked");

    // Reconnect: a fresh bridge to the same live session.
    tv->remote_reconnect = 1;
    deadline = now_time_us()+15*Million(1);
    for(;;)
    {
      pill = uishell_remote_diag_build(scratch.arena, ws, view, size);
      tv = rd_view_state(UIShell_TerminalViewState);
      if((tv->session && cleat_session_connection_state(tv->session) == CLEAT_SESSION_STREAMING) || now_time_us() > deadline) { break; }
      sleep_ms(20);
    }
    RemoteCheck(!tv->remote_reconnect && tv->session && cleat_session_connection_state(tv->session) == CLEAT_SESSION_STREAMING, "reconnect restores streaming");
    input = str8_lit("echo WH-AGAIN-$((6*8))\r");
    if(tv->session) { cleat_session_write_bytes(tv->session, input.str, input.size); }
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
