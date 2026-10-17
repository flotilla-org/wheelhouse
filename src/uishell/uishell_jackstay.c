// Jackstay presentation: shell-owned focus/texture, independently owned workers.
#if defined(WHEELHOUSE_JACKSTAY)
#include "jackstay/wheelhouse_jackstay.c"
typedef struct UIShell_JackstayViewport UIShell_JackstayViewport;
struct UIShell_JackstayViewport
{
  U32 width, height;
  F32 scale;
};

internal UIShell_JackstayViewport
uishell_jackstay_viewport(Vec2F32 dim, F32 layout_scale, F32 backing_scale)
{
  // Layout coordinates already include DPI scaling on Windows, but are
  // points on macOS. Remove only layout scaling to request a logical page;
  // the producer rasterizes it at the combined logical-to-backing scale.
  if(layout_scale <= 0)layout_scale=1;
  if(backing_scale <= 0)backing_scale=1;
  F32 display=layout_scale*backing_scale;
  UIShell_JackstayViewport result={(U32)(Max(0,dim.x)/layout_scale),(U32)(Max(0,dim.y)/layout_scale),display};
  return result;
}

typedef struct UIShell_JackstayView UIShell_JackstayView;
// A D3D11 pool texture imported on the renderer's device.
typedef struct UIShell_JackstayImport UIShell_JackstayImport;
struct UIShell_JackstayImport
{
  U64 incarnation, pool_id;
  U32 slot_id;
  R_Handle texture;
};
struct UIShell_JackstayView
{
  UIShell_JackstayView *next;
  RD_ViewAddress view;
  WH_Jackstay *session;
  R_Handle texture; // shown: an owned upload, or an entry of imports
  B32 texture_owned;
  U64 upload_frame, version, epoch, pointer_epoch, press_serial;
  U64 keys[WM_Key_COUNT][2];
  U32 width, height;
  B32 focus_requested, focused, content_focus, closing, disconnecting, initialized;
  U32 mode;
  U8 paths[3][1024]; U64 sizes[3]; TxtPt cursor[3], mark[3];
  Rng2F32 image_rect;
  U32 buttons;
  Vec2F32 mouse;
  // D3D11 import: the frame on screen, its imports, and the view's own
  // release fence, signalled after the draws that sample a frame.
  WH_JS_Frame shown;
  UIShell_JackstayImport imports[16];
  U64 readiness_incarnation, readiness_id;
  R_Handle readiness, release;
  U64 release_value;
  // What the producer reports through affordances: its title for the tab
  // (live, never saved), and the last presentation sent, which goes hidden
  // once the View stops being built.
  char title[256];
  B32 built;
  U32 present_width, present_height;
  F32 present_scale;
  B32 presented, present_focused;
  // A web View: the program its Handler runs (its process, supervised), the
  // page it was started on, why it failed, the address bar's text, and the
  // page last written as where it got to (`web_url`).
  B32 web;
  WheelhouseConnector *process;
  B32 retrying; // until the process leaves its failure
  char log[1024];
  char page[2048];
  char failure[2048];
  U8 address[2048]; U64 address_size; TxtPt address_cursor, address_mark;
  char address_url[2048];
  char reached[2048];
};
global UIShell_JackstayView *uishell_jackstay_views;
internal void uishell_jackstay_wake(void *unused) { (void)unused; wm_send_wakeup_event(); }
internal UIShell_JackstayView *uishell_jackstay_find(RD_ViewAddress view)
{
  for(UIShell_JackstayView *v=uishell_jackstay_views;v;v=v->next) if(rd_view_address_match(v->view,view))return v;
  return 0;
}
// A mounted panel does not supply default focus navigation. Each View owns
// the active key shared by its native controls and embedded content.
internal UI_Box *uishell_jackstay_view_root(UIShell_JackstayView *v,Rng2F32 rect)
{
  ui_set_next_pref_width(ui_px(dim_2f32(rect).x,1));
  ui_set_next_pref_height(ui_px(dim_2f32(rect).y,1));
  ui_set_next_child_layout_axis(Axis2_Y);
  UI_Box *root;
  UI_Focus(UI_FocusKind_On)
  {root=ui_build_box_from_key(UI_BoxFlag_DefaultFocusNav|UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder,
    ui_key_from_stringf(ui_active_seed_key(),"###jackstay_focus_%I64u",rd_view_ui_key(v->view).u64[0]));}
  return root;
}
// A move that changes the View's address keeps its session.
internal void uishell_jackstay_rekey(RD_ViewAddress from,RD_ViewAddress to)
{
  UIShell_JackstayView *v=uishell_jackstay_find(from);
  if(v && !uishell_jackstay_find(to))v->view=to;
}
// The title a View's producer reports, its page's or else its window's, for
// its tab (rd_view_label_from_cfg): live, never saved. Empty when none.
internal String8 uishell_jackstay_title(RD_ViewAddress view)
{
  UIShell_JackstayView *v=uishell_jackstay_find(view);
  return v?str8_cstring(v->title):str8_zero();
}
//- Web Views (WH_HANDLERS): a Handler's program, launched and supervised
// like a subscription's connector (connector.rs), whose endpoint the View
// shows. The process is the View's: it starts on the View's first build and
// stops when the View closes or its workspace is kept (uishell_jackstay_tick).

// The page a web View opens on without an address, bundled with Wheelhouse.
read_only global String8 uishell_web_start_html = str8_lit_comp(
  "<!doctype html><html><head><meta charset=\"utf-8\"><title>New page</title><style>"
  "body{font:16px system-ui,sans-serif;margin:0;height:100vh;display:flex;align-items:center;justify-content:center;"
  "background:#1e1f22;color:#ddd}form{display:flex;gap:8px;width:min(560px,90vw)}"
  "input{flex:1;font:inherit;padding:8px 10px;border-radius:6px;border:1px solid #555;background:#2b2d31;color:inherit}"
  "button{font:inherit;padding:8px 14px;border-radius:6px;border:0;background:#4a6cf7;color:#fff}</style></head><body>"
  "<form onsubmit=\"var u=this.u.value.trim();if(u)location.href=/^[a-z][a-z0-9+.-]*:\\/\\//i.test(u)?u:'https://'+u;return false\">"
  "<input name=\"u\" placeholder=\"Enter an address\" autofocus><button>Go</button></form></body></html>");

// Where the start page is written for the program to open, beside the logs.
internal String8 uishell_web_start_path(Arena *arena)
{
  return push_str8f(arena, "%S/web-start.html", str8_chop_last_slash(rd_state->log_path));
}

