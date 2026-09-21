// Native andamento control surface. Included after the workspace helpers.
#include "andamento.h"
#include "ingress/ingress.h"
global WheelhouseIngress *uishell_ingress;
global String8 uishell_sidebar_live_config;
global B32 uishell_sidebar_live;
global U64 uishell_sidebar_last_tick;
// The first resource is primary. This descriptor is local fixture input, not
// an andamento wire format. Persist IDs on tabs independently of their labels.
typedef struct UIShell_SidebarResource UIShell_SidebarResource;
struct UIShell_SidebarResource
{
  char *id;
  char *label;
  char *command;
  char *windows_command;
};
#include "uishell/generated/sidebar_fixture.h"

typedef struct UIShell_SidebarSection UIShell_SidebarSection;
struct UIShell_SidebarSection
{
  UIShell_SidebarSection *next;
  String8 key;
  F32 height_px;
  B32 collapsed;
};

struct UIShell_SidebarState
{
  UIShell_SidebarSection *sections;
  Andamento *core;
  AndamentoSnapshot *snapshot;
  U64 topology_hash;
  B32 initialized;
  B32 restored;
  U8 error[512];
  U8 inspection[2048];
};

internal B32
uishell_sidebar_uses_andamento(CFG_Node *owner)
{
  CFG_Node *setting = cfg_node_child_from_string(owner, str8_lit("sidebar_mode"));
  return str8_match(setting->first->string, str8_lit("andamento"), 0) ||
         (setting == &cfg_nil_node && uishell_sidebar_live);
}

internal AndamentoText
uishell_sidebar_text(String8 s)
{
  AndamentoText result = {s.str, s.size};
  return result;
}

internal String8
uishell_sidebar_string(AndamentoText s)
{
  return str8((U8 *)s.data, s.len);
}

internal B32
uishell_sidebar_result(UIShell_SidebarState *state, B32 ok, char *error)
{
  if(!ok)
  {
    String8 message = error ? str8_cstring(error) : str8_lit("Sidebar operation failed");
    U64 size = Min(message.size, sizeof(state->error)-1);
    MemoryCopy(state->error, message.str, size);
    state->error[size] = 0;
  }
  andamento_string_free(error);
  return ok;
}

internal void
uishell_sidebar_release(UIShell_SidebarState *state)
{
  if(state != 0)
  {
    andamento_snapshot_release(state->snapshot);
    andamento_destroy(state->core);
  }
}

internal void
uishell_sidebar_refresh(UIShell_SidebarState *state)
{
  char *error = 0;
  B32 current = andamento_snapshot_is_current(state->core, state->snapshot, &error);
  if(error != 0) { uishell_sidebar_result(state, 0, error); return; }
  if(current) { return; }
  AndamentoSnapshot *next = andamento_snapshot_acquire(state->core, &error);
  if(uishell_sidebar_result(state, next != 0, error))
  {
    andamento_snapshot_release(state->snapshot);
    state->snapshot = next;
  }
}

internal void
uishell_sidebar_observe(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  Temp scratch = scratch_begin(0, 0);
  AndamentoWorkspace *items = push_array(scratch.arena, AndamentoWorkspace, split->inventory.count);
  U64 hash = 5381, count = 0;
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
  {
    items[count] = (AndamentoWorkspace){w->id, count, uishell_sidebar_text(w->display_name), w == split->inventory.selected};
    hash = hash*33 + w->id;
    hash = hash*33 + items[count].selected;
    for(U64 i = 0; i < w->display_name.size; i++) { hash = hash*33 + w->display_name.str[i]; }
    count++;
  }
  if(hash != state->topology_hash)
  {
    char *error = 0;
    B32 ok = andamento_observe(state->core, items, count, 0, 0, &error);
    if(uishell_sidebar_result(state, ok, error))
    {
      state->topology_hash = hash;
      uishell_sidebar_refresh(state);
      rd_request_frame();
    }
  }
  scratch_end(scratch);
}

internal UIShell_SidebarState *
uishell_sidebar_init(RD_WindowState *ws)
{
  if(ws->sidebar == 0) { ws->sidebar = push_array(ws->arena, UIShell_SidebarState, 1); }
  UIShell_SidebarState *state = ws->sidebar;
  if(!state->initialized)
  {
    state->initialized = 1;
    char *error = 0;
    if(andamento_abi_version() != 2)
    {
      uishell_sidebar_result(state, 0, 0);
      return state;
    }
    String8 config = uishell_sidebar_live ? uishell_sidebar_live_config : str8_cstring((char *)uishell_sidebar_fixture_config);
    state->core = andamento_create(config.str, config.size, &error);
    if(uishell_sidebar_result(state, state->core != 0, error))
    {
      String8 patches = uishell_sidebar_live ? str8_zero() : str8_cstring((char *)uishell_sidebar_fixture_patches);
      for(U64 start = 0; start < patches.size;)
      {
        U64 end = start;
        while(end < patches.size && patches.str[end] != '\n') { end++; }
        if(end > start)
        {
          error = 0;
          B32 ok = andamento_apply_patch_json(state->core, 0, uishell_sidebar_text(str8(patches.str+start, end-start)), &error);
          if(!uishell_sidebar_result(state, ok, error)) { break; }
        }
        start = end+1;
      }
#if OS_WINDOWS
      if(!uishell_sidebar_live)
      {
      AndamentoFact recipe = {0};
      recipe.key = uishell_sidebar_text(str8_lit("action.primary.recipe"));
      recipe.kind = ANDAMENTO_FACT_TEXT;
      recipe.text = uishell_sidebar_text(str8_lit("cmd.exe /K echo Wheelhouse sidebar fixture"));
      error = 0;
      B32 ok = andamento_apply_entity(state->core, 0, uishell_sidebar_text(str8_lit("vessel")), uishell_sidebar_text(str8_lit("v")), uishell_sidebar_text(str8_lit("fixture")), &recipe, 1, &error);
      uishell_sidebar_result(state, ok, error);
      }
#endif
      uishell_sidebar_refresh(state);
    }
  }
  return state;
}

