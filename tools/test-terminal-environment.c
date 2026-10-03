#include "uishell/uishell_terminal_environment.h"
#include <string.h>

static void
set_test_variable(char const *name, char const *value)
{
#if defined(_WIN32)
  _putenv_s(name, value);
  SetEnvironmentVariableA(name, value);
#else
  setenv(name, value, 1);
#endif
}

int main(void)
{
  // Assert observable launch behavior, including absent and empty flags. The
  // same preparation function runs in the real app before Cleat is opened.
  char const *flags[] = {"NO_COLOR", "CLICOLOR", "CLICOLOR_FORCE", "FORCE_COLOR",
    "CI", "GITHUB_ACTIONS", "CODEX_CI", "CODEX_THREAD_ID", "CLAUDECODE",
    "CLAUDE_CODE_ENTRYPOINT", "CLAUDE_CODE_SESSION_ID"};
  char const *keep[] = {"PATH", "TERM", "TERM_PROGRAM", "COLORTERM",
    "WHEELHOUSE_SOCKET", "FLOTILLA_BIN", "WHEELHOUSE_ENV_TEST_CREDENTIAL"};
  for(size_t i = 0; i < sizeof(flags)/sizeof(flags[0]); i++)
  { set_test_variable(flags[i], i%2 ? "runner" : ""); }
  for(size_t i = 0; i < sizeof(keep)/sizeof(keep[0]); i++)
  { set_test_variable(keep[i], "preserved-test-value"); }
  int good = uishell_prepare_terminal_environment();
  for(size_t i = 0; i < sizeof(flags)/sizeof(flags[0]); i++)
  {
    if(getenv(flags[i]) != 0) { fprintf(stderr, "FAIL: retained %s\n", flags[i]); good = 0; }
#if defined(_WIN32)
    SetLastError(ERROR_SUCCESS);
    if(GetEnvironmentVariableA(flags[i], 0, 0) != 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND)
    { fprintf(stderr, "FAIL: native environment retained %s\n", flags[i]); good = 0; }
#endif
  }
  for(size_t i = 0; i < sizeof(keep)/sizeof(keep[0]); i++)
  {
    char const *value = getenv(keep[i]);
    if(!value || strcmp(value, "preserved-test-value"))
    { fprintf(stderr, "FAIL: changed %s\n", keep[i]); good = 0; }
  }
  good = uishell_prepare_terminal_environment() && good;
  fprintf(stderr, "%s: terminal launch environment\n", good ? "PASS" : "FAIL");
  return !good;
}
