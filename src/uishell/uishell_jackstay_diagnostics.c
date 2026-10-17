#if defined(WHEELHOUSE_JACKSTAY)
internal B32
uishell_jackstay_pixel_diagnostics(void)
{
  B32 ok=1;
#if OS_MAC
  Temp scratch=scratch_begin(0,0);
  // Exercise the View's real upload and renderer: channel order, owned
  // texture bytes, and an older image surviving a newer publication.
  for(U32 bgra=0;bgra<2;bgra++)
  {
    U8 pixels[8]={255,0,0,255,0,0,255,255};
    if(bgra){pixels[0]=0;pixels[2]=255;pixels[4]=255;pixels[6]=0;}
    WH_JS_Frame frame={.pixels=pixels,.width=2,.height=1,
      .pixel_format=bgra?FT_PIXEL_FORMAT_BGRA8_UNORM:FT_PIXEL_FORMAT_RGBA8_UNORM};
    R_Handle first=uishell_jackstay_cpu_texture(&frame);
    MemorySet(pixels,0,sizeof(pixels));
    R_Handle second=uishell_jackstay_cpu_texture(&frame);
    DR_Bucket *bucket=dr_bucket_make();
    DR_BucketScope(bucket)
      dr_img(r2f32p(0,0,32,16),r2f32p(0,0,2,1),first,v4f32(1,1,1,1),0,0,0);
    R_Readback image=r_pass_list_readback(scratch.arena,v2s32(32,16),&bucket->passes);
    B32 valid=image.format==R_Tex2DFormat_BGRA8 && image.data.size>=32*16*4;
    if(valid)
    {
      U8 *left=image.data.str+(8*32+8)*4,*right=image.data.str+(8*32+24)*4;
      valid=left[2]>250 && left[0]<5 && right[0]>250 && right[2]<5;
    }
    if(!valid)fprintf(stderr,"FAIL Jackstay %s texture upload/readback\n",bgra?"BGRA":"RGBA");
    ok=valid && ok;
    r_tex2d_release(first);r_tex2d_release(second);
  }
  // Import actual IOSurfaces through Metal and sample with the renderer. The
  // texture must retain the surface after our original reference is dropped.
  for(U32 bgra=0;bgra<2;bgra++)
  {
    NSDictionary *properties=@{(id)kIOSurfaceWidth:@2,(id)kIOSurfaceHeight:@1,
      (id)kIOSurfaceBytesPerElement:@4,(id)kIOSurfacePixelFormat:@(bgra?'BGRA':'RGBA')};
    IOSurfaceRef surface=IOSurfaceCreate((CFDictionaryRef)properties);
    R_Handle texture={0};
    if(surface && IOSurfaceLock(surface,0,0)==kIOReturnSuccess)
    {
      U8 pixels[8]={255,0,0,255,0,0,255,255};
      if(bgra){pixels[0]=0;pixels[2]=255;pixels[4]=255;pixels[6]=0;}
      MemoryCopy(IOSurfaceGetBaseAddress(surface),pixels,sizeof(pixels));
      IOSurfaceUnlock(surface,0,0);
      texture=r_tex2d_open_shared(surface);
    }
    if(surface)CFRelease(surface);
    DR_Bucket *bucket=dr_bucket_make();
    DR_BucketScope(bucket)
      dr_img(r2f32p(0,0,32,16),r2f32p(0,0,2,1),texture,v4f32(1,1,1,1),0,0,0);
    R_Readback image=r_pass_list_readback(scratch.arena,v2s32(32,16),&bucket->passes);
    B32 valid=texture.u64[0] && image.format==R_Tex2DFormat_BGRA8 && image.data.size>=32*16*4;
    if(valid)
    {
      U8 *left=image.data.str+(8*32+8)*4,*right=image.data.str+(8*32+24)*4;
      valid=left[2]>250 && left[0]<5 && right[0]>250 && right[2]<5;
    }
    if(!valid)fprintf(stderr,"FAIL Jackstay %s IOSurface import/readback\n",bgra?"BGRA":"RGBA");
    ok=valid && ok;
    r_tex2d_release(texture);
  }
  // A foreign producer event gates renderer work; export the renderer's
  // completion event back to a separate Metal device and observe its signal.
  id<MTLDevice> external=MTLCreateSystemDefaultDevice();
  id<MTLSharedEvent> ready=[external newSharedEvent];
  MTLSharedEventHandle *ready_handle=[ready newSharedEventHandle];
  R_Handle imported=r_timeline_open_shared(ready_handle);
  R_Handle released=r_timeline_alloc_shared();
  id<MTLSharedEvent> completion=[external newSharedEventWithHandle:r_native_timeline(released)];
  ready.signaledValue=7;
  B32 timelines=imported.u64[0] && released.u64[0] && completion &&
    r_queue_wait(imported,7) && r_queue_signal(released,9);
  U64 deadline=now_time_us()+1000000;
  while(timelines && completion.signaledValue<9 && now_time_us()<deadline)sleep_ms(1);
  timelines=timelines && completion.signaledValue>=9;
  if(!timelines)fprintf(stderr,"FAIL Jackstay Metal readiness/completion timelines\n");
  ok=timelines && ok;
  [completion release];[ready_handle release];[ready release];[external release];
  r_timeline_release(imported);r_timeline_release(released);
  scratch_end(scratch);
#endif
  fprintf(stderr,"Jackstay pixel diagnostics: %s\n",ok?"passed":"FAILED");
  return ok;
}
internal B32
uishell_jackstay_geometry_diagnostics(void)
{
  // The same logical page must keep its size across display conventions.
  // macOS uses points plus a backing scale; Windows scales layout coordinates.
  struct { char *name; F32 layout,backing; U32 width,height; F32 scale; } cases[]=
  {
    {"1x",1,1,800,600,1},
    {"Retina",1,2,800,600,2},
    {"Windows 150%",1.5f,1,800,600,1.5f},
    {"Windows 200%",2,1,800,600,2},
    {"fractional backing",1,1.5f,800,600,1.5f},
    {"layout and backing",1.5f,2,800,600,3},
  };
  B32 ok=1;
  for(U32 i=0;i<ArrayCount(cases);i++)
  {
    Vec2F32 canvas=v2f32(800*cases[i].layout,600*cases[i].layout);
    UIShell_JackstayViewport viewport=uishell_jackstay_viewport(canvas,cases[i].layout,cases[i].backing);
    // A producer honoring both hints must deliver one texel per backing pixel.
    B32 match=viewport.width==cases[i].width && viewport.height==cases[i].height &&
      viewport.scale==cases[i].scale &&
      viewport.width*viewport.scale==canvas.x*cases[i].backing &&
      viewport.height*viewport.scale==canvas.y*cases[i].backing;
    if(!match)fprintf(stderr,"FAIL Jackstay %s: requested %ux%u @ %.2fx for %.0fx%.0f canvas\n",
      cases[i].name,viewport.width,viewport.height,viewport.scale,canvas.x,canvas.y);
    ok=match && ok;
  }
  UIShell_JackstayViewport empty=uishell_jackstay_viewport(v2f32(0,0),1,2);
  ok=empty.width==0 && empty.height==0 && empty.scale==2 && ok;
  // Truncate preferred logical size, rather than requesting outside the pane.
  UIShell_JackstayViewport fractional=uishell_jackstay_viewport(v2f32(1200.75f,900.75f),1.5f,1);
  ok=fractional.width==800 && fractional.height==600 && fractional.scale==1.5f && ok;
  fprintf(stderr,"Jackstay geometry diagnostics: %s\n",ok?"passed":"FAILED");
  return ok;
}
// Build the actual address bar and canvas with toolkit events, without sending
// OS input. The optional reference producer also verifies navigation delivery.
internal void
uishell_jackstay_input_test_events(RD_WindowState *ws, UIShell_JackstayView *v, UI_Event event, UI_Event trailing)
{
  UI_IconInfo icons=ws->ui->icon_info;
  UI_AnimationInfo animation={0};
  UI_EventNode node={.v=event},tail={.v=trailing};
  UI_EventList events={0};
  if(event.kind) {events.first=events.last=&node;events.count=1;}
  if(trailing.kind) {node.next=&tail;tail.prev=&node;events.last=&tail;events.count++;}
  ui_begin_build(ws->os,&events,&icons,ws->theme,&animation,1.f/60,1.f/60);
  ui_state->mouse=event.kind?event.pos:v2f32(-100,-100);
  ui_set_next_rect(r2f32p(0,0,640,480));
  ui_set_next_child_layout_axis(Axis2_Y);
  UI_Focus(UI_FocusKind_Root) UI_Focus(UI_FocusKind_On)
  {
    UI_Box *root=ui_build_box_from_key(0,ui_key_make(7142));
    UI_Parent(root) UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
    {
      UIShell_ControlledSplit split={.owner_cfg=cfg_node_from_id(ws->cfg_id)};
      uishell_sidebar_footer_ui(r2f32p(0,480,640,520),&split);
      Temp scratch=scratch_begin(0,0);
      CFG_Node *owner=rd_cfg_from_panel_owner(uishell_regs()->panel.owner);
      UIShell_WorkspaceMount mount=uishell_workspace_mount_from_owner_cfg(scratch.arena,cfg_node_from_id(ws->cfg_id),owner);
      rd_panel_area_ui(scratch,r2f32p(0,0,640,480),r2f32p(0,0,640,480),ws,&mount,1,0,0,0,0);
      scratch_end(scratch);
    }
  }
  ui_end_build();
}