// Populate ordinary workspace configuration once. Focus and restoration reuse
// that configuration, including any panel moves and tab selections by the user.
internal void
uishell_sidebar_populate(CFG_Node *workspace, const UIShell_SidebarResource *resources, U64 count, String8 cwd)
{
  CFG_Node *panels = cfg_node_new(rd_state->cfg, workspace, str8_lit("panels"));
  CFG_Node *primary = panels, *overflow = panels;
  if(count > 1)
  {
    cfg_node_new(rd_state->cfg, workspace, str8_lit("split_x"));
    primary = cfg_node_new(rd_state->cfg, panels, str8_lit("0.6"));
    overflow = cfg_node_new(rd_state->cfg, panels, str8_lit("0.4"));
  }
  cfg_node_new(rd_state->cfg, primary, str8_lit("selected"));
  for(U64 i = 0; i < count; i++)
  {
    const UIShell_SidebarResource *resource = &resources[i];
    char *command = resource->command;
#if OS_WINDOWS
    command = resource->windows_command;
#endif
    CFG_Node *tab = rd_cfg_new_view_tab(i == 0 ? primary : overflow, str8_lit("terminal"), str8_cstring(command), i <= 1);
    CFG_Node *id = cfg_node_new(rd_state->cfg, tab, str8_lit("resource_id"));
    cfg_node_new(rd_state->cfg, id, str8_cstring(resource->id));
    CFG_Node *label = cfg_node_new(rd_state->cfg, tab, str8_lit("label"));
    cfg_node_new(rd_state->cfg, label, str8_cstring(resource->label));
    if(cwd.size)
    {
      CFG_Node *dir = cfg_node_new(rd_state->cfg, tab, str8_lit("cwd"));
      cfg_node_new(rd_state->cfg, dir, cwd);
    }
  }
}

// Effects change the real config tree. Bind identity before the terminal view
// can start, and acknowledge creation before the next topology observation.
internal void
uishell_sidebar_effects(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  char *error = 0;
  AndamentoEffects *effects = andamento_effects_take(state->core, &error);
  if(!uishell_sidebar_result(state, effects != 0, error)) { return; }
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  for(U64 i = 0; i < andamento_effects_count(effects); i++)
  {
    AndamentoEffect effect = {0};
    if(!andamento_effects_get(effects, i, &effect)) { continue; }
    CFG_Node *workspace = &cfg_nil_node;
    uint32_t outcome = ANDAMENTO_COMPLETE_ERROR;
    String8 failure = str8_lit("Workspace is no longer available");
    if(effect.kind == ANDAMENTO_EFFECT_MATERIALIZE)
    {
      // Reuse a previously created fixture workspace after switching modes or
      // restarting the app; its persisted identity is independent of its label.
      for(CFG_Node *c = split->owner_cfg->first; c != &cfg_nil_node; c = c->next)
      {
        if(str8_match(c->string, str8_lit("workspace"), 0) &&
           str8_match(cfg_node_child_from_string(c, str8_lit("sidebar_entity_kind"))->first->string, uishell_sidebar_string(effect.entity_kind), 0) &&
           str8_match(cfg_node_child_from_string(c, str8_lit("sidebar_entity_id"))->first->string, uishell_sidebar_string(effect.entity_id), 0))
        { workspace = c; break; }
      }
      if(workspace == &cfg_nil_node && effect.recipe.len != 0)
      {
        workspace = cfg_node_new(rd_state->cfg, split->owner_cfg, str8_lit("workspace"));
        CFG_Node *label = cfg_node_new(rd_state->cfg, workspace, str8_lit("label"));
        cfg_node_new(rd_state->cfg, label, uishell_sidebar_string(effect.name));
        CFG_Node *kind = cfg_node_new(rd_state->cfg, workspace, str8_lit("sidebar_entity_kind"));
        cfg_node_new(rd_state->cfg, kind, uishell_sidebar_string(effect.entity_kind));
        CFG_Node *id = cfg_node_new(rd_state->cfg, workspace, str8_lit("sidebar_entity_id"));
        cfg_node_new(rd_state->cfg, id, uishell_sidebar_string(effect.entity_id));
        String8 cwd = effect.has_cwd ? uishell_sidebar_string(effect.cwd) : str8_zero();
        if(!uishell_sidebar_live && str8_match(uishell_sidebar_string(effect.entity_kind), str8_lit("vessel"), 0) &&
           str8_match(uishell_sidebar_string(effect.entity_id), str8_lit("multi"), 0))
        {
          uishell_sidebar_populate(workspace, uishell_sidebar_fixture_resources,
                                   ArrayCount(uishell_sidebar_fixture_resources), cwd);
        }
        else
        {
          Temp scratch = scratch_begin(0, 0);
          String8 command = uishell_sidebar_string(effect.recipe);
          U8 *command_z = push_array(scratch.arena, U8, command.size+1);
          MemoryCopy(command_z, command.str, command.size);
          UIShell_SidebarResource resource = {"primary", "Terminal", (char *)command_z, (char *)command_z};
          uishell_sidebar_populate(workspace, &resource, 1, cwd);
          scratch_end(scratch);
        }
      }
      failure = str8_lit("No terminal command is available for this entry");
      if(workspace != &cfg_nil_node) { outcome = ANDAMENTO_COMPLETE_MATERIALIZE; }
    }
    else if(effect.kind == ANDAMENTO_EFFECT_FOCUS)
    {
      for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
      { if(w->id == effect.workspace_id) { workspace = w->mount.owner_cfg; break; } }
      if(workspace != &cfg_nil_node) { outcome = ANDAMENTO_COMPLETE_FOCUS; }
    }
    else if(effect.kind == ANDAMENTO_EFFECT_INSPECT)
    {
      Temp scratch = scratch_begin(0, 0);
      String8 message = push_str8f(scratch.arena, "%S: %S", uishell_sidebar_string(effect.entity_kind), uishell_sidebar_string(effect.entity_id));
      U64 size = Min(message.size, sizeof(state->inspection)-1);
      MemoryCopy(state->inspection, message.str, size);
      state->inspection[size] = 0;
      scratch_end(scratch);
      continue;
    }
    if(workspace != &cfg_nil_node)
    {
      ws->root_controlled_split_initialized = 1;
      ws->root_controlled_split_selected_workspace_id = workspace->id;
      ws->window_layout_reset = 1;
    }
    error = 0;
    B32 ok = andamento_complete(state->core, effect.request_id, outcome, workspace == &cfg_nil_node ? 0 : workspace->id, uishell_sidebar_text(failure), &error);
    uishell_sidebar_result(state, ok, error);
  }
  andamento_effects_release(effects);
}

