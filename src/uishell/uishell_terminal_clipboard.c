// Live provider effects have one destructive consumption path. Render updates,
// recordings, cell caches and preview surfaces never call this consumer.
typedef struct UIShell_TerminalClipboardContext UIShell_TerminalClipboardContext;
struct UIShell_TerminalClipboardContext
{
  B32 allowed, input_owner, window_active, live, controller, supported;
};

typedef struct UIShell_TerminalClipboardOps UIShell_TerminalClipboardOps;
struct UIShell_TerminalClipboardOps
{
  // Fakes stand in for the provider queue and desktop clipboard I/O boundaries.
  cleat_clipboard_event const *(*acquire)(void *user, cleat_session *session);
  void (*release)(void *user, cleat_clipboard_event const *event);
  B32 (*write)(void *user, U32 destination, B32 clear, String8 text);
  UIShell_TerminalClipboardContext (*context)(void *user, UIShell_TerminalViewState *tv);
  U64 (*dropped)(void *user, cleat_session *session);
  void *user;
};

internal void
uishell_terminal_clipboard_unregister(UIShell_TerminalViewState *tv)
{
  UIShell_TerminalViewState **link = &uishell_terminal_clipboard_views;
  while(*link && *link != tv) { link = &(*link)->clipboard_next; }
  if(*link) { *link = tv->clipboard_next; }
  tv->clipboard_registered = 0;
  tv->clipboard_next = 0;
}

internal void
uishell_terminal_clipboard_register(UIShell_TerminalViewState *tv, CFG_ID view, CFG_ID window)
{
  tv->clipboard_view_id = view;
  tv->clipboard_window_id = window;
  if(!tv->clipboard_registered)
  {
    tv->clipboard_next = uishell_terminal_clipboard_views;
    uishell_terminal_clipboard_views = tv;
    tv->clipboard_registered = 1;
  }
}

// Strict UTF-8: exclude NUL, overlong encodings, surrogates and > U+10FFFF.
internal B32
uishell_terminal_clipboard_text_valid(String8 text)
{
  if(!text.str || text.size == 0 || text.size > KB(64)) { return 0; }
  for(U64 i = 0; i < text.size;)
  {
    U8 lead = text.str[i++];
    if(lead == 0) { return 0; }
    if(lead < 0x80) { continue; }
    U32 n = 0, cp = 0, min = 0;
    if(lead >= 0xc2 && lead <= 0xdf) { n = 1; cp = lead & 0x1f; min = 0x80; }
    else if(lead >= 0xe0 && lead <= 0xef) { n = 2; cp = lead & 0xf; min = 0x800; }
    else if(lead >= 0xf0 && lead <= 0xf4) { n = 3; cp = lead & 7; min = 0x10000; }
    else { return 0; }
    if(n > text.size-i) { return 0; }
    while(n--)
    {
      U8 byte = text.str[i++];
      if((byte & 0xc0) != 0x80) { return 0; }
      cp = (cp << 6) | (byte & 0x3f);
    }
    if(cp < min || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) { return 0; }
  }
  return 1;
}

internal B32
uishell_terminal_clipboard_eligible(UIShell_TerminalClipboardContext c)
{
  return c.allowed && c.input_owner && c.window_active && c.live && c.controller && c.supported;
}

internal void
uishell_terminal_clipboard_reject(UIShell_TerminalViewState *tv, char const *reason, U64 count)
{
  U64 previous = tv->clipboard_rejected;
  tv->clipboard_rejected += Min(count, max_U64-tv->clipboard_rejected);
  // Bounded diagnostics, with no application payload in the log.
  if(previous < 4)
  { fprintf(stderr, "terminal clipboard effect discarded (view=%llu count=%llu lost_or_rejected=%llu reason=%s)\n",
            (unsigned long long)tv->clipboard_view_id, (unsigned long long)tv->clipboard_rejected, (unsigned long long)count, reason); }
}

