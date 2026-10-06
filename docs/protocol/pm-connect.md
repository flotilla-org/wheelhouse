# Metadata ingress over local HTTP

Wheelhouse owns this contract (issue #22). Any local producer may publish;
Flotilla's `pm connect` is one producer. Andamento owns metadata semantics.

## Endpoint and payload

Wheelhouse listens on the endpoint configured by `--andamento_socket`: a Unix
socket on Unix hosts or a local byte-mode named pipe (`\\.\pipe\<name>`) on Windows.
Producers discover it through `WHEELHOUSE_SOCKET` or an explicit socket argument.
On Unix, the socket must live in a private directory (mode 0700); the listener
uses mode 0600. Startup fails if the path already exists, and clean shutdown
removes its own socket.
On Windows the name is used directly, without filesystem-path mapping. The
listener follows [ADR 0011](../adr/0011-windows-local-ipc-uses-named-pipes-with-logical-endpoints.md):
a protected DACL grants SYSTEM and the current user, remote clients are rejected,
and the first pipe instance fails if the name is already served. Shutdown closes
the pipe instances. Clients must verify `GetNamedPipeServerProcessId` and that
process's owner before sending requests. No implicit endpoint stealing or fixed
global name is used.

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
assume one capability per connection. Native pane identity generalization
and dynamic terminal resources are separate work.

## Opt-in ingress recording and replay

Add `--ingress_record` to Wheelhouse, or `--ingress-record` to
`scripts/run-daily-driver.sh` (also supported by the Windows Python launcher).
It is off by default: the ordinary router installs no recorder, performs no
recording work, and creates no recording files. All producers use the same
recorder on Unix sockets and Windows named pipes; no producer changes are needed.

The active file is `logs/ingress.jsonl`, beside `ui_thread.uishell_log` in the app
data folder. With an explicit `--user:<file>`, both logs live beside that file;
the daily driver uses its persistent settings folder. Default retention is four
files of at most 4 MiB each (16 MiB total), including the active file.
Archives are `ingress.jsonl.1` (newest) through `.3` (oldest). Configure with
`--ingress_record_bytes:<bytes>` and `--ingress_record_files:<count>` (1–100);
the launcher uses `--ingress-record-bytes <bytes>` and
`--ingress-record-files <count>`. Reduced bounds also prune existing recordings
on startup. Lines are never split between files. An entry larger than the file
limit disables recording rather than exceeding the bound.

Each JSONL entry carries `recording_id` (one listener run), `sequence` (complete
request receive order), `timestamp_ms` (Unix receive time), `received_ms`
(process monotonic receive time), `method`, `path`, `producer` (`source_id` when
present), decoded `body`, original byte-preserving `body_utf8`, and response
`status`. Producer identity is self-reported; peer identity is not inferred.
Non-patch requests, including health and discovery GETs, have null bodies.
Invalid UTF-8 and oversized bodies have no captured body. Responses can finish
out of order, so replay sorts by recording/run and sequence, not file order.

Recording uses a separate filesystem worker with a bounded 64-entry queue.
Requests never wait for that worker and retain their existing application
status and five-second deadline. A write/start failure, oversized entry, or full
writer queue disables recording with one stderr warning; requests continue.
This is a diagnostic capture, not a delivery journal: abrupt process termination
can lose queued entries, and a disabled recorder leaves an incomplete capture.

Replay accepted patches through the real Andamento C ABI and shipped template:

```sh
python3 tools/replay-ingress.py /path/to/logs/ingress.jsonl* \
  --library /path/to/libandamento_ffi.so > sidebar-states.jsonl
```

Use `.dylib` on macOS or `andamento_ffi.dll` on Windows. `--template <file>`
selects another template. Output includes every visible node's kind, id, label,
state and rendered fields after each accepted patch, including role and convoy
statuses. `data/sidebar/fixture.jsonl` is also accepted directly. Rejected
requests remain in the capture but do not apply during replay. A 422/503 can
represent partial application across windows or a timeout race, so a recording
alone cannot reconstruct those host-specific effects or UI-local selection,
collapse and workspace observations. Replay starts a fresh core for each listener run; rotated files may be supplied
in any order. A retained tail can lack initial facts until a producer reasserts them.

Before attaching a capture to an issue, redact it:

```sh
python3 tools/replay-ingress.py /path/to/logs/ingress.jsonl* --redact > ingress-redacted.jsonl
```

Redaction replaces paths, hosts, labels, recipes, URLs and other private text
facts with deterministic SHA-256 tokens, including string lists and group-path
labels/values. It preserves target/resource references, source ids, fact keys,
lifecycle/status facts, numbers, booleans and ingress ordering. The original
body is rebuilt from the redacted body; malformed bodies are removed. Resource
and source identities are intentionally retained even if they contain a path or
hostname. Hashing is deterministic pseudonymization, not encryption. Redacted
labels may sort differently in the sidebar; patch order is unchanged.
