// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

//- GENERATED CODE

#ifndef WINDOW_MANAGER_META_H
#define WINDOW_MANAGER_META_H

typedef enum WM_Key
{
WM_Key_Null,
WM_Key_Esc,
WM_Key_F1,
WM_Key_F2,
WM_Key_F3,
WM_Key_F4,
WM_Key_F5,
WM_Key_F6,
WM_Key_F7,
WM_Key_F8,
WM_Key_F9,
WM_Key_F10,
WM_Key_F11,
WM_Key_F12,
WM_Key_F13,
WM_Key_F14,
WM_Key_F15,
WM_Key_F16,
WM_Key_F17,
WM_Key_F18,
WM_Key_F19,
WM_Key_F20,
WM_Key_F21,
WM_Key_F22,
WM_Key_F23,
WM_Key_F24,
WM_Key_Tick,
WM_Key_0,
WM_Key_1,
WM_Key_2,
WM_Key_3,
WM_Key_4,
WM_Key_5,
WM_Key_6,
WM_Key_7,
WM_Key_8,
WM_Key_9,
WM_Key_Minus,
WM_Key_Equal,
WM_Key_Backspace,
WM_Key_Tab,
WM_Key_Q,
WM_Key_W,
WM_Key_E,
WM_Key_R,
WM_Key_T,
WM_Key_Y,
WM_Key_U,
WM_Key_I,
WM_Key_O,
WM_Key_P,
WM_Key_LeftBracket,
WM_Key_RightBracket,
WM_Key_BackSlash,
WM_Key_CapsLock,
WM_Key_A,
WM_Key_S,
WM_Key_D,
WM_Key_F,
WM_Key_G,
WM_Key_H,
WM_Key_J,
WM_Key_K,
WM_Key_L,
WM_Key_Semicolon,
WM_Key_Quote,
WM_Key_Return,
WM_Key_Shift,
WM_Key_Z,
WM_Key_X,
WM_Key_C,
WM_Key_V,
WM_Key_B,
WM_Key_N,
WM_Key_M,
WM_Key_Comma,
WM_Key_Period,
WM_Key_Slash,
WM_Key_Ctrl,
WM_Key_Alt,
WM_Key_Space,
WM_Key_Menu,
WM_Key_ScrollLock,
WM_Key_Pause,
WM_Key_Insert,
WM_Key_Home,
WM_Key_PageUp,
WM_Key_Delete,
WM_Key_End,
WM_Key_PageDown,
WM_Key_Up,
WM_Key_Left,
WM_Key_Down,
WM_Key_Right,
WM_Key_Ex0,
WM_Key_Ex1,
WM_Key_Ex2,
WM_Key_Ex3,
WM_Key_Ex4,
WM_Key_Ex5,
WM_Key_Ex6,
WM_Key_Ex7,
WM_Key_Ex8,
WM_Key_Ex9,
WM_Key_Ex10,
WM_Key_Ex11,
WM_Key_Ex12,
WM_Key_Ex13,
WM_Key_Ex14,
WM_Key_Ex15,
WM_Key_Ex16,
WM_Key_Ex17,
WM_Key_Ex18,
WM_Key_Ex19,
WM_Key_Ex20,
WM_Key_Ex21,
WM_Key_Ex22,
WM_Key_Ex23,
WM_Key_Ex24,
WM_Key_Ex25,
WM_Key_Ex26,
WM_Key_Ex27,
WM_Key_Ex28,
WM_Key_Ex29,
WM_Key_NumLock,
WM_Key_NumSlash,
WM_Key_NumStar,
WM_Key_NumMinus,
WM_Key_NumPlus,
WM_Key_NumPeriod,
WM_Key_Num0,
WM_Key_Num1,
WM_Key_Num2,
WM_Key_Num3,
WM_Key_Num4,
WM_Key_Num5,
WM_Key_Num6,
WM_Key_Num7,
WM_Key_Num8,
WM_Key_Num9,
WM_Key_LeftMouseButton,
WM_Key_MiddleMouseButton,
WM_Key_RightMouseButton,
WM_Key_COUNT,
} WM_Key;

