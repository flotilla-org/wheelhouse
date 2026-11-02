# Research: what agent session logs contain, and how others present each kind

Status: research notes, 2 November 2026. Nothing here is decided. The
decisions taken from it will be recorded on
[the Rich Text map](https://github.com/flotilla-org/wheelhouse/issues/537) and
[#667](https://github.com/flotilla-org/wheelhouse/issues/667).

Question: [#666](https://github.com/flotilla-org/wheelhouse/issues/666). The
interim translator `tools/claude-to-events.py` (#615, merged as #620) turns
every non-assistant record into a user bubble. What kinds of record do real
logs hold, who produced each, and how should each present? How do AG-UI's
Claude Agent SDK integration and the Claude app handle them? Where do the
Claude app's tool-run summaries come from? This extends
[`rich-text-sources.md`](rich-text-sources.md) §1 and §2, which describe the
file layouts and record shapes; this note does not repeat them.

**Sources.**

- Claude Code transcripts: every `.jsonl` under `~/.claude/projects/` on this
  machine (211 files, 201 MB; 106 main sessions and 106 subagent files),
  written by Claude Code 2.1.283, 2.1.295 and 2.1.309. Entry points:
  `claude-desktop` (51,455 records), `sdk-cli` (1,086, Flotilla's one-shot
  probes), `cli` (4). Only keys, types, flags and counts were read by
  scripts; no message content is copied here. All examples below are
  synthetic.
- Claude Code CLI: the bundle the desktop app runs on this host,
  `~/.claude/remote/ccd-cli/2.1.309-537ebbf12df5` (a single-file executable
  with the JS embedded). Read with byte searches; minified names are not
  quoted as if stable. Written `cli:` below.
- Claude Agent SDK TypeScript reference,
  <https://code.claude.com/docs/en/agent-sdk/typescript>, read 2 November
  2026. Written `sdk-docs:`.
- AG-UI: `ag-ui-protocol/ag-ui` at `c0a4ae4fb5e7` (2 November 2026), directory
  `integrations/claude-agent-sdk/`. It pins `claude-agent-sdk==0.2.170`
  (Python) and `@anthropic-ai/claude-agent-sdk 0.3.306` (TypeScript). Paths
  below are relative to that directory.
- Codex: rollouts under `~/.codex/sessions/` (510 files, all plain `.jsonl`),
  and `openai/codex` at `adca2035762` (`main`, 2 November 2026). Paths are
  under `codex-rs/`.
- Running processes: the names (not values) of two `CLAUDE_CODE_*`
  environment variables of the desktop-spawned CLI processes on this host.

## Summary

- **A non-assistant record has at least nine producers.** In main sessions,
  of 713 text-bearing `user` items, 339 were typed by the user. The rest are
  task notifications (144), skill and command expansions (48), host-injected
  events (25), messages from other sessions (13), interrupt markers (9),
  compaction summaries (6) and the like. Another 98 are prompts that Flotilla
  sent through the SDK.
- **Claude Code now labels most of them.** User records carry `origin`
  (`human`, `task-notification`, `peer`, `coordinator`, ...), `promptSource`
  and `turnOrigin`, and `isMeta` marks harness text. The SDK documents
  `origin` as `SDKMessageOrigin`. Records without `origin` (older records,
  subagent briefs, host injections over the SDK) fall back to a leading tag:
  `<task-notification>`, `<command-name>`, `<system-reminder>`, and so on.
- **The translator misses typed prompts.** 26 prompts that the user typed
  while a turn was running were delivered as `attachment` records
  (`queued_command`, `humanTurn: true`), and the translator never reads
  attachments. 15,237 attachments in total; nearly all are model context to
  hide.
- **Images appear only in tool results:** 61 PNGs, all from `Read`, base64
  p50 35 kB, p90 181 kB, max 513 kB. 22 of 61 exceed the translator's 64 KiB
  cap, which would cut the base64 and corrupt the image. None in user
  messages on this machine.
- **AG-UI never shows harness-injected user text.** Its adapter takes user
  messages from the AG-UI client, and from the SDK stream it uses only
  assistant blocks, tool results, system messages (as Custom
  `system:<subtype>`) and the result. Every other SDK message type is
  dropped.
- **Tool-run summaries are model-generated, opt-in, and not persisted.** The
  CLI can call its small fast model after each tool batch, with a fixed
  prompt asking for a ~30-character, past-tense, "git-commit-subject" label
  "in a mobile app". It emits the label as an SDK `tool_use_summary` message
  (`summary`, `preceding_tool_use_ids`) and never writes it to the
  transcript. It is gated by `CLAUDE_CODE_EMIT_TOOL_USE_SUMMARIES`, and the
  desktop app on this host launches the CLI with it set to `false`. So the
  "ran 3 tools" lines in the desktop app most likely come from the app's own
  rules (unverified; the app is not installed on this machine).
- **Codex classifies at the source.** Its `UserMessage` turn item already
  excludes contextual fragments (`<environment_context>`, `# AGENTS.md`,
  `<subagent_notification>`, `<turn_aborted>`, skills, shell commands), and
  `developer` messages produce no turn item at all.

## 1. Claude Code

### 1.1 Record types by producer

Counts are over all 211 files. "Main" and "sub" split main sessions from
subagent transcripts where it matters.

| `type` | Count | Producer | Presentation |
|---|---:|---|---|
| `assistant` | 23,305 | agent | See §1.3 |
| `user` | 13,463 | mixed: see §1.2 | 12,578 carry `tool_result` blocks; the rest are classified in §1.2 |
| `attachment` | 15,237 | harness | Hidden, except `queued_command` (§1.4) |
| `system` / `stop_hook_summary` | 524 | harness (hooks) | Hidden; a notice only when `hookErrors` is non-empty or `preventedContinuation` is true |
| `system` / `compact_boundary` | 9 | harness | Notice ("Conversation compacted"); `compactMetadata` has `trigger`, `preTokens`, `postTokens`, `durationMs` |
| `system` / `informational` | 3 | harness | Notice (`level: notice`) |
| `system` / `local_command` | 3 | harness, for a user's local slash command | Command output, attached to the command chip |
| `system` / `api_error` | 1 | harness | Error notice (`level: error`), with `retryAttempt`, `maxRetries` |
| `file-history-snapshot`, `file-history-delta` | 428 | harness | Hidden; links to backups outside the file (§1.6) |
| `custom-title`, `agent-name`, `last-prompt`, `mode`, `permission-mode`, `cost-state`, `queue-operation` | 6,459 | harness | Hidden; the latest `custom-title` is the session title |
| `pr-link`, `frame-link`, `bridge-session`, `atis-latch`, `artifact-*` | 5,322 | host app | Hidden; vary by host and version |
| `api-request`, `api-request-blob`, `api-request-shape` | 3,366 | harness | Hidden. `api-request-blob` holds request copies, including 29 more image copies |

### 1.2 `user` records with text

Each text item is classified by flags first, then by its leading tag.
"Origin" is `origin.kind`; `-` means the field is absent.

| Class | Main | Sub | Signals | Producer | Presentation |
|---|---:|---:|---|---|---|
| Typed prompt | 339 | 0 | `origin.kind: human`, `turnOrigin: human`, `promptSource: sdk` (the desktop app is an SDK host) | user | **User bubble** |
| Reminder block before a typed prompt | 20 | 0 | Same record as a typed prompt: content is `[text <system-reminder>…, text typed]` | harness | **Hidden**; the bubble shows only the typed block |
| SDK prompt from a program | 98 | 0 | No `origin`; `promptSource`/`turnOrigin: sdk`; entry point `sdk-cli` (Flotilla's probes) | other program | User bubble. The host decides who "the user" is; nothing marks it as a program |
| Subagent brief | 0 | 106 | First `user` record of each subagent file; no flags | agent (the parent's `Agent` call `prompt`) | **User bubble in the subagent thread**, styled as the brief; the parent already shows it as the call's input |
| Task notification | 144 | 8 | `origin.kind: task-notification` (often `producer: session-task`), `turnOrigin: task_notification`; text starts `<task-notification>` with `<task-id>`, `<tool-use-id>`, `<status>`, `<summary>`, `<result>`, `<usage>`, `<output-file>` | harness, reporting a background task or subagent | **Notice**, linked to the tool call named in `<tool-use-id>`; `<result>` collapsed |
| Skill or command expansion | 29 | 65 | `isMeta: true`; text starts `Base directory for this skill:` | skill | **Hidden**, or collapsed under the command chip |
| Slash command | 19 | 0 | Text starts `<command-message>` or `<command-name>`, with `<command-args>` | user | **Command chip** (`/name args`) |
| Local command caveat | 4 | 0 | `isMeta`; `<local-command-caveat>` | harness | Hidden |
| Local command output | 1 | 0 | `<local-command-stdout>` | harness | Output under the command chip |
| Host-injected event | 25 | 0 | No `origin`; `turnOrigin: sdk`; text is `<system-reminder>` wrapping `<side-sessions-event>` (19), or `<ci-monitor-event>` (6) | host app | **Notice**, collapsed |
| Message from another session | 13 | 0 | `origin.kind: peer` with `from`, `name`, `fromSession`; `isMeta`; text contains `<cross-session-message>` or `<agent-message>` | other session | **Attributed bubble** (sender name), not the user's |
| Coordinator message | 0 | 3 | `origin.kind: coordinator`, `isMeta` | other agent | Attributed bubble |
| Interrupt marker | 9 | 2 | Text starts `[Request interrupted` | harness | Ends the run as interrupted; no bubble (the translator already does this) |
| Compaction summary | 6 | 3 | `isCompactSummary: true` | harness (model-written) | **Collapsed, under the compaction notice** |
| Other `isMeta` text | 6 | 7 | `isMeta` with no tag, or (sub) a lone `<system-reminder>` | harness | Hidden |

Notes:

- **`origin` is documented.** `SDKUserMessage.origin?: SDKMessageOrigin`, a
  union of `human`, `channel` (`server`), `peer` (`from`, `fromMode`,
  `name`, `fromSession`, `senderTaskId`, `body`, ...), `task-notification`
  (`subkind`, `fireReason`, `runId`), `coordinator`, `auto-continuation` and
  `unclassified` (sdk-docs, `SDKMessageOrigin`). The CLI's schema adds
  `observer` and `observer-activity`, and says: "A host wrapping keyboard
  input must stamp {kind:'human'} explicitly" (cli: zod schema for
  `origin`). `unclassified` is "never presumed human" (same).
- **The CLI uses the same split itself.** A function that renders a
  transcript for one of its helper prompts maps each record to
  `user_message` or `non_user_message` (with a `source`) from `origin` and
  `isMeta`, unwraps `queued_command` attachments the same way, and turns
  `tool_use` and `tool_result` into `tool_call` and `tool_result` items
  (cli: near the `tool_use_summary_generation` prompt).
- **Tags are the fallback,** for records without `origin`: older versions,
  subagent files and host injections. Tags seen anywhere in user text:
  `task-notification` (153), `system-reminder` (45), `command-name` (19),
  `command-message` (19), `side-sessions-event` (15), `ci-monitor-event`
  (9), `cross-session-message` (8), `agent-message` (5),
  `local-command-caveat` (4).
- **One record can mix classes.** The 20 human records with a separate
  reminder block show that the unit of classification is the text block,
  not the record.

Synthetic example of a typed prompt and its injected companion:

```json
{"type":"user","origin":{"kind":"human"},"turnOrigin":"human","message":{"role":"user","content":[
  {"type":"text","text":"<system-reminder>\nToday's date is 2026-11-02.\n</system-reminder>"},
  {"type":"text","text":"Why does the parser drop the last line?"}]}}
```

### 1.3 `assistant` content blocks

| Block | Count | Producer | Presentation |
|---|---:|---|---|
| `tool_use` | 12,640 | agent | Tool call (#667) |
| `thinking` | 8,672 | agent | Reasoning, only when the text is non-empty (it is usually empty); never the signature |
| `text` | 2,095 | agent | Assistant bubble (Markdown) |

- No `<synthetic>`-model records were present on this machine.
- Tools by count: `Bash` 11,387, `Write` 315, `Read` 238, MCP 203, `Edit` 131,
  `Agent` 106, `Skill` 82, `WebFetch` 81, `ToolSearch` 32, `SendMessage` 20,
  then `Artifact`, `WebSearch`, `Monitor`, `SubagentHandback`,
  `AskUserQuestion`, `TaskStop`, `ListAgents`, `SendUserFile`.
- **44% of `Bash` calls (4,968 of 11,387) carry a model-written
  `description`.** It is a ready short title for a call or a run.
- **Runs:** in main sessions, the number of tool calls between two pieces of
  assistant text or user prompts is p50 2, p90 6, p99 11, max 22, over 1,307
  runs; 447 runs are a single call.

### 1.4 Attachments

Each `attachment` record has `attachment.type`. Most also carry `rendered`
(the text the model sees, usually a `<system-reminder>`) and `renderedRole`
(`system` or `user`).

| `attachment.type` | Count | Producer | Presentation |
|---|---:|---|---|
| `total_tokens_reminder`, `silent_turn_reminder`, `date`, `model`, `environment`, `session_context`, `credential_org`, `auto_mode`, `auto_mode_exit` | 11,516 | harness | Hidden |
| `deferred_tools_record`, `deferred_tools_delta`, `mcp_instructions_delta`, `skill_listing`, `agent_listing_delta`, `command_permissions`, `invoked_skills` | 2,908 | harness | Hidden |
| `prompt_snapshot` | 438 | harness | Hidden (system prompt and tools as sent) |
| **`queued_command`** | 131 | user (26, `origin.kind: human`, `humanTurn: true`), task notification (97), coordinator (4), peer (3) | **The human ones are user bubbles.** They are prompts typed mid-turn and folded into the running turn. Others as in §1.2 |
| `edited_text_file` | 102 | harness (the user or a tool changed a file) | Hidden, or a small "file changed" chip |
| `instructions` | 49 | harness (CLAUDE.md and similar loaded) | Hidden |
| `file`, `compact_file_reference` | 44 | user (@-mention) or harness | Attachment chip with the file name |
| `diagnostics` | 26 | harness (IDE or LSP) | Hidden, or a notice when new |
| `remote_session_change` | 14 | host app | Hidden |
| `hook_additional_context`, `hook_system_message` | 12 | hook | Hidden; a notice for `hook_system_message` |
| `batching_reminder_sent`, `thinking_drop`, `read_truncation_notice`, `task_status` | 23 | harness | Hidden |

### 1.5 Images

| Where | Count | Sizes (base64 characters) | Notes |
|---|---:|---|---|
| `tool_result` content, `image` blocks | 61 (59 main, 2 sub) | p50 34,868; p90 180,816; max 512,832; total 5.3 MB | All `image/png`, all `source.type: base64`, all from `Read` |
| Same images, in `toolUseResult.file.base64` | 59 | 4.3 MB total | A second copy, with `dimensions` and `originalSize` |
| `api-request-blob` records | 29 | | A third copy, inside request blobs |
| User messages | 0 | | None on this machine. Claude Code accepts pasted images as `image` blocks; `history.jsonl` has `pastedContents` |

- **22 of the 61 are over 64 KiB of base64.** The translator passes an
  image through the same 64 KiB `cap()` as text, so it would emit a cut,
  undecodable image. Images need their own limit, or a reference.
- A tool result that mixes text and images (`content` a list of `text` and
  `image` blocks) is not handled by `sole_image()`; none were seen, but the
  shape is legal.

### 1.6 Outside the `.jsonl`

| Location | Contents here | Keyed by |
|---|---|---|
| `<project>/<session>/subagents/agent-<id>.jsonl` | 106 transcripts | `meta.json` `toolUseId` |
| `<project>/<session>/subagents/agent-<id>.meta.json`, `.prefix.json` | 211 files | |
| `<project>/<session>/tool-results/*.txt` | 70 spills: p50 36 kB, max 150 kB, 2.9 MB total. 71 results carry the `<persisted-output>` preview | The path in the preview |
| `<project>/<session>/custom-title.json` | 6 | |
| `<project>/memory/*.md` | 44 auto-memory notes across 8 projects | Per project, not per session |
| `~/.claude/file-history/<session>/<hash>@v<n>` | 115 backups, 2.3 MB | `file-history-snapshot` (`snapshot.trackedFileBackups`) and `file-history-delta` (`trackingPath`, `backup`) |
| `~/.claude/history.jsonl` | 498 prompt-history entries: `display`, `pastedContents`, `timestamp`, `project`, `sessionId` | Session id |
| `~/.claude/sessions/<pid>.json` | Live session registry: `sessionId`, `pid`, `status`, `name`, `messagingSocketPath`, `peerProtocol`, ... | pid |

No tool-run summary, session summary or app-side presentation state was
found in any of these.

## 2. Codex rollouts

`response_item` messages carry the model's view and `item_completed` events
carry Codex's typed `TurnItem`s (`rich-text-sources.md` §2). Counts over all
510 rollouts; "sub" is a rollout whose `session_meta.source` is a subagent.

| Record | Main | Sub | Producer | Presentation |
|---|---:|---:|---|---|
| `message` `user`, plain text | 1,686 | 8,710 | user (main); parent agent (sub) | User bubble; in a subagent, the brief |
| `message` `user`, `<subagent_notification>` | 244 | 6,315 | harness | Notice |
| `message` `user`, `<environment_context>` | 309 | 656 | harness | Hidden |
| `message` `user`, `# AGENTS.md …` | 242 | 333 | harness | Hidden |
| `message` `user`, `<turn_aborted>` | 135 | 543 | harness | Ends the run as interrupted |
| `message` `user`, `<skill>` | 43 | 432 | skill | Hidden, or a chip |
| `message` `user`, `<user_shell_command>` | 11 | 51 | user (`!` command) | Command chip with output |
| `message` `developer` (`<collaboration_mode>`, `<skills_instructions>`, `<apps_instructions>`, `<plugins_instructions>`, `<multi_agent_mode>`, `<model_switch>`, plain) | 1,837 | 2,134 | harness | Hidden |
| `item_completed` `UserMessage` | 1,605 | 8,710 | user | User bubble |
| `item_completed` `ContextCompaction` | 1,257 in all | | harness | Compaction notice |
| Images | 12 in all | | tool (`function_call_output` lists with `input_image` data URLs): p50 200 kB, max 5.9 MB | Image result |

- **Codex already separates them.** `parse_turn_item` returns a `UserMessage`
  only when `is_contextual_user_message_content` is false
  (`core/src/event_mapping.rs:127`). It returns nothing for the `system` role
  (`:211`), nor, through the fallback arm, for `developer` (`:212`). The
  contextual matchers are
  `UserInstructions`, `EnvironmentsState`, `AdditionalContextUserFragment`,
  `AgentMessageBoardNotification`, skill prompts, `UserShellCommand`,
  `TurnAborted`, `SubagentNotification`, `InternalModelContextFragment` and
  legacy warnings (`core/src/context/contextual_user_message.rs:24`). Hook
  prompts become a separate `HookPrompt` item.
- On disk, `UserMessage` items (10,315) hold only plain user text, plus one
  `skill` input. So **a Codex translator should read `TurnItem`s and ignore
  `response_item` messages**, and needs no tag heuristics.
- Inside a subagent rollout, the parent's brief is still a `UserMessage`;
  only `session_meta.source` tells a reader it came from an agent.

## 3. AG-UI's Claude Agent SDK integration

AG-UI's adapter drives a live SDK session. It does not read transcripts for
display; its `native_history.py` reads them only to recover unanswered tool
calls after a crash, and it accepts only CLI version 2.1.306
(`python/ag_ui_claude_sdk/native_history.py:77`).

| SDK input | AG-UI output | Source |
|---|---|---|
| User prompt | Comes from the client's `RunAgentInput.messages`, not the SDK stream. The adapter emits no user TextMessage | `python/ag_ui_claude_sdk/utils.py:290` |
| `stream_event` `text_delta` | `TEXT_MESSAGE_START` / `CONTENT` / `END`, role `assistant`, id = API `message.id` | `python/ag_ui_claude_sdk/adapter.py:1033` |
| `stream_event` thinking block | `REASONING_START`, `REASONING_MESSAGE_START` (role `reasoning`), content, end; the signature as `REASONING_ENCRYPTED_VALUE` | `adapter.py:1100`, `:1157` |
| `stream_event` `tool_use` block | `TOOL_CALL_START` (MCP prefix stripped from the name), `TOOL_CALL_ARGS` deltas, `TOOL_CALL_END` | `adapter.py:1118`, `:1215` |
| `Task`/`Agent` tool call | Also `ACTIVITY_SNAPSHOT`, type `claude-subagent-task`; its result closes it with an `ACTIVITY_DELTA` (`status` `completed` or `failed`) | `python/ag_ui_claude_sdk/handlers.py:62`, `:404` |
| Complete `AssistantMessage` | `CUSTOM` `claude:assistant_content` with the raw blocks; a snapshot message with text and tool calls, thinking left out | `adapter.py:1281`; `utils.py:557` |
| `UserMessage` `tool_result` | `TOOL_CALL_RESULT`, role `tool`; `rawEvent` carries `parent_tool_use_id` (subagent nesting) and `tool_use_result` (the structured side copy) | `handlers.py:332` |
| Tool result with images | Content parts `{type: image, source: {type: data, value, mimeType}}`; unknown block kinds become JSON text with `unsupportedType` | `utils.py:669` |
| `UserMessage` text blocks (task notifications, peer messages, reminders) | **Nothing** | `adapter.py:1297` handles only tool blocks |
| `SystemMessage` (any subtype, including `compact_boundary`) | `CUSTOM` `system:<subtype>` with the raw data | `adapter.py:1327` |
| `ResultMessage` | Run metadata; when nothing streamed, the result text as an assistant message; `RUN_ERROR` when `is_error` | `adapter.py:1338` |
| Any other SDK type (`tool_use_summary`, `tool_progress`, `task_*`, `rate_limit_event`, ...) | **Dropped**: no branch | `typescript/src/adapter.ts:900` to `:1260` (same in TypeScript) |

The TypeScript adapter matches the Python one branch for branch
(`typescript/src/adapter.ts:1118` assistant, `:1180` user, `:1229` system,
`:1260` result).

**Not run live.** The integration consumes a live SDK stream, so showing it a
real session would mean resuming that session, which sends its content to
the API. The brief rules that out. A synthetic run would only confirm the
mapping above, which the source states directly.

## 4. Tool-run summaries in the Claude app

### What the CLI does

- **Generator.** After a batch of tool calls finishes, the query loop checks
  `gates.emitToolUseSummaries`, then that the batch is non-empty, the turn
  was not aborted, and the loop is not a subagent (`!agentId`). It then
  starts a summary request without awaiting it. The pending promise is
  carried into the next iteration as `pendingToolUseSummary`, so the
  summary arrives while the next model response streams (cli: query loop,
  `query_tool_execution_end`).
- **Model call.** A helper call with `querySource:
  "tool_use_summary_generation"`, no tools, thinking disabled, no prompt
  caching. It runs on the CLI's small fast model, the one
  `ANTHROPIC_SMALL_FAST_MODEL` overrides (cli: the helper that serves it).
  Which model id that resolves to by default is **unverified**; a
  third-party design note says Haiku
  ([Qwen Code](https://qwenlm.github.io/qwen-code-docs/en/design/tool-use-summary/tool-use-summary-design/)).
- **Prompt** (cli, quoted):

  > Write a short summary label describing what these tool calls
  > accomplished. It appears as a single-line row in a mobile app and
  > truncates around 30 characters, so think git-commit-subject, not
  > sentence.
  >
  > Keep the verb in past tense and the most distinctive noun. Drop
  > articles, connectors, and long location context first.

  The prompt gives examples such as `Searched in auth/`, `Read config.json` and
  `Ran failing tests`. The user part is recent user context, then
  "User's intent (from assistant's last message)" cut to 200 characters,
  then each tool's name, input and output, each cut to 300 characters,
  ending in `Label:`.
- **Output.** An SDK message `{type: "tool_use_summary", summary,
  preceding_tool_use_ids, uuid, session_id}` (cli: zod schema; the type is
  `SDKToolUseSummaryMessage` in the `SDKMessage` union, sdk-docs). The
  interactive terminal renderer ignores it, and the transcript writer skips
  it, which matches its absence from every `.jsonl` here.
- **Gate.** `CLAUDE_CODE_EMIT_TOOL_USE_SUMMARIES` (cli: environment
  registry). The CLI processes the desktop app launched on this host run
  with `CLAUDE_CODE_EMIT_TOOL_USE_SUMMARIES=false` and
  `CLAUDE_CODE_ENTRYPOINT=claude-desktop`.

### What the desktop app shows

- **This machine has no desktop app.** The app runs on another host and
  drives this one through `~/.claude/remote/` (a server binary, a run
  directory with an RPC socket, and versioned CLI bundles). There is no
  `~/.config/Claude`, and `~/.local/share/claude` and `~/.cache/claude*`
  hold only CLI versions and caches. So its storage could not be inspected.
- **Since the app turns the summary gate off,** its "ran 3 tools" rows are
  presumably built by the app from the tool calls themselves:
  **rule-based, unverified.** The "better" rows may come from per-tool
  labels the CLI already has. Every tool defines `getToolUseSummary` and
  `getActivityDescription`: Bash returns its `description` input or a
  shortened command; Read returns "Reading <file>"; WebSearch returns
  "Searching for <query>" (cli: tool definitions). The CLI sends these with
  permission requests. Whether the app uses them for run rows is
  **unverified**.
- The prompt's "mobile app" wording suggests the generated labels serve the
  mobile and remote clients (**inference**).
- **A different, session-level summary exists:** a classifier that sets
  `task_summary` session metadata and a headline (cli: `onClassified`,
  `notifyMetadataChanged({task_summary})`). It describes the session, not a
  tool run.

## 5. Recommendations

### Translator classes

Classify by text block, using flags first and tags second. Every class maps
onto events the contract already has (#548, #615), plus one optional field.

| Class | Detect | Contract events |
|---|---|---|
| `prompt` | `origin.kind == human`; or no `origin`, not `isMeta`, no leading tag; or a `queued_command` attachment with `origin.kind == human` | `TextMessage` role `user` |
| `brief` | First user text in a subagent file | `TextMessage` role `user` in the subagent thread (the thread's `parent` already says who sent it) |
| `peer` | `origin.kind` in `peer`, `channel`, `coordinator`, `observer` | `TextMessage` role `user` plus a new optional `origin` field on `TextMessageStart` (`{kind, name?, from?}`, mirroring `SDKMessageOrigin`), so the renderer can attribute it. Readers that skip unknown fields fall back to a user bubble |
| `notification` | `origin.kind == task-notification`, or `<task-notification>` | `Custom` `taskNotification` `{taskId, toolCallId, status, summary, result?}`, with `toolCallId` linking it to the call |
| `command` | `<command-name>` / `<command-message>` | `Custom` `command` `{name, args}`; a following `<local-command-stdout>` or `system`/`local_command` becomes `Custom` `commandOutput` |
| `expansion` | `isMeta` skill base, `<local-command-caveat>` | Not emitted (or `Raw`, when a debug view wants it) |
| `notice` | Host events (`<ci-monitor-event>`, `<side-sessions-event>` inside a reminder), `system` `informational` / `api_error`, `stop_hook_summary` with errors | `TextMessage` role `system`, collapsed by default |
| `context` | `<system-reminder>`-only blocks, other `isMeta`, all other attachments | Not emitted |
| `compaction` | `compact_boundary` plus the next `isCompactSummary` record | Existing `Custom` `compaction`, with the summary text added to its value |
| `interrupt` | `[Request interrupted` | `RunFinished` `outcome: interrupted` (unchanged) |

Also:

- Read `queued_command` attachments; this is the one change that recovers
  lost user text.
- Give images their own size limit, separate from the 64 KiB text cap, or
  emit a `truncated` reference instead of cut base64.
- For Codex later: map `TurnItem`s directly. `UserMessage` → `prompt`;
  `<subagent_notification>` → `notification`; `<user_shell_command>` →
  `command`. Everything else is context.

### For #667: tool calls and run summaries

- **Rules are enough for the summary line, and come first.** Runs are short
  (p50 2 calls, p90 6, max 22). A rule-based line ("Ran 4 commands, edited 2
  files, 1 failed") covers them. For a single call, or a run of one kind,
  use the call's own title: Bash's `description` (present on 44% of calls),
  the path, or the query. That is what Claude Code's per-tool
  `getToolUseSummary` does.
- **A model summary, if wanted, should copy Claude Code's design.**
  - Generate it after a batch completes, off the critical path, from names
    plus inputs and outputs cut to 300 characters.
  - Keep it short, in the past tense.
  - Deliver it as an event keyed by the preceding tool call ids.

  In the contract, that is `Custom` `toolRunSummary` `{summary,
  toolCallIds}`, which a fold can attach to the group. Claude Code does not
  persist it. If Wheelhouse wants it stable across reloads, it must store
  the summary itself, keyed by the tool call ids, since the native log will
  never contain it.
- **Grouping:** a run is the consecutive tool calls between assistant text,
  user prompts and notices. `notification` events for background tasks
  should attach to their originating call (via `toolCallId`), not break or
  join runs.
- **SDK-fed sources get summaries for free.** A live source on the SDK
  stream with `CLAUDE_CODE_EMIT_TOOL_USE_SUMMARIES` set would emit
  `tool_use_summary` messages. A translator from that stream can map them to
  the same `Custom` event, at the cost of a small-model call per batch on the
  user's account.
