struct CleatProvider
{
  Arena *arena;
  U64 session_count;
};

struct CleatSession
{
  Arena *arena;
  cleat_provider *provider;
  U16 cols;
  U16 rows;
  F32 cell_width_px;
  F32 cell_height_px;
  cleat_dirty_state dirty;
  U64 input_event_count;
  U8 input_buffer[1024];
  U64 input_size;
  U8 status_buffer[256];
  U64 status_size;
  cleat_cell *cells;
  U32 *cell_codepoints;
  U64 cell_cap;
};

internal cleat_rgb
cleat_mock_rgb(U8 r, U8 g, U8 b)
{
  cleat_rgb result = {r, g, b};
  return result;
}

internal void
cleat_mock_session_set_status(cleat_session *session, String8 string)
{
  session->status_size = Min(string.size, sizeof(session->status_buffer));
  MemoryCopy(session->status_buffer, string.str, session->status_size);
  if(session->dirty == CLEAT_DIRTY_CLEAN)
  {
    session->dirty = CLEAT_DIRTY_PARTIAL;
  }
}

internal void
cleat_mock_session_append_input(cleat_session *session, String8 string)
{
  U64 available_size = sizeof(session->input_buffer) - session->input_size;
  U64 copy_size = Min(available_size, string.size);
  MemoryCopy(session->input_buffer + session->input_size, string.str, copy_size);
  session->input_size += copy_size;
  session->dirty = CLEAT_DIRTY_PARTIAL;
}

internal String8
cleat_mock_key_name(Arena *arena, U32 key_code)
{
  String8 result = str8_lit("Unknown");
  switch(key_code)
  {
    case CLEAT_KEY_ENTER:       { result = str8_lit("Return"); }break;
    case CLEAT_KEY_ESCAPE:      { result = str8_lit("Escape"); }break;
    case CLEAT_KEY_BACKSPACE:   { result = str8_lit("Backspace"); }break;
    case CLEAT_KEY_TAB:         { result = str8_lit("Tab"); }break;
    case CLEAT_KEY_DELETE:      { result = str8_lit("Delete"); }break;
    case CLEAT_KEY_ARROW_UP:    { result = str8_lit("Arrow Up"); }break;
    case CLEAT_KEY_ARROW_DOWN:  { result = str8_lit("Arrow Down"); }break;
    case CLEAT_KEY_ARROW_LEFT:  { result = str8_lit("Arrow Left"); }break;
    case CLEAT_KEY_ARROW_RIGHT: { result = str8_lit("Arrow Right"); }break;
    default:                   { result = push_str8f(arena, "Key %u", key_code); }break;
  }
  return result;
}

internal void
cleat_mock_session_ensure_cell_cap(cleat_session *session, U64 cap)
{
  if(session->cell_cap < cap)
  {
    session->cells = push_array(session->arena, cleat_cell, cap);
    session->cell_codepoints = push_array(session->arena, U32, cap);
    session->cell_cap = cap;
  }
}

internal void
cleat_mock_session_clear_cells(cleat_session *session)
{
  U64 cell_count = (U64)session->cols*(U64)session->rows;
  cleat_rgb fg = cleat_mock_rgb(188, 209, 191);
  cleat_rgb bg = cleat_mock_rgb(4, 4, 4);
  for(U64 idx = 0; idx < cell_count; idx += 1)
  {
    session->cell_codepoints[idx] = ' ';
    session->cells[idx].graphemes = &session->cell_codepoints[idx];
    session->cells[idx].grapheme_count = 1;
    session->cells[idx].fg = fg;
    session->cells[idx].bg = bg;
    session->cells[idx].flags = 0;
    session->cells[idx].width = CLEAT_CELL_WIDTH_NARROW;
  }
}

internal void
cleat_mock_session_write_line(cleat_session *session, U64 row, U64 col, String8 string, cleat_rgb fg, cleat_rgb bg)
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

