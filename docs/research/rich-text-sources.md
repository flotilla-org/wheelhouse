# Research: which sources feed Rich Text first, and their shapes

Status: research notes, 31 October 2026. Nothing here is decided.

Question: [#539](https://github.com/flotilla-org/wheelhouse/issues/539), for the
map [#537](https://github.com/flotilla-org/wheelhouse/issues/537). What do the
agent transcripts and logs that Wheelhouse will show look like, how do they
arrive, and how big do they get? Which two or three should the first slice
support? Terms (**Rich Text**, **Block**, **Embedded View**) are as in
CONTEXT.md on branch `research/rich-text`.

**Sources.**

- Claude Code: real files under `~/.claude/projects/` on this machine, written
  by Claude Code 2.1.283 to 2.1.309. The format is not publicly documented, so
  everything below is observed. Only record shapes, keys and counts were read;
  no message content was copied.
- Codex: real rollouts under `~/.codex/sessions/` (Codex CLI 0.11x to 0.16x;
  installed `codex-cli 0.162.1`), and `openai/codex` source at `main`
  `67f0994b5`. Paths below are under `codex-rs/` in that tree.
- Flotilla: `~/dev/flotilla` at `49ccb29c5` (23 October 2026), written
  `flotilla:` below.
- Cleat: `~/dev/cleat` at `ce8712a` (10 October 2026), written `cleat:`, and
  its recordings under `~/.local/state/cleat/`.
- Logs: every `*.log` over 10 kB under `~/.cache` and `~/.local/state/flotilla`
  (31 files).

All measurements were made by scripts reading these files on 31 October 2026.

## Summary

- **Both agent transcripts on disk are append-only JSON Lines of completed
  entries.** Neither persists token-level streaming. The only "update in place"
  a reader sees is a tool call whose result arrives in a later line, keyed by
  the call's id, plus (Claude) one API message split across several lines.
- **Token streaming exists only on live channels:** Codex's app-server
  notifications (`item/agentMessage/delta`, `item/commandExecution/outputDelta`)
  and Claude Code's `--output-format stream-json --include-partial-messages`.
- **Flotilla publishes no transcripts.** It records the harness's transcript
  path without reading it, keeps markdown and plain-text artifacts, and its
  self-describing tables carry state, not conversation. Its table stream is,
  though, a ready snapshot-plus-keyed-upsert transport.
- **ANSI appears only on PTY output.** Every captured log file on this machine
  has zero escape sequences. Cleat recordings are full of them, but the big
  ones are agent TUIs painted by cursor addressing, not line logs; they need a
  VT engine, not an ANSI-to-runs parser.
- **Recommendation:** Claude Code JSONL, Codex rollouts, and line-oriented
  ANSI text. See the last section.

## 1. Claude Code session transcripts

### Layout on disk

- One directory per working directory, named by the path with `/` and `.`
  replaced by `-`: `~/.claude/projects/-home-robert-dev-wheelhouse/`.
- A session is `<session-uuid>.jsonl`. Beside it a directory `<session-uuid>/`
  holds:
  - `subagents/agent-<id>.jsonl`, one transcript per subagent, with
    `agent-<id>.meta.json` (keys `agentType`, `description`, `toolUseId`,
    `spawnDepth`, `requestShape`, `requestNonInteractive`) and
    `agent-<id>.prefix.json`;
  - `tool-results/*.txt`, large tool outputs spilled out of the transcript (65
    files, 2.8 MB in one session);
  - `custom-title.json`.
- Every record in a subagent file has `isSidechain: true` (33,088 of 33,314;
  the rest have no flag). Main-session records have `false` or no flag. The
  link from parent to child is the parent's `Agent` tool call id
  (`meta.json` `toolUseId`), and the child's records carry `agentId`.

### Record types

Each line is one JSON object with a `type`. Counts over all 205 files:

| `type` | main | subagent | What it is |
|---|---:|---:|---|
| `assistant` | 5,676 | 13,956 | One content block of a model response |
| `user` | 2,827 | 8,501 | A user prompt, or the tool results returned to the model |
| `attachment` | 3,885 | 10,628 | Context injected into a turn (reminders, file mentions, diagnostics) |
| `system` | 320 | 3 | Subtypes `stop_hook_summary` (311), `compact_boundary` (7), `local_command`, `informational`, `api_error` |
| `queue-operation`, `last-prompt`, `custom-title`, `agent-name`, `mode`, `permission-mode`, `cost-state`, `pr-link`, `frame-link`, `bridge-session`, `atis-latch`, `artifact-*` | 7,000+ | 0 | Session metadata, re-appended when it changes |
| `file-history-snapshot`, `file-history-delta` | 225 | 0 | Checkpoints of edited files, keyed by `messageId` |
| `api-request`, `api-request-blob`, `api-request-shape` | 103 | 226 | Request digests and blobs |

Conversational records (`user`, `assistant`, `attachment`, `system`) share an
envelope: `uuid`, `parentUuid`, `timestamp`, `sessionId`, `cwd`, `gitBranch`,
`version`, `isSidechain`, `agentId`, `userType`, `entrypoint`, `slug`. They
carry `message`, an Anthropic Messages API message (`role`, `content`, and for
assistants `id`, `model`, `stop_reason`, `usage`).

A reader must skip unknown types: product-specific ones (`pr-link`,
`frame-link`, `bridge-session`, `artifact-autoreact-ledger`) appear and vary by
host app and version.

### Content blocks

| Record | Block `type` | Count | Notes |
|---|---|---:|---|
| assistant | `tool_use` | 10,675 | keys `id`, `name`, `input`, `caller` |
| user | `tool_result` | 10,674 | `tool_use_id`; `content` is a string (10,530) or a list of `text` (146), `tool_reference` (32) or `image` (7) blocks |
| assistant | `thinking` | 7,393 | keys `thinking`, `signature`. The text is usually empty: p50 and p90 are 0 characters, p99 296 |
| assistant | `text` | 1,564 | Markdown. Length p50 140, p90 3,757, max 23,209 characters |
| user | string content | 559 | A typed prompt |
| user | `text` | 97 | Prompt with structured content |

- **Every `assistant` line holds exactly one content block** (19,635 of
  19,635). One API response is split across lines that share `message.id`:
  1 line for 3,058 messages, 2 for 5,853, 3 for 1,473, up to 7. A thinking
  block followed by a tool call is the common pair.
- **Tool results come in a later `user` line,** matched by `tool_use_id`. The
  gap is 1 line at p50 and p90, and at most 13 (parallel calls). 81 calls had
  no result (interrupted or the session ended), and 3 results had no call in
  the same file.
- **`toolUseResult`** on the user record is a structured side copy of the
  result: `stdout`, `stderr`, `interrupted`, `isImage` for Bash (2,096);
  `filePath`, `originalFile`, `structuredPatch` for edits (34); `agentId`,
  `status`, `outputFile` for subagents (93). A renderer can draw an edit as a
  diff from `structuredPatch` rather than the text summary.
- Tool calls are dominated by `Bash` (9,784 of 10,740), then `Write`, `Read`,
  `Edit`, `Agent`, `Skill`, `WebFetch`.

### Ordering and edits

- `uuid` never repeats within a file. `parentUuid` names an earlier line in
  all but 3 cases. The `parentUuid` graph is a tree, not a list: 85 parents
  have more than one child across the 100 largest files (parallel tool results
  and rewinds).
- **Nothing is rewritten in place.** Changes to session state are appended as
  a new record that supersedes the old one: `custom-title` 903 times,
  `last-prompt` 1,013, `agent-name` 899 across 108 main files. The latest wins.
- **Compaction** appends a `system` record with subtype `compact_boundary` and
  `logicalParentUuid` (7 seen), then a `user` record flagged
  `isCompactSummary`. Earlier lines stay in the file.
- Partial streaming is not persisted. One assistant record carried
  `isAbortedMidStream`.

### Sizes

Only 4 interactive sessions remain on this machine; the other 104 main files
are one-shot Flotilla probe sessions (`-home-robert--local-state-flotilla-probe-cwd`,
10 to 19 lines each).

| | files | lines p50 | lines p90 | lines max | bytes p50 | bytes p90 | bytes max |
|---|---:|---:|---:|---:|---:|---:|---:|
| Main sessions of real work | 3 | 1,121 | | 17,734 | 2.2 MB | | 35.5 MB |
| Subagent transcripts | 97 | 239 | 585 | 2,663 | 836 kB | 2.3 MB | 8.3 MB |

- Line length: p50 1.7 kB, p90 5.4 kB, max 514 kB.
- Tool result length: p50 1,273, p90 7,795, p99 25,191, max 512,920 characters.
- Escape sequences in tool results: SGR in 3 of 10,683, OSC 8 in none, a bare
  CR in 10. Claude Code's tools return mostly plain text.

### Delivery

- **File tail.** The file only grows. A reader can follow it by offset with
  inotify, and resume at a byte offset.
- **Hooks** carry the path. Claude Code's hook payloads include
  `transcript_path`, and `SubagentStop` includes `agent_transcript_path`;
  Flotilla parses both (`flotilla: crates/flotilla-agent/src/hooks.rs:26`,
  test payload at `:783`).
- **Live streaming** is available only in print mode:
  `claude -p --output-format stream-json --include-partial-messages`
  (`claude --help`, 2.1.295). An interactive session's partial tokens are
  visible only on its terminal.

## 2. Codex CLI rollouts

### Layout on disk

- `~/.codex/sessions/YYYY/MM/DD/rollout-<timestamp>-<thread-uuid>.jsonl`. 510
  files here, all with `history_mode: paginated`.
- Cold rollouts are recompressed to `.jsonl.zst` by a background worker
  (`rollout/src/compression.rs:26`, `spawn_rollout_compression_worker` `:51`).
  Before appending to one, the writer decompresses it
  (`materialize_rollout_for_append`, `rollout/src/recorder.rs:2088`), then
  opens with `append(true)` (`:2094`). A reader must accept both forms.
- **Subagents are separate rollouts.** `session_meta` carries `source`
  (`{"subagent": {"thread_spawn": ...}}` for 250 of 510 files),
  `parent_thread_id`, `forked_from_id`, `agent_nickname`, `agent_role`.

### Record shape

Every line is `{timestamp, ordinal, type, payload}` (1,090,739 lines; 598 also
carry `metadata`). `ordinal` strictly increases within every file.

| `type` / `payload.type` | Count | What it is |
|---|---:|---|
| `response_item` / `function_call` | 206,870 | Tool call: `call_id`, `name`, `namespace`, `arguments` (JSON string) |
| `response_item` / `function_call_output` | 206,755 | Result by `call_id`; `output` is a string (199,937) or a list (6,818) |
| `response_item` / `reasoning` | 133,987 | `summary`, `content`, `encrypted_content` |
| `response_item` / `message` | 100,336 | `role` (assistant, user, developer); content `output_text` or `input_text` |
| `response_item` / `custom_tool_call`, `custom_tool_call_output` | 49,048 / 48,938 | Freeform tools such as `apply_patch` |
| `response_item` / `web_search_call`, `tool_search_*`, `agent_message` | 371 | |
| `event_msg` / `item_completed` | 103,768 | A finished `TurnItem`: `AgentMessage`, `UserMessage`, `CommandExecution`, `FileChange`, `ContextCompaction`, `Reasoning`, `McpToolCall`, `WebSearch`, `SubAgentActivity`, ... with `turn_id`, `started_at_ms`, `completed_at_ms` |
| `event_msg` / `token_count`, `task_started`, `task_complete`, `turn_aborted` | 226,585 | Turn boundaries and usage |
| `turn_context` | 11,192 | Model, cwd, sandbox and approval policy per turn |
| `compacted` | 1,257 | `replacement_history`, window numbers |
| `session_meta` | 515 | First line (and on resume) |

There are two parallel views of a turn: raw model `response_item`s, and
`item_completed` events with typed `TurnItem`s (for example a
`CommandExecution` with its aggregated output, or a `FileChange`). For display,
the `TurnItem`s are the better shape.

### What is persisted

`rollout/src/policy.rs` decides:

- **Deltas are never persisted:** `AgentMessageContentDelta`,
  `ExecCommandOutputDelta`, `ReasoningContentDelta`, `ItemStarted`,
  `ExecCommandBegin` and the rest are "transient, non-durable events"
  (`policy.rs:149` to `:226`).
- `ItemCompleted` is persisted in paginated mode (`:232`).
- Command output over 64 KiB is cut in the middle with a marker
  (`PERSISTED_COMMAND_OUTPUT_MAX_BYTES`, `:17`); MCP results likewise (`:16`).

### Sizes

| | p50 | p90 | p99 | max |
|---|---:|---:|---:|---:|
| Lines per rollout | 433 | 11,429 | 15,285 | 19,240 |
| Bytes per rollout | 857 kB | 20.3 MB | | 44.8 MB |
| Bytes per line | 812 | 2,795 | 22,466 | 5.9 MB |

SGR appears in 860 of 255,693 tool outputs, OSC 8 in 1.

### Delivery

- **File tail**, as for Claude Code, with the `.zst` caveat for cold files.
- **App-server** (`codex app-server`, JSON-RPC) streams the live turn. Its
  notifications include `turn/started`, `turn/completed`, `item/started`,
  `item/completed`, `item/agentMessage/delta`, `item/reasoning/textDelta`,
  `item/commandExecution/outputDelta`, `item/fileChange/patchUpdated` and
  `item/mcpToolCall/progress` (`app-server-protocol/src/protocol/common.rs:2039`
  to `:2087`). So live, an item is opened, grows by deltas and is closed; on
  disk only the closed item exists. Flotilla already drives Codex this way for
  input (the `codex-app-server` **Transport**, `flotilla: CONTEXT.md:515`).

## 3. Flotilla

Flotilla publishes no transcript and stores none.

- **Transcripts belong to the harness.** An **AgentSession** "owns" its
  transcript conceptually (`flotilla: CONTEXT.md:478`), but the hook event
  records only the path: "Native transcript location, recorded without reading
  or copying the file" (`flotilla: crates/flotilla-protocol/src/provider_data.rs:267`).
  The path is inside the vessel, so a host cannot read it directly.
- **Archiving session logs is deferred.** ADR 0028 makes "agent session logs"
  part of a convoy's durable essence, archived to object storage when a vessel
  is parked (`flotilla: docs/adr/0028-world-terminal-exits-and-the-vessel-as-cache.md:87`),
  and lists the machinery as deferred (`:190`). ADR 0059 defers object-storage
  logs to #2872 and #2889 (`docs/adr/0059-agent-sessions-processes-and-channels.md:239`).
- **Crews run in cleat sessions.** The daemon captures the screen only to
  classify attention and obstructions (`flotilla: crates/flotilla-daemon/src/runtime/terminal.rs:86`).
  Their recordings are on disk (section 4), but Flotilla does not serve them.
- **Artifacts** are blobs with a media type, served at `/v1/artifacts/content`:
  - `brief`, `text/markdown` (`flotilla: crates/flotilla-daemon/src/artifact.rs:107`);
  - `review-round`, `text/plain` (`:426`);
  - `build-log`, `text/plain`, kept 30 days (`crates/flotilla-daemon/src/image_build.rs:170`).
- **Messages** between roles have a free-text `body` with `sender`,
  `receiver`, `relation`, `in_reply_to` and `supersedes`
  (`flotilla: crates/flotilla-resources/src/message.rs:92`). An inbox is a
  thread, close to a chat, but it is not an agent transcript.
- **ADR 0067 tables** describe themselves with Frictionless Table Schema and
  stream `table`, `rows`, `synced`, then keyed `upsert`/`delete`, `progress`
  and `resync` (`flotilla: docs/adr/0067-tables-describe-themselves-on-the-wire.md`,
  decision 8). They are implemented as `/tables/{table}/rows` with `watch`
  (`flotilla: docs/table-http-api.md`; `crates/flotilla-protocol/src/table.rs:17`).
  The tables are stored kinds, the per-home event `log` of state-machine
  edges, and named queries over issues, checkouts and roles (decision 7). None
  carries transcript-like data. The stream vocabulary, though, is the
  snapshot-then-keyed-changes shape a Block feed needs.
- **The daemon's own log** (`~/.local/state/flotilla/daemon.log`) is plain
  text: 5.5 MB, 21,512 lines, no escape sequences.

## 4. ANSI logs and Cleat recordings

### Cleat's recording format

- A **Recording** is "the append-only log of a session's output (asciicast
  v3), optionally with VT snapshots for seek" (`cleat: CONTEXT.md:221`). One
  `session.cast` per session under
  `~/.local/state/cleat/<runtime>/sessions/<name>/`.
- First line is a header: `version`, `term` (`cols`, `rows`, `type`),
  `timestamp`, and `cleat` (version, build, engine)
  (`cleat: crates/cleat/src/asciicast.rs:14`). Then one event per line,
  `[time, code, data]`, with codes `o` output, `i` input, `r` resize, `m`
  marker, `x` exit, or custom (`asciicast.rs:31`). Custom codes `S`, `s`, `g`,
  `a`, `d` also occur.
- Cleat's history capture already turns the emulated screen and scrollback into
  cells with per-cell OSC 8 URIs, under per-capture budgets
  (`cleat: docs/scoped-history-capture.md`, `docs/terminal-hyperlinks.md`).

### What real recordings contain

996 recordings totalling 9.7 GB: size p50 513 bytes (empty dummy crews), p90
37 MB, max 240 MB. 398 are crew terminals (`terminal-convoy-*`, max 169 MB).

An 85-file sample, stratified by size, read up to 64 MB each:

| Size | Output chars | SGR | OSC 8 | Bare CR | Cursor moves | Newlines |
|---:|---:|---:|---:|---:|---:|---:|
| 13.8 MB | 5.5 M | 472,804 | 8 | 0 | 397,326 | 707 |
| 31.4 MB (crew) | 12.4 M | 1,013,518 | 48 | 0 | 741,037 | 25,189 |
| 56.7 MB | 19.1 M | 1,634,659 | 4 | 0 | 1,381,986 | 1,580 |
| 137.2 MB | 26.5 M | 2,315,224 | 16 | 0 | 2,015,082 | 2,329 |

- **The big recordings are agent TUIs, not logs.** Cursor-addressed moves
  outnumber newlines 100 to 1000 times. Turning them into text needs a VT
  engine; an ANSI-to-styled-runs parser would produce garbage. For these,
  Rich Text should take Cleat's history capture (cells, styles, links), or the
  agent's own transcript, not the byte stream.
- SGR parameters seen, by first parameter: `0`, `49`, `39`, `22`, `2`, `1` and
  `38` dominate (reset, default colours, dim, bold, extended foreground);
  `4`, `3`, `9`, `24`, `23`, `48` are rare. OSC 8 is present but rare.

### Captured log files

Every `*.log` over 10 kB under `~/.cache` and `~/.local/state/flotilla` (31
files: cargo and CI logs, benches, daemon logs) has **no SGR, no OSC 8 and no
bare CR**. Programs drop colour when stdout is not a terminal. The
`gh run view --log` output of a Wheelhouse CI run (573 kB, 4,967 lines) also
has none.

- Lines: median length 53 to 282 characters per file; maximum line 4,660
  characters, except `shpool/daemonized-shpool.log`, which is 31 lines with one
  line of **87.9 MB**.
- The longest file by lines is 21,512 lines (`daemon.log`). The largest Codex
  rollout is 19,240 lines and the largest Claude transcript 17,734, so 100k
  Blocks is a ceiling, not the norm, for any one source today.

### What an ANSI log source needs

Since styled logs come only from a PTY, an ANSI log source is a command's
output captured with a terminal (or with colour forced). It must handle:

- **SGR:** reset, bold, dim, italic, underline, strike, 16 and 256 colours and
  truecolor (`38;5`, `38;2`, `48;…`), default fg and bg (`39`, `49`);
- **OSC 8** hyperlinks, as link runs that come back as actions;
- **CR overwrite** within a line (progress bars), which replaces the tail
  Block's text rather than appending;
- **pathological lines:** an 88 MB line exists, so lines need a hard cap or
  split for layout;
- everything else (cursor moves, erase, alternate screen) is a sign the source
  is a TUI and belongs in a terminal View, not Rich Text.

## 5. Delivery, per source

| Source | On disk | Edits to earlier entries | Live channel |
|---|---|---|---|
| Claude Code | Append-only JSONL | None. Tool result is a later line keyed by `tool_use_id`; one message spans lines by `message.id`; metadata re-appended, latest wins | File tail; hooks give the path; `stream-json` with partial messages in print mode only |
| Codex | Append-only JSONL, later maybe `.jsonl.zst` | None. Output keyed by `call_id`; `ordinal` monotonic | File tail; app-server `item/started` → `…/delta` → `item/completed` |
| Flotilla | No transcripts. Artifacts are immutable blobs | Table rows upserted by key | ADR 0067 table stream (`watch`) over HTTP or WebSocket |
| Cleat recording | Append-only asciicast v3 | None in the file; the screen it paints is rewritten constantly | Cleat attach and render updates |
| ANSI line log | Append-only bytes | CR rewrites the current line | Pipe or PTY tail |

So, for the streaming ticket: from files, a Block is created complete except
for two cases. A tool call Block is created pending and updated once when its
result arrives. A Claude message may gain Blocks from following lines with the
same `message.id`. Token-level growth of the tail Block happens only on live
channels (Codex app-server, Claude `stream-json`) and on CR progress lines.

## Recommendation

The first slice should support three sources:

1. **Claude Code session JSONL.** It is the daily driver's transcript, tailable
   from a path that hooks already deliver, and it exercises the tool-call
   pairing, subagent links and compaction.
2. **Codex rollout JSONL.** It is the crews' harness. Its shapes differ from
   Claude's but map to the same Blocks, which tests the ruling of one Rich Text
   model for every source. Its app-server deltas are the natural first live
   stream when streaming lands.
3. **Line-oriented ANSI text** from a file or a command's PTY: SGR runs, OSC 8
   links and CR rewrites, with a line-length cap. It covers build and CI logs
   and Flotilla's `text/plain` artifacts, and it is the model for tool-result
   bodies inside the two transcripts.

Not in the first slice:

- **Cleat agent-TUI recordings:** they need VT emulation; take Cleat's history
  capture later if wanted.
- **Flotilla:** it has no transcripts to show. Its `text/markdown` briefs come
  through the markdown path already decided, and its table stream is a
  candidate transport once Flotilla archives session logs.

Block kinds these imply, beyond the markdown set (paragraph, heading, list,
quote, code, table) the web map already fixed:

- **Message:** a turn from a role (user, assistant, developer or system), whose
  body is markdown Blocks. Keyed by Claude `uuid` or Codex item id.
- **Reasoning:** collapsed by default. Usually empty or a summary only
  (Claude stores a signature with empty text; Codex stores
  `encrypted_content` and a `summary`).
- **Tool call:** name, a short input summary and a status (pending, done,
  failed, interrupted). Keyed by `tool_use_id` or `call_id`, and replaced once
  when the result lands. Its result body is one of:
  - **log output:** ANSI line text (Bash, `CommandExecution`);
  - **diff:** a patch (Claude `structuredPatch`, Codex `FileChange` or
    `apply_patch`), probably an **Embedded View** of the code view;
  - **image**;
  - **truncated marker** with a link to the full output (Claude
    `tool-results/`, Codex's 64 KiB cut).
- **Subagent:** a link to another transcript, opened as an **Embedded View**
  or a separate View, keyed by the spawning call.
- **Divider:** compaction boundaries, turn boundaries and aborted turns.
- **Log line or line group:** ANSI runs, the open question the map lists
  under "Not yet specified".
