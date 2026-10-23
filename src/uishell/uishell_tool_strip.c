// The tool strip (docs/design/subject-workspaces.md, "Tool strip"): a column
// of icons at the Visible Workspace's left edge. Its sections, top to bottom:
// the workspace's subject, its Workspace Layouts, its Views (every slot,
// placed or not), the related workspaces and the linked items its subject
// kind's `strip` lists. Each section shows as many items as its user setting
// says and scrolls the rest; the linked items take what is left. Dragging a
// divider resizes the section above it.

typedef enum UIShell_StripSection
{
  UIShell_StripSection_Subject,
  UIShell_StripSection_Layouts,
  UIShell_StripSection_Views,
  UIShell_StripSection_Related,
  UIShell_StripSection_Linked,
  UIShell_StripSection_COUNT
}
UIShell_StripSection;

// The settings sizing each section, in items; the last section has none.
read_only global String8 uishell_tool_strip_size_settings[UIShell_StripSection_COUNT-1] =
{
  str8_lit_comp("tool_strip_subject_size"),
  str8_lit_comp("tool_strip_layouts_size"),
  str8_lit_comp("tool_strip_views_size"),
  str8_lit_comp("tool_strip_related_size"),
};

internal F32
uishell_tool_strip_item_height(void)
{
  return floor_f32(ui_top_font_size()*2.6f);
}

internal F32
uishell_tool_strip_divider_height(void)
{
  return floor_f32(ui_top_font_size()*0.6f);
}

// The most items a section's setting may ask for (its @range in uishell_meta.h).
#define UISHELL_TOOL_STRIP_SECTION_MAX 64

// A View's icon, by its slot's role.
read_only global struct { String8 role; RD_IconKind kind; } uishell_tool_strip_view_icons[] =
{
  {str8_lit_comp("shell"), RD_IconKind_Machine}, {str8_lit_comp("tui"), RD_IconKind_Window},
  {str8_lit_comp("agent"), RD_IconKind_Person}, {str8_lit_comp("files"), RD_IconKind_FolderClosedOutline},
  {str8_lit_comp("overview"), RD_IconKind_Thumbnails}, {str8_lit_comp("review"), RD_IconKind_Glasses},
  {str8_lit_comp("code"), RD_IconKind_Module},
};

internal String8
uishell_tool_strip_view_icon(UIShell_StripView *view)
{
  for(U64 i = 0; i < ArrayCount(uishell_tool_strip_view_icons); i++)
  {
    if(str8_match(view->role, uishell_tool_strip_view_icons[i].role, 0))
    { return rd_icon_kind_text_table[uishell_tool_strip_view_icons[i].kind]; }
  }
  return rd_icon_kind_text_table[RD_IconKind_FileOutline];
}

// One item: a square-ish button at `rect` (relative to its section), with
// `text` in the icon font or, for a layout's glyph, the main font.
internal UI_Signal
uishell_tool_strip_item(Rng2F32 rect, String8 text, B32 icon_font, B32 selected, B32 weak, UI_Key key)
{
  UI_Signal sig = {0};
  if(selected) { ui_set_next_background_color(uishell_sidebar_selection_fill(1)); }
  UI_Rect(rect) UI_TextAlignment(UI_TextAlign_Center) UI_CornerRadius(ui_top_font_size()*0.25f)
    RD_Font(icon_font ? RD_FontSlot_Icons : RD_FontSlot_Main) UI_TagF(weak ? "weak" : "")
  {
    UI_Box *box = ui_build_box_from_key(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText|UI_BoxFlag_DrawHotEffects|
                                        UI_BoxFlag_DrawActiveEffects|UI_BoxFlag_DisableTextTrunc|
                                        (selected ? UI_BoxFlag_DrawBackground : 0), key);
    ui_box_equip_display_string(box, text);
    sig = ui_signal_from_box(box);
  }
  return sig;
}

internal void
uishell_tool_strip_tooltip(UI_Signal sig, String8 text)
{
  if(ui_hovering(sig) && !rd_drag_is_active()) UI_Tooltip RD_Font(RD_FontSlot_Main)
  {
    ui_state->tooltip_anchor_key = sig.box->key;
    ui_label(text);
  }
}

