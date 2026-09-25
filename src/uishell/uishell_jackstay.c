// Jackstay presentation: shell-owned focus/texture, independently owned workers.
#if defined(WHEELHOUSE_JACKSTAY)
#include "jackstay/wheelhouse_jackstay.c"
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
  CFG_ID id;
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
};
global UIShell_JackstayView *uishell_jackstay_views;
internal void uishell_jackstay_wake(void *unused) { (void)unused; wm_send_wakeup_event(); }
internal UIShell_JackstayView *uishell_jackstay_find(CFG_ID id)
{
  for(UIShell_JackstayView *v=uishell_jackstay_views;v;v=v->next) if(v->id==id)return v;
  return 0;
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
internal B32 uishell_jackstay_event(CFG_ID id,WM_Event *event)
{
  UIShell_JackstayView *v=uishell_jackstay_find(id);
  if(!v || !v->session || !v->focused)return 0;
  if(event->kind==WM_EventKind_WindowLoseFocus) {uishell_jackstay_unfocus(v);return 0;}
  if(event->kind==WM_EventKind_Press && event->key==WM_Key_Esc &&
     (event->modifiers&(WM_Modifier_Ctrl|WM_Modifier_Shift))==(WM_Modifier_Ctrl|WM_Modifier_Shift))
  { v->content_focus=0;uishell_jackstay_unfocus(v);return 1; }
  if(event->kind!=WM_EventKind_Press && event->kind!=WM_EventKind_Release && event->kind!=WM_EventKind_Text)return 0;
  if(event->key==WM_Key_LeftMouseButton || event->key==WM_Key_RightMouseButton || event->key==WM_Key_MiddleMouseButton)return 0;
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
internal void uishell_jackstay_tick(B32 before,B32 quit)
{
  for(UIShell_JackstayView **link=&uishell_jackstay_views;*link;)
  {
    UIShell_JackstayView *v=*link;
    if(before){v->focus_requested=0;}
    else if(!v->focus_requested)uishell_jackstay_unfocus(v);
    CFG_Node *cfg=cfg_node_from_id(v->id);
    if(quit || cfg==&cfg_nil_node || !str8_match(cfg->string,str8_lit("jackstay"),0))
    {
      if(!v->closing)uishell_jackstay_drop_frames(v);
      v->closing=1;if(v->session)wh_js_stop(v->session);
    }
    if(v->closing && (!v->session || wh_js_destroy(&v->session)))
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
// view settings each one saves.
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
  CFG_Node *setting=cfg_node_child_from_string(cfg_node_from_id(uishell_regs()->view),key);
  if(setting!=&cfg_nil_node)cfg_node_release(rd_state->cfg,setting);
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
RD_VIEW_UI_FUNCTION_DEF(jackstay)
{
  (void)eval;
  CFG_ID id=uishell_regs()->view;
  UIShell_JackstayView *v=uishell_jackstay_find(id);
  if(!v) {v=calloc(1,sizeof(*v));v->id=id;v->next=uishell_jackstay_views;uishell_jackstay_views=v;}
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
      String8 d3d11=rd_view_setting_from_name(str8_lit("d3d11_endpoint"));
      String8 porthole=rd_view_setting_from_name(str8_lit("porthole_endpoint"));
      if(d3d11.size){v->mode=UISHELL_JACKSTAY_MODE_D3D11;paths[0]=d3d11;paths[1]=str8_zero();}
      else if(porthole.size)
      {
        v->mode=UISHELL_JACKSTAY_MODE_PORTHOLE;paths[0]=porthole;
        paths[1]=rd_view_setting_from_name(str8_lit("porthole_session"));
        paths[2]=rd_view_setting_from_name(str8_lit("attach_token"));
      }
      else{v->mode=UISHELL_JACKSTAY_MODE_SEPARATE;paths[0]=uishell_jackstay_address(str8_lit("media"));}
    }
    for(int i=0;i<3;++i){v->sizes[i]=Min(paths[i].size,sizeof(v->paths[i])-1);MemoryCopy(v->paths[i],paths[i].str,v->sizes[i]);v->cursor[i]=v->mark[i]=txt_pt(1,1);}
    // A scripted layout may carry the attach token and a one-shot `connect`;
    // both are consumed here and never saved back. Connecting this way
    // observes only; control still needs an explicit click.
    B32 connect=rd_view_setting_b32_from_name(str8_lit("connect"));
    uishell_jackstay_forget_setting(str8_lit("attach_token"));
    uishell_jackstay_forget_setting(str8_lit("connect"));
    if(connect && v->sizes[0])uishell_jackstay_open(v,0);
  }
  ui_set_next_pref_width(ui_px(dim_2f32(rect).x,1));
  ui_set_next_pref_height(ui_px(dim_2f32(rect).y,1));
  UI_Column UI_PrefWidth(ui_pct(1,0))
  {
    if(!v->session)
    {
      UI_PrefHeight(ui_em(1.8f,1))
      {
        ui_label(str8_lit(UISHELL_JACKSTAY_ADDRESS_LABEL));
        ui_line_edit(&v->cursor[0],&v->mark[0],v->paths[0],sizeof(v->paths[0])-1,&v->sizes[0],str8(v->paths[0],v->sizes[0]),str8_lit("###source"));
        if(ui_clicked(ui_button(uishell_jackstay_mode_label(v->mode))))v->mode=(v->mode+1)%UISHELL_JACKSTAY_MODE_COUNT;
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
    else
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
      UI_PrefHeight(ui_em(1.8f,1)) UI_Row
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
      ui_set_next_pref_height(ui_px(Max(0,dim_2f32(rect).y-ui_top_font_size()*1.8f),1));
      UI_Key canvas_key=ui_key_from_string(ui_active_seed_key(),str8_lit("jackstay_canvas"));
      // Query panel eligibility without marking every control as focused.
      B32 panel_active=0;
      UI_Focus(UI_FocusKind_On){panel_active=ui_is_focus_active();}
      if(v->content_focus && panel_active)ui_set_auto_focus_active_key(canvas_key);
      UI_Box *canvas=ui_build_box_from_string(UI_BoxFlag_Clickable|UI_BoxFlag_ClickToFocus|UI_BoxFlag_Clip|UI_BoxFlag_DrawBackground|UI_BoxFlag_DisableFocusOverlay|UI_BoxFlag_DisableFocusBorder,str8_lit("jackstay_canvas"));
      UI_Signal signal=ui_signal_from_box(canvas);
      B32 clicked=!!(signal.f&UI_SignalFlag_LeftPressed);
      if(clicked){v->content_focus=1;uishell_cmd("focus_panel");}
      B32 focused=v->content_focus && (panel_active || clicked);
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
  }
  (void)rect;
}
#else
internal B32 uishell_jackstay_pending(void){return 0;}
internal B32 uishell_jackstay_event(CFG_ID id,WM_Event *event){(void)id;(void)event;return 0;}
internal void uishell_jackstay_tick(B32 before,B32 quit){(void)before;(void)quit;}
RD_VIEW_UI_FUNCTION_DEF(jackstay){(void)eval;(void)rect;ui_label(str8_lit("This build does not include Jackstay."));}
#endif
