// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- GENERATED CODE

#if WM_BACKEND_MULTI
internal WM_SystemInfo * wm_get_system_info(void) { return wm_backend->get_system_info( ); }
internal B32 wm_apply_clipboard_write(U32 destination, B32 clear, String8 text) { return wm_backend->apply_clipboard_write(destination, clear, text); }
internal void wm_set_clipboard_text_impl(String8 string, String8 html) { return wm_backend->set_clipboard_text_impl(string, html); }
internal String8 wm_get_clipboard_text_impl(Arena *arena) { return wm_backend->get_clipboard_text_impl(arena); }
internal void wm_set_selection_text(String8 string) { return wm_backend->set_selection_text(string); }
internal String8 wm_get_selection_text(Arena *arena) { return wm_backend->get_selection_text(arena); }
internal WM_Window wm_window_open(Rng2F32 rect, WM_WindowFlags flags, String8 title) { return wm_backend->window_open(rect, flags, title); }
internal void wm_window_close(WM_Window window) { return wm_backend->window_close(window); }
internal void wm_window_set_title(WM_Window window, String8 title) { return wm_backend->window_set_title(window, title); }
internal void wm_set_application_icon(Vec2S32 dim, U8 *rgba) { return wm_backend->set_application_icon(dim, rgba); }
internal void wm_window_first_paint(WM_Window window) { return wm_backend->window_first_paint(window); }
internal void wm_window_focus(WM_Window window) { return wm_backend->window_focus(window); }
internal B32 wm_window_is_focused(WM_Window window) { return wm_backend->window_is_focused(window); }
internal B32 wm_window_is_fullscreen(WM_Window window) { return wm_backend->window_is_fullscreen(window); }
internal void wm_window_set_fullscreen(WM_Window window, B32 fullscreen) { return wm_backend->window_set_fullscreen(window, fullscreen); }
internal B32 wm_window_is_maximized(WM_Window window) { return wm_backend->window_is_maximized(window); }
internal void wm_window_set_maximized(WM_Window window, B32 maximized) { return wm_backend->window_set_maximized(window, maximized); }
internal B32 wm_window_is_minimized(WM_Window window) { return wm_backend->window_is_minimized(window); }
internal void wm_window_set_minimized(WM_Window window, B32 minimized) { return wm_backend->window_set_minimized(window, minimized); }
internal void wm_window_bring_to_front(WM_Window window) { return wm_backend->window_bring_to_front(window); }
internal void wm_window_set_monitor(WM_Window window, WM_Monitor monitor) { return wm_backend->window_set_monitor(window, monitor); }
internal void wm_window_clear_custom_border_data(WM_Window handle) { return wm_backend->window_clear_custom_border_data(handle); }
internal void wm_window_push_custom_title_bar(WM_Window handle, F32 thickness) { return wm_backend->window_push_custom_title_bar(handle, thickness); }
internal void wm_window_push_custom_edges(WM_Window handle, F32 thickness) { return wm_backend->window_push_custom_edges(handle, thickness); }
internal void wm_window_push_custom_title_bar_client_area(WM_Window handle, Rng2F32 rect) { return wm_backend->window_push_custom_title_bar_client_area(handle, rect); }
internal B32 wm_window_should_draw_custom_title_bar_controls(WM_Window window) { return wm_backend->window_should_draw_custom_title_bar_controls(window); }
internal F32 wm_window_native_title_bar_left_padding(WM_Window window) { return wm_backend->window_native_title_bar_left_padding(window); }
internal Rng2F32 wm_rect_from_window(WM_Window window) { return wm_backend->rect_from_window(window); }
internal Rng2F32 wm_client_rect_from_window(WM_Window window) { return wm_backend->client_rect_from_window(window); }
internal F32 wm_dpi_from_window(WM_Window window) { return wm_backend->dpi_from_window(window); }
internal F32 wm_layout_scale_from_window(WM_Window window) { return wm_backend->layout_scale_from_window(window); }
internal F32 wm_backing_scale_from_window(WM_Window window) { return wm_backend->backing_scale_from_window(window); }
internal WM_NativeSurface wm_native_surface(WM_Window window) { return wm_backend->native_surface(window); }
internal void wm_window_presented(WM_Window window) { return wm_backend->window_presented(window); }
internal B32 wm_window_frame_ready(WM_Window window) { return wm_backend->window_frame_ready(window); }
internal B32 wm_window_frame_pending(WM_Window window) { return wm_backend->window_frame_pending(window); }
internal WM_ExtWindow wm_focused_external_window(void) { return wm_backend->focused_external_window( ); }
internal void wm_focus_external_window(WM_ExtWindow ext_window) { return wm_backend->focus_external_window(ext_window); }
internal WM_MonitorArray wm_push_monitors_array(Arena *arena) { return wm_backend->push_monitors_array(arena); }
internal WM_Monitor wm_primary_monitor(void) { return wm_backend->primary_monitor( ); }
internal WM_Monitor wm_monitor_from_window(WM_Window window) { return wm_backend->monitor_from_window(window); }
internal String8 wm_name_from_monitor(Arena *arena, WM_Monitor monitor) { return wm_backend->name_from_monitor(arena, monitor); }
internal Vec2F32 wm_dim_from_monitor(WM_Monitor monitor) { return wm_backend->dim_from_monitor(monitor); }
internal F32 wm_dpi_from_monitor(WM_Monitor monitor) { return wm_backend->dpi_from_monitor(monitor); }
internal void wm_send_wakeup_event(void) { return wm_backend->send_wakeup_event( ); }
internal WM_EventList wm_get_events(Arena *arena, B32 wait) { return wm_backend->get_events(arena, wait); }
internal WM_Modifiers wm_get_modifiers(void) { return wm_backend->get_modifiers( ); }
internal B32 wm_key_is_down(WM_Key key) { return wm_backend->key_is_down(key); }
internal Vec2F32 wm_mouse_from_window(WM_Window window) { return wm_backend->mouse_from_window(window); }
internal void wm_set_main_menu(WM_MenuArray menu_array) { return wm_backend->set_main_menu(menu_array); }
internal B32 wm_application_menu_bar_is_native(void) { return wm_backend->application_menu_bar_is_native( ); }
internal void wm_set_preferred_window_decorations(B32 enabled) { return wm_backend->set_preferred_window_decorations(enabled); }
internal void wm_set_preferred_native_menu_bar(B32 enabled) { return wm_backend->set_preferred_native_menu_bar(enabled); }
internal void wm_set_cursor(WM_Cursor cursor) { return wm_backend->set_cursor(cursor); }
internal void wm_graphical_message(B32 error, String8 title, String8 message) { return wm_backend->graphical_message(error, title, message); }
internal String8 wm_graphical_pick_file(Arena *arena, String8 initial_path) { return wm_backend->graphical_pick_file(arena, initial_path); }
internal void wm_show_in_filesystem_ui(String8 path) { return wm_backend->show_in_filesystem_ui(path); }
internal void wm_open_in_browser(String8 url) { return wm_backend->open_in_browser(url); }
internal B32 wm_reduce_motion(void) { return wm_backend->reduce_motion( ); }
#endif
C_LINKAGE_BEGIN
String8 wm_key_display_name_table[143] =
{
str8_lit_comp("Invalid Key"),
str8_lit_comp("Escape"),
str8_lit_comp("F1"),
str8_lit_comp("F2"),
str8_lit_comp("F3"),
str8_lit_comp("F4"),
str8_lit_comp("F5"),
str8_lit_comp("F6"),
str8_lit_comp("F7"),
str8_lit_comp("F8"),
str8_lit_comp("F9"),
str8_lit_comp("F10"),
str8_lit_comp("F11"),
str8_lit_comp("F12"),
str8_lit_comp("F13"),
str8_lit_comp("F14"),
str8_lit_comp("F15"),
str8_lit_comp("F16"),
str8_lit_comp("F17"),
str8_lit_comp("F18"),
str8_lit_comp("F19"),
str8_lit_comp("F20"),
str8_lit_comp("F21"),
str8_lit_comp("F22"),
str8_lit_comp("F23"),
str8_lit_comp("F24"),
str8_lit_comp("Tick"),
str8_lit_comp("0"),
str8_lit_comp("1"),
str8_lit_comp("2"),
str8_lit_comp("3"),
str8_lit_comp("4"),
str8_lit_comp("5"),
str8_lit_comp("6"),
str8_lit_comp("7"),
str8_lit_comp("8"),
str8_lit_comp("9"),
str8_lit_comp("Minus"),
str8_lit_comp("Equal"),
str8_lit_comp("Backspace"),
str8_lit_comp("Tab"),
str8_lit_comp("Q"),
str8_lit_comp("W"),
str8_lit_comp("E"),
str8_lit_comp("R"),
str8_lit_comp("T"),
str8_lit_comp("Y"),
str8_lit_comp("U"),
str8_lit_comp("I"),
str8_lit_comp("O"),
str8_lit_comp("P"),
str8_lit_comp("Left Bracket"),
str8_lit_comp("Right Bracket"),
str8_lit_comp("Back Slash"),
str8_lit_comp("Caps Lock"),
str8_lit_comp("A"),
str8_lit_comp("S"),
str8_lit_comp("D"),
str8_lit_comp("F"),
str8_lit_comp("G"),
str8_lit_comp("H"),
str8_lit_comp("J"),
str8_lit_comp("K"),
str8_lit_comp("L"),
str8_lit_comp("Semicolon"),
str8_lit_comp("Quote"),
str8_lit_comp("Return"),
str8_lit_comp("Shift"),
str8_lit_comp("Z"),
str8_lit_comp("X"),
str8_lit_comp("C"),
str8_lit_comp("V"),
str8_lit_comp("B"),
str8_lit_comp("N"),
str8_lit_comp("M"),
str8_lit_comp("Comma"),
str8_lit_comp("Period"),
str8_lit_comp("Slash"),
str8_lit_comp("Ctrl"),
str8_lit_comp("Alt"),
str8_lit_comp("Space"),
str8_lit_comp("Menu"),
str8_lit_comp("Scroll Lock"),
str8_lit_comp("Pause"),
str8_lit_comp("Insert"),
str8_lit_comp("Home"),
str8_lit_comp("Page Up"),
str8_lit_comp("Delete"),
str8_lit_comp("End"),
str8_lit_comp("Page Down"),
str8_lit_comp("Up"),
str8_lit_comp("Left"),
str8_lit_comp("Down"),
str8_lit_comp("Right"),
str8_lit_comp("Ex0"),
str8_lit_comp("Ex1"),
str8_lit_comp("Ex2"),
str8_lit_comp("Ex3"),
str8_lit_comp("Ex4"),
str8_lit_comp("Ex5"),
str8_lit_comp("Ex6"),
str8_lit_comp("Ex7"),
str8_lit_comp("Ex8"),
str8_lit_comp("Ex9"),
str8_lit_comp("Ex10"),
str8_lit_comp("Ex11"),
str8_lit_comp("Ex12"),
str8_lit_comp("Ex13"),
str8_lit_comp("Ex14"),
str8_lit_comp("Ex15"),
str8_lit_comp("Ex16"),
str8_lit_comp("Ex17"),
str8_lit_comp("Ex18"),
str8_lit_comp("Ex19"),
str8_lit_comp("Ex20"),
str8_lit_comp("Ex21"),
str8_lit_comp("Ex22"),
str8_lit_comp("Ex23"),
str8_lit_comp("Ex24"),
str8_lit_comp("Ex25"),
str8_lit_comp("Ex26"),
str8_lit_comp("Ex27"),
str8_lit_comp("Ex28"),
str8_lit_comp("Ex29"),
str8_lit_comp("Num Lock"),
str8_lit_comp("Numpad Slash"),
str8_lit_comp("Numpad Star"),
str8_lit_comp("Numpad Minus"),
str8_lit_comp("Numpad Plus"),
str8_lit_comp("Numpad Period"),
str8_lit_comp("Numpad 0"),
str8_lit_comp("Numpad 1"),
str8_lit_comp("Numpad 2"),
str8_lit_comp("Numpad 3"),
str8_lit_comp("Numpad 4"),
str8_lit_comp("Numpad 5"),
str8_lit_comp("Numpad 6"),
str8_lit_comp("Numpad 7"),
str8_lit_comp("Numpad 8"),
str8_lit_comp("Numpad 9"),
str8_lit_comp("Left Mouse Button"),
str8_lit_comp("Middle Mouse Button"),
str8_lit_comp("Right Mouse Button"),
};

