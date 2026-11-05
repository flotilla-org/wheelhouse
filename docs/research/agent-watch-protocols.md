# Research: how agent protocols let a client watch a live thread

Status: research notes, 5 November 2026. Nothing here is decided.

Question: [#687](https://github.com/flotilla-org/wheelhouse/issues/687), for the
map [#686](https://github.com/flotilla-org/wheelhouse/issues/686). How do
existing agent protocols let a client watch a live agent thread, and what does
each give the Agent View's protocol? The prototype's findings are on
[#683](https://github.com/flotilla-org/wheelhouse/issues/683).

**Sources.** All read at these revisions on 5 November 2026:

| Prefix | Source | Revision |
| --- | --- | --- |
| `ag-ui:` | [ag-ui-protocol/ag-ui](https://github.com/ag-ui-protocol/ag-ui), spec 1.0 under `docs/spec/1.0/` (rendered at docs.ag-ui.com/spec/1.0) | `d540f59` (2026-11-05); `@ag-ui/core` 1.0.4 |
| `acp:` | [agentclientprotocol/agent-client-protocol](https://github.com/agentclientprotocol/agent-client-protocol) (formerly under zed-industries), docs under `docs/` | `734309d` (2026-11-04); release 1.15.1 |
| `codex:` | [openai/codex](https://github.com/openai/codex), `codex-rs/app-server*` | `ee80baa` (2026-11-05) |
| `claude-sdk:` | `@anthropic-ai/claude-agent-sdk` 0.3.318 from npm (published 2026-11-04), its `sdk.d.ts` and `bridge.d.ts` | npm tarball |
| | [Streaming Input](https://code.claude.com/docs/en/agent-sdk/streaming-vs-single-mode) docs page | fetched 2026-11-05 |

The Claude SDK ships as compiled JavaScript, so its type declarations and doc
comments are the primary source; behaviour they don't state is marked
[unconfirmed]. ACP material is marked by stage: **v1 stable**, **v2 draft**, or
an **RFD** with its stage (Draft, Active, Completed). Anything in an RFD may
change.

## Summary

| | AG-UI 1.0 | ACP | Codex app-server | Claude Agent SDK |
| --- | --- | --- | --- | --- |
| **Joining a live thread** | Not defined. One POST, one run, one consumer. | v1: `session/load` replays everything, but not into a running turn. Multi-client attach is an open RFD. | `thread/resume` on a loaded thread adds the connection as a subscriber and returns a snapshot that includes the active turn. | No. One host process owns the session. |
| **Resuming after a drop** | No resumption in the HTTP bindings. | v1: none. v2 transport plans `Last-Event-ID`. | Call `thread/resume` again: a fresh snapshot, no cursor into the live stream. | Re-spawn with `resume: sessionId`. The alpha remote bridge resumes by SSE sequence number. |
| **Several watchers** | No. | Open RFD: controllers and observers, first answer wins. | Yes. Approvals go to every subscriber, first answer wins, `serverRequest/resolved` tells the rest. | No, unless the host fans out. |
| **History paging** | No. `MESSAGES_SNAPSHOT` is the whole conversation. | v2 draft: `replayFrom` cursor, only `start` so far. | Yes. `thread/turns/list` and `thread/items/list` with opaque cursors, both directions. | `getSessionMessages(limit, offset)` over the JSONL file. |
| **Agent state** | `STATE_*` (one JSON value per run), `ACTIVITY_*` (per-message widgets), `SUBAGENT_*` (run-scoped). | Plans as updates. v2: `state_update` and a background list; subagents as child sessions. | Typed notifications: plan, goal, status, background terminals, collab agent items. | Typed system messages: `task_*`, `background_tasks_changed`, `session_state_changed`. Todos are tool calls. |
| **Input** | `RunAgentInput` POST. Starts a run. Approvals by interrupt and resume. | `session/prompt`; v2 acks on insertion. `session/cancel`. Permissions are agent-to-client requests. | `turn/start`, `turn/steer`, `turn/interrupt`, an experimental queue. Approvals are server requests. | `streamInput` with `priority`; `interrupt()`, `stopTask()`. `canUseTool` callback. |
| **Transport** | HTTP POST plus SSE, or protobuf. Custom transports allowed. | stdio JSON-RPC. HTTP and WebSocket in an Active RFD. | JSON-RPC over stdio, WebSocket, or a Unix socket that upgrades to WebSocket. | Library over a CLI subprocess on stdio. |

## AG-UI

### Joining

AG-UI 1.0 has no way to attach to a run that someone else started.

- The exchange is one `RunAgentInput` in and one ordered event stream out
  (`ag-ui:docs/spec/1.0/basic/transports/index.mdx:16-31`). A stream MUST begin
  with `RUN_STARTED` or `RUN_ERROR` (`ag-ui:.../events/lifecycle.mdx:14-18`).
- `RUN_STARTED` MAY echo the input "so a consumer that did not make the request
  can still see what the agent was asked" (`lifecycle.mdx:33-37`). The spec
  imagines other consumers but gives them no way in.
- A stream MAY carry several runs, "a replayed thread is the common case". A
  producer restating history MUST restate it as snapshots
  (`MESSAGES_SNAPSHOT`, `STATE_SNAPSHOT`), because "the streaming triads are
  for new material only" (`lifecycle.mdx:190-203`).
- **Joining mid-run breaks a rule.** A producer "MUST NOT send a content or end
  event for an identifier that is not open"
  (`ag-ui:.../basic/patterns/streaming.mdx:21-23`). A watcher that joins
  while a message is streaming has missed its `TEXT_MESSAGE_START`. If it gets
  the partial message in a `MESSAGES_SNAPSHOT`, the following
  `TEXT_MESSAGE_CONTENT` deltas are still a violation, because snapshots don't
  open items.
- The TypeScript client has `connectAgent()`, which calls a protected
  `connect(input)` that throws `AGUIConnectNotImplementedError` by default
  (`ag-ui:sdks/typescript/packages/client/src/agent/agent.ts:440-477`).
  `HttpAgent` doesn't implement it. The docs say it "must be provided by a
  framework like CopilotKit" (`ag-ui:docs/sdk/js/client/abstract-agent.mdx:157-167`),
  and the spec doesn't mention it. A code comment says `connectAgent()` "only
  reads the thread's history and answers nothing" (`agent.ts:57-63`). Its wire
  shape is undefined.

### Resuming

- The SSE binding says: "no stream resumption: SSE's `Last-Event-ID` mechanism
  is not used, and a broken stream cannot be re-entered. Re-running is a new
  run with a new `runId`" (`ag-ui:.../transports/http-sse.mdx:51-55`).
- A dropped stream is a *truncated* run. The consumer MUST NOT invent a
  `RUN_FINISHED` for it (`transports/index.mdx:49-58`).
- Capabilities include `transport.resumable` ("resuming interrupted streams via
  sequence numbers") and `transport.pushNotifications`, but the spec says
  neither binding honours them, and events carry no sequence number
  (`ag-ui:.../basic/capabilities.mdx:93-99`, `schema.json:1659-1684`).

### Several watchers

Not addressed. One POST has one consumer. Interrupts assume a single consumer:
"Coverage is the *consumer's* rule to enforce: the consumer holds the
interrupts the closing `RUN_FINISHED` delivered and it assembles the resume
list" (`ag-ui:.../patterns/interrupt-resume.mdx:54-61`). Nothing says what
happens when two consumers answer.

### History

None on demand. `MESSAGES_SNAPSHOT` is "the complete set of messages the
producer owns, in order". It reconciles by id, and messages missing from it
are dropped (`ag-ui:.../events/state.mdx`, `MESSAGES_SNAPSHOT`). No paging, no
cursor. `RunAgentInput.messages` is "the complete history"
(`ag-ui:.../basic/run-input.mdx:36-40`). #683 already deviates from that.

### State

Three channels apart from messages, all using JSON snapshots and RFC 6902
deltas (`ag-ui:.../basic/patterns/snapshots.mdx`):

- **`STATE_SNAPSHOT` / `STATE_DELTA`.** One untyped JSON value ("any JSON
  value, not only an object"). It persists across runs on a thread, and the
  next run's input carries it back (`events/state.mdx`). There is "no
  per-subagent state" (`events/subagents.mdx`, State).
- **`ACTIVITY_SNAPSHOT` / `ACTIVITY_DELTA`.** Per-`messageId` widgets with an
  open `activityType` string and object `content`. They keep a place in the
  message sequence as role `activity`, and are never sent back
  (`events/activity.mdx`). This fits a checklist or a search placed inline.
- **`SUBAGENT_STARTED` / `_FINISHED` / `_ERROR`** with `subagentRunId`
  attribution and `parentSubagentRunId` nesting. They are **run-scoped**: "A
  run MUST NOT finish while an invocation is still active"
  (`events/subagents.mdx`, Termination). A `suspended` outcome lets a later
  run continue the same id. A Claude or Codex background subagent that outlives
  the turn doesn't fit.

### Input

- `RunAgentInput` fields: `threadId`, `runId`, `messages`, `tools`,
  `context`, `state`, `forwardedProps`, `resume`, `protocolVersion`
  (`run-input.mdx`).
- **There is no mid-run input**: "The protocol has no mid-run channel from the
  consumer, so the run does not wait: it ends" (`interrupt-resume.mdx:5-8`). An
  approval is a `RUN_FINISHED` with an interrupt outcome. The answer is a new
  run whose `resume` list covers every interrupt by id, each answered or
  abandoned.
- **Stop: 1.0 now names a cancelled outcome.** A run "stopped on purpose" MUST
  end with `RUN_FINISHED` and `outcome: {type: "cancelled"}`, closing
  everything it opened first. Closing a stream is truncation, not cancellation
  (`lifecycle.mdx:79-115`, `schema.json:927`). The spec gives no request for
  stopping a run. Steering isn't in the spec.

### Transport

- The binding contract asks for ordered, complete delivery; the input before
  any events; a termination signal you can tell from truncation; and an error
  path for rejected input (`transports/index.mdx:14-36`).
- Standard bindings are HTTP POST with an SSE response (required for HTTP), or
  the same POST negotiated to length-prefixed protobuf frames.
- Custom transports such as WebSockets or pipes are allowed. One carrying JSON
  "SHOULD frame events exactly as the SSE binding does"
  (`transports/index.mdx:60-71`).
- **On a Local Endpoint:** the POST half drops straight onto HTTP over a Unix
  socket. Protobuf is chosen by `Accept` content negotiation, so CBOR could be
  negotiated the same way, as our own media type.

## Agent Client Protocol

ACP is JSON-RPC between one client (an editor) and one agent. v1 is stable
(release 1.15.1). v2 was published as a draft on 20 July 2026
(`acp:docs/announcements/acp-v2-draft.mdx`). Its theme is "moving beyond the
turn", and it states its goals as replay, multiple clients, background work and
queued messages.

### Joining

- **v1:** `session/load` "MUST replay the entire conversation" as
  `session/update` notifications, then responds
  (`acp:docs/protocol/v1/session-setup.mdx:85-191`). `session/resume` reattaches
  without replay (`:207-233`). Neither speaks of joining a turn in progress.
  The v2 prompt RFD names that gap: "If you call load during a currently
  running session, how do you know that the turn is done?"
  (`acp:docs/rfds/v2/prompt.mdx`, Status quo).
- **v2 draft:**
  - `session/load` is folded into `session/resume` with an optional
    `replayFrom` cursor (`acp:docs/rfds/v2/session-resume-replay.mdx`).
  - Replay "uses the same updates as live delivery" and MAY send only an
    entity's current state, such as a terminal's output snapshot.
  - Resume sends a current-state snapshot (`state_update`) after any replay
    and before its response (`rfds/v2/prompt.mdx`, Foreground state contract).
  - Replay rebuilds rather than merges, and IDs are not guaranteed stable
    across reconnects.
- **Attaching to a live session:** the Multi-Client Session Attach RFD is an
  open PR, not merged ([#533](https://github.com/agentclientprotocol/agent-client-protocol/pull/533),
  opened 2026-02-18, last updated 2026-05-20). It proposes `session/attach`
  with `historyPolicy: full | pending_only | none`. Two merged RFDs cite it as
  where multi-client semantics will live (`rfds/session-status.mdx:211`,
  `rfds/session-archive.mdx:217`).

### Resuming

- **v1:** the Streamable HTTP RFD (stage **Active**) says sessions outlive
  connections, and the client reconnects via `session/load`. But "In-flight
  messages are not replayed. There is no message sequencing or stream
  resumption" (`acp:docs/rfds/streamable-http-websocket-transport.mdx`,
  Durability, v1).
- **v2 plans** IDs on streamed messages, "SSE `Last-Event-ID`-style
  resumption", defined reconnection, and keepalive. These are transport
  requirements that can land after v2 (same section, v2).
- `replayFrom` supports only `{type: "start"}` so far. Later cursors for a
  message or checkpoint are left as future work.

### Several watchers

- **v1 and v2 define no multi-client sharing.**
- v2 prepares for it. A prompt's response returns the agent-assigned
  `messageId` of the inserted user message. "Other clients identify the message
  by `messageId` without needing the original request" (`rfds/v2/prompt.mdx`).
- PR #533 proposes:
  - **controller** and **observer** roles;
  - `request_permission` broadcast to all controllers, **first response wins**,
    and a `permission_resolved` notification to everyone;
  - `session/detach`;
  - a proxy or multiplexer in front of unchanged agents, using WebSocket or
    SSE.

  That is the same model Codex's app-server already runs.

### History

- No paging. v1 replays everything.
- v2 adds a cursor type that admits finer cursors later
  (`session-resume-replay.mdx`).
- Agents "are NOT REQUIRED to persist every accepted input". Clients MUST NOT
  assume an acknowledged message will appear after resume
  (`rfds/v2/prompt.mdx`, Correlation and replay).

### State

- **Plans (v1 stable):** a `plan` session update. Entries carry `content`,
  `priority` and `status`, and each update replaces the whole list
  (`acp:docs/protocol/v1/agent-plan.mdx`).
- **Plans (v2 draft):** `plan_update` with `{type: "items", planId, entries}`.
  Markdown and file plans stay behind the Draft plan-operations RFD
  (`rfds/v2/plan-variants.mdx`).
- **Session state (v2 draft):** `state_update` with `running`, `idle` (plus
  `stopReason`, usage) or `requires_action`. These are snapshots of
  aggregate *foreground* activity (`rfds/v2/prompt.mdx`).
- **Background work (v2 draft):**
  - Background work is terminals, tool calls and subagent sessions that
    outlive the foreground. No new entity type is added.
  - Each `state_update` can carry a `background` list referencing them.
  - A new `paused` tool-call status.
  - Per-entity `capabilities.cancel` with `tool_call/cancel` and
    `terminal/cancel`.
  - Housekeeping the agent starts itself "SHOULD NOT" be listed
    (`rfds/v2/background-work.mdx`).

  The RFD notes that `claude-agent-acp` and `codex-acp` each use a private
  `async_task_*` extension that "only one Client negotiates, so other Clients
  see nothing".
- **Subagents (RFD, Active):** each subagent is its own restricted ACP session.
  A `subagent_update` upsert associates it with the parent. Messages between
  sessions are `session_message` upserts. Only cancellation is defined for
  clients (`acp:docs/rfds/subagents.mdx`).

### Input

- **v1:** `session/prompt` is a request whose response ends the turn.
  `session/cancel` stops it. Permissions are agent-to-client
  `session/request_permission` requests
  (`acp:docs/protocol/v1/tool-calls.mdx:138-144`).
- **v2 draft:**
  - "acceptance means insertion". The response carries the inserted user
    message's `messageId` and returns before the turn ends.
  - Prompts sent during active work are not rejected. The agent inserts them
    now or holds them in order. "Capabilities for explicit queueing or
    steering are left to a later RFD".
  - Failures after insertion end with an `error` stop reason on the idle
    `state_update` (`rfds/v2/prompt.mdx`).
  - Permission requests gain their own `title`, `description` and an
    extensible `subject` (`announcements/acp-v2-draft.mdx`).
- **IDs are owned by the agent.** "No client-generated `promptId`". So a lost
  response leaves the submission ambiguous, and the RFD says so
  (`rfds/v2/prompt.mdx`).

### Transport

- **v1 stable:** stdio only, newline-delimited JSON-RPC, client spawns agent
  (`acp:docs/protocol/v1/transports.mdx`).
- **Streamable HTTP and WebSocket (RFD, Active):**
  - one `/acp` endpoint;
  - `POST` for client messages, answering `202` with the response sent later
    on a stream;
  - a long-lived `GET` SSE stream per connection, plus one per session
    (`Acp-Connection-Id`, `Acp-Session-Id` headers);
  - WebSocket upgrade on the same endpoint;
  - HTTP/2 and cookie support required.
- **On a Local Endpoint:** the per-session GET stream with POSTed input is
  close to what we want. Requiring HTTP/2 and cookies is a burden on a Unix
  socket. [unconfirmed] whether the RFD allows HTTP/1.1 for local use.

## Codex app-server

JSON-RPC v2 between Codex's own clients (the TUI, IDE extensions) and the app
server. Method names are listed in
`codex:codex-rs/app-server-protocol/src/protocol/common.rs`. Methods marked
`#[experimental]` need the client to opt in.

### Joining

- `thread/resume` on a thread that is already loaded **adds the connection as a
  subscriber** (`codex:codex-rs/app-server/src/request_processors/thread_lifecycle.rs:1181`,
  `try_add_connection_to_thread`).
- The response snapshot **merges the active turn**: `merge_turn_history_with_active_turn`,
  `merge_active_turn_into_page` (`thread_lifecycle.rs:1060-1150`).
- After the response, the server sends token-usage and goal snapshots, then
  **replays pending server requests** (approvals, questions) to the new
  connection (`thread_lifecycle.rs:1255-1290`,
  `app-server/src/outgoing_message.rs:525-560`).
- For paginated threads the resume can skip full history.
  - `excludeTurns: true` returns metadata and "live-resume state".
  - `initialTurnsPage` asks for a first page in the response.
  - The response returns `turnsBackwardsCursor` and `itemsBackwardsCursor`
    for paging back (`app-server-protocol/src/protocol/v2/thread.rs:354-470`).
- "Full-history hydration is deprecated for paginated threads" (`thread.rs`,
  `exclude_turns` doc).
- `thread/unsubscribe` leaves (`thread.rs:740-758`). `thread/loaded/list`
  lists live threads.
- The #683 prototype confirmed this: a second client resumed a running
  thread, paged its history, then followed it live.

### Resuming

- **No sequence numbers.** Notifications are wrapped with `emitted_at_ms` and
  nothing else (`outgoing_message.rs:1022-1027`). A dropped client calls
  `thread/resume` again and gets a fresh snapshot.
- [unconfirmed] whether a notification can fall between the subscriber being
  added and the response being sent. The connection joins before the response
  is built (`thread_lifecycle.rs:1181-1255`), so a client should expect
  notifications that overlap the snapshot and must reconcile by item and turn
  id.

### Several watchers

- **Yes.** Each server request is sent to every subscribed connection
  (`send_request_to_connections`, `outgoing_message.rs:388`).
- The first response takes the callback out of the shared map, and later
  answers find nothing (`take_connection_callback`, `outgoing_message.rs:657-687`).
- Resolution goes out to all subscribers as `serverRequest/resolved
  {threadId, requestId}` (`outgoing_message.rs:232-241`,
  `protocol/v2/notification.rs:74-77`).
- **Who may answer is gated in two places.**
  - "Human command" approvals go only to connections that set
    `requiredHumanApproval` at `initialize`, with the experimental API on
    (`request_processors/initialize_processor.rs:144-147`,
    `outgoing_message.rs:1029-1032`).
  - User-verification elicitations are owned by one connection.

### History

`thread/turns/list` and `thread/items/list` page with opaque cursors,
`limit`, `sortDirection`, and an `itemsView` (summary or full)
(`thread.rs:1809-1822`). `thread/read` reads a thread that isn't loaded.
`thread/timeline/list` is experimental.

### State

Typed notifications, separate from items:

- **Plans:** `turn/plan/updated {threadId, turnId, explanation, plan: [{step,
  status}]}` (`protocol/v2/turn.rs:630-643`). `item/plan/delta` streams
  proposed-plan items (experimental).
- **Thread status:** `thread/status/changed`. `ThreadStatus` is `notLoaded`,
  `idle`, `systemError`, or `active {activeFlags: [waitingOnApproval |
  waitingOnUserInput]}` (`thread.rs:1721-1738`).
- **Background terminals:** experimental `thread/backgroundTerminals/list`,
  `/terminate` and `/clean`. ACP's background-work RFD notes that
  `ThreadStatus` has no flag for them, so a thread reports `idle` while
  terminals run.
- **Subagents** are threads of their own. A `CollabAgentToolCall` item names
  sender and receiver thread ids and the target agents' last known status,
  and there is a `SubAgentActivity` item (`protocol/v2/item.rs:437-470`).
- **Also:** `thread/goal/*`, `turn/diff/updated`, `thread/tokenUsage/updated`,
  and the experimental `thread/queue/changed`.

### Input

- `turn/start` (input items plus per-turn overrides).
- `turn/steer` with `expectedTurnId`, a precondition that fails if the active
  turn has changed, and an optional `clientUserMessageId`
  (`turn.rs:321-346`).
- `turn/interrupt {threadId, turnId}` (`turn.rs:361-369`).
- **An experimental server-held queue:** `thread/queue/add`, `list`,
  `update`, `delete`, `reorder` and `start`, plus a `thread/queue/changed`
  notification. A `QueuedSubmission` is `{id, input, clientUserMessageId}`
  (`thread.rs:930-1000`). This bears on the map's open question about queued
  prompts the producer holds.
- Approvals are server-to-client JSON-RPC requests on the thread
  (`item/commandExecution/requestApproval`, `item/fileChange/requestApproval`,
  `item/tool/requestUserInput`, and others), answered by JSON-RPC response.
- **Correlation:** `clientUserMessageId` is **client-generated**, the opposite
  of ACP v2.

### Transport

- `--listen` takes `stdio://` (the default), `unix://` or `unix://PATH`,
  `ws://IP:PORT`, or `off`
  (`codex:codex-rs/app-server-transport/src/transport/mod.rs:100-170`).
- **The Unix socket is an HTTP endpoint upgraded to WebSocket**, with optional
  zstd compression (`transport/unix_socket.rs`, `unix_socket_tests.rs:126`).
- The Windows build uses `UnixListener` through `codex_uds`, not a named pipe
  (`unix_socket.rs:114-123`). [unconfirmed] which Windows socket API that is.
- There is also a `remote_control` transport (`transport/remote_control/`).
  Not read.
- **On a Local Endpoint:** Codex already serves JSON-RPC over HTTP on a Unix
  socket, but behind a WebSocket upgrade. An adapter (the #683 host) can turn it
  into POST for input and an event stream for output.

## Claude Agent SDK

A library (TypeScript, Python) that spawns the Claude Code CLI as a subprocess
and talks to it over stdio. `spawnClaudeCodeProcess` lets a host substitute the
process (`claude-sdk:sdk.d.ts:2553`). `query({prompt, options})` returns a
`Query`, an async generator of `SDKMessage` with control methods
(`sdk.d.ts:2960-3370`).

### Joining

- **The local SDK has no join.** The process that called `query()` owns the
  session's only stream.
- Another process can read the transcript (`getSessionMessages`) or start its
  own process on the session with `resume`. That is a second agent process on
  the same session file, not a watcher. [unconfirmed] whether two live
  processes on one session are safe.
- **`reinitialize()`:** "Use this after a transport gap (e.g. reattaching to a
  daemon whose ring buffer evicted frames during a disconnect): the CLI's
  response carries any `can_use_tool` / `request_user_dialog` control requests
  the loop is still blocked on, and the SDK redelivers them"
  (`sdk.d.ts:3090-3110`). So an on-demand daemon with a ring buffer exists, and
  can be reattached. Its wire protocol isn't public. [unconfirmed]

### Resuming

- **Locally:** `resume: sessionId` loads the conversation from the JSONL
  transcript. `resumeSessionAt` resumes at a given message UUID, and
  `forkSession` forks (`sdk.d.ts:2164-2190`, `872`).
- **The alpha remote bridge resumes by sequence number**
  (`claude-sdk:bridge.d.ts`, `@alpha`, for claude.ai's session ingress).
  - `getSequenceNum()` is the "Live SSE event-stream high-water mark. … Persist
    this and pass back as `initialSequenceNum` on re-attach so the server
    resumes instead of replaying full history".
  - `initialSequenceNum` "Seeds the first SSE connect's `from_sequence_num`".
  - `reconnectTransport` "Carries the SSE sequence number so the server resumes
    the stream, and hands the old transport's undelivered writes to the new
    one".
  - An `outboundOnly` mode is for "mirror-mode attachments where the remote UI
    should see the session but not be able to drive it".

  This is Anthropic's own watch-from-elsewhere path: SSE with a sequence
  number, a resume cursor, and a read-only mode.

### Several watchers

None in the local SDK. `canUseTool` is one callback in the host
(`sdk.d.ts:1662-1665`). With several watchers, the host must fan out and
decide who wins. The bridge gives claude.ai a second seat: the host forwards
`can_use_tool` with `sendControlRequest`, and can dismiss a prompt answered
locally with `sendControlCancelRequest` (`bridge.d.ts:50-62`).

### History

`getSessionMessages(sessionId, {limit, offset, includeSystemMessages})` reads
the JSONL transcript and rebuilds the chain via `parentUuid` (`sdk.d.ts:925-955`).
`getSubagentMessages` and `listSubagents` read subagent transcripts. Paging by
offset over a file works for backfill, but it isn't live.

### State

Separate typed system messages, apart from assistant and user messages:

- `session_state_changed {state: idle | running | requires_action}`
  (`sdk.d.ts:6237-6243`). The bridge's `SessionState` has the same three
  values, and so does ACP v2.
- **Background tasks:**
  - `task_started` carries `task_id`, `run_id`, `tool_use_id`,
    `subagent_type`, `is_backgrounded`, and nesting depth.
  - Then come `task_updated`, `task_progress` and `task_notification`.
  - `background_tasks_changed` carries "Every live background task after the
    change. REPLACE semantics", with `parent_task_id` and `ambient` (exclude
    from activity indicators) (`sdk.d.ts:3840-3870`, `6466-6490`).
- **Subagent content** comes as messages with `parent_tool_use_id`, plus
  `forwardSubagentText` (`sdk.d.ts:1940-1945`).
- **Todos and tasks are tool calls**: `TodoWrite`, `TaskCreate` and
  `TaskUpdate` inputs (`claude-sdk:sdk-tools.d.ts:1089`, `2793`). No state
  channel. The host derives the list from tool inputs.
- **Streaming:** `includePartialMessages` adds token-level `stream_event`
  messages (`sdk.d.ts:1936-1939`). Without it you get one record per block.

### Input

- **Streaming input** (an `AsyncIterable<SDKUserMessage>` or `streamInput`)
  is the recommended mode. It supports "Queued messages: send multiple
  messages that process sequentially, with ability to interrupt"
  ([Streaming Input](https://code.claude.com/docs/en/agent-sdk/streaming-vs-single-mode)).
- `SDKUserMessage` has `priority?: 'now' | 'next' | 'later'` and `origin`
  (`sdk.d.ts:6656-6669`). [unconfirmed] what each priority does. The type
  doesn't document it. Presumably `now` steers the running turn and `later`
  queues.
- **Stop:** `interrupt()` stops the turn, `stopTask(taskId)` stops one
  background task, and `backgroundTasks(toolUseId?)` backgrounds foreground
  tasks.
- **Approvals:** `canUseTool` in the host, delivered as control requests on
  stdio.
- **Input is not tied to a run.** It goes into one long-lived stream.

### Transport

- The SDK speaks stdio to a child process, and the host is a library inside
  our process.
- **On a Local Endpoint:** a Claude producer is a host process that runs
  `query()`, serves the Local Endpoint, and fans events out to watchers. That
  is the role #683's Node host played for Codex. Nothing in the SDK serves
  HTTP.

## Synthesis

### What we can take as it is

- **AG-UI's vocabulary:** events, `RunAgentInput`, interrupt and `resume`, and
  snapshots with RFC 6902 deltas.
- **AG-UI 1.0's cancelled outcome.** 1.0 now defines `outcome: {type:
  "cancelled"}`, so #683's planned extension is standard. Rename to match the
  spec: the type is `cancelled`, with no extra fields (`schema.json:927`).
- **AG-UI's activity events** for state that has a place in the conversation,
  such as an inline checklist or a search.
- **The three-value session state** (`idle`, `running`, `requires_action`).
  ACP v2, the Claude SDK and its bridge use it. Codex has the same thing as
  `idle` or `active{waitingOnApproval|waitingOnUserInput}`. Carry it as thread
  state, not as run events.
- **Codex's watcher model:** fan approvals out to every watcher, first answer
  wins, broadcast the resolution, and replay pending requests to anyone who
  joins. ACP's attach RFD (#533) proposes the same rules. Codex supplies it
  natively, so its adapter passes it through.
- **Codex's paging:** opaque cursors in both directions, and a resume that can
  ask for "metadata and first page only".
- **Background work as references to existing entities**, as in ACP v2: tool
  calls, terminals and subagents that outlive the turn, listed in a snapshot
  that replaces the whole set. Claude's `background_tasks_changed` is already
  shaped this way.
- **The transport shape:** POST for input, and a long-lived GET event stream
  per thread. AG-UI's request half and ACP's per-session GET stream both
  fit HTTP on a Unix socket. CBOR can be a negotiated media type, the way
  AG-UI negotiates protobuf.

### What the Agent View protocol must add

1. **A watch stream per thread, apart from any run:**
   `GET /threads/{id}/events`. Every frame carries `seq`. AG-UI ties a stream
   to the POST that started a run, and no spec here has a standard watch
   stream.
2. **Joining mid-run.** AG-UI forbids content events for an item the stream
   never opened. So a join needs a rule:
   - The join opens with snapshots: messages, thread state, and any open run
     with its open items.
   - After the snapshot, deltas for an item open in the snapshot are legal.
   - The other choice is to restate open items as `*_START` plus their content
     so far, then continue live.

   This is our deviation from AG-UI and should be written down.
3. **Resuming from `seq`.** `?after=N` replays from a bounded buffer. If `N`
   has been evicted, the stream re-snapshots. Claude's bridge
   (`from_sequence_num`) and its daemon ring buffer, and ACP's planned
   `Last-Event-ID`, point the same way. Codex has no `seq`, so its adapter must
   number events itself and re-snapshot on its own reconnects.
4. **Backfill on demand:** `GET /threads/{id}/events?before=N` (#683), or
   message paging. Adapters map it to `thread/turns/list` and
   `thread/items/list` (Codex) or `getSessionMessages` (Claude).
5. **Thread-scoped agent state that outlives runs.** AG-UI's subagents must
   close before `RUN_FINISHED`, and its state is one untyped value per run.
   Background subagents, tasks and terminals need a thread-level channel with
   a typed schema of our own: `STATE_SNAPSHOT` / `STATE_DELTA` on the watch
   stream with a schema we define, or our own events. Plans and todos belong
   there too. For Claude, todos come from `TodoWrite` and `TaskCreate` tool
   inputs.
6. **Answers when several watchers are attached.**
   - Interrupts go to every watcher.
   - The first `resume` wins. Later ones are rejected with a clear error.
   - A resolution event, our `serverRequest/resolved`, goes to everyone.
   - Pending interrupts are replayed in the join snapshot.
   - Optionally, observer and controller roles (ACP #533, Claude's
     `outboundOnly`).

   AG-UI's coverage rule (resume answers every open interrupt) assumes one
   consumer, so with several watchers the producer has to own the rule.
7. **Acknowledging input.** The POST answers with the message id once the
   message is inserted (ACP v2), and accepts a client-generated id
   (Codex's `clientUserMessageId`) so a lost response isn't ambiguous. That
   also gives the composer its "pending until echoed" state (#683).
8. **Steering versus queueing.** No spec here defines it. Codex has
   `turn/steer` and an experimental queue, Claude has `priority`, and ACP
   defers it to a later RFD. Our input needs a per-message mode, and perhaps a
   queue the producer advertises.
9. **Stopping things.** Stopping a run (`POST …/stop`, ending with
   `cancelled`) and stopping one background entity (ACP's `tool_call/cancel`,
   Claude's `stopTask`, Codex's `backgroundTerminals/terminate`) are separate
   requests.

## Open questions

- Is a mid-run join a snapshot followed by deltas, or restated `*_START`
  triads? It decides whether a stock AG-UI consumer can read our watch stream.
- Is thread state one schema for every harness, or a common core plus
  per-harness extensions?
- Should watcher roles be in the first slice, or should every watcher be a
  controller until a use needs observers?
- Can the Claude producer use the CLI's own daemon and its ring buffer, or
  must it be a host we run? That waits on the daemon's protocol being
  documented.
