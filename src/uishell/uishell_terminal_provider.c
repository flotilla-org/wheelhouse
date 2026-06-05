struct UIShell_TerminalProvider
{
  Arena *arena;
  U64 session_count;
};

struct UIShell_TerminalSession
{
  Arena *arena;
  UIShell_TerminalProvider *provider;
  U16 cols;
  U16 rows;
  F32 cell_width_px;
  F32 cell_height_px;
  UIShell_TerminalDirtyState dirty;
  U64 input_event_count;
  U8 input_buffer[1024];
  U64 input_size;
  U8 status_buffer[256];
  U64 status_size;
  UIShell_TerminalCell *cells;
  U32 *cell_codepoints;
  U64 cell_cap;
};

internal UIShell_TerminalRGB
uishell_terminal_rgb(U8 r, U8 g, U8 b)
{
  UIShell_TerminalRGB result = {r, g, b};
  return result;
}

internal void
uishell_terminal_session_set_status(UIShell_TerminalSession *session, String8 string)
{
  session->status_size = Min(string.size, sizeof(session->status_buffer));
  MemoryCopy(session->status_buffer, string.str, session->status_size);
  if(session->dirty == UIShell_TerminalDirtyState_Clean)
  {
    session->dirty = UIShell_TerminalDirtyState_Partial;
  }
}

internal void
uishell_terminal_session_append_input(UIShell_TerminalSession *session, String8 string)
{
  U64 available_size = sizeof(session->input_buffer) - session->input_size;
  U64 copy_size = Min(available_size, string.size);
  MemoryCopy(session->input_buffer + session->input_size, string.str, copy_size);
  session->input_size += copy_size;
  session->dirty = UIShell_TerminalDirtyState_Partial;
}

internal String8
uishell_terminal_key_name(Arena *arena, WM_Key key)
{
  String8 result = str8_lit("Unknown");
  if(key < WM_Key_COUNT && wm_key_display_name_table[key].size != 0)
  {
    result = wm_key_display_name_table[key];
  }
  else
  {
    result = push_str8f(arena, "Key %u", key);
  }
  return result;
}

internal void
uishell_terminal_session_ensure_cell_cap(UIShell_TerminalSession *session, U64 cap)
{
  if(session->cell_cap < cap)
  {
    session->cells = push_array(session->arena, UIShell_TerminalCell, cap);
    session->cell_codepoints = push_array(session->arena, U32, cap);
    session->cell_cap = cap;
  }
}

internal void
uishell_terminal_session_clear_cells(UIShell_TerminalSession *session)
{
  U64 cell_count = (U64)session->cols*(U64)session->rows;
  UIShell_TerminalRGB fg = uishell_terminal_rgb(188, 209, 191);
  UIShell_TerminalRGB bg = uishell_terminal_rgb(4, 4, 4);
  for(U64 idx = 0; idx < cell_count; idx += 1)
  {
    session->cell_codepoints[idx] = ' ';
    session->cells[idx].graphemes = &session->cell_codepoints[idx];
    session->cells[idx].grapheme_count = 1;
    session->cells[idx].fg = fg;
    session->cells[idx].bg = bg;
    session->cells[idx].flags = 0;
    session->cells[idx].width = 1;
  }
}

internal void
uishell_terminal_session_write_line(UIShell_TerminalSession *session, U64 row, U64 col, String8 string, UIShell_TerminalRGB fg, UIShell_TerminalRGB bg)
{
  if(row < session->rows && col < session->cols)
  {
    U64 x = col;
    for(U64 idx = 0; idx < string.size && x < session->cols;)
    {
      UnicodeDecode decode = utf8_decode(string.str+idx, string.size-idx);
      U32 codepoint = decode.codepoint;
      U64 advance = Max(1, decode.inc);
      idx += advance;
      if(codepoint == 0 || codepoint == max_U32)
      {
        codepoint = '?';
      }
      U64 cell_idx = row*(U64)session->cols + x;
      session->cell_codepoints[cell_idx] = codepoint;
      session->cells[cell_idx].fg = fg;
      session->cells[cell_idx].bg = bg;
      x += 1;
    }
  }
}

