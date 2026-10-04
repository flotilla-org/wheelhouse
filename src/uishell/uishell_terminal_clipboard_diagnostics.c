// Provider acquisitions and native writes are the only faked process boundaries.
// This diagnostic never opens, reads or overwrites a desktop clipboard.
typedef struct UIShell_ClipboardFixture UIShell_ClipboardFixture;
struct UIShell_ClipboardFixture
{
  cleat_clipboard_event events[32];
  U64 count, acquired, released, writes, clears, lost, context_calls;
  U32 destination;
  U8 text[KB(64)+1];
  String8 last_text;
  UIShell_TerminalClipboardContext contexts[2];
  UIShell_TerminalViewState views[2];
  B32 sink_failure;
};
internal cleat_clipboard_event const *
uishell_clipboard_fixture_acquire(void *user, cleat_session *session)
{
  UIShell_ClipboardFixture *f = user;
  return f->acquired < f->count ? &f->events[f->acquired++] : 0;
}
internal void
uishell_clipboard_fixture_release(void *user, cleat_clipboard_event const *event)
{ ((UIShell_ClipboardFixture *)user)->released++; }
internal U64
uishell_clipboard_fixture_dropped(void *user, cleat_session *session)
{ return ((UIShell_ClipboardFixture *)user)->lost; }
internal B32
uishell_clipboard_fixture_write(void *user, U32 destination, B32 clear, String8 text)
{
  UIShell_ClipboardFixture *f = user;
  if(f->sink_failure) { return 0; }
  f->writes++; f->clears += clear; f->destination = destination;
  f->last_text = clear ? str8_zero() : text;
  return 1;
}
internal UIShell_TerminalClipboardContext
uishell_clipboard_fixture_context(void *user, UIShell_TerminalViewState *tv)
{
  UIShell_ClipboardFixture *f = user;
  f->context_calls++;
  return f->contexts[tv == &f->views[1]];
}
internal void
uishell_clipboard_fixture_event(UIShell_ClipboardFixture *f, U64 sequence, U32 destination, U32 kind, String8 text)
{
  f->count = 1; f->acquired = f->released = 0;
  f->events[0] = (cleat_clipboard_event){.session_epoch = {1}, .connection_epoch = 1, .sequence = sequence,
    .destination = destination, .kind = kind, .text = text.str, .text_len = text.size};
}

internal UIShell_TerminalClipboardContext
uishell_clipboard_fixture_host_context(void *user, UIShell_TerminalViewState *tv)
{
  UIShell_ClipboardFixture *f = user;
  // Only OS window focus and provider role/capability are injected boundaries.
  f->context_calls++;
  return uishell_terminal_clipboard_host_context(tv, f->contexts[0].window_active,
    f->contexts[0].controller, f->contexts[0].supported);
}

