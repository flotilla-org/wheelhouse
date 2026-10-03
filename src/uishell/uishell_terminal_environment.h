#ifndef UISHELL_TERMINAL_ENVIRONMENT_H
#define UISHELL_TERMINAL_ENVIRONMENT_H

#include <stdio.h>
#include <stdlib.h>
#if defined(_WIN32)
#include <windows.h>
#endif

// Wheelhouse is an interactive terminal host, even when launched by an agent
// or CI runner. Keep the user's paths, credentials and workspace routing, but
// do not give new shells the enclosing runner's colour policy or agent identity.
// Run once at startup, before opening providers or starting GUI worker threads.
// Cleat owns TERM/TERM_PROGRAM/COLORTERM; do not replace its terminal identity.
static int
uishell_prepare_terminal_environment(void)
{
  char const *names[] =
  {
    "NO_COLOR", "CLICOLOR", "CLICOLOR_FORCE", "FORCE_COLOR",
    "CI", "GITHUB_ACTIONS", "CODEX_CI", "CODEX_THREAD_ID",
    "CLAUDECODE", "CLAUDE_CODE_ENTRYPOINT", "CLAUDE_CODE_SESSION_ID",
  };
  int good = 1;
  for(size_t i = 0; i < sizeof(names)/sizeof(names[0]); i++)
  {
#if defined(_WIN32)
    // Native libraries and the CRT can read different environment copies.
    int removed = _putenv_s(names[i], "") == 0;
    if(!SetEnvironmentVariableA(names[i], 0) && GetLastError() != ERROR_ENVVAR_NOT_FOUND)
    { removed = 0; }
#else
    int removed = unsetenv(names[i]) == 0;
#endif
    if(!removed)
    {
      fprintf(stderr, "Could not clear inherited terminal environment flag %s\n", names[i]);
      good = 0;
    }
  }
  return good;
}

#endif
