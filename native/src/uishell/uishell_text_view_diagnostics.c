// Text view checks (#622), headless: the per-line token lookup against the
// linear scan it replaced, the scope tree on unbalanced brackets, and the
// timing test: a 10k-line file scrolled to its end a wheel notch a frame,
// its frame costs printed as TEXT_VIEW_BENCH.

#define TextCheck(condition, label) do { if(!(condition)) { fprintf(stderr, "FAIL text view: %s (line %d)\n", label, __LINE__); failures++; } \
                                         else { fprintf(stderr, "PASS text view: %s\n", label); } } while(0)

// Lexed in the artifact cache's threads: waits for it, as a View's frames do.
internal TXT_TextInfo
uishell_text_diag_info(Access *access, C_Key key, TXT_LangKind lang, U64 lines)
{
  TXT_TextInfo info = {0};
  for(U32 i = 0; i < 2000; i += 1)
  {
    U128 hash = {0};
    info = txt_text_info_from_key_lang(access, key, lang, &hash);
    if(info.lines_count >= lines) { break; }
    sleep_ms(5);
  }
  return info;
}

internal String8
uishell_text_diag_rust(Arena *arena, U64 lines)
{
  String8List out = {0};
  for(U64 i = 0; out.node_count < lines; i += 1)
  {
    switch(i % 9)
    {
      case 0: str8_list_pushf(arena, &out, "/* berth %llu: a block comment\n", (unsigned long long)i); break;
      case 1: str8_list_pushf(arena, &out, "   that spans two lines */\n"); break;
      case 2: str8_list_pushf(arena, &out, "fn berth_%llu(tide: u32, name: &str) -> Option<u64> {\n", (unsigned long long)i); break;
      case 3: str8_list_pushf(arena, &out, "    let moored = \"hawser {}\".replace(\"{}\", name); // a line comment\n"); break;
      case 4: str8_list_pushf(arena, &out, "\n"); break;
      case 5: str8_list_pushf(arena, &out, "    if tide > %llu { return Some(tide as u64 * 0x%llx); }\n", (unsigned long long)i, (unsigned long long)i); break;
      case 6: str8_list_pushf(arena, &out, "\t\tlet capstan = [1, 2, 3].iter().map(|x| x * 2).collect::<Vec<_>>();\r\n"); break;
      case 7: str8_list_pushf(arena, &out, "    None\n"); break;
      case 8: str8_list_pushf(arena, &out, "}\n"); break;
    }
  }
  return str8_list_join(arena, &out, 0);
}