// Rebind persisted fixture identities without launching their recipes again.
internal void
uishell_sidebar_restore(UIShell_SidebarState *state, UIShell_ControlledSplit *split)
{
  if(state->restored || state->snapshot == 0) { return; }
  state->restored = 1;
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  CFG_ID selected = ws->root_controlled_split_selected_workspace_id;
  for(UIShell_MaterializedWorkspace *w = split->inventory.first; w; w = w->next)
  {
    String8 kind = cfg_node_child_from_string(w->mount.owner_cfg, str8_lit("sidebar_entity_kind"))->first->string;
    String8 id = cfg_node_child_from_string(w->mount.owner_cfg, str8_lit("sidebar_entity_id"))->first->string;
    if(kind.size == 0 || id.size == 0) { continue; }
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
      if(node.state != ANDAMENTO_LIVE && node.activate != ANDAMENTO_NONE &&
         str8_match(kind, uishell_sidebar_string(node.entity_kind), 0) &&
         str8_match(id, uishell_sidebar_string(node.entity_id), 0))
      {
        char *error = 0;
        B32 ok = andamento_dispatch(state->core, state->snapshot, node.activate, &error);
        if(uishell_sidebar_result(state, ok, error)) { uishell_sidebar_effects(state, split); }
        uishell_sidebar_refresh(state);
        break;
      }
    }
  }
  ws->root_controlled_split_selected_workspace_id = selected;
  uishell_sidebar_observe(state, split);
}

// Interactive compact-sidebar prototype; see docs/design/andamento-sidebar-prototype.md.
// Borderless controls retain the toolkit's keyboard activation and focus cues.
internal UI_Signal
uishell_sidebar_button(String8 text)
{
  UI_Box *box = ui_build_box_from_string(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText|
    UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects, text);
  return ui_signal_from_box(box);
}

// Use the shell's icon-font expander, not a text-font '>' in a narrow label.
// Padding and truncation are inappropriate for this fixed-size glyph.
internal UI_Signal
uishell_sidebar_disclosure(B32 expanded, String8 key)
{
  UI_Signal result = {0};
  UI_PrefWidth(ui_em(1.5f, 1)) UI_TextPadding(0)
  {
    result = ui_expander(expanded, key);
    result.box->flags |= UI_BoxFlag_DisableTextTrunc;
  }
  return result;
}

