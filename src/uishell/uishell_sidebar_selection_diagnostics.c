// Report coverage against inventory IDs, including collapsed descendants.
internal B32
uishell_sidebar_coverage_diagnostics(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  B32 ok = 1;
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
  {
    B32 found = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state->snapshot, i, &node);
      if(!node.is_section && node.state == ANDAMENTO_LIVE && node.workspace_id == w->id) { found = 1; break; }
    }
    if(!found) { fprintf(stderr, "Open workspace without sidebar entry: %llu (%.*s)\n", (unsigned long long)w->id, (int)w->display_name.size, w->display_name.str); ok = 0; }
  }
  return ok;
}

// Scenario tests use the production builder and inspect its emitted UI boxes.
internal void
uishell_sidebar_selection_boxes(UI_Box *box, U64 *selected, U64 *outlined)
{
  Vec4F32 fill = uishell_sidebar_selection_fill(0), chip = uishell_sidebar_selection_fill(1);
  for(UI_Box *b = box; !ui_box_is_nil(b); b = b->next)
  {
    if((b->flags & UI_BoxFlag_DrawBackground) &&
       (MemoryMatch(&b->background_color, &chip, sizeof(fill)) ||
        MemoryMatch(&b->background_color, &fill, sizeof(fill)))) { *selected += 1; }
    if((b->flags & UI_BoxFlag_DrawBorder) &&
       MemoryMatch(&b->border_color, &chip, sizeof(chip))) { *outlined += 1; }
    uishell_sidebar_selection_boxes(b->first, selected, outlined);
  }
}

