#ifndef UISHELL_TERMINAL_PROVIDER_H
#define UISHELL_TERMINAL_PROVIDER_H

#define UISHELL_USE_CLEAT_PROVIDER 1

#include "cleat_provider.h"

#if CLEAT_PROVIDER_ABI_VERSION != 12u
# error "Wheelhouse requires Cleat provider ABI 12: build against the Cleat revision in ci/pins.toml"
#endif

// Views within an attachment (cleat#399, packet protocol 16): one attachment
// per session, a Cleat view per Wheelhouse View (uishell_cleat_attachments.c).
#if !defined(CLEAT_ROLE_NO_VIEW)
# error "Wheelhouse requires Cleat views (cleat#399): cleat_session_open_view and CLEAT_ROLE_NO_VIEW"
#endif

#if !defined(CLEAT_CELL_FLAG_BOLD)
# define CLEAT_CELL_FLAG_BOLD (1u << 0)
# define CLEAT_CELL_FLAG_ITALIC (1u << 1)
# define CLEAT_CELL_FLAG_FAINT (1u << 2)
# define CLEAT_CELL_FLAG_BLINK (1u << 3)
# define CLEAT_CELL_FLAG_INVERSE (1u << 4)
# define CLEAT_CELL_FLAG_INVISIBLE (1u << 5)
# define CLEAT_CELL_FLAG_STRIKETHROUGH (1u << 6)
# define CLEAT_CELL_FLAG_OVERLINE (1u << 7)
# define CLEAT_CELL_FLAG_UNDERLINE (1u << 8)
#endif

#endif // UISHELL_TERMINAL_PROVIDER_H
