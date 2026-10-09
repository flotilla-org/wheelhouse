// The actual shared embedded query row is built on a Text view. No query state
// or editing implementation is replaced; only physical input is synthesized.
internal B32
uishell_query_ui_diagnostics(RD_WindowState *ws)
{
  UI_State *saved_ui = ui_state, *saved_window_ui = ws->ui;
  UI_State *test = ui_state_alloc();
  UIShell_CmdList saved_commands[2] = {rd_state->cmds[0], rd_state->cmds[1]};
  Arena *saved_arenas[2] = {rd_state->cmds_arenas[0], rd_state->cmds_arenas[1]};
  U64 saved_generation = rd_state->cmds_gen;
  B32 saved_text_edit = rd_state->text_edit_mode;
  Arena *arena = arena_alloc();
  rd_state->cmds[0] = (UIShell_CmdList){0}; rd_state->cmds[1] = (UIShell_CmdList){0};
  rd_state->cmds_arenas[0] = rd_state->cmds_arenas[1] = arena;
  rd_state->cmds_gen = 0;
  ws->ui = test; ui_select_state(test);
  CFG_Node *view = cfg_node_new(rd_state->cfg, cfg_node_from_id(ws->cfg_id), str8_lit("text"));
  RD_ViewState *vs = rd_view_state_from_cfg(view);
  B32 ok = 1;
#define QueryCheck(expr) do { if(!(expr)) { fprintf(stderr, "FAIL embedded query UI %d: %s\n", __LINE__, #expr); ok = 0; } } while(0)
  UIShell_RegsScope(.window = ws->cfg_id, .view = view->id, .tab = view->id,
                    .edit_owner_key = ui_key_zero(), .edit_owner_captured = 0)
  {
    for(U64 frame = 0; frame < 8; frame++)
    {
      UI_EventList events = {0};
      if(frame == 0 || frame == 3 || frame == 6)
      {
        if(frame == 3) { vs->contents_are_focused = 1; }
        // Native menu events enter the same shell resolver as physical keys.
        WM_Event menu = {.kind = WM_EventKind_MenuCommand, .window = ws->os, .string = str8_lit("search")};
        QueryCheck(uishell_route_command_activation(arena, ws, &menu, 0, 1));
        uishell_edit_drain_commands();
        QueryCheck(vs->query_is_open && !vs->contents_are_focused);
        if(frame == 3 || frame == 6)
        {
          QueryCheck(vs->query_mark.column == 1);
          QueryCheck(vs->query_cursor.column == rd_view_query_input().size+1);
        }
      }
      if(frame == 1 || frame == 4)
      {
        UI_Event text = {.kind = UI_EventKind_Text, .string = frame == 1 ? str8_lit("edited \xce\xbb") : str8_lit("replacement")};
        ui_event_list_push(arena, &events, &text);
      }
      if(frame == 5)
      {
        UI_Event escape = {.kind = UI_EventKind_Press, .key = WM_Key_Esc, .slot = UI_EventActionSlot_Cancel};
        ui_event_list_push(arena, &events, &escape);
      }
      UI_IconInfo icons = saved_window_ui->icon_info;
      UI_AnimationInfo animation = {0};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      ui_set_next_rect(r2f32p(0, 0, 600, 400));
      UI_Box *root = ui_build_box_from_key(0, ui_key_make(98001));
      UI_Parent(root) UI_ChildLayoutAxis(Axis2_Y)
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(16)
      UI_PrefWidth(ui_px(600, 1)) UI_PrefHeight(ui_px(28, 1))
      { rd_view_ui(r2f32p(0, 0, 600, 400)); }
      ui_end_build();
      if(frame == 1 || frame == 3)
      { QueryCheck(str8_match(rd_view_query_input(), str8_lit("edited \xce\xbb"), 0)); }
      if(frame == 4 || frame == 6)
      { QueryCheck(str8_match(rd_view_query_input(), str8_lit("replacement"), 0)); }
      if(frame == 5) { QueryCheck(!vs->query_is_open && vs->query_string_size == 0); }
      if(frame == 0 || frame == 3 || frame == 6)
      {
        QueryCheck(!ui_key_match(ui_state->edit_owner_key, ui_key_zero()));
      }
    }
    // Floating palette activation retains its existing fresh lister policy and
    // never changes the embedded query's text, command, open state or owner.
    for(U64 repeat = 0; repeat < 2; repeat++)
    {
      uishell_cmd("open_palette"); uishell_edit_drain_commands();
      QueryCheck(ws->query_is_active);
      CFG_Node *root = rd_immediate_cfg_from_keyf("window_query_%p", cfg_node_from_id(ws->cfg_id));
      CFG_Node *lister = cfg_node_child_from_string(root, str8_lit("watch"));
      QueryCheck(cfg_node_child_from_string(lister, str8_lit("lister")) != &cfg_nil_node);
      CFG_Node *query = cfg_node_child_from_string(lister, str8_lit("query"));
      QueryCheck(cfg_node_child_from_string(query, str8_lit("input"))->first->string.size == 0);
      QueryCheck(vs->query_is_open && str8_match(rd_view_query_input(), str8_lit("replacement"), 0));
      QueryCheck(str8_match(rd_view_query_cmd(), str8_lit("search"), 0));
      QueryCheck(uishell_regs()->view == view->id);
      UIShell_RegsScope(.view = lister->id)
      { uishell_cmd("update_query", .string = str8_lit("palette edited")); uishell_edit_drain_commands(); }
    }
    uishell_cmd("cancel_query"); uishell_edit_drain_commands();
  }
  cfg_node_release(rd_state->cfg, view);
  ui_state_release(test); ws->ui = saved_window_ui; ui_select_state(saved_ui);
  rd_state->cmds[0] = saved_commands[0]; rd_state->cmds[1] = saved_commands[1];
  rd_state->cmds_arenas[0] = saved_arenas[0]; rd_state->cmds_arenas[1] = saved_arenas[1];
  rd_state->cmds_gen = saved_generation; rd_state->text_edit_mode = saved_text_edit;
  arena_release(arena);
#undef QueryCheck
  fprintf(stderr, "embedded query UI scenario %s\n", ok ? "passed" : "failed");
  return ok;
}