// What the program is given to open: an http(s) URL, or a file's path (the
// start page's when there is no URL).
internal String8 uishell_web_page_argument(Arena *arena, String8 url)
{
  if(!url.size) { return uishell_web_start_path(arena); }
  String8 path = wh_path_from_file_url(arena, url);
  return path.size ? path : url;
}

internal void uishell_web_fail(UIShell_JackstayView *v, String8 why)
{
  U64 size = Min(why.size, sizeof(v->failure) - 1);
  MemoryCopy(v->failure, why.str, size);
  v->failure[size] = 0;
}

// Starts the Handler's program for `url` (empty: the start page) at the
// View's logical size, or records why it couldn't.
internal void uishell_web_launch(UIShell_JackstayView *v, String8 url, U32 width, U32 height)
{
  Temp scratch = scratch_begin(0, 0);
  local_persist U32 launches = 0;
  v->failure[0] = 0;
  U64 page_size = Min(url.size, sizeof(v->page) - 1);
  MemoryCopy(v->page, url.str, page_size);
  v->page[page_size] = 0;
  WH_Handler *handler = wh_handler_from_url(url.size ? url : str8_lit("file:"));
  if(!handler->name.size)
  {
    uishell_web_fail(v, push_str8f(scratch.arena, "Wheelhouse has nothing that shows %S.", url));
    scratch_end(scratch);
    return;
  }
  if(!url.size) { write_data_to_file_path(uishell_web_start_path(scratch.arena), uishell_web_start_html); }
  // WHEELHOUSE_<NAME> names another program to run.
  String8 variable = push_str8f(scratch.arena, "WHEELHOUSE_%S", upper_from_str8(scratch.arena, handler->name));
  char *named = getenv((char *)variable.str);
  String8 program = named && named[0] ? str8_cstring(named) : handler->program;
  String8 endpoint = push_str8f(scratch.arena, "wh-%u-%u", get_process_info()->pid, ++launches);
  String8 values[][2] =
  {
    {str8_lit("{endpoint}"), endpoint},
    {str8_lit("{width}"), push_str8f(scratch.arena, "%u", width ? width : 800)},
    {str8_lit("{height}"), push_str8f(scratch.arena, "%u", height ? height : 600)},
    {str8_lit("{page}"), uishell_web_page_argument(scratch.arena, url)},
  };
  WheelhouseIngressText argv[32];
  U64 argc = 0;
  argv[argc++] = (WheelhouseIngressText){program.str, program.size};
  String8List args = wh_handler_arguments(scratch.arena, handler, values, ArrayCount(values));
  for(String8Node *n = args.first; n && argc < ArrayCount(argv); n = n->next) { argv[argc++] = (WheelhouseIngressText){n->string.str, n->string.size}; }
  String8 log = push_str8f(scratch.arena, "%S/%S.log", str8_chop_last_slash(rd_state->log_path), endpoint);
  snprintf(v->log, sizeof(v->log), "%.*s", (int)log.size, (char *)log.str);
  U8 error[512] = {0};
  v->process = wheelhouse_connector_start(argv, argc, 0, 0, (WheelhouseIngressText){log.str, log.size},
                                          WHEELHOUSE_CONNECTOR_READY_LINE, wm_send_wakeup_event, error, sizeof(error));
  if(!v->process) { uishell_web_fail(v, push_str8f(scratch.arena, "Couldn't start %S: %s", program, error)); }
  scratch_end(scratch);
}