String8 wm_key_cfg_name_table[143] =
{
str8_lit_comp("null"),
str8_lit_comp("esc"),
str8_lit_comp("f1"),
str8_lit_comp("f2"),
str8_lit_comp("f3"),
str8_lit_comp("f4"),
str8_lit_comp("f5"),
str8_lit_comp("f6"),
str8_lit_comp("f7"),
str8_lit_comp("f8"),
str8_lit_comp("f9"),
str8_lit_comp("f10"),
str8_lit_comp("f11"),
str8_lit_comp("f12"),
str8_lit_comp("f13"),
str8_lit_comp("f14"),
str8_lit_comp("f15"),
str8_lit_comp("f16"),
str8_lit_comp("f17"),
str8_lit_comp("f18"),
str8_lit_comp("f19"),
str8_lit_comp("f20"),
str8_lit_comp("f21"),
str8_lit_comp("f22"),
str8_lit_comp("f23"),
str8_lit_comp("f24"),
str8_lit_comp("tick"),
str8_lit_comp("0"),
str8_lit_comp("1"),
str8_lit_comp("2"),
str8_lit_comp("3"),
str8_lit_comp("4"),
str8_lit_comp("5"),
str8_lit_comp("6"),
str8_lit_comp("7"),
str8_lit_comp("8"),
str8_lit_comp("9"),
str8_lit_comp("minus"),
str8_lit_comp("equal"),
str8_lit_comp("backspace"),
str8_lit_comp("tab"),
str8_lit_comp("q"),
str8_lit_comp("w"),
str8_lit_comp("e"),
str8_lit_comp("r"),
str8_lit_comp("t"),
str8_lit_comp("y"),
str8_lit_comp("u"),
str8_lit_comp("i"),
str8_lit_comp("o"),
str8_lit_comp("p"),
str8_lit_comp("left_bracket"),
str8_lit_comp("right_bracket"),
str8_lit_comp("backslash"),
str8_lit_comp("caps_lock"),
str8_lit_comp("a"),
str8_lit_comp("s"),
str8_lit_comp("d"),
str8_lit_comp("f"),
str8_lit_comp("g"),
str8_lit_comp("h"),
str8_lit_comp("j"),
str8_lit_comp("k"),
str8_lit_comp("l"),
str8_lit_comp("semicolon"),
str8_lit_comp("quote"),
str8_lit_comp("return"),
str8_lit_comp("shift"),
str8_lit_comp("z"),
str8_lit_comp("x"),
str8_lit_comp("c"),
str8_lit_comp("v"),
str8_lit_comp("b"),
str8_lit_comp("n"),
str8_lit_comp("m"),
str8_lit_comp("comma"),
str8_lit_comp("period"),
str8_lit_comp("slash"),
str8_lit_comp("ctrl"),
str8_lit_comp("alt"),
str8_lit_comp("space"),
str8_lit_comp("menu"),
str8_lit_comp("scroll_lock"),
str8_lit_comp("pause"),
str8_lit_comp("insert"),
str8_lit_comp("home"),
str8_lit_comp("page_up"),
str8_lit_comp("delete"),
str8_lit_comp("end"),
str8_lit_comp("page_down"),
str8_lit_comp("up"),
str8_lit_comp("left"),
str8_lit_comp("down"),
str8_lit_comp("right"),
str8_lit_comp("ex0"),
str8_lit_comp("ex1"),
str8_lit_comp("ex2"),
str8_lit_comp("ex3"),
str8_lit_comp("ex4"),
str8_lit_comp("ex5"),
str8_lit_comp("ex6"),
str8_lit_comp("ex7"),
str8_lit_comp("ex8"),
str8_lit_comp("ex9"),
str8_lit_comp("ex10"),
str8_lit_comp("ex11"),
str8_lit_comp("ex12"),
str8_lit_comp("ex13"),
str8_lit_comp("ex14"),
str8_lit_comp("ex15"),
str8_lit_comp("ex16"),
str8_lit_comp("ex17"),
str8_lit_comp("ex18"),
str8_lit_comp("ex19"),
str8_lit_comp("ex20"),
str8_lit_comp("ex21"),
str8_lit_comp("ex22"),
str8_lit_comp("ex23"),
str8_lit_comp("ex24"),
str8_lit_comp("ex25"),
str8_lit_comp("ex26"),
str8_lit_comp("ex27"),
str8_lit_comp("ex28"),
str8_lit_comp("ex29"),
str8_lit_comp("num_lock"),
str8_lit_comp("numpad_slash"),
str8_lit_comp("numpad_star"),
str8_lit_comp("numpad_minus"),
str8_lit_comp("numpad_plus"),
str8_lit_comp("numpad_period"),
str8_lit_comp("numpad_0"),
str8_lit_comp("numpad_1"),
str8_lit_comp("numpad_2"),
str8_lit_comp("numpad_3"),
str8_lit_comp("numpad_4"),
str8_lit_comp("numpad_5"),
str8_lit_comp("numpad_6"),
str8_lit_comp("numpad_7"),
str8_lit_comp("numpad_8"),
str8_lit_comp("numpad_9"),
str8_lit_comp("left_mouse"),
str8_lit_comp("middle_mouse"),
str8_lit_comp("right_mouse"),
};

C_LINKAGE_END