internal void
uishell_sidebar_ui(Rng2F32 rect, UIShell_ControlledSplit *split)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  uishell_sidebar_restore(state, split);
  size_t action = ANDAMENTO_NONE;
  Temp scratch = scratch_begin(0, 0);
  F32 em = ui_top_font_size(), row_height = floor_f32(em*1.65f);
  Vec2F32 dim = dim_2f32(rect);
  UI_Box *root;
  UI_Focus(UI_FocusKind_On) UI_Rect(rect)
  {
    root = ui_build_box_from_string(UI_BoxFlag_DrawBackground|UI_BoxFlag_Clip|
      UI_BoxFlag_DefaultFocusNav, str8_lit("###andamento_sidebar"));
  }
  U64 count = state->snapshot ? andamento_snapshot_node_count(state->snapshot) : 0;
  AndamentoNode *nodes = push_array(scratch.arena, AndamentoNode, count);
  B32 *hidden = push_array(scratch.arena, B32, count);
  U64 *depth = push_array(scratch.arena, U64, count);
  U64 *sections = push_array(scratch.arena, U64, count);
  U64 section_count = 0;
  for(U64 i = 0; i < count; i++)
  {
    andamento_snapshot_node(state->snapshot, i, &nodes[i]);
    if(nodes[i].is_section) { sections[section_count++] = i; }
    if(nodes[i].parent != ANDAMENTO_NONE)
    {
      U64 parent = nodes[i].parent;
      hidden[i] = hidden[parent] || (!nodes[parent].is_section && nodes[parent].collapsed);
      depth[i] = depth[parent] + !nodes[parent].is_section;
    }
  }
  UIShell_SidebarSection **states = push_array(scratch.arena, UIShell_SidebarSection *, section_count);
  F32 *heights = push_array(scratch.arena, F32, section_count);
  U64 *rows = push_array(scratch.arena, U64, section_count);
  U64 *entries = push_array(scratch.arena, U64, section_count);
  AndamentoText diagnostic = {0};
  if(state->snapshot && andamento_snapshot_diagnostic_count(state->snapshot))
  { andamento_snapshot_diagnostic(state->snapshot, 0, &diagnostic); }
  F32 footer = (state->error[0] || state->inspection[0] || diagnostic.len) ? row_height*2 : 0;
  F32 available = Max(0.f, dim.y-footer-section_count*row_height);
  // Template controls share one fixed row at the bottom of the sidebar.
  B32 has_controls = 0;
  for(U64 n = 0; n < section_count; n++)
  { has_controls |= nodes[sections[n]].control_count != 0; }
  F32 controls_height = has_controls ? row_height : 0;
  available = Max(0.f, available-controls_height);
  U64 flexible = section_count;
  for(U64 n = 0; n < section_count; n++)
  {
    String8 key = uishell_sidebar_string(nodes[sections[n]].key);
    UIShell_SidebarSection *section = state->sections;
    for(; section; section = section->next) { if(str8_match(section->key, key, 0)) { break; } }
    if(!section)
    {
      section = push_array(ws->arena, UIShell_SidebarSection, 1);
      section->key = push_str8_copy(ws->arena, key);
      section->next = state->sections;
      state->sections = section;
    }
    states[n] = section;
    U64 end = n+1 < section_count ? sections[n+1] : count;
    for(U64 i = sections[n]; i < end; i++)
    {
      if(nodes[i].parent == sections[n]) { entries[n]++; }
      if(hidden[i]) { continue; }
      rows[n] += !nodes[i].is_section;
      for(U64 c = 0; !nodes[i].is_section && c < nodes[i].control_count; c++)
      {
        AndamentoControl control = {0};
        if(andamento_snapshot_control(state->snapshot, nodes[i].first_control+c, &control) && control.action != ANDAMENTO_NONE) { rows[n]++; }
      }
    }
    if(!section->collapsed && rows[n] && flexible == section_count) { flexible = n; }
  }
  // Secondary sections have a bounded body; the first expanded section fills
  // the remainder. Headers stay outside all scrolling content.
  // Empty sections have no useful controls or content to reveal.
  for(U64 n = 0; n < section_count; n++)
  { if(!rows[n] && !nodes[sections[n]].control_count) { available += row_height; } }
  F32 remaining = available;
  for(U64 n = 0; n < section_count; n++)
  {
    if(n == flexible || states[n]->collapsed || !rows[n]) { continue; }
    F32 wanted = states[n]->height_px > 0 ? states[n]->height_px : Min(rows[n], 7)*row_height;
    heights[n] = Min(wanted, remaining*0.4f);
    remaining -= heights[n];
  }
  if(flexible < section_count) { heights[flexible] = remaining; }
  F32 y = 0;
  UI_Parent(root)
  {
    for(U64 n = 0; n < section_count; n++)
    {
      AndamentoNode *section_node = &nodes[sections[n]];
      if(!rows[n] && !section_node->control_count) { continue; }
      String8 key = uishell_sidebar_string(section_node->key);
      String8 title = uishell_sidebar_string(section_node->label);
      if(section_node->field_count)
      {
        AndamentoField field = {0};
        andamento_snapshot_field(state->snapshot, section_node->first_field, &field);
        title = uishell_sidebar_string(field.text);
      }
      UI_Box *header;
      UI_Rect(r2f32p(0, y+(n != flexible && heights[n] > 0 ? 6.f : 0.f), dim.x, y+row_height)) UI_ChildLayoutAxis(Axis2_X)
      { header = ui_build_box_from_stringf(0, "###section_header_%S", key); }
      UI_Parent(header) UI_PrefHeight(ui_pct(1, 1))
      {
        B32 toggle = 0;
        ui_spacer(ui_em(0.3f, 1));
        toggle |= ui_clicked(uishell_sidebar_disclosure(!states[n]->collapsed, push_str8f(scratch.arena, "###section_toggle_%S", key)));
        UI_PrefWidth(ui_pct(1, 0))
        { toggle |= ui_clicked(uishell_sidebar_button(push_str8f(scratch.arena, "%S  (%I64u)###section_%S", title, entries[n], key))); }
        if(toggle) { states[n]->collapsed = !states[n]->collapsed; }
      }
      y += row_height;
      if(heights[n] <= 0) { continue; }
      // The top edge of a secondary body resizes its allocation without
      // changing another section's scroll position or core state.
      if(n != flexible)
      {
        UI_Rect(r2f32p(0, y-row_height, dim.x, y-row_height+6.f)) UI_HoverCursor(WM_Cursor_UpDown)
        {
          UI_Box *divider = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawSideTop, "###resize_%S", key);
          UI_Signal sig = ui_signal_from_box(divider);
          if(ui_dragging(sig))
          {
            if(ui_pressed(sig)) { F32 initial = heights[n]; ui_store_drag_struct(&initial); }
            states[n]->height_px = Clamp(row_height, *ui_get_drag_struct(F32)-ui_drag_delta().y, Max(row_height, available*0.7f));
          }
        }
      }
      UI_ScrollRegionParams params = ui_scroll_region_params(r2f32p(0, y, dim.x, y+heights[n]),
        UI_ScrollAxisPolicy_Off, UI_ScrollAxisPolicy_Auto);
      params.content_dim_px = v2f32(0, rows[n]*row_height);
      UI_ScrollRegion region = ui_scroll_region_layout(params);
      UI_Key content_key = ui_key_from_stringf(root->key, "section_body_%S", key);
      UI_Box *previous = ui_box_from_key(content_key);
      UI_ScrollRegionAxis axes[Axis2_COUNT] = {0};
      axes[Axis2_Y].range = r1s64(0, Max(0, (S64)(rows[n]*row_height-heights[n])));
      axes[Axis2_Y].visible = (S64)heights[n];
      axes[Axis2_Y].position.idx = clamp_1s64(axes[Axis2_Y].range, (S64)previous->view_off_target.y);
      UI_ScrollRegionSignal scroll = ui_scroll_region_build(root, content_key, &region, axes,
        UI_BoxFlag_ViewScrollY|UI_BoxFlag_ViewClamp|UI_BoxFlag_AllowOverflowY);
      UI_Box *body = scroll.content_box;
      body->view_off_target.y = (F32)scroll.position.y.idx;
      body->view_off.y = Clamp(0.f, body->view_off.y, (F32)axes[Axis2_Y].range.max);
      body->child_layout_axis = Axis2_Y;
      UI_Parent(body) UI_PrefWidth(ui_pct(1, 0)) UI_PrefHeight(ui_px(row_height, 1))
      {
        U64 end = n+1 < section_count ? sections[n+1] : count;
        for(U64 i = sections[n]; i < end; i++)
        {
          if(hidden[i]) { continue; }
          AndamentoNode node = nodes[i];
          B32 children = i+1 < count && nodes[i+1].parent == i;
          B32 can_activate = node.activate != ANDAMENTO_NONE && (node.openable || node.state == ANDAMENTO_LIVE);
          String8 node_key = uishell_sidebar_string(node.key);
          String8 full_label = uishell_sidebar_string(node.label);
          String8 label = full_label;
          String8 kind = uishell_sidebar_string(node.entity_kind);
          String8 status = str8_zero();
          String8 context = str8_zero();
          for(U64 f = 0; f < node.field_count; f++)
          {
            AndamentoField field = {0};
            andamento_snapshot_field(state->snapshot, node.first_field+f, &field);
            String8 value = uishell_sidebar_string(field.text);
            // Native templates declare the display label first. The core may
            // abbreviate it; node.label remains the full hover/inspection text.
            if(f == 0 && value.size) { label = value; }
            if(!str8_match(value, label, 0) && !str8_match(value, kind, 0) && status.size == 0) { status = value; }
            // Native Attention templates append context identities after the
            // label/kind/state fields. Match identities without parsing them.
            if(f >= 3 && value.size)
            {
              for(U64 j = 0; j < count; j++)
              {
                if(!nodes[j].is_section && str8_match(value, uishell_sidebar_string(nodes[j].entity_id), 0))
                { value = uishell_sidebar_string(nodes[j].label); break; }
              }
              if(!str8_match(value, label, 0))
              { context = context.size ? push_str8f(scratch.arena, "%S / %S", context, value) : value; }
            }
          }
          if(!node.is_section) UI_TagF(node.selected ? "tab" : "")
          {
            // Supply drawing flags at construction so the toolkit resolves the
            // selected row's theme colours, even while the terminal has focus.
            UI_Box *row;
            UI_ChildLayoutAxis(Axis2_X)
            { row = ui_build_box_from_stringf(node.selected ? UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder : 0, "###sidebar_row_%S", node_key); }
            UI_Parent(row)
            {
              ui_spacer(ui_em(0.3f+depth[i]*0.8f, 1));
              UI_PrefWidth(ui_em(1.5f, 1))
              {
                if(children && node.toggle != ANDAMENTO_NONE)
                {
                  if(ui_clicked(uishell_sidebar_disclosure(!node.collapsed, push_str8f(scratch.arena, "###toggle_%S", node_key)))) { action = node.toggle; }
                }
                else { ui_spacer(ui_em(1.5f, 1)); }
              }
              UI_TagF("weak") UI_PrefWidth(ui_em(1.5f, 1)) UI_TextPadding(0) UI_TextAlignment(UI_TextAlign_Center) RD_Font(RD_FontSlot_Icons)
              {
                RD_IconKind icon = RD_IconKind_FileOutline;
                if(str8_match(kind, str8_lit("project"), 0)) { icon = RD_IconKind_FolderClosedOutline; }
                else if(str8_match(kind, str8_lit("convoy"), 0)) { icon = RD_IconKind_Threads; }
                else if(str8_match(kind, str8_lit("vessel"), 0) || str8_match(kind, str8_lit("session"), 0)) { icon = RD_IconKind_Machine; }
                ui_label(rd_icon_kind_text_table[icon]);
              }
              UI_PrefWidth(ui_pct(1, 0))
              {
                // Text remains available in the tooltip; terse marks distinguish
                // selected/open workspaces from producer activity state.
                String8 mark = node.state == ANDAMENTO_LIVE ? str8_lit("• ") : str8_zero();
                String8 display = context.size ? push_str8f(scratch.arena, "%S · %S", label, context) : label;
                UI_Signal sig = uishell_sidebar_button(push_str8f(scratch.arena, "%S%S###entry_%S", mark, display, node_key));
                // The rich tooltip already includes the full label. Do not also
                // enroll this row in the shell's automatic truncated-text hover.
                sig.box->flags |= UI_BoxFlag_DisableTruncatedHover;
                if(ui_clicked(sig))
                {
                  if(can_activate) { action = node.activate; }
                  else
                  {
                    String8 detail = push_str8f(scratch.arena, "%S (%S) — no opening recipe", full_label, kind);
                    U64 size = Min(detail.size, sizeof(state->inspection)-1);
                    MemoryCopy(state->inspection, detail.str, size); state->inspection[size] = 0;
                    rd_request_frame();
                  }
                }
                if(ui_hovering(sig))
                {
                  UI_Tooltip UI_PrefWidth(ui_text_dim(1, 1)) UI_PrefHeight(ui_em(1.6f, 1))
                  {
                    ui_labelf("%S · %S", full_label, kind);
                    if(context.size) { ui_label(context); }
                    for(U64 f = 0; f < node.field_count; f++)
                    {
                      AndamentoField field = {0}; andamento_snapshot_field(state->snapshot, node.first_field+f, &field);
                      String8 value = uishell_sidebar_string(field.text);
                      if(f < 3 && !str8_match(value, label, 0) && !str8_match(value, kind, 0)) { ui_label(value); }
                    }
                    ui_label(node.selected ? str8_lit("Current workspace") : can_activate ? (node.state == ANDAMENTO_LIVE ? str8_lit("Focus workspace") : str8_lit("Open workspace")) : str8_lit("No opening recipe available"));
                  }
                }
              }
              if(node.selected) UI_PrefWidth(ui_em(1.5f, 1)) UI_TextPadding(0) RD_Font(RD_FontSlot_Icons)
              { ui_label(rd_icon_kind_text_table[RD_IconKind_Check]); }
              UI_TagF("weak") UI_PrefWidth(ui_text_dim(0.6f, 1))
              {
                if(node.state == ANDAMENTO_OPENING) { ui_label(str8_lit("…")); }
                else if(str8_match(status, str8_lit("failed"), 0)) { ui_label(str8_lit("!")); }
                else if(str8_match(status, str8_lit("waiting"), 0)) { ui_label(str8_lit("◷")); }
                else if(str8_match(status, str8_lit("active"), 0)) { ui_label(str8_lit("●")); }
              }
            }
          }
          for(U64 c = 0; !node.is_section && c < node.control_count; c++)
          {
            AndamentoControl control = {0};
            if(andamento_snapshot_control(state->snapshot, node.first_control+c, &control) && control.action != ANDAMENTO_NONE)
            {
              if(ui_clicked(uishell_sidebar_button(push_str8f(scratch.arena, "%S###control_%S_%I64u", uishell_sidebar_string(control.label), node_key, c)))) { action = control.action; }
            }
          }
        }
      }
      // Children get first refusal; the viewport consumes the remaining wheel
      // input. Header and sibling viewport geometry are outside this box.
      ui_signal_from_box(body);
      y += heights[n];
    }
    if(has_controls)
    {
      UI_Box *toolbar;
      UI_Rect(r2f32p(0, dim.y-controls_height, dim.x, dim.y)) UI_ChildLayoutAxis(Axis2_X)
      { toolbar = ui_build_box_from_string(UI_BoxFlag_Clip|UI_BoxFlag_DrawSideTop, str8_lit("###sidebar_controls")); }
      UI_Parent(toolbar) UI_PrefHeight(ui_pct(1, 1)) UI_PrefWidth(ui_text_dim(1.2f, 1))
      {
        ui_spacer(ui_em(0.5f, 1));
        for(U64 n = 0; n < section_count; n++)
        {
          AndamentoNode *section_node = &nodes[sections[n]];
          String8 key = uishell_sidebar_string(section_node->key);
          for(U64 c = 0; c < section_node->control_count; c++)
          {
            AndamentoControl control = {0};
            if(andamento_snapshot_control(state->snapshot, section_node->first_control+c, &control) && control.action != ANDAMENTO_NONE)
            {
              B32 checked = control.value_kind == 1 && control.checked;
              UI_Box *button;
              UI_TagF("tab")
              {
                button = ui_build_box_from_stringf(UI_BoxFlag_Clickable|UI_BoxFlag_DrawText|
                  UI_BoxFlag_DrawHotEffects|UI_BoxFlag_DrawActiveEffects|UI_BoxFlag_DisableTruncatedHover|
                  (checked ? UI_BoxFlag_DrawBackground|UI_BoxFlag_DrawBorder : 0),
                  "%S###control_%S_%I64u", uishell_sidebar_string(control.label), key, c);
              }
              if(ui_clicked(ui_signal_from_box(button))) { action = control.action; }
              ui_spacer(ui_em(0.4f, 1));
            }
          }
        }
      }
    }
    if(footer)
    {
      UI_Rect(r2f32p(0, dim.y-controls_height-footer, dim.x, dim.y-controls_height-row_height))
      {
        ui_label(state->error[0] ? str8_cstring((char *)state->error) : state->inspection[0] ? str8_cstring((char *)state->inspection) : uishell_sidebar_string(diagnostic));
      }
      UI_Rect(r2f32p(0, dim.y-controls_height-row_height, dim.x, dim.y-controls_height))
      {
        if(state->error[0] || state->inspection[0])
        {
          if(ui_clicked(uishell_sidebar_button(str8_lit("Dismiss###sidebar_dismiss"))))
          { state->inspection[0] = state->error[0] = 0; rd_request_frame(); }
        }
        else { UI_TagF("weak") ui_label(str8_lit("Sidebar configuration diagnostic")); }
      }
    }
  }
  scratch_end(scratch);
  // Dispatch only against the snapshot used above, before topology mutations.
  if(state->core != 0)
  {
    if(action != ANDAMENTO_NONE)
    {
      char *error = 0;
      B32 ok = andamento_dispatch(state->core, state->snapshot, action, &error);
      if(uishell_sidebar_result(state, ok, error)) { uishell_sidebar_effects(state, split); }
      uishell_sidebar_refresh(state);
      rd_request_frame();
    }
    Temp scratch = scratch_begin(0, 0);
    UIShell_ControlledSplit current = uishell_root_controlled_split_from_window(scratch.arena, split->owner_cfg);
    uishell_sidebar_observe(state, &current);
    scratch_end(scratch);
  }
}