// Typing a theme name applies it only once it names a theme (#264); other
// settings still apply as they're typed.
internal B32
uishell_setting_typing_diagnostics(void)
{
  E_Eval theme = {0}, other = {0};
  theme.space.kind = other.space.kind = RD_EvalSpaceKind_MetaCfg;
  theme.space.u64s[1] = e_id_from_string(str8_lit("theme"));
  other.space.u64s[1] = e_id_from_string(str8_lit("font_size"));
  String8 preset = rd_theme_preset_display_string_table[RD_ThemePreset_DefaultDark];
  B32 ok = !uishell_setting_applies_while_typing(theme, str8_prefix(preset, 3)) &&
    uishell_setting_applies_while_typing(theme, preset) &&
    uishell_setting_applies_while_typing(other, str8_lit("1"));
  fprintf(stderr, "%s a theme applies while typed only once it names one\n", ok ? "PASS" : "FAIL");
  // A finished edit saves a theme the picker lists, never a partial name, and
  // a name that isn't a theme still draws with the fallback's colours.
  Temp scratch = scratch_begin(0, 0);
  String8 completed = uishell_theme_name_from_typed(scratch.arena, str8_prefix(preset, 3));
  String8 exact = uishell_theme_name_from_typed(scratch.arena, preset);
  String8 none = uishell_theme_name_from_typed(scratch.arena, str8_lit("zzqqxx"));
  Access *access = access_open();
  UI_Theme *unknown = rd_theme_from_name_and_colors(scratch.arena, access, str8_prefix(preset, 3), (CFG_NodePtrList){0}, 1);
  access_close(access);
  B32 saved = str8_match(str8_prefix(completed, 3), str8_prefix(preset, 3), 0) && completed.size > 3 &&
    str8_match(exact, preset, 0) && none.size == 0 && unknown != 0 && unknown->patterns_count > 0;
  fprintf(stderr, "%s a finished theme edit saves a listed theme; an unknown name falls back\n", saved ? "PASS" : "FAIL");
  scratch_end(scratch);
  return ok && saved;
}
