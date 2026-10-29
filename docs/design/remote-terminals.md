# Remote terminals

Status: design, agreed 23 October 2026, for [Wheelhouse #104](https://github.com/flotilla-org/wheelhouse/issues/104). The ruling it implements is on [#21](https://github.com/flotilla-org/wheelhouse/issues/21). Its Cleat half is [cleat#285](https://github.com/flotilla-org/cleat/issues/285) (`cleat proxy` and provider transports), and its Flotilla half, [flotilla#2106](https://github.com/flotilla-org/flotilla/issues/2106), publishes direct endpoints.

A crew session on another host opens as an ordinary Cleat-backed Terminal View. Its provider reaches the remote daemon through a transport command; there is no local terminal running `flotilla attach` and no nested terminal. There is one VT engine, so selection, links, clipboard and scrollback behave as they do for a local daemon view. The command recipe stays available as the fallback for subjects with no direct endpoint.

## The endpoint

A direct view's View Spec names a **Cleat endpoint**, the way a Jackstay view's spec names its endpoint. The core's `cleat-session` recipe carries it from Flotilla's `action.primary.direct.*` facts. On the Terminal View the endpoint is a set of settings:

| Setting | Meaning |
| --- | --- |
| `cleat_transport` | `local` or `ssh` |
| `cleat_host` | the ssh destination, which the user's ssh configuration resolves |
| `cleat_runtime_root` | the endpoint daemon's runtime root; empty for the one Cleat discovers |
| `cleat_daemon` | the daemon's name; empty for the default daemon |
| `cleat_session` | the session. It is only ever attached to, never created |
| `cleat_command` | optional: the Cleat the remote host runs, for one not on its non-interactive PATH |

A `local` endpoint attaches through the shared connection to that runtime root and daemon, an `ssh` endpoint through the shared connection to that host, Cleat, runtime root and daemon. Every View of one session shares one attachment on it (see "Shared connections and attachments").

Any other transport is never guessed at. Its slot is unshown, and a View that has one attaches nothing; it shows the reconnect of a bridge that never started.

`cleat_command` is a View setting only. It isn't part of the View Spec or the facts, so a published endpoint can't choose the command a remote host runs. It is for a user's own host whose non-interactive PATH lacks a new enough Cleat. A View opened from the sidebar runs `cleat` from that PATH.

**Opening from the sidebar.** When a workspace subject publishes a complete direct endpoint, the core ([andamento#186](https://github.com/flotilla-org/andamento/pull/186)) resolves its primary to the `cleat-session` recipe (`WHEELHOUSE_SLOT_CLEAT_SESSION`) instead of the command recipe. Opening the subject creates the workspace with its primary waiting, and the first slot plan fills it with a terminal of that session. A Cleat session is attached to, never started, so an update always makes a new View; it never replaces a terminal in place. When the endpoint goes away, the primary falls back to the command recipe, which still opens `flotilla attach` in a local terminal. The **Direct Cleat Attach** user setting (`direct_cleat_attach`, on by default) makes the same fallback by choice while direct attach is new: off, the core prefers the command recipe (`wheelhouse_set_direct_attach`), keeping the endpoint for a subject that publishes no recipe, and turning it either way rebinds open primaries to the other way in. The Cleat session itself carries on.

## The transport seam

Wheelhouse dials out. For an `ssh` endpoint the connection opens its provider with `cleat_provider_open_transport` and a command transport:

```
ssh -T -o BatchMode=yes -- <host> cleat proxy [--daemon=<daemon>] [--runtime-root=<root>]
```

The command's stdio carries the packet stream. ssh is Wheelhouse's own choice of route; Cleat chooses none. Cleat builds the far end's `cleat proxy` command line (`cleat_proxy_command`), with each word quoted for the login shell that ssh hands it to, and Wheelhouse puts ssh around it. Values follow `=`, so one starting with `-` is never read as a flag, and a leading `~/` in `cleat_command` stays unquoted so the remote shell expands it. BatchMode makes ssh fail with a reason instead of prompting, because there is no terminal for it to prompt on. **The user's ssh configuration is the authentication.** Wheelhouse stores no credentials, and keys, agents, jump hosts and ControlMaster all work as they do for `ssh` at a prompt.

**One bridge per daemon.** A provider multiplexes every session over one connection, so Wheelhouse keeps one connection per route and daemon, local or remote, shared by every View on it ([#417](https://github.com/flotilla-org/wheelhouse/issues/417)). This reverses the first cut, which gave each remote View its own bridge to keep reconnect local to the View that asked for it. The connection owns reconnect instead, and reattaches all of its sessions together.

A later transport must provide:

- **A byte stream to a daemon's packet socket.** This is a command whose stdio carries it, or a socket the provider connects to, as Cleat's `unix_socket` transport does.
- **A reason when it fails.** The provider reports the command's last stderr line, so the transport must write its reason there.
- **No reconnect of its own.** If the transport ends, the stream ends. Recovery belongs to the connection, so a transport that restarts itself would hide disconnects.
- **Its own authentication.** Cleat adds none. Whoever can run the command controls the sessions behind it.

## Shared connections and attachments

`native/src/uishell/uishell_cleat_attachments.c` keeps a registry of three things:

- **Connection:** one provider per route (transport, host, Cleat command, runtime root) and daemon. It is counted by the attachments on it, and by daemon-backed Views that keep their own session handle. When the last one goes, it lingers for about 30 seconds, so a View reopened soon after reuses it, and then closes; closing an ssh connection ends its bridge.
- **Attachment:** one per session per connection, attached with `CLEAT_ROLE_NO_VIEW`, so it has no view of its own and its live stream is paused until a View opens one. It is counted by Views and closed at the end of the frame its count reaches zero, so a View released and another acquired in the same frame (a slot replacement, a layout change) keep the stream. A daemon that predates views fails the attach; there is no fallback to a default view, and the View says Cleat could not attach the session, or `session closed: <reason>`.
- **View:** each Terminal View has its own Cleat view on the attachment (`cleat_session_open_view`), with its own role, size, geometry, input, scroll position and render updates. All views share the attachment's wake, and each polls its own. The live stream is acknowledged when any view consumes it; a view behind catches up from the attachment's copy.

A role policy maps Views to Cleat roles. The one policy now is *last-touched → controller*: the View most recently focused, typed or clicked in asks for shared driving (`cleat_view_set_role`) and the others watch, so Cleat sizes the grid to it. When it goes, the most recently touched remaining View takes over. Clicking "watching — click to take control" takes control over another client (`cleat_view_take_control`).

Only direct Cleat attach shares. The `flotilla attach` recipe runs one process per View, as before, so turning Direct Cleat Attach off gives up sharing.

## Hosting and chrome

A session reached over a transport reports hosting `remote:<kind>`, for example `remote:command`. The chrome shows it in the hosting overlay and settings row. It offers no move: its action reads "Hosted remotely" and is disabled, because the session belongs to its daemon on the other host and can't be handed over or adopted from here. A transfer on the remote daemon closes the View's channel rather than being followed (cleat#285).

## Disconnect and reconnect

A remote View follows [Stopped and disconnected terminals](stopped-and-disconnected-terminals.md): losing the bridge is a disconnection, never a process exit.

- When the bridge dies, the session reads `DISCONNECTED` and the provider stops retrying. Cleat doesn't respawn a command transport, so the connection does: if it had streamed, it opens a new bridge at the end of that frame and re-attaches every session on it, each with its Views' Cleat views at their last size and role.
- A bridge that fails before it streams, for example a bad host, refused auth or a missing Cleat, gives up. Its Views' pill then says `disconnected: <reason> — click to reconnect`. The reason is the bridge's own, such as ssh's last stderr line, and the View keeps its last frame. Nothing retries until the user asks, so a host that is down doesn't respawn ssh in a loop.
- A click reconnects the connection, for all of its Views: a new bridge, attaching the same sessions.
- A View whose connection never started, for example because ssh couldn't be spawned, shows the same reconnect. A View with a transport this build doesn't know attaches nothing, and its reconnect re-reads its settings.
- The remote session is never restarted or reported as exited.

## Diagnostics

The remote output-admission warning ("cycle protection does not cover remote relationships") is diagnostics, not content. Cleat reports it through `cleat_provider_transport_state`, and Wheelhouse writes it to the log once per connection, with the host. When a bridge gives up, its error is logged once too, by each View on it, including one that never streamed, such as a bad host or refused auth. Neither ever reaches the terminal.

## Tests

- `--hosting_diagnostics` covers `remote:` hosting, the bridge command around Cleat's quoted command line, refusing an endpoint holding a NUL, and cleaning the transport texts for display.
- `tools/test-remote-terminal.sh` runs `--remote_terminal_diagnostics` against a private daemon session. It checks:
  - attach as controller;
  - input and resize reaching the remote PTY;
  - the warning staying out of the content;
  - a killed bridge that had streamed reopening and re-attaching by itself, with the session still running;
  - a requested reconnect reaching the same live session;
  - a bridge that fails before it streams giving up with its reason, logged.
- `--managed_content_diagnostics` covers opening a subject with a direct endpoint: its primary waits, then becomes the endpoint's terminal, and the next plan finds it current. With the setting turned off, the primary rebinds to its command recipe.
- `tools/test-shared-terminals.sh` runs `--shared_terminal_diagnostics` against a private daemon with two sessions, locally and through the shim. It checks that two Views of one session make one attachment and one connection (and one attachment the daemon sees); that the last-touched View is the only controller, takes input and sizes the grid, and that releasing it passes control to the most recently touched remaining View; that a View released and re-acquired in one frame doesn't re-attach; that a connection lingers after its last attachment and then closes, ending its bridge; that two sessions on one remote daemon share one bridge and are both re-attached when it is killed; that scrolling one View into history leaves the other live; and that the recipe path runs one process per View.
- By default the scripts run through a PATH `ssh` shim, so it needs no sshd. CI runs it on macOS; the Linux job builds Cleat without a VT engine, which `cleat launch` refuses. Setting `WH_REMOTE_HOST=localhost` (or `WH_SHARED_HOST`) runs it over real ssh.

## Not covered

- Discovery of hosts or daemons outside the catalog.
- Tunnel multiplexing.
- Flow control and back-pressure across a connection's attachments (reading visible sessions first), and panning a clipped View.
- Non-ssh transports.
- Windows: Cleat's command transport is Unix only, so a remote View there shows the bridge's unsupported error.
