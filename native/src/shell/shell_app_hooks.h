// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

#ifndef SHELL_APP_HOOKS_H
#define SHELL_APP_HOOKS_H

////////////////////////////////
//~ rjf: Shell/App Hook Contract
//
// Apps may define these macros before including the shell implementation. The
// shell provides conservative no-op defaults so generic shell behavior remains
// buildable without an app layer.

#if !defined(UISHELL_APP_REGISTER_CMD_PACKS)
# define UISHELL_APP_REGISTER_CMD_PACKS() ((void)0)
#endif

#if !defined(UISHELL_APP_BUILD_HELP_MENU)
# define UISHELL_APP_BUILD_HELP_MENU() ((void)0)
#endif

#if !defined(UISHELL_APP_PUSH_PALETTE_QUERY_ROOTS)
# define UISHELL_APP_PUSH_PALETTE_QUERY_ROOTS(arena, exprs) ((void)(arena), (void)(exprs))
#endif

#if !defined(UISHELL_APP_RESET_PANELS)
# define UISHELL_APP_RESET_PANELS(window) ((void)(window))
#endif

#if !defined(UISHELL_APP_DEFAULT_WORKSPACE_PANELS)
# define UISHELL_APP_DEFAULT_WORKSPACE_PANELS(window, owner) ((void)(window), (void)(owner))
#endif

#if !defined(UISHELL_APP_SAVE_BEFORE_EXIT)
# define UISHELL_APP_SAVE_BEFORE_EXIT() ((void)0)
#endif

#if !defined(UISHELL_APP_AUTOSAVE)
# define UISHELL_APP_AUTOSAVE() ((void)0)
#endif

#if !defined(UISHELL_APP_OPEN_DASHBOARD)
# define UISHELL_APP_OPEN_DASHBOARD(cmd_line) ((void)(cmd_line))
#endif

#if !defined(UISHELL_APP_INITIAL_LOAD)
# define UISHELL_APP_INITIAL_LOAD(user_path, project_path) ((void)(user_path), (void)(project_path))
#endif

#if !defined(UISHELL_APP_OPEN_USER_COMMAND_NAME)
# define UISHELL_APP_OPEN_USER_COMMAND_NAME() str8_zero()
#endif

#if !defined(UISHELL_APP_OPEN_PROJECT_COMMAND_NAME)
# define UISHELL_APP_OPEN_PROJECT_COMMAND_NAME() str8_zero()
#endif

// Before each command an Automation Endpoint invocation pushed runs: 0
// skips it. After it, the user errors it logged.
#if !defined(UISHELL_APP_AUTOMATION_ADMIT)
# define UISHELL_APP_AUTOMATION_ADMIT(name, regs) ((void)(name), (void)(regs), 1)
#endif

#if !defined(UISHELL_APP_AUTOMATION_RAN)
# define UISHELL_APP_AUTOMATION_RAN(ticket, errors) ((void)(ticket), (void)(errors))
#endif

#endif // SHELL_APP_HOOKS_H
