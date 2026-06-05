#ifndef UISHELL_TERMINAL_PROVIDER_H
#define UISHELL_TERMINAL_PROVIDER_H

typedef struct UIShell_TerminalProvider UIShell_TerminalProvider;
typedef struct UIShell_TerminalSession UIShell_TerminalSession;

typedef enum UIShell_TerminalDirtyState
{
  UIShell_TerminalDirtyState_Clean,
  UIShell_TerminalDirtyState_Partial,
  UIShell_TerminalDirtyState_Full,
}
UIShell_TerminalDirtyState;

typedef enum UIShell_TerminalInputKind
{
  UIShell_TerminalInputKind_Null,
  UIShell_TerminalInputKind_Key,
  UIShell_TerminalInputKind_Text,
  UIShell_TerminalInputKind_Mouse,
  UIShell_TerminalInputKind_Focus,
  UIShell_TerminalInputKind_Paste,
  UIShell_TerminalInputKind_Resize,
}
UIShell_TerminalInputKind;

typedef struct UIShell_TerminalRGB UIShell_TerminalRGB;
struct UIShell_TerminalRGB
{
  U8 r;
  U8 g;
  U8 b;
};

typedef struct UIShell_TerminalCell UIShell_TerminalCell;
struct UIShell_TerminalCell
{
  U32 *graphemes;
  U64 grapheme_count;
  UIShell_TerminalRGB fg;
  UIShell_TerminalRGB bg;
  U32 flags;
  U32 width;
};

typedef struct UIShell_TerminalCursor UIShell_TerminalCursor;
struct UIShell_TerminalCursor
{
  U16 col;
  U16 row;
  B32 visible;
  U32 style;
  B32 wide_tail;
};

typedef struct UIShell_TerminalSnapshot UIShell_TerminalSnapshot;
struct UIShell_TerminalSnapshot
{
  U16 cols;
  U16 rows;
  UIShell_TerminalCell *cells;
  U64 cell_count;
  UIShell_TerminalCursor cursor;
  UIShell_TerminalDirtyState dirty;
};

typedef struct UIShell_TerminalProviderDesc UIShell_TerminalProviderDesc;
struct UIShell_TerminalProviderDesc
{
  U32 abi_version;
  U32 requested_features;
};

typedef struct UIShell_TerminalSessionDesc UIShell_TerminalSessionDesc;
struct UIShell_TerminalSessionDesc
{
  U16 cols;
  U16 rows;
  F32 cell_width_px;
  F32 cell_height_px;
  String8 command;
  String8 cwd;
};

typedef struct UIShell_TerminalInputEvent UIShell_TerminalInputEvent;
struct UIShell_TerminalInputEvent
{
  UIShell_TerminalInputKind kind;
  WM_Modifiers modifiers;
  WM_Key key;
  String8 text;
  U16 cell_col;
  U16 cell_row;
  F32 x_px;
  F32 y_px;
  F32 wheel_delta_x;
  F32 wheel_delta_y;
  B32 enabled;
};

internal UIShell_TerminalProvider *uishell_terminal_mock_provider_open(Arena *arena, UIShell_TerminalProviderDesc *desc);
internal void uishell_terminal_mock_provider_close(UIShell_TerminalProvider *provider);
internal UIShell_TerminalSession *uishell_terminal_session_create(Arena *arena, UIShell_TerminalProvider *provider, UIShell_TerminalSessionDesc *desc);
internal void uishell_terminal_session_destroy(UIShell_TerminalSession *session);
internal B32 uishell_terminal_session_resize(UIShell_TerminalSession *session, U16 cols, U16 rows, F32 cell_w_px, F32 cell_h_px);
internal B32 uishell_terminal_session_send_input(UIShell_TerminalSession *session, UIShell_TerminalInputEvent *event);
internal B32 uishell_terminal_session_write_bytes(UIShell_TerminalSession *session, U8 *bytes, U64 size);
internal UIShell_TerminalDirtyState uishell_terminal_session_dirty(UIShell_TerminalSession *session);
internal B32 uishell_terminal_session_snapshot(UIShell_TerminalSession *session, UIShell_TerminalSnapshot *out);
internal void uishell_terminal_session_release_snapshot(UIShell_TerminalSession *session, UIShell_TerminalSnapshot *snapshot);

#endif // UISHELL_TERMINAL_PROVIDER_H
