// Hosting settings diagnostics run against the production schema and view state.
internal B32
uishell_hosting_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  U32 failures = 0;
#define HostingCheck(condition, label) do { B32 passed = !!(condition); failures += !passed; fprintf(stderr, "%s hosting: %s\n", passed ? "PASS" : "FAIL", label); } while(0)
  // Issue #109: absent hosting is unavailable; in-process and every daemon
  // identity retain their exact value and offer the corresponding action.
  // Explicit generator covers empty, in-process, and daemon names/generations.
  String8 hosts[] = {str8_zero(), str8_lit("in_process"), str8_lit("daemon:default@0"),
                     str8_lit("daemon:work-name@1"), str8_lit("daemon:work@18446744073709551615")};
  for(U64 i = 0; i < ArrayCount(hosts); i++)
  {
    UIShell_RuntimeSetting setting = uishell_terminal_hosting_setting(scratch.arena, hosts[i]);
    HostingCheck(str8_match(setting.value, i == 0 ? str8_lit("unavailable") : hosts[i], 0), "live identity preserved");
    HostingCheck(str8_match(setting.command, i == 0 ? str8_zero() : i == 1 ? str8_lit("terminal_transfer") : str8_lit("terminal_adopt"), 0), "action follows hosting");
    HostingCheck(str8_match(setting.action, i == 0 ? str8_lit("Hosting unavailable") : i == 1 ? str8_lit("Hand to daemon") : str8_lit("Adopt"), 0), "action label follows hosting");
  }
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *panel = cfg_node_new(rd_state->cfg, window, str8_lit("panels"));
  CFG_Node *view = rd_cfg_new_view_tab(panel, str8_lit("terminal"), str8_zero(), 1);
  UIShell_RegsScope(.window = ws->cfg_id, .panel = panel->id, .tab = view->id, .view = view->id)
  {
    // Issue #109: hosting overlay is per-view and absent means off.
    HostingCheck(!rd_view_setting_b32_from_name(str8_lit("show_hosting_overlay")), "overlay defaults off");
    cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("show_hosting_overlay"));
    CFG_Node *overlay = cfg_node_child_from_string(view, str8_lit("show_hosting_overlay"));
    cfg_node_new(rd_state->cfg, overlay, str8_lit("1"));
    HostingCheck(rd_view_setting_b32_from_name(str8_lit("show_hosting_overlay")), "overlay can be enabled for this view");
    cfg_node_release(rd_state->cfg, overlay);
    HostingCheck(!rd_view_setting_b32_from_name(str8_lit("show_hosting_overlay")), "removing override restores off");
    // Runtime metadata/actions are schema rows and cannot enter text editing.
    char *keys[] = {"hosting", "hosting_action"};
    for(U64 i = 0; i < ArrayCount(keys); i++)
    {
      E_Eval eval = e_eval_from_stringf("config.$%I64x.%s", view->id, keys[i]);
      MD_Node *schema = uishell_runtime_setting_schema(scratch.arena, eval.space);
      HostingCheck(!md_node_is_nil(schema), "runtime row is exposed by the settings schema");
      HostingCheck(!ev_type_key_is_editable(eval.irtree.type_key), "runtime row is read-only");
    }
    // Before startup, the settings hook is safe and its action is disabled.
    UIShell_RuntimeSetting setting = uishell_runtime_setting(scratch.arena, view, str8_lit("terminal_hosting"));
    HostingCheck(setting.command.size == 0, "unstarted session has no action");
    UIShell_TerminalViewState *tv = rd_view_state(UIShell_TerminalViewState);
    cleat_provider_desc pd = {.abi_version = CLEAT_PROVIDER_ABI_VERSION, .backend = CLEAT_PROVIDER_BACKEND_IN_PROCESS};
    tv->provider = cleat_provider_open(&pd);
    String8 command = str8_lit("/bin/sh -c 'sleep 60'");
    cleat_session_desc sd = {.cols = 80, .rows = 24, .cell_width_px = 8, .cell_height_px = 16,
      .vt_engine = CLEAT_PROVIDER_VT_PASSTHROUGH, .command = command.str, .command_len = command.size};
    tv->session = tv->provider ? cleat_session_create(tv->provider, &sd) : 0;
    // Real provider collaborator: the runtime row reads the live session.
    setting = uishell_runtime_setting(scratch.arena, view, str8_lit("terminal_hosting"));
    HostingCheck(tv->session != 0 && str8_match(setting.value, str8_lit("in_process"), 0), "reads real session hosting");
    HostingCheck(str8_match(setting.command, str8_lit("terminal_transfer"), 0), "real session offers transfer");
    // Issue #109: the actual terminal canvas only builds the hosting pill
    // when its per-view overlay setting is enabled (off/on/off sequence).
    tv->initialized = 1;
    UI_State *saved_ui = ui_state, *test_ui = ui_state_alloc();
    ui_select_state(test_ui);
    for(U32 enabled = 0; enabled < 3; enabled++)
    {
      if(enabled == 1)
      {
        CFG_Node *node = cfg_node_child_from_string_or_alloc(rd_state->cfg, view, str8_lit("show_hosting_overlay"));
        cfg_node_new(rd_state->cfg, node, str8_lit("1"));
      }
      if(enabled == 2) { cfg_node_release(rd_state->cfg, cfg_node_child_from_string(view, str8_lit("show_hosting_overlay"))); }
      UI_EventList events = {0};
      UI_AnimationInfo animation = {0};
      ui_begin_build(ws->os, &events, &ws->ui->icon_info, ws->theme, &animation, 1.f/60, 1.f/60);
      UI_Key root_key = ui_key_from_string(ui_active_seed_key(), str8_lit("terminal_root"));
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
      {
        E_Eval eval = {0};
        rd_view_ui__terminal(eval, r2f32p(0, 0, 640, 480));
      }
      UI_Box *pill = ui_box_from_key(ui_key_from_string(root_key, str8_lit("terminal_hosting")));
      HostingCheck((!ui_box_is_nil(pill) && pill->last_touched_build_index == ui_state->build_index) == (enabled == 1), "canvas pill follows off/on/off setting");
      ui_end_build();
    }
    // Exercise the same schema-driven lister used by Selected Tab Settings.
    CFG_Node *settings = rd_cfg_new_view_tab(panel, str8_lit("watch"),
                                           push_str8f(scratch.arena, "query:config.$%I64x", view->id), 0);
    cfg_node_new(rd_state->cfg, settings, str8_lit("lister"));
    B32 saw_value = 0, saw_action = 0;
    Vec2F32 action_pos = {0};
    UIShell_CmdNode *before_action = rd_state->cmds[0].last;
    UIShell_RegsScope(.view = settings->id)
    for(U32 frame = 0; frame < 5; frame++)
    {
      UI_EventList events = {0};
      UI_EventNode click = {0};
      if(frame >= 3)
      {
        click.v = (UI_Event){.kind = frame == 3 ? UI_EventKind_Press : UI_EventKind_Release,
                             .key = WM_Key_LeftMouseButton, .pos = action_pos};
        events.first = events.last = &click;
        events.count = 1;
      }
      UI_AnimationInfo animation = {0};
      ui_begin_build(ws->os, &events, &ws->ui->icon_info, ws->theme, &animation, 1.f/60, 1.f/60);
      if(frame >= 3) { ui_state->mouse = action_pos; }
      UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12) UI_PrefHeight(ui_em(3, 1))
      { uishell_watch_view_ui(r2f32p(0, 0, 640, 480)); }
      ui_end_build();
      for(UI_Box *box = ui_state->root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, ui_state->root).next)
      {
        String8 text = ui_box_display_string(box);
        saw_value |= str8_match(text, str8_lit("Hosting: in_process"), 0);
        if(str8_match(text, str8_lit("Hand to daemon"), 0))
        {
          saw_action = 1;
          action_pos = center_2f32(box->rect);
        }
      }
    }
    HostingCheck(saw_value, "tab settings renders live read-only hosting");
    HostingCheck(saw_action, "tab settings renders hosting action button");
    // Clicking the schema action must route to its terminal, not the lister.
    B32 routed = 0;
    for(UIShell_CmdNode *n = before_action ? before_action->next : rd_state->cmds[0].first; n; n = n->next)
    {
      if(str8_match(n->cmd.name, str8_lit("terminal_transfer"), 0))
      { routed |= n->cmd.regs->view == view->id && n->cmd.regs->tab == view->id; }
    }
    HostingCheck(routed, "settings click routes action to owning terminal");
    ui_select_state(saved_ui);
    ui_state_release(test_ui);
    // Presentation never creates a persisted hosting value or action.
    HostingCheck(cfg_node_child_from_string(view, str8_lit("hosting")) == &cfg_nil_node &&
                 cfg_node_child_from_string(view, str8_lit("hosting_action")) == &cfg_nil_node, "runtime metadata is not persisted");
    uishell_terminal_runtime_release(tv);
  }
  cfg_node_release(rd_state->cfg, panel);
#undef HostingCheck
  scratch_end(scratch);
  return failures == 0;
}
