#ifndef UISHELL_TERMINAL_PROVIDER_H
#define UISHELL_TERMINAL_PROVIDER_H

#define CLEAT_PROVIDER_ABI_VERSION 1u
#define CLEAT_PROVIDER_BACKEND_MOCK 0u
#define CLEAT_PROVIDER_BACKEND_IN_PROCESS 1u
#define CLEAT_PROVIDER_BACKEND_DAEMON 2u
#define CLEAT_PROVIDER_VT_DEFAULT 0u
#define CLEAT_PROVIDER_VT_PASSTHROUGH 1u
#define CLEAT_PROVIDER_VT_GHOSTTY 2u
#define CLEAT_INPUT_KEY 1u
#define CLEAT_INPUT_TEXT 2u
#define CLEAT_INPUT_MOUSE 3u
#define CLEAT_INPUT_FOCUS 4u
#define CLEAT_INPUT_PASTE 5u
#define CLEAT_INPUT_RESIZE 6u
#define CLEAT_KEY_UNICODE_SCALAR 1u
#define CLEAT_KEY_NAMED 2u
#define CLEAT_KEY_ENTER 1u
#define CLEAT_KEY_ESCAPE 2u
#define CLEAT_KEY_BACKSPACE 3u
#define CLEAT_KEY_TAB 4u
#define CLEAT_KEY_DELETE 5u
#define CLEAT_KEY_ARROW_UP 12u
#define CLEAT_KEY_ARROW_DOWN 13u
#define CLEAT_KEY_ARROW_LEFT 14u
#define CLEAT_KEY_ARROW_RIGHT 15u
#define CLEAT_CELL_WIDTH_NARROW 0u
#define CLEAT_CELL_WIDTH_WIDE 1u
#define CLEAT_CELL_WIDTH_SPACER_TAIL 2u
#define CLEAT_CELL_WIDTH_SPACER_HEAD 3u
#define CLEAT_CURSOR_STYLE_BAR 0u
#define CLEAT_CURSOR_STYLE_BLOCK 1u
#define CLEAT_CURSOR_STYLE_UNDERLINE 2u
#define CLEAT_CURSOR_STYLE_BLOCK_HOLLOW 3u

typedef struct CleatProvider cleat_provider;
typedef struct CleatSession cleat_session;

typedef enum cleat_dirty_state
{
  CLEAT_DIRTY_CLEAN = 0,
  CLEAT_DIRTY_PARTIAL = 1,
  CLEAT_DIRTY_FULL = 2,
}
cleat_dirty_state;

typedef struct cleat_provider_desc cleat_provider_desc;
struct cleat_provider_desc
{
  U32 abi_version;
  U32 requested_features;
  U32 backend;
  U8 const *runtime_root;
  U64 runtime_root_len;
};

typedef struct cleat_session_desc cleat_session_desc;
struct cleat_session_desc
{
  U16 cols;
  U16 rows;
  F32 cell_width_px;
  F32 cell_height_px;
  U32 vt_engine;
  U8 const *command;
  U64 command_len;
  U8 const *cwd;
  U64 cwd_len;
  B32 record;
};

typedef struct cleat_rgb cleat_rgb;
struct cleat_rgb
{
  U8 r;
  U8 g;
  U8 b;
};

typedef struct cleat_cell cleat_cell;
struct cleat_cell
{
  U32 const *graphemes;
  U64 grapheme_count;
  cleat_rgb fg;
  cleat_rgb bg;
  U32 flags;
  U32 width;
};

typedef struct cleat_cursor cleat_cursor;
struct cleat_cursor
{
  U16 col;
  U16 row;
  B32 visible;
  U32 style;
  B32 wide_tail;
};

typedef struct cleat_snapshot cleat_snapshot;
struct cleat_snapshot
{
  U16 cols;
  U16 rows;
  cleat_cell const *cells;
  U64 cell_count;
  cleat_cursor cursor;
  cleat_dirty_state dirty;
};

typedef struct cleat_input_event cleat_input_event;
struct cleat_input_event
{
  U32 kind;
  U16 modifiers;
  U32 key_kind;
  U32 key_code;
  U8 const *text;
  U64 text_len;
  U16 cell_col;
  U16 cell_row;
  F32 x_px;
  F32 y_px;
  F32 wheel_delta_x;
  F32 wheel_delta_y;
};

internal U32 cleat_provider_abi_version(void);
internal cleat_provider *cleat_provider_open(cleat_provider_desc const *desc);
internal void cleat_provider_close(cleat_provider *provider);
internal cleat_session *cleat_session_create(cleat_provider *provider, cleat_session_desc const *desc);
internal void cleat_session_destroy(cleat_session *session);
internal B32 cleat_session_resize(cleat_session *session, U16 cols, U16 rows, F32 cell_w_px, F32 cell_h_px);
internal B32 cleat_session_send_input(cleat_session *session, cleat_input_event const *event);
internal B32 cleat_session_write_bytes(cleat_session *session, U8 const *bytes, U64 size);
internal cleat_dirty_state cleat_session_dirty(cleat_session const *session);
internal B32 cleat_session_snapshot(cleat_session *session, cleat_snapshot *out);
internal void cleat_session_release_snapshot(cleat_session *session, cleat_snapshot *snapshot);

#endif // UISHELL_TERMINAL_PROVIDER_H
