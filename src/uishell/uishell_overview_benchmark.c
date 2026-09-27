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
  b->command = cmd_line_string(cmd, str8_lit("overview_benchmark_command"));
  b->live = b->command.size != 0;
  b->live_transitions = cmd_line_has_flag(cmd, str8_lit("overview_benchmark_live_transitions"));
  b->live_phase = max_U32;
  b->interactive = b->live || cmd_line_has_flag(cmd, str8_lit("overview_benchmark_interactive"));
  b->start_us = now_time_us();
  b->busy = cmd_line_has_flag(cmd, str8_lit("overview_benchmark_busy"));
  b->interrupt = cmd_line_has_flag(cmd, str8_lit("overview_benchmark_interrupt"));
  b->enabled = 1;
  rd_state->frame_metrics = &b->shell_metrics;
  uishell_terminal_metrics = &b->terminal_metrics;
  rd_state->frame_replay.fixed_dt = b->interactive ? 0 : 1.f/60.f;
  rd_state->frame_replay.suppress_input = !b->interactive;
  rd_state->frame_replay.prepare_window = uishell_overview_benchmark_window;
  b->phase_frames = 30;
  b->screenshots = !cmd_line_has_flag(cmd, str8_lit("overview_benchmark_no_screenshots"));
  String8 path = push_str8f(rd_state->arena, "%S/frames.csv", b->directory);
  b->metrics = fopen((char *)path.str, "w");
  if(b->metrics == 0) { fprintf(stderr, "cannot open benchmark metrics\n"); abort_self(2); }
  fprintf(b->metrics, "frame,phase,frame_us,build_us,deferred,rebuilds,cells_built,terminal_visits,updates,surface_allocations,surface_pixels,zoom_t,width,height,surface_us,glyph_us,window_us,surface_admissions,surface_deferred,rect_instances,provider_starts,provider_start_us,background_starts,live_final_count,provider_resize_us,provider_update_us,empty_layout_resizes,event_wait_us,background_updates,snapshot_deferred,live_image_terminals,live_image_resources,live_image_seen,background_surface_redraws\n");
}