internal B32
uishell_text_view_diagnostics(RD_WindowState *ws)
{
  Arena *arena = arena_alloc();
  U32 failures = 0;
  Access *access = access_open();
  C_Root root = c_root_alloc();

  //- The lookup gives every line the tokens the linear scan gives it.
  {
    struct { String8 text; TXT_LangKind lang; } cases[] =
    {
      {uishell_text_diag_rust(arena, 400), TXT_LangKind_Rust},
      {str8_lit("int a; /* one\ntwo\n\nthree */ int b;\n\n\"s\\\"\"\n// end"), TXT_LangKind_C},
      {str8_lit("\n\n\nx\n"), TXT_LangKind_C},
      {str8_lit("const tide = 3;\r\nconst berth = \"a\r\nb\";\r\n"), TXT_LangKind_Zig},
    };
    U64 lines = 0, agree = 0;
    for EachElement(c, cases)
    {
      C_Key key = c_key_make(root, c_id_make(c, 1));
      c_submit_data(key, 0, cases[c].text);
      TXT_TextInfo info = uishell_text_diag_info(access, key, cases[c].lang, 1);
      for(S64 line = 0; line <= (S64)info.lines_count + 1; line += 1)
      {
        TXT_TokenArray fast = txt_token_array_from_info_line_num(&info, line);
        TXT_TokenArray slow = txt_token_array_from_info_line_num__linear_scan(&info, line);
        lines += 1;
        agree += fast.v == slow.v && fast.count == slow.count;
      }
    }
    fprintf(stderr, "TEXT_VIEW_LOOKUP lines=%llu agree=%llu\n", (unsigned long long)lines, (unsigned long long)agree);
    TextCheck(lines > 400 && agree == lines, "the binary search finds each line's tokens, as the linear scan does");
  }

  //- Brackets that don't balance: only what has a scope gets a point.
  {
    C_Key key = c_key_make(root, c_id_make(100, 1));
    c_submit_data(key, 0, str8_lit("} ) ] fn tide() { [ ( ) }\n"));
    TXT_TextInfo info = uishell_text_diag_info(access, key, TXT_LangKind_Rust, 1);
    B32 in_range = 1;
    for(U64 i = 0; i < info.scope_pts.count; i += 1) { in_range &= info.scope_pts.v[i].scope_idx < info.scope_nodes.count; }
    fprintf(stderr, "TEXT_VIEW_SCOPES points=%llu nodes=%llu\n", (unsigned long long)info.scope_pts.count, (unsigned long long)info.scope_nodes.count);
    TextCheck(info.scope_nodes.count == 4 && info.scope_pts.count == 7 && in_range, "stray closers leave no scope points");
    TXT_ScopeNode *unclosed = txt_scope_node_from_info_num(&info, 2);
    TextCheck(unclosed->token_idx_range.max == info.tokens.count-1, "a scope never closed runs to the last token");
  }

  //- Timing: a 10k-line file, scrolled to its end.
  {
    String8 dir = str8_chop_last_slash(uishell_launch_user_path);
    if(dir.size == 0) { dir = get_current_path(arena); }
    String8 path = push_str8f(arena, "%S/text-view-large.rs", dir);
    U64 line_count = 10000;
    write_data_to_file_path(path, uishell_text_diag_rust(arena, line_count));
    CFG_Node *window = cfg_node_from_id(ws->cfg_id);
    CFG_Node *host = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
    RD_PanelAddress panel = rd_fixture_leaf(rd_fixture_owner(host));
    CFG_Node *view = rd_fixture_view(panel, str8_lit("text"), rd_eval_string_from_file_path(arena, path), 1);
    RD_ViewAddress address = rd_view_address_from_cfg(view);
    UI_State *saved_ui = ui_state, *saved_window_ui = ws->ui, *test_ui = ui_state_alloc();
    ws->ui = test_ui;
    ui_select_state(test_ui);
    Rng2F32 rect = r2f32p(0, 0, 1200, 900);
    Vec2F32 mouse = center_2f32(rect);
    U64 cap = 20000, frames = 0, now_us = 1000000;
    U64 *frame_us = push_array(arena, U64, cap);
    S64 top = 0, last_top = -1;
    U32 still = 0;
    B32 loaded = 0;
    for(U64 i = 0; i < cap + 2000 && frames < cap; i += 1)
    {
      // A wheel notch a frame, once it has loaded.
      UI_EventNode node = {.v = (UI_Event){.kind = UI_EventKind_Scroll, .pos = mouse, .delta_2f32 = v2f32(0, 1), .timestamp_us = now_us += 16667}};
      UI_EventList events = {&node, &node, 1}, none = {0};
      UI_AnimationInfo animation = {0};
      U64 begin = now_time_us();
      ui_begin_build(ws->os, loaded ? &events : &none, &ws->ui->icon_info, ws->theme, &animation, 1.f/60, 1.f/60);
      ui_state->mouse = mouse;
      UIShell_RegsScope(.window = ws->cfg_id, .panel = panel, .tab = address, .view = address)
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      {
        wh_view_ui_direct(wh_renderer_from_name(str8_lit("text")), address, rect);
      }
      ui_end_build();
      U64 took = now_time_us() - begin;
      MemoryZeroStruct(&rd_state->cmds[0]);
      top = rd_view_state_from_view(address, 0)->scroll_pos.y.idx;
      if(!loaded)
      {
        // Loaded once the bar counts the file's lines: one more than it
        // ends, after its last newline.
        String8 count = push_str8f(arena, "%llu lines", (unsigned long long)line_count+1);
        for(UI_Box *b = ui_state->root; !ui_box_is_nil(b) && !loaded; b = ui_box_rec_df_pre(b, ui_state->root).next)
        {
          loaded = str8_match(ui_box_display_string(b), count, 0);
        }
        if(!loaded) { sleep_ms(5); }
        continue;
      }
      frame_us[frames++] = took;
      still = top == last_top ? still + 1 : 0;
      last_top = top;
      if(still >= 30) { break; }
    }
    // The first and last 60 frames, before sorting: the file's top and end.
    U64 total_us = 0, start_us = 0, end_us = 0, edge_frames = Min(frames, 60);
    for(U64 i = 0; i < frames; i += 1) { total_us += frame_us[i]; }
    for(U64 i = 0; i < edge_frames; i += 1) { start_us += frame_us[i]; end_us += frame_us[frames-1-i]; }
    U64 start_mean = edge_frames ? start_us/edge_frames : 0, end_mean = edge_frames ? end_us/edge_frames : 0;
    qsort(frame_us, frames, sizeof(U64), uishell_rt_diag_u64_compare);
    U64 p50 = frames ? frame_us[frames/2] : 0, p95 = frames ? frame_us[frames*95/100] : 0, max = frames ? frame_us[frames-1] : 0;
    printf("TEXT_VIEW_BENCH build=%s lines=%llu frames=%llu top=%lld frame_us_p50=%llu frame_us_p95=%llu frame_us_max=%llu frame_us_at_start=%llu frame_us_at_end=%llu frame_us_total=%llu\n",
           BUILD_DEBUG ? "debug" : "release", (unsigned long long)line_count, (unsigned long long)frames, (long long)top,
           (unsigned long long)p50, (unsigned long long)p95, (unsigned long long)max, (unsigned long long)start_mean, (unsigned long long)end_mean, (unsigned long long)total_us);
    TextCheck(loaded, "the 10k-line file opens in the text view");
    TextCheck(top > (S64)line_count - 100, "the wheel scrolls it to its end");
    ws->ui = saved_window_ui;
    ui_select_state(saved_ui);
  }

  c_root_release(root);
  access_close(access);
  arena_release(arena);
  fprintf(stderr, "text view diagnostics %s (%u failures)\n", failures == 0 ? "passed" : "failed", failures);
  return failures == 0;
}
#undef TextCheck
