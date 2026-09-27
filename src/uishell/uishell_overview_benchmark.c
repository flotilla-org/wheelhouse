// Deterministic rendering workload; this intentionally bypasses PTY/VT parsing.
// Replay enters at the same render-update consumer as a live Cleat terminal.
internal void
uishell_overview_benchmark_init(CmdLine *cmd)
{
  UIShell_OverviewBenchmark *b = &uishell_overview_benchmark;
  b->directory = cmd_line_string(cmd, str8_lit("overview_benchmark"));
  if(b->directory.size == 0) { return; }
  b->count = (U32)u64_from_str8(cmd_line_string(cmd, str8_lit("overview_benchmark_count")), 10);
  if(b->count != 1 && b->count != 16 && b->count != 48)
  { fprintf(stderr, "overview benchmark count must be 1, 16 or 48\n"); abort_self(2); }
#if !OS_MAC
  fprintf(stderr, "overview benchmark native resize currently requires macOS\n");
  abort_self(2);
#endif
  if(!cmd_line_has_flag(cmd, str8_lit("user")) || !cmd_line_has_flag(cmd, str8_lit("project")) ||
     cmd_line_has_flag(cmd, str8_lit("andamento_socket")))
  { fprintf(stderr, "benchmark requires isolated --user/--project and no live ingress\n"); abort_self(2); }
  b->enabled = 1;
  b->phase_frames = 30;
  b->screenshots = !cmd_line_has_flag(cmd, str8_lit("overview_benchmark_no_screenshots"));
  String8 path = push_str8f(rd_state->arena, "%S/frames.csv", b->directory);
  b->metrics = fopen((char *)path.str, "w");
  if(b->metrics == 0) { fprintf(stderr, "cannot open benchmark metrics\n"); abort_self(2); }
  fprintf(b->metrics, "frame,phase,frame_us,build_us,deferred,rebuilds,cells_built,terminal_visits,updates,surface_allocations,surface_pixels,zoom_t,width,height\n");
}

internal void
uishell_overview_benchmark_begin(void)
{
  UIShell_OverviewBenchmark *b = &uishell_overview_benchmark;
  if(!b->enabled) { return; }
  b->deferred = b->rebuilds = b->cells_built = b->terminal_visits = b->updates = b->surface_allocations = 0;
  b->begin_us = now_time_us();
  rd_request_frame();
}

internal void
uishell_overview_benchmark_window(RD_WindowState *ws)
{
  UIShell_OverviewBenchmark *b = &uishell_overview_benchmark;
  if(!b->enabled) { return; }
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  if(!b->initialized)
  {
    // Refuse existing workspace content rather than mutating a user's layout.
    if(cfg_node_child_from_string(window, str8_lit("workspace")) != &cfg_nil_node ||
       cfg_node_child_from_string(window, str8_lit("panels")) != &cfg_nil_node)
    { fprintf(stderr, "benchmark needs an empty fixture window\n"); abort_self(2); }
    for(U32 i=0; i<b->count; i++)
    {
      CFG_Node *owner = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
      CFG_Node *panels = cfg_node_new(rd_state->cfg, owner, str8_lit("panels"));
      CFG_Node *panel = cfg_node_new(rd_state->cfg, panels, str8_lit("1"));
      cfg_node_new(rd_state->cfg, panel, str8_lit("selected"));
      CFG_Node *view = rd_cfg_new_view_tab(panel, str8_lit("terminal"), str8_zero(), 1);
      cfg_node_new(rd_state->cfg, view, str8_lit("overview_fixture"));
      b->owners[i] = owner->id; b->views[i] = view->id;
    }
    ws->root_controlled_split_initialized = 1;
    ws->root_controlled_split_selected_workspace_id = b->owners[0];
    b->initialized = 1;
  }
  U32 phase = b->frame/b->phase_frames;
  if(b->frame % b->phase_frames == 0)
  {
    // These are the same state changes made by overview activation/selection.
    ws->workspace_zoom_open = (phase == 1 || phase == 2 || phase >= 4);
    if(phase == 2) { ws->root_controlled_split_selected_workspace_id = b->owners[b->count-1]; }
    if(phase == 4) { ws->root_controlled_split_selected_workspace_id = b->owners[0]; }
#if OS_MAC
    if(phase == 5 || phase == 6)
    {
      MAC_WM_Window *window = mac_wm_window_from_handle(ws->os);
      [window->ns_window setContentSize:NSMakeSize(phase == 5 ? 900 : 1200, phase == 5 ? 600 : 800)];
    }
#endif
  }
}

