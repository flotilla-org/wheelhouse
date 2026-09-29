// Production-policy regressions shared by standalone and application diagnostics.
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define NativeMenuCheck(x) do { if(!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); failures++; } } while(0)

internal B32
cfg_native_menu_diagnostics(void)
{
  U64 failures = 0;
  CFG_Ctx *saved_ctx = cfg_ctx;
  Arena *arena = arena_alloc();
  CFG_State *cfg = cfg_state_alloc();
  cfg_ctx_select(cfg_state_ctx(cfg));
  CFG_Node *user = cfg_node_new(cfg, cfg_node_root(), str8_lit("user"));
  CFG_SchemaNode *schema_slot = 0;
  CFG_SchemaTable schemas = {&schema_slot, 1};
  CFG_NodePtrList parsed = cfg_node_ptr_list_from_string(arena, cfg, &schemas, str8_zero(), str8_lit(
    "keybindings: {\n"
    " { palette left_mouse super }\n"
    " { palette k p super }\n" // Flattened by legacy parser; not a native single stroke.
    " { palette p super shift alt }\n"
    " { palette l ctrl }\n"
    " { control p ctrl shift alt }\n"
    " { owner f super }\n"
    " { shadow f super }\n"
    " { shadow g super }\n"
    " { unsupported left_mouse }\n"
    " { chord k p ctrl }\n"
    " { function f11 }\n"
    "}\n"));
  for(CFG_NodePtrNode *n = parsed.first; n; n = n->next) { cfg_node_insert_child(cfg, user, user->last, n->v); }
  CFG_KeyMap *map = cfg_key_map_from_cfg(arena);
  CFG_Binding palette = cfg_native_menu_binding(map, str8_lit("palette"));
  NativeMenuCheck(palette.key == WM_Key_P);
  NativeMenuCheck(palette.modifiers == (WM_Modifier_Super|WM_Modifier_Shift|WM_Modifier_Alt));
  CFG_Binding control = cfg_native_menu_binding(map, str8_lit("control"));
  NativeMenuCheck(control.key == WM_Key_P && control.modifiers == (WM_Modifier_Ctrl|WM_Modifier_Shift|WM_Modifier_Alt));
  NativeMenuCheck(cfg_native_menu_binding(map, str8_lit("missing")).key == WM_Key_Null);
  NativeMenuCheck(cfg_native_menu_binding(map, str8_lit("unsupported")).key == WM_Key_Null);
  NativeMenuCheck(cfg_native_menu_binding(map, str8_lit("chord")).key == WM_Key_Null);
  NativeMenuCheck(cfg_native_menu_binding(map, str8_lit("shadow")).key == WM_Key_G);
  NativeMenuCheck(cfg_native_menu_binding(map, str8_lit("function")).key == WM_Key_F11);
  NativeMenuCheck(wm_menu_codepoint_from_key(WM_Key_P) == 'p');
  NativeMenuCheck(wm_menu_codepoint_from_key(WM_Key_F11) == 0xf70e);
  NativeMenuCheck(wm_menu_codepoint_from_key(WM_Key_Left) == 0xf702);
  NativeMenuCheck(wm_menu_codepoint_from_key(WM_Key_Num1) == 0);
  NativeMenuCheck(wm_menu_codepoint_from_key(WM_Key_LeftMouseButton) == 0);
  NativeMenuCheck(wm_menu_codepoint_from_key(WM_Key_Ctrl) == 0);

  // The actual menu-refresh fingerprint includes every displayed/dispatch field.
  WM_MenuItem item = {WM_MenuItemKind_Command, str8_lit("Palette"), str8_lit("palette"), palette.key, palette.modifiers};
  WM_Menu menu = {str8_lit("Probe"), 1, &item};
  WM_MenuArray menus = {1, &menu};
  U64 hash = wm_menu_hash(menus, 1);
  NativeMenuCheck(hash == wm_menu_hash(menus, 1));
  NativeMenuCheck(hash != wm_menu_hash(menus, 0));
  WM_MenuItem original_item = item;
  item.shortcut_key = WM_Key_L;
  NativeMenuCheck(hash != wm_menu_hash(menus, 1));
  item = original_item;
  item.shortcut_modifiers = WM_Modifier_Ctrl;
  NativeMenuCheck(hash != wm_menu_hash(menus, 1));
  item = original_item;
  item.command_name = str8_lit("other_command");
  NativeMenuCheck(hash != wm_menu_hash(menus, 1));
  item = original_item;
  item.label = str8_lit("Other label");
  NativeMenuCheck(hash != wm_menu_hash(menus, 1));
  item = original_item;
  item.kind = WM_MenuItemKind_Separator;
  NativeMenuCheck(hash != wm_menu_hash(menus, 1));
  item = original_item;
  menu.label = str8_lit("Another menu");
  NativeMenuCheck(hash != wm_menu_hash(menus, 1));
  menu.label = str8_lit("Probe");
  menu.item_count = 0;
  NativeMenuCheck(hash != wm_menu_hash(menus, 1));
  menu.item_count = 1;
  item.label = push_str8_copy(arena, item.label);
  NativeMenuCheck(hash == wm_menu_hash(menus, 1));

  NativeMenuCheck(wm_key_event_is_shell_owned(0));
  NativeMenuCheck(!wm_key_event_is_shell_owned(1));

  // Both event owners resolve the same command and retain the event's window.
  // The adapter uses this ownership policy before translating physical keys.
  for(U64 tracking = 0; tracking < 2; tracking++)
  {
    for(U64 window = 1; window <= 2; window++)
    {
      WM_EventList events = {0};
      if(tracking) { wm_event_list_push_new(arena, &events, WM_EventKind_MenuOpen); }
      WM_Event *event = wm_event_list_push_new(arena, &events,
        wm_key_event_is_shell_owned(tracking) ? WM_EventKind_Press : WM_EventKind_MenuCommand);
      event->key = palette.key;
      event->modifiers = palette.modifiers;
      event->string = str8_lit("palette");
      event->window.u64[0] = window;
      WM_Event *release = wm_event_list_push_new(arena, &events, WM_EventKind_Release);
      release->key = palette.key;
      B32 recording = tracking;
      cfg_process_binding_recording(cfg, &recording, 0, str8_lit("new_binding"), &events);
      U64 dispatches = 0, saved = 0;
      for(WM_Event *e = events.first; e; e = e->next)
      {
        if(recording && e->kind == WM_EventKind_Press) { saved++; }
        String8 command = cfg_command_from_menu_or_binding(arena, map, e);
        if(command.size)
        {
          dispatches++;
          NativeMenuCheck(str8_match(command, str8_lit("palette"), 0));
          NativeMenuCheck(e->window.u64[0] == window);
        }
      }
      NativeMenuCheck(dispatches == 1 && saved == 0);
    }
  }
  // Menu-open cancellation wins even over a key already queued in this batch.
  WM_EventList events = {0};
  wm_event_list_push_new(arena, &events, WM_EventKind_Press)->key = WM_Key_P;
  wm_event_list_push_new(arena, &events, WM_EventKind_MenuOpen);
  B32 recording = 1;
  U64 before = cfg_ctx->change_gen;
  NativeMenuCheck(cfg_process_binding_recording(cfg, &recording, 0, str8_lit("new_binding"), &events));
  NativeMenuCheck(!recording && cfg_ctx->change_gen == before);
  // An actual recorded stroke mutates the config and is consumed.
  events = (WM_EventList){0};
  WM_Event *press = wm_event_list_push_new(arena, &events, WM_EventKind_Press);
  press->key = WM_Key_L;
  press->modifiers = WM_Modifier_Super;
  recording = 1;
  NativeMenuCheck(cfg_process_binding_recording(cfg, &recording, 0, str8_lit("recorded"), &events));
  NativeMenuCheck(!recording && events.count == 0);
  map = cfg_key_map_from_cfg(arena);
  NativeMenuCheck(cfg_native_menu_binding(map, str8_lit("recorded")).key == WM_Key_L);
  // A printable recording consumes its companion text but preserves release.
  events = (WM_EventList){0};
  wm_event_list_push_new(arena, &events, WM_EventKind_Press)->key = WM_Key_T;
  wm_event_list_push_new(arena, &events, WM_EventKind_Text)->character = 't';
  wm_event_list_push_new(arena, &events, WM_EventKind_Release)->key = WM_Key_T;
  recording = 1;
  NativeMenuCheck(cfg_process_binding_recording(cfg, &recording, 0, str8_lit("recorded_text"), &events));
  NativeMenuCheck(!recording && events.count == 1 && events.first->kind == WM_EventKind_Release);
  NativeMenuCheck(cfg_command_from_menu_or_binding(arena, map, events.first).size == 0);
  // Cancel with Escape followed by another press: no binding may be saved.
  events = (WM_EventList){0};
  wm_event_list_push_new(arena, &events, WM_EventKind_Press)->key = WM_Key_Esc;
  wm_event_list_push_new(arena, &events, WM_EventKind_Press)->key = WM_Key_G;
  recording = 1;
  before = cfg_ctx->change_gen;
  NativeMenuCheck(cfg_process_binding_recording(cfg, &recording, 0, str8_lit("canceled"), &events));
  NativeMenuCheck(!recording && before == cfg_ctx->change_gen);

  // Edit and remove real config nodes, then rebuild the effective map.
  CFG_KeyMapNodePtrList nodes = cfg_key_map_node_ptr_list_from_name(arena, map, str8_lit("control"));
  CFG_Node *binding = cfg_node_from_id(nodes.first->v->cfg_id);
  cfg_node_release_all_children(cfg, binding);
  cfg_node_new(cfg, binding, str8_lit("control"));
  cfg_node_new(cfg, binding, str8_lit("l"));
  cfg_node_new(cfg, binding, str8_lit("super"));
  map = cfg_key_map_from_cfg(arena);
  control = cfg_native_menu_binding(map, str8_lit("control"));
  NativeMenuCheck(control.key == WM_Key_L && control.modifiers == WM_Modifier_Super);
  cfg_node_release(cfg, binding);
  map = cfg_key_map_from_cfg(arena);
  NativeMenuCheck(cfg_native_menu_binding(map, str8_lit("control")).key == WM_Key_Null);
  fprintf(stderr, "%llu native menu policy failures\n", (unsigned long long)failures);
  cfg_ctx_select(saved_ctx);
  cfg_state_release(cfg);
  arena_release(arena);
  return failures == 0;
}

#undef NativeMenuCheck
