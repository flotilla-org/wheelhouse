// Native UI benchmark: real core, panel host, font metrics and precise events.
// Measures UI construction/layout, excluding GPU submission and frame pacing.
// Real-core scenario: a refused intent still invalidates, and a followed
// view must never return text from the model text it replaced.
internal B32
uishell_sidebar_benchmark_label_lifecycle(UIShell_SidebarState *state)
{
  Temp scratch = scratch_begin(0, 0);
  B32 saved_uncached = uishell_sidebar_benchmark_uncached, ok = 1;
  uishell_sidebar_benchmark_uncached = 0;
  U64 count = uishell_sidebar_node_count(state);
  UIShell_SidebarNode *nodes = push_array(scratch.arena, UIShell_SidebarNode, count);
  for(U64 i = 0; i < count; i++) { uishell_sidebar_node(state, i, &nodes[i]); }
  String8 identity = str8_lit("bench-project-0");
  String8 label = uishell_sidebar_context_label(state, nodes, count, identity);
  ok &= str8_match(label, str8_lit("Benchmark project 0"), 0);
  UIShell_SidebarLabel *labels = state->labels;
  uishell_sidebar_refresh(state);
  ok &= state->labels == labels; // Current revision retains the map.
  char *error = 0;
  // An intent invalidates the labels, even one the core refuses.
  UIShell_SidebarIntent unknown = {.some = 1, .kind = WHEELHOUSE_INTENT_ACTIVATE_PLACEMENT, .key = str8_lit("5:other1:x1:y1:z")};
  B32 dispatched = uishell_sidebar_intend(state, unknown, &error);
  ok &= !dispatched && !state->labels && !state->labels_view;
  wheelhouse_string_free(error);
  uishell_sidebar_context_label(state, nodes, count, identity);
  char *values[] = {"Replacement project", "Benchmark project 0"};
  for(U64 i = 0; i < ArrayCount(values); i++)
  {
    String8 patch = push_str8f(scratch.arena,
      "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"project\",\"id\":\"bench-project-0\"}},"
      "\"source_id\":\"benchmark\",\"set\":{\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"%s\"}}},\"unset\":[]}", values[i]);
    error = 0;
    B32 applied = wheelhouse_apply_patch_json(state->core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(patch), &error);
    ok &= uishell_sidebar_result(state, applied, error);
    uishell_sidebar_refresh(state);
    ok &= !state->labels && !state->labels_view;
    count = uishell_sidebar_node_count(state);
    nodes = push_array(scratch.arena, UIShell_SidebarNode, count);
    for(U64 n = 0; n < count; n++) { uishell_sidebar_node(state, n, &nodes[n]); }
    ok &= str8_match(uishell_sidebar_context_label(state, nodes, count, identity), str8_cstring(values[i]), 0);
  }
  uishell_sidebar_labels_invalidate(state);
  uishell_sidebar_benchmark_uncached = saved_uncached;
  scratch_end(scratch);
  if(!ok) { fprintf(stderr, "FAIL sidebar label view lifecycle\n"); }
  return ok;
}

internal B32
uishell_sidebar_benchmark_within(UI_Box *box, UI_Box *ancestor)
{
  for(UI_Box *b = box; !ui_box_is_nil(b); b = b->parent) { if(b == ancestor) { return 1; } }
  return 0;
}

internal UI_Box *
uishell_sidebar_benchmark_clickable(UI_Box *box)
{
  if(box->flags & UI_BoxFlag_Clickable) { return box; }
  for(UI_Box *child = box->first; !ui_box_is_nil(child); child = child->next)
  {
    UI_Box *found = uishell_sidebar_benchmark_clickable(child);
    if(!ui_box_is_nil(found)) { return found; }
  }
  return &ui_nil_box;
}

// One idle synthetic build, as the benchmark's frames are.
internal void
uishell_sidebar_benchmark_build(RD_WindowState *ws, CFG_Node *window, UIShell_ControlledSplit *split)
{
  Temp shell_frame = temp_begin(rd_frame_arena());
  dr_begin_frame(rd_font_from_slot(RD_FontSlot_Icons));
  fnt_frame();
  UI_IconInfo icons = ws->ui->icon_info;
  UI_AnimationInfo animation = {0}; animation.scroll_animation_rate = .5f;
  UI_EventList events = {0};
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  ui_state->mouse = v2f32(-100, -100);
  UIShell_RegsScope(.window = window->id)
  UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
  { uishell_control_surface_ui(r2f32p(0, 0, 400, 800), split); }
  ui_end_build();
  temp_end(shell_frame);
}