internal void
uishell_terminal_clipboard_drain(UIShell_TerminalViewState *tv, UIShell_TerminalClipboardOps *ops,
                                 UIShell_TerminalClipboardContext context, cleat_clipboard_event const *first_event)
{
  if(!tv->session) { return; }
  if(tv->clipboard_session != tv->session)
  {
    tv->clipboard_session = tv->session;
    tv->clipboard_identity_valid = 0;
    tv->clipboard_provider_dropped = 0;
  }
  U64 dropped = ops->dropped(ops->user, tv->session);
  if(dropped > tv->clipboard_provider_dropped)
  {
    uishell_terminal_clipboard_reject(tv, "provider loss", dropped-tv->clipboard_provider_dropped);
  }
  tv->clipboard_provider_dropped = dropped;
  U64 events = 0, bytes = 0;
  for(cleat_clipboard_event const *event = first_event; event != 0; event = ops->acquire(ops->user, tv->session))
  {
    B32 fresh = 1;
    // Cleat guarantees ordered effects and resets its receipt queue on recipient
    // changes/transfer. Remember even suppressed identities: focus cannot retry.
    if(tv->clipboard_identity_valid && MemoryMatch(tv->clipboard_epoch, event->session_epoch, 16))
    {
      fresh = event->connection_epoch > tv->clipboard_connection ||
        (event->connection_epoch == tv->clipboard_connection && event->sequence > tv->clipboard_sequence);
    }
    if(fresh)
    {
      MemoryCopy(tv->clipboard_epoch, event->session_epoch, 16);
      tv->clipboard_connection = event->connection_epoch;
      tv->clipboard_sequence = event->sequence;
      tv->clipboard_identity_valid = 1;
    }
    events++;
    B32 bounded = events <= 16 && event->text_len <= KB(64) && event->text_len <= KB(256)-bytes;
    if(bounded) { bytes += event->text_len; }
    String8 text = str8((U8 *)event->text, event->text_len);
    B32 clear = event->kind == 2;
    B32 valid = event->destination <= 1 &&
      ((clear && event->text_len == 0 && event->text == 0) ||
       (event->kind == 1 && bounded && uishell_terminal_clipboard_text_valid(text)));
    char const *rejection = !fresh ? "consumed identity" : !bounded ? "queue/payload bound" : !valid ? "unsupported/invalid content" :
      !uishell_terminal_clipboard_eligible(context) ? "host policy/ownership" : 0;
    if(!rejection && !ops->write(ops->user, event->destination, clear, text)) { rejection = "native adapter failure"; }
    if(rejection) { uishell_terminal_clipboard_reject(tv, rejection, 1); }
    ops->release(ops->user, event);
  }
}

// Multiple runtime references to one handle share a destructive queue and one
// watermark. Elect the eligible reference before draining; an inactive alias
// must not discard the active reference's event. Distinct provider handles use
// Cleat's sole-recipient contract, including across windows and reconnects.
internal void
uishell_terminal_clipboard_dispatch_with_ops(UIShell_TerminalClipboardOps *ops)
{
  Temp scratch = scratch_begin(0, 0);
  typedef struct ClipboardReference ClipboardReference;
  struct ClipboardReference { ClipboardReference *next; UIShell_TerminalViewState *view; };
  typedef struct ClipboardHandle ClipboardHandle;
  struct ClipboardHandle { ClipboardHandle *next; cleat_session *session; ClipboardReference *first, *last; };
  U64 count = 0;
  for(UIShell_TerminalViewState *tv = uishell_terminal_clipboard_views; tv; tv = tv->clipboard_next) { count++; }
  U64 slots_count = Max(1, count*2);
  ClipboardHandle **slots = push_array(scratch.arena, ClipboardHandle *, slots_count);
  // Hash distinct handles once: inactive references share the same destructive
  // queue/watermark. Idle handles never build the host configuration context.
  for(UIShell_TerminalViewState *tv = uishell_terminal_clipboard_views; tv; tv = tv->clipboard_next)
  {
    if(!tv->session) { continue; }
    U64 slot = ((U64)(uintptr_t)tv->session >> 4)%slots_count;
    ClipboardHandle *handle = slots[slot];
    for(; handle && handle->session != tv->session; handle = handle->next) {}
    if(!handle)
    {
      handle = push_array(scratch.arena, ClipboardHandle, 1);
      handle->session = tv->session;
      handle->next = slots[slot]; slots[slot] = handle;
    }
    ClipboardReference *reference = push_array(scratch.arena, ClipboardReference, 1);
    reference->view = tv;
    SLLQueuePush(handle->first, handle->last, reference);
  }
  for(U64 slot = 0; slot < slots_count; slot++)
  for(ClipboardHandle *handle = slots[slot]; handle; handle = handle->next)
  {
    UIShell_TerminalViewState *tv = handle->first->view;
    cleat_clipboard_event const *first_event = ops->acquire(ops->user, tv->session);
    UIShell_TerminalClipboardContext context = {0};
    if(first_event)
    {
      context = ops->context(ops->user, tv);
      // Only event-bearing shared handles need alias context resolution.
      for(ClipboardReference *reference = handle->first->next; reference; reference = reference->next)
      {
        UIShell_TerminalClipboardContext candidate = ops->context(ops->user, reference->view);
        if(uishell_terminal_clipboard_eligible(candidate)) { context = candidate; break; }
      }
    }
    uishell_terminal_clipboard_drain(tv, ops, context, first_event);
  }
  // Preserve consumption state when any alias retires, without a view scan.
  for(U64 slot = 0; slot < slots_count; slot++)
  for(ClipboardHandle *handle = slots[slot]; handle; handle = handle->next)
  {
    UIShell_TerminalViewState *source = handle->first->view;
    for(ClipboardReference *reference = handle->first->next; reference; reference = reference->next)
    {
      UIShell_TerminalViewState *tv = reference->view;
      tv->clipboard_session = source->clipboard_session;
      MemoryCopyArray(tv->clipboard_epoch, source->clipboard_epoch);
      tv->clipboard_connection = source->clipboard_connection;
      tv->clipboard_sequence = source->clipboard_sequence;
      tv->clipboard_identity_valid = source->clipboard_identity_valid;
      tv->clipboard_provider_dropped = source->clipboard_provider_dropped;
    }
  }
  scratch_end(scratch);
}

