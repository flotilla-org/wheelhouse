// Render the shipped Git placement at two widths, using the same producer
// patch format as ingress. The renderer uses field classes, never entity kinds.
internal B32
uishell_sidebar_git_diagnostics(RD_WindowState *ws, UIShell_ControlledSplit *split)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *saved_sidebar = ws->sidebar, fixture = {0};
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  fixture.initialized = fixture.restored = 1;
  String8 config = str8_cstring((char *)uishell_sidebar_daily_config);
  fixture.core = andamento_create(config.str, config.size, 0);
  B32 ok = fixture.core != 0;
  String8 patches = str8_cstring((char *)uishell_sidebar_git_patches);
  for(U64 start = 0; start < patches.size;)
  {
    U64 end = start;
    while(end < patches.size && patches.str[end] != '\n') { end++; }
    ok = andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(str8(patches.str+start, end-start)), 0) && ok;
    start = end+1;
  }
  uishell_sidebar_refresh(&fixture);
  U64 repos = 0, worktrees = 0;
  for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
  {
    AndamentoNode node = {0}; andamento_snapshot_node(fixture.snapshot, i, &node);
    repos += str8_match(uishell_sidebar_string(node.entity_kind), str8_lit("repo"), 0);
    if(str8_match(uishell_sidebar_string(node.entity_kind), str8_lit("worktree"), 0))
    { worktrees++; ok = ok && node.state == ANDAMENTO_LATENT; }
  }
  fprintf(stderr, "Git fixture: repos=%lu worktrees=%lu diagnostics=%lu\n", repos, worktrees, andamento_snapshot_diagnostic_count(fixture.snapshot));
  ok = ok && repos == 2 && worktrees == 4 && andamento_snapshot_diagnostic_count(fixture.snapshot) == 0;
  ws->sidebar = &fixture;
  ui_select_state(test);
  for(U32 width_index = 0; width_index < 2; width_index++)
  {
    F32 width = width_index ? 800 : 220;
    for(U32 frame = 0; frame < 4; frame++)
    {
      UI_IconInfo icons = ws->ui->icon_info;
      UI_AnimationInfo animation = {0}; UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
      {
        uishell_sidebar_ui(r2f32p(0, 0, width, 900), split);
        for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
        {
          AndamentoNode node = {0}; andamento_snapshot_node(fixture.snapshot, i, &node);
          if(!str8_match(uishell_sidebar_string(node.entity_kind), str8_lit("worktree"), 0)) { continue; }
          String8 text = uishell_sidebar_fields(scratch.arena, fixture.snapshot, node, width-66);
          ok = ok && str8_find_needle(text, 0, str8_lit("dirty:"), 0) < text.size;
          ok = ok && (str8_find_needle(text, 0, str8_lit("main"), 0) < text.size ||
                      str8_find_needle(text, 0, str8_lit("feature"), 0) < text.size);
          if(width_index) { ok = ok && str8_find_needle(text, 0, str8_lit("↑"), 0) < text.size; }
          else { ok = ok && text.size < uishell_sidebar_fields(scratch.arena, fixture.snapshot, node, 800).size; }
        }
      }
      ui_end_build();
    }
  }
  ws->sidebar = saved_sidebar;
  ui_select_state(saved_ui); ui_state_release(test);
  uishell_sidebar_release(&fixture);
  scratch_end(scratch);
  fprintf(stderr, "Git sidebar diagnostics: %s (two repos, four worktrees, 220px and 800px)\n", ok ? "passed" : "FAILED");
  return ok;
}
