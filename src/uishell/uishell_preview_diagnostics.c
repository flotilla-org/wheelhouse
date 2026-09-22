// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

// Count whole-panel scrims in the production panel builder's output.
internal U32
uishell_preview_dim_count(UI_Box *root, F32 alpha)
{
  U32 count = 0;
  for(UI_Box *box = root; !ui_box_is_nil(box); box = ui_box_rec_df_pre(box, root).next)
  {
    if((box->flags & UI_BoxFlag_DrawBackground) &&
       abs_f32(box->background_color.w-alpha) < 0.0001f &&
       dim_2f32(box->rect).x > 100 && dim_2f32(box->rect).y > 100)
    { count++; }
  }
  return count;
}

internal B32
uishell_preview_diagnostics(RD_WindowState *ws)
{
  Temp scratch = scratch_begin(0, 0);
  UI_State *saved_ui = ui_state, *test_ui = ui_state_alloc();
  RD_WorkspaceSurfaceEntry *saved_entry = ws->active_workspace_surface_entry;
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *owners[2];
  CFG_Node *views[2];
  UIShell_WorkspaceMount mounts[2];
  for(U32 i = 0; i < 2; i++)
  {
    owners[i] = cfg_node_new(rd_state->cfg, window, str8_lit("workspace"));
    cfg_node_new(rd_state->cfg, owners[i], str8_lit("split_x"));
    CFG_Node *panels = cfg_node_new(rd_state->cfg, owners[i], str8_lit("panels"));
    for(U32 p = 0; p < 2; p++)
    {
      CFG_Node *panel = cfg_node_new(rd_state->cfg, panels, str8_lit("0.5"));
      if(p == 0) { cfg_node_new(rd_state->cfg, panel, str8_lit("selected")); }
      CFG_Node *view = rd_cfg_new_view_tab(panel, str8_lit("terminal_fixture"), str8_zero(), 1);
      if(p == 0) { views[i] = view; }
    }
    mounts[i] = uishell_workspace_mount_from_owner_cfg(scratch.arena, window, owners[i]);
  }
  U32 failures = 0;
#define PreviewCheck(condition, label) do { if(!(condition)) { fprintf(stderr, "FAIL preview: %s\n", label); failures++; } } while(0)
  ui_select_state(test_ui);
  UIShell_RegsScope(.window = ws->cfg_id)
  {
    F32 dim = rd_setting_f32_from_name(str8_lit("inactive_panel_dim"));
    PreviewCheck(dim > 0.001f, "fixture requires inactive-panel dimming enabled");
    for(U32 frame = 0; frame < 12; frame++)
    {
      B32 overview = frame >= 4 && frame < 8;
      B32 press = frame == 2 || frame == 6;
      B32 file_drop = frame == 5 || frame == 9;
      if(frame == 9) { ws->drop_completion_panel = 0; }
      if(frame == 6)
      {
        for(U32 i = 0; i < 2; i++) { rd_view_state_from_cfg(views[i])->contents_are_focused = 0; }
      }
      UI_IconInfo icons = ws->ui->icon_info;
      UI_AnimationInfo animation = {0};
      UI_EventNode event = {.v = press ?
                            (UI_Event){.kind = UI_EventKind_Press, .key = WM_Key_LeftMouseButton, .pos = v2f32(200, 150)} :
                            file_drop ? (UI_Event){.kind = UI_EventKind_FileDrop, .pos = v2f32(200, 150)} :
                                        (UI_Event){.kind = UI_EventKind_Text, .string = str8_lit("x")}};
      UI_EventList events = {.first = &event, .last = &event, .count = 1};
      ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
      ui_state->mouse = v2f32(200, 150);
      UI_Box *wrappers[2];
      for(U32 i = 0; i < 2; i++)
      {
        B32 preview = overview || i != 0;
        // Same full-size layout coordinates, as in the live + hover-preview path.
        ui_set_next_rect(r2f32p(0, 0, 640, 480));
        wrappers[i] = ui_build_box_from_key(preview ? UI_BoxFlag_IgnoreInteraction : 0, ui_key_make(100+i));
        RD_WorkspaceSurfaceEntry entry = {.workspace_id = owners[i]->id, .composite = !preview};
        // Returning from overview also exercises the direct, non-surface path.
        ws->active_workspace_surface_entry = frame >= 8 && !preview ? 0 : &entry;
        UIShell_Regs before = *uishell_regs();
        UI_Key hot = ui_hot_key(), active = ui_active_key(UI_MouseButtonKind_Left);
        U64 event_count = events.count;
        CFG_ID drop_completion_panel = ws->drop_completion_panel;
        UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(12)
        UI_Parent(wrappers[i]) UI_Focus(preview ? UI_FocusKind_Off : UI_FocusKind_On)
        {
          rd_panel_area_ui(scratch, r2f32p(0, 0, 640, 480), r2f32p(0, 0, 640, 480), ws, &mounts[i], !preview, 0, 0, 0, 0);
        }
        if(preview)
        {
          PreviewCheck(uishell_regs()->view == before.view && uishell_regs()->panel == before.panel, "preview preserves live command target");
          PreviewCheck(ui_key_match(hot, ui_hot_key()) && ui_key_match(active, ui_active_key(UI_MouseButtonKind_Left)), "preview preserves live hot/active keys");
          PreviewCheck(events.count == event_count, "preview does not consume remaining input");
          if(press) { PreviewCheck(!rd_view_state_from_cfg(views[i])->contents_are_focused, "overlapping preview press does not focus view contents"); }
          if(file_drop) { PreviewCheck(ws->drop_completion_panel == drop_completion_panel, "preview file drop does not retarget completion"); }
        }
        else if(press) { PreviewCheck(rd_view_state_from_cfg(views[i])->contents_are_focused, "live view accepts press focus"); }
        else if(file_drop)
        {
          PreviewCheck(events.count == 0, "direct live workspace accepts file drop");
          PreviewCheck(ws->drop_completion_panel != drop_completion_panel, "direct live file drop targets panel");
        }
      }
      ws->active_workspace_surface_entry = 0;
      ui_end_build();
      if(frame == 3 || frame == 7 || frame == 11)
      {
        U32 live_dims = uishell_preview_dim_count(wrappers[0], dim);
        U32 preview_dims = uishell_preview_dim_count(wrappers[1], dim);
        fprintf(stderr, "Panel scrims, phase %u: selected=%u other=%u (expected %u,0)\n", frame/4, live_dims, preview_dims, overview ? 0 : 1);
        PreviewCheck(live_dims == (overview ? 0 : 1), "selected workspace dimming follows live/overview presentation");
        PreviewCheck(preview_dims == 0, "preview omits inactive-panel dimming");
      }
    }
  }
  ws->active_workspace_surface_entry = saved_entry;
  ui_select_state(saved_ui);
  ui_state_release(test_ui);
  for(U32 i = 0; i < 2; i++) { cfg_node_release(rd_state->cfg, owners[i]); }
  scratch_end(scratch);
  fprintf(stderr, "Preview diagnostics: %u failures\n", failures);
#undef PreviewCheck
  return failures == 0;
}
