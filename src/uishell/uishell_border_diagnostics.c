typedef struct UIShell_BorderDiagnosticGroup UIShell_BorderDiagnosticGroup;
struct UIShell_BorderDiagnosticGroup
{
  UIShell_BorderDiagnosticGroup *next;
  R_BatchGroup2DNode *group;
  R_BatchGroup2DParams params;
};

// Compare production-frame seam pixels, not a copy of the border geometry.
internal B32
uishell_border_diagnostics(RD_WindowState *ws)
{
  Temp saved = scratch_begin(0, 0);
  CFG_Node *window = cfg_node_from_id(ws->cfg_id);
  CFG_Node *pct = cfg_node_child_from_string(window, str8_lit("control_split_pct"));
  B32 had_pct = pct != &cfg_nil_node;
  String8 old_pct = str8_copy(saved.arena, pct->first->string);
  if(!had_pct) { pct = cfg_node_new(rd_state->cfg, window, str8_lit("control_split_pct")); }
  B32 ok = 1;
  // A zero allocation must stay zero even when snapping an absolute boundary
  // would otherwise round its fractional origin to the next logical pixel.
  CFG_Node *zero_owner = cfg_node_new(rd_state->cfg, window, str8_lit("border_diagnostic_owner"));
  UIShell_ControlledSplit zero_split = {.owner_cfg = zero_owner};
  UI_FontSize(12)
  {
    ok &= uishell_controlled_split_control_width_px(&zero_split, r2f32p(3.75f, 0, 83.75f, 200)) == 0.f;
    CFG_Node *closed_owner = cfg_node_new(rd_state->cfg, window, str8_lit("border_diagnostic_closed"));
    cfg_node_new(rd_state->cfg, closed_owner, str8_lit("control_split_collapsed"));
    UIShell_ControlledSplit closed_split = {.owner_cfg = closed_owner};
    ok &= uishell_controlled_split_control_width_px(&closed_split, r2f32p(3.75f, 0, 1203.75f, 200)) == 0.f;
    cfg_node_release(rd_state->cfg, closed_owner);
  }
  if(!ok) { fprintf(stderr, "FAIL zero-width controlled-split allocation\n"); }
  // Commands and rendered geometry must share an aligned, in-range seam.
  UI_FontSize(12)
  {
    CFG_Node *test_pct = cfg_node_new(rd_state->cfg, zero_owner, str8_lit("control_split_pct"));
    F32 ratios[] = {0.f, 0.2f, 1.f};
    for(U32 phase = 0; phase < 9; phase++)
    {
      Rng2F32 rect = r2f32p(phase/8.f, 0, 1200.f + phase/8.f, 200);
      Rng1F32 range = uishell_controlled_split_control_width_range_px(&zero_split, rect, 12);
      for(U32 i = 0; i < ArrayCount(ratios); i++)
      {
        cfg_node_new_replacef(rd_state->cfg, test_pct, "%f", ratios[i]);
        F32 settled = uishell_controlled_split_settled_control_width(&zero_split, rect, 12);
        F32 rendered = uishell_controlled_split_control_width_px(&zero_split, rect);
        F32 edge = rect.x0 + settled;
        ok &= settled >= range.min && settled <= range.max;
        ok &= settled == rendered && edge == round_f32(edge);
      }
    }
  }
  cfg_node_release(rd_state->cfg, zero_owner);
  if(!ok) { fprintf(stderr, "FAIL controlled-split allocation alignment/range\n"); }
  U8 baseline[2][128] = {0};
  B32 saved_suppress = rd_state->frame_replay.suppress_input;
  rd_state->frame_replay.suppress_input = 1;
  for(U32 phase = 0; phase < 9; phase++)
  {
    Temp scratch = scratch_begin(&saved.arena, 1);
    Rng2F32 client = wm_client_rect_from_window(ws->os);
    F32 available = dim_2f32(client).x - 2.f*rd_window_edge_inset_px(ws);
    F32 width = floor_f32(available*0.2f) + phase/8.f;
    cfg_node_new_replacef(rd_state->cfg, pct, "%.9f", width/available);
    fnt_frame();
    dr_begin_frame(rd_font_from_slot(RD_FontSlot_Icons));
    rd_window_frame();
    F32 edge = ws->sidebar->rect.x1;
    UIShell_BorderDiagnosticGroup *groups = 0;
    for(R_PassNode *p = ws->draw_bucket->passes.first; p; p = p->next)
    {
      if(p->v.kind != R_PassKind_UI) { continue; }
      for(R_BatchGroup2DNode *g = p->v.params_ui->rects.first; g; g = g->next)
      {
        UIShell_BorderDiagnosticGroup *saved_group = push_array(scratch.arena, UIShell_BorderDiagnosticGroup, 1);
        saved_group->group = g;
        saved_group->params = g->params;
        SLLStackPush(groups, saved_group);
      }
    }
    for(U32 scale = 1; scale <= 2; scale++)
    {
      // Readback has its own viewport. Scale transforms and scissors, as
      // window submission does for a backing scale, without changing layout.
      for(UIShell_BorderDiagnosticGroup *g = groups; g; g = g->next)
      {
        g->group->params.xform = mul_3x3f32(make_scale_3x3f32(v2f32(scale, scale)), g->params.xform);
        g->group->params.clip.p0 = scale_2f32(g->params.clip.p0, scale);
        g->group->params.clip.p1 = scale_2f32(g->params.clip.p1, scale);
      }
      R_Readback pixels = r_pass_list_readback(scratch.arena,
        v2s32((S32)ceil_f32(client.x1*scale), (S32)ceil_f32(client.y1*scale)), &ws->draw_bucket->passes);
      U8 profile[128] = {0};
      // This isolated startup frame has no floating cards/menus. Sample the
      // control host's continuous vertical seam below the title bar.
      S32 y = (S32)(client.y1*0.6f*scale), anchor = (S32)floor_f32(edge)*scale;
      if(pixels.format != R_Tex2DFormat_BGRA8 ||
         pixels.data.size < (U64)pixels.size.x*pixels.size.y*4 ||
         anchor < 16 || anchor+15 >= pixels.size.x || y < 0 || y >= pixels.size.y)
      { fprintf(stderr, "FAIL border diagnostics: no BGRA readback at %ux\n", scale); ok = 0; }
      else
      {
        for(S32 dx = -16; dx < 16; dx++)
        { MemoryCopy(profile+(dx+16)*4, pixels.data.str+((U64)y*pixels.size.x+anchor+dx)*4, 4); }
        if(phase == 0)
        {
          MemoryCopyArray(baseline[scale-1], profile);
          // GL reads linear stage bytes; Metal/D3D read their final color
          // target. Require actual RGB contrast, not a fixed encoded brightness.
          U8 contrast = 0;
          for(U32 channel = 0; channel < 3; channel++)
          {
            U8 lo = 255, hi = 0;
            for(U32 i = channel; i < sizeof(profile); i += 4)
            {
              lo = Min(lo, profile[i]);
              hi = Max(hi, profile[i]);
            }
            contrast = Max(contrast, hi-lo);
          }
          if(contrast == 0)
          {
            fprintf(stderr, "FAIL border diagnostics: no visible seam at %ux\n", scale);
            ok = 0;
          }
        }
        else if(!MemoryMatchArray(baseline[scale-1], profile))
        {
          fprintf(stderr, "FAIL border raster profile changed at phase %u, %ux\n", phase, scale);
          ok = 0;
        }
      }
      // Restore exact values even after an invalid readback; no inverse scaling.
      for(UIShell_BorderDiagnosticGroup *g = groups; g; g = g->next)
      {
        g->group->params = g->params;
      }
    }
    scratch_end(scratch);
  }
  if(had_pct) { cfg_node_new_replace(rd_state->cfg, pct, old_pct); }
  else { cfg_node_release(rd_state->cfg, pct); }
  rd_state->frame_replay.suppress_input = saved_suppress;
  fprintf(stderr, "Controlled-split border raster diagnostics: %s (fractional positions, 1x/2x)\n", ok ? "passed" : "FAILED");
  scratch_end(saved);
  return ok;
}