// An entity's item: its hover card opens beside the strip, and a click runs
// its primary action (opening its URL, or opening or showing its workspace).
internal void
uishell_tool_strip_entity(UIShell_SidebarState *state, Rng2F32 rect, AndamentoEntity entity, UI_Key key)
{
  AndamentoNode node = {0};
  U64 index = uishell_sidebar_card_find(state, entity, &node);
  B32 found = index != ANDAMENTO_NONE;
  RD_IconKind icon = uishell_sidebar_card_icon(entity.kind);
  // A workspace not open yet is weak, as its row is.
  B32 unopened = node.state == ANDAMENTO_LATENT || node.state == ANDAMENTO_OPENING;
  UI_Signal sig = uishell_tool_strip_item(rect, rd_icon_kind_text_table[icon], 1, 0, !found || unopened, key);
  if(!found)
  {
    uishell_tool_strip_tooltip(sig, uishell_sidebar_string(entity.id));
    return;
  }
  uishell_sidebar_card_source(state, node, sig, str8_zero(), 0);
  AndamentoDetailAction primary = {0};
  if(ui_clicked(sig) && andamento_snapshot_detail_action(state->snapshot, index, 0, &primary))
  { uishell_sidebar_card_queue_action(state, node, primary.action); }
}

internal void
uishell_tool_strip_set_size(String8 name, U64 size)
{
  CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
  CFG_Node *node = cfg_node_child_from_string_or_alloc(rd_state->cfg, user, name);
  cfg_node_new_replacef(rd_state->cfg, node, "%I64u", size);
}

// A section's size after dragging its divider `delta` pixels from where a
// section of `start` items ended: the nearest whole item, within the setting's range.
internal U64
uishell_tool_strip_size_from_drag(U64 start, F32 delta, F32 item_h)
{
  F32 items = round_f32(((F32)start*item_h + delta)/item_h);
  return (U64)Clamp(0.f, items, (F32)UISHELL_TOOL_STRIP_SECTION_MAX);
}

// The divider below section `section`: dragging it resizes the section, in
// whole items, from the `shown` items it shows (fewer than its setting asks
// for when the window is short); a double click restores its default.
internal void
uishell_tool_strip_divider(Rng2F32 rect, UIShell_StripSection section, U64 size, U64 shown)
{
  F32 item_h = uishell_tool_strip_item_height();
  UI_Rect(r2f32p(rect.x0+4, floor_f32((rect.y0+rect.y1)/2), rect.x1-4, floor_f32((rect.y0+rect.y1)/2)+1))
    UI_BackgroundColor(ui_color_from_name(str8_lit("border")))
  { ui_build_box_from_key(UI_BoxFlag_DrawBackground, ui_key_zero()); }
  UI_Rect(rect)
  {
    ui_set_next_hover_cursor(WM_Cursor_UpDown);
    UI_Box *box = ui_build_box_from_stringf(UI_BoxFlag_Clickable, "###tool_strip_divider_%i", section);
    UI_Signal sig = ui_signal_from_box(box);
    String8 name = uishell_tool_strip_size_settings[section];
    if(ui_double_clicked(sig))
    {
      ui_kill_action();
      CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
      cfg_node_release(rd_state->cfg, cfg_node_child_from_string(user, name));
    }
    else if(ui_pressed(sig))
    {
      U64 start = Min(size, shown);
      ui_store_drag_struct(&start);
    }
    else if(ui_dragging(sig))
    {
      U64 next = uishell_tool_strip_size_from_drag(*ui_get_drag_struct(U64), ui_drag_delta().y, item_h);
      if(next != size) { uishell_tool_strip_set_size(name, next); }
    }
  }
}

