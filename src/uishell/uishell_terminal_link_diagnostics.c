// Model checks; no browser is launched and no native gesture is simulated.
internal B32
uishell_terminal_link_diagnostics(void)
{
  B32 ok = 1;
#define LinkCheck(expr) do { if(!(expr)) { fprintf(stderr, "terminal link check failed: %s\n", #expr); ok = 0; } } while(0)
  String8 allowed[] = {str8_lit("https://example.com/actual?q=x#fragment"), str8_lit("HTTP://example.com"), str8_lit("https://xn--bcher-kva.example/%C3%BC")};
  String8 denied[] = {str8_lit("file:///tmp/local"), str8_lit("file://remote/tmp/local"), str8_lit("javascript:alert(1)"),
    str8_lit("mailto:a@example.com"), str8_lit("//example.com"), str8_lit("https://"), str8_lit("https:///tmp"),
    str8_lit("https://?query"), str8_lit("https://#fragment"), str8_lit("https://host/\0hidden"), str8_lit("https://host/\n"),
    str8_lit("https://host/\x1b"), str8_lit("https://host/a b"), str8_lit("https://host/\\path"), str8_lit("https://host/\xE2\x80\xAE")};
  for(U64 i = 0; i < ArrayCount(allowed); i++) { LinkCheck(uishell_terminal_link_allowed(allowed[i])); }
  for(U64 i = 0; i < ArrayCount(denied); i++) { LinkCheck(!uishell_terminal_link_allowed(denied[i])); }
#if OS_MAC
  WM_Modifiers modifier = WM_Modifier_Super;
#else
  WM_Modifiers modifier = WM_Modifier_Ctrl;
#endif
  LinkCheck(uishell_terminal_link_modifier(modifier));
  LinkCheck(!uishell_terminal_link_modifier(0));
  LinkCheck(!uishell_terminal_link_modifier(modifier|WM_Modifier_Shift));
  LinkCheck(!uishell_terminal_link_modifier(modifier|WM_Modifier_Alt));
  B32 held = 0;
  LinkCheck(!uishell_terminal_link_claim(&held, 1, 0, 0)); // plain press belongs to selection/app
  LinkCheck(!uishell_terminal_link_claim(&held, 0, 1, 1)); // adding modifier on release cannot activate
  LinkCheck(uishell_terminal_link_claim(&held, 1, 0, 1));
  LinkCheck(uishell_terminal_link_claim(&held, 0, 0, 0)); // drag after releasing modifier
  LinkCheck(uishell_terminal_link_claim(&held, 0, 1, 0)); // release never reaches child
  LinkCheck(!held);
  LinkCheck(!uishell_terminal_link_claim(&held, 0, 0, 0));

  LinkCheck(uishell_terminal_link_claim(&held, 1, 0, 1));
  LinkCheck(!uishell_terminal_link_claim(&held, 1, 0, 0)); // lost release / browser focus round trip
  Temp scratch = scratch_begin(0, 0);
  LinkCheck(str8_match(uishell_terminal_link_display(scratch.arena, str8_lit("a\0\n\\b")), str8_lit("a\\x00\\x0a\\x5cb"), 0));
  scratch_end(scratch);

  U32 label = 'X';
  U8 destination[] = "https://one.test";
  cleat_render_cell cells[3] = {0};
  cells[0].graphemes = &label; cells[0].grapheme_count = 1;
  cells[0].style.width = CLEAT_CELL_WIDTH_WIDE;
  cells[0].style.hyperlink_uri = (cleat_str){destination, sizeof(destination)-1};
  cells[1].style.width = CLEAT_CELL_WIDTH_SPACER_TAIL;
  cells[2].style.hyperlink_uri = (cleat_str){(U8 *)"https://two.test", 16};
  cleat_render_row row = {.row = 0, .col_count = 3, .cells = cells, .cell_count = 3};
  cleat_render_update_op op = {.kind = CLEAT_RENDER_OP_FULL_VISIBLE_REPLACE, .rows = &row, .row_desc_count = 1};
  cleat_render_update update = {.cols = 3, .rows = 2, .ops = &op, .op_count = 1};
  UIShell_TerminalCellCache cache = {0};
  uishell_terminal_cell_cache_apply_render_update(&cache, &update);
  destination[8] = 'z'; // provider storage can change after the pull
  LinkCheck(str8_match(uishell_terminal_link_at(&cache, 0, 0), str8_lit("https://one.test"), 0));
  LinkCheck(str8_match(uishell_terminal_link_at(&cache, 1, 0), str8_lit("https://one.test"), 0));
  LinkCheck(str8_match(uishell_terminal_link_at(&cache, 2, 0), str8_lit("https://two.test"), 0));
  LinkCheck(!uishell_terminal_link_at(&cache, 3, 0).size);
  op = (cleat_render_update_op){.kind = CLEAT_RENDER_OP_SCROLL_COPY, .src_row = 0, .dst_row = 1, .row_count = 1};
  uishell_terminal_cell_cache_apply_render_update(&cache, &update);
  LinkCheck(str8_match(uishell_terminal_link_at(&cache, 1, 1), str8_lit("https://one.test"), 0));
  MemoryZeroArray(cells);
  cells[0].style.hyperlink_uri.len = 123; // malformed null pointer must clear the old link safely
  op = (cleat_render_update_op){.kind = CLEAT_RENDER_OP_ROW_REPLACE, .rows = &row, .row_desc_count = 1};
  uishell_terminal_cell_cache_apply_render_update(&cache, &update);
  LinkCheck(!uishell_terminal_link_at(&cache, 0, 0).size); // stationary hover no longer has a target
  LinkCheck(str8_match(uishell_terminal_link_at(&cache, 1, 1), str8_lit("https://one.test"), 0));
  update.cols = 4; update.op_count = 0;
  uishell_terminal_cell_cache_apply_render_update(&cache, &update);
  LinkCheck(!uishell_terminal_link_at(&cache, 1, 1).size); // resize never keeps old coordinates
  arena_release(cache.arena);
#undef LinkCheck
  if(ok) { fprintf(stderr, "terminal link diagnostics passed\n"); }
  return ok;
}
