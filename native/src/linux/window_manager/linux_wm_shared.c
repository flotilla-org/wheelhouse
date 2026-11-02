// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Keys

// X11 keysyms and xkbcommon's are the same numbers.
internal WM_Key
lnx_wm_key_from_keysym(U32 keysym, B32 *right_sided)
{
  WM_Key key = WM_Key_Null;
  switch(keysym)
  {
    default:
    {
      if(0){}
      else if(XK_F1 <= keysym && keysym <= XK_F24) { key = (WM_Key)(WM_Key_F1 + (keysym - XK_F1)); }
      else if('0' <= keysym && keysym <= '9')      { key = WM_Key_0 + (keysym-'0'); }
    }break;
    case XK_Escape:{key = WM_Key_Esc;};break;
    case XK_ISO_Left_Tab:{key = WM_Key_Tab;}break;
    case XK_Caps_Lock:{key = WM_Key_CapsLock;}break;
    case XK_Menu:{key = WM_Key_Menu;}break;
    case XK_Scroll_Lock:{key = WM_Key_ScrollLock;}break;
    case XK_Insert:{key = WM_Key_Insert;}break;
    case XK_Num_Lock:{key = WM_Key_NumLock;}break;
    case XK_KP_Divide:{key = WM_Key_NumSlash;}break;
    case XK_KP_Multiply:{key = WM_Key_NumStar;}break;
    case XK_KP_Subtract:{key = WM_Key_NumMinus;}break;
    case XK_KP_Add:{key = WM_Key_NumPlus;}break;
    case XK_KP_Decimal:{key = WM_Key_NumPeriod;}break;
    case XK_KP_Enter:{key = WM_Key_Return;}break;
    case XK_KP_0:case XK_KP_Insert:{key = WM_Key_Num0;}break;
    case XK_KP_1:case XK_KP_End:{key = WM_Key_Num1;}break;
    case XK_KP_2:case XK_KP_Down:{key = WM_Key_Num2;}break;
    case XK_KP_3:case XK_KP_Page_Down:{key = WM_Key_Num3;}break;
    case XK_KP_4:case XK_KP_Left:{key = WM_Key_Num4;}break;
    case XK_KP_5:case XK_KP_Begin:{key = WM_Key_Num5;}break;
    case XK_KP_6:case XK_KP_Right:{key = WM_Key_Num6;}break;
    case XK_KP_7:case XK_KP_Home:{key = WM_Key_Num7;}break;
    case XK_KP_8:case XK_KP_Up:{key = WM_Key_Num8;}break;
    case XK_KP_9:case XK_KP_Page_Up:{key = WM_Key_Num9;}break;
    case XK_KP_Delete:{key = WM_Key_NumPeriod;}break;
    case '`':case '~':{key = WM_Key_Tick;}break;
    case XK_BackSpace:{key = WM_Key_Backspace;}break;
    case XK_Delete:{key = WM_Key_Delete;}break;
    case XK_Return:{key = WM_Key_Return;}break;
    case XK_Pause:{key = WM_Key_Pause;}break;
    case XK_Tab:{key = WM_Key_Tab;}break;
    case XK_Left:{key = WM_Key_Left;}break;
    case XK_Right:{key = WM_Key_Right;}break;
    case XK_Up:{key = WM_Key_Up;}break;
    case XK_Down:{key = WM_Key_Down;}break;
    case XK_Home:{key = WM_Key_Home;}break;
    case XK_End:{key = WM_Key_End;}break;
    case XK_Page_Up:{key = WM_Key_PageUp;}break;
    case XK_Page_Down:{key = WM_Key_PageDown;}break;
    case XK_Alt_L:{ key = WM_Key_Alt; }break;
    case XK_Alt_R:{ key = WM_Key_Alt; *right_sided = 1;}break;
    case XK_Shift_L:{ key = WM_Key_Shift; }break;
    case XK_Shift_R:{ key = WM_Key_Shift; *right_sided = 1;}break;
    case XK_Control_L:{ key = WM_Key_Ctrl; }break;
    case XK_Control_R:{ key = WM_Key_Ctrl; *right_sided = 1;}break;
    case '-':{key = WM_Key_Minus;}break;
    case '=':{key = WM_Key_Equal;}break;
    case '[':{key = WM_Key_LeftBracket;}break;
    case ']':{key = WM_Key_RightBracket;}break;
    case ';':{key = WM_Key_Semicolon;}break;
    case '\'':{key = WM_Key_Quote;}break;
    case '.':{key = WM_Key_Period;}break;
    case ',':{key = WM_Key_Comma;}break;
    case '/':{key = WM_Key_Slash;}break;
    case '\\':{key = WM_Key_BackSlash;}break;
    case '\t':{key = WM_Key_Tab;}break;
    case 'a':case 'A':{key = WM_Key_A;}break;
    case 'b':case 'B':{key = WM_Key_B;}break;
    case 'c':case 'C':{key = WM_Key_C;}break;
    case 'd':case 'D':{key = WM_Key_D;}break;
    case 'e':case 'E':{key = WM_Key_E;}break;
    case 'f':case 'F':{key = WM_Key_F;}break;
    case 'g':case 'G':{key = WM_Key_G;}break;
    case 'h':case 'H':{key = WM_Key_H;}break;
    case 'i':case 'I':{key = WM_Key_I;}break;
    case 'j':case 'J':{key = WM_Key_J;}break;
    case 'k':case 'K':{key = WM_Key_K;}break;
    case 'l':case 'L':{key = WM_Key_L;}break;
    case 'm':case 'M':{key = WM_Key_M;}break;
    case 'n':case 'N':{key = WM_Key_N;}break;
    case 'o':case 'O':{key = WM_Key_O;}break;
    case 'p':case 'P':{key = WM_Key_P;}break;
    case 'q':case 'Q':{key = WM_Key_Q;}break;
    case 'r':case 'R':{key = WM_Key_R;}break;
    case 's':case 'S':{key = WM_Key_S;}break;
    case 't':case 'T':{key = WM_Key_T;}break;
    case 'u':case 'U':{key = WM_Key_U;}break;
    case 'v':case 'V':{key = WM_Key_V;}break;
    case 'w':case 'W':{key = WM_Key_W;}break;
    case 'x':case 'X':{key = WM_Key_X;}break;
    case 'y':case 'Y':{key = WM_Key_Y;}break;
    case 'z':case 'Z':{key = WM_Key_Z;}break;
    case ' ':{key = WM_Key_Space;}break;
  }
  return key;
}

