// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

internal CFG_KeyMap *
cfg_key_map_from_cfg(Arena *arena)
{
  Temp scratch = scratch_begin(&arena, 1);
  CFG_KeyMap *key_map = push_array(arena, CFG_KeyMap, 1);
  {
    key_map->name_slots_count = 4096;
    key_map->name_slots = push_array(arena, CFG_KeyMapSlot, key_map->name_slots_count);
    key_map->binding_slots_count = 4096;
    key_map->binding_slots = push_array(arena, CFG_KeyMapSlot, key_map->binding_slots_count);
    
    //- rjf: gather & parse all explicitly stored keybinding sets
    CFG_NodePtrList keybindings_cfg_list = cfg_node_top_level_list_from_string(scratch.arena, str8_lit("keybindings"));
    for(CFG_NodePtrNode *n = keybindings_cfg_list.first; n != 0; n = n->next)
    {
      CFG_Node *keybindings_root = n->v;
      for(CFG_Node *keybinding = keybindings_root->first; keybinding != &cfg_nil_node; keybinding = keybinding->next)
      {
        String8 name = {0};
        CFG_Binding binding = {0};
        U64 key_count = 0, name_count = 0;
        B32 nested = 0;
        for(CFG_Node *child = keybinding->first; child != &cfg_nil_node; child = child->next)
        {
          nested |= child->first != &cfg_nil_node;
          if(0){}
          else if(str8_match(child->string, str8_lit("ctrl"), 0))   { binding.modifiers |= WM_Modifier_Ctrl; }
          else if(str8_match(child->string, str8_lit("alt"), 0))    { binding.modifiers |= WM_Modifier_Alt; }
          else if(str8_match(child->string, str8_lit("shift"), 0))  { binding.modifiers |= WM_Modifier_Shift; }
          else if(str8_match(child->string, str8_lit("accel"), 0))  { binding.modifiers |= WM_Modifier_Accel; }
          else if(str8_match(child->string, str8_lit("super"), 0) ||
                  str8_match(child->string, str8_lit("cmd"), 0) ||
                  str8_match(child->string, str8_lit("command"), 0))
          {
            binding.modifiers |= WM_Modifier_Super;
          }
          else
          {
            WM_Key key = WM_Key_Null;
            for EachEnumVal(WM_Key, k)
            {
              if(str8_match(child->string, wm_key_cfg_name_table[k], StringMatchFlag_CaseInsensitive))
              {
                key = k;
                break;
              }
            }
            if(key != WM_Key_Null)
            {
              key_count += 1;
              binding.key = key;
            }
            else
            {
              name_count += 1;
              name = child->string;
            }
          }
        }
        if(name.size != 0)
        {
          U64 name_hash = u64_djb2_hash_from_str8(name);
          U64 binding_hash = u64_djb2_hash_from_str8(str8_struct(&binding));
          U64 name_slot_idx = name_hash%key_map->name_slots_count;
          U64 binding_slot_idx = binding_hash%key_map->binding_slots_count;
          CFG_KeyMapNode *n = push_array(arena, CFG_KeyMapNode, 1);
          n->cfg_id = keybinding->id;
          n->name = push_str8_copy(arena, name);
          n->binding = binding;
          n->native_shortcut_eligible = key_count == 1 && name_count == 1 && !nested;
          SLLQueuePush_N(key_map->name_slots[name_slot_idx].first, key_map->name_slots[name_slot_idx].last, n, name_hash_next);
          SLLQueuePush_N(key_map->binding_slots[binding_slot_idx].first, key_map->binding_slots[binding_slot_idx].last, n, binding_hash_next);
        }
      }
    }
  }
  scratch_end(scratch);
  return key_map;
}

internal CFG_KeyMapNodePtrList
cfg_key_map_node_ptr_list_from_name(Arena *arena, CFG_KeyMap *key_map, String8 string)
{
  CFG_KeyMapNodePtrList list = {0};
  {
    U64 hash = u64_djb2_hash_from_str8(string);
    U64 slot_idx = hash%key_map->name_slots_count;
    for(CFG_KeyMapNode *n = key_map->name_slots[slot_idx].first; n != 0; n = n->name_hash_next)
    {
      if(str8_match(n->name, string, 0))
      {
        CFG_KeyMapNodePtr *ptr = push_array(arena, CFG_KeyMapNodePtr, 1);
        ptr->v = n;
        SLLQueuePush(list.first, list.last, ptr);
        list.count += 1;
      }
    }
  }
  return list;
}

internal CFG_KeyMapNodePtrList
cfg_key_map_node_ptr_list_from_binding(Arena *arena, CFG_KeyMap *key_map, CFG_Binding binding)
{
  CFG_KeyMapNodePtrList list = {0};
  {
    U64 hash = u64_djb2_hash_from_str8(str8_struct(&binding));
    U64 slot_idx = hash%key_map->binding_slots_count;
    for(CFG_KeyMapNode *n = key_map->binding_slots[slot_idx].first; n != 0; n = n->binding_hash_next)
    {
      if(MemoryMatchStruct(&binding, &n->binding))
      {
        CFG_KeyMapNodePtr *ptr = push_array(arena, CFG_KeyMapNodePtr, 1);
        ptr->v = n;
        SLLQueuePush(list.first, list.last, ptr);
        list.count += 1;
      }
    }
  }
  return list;
}

