// Native UI benchmark: real core, panel host, font metrics and precise events.
// Measures UI construction/layout, excluding GPU submission and frame pacing.
// Real-core scenario: failed dispatch still invalidates, and a refreshed
// snapshot must never return text from its released predecessor.
internal B32
uishell_sidebar_benchmark_label_lifecycle(UIShell_SidebarState *state)
{
  Temp scratch = scratch_begin(0, 0);
  B32 saved_uncached = uishell_sidebar_benchmark_uncached, ok = 1;
  uishell_sidebar_benchmark_uncached = 0;
  U64 count = andamento_snapshot_node_count(state->snapshot);
  AndamentoNode *nodes = push_array(scratch.arena, AndamentoNode, count);
  for(U64 i = 0; i < count; i++) { andamento_snapshot_node(state->snapshot, i, &nodes[i]); }
  String8 identity = str8_lit("bench-project-0");
  String8 label = uishell_sidebar_context_label(state, nodes, count, identity);
  ok &= str8_match(label, str8_lit("Benchmark project 0"), 0);
  UIShell_SidebarLabel *labels = state->labels;
  uishell_sidebar_refresh(state);
  ok &= state->labels == labels; // Current revision retains the map.
  char *error = 0;
  B32 dispatched = uishell_sidebar_dispatch(state, ANDAMENTO_NONE, &error);
  ok &= !dispatched && !state->labels && !state->labels_snapshot;
  andamento_string_free(error);
  uishell_sidebar_context_label(state, nodes, count, identity);
  char *values[] = {"Replacement project", "Benchmark project 0"};
  for(U64 i = 0; i < ArrayCount(values); i++)
  {
    String8 patch = push_str8f(scratch.arena,
      "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"project\",\"id\":\"bench-project-0\"}},"
      "\"source_id\":\"benchmark\",\"set\":{\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"%s\"}}},\"unset\":[]}", values[i]);
    error = 0;
    B32 applied = andamento_apply_patch_json(state->core, 0, uishell_sidebar_text(patch), &error);
    ok &= uishell_sidebar_result(state, applied, error);
    uishell_sidebar_refresh(state);
    ok &= !state->labels && !state->labels_snapshot;
    count = andamento_snapshot_node_count(state->snapshot);
    nodes = push_array(scratch.arena, AndamentoNode, count);
    for(U64 n = 0; n < count; n++) { andamento_snapshot_node(state->snapshot, n, &nodes[n]); }
    ok &= str8_match(uishell_sidebar_context_label(state, nodes, count, identity), str8_cstring(values[i]), 0);
  }
  uishell_sidebar_labels_invalidate(state);
  uishell_sidebar_benchmark_uncached = saved_uncached;
  scratch_end(scratch);
  if(!ok) { fprintf(stderr, "FAIL sidebar label snapshot lifecycle\n"); }
  return ok;
}

