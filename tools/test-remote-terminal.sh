#!/usr/bin/env bash
# Remote terminal acceptance (#104), headless: a private Cleat daemon runs a
# shell session, and Wheelhouse's --remote_terminal_diagnostics attaches a
# Terminal View to it through `ssh <host> cleat proxy`.
#
#   tools/test-remote-terminal.sh WHEELHOUSE CLEAT [-- RUNNER...]
#
# By default `ssh` is a PATH shim that runs the remote command here, so no
# sshd is needed. WH_REMOTE_HOST=<destination> uses the real ssh instead; that
# host must reach the same runtime root (e.g. localhost).
set -euo pipefail
wheelhouse=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
cleat=$(cd "$(dirname "$2")" && pwd)/$(basename "$2")
shift 2
[ "${1-}" = "--" ] && shift
work=$(mktemp -d /tmp/wh-remote-XXXX)
root="$work/rt"
cleanup() {
  "$cleat" --runtime-root "$root" kill crew >/dev/null 2>&1 || true
  if [ -f "$root/default/daemon.pid" ]; then kill "$(cat "$root/default/daemon.pid")" 2>/dev/null || true; fi
  rm -rf "$work"
}
trap cleanup EXIT
unset CLEAT_SESSION CLEAT_DAEMON CLEAT_OUTPUT_DAEMON CLEAT_RUNTIME_DIR
mkdir -p "$root"
"$cleat" --runtime-root "$root" launch crew --cmd "sh" >/dev/null
host=${WH_REMOTE_HOST-}
if [ -z "$host" ]; then
  host=shimmed-host
  mkdir -p "$work/bin"
  # ssh [options] -- HOST COMMAND: run COMMAND here, as sshd would there.
  # While $WH_REMOTE_DOWN exists the host is unreachable, as ssh reports it.
  export WH_REMOTE_DOWN="$work/down"
  cat >"$work/bin/ssh" <<'EOF'
#!/bin/sh
[ -e "$WH_REMOTE_DOWN" ] && { [ -s "$WH_REMOTE_DOWN" ] && sleep "$(cat "$WH_REMOTE_DOWN")"; echo "ssh: connect to host: Connection refused" >&2; exit 255; }
while [ "$#" -gt 0 ] && [ "$1" != "--" ]; do shift; done
shift 2
exec sh -c "$1"
EOF
  chmod +x "$work/bin/ssh"
  export PATH="$work/bin:$PATH"
fi
WH_REMOTE_HOST="$host" WH_REMOTE_CLEAT="$cleat" WH_REMOTE_ROOT="$root" WH_REMOTE_SESSION=crew \
  ${@+"$@"} "$wheelhouse" --user:"$work/user" --project:"$work/project" --remote_terminal_diagnostics
