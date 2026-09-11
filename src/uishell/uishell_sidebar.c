// Native andamento control surface. Included after the workspace helpers.
#include "andamento.h"
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

struct UIShell_SidebarState
{
  Andamento *core;
  AndamentoSnapshot *snapshot;
  U64 topology_hash;
  B32 initialized;
  B32 restored;
  U8 error[512];
};

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
    state->core = andamento_create((U8 *)uishell_sidebar_fixture_config, sizeof(uishell_sidebar_fixture_config)-1, &error);
    if(uishell_sidebar_result(state, state->core != 0, error))
    {
      String8 patches = str8_cstring((char *)uishell_sidebar_fixture_patches);
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
      AndamentoFact recipe = {0};
      recipe.key = uishell_sidebar_text(str8_lit("action.primary.recipe"));
      recipe.kind = ANDAMENTO_FACT_TEXT;
      recipe.text = uishell_sidebar_text(str8_lit("cmd.exe /K echo Wheelhouse sidebar fixture"));
      error = 0;
      B32 ok = andamento_apply_entity(state->core, 0, uishell_sidebar_text(str8_lit("vessel")), uishell_sidebar_text(str8_lit("v")), uishell_sidebar_text(str8_lit("fixture")), &recipe, 1, &error);
      uishell_sidebar_result(state, ok, error);
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
        if(str8_match(uishell_sidebar_string(effect.entity_kind), str8_lit("vessel"), 0) &&
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
      wm_graphical_message(0, str8_lit("Sidebar entry"), message);
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
      if(node.activate != ANDAMENTO_NONE &&
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

internal void
uishell_sidebar_ui(Rng2F32 rect, UIShell_ControlledSplit *split)
{
  RD_WindowState *ws = rd_window_state_from_cfg__existing(split->owner_cfg);
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
  uishell_sidebar_restore(state, split);
  size_t action = ANDAMENTO_NONE;
  UI_Box *box = &ui_nil_box;
  UI_Focus(UI_FocusKind_On) UI_Rect(rect) UI_ChildLayoutAxis(Axis2_Y)
  {
    box = ui_build_box_from_string(UI_BoxFlag_DrawBackground|UI_BoxFlag_Clip|UI_BoxFlag_ViewScrollY|UI_BoxFlag_ViewClamp|UI_BoxFlag_AllowOverflowY|UI_BoxFlag_DefaultFocusNav, str8_lit("###andamento_sidebar"));
  }
  {
    UI_Parent(box) UI_PrefWidth(ui_pct(1, 0)) UI_PrefHeight(ui_em(1.8f, 1))
    {
      UI_TagF("weak") ui_label(str8_lit("Example data / local workspaces"));
      if(state->error[0]) { ui_label(str8_cstring((char *)state->error)); }
      if(state->snapshot != 0)
      {
        Temp scratch = scratch_begin(0, 0);
        U64 count = andamento_snapshot_node_count(state->snapshot);
        B32 *hidden = push_array(scratch.arena, B32, count);
        U64 *depth = push_array(scratch.arena, U64, count);
        for(U64 i = 0; i < count; i++)
        {
          AndamentoNode node = {0};
          if(!andamento_snapshot_node(state->snapshot, i, &node)) { continue; }
          if(node.parent != ANDAMENTO_NONE)
          {
            AndamentoNode parent = {0};
            andamento_snapshot_node(state->snapshot, node.parent, &parent);
            hidden[i] = hidden[node.parent] || parent.collapsed;
            depth[i] = depth[node.parent] + !parent.is_section;
          }
          if(hidden[i]) { continue; }
          String8 key = uishell_sidebar_string(node.key);
          String8 label = uishell_sidebar_string(node.label);
          if(node.is_section && node.field_count)
          {
            AndamentoField field = {0};
            if(andamento_snapshot_field(state->snapshot, node.first_field, &field)) { label = uishell_sidebar_string(field.text); }
          }
          UI_Row
          {
            ui_spacer(ui_em(0.4f + depth[i]*0.7f, 1));
            UI_PrefWidth(ui_em(1.5f, 1))
            {
              if(node.toggle != ANDAMENTO_NONE)
              {
                if(ui_clicked(ui_buttonf("%s###sidebar_toggle_%u_%S", node.collapsed ? ">" : "v", node.is_section, key))) { action = node.toggle; }
              }
              else { ui_spacer(ui_em(1.5f, 1)); }
            }
            UI_PrefWidth(ui_pct(1, 0))
            {
              if(node.activate != ANDAMENTO_NONE)
              {
                if(ui_clicked(ui_buttonf("%S###sidebar_entry_%u_%S", label, node.is_section, key))) { action = node.activate; }
              }
              else if(node.field_count || !node.is_section) { ui_label(label); }
            }
          }
          if(!node.is_section)
          {
            UI_Row
            {
              ui_spacer(ui_em(1.9f + depth[i]*0.7f, 1));
              UI_TagF("weak") UI_PrefWidth(ui_text_dim(0.5f, 1))
              {
                for(U64 f = 0; f < node.field_count; f++)
                {
                  AndamentoField field = {0};
                  if(andamento_snapshot_field(state->snapshot, node.first_field+f, &field) &&
                     !str8_match(uishell_sidebar_string(field.text), uishell_sidebar_string(node.label), 0))
                  { ui_label(uishell_sidebar_string(field.text)); }
                }
                ui_label(node.state == ANDAMENTO_LIVE ? str8_lit("live") : node.state == ANDAMENTO_OPENING ? str8_lit("opening") : str8_lit("available"));
              }
            }
          }
          for(U64 c = 0; c < node.control_count; c++)
          {
            AndamentoControl control = {0};
            if(andamento_snapshot_control(state->snapshot, node.first_control+c, &control) && control.action != ANDAMENTO_NONE)
            {
              if(ui_clicked(ui_buttonf("%s %S###sidebar_control_%u_%S_%I64u", control.checked ? "[x]" : "[ ]", uishell_sidebar_string(control.label), node.is_section, key, c))) { action = control.action; }
            }
          }
        }
        for(U64 i = 0; i < andamento_snapshot_diagnostic_count(state->snapshot); i++)
        {
          AndamentoText diagnostic = {0};
          if(andamento_snapshot_diagnostic(state->snapshot, i, &diagnostic)) { ui_label(uishell_sidebar_string(diagnostic)); }
        }
        scratch_end(scratch);
      }
    }
  }
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
  B32 andamento = str8_match(setting->first->string, str8_lit("andamento"), 0);
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
internal B32
uishell_sidebar_diagnostics(CFG_Node *window)
{
  Temp scratch = scratch_begin(0, 0);
  RD_WindowState *ws = rd_window_state_from_cfg__existing(window);
  UIShell_SidebarState *state = uishell_sidebar_init(ws);
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