internal B32
uishell_sidebar_benchmark(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  UIShell_SidebarState *saved = ws->sidebar;
  UI_State *saved_ui = ui_state;
  B32 ok = uishell_sidebar_labels_diagnostics();
  enum { warmup = 40, frames = 240, sample_interval = 40, layouts = 2, inputs = 2 };
  fprintf(stderr, "SIDEBAR_CONFIG frames=%u warmup=%u sample_interval=%u combinations=%u worker_cpus=%u\n", frames, warmup, sample_interval, layouts*inputs, get_system_info()->logical_processor_count);
  U64 sizes[] = {uishell_sidebar_benchmark_issues};
  for(U64 size_index = 0; size_index < ArrayCount(sizes); size_index++)
  {
    UIShell_SidebarState state = {.initialized = 1, .restored = 1};
    char *error = 0;
    String8 config = str8_cstring((char *)uishell_sidebar_daily_config);
    state.core = andamento_create(config.str, config.size, &error);
    ok &= uishell_sidebar_result(&state, state.core != 0, error);
    if(!state.core) { scratch_end(scratch); return 0; }
    String8 patches = str8_cstring((char *)uishell_sidebar_fixture_patches);
    for(U64 start = 0; start < patches.size;)
    {
      U64 end = start;
      while(end < patches.size && patches.str[end] != '\n') { end++; }
      error = 0;
      B32 applied = andamento_apply_patch_json(state.core, 0,
        uishell_sidebar_text(str8_substr(patches, r1u64(start, end))), &error);
      ok &= uishell_sidebar_result(&state, applied, error);
      start = end+1;
    }
    for(U64 i = 0; i < sizes[size_index]+10; i++)
    {
      B32 project = i < 10;
      U64 number = project ? i : i-10;
      String8 patch = push_str8f(scratch.arena,
        "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"%s\",\"id\":\"bench-%s-%I64u\"}},"
        "\"source_id\":\"benchmark\",\"set\":{"
        "\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"Benchmark %s %I64u\"}},"
        "\"flotilla.project\":{\"value\":{\"type\":\"text\",\"value\":\"bench-project-%I64u\"}},"
        "\"flotilla.issue.state\":{\"value\":{\"type\":\"text\",\"value\":\"open\"}},"
        "\"status.attention\":{\"value\":{\"type\":\"bool\",\"value\":%s}}},\"unset\":[]}",
        project ? "project" : "issue", project ? "project" : "issue", number,
        project ? "project" : "issue", number, project ? number : number%10,
        project ? "false" : "true");
      error = 0;
      B32 applied = andamento_apply_patch_json(state.core, 0, uishell_sidebar_text(patch), &error);
      ok &= uishell_sidebar_result(&state, applied, error);
    }
    uishell_sidebar_refresh(&state);
    ok &= uishell_sidebar_benchmark_label_lifecycle(&state);
    ws->sidebar = &state;
    CFG_Node *old_host = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
    cfg_node_unhook(rd_state->cfg, window, old_host);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    CFG_Node *host = uishell_sidebar_dock_layout(&split);
    U64 sections = 0;
    for(CFG_Node *panel = host->first; panel != &cfg_nil_node; panel = panel->next) { sections++; }
    ok &= sections == 4;
    for(U64 merged = 0; merged < layouts; merged++)
    {
      if(merged)
      {
        // Pair Projects/Sessions and Attention/Git, keeping both expensive
        // views visible while exercising the production merged-tab host.
        CFG_Node *destinations[2] = {host->first, host->first->next->next};
        for(U64 pair = 0; pair < 2; pair++)
        {
          CFG_Node *destination = destinations[pair], *source = destination->next;
          CFG_Node *view = cfg_node_child_from_string(source, str8_lit("sidebar_section"));
          CFG_Node *selected = cfg_node_child_from_string(destination, str8_lit("sidebar_section"));
          cfg_node_unhook(rd_state->cfg, source, view);
          cfg_node_insert_child(rd_state->cfg, destination, destination->last, view);
          cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("selected")));
          cfg_node_child_from_string_or_alloc(rd_state->cfg, selected, str8_lit("selected"));
          cfg_node_release(rd_state->cfg, source);
        }
      }
      for(U64 scrolling = 0; scrolling < inputs; scrolling++)
      {
        UI_State *test_ui = ui_state_alloc();
        ui_select_state(test_ui);
        U64 elapsed = 0, analysis = 0, context = 0, font_baseline = 0;
        for(U64 frame = 0; frame < frames; frame++)
        {
          // update() normally owns this boundary. This diagnostic builds many
          // frames inside one update, so retire the preceding frame's font runs.
          fnt_frame();
          UI_IconInfo icons = ws->ui->icon_info;
          UI_AnimationInfo animation = {0}; animation.scroll_animation_rate = .5f;
          UI_EventList events = {0}; UI_EventNode event = {0};
          if(scrolling && frame >= warmup)
          {
            UI_Key root = ui_key_from_stringf(ui_key_zero(), "andamento_section_%S", str8_lit("tree"));
            UI_Box *body = ui_box_from_key(ui_key_from_stringf(root, "section_body_%S", str8_lit("tree")));
            event.v = (UI_Event){.kind = UI_EventKind_Scroll, .pos = center_2f32(body->rect),
                                .delta_2f32 = {0, .25f}, .scroll_is_precise = 1};
            events.first = events.last = &event; events.count = 1;
          }
          uishell_sidebar_analysis_us = uishell_sidebar_context_us = 0;
          uishell_sidebar_benchmark_active = 1;
          U64 start = now_time_us();
          ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
          ui_state->mouse = events.count ? event.v.pos : v2f32(-100, -100);
          UIShell_RegsScope(.window = window->id)
          UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
          { uishell_control_surface_ui(r2f32p(0, 0, 400, 800), &split); }
          ui_end_build();
          U64 frame_elapsed = now_time_us()-start;
          // Repeated native frames at a fixed catalog must bound transient font
          // storage. Allow one MiB for scroll-dependent text after warmup.
          if(frame+1 == warmup) { font_baseline = arena_pos(fnt_state->frame_arena); }
          if(frame+1 == frames && arena_pos(fnt_state->frame_arena) > font_baseline+MB(1))
          { ok = 0; fprintf(stderr, "FAIL sidebar frame storage: font grew from %llu to %llu bytes\n", (unsigned long long)font_baseline, (unsigned long long)arena_pos(fnt_state->frame_arena)); }
#if OS_LINUX
          if((frame+1)%sample_interval == 0)
          {
            fprintf(stderr, "SIDEBAR_STORAGE frame=%lu draw=%lu font=%lu ui=%lu\n", frame+1, arena_pos(dr_thread_ctx->arena), arena_pos(fnt_state->frame_arena), arena_pos(ui_state->arena));
            FILE *status = fopen("/proc/self/status", "r");
            char line[256];
            while(status && fgets(line, sizeof(line), status))
            { if(strncmp(line, "VmRSS:", 6) == 0 || strncmp(line, "VmSize:", 7) == 0) { fprintf(stderr, "SIDEBAR_RSS issues=%lu frame=%lu %s", sizes[size_index], frame+1, line); } }
            if(status) { fclose(status); }
          }
#endif
          if(frame >= warmup)
          {
            elapsed += frame_elapsed;
            analysis += uishell_sidebar_analysis_us;
            context += uishell_sidebar_context_us;
          }
          uishell_sidebar_benchmark_active = 0;
        }
        UI_Key tree_key = ui_key_from_stringf(ui_key_zero(), "andamento_section_%S", str8_lit("tree"));
        UI_Key attention_key = ui_key_from_stringf(ui_key_zero(), "andamento_section_%S", str8_lit("attention"));
        UI_Box *tree_body = ui_box_from_key(ui_key_from_stringf(tree_key, "section_body_%S", str8_lit("tree")));
        UI_Box *attention_body = ui_box_from_key(ui_key_from_stringf(attention_key, "section_body_%S", str8_lit("attention")));
        // Precise events must move Projects while Attention keeps its own offset,
        // with both expensive sections visible in each panel arrangement.
        B32 layout_ok = !ui_box_is_nil(tree_body) && !ui_box_is_nil(attention_body) &&
          dim_2f32(tree_body->rect).y > 0 && dim_2f32(attention_body->rect).y > 0 &&
          (scrolling ? tree_body->view_off_target.y > 0 : tree_body->view_off_target.y == 0) &&
          attention_body->view_off_target.y == 0;
        ok &= layout_ok;
        if(!layout_ok) { fprintf(stderr, "FAIL sidebar benchmark visible sections/independent scroll\n"); }
        fprintf(stderr, "SIDEBAR_LAYOUT panels=%s input=%s tree_scroll=%g attention_scroll=%g tree_height=%g attention_height=%g\n",
          merged ? "merged" : "four", scrolling ? "precise" : "idle", tree_body->view_off_target.y,
          attention_body->view_off_target.y, dim_2f32(tree_body->rect).y, dim_2f32(attention_body->rect).y);
        fprintf(stderr, "SIDEBAR_BENCH issues=%llu nodes=%llu panels=%s input=%s frames=%u frame_us=%.2f analysis_us=%.2f context_us=%.2f\n",
          (unsigned long long)sizes[size_index], (unsigned long long)andamento_snapshot_node_count(state.snapshot), merged ? "merged" : "four",
          scrolling ? "precise" : "idle", frames-warmup, elapsed/(F64)(frames-warmup), analysis/(F64)(frames-warmup), context/(F64)(frames-warmup));
        ui_select_state(saved_ui); ui_state_release(test_ui);
      }
    }
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT));
    if(old_host != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, old_host); }
    // A real state owns snapshots, a core, labels and both hover-card arenas.
    // Teardown must clear every owned pointer so repeated release is safe.
    for(U64 i = 0; i < ArrayCount(state.cards); i++)
    { if(!state.cards[i].arena) { state.cards[i].arena = arena_alloc(); } }
    uishell_sidebar_release(&state);
    B32 released = !state.core && !state.snapshot && !state.labels_arena;
    for(U64 i = 0; i < ArrayCount(state.cards); i++) { released &= !state.cards[i].arena; }
    ok &= released;
    if(!released) { fprintf(stderr, "FAIL sidebar owned resources remain after release\n"); }
    else { uishell_sidebar_release(&state); }
  }
  ws->sidebar = saved;
  scratch_end(scratch);
  return ok;
}