internal U32
cleat_provider_abi_version(void)
{
  return CLEAT_PROVIDER_ABI_VERSION;
}

internal cleat_provider *
cleat_provider_open(cleat_provider_desc const *desc)
{
  if(desc != 0 && desc->abi_version != 0 && desc->abi_version != CLEAT_PROVIDER_ABI_VERSION)
  {
    return 0;
  }
  cleat_provider *provider = push_array(rd_view_arena(), cleat_provider, 1);
  provider->arena = rd_view_arena();
  return provider;
}

internal void
cleat_provider_close(cleat_provider *provider)
{
  (void)provider;
}

internal cleat_session *
cleat_session_create(cleat_provider *provider, cleat_session_desc const *desc)
{
  Arena *arena = rd_push_view_arena();
  cleat_session *session = push_array(arena, cleat_session, 1);
  session->arena = arena;
  session->provider = provider;
  session->cols = ClampBot(1, desc ? desc->cols : 80);
  session->rows = ClampBot(1, desc ? desc->rows : 24);
  session->cell_width_px = desc ? desc->cell_width_px : 0;
  session->cell_height_px = desc ? desc->cell_height_px : 0;
  session->dirty = CLEAT_DIRTY_FULL;
  cleat_mock_session_set_status(session, str8_lit("mock provider session created"));
  if(provider != 0)
  {
    provider->session_count += 1;
  }
  return session;
}

internal void
cleat_session_destroy(cleat_session *session)
{
  if(session != 0 && session->provider != 0 && session->provider->session_count != 0)
  {
    session->provider->session_count -= 1;
  }
}

internal B32
cleat_session_resize(cleat_session *session, U16 cols, U16 rows, F32 cell_w_px, F32 cell_h_px)
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
    session->dirty = CLEAT_DIRTY_FULL;
    Temp scratch = scratch_begin(0, 0);
    cleat_mock_session_set_status(session, push_str8f(scratch.arena, "resized to %ux%u", cols, rows));
    scratch_end(scratch);
    result = 1;
  }
  return result;
}

internal B32
cleat_session_send_input(cleat_session *session, cleat_input_event const *event)
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
    case CLEAT_INPUT_TEXT:
    {
      cleat_mock_session_append_input(session, str8(event->text, event->text_len));
      cleat_mock_session_set_status(session, push_str8f(scratch.arena, "text input %I64u bytes", event->text_len));
    }break;
    case CLEAT_INPUT_KEY:
    {
      if(event->key_kind == CLEAT_KEY_NAMED && event->key_code == CLEAT_KEY_BACKSPACE)
      {
        if(session->input_size != 0)
        {
          session->input_size -= 1;
        }
        cleat_mock_session_set_status(session, str8_lit("key press: Backspace"));
      }
      else if(event->key_kind == CLEAT_KEY_NAMED && event->key_code == CLEAT_KEY_ENTER)
      {
        session->input_size = 0;
        cleat_mock_session_set_status(session, str8_lit("key press: Return"));
      }
      else if(event->key_kind == CLEAT_KEY_UNICODE_SCALAR)
      {
        U8 buffer[4];
        U32 size = utf8_encode(buffer, event->key_code);
        cleat_mock_session_append_input(session, str8(buffer, size));
        cleat_mock_session_set_status(session, push_str8f(scratch.arena, "unicode key U+%X", event->key_code));
      }
      else
      {
        cleat_mock_session_set_status(session, push_str8f(scratch.arena, "key press: %S", cleat_mock_key_name(scratch.arena, event->key_code)));
      }
    }break;
    case CLEAT_INPUT_MOUSE:
    {
      if(event->wheel_delta_x != 0 || event->wheel_delta_y != 0)
      {
        cleat_mock_session_set_status(session, push_str8f(scratch.arena, "scroll %.1f %.1f", event->wheel_delta_x, event->wheel_delta_y));
      }
      else
      {
        cleat_mock_session_set_status(session, push_str8f(scratch.arena, "mouse at %u,%u", event->cell_col, event->cell_row));
      }
    }break;
    case CLEAT_INPUT_FOCUS:
    {
      cleat_mock_session_set_status(session, str8_lit("focus event"));
    }break;
    case CLEAT_INPUT_PASTE:
    {
      cleat_mock_session_append_input(session, str8(event->text, event->text_len));
      cleat_mock_session_set_status(session, push_str8f(scratch.arena, "paste %I64u bytes", event->text_len));
    }break;
    case CLEAT_INPUT_RESIZE:
    {
      cleat_session_resize(session, event->cell_col, event->cell_row, session->cell_width_px, session->cell_height_px);
    }break;
  }
  scratch_end(scratch);
  return result;
}