internal UIShell_TerminalProvider *
uishell_terminal_mock_provider_open(Arena *arena, UIShell_TerminalProviderDesc *desc)
{
  (void)desc;
  UIShell_TerminalProvider *provider = push_array(arena, UIShell_TerminalProvider, 1);
  provider->arena = arena;
  return provider;
}

internal void
uishell_terminal_mock_provider_close(UIShell_TerminalProvider *provider)
{
  (void)provider;
}

internal UIShell_TerminalSession *
uishell_terminal_session_create(Arena *arena, UIShell_TerminalProvider *provider, UIShell_TerminalSessionDesc *desc)
{
  UIShell_TerminalSession *session = push_array(arena, UIShell_TerminalSession, 1);
  session->arena = arena;
  session->provider = provider;
  session->cols = ClampBot(1, desc->cols);
  session->rows = ClampBot(1, desc->rows);
  session->cell_width_px = desc->cell_width_px;
  session->cell_height_px = desc->cell_height_px;
  session->dirty = UIShell_TerminalDirtyState_Full;
  uishell_terminal_session_set_status(session, str8_lit("mock provider session created"));
  if(provider != 0)
  {
    provider->session_count += 1;
  }
  return session;
}

internal void
uishell_terminal_session_destroy(UIShell_TerminalSession *session)
{
  if(session != 0 && session->provider != 0 && session->provider->session_count != 0)
  {
    session->provider->session_count -= 1;
  }
}

internal B32
uishell_terminal_session_resize(UIShell_TerminalSession *session, U16 cols, U16 rows, F32 cell_w_px, F32 cell_h_px)
{
  B32 result = 0;
  cols = ClampBot(1, cols);
  rows = ClampBot(1, rows);
  if(session->cols != cols || session->rows != rows || session->cell_width_px != cell_w_px || session->cell_height_px != cell_h_px)
  {
    session->cols = cols;
    session->rows = rows;
    session->cell_width_px = cell_w_px;
    session->cell_height_px = cell_h_px;
    session->dirty = UIShell_TerminalDirtyState_Full;
    Temp scratch = scratch_begin(0, 0);
    uishell_terminal_session_set_status(session, push_str8f(scratch.arena, "resized to %ux%u", cols, rows));
    scratch_end(scratch);
    result = 1;
  }
  return result;
}

internal B32
uishell_terminal_session_send_input(UIShell_TerminalSession *session, UIShell_TerminalInputEvent *event)
{
  B32 result = 1;
  Temp scratch = scratch_begin(0, 0);
  session->input_event_count += 1;
  switch(event->kind)
  {
    default:
    {
      result = 0;
    }break;
    case UIShell_TerminalInputKind_Text:
    {
      uishell_terminal_session_append_input(session, event->text);
      uishell_terminal_session_set_status(session, push_str8f(scratch.arena, "text input %I64u bytes", event->text.size));
    }break;
    case UIShell_TerminalInputKind_Key:
    {
      if(event->key == WM_Key_Backspace)
      {
        if(session->input_size != 0)
        {
          session->input_size -= 1;
        }
        uishell_terminal_session_set_status(session, str8_lit("key press: Backspace"));
      }
      else if(event->key == WM_Key_Return)
      {
        session->input_size = 0;
        uishell_terminal_session_set_status(session, str8_lit("key press: Return"));
      }
      else
      {
        uishell_terminal_session_set_status(session, push_str8f(scratch.arena, "key press: %S", uishell_terminal_key_name(scratch.arena, event->key)));
      }
    }break;
    case UIShell_TerminalInputKind_Mouse:
    {
      if(event->wheel_delta_x != 0 || event->wheel_delta_y != 0)
      {
        uishell_terminal_session_set_status(session, push_str8f(scratch.arena, "scroll %.1f %.1f", event->wheel_delta_x, event->wheel_delta_y));
      }
      else
      {
        uishell_terminal_session_set_status(session, push_str8f(scratch.arena, "mouse at %u,%u", event->cell_col, event->cell_row));
      }
    }break;
    case UIShell_TerminalInputKind_Focus:
    {
      uishell_terminal_session_set_status(session, event->enabled ? str8_lit("focus") : str8_lit("blur"));
    }break;
    case UIShell_TerminalInputKind_Paste:
    {
      uishell_terminal_session_append_input(session, event->text);
      uishell_terminal_session_set_status(session, push_str8f(scratch.arena, "paste %I64u bytes", event->text.size));
    }break;
    case UIShell_TerminalInputKind_Resize:
    {
      uishell_terminal_session_resize(session, event->cell_col, event->cell_row, session->cell_width_px, session->cell_height_px);
    }break;
  }
  scratch_end(scratch);
  return result;
}