// Select in effective configuration order, skipping unsupported and shadowed
// bindings. Never publish a shortcut that the shell assigns to another command.
internal CFG_Binding
cfg_native_menu_binding(CFG_KeyMap *key_map, String8 command)
{
  CFG_Binding result = {0};
  if(key_map != 0)
  {
    U64 slot = u64_djb2_hash_from_str8(command)%key_map->name_slots_count;
    for(CFG_KeyMapNode *n = key_map->name_slots[slot].first; n; n = n->name_hash_next)
    {
      if(!str8_match(n->name, command, 0) || !n->native_shortcut_eligible ||
         wm_menu_codepoint_from_key(n->binding.key) == 0 ||
         (n->binding.modifiers & ~(WM_Modifier_Ctrl|WM_Modifier_Super|WM_Modifier_Shift|WM_Modifier_Alt)))
      {
        continue;
      }
      U64 binding_slot = u64_djb2_hash_from_str8(str8_struct(&n->binding))%key_map->binding_slots_count;
      CFG_KeyMapNode *owner = key_map->binding_slots[binding_slot].first;
      for(; owner && !MemoryMatchStruct(&owner->binding, &n->binding); owner = owner->binding_hash_next) {}
      if(owner && str8_match(owner->name, command, 0))
      {
        result = n->binding;
        break;
      }
    }
  }
  return result;
}

internal String8
cfg_command_from_menu_or_binding(Arena *arena, CFG_KeyMap *key_map, WM_Event *event)
{
  String8 result = {0};
  if(event->kind == WM_EventKind_MenuCommand) { result = event->string; }
  else if(event->kind == WM_EventKind_Press)
  {
    CFG_Binding binding = {event->key, event->modifiers};
    CFG_KeyMapNodePtrList nodes = cfg_key_map_node_ptr_list_from_binding(arena, key_map, binding);
    if(nodes.first != 0) { result = nodes.first->v->name; }
  }
  return result;
}

// The shell and policy tests share the actual event consumption/config mutation
// seam. A native menu open cancels the entire batch before any key can be saved.
internal B32
cfg_process_binding_recording(CFG_State *cfg, B32 *active, CFG_ID binding_id, String8 command, WM_EventList *events)
{
  B32 changed = 0;
  if(wm_events_cancel_key_recording(events))
  {
    B32 changed = *active;
    *active = 0;
    return changed;
  }
  if(*active)
  {
    if(wm_key_press(events, wm_window_zero(), 0, WM_Key_Esc))
    {
      changed = 1;
      *active = 0;
    }
    if(wm_key_press(events, wm_window_zero(), 0, WM_Key_Delete))
    {
      changed = 1;
      cfg_node_release(cfg, cfg_node_from_id(binding_id));
      *active = 0;
    }
    for(WM_Event *event = events->first, *next = 0; event != 0 && *active; event = next)
    {
      next = event->next;
      if(event->kind == WM_EventKind_Press &&
         event->key != WM_Key_Esc &&
         event->key != WM_Key_Return &&
         event->key != WM_Key_Backspace &&
         event->key != WM_Key_Delete &&
         event->key != WM_Key_LeftMouseButton &&
         event->key != WM_Key_RightMouseButton &&
         event->key != WM_Key_MiddleMouseButton &&
         event->key != WM_Key_Ctrl &&
         event->key != WM_Key_Alt &&
         event->key != WM_Key_Shift)
      {
        *active = 0;
        CFG_Node *binding = cfg_node_from_id(binding_id);
        if(binding == &cfg_nil_node)
        {
          CFG_Node *user = cfg_node_child_from_string(cfg_node_root(), str8_lit("user"));
          CFG_Node *keybindings = cfg_node_child_from_string_or_alloc(cfg, user, str8_lit("keybindings"));
          binding = cfg_node_new(cfg, keybindings, str8_lit(""));
        }
        cfg_node_release_all_children(cfg, binding);
        cfg_node_new(cfg, binding, command);
        cfg_node_new(cfg, binding, wm_key_cfg_name_table[event->key]);
        if(event->modifiers & WM_Modifier_Ctrl)  { cfg_node_new(cfg, binding, str8_lit("ctrl")); }
        if(event->modifiers & WM_Modifier_Shift) { cfg_node_new(cfg, binding, str8_lit("shift")); }
        if(event->modifiers & WM_Modifier_Alt)   { cfg_node_new(cfg, binding, str8_lit("alt")); }
        if(event->modifiers & WM_Modifier_Super) { cfg_node_new(cfg, binding, str8_lit("super")); }
        U32 codepoint = wm_codepoint_from_modifiers_and_key(event->modifiers, event->key);
        wm_text(events, event->window, codepoint);
        wm_eat_event(events, event);
        changed = 1;
        break;
      }
    }
  }

  return changed;
}