internal B32
uishell_terminal_clipboard_host_checks(Arena *arena, CFG_State *cfg)
{
  rd_state->cfg = cfg; rd_state->frame_index = 10;
  UIShell_ClipboardFixture *f = push_array(arena, UIShell_ClipboardFixture, 1);
  UIShell_TerminalViewState *tv = &f->views[0];
  tv->session = (cleat_session *)tv; tv->focus_active = 1; tv->input_frame = 9;
  f->contexts[0] = (UIShell_TerminalClipboardContext){.allowed=1, .input_owner=1, .window_active=1, .live=1, .controller=1, .supported=1};
  CFG_Node *user = cfg_node_new(cfg, cfg_node_root(), str8_lit("user"));
  CFG_Node *window = cfg_node_new(cfg, user, str8_lit("window"));
  CFG_Node *workspace = cfg_node_new(cfg, window, str8_lit("workspace"));
  CFG_Node *panels = cfg_node_new(cfg, workspace, str8_lit("panels"));
  cfg_node_new(cfg, panels, str8_lit("selected"));
  CFG_Node *view = cfg_node_new(cfg, panels, str8_lit("terminal"));
  CFG_Node *selected = cfg_node_new(cfg, view, str8_lit("selected"));
  CFG_Node *watcher = cfg_node_new(cfg, panels, str8_lit("terminal"));
  CFG_Node *other_workspace = cfg_node_new(cfg, window, str8_lit("workspace"));
  CFG_Node *other_panels = cfg_node_new(cfg, other_workspace, str8_lit("panels"));
  cfg_node_new(cfg, other_panels, str8_lit("selected"));
  CFG_Node *preview = cfg_node_new(cfg, other_panels, str8_lit("terminal"));
  cfg_node_new(cfg, preview, str8_lit("selected"));
  RD_WindowState *ws = push_array(arena, RD_WindowState, 1);
  ws->ui = push_array(arena, UI_State, 1);
  ws->ui->edit_owner_terminal = 1; ws->ui->edit_owner_user = tv; ws->ui->edit_owner_view = view->id;
  ws->cfg_id = window->id; ws->root_controlled_split_initialized = 1;
  ws->root_controlled_split_selected_workspace_id = workspace->id;
  rd_state->window_state_last_accessed_id = window->id; rd_state->window_state_last_accessed = ws;
  RD_ViewState *vs = push_array(arena, RD_ViewState, 1);
  vs->cfg_id = view->id; vs->user_data = tv;
  rd_state->view_state_last_accessed_id = view->id; rd_state->view_state_last_accessed = vs;
  uishell_terminal_clipboard_views = 0;
  uishell_terminal_clipboard_register(tv, view->id, window->id);
  UIShell_TerminalClipboardOps ops = {uishell_clipboard_fixture_acquire, uishell_clipboard_fixture_release,
    uishell_clipboard_fixture_write, uishell_clipboard_fixture_host_context, uishell_clipboard_fixture_dropped, f};
  B32 ok = 1;
#define HostClipboardCheck(c, why) do { if(!(c)) { fprintf(stderr, "clipboard host: %s\n", why); ok = 0; } } while(0)
  U64 sequence = 0;
  // The actual host config/context resolver delivers without any renderer,
  // render dirty flag, cell feed, provider refresh, or local selection highlight.
  uishell_clipboard_fixture_event(f, ++sequence, 0, 1, str8_lit("effect-only"));
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  HostClipboardCheck(f->writes == 1 && f->released == 1, "real host dispatch with effect-only wake");
  // Generate independent UI ownership transitions before draining, including
  // modal focus, preview/overview, replay, inactivity and capability demotion.
  B32 *gates[] = {&ws->query_is_active, &ws->menu_bar_focused, &ws->hover_eval_focused,
    &rd_state->popup_active, &ws->ui->ctx_menu_open, &ws->ui->next_ctx_menu_open, &ws->workspace_zoom_open, &rd_state->frame_replay.suppress_input, &rd_state->quit};
  for(U64 i = 0; i < ArrayCount(gates)+8; i++)
  {
    U64 before = f->writes;
    if(i < ArrayCount(gates)) { *gates[i] = 1; }
    else switch(i-ArrayCount(gates))
    {
      case 0: tv->focus_active = 0; break;
      case 1: tv->input_frame = 8; break;
      case 2: ws->root_controlled_split_selected_workspace_id = other_workspace->id; break;
      case 3: vs->user_data = 0; break;
      case 4: f->contexts[0].controller = 0; break;
      case 5: f->contexts[0].window_active = 0; break;
      case 6: f->contexts[0].supported = 0; break;
      case 7: ws->ui->edit_owner_user = 0; break;
    }
    uishell_clipboard_fixture_event(f, ++sequence, 0, 1, str8_lit("ineligible"));
    uishell_terminal_clipboard_dispatch_with_ops(&ops);
    HostClipboardCheck(f->writes == before && f->released == 1, "current host context suppresses stale ownership");
    if(i < ArrayCount(gates)) { *gates[i] = 0; }
    tv->focus_active = 1; tv->input_frame = 9; vs->user_data = tv; ws->ui->edit_owner_user = tv;
    ws->root_controlled_split_selected_workspace_id = workspace->id;
    f->contexts[0] = (UIShell_TerminalClipboardContext){.allowed=1, .input_owner=1, .window_active=1, .live=1, .controller=1, .supported=1};
    f->acquired = f->released = 0;
    uishell_terminal_clipboard_dispatch_with_ops(&ops);
    HostClipboardCheck(f->writes == before && f->released == 1, "restoring host focus cannot replay suppressed effects");
  }
  // User deny cannot be overridden by a view setting; denying does not change
  // the independently maintained local selection or operator Copy interface.
  CFG_Node *deny = cfg_node_new(cfg, user, str8_lit("deny_application_clipboard_writes"));
  cfg_node_new(cfg, deny, str8_lit("true"));
  cfg_node_new(cfg, cfg_node_new(cfg, view, str8_lit("deny_application_clipboard_writes")), str8_lit("0"));
  U64 before = f->writes;
  tv->has_selection = 1;
  uishell_clipboard_fixture_event(f, ++sequence, 1, 2, str8_zero());
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  HostClipboardCheck(f->writes == before && f->released == 1 && tv->has_selection, "host deny overrides workspace config");
  cfg_node_release(cfg, deny);
  // Current selected-tab config wins even if the previous build still reports
  // terminal focus. Hydrated/previews and replaced view types are never owners.
  cfg_node_release(cfg, selected);
  CFG_Node *watcher_selected = cfg_node_new(cfg, watcher, str8_lit("selected"));
  uishell_clipboard_fixture_event(f, ++sequence, 0, 1, str8_lit("old tab"));
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  HostClipboardCheck(f->writes == before && f->released == 1, "tab change before drain");
  cfg_node_release(cfg, watcher_selected); cfg_node_new(cfg, view, str8_lit("selected"));
  cfg_node_equip_string(cfg, view, str8_lit("terminal_fixture"));
  uishell_clipboard_fixture_event(f, ++sequence, 0, 1, str8_lit("hydration"));
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  HostClipboardCheck(f->writes == before && f->released == 1, "retained fixture cannot deliver clipboard");
  uishell_terminal_clipboard_unregister(tv);

#undef HostClipboardCheck
  return ok;
}

