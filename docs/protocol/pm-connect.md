# Metadata ingress over HTTP/UDS

Wheelhouse owns this contract (issue #22). Any local producer may publish;
Flotilla's `pm connect` is one producer. Andamento owns metadata semantics.

## Endpoint and payload

Wheelhouse listens on an explicitly configured Unix socket. Producers discover
it through `WHEELHOUSE_SOCKET` or an explicit socket argument. The socket must
live in a private directory (mode 0700); the listener uses mode 0600. Startup
fails if the path already exists, and clean shutdown removes its own socket.
No implicit socket stealing or fixed global socket name is used.

Send `POST /v1/metadata/patch` using HTTP/1.1, `Content-Type: application/json`,
and one UTF-8 JSON object. The body is the byte-identical metadata-patch message
used by the Zellij Andamento pipe (including `type: "metadata-patch"`). No
additional envelope or newline framing is introduced. Standard HTTP framing,
including chunked requests, applies. Maximum decoded body size is 1 MiB.

| Status | Meaning |
| --- | --- |
| 204 | Patch applied to the receiving native sidebar cores |
| 400 | Invalid JSON/message envelope |
| 404 / 405 | Unknown path / unsupported method |
| 413 | Body too large |
| 415 | Content type is not application/json |
| 422 | Andamento rejected the patch |
| 503 | Queue full, UI unavailable, or application deadline exceeded |

`GET /v1/health` returns 204 while the listener is running. Health does not
assert that the UI has processed any facts. Connections may be reused; clients
must read the HTTP response before considering delivery successful.

## Delivery and lifetime

Delivery is at least once. Retry transport failures and 5xx responses with a
bounded delay; do not retry invalid 4xx requests. A lost response may mean the
patch was already applied. Reapplying the same target/source/key values is
safe, and refreshes TTL from the receiver's monotonic application time.
Producers serialize their patches and periodically reassert current facts,
including after reconnect, because ingress is not durable storage. Unsets are
also safe to repeat. A new native window receives the next reassertion; the
endpoint broadcasts to existing windows without sharing their selection,
collapse, or scroll state. A 204 means all existing windows accepted the patch.
Broadcast application is not transactional: a 422 (a core rejected the patch)
or 503 (a core was unavailable) can follow application to other windows.
A rejection takes precedence over unavailability when both occur.

The HTTP listener only queues bytes. The UI thread applies patches, advances
expiry during quiet periods with a 250ms wake tick while ingress is enabled, and refreshes snapshots between frames. An HTTP
acknowledgement means application, not merely queue acceptance. The bounded
queue and a five-second application deadline prevent indefinite requests.
A timeout may race with application; the same duplicate-delivery rule applies.

## Scope and versioning

The `/v1` path versions this delivery contract. Metadata keys and values remain
Andamento's producer schema. The endpoint has no activate, focus, spawn, or
other host-control verbs: selection dispatches local Andamento host effects.
Recipes in facts are materialized only by explicit UI activation.

This endpoint does not register a presentation manager. Future registration
must allow one client to advertise multiple capability subsets; it must not
assume one capability per connection. Native pane identity generalization,
dynamic terminal resources, and Windows transport are separate work.