internal B32
uishell_sidebar_choice_ui(Rng2F32 *rect, UIShell_ControlledSplit *split)
{
  CFG_Node *setting = cfg_node_child_from_string(split->owner_cfg, str8_lit("sidebar_mode"));
  B32 andamento = uishell_sidebar_uses_andamento(split->owner_cfg);
  F32 height = ui_top_font_size()*2.2f;
  Rng2F32 header = *rect;
  header.y1 = Min(header.y1, header.y0+height);
  UI_Box *box = &ui_nil_box;
  UI_Rect(header) UI_ChildLayoutAxis(Axis2_X)
  {
    box = ui_build_box_from_string(UI_BoxFlag_DrawBackground, str8_lit("###sidebar_mode"));
  }
  {
    UI_Parent(box) UI_PrefWidth(ui_pct(0.5f, 0)) UI_PrefHeight(ui_pct(1, 0))
    {
      B32 previews = 0, core = 0;
      UI_TagF("tab") UI_TagF(andamento ? "inactive" : "")
      { previews = ui_clicked(ui_button(str8_lit("Workspaces###sidebar_previews"))); }
      UI_TagF("tab") UI_TagF(andamento ? "" : "inactive")
      { core = ui_clicked(ui_button(str8_lit("Andamento###sidebar_andamento"))); }
      if(previews || core)
      {
        setting = cfg_node_child_from_string_or_alloc(rd_state->cfg, split->owner_cfg, str8_lit("sidebar_mode"));
        cfg_node_new_replace(rd_state->cfg, setting, core ? str8_lit("andamento") : str8_lit("previews"));
        andamento = core;
        rd_request_frame();
      }
    }
  }
  rect->y0 = header.y1;
  if(andamento) { uishell_sidebar_ui(*rect, split); }
  return andamento;
}

