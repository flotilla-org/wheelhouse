# Automation Endpoint

The **Automation Endpoint** is the set of routes on Wheelhouse's ingress
**Local Endpoint** that let local clients (agents running in Wheelhouse
terminals, test harnesses, scripts) observe the shell. It is the endpoint
that [metadata producers](pm-connect.md) publish to: one socket serves both.
The design is on [#343](https://github.com/flotilla-org/wheelhouse/issues/343):
Automation IDs (#346), the Semantic and Placement Trees (#347), Caller
Context and discovery (#348), and gating in
[ADR 0014](../adr/0014-automation-endpoint-invokes-named-commands.md).

Everything here is read-only. Commands and intents come later (ADR 0014),
as do the Placement Tree (#564) and the Dashboard part of the Semantic Tree.

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
  After a crash it may name a dead endpoint, so check `GET /v1/health`
  (204) before trusting it.

**Environment.** At startup, before any terminal opens, Wheelhouse replaces
`WHEELHOUSE_SOCKET` in its own environment with its own address, or removes it
when automation is `off` or there is no listener. Its terminals inherit that,
so a client in a Wheelhouse terminal reaches the instance that hosts it, never
an enclosing one: running Wheelhouse inside Wheelhouse is the everyday case.

## Gating

**Who may connect** is who can reach the endpoint, and nothing else: there
is no authentication (ADR 0014's trust boundary is the user). On Unix the
socket is mode 0600 in a directory Wheelhouse makes, or checks, mode 0700
and owned by the user, and refuses otherwise; on Windows the pipe's DACL
grants only SYSTEM and the user (ADR 0011).

The user setting `automation: off | observe | full` (default `full`) is read
from the user file at launch. Any other value is taken as `off`, with a
warning on stderr, so a mistyped `off` leaves automation off.

- `off`: there is no default listener and terminals get no
  `WHEELHOUSE_SOCKET`. An address given on the command line is still served,
  for metadata, and the automation routes answer 404 there. Turning it off
  while Wheelhouse runs makes them answer 404 from the next frame.
- `observe` and `full` behave the same until commands exist (ADR 0014).

The metadata routes, `/v1/observed/workdirs` included, are not affected by
the setting. Each Dashboard subscription's own endpoint serves none of the
automation routes.

`automation_terminal_text: off | screen` (default `off`) decides whether a
terminal's screen may be read at all; see [Terminals](#terminals).

## Automation IDs

Typed, path-shaped strings that mirror View Address, compared with `==`.
They never derive from display text or from a UI key. Each node also says
whether its ID is `durable`: it survives relaunch, or lasts only while the
app runs.

| Node | Automation ID | Durable |
| --- | --- | --- |
| Workspace | `workspace/<uuid>` | yes |
| Panel | `workspace/<uuid>/panel/<panel-id>` | yes |
| View | `<owner>/view/<key>`, and `<owner>/view/<key>@previous` for an instance a keep-previous rebind kept. The owner is `workspace/<uuid>`, or `window/<uuid>` for a window's own Views; a workspace View's key is its Slot key. | yes; not on transient hosts |

Windows, sidebar sections and rows, detached cards and overlays have IDs of
their own (#343), which the Placement Tree (#564) and the Dashboard part
will carry.

## Routes

All responses are `Cache-Control: no-store`.

| Route | Answers |
| --- | --- |
| `GET /v1/observed/semantic` | the Semantic Tree, once |
| `GET /v1/observed/events?since=<cursor>` | the Semantic Tree, held open: a snapshot first and after every change |
| `GET /v1/observed/self?session=<cleat-session-id>` | the Caller Context: the views in this process showing that session |

Each takes:

- `root=<automation-id>`: only that node and what is under it; 404 when no
  node has the ID. Percent-encode the slashes or not: both work, but a `+`
  must be `%2B`.
- `include=terminal_text`: terminals' screens, where the setting allows.

Unknown parameters are ignored. 400 is a malformed query (or `self` without
a session), 404 every route while automation is off, and 503 when the UI
didn't build the tree within 5 seconds.

### Response

```json
{"schema": 1, "generation": 7, "nodes": [ ... ]}
{"schema": 1, "generation": 7, "session": "<id>", "views": [ ... ]}
```

`schema` is 1 for every route. Adding fields keeps it, and removing or
changing one bumps it. A response over 4 MiB drops terminal text and says
`"truncated": true`; one still over is a 500.

**Encoding.** JSON by default. With `Accept: application/cbor`, the same value
as CBOR in [flotilla ADR 0063](https://github.com/flotilla-org/flotilla)'s
profile, written by `flotilla-cbor`: deterministic, text map keys, and no
tags. In JSON, an integer beyond ±2^53 is `{"u64": "<decimal>"}`, a negative
one too (`{"u64": "-…"}`), since JSON readers can't keep it exactly. CBOR
carries it as an integer.

**Change detection.** One `generation` counter, which the Placement Tree will
share when it lands. While a read waits for the tree or an event stream is
open, the UI builds the tree at each frame's end and compares it with the
last one, and the generation goes up when it differs. Otherwise nothing is
built. A terminal's screen text doesn't count as a change, but its size
does.

### Events

`GET /v1/observed/events` answers `application/x-ndjson` and stays open, as
drydock's `/v1/events` does. Each line is one event:

```json
{"cursor": "<instance-uuid>:<generation>", "event": {"kind": "snapshot", "tree": {"schema": 1, "generation": 7, "nodes": [ ... ]}}}
```

- **The first event is a snapshot,** unless `since` is the current cursor:
  then the stream starts at the next change.
- **After every change,** another snapshot, until the arrangement and Slots
  have Core deltas (#441); `delta` events come then.
- **A cursor that can't be resumed** (another instance's, an older one, or
  nonsense) gets a snapshot. Cursors are opaque, and no history is kept.
- **The stream ends** when automation is turned off, or when its `root`
  goes.
- **Screen text never makes an event by itself.** A stream that asks for
  `include=terminal_text` gets the screens in the snapshots structural
  changes send, not live screen updates.
- **At most 64 streams** are open at once; more get 503.

With `Accept: application/cbor`, each event is an ADR 0063 frame: the magic
`WHEVENTS`, a big-endian u32 length, then the event as one CBOR map with
`"type": "event"` beside `cursor` and `event`.

### Nodes

Nested nodes, each `{id, kind, name, state, durable, bounds, props, children}`.
Empty fields are left out, and the Semantic Tree has no `bounds`.

- **kind:** `workspace`, `panel` or `view`. The Placement Tree (#564) adds
  its own.
- **state:** flags. `selected` is a window's shown workspace, or the View a
  panel shows. `collapsed` is a panel shown without space.
- **workspace:** `name` is the user's name for it, else its subject's.
  `props.subject` is its Workspace Subject, `{provider, kind, id}`. Its child
  is its panel tree, followed by the Views of Slots the arrangement doesn't
  place.
- **panel:** a split has `props.split` (`row` lays children out left to
  right, `column` top to bottom) and `props.weights`, one per child. A tab
  panel's children are its Views in tab order.
- **view:** the current instance of a Slot. `props.slot` is the Slot key,
  `props.content` what it shows (`terminal`, `sessions`, `table`, `jackstay`,
  ...), and `props.role` its role when it has one.

### Terminals

A terminal View carries `props.terminal`:

| Field | |
| --- | --- |
| `session` | the Cleat session it shows, when it has an ID (in-process sessions don't yet: cleat#170) |
| `cwd` | its launch directory |
| `process` | the command it runs |
| `cols`, `rows` | its size, once it has been drawn |
| `text`, `cursor` | with `automation_terminal_text: screen` and `include=terminal_text` only: the visible rows, trailing blanks trimmed, and `{row, col, visible}`. Never scrollback, and never a change by itself. |

Screen text can hold anything a terminal shows. Who may read it is who may
reach the endpoint: the user, through the socket's private directory and
mode 0600, or the pipe's DACL on Windows (ADR 0011).

### Example: the Semantic Tree

```json
{
  "schema": 1,
  "generation": 3,
  "nodes": [
    {
      "id": "workspace/0192f1a2-3b4c-7d5e-8f60-718293a4b5c6",
      "kind": "workspace",
      "name": "wheelhouse#554",
      "state": ["selected"],
      "durable": true,
      "props": {"subject": {"provider": "flotilla", "kind": "vessel", "id": "convoy-1/work"}},
      "children": [
        {
          "id": "workspace/0192f1a2-3b4c-7d5e-8f60-718293a4b5c6/panel/1",
          "kind": "panel",
          "durable": true,
          "props": {"split": "row", "weights": [2.0, 1.0]},
          "children": [
            {
              "id": "workspace/0192f1a2-3b4c-7d5e-8f60-718293a4b5c6/panel/2",
              "kind": "panel",
              "durable": true,
              "children": [
                {
                  "id": "workspace/0192f1a2-3b4c-7d5e-8f60-718293a4b5c6/view/primary",
                  "kind": "view",
                  "state": ["selected"],
                  "durable": true,
                  "props": {
                    "slot": "primary",
                    "content": "terminal",
                    "role": "agent",
                    "terminal": {"session": "convoy-1-work", "cwd": "/repo", "process": "claude", "cols": 120, "rows": 40}
                  }
                }
              ]
            },
            {
              "id": "workspace/0192f1a2-3b4c-7d5e-8f60-718293a4b5c6/panel/3",
              "kind": "panel",
              "durable": true,
              "children": [
                {
                  "id": "workspace/0192f1a2-3b4c-7d5e-8f60-718293a4b5c6/view/u:1",
                  "kind": "view",
                  "durable": true,
                  "props": {"slot": "u:1", "content": "sessions"}
                }
              ]
            }
          ]
        }
      ]
    }
  ]
}
```

### Example: the Caller Context

An agent in a Wheelhouse terminal asks where it is:

```sh
curl --unix-socket "$WHEELHOUSE_SOCKET" "http://wheelhouse/v1/observed/self?session=$CLEAT_SESSION"
```

```json
{
  "schema": 1,
  "generation": 3,
  "session": "convoy-1-work",
  "views": [
    {
      "id": "workspace/0192f1a2-3b4c-7d5e-8f60-718293a4b5c6/view/primary",
      "workspace": "workspace/0192f1a2-3b4c-7d5e-8f60-718293a4b5c6",
      "subject": {"provider": "flotilla", "kind": "vessel", "id": "convoy-1/work"}
    }
  ]
}
```

No view showing the session is an empty list, not an error. Several views
can show one session. The caller states its session and the endpoint
doesn't verify it: Caller Context is a convenience, not security.

## Observed workdirs

`GET /v1/observed/workdirs` (README, "Live core facts") gives each terminal's
directory. Each row now also has `view`, the View's Automation ID, or null.
**`view_id` is deprecated:** it is a session-local number that names nothing
outside this process. Use `view`.

## Recording

`--ingress_record` captures these requests with the others, as
`pm-connect.md` describes: for a read or a stream, its method, path (without
the query) and status.
