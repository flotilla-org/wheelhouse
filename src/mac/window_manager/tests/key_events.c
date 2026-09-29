// Adapted from RAD ad1a3e18; Wheelhouse keeps Command distinct from Control.
// Licensed under the MIT license (https://opensource.org/license/mit/)

#define BUILD_CONSOLE_INTERFACE 1
#include "base/base_inc.h"
#include "window_manager/window_manager_inc.h"
#include "base/base_inc.c"
#include "window_manager/window_manager_inc.c"

#define MacMenuCheck(expr) do { if(!(expr)) { good = 0; fprintf(stderr, "FAIL native menu line %d: %s\n", __LINE__, #expr); } } while(0)

internal void
entry_point(CmdLine *cmdline)
{
  @autoreleasepool
  {
    WM_Window window = wm_window_open(r2f32p(0, 0, 320, 200), 0, str8_lit("Key event tests"));
    NSWindow *ns_window = mac_wm_window_from_handle(window)->ns_window;
    struct
    {
      char *name;
      NSEventModifierFlags flags;
      U16 key_code;
      WM_Key key;
      NSString *characters;
      U32 expected_character;
      WM_Modifiers expected_modifiers;
      B32 repeat;
    } cases[] =
    {
      {"plain",          0,                          0, WM_Key_A, @"a",      'a',    0},
      {"shift",          NSEventModifierFlagShift,   0, WM_Key_A, @"A",      'A',    WM_Modifier_Shift},
      {"option",         NSEventModifierFlagOption,  0, WM_Key_A, @"\u00e5", 0xe5,   WM_Modifier_Alt},
      {"option-shift",   NSEventModifierFlagOption|NSEventModifierFlagShift,
                                                     0, WM_Key_A, @"\u00c5", 0xc5,   WM_Modifier_Alt|WM_Modifier_Shift},
      {"command-shift-p",NSEventModifierFlagCommand|NSEventModifierFlagShift,
                                                    35, WM_Key_P, @"p",      0,      WM_Modifier_Super|WM_Modifier_Shift},
      {"command-f",      NSEventModifierFlagCommand, 3, WM_Key_F, @"f",      0,      WM_Modifier_Super},
      {"control-a",      NSEventModifierFlagControl, 0, WM_Key_A, @"\x01",   0,      WM_Modifier_Ctrl},
      {"control-shift-p",NSEventModifierFlagControl|NSEventModifierFlagShift,
                                                    35, WM_Key_P, @"P",      0,      WM_Modifier_Ctrl|WM_Modifier_Shift},
      // Control/Command always reserve the chord for a shortcut, even when
      // AppKit supplies printable characters alongside Option.
      {"control-option", NSEventModifierFlagControl|NSEventModifierFlagOption,
                                                     0, WM_Key_A, @"\u00e5", 0,      WM_Modifier_Ctrl|WM_Modifier_Alt},
      {"command-option", NSEventModifierFlagCommand|NSEventModifierFlagOption,
                                                     0, WM_Key_A, @"\u00e5", 0,      WM_Modifier_Super|WM_Modifier_Alt},
      {"control-command",NSEventModifierFlagControl|NSEventModifierFlagCommand,
                                                     0, WM_Key_A, @"a",      0,      WM_Modifier_Ctrl|WM_Modifier_Super},
      {"command-arrow",  NSEventModifierFlagCommand|NSEventModifierFlagFunction|NSEventModifierFlagNumericPad,
                                                   123, WM_Key_Left, @"\uf702", 0,   WM_Modifier_Super},
      {"fn-text",        NSEventModifierFlagFunction, 0, WM_Key_A, @"a",      'a',    0},
      {"numpad-text",    NSEventModifierFlagNumericPad,83,WM_Key_Num1, @"1",   '1',    0},
      {"return",         0,                         36, WM_Key_Return, @"\r", 0,     0},
      {"delete",         0,                         51, WM_Key_Backspace, @"\x7f", 0, 0},
      {"left-arrow",     0,                        123, WM_Key_Left, @"\uf702", 0,   0},
      {"repeat-text",    0,                          0, WM_Key_A, @"a",      'a',    0, 1},
      {"repeat-command", NSEventModifierFlagCommand, 3, WM_Key_F, @"f",      0,      WM_Modifier_Super, 1},
    };
    U64 failures = 0;
    for EachElement(idx, cases)
    {
      Temp scratch = scratch_begin(0, 0);
      wm_get_events(scratch.arena, 0);
      // Post into this application's queue, not the system-wide input stream.
      for(U64 release = 0; release < 2; release += 1)
      {
        NSEvent *event = [NSEvent keyEventWithType:release ? NSEventTypeKeyUp : NSEventTypeKeyDown
                                         location:NSZeroPoint
                                    modifierFlags:cases[idx].flags
                                        timestamp:0
                                     windowNumber:[ns_window windowNumber]
                                          context:0
                                       characters:cases[idx].characters
                      charactersIgnoringModifiers:cases[idx].characters
                                        isARepeat:!release && cases[idx].repeat
                                          keyCode:cases[idx].key_code];
        [NSApp postEvent:event atStart:NO];
      }
      WM_EventList events = wm_get_events(scratch.arena, 0);
      U64 presses = 0, releases = 0, texts = 0;
      B32 good = 1;
      for(WM_Event *event = events.first; event != 0; event = event->next)
      {
        if(event->kind == WM_EventKind_Press || event->kind == WM_EventKind_Release)
        {
          B32 release = event->kind == WM_EventKind_Release;
          presses += !release;
          releases += release;
          good &= wm_window_match(event->window, window);
          good &= event->key == cases[idx].key;
          good &= event->modifiers == cases[idx].expected_modifiers;
          good &= event->is_repeat == (!release && cases[idx].repeat);
        }
        else if(event->kind == WM_EventKind_Text)
        {
          texts += 1;
          good &= wm_window_match(event->window, window);
          good &= event->character == cases[idx].expected_character;
        }
      }
      good &= presses == 1 && releases == 1;
      good &= texts == (cases[idx].expected_character != 0);
      good &= !mac_wm_state->key_is_down[cases[idx].key];
      fprintf(stderr, "%s: %s (press=%llu release=%llu text=%llu)\n",
              good ? "PASS" : "FAIL", cases[idx].name, presses, releases, texts);
      failures += !good;
      scratch_end(scratch);
    }
    // Adapted from RAD c4895d6a after fb6f2a2c. Exercise real NSMenuItems,
    // AppKit equivalent matching and the same event queue as the application.
    WM_MenuItem items[] =
    {
      {WM_MenuItemKind_Command, str8_lit("Palette"), str8_lit("open_palette"), WM_Key_P, WM_Modifier_Super|WM_Modifier_Shift},
      {WM_MenuItemKind_Command, str8_lit("Exit"), str8_lit("exit"), WM_Key_Q, WM_Modifier_Super},
      {WM_MenuItemKind_Command, str8_lit("Step"), str8_lit("step"), WM_Key_F11, 0},
      {WM_MenuItemKind_Command, str8_lit("Control"), str8_lit("control"), WM_Key_L, WM_Modifier_Ctrl|WM_Modifier_Alt|WM_Modifier_Shift},
    };
    WM_Menu menu = {str8_lit("Probe"), ArrayCount(items), items};
    WM_MenuArray menus = {1, &menu};
    wm_set_preferred_native_menu_bar(1);
    wm_set_main_menu(menus);
    // The constructor installs the application menu first, then supplied menus.
    NSMenu *native_menu = [[[NSApp mainMenu] itemAtIndex:1] submenu];
    NSMenuItem *palette = [native_menu itemAtIndex:0];
    B32 good = 1;
    MacMenuCheck([[palette keyEquivalent] isEqualToString:@"p"] &&
                 [palette keyEquivalentModifierMask] == (NSEventModifierFlagCommand|NSEventModifierFlagShift));
    NSMenuItem *quit = [[[[NSApp mainMenu] itemAtIndex:0] submenu] itemAtIndex:0];
    MacMenuCheck([[quit keyEquivalent] isEqualToString:@"q"] && [quit keyEquivalentModifierMask] == NSEventModifierFlagCommand);
    MacMenuCheck([[[native_menu itemAtIndex:1] keyEquivalent] characterAtIndex:0] == NSF11FunctionKey);
    MacMenuCheck([[native_menu itemAtIndex:2] keyEquivalentModifierMask] ==
            (NSEventModifierFlagControl|NSEventModifierFlagOption|NSEventModifierFlagShift));
    WM_Window second = wm_window_open(r2f32p(0, 0, 320, 200), 0, str8_lit("Second menu target"));
    for(U64 target = 0; target < 2; target++)
    {
      WM_Window target_window = target ? second : window;
      MAC_WM_Window *target_mac_window = mac_wm_window_from_handle(target_window);
      [target_mac_window->ns_window makeKeyAndOrderFront:0];
      mac_wm_set_focused_window(target_mac_window);
      Temp scratch = scratch_begin(0, 0);
      wm_get_events(scratch.arena, 0);
      NSEvent *key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
                               modifierFlags:NSEventModifierFlagCommand|NSEventModifierFlagShift
                                   timestamp:0 windowNumber:[target_mac_window->ns_window windowNumber]
                                     context:0 characters:@"P" charactersIgnoringModifiers:@"P"
                                   isARepeat:NO keyCode:35];
      // Closed menu: physical press only, no native command or shortcut text.
      [NSApp postEvent:key atStart:NO];
      WM_EventList closed = wm_get_events(scratch.arena, 0);
      U64 presses = 0, commands = 0, texts = 0;
      for(WM_Event *e = closed.first; e; e = e->next)
      {
        presses += e->kind == WM_EventKind_Press;
        commands += e->kind == WM_EventKind_MenuCommand;
        texts += e->kind == WM_EventKind_Text;
      }
      MacMenuCheck(presses == 1 && commands == 0 && texts == 0);
      // A second gesture needs a fresh NSEvent; do not repost the same object
      // that NSApplication has already dequeued for the closed-menu gesture.
      key = [NSEvent keyEventWithType:NSEventTypeKeyDown location:NSZeroPoint
                       modifierFlags:NSEventModifierFlagCommand|NSEventModifierFlagShift
                           timestamp:[[NSProcessInfo processInfo] systemUptime] windowNumber:[target_mac_window->ns_window windowNumber]
                             context:0 characters:@"P" charactersIgnoringModifiers:@"P"
                           isARepeat:NO keyCode:35];
      // Tracking: AppKit matches the equivalent, producing exactly one command.
      [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidBeginTrackingNotification object:[NSApp mainMenu]];
      [mac_wm_state->menu_target menuWillOpen:native_menu];
      WM_EventList tracked = {0};
      if(target == 0)
      {
        MacMenuCheck([native_menu performKeyEquivalent:key]);
        [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidEndTrackingNotification object:[NSApp mainMenu]];
        tracked = wm_get_events(scratch.arena, 0);
      }
      else
      {
        // A nested pump while tracking must give AppKit the key, too.
        [NSApp postEvent:key atStart:NO];
        tracked = wm_get_events(scratch.arena, 0);
        [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidEndTrackingNotification object:[NSApp mainMenu]];
      }
      U64 opens = 0;
      presses = commands = texts = 0;
      for(WM_Event *e = tracked.first; e; e = e->next)
      {
        opens += e->kind == WM_EventKind_MenuOpen;
        presses += e->kind == WM_EventKind_Press;
        texts += e->kind == WM_EventKind_Text;
        if(e->kind == WM_EventKind_MenuCommand)
        {
          commands++;
          MacMenuCheck(opens == 1 && str8_match(e->string, str8_lit("open_palette"), 0));
          MacMenuCheck(wm_window_match(e->window, target_window));
        }
      }
      if(opens != 1 || commands != 1 || presses != 0 || texts != 0)
      {
        fprintf(stderr, "native tracking target=%llu: open=%llu command=%llu press=%llu text=%llu\n",
                target, opens, commands, presses, texts);
      }
      MacMenuCheck(opens == 1 && commands == 1 && presses == 0 && texts == 0);
      // Clicking the same entry reaches the same target/command.
      [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidBeginTrackingNotification object:[NSApp mainMenu]];
      [mac_wm_state->menu_target menuWillOpen:native_menu];
      [native_menu performActionForItemAtIndex:0];
      [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidEndTrackingNotification object:[NSApp mainMenu]];
      tracked = wm_get_events(scratch.arena, 0);
      commands = 0;
      for(WM_Event *e = tracked.first; e; e = e->next)
      {
        if(e->kind == WM_EventKind_MenuCommand)
        {
          commands++;
          MacMenuCheck(str8_match(e->string, str8_lit("open_palette"), 0) && wm_window_match(e->window, target_window));
        }
      }
      MacMenuCheck(commands == 1);
      scratch_end(scratch);
    }
    NSMenu *installed = [NSApp mainMenu];
    [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidBeginTrackingNotification object:installed];
    items[0].shortcut_key = WM_Key_L;
    items[0].shortcut_modifiers = WM_Modifier_Ctrl;
    wm_set_main_menu(menus);
    MacMenuCheck([NSApp mainMenu] == installed && mac_wm_state->pending_main_menu != 0);
    [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidEndTrackingNotification object:installed];
    MacMenuCheck(!mac_wm_state->menu_tracking && mac_wm_state->pending_main_menu == 0);
    palette = [[[[NSApp mainMenu] itemAtIndex:1] submenu] itemAtIndex:0];
    MacMenuCheck([[palette keyEquivalent] isEqualToString:@"l"] && [palette keyEquivalentModifierMask] == NSEventModifierFlagControl);
    // Deactivation must restore ownership even without a close/end callback.
    [[NSNotificationCenter defaultCenter] postNotificationName:NSMenuDidBeginTrackingNotification object:[NSApp mainMenu]];
    items[0].shortcut_key = WM_Key_Null;
    items[0].shortcut_modifiers = 0;
    wm_set_main_menu(menus);
    [[NSNotificationCenter defaultCenter] postNotificationName:NSApplicationDidResignActiveNotification object:NSApp];
    MacMenuCheck(!mac_wm_state->menu_tracking && mac_wm_state->pending_main_menu == 0);
    palette = [[[[NSApp mainMenu] itemAtIndex:1] submenu] itemAtIndex:0];
    MacMenuCheck([[palette keyEquivalent] length] == 0);
    wm_set_preferred_native_menu_bar(0);
    wm_set_main_menu(menus);
    MacMenuCheck([[NSApp mainMenu] numberOfItems] == 1);
    quit = [[[[NSApp mainMenu] itemAtIndex:0] submenu] itemAtIndex:0];
    MacMenuCheck([[quit keyEquivalent] isEqualToString:@"q"]);
    items[1].shortcut_key = WM_Key_Null;
    items[1].shortcut_modifiers = 0;
    wm_set_main_menu(menus);
    quit = [[[[NSApp mainMenu] itemAtIndex:0] submenu] itemAtIndex:0];
    MacMenuCheck([[quit keyEquivalent] length] == 0);
    fprintf(stderr, "%s: native menu bindings, tracking, rebuild and window context\n", good ? "PASS" : "FAIL");
    failures += !good;
    wm_window_close(second);
    wm_window_close(window);
    exit(failures != 0);
  }
}
