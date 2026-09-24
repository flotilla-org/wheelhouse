# Windows Local IPC Uses Named Pipes with Logical Endpoints

**Status:** accepted; not yet implemented.

Wheelhouse, Jackstay's setup channel and our Windows test tooling need one
local transport on Windows. This record is the shared contract; other
repositories link here rather than restating it. Decided in
[Choose one Windows local IPC transport across Wheelhouse, Jackstay and test
tooling](https://github.com/flotilla-org/wheelhouse/issues/55).

## Transport

New Windows local IPC uses named pipes in byte mode. Windows `AF_UNIX` is not
used for new work: it cannot pass handles, gives no reliable peer credentials,
and CPython on Windows does not expose it. Each protocol keeps its own framing
over the stream, as it does over Unix sockets, so code above the transport is
the same on every platform.

## Endpoint identity

An endpoint is a logical address: a scope, a name and a transport kind. Each
platform renders it directly: a Unix socket under the runtime directory, or a
`\\.\pipe\` name on Windows. There is no mapping step from a filesystem path to
a pipe name. Runtime-directory metadata such as owner, PID and generation may
accompany an endpoint for lifecycle purposes, but it never determines the
address. The transport kind leaves room for remote variants later, such as
Flotilla's Tender endpoints, without pretending they are local paths.

## Security baseline

A server creates its endpoint with:

- an explicit protected DACL (`D:P`) granting SYSTEM and the current user, or
  the logon SID when the endpoint is bound to one session;
- `PIPE_REJECT_REMOTE_CLIENTS`;
- `FILE_FLAG_FIRST_PIPE_INSTANCE` on the first instance, so a pre-existing pipe
  of the same name is an error rather than a silent takeover.

A client verifies the server before trusting it: `GetNamedPipeServerProcessId`,
then that process's owner.

## Peer identity

Accepting a connection reports the peer: PID, user SID and Windows session ID
on Windows; peer credentials on POSIX, where macOS has no session concept.
An endpoint may refuse a peer from another session, which keeps a Session 0
SSH process from being mistaken for a GUI-session process. Jackstay duplicates
handles only into a verified peer PID; the rest of handle transfer belongs to
its D3D11 design.

## Existing endpoints

Cleat's path-hash pipe names with marker files and Porthole's default-DACL
pipes are unchanged by this record. Moving Cleat to logical endpoints belongs
with [cleat#122](https://github.com/flotilla-org/cleat/issues/122); Porthole
hardening is tracked separately in Porthole.