internal void
uishell_overview_benchmark_begin(void)
{
  UIShell_OverviewBenchmark *b = &uishell_overview_benchmark;
  if(!b->enabled) { return; }
  MemoryZeroStruct(&b->shell_metrics);
  MemoryZeroStruct(&b->terminal_metrics);
  b->shell_metrics.begin_us = now_time_us();
  b->source_frame = b->interactive ? (U32)((b->shell_metrics.begin_us-b->start_us)*60/1000000) : Min(b->frame, 7*b->phase_frames);
  if(!b->live) { rd_request_frame(); }
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
      CFG_Node *view = rd_cfg_new_view_tab(panel, str8_lit("terminal"), b->command, 1);
      cfg_node_new(rd_state->cfg, view, b->live ? str8_lit("overview_live") : str8_lit("overview_fixture"));
      b->owners[i] = owner->id; b->views[i] = view->id;
    }
    ws->root_controlled_split_initialized = 1;
    ws->root_controlled_split_selected_workspace_id = b->owners[0];
    b->initialized = 1;
    if(b->live) { ws->workspace_zoom_open = 1; }
  }
  if(b->live && b->live_transitions)
  {
    U32 phase = Min(5, (b->shell_metrics.begin_us-b->start_us)/2000000);
    if(phase != b->live_phase)
    {
      b->live_phase = phase;
      ws->workspace_zoom_open = phase != 2;
      if(phase == 1) { ws->root_controlled_split_selected_workspace_id = b->owners[b->count-1]; }
      if(phase == 3) { ws->root_controlled_split_selected_workspace_id = b->owners[0]; }
#if OS_MAC
      if(phase == 4 || phase == 5)
      {
        MAC_WM_Window *window = mac_wm_window_from_handle(ws->os);
        [window->ns_window setContentSize:NSMakeSize(phase == 4 ? 900 : 1200, phase == 4 ? 600 : 800)];
      }
#endif
    }
  }
  if(b->interactive) { return; }
  if(b->interrupt)
  {
    if(b->frame == 32 || b->frame == 36)
    {
      ws->root_controlled_split_selected_workspace_id = b->owners[b->frame == 32 ? Min(1, b->count-1) : b->count/2];
      ws->workspace_zoom_open = 0;
    }
    if(b->frame == 34 || b->frame == 39) { ws->workspace_zoom_open = 1; }
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
  U32 kind = b->busy ? 3 : id % 4;
  // Static, cursor-only at 2 Hz, row update at 10 Hz, scrolling at 30 Hz.
  U32 source_frame = b->source_frame; // scripted replay stops output in its final phase
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
  b->terminal_metrics.updates++;
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
  U64 elapsed = now_time_us()-b->shell_metrics.begin_us;
  RD_WindowState *ws = rd_state->first_window_state;
  if(ws == &rd_nil_window_state)
  {
    if(b->interactive) { fclose(b->metrics); b->metrics = 0; b->enabled = 0; return; }
    fprintf(stderr, "benchmark window lost\n"); abort_self(2);
  }
  B32 pending[48] = {0};
  // Selected/expanding workspaces must never wait behind the preview queue.
  for(U64 i = 0; i < ws->workspace_surface_entry_count; i += 1)
  {
    RD_WorkspaceSurfaceEntry *entry = &ws->workspace_surface_entries[i];
    for(U32 j = 0; j < b->count; j += 1)
    {
      if(b->owners[j] == entry->workspace_id) { pending[j] = entry->defer_render; break; }
    }
    if(entry->full_res || entry->composite)
    {
      RD_SurfaceCacheNode *node = rd_window_surface_node_lookup(ws, entry->box_key);
      if(entry->defer_render || node == 0 || node->rendered_hash == 0 ||
         node->workspace_content_version != entry->content_version_accum)
      { fprintf(stderr, "selected workspace missed render admission\n"); abort_self(2); }
    }
  }
  for(U32 i = 0; i < b->count; i += 1)
  {
    if(!pending[i]) { b->pending_since[i] = 0; }
    else
    {
      if(b->pending_since[i] == 0) { b->pending_since[i] = b->frame+1; }
      if(b->frame+1-b->pending_since[i] > (b->count+3)/4+2)
      { fprintf(stderr, "preview render queue starved workspace %u\n", i); abort_self(2); }
    }
  }
  U64 instances = 0;
  for(R_PassNode *p = ws->draw_bucket->passes.first; p != 0; p = p->next)
  {
    if(p->v.kind == R_PassKind_UI && !p->v.params_ui->preserve)
    {
      for(R_BatchGroup2DNode *g = p->v.params_ui->rects.first; g != 0; g = g->next)
      { instances += g->batches.byte_count/g->batches.bytes_per_inst; }
    }
  }
  U64 live_final_count = 0, live_image_terminals = 0, live_image_resources = 0, live_image_seen = 0;
  if(b->live)
  {
    String8 marker = str8_lit("FINAL UPDATE");
    for(U32 i = 0; i < b->count; i += 1)
    {
      RD_ViewState *view = rd_view_state_from_cfg(cfg_node_from_id(b->views[i]));
      UIShell_TerminalViewState *terminal = (UIShell_TerminalViewState *)view->user_data;
      if(terminal != 0)
      {
        live_image_terminals += terminal->image_cache.placement_count != 0;
        b->live_image_seen[i] |= terminal->image_cache.placement_count != 0;
        live_image_seen += b->live_image_seen[i];
        for(UIShell_TerminalImageResource *r = terminal->image_cache.first_resource; r; r = r->next)
        { live_image_resources += r->valid != 0; }
      }
      if(terminal == 0 || terminal->cell_cache.cols < marker.size) { continue; }
      UIShell_TerminalCellCache *cache = &terminal->cell_cache;
      for(U32 row = 0; row < cache->rows; row += 1)
      {
        B32 matches = 1;
        for(U32 col = 0; col < marker.size; col += 1)
        {
          cleat_cell *cell = &cache->cells[row*cache->cols+col];
          matches = matches && cell->grapheme_count != 0 && cell->graphemes[0] == marker.str[col];
        }
        if(matches) { live_final_count += 1; break; }
      }
    }
  }
  U64 pixels=0;
  for(RD_SurfaceCacheNode *n=ws->first_surface_cache_node; n; n=n->next)
  { pixels += (U64)n->size.x*n->size.y; }
  Vec2F32 dim=dim_2f32(wm_client_rect_from_window(ws->os));
  U32 phase=b->live ? (b->live_transitions ? b->live_phase : 0) : b->frame/b->phase_frames;
  fprintf(b->metrics, "%u,%u,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%.6f,%.0f,%.0f,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu\n",
    b->frame,phase,(unsigned long long)elapsed,(unsigned long long)b->shell_metrics.build_us,(unsigned long long)b->terminal_metrics.deferred,(unsigned long long)b->terminal_metrics.rebuilds,
    (unsigned long long)b->terminal_metrics.cells_built,(unsigned long long)b->terminal_metrics.terminal_visits,
    (unsigned long long)b->terminal_metrics.updates,(unsigned long long)b->shell_metrics.surface_allocations,
    (unsigned long long)pixels,ws->workspace_zoom_t,dim.x,dim.y,
    (unsigned long long)b->shell_metrics.surface_us,(unsigned long long)b->terminal_metrics.glyph_us,(unsigned long long)b->shell_metrics.window_us,
    (unsigned long long)b->shell_metrics.surface_admissions,(unsigned long long)b->shell_metrics.surface_deferred,(unsigned long long)instances,
    (unsigned long long)b->terminal_metrics.provider_starts,(unsigned long long)b->terminal_metrics.provider_start_us,
    (unsigned long long)b->terminal_metrics.background_starts,(unsigned long long)live_final_count,
    (unsigned long long)b->terminal_metrics.provider_resize_us,(unsigned long long)b->terminal_metrics.provider_update_us,
    (unsigned long long)b->terminal_metrics.empty_layout_resizes,(unsigned long long)b->shell_metrics.event_wait_us,
    (unsigned long long)b->terminal_metrics.background_updates,(unsigned long long)b->terminal_metrics.snapshot_deferred,
    (unsigned long long)live_image_terminals,(unsigned long long)live_image_resources,(unsigned long long)live_image_seen,
    (unsigned long long)b->shell_metrics.background_surface_redraws);
  fflush(b->metrics);
  if(b->interactive && (!b->live || !b->screenshots)) { b->frame += 1; return; }
  B32 phase_checkpoint = b->frame%b->phase_frames == b->phase_frames-1;
  if(b->live)
  {
    B32 drained = live_final_count == b->count && b->terminal_metrics.deferred == 0 &&
                  b->shell_metrics.surface_deferred == 0 && b->terminal_metrics.snapshot_deferred == 0 &&
                  b->terminal_metrics.updates == 0 && b->terminal_metrics.rebuilds == 0 && b->shell_metrics.surface_admissions == 0;
    if(drained) { phase = 6; }
    phase_checkpoint = !(b->live_checkpoint_mask & (1u << phase)) &&
                       (drained || now_time_us()-b->start_us >= (2*phase+1)*1000000ull);
    if(phase_checkpoint) { b->live_checkpoint_mask |= 1u << phase; }
  }
  if(b->screenshots && (phase_checkpoint || (!b->live && (b->frame == 31 || b->frame == 33 || b->frame == 37))))
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
    String8 path = phase_checkpoint ? push_str8f(scratch.arena,"%S/phase-%u.ppm",b->directory,phase) :
                                     push_str8f(scratch.arena,"%S/frame-%u.ppm",b->directory,b->frame);
    if(!write_data_list_to_file_path(path,output))
    { fprintf(stderr,"benchmark checkpoint write failed\n"); abort_self(2); }
    scratch_end(scratch);
  }
  b->frame++;
  if(!b->interactive && b->frame==8*b->phase_frames)
  {
    fclose(b->metrics); b->metrics=0;
    // All terminal data is in-process fixture storage; there are no child sessions.
    abort_self(0);
  }
}
