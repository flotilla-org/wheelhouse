# Automation Endpoint

The **Automation Endpoint** is the set of routes on Wheelhouse's ingress
**Local Endpoint** that let local clients (agents running in Wheelhouse
terminals, test harnesses, scripts) observe the shell. It is the endpoint
that [metadata producers](pm-connect.md) publish to: one socket serves both.
The design is on [#343](https://github.com/flotilla-org/wheelhouse/issues/343):
Automation IDs (#346), the Semantic and Placement Trees (#347), Caller
Context and discovery (#348), and gating in
[ADR 0014](../adr/0014-automation-endpoint-invokes-named-commands.md).

The read routes arrive with the Semantic Tree; this page covers how a client
finds the endpoint and how it is gated.

## Discovery

**Default listener.** Every instance listens at an address of its own,
`wheelhouse/<instance-uuid>` under the runtime directory, rendered per
platform as [ADR 0011](../adr/0011-windows-local-ipc-uses-named-pipes-with-logical-endpoints.md)
describes. There is no fixed global name, and no instance takes over another's
endpoint.

| Platform | Address |
| --- | --- |
| Unix, with `XDG_RUNTIME_DIR` | `$XDG_RUNTIME_DIR/wheelhouse/<instance-uuid>` |
| Unix, without it (macOS) | `$TMPDIR/wheelhouse-<uid>/<instance-uuid>`; `/tmp/wheelhouse-<uid>/…` when `TMPDIR` is unset or too long for a socket path (104 bytes on macOS) |
| Windows | `\\.\pipe\wheelhouse\<instance-uuid>` |

On Unix the directory is made private to the user (mode 0700), and one that
isn't is refused. If the default listener can't start, Wheelhouse runs without
one and says why on stderr.

**Explicit address.** A launcher or test names the address and learns it from
a file, never from the environment:

- `--automation_socket:<path>` listens there instead. `--andamento_socket:<path>`
  still works, and also turns on the live sidebar; if both are given they must
  name the same endpoint. Wheelhouse exits if an address it was given can't be
  served.
- `--automation_socket_file:<path>` makes Wheelhouse write where it listens to
  that file once the listener is up: exactly the address, with no newline, or
  an empty file when it listens nowhere. The file is replaced whole, so a
  reader never sees part of it, and removed when Wheelhouse exits cleanly.

**Environment.** At startup, before any terminal opens, Wheelhouse replaces
`WHEELHOUSE_SOCKET` in its own environment with its own address, or removes it
when automation is `off` or there is no listener. Its terminals inherit that,
so a client in a Wheelhouse terminal reaches the instance that hosts it, never
an enclosing one: running Wheelhouse inside Wheelhouse is the everyday case.

## Gating

The user setting `automation: off | observe | full` (default `full`) is read
from the user file at launch. Any other value is taken as `full`, with a
warning on stderr.

- `off`: there is no default listener and terminals get no
  `WHEELHOUSE_SOCKET`. An address given on the command line is still served,
  for metadata; the automation routes will answer 404 there.
- `observe` and `full` behave the same until commands exist (ADR 0014).

The metadata routes are not affected by the setting.