internal void
uishell_tool_strip_ui(RD_WindowState *ws, Rng2F32 rect, UIShell_ControlledSplit *split)
{
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  state->strip_rect = rect;
  if(dim_2f32(rect).x <= 0 || dim_2f32(rect).y <= 0) { return; }
  Temp scratch = scratch_begin(0, 0);
  UIShell_MaterializedWorkspace *workspace = split->inventory.selected;
  CFG_Node *owner = workspace ? cfg_node_from_id(workspace->id) : &cfg_nil_node;

  // What each section lists.
  AndamentoEntity subject = owner != &cfg_nil_node ? uishell_store_subject(scratch.arena, owner) : (AndamentoEntity){0};
  UIShell_WorkspaceLayouts layouts = owner != &cfg_nil_node ? uishell_store_layouts(scratch.arena, owner) : (UIShell_WorkspaceLayouts){0};
  UIShell_StripViews views = owner != &cfg_nil_node ? uishell_store_strip_views(scratch.arena, owner) : (UIShell_StripViews){0};
  UIShell_StripEntries entries = owner != &cfg_nil_node ? uishell_store_strip_entries(scratch.arena, owner) : (UIShell_StripEntries){0};
  U64 counts[UIShell_StripSection_COUNT] = {subject.kind.len != 0, layouts.count, views.count};
  for(U64 i = 0; i < entries.count; i++)
  { counts[entries.v[i].section == ANDAMENTO_STRIP_RELATED ? UIShell_StripSection_Related : UIShell_StripSection_Linked] += 1; }
  UIShell_StripEntry *related = push_array(scratch.arena, UIShell_StripEntry, entries.count+1);
  UIShell_StripEntry *linked = push_array(scratch.arena, UIShell_StripEntry, entries.count+1);
  {
    U64 r = 0, l = 0;
    for(U64 i = 0; i < entries.count; i++)
    {
      if(entries.v[i].section == ANDAMENTO_STRIP_RELATED) { related[r++] = entries.v[i]; }
      else { linked[l++] = entries.v[i]; }
    }
  }

  F32 border = floor_f32(Clamp(0.f, rd_setting_f32_from_name(str8_lit("panel_border_px")), 4.f));
  F32 item_h = uishell_tool_strip_item_height(), divider_h = uishell_tool_strip_divider_height();
  F32 pad = floor_f32(ui_top_font_size()*0.3f);
  Rng1F32 item_x = r1f32(rect.x0+pad, rect.x1-pad-border);
  F32 y = rect.y0 + pad;
  // Each section keeps its area, empty or not, so items stay put as Views
  // open and close and as the Visible Workspace changes.
  U64 sizes[UIShell_StripSection_COUNT] = {0};
  for(U64 i = 0; i < ArrayCount(uishell_tool_strip_size_settings); i++)
  { sizes[i] = rd_setting_u64_from_name(uishell_tool_strip_size_settings[i]); }
  U64 shown_above = 0;
  for(UIShell_StripSection section = 0; section < UIShell_StripSection_COUNT; section++)
  {
    U64 count = counts[section];
    B32 last = section == UIShell_StripSection_Linked;
    // The divider above each section after the first resizes the one above.
    if(section != 0)
    {
      uishell_tool_strip_divider(r2f32p(rect.x0, y, rect.x1-border, y+divider_h), section-1, sizes[section-1], shown_above);
      y += divider_h;
    }
    F32 height = Max(0.f, rect.y1-pad-y);
    if(!last) { height = Min((F32)sizes[section]*item_h, height); }
    U64 shown = (U64)(height/item_h);
    shown_above = shown;
    Rng2F32 section_rect = r2f32p(rect.x0, y, rect.x1-border, y+height);
    y += height;
    if(height <= 0) { continue; }

    // Overflow scrolls by whole items.
    U64 *scroll = &state->strip_scroll[section];
    U64 max_scroll = count > shown ? count-shown : 0;
    *scroll = Min(*scroll, max_scroll);
    UI_Box *clip = &ui_nil_box;
    UI_Rect(section_rect)
    { clip = ui_build_box_from_stringf(UI_BoxFlag_Clip|UI_BoxFlag_Scroll|UI_BoxFlag_Clickable, "###tool_strip_section_%i", section); }
    UI_Parent(clip)
    {
      for(U64 i = *scroll; i < count && i < *scroll+shown+1; i++)
      {
        F32 top = (F32)(i-*scroll)*item_h;
        Rng2F32 r = r2f32p(item_x.min-section_rect.x0, top+1, item_x.max-section_rect.x0, top+item_h-1);
        UI_Key key = ui_key_from_stringf(ui_key_zero(), "###tool_strip_item_%i_%I64u", section, i);
        switch(section)
        {
          case UIShell_StripSection_Subject:
          {
            AndamentoNode node = {0};
            U64 index = uishell_sidebar_card_find(state, subject, &node);
            UI_Signal sig = uishell_tool_strip_item(r, rd_icon_kind_text_table[uishell_sidebar_card_icon(subject.kind)], 1, 0, 0, key);
            if(index != ANDAMENTO_NONE) { uishell_sidebar_card_source(state, node, sig, str8_zero(), 1); }
            // Its row in the sidebar, which opens to show it.
            if(ui_clicked(sig))
            {
              state->reveal_workspace_id = workspace->id;
              CFG_Node *collapsed = cfg_node_child_from_string(split->owner_cfg, str8_lit("control_split_collapsed"));
              if(collapsed != &cfg_nil_node) { cfg_node_release(rd_state->cfg, collapsed); }
              ws->workspace_zoom_open = 0;
              rd_request_frame();
            }
          }break;
          case UIShell_StripSection_Layouts:
          {
            String8 name = layouts.names[i], glyph = layouts.glyphs[i];
            if(!glyph.size && name.size) { glyph = str8_prefix(name, utf8_decode(name.str, name.size).inc); }
            if(!glyph.size) { glyph = str8_lit("?"); }
            UI_Signal sig = uishell_tool_strip_item(r, glyph, 0, i == layouts.current, 0, key);
            String8 tip = i == layouts.fallback ? push_str8f(scratch.arena, "%S (default)", name) : name;
            if(i == layouts.current && layouts.owned) { tip = push_str8f(scratch.arena, "%S, rearranged", tip); }
            uishell_tool_strip_tooltip(sig, tip);
            if(ui_clicked(sig) && i != layouts.current) { uishell_store_choose_layout(owner, name); }
            UI_Key menu = ui_key_from_stringf(key, "menu");
            if(ui_right_clicked(sig)) { ui_ctx_menu_open(menu, sig.box->key, v2f32(dim_2f32(r).x, 0)); }
            UI_CtxMenu(menu) RD_Font(RD_FontSlot_Main) UI_PrefWidth(ui_em(14, 1))
            {
              if(ui_clicked(ui_button(str8_lit("Show this layout")))) { uishell_store_choose_layout(owner, name); ui_ctx_menu_close(); }
              if(ui_clicked(ui_button(str8_lit("Show the default layout")))) { uishell_store_choose_layout(owner, str8_zero()); ui_ctx_menu_close(); }
              if(i == layouts.current && layouts.owned && ui_clicked(ui_button(str8_lit("Reset layout"))))
              { uishell_store_reset_layout(owner); ui_ctx_menu_close(); }
            }
          }break;
          case UIShell_StripSection_Views:
          {
            UIShell_StripView *view = &views.v[i];
            B32 placed = view->view != &cfg_nil_node;
            RD_ViewAddress address = placed ? rd_view_address_from_cfg(view->view) : rd_nil_view_address;
            B32 selected = placed && rd_view_is_selected(address);
            UI_Signal sig = uishell_tool_strip_item(r, uishell_tool_strip_view_icon(view), 1, selected, !placed, key);
            String8 title = uishell_store_slot_title(view->key);
            uishell_tool_strip_tooltip(sig, placed ? title : push_str8f(scratch.arena, "%S (not shown: click to show)", title));
            if(ui_clicked(sig))
            {
              if(placed)
              {
                UIShell_RegsScope(.window = ws->cfg_id, .panel = rd_panel_from_view(address), .view = address, .tab = address)
                { uishell_cmd("focus_tab"); }
              }
              else { uishell_store_show_slot(owner, 0, view->key); }
            }
          }break;
          case UIShell_StripSection_Related: { uishell_tool_strip_entity(state, r, related[i].entity, key); }break;
          case UIShell_StripSection_Linked: { uishell_tool_strip_entity(state, r, linked[i].entity, key); }break;
          default:{}break;
        }
      }
    }
    // After its items, so they take the hover first.
    UI_Signal wheel = ui_signal_from_box(clip);
    if(wheel.scroll.y > 0) { *scroll = Min(*scroll+1, max_scroll); }
    else if(wheel.scroll.y < 0 && *scroll > 0) { *scroll -= 1; }
  }

  // The strip's seam with the workspace, and its background: built last, as
  // siblings draw last-built first.
  if(border >= 1.f)
  {
    UI_Rect(r2f32p(rect.x1-border, rect.y0, rect.x1, rect.y1)) UI_BackgroundColor(ui_color_from_name(str8_lit("border")))
    { ui_build_box_from_key(UI_BoxFlag_DrawBackground, ui_key_zero()); }
  }
  UI_Rect(rect) UI_BackgroundColor(ui_color_from_name(str8_lit("background")))
  { ui_build_box_from_stringf(UI_BoxFlag_DrawBackground|UI_BoxFlag_Clickable, "###tool_strip_%I64u", split->owner_cfg->id); }
  scratch_end(scratch);
}