internal B32
uishell_terminal_clipboard_host_diagnostics(void)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_State *cfg = cfg_state_alloc();
  CFG_Ctx *saved_cfg = cfg_ctx;
  RD_State *saved_rd = rd_state;
  UIShell_TerminalViewState *saved_views = uishell_terminal_clipboard_views;
  cfg_ctx_select(cfg_state_ctx(cfg));
  rd_state = push_array(scratch.arena, RD_State, 1);
  // The checks may return early; the wrapper always restores their global state.
  B32 ok = uishell_terminal_clipboard_host_checks(scratch.arena, cfg);
  uishell_terminal_clipboard_views = saved_views;
  rd_state = saved_rd;
  cfg_ctx_select(saved_cfg); cfg_state_release(cfg);
  scratch_end(scratch);
  return ok;
}

internal B32
uishell_terminal_clipboard_diagnostics(void)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_ClipboardFixture *f = push_array(scratch.arena, UIShell_ClipboardFixture, 1);
  UIShell_TerminalViewState *saved = uishell_terminal_clipboard_views;
  uishell_terminal_clipboard_views = 0;
  f->views[0].session = (cleat_session *)&f->views[0];
  f->contexts[0] = (UIShell_TerminalClipboardContext){.allowed=1, .input_owner=1, .window_active=1, .live=1, .controller=1, .supported=1};
  UIShell_TerminalClipboardOps ops = {uishell_clipboard_fixture_acquire, uishell_clipboard_fixture_release,
    uishell_clipboard_fixture_write, uishell_clipboard_fixture_context, uishell_clipboard_fixture_dropped, f};
  uishell_terminal_clipboard_register(&f->views[0], 1, 1);
  B32 ok = 1;