// Exercises the native host adapter, not just andamento's own C fixture.
// Exercise disclosure geometry with the same font/padding as the shell.
internal B32
uishell_sidebar_disclosure_diagnostics(RD_WindowState *ws)
{
  UI_State *saved = ui_state;
  UI_State *test_ui = ui_state_alloc();
  ui_select_state(test_ui);
  UI_IconInfo icons = ws->ui->icon_info;
  UI_AnimationInfo animation = {0};
  UI_EventList events = {0};
  ui_begin_build(ws->os, &events, &icons, ws->theme, &animation, 1.f/60, 1.f/60);
  UI_Box *buttons[2] = {0};
  UI_Font(rd_font_from_slot(RD_FontSlot_Main)) UI_FontSize(11) UI_TextPadding(3)
  UI_PrefWidth(ui_em(1.2f, 1)) UI_PrefHeight(ui_em(1.65f, 1))
  {
    buttons[0] = uishell_sidebar_disclosure(1, str8_lit("###disclosure_open")).box;
    buttons[1] = uishell_sidebar_disclosure(0, str8_lit("###disclosure_closed")).box;
  }
  ui_end_build();
  B32 ok = 1;
  for(U64 i = 0; i < 2; i++)
  {
    F32 available = buttons[i]->rect.x1-ui_box_text_position(buttons[i]).x;
    F32 text_width = buttons[i]->display_fruns.dim.x;
    if(text_width <= 0 || text_width > available) { fprintf(stderr, "FAIL disclosure %llu: text=%g available=%g\n", i, text_width, available); }
    ok = ok && text_width > 0 && text_width <= available;
  }
  ui_select_state(saved);
  ui_state_release(test_ui);
  return ok;
}