internal B32
uishell_sidebar_selection_diagnostics(RD_WindowState *ws, UIShell_ControlledSplit *host_split)
{
  Temp scratch = scratch_begin(0, 0);
  UIShell_SidebarState *saved = ws->sidebar;
  UI_State *saved_ui = ui_state;
  F32 saved_rate = rd_state->menu_animation_rate;
  rd_state->menu_animation_rate = 1.f;
  B32 ok = 1;
  char *error = 0;
  UIShell_SidebarState state = {.initialized = 1};
  String8 config = str8_lit(
    "region \"tree\" root-template=\"title\" placement=\"tree\"\n"
    "template \"title\" slot=\"compact\" node-kind=\"entity\" { field \"label\" source=\"literal\" value=\"Projects\"; }\n"
    "placement \"tree\" { for \"project\" kind=\"project\" { apply-template \"project\"; }; }\n"
    "template \"project\" { field \"label\" key=\"display.label\"; for \"convoy\" kind=\"convoy\" { match \"flotilla.project\" of=\"project\"; apply-template \"convoy\"; }; }\n"
    "template \"convoy\" { field \"label\" key=\"display.label\"; for \"vessel\" kind=\"vessel\" { match \"flotilla.convoy\" of=\"convoy\"; apply-template \"vessel\"; }; }\n"
    "template \"vessel\" { field \"label\" key=\"display.label\"; }\n"
    "region \"attention\" root-template=\"title\" placement=\"attention\"\n"
    "placement \"attention\" { for \"attention\" kind=\"vessel\" { apply-template \"vessel\"; }; }\n"
    "region \"duplicate\" root-template=\"title\" placement=\"tree\"\n");
  state.core = andamento_create(config.str, config.size, &error);
  ok &= uishell_sidebar_result(&state, state.core != 0, error);
  if(!state.core) { uishell_sidebar_release(&state); scratch_end(scratch); return 0; }
  char *kinds[] = {"project", "convoy", "vessel"};
  char *ids[] = {"selection-project", "selection-convoy", "selection-vessel"};
  for(U64 i = 0; i < 3; i++)
  {
    String8 patch = push_str8f(scratch.arena,
      "{\"target\":{\"kind\":\"entity\",\"value\":{\"kind\":\"%s\",\"id\":\"%s\"}},\"source_id\":\"selection-test\",\"set\":{"
      "\"display.label\":{\"value\":{\"type\":\"text\",\"value\":\"%s\"}},"
      "\"flotilla.project\":{\"value\":{\"type\":\"text\",\"value\":\"selection-project\"}},"
      "\"flotilla.convoy\":{\"value\":{\"type\":\"text\",\"value\":\"selection-convoy\"}}},\"unset\":[]}", kinds[i], ids[i], ids[i]);
    ok &= andamento_apply_patch_json(state.core, 0, uishell_sidebar_text(patch), 0);
  }
  // The split's inventory is the real process-boundary observation contract;
  // no terminal process is required to verify navigation presentation.
  UIShell_ControlledSplit split = *host_split;
  UIShell_MaterializedWorkspace vessel = {0}, convoy = {0};
  vessel.id = 420; vessel.display_name = str8_lit("Vessel"); vessel.next = &convoy;
  convoy.id = 430; convoy.display_name = str8_lit("Convoy");
  vessel.mount.owner_cfg = convoy.mount.owner_cfg = &cfg_nil_node;
  vessel.workspace_id = uishell_workspace_id_make(); convoy.workspace_id = uishell_workspace_id_make();
  uishell_workspace_index_insert(vessel.workspace_id, vessel.id);
  uishell_workspace_index_insert(convoy.workspace_id, convoy.id);
  split.inventory.first = &vessel; split.inventory.last = &convoy; split.inventory.count = 2;
  for(U64 i = 1; i < 3; i++)
  {
    String8 patch = push_str8f(scratch.arena,
      "{\"target\":{\"kind\":\"tab\",\"value\":\"%S\"},\"source_id\":\"selection-test\",\"set\":{"
      "\"entity.kind\":{\"value\":{\"type\":\"text\",\"value\":\"%s\"}},"
      "\"entity.id\":{\"value\":{\"type\":\"text\",\"value\":\"%s\"}}},\"unset\":[]}",
      uishell_string_from_workspace_id(scratch.arena, i == 1 ? convoy.workspace_id : vessel.workspace_id), kinds[i], ids[i]);
    ok &= andamento_apply_patch_json(state.core, 0, uishell_sidebar_text(patch), 0);
  }
  UIShell_SidebarSection section = {.key = str8_lit("tree")};
  state.sections = &section;
  ws->sidebar = &state;
  CFG_Node *view = uishell_sidebar_region_view(host_split->owner_cfg, str8_lit("tree"));
  for(U64 docked = 0; docked < 2; docked++)
  for(U64 section_closed = 0; section_closed < 2; section_closed++)
  for(U64 inline_layout = 0; inline_layout < 2; inline_layout++)
  for(U64 width = 0; width < 2; width++)
  for(U64 selection = 0; selection < 2; selection++)
  for(U64 project_closed = 0; project_closed < 2; project_closed++)
  for(U64 convoy_closed = 0; convoy_closed < 2; convoy_closed++)
  {
    if(width == 0 && selection == 0 && project_closed == 0 && convoy_closed == 0)
    {
      String8 layout = str8_lit("for \"vessel\" kind=\"vessel\"");
      U64 at = str8_find_needle(config, 0, layout, 0)+layout.size;
      String8 adjusted = inline_layout ? push_str8f(scratch.arena, "%S layout=\"inline\"%S", str8_prefix(config, at), str8_skip(config, at)) : config;
      ok &= andamento_configure(state.core, uishell_sidebar_text(adjusted), 0);
      uishell_sidebar_refresh(&state);
    }
    section.collapsed = section_closed;
    CFG_Node *collapsed = cfg_node_child_from_string(view, str8_lit("section_collapsed"));
    if(section_closed && collapsed == &cfg_nil_node) { cfg_node_new(rd_state->cfg, view, str8_lit("section_collapsed")); }
    if(!section_closed && collapsed != &cfg_nil_node) { cfg_node_release(rd_state->cfg, collapsed); }
    split.inventory.selected = selection ? &convoy : &vessel;
    uishell_sidebar_observe(&state, &split);
    ok &= uishell_sidebar_coverage_diagnostics(&state, &split);
    for(U64 kind = 0; kind < 2; kind++)
    {
      for(U64 index = 0; index < andamento_snapshot_node_count(state.snapshot); index++)
      {
        AndamentoNode node = {0}; uishell_sidebar_snapshot_node(state.snapshot, index, &node);
        if(str8_match(uishell_sidebar_string(node.entity_id), str8_cstring(ids[kind]), 0) &&
           node.collapsed != (kind ? convoy_closed : project_closed))
        { uishell_sidebar_dispatch(&state, node.toggle, 0); uishell_sidebar_refresh(&state); break; }
      }
    }
    UI_State *test_ui = ui_state_alloc(); ui_select_state(test_ui);
    state.managed_cfg_generation = cfg_change_gen(); state.managed_dirty = 0;
    UI_IconInfo icons = ws->ui->icon_info; UI_AnimationInfo animation = {0}; UI_EventList events = {0};
    ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
    UIShell_RegsScope(.window = host_split->owner_cfg->id, .view = view->id, .panel = view->parent->id)
    UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
    { uishell_sidebar_render(r2f32p(0, 0, width ? 800 : 240, 600), &split,
        (UIShell_SidebarRenderParams){docked ? UIShell_SidebarRenderMode_SectionPanel : UIShell_SidebarRenderMode_DiagnosticAggregate, str8_lit("tree")}); }
    ui_end_build();
    U64 selected = 0, outlined = 0;
    uishell_sidebar_selection_boxes(test_ui->root->first, &selected, &outlined);
    B32 hidden = section_closed || project_closed || (!selection && convoy_closed);
    B32 pass = selected == !hidden && outlined == hidden;
    if(!pass) { fprintf(stderr, "FAIL selection width=%llu convoy-selected=%llu project-closed=%llu convoy-closed=%llu: selected=%llu outlined=%llu\n", (unsigned long long)width, (unsigned long long)selection, (unsigned long long)project_closed, (unsigned long long)convoy_closed, (unsigned long long)selected, (unsigned long long)outlined); }
    ok &= pass;
    ui_select_state(saved_ui); ui_state_release(test_ui);
  }
  CFG_Node *collapsed = cfg_node_child_from_string(view, str8_lit("section_collapsed"));
  if(collapsed != &cfg_nil_node) { cfg_node_release(rd_state->cfg, collapsed); }
  ws->sidebar = saved; rd_state->menu_animation_rate = saved_rate;
  uishell_sidebar_release(&state);
  scratch_end(scratch);
  fprintf(stderr, "Sidebar selection diagnostics: %s (240/800px, rows/chips, aggregate/docked, both subjects, all collapse combinations)\n", ok ? "passed" : "FAILED");
  return ok;
}
