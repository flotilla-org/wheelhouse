// Native UI benchmark: real core, panel host, font metrics and precise events.
// Measures UI construction/layout, excluding GPU submission and frame pacing.
internal B32
uishell_sidebar_benchmark(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  UIShell_SidebarState *saved = ws->sidebar;
  UI_State *saved_ui = ui_state;
  B32 ok = 1;
  U64 sizes[] = {100, 1000};
  for(U64 size_index = 0; size_index < ArrayCount(sizes); size_index++)
  {
    UIShell_SidebarState state = {.initialized = 1, .restored = 1};
    char *error = 0;
    String8 config = str8_cstring((char *)uishell_sidebar_daily_config);
    state.core = andamento_create(config.str, config.size, &error);
    ok &= uishell_sidebar_result(&state, state.core != 0, error);
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
    ws->sidebar = &state;
    CFG_Node *old_host = cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT);
    cfg_node_unhook(rd_state->cfg, window, old_host);
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    CFG_Node *host = uishell_sidebar_dock_layout(&split);
    U64 sections = 0;
    for(CFG_Node *panel = host->first; panel != &cfg_nil_node; panel = panel->next) { sections++; }
    ok &= sections == 4;
    for(U64 merged = 0; merged < 2; merged++)
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
          UIShell_CmdNode *before = rd_state->cmds[0].last;
          UIShell_RegsScope(.window = window->id, .panel = source->id, .view = view->id,
                           .dst_panel = destination->id, .prev_tab = destination->last->id)
          { uishell_dispatch_tab_command(str8_lit("move_view")); }
          uishell_sidebar_docking_drain(before);
        }
      }
      for(U64 scrolling = 0; scrolling < 2; scrolling++)
      {
        UI_State *test_ui = ui_state_alloc();
        ui_select_state(test_ui);
        U64 elapsed = 0, analysis = 0, context = 0;
        for(U64 frame = 0; frame < 240; frame++)
        {
          UI_IconInfo icons = ws->ui->icon_info;
          UI_AnimationInfo animation = {0}; animation.scroll_animation_rate = .5f;
          UI_EventList events = {0}; UI_EventNode event = {0};
          if(scrolling && frame >= 40)
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
          if(frame >= 40)
          {
            elapsed += now_time_us()-start;
            analysis += uishell_sidebar_analysis_us;
            context += uishell_sidebar_context_us;
          }
          uishell_sidebar_benchmark_active = 0;
        }
        fprintf(stderr, "SIDEBAR_BENCH issues=%lu nodes=%lu panels=%s input=%s frames=200 frame_us=%.2f analysis_us=%.2f context_us=%.2f\n",
          sizes[size_index], (U64)andamento_snapshot_node_count(state.snapshot), merged ? "merged" : "four",
          scrolling ? "precise" : "idle", elapsed/200., analysis/200., context/200.);
        ui_select_state(saved_ui); ui_state_release(test_ui);
      }
    }
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, RD_DOCK_SIDEBAR_ROOT));
    if(old_host != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, old_host); }
    uishell_sidebar_release(&state);
  }
  ws->sidebar = saved;
  scratch_end(scratch);
  return ok;
}
