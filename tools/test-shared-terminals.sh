#!/usr/bin/env bash
# Shared terminal acceptance (#417), headless: a private Cleat daemon runs two
# shell sessions, and Wheelhouse's --shared_terminal_diagnostics shows them in
# several Views, locally and through `ssh <host> cleat proxy`, checking that
# they share attachments and connections.
#
#   tools/test-shared-terminals.sh WHEELHOUSE CLEAT [-- RUNNER...]
#
# By default `ssh` is a PATH shim that runs the remote command here, so no
# sshd is needed and killing its `cleat proxy` fails the transport.
# WH_SHARED_HOST=<destination> uses the real ssh instead; that host must
# reach the same runtime root (e.g. localhost).
set -euo pipefail
wheelhouse=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cleat=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
shift 2
[ "${1-}" = "--" ] && shift
work=$(mktemp -d /tmp/wh-shared-XXXX)
root="$work/rt"
cleanup() {
  for session in crew-one crew-two; do "$cleat" --runtime-root "$root" kill "$session" >/dev/null 2>&1 || true; done
  if [ -f "$root/default/daemon.pid" ]; then kill "$(cat "$root/default/daemon.pid")" 2>/dev/null || true; fi
  rm -rf "$work"
}
trap cleanup EXIT
unset CLEAT_SESSION CLEAT_DAEMON CLEAT_OUTPUT_DAEMON CLEAT_RUNTIME_DIR
mkdir -p "$root"
"$cleat" --runtime-root "$root" launch crew-one --cmd "sh" >/dev/null
"$cleat" --runtime-root "$root" launch crew-two --cmd "sh" >/dev/null
host=${WH_SHARED_HOST-}
if [ -z "$host" ]; then
  host=shimmed-host
  mkdir -p "$work/bin"
  # ssh [options] -- HOST COMMAND: run COMMAND here, as sshd would there.
  cat >"$work/bin/ssh" <<'SHIM'
#!/bin/sh
while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do shift; done
shift 2
exec sh -c "$1"
SHIM
  chmod +x "$work/bin/ssh"
  export PATH="$work/bin:$PATH"
fi
WH_SHARED_HOST="$host" WH_SHARED_CLEAT="$cleat" WH_SHARED_ROOT="$root" WH_SHARED_SESSIONS=crew-one,crew-two \
  ${@+"$@"} "$wheelhouse" --user:"$work/user" --project:"$work/project" --shared_terminal_diagnostics