#define ClipboardCheck(c, why) do { if(!(c)) { fprintf(stderr, "clipboard: %s\n", why); ok = 0; } } while(0)
  U64 seq = 0;
  // Unicode writes and explicit clear affect exactly the requested destination,
  // independently of local selection highlights or screen/render generations.
  f->views[0].has_selection = 1;
  for(U32 destination = 0; destination <= 1; destination++)
  {
    String8 unicode = str8_lit("copy: \xc3\xa9 \xe4\xb8\xad \xf0\x9f\x98\x80");
    U64 before = f->writes;
    uishell_clipboard_fixture_event(f, ++seq, destination, 1, unicode);
    uishell_terminal_clipboard_dispatch_with_ops(&ops);
    ClipboardCheck(f->writes == before+1 && f->destination == destination && str8_match(f->last_text, unicode, 0), "Unicode destination write");
    ClipboardCheck(f->released == 1 && f->views[0].has_selection, "owned event release preserves local selection");
    uishell_clipboard_fixture_event(f, ++seq, destination, 2, str8_zero());
    uishell_terminal_clipboard_dispatch_with_ops(&ops);
    ClipboardCheck(f->writes == before+2 && f->clears == destination+1 && f->last_text.size == 0, "explicit destination clear");
  }
  // Each policy bit independently rejects; re-enabling it never runs a queued
  // or duplicated suppressed effect. Generate all six single-bit focus/role cases.
  for(U32 bit = 0; bit < 6; bit++)
  {
    B32 *bits[] = {&f->contexts[0].allowed, &f->contexts[0].input_owner, &f->contexts[0].window_active,
      &f->contexts[0].live, &f->contexts[0].controller, &f->contexts[0].supported};
    U64 before = f->writes;
    *bits[bit] = 0;
    uishell_clipboard_fixture_event(f, ++seq, 0, 1, str8_lit("suppressed"));
    uishell_terminal_clipboard_dispatch_with_ops(&ops);
    ClipboardCheck(f->writes == before && f->released == 1, "focus/role/deny drop before drain");
    *bits[bit] = 1;
    f->acquired = f->released = 0; // repeat the same live identity
    uishell_terminal_clipboard_dispatch_with_ops(&ops);
    ClipboardCheck(f->writes == before && f->released == 1, "suppressed identity cannot retry on later focus");
  }
  // Unsupported kind/destination and malformed/empty text leave the sink intact.
  String8 invalid[] = {str8_zero(), str8_lit("\0"), str8_lit("\xc0\x80"), str8_lit("\xed\xa0\x80"),
    str8_lit("\xf4\x90\x80\x80"), str8_lit("\xe2\x82"), str8_lit("\x80"), str8_lit("\xe2x\xac")};
  for(U64 i = 0; i < ArrayCount(invalid)+4; i++)
  {
    U64 before = f->writes;
    uishell_clipboard_fixture_event(f, ++seq, i == ArrayCount(invalid) ? 2 : 0,
      i == ArrayCount(invalid)+1 ? 3 : 1, i < ArrayCount(invalid) ? invalid[i] : str8_lit("valid"));
    if(i == ArrayCount(invalid)+2) { f->events[0].text = 0; }
    if(i == ArrayCount(invalid)+3) { f->events[0].kind = 2; }
    uishell_terminal_clipboard_dispatch_with_ops(&ops);
    ClipboardCheck(f->writes == before && f->released == 1, "malformed/unsupported content leaves sink intact");
  }
  // Explicit boundary generator covers max-1, max and max+1 UTF-8 payload bytes.
  MemorySet(f->text, 'x', sizeof(f->text));
  for(U64 size = KB(64)-1; size <= KB(64)+1; size++)
  {
    U64 before = f->writes;
    uishell_clipboard_fixture_event(f, ++seq, 0, 1, str8(f->text, size));
    uishell_terminal_clipboard_dispatch_with_ops(&ops);
    ClipboardCheck(f->writes == before+(size <= KB(64)) && f->released == 1, "64 KiB payload bound");
  }
  // Queue bounds drop newest while releasing every owned event. Generate event
  // and byte boundaries separately, including clear events with no text bytes.
  for(U32 payload = 0; payload <= 1; payload++)
  {
    U64 before = f->writes;
    f->count = 17; f->acquired = f->released = 0;
    for(U64 i = 0; i < f->count; i++)
    {
      f->events[i] = (cleat_clipboard_event){.session_epoch={1}, .connection_epoch=1, .sequence=++seq,
        .kind=payload ? 1 : 2, .text=payload ? f->text : 0, .text_len=payload ? KB(64) : 0};
    }
    uishell_terminal_clipboard_dispatch_with_ops(&ops);
    ClipboardCheck(f->writes == before+(payload ? 4 : 16) && f->released == 17, "queue event/byte bounds");
  }
  // Native adapter refusal must still release and consume its event.
  U64 before = f->writes;
  f->sink_failure = 1;
  uishell_clipboard_fixture_event(f, ++seq, 0, 1, str8_lit("failure"));
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  f->sink_failure = 0;
  f->acquired = f->released = 0;
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  ClipboardCheck(f->writes == before && f->released == 1, "failed native attempt never retries");
  // Redraw, resize, refresh and repeated wakes see an empty queue; none repeat.
  U64 contexts_before_idle = f->context_calls;
  for(U32 wake = 0; wake < 5; wake++) { uishell_terminal_clipboard_dispatch_with_ops(&ops); }
  ClipboardCheck(f->context_calls == contexts_before_idle, "idle queues never resolve host configuration");
  ClipboardCheck(f->writes == before, "empty effect-only wakes never redraw/replay clipboard");
  // Two references to the same session elect the active view before acquisition,
  // then retain the consumed identity when the active reference is removed.
  f->views[1].session = f->views[0].session;
  f->contexts[1] = (UIShell_TerminalClipboardContext){.allowed=1, .input_owner=0, .window_active=0, .live=1, .controller=1, .supported=1};
  uishell_terminal_clipboard_register(&f->views[1], 2, 2);
  uishell_clipboard_fixture_event(f, ++seq, 1, 1, str8_lit("alias"));
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  ClipboardCheck(f->writes == before+1 && f->released == 1, "inactive window alias cannot steal active reference event");
  uishell_terminal_clipboard_unregister(&f->views[1]);
  f->acquired = f->released = 0;
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  ClipboardCheck(f->writes == before+1, "alias retirement preserves consumption watermark");
  // Generate higher activation with reset sequence, then older activation with
  // higher sequence. Only the new activation is fresh. Hosting changes actor ID.
  uishell_clipboard_fixture_event(f, ++seq, 0, 1, str8_lit("reconnected"));
  f->events[0].connection_epoch = 2;
  f->events[0].sequence = 1; // A higher activation is fresh even if its sequence resets.
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  uishell_clipboard_fixture_event(f, ++seq, 0, 1, str8_lit("old activation"));
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  ClipboardCheck(f->writes == before+2, "old connection effect rejected");
  uishell_clipboard_fixture_event(f, 1, 0, 1, str8_lit("new actor"));
  f->events[0].session_epoch[0] = 2;
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  ClipboardCheck(f->writes == before+3, "new actor epoch after hosting transfer");
  // Session replacement clears old view ownership before admitting a new child.
  f->views[0].session = (cleat_session *)&f->views[1]; f->contexts[0].input_owner = 0;
  uishell_clipboard_fixture_event(f, 1, 0, 1, str8_lit("replacement startup"));
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  f->contexts[0].input_owner = 1;
  f->acquired = f->released = 0;
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  ClipboardCheck(f->writes == before+3, "replacement cannot inherit old focus or replay startup");
  // Provider queue loss has bounded observable diagnostics, without sink I/O.
  U64 rejected = f->views[0].clipboard_rejected;
  f->count = f->acquired = 0; f->lost = 3;
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  ClipboardCheck(f->views[0].clipboard_rejected == rejected+3, "provider loss delta observed");
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
  ClipboardCheck(f->views[0].clipboard_rejected == rejected+3, "unchanged loss counter not repeated");
  uishell_terminal_clipboard_unregister(&f->views[0]);
  ClipboardCheck(uishell_terminal_clipboard_views == 0, "retirement removes clipboard target");
  uishell_terminal_clipboard_views = saved;
#undef ClipboardCheck
  ok = uishell_terminal_clipboard_host_diagnostics() && ok;
  fprintf(stderr, "terminal clipboard diagnostics %s\n", ok ? "passed" : "failed");
  scratch_end(scratch);
  return ok;
}
