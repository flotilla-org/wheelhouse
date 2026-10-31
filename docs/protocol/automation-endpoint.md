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

The user setting `automation: off | observe | full` (default `full`) is read
from the user file at launch. Any other value is taken as `full`, with a
warning on stderr.

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
| Window | `window/<window-uuid>` | yes |
| Workspace | `workspace/<uuid>` | yes |
| Panel | `<owner>/panel/<panel-id>`: the owner is `workspace/<uuid>`, or `window/<uuid>` for the sidebar dock | yes |
| View | `<owner>/view/<key>`, and `<owner>/view/<key>@previous` for an instance a keep-previous rebind kept. A workspace View's key is its Slot key. | yes; not on transient hosts |
| Sidebar section and row | `section/<region-key>`, `section/<region-key>/row/<node-key>` | yes |
| Detached card | `window/<uuid>/card/<provider>/<kind>/<id>`; pinned, `pinned/<provider>/<kind>/<id>` | as its Presentation State is |
| Overlay | `window/<uuid>/overlay/<name>`, e.g. `palette` | no |

Schema 1 of the Semantic Tree carries workspaces, panels and views. The other
rows are reserved for the Placement Tree and the Dashboard part.

## Routes

All responses are `Cache-Control: no-store`.

| Route | Answers |
| --- | --- |
| `GET /v1/observed/semantic` | the Semantic Tree |
| `GET /v1/observed/self?session=<cleat-session-id>` | the Caller Context: the views in this process showing that session |

Both take:

- `root=<automation-id>`: only that node and what is under it; 404 when no
  node has the ID. Percent-encode the slashes or not: both work.
- `since=<generation>&wait=<ms>`: answer once `generation` is greater than
  `since`, or 204 when `wait` (at most 60000) passes first. Without `since`,
  the current tree at once.
- `include=terminal_text`: terminals' screens, where the setting allows.

400 is a malformed query (or `self` without a session), 404 the routes when
automation is off, and 503 when the UI couldn't answer in time.

### Response

```json
{"schema": 1, "generation": 7, "nodes": [ ... ]}
{"schema": 1, "generation": 7, "session": "<id>", "views": [ ... ]}
```

`schema` is 1 for both routes. Adding fields keeps it, and removing or
changing one bumps it. A response over 4 MiB drops terminal text first, then
whole workspaces from the end, and says `"truncated": true`.

**Encoding.** JSON by default. With `Accept: application/cbor`, the same value
as CBOR in [flotilla ADR 0063](https://github.com/flotilla-org/flotilla)'s
profile: deterministic (shortest integer and float forms, map keys in the
bytewise order of their encodings, definite lengths), text map keys, and no
tags. In JSON, an integer beyond ±2^53 is `{"u64": "<decimal>"}`, since
JSON readers can't keep it exactly. CBOR carries it as an integer.

**Change detection.** One `generation` counter, which the Placement Tree will
share when it lands. At each frame's end, while a client is waiting or has
read in the last 10 seconds, the tree is built and compared with the last
one, and the generation goes up when it differs. Otherwise nothing is built.
A terminal's screen text doesn't count as a change, but its size does.

### Nodes

Nested nodes, each `{id, kind, name, state, durable, bounds, props, children}`.
Empty fields are left out, and the Semantic Tree has no `bounds`.

- **kind:** `window`, `workspace`, `panel`, `view`, `section`, `row`, `card`,
  `overlay` or `tile`. The Semantic Tree has `workspace`, `panel` and `view`.
- **state:** flags. `selected` is a window's shown workspace, or the View a
  panel shows. `collapsed` is a panel shown without space. `disabled` is
  reserved.
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
| `text`, `cursor` | with `automation_terminal_text: screen` and `include=terminal_text` only: the visible rows, trailing blanks trimmed, and `{row, col, visible}`. Never scrollback. |

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
directory. Each row now also has `view`, the View's Automation ID.
**`view_id` is deprecated:** it is a session-local number that names nothing
outside this process. Use `view`.

## Recording

`--ingress_record` captures these requests with the others, as
`pm-connect.md` describes: for a read, its method, path (without the query)
and status.