internal void uishell_jackstay_unfocus(UIShell_JackstayView *v)
{
  if(v->session)wh_js_focus(v->session,false);
  MemoryZeroArray(v->keys);v->buttons=0;v->focused=0;
}
#if OS_WINDOWS
#define UISHELL_JACKSTAY_ADDRESS_LABEL "Source endpoint name"
#else
#define UISHELL_JACKSTAY_ADDRESS_LABEL "Source endpoint name, or absolute socket path"
#endif
// An address is a Local Endpoint name (ADR 0011) or, on POSIX, a socket path.
internal B32 uishell_jackstay_is_path(String8 address) {return OS_WINDOWS==0 && address.size && address.str[0]=='/';}
internal String8 uishell_jackstay_address(String8 role)
{
  Temp scratch=scratch_begin(0,0);
  String8 endpoint=rd_view_setting_from_name(push_str8f(scratch.arena,"%S_endpoint",role));
  String8 socket=rd_view_setting_from_name(push_str8f(scratch.arena,"%S_socket",role));
  scratch_end(scratch);
  return endpoint.size?endpoint:socket;
}
internal void uishell_jackstay_store_address(String8 role,String8 address)
{
  Temp scratch=scratch_begin(0,0);
  B32 path=uishell_jackstay_is_path(address);
  rd_store_view_param(push_str8f(scratch.arena,"%S_endpoint",role),path?str8_zero():address);
  rd_store_view_param(push_str8f(scratch.arena,"%S_socket",role),path?address:str8_zero());
  scratch_end(scratch);
}
internal U32 uishell_jackstay_modifiers(WM_Modifiers flags)
{
  return ((flags&WM_Modifier_Ctrl)?FT_INPUT_CONTROL:0)|((flags&WM_Modifier_Shift)?FT_INPUT_SHIFT:0)|
         ((flags&WM_Modifier_Alt)?FT_INPUT_ALT:0)|((flags&WM_Modifier_Super)?FT_INPUT_SUPER:0);
}
internal B32 uishell_jackstay_release_event(RD_ViewAddress view,WM_Event *event)
{
  UIShell_JackstayView *v=uishell_jackstay_find(view);
  if(!v || !v->session || !v->focused)return 0;
  if(event->kind==WM_EventKind_Press && event->key==WM_Key_Esc &&
     (event->modifiers&(WM_Modifier_Ctrl|WM_Modifier_Shift))==(WM_Modifier_Ctrl|WM_Modifier_Shift))
  { v->content_focus=0;uishell_jackstay_unfocus(v);return 1; }
  return 0;
}
internal B32 uishell_jackstay_event(RD_ViewAddress view,WM_Event *event)
{
  UIShell_JackstayView *v=uishell_jackstay_find(view);
  if(!v || !v->session || !v->focused)return 0;
  if(event->kind==WM_EventKind_WindowLoseFocus) {uishell_jackstay_unfocus(v);return 0;}
  if(uishell_jackstay_release_event(view,event))return 1;
  B32 mouse=event->key==WM_Key_LeftMouseButton || event->key==WM_Key_RightMouseButton || event->key==WM_Key_MiddleMouseButton;
  if(event->kind==WM_EventKind_Press && mouse && !contains_2f32(v->image_rect,event->pos))
  { v->content_focus=0;uishell_jackstay_unfocus(v);return 0; }
  if(event->kind!=WM_EventKind_Press && event->kind!=WM_EventKind_Release && event->kind!=WM_EventKind_Text)return 0;
  if(mouse)return 0;
  WH_JS_State state;wh_js_snapshot(v->session,&state,0,0);
  if(v->epoch!=state.input_epoch) {MemoryZeroArray(v->keys);v->buttons=0;v->epoch=state.input_epoch;}
  ft_input_event input={.modifiers=uishell_jackstay_modifiers(event->modifiers)};
  if(event->kind==WM_EventKind_Text)
  {
    U8 text[8]={0};U32 length=utf8_encode(text,event->character);
    input.kind=FT_INPUT_TEXT;input.text=text;input.text_len=length;
    if(state.control && !state.resetting)wh_js_send(v->session,&input);
    return 1;
  }
  char *code=0;
  switch(event->key)
  {
#define JS_KEY(wm,dom) case WM_Key_##wm: code=dom;break;
    JS_KEY(Esc,"Escape") JS_KEY(Return,"Enter") JS_KEY(Tab,"Tab") JS_KEY(Backspace,"Backspace")
    JS_KEY(Space,"Space") JS_KEY(Up,"ArrowUp") JS_KEY(Down,"ArrowDown") JS_KEY(Left,"ArrowLeft") JS_KEY(Right,"ArrowRight")
    JS_KEY(Delete,"Delete") JS_KEY(Insert,"Insert") JS_KEY(Home,"Home") JS_KEY(End,"End") JS_KEY(PageUp,"PageUp") JS_KEY(PageDown,"PageDown")
    JS_KEY(Minus,"Minus") JS_KEY(Equal,"Equal") JS_KEY(Tick,"Backquote") JS_KEY(LeftBracket,"BracketLeft") JS_KEY(RightBracket,"BracketRight")
    JS_KEY(BackSlash,"Backslash") JS_KEY(Semicolon,"Semicolon") JS_KEY(Quote,"Quote") JS_KEY(Comma,"Comma") JS_KEY(Period,"Period") JS_KEY(Slash,"Slash")
    case WM_Key_Ctrl:code=event->right_sided?"ControlRight":"ControlLeft";break;
    case WM_Key_Shift:code=event->right_sided?"ShiftRight":"ShiftLeft";break;
    case WM_Key_Alt:code=event->right_sided?"AltRight":"AltLeft";break;
#undef JS_KEY
    default:break;
  }
  // WM letter values are keyboard positions, not committed text. Use an explicit
  // table: enum order follows keyboard rows rather than alphabetic order.
  struct {WM_Key key;char *code;} letters[]={
#define JS_LETTER(k) {WM_Key_##k,"Key" #k}
    JS_LETTER(A),JS_LETTER(B),JS_LETTER(C),JS_LETTER(D),JS_LETTER(E),JS_LETTER(F),JS_LETTER(G),JS_LETTER(H),JS_LETTER(I),JS_LETTER(J),JS_LETTER(K),JS_LETTER(L),JS_LETTER(M),JS_LETTER(N),JS_LETTER(O),JS_LETTER(P),JS_LETTER(Q),JS_LETTER(R),JS_LETTER(S),JS_LETTER(T),JS_LETTER(U),JS_LETTER(V),JS_LETTER(W),JS_LETTER(X),JS_LETTER(Y),JS_LETTER(Z),
#undef JS_LETTER
  };
  for(U64 i=0;i<ArrayCount(letters);++i)if(event->key==letters[i].key)code=letters[i].code;
  char generated[16];
  if(event->key>=WM_Key_0 && event->key<=WM_Key_9){snprintf(generated,sizeof(generated),"Digit%d",event->key-WM_Key_0);code=generated;}
  if(event->key>=WM_Key_F1 && event->key<=WM_Key_F24){snprintf(generated,sizeof(generated),"F%d",event->key-WM_Key_F1+1);code=generated;}
  if(!code)return 1;
  U64 *press=&v->keys[event->key][!!event->right_sided];
  input.kind=FT_INPUT_KEY;input.key_kind=FT_INPUT_PHYSICAL_KEY;snprintf(input.key,sizeof(input.key),"%s",code);
  if(event->kind==WM_EventKind_Release) {if(!*press)return 1;input.action=FT_INPUT_UP;input.press=*press;*press=0;}
  else {input.action=*press?FT_INPUT_REPEAT:FT_INPUT_DOWN;if(!*press)*press=++v->press_serial;input.press=*press;}
  if(!wh_js_send(v->session,&input))*press=0;
  return 1;
}
internal B32 uishell_jackstay_pending(void) {return uishell_jackstay_views!=0;}
// The renderer's device, when it can import shared frames. The release fence
// belongs to the view and survives reconnects, so its values only increase.
internal WH_JS_Gpu uishell_jackstay_gpu(UIShell_JackstayView *v)
{
  WH_JS_Gpu gpu={0};
  R_AdapterInfo adapter=r_adapter_info();
  if(adapter.shared_timelines && r_handle_match(v->release,r_handle_zero()))v->release=r_timeline_alloc_shared();
  if(adapter.shared_timelines && !r_handle_match(v->release,r_handle_zero()))
  {gpu.adapter=adapter.id;gpu.device=r_native_device();gpu.release_fence=r_native_timeline(v->release);}
  return gpu;
}
// Release the shown import frame once the GPU has finished every draw queued
// so far: the signal follows them on the same queue.
internal void uishell_jackstay_retire(UIShell_JackstayView *v)
{
  if(!v->shown.native)return;
  U64 value=++v->release_value;
  // If the signal fails this value may never complete: the frame stays held,
  // which is safe, until a later signal passes it.
  r_queue_signal(v->release,value);
  wh_js_frame_release(v->session,&v->shown,value);
  MemoryZeroStruct(&v->shown);
}
// Drop imports that no held frame uses. D3D11 keeps a resource alive for GPU
// work already queued on it; r_tex2d_release also defers to the frame's end.
internal void uishell_jackstay_forget_imports(UIShell_JackstayView *v,B32 all)
{
  for(U64 i=0;i<ArrayCount(v->imports);++i)
  {
    UIShell_JackstayImport *import=&v->imports[i];
    B32 in_use=!all && v->shown.native && import->incarnation==v->shown.incarnation && import->pool_id==v->shown.pool_id;
    if(!r_handle_match(import->texture,r_handle_zero()) && !in_use){r_tex2d_release(import->texture);MemoryZeroStruct(import);}
  }
  if(all && !r_handle_match(v->readiness,r_handle_zero())){r_timeline_release(v->readiness);v->readiness=r_handle_zero();}
}
internal void uishell_jackstay_show_texture(UIShell_JackstayView *v,R_Handle texture,B32 owned,U32 width,U32 height,U64 version)
{
  if(v->texture_owned && !r_handle_match(v->texture,r_handle_zero()))r_tex2d_release(v->texture);
  v->texture=texture;v->texture_owned=owned;v->width=width;v->height=height;v->version=version;
}
// Take a frame the worker handed over for import: import (cached by pool
// slot and fence), skip it if its producer died, queue the GPU wait for its
// copy, then retire the frame it replaces.
internal void uishell_jackstay_take_import(UIShell_JackstayView *v,WH_JS_Frame *frame)
{
  R_Handle texture=r_handle_zero();
  UIShell_JackstayImport *free_import=0;
  for(U64 i=0;i<ArrayCount(v->imports);++i)
  {
    UIShell_JackstayImport *import=&v->imports[i];
    if(r_handle_match(import->texture,r_handle_zero())){if(!free_import)free_import=import;continue;}
    if(import->incarnation==frame->incarnation && import->pool_id==frame->pool_id && import->slot_id==frame->slot_id)texture=import->texture;
  }
  if(r_handle_match(texture,r_handle_zero()) && free_import)
  {
    texture=r_tex2d_open_shared(frame->texture);
    if(!r_handle_match(texture,r_handle_zero()))
    {free_import->texture=texture;free_import->incarnation=frame->incarnation;free_import->pool_id=frame->pool_id;free_import->slot_id=frame->slot_id;}
  }
  if(r_handle_match(v->readiness,r_handle_zero()) || v->readiness_incarnation!=frame->incarnation || v->readiness_id!=frame->fence_id)
  {
    if(!r_handle_match(v->readiness,r_handle_zero()))r_timeline_release(v->readiness);
    v->readiness=r_timeline_open_shared(frame->readiness);
    v->readiness_incarnation=frame->incarnation;v->readiness_id=frame->fence_id;
  }
  if(r_handle_match(texture,r_handle_zero()) || r_handle_match(v->readiness,r_handle_zero()))
  {
    wh_js_frame_release(v->session,frame,0);
    wh_js_refuse_import(v->session,"the renderer could not open the shared texture or fence");
    return;
  }
  if(!wh_js_readiness_alive(r_native_timeline(v->readiness)))
  {
    // The copy behind it may never have run; keep the last good frame.
    wh_js_frame_release(v->session,frame,0);
    return;
  }
  // The frame on screen was last sampled by work already queued; release it
  // before this frame's wait so its release does not also wait on the copy.
  uishell_jackstay_retire(v);
  if(!r_queue_wait(v->readiness,frame->fence_value))
  {
    wh_js_frame_release(v->session,frame,0);
    wh_js_refuse_import(v->session,"the renderer could not wait for the producer");
    uishell_jackstay_show_texture(v,r_handle_zero(),0,0,0,0);
    return;
  }
  v->shown=*frame;
  uishell_jackstay_show_texture(v,texture,0,frame->width,frame->height,frame->version);
  uishell_jackstay_forget_imports(v,0);
}
// Stop showing frames: release the held one and every import.
internal void uishell_jackstay_drop_frames(UIShell_JackstayView *v)
{
  if(v->session)uishell_jackstay_retire(v);
  uishell_jackstay_forget_imports(v,1);
  uishell_jackstay_show_texture(v,r_handle_zero(),0,0,0,0);
}
// Connects once the program says where it publishes, and notes a failure.
internal void uishell_web_poll(UIShell_JackstayView *v)
{
  if(!v->process) { return; }
  char text[2048];
  size_t length = 0;
  U32 state = wheelhouse_connector_state(v->process, (uint8_t *)text, sizeof(text), &length);
  // A retry's restart lands on the supervisor's thread a moment later.
  if(v->retrying)
  {
    if(state == WheelhouseConnectorState_Failed) { return; }
    v->retrying = 0;
  }
  if(state == WheelhouseConnectorState_Ready && length >= sizeof(v->paths[0]))
  {
    uishell_web_fail(v, str8_lit("Its endpoint is longer than Wheelhouse takes."));
    return;
  }
  if(state == WheelhouseConnectorState_Ready && !v->session && !v->disconnecting)
  {
    // Its first stdout line is the endpoint: a socket path, or a name.
    U64 size = Min(length, sizeof(v->paths[0]) - 1);
    MemoryCopy(v->paths[0], text, size);
    v->paths[0][size] = 0;
    v->sizes[0] = size;
    WH_JS_Endpoint endpoint = {(char *)v->paths[0], "", 1, WH_JS_MEDIA_CPU, 0, 0};
    v->session = wh_js_create(endpoint, uishell_jackstay_gpu(v), uishell_jackstay_wake, 0);
    // Its own page: control from the start, as a terminal's input is.
    if(v->session) { wh_js_connect(v->session, true); }
    v->failure[0] = 0;
  }
  if(state == WheelhouseConnectorState_Failed)
  {
    uishell_web_fail(v, str8_cstring(text));
    if(v->session && !v->disconnecting) { uishell_jackstay_drop_frames(v); wh_js_stop(v->session); v->disconnecting = 1; }
  }
}