internal void
uishell_overview_benchmark_feed(CFG_Node *view, UIShell_TerminalCellCache *cache, UIShell_TerminalImageCache *images, U16 cols, U16 rows)
{
  UIShell_OverviewBenchmark *b = &uishell_overview_benchmark;
  U32 id = 0;
  for(; id < b->count && b->views[id] != view->id; id++) {}
  if(id == b->count) { return; }
  U32 kind = id % 4;
  // Static, cursor-only at 2 Hz, row update at 10 Hz, scrolling at 30 Hz.
  U32 source_frame = Min(b->frame, 7*b->phase_frames); // final update, then silence
  U64 tick = kind == 0 ? 0 : source_frame/(kind == 1 ? 30 : kind == 2 ? 6 : 2);
  U64 generation = tick + 1;
  B32 full = cache->cells == 0 || cache->cols != cols || cache->rows != rows ||
    (kind == 3 && generation > cache->render_generation + 1);
  if(!full && cache->render_generation == generation) { return; }
  Temp scratch = scratch_begin(0, 0);
  cleat_render_update update = {.size=sizeof(update), .cols=cols, .rows=rows,
    .render_generation=generation, .dirty=full ? CLEAT_DIRTY_FULL : CLEAT_DIRTY_PARTIAL,
    .cursor={.row=rows-1, .col=(U16)(tick%cols), .visible=kind==1, .style=CLEAT_CURSOR_STYLE_BLOCK}};
  cleat_render_update_op ops[2] = {0};
  if(full || kind != 1)
  {
    U32 row_count = full ? rows : 1;
    cleat_render_cell *cells = push_array(scratch.arena, cleat_render_cell, row_count*cols);
    U32 *text = push_array(scratch.arena, U32, row_count*cols);
    for(U32 row=0; row<row_count; row++)
    {
      for(U32 col=0; col<cols; col++)
      {
        U32 n=row*cols+col;
        U32 logical_row = full ? row : kind == 3 ? rows-1 : rows/2;
        U32 content_tick = (kind == 3 || (kind == 2 && logical_row == rows/2)) ? (U32)tick : 0;
        text[n] = col%16 == 15 ? ' ' : 'a'+(col+logical_row+id+content_tick)%26;
        cells[n] = (cleat_render_cell){.size=sizeof(*cells), .graphemes=&text[n], .grapheme_count=1,
          .style={.size=sizeof(cleat_render_style), .width=CLEAT_CELL_WIDTH_NARROW,
                  .fg={(U8)(140+id%8*12),200,180}, .bg={12,16,20}}};
      }
    }
    U32 op_count=0;
    if(!full && kind==3)
    { ops[op_count++] = (cleat_render_update_op){.size=sizeof(ops[0]), .kind=CLEAT_RENDER_OP_SCROLL_COPY,
        .src_row=1, .dst_row=0, .row_count=rows-1}; }
    ops[op_count++] = (cleat_render_update_op){.size=sizeof(ops[0]),
      .kind=full ? CLEAT_RENDER_OP_FULL_VISIBLE_REPLACE : CLEAT_RENDER_OP_ROW_REPLACE,
      .first_row=full ? 0 : kind==3 ? rows-1 : rows/2, .row_count=row_count,
      .col_count=cols, .cells=cells, .cell_count=row_count*cols};
    update.ops=ops; update.op_count=op_count;
  }
  uishell_terminal_cell_cache_apply_render_update(cache, &update);
  uishell_terminal_image_cache_apply_render_update(images, 0, &update);
  b->updates++;
  scratch_end(scratch);
}