internal B32
uishell_sidebar_diagnostics(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  if(!uishell_sidebar_disclosure_diagnostics(ws)) { scratch_end(scratch); return 0; }
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  if(state->core == 0 || state->snapshot == 0)
  {
    fprintf(stderr, "Sidebar host diagnostics: FAILED (initialization: %s)\n", state->error);
    scratch_end(scratch);
    return 0;
  }
  UIShell_ControlledSplit split = uishell_root_controlled_split_from_window(scratch.arena, window);
  U64 before = split.inventory.count;
  size_t activate = ANDAMENTO_NONE;
  for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
  {
    AndamentoNode node = {0};
    if(andamento_snapshot_node(state->snapshot, i, &node) &&
       str8_match(uishell_sidebar_string(node.entity_id), str8_lit("multi"), 0))
    { activate = node.activate; break; }
  }
  char *error = 0;
  B32 ok = state->core != 0 && activate != ANDAMENTO_NONE;
  if(ok) { ok = andamento_dispatch(state->core, state->snapshot, activate, &error); }
  ok = uishell_sidebar_result(state, ok, error);
  if(ok)
  {
    uishell_sidebar_effects(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    CFG_ID created = ws->root_controlled_split_selected_workspace_id;
    CFG_Node *workspace = cfg_node_from_id(created);
    ok = split.inventory.count == before+1 && workspace != &cfg_nil_node &&
         cfg_node_child_from_string(workspace, str8_lit("sidebar_entity_id")) != &cfg_nil_node;
    CFG_Node *panels = cfg_node_child_from_string(workspace, str8_lit("panels"));
    CFG_PanelTree tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
    ok = ok && cfg_node_child_from_string(workspace, str8_lit("split_x")) != &cfg_nil_node &&
         tree.root->child_count == 2 && tree.root->first->tabs.count == 1 && tree.root->last->tabs.count == 2;
    CFG_Node *tools_tab = tree.root->last->tabs.last->v;
    CFG_ID tools_id = tools_tab->id;
    // Simulate selecting the second overflow tab; refocusing must preserve it.
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(tree.root->last->selected_tab, str8_lit("selected")));
    cfg_node_new(rd_state->cfg, tools_tab, str8_lit("selected"));
    uishell_sidebar_observe(state, &split);
    B32 live = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0};
      andamento_snapshot_node(state->snapshot, i, &node);
      if(node.workspace_id == created && node.state == ANDAMENTO_LIVE)
      { live = 1; activate = node.activate; }
    }
    ok = ok && live;
    // A second activation focuses the same workspace without creating another.
    error = 0;
    ok = ok && andamento_dispatch(state->core, state->snapshot, activate, &error);
    uishell_sidebar_result(state, ok, error);
    uishell_sidebar_effects(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    ok = ok && split.inventory.count == before+1 && ws->root_controlled_split_selected_workspace_id == created;
    tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
    ok = ok && tree.root->last->selected_tab->id == tools_id;
    uishell_sidebar_refresh(state);
    // A pending focus whose target disappears must be completed as a failure.
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
      if(node.workspace_id == created && node.state == ANDAMENTO_LIVE) { activate = node.activate; }
    }
    error = 0;
    ok = ok && andamento_dispatch(state->core, state->snapshot, activate, &error);
    uishell_sidebar_result(state, ok, error);
    cfg_node_release(rd_state->cfg, workspace);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    uishell_sidebar_effects(state, &split);
    uishell_sidebar_observe(state, &split);
    ok = ok && andamento_snapshot_diagnostic_count(state->snapshot) > 0;
    B32 latent = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
      if(str8_match(uishell_sidebar_string(node.entity_id), str8_lit("multi"), 0) && node.state == ANDAMENTO_LATENT)
      { latent = 1; activate = node.activate; }
    }
    ok = ok && latent && split.inventory.count == before;
    // Retry after closure/error can create a new presentation of the same entity.
    error = 0;
    ok = ok && andamento_dispatch(state->core, state->snapshot, activate, &error);
    uishell_sidebar_result(state, ok, error);
    uishell_sidebar_effects(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    ok = ok && split.inventory.count == before+1;
    workspace = cfg_node_from_id(ws->root_controlled_split_selected_workspace_id);
    panels = cfg_node_child_from_string(workspace, str8_lit("panels"));
    tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
    tools_tab = tree.root->last->tabs.last->v;
    tools_id = tools_tab->id;
    cfg_node_release(rd_state->cfg, cfg_node_child_from_string(tree.root->last->selected_tab, str8_lit("selected")));
    cfg_node_new(rd_state->cfg, tools_tab, str8_lit("selected"));
    // A new core instance rebinds persisted identities without duplicating them.
    uishell_sidebar_release(state);
    MemoryZeroStruct(state);
    state = uishell_sidebar_init(ws);
    uishell_sidebar_restore(state, &split);
    split = uishell_root_controlled_split_from_window(scratch.arena, window);
    ok = ok && split.inventory.count == before+1;
    B32 restored_live = 0;
    for(U64 i = 0; i < andamento_snapshot_node_count(state->snapshot); i++)
    {
      AndamentoNode node = {0}; andamento_snapshot_node(state->snapshot, i, &node);
      restored_live |= node.state == ANDAMENTO_LIVE;
    }
    tree = cfg_panel_tree_from_panels_cfg(scratch.arena, panels, Axis2_X);
    ok = ok && restored_live && tree.root->last->selected_tab->id == tools_id &&
         tree.root->first->tabs.count == 1 && tree.root->last->tabs.count == 2;
  }
  fprintf(stderr, "Sidebar host diagnostics: %s (split layout, overflow selection, focus, close, failure, retry, restore)\n", ok ? "passed" : "FAILED");
  scratch_end(scratch);
  return ok;
}