// Rows out of view are spacers (#500): at the top, middle and end of the
// Projects body, every row a full build shows is built where it was, and
// far fewer rows are built. A row given keyboard focus out of view scrolls
// in, and stays built when the view scrolls away from it.
internal B32
uishell_sidebar_benchmark_culling(RD_WindowState *ws, CFG_Node *window, UIShell_ControlledSplit *split)
{
  Temp scratch = scratch_begin(0, 0);
  B32 ok = 1;
  UI_State *saved_ui = ui_state;
  UI_State *test_ui = ui_state_alloc();
  ui_select_state(test_ui);
  UI_Key root_key = ui_key_from_stringf(ui_key_zero(), "wheelhouse_section_%S", str8_lit("tree"));
  UI_Key body_key = ui_key_from_stringf(root_key, "section_body_%S", str8_lit("tree"));
  // Its own arena: the render's scratch reuses the benchmark's.
  uishell_sidebar_row_log_arena = arena_alloc();
  uishell_sidebar_benchmark_build(ws, window, split);
  uishell_sidebar_benchmark_build(ws, window, split);
  UI_Box *body = ui_box_from_key(body_key);
  UIShell_SidebarSection *section = ws->sidebar->sections;
  while(section && !str8_match(section->key, str8_lit("tree"), 0)) { section = section->next; }
  F32 range = section ? Max(0.f, section->content_height-dim_2f32(body->rect).y) : 0.f;
  if(ui_box_is_nil(body) || range < 1000.f) { ok = 0; fprintf(stderr, "FAIL sidebar culling: Projects doesn't scroll (%g)\n", range); }
  F32 offsets[] = {0.f, floor_f32(range*0.5f), range};
  for(U64 o = 0; ok && o < ArrayCount(offsets); o++)
  {
    U64 built[2] = {0};
    U64 *keys = 0; Rng2F32 *rects = 0; U64 shown = 0;
    for(U64 cull = 0; cull < 2; cull++)
    {
      uishell_sidebar_cull_rows = (B32)cull;
      body = ui_box_from_key(body_key);
      body->view_off.y = body->view_off_target.y = offsets[o];
      uishell_sidebar_benchmark_build(ws, window, split);
      body = ui_box_from_key(body_key);
      body->view_off.y = body->view_off_target.y = offsets[o];
      uishell_sidebar_row_log = 0;
      uishell_sidebar_rows_built = 0;
      uishell_sidebar_benchmark_build(ws, window, split);
      built[cull] = uishell_sidebar_rows_built;
      body = ui_box_from_key(body_key);
      U64 count = 0;
      for(UIShell_SidebarBuiltRow *r = uishell_sidebar_row_log; r; r = r->next) { count++; }
      if(!cull)
      {
        keys = push_array(scratch.arena, U64, count); rects = push_array(scratch.arena, Rng2F32, count);
        for(UIShell_SidebarBuiltRow *r = uishell_sidebar_row_log; r; r = r->next)
        {
          if(!uishell_sidebar_benchmark_within(r->slot, body) || r->slot->rect.y1 <= body->rect.y0 || r->slot->rect.y0 >= body->rect.y1) { continue; }
          keys[shown] = r->key; rects[shown] = r->slot->rect; shown++;
        }
        continue;
      }
      for(U64 k = 0; k < shown; k++)
      {
        UIShell_SidebarBuiltRow *r = uishell_sidebar_row_log;
        while(r && r->key != keys[k]) { r = r->next; }
        if(!r || abs_f32(r->slot->rect.y0-rects[k].y0) > 0.5f || abs_f32(r->slot->rect.y1-rects[k].y1) > 0.5f ||
           abs_f32(r->slot->rect.x0-rects[k].x0) > 0.5f || abs_f32(r->slot->rect.x1-rects[k].x1) > 0.5f)
        {
          ok = 0;
          fprintf(stderr, "FAIL sidebar culling at %g: a shown row (%g..%g) is %s\n", offsets[o], rects[k].y0, rects[k].y1,
            r ? "built elsewhere" : "not built");
          break;
        }
      }
    }
    if(!shown || built[1]*4 > built[0])
    { ok = 0; fprintf(stderr, "FAIL sidebar culling at %g: %llu rows shown, %llu built of %llu\n", offsets[o], (unsigned long long)shown, (unsigned long long)built[1], (unsigned long long)built[0]); }
    fprintf(stderr, "SIDEBAR_CULL offset=%g shown=%llu built=%llu full=%llu\n", offsets[o], (unsigned long long)shown, (unsigned long long)built[1], (unsigned long long)built[0]);
  }
  // Focus a row built below the view, in the margin: the view scrolls to it,
  // and the row after it is built, for the next arrow key to reach.
  uishell_sidebar_cull_rows = 1;
  body = ui_box_from_key(body_key);
  body->view_off.y = body->view_off_target.y = 0;
  uishell_sidebar_row_log = 0;
  uishell_sidebar_benchmark_build(ws, window, split);
  body = ui_box_from_key(body_key);
  // The nearest: project headers are built wherever they are.
  UIShell_SidebarBuiltRow *below = 0;
  for(UIShell_SidebarBuiltRow *r = uishell_sidebar_row_log; r; r = r->next)
  {
    if(!uishell_sidebar_benchmark_within(r->slot, body) || r->slot->rect.y0 < body->rect.y1) { continue; }
    if(!below || r->slot->rect.y0 < below->slot->rect.y0) { below = r; }
  }
  UI_Box *root = ui_box_from_key(root_key);
  UI_Box *target = below ? uishell_sidebar_benchmark_clickable(below->slot) : &ui_nil_box;
  if(ok && (ui_box_is_nil(target) || ui_box_is_nil(root))) { ok = 0; fprintf(stderr, "FAIL sidebar culling: no row built below the view\n"); }
  if(ok)
  {
    U64 focused = below->key;
    UI_Key focus_key = target->key;
    root->default_nav_focus_hot_key = root->default_nav_focus_next_hot_key = focus_key;
    uishell_sidebar_benchmark_build(ws, window, split);
    body = ui_box_from_key(body_key);
    if(body->view_off_target.y <= 0) { ok = 0; fprintf(stderr, "FAIL sidebar culling: a row focused below the view didn't scroll in\n"); }
    // Where it scrolled to, settled.
    F32 scrolled = body->view_off_target.y;
    body->view_off.y = scrolled;
    uishell_sidebar_row_log = 0;
    uishell_sidebar_benchmark_build(ws, window, split);
    UIShell_SidebarBuiltRow *row = uishell_sidebar_row_log;
    while(row && row->key != focused) { row = row->next; }
    B32 next_built = 0;
    for(UIShell_SidebarBuiltRow *r = uishell_sidebar_row_log; row && r; r = r->next)
    { next_built |= abs_f32(r->slot->rect.y0-row->slot->rect.y1) < 0.5f; }
    if(!next_built) { ok = 0; fprintf(stderr, "FAIL sidebar culling: the row after the focused one isn't built\n"); }
    // Scrolled away to the end, the focused row stays built, and the view
    // stays where it was put.
    body->view_off.y = body->view_off_target.y = range;
    uishell_sidebar_benchmark_build(ws, window, split);
    body = ui_box_from_key(body_key);
    body->view_off.y = body->view_off_target.y = range;
    uishell_sidebar_row_log = 0;
    uishell_sidebar_benchmark_build(ws, window, split);
    body = ui_box_from_key(body_key);
    UIShell_SidebarBuiltRow *r = uishell_sidebar_row_log;
    while(r && r->key != focused) { r = r->next; }
    if(!r) { ok = 0; fprintf(stderr, "FAIL sidebar culling: the focused row wasn't built out of view\n"); }
    root = ui_box_from_key(root_key);
    if(!ui_key_match(root->default_nav_focus_hot_key, focus_key)) { ok = 0; fprintf(stderr, "FAIL sidebar culling: the row lost focus\n"); }
    if(body->view_off_target.y < range-1.f) { ok = 0; fprintf(stderr, "FAIL sidebar culling: the view went back to the focused row (%g of %g)\n", body->view_off_target.y, range); }
  }
  arena_release(uishell_sidebar_row_log_arena);
  uishell_sidebar_row_log_arena = 0;
  uishell_sidebar_row_log = 0;
  uishell_sidebar_cull_rows = 1;
  ui_select_state(saved_ui);
  ui_state_release(test_ui);
  scratch_end(scratch);
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
  enum { warmup = 1000, frames = 5000, sample_interval = 200, layouts = 2, inputs = 2 };
  fprintf(stderr, "SIDEBAR_CONFIG frames=%u warmup=%u sample_interval=%u combinations=%u worker_cpus=%u\n", frames, warmup, sample_interval, layouts*inputs, get_system_info()->logical_processor_count);
  U64 sizes[] = {uishell_sidebar_benchmark_issues};
  for(U64 size_index = 0; size_index < ArrayCount(sizes); size_index++)
  {
    UIShell_SidebarState state = {.initialized = 1};
    char *error = 0;
    String8 config = str8_cstring((char *)uishell_sidebar_daily_config);
    state.core = wheelhouse_create(config.str, config.size, &error);
    ok &= uishell_sidebar_result(&state, state.core != 0, error);
    if(!state.core) { scratch_end(scratch); return 0; }
    String8 patches = str8_cstring((char *)uishell_sidebar_fixture_patches);
    for(U64 start = 0; start < patches.size;)
    {
      U64 end = start;
      while(end < patches.size && patches.str[end] != '\n') { end++; }
      error = 0;
      B32 applied = wheelhouse_apply_patch_json(state.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider),
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
      B32 applied = wheelhouse_apply_patch_json(state.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(patch), &error);
      ok &= uishell_sidebar_result(&state, applied, error);
    }
    uishell_sidebar_refresh(&state);
    ok &= uishell_sidebar_benchmark_label_lifecycle(&state);
    ws->sidebar = &state;
    // The core keeps the sidebar's arrangement once this marks it. Isolate
    // it with the layout and its Views so the fixture's follows the core's.
    RD_PanelOwner sidebar_owner = rd_panel_owner_from_host(RD_PanelOwnerKind_Sidebar, window);
    RD_Arrangement *old_layout = rd_arrangement_from_panel_owner(scratch.arena, sidebar_owner);
    CFG_Node *old_views = cfg_node_child_from_string(window, str8_lit("window_views"));
    CFG_Node *old_inventory = cfg_node_child_from_string(window, str8_lit("sidebar_generation"));
    cfg_node_unhook(rd_state->cfg, window, old_inventory);
    cfg_node_unhook(rd_state->cfg, window, old_views);
    {
      RD_Arrangement *empty = rd_arrangement_alloc(scratch.arena, sidebar_owner);
      rd_arrangement_set(empty);
    }
    UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
    state.window = window->id;
    uishell_sidebar_dock_layout(&split);
    // The shipped KDL may add regions (such as local sections); the benchmark
    // needs the four it measures.
    String8 measured[] = {str8_lit("tree"), str8_lit("sessions"), str8_lit("attention"), str8_lit("git")};
    for(U64 i = 0; i < ArrayCount(measured); i++)
    {
      if(uishell_sidebar_region_view(window, measured[i]) == &cfg_nil_node)
      { ok = 0; fprintf(stderr, "FAIL sidebar benchmark: no %.*s section\n", str8_varg(measured[i])); }
    }
    ok &= uishell_sidebar_benchmark_culling(ws, window, &split);
    for(U64 merged = 0; merged < layouts; merged++)
    {
      if(merged)
      {
        // Pair Projects/Sessions and Attention/Git, keeping both expensive
        // views visible while exercising the production merged-tab host.
        String8 destinations[] = {str8_lit("tree"), str8_lit("attention")};
        String8 sources[] = {str8_lit("sessions"), str8_lit("git")};
        for(U64 pair = 0; pair < ArrayCount(destinations); pair++)
        {
          RD_ViewAddress selected = rd_view_address_from_cfg(uishell_sidebar_region_view(window, destinations[pair]));
          RD_ViewAddress view = rd_view_address_from_cfg(uishell_sidebar_region_view(window, sources[pair]));
          RD_Arrangement *sidebar = rd_arrangement_from_panel_owner(scratch.arena, sidebar_owner);
          RD_ArrangementPanel *destination = rd_arrangement_panel_from_view(sidebar, selected);
          RD_ArrangementPanel *source = rd_arrangement_panel_from_view(sidebar, view);
          rd_arrangement_select(sidebar, destination->id, selected);
          rd_arrangement_insert_tab(sidebar, view, destination->id, destination->last_tab->view, 0);
          if(source->first == &rd_nil_arrangement_panel && source->tab_count == 0) { rd_arrangement_remove(sidebar, source->id); }
          rd_arrangement_set(sidebar);
        }
      }
      for(U64 scrolling = 0; scrolling < inputs; scrolling++)
      {
        UI_State *test_ui = ui_state_alloc();
        ui_select_state(test_ui);
        U64 elapsed = 0, analysis = 0, context = 0, font_baseline = 0;
        for(U64 frame = 0; frame < frames; frame++)
        {
          // update() normally retires shell registers and draw/font runs.
          // This diagnostic builds many frames inside one update: restore the
          // enclosing frame's arena position after every synthetic build.
          Temp shell_frame = temp_begin(rd_frame_arena());
          dr_begin_frame(rd_font_from_slot(RD_FontSlot_Icons));
          fnt_frame();
          UI_IconInfo icons = ws->ui->icon_info;
          UI_AnimationInfo animation = {0}; animation.scroll_animation_rate = .5f;
          UI_EventList events = {0}; UI_EventNode event = {0};
          if(scrolling && frame >= warmup)
          {
            UI_Key root = ui_key_from_stringf(ui_key_zero(), "wheelhouse_section_%S", str8_lit("tree"));
            UI_Box *body = ui_box_from_key(ui_key_from_stringf(root, "section_body_%S", str8_lit("tree")));
            event.v = (UI_Event){.kind = UI_EventKind_Scroll, .pos = center_2f32(body->rect),
                                .delta_2f32 = v2f32(0, .25f), .scroll_is_precise = 1};
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
          temp_end(shell_frame);
          // Repeated native frames at a fixed catalog must bound transient font
          // storage. Allow one MiB for scroll-dependent text after warmup.
          if(frame+1 == warmup) { font_baseline = arena_pos(fnt_state->frame_arena); }
          if(frame+1 == frames && arena_pos(fnt_state->frame_arena) > font_baseline+MB(1))
          { ok = 0; fprintf(stderr, "FAIL sidebar frame storage: font grew from %llu to %llu bytes\n", (unsigned long long)font_baseline, (unsigned long long)arena_pos(fnt_state->frame_arena)); }
#if OS_LINUX
          if((frame+1)%sample_interval == 0)
          {
            fprintf(stderr, "SIDEBAR_STORAGE frame=%llu draw=%llu font=%llu ui=%llu shell=%llu\n", (unsigned long long)(frame+1), (unsigned long long)arena_pos(dr_thread_ctx->arena), (unsigned long long)arena_pos(fnt_state->frame_arena), (unsigned long long)arena_pos(ui_state->arena), (unsigned long long)arena_pos(rd_frame_arena()));
            FILE *status = fopen("/proc/self/status", "r");
            char line[256];
            while(status && fgets(line, sizeof(line), status))
            { if(strncmp(line, "VmRSS:", 6) == 0 || strncmp(line, "VmSize:", 7) == 0) { fprintf(stderr, "SIDEBAR_RSS issues=%llu frame=%llu %s", (unsigned long long)sizes[size_index], (unsigned long long)(frame+1), line); } }
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
        UI_Key tree_key = ui_key_from_stringf(ui_key_zero(), "wheelhouse_section_%S", str8_lit("tree"));
        UI_Key attention_key = ui_key_from_stringf(ui_key_zero(), "wheelhouse_section_%S", str8_lit("attention"));
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
          (unsigned long long)sizes[size_index], (unsigned long long)uishell_sidebar_node_count(&state), merged ? "merged" : "four",
          scrolling ? "precise" : "idle", frames-warmup, elapsed/(F64)(frames-warmup), analysis/(F64)(frames-warmup), context/(F64)(frames-warmup));
        ui_select_state(saved_ui); ui_state_release(test_ui);
      }
    }
    {
      RD_Arrangement *fixture = rd_arrangement_from_panel_owner(scratch.arena, sidebar_owner);
      rd_arrangement_discard(fixture);
      rd_arrangement_set(fixture);
    }
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("window_views")));
    if(old_views != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, old_views); }
    rd_arrangement_set(old_layout);
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(window, str8_lit("sidebar_generation")));
    if(old_inventory != &cfg_nil_node) { cfg_node_insert_child(rd_state->cfg, window, window->last, old_inventory); }
    // A real state owns core/model, labels, cards and records resources.
    // Teardown must clear every owned pointer so repeated release is safe.
    for(U64 i = 0; i < ArrayCount(state.cards); i++)
    { if(!state.cards[i].arena) { state.cards[i].arena = arena_alloc(); } }
    if(!state.records_arena) { state.records_arena = arena_alloc(); }
    // Releasing it as a window's would release the window's local sections.
    state.window = 0;
    uishell_sidebar_dashboard(&state);
    ok &= state.dashboard != 0;
    uishell_sidebar_release(&state);
    B32 released = !state.core && !state.model.arena && !state.subscribed && !state.labels_arena &&
      !state.records_arena && !state.records && !state.dashboard &&
      !state.titles_arena && !state.titles_view && !state.titles && !state.title_count && !state.sidebar_notes_arena;
    for(U64 i = 0; i < ArrayCount(state.cards); i++) { released &= !state.cards[i].arena; }
    ok &= released;
    if(!released) { fprintf(stderr, "FAIL sidebar owned resources remain after release\n"); }
    else { uishell_sidebar_release(&state); }
  }
  ws->sidebar = saved;
  scratch_end(scratch);
  return ok;
}
