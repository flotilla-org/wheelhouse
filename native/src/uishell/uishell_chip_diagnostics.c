// Exercise real row geometry, rather than relying only on measured fold plans.
internal B32
uishell_sidebar_chip_diagnostics(RD_WindowState *ws, UIShell_ControlledSplit *split)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *saved_sidebar = ws->sidebar, fixture = {0};
  UI_State *saved_ui = ui_state, *test = ui_state_alloc();
  fixture.initialized = 1;
  String8 config = str8_cstring((char *)uishell_sidebar_daily_config);
  fixture.core = wheelhouse_create(config.str, config.size, 0);
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
    ok = wheelhouse_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(str8(patches.str+start, end-start)), 0) && ok;
    start = end+1;
  }
  uishell_sidebar_refresh(&fixture);
  if(!ok || !uishell_sidebar_viewing(&fixture))
  {
    uishell_sidebar_release(&fixture); ui_state_release(test); scratch_end(scratch);
    fprintf(stderr, "Chip sidebar diagnostics: FAILED (fixture patches)\n");
    return 0;
  }
  // Producer suggestion, local template override, then kind fallback. Labels
  // must never participate in icon resolution.
  B32 icon_font = 0;
  UIShell_SidebarNode default_role = {.entity.kind = uishell_sidebar_text(str8_lit("role")),
    .label = uishell_sidebar_text(str8_lit("terminal overview governor"))};
  ok = str8_match(uishell_sidebar_chip_icon(&fixture, default_role, &icon_font),
    rd_icon_kind_text_table[RD_IconKind_Threads], 0) && icon_font && ok;
  String8 override_field = str8_lit("value=\"\" presents=\"icon\"");
  U64 at = str8_find_needle(config, 0, override_field, 0);
  String8 override_config = push_str8f(scratch.arena, "%Svalue=\"review\" presents=\"icon\"%S",
    str8_prefix(config, at), str8_skip(config, at+override_field.size));
  ok = wheelhouse_configure(fixture.core, uishell_sidebar_text(override_config), 0) && ok;
  uishell_sidebar_refresh(&fixture);
  for(U64 phase = 0; phase < 2; phase++)
  {
    for(U64 i = 0; i < uishell_sidebar_node_count(&fixture); i++)
    {
      UIShell_SidebarNode node = {0}; uishell_sidebar_node(&fixture, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity.kind), str8_lit("role"), 0) &&
         str8_match(uishell_sidebar_string(node.layout), str8_lit("inline"), 0))
      { ok = str8_match(uishell_sidebar_chip_icon(&fixture, node, &icon_font),
          rd_icon_kind_text_table[phase ? RD_IconKind_Glasses : RD_IconKind_Gear], 0) && icon_font && ok; }
    }
    String8 patch = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"role\",\"id\":\"p/governor\"}},\"source_id\":\"fixture\",\"set\":{},\"unset\":[\"presentation.icon\"]}");
    ok = wheelhouse_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(patch), 0) && ok;
    uishell_sidebar_refresh(&fixture);
  }
  ok = wheelhouse_configure(fixture.core, uishell_sidebar_text(config), 0) && ok;
  uishell_sidebar_refresh(&fixture);
  // Producer labels that resemble directives remain ordinary labels. The
  // first width also exercises a wide-glyph compact subject reference.
  String8 unicode_patch = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"convoy\",\"id\":\"build\"}},\"source_id\":\"fixture\",\"set\":{\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"chip-icon:列車の作業\"}},\"display.label.medium\":{\"value\":{\"type\":\"text\",\"value\":\"chip-status:列車\"}},\"display.label.short\":{\"value\":{\"type\":\"text\",\"value\":\"列車\"}}},\"unset\":[]}");
  ok = wheelhouse_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(unicode_patch), 0) && ok;
  unicode_patch = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"change_request\",\"id\":\"pr-281\"}},\"source_id\":\"fixture\",\"set\":{\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"漢!281\"}}},\"unset\":[]}");
  ok = wheelhouse_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(unicode_patch), 0) && ok;
  uishell_sidebar_refresh(&fixture);
  for(U64 i = 0; i < uishell_sidebar_node_count(&fixture); i++)
  {
    UIShell_SidebarNode node = {0}; uishell_sidebar_node(&fixture, i, &node);
    if(str8_match(uishell_sidebar_string(node.entity.id), str8_lit("build"), 0))
    {
      ok = !uishell_sidebar_string(node.icon).size &&
        str8_match(uishell_sidebar_string(node.medium_label), str8_lit("chip-status:列車"), 0) &&
        str8_match(uishell_sidebar_chip_status(&fixture, node), str8_lit("active"), 0) && ok;
    }
  }
  // A chip takes its status and attention from what the template presents,
  // here a fact other than the row's own status.
  {
    String8 activity_field = str8_lit("field \"activity\" key=\"status.state\" presents=\"status\"");
    U64 activity_at = str8_find_needle(config, 0, activity_field, 0);
    String8 presented_config = push_str8f(scratch.arena, "%Sfield \"activity\" key=\"presented.status\" presents=\"status\"%S",
      str8_prefix(config, activity_at), str8_skip(config, activity_at+activity_field.size));
    ok = activity_at < config.size && wheelhouse_configure(fixture.core, uishell_sidebar_text(presented_config), 0) && ok;
    String8 attentions[] = {str8_lit("true"), str8_lit("false")};
    for(U64 step = 0; step < ArrayCount(attentions); step++)
    {
      String8 patch = push_str8f(scratch.arena, "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"vessel\",\"id\":\"chip-v\"}},\"source_id\":\"fixture\","
        "\"set\":{\"status.state\":{\"value\":{\"type\":\"text\",\"value\":\"idle\"}},"
        "\"presented.status\":{\"value\":{\"type\":\"text\",\"value\":\"waiting\"}},"
        "\"status.attention\":{\"value\":{\"type\":\"text\",\"value\":\"%S\"}}},\"unset\":[]}", attentions[step]);
      ok = wheelhouse_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(patch), 0) && ok;
      uishell_sidebar_refresh(&fixture);
      B32 found = 0;
      for(U64 i = 0; !found && i < uishell_sidebar_node_count(&fixture); i++)
      {
        UIShell_SidebarNode node = {0}; uishell_sidebar_node(&fixture, i, &node);
        if(!str8_match(uishell_sidebar_string(node.entity.id), str8_lit("chip-v"), 0)) { continue; }
        found = 1;
        node.state = WHEELHOUSE_LIVE; node.selected = 0;
        String8 status = uishell_sidebar_chip_status(&fixture, node);
        B32 attention = uishell_sidebar_chip_attention(&fixture, node);
        B32 presented = str8_match(status, str8_lit("waiting"), 0) && attention == (step == 0);
        if(!presented)
        { fprintf(stderr, "FAIL chip presented values: step %llu status %.*s attention %d\n", (unsigned long long)step, (int)status.size, status.str, attention); }
        ok = presented && ok;
      }
      ok = found && ok;
    }
    String8 restore = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"vessel\",\"id\":\"chip-v\"}},\"source_id\":\"fixture\","
      "\"set\":{\"status.state\":{\"value\":{\"type\":\"text\",\"value\":\"active\"}}},\"unset\":[\"presented.status\",\"status.attention\"]}");
    ok = wheelhouse_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(restore), 0) && ok;
    ok = wheelhouse_configure(fixture.core, uishell_sidebar_text(config), 0) && ok;
    uishell_sidebar_refresh(&fixture);
  }
  ws->sidebar = &fixture;
  ui_select_state(test);
  // A convoy switches between a compact row of workspace actions and child
  // rows. It starts collapsed; exercise the actual collapse action and
  // renderer in both directions.
  for(U32 phase = 0; phase < 3; phase++)
  {
    UIShell_SidebarNode convoy = {0};
    String8 vessel_key = str8_zero(), owner_key = str8_zero();
    for(U64 i = 0; i < uishell_sidebar_node_count(&fixture); i++)
    {
      UIShell_SidebarNode node = {0}; uishell_sidebar_node(&fixture, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity.id), str8_lit("build"), 0)) { convoy = node; }
      if(str8_match(uishell_sidebar_string(node.entity.id), str8_lit("chip-v"), 0) &&
         str8_match(uishell_sidebar_string(node.layout), str8_lit("inline"), 0))
      {
        vessel_key = push_str8_copy(scratch.arena, uishell_sidebar_string(node.key));
        UIShell_SidebarNode owner = {0}; uishell_sidebar_node(&fixture, node.parent, &owner);
        owner_key = push_str8_copy(scratch.arena, uishell_sidebar_string(owner.key));
      }
    }
    if(!uishell_sidebar_string(convoy.key).size)
    {
      fprintf(stderr, "FAIL chip fixture: build convoy is missing\n");
      ok = 0;
      break;
    }
    if(phase)
    {
      ok = uishell_sidebar_intend(&fixture, uishell_sidebar_toggle_intent(convoy), 0) && ok;
      uishell_sidebar_refresh(&fixture);
    }
    for(U32 frame = 0; frame < 2; frame++)
    {
      UI_IconInfo icons = ws->ui->icon_info;
      UI_AnimationInfo animation = {0}; UI_EventList events = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      ui_state->mouse = v2f32(-100, -100);
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
      { uishell_sidebar_ui(r2f32p(0, 0, 600, 900), split); }
      ui_end_build();
    }
    B32 row_seen = 0, chip_seen = 0;
    F32 convoy_x = 0, vessel_x = 0;
    for(UI_Box *box = test->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, test->root).next)
    {
      UI_Box *keyed = box->parent;
      while(!ui_box_is_nil(keyed) && ui_key_match(keyed->key, ui_key_zero())) { keyed = keyed->parent; }
      if(ui_box_is_nil(keyed)) { continue; }
      row_seen |= ui_key_match(box->key, ui_key_from_stringf(keyed->key, "###sidebar_row_%S", vessel_key));
      chip_seen |= ui_key_match(box->key, ui_key_from_stringf(keyed->key, "###action_%S", vessel_key));
      if(ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###entry_%S", owner_key))) { convoy_x = box->rect.x0; }
      if(ui_key_match(box->key, ui_key_from_stringf(box->parent->key, "###entry_%S", vessel_key))) { vessel_x = box->rect.x0; }
    }
    if(!vessel_key.size || row_seen != (phase == 1) || chip_seen != (phase != 1))
    {
      fprintf(stderr, "FAIL convoy expansion: phase %u vessel row=%u chip=%u\n", phase, row_seen, chip_seen);
      ok = 0;
    }
    // A vessel's row sits under its convoy's, a visible level further in: an
    // icon's width (1.2 em, over 10 px at any sidebar font size), not a few
    // pixels that a narrow icon glyph hides.
    if(row_seen && !(convoy_x > 0 && vessel_x >= convoy_x+10.f))
    {
      fprintf(stderr, "FAIL convoy expansion: phase %u vessel label x=%g not indented past convoy x=%g\n", phase, vessel_x, convoy_x);
      ok = 0;
    }
  }
  F32 widths[] = {90, 140, 240, 320, 600};
  for(U64 w = 0; w < ArrayCount(widths); w++)
  {
    F32 status_x = 0;
    for(U64 phase = 0; phase < 3; phase++)
    {
      // First latent, then pending, then no subjects. All three must leave
      // the trailing slot at precisely the same location for this width.
      if(phase == 1)
      {
        for(U64 i = 0; i < uishell_sidebar_node_count(&fixture); i++)
        {
          UIShell_SidebarNode node = {0}; uishell_sidebar_node(&fixture, i, &node);
          if(str8_match(uishell_sidebar_string(node.entity.id), str8_lit("chip-v"), 0))
          { ok = uishell_sidebar_intend(&fixture, uishell_sidebar_activate_intent(node), 0) && ok; break; }
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
          ok = wheelhouse_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(patch), 0) && ok;
        }
        uishell_sidebar_refresh(&fixture);
      }
      UIShell_SidebarNode convoy = {0};
      for(U64 i = 0; i < uishell_sidebar_node_count(&fixture); i++)
      {
        UIShell_SidebarNode node = {0}; uishell_sidebar_node(&fixture, i, &node);
        if(str8_match(uishell_sidebar_string(node.entity.id), str8_lit("build"), 0)) { convoy = node; break; }
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
      if(phase == 0)
      {
        UI_Box *viewport = &ui_nil_box, *first = &ui_nil_box, *last = &ui_nil_box, *overflow = &ui_nil_box;
        for(UI_Box *box = row; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, row).next)
        {
          if(box->flags & UI_BoxFlag_ViewScrollX) { viewport = box; }
        }
        for(UI_Box *box = viewport->first; !ui_box_is_nil(box); box = box->next)
        {
          String8 display = ui_box_display_string(box);
          if(display.size && display.str[0] == '+') { overflow = box; }
          String8 label = ui_box_display_string(box->first);
          if(box->flags & UI_BoxFlag_Clickable &&
             (str8_match(label, str8_lit("漢!281"), 0) || str8_match(label, str8_lit("c!1000"), 0)))
          { if(ui_box_is_nil(first)) { first = box; } last = box; }
        }
        UI_Box *nav = viewport;
        while(!ui_box_is_nil(nav) && !(nav->flags & UI_BoxFlag_DefaultFocusNav)) { nav = nav->parent; }
        if(ui_box_is_nil(first) || ui_box_is_nil(nav) || dim_2f32(viewport->rect).x <= 0)
        { ok = 0; fprintf(stderr, "FAIL chip navigation viewport at width %g\n", widths[w]); }
        else
        {
          UI_Key last_key = last->key, viewport_key = viewport->key, nav_key = nav->key;
          UI_Key row_key = row->key, overflow_key = overflow->key;
          nav->default_nav_focus_next_hot_key = first->key;
          UI_Box *body = viewport->parent;
          while(!ui_box_is_nil(body) && !(body->flags & UI_BoxFlag_ViewScrollY)) { body = body->parent; }
          UI_Key body_key = body->key;
          F32 body_offset = body->view_off_target.y;
          // Scroll animation is disabled (rate=1): frames sequence native
          // events and retained layout, not a timed animation assumption.
          for(U32 frame = 0; frame < 30; frame++)
          {
            UI_IconInfo icons = ws->ui->icon_info;
            UI_AnimationInfo animation = {.scroll_animation_rate = 1};
            UI_EventList events = {0};
            UI_EventNode event = {0};
            // Native Tab navigation passes through ui_begin_build, rather than
            // directly assigning the final focus or offset under test.
            if(frame > 0 && frame < 5 && !ui_key_match(nav->default_nav_focus_hot_key, last_key))
            { event.v = (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Tab}; }
            if(frame >= 19 && frame < 25 && !ui_box_is_nil(overflow) &&
               !ui_key_match(nav->default_nav_focus_hot_key, overflow->key))
            { event.v = (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_Tab}; }
            if(frame == 10)
            { event.v = (UI_Event){.kind = UI_EventKind_Scroll, .pos = center_2f32(viewport->rect), .delta_2f32 = v2f32(-1000, 0), .scroll_is_precise = 1}; }
            if(event.v.kind != UI_EventKind_Null) { events.first = events.last = &event; events.count = 1; }
            ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
            ui_state->mouse = frame == 15 ? v2f32(-100, -100) : center_2f32(viewport->rect);
            UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
            {
              // Remove the sidebar for one build, then refocus the same chip
              // at the same width. Its cached key must not suppress reveal.
              if(frame != 14) { uishell_sidebar_ui(r2f32p(0, 0, frame >= 6 ? Max(80.f, widths[w]-30.f) : widths[w], 900), split); }
              else { viewport->view_off.x = viewport->view_off_target.x = 0; }
            }
            ui_end_build();
            if(frame != 14)
            {
              // Removed boxes are reclaimed by ui_end_build. Resolve retained
              // keys again instead of reading pointers from before the rebuild.
              body = ui_box_from_key(body_key);
              row = ui_box_from_key(row_key);
              viewport = ui_box_from_key(viewport_key);
              nav = ui_box_from_key(nav_key);
              last = ui_box_from_key(last_key);
              overflow = ui_box_from_key(overflow_key);
            }
            if(frame == 15)
            {
              test->default_nav_root_key = nav_key;
              nav->default_nav_focus_next_hot_key = last_key;
            }
            if(frame == 9 || frame == 18)
            {
              F32 visible = Min(dim_2f32(last->rect).x, dim_2f32(viewport->rect).x);
              if(!ui_key_match(nav->default_nav_focus_hot_key, last_key) ||
                 Min(last->rect.x1, viewport->rect.x1)-Max(last->rect.x0, viewport->rect.x0) < visible-1)
              { ok = 0; fprintf(stderr, "FAIL Tab chip reveal at width %g: focus=%d chip=[%g,%g] viewport=[%g,%g] offset=%g target=%g\n", widths[w], ui_key_match(nav->default_nav_focus_hot_key, last_key), last->rect.x0, last->rect.x1, viewport->rect.x0, viewport->rect.x1, viewport->view_off.x, viewport->view_off_target.x); }
            }
            if(frame == 25 && !ui_box_is_nil(overflow))
            {
              if(overflow->parent != viewport || !ui_key_match(nav->default_nav_focus_hot_key, overflow->key) ||
                 Min(overflow->rect.x1, viewport->rect.x1)-Max(overflow->rect.x0, viewport->rect.x0) <
                   Min(dim_2f32(overflow->rect).x, dim_2f32(viewport->rect).x)-1)
              { ok = 0; fprintf(stderr, "FAIL overflow chip focus/reveal at width %g\n", widths[w]); }
            }
            if(frame == 12 && (viewport->view_off_target.x != 0 || viewport->view_off.x != 0))
            { ok = 0; fprintf(stderr, "FAIL precise horizontal chip scroll at width %g\n", widths[w]); }
          }
          if(body->view_off_target.y != body_offset)
          { ok = 0; fprintf(stderr, "FAIL horizontal chip scroll changed outer offset\n"); }
          nav->default_nav_focus_next_hot_key = ui_key_zero();
        }
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
    fixture.core = 0;
    fixture.core = wheelhouse_create(config.str, config.size, 0);
    if(!fixture.core) { ok = 0; break; }
    for(U64 start = 0; start < patches.size;)
    {
      U64 end = start; while(end < patches.size && patches.str[end] != '\n') { end++; }
      ok = wheelhouse_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(str8(patches.str+start, end-start)), 0) && ok;
      start = end+1;
    }
    uishell_sidebar_refresh(&fixture);
  }
  if(fixture.core && uishell_sidebar_viewing(&fixture))
  {
    // Simulate a template with stale activity presentation. The core's retained
    // terminal status still wins, including pending glyphs and menu labels.
    String8 activity_field = str8_lit("field \"activity\" key=\"status.state\" presents=\"status\"");
    U64 activity_at = str8_find_needle(config, 0, activity_field, 0);
    String8 stale_config = push_str8f(scratch.arena, "%Sfield \"activity\" source=\"literal\" value=\"active\" presents=\"status\"%S",
      str8_prefix(config, activity_at), str8_skip(config, activity_at+activity_field.size));
    ok = activity_at < config.size && wheelhouse_configure(fixture.core, uishell_sidebar_text(stale_config), 0) && ok;
    uishell_sidebar_refresh(&fixture);
    for(U64 i = 0; uishell_sidebar_viewing(&fixture) && i < uishell_sidebar_node_count(&fixture); i++)
    {
      UIShell_SidebarNode node = {0}; uishell_sidebar_node(&fixture, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity.id), str8_lit("chip-v"), 0))
      { ok = uishell_sidebar_intend(&fixture, uishell_sidebar_activate_intent(node), 0) && ok; break; }
    }
    WheelhouseEffects *effects = wheelhouse_effects_take(fixture.core, 0);
    WheelhouseEffect effect = {0};
    B32 materialized = effects && wheelhouse_effects_get(effects, 0, &effect) && effect.kind == WHEELHOUSE_EFFECT_MATERIALIZE;
    ok = materialized && ok;
    // A workspace this test makes up, read back through the index as 4242.
    UIShell_WorkspaceId retained_id = uishell_workspace_id_make();
    uishell_workspace_index_insert(retained_id, 4242);
    if(materialized) { ok = wheelhouse_complete(fixture.core, effect.request_id, 1, uishell_sidebar_workspace(retained_id), uishell_sidebar_text(str8_zero()), 0) && ok; }
    if(effects) { wheelhouse_effects_release(effects); }
    WheelhouseWorkspace retained = {uishell_sidebar_workspace(retained_id), 0, uishell_sidebar_text(str8_lit("chip-v")), 1};
    ok = wheelhouse_observe(fixture.core, &retained, 1, 0, 0, 0) && ok;
    String8 terminal_patch = str8_lit("{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"vessel\",\"id\":\"chip-v\"}},\"source_id\":\"fixture\",\"set\":{\"flotilla.convoy.phase\":{\"value\":{\"type\":\"text\",\"value\":\"landed\"}}},\"unset\":[]}");
    ok = wheelhouse_apply_patch_json(fixture.core, 0, uishell_sidebar_text(uishell_sidebar_host_provider), uishell_sidebar_text(terminal_patch), 0) && ok;
    uishell_sidebar_refresh(&fixture);
    B32 toggled = 0;
    for(U64 i = 0; !toggled && i < uishell_sidebar_node_count(&fixture); i++)
    {
      UIShell_SidebarNode node = {0}; uishell_sidebar_node(&fixture, i, &node);
      for(U64 c = 0; c < node.control_count; c++)
      {
        WheelhouseControl control = {0}; control = uishell_sidebar_node_control(&fixture, node, c);
        if(str8_match(uishell_sidebar_string(control.label), str8_lit("Show finished"), 0))
        { toggled = uishell_sidebar_intend(&fixture, uishell_sidebar_control_intent(control), 0); break; }
      }
    }
    uishell_sidebar_refresh(&fixture);
    B32 ended_chip = 0;
    for(U64 i = 0; i < uishell_sidebar_node_count(&fixture); i++)
    {
      UIShell_SidebarNode node = {0}; uishell_sidebar_node(&fixture, i, &node);
      if(uishell_sidebar_node_cfg(&node) == 4242 && str8_match(uishell_sidebar_string(node.entity.id), str8_lit("chip-v"), 0))
      {
        node.state = WHEELHOUSE_OPENING;
        ended_chip = str8_match(uishell_sidebar_string(node.status), str8_lit("active"), 0) &&
          str8_match(uishell_sidebar_chip_status(&fixture, node), str8_lit("ended"), 0) &&
          str8_match(uishell_sidebar_status_mark(node, uishell_sidebar_chip_status(&fixture, node)), str8_lit("×"), 0) &&
          str8_match(uishell_sidebar_chip_label(&fixture, node), str8_lit("Subject workspace ×"), 0);
        if(!ended_chip)
        {
          String8 activity = uishell_sidebar_string(node.status);
          String8 status = uishell_sidebar_chip_status(&fixture, node), label = uishell_sidebar_chip_label(&fixture, node);
          fprintf(stderr, "FAIL retained chip: activity %.*s status %.*s label %.*s\n",
            (int)activity.size, activity.str, (int)status.size, status.str, (int)label.size, label.str);
        }
      }
    }
    ok = toggled && ended_chip && ok;
    if(!toggled || !ended_chip) { fprintf(stderr, "FAIL retained chip setup: materialized %d toggled %d ended %d\n", materialized, toggled, ended_chip); }
  }
  else { ok = 0; }
  ws->sidebar = saved_sidebar;
  ui_select_state(saved_ui); ui_state_release(test);
  uishell_sidebar_release(&fixture);
  scratch_end(scratch);
  fprintf(stderr, "Chip sidebar diagnostics: %s (convoy expansion, 90/140/240/320/600px, Tab reveal, precise horizontal scroll, latent/pending/removed, fixed status, ended precedence)\n", ok ? "passed" : "FAILED");
  return ok;
}