internal B32
cleat_session_write_bytes(cleat_session *session, U8 const *bytes, U64 size)
{
  cleat_mock_session_append_input(session, str8(bytes, size));
  Temp scratch = scratch_begin(0, 0);
  cleat_mock_session_set_status(session, push_str8f(scratch.arena, "raw write %I64u bytes", size));
  scratch_end(scratch);
  return 1;
}

internal cleat_dirty_state
cleat_session_dirty(cleat_session const *session)
{
  return session->dirty;
}

internal B32
cleat_session_snapshot(cleat_session *session, cleat_snapshot *out)
{
  U64 cell_count = (U64)session->cols*(U64)session->rows;
  cleat_mock_session_ensure_cell_cap(session, cell_count);
  cleat_mock_session_clear_cells(session);
  
  cleat_rgb fg_normal = cleat_mock_rgb(188, 209, 191);
  cleat_rgb fg_dim = cleat_mock_rgb(126, 142, 131);
  cleat_rgb fg_accent = cleat_mock_rgb(151, 196, 255);
  cleat_rgb fg_prompt = cleat_mock_rgb(255, 220, 118);
  cleat_rgb bg_normal = cleat_mock_rgb(4, 4, 4);
  cleat_rgb bg_header = cleat_mock_rgb(18, 28, 32);
  
  Temp scratch = scratch_begin(0, 0);
  String8 input_string = str8(session->input_buffer, session->input_size);
  String8 status_string = str8(session->status_buffer, session->status_size);
  cleat_mock_session_write_line(session, 0, 0, str8_lit("uishell terminal"), fg_accent, bg_header);
  cleat_mock_session_write_line(session, 1, 0, str8_lit("provider: cleat-shaped mock provider"), fg_normal, bg_normal);
  cleat_mock_session_write_line(session, 2, 0, push_str8f(scratch.arena, "grid: %ux%u cells, %.1fx%.1f px", session->cols, session->rows, session->cell_width_px, session->cell_height_px), fg_dim, bg_normal);
  cleat_mock_session_write_line(session, 3, 0, push_str8f(scratch.arena, "$ %S", input_string), fg_prompt, bg_normal);
  cleat_mock_session_write_line(session, 4, 0, push_str8f(scratch.arena, "last event: %S", status_string), fg_dim, bg_normal);
  cleat_mock_session_write_line(session, 5, 0, push_str8f(scratch.arena, "events captured: %I64u", session->input_event_count), fg_dim, bg_normal);
  scratch_end(scratch);
  
  out->cols = session->cols;
  out->rows = session->rows;
  out->cells = session->cells;
  out->cell_count = cell_count;
  out->cursor.col = (U16)Clamp(0, 2 + session->input_size, (U64)(session->cols-1));
  out->cursor.row = Min((U16)3, (U16)(session->rows-1));
  out->cursor.visible = 1;
  out->cursor.style = CLEAT_CURSOR_STYLE_BLOCK;
  out->cursor.wide_tail = 0;
  out->dirty = session->dirty;
  session->dirty = CLEAT_DIRTY_CLEAN;
  return 1;
}

internal void
cleat_session_release_snapshot(cleat_session *session, cleat_snapshot *snapshot)
{
  (void)session;
  MemoryZeroStruct(snapshot);
}
