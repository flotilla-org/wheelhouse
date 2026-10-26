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

A `local` endpoint attaches through the shared daemon provider for that runtime root and daemon. An `ssh` endpoint gets a provider of its own.

Any other transport is never guessed at. Its slot is unshown, and a View that has one attaches nothing; it shows the reconnect of a bridge that never started.

`cleat_command` is a View setting only. It isn't part of the View Spec or the facts, so a published endpoint can't choose the command a remote host runs. It is for a user's own host whose non-interactive PATH lacks a new enough Cleat. A View opened from the sidebar runs `cleat` from that PATH.

**Opening from the sidebar.** When a workspace subject publishes a complete direct endpoint, the core ([andamento#186](https://github.com/flotilla-org/andamento/pull/186)) resolves its primary to the `cleat-session` recipe (`WHEELHOUSE_SLOT_CLEAT_SESSION`) instead of the command recipe. Opening the subject creates the workspace with its primary waiting, and the first slot plan fills it with a terminal of that session. A Cleat session is attached to, never started, so an update always makes a new View; it never replaces a terminal in place. When the endpoint goes away, the primary falls back to the command recipe, which still opens `flotilla attach` in a local terminal. The **Direct Cleat Attach** user setting (`direct_cleat_attach`, on by default) makes the same fallback by choice while direct attach is new: off, the core passes endpoints over (`wheelhouse_set_direct_attach`), and turning it either way rebinds open primaries to the other way in. The Cleat session itself carries on. A subject that publishes an endpoint but no command recipe has no other way in, so its primary keeps its Cleat terminal.

## The transport seam

Wheelhouse dials out. For an `ssh` endpoint the View opens its provider with `cleat_provider_open_transport` and a command transport:

```
ssh -T -o BatchMode=yes -- <host> cleat proxy [--daemon=<daemon>] [--runtime-root=<root>]
```

The command's stdio carries the packet stream. ssh is Wheelhouse's own choice of route; Cleat chooses none. Cleat builds the far end's `cleat proxy` command line (`cleat_proxy_command`), with each word quoted for the login shell that ssh hands it to, and Wheelhouse puts ssh around it. Values follow `=`, so one starting with `-` is never read as a flag, and a leading `~/` in `cleat_command` stays unquoted so the remote shell expands it. BatchMode makes ssh fail with a reason instead of prompting, because there is no terminal for it to prompt on. **The user's ssh configuration is the authentication.** Wheelhouse stores no credentials, and keys, agents, jump hosts and ControlMaster all work as they do for `ssh` at a prompt.

**Each remote View has its own bridge.** Local daemon views share one provider per daemon because a provider multiplexes every session over one connection. A remote bridge could be shared the same way, keyed by host, root and daemon. The cost is reconnect: Cleat doesn't respawn a command transport, so recovering means a new provider, and a shared one would make every View on it reconnect together. One bridge per View keeps reconnect local to the View that asked for it. ssh's ControlMaster removes most of the cost of the extra connections. Sharing remains an optimisation for later, without changing this seam.

A later transport must provide:

- **A byte stream to a daemon's packet socket.** This is a command whose stdio carries it, or a socket the provider connects to, as Cleat's `unix_socket` transport does.
- **A reason when it fails.** The provider reports the command's last stderr line, so the transport must write its reason there.
- **No reconnect of its own.** If the transport ends, the stream ends. Recovery belongs to the View, so a transport that restarts itself would hide disconnects.
- **Its own authentication.** Cleat adds none. Whoever can run the command controls the sessions behind it.

## Hosting and chrome

A session reached over a transport reports hosting `remote:<kind>`, for example `remote:command`. The chrome shows it in the hosting overlay and settings row. It offers no move: its action reads "Hosted remotely" and is disabled, because the session belongs to its daemon on the other host and can't be handed over or adopted from here. A transfer on the remote daemon closes the View's channel rather than being followed (cleat#285).

## Disconnect and reconnect

A remote View follows [Stopped and disconnected terminals](stopped-and-disconnected-terminals.md): losing the bridge is a disconnection, never a process exit.

- When the bridge dies, the session reads `DISCONNECTED` and the provider stops retrying. The connection pill then says `disconnected: <reason> — click to reconnect`. The reason is the bridge's own, such as ssh's last stderr line, and the View keeps its last frame.
- The pill doesn't animate or poll while waiting. Nothing reconnects until the user asks, and the remote session is never restarted or reported as exited.
- A click reconnects. The View releases its session and provider and, on the next frame, opens a new bridge and attaches to the same session.
- A View whose bridge never started, for example because ssh couldn't be spawned, shows the same reconnect.

## Diagnostics

The remote output-admission warning ("cycle protection does not cover remote relationships") is diagnostics, not content. Cleat reports it through `cleat_provider_transport_state`, and Wheelhouse writes it to the log once per connection, with the host. When a bridge gives up, its error is logged once too, including one that never streamed, such as a bad host or refused auth. Neither ever reaches the terminal.

## Tests

- `--hosting_diagnostics` covers `remote:` hosting, the bridge command around Cleat's quoted command line, refusing an endpoint holding a NUL, and cleaning the transport texts for display.
- `tools/test-remote-terminal.sh` runs `--remote_terminal_diagnostics` against a private daemon session. It checks:
  - attach as controller;
  - input and resize reaching the remote PTY;
  - the warning staying out of the content;
  - a killed bridge showing the reconnect within 3 s, with the session still running and nothing reconnecting by itself;
  - reconnect reaching the same live session;
  - a bridge that fails before it streams giving up with its reason, logged.
- `--managed_content_diagnostics` covers opening a subject with a direct endpoint: its primary waits, then becomes the endpoint's terminal, and the next plan finds it current. With the setting turned off, the primary rebinds to its command recipe.
- By default the script runs through a PATH `ssh` shim, so it needs no sshd. CI runs it on macOS; the Linux job builds Cleat without a VT engine, which `cleat launch` refuses. Setting `WH_REMOTE_HOST=localhost` runs it over real ssh.

## Not covered

- Discovery of hosts or daemons outside the catalog.
- Tunnel multiplexing.
- Non-ssh transports.
- Windows: Cleat's command transport is Unix only, so a remote View there shows the bridge's unsupported error.