internal void uishell_jackstay_tick(B32 before,B32 quit)
{
  for(UIShell_JackstayView **link=&uishell_jackstay_views;*link;)
  {
    UIShell_JackstayView *v=*link;
    if(before){v->focus_requested=0;v->built=0;}
    else if(!v->focus_requested)uishell_jackstay_unfocus(v);
    // A View this frame didn't build is no longer shown.
    if(!before && v->session && v->presented && !v->built)
    {wh_js_present(v->session,v->present_width,v->present_height,v->present_scale,0,0);v->presented=0;}
    CFG_Node *cfg=rd_cfg_from_view(v->view);
    // A web View's process stops with it, and when its workspace is kept.
    String8 kind=v->web?str8_lit("web"):str8_lit("jackstay");
    if(quit || cfg==&cfg_nil_node || !str8_match(cfg->string,kind,0) || (v->web && !rd_view_node_is_live(cfg)))
    {
      if(!v->closing)uishell_jackstay_drop_frames(v);
      v->closing=1;if(v->session)wh_js_stop(v->session);
      if(v->process)wheelhouse_connector_stop_begin(v->process);
    }
    // Stopping it ends its run (and what it started) on the supervisor's
    // thread; once nothing runs, releasing it doesn't wait.
    // Its log goes with it.
    if(v->closing && v->process && !wheelhouse_connector_running(v->process,0))
    {wheelhouse_connector_stop(v->process);v->process=0;if(v->log[0])delete_file_at_path(str8_cstring(v->log));}
    if(v->closing && !v->process && (!v->session || wh_js_destroy(&v->session)))
    {
      // Jackstay holds its own reference to the release fence for releases
      // still pending on the GPU.
      if(!r_handle_match(v->release,r_handle_zero()))r_timeline_release(v->release);
      *link=v->next;free(v);continue;
    }
    if(v->closing)rd_request_frame();
    link=&v->next;
  }
}
// Connection forms, in the order the mode button cycles through them, and the
// view settings each one saves. D3D11 and Porthole sessions exist on Windows
// only, so the button stops before them elsewhere.
enum
{
  UISHELL_JACKSTAY_MODE_COMBINED,
  UISHELL_JACKSTAY_MODE_SEPARATE,
  UISHELL_JACKSTAY_MODE_D3D11,
  UISHELL_JACKSTAY_MODE_PORTHOLE,
  UISHELL_JACKSTAY_MODE_COUNT
};
internal String8 uishell_jackstay_mode_label(U32 mode)
{
  switch(mode)
  {
    case UISHELL_JACKSTAY_MODE_SEPARATE:return str8_lit("Separate media/input endpoints###mode");
    case UISHELL_JACKSTAY_MODE_D3D11:return str8_lit("D3D11 source endpoint###mode");
    case UISHELL_JACKSTAY_MODE_PORTHOLE:return str8_lit("Porthole capture session###mode");
    default:return str8_lit("Combined source endpoint###mode");
  }
}
// Remove a one-shot view setting so the layout never saves it back.
internal void uishell_jackstay_forget_setting(String8 key)
{
  rd_view_setting_clear(uishell_regs()->view,key);
}
internal void uishell_jackstay_open(UIShell_JackstayView *v,B32 control)
{
  for(int i=0;i<3;++i)v->paths[i][v->sizes[i]]=0;
  String8 first=str8(v->paths[0],v->sizes[0]),second=str8(v->paths[1],v->sizes[1]);
  uishell_jackstay_store_address(str8_lit("source"),v->mode==UISHELL_JACKSTAY_MODE_COMBINED?first:str8_zero());
  uishell_jackstay_store_address(str8_lit("media"),v->mode==UISHELL_JACKSTAY_MODE_SEPARATE?first:str8_zero());
  uishell_jackstay_store_address(str8_lit("input"),v->mode==UISHELL_JACKSTAY_MODE_SEPARATE?second:str8_zero());
  rd_store_view_param(str8_lit("d3d11_endpoint"),v->mode==UISHELL_JACKSTAY_MODE_D3D11?first:str8_zero());
  rd_store_view_param(str8_lit("porthole_endpoint"),v->mode==UISHELL_JACKSTAY_MODE_PORTHOLE?first:str8_zero());
  rd_store_view_param(str8_lit("porthole_session"),v->mode==UISHELL_JACKSTAY_MODE_PORTHOLE?second:str8_zero());
  WH_JS_Endpoint endpoint={(char *)v->paths[0],(char *)v->paths[1],v->mode==UISHELL_JACKSTAY_MODE_COMBINED,WH_JS_MEDIA_CPU,0,0};
  if(v->mode==UISHELL_JACKSTAY_MODE_D3D11){endpoint.kind=WH_JS_MEDIA_D3D11;endpoint.input="";}
  if(v->mode==UISHELL_JACKSTAY_MODE_PORTHOLE)
  {endpoint.kind=WH_JS_MEDIA_PORTHOLE;endpoint.input="";endpoint.session=(char *)v->paths[1];endpoint.token=(char *)v->paths[2];}
  v->session=wh_js_create(endpoint,uishell_jackstay_gpu(v),uishell_jackstay_wake,0);
  // The attach token lives only in the running session.
  MemoryZeroArray(v->paths[2]);v->sizes[2]=0;
  if(v->session)wh_js_connect(v->session,control);
}
// A web View's address bar: the page it shows (once it reports one, the page
// it reached), which typing a new address and Return opens in place, or
// relaunches with when it failed.
internal void uishell_web_address_bar(UIShell_JackstayView *v,char *reached)
{
  UI_Key key=ui_key_from_string(ui_active_seed_key(),str8_lit("###address"));
  B32 active=ui_is_key_auto_focus_active(key);
  if(!active && strcmp(v->address_url,reached))
  {
    snprintf(v->address_url,sizeof(v->address_url),"%s",reached);
    v->address_size=Min(strlen(reached),sizeof(v->address)-1);
    MemoryCopy(v->address,reached,v->address_size);
    v->address_cursor=v->address_mark=txt_pt(1,1+v->address_size);
  }
  UI_Signal signal=ui_line_edit(&v->address_cursor,&v->address_mark,v->address,sizeof(v->address)-1,&v->address_size,
                                str8(v->address,v->address_size),str8_lit("###address"));
  if(ui_pressed(signal))
  {
    v->content_focus=0;uishell_jackstay_unfocus(v);
    uishell_cmd("focus_panel");
    // Address fields begin editing on one click; preserve selection gestures
    // once editing has started.
    if(!active)
    {
      ui_set_auto_focus_active_key(key);ui_kill_action();
      v->address_cursor=txt_pt(1,1+v->address_size);v->address_mark=txt_pt(1,1);
      // The click and initial typing may share this build. The generic field
      // was not active when it built, so consume that queued text now.
      ui_consume_text_edit_events(key,v->address,sizeof(v->address)-1,&v->address_size,
        &v->address_cursor,&v->address_mark,0);
    }
  }
  if(active || ui_pressed(signal))ui_take_text_field_focus();
  if(!ui_committed(signal))return;
  Temp scratch=scratch_begin(0,0);
  String8 url=wh_url_from_address(scratch.arena,str8(v->address,v->address_size));
  if(url.size && v->session)wh_js_navigate(v->session,FT_AFF_NAVIGATION_LOAD,(char *)push_str8_copy(scratch.arena,url).str);
  else if(url.size && v->failure[0])
  {
    // Started afresh on the new page.
    if(v->process){wheelhouse_connector_stop(v->process);v->process=0;if(v->log[0])delete_file_at_path(str8_cstring(v->log));}
    uishell_web_launch(v,url,v->present_width,v->present_height);
  }
  scratch_end(scratch);
}

