// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal CFG_Node *
cfg_window_from_cfg(CFG_Node *cfg)
{
  CFG_Node *result = &cfg_nil_node;
  for(CFG_Node *c = cfg; c != &cfg_nil_node; c = c->parent)
  {
    if(c->parent->parent == cfg_node_root() && str8_match(c->string, str8_lit("window"), 0))
    {
      result = c;
      break;
    }
  }
  return result;
}

// A panel's children are its sub-panels, its Views and these options. `id`
// is the panel's stable ID (shell_arrangement.h); `section_hint_cleanup`
// marks a panel restore took a sidebar section out of, for the sidebar's
// reconciliation to remove if nothing is left in it.
internal B32
cfg_panel_child_is_option(String8 name)
{
  return str8_match(name, str8_lit("tabs_on_bottom"), 0) || str8_match(name, str8_lit("section_collapsed"), 0) ||
    str8_match(name, str8_lit("id"), 0) || str8_match(name, str8_lit("section_hint_cleanup"), 0) ||
    // The ID a provider's document gave the panel (uishell_workspace_store.c).
    str8_match(name, str8_lit("doc_id"), 0);
}