internal UIShell_TerminalClipboardContext
uishell_terminal_clipboard_host_context(UIShell_TerminalViewState *tv, B32 window_active, B32 controller, B32 supported)
{
  UIShell_TerminalClipboardContext result = {0};
  CFG_Node *view = cfg_node_from_id(tv->clipboard_view_id);
  CFG_Node *window = cfg_node_from_id(tv->clipboard_window_id);
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  if(view == &cfg_nil_node || window == &cfg_nil_node || ws == &rd_nil_window_state) { return result; }
  // A host deny lives only in the user bucket. Workspace/view config must not
  // override this host policy (operator Copy uses the separate existing path).
  CFG_Node *host = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  String8 deny = cfg_node_child_from_string(host, str8_lit("deny_application_clipboard_writes"))->first->string;
  result.allowed = !str8_match(deny, str8_lit("1"), 0) && !str8_match(deny, str8_lit("true"), StringMatchFlag_CaseInsensitive);
  Temp scratch = scratch_begin(0, 0);
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  UIShell_WorkspaceMount *mount = uishell_controlled_split_selected_mount(&split);
  result.input_owner = mount->panel_tree.focused->selected_tab == view && tv->focus_active &&
    ws->ui && ws->ui->edit_owner_terminal && ws->ui->edit_owner_user == tv && ws->ui->edit_owner_view == view->id &&
    tv->input_frame + 1 == rd_state->frame_index && !ws->query_is_active && !rd_state->popup_active && !ws->menu_bar_focused && !ws->hover_eval_focused && (!ws->ui || (!ws->ui->ctx_menu_open && !ws->ui->next_ctx_menu_open));
  result.window_active = window_active;
  result.live = !rd_state->quit && !ws->workspace_zoom_open &&
    !rd_state->frame_replay.suppress_input && !rd_state->frame_replay.prepare_window && rd_state->frame_replay.fixed_dt == 0 &&
    str8_match(view->string, str8_lit("terminal"), 0) && rd_view_state_from_cfg(view)->user_data == tv;
  result.controller = controller;
  result.supported = supported;
  scratch_end(scratch);
  return result;
}

internal UIShell_TerminalClipboardContext
uishell_terminal_clipboard_context(void *user, UIShell_TerminalViewState *tv)
{
  (void)user;
  RD_WindowState *ws = rd_window_state_from_cfg__existing(cfg_node_from_id(tv->clipboard_window_id));
  return uishell_terminal_clipboard_host_context(tv, ws != &rd_nil_window_state && wm_window_is_focused(ws->os),
    cleat_session_role(tv->session) == CLEAT_ROLE_CONTROLLER, cleat_session_clipboard_supported(tv->session));
}

internal cleat_clipboard_event const *
uishell_terminal_clipboard_acquire(void *user, cleat_session *session)
{ (void)user; return cleat_session_acquire_clipboard_event(session); }
internal void
uishell_terminal_clipboard_release(void *user, cleat_clipboard_event const *event)
{ (void)user; cleat_clipboard_event_release(event); }
internal U64
uishell_terminal_clipboard_dropped(void *user, cleat_session *session)
{ (void)user; return cleat_session_clipboard_dropped(session); }
internal B32
uishell_terminal_clipboard_write(void *user, U32 destination, B32 clear, String8 text)
{ (void)user; return wm_apply_clipboard_write(destination, clear, text); }

internal void
uishell_terminal_clipboard_dispatch(void)
{
  UIShell_TerminalClipboardOps ops = {uishell_terminal_clipboard_acquire, uishell_terminal_clipboard_release,
    uishell_terminal_clipboard_write, uishell_terminal_clipboard_context, uishell_terminal_clipboard_dropped, 0};
  uishell_terminal_clipboard_dispatch_with_ops(&ops);
}
