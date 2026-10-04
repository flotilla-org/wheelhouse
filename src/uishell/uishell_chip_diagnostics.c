// Exercise real row geometry, rather than relying only on measured fold plans.
internal B32
uishell_sidebar_chip_diagnostics(RD_WindowState *ws, UIShell_ControlledSplit *split)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *saved_sidebar = ws->sidebar, fixture = {0};
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  fixture.initialized = fixture.restored = 1;
  String8 config = str8_cstring((char *)uishell_sidebar_daily_config);
  fixture.core = andamento_create(config.str, config.size, 0);
  if(!fixture.core)
  {
    ui_state_release(test); scratch_end(scratch);
    fprintf(stderr, "Chip sidebar diagnostics: FAILED (fixture initialization)\n");
    return 0;
  }
  B32 ok = 1;
  String8 patches = str8_cstring((char *)uishell_sidebar_fixture_patches);
  for(U64 start = 0; start < patches.size;)
  {
    U64 end = start;
    while(end < patches.size && patches.str[end] != '\n') { end++; }
    ok = andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(str8(patches.str+start, end-start)), 0) && ok;
    start = end+1;
  }
  uishell_sidebar_refresh(&fixture);
  if(!ok || !fixture.snapshot)
  {
    uishell_sidebar_release(&fixture); ui_state_release(test); scratch_end(scratch);
    fprintf(stderr, "Chip sidebar diagnostics: FAILED (fixture patches)\n");
    return 0;
  }
  // Producer suggestion, local template override, then kind fallback. Labels
  // must never participate in icon resolution.
  B32 icon_font = 0;
  AndamentoNode default_role = {.entity_kind = uishell_sidebar_text(str8_lit("role")),
    .label = uishell_sidebar_text(str8_lit("terminal overview governor"))};
  ok = str8_match(uishell_sidebar_chip_icon(&fixture, default_role, &icon_font),
    rd_icon_kind_text_table[RD_IconKind_Threads], 0) && icon_font && ok;
  String8 override_field = str8_lit("value=\"\" prefix=\"chip-icon-override:\"");
  U64 at = str8_find_needle(config, 0, override_field, 0);
  String8 override_config = push_str8f(scratch.arena, "%Svalue=\"review\" prefix=\"chip-icon-override:\"%S",
    str8_prefix(config, at), str8_skip(config, at+override_field.size));
  ok = andamento_configure(fixture.core, uishell_sidebar_text(override_config), 0) && ok;
  uishell_sidebar_refresh(&fixture);
  for(U64 phase = 0; phase < 2; phase++)
  {
    for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(fixture.snapshot, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity_kind), str8_lit("role"), 0) &&
         str8_match(uishell_sidebar_string(node.layout), str8_lit("inline"), 0))
      { ok = str8_match(uishell_sidebar_chip_icon(&fixture, node, &icon_font),
          rd_icon_kind_text_table[phase ? RD_IconKind_Glasses : RD_IconKind_Gear], 0) && icon_font && ok; }
    }
    String8 patch = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"role\",\"id\":\"p/governor\"}},\"source_id\":\"fixture\",\"set\":{},\"unset\":[\"presentation.icon\"]}");
    ok = andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(patch), 0) && ok;
    uishell_sidebar_refresh(&fixture);
  }
  ok = andamento_configure(fixture.core, uishell_sidebar_text(config), 0) && ok;
  uishell_sidebar_refresh(&fixture);
  // Producer labels that resemble directives remain ordinary labels. The
  // first width also exercises a wide-glyph compact subject reference.
  String8 unicode_patch = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"convoy\",\"id\":\"build\"}},\"source_id\":\"fixture\",\"set\":{\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"chip-icon:列車の作業\"}},\"display.label.medium\":{\"value\":{\"type\":\"text\",\"value\":\"chip-status:列車\"}},\"display.label.short\":{\"value\":{\"type\":\"text\",\"value\":\"列車\"}}},\"unset\":[]}");
  ok = andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(unicode_patch), 0) && ok;
  unicode_patch = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"change_request\",\"id\":\"pr-281\"}},\"source_id\":\"fixture\",\"set\":{\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"漢!281\"}}},\"unset\":[]}");
  ok = andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(unicode_patch), 0) && ok;
  uishell_sidebar_refresh(&fixture);
  for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
  {
    AndamentoNode node = {0}; andamento_snapshot_node(fixture.snapshot, i, &node);
    if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("build"), 0))
    {
      ok = !uishell_sidebar_chip_fact(&fixture, node, str8_lit("chip-icon:")).size &&
        !uishell_sidebar_chip_field(&fixture, node, 0, str8_lit("chip-icon:列車の作業")) &&
        str8_match(uishell_sidebar_chip_status(&fixture, node), str8_lit("active"), 0) && ok;
    }
  }
  ws->sidebar = &fixture;
  ui_select_state(test);
  F32 widths[] = {240, 320, 600};
  for(U64 w = 0; w < ArrayCount(widths); w++)
  {
    F32 status_x = 0;
    for(U64 phase = 0; phase < 3; phase++)
    {
      // First latent, then pending, then no subjects. All three must leave
      // the trailing slot at precisely the same location for this width.
      if(phase == 1)
      {
        for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
        {
          AndamentoNode node = {0}; andamento_snapshot_node(fixture.snapshot, i, &node);
          if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("chip-v"), 0))
          { ok = andamento_dispatch(fixture.core, fixture.snapshot, node.activate, 0) && ok; break; }
        }
        uishell_sidebar_refresh(&fixture);
      }
      if(phase == 2)
      {
        char *ids[] = {"pr-281", "pr-1000", "no-forge", "issue-137"};
        for(U64 i = 0; i < ArrayCount(ids); i++)
        {
          String8 patch = push_str8f(scratch.arena,
            "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"%s\",\"id\":\"%s\"}},\"source_id\":\"fixture\",\"set\":{},\"unset\":[\"flotilla.subject_of\"]}",
            i == 3 ? "issue" : "change_request", ids[i]);
          ok = andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(patch), 0) && ok;
        }
        uishell_sidebar_refresh(&fixture);
      }
      AndamentoNode convoy = {0};
      for(U64 i = 0; i < andamento_snapshot_node_count(fixture.snapshot); i++)
      {
        AndamentoNode node = {0}; andamento_snapshot_node(fixture.snapshot, i, &node);
        if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("build"), 0)) { convoy = node; break; }
      }
      String8 convoy_key = push_str8_copy(scratch.arena, uishell_sidebar_string(convoy.key));
      UI_Box *row = &ui_nil_box;
      for(U64 frame = 0; frame < 6; frame++)
      {
        UI_IconInfo icons = ws->ui->icon_info;
        UI_AnimationInfo animation = {0}; UI_EventList events = {0};
        ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
        { uishell_sidebar_ui(r2f32p(0, 0, widths[w], 900), split); }
        ui_end_build();
        for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
        {
          if(!ui_box_is_nil(box->parent) && ui_key_match(box->key,
             ui_key_from_stringf(box->parent->key, "###entry_%S", convoy_key)))
          { row = box->parent; break; }
        }
      }
      if(ui_box_is_nil(row)) { ok = 0; fprintf(stderr, "FAIL chip diagnostics: missing convoy row\n"); continue; }
      UI_Box *status = row->last;
      if(phase == 0) { status_x = status->rect.x0; }
      if(abs_f32(status->rect.x0-status_x) > 0.01f || abs_f32(dim_2f32(status->rect).x-11.f*1.2f) > 1.f ||
         abs_f32(status->rect.x1-row->rect.x1) > 1.f)
      {
        fprintf(stderr, "FAIL chip status geometry: width %g phase %llu status [%g,%g], original %g row end %g\n",
          widths[w], (unsigned long long)phase, status->rect.x0, status->rect.x1, status_x, row->rect.x1);
        ok = 0;
      }
      if(phase < 2)
      {
        U64 subjects = 0;
        for(UI_Box *box = row; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, row).next)
        {
          String8 text = ui_box_display_string(box);
          subjects += str8_match(text, str8_lit("漢!281"), 0) || str8_match(text, str8_lit("!281"), 0) || str8_match(text, str8_lit("c!1000"), 0);
          if(text.size && text.str[0] == '+' && box->pref_size[Axis2_X].kind == UI_SizeKind_Pixels)
          {
            if(dim_2f32(box->rect).x+1.f < box->pref_size[Axis2_X].value)
            { fprintf(stderr, "FAIL chip overflow: width %g expected %g\n", dim_2f32(box->rect).x, box->pref_size[Axis2_X].value); ok = 0; }
          }
        }
        if(subjects != 2) { fprintf(stderr, "FAIL chip attention: width %g phase %llu subjects %llu\n", widths[w], (unsigned long long)phase, (unsigned long long)subjects); ok = 0; }
      }
    }
    // Restore subjects and cancel the synthetic pending request for next width.
    uishell_sidebar_release(&fixture);
    fixture.core = 0; fixture.snapshot = 0;
    fixture.core = andamento_create(config.str, config.size, 0);
    if(!fixture.core) { ok = 0; break; }
    for(U64 start = 0; start < patches.size;)
    {
      U64 end = start; while(end < patches.size && patches.str[end] != '\n') { end++; }
      ok = andamento_apply_patch_json(fixture.core, 0, uishell_sidebar_text(str8(patches.str+start, end-start)), 0) && ok;
      start = end+1;
    }
    uishell_sidebar_refresh(&fixture);
  }
  ws->sidebar = saved_sidebar;
  ui_select_state(saved_ui); ui_state_release(test);
  uishell_sidebar_release(&fixture);
  scratch_end(scratch);
  fprintf(stderr, "Chip sidebar diagnostics: %s (240/320/600px, latent/pending/removed, fixed status)\n", ok ? "passed" : "FAILED");
  return ok;
}