#if WM_BACKEND_MULTI
#define WM_HOOK_NAME(name) wm_##name
#define wm_init WM_HOOK_NAME(init)
#define wm_get_system_info WM_HOOK_NAME(get_system_info)
#define wm_apply_clipboard_write WM_HOOK_NAME(apply_clipboard_write)
#define wm_set_clipboard_text_impl WM_HOOK_NAME(set_clipboard_text_impl)
#define wm_get_clipboard_text_impl WM_HOOK_NAME(get_clipboard_text_impl)
#define wm_set_selection_text WM_HOOK_NAME(set_selection_text)
#define wm_get_selection_text WM_HOOK_NAME(get_selection_text)
#define wm_window_open WM_HOOK_NAME(window_open)
#define wm_window_close WM_HOOK_NAME(window_close)
#define wm_window_set_title WM_HOOK_NAME(window_set_title)
#define wm_set_application_icon WM_HOOK_NAME(set_application_icon)
#define wm_window_first_paint WM_HOOK_NAME(window_first_paint)
#define wm_window_focus WM_HOOK_NAME(window_focus)
#define wm_window_is_focused WM_HOOK_NAME(window_is_focused)
#define wm_window_is_fullscreen WM_HOOK_NAME(window_is_fullscreen)
#define wm_window_set_fullscreen WM_HOOK_NAME(window_set_fullscreen)
#define wm_window_is_maximized WM_HOOK_NAME(window_is_maximized)
#define wm_window_set_maximized WM_HOOK_NAME(window_set_maximized)
#define wm_window_is_minimized WM_HOOK_NAME(window_is_minimized)
#define wm_window_set_minimized WM_HOOK_NAME(window_set_minimized)
#define wm_window_bring_to_front WM_HOOK_NAME(window_bring_to_front)
#define wm_window_set_monitor WM_HOOK_NAME(window_set_monitor)
#define wm_window_clear_custom_border_data WM_HOOK_NAME(window_clear_custom_border_data)
#define wm_window_push_custom_title_bar WM_HOOK_NAME(window_push_custom_title_bar)
#define wm_window_push_custom_edges WM_HOOK_NAME(window_push_custom_edges)
#define wm_window_push_custom_title_bar_client_area WM_HOOK_NAME(window_push_custom_title_bar_client_area)
#define wm_window_should_draw_custom_title_bar_controls WM_HOOK_NAME(window_should_draw_custom_title_bar_controls)
#define wm_window_native_title_bar_left_padding WM_HOOK_NAME(window_native_title_bar_left_padding)
#define wm_rect_from_window WM_HOOK_NAME(rect_from_window)
#define wm_client_rect_from_window WM_HOOK_NAME(client_rect_from_window)
#define wm_dpi_from_window WM_HOOK_NAME(dpi_from_window)
#define wm_layout_scale_from_window WM_HOOK_NAME(layout_scale_from_window)
#define wm_backing_scale_from_window WM_HOOK_NAME(backing_scale_from_window)
#define wm_native_surface WM_HOOK_NAME(native_surface)
#define wm_window_presented WM_HOOK_NAME(window_presented)
#define wm_window_frame_ready WM_HOOK_NAME(window_frame_ready)
#define wm_window_frame_pending WM_HOOK_NAME(window_frame_pending)
#define wm_focused_external_window WM_HOOK_NAME(focused_external_window)
#define wm_focus_external_window WM_HOOK_NAME(focus_external_window)
#define wm_push_monitors_array WM_HOOK_NAME(push_monitors_array)
#define wm_primary_monitor WM_HOOK_NAME(primary_monitor)
#define wm_monitor_from_window WM_HOOK_NAME(monitor_from_window)
#define wm_name_from_monitor WM_HOOK_NAME(name_from_monitor)
#define wm_dim_from_monitor WM_HOOK_NAME(dim_from_monitor)
#define wm_dpi_from_monitor WM_HOOK_NAME(dpi_from_monitor)
#define wm_send_wakeup_event WM_HOOK_NAME(send_wakeup_event)
#define wm_get_events WM_HOOK_NAME(get_events)
#define wm_get_modifiers WM_HOOK_NAME(get_modifiers)
#define wm_key_is_down WM_HOOK_NAME(key_is_down)
#define wm_mouse_from_window WM_HOOK_NAME(mouse_from_window)
#define wm_set_main_menu WM_HOOK_NAME(set_main_menu)
#define wm_application_menu_bar_is_native WM_HOOK_NAME(application_menu_bar_is_native)
#define wm_set_preferred_window_decorations WM_HOOK_NAME(set_preferred_window_decorations)
#define wm_set_preferred_native_menu_bar WM_HOOK_NAME(set_preferred_native_menu_bar)
#define wm_set_cursor WM_HOOK_NAME(set_cursor)
#define wm_graphical_message WM_HOOK_NAME(graphical_message)
#define wm_graphical_pick_file WM_HOOK_NAME(graphical_pick_file)
#define wm_show_in_filesystem_ui WM_HOOK_NAME(show_in_filesystem_ui)
#define wm_open_in_browser WM_HOOK_NAME(open_in_browser)
#define wm_reduce_motion WM_HOOK_NAME(reduce_motion)
#define WM_HookDecls \
internal B32 wm_init(CmdLine *cmdln);\
internal WM_SystemInfo * wm_get_system_info(void);\
internal B32 wm_apply_clipboard_write(U32 destination, B32 clear, String8 text);\
internal void wm_set_clipboard_text_impl(String8 string, String8 html);\
internal String8 wm_get_clipboard_text_impl(Arena *arena);\
internal void wm_set_selection_text(String8 string);\
internal String8 wm_get_selection_text(Arena *arena);\
internal WM_Window wm_window_open(Rng2F32 rect, WM_WindowFlags flags, String8 title);\
internal void wm_window_close(WM_Window window);\
internal void wm_window_set_title(WM_Window window, String8 title);\
internal void wm_set_application_icon(Vec2S32 dim, U8 *rgba);\
internal void wm_window_first_paint(WM_Window window);\
internal void wm_window_focus(WM_Window window);\
internal B32 wm_window_is_focused(WM_Window window);\
internal B32 wm_window_is_fullscreen(WM_Window window);\
internal void wm_window_set_fullscreen(WM_Window window, B32 fullscreen);\
internal B32 wm_window_is_maximized(WM_Window window);\
internal void wm_window_set_maximized(WM_Window window, B32 maximized);\
internal B32 wm_window_is_minimized(WM_Window window);\
internal void wm_window_set_minimized(WM_Window window, B32 minimized);\
internal void wm_window_bring_to_front(WM_Window window);\
internal void wm_window_set_monitor(WM_Window window, WM_Monitor monitor);\
internal void wm_window_clear_custom_border_data(WM_Window handle);\
internal void wm_window_push_custom_title_bar(WM_Window handle, F32 thickness);\
internal void wm_window_push_custom_edges(WM_Window handle, F32 thickness);\
internal void wm_window_push_custom_title_bar_client_area(WM_Window handle, Rng2F32 rect);\
internal B32 wm_window_should_draw_custom_title_bar_controls(WM_Window window);\
internal F32 wm_window_native_title_bar_left_padding(WM_Window window);\
internal Rng2F32 wm_rect_from_window(WM_Window window);\
internal Rng2F32 wm_client_rect_from_window(WM_Window window);\
internal F32 wm_dpi_from_window(WM_Window window);\
internal F32 wm_layout_scale_from_window(WM_Window window);\
internal F32 wm_backing_scale_from_window(WM_Window window);\
internal WM_NativeSurface wm_native_surface(WM_Window window);\
internal void wm_window_presented(WM_Window window);\
internal B32 wm_window_frame_ready(WM_Window window);\
internal B32 wm_window_frame_pending(WM_Window window);\
internal WM_ExtWindow wm_focused_external_window(void);\
internal void wm_focus_external_window(WM_ExtWindow ext_window);\
internal WM_MonitorArray wm_push_monitors_array(Arena *arena);\
internal WM_Monitor wm_primary_monitor(void);\
internal WM_Monitor wm_monitor_from_window(WM_Window window);\
internal String8 wm_name_from_monitor(Arena *arena, WM_Monitor monitor);\
internal Vec2F32 wm_dim_from_monitor(WM_Monitor monitor);\
internal F32 wm_dpi_from_monitor(WM_Monitor monitor);\
internal void wm_send_wakeup_event(void);\
internal WM_EventList wm_get_events(Arena *arena, B32 wait);\
internal WM_Modifiers wm_get_modifiers(void);\
internal B32 wm_key_is_down(WM_Key key);\
internal Vec2F32 wm_mouse_from_window(WM_Window window);\
internal void wm_set_main_menu(WM_MenuArray menu_array);\
internal B32 wm_application_menu_bar_is_native(void);\
internal void wm_set_preferred_window_decorations(B32 enabled);\
internal void wm_set_preferred_native_menu_bar(B32 enabled);\
internal void wm_set_cursor(WM_Cursor cursor);\
internal void wm_graphical_message(B32 error, String8 title, String8 message);\
internal String8 wm_graphical_pick_file(Arena *arena, String8 initial_path);\
internal void wm_show_in_filesystem_ui(String8 path);\
internal void wm_open_in_browser(String8 url);\
internal B32 wm_reduce_motion(void);\
/* WM_HookDecls */
#define WM_HookFields \
WM_SystemInfo * (*get_system_info)(void);\
B32 (*apply_clipboard_write)(U32 destination, B32 clear, String8 text);\
void (*set_clipboard_text_impl)(String8 string, String8 html);\
String8 (*get_clipboard_text_impl)(Arena *arena);\
void (*set_selection_text)(String8 string);\
String8 (*get_selection_text)(Arena *arena);\
WM_Window (*window_open)(Rng2F32 rect, WM_WindowFlags flags, String8 title);\
void (*window_close)(WM_Window window);\
void (*window_set_title)(WM_Window window, String8 title);\
void (*set_application_icon)(Vec2S32 dim, U8 *rgba);\
void (*window_first_paint)(WM_Window window);\
void (*window_focus)(WM_Window window);\
B32 (*window_is_focused)(WM_Window window);\
B32 (*window_is_fullscreen)(WM_Window window);\
void (*window_set_fullscreen)(WM_Window window, B32 fullscreen);\
B32 (*window_is_maximized)(WM_Window window);\
void (*window_set_maximized)(WM_Window window, B32 maximized);\
B32 (*window_is_minimized)(WM_Window window);\
void (*window_set_minimized)(WM_Window window, B32 minimized);\
void (*window_bring_to_front)(WM_Window window);\
void (*window_set_monitor)(WM_Window window, WM_Monitor monitor);\
void (*window_clear_custom_border_data)(WM_Window handle);\
void (*window_push_custom_title_bar)(WM_Window handle, F32 thickness);\
void (*window_push_custom_edges)(WM_Window handle, F32 thickness);\
void (*window_push_custom_title_bar_client_area)(WM_Window handle, Rng2F32 rect);\
B32 (*window_should_draw_custom_title_bar_controls)(WM_Window window);\
F32 (*window_native_title_bar_left_padding)(WM_Window window);\
Rng2F32 (*rect_from_window)(WM_Window window);\
Rng2F32 (*client_rect_from_window)(WM_Window window);\
F32 (*dpi_from_window)(WM_Window window);\
F32 (*layout_scale_from_window)(WM_Window window);\
F32 (*backing_scale_from_window)(WM_Window window);\
WM_NativeSurface (*native_surface)(WM_Window window);\
void (*window_presented)(WM_Window window);\
B32 (*window_frame_ready)(WM_Window window);\
B32 (*window_frame_pending)(WM_Window window);\
WM_ExtWindow (*focused_external_window)(void);\
void (*focus_external_window)(WM_ExtWindow ext_window);\
WM_MonitorArray (*push_monitors_array)(Arena *arena);\
WM_Monitor (*primary_monitor)(void);\
WM_Monitor (*monitor_from_window)(WM_Window window);\
String8 (*name_from_monitor)(Arena *arena, WM_Monitor monitor);\
Vec2F32 (*dim_from_monitor)(WM_Monitor monitor);\
F32 (*dpi_from_monitor)(WM_Monitor monitor);\
void (*send_wakeup_event)(void);\
WM_EventList (*get_events)(Arena *arena, B32 wait);\
WM_Modifiers (*get_modifiers)(void);\
B32 (*key_is_down)(WM_Key key);\
Vec2F32 (*mouse_from_window)(WM_Window window);\
void (*set_main_menu)(WM_MenuArray menu_array);\
B32 (*application_menu_bar_is_native)(void);\
void (*set_preferred_window_decorations)(B32 enabled);\
void (*set_preferred_native_menu_bar)(B32 enabled);\
void (*set_cursor)(WM_Cursor cursor);\
void (*graphical_message)(B32 error, String8 title, String8 message);\
String8 (*graphical_pick_file)(Arena *arena, String8 initial_path);\
void (*show_in_filesystem_ui)(String8 path);\
void (*open_in_browser)(String8 url);\
B32 (*reduce_motion)(void);\
/* WM_HookFields */
#define WM_HookTableInit(prefix) \
wm_##prefix##_get_system_info,\
wm_##prefix##_apply_clipboard_write,\
wm_##prefix##_set_clipboard_text_impl,\
wm_##prefix##_get_clipboard_text_impl,\
wm_##prefix##_set_selection_text,\
wm_##prefix##_get_selection_text,\
wm_##prefix##_window_open,\
wm_##prefix##_window_close,\
wm_##prefix##_window_set_title,\
wm_##prefix##_set_application_icon,\
wm_##prefix##_window_first_paint,\
wm_##prefix##_window_focus,\
wm_##prefix##_window_is_focused,\
wm_##prefix##_window_is_fullscreen,\
wm_##prefix##_window_set_fullscreen,\
wm_##prefix##_window_is_maximized,\
wm_##prefix##_window_set_maximized,\
wm_##prefix##_window_is_minimized,\
wm_##prefix##_window_set_minimized,\
wm_##prefix##_window_bring_to_front,\
wm_##prefix##_window_set_monitor,\
wm_##prefix##_window_clear_custom_border_data,\
wm_##prefix##_window_push_custom_title_bar,\
wm_##prefix##_window_push_custom_edges,\
wm_##prefix##_window_push_custom_title_bar_client_area,\
wm_##prefix##_window_should_draw_custom_title_bar_controls,\
wm_##prefix##_window_native_title_bar_left_padding,\
wm_##prefix##_rect_from_window,\
wm_##prefix##_client_rect_from_window,\
wm_##prefix##_dpi_from_window,\
wm_##prefix##_layout_scale_from_window,\
wm_##prefix##_backing_scale_from_window,\
wm_##prefix##_native_surface,\
wm_##prefix##_window_presented,\
wm_##prefix##_window_frame_ready,\
wm_##prefix##_window_frame_pending,\
wm_##prefix##_focused_external_window,\
wm_##prefix##_focus_external_window,\
wm_##prefix##_push_monitors_array,\
wm_##prefix##_primary_monitor,\
wm_##prefix##_monitor_from_window,\
wm_##prefix##_name_from_monitor,\
wm_##prefix##_dim_from_monitor,\
wm_##prefix##_dpi_from_monitor,\
wm_##prefix##_send_wakeup_event,\
wm_##prefix##_get_events,\
wm_##prefix##_get_modifiers,\
wm_##prefix##_key_is_down,\
wm_##prefix##_mouse_from_window,\
wm_##prefix##_set_main_menu,\
wm_##prefix##_application_menu_bar_is_native,\
wm_##prefix##_set_preferred_window_decorations,\
wm_##prefix##_set_preferred_native_menu_bar,\
wm_##prefix##_set_cursor,\
wm_##prefix##_graphical_message,\
wm_##prefix##_graphical_pick_file,\
wm_##prefix##_show_in_filesystem_ui,\
wm_##prefix##_open_in_browser,\
wm_##prefix##_reduce_motion,\
/* WM_HookTableInit */
#endif
C_LINKAGE_BEGIN
extern String8 wm_key_display_name_table[143];
extern String8 wm_key_cfg_name_table[143];

C_LINKAGE_END

#endif // WINDOW_MANAGER_META_H
