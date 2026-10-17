# The Automation Endpoint May Invoke Named Commands

**Status:** accepted, not yet implemented. Decided in
[Define command invocation over the Automation Endpoint](https://github.com/flotilla-org/wheelhouse/issues/349),
on [Map: Wheelhouse's UI is machine-readable and locale-ready](https://github.com/flotilla-org/wheelhouse/issues/343).
Implementation is on hold until the current Andamento change lands.

## Context

The ingress **Local Endpoint** began as a metadata channel. `pm-connect.md`
says it "has no activate, focus, spawn, or other host-control verbs", so that a
metadata producer could never drive the UI. Agents and test harnesses now need
to act on the shell, not only observe it. The alternatives are synthetic input
(clicks at coordinates, faked keys) and screenshot-driven computer use. Both
are fragile and platform-specific, and they break whenever layout or hover
state changes.

## Decision

The **Automation Endpoint** may invoke **named commands**, the same
`uishell_cmd`s that the command palette and keybindings run, through
`POST /v1/commands/invoke`. That reverses the "no host-control verbs" rule.

- **What can be invoked.** Only commands listed in the palette (`ListInUI`), or
  internal commands that opt in with an automation flag on their own table
  entry. There is no synthetic input and no clicking at coordinates. If an
  agent needs to do something, the fix is to give it a named command.
- **Targets** are **Automation IDs** that resolve into the window, panel and
  view registers. A command that needs a target uses the caller's **Caller
  Context**, or fails. It never falls back to whatever has focus.
- **Invocation is non-interactive.** A missing required argument is an error
  (422), and invocation never opens a prompt on screen. Unless the command is
  itself about focus, it neither raises the OS window nor moves keyboard focus.
- **Trust boundary: the user, as in ADR 0011.** Any process running as the user
  can reach the socket, and could already do anything the user can. So
  commands are on by default, and the `automation: off | observe | full`
  setting exists for anyone who wants a read-only or closed endpoint.
- **For now, metadata producers and automation clients share one socket.** A
  separate socket, or per-client capabilities and authentication, can be
  added later without changing the routes.

## Consequences

- Command names become a contract once clients depend on them. Until we commit
  to compatibility they can change freely. After that, the existing remap
  table provides aliases.
- `GET /v1/commands` is generated from the command tables, so the discovery
  list can't drift from the code.
- Command requests are recorded by `--ingress_record` like every other route,
  so a recorded agent session can be replayed as a test.
- `pm-connect.md` will be updated when this is implemented. Until then it
  describes today's endpoint.