// Called between frames only: snapshots used by clicks have finished dispatch.
internal U32
uishell_sidebar_apply_live(void *unused, const U8 *data, size_t size)
{
  B32 rejected = 0, unavailable = 0, applied = 0;
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  {
    UIShell_SidebarState *state = uishell_sidebar_init(ws);
    if(state->core == 0) { unavailable = 1; continue; }
    char *error = 0;
    B32 accepted = andamento_apply_patch_json(state->core, wheelhouse_ingress_now_ms(), (AndamentoText){data, size}, &error);
    if(!uishell_sidebar_result(state, accepted, error)) { rejected = 1; }
    if(accepted) { state->restored = 0; state->error[0] = 0; applied = 1; }
  }
  rd_request_frame();
  return rejected ? 0 : (unavailable || !applied) ? 2 : 1;
}

internal void
uishell_sidebar_poll_live(void)
{
  if(uishell_ingress == 0) { return; }
  wheelhouse_ingress_poll(uishell_ingress, uishell_sidebar_apply_live, 0);
  U64 now = wheelhouse_ingress_now_ms();
  if(now - uishell_sidebar_last_tick >= 250)
  {
    uishell_sidebar_last_tick = now;
    for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
    {
      UIShell_SidebarState *state = uishell_sidebar_init(ws);
      if(state->core != 0)
      {
        char *error = 0;
        B32 ok = andamento_tick(state->core, now, &error);
        uishell_sidebar_result(state, ok, error);
      }
    }
    rd_request_frame();
  }
  // Apply every queued patch and expiry tick before publishing one snapshot.
  // Andamento owns change detection, including action-only changes and leases.
  for(RD_WindowState *ws = rd_state->first_window_state; ws != &rd_nil_window_state; ws = ws->order_next)
  {
    UIShell_SidebarState *state = uishell_sidebar_init(ws);
    if(state->core != 0) { uishell_sidebar_refresh(state); }
  }
}
