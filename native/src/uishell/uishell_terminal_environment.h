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
// An enclosing Cleat session's identity (CLEAT_SESSION, CLEAT_DAEMON,
// CLEAT_OUTPUT_DAEMON) names that session, not ours: in-process terminals get
// no coordinates, and a daemon sets its own children's. CLEAT_RUNTIME_DIR
// stays: it is also where Wheelhouse's own daemon-backed terminals live.
static int
uishell_prepare_terminal_environment(void)
{
  char const *names[] =
  {
    "NO_COLOR", "CLICOLOR", "CLICOLOR_FORCE", "FORCE_COLOR",
    "CI", "GITHUB_ACTIONS", "CODEX_CI", "CODEX_THREAD_ID",
    "CLAUDECODE", "CLAUDE_CODE_ENTRYPOINT", "CLAUDE_CODE_SESSION_ID",
    "CLEAT_SESSION", "CLEAT_DAEMON", "CLEAT_OUTPUT_DAEMON",
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

// WHEELHOUSE_SOCKET names this instance's Automation Endpoint (#554), so its
// terminals reach it and never an enclosing Wheelhouse's: replaced with
// `address`, or removed when there is none (empty or NULL; automation off).
// Run at startup, before terminals open.
static int
uishell_advertise_automation_endpoint(char const *address)
{
  char const *name = "WHEELHOUSE_SOCKET";
  int set = address != 0 && address[0] != 0;
#if defined(_WIN32)
  int good = _putenv_s(name, set ? address : "") == 0;
  if(!SetEnvironmentVariableA(name, set ? address : 0) && (set || GetLastError() != ERROR_ENVVAR_NOT_FOUND))
  { good = 0; }
#else
  int good = (set ? setenv(name, address, 1) : unsetenv(name)) == 0;
#endif
  if(!good) { fprintf(stderr, "Could not %s %s\n", set ? "set" : "clear", name); }
  return good;
}

#endif
