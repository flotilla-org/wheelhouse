# Research: how a Cleat session's identity reaches its processes

Status: research notes, 10 October 2026. Nothing here is decided.

Question: [#344](https://github.com/flotilla-org/wheelhouse/issues/344), for the
map [#343](https://github.com/flotilla-org/wheelhouse/issues/343). What does a
process running inside a Wheelhouse terminal know about its Cleat session, and
what could it be told?

**Sources.** Wheelhouse is cited at `origin/main` 3260412. Cleat is cited at
upstream `main` ce8712a (10 October 2026) and written `cleat:` below. The
sibling checkout at `../cleat` is at e73a85f (2 August 2026). That is older than
the provider ABI Wheelhouse now calls: it has no `cleat_session_transfer`, and
its ABI version is 8, not 11. Wheelhouse builds against whatever `../cleat`
holds (`build.sh:31-47`), so that checkout needs updating before anyone builds
on these notes. Where the two Cleat revisions differ, these notes use the newer
one.

## Session ID

- **Cleat generates `session-<UUIDv4>` unless the caller picks an ID.**
  - It happens in `RuntimeLayout::create_session`
    (`cleat:crates/cleat/src/runtime.rs:426`) and in the CLI or daemon launch
    path (`cleat:crates/cleat/src/session.rs:1329-1338`, `start_session`).
  - IDs must be filesystem-safe: ASCII alphanumerics plus `.`, `_` and `-`
    (`cleat:runtime.rs:482`, `validate_runtime_name`). Each ID is a directory
    name under its daemon's `sessions/`.
  - Launching with an ID that is already live reuses that session
    (`cleat:README.md:140`).
- **The address is `(daemon, id)`, not the ID alone** (`cleat:README.md:136`).
  - A daemon has a logical name, such as `default`, and physical generations
    (`name@N`).
  - `logical_name()` stays the same across generations
    (`cleat:runtime.rs:194-197`).
- **The ID survives reattachment.** For daemon-hosted terminals, Wheelhouse
  stores the ID as the View's `session` parameter right after creating the
  session. Reopening the workspace calls `cleat_session_attach` with that ID
  (`src/uishell/uishell_views.c:4144-4206`). The Sessions view opens a Terminal
  View on any directory row by writing the same `session` parameter
  (`uishell_views.c:2819-2832`).
- **The ID survives transfer.** Moving a session between daemons carries its
  `SessionMetadata`, including the ID. The adopter reads
  `received.manifest.session.id` (`cleat:session_transfer.rs:491`). Promoting an
  embedded session does the same (`cleat:embedded_transfer.rs:84`). After a
  move, Wheelhouse re-reads the ID and stores it with the target daemon name
  (`uishell_views.c:4018-4027`). The hosting epoch increases by one, but the ID
  does not change (`cleat:README.md:134`).
- **Cleat's ADR 0001 says each session has one durable identity**, reused
  across recreations. A display name could be added later as an alias
  (`cleat:docs/adr/0001-session-hosting-and-recreation.md:3-5, 25-30`).
- **The gap: in-process sessions have no ID that callers can read.**
  - In-process is Wheelhouse's default. A terminal is hosted by a daemon only
    if its View sets `daemon:1` or `session:"…"` (`uishell_views.c:4149-4164`).
    Otherwise it uses `CLEAT_PROVIDER_BACKEND_IN_PROCESS`.
  - The in-process session still gets a `session-<uuid>` ID, under
    `<root>/.embedded` (`cleat:provider_ffi.rs:2556-2565`).
  - But `cleat_session_id` returns false for in-process sessions
    (`cleat:provider_ffi.rs:1417-1418`; `cleat:include/cleat_provider.h:604`).
  - Wheelhouse does not persist the ID, so each launch starts a new session
    with a new ID.

## Child environment

- **Cleat exports the session's coordinates, for daemon-hosted sessions only.**
  - The variables are `CLEAT_RUNTIME_DIR`, `CLEAT_DAEMON` (the logical name),
    `CLEAT_SESSION` (the ID) and `CLEAT_OUTPUT_DAEMON` (the physical generation)
    (`cleat:runtime.rs:12-16, 133-140, 180-192`). The README documents them as
    the session's equivalent of tmux's `$TMUX` (`cleat:README.md:154-161`).
  - They are set last and override inherited values. A caller cannot set them,
    because `validate_environment` rejects them
    (`cleat:runtime.rs:21-29`). The merge is in
    `cleat:platform/unix.rs:677-720` and
    `cleat:platform/pty/windows.rs:366-388`.
  - Only the daemon passes coordinates. It calls
    `HostedSession::spawn → SessionRuntime::spawn_in_daemon`
    (`cleat:session.rs:1889-1898, 3508-3509`). The in-process path calls
    `SessionRuntime::spawn`, which passes `None`
    (`cleat:session_runtime.rs:119-121`; `cleat:provider_ffi.rs:2580`).
- **The base environment for each child is the environment of the process
  that hosts it.**
  - The default `ChildEnvironmentPolicy::Inherit` uses `env::vars_os()`
    (`cleat:runtime.rs:64-71`; `cleat:platform/unix.rs:623-624`).
  - In-process sessions therefore inherit Wheelhouse's environment.
  - Daemon sessions inherit the daemon's environment. That is whatever
    environment the first `cleat` client to auto-start the daemon had: the
    spawn copies the parent's environment, then calls `setsid`
    (`cleat:platform/daemon.rs:38-51`; auto-start in
    `cleat:session.rs:1337`).
- **Wheelhouse cannot add variables through the provider ABI.**
  - `cleat_session_desc` has no environment field
    (`cleat:include/cleat_provider.h:180-219`).
  - `create_daemon_session` hard-codes `Inherit` and an empty list
    (`cleat:provider_ffi.rs:2600-2607`).
  - Cleat says so explicitly: "The C provider ABI remains unchanged and uses
    inheritance until it gains environment declarations"
    (`cleat:docs/terminal-identity.md:98-99`).
  - Only the CLI and HTTP paths accept explicit entries: `--env`,
    `--env-clear`, and `environment_policy: "declared"`
    (`cleat:docs/terminal-identity.md:69-99`).
- **The only lever Wheelhouse has today is its own process environment.**
  - `uishell_prepare_terminal_environment` removes inherited agent and CI
    variables at startup (`src/uishell/uishell_terminal_environment.h:15-42`;
    called at `uishell_main.c:156`).
  - Anything Wheelhouse sets with `setenv` there reaches its in-process
    children. It reaches daemon children only if Wheelhouse is the process
    that starts the daemon.
  - The daily driver already relies on this. It sets `WHEELHOUSE_SOCKET` in
    the app's environment (`tools/daily-driver.py:136`), and the README says
    terminals inherit it (`README.md:201, 260`). That holds for in-process
    terminals. It holds for daemon terminals only when this Wheelhouse
    started the daemon.
- **Sessions started outside Wheelhouse do not get Wheelhouse's variables.**
  - A session from `cleat launch`, attached later through the Sessions view,
    has the daemon's environment plus the `CLEAT_*` coordinates.
  - The environment is fixed when the process is created, and Cleat's notes
    confirm that "existing sessions retain their original environment"
    (`cleat:docs/terminal-identity.md:70`). So a variable added later cannot
    reach the process at all.
- **Transfer keeps the process and its environment as they were.**
  - A session promoted from in-process to a daemon (`uishell_terminal_move`,
    `uishell_views.c:3999-4029`) never received `CLEAT_SESSION`.
  - A session moved to a differently named daemon keeps the old
    `CLEAT_DAEMON`.
- **Bug-shaped finding: a Wheelhouse started inside a Cleat session leaks the
  outer coordinates.**
  - When no coordinates are passed, the reserved names are not stripped
    (`cleat:platform/unix.rs:691`, `coordinates.is_some() && …`; same at
    `cleat:platform/pty/windows.rs:375`).
  - `uishell_prepare_terminal_environment` does not clear `CLEAT_*` either.
  - So in-process terminals inherit the outer session's `CLEAT_SESSION`, which
    names the wrong session.

## Peer identity

- **ADR 0011 requires the Local Endpoint to report the peer**: PID, user SID
  and Windows session ID on Windows, and peer credentials on POSIX
  (`docs/adr/0011-…md`, section "Peer identity").
- **The ingress collects none of this today.**
  - On Unix it serves a plain `tokio::net::UnixListener` through `axum::serve`,
    with no connect-info (`src/ingress/lib.rs:187-202`).
  - The Windows listener's address type is `()`
    (`src/ingress/transport/windows.rs:142-146`).
  - Nothing calls `peer_cred`, `GetNamedPipeClientProcessId` or
    `ProcessIdToSessionId`.
- **Getting the PID is straightforward on each platform:**
  - **Linux and macOS:** tokio's `UnixStream::peer_cred()` returns a PID. Linux
    reads `SO_PEERCRED`. macOS reads `LOCAL_PEEREPID` and calls `getpeereid`
    (tokio 1.53 `src/net/unix/ucred.rs:92, 297-331`).
  - **Windows:** `GetNamedPipeClientProcessId` on the pipe handle, then
    `ProcessIdToSessionId`. These are Win32 APIs. Wheelhouse does not use them
    yet.
- **Mapping a PID to a Cleat session.** Here is what each platform offers:
  - **The Unix process structure is useful.**
    - `forkpty` makes the child a session leader (`cleat:platform/unix.rs:85`).
      Under POSIX, its PID is then also its session ID and process group ID,
      and descendants stay in that session unless they call `setsid`. Cleat
      relies on this when it signals the leader's PID as a process group
      (`cleat:platform/unix.rs:263-271`).
    - So on both Linux and macOS, if `getsid(peer_pid)` equals a session's
      `leader_pid`, the peer belongs to that session.
    - The mapping fails for processes that called `setsid`: nested
      multiplexers, daemonised helpers, and Cleat's own daemon
      (`cleat:platform/daemon.rs:44-48`).
  - **Walking parent PIDs is the fallback.** It works only until a process is
    reparented to init or a subreaper. Cleat's kill-tree code already walks
    descendants with `sysinfo`, and guards against PID reuse with a birth
    timestamp taken from `/proc/<pid>/stat`
    (`cleat:platform/signals/tree.rs:1-38, 91, 174`).
  - **Cleat knows each session's PIDs, but the provider ABI does not expose
    them.**
    - `cleat inspect` (`ProcessInspect`) reports `leader_pid` and
      `foreground_pgid` (`cleat:protocol.rs:167-174`;
      `cleat:session_runtime.rs:633-666`).
    - `cleat_directory_entry` has no PID field
      (`cleat:include/cleat_provider.h:226-236`).
    - So Wheelhouse would need to query each daemon over HTTP, or Cleat would
      need a new lookup (PID to session).
  - **For in-process sessions, the session leader's parent is Wheelhouse.**
    `PtyChild` holds the PID (`cleat:platform/unix.rs:88, 203`), but the
    provider ABI does not export it either.
  - **Windows has no equivalent to `getsid`.**
    - `foreground_pgid` returns `None` (`cleat:platform/pty/windows.rs:157-159`).
    - The child is created with `CREATE_NEW_PROCESS_GROUP`
      (`cleat:platform/pty/windows.rs:338`). That is a console control-event
      group, and Cleat never reads it back.
    - Cleat does not use a job object.
    - That leaves the parent-PID chain, which is weaker still: Windows does not
      reparent orphans, so a stale parent PID can be reused by another process.
- **Another option: read the peer's environment.** A same-user process can read
  `/proc/<pid>/environ` on Linux, or `KERN_PROCARGS2` on macOS, and take
  `CLEAT_SESSION` from it. Windows has no supported API for this. The value is
  the environment at exec time, so it inherits the gaps described under "Child
  environment". These are general platform facts, not checked against code in
  these repositories.

## Listener start

- **The ingress starts only with `--andamento_socket:<path>`, and that also
  requires `--andamento_config:<KDL>`.**
  - Configuration is validated before the listener binds
    (`src/uishell/uishell_main.c:203-237`; help text at `:352-353`).
  - Without the flag, no ingress runs. The ingress exists only for the live
    sidebar.
- **Address rules today:**
  - **Unix:** the parent directory must belong to the user and be mode 0700.
    The socket is set to 0600, and the guard unlinks only its own inode on
    exit (`src/ingress/lib.rs:118-163`).
  - **An existing path is an error.** `UnixListener::bind` fails, so a socket
    left by a crash blocks the next start. `docs/protocol/pm-connect.md:9-14`
    says startup fails if the path exists.
  - **Windows:** a `\\.\pipe\<name>` with the ADR 0011 DACL and a first
    instance that refuses an existing name
    (`src/ingress/transport/windows.rs:119-139`).
- **The launcher picks the address.**
  - `scripts/run-daily-driver.sh` runs `tools/daily-driver.py`.
  - That script creates a fresh `mkdtemp` directory, `/tmp/wh-daily-*/facts.sock`.
    It uses `/tmp` because macOS `TMPDIR` is too long for a socket path. On
    Windows it uses `\\.\pipe\wheelhouse-daily-<uuid>`.
  - It exports `WHEELHOUSE_SOCKET`, removes any inherited `WHEELHOUSE_PANE_ID`,
    and passes `--andamento_socket:` to the app
    (`tools/daily-driver.py:125-159`).
  - It then hands the same address to the git watcher and to `flotilla pm
    connect` (`:164, 178`).
- **What starting it by default at a runtime-directory address would involve:**
  - **Separate the listener from the sidebar.** It would need to start without
    `--andamento_config`, with `/v1/metadata/patch` refused or disabled until a
    template is loaded.
  - **Name an address per instance.** Several Wheelhouse instances can run, so
    a fixed name collides. ADR 0011 allows owner, PID or generation metadata
    beside the endpoint, but never as the address itself.
  - **Choose a directory per platform.**
    - **Linux:** `$XDG_RUNTIME_DIR` is already user-owned and 0700.
    - **macOS:** there is no XDG runtime directory, and `TMPDIR` is long.
      `sun_path` holds about 104 bytes on macOS, which is why the daily driver
      uses `/tmp`.
    - **Windows:** a pipe name, which needs no directory.
  - **Handle stale sockets.** For example, test with a connect and then unlink,
    as Cleat's `ensure_daemon_started` does (`cleat:session.rs:5501`).
  - **Advertise the address to children.** Set `WHEELHOUSE_SOCKET` in
    Wheelhouse's own environment before any terminal or daemon starts. The
    launcher's explicit address should still win.

## Implications for Caller Context

These are options, not decisions.

- **A. Use `CLEAT_SESSION` from the environment.** It costs nothing for
  daemon-hosted sessions, including ones started outside Wheelhouse.
  - It is missing for in-process sessions, which are the default, and for
    promoted ones.
  - It can be stale after a cross-daemon transfer, and wrong when the outer
    coordinates leak.
  - The caller can also forge it. That matters only if Caller Context is used
    for authorisation rather than as a convenience.
- **B. Have Cleat always export coordinates.** In-process sessions would get
  them too, the outer `CLEAT_*` would always be stripped, and `cleat_session_id`
  would work for every backend.
  - This is a Cleat change (provider ABI, `spawn` with coordinates).
  - It fixes the default case and the leak, but not promoted or transferred
    processes, whose environment is already fixed.
- **C. Map the peer PID from the Local Endpoint.**
  - Read the PID with `SO_PEERCRED` or `LOCAL_PEEREPID`. On Unix, match
    `getsid(pid)` against the session leaders; on Windows, walk the parent PIDs.
  - It cannot be forged and it survives transfer, because the PIDs do not
    change.
  - It needs a PID-to-session lookup that Cleat does not expose today, and it
    fails for processes that called `setsid` and on Windows.
  - It requires the ingress to start capturing peer credentials, as ADR 0011
    already asks.
- **D. Use both.** The environment gives the claim, and the peer PID confirms
  it where the platform allows. The two can conflict, so it would have to
  define what happens then.
- **For any option, add Wheelhouse's own variables (`WHEELHOUSE_SOCKET`) by
  setting them in Wheelhouse's own environment.** That reaches in-process
  children and daemons Wheelhouse starts. It does not reach a daemon that was
  already running, and doing that needs environment declarations in Cleat's
  provider ABI.
- **Whichever option is chosen, the map's rule holds:** the environment carries
  the endpoint and a session ID, never a view ID. A session ID stays put across
  reattaching and transfer, but a View is found from it only through the
  workspace's `session` View parameter.