// Read the actual composed stage, after normal surface rendering. The generic
// pass-list readback diagnostic only supports flat UI and cannot replay overview
// surface brackets. Keep this macOS test adapter out of production rendering.
internal R_Readback
uishell_overview_benchmark_readback(Arena *arena, RD_WindowState *ws)
{
  R_Readback result = {0};
#if OS_MAC
  MutexScopeW(r_mtl_state->device_rw_mutex)
  {
    R_MTL_Window *window = r_mtl_window_from_handle(ws->r);
    id<MTLTexture> stage = window->stage_color;
    if(stage != 0)
    {
      U64 width = [stage width], height = [stage height];
      MTLTextureDescriptor *desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm_sRGB width:width height:height mipmapped:NO];
      desc.usage = MTLTextureUsageRenderTarget;
      desc.storageMode = MTLStorageModeShared;
      id<MTLTexture> final = [r_mtl_state->device newTextureWithDescriptor:desc];
      id<MTLCommandBuffer> command = [r_mtl_state->command_queue commandBuffer];
      MTLRenderPassDescriptor *pass = [MTLRenderPassDescriptor renderPassDescriptor];
      pass.colorAttachments[0].texture = final;
      pass.colorAttachments[0].loadAction = MTLLoadActionClear;
      pass.colorAttachments[0].storeAction = MTLStoreActionStore;
      id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
      R_MTL_FinalizeUniforms uniforms = {v2f32(width, height)};
      [encoder setRenderPipelineState:r_mtl_state->finalize_pipeline];
      [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:0];
      [encoder setFragmentTexture:stage atIndex:0];
      [encoder setFragmentSamplerState:r_mtl_state->samplers[R_Tex2DSampleKind_Nearest] atIndex:0];
      [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4];
      [encoder endEncoding];
      [command commit];
      [command waitUntilCompleted];
      if([command status] == MTLCommandBufferStatusCompleted)
      {
        U8 *pixels = push_array_no_zero(arena, U8, width*height*4);
        [final getBytes:pixels bytesPerRow:width*4 fromRegion:MTLRegionMake2D(0,0,width,height) mipmapLevel:0];
        result = (R_Readback){v2s32(width,height), R_Tex2DFormat_BGRA8, str8(pixels,width*height*4)};
      }
      [final release];
    }
  }
#endif
  return result;
}

internal void
uishell_overview_benchmark_end(void)
{
  UIShell_OverviewBenchmark *b = &uishell_overview_benchmark;
  if(!b->enabled || !b->initialized) { return; }
  U64 elapsed = now_time_us()-b->begin_us;
  RD_WindowState *ws = rd_state->first_window_state;
  if(ws == &rd_nil_window_state) { fprintf(stderr, "benchmark window lost\n"); abort_self(2); }
  U64 pixels=0;
  for(RD_SurfaceCacheNode *n=ws->first_surface_cache_node; n; n=n->next)
  { pixels += (U64)n->size.x*n->size.y; }
  Vec2F32 dim=dim_2f32(wm_client_rect_from_window(ws->os));
  U32 phase=b->frame/b->phase_frames;
  fprintf(b->metrics, "%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.6f,%.0f,%.0f\n",
    b->frame,phase,(unsigned long long)elapsed,(unsigned long long)b->build_us,(unsigned long long)b->deferred,(unsigned long long)b->rebuilds,
    (unsigned long long)b->cells_built,(unsigned long long)b->terminal_visits,
    (unsigned long long)b->updates,(unsigned long long)b->surface_allocations,
    (unsigned long long)pixels,ws->workspace_zoom_t,dim.x,dim.y);
  fflush(b->metrics);
  if(b->screenshots && b->frame%b->phase_frames == b->phase_frames-1)
  {
    Temp scratch=scratch_begin(0,0);
    R_Readback rb=uishell_overview_benchmark_readback(scratch.arena, ws);
    if(rb.format != R_Tex2DFormat_BGRA8 || rb.data.size != (U64)rb.size.x*rb.size.y*4)
    { fprintf(stderr,"benchmark checkpoint readback failed\n"); abort_self(2); }
    U64 count=(U64)rb.size.x*rb.size.y;
    U8 *rgb=push_array_no_zero(scratch.arena,U8,count*3);
    for(U64 i=0;i<count;i++) { rgb[3*i]=rb.data.str[4*i+2]; rgb[3*i+1]=rb.data.str[4*i+1]; rgb[3*i+2]=rb.data.str[4*i]; }
    String8List output={0};
    str8_list_push(scratch.arena,&output,push_str8f(scratch.arena,"P6\n%i %i\n255\n",rb.size.x,rb.size.y));
    str8_list_push(scratch.arena,&output,str8(rgb,count*3));
    if(!write_data_list_to_file_path(push_str8f(scratch.arena,"%S/phase-%u.ppm",b->directory,phase),output))
    { fprintf(stderr,"benchmark checkpoint write failed\n"); abort_self(2); }
    scratch_end(scratch);
  }
  b->frame++;
  if(b->frame==8*b->phase_frames)
  {
    fclose(b->metrics); b->metrics=0;
    // All terminal data is in-process fixture storage; there are no child sessions.
    abort_self(0);
  }
}