////////////////////////////////
//~ Custom Borders

// The window manager draws no frame around a custom-border window, so the
// edges and title bar the shell reports stand in for it: a press there hands
// the drag to the window manager (X11: _NET_WM_MOVERESIZE; Wayland:
// xdg_toplevel.move and .resize). -1 is client area.
internal int
lnx_wm_moveresize_code_from_border(Rng2F32 window_rect, F32 edge, F32 title_bar_thickness, B32 maximized, B32 in_client_area, Vec2F32 pos)
{
  Rng2F32 inside_edges = pad_2f32(window_rect, -edge);
  Rng2F32 title_bar = r2f32p(inside_edges.x0, inside_edges.y0, inside_edges.x1, inside_edges.y0 + title_bar_thickness);
  if(!contains_2f32(window_rect, pos)) { return -1; }
  if(contains_2f32(inside_edges, pos) && !contains_2f32(title_bar, pos)) { return -1; }
  if(in_client_area) { return -1; }
  if(contains_2f32(title_bar, pos)) { return _NET_WM_MOVERESIZE_MOVE; }
  // A maximized window has no edges to drag; they act as title bar.
  if(maximized) { return _NET_WM_MOVERESIZE_MOVE; }
  B32 left = pos.x < inside_edges.x0, right = pos.x >= inside_edges.x1;
  B32 top = pos.y < inside_edges.y0, bottom = pos.y >= inside_edges.y1;
  if(top && left)     { return _NET_WM_MOVERESIZE_SIZE_TOPLEFT; }
  if(top && right)    { return _NET_WM_MOVERESIZE_SIZE_TOPRIGHT; }
  if(bottom && left)  { return _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT; }
  if(bottom && right) { return _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT; }
  if(left)            { return _NET_WM_MOVERESIZE_SIZE_LEFT; }
  if(right)           { return _NET_WM_MOVERESIZE_SIZE_RIGHT; }
  if(top)             { return _NET_WM_MOVERESIZE_SIZE_TOP; }
  return _NET_WM_MOVERESIZE_SIZE_BOTTOM;
}

// The cursor a custom border shows over an edge.
internal WM_Cursor
lnx_wm_cursor_from_moveresize_code(int code, WM_Cursor otherwise)
{
  switch(code)
  {
    default:{}break;
    case _NET_WM_MOVERESIZE_SIZE_TOPLEFT:
    case _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT:{return WM_Cursor_DownRight;}
    case _NET_WM_MOVERESIZE_SIZE_TOPRIGHT:
    case _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT:{return WM_Cursor_UpRight;}
    case _NET_WM_MOVERESIZE_SIZE_LEFT:
    case _NET_WM_MOVERESIZE_SIZE_RIGHT:{return WM_Cursor_LeftRight;}
    case _NET_WM_MOVERESIZE_SIZE_TOP:
    case _NET_WM_MOVERESIZE_SIZE_BOTTOM:{return WM_Cursor_UpDown;}
  }
  return otherwise;
}

////////////////////////////////
//~ Shell Operations

internal void *
lnx_wm_reap_browser(void *data)
{
  pid_t pid = (pid_t)(intptr_t)data;
  while(waitpid(pid, 0, 0) < 0 && errno == EINTR) {}
  return 0;
}

internal void
lnx_wm_open_in_browser(String8 url)
{
  Temp scratch = scratch_begin(0, 0);
  String8 copy = push_str8_copy(scratch.arena, url);
  char *argv[] = {"xdg-open", (char *)copy.str, 0};
  pid_t pid = 0;
  // URI is one argv element, never shell source.
  if(posix_spawnp(&pid, "xdg-open", 0, 0, argv, environ) == 0)
  {
    pthread_t reaper;
    if(pthread_create(&reaper, 0, lnx_wm_reap_browser, (void *)(intptr_t)pid) == 0) { pthread_detach(reaper); }
    else { lnx_wm_reap_browser((void *)(intptr_t)pid); }
  }
  scratch_end(scratch);
}