// Where a web View got to, once a navigation settles: its `web_url`, saved
// with its Slot (uishell_store_save_resolution). The start page is no page.
internal void uishell_web_note_reached(UIShell_JackstayView *v,WH_JS_State *state)
{
  if(!state->affordances || !state->url[0] || state->loading || !strcmp(v->reached,state->url))return;
  snprintf(v->reached,sizeof(v->reached),"%s",state->url);
  Temp scratch=scratch_begin(0,0);
  String8 url=str8_cstring(state->url);
  B32 start=str8_match(uishell_web_page_argument(scratch.arena,url),uishell_web_start_path(scratch.arena),0);
  if(start)rd_view_setting_clear(uishell_regs()->view,str8_lit("web_url"));
  else rd_store_view_param(str8_lit("web_url"),url);
  scratch_end(scratch);
}

// A connected View: the producer's frames, its navigation, and input. A web
// View's navigation row is its address bar, and it has no connection row.
internal void uishell_jackstay_session_ui(UIShell_JackstayView *v,Rng2F32 rect)
{
  WH_JS_State state;WH_JS_Frame frame={0};
  // A view may be built for both a preview and its main panel. Take at
  // most one frame per shell frame: an upload is an immutable texture, and
  // an import frame is released only after the GPU is done with it.
  wh_js_snapshot(v->session,&state,v->upload_frame!=rd_state->frame_index?&frame:0,v->version);
  v->upload_frame=rd_state->frame_index;
  if(frame.native)uishell_jackstay_take_import(v,&frame);
  else if(frame.pixels)
  {
    R_Handle texture=r_tex2d_alloc(R_ResourceKind_Static,v2s32(frame.width,frame.height),R_Tex2DFormat_RGBA8,frame.pixels);
    if(!r_handle_match(texture,r_handle_zero())) {
      // A CPU frame replaces any shown import.
      uishell_jackstay_retire(v);
      uishell_jackstay_forget_imports(v,1);
      uishell_jackstay_show_texture(v,texture,1,frame.width,frame.height,frame.version);
    }
    free(frame.pixels);
  }
  // The producer's title for the tab, and its navigation when it has any.
  char *title=state.page_title[0]?state.page_title:state.window_title;
  MemoryCopy(v->title,title,sizeof(v->title));
  B32 navigation=v->web || (state.affordances && (state.url[0] || state.navigation_capabilities));
  if(v->web)uishell_web_note_reached(v,&state);
  if(navigation) UI_PrefHeight(ui_em(1.8f,1)) UI_Row
  {
    U32 caps=state.navigation_capabilities;
    UI_PrefWidth(ui_em(2.2f,1))
    {
      if(caps&(1u<<(FT_AFF_NAVIGATION_BACK-1))) UI_Flags(state.can_go_back?0:UI_BoxFlag_Disabled)
      if(ui_clicked(rd_icon_button(RD_IconKind_LeftArrow,0,str8_lit("###back"))))wh_js_navigate(v->session,FT_AFF_NAVIGATION_BACK,0);
      if(caps&(1u<<(FT_AFF_NAVIGATION_FORWARD-1))) UI_Flags(state.can_go_forward?0:UI_BoxFlag_Disabled)
      if(ui_clicked(rd_icon_button(RD_IconKind_RightArrow,0,str8_lit("###forward"))))wh_js_navigate(v->session,FT_AFF_NAVIGATION_FORWARD,0);
      if(caps&(1u<<(FT_AFF_NAVIGATION_RELOAD-1)))
      if(ui_clicked(rd_icon_button(RD_IconKind_Refresh,0,str8_lit("###reload"))))wh_js_navigate(v->session,FT_AFF_NAVIGATION_RELOAD,0);
    }
    if(v->web)
    {
      UI_PrefWidth(ui_pct(1,0)) uishell_web_address_bar(v,state.url[0]?state.url:v->page);
      if(state.loading || !state.ready)UI_PrefWidth(ui_em(6,1)) ui_label(str8_lit("Loading…"));
    }
    else UI_PrefWidth(ui_pct(1,0)) ui_labelf("%s%s",state.url,state.loading?"  (loading)":"");
  }
  if(!v->web) UI_PrefHeight(ui_em(1.8f,1)) UI_Row
  {
    if(state.path_status[0] && state.width)UI_PrefWidth(ui_pct(1,0)) ui_labelf("%s | %s | %ux%u %s",state.media_status,state.input_status,state.width,state.height,state.path_status);
    else if(state.path_status[0])UI_PrefWidth(ui_pct(1,0)) ui_labelf("%s | %s | %s",state.media_status,state.input_status,state.path_status);
    else UI_PrefWidth(ui_pct(1,0)) ui_labelf("%s | %s",state.media_status,state.input_status);
    UI_PrefWidth(ui_em(8,1))
    {
      if(ui_clicked(ui_button(str8_lit("Retry control###retry"))) && !v->disconnecting)wh_js_connect(v->session,true);
      if(ui_clicked(ui_button(str8_lit("Disconnect###disconnect")))){uishell_jackstay_drop_frames(v);wh_js_stop(v->session);v->disconnecting=1;}
    }
  }
  ui_set_next_pref_height(ui_px(Max(0,dim_2f32(rect).y-ui_top_font_size()*1.8f*((navigation?1:0)+(v->web?0:1))),1));
  UI_Key canvas_key=ui_key_from_string(ui_active_seed_key(),str8_lit("jackstay_canvas"));
  // Query panel eligibility without marking every control as focused.
  B32 panel_active=0;
  UI_Focus(UI_FocusKind_On){panel_active=ui_is_focus_active();}
  UI_Box *canvas=ui_build_box_from_string(UI_BoxFlag_Clickable|UI_BoxFlag_ClickToFocus|UI_BoxFlag_Clip|UI_BoxFlag_DrawBackground|UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder,str8_lit("jackstay_canvas"));
  UI_Signal signal=ui_signal_from_box(canvas);
  B32 clicked=!!(signal.f&UI_SignalFlag_LeftPressed);
  if(clicked)
  {
    v->content_focus=1;ui_release_text_field_focus();ui_clear_edit_owner(ui_state);
    ui_set_auto_focus_active_key(canvas_key);uishell_cmd("focus_panel");
  }
  B32 focused=v->content_focus && (clicked ||
    (panel_active && !ui_text_field_focus() && ui_is_key_auto_focus_active(canvas_key)));
  RD_WindowState *window=rd_window_state_from_cfg(cfg_node_from_id(uishell_regs()->window));
  focused=focused && wm_window_is_focused(window->os);
  if(focused){v->focus_requested=1;v->focused=1;wh_js_focus(v->session,true);}
  if(clicked && !state.control && !state.connecting)wh_js_connect(v->session,true);
  if(v->epoch!=state.input_epoch){MemoryZeroArray(v->keys);v->buttons=0;v->epoch=state.input_epoch;}
  if(v->pointer_epoch!=state.pointer_epoch){v->buttons=0;v->pointer_epoch=state.pointer_epoch;}
  Vec2F32 dim=dim_2f32(canvas->rect);
  F32 scale=v->width && v->height?Min(dim.x/v->width,dim.y/v->height):0;
  Rng2F32 image=canvas->rect;
  image.x0+=(dim.x-v->width*scale)/2;image.y0+=(dim.y-v->height*scale)/2;
  image.x1=image.x0+v->width*scale;image.y1=image.y0+v->height*scale;
  if(focused && v->buttons && !MemoryMatch(&image,&v->image_rect,sizeof(image))) {wh_js_focus(v->session,false);wh_js_focus(v->session,focused);v->buttons=0;}
  if(focused)v->image_rect=image;
  // How the View shows the producer, which a producer that takes
  // affordances sizes itself to: the canvas in logical units at the
  // display's scale. Unchanged values are not sent again.
  {
    UIShell_JackstayViewport viewport=uishell_jackstay_viewport(dim,
      wm_layout_scale_from_window(window->os),wm_backing_scale_from_window(window->os));
    F32 display=viewport.scale;
    U32 width=viewport.width,height=viewport.height;
    if(!v->presented || width!=v->present_width || height!=v->present_height || display!=v->present_scale || focused!=v->present_focused)
    {
      wh_js_present(v->session,width,height,display,1,focused);
      v->present_width=width;v->present_height=height;v->present_scale=display;v->present_focused=focused;v->presented=1;
    }
    v->built=1;
  }
  DR_Bucket *bucket=dr_bucket_make();bucket->content_version=v->version;
  rd_workspace_surface_contribute_version(v->version);
  DR_BucketScope(bucket)
  {if(v->width && v->height)dr_img(image,r2f32p(0,0,v->width,v->height),v->texture,state.connected?v4f32(1,1,1,1):v4f32(.45f,.45f,.45f,1),0,0,0);}
  ui_box_equip_draw_bucket(canvas,bucket);
  Vec2F32 mouse=ui_mouse();
  B32 inside=mouse.x>=image.x0 && mouse.x<image.x1 && mouse.y>=image.y0 && mouse.y<image.y1;
  if(focused && state.control && !state.resetting && scale>0)
  {
    ft_input_event input={.geometry_revision=state.input.geometry.revision,.modifiers=uishell_jackstay_modifiers(signal.event_flags)};
    input.x=Clamp(0,(mouse.x-image.x0)/(image.x1-image.x0),1)*state.input.geometry.width;
    input.y=Clamp(0,(mouse.y-image.y0)/(image.y1-image.y0),1)*state.input.geometry.height;
    if((inside || v->buttons) && (mouse.x!=v->mouse.x || mouse.y!=v->mouse.y)) {input.kind=FT_INPUT_MOTION;wh_js_send(v->session,&input);}
    struct {U32 down,up,button;} buttons[]={
      {UI_SignalFlag_LeftPressed,UI_SignalFlag_LeftReleased,1},
      {UI_SignalFlag_MiddlePressed,UI_SignalFlag_MiddleReleased,2},
      {UI_SignalFlag_RightPressed,UI_SignalFlag_RightReleased,3}};
    for(U64 i=0;i<ArrayCount(buttons);++i) {
      U32 bit=1u<<i;
      input.kind=FT_INPUT_BUTTON;input.button=buttons[i].button;
      // A quick click can deliver both transitions in one shell frame.
      if(inside && (signal.f&buttons[i].down)) {
        input.action=FT_INPUT_DOWN;
        if(wh_js_send(v->session,&input))v->buttons|=bit;
      }
      if((v->buttons&bit) && (signal.f&buttons[i].up)) {
        input.action=FT_INPUT_UP;
        if(wh_js_send(v->session,&input))v->buttons&=~bit;
      }
    }
    for(UI_Event *event=0;ui_next_event(&event);) {
      if(event->kind==UI_EventKind_Scroll && inside) {
        ft_input_event scroll={.kind=FT_INPUT_SCROLL,.geometry_revision=state.input.geometry.revision,
          .pointer_x=input.x,.pointer_y=input.y,.x=event->delta_2f32.x/30.0,.y=event->delta_2f32.y/30.0,
          .scroll_unit=FT_INPUT_SCROLL_LINE,.modifiers=uishell_jackstay_modifiers(event->modifiers)};
        wh_js_send(v->session,&scroll);ui_eat_event(event);
      }
    }
  }
  if(focused)v->mouse=mouse;
}
WH_VIEW_UI_FUNCTION_DEF(jackstay)
{
  Rng2F32 rect = ctx->rect;
  RD_ViewAddress view=ctx->settings.view;
  UIShell_JackstayView *v=uishell_jackstay_find(view);
  if(!v) {v=calloc(1,sizeof(*v));v->view=view;v->next=uishell_jackstay_views;uishell_jackstay_views=v;}
  if(v->disconnecting && wh_js_destroy(&v->session)) {
    v->disconnecting=0;v->content_focus=0;uishell_jackstay_unfocus(v);
  }
  if(!v->initialized)
  {
    v->initialized=1;
    // Local Endpoint names are saved as *_endpoint; POSIX socket paths keep
    // their original *_socket keys.
    String8 paths[3]={uishell_jackstay_address(str8_lit("source")),uishell_jackstay_address(str8_lit("input")),str8_zero()};
    v->mode=UISHELL_JACKSTAY_MODE_COMBINED;
    if(!paths[0].size)
    {
      String8 d3d11=wh_view_setting(ctx, str8_lit("d3d11_endpoint"));
      String8 porthole=wh_view_setting(ctx, str8_lit("porthole_endpoint"));
      if(d3d11.size){v->mode=UISHELL_JACKSTAY_MODE_D3D11;paths[0]=d3d11;paths[1]=str8_zero();}
      else if(porthole.size)
      {
        v->mode=UISHELL_JACKSTAY_MODE_PORTHOLE;paths[0]=porthole;
        paths[1]=ctx->resolution.porthole_session;
        paths[2]=ctx->resolution.attach_token;
      }
      else{v->mode=UISHELL_JACKSTAY_MODE_SEPARATE;paths[0]=uishell_jackstay_address(str8_lit("media"));}
    }
    for(int i=0;i<3;++i){v->sizes[i]=Min(paths[i].size,sizeof(v->paths[i])-1);MemoryCopy(v->paths[i],paths[i].str,v->sizes[i]);v->cursor[i]=v->mark[i]=txt_pt(1,1);}
    // A scripted layout may carry the attach token and a one-shot `connect`;
    // both are consumed here and never saved back. Connecting this way
    // observes only; control still needs an explicit click.
    B32 connect=wh_view_setting_b32(ctx, str8_lit("connect"));
    uishell_jackstay_forget_setting(str8_lit("attach_token"));
    uishell_jackstay_forget_setting(str8_lit("connect"));
    if(connect && v->sizes[0])uishell_jackstay_open(v,0);
  }
  UI_Parent(uishell_jackstay_view_root(v,rect)) UI_Focus(UI_FocusKind_On) UI_PrefWidth(ui_pct(1,0))
  {
    if(!v->session)
    {
      UI_PrefHeight(ui_em(1.8f,1))
      {
        ui_label(str8_lit(UISHELL_JACKSTAY_ADDRESS_LABEL));
        ui_line_edit(&v->cursor[0],&v->mark[0],v->paths[0],sizeof(v->paths[0])-1,&v->sizes[0],str8(v->paths[0],v->sizes[0]),str8_lit("###source"));
        if(ui_clicked(ui_button(uishell_jackstay_mode_label(v->mode))))v->mode=(v->mode+1)%(OS_WINDOWS?UISHELL_JACKSTAY_MODE_COUNT:UISHELL_JACKSTAY_MODE_D3D11);
        if(v->mode==UISHELL_JACKSTAY_MODE_SEPARATE){ui_label(str8_lit("Input endpoint (optional)"));ui_line_edit(&v->cursor[1],&v->mark[1],v->paths[1],sizeof(v->paths[1])-1,&v->sizes[1],str8(v->paths[1],v->sizes[1]),str8_lit("###input"));}
        if(v->mode==UISHELL_JACKSTAY_MODE_PORTHOLE)
        {
          ui_label(str8_lit("Capture session id"));
          ui_line_edit(&v->cursor[1],&v->mark[1],v->paths[1],sizeof(v->paths[1])-1,&v->sizes[1],str8(v->paths[1],v->sizes[1]),str8_lit("###session"));
          ui_label(str8_lit("Attach token (not saved)"));
          ui_line_edit(&v->cursor[2],&v->mark[2],v->paths[2],sizeof(v->paths[2])-1,&v->sizes[2],str8(v->paths[2],v->sizes[2]),str8_lit("###token"));
        }
        B32 ready=v->sizes[0] && (v->mode!=UISHELL_JACKSTAY_MODE_PORTHOLE || (v->sizes[1] && v->sizes[2]));
        if(ui_clicked(ui_button(str8_lit("Connect###connect"))) && ready)uishell_jackstay_open(v,1);
      }
    }
    else uishell_jackstay_session_ui(v,rect);
  }
}
// Whether the system browser can open `url` (Open in browser).
internal B32 uishell_web_browsable(String8 url)
{
  return str8_match(str8_prefix(url,7),str8_lit("http://"),StringMatchFlag_CaseInsensitive) ||
         str8_match(str8_prefix(url,8),str8_lit("https://"),StringMatchFlag_CaseInsensitive);
}
WH_VIEW_UI_FUNCTION_DEF(web)
{
  Rng2F32 rect=ctx->rect;
  RD_ViewAddress view=ctx->settings.view;
  UIShell_JackstayView *v=uishell_jackstay_find(view);
  if(!v){v=calloc(1,sizeof(*v));v->view=view;v->web=1;v->next=uishell_jackstay_views;uishell_jackstay_views=v;}
  if(v->disconnecting && wh_js_destroy(&v->session)){v->disconnecting=0;v->content_focus=0;uishell_jackstay_unfocus(v);}
  if(!v->initialized)
  {
    v->initialized=1;
    // Where it got to, else the page it opens on; neither is the start page.
    String8 url=ctx->resolution.web_url.size?ctx->resolution.web_url:wh_view_setting(ctx,str8_lit("url"));
    snprintf(v->reached,sizeof(v->reached),"%.*s",(int)url.size,(char *)url.str);
    RD_WindowState *window=rd_window_state_from_cfg(cfg_node_from_id(uishell_regs()->window));
    Vec2F32 dim=dim_2f32(rect);
    UIShell_JackstayViewport viewport=uishell_jackstay_viewport(dim,
      wm_layout_scale_from_window(window->os),wm_backing_scale_from_window(window->os));
    uishell_web_launch(v,url,viewport.width,viewport.height);
  }
  uishell_web_poll(v);
  UI_Parent(uishell_jackstay_view_root(v,rect)) UI_Focus(UI_FocusKind_On) UI_PrefWidth(ui_pct(1,0))
  {
    if(v->session)uishell_jackstay_session_ui(v,rect);
    else
    {
      String8 page=str8_cstring(v->page);
      UI_PrefHeight(ui_em(1.8f,1)) UI_Row UI_PrefWidth(ui_pct(1,0)) uishell_web_address_bar(v,v->page);
      UI_PrefHeight(ui_em(1.8f,1))
      {
        if(!v->failure[0])ui_label(str8_lit("Starting…"));
        else
        {
          // Why: the program's exit and the end of what it wrote.
          ui_labelf("Wheelhouse couldn't show %S.",page.size?page:str8_lit("the start page"));
          String8 failure=str8_cstring(v->failure);
          U64 shown=0;
          for(U64 start=0;start<failure.size && shown<12;shown++)
          {
            U64 end=str8_find_needle(failure,start,str8_lit("\n"),0);
            ui_label(str8_substr(failure,r1u64(start,end)));
            start=end+1;
          }
          UI_Row UI_PrefWidth(ui_em(10,1))
          {
            if(ui_clicked(ui_button(str8_lit("Retry###web_retry"))))
            {
              v->failure[0]=0;
              if(v->process){wheelhouse_connector_restart(v->process);v->retrying=1;}
              else uishell_web_launch(v,page,v->present_width,v->present_height);
            }
            if(uishell_web_browsable(page) && ui_clicked(ui_button(str8_lit("Open in browser###web_open"))))wm_open_in_browser(page);
          }
        }
      }
    }
  }
}
#else
internal B32 uishell_jackstay_pending(void){return 0;}
internal B32 uishell_jackstay_release_event(RD_ViewAddress view,WM_Event *event){(void)view;(void)event;return 0;}
internal B32 uishell_jackstay_event(RD_ViewAddress view,WM_Event *event){(void)view;(void)event;return 0;}
internal void uishell_jackstay_rekey(RD_ViewAddress from,RD_ViewAddress to){(void)from;(void)to;}
internal String8 uishell_jackstay_title(RD_ViewAddress view){(void)view;return str8_zero();}
internal void uishell_jackstay_tick(B32 before,B32 quit){(void)before;(void)quit;}
WH_VIEW_UI_FUNCTION_DEF(jackstay){(void)ctx;ui_label(str8_lit("This build does not include Jackstay."));}
WH_VIEW_UI_FUNCTION_DEF(web)
{
  String8 url=wh_view_setting(ctx,str8_lit("url"));
  ui_label(str8_lit("This build does not include web Views."));
  if(url.size && ui_clicked(ui_buttonf("Open in browser###web_open")))wm_open_in_browser(url);
}
#endif