internal B32
uishell_terminal_session_write_bytes(UIShell_TerminalSession *session, U8 *bytes, U64 size)
{
  uishell_terminal_session_append_input(session, str8(bytes, size));
  Temp scratch = scratch_begin(0, 0);
  uishell_terminal_session_set_status(session, push_str8f(scratch.arena, "raw write %I64u bytes", size));
  scratch_end(scratch);
  return 1;
}

internal UIShell_TerminalDirtyState
uishell_terminal_session_dirty(UIShell_TerminalSession *session)
{
  return session->dirty;
}

internal B32
uishell_terminal_session_snapshot(UIShell_TerminalSession *session, UIShell_TerminalSnapshot *out)
{
  U64 cell_count = (U64)session->cols*(U64)session->rows;
  uishell_terminal_session_ensure_cell_cap(session, cell_count);
  uishell_terminal_session_clear_cells(session);
  
  UIShell_TerminalRGB fg_normal = uishell_terminal_rgb(188, 209, 191);
  UIShell_TerminalRGB fg_dim = uishell_terminal_rgb(126, 142, 131);
  UIShell_TerminalRGB fg_accent = uishell_terminal_rgb(151, 196, 255);
  UIShell_TerminalRGB fg_prompt = uishell_terminal_rgb(255, 220, 118);
  UIShell_TerminalRGB bg_normal = uishell_terminal_rgb(4, 4, 4);
  UIShell_TerminalRGB bg_header = uishell_terminal_rgb(18, 28, 32);
  
  Temp scratch = scratch_begin(0, 0);
  String8 input_string = str8(session->input_buffer, session->input_size);
  String8 status_string = str8(session->status_buffer, session->status_size);
  uishell_terminal_session_write_line(session, 0, 0, str8_lit("uishell terminal"), fg_accent, bg_header);
  uishell_terminal_session_write_line(session, 1, 0, str8_lit("provider: mock terminal provider"), fg_normal, bg_normal);
  uishell_terminal_session_write_line(session, 2, 0, push_str8f(scratch.arena, "grid: %ux%u cells, %.1fx%.1f px", session->cols, session->rows, session->cell_width_px, session->cell_height_px), fg_dim, bg_normal);
  uishell_terminal_session_write_line(session, 3, 0, push_str8f(scratch.arena, "$ %S", input_string), fg_prompt, bg_normal);
  uishell_terminal_session_write_line(session, 4, 0, push_str8f(scratch.arena, "last event: %S", status_string), fg_dim, bg_normal);
  uishell_terminal_session_write_line(session, 5, 0, push_str8f(scratch.arena, "events captured: %I64u", session->input_event_count), fg_dim, bg_normal);
  scratch_end(scratch);
  
  out->cols = session->cols;
  out->rows = session->rows;
  out->cells = session->cells;
  out->cell_count = cell_count;
  out->cursor.col = (U16)Clamp(0, 2 + session->input_size, (U64)(session->cols-1));
  out->cursor.row = Min((U16)3, (U16)(session->rows-1));
  out->cursor.visible = 1;
  out->cursor.style = 0;
  out->cursor.wide_tail = 0;
  out->dirty = session->dirty;
  session->dirty = UIShell_TerminalDirtyState_Clean;
  return 1;
}

internal void
uishell_terminal_session_release_snapshot(UIShell_TerminalSession *session, UIShell_TerminalSnapshot *snapshot)
{
  (void)session;
  MemoryZeroStruct(snapshot);
}