internal void
uishell_jackstay_input_test_frame(RD_WindowState *ws, UIShell_JackstayView *v, UI_Event event)
{
  uishell_jackstay_input_test_events(ws,v,event,(UI_Event){0});
}

// Resolve controls through their actual keyed ancestors, including View
// surfaces. The fixture supplies no focus-navigation scope of its own.
internal UI_Box *
uishell_jackstay_input_test_box(String8 name)
{
  for(UI_Box *box=ui_state->root;!ui_box_is_nil(box);box=ui_box_rec_df_pre(box,ui_state->root).next)
  for(UI_Box *p=box->parent;!ui_box_is_nil(p);p=p->parent)
  {if(ui_key_match(box->key,ui_key_from_string(p->key,name)))return box;}
  return &ui_nil_box;
}
internal UI_Box *
uishell_jackstay_input_test_focus_root(UI_Box *box)
{
  for(UI_Box *p=box->parent;!ui_box_is_nil(p);p=p->parent)
  {if(p->flags & UI_BoxFlag_DefaultFocusNav)return p;}
  return &ui_nil_box;
}

internal B32
uishell_jackstay_input_diagnostics(RD_WindowState *ws)
{
  Temp scratch=scratch_begin(0,0);
  UI_State *saved_ui=ui_state,*saved_window_ui=ws->ui,*test=ui_state_alloc();
  UIShell_JackstayView *saved_views=uishell_jackstay_views;
  UIShell_CmdList saved_cmds=rd_state->cmds[0];
  CFG_KeyMap *saved_keys=rd_state->key_map;
  UIShell_SidebarState *saved_sidebar=ws->sidebar,sidebar={0};
  ws->sidebar=&sidebar;
  // An empty-profile first frame precedes the default-bindings command.
  // Use explicit records through the production config/key-map reader.
  CFG_Node *bindings=cfg_node_new(rd_state->cfg,cfg_node_child_from_string(cfg_node_root(),str8_lit("user")),str8_lit("keybindings"));
  CFG_Node *palette_binding=cfg_node_new(rd_state->cfg,bindings,str8_zero());
  cfg_node_new(rd_state->cfg,palette_binding,str8_lit("open_palette"));
  CFG_Node *palette_key=cfg_node_new(rd_state->cfg,palette_binding,str8_lit("p"));
  cfg_node_new(rd_state->cfg,palette_binding,str8_lit("accel"));
  cfg_node_new(rd_state->cfg,palette_binding,str8_lit("shift"));
  CFG_Node *accept_binding=cfg_node_new(rd_state->cfg,bindings,str8_zero());
  cfg_node_new(rd_state->cfg,accept_binding,str8_lit("accept"));
  cfg_node_new(rd_state->cfg,accept_binding,wm_key_cfg_name_table[WM_Key_Return]);
  rd_state->key_map=cfg_key_map_from_cfg(scratch.arena);
  CFG_Node *owner=cfg_node_new(rd_state->cfg,cfg_node_from_id(ws->cfg_id),str8_lit("workspace"));
  RD_PanelAddress panel=rd_fixture_leaf(rd_fixture_owner(owner));
  CFG_Node *cfg=rd_fixture_view(panel,str8_lit("web"),str8_zero(),1);
  rd_panel_focus_mark(panel);
  char *producer=getenv("WHEELHOUSE_INPUT_TEST_ENDPOINT");
  WH_JS_Endpoint endpoint={producer?producer:"wheelhouse-input-diagnostic","",1,WH_JS_MEDIA_CPU,0,0};
  UIShell_JackstayView v={.view=rd_view_address_from_cfg(cfg),.web=1,.initialized=1,.content_focus=1,.focused=1};
  v.session=wh_js_create(endpoint,(WH_JS_Gpu){0},0,0);
  if(!v.session) {ws->sidebar=saved_sidebar;rd_state->key_map=saved_keys;cfg_node_release(rd_state->cfg,bindings);cfg_node_release(rd_state->cfg,owner);ui_state_release(test);scratch_end(scratch);return 0;}
  snprintf(v.page,sizeof(v.page),"https://example.invalid/");
  uishell_jackstay_views=&v;
  ws->ui=test;ui_select_state(test);
  B32 ok=uishell_jackstay_pixel_diagnostics();
  if(producer)
  {
    wh_js_connect(v.session,true);
    WH_JS_State state={0};
    for(U32 i=0;i<600;i++)
    {
      wh_js_snapshot(v.session,&state,0,0);if(state.control && state.navigation_capabilities)break;
      if(!state.control && !state.connecting && i%20==0)wh_js_connect(v.session,true);
      sleep_ms(5);
    }
    if(!state.control || !state.navigation_capabilities)
    {fprintf(stderr,"FAIL Jackstay input producer connection\n");ok=0;}
  }
#define JSInputCheck(x) do{if(!(x)){fprintf(stderr,"FAIL Jackstay input line %d: %s\n",__LINE__,#x);ok=0;}}while(0)
  UIShell_RegsScope(.window=ws->cfg_id,.panel=panel,.view=v.view,.tab=v.view)
  {
    for(U32 frame=0;frame<3;frame++)uishell_jackstay_input_test_frame(ws,&v,(UI_Event){0});
    UI_Box *address=uishell_jackstay_input_test_box(str8_lit("###address"));
    UI_Key address_key=address->key;
    JSInputCheck(!ui_box_is_nil(address));
    Vec2F32 point=center_2f32(address->rect);
    uishell_jackstay_input_test_frame(ws,&v,(UI_Event){.kind=UI_EventKind_Press,.key=WM_Key_LeftMouseButton,.pos=point});
    uishell_jackstay_input_test_frame(ws,&v,(UI_Event){.kind=UI_EventKind_Release,.key=WM_Key_LeftMouseButton,.pos=point});
    JSInputCheck(!v.content_focus);
    UI_Box *root=uishell_jackstay_input_test_focus_root(address);
    JSInputCheck(!ui_box_is_nil(root));
    JSInputCheck(ui_key_match(root->default_nav_focus_active_key,address_key));
    // Ordinary typing must reach the real text-edit consumer after page focus.
    WM_Event text={.kind=WM_EventKind_Text,.window=ws->os,.character='x',.source_key=WM_Key_X};
    JSInputCheck(!uishell_route_content_activation(ws,&text,0));
    uishell_jackstay_input_test_frame(ws,&v,(UI_Event){.kind=UI_EventKind_Text,.string=str8_lit("x")});
    JSInputCheck(v.address_size==1 && v.address[0]=='x');
    snprintf(v.page,sizeof(v.page),"https://example.invalid/new");
    uishell_jackstay_input_test_frame(ws,&v,(UI_Event){0});
    JSInputCheck(v.address_size==1 && v.address[0]=='x');
    // Only the address field owns keyboard focus. Other controls must not
    // inherit its highlight, and the sidebar footer must not steal Enter.
    UI_Box *idle_canvas=uishell_jackstay_input_test_box(str8_lit("jackstay_canvas"));
    JSInputCheck(!(idle_canvas->flags & (UI_BoxFlag_FocusHot|UI_BoxFlag_FocusActive)));
    for(UI_Box *box=root->first;!ui_box_is_nil(box);box=ui_box_rec_df_pre(box,root).next)
    {
      if(box->flags & UI_BoxFlag_KeyboardClickable && !ui_key_match(box->key,address_key))
      {JSInputCheck(!(box->flags & (UI_BoxFlag_FocusHot|UI_BoxFlag_FocusActive)));}
    }
    // Return stays with the host field and resolves through the real binding.
    WM_Event return_key={.kind=WM_EventKind_Press,.window=ws->os,.key=WM_Key_Return};
    JSInputCheck(!uishell_route_content_activation(ws,&return_key,0));
    String8 accept=cfg_command_from_menu_or_binding(scratch.arena,rd_state->key_map,&return_key);
    JSInputCheck(str8_match(accept,str8_lit("accept"),0));
    ws->ui_events=(UI_EventList){0};
    JSInputCheck(uishell_dispatch_ui_event_command(accept));
    JSInputCheck(ws->ui_events.count==1);
    UI_Event enter=ws->ui_events.first->v;
    ws->ui_events=(UI_EventList){0};
    uishell_jackstay_input_test_frame(ws,&v,enter);
    JSInputCheck(!test->next_ctx_menu_open);
    JSInputCheck(ui_key_match(root->default_nav_focus_next_active_key,ui_key_zero()));
    if(producer)
    {
      WH_JS_State state={0};
      for(U32 i=0;i<400;i++)
      {wh_js_snapshot(v.session,&state,0,0);if(!strcmp(state.url,"https://x"))break;sleep_ms(5);}
      JSInputCheck(!strcmp(state.url,"https://x"));
    }
    // Explicitly clicking content transfers ownership back without waiting for
    // the text-field marker to expire on a later frame.
    UI_Box *canvas=uishell_jackstay_input_test_box(str8_lit("jackstay_canvas"));
    UI_Key canvas_key=canvas->key;
    Vec2F32 content_point=center_2f32(canvas->rect);
    uishell_jackstay_input_test_frame(ws,&v,(UI_Event){.kind=UI_EventKind_Press,.key=WM_Key_LeftMouseButton,.pos=content_point});
    uishell_jackstay_input_test_frame(ws,&v,(UI_Event){.kind=UI_EventKind_Release,.key=WM_Key_LeftMouseButton,.pos=content_point});
    root=uishell_jackstay_input_test_focus_root(ui_box_from_key(canvas_key));
    JSInputCheck(v.content_focus && !ui_text_field_focus_in(test));
    JSInputCheck(ui_key_match(root->default_nav_focus_active_key,canvas_key));
    canvas=ui_box_from_key(canvas_key);
    JSInputCheck((canvas->flags & UI_BoxFlag_FocusActive) && !(canvas->flags & UI_BoxFlag_FocusActiveDisabled));
    uishell_jackstay_input_test_frame(ws,&v,(UI_Event){0});
    JSInputCheck(v.focused==wm_window_is_focused(ws->os));
    // A click and typing can arrive together, before a field has a build in
    // which it is already active. That first character must not disappear.
    uishell_jackstay_input_test_events(ws,&v,
      (UI_Event){.kind=UI_EventKind_Press,.key=WM_Key_LeftMouseButton,.pos=point},
      (UI_Event){.kind=UI_EventKind_Text,.string=str8_lit("y")});
    JSInputCheck(v.address_size==1 && v.address[0]=='y');
    uishell_jackstay_input_test_frame(ws,&v,(UI_Event){.kind=UI_EventKind_Release,.key=WM_Key_LeftMouseButton,.pos=point});
    ui_release_text_field_focus();
    // The palette must win before raw embedded input, using the real key map.
    v.focused=v.content_focus=1;
    WM_Event palette={.kind=WM_EventKind_Press,.window=ws->os,.key=WM_Key_P,.modifiers=WM_Modifier_Accel|WM_Modifier_Shift};
    String8 binding=cfg_command_from_menu_or_binding(scratch.arena,rd_state->key_map,&palette);
    JSInputCheck(str8_match(binding,str8_lit("open_palette"),0));
    JSInputCheck(uishell_app_cmd_info_from_string(binding).flags & UIShell_CmdFlag_GlobalShortcut);
    JSInputCheck(!rd_state->popup_active && !ws->query_is_active && !ws->menu_bar_focused && !test->hover_card_focus);
    U64 before=rd_state->cmds[0].count;
    JSInputCheck(uishell_route_content_activation(ws,&palette,0));
    JSInputCheck(rd_state->cmds[0].count==before+1);
    JSInputCheck(rd_state->cmds[0].last && str8_match(rd_state->cmds[0].last->cmd.regs->cmd_name,str8_lit("open_palette"),0));
    // Repeats, physical text, and release belong to that activation even if
    // focus has changed since key-down. Unrelated text remains available.
    WM_Event correlated={.kind=WM_EventKind_Text,.window=ws->os,.character='P',.source_key=WM_Key_P};
    JSInputCheck(uishell_route_edit_activation(scratch.arena,ws,&correlated,0));
    JSInputCheck(uishell_route_edit_activation(scratch.arena,ws,&palette,0));
    WM_Event release={.kind=WM_EventKind_Release,.window=ws->os,.key=WM_Key_P};
    JSInputCheck(uishell_route_edit_activation(scratch.arena,ws,&release,0));
    JSInputCheck(!uishell_route_edit_activation(scratch.arena,ws,&text,0));
    // A rebound palette shortcut has the same ownership; ordinary content
    // keys and non-global bindings are still left to the producer.
    cfg_node_release(rd_state->cfg,palette_key);
    palette_key=cfg_node_new(rd_state->cfg,palette_binding,str8_lit("k"));
    rd_state->key_map=cfg_key_map_from_cfg(scratch.arena);
    WM_Event rebound=palette;rebound.key=WM_Key_K;
    before=rd_state->cmds[0].count;
    JSInputCheck(uishell_route_content_activation(ws,&rebound,0));
    JSInputCheck(rd_state->cmds[0].count==before+1);
    WM_Event lost={.kind=WM_EventKind_WindowLoseFocus,.window=ws->os};
    uishell_route_edit_activation(scratch.arena,ws,&lost,0);
    JSInputCheck(!ws->edit_chord_held[WM_Key_K]);
    WM_Event ordinary={.kind=WM_EventKind_Press,.window=ws->os,.key=WM_Key_A};
    before=rd_state->cmds[0].count;
    JSInputCheck(uishell_route_content_activation(ws,&ordinary,0));
    JSInputCheck(rd_state->cmds[0].count==before);
    CFG_Node *local_binding=cfg_node_new(rd_state->cfg,bindings,str8_zero());
    cfg_node_new(rd_state->cfg,local_binding,str8_lit("open_url"));
    cfg_node_new(rd_state->cfg,local_binding,str8_lit("r"));
    cfg_node_new(rd_state->cfg,local_binding,str8_lit("accel"));
    rd_state->key_map=cfg_key_map_from_cfg(scratch.arena);
    WM_Event local=ordinary;local.key=WM_Key_R;local.modifiers=WM_Modifier_Accel;
    JSInputCheck(uishell_route_content_activation(ws,&local,0));
    JSInputCheck(rd_state->cmds[0].count==before);
    WM_Event other_window=palette;other_window.window=(WM_Window){~0ull};
    JSInputCheck(!uishell_route_content_activation(ws,&other_window,0));
    // Release capture stays available even if a global command is rebound
    // onto the reserved chord.
    cfg_node_release(rd_state->cfg,palette_key);
    cfg_node_new(rd_state->cfg,palette_binding,wm_key_cfg_name_table[WM_Key_Esc]);
    cfg_node_new(rd_state->cfg,palette_binding,str8_lit("ctrl"));
    WM_Event escape={.kind=WM_EventKind_Press,.window=ws->os,.key=WM_Key_Esc,
      .modifiers=WM_Modifier_Ctrl|WM_Modifier_Shift|WM_Modifier_Accel};
    rd_state->key_map=cfg_key_map_from_cfg(scratch.arena);
    JSInputCheck(str8_match(cfg_command_from_menu_or_binding(scratch.arena,rd_state->key_map,&escape),str8_lit("open_palette"),0));
    JSInputCheck(uishell_route_content_activation(ws,&escape,0));
    JSInputCheck(!v.focused && !v.content_focus && rd_state->cmds[0].count==before);
    // A click outside the image relinquishes content before later typing in
    // the same WM event batch, before a UI build can update focus.
    v.focused=v.content_focus=1;v.image_rect=ui_box_from_key(canvas_key)->rect;
    WM_Event outside={.kind=WM_EventKind_Press,.window=ws->os,.key=WM_Key_LeftMouseButton,.pos=point};
    JSInputCheck(!uishell_route_content_activation(ws,&outside,0));
    JSInputCheck(!v.focused && !v.content_focus);
    JSInputCheck(!uishell_route_content_activation(ws,&text,0));
    // Relinquishing the field leaves normal button activation available.
    ui_release_text_field_focus();ui_clear_edit_owner(test);
    uishell_jackstay_input_test_frame(ws,&v,enter);
    JSInputCheck(test->next_ctx_menu_open);
    ui_ctx_menu_close();
  }
#undef JSInputCheck
  wh_js_stop(v.session);
  for(U32 i=0;i<400;i++)
  {WH_JS_State state;wh_js_snapshot(v.session,&state,0,0);if(state.stopped)break;sleep_ms(5);}
  wh_js_destroy(&v.session);
  uishell_jackstay_views=saved_views;ws->ui=saved_window_ui;ui_select_state(saved_ui);
  rd_state->cmds[0]=saved_cmds;
  ws->sidebar=saved_sidebar;
  rd_state->key_map=saved_keys;cfg_node_release(rd_state->cfg,bindings);
  cfg_node_release(rd_state->cfg,owner);ui_state_release(test);scratch_end(scratch);
  fprintf(stderr,"Jackstay input diagnostics: %s\n",ok?"passed":"FAILED");
  return ok;
}
#endif
